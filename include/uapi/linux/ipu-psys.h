/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* Copyright (C) 2013 - 2020 Intel Corporation */

#ifndef _UAPI_IPU_PSYS_H
#define _UAPI_IPU_PSYS_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

/**
 * struct ipu_psys_capability - Capability query
 *
 * @version:	Version number
 * @driver:	Driver name (unused)
 * @pg_count:	PG count
 * @dev_model:	Device model
 * @reserved:	Reserved
 */
struct ipu_psys_capability {
	uint32_t version;
	uint8_t driver[20];
	uint32_t pg_count;
	uint8_t dev_model[32];
	uint32_t reserved[17];
} __attribute__ ((packed));

/**
 * struct ipu_psys_event - Event occurred due to command execution
 *
 * @type:	Type of event (IPU_PSYS_EVENT_TYPE_CMD_COMPLETE or
 * IPU_PSYS_EVENT_TYPE_BUFFER_COMPLETE)
 * @user_token:	Token of command (given in struct ipu_psys_command)
 * @issue_id:	Unique ID of command
 * @buffer_idx:	Buffer index (unused)
 * @reserved:	Reserved
 */
struct ipu_psys_event {
	uint32_t type;
	uint64_t user_token;
	uint64_t issue_id;
	uint32_t buffer_idx;
	int32_t reserved[2];
} __attribute__ ((packed));

#define IPU_PSYS_EVENT_TYPE_CMD_COMPLETE	1
#define IPU_PSYS_EVENT_TYPE_BUFFER_COMPLETE	2

/**
 * struct ipu_psys_buffer - Buffer for input/output terminals
 *
 * @len:		Total allocated size of base
 * @userptr:		User pointer
 * @resv:		Reserved
 * @fd:			DMA-BUF handle
 * @data_offset:	Offset to valid data
 * @bytes_used:		Amount of valid data including offset
 * @flags:		Flags
 * @isp_buf_id:		ISP buffer object ID
 * @reserved:		Reserved
 */
struct ipu_psys_buffer {
	uint64_t len;
	union {
		int fd;
		void __user *userptr;
		uint64_t resv;
	};
	uint32_t data_offset;
	uint32_t bytes_used;
	uint32_t flags;
	uint32_t isp_buf_id;
	uint32_t reserved[1];
} __attribute__ ((packed));

#define IPU_BUFFER_FLAG_INPUT	(1 << 0)
#define IPU_BUFFER_FLAG_OUTPUT	(1 << 1)
#define IPU_BUFFER_FLAG_MAPPED	(1 << 2)
#define IPU_BUFFER_FLAG_NO_FLUSH	(1 << 3)
#define IPU_BUFFER_FLAG_DMA_HANDLE	(1 << 4)
#define IPU_BUFFER_FLAG_USERPTR	(1 << 5)

#define	IPU_PSYS_CMD_PRIORITY_HIGH	0
#define	IPU_PSYS_CMD_PRIORITY_MED	1
#define	IPU_PSYS_CMD_PRIORITY_LOW	2
#define	IPU_PSYS_CMD_PRIORITY_NUM	3

enum ipu_psys_command_type {
	CMD_TYPE_START,
	CMD_TYPE_ENQUEUE,
	CMD_TYPE_STOP
};

/**
 * struct ipu_psys_command - Processing command
 * @type:		Type of command
 * @issue_id:		Unique id for the command set by user
 * @user_token:		Token of the command
 * @priority:		Priority of the command
 * @pg_manifest:	Userspace pointer to program group manifest
 * @buffers:		Userspace pointers to array of psys dma buf structs
 * @pg_manifest_size:	Size of program group manifest
 * @bufcount:		Number of buffers in buffers array
 * @min_psys_freq:	Minimum psys frequency in MHz used for this cmd
 * @frame_counter:      Counter of current frame synced between isys and psys
 * @kernel_enable_bitmap:       Enable bits for each individual kernel
 * @terminal_enable_bitmap:     Enable bits for each individual terminals
 * @routing_enable_bitmap:      Enable bits for each individual routing
 * @rbm:                        Enable bits for routing
 * @reserved:                   Reserved
 *
 * Specifies a processing command with input and output buffers.
 */
struct ipu_psys_command {
	enum ipu_psys_command_type type;
	uint64_t issue_id;
	uint64_t user_token;
	uint32_t priority;
	void __user *pg_manifest;
	struct ipu_psys_buffer __user *buffers;
	uint32_t pg_manifest_size;
	uint32_t bufcount;
	uint32_t min_psys_freq;
	uint32_t frame_counter;
	uint32_t kernel_enable_bitmap[4];
	uint32_t terminal_enable_bitmap[4];
	uint32_t routing_enable_bitmap[4];
	uint32_t rbm[5];
	uint32_t reserved[3];
} __attribute__ ((packed));

/**
 * struct ipu_psys_manifest - PG Manifest
 *
 * @index:	Package diretory index
 * @size:	Size of manifest buffer
 * @manifest:	Userspace pointer to manifest buffer
 * @reserved:	Reserved
 */
struct ipu_psys_manifest {
	uint32_t index;
	uint32_t size;
	void __user *manifest;
	uint32_t reserved[5];
} __attribute__ ((packed));

#define IPU_REG_QUERYCAP	1
#define IPU_REG_QCMD		2
#define IPU_REG_DQEVENT		3
#define IPU_REG_GET_MANIFEST	4

#endif /* _UAPI_IPU_PSYS_H */
