/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * include/uapi/linux/isp.h
 *
 * Copyright (C) Google LLC
 */

#ifndef __UAPI_LINUX_ISP_H__
#define __UAPI_LINUX_ISP_H__

#include <linux/compiler.h>
#include <linux/const.h>
#include <linux/ioctl.h>
#include <linux/types.h>

/**
 * struct isp_output - Describe output data buffer
 *
 * @address:		Memory address of the buffer
 * @size:		Size of the buffer
 * @length:		Length of the data available (may be larger than size)
 */
struct isp_output {
	__u64		address;
	__u32		size;
	__u32		length;
} __attribute__((packed));

/**
 * struct isp_header - A set of ISP queries/operations
 *
 * @num_requests:	Number of requests
 * @error:		Error index in case of an error
 * @output:		Query results
 */
struct isp_header {
	__u32		num_requests;
	__u32		error;
	struct isp_output output;
} __attribute__((packed));

#define ISP_ENTITY_NAME_SZ	16

/**
 * struct isp_query_entity_entry - Entries from a tree query
 *
 * @id:			Entity identifier (unique)
 * @name:		Entity name (not unique)
 * @parent:		Identifier of the parent object or 0 for the root node
 */
struct isp_query_entity_entry {
	__u32		id;
	char		name[ISP_ENTITY_NAME_SZ];
	__u32		parent;
} __attribute__((packed));

#define ISP_EVENT_NAME_SZ	16

/**
 * struct isp_query_event_entry - Entry in an event query
 *
 * @id:			Event identifier (unique for that entity)
 * @name:		Event name (unique for that entity)
 */
struct isp_query_event_entry {
	__u32		id;
	char		name[ISP_EVENT_NAME_SZ];
} __attribute__((packed));

/**
 * struct isp_query_operation_entry - Entry in an operation query
 *
 * @id:			Operation identifier
 * @state:		Operation state
 */
struct isp_query_operation_entry {
	__u32		id;
	__u32		state;
} __attribute__((packed));

/**
 * struct isp_query_dmabuf_entry - Entry in DMA buffer query
 *
 * @id:			ISP ID that corresponds to the given DMA buffer
 *			file descriptor (if any)
 */
struct isp_query_dmabuf_entry {
	__u32		id;
};

/**
 * enum query_depth - Maximal depth of the query stack
 *
 * ISP_QUERY_EXACT_OBJECT:	Stack size to hold exactly one object
 * ISP_QUERY_ALL_OBJECTS:	Stack size assigned by ISP internally
 */
enum query_depth {
	ISP_QUERY_EXACT_OBJECT	= 1,
	ISP_QUERY_ALL_OBJECTS	= 0xffffffff,
};

/**
 * struct isp_query_entities - Query entities
 *
 * @id:			Entity ID or ISP_OBJ_ID_ROOT (0) for root
 * @maxdepth:		Depth of the query.
 *			- ISP_QUERY_EXACT_OBJECT: object itself
 *			- ISP_QUERY_ALL_OBJECTS: unlimited
 * @num_entities:	output: number of entities read
 * @graph_version:	output: Version of the graph at the time of the query
 */
struct isp_query_entities {
	__u32		id;
	__u32		maxdepth;
	__u32		num_entities;
	__u32		graph_version;
} __attribute__((packed));

/**
 * struct isp_query_events - Query events from an entity
 *
 * @entity:		Entity id
 * @id:			Event id or ISP_QUERY_ALL_OBJECTS (0xffffffff) for all
 * @num_events:		output: number of events read
 * @graph_version:	output: Version of the graph at the time of the query
 */
struct isp_query_events {
	__u32		entity;
	__u32		id;
	__u32		num_events;
	__u32		graph_version;
} __attribute__((packed));

/**
 * enum isp_operation_query_mode - Query operation IOCTL mode
 *
 * ISP_OP_QUERY_UNIQUE:			Query exact operation
 * ISP_OP_QUERY_UNIQUE_AND_DEPS:	Query exact operation and its child
 *					(dependent) operations
 * ISP_OP_QUERY_ALL:			Query all operations
 * ISP_OP_QUERY_SLEEP:			Query operations in sleep state
 * ISP_OP_QUERY_QUEUED:			Query operations in queued state
 */
enum isp_operation_query_mode {
	ISP_OP_QUERY_UNIQUE,
	ISP_OP_QUERY_UNIQUE_AND_DEPS,
	ISP_OP_QUERY_ALL,
	ISP_OP_QUERY_SLEEP,
	ISP_OP_QUERY_QUEUED,
};

/**
 * struct isp_query_operations - Query one or all pipeline operations
 *
 * @id:			id of the operation or ISP_OP_ID_ALL_OP
 * @num_ops:		output: number of operations queried
 * @mode:		ISP_OP_QUERY_UNIQUE, ISP_OP_QUERY_UNIQUE_AND_DEPS,
 *			ISP_OP_QUERY_ALL, ISP_OP_QUERY_SLEEP,
 *			ISP_OP_QUERY_QUEUED
 */
struct isp_query_operations {
	__u32		id;
	__u32		num_ops;
	__u32		mode;
} __attribute__((packed));

/**
 * struct isp_query_dmabuf - Query DMA buffer
 * @fd:			DMA buffer file descriptor
 */
struct isp_query_dmabuf {
	__u32		fd;
};

/**
 * enum query_type - Types of ISP query ioctl
 *
 * @ISP_QUERY_TYPE_ENTITIES:	Query entity objects
 * @ISP_QUERY_TYPE_EVENTS:	Query event objects
 * @ISP_QUERY_TYPE_OPERATIONS:	Query operation objects
 * @ISP_QUERY_TYPE_DMABUF:	Query DMA buffer object
 */
enum query_type {
	ISP_QUERY_TYPE_ENTITIES,
	ISP_QUERY_TYPE_EVENTS,
	ISP_QUERY_TYPE_OPERATIONS,
	ISP_QUERY_TYPE_DMABUF,
};

/**
 * struct isp_query - Objects query ioctl
 *
 * @query_type:		Type of objects to query
 * @query_entities:	Used when query type is ISP_QUERY_TYPE_ENTITIES
 * @query_events:	Used when query type is ISP_QUERY_TYPE_EVENTS
 * @query_operations:	Used when query type is ISP_QUERY_TYPE_OPERATIONS
 * @query_dmabuf:	Used when query type is ISP_QUERY_TYPE_DMABUF
 */
struct isp_query {
	__u32		query_type;
	union {
		struct isp_query_entities	query_entities;
		struct isp_query_events		query_events;
		struct isp_query_operations	query_operations;
		struct isp_query_dmabuf		query_dmabuf;
		__u8 max_query_size[128];
	};
} __attribute__((packed));

/**
 * enum isp_dmabuf_instruction_id - Special RW instructions buffer_id values
 *
 * @ISP_DMABUF_INSTRUCTION_NO_BUFFER:	Operation does not need a DMA buffer
 */
enum isp_dmabuf_instruction_id {
	ISP_DMABUF_INSTRUCTION_NO_BUFFER    = 0xffffffff,
};

/**
 * enum isp_dmabuf_instruction_op - DMA buffer instruction operation type
 *
 * @ISP_OP_DMABUF_ADD:		Add (import) DMA buffer
 * @ISP_OP_DMABUF_REMOVE:	Remove (release) DMA buffer
 */
enum isp_dmabuf_instruction_op {
	ISP_OP_DMABUF_ADD,
	ISP_OP_DMABUF_REMOVE,
};

/**
 * struct isp_dmabuf_instruction - Operation DMA buffer instruction
 *
 * @op:		DMA buffer operation
 * @dma_fd:	DMA buffer ID (or ISP_RW_INSTRUCTION_NO_BUFFER)
 * @buf_id:	Requested ISP object ID
 */
struct isp_dmabuf_instruction {
	__u32		op;
	__u32		dma_fd;
	__u32		buf_id;
};

/**
 * enum isp_instance_instruction_op - Instance instruction operation type
 *
 * @ISP_OP_INSTANCE_CREATE:	Create entity instance (context)
 * @ISP_OP_INSTANCE_DESTROY:	Destroy entity instance (context)
 */
enum isp_instance_instruction_op {
	ISP_OP_INSTANCE_CREATE,
	ISP_OP_INSTANCE_DESTROY,
};

/**
 * struct isp_instance_instruction - Operation instance instruction
 *
 * @op:		Entity instance operation
 * @buf_id:	Requested ISP object ID
 */
struct isp_instance_instruction {
	__u32		op;
	__u32		id;
};

/**
 * enum isp_op_instance_id - Special operation instance values
 *
 * @ISP_OP_NO_INSTANCE:	Operation does not need an instance
 */
enum isp_op_instance_id {
	ISP_OP_NO_INSTANCE	= 0xffffffff,
};

/**
 * struct isp_out_fence_instruction - Export fence instruction
 *
 * @id:		ID of the exported fence
 */
struct isp_out_fence_instruction {
	__u32		id;
};

#define ISP_RW_INSN_MAX_NUM_BUFFERS	1024

/**
 * struct isp_read_instruction - Operation read instruction
 *
 * @reg:		Register to perform operation on
 * @size:		Size of blob data
 * @num_buffers:	Number of DMA-buffer IDs in the buffers list (if any)
 * @buffers_list:	Pointer to array of DMA-buffer IDs or 0x0
 * @ptr:		User pointer to instruction blob
 */
struct isp_read_instruction {
	__u32		reg;
	__u32		size;
	__u32		num_buffers;
	__u64		buffers_list;
	__u64		ptr;
} __attribute__((packed));

/**
 * struct isp_write_instruction - Operation write instruction
 *
 * @reg:		Register to perform operation on
 * @size:		Size of blob data
 * @num_buffers:	Number of DMA-buffer IDs in the buffers list (if any)
 * @buffers_list:	Pointer to array of DMA-buffer IDs or 0x0
 * @ptr:		User pointer to instruction blob
 */
struct isp_write_instruction {
	__u32		reg;
	__u32		size;
	__u32		num_buffers;
	__u64		buffers_list;
	__u64		ptr;
} __attribute__((packed));

/**
 * enum isp_rw_instruction_type - Type of operation instruction
 *
 * @ISP_READ_INSTRUCTION:	Read instruction
 * @ISP_WRITE_INSTRUCTION:	Write instruction
 * @ISP_DMABUF_INSTRUCTION:	DMA buffer instruction
 * @ISP_INSTANCE_INSTRUCTION:	Entity instance instruction
 * @ISP_OUT_FENCE_INSTRUCTION:	Export fence instruction
 */
enum isp_rw_instruction_type {
	ISP_READ_INSTRUCTION,
	ISP_WRITE_INSTRUCTION,
	ISP_DMABUF_INSTRUCTION,
	ISP_INSTANCE_INSTRUCTION,
	ISP_OUT_FENCE_INSTRUCTION,
};

/**
 * struct isp_rw_instruction - Operation read/write instruction
 *
 * @type:	Type of instruction
 * @error:	Instruction error code (if any)
 * @rd:		Used when type is ISP_READ_INSTRUCTION
 * @wr:		Used when type is ISP_WRITE_INSTRUCTION
 * @db:		Used when type is ISP_DMABUF_INSTRUCTION
 * @in:		Used when type is ISP_INSTANCE_INSTRUCTION
 * @of:		Used when type is ISP_OUT_FENCE_INSTRUCTION
 */
struct isp_rw_instruction {
	__u32		type;
	__s32		error;
	union {
		struct isp_read_instruction		rd;
		struct isp_write_instruction		wr;
		struct isp_dmabuf_instruction		db;
		struct isp_instance_instruction		in;
		struct isp_out_fence_instruction	of;
		__u32				reserved[28];
	};
} __attribute__((packed));

/**
 * struct isp_rw_instruction_list - List of read/write instructions
 *
 * @num_entries		- Number of read/write instructions
 * @payload		- Array of read/write instructions
 */
struct isp_rw_instruction_list {
	__u32				num_entries;
	struct isp_rw_instruction	instructions[];
} __attribute__((packed));

/**
 * enum isp_dependency_type - Operation dependency entry type
 *
 * @ISP_DEPENDENCY_NONE:	Empty entry (no dependency)
 * @ISP_DEPENDENCY_OP:		Operation dependency
 * @ISP_DEPENDENCY_EVENT:	Event dependency
 * @ISP_DEPENDENCY_FENCE_IN:	Fence IN (sync_file) dependency
 */
enum isp_dependency_type {
	ISP_DEPENDENCY_NONE,
	ISP_DEPENDENCY_OP,
	ISP_DEPENDENCY_EVENT,
	ISP_DEPENDENCY_FENCE_IN,
};

/**
 * struct isp_dependency - Operation dependency
 *
 * @type:		Type of the object operation depends on or
 *			ISP_DEPENDENCY_NONE.
 * @id:			ID of the object operation depends on
 */
struct isp_dependency {
	__u32		type;
	__u32		id;
} __attribute__((packed));

#define ISP_MAX_DEPENDENCIES		8

/**
 * enum isp_dependency_mode - Dependency execution mode
 *
 * @ISP_DEPENDENCY_WEAK_ORDER:		Weak dependency execution mode
 * @ISP_DEPENDENCY_STRICT_ORDER:	Strict dependency execution mode
 */
enum isp_dependency_mode {
	ISP_DEPENDENCY_WEAK_ORDER,
	ISP_DEPENDENCY_STRICT_ORDER,
};

/**
 * enum operation_entity_id - Special value of operation entity ID
 *
 * @ISP_OP_NO_ENTITY:	Set when operation has no entity to be executed on
 */
enum operation_entity_id {
	ISP_OP_NO_ENTITY = 0xffffffff,
};

/**
 * enum isp_fence_fd - Special values of operation_add out fence FD
 *
 * @ISP_OP_NO_FENCE:	Set when operation did not export fence out (sync_file)
 */
enum isp_fence_fd {
	ISP_OP_NO_FENCE = 0xffffffff,
};

/**
 * enum isp_no_rd_wr - Special values of operation_add RW instructions list
 *
 * @ISP_OP_NO_RW_LIST:	Set when operation has no RW instructions list
 */
enum isp_no_rd_wr {
	ISP_OP_NO_RW_LIST = 0x0,
};

/**
 * struct isp_operation_add - Add operation to the list
 *
 * @id:			ID of the operation
 * @mode:		Dependency execution mode: ISP_DEPENDENCY_WEAK_ORDER
 *			or ISP_DEPENDENCY_STRICT_ORDER.
 * @deps:		Array that describes operation dependencies (if any).
 * @delay_ns:		Time to pause an operation after all its dependencies
 *			are ready
 * @rd_wr_list:		Pointer to the property read/write list or
 *			ISP_OP_NO_RW_LIST
 * @entity:		ID of the entity operation is executed on or
 *			ISP_OP_NO_ENTITY
 * @instance:		ID of the entity instance (context) operation is
 *			executed on or ISP_OP_NO_INSTANCE
 */
struct isp_operation_add {
	__u32			id;
	/*
	 * Pre-execution dependencies list and dependency execution mode
	 */
	__u32			mode;
	struct isp_dependency	deps[ISP_MAX_DEPENDENCIES];
	/*
	 * Execution context specific data (if any)
	 */
	__u64			delay_ns;
	__u64			rd_wr_list;
	__u32			entity;
	__u32			instance;
} __attribute__((packed));

/**
 * struct isp_operation_remove - Remove one operation from the list
 *
 * @id:			id of the operation. Must be present in the current
 *			queue
 */
struct isp_operation_remove {
	__u32		id;
} __attribute__((packed));

/**
 * enum isp_operation_state - ISP operation state
 *
 * ISP_OPERATION_STATE_SLEEP:		Operation is waiting for events
 * ISP_OPERATION_STATE_QUEUED:		Operation is queued for execution
 * ISP_OPERATION_STATE_RUNNING:		Operation is being executed
 * ISP_OPERATION_STATE_EXECUTED:	Operation was executed
 * ISP_OPERATION_STATE_DELETED:		Operation was deleted
 */
enum isp_operation_state {
	ISP_OPERATION_STATE_SLEEP	= _BITUL(0),
	ISP_OPERATION_STATE_QUEUED	= _BITUL(1),
	ISP_OPERATION_STATE_RUNNING	= _BITUL(2),
	ISP_OPERATION_STATE_EXECUTED	= _BITUL(3),
	ISP_OPERATION_STATE_DELETED	= _BITUL(4),
};

/**
 * enum isp_operation_query_id - Predefined operation ID to use when not
 * querying pipeline for a specific operation ID.
 *
 * ISP_OP_ID_ALL_OP:	Set when query all operations
 */
enum isp_operation_query_id {
	ISP_OP_ID_ALL_OP = 0xffffffff,
};

/**
 * enum operation_type - Type of operation ioctl
 *
 * @ISP_OPERATION_TYPE_ADD:	Add operation
 * @ISP_OPERATION_TYPE_REMOVE:	Remove operation
 */
enum operation_type {
	ISP_OPERATION_TYPE_ADD,
	ISP_OPERATION_TYPE_REMOVE,
};

/**
 * struct isp_operation - Operation request
 *
 * @operation_type	- Type of operation request (ISP_OPERATION_TYPE_ADD,
 *			  ISP_OPERATION_TYPE_REMOVE or ISP_OPERATION_TYPE_QUERY)
 * @operation_add	- Add operation to execution pipelines
 * @operation_remove	- Remove operation from execution pipeline
 */
struct isp_operation {
	__u32		operation_type;
	union {
		struct isp_operation_add	operation_add;
		struct isp_operation_remove	operation_remove;
		__u8 reserved[128];
	};
} __attribute__((packed));

/**
 * enum isp_obj_id - ISP object IDs
 *
 * @ISP_OBJ_ID_ROOT:		Root object
 */
enum isp_obj_id {
	ISP_OBJ_ID_ROOT = 0,
};

/**
 * enum isp_completion_type - ISP completion entry type
 *
 * ISP_COMPLETION_TYPE_EXECUTED:		Operation executed
 * ISP_COMPLETION_TYPE_DELETED:			Operation deleted
 */
enum isp_completion_type {
	ISP_COMPLETION_TYPE_EXECUTED,
	ISP_COMPLETION_TYPE_DELETED,
};

/**
 * struct isp_completion - Operation completion event
 *
 * @seqno:		Sequence Number
 * @id:			Operation ID
 * @type:		Type of event (executed, deleted, buffer overflow)
 */
struct isp_completion {
	__u64		seqno;
	__u32		id;
	__u8		type;
	union {
		__u8 reserved[3];
	};
} __attribute__((packed));

#define ISP_IOC_QUERY(size)	_IOC(_IOC_READ | _IOC_WRITE, '>', 1, (size))
#define ISP_IOC_OPERATION(size)	_IOC(_IOC_READ | _IOC_WRITE, '>', 2, (size))

#endif /* __UAPI_LINUX_ISP_H__ */
