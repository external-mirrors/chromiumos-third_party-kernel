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
 * struct isp_output - Query output data buffer
 *
 * @address:		Memory address of the buffer
 * @size:		Size of the buffer
 */
struct isp_output {
	__u64		address;
	__u32		size;
} __attribute__((packed));

/**
 * struct isp_query_desc - Query request header descriptor
 *
 * @output:		Query results
 */
struct isp_query_desc {
	struct isp_output	output;
} __attribute__((packed));

/**
 * struct isp_header - A set of ISP queries/operations
 *
 * @num_requests:	Number of requests
 * @qd:			Query header descriptor (only for ISP_IOC_QUERY calls)
 * @reserved:		Reserved for alignment and future extension
 */
struct isp_header {
	__u32			num_requests;
	union {
		struct isp_query_desc	qd;
		__u8			reserved[64];
	};
} __attribute__((packed));

#define ISP_ENTITY_NAME_SZ	16

/**
 * struct isp_query_entity_entry - Entity query result entry
 *
 * @id:			Entity identifier (unique)
 * @parent:		Identifier of the parent object or 0 for the root node
 * @name:		Entity name (not unique)
 */
struct isp_query_entity_entry {
	__u32		id;
	__u32		parent;
	char		name[ISP_ENTITY_NAME_SZ];
} __attribute__((packed));

#define ISP_EVENT_NAME_SZ	16

/**
 * struct isp_query_event_entry - Event query result entry
 *
 * @id:			Event identifier (unique for that entity)
 * @name:		Event name (unique for that entity)
 */
struct isp_query_event_entry {
	__u32		id;
	char		name[ISP_EVENT_NAME_SZ];
} __attribute__((packed));

/**
 * struct isp_query_operation_entry - Operation query result entry
 *
 * @id:			Operation identifier
 * @state:		Operation state
 * @num_blockers:	The number of notifications the OP is waiting for
 */
struct isp_query_operation_entry {
	__u32		id;
	__u32		state;
	__u32		num_blockers;
} __attribute__((packed));

/**
 * struct isp_query_dmabuf_entry - DMA buffer query result entry
 *
 * @id:			ISP ID that corresponds to the given DMA buffer
 *			file descriptor (if any)
 */
struct isp_query_dmabuf_entry {
	__u32		id;
};

/**
 * struct isp_query_entities - Query entities
 *
 * @id:			Entity ID or ISP_ENTITY_ID_ROOT (0) for root
 * @num_entities:	out: number of entities read
 * @graph_version:	out: Version of the graph at the time of the query
 */
struct isp_query_entities {
	__u32		id;
	__u32		num_entities;
	__u32		graph_version;
} __attribute__((packed));

/**
 * struct isp_query_events - Query events from an entity
 *
 * @id:			Entity id
 * @num_events:		out: number of events read
 * @graph_version:	out: Version of the graph at the time of the query
 */
struct isp_query_events {
	__u32		id;
	__u32		num_events;
	__u32		graph_version;
} __attribute__((packed));

/**
 * enum isp_operation_query_mode - Query operation IOCTL mode
 *
 * @ISP_OP_QUERY_UNIQUE:	Query exact operation
 * @ISP_OP_QUERY_ALL:		Query all operations
 */
enum isp_operation_query_mode {
	/* zero is left as invalid value to catch possible errors */
	ISP_OP_QUERY_UNIQUE		= 1,
	ISP_OP_QUERY_ALL,
};

/**
 * struct isp_query_operations - Query one or all pipeline operations
 *
 * @id:			id of the operation or ISP_OP_ID_ALL_OP
 * @num_ops:		out: number of operations queried
 * @mode:		Operation query mode (one of isp_operation_query_mode)
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
	/* zero is left as invalid value to catch possible errors */
	ISP_QUERY_TYPE_ENTITIES		= 1,
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
 * @reserved:		Reserved for alignment and future extension
 */
struct isp_query {
	__u32		query_type;
	union {
		struct isp_query_entities	query_entities;
		struct isp_query_events		query_events;
		struct isp_query_operations	query_operations;
		struct isp_query_dmabuf		query_dmabuf;
		__u8				reserved[128];
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
	/* zero is left as invalid value to catch possible errors */
	ISP_OP_DMABUF_ADD		= 1,
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
	/* zero is left as invalid value to catch possible errors */
	ISP_OP_INSTANCE_CREATE		= 1,
	ISP_OP_INSTANCE_DESTROY,
};

/**
 * struct isp_instance_instruction - Operation instance instruction
 *
 * @op:		Entity instance operation
 * @id:		Requested ISP object ID
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
 * struct isp_export_fence_instruction - Export fence instruction
 *
 * @id:		ID of the exported fence
 */
struct isp_export_fence_instruction {
	__u32		id;
};

/**
 * struct isp_signal_fence_instruction - Signal and destroy exported fence
 *
 * @id:		ID of the fence to signal
 */
struct isp_signal_fence_instruction {
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
 * @ISP_READ_INSTRUCTION:		Read instruction
 * @ISP_WRITE_INSTRUCTION:		Write instruction
 * @ISP_DMABUF_INSTRUCTION:		DMA buffer instruction
 * @ISP_INSTANCE_INSTRUCTION:		Entity instance instruction
 * @ISP_EXPORT_FENCE_INSTRUCTION:	Export fence instruction
 * @ISP_SIGNAL_FENCE_INSTRUCTION:	Signal and destroy exported fence
 */
enum isp_rw_instruction_type {
	/* zero is left as invalid value to catch possible errors */
	ISP_READ_INSTRUCTION		= 1,
	ISP_WRITE_INSTRUCTION,
	ISP_DMABUF_INSTRUCTION,
	ISP_INSTANCE_INSTRUCTION,
	ISP_EXPORT_FENCE_INSTRUCTION,
	ISP_SIGNAL_FENCE_INSTRUCTION,
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
 * @of:		Used when type is ISP_EXPORT_FENCE_INSTRUCTION
 * @sf:		User when type is ISP_SIGNAL_FENCE_INSTRUCTION
 * @reserved:	Reserved for alignment and future extension
 */
struct isp_rw_instruction {
	__u32		type;
	__s32		error;
	union {
		struct isp_read_instruction		rd;
		struct isp_write_instruction		wr;
		struct isp_dmabuf_instruction		db;
		struct isp_instance_instruction		in;
		struct isp_export_fence_instruction	ef;
		struct isp_signal_fence_instruction	sf;
		__u8					reserved[128];
	};
} __attribute__((packed));

/**
 * enum isp_dependency_type - Operation dependency entry type
 *
 * @ISP_DEPENDENCY_NONE:	Empty entry (no dependency)
 * @ISP_DEPENDENCY_OP:		Operation dependency
 * @ISP_DEPENDENCY_EVENT:	Event dependency
 * @ISP_DEPENDENCY_FENCE:	Fence (imported fence) dependency
 */
enum isp_dependency_type {
	/* zero is left as invalid value to catch possible errors */
	ISP_DEPENDENCY_NONE		= 1,
	ISP_DEPENDENCY_OP,
	ISP_DEPENDENCY_EVENT,
	ISP_DEPENDENCY_FENCE,
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
	/* zero is left as invalid value to catch possible errors */
	ISP_DEPENDENCY_WEAK_ORDER	= 1,
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
 * @ISP_OP_NO_FENCE:	Set when operation did not export fence
 */
enum isp_fence_fd {
	ISP_OP_NO_FENCE = 0xffffffff,
};

/**
 * enum isp_op_payload - Special values of operation_add payload
 *
 * @ISP_OP_NULL_PTR:	No payload data (NULL pointer)
 */
enum isp_op_payload {
	ISP_OP_NULL_PTR = 0x0,
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
 * @instruction:	Pointer to the operation's read/write instruction or
 *			ISP_OP_NULL_PTR
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
	__u64			instruction;
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
 * @ISP_OPERATION_STATE_SLEEP:		Operation is waiting for events
 * @ISP_OPERATION_STATE_QUEUED:		Operation is queued for execution
 * @ISP_OPERATION_STATE_RUNNING:	Operation is being executed
 * @ISP_OPERATION_STATE_EXECUTED:	Operation was executed
 * @ISP_OPERATION_STATE_DELETED:	Operation was deleted
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
 * @ISP_OP_ID_ALL_OP:	Set when query all operations
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
	/* zero is left as invalid value to catch possible errors */
	ISP_OPERATION_TYPE_ADD		= 1,
	ISP_OPERATION_TYPE_REMOVE,
};

/**
 * struct isp_operation - Operation request
 *
 * @operation_type:	Type of operation request (ISP_OPERATION_TYPE_ADD,
 *			ISP_OPERATION_TYPE_REMOVE or ISP_OPERATION_TYPE_QUERY)
 * @operation_add:	Add operation to execution pipelines
 * @operation_remove:	Remove operation from execution pipeline
 * @reserved:		Reserved for alignment and future extension
 */
struct isp_operation {
	__u32		operation_type;
	union {
		struct isp_operation_add	operation_add;
		struct isp_operation_remove	operation_remove;
		__u8				reserved[128];
	};
} __attribute__((packed));

/**
 * enum isp_entity_id - ISP entity IDs
 *
 * @ISP_ENTITY_ID_ROOT:		Root entity object
 */
enum isp_entity_id {
	ISP_ENTITY_ID_ROOT = 0,
};

/**
 * enum isp_completion_type - ISP completion entry type
 *
 * @ISP_COMPLETION_TYPE_EXECUTED:	Operation executed
 * @ISP_COMPLETION_TYPE_DELETED:	Operation deleted
 */
enum isp_completion_type {
	/* zero is left as invalid value to catch possible errors */
	ISP_COMPLETION_TYPE_EXECUTED	= 1,
	ISP_COMPLETION_TYPE_DELETED,
};

/**
 * struct isp_completion - Operation completion event
 *
 * @seqno:		Sequence Number
 * @id:			Operation ID
 * @type:		Type of event (executed, deleted, buffer overflow)
 * @reserved:		Reserved for alignment and future extension
 */
struct isp_completion {
	__u64		seqno;
	__u32		id;
	__u8		type;
	union {
		__u8		reserved[19];
	};
} __attribute__((packed));

#define ISP_IOC_QUERY(size)	_IOC(_IOC_READ | _IOC_WRITE, '>', 1, (size))
#define ISP_IOC_OPERATION(size)	_IOC(_IOC_READ | _IOC_WRITE, '>', 2, (size))

#endif /* __UAPI_LINUX_ISP_H__ */
