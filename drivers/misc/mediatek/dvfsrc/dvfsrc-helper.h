/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2025 MediaTek Inc.
 */

#ifndef __DVFSRC_DEBUG_H
#define __DVFSRC_DEBUG_H

#include <soc/mediatek/dvfsrc-exp.h>

struct mtk_dvfsrc {
	struct device *dev;
	struct icc_path *bw_path;
	struct icc_path *perf_path;
	struct icc_path *hrt_path;
	int num_perf;
	u32 *perfs_peak_bw;
};

extern int dvfsrc_register_sysfs(struct device *dev);
extern void dvfsrc_unregister_sysfs(struct device *dev);
#endif
