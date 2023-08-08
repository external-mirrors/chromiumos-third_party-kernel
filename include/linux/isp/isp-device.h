/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ISP device management
 *
 * Copyright (C) Google LLC
 * Copyright (C) Intel Corporation
 */

#ifndef __LINUX_ISP_DEVICE_H__
#define __LINUX_ISP_DEVICE_H__

#include <linux/isp/isp-namespace.h>
#include <linux/isp/isp-pipeline.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rwsem.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/wait.h>

struct file;
struct isp_obj_entity;

/**
 * struct isp_device - ISP device structure
 *
 * This structure represents a complete ISP device.
 */
struct isp_device {
	/** @uapi: UAPI calls tracking. */
	struct {
		/**
		 * @uapi.wait: Wait queue head to wait on UAPI calls completion.
		 **/
		wait_queue_head_t wait;
		/**
		 * @uapi.calls_in_progress: Number of UAPI calls in progress.
		 */
		int calls_in_progress;
		/**
		 * @uapi.unregister_in_progress: Indicates if the ISP device is
		 * being unregistered.
		 */
		bool unregister_in_progress;
	} uapi;

	/** @ns_enum_lock: Namespace objects enumeration (query) lock */
	struct rw_semaphore	ns_enum_lock;

	/** @ns: ISP objects namespace */
	struct isp_ns ns;

	/** @root_entity: ISP root entity object */
	struct isp_obj_entity *root_entity;

	/** @devnode: The device node associated with this ISP device */
	struct device devnode;
	/** @cdev: The character device associated with this ISP device */
	struct cdev cdev;
	/** @minor: The minor number of the character device node */
	int minor;

	/**
	 * @release: Release handler, called when the last reference to the
	 * isp_device is dropped.
	 */
	void (*release)(struct isp_device *isp);
};

/**
 * struct isp_fh - ISP file handle
 *
 * Everything that is specific to a given file handle can be found here
 * (directly or indirectly).
 */
struct isp_fh {
	/** @isp: the ISP device */
	struct isp_device *isp;
	/** @pipeline: Operation execution pipeline */
	struct isp_pipeline pipeline;
};

struct isp_device *isp_device_get(void);
void isp_device_put(struct isp_device *isp);

int isp_device_uapi_call_enter(struct isp_device *isp);
void isp_device_uapi_call_exit(struct isp_device *isp);

void isp_ns_enumeration_begin(struct isp_device *isp);
void isp_ns_enumeration_end(struct isp_device *isp);
void isp_ns_enumeration_forbid(struct isp_device *isp);
void isp_ns_enumeration_permit(struct isp_device *isp);

#endif /* __LINUX_ISP_DEVICE_H__ */
