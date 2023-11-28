// SPDX-License-Identifier: GPL-2.0
/*
 * ISP device management
 *
 * Copyright (C) Google LLC
 * Copyright (C) Intel Corporation
 */

#define pr_fmt(fmt) "isp-device: " fmt

#include <linux/isp/isp-entity.h>
#include <linux/isp/isp-device.h>
#include <linux/isp/isp-ioctl.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#define ISP_NAME			"isp"
#define ISP_DEVICE_COUNT		1

static dev_t isp_dev_t;
static struct isp_device *isp_device;

static inline struct isp_device *devnode_to_isp(struct device *dev)
{
	return container_of(dev, struct isp_device, devnode);
}

static void isp_devnode_release(struct device *devnode)
{
	struct isp_device *isp = devnode_to_isp(devnode);

	isp->release(isp);
}

static void isp_device_release_free(struct isp_device *isp)
{
	isp_ns_release(&isp->ns);
	kfree(isp);
}

static int isp_device_init(struct isp_device *isp)
{
	int ret;

	ret = isp_ns_init(&isp->ns, ISP_NS_POL_UNIQUE_ID);
	if (ret)
		return ret;

	init_rwsem(&isp->ns_enum_lock);
	init_waitqueue_head(&isp->uapi.wait);

	isp->devnode.release = isp_devnode_release;
	device_initialize(&isp->devnode);

	isp->release = isp_device_release_free;
	return 0;
}

/**
 * isp_device_get - Acquire a reference to a ISP device
 *
 * The reference acquired by this function must be released with
 * isp_device_put().
 *
 * Return: The @isp pointer on success, or NULL otherwise.
 */
struct isp_device *isp_device_get(void)
{
	if (!isp_device)
		return NULL;
	if (!devnode_to_isp(get_device(&isp_device->devnode)))
		return NULL;
	return isp_device;
}
EXPORT_SYMBOL_GPL(isp_device_get);

/**
 * isp_device_put - Release a reference to a ISP device
 * @isp: The ISP device
 *
 * This function is part of the ISP device reference count handling. It
 * releases a reference to the @isp device previously acquired by
 * isp_device_get(), or the initial reference acquired by __isp_device_init()
 * or isp_device_init(). When the last reference is released the
 * &isp_device.release function is called, which typically destroys the ISP
 * device. Unless the caller ensures that another reference still exists, the
 * @isp pointer must not be touched once this function returns.
 */
void isp_device_put(struct isp_device *isp)
{
	put_device(&isp->devnode);
}
EXPORT_SYMBOL_GPL(isp_device_put);

static int __must_check isp_device_register(struct isp_device *isp,
					    struct module *owner)
{
	int ret;

	isp->minor = 0;
	isp->cdev.owner = owner;
	isp->devnode.devt = MKDEV(MAJOR(isp_dev_t), isp->minor);
	dev_set_name(&isp->devnode, ISP_NAME);
	cdev_init(&isp->cdev, &isp_file_operations);
	kobject_set_name(&isp->cdev.kobj, ISP_NAME);

	isp->root_entity = isp_root_entity_register(isp);
	if (!isp->root_entity) {
		ret = -EINVAL;
		goto out_release;
	}

	ret = cdev_device_add(&isp->cdev, &isp->devnode);
	if (ret < 0) {
		pr_err("Can't add " ISP_NAME " cdev\n");
		goto out_release;
	}

	/* Get the device for the root node. */
	get_device(&isp->devnode);

	return 0;

out_release:
	return ret;
}

static void isp_device_unregister(struct isp_device *isp)
{
	/*
	 * Block all new UAPI calls, and wait for any in-progress call to
	 * terminate.
	 */
	spin_lock(&isp->uapi.wait.lock);
	isp->uapi.unregister_in_progress = true;
	if (isp->uapi.calls_in_progress)
		wait_event_interruptible_locked(isp->uapi.wait,
						!isp->uapi.calls_in_progress);
	spin_unlock(&isp->uapi.wait.lock);

	isp_entity_unregister(isp->root_entity);
	isp->root_entity = NULL;

	cdev_device_del(&isp->cdev, &isp->devnode);
}

/**
 * isp_ns_enumeration_begin() - Namespace enumeration entry point.
 * @isp: pointer to ISP device
 *
 * Grabs namespace enumeration lock (read mode), so that drivers are not
 * permitted to add new objects in the meantime.
 *
 * This should be called only for global namespace enumeration.
 */
void isp_ns_enumeration_begin(struct isp_device *isp)
{
	down_read(&isp->ns_enum_lock);
}

/**
 * isp_ns_enumeration_end() - Namespace enumeration exit point.
 * @isp: pointer to ISP device
 *
 * Releases namespace enumeration lock (read mode) so that drivers can
 * modify namespace.
 *
 * This should be called only for global namespace enumeration.
 */
void isp_ns_enumeration_end(struct isp_device *isp)
{
	up_read(&isp->ns_enum_lock);
}

/**
 * isp_ns_enumeration_forbid() - Namespace modification entry point.
 * @isp: pointer to ISP device.
 *
 * Driver sometimes need to add several namespace objects which are linked
 * to each other (graph) yet driver can only insert one object at a time,
 * making it possible for user-space objects enumeration to race against
 * drivers' namespace modification. This function forbids namespace
 * enumeration and lets drivers to insert all the objects and properly
 * link them.
 *
 * This should be called only for global namespace modification.
 */
void isp_ns_enumeration_forbid(struct isp_device *isp)
{
	down_write(&isp->ns_enum_lock);
}
EXPORT_SYMBOL_GPL(isp_ns_enumeration_forbid);

/**
 * isp_ns_enumeration_permit() - Namespace modification exit point.
 * @isp: pointer to ISP device
 *
 * This unlocks namespace enumeration.
 *
 * This should be called only for global namespace modification.
 */
void isp_ns_enumeration_permit(struct isp_device *isp)
{
	up_write(&isp->ns_enum_lock);
}
EXPORT_SYMBOL_GPL(isp_ns_enumeration_permit);

/**
 * isp_device_uapi_call_enter - UAPI call entry tracing
 * @isp: The ISP device
 *
 * This function must be called at the beginning of all UAPI call handlers.
 * With isp_device_uapi_call_exit(), it tracks UAPI calls to handle races with
 * isp_device_unregister().
 *
 * If the device is being unregistered, this function will return an error that
 * must be forwarded by the caller to user-space, returning from the UAPI call
 * immediately.
 *
 * Return: 0 on success or an error code otherwise
 */
int isp_device_uapi_call_enter(struct isp_device *isp)
{
	int ret = 0;

	spin_lock(&isp->uapi.wait.lock);
	if (unlikely(isp->uapi.unregister_in_progress))
		ret = -ENOTCONN;
	else if (unlikely(isp->uapi.calls_in_progress == INT_MAX))
		ret = -EBUSY;
	else
		isp->uapi.calls_in_progress++;
	spin_unlock(&isp->uapi.wait.lock);

	WARN_ON(ret == -EBUSY);

	return ret;
}

/**
 * isp_device_uapi_call_exit - UAPI call exit tracing
 * @isp: The ISP device
 *
 * This function must be called at the end of all UAPI call handlers. With
 * isp_device_uapi_call_entry(), it tracks UAPI calls to handle races with
 * isp_device_unregister().
 *
 * When the last UAPI call terminates, this function will wake up any
 * isp_device_unregister() waiting call.
 */
void isp_device_uapi_call_exit(struct isp_device *isp)
{
	bool warn = false;

	spin_lock(&isp->uapi.wait.lock);
	if (isp->uapi.calls_in_progress) {
		isp->uapi.calls_in_progress--;
		wake_up_locked(&isp->uapi.wait);
	} else {
		warn = true;
	}
	spin_unlock(&isp->uapi.wait.lock);

	WARN_ON(warn);
}

static int __init isp_init(void)
{
	struct isp_device *isp;
	int ret;

	ret = alloc_chrdev_region(&isp_dev_t, 0, ISP_DEVICE_COUNT, ISP_NAME);
	if (ret < 0) {
		pr_warn("Can't allocate major for " ISP_NAME "\n");
		return ret;
	}

	isp = kzalloc(sizeof(*isp), GFP_KERNEL);
	if (!isp) {
		ret = -ENOMEM;
		goto err_unregister_chrdev_region;
	}

	ret = isp_device_init(isp);
	if (ret) {
		pr_warn("Can't initialize isp_device\n");
		kfree(isp);
		goto err_unregister_chrdev_region;
	}

	ret = isp_device_register(isp, THIS_MODULE);
	if (ret) {
		pr_warn("Can't register isp_device\n");
		goto err_isp_put;
	}

	isp_device = isp;

	return 0;

err_isp_put:
	isp_device_put(isp);
err_unregister_chrdev_region:
	unregister_chrdev_region(isp_dev_t, ISP_DEVICE_COUNT);
	return ret;
}

static void __exit isp_exit(void)
{
	isp_device_unregister(isp_device);
	isp_device_put(isp_device);
	unregister_chrdev_region(isp_dev_t, ISP_DEVICE_COUNT);
}

subsys_initcall(isp_init);
module_exit(isp_exit);

MODULE_DESCRIPTION("ISP core");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(DMA_BUF);
