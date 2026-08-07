/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 MediaTek Inc.
 */

#ifndef __MTK_DP_MST_H__
#define __MTK_DP_MST_H__

#include "mtk_dp_common.h"

void mtk_dp_mst_drv_init(struct mtk_dp *mtk_dp);
void mtk_dp_mst_drv_deinit(struct mtk_dp *mtk_dp);
void mtk_dp_mst_drv_prepare(struct mtk_dp *mtk_dp);
void mtk_dp_mst_drv_unprepare(struct mtk_dp *mtk_dp);
int mtk_dp_mst_drv_calculate_pbn(struct mtk_dp *mtk_dp,
				 struct drm_display_mode *mode, u8 bpp, bool dsc);
int mtk_dp_mst_drv_find_vcpi_slots(struct mtk_dp *mtk_dp,
				   int pbn_div, const int pbn_allocating);
void mtk_dp_mst_drv_set_hdcp_setting(struct mtk_dp *mtk_dp);
void mtk_dp_mst_atomic_disable(struct mtk_dp *mtk_dp,
			       enum dp_encoder_id id, struct drm_atomic_state *state);
void mtk_dp_mst_atomic_enable(struct mtk_dp *mtk_dp,
			      enum dp_encoder_id id, struct drm_atomic_state *state);
int mtk_dp_mst_atomic_check(struct mtk_dp *mtk_dp, enum dp_encoder_id id,
			    struct drm_bridge_state *bridge_state,
			    struct drm_crtc_state *crtc_state,
			    struct drm_connector_state *conn_state);
enum drm_mode_status mtk_dp_mst_encoder_mode_valid_mt8196(u8 encoder_id,
							  const struct drm_display_mode *mode);
int mtk_dp_mst_best_encoder_mt8196(struct mtk_dp *mtk_dp, struct drm_connector *connector);
#endif

