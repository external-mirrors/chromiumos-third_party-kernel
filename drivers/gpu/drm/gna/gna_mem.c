// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2017-2022 Intel Corporation

#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_managed.h>

#include <linux/atomic.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/math.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>

#include "gna_device.h"
#include "gna_gem.h"
#include "gna_mem.h"
#include "gna_request.h"

static void gna_mmu_set(struct gna_device *gna_priv)
{
	struct gna_mmu_object *mmu;
	dma_addr_t pagetable_dma;
	u32 *pgdirn;
	int i;

	mmu = &gna_priv->mmu;

	pgdirn = mmu->hwdesc->mmu.pagedir_n;

	for (i = 0; i < mmu->num_pagetables; i++) {
		pagetable_dma = mmu->pagetables_dma[i];
		pgdirn[i] = pagetable_dma >> PAGE_SHIFT;
	}

	for (; i < GNA_PGDIRN_LEN; i++)
		pgdirn[i] = GNA_PGDIR_INVALID;
}

/* descriptor and page tables allocation */
int gna_mmu_init(struct gna_device *gna_priv)
{
	struct device *parent = gna_dev(gna_priv);
	struct gna_mmu_object *mmu;
	int desc_size;
	int i;

	if (gna_priv->info.num_pagetables > GNA_PGDIRN_LEN) {
		dev_dbg(gna_dev(gna_priv), "number of pagetables requested too large: %u\n", gna_priv->info.num_pagetables);
		return -EINVAL;
	}

	mmu = &gna_priv->mmu;

	desc_size = round_up(gna_priv->info.desc_info.desc_size, PAGE_SIZE);

	mmu->hwdesc = dmam_alloc_coherent(parent, desc_size, &mmu->hwdesc_dma,
					GFP_KERNEL);
	if (!mmu->hwdesc)
		return -ENOMEM;

	mmu->num_pagetables = gna_priv->info.num_pagetables;

	mmu->pagetables_dma = drmm_kmalloc_array(&gna_priv->drm, mmu->num_pagetables, sizeof(*mmu->pagetables_dma),
						GFP_KERNEL);
	if (!mmu->pagetables_dma)
		return -ENOMEM;

	mmu->pagetables = drmm_kmalloc_array(&gna_priv->drm, mmu->num_pagetables, sizeof(*mmu->pagetables), GFP_KERNEL);

	if (!mmu->pagetables)
		return -ENOMEM;

	for (i = 0; i < mmu->num_pagetables; i++) {
		mmu->pagetables[i] = dmam_alloc_coherent(parent, PAGE_SIZE,
							&mmu->pagetables_dma[i], GFP_KERNEL);
		if (!mmu->pagetables[i])
			return -ENOMEM;
	}

	gna_mmu_set(gna_priv);

	return 0;
}

static void gna_delete_score_requests(u32 handle, struct gna_device *gna_priv)
{
	struct gna_request *req, *temp_req;
	struct list_head *reqs_list;
	int i;

	mutex_lock(&gna_priv->reqlist_lock);

	reqs_list = &gna_priv->request_list;
	if (!list_empty(reqs_list)) {
		list_for_each_entry_safe(req, temp_req, reqs_list, node) {
			for (i = 0; i < req->buffer_count; ++i) {
				if (req->buffer_list[i].gna.handle == handle) {
					list_del_init(&req->node);
					cancel_work_sync(&req->work);
					atomic_dec(&gna_priv->enqueued_requests);
					kref_put(&req->refcount, gna_request_release);
					break;
				}
			}
		}
	}

	mutex_unlock(&gna_priv->reqlist_lock);
}

void gna_gem_obj_release_work(struct work_struct *work)
{
	struct gna_gem_object *gnagemo;

	gnagemo = container_of(work, struct gna_gem_object, work);

	gna_delete_score_requests(gnagemo->handle, to_gna_device(gnagemo->base.base.dev));
}
