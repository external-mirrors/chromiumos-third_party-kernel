/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2020 MediaTek Inc.
 */
#ifndef __MCUPM_DEFINE_H__
#define __MCUPM_DEFINE_H__

#include <linux/types.h>

/* MCUPM MBOX */
#define MCUPM_MBOX_SLOT_SIZE		0x4

#define MCUPM_LOGGER_SUPPORT		(1)
#define MCUPM_COREDUMP_SUPPORT		(1)
#define MCUPM_ALIVE_THREAD		(0)

#define MCUPM_MBOX_NO_SUSPEND		4

#define MCUPM_MBOX_OFFSET_PDN		10 /* offset: 40 bytes */
#define MCUPM_MBOX_SIZE_PDN		1  /* slot size: 4 bytes */

#define MCUPM_MBOX_OFFSET_STATE		(MCUPM_MBOX_OFFSET_PDN + \
					MCUPM_MBOX_SIZE_PDN)
#define MCUPM_MBOX_SIZE_STATE		1  /* slot size: 4 bytes */

#define MCUPM_MBOX_OFFSET_TIMESTAMP	(MCUPM_MBOX_OFFSET_STATE + \
					MCUPM_MBOX_SIZE_STATE)

#define MCUPM_PLT_INIT			0x504C5401
#define MCUPM_PLT_LOG_ENABLE		0x504C5402
#define MCUPM_PLT_SERV_READY		0x504C5403
#define MCUPM_POWER_DOWN		0x4D50444E

/* MCUPM IPI define */
struct mcupm_ipi_data_s {
	unsigned int cmd;
	union {
		struct {
			unsigned int phys;
			unsigned int size;
		} ctrl;
		struct {
			unsigned int enable;
		} logger;
		struct {
			unsigned int mode;
		} ts;
	} u;
};

int mcupm_mbox_write(unsigned int mbox, unsigned int slot, void *buf,
				unsigned int len);
int mcupm_mbox_read(unsigned int mbox, unsigned int slot, void *buf,
			unsigned int len);
#endif
