/*
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2022 Intel Corporation.
 * Copyright(c) 2023 Semihalf.
 */

#include "pgtable.h"

struct pkvm_iommu_spgt {
	int refcount;
	struct hlist_node hnode;
	unsigned long root_gpa;
	unsigned long index;
	struct pkvm_pgtable pgt;
};

int pkvm_iommu_spgt_pool_init(void *mem_base, unsigned long nr_pages);
struct pkvm_pgtable *pkvm_get_iommu_spgt(unsigned long root_gpa);
void pkvm_put_iommu_spgt(struct pkvm_pgtable *spgt);
