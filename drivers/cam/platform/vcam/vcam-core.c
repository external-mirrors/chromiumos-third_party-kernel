// SPDX-License-Identifier: GPL-2.0
/*
 * CAM test device driver
 *
 * Copyright (C) 2022 Google LLC
 */

#include <linux/cam/cam-buffer.h>
#include <linux/cam/cam-device.h>
#include <linux/cam/cam-entity.h>
#include <linux/delay.h>
#include <linux/dma-buf.h>
#include <linux/dma-resv.h>
#include <linux/hrtimer.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <uapi/linux/vcam.h>

#define VCAM_DEVICE_NAME		"vcam"

#define EVENT_TRIGGER_MS		1010

#define ENTITY_FAST_IRQ			0
#define ENTITY_FAST_IRQ_EVENT		0

#define ENTITY_SLOW_IRQ			1
#define ENTITY_SLOW_IRQ_EVENT		1

static const char *entity_names[] = {
	VCAM_ROOT_ENTITY_NAME,
	VCAM_FAST_IRQ_ENTITY_NAME,
	VCAM_SLOW_IRQ_ENTITY_NAME,
};

struct vcam_device {
	struct device			*dev;
	struct cam_device		*cam;

	struct cam_obj_entity		*root_entity;
	struct cam_obj_entity		*entities[2];
	struct cam_obj_event		*events[2];

	spinlock_t			instances_lock;
	struct list_head		instances;

	struct hrtimer			event_timer_fast;
	struct hrtimer			event_timer_slow;
	unsigned long			timer_start_ts;
};

struct vcam_exec_instance {
	struct cam_obj_instance		*instance;
	struct list_head		entry;
};

#define VCAM_NUM_INSTANCES		8
#define VCAM_INSTANCE_DATA_BUF_SZ	64

struct vcam_entity_instance_data {
	char				*buf;
};

struct vcam_buffer {
	u64				phys;
	struct dma_buf_attachment	*dma_attach;
	struct sg_table			*dma_sgt;
};

static void *entity_instance_create(void *dev)
{
	struct vcam_entity_instance_data *data;

	pr_devel("VCAM: instance create\n");

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return NULL;

	data->buf = kzalloc(VCAM_INSTANCE_DATA_BUF_SZ, GFP_KERNEL);
	if (!data->buf) {
		kfree(data);
		data = NULL;
	}
	return data;
}

static void entity_instance_destroy(void *dev, void *instance_data)
{
	struct vcam_entity_instance_data *data = instance_data;

	pr_devel("VCAM: instance destroy\n");
	kfree(data->buf);
	kfree(data);
}

static int record_event_instance(struct vcam_device *vcam,
				 struct cam_obj_instance *instance)
{
	struct vcam_exec_instance *inst;
	unsigned long flags;

	inst = kzalloc(sizeof(*inst), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	if (!cam_instance_get(instance)) {
		kfree(inst);
		return -EINVAL;
	}

	INIT_LIST_HEAD(&inst->entry);
	inst->instance = instance;

	spin_lock_irqsave(&vcam->instances_lock, flags);
	list_add(&inst->entry, &vcam->instances);
	spin_unlock_irqrestore(&vcam->instances_lock, flags);
	return 0;
}

static int entity_instance_read(void *dev,
				struct cam_obj_instance *instance,
				struct cam_read_instruction *rw)
{
	char dummy_buffer[32] = {};
	u64 len;

	pr_devel("VCAM: execute entity register %u read\n", rw->reg);

	if (!rw->size)
		return 0;

	strcpy(dummy_buffer, "From VCAM driver");
	len = strlen(dummy_buffer);
	if (rw->size < len)
		len = rw->size;
	if (copy_to_user(u64_to_user_ptr(rw->ptr), dummy_buffer, len)) {
		pr_err("VCAM: cannot copy to user\n");
		return -EINVAL;
	}

	return 0;
}

static int entity_instance_write(void *dev,
				 struct cam_obj_instance *instance,
				 struct cam_write_instruction *rw)
{
	struct vcam_device *vcam = dev;
	char dummy_buffer[32] = {};

	pr_devel("VCAM: execute entity register %u write\n", rw->reg);

	if (rw->size > sizeof(dummy_buffer)) {
		pr_err("VCAM: write size is too large");
		return -EINVAL;
	}

	if (copy_from_user(dummy_buffer, u64_to_user_ptr(rw->ptr), rw->size)) {
		pr_err("VCAM: cannot copy from user\n");
		return -EINVAL;
	}

	pr_devel("VCAM: register write payload: %s\n", dummy_buffer);
	return record_event_instance(vcam, instance);
}

static void dmabuf_remove(void *dev, void *buf, struct dma_buf *dma_buf)
{
	struct vcam_buffer *buffer;

	buffer = (struct vcam_buffer *)buf;
	if (!IS_ERR_OR_NULL(buffer->dma_sgt)) {
		dma_resv_lock(buffer->dma_attach->dmabuf->resv, NULL);
		dma_buf_unmap_attachment(buffer->dma_attach,
					 buffer->dma_sgt,
					 DMA_TO_DEVICE);
		dma_resv_unlock(buffer->dma_attach->dmabuf->resv);
	}

	if (!IS_ERR_OR_NULL(buffer->dma_attach))
		dma_buf_detach(dma_buf, buffer->dma_attach);

	kfree(buffer);
}

static void *dmabuf_add(void *dev, struct dma_buf *dma_buf)
{
	struct vcam_buffer *buffer;
	struct vcam_device *vcam;

	vcam = (struct vcam_device *)dev;
	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer)
		return NULL;

	buffer->dma_attach = dma_buf_attach(dma_buf, vcam->dev);
	if (IS_ERR(buffer->dma_attach))
		goto error;

	dma_resv_lock(buffer->dma_attach->dmabuf->resv, NULL);
	buffer->dma_sgt = dma_buf_map_attachment(buffer->dma_attach,
						 DMA_TO_DEVICE);
	dma_resv_unlock(buffer->dma_attach->dmabuf->resv);
	if (IS_ERR(buffer->dma_sgt))
		goto error;

	buffer->phys = sg_dma_address(buffer->dma_sgt->sgl);
	return buffer;

error:
	dmabuf_remove(dev, buffer, dma_buf);
	return NULL;
}

static struct cam_entity_ops entity_ops = {
	.instance_read		= entity_instance_read,
	.instance_write		= entity_instance_write,
	.instance_create	= entity_instance_create,
	.instance_destroy	= entity_instance_destroy,
	.dmabuf_add		= dmabuf_add,
	.dmabuf_remove		= dmabuf_remove,
};

static void trigger_event_on(struct vcam_device *vcam,
			     u32 entity_id,
			     u32 event_id)
{
	struct vcam_exec_instance *inst;
	unsigned long flags;

	if (!vcam->entities[entity_id] || !vcam->events[event_id])
		return;

	cam_event_trigger_signals(vcam->entities[entity_id],
				  vcam->events[event_id]);

	spin_lock_irqsave(&vcam->instances_lock, flags);
	while (!list_empty(&vcam->instances)) {
		inst = list_first_entry(&vcam->instances,
					struct vcam_exec_instance,
					entry);
		list_del(&inst->entry);

		cam_instance_event_trigger_signals(vcam->entities[entity_id],
						   inst->instance,
						   vcam->events[event_id]);
		cam_instance_put(inst->instance);
		kfree(inst);
	}
	spin_unlock_irqrestore(&vcam->instances_lock, flags);
}

static enum hrtimer_restart vcam_event_timer(struct vcam_device *vcam)
{
	/*
	 * Trigger only ENTITY_FAST_IRQ events. We use other entities
	 * block OPs on and to test query/add/remove ioctl().
	 */
	trigger_event_on(vcam, ENTITY_FAST_IRQ, ENTITY_FAST_IRQ_EVENT);

	/*
	 * Flush all events every 5 seconds.
	 * 5s ought to be enough for anybody.
	 */
	if (!time_after(jiffies, vcam->timer_start_ts + 5 * HZ))
		return HRTIMER_RESTART;

	/* We cancel HR timer, send spurious wakeup to all entities */
	trigger_event_on(vcam, ENTITY_SLOW_IRQ, ENTITY_SLOW_IRQ_EVENT);
	trigger_event_on(vcam, ENTITY_FAST_IRQ, ENTITY_FAST_IRQ_EVENT);

	vcam->timer_start_ts = jiffies;

	return HRTIMER_RESTART;
}

static enum hrtimer_restart vcam_event_hrtimer_fast(struct hrtimer *hrtimer)
{
	struct vcam_device *vcam = container_of(hrtimer,
						struct vcam_device,
						event_timer_fast);
	enum hrtimer_restart ret;

	ret = vcam_event_timer(vcam);
	if (ret == HRTIMER_NORESTART)
		return HRTIMER_NORESTART;

	hrtimer_forward_now(hrtimer, ms_to_ktime(EVENT_TRIGGER_MS / 2));
	return HRTIMER_RESTART;
}

static enum hrtimer_restart vcam_event_hrtimer_slow(struct hrtimer *hrtimer)
{
	struct vcam_device *vcam = container_of(hrtimer,
						struct vcam_device,
						event_timer_slow);
	enum hrtimer_restart ret;

	ret = vcam_event_timer(vcam);
	if (ret == HRTIMER_NORESTART)
		return HRTIMER_NORESTART;

	hrtimer_forward_now(hrtimer, ms_to_ktime(EVENT_TRIGGER_MS));
	return HRTIMER_RESTART;
}

static void cam_objects_release(struct vcam_device *vcam)
{
	int obj;

	trigger_event_on(vcam, ENTITY_FAST_IRQ, ENTITY_FAST_IRQ_EVENT);
	trigger_event_on(vcam, ENTITY_SLOW_IRQ, ENTITY_SLOW_IRQ_EVENT);

	hrtimer_cancel(&vcam->event_timer_fast);
	hrtimer_cancel(&vcam->event_timer_slow);

	for (obj = 0; obj < ARRAY_SIZE(vcam->events); obj++) {
		if (vcam->events[obj])
			cam_event_unregister(vcam->events[obj]);
	}

	for (obj = 0; obj < ARRAY_SIZE(vcam->entities); obj++) {
		if (vcam->entities[obj])
			cam_entity_unregister(vcam->entities[obj]);
	}

	if (vcam->root_entity)
		cam_entity_unregister(vcam->root_entity);

	dev_info(vcam->dev, "%s: done\n", __func__);
}

static int vcam_probe(struct platform_device *pdev)
{
	struct vcam_device *vcam;
	int parent_obj, obj, idx;
	u32 parent_id;
	int ret;

	vcam = kzalloc(sizeof(*vcam), GFP_KERNEL);
	if (!vcam)
		return -ENOMEM;

	spin_lock_init(&vcam->instances_lock);
	INIT_LIST_HEAD(&vcam->instances);
	vcam->dev = &pdev->dev;
	platform_set_drvdata(pdev, vcam);
	hrtimer_init(&vcam->event_timer_fast, CLOCK_MONOTONIC,
		     HRTIMER_MODE_REL);
	vcam->event_timer_fast.function = vcam_event_hrtimer_fast;
	hrtimer_init(&vcam->event_timer_slow, CLOCK_MONOTONIC,
		     HRTIMER_MODE_REL);
	vcam->event_timer_slow.function = vcam_event_hrtimer_slow;

	vcam->cam = cam_device_get();
	if (!vcam->cam) {
		dev_err(vcam->dev, "%s: cam device initialization failed\n",
			__func__);
		kfree(vcam);
		return -ENOMEM;
	}

	cam_ns_enumeration_forbid(vcam->cam);
	idx = 0;
	vcam->root_entity = cam_entity_register(vcam->cam,
						CAM_OBJ_ID_ROOT,
						vcam,
						&entity_ops,
						CAM_ENTITY_MIN_INSTANCES,
						entity_names[idx]);
	if (!vcam->root_entity) {
		ret = -ENOMEM;
		goto error;
	}

	idx = 1;
	for (obj = 0; obj < ARRAY_SIZE(vcam->entities); obj++) {
		struct cam_obj_entity *entity;

		parent_id = cam_entity_id(vcam->root_entity);
		entity = cam_entity_register(vcam->cam,
					     parent_id,
					     vcam,
					     &entity_ops,
					     VCAM_NUM_INSTANCES,
					     entity_names[idx]);
		vcam->entities[obj] = entity;
		if (!vcam->entities[obj]) {
			dev_err(vcam->dev, "%s: failed to register entity\n",
				__func__);
			ret = -ENOMEM;
			goto error;
		}
		idx++;
	}

	parent_obj = 0;
	obj = 0;
	idx = 1;

	while (obj < ARRAY_SIZE(vcam->events)) {
		parent_id = cam_entity_id(vcam->entities[parent_obj]);

		vcam->events[obj] = cam_event_register(vcam->cam,
						       parent_id,
						       "%s-event",
						       entity_names[idx]);
		if (!vcam->events[obj]) {
			dev_err(vcam->dev, "%s: failed to register event\n",
				__func__);
			ret = -ENOMEM;
			goto error;
		}

		idx++;
		obj++;
		parent_obj++;
	}

	cam_ns_enumeration_permit(vcam->cam);
	vcam->timer_start_ts = jiffies;
	hrtimer_start(&vcam->event_timer_fast, 0, HRTIMER_MODE_REL);
	hrtimer_start(&vcam->event_timer_slow, 0, HRTIMER_MODE_REL);

	dev_info(vcam->dev, "%s: done\n", __func__);
	return 0;

error:
	cam_objects_release(vcam);
	cam_ns_enumeration_permit(vcam->cam);
	cam_device_put(vcam->cam);
	kfree(vcam);
	return ret;
}

static int vcam_remove(struct platform_device *pdev)
{
	struct vcam_device *vcam = platform_get_drvdata(pdev);

	cam_objects_release(vcam);
	cam_device_put(vcam->cam);

	platform_set_drvdata(pdev, NULL);
	kfree(vcam);
	return 0;
}

static struct platform_driver vcam_driver = {
	.probe = vcam_probe,
	.remove = vcam_remove,
	.driver = {
		.name = VCAM_DEVICE_NAME,
	},
};

static void vcam_device_release(struct device *dev)
{
}

static struct platform_device vcam_device = {
	.name = VCAM_DEVICE_NAME,
	.dev = {
		.release = vcam_device_release,
	},
};

static int __init vcam_init(void)
{
	int ret;

	ret = platform_driver_register(&vcam_driver);
	if (ret < 0) {
		pr_err("%s: platform driver registration failed: %d\n", __func__,
		       ret);
		return ret;
	}

	ret = platform_device_register(&vcam_device);
	if (ret < 0) {
		pr_err("%s: platform device registration failed: %d\n", __func__,
		       ret);
		return ret;
	}

	return 0;
}

static void __exit vcam_exit(void)
{
	platform_device_unregister(&vcam_device);
	platform_driver_unregister(&vcam_driver);
}

module_init(vcam_init);
module_exit(vcam_exit);

MODULE_DESCRIPTION("CAM test device driver");
MODULE_IMPORT_NS(DMA_BUF);
MODULE_LICENSE("GPL v2");
