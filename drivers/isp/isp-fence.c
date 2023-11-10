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

static bool isp_valid_fence_id(u32 id, u32 low, u32 high)
{
	if (id + low > high) {
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

/**
 * isp_out_fence_fd() - Return OUT (exported) ISP fence file descriptor
 * @fc: ISP fence
 *
 * Return: negative code on error and file descriptor otherwise
 */
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

/**
 * isp_in_fence_unregister() - Unregister IN (imported) fence
 * @nsobj: namespace object of the ISP fence
 *
 * Removes ISP fence from DMA fence callback list and from the namespace.
 */
void isp_in_fence_unregister(struct isp_obj *nsobj)
{
	struct isp_obj_fence *fc;

	fc = nsobj_to_isp_in_fence(nsobj);
	if (WARN_ON(!fc))
		return;

	if (fc->in.fence)
		dma_fence_remove_callback(fc->in.fence, &fc->in.cb);
	dma_fence_put(fc->in.fence);

	isp_obj_remove(&fc->nsobj);
}

/**
 * isp_out_fence_unregister() - Unregister OUT (exported) fence
 * @nsobj: namespace object of the ISP fence
 *
 * This removes the fence from the namespace and puts underlying DMA fence
 * ref-count.
 */
void isp_out_fence_unregister(struct isp_obj *nsobj)
{
	struct isp_obj_fence *fc;

	fc = nsobj_to_isp_out_fence(nsobj);
	if (WARN_ON(!fc))
		return;

	isp_obj_remove(nsobj);
	/*
	 * Exported DMA fence can be imported many times so we need to depend
	 * on DMA fence ref-counter and cannot release ISP fence object,
	 * because DMA fence uses context_lock which it borrows from ISP
	 * fence.
	 *
	 * We take a different approach here: we put DMA fence and release
	 * ISP fence object only from DMA fence ->release callback.
	 */
	dma_fence_put(&fc->out.fence);
}

/**
 * isp_fence_put() - Put ISP fence ref-count
 * @fc: ISP fence
 */
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

	kfree(fc);
}

static void isp_in_fence_cb(struct dma_fence *f, struct dma_fence_cb *cb)
{
	struct isp_obj_fence *fc;
	unsigned long flags;

	fc = container_of(cb, struct isp_obj_fence, in.cb);

	write_lock_irqsave(&fc->in.notify_lock, flags);
	isp_fire_active_signals(&fc->in.active_sig_chain);
	write_unlock_irqrestore(&fc->in.notify_lock, flags);

	/*
	 * This is where we could release IN fence, since it's been signaled
	 * and there is no dedicated in-fence-destroy OP instruction.
	 *
	 * Could, but we cannot do much here in fact, because this function
	 * is executed in atomic context.
	 *
	 * Add fence to pipeline deferred release list so that we can
	 * unregister this fence from non-atomic context some time later.
	 */
	isp_in_fence_release_deferred(fc);
}

/**
 * isp_in_fence_register() - Register new IN (imported) fence
 * @pipeline: ISP pipeline
 * @fd: file descriptor of imported DMA fence
 * @id: ID of the ISP fence object
 *
 * This creates ISP fence object, imports underlying DMA fence and registers
 * a callback so that we get DMA fence completion signals.
 *
 * Return: 0 on success or negative error code otherwise
 */
int isp_in_fence_register(struct isp_pipeline *pipeline, u32 fd, u32 id)
{
	struct isp_obj_fence *fc;
	int ret;

	fc = kzalloc(sizeof(*fc), GFP_KERNEL);
	if (!fc)
		return -ENOMEM;

	INIT_LIST_HEAD(&fc->in.active_sig_chain);
	INIT_LIST_HEAD(&fc->in.release_entry);
	rwlock_init(&fc->in.notify_lock);
	fc->in.pipeline = pipeline;
	isp_obj_init(&fc->nsobj,
		     ISP_OBJ_TYPE_IN_FENCE,
		     isp_in_fence_release,
		     &pipeline->objs);

	if (!isp_valid_fence_id(id, ISP_OBJS_NS_IN_FENCE_ID_START,
				ISP_OBJS_NS_IN_FENCE_ID_END))
		goto error;

	isp_obj_set_id(&fc->nsobj, id + ISP_OBJS_NS_IN_FENCE_ID_START);

	fc->in.fence = sync_file_get_fence(fd);
	if (!fc->in.fence)
		goto error;

	ret = dma_fence_add_callback(fc->in.fence,
				     &fc->in.cb,
				     isp_in_fence_cb);
	/* -ENOENT is returned when fence is already signaled */
	if (ret && ret != -ENOENT)
		goto error;

	/* Fences belong to USER_ID namespace, so we don't need id */
	if (isp_obj_move(&fc->nsobj, NULL))
		goto error;

	return 0;

error:
	isp_in_fence_release(&fc->nsobj);
	return -EINVAL;
}
ALLOW_ERROR_INJECTION(isp_in_fence_register, ERRNO);

/**
 * isp_in_fence_activate_signal() - ISP signal activation for imported fence
 * @sig: ISP signal
 *
 * Adds ISP signal to the imported ISP fence active chain, if the underlying
 * DMA fence has not been signaled yet.
 *
 * Return: true on success and false otherwise
 */
bool isp_in_fence_activate_signal(struct isp_op_signal *sig)
{
	struct isp_obj_fence *fc;
	unsigned long flags;

	fc = nsobj_to_isp_in_fence(sig->source);
	if (WARN_ON(!fc))
		return false;

	write_lock_irqsave(&fc->in.notify_lock, flags);
	list_add_tail(&sig->entry, &fc->in.active_sig_chain);
	write_unlock_irqrestore(&fc->in.notify_lock, flags);

	if (dma_fence_is_signaled(fc->in.fence)) {
		/*
		 * If the fence is in signaled state at this point then check
		 * whether sig activation has raced with fence signaling, IOW
		 * whether list_add() has raced with isp_fence_fence_cb().
		 *
		 * If the sig is on the fence's active_sig_chain list then
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

/**
 * isp_in_fence_deactivate_signal() - ISP signal deactivation for imported fence
 * @sig: ISP signal
 *
 * Performs ISP signal deactivation (if signal has not been fired yet).
 *
 * Return: true on success and false otherwise
 */
bool isp_in_fence_deactivate_signal(struct isp_op_signal *sig)
{
	struct isp_op_signal *active;
	struct isp_obj_fence *fc;
	unsigned long flags;
	bool ret;

	fc = nsobj_to_isp_in_fence(sig->source);
	if (WARN_ON(!fc))
		return false;

	ret = false;
	write_lock_irqsave(&fc->in.notify_lock, flags);
	list_for_each_entry(active, &fc->in.active_sig_chain, entry) {
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

	kfree(fc);
}

static void isp_dma_fence_release(struct dma_fence *fence)
{
	struct isp_obj_fence *fc;

	fc = container_of(fence, struct isp_obj_fence, out.fence);
	isp_obj_deinit(&fc->nsobj);
}

struct isp_obj_fence *isp_out_fence_lookup(struct isp_ns *ns, u32 id)
{
	struct isp_obj *nsobj;

	if (!isp_valid_fence_id(id, ISP_OBJS_NS_OUT_FENCE_ID_START,
				ISP_OBJS_NS_OUT_FENCE_ID_END))
		return NULL;

	id += ISP_OBJS_NS_OUT_FENCE_ID_START;
	nsobj = isp_obj_lookup(ns, ISP_OBJ_TYPE_OUT_FENCE, id);
	if (!nsobj)
		return NULL;

	return nsobj_to_isp_out_fence(nsobj);
}

struct isp_obj_fence *isp_in_fence_lookup(struct isp_ns *ns, u32 id)
{
	struct isp_obj *nsobj;

	if (!isp_valid_fence_id(id, ISP_OBJS_NS_IN_FENCE_ID_START,
				ISP_OBJS_NS_IN_FENCE_ID_END))
		return NULL;

	id += ISP_OBJS_NS_IN_FENCE_ID_START;
	nsobj = isp_obj_lookup(ns, ISP_OBJ_TYPE_IN_FENCE, id);
	if (!nsobj)
		return NULL;

	return nsobj_to_isp_in_fence(nsobj);
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

/**
 * isp_out_fence_register() - Register new OUT (exported) fence
 * @ns: namespace
 * @context: ID of the context this fence is run on
 * @seqno: sequential number within this context
 *
 * This creates ISP fence object, a corresponding DMA fence object and
 * installs the syncfile into process' fd-table so that the DMA fence
 * can be exported.
 *
 * Return: ISP fence points on success and NULL on error
 */
struct isp_obj_fence *isp_out_fence_register(struct isp_ns *ns,
					     u64 context, u64 seqno)
{
	struct sync_file *syncfile = NULL;
	struct isp_obj_fence *fc;

	fc = kzalloc(sizeof(*fc), GFP_KERNEL);
	if (!fc)
		return NULL;

	spin_lock_init(&fc->out.context_lock);
	fc->out.fd = -1;
	isp_obj_init(&fc->nsobj,
		     ISP_OBJ_TYPE_OUT_FENCE,
		     isp_out_fence_release,
		     ns);

	dma_fence_init(&fc->out.fence, &isp_out_fence_ops,
		       &fc->out.context_lock, context, seqno);

	fc->out.fd = get_unused_fd_flags(O_CLOEXEC);
	if (fc->out.fd < 0)
		goto error;

	if (!isp_valid_fence_id(fc->out.fd, ISP_OBJS_NS_OUT_FENCE_ID_START,
				ISP_OBJS_NS_OUT_FENCE_ID_END))
		goto error;

	isp_obj_set_id(&fc->nsobj, fc->out.fd + ISP_OBJS_NS_OUT_FENCE_ID_START);

	syncfile = sync_file_create(&fc->out.fence);
	if (!syncfile)
		goto error;

	/*
	 * Bump ref-count because we are not the only ones who hold a
	 * reference to that object. DMA fence indirectly owns this object
	 * and we can release it only once DMA fence ref-count reaches 0.
	 */
	if (WARN_ON(!isp_obj_get(&fc->nsobj)))
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

/**
 * isp_fire_out_fence_signal() - Signal completion of a DMA fence
 * @nsobj: namespace object of the ISP fence
 *
 * Return: 0 on success or negative error code otherwise
 */
int isp_fire_out_fence_signal(struct isp_obj *nsobj)
{
	struct isp_obj_fence *fc;

	fc = nsobj_to_isp_out_fence(nsobj);
	if (WARN_ON(!fc))
		return -EINVAL;

	return dma_fence_signal(&fc->out.fence);
}

/**
 * isp_drain_fences() - Drains pipeline's fences
 * @pipeline: ISP pipeline to drain
 *
 * This signals and unregisters all OUT (exported) and IN (imported)
 * ISP fences that the pipeline in question owns.
 *
 * Return: 0
 */
int isp_drain_fences(struct isp_pipeline *pipeline)
{
	struct isp_obj *nsobj;
	struct isp_obj *save;

	isp_ns_for_each_obj_safe(nsobj, save, &pipeline->objs) {
		switch (isp_obj_type(nsobj)) {
		case ISP_OBJ_TYPE_OUT_FENCE:
			isp_fire_out_fence_signal(nsobj);
			isp_out_fence_unregister(nsobj);
			break;
		case ISP_OBJ_TYPE_IN_FENCE:
			isp_in_fence_unregister(nsobj);
			break;
		default:
			break;
		}
	}
	return 0;
}
