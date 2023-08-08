/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ISP buffer
 *
 * Copyright (C) Google LLC
 */

#ifndef __LINUX_ISP_BUFFER_H__
#define __LINUX_ISP_BUFFER_H__

#include <linux/isp/isp-namespace.h>
#include <linux/scatterlist.h>
#include <linux/sync_file.h>
#include <linux/dma-buf.h>
#include <linux/types.h>
#include <linux/workqueue.h>

struct isp_obj_entity;

/**
 * isp_obj_buffer - ISP buffer structure
 *
 * This structure represents a ISP buffer and holds information of the target
 * dma_buf object.
 */
struct isp_obj_buffer {
	/** @nsobj: namespace object */
	struct isp_obj			nsobj;
	/** @dma_buf: pointer to the target dma_buf object */
	struct dma_buf			*dma_buf;
	/** @device_buffer: device-specific DMA-buffer mapping */
	void				*driver_data;
	/** @entity: ISP entity that imported this buffer */
	struct isp_obj_entity		*entity;
	/** @release_work: Deferred buffer release */
	struct work_struct		release_work;
};

struct isp_obj_buffer *isp_buffer_register(struct isp_ns *ns,
					   struct isp_obj_entity *entity,
					   u32 fd,
					   u32 id);
int isp_buffer_unregister(struct isp_ns *ns, u32 id);

struct isp_obj_buffer *isp_buffer_lookup(struct isp_ns *ns, u32 id);
bool isp_buffer_get(struct isp_obj_buffer *buffer);
void isp_buffer_put(struct isp_obj_buffer *buffer);

void *isp_buffer_driver_data(struct isp_obj_buffer *buffer);

struct isp_pipeline;
struct isp_koutput;

int isp_enum_buffer(struct isp_pipeline *pipeline,
		    struct isp_query_dmabuf *query,
		    struct isp_koutput *output);

int isp_drain_buffers(struct isp_pipeline *pipeline);

#endif /* __LINUX_ISP_BUFFER_H__ */
