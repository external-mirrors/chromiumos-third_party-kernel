// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#include "linux/version.h"
#include "mali_kbase.h"
#include "mtk_gpufreq.h"
#include "mtk_platform_common.h"
#include "mtk_platform_devfreq_thermal.h"

int mtk_devfreq_thermal_get_real_power(struct devfreq *df,
						  u32 *power,
						  unsigned long freq_Hz,
						  unsigned long voltage_mV)
{
	if (!df || !power)
		return -EINVAL;

	*power = gpufreq_get_leakage_power(TARGET_DEFAULT, voltage_mV * 100) +
		 gpufreq_get_dynamic_power(TARGET_DEFAULT, freq_Hz / 1000, voltage_mV * 100);
	return (*power == 0 ? -EINVAL : 0);
}

struct devfreq_cooling_power mtk_devfreq_cooling_power_ops = {
	.get_real_power = &mtk_devfreq_thermal_get_real_power,
};
