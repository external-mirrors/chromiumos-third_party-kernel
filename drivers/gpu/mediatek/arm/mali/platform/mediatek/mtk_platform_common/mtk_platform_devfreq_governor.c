// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#include "governor.h"
#include "mali_kbase.h"
#include "mtk_gpufreq.h"
#include "mtk_platform_common.h"
#include "mtk_platform_devfreq_governor.h"

#ifndef KHZ_TO_HZ
#define KHZ_TO_HZ(KHz)	((KHz)*1000)
#endif

#define DUPLICATE_OPP_NUM 2

static int mtk_devfreq_governor_get_target_freq(struct devfreq *df, unsigned long *freq)
{
	int ret;

	ret = devfreq_update_stats(df);
	if (ret)
		return ret;

	*freq = KHZ_TO_HZ(gpufreq_get_freq_by_idx(TARGET_STACK, 0));

	return 0;
}

static int mtk_devfreq_governor_event_handler(struct devfreq *devfreq, unsigned int event, void *data)
{
	switch (event) {
	case DEVFREQ_GOV_START:
		devfreq_monitor_start(devfreq);
		break;

	case DEVFREQ_GOV_STOP:
		devfreq_monitor_stop(devfreq);
		break;

	case DEVFREQ_GOV_UPDATE_INTERVAL:
		devfreq_update_interval(devfreq, (unsigned int *)data);
		break;

	case DEVFREQ_GOV_SUSPEND:
		devfreq_monitor_suspend(devfreq);
		break;

	case DEVFREQ_GOV_RESUME:
		devfreq_monitor_resume(devfreq);
		break;

	default:
		break;
	}

	return 0;
}

struct devfreq_governor mtk_devfreq_governor_gpueb = {
	.name = MTK_GPU_DEVFREQ_GOVERNOR_GPUEB,
	.get_target_freq = mtk_devfreq_governor_get_target_freq,
	.event_handler = mtk_devfreq_governor_event_handler,
};

static int mtk_devfreq_governor_target(struct device *dev, unsigned long *freq, u32 flags)
{
	struct kbase_device *kbdev = (struct kbase_device *)mtk_common_get_kbdev();
	struct devfreq *devfreq = kbdev->devfreq;
	int opp_idx = -1;
	unsigned long freq_khz = 0;

	freq_khz = *freq / 1000;

	if (IS_ERR_OR_NULL(kbdev) || devfreq == NULL || devfreq->governor == NULL) {
		dev_err(kbdev->dev, "@%s: Invalid input parameter\n", __func__);
		return 0;
	}

#if defined(CONFIG_MTK_GPUFREQ_V2)
	if (!strcmp(devfreq->governor->name, MTK_GPU_DEVFREQ_GOVERNOR_GPUEB)) {
		gpufreq_fix_target_oppidx(TARGET_STACK, -1);
		gpufreq_set_limit(TARGET_STACK, LIMIT_THERMAL_AP, freq_khz, GPUPPM_KEEP_IDX);
		opp_idx = gpufreq_get_cur_oppidx(TARGET_STACK);
	} else {
		opp_idx = gpufreq_get_oppidx_by_freq(TARGET_STACK, freq_khz);
		gpufreq_set_limit(TARGET_STACK, LIMIT_THERMAL_AP, freq_khz, GPUPPM_KEEP_IDX);
		gpufreq_fix_target_oppidx(TARGET_STACK, opp_idx);
	}

	*freq = KHZ_TO_HZ(gpufreq_get_freq_by_idx(TARGET_STACK, opp_idx));
	kbdev->current_nominal_freq = *freq;

	dev_dbg(kbdev->dev, "@%s: governor=%s, freq=%lu, current_nominal_freq=%lu, opp_idx=%d\n",
		__func__, devfreq->governor->name, *freq, kbdev->current_nominal_freq, opp_idx);
#endif /* CONFIG_MTK_GPUFREQ_V2 */

	return 0;
}

static int mtk_common_devfreq_get_cur_freq(struct device *dev, unsigned long *freq)
{
	struct kbase_device *kbdev = (struct kbase_device *)mtk_common_get_kbdev();

	if (!IS_ERR_OR_NULL(kbdev))
		*freq = KHZ_TO_HZ(gpufreq_get_cur_freq(TARGET_STACK));
	else
		*freq = 0;

	return 0;
}

static int mtk_common_devfreq_status(struct device *dev, struct devfreq_dev_status *stat)
{
	return 0;
}

void mtk_devfreq_governor_update_profile(struct devfreq_dev_profile *dp)
{
	dp->target = mtk_devfreq_governor_target;
	dp->get_cur_freq = mtk_common_devfreq_get_cur_freq;
	dp->exit = NULL;
}

#ifdef MALI_MTK_DEVFREQ_ENABLE
int mtk_devfreq_init_freq_table(struct kbase_device *kbdev, struct devfreq_dev_profile *dp)
{
	unsigned int i = 0;
	int count;

	count = gpufreq_get_opp_num(TARGET_STACK);
	if (count < 0)
		return count;

	count = count - DUPLICATE_OPP_NUM; /* for the lowest frequency of stack */

	dp->freq_table = kmalloc_array((size_t)count, sizeof(dp->freq_table[0]), GFP_KERNEL);
	if (!dp->freq_table)
		return -ENOMEM;

	for (i = 0; i < count; i++)
		dp->freq_table[i] = KHZ_TO_HZ(gpufreq_get_freq_by_idx(TARGET_STACK, i));

	if ((unsigned int)count != i)
		dev_warn(kbdev->dev, "Unable to enumerate all OPPs (%d!=%u\n", count, i);

	dp->max_state = i;

	return 0;
}
#endif /* MALI_MTK_DEVFREQ_ENABLE */

int mtk_devfreq_governor_init(struct kbase_device *kbdev)
{
	int ret = 0;

	ret = devfreq_add_governor(&mtk_devfreq_governor_gpueb);
	if (ret || IS_ERR_OR_NULL(kbdev)) {
		dev_err(kbdev->dev, "@%s: Failed to add governor '%s' (ret: %d)\n",
			__func__, mtk_devfreq_governor_gpueb.name, ret);
		return ret;
	}

#if defined(CONFIG_MTK_GPUFREQ_V2)
	kbdev->current_nominal_freq = KHZ_TO_HZ(gpufreq_get_cur_freq(TARGET_STACK));
#endif /* CONFIG_MTK_GPUFREQ_V2 */

	return ret;
}

int mtk_devfreq_governor_term(struct kbase_device *kbdev)
{
	int ret = 0;

	ret = devfreq_remove_governor(&mtk_devfreq_governor_gpueb);
	if (ret) {
		dev_err(kbdev->dev, "@%s: Failed to remove governor '%s' (ret: %d)\n",
			__func__, mtk_devfreq_governor_gpueb.name, ret);
		return ret;
	}

	return ret;
}
