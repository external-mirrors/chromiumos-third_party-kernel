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
#include <linux/workqueue.h>

struct cam_obj_entity;

/**
 * cam_obj_buffer - CAM buffer structure
 *
 * This structure represents a CAM buffer and holds information of the target
 * dma_buf object.
 */
struct cam_obj_buffer {
	/** @nsobj: namespace object */
	struct cam_obj			nsobj;
	/** @dma_buf: pointer to the target dma_buf object */
	struct dma_buf			*dma_buf;
	/** @device_buffer: device-specific DMA-buffer mapping */
	void				*driver_data;
	/** @entity: CAM entity that imported this buffer */
	struct cam_obj_entity		*entity;
	/** @release_work: Deferred buffer release */
	struct work_struct		release_work;
};

struct cam_obj_buffer *cam_buffer_register(struct cam_ns *ns,
					   struct cam_obj_entity *entity,
					   u32 fd,
					   u32 id);
int cam_buffer_unregister(struct cam_ns *ns, u32 id);

struct cam_obj_buffer *cam_buffer_lookup(struct cam_ns *ns, u32 id);
bool cam_buffer_get(struct cam_obj_buffer *buffer);
void cam_buffer_put(struct cam_obj_buffer *buffer);

void *cam_buffer_driver_data(struct cam_obj_buffer *buffer);

struct cam_pipeline;
struct cam_koutput;

int cam_enum_buffer(struct cam_pipeline *pipeline,
		    struct cam_query_dmabuf *query,
		    struct cam_koutput *output);

#endif /* __LINUX_CAM_BUFFER_H__ */
