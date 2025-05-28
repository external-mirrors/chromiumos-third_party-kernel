/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 MediaTek Inc.
 */

#ifndef __GED_CLK_RATE_TRACE_H__
#define __GED_CLK_RATE_TRACE_H__

#include "linux/notifier.h"

int ged_clk_rate_trace_init(struct device *dev);
void ged_clk_rate_trace_exit(void);
int ged_clk_rate_trace_notifier_register(void *notifier_handle,
					 struct notifier_block *nb);
void ged_clk_rate_trace_notifier_unregister(void *notifier_handle,
					    struct notifier_block *nb);
unsigned int ged_clk_rate_trace_get_default_freq(void);
struct srcu_notifier_head *ged_get_notifier_handle(void);
void ged_clk_rate_change_notify(unsigned int freq_new);

#endif
