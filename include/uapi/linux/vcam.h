/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * VCAM test driver
 *
 * Copyright (C) 2022 Google LLC
 */

#ifndef __UAPI_LINUX_VCAM_H__
#define __UAPI_LINUX_VCAM_H__

#include <linux/compiler.h>
#include <linux/const.h>
#include <linux/types.h>

#define VCAM_ROOT_ENTITY_NAME		"VCAM main"
#define VCAM_DMA_IMPORT_ENTITY_NAME	"DMA import"

/*
 * Per VCAM implementation, FAST_IRQ entity is the one that triggers events
 * frequently (hrtimer), we use it for OP execution tests.
 *
 * SLOW_IRQ entity, on the other hand, works on a much slower pace (also
 * hrtimer), so that we can test query_operations and remove_operations.
 */
#define VCAM_FAST_IRQ_ENTITY_NAME	"Fast IRQ"
#define VCAM_SLOW_IRQ_ENTITY_NAME	"Slow IRQ"

#define VCAM_DMABUF_ADD			1
#define VCAM_DMABUF_REMOVE		2

#define INVALID_BUFFER_ID	0xffffffff

/**
 * struct vcam_dmabuf_instruction - DMA instruction (attach/detach/etc.)
 *
 * @type:		Type of instruction
 * @dma_buf_fd:		DMABUF fd
 * @cam_id:		ID of the corresponding CAM buffer object
 */
struct vcam_dmabuf_instruction {
	__u32			type;
	__s32			fd;
	__u32			cam_id;
} __attribute__((packed));

#endif /* __UAPI_LINUX_VCAM_H__ */
