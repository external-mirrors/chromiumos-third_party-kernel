/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CAM sync file
 *
 * Copyright (C) Google LLC
 */

#ifndef __LINUX_CAM_SYNCFILE_H__
#define __LINUX_CAM_SYNCFILE_H__

#include <linux/cam/cam-device.h>
#include <linux/cam/cam-namespace.h>
#include <linux/dma-fence.h>
#include <linux/types.h>

#include <uapi/linux/cam.h>

#define CAM_SYNCFILE_NAME_SZ		16

struct cam_obj_syncfile {
	/** @nsobj: namespace object */
	struct cam_obj	nsobj;

	/* CAM syncfile is either imported (in) or exported (out) */
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

	char		name[CAM_SYNCFILE_NAME_SZ];
};

struct cam_obj_op;

struct cam_obj_syncfile *cam_out_syncfile_register(struct cam_device *cam,
						   struct cam_obj_op *op,
						   const char *namefmt,
						   ...);

struct cam_obj_syncfile *cam_in_syncfile_register(struct cam_device *cam,
						  struct cam_obj_op *op,
						  int fd,
						  const char *namefmt,
						  ...);

int cam_out_syncfile_fd(struct cam_obj_syncfile *sf);

void cam_in_syncfile_unregister(struct cam_obj *nsobj);
void cam_out_syncfile_unregister(struct cam_obj *nsobj);

void cam_syncfile_put(struct cam_obj_syncfile *sf);

bool cam_in_syncfile_activate_signal(struct cam_op_signal *sig);
void cam_in_syncfile_deactivate_signal(struct cam_op_signal *sig);

int cam_fire_out_syncfile_signal(struct cam_obj *nsobj);

#endif /* __LINUX_CAM_SYNCFILE_H__ */
