/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 MediaTek Inc.
 */

#ifndef __MTK_DPC_H__
#define __MTK_DPC_H__

enum mtk_dpc_subsys {
	DPC_SUBSYS_DISP,
	DPC_SUBSYS_MML0,
	DPC_SUBSYS_MML1,
	DPC_SUBSYS_CNT
};

enum channel_type {
	CHANNEL_SRT_READ,
	CHANNEL_HRT_READ,
};

int dpc_get_freq_step(struct device *dev);
unsigned long dpc_get_freq_by_idx(struct device *dev, u32 index);
void dpc_enable(struct device *dev, const enum mtk_dpc_subsys subsys);
void dpc_disable(struct device *dev, const enum mtk_dpc_subsys subsys);
void dpc_hrt_bw_set(struct device *dev, const enum mtk_dpc_subsys subsys, const u32 bw_in_mb);
void dpc_srt_bw_set(struct device *dev, const enum mtk_dpc_subsys subsys, const u32 bw_in_mb);
void dpc_dvfs_set(struct device *dev, const enum mtk_dpc_subsys subsys, const u8 level,
		  bool update_level);
void dpc_channel_bw_set_by_idx(struct device *dev, const enum mtk_dpc_subsys subsys,
			       const enum channel_type type, const u8 idx, const u32 bw_in_mb);

#endif
