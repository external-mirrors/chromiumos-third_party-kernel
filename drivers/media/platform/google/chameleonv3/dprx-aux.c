#include <linux/string.h>

#include "dprx.h"

static void handle_i2c_read(struct dprx_dp *dp, struct aux_msg *req,
			    struct aux_msg *res)
{
	int r;

	r = dprx_i2c_read(&dp->sinks[0], req->addr, res->data, req->len);
	if (!r) {
		res->cmd = AUX_ACK;
		res->len = req->len;
	} else {
		res->cmd = AUX_I2C_NACK;
		res->len = 0;
	}
}

static void handle_i2c_write(struct dprx_dp *dp, struct aux_msg *req,
			     struct aux_msg *res)
{
	int r;

	r = dprx_i2c_write(&dp->sinks[0], req->addr, req->data, req->len);
	if (!r)
		res->cmd = AUX_ACK;
	else
		res->cmd = AUX_I2C_NACK;
	res->len = 0;
}

void dprx_aux_handle_request(struct dprx_dp *dp, struct aux_msg *req,
			     struct aux_msg *res)
{
	if (req->cmd & 8) {
		dprx_dpcd_access(dp, req, res);
	} else {
		if (req->cmd & 1)
			handle_i2c_read(dp, req, res);
		else
			handle_i2c_write(dp, req, res);
		if (!(req->cmd & 4))
			dp->sinks[0].segment = 0;
	}
}

int dprx_aux_read_request(struct dprx_dp *dp, struct aux_msg *req)
{
	u8 data[20];
	int len;

	len = dprx_dprx_read_aux(dp, data);
	if (!len)
		return 0;

	req->cmd = data[0] >> 4;
	req->addr = (data[0] & 0xf) << 16 | data[1] << 8 | data[2];
	if (len < 4) {
		req->len = 0;
	} else {
		req->len = data[3] + 1;
		memcpy(req->data, &data[4], req->len);
	}

	return 1;
}

void dprx_aux_write_response(struct dprx_dp *dp, struct aux_msg *res)
{
	u8 data[20];

	data[0] = res->cmd << 4;
	memcpy(&data[1], res->data, res->len);

	dprx_dprx_write_aux(dp, data, res->len + 1);
}
