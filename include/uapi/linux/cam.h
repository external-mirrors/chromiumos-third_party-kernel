/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * include/uapi/linux/cam.h
 *
 * Copyright (C) Google LLC
 */

#ifndef __UAPI_LINUX_CAM_H__
#define __UAPI_LINUX_CAM_H__

#include <linux/compiler.h>
#include <linux/const.h>
#include <linux/ioctl.h>
#include <linux/types.h>

/**
 * struct cam_output - Describe output data buffer
 *
 * @address:		Memory address of the buffer
 * @size:		Size of the buffer
 * @length:		Length of the data available (may be larger than size)
 */
struct cam_output {
	__u64		address;
	__u32		size;
	__u32		length;
} __attribute__((packed));

/**
 * struct cam_header - A set of CAM queries/operations
 *
 * @num_requests:	Number of requests
 * @error:		Error index in case of an error
 * @output:		Query results
 */
struct cam_header {
	__u32		num_requests;
	__u32		error;
	struct cam_output output;
} __attribute__((packed));

#define CAM_ENTITY_NAME_SZ	16

/**
 * struct cam_query_entity_entry - Entries from a tree query
 *
 * @id:			Entity identifier (unique)
 * @name:		Entity name (not unique)
 * @parent:		Identifier of the parent object or 0 for the root node
 */
struct cam_query_entity_entry {
	__u32		id;
	char		name[CAM_ENTITY_NAME_SZ];
	__u32		parent;
} __attribute__((packed));

#define CAM_EVENT_NAME_SZ	16

/**
 * struct cam_query_event_entry - Entry in an event query
 *
 * @id:			Event identifier (unique for that entity)
 * @name:		Event name (unique for that entity)
 */
struct cam_query_event_entry {
	__u32		id;
	char		name[CAM_EVENT_NAME_SZ];
} __attribute__((packed));

/**
 * struct cam_query_operation_entry - Entry in an operation query
 *
 * @id:			Operation identifier
 * @state:		Operation state
 */
struct cam_query_operation_entry {
	__u32		id;
	__u32		state;
} __attribute__((packed));

/**
 * struct cam_query_dmabuf_entry - Entry in DMA buffer query
 *
 * @id:			CAM ID that corresponds to the given DMA buffer
 *			file descriptor (if any)
 */
struct cam_query_dmabuf_entry {
	__u32		id;
};

/**
 * enum query_depth - Maximal depth of the query stack
 *
 * CAM_QUERY_EXACT_OBJECT:	Stack size to hold exactly one object
 * CAM_QUERY_ALL_OBJECTS:	Stack size assigned by CAM internally
 */
enum query_depth {
	CAM_QUERY_EXACT_OBJECT	= 1,
	CAM_QUERY_ALL_OBJECTS	= 0xffffffff,
};

/**
 * struct cam_query_entities - Query entities
 *
 * @id:			Entity ID or CAM_OBJ_ID_ROOT (0) for root
 * @maxdepth:		Depth of the query.
 *			- CAM_QUERY_EXACT_OBJECT: object itself
 *			- CAM_QUERY_ALL_OBJECTS: unlimited
 * @num_entities:	output: number of entities read
 * @graph_version:	output: Version of the graph at the time of the query
 */
struct cam_query_entities {
	__u32		id;
	__u32		maxdepth;
	__u32		num_entities;
	__u32		graph_version;
} __attribute__((packed));

/**
 * struct cam_query_events - Query events from an entity
 *
 * @entity:		Entity id
 * @id:			Event id or CAM_QUERY_ALL_OBJECTS (0xffffffff) for all
 * @num_events:		output: number of events read
 * @graph_version:	output: Version of the graph at the time of the query
 */
struct cam_query_events {
	__u32		entity;
	__u32		id;
	__u32		num_events;
	__u32		graph_version;
} __attribute__((packed));

/**
 * enum cam_operation_query_mode - Query operation IOCTL mode
 *
 * CAM_OP_QUERY_UNIQUE:			Query exact operation
 * CAM_OP_QUERY_UNIQUE_AND_DEPS:	Query exact operation and its child
 *					(dependent) operations
 * CAM_OP_QUERY_ALL:			Query all operations
 * CAM_OP_QUERY_SLEEP:			Query operations in sleep state
 * CAM_OP_QUERY_QUEUED:			Query operations in queued state
 */
enum cam_operation_query_mode {
	CAM_OP_QUERY_UNIQUE,
	CAM_OP_QUERY_UNIQUE_AND_DEPS,
	CAM_OP_QUERY_ALL,
	CAM_OP_QUERY_SLEEP,
	CAM_OP_QUERY_QUEUED,
};

/**
 * struct cam_query_operations - Query one or all pipeline operations
 *
 * @id:			id of the operation or CAM_OP_ID_ALL_OP
 * @num_ops:		output: number of operations queried
 * @mode:		CAM_OP_QUERY_UNIQUE, CAM_OP_QUERY_UNIQUE_AND_DEPS,
 *			CAM_OP_QUERY_ALL, CAM_OP_QUERY_SLEEP,
 *			CAM_OP_QUERY_QUEUED
 */
struct cam_query_operations {
	__u32		id;
	__u32		num_ops;
	__u32		mode;
} __attribute__((packed));

/**
 * struct cam_query_dmabuf - Query DMA buffer
 * @fd:			DMA buffer file descriptor
 */
struct cam_query_dmabuf {
	__u32		fd;
};

/**
 * enum query_type - Types of CAM query ioctl
 *
 * @CAM_QUERY_TYPE_ENTITIES:	Query entity objects
 * @CAM_QUERY_TYPE_EVENTS:	Query event objects
 * @CAM_QUERY_TYPE_OPERATIONS:	Query operation objects
 * @CAM_QUERY_TYPE_DMABUF:	Query DMA buffer object
 */
enum query_type {
	CAM_QUERY_TYPE_ENTITIES,
	CAM_QUERY_TYPE_EVENTS,
	CAM_QUERY_TYPE_OPERATIONS,
	CAM_QUERY_TYPE_DMABUF,
};

/**
 * struct cam_query - Objects query ioctl
 *
 * @query_type:		Type of objects to query
 * @query_entities:	Used when query type is CAM_QUERY_TYPE_ENTITIES
 * @query_events:	Used when query type is CAM_QUERY_TYPE_EVENTS
 * @query_operations:	Used when query type is CAM_QUERY_TYPE_OPERATIONS
 * @query_dmabuf:	Used when query type is CAM_QUERY_TYPE_DMABUF
 */
struct cam_query {
	__u32		query_type;
	union {
		struct cam_query_entities	query_entities;
		struct cam_query_events		query_events;
		struct cam_query_operations	query_operations;
		struct cam_query_dmabuf		query_dmabuf;
		__u8 max_query_size[128];
	};
} __attribute__((packed));

/**
 * enum cam_dmabuf_instruction_id - Special RW instructions buffer_id values
 *
 * @CAM_DMABUF_INSTRUCTION_NO_BUFFER:	Operation does not need a DMA buffer
 */
enum cam_dmabuf_instruction_id {
	CAM_DMABUF_INSTRUCTION_NO_BUFFER    = 0xffffffff,
};

/**
 * enum cam_dmabuf_instruction_op - DMA buffer instruction operation type
 *
 * @CAM_OP_DMABUF_ADD:		Add (import) DMA buffer
 * @CAM_OP_DMABUF_REMOVE:	Remove (release) DMA buffer
 */
enum cam_dmabuf_instruction_op {
	CAM_OP_DMABUF_ADD,
	CAM_OP_DMABUF_REMOVE,
};

/**
 * struct cam_dmabuf_instruction - Operation DMA buffer instruction
 *
 * @op:		DMA buffer operation
 * @dma_fd:	DMA buffer ID (or CAM_RW_INSTRUCTION_NO_BUFFER)
 * @buf_id:	Requested CAM object ID
 */
struct cam_dmabuf_instruction {
	__u32		op;
	__u32		dma_fd;
	__u32		buf_id;
};

/**
 * enum cam_instance_instruction_op - Instance instruction operation type
 *
 * @CAM_OP_INSTANCE_CREATE:	Create entity instance (context)
 * @CAM_OP_INSTANCE_DESTROY:	Destroy entity instance (context)
 */
enum cam_instance_instruction_op {
	CAM_OP_INSTANCE_CREATE,
	CAM_OP_INSTANCE_DESTROY,
};

/**
 * struct cam_instance_instruction - Operation instance instruction
 *
 * @op:		Entity instance operation
 * @buf_id:	Requested CAM object ID
 */
struct cam_instance_instruction {
	__u32		op;
	__u32		id;
};

/**
 * enum cam_op_instance_id - Special operation instance values
 *
 * @CAM_OP_NO_INSTANCE:	Operation does not need an instance
 */
enum cam_op_instance_id {
	CAM_OP_NO_INSTANCE	= 0xffffffff,
};

/**
 * struct cam_out_fence_instruction - Export fence instruction
 *
 * @id:		ID of the exported fence
 */
struct cam_out_fence_instruction {
	__u32		id;
};

#define CAM_RW_INSN_MAX_NUM_BUFFERS	1024

/**
 * struct cam_read_instruction - Operation read instruction
 *
 * @reg:		Register to perform operation on
 * @size:		Size of blob data
 * @num_buffers:	Number of DMA-buffer IDs in the buffers list (if any)
 * @buffers_list:	Pointer to array of DMA-buffer IDs or 0x0
 * @ptr:		User pointer to instruction blob
 */
struct cam_read_instruction {
	__u32		reg;
	__u32		size;
	__u32		num_buffers;
	__u64		buffers_list;
	__u64		ptr;
} __attribute__((packed));

/**
 * struct cam_write_instruction - Operation write instruction
 *
 * @reg:		Register to perform operation on
 * @size:		Size of blob data
 * @num_buffers:	Number of DMA-buffer IDs in the buffers list (if any)
 * @buffers_list:	Pointer to array of DMA-buffer IDs or 0x0
 * @ptr:		User pointer to instruction blob
 */
struct cam_write_instruction {
	__u32		reg;
	__u32		size;
	__u32		num_buffers;
	__u64		buffers_list;
	__u64		ptr;
} __attribute__((packed));

/**
 * enum cam_rw_instruction_type - Type of operation instruction
 *
 * @CAM_READ_INSTRUCTION:	Read instruction
 * @CAM_WRITE_INSTRUCTION:	Write instruction
 * @CAM_DMABUF_INSTRUCTION:	DMA buffer instruction
 * @CAM_INSTANCE_INSTRUCTION:	Entity instance instruction
 * @CAM_OUT_FENCE_INSTRUCTION:	Export fence instruction
 */
enum cam_rw_instruction_type {
	CAM_READ_INSTRUCTION,
	CAM_WRITE_INSTRUCTION,
	CAM_DMABUF_INSTRUCTION,
	CAM_INSTANCE_INSTRUCTION,
	CAM_OUT_FENCE_INSTRUCTION,
};

/**
 * struct cam_rw_instruction - Operation read/write instruction
 *
 * @type:	Type of instruction
 * @error:	Instruction error code (if any)
 * @rd:		Used when type is CAM_READ_INSTRUCTION
 * @wr:		Used when type is CAM_WRITE_INSTRUCTION
 * @db:		Used when type is CAM_DMABUF_INSTRUCTION
 * @in:		Used when type is CAM_INSTANCE_INSTRUCTION
 * @of:		Used when type is CAM_OUT_FENCE_INSTRUCTION
 */
struct cam_rw_instruction {
	__u32		type;
	__s32		error;
	union {
		struct cam_read_instruction		rd;
		struct cam_write_instruction		wr;
		struct cam_dmabuf_instruction		db;
		struct cam_instance_instruction		in;
		struct cam_out_fence_instruction	of;
		__u32				reserved[28];
	};
} __attribute__((packed));

/**
 * struct cam_rw_instruction_list - List of read/write instructions
 *
 * @num_entries		- Number of read/write instructions
 * @payload		- Array of read/write instructions
 */
struct cam_rw_instruction_list {
	__u32				num_entries;
	struct cam_rw_instruction	instructions[];
} __attribute__((packed));

/**
 * enum cam_dependency_type - Operation dependency entry type
 *
 * @CAM_DEPENDENCY_NONE:	Empty entry (no dependency)
 * @CAM_DEPENDENCY_OP:		Operation dependency
 * @CAM_DEPENDENCY_EVENT:	Event dependency
 * @CAM_DEPENDENCY_FENCE_IN:	Fence IN (sync_file) dependency
 */
enum cam_dependency_type {
	CAM_DEPENDENCY_NONE,
	CAM_DEPENDENCY_OP,
	CAM_DEPENDENCY_EVENT,
	CAM_DEPENDENCY_FENCE_IN,
};

/**
 * struct cam_dependency - Operation dependency
 *
 * @type:		Type of the object operation depends on or
 *			CAM_DEPENDENCY_NONE.
 * @id:			ID of the object operation depends on
 */
struct cam_dependency {
	__u32		type;
	__u32		id;
} __attribute__((packed));

#define CAM_MAX_DEPENDENCIES		8

/**
 * enum cam_dependency_mode - Dependency execution mode
 *
 * @CAM_DEPENDENCY_WEAK_ORDER:		Weak dependency execution mode
 * @CAM_DEPENDENCY_STRICT_ORDER:	Strict dependency execution mode
 */
enum cam_dependency_mode {
	CAM_DEPENDENCY_WEAK_ORDER,
	CAM_DEPENDENCY_STRICT_ORDER,
};

/**
 * enum operation_entity_id - Special value of operation entity ID
 *
 * @CAM_OP_NO_ENTITY:	Set when operation has no entity to be executed on
 */
enum operation_entity_id {
	CAM_OP_NO_ENTITY = 0xffffffff,
};

/**
 * enum cam_fence_fd - Special values of operation_add out fence FD
 *
 * @CAM_OP_NO_FENCE:	Set when operation did not export fence out (sync_file)
 */
enum cam_fence_fd {
	CAM_OP_NO_FENCE = 0xffffffff,
};

/**
 * enum cam_no_rd_wr - Special values of operation_add RW instructions list
 *
 * @CAM_OP_NO_RW_LIST:	Set when operation has no RW instructions list
 */
enum cam_no_rd_wr {
	CAM_OP_NO_RW_LIST = 0x0,
};

/**
 * struct cam_operation_add - Add operation to the list
 *
 * @id:			ID of the operation
 * @mode:		Dependency execution mode: CAM_DEPENDENCY_WEAK_ORDER
 *			or CAM_DEPENDENCY_STRICT_ORDER.
 * @deps:		Array that describes operation dependencies (if any).
 * @delay_ns:		Time to pause an operation after all its dependencies
 *			are ready
 * @rd_wr_list:		Pointer to the property read/write list or
 *			CAM_OP_NO_RW_LIST
 * @entity:		ID of the entity operation is executed on or
 *			CAM_OP_NO_ENTITY
 * @instance:		ID of the entity instance (context) operation is
 *			executed on or CAM_OP_NO_INSTANCE
 */
struct cam_operation_add {
	__u32			id;
	/*
	 * Pre-execution dependencies list and dependency execution mode
	 */
	__u32			mode;
	struct cam_dependency	deps[CAM_MAX_DEPENDENCIES];
	/*
	 * Execution context specific data (if any)
	 */
	__u64			delay_ns;
	__u64			rd_wr_list;
	__u32			entity;
	__u32			instance;
} __attribute__((packed));

/**
 * struct cam_operation_remove - Remove one operation from the list
 *
 * @id:			id of the operation. Must be present in the current
 *			queue
 */
struct cam_operation_remove {
	__u32		id;
} __attribute__((packed));

/**
 * enum cam_operation_state - CAM operation state
 *
 * CAM_OPERATION_STATE_SLEEP:		Operation is waiting for events
 * CAM_OPERATION_STATE_QUEUED:		Operation is queued for execution
 * CAM_OPERATION_STATE_RUNNING:		Operation is being executed
 * CAM_OPERATION_STATE_EXECUTED:	Operation was executed
 * CAM_OPERATION_STATE_DELETED:		Operation was deleted
 */
enum cam_operation_state {
	CAM_OPERATION_STATE_SLEEP	= _BITUL(0),
	CAM_OPERATION_STATE_QUEUED	= _BITUL(1),
	CAM_OPERATION_STATE_RUNNING	= _BITUL(2),
	CAM_OPERATION_STATE_EXECUTED	= _BITUL(3),
	CAM_OPERATION_STATE_DELETED	= _BITUL(4),
};

/**
 * enum cam_operation_query_id - Predefined operation ID to use when not
 * querying pipeline for a specific operation ID.
 *
 * CAM_OP_ID_ALL_OP:	Set when query all operations
 */
enum cam_operation_query_id {
	CAM_OP_ID_ALL_OP = 0xffffffff,
};

/**
 * enum operation_type - Type of operation ioctl
 *
 * @CAM_OPERATION_TYPE_ADD:	Add operation
 * @CAM_OPERATION_TYPE_REMOVE:	Remove operation
 */
enum operation_type {
	CAM_OPERATION_TYPE_ADD,
	CAM_OPERATION_TYPE_REMOVE,
};

/**
 * struct cam_operation - Operation request
 *
 * @operation_type	- Type of operation request (CAM_OPERATION_TYPE_ADD,
 *			  CAM_OPERATION_TYPE_REMOVE or CAM_OPERATION_TYPE_QUERY)
 * @operation_add	- Add operation to execution pipelines
 * @operation_remove	- Remove operation from execution pipeline
 */
struct cam_operation {
	__u32		operation_type;
	union {
		struct cam_operation_add	operation_add;
		struct cam_operation_remove	operation_remove;
		__u8 reserved[128];
	};
} __attribute__((packed));

/**
 * enum cam_obj_id - CAM object IDs
 *
 * @CAM_OBJ_ID_ROOT:		Root object
 */
enum cam_obj_id {
	CAM_OBJ_ID_ROOT = 0,
};

/**
 * enum cam_completion_type - CAM completion entry type
 *
 * CAM_COMPLETION_TYPE_EXECUTED:		Operation executed
 * CAM_COMPLETION_TYPE_DELETED:			Operation deleted
 */
enum cam_completion_type {
	CAM_COMPLETION_TYPE_EXECUTED,
	CAM_COMPLETION_TYPE_DELETED,
};

/**
 * struct cam_completion - Operation completion event
 *
 * @seqno:		Sequence Number
 * @id:			Operation ID
 * @type:		Type of event (executed, deleted, buffer overflow)
 */
struct cam_completion {
	__u64		seqno;
	__u32		id;
	__u8		type;
	union {
		__u8 reserved[3];
	};
} __attribute__((packed));

#define CAM_IOC_QUERY(size)	_IOC(_IOC_READ | _IOC_WRITE, '>', 1, (size))
#define CAM_IOC_OPERATION(size)	_IOC(_IOC_READ | _IOC_WRITE, '>', 2, (size))

#endif /* __UAPI_LINUX_CAM_H__ */
