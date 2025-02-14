// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#include "linux/version.h"
#include "mali_kbase.h"
#include "mali_kbase_pm_internal.h"
#include "mtk_gpufreq.h"
#include "mtk_platform_common.h"
#include "mtk_platform_devfreq_thermal.h"

int mtk_devfreq_thermal_get_real_power(struct devfreq *df,
						  u32 *power,
						  unsigned long freq_Hz,
						  unsigned long voltage_mV)
{
	static struct kbasep_pm_metrics last_metrics;
	struct kbasep_pm_metrics diff;
	struct kbase_device *kbdev;
	u64 total_time;

	if (!df || !power)
		return -EINVAL;

	*power = gpufreq_get_leakage_power(TARGET_DEFAULT, voltage_mV * 100) +
		 gpufreq_get_dynamic_power(TARGET_DEFAULT, freq_Hz / 1000, voltage_mV * 100);

	/* apply utilization */
	kbdev = dev_get_drvdata(&df->dev);
	if (!kbdev || *power == 0)
		return -EINVAL;

	kbase_pm_get_dvfs_metrics(kbdev, &last_metrics, &diff);

	total_time = diff.time_busy + (u64)diff.time_idle;
	*power = div_u64(*power * (u64)diff.time_busy, max(total_time, 1ull));

	return 0;
}

struct devfreq_cooling_power mtk_devfreq_cooling_power_ops = {
	.get_real_power = &mtk_devfreq_thermal_get_real_power,
};
