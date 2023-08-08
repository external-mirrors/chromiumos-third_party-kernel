/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ISP sync file
 *
 * Copyright (C) Google LLC
 */

#ifndef __LINUX_ISP_SYNCFILE_H__
#define __LINUX_ISP_SYNCFILE_H__

#include <linux/isp/isp-device.h>
#include <linux/isp/isp-namespace.h>
#include <linux/dma-fence.h>
#include <linux/types.h>

#include <uapi/linux/isp.h>

#define ISP_SYNCFILE_NAME_SZ		16

struct isp_obj_syncfile {
	/** @nsobj: namespace object */
	struct isp_obj	nsobj;

	/* ISP syncfile is either imported (in) or exported (out) */
	union {
		struct export {
			/** @fd: File descriptor of this fence */
			int			fd;
			/** @context_lock: DMA fence context lock */
			spinlock_t		context_lock;
			/** @fence_context: Fence context index */
			u64			fence_context;
			/** @fence_seqno: Accumulative fence seqno */
			atomic64_t		fence_seqno;
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
			 * @notify_active_chain: list of operations that are
			 * blocked on us
			 */
			struct list_head	notify_active_chain;
		} in;
	};

	char		name[ISP_SYNCFILE_NAME_SZ];
};

struct isp_obj_op;

struct isp_obj_syncfile *isp_out_syncfile_register(struct isp_device *isp,
						   struct isp_obj_op *op,
						   const char *namefmt,
						   ...);

struct isp_obj_syncfile *isp_in_syncfile_register(struct isp_device *isp,
						  struct isp_obj_op *op,
						  int fd,
						  const char *namefmt,
						  ...);

int isp_out_syncfile_fd(struct isp_obj_syncfile *sf);

void isp_in_syncfile_unregister(struct isp_obj *nsobj);
void isp_out_syncfile_unregister(struct isp_obj *nsobj);

void isp_syncfile_put(struct isp_obj_syncfile *sf);

bool isp_in_syncfile_activate_signal(struct isp_op_signal *sig);
void isp_in_syncfile_deactivate_signal(struct isp_op_signal *sig);

int isp_fire_out_syncfile_signal(struct isp_obj *nsobj);

#endif /* __LINUX_ISP_SYNCFILE_H__ */
