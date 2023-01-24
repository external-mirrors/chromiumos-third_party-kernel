/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CAM buffer
 *
 * Copyright (C) 2022 Google LLC
 */

#ifndef __LINUX_CAM_BUFFER_H__
#define __LINUX_CAM_BUFFER_H__

#include <linux/cam/cam-namespace.h>
#include <linux/scatterlist.h>
#include <linux/sync_file.h>
#include <linux/dma-buf.h>
#include <linux/types.h>

/**
 * cam_obj_buffer - CAM buffer structure
 *
 * This structure represents a CAM buffer and holds information of the target
 * dma_buf object.
 */
struct cam_obj_buffer {
	/** @nsobj: namespace object */
	struct cam_obj			nsobj;

	/** @phys: the physical address of the buffer */
	u64				phys;
	/** @va: the virtual address of the buffer */
	void				*va;

	/** @dma_buf: pointer to the target dma_buf object */
	struct dma_buf			*dma_buf;
	/** @dma_attach: pointer to the dma_buf_attachment object */
	struct dma_buf_attachment	*dma_attach;
	/** @dma_sgt: pointer to the sg_table object */
	struct sg_table			*dma_sgt;
};

struct cam_obj_entity;

struct cam_obj_buffer *cam_buffer_register(struct cam_ns *ns,
					   struct cam_obj_entity *entity,
					   u32 fd,
					   u32 id);
void cam_buffer_unregister(struct cam_ns *ns, u32 id);

struct cam_obj_buffer *cam_buffer_lookup(struct cam_ns *ns, u32 id);
void cam_buffer_put(struct cam_obj_buffer *buffer);

#endif /* __LINUX_CAM_BUFFER_H__ */
