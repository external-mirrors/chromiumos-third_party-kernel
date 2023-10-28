// SPDX-License-Identifier: GPL-2.0
/*
 * ISP requests executor
 *
 * Copyright (C) Google LLC
 */

#define pr_fmt(fmt) "isp-pipeline: " fmt

#include <linux/isp/isp-buffer.h>
#include <linux/isp/isp-device.h>
#include <linux/isp/isp-entity.h>
#include <linux/isp/isp-fence.h>
#include <linux/isp/isp-graph.h>
#include <linux/isp/isp-output.h>
#include <linux/isp/isp-pipeline.h>
#include <linux/isp/isp-ringbuffer.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/atomic.h>

#include <trace/events/isp.h>

#include <uapi/linux/isp.h>

/*
 * a counter used to assign a unique ID to each pipeline
 * we ignore the case of overflow, so it should only be used for debugging
 */
static atomic_t pipeline_count = ATOMIC_INIT(1);

/**
 * isp_pipeline_is_active() - Check whether the execution pipeline is active
 * @pipeline: pointer to ISP pipeline
 *
 * Return: true if the pipeline is active, or false otherwise.
 */
static bool isp_pipeline_is_active(struct isp_pipeline *pipeline)
{
	if (test_bit(ISP_PIPELINE_IO_EXITING, &pipeline->io_state))
		return false;
	if (!test_bit(ISP_PIPELINE_IO_ACTIVE, &pipeline->io_state))
		return false;
	return true;
}

static struct isp_obj_op *nsobj_to_isp_op(struct isp_obj *nsobj)
{
	/* Should never happen */
	if (!isp_obj_check_type(nsobj, ISP_OBJ_TYPE_OPERATION))
		return NULL;

	return container_of(nsobj, struct isp_obj_op, nsobj);
}

static struct isp_obj_op *isp_op_lookup(struct isp_ns *ns, u32 id)
{
	struct isp_obj *nsobj;

	nsobj = isp_obj_lookup(ns, ISP_OBJ_TYPE_OPERATION, id);
	if (!nsobj)
		return NULL;

	return nsobj_to_isp_op(nsobj);
}

static void isp_op_put(struct isp_obj_op *op)
{
	if (likely(op))
		isp_obj_put(&op->nsobj);
	else
		WARN_ON(1);
}

/**
 * release_signal() - Release the signal
 * @sig: pointer to ISP signal
 */
static void release_signal(struct isp_op_signal *sig)
{
	/*
	 * NOTE that for both source and target a signal is considered to
	 * be consumed and, hence, target and source ref-counters can be
	 * dropped. Note that this can be the final put for source.
	 */
	isp_obj_put(sig->source);
	isp_obj_put(sig->target);
	kfree(sig);
}

/**
 * isp_op_release() - Release ISP operation
 * @nsobj: pointer to ISP object that represents a ISP operation
 *
 * Release a ISP operation.
 * If the operation has any post actions registered, then execute them before
 * releasing.
 */
static void isp_op_release(struct isp_obj *nsobj)
{
	struct isp_obj_op *op = nsobj_to_isp_op(nsobj);

	WARN_ON(!list_empty(&op->notify_active_chain));
	WARN_ON(!list_empty(&op->notify_pending_chain));
	WARN_ON(!list_empty(&op->notifiers));

	if (op->exec_entity)
		isp_entity_put(op->exec_entity);
	if (op->exec_instance)
		isp_instance_put(op->exec_instance);
	kfree(op);
}

/**
 * isp_op_set_state() - Set ISP operation to a new state
 * @op: pointer to ISP operation
 * @new_state: the new state of the target operation
 *
 * This handles the state transfer of a ISP operation.
 * Note that it's not allowed to roll back an operation's state:
 * For example the transfer will fail if the operation is already executed or
 * deleted.
 * Also, it's not allowed to delete a running operation.
 *
 * Return: True if the operation is set to the new state, or false otherwise.
 */
static bool isp_op_set_state(struct isp_obj_op *op,
			     enum isp_operation_state new_state)
{
	unsigned long flags;
	bool ret = false;

	write_lock_irqsave(&op->notify_lock, flags);
	/* Cannot do anything with this object */
	if (op->state == ISP_OPERATION_STATE_DELETED)
		goto out;
	/* Too late to delete it */
	if (op->state == ISP_OPERATION_STATE_EXECUTED)
		goto out;
	/* This ship has sailed. Too late to delete this object. */
	if ((op->state == ISP_OPERATION_STATE_RUNNING ||
	     op->state == ISP_OPERATION_STATE_QUEUED) &&
	    new_state == ISP_OPERATION_STATE_DELETED)
		goto out;
	/* Do not go backwards */
	if (WARN_ON(op->state > new_state))
		goto out;

	op->state = new_state;
	trace_isp_operation_set_state(op);
	ret = true;
out:
	write_unlock_irqrestore(&op->notify_lock, flags);
	return ret;
}

/**
 * isp_op_enqueue() - Enqueue an operation
 * @op: pointer to ISP operation
 *
 * This transitions the operation state to QUEUED, add it to IO thread and then
 * wakes up the thread.
 */
static void isp_op_enqueue(struct isp_obj_op *op)
{
	struct isp_pipeline *pipeline;
	unsigned long flags;

	/*
	 * This is where operation enqueuing (and execution) is synchronized
	 * with operation removal. If we are not able to set operation state
	 * to QUEUED then we lost the race against operation removal.
	 *
	 * Consequentially if we successfully set operation to QUEUED then
	 * operation removal should never succeed.
	 */
	if (!isp_op_set_state(op, ISP_OPERATION_STATE_QUEUED))
		return;

	pipeline = op->pipeline;
	spin_lock_irqsave(&pipeline->io_queue_lock, flags);
	list_add_tail(&op->io_queue_entry, &pipeline->io_queue);
	spin_unlock_irqrestore(&pipeline->io_queue_lock, flags);

	if (isp_pipeline_is_active(pipeline))
		wake_up(&pipeline->io_queue_wait);
}

enum {
	ISP_OP_PENDING_SIGNAL_NONE,
	ISP_OP_PENDING_SIGNAL_ACTIVATED,
	ISP_OP_PENDING_SIGNAL_FAILURE,
};

/**
 * isp_op_activate_pending_signal() - Activate the first pending signal in the
 * pending chain of an operation
 * @op: pointer to ISP operation to activate the signal from
 *
 * Return:
 *   ISP_OP_PENDING_SIGNAL_ACTIVATED when the signal is activated;
 *   ISP_OP_PENDING_SIGNAL_NONE when the pending chain is empty;
 *   ISP_OP_PENDING_SIGNAL_FAILURE when the signal failed to be activated.
 */
static int isp_op_activate_pending_signal(struct isp_obj_op *op)
{
	struct isp_op_signal *sig;
	unsigned long flags;
	bool ret;

	write_lock_irqsave(&op->notify_lock, flags);
	if (list_empty(&op->notify_pending_chain)) {
		write_unlock_irqrestore(&op->notify_lock, flags);
		return ISP_OP_PENDING_SIGNAL_NONE;
	}

	sig = list_first_entry(&op->notify_pending_chain,
			       struct isp_op_signal,
			       entry);
	list_del(&sig->entry);
	write_unlock_irqrestore(&op->notify_lock, flags);

	ret = sig->activate(sig);
	if (!ret) {
		/*
		 * We failed to activate pending signal, something is not
		 * right with the signal source: e.g. source OP is in
		 * executed/deleted state. Decrement ->num_blockers, because
		 * this signal will not be raised.
		 */
		atomic_dec(&op->num_blockers);
		return ISP_OP_PENDING_SIGNAL_FAILURE;
	}
	trace_isp_signal_add_active(sig);
	return ISP_OP_PENDING_SIGNAL_ACTIVATED;
}

/**
 * isp_op_notify() - Notify the signal target
 * @sig: pointer to ISP signal
 *
 * This notifies the signal target, which is an operation, and possibly
 * enqueues it if it's no longer being blocked on any dependencies.
 *
 * Return: True if the target operation is enqueued, or false otherwise.
 */
static bool isp_op_notify(struct isp_op_signal *sig)
{
	struct isp_obj_op *op;
	bool execute;
	int ret;

	op = nsobj_to_isp_op(sig->target);
	if (!op)
		return false;

	trace_isp_signal_fire_active(sig);
	execute = atomic_dec_and_test(&op->num_blockers);
	/*
	 * We are in STRICT mode, pick first pending signal and try to
	 * activate it (until we activated a signal or ran out of pending
	 * signals).
	 *
	 * In WEAK mode this will return PENDING_SIGNAL_NONE and will
	 * not alter execute value that we read earlier.
	 */
	do {
		ret = isp_op_activate_pending_signal(op);

		if (ret == ISP_OP_PENDING_SIGNAL_NONE)
			break;
		if (ret == ISP_OP_PENDING_SIGNAL_ACTIVATED)
			break;

		execute = (atomic_read(&op->num_blockers) == 0);
	} while (ret == ISP_OP_PENDING_SIGNAL_FAILURE);

	if (execute)
		isp_op_enqueue(op);
	return execute;
}

static void isp_drain_op_fences(struct isp_obj_op *op)
{
	struct isp_obj *link;
	struct isp_obj *save;

	isp_obj_for_each_link_safe(link, save, &op->nsobj) {
		switch (isp_obj_type(link)) {
		case ISP_OBJ_TYPE_IN_FENCE:
			isp_in_fence_unregister(link);
			break;
		default:
			pr_err("Unknown link object type: %d\n",
			       isp_obj_type(link));
		}
	}
}

/**
 * isp_op_destroy_signals() - Release all signals that operation owns
 * @op: operation to drain
 */
static void isp_op_destroy_signals(struct isp_obj_op *op)
{
	struct isp_op_signal *sig, *safe;

	list_for_each_entry_safe(sig, safe, &op->notifiers, notifiers_entry) {
		list_del_init(&sig->notifiers_entry);
		release_signal(sig);
	}
}

/**
 * isp_drain_op_signals() - Drains operation signals. This also includes
 * deactivation of already activated signals (without raising them).
 * @op: operation to drain
 */
static void isp_drain_op_signals(struct isp_obj_op *op)
{
	struct isp_op_signal *sig, *safe;
	unsigned long flags;

	write_lock_irqsave(&op->notify_lock, flags);
	/* First, remove all pending signals, so nothing gets activated */
	list_for_each_entry_safe(sig, safe, &op->notify_pending_chain, entry) {
		list_del_init(&sig->entry);
	}
	write_unlock_irqrestore(&op->notify_lock, flags);

	/* Second, deactivate all already activated (yet not raised) signals */
	list_for_each_entry_safe(sig, safe, &op->notifiers, notifiers_entry) {
		sig->deactivate(sig);
	}

	/* Now, release all signals owned by the operation */
	isp_op_destroy_signals(op);
}

/**
 * isp_drain_op() - Drains a single operation.
 * @op: operation to drain
 *
 * Return: true on success or false otherwise.
 */
static bool isp_drain_op(struct isp_obj_op *op)
{
	/*
	 * This is where operation removal is synchronized with operation
	 * enqueuing and execution. If we are not able to set operation state
	 * to DELETED then we lost the race against enqueue path and should
	 * let operation to execute. Consequentially if we successfully set
	 * operation to DELETE then operation enqueuing/execution should never
	 * occur.
	 */
	if (!isp_op_set_state(op, ISP_OPERATION_STATE_DELETED))
		return false;

	isp_drain_op_signals(op);
	isp_drain_op_fences(op);
	isp_obj_remove(&op->nsobj);
	/*
	 * We cannot deinit OP nsobj at this point, as we still
	 * may have other operations in the objs_list that hold
	 * reference to this OP (dependency, etc.)
	 */
	isp_op_put(op);
	return true;
}

/**
 * isp_drain_ops() - Drains operations from the pipeline. This must be used
 * only from the pipeline (emergency) termination path.
 * @pipeline: pointer to ISP pipeline
 */
static void isp_drain_ops(struct isp_pipeline *pipeline)
{
	struct isp_obj *nsobj;
	struct isp_obj *save;

	isp_ns_for_each_obj_safe(nsobj, save, &pipeline->ops) {
		struct isp_obj_op *op;

		op = nsobj_to_isp_op(nsobj);
		if (WARN_ON(!op))
			return;

		isp_drain_op(op);
	}
}

/*
 * isp_op_fire_signals() - Signal the operations blocked on the input operation
 * @op: pointer to ISP operation
 *
 * This walks the notify_active_chain and raises signals for all pipeline
 * isp_op objects that are blocked on us.
 */
static void isp_op_fire_signals(struct isp_obj_op *op)
{
	/*
	 * We are in ISP_OPERATION_STATE_EXECUTED, no new signals can be
	 * registered. So we should be fine without taking the notify_lock
	 * here.
	 *
	 * Famous last words.
	 */
	isp_fire_active_signals(&op->notify_active_chain);
	isp_op_destroy_signals(op);
	isp_drain_op_fences(op);
}

/**
 * isp_op_activate_signal() - Activate a pending signal
 * @sig: pointer to ISP signal where its source is an operation
 *
 * This activates the signal by appending it to source operation's notify
 * chain.
 * Unlike isp_op_activate_pending_signal(), which is where sig->activate() is
 * called, this is the actual callback that does the work and to be fed into
 * isp_op_add_pending_signal() to construct the ISP signal object.
 *
 * Return: True on success, or false otherwise e.g. the source operation is
 * already EXECUTED or DELETED.
 */
static bool isp_op_activate_signal(struct isp_op_signal *sig)
{
	struct isp_obj_op *source;
	unsigned long flags;
	bool ret;

	source = nsobj_to_isp_op(sig->source);
	if (WARN_ON(!source))
		return false;

	write_lock_irqsave(&source->notify_lock, flags);
	if (source->state == ISP_OPERATION_STATE_EXECUTED ||
	    source->state == ISP_OPERATION_STATE_DELETED) {
		write_unlock_irqrestore(&source->notify_lock, flags);
		ret = false;
		goto out;
	}
	list_add_tail(&sig->entry, &source->notify_active_chain);
	write_unlock_irqrestore(&source->notify_lock, flags);
	ret = true;

out:
	return ret;
}

static bool isp_op_deactivate_signal(struct isp_op_signal *sig)
{
	struct isp_op_signal *active;
	struct isp_obj_op *source;
	unsigned long flags;
	bool ret;

	source = nsobj_to_isp_op(sig->source);
	if (WARN_ON(!source))
		return false;

	ret = true;
	write_lock_irqsave(&source->notify_lock, flags);
	list_for_each_entry(active, &source->notify_active_chain, entry) {
		if (active == sig) {
			list_del_init(&sig->entry);
			ret = true;
			break;
		}
	}
	write_unlock_irqrestore(&source->notify_lock, flags);

	return ret;
}

/**
 * isp_op_completion_event() - Notify OP completion to user-space
 * @pipeline: pointer to ISP pipeline
 * @op: pointer to ISP operation
 *
 * This records the completion of an operation and push it into ring buffer to
 * notify user-space.
 * This expects that @op is either EXECUTED or DELETED at this stage.
 */
static void isp_op_completion_event(struct isp_pipeline *pipeline,
				    struct isp_obj_op *op)
{
	struct isp_completion completion = {};

	completion.id = isp_obj_id(&op->nsobj);

	/*
	 * This OP is executed, nothing can change its ->state so we
	 * don't really need to take the notify_lock.
	 */
	if (op->state == ISP_OPERATION_STATE_EXECUTED)
		completion.type = ISP_COMPLETION_TYPE_EXECUTED;
	else if (op->state == ISP_OPERATION_STATE_DELETED)
		completion.type = ISP_COMPLETION_TYPE_DELETED;
	else
		pr_err("Unknown OP state: %d\n", op->state);

	isp_ringbuffer_write(&pipeline->event_buffer, &completion);
}

/*
 * prepare() stage should only handle instructions that set up execution
 * context: e.g. create entity instance, import DMA buffer, etc. We cannot
 * handle instructions that destroy execution context, as there may be
 * operations in the batch that depend on that context. Therefore context
 * destructions instructions are handled during operation execution.
 */
static int isp_prepare_dmabuf_instruction(struct isp_obj_op *op,
					  struct isp_dmabuf_instruction *insn)
{
	struct isp_pipeline *pipeline = op->pipeline;

	if (insn->op == ISP_OP_DMABUF_REMOVE)
		return 0;

	if (insn->op == ISP_OP_DMABUF_ADD) {
		struct isp_obj_buffer *buffer;

		buffer = isp_buffer_register(&pipeline->objs,
					     op->exec_entity,
					     insn->dma_fd,
					     insn->buf_id);
		if (!buffer)
			return -EINVAL;

		isp_buffer_put(buffer);
		return 0;
	}

	pr_devel("Unknown dmabuf instruction operation: %d\n", insn->op);
	return -EINVAL;
}

static int
isp_prepare_instance_instruction(struct isp_obj_op *op,
				 struct isp_instance_instruction *insn)
{
	struct isp_pipeline *pipeline = op->pipeline;

	if (insn->op == ISP_OP_INSTANCE_DESTROY)
		return 0;

	if (insn->op == ISP_OP_INSTANCE_CREATE) {
		struct isp_obj_instance *instance;

		instance = isp_instance_create(&pipeline->objs,
					       op->exec_entity,
					       insn->id);
		if (!instance)
			return -EINVAL;

		isp_instance_put(instance);
		return 0;
	}

	pr_devel("Unknown instance instruction operation: %d\n", insn->op);
	return -EINVAL;
}

static int isp_run_dmabuf_instruction(struct isp_obj_op *op,
				      struct isp_dmabuf_instruction *insn)
{
	struct isp_pipeline *pipeline = op->pipeline;

	if (insn->op == ISP_OP_DMABUF_ADD)
		return 0;

	if (insn->op == ISP_OP_DMABUF_REMOVE)
		return isp_buffer_unregister(&pipeline->objs, insn->buf_id);

	pr_err("Unknown dmabuf instruction operation: %d\n", insn->op);
	return -EINVAL;
}

static int isp_run_instance_instruction(struct isp_obj_op *op,
					struct isp_instance_instruction *insn)
{
	struct isp_pipeline *pipeline = op->pipeline;

	if (insn->op == ISP_OP_INSTANCE_CREATE)
		return 0;

	if (insn->op == ISP_OP_INSTANCE_DESTROY)
		return isp_instance_destroy(&pipeline->objs, insn->id);

	pr_err("Unknown instance instruction operation: %d\n", insn->op);
	return -EINVAL;
}

static int
isp_run_signal_fence_instruction(struct isp_obj_op *op,
				 struct isp_signal_fence_instruction *insn)
{
	struct isp_pipeline *pipeline = op->pipeline;
	struct isp_obj_fence *fence;

	fence = isp_out_fence_lookup(&pipeline->objs, insn->id);
	if (!fence)
		return -ENOENT;

	isp_fire_out_fence_signal(&fence->nsobj);
	isp_out_fence_unregister(&fence->nsobj);
	isp_fence_put(fence);
	return 0;
}

/*
 * isp_read_instruction() and isp_write_instruction() hold the reference of
 * DMA-buf objects only through out corresponding entity call. If the driver
 * needs to access that buffer from different context (e.g. IRQ which may
 * happen after entity call returns) the driver need to additionally increment
 * DMA-buf object's ref-count and decrement it once DMA-buf access is done.
 *
 * Driver cannot lookup DMA buffer objects directly, because those belong to
 * pipeline local namespace. However, we pass pointer to isp_obj_buffer object
 * down to the entity call.
 *
 * User-space provides us with an array of buffer object IDs, we overwrite
 * those IDs with pointers to actual buffer objects (if such objects exists)
 * right before entity call. Functions operate on local copy of user-supplied
 * RW instruction.
 */
static void isp_buffers_list_put(u32 num_buffers, u64 *list)
{
	u32 i;

	if (!list)
		return;

	for (i = 0; i < num_buffers; i++) {
		if (list[i]) {
			struct isp_obj_buffer *buffer;

			buffer = (struct isp_obj_buffer *)list[i];
			isp_buffer_put(buffer);
		}
	}

	kvfree(list);
}

static u64 *isp_buffers_list_get(struct isp_pipeline *pipeline,
				 u32 num_buffers,
				 u64 buffers_list)
{
	u64 __user *payload;
	u64 *list;
	u32 i;

	if (num_buffers > ISP_RW_INSN_MAX_NUM_BUFFERS)
		return NULL;

	list = kvcalloc(num_buffers, sizeof(u64), GFP_KERNEL);
	if (!list)
		return NULL;

	payload = (u64 *)buffers_list;
	for (i = 0; i < num_buffers; i++) {
		struct isp_obj_buffer *buffer;
		u64 id;

		if (copy_from_user(&id, payload, sizeof(id)))
			goto error;

		buffer = isp_buffer_lookup(&pipeline->objs, id);
		if (!buffer)
			goto error;

		list[i] = (u64)buffer;
		payload++;
	}

	return list;

error:
	isp_buffers_list_put(num_buffers, list);
	return NULL;
}

static int isp_read_instruction(struct isp_obj_op *op,
				struct isp_read_instruction *insn)
{
	struct isp_obj_entity *entity = op->exec_entity;
	struct isp_pipeline *pipeline = op->pipeline;
	u64 *buffers_list;
	void *dev;
	int ret;

	if (!op->exec_instance)
		return -EINVAL;

	/* Set exclusive ownership flag */
	if (!isp_instance_private_set(op->exec_instance, op))
		return -EINVAL;

	if (insn->num_buffers) {
		buffers_list = isp_buffers_list_get(pipeline,
						    insn->num_buffers,
						    insn->buffers_list);
		if (!buffers_list) {
			isp_instance_private_set(op->exec_instance, NULL);
			return -EINVAL;
		}

		insn->buffers_list = (u64)buffers_list;
	}

	dev = isp_entity_driver_data(entity);
	ret = entity->ops->instance_read(dev, op->exec_instance, insn);

	/* If insn executed in non-deferred context then clear ownership */
	if (ret != ISP_INSTRUCTION_EXEC_DEFERRED)
		isp_instance_private_set(op->exec_instance, NULL);

	if (insn->num_buffers)
		isp_buffers_list_put(insn->num_buffers, buffers_list);
	return ret;
}

static int isp_write_instruction(struct isp_obj_op *op,
				 struct isp_write_instruction *insn)
{
	struct isp_obj_entity *entity = op->exec_entity;
	struct isp_pipeline *pipeline = op->pipeline;
	u64 *buffers_list;
	void *dev;
	int ret;

	if (!op->exec_instance)
		return -EINVAL;

	/* Set exclusive ownership flag */
	if (!isp_instance_private_set(op->exec_instance, op))
		return -EINVAL;

	if (insn->num_buffers) {
		buffers_list = isp_buffers_list_get(pipeline,
						    insn->num_buffers,
						    insn->buffers_list);
		if (!buffers_list) {
			isp_instance_private_set(op->exec_instance, NULL);
			return -EINVAL;
		}

		insn->buffers_list = (u64)buffers_list;
	}

	dev = isp_entity_driver_data(entity);
	ret = entity->ops->instance_write(dev, op->exec_instance, insn);

	/* If insn executed in non-deferred context then clear ownership */
	if (ret != ISP_INSTRUCTION_EXEC_DEFERRED)
		isp_instance_private_set(op->exec_instance, NULL);

	if (insn->num_buffers)
		isp_buffers_list_put(insn->num_buffers, buffers_list);
	return ret;
}

static void isp_op_set_rw_instruction_error(struct isp_obj_op *op, int error)
{
	struct isp_rw_instruction __user *payload;

	payload = op->exec_instruction_addr;
	if (payload == ISP_OP_NULL_PTR)
		return;

	put_user(error, &payload->error);
}

static void isp_op_run_complete(struct isp_obj_op *op)
{
	struct isp_pipeline *pipeline = op->pipeline;

	/* New signals cannot be registered after this line */
	WARN_ON(!isp_op_set_state(op, ISP_OPERATION_STATE_EXECUTED));

	if (op->exec_error)
		isp_op_set_rw_instruction_error(op, op->exec_error);

	/*
	 * Remove operation from the namespace, so that its ID can be
	 * reused from now on.
	 */
	isp_obj_remove(&op->nsobj);

	/* Notify user-space that we are done with this OP */
	isp_op_completion_event(pipeline, op);

	/* Notify dependencies of operation completion */
	isp_op_fire_signals(op);

	/*
	 * Lastly, put operation's refcount. Note that we cannot reliably
	 * isp_obj_deinit() here, because some other OP that is still blocked
	 * on some signals may be holding ref-count of this OP.
	 */
	isp_op_put(op);
}

static int isp_op_run_rw_instructions(struct isp_obj_op *op)
{
	struct isp_rw_instruction __user *payload;
	struct isp_rw_instruction insn;
	int ret;

	payload = op->exec_instruction_addr;
	if (payload == ISP_OP_NULL_PTR) {
		/* No execution payload. Mark it done. */
		return ISP_INSTRUCTION_EXEC_HANDLED;
	}

	/* At this point OPs require an entity to be run against */
	if (!op->exec_entity)
		return ISP_INSTRUCTION_EXEC_HANDLED;

	if (copy_from_user(&insn, payload, sizeof(insn))) {
		pr_err("Unable to access RW instruction\n");
		return ISP_INSTRUCTION_EXEC_HANDLED;
	}

	ret = -EINVAL;

	switch (insn.type) {
	case ISP_READ_INSTRUCTION:
		ret = isp_read_instruction(op, &insn.rd);
		break;
	case ISP_WRITE_INSTRUCTION:
		ret = isp_write_instruction(op, &insn.wr);
		break;
	case ISP_DMABUF_INSTRUCTION:
		ret = isp_run_dmabuf_instruction(op, &insn.db);
		break;
	case ISP_INSTANCE_INSTRUCTION:
		ret = isp_run_instance_instruction(op, &insn.in);
		break;
	case ISP_SIGNAL_FENCE_INSTRUCTION:
		ret = isp_run_signal_fence_instruction(op, &insn.sf);
		break;
	}

	if (ret < 0) {
		pr_devel("Operation execution error: %d\n", ret);
		op->exec_error = ret;
	}

	return ret;
}

/**
 * isp_op_run() - Execute the target operation
 * @op: pointer to ISP operation to be executed
 *
 * This is the place where an operation is being executed.
 */
static void isp_op_run(struct isp_obj_op *op)
{
	WARN_ON(!isp_op_set_state(op, ISP_OPERATION_STATE_RUNNING));

	if (op->delay_ns)
		ndelay(op->delay_ns);

	if (isp_op_run_rw_instructions(op) == ISP_INSTRUCTION_EXEC_DEFERRED)
		return;

	isp_op_run_complete(op);
}

static bool io_queue_status(struct isp_pipeline *pipeline)
{
	unsigned long flags;
	bool pending_ops;

	if (test_bit(ISP_PIPELINE_IO_EXITING, &pipeline->io_state))
		return true;
	if (signal_pending(current))
		return true;

	spin_lock_irqsave(&pipeline->io_comp_queue_lock, flags);
	pending_ops = !list_empty(&pipeline->io_comp_queue);
	spin_unlock_irqrestore(&pipeline->io_comp_queue_lock, flags);

	if (pending_ops)
		return true;

	spin_lock_irqsave(&pipeline->io_queue_lock, flags);
	pending_ops = !list_empty(&pipeline->io_queue);
	spin_unlock_irqrestore(&pipeline->io_queue_lock, flags);

	return pending_ops;
}

static void isp_pipeline_io_completions(struct isp_pipeline *pipeline)
{
	struct isp_obj_op *op;
	unsigned long flags;

	do {
		op = NULL;

		spin_lock_irqsave(&pipeline->io_comp_queue_lock, flags);
		if (!list_empty(&pipeline->io_comp_queue)) {
			op = list_first_entry(&pipeline->io_comp_queue,
					      struct isp_obj_op,
					      io_queue_entry);
			list_del(&op->io_queue_entry);
		}
		spin_unlock_irqrestore(&pipeline->io_comp_queue_lock, flags);

		if (op)
			isp_op_run_complete(op);
	} while (op);
}

/**
 * isp_pipeline_io_worker() - IO-thread worker that consumes the pipeline queue
 * @data: pointer to ISP pipeline
 *
 * This worker thread executes each ISP operations in the IO-queue while the
 * pipeline is active, and clean up the dangling events/operations/fences
 * once the IO-queue becomes empty or pipeline becomes inactive.
 */
static int isp_pipeline_io_worker(void *data)
{
	struct isp_pipeline *pipeline = data;
	char buf[TASK_COMM_LEN];
	unsigned long flags;

	snprintf(buf, sizeof(buf), "isp-io");
	set_task_comm(current, buf);

	while (!test_bit(ISP_PIPELINE_IO_EXITING, &pipeline->io_state)) {
		struct isp_obj_op *op = NULL;

		if (signal_pending(current)) {
			struct ksignal ksig;

			if (!get_signal(&ksig))
				continue;

			clear_bit(ISP_PIPELINE_IO_ACTIVE, &pipeline->io_state);
			break;
		}

		/*
		 * Flush previously completed operations that were executed
		 * in deferred contexts.
		 */
		isp_pipeline_io_completions(pipeline);

		spin_lock_irqsave(&pipeline->io_queue_lock, flags);
		if (!list_empty(&pipeline->io_queue)) {
			op = list_first_entry(&pipeline->io_queue,
					      struct isp_obj_op,
					      io_queue_entry);
			list_del(&op->io_queue_entry);
		}
		spin_unlock_irqrestore(&pipeline->io_queue_lock, flags);

		if (op) {
			isp_op_run(op);
		} else {
			trace_isp_io_worker_sleep(pipeline);
			wait_event_interruptible(pipeline->io_queue_wait,
						 io_queue_status(pipeline));
			trace_isp_io_worker_wakeup(pipeline);
		}
	}

	isp_drain_out_fences(pipeline);
	isp_drain_instances(pipeline);
	isp_drain_buffers(pipeline);
	isp_drain_ops(pipeline);

	mutex_lock(&pipeline->io_release_lock);
	pipeline->io_thread = NULL;
	mutex_unlock(&pipeline->io_release_lock);

	clear_bit(ISP_PIPELINE_IO_EXITING, &pipeline->io_state);
	do_exit(0);
	return 0;
}

/**
 * isp_fire_active_signals() - Raise all signals in an active chain
 * @notify_active_chain: operation notify chain with signals to be fired
 *
 * After firing the signals will be removed from the chain and released.
 */
void isp_fire_active_signals(struct list_head *notify_active_chain)
{
	struct isp_op_signal *sig, *safe;

	list_for_each_entry_safe(sig, safe, notify_active_chain, entry) {
		if (sig->instance != ISP_OP_NO_INSTANCE)
			continue;

		list_del_init(&sig->entry);
		sig->fire(sig);
	}
}

/**
 * isp_instance_fire_active_signals() - Raise signals that wait on instance
 * event
 * @instance: entity instance (context)
 * @notify_active_chain: operation notify chain with signals to be fired
 * @error: execution context error code, or 0 if success
 *
 * After firing the signals will be removed from the chain and released.
 */
void isp_instance_fire_active_signals(struct isp_obj_instance *instance,
				      struct list_head *notify_active_chain,
				      int error)
{
	struct isp_op_signal *sig, *safe;
	struct isp_pipeline *pipeline;
	struct isp_obj_op *op;

	/*
	 * This synchronizes with instance drain. If instance has ->private
	 * set then pipeline still exists and we can queue deferred OP
	 * completion.
	 *
	 * We don't need to disable local IRQs here.
	 */
	spin_lock(&instance->lock);
	op = instance->private;
	instance->private = NULL;

	/*
	 * If instance has no OP then we raised with pipeline destruction
	 * and there is nothing left to do (signaled are deactivated during
	 * OP drain)
	 */
	if (!op) {
		spin_unlock(&instance->lock);
		return;
	}

	list_for_each_entry_safe(sig, safe, notify_active_chain, entry) {
		if (sig->instance != isp_obj_id(&instance->nsobj))
			continue;

		list_del_init(&sig->entry);
		sig->fire(sig);
	}

	pipeline = op->pipeline;
	op->exec_error = error;

	/*
	 * Instances can fire signals from atomic context (e.g. driver
	 * IRQ), while operation completion in general requires a
	 * non-atomic context: we need to cleanup namespace, notify
	 * dependencies, notify exported objects, etc.
	 *
	 * Handle operation completion in a deferred context.
	 */
	spin_lock(&pipeline->io_comp_queue_lock);
	list_add_tail(&op->io_queue_entry, &pipeline->io_comp_queue);
	spin_unlock(&pipeline->io_comp_queue_lock);

	if (isp_pipeline_is_active(pipeline))
		wake_up(&pipeline->io_queue_wait);
	spin_unlock(&instance->lock);
}

/**
 * isp_pipeline_dequeue() - Dequeue an operation from the pipeline
 * @pipeline: pointer to ISP pipeline
 * @req: remove request from user-space
 *
 * This essentially marks the target operation to DELETED.
 *
 * Return: 0 on success or a negative error code otherwise.
 */
int isp_pipeline_dequeue(struct isp_pipeline *pipeline,
			 struct isp_operation_remove *req)
{
	struct isp_obj_op *op;

	if (!isp_pipeline_is_active(pipeline))
		return -EINVAL;

	op = isp_op_lookup(&pipeline->ops, req->id);
	if (!op)
		return -EINVAL;

	/*
	 * OP removal is not guaranteed to succeed. For instance if at this
	 * point OP is already QUEUED or EXECUTING then we won't be able
	 * to remove it, it's going to get executed.
	 */
	if (isp_drain_op(op)) {
		/* Let user-space know that we deleted the OP */
		isp_op_completion_event(pipeline, op);
	}

	/* Put lookup ref-count */
	isp_op_put(op);
	return 0;
}
ALLOW_ERROR_INJECTION(isp_pipeline_dequeue, ERRNO);

/**
 * isp_op_add_pending_signal() - Create and add a pending signal
 * @source: pointer to ISP object that blocks @target
 * @target: pointer to ISP operation that depends on @source
 * @instance: ID of entity instance
 * @activate: the callback called to activate the pending signal
 * @deactivate: the callback called to deactivate the active signal
 *
 * This allocate a pending ISP signal and append it to target's notify chain.
 * Note that the fire callback is always isp_op_notify() in the current design.
 *
 * Return: 0 on success or a negative error code otherwise.
 */
static int isp_op_add_pending_signal(struct isp_obj *source,
				     struct isp_obj_op *target,
				     u32 instance,
				     bool (*activate)(struct isp_op_signal *),
				     bool (*deactivate)(struct isp_op_signal *))
{
	struct isp_op_signal *sig;

	sig = kzalloc(sizeof(struct isp_op_signal), GFP_KERNEL);
	if (!sig)
		return -ENOMEM;

	sig->instance	= instance;
	sig->activate	= activate;
	sig->fire	= isp_op_notify;
	sig->deactivate	= deactivate;

	INIT_LIST_HEAD(&sig->entry);
	INIT_LIST_HEAD(&sig->notifiers_entry);

	/* Signal should keep both objects alive until it triggers */
	if (!isp_obj_get(source))
		goto error;
	sig->source	= source;

	if (!isp_obj_get(&target->nsobj))
		goto error;
	sig->target	= &target->nsobj;

	list_add_tail(&sig->entry, &target->notify_pending_chain);
	list_add_tail(&sig->notifiers_entry, &target->notifiers);
	atomic_inc(&target->num_blockers);
	trace_isp_signal_add_pending(sig);
	return 0;
error:
	if (sig->source)
		isp_obj_put(source);
	kfree(sig);
	return -EINVAL;
}

/**
 * isp_op_dependency_add() - Create OP-to-OP dependency
 * @pipeline: pointer to ISP pipeline
 * @req: add request from user-space
 * @op: pointer to the dependent operation
 *
 * Return: 0 on success or a negative error code otherwise.
 */
static int isp_op_dependency_add(struct isp_pipeline *pipeline,
				 struct isp_dependency *req,
				 struct isp_obj_op *op)
{
	struct isp_obj_op *dep_op;
	int ret;

	dep_op = isp_op_lookup(&pipeline->ops, req->id);
	if (!dep_op) {
		/*
		 * Unsatisfied OP dependencies are considered to be
		 * non-critical. This may turn this operation into
		 * instant if it has no other dependencies.
		 */
		return 0;
	}

	ret = isp_op_add_pending_signal(&dep_op->nsobj, op,
					ISP_OP_NO_INSTANCE,
					isp_op_activate_signal,
					isp_op_deactivate_signal);
	isp_op_put(dep_op);
	return ret;
}

/**
 * isp_event_dependency_add() - Create Event-to-OP dependency
 * @pipeline: pointer to ISP pipeline
 * @req: add request from user-space
 * @op: pointer to the dependent operation
 *
 * Return: 0 on success or a negative error code otherwise.
 */
static int isp_event_dependency_add(struct isp_pipeline *pipeline,
				    struct isp_dependency *req,
				    struct isp_obj_op *op)
{
	struct isp_obj_event *dep_event;
	u32 instance;
	int ret;

	dep_event = isp_event_lookup(pipeline->isp, req->id);
	if (!dep_event)
		return -EINVAL;

	instance = ISP_OP_NO_INSTANCE;
	if (op->exec_instance)
		instance = isp_obj_id(&op->exec_instance->nsobj);

	ret = isp_op_add_pending_signal(&dep_event->nsobj, op,
					instance,
					isp_event_activate_signal,
					isp_event_deactivate_signal);
	isp_event_put(dep_event);
	return ret;
}

/**
 * isp_fence_in_dependency_add() - Create In-Fence-to-OP dependency
 * @pipeline: pointer to ISP pipeline
 * @req: add request from user-space
 * @op: pointer to the dependent operation
 *
 * Return: 0 on success or a negative error code otherwise.
 */
static int isp_fence_in_dependency_add(struct isp_pipeline *pipeline,
				       struct isp_dependency *req,
				       struct isp_obj_op *op)
{
	struct isp_obj_fence *sf;
	int ret;

	/* Imported fences are linked to their OP */
	sf = isp_in_fence_register(pipeline->isp, op, req->id,
				   "in-fence-%d", req->id);
	if (!sf)
		return -EINVAL;

	ret = isp_op_add_pending_signal(&sf->nsobj, op,
					ISP_OP_NO_INSTANCE,
					isp_in_fence_activate_signal,
					isp_in_fence_deactivate_signal);
	return ret;
}

/**
 * isp_export_fence_instruction() - Export fence (out-fence)
 * @op: pointer to the dependent operation
 * @insn: struct that will hold ID of exported fence
 *
 * Return: 0 on success or a negative error code otherwise.
 */
static int
isp_export_fence_instruction(struct isp_obj_op *op,
			     struct isp_export_fence_instruction *insn)
{
	struct isp_pipeline *pipeline = op->pipeline;
	struct isp_obj_fence *sf;

	insn->id = ISP_OP_NO_FENCE;
	/* Exported fences are pipeline objects, not linked to any OPs */
	sf = isp_out_fence_register(&pipeline->objs, "out-fence-%d",
				    isp_obj_id(&op->nsobj));
	if (!sf)
		return -EINVAL;

	insn->id = isp_out_fence_fd(sf);
	return 0;
}

/**
 * isp_activate_strict_dependency_mode() - Activate signal in STRICT mode
 * @op: pointer to ISP operation to activate the signal from
 *
 * This activates the pending signal of an operation in STRICT mode i.e.
 * we only activate one signal at a time from the notify chain.
 *
 * Return: True on successful signal activation or false otherwise.
 */
static bool isp_activate_strict_dependency_mode(struct isp_obj_op *op)
{
	bool activated = false;
	int ret;

	/*
	 * In STRICT mode we pick first pending signal and try to
	 * activate it (move it into active notify list).
	 */
	do {
		ret = isp_op_activate_pending_signal(op);

		if (ret == ISP_OP_PENDING_SIGNAL_NONE)
			break;
		if (ret == ISP_OP_PENDING_SIGNAL_ACTIVATED) {
			activated = true;
			break;
		}
		/*
		 * Carry on until we either have a successfully
		 * activated signal or no remaining pending signals.
		 */
	} while (ret == ISP_OP_PENDING_SIGNAL_FAILURE);

	/*
	 * We did not activate any of the pending signals (if there were any).
	 * Sanity check that ->num_blockers is zero.
	 */
	if (!activated)
		WARN_ON(atomic_read(&op->num_blockers) != 0);
	return activated;
}

/**
 * isp_activate_weak_dependency_mode() - Activate signal in WEAK mode
 * @op: pointer to ISP operation that signal is activated from
 *
 * This activates the pending signal of an operation in weak mode i.e.
 * we activate as many signals as possible by walking through the notify chain.
 *
 * Return: True on any successful signal activation or false otherwise.
 */
static bool isp_activate_weak_dependency_mode(struct isp_obj_op *op)
{
	bool activated = false;
	int ret;

	/*
	 * In WEAK mode we activate all pending signals (if any).
	 * If we fail to activate any of the signals (e.g. unsatisfied
	 * dependency) we just continue.
	 */
	do {
		ret = isp_op_activate_pending_signal(op);

		if (ret == ISP_OP_PENDING_SIGNAL_ACTIVATED)
			activated = true;
		/*
		 * Carry on until there is no remaining pending signals.
		 */
	} while (ret != ISP_OP_PENDING_SIGNAL_NONE);

	/*
	 * Ditto. We did not activate any of the pending signals (if there
	 * were any). Sanity check that ->num_blockers is zero.
	 */
	if (!activated)
		WARN_ON(atomic_read(&op->num_blockers) != 0);
	return activated;
}

/**
 * isp_op_instruction_add() - Add RW instructions to an operation
 * @pipeline: pointer to ISP pipeline
 * @req: add request from user-space
 * @op: pointer to ISP operation that the instructions are adding to
 *
 * This copies the register read/write instructions from the user-space request
 * to the target operation.
 *
 * Return: 0 on success or a negative error code otherwise.
 */
static int isp_op_instruction_add(struct isp_pipeline *pipeline,
				  struct isp_operation_add *req,
				  struct isp_obj_op *op)
{
	op->delay_ns			= req->delay_ns;
	op->exec_instruction_addr	= (void *)ISP_OP_NULL_PTR;
	op->exec_entity			= NULL;
	op->exec_instance		= NULL;
	op->exec_error			= 0;

	if (req->entity == ISP_OP_NO_ENTITY &&
	    req->instruction != ISP_OP_NULL_PTR)
		return -EINVAL;

	if (req->instruction != ISP_OP_NULL_PTR) {
		struct isp_rw_instruction insn;
		uintptr_t __user *addr;

		addr = u64_to_user_ptr(req->instruction);
		op->exec_instruction_addr = addr;
		/* Make sure we can access instruction */
		if (copy_from_user(&insn, addr, sizeof(insn)))
			goto error;
	}

	if (req->entity != ISP_OP_NO_ENTITY) {
		op->exec_entity = isp_entity_lookup(pipeline->isp,
						    req->entity);
		if (!op->exec_entity)
			goto error;
	}

	if (req->instance != ISP_OP_NO_INSTANCE) {
		if (!op->exec_entity)
			goto error;

		op->exec_instance = isp_instance_lookup(&pipeline->objs,
							req->instance);
		if (!op->exec_instance)
			goto error;

		if (!isp_instance_verify(op->exec_entity, op->exec_instance))
			goto error;
	}

	return 0;

error:
	return -EINVAL;
}

static int isp_op_prepare_rw_instruction(struct isp_obj_op *op)
{
	struct isp_rw_instruction __user *payload;
	struct isp_rw_instruction insn;
	int ret;

	payload = op->exec_instruction_addr;
	if (payload == ISP_OP_NULL_PTR)
		return 0;

	if (copy_from_user(&insn, payload, sizeof(insn))) {
		pr_err("Unable to access RW instruction\n");
		return -EFAULT;
	}

	ret = 0;
	switch (insn.type) {
	case ISP_DMABUF_INSTRUCTION:
		ret = isp_prepare_dmabuf_instruction(op, &insn.db);
		break;
	case ISP_INSTANCE_INSTRUCTION:
		ret = isp_prepare_instance_instruction(op, &insn.in);
		break;
	case ISP_EXPORT_FENCE_INSTRUCTION:
		ret = isp_export_fence_instruction(op, &insn.ef);
		ret |= copy_to_user(payload, &insn, sizeof(insn));
		break;
	}

	if (ret < 0) {
		pr_devel("Failed instruction at prepare stage: %d\n", ret);
		isp_op_set_rw_instruction_error(op, ret);
	}

	return ret;
}

/**
 * isp_pipeline_enqueue_prepare() - Create an operation
 * @pipeline: pointer to ISP pipeline
 * @req: add request from user-space
 *
 * This creates an operation and adds dependencies to it based on the
 * user-space request. The operation is not enqueued at this point as
 * this is only a preparation step.
 *
 * This is performed for all operations in the batch, because we want to
 * setup execution context for the batch: create/request entity instances,
 * import DMA buffers, etc. If we fail at any point then entire operations
 * batch is discarded.
 *
 * Return: 0 on success or a negative error code otherwise.
 */
int isp_pipeline_enqueue_prepare(struct isp_pipeline *pipeline,
				 struct isp_operation_add *req)
{
	struct isp_obj_op *op;
	int i, ret;

	op = kzalloc(sizeof(struct isp_obj_op), GFP_KERNEL);
	if (!op)
		return -ENOMEM;

	isp_obj_init(&op->nsobj, ISP_OBJ_TYPE_OPERATION,
		     isp_op_release, &pipeline->ops);
	isp_obj_set_id(&op->nsobj, req->id);

	atomic_set(&op->num_blockers, 0);
	INIT_LIST_HEAD(&op->notify_active_chain);
	INIT_LIST_HEAD(&op->notify_pending_chain);
	INIT_LIST_HEAD(&op->notifiers);
	INIT_LIST_HEAD(&op->io_queue_entry);
	rwlock_init(&op->notify_lock);
	op->pipeline = pipeline;
	isp_op_set_state(op, ISP_OPERATION_STATE_SLEEP);

	/*
	 * Execution context (driver) data.
	 */
	if (isp_op_instruction_add(pipeline, req, op)) {
		isp_op_release(&op->nsobj);
		return -EINVAL;
	}

	if (isp_obj_insert(&op->nsobj)) {
		isp_op_release(&op->nsobj);
		return -EINVAL;
	}

	ret = 0;
	/*
	 * This adds all dependencies (if any) into operation's pending list.
	 * None are activated at this point. We do it this way because some
	 * signals are raised from the IRQ context and we don't want to
	 * kmalloc() anything from atomic context. So we allocate all signal
	 * structs now, put them on the pending list (of signal target, which
	 * is current operation) then move them to the active list (of signal
	 * source, which object that we depend on) when needed.
	 *
	 * This has one extra benefit: if something fails at this point
	 * rollback is pretty simple, because all dependencies are inactive
	 * (on the pending list) so we won't race against the external events.
	 */
	for (i = 0; i < ISP_MAX_DEPENDENCIES; i++) {
		struct isp_dependency *dep = &req->deps[i];

		if (dep->type == ISP_DEPENDENCY_NONE)
			break;

		switch (dep->type) {
		case ISP_DEPENDENCY_OP:
			ret = isp_op_dependency_add(pipeline, dep, op);
			break;
		case ISP_DEPENDENCY_EVENT:
			ret = isp_event_dependency_add(pipeline, dep, op);
			break;
		case ISP_DEPENDENCY_FENCE:
			ret = isp_fence_in_dependency_add(pipeline, dep, op);
			break;
		}

		if (ret)
			break;
	}

	if (!ret)
		ret = isp_op_prepare_rw_instruction(op);

	if (!ret)
		trace_isp_operation_add(op);
	return ret;
}
ALLOW_ERROR_INJECTION(isp_pipeline_enqueue_prepare, ERRNO);

/**
 * isp_pipeline_enqueue_submit() - Submit an operation
 * @pipeline: pointer to ISP pipeline
 * @req: add request from user-space
 *
 * This enqueues operation for execution if it has no blockers.
 *
 * Return: 0 on success or a negative error code otherwise.
 */
int isp_pipeline_enqueue_submit(struct isp_pipeline *pipeline,
				struct isp_operation_add *req)
{
	struct isp_obj_op *op;
	bool execute = false;

	/* Check pipeline status as late as possible */
	if (!isp_pipeline_is_active(pipeline))
		return -EINVAL;

	op = isp_op_lookup(&pipeline->ops, req->id);
	if (!op)
		return -EINVAL;

	/*
	 * Based on the dependency mode this will attempt to activate required
	 * number of pending signals. If nothing is activated then we convert
	 * this operation into immediate and queue it for execution.
	 */
	if (req->mode == ISP_DEPENDENCY_STRICT_ORDER) {
		if (!isp_activate_strict_dependency_mode(op))
			execute = true;
	} else {
		if (!isp_activate_weak_dependency_mode(op))
			execute = true;
	}

	/*
	 * Not blocked on any signals. Note, the object may already be in
	 * ISP_OPERATION_STATE_DELETED at this point.
	 */
	if (execute)
		isp_op_enqueue(op);

	isp_op_put(op);
	return 0;
}
ALLOW_ERROR_INJECTION(isp_pipeline_enqueue_submit, ERRNO);

static void isp_cancel_dmabuf_instruction(struct isp_obj_op *op,
					  struct isp_dmabuf_instruction *insn)
{
	struct isp_pipeline *pipeline = op->pipeline;

	if (insn->op == ISP_OP_DMABUF_REMOVE)
		return;

	isp_buffer_unregister(&pipeline->objs, insn->buf_id);
}

static void
isp_cancel_instance_instruction(struct isp_obj_op *op,
				struct isp_instance_instruction *insn)
{
	struct isp_pipeline *pipeline = op->pipeline;

	if (insn->op == ISP_OP_INSTANCE_DESTROY)
		return;

	isp_instance_destroy(&pipeline->objs, insn->id);
}

static void isp_op_cancel_rw_instruction(struct isp_obj_op *op)
{
	struct isp_rw_instruction __user *payload;
	struct isp_rw_instruction insn;

	payload = op->exec_instruction_addr;
	if (payload == ISP_OP_NULL_PTR)
		return;

	if (copy_from_user(&insn, payload, sizeof(insn))) {
		pr_err("Unable to access RW instruction\n");
		return;
	}

	switch (insn.type) {
	case ISP_DMABUF_INSTRUCTION:
		isp_cancel_dmabuf_instruction(op, &insn.db);
		break;
	case ISP_INSTANCE_INSTRUCTION:
		isp_cancel_instance_instruction(op, &insn.in);
		break;
	}
}

/**
 * isp_pipeline_enqueue_cancel() - Cancel an operation
 * @pipeline: pointer to ISP pipeline
 * @req: add request from user-space
 *
 * This drains an operation which we failed to prepare.
 *
 * Return: 0 on success or a negative error code otherwise.
 */
int isp_pipeline_enqueue_cancel(struct isp_pipeline *pipeline,
				struct isp_operation_add *req)
{
	struct isp_obj_op *op = isp_op_lookup(&pipeline->ops, req->id);

	if (!op)
		return 0;

	isp_op_set_state(op, ISP_OPERATION_STATE_DELETED);
	isp_op_cancel_rw_instruction(op);
	isp_drain_op_signals(op);
	isp_drain_op_fences(op);
	/* drop lookup ref-count */
	isp_op_put(op);
	/* Now release the object */
	isp_obj_remove(&op->nsobj);
	isp_obj_deinit(&op->nsobj);
	return 0;
}

static bool isp_op_enum(struct isp_obj_op *op, struct isp_koutput *output)
{
	struct isp_query_operation_entry *qent;
	unsigned long flags;
	u32 state;

	/* User just want the size, not the data. */
	if (!isp_output_has_buffer(output))
		goto out;

	isp_output_next_entry(output, qent);
	if (!qent)
		return false;

	if (__put_user(isp_obj_id(&op->nsobj), &qent->id))
		return false;

	/*
	 * State and num_blockers can become obsolete by the time we
	 * finish enumeration
	 */
	read_lock_irqsave(&op->notify_lock, flags);
	state = op->state;
	read_unlock_irqrestore(&op->notify_lock, flags);

	if (__put_user(state, &qent->state))
		return false;

	if (__put_user(atomic_read(&op->num_blockers), &qent->num_blockers))
		return false;

out:
	output->num_entries++;
	return true;
}

static bool isp_ns_walk_callback(struct isp_obj *nsobj,
				 struct isp_ns_walk_control *ctl)
{
	struct isp_obj_op *op;

	op = nsobj_to_isp_op(nsobj);
	if (WARN_ON(!op))
		return true;

	if (!isp_op_enum(op, ctl->output))
		return true;

	return false;
}

/**
 * isp_enum_operations() - Response to user-space operation queries
 * @pipeline: pointer to ISP pipeline
 * @query: query request from user-space
 * @output: pointer to user-space buffer for the output
 *
 * This handles different user-space operation queries based on the query mode.
 * Check @isp_operation_query_mode in UAPI for supported modes.
 *
 * Return: 0 on success or a negative error code otherwise.
 */
int isp_enum_operations(struct isp_pipeline *pipeline,
			struct isp_query_operations *query,
			struct isp_koutput *output)
{
	query->num_ops = 0;

	if (query->mode == ISP_OP_QUERY_UNIQUE) {
		struct isp_obj_op *op;

		op = isp_op_lookup(&pipeline->ops, query->id);
		if (!op)
			return -ENOENT;

		isp_op_enum(op, output);
		isp_op_put(op);

		query->num_ops = output->num_entries;
		return 0;
	}

	if (query->mode == ISP_OP_QUERY_ALL) {
		struct isp_ns_walk_control ctl = {};

		ctl.output	= output;
		ctl.cb		= isp_ns_walk_callback;
		isp_ns_for_each(&pipeline->ops, &ctl);

		query->num_ops = output->num_entries;
		return 0;
	}

	return -EINVAL;
}
ALLOW_ERROR_INJECTION(isp_enum_operations, ERRNO);

/**
 * isp_pipeline_destroy() - Destroy ISP execution pipeline
 * @pipeline: pointer to ISP pipeline
 *
 * We don't release the pipeline because it's an embedded member of isp_device.
 */
void isp_pipeline_destroy(struct isp_pipeline *pipeline)
{
	isp_ringbuffer_release(&pipeline->event_buffer);
	isp_ns_release(&pipeline->ops);
	isp_ns_release(&pipeline->objs);
}

/**
 * isp_pipeline_io_setup() - Set up IO-thread in pipeline
 * @pipeline: pointer to ISP pipeline
 *
 * Return: 0 on success or a negative error code otherwise.
 */
int isp_pipeline_io_setup(struct isp_pipeline *pipeline)
{
	if (test_bit(ISP_PIPELINE_IO_ACTIVE, &pipeline->io_state) ||
	    pipeline->io_thread) {
		pr_err("I/O thread already setup\n");
		return -EINVAL;
	}

	pipeline->io_thread = create_io_thread(isp_pipeline_io_worker,
					       pipeline,
					       NUMA_NO_NODE);
	if (IS_ERR(pipeline->io_thread)) {
		pipeline->io_thread = NULL;
		return -EINVAL;
	}

	init_waitqueue_head(&pipeline->io_queue_wait);
	pipeline->io_thread->flags |= PF_NO_SETAFFINITY;
	set_bit(ISP_PIPELINE_IO_ACTIVE, &pipeline->io_state);
	wake_up_new_task(pipeline->io_thread);
	return 0;
}

/**
 * isp_pipeline_io_release() - Release IO-thread in ISP pipeline
 * @pipeline: pointer to ISP pipeline
 *
 * Return: 0 on success or a negative error code otherwise.
 */
int isp_pipeline_io_release(struct isp_pipeline *pipeline)
{
	set_bit(ISP_PIPELINE_IO_EXITING, &pipeline->io_state);
	if (!test_bit(ISP_PIPELINE_IO_ACTIVE, &pipeline->io_state))
		return 0;

	mutex_lock(&pipeline->io_release_lock);
	if (pipeline->io_thread)
		wake_up(&pipeline->io_queue_wait);
	mutex_unlock(&pipeline->io_release_lock);

	while (test_bit(ISP_PIPELINE_IO_EXITING, &pipeline->io_state))
		schedule_timeout(HZ / 10);
	return 0;
}

/**
 * isp_pipeline_init() - Initialize ISP execution pipeline
 * @isp: pointer to ISP device
 * @pipeline: pointer to ISP pipeline which belongs to the provided ISP device
 *
 * Return: 0 on success or a negative error code otherwise.
 */
int isp_pipeline_init(struct isp_device *isp, struct isp_pipeline *pipeline)
{
	int ret;

	ret = isp_ns_init(&pipeline->ops, ISP_NS_POL_USER_ID);
	if (ret)
		return ret;

	ret = isp_ns_init(&pipeline->objs, ISP_NS_POL_USER_ID);
	if (ret) {
		isp_ns_release(&pipeline->ops);
		return ret;
	}

	ret = isp_ringbuffer_init(&pipeline->event_buffer, ISP_RINGBUFFER_SIZE);
	if (ret) {
		isp_ns_release(&pipeline->ops);
		isp_ns_release(&pipeline->objs);
		return ret;
	}

	INIT_LIST_HEAD(&pipeline->io_queue);
	spin_lock_init(&pipeline->io_queue_lock);
	INIT_LIST_HEAD(&pipeline->io_comp_queue);
	spin_lock_init(&pipeline->io_comp_queue_lock);
	mutex_init(&pipeline->io_release_lock);
	pipeline->io_thread = NULL;
	pipeline->isp = isp;
	pipeline->id = atomic_inc_return(&pipeline_count);
	return ret;
}
