// SPDX-License-Identifier: GPL-2.0
/*
 * CAM buffer
 *
 * Copyright (C) Google LLC
 */

#define pr_fmt(fmt) "cam-buffer: " fmt

#include <linux/cam/cam-buffer.h>
#include <linux/cam/cam-device.h>
#include <linux/cam/cam-entity.h>
#include <linux/cam/cam-namespace.h>
#include <linux/cam/cam-output.h>
#include <linux/cam/cam-pipeline.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include <uapi/linux/cam.h>

static bool cam_valid_buffer_id(u32 id)
{
	if (id > CAM_OBJS_NS_BUFFER_ID_END) {
		pr_devel("Invalid buffer ID: %u\n", id);
		return false;
	}
	return true;
}

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
 * @work: pointer to buffer deferred release work
 */
static void cam_buffer_release(struct work_struct *work)
{
	struct cam_obj_buffer *buffer;

	buffer = container_of(work, struct cam_obj_buffer, release_work);

	if (buffer->driver_data) {
		void *dev = cam_entity_driver_data(buffer->entity);

		buffer->entity->ops->dmabuf_remove(dev,
						   buffer->driver_data,
						   buffer->dma_buf);
	}

	if (buffer->entity)
		cam_entity_put(buffer->entity);

	if (!IS_ERR_OR_NULL(buffer->dma_buf))
		dma_buf_put(buffer->dma_buf);

	kfree(buffer);
}

static void cam_buffer_deferred_release(struct cam_obj *nsobj)
{
	struct cam_obj_buffer *buffer = nsobj_to_cam_buffer(nsobj);

	if (!buffer)
		return;

	queue_work(system_long_wq, &buffer->release_work);
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
	void *dev;

	if (!cam_valid_buffer_id(id))
		return NULL;

	if (!cam_entity_get(entity))
		return NULL;

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer) {
		cam_entity_put(entity);
		return NULL;
	}

	cam_obj_init(&buffer->nsobj,
		     CAM_OBJ_TYPE_BUFFER,
		     cam_buffer_deferred_release,
		     ns);
	cam_obj_set_id(&buffer->nsobj, id);
	INIT_WORK(&buffer->release_work, cam_buffer_release);

	buffer->dma_buf = dma_buf_get(fd);
	if (IS_ERR(buffer->dma_buf))
		goto error;

	dev = cam_entity_driver_data(entity);
	buffer->entity = entity;
	buffer->driver_data = entity->ops->dmabuf_add(dev, buffer->dma_buf);
	if (!buffer->driver_data)
		goto error;

	if (cam_obj_insert(&buffer->nsobj))
		goto error;

	return buffer;

error:
	cam_buffer_release(&buffer->release_work);
	return NULL;
}
EXPORT_SYMBOL_GPL(cam_buffer_register);
ALLOW_ERROR_INJECTION(cam_buffer_register, NULL);

/**
 * cam_buffer_unregister() - Unregister (remove from namespace and possibly
 * release) imported DMA buffer
 * @ns: pointer to CAM namespace
 * @id: ID of the namespace object
 *
 * Return: 0 on success or negative error code otherwise
 */
int cam_buffer_unregister(struct cam_ns *ns, u32 id)
{
	if (!cam_valid_buffer_id(id))
		return -EINVAL;
	return cam_obj_remove_id(ns, CAM_OBJ_TYPE_BUFFER, id);
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

	if (!cam_valid_buffer_id(id))
		return NULL;

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

/**
 * cam_buffer_get() - Increment ref-counter of the CAM buffer
 * @buffer: pointer to CAM buffer
 *
 * Return: true if buffer ref-count was incremented and false otherwise
 */
bool __must_check cam_buffer_get(struct cam_obj_buffer *buffer)
{
	if (cam_obj_get(&buffer->nsobj))
		return true;

	return false;
}
EXPORT_SYMBOL_GPL(cam_buffer_get);

void *cam_buffer_driver_data(struct cam_obj_buffer *buffer)
{
	return buffer->driver_data;
}
EXPORT_SYMBOL_GPL(cam_buffer_driver_data);

static bool enum_buffer(struct cam_obj *nsobj, struct cam_ns_walk_control *ctl)
{
	struct cam_query_dmabuf_entry *qent;
	struct cam_obj_buffer *buffer;
	struct cam_koutput *output;
	struct dma_buf *dma_buf;

	output = ctl->data;
	if (!cam_output_has_buffer(output))
		return true;

	if (!(nsobj->type & CAM_OBJ_TYPE_BUFFER))
		return false;

	buffer = nsobj_to_cam_buffer(nsobj);
	if (WARN_ON(!buffer))
		return true;

	dma_buf = (struct dma_buf *)ctl->flags;
	if (buffer->dma_buf != dma_buf)
		return false;

	cam_output_next_entry(output, qent);
	if (!qent)
		return true;

	if (put_user(cam_obj_id(nsobj), &qent->id))
		return true;

	output->num_entries++;
	return true;
}

int cam_enum_buffer(struct cam_pipeline *pipeline,
		    struct cam_query_dmabuf *query,
		    struct cam_koutput *output)
{
	struct cam_ns_walk_control ctl;
	struct dma_buf *dma_buf;

	dma_buf = dma_buf_get(query->fd);
	if (IS_ERR(dma_buf))
		return -EINVAL;

	/*
	 * @FIXME
	 * This is a quick and dirty implementation that walks the entire
	 * namespace searching for DMA buffers.
	 */
	ctl.data	= output;
	ctl.flags	= (u64)dma_buf;
	ctl.cb		= enum_buffer;
	cam_ns_for_each(&pipeline->objs, &ctl);
	dma_buf_put(dma_buf);

	if (!output->num_entries)
		return -ENOENT;
	return 0;
}
ALLOW_ERROR_INJECTION(cam_enum_buffer, ERRNO);

/**
 * cam_drain_buffers() - Drains pipeline buffers. This must be used
 * only from the pipeline (emergency) termination path.
 * @pipeline: pointer to CAM pipeline
 *
 * Return: 0 on success or negative error code otherwise
 */
int cam_drain_buffers(struct cam_pipeline *pipeline)
{
	struct cam_obj *nsobj;
	struct cam_obj *save;
	int ret;

	cam_ns_for_each_obj_safe(nsobj, save, &pipeline->objs) {
		if (cam_obj_type(nsobj) != CAM_OBJ_TYPE_BUFFER)
			continue;
		ret = cam_buffer_unregister(&pipeline->objs, cam_obj_id(nsobj));
		if (ret)
			return ret;
	}
	return 0;
}
