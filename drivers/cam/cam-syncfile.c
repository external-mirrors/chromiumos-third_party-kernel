// SPDX-License-Identifier: GPL-2.0
/*
 * CAM sync file
 *
 * Copyright (C) 2022 Google LLC
 */

#define pr_fmt(fmt) "cam-syncfile: " fmt

#include <linux/cam/cam-entity.h>
#include <linux/cam/cam-graph.h>
#include <linux/cam/cam-output.h>
#include <linux/cam/cam-syncfile.h>
#include <linux/sync_file.h>
#include <linux/kernel.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <uapi/linux/cam.h>

static struct cam_obj_syncfile *__nsobj_to_cam_syncfile(struct cam_obj *nsobj,
							u32 type)
{
	if (!cam_obj_check_type(nsobj, type))
		return NULL;

	return container_of(nsobj, struct cam_obj_syncfile, nsobj);
}

int cam_out_syncfile_fd(struct cam_obj_syncfile *sf)
{
	if (!(sf->nsobj.type & CAM_OBJ_TYPE_OUT_SYNCFILE))
		return -1;

	return sf->out.fd;
}

static struct cam_obj_syncfile *nsobj_to_cam_in_syncfile(struct cam_obj *nsobj)
{
	return __nsobj_to_cam_syncfile(nsobj, CAM_OBJ_TYPE_IN_SYNCFILE);
}

static struct cam_obj_syncfile *nsobj_to_cam_out_syncfile(struct cam_obj *nsobj)
{
	return __nsobj_to_cam_syncfile(nsobj, CAM_OBJ_TYPE_OUT_SYNCFILE);
}

void cam_in_syncfile_unregister(struct cam_obj *nsobj)
{
	struct cam_obj_syncfile *sf;

	sf = nsobj_to_cam_in_syncfile(nsobj);
	if (WARN_ON(!sf))
		return;

	cam_obj_remove(&sf->nsobj);
	cam_obj_deinit(&sf->nsobj);
}

void cam_out_syncfile_unregister(struct cam_obj *nsobj)
{
	struct cam_obj_syncfile *sf;

	sf = nsobj_to_cam_out_syncfile(nsobj);
	if (WARN_ON(!sf))
		return;

	cam_obj_unlink(nsobj);
	cam_obj_remove(nsobj);
	/*
	 * Exported DMA fence can be imported many times so we need to depend
	 * on DMA fence ref-counter and cannot release CAM syncfile object,
	 * because DMA fence uses context_lock which it borrows from CAM
	 * syncfile.
	 *
	 * We take a different approach here: we put DMA fence and release
	 * CAM syncfile object only from DAM fence ->release callback.
	 */
	dma_fence_put(&sf->out.fence);
}

void cam_syncfile_put(struct cam_obj_syncfile *sf)
{
	if (likely(sf))
		cam_obj_put(&sf->nsobj);
	else
		WARN_ON(1);
}

static void cam_in_syncfile_release(struct cam_obj *nsobj)
{
	struct cam_obj_syncfile *sf = nsobj_to_cam_in_syncfile(nsobj);

	if (sf->in.fence)
		dma_fence_remove_callback(sf->in.fence, &sf->in.cb);
	dma_fence_put(sf->in.fence);
	cam_obj_unlink(nsobj);
	kfree(sf);
}

static void cam_syncfile_fence_cb(struct dma_fence *f, struct dma_fence_cb *cb)
{
	struct cam_obj_syncfile *sf;
	unsigned long flags;

	sf = container_of(cb, struct cam_obj_syncfile, in.cb);

	write_lock_irqsave(&sf->in.notify_lock, flags);
	cam_fire_active_signals(&sf->in.notify_active_chain);
	write_unlock_irqrestore(&sf->in.notify_lock, flags);
}

__printf(4, 5)
struct cam_obj_syncfile *cam_in_syncfile_register(struct cam_device *cam,
						  struct cam_obj_op *op,
						  int fd,
						  const char *namefmt,
						  ...)
{
	char name[CAM_SYNCFILE_NAME_SZ];
	struct cam_obj_syncfile *sf;
	va_list args;
	int ret;

	sf = kzalloc(sizeof(*sf), GFP_KERNEL);
	if (!sf)
		return NULL;

	va_start(args, namefmt);
	vsnprintf(name, sizeof(name), namefmt, args);
	va_end(args);

	INIT_LIST_HEAD(&sf->in.notify_active_chain);
	rwlock_init(&sf->in.notify_lock);
	strlcpy(sf->name, name, CAM_SYNCFILE_NAME_SZ);
	cam_obj_init(&sf->nsobj,
		     CAM_OBJ_TYPE_IN_SYNCFILE,
		     cam_in_syncfile_release,
		     &cam->ns);

	sf->in.fence = sync_file_get_fence(fd);
	if (!sf->in.fence)
		goto error;

	ret = dma_fence_add_callback(sf->in.fence,
				     &sf->in.cb,
				     cam_syncfile_fence_cb);
	/* -ENOENT is returned when fence is already signaled */
	if (ret && ret != -ENOENT)
		goto error;

	if (cam_obj_link(&sf->nsobj, &op->nsobj))
		goto error;

	if (cam_obj_insert(&sf->nsobj))
		goto error;

	return sf;

error:
	cam_in_syncfile_release(&sf->nsobj);
	return NULL;
}
ALLOW_ERROR_INJECTION(cam_in_syncfile_register, NULL);

bool cam_in_syncfile_activate_signal(struct cam_op_signal *sig)
{
	struct cam_obj_syncfile *sf;
	unsigned long flags;

	sf = nsobj_to_cam_in_syncfile(sig->source);
	if (WARN_ON(!sf))
		return false;

	if (dma_fence_is_signaled(sf->in.fence))
		return false;

	/*
	 * notify_active_chain is accessed from the IRQ context,
	 * so we need to disable local IRQs.
	 */
	write_lock_irqsave(&sf->in.notify_lock, flags);
	list_add_tail(&sig->entry, &sf->in.notify_active_chain);
	write_unlock_irqrestore(&sf->in.notify_lock, flags);
	return true;
}

void cam_drain_in_syncfile(struct cam_obj *nsobj)
{
	struct cam_obj_syncfile *sf;
	unsigned long flags;

	sf = nsobj_to_cam_in_syncfile(nsobj);
	if (WARN_ON(!sf))
		return;

	/* Make sure we don't get signals */
	dma_fence_remove_callback(sf->in.fence, &sf->in.cb);

	write_lock_irqsave(&sf->in.notify_lock, flags);
	cam_drain_active_signals(&sf->in.notify_active_chain, NULL);
	write_unlock_irqrestore(&sf->in.notify_lock, flags);
}

static void cam_out_syncfile_release(struct cam_obj *nsobj)
{
	struct cam_obj_syncfile *sf;

	sf = nsobj_to_cam_out_syncfile(nsobj);
	if (WARN_ON(!sf))
		return;

	cam_obj_unlink(nsobj);
	kfree(sf);
}

static void cam_dma_fence_release(struct dma_fence *fence)
{
	struct cam_obj_syncfile *sf;

	sf = container_of(fence, struct cam_obj_syncfile, out.fence);
	cam_obj_deinit(&sf->nsobj);
}

static const char *cam_dma_fence_driver_name(struct dma_fence *fence)
{
	return "kcam";
}

static const char *cam_dma_fence_timeline_name(struct dma_fence *fence)
{
	return "kcam-timeline";
}

static struct dma_fence_ops cam_out_fence_ops = {
	.get_driver_name	= cam_dma_fence_driver_name,
	.get_timeline_name	= cam_dma_fence_timeline_name,
	.release		= cam_dma_fence_release,
};

__printf(3, 4)
struct cam_obj_syncfile *cam_out_syncfile_register(struct cam_device *cam,
						   struct cam_obj_op *op,
						   const char *namefmt,
						   ...)
{
	struct sync_file *syncfile = NULL;
	char name[CAM_SYNCFILE_NAME_SZ];
	struct cam_obj_syncfile *sf;
	va_list args;

	sf = kzalloc(sizeof(*sf), GFP_KERNEL);
	if (!sf)
		return NULL;

	va_start(args, namefmt);
	vsnprintf(name, sizeof(name), namefmt, args);
	va_end(args);

	spin_lock_init(&sf->out.context_lock);
	atomic64_set(&sf->out.fence_seqno, 0);
	sf->out.fd = -1;
	strlcpy(sf->name, name, CAM_SYNCFILE_NAME_SZ);
	cam_obj_init(&sf->nsobj,
		     CAM_OBJ_TYPE_OUT_SYNCFILE,
		     cam_out_syncfile_release,
		     &cam->ns);

	sf->out.fence_context = dma_fence_context_alloc(1);
	dma_fence_init(&sf->out.fence,
		       &cam_out_fence_ops,
		       &sf->out.context_lock,
		       sf->out.fence_context,
		       atomic64_inc_return(&sf->out.fence_seqno));

	sf->out.fd = get_unused_fd_flags(O_CLOEXEC);
	if (sf->out.fd < 0)
		goto error;

	syncfile = sync_file_create(&sf->out.fence);
	if (!syncfile)
		goto error;

	if (cam_obj_link(&sf->nsobj, &op->nsobj))
		goto error;

	if (cam_obj_insert(&sf->nsobj))
		goto error;

	fd_install(sf->out.fd, syncfile->file);
	return sf;

error:
	if (sf->out.fd > 0)
		put_unused_fd(sf->out.fd);
	if (syncfile)
		fput(syncfile->file);
	cam_out_syncfile_release(&sf->nsobj);
	return NULL;
}
ALLOW_ERROR_INJECTION(cam_out_syncfile_register, NULL);

int cam_fire_out_syncfile_signal(struct cam_obj *nsobj)
{
	struct cam_obj_syncfile *sf;

	sf = nsobj_to_cam_out_syncfile(nsobj);
	if (WARN_ON(!sf))
		return -EINVAL;

	return dma_fence_signal(&sf->out.fence);
}
