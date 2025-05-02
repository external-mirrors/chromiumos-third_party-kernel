/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 MediaTek Inc.
 */

#ifndef __MTK_PLATFORM_COMMON_H__
#define __MTK_PLATFORM_COMMON_H__

#include <linux/platform_device.h>
#include <backend/gpu/mali_kbase_pm_defs.h>

struct kbase_device *mtk_common_get_kbdev(void);
bool mtk_common_pm_is_mfg_active(void);
void mtk_common_pm_mfg_active(void);
void mtk_common_pm_mfg_idle(void);
int mtk_common_gpufreq_bringup(void);
int mtk_common_gpufreq_commit(int opp_idx);
int mtk_common_gpufreq_dual_commit(int gpu_oppidx, int stack_oppidx);
int mtk_common_ged_dvfs_get_last_commit_idx(void);
int mtk_common_ged_dvfs_get_last_commit_top_idx(void);
int mtk_common_ged_dvfs_get_last_commit_stack_idx(void);
unsigned long mtk_common_ged_dvfs_write_sysram_last_commit_idx(void);
unsigned long mtk_common_ged_dvfs_write_sysram_last_commit_top_idx(void);
unsigned long mtk_common_ged_dvfs_write_sysram_last_commit_stack_idx(void);
unsigned long mtk_common_ged_dvfs_write_sysram_last_commit_idx_test(int commit_idx);
unsigned long mtk_common_ged_dvfs_write_sysram_last_commit_top_idx_test(int commit_idx);
unsigned long mtk_common_ged_dvfs_write_sysram_last_commit_stack_idx_test(int commit_idx);
unsigned long mtk_common_ged_dvfs_write_sysram_last_commit_dual(void);
unsigned long mtk_common_ged_dvfs_write_sysram_last_commit_dual_test(int top_idx, int stack_idx);
int mtk_common_ged_dvfs_update_step_size(int low_step, int med_step, int high_step);
int mtk_common_device_init(struct kbase_device *kbdev);
void mtk_common_device_term(struct kbase_device *kbdev);

#ifdef MALI_MTK_DEBUG_FS
void mtk_common_debugfs_init(struct kbase_device *kbdev);
#endif /* MALI_MTK_DEBUG_FS */

int mtk_common_platform_coremask_init(struct kbase_device *kbdev);
int mtk_platform_pm_init(struct kbase_device *kbdev);
void mtk_platform_pm_term(struct kbase_device *kbdev);

#endif /* __MTK_PLATFORM_COMMON_H__ */
