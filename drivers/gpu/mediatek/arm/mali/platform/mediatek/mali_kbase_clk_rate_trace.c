// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2025 MediaTek Inc.
 */

#include <ged_clk_rate_trace.h>
#include <mali_kbase.h>
#include <mali_kbase_config_platform.h>

static void *enumerate_gpu_clk(struct kbase_device *kbdev, unsigned int index)
{
	if (index > 0)
		return NULL;

	dev_dbg(kbdev->dev, "@%s:", __func__);
	return ged_get_notifier_handle();
}

static unsigned long get_gpu_clk_rate(struct kbase_device *kbdev,
				      void *gpu_clk_handle)
{
	dev_dbg(kbdev->dev, "@%s:", __func__);
	return ged_clk_rate_trace_get_default_freq();
}

static int gpu_clk_notifier_register(struct kbase_device *kbdev,
				     void *gpu_clk_handle,
				     struct notifier_block *nb)
{
	dev_dbg(kbdev->dev, "@%s:", __func__);
	return ged_clk_rate_trace_notifier_register(gpu_clk_handle, nb);
}

static void gpu_clk_notifier_unregister(struct kbase_device *kbdev,
					void *gpu_clk_handle,
					struct notifier_block *nb)
{
	dev_dbg(kbdev->dev, "@%s:", __func__);
	ged_clk_rate_trace_notifier_unregister(gpu_clk_handle, nb);
}

struct kbase_clk_rate_trace_op_conf clk_rate_trace_ops = {
	.get_gpu_clk_rate = get_gpu_clk_rate,
	.enumerate_gpu_clk = enumerate_gpu_clk,
	.gpu_clk_notifier_register = gpu_clk_notifier_register,
	.gpu_clk_notifier_unregister = gpu_clk_notifier_unregister,
};
