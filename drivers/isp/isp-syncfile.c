// SPDX-License-Identifier: GPL-2.0
/*
 * ISP sync file
 *
 * Copyright (C) Google LLC
 */

#define pr_fmt(fmt) "isp-syncfile: " fmt

#include <linux/isp/isp-entity.h>
#include <linux/isp/isp-graph.h>
#include <linux/isp/isp-output.h>
#include <linux/isp/isp-syncfile.h>
#include <linux/sync_file.h>
#include <linux/kernel.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <uapi/linux/isp.h>

static struct isp_obj_syncfile *__nsobj_to_isp_syncfile(struct isp_obj *nsobj,
							u32 type)
{
	if (!isp_obj_check_type(nsobj, type))
		return NULL;

	return container_of(nsobj, struct isp_obj_syncfile, nsobj);
}

int isp_out_syncfile_fd(struct isp_obj_syncfile *sf)
{
	if (!(sf->nsobj.type & ISP_OBJ_TYPE_OUT_SYNCFILE))
		return -1;

	return sf->out.fd;
}

static struct isp_obj_syncfile *nsobj_to_isp_in_syncfile(struct isp_obj *nsobj)
{
	return __nsobj_to_isp_syncfile(nsobj, ISP_OBJ_TYPE_IN_SYNCFILE);
}

static struct isp_obj_syncfile *nsobj_to_isp_out_syncfile(struct isp_obj *nsobj)
{
	return __nsobj_to_isp_syncfile(nsobj, ISP_OBJ_TYPE_OUT_SYNCFILE);
}

void isp_in_syncfile_unregister(struct isp_obj *nsobj)
{
	struct isp_obj_syncfile *sf;

	sf = nsobj_to_isp_in_syncfile(nsobj);
	if (WARN_ON(!sf))
		return;

	isp_obj_remove(&sf->nsobj);
	isp_obj_deinit(&sf->nsobj);
}

void isp_out_syncfile_unregister(struct isp_obj *nsobj)
{
	struct isp_obj_syncfile *sf;

	sf = nsobj_to_isp_out_syncfile(nsobj);
	if (WARN_ON(!sf))
		return;

	isp_obj_unlink(nsobj);
	isp_obj_remove(nsobj);
	/*
	 * Exported DMA fence can be imported many times so we need to depend
	 * on DMA fence ref-counter and cannot release ISP syncfile object,
	 * because DMA fence uses context_lock which it borrows from ISP
	 * syncfile.
	 *
	 * We take a different approach here: we put DMA fence and release
	 * ISP syncfile object only from DAM fence ->release callback.
	 */
	dma_fence_put(&sf->out.fence);
}

void isp_syncfile_put(struct isp_obj_syncfile *sf)
{
	if (likely(sf))
		isp_obj_put(&sf->nsobj);
	else
		WARN_ON(1);
}

static void isp_in_syncfile_release(struct isp_obj *nsobj)
{
	struct isp_obj_syncfile *sf = nsobj_to_isp_in_syncfile(nsobj);

	if (sf->in.fence)
		dma_fence_remove_callback(sf->in.fence, &sf->in.cb);
	dma_fence_put(sf->in.fence);
	isp_obj_unlink(nsobj);
	kfree(sf);
}

static void isp_syncfile_fence_cb(struct dma_fence *f, struct dma_fence_cb *cb)
{
	struct isp_obj_syncfile *sf;
	unsigned long flags;

	sf = container_of(cb, struct isp_obj_syncfile, in.cb);

	write_lock_irqsave(&sf->in.notify_lock, flags);
	isp_fire_active_signals(&sf->in.notify_active_chain);
	write_unlock_irqrestore(&sf->in.notify_lock, flags);
}

__printf(4, 5)
struct isp_obj_syncfile *isp_in_syncfile_register(struct isp_device *isp,
						  struct isp_obj_op *op,
						  int fd,
						  const char *namefmt,
						  ...)
{
	char name[ISP_SYNCFILE_NAME_SZ];
	struct isp_obj_syncfile *sf;
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
	strscpy(sf->name, name, ISP_SYNCFILE_NAME_SZ);
	isp_obj_init(&sf->nsobj,
		     ISP_OBJ_TYPE_IN_SYNCFILE,
		     isp_in_syncfile_release,
		     &isp->ns);

	sf->in.fence = sync_file_get_fence(fd);
	if (!sf->in.fence)
		goto error;

	ret = dma_fence_add_callback(sf->in.fence,
				     &sf->in.cb,
				     isp_syncfile_fence_cb);
	/* -ENOENT is returned when fence is already signaled */
	if (ret && ret != -ENOENT)
		goto error;

	if (isp_obj_link(&sf->nsobj, &op->nsobj))
		goto error;

	if (isp_obj_insert(&sf->nsobj))
		goto error;

	return sf;

error:
	isp_in_syncfile_release(&sf->nsobj);
	return NULL;
}
ALLOW_ERROR_INJECTION(isp_in_syncfile_register, NULL);

bool isp_in_syncfile_activate_signal(struct isp_op_signal *sig)
{
	struct isp_obj_syncfile *sf;
	unsigned long flags;

	sf = nsobj_to_isp_in_syncfile(sig->source);
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

void isp_in_syncfile_deactivate_signal(struct isp_op_signal *sig)
{
	struct isp_op_signal *active;
	struct isp_obj_syncfile *sf;
	unsigned long flags;

	sf = nsobj_to_isp_in_syncfile(sig->source);
	if (WARN_ON(!sf))
		return;

	/*
	 * notify_active_chain is accessed from the IRQ context,
	 * so we need to disable local IRQs.
	 */
	write_lock_irqsave(&sf->in.notify_lock, flags);
	list_for_each_entry(active, &sf->in.notify_active_chain, entry) {
		if (active == sig) {
			list_del_init(&sig->entry);
			break;
		}
	}
	write_unlock_irqrestore(&sf->in.notify_lock, flags);
}

static void isp_out_syncfile_release(struct isp_obj *nsobj)
{
	struct isp_obj_syncfile *sf;

	sf = nsobj_to_isp_out_syncfile(nsobj);
	if (WARN_ON(!sf))
		return;

	isp_obj_unlink(nsobj);
	kfree(sf);
}

static void isp_dma_fence_release(struct dma_fence *fence)
{
	struct isp_obj_syncfile *sf;

	sf = container_of(fence, struct isp_obj_syncfile, out.fence);
	isp_obj_deinit(&sf->nsobj);
}

static const char *isp_dma_fence_driver_name(struct dma_fence *fence)
{
	return "isp";
}

static const char *isp_dma_fence_timeline_name(struct dma_fence *fence)
{
	return "isp-timeline";
}

static struct dma_fence_ops isp_out_fence_ops = {
	.get_driver_name	= isp_dma_fence_driver_name,
	.get_timeline_name	= isp_dma_fence_timeline_name,
	.release		= isp_dma_fence_release,
};

__printf(3, 4)
struct isp_obj_syncfile *isp_out_syncfile_register(struct isp_device *isp,
						   struct isp_obj_op *op,
						   const char *namefmt,
						   ...)
{
	struct sync_file *syncfile = NULL;
	char name[ISP_SYNCFILE_NAME_SZ];
	struct isp_obj_syncfile *sf;
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
	strscpy(sf->name, name, ISP_SYNCFILE_NAME_SZ);
	isp_obj_init(&sf->nsobj,
		     ISP_OBJ_TYPE_OUT_SYNCFILE,
		     isp_out_syncfile_release,
		     &isp->ns);

	sf->out.fence_context = dma_fence_context_alloc(1);
	dma_fence_init(&sf->out.fence,
		       &isp_out_fence_ops,
		       &sf->out.context_lock,
		       sf->out.fence_context,
		       atomic64_inc_return(&sf->out.fence_seqno));

	sf->out.fd = get_unused_fd_flags(O_CLOEXEC);
	if (sf->out.fd < 0)
		goto error;

	syncfile = sync_file_create(&sf->out.fence);
	if (!syncfile)
		goto error;

	if (isp_obj_link(&sf->nsobj, &op->nsobj))
		goto error;

	if (isp_obj_insert(&sf->nsobj))
		goto error;

	fd_install(sf->out.fd, syncfile->file);
	return sf;

error:
	if (sf->out.fd > 0)
		put_unused_fd(sf->out.fd);
	if (syncfile)
		fput(syncfile->file);
	isp_out_syncfile_release(&sf->nsobj);
	return NULL;
}
ALLOW_ERROR_INJECTION(isp_out_syncfile_register, NULL);

int isp_fire_out_syncfile_signal(struct isp_obj *nsobj)
{
	struct isp_obj_syncfile *sf;

	sf = nsobj_to_isp_out_syncfile(nsobj);
	if (WARN_ON(!sf))
		return -EINVAL;

	return dma_fence_signal(&sf->out.fence);
}
