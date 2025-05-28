/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#ifndef __MTK_GPU_IPA_H__
#define __MTK_GPU_IPA_H__

#include <linux/devfreq.h>
#include <linux/devfreq_cooling.h>

extern struct devfreq_cooling_power mtk_devfreq_cooling_power_ops;

int mtk_devfreq_thermal_get_real_power(struct devfreq *df,
							u32 *power,
							unsigned long freq_Hz,
							unsigned long voltage_mV);

#endif /* __MTK_GPU_IPA_H__ */
