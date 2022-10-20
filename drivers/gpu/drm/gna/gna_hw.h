/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2017-2022 Intel Corporation */

#ifndef __GNA_HW_H__
#define __GNA_HW_H__

#include <linux/mm_types.h>

#define GNA_FEATURES						\
	.max_hw_mem = 256 * 1024 * 1024,			\
		.num_pagetables = 64,				\
		.num_page_entries = PAGE_SIZE / sizeof(u32),	\
		/* desc_info all in bytes */			\
		.desc_info = {					\
		.rsvd_size = 256,				\
		.cfg_size = 256,				\
		.desc_size = 784,				\
		.mmu_info = {					\
			.vamax_size = 4,			\
			.rsvd_size = 12,			\
			.pd_size = 4 * 64,			\
		},						\
	}

#define GNA_GEN1_FEATURES			\
	GNA_FEATURES,				\
		.max_layer_count = 1024

#define GNA_GEN2_FEATURES			\
	GNA_FEATURES,				\
		.max_layer_count = 4096

#define GNA_DEV_HWID_CNL	0x5A11
#define GNA_DEV_HWID_EHL	0x4511
#define GNA_DEV_HWID_GLK	0x3190
#define GNA_DEV_HWID_ICL	0x8A11
#define GNA_DEV_HWID_JSL	0x4E11
#define GNA_DEV_HWID_TGL	0x9A11
#define GNA_DEV_HWID_RKL	0x4C11
#define GNA_DEV_HWID_ADL	0x464F
#define GNA_DEV_HWID_RPL	0xA74F
#define GNA_DEV_HWID_MTL	0x7E4C

/* GNA MMIO registers */
#define GNA_MMIO_IBUFFS		0xB4

#define GNA_PGDIRN_LEN			64
#define GNA_PGDIR_INVALID		1

struct gna_mmu_info {
	u32 vamax_size;
	u32 rsvd_size;
	u32 pd_size;
};

struct gna_desc_info {
	u32 rsvd_size;
	u32 cfg_size;
	u32 desc_size;
	struct gna_mmu_info mmu_info;
};

struct gna_hw_info {
	u8 in_buf_s;
};

struct gna_dev_info {
	u32 hwid;
	u32 num_pagetables;
	u32 num_page_entries;
	u32 max_layer_count;
	u64 max_hw_mem;

	struct gna_desc_info desc_info;
};

#endif // __GNA_HW_H__
