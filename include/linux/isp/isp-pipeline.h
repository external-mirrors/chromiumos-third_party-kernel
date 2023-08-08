/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ISP requests execution pipeline
 *
 * Copyright (C) Google LLC
 */

#ifndef __LINUX_ISP_PIPELINE_H__
#define __LINUX_ISP_PIPELINE_H__

#include <linux/isp/isp-namespace.h>
#include <linux/isp/isp-ringbuffer.h>
#include <linux/rwsem.h>
#include <linux/sched.h>

#define ISP_PIPELINE_IO_ACTIVE	(1 << 0)
#define ISP_PIPELINE_IO_EXITING	(1 << 1)

/**
 * isp_pipeline - ISP execution pipeline
 *
 * @TODO add documentation
 */
struct isp_pipeline {
	/** @ops: Operations namespace */
	struct isp_ns		ops;
	/** @objs: Namespace of objects specific to this pipeline */
	struct isp_ns		objs;
	/** @isp: ISP device */
	struct isp_device	*isp;
	/** @event_buffer: notifications for user-space */
	struct isp_ringbuffer	event_buffer;
	/** @io_queue_lock: Spinlock to protect IO queue */
	spinlock_t		io_queue_lock;
	/** @io_queue: A queue of OPs */
	struct list_head	io_queue;
	/** @io_thread: IO thread that handles OPs execution */
	struct task_struct	*io_thread;
	/** @io_queue_wait: IO-queue wait_queue */
	wait_queue_head_t	io_queue_wait;
	/** @io_state: IO queue/thread state flags */
	unsigned long		io_state;
	/** @io_relase_lock: Lock to protect IO-thread release */
	struct mutex		io_release_lock;
	/** @id: ID of the pipeline used for debugging */
	int			id;
};

/**
 * isp_op_signal - signalling mechanism between operations
 *
 * This is used to register signal for an operation dependency
 * pair (source-target), so that the source can notify the target
 * when it's no longer a blocker (executed or deleted).
 *
 * All signals are considered "pending" (inactive) in the beginning,
 * and then the pipeline marks them as "active" when it's set up and
 * ready to go.
 *
 * There are two modes for activating the pending signals of an
 * operation in the pipeline flow:
 * - STRICT mode: activate one pending signal at a time (in FIFO order)
 * - WEAK mode: activate all pending signals at once
 *
 * After the attempt of running an operation (no matter it's EXECUTED
 * or DELETED), the pipeline fires all the registered signals of the
 * operation, so the dependencies are notified.
 *
 * Signal holds ref-counters of both target and source objects until
 * it's not signaled and destroyed.
 *
 * We hold references of both, because both can be deleted and
 * namespace will not own the objects by the time we trigger signal.
 *
 * Source and target objects, in theory, can be of any pipeline
 * object type, so we store namespace object. Proper cast and
 * confidence checking should be performed in the fire().
 */
struct isp_op_signal {
	/** @source: Namespace object of the signal source */
	struct isp_obj			*source;
	/** @target: Namespace object of the signal target */
	struct isp_obj			*target;
	/** @activate: Function that activates pending signal */
	bool (*activate)(struct isp_op_signal *sig);
	/** @fire: Function that raises the signal */
	bool (*fire)(struct isp_op_signal *sig);
	/**
	 * @deactivate: Function that removes the signal from the active
	 * chain without raising it
	 */
	void (*deactivate)(struct isp_op_signal *sig);
	/**
	 * @entry: List entry in the pending/active lists of either a
	 * targer or source object, depending on the signal state
	 */
	struct list_head		entry;
	/** @notifiers_entry: List entry in the owner object list */
	struct list_head		notifiers_entry;
	/** @instance: ID of entity instance */
	u32				instance;
};

/**
 * isp_obj_op - ISP operations
 *
 * The actual executable request.
 */
struct isp_obj_op {
	/** @nsobj: Namespace object */
	struct isp_obj			nsobj;
	/** @delay_ns: Execution delays */
	u64				delay_ns;
	/** @exec_entity: Entity object OP will be executed on */
	struct isp_obj_entity		*exec_entity;
	/** @exec_instance: Entity instance object OP will be executed on */
	struct isp_obj_instance		*exec_instance;
	/** @exec_rw_list_addr: Pointer to the list of read/write payloads */
	void __user			*exec_rw_list_addr;
	/** @isp: Execution pipeline */
	struct isp_pipeline		*pipeline;
	/** @num_blocker: Number of objects we are (or will be) blocked on */
	atomic_t			num_blockers;
	/** @io_queue_entry: Entry in pipeline's IO queue */
	struct list_head		io_queue_entry;

	/*
	 * Everything below this line needs to be protected by notify_lock,
	 * including object state transitions.
	 */
	/** @notify_lock: Lock that protects state and notifications */
	rwlock_t			notify_lock;
	/** @state: State of the operation */
	enum isp_operation_state	state;
	/**
	 * @notify_active_chain: List of operations that are blocked on us.
	 * This is a list of imported signals.
	 */
	struct list_head		notify_active_chain;
	/**
	 * @notify_pending_chain: List of signals that we will be blocked on.
	 * This is a list of signals that will be exported.
	 * See comment in isp_pipeline_enqueue().
	 */
	struct list_head		notify_pending_chain;
	/**
	 * @notifiers: List of all signals this OP owns.
	 */
	struct list_head		notifiers;
};

struct isp_device;
struct isp_koutput;
struct isp_operation_remove;
struct isp_operation_add;
struct isp_operation_query;

int isp_pipeline_dequeue(struct isp_pipeline *pipeline,
			 struct isp_operation_remove *op);

int isp_pipeline_enqueue_prepare(struct isp_pipeline *pipeline,
				 struct isp_operation_add *op);
int isp_pipeline_enqueue_submit(struct isp_pipeline *pipeline,
				struct isp_operation_add *op);
int isp_pipeline_enqueue_cancel(struct isp_pipeline *pipeline,
				struct isp_operation_add *op);

int isp_enum_operations(struct isp_pipeline *pipeline,
			struct isp_query_operations *query,
			struct isp_koutput *output);

void isp_fire_active_signals(struct list_head *notify_active_chain);
void isp_instance_fire_active_signals(struct isp_obj_instance *instance,
				      struct list_head *notify_active_chain);

int isp_pipeline_io_setup(struct isp_pipeline *pipeline);
int isp_pipeline_io_release(struct isp_pipeline *pipeline);

void isp_pipeline_destroy(struct isp_pipeline *pipeline);
int isp_pipeline_init(struct isp_device *isp, struct isp_pipeline *pipeline);

#endif /* __LINUX_ISP_PIPELINE_H__ */
