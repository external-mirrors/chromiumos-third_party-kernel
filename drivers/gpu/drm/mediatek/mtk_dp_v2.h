/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 MediaTek Inc.
 */

#ifndef __MTK_DP_V2_H__
#define __MTK_DP_V2_H__

#include <drm/drm_atomic_helper.h>
#include "mtk_dp_common.h"

extern const struct drm_encoder_helper_funcs mtk_dp_encoder_helper_funcs;

u32 mtk_dp_read_v2(struct mtk_dp *mtk_dp, u32 offset);
void mtk_dp_mask_v2(struct mtk_dp *mtk_dp, u32 offset, u32 val, u32 mask);
void mtk_dp_write_byte_v2(struct mtk_dp *mtk_dp, u32 addr, u8 val, u32 mask);
void mtk_dp_video_enable_v2(struct mtk_dp *mtk_dp, const enum dp_encoder_id encoder_id);
void mtk_dp_video_disable_v2(struct mtk_dp *mtk_dp, const enum dp_encoder_id encoder_id);
void mtk_dp_audio_pg_enable_v2(struct mtk_dp *mtk_dp, const enum dp_encoder_id encoder_id, u8 channel,
			    u8 fs, u8 enable);
void mtk_dp_audio_config_v2(struct mtk_dp *mtk_dp, const enum dp_encoder_id encoder_id);
void mtk_dp_audio_mute_v2(struct mtk_dp *mtk_dp, const enum dp_encoder_id encoder_id, bool enable);
void mtk_dp_video_mute_v2(struct mtk_dp *mtk_dp, const enum dp_encoder_id encoder_id, bool enable);
u8 mtk_dp_color_get_bpp_v2(u8 color_format, u8 color_depth);
bool mtk_dp_parse_audio_cap_v2(struct mtk_dp *mtk_dp, struct mtk_dp_audio_cfg *cfg);
void mtk_dp_audio_info_v2(struct mtk_dp *mtk_dp, const enum dp_encoder_id encoder_id,
		       int *ch, int *fs, int *len);
int mtk_dp_update_bits_v2(struct mtk_dp *mtk_dp, u32 offset, u32 val, u32 mask);
void mtk_dp_audio_update_plugged_status_v2(struct mtk_dp *mtk_dp, bool video_enable);
void mtk_dp_hdcp_update_value(struct mtk_dp *mtk_dp);
void mtk_dp_hdcp_enable(struct mtk_dp *mtk_dp);
void mtk_dp_hdcp_disable(struct mtk_dp *mtk_dp);
void mtk_dp_hdcp_atomic_check(struct mtk_dp *mtk_dp, enum dp_encoder_id id, struct drm_connector_state *state);
u32 mtk_dp_dsc_cal_clock_v2(struct drm_display_mode *mode);
void mtk_dp_dsc_check_prepare_v2(struct mtk_dp *mtk_dp, const enum dp_encoder_id encoder_id);

static inline struct mtk_dp_con *encoder_to_mtk_con(struct drm_encoder *encoder,
						    enum drm_dp_mst_mode dp_mode)
{
	struct drm_connector *connector;
	struct mtk_dp_con *mtk_con = NULL;
	struct drm_device *dev = encoder->dev;
	struct drm_connector_list_iter conn_iter;

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		if (connector && connector->possible_encoders & drm_encoder_mask(encoder)) {
			mtk_con = container_of(connector, struct mtk_dp_con, connector);
			if (mtk_con->encoder == encoder && mtk_con->dp_mode == dp_mode)
				break;
		}
	}
	drm_connector_list_iter_end(&conn_iter);

	if (!mtk_con)
		dev_err(mtk_con->mtk_dp->dev, "fail to find the mtk connector by the encoder");

	return mtk_con;
}

static inline int mtk_dp_con_id(struct mtk_dp *mtk_dp, struct mtk_dp_con *mtk_con)
{
	int i = 0;
	int id = -ENODEV;

	for (i = 0; i < ARRAY_SIZE(mtk_dp->mtk_con); i++) {
		if (mtk_dp->mtk_con[i] && mtk_dp->mtk_con[i] == mtk_con) {
			id = i;
			break;
		}
	}

	return id;
}

static inline int encoder_id_to_con_id(struct mtk_dp *mtk_dp,
				       int encoder_id, enum drm_dp_mst_mode dp_mode)
{
	int i = 0;
	int id = -ENODEV;

	for (i = 0; i < ARRAY_SIZE(mtk_dp->mtk_con); i++) {
		if (mtk_dp->mtk_con[i] && mtk_dp->mtk_con[i]->encoder &&
		    mtk_dp->mtk_con[i]->encoder_id == encoder_id &&
			mtk_dp->mtk_con[i]->dp_mode == dp_mode) {
			id = i;
			break;
		}
	}

	return id;
}
#endif
