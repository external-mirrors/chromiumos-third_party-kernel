/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ISP fence
 *
 * Copyright (C) Google LLC
 */

#ifndef __LINUX_ISP_FENCE_H__
#define __LINUX_ISP_FENCE_H__

#include <linux/isp/isp-device.h>
#include <linux/isp/isp-namespace.h>
#include <linux/dma-fence.h>
#include <linux/types.h>

#include <uapi/linux/isp.h>

struct isp_obj_fence {
	/** @nsobj: namespace object */
	struct isp_obj	nsobj;

	/* ISP fence is either exported (out) or imported (in) */
	union {
		struct export {
			/** @fd: File descriptor of this fence */
			int			fd;
			/** @context_lock: DMA fence context lock */
			spinlock_t		context_lock;
			/** @fence: base dma_fence object */
			struct dma_fence	fence;
		} out;

		struct import {
			/** @fence: base dma_fence object */
			struct dma_fence	*fence;
			/** @cb: DMA fence notify callback */
			struct dma_fence_cb	cb;
			/** @notify_lock: protects list of signals */
			rwlock_t		notify_lock;
			/**
			 * @active_sig_chain: list of operations that are
			 * blocked on us
			 */
			struct list_head	active_sig_chain;
			/** @release_entry: Pipeline fence relase entry */
			struct list_head	release_entry;
			/** @pipeline: ISP pipeilne that owns this fence */
			struct isp_pipeline	*pipeline;
		} in;
	};
};

struct isp_obj_op;

struct isp_obj_fence *isp_out_fence_register(struct isp_ns *ns,
					     u64 context, u64 seqno);

int isp_in_fence_register(struct isp_pipeline *pipeline, u32 fd, u32 id);

int isp_out_fence_fd(struct isp_obj_fence *sf);

struct isp_obj_fence *isp_out_fence_lookup(struct isp_ns *ns, u32 id);
struct isp_obj_fence *isp_in_fence_lookup(struct isp_ns *ns, u32 id);

void isp_in_fence_unregister(struct isp_obj *nsobj);
void isp_out_fence_unregister(struct isp_obj *nsobj);

void isp_fence_put(struct isp_obj_fence *sf);

bool isp_in_fence_activate_signal(struct isp_op_signal *sig);
bool isp_in_fence_deactivate_signal(struct isp_op_signal *sig);

int isp_fire_out_fence_signal(struct isp_obj *nsobj);
int isp_drain_fences(struct isp_pipeline *pipeline);
#endif /* __LINUX_ISP_FENCE_H__ */
