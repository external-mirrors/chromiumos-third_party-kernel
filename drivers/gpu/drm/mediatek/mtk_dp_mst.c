// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024 MediaTek Inc.
 */

#include <drm/display/drm_dp_helper.h>
#include <drm/display/drm_dp_mst_helper.h>
#include <drm/display/drm_dsc.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_modes.h>
#include <drm/drm_print.h>
#include <linux/bitops.h>

#include <video/videomode.h>

#include "mtk_dp_mst.h"
#include "mtk_dp_v2.h"
#include "mtk_dp_reg_v2.h"

#define DPTX_DPCD_TRANS_BYTES_MAX	16
#define TOTAL_AVAVIL_SLOT		63

static void mtk_dp_mst_hal_enc_enable(struct mtk_dp *mtk_dp, const enum dp_encoder_id id,
				const u8 enable)
{
	u32 reg_offset_enc = DP_REG_OFFSET(id);

	/* encoder to MST Tx must set 4lane, set 2 */
	if (enable)
		WRITE_BYTE_MASK(mtk_dp, (REG_3000_DP_ENCODER0_P0 + reg_offset_enc),
				2 << LANE_NUM_DP_ENCODER0_P0_FLDMASK_POS,
				LANE_NUM_DP_ENCODER0_P0_FLDMASK);

	WRITE_BYTE_MASK(mtk_dp, (REG_3308_DP_ENCODER1_P0 + reg_offset_enc),
			((enable > 0) ? 0x01 : 0x00) << DP_MST_EN_DP_ENCODER1_P0_FLDMASK_POS,
			DP_MST_EN_DP_ENCODER1_P0_FLDMASK);

	/* mixer_sdp & enhanced_frame should be disable in MST mode*/
	WRITE_4BYTE_MASK(mtk_dp, (REG_3030_DP_ENCODER0_P0 + reg_offset_enc),
			((enable == 0) ? 0x01 : 0x00) << MIXER_SDP_EN_DP_ENCODER0_P0_FLDMASK_POS,
			MIXER_SDP_EN_DP_ENCODER0_P0_FLDMASK);
	WRITE_BYTE_MASK(mtk_dp, (REG_3000_DP_ENCODER0_P0 + reg_offset_enc),
			((enable == 0) ? 0x01 : 0x00)
			<< ENHANCED_FRAME_EN_DP_ENCODER0_P0_FLDMASK_POS,
			ENHANCED_FRAME_EN_DP_ENCODER0_P0_FLDMASK);
}

static void mtk_dp_mst_hal_mst_enable(struct mtk_dp *mtk_dp, const u8 enable)
{
	WRITE_BYTE_MASK(mtk_dp, REG_3808_DP_MST_DPTX,
			((enable > 0) ? 0x01 : 0x00) << MST_EN_TX_DP_MST_DPTX_FLDMASK_POS,
			MST_EN_TX_DP_MST_DPTX_FLDMASK);

	/* disable HDCP ECF bypass */
	WRITE_BYTE_MASK(mtk_dp, REG_3888_DP_MST_DPTX,
			((enable == 0) ? 0x01 : 0x00),
			HDCP_ECF_BYPASS_DP_MST_DPTX_FLDMASK);
}

static void mtk_dp_mst_hal_trans_enable(struct mtk_dp *mtk_dp, const bool enable)
{
	u32 port_mux = enable ? 0x04 : 0x00;
	u32 value = 0x0;

	if (mtk_dp->dp_ready && enable != 0x0)
		value = 0x1;

	/* enable need wait training lock otherwise TPS4 training fail */
	WRITE_4BYTE_MASK(mtk_dp, REG_3480_DP_TRANS_P0,
			 value << MST_EN_DP_TRANS_P0_FLDMASK_POS,
			 MST_EN_DP_TRANS_P0_FLDMASK);

	/* PRE_MISC_PORT_MUX: 4 = MST mode */
	WRITE_4BYTE_MASK(mtk_dp, REG_3400_DP_TRANS_P0,
			 (0 << PRE_MISC_LANE0_MUX_DP_TRANS_P0_FLDMASK_POS) |
			 (1 << PRE_MISC_LANE1_MUX_DP_TRANS_P0_FLDMASK_POS) |
			 (2 << PRE_MISC_LANE2_MUX_DP_TRANS_P0_FLDMASK_POS) |
			 (3 << PRE_MISC_LANE3_MUX_DP_TRANS_P0_FLDMASK_POS) |
			 (port_mux << PRE_MISC_PORT_MUX_DP_TRANS_P0_FLDMASK_POS),
			 PRE_MISC_LANE0_MUX_DP_TRANS_P0_FLDMASK |
			 PRE_MISC_LANE1_MUX_DP_TRANS_P0_FLDMASK |
			 PRE_MISC_LANE2_MUX_DP_TRANS_P0_FLDMASK |
			 PRE_MISC_LANE3_MUX_DP_TRANS_P0_FLDMASK |
			 PRE_MISC_PORT_MUX_DP_TRANS_P0_FLDMASK);
}

static void mtk_dp_mst_hal_tx_enable(struct mtk_dp *mtk_dp, const u8 enable)
{
	enum dp_encoder_id id;

	drm_dbg_kms(mtk_dp->drm_dev, "[DPTX] MST hal tx enable %d\n", enable);
	for (id = 0; id < DP_ENCODER_NUM; id++)
		mtk_dp_mst_hal_enc_enable(mtk_dp, id, enable);

	mtk_dp_mst_hal_mst_enable(mtk_dp, enable);
	mtk_dp_mst_hal_trans_enable(mtk_dp, enable);
}

static void mtk_dp_mst_hal_tx_init(struct mtk_dp *mtk_dp)
{
	u32 reg_offset_enc;
	enum dp_encoder_id id = DP_ENCODER_NUM;

	for (id = 0; id < DP_ENCODER_NUM; id++) {
		reg_offset_enc = DP_REG_OFFSET(id);

		WRITE_BYTE_MASK(mtk_dp, (REG_3308_DP_ENCODER1_P0 + reg_offset_enc),
				(1 << MST_RESET_SW_DP_ENCODER1_P0_FLDMASK_POS) |
				(1 << MST_SDP_FIFO_RST_DP_ENCODER1_P0_FLDMASK_POS),
				(MST_RESET_SW_DP_ENCODER1_P0_FLDMASK) |
				(MST_SDP_FIFO_RST_DP_ENCODER1_P0_FLDMASK));

		usleep_range(20, 21);

		WRITE_BYTE_MASK(mtk_dp, (REG_3308_DP_ENCODER1_P0 + reg_offset_enc), 0x0,
				(MST_RESET_SW_DP_ENCODER1_P0_FLDMASK) |
				(MST_SDP_FIFO_RST_DP_ENCODER1_P0_FLDMASK));
	}

	WRITE_2BYTE_MASK(mtk_dp, REG_3800_DP_MST_DPTX, BIT(15), BIT(15));
	WRITE_2BYTE_MASK(mtk_dp, REG_3894_DP_MST_DPTX,
			 (1 << RST_MST_FIFO_WPTR_DP_MST_DPTX_FLDMASK_POS) |
			 (1 << RST_MST_FIFO_RPTR_DP_MST_DPTX_FLDMASK_POS),
			 RST_MST_FIFO_WPTR_DP_MST_DPTX_FLDMASK |
			 RST_MST_FIFO_RPTR_DP_MST_DPTX_FLDMASK);
	mdelay(1);
	WRITE_2BYTE_MASK(mtk_dp, REG_3800_DP_MST_DPTX, 0x0, BIT(15));
	WRITE_2BYTE_MASK(mtk_dp, REG_3894_DP_MST_DPTX, 0x0,
			 RST_MST_FIFO_WPTR_DP_MST_DPTX_FLDMASK |
			 RST_MST_FIFO_RPTR_DP_MST_DPTX_FLDMASK);
	WRITE_2BYTE(mtk_dp, REG_3898_DP_MST_DPTX, 0x4321);

	if (id >= 4)
		dev_err(mtk_dp->dev, "[DPTX] Should add reg_mst_source_sel configurations\n");

	/* early enable mst when we know the primary branch support MST*/
	mtk_dp_mst_hal_mst_enable(mtk_dp, true);
}

static void mtk_dp_mst_hal_mst_config(struct mtk_dp *mtk_dp)
{
	/* This function is executed after link training done */
	enum dp_lane_count lane_count = mtk_dp->training_info.link_lane_count;

	if (!mtk_dp->dp_ready) {
		dev_err(mtk_dp->dev, "[DPTX] Un-expected training state %d while setting mst lane count\n",
			mtk_dp->dp_ready);
	}

	WRITE_BYTE_MASK(mtk_dp, REG_3808_DP_MST_DPTX,
			(lane_count >> 1) << LANE_NUM_TX_DP_MST_DPTX_FLDMASK_POS,
			LANE_NUM_TX_DP_MST_DPTX_FLDMASK);
	WRITE_4BYTE_MASK(mtk_dp, REG_3884_DP_MST_DPTX, 0x0,
			 HDCP_ECF_FIFO_OV_DP_MST_DPTX_FLDMASK); /* Disable HDCP bypass */
}

static void mtk_dp_mst_hal_set_mtp_size(struct mtk_dp *mtk_dp, const enum dp_encoder_id id,
				const u8 num_slots)
{
	u16 num_slot_enc;
	u32 reg_offset_enc = DP_REG_OFFSET(id);

	switch (mtk_dp->training_info.link_lane_count) {
	case DP_4LANE:
		num_slot_enc = num_slots;
		break;
	case DP_2LANE:
		num_slot_enc = num_slots >> 1;
		break;
	case DP_1LANE:
		num_slot_enc = num_slots >> 2;
		break;
	default:
		num_slot_enc = num_slots;
		dev_err(mtk_dp->dev, "[DPTX] Unknown lane count %d\n",
			mtk_dp->training_info.link_lane_count);
		break;
	}

	/* update slots number */
	WRITE_2BYTE_MASK(mtk_dp, (REG_3310_DP_ENCODER1_P0 + reg_offset_enc),
			 num_slot_enc << MST_TIME_SLOT_DP_ENCODER1_P0_FLDMASK_POS,
			 MST_TIME_SLOT_DP_ENCODER1_P0_FLDMASK);
}

static void mtk_dp_mst_hal_set_timeslot(struct mtk_dp *mtk_dp, const enum dp_encoder_id id,
				const u16 start_slot, const u16 end_slot, const u32 vcpi)
{
	u32 slot, addr;

	if (vcpi > DP_ENCODER_NUM)
		dev_err(mtk_dp->dev, "[DPTX] Un-expected vcpi%d", vcpi);

	for (slot = start_slot; slot < end_slot; slot++) {
		addr = REG_38A8_DP_MST_DPTX + ((slot >> 1) << 2);
		if (slot % 2)	/* odd slots */
			WRITE_2BYTE_MASK(mtk_dp, addr,
					 vcpi << VC_PAYLOAD_TIMESLOT_1_DP_MST_DPTX_FLDMASK_POS,
					 VC_PAYLOAD_TIMESLOT_1_DP_MST_DPTX_FLDMASK);
		else		/* even slots */
			WRITE_2BYTE_MASK(mtk_dp, addr,
					 vcpi << VC_PAYLOAD_TIMESLOT_0_DP_MST_DPTX_FLDMASK_POS,
					 VC_PAYLOAD_TIMESLOT_0_DP_MST_DPTX_FLDMASK);
	}
}

static void mtk_dp_mst_hal_set_id_buf(struct mtk_dp *mtk_dp, const enum dp_encoder_id id,
				const u32 vcpi)
{
	/* overwrite ID buffer define */
	if (id % 2)	/* encoder 1/3/... */
		WRITE_2BYTE_MASK(mtk_dp, (REG_3854_DP_MST_DPTX + ((id >> 1) << 2)),
				 vcpi << ID_BUF_2_OV_DP_MST_DPTX_FLDMASK_POS,
				 ID_BUF_2_OV_DP_MST_DPTX_FLDMASK);
	else			/* encoder 0/2... */
		WRITE_2BYTE_MASK(mtk_dp, (REG_3854_DP_MST_DPTX + ((id >> 1) << 2)),
				 vcpi << ID_BUF_1_OV_DP_MST_DPTX_FLDMASK_POS,
				 ID_BUF_1_OV_DP_MST_DPTX_FLDMASK);
}

static void mtk_dp_mst_hal_clear_id_buf(struct mtk_dp *mtk_dp)
{
	WRITE_2BYTE(mtk_dp, REG_3854_DP_MST_DPTX, 0x0);
	WRITE_2BYTE(mtk_dp, REG_3858_DP_MST_DPTX, 0x0);
	WRITE_2BYTE(mtk_dp, REG_385C_DP_MST_DPTX, 0x0);
	WRITE_2BYTE(mtk_dp, REG_3860_DP_MST_DPTX, 0x0);
}

static void mtk_dp_mst_hal_reset_payload(struct mtk_dp *mtk_dp)
{
	u32 id, i;
	u32 reg_offset_enc;

	mtk_dp_mst_hal_clear_id_buf(mtk_dp);

	for (id = 0; id < DP_ENCODER_NUM; id++) {
		reg_offset_enc = DP_REG_OFFSET(id);

		/* update slots number */
		WRITE_2BYTE_MASK(mtk_dp, (REG_3310_DP_ENCODER1_P0 + reg_offset_enc),
				 0x00 << MST_TIME_SLOT_DP_ENCODER1_P0_FLDMASK_POS,
				 MST_TIME_SLOT_DP_ENCODER1_P0_FLDMASK);
	}

	/* update corresponding vcpi */
	for (i = 0; i < 32; i++)
		WRITE_2BYTE_MASK(mtk_dp, (REG_38A8_DP_MST_DPTX + (i << 2)), 0x00,
				 VC_PAYLOAD_TIMESLOT_0_DP_MST_DPTX_FLDMASK |
				 VC_PAYLOAD_TIMESLOT_1_DP_MST_DPTX_FLDMASK);

	for (i = 0; i < 4; i++)
		WRITE_2BYTE_MASK(mtk_dp, REG_3984_DP_MST_DPTX  + (i << 2),
				 0x0, HDCP_TIMESLOT_MST_TX_0_DP_MST_DPTX_FLDMASK);
}

static void mtk_dp_mst_hal_vcp_table_update(struct mtk_dp *mtk_dp)
{
	/* update TX ID buffer */
	WRITE_2BYTE_MASK(mtk_dp, REG_3868_DP_MST_DPTX,
			 1 << UPDATE_ID_BUF_TX_TRIG_DP_MST_DPTX_FLDMASK_POS,
			 UPDATE_ID_BUF_TX_TRIG_DP_MST_DPTX_FLDMASK);

	drm_dbg_kms(mtk_dp->drm_dev, "[DPTX] Update VC payload id table\n");

	/* trigger to update VC payload table */
	WRITE_BYTE_MASK(mtk_dp, REG_3980_DP_MST_DPTX,
			1 << VC_PAYLOAD_TABLE_TX_UPDATE_DP_MST_DPTX_FLDMASK_POS,
			VC_PAYLOAD_TABLE_TX_UPDATE_DP_MST_DPTX_FLDMASK);
}

static void mtk_dp_mst_hal_stream_enable(struct mtk_dp *mtk_dp, int encoder_id,
				const u8 payload_mask, const u32 max_payloads)
{
	u32 reg_offset_enc;

	reg_offset_enc = DP_REG_OFFSET(encoder_id);

	if ((payload_mask >> encoder_id) & 0x1) {
		/*reg_dp_mst_en*/
		WRITE_BYTE_MASK(mtk_dp, (REG_3308_DP_ENCODER1_P0 + reg_offset_enc),
				1 << DP_MST_EN_DP_ENCODER1_P0_FLDMASK_POS,
				DP_MST_EN_DP_ENCODER1_P0_FLDMASK);

		WRITE_BYTE_MASK(mtk_dp, REG_3930_DP_MST_DPTX,
				0x1 << encoder_id, BIT(encoder_id));
	}

	drm_dbg_kms(mtk_dp->drm_dev,
		    "[DPTX] MST enable, payload_mask:0x%x, max_payloads:0x%x, REG(0x%x, 0x%x)\n",
		    payload_mask, max_payloads,
		    READ_BYTE(mtk_dp, REG_3308_DP_ENCODER1_P0 + reg_offset_enc),
		    READ_BYTE(mtk_dp, REG_3930_DP_MST_DPTX));
}

static void mtk_dp_mst_hal_trigger_act(struct mtk_dp *mtk_dp)
{
	if (mtk_dp->training_info.link_rate < DP_LINK_RATE_UHBR10) {
		/* trigger VCPF */
		WRITE_BYTE_MASK(mtk_dp, REG_3894_DP_MST_DPTX,
				(0x1 << RST_MST_FIFO_WPTR_DP_MST_DPTX_FLDMASK_POS) |
				(0x1 << RST_MST_FIFO_RPTR_DP_MST_DPTX_FLDMASK_POS),
				RST_MST_FIFO_WPTR_DP_MST_DPTX_FLDMASK |
				RST_MST_FIFO_RPTR_DP_MST_DPTX_FLDMASK);

		/* write one trigger */
		WRITE_BYTE_MASK(mtk_dp, REG_3980_DP_MST_DPTX,
				0x1 << ACT_TRIGGER_MST_TX_DP_MST_DPTX_FLDMASK_POS,
				ACT_TRIGGER_MST_TX_DP_MST_DPTX_FLDMASK);

		usleep_range(10, 11);

		/* trigger VCPF */
		WRITE_BYTE_MASK(mtk_dp, REG_3894_DP_MST_DPTX, 0,
				RST_MST_FIFO_WPTR_DP_MST_DPTX_FLDMASK |
				RST_MST_FIFO_RPTR_DP_MST_DPTX_FLDMASK);
	}
}

static void mtk_dp_mst_hal_hdcp_enable(struct mtk_dp *mtk_dp, const bool enable)
{
	WRITE_BYTE_MASK(mtk_dp, REG_3980_DP_MST_DPTX,
			((enable > 0) ? 0x01 : 0x00)
			<< ENCRYPTION_EN_MST_TX_DP_MST_DPTX_FLDMASK_POS,
			ENCRYPTION_EN_MST_TX_DP_MST_DPTX_FLDMASK);
}

static void mtk_dp_mst_hal_hdcp_set_timeslot(
				struct mtk_dp *mtk_dp, const u16 start_slot, const u16 end_slot)
{
	u32 hdcp_bitmap_upper = 0x0;
	u32 hdcp_bitmap_lower = 0x0;

	hdcp_bitmap_lower = GENMASK_ULL(end_slot, start_slot) & GENMASK(31, 0);
	hdcp_bitmap_upper = (GENMASK_ULL(end_slot, start_slot) & GENMASK_ULL(63, 32)) >> 32;

	WRITE_2BYTE_MASK(mtk_dp, REG_3984_DP_MST_DPTX, hdcp_bitmap_lower,
			 hdcp_bitmap_lower & HDCP_TIMESLOT_MST_TX_0_DP_MST_DPTX_FLDMASK);
	WRITE_2BYTE_MASK(mtk_dp, REG_3988_DP_MST_DPTX, hdcp_bitmap_lower >> 16,
			 (hdcp_bitmap_lower >> 16) & HDCP_TIMESLOT_MST_TX_1_DP_MST_DPTX_FLDMASK);
	WRITE_2BYTE_MASK(mtk_dp, REG_398C_DP_MST_DPTX, hdcp_bitmap_upper,
			 hdcp_bitmap_upper & HDCP_TIMESLOT_MST_TX_2_DP_MST_DPTX_FLDMASK);
	WRITE_2BYTE_MASK(mtk_dp, REG_3990_DP_MST_DPTX, hdcp_bitmap_upper >> 16,
			 (hdcp_bitmap_upper >> 16) & HDCP_TIMESLOT_MST_TX_3_DP_MST_DPTX_FLDMASK);

	/*reg_trig_hdcp_timeslot, WO*/
	WRITE_BYTE_MASK(mtk_dp, REG_3984_DP_MST_DPTX, 0, BIT(0));
	WRITE_2BYTE_MASK(mtk_dp, REG_3980_DP_MST_DPTX,
			 1 << TRIG_HDCP_TIMESLOT_MST_TX_DP_MST_DPTX_FLDMASK_POS,
			 TRIG_HDCP_TIMESLOT_MST_TX_DP_MST_DPTX_FLDMASK);
}

static void mtk_dp_mst_hal_hdcp_trigger_act(struct mtk_dp *mtk_dp)
{
	if (mtk_dp->training_info.link_rate < DP_LINK_RATE_UHBR10) {
		WRITE_BYTE_MASK(mtk_dp, REG_3980_DP_MST_DPTX,
				0x1 << TRIG_HDCP_TIMESLOT_MST_TX_DP_MST_DPTX_FLDMASK_POS,
				TRIG_HDCP_TIMESLOT_MST_TX_DP_MST_DPTX_FLDMASK);
	}
}

static void mtk_dp_mst_drv_video_mute_all(struct mtk_dp *mtk_dp)
{
	enum dp_encoder_id id;

	for (id = 0; id < DP_ENCODER_NUM; id++)
		mtk_dp_video_mute_v2(mtk_dp, id, true);
}

static void mtk_dp_mst_drv_audio_mute_all(struct mtk_dp *mtk_dp)
{
	enum dp_encoder_id id;

	for (id = 0; id < DP_ENCODER_NUM; id++)
		mtk_dp_audio_mute_v2(mtk_dp, id, true);
}

static void mtk_dp_mst_drv_stream_enable(struct mtk_dp *mtk_dp, int encoder_id)
{
	struct drm_dp_mst_topology_state *mst_state;
	int ch, fs, len;

	if (encoder_id < 0 || encoder_id >= DP_ENCODER_NUM) {
		dev_err(mtk_dp->dev, "[DPTX] fail, encoder id error\n");
		return;
	}

	ch = mtk_dp->info[encoder_id].audio_cur_cfg.channels;
	fs = mtk_dp->info[encoder_id].audio_cur_cfg.sample_rate;
	len = mtk_dp->info[encoder_id].audio_cur_cfg.word_length_bits;

	mst_state = to_drm_dp_mst_topology_state(mtk_dp->mgr.base.state);
	if (IS_ERR(mst_state)) {
		dev_err(mtk_dp->dev, "[DPTX] fail to get mst topology state!\n");
		return;
	}

	mtk_dp_mst_hal_stream_enable(mtk_dp, encoder_id,
				     mst_state->payload_mask, mtk_dp->mgr.max_payloads);

	mtk_dp_video_mute_v2(mtk_dp, encoder_id, true);
	mtk_dp_video_enable_v2(mtk_dp, encoder_id);
	mtk_dp_video_mute_v2(mtk_dp, encoder_id, false);

	drm_dbg_kms(mtk_dp->drm_dev, "[DPTX] [%d] ch:%d, fs:%d, len:%d\n", encoder_id, ch, fs, len);

	mtk_dp->audio_enable = mtk_dp_parse_audio_cap_v2(mtk_dp,
		&mtk_dp->info[encoder_id].audio_cur_cfg);

	if (mtk_dp->audio_enable) {
		mtk_dp_audio_mute_v2(mtk_dp, encoder_id, true);
		mtk_dp_audio_pg_enable_v2(mtk_dp, encoder_id, ch, fs, false);
		mtk_dp_audio_config_v2(mtk_dp, encoder_id);
		mtk_dp_audio_mute_v2(mtk_dp, encoder_id, false);

	} else {
		mtk_dp_audio_mute_v2(mtk_dp, encoder_id, true);
	}
}

static bool mtk_dp_mst_drv_first_stream_enable(struct mtk_dp *mtk_dp)
{
	int video_enable_count = 0;
	u8 i;

	for (i = 0; i < ARRAY_SIZE(mtk_dp->mtk_con); i++)
		if (mtk_dp->mtk_con[i] && mtk_dp->mtk_con[i]->video_enable)
			video_enable_count++;

	return video_enable_count == 0;
}

static void mtk_dp_mst_drv_update_vcp_table(struct mtk_dp *mtk_dp, int encoder_id)
{
	int con_id = 0;
	u16 start_slot, end_slot;
	struct drm_dp_mst_atomic_payload *payload;
	struct drm_dp_mst_topology_state *mst_state;

	mst_state = to_drm_dp_mst_topology_state(mtk_dp->mgr.base.state);
	if (IS_ERR_OR_NULL(mst_state)) {
		dev_err(mtk_dp->dev, "[DPTX] fail to get mst topology state!\n");
		return;
	}

	if (mtk_dp_mst_drv_first_stream_enable(mtk_dp))
		mtk_dp_mst_hal_reset_payload(mtk_dp);

	if ((mst_state->payload_mask >> encoder_id) & 0x1) {
		con_id = encoder_id_to_con_id(mtk_dp, encoder_id, DRM_DP_MST);
		if (con_id < 0)
			return;

		drm_dbg_kms(mtk_dp->drm_dev, "[DPTX] %d\n", con_id);

		payload = drm_atomic_get_mst_payload_state(mst_state,
					mtk_dp->mtk_con[con_id]->port);
		if (IS_ERR_OR_NULL(payload)) {
			dev_err(mtk_dp->dev, "[DPTX] fail to get mst payload state!\n");
			return;
		}

		start_slot = payload->vc_start_slot;
		end_slot = start_slot + payload->time_slots;

		mtk_dp_mst_hal_set_mtp_size(mtk_dp, encoder_id, payload->time_slots);

		drm_dbg_kms(mtk_dp->drm_dev,
			    "[DPTX] Start allocate VCPI %d, start slot %d, end slot %d\n",
			    payload->vcpi, start_slot, end_slot - 1);
		/* reg_vc_payload_timeslot */
		if (payload->vcpi < 1 || payload->vcpi > DP_ENCODER_NUM) {
			dev_err(mtk_dp->dev, "[DPTX] Invalid VCPI, vcpi %d\n", payload->vcpi);
		} else if ((start_slot > 64) || (end_slot > 64)) {
			dev_err(mtk_dp->dev, "[DPTX] Invalid slot region, start_slot %d, end_slot %d\n",
				start_slot, end_slot);
		} else {
			mtk_dp_mst_hal_set_timeslot(mtk_dp, encoder_id,
						    start_slot, end_slot, payload->vcpi);
			mtk_dp_mst_hal_set_id_buf(mtk_dp, encoder_id, payload->vcpi);
		}
	}

	mtk_dp_mst_hal_vcp_table_update(mtk_dp);
}

static int mtk_dp_mst_drv_get_fec_status(struct mtk_dp *mtk_dp)
{
	int ret;
	u8 status;

	ret = drm_dp_dpcd_readb(&mtk_dp->aux, DP_FEC_STATUS, &status);
	if (ret < 0)
		return ret;

	return status;
}

static void mtk_dp_mst_drv_wait_fec_detected(struct mtk_dp *mtk_dp, bool enabled)
{
	int mask = enabled ? DP_FEC_DECODE_EN_DETECTED : DP_FEC_DECODE_DIS_DETECTED;
	int status;
	int err;

	if (!drm_dp_sink_supports_fec(mtk_dp->mtk_con[DP_FIRST_CON]->fec_cap))
		return;

	mtk_dp_fec_enable_v2(mtk_dp);

	err = readx_poll_timeout(mtk_dp_mst_drv_get_fec_status, mtk_dp, status,
				 status & mask || status < 0,
				 10000, 200000);

	if (!err && status >= 0)
		return;

	if (err == -ETIMEDOUT)
		dev_err(mtk_dp->dev, "[DPTX] Timeout detected, FEC:%s\n",
			str_enabled_disabled(enabled));
	else
		dev_err(mtk_dp->dev, "[DPTX] fail to read FEC detected status:%d\n", status);
}

static void mtk_dp_mst_drv_update_payload(struct mtk_dp *mtk_dp, struct mtk_dp_con *mtk_con)
{
	struct drm_dp_mst_topology_state *mst_state;
	bool first_stream = mtk_dp_mst_drv_first_stream_enable(mtk_dp);
	int i, ret;
	struct drm_dp_mst_atomic_payload *payload;

	mst_state = to_drm_dp_mst_topology_state(mtk_dp->mgr.base.state);
	if (IS_ERR_OR_NULL(mst_state)) {
		dev_err(mtk_dp->dev, "[DPTX] fail to get mst topology state!\n");
		return;
	}

	payload = drm_atomic_get_mst_payload_state(mst_state, mtk_con->port);
	if (IS_ERR_OR_NULL(payload)) {
		dev_err(mtk_dp->dev, "[DPTX] fail to get mst payload state!\n");
		return;
	}

	drm_dbg_kms(mtk_dp->drm_dev, "[DPTX] first_stream:%d\n", first_stream);

	if (first_stream) {
		if (mtk_dp->rx_cap[DP_DPCD_REV] >= 0x11) {
			for (i = 0; i < 3; i++) {
				ret = drm_dp_dpcd_writeb(&mtk_dp->aux,
						DP_SET_POWER, DP_SET_POWER_D0);
				if (ret == 1)
					break;
				usleep_range(1000, 2000);
			}

			if (ret != 1)
				dev_err(mtk_dp->dev, "[DPTX] Set power to DP_SET_POWER_D0 failed\n");
		}
	}

	drm_dp_send_power_updown_phy(&mtk_dp->mgr, mtk_con->port, true);

	if (first_stream)
		drm_dp_mst_topology_queue_probe(&mtk_dp->mgr);

	ret = drm_dp_add_payload_part1(&mtk_dp->mgr, mst_state, payload);
	if (ret < 0) {
		dev_err(mtk_dp->dev, "[DPTX] fail to add payload part1!");
		return;
	}

	mtk_dp_mst_hal_trigger_act(mtk_dp);
	ret = drm_dp_check_act_status(&mtk_dp->mgr);
	if (ret != 0)
		dev_err(mtk_dp->dev, "[DPTX] fail to check act status!");

	if (first_stream)
		mtk_dp_mst_drv_wait_fec_detected(mtk_dp, true);

	ret = drm_dp_add_payload_part2(&mtk_dp->mgr,
			drm_atomic_get_mst_payload_state(mst_state, mtk_con->port));
	if (ret < 0) {
		dev_err(mtk_dp->dev, "[DPTX] fail to add payload part2!");
		return;
	}
}

static int mtk_dp_mst_drv_slots(int pbn_div, int pbn)
{
	int num_slots;

	num_slots = (pbn / pbn_div) + 1;

	return num_slots;
}

int mtk_dp_mst_drv_find_vcpi_slots(struct mtk_dp *mtk_dp, int pbn_div, const int pbn_allocating)
{
	int slots = 0;

	slots = mtk_dp_mst_drv_slots(pbn_div, pbn_allocating);
	if (slots > TOTAL_AVAVIL_SLOT) {
		drm_dbg_kms(mtk_dp->drm_dev, "[DPTX] Un-expected slots %d\n", slots);
		return 0;
	}

	drm_dbg_kms(mtk_dp->drm_dev, "[DPTX] Slots before fine-tune %d, %d\n",
		    slots, pbn_allocating);

	switch (mtk_dp->training_info.link_lane_count) {
	case DP_1LANE:
		slots += (4 - (slots % 4));
		break;
	case DP_2LANE:
		slots += (2 - (slots % 2));
		break;
	case DP_4LANE:
		slots++;
		break;
	}

	if (slots > TOTAL_AVAVIL_SLOT) {
		drm_dbg_kms(mtk_dp->drm_dev, "[DPTX] abnormal slots:%d\n", slots);
		return 0;
	}

	drm_dbg_kms(mtk_dp->drm_dev, "[DPTX] Slots after fine-tune %d\n", slots);
	return slots;
}

int mtk_dp_mst_drv_calculate_pbn(struct mtk_dp *mtk_dp,
		struct drm_display_mode *mode, u8 bpp, bool dsc)
{
	u32 pixel_clock;
	u32 need_pbn;

	if (dsc)
		pixel_clock = mtk_dp_dsc_cal_clock_v2(mode);
	else
		pixel_clock = mode->clock;

	need_pbn = drm_dp_calc_pbn_mode(pixel_clock, bpp << 4);

	drm_dbg_kms(mtk_dp->drm_dev, "[DPTX] bpp %d, pixel_clock %d, need_pbn:%d, dsc:%d\n",
		    bpp, pixel_clock, need_pbn, dsc);

	return need_pbn;
}

static void mtk_dp_mst_drv_configure_stream(struct mtk_dp *mtk_dp, int encoder_id)
{
	mtk_dp_mst_drv_stream_enable(mtk_dp, encoder_id);

	mtk_dp_mst_drv_update_vcp_table(mtk_dp, encoder_id);
}

static void mtk_dp_mst_drv_start(struct mtk_dp *mtk_dp, struct mtk_dp_con *mtk_con)
{
	/* update payload, after update vcpi */
	mtk_dp_mst_drv_update_payload(mtk_dp, mtk_con);

	/* configure vcpi for DPTX HW*/
	mtk_dp_mst_drv_configure_stream(mtk_dp, mtk_con->encoder_id);

	mtk_con->video_enable = true;
}

static void mtk_dp_mst_drv_stop(struct mtk_dp *mtk_dp,
			 struct drm_atomic_state *state, struct mtk_dp_con *mtk_con)
{
	struct drm_dp_mst_atomic_payload *payload;
	struct drm_dp_mst_topology_state *mst_state;
	struct drm_dp_mst_topology_state *old_mst_state;
	struct drm_dp_mst_topology_state *new_mst_state;
	const struct drm_dp_mst_atomic_payload *old_payload;
	struct drm_dp_mst_atomic_payload *new_payload;
	int ret;

	drm_dbg_kms(mtk_dp->drm_dev, "[DPTX] MST stop\n");

	mst_state = to_drm_dp_mst_topology_state(mtk_dp->mgr.base.state);
	if (IS_ERR_OR_NULL(mst_state)) {
		dev_err(mtk_dp->dev, "[DPTX] fail to get mst topology state!\n");
		goto end;
	}

	payload = drm_atomic_get_mst_payload_state(mst_state, mtk_con->port);
	if (IS_ERR_OR_NULL(payload)) {
		dev_err(mtk_dp->dev, "[DPTX] fail to get mst payload state!\n");
		goto end;
	}

	drm_dp_remove_payload_part1(&mtk_dp->mgr, mst_state, payload);

	new_mst_state = drm_atomic_get_new_mst_topology_state(state, &mtk_dp->mgr);
	if (!new_mst_state) {
		dev_err(mtk_dp->dev, "[DPTX] fail to get new mst state!1\n");
		goto end;
	}

	old_mst_state = drm_atomic_get_old_mst_topology_state(state, &mtk_dp->mgr);
	if (!old_mst_state) {
		dev_err(mtk_dp->dev, "[DPTX] fail to get old mst state!1\n");
		goto end;
	}

	old_payload = drm_atomic_get_mst_payload_state(old_mst_state, mtk_con->port);
	new_payload = drm_atomic_get_mst_payload_state(new_mst_state, mtk_con->port);
	if (!old_payload || !new_payload) {
		dev_err(mtk_dp->dev, "[DPTX] fail to get old/new payload!\n");
		goto end;
	}

	mtk_dp_mst_hal_trigger_act(mtk_dp);
	ret = drm_dp_check_act_status(&mtk_dp->mgr);
	if (ret != 0)
		dev_err(mtk_dp->dev, "[DPTX] fail to check act status!");

	drm_dp_remove_payload_part2(&mtk_dp->mgr, new_mst_state, old_payload, new_payload);

end:
	drm_dp_send_power_updown_phy(&mtk_dp->mgr, mtk_con->port, false);

	mtk_dp_video_disable_v2(mtk_dp, mtk_con->encoder_id);

	mtk_con->video_enable = false;
}

void mtk_dp_mst_drv_prepare(struct mtk_dp *mtk_dp)
{
	struct drm_dp_mst_topology_state *mst_state;
	enum dp_encoder_id id;
	int lane_count;
	int link_rate;
	u8 link_coding;

	guard(mutex)(&mtk_dp->mst_mutex);

	if (mtk_dp->mst_start)
		return;

	drm_dbg_kms(mtk_dp->drm_dev, "[DPTX] MST prepare\n");

	for (id = 0; id < DP_ENCODER_NUM; id++) {
		mtk_dp->info[id].format = DP_PIXELFORMAT_RGB;
		mtk_dp->info[id].depth = DP_COLOR_DEPTH_8BIT;
	}

	mtk_dp_mst_drv_video_mute_all(mtk_dp);
	mtk_dp_mst_drv_audio_mute_all(mtk_dp);

	mtk_dp_mst_hal_tx_init(mtk_dp);

	drm_dp_mst_topology_mgr_set_mst(&mtk_dp->mgr, true);

	mst_state = to_drm_dp_mst_topology_state(mtk_dp->mgr.base.state);
	if (IS_ERR(mst_state)) {
		dev_err(mtk_dp->dev, "[DPTX] fail to get mst topology state!\n");
		return;
	}

	lane_count = mtk_dp->training_info.link_lane_count;
	link_rate = mtk_dp->training_info.link_rate * 27000;

	mst_state->pbn_div = drm_dp_get_vc_payload_bw(&mtk_dp->mgr, link_rate, lane_count);
	drm_dbg_kms(mtk_dp->drm_dev, "[DPTX] count:%d, rate:%d, pbn_div:%d %d\n",
		    lane_count, link_rate,
		    mst_state->pbn_div.full, dfixed_trunc(mst_state->pbn_div));

	link_coding = mtk_dp->training_info.link_rate >= DP_LINK_RATE_UHBR10 ?
		DP_CAP_ANSI_128B132B : DP_CAP_ANSI_8B10B;
	drm_dp_mst_update_slots(mst_state, link_coding);

	mtk_dp_mst_hal_tx_enable(mtk_dp, true);
	mtk_dp_mst_hal_mst_config(mtk_dp);

	mtk_dp->mst_start = true;
}

void mtk_dp_mst_drv_unprepare(struct mtk_dp *mtk_dp)
{
	guard(mutex)(&mtk_dp->mst_mutex);

	if (!mtk_dp->mst_start)
		return;

	drm_dbg_kms(mtk_dp->drm_dev, "[DPTX] MST unprepare\n");

	mtk_dp_mst_drv_video_mute_all(mtk_dp);
	mtk_dp_mst_drv_audio_mute_all(mtk_dp);

	mtk_dp_mst_hal_tx_enable(mtk_dp, false);
	mtk_dp_mst_hal_hdcp_enable(mtk_dp, false);

	drm_dp_mst_topology_mgr_set_mst(&mtk_dp->mgr, false);

	mtk_dp->mst_start = false;
}

static u8 mtk_dp_mst_drv_get_sink_count(struct mtk_dp *mtk_dp)
{
	u8 sink_count = 0;
	u8 i;

	if (!mtk_dp->mst_enable)
		return 0;

	for (i = 0; i < ARRAY_SIZE(mtk_dp->mtk_con); i++) {
		if (mtk_dp->mtk_con[i] && mtk_dp->mtk_con[i]->dp_mode == DRM_DP_MST &&
			mtk_dp->mtk_con[i]->connector.status == connector_status_connected)
			sink_count++;
	}

	return sink_count;
}

static enum drm_mode_status mtk_dp_mst_drv_check_mode(struct mtk_dp *mtk_dp,
								struct mtk_dp_con *mtk_con,
								struct drm_display_mode *mode,
								int bpp, bool *dsc)
{
	struct drm_dp_mst_topology_state *mst_state;
	int slots = 0;
	int need_pbn = 0;
	int valid_pbn = 0;
	enum drm_mode_status ret = MODE_CLOCK_HIGH;
	u8 sink_count;

	if ((mtk_dp->data->max_hdisplay != 0 && mode->hdisplay > mtk_dp->data->max_hdisplay) ||
	    (mtk_dp->data->max_vdisplay != 0 && mode->vdisplay > mtk_dp->data->max_vdisplay) ||
	    (mode->htotal - mode->hdisplay < mtk_dp->data->min_hblanking))
		return MODE_BAD;

	/* This is for temporarily removing the timing. */
	if (mode->hdisplay < mtk_dp->data->min_hdisplay ||
	    mode->vdisplay < mtk_dp->data->min_vdisplay)
		return MODE_BAD;

	if(!mtk_dp_timing_alignment_valid(mtk_dp, mode))
		return MODE_BAD;

	mst_state = to_drm_dp_mst_topology_state(mtk_dp->mgr.base.state);
	if (IS_ERR(mst_state)) {
		dev_err(mtk_dp->dev, "[DPTX] fail to get mst topology state!\n");
		return ret;
	}

	*dsc = false;

	sink_count = mtk_dp_mst_drv_get_sink_count(mtk_dp);
	if (!sink_count) {
		dev_err(mtk_dp->dev, "fail to get mst sink count!\n");
		return ret;
	}

	valid_pbn = dfixed_trunc(mst_state->pbn_div) * TOTAL_AVAVIL_SLOT / sink_count;

	need_pbn = mtk_dp_mst_drv_calculate_pbn(mtk_dp, mode, bpp, false);
	slots = mtk_dp_mst_drv_find_vcpi_slots(mtk_dp, dfixed_trunc(mst_state->pbn_div), need_pbn);
	if (slots && (slots * dfixed_trunc(mst_state->pbn_div)) < valid_pbn &&
		slots * dfixed_trunc(mst_state->pbn_div) < mtk_con->port->full_pbn) {
		*dsc = false;
		ret = MODE_OK;
		goto end;
	}

	if (mtk_dp->data->dsc_support &&
		drm_dp_sink_supports_fec(mtk_dp->mtk_con[DP_FIRST_CON]->fec_cap) &&
		drm_dp_sink_supports_fec(mtk_con->fec_cap) &&
		drm_dp_sink_supports_dsc(mtk_con->dsc_dpcd)
		) {
		/* DSC only support RGB 8bit now */
		need_pbn = mtk_dp_mst_drv_calculate_pbn(mtk_dp, mode,
			mtk_dp_color_get_bpp_v2(DP_PIXELFORMAT_RGB, DP_COLOR_DEPTH_8BIT), true);
		slots = mtk_dp_mst_drv_find_vcpi_slots(mtk_dp,
					dfixed_trunc(mst_state->pbn_div), need_pbn);
		if (slots && (slots * dfixed_trunc(mst_state->pbn_div)) < valid_pbn &&
			slots * dfixed_trunc(mst_state->pbn_div) < mtk_con->port->full_pbn) {
			*dsc = true;
			ret = MODE_OK;
		}
	}

end:
	drm_dbg_kms(mtk_dp->drm_dev,
		    "[DPTX] pbn_div:%d, slots:%d, valid_pbn:%d, need_pbn:%d, dsc:%d, "
		    "port_full:%d, ret:%d\n",
		    dfixed_trunc(mst_state->pbn_div), slots, valid_pbn, need_pbn, *dsc,
		    mtk_con->port->full_pbn, ret);

	return ret;
}

static bool mtk_dp_mst_drv_detect_dsc_hblank_expansion_quirk(const struct mtk_dp_con *mtk_con)
{
	struct drm_dp_aux *aux = mtk_con->dsc_aux;
	struct drm_dp_desc desc;
	u8 dpcd[DP_RECEIVER_CAP_SIZE];

	memset(dpcd, 0, sizeof(dpcd));

	if (!aux)
		return false;

	if (drm_dp_mst_port_is_logical(mtk_con->port)) {
		aux = drm_dp_mst_aux_for_parent(mtk_con->port);
		if (!aux)
			aux = &mtk_con->mtk_dp->aux;
	}

	if (drm_dp_read_dpcd_caps(aux, dpcd) < 0)
		return false;

	if (drm_dp_read_desc(aux, &desc, drm_dp_is_branch(dpcd)) < 0)
		return false;

	if (!drm_dp_has_quirk(&desc, DP_DPCD_QUIRK_HBLANK_EXPANSION_REQUIRES_DSC))
		return false;

	if (!drm_dp_128b132b_supported(dpcd) &&
		!(dpcd[DP_RECEIVE_PORT_0_CAP_0] & DP_HBLANK_EXPANSION_CAPABLE))
		return false;

	drm_dbg_kms(mtk_con->mtk_dp->drm_dev, "[DPTX] DSC HBLANK expansion quirk detected\n");

	return true;
}

static void mtk_dp_mst_drv_read_port_dsc_caps(struct mtk_dp *mtk_dp, struct mtk_dp_con *connector)
{
	u8 dpcd_caps[DP_RECEIVER_CAP_SIZE];
	memset(dpcd_caps, 0, sizeof(dpcd_caps));

	if (!connector->dsc_aux)
		return;

	if (drm_dp_read_dpcd_caps(connector->dsc_aux, dpcd_caps) < 0)
		return;

	if (dpcd_caps[DP_DPCD_REV] < DP_DPCD_REV_14)
		return;

	memset(connector->dsc_dpcd, 0, sizeof(connector->dsc_dpcd));
	connector->fec_cap = 0;

	if (drm_dp_dpcd_read(connector->dsc_aux, DP_DSC_SUPPORT, connector->dsc_dpcd,
		DP_DSC_RECEIVER_CAP_SIZE) < 0) {
		dev_err(mtk_dp->dev, "[DPTX] fail to read DSC DPCD 0x%x\n",
			DP_DSC_SUPPORT);
		return;
	}
	drm_dbg_kms(mtk_dp->drm_dev, "[DPTX] DSC DPCD: %*ph\n",
		    DP_DSC_RECEIVER_CAP_SIZE, connector->dsc_dpcd);

	if (drm_dp_dpcd_readb(connector->dsc_aux, DP_FEC_CAPABILITY,
		&connector->fec_cap) < 0) {
		dev_err(mtk_dp->dev, "[DPTX] fail to read FEC DPCD\n");
		return;
	}
	drm_dbg_kms(mtk_dp->drm_dev, "[DPTX] FEC cap:0x%x\n", connector->fec_cap);
}

void mtk_dp_mst_drv_set_hdcp_setting(struct mtk_dp *mtk_dp)
{
	u8 i;
	int encoder_id;
	u16 start_slot, end_slot;
	struct drm_dp_mst_atomic_payload *payload;
	struct drm_dp_mst_topology_state *mst_state;

	mtk_dp_mst_hal_hdcp_enable(mtk_dp, true);

	mst_state = to_drm_dp_mst_topology_state(mtk_dp->mgr.base.state);
	if (IS_ERR_OR_NULL(mst_state)) {
		dev_err(mtk_dp->dev, "[DPTX] fail to get mst topology state!\n");
		return;
	}

	for (i = 0; i < ARRAY_SIZE(mtk_dp->mtk_con); i++) {
		if (!mst_con_with_encoder(mtk_dp->mtk_con[i]))
			continue;

		encoder_id = mtk_dp->mtk_con[i]->encoder_id;
		if ((mst_state->payload_mask >> encoder_id) & 0x1) {
			payload = drm_atomic_get_mst_payload_state(mst_state,
								   mtk_dp->mtk_con[i]->port);
			if (IS_ERR_OR_NULL(payload)) {
				dev_err(mtk_dp->dev, "[DPTX] fail to get mst payload state!\n");
				continue;
			}

			start_slot = payload->vc_start_slot;
			end_slot = start_slot + payload->time_slots;

			/* reg_vc_payload_timeslot */
			if ((start_slot > 64) || (end_slot > 64))
				dev_err(mtk_dp->dev,
					"[DPTX] Invalid slot region, start_slot %d, end_slot %d\n",
					start_slot, end_slot);
			else
				mtk_dp_mst_hal_hdcp_set_timeslot(mtk_dp, start_slot, end_slot);
		}
	}

	mtk_dp_mst_hal_hdcp_trigger_act(mtk_dp);
}

void mtk_dp_mst_atomic_disable(struct mtk_dp *mtk_dp, enum dp_encoder_id id,
			       struct drm_atomic_state *state)
{
	bool re_enable[DP_CONNECTOR_NUM] = {false};
	bool need_hdcp = false;
	int con_id;
	u8 i;

	guard(mutex)(&mtk_dp->mst_mutex);

	con_id = encoder_id_to_con_id(mtk_dp, id, DRM_DP_MST);
	if (con_id < 0)
		return;

	drm_dbg_kms(mtk_dp->drm_dev, "[DPTX] bridge[%d] MST disable", id);

	for (i = 0; i < ARRAY_SIZE(mtk_dp->mtk_con); i++) {
		if (mst_con_with_encoder(mtk_dp->mtk_con[i]) && mtk_dp->mtk_con[i]->video_enable) {
			if (i != con_id)
				re_enable[i] = true;
			mtk_dp_mst_drv_stop(mtk_dp, state, mtk_dp->mtk_con[i]);
		}
	}

	/*
	 * In a multi-channel enabled scenario, if one of the
	 * channels is disabled, the slot positions of the other
	 * channels may change. At this point, a restart of
	 * the other channels is required to make the necessary
	 * adjustments to the hardware.
	 */
	if (mtk_dp->training_info.cable_plug_in)
		for (i = 0; i < ARRAY_SIZE(mtk_dp->mtk_con); i++)
			if (mst_con_with_encoder(mtk_dp->mtk_con[i]) && re_enable[i])
				mtk_dp_mst_drv_start(mtk_dp, mtk_dp->mtk_con[i]);

	mtk_dp_audio_update_plugged_status_v2(mtk_dp);

	mtk_dp_hdcp_disable(mtk_dp);
	need_hdcp = mtk_dp_hdcp_need_hdcp(mtk_dp);
	if (need_hdcp)
		mtk_dp_hdcp_enable(mtk_dp);
}

void mtk_dp_mst_atomic_enable(struct mtk_dp *mtk_dp, enum dp_encoder_id id,
			      struct drm_atomic_state *state)
{
	int con_id;

	guard(mutex)(&mtk_dp->mst_mutex);

	con_id = encoder_id_to_con_id(mtk_dp, id, DRM_DP_MST);
	if (con_id < 0)
		return;

	drm_dbg_kms(mtk_dp->drm_dev, "[DPTX] bridge[%d] MST enable", id);

	if (!mtk_dp->training_info.cable_plug_in || !mtk_dp->dp_ready) {
		dev_err(mtk_dp->dev, "[DPTX] bridge[%d] MST cable_plug_in:%d, dp_ready:%d",
			id, mtk_dp->training_info.cable_plug_in, mtk_dp->dp_ready);
		return;
	}

	mtk_dp_mst_drv_start(mtk_dp, mtk_dp->mtk_con[con_id]);

	mtk_dp_audio_update_plugged_status_v2(mtk_dp);

	drm_dbg_kms(mtk_dp->drm_dev, "[DPTX] bridge[%d] MST content type:%d, content protection:%d",
		    id, mtk_dp->con_state[id].hdcp_content_type,
		    mtk_dp->con_state[id].content_protection);
	if (mtk_dp->con_state[id].content_protection ==
		DRM_MODE_CONTENT_PROTECTION_DESIRED) {
		mtk_dp_hdcp_disable(mtk_dp);
		mtk_dp_hdcp_enable(mtk_dp);
	}
}

int mtk_dp_mst_atomic_check(struct mtk_dp *mtk_dp, enum dp_encoder_id id,
			    struct drm_bridge_state *bridge_state,
			    struct drm_crtc_state *crtc_state,
			    struct drm_connector_state *conn_state)
{
	struct drm_atomic_state *state = crtc_state->state;
	struct drm_dp_mst_topology_state *mst_state;
	int con_id;
	int pbn_need;
	int pbn_allocated;
	int slot_need;
	int slot_allocated;
	bool dsc = false;
	u8 bpp;
	int ret;

	guard(mutex)(&mtk_dp->mst_mutex);

	con_id = encoder_id_to_con_id(mtk_dp, id, DRM_DP_MST);
	if (con_id < 0)
		return con_id;

	mst_state = to_drm_dp_mst_topology_state(mtk_dp->mgr.base.state);
	if (IS_ERR(mst_state)) {
		dev_err(mtk_dp->dev, "[DPTX] fail to get mst topology state!\n");
		return PTR_ERR(mst_state);
	}

	bpp = mtk_dp_color_get_bpp_v2(mtk_dp->info[id].format, mtk_dp->info[id].depth);
	mtk_dp_mst_drv_check_mode(mtk_dp, mtk_dp->mtk_con[con_id], &crtc_state->adjusted_mode, bpp, &dsc);

	if (dsc)
		bpp = mtk_dp_color_get_bpp_v2(DP_PIXELFORMAT_RGB, DP_COLOR_DEPTH_8BIT);

	pbn_need = mtk_dp_mst_drv_calculate_pbn(mtk_dp, &crtc_state->adjusted_mode, bpp, dsc);
	slot_need = mtk_dp_mst_drv_find_vcpi_slots(mtk_dp,
						   dfixed_trunc(mst_state->pbn_div), pbn_need);
	pbn_allocated = slot_need * dfixed_trunc(mst_state->pbn_div);
	slot_allocated = drm_dp_atomic_find_time_slots(state,
						       &mtk_dp->mgr, mtk_dp->mtk_con[con_id]->port, pbn_allocated);

	if (slot_allocated < 0) {
		dev_err(mtk_dp->dev, "[DPTX] fail to find time slots:%d", slot_allocated);
		ret = slot_allocated;
		goto fail;
	}

	ret = drm_dp_mst_atomic_check(state);
	if (ret) {
		dev_err(mtk_dp->dev, "[DPTX] fail to atomic check:%d", ret);
		goto fail;
	}

	drm_mode_copy(&mtk_dp->mode[id], &crtc_state->adjusted_mode);

	mtk_dp->dsc_enable[id] = dsc;
	if (mtk_dp->dsc_enable[id]) {
		mtk_dp_dsc_check_prepare_v2(mtk_dp, id);
		mtk_dp->prop_dsc_enable[id]->values[0] = 1;
	} else {
		mtk_dp->prop_dsc_enable[id]->values[0] = 0;
	}

	drm_dbg_kms(mtk_dp->drm_dev,
		    "[DPTX] bridge[%d] MST atomic check, color:in[0x%04x] out[0x%04x], bpp:%d, dsc:%d, tt:%d %d, act:%d %d, fps:%d, clk:%d, pbn:%d %d, slot:%d %d\n",
		    id, bridge_state->input_bus_cfg.format, bridge_state->output_bus_cfg.format,
		    bpp, dsc, crtc_state->adjusted_mode.htotal, crtc_state->adjusted_mode.vtotal,
		    crtc_state->adjusted_mode.hdisplay, crtc_state->adjusted_mode.vdisplay,
		    drm_mode_vrefresh(&crtc_state->adjusted_mode), crtc_state->adjusted_mode.clock,
		    pbn_need, pbn_allocated, slot_need, slot_allocated);

	return 0;

fail:
	dev_err(mtk_dp->dev,
		"[DPTX] bridge[%d] MST atomic check fail! bpp:%d, dsc:%d, tt:%d %d, act:%d %d, fps:%d, clk:%d, pbn:%d %d, slot:%d %d\n",
		id, bpp, dsc, crtc_state->adjusted_mode.htotal, crtc_state->adjusted_mode.vtotal,
		crtc_state->adjusted_mode.hdisplay, crtc_state->adjusted_mode.vdisplay,
		drm_mode_vrefresh(&crtc_state->adjusted_mode), crtc_state->adjusted_mode.clock,
		pbn_need, pbn_allocated, slot_need, slot_allocated);

	return ret;
}

static int mtk_dp_mst_connector_late_register(struct drm_connector *connector)
{
	struct mtk_dp_con *mtk_con = container_of(connector, struct mtk_dp_con, connector);

	return drm_dp_mst_connector_late_register(connector, mtk_con->port);
}

static void mtk_dp_mst_connector_early_unregister(struct drm_connector *connector)
{
	struct mtk_dp_con *mtk_con = container_of(connector, struct mtk_dp_con, connector);

	drm_dbg_kms(mtk_con->mtk_dp->drm_dev,
		    "[DPTX] connector[%d] MST early unregister\n", mtk_dp_con_id(mtk_con->mtk_dp, mtk_con));

	drm_dp_mst_connector_early_unregister(connector, mtk_con->port);
}

static void mtk_dp_mst_connector_destroy(struct drm_connector *connector)
{
	struct mtk_dp_con *mtk_con = container_of(connector, struct mtk_dp_con, connector);
	struct mtk_dp *mtk_dp = mtk_con->mtk_dp;
	int id =  mtk_dp_con_id(mtk_dp, mtk_con);

	drm_dbg_kms(mtk_dp->drm_dev, "[DPTX] connector[%d] MST destroy\n", id);

	if (id < 0)
		return;

	drm_connector_cleanup(connector);

	if (mtk_con->port)
		drm_dp_mst_put_port_malloc(mtk_con->port);

	kfree(mtk_con);
	mtk_dp->mtk_con[id] = NULL;
}

static const struct drm_connector_funcs mtk_dp_mst_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.late_register = mtk_dp_mst_connector_late_register,
	.early_unregister = mtk_dp_mst_connector_early_unregister,
	.reset = drm_atomic_helper_connector_reset,
	.destroy = mtk_dp_mst_connector_destroy,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int mtk_dp_mst_connector_get_modes(struct drm_connector *connector)
{
	struct mtk_dp_con *mtk_con = container_of(connector, struct mtk_dp_con, connector);
	struct mtk_dp *mtk_dp = mtk_con->mtk_dp;
	const struct drm_edid *drm_edid;
	struct mtk_dp_audio_cfg *audio_caps;
	struct cea_sad *sads;
	int encoder_id;
	int ret;

	if (drm_connector_is_unregistered(connector)) {
		drm_edid_connector_update(connector, NULL);
		return drm_edid_connector_add_modes(connector);
	}

	drm_edid = drm_dp_mst_edid_read(connector, &mtk_dp->mgr, mtk_con->port);

	drm_edid_connector_update(connector, drm_edid);
	ret = drm_edid_connector_add_modes(connector);

	/* audio caps */
	encoder_id = mtk_con->encoder_id;
	audio_caps = &mtk_dp->info[encoder_id].audio_cur_cfg;
	audio_caps->sad_count = drm_edid_to_sad(drm_edid_raw(drm_edid), &sads);
	kfree(sads);
	audio_caps->detect_monitor = drm_detect_monitor_audio(drm_edid_raw(drm_edid));

	drm_edid_free(drm_edid);

	drm_dbg_kms(mtk_dp->drm_dev, "[DPTX] connector[%d] MST modes:%d\n", mtk_dp_con_id(mtk_dp, mtk_con), ret);

	return ret;
}

static enum drm_mode_status mtk_dp_mst_connector_mode_valid(struct drm_connector *connector,
								struct drm_display_mode *mode)
{
	struct mtk_dp_con *mtk_con;
	struct mtk_dp *mtk_dp;
	enum drm_mode_status mode_status;
	bool dsc = false;
	u8 bpp;

	mtk_con = container_of(connector, struct mtk_dp_con, connector);
	mtk_dp = mtk_con->mtk_dp;

	/* MST only support RGB 8bit now */
	bpp = mtk_dp_color_get_bpp_v2(DP_PIXELFORMAT_RGB, DP_COLOR_DEPTH_8BIT);
	mode_status = mtk_dp_mst_drv_check_mode(mtk_dp, mtk_con, mode, bpp, &dsc);

	drm_dbg_kms(mtk_dp->drm_dev,
		    "[DPTX] connector[%d] MST mode valid, status:%d, dsc:%d, Htt:%d, Vtt:%d, Hact:%d, Vact:%d, fps:%d, clk:%d\n",
		    mtk_dp_con_id(mtk_dp, mtk_con), mode_status, dsc,
		    mode->htotal, mode->vtotal,
		    mode->hdisplay, mode->vdisplay,
		    drm_mode_vrefresh(mode), mode->clock);

	return mode_status;
}

static struct drm_encoder *mtk_dp_mst_connector_atomic_best_encoder(struct drm_connector *connector,
							struct drm_atomic_state *state)
{
	struct mtk_dp_con *mtk_con;
	struct mtk_dp *mtk_dp;
	struct drm_bridge *bridge;
	u8 i, j;

	mtk_con = container_of(connector, struct mtk_dp_con, connector);
	mtk_dp = mtk_con->mtk_dp;

	if (mtk_con->encoder)
		return mtk_con->encoder;

	for (i = 0; i < DP_ENCODER_NUM; i++) {
		bridge = devm_drm_of_get_bridge(mtk_dp->dev,
					mtk_dp->dev->of_node, i, DP_ENCODER_ENDPOINT);
		if (IS_ERR(bridge)) {
			drm_dbg_kms(mtk_dp->drm_dev,
				    "[DPTX] best encoder, can not find bridge[%d, %d]", i, 0);
			continue;
		}
		if (!bridge->encoder) {
			drm_dbg_kms(mtk_dp->drm_dev,
				    "[DPTX] best encoder, bridge have no encoder[%d, %d]", i, 0);
			continue;
		}
		drm_dbg_kms(mtk_dp->drm_dev,
			    "[DPTX] best encoder, found dp_intf[%d] bridge node:%pOF\n",
			i, bridge->of_node);

		for (j = 0; j < ARRAY_SIZE(mtk_dp->mtk_con); j++) {
			if (mtk_dp->mtk_con[j] &&
			    mtk_dp->mtk_con[j]->dp_mode == DRM_DP_MST &&
			    mtk_dp->mtk_con[j]->encoder == bridge->encoder)
				break;
		}

		if (j == ARRAY_SIZE(mtk_dp->mtk_con)) {
			drm_dbg_kms(mtk_dp->drm_dev,
				    "[DPTX] connector[%d] MST best encoder, found encoder with dp_intf[%d] node:%pOF\n",
				    mtk_dp_con_id(mtk_dp, mtk_con), i, bridge->of_node);

			mtk_con->encoder = bridge->encoder;
			mtk_con->encoder_id = i;

			return bridge->encoder;
		}
	}

	drm_dbg_kms(mtk_dp->drm_dev, "[DPTX] best encoder, fail to find encoder!");

	return NULL;
}

static int mtk_dp_mst_connector_atomic_check(struct drm_connector *connector,
			struct drm_atomic_state *state)
{
	struct mtk_dp_con *mtk_con = container_of(connector, struct mtk_dp_con, connector);

	drm_dbg_kms(mtk_con->mtk_dp->drm_dev,
		    "[DPTX] connector[%d] MST atomic check\n", mtk_dp_con_id(mtk_con->mtk_dp, mtk_con));

	return drm_dp_atomic_release_time_slots(state, &mtk_con->mtk_dp->mgr, mtk_con->port);
}

static int mtk_dp_mst_connector_detect(struct drm_connector *connector,
			struct drm_modeset_acquire_ctx *ctx, bool force)
{
	struct mtk_dp_con *mtk_con = container_of(connector, struct mtk_dp_con, connector);
	struct mtk_dp *mtk_dp = mtk_con->mtk_dp;
	int ret;

	ret = drm_dp_mst_detect_port(connector, ctx, &mtk_dp->mgr,
			mtk_con->port);

	drm_dbg_kms(mtk_dp->drm_dev,
		    "[DPTX] connector[%d] MST detect:%d\n", mtk_dp_con_id(mtk_dp, mtk_con), ret);

	return ret;
}

static const struct drm_connector_helper_funcs mtk_dp_mst_connector_helper_funcs = {
	.get_modes = mtk_dp_mst_connector_get_modes,
	.mode_valid = mtk_dp_mst_connector_mode_valid,
	.atomic_best_encoder = mtk_dp_mst_connector_atomic_best_encoder,
	.atomic_check = mtk_dp_mst_connector_atomic_check,
	.detect_ctx = mtk_dp_mst_connector_detect,
};

static struct drm_connector *mtk_dp_add_connector(struct drm_dp_mst_topology_mgr *mgr,
								struct drm_dp_mst_port *port,
								const char *pathprop)
{
	struct mtk_dp_con *mtk_con;
	struct drm_connector *connector;
	struct drm_bridge *bridge;
	struct mtk_dp *mtk_dp;
	int con_id;
	int ret;
	u8 i;

	mtk_dp = container_of(mgr, struct mtk_dp, mgr);

	for (i = 0; i < ARRAY_SIZE(mtk_dp->mtk_con); i++) {
		if (!mtk_dp->mtk_con[i]) {
			con_id = i;
			break;
		}
	}

	if (i == ARRAY_SIZE(mtk_dp->mtk_con)) {
		dev_err(mtk_dp->dev, "[DPTX] create con, fail to find idle item in mtk_con array!");
		return NULL;
	}

	mtk_con = kzalloc(sizeof(*mtk_con), GFP_KERNEL);
	if (!mtk_con)
		return NULL;

	mtk_dp->mtk_con[con_id] = mtk_con;
	mtk_dp->mtk_con[con_id]->dp_mode = DRM_DP_MST;
	mtk_con->mtk_dp = mtk_dp;
	mtk_con->port = port;
	drm_dp_mst_get_port_malloc(port);

	mtk_con->dsc_aux = drm_dp_mst_dsc_aux_for_port(port);
	mtk_dp_mst_drv_read_port_dsc_caps(mtk_dp, mtk_con);
	mtk_con->dsc_hblank_expansion_quirk =
		mtk_dp_mst_drv_detect_dsc_hblank_expansion_quirk(mtk_con);

	connector = &mtk_con->connector;
	ret = drm_connector_init(mtk_dp->drm_dev, connector, &mtk_dp_mst_connector_funcs,
			DRM_MODE_CONNECTOR_DisplayPort);
	if (ret) {
		dev_err(mtk_dp->dev, "[DPTX] create con, fail to init connector!\n");
		goto end;
	}
	drm_connector_helper_add(connector, &mtk_dp_mst_connector_helper_funcs);

	for (i = 0; i < DP_ENCODER_NUM; i++) {
		bridge = devm_drm_of_get_bridge(mtk_dp->dev, mtk_dp->dev->of_node, i, 0);
		if (IS_ERR(bridge)) {
			drm_dbg_kms(mtk_dp->drm_dev,
				    "[DPTX] create con, fail to find bridge[%d, %d]", i, 0);
			continue;
		}

		if (!bridge->encoder) {
			drm_dbg_kms(mtk_dp->drm_dev,
				    "[DPTX] create con, fail to find bridge encoder[%d, %d]", i, 0);
			continue;
		}

		drm_connector_attach_encoder(connector, bridge->encoder);
	}

	if (mtk_con->connector.funcs->reset)
		mtk_con->connector.funcs->reset(&mtk_con->connector);

	drm_connector_attach_content_protection_property(&mtk_con->connector, true);

	ret = drm_connector_register(&mtk_con->connector);
	if (ret) {
		drm_dbg_kms(mtk_dp->drm_dev,
			    "[DPTX] create con, fail to register connector:%d\n", ret);
		goto end;
	}

	drm_dbg_kms(mtk_dp->drm_dev, "[DPTX] create con, create MST connector[%d]\n", con_id);

	return connector;

end:
	drm_dp_mst_put_port_malloc(port);
	mtk_dp->mtk_con[con_id] = NULL;
	kfree(mtk_con);
	return NULL;
}

static const struct drm_dp_mst_topology_cbs mtk_mst_topology_cbs = {
	.add_connector = mtk_dp_add_connector,
};

void mtk_dp_mst_drv_init(struct mtk_dp *mtk_dp)
{
	int ret = 0;
	int conn_base_id = 0;

	drm_dbg_kms(mtk_dp->drm_dev, "[DPTX] MST init\n");

	mtk_dp->mgr.cbs = &mtk_mst_topology_cbs;
	ret = drm_dp_mst_topology_mgr_init(&mtk_dp->mgr, mtk_dp->drm_dev, &mtk_dp->aux,
			DPTX_DPCD_TRANS_BYTES_MAX, DP_ENCODER_NUM, conn_base_id);
	if (ret < 0)
		dev_err(mtk_dp->dev, "[DPTX] fail to init mst topology mgr!\n");
}

void mtk_dp_mst_drv_deinit(struct mtk_dp *mtk_dp)
{
	drm_dbg_kms(mtk_dp->drm_dev, "[DPTX] MST deinit\n");

	drm_dp_mst_topology_mgr_destroy(&mtk_dp->mgr);
	mtk_dp->mgr.cbs = NULL;
}
