/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2025 MediaTek Inc.
 */
#ifndef __DVFSRC_EXP_H
#define __DVFSRC_EXP_H

#if IS_ENABLED(CONFIG_MTK_DVFSRC)
u32 dvfsrc_get_required_opp_peak_bw(struct device_node *np, int index);
#else
static inline u32 dvfsrc_get_required_opp_peak_bw(struct device_node *np,
						  int index)
{ return 0; }
#endif /* CONFIG_MTK_DVFSRC */

#endif
