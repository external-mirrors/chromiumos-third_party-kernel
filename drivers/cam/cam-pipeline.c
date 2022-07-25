// SPDX-License-Identifier: GPL-2.0
/*
 * CAM requests executor
 *
 * Copyright (C) 2022 Google LLC
 */

#define pr_fmt(fmt) "cam-pipeline: " fmt

#include <linux/cam/cam-device.h>
#include <linux/cam/cam-entity.h>
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

#include <trace/events/cam.h>

#include <uapi/linux/cam.h>

/**
 * cam_pipeline_active() - Check whether the execution pipeline is active
 * @pipeline: pointer to CAM pipeline
 *
 * Return: true if the pipeline is active, or false otherwise.
 */
static bool cam_pipeline_active(struct cam_pipeline *pipeline)
{
	if (WARN_ON(!test_bit(CAM_PIPELINE_IO_ACTIVE, &pipeline->io_state)))
		return false;
	if (WARN_ON(!pipeline->io_thread))
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
 * process_post_exec_action() - Execute a post-operation action
 * @action: pointer to the action
 *
 * Different actions will be taken depending on the action type.
 */
static void process_post_exec_action(struct cam_op_exec_action *action)
{
	/*
	 * Fences (syncfiles) are short-lived objects, they are supposed to
	 * be released once OP execution is done. Pipeline (exec action in
	 * particular) creates and owns these syncfile objects so we can just
	 * proceed releasing it, but after we signal OUT fences.
	 */
	switch (action->type) {
	case CAM_OP_POST_EXEC_ACTION_FENCE_OUT:
		cam_out_syncfile_signal(action->syncfile);
		cam_syncfile_unregister(action->syncfile);
		break;
	case CAM_OP_POST_EXEC_ACTION_FENCE_IN:
		cam_syncfile_unregister(action->syncfile);
		break;
	default:
		pr_err("Unknown OP post exec action type: %d\n",
		       action->type);
	}
}

/**
 * cam_op_process_post_actions() - Execute a list of post actions
 * @op: pointer to CAM operation
 */
static void cam_op_process_post_actions(struct cam_obj_op *op)
{
	struct cam_op_exec_action *action;

	while (!list_empty(&op->post_exec_action_chain)) {
		action = list_first_entry(&op->post_exec_action_chain,
					  struct cam_op_exec_action,
					  entry);

		list_del(&action->entry);
		process_post_exec_action(action);
		kfree(action);
	}
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

	cam_op_process_post_actions(op);

	if (op->exec_entity)
		cam_entity_put(op->exec_entity);
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
	if (op->state == CAM_OPERATION_STATE_RUNNING &&
	    new_state == CAM_OPERATION_STATE_DELETED)
		goto out;
	/* Do not go backwards */
	if (WARN_ON(op->state > new_state))
		goto out;

	trace_cam_operation_set_state(op);
	op->state = new_state;
	ret = true;
out:
	write_unlock_irqrestore(&op->notify_lock, flags);
	return ret;
}

/**
 * pipeline_walk_cb_dequeue() - Pipeline walk dequeue callback
 * @nsobj: pointer to CAM object that represents a CAM operation
 * @ctl: auxiliary data
 *
 * This callback marks its associated operation as DELETED.
 *
 * Return: True on success or false otherwise.
 */
static bool pipeline_walk_cb_dequeue(struct cam_obj *nsobj,
				     struct cam_graph_walk *ctl)
{
	struct cam_obj_op *op;

	op = nsobj_to_cam_op(nsobj);
	if (!op)
		return false;

	/*
	 * NOTE:
	 *
	 * DELETE_OP doesn't do anything at this point but simply walks the
	 * graph of objects and marks them as DELETED (if objects can be
	 * marked as DELETED).
	 *
	 * But we, in general, want to drop object reference counter and to
	 * delete it from the namespace. This will race with the delayed
	 * object execution.
	 */
	return cam_op_set_state(op, CAM_OPERATION_STATE_DELETED);
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

	pipeline = op->pipeline;
	cam_op_set_state(op, CAM_OPERATION_STATE_QUEUED);
	spin_lock_irqsave(&pipeline->io_queue_lock, flags);
	list_add_tail(&op->io_queue_entry, &pipeline->io_queue);
	spin_unlock_irqrestore(&pipeline->io_queue_lock, flags);

	if (cam_pipeline_active(pipeline))
		wake_up_process(pipeline->io_thread);
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
		 * The signal was not activated, it will never be raised so
		 * release it now. Note that this can be the final put for
		 * the source.
		 */
		release_signal(sig);
		/*
		 * We failed to activate pending signal, something is not
		 * right with the signal source: e.g. source OP is in
		 * executed/deleted state. Decrement ->num_blockers, because
		 * this signal will not be raised.
		 */
		atomic_dec(&op->num_blockers);
		return CAM_OP_PENDING_SIGNAL_FAILURE;
	}
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
	struct cam_op_signal *sig;

	while (!list_empty(notify_active_chain)) {
		sig = list_first_entry(notify_active_chain,
				       struct cam_op_signal,
				       entry);

		list_del_init(&sig->entry);
		sig->fire(sig);
		release_signal(sig);
	}
}

static void drain_notify_chain(struct list_head *notify_chain)
{
	struct cam_op_signal *sig;

	while (!list_empty(notify_chain)) {
		sig = list_first_entry(notify_chain,
				       struct cam_op_signal,
				       entry);

		list_del_init(&sig->entry);
		release_signal(sig);
	}
}

/**
 * cam_drain_active_signals() - Drain all signals from an active notify chain
 * @notify_chain: operation notify chain with signals
 *
 * Unlike cam_fire_active_signals(), this simply removes and releases the
 * signals without running their callbacks.
 */
void cam_drain_active_signals(struct list_head *notify_active_chain)
{
	drain_notify_chain(notify_active_chain);
}

static void cam_drain_op_callback(struct cam_obj *nsobj,
				  struct cam_ns_walk_control *ctl)
{
	struct cam_obj_op *op;

	op = nsobj_to_cam_op(nsobj);
	if (WARN_ON(!op))
		return;

	/*
	 * OPs have pending and active signals chains, all of which
	 * need to be drained becuase they hold refcounters.
	 */
	drain_notify_chain(&op->notify_active_chain);
	drain_notify_chain(&op->notify_pending_chain);

	/*
	 * Note we cannot remove OP from namespace here, because we are
	 * under RCU read-side lock. We instead add OPs to IO-queue and
	 * then remove (flush) all queued OPs, outside of RCU read-side
	 * lock.
	 */
	list_del_init(&op->io_queue_entry);
	list_add(&op->io_queue_entry, &op->pipeline->io_queue);
}

/**
 * cam_drain_ops() - Drain signals from both pending and active chains of a
 * pipeline
 * @pipeline: pointer to CAM pipeline
 */
static void cam_drain_ops(struct cam_pipeline *pipeline)
{
	struct cam_ns_walk_control ctl = {0, };

	ctl.cb		= cam_drain_op_callback;
	cam_ns_for_each(&pipeline->ns, &ctl);
}

/**
 * cam_flush_ops() - Flush and unregister all the operations of a pipeline
 * @pipeline: pointer to CAM pipeline
 *
 * Flush all the operations in the IO-queue.
 */
static void cam_flush_ops(struct cam_pipeline *pipeline)
{
	struct cam_obj_op *op;

	while (!list_empty(&pipeline->io_queue)) {
		op = list_first_entry(&pipeline->io_queue,
				      struct cam_obj_op,
				      io_queue_entry);
		list_del(&op->io_queue_entry);

		cam_obj_remove(&op->nsobj);
		cam_obj_deinit(&op->nsobj);
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

	completion.ts = ktime_to_ns(ktime_get());
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

static void cam_op_run_rw_instructions(struct cam_obj_op *op)
{
	struct cam_rw_instruction_list rw_list;
	struct cam_rw_instruction __user *payload;
	struct cam_obj_entity *entity;
	int i;

	/* No execution payload, this probably was a SYNC operation */
	if (op->exec_rw_list_addr == CAM_NO_RD_WR)
		return;

	/*
	 * @FIXME: entity_ops may be updated soon, but at this point we
	 * execute OPs only on entities
	 */
	if (!op->exec_entity)
		return;

	if (copy_from_user(&rw_list, op->exec_rw_list_addr, sizeof(rw_list))) {
		pr_err("Unable to access opeeration RW instructions list\n");
		return;
	}

	entity = op->exec_entity;
	payload = op->exec_rw_list_addr +
		offsetof(struct cam_rw_instruction_list, instructions);

	for (i = 0; i < rw_list.num_entries; i++) {
		struct cam_rw_instruction insn;
		int ret;

		if (copy_from_user(&insn, payload, sizeof(insn))) {
			pr_err("Ubable to access RW instruction\n");
			break;
		}

		switch (insn.type) {
		case CAM_READ_INSTRUCTION:
			ret = entity->ops->read(entity, &insn.rd);
			break;
		case CAM_WRITE_INSTRUCTION:
			ret = entity->ops->write(entity, &insn.wr);
			break;
		default:
			pr_err("Invalid operation instruction type: %d\n",
			       insn.type);
			ret = -EINVAL;
			break;
		}

		if (ret) {
			pr_err("Operation execution error, aborting\n");
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

	/*
	 * After this the only valid next state is CAM_OPERATION_STATE_EXECUTED.
	 * But first... we need to successfully set object to RUNNING, which
	 * is not guaranteed. For instance, while object was queued and awaited
	 * to be executed or while it was blocked on signal(-s), it might have
	 * been marked as CAM_OPERATION_STATE_DELETED by the user-space.
	 */
	if (!cam_op_set_state(op, CAM_OPERATION_STATE_RUNNING))
		goto done;

	/*
	 * We don't expect pipeline notify_lock to be locked often (if
	 * ever).
	 */
	if (op->delay_ns)
		ndelay(op->delay_ns);

	cam_op_run_rw_instructions(op);

done:
	/* New signals cannot be registered after this line */
	cam_op_set_state(op, CAM_OPERATION_STATE_EXECUTED);

	/*
	 * First, remove operation from the namespace, so that ID can
	 * be reused from now on (in case if user-space attempts to do
	 * something like this immediately after it reads completion
	 * event)
	 */
	cam_obj_remove(&op->nsobj);
	/* Second, notify user-space that we are done with this OP */
	cam_op_completion_event(pipeline, op);
	/*
	 * Third, trigger all registered signals. Even if the operation was in
	 * DELETED state
	 */
	cam_op_fire_signals(op);
	/*
	 * Lastly, put operation's refcount. Note that we cannot reliably
	 * cam_obj_deinit() here, because some other OP that is still pending
	 * other signals may at the end depend on this OP (e.g. in STRICT
	 * mode). So this OP's refcounter is not guaranteed to be 1 here,
	 * it is supposed to get to zero once signal that is holding this
	 * OP is raised.
	 */
	cam_op_put(op);
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

	while (test_bit(CAM_PIPELINE_IO_ACTIVE, &pipeline->io_state)) {
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
			set_current_state(TASK_INTERRUPTIBLE);
			schedule();
			set_current_state(TASK_RUNNING);
		}
	}

	/*
	 * We don't have execution IO thread running anymore, but we still
	 * have driver's entities and fence, which can signal OPs. Drain
	 * events' and fences' signals first then drain signals (pending
	 * and active ones) of all the OPs. Lastly remove and deinit drained
	 * OPs.
	 */
	cam_drain_events(pipeline->cam);
	cam_drain_in_syncfiles(pipeline->cam);
	cam_drain_ops(pipeline);
	cam_flush_ops(pipeline);

	/* All signals were drained, nothing should cam_op_enqueue() */
	pipeline->io_thread = NULL;
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
	struct cam_graph_walk ctl;
	struct cam_obj_op *op;
	int ret;

	if (!cam_pipeline_active(pipeline))
		return -EINVAL;

	op = cam_op_lookup(&pipeline->ns, req->id);
	if (!op)
		return -EINVAL;

	if (req->mode == CAM_REMOVE_UNIQUE)
		ctl.flags = CAM_GRAPH_WALK_ONESHOT;

	if (req->mode == CAM_REMOVE_RECURSIVE)
		ctl.flags = CAM_GRAPH_WALK_RECURSIVE;

	ctl.data = NULL;
	ctl.cb = pipeline_walk_cb_dequeue;

	ret = pipeline_walk(&op->nsobj, &ctl);
	cam_op_put(op);
	return ret;
}
ALLOW_ERROR_INJECTION(cam_pipeline_dequeue, ERRNO);

/**
 * cam_op_add_pending_signal() - Create and add a pending signal
 * @source: pointer to CAM object that blocks @target
 * @target: pointer to CAM operation that depends on @source
 * @activate: the callback called to activate the pending signal
 *
 * This allocate a pending CAM signal and append it to target's notify chain.
 * Note that the fire callback is always cam_op_notify() in the current design.
 *
 * Return: 0 on success or a negative error code otherwise.
 */
static int cam_op_add_pending_signal(struct cam_obj *source,
				     struct cam_obj_op *target,
				     bool (*activate)(struct cam_op_signal *))
{
	struct cam_op_signal *sig;

	sig = kzalloc(sizeof(struct cam_op_signal), GFP_KERNEL);
	if (!sig)
		return -ENOMEM;

	sig->activate	= activate;
	sig->fire	= cam_op_notify;
	INIT_LIST_HEAD(&sig->entry);

	/* Signal should keep both objects alive until it triggers */
	if (!cam_obj_get(source))
		goto error;
	sig->source	= source;

	if (!cam_obj_get(&target->nsobj))
		goto error;
	sig->target	= &target->nsobj;

	list_add_tail(&sig->entry, &target->notify_pending_chain);
	atomic_inc(&target->num_blockers);
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

	dep_op = cam_op_lookup(&pipeline->ns, req->id);
	if (!dep_op) {
		/*
		 * Unsatisfied OP dependencies are considered to be
		 * non-critical. This may turn this operation into
		 * instant if it has no other dependencies.
		 */
		return 0;
	}

	ret = cam_op_add_pending_signal(&dep_op->nsobj, op,
					cam_op_activate_signal);
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
	int ret;

	dep_event = cam_event_lookup(pipeline->cam, req->id);
	if (!dep_event)
		return -EINVAL;

	ret = cam_op_add_pending_signal(&dep_event->nsobj, op,
					cam_event_activate_signal);
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
	struct cam_op_exec_action *action;
	int ret;

	action = kzalloc(sizeof(*action), GFP_KERNEL);
	if (!action)
		return -ENOMEM;

	INIT_LIST_HEAD(&action->entry);
	action->type = CAM_OP_POST_EXEC_ACTION_FENCE_IN;
	action->syncfile = cam_in_syncfile_register(pipeline->cam,
						    req->id,
						    "in-fence-%d",
						    req->id);
	if (!action->syncfile) {
		ret = -EINVAL;
		goto error;
	}

	ret = cam_op_add_pending_signal(&action->syncfile->nsobj, op,
					cam_in_syncfile_activate_signal);
	if (ret)
		goto error;

	list_add_tail(&action->entry, &op->post_exec_action_chain);
	return 0;

error:
	if (action->syncfile)
		cam_syncfile_unregister(action->syncfile);
	kfree(action);
	return ret;
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
 * cam_flush_op_dependencies() - Flush operation dependencies
 * @op: pointer to CAM operation
 */
static void cam_flush_op_dependencies(struct cam_obj_op *op)
{
	drain_notify_chain(&op->notify_pending_chain);
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
	op->exec_rw_list_addr	= CAM_NO_RD_WR;
	op->exec_entity		= NULL;
	req->fence_out		= CAM_NO_FENCE;

	if (req->entity == CAM_NO_ENTITY && req->rd_wr_list != CAM_NO_RD_WR)
		return -EINVAL;

	if (req->rd_wr_list != CAM_NO_RD_WR) {
		struct cam_rw_instruction_list rw;
		uintptr_t __user *addr;
		size_t size;

		addr = u64_to_user_ptr(req->rd_wr_list);
		size = sizeof(rw);
		if (copy_from_user(&rw, addr, size))
			goto error;

		if (rw.num_entries == 0)
			goto error;

		size += rw.num_entries * sizeof(struct cam_rw_instruction);
		if (!access_ok(addr, size))
			goto error;

		op->exec_rw_list_addr = addr;
	}

	if (req->entity != CAM_NO_ENTITY) {
		op->exec_entity = cam_entity_lookup(pipeline->cam,
						    req->entity);
		if (!op->exec_entity)
			goto error;
	}

	if (req->flags & CAM_OPERATION_FLAG_EXPORT_FENCE) {
		struct cam_op_exec_action *action;
		u32 id;

		id = cam_obj_id(&op->nsobj);
		action = kzalloc(sizeof(*action), GFP_KERNEL);
		if (!action)
			goto error;

		INIT_LIST_HEAD(&action->entry);
		action->type = CAM_OP_POST_EXEC_ACTION_FENCE_OUT;
		action->syncfile = cam_out_syncfile_register(pipeline->cam,
							     "out-fence-%d",
							     id);
		if (!action->syncfile) {
			kfree(action);
			goto error;
		}

		list_add_tail(&action->entry, &op->post_exec_action_chain);
		req->fence_out = cam_out_syncfile_fd(action->syncfile);
	}

	return 0;

error:
	return -EINVAL;
}

/**
 * cam_pipeline_enqueue() - Create and enqueue an operation
 * @pipeline: pointer to CAM pipeline
 * @req: add request from user-space
 *
 * This creates an operation and adds dependencies to it based on the
 * user-space request.
 * After that the operation will be enqueued immediately if it has no
 * dependencies or all its dependencies are EXECUTED or DELETED.
 *
 * Return: 0 on success or a negative error code otherwise.
 */
int cam_pipeline_enqueue(struct cam_pipeline *pipeline,
			 struct cam_operation_add *req)
{
	struct cam_obj_op *op;
	bool execute;
	int i;

	op = kzalloc(sizeof(struct cam_obj_op), GFP_KERNEL);
	if (!op)
		return -ENOMEM;

	cam_obj_init(&op->nsobj, CAM_OBJ_TYPE_OPERATION,
		     cam_op_release, &pipeline->ns);
	cam_obj_set_id(&op->nsobj, req->id);

	atomic_set(&op->num_blockers, 0);
	INIT_LIST_HEAD(&op->notify_active_chain);
	INIT_LIST_HEAD(&op->notify_pending_chain);
	INIT_LIST_HEAD(&op->post_exec_action_chain);
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
		int ret;

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
			goto error;
	}

	execute = false;

	/* Check pipeline status as late as possible */
	if (!cam_pipeline_active(pipeline))
		goto error;

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

	trace_cam_operation_add(op);

	/*
	 * Not blocked on any signals. Note, the object may already be in
	 * CAM_OPERATION_STATE_DELETED at this point.
	 */
	if (execute)
		cam_op_enqueue(op);

	return 0;

error:
	/*
	 * OP was properly initialized and inserted into the pipeline,
	 * we are here because OP had unmet dependency requirements
	 * (one or more).
	 */

	/*
	 * Mark it as non-executable (just in case) and make it invisible
	 * to query ioctl
	 */
	cam_op_set_state(op, CAM_OPERATION_STATE_DELETED);
	cam_flush_op_dependencies(op);
	/* Now release the object */
	cam_obj_remove(&op->nsobj);
	cam_obj_deinit(&op->nsobj);
	return -EINVAL;
}
ALLOW_ERROR_INJECTION(cam_pipeline_enqueue, ERRNO);

/*
 * This function is called under RCU, so it cannot sleep.
 */
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

static void cam_ns_walk_callback(struct cam_obj *nsobj,
				 struct cam_ns_walk_control *ctl)
{
	struct cam_obj_op *op;
	unsigned long flags;
	bool valid;

	op = nsobj_to_cam_op(nsobj);
	if (!op)
		return;

	/* This is racy but what can we do */
	read_lock_irqsave(&op->notify_lock, flags);
	valid = (op->state & ctl->flags);
	read_unlock_irqrestore(&op->notify_lock, flags);

	if (valid)
		cam_op_enum(op, ctl->data);
}

static void query_state_filter(struct cam_pipeline *pipeline,
			       int state,
			       struct cam_koutput *output)
{
	struct cam_ns_walk_control ctl;

	ctl.data	= output;
	ctl.flags	= state;
	ctl.cb		= cam_ns_walk_callback;
	cam_ns_for_each(&pipeline->ns, &ctl);
}

/**
 * cam_pipeline_query() - Response to user-space operation queries
 * @pipeline: pointer to CAM pipeline
 * @query: query request from user-space
 * @output: pointer to user-space buffer for the output
 *
 * This handles different user-space operation queries based on the query mode.
 * Check @cam_operation_query_mode in UAPI for supported modes.
 *
 * Return: 0 on success or a negative error code otherwise.
 */
int cam_pipeline_query(struct cam_pipeline *pipeline,
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

		op = cam_op_lookup(&pipeline->ns, query->id);
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
ALLOW_ERROR_INJECTION(cam_pipeline_query, ERRNO);

/**
 * cam_pipeline_destroy() - Destroy CAM execution pipeline
 * @pipeline: pointer to CAM pipeline
 *
 * We don't release the pipeline because it's an embedded member of cam_device.
 */
void cam_pipeline_destroy(struct cam_pipeline *pipeline)
{
	cam_ringbuffer_release(&pipeline->event_buffer);
	cam_ns_release(&pipeline->ns);
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
	if (!test_bit(CAM_PIPELINE_IO_ACTIVE, &pipeline->io_state))
		return 0;

	clear_bit(CAM_PIPELINE_IO_ACTIVE, &pipeline->io_state);
	if (WARN_ON(!pipeline->io_thread))
		return -EINVAL;
	wake_up_process(pipeline->io_thread);
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

	ret = cam_ns_init(&pipeline->ns, CAM_NS_POL_USER_ID);
	if (ret)
		return ret;

	ret = cam_ringbuffer_init(&pipeline->event_buffer,
				  sizeof(struct cam_completion),
				  CAM_RINGBUFFER_SIZE);
	if (ret) {
		cam_ns_release(&pipeline->ns);
		return ret;
	}

	INIT_LIST_HEAD(&pipeline->io_queue);
	spin_lock_init(&pipeline->io_queue_lock);
	pipeline->io_thread = NULL;
	pipeline->cam = cam;
	return ret;
}
