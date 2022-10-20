// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2017-2022 Intel Corporation

#include <drm/drm_device.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_file.h>

#include <linux/workqueue.h>

#include <uapi/drm/gna_drm.h>

#include "gna_device.h"
#include "gna_gem.h"
#include "gna_request.h"

int gna_score_ioctl(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	union gna_compute *score_args = data;
	u64 request_id;
	int ret;

	ret = gna_validate_score_config(&score_args->in.config, to_gna_device(dev));
	if (ret)
		return ret;

	ret = gna_enqueue_request(&score_args->in.config, file, &request_id);
	if (ret)
		return ret;

	score_args->out.request_id = request_id;

	return 0;
}

int gna_gem_free_ioctl(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	struct gna_device *gna_priv = to_gna_device(dev);
	struct gna_gem_free *args = data;
	struct gna_gem_object *gnagemo;
	struct drm_gem_object *drmgemo;
	int ret;

	drmgemo = drm_gem_object_lookup(file, args->handle);
	if (!drmgemo)
		return -ENOENT;

	gnagemo = to_gna_gem_obj(to_drm_gem_shmem_obj(drmgemo));

	queue_work(gna_priv->request_wq, &gnagemo->work);
	cancel_work_sync(&gnagemo->work);

	ret = drm_gem_handle_delete(file, args->handle);

	drm_gem_object_put(drmgemo);
	return ret;
}

int gna_getparam_ioctl(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	struct gna_device *gna_priv = to_gna_device(dev);
	union gna_parameter *param = data;

	return gna_getparam(gna_priv, param);
}

static struct drm_gem_shmem_object *
drm_gem_shmem_create_with_handle(struct drm_file *file_priv,
				struct drm_device *dev, size_t size,
				uint32_t *handle)
{
	struct drm_gem_shmem_object *shmem;
	int ret;

	shmem = drm_gem_shmem_create(dev, size);
	if (IS_ERR(shmem))
		return shmem;

	/*
	 * Allocate an id of idr table where the obj is registered
	 * and handle has the id what user can see.
	 */
	ret = drm_gem_handle_create(file_priv, &shmem->base, handle);
	/* drop reference from allocate - handle holds it now. */
	drm_gem_object_put(&shmem->base);
	if (ret)
		return ERR_PTR(ret);

	return shmem;
}

int gna_gem_new_ioctl(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	struct drm_gem_shmem_object *drmgemshm;
	struct gna_gem_object *gnagemo;
	union gna_gem_new *args = data;

	drmgemshm = drm_gem_shmem_create_with_handle(file, dev, args->in.size,
						&args->out.handle);

	if (IS_ERR(drmgemshm))
		return PTR_ERR(drmgemshm);

	args->out.size_granted = drmgemshm->base.size;
	args->out.vma_fake_offset = drm_vma_node_offset_addr(&drmgemshm->base.vma_node);

	gnagemo = to_gna_gem_obj(drmgemshm);
	gnagemo->handle = args->out.handle;

	INIT_WORK(&gnagemo->work, gna_gem_obj_release_work);
	return 0;
}
