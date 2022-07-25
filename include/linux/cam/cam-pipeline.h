/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CAM requests execution pipeline
 *
 * Copyright (C) 2022 Google LLC
 */

#ifndef __LINUX_CAM_PIPELINE_H__
#define __LINUX_CAM_PIPELINE_H__

#include <linux/cam/cam-namespace.h>
#include <linux/cam/cam-ringbuffer.h>
#include <linux/rwsem.h>
#include <linux/sched.h>

#define CAM_PIPELINE_IO_ACTIVE	(1 << 0)

/**
 * cam_pipeline - CAM execution pipeline
 *
 * @TODO more details? (io_uring etc?)
 */
struct cam_pipeline {
	/** @ns: Operations namespace */
	struct cam_ns		ns;
	/** @cam: CAM device */
	struct cam_device	*cam;
	/** @event_buffer: notifications for user-space */
	struct cam_ringbuffer	event_buffer;
	/** @io_thread: IO thread that handles OPs execution */
	struct task_struct	*io_thread;
	/** @io_queue_lock: Spinlock to protect IO queue */
	spinlock_t		io_queue_lock;
	/** @io_queue: A queue of OPs */
	struct list_head	io_queue;
	/** @io_state: IO queue/thread state flags */
	unsigned long		io_state;
};

/**
 * cam_op_signal - signalling mechanism between operations
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
struct cam_op_signal {
	/** @source: Namespace object of the signal source */
	struct cam_obj			*source;
	/** @target: Namespace object of the signal target */
	struct cam_obj			*target;
	/** @entry: List entry in the pending/active lists */
	struct list_head		entry;
	/** @activate: Function that activates pending signal */
	bool (*activate)(struct cam_op_signal *sig);
	/** @fire: Function that raises the signal */
	bool (*fire)(struct cam_op_signal *sig);
};

enum cam_op_exec_action_type {
	CAM_OP_POST_EXEC_ACTION_FENCE_OUT,
	CAM_OP_POST_EXEC_ACTION_FENCE_IN,
};

struct cam_op_exec_action {
	enum cam_op_exec_action_type		type;
	union {
		struct cam_obj_syncfile	*syncfile;
	};
	struct list_head		entry;
};

struct cam_obj_syncfile;

/**
 * cam_obj_op - CAM operations
 *
 * The actual executable request.
 */
struct cam_obj_op {
	/** @nsobj: Namespace object */
	struct cam_obj			nsobj;
	/** @delay_ns: Execution delays */
	u64				delay_ns;
	/** @exec_entity: Entity object OP will be executed on */
	struct cam_obj_entity		*exec_entity;
	/** @exec_rw_list_addr: Pointer to the list of read/write payloads */
	void __user			*exec_rw_list_addr;
	/** @cam: Execution pipeline */
	struct cam_pipeline		*pipeline;
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
	enum cam_operation_state	state;
	/** @notify_active_chain: List of operations that are blocked on us */
	struct list_head		notify_active_chain;
	/**
	 * @notify_pending_chain: List of signals that we will be blocked on.
	 * See comment in cam_pipeline_enqueue().
	 */
	struct list_head		notify_pending_chain;
	/**
	 * @post_exec_action_chain: List of exec actions that need to be
	 * signaled/cleaned up once operation is executed.
	 */
	struct list_head		post_exec_action_chain;
};

struct cam_device;
struct cam_koutput;
struct cam_operation_remove;
struct cam_operation_add;
struct cam_operation_query;

int cam_pipeline_dequeue(struct cam_pipeline *pipeline,
			 struct cam_operation_remove *op);

int cam_pipeline_enqueue(struct cam_pipeline *pipeline,
			 struct cam_operation_add *op);

int cam_pipeline_query(struct cam_pipeline *pipeline,
		       struct cam_query_operations *query,
		       struct cam_koutput *output);

void cam_fire_active_signals(struct list_head *notify_active_chain);
void cam_drain_active_signals(struct list_head *notify_active_chain);

int cam_pipeline_io_setup(struct cam_pipeline *pipeline);
int cam_pipeline_io_release(struct cam_pipeline *pipeline);

void cam_pipeline_destroy(struct cam_pipeline *pipeline);
int cam_pipeline_init(struct cam_device *cam, struct cam_pipeline *pipeline);

#endif /* __LINUX_CAM_PIPELINE_H__ */
