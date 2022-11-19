// SPDX-License-Identifier: GPL-2.0
/*
 * CAM device management
 *
 * Copyright (C) 2022 Google LLC
 * Copyright (C) 2020 Intel Corporation
 */

#define pr_fmt(fmt) "cam-device: " fmt

#include <linux/cam/cam-entity.h>
#include <linux/cam/cam-device.h>
#include <linux/cam/cam-ioctl.h>
#include <linux/device.h>
#include <linux/dma-fence.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#define CAM_NAME			"cam"
#define CAM_DEVICE_COUNT		1

static dev_t cam_dev_t;
static struct bus_type cam_bus = {
	.name = CAM_NAME,
};
static struct cam_device *cam_device;

static inline struct cam_device *devnode_to_cam(struct device *dev)
{
	return container_of(dev, struct cam_device, devnode);
}

static void cam_devnode_release(struct device *devnode)
{
	struct cam_device *cam = devnode_to_cam(devnode);

	cam->release(cam);
}

static void cam_device_release_free(struct cam_device *cam)
{
	cam_ns_release(&cam->ns);
	kfree(cam);
}

static int cam_device_init(struct cam_device *cam)
{
	int ret;

	ret = cam_ns_init(&cam->ns, CAM_NS_POL_UNIQUE_ID);
	if (ret)
		return ret;

	init_waitqueue_head(&cam->uapi.wait);

	cam->devnode.bus = &cam_bus;
	cam->devnode.release = cam_devnode_release;
	device_initialize(&cam->devnode);

	cam->release = cam_device_release_free;

	spin_lock_init(&cam->context_lock);
	cam->fence_context = dma_fence_context_alloc(1);
	atomic64_set(&cam->fence_seqno, 0);

	return 0;
}

/**
 * cam_device_get - Acquire a reference to a CAM device
 *
 * The reference acquired by this function must be released with
 * cam_device_put().
 *
 * Return: The @cam pointer on success, or NULL otherwise.
 */
struct cam_device *cam_device_get(void)
{
	if (!cam_device)
		return NULL;
	if (!devnode_to_cam(get_device(&cam_device->devnode)))
		return NULL;
	return cam_device;
}
EXPORT_SYMBOL_GPL(cam_device_get);

/**
 * cam_device_put - Release a reference to a CAM device
 * @cam: The CAM device
 *
 * This function is part of the CAM device reference count handling. It
 * releases a reference to the @cam device previously acquired by
 * cam_device_get(), or the initial reference acquired by __cam_device_init()
 * or cam_device_init(). When the last reference is released the
 * &cam_device.release function is called, which typically destroys the CAM
 * device. Unless the caller ensures that another reference still exists, the
 * @cam pointer must not be touched once this function returns.
 */
void cam_device_put(struct cam_device *cam)
{
	put_device(&cam->devnode);
}
EXPORT_SYMBOL_GPL(cam_device_put);

static int __must_check cam_device_register(struct cam_device *cam,
					    struct module *owner)
{
	int ret;

	cam->minor = 0;
	cam->cdev.owner = owner;
	cam->devnode.devt = MKDEV(MAJOR(cam_dev_t), cam->minor);
	dev_set_name(&cam->devnode, CAM_NAME);
	cdev_init(&cam->cdev, &cam_file_operations);
	kobject_set_name(&cam->cdev.kobj, CAM_NAME);

	cam->root_entity = cam_root_entity_register(cam);
	if (!cam->root_entity) {
		ret = -EINVAL;
		goto out_release;
	}

	ret = cdev_device_add(&cam->cdev, &cam->devnode);
	if (ret < 0) {
		pr_err("Can't add " CAM_NAME "cdev\n");
		goto out_release;
	}

	/* Get the device for the root node. */
	get_device(&cam->devnode);

	return 0;

out_release:
	return ret;
}

static void cam_device_unregister(struct cam_device *cam)
{
	/*
	 * Block all new UAPI calls, and wait for any in-progress call to
	 * terminate.
	 */
	spin_lock(&cam->uapi.wait.lock);
	cam->uapi.unregister_in_progress = true;
	if (cam->uapi.calls_in_progress)
	        wait_event_interruptible_locked(cam->uapi.wait,
						!cam->uapi.calls_in_progress);
	spin_unlock(&cam->uapi.wait.lock);

	cam_entity_unregister(cam->root_entity);
	cam->root_entity = NULL;

	cdev_device_del(&cam->cdev, &cam->devnode);
}

/**
 * cam_device_uapi_call_enter - UAPI call entry tracing
 * @cam: The CAM device
 *
 * This function must be called at the beginning of all UAPI call handlers.
 * With cam_device_uapi_call_exit(), it tracks UAPI calls to handle races with
 * cam_device_unregister().
 *
 * If the device is being unregistered, this function will return an error that
 * must be forwarded by the caller to userspace, returning from the UAPI call
 * immediately.
 *
 * Return: 0 on success or an error code otherwise
 */
int cam_device_uapi_call_enter(struct cam_device *cam)
{
	int ret = 0;

	spin_lock(&cam->uapi.wait.lock);
	if (unlikely(cam->uapi.unregister_in_progress))
		ret = -ENOTCONN;
	else if (unlikely(cam->uapi.calls_in_progress == INT_MAX))
		ret = -EBUSY;
	else
		cam->uapi.calls_in_progress++;
	spin_unlock(&cam->uapi.wait.lock);

	WARN_ON(ret == -EBUSY);

	return ret;
}

/**
 * cam_device_uapi_call_exit - UAPI call exit tracing
 * @cam: The CAM device
 *
 * This function must be called at the end of all UAPI call handlers. With
 * cam_device_uapi_call_entry(), it tracks UAPI calls to handle races with
 * cam_device_unregister().
 *
 * When the last UAPI call terminates, this function will wake up any
 * cam_device_unregister() waiting call.
 */
void cam_device_uapi_call_exit(struct cam_device *cam)
{
	bool warn = false;

	spin_lock(&cam->uapi.wait.lock);
	if (cam->uapi.calls_in_progress) {
		cam->uapi.calls_in_progress--;
		wake_up_locked(&cam->uapi.wait);
	} else {
		warn = true;
	}
	spin_unlock(&cam->uapi.wait.lock);

	WARN_ON(warn);
}

static int __init cam_init(void)
{
	struct cam_device *cam;
	int ret;

	ret = alloc_chrdev_region(&cam_dev_t, 0, CAM_DEVICE_COUNT, CAM_NAME);
	if (ret < 0) {
		pr_warn("can't allocate major for " CAM_NAME "\n");
		return ret;
	}

	ret = bus_register(&cam_bus);
	if (ret < 0) {
		pr_warn("can't register bus for " CAM_NAME "\n");
		goto err_unregister_chrdev_region;
	}

	cam = kzalloc(sizeof(*cam), GFP_KERNEL);
	if (!cam) {
		ret = -ENOMEM;
		goto err_bus_unregister;
	}

	ret = cam_device_init(cam);
	if (ret) {
		pr_warn("can't initialize cam_device\n");
		kfree(cam);
		goto err_bus_unregister;
	}

	ret = cam_device_register(cam, THIS_MODULE);
	if (ret) {
		pr_warn("can't register cam_device\n");
		goto err_cam_put;
	}

	cam_device = cam;

	return 0;

err_cam_put:
	cam_device_put(cam);

err_bus_unregister:
	bus_unregister(&cam_bus);

err_unregister_chrdev_region:
	unregister_chrdev_region(cam_dev_t, CAM_DEVICE_COUNT);

	return ret;
}

static void __exit cam_exit(void)
{
	cam_device_unregister(cam_device);
	cam_device_put(cam_device);
	bus_unregister(&cam_bus);
	unregister_chrdev_region(cam_dev_t, CAM_DEVICE_COUNT);
}

subsys_initcall(cam_init);
module_exit(cam_exit);

MODULE_DESCRIPTION("CAM core");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(DMA_BUF);
