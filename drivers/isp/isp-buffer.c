// SPDX-License-Identifier: GPL-2.0
/*
 * ISP buffer
 *
 * Copyright (C) Google LLC
 */

#define pr_fmt(fmt) "isp-buffer: " fmt

#include <linux/isp/isp-buffer.h>
#include <linux/isp/isp-device.h>
#include <linux/isp/isp-entity.h>
#include <linux/isp/isp-namespace.h>
#include <linux/isp/isp-output.h>
#include <linux/isp/isp-pipeline.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include <uapi/linux/isp.h>

static bool isp_valid_buffer_id(u32 id)
{
	if (id > ISP_OBJS_NS_BUFFER_ID_END) {
		pr_devel("Invalid buffer ID: %u\n", id);
		return false;
	}
	return true;
}

/**
 * nsobj_to_isp_buffer() - Get ISP buffer pointer from the associated ISP
 * object
 * @nsobj: pointer to ISP object that represents a ISP buffer
 *
 * Return: NULL on error or ISP buffer pointer otherwise.
 */
static struct isp_obj_buffer *nsobj_to_isp_buffer(struct isp_obj *nsobj)
{
	if (!isp_obj_check_type(nsobj, ISP_OBJ_TYPE_BUFFER))
		return NULL;

	return container_of(nsobj, struct isp_obj_buffer, nsobj);
}

/**
 * isp_buffer_release() - The release function of ISP buffer
 * @work: pointer to buffer deferred release work
 */
static void isp_buffer_release(struct work_struct *work)
{
	struct isp_obj_buffer *buffer;

	buffer = container_of(work, struct isp_obj_buffer, release_work);

	if (buffer->driver_data) {
		void *dev = isp_entity_driver_data(buffer->entity);

		buffer->entity->ops->dmabuf_remove(dev,
						   buffer->driver_data,
						   buffer->dma_buf);
	}

	if (buffer->entity)
		isp_entity_put(buffer->entity);

	if (!IS_ERR_OR_NULL(buffer->dma_buf))
		dma_buf_put(buffer->dma_buf);

	kfree(buffer);
}

static void isp_buffer_deferred_release(struct isp_obj *nsobj)
{
	struct isp_obj_buffer *buffer = nsobj_to_isp_buffer(nsobj);

	if (!buffer)
		return;

	queue_work(system_long_wq, &buffer->release_work);
}

/**
 * isp_buffer_register() - Imports and registers (inserts into namespace) DMA
 * buffer
 * @ns: pointer to ISP namespace
 * @entity: parent entity to link to
 * @fd: file descriptor of the imported DMA buffer
 * @id: requested object ID
 *
 * Return: NULL on error or ISP buffer pointer otherwise.
 */
struct isp_obj_buffer *isp_buffer_register(struct isp_ns *ns,
					   struct isp_obj_entity *entity,
					   u32 fd,
					   u32 id)
{
	struct isp_obj_buffer *buffer;
	void *dev;

	if (!isp_valid_buffer_id(id))
		return NULL;

	if (!isp_entity_get(entity))
		return NULL;

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer) {
		isp_entity_put(entity);
		return NULL;
	}

	isp_obj_init(&buffer->nsobj,
		     ISP_OBJ_TYPE_BUFFER,
		     isp_buffer_deferred_release,
		     ns);
	isp_obj_set_id(&buffer->nsobj, id);
	INIT_WORK(&buffer->release_work, isp_buffer_release);

	buffer->dma_buf = dma_buf_get(fd);
	if (IS_ERR(buffer->dma_buf))
		goto error;

	dev = isp_entity_driver_data(entity);
	buffer->entity = entity;
	buffer->driver_data = entity->ops->dmabuf_add(dev, buffer->dma_buf);
	if (!buffer->driver_data)
		goto error;

	if (isp_obj_insert(&buffer->nsobj))
		goto error;

	return buffer;

error:
	isp_buffer_release(&buffer->release_work);
	return NULL;
}
EXPORT_SYMBOL_GPL(isp_buffer_register);
ALLOW_ERROR_INJECTION(isp_buffer_register, NULL);

/**
 * isp_buffer_unregister() - Unregister (remove from namespace and possibly
 * release) imported DMA buffer
 * @ns: pointer to ISP namespace
 * @id: ID of the namespace object
 *
 * Return: 0 on success or negative error code otherwise
 */
int isp_buffer_unregister(struct isp_ns *ns, u32 id)
{
	if (!isp_valid_buffer_id(id))
		return -EINVAL;
	return isp_obj_remove_id(ns, ISP_OBJ_TYPE_BUFFER, id);
}
EXPORT_SYMBOL_GPL(isp_buffer_unregister);

/**
 * isp_buffer_lookup() - Lookup ISP buffer by ID
 * @ns: pointer to ISP namespace
 * @id: ID of ISP buffer
 *
 * Return: NULL on error or ISP buffer pointer otherwise. Returned object is
 * valid and has incremented ref-counter, call isp_buffer_put() to properly
 * decrement ref-counter back.
 */
struct isp_obj_buffer *isp_buffer_lookup(struct isp_ns *ns, u32 id)
{
	struct isp_obj *nsobj;

	if (!isp_valid_buffer_id(id))
		return NULL;

	nsobj = isp_obj_lookup(ns, ISP_OBJ_TYPE_BUFFER, id);
	if (!nsobj)
		return NULL;

	return nsobj_to_isp_buffer(nsobj);
}
EXPORT_SYMBOL_GPL(isp_buffer_lookup);
ALLOW_ERROR_INJECTION(isp_buffer_lookup, NULL);

/**
 * isp_buffer_put() - Decrements ref-counter of the ISP buffer
 * @buffer: pointer to ISP buffer
 */
void isp_buffer_put(struct isp_obj_buffer *buffer)
{
	if (likely(buffer))
		isp_obj_put(&buffer->nsobj);
	else
		WARN_ON(1);
}
EXPORT_SYMBOL_GPL(isp_buffer_put);

/**
 * isp_buffer_get() - Increment ref-counter of the ISP buffer
 * @buffer: pointer to ISP buffer
 *
 * Return: true if buffer ref-count was incremented and false otherwise
 */
bool __must_check isp_buffer_get(struct isp_obj_buffer *buffer)
{
	if (isp_obj_get(&buffer->nsobj))
		return true;

	return false;
}
EXPORT_SYMBOL_GPL(isp_buffer_get);

void *isp_buffer_driver_data(struct isp_obj_buffer *buffer)
{
	return buffer->driver_data;
}
EXPORT_SYMBOL_GPL(isp_buffer_driver_data);

static bool enum_buffer(struct isp_obj *nsobj, struct isp_ns_walk_control *ctl)
{
	struct isp_query_dmabuf_entry *qent;
	struct isp_obj_buffer *buffer;
	struct isp_koutput *output;
	struct dma_buf *dma_buf;

	output = ctl->data;
	if (!isp_output_has_buffer(output))
		return true;

	if (!(nsobj->type & ISP_OBJ_TYPE_BUFFER))
		return false;

	buffer = nsobj_to_isp_buffer(nsobj);
	if (WARN_ON(!buffer))
		return true;

	dma_buf = (struct dma_buf *)ctl->flags;
	if (buffer->dma_buf != dma_buf)
		return false;

	isp_output_next_entry(output, qent);
	if (!qent)
		return true;

	if (put_user(isp_obj_id(nsobj), &qent->id))
		return true;

	output->num_entries++;
	return true;
}

int isp_enum_buffer(struct isp_pipeline *pipeline,
		    struct isp_query_dmabuf *query,
		    struct isp_koutput *output)
{
	struct isp_ns_walk_control ctl;
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
	isp_ns_for_each(&pipeline->objs, &ctl);
	dma_buf_put(dma_buf);

	if (!output->num_entries)
		return -ENOENT;
	return 0;
}
ALLOW_ERROR_INJECTION(isp_enum_buffer, ERRNO);

/**
 * isp_drain_buffers() - Drains pipeline buffers. This must be used
 * only from the pipeline (emergency) termination path.
 * @pipeline: pointer to ISP pipeline
 *
 * Return: 0 on success or negative error code otherwise
 */
int isp_drain_buffers(struct isp_pipeline *pipeline)
{
	struct isp_obj *nsobj;
	struct isp_obj *save;
	int ret;

	isp_ns_for_each_obj_safe(nsobj, save, &pipeline->objs) {
		if (isp_obj_type(nsobj) != ISP_OBJ_TYPE_BUFFER)
			continue;
		ret = isp_buffer_unregister(&pipeline->objs, isp_obj_id(nsobj));
		if (ret)
			return ret;
	}
	return 0;
}
