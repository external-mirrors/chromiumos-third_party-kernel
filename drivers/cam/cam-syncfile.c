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

void cam_syncfile_unregister(struct cam_obj_syncfile *sf)
{
	cam_obj_remove(&sf->nsobj);
	cam_obj_deinit(&sf->nsobj);
}

void cam_syncfile_put(struct cam_obj_syncfile *sf)
{
	if (likely(sf))
		cam_obj_put(&sf->nsobj);
	else
		WARN_ON(1);
}

static struct cam_obj_syncfile *nsobj_to_cam_in_syncfile(struct cam_obj *nsobj)
{
	return __nsobj_to_cam_syncfile(nsobj, CAM_OBJ_TYPE_IN_SYNCFILE);
}

static void cam_in_syncfile_release(struct cam_obj *nsobj)
{
	struct cam_obj_syncfile *sf = nsobj_to_cam_in_syncfile(nsobj);

	if (sf->in.fence)
		dma_fence_remove_callback(sf->in.fence, &sf->in.cb);
	dma_fence_put(sf->in.fence);
	cam_graph_node_unlink(nsobj);
	kfree(sf);
}

static void cam_syncfile_fence_cb(struct dma_fence *f, struct dma_fence_cb *cb)
{
	struct cam_obj_syncfile *sf;

	sf = container_of(cb, struct cam_obj_syncfile, in.cb);
	cam_in_syncfile_trigger_signals(sf);
}

__printf(3, 4)
struct cam_obj_syncfile *cam_in_syncfile_register(struct cam_device *cam,
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
	if (ret && ret != -ENOENT) {
		goto error;
	}

	if (cam_graph_node_link(cam, &sf->nsobj, CAM_OBJ_ID_ROOT))
		goto error;

	if (cam_obj_insert(&sf->nsobj))
		goto error;

	return sf;

error:
	cam_in_syncfile_release(&sf->nsobj);
	return NULL;
}
ALLOW_ERROR_INJECTION(cam_in_syncfile_register, NULL);

/**
 * cam_in_syncfile_trigger_signals() - Trigger active syncfile signals
 * @sf: pointer to CAM syncfile
 */
void cam_in_syncfile_trigger_signals(struct cam_obj_syncfile *sf)
{
	unsigned long flags;

	write_lock_irqsave(&sf->in.notify_lock, flags);
	cam_fire_active_signals(&sf->in.notify_active_chain);
	write_unlock_irqrestore(&sf->in.notify_lock, flags);
}

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

static void cam_drain_in_syncfile_callback(struct cam_obj *nsobj,
					   struct cam_ns_walk_control *ctl)
{
	struct cam_obj_syncfile *sf;
	unsigned long flags;

	if (!(nsobj->type & CAM_OBJ_TYPE_IN_SYNCFILE))
		return;

	sf = nsobj_to_cam_in_syncfile(nsobj);
	if (WARN_ON(!sf))
		return;

	write_lock_irqsave(&sf->in.notify_lock, flags);
	cam_drain_active_signals(&sf->in.notify_active_chain);
	write_unlock_irqrestore(&sf->in.notify_lock, flags);
}


int cam_drain_in_syncfiles(struct cam_device *cam)
{
	struct cam_ns_walk_control ctl = {};

	ctl.cb		= cam_drain_in_syncfile_callback;
	cam_ns_for_each(&cam->ns, &ctl);
	return 0;
}

static struct cam_obj_syncfile *nsobj_to_cam_out_syncfile(struct cam_obj *nsobj)
{
	return __nsobj_to_cam_syncfile(nsobj, CAM_OBJ_TYPE_OUT_SYNCFILE);
}

static void cam_out_syncfile_release(struct cam_obj *nsobj)
{
	struct cam_obj_syncfile *sf = nsobj_to_cam_out_syncfile(nsobj);

	dma_fence_put(sf->out.fence);
	cam_graph_node_unlink(nsobj);
	kfree(sf);
}

static const char *cam_driver_name(struct dma_fence *fence)
{
	return "kcam";
}

static const char *cam_timeline_name(struct dma_fence *fence)
{
	return "kcam-timeline";
}

static struct dma_fence_ops cam_out_fence_ops = {
	.get_driver_name        = cam_driver_name,
	.get_timeline_name      = cam_timeline_name,
};

__printf(2, 3)
struct cam_obj_syncfile *cam_out_syncfile_register(struct cam_device *cam,
						   const char *namefmt,
						   ...)
{
	char name[CAM_SYNCFILE_NAME_SZ];
	struct cam_obj_syncfile *sf;
	struct sync_file *syncfile = NULL;
	va_list args;

	sf = kzalloc(sizeof(*sf), GFP_KERNEL);
	if (!sf)
		return NULL;

	va_start(args, namefmt);
	vsnprintf(name, sizeof(name), namefmt, args);
	va_end(args);

	sf->out.fd = -1;
	strlcpy(sf->name, name, CAM_SYNCFILE_NAME_SZ);
	cam_obj_init(&sf->nsobj,
		     CAM_OBJ_TYPE_OUT_SYNCFILE,
		     cam_out_syncfile_release,
		     &cam->ns);

	sf->out.fence = kzalloc(sizeof(struct dma_fence), GFP_KERNEL);
	if (!sf->out.fence)
		goto error;

	dma_fence_init(sf->out.fence,
		       &cam_out_fence_ops,
		       &cam->context_lock,
		       cam->fence_context,
		       atomic64_inc_return(&cam->fence_seqno));

	sf->out.fd = get_unused_fd_flags(O_CLOEXEC);
	if (sf->out.fd < 0)
		goto error;

	syncfile = sync_file_create(sf->out.fence);
	if (!syncfile)
		goto error;

	if (cam_graph_node_link(cam, &sf->nsobj, CAM_OBJ_ID_ROOT))
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

int cam_out_syncfile_signal(struct cam_obj_syncfile *sf)
{
	return dma_fence_signal(sf->out.fence);
}
