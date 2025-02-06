/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#ifndef __MTK_GPU_DVFS_H__
#define __MTK_GPU_DVFS_H__

#include <ged_dvfs.h>

void mtk_common_cal_gpu_utilization_ex(unsigned int *pui32Loading,
						unsigned int *pui32Block,
						unsigned int *pui32Idle,
						void *Util_Ex);
void mtk_common_ged_dvfs_commit(unsigned long ui32NewFreqID,
					GED_DVFS_COMMIT_TYPE eCommitType,
					int *pbCommited);
void mtk_common_ged_dvfs_dual_commit(unsigned long gpuNewFreqID,
						unsigned long stackNewFreqID,
						int *pbCommited);
void mtk_common_update_gpu_utilization(void);
int mtk_set_core_mask(u64 core_mask);
u64 mtk_common_get_system_timer(void);

extern void (*ged_dvfs_cal_gpu_utilization_ex_fp)(unsigned int *pui32Loading,
		unsigned int *pui32Block, unsigned int *pui32Idle, void *Util_Ex);
extern void (*ged_dvfs_gpu_freq_commit_fp)(unsigned long ui32NewFreqID,
		GED_DVFS_COMMIT_TYPE eCommitType, int *pbCommited);
extern void (*ged_dvfs_gpu_freq_dual_commit_fp)(unsigned long gpuNewFreqID,
		unsigned long stackNewFreqID, int *pbCommited);

void MTKGPUFreq_change_notify(u32 clk_idx, u32 gpufreq);
extern void (*mtk_notify_gpu_freq_change_fp)(u32 clk_idx, u32 gpufreq);

extern int (*ged_dvfs_set_gpu_core_mask_fp)(u64 core_mask);

extern void (*mtk_set_gpu_idle_fp)(unsigned int val);
extern u64 (*mtk_get_system_timer_fp)(void);

int mtk_dvfs_procfs_init(struct kbase_device *kbdev, struct proc_dir_entry *parent);
int mtk_dvfs_procfs_term(struct kbase_device *kbdev, struct proc_dir_entry *parent);

int mtk_dvfs_init(struct kbase_device *kbdev);
int mtk_dvfs_term(struct kbase_device *kbdev);

#endif /* __MTK_GPU_DVFS_H__ */
