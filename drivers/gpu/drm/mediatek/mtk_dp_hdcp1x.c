// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019-2024 MediaTek Inc.
 */

#include <drm/display/drm_hdcp_helper.h>

#include "mtk_dp_hdcp1x.h"
#include "mtk_dp_reg_v2.h"
#include "mtk_dp_v2.h"

#define HDCP1X_R0_WDT			100
#define HDCP1X_REP_RDY_WDT		5000

#if IS_ENABLED(CONFIG_TEE)
static void mtk_dp_hdcp1x_start_cipher(struct mtk_hdcp_info *hdcp_info, bool enable)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);

	if (enable) {
		mtk_dp_update_bits_v2(mtk_dp, REG_3480_DP_TRANS_P0,
				      REQ_BLOCK_CIPHER_AUTH_DP_TRANS_P0_FLDMASK,
				      REQ_BLOCK_CIPHER_AUTH_DP_TRANS_P0_FLDMASK);
		mtk_dp_update_bits_v2(mtk_dp, REG_3480_DP_TRANS_P0,
				      KM_GENERATED_DP_TRANS_P0_FLDMASK,
				      KM_GENERATED_DP_TRANS_P0_FLDMASK);
	} else {
		mtk_dp_update_bits_v2(mtk_dp,
				      REG_3480_DP_TRANS_P0,
				      0, KM_GENERATED_DP_TRANS_P0_FLDMASK);
		mtk_dp_update_bits_v2(mtk_dp,
				      REG_3480_DP_TRANS_P0,
				      0, REQ_BLOCK_CIPHER_AUTH_DP_TRANS_P0_FLDMASK);
	}
}

static bool mtk_dp_hdcp1x_get_r0_available(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	bool R0_available;
	u32 ret;

	ret = mtk_dp_read_v2(mtk_dp, REG_34A4_DP_TRANS_P0);
	if (ret & R0_AVAILABLE_DP_TRANS_P0_FLDMASK)
		R0_available = true;
	else
		R0_available = false;

	return R0_available;
}

static void mtk_dp_hdcp1x_set_repeater(struct mtk_hdcp_info *hdcp_info, bool enable)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);

	if (enable)
		mtk_dp_update_bits_v2(mtk_dp, REG_34A4_DP_TRANS_P0,
				      REPEATER_I_DP_TRANS_P0_FLDMASK, REPEATER_I_DP_TRANS_P0_FLDMASK);
	else
		mtk_dp_update_bits_v2(mtk_dp, REG_34A4_DP_TRANS_P0,
				      0, REPEATER_I_DP_TRANS_P0_FLDMASK);
}

static int mtk_dp_hdcp1x_init(struct mtk_hdcp_info *hdcp_info)
{
	int ret;
	u8 i;

	for (i = 0; i < 5; i++) {
		hdcp_info->hdcp1x_info.b_ksv[i] = 0x00;
		hdcp_info->hdcp1x_info.a_ksv[i] = 0x00;
	}

	for (i = 0; i < 5; i++)
		hdcp_info->hdcp1x_info.v[i] = 0x00;

	hdcp_info->hdcp1x_info.b_info[0] = 0x00;
	hdcp_info->hdcp1x_info.b_info[1] = 0x00;
	hdcp_info->hdcp1x_info.device_count = 0x00;

	for (i = 0; i < DP_ENCODER_NUM; i++) {
		ret = tee_hdcp_update_encrypt(i, hdcp_info, false, HDCP_NONE);
		if (ret)
			return ret;
	}

	mtk_dp_hdcp1x_start_cipher(hdcp_info, false);

	for (i = 0; i < DP_ENCODER_NUM; i++) {
		ret = tee_hdcp1x_soft_rst(i, hdcp_info);
		if (ret)
			return ret;
	}

	return 0;
}

static int mtk_dp_hdcp1x_read_sink_b_ksv(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	u8 read_buffer[DRM_HDCP_KSV_LEN], i;
	ssize_t ret;

	if (hdcp_info->hdcp1x_info.capable) {
		ret = drm_dp_dpcd_read(&mtk_dp->aux,
				       DP_AUX_HDCP_BKSV, read_buffer, DRM_HDCP_KSV_LEN);
		if (ret < 0)
			return ret;

		for (i = 0; i < DRM_HDCP_KSV_LEN; i++) {
			hdcp_info->hdcp1x_info.b_ksv[i] = read_buffer[i];
			dev_dbg(mtk_dp->dev, "[HDCP1.X] Bksv:0x%x\n", read_buffer[i]);
		}
	}

	return 0;
}

static int mtk_dp_hdcp1x_check_sink_ksv_ready(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	u8 read_buffer;
	ssize_t ret;

	ret = drm_dp_dpcd_read(&mtk_dp->aux, DP_AUX_HDCP_BSTATUS, &read_buffer, 1);
	if (ret < 0)
		return ret;

	return (read_buffer & DP_BSTATUS_READY)  ? 0 : -EAGAIN;
}

static int mtk_dp_hdcp1x_read_sink_b_info(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	u8 read_buffer[DRM_HDCP_BSTATUS_LEN];
	ssize_t ret;

	ret = drm_dp_dpcd_read(&mtk_dp->aux, DP_AUX_HDCP_BINFO, read_buffer, DRM_HDCP_BSTATUS_LEN);
	if (ret < 0)
		return ret;

	hdcp_info->hdcp1x_info.b_info[0] = read_buffer[0];
	hdcp_info->hdcp1x_info.b_info[1] = read_buffer[1];
	hdcp_info->hdcp1x_info.device_count = DRM_HDCP_NUM_DOWNSTREAM(read_buffer[0]);

	dev_dbg(mtk_dp->dev, "[HDCP1.X] Binfo max_cascade_EXCEEDED:%lu\n",
		DRM_HDCP_MAX_CASCADE_EXCEEDED(read_buffer[1]));
	dev_dbg(mtk_dp->dev, "[HDCP1.X] Binfo DEPTH:%d\n", read_buffer[1] & 0x07);
	dev_dbg(mtk_dp->dev, "[HDCP1.X] Binfo max_devs_EXCEEDED:%lu\n",
		DRM_HDCP_MAX_DEVICE_EXCEEDED(read_buffer[0]));
	dev_dbg(mtk_dp->dev, "[HDCP1.X] Binfo device_count:%d\n",
		hdcp_info->hdcp1x_info.device_count);

	return 0;
}

static int mtk_dp_hdcp1x_read_sink_ksv(struct mtk_hdcp_info *hdcp_info, u8 dev_count)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	u8 times = dev_count / 3;
	u8 remain = dev_count % 3;
	ssize_t ret;
	u8 i;

	dev_dbg(mtk_dp->dev, "[HDCP1.X] dev_count:%d\n", dev_count);

	if (times > 0) {
		for (i = 0; i < times; i++) {
			ret = drm_dp_dpcd_read(&mtk_dp->aux, DP_AUX_HDCP_KSV_FIFO,
					       hdcp_info->hdcp1x_info.ksvfifo + i * 15, 15);
			if (ret < 0)
				return ret;
			dev_dbg(mtk_dp->dev, "[HDCP1.X] Read ksvfifo[%d]:0x%x\n",
				i * 15, hdcp_info->hdcp1x_info.ksvfifo[i * 15]);
		}
	}

	if (remain > 0) {
		ret = drm_dp_dpcd_read(&mtk_dp->aux, DP_AUX_HDCP_KSV_FIFO,
				       hdcp_info->hdcp1x_info.ksvfifo + times * 15, remain * 5);
		if (ret < 0)
			return ret;
		dev_dbg(mtk_dp->dev, "[HDCP1.X] Read ksvfifo[%d]:0x%x\n",
			times * 15, hdcp_info->hdcp1x_info.ksvfifo[times * 15]);
	}

	return 0;
}

static int mtk_dp_hdcp1x_read_sink_sha_v(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	u8 read_buffer[4], i, j;
	ssize_t ret;

	for (i = 0; i < 5; i++) {
		ret = drm_dp_dpcd_read(&mtk_dp->aux, DP_AUX_HDCP_V_PRIME(i), read_buffer, 4);
		if (ret < 0)
			return ret;

		for (j = 0; j < 4; j++) {
			hdcp_info->hdcp1x_info.v[(i * 4) + j] = read_buffer[3 - j];
			dev_dbg(mtk_dp->dev, "[HDCP1.X] Read sink V:0x%x\n",
				hdcp_info->hdcp1x_info.v[(i * 4) + j]);
		}
	}

	return 0;
}

static int mtk_dp_hdcp1x_auth_with_repeater(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	u8 *buffer;
	u32 len;
	int ret;
	u8 id;

	id = mtk_dp->mst_enable ? DP_ENCODER_ID_0 : DP_SST_ENCODER_PORT;

	if (hdcp_info->hdcp1x_info.device_count > HDCP1X_REP_MAXDEVS) {
		dev_err(mtk_dp->dev, "[HDCP1.X] Repeater:%d exceed max devs\n",
			hdcp_info->hdcp1x_info.device_count);
		return -EINVAL;
	}

	ret = mtk_dp_hdcp1x_read_sink_ksv(hdcp_info, hdcp_info->hdcp1x_info.device_count);
	if (ret)
		return ret;

	ret = mtk_dp_hdcp1x_read_sink_sha_v(hdcp_info);
	if (ret)
		return ret;

	len = hdcp_info->hdcp1x_info.device_count * DRM_HDCP_KSV_LEN + HDCP1X_B_INFO_LEN;
	buffer = kmalloc(len, GFP_KERNEL);
	if (!buffer) {
		dev_err(mtk_dp->dev, "[HDCP1.X] Out of Memory\n");
		return -ENOMEM;
	}

	memcpy(buffer, hdcp_info->hdcp1x_info.ksvfifo, len - HDCP1X_B_INFO_LEN);
	memcpy(buffer + (len - HDCP1X_B_INFO_LEN), hdcp_info->hdcp1x_info.b_info,
	       HDCP1X_B_INFO_LEN);
	ret = tee_hdcp1x_compute_compare_v(id, hdcp_info, buffer, len, hdcp_info->hdcp1x_info.v);
	if (!ret)
		dev_dbg(mtk_dp->dev, "[HDCP1.X] Check V' pass\n");
	else
		dev_err(mtk_dp->dev, "[HDCP1.X] Check V' fail\n");

	kfree(buffer);

	return ret;
}

static int mtk_dp_hdcp1x_verify_b_ksv(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	int i, j, k = 0;
	u8 ksv;

	for (i = 0; i < DRM_HDCP_KSV_LEN; i++) {
		ksv = hdcp_info->hdcp1x_info.b_ksv[i];
		for (j = 0; j < 8; j++)
			k += (ksv >> j) & 0x01;
	}

	if (k != 20) {
		dev_err(mtk_dp->dev, "[HDCP1.X] Check BKSV 20'1' 20'0' fail\n");
		return -EINVAL;
	}

	return 0;
}

static int mtk_dp_hdcp1x_write_a_ksv(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	int i, k, j;
	ssize_t ret;
	u8 tmp;

	ret = tee_get_aksv(hdcp_info, hdcp_info->hdcp1x_info.a_ksv);
	if (ret)
		return ret;

	ret = drm_dp_dpcd_write(&mtk_dp->aux, DP_AUX_HDCP_AKSV, hdcp_info->hdcp1x_info.a_ksv,
				DRM_HDCP_KSV_LEN);
	if (ret < 0)
		return ret;

	for (i = 0, k = 0; i < DRM_HDCP_KSV_LEN; i++) {
		tmp = hdcp_info->hdcp1x_info.a_ksv[i];

		for (j = 0; j < 8; j++)
			k += (tmp >> j) & 0x01;
		dev_dbg(mtk_dp->dev, "[HDCP1.X] Aksv:0x%x\n", tmp);
	}

	if (k != 20) {
		dev_err(mtk_dp->dev, "[HDCP1.X] Check AKSV 20'1' 20'0' fail\n");
		return -EINVAL;
	}

	return 0;
}

static int mtk_dp_hdcp1x_write_an(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	u8 an_value[DRM_HDCP_AN_LEN] = { /* on DP Spec p99 */
		0x03, 0x04, 0x07, 0x0C, 0x13, 0x1C, 0x27, 0x34};
	int ret;
	u8 id;

	if (!mtk_dp->mst_enable) {
		ret = tee_hdcp1x_set_tx_an(DP_SST_ENCODER_PORT, hdcp_info, an_value);
		if (ret)
			return ret;
	} else {
		for (id = 0; id < DP_ENCODER_NUM; id++) {
			ret = tee_hdcp1x_set_tx_an(id, hdcp_info, an_value);
			if (ret)
				return ret;
		}
	}

	ret = drm_dp_dpcd_write(&mtk_dp->aux, DP_AUX_HDCP_AN, an_value, DRM_HDCP_AN_LEN);
	if (ret < 0)
		return ret;

	mdelay(5);

	return 0;
}

static int mtk_dp_hdcp1x_check_r0(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	u8 value[DRM_HDCP_BSTATUS_LEN];
	bool sink_R0_available = false;
	int i, tries;
	ssize_t ret;
	bool tmp;
	u8 id;

	id = mtk_dp->mst_enable ? DP_ENCODER_ID_0 : DP_SST_ENCODER_PORT;

	tmp = mtk_dp_hdcp1x_get_r0_available(hdcp_info);
	if (!tmp) {
		dev_err(mtk_dp->dev, "[HDCP1.X] Fail to get R0 available\n");
		return -EINVAL;
	}

	tries = 2;
	for (i = 0; i < tries; i++) {
		ret = drm_dp_dpcd_read(&mtk_dp->aux, DP_AUX_HDCP_BSTATUS, value, 1);
		if (ret < 0)
			continue;

		sink_R0_available = (value[0x0] & DP_BSTATUS_R0_PRIME_READY) ? true : false;
		if (sink_R0_available)
			break;
	}

	if (i == tries) {
		dev_err(mtk_dp->dev, "[HDCP1.X] R0 no available\n");
		return -EINVAL;
	}

	tries = 3;
	for (i = 0; i < tries; i++) {
		ret = drm_dp_dpcd_read(&mtk_dp->aux, DP_AUX_HDCP_RI_PRIME, value, DRM_HDCP_RI_LEN);
		if (ret == DRM_HDCP_RI_LEN) {
			ret = tee_compare_r0(id, hdcp_info, value, DRM_HDCP_RI_LEN);
			if (!ret)
				return ret;
		}

		dev_dbg(mtk_dp->dev, "[HDCP1.X] R0 check FAIL, Rx_R0:0x%x, 0x%x, retry\n",
			value[0x1], value[0x0]);
		mdelay(5);
	}

	dev_err(mtk_dp->dev, "[HDCP1.X] R0 check fail\n");
	return -EINVAL;
}

static int mtk_dp_hdcp1x_update_encrypt(struct mtk_hdcp_info *hdcp_info, bool enable)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	int ret;
	u8 id;

	if (!mtk_dp->mst_enable) {
		ret = tee_hdcp_update_encrypt(DP_SST_ENCODER_PORT, hdcp_info, enable, enable ? HDCP_V1 : HDCP_NONE);
		if (ret)
			return ret;

		mtk_dp_update_bits_v2(mtk_dp, REG_3000_DP_ENCODER0_P0,
				      enable ? HDCP_FRAME_EN_DP_ENCODER0_P0_FLDMASK : 0,
				      HDCP_FRAME_EN_DP_ENCODER0_P0_FLDMASK);

		return 0;
	}

	ret = tee_hdcp_toggle_encrypt(hdcp_info);
	if (ret)
		return ret;

	mtk_dp_update_bits_v2(mtk_dp, REG_3984_DP_MST_DPTX, enable ? BIT(0) : 0, BIT(0));
	ret = tee_hdcp_toggle_encrypt(hdcp_info);
	if (ret)
		return ret;

	for (id = 0; id < DP_ENCODER_NUM; id++) {
		ret = tee_hdcp_update_encrypt(id, hdcp_info, enable, enable ? HDCP_V1 : HDCP_NONE);
		if (ret)
			return ret;
	}

	return 0;
}

/* Implements Part 1 of the HDCP authorization procedure */
static int mtk_dp_hdcp1x_auth(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	int ret, i, tries = 2;
	bool success = false;
	ktime_t end;
	u8 id;

	if (!hdcp_info->hdcp1x_info.capable)
		return -EAGAIN;

	ret = mtk_dp_hdcp1x_init(hdcp_info);
	if (ret)
		return ret;

	ret = mtk_dp_hdcp1x_write_an(hdcp_info);
	if (ret)
		return ret;
	ret = mtk_dp_hdcp1x_write_a_ksv(hdcp_info);
	if (ret)
		return ret;

	for (i = 0; i < tries; i++) {
		ret = mtk_dp_hdcp1x_read_sink_b_ksv(hdcp_info);
		if (ret)
			continue;

		ret = mtk_dp_hdcp1x_verify_b_ksv(hdcp_info);
		if (!ret)
			break;
	}
	if (i == tries)
		return -ENODEV;
	if (drm_hdcp_check_ksvs_revoked(mtk_dp->drm_dev, hdcp_info->hdcp1x_info.b_ksv, 1) > 0) {
		dev_err(mtk_dp->dev, "[HDCP1.X] BKSV is revoked\n");
		return -EPERM;
	}

	mtk_dp_hdcp1x_set_repeater(hdcp_info, hdcp_info->hdcp1x_info.repeater);

	if (!mtk_dp->mst_enable) {
		ret = tee_calculate_lm(DP_SST_ENCODER_PORT, hdcp_info, hdcp_info->hdcp1x_info.b_ksv);
		if (ret)
			return ret;
	} else {
		for (id = 0; id < DP_ENCODER_NUM; id++) {
			ret = tee_calculate_lm(id, hdcp_info, hdcp_info->hdcp1x_info.b_ksv);
			if (ret)
				return ret;
		}
	}

	mtk_dp_hdcp1x_start_cipher(hdcp_info, true);

	/* Wait 100ms(at least) before check R0 */
	msleep(HDCP1X_R0_WDT);
	ret = mtk_dp_hdcp1x_check_r0(hdcp_info);
	if (ret)
		return ret;

	ret = mtk_dp_hdcp1x_update_encrypt(hdcp_info, true);
	if (ret)
		return ret;

	if (!hdcp_info->hdcp1x_info.repeater)
		return 0;

	/* Check ksv ready (defined max time as 5s in spec) */
	end = ktime_add_ms(ktime_get_raw(), HDCP1X_REP_RDY_WDT);
	while(!ktime_after(ktime_get_raw(), end)) {
    		ret = mtk_dp_hdcp1x_check_sink_ksv_ready(hdcp_info);
    		if (!ret) {
        		success = true;
        		break;
    		}
    		msleep(100);
	}

	if (!success) {
    		ret = -ETIMEDOUT;
    		dev_err(mtk_dp->dev, "[HDCP1.X] Check sink ksv ready timeout\n");
    		goto fail;
	}

	ret = mtk_dp_hdcp1x_check_sink_ksv_ready(hdcp_info);
	if (ret)
		goto fail;

	ret = mtk_dp_hdcp1x_read_sink_b_info(hdcp_info);
	if (ret)
		goto fail;

	ret = mtk_dp_hdcp1x_auth_with_repeater(hdcp_info);
	if (ret)
		goto fail;

	return 0;

fail:
	mtk_dp_hdcp1x_update_encrypt(hdcp_info, false);

	return ret;
}

int mtk_dp_hdcp1x_enable(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	int ret = 0, i, tries = 3;

	hdcp_info->auth_status = AUTH_INIT;

	ret = tee_add_device(hdcp_info, HDCP_VERSION_1X);
	if (ret)
		goto fail;

	for (i = 0; i < tries; i++) {
		ret = mtk_dp_hdcp1x_auth(hdcp_info);
		if (!ret) {
			hdcp_info->auth_version = HDCP_VERSION_1X;
			hdcp_info->auth_status = AUTH_PASS;
			dev_dbg(mtk_dp->dev, "[HDCP1.X] Authentication done\n");

			return 0;
		}
	}

	tee_remove_device(hdcp_info);

fail:
	hdcp_info->auth_status = AUTH_FAIL;
	dev_err(mtk_dp->dev, "[HDCP1.X] Authentication fail\n");

	return ret;
}

int mtk_dp_hdcp1x_disable(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);

	if (hdcp_info->auth_status == AUTH_PASS) {
		mtk_dp_hdcp1x_start_cipher(hdcp_info, false);
		mtk_dp_hdcp1x_update_encrypt(hdcp_info, false);
	}

	tee_remove_device(hdcp_info);

	hdcp_info->auth_version = HDCP_NONE;
	hdcp_info->auth_status = AUTH_ZERO;
	dev_dbg(mtk_dp->dev, "[HDCP1.X] Disable Authentication\n");

	return 0;
}

int mtk_dp_hdcp1x_check_link(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	int ret = -EINVAL;
	u8 bstatus;

	guard(mutex)(&mtk_dp->hdcp_mutex);

	if (mtk_dp->hdcp_info.auth_status != AUTH_PASS)
		return -EINVAL;

	if (!mtk_dp->training_info.cable_plug_in)
		goto err;

	ret = drm_dp_dpcd_read(&mtk_dp->aux, DP_AUX_HDCP_BSTATUS, &bstatus, 1);
	if (ret != 1) {
		dev_err(mtk_dp->dev, "[HDCP1.X] Read bstatus failed, reauth\n");
		goto err;
	}

	ret = bstatus & (DP_BSTATUS_LINK_FAILURE | DP_BSTATUS_REAUTH_REQ);
	if (ret) {
		dev_err(mtk_dp->dev, "[HDCP1.X] link failed / reauth req:0x%x\n", bstatus);
		goto err;
	}

	return ret;

err:
	ret = mtk_dp_hdcp1x_disable(hdcp_info);
	if (ret || !mtk_dp->training_info.cable_plug_in)
		return -EAGAIN;
	mtk_dp_hdcp_update_value(mtk_dp);

	ret = mtk_dp_hdcp1x_enable(hdcp_info);
	if (!ret)
		mtk_dp_hdcp_update_value(mtk_dp);

	return ret;
}
#endif

void mtk_dp_hdcp1x_get_info(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	u8 tmp[2];
	ssize_t ret;

	ret = drm_dp_dpcd_read(&mtk_dp->aux, DP_AUX_HDCP_BCAPS, tmp, 0x1);
	if (ret < 0)
		return;

	hdcp_info->hdcp1x_info.capable = tmp[0x0] & DP_BCAPS_HDCP_CAPABLE;
	hdcp_info->hdcp1x_info.repeater = tmp[0x0] & DP_BCAPS_REPEATER_PRESENT;

	dev_dbg(mtk_dp->dev, "[HDCP1.X] Capable:%d, Reapeater:%d\n",
		hdcp_info->hdcp1x_info.capable,
		hdcp_info->hdcp1x_info.repeater);
}
