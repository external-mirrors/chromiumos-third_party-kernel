// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2023-2024 Google LLC.
 * Author: Paweł Anikiel <panikiel@google.com>
 */

#include <linux/string.h>
#include "intel-dprx-sink.h"

#define MT_GET_MESSAGE_TRANSACTION_VERSION 0x00
#define MT_LINK_ADDRESS		0x01
#define MT_CONNECTION_STATUS_NOTIFY 0x02
#define MT_ENUM_PATH_RESOURCES	0x10
#define MT_ALLOCATE_PAYLOAD	0x11
#define MT_QUERY_PAYLOAD	0x12
#define MT_RESOURCE_STATUS_NOTIFY 0x13
#define MT_CLEAR_PAYLOAD_ID_TABLE 0x14
#define MT_REMOTE_DPCD_READ	0x20
#define MT_REMOTE_DPCD_WRITE	0x21
#define MT_REMOTE_I2C_READ	0x22
#define MT_REMOTE_I2C_WRITE	0x23
#define MT_POWER_UP_PHY		0x24
#define MT_POWER_DOWN_PHY	0x25
#define MT_SINK_EVENT_NOTIFY	0x30
#define MT_QUERY_STREAM_ENCRYPTION_STATUS 0x38

#define MT_NAK 0x80
#define MT_WRITE_FAILURE	0x01
#define MT_INVALID_RAD		0x02
#define MT_CRC_FAILURE		0x03
#define MT_BAD_PARAM		0x04
#define MT_DEFER		0x05
#define MT_LINK_FAILURE		0x06
#define MT_NO_RESOURCES		0x07
#define MT_DPCD_FAIL		0x08
#define MT_I2C_NAK		0x09
#define MT_ALLOCATE_FAIL	0x0a

#define AUX_ACK 0x0
#define AUX_I2C_NACK 0x4

struct aux_msg {
	u8 cmd;
	u32 addr;
	u8 len;
	u8 data[16];
};

static int dp_sink_i2c_read(struct dp_sink *sink, u8 addr, u8 *buf, int len)
{
	int offset;

	if (addr == 0x50) {
		offset = sink->offset + sink->segment * 256;
		if (len + offset > sink->blocks * 128)
			return -1;
		memcpy(buf, sink->edid + offset, len);
		sink->offset += len;
	} else if (addr == 0x30) {
		if (len == 1)
			buf[0] = sink->segment;
		else if (len > 1)
			return -1;
	}

	return 0;
}

static int dp_sink_i2c_write(struct dp_sink *sink, u8 addr, u8 *buf, int len)
{
	if (addr == 0x50) {
		if (len == 1)
			sink->offset = buf[0];
		else if (len > 1)
			return -1;
	} else if (addr == 0x30) {
		if (len == 1)
			sink->segment = buf[0];
		else if (len > 1)
			return -1;
	} else {
		return -1;
	}

	return 0;
}

static int port_number_to_sink_index(u8 port_number)
{
	if (port_number < 8)
		return -1;

	return port_number - 8;
}

static u8 sink_index_to_port_number(int sink_index)
{
	return sink_index + 8;
}

static void msg_transfer_write_nak(struct dp_sink_device *sink_dev,
				   struct msg_transaction *rep,
				   u8 ident, u8 reason)
{
	rep->buf[0] = MT_NAK | ident;
	memcpy(&rep->buf[1], sink_dev->dpcd.guid, 16);
	rep->buf[17] = reason;
	rep->buf[18] = 0;
	rep->len = 19;
}

static void execute_link_address(struct dp_sink_device *sink_dev,
				 struct msg_transaction *req,
				 struct msg_transaction *rep)
{
	int ports = sink_dev->sink_count + 1;
	u8 port_number;
	u8 *buf;
	int i;

	rep->buf[0] = MT_LINK_ADDRESS;
	memcpy(rep->buf + 1, sink_dev->dpcd.guid, 16);
	rep->buf[17] = ports;
	/* port 0 */
	rep->buf[18] = 0x90; /* input, source device, port 0 */
	rep->buf[19] = 0x40; /* no msg, connected */

	buf = rep->buf + 20;
	for (i = 0; i < sink_dev->sink_count; i++) {
		port_number = sink_index_to_port_number(i);
		if (sink_dev->sinks[i].blocks > 0) {
			buf[0] = 0x30 | port_number; /* output, sink device, port number */
			buf[1] = 0x40; /* no msg, connected */
		} else {
			buf[0] = port_number; /* output, no device, port number */
			buf[1] = 0x00; /* no msg, disconnected */
		}
		buf[2] = 0x00; /* DPCD 0 */
		memset(buf + 3, 0, 16); /* GUID */
		buf[19] = 0x00; /* 0 SDP streams, 0 SDP stream sinks */
		buf += 20;
	}
	rep->len = ports * 20;
}

static void execute_enum_path_resources(struct dp_sink_device *sink_dev,
					struct msg_transaction *req,
					struct msg_transaction *rep)
{
	u8 port;

	port = req->buf[1] >> 4;

	sink_dev->total_pbn = sink_dev->dpcd.link_conf[0] *
			sink_dev->dpcd.link_conf[1] * 32;

	rep->buf[0] = MT_ENUM_PATH_RESOURCES;
	rep->buf[1] = port << 4;
	rep->buf[2] = sink_dev->total_pbn >> 8;
	rep->buf[3] = sink_dev->total_pbn & 0xff;
	rep->buf[4] = (sink_dev->total_pbn - sink_dev->sum_pbn) >> 8;
	rep->buf[5] = (sink_dev->total_pbn - sink_dev->sum_pbn) & 0xff;
	rep->len = 6;
}

static void execute_allocate_payload(struct dp_sink_device *sink_dev,
				     struct msg_transaction *req,
				     struct msg_transaction *rep)
{
	u8 port;
	u8 id;
	u16 pbn;
	int sink_index;

	port = req->buf[1] >> 4;
	id = req->buf[2] & 0x7f;
	pbn = req->buf[3] << 8 | req->buf[4];

	sink_index = port_number_to_sink_index(port);
	if (sink_index == -1 || sink_index >= sink_dev->sink_count) {
		msg_transfer_write_nak(sink_dev, rep, MT_ALLOCATE_PAYLOAD, MT_BAD_PARAM);
		return;
	}

	sink_dev->vc_id[sink_index] = id;
	intel_dprx_set_vc_ids(sink_dev->dprx, sink_dev->vc_id);

	rep->buf[0] = MT_ALLOCATE_PAYLOAD;
	rep->buf[1] = port << 4;
	rep->buf[2] = id;
	rep->buf[3] = pbn >> 8;
	rep->buf[4] = pbn & 0xff;
	rep->len = 5;
}

static void execute_clear_payload_id_table(struct dp_sink_device *sink_dev,
					   struct msg_transaction *req,
					   struct msg_transaction *rep)
{
	intel_dprx_clear_vc_payload_table(sink_dev->dprx);
	rep->buf[0] = MT_CLEAR_PAYLOAD_ID_TABLE;
	rep->len = 1;
}

static void execute_remote_dpcd_read(struct dp_sink_device *sink_dev,
				     struct msg_transaction *req,
				     struct msg_transaction *rep)
{
	u8 port;
	u32 addr;
	int num_bytes;
	int sink_index;
	int i;

	port = req->buf[1] >> 4;
	addr = (req->buf[1] & 0xf) << 16 | req->buf[2] << 8 | req->buf[3];
	num_bytes = req->buf[4];

	sink_index = port_number_to_sink_index(port);
	if (sink_index == -1 || sink_index >= sink_dev->sink_count) {
		msg_transfer_write_nak(sink_dev, rep, MT_REMOTE_DPCD_READ, MT_BAD_PARAM);
		return;
	}

	rep->buf[0] = MT_REMOTE_DPCD_READ;
	rep->buf[1] = port;
	rep->buf[2] = num_bytes;
	for (i = 0; i < num_bytes; i++)
		rep->buf[3+i] = 0;
	rep->len = 3 + num_bytes;
}

static void execute_remote_i2c_read(struct dp_sink_device *sink_dev,
				    struct msg_transaction *req,
				    struct msg_transaction *rep)
{
	u8 *req_buf = req->buf;
	struct dp_sink *sink;
	u8 port;
	int sink_index;
	int num_write_transactions;
	u8 addr;
	int len;
	int res;
	int i;

	port = req_buf[1] >> 4;
	sink_index = port_number_to_sink_index(port);
	if (sink_index == -1 || sink_index >= sink_dev->sink_count) {
		msg_transfer_write_nak(sink_dev, rep, MT_REMOTE_I2C_READ, MT_BAD_PARAM);
		return;
	}

	sink = &sink_dev->sinks[sink_index];

	num_write_transactions = req_buf[1] & 0x3;
	req_buf += 2;
	for (i = 0; i < num_write_transactions; i++) {
		addr = req_buf[0] & 0x7f;
		len = req_buf[1];
		res = dp_sink_i2c_write(sink, addr, &req_buf[2], len);
		if (res) {
			msg_transfer_write_nak(sink_dev, rep, MT_REMOTE_I2C_READ, MT_I2C_NAK);
			return;
		}
		req_buf += len + 3;
	}
	addr = req_buf[0] & 0x7f;
	len = req_buf[1];

	rep->buf[0] = MT_REMOTE_I2C_READ;
	rep->buf[1] = port;
	rep->buf[2] = len;
	res = dp_sink_i2c_read(sink, addr, rep->buf + 3, len);
	if (res) {
		msg_transfer_write_nak(sink_dev, rep, MT_REMOTE_I2C_READ, MT_I2C_NAK);
		return;
	}
	rep->len = len + 3;
}

static void execute_power_up_phy(struct dp_sink_device *sink_dev,
				 struct msg_transaction *req,
				 struct msg_transaction *rep)
{
	u8 port;

	port = req->buf[1] >> 4;

	rep->buf[0] = MT_POWER_UP_PHY;
	rep->buf[1] = port << 4;
	rep->len = 2;
}

static void msg_transaction_execute(struct dp_sink_device *sink_dev,
				    struct msg_transaction *req,
				    struct msg_transaction *rep)
{
	u8 ident = req->buf[0] & 0x7f;

	switch (ident) {
	case MT_LINK_ADDRESS:
		execute_link_address(sink_dev, req, rep);
		break;
	case MT_ENUM_PATH_RESOURCES:
		execute_enum_path_resources(sink_dev, req, rep);
		break;
	case MT_ALLOCATE_PAYLOAD:
		execute_allocate_payload(sink_dev, req, rep);
		break;
	case MT_CLEAR_PAYLOAD_ID_TABLE:
		execute_clear_payload_id_table(sink_dev, req, rep);
		break;
	case MT_REMOTE_DPCD_READ:
		execute_remote_dpcd_read(sink_dev, req, rep);
		break;
	case MT_REMOTE_I2C_READ:
		execute_remote_i2c_read(sink_dev, req, rep);
		break;
	case MT_POWER_UP_PHY:
		execute_power_up_phy(sink_dev, req, rep);
		break;
	default:
		msg_transfer_write_nak(sink_dev, rep, ident, MT_BAD_PARAM);
		break;
	}
}

/* Taken from drivers/gpu/drm/display/drm_dp_mst_topology.c */

static u8 get_hdr_crc4(const uint8_t *data, size_t num_nibbles)
{
	u8 bitmask = 0x80;
	u8 bitshift = 7;
	u8 array_index = 0;
	int number_of_bits = num_nibbles * 4;
	u8 remainder = 0;

	while (number_of_bits != 0) {
		number_of_bits--;
		remainder <<= 1;
		remainder |= (data[array_index] & bitmask) >> bitshift;
		bitmask >>= 1;
		bitshift--;
		if (bitmask == 0) {
			bitmask = 0x80;
			bitshift = 7;
			array_index++;
		}
		if ((remainder & 0x10) == 0x10)
			remainder ^= 0x13;
	}

	number_of_bits = 4;
	while (number_of_bits != 0) {
		number_of_bits--;
		remainder <<= 1;
		if ((remainder & 0x10) != 0)
			remainder ^= 0x13;
	}

	return remainder;
}

static u8 get_body_crc4(const uint8_t *data, u8 number_of_bytes)
{
	u8 bitmask = 0x80;
	u8 bitshift = 7;
	u8 array_index = 0;
	int number_of_bits = number_of_bytes * 8;
	u16 remainder = 0;

	while (number_of_bits != 0) {
		number_of_bits--;
		remainder <<= 1;
		remainder |= (data[array_index] & bitmask) >> bitshift;
		bitmask >>= 1;
		bitshift--;
		if (bitmask == 0) {
			bitmask = 0x80;
			bitshift = 7;
			array_index++;
		}
		if ((remainder & 0x100) == 0x100)
			remainder ^= 0xd5;
	}

	number_of_bits = 8;
	while (number_of_bits != 0) {
		number_of_bits--;
		remainder <<= 1;
		if ((remainder & 0x100) != 0)
			remainder ^= 0xd5;
	}

	return remainder & 0xff;
}


static void sideband_msg_read(struct dp_sink_device *sink_dev, u8 *buf, int len)
{
	int link_count_total;
	int rad_len;
	int hdr_len;
	int body_len;
	bool start;
	bool end;
	int seq_no;
	struct msg_transaction *req;
	struct msg_transaction *rep;

	link_count_total = buf[0] >> 4;
	rad_len = link_count_total / 2;
	hdr_len = rad_len + 3;
	body_len = buf[rad_len + 1] & 0x3f;

	/* If message is incomplete, do nothing */
	if (hdr_len + body_len > len)
		return;

	start  = (buf[rad_len + 2] >> 7) & 1;
	end    = (buf[rad_len + 2] >> 6) & 1;
	seq_no = (buf[rad_len + 2] >> 4) & 1;

	req = &sink_dev->mt_req[seq_no];
	rep = &sink_dev->mt_rep[seq_no];

	if (start)
		req->len = 0;
	if (req->len + body_len - 1 < 256) {
		memcpy(req->buf + req->len, buf + hdr_len, body_len - 1);
		req->len += body_len - 1;
	}

	if (end) {
		rep->written = 0;
		memcpy(rep->rad, buf + 1, rad_len);
		rep->link_count_total = link_count_total;
		msg_transaction_execute(sink_dev, req, rep);
	}
}

static void sideband_msg_write(struct dp_sink_device *sink_dev, u8 *buf, int buf_len)
{
	int rad_len;
	int hdr_len;
	int body_len;
	bool start;
	bool end;
	u8 hdr_crc4;
	u8 body_crc4;
	struct msg_transaction *rep;

	rep = &sink_dev->mt_rep[sink_dev->mt_seq_no];
	if (rep->len == 0) {
		sink_dev->mt_seq_no ^= 1;
		rep = &sink_dev->mt_rep[sink_dev->mt_seq_no];
		if (rep->len == 0)
			return;
	}

	rad_len = rep->link_count_total / 2;
	hdr_len = rad_len + 3;
	body_len = min(rep->len - rep->written + 1, buf_len - hdr_len);

	start = (rep->written == 0);
	end   = (rep->written + body_len - 1 == rep->len);

	buf[0] = rep->link_count_total << 4 | ((rep->link_count_total - 1) & 0xf);
	memcpy(buf + 1, rep->rad, rad_len);
	buf[rad_len + 1] = body_len;
	buf[rad_len + 2] = start << 7 | end << 6 | sink_dev->mt_seq_no << 4;
	hdr_crc4 = get_hdr_crc4(buf, hdr_len * 2 - 1);
	buf[rad_len + 2] |= hdr_crc4;
	memcpy(buf + hdr_len, rep->buf + rep->written, body_len - 1);
	body_crc4 = get_body_crc4(buf + hdr_len, body_len - 1);
	buf[hdr_len + body_len - 1] = body_crc4;
	rep->written += body_len - 1;

	if (end) {
		rep->len = 0;
		rep->written = 0;
		sink_dev->mt_seq_no ^= 1;
	}
}

static bool sideband_msg_pending(struct dp_sink_device *sink_dev)
{
	return sink_dev->mt_rep[0].len > 0 || sink_dev->mt_rep[1].len > 0;
}

static void dpcd_clear_vc_payload_table(struct dp_sink_device *sink_dev)
{
	memset(sink_dev->dpcd.vc_table, 0, 64);
}

static void dpcd_allocate_vc_payload(struct dp_sink_device *sink_dev, int start, int count, u8 id)
{
	if (count > 64 - start)
		count = 64 - start;
	memset(sink_dev->dpcd.vc_table + start, id, count);
}

static void dpcd_deallocate_vc_payload(struct dp_sink_device *sink_dev, int start, u8 id)
{
	int to = start;
	int i;

	for (i = start; i < 64; i++) {
		if (sink_dev->dpcd.vc_table[i] == id)
			sink_dev->dpcd.vc_table[i] = 0;
		else
			sink_dev->dpcd.vc_table[to++] = sink_dev->dpcd.vc_table[i];
	}
}

static void dpcd_handle_payload_allocate(struct dp_sink_device *sink_dev)
{
	u8 id = sink_dev->dpcd.vc_alloc[0x0];
	u8 start = sink_dev->dpcd.vc_alloc[0x1];
	u8 count = sink_dev->dpcd.vc_alloc[0x2];

	if (id == 0 && start == 0 && count == 0x3f) {
		dpcd_clear_vc_payload_table(sink_dev);
		intel_dprx_clear_vc_payload_table(sink_dev->dprx);
	} else {
		if (count == 0)
			dpcd_deallocate_vc_payload(sink_dev, start, id);
		else
			dpcd_allocate_vc_payload(sink_dev, start, count, id);
		intel_dprx_set_vc_payload_table(sink_dev->dprx, sink_dev->dpcd.vc_table);
	}
	sink_dev->dpcd.vc_table_status |= 1 << 0;
}





/* 100h */
static void dpcd_write_link_bw_set(struct dp_sink_device *sink_dev, u8 val)
{
	sink_dev->dpcd.link_conf[0x0] = val;
	intel_dprx_set_link_rate(sink_dev->dprx, val);
}

/* 101h */
static void dpcd_write_lane_count_set(struct dp_sink_device *sink_dev, u8 val)
{
	sink_dev->dpcd.link_conf[0x1] = val;
	intel_dprx_set_lane_count(sink_dev->dprx, val & 0x1f);
}

/* 102h */
static void dpcd_write_training_pattern_set(struct dp_sink_device *sink_dev, u8 val)
{
	sink_dev->dpcd.link_conf[0x2] = val;
	intel_dprx_set_training_pattern(sink_dev->dprx, val & 0xf);
	intel_dprx_set_scrambler(sink_dev->dprx, !((val >> 5) & 1));
}

/* 111h */
static void dpcd_write_mstm_ctrl(struct dp_sink_device *sink_dev, u8 *src, u32 offset, u32 count)
{
	u8 val = *src;

	sink_dev->dpcd.mstm_ctrl = val;
	intel_dprx_set_mst(sink_dev->dprx, val & 1);
}

/* 1c0h */
static void dpcd_write_payload_allocate_set(struct dp_sink_device *sink_dev, u8 val)
{
	sink_dev->dpcd.vc_alloc[0x0] = val & 0x7f;
}

/* 1c1h */
static void dpcd_write_payload_allocate_start_time_slot(struct dp_sink_device *sink_dev, u8 val)
{
	sink_dev->dpcd.vc_alloc[0x1] = val & 0x3f;
}

/* 1c2h */
static void dpcd_write_payload_allocate_time_slot_count(struct dp_sink_device *sink_dev, u8 val)
{
	sink_dev->dpcd.vc_alloc[0x2] = val & 0x3f;
	dpcd_handle_payload_allocate(sink_dev);
}

/* 201h */
static void dpcd_write_device_service_irq_vector(struct dp_sink_device *sink_dev, u8 val)
{
	sink_dev->dpcd.irq_vector &= ~val;

	if (sideband_msg_pending(sink_dev)) {
		sink_dev->dpcd.irq_vector |= 1 << 4;
		sideband_msg_write(sink_dev, sink_dev->dpcd.down_rep, 48);
		intel_dprx_pulse_hpd(sink_dev->dprx);
	}
}

/* 202h */
static u8 dpcd_read_lane01_status(struct dp_sink_device *sink_dev)
{
	int cr_lock;
	int sym_lock;
	u8 res = 0;

	cr_lock = intel_dprx_get_cr_lock(sink_dev->dprx);
	sym_lock = intel_dprx_get_sym_lock(sink_dev->dprx);
	/* lane 0 */
	if (cr_lock & (1 << 0))
		res |= 0x1;
	if (sym_lock & (1 << 0))
		res |= 0x6;
	/* lane 1 */
	if (cr_lock & (1 << 1))
		res |= 0x10;
	if (sym_lock & (1 << 1))
		res |= 0x60;

	return res;
}

/* 203h */
static u8 dpcd_read_lane23_status(struct dp_sink_device *sink_dev)
{
	int cr_lock;
	int sym_lock;
	u8 res = 0;

	cr_lock = intel_dprx_get_cr_lock(sink_dev->dprx);
	sym_lock = intel_dprx_get_sym_lock(sink_dev->dprx);
	/* lane 2 */
	if (cr_lock & (1 << 2))
		res |= 0x1;
	if (sym_lock & (1 << 2))
		res |= 0x6;
	/* lane 3 */
	if (cr_lock & (1 << 3))
		res |= 0x10;
	if (sym_lock & (1 << 3))
		res |= 0x60;

	return res;
}

/* 204h */
static u8 dpcd_read_lane_align_status(struct dp_sink_device *sink_dev)
{
	return intel_dprx_get_interlane_align(sink_dev->dprx);
}

/* 205h */
static u8 dpcd_read_sink_status(struct dp_sink_device *sink_dev)
{
	return intel_dprx_get_sink_status(sink_dev->dprx);
}

/* 2c0h */
static void
dpcd_read_payload_table_update_status(struct dp_sink_device *sink_dev, u8 *dest, u32 offset,
				      u32 count)
{
	*dest = sink_dev->dpcd.vc_table_status;
	if (intel_dprx_get_act(sink_dev->dprx))
		*dest |= 1 << 1;
}

/* 2c0h */
static void
dpcd_write_payload_table_update_status(struct dp_sink_device *sink_dev, u8 *src, u32 offset,
				       u32 count)
{
	if (*src & 0x1) {
		sink_dev->dpcd.vc_table_status = 0;
		intel_dprx_clear_act(sink_dev->dprx);
	}
}





static void dpcd_read_caps(struct dp_sink_device *sink_dev, u8 *dest, u32 offset, u32 count)
{
	memcpy(dest, sink_dev->dpcd.caps + offset, count);
}

static void dpcd_read_mstm_cap(struct dp_sink_device *sink_dev, u8 *dest, u32 offset, u32 count)
{
	*dest = sink_dev->dpcd.mstm_cap;
}

static void dpcd_read_guid(struct dp_sink_device *sink_dev, u8 *dest, u32 offset, u32 count)
{
	memcpy(dest, sink_dev->dpcd.guid + offset, count);
}

static void dpcd_write_guid(struct dp_sink_device *sink_dev, u8 *src, u32 offset, u32 count)
{
	memcpy(sink_dev->dpcd.guid + offset, src, count);
}

static void dpcd_read_link_conf(struct dp_sink_device *sink_dev, u8 *dest, u32 offset, u32 count)
{
	memcpy(dest, sink_dev->dpcd.link_conf + offset, count);
}

static void dpcd_write_link_conf(struct dp_sink_device *sink_dev, u8 *src, u32 offset, u32 count)
{
	if (offset <= 0 && 0 < offset + count)
		dpcd_write_link_bw_set(sink_dev, src[0 - offset]);
	if (offset <= 1 && 1 < offset + count)
		dpcd_write_lane_count_set(sink_dev, src[1 - offset]);
	if (offset <= 2 && 2 < offset + count)
		dpcd_write_training_pattern_set(sink_dev, src[2 - offset]);
}

static void dpcd_read_mstm_ctrl(struct dp_sink_device *sink_dev, u8 *dest, u32 start, u32 count)
{
	*dest = sink_dev->dpcd.mstm_ctrl;
}

static void dpcd_read_vc_alloc(struct dp_sink_device *sink_dev, u8 *dest, u32 offset, u32 count)
{
	memcpy(dest, sink_dev->dpcd.vc_alloc + offset, count);
}

static void dpcd_write_vc_alloc(struct dp_sink_device *sink_dev, u8 *src, u32 offset, u32 count)
{
	if (offset <= 0 && 0 < offset + count)
		dpcd_write_payload_allocate_set(sink_dev, src[0 - offset]);
	if (offset <= 1 && 1 < offset + count)
		dpcd_write_payload_allocate_start_time_slot(sink_dev, src[1 - offset]);
	if (offset <= 2 && 2 < offset + count)
		dpcd_write_payload_allocate_time_slot_count(sink_dev, src[2 - offset]);
}

static void dpcd_read_sink_stat(struct dp_sink_device *sink_dev, u8 *dest, u32 offset, u32 count)
{
	if (offset <= 0 && 0 < offset + count)
		dest[0 - offset] = sink_dev->dpcd.sink_count;
	if (offset <= 1 && 1 < offset + count)
		dest[1 - offset] = sink_dev->dpcd.irq_vector;
}

static void dpcd_write_sink_stat(struct dp_sink_device *sink_dev, u8 *src, u32 offset, u32 count)
{
	if (offset <= 1 && 1 < offset + count)
		dpcd_write_device_service_irq_vector(sink_dev, src[1 - offset]);
}

static void dpcd_read_link_stat(struct dp_sink_device *sink_dev, u8 *dest, u32 offset, u32 count)
{
	if (offset <= 0 && 0 < offset + count)
		dest[0 - offset] = dpcd_read_lane01_status(sink_dev);
	if (offset <= 1 && 1 < offset + count)
		dest[1 - offset] = dpcd_read_lane23_status(sink_dev);
	if (offset <= 2 && 2 < offset + count)
		dest[2 - offset] = dpcd_read_lane_align_status(sink_dev);
	if (offset <= 3 && 3 < offset + count)
		dest[3 - offset] = dpcd_read_sink_status(sink_dev);
	if (offset <= 4 && 4 < offset + count)
		dest[4 - offset] = 0x55;
	if (offset <= 5 && 5 < offset + count)
		dest[5 - offset] = 0x55;
}

static void dpcd_read_vc_table(struct dp_sink_device *sink_dev, u8 *dest, u32 offset, u32 count)
{
	memcpy(dest, sink_dev->dpcd.vc_table + offset + 1, count);
}

static void dpcd_read_sink_spec(struct dp_sink_device *sink_dev, u8 *dest, u32 offset, u32 count)
{
	memcpy(dest, sink_dev->dpcd.sink_spec + offset, count);
}

static void dpcd_read_down_req(struct dp_sink_device *sink_dev, u8 *dest, u32 offset, u32 count)
{
	memcpy(dest, sink_dev->dpcd.down_req + offset, count);
}

static void dpcd_write_down_req(struct dp_sink_device *sink_dev, u8 *src, u32 offset, u32 count)
{
	memcpy(sink_dev->dpcd.down_req + offset, src, count);
	/*
	 * The sideband message may require multiple AUX transactions to be
	 * fully written. Normally, the source writes the data in order,
	 * in blocks of 16. Unfortunately, the spec doesn't say what to
	 * do if the source behaves differently than that.
	 *
	 * Approach taken here: when we get a write, assume all the
	 * bytes before the starting address are valid, try to parse
	 * the message up to the last byte written in this transaction
	 * (if it's incomplete, nothing happens).
	 */
	sideband_msg_read(sink_dev, sink_dev->dpcd.down_req, offset + count);
	if (!(sink_dev->dpcd.irq_vector & (1 << 4)) && sideband_msg_pending(sink_dev)) {
		sink_dev->dpcd.irq_vector |= 1 << 4;
		sideband_msg_write(sink_dev, sink_dev->dpcd.down_rep, 48);
		intel_dprx_pulse_hpd(sink_dev->dprx);
	}
}

static void dpcd_read_down_rep(struct dp_sink_device *sink_dev, u8 *dest, u32 offset, u32 count)
{
	memcpy(dest, sink_dev->dpcd.down_rep + offset, count);
}

struct dpcd_range {
	u32 start;
	u32 end;
	void (*read)(struct dp_sink_device *sink_dev, u8 *src, u32 offset, u32 count);
	void (*write)(struct dp_sink_device *sink_dev, u8 *src, u32 offset, u32 count);
};

static struct dpcd_range dpcd_ranges[] = {
	{ 0x00000, 0x00010, dpcd_read_caps,      NULL },
	{ 0x00021, 0x00022, dpcd_read_mstm_cap,  NULL },
	{ 0x00030, 0x00040, dpcd_read_guid,      dpcd_write_guid },
	{ 0x00100, 0x00103, dpcd_read_link_conf, dpcd_write_link_conf },
	{ 0x00111, 0x00112, dpcd_read_mstm_ctrl, dpcd_write_mstm_ctrl },
	{ 0x001c0, 0x001c3, dpcd_read_vc_alloc,  dpcd_write_vc_alloc },
	{ 0x00200, 0x00202, dpcd_read_sink_stat, dpcd_write_sink_stat },
	{ 0x00202, 0x00208, dpcd_read_link_stat, NULL },
	{ 0x002c0, 0x002c1, dpcd_read_payload_table_update_status,
						 dpcd_write_payload_table_update_status },
	{ 0x002c1, 0x00300, dpcd_read_vc_table,  NULL },
	{ 0x00400, 0x0040c, dpcd_read_sink_spec, NULL },
	{ 0x01000, 0x01030, dpcd_read_down_req,  dpcd_write_down_req },
	{ 0x01400, 0x01430, dpcd_read_down_rep,  NULL },
	{ 0x02002, 0x02004, dpcd_read_sink_stat, dpcd_write_sink_stat },
	{ 0x0200c, 0x02010, dpcd_read_link_stat, NULL },
};

static void dpcd_access(struct dp_sink_device *sink_dev, struct aux_msg *req,
			struct aux_msg *res)
{
	struct dpcd_range *range;
	struct dpcd_range *range_end = dpcd_ranges + ARRAY_SIZE(dpcd_ranges);
	bool read = req->cmd & 1;
	u32 start;
	u32 end;
	u8 *buf;
	u32 offset;
	u32 count;

	res->cmd = AUX_ACK;
	if (read) {
		res->len = req->len;
		memset(res->data, 0, res->len);
	} else {
		res->len = 0;
	}

	for (range = dpcd_ranges; range < range_end; range++) {
		if (range->end <= req->addr || req->addr + req->len <= range->start)
			continue;
		start = max(range->start, req->addr);
		end   = min(range->end,   req->addr + req->len);
		count = end - start;
		offset = start - range->start;
		if (read) {
			buf = res->data + (start - req->addr);
			range->read(sink_dev, buf, offset, count);
		} else if (range->write) {
			buf = req->data + (start - req->addr);
			range->write(sink_dev, buf, offset, count);
		}
	}
}

static void handle_i2c_read(struct dp_sink_device *sink_dev, struct aux_msg *req,
			    struct aux_msg *res)
{
	int r;

	r = dp_sink_i2c_read(&sink_dev->sinks[0], req->addr, res->data, req->len);
	if (!r) {
		res->cmd = AUX_ACK;
		res->len = req->len;
	} else {
		res->cmd = AUX_I2C_NACK;
		res->len = 0;
	}
}

static void handle_i2c_write(struct dp_sink_device *sink_dev, struct aux_msg *req,
			     struct aux_msg *res)
{
	int r;

	r = dp_sink_i2c_write(&sink_dev->sinks[0], req->addr, req->data, req->len);
	if (!r)
		res->cmd = AUX_ACK;
	else
		res->cmd = AUX_I2C_NACK;
	res->len = 0;
}

static void read_aux_request(struct aux_msg *req, struct dp_aux_buf *buf)
{
	req->cmd = buf->data[0] >> 4;
	req->addr = (buf->data[0] & 0xf) << 16 | buf->data[1] << 8 | buf->data[2];
	if (buf->len < 4) {
		req->len = 0;
	} else {
		req->len = buf->data[3] + 1;
		memcpy(req->data, &buf->data[4], req->len);
	}
}

static void write_aux_reply(struct aux_msg *rep, struct dp_aux_buf *buf)
{
	buf->data[0] = rep->cmd << 4;
	memcpy(&buf->data[1], rep->data, rep->len);
	buf->len = rep->len + 1;
}

static u8 dpcd_caps[16] = {
	0x14, // DPCD 1.4
	// TODO(b:322961797): Temporary reduce max link rate to 5.4Gbps until
	// issue with 8.1Gbps is resolved.
	0x14, // Max link rate 5.4Gbps
	0xc4, // Max lane count 4, TPS3, Enhanced frame cap
	0x81, // Down-spread, TPS4
	0x01, // 2 Receiver ports for SST (video & audio)
	0x00, // no downstream ports
	0x01, // 8b/10b support
	0x80, // no downstream ports, OUI present
	0x02, // has local EDID
	0x00, // buffer size?
	0x06,
	0x00,
	0x00, // no physical i2c bus
	0x00, // reserved for eDP
	0x00, // no extended receiver capability present
	0x00, // no legacy adaptor caps
};

static u8 dpcd_sink_spec[12] = {
	0x12, /* OUI byte 0 */
	0x34, /* OUI byte 1 */
	0x56, /* OUI byte 2 */
	'I',  /* Device identification string */
	'N',
	'T',
	'E',
	'L',
	'\0',
	0x30, /* HW revision */
	0x00, /* SW revision major */
	0x00, /* SW revision minor */
};

static void reset_dpcd(struct dpcd_mem *dpcd, int mst, int sink_count)
{
	memset(dpcd, 0, sizeof(struct dpcd_mem));
	memcpy(dpcd->caps, dpcd_caps, 16);
	memcpy(dpcd->sink_spec, dpcd_sink_spec, 12);
	dpcd->mstm_cap = mst;
	dpcd->sink_count = sink_count;
}

void dp_sink_device_reset(struct dp_sink_device *sink_dev)
{
	int i;

	for (i = 0; i < 4; i++) {
		sink_dev->sinks[i].offset = 0;
		sink_dev->sinks[i].segment = 0;
		sink_dev->vc_id[i] = 0;
	}
	sink_dev->total_pbn = 0;
	sink_dev->sum_pbn = 0;

	reset_dpcd(&sink_dev->dpcd, sink_dev->mst, sink_dev->sink_count);

	dpcd_write_link_bw_set(sink_dev, 0x1e);
	dpcd_write_lane_count_set(sink_dev, 0x04);

	for (i = 0; i < 2; i++) {
		memset(&sink_dev->mt_req[i], 0, sizeof(struct msg_transaction));
		memset(&sink_dev->mt_rep[i], 0, sizeof(struct msg_transaction));
	}
	sink_dev->mt_seq_no = 0;
}

void dp_sink_device_init(struct dp_sink_device *sink_dev,
			 struct intel_dprx *dprx, int mst_count)
{
	int i;

	if (mst_count == 0) {
		sink_dev->mst = 0;
		sink_dev->sink_count = 1;
	} else {
		sink_dev->mst = 1;
		sink_dev->sink_count = mst_count;
	}
	sink_dev->dprx = dprx;

	for (i = 0; i < 4; i++)
		sink_dev->sinks[i].blocks = 0;

	dp_sink_device_reset(sink_dev);
};

void dp_sink_device_handle_request(struct dp_sink_device *sink_dev,
				   struct dp_aux_buf *req_buf,
				   struct dp_aux_buf *rep_buf)
{
	struct aux_msg req;
	struct aux_msg rep;

	read_aux_request(&req, req_buf);

	if (req.cmd & 8) {
		dpcd_access(sink_dev, &req, &rep);
	} else {
		if (req.cmd & 1)
			handle_i2c_read(sink_dev, &req, &rep);
		else
			handle_i2c_write(sink_dev, &req, &rep);
		if (!(req.cmd & 4))
			sink_dev->sinks[0].segment = 0;
	}

	write_aux_reply(&rep, rep_buf);
}
