/*
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2022 Intel Corporation.
 * Copyright(c) 2023 Semihalf.
 */

#include <linux/hashtable.h>
#include <pkvm_spinlock.h>
#include <pkvm.h>
#include <gfp.h>
#include "pkvm_hyp.h"
#include "iommu_spgt.h"
#include "memory.h"
#include "ept.h"
#include "bug.h"

static DEFINE_HASHTABLE(iommu_spgt_hasht, 8);
static DECLARE_BITMAP(iommu_spgt_bitmap, PKVM_MAX_PDEV_NUM);
static struct pkvm_iommu_spgt pkvm_iommu_spgt[PKVM_MAX_PDEV_NUM];
static pkvm_spinlock_t iommu_spgt_lock = { __ARCH_PKVM_SPINLOCK_UNLOCKED };

static struct pkvm_pool iommu_spgt_pool;

static void *iommu_spgt_zalloc_page(void)
{
	return ept_zalloc_page(&iommu_spgt_pool);
}

static void iommu_spgt_get_page(void *vaddr)
{
	pkvm_get_page(&iommu_spgt_pool, vaddr);
}

static void iommu_spgt_put_page(void *vaddr)
{
	pkvm_put_page(&iommu_spgt_pool, vaddr);
}

static void iommu_spgt_flush_cache(void *vaddr, unsigned int size)
{
	if (!pkvm_hyp->iommu_coherent)
		pkvm_clflush_cache_range(vaddr, size);
}

static void flush_tlb_noop(struct pkvm_pgtable *pgt) { };

static struct pkvm_mm_ops iommu_spgt_mm_ops = {
	.phys_to_virt = pkvm_phys_to_virt,
	.virt_to_phys = pkvm_virt_to_phys,
	.zalloc_page = iommu_spgt_zalloc_page,
	.get_page = iommu_spgt_get_page,
	.put_page = iommu_spgt_put_page,
	.page_count = pkvm_page_count,
	.flush_tlb = flush_tlb_noop,
	.flush_cache = iommu_spgt_flush_cache,
};

int pkvm_iommu_spgt_pool_init(void *mem_base, unsigned long nr_pages)
{
	unsigned long pfn = __pkvm_pa(mem_base) >> PAGE_SHIFT;

	return pkvm_pool_init(&iommu_spgt_pool, pfn, nr_pages, 0);
}

struct pkvm_pgtable *pkvm_get_iommu_spgt(unsigned long root_gpa)
{
	struct pkvm_iommu_spgt *spgt = NULL, *tmp;
	unsigned long index;
	int ret;

	pkvm_spin_lock(&iommu_spgt_lock);

	hash_for_each_possible(iommu_spgt_hasht, tmp, hnode, root_gpa) {
		if (tmp->root_gpa == root_gpa) {
			if (tmp->refcount > 0) {
				tmp->refcount++;
				spgt = tmp;
				break;
			}
		}
	}

	if (spgt)
		goto out;

	index = find_first_zero_bit(iommu_spgt_bitmap, PKVM_MAX_PDEV_NUM);
	if (index < PKVM_MAX_PDEV_NUM) {
		spgt = &pkvm_iommu_spgt[index];

		ret = pkvm_pgtable_init(&spgt->pgt, &iommu_spgt_mm_ops, &ept_ops,
					&pkvm_hyp->ept_cap, true);
		if (ret) {
			pkvm_err("%s: pgtable init failed err=%d\n", __func__, ret);
			spgt = NULL;
			goto out;
		}

		__set_bit(index, iommu_spgt_bitmap);
		spgt->root_gpa = root_gpa;
		spgt->index = index;
		spgt->refcount = 1;
		hash_add(iommu_spgt_hasht, &spgt->hnode, root_gpa);
	}
out:
	pkvm_spin_unlock(&iommu_spgt_lock);

	return spgt ? &spgt->pgt : NULL;
}

void pkvm_put_iommu_spgt(struct pkvm_pgtable *pgt)
{
	struct pkvm_iommu_spgt *spgt = NULL, *tmp;
	int bkt;

	pkvm_spin_lock(&iommu_spgt_lock);

	hash_for_each(iommu_spgt_hasht, bkt, tmp, hnode) {
		if (&tmp->pgt == pgt) {
			spgt = tmp;
			break;
		}
	}
	PKVM_ASSERT(spgt);
	PKVM_ASSERT(spgt->refcount > 0);

	if (--spgt->refcount > 0)
		goto out;

	hash_del(&spgt->hnode);

	__clear_bit(spgt->index, iommu_spgt_bitmap);

	pkvm_pgtable_destroy(&spgt->pgt, NULL);

	memset(spgt, 0, sizeof(struct pkvm_iommu_spgt));

out:
	pkvm_spin_unlock(&iommu_spgt_lock);
}
