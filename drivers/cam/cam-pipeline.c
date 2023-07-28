// SPDX-License-Identifier: GPL-2.0
/*
 * CAM requests executor
 *
 * Copyright (C) Google LLC
 */

#define pr_fmt(fmt) "cam-pipeline: " fmt

#include <linux/cam/cam-buffer.h>
#include <linux/cam/cam-device.h>
#include <linux/cam/cam-entity.h>
#include <linux/cam/cam-graph.h>
#include <linux/cam/cam-output.h>
#include <linux/cam/cam-pipeline.h>
#include <linux/cam/cam-ringbuffer.h>
#include <linux/cam/cam-syncfile.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/atomic.h>

#include <trace/events/cam.h>

#include <uapi/linux/cam.h>

/*
 * a counter used to assign a unique ID to each pipeline
 * we ignore the case of overflow, so it should only be used for debugging
 */
static atomic_t pipeline_count = ATOMIC_INIT(1);

/**
 * cam_pipeline_is_active() - Check whether the execution pipeline is active
 * @pipeline: pointer to CAM pipeline
 *
 * Return: true if the pipeline is active, or false otherwise.
 */
static bool cam_pipeline_is_active(struct cam_pipeline *pipeline)
{
	if (test_bit(CAM_PIPELINE_IO_EXITING, &pipeline->io_state))
		return false;
	if (!test_bit(CAM_PIPELINE_IO_ACTIVE, &pipeline->io_state))
		return false;
	return true;
}

static struct cam_obj_op *nsobj_to_cam_op(struct cam_obj *nsobj)
{
	/* Should never happen */
	if (!cam_obj_check_type(nsobj, CAM_OBJ_TYPE_OPERATION))
		return NULL;

	return container_of(nsobj, struct cam_obj_op, nsobj);
}

static struct cam_obj_op *cam_op_lookup(struct cam_ns *ns, u32 id)
{
	struct cam_obj *nsobj;

	nsobj = cam_obj_lookup(ns, CAM_OBJ_TYPE_OPERATION, id);
	if (!nsobj)
		return NULL;

	return nsobj_to_cam_op(nsobj);
}

static void cam_op_put(struct cam_obj_op *op)
{
	if (likely(op))
		cam_obj_put(&op->nsobj);
	else
		WARN_ON(1);
}

/**
 * release_signal() - Release the signal
 * @sig: pointer to CAM signal
 */
static void release_signal(struct cam_op_signal *sig)
{
	/*
	 * NOTE that for both source and target a signal is considered to
	 * be consumed and, hence, target and source ref-counters can be
	 * dropped. Note that this can be the final put for source.
	 */
	cam_obj_put(sig->source);
	cam_obj_put(sig->target);
	kfree(sig);
}

/**
 * cam_op_release() - Release CAM operation
 * @nsobj: pointer to CAM object that represents a CAM operation
 *
 * Release a CAM operation.
 * If the operation has any post actions registered, then execute them before
 * releasing.
 */
static void cam_op_release(struct cam_obj *nsobj)
{
	struct cam_obj_op *op = nsobj_to_cam_op(nsobj);

	WARN_ON(!list_empty(&op->notify_active_chain));
	WARN_ON(!list_empty(&op->notify_pending_chain));
	WARN_ON(!list_empty(&op->notifiers));

	if (op->exec_entity)
		cam_entity_put(op->exec_entity);
	if (op->exec_instance)
		cam_instance_put(op->exec_instance);
	kfree(op);
}

/**
 * cam_op_set_state() - Set CAM operation to a new state
 * @op: pointer to CAM operation
 * @new_state: the new state of the target operation
 *
 * This handles the state transfer of a CAM operation.
 * Note that it's not allowed to roll back an operation's state:
 * For example the transfer will fail if the operation is already executed or
 * deleted.
 * Also, it's not allowed to delete a running operation.
 *
 * Return: True if the operation is set to the new state, or false otherwise.
 */
static bool cam_op_set_state(struct cam_obj_op *op,
			     enum cam_operation_state new_state)
{
	unsigned long flags;
	bool ret = false;

	write_lock_irqsave(&op->notify_lock, flags);
	/* Cannot do anything with this object */
	if (op->state == CAM_OPERATION_STATE_DELETED)
		goto out;
	/* Too late to delete it */
	if (op->state == CAM_OPERATION_STATE_EXECUTED)
		goto out;
	/* This ship has sailed. Too late to delete this object. */
	if ((op->state == CAM_OPERATION_STATE_RUNNING ||
	     op->state == CAM_OPERATION_STATE_QUEUED) &&
	    new_state == CAM_OPERATION_STATE_DELETED)
		goto out;
	/* Do not go backwards */
	if (WARN_ON(op->state > new_state))
		goto out;

	op->state = new_state;
	trace_cam_operation_set_state(op);
	ret = true;
out:
	write_unlock_irqrestore(&op->notify_lock, flags);
	return ret;
}

/**
 * pipeline_walk() - Walk through the dependency graph
 * @nsobj: pointer to CAM object that represents the operation to start with
 * @ctl: auxiliary data
 *
 * This walks through the operation dependency graph and executes the provided
 * callbacks on the root operation and all its dependencies.
 * It's also possible to apply the callback on the root operation only by
 * setting CAM_GRAPH_WALK_ONESHOT in the control flag.
 *
 * Return: 0 on success or error value otherwise.
 */
static int pipeline_walk(struct cam_obj *nsobj, struct cam_graph_walk *ctl)
{
	struct cam_graph_stack st;
	unsigned long flags;
	bool abort;
	int ret;

	ret = cam_graph_stack_alloc(&st, CAM_GRAPH_STACK_DEPTH);
	if (ret)
		return ret;

	if (!cam_obj_get(nsobj)) {
		ret = -EINVAL;
		goto out;
	}

	cam_graph_stack_push(&st, nsobj);
	abort = false;

	while (!abort && !cam_graph_stack_empty(&st)) {
		struct cam_obj_op *op;
		struct cam_op_signal *psig;

		nsobj = cam_graph_stack_front(&st);
		cam_graph_stack_pop(&st);

		if (!ctl->cb(nsobj, ctl)) {
			ret = -EFAULT;
			abort = true;
		}

		if (ctl->flags & CAM_GRAPH_WALK_ONESHOT) {
			ret = 0;
			abort = true;
		}

		op = nsobj_to_cam_op(nsobj);
		/* Should never happen */
		WARN_ON(!op);

		read_lock_irqsave(&op->notify_lock, flags);
		list_for_each_entry(psig, &op->notify_active_chain, entry) {
			if (abort)
				continue;
			if (!cam_obj_get(psig->target))
				continue;
			ret = cam_graph_stack_push(&st, psig->target);
			if (ret) {
				cam_obj_put(psig->target);
				abort = true;
			}
		}
		read_unlock_irqrestore(&op->notify_lock, flags);
		cam_obj_put(nsobj);
	}

out:
	while (!cam_graph_stack_empty(&st)) {
		cam_obj_put(cam_graph_stack_front(&st));
		cam_graph_stack_pop(&st);
	}
	cam_graph_stack_free(&st);
	return ret;
}

/**
 * cam_op_enqueue() - Enqueue an operation
 * @op: pointer to CAM operation
 *
 * This transitions the operation state to QUEUED, add it to IO thread and then
 * wakes up the thread.
 */
static void cam_op_enqueue(struct cam_obj_op *op)
{
	struct cam_pipeline *pipeline;
	unsigned long flags;

	/*
	 * This is where operation enqueuing (and execution) is synchronized
	 * with operation removal. If we are not able to set operation state
	 * to QUEUED then we lost the race against operation removal.
	 *
	 * Consequentially if we successfully set operation to QUEUED then
	 * operation removal should never succeed.
	 */
	if (!cam_op_set_state(op, CAM_OPERATION_STATE_QUEUED))
		return;

	pipeline = op->pipeline;
	spin_lock_irqsave(&pipeline->io_queue_lock, flags);
	list_add_tail(&op->io_queue_entry, &pipeline->io_queue);
	spin_unlock_irqrestore(&pipeline->io_queue_lock, flags);

	if (cam_pipeline_is_active(pipeline))
		wake_up(&pipeline->io_queue_wait);
}

enum {
	CAM_OP_PENDING_SIGNAL_NONE,
	CAM_OP_PENDING_SIGNAL_ACTIVATED,
	CAM_OP_PENDING_SIGNAL_FAILURE,
};

/**
 * cam_op_activate_pending_signal() - Activate the first pending signal in the
 * pending chain of an operation
 * @op: pointer to CAM operation to activate the signal from
 *
 * Return:
 *   CAM_OP_PENDING_SIGNAL_ACTIVATED when the signal is activated;
 *   CAM_OP_PENDING_SIGNAL_NONE when the pending chain is empty;
 *   CAM_OP_PENDING_SIGNAL_FAILURE when the signal failed to be activated.
 */
static int cam_op_activate_pending_signal(struct cam_obj_op *op)
{
	struct cam_op_signal *sig;
	unsigned long flags;
	bool ret;

	write_lock_irqsave(&op->notify_lock, flags);
	if (list_empty(&op->notify_pending_chain)) {
		write_unlock_irqrestore(&op->notify_lock, flags);
		return CAM_OP_PENDING_SIGNAL_NONE;
	}

	sig = list_first_entry(&op->notify_pending_chain,
			       struct cam_op_signal,
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
		return CAM_OP_PENDING_SIGNAL_FAILURE;
	}
	trace_cam_signal_add_active(sig);
	return CAM_OP_PENDING_SIGNAL_ACTIVATED;
}

/**
 * cam_op_notify() - Notify the signal target
 * @sig: pointer to CAM signal
 *
 * This notifies the signal target, which is an operation, and possibly
 * enqueues it if it's no longer being blocked on any dependencies.
 *
 * Return: True if the target operation is enqueued, or false otherwise.
 */
static bool cam_op_notify(struct cam_op_signal *sig)
{
	struct cam_obj_op *op;
	bool execute;
	int ret;

	op = nsobj_to_cam_op(sig->target);
	if (!op)
		return false;

	trace_cam_signal_fire_active(sig);
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
		ret = cam_op_activate_pending_signal(op);

		if (ret == CAM_OP_PENDING_SIGNAL_NONE)
			break;
		if (ret == CAM_OP_PENDING_SIGNAL_ACTIVATED)
			break;

		execute = (atomic_read(&op->num_blockers) == 0);
	} while (ret == CAM_OP_PENDING_SIGNAL_FAILURE);

	if (execute)
		cam_op_enqueue(op);
	return execute;
}

/**
 * cam_fire_active_signals() - Raise all signals in an active chain
 * @notify_active_chain: operation notify chain with signals to be fired
 *
 * After firing the signals will be removed from the chain and released.
 */
void cam_fire_active_signals(struct list_head *notify_active_chain)
{
	struct cam_op_signal *sig, *safe;

	list_for_each_entry_safe(sig, safe, notify_active_chain, entry) {
		if (sig->instance != CAM_OP_NO_INSTANCE)
			continue;

		list_del_init(&sig->entry);
		sig->fire(sig);
	}
}

/**
 * cam_instance_fire_active_signals() - Raise signals that wait on instance
 * event
 * @instance: entity instance (context)
 * @notify_active_chain: operation notify chain with signals to be fired
 *
 * After firing the signals will be removed from the chain and released.
 */
void cam_instance_fire_active_signals(struct cam_obj_instance *instance,
				      struct list_head *notify_active_chain)
{
	struct cam_op_signal *sig, *safe;

	list_for_each_entry_safe(sig, safe, notify_active_chain, entry) {
		if (sig->instance != cam_obj_id(&instance->nsobj))
			continue;

		list_del_init(&sig->entry);
		sig->fire(sig);
	}
}

static void cam_drain_op_syncfiles(struct cam_obj_op *op)
{
	struct cam_obj *link;
	struct cam_obj *save;

	cam_obj_for_each_link_safe(link, save, &op->nsobj) {
		switch (cam_obj_type(link)) {
		case CAM_OBJ_TYPE_IN_SYNCFILE:
			cam_in_syncfile_unregister(link);
			break;
		case CAM_OBJ_TYPE_OUT_SYNCFILE:
			cam_fire_out_syncfile_signal(link);
			cam_out_syncfile_unregister(link);
			break;
		default:
			pr_err("Unknown link object type: %d\n",
			       cam_obj_type(link));
		}
	}
}

/**
 * cam_op_destroy_signals() - Release all signals that operation owns
 * @op: operation to drain
 */
static void cam_op_destroy_signals(struct cam_obj_op *op)
{
	struct cam_op_signal *sig, *safe;

	list_for_each_entry_safe(sig, safe, &op->notifiers, notifiers_entry) {
		list_del_init(&sig->notifiers_entry);
		release_signal(sig);
	}
}

/**
 * cam_drain_op_signals() - Drains operation signals. This also includes
 * deactivation of already activated signals (without raising them).
 * @op: operation to drain
 */
static void cam_drain_op_signals(struct cam_obj_op *op)
{
	struct cam_op_signal *sig, *safe;
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
	cam_op_destroy_signals(op);
}

/**
 * cam_drain_op() - Drains a single operation.
 * @op: operation to drain
 *
 * Return: true on success or false otherwise.
 */
static bool cam_drain_op(struct cam_obj_op *op)
{
	/*
	 * This is where operation removal is synchronized with operation
	 * enqueuing and execution. If we are not able to set operation state
	 * to DELETED then we lost the race against enqueue path and should
	 * let operation to execute. Consequentially if we successfully set
	 * operation to DELETE then operation enqueuing/execution should never
	 * occur.
	 */
	if (!cam_op_set_state(op, CAM_OPERATION_STATE_DELETED))
		return false;

	cam_drain_op_signals(op);
	cam_drain_op_syncfiles(op);
	cam_obj_remove(&op->nsobj);
	/*
	 * We cannot deinit OP nsobj at this point, as we still
	 * may have other operations in the objs_list that hold
	 * reference to this OP (dependency, etc.)
	 */
	cam_op_put(op);
	return true;
}

/**
 * cam_drain_ops() - Drains operations from the pipeline. This must be used
 * only from the pipeline (emergency) termination path.
 * @pipeline: pointer to CAM pipeline
 */
static void cam_drain_ops(struct cam_pipeline *pipeline)
{
	struct cam_obj *nsobj;
	struct cam_obj *save;

	cam_ns_for_each_obj_safe(nsobj, save, &pipeline->ops) {
		struct cam_obj_op *op;

		op = nsobj_to_cam_op(nsobj);
		if (WARN_ON(!op))
			return;

		cam_drain_op(op);
	}
}

/*
 * cam_op_fire_signals() - Signal the operations blocked on the input operation
 * @op: pointer to CAM operation
 *
 * This walks the notify_active_chain and raises signals for all pipeline
 * cam_op objects that are blocked on us.
 */
static void cam_op_fire_signals(struct cam_obj_op *op)
{
	/*
	 * We are in CAM_OPERATION_STATE_EXECUTED, no new signals can be
	 * registered. So we should be fine without taking the notify_lock
	 * here.
	 *
	 * Famous last words.
	 */
	cam_fire_active_signals(&op->notify_active_chain);
	cam_op_destroy_signals(op);
	cam_drain_op_syncfiles(op);
}

/**
 * cam_op_activate_signal() - Activate a pending signal
 * @sig: pointer to CAM signal where its source is an operation
 *
 * This activates the signal by appending it to source operation's notify
 * chain.
 * Unlike cam_op_activate_pending_signal(), which is where sig->activate() is
 * called, this is the actual callback that does the work and to be fed into
 * cam_op_add_pending_signal() to construct the CAM signal object.
 *
 * Return: True on success, or false otherwise e.g. the source operation is
 * already EXECUTED or DELETED.
 */
static bool cam_op_activate_signal(struct cam_op_signal *sig)
{
	struct cam_obj_op *source;
	unsigned long flags;
	bool ret;

	source = nsobj_to_cam_op(sig->source);
	if (WARN_ON(!source))
		return false;

	write_lock_irqsave(&source->notify_lock, flags);
	if (source->state == CAM_OPERATION_STATE_EXECUTED ||
	    source->state == CAM_OPERATION_STATE_DELETED) {
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

static void cam_op_deactivate_signal(struct cam_op_signal *sig)
{
	struct cam_op_signal *active;
	struct cam_obj_op *source;
	unsigned long flags;

	source = nsobj_to_cam_op(sig->source);
	if (WARN_ON(!source))
		return;

	write_lock_irqsave(&source->notify_lock, flags);
	list_for_each_entry(active, &source->notify_active_chain, entry) {
		if (active == sig) {
			list_del_init(&sig->entry);
			break;
		}
	}
	write_unlock_irqrestore(&source->notify_lock, flags);
}

/**
 * cam_op_completion_event() - Notify OP completion to user-space
 * @pipeline: pointer to CAM pipeline
 * @op: pointer to CAM operation
 *
 * This records the completion of an operation and push it into ring buffer to
 * notify user-space.
 * This expects that @op is either EXECUTED or DELETED at this stage.
 */
static void cam_op_completion_event(struct cam_pipeline *pipeline,
				    struct cam_obj_op *op)
{
	struct cam_completion completion = {};

	completion.id = cam_obj_id(&op->nsobj);

	/*
	 * This OP is executed, nothing can change its ->state so we
	 * don't really need to take the notify_lock.
	 */
	if (op->state == CAM_OPERATION_STATE_EXECUTED)
		completion.type = CAM_COMPLETION_TYPE_EXECUTED;
	else if (op->state == CAM_OPERATION_STATE_DELETED)
		completion.type = CAM_COMPLETION_TYPE_DELETED;
	else
		pr_err("Unknown OP state: %d\n", op->state);

	cam_ringbuffer_write(&pipeline->event_buffer, &completion);
}

/*
 * prepare() stage should only handle instructions that set up execution
 * context: e.g. create entity instance, import DMA buffer, etc. We cannot
 * handle instructions that destroy execution context, as there may be
 * operations in the batch that depend on that context. Therefore context
 * destructions instructions are handled during operation execution.
 */
static int cam_prepare_dmabuf_instruction(struct cam_obj_op *op,
					  struct cam_dmabuf_instruction *insn)
{
	struct cam_pipeline *pipeline = op->pipeline;

	if (insn->op == CAM_OP_DMABUF_REMOVE)
		return 0;

	if (insn->op == CAM_OP_DMABUF_ADD) {
		struct cam_obj_buffer *buffer;

		buffer = cam_buffer_register(&pipeline->objs,
					     op->exec_entity,
					     insn->dma_fd,
					     insn->buf_id);
		if (!buffer)
			return -EINVAL;

		cam_buffer_put(buffer);
		return 0;
	}

	pr_err("Unknown dmabuf instruction operation: %d\n", insn->op);
	return -EINVAL;
}

static int
cam_prepare_instance_instruction(struct cam_obj_op *op,
				 struct cam_instance_instruction *insn)
{
	struct cam_pipeline *pipeline = op->pipeline;

	if (insn->op == CAM_OP_INSTANCE_DESTROY)
		return 0;

	if (insn->op == CAM_OP_INSTANCE_CREATE) {
		struct cam_obj_instance *instance;

		instance = cam_instance_create(&pipeline->objs,
					       op->exec_entity,
					       insn->id);
		if (!instance)
			return -EINVAL;

		cam_instance_put(instance);
		return 0;
	}

	pr_err("Unknown instance instruction operation: %d\n", insn->op);
	return -EINVAL;
}

static int cam_run_dmabuf_instruction(struct cam_obj_op *op,
				      struct cam_dmabuf_instruction *insn)
{
	struct cam_pipeline *pipeline = op->pipeline;

	if (insn->op == CAM_OP_DMABUF_ADD)
		return 0;

	if (insn->op == CAM_OP_DMABUF_REMOVE)
		return cam_buffer_unregister(&pipeline->objs, insn->buf_id);

	pr_err("Unknown dmabuf instruction operation: %d\n", insn->op);
	return -EINVAL;
}

static int cam_run_instance_instruction(struct cam_obj_op *op,
					struct cam_instance_instruction *insn)
{
	struct cam_pipeline *pipeline = op->pipeline;

	if (insn->op == CAM_OP_INSTANCE_CREATE)
		return 0;

	if (insn->op == CAM_OP_INSTANCE_DESTROY)
		return cam_instance_destroy(&pipeline->objs, insn->id);

	pr_err("Unknown instance instruction operation: %d\n", insn->op);
	return -EINVAL;
}

/*
 * cam_read_instruction() and cam_write_instruction() hold the reference of
 * DMA-buf objects only through out corresponding entity call. If the driver
 * needs to access that buffer from different context (e.g. IRQ which may
 * happen after entity call returns) the driver need to additionally increment
 * DMA-buf object's ref-count and decrement it once DMA-buf access is done.
 *
 * Driver cannot lookup DMA buffer objects directly, because those belong to
 * pipeline local namespace. However, we pass pointer to cam_obj_buffer object
 * down to the entity call.
 *
 * User-space provides us with an array of buffer object IDs, we overwrite
 * those IDs with pointers to actual buffer objects (if such objects exists)
 * right before entity call. Functions operate on local copy of user-supplied
 * RW instruction.
 */
static void cam_buffers_list_put(u32 num_buffers, u64 *list)
{
	u32 i;

	if (!list)
		return;

	for (i = 0; i < num_buffers; i++) {
		if (list[i]) {
			struct cam_obj_buffer *buffer;

			buffer = (struct cam_obj_buffer *)list[i];
			cam_buffer_put(buffer);
		}
	}

	kvfree(list);
}

static u64 *cam_buffers_list_get(struct cam_pipeline *pipeline,
				 u32 num_buffers,
				 u64 buffers_list)
{
	u64 __user *payload;
	u64 *list;
	u32 i;

	if (num_buffers > CAM_RW_INSN_MAX_NUM_BUFFERS)
		return NULL;

	list = kvcalloc(num_buffers, sizeof(u64), GFP_KERNEL);
	if (!list)
		return NULL;

	payload = (u64 *)buffers_list;
	for (i = 0; i < num_buffers; i++) {
		struct cam_obj_buffer *buffer;
		u64 id;

		if (copy_from_user(&id, payload, sizeof(id)))
			goto error;

		buffer = cam_buffer_lookup(&pipeline->objs, id);
		if (!buffer)
			goto error;

		list[i] = (u64)buffer;
		payload++;
	}

	return list;

error:
	cam_buffers_list_put(num_buffers, list);
	return NULL;
}

static int cam_read_instruction(struct cam_obj_op *op,
				struct cam_read_instruction *insn)
{
	struct cam_obj_entity *entity = op->exec_entity;
	struct cam_pipeline *pipeline = op->pipeline;
	u64 *buffers_list;
	void *dev;
	int ret;

	if (!op->exec_instance)
		return -EINVAL;

	if (insn->num_buffers) {
		buffers_list = cam_buffers_list_get(pipeline,
						    insn->num_buffers,
						    insn->buffers_list);
		if (!buffers_list)
			return -EINVAL;

		insn->buffers_list = (u64)buffers_list;
	}

	dev = cam_entity_driver_data(entity);
	ret = entity->ops->instance_read(dev, op->exec_instance, insn);

	if (insn->num_buffers)
		cam_buffers_list_put(insn->num_buffers, buffers_list);
	return ret;
}

static int cam_write_instruction(struct cam_obj_op *op,
				 struct cam_write_instruction *insn)
{
	struct cam_obj_entity *entity = op->exec_entity;
	struct cam_pipeline *pipeline = op->pipeline;
	u64 *buffers_list;
	void *dev;
	int ret;

	if (!op->exec_instance)
		return -EINVAL;

	if (insn->num_buffers) {
		buffers_list = cam_buffers_list_get(pipeline,
						    insn->num_buffers,
						    insn->buffers_list);
		if (!buffers_list)
			return -EINVAL;

		insn->buffers_list = (u64)buffers_list;
	}

	dev = cam_entity_driver_data(entity);
	ret = entity->ops->instance_write(dev, op->exec_instance, insn);

	if (insn->num_buffers)
		cam_buffers_list_put(insn->num_buffers, buffers_list);
	return ret;
}

static void cam_op_run_rw_instructions(struct cam_obj_op *op)
{
	struct cam_rw_instruction __user *payload;
	struct cam_rw_instruction_list rw_list;
	int i;

	/* No execution payload, this probably was a SYNC operation */
	if (op->exec_rw_list_addr == CAM_OP_NO_RW_LIST)
		return;

	/* At this point OPs require an entity to be run against */
	if (!op->exec_entity)
		return;

	if (copy_from_user(&rw_list, op->exec_rw_list_addr, sizeof(rw_list))) {
		pr_err("Unable to access operation RW instructions list\n");
		return;
	}

	payload = op->exec_rw_list_addr +
		offsetof(struct cam_rw_instruction_list, instructions);

	for (i = 0; i < rw_list.num_entries; i++) {
		struct cam_rw_instruction insn;
		int ret = 0;

		if (copy_from_user(&insn, payload, sizeof(insn))) {
			pr_err("Unable to access RW instruction\n");
			break;
		}

		switch (insn.type) {
		case CAM_READ_INSTRUCTION:
			ret = cam_read_instruction(op, &insn.rd);
			break;
		case CAM_WRITE_INSTRUCTION:
			ret = cam_write_instruction(op, &insn.wr);
			break;
		case CAM_DMABUF_INSTRUCTION:
			ret = cam_run_dmabuf_instruction(op, &insn.db);
			break;
		case CAM_INSTANCE_INSTRUCTION:
			ret = cam_run_instance_instruction(op, &insn.in);
			break;
		}

		if (ret) {
			pr_err("Operation execution error, aborting\n");
			put_user(ret, &payload->error);
			break;
		}

		payload++;
	}
}

/**
 * cam_op_run() - Execute the target operation
 * @op: pointer to CAM operation to be executed
 *
 * This is the place where an operation is being executed.
 * Here's the simplified flow:
 *	1. Set operation's state to RUNNING
 *	2. Read/write registers according to the registered instructions
 *	3. Set operation's state to EXECUTED
 *	4. Notify the completion to user-space
 *	5. Trigger registered signals to notify the dependencies that are
 *	   blocked on the current operation
 *	6. clean-up for the executed operation (e.g. put ref-counter)
 */
static void cam_op_run(struct cam_obj_op *op)
{
	struct cam_pipeline *pipeline = op->pipeline;

	WARN_ON(!cam_op_set_state(op, CAM_OPERATION_STATE_RUNNING));

	if (op->delay_ns)
		ndelay(op->delay_ns);

	cam_op_run_rw_instructions(op);

	/* New signals cannot be registered after this line */
	WARN_ON(!cam_op_set_state(op, CAM_OPERATION_STATE_EXECUTED));

	/*
	 * Remove operation from the namespace, so that its ID can be reused
	 * from now on.
	 */
	cam_obj_remove(&op->nsobj);

	/* Notify user-space that we are done with this OP */
	cam_op_completion_event(pipeline, op);
	cam_op_fire_signals(op);
	/*
	 * Lastly, put operation's refcount. Note that we cannot reliably
	 * cam_obj_deinit() here, because some other OP that is still blocked
	 * on some signals may be holding ref-count of this OP.
	 */
	cam_op_put(op);
}

static bool io_queue_status(struct cam_pipeline *pipeline)
{
	unsigned long flags;
	bool pending_ops;

	if (test_bit(CAM_PIPELINE_IO_EXITING, &pipeline->io_state))
		return true;
	if (signal_pending(current))
		return true;

	spin_lock_irqsave(&pipeline->io_queue_lock, flags);
	pending_ops = !list_empty(&pipeline->io_queue);
	spin_unlock_irqrestore(&pipeline->io_queue_lock, flags);

	return pending_ops;
}

/**
 * cam_pipeline_io_worker() - IO-thread worker that consumes the pipeline queue
 * @data: pointer to CAM pipeline
 *
 * This worker thread executes each CAM operations in the IO-queue while the
 * pipeline is active, and clean up the dangling events/operations/syncfiles
 * once the IO-queue becomes empty or pipeline becomes inactive.
 */
static int cam_pipeline_io_worker(void *data)
{
	struct cam_pipeline *pipeline = data;
	char buf[TASK_COMM_LEN];
	unsigned long flags;

	snprintf(buf, sizeof(buf), "cam-io");
	set_task_comm(current, buf);

	while (!test_bit(CAM_PIPELINE_IO_EXITING, &pipeline->io_state)) {
		struct cam_obj_op *op = NULL;

		if (signal_pending(current)) {
			struct ksignal ksig;

			if (!get_signal(&ksig))
				continue;

			clear_bit(CAM_PIPELINE_IO_ACTIVE, &pipeline->io_state);
			break;
		}

		spin_lock_irqsave(&pipeline->io_queue_lock, flags);
		if (!list_empty(&pipeline->io_queue)) {
			op = list_first_entry(&pipeline->io_queue,
					      struct cam_obj_op,
					      io_queue_entry);
			list_del(&op->io_queue_entry);
		}
		spin_unlock_irqrestore(&pipeline->io_queue_lock, flags);

		if (op) {
			cam_op_run(op);
		} else {
			trace_cam_io_worker_sleep(pipeline);
			wait_event_interruptible(pipeline->io_queue_wait,
						 io_queue_status(pipeline));
			trace_cam_io_worker_wakeup(pipeline);
		}
	}

	cam_drain_instances(pipeline);
	cam_drain_buffers(pipeline);
	cam_drain_ops(pipeline);

	mutex_lock(&pipeline->io_release_lock);
	pipeline->io_thread = NULL;
	mutex_unlock(&pipeline->io_release_lock);

	clear_bit(CAM_PIPELINE_IO_EXITING, &pipeline->io_state);
	do_exit(0);
	return 0;
}

/**
 * cam_pipeline_dequeue() - Dequeue an operation from the pipeline
 * @pipeline: pointer to CAM pipeline
 * @req: remove request from user-space
 *
 * This essentially marks the target operation to DELETED.
 *
 * Return: 0 on success or a negative error code otherwise.
 */
int cam_pipeline_dequeue(struct cam_pipeline *pipeline,
			 struct cam_operation_remove *req)
{
	struct cam_obj_op *op;

	if (!cam_pipeline_is_active(pipeline))
		return -EINVAL;

	op = cam_op_lookup(&pipeline->ops, req->id);
	if (!op)
		return -EINVAL;

	/*
	 * OP removal is not guaranteed to succeed. For instance if at this
	 * point OP is already QUEUED or EXECUTING then we won't be able
	 * to remove it, it's going to get executed.
	 */
	if (cam_drain_op(op)) {
		/* Let user-space know that we deleted the OP */
		cam_op_completion_event(pipeline, op);
	}

	/* Put lookup ref-count */
	cam_op_put(op);
	return 0;
}
ALLOW_ERROR_INJECTION(cam_pipeline_dequeue, ERRNO);

/**
 * cam_op_add_pending_signal() - Create and add a pending signal
 * @source: pointer to CAM object that blocks @target
 * @target: pointer to CAM operation that depends on @source
 * @instance: ID of entity instance
 * @activate: the callback called to activate the pending signal
 * @deactivate: the callback called to deactivate the active signal
 *
 * This allocate a pending CAM signal and append it to target's notify chain.
 * Note that the fire callback is always cam_op_notify() in the current design.
 *
 * Return: 0 on success or a negative error code otherwise.
 */
static int cam_op_add_pending_signal(struct cam_obj *source,
				     struct cam_obj_op *target,
				     u32 instance,
				     bool (*activate)(struct cam_op_signal *),
				     void (*deactivate)(struct cam_op_signal *))
{
	struct cam_op_signal *sig;

	sig = kzalloc(sizeof(struct cam_op_signal), GFP_KERNEL);
	if (!sig)
		return -ENOMEM;

	sig->instance	= instance;
	sig->activate	= activate;
	sig->fire	= cam_op_notify;
	sig->deactivate	= deactivate;

	INIT_LIST_HEAD(&sig->entry);
	INIT_LIST_HEAD(&sig->notifiers_entry);

	/* Signal should keep both objects alive until it triggers */
	if (!cam_obj_get(source))
		goto error;
	sig->source	= source;

	if (!cam_obj_get(&target->nsobj))
		goto error;
	sig->target	= &target->nsobj;

	list_add_tail(&sig->entry, &target->notify_pending_chain);
	list_add_tail(&sig->notifiers_entry, &target->notifiers);
	atomic_inc(&target->num_blockers);
	trace_cam_signal_add_pending(sig);
	return 0;
error:
	if (sig->source)
		cam_obj_put(source);
	kfree(sig);
	return -EINVAL;
}

/**
 * cam_op_dependency_add() - Create OP-to-OP dependency
 * @pipeline: pointer to CAM pipeline
 * @req: add request from user-space
 * @op: pointer to the dependent operation
 *
 * Return: 0 on success or a negative error code otherwise.
 */
static int cam_op_dependency_add(struct cam_pipeline *pipeline,
				 struct cam_dependency *req,
				 struct cam_obj_op *op)
{
	struct cam_obj_op *dep_op;
	int ret;

	dep_op = cam_op_lookup(&pipeline->ops, req->id);
	if (!dep_op) {
		/*
		 * Unsatisfied OP dependencies are considered to be
		 * non-critical. This may turn this operation into
		 * instant if it has no other dependencies.
		 */
		return 0;
	}

	ret = cam_op_add_pending_signal(&dep_op->nsobj, op,
					CAM_OP_NO_INSTANCE,
					cam_op_activate_signal,
					cam_op_deactivate_signal);
	cam_op_put(dep_op);
	return ret;
}

/**
 * cam_event_dependency_add() - Create Event-to-OP dependency
 * @pipeline: pointer to CAM pipeline
 * @req: add request from user-space
 * @op: pointer to the dependent operation
 *
 * Return: 0 on success or a negative error code otherwise.
 */
static int cam_event_dependency_add(struct cam_pipeline *pipeline,
				    struct cam_dependency *req,
				    struct cam_obj_op *op)
{
	struct cam_obj_event *dep_event;
	u32 instance;
	int ret;

	dep_event = cam_event_lookup(pipeline->cam, req->id);
	if (!dep_event)
		return -EINVAL;

	instance = CAM_OP_NO_INSTANCE;
	if (op->exec_instance)
		instance = cam_obj_id(&op->exec_instance->nsobj);

	ret = cam_op_add_pending_signal(&dep_event->nsobj, op,
					instance,
					cam_event_activate_signal,
					cam_event_deactivate_signal);
	cam_event_put(dep_event);
	return ret;
}

/**
 * cam_fence_in_dependency_add() - Create In-Fence-to-OP dependency
 * @pipeline: pointer to CAM pipeline
 * @req: add request from user-space
 * @op: pointer to the dependent operation
 *
 * Return: 0 on success or a negative error code otherwise.
 */
static int cam_fence_in_dependency_add(struct cam_pipeline *pipeline,
				       struct cam_dependency *req,
				       struct cam_obj_op *op)
{
	struct cam_obj_syncfile *sf;
	int ret;

	/*
	 * We store syncfile pointer indirectly: syncfile is linked to this
	 * OP.
	 */
	sf = cam_in_syncfile_register(pipeline->cam, op, req->id,
				      "in-fence-%d", req->id);
	if (!sf)
		return -EINVAL;

	ret = cam_op_add_pending_signal(&sf->nsobj, op,
					CAM_OP_NO_INSTANCE,
					cam_in_syncfile_activate_signal,
					cam_in_syncfile_deactivate_signal);
	return ret;
}

/**
 * cam_out_fence_instruction() - Create Out-Fence-to-OP
 * @op: pointer to the dependent operation
 * @insn: struct that will hold ID of exported fence
 *
 * Return: 0 on success or a negative error code otherwise.
 */
static int cam_out_fence_instruction(struct cam_obj_op *op,
				     struct cam_out_fence_instruction *insn)
{
	struct cam_pipeline *pipeline = op->pipeline;
	struct cam_obj_syncfile *sf;

	insn->id = CAM_OP_NO_FENCE;
	/*
	 * We store syncfile pointer indirectly: syncfile is linked to this
	 * OP.
	 */
	sf = cam_out_syncfile_register(pipeline->cam, op, "out-fence-%d",
				       cam_obj_id(&op->nsobj));
	if (!sf)
		return -EINVAL;

	insn->id = cam_out_syncfile_fd(sf);
	return 0;
}

/**
 * cam_activate_strict_dependency_mode() - Activate signal in STRICT mode
 * @op: pointer to CAM operation to activate the signal from
 *
 * This activates the pending signal of an operation in STRICT mode i.e.
 * we only activate one signal at a time from the notify chain.
 *
 * Return: True on successful signal activation or false otherwise.
 */
static bool cam_activate_strict_dependency_mode(struct cam_obj_op *op)
{
	bool activated = false;
	int ret;

	/*
	 * In STRICT mode we pick first pending signal and try to
	 * activate it (move it into active notify list).
	 */
	do {
		ret = cam_op_activate_pending_signal(op);

		if (ret == CAM_OP_PENDING_SIGNAL_NONE)
			break;
		if (ret == CAM_OP_PENDING_SIGNAL_ACTIVATED) {
			activated = true;
			break;
		}
		/*
		 * Carry on until we either have a successfully
		 * activated signal or no remaining pending signals.
		 */
	} while (ret == CAM_OP_PENDING_SIGNAL_FAILURE);

	/*
	 * We did not activate any of the pending signals (if there were any).
	 * Sanity check that ->num_blockers is zero.
	 */
	if (!activated)
		WARN_ON(atomic_read(&op->num_blockers) != 0);
	return activated;
}

/**
 * cam_activate_weak_dependency_mode() - Activate signal in WEAK mode
 * @op: pointer to CAM operation that signal is activated from
 *
 * This activates the pending signal of an operation in weak mode i.e.
 * we activate as many signals as possible by walking through the notify chain.
 *
 * Return: True on any successful signal activation or false otherwise.
 */
static bool cam_activate_weak_dependency_mode(struct cam_obj_op *op)
{
	bool activated = false;
	int ret;

	/*
	 * In WEAK mode we activate all pending signals (if any).
	 * If we fail to activate any of the signals (e.g. unsatisfied
	 * dependency) we just continue.
	 */
	do {
		ret = cam_op_activate_pending_signal(op);

		if (ret == CAM_OP_PENDING_SIGNAL_ACTIVATED)
			activated = true;
		/*
		 * Carry on until there is no remaining pending signals.
		 */
	} while (ret != CAM_OP_PENDING_SIGNAL_NONE);

	/*
	 * Ditto. We did not activate any of the pending signals (if there
	 * were any). Sanity check that ->num_blockers is zero.
	 */
	if (!activated)
		WARN_ON(atomic_read(&op->num_blockers) != 0);
	return activated;
}

/**
 * cam_op_instruction_add() - Add RW instructions to an operation
 * @pipeline: pointer to CAM pipeline
 * @req: add request from user-space
 * @op: pointer to CAM operation that the instructions are adding to
 *
 * This copies the register read/write instructions from the user-space request
 * to the target operation.
 *
 * Return: 0 on success or a negative error code otherwise.
 */
static int cam_op_instruction_add(struct cam_pipeline *pipeline,
				  struct cam_operation_add *req,
				  struct cam_obj_op *op)
{
	op->delay_ns		= req->delay_ns;
	op->exec_rw_list_addr	= (void *)CAM_OP_NO_RW_LIST;
	op->exec_entity		= NULL;
	op->exec_instance	= NULL;

	if (req->entity == CAM_OP_NO_ENTITY &&
	    req->rd_wr_list != CAM_OP_NO_RW_LIST)
		return -EINVAL;

	if (req->rd_wr_list != CAM_OP_NO_RW_LIST) {
		struct cam_rw_instruction_list rw;
		struct cam_rw_instruction insn;
		uintptr_t __user *addr;
		size_t size;

		addr = u64_to_user_ptr(req->rd_wr_list);
		op->exec_rw_list_addr = addr;
		size = sizeof(rw);
		if (copy_from_user(&rw, addr, size))
			goto error;

		if (rw.num_entries == 0)
			goto error;

		/*
		 * This will attempt to access the last RW instruction in
		 * the buffer. This check is not very reliable. User-space
		 * maps whole pages, so we will fail here only when the
		 * number of instructions points past allocated buffer pages.
		 */
		size += rw.num_entries * sizeof(struct cam_rw_instruction);
		addr += size - sizeof(struct cam_rw_instruction);
		if (copy_from_user(&insn, addr, sizeof(insn)))
			goto error;
	}

	if (req->entity != CAM_OP_NO_ENTITY) {
		op->exec_entity = cam_entity_lookup(pipeline->cam,
						    req->entity);
		if (!op->exec_entity)
			goto error;
	}

	if (req->instance != CAM_OP_NO_INSTANCE) {
		if (!op->exec_entity)
			goto error;

		op->exec_instance = cam_instance_lookup(&pipeline->objs,
							req->instance);
		if (!op->exec_instance)
			goto error;
	}

	return 0;

error:
	return -EINVAL;
}

static int cam_op_prepare_rw_instruction(struct cam_obj_op *op)
{
	struct cam_rw_instruction __user *payload;
	struct cam_rw_instruction_list rw_list;
	int i;

	if (op->exec_rw_list_addr == CAM_OP_NO_RW_LIST)
		return 0;

	if (copy_from_user(&rw_list, op->exec_rw_list_addr, sizeof(rw_list))) {
		pr_err("Unable to access operation RW instructions list\n");
		return -EFAULT;
	}

	payload = op->exec_rw_list_addr +
		offsetof(struct cam_rw_instruction_list, instructions);

	for (i = 0; i < rw_list.num_entries; i++) {
		struct cam_rw_instruction insn;
		int ret = 0;

		if (copy_from_user(&insn, payload, sizeof(insn))) {
			pr_err("Unable to access RW instruction\n");
			return -EFAULT;
		}

		switch (insn.type) {
		case CAM_DMABUF_INSTRUCTION:
			ret = cam_prepare_dmabuf_instruction(op, &insn.db);
			break;
		case CAM_INSTANCE_INSTRUCTION:
			ret = cam_prepare_instance_instruction(op, &insn.in);
			break;
		case CAM_OUT_FENCE_INSTRUCTION:
			ret = cam_out_fence_instruction(op, &insn.of);
			ret |= copy_to_user(payload, &insn, sizeof(insn));
			break;
		}

		if (ret) {
			pr_err("Failed instruction at prepare stage\n");
			put_user(ret, &payload->error);
			return ret;
		}

		payload++;
	}

	return 0;
}

/**
 * cam_pipeline_enqueue_prepare() - Create an operation
 * @pipeline: pointer to CAM pipeline
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
int cam_pipeline_enqueue_prepare(struct cam_pipeline *pipeline,
				 struct cam_operation_add *req)
{
	struct cam_obj_op *op;
	int i, ret;

	op = kzalloc(sizeof(struct cam_obj_op), GFP_KERNEL);
	if (!op)
		return -ENOMEM;

	cam_obj_init(&op->nsobj, CAM_OBJ_TYPE_OPERATION,
		     cam_op_release, &pipeline->ops);
	cam_obj_set_id(&op->nsobj, req->id);

	atomic_set(&op->num_blockers, 0);
	INIT_LIST_HEAD(&op->notify_active_chain);
	INIT_LIST_HEAD(&op->notify_pending_chain);
	INIT_LIST_HEAD(&op->notifiers);
	INIT_LIST_HEAD(&op->io_queue_entry);
	rwlock_init(&op->notify_lock);
	op->pipeline = pipeline;
	cam_op_set_state(op, CAM_OPERATION_STATE_SLEEP);

	/*
	 * Execution context (driver) data.
	 */
	if (cam_op_instruction_add(pipeline, req, op)) {
		cam_op_release(&op->nsobj);
		return -EINVAL;
	}

	if (cam_obj_insert(&op->nsobj)) {
		cam_op_release(&op->nsobj);
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
	for (i = 0; i < CAM_MAX_DEPENDENCIES; i++) {
		struct cam_dependency *dep = &req->deps[i];

		if (dep->type == CAM_DEPENDENCY_NONE)
			break;

		switch (dep->type) {
		case CAM_DEPENDENCY_OP:
			ret = cam_op_dependency_add(pipeline, dep, op);
			break;
		case CAM_DEPENDENCY_EVENT:
			ret = cam_event_dependency_add(pipeline, dep, op);
			break;
		case CAM_DEPENDENCY_FENCE_IN:
			ret = cam_fence_in_dependency_add(pipeline, dep, op);
			break;
		}

		if (ret)
			break;
	}

	if (!ret)
		ret = cam_op_prepare_rw_instruction(op);

	if (!ret)
		trace_cam_operation_add(op);
	return ret;
}
ALLOW_ERROR_INJECTION(cam_pipeline_enqueue_prepare, ERRNO);

/**
 * cam_pipeline_enqueue_submit() - Submit an operation
 * @pipeline: pointer to CAM pipeline
 * @req: add request from user-space
 *
 * This enqueues operation for execution if it has no blockers.
 *
 * Return: 0 on success or a negative error code otherwise.
 */
int cam_pipeline_enqueue_submit(struct cam_pipeline *pipeline,
				struct cam_operation_add *req)
{
	struct cam_obj_op *op;
	bool execute = false;

	/* Check pipeline status as late as possible */
	if (!cam_pipeline_is_active(pipeline))
		return -EINVAL;

	op = cam_op_lookup(&pipeline->ops, req->id);
	if (!op)
		return -EINVAL;

	/*
	 * Based on the dependency mode this will attempt to activate required
	 * number of pending signals. If nothing is activated then we convert
	 * this operation into immediate and queue it for execution.
	 */
	if (req->mode == CAM_DEPENDENCY_STRICT_ORDER) {
		if (!cam_activate_strict_dependency_mode(op))
			execute = true;
	} else {
		if (!cam_activate_weak_dependency_mode(op))
			execute = true;
	}

	/*
	 * Not blocked on any signals. Note, the object may already be in
	 * CAM_OPERATION_STATE_DELETED at this point.
	 */
	if (execute)
		cam_op_enqueue(op);

	cam_op_put(op);
	return 0;
}
ALLOW_ERROR_INJECTION(cam_pipeline_enqueue_submit, ERRNO);

static void cam_cancel_dmabuf_instruction(struct cam_obj_op *op,
					  struct cam_dmabuf_instruction *insn)
{
	struct cam_pipeline *pipeline = op->pipeline;

	if (insn->op == CAM_OP_DMABUF_REMOVE)
		return;

	cam_buffer_unregister(&pipeline->objs, insn->buf_id);
}

static void
cam_cancel_instance_instruction(struct cam_obj_op *op,
				struct cam_instance_instruction *insn)
{
	struct cam_pipeline *pipeline = op->pipeline;

	if (insn->op == CAM_OP_INSTANCE_DESTROY)
		return;

	cam_instance_destroy(&pipeline->objs, insn->id);
}

static void cam_op_cancel_rw_instruction(struct cam_obj_op *op)
{
	struct cam_rw_instruction __user *payload;
	struct cam_rw_instruction_list rw_list;
	int i;

	if (op->exec_rw_list_addr == CAM_OP_NO_RW_LIST)
		return;

	if (copy_from_user(&rw_list, op->exec_rw_list_addr, sizeof(rw_list))) {
		pr_err("Unable to access operation RW instructions list\n");
		return;
	}

	payload = op->exec_rw_list_addr +
		offsetof(struct cam_rw_instruction_list, instructions);

	for (i = 0; i < rw_list.num_entries; i++) {
		struct cam_rw_instruction insn;

		if (copy_from_user(&insn, payload, sizeof(insn))) {
			pr_err("Unable to access RW instruction\n");
			break;
		}

		switch (insn.type) {
		case CAM_DMABUF_INSTRUCTION:
			cam_cancel_dmabuf_instruction(op, &insn.db);
			break;
		case CAM_INSTANCE_INSTRUCTION:
			cam_cancel_instance_instruction(op, &insn.in);
			break;
		}

		payload++;
	}
}

/**
 * cam_pipeline_enqueue_cancel() - Cancel an operation
 * @pipeline: pointer to CAM pipeline
 * @req: add request from user-space
 *
 * This drains an operation which we failed to prepare.
 *
 * Return: 0 on success or a negative error code otherwise.
 */
int cam_pipeline_enqueue_cancel(struct cam_pipeline *pipeline,
				struct cam_operation_add *req)
{
	struct cam_obj_op *op = cam_op_lookup(&pipeline->ops, req->id);

	if (!op)
		return 0;

	cam_op_set_state(op, CAM_OPERATION_STATE_DELETED);
	cam_op_cancel_rw_instruction(op);
	cam_drain_op_signals(op);
	cam_drain_op_syncfiles(op);
	/* drop lookup ref-count */
	cam_op_put(op);
	/* Now release the object */
	cam_obj_remove(&op->nsobj);
	cam_obj_deinit(&op->nsobj);
	return 0;
}

static bool cam_op_enum(struct cam_obj_op *op, struct cam_koutput *output)
{
	struct cam_query_operation_entry *qent;

	/* User just want the size, not the data. */
	if (!cam_output_has_buffer(output))
		goto out;

	cam_output_next_entry(output, qent);
	if (!qent)
		return false;

	if (__put_user(cam_obj_id(&op->nsobj), &qent->id))
		return false;

	/* This is racy either way */
	if (__put_user(op->state, &qent->state))
		return false;

out:
	output->num_entries++;
	return true;
}

static bool pipeline_walk_query_callback(struct cam_obj *nsobj,
					 struct cam_graph_walk *ctl)
{
	struct cam_obj_op *op;

	op = nsobj_to_cam_op(nsobj);
	if (!op)
		return false;

	return cam_op_enum(op, ctl->data);
}

static bool cam_ns_walk_callback(struct cam_obj *nsobj,
				 struct cam_ns_walk_control *ctl)
{
	struct cam_obj_op *op;
	unsigned long flags;
	bool valid;

	op = nsobj_to_cam_op(nsobj);
	if (WARN_ON(!op))
		return true;

	/* This is racy but what can we do */
	read_lock_irqsave(&op->notify_lock, flags);
	valid = (op->state & ctl->flags);
	read_unlock_irqrestore(&op->notify_lock, flags);

	if (valid)
		cam_op_enum(op, ctl->data);
	return false;
}

static void query_state_filter(struct cam_pipeline *pipeline,
			       int state,
			       struct cam_koutput *output)
{
	struct cam_ns_walk_control ctl;

	ctl.data	= output;
	ctl.flags	= state;
	ctl.cb		= cam_ns_walk_callback;
	cam_ns_for_each(&pipeline->ops, &ctl);
}

/**
 * cam_enum_operations() - Response to user-space operation queries
 * @pipeline: pointer to CAM pipeline
 * @query: query request from user-space
 * @output: pointer to user-space buffer for the output
 *
 * This handles different user-space operation queries based on the query mode.
 * Check @cam_operation_query_mode in UAPI for supported modes.
 *
 * Return: 0 on success or a negative error code otherwise.
 */
int cam_enum_operations(struct cam_pipeline *pipeline,
			struct cam_query_operations *query,
			struct cam_koutput *output)
{
	int state;
	int ret;

	query->num_ops = 0;

	/*
	 * We have two different mechanisms here: the former one starts at
	 * a given pipeline work item and walks the dependency graph (think
	 * BFS or DFS), the latter one simply does a linear scan of pipeline
	 * hash table.
	 */
	if (query->id != CAM_OP_ID_ALL_OP) {
		struct cam_graph_walk ctl;
		struct cam_obj_op *op;

		op = cam_op_lookup(&pipeline->ops, query->id);
		if (!op)
			return -EINVAL;

		if (query->mode == CAM_OP_QUERY_UNIQUE)
			ctl.flags = CAM_GRAPH_WALK_ONESHOT;
		if (query->mode == CAM_OP_QUERY_UNIQUE_AND_DEPS)
			ctl.flags = CAM_GRAPH_WALK_RECURSIVE;

		ctl.data = output;
		ctl.cb = pipeline_walk_query_callback;

		ret = pipeline_walk(&op->nsobj, &ctl);
		query->num_ops = output->num_entries;
		cam_op_put(op);
		return ret;
	}

	switch (query->mode) {
	case CAM_OP_QUERY_ALL:
		/* CAM_OPERATION_STATE_EXECUTED | CAM_OPERATION_STATE_DELETED ? */
		state = CAM_OPERATION_STATE_QUEUED | CAM_OPERATION_STATE_SLEEP |
			CAM_OPERATION_STATE_RUNNING;
		break;
	case CAM_OP_QUERY_SLEEP:
		state = CAM_OPERATION_STATE_SLEEP;
		break;
	case CAM_OP_QUERY_QUEUED:
		state = CAM_OPERATION_STATE_QUEUED;
		break;
	default:
		return -EINVAL;
	}

	query_state_filter(pipeline, state, output);
	query->num_ops = output->num_entries;
	return 0;
}
ALLOW_ERROR_INJECTION(cam_enum_operations, ERRNO);

/**
 * cam_pipeline_destroy() - Destroy CAM execution pipeline
 * @pipeline: pointer to CAM pipeline
 *
 * We don't release the pipeline because it's an embedded member of cam_device.
 */
void cam_pipeline_destroy(struct cam_pipeline *pipeline)
{
	cam_ringbuffer_release(&pipeline->event_buffer);
	cam_ns_release(&pipeline->ops);
	cam_ns_release(&pipeline->objs);
}

/**
 * cam_pipeline_io_setup() - Set up IO-thread in pipeline
 * @pipeline: pointer to CAM pipeline
 *
 * Return: 0 on success or a negative error code otherwise.
 */
int cam_pipeline_io_setup(struct cam_pipeline *pipeline)
{
	if (test_bit(CAM_PIPELINE_IO_ACTIVE, &pipeline->io_state) ||
	    pipeline->io_thread) {
		pr_err("I/O thread already setup\n");
		return -EINVAL;
	}

	pipeline->io_thread = create_io_thread(cam_pipeline_io_worker,
					       pipeline,
					       NUMA_NO_NODE);
	if (IS_ERR(pipeline->io_thread)) {
		pipeline->io_thread = NULL;
		return -EINVAL;
	}

	init_waitqueue_head(&pipeline->io_queue_wait);
	pipeline->io_thread->flags |= PF_NO_SETAFFINITY;
	set_bit(CAM_PIPELINE_IO_ACTIVE, &pipeline->io_state);
	wake_up_new_task(pipeline->io_thread);
	return 0;
}

/**
 * cam_pipeline_io_release() - Release IO-thread in CAM pipeline
 * @pipeline: pointer to CAM pipeline
 *
 * Return: 0 on success or a negative error code otherwise.
 */
int cam_pipeline_io_release(struct cam_pipeline *pipeline)
{
	set_bit(CAM_PIPELINE_IO_EXITING, &pipeline->io_state);
	if (!test_bit(CAM_PIPELINE_IO_ACTIVE, &pipeline->io_state))
		return 0;

	mutex_lock(&pipeline->io_release_lock);
	if (pipeline->io_thread)
		wake_up(&pipeline->io_queue_wait);
	mutex_unlock(&pipeline->io_release_lock);

	while (test_bit(CAM_PIPELINE_IO_EXITING, &pipeline->io_state))
		schedule_timeout(HZ / 10);
	return 0;
}

/**
 * cam_pipeline_init() - Initialize CAM execution pipeline
 * @cam: pointer to CAM device
 * @pipeline: pointer to CAM pipeline which belongs to the provided CAM device
 *
 * Return: 0 on success or a negative error code otherwise.
 */
int cam_pipeline_init(struct cam_device *cam, struct cam_pipeline *pipeline)
{
	int ret;

	ret = cam_ns_init(&pipeline->ops, CAM_NS_POL_USER_ID);
	if (ret)
		return ret;

	ret = cam_ns_init(&pipeline->objs, CAM_NS_POL_USER_ID);
	if (ret) {
		cam_ns_release(&pipeline->ops);
		return ret;
	}

	ret = cam_ringbuffer_init(&pipeline->event_buffer,
				  sizeof(struct cam_completion),
				  CAM_RINGBUFFER_SIZE);
	if (ret) {
		cam_ns_release(&pipeline->ops);
		cam_ns_release(&pipeline->objs);
		return ret;
	}

	INIT_LIST_HEAD(&pipeline->io_queue);
	spin_lock_init(&pipeline->io_queue_lock);
	mutex_init(&pipeline->io_release_lock);
	pipeline->io_thread = NULL;
	pipeline->cam = cam;
	pipeline->id = atomic_inc_return(&pipeline_count);
	return ret;
}
