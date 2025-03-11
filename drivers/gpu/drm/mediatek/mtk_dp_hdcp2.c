// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019-2024 MediaTek Inc.
 */

#include <drm/display/drm_hdcp_helper.h>

#include "mtk_dp_hdcp2.h"
#include "mtk_dp_reg_v2.h"
#include "mtk_dp_v2.h"

#define HDCP2_LC_RETRY_CNT			3

#if IS_ENABLED(CONFIG_TEE)
static u8 t_rtx[HDCP_2_2_RTX_LEN] = {
	0x18, 0xfa, 0xe4, 0x20, 0x6a, 0xfb, 0x51, 0x49
};

static u8 t_tx_caps[HDCP_2_2_TXCAPS_LEN] = {
	0x02, 0x00, 0x00
};

static u8 t_rn[HDCP_2_2_RN_LEN] = {
	0x32, 0x75, 0x3e, 0xa8, 0x78, 0xa6, 0x38, 0x1c
};

static u8 t_riv[HDCP_2_2_RIV_LEN] = {
	0x40, 0x2b, 0x6b, 0x43, 0xc5, 0xe8, 0x86, 0xd8
};

static void mtk_dp_hdcp2x_fill_stream_type(struct mtk_hdcp_info *hdcp_info, u8 uc_type)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);

	mtk_dp_update_bits_v2(mtk_dp, REG_34D0_DP_TRANS_P0, uc_type, 0xff);
}

static int mtk_dp_hdcp2x_set_auth_pass(struct mtk_hdcp_info *hdcp_info, bool enable)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	int max_wait = 0;
	int ret;
	u8 id;

	if (enable) {
		mtk_dp_update_bits_v2(mtk_dp, REG_3400_DP_TRANS_P0,
				      HDCP_SEL_DP_TRANS_P0_FLDMASK, HDCP_SEL_DP_TRANS_P0_FLDMASK);
		mtk_dp_update_bits_v2(mtk_dp, REG_34A4_DP_TRANS_P0,
				      HDCP22_AUTH_DONE_DP_TRANS_P0_FLDMASK,
				      HDCP22_AUTH_DONE_DP_TRANS_P0_FLDMASK);
	} else {
		if (!mtk_dp->mst_enable) {
			ret = tee_hdcp_update_encrypt(DP_SST_ENCODER_PORT, hdcp_info, enable, HDCP_NONE);
			if (ret)
				return ret;
		} else {
			for (id = 0; id < DP_ENCODER_NUM; id++) {
				ret = tee_hdcp_update_encrypt(id, hdcp_info, enable, HDCP_NONE);
				if (ret)
					return ret;
			}
		}

		do {
			usleep_range(200, 300);
			max_wait++;
		} while ((mtk_dp_read_v2(mtk_dp, REG_34A4_DP_TRANS_P0 + 1) & BIT(5)) && max_wait < 10);

		mtk_dp_update_bits_v2(mtk_dp, REG_3400_DP_TRANS_P0, 0, HDCP_SEL_DP_TRANS_P0_FLDMASK);
		mtk_dp_update_bits_v2(mtk_dp, REG_34A4_DP_TRANS_P0, 0, HDCP22_AUTH_DONE_DP_TRANS_P0_FLDMASK);

		if (!mtk_dp->mst_enable) {
			ret = tee_hdcp2_soft_rst(DP_SST_ENCODER_PORT, hdcp_info);
			if (ret)
				return ret;
		} else {
			for (id = 0; id < DP_ENCODER_NUM; id++) {
				ret = tee_hdcp2_soft_rst(id, hdcp_info);
				if (ret)
					return ret;
			}
		}
	}

	return 0;
}

static int mtk_dp_hdcp2x_update_encrypt(struct mtk_hdcp_info *hdcp_info, bool enable, u32 version)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	int ret;
	u8 id;

	if (!mtk_dp->mst_enable) {
		ret = tee_hdcp_update_encrypt(DP_SST_ENCODER_PORT, hdcp_info, enable, version);
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
		ret = tee_hdcp_update_encrypt(id, hdcp_info, enable, version);
		if (ret)
			return ret;
	}

	return 0;
}

static int mtk_dp_hdcp2x_init(struct mtk_hdcp_info *hdcp_info)
{
	int ret;

	memset(&hdcp_info->hdcp2_info.hdcp_tx, 0, sizeof(struct hdcp2_info_tx));
	memset(&hdcp_info->hdcp2_info.hdcp_rx, 0, sizeof(struct hdcp2_info_rx));
	memset(&hdcp_info->hdcp2_info.ake_stored_km, 0, sizeof(struct hdcp2_ake_stored_km));
	memcpy(hdcp_info->hdcp2_info.hdcp_tx.ake_init.r_tx, t_rtx, HDCP_2_2_RTX_LEN);
	memcpy(&hdcp_info->hdcp2_info.hdcp_tx.tx_caps, t_tx_caps, HDCP_2_2_TXCAPS_LEN);
	memcpy(hdcp_info->hdcp2_info.hdcp_tx.lc_init.r_n, t_rn, HDCP_2_2_RN_LEN);
	memcpy(hdcp_info->hdcp2_info.hdcp_tx.send_eks.riv, t_riv, HDCP_2_2_RIV_LEN);

	ret = mtk_dp_hdcp2x_update_encrypt(hdcp_info, false, HDCP_NONE);
	if (ret)
		return ret;

	return mtk_dp_hdcp2x_set_auth_pass(hdcp_info, false);
}

static int mtk_dp_hdcp2x_recv_rep_auth_send_recv_id_list(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	u32 len = 0, len_recv_id_list = 0;
	u8 *buffer = NULL;
	int ret = 0;

	len_recv_id_list = hdcp_info->hdcp2_info.device_count * HDCP_2_2_RECEIVER_ID_LEN;
	len = len_recv_id_list + HDCP_2_2_RXINFO_LEN + HDCP_2_2_SEQ_NUM_LEN;
	buffer = kmalloc(len, GFP_KERNEL);
	if (!buffer) {
		dev_err(mtk_dp->dev, "[HDCP2.X] Out of memory\n");
		return -ENOMEM;
	}

	memcpy(buffer, hdcp_info->hdcp2_info.hdcp_rx.recvid_list.receiver_ids,
	       len_recv_id_list);
	memcpy(buffer + len_recv_id_list,
	       hdcp_info->hdcp2_info.hdcp_rx.recvid_list.rx_info, HDCP_2_2_RXINFO_LEN);
	memcpy(buffer + len_recv_id_list + HDCP_2_2_RXINFO_LEN,
	       hdcp_info->hdcp2_info.hdcp_rx.recvid_list.seq_num_v, HDCP_2_2_SEQ_NUM_LEN);

	ret = tee_hdcp2_compute_compare_v(hdcp_info, buffer, len,
					  hdcp_info->hdcp2_info.hdcp_rx.recvid_list.v_prime,
					  hdcp_info->hdcp2_info.hdcp_tx.send_ack.v);

	kfree(buffer);

	return ret;
}

static int mtk_dp_hdcp2x_verify_mprime(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	u8 *buffer = NULL;
	u32 id_type_len = 0;
	u32 len = 0;
	int ret = 0;

	id_type_len = hdcp_info->hdcp2_info.hdcp_tx.k * sizeof(struct hdcp2_streamid_type);
	len = id_type_len + HDCP_2_2_SEQ_NUM_LEN;
	buffer = kmalloc(len, GFP_KERNEL);
	if (!buffer) {
		dev_err(mtk_dp->dev, "[HDCP2.X] Out of memory\n");
		return -ENOMEM;
	}

	memcpy(buffer, hdcp_info->hdcp2_info.hdcp_tx.stream_manage.streams, id_type_len);
	memcpy(buffer + id_type_len, &hdcp_info->hdcp2_info.hdcp_tx.seq_num_m, HDCP_2_2_SEQ_NUM_LEN);
	ret = tee_hdcp2_compute_compare_m(hdcp_info, buffer, len,
					  hdcp_info->hdcp2_info.hdcp_rx.stream_ready.m_prime);

	kfree(buffer);

	return ret;
}

static void mtk_dp_hdcp2x_wait_for_cp_irq(struct mtk_hdcp_info *hdcp_info, int timeout)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	long ret;

	ret = wait_event_interruptible_timeout(hdcp_info->hdcp2_info.cp_irq_queue,
			hdcp_info->hdcp2_info.cp_irq_cached != atomic_read(&hdcp_info->hdcp2_info.cp_irq),
			msecs_to_jiffies(timeout));
	if (!ret)
		dev_info(mtk_dp->dev, "[HDCP2.X] Timedout at waiting for CP_IRQ\n");
}

static int mtk_dp_hdcp2x_read_ake_send_cert(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	ktime_t msg_end;
	bool msg_expired;
	ssize_t ret;

	dev_dbg(mtk_dp->dev, "[HDCP2.X] HDCP_2_2_AKE_SEND_CERT\n");

	mdelay(HDCP_2_2_CERT_TIMEOUT_MS);

	msg_end = ktime_add_ms(ktime_get_raw(), HDCP_2_2_DP_CERT_READ_TIMEOUT_MS);

	ret = drm_dp_dpcd_read(&mtk_dp->aux, DP_HDCP_2_2_AKE_SEND_CERT_OFFSET,
			       (void *)&hdcp_info->hdcp2_info.hdcp_rx.cert_rx, HDCP2_CERTRX_LEN);
	if (ret < 0)
		return ret;

	msg_expired = ktime_after(ktime_get_raw(), msg_end);
	if (msg_expired)
		dev_info(mtk_dp->dev, "[HDCP2.X] Timeout to read Ake send cert\n");

	ret = drm_dp_dpcd_read(&mtk_dp->aux, DP_HDCP_2_2_REG_RRX_OFFSET,
			       hdcp_info->hdcp2_info.hdcp_rx.send_cert.r_rx, HDCP_2_2_RRX_LEN);
	if (ret < 0)
		return ret;

	ret = drm_dp_dpcd_read(&mtk_dp->aux, DP_HDCP_2_2_REG_RX_CAPS_OFFSET,
			       hdcp_info->hdcp2_info.hdcp_rx.send_cert.rx_caps,
		HDCP_2_2_RXCAPS_LEN);
	if (ret < 0)
		return ret;

	return 0;
}

static int mtk_dp_hdcp2x_read_ake_send_hprime(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	ktime_t msg_end;
	bool msg_expired;
	u8 rx_status = 0;
	int timeout;
	ssize_t ret;

	dev_dbg(mtk_dp->dev, "[HDCP2.X] HDCP_2_2_AKE_SEND_HPRIME\n");

	timeout = hdcp_info->hdcp2_info.stored_km ?
		HDCP_2_2_HPRIME_PAIRED_TIMEOUT_MS : HDCP_2_2_HPRIME_NO_PAIRED_TIMEOUT_MS;

	mtk_dp_hdcp2x_wait_for_cp_irq(hdcp_info, timeout);
	hdcp_info->hdcp2_info.cp_irq_cached = atomic_read(&hdcp_info->hdcp2_info.cp_irq);

	ret = drm_dp_dpcd_read(&mtk_dp->aux, DP_HDCP_2_2_REG_RXSTATUS_OFFSET, &rx_status,
			       HDCP_2_2_DP_RXSTATUS_LEN);
	if (ret != HDCP_2_2_DP_RXSTATUS_LEN)
		return ret >= 0 ? -EIO : ret;

	if (!HDCP_2_2_DP_RXSTATUS_H_PRIME(rx_status))
		return -EAGAIN;

	msg_end = ktime_add_ms(ktime_get_raw(), HDCP_2_2_DP_HPRIME_READ_TIMEOUT_MS);

	ret = drm_dp_dpcd_read(&mtk_dp->aux, DP_HDCP_2_2_AKE_SEND_HPRIME_OFFSET,
			       hdcp_info->hdcp2_info.hdcp_rx.send_hprime.h_prime,
			       HDCP_2_2_H_PRIME_LEN);
	if (ret < 0)
		return ret;

	msg_expired = ktime_after(ktime_get_raw(), msg_end);
	if (msg_expired)
		dev_info(mtk_dp->dev, "[HDCP2.X] Timeout to read AKE hprime\n");

	return 0;
}

static int mtk_dp_hdcp2x_read_ake_send_pairing_info(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	ktime_t msg_end;
	bool msg_expired;
	u8 rx_status = 0;
	ssize_t ret;

	dev_dbg(mtk_dp->dev, "[HDCP2.X] HDCP_2_2_AKE_SEND_PAIRING_INFO\n");

	mtk_dp_hdcp2x_wait_for_cp_irq(hdcp_info, HDCP_2_2_PAIRING_TIMEOUT_MS);
	hdcp_info->hdcp2_info.cp_irq_cached = atomic_read(&hdcp_info->hdcp2_info.cp_irq);

	ret = drm_dp_dpcd_read(&mtk_dp->aux, DP_HDCP_2_2_REG_RXSTATUS_OFFSET, &rx_status,
			       HDCP_2_2_DP_RXSTATUS_LEN);
	if (ret != HDCP_2_2_DP_RXSTATUS_LEN)
		return ret >= 0 ? -EIO : ret;

	if (!HDCP_2_2_DP_RXSTATUS_PAIRING(rx_status))
		return -EAGAIN;

	msg_end = ktime_add_ms(ktime_get_raw(), HDCP_2_2_DP_PAIRING_READ_TIMEOUT_MS);

	ret = drm_dp_dpcd_read(&mtk_dp->aux, DP_HDCP_2_2_AKE_SEND_PAIRING_INFO_OFFSET,
			       hdcp_info->hdcp2_info.hdcp_rx.pairing_info.e_kh_km,
			       HDCP_2_2_E_KH_KM_LEN);
	if (ret < 0)
		return ret;

	msg_expired = ktime_after(ktime_get_raw(), msg_end);
	if (msg_expired)
		dev_err(mtk_dp->dev, "[HDCP2.X] Timeout to read pairing info\n");

	return 0;
}

static int mtk_dp_hdcp2x_read_lc_send_lprime(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	ssize_t ret;

	dev_dbg(mtk_dp->dev, "[HDCP2.X] HDCP_2_2_LC_SEND_LPRIME\n");

	mdelay(HDCP_2_2_DP_LPRIME_TIMEOUT_MS);

	ret = drm_dp_dpcd_read(&mtk_dp->aux, DP_HDCP_2_2_LC_SEND_LPRIME_OFFSET,
			       hdcp_info->hdcp2_info.hdcp_rx.send_lprime.l_prime,
		HDCP_2_2_L_PRIME_LEN);
	if (ret < 0)
		return ret;

	return 0;
}

static int mtk_dp_hdcp2x_read_rep_send_recvid_list(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	u8 rx_status = 0;
	ssize_t ret;

	dev_dbg(mtk_dp->dev, "[HDCP2.X] HDCP_2_2_REP_SEND_RECVID_LIST\n");

	mtk_dp_hdcp2x_wait_for_cp_irq(hdcp_info, HDCP_2_2_RECVID_LIST_TIMEOUT_MS);
	hdcp_info->hdcp2_info.cp_irq_cached = atomic_read(&hdcp_info->hdcp2_info.cp_irq);

	ret = drm_dp_dpcd_read(&mtk_dp->aux, DP_HDCP_2_2_REG_RXSTATUS_OFFSET, &rx_status,
			       HDCP_2_2_DP_RXSTATUS_LEN);
	if (ret != HDCP_2_2_DP_RXSTATUS_LEN)
		return ret >= 0 ? -EIO : ret;

	if (!HDCP_2_2_DP_RXSTATUS_READY(rx_status)) {
		dev_err(mtk_dp->dev, "[HDCP2.X] RX status no ready\n");
		return -EAGAIN;
	}

	ret = drm_dp_dpcd_read(&mtk_dp->aux, DP_HDCP_2_2_REP_SEND_RECVID_LIST_OFFSET,
			       hdcp_info->hdcp2_info.hdcp_rx.recvid_list.rx_info,
			       HDCP_2_2_RXINFO_LEN);
	if (ret < 0)
		return ret;

	ret = tee_hdcp2_set_rx_info(hdcp_info, hdcp_info->hdcp2_info.hdcp_rx.recvid_list.rx_info);
	if (ret)
		return ret;

	hdcp_info->hdcp2_info.device_count =
	(HDCP_2_2_DEV_COUNT_HI(hdcp_info->hdcp2_info.hdcp_rx.recvid_list.rx_info[0]) << 4 |
	HDCP_2_2_DEV_COUNT_LO(hdcp_info->hdcp2_info.hdcp_rx.recvid_list.rx_info[1]));

	ret = drm_dp_dpcd_read(&mtk_dp->aux, DP_HDCP_2_2_REG_SEQ_NUM_V_OFFSET,
			       hdcp_info->hdcp2_info.hdcp_rx.recvid_list.seq_num_v,
			       HDCP_2_2_SEQ_NUM_LEN);
	if (ret < 0)
		return ret;

	ret = drm_dp_dpcd_read(&mtk_dp->aux, DP_HDCP_2_2_REG_VPRIME_OFFSET,
			       hdcp_info->hdcp2_info.hdcp_rx.recvid_list.v_prime,
			       HDCP_2_2_V_PRIME_HALF_LEN);
	if (ret < 0)
		return ret;

	ret = drm_dp_dpcd_read(&mtk_dp->aux, DP_HDCP_2_2_REG_RECV_ID_LIST_OFFSET,
			       hdcp_info->hdcp2_info.hdcp_rx.recvid_list.receiver_ids,
			       hdcp_info->hdcp2_info.device_count * HDCP_2_2_RECEIVER_ID_LEN);
	if (ret < 0)
		return ret;

	return 0;
}

static int mtk_dp_hdcp2x_read_rep_stream_ready(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	ssize_t ret;

	dev_dbg(mtk_dp->dev, "[HDCP2.X] HDCP_2_2_REP_STREAM_READY\n");

	mdelay(HDCP_2_2_STREAM_READY_TIMEOUT_MS);

	ret = drm_dp_dpcd_read(&mtk_dp->aux, DP_HDCP_2_2_REP_STREAM_READY_OFFSET,
			       hdcp_info->hdcp2_info.hdcp_rx.stream_ready.m_prime,
			       HDCP_2_2_MPRIME_LEN);
	if (ret < 0)
		return ret;

	return 0;
}

static int mtk_dp_hdcp2x_write_ake_init(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	ssize_t ret;
	u8 id;

	dev_dbg(mtk_dp->dev, "[HDCP2.X] HDCP_2_2_AKE_Init\n");

	for (id = 0; id < DP_ENCODER_NUM; id++) {
		ret = tee_hdcp2_soft_rst(id, hdcp_info);
		if (ret)
			return ret;
	}

	ret = drm_dp_dpcd_write(&mtk_dp->aux, DP_HDCP_2_2_AKE_INIT_OFFSET,
				hdcp_info->hdcp2_info.hdcp_tx.ake_init.r_tx, HDCP_2_2_RTX_LEN);
	if (ret < 0)
		return ret;

	ret = drm_dp_dpcd_write(&mtk_dp->aux, DP_HDCP_2_2_REG_TXCAPS_OFFSET,
				(void *)&hdcp_info->hdcp2_info.hdcp_tx.tx_caps,
				HDCP_2_2_TXCAPS_LEN);
	if (ret < 0)
		return ret;

	return 0;
}

static int mtk_dp_hdcp2x_write_ake_no_stored_km(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	ssize_t ret;

	dev_dbg(mtk_dp->dev, "[HDCP2.X] HDCP_2_2_AKE_NO_STORED_KM\n");

	ret = drm_dp_dpcd_write(&mtk_dp->aux, DP_HDCP_2_2_AKE_NO_STORED_KM_OFFSET,
				hdcp_info->hdcp2_info.hdcp_tx.no_stored_km.e_kpub_km,
				HDCP_2_2_E_KPUB_KM_LEN);
	if (ret < 0)
		return ret;

	return 0;
}

static int mtk_dp_hdcp2x_write_ake_stored_km(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	ssize_t ret;

	dev_dbg(mtk_dp->dev, "[HDCP2.X] HDCP_2_2_AKE_STORED_KM\n");

	ret = drm_dp_dpcd_write(&mtk_dp->aux, DP_HDCP_2_2_AKE_STORED_KM_OFFSET,
				hdcp_info->hdcp2_info.ake_stored_km.e_kh_km_m,
				HDCP_2_2_E_KH_KM_LEN);
	if (ret < 0)
		return ret;

	ret = drm_dp_dpcd_write(&mtk_dp->aux, DP_HDCP_2_2_REG_M_OFFSET,
				hdcp_info->hdcp2_info.ake_stored_km.e_kh_km_m +
				HDCP_2_2_E_KH_KM_LEN,
				HDCP_2_2_E_KH_KM_M_LEN - HDCP_2_2_E_KH_KM_LEN);
	if (ret < 0)
		return ret;

	return 0;
}

static int mtk_dp_hdcp2x_write_lc_init(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	ssize_t ret;

	dev_dbg(mtk_dp->dev, "[HDCP2.X] HDCP_2_2_LC_INIT\n");

	ret = drm_dp_dpcd_write(&mtk_dp->aux, DP_HDCP_2_2_LC_INIT_OFFSET,
				hdcp_info->hdcp2_info.hdcp_tx.lc_init.r_n, HDCP_2_2_RN_LEN);
	if (ret < 0)
		return ret;

	return 0;
}

static int mtk_dp_hdcp2x_write_ske_send_eks(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	ssize_t ret;

	dev_dbg(mtk_dp->dev, "[HDCP2.X] HDCP_2_2_SKE_SEND_EKS\n");

	ret = drm_dp_dpcd_write(&mtk_dp->aux, DP_HDCP_2_2_SKE_SEND_EKS_OFFSET,
				hdcp_info->hdcp2_info.hdcp_tx.send_eks.e_dkey_ks,
		HDCP_2_2_E_DKEY_KS_LEN);
	if (ret < 0)
		return ret;

	ret = drm_dp_dpcd_write(&mtk_dp->aux, DP_HDCP_2_2_REG_RIV_OFFSET,
				hdcp_info->hdcp2_info.hdcp_tx.send_eks.riv, HDCP_2_2_RIV_LEN);
	if (ret < 0)
		return ret;

	return 0;
}

static int mtk_dp_hdcp2x_write_stream_type(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	ssize_t ret;

	dev_dbg(mtk_dp->dev, "[HDCP2.X] Write stream type\n");

	ret = drm_dp_dpcd_write(&mtk_dp->aux, DP_HDCP_2_2_REG_STREAM_TYPE_OFFSET,
				&mtk_dp->con_state[DP_SST_ENCODER_PORT].content_type, 1);
	if (ret < 0)
		return ret;

	return 0;
}

static int mtk_dp_hdcp2x_write_send_ack(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	ssize_t ret;

	dev_dbg(mtk_dp->dev, "[HDCP2.X] HDCP_2_2_SEND_ACK\n");

	ret = drm_dp_dpcd_write(&mtk_dp->aux, DP_HDCP_2_2_REP_SEND_ACK_OFFSET,
				hdcp_info->hdcp2_info.hdcp_tx.send_ack.v,
				HDCP_2_2_V_PRIME_HALF_LEN);
	if (ret < 0)
		return ret;

	return 0;
}

/* Authentication flow starts from here */
static int mtk_dp_hdcp2x_key_exchange(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	bool stored;
	int ret;

	if (!hdcp_info->hdcp2_info.capable)
		return -EAGAIN;

	ret = mtk_dp_hdcp2x_init(hdcp_info);
	if (ret)
		return ret;

	ret = mtk_dp_hdcp2x_write_ake_init(hdcp_info);
	if (ret)
		return ret;

	ret = mtk_dp_hdcp2x_read_ake_send_cert(hdcp_info);
	if (ret)
		return ret;

	hdcp_info->hdcp2_info.repeater =
		HDCP_2_2_RX_REPEATER(hdcp_info->hdcp2_info.hdcp_rx.send_cert.rx_caps[2]);

	if (drm_hdcp_check_ksvs_revoked(mtk_dp->drm_dev,
					hdcp_info->hdcp2_info.hdcp_rx.send_cert.cert_rx.receiver_id,
					1) > 0) {
		dev_err(mtk_dp->dev, "[HDCP2.X] Receiver ID is revoked\n");
		return -EPERM;
	}

	ret = tee_ake_certificate(hdcp_info,
				  (u8 *)&hdcp_info->hdcp2_info.hdcp_rx.cert_rx, &stored,
				  hdcp_info->hdcp2_info.ake_stored_km.e_kh_km_m +
				  HDCP_2_2_E_KH_KM_LEN,
				  hdcp_info->hdcp2_info.ake_stored_km.e_kh_km_m);
	if (ret)
		return ret;

	hdcp_info->hdcp2_info.stored_km = stored;

	if (!hdcp_info->hdcp2_info.stored_km) {
		ret = tee_enc_rsaes_oaep(hdcp_info,
					 hdcp_info->hdcp2_info.hdcp_tx.no_stored_km.e_kpub_km);
		if (ret)
			return ret;

		ret = mtk_dp_hdcp2x_write_ake_no_stored_km(hdcp_info);
		if (ret)
			return ret;

	} else {
		ret = mtk_dp_hdcp2x_write_ake_stored_km(hdcp_info);
		if (ret)
			return ret;
	}

	ret = mtk_dp_hdcp2x_read_ake_send_hprime(hdcp_info);
	if (ret)
		return ret;

	ret = tee_ake_h_prime(hdcp_info,
			      hdcp_info->hdcp2_info.hdcp_tx.ake_init.r_tx,
			      hdcp_info->hdcp2_info.hdcp_rx.send_cert.r_rx,
			      hdcp_info->hdcp2_info.hdcp_rx.send_cert.rx_caps,
			      (u8 *)&hdcp_info->hdcp2_info.hdcp_tx.tx_caps,
			      hdcp_info->hdcp2_info.hdcp_rx.send_hprime.h_prime,
			      HDCP_2_2_H_PRIME_LEN);
	if (ret) {
		if (hdcp_info->hdcp2_info.stored_km)
			tee_clear_paring(hdcp_info);
		return ret;
	}

	if (!hdcp_info->hdcp2_info.stored_km) {
		ret = mtk_dp_hdcp2x_read_ake_send_pairing_info(hdcp_info);
		if (ret)
			return ret;

		/* Store m, km, Ekh(km) */
		ret = tee_ake_paring(hdcp_info,
				     hdcp_info->hdcp2_info.hdcp_rx.pairing_info.e_kh_km);
		if (ret)
			return ret;
	}

	return 0;
}

static int mtk_dp_hdcp2x_locality_check(struct mtk_hdcp_info *hdcp_info)
{
	int ret, i, tries = HDCP2_LC_RETRY_CNT;

	for (i = 0; i < tries; i++) {
		ret = mtk_dp_hdcp2x_write_lc_init(hdcp_info);
		if (ret)
			continue;

		ret = mtk_dp_hdcp2x_read_lc_send_lprime(hdcp_info);
		if (ret)
			continue;

		ret = tee_lc_l_prime(hdcp_info, hdcp_info->hdcp2_info.hdcp_tx.lc_init.r_n,
				     hdcp_info->hdcp2_info.hdcp_rx.send_lprime.l_prime,
				     HDCP_2_2_L_PRIME_LEN);
		if (!ret)
			return 0;
	}

	return ret;
}

static int mtk_dp_hdcp2x_session_key_exchange(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	int ret;
	u8 id;

	if (!mtk_dp->mst_enable) {
		ret = tee_ske_enc_ks(DP_SST_ENCODER_PORT, hdcp_info,
				     hdcp_info->hdcp2_info.hdcp_tx.send_eks.riv,
				     hdcp_info->hdcp2_info.hdcp_tx.send_eks.e_dkey_ks);
		if (ret)
			return ret;
	} else {
		for (id = 0; id < DP_ENCODER_NUM; id++) {
			ret = tee_ske_enc_ks(id, hdcp_info,
					     hdcp_info->hdcp2_info.hdcp_tx.send_eks.riv,
					     hdcp_info->hdcp2_info.hdcp_tx.send_eks.e_dkey_ks);
			if (ret)
				return ret;
		}
	}

	ret = mtk_dp_hdcp2x_write_ske_send_eks(hdcp_info);
	if (ret)
		return ret;

	return 0;
}

static int mtk_dp_hdcp2x_authenticate_repeater(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	u8 *rx_info;
	int ret;

	ret = mtk_dp_hdcp2x_read_rep_send_recvid_list(hdcp_info);
	if (ret)
		return ret;

	rx_info = hdcp_info->hdcp2_info.hdcp_rx.recvid_list.rx_info;

	if (HDCP_2_2_MAX_CASCADE_EXCEEDED(rx_info[1]) ||
	    HDCP_2_2_MAX_DEVS_EXCEEDED(rx_info[1])) {
		dev_err(mtk_dp->dev, "[HDCP2.X] Topology max size exceeded\n");
		return -EINVAL;
	}

	if (drm_hdcp_check_ksvs_revoked(mtk_dp->drm_dev,
					hdcp_info->hdcp2_info.hdcp_rx.recvid_list.receiver_ids,
					hdcp_info->hdcp2_info.device_count) > 0) {
		dev_err(mtk_dp->dev, "[HDCP2.X] Revoked receiver ID(s) is in list\n");
		return -EPERM;
	}

	ret = mtk_dp_hdcp2x_recv_rep_auth_send_recv_id_list(hdcp_info);
	if (ret)
		return -EINVAL;

	ret = mtk_dp_hdcp2x_write_send_ack(hdcp_info);
	if (ret)
		return ret;

	return 0;
}

static int mtk_dp_hdcp2x_authenticate(struct mtk_hdcp_info *hdcp_info)
{
	int ret;

	ret = mtk_dp_hdcp2x_key_exchange(hdcp_info);
	if (ret)
		return ret;

	ret = mtk_dp_hdcp2x_locality_check(hdcp_info);
	if (ret)
		return ret;

	ret = mtk_dp_hdcp2x_session_key_exchange(hdcp_info);
	if (ret)
		return ret;

	if (!hdcp_info->hdcp2_info.repeater) {
		ret = mtk_dp_hdcp2x_write_stream_type(hdcp_info);
		if (ret)
			return ret;
	}

	if (hdcp_info->hdcp2_info.repeater) {
		ret = mtk_dp_hdcp2x_authenticate_repeater(hdcp_info);
		if (ret)
			return ret;
	}

	return 0;
}

static void mtk_dp_hdcp2x_prepare_streams(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	int con_id;
	u8 i = 0;
	u8 j = 0;

	if (!mtk_dp->mst_enable) {
		hdcp_info->hdcp2_info.hdcp_tx.k = 1;

		hdcp_info->hdcp2_info.hdcp_tx.stream_manage.streams[0].stream_id = 0;
		hdcp_info->hdcp2_info.hdcp_tx.stream_manage.streams[0].stream_type =
			mtk_dp->con_state[DP_SST_ENCODER_PORT].content_type;
	} else {
		for (i = 0; i < DP_ENCODER_NUM; i++) {
			con_id = encoder_id_to_con_id(mtk_dp, i, DRM_DP_MST);
			if (con_id >= 0 && mtk_dp->mtk_con[con_id]->video_enable) {
				hdcp_info->hdcp2_info.hdcp_tx.stream_manage.streams[j].stream_id = j;
				hdcp_info->hdcp2_info.hdcp_tx.stream_manage.streams[j].stream_type =
					mtk_dp->con_state[i].content_type;
				j++;
			}
		}
		hdcp_info->hdcp2_info.hdcp_tx.k = j;
	}
}

static int _mtk_dp_hdcp2x_propagate_stream_management_info(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	struct hdcp2_rep_stream_manage stream_manage;
	int ret, i;

	if (hdcp_info->hdcp2_info.hdcp_tx.seq_num_m > HDCP_2_2_SEQ_NUM_MAX)
		return -ERANGE;

	drm_hdcp_cpu_to_be24(stream_manage.seq_num_m, hdcp_info->hdcp2_info.hdcp_tx.seq_num_m);
	stream_manage.k = cpu_to_be16(hdcp_info->hdcp2_info.hdcp_tx.k);

	for (i = 0; i < hdcp_info->hdcp2_info.hdcp_tx.k; i++) {
		stream_manage.streams[i].stream_id =
			hdcp_info->hdcp2_info.hdcp_tx.stream_manage.streams[i].stream_id;
		stream_manage.streams[i].stream_type =
			hdcp_info->hdcp2_info.hdcp_tx.stream_manage.streams[i].stream_type;
	}

	/* Send it to Repeater */
	ret = drm_dp_dpcd_write(&mtk_dp->aux, DP_HDCP_2_2_REP_STREAM_MANAGE_OFFSET,
				stream_manage.seq_num_m, HDCP_2_2_SEQ_NUM_LEN);
	if (ret < 0) {
		dev_err(mtk_dp->dev, "[HDCP2.X] Failed to write seq_num_m:%d\n", ret);
		return ret;
	}

	ret = drm_dp_dpcd_write(&mtk_dp->aux, DP_HDCP_2_2_REG_K_OFFSET,
				&stream_manage.k, HDCP2_K_LEN);
	if (ret < 0) {
		dev_err(mtk_dp->dev, "[HDCP2.X] Failed to write k:%d\n", ret);
		return ret;
	}

	ret = drm_dp_dpcd_write(&mtk_dp->aux, DP_HDCP_2_2_REG_STREAM_ID_TYPE_OFFSET,
				stream_manage.streams,
				hdcp_info->hdcp2_info.hdcp_tx.k *
				sizeof(struct hdcp2_streamid_type));
	if (ret < 0) {
		dev_err(mtk_dp->dev, "[HDCP2.X] Failed to write streams info:%d\n", ret);
		return ret;
	}

	mtk_dp_hdcp2x_fill_stream_type(hdcp_info,
				       mtk_dp->con_state[DP_SST_ENCODER_PORT].content_type);

	ret = mtk_dp_hdcp2x_read_rep_stream_ready(hdcp_info);
	if (ret) {
		dev_err(mtk_dp->dev, "[HDCP2.X] Failed to read rep stream ready:%d\n", ret);
		return ret;
	}

	ret = mtk_dp_hdcp2x_verify_mprime(hdcp_info);
	if (ret) {
		dev_err(mtk_dp->dev, "[HDCP2.X] Failed to read rep stream ready:%d\n", ret);
		return ret;
	}

	hdcp_info->hdcp2_info.hdcp_tx.seq_num_m++;

	return 0;
}

static int mtk_dp_hdcp2x_propagate_stream_management_info(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	int i, ret, tries = 3;

	if (!hdcp_info->hdcp2_info.repeater)
		return 0;

	for (i = 0; i < tries; i++) {
		ret = _mtk_dp_hdcp2x_propagate_stream_management_info(hdcp_info);
		if (!ret)
			break;

		if (hdcp_info->hdcp2_info.hdcp_tx.seq_num_m > HDCP_2_2_SEQ_NUM_MAX) {
			dev_err(mtk_dp->dev, "[HDCP2.X] seq_num_m roll over:(%d)\n", ret);
			break;
		}

		dev_dbg(mtk_dp->dev, "[HDCP2.X] stream management %d of %d failed:(%d)\n",
			i + 1, tries, ret);
	}

	return ret;
}

int mtk_dp_hdcp2x_enable(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	int ret, i, tries = 3;
	u8 version = HDCP_NONE;

	hdcp_info->auth_status = AUTH_INIT;

	ret = tee_add_device(hdcp_info, HDCP_VERSION_2X);
	if (ret)
		goto fail2;

	for (i = 0; i < tries; i++) {
		ret = mtk_dp_hdcp2x_authenticate(hdcp_info);
		if (ret)
			continue;

		mtk_dp_hdcp2x_prepare_streams(hdcp_info);

		ret = mtk_dp_hdcp2x_propagate_stream_management_info(hdcp_info);
		if (!ret) {
			dev_dbg(mtk_dp->dev, "[HDCP2.X] Stream management done\n");
			break;
		}
	}
	if (i == tries)
		goto fail1;

	msleep(HDCP_2_2_DELAY_BEFORE_ENCRYPTION_EN);

	ret = mtk_dp_hdcp2x_set_auth_pass(hdcp_info, true);
	if (ret)
		return ret;

	if (HDCP_2_2_HDCP1_DEVICE_CONNECTED(hdcp_info->hdcp2_info.hdcp_rx.recvid_list.rx_info[1]))
		version = HDCP_V1;
	else if (HDCP_2_2_HDCP_2_0_REP_CONNECTED(hdcp_info->hdcp2_info.hdcp_rx.recvid_list.rx_info[1]))
		version = HDCP_V2;
	else
		version = HDCP_V2_3;

	ret = mtk_dp_hdcp2x_update_encrypt(hdcp_info, true, version);
	if (ret)
		return ret;

	hdcp_info->auth_version = HDCP_VERSION_2X;
	hdcp_info->auth_status = AUTH_PASS;
	dev_dbg(mtk_dp->dev, "[HDCP2.X] Authentication done\n");

	return 0;

fail1:
	tee_remove_device(hdcp_info);

fail2:
	hdcp_info->auth_status = AUTH_FAIL;
	dev_err(mtk_dp->dev, "[HDCP2.X] Authentication fail\n");

	return ret;
}

int mtk_dp_hdcp2x_disable(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	int ret;

	if (hdcp_info->auth_status == AUTH_PASS) {
		ret = mtk_dp_hdcp2x_update_encrypt(hdcp_info, false, HDCP_NONE);
		if (ret)
			return ret;

		ret = mtk_dp_hdcp2x_set_auth_pass(hdcp_info, false);
		if (ret)
			return ret;
	}

	tee_remove_device(hdcp_info);

	hdcp_info->auth_version = HDCP_NONE;
	hdcp_info->auth_status = AUTH_ZERO;
	dev_dbg(mtk_dp->dev, "[HDCP2.X] Disable Authentication\n");

	return 0;
}

int mtk_dp_hdcp2x_check_link(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	u8 rx_status;
	int ret;
	int tmp = LINK_PROTECTED;

	guard(mutex)(&mtk_dp->hdcp_mutex);

	if (mtk_dp->hdcp_info.auth_status != AUTH_PASS)
		return -EINVAL;

	if (!mtk_dp->training_info.cable_plug_in)
		goto err;

	ret = drm_dp_dpcd_read(&mtk_dp->aux, DP_HDCP_2_2_REG_RXSTATUS_OFFSET,
			       &rx_status, HDCP_2_2_DP_RXSTATUS_LEN);
	if (ret != HDCP_2_2_DP_RXSTATUS_LEN) {
		dev_dbg(mtk_dp->dev, "[HDCP2.X] Read bstatus failed, reauth\n");
		goto err;
	}
	if (HDCP_2_2_DP_RXSTATUS_REAUTH_REQ(rx_status))
		tmp = REAUTH_REQUEST;
	else if (HDCP_2_2_DP_RXSTATUS_LINK_FAILED(rx_status))
		tmp = LINK_INTEGRITY_FAILURE;
	else if (HDCP_2_2_DP_RXSTATUS_READY(rx_status))
		tmp = TOPOLOGY_CHANGE;

	if (tmp == LINK_PROTECTED) {
		return 0;
	} else if (tmp == TOPOLOGY_CHANGE) {
		ret = mtk_dp_hdcp2x_authenticate_repeater(hdcp_info);
		if (ret)
			goto err;
	} else {
		dev_err(mtk_dp->dev, "[HDCP2.X] link failed with:0x%x, retrying auth\n", tmp);
		goto err;
	}

	return 0;

err:
	ret = mtk_dp_hdcp2x_disable(hdcp_info);
	if (ret || !mtk_dp->training_info.cable_plug_in)
		return -EAGAIN;
	mtk_dp_hdcp_update_value(mtk_dp);

	ret = mtk_dp_hdcp2x_enable(hdcp_info);
	if (!ret)
		mtk_dp_hdcp_update_value(mtk_dp);

	return ret;
}
#endif

void mtk_dp_hdcp2x_get_info(struct mtk_hdcp_info *hdcp_info)
{
	struct mtk_dp *mtk_dp = container_of(hdcp_info, struct mtk_dp, hdcp_info);
	u8 tmp[3];
	ssize_t ret;

	ret = drm_dp_dpcd_read(&mtk_dp->aux,
			       DP_HDCP_2_2_REG_RX_CAPS_OFFSET, tmp, HDCP_2_2_RXCAPS_LEN);
	if (ret < 0)
		return;

	if (!HDCP_2_2_DP_HDCP_CAPABLE(tmp[2]) || tmp[0] != HDCP_2_2_RX_CAPS_VERSION_VAL) {
		hdcp_info->hdcp2_info.capable = false;
	} else {
		hdcp_info->hdcp2_info.capable = true;
		hdcp_info->hdcp2_info.repeater = HDCP_2_2_RX_REPEATER(tmp[2]);
	}

	dev_dbg(mtk_dp->dev, "[HDCP2.X] Capable: %d, Reapeater: %d\n",
		hdcp_info->hdcp2_info.capable,
		hdcp_info->hdcp2_info.repeater);
}

void mtk_dp_hdcp2x_irq(struct mtk_hdcp_info *hdcp_info)
{
	atomic_inc(&hdcp_info->hdcp2_info.cp_irq);
	wake_up_all(&hdcp_info->hdcp2_info.cp_irq_queue);
}
