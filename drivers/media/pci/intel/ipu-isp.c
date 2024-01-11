// SPDX-License-Identifier: GPL-2.0
/*
 * IPU PSYS KCAM
 *
 * Copyright (C) 2022 Google LLC
 */

#include <linux/isp/isp-device.h>
#include <linux/isp/isp-entity.h>
#include <linux/isp/isp-buffer.h>
#include <linux/dma-buf.h>
#include <linux/rhashtable.h>
#include <linux/anon_inodes.h>

#include <uapi/linux/isp.h>
#include <uapi/linux/ipu-psys.h>

#include "ipu.h"
#include "ipu-bus.h"
#include "ipu-psys.h"
#include "ipu-cpd.h"

static struct ipu_isp_psys_instance *
isp_psys_instance_open(struct ipu_psys *psys)
{
	struct ipu_isp_psys_instance *instance;
	int rval;

	instance = kzalloc(sizeof(*instance), GFP_KERNEL);
	if (!instance)
		return ERR_PTR(-ENOMEM);

	instance->psys = psys;

	mutex_init(&instance->mutex);

	rval = ipu_psys_instance_init(instance);
	if (rval)
		goto instance_init_failed;

	mutex_lock(&psys->mutex);
	list_add_tail(&instance->list, &psys->instances);
	mutex_unlock(&psys->mutex);

	return instance;

instance_init_failed:
	mutex_destroy(&instance->mutex);
	kfree(instance);

	return ERR_PTR(rval);
}

static void isp_psys_instance_release(struct ipu_psys *psys,
				      struct ipu_isp_psys_instance *instance)
{
	mutex_lock(&psys->mutex);
	list_del(&instance->list);
	mutex_unlock(&psys->mutex);

	ipu_psys_instance_deinit(instance);

	mutex_lock(&psys->mutex);
	if (list_empty(&psys->instances))
		psys->power_gating = 0;
	mutex_unlock(&psys->mutex);
	mutex_destroy(&instance->mutex);
	kfree(instance);
}

static void *ipu_isp_instance_create(void *dev)
{
	struct ipu_bus_device *adev = dev;
	struct ipu_device *isp = adev->isp;
	struct ipu_psys *psys = ipu_bus_get_drvdata(adev);
	struct ipu_isp_psys_instance *instance;

	if (isp->flr_done)
		return NULL;

	instance = isp_psys_instance_open(psys);
	if (IS_ERR(instance)) {
		dev_err(&adev->auxdev.dev, "isp_psys_instance_open failed\n");
		return NULL;
	}

	return instance;
}

static void ipu_isp_instance_destroy(void *dev, void *instance_data)
{
	struct ipu_bus_device *adev = dev;
	struct ipu_psys *psys = ipu_bus_get_drvdata(adev);
	struct ipu_isp_psys_instance *instance = instance_data;

	isp_psys_instance_release(psys, instance);
}

static void ipu_isp_buffer_register(struct ipu_psys *psys,
				    struct ipu_isp_psys_dbuf *buffer)
{
	mutex_lock(&psys->mutex);
	list_add(&buffer->bufmap_list, &psys->bufmap);
	mutex_unlock(&psys->mutex);
}

static void ipu_isp_buffer_unregister(struct ipu_psys *psys,
				      struct ipu_isp_psys_dbuf *buffer)
{
	mutex_lock(&psys->mutex);
	list_del(&buffer->bufmap_list);
	mutex_unlock(&psys->mutex);
}

static void buffer_unmap(struct ipu_isp_psys_dbuf *buffer);
static int buffer_map(struct device *dev, struct ipu_isp_psys_dbuf *buffer)
{
	struct iosys_map dmap;
	int err;

	buffer->dma_attach = dma_buf_attach(buffer->dma_buf, dev);
	if (IS_ERR(buffer->dma_attach)) {
		dev_err(dev, "dma_buf_attach fail\n");
		goto unmap_buffer;
	}

	buffer->dma_sgt = dma_buf_map_attachment_unlocked(buffer->dma_attach,
							  DMA_BIDIRECTIONAL);
	if (IS_ERR(buffer->dma_sgt)) {
		dev_err(dev, "dma_buf_map_attachment fail\n");
		goto unmap_buffer;
	}

	buffer->dma_addr = sg_dma_address(buffer->dma_sgt->sgl);
	err = dma_buf_vmap_unlocked(buffer->dma_buf, &dmap);

	if (IS_ERR(err)) {
		dev_err(dev, "dma_buf_vmap fail\n");
		goto unmap_buffer;
	}
	buffer->va = dmap.vaddr;

	return 0;

unmap_buffer:
	buffer_unmap(buffer);

	return -1;
}

static void buffer_unmap(struct ipu_isp_psys_dbuf *buffer)
{
	if (!IS_ERR_OR_NULL(buffer->va)) {
		struct iosys_map dmap;

		iosys_map_set_vaddr(&dmap, buffer->va);
		dma_buf_vunmap_unlocked(buffer->dma_buf, &dmap);
	}

	if (!IS_ERR_OR_NULL(buffer->dma_sgt))
		dma_buf_unmap_attachment_unlocked(buffer->dma_attach,
						  buffer->dma_sgt,
						  DMA_BIDIRECTIONAL);

	if (!IS_ERR_OR_NULL(buffer->dma_attach))
		dma_buf_detach(buffer->dma_buf,
			       buffer->dma_attach);
}

static struct ipu_isp_psys_dbuf *
ipu_isp_buffer_lookup(struct ipu_psys *psys, struct dma_buf *dbuf)
{
	struct ipu_isp_psys_dbuf *buf = NULL;
	struct ipu_isp_psys_dbuf *b;

	mutex_lock(&psys->mutex);
	list_for_each_entry(b, &psys->bufmap, bufmap_list) {
		if (b->dma_buf == dbuf) {
			if (kref_get_unless_zero(&b->kref))
				buf = b;
			break;
		}
	}
	mutex_unlock(&psys->mutex);

	return buf;
}

static void buffer_release(struct kref *ref)
{
	struct ipu_isp_psys_dbuf *buffer;

	buffer = container_of(ref, struct ipu_isp_psys_dbuf, kref);

	ipu_isp_buffer_unregister(buffer->psys, buffer);
	buffer_unmap(buffer);

	kfree(buffer);
}

static void *ipu_isp_dmabuf_add(void *dev, struct dma_buf *dma_buf)
{
	struct ipu_bus_device *adev = dev;
	struct ipu_psys *psys = ipu_bus_get_drvdata(adev);
	struct ipu_isp_psys_dbuf *buffer;

	buffer = ipu_isp_buffer_lookup(psys, dma_buf);

	if (!buffer) {
		buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
		if (!buffer)
			goto put_dma_buf;

		buffer->dma_buf = dma_buf;
		if (buffer_map(&adev->auxdev.dev, buffer) < 0)
			goto put_dma_buf;

		kref_init(&buffer->kref);
		buffer->psys = psys;
		ipu_isp_buffer_register(psys, buffer);
	}
	return buffer;

put_dma_buf:
	kfree(buffer);

	return NULL;
}

static void ipu_isp_dmabuf_remove(void *dev,
				  void *data,
				  struct dma_buf *dma_buf)
{
	struct ipu_isp_psys_dbuf *buffer = data;

	kref_put(&buffer->kref, buffer_release);
}

static int ipu_isp_instance_read(void *dev,
				 struct isp_obj_instance *instance,
				 struct isp_read_instruction *inst)
{
	struct ipu_bus_device *adev = dev;
	struct ipu_psys *psys = ipu_bus_get_drvdata(adev);
	union {
		struct ipu_psys_capability caps;
		struct ipu_psys_manifest m;
		struct ipu_psys_event ev;
	} karg;
	struct ipu_isp_psys_instance *psys_instance;
	void __user *up = (void __user *)inst->ptr;
	int err = 0;

	if (inst->size > sizeof(karg))
		return -ENOTTY;

	memset(&karg, 0, sizeof(karg));
	psys_instance = isp_instance_driver_data(instance);

	switch (inst->reg) {
	case IPU_REG_QUERYCAP:
		karg.caps = psys_instance->psys->caps;
		break;
	case IPU_REG_GET_MANIFEST:
		err = copy_from_user(&karg.m, up, sizeof(karg.m));
		if (err) {
			err = -EFAULT;
			break;
		}
		err = ipu_get_manifest(&karg.m, psys);
		break;
	case IPU_REG_DQEVENT:
		err = ipu_ioctl_dqevent(&karg.ev, psys_instance);
		break;
	default:
		dev_err(&adev->auxdev.dev, "unsupported reg for read %x\n",
			inst->reg);
		err = -ENOTTY;
	}

	if (err)
		return err;

	if (copy_to_user(up, &karg, inst->size))
		return -EFAULT;

	return ISP_INSTRUCTION_EXEC_HANDLED;
}

static int ipu_isp_instance_write(void *dev,
				  struct isp_obj_instance *instance,
				  struct isp_write_instruction *inst)
{
	struct ipu_bus_device *adev = dev;
	struct ipu_psys_command cmd;
	void __user *up = (void __user *)inst->ptr;
	int err = 0;

	if (inst->size != sizeof(cmd)) {
		dev_err(&adev->auxdev.dev, "instance_write: wrong argument size");
		return -ENOTTY;
	}

	err = copy_from_user(&cmd, up, inst->size);
	if (err) {
		dev_err(&adev->auxdev.dev, "instance_write: copy from user error\n");
		return -EFAULT;
	}

	switch (inst->reg) {
	case IPU_REG_QCMD:
		err = ipu_psys_kcmd_new(&cmd,
					adev,
					instance,
					(struct isp_obj_buffer **)inst->buffers_list);
		break;
	default:
		dev_err(&adev->auxdev.dev, "unsupported reg for write %x\n",
			inst->reg);
		err = -ENOTTY;
	}

	if (err)
		return err;

	return ISP_INSTRUCTION_EXEC_DEFERRED;
}

static struct isp_entity_ops isp_entity_ops = {
	.instance_read		= ipu_isp_instance_read,
	.instance_write		= ipu_isp_instance_write,
	.instance_create	= ipu_isp_instance_create,
	.instance_destroy	= ipu_isp_instance_destroy,
	.dmabuf_add		= ipu_isp_dmabuf_add,
	.dmabuf_remove		= ipu_isp_dmabuf_remove,
};

int ipu_isp_init(struct ipu_bus_device *adev, unsigned int id)
{
	struct isp_device *isp;
	struct isp_obj_entity *isp_entity;
	struct isp_obj_event *isp_event;
	struct ipu_psys *psys;
	int ret;

	isp = isp_device_get();
	if (!isp) {
		dev_err(&adev->auxdev.dev,
			"%s: isp device initialization failed\n",
			__func__);
		return -ENOMEM;
	}

	isp_ns_enumeration_forbid(isp);
	isp_entity = isp_entity_register(isp,
					 ISP_ENTITY_ID_ROOT,
					 adev,
					 &isp_entity_ops,
					 U32_MAX >> 1,
					 "PSYS%d",
					 id);
	if (!isp_entity) {
		ret = -ENOMEM;
		goto out_isp_put;
	}

	isp_event = isp_event_register(isp,
				       isp_entity_id(isp_entity),
				       "PSYS%d-internal",
				       id);

	if (!isp_event) {
		ret = -ENOMEM;
		goto out_isp_unregister_entity;
	}
	isp_ns_enumeration_permit(isp);

	psys = ipu_bus_get_drvdata(adev);
	psys->isp = isp;
	psys->isp_entity = isp_entity;
	psys->isp_event = isp_event;
	INIT_LIST_HEAD(&psys->bufmap);

	isp_device_put(isp);

	return 0;

out_isp_unregister_entity:
	isp_entity_unregister(isp_entity);

out_isp_put:
	isp_ns_enumeration_permit(isp);
	isp_device_put(isp);
	return ret;
}

void ipu_isp_exit(struct ipu_bus_device *adev)
{
	struct ipu_psys *psys;

	psys = ipu_bus_get_drvdata(adev);
	isp_event_unregister(psys->isp_event);
	isp_entity_unregister(psys->isp_entity);
	isp_device_put(psys->isp);
}
