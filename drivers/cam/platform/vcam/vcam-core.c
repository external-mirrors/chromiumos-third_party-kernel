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
#include <linux/hrtimer.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <uapi/linux/vcam.h>

#define VCAM_DEVICE_NAME		"vcam"

#define EVENT_TRIGGER_MS		1010

#define ENTITY_1			0
#define ENTITY_1_EVENT_1		0
#define ENTITY_1_EVENT_2		1

#define ENTITY_2			1
#define ENTITY_2_EVENT_1		2
#define ENTITY_2_EVENT_2		3

static const char *entity_names[] = {
	VCAM_ROOT_ENTITY_NAME,
	VCAM_DMA_IMPORT_ENTITY_NAME,
	VCAM_FAST_IRQ_ENTITY_NAME,
	VCAM_SLOW_IRQ_ENTITY_NAME,
};

struct vcam_device {
	struct device			*dev;
	struct cam_device		*cam;

	struct cam_obj_entity		*root_entity;
	struct cam_obj_entity		*dma_import_entity;
	struct cam_obj_entity		*entities[2];
	struct cam_obj_event		*events[4];

	struct cam_obj_buffer		*buffer;

	struct hrtimer			event_timer_fast;
	struct hrtimer			event_timer_slow;
	unsigned long			timer_start_ts;
};

static int entity_read(struct cam_obj_entity *entity,
		       struct cam_read_instruction *rw)
{
	char dummy_buffer[32] = {};
	u64 len;

	pr_info("VCAM: execute entity register %u read\n", rw->reg);

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

static int entity_write(struct cam_obj_entity *entity,
			struct cam_write_instruction *rw)
{
	char dummy_buffer[32] = {};

	pr_info("VCAM: execute entity register %u write\n", rw->reg);

	if (rw->size > sizeof(dummy_buffer)) {
		pr_err("VCAM: write size is too large");
		return -EINVAL;
	}

	if (copy_from_user(dummy_buffer, u64_to_user_ptr(rw->ptr), rw->size)) {
		pr_err("VCAM: cannot copy from user\n");
		return -EINVAL;
	}

	pr_info("VCAM: register write payload: %s\n", dummy_buffer);
	return 0;
}

static struct cam_entity_ops entity_ops = {
	.read		= entity_read,
	.write		= entity_write,
};

static int dma_importer_read(struct cam_obj_entity *entity,
			     struct cam_read_instruction *rw)
{
	pr_info("VCAM: DMA importer read\n");
	return 0;
}

static int vcam_buffer_add(struct vcam_device *vcam,
			   struct cam_obj_entity *entity,
			   int fd)
{
	if (WARN_ON(vcam->buffer))
		return -EINVAL;

	vcam->buffer = cam_buffer_register(vcam->cam,
					   cam_entity_id(entity),
					   vcam->dev,
					   fd);
	if (!vcam->buffer)
		return -EINVAL;

	return 0;
}

static int vcam_buffer_remove(struct vcam_device *vcam, int fd)
{
	if (!vcam->buffer)
		return -EINVAL;
	cam_buffer_unregister(vcam->buffer);
	vcam->buffer = NULL;
	return 0;
}

static int dma_importer_write(struct cam_obj_entity *entity,
			      struct cam_write_instruction *rw)
{
	struct vcam_dmabuf_instruction insn;
	struct vcam_device *vcam;
	int ret;

	pr_info("VCAM: DMA importer write\n");

	if (rw->size != sizeof(struct vcam_dmabuf_instruction)) {
		pr_err("Invalid size of DMABUF insn\n");
		return -EINVAL;
	}

	if (copy_from_user(&insn, u64_to_user_ptr(rw->ptr), rw->size)) {
		pr_err("Cannot copy DMABUF insn from user\n");
		return -EINVAL;
	}

	vcam = cam_entity_driver_data(entity);
	if (WARN_ON(!vcam))
		return -EINVAL;

	switch (insn.type) {
	case VCAM_DMABUF_ADD:
		ret = vcam_buffer_add(vcam, entity, insn.fd);
		break;
	case VCAM_DMABUF_REMOVE:
		ret = vcam_buffer_remove(vcam, insn.fd);
		break;
	default:
		ret = -EINVAL;
		pr_err("Unknown type of dmabuf insn: %d\n", insn.type);
		break;
	}

	return ret;
}

static struct cam_entity_ops dma_importer_entity_ops = {
	.read		= dma_importer_read,
	.write		= dma_importer_write,
};

static void trigger_event_on(struct vcam_device *vcam,
			     u32 entity_id,
			     u32 event_id)
{
	if (!vcam->entities[entity_id] || !vcam->events[event_id])
		return;

	cam_event_trigger_signals(vcam->entities[entity_id],
				  vcam->events[event_id]);
}

static enum hrtimer_restart vcam_event_timer(struct vcam_device *vcam)
{
	/*
	 * Trigger only ENTITY_1 events. We use ENTITY_2 to block OPs
	 * on and to test query/add/remove ioctl().
	 */
	trigger_event_on(vcam, ENTITY_1, ENTITY_1_EVENT_1);
	trigger_event_on(vcam, ENTITY_1, ENTITY_1_EVENT_2);

	/*
	 * Flush all events every 5 seconds.
	 * 5s ought to be enough for anybody.
	 */
	if (!time_after(jiffies, vcam->timer_start_ts + 5 * HZ))
		return HRTIMER_RESTART;

	dev_info(vcam->dev, "events: trigger slow events\n");
	/* We cancel HR timer, send spurious wakeup to all entities */
	trigger_event_on(vcam, ENTITY_2, ENTITY_2_EVENT_1);
	trigger_event_on(vcam, ENTITY_2, ENTITY_2_EVENT_2);
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

	trigger_event_on(vcam, ENTITY_1, ENTITY_1_EVENT_1);
	trigger_event_on(vcam, ENTITY_1, ENTITY_1_EVENT_2);
	trigger_event_on(vcam, ENTITY_2, ENTITY_2_EVENT_1);
	trigger_event_on(vcam, ENTITY_2, ENTITY_2_EVENT_2);

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

	if (vcam->buffer) {
		pr_err("User-space did not destroy imported DMA buffer\n");
		cam_buffer_unregister(vcam->buffer);
		vcam->buffer = NULL;
	}

	if (vcam->dma_import_entity)
		cam_entity_unregister(vcam->dma_import_entity);

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
		return ret;
	}

	idx = 0;
	vcam->root_entity = cam_entity_register(vcam->cam,
						CAM_OBJ_ID_ROOT,
						vcam,
						&entity_ops,
						entity_names[idx]);
	if (!vcam->root_entity) {
		ret = -ENOMEM;
		goto error;
	}

	idx = 1;
	parent_id = cam_entity_id(vcam->root_entity);
	vcam->dma_import_entity = cam_entity_register(vcam->cam,
						      parent_id,
						      vcam,
						      &dma_importer_entity_ops,
						      entity_names[idx]);
	if (!vcam->dma_import_entity) {
		ret = -ENOMEM;
		goto error;
	}

	idx = 2;
	for (obj = 0; obj < ARRAY_SIZE(vcam->entities); obj++) {
		parent_id = cam_entity_id(vcam->root_entity);

		vcam->entities[obj] = cam_entity_register(vcam->cam,
							  parent_id,
							  vcam,
							  &entity_ops,
							  entity_names[idx]);
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
	idx = 2;

	while (obj < ARRAY_SIZE(vcam->events)) {
		parent_id = cam_entity_id(vcam->entities[parent_obj]);

		vcam->events[obj] = cam_event_register(vcam->cam,
						       parent_id,
						       "%s.begin",
						       entity_names[idx]);
		if (!vcam->events[obj]) {
			dev_err(vcam->dev, "%s: failed to register event\n",
				__func__);
			ret = -ENOMEM;
			goto error;
		}

		obj++;

		vcam->events[obj] = cam_event_register(vcam->cam,
						       parent_id,
						       "%s.end",
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

	vcam->timer_start_ts = jiffies;
	hrtimer_start(&vcam->event_timer_fast, 0, HRTIMER_MODE_REL);
	hrtimer_start(&vcam->event_timer_slow, 0, HRTIMER_MODE_REL);

	dev_info(vcam->dev, "%s: done\n", __func__);
	return 0;

error:
	cam_objects_release(vcam);
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
MODULE_LICENSE("GPL v2");
