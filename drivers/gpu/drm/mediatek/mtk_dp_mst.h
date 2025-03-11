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
#endif

