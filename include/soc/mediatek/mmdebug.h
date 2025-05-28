/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2025 MediaTek Inc.
 */

#ifndef _MMDEBUG_H_
#define _MMDEBUG_H_

#if IS_ENABLED(CONFIG_MTK_MMDEBUG)
bool mmdebug_is_init_done(void);
int mtk_mmdebug_status_dump_register_notifier(struct notifier_block *nb);
#else
static inline bool mmdebug_is_init_done(void) { return false; }
static inline int mtk_mmdebug_status_dump_register_notifier(struct notifier_block *nb) { return 0; }
#endif

#endif

