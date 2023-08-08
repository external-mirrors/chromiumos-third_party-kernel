// SPDX-License-Identifier: GPL-2.0
/*
 * ISP entity/events
 *
 * Copyright (C) Google LLC
 */

#define pr_fmt(fmt) "isp-entity: " fmt

#include <linux/isp/isp-device.h>
#include <linux/isp/isp-entity.h>
#include <linux/isp/isp-graph.h>
#include <linux/isp/isp-output.h>
#include <linux/isp/isp-pipeline.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>

#include <trace/events/isp.h>

static int root_entity_instance_read(void *dev,
				     struct isp_obj_instance *instance,
				     struct isp_read_instruction *rw)
{
	WARN_ON(1);
	return -EINVAL;
}

static int root_entity_instance_write(void *dev,
				      struct isp_obj_instance *instance,
				      struct isp_write_instruction *rw)
{
	WARN_ON(1);
	return -EINVAL;
}

static void *root_entity_instance_create(void *dev)
{
	WARN_ON(1);
	return NULL;
}

static void root_entity_instance_destroy(void *dev, void *data)
{
	WARN_ON(1);
}

static void *root_entity_dmabuf_add(void *dev, struct dma_buf *buffer)
{
	WARN_ON(1);
	return NULL;
}

static void root_entity_dmabuf_remove(void *dev,
				      void *data,
				      struct dma_buf *dma_buf)
{
	WARN_ON(1);
}

static struct isp_entity_ops root_entity_ops = {
	.instance_read		= root_entity_instance_read,
	.instance_write		= root_entity_instance_write,
	.instance_create	= root_entity_instance_create,
	.instance_destroy	= root_entity_instance_destroy,
	.dmabuf_add		= root_entity_dmabuf_add,
	.dmabuf_remove		= root_entity_dmabuf_remove,
};

/**
 * nsobj_to_isp_entity() - Get ISP entity pointer from the associated ISP
 * object
 * @nsobj: pointer to ISP object that represents a ISP entity
 *
 * Return: NULL on error or ISP entity pointer otherwise.
 */
static struct isp_obj_entity *nsobj_to_isp_entity(struct isp_obj *nsobj)
{
	/* Should never happen */
	if (!isp_obj_check_type(nsobj, ISP_OBJ_TYPE_ENTITY))
		return NULL;

	return container_of(nsobj, struct isp_obj_entity, nsobj);
}

/**
 * isp_entity_lookup() - Lookup ISP entity by ID
 * @isp: pointer to ISP device
 * @id: ID of ISP entity
 *
 * Return: NULL on error or ISP entity pointer otherwise. Returned object is
 * valid and has incremented ref-counter, call isp_entity_put() to properly
 * decrement ref-counter back.
 */
struct isp_obj_entity *isp_entity_lookup(struct isp_device *isp, u32 id)
{
	struct isp_obj *nsobj;

	nsobj = isp_obj_lookup(&isp->ns, ISP_OBJ_TYPE_ENTITY, id);
	if (!nsobj)
		return NULL;

	return nsobj_to_isp_entity(nsobj);
}
EXPORT_SYMBOL_GPL(isp_entity_lookup);
ALLOW_ERROR_INJECTION(isp_entity_lookup, NULL);

/**
 * isp_entity_release() - The release function of ISP entity
 * @nsobj: pointer to ISP object that represents a ISP entity
 */
static void isp_entity_release(struct isp_obj *nsobj)
{
	struct isp_obj_entity *entity = nsobj_to_isp_entity(nsobj);

	isp_obj_unlink(nsobj);
	kfree(entity);
}

u32 isp_entity_id(struct isp_obj_entity *entity)
{
	return isp_obj_id(&entity->nsobj);
}
EXPORT_SYMBOL_GPL(isp_entity_id);

void *isp_entity_driver_data(struct isp_obj_entity *entity)
{
	return entity->driver_data;
}
EXPORT_SYMBOL_GPL(isp_entity_driver_data);

/**
 * isp_entity_get() - Increments ref-counter of the ISP entity
 * @entity: pointer to ISP entity
 *
 * Return: true if entity ref-count was incremented and false otherwise
 */
bool __must_check isp_entity_get(struct isp_obj_entity *entity)
{
	if (isp_obj_get(&entity->nsobj))
		return true;

	return false;
}

/**
 * isp_entity_put() - Decrements ref-counter of the ISP entity
 * @entity: pointer to ISP entity
 */
void isp_entity_put(struct isp_obj_entity *entity)
{
	if (likely(entity))
		isp_obj_put(&entity->nsobj);
	else
		WARN_ON(1);
}
EXPORT_SYMBOL_GPL(isp_entity_put);

/**
 * isp_entity_unregister() - Unregister (remove from namespace and possibly
 * release) ISP entity
 * @entity: pointer to ISP entity
 */
void isp_entity_unregister(struct isp_obj_entity *entity)
{
	isp_obj_remove(&entity->nsobj);
	isp_obj_deinit(&entity->nsobj);
}
EXPORT_SYMBOL_GPL(isp_entity_unregister);

/**
 * isp_entity_register() - Imports and registers (inserts into namespace) ISP
 * entity
 * @isp: pointer to ISP device
 * @parent_id: ID of parent entity object
 * @driver_data: Pointer to driver data associated with this entity
 * @ops: entity operations
 * @num_instances: max number of instances
 * @namefmt: entity name format string
 *
 * Return: NULL on error or ISP entity pointer otherwise.
 */
__printf(6, 7)
struct isp_obj_entity *isp_entity_register(struct isp_device *isp,
					   u32 parent_id,
					   void *driver_data,
					   struct isp_entity_ops *ops,
					   s32 num_instances,
					   const char *namefmt,
					   ...)
{
	char name[ISP_ENTITY_NAME_SZ];
	struct isp_obj_entity *entity;
	struct isp_obj_entity *link;
	va_list args;

	lockdep_assert_held_write(&isp->ns_enum_lock);
	if (WARN_ON(num_instances < ISP_ENTITY_MIN_INSTANCES))
		return NULL;

	if (WARN_ON(!ops))
		return NULL;

	if (!ops->instance_read)
		ops->instance_read = root_entity_instance_read;
	if (!ops->instance_write)
		ops->instance_write = root_entity_instance_write;
	if (!ops->instance_create)
		ops->instance_create = root_entity_instance_create;
	if (!ops->instance_destroy)
		ops->instance_destroy = root_entity_instance_destroy;
	if (!ops->dmabuf_add)
		ops->dmabuf_add = root_entity_dmabuf_add;
	if (!ops->dmabuf_remove)
		ops->dmabuf_remove = root_entity_dmabuf_remove;

	entity = kzalloc(sizeof(*entity), GFP_KERNEL);
	if (!entity)
		return NULL;

	link = isp_entity_lookup(isp, parent_id);
	if (!link)
		goto error;

	va_start(args, namefmt);
	vsnprintf(name, sizeof(name), namefmt, args);
	va_end(args);

	atomic_set(&entity->instances_avail, num_instances);
	entity->ops = ops;
	entity->driver_data = driver_data;
	strscpy(entity->name, name, ISP_ENTITY_NAME_SZ);
	isp_obj_init(&entity->nsobj, ISP_OBJ_TYPE_ENTITY, isp_entity_release,
		     &isp->ns);

	if (isp_obj_link(&entity->nsobj, &link->nsobj))
		goto error;

	/* Link increments ref-counter of the object we link to */
	isp_entity_put(link);

	if (isp_obj_insert(&entity->nsobj))
		goto error;

	return entity;

error:
	isp_entity_put(link);
	isp_entity_release(&entity->nsobj);
	return NULL;
}
EXPORT_SYMBOL_GPL(isp_entity_register);
ALLOW_ERROR_INJECTION(isp_entity_register, NULL);

/*
 * isp_root_entity_register() - Register the root entity node of a ISP device
 * @isp: pointer to ISP device
 *
 * A special case of entity: no parent and hard-coded name. We technically
 * could use isp_entity_register() and then have a bunch of if-s there to
 * special case entity that must have NULL parent, but it's simpler this
 * way and keeps isp_entity_register() straightforward and clean.
 *
 * Return: NULL on error or ISP entity pointer otherwise.
 */
struct isp_obj_entity *isp_root_entity_register(struct isp_device *isp)
{
	struct isp_obj_entity *entity;

	entity = kzalloc(sizeof(*entity), GFP_KERNEL);
	if (!entity)
		return NULL;

	atomic_set(&entity->instances_avail, ISP_ENTITY_MIN_INSTANCES);
	entity->ops = &root_entity_ops;
	strscpy(entity->name, "ISP root entity", ISP_ENTITY_NAME_SZ);
	isp_obj_init(&entity->nsobj, ISP_OBJ_TYPE_ENTITY, isp_entity_release,
		     &isp->ns);

	if (isp_obj_insert(&entity->nsobj))
		goto error;

	if (WARN_ON(isp_entity_id(entity) != ISP_OBJ_ID_ROOT)) {
		isp_obj_remove(&entity->nsobj);
		goto error;
	}

	return entity;

error:
	isp_obj_deinit(&entity->nsobj);
	return NULL;
}

static bool isp_valid_instance_id(u32 id)
{
	if (id + ISP_OBJS_NS_INSTANCE_ID_START > ISP_OBJS_NS_INSTANCE_ID_END) {
		pr_devel("Invalid instance ID: %u\n", id);
		return false;
	}
	return true;
}

/**
 * nsobj_to_isp_instance() - Get ISP instance pointer from the associated ISP
 * object
 * @nsobj: pointer to ISP object that represents a ISP instance
 *
 * Return: NULL on error or ISP instance pointer otherwise.
 */
static struct isp_obj_instance *nsobj_to_isp_instance(struct isp_obj *nsobj)
{
	if (!isp_obj_check_type(nsobj, ISP_OBJ_TYPE_INSTANCE))
		return NULL;

	return container_of(nsobj, struct isp_obj_instance, nsobj);
}

/**
 * isp_instance_release() - The release function of ISP instance
 * @work: pointer to instance deferred release work
 */
static void isp_instance_release(struct work_struct *work)
{
	struct isp_obj_instance *instance;
	struct isp_obj *link;
	void *dev, *dev_data;

	instance = container_of(work, struct isp_obj_instance, release_work);
	link = isp_obj_linked_to(&instance->nsobj);
	dev_data = instance->driver_data;
	if (link) {
		struct isp_obj_entity *entity;

		entity = nsobj_to_isp_entity(link);
		if (entity) {
			dev = isp_entity_driver_data(entity);
			atomic_inc(&entity->instances_avail);
			if (dev_data)
				entity->ops->instance_destroy(dev, dev_data);
		} else {
			pr_err("Unable to destroy entity instance\n");
		}
		isp_obj_put(link);
	}

	isp_obj_unlink(&instance->nsobj);
	kfree(instance);
}

static void isp_instance_deferred_release(struct isp_obj *nsobj)
{
	struct isp_obj_instance *instance = nsobj_to_isp_instance(nsobj);

	if (!instance)
		return;

	queue_work(system_long_wq, &instance->release_work);
}

/**
 * isp_instance_destroy() - Destroy entity instance (context)
 * @ns: namespace
 * @id: ID of the instance object
 *
 * Return: 0 on success or negative error code otherwise
 */
int isp_instance_destroy(struct isp_ns *ns, u32 id)
{
	if (!isp_valid_instance_id(id))
		return -EINVAL;

	id += ISP_OBJS_NS_INSTANCE_ID_START;
	return isp_obj_remove_id(ns, ISP_OBJ_TYPE_INSTANCE, id);
}

/**
 * isp_instance_create() - Create entity instance (context)
 * @ns: namespace
 * @entity: ISP entity
 * @id: ID of the instance (context) object
 *
 * Return: NULL on error or ISP instance pointer otherwise
 */
struct isp_obj_instance *isp_instance_create(struct isp_ns *ns,
					     struct isp_obj_entity *entity,
					     u32 id)
{
	struct isp_obj_instance *instance;
	void *dev;

	if (!isp_valid_instance_id(id))
		return NULL;

	id += ISP_OBJS_NS_INSTANCE_ID_START;
	instance = kzalloc(sizeof(*instance), GFP_KERNEL);
	if (!instance)
		return NULL;

	isp_obj_init(&instance->nsobj, ISP_OBJ_TYPE_INSTANCE,
		     isp_instance_deferred_release,
		     ns);
	isp_obj_set_id(&instance->nsobj, id);
	INIT_WORK(&instance->release_work, isp_instance_release);

	if (atomic_dec_if_positive(&entity->instances_avail) < 0)
		goto error;

	if (isp_obj_link(&instance->nsobj, &entity->nsobj)) {
		/*
		 * We failed to link instance to entity so we need to rollback
		 * instances_avail changes.
		 */
		atomic_inc(&entity->instances_avail);
		goto error;
	}

	dev = isp_entity_driver_data(entity);
	instance->driver_data = entity->ops->instance_create(dev);
	if (!instance->driver_data)
		goto error;

	if (isp_obj_insert(&instance->nsobj))
		goto error;

	return instance;

error:
	isp_instance_release(&instance->release_work);
	return NULL;
}

/**
 * isp_instance_lookup() - Lookup ISP instance by ID
 * @ns: pointer to namespace
 * @id: ID of ISP instance
 *
 * Return: NULL on error or ISP instance pointer otherwise. Returned object is
 * valid and has incremented ref-counter, call isp_instance_put() to properly
 * decrement ref-counter back.
 */
struct isp_obj_instance *isp_instance_lookup(struct isp_ns *ns, u32 id)
{
	struct isp_obj *obj = NULL;

	if (!isp_valid_instance_id(id))
		return NULL;

	id += ISP_OBJS_NS_INSTANCE_ID_START;
	obj = isp_obj_lookup(ns, ISP_OBJ_TYPE_INSTANCE, id);
	if (!obj)
		return NULL;

	return nsobj_to_isp_instance(obj);
}
ALLOW_ERROR_INJECTION(isp_instance_lookup, NULL);

/**
 * isp_instance_verify() - Test that provided instance is in fact instance of
 * the given entity.
 * @entity: pointer to ISP entity
 * @instance: pointer to ISP instance
 *
 * Return: true if so, false otherwise.
 */
bool isp_instance_verify(struct isp_obj_entity *entity,
			 struct isp_obj_instance *instance)
{
	/* Instance is linked to its entity */
	return isp_obj_id(&entity->nsobj) == isp_obj_link_id(&instance->nsobj);
}

/**
 * isp_instance_put() - Decrements ref-counter of the ISP instance
 * @instance: pointer to ISP instance
 */
void isp_instance_put(struct isp_obj_instance *instance)
{
	if (likely(instance))
		isp_obj_put(&instance->nsobj);
	else
		WARN_ON(1);
}
EXPORT_SYMBOL_GPL(isp_instance_put);

/**
 * isp_instance_get() - Increment ref-counter of the ISP instance
 * @instance: pointer to ISP instance
 *
 * Return: true if instance ref-count was incremented and false otherwise
 */
bool __must_check isp_instance_get(struct isp_obj_instance *instance)
{
	if (isp_obj_get(&instance->nsobj))
		return true;

	return false;
}
EXPORT_SYMBOL_GPL(isp_instance_get);

/**
 * isp_instance_driver_data() - Access instance driver data
 * @instance: pointer to ISP instance
 *
 * Return: a pointer to instance driver data
 */
void *isp_instance_driver_data(struct isp_obj_instance *instance)
{
	return instance->driver_data;
}
EXPORT_SYMBOL_GPL(isp_instance_driver_data);

/**
 * nsobj_to_isp_event() - Get ISP event pointer from the associated ISP
 * object
 * @nsobj: pointer to ISP object that represents a ISP event
 *
 * Return: NULL on error or ISP event pointer otherwise.
 */
static struct isp_obj_event *nsobj_to_isp_event(struct isp_obj *nsobj)
{
	/* Should never happen */
	if (!isp_obj_check_type(nsobj, ISP_OBJ_TYPE_EVENT))
		return NULL;

	return container_of(nsobj, struct isp_obj_event, nsobj);
}

/**
 * isp_event_lookup() - Lookup ISP event by ID
 * @isp: pointer to ISP device
 * @event_id: ID of ISP event
 *
 * Return: NULL on error or ISP event pointer otherwise. Returned object is
 * valid and has incremented ref-counter, call isp_event_put() to properly
 * decrement ref-counter back.
 */
struct isp_obj_event *isp_event_lookup(struct isp_device *isp, u32 event_id)
{
	struct isp_obj *event = NULL;

	event = isp_obj_lookup(&isp->ns, ISP_OBJ_TYPE_EVENT, event_id);
	if (!event)
		return NULL;

	return nsobj_to_isp_event(event);
}
EXPORT_SYMBOL_GPL(isp_event_lookup);
ALLOW_ERROR_INJECTION(isp_event_lookup, NULL);

/**
 * isp_event_release() - The release function of ISP event
 * @nsobj: pointer to ISP object that represents a ISP event
 */
static void isp_event_release(struct isp_obj *nsobj)
{
	struct isp_obj_event *event = nsobj_to_isp_event(nsobj);

	isp_obj_unlink(nsobj);
	kfree(event);
}

/**
 * isp_event_activate_signal() - Activate a pending signal
 * @sig: pointer to ISP signal, the signal source should be a ISP event
 *
 * Return: True on success or false otherwise.
 */
bool isp_event_activate_signal(struct isp_op_signal *sig)
{
	struct isp_obj_event *event;
	unsigned long flags;

	event = nsobj_to_isp_event(sig->source);
	if (WARN_ON(!event))
		return false;

	/*
	 * notify_active_chain is accessed from the IRQ context,
	 * so we need to disable local IRQs.
	 */
	write_lock_irqsave(&event->notify_lock, flags);
	list_add_tail(&sig->entry, &event->notify_active_chain);
	write_unlock_irqrestore(&event->notify_lock, flags);
	return true;
}

void isp_event_deactivate_signal(struct isp_op_signal *sig)
{
	struct isp_op_signal *active;
	struct isp_obj_event *event;
	unsigned long flags;

	event = nsobj_to_isp_event(sig->source);
	if (WARN_ON(!event))
		return;

	/*
	 * notify_active_chain is accessed from the IRQ context,
	 * so we need to disable local IRQs.
	 */
	write_lock_irqsave(&event->notify_lock, flags);
	list_for_each_entry(active, &event->notify_active_chain, entry) {
		if (active == sig) {
			list_del_init(&sig->entry);
			break;
		}
	}
	write_unlock_irqrestore(&event->notify_lock, flags);
}

/**
 * isp_event_trigger_signals() - Trigger and notify all active signals
 * depending on the provided event
 * @entity: pointer to ISP entity
 * @event: pointer to ISP event
 */
void isp_event_trigger_signals(struct isp_obj_entity *entity,
			       struct isp_obj_event *event)
{
	unsigned long flags;

	trace_isp_event_trigger(entity, NULL, event);
	write_lock_irqsave(&event->notify_lock, flags);
	isp_fire_active_signals(&event->notify_active_chain);
	write_unlock_irqrestore(&event->notify_lock, flags);
}
EXPORT_SYMBOL_GPL(isp_event_trigger_signals);

/**
 * isp_instance_event_trigger_signals() - Trigger and notify all active signals
 * depending on the provided event instance
 * @entity: pointer to ISP entity
 * @instance: entity instance (context)
 * @event: pointer to ISP event
 */
void isp_instance_event_trigger_signals(struct isp_obj_entity *entity,
					struct isp_obj_instance *instance,
					struct isp_obj_event *event)
{
	unsigned long flags;

	trace_isp_event_trigger(entity, instance, event);
	write_lock_irqsave(&event->notify_lock, flags);
	isp_instance_fire_active_signals(instance, &event->notify_active_chain);
	write_unlock_irqrestore(&event->notify_lock, flags);
}
EXPORT_SYMBOL_GPL(isp_instance_event_trigger_signals);

/**
 * isp_event_put() - Decrements ref-counter of the ISP event
 * @event: pointer to ISP event
 */
void isp_event_put(struct isp_obj_event *event)
{
	if (likely(event))
		isp_obj_put(&event->nsobj);
	else
		WARN_ON(1);
}
EXPORT_SYMBOL_GPL(isp_event_put);

/**
 * isp_event_id() - Return ID of the ISP event
 * @event: pointer to ISP event
 *
 * Return: Object ID.
 */
u32 isp_event_id(struct isp_obj_event *event)
{
	return isp_obj_id(&event->nsobj);
}
EXPORT_SYMBOL_GPL(isp_event_id);

/**
 * isp_event_unregister() - Unregister event (remove from namespace and
 * possibly release)
 * @event: pointer to ISP event
 */
void isp_event_unregister(struct isp_obj_event *event)
{
	/* @FIXME */
	WARN_ON(!list_empty(&event->notify_active_chain));
	isp_obj_remove(&event->nsobj);
	isp_obj_deinit(&event->nsobj);
}
EXPORT_SYMBOL_GPL(isp_event_unregister);

/**
 * isp_event_register() - Imports and registers (inserts into namespace) ISP
 * event
 * @isp: pointer to ISP device
 * @entity_id: ID of parent entity object
 * @namefmt: event name format string
 *
 * Return: NULL on error or ISP event pointer otherwise.
 */
__printf(3, 4)
struct isp_obj_event *isp_event_register(struct isp_device *isp,
					 u32 entity_id,
					 const char *namefmt,
					 ...)
{
	struct isp_obj_entity *entity;
	char name[ISP_EVENT_NAME_SZ];
	struct isp_obj_event *event;
	va_list args;

	lockdep_assert_held_write(&isp->ns_enum_lock);

	event = kzalloc(sizeof(*event), GFP_KERNEL);
	if (!event)
		return NULL;

	entity = isp_entity_lookup(isp, entity_id);
	if (!entity)
		goto error;

	va_start(args, namefmt);
	vsnprintf(name, sizeof(name), namefmt, args);
	va_end(args);

	strscpy(event->name, name, ISP_EVENT_NAME_SZ);
	INIT_LIST_HEAD(&event->notify_active_chain);
	rwlock_init(&event->notify_lock);
	isp_obj_init(&event->nsobj, ISP_OBJ_TYPE_EVENT, isp_event_release,
		     &isp->ns);

	if (isp_obj_link(&event->nsobj, &entity->nsobj))
		goto error;

	/* Link increments ref-counter of the object we link to */
	isp_entity_put(entity);

	if (isp_obj_insert(&event->nsobj))
		goto error;

	return event;

error:
	isp_entity_put(entity);
	isp_event_release(&event->nsobj);
	return NULL;
}
EXPORT_SYMBOL_GPL(isp_event_register);
ALLOW_ERROR_INJECTION(isp_event_register, NULL);

/**
 * enum_entity() - Entity enumeration callback
 * @nsobj: pointer to ISP object that represents a ISP entity
 * @ctl: auxiliary data
 *
 * This callback increases the entry counter of the output and, if necessary,
 * copy the entity name to the user space.
 *
 * This function may sleep.
 *
 * Return: True on success or false otherwise.
 */
static bool enum_entity(struct isp_obj *nsobj, struct isp_graph_walk *ctl)
{
	struct isp_query_entity_entry *qent;
	struct isp_obj_entity *entity;
	struct isp_koutput *output;

	output = ctl->data;

	/* User just want the size, not the data. */
	if (!isp_output_has_buffer(output))
		goto out;

	isp_output_next_entry(output, qent);
	if (!qent)
		return false;

	if (put_user(isp_obj_id(nsobj), &qent->id))
		return false;

	entity = nsobj_to_isp_entity(nsobj);
	if (copy_to_user(&qent->name, entity->name, strlen(entity->name)))
		return false;

	if (put_user(isp_obj_link_id(nsobj), &qent->parent))
		return false;

out:
	output->num_entries++;
	return true;
}

/**
 * enum_event() - Event enumeration callback
 * @nsobj: pointer to ISP object that represents a ISP event
 * @ctl: auxiliary data
 *
 * This callback increases the entry counter of the output and, if necessary,
 * copy the event name to the user space.
 *
 * This function may sleep.
 *
 * Return: True on success or false otherwise.
 */
static bool enum_event(struct isp_obj *nsobj, struct isp_graph_walk *ctl)
{
	struct isp_query_event_entry *qent;
	struct isp_obj_event *event;
	struct isp_koutput *output;

	output = ctl->data;

	/* User just want the size, not the data. */
	if (!isp_output_has_buffer(output))
		goto out;

	isp_output_next_entry(output, qent);
	if (!qent)
		return false;

	if (put_user(isp_obj_id(nsobj), &qent->id))
		return false;

	event = nsobj_to_isp_event(nsobj);
	if (copy_to_user(&qent->name, event->name, strlen(event->name)))
		return false;

out:
	output->num_entries++;
	return true;
}

/**
 * entity_depth_limit() - Get depth limit of a query
 * @query: input query from user space
 *
 * Return: The maximal available depth.
 */
static u32 entity_depth_limit(struct isp_query_entities *query)
{
	return min_t(u32, query->maxdepth, ISP_GRAPH_STACK_DEPTH);
}

/**
 * isp_enum_entities() - Enumerate objects that belong to an entity
 * @isp: pointer to ISP device
 * @query: input query from user space
 * @output: output data to user space
 *
 * Return: 0 on success or error value otherwise.
 */
int isp_enum_entities(struct isp_device *isp,
		      struct isp_query_entities *query,
		      struct isp_koutput *output)
{
	struct isp_graph_walk ctl = {};
	struct isp_obj_entity *entity;
	size_t depth;
	int ret;

	query->num_entities = 0;

	entity = isp_entity_lookup(isp, query->id);
	if (!entity)
		return -ENOENT;

	ctl.data	= output;
	ctl.cb		= enum_entity;
	ctl.match_type	= ISP_OBJ_TYPE_ENTITY;
	ctl.match_id	= ISP_QUERY_ALL_OBJECTS;

	depth = entity_depth_limit(query);
	ret = isp_enum_graph_objects(&ctl, &entity->nsobj, depth);
	query->num_entities = output->num_entries;
	isp_entity_put(entity);
	return ret;
}

/**
 * isp_enum_events() - Enumerate objects that belong to an event
 * @isp: pointer to ISP device
 * @query: input query from user space
 * @output: output data to user space
 *
 * Return: 0 on success or error value otherwise.
 */
int isp_enum_events(struct isp_device *isp,
		    struct isp_query_events *query,
		    struct isp_koutput *output)
{
	struct isp_graph_walk ctl = {};
	struct isp_obj_entity *entity;
	int ret;

	query->num_events = 0;

	entity = isp_entity_lookup(isp, query->entity);
	if (!entity)
		return -ENOENT;

	ctl.data	= output;
	ctl.cb		= enum_event;
	ctl.match_type	= ISP_OBJ_TYPE_EVENT;
	/* query->id is either a specific object ID or ISP_QUERY_ALL_OBJECTS */
	ctl.match_id	= query->id;

	ret = isp_enum_graph_objects(&ctl, &entity->nsobj,
				     ISP_GRAPH_STACK_DEPTH);
	query->num_events = output->num_entries;
	isp_entity_put(entity);
	/*
	 * We queried a particular event on a particular entity but could not
	 * find it.
	 */
	if (ctl.match_id != ISP_QUERY_ALL_OBJECTS && !output->num_entries)
		ret = -ENOENT;
	return ret;
}

/**
 * isp_drain_instances() - Drains pipeline entities instances. This must be used
 * only from the pipeline (emergency) termination path.
 * @pipeline: pointer to ISP pipeline
 *
 * Return: 0 on success or negative error code otherwise.
 */
int isp_drain_instances(struct isp_pipeline *pipeline)
{
	struct isp_obj *nsobj;
	struct isp_obj *save;
	int ret;

	isp_ns_for_each_obj_safe(nsobj, save, &pipeline->objs) {
		if (isp_obj_type(nsobj) != ISP_OBJ_TYPE_INSTANCE)
			continue;

		ret = isp_obj_remove_id(&pipeline->objs,
					ISP_OBJ_TYPE_INSTANCE,
					isp_obj_id(nsobj));
		if (ret)
			return ret;
	}
	return 0;
}
