/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2025 MediaTek Inc.
 * Author: Echo.Zhang <echo.zhang@mediatek.com>
 */

#ifndef __MTK_MMQOS_H__
#define __MTK_MMQOS_H__

#define MTK_MMQOS_MAX_BW             (0x10000000)

enum hrt_type {
	HRT_MD,
	HRT_CAM,
	HRT_DISP,
	HRT_MML,
	HRT_TYPE_NUM,
	HRT_NONE = HRT_TYPE_NUM,
	HRT_MAX_BWL,
	HRT_DISP_BY_LARB,
	SRT_VDEC,
	SRT_VENC,
	SRT_MDP,
	SRT_MML,
};

#if IS_ENABLED(CONFIG_INTERCONNECT_MTK_MMQOS_COMMON)
s32 mtk_mmqos_set_hrt_bw(enum hrt_type type, u32 bw);
s32 mtk_mmqos_get_hrt_ratio(enum hrt_type type);
s32 mtk_mmqos_get_avail_hrt_bw(enum hrt_type type);
#else
static inline s32 mtk_mmqos_set_hrt_bw(enum hrt_type type, u32 bw)
{
	return 0;
}

static inline s32 mtk_mmqos_get_hrt_ratio(enum hrt_type type)
{
	return 0;
}

static inline s32 mtk_mmqos_get_avail_hrt_bw(enum hrt_type type)
{
	return -1;
}
#endif

#endif /* MTK_MMQOS_H */
