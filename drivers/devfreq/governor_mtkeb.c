// SPDX-License-Identifier: GPL-2.0
/*
 * linux/drivers/devfreq/governor_mtkeb.c
 *
 * Copyright 2025 Google LLC.
 *
 * The mtkeb governor always returns DEVFREQ_MAX_FREQ just like the
 * performance governor. The device is expected to control the
 * frequency by itself, while set the target frequency as an upper limit
 * to account for software throttling.
 * This governor also starts the devfreq monitor to update the device
 * status for throttling purpose.
 */

#include <linux/devfreq.h>
#include <linux/module.h>
#include "governor.h"

static int devfreq_mtkeb_func(struct devfreq *df,
				    unsigned long *freq)
{
	int ret;

	ret = devfreq_update_stats(df);
	if (ret)
		return ret;

	*freq = DEVFREQ_MAX_FREQ;

	return 0;
}

static int devfreq_mtkeb_handler(struct devfreq *devfreq,
					unsigned int event, void *data)
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


static struct devfreq_governor devfreq_mtkeb = {
	.name = DEVFREQ_GOV_MTKEB,
	.get_target_freq = devfreq_mtkeb_func,
	.event_handler = devfreq_mtkeb_handler,
};

static int __init devfreq_mtkeb_init(void)
{
	return devfreq_add_governor(&devfreq_mtkeb);
}
subsys_initcall(devfreq_mtkeb_init);

static void __exit devfreq_mtkeb_exit(void)
{
	int ret;

	ret = devfreq_remove_governor(&devfreq_mtkeb);
	if (ret)
		pr_err("%s: failed remove governor %d\n", __func__, ret);

	return;
}
module_exit(devfreq_mtkeb_exit);
MODULE_LICENSE("GPL");
