#include <linux/string.h>
#include "dprx.h"

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

#define MT_NACK 0x80
#define MT_BAD_PARAM 0x4

static void execute_link_address(struct dprx_dp *dp,
				 struct msg_transaction *req,
				 struct msg_transaction *rep)
{
	int ports = dp->sink_count + 1;
	u8 *buf;
	int i;

	rep->buf[0] = MT_LINK_ADDRESS;
	memcpy(rep->buf + 1, dp->dpcd.guid, 16);
	rep->buf[17] = ports;
	/* port 0 */
	rep->buf[18] = 0x90; /* input, source device, port 0 */
	rep->buf[19] = 0x40; /* no msg, connected */

	buf = rep->buf + 20;
	for (i = 1; i < ports; i++) {
		buf[0] = 0x30 | i; /* output, sink device, port i */
		buf[1] = 0x40; /* no msg, connected */
		buf[2] = 0x00; /* DPCD 0 */
		memset(buf + 3, 0, 16); /* GUID */
		buf[19] = 0x00; /* 0 SDP streams, 0 SDP stream sinks */
		buf += 20;
	}
	rep->len = ports * 20;
}

static void execute_enum_path_resources(struct dprx_dp *dp,
					struct msg_transaction *req,
					struct msg_transaction *rep)
{
	u8 port;

	port = req->buf[1] >> 4;

	dp->total_pbn = dp->dpcd.link_conf[0] *
			dp->dpcd.link_conf[1] * 32;

	rep->buf[0] = MT_ENUM_PATH_RESOURCES;
	rep->buf[1] = port << 4;
	rep->buf[2] = dp->total_pbn >> 8;
	rep->buf[3] = dp->total_pbn & 0xff;
	rep->buf[4] = (dp->total_pbn - dp->sum_pbn) >> 8;
	rep->buf[5] = (dp->total_pbn - dp->sum_pbn) & 0xff;
	rep->len = 6;
}

static void execute_allocate_payload(struct dprx_dp *dp,
				     struct msg_transaction *req,
				     struct msg_transaction *rep)
{
	u8 port;
	u8 id;
	u16 pbn;

	port = req->buf[1] >> 4;
	id = req->buf[2] & 0x7f;
	pbn = req->buf[3] << 8 | req->buf[4];

	dp->vc_id[port-1] = id;
	dprx_dprx_set_vc_ids(dp, dp->vc_id);

	rep->buf[0] = MT_ALLOCATE_PAYLOAD;
	rep->buf[1] = port << 4;
	rep->buf[2] = id;
	rep->buf[3] = pbn >> 8;
	rep->buf[4] = pbn & 0xff;
	rep->len = 5;
}

static void execute_clear_payload_id_table(struct dprx_dp *dp,
					   struct msg_transaction *req,
					   struct msg_transaction *rep)
{
	dprx_dprx_clear_vc_payload_table(dp);

	rep->buf[0] = MT_CLEAR_PAYLOAD_ID_TABLE;
	rep->len = 1;
}

static void execute_remote_i2c_read(struct dprx_dp *dp,
				    struct msg_transaction *req,
				    struct msg_transaction *rep)
{
	u8 *req_buf = req->buf;
	struct sink *sink;
	u8 port;
	int num_write_transactions;
	u8 addr;
	int len;
	int i;

	port = req_buf[1] >> 4;

	if (port < 1 || port > dp->sink_count) {
		rep->buf[0] = MT_NACK | MT_REMOTE_I2C_READ;
		memcpy(&rep->buf[1], dp->dpcd.guid, 16);
		rep->buf[17] = MT_BAD_PARAM;
		rep->buf[18] = 0;
		rep->len = 18;
		return;
	}

	sink = &dp->sinks[port-1];

	num_write_transactions = req_buf[1] & 0x3;
	req_buf += 2;
	for (i = 0; i < num_write_transactions; i++) {
		addr = req_buf[0] & 0x7f;
		len = req_buf[1];
		dprx_i2c_write(sink, addr, &req_buf[2], len);
		req_buf += len + 3;
	}
	addr = req_buf[0] & 0x7f;
	len = req_buf[1];

	rep->buf[0] = MT_REMOTE_I2C_READ;
	rep->buf[1] = port;
	rep->buf[2] = len;
	dprx_i2c_read(sink, addr, rep->buf + 3, len);
	rep->len = len + 3;
}

static void execute_power_up_phy(struct dprx_dp *dp,
				 struct msg_transaction *req,
				 struct msg_transaction *rep)
{
	u8 port;

	port = req->buf[1] >> 4;

	rep->buf[0] = MT_POWER_UP_PHY;
	rep->buf[1] = port << 4;
	rep->len = 2;
}

void dprx_mt_execute(struct dprx_dp *dp, struct msg_transaction *req,
		     struct msg_transaction *rep)
{
	switch (req->buf[0] & 0x7f) {
	case MT_LINK_ADDRESS:
		execute_link_address(dp, req, rep);
		break;
	case MT_ENUM_PATH_RESOURCES:
		execute_enum_path_resources(dp, req, rep);
		break;
	case MT_ALLOCATE_PAYLOAD:
		execute_allocate_payload(dp, req, rep);
		break;
	case MT_CLEAR_PAYLOAD_ID_TABLE:
		execute_clear_payload_id_table(dp, req, rep);
		break;
	case MT_REMOTE_I2C_READ:
		execute_remote_i2c_read(dp, req, rep);
		break;
	case MT_POWER_UP_PHY:
		execute_power_up_phy(dp, req, rep);
		break;
	default:
		rep->len = 0;
	}
}
