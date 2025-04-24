// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 MediaTek Inc.
 */

#include "linux/clk.h"
#include "linux/device.h"
#include "linux/mutex.h"
#include "ged_clk_rate_trace.h"
#include "ged_type.h"
#include "mali_kbase_config.h"
#include "mtk_gpufreq.h"

#define KHZ_TO_HZ(KHz) ((KHz) * 1000)

static struct device *ged_dev;
static struct srcu_notifier_head notifier_head;
static unsigned int last_freq_hz;
static struct mutex lock;
static bool is_inited;

inline static unsigned int get_last_freq(void)
{
	return last_freq_hz;
}

inline static void update_last_freq(unsigned int freq)
{
	last_freq_hz = freq;
}

inline static unsigned int get_current_freq(void)
{
	return KHZ_TO_HZ(gpufreq_get_freq_by_idx(
		TARGET_STACK, gpufreq_get_cur_oppidx(TARGET_STACK)));
}

unsigned int ged_clk_rate_trace_get_default_freq(void)
{
	/* return the lowest frequency */
	return KHZ_TO_HZ(gpufreq_get_freq_by_idx(
		TARGET_STACK, gpufreq_get_opp_num(TARGET_STACK) - 1));
}
EXPORT_SYMBOL(ged_clk_rate_trace_get_default_freq);

int ged_clk_rate_trace_notifier_register(void *notifier_handle,
					 struct notifier_block *nb)
{
	if (!is_inited || !notifier_handle || !nb) {
		dev_err(ged_dev, "%s: fail", __func__);
		return -EINVAL;
	}

	dev_dbg(ged_dev, "@%s:", __func__);
	return srcu_notifier_chain_register(notifier_handle, nb);
}
EXPORT_SYMBOL(ged_clk_rate_trace_notifier_register);

void ged_clk_rate_trace_notifier_unregister(void *notifier_handle,
					    struct notifier_block *nb)
{
	struct srcu_notifier_head *nh = notifier_handle;

	if (!is_inited || !nh || !nb) {
		dev_err(ged_dev, "%s: fail", __func__);
		return;
	}

	dev_dbg(ged_dev, "@%s:", __func__);
	srcu_notifier_chain_unregister(nh, nb);
}
EXPORT_SYMBOL(ged_clk_rate_trace_notifier_unregister);

struct srcu_notifier_head *ged_get_notifier_handle(void)
{
	return is_inited ? &notifier_head : NULL;
}
EXPORT_SYMBOL(ged_get_notifier_handle);

int ged_clk_rate_trace_init(struct device *dev)
{
	if (!dev)
		return GED_ERROR_INVALID_PARAMS;

	if (is_inited)
		return GED_OK;

	last_freq_hz = ged_clk_rate_trace_get_default_freq();
	dev_dbg(ged_dev, "@%s: last_freq_hz:%u", __func__, last_freq_hz);
	if (!last_freq_hz)
		return GED_ERROR_FAIL;

	ged_dev = dev;
	srcu_init_notifier_head(&notifier_head);
	mutex_init(&lock);
	is_inited = true;

	dev_info(dev, "@%s: done", __func__);
	return GED_OK;
}
EXPORT_SYMBOL(ged_clk_rate_trace_init);

void ged_clk_rate_trace_exit(void)
{
	struct device *dev = ged_dev;

	if (!is_inited)
		return;

	last_freq_hz = 0;
	ged_dev = NULL;
	srcu_cleanup_notifier_head(&notifier_head);
	is_inited = false;

	dev_info(dev, "@%s: done", __func__);
}
EXPORT_SYMBOL(ged_clk_rate_trace_exit);

void ged_clk_rate_change_notify(unsigned int freq_new)
{
	void *notifier;
	unsigned int old_rate, new_rate;

	notifier = ged_get_notifier_handle();
	if (unlikely(!notifier)) {
		dev_err(ged_dev, "%s: get notifiler handle fail", __func__);
		return;
	}

	mutex_lock(&lock);

	old_rate = get_last_freq();
	if (!old_rate) {
		mutex_unlock(&lock);
		dev_err(ged_dev, "%s: get last frequency fail", __func__);
		return;
	}

	new_rate = KHZ_TO_HZ(freq_new);
	if (new_rate != old_rate) {
		struct kbase_gpu_clk_notifier_data data;
		data.gpu_clk_handle = notifier;
		data.old_rate = old_rate;
		data.new_rate = new_rate;

		srcu_notifier_call_chain(notifier, POST_RATE_CHANGE, &data);
		dev_dbg(ged_dev,
			"@%s: clk change notify: new_rate:%u, old_rate:%u",
			__func__, new_rate, old_rate);
	}

	update_last_freq(new_rate);
	mutex_unlock(&lock);
}
EXPORT_SYMBOL(ged_clk_rate_change_notify);
