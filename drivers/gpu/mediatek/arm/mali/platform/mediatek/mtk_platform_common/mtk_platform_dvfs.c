// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#include "mali_kbase.h"
#include "backend/gpu/mali_kbase_pm_internal.h"
#include "backend/gpu/mali_kbase_pm_defs.h"
#include "mtk_gpufreq.h"
#include "mtk_gpu_utility.h"
#include "mtk_platform_common.h"
#include "mtk_platform_dvfs.h"
#include "ged_clk_rate_trace.h"

void mtk_common_ged_dvfs_commit(unsigned long ui32NewFreqID,
					GED_DVFS_COMMIT_TYPE eCommitType,
					int *pbCommited)
{
	struct kbase_device *kbdev = (struct kbase_device *)mtk_common_get_kbdev();
	int ret;

	if (!IS_ERR_OR_NULL(kbdev)) {
		if (kbdev->pm.backend.gpu_ready) {
			ret = mtk_common_gpufreq_commit(ui32NewFreqID);
			if (pbCommited)
				*pbCommited = (ret == 0) ? true : false;
		}
	}
}

void mtk_common_ged_dvfs_dual_commit(unsigned long gpuNewFreqID,
					unsigned long stackNewFreqID,
					int *pbCommited)
{
	struct kbase_device *kbdev = (struct kbase_device *)mtk_common_get_kbdev();
	int ret;

	if (!IS_ERR_OR_NULL(kbdev)) {
		if (kbdev->pm.backend.gpu_ready) {
			ret = mtk_common_gpufreq_dual_commit(gpuNewFreqID, stackNewFreqID);
			if (pbCommited)
				*pbCommited = (ret == 0) ? true : false;
		}
	}
}

void mtk_common_update_gpu_utilization(void)
{
	unsigned int loading, block, idle;
	struct GpuUtilization_Ex util_ex;

	mtk_common_cal_gpu_utilization_ex(&loading, &block, &idle, &util_ex);
}

void mtk_common_cal_gpu_utilization_ex(unsigned int *pui32Loading,
						  unsigned int *pui32Block,
						  unsigned int *pui32Idle,
						  void *Util_Ex)
{
	static struct kbasep_pm_metrics diff;
	static struct kbasep_pm_metrics dvfs_last;
	struct kbase_device *kbdev = (struct kbase_device *)mtk_common_get_kbdev();
	struct GpuUtilization_Ex *util_ex = (struct GpuUtilization_Ex *)Util_Ex;
	int utilisation;
	unsigned long long delta_time;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	kbase_pm_get_dvfs_metrics(kbdev, &dvfs_last, &diff);
	delta_time = max(diff.time_busy + diff.time_idle, 1u);

	utilisation = (100 * diff.time_busy)/delta_time;
	util_ex->util_active = utilisation;
	util_ex->delta_time = delta_time << 8; /* 8 = KBASE_PM_TIME_SHIFT */

	if (pui32Loading)
		*pui32Loading = utilisation;

	if (pui32Idle)
		*pui32Idle = 100 - utilisation;

}

int (*mtk_common_rate_change_notify_fp)(struct kbase_device *kbdev,
					u32 clk_index, u32 clk_rate_hz) = NULL;
EXPORT_SYMBOL(mtk_common_rate_change_notify_fp);

void MTKGPUFreq_change_notify(u32 clk_idx, u32 gpufreq)
{
	struct kbase_device *kbdev = (struct kbase_device *)mtk_common_get_kbdev();

	if (mtk_common_rate_change_notify_fp && !IS_ERR_OR_NULL(kbdev))
		mtk_common_rate_change_notify_fp(kbdev, clk_idx, gpufreq);

	ged_clk_rate_change_notify(gpufreq);
}

int mtk_set_core_mask(u64 core_mask)
{
	struct kbase_device *kbdev = (struct kbase_device *)mtk_common_get_kbdev();
	int ret = 0;

	kbase_devfreq_set_core_mask(kbdev, core_mask);

	/* TODO: need to check get_core_mask hang issue, to verity the scaling result */
	// current_mask = kbdev->pm.backend.ca_cores_enabled;

	return ret;
}

u64 mtk_common_get_system_timer(void)
{
	struct kbase_device *kbdev = (struct kbase_device *)mtk_common_get_kbdev();
	u64 system_time_tmp = 0;
	bool gpu_ready;
	unsigned long flags;

	/* need to lock for using function point */
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	gpu_ready = kbdev->pm.backend.gpu_ready;

	if (gpu_ready)
		kbase_backend_get_gpu_time_norequest(kbdev, NULL, &system_time_tmp, NULL);

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return system_time_tmp;
}

int mtk_dvfs_init(struct kbase_device *kbdev)
{
	if (IS_ERR_OR_NULL(kbdev))
		return -1;

	ged_dvfs_cal_gpu_utilization_ex_fp = mtk_common_cal_gpu_utilization_ex;
	mtk_notify_gpu_freq_change_fp = MTKGPUFreq_change_notify;
	ged_dvfs_gpu_freq_commit_fp = mtk_common_ged_dvfs_commit;
	ged_dvfs_gpu_freq_dual_commit_fp = mtk_common_ged_dvfs_dual_commit;
	ged_dvfs_set_gpu_core_mask_fp = mtk_set_core_mask;
	mtk_get_system_timer_fp = mtk_common_get_system_timer;
	mtk_set_gpu_idle_fp = NULL;
	return 0;
}

int mtk_dvfs_term(struct kbase_device *kbdev)
{
	if (IS_ERR_OR_NULL(kbdev))
		return -1;
	ged_dvfs_cal_gpu_utilization_ex_fp = NULL;
	mtk_notify_gpu_freq_change_fp = NULL;
	ged_dvfs_gpu_freq_commit_fp = NULL;
	ged_dvfs_gpu_freq_dual_commit_fp = NULL;
	ged_dvfs_set_gpu_core_mask_fp = NULL;
	mtk_set_gpu_idle_fp = NULL;
	return 0;
}
