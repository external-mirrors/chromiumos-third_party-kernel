// SPDX-License-Identifier: GPL-2.0
/*
 * ISP test device driver
 *
 * Copyright (C) 2022 Google LLC
 */

#include <linux/isp/isp-buffer.h>
#include <linux/isp/isp-device.h>
#include <linux/isp/isp-entity.h>
#include <linux/delay.h>
#include <linux/dma-buf.h>
#include <linux/dma-resv.h>
#include <linux/hrtimer.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <uapi/linux/visp.h>

/*
 * This is a testing driver for ISP subsystem. We use it in conduction
 * with visptest tool, so the driver doesn't do much beside the basic ISP
 * functionality testing (entities registration, events registration,
 * instances handling, etc.).
 */

#define VISP_DEVICE_NAME		"visp"

#define EVENT_TRIGGER_MS		1010

#define ROOT_ENTITY		0

/* Simulate frequent device IRQ for fast events */
#define FAST_ENTITY		1
#define FAST_ENTITY_EVENT	1

/* Simulate slower device IRQ for slow events */
#define SLOW_ENTITY		2
#define SLOW_ENTITY_EVENT	2

/* Very fast device IRQ for benchmarking mode */
#define BM_ENTITY		3
#define BM_ENTITY_EVENT		3

#define BM_IRQ_INTERVAL		200000

static const char *entity_names[] = {
	VISP_ROOT_ENTITY_NAME,
	VISP_FAST_IRQ_ENTITY_NAME,
	VISP_SLOW_IRQ_ENTITY_NAME,
	VISP_BM_IRQ_ENTITY_NAME,
};

struct visp_device {
	struct device			*dev;
	struct isp_device		*isp;

	struct isp_obj_entity		*entities[4];
	struct isp_obj_event		*events[4];

	spinlock_t			instances_lock;
	struct list_head		instances[4];

	struct hrtimer			event_timer_fast;
	struct hrtimer			event_timer_slow;
	unsigned long			timer_start_ts;

	/* Used only in visptest benchmark mode */
	atomic_t			bm_ops;
	atomic_t			bm_init_done;
	struct hrtimer			event_timer_bm;
};

struct visp_exec_instance {
	struct isp_obj_instance		*instance;
	struct list_head		entry;
};

#define VISP_NUM_INSTANCES		8
#define VISP_INSTANCE_DATA_BUF_SZ	64

struct visp_entity_instance_data {
	char				*buf;
};

struct visp_buffer {
	u64				phys;
	struct dma_buf_attachment	*dma_attach;
	struct sg_table			*dma_sgt;
};

static void trigger_event_on(struct visp_device *visp,
			     u32 entity_id,
			     u32 event_id)
{
	struct visp_exec_instance *inst;
	unsigned long flags;

	if (!visp->entities[entity_id] || !visp->events[event_id])
		return;

	isp_event_trigger_signals(visp->entities[entity_id],
				  visp->events[event_id]);

	spin_lock_irqsave(&visp->instances_lock, flags);
	/* We trigger all in-flight instance operations for entity at once */
	while (!list_empty(&visp->instances[entity_id])) {
		inst = list_first_entry(&visp->instances[entity_id],
					struct visp_exec_instance,
					entry);
		list_del(&inst->entry);

		isp_instance_event_trigger_signals(visp->entities[entity_id],
						   inst->instance,
						   visp->events[event_id],
						   0);
		isp_instance_put(inst->instance);
		kfree(inst);
	}
	spin_unlock_irqrestore(&visp->instances_lock, flags);
}

static enum hrtimer_restart visp_event_timer(struct visp_device *visp)
{
	/*
	 * Trigger only FAST_ENTITY events. We use other entities
	 * block OPs on and to test query/add/remove ioctl().
	 */
	trigger_event_on(visp, FAST_ENTITY, FAST_ENTITY_EVENT);

	/* Flush all events every 5 seconds */
	if (!time_after(jiffies, visp->timer_start_ts + 5 * HZ))
		return HRTIMER_RESTART;

	trigger_event_on(visp, SLOW_ENTITY, SLOW_ENTITY_EVENT);
	visp->timer_start_ts = jiffies;

	return HRTIMER_RESTART;
}

static enum hrtimer_restart visp_event_hrtimer_fast(struct hrtimer *hrtimer)
{
	struct visp_device *visp = container_of(hrtimer,
						struct visp_device,
						event_timer_fast);
	enum hrtimer_restart ret;

	ret = visp_event_timer(visp);
	if (ret == HRTIMER_NORESTART)
		return HRTIMER_NORESTART;

	hrtimer_forward_now(hrtimer, ms_to_ktime(EVENT_TRIGGER_MS / 2));
	return HRTIMER_RESTART;
}

static enum hrtimer_restart visp_event_hrtimer_slow(struct hrtimer *hrtimer)
{
	struct visp_device *visp = container_of(hrtimer,
						struct visp_device,
						event_timer_slow);
	enum hrtimer_restart ret;

	ret = visp_event_timer(visp);
	if (ret == HRTIMER_NORESTART)
		return HRTIMER_NORESTART;

	hrtimer_forward_now(hrtimer, ms_to_ktime(EVENT_TRIGGER_MS));
	return HRTIMER_RESTART;
}

static enum hrtimer_restart visp_event_hrtimer_bm(struct hrtimer *hrtimer)
{
	struct visp_device *visp;
	static ktime_t interval;

	visp = container_of(hrtimer, struct visp_device, event_timer_bm);

	trigger_event_on(visp, BM_ENTITY, BM_ENTITY_EVENT);

	if (atomic_read(&visp->bm_ops) >= BM_NUM_OPS) {
		/*
		 * Reset device's state so that it can be used in both
		 * benchmark and non-benchmark modes.
		 */
		atomic_set(&visp->bm_init_done, 0);
		atomic_set(&visp->bm_ops, 0);

		visp->timer_start_ts = jiffies;
		hrtimer_start(&visp->event_timer_fast, 0, HRTIMER_MODE_REL);
		hrtimer_start(&visp->event_timer_slow, 0, HRTIMER_MODE_REL);

		return HRTIMER_NORESTART;
	}

	interval = ktime_set(0, BM_IRQ_INTERVAL);
	hrtimer_forward_now(hrtimer, interval);
	return HRTIMER_RESTART;
}

static void visp_configure_bm_hrtimer(struct visp_device *visp)
{
	static ktime_t interval;

	if (atomic_xchg(&visp->bm_init_done, 1))
		return;

	/* In benchmark mode we only want benchmark hrtimer */
	trigger_event_on(visp, FAST_ENTITY, FAST_ENTITY_EVENT);
	trigger_event_on(visp, SLOW_ENTITY, SLOW_ENTITY_EVENT);
	hrtimer_cancel(&visp->event_timer_fast);
	hrtimer_cancel(&visp->event_timer_slow);

	interval = ktime_set(0, BM_IRQ_INTERVAL);
	hrtimer_start(&visp->event_timer_bm, interval, HRTIMER_MODE_REL);
}

static void *entity_instance_create(void *dev)
{
	struct visp_entity_instance_data *data;

	pr_devel("VISP: instance create\n");

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return NULL;

	data->buf = kzalloc(VISP_INSTANCE_DATA_BUF_SZ, GFP_KERNEL);
	if (!data->buf) {
		kfree(data);
		data = NULL;
	}
	return data;
}

static void entity_instance_destroy(void *dev, void *instance_data)
{
	struct visp_entity_instance_data *data = instance_data;

	pr_devel("VISP: instance destroy\n");
	kfree(data->buf);
	kfree(data);
}

/*
 * This records an in-flight instance operation for a given entity, which is
 * finished when a corresponding event fires.
 */
static int record_event_instance(struct visp_device *visp,
				 u32 event_id,
				 struct isp_obj_instance *instance)
{
	struct visp_exec_instance *inst;
	unsigned long flags;

	inst = kzalloc(sizeof(*inst), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	if (!isp_instance_get(instance)) {
		kfree(inst);
		return -EINVAL;
	}

	INIT_LIST_HEAD(&inst->entry);
	inst->instance = instance;

	spin_lock_irqsave(&visp->instances_lock, flags);
	list_add(&inst->entry, &visp->instances[event_id]);
	spin_unlock_irqrestore(&visp->instances_lock, flags);
	return ISP_INSTRUCTION_EXEC_DEFERRED;
}

static int entity_instance_write(void *dev, u32 entity_id,
				 struct isp_obj_instance *instance,
				 struct isp_write_instruction *rw)
{
	struct visp_device *visp = dev;
	char dummy_buffer[32] = {};

	pr_devel("VISP: execute entity register %u write\n", rw->reg);

	if (rw->size > sizeof(dummy_buffer)) {
		pr_err("VISP: write size is too large");
		return -EINVAL;
	}

	if (copy_from_user(dummy_buffer, u64_to_user_ptr(rw->ptr), rw->size)) {
		pr_err("VISP: cannot copy from user\n");
		return -EINVAL;
	}

	pr_devel("VISP: register write payload: %s\n", dummy_buffer);
	return record_event_instance(visp, entity_id, instance);
}

static int entity_instance_read(void *dev,
				struct isp_obj_instance *instance,
				struct isp_read_instruction *rw)
{
	char dummy_buffer[32] = {};
	u64 len;

	pr_devel("VISP: execute entity register %u read\n", rw->reg);

	if (!rw->size)
		return ISP_INSTRUCTION_EXEC_HANDLED;

	strcpy(dummy_buffer, "From VISP driver");
	len = strlen(dummy_buffer);
	if (rw->size < len)
		len = rw->size;
	if (copy_to_user(u64_to_user_ptr(rw->ptr), dummy_buffer, len)) {
		pr_err("VISP: cannot copy to user\n");
		return -EINVAL;
	}

	return ISP_INSTRUCTION_EXEC_HANDLED;
}

static int fast_entity_instance_write(void *dev,
				      struct isp_obj_instance *instance,
				      struct isp_write_instruction *rw)
{
	return entity_instance_write(dev, FAST_ENTITY, instance, rw);
}

static int slow_entity_instance_write(void *dev,
				      struct isp_obj_instance *instance,
				      struct isp_write_instruction *rw)
{
	return entity_instance_write(dev, SLOW_ENTITY, instance, rw);
}

static int bm_entity_instance_read(void *dev,
				   struct isp_obj_instance *instance,
				   struct isp_read_instruction *rw)
{
	struct visp_device *visp = dev;

	atomic_inc(&visp->bm_ops);
	return ISP_INSTRUCTION_EXEC_HANDLED;
}

static int bm_entity_instance_write(void *dev,
				    struct isp_obj_instance *instance,
				    struct isp_write_instruction *rw)
{
	struct visp_device *visp = dev;
	int ret;

	pr_devel("VISP: execute entity bm register %u write\n", rw->reg);

	/* Only once */
	visp_configure_bm_hrtimer(visp);

	ret = record_event_instance(visp, BM_ENTITY, instance);
	if (ret == ISP_INSTRUCTION_EXEC_DEFERRED)
		atomic_inc(&visp->bm_ops);

	return ret;
}

static void dmabuf_remove(void *dev, void *buf, struct dma_buf *dma_buf)
{
	struct visp_buffer *buffer;

	buffer = (struct visp_buffer *)buf;
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
	struct visp_buffer *buffer;
	struct visp_device *visp;

	visp = (struct visp_device *)dev;
	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer)
		return NULL;

	buffer->dma_attach = dma_buf_attach(dma_buf, visp->dev);
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

static const struct isp_entity_ops slow_entity_ops = {
	.instance_read		= entity_instance_read,
	.instance_write		= slow_entity_instance_write,
	.instance_create	= entity_instance_create,
	.instance_destroy	= entity_instance_destroy,
	.dmabuf_add		= dmabuf_add,
	.dmabuf_remove		= dmabuf_remove,
};

static const struct isp_entity_ops fast_entity_ops = {
	.instance_read		= entity_instance_read,
	.instance_write		= fast_entity_instance_write,
	.instance_create	= entity_instance_create,
	.instance_destroy	= entity_instance_destroy,
	.dmabuf_add		= dmabuf_add,
	.dmabuf_remove		= dmabuf_remove,
};

static const struct isp_entity_ops bm_entity_ops = {
	.instance_read		= bm_entity_instance_read,
	.instance_write		= bm_entity_instance_write,
	.instance_create	= entity_instance_create,
	.instance_destroy	= entity_instance_destroy,
	.dmabuf_add		= dmabuf_add,
	.dmabuf_remove		= dmabuf_remove,
};

static void isp_objects_release(struct visp_device *visp)
{
	int idx;

	trigger_event_on(visp, FAST_ENTITY, FAST_ENTITY);
	trigger_event_on(visp, SLOW_ENTITY, SLOW_ENTITY_EVENT);
	trigger_event_on(visp, BM_ENTITY, BM_ENTITY_EVENT);

	hrtimer_cancel(&visp->event_timer_fast);
	hrtimer_cancel(&visp->event_timer_slow);
	hrtimer_cancel(&visp->event_timer_bm);

	for (idx = ARRAY_SIZE(visp->events) - 1; idx >= 0; idx--) {
		if (visp->events[idx])
			isp_event_unregister(visp->events[idx]);
	}

	for (idx = ARRAY_SIZE(visp->entities) - 1; idx >= 0; idx--) {
		if (visp->entities[idx])
			isp_entity_unregister(visp->entities[idx]);
	}
}

static int visp_probe(struct platform_device *pdev)
{
	struct visp_device *visp;
	int idx, ret;

	visp = kzalloc(sizeof(*visp), GFP_KERNEL);
	if (!visp)
		return -ENOMEM;

	atomic_set(&visp->bm_init_done, 0);
	atomic_set(&visp->bm_ops, 0);
	spin_lock_init(&visp->instances_lock);
	visp->dev = &pdev->dev;
	platform_set_drvdata(pdev, visp);

	hrtimer_init(&visp->event_timer_fast, CLOCK_MONOTONIC,
		     HRTIMER_MODE_REL);
	visp->event_timer_fast.function = visp_event_hrtimer_fast;

	hrtimer_init(&visp->event_timer_slow, CLOCK_MONOTONIC,
		     HRTIMER_MODE_REL);
	visp->event_timer_slow.function = visp_event_hrtimer_slow;

	hrtimer_init(&visp->event_timer_bm, CLOCK_MONOTONIC,
		     HRTIMER_MODE_REL);
	visp->event_timer_bm.function = visp_event_hrtimer_bm;

	visp->isp = isp_device_get();
	if (!visp->isp) {
		dev_err(visp->dev, "isp device initialization failed\n");
		kfree(visp);
		return -ENOMEM;
	}

	isp_ns_enumeration_forbid(visp->isp);

	for (idx = 0; idx < ARRAY_SIZE(visp->entities); idx++) {
		static const struct isp_entity_ops *ops;
		struct isp_obj_entity *entity;
		u32 parent_id, num_instances;

		switch (idx) {
		case ROOT_ENTITY:
			parent_id = ISP_ENTITY_ID_ROOT;
			num_instances = ISP_ENTITY_MIN_INSTANCES;
			ops = &slow_entity_ops;
			break;
		case FAST_ENTITY:
			parent_id = isp_entity_id(visp->entities[ROOT_ENTITY]);
			num_instances = VISP_NUM_INSTANCES;
			ops = &fast_entity_ops;
			break;
		case SLOW_ENTITY:
			parent_id = isp_entity_id(visp->entities[ROOT_ENTITY]);
			num_instances = VISP_NUM_INSTANCES;
			ops = &slow_entity_ops;
			break;
		case BM_ENTITY:
			parent_id = isp_entity_id(visp->entities[ROOT_ENTITY]);
			num_instances = VISP_NUM_INSTANCES;
			ops = &bm_entity_ops;
			break;
		}

		INIT_LIST_HEAD(&visp->instances[idx]);
		entity = isp_entity_register(visp->isp,
					     parent_id,
					     visp,
					     ops,
					     num_instances,
					     entity_names[idx]);
		visp->entities[idx] = entity;
		if (!visp->entities[idx]) {
			dev_err(visp->dev, "failed to register entity: %s\n",
				entity_names[idx]);
			ret = -ENOMEM;
			goto error;
		}
	}

	for (idx = 1; idx < ARRAY_SIZE(visp->events); idx++) {
		u32 parent_id = isp_entity_id(visp->entities[idx]);

		visp->events[idx] = isp_event_register(visp->isp,
						       parent_id,
						       "%s-event",
						       entity_names[idx]);
		if (!visp->events[idx]) {
			dev_err(visp->dev, "failed to register %s-event\n",
				entity_names[idx]);
			ret = -ENOMEM;
			goto error;
		}
	}

	isp_ns_enumeration_permit(visp->isp);
	visp->timer_start_ts = jiffies;
	hrtimer_start(&visp->event_timer_fast, 0, HRTIMER_MODE_REL);
	hrtimer_start(&visp->event_timer_slow, 0, HRTIMER_MODE_REL);

	return 0;

error:
	isp_objects_release(visp);
	isp_ns_enumeration_permit(visp->isp);
	isp_device_put(visp->isp);
	kfree(visp);
	return ret;
}

static int visp_remove(struct platform_device *pdev)
{
	struct visp_device *visp = platform_get_drvdata(pdev);

	isp_objects_release(visp);
	isp_device_put(visp->isp);

	platform_set_drvdata(pdev, NULL);
	kfree(visp);
	return 0;
}

static struct platform_driver visp_driver = {
	.probe = visp_probe,
	.remove = visp_remove,
	.driver = {
		.name = VISP_DEVICE_NAME,
	},
};

static void visp_device_release(struct device *dev)
{
}

static struct platform_device visp_device = {
	.name = VISP_DEVICE_NAME,
	.dev = {
		.release = visp_device_release,
	},
};

static int __init visp_init(void)
{
	int ret;

	ret = platform_driver_register(&visp_driver);
	if (ret < 0) {
		pr_err("platform driver registration failed: %d\n", ret);
		return ret;
	}

	ret = platform_device_register(&visp_device);
	if (ret < 0) {
		pr_err("platform device registration failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static void __exit visp_exit(void)
{
	platform_device_unregister(&visp_device);
	platform_driver_unregister(&visp_driver);
}

module_init(visp_init);
module_exit(visp_exit);

MODULE_DESCRIPTION("ISP test device driver");
MODULE_IMPORT_NS(DMA_BUF);
MODULE_LICENSE("GPL v2");
