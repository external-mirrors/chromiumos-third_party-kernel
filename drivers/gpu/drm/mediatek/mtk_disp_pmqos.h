/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 MediaTek Inc.
 */

#ifndef _MTK_DISP_PMQOS_H_
#define _MTK_DISP_PMQOS_H_

#include <soc/mediatek/mtk-interconnect-provider.h>

#define NO_PENDING_HRT (0xffffffff)

void mtk_disp_pmqos_mmdvfs_init(struct device *dev, struct device *dpc_dev);
void mtk_disp_pmqos_set_mmclk_by_pixclk(struct device *dev, int crtc_id, unsigned int pixclk,
					const char *caller);
void mtk_disp_pmqos_set_channel_hrt_bw(struct device *dev, int crtc_id, unsigned int bw, int i);
void mtk_disp_pmqos_set_module_hrt(struct icc_path *request, struct device *dev,
				   unsigned int bandwidth);
void mtk_disp_pmqos_set_module_srt(struct icc_path *request, struct device *dev,
				   unsigned int bandwidth);
void mtk_disp_pmqos_set_hrt_bw(struct device *dev, int crtc_id, unsigned int bw);
void mtk_disp_pmqos_clear_channel_srt_bw(struct device *dev, int crtc_id);
void mtk_disp_pmqos_set_channel_srt_bw(struct device *dev, int crtc_id, unsigned int bw, int i);
void mtk_disp_pmqos_set_srt_bw(struct device *dev, int crtc_id, unsigned int bw);
#endif
