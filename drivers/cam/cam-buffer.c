// SPDX-License-Identifier: GPL-2.0
/*
 * CAM buffer
 *
 * Copyright (C) 2022 Google LLC
 */

#define pr_fmt(fmt) "cam-buffer: " fmt

#include <linux/cam/cam-buffer.h>
#include <linux/cam/cam-device.h>
#include <linux/cam/cam-entity.h>
#include <linux/cam/cam-namespace.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include <uapi/linux/cam.h>

/**
 * nsobj_to_cam_buffer() - Get CAM buffer pointer from the associated CAM
 * object
 * @nsobj: pointer to CAM object that represents a CAM buffer
 *
 * Return: NULL on error or CAM buffer pointer otherwise.
 */
static struct cam_obj_buffer *nsobj_to_cam_buffer(struct cam_obj *nsobj)
{
	if (!cam_obj_check_type(nsobj, CAM_OBJ_TYPE_BUFFER))
		return NULL;

	return container_of(nsobj, struct cam_obj_buffer, nsobj);
}

/**
 * cam_buffer_release() - The release function of CAM buffer
 * @nsobj: pointer to CAM object that represents a CAM buffer
 */
static void cam_buffer_release(struct cam_obj *nsobj)
{
	struct cam_obj_buffer *buffer = nsobj_to_cam_buffer(nsobj);

	if (!buffer)
		return;

	cam_obj_unlink(nsobj);

	if (!IS_ERR_OR_NULL(buffer->dma_sgt)) {
		dma_buf_unmap_attachment(buffer->dma_attach,
					 buffer->dma_sgt,
					 DMA_TO_DEVICE);
	}

	if (!IS_ERR_OR_NULL(buffer->dma_attach)) {
		dma_buf_detach(buffer->dma_buf,
			       buffer->dma_attach);
	}

	if (!IS_ERR_OR_NULL(buffer->dma_buf))
		dma_buf_put(buffer->dma_buf);

	kfree(buffer);
}

/**
 * cam_buffer_register() - Imports and registers (inserts into namespace) DMA
 * buffer
 * @ns: pointer to CAM namespace
 * @entity: parent entity to link to
 * @fd: file descriptor of the imported DMA buffer
 * @id: requested object ID
 *
 * Return: NULL on error or CAM buffer pointer otherwise.
 */
struct cam_obj_buffer *cam_buffer_register(struct cam_ns *ns,
					   struct cam_obj_entity *entity,
					   u32 fd,
					   u32 id)
{
	struct cam_obj_buffer *buffer;
	struct device *dev;

	dev = entity->ops->device(entity);
	if (!dev)
		return NULL;

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer)
		return NULL;

	cam_obj_init(&buffer->nsobj,
		     CAM_OBJ_TYPE_BUFFER,
		     cam_buffer_release,
		     ns);
	cam_obj_set_id(&buffer->nsobj, id);

	buffer->dma_buf = dma_buf_get(fd);
	if (IS_ERR(buffer->dma_buf))
		goto error;

	buffer->dma_attach = dma_buf_attach(buffer->dma_buf, dev);
	if (IS_ERR(buffer->dma_attach))
		goto error;

	buffer->dma_sgt = dma_buf_map_attachment(buffer->dma_attach,
						 DMA_TO_DEVICE);
	if (IS_ERR(buffer->dma_sgt))
		goto error;

	buffer->phys = sg_dma_address(buffer->dma_sgt->sgl);
	buffer->va = sg_virt(buffer->dma_sgt->sgl);

	/* cam_obj_link() will increment entity ref-count */
	if (cam_obj_link(&buffer->nsobj, &entity->nsobj))
		goto error;

	if (cam_obj_insert(&buffer->nsobj))
		goto error;

	return buffer;

error:
	cam_buffer_release(&buffer->nsobj);
	return NULL;
}
EXPORT_SYMBOL_GPL(cam_buffer_register);
ALLOW_ERROR_INJECTION(cam_buffer_register, NULL);

/**
 * cam_buffer_unregister() - Unregister (remove from namespace and possibly
 * release) imported DMA buffer
 * @ns: pointer to CAM namespace
 * @id: ID of the namespace object
 */
void cam_buffer_unregister(struct cam_ns *ns, u32 id)
{
	cam_obj_remove_id(ns, CAM_OBJ_TYPE_BUFFER, id);
}
EXPORT_SYMBOL_GPL(cam_buffer_unregister);

/**
 * cam_buffer_lookup() - Lookup CAM buffer by ID
 * @ns: pointer to CAM namespace
 * @id: ID of CAM buffer
 *
 * Return: NULL on error or CAM buffer pointer otherwise. Returned object is
 * valid and has incremented ref-counter, call cam_buffer_put() to properly
 * decrement ref-counter back.
 */
struct cam_obj_buffer *cam_buffer_lookup(struct cam_ns *ns, u32 id)
{
	struct cam_obj *nsobj;

	nsobj = cam_obj_lookup(ns, CAM_OBJ_TYPE_BUFFER, id);
	if (!nsobj)
		return NULL;

	return nsobj_to_cam_buffer(nsobj);
}
EXPORT_SYMBOL_GPL(cam_buffer_lookup);
ALLOW_ERROR_INJECTION(cam_buffer_lookup, NULL);

/**
 * cam_buffer_put() - Decrements ref-counter of the CAM buffer
 * @buffer: pointer to CAM buffer
 */
void cam_buffer_put(struct cam_obj_buffer *buffer)
{
	if (likely(buffer))
		cam_obj_put(&buffer->nsobj);
	else
		WARN_ON(1);
}
EXPORT_SYMBOL_GPL(cam_buffer_put);
