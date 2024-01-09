#include <linux/string.h>
#include "dprx.h"

static void dpcd_clear_vc_payload_table(struct dprx_dp *dp)
{
	memset(dp->dpcd.vc_table, 0, 64);
}

static void dpcd_allocate_vc_payload(struct dprx_dp *dp, int start, int count, u8 id)
{
	if (count > 64 - start)
		count = 64 - start;
	memset(dp->dpcd.vc_table + start, id, count);
}

static void dpcd_deallocate_vc_payload(struct dprx_dp *dp, int start, u8 id)
{
	int i;
	int to = start;

	for (i = start; i < 64; i++) {
		if (dp->dpcd.vc_table[i] == id)
			dp->dpcd.vc_table[i] = 0;
		else
			dp->dpcd.vc_table[to++] = dp->dpcd.vc_table[i];
	}
}

static void dpcd_handle_payload_allocate(struct dprx_dp *dp)
{
	u8 id = dp->dpcd.vc_alloc[0x0];
	u8 start = dp->dpcd.vc_alloc[0x1];
	u8 count = dp->dpcd.vc_alloc[0x2];

	if (id == 0 && start == 0 && count == 0x3f) {
		dpcd_clear_vc_payload_table(dp);
		dprx_dprx_clear_vc_payload_table(dp);
	} else {
		if (count == 0)
			dpcd_deallocate_vc_payload(dp, start, id);
		else
			dpcd_allocate_vc_payload(dp, start, count, id);
		dprx_dprx_set_vc_payload_table(dp, dp->dpcd.vc_table, dp->vc_id);
	}
	dp->dpcd.vc_table_status |= 1 << 0;
}





/* 100h */
static void dpcd_write_link_bw_set(struct dprx_dp *dp, u8 val)
{
	dp->dpcd.link_conf[0x0] = val;
	dprx_dprx_set_link_rate(dp, val);
}

/* 101h */
static void dpcd_write_lane_count_set(struct dprx_dp *dp, u8 val)
{
	dp->dpcd.link_conf[0x1] = val;
	dprx_dprx_set_lane_count(dp, val & 0x1f);
}

/* 102h */
static void dpcd_write_training_pattern_set(struct dprx_dp *dp, u8 val)
{
	dp->dpcd.link_conf[0x2] = val;
	dprx_dprx_set_training_pattern(dp, val & 0xf);
	dprx_dprx_set_scrambler(dp, !((val >> 5) & 1));
}

/* 111h */
static void dpcd_write_mstm_ctrl(struct dprx_dp *dp, u8 *src, u32 offset, u32 count)
{
	u8 val = *src;
	dp->dpcd.mstm_ctrl = val;
	dprx_dprx_set_mst(dp, val & 1);
}

/* 1c0h */
static void dpcd_write_payload_allocate_set(struct dprx_dp *dp, u8 val)
{
	dp->dpcd.vc_alloc[0x0] = val & 0x7f;
}

/* 1c1h */
static void dpcd_write_payload_allocate_start_time_slot(struct dprx_dp *dp, u8 val)
{
	dp->dpcd.vc_alloc[0x1] = val & 0x3f;
}

/* 1c2h */
static void dpcd_write_payload_allocate_time_slot_count(struct dprx_dp *dp, u8 val)
{
	dp->dpcd.vc_alloc[0x2] = val & 0x3f;
	dpcd_handle_payload_allocate(dp);
}

/* 201h */
static void dpcd_write_device_service_irq_vector(struct dprx_dp *dp, u8 val)
{
	dp->dpcd.irq_vector &= ~val;

	if (dprx_sbmsg_pending(dp)) {
		dp->dpcd.irq_vector |= 1 << 4;
		dprx_sbmsg_write(dp, dp->dpcd.down_rep, 48);
		dprx_dprx_pulse_hpd(dp);
	}
}

/* 202h */
static u8 dpcd_read_lane01_status(struct dprx_dp *dp)
{
	int cr_lock;
	int sym_lock;
	u8 res = 0;

	cr_lock = dprx_dprx_get_cr_lock(dp);
	sym_lock = dprx_dprx_get_sym_lock(dp);
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
static u8 dpcd_read_lane23_status(struct dprx_dp *dp)
{
	int cr_lock;
	int sym_lock;
	u8 res = 0;

	cr_lock = dprx_dprx_get_cr_lock(dp);
	sym_lock = dprx_dprx_get_sym_lock(dp);
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
static u8 dpcd_read_lane_align_status(struct dprx_dp *dp)
{
	return dprx_dprx_get_interlane_align(dp);
}

/* 205h */
static u8 dpcd_read_sink_status(struct dprx_dp *dp)
{
	return dprx_dprx_get_sink_status(dp);
}

/* 2c0h */
static void dpcd_read_payload_table_update_status(struct dprx_dp *dp, u8 *dest, u32 offset, u32 count)
{
	*dest = dp->dpcd.vc_table_status;
	if (dprx_dprx_get_act(dp))
		*dest |= 1 << 1;
}

/* 2c0h */
static void dpcd_write_payload_table_update_status(struct dprx_dp *dp, u8 *src, u32 offset, u32 count)
{
	if (*src & 0x1) {
		dp->dpcd.vc_table_status = 0;
		dprx_dprx_clear_act(dp);
	}
}





static void dpcd_read_caps(struct dprx_dp *dp, u8 *dest, u32 offset, u32 count)
{
	memcpy(dest, dp->dpcd.caps + offset, count);
}

static void dpcd_read_mstm_cap(struct dprx_dp *dp, u8 *dest, u32 offset, u32 count)
{
	*dest = dp->dpcd.mstm_cap;
}

static void dpcd_read_guid(struct dprx_dp *dp, u8 *dest, u32 offset, u32 count)
{
	memcpy(dest, dp->dpcd.guid + offset, count);
}

static void dpcd_write_guid(struct dprx_dp *dp, u8 *src, u32 offset, u32 count)
{
	memcpy(dp->dpcd.guid + offset, src, count);
}

static void dpcd_read_link_conf(struct dprx_dp *dp, u8 *dest, u32 offset, u32 count)
{
	memcpy(dest, dp->dpcd.link_conf + offset, count);
}

static void dpcd_write_link_conf(struct dprx_dp *dp, u8 *src, u32 offset, u32 count)
{
	if (offset <= 0 && 0 < offset + count)
		dpcd_write_link_bw_set(dp, src[0 - offset]);
	if (offset <= 1 && 1 < offset + count)
		dpcd_write_lane_count_set(dp, src[1 - offset]);
	if (offset <= 2 && 2 < offset + count)
		dpcd_write_training_pattern_set(dp, src[2 - offset]);

	while (dprx_dprx_get_rx_busy(dp)) {}
}

static void dpcd_read_mstm_ctrl(struct dprx_dp *dp, u8 *dest, u32 start, u32 count)
{
	*dest = dp->dpcd.mstm_ctrl;
}

static void dpcd_read_vc_alloc(struct dprx_dp *dp, u8 *dest, u32 offset, u32 count)
{
	memcpy(dest, dp->dpcd.vc_alloc + offset, count);
}

static void dpcd_write_vc_alloc(struct dprx_dp *dp, u8 *src, u32 offset, u32 count)
{
	if (offset <= 0 && 0 < offset + count)
		dpcd_write_payload_allocate_set(dp, src[0 - offset]);
	if (offset <= 1 && 1 < offset + count)
		dpcd_write_payload_allocate_start_time_slot(dp, src[1 - offset]);
	if (offset <= 2 && 2 < offset + count)
		dpcd_write_payload_allocate_time_slot_count(dp, src[2 - offset]);
}

static void dpcd_read_sink_stat(struct dprx_dp *dp, u8 *dest, u32 offset, u32 count)
{
	if (offset <= 0 && 0 < offset + count)
		dest[0 - offset] = dp->dpcd.sink_count;
	if (offset <= 1 && 1 < offset + count)
		dest[1 - offset] = dp->dpcd.irq_vector;
}

static void dpcd_write_sink_stat(struct dprx_dp *dp, u8 *src, u32 offset, u32 count)
{
	if (offset <= 1 && 1 < offset + count)
		dpcd_write_device_service_irq_vector(dp, src[1 - offset]);
}

static void dpcd_read_link_stat(struct dprx_dp *dp, u8 *dest, u32 offset, u32 count)
{
	if (offset <= 0 && 0 < offset + count)
		dest[0 - offset] = dpcd_read_lane01_status(dp);
	if (offset <= 1 && 1 < offset + count)
		dest[1 - offset] = dpcd_read_lane23_status(dp);
	if (offset <= 2 && 2 < offset + count)
		dest[2 - offset] = dpcd_read_lane_align_status(dp);
	if (offset <= 3 && 3 < offset + count)
		dest[3 - offset] = dpcd_read_sink_status(dp);
	if (offset <= 4 && 4 < offset + count)
		dest[4 - offset] = 0x55;
	if (offset <= 5 && 5 < offset + count)
		dest[5 - offset] = 0x55;
}

static void dpcd_read_vc_table(struct dprx_dp *dp, u8 *dest, u32 offset, u32 count)
{
	memcpy(dest, dp->dpcd.vc_table + offset + 1, count);
}

static void dpcd_read_sink_spec(struct dprx_dp *dp, u8 *dest, u32 offset, u32 count)
{
	memcpy(dest, dp->dpcd.sink_spec + offset, count);
}

static void dpcd_read_down_req(struct dprx_dp *dp, u8 *dest, u32 offset, u32 count)
{
	memcpy(dest, dp->dpcd.down_req + offset, count);
}

static void dpcd_write_down_req(struct dprx_dp *dp, u8 *src, u32 offset, u32 count)
{
	memcpy(dp->dpcd.down_req + offset, src, count);
	/*
	 * The sideband message may require multiple AUX transactions to be
	 * fully written. Normally, the source writes the data in order,
	 * in blocks of 16. Unfortunately, the spec doesn't say what to
	 * do if the source behaves differently that that.
	 *
	 * Approach taken here: when we get a write, assume all the
	 * bytes before the starting address are valid, try to parse
	 * the message up to the last byte written in this transaction
	 * (if it's incomplete, nothing happens).
	 */
	dprx_sbmsg_read(dp, dp->dpcd.down_req, offset + count);
	if (!(dp->dpcd.irq_vector & (1 << 4)) && dprx_sbmsg_pending(dp)) {
		dp->dpcd.irq_vector |= 1 << 4;
		dprx_sbmsg_write(dp, dp->dpcd.down_rep, 48);
		dprx_dprx_pulse_hpd(dp);
	}
}

static void dpcd_read_down_rep(struct dprx_dp *dp, u8 *dest, u32 offset, u32 count)
{
	memcpy(dest, dp->dpcd.down_rep + offset, count);
}

struct dpcd_range {
	u32 start;
	u32 end;
	void (*read) (struct dprx_dp *, u8 *, u32, u32);
	void (*write)(struct dprx_dp *, u8 *, u32, u32);
};

struct dpcd_range dpcd_ranges[] = {
	{ 0x00000, 0x00010, dpcd_read_caps,      NULL },
	{ 0x00021, 0x00022, dpcd_read_mstm_cap,  NULL },
	{ 0x00030, 0x00040, dpcd_read_guid,      dpcd_write_guid },
	{ 0x00100, 0x00103, dpcd_read_link_conf, dpcd_write_link_conf },
	{ 0x00111, 0x00112, dpcd_read_mstm_ctrl, dpcd_write_mstm_ctrl },
	{ 0x001c0, 0x001c3, dpcd_read_vc_alloc,  dpcd_write_vc_alloc },
	{ 0x00200, 0x00202, dpcd_read_sink_stat, dpcd_write_sink_stat },
	{ 0x00202, 0x00208, dpcd_read_link_stat, NULL },
	{ 0x002c0, 0x002c1, dpcd_read_payload_table_update_status, dpcd_write_payload_table_update_status },
	{ 0x002c1, 0x00300, dpcd_read_vc_table,  NULL },
	{ 0x00400, 0x0040c, dpcd_read_sink_spec, NULL },
	{ 0x01000, 0x01030, dpcd_read_down_req,  dpcd_write_down_req },
	{ 0x01400, 0x01430, dpcd_read_down_rep,  NULL },
	{ 0x02002, 0x02004, dpcd_read_sink_stat, dpcd_write_sink_stat },
	{ 0x0200c, 0x02010, dpcd_read_link_stat, NULL },
};

void dprx_dpcd_access(struct dprx_dp *dp, struct aux_msg *req,
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
			range->read(dp, buf, offset, count);
		} else if (range->write) {
			buf = req->data + (start - req->addr);
			range->write(dp, buf, offset, count);
		}
	}
}

void dprx_dpcd_init(struct dprx_dp *dp)
{
	struct dpcd_mem *dpcd = &dp->dpcd;

	memset(dpcd, 0, sizeof(struct dpcd_mem));

	dpcd->caps[0x0] = 0x14, // DPCD 1.4
	dpcd->caps[0x1] = 0x1e, // Max link rate 8.1Gbps
	dpcd->caps[0x2] = 0xc4, // Max lane count 4, TPS3, Enhanced frame cap
	dpcd->caps[0x3] = 0x81, // Down-spread, TPS4
	dpcd->caps[0x4] = 0x01, // 2 Reciever ports for SST (video & audio)
	dpcd->caps[0x5] = 0x00, // no downstream ports
	dpcd->caps[0x6] = 0x01, // 8b/10b support
	dpcd->caps[0x7] = 0x80, // no downstream ports, OUI present
	dpcd->caps[0x8] = 0x02, // has local EDID
	dpcd->caps[0x9] = 0x00, // buffer size?
	dpcd->caps[0xa] = 0x06,
	dpcd->caps[0xb] = 0x00,
	dpcd->caps[0xc] = 0x00, // no physical i2c bus
	dpcd->caps[0xd] = 0x00, // reserved for eDP
	dpcd->caps[0xe] = 0x00, // no extended receiver capability present
	dpcd->caps[0xf] = 0x00, // no legacy adaptor caps

	dpcd->mstm_cap = dp->has_mst;
	dpcd->sink_count = dp->has_mst ? dp->sink_count : 1;

	dpcd->sink_spec[0x0] = 0x12;
	dpcd->sink_spec[0x1] = 0x34;
	dpcd->sink_spec[0x2] = 0x56;
	dpcd->sink_spec[0x3] = 'c';
	dpcd->sink_spec[0x4] = 'h';
	dpcd->sink_spec[0x5] = 'a';
	dpcd->sink_spec[0x6] = 'm';
	dpcd->sink_spec[0x7] = 'e';
	dpcd->sink_spec[0x8] = 'l';
	dpcd->sink_spec[0x9] = 0x30;
	dpcd->sink_spec[0xa] = 0x00;
	dpcd->sink_spec[0xb] = 0x00;

	dpcd_write_link_bw_set(dp, 0x1e);
	dpcd_write_lane_count_set(dp, 0x04);
};
