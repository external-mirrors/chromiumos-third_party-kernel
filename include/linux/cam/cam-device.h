/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CAM device management
 *
 * Copyright (C) 2022 Google LLC
 * Copyright (C) 2020 Intel Corporation
 */

#ifndef __LINUX_CAM_DEVICE_H__
#define __LINUX_CAM_DEVICE_H__

#include <linux/cam/cam-namespace.h>
#include <linux/cam/cam-pipeline.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/wait.h>

struct file;
struct cam_obj_entity;

/**
 * struct cam_device - CAM device structure
 *
 * This structure represents a complete CAM device.
 */
struct cam_device {
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
		 * @uapi.unregister_in_progress: Indicates if the CAM device is
		 * being unregistered.
		 */
		bool unregister_in_progress;
	} uapi;

	/** @ns: CAM objects namespace */
	struct cam_ns ns;

	/** @root_entity: CAM root entity object */
	struct cam_obj_entity *root_entity;

	/** @devnode: The device node associated with this CAM device */
	struct device devnode;
	/** @cdev: The character device associated with this CAM device */
	struct cdev cdev;
	/** @minor: The minor number of the character device node */
	int minor;

	/**
	 * @release: Release handler, called when the last reference to the
	 * cam_device is dropped.
	 */
	void (*release)(struct cam_device *cam);
};

/**
 * struct cam_fh - CAM file handle
 *
 * Everything that is specific to a given file handle can be found here
 * (directly or indirectly).
 */
struct cam_fh {
	/** @cam: the CAM device */
	struct cam_device *cam;
	/** @pipeline: Operation execution pipeline */
	struct cam_pipeline pipeline;
};

struct cam_device *cam_device_get(void);
void cam_device_put(struct cam_device *cam);

int cam_device_uapi_call_enter(struct cam_device *cam);
void cam_device_uapi_call_exit(struct cam_device *cam);

#endif /* __LINUX_CAM_DEVICE_H__ */
