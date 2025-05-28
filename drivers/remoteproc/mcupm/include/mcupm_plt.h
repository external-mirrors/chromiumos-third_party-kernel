/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2020 MediaTek Inc.
 */
#ifndef __MCUPM_PLT_H__
#define __MCUPM_PLT_H__
#include <linux/device.h>

/* import from mcupm_logger */
int mcupm_plt_module_init(struct device *dev);
void mcupm_plt_module_exit(struct device *dev);
#endif
