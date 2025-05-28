/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2025 MediaTek Inc.
 * Author: Echo.Zhang <echo.zhang@mediatek.com>
 */

#ifndef __MMQOS_VCP_H__
#define __MMQOS_VCP_H__

#include "mmqos-global.h"
#include "mmqos-mtk.h"
#include "mmqos-vcp-memory.h"

#define IPI_TIMEOUT_MS        (200U)
#define VCP_TEST_NUM          (100)
#define PIN_OUT_SIZE_MMQOS    (2)

enum ipi_func_id {
	FUNC_MMQOS_INIT,
	FUNC_TEST,
	FUNC_SYNC_STATE,
	FUNC_NUM
};

struct mmqos_ipi_data {
	u8 func;
	u8 idx;
	u8 ack;
	u32 base;
};

#if IS_ENABLED(CONFIG_MTK_MMQOS_VCP)
int mmqos_vcp_init_thread(void *data);
bool mmqos_is_init_done(void);
int mmqos_vcp_ipi_send(enum ipi_func_id func, const u8 idx, u32 *data);
int mmqos_set_state_to_vcp(void);
#else
inline int mmqos_vcp_init_thread(void *data) { return -EINVAL; }
inline bool mmqos_is_init_done(void) { return false; }
inline int mmqos_vcp_ipi_send(enum ipi_func_id func, const u8 idx, u32 *data) { return -EINVAL; }
inline int mmqos_set_state_to_vcp(void){ return 0; }
#endif /* CONFIG_MTK_MMQOS_VCP*/

#endif /* MMQOS_VCP_H */
