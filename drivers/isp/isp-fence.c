// SPDX-License-Identifier: GPL-2.0
/*
 * ISP DMA fence
 *
 * Copyright (C) Google LLC
 */

#define pr_fmt(fmt) "isp-fence: " fmt

#include <linux/isp/isp-entity.h>
#include <linux/isp/isp-fence.h>
#include <linux/isp/isp-graph.h>
#include <linux/isp/isp-output.h>
#include <linux/sync_file.h>
#include <linux/kernel.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <uapi/linux/isp.h>

static bool isp_valid_fence_id(u32 id)
{
	if (id + ISP_OBJS_NS_FENCE_ID_START > ISP_OBJS_NS_FENCE_ID_END) {
		pr_devel("Invalid fence ID: %u\n", id);
		return false;
	}
	return true;
}

static struct isp_obj_fence *nsobj_to_isp_fence(struct isp_obj *nsobj,
						u32 type)
{
	if (!isp_obj_check_type(nsobj, type))
		return NULL;

	return container_of(nsobj, struct isp_obj_fence, nsobj);
}

int isp_out_fence_fd(struct isp_obj_fence *fc)
{
	if (!(fc->nsobj.type & ISP_OBJ_TYPE_OUT_FENCE))
		return -1;

	return fc->out.fd;
}

static struct isp_obj_fence *nsobj_to_isp_in_fence(struct isp_obj *nsobj)
{
	return nsobj_to_isp_fence(nsobj, ISP_OBJ_TYPE_IN_FENCE);
}

static struct isp_obj_fence *nsobj_to_isp_out_fence(struct isp_obj *nsobj)
{
	return nsobj_to_isp_fence(nsobj, ISP_OBJ_TYPE_OUT_FENCE);
}

void isp_in_fence_unregister(struct isp_obj *nsobj)
{
	struct isp_obj_fence *fc;

	fc = nsobj_to_isp_in_fence(nsobj);
	if (WARN_ON(!fc))
		return;

	isp_obj_remove(&fc->nsobj);
	isp_obj_deinit(&fc->nsobj);
}

void isp_out_fence_unregister(struct isp_obj *nsobj)
{
	struct isp_obj_fence *fc;

	fc = nsobj_to_isp_out_fence(nsobj);
	if (WARN_ON(!fc))
		return;

	isp_obj_unlink(nsobj);
	isp_obj_remove(nsobj);
	/*
	 * Exported DMA fence can be imported many times so we need to depend
	 * on DMA fence ref-counter and cannot release ISP fence object,
	 * because DMA fence uses context_lock which it borrows from ISP
	 * fence.
	 *
	 * We take a different approach here: we put DMA fence and release
	 * ISP fence object only from DAM fence ->release callback.
	 */
	dma_fence_put(&fc->out.fence);
}

void isp_fence_put(struct isp_obj_fence *fc)
{
	if (likely(fc))
		isp_obj_put(&fc->nsobj);
	else
		WARN_ON(1);
}

static void isp_in_fence_release(struct isp_obj *nsobj)
{
	struct isp_obj_fence *fc = nsobj_to_isp_in_fence(nsobj);

	if (fc->in.fence)
		dma_fence_remove_callback(fc->in.fence, &fc->in.cb);
	dma_fence_put(fc->in.fence);
	isp_obj_unlink(nsobj);
	kfree(fc);
}

static void isp_fence_fence_cb(struct dma_fence *f, struct dma_fence_cb *cb)
{
	struct isp_obj_fence *fc;
	unsigned long flags;

	fc = container_of(cb, struct isp_obj_fence, in.cb);

	write_lock_irqsave(&fc->in.notify_lock, flags);
	isp_fire_active_signals(&fc->in.notify_active_chain);
	write_unlock_irqrestore(&fc->in.notify_lock, flags);
}

__printf(4, 5)
struct isp_obj_fence *isp_in_fence_register(struct isp_device *isp,
					    struct isp_obj_op *op,
					    int fd,
					    const char *namefmt,
					    ...)
{
	char name[ISP_FENCE_NAME_SZ];
	struct isp_obj_fence *fc;
	va_list args;
	int ret;

	fc = kzalloc(sizeof(*fc), GFP_KERNEL);
	if (!fc)
		return NULL;

	va_start(args, namefmt);
	vsnprintf(name, sizeof(name), namefmt, args);
	va_end(args);

	INIT_LIST_HEAD(&fc->in.notify_active_chain);
	rwlock_init(&fc->in.notify_lock);
	strscpy(fc->name, name, ISP_FENCE_NAME_SZ);
	isp_obj_init(&fc->nsobj,
		     ISP_OBJ_TYPE_IN_FENCE,
		     isp_in_fence_release,
		     &isp->ns);

	fc->in.fence = sync_file_get_fence(fd);
	if (!fc->in.fence)
		goto error;

	ret = dma_fence_add_callback(fc->in.fence,
				     &fc->in.cb,
				     isp_fence_fence_cb);
	/* -ENOENT is returned when fence is already signaled */
	if (ret && ret != -ENOENT)
		goto error;

	if (isp_obj_link(&fc->nsobj, &op->nsobj))
		goto error;

	if (isp_obj_insert(&fc->nsobj))
		goto error;

	return fc;

error:
	isp_in_fence_release(&fc->nsobj);
	return NULL;
}
ALLOW_ERROR_INJECTION(isp_in_fence_register, NULL);

bool isp_in_fence_activate_signal(struct isp_op_signal *sig)
{
	struct isp_obj_fence *fc;
	unsigned long flags;

	fc = nsobj_to_isp_in_fence(sig->source);
	if (WARN_ON(!fc))
		return false;

	/*
	 * notify_active_chain is accessed from the IRQ context,
	 * so we need to disable local IRQs.
	 */
	write_lock_irqsave(&fc->in.notify_lock, flags);
	list_add_tail(&sig->entry, &fc->in.notify_active_chain);
	write_unlock_irqrestore(&fc->in.notify_lock, flags);

	if (dma_fence_is_signaled(fc->in.fence)) {
		/*
		 * If the fence is in signaled state at this point then check
		 * whether sig activation has raced with fence signaling, IOW
		 * whether list_add() has raced with isp_fence_fence_cb().
		 *
		 * If the sig is on the fence's active_chain list then
		 * activation happened too late and fence won't ever fire that
		 * sig: remove the sig and let pipeline handle failed
		 * activation.
		 *
		 * If the sig is not on the list, then it was activated just
		 * in time and isp_fence_fence_cb() fired it.
		 */
		if (isp_in_fence_deactivate_signal(sig))
			return false;
	}
	return true;
}

bool isp_in_fence_deactivate_signal(struct isp_op_signal *sig)
{
	struct isp_op_signal *active;
	struct isp_obj_fence *fc;
	unsigned long flags;
	bool ret;

	fc = nsobj_to_isp_in_fence(sig->source);
	if (WARN_ON(!fc))
		return false;

	/*
	 * notify_active_chain is accessed from the IRQ context,
	 * so we need to disable local IRQs.
	 */
	ret = false;
	write_lock_irqsave(&fc->in.notify_lock, flags);
	list_for_each_entry(active, &fc->in.notify_active_chain, entry) {
		if (active == sig) {
			list_del_init(&sig->entry);
			ret = true;
			break;
		}
	}
	write_unlock_irqrestore(&fc->in.notify_lock, flags);

	return ret;
}

static void isp_out_fence_release(struct isp_obj *nsobj)
{
	struct isp_obj_fence *fc;

	fc = nsobj_to_isp_out_fence(nsobj);
	if (WARN_ON(!fc))
		return;

	isp_obj_unlink(nsobj);
	kfree(fc);
}

static void isp_dma_fence_release(struct dma_fence *fence)
{
	struct isp_obj_fence *fc;

	fc = container_of(fence, struct isp_obj_fence, out.fence);
	isp_obj_deinit(&fc->nsobj);
}

static struct isp_obj *isp_fence_lookup(struct isp_ns *ns, u32 type, u32 id)
{
	if (!isp_valid_fence_id(id))
		return NULL;

	id += ISP_OBJS_NS_FENCE_ID_START;
	return isp_obj_lookup(ns, type, id);
}

struct isp_obj_fence *isp_out_fence_lookup(struct isp_ns *ns, u32 id)
{
	struct isp_obj *nsobj;

	nsobj = isp_fence_lookup(ns, ISP_OBJ_TYPE_OUT_FENCE, id);
	if (!nsobj)
		return NULL;

	return nsobj_to_isp_out_fence(nsobj);
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

__printf(2, 3)
struct isp_obj_fence *isp_out_fence_register(struct isp_ns *ns,
					     const char *namefmt,
					     ...)
{
	struct sync_file *syncfile = NULL;
	char name[ISP_FENCE_NAME_SZ];
	struct isp_obj_fence *fc;
	va_list args;

	fc = kzalloc(sizeof(*fc), GFP_KERNEL);
	if (!fc)
		return NULL;

	va_start(args, namefmt);
	vsnprintf(name, sizeof(name), namefmt, args);
	va_end(args);

	spin_lock_init(&fc->out.context_lock);
	atomic64_set(&fc->out.fence_seqno, 0);
	fc->out.fd = -1;
	strscpy(fc->name, name, ISP_FENCE_NAME_SZ);
	isp_obj_init(&fc->nsobj,
		     ISP_OBJ_TYPE_OUT_FENCE,
		     isp_out_fence_release,
		     ns);

	fc->out.fence_context = dma_fence_context_alloc(1);
	dma_fence_init(&fc->out.fence,
		       &isp_out_fence_ops,
		       &fc->out.context_lock,
		       fc->out.fence_context,
		       atomic64_inc_return(&fc->out.fence_seqno));

	fc->out.fd = get_unused_fd_flags(O_CLOEXEC);
	if (fc->out.fd < 0)
		goto error;

	if (!isp_valid_fence_id(fc->out.fd))
		goto error;

	isp_obj_set_id(&fc->nsobj, fc->out.fd + ISP_OBJS_NS_FENCE_ID_START);

	syncfile = sync_file_create(&fc->out.fence);
	if (!syncfile)
		goto error;

	if (isp_obj_insert(&fc->nsobj))
		goto error;

	fd_install(fc->out.fd, syncfile->file);
	return fc;

error:
	if (fc->out.fd > 0)
		put_unused_fd(fc->out.fd);
	if (syncfile)
		fput(syncfile->file);
	isp_out_fence_release(&fc->nsobj);
	return NULL;
}
ALLOW_ERROR_INJECTION(isp_out_fence_register, NULL);

int isp_fire_out_fence_signal(struct isp_obj *nsobj)
{
	struct isp_obj_fence *fc;

	fc = nsobj_to_isp_out_fence(nsobj);
	if (WARN_ON(!fc))
		return -EINVAL;

	return dma_fence_signal(&fc->out.fence);
}

int isp_drain_out_fences(struct isp_pipeline *pipeline)
{
	struct isp_obj *nsobj;
	struct isp_obj *save;

	isp_ns_for_each_obj_safe(nsobj, save, &pipeline->objs) {
		if (isp_obj_type(nsobj) != ISP_OBJ_TYPE_OUT_FENCE)
			continue;

		isp_fire_out_fence_signal(nsobj);
		isp_out_fence_unregister(nsobj);
	}
	return 0;
}
