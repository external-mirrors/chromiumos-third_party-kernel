// SPDX-License-Identifier: GPL-2.0
/*
 * CAM entity/events
 *
 * Copyright (C) Google LLC
 */

#define pr_fmt(fmt) "cam-entity: " fmt

#include <linux/cam/cam-device.h>
#include <linux/cam/cam-entity.h>
#include <linux/cam/cam-graph.h>
#include <linux/cam/cam-output.h>
#include <linux/cam/cam-pipeline.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>

#include <trace/events/cam.h>

static int root_entity_instance_read(void *dev,
				     struct cam_obj_instance *instance,
				     struct cam_read_instruction *rw)
{
	WARN_ON(1);
	return -EINVAL;
}

static int root_entity_instance_write(void *dev,
				      struct cam_obj_instance *instance,
				      struct cam_write_instruction *rw)
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

static struct cam_entity_ops root_entity_ops = {
	.instance_read		= root_entity_instance_read,
	.instance_write		= root_entity_instance_write,
	.instance_create	= root_entity_instance_create,
	.instance_destroy	= root_entity_instance_destroy,
	.dmabuf_add		= root_entity_dmabuf_add,
	.dmabuf_remove		= root_entity_dmabuf_remove,
};

/**
 * nsobj_to_cam_entity() - Get CAM entity pointer from the associated CAM
 * object
 * @nsobj: pointer to CAM object that represents a CAM entity
 *
 * Return: NULL on error or CAM entity pointer otherwise.
 */
static struct cam_obj_entity *nsobj_to_cam_entity(struct cam_obj *nsobj)
{
	/* Should never happen */
	if (!cam_obj_check_type(nsobj, CAM_OBJ_TYPE_ENTITY))
		return NULL;

	return container_of(nsobj, struct cam_obj_entity, nsobj);
}

/**
 * cam_entity_lookup() - Lookup CAM entity by ID
 * @cam: pointer to CAM device
 * @id: ID of CAM entity
 *
 * Return: NULL on error or CAM entity pointer otherwise. Returned object is
 * valid and has incremented ref-counter, call cam_entity_put() to properly
 * decrement ref-counter back.
 */
struct cam_obj_entity *cam_entity_lookup(struct cam_device *cam, u32 id)
{
	struct cam_obj *nsobj;

	nsobj = cam_obj_lookup(&cam->ns, CAM_OBJ_TYPE_ENTITY, id);
	if (!nsobj)
		return NULL;

	return nsobj_to_cam_entity(nsobj);
}
EXPORT_SYMBOL_GPL(cam_entity_lookup);
ALLOW_ERROR_INJECTION(cam_entity_lookup, NULL);

/**
 * cam_entity_release() - The release function of CAM entity
 * @nsobj: pointer to CAM object that represents a CAM entity
 */
static void cam_entity_release(struct cam_obj *nsobj)
{
	struct cam_obj_entity *entity = nsobj_to_cam_entity(nsobj);

	cam_obj_unlink(nsobj);
	kfree(entity);
}

u32 cam_entity_id(struct cam_obj_entity *entity)
{
	return cam_obj_id(&entity->nsobj);
}
EXPORT_SYMBOL_GPL(cam_entity_id);

void *cam_entity_driver_data(struct cam_obj_entity *entity)
{
	return entity->driver_data;
}
EXPORT_SYMBOL_GPL(cam_entity_driver_data);

/**
 * cam_entity_get() - Increments ref-counter of the CAM entity
 * @entity: pointer to CAM entity
 *
 * Return: true if entity ref-count was incremented and false otherwise
 */
bool __must_check cam_entity_get(struct cam_obj_entity *entity)
{
	if (cam_obj_get(&entity->nsobj))
		return true;

	return false;
}

/**
 * cam_entity_put() - Decrements ref-counter of the CAM entity
 * @entity: pointer to CAM entity
 */
void cam_entity_put(struct cam_obj_entity *entity)
{
	if (likely(entity))
		cam_obj_put(&entity->nsobj);
	else
		WARN_ON(1);
}
EXPORT_SYMBOL_GPL(cam_entity_put);

/**
 * cam_entity_unregister() - Unregister (remove from namespace and possibly
 * release) CAM entity
 * @entity: pointer to CAM entity
 */
void cam_entity_unregister(struct cam_obj_entity *entity)
{
	cam_obj_remove(&entity->nsobj);
	cam_obj_deinit(&entity->nsobj);
}
EXPORT_SYMBOL_GPL(cam_entity_unregister);

/**
 * cam_entity_register() - Imports and registers (inserts into namespace) CAM
 * entity
 * @cam: pointer to CAM device
 * @parent_id: ID of parent entity object
 * @driver_data: Pointer to driver data associated with this entity
 * @ops: entity operations
 * @num_instances: max number of instances
 * @namefmt: entity name format string
 *
 * Return: NULL on error or CAM entity pointer otherwise.
 */
__printf(6, 7)
struct cam_obj_entity *cam_entity_register(struct cam_device *cam,
					   u32 parent_id,
					   void *driver_data,
					   struct cam_entity_ops *ops,
					   s32 num_instances,
					   const char *namefmt,
					   ...)
{
	char name[CAM_ENTITY_NAME_SZ];
	struct cam_obj_entity *entity;
	struct cam_obj_entity *link;
	va_list args;

	lockdep_assert_held_write(&cam->ns_enum_lock);
	if (WARN_ON(num_instances < CAM_ENTITY_MIN_INSTANCES))
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

	link = cam_entity_lookup(cam, parent_id);
	if (!link)
		goto error;

	va_start(args, namefmt);
	vsnprintf(name, sizeof(name), namefmt, args);
	va_end(args);

	atomic_set(&entity->instances_avail, num_instances);
	entity->ops = ops;
	entity->driver_data = driver_data;
	strscpy(entity->name, name, CAM_ENTITY_NAME_SZ);
	cam_obj_init(&entity->nsobj, CAM_OBJ_TYPE_ENTITY, cam_entity_release,
		     &cam->ns);

	if (cam_obj_link(&entity->nsobj, &link->nsobj))
		goto error;

	/* Link increments ref-counter of the object we link to */
	cam_entity_put(link);

	if (cam_obj_insert(&entity->nsobj))
		goto error;

	return entity;

error:
	cam_entity_put(link);
	cam_entity_release(&entity->nsobj);
	return NULL;
}
EXPORT_SYMBOL_GPL(cam_entity_register);
ALLOW_ERROR_INJECTION(cam_entity_register, NULL);

/*
 * cam_root_entity_register() - Register the root entity node of a CAM device
 * @cam: pointer to CAM device
 *
 * A special case of entity: no parent and hard-coded name. We technically
 * could use cam_entity_register() and then have a bunch of if-s there to
 * special case entity that must have NULL parent, but it's simpler this
 * way and keeps cam_entity_register() straightforward and clean.
 *
 * Return: NULL on error or CAM entity pointer otherwise.
 */
struct cam_obj_entity *cam_root_entity_register(struct cam_device *cam)
{
	struct cam_obj_entity *entity;

	entity = kzalloc(sizeof(*entity), GFP_KERNEL);
	if (!entity)
		return NULL;

	atomic_set(&entity->instances_avail, CAM_ENTITY_MIN_INSTANCES);
	entity->ops = &root_entity_ops;
	strscpy(entity->name, "CAM root entity", CAM_ENTITY_NAME_SZ);
	cam_obj_init(&entity->nsobj, CAM_OBJ_TYPE_ENTITY, cam_entity_release,
		     &cam->ns);

	if (cam_obj_insert(&entity->nsobj))
		goto error;

	if (WARN_ON(cam_entity_id(entity) != CAM_OBJ_ID_ROOT)) {
		cam_obj_remove(&entity->nsobj);
		goto error;
	}

	return entity;

error:
	cam_obj_deinit(&entity->nsobj);
	return NULL;
}

static bool cam_valid_instance_id(u32 id)
{
	if (id + CAM_OBJS_NS_INSTANCE_ID_START > CAM_OBJS_NS_INSTANCE_ID_END) {
		pr_err("Invalid instance ID: %u\n", id);
		return false;
	}
	return true;
}

/**
 * nsobj_to_cam_instance() - Get CAM instance pointer from the associated CAM
 * object
 * @nsobj: pointer to CAM object that represents a CAM instance
 *
 * Return: NULL on error or CAM instance pointer otherwise.
 */
static struct cam_obj_instance *nsobj_to_cam_instance(struct cam_obj *nsobj)
{
	if (!cam_obj_check_type(nsobj, CAM_OBJ_TYPE_INSTANCE))
		return NULL;

	return container_of(nsobj, struct cam_obj_instance, nsobj);
}

/**
 * cam_instance_release() - The release function of CAM instance
 * @work: pointer to instance deferred release work
 */
static void cam_instance_release(struct work_struct *work)
{
	struct cam_obj_instance *instance;
	struct cam_obj *link;
	void *dev, *dev_data;

	instance = container_of(work, struct cam_obj_instance, release_work);
	link = cam_obj_linked_to(&instance->nsobj);
	dev_data = instance->driver_data;
	if (link) {
		struct cam_obj_entity *entity;

		entity = nsobj_to_cam_entity(link);
		if (entity) {
			dev = cam_entity_driver_data(entity);
			atomic_inc(&entity->instances_avail);
			if (dev_data)
				entity->ops->instance_destroy(dev, dev_data);
		} else {
			pr_err("Unable to destroy entity instance\n");
		}
		cam_obj_put(link);
	}

	cam_obj_unlink(&instance->nsobj);
	kfree(instance);
}

static void cam_instance_deferred_release(struct cam_obj *nsobj)
{
	struct cam_obj_instance *instance = nsobj_to_cam_instance(nsobj);

	if (!instance)
		return;

	queue_work(system_long_wq, &instance->release_work);
}

/**
 * cam_instance_destroy() - Destroy entity instance (context)
 * @ns: namespace
 * @id: ID of the instance object
 *
 * Return: 0 on success or negative error code otherwise
 */
int cam_instance_destroy(struct cam_ns *ns, u32 id)
{
	if (!cam_valid_instance_id(id))
		return -EINVAL;

	id += CAM_OBJS_NS_INSTANCE_ID_START;
	return cam_obj_remove_id(ns, CAM_OBJ_TYPE_INSTANCE, id);
}

/**
 * cam_instance_create() - Create entity instance (context)
 * @ns: namespace
 * @entity: CAM entity
 * @id: ID of the instance (context) object
 *
 * Return: NULL on error or CAM instance pointer otherwise
 */
struct cam_obj_instance *cam_instance_create(struct cam_ns *ns,
					     struct cam_obj_entity *entity,
					     u32 id)
{
	struct cam_obj_instance *instance;
	void *dev;

	if (!cam_valid_instance_id(id))
		return NULL;

	id += CAM_OBJS_NS_INSTANCE_ID_START;
	instance = kzalloc(sizeof(*instance), GFP_KERNEL);
	if (!instance)
		return NULL;

	cam_obj_init(&instance->nsobj, CAM_OBJ_TYPE_INSTANCE,
		     cam_instance_deferred_release,
		     ns);
	cam_obj_set_id(&instance->nsobj, id);
	INIT_WORK(&instance->release_work, cam_instance_release);

	if (atomic_dec_if_positive(&entity->instances_avail) < 0)
		goto error;

	if (cam_obj_link(&instance->nsobj, &entity->nsobj)) {
		/*
		 * We failed to link instance to entity so we need to rollback
		 * instances_avail changes.
		 */
		atomic_inc(&entity->instances_avail);
		goto error;
	}

	dev = cam_entity_driver_data(entity);
	instance->driver_data = entity->ops->instance_create(dev);
	if (!instance->driver_data)
		goto error;

	if (cam_obj_insert(&instance->nsobj))
		goto error;

	return instance;

error:
	cam_instance_release(&instance->release_work);
	return NULL;
}

/**
 * cam_instance_lookup() - Lookup CAM instance by ID
 * @ns: pointer to namespace
 * @id: ID of CAM instance
 *
 * Return: NULL on error or CAM instance pointer otherwise. Returned object is
 * valid and has incremented ref-counter, call cam_instance_put() to properly
 * decrement ref-counter back.
 */
struct cam_obj_instance *cam_instance_lookup(struct cam_ns *ns, u32 id)
{
	struct cam_obj *obj = NULL;

	if (!cam_valid_instance_id(id))
		return NULL;

	id += CAM_OBJS_NS_INSTANCE_ID_START;
	obj = cam_obj_lookup(ns, CAM_OBJ_TYPE_INSTANCE, id);
	if (!obj)
		return NULL;

	return nsobj_to_cam_instance(obj);
}
ALLOW_ERROR_INJECTION(cam_instance_lookup, NULL);

/**
 * cam_instance_put() - Decrements ref-counter of the CAM instance
 * @instance: pointer to CAM instance
 */
void cam_instance_put(struct cam_obj_instance *instance)
{
	if (likely(instance))
		cam_obj_put(&instance->nsobj);
	else
		WARN_ON(1);
}
EXPORT_SYMBOL_GPL(cam_instance_put);

/**
 * cam_instance_get() - Increment ref-counter of the CAM instance
 * @instance: pointer to CAM instance
 *
 * Return: true if instance ref-count was incremented and false otherwise
 */
bool __must_check cam_instance_get(struct cam_obj_instance *instance)
{
	if (cam_obj_get(&instance->nsobj))
		return true;

	return false;
}
EXPORT_SYMBOL_GPL(cam_instance_get);

/**
 * cam_instance_driver_data() - Access instance driver data
 * @instance: pointer to CAM instance
 *
 * Return: a pointer to instance driver data
 */
void *cam_instance_driver_data(struct cam_obj_instance *instance)
{
	return instance->driver_data;
}
EXPORT_SYMBOL_GPL(cam_instance_driver_data);

/**
 * nsobj_to_cam_event() - Get CAM event pointer from the associated CAM
 * object
 * @nsobj: pointer to CAM object that represents a CAM event
 *
 * Return: NULL on error or CAM event pointer otherwise.
 */
static struct cam_obj_event *nsobj_to_cam_event(struct cam_obj *nsobj)
{
	/* Should never happen */
	if (!cam_obj_check_type(nsobj, CAM_OBJ_TYPE_EVENT))
		return NULL;

	return container_of(nsobj, struct cam_obj_event, nsobj);
}

/**
 * cam_event_lookup() - Lookup CAM event by ID
 * @cam: pointer to CAM device
 * @event_id: ID of CAM event
 *
 * Return: NULL on error or CAM event pointer otherwise. Returned object is
 * valid and has incremented ref-counter, call cam_event_put() to properly
 * decrement ref-counter back.
 */
struct cam_obj_event *cam_event_lookup(struct cam_device *cam, u32 event_id)
{
	struct cam_obj *event = NULL;

	event = cam_obj_lookup(&cam->ns, CAM_OBJ_TYPE_EVENT, event_id);
	if (!event)
		return NULL;

	return nsobj_to_cam_event(event);
}
EXPORT_SYMBOL_GPL(cam_event_lookup);
ALLOW_ERROR_INJECTION(cam_event_lookup, NULL);

/**
 * cam_event_release() - The release function of CAM event
 * @nsobj: pointer to CAM object that represents a CAM event
 */
static void cam_event_release(struct cam_obj *nsobj)
{
	struct cam_obj_event *event = nsobj_to_cam_event(nsobj);

	cam_obj_unlink(nsobj);
	kfree(event);
}

/**
 * cam_event_activate_signal() - Activate a pending signal
 * @sig: pointer to CAM signal, the signal source should be a CAM event
 *
 * Return: True on success or false otherwise.
 */
bool cam_event_activate_signal(struct cam_op_signal *sig)
{
	struct cam_obj_event *event;
	unsigned long flags;

	event = nsobj_to_cam_event(sig->source);
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

void cam_event_deactivate_signal(struct cam_op_signal *sig)
{
	struct cam_op_signal *active;
	struct cam_obj_event *event;
	unsigned long flags;

	event = nsobj_to_cam_event(sig->source);
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
 * cam_event_trigger_signals() - Trigger and notify all active signals
 * depending on the provided event
 * @entity: pointer to CAM entity
 * @event: pointer to CAM event
 */
void cam_event_trigger_signals(struct cam_obj_entity *entity,
			       struct cam_obj_event *event)
{
	unsigned long flags;

	trace_cam_event_trigger(entity, NULL, event);
	write_lock_irqsave(&event->notify_lock, flags);
	cam_fire_active_signals(&event->notify_active_chain);
	write_unlock_irqrestore(&event->notify_lock, flags);
}
EXPORT_SYMBOL_GPL(cam_event_trigger_signals);

/**
 * cam_instance_event_trigger_signals() - Trigger and notify all active signals
 * depending on the provided event instance
 * @entity: pointer to CAM entity
 * @instance: entity instance (context)
 * @event: pointer to CAM event
 */
void cam_instance_event_trigger_signals(struct cam_obj_entity *entity,
					struct cam_obj_instance *instance,
					struct cam_obj_event *event)
{
	unsigned long flags;

	trace_cam_event_trigger(entity, instance, event);
	write_lock_irqsave(&event->notify_lock, flags);
	cam_instance_fire_active_signals(instance, &event->notify_active_chain);
	write_unlock_irqrestore(&event->notify_lock, flags);
}
EXPORT_SYMBOL_GPL(cam_instance_event_trigger_signals);

/**
 * cam_event_put() - Decrements ref-counter of the CAM event
 * @event: pointer to CAM event
 */
void cam_event_put(struct cam_obj_event *event)
{
	if (likely(event))
		cam_obj_put(&event->nsobj);
	else
		WARN_ON(1);
}
EXPORT_SYMBOL_GPL(cam_event_put);

/**
 * cam_event_id() - Return ID of the CAM event
 * @event: pointer to CAM event
 *
 * Return: Object ID.
 */
u32 cam_event_id(struct cam_obj_event *event)
{
	return cam_obj_id(&event->nsobj);
}
EXPORT_SYMBOL_GPL(cam_event_id);

/**
 * cam_event_unregister() - Unregister event (remove from namespace and
 * possibly release)
 * @event: pointer to CAM event
 */
void cam_event_unregister(struct cam_obj_event *event)
{
	/* @FIXME */
	WARN_ON(!list_empty(&event->notify_active_chain));
	cam_obj_remove(&event->nsobj);
	cam_obj_deinit(&event->nsobj);
}
EXPORT_SYMBOL_GPL(cam_event_unregister);

/**
 * cam_event_register() - Imports and registers (inserts into namespace) CAM
 * event
 * @cam: pointer to CAM device
 * @entity_id: ID of parent entity object
 * @namefmt: event name format string
 *
 * Return: NULL on error or CAM event pointer otherwise.
 */
__printf(3, 4)
struct cam_obj_event *cam_event_register(struct cam_device *cam,
					 u32 entity_id,
					 const char *namefmt,
					 ...)
{
	struct cam_obj_entity *entity;
	char name[CAM_EVENT_NAME_SZ];
	struct cam_obj_event *event;
	va_list args;

	lockdep_assert_held_write(&cam->ns_enum_lock);

	event = kzalloc(sizeof(*event), GFP_KERNEL);
	if (!event)
		return NULL;

	entity = cam_entity_lookup(cam, entity_id);
	if (!entity)
		goto error;

	va_start(args, namefmt);
	vsnprintf(name, sizeof(name), namefmt, args);
	va_end(args);

	strscpy(event->name, name, CAM_EVENT_NAME_SZ);
	INIT_LIST_HEAD(&event->notify_active_chain);
	rwlock_init(&event->notify_lock);
	cam_obj_init(&event->nsobj, CAM_OBJ_TYPE_EVENT, cam_event_release,
		     &cam->ns);

	if (cam_obj_link(&event->nsobj, &entity->nsobj))
		goto error;

	/* Link increments ref-counter of the object we link to */
	cam_entity_put(entity);

	if (cam_obj_insert(&event->nsobj))
		goto error;

	return event;

error:
	cam_entity_put(entity);
	cam_event_release(&event->nsobj);
	return NULL;
}
EXPORT_SYMBOL_GPL(cam_event_register);
ALLOW_ERROR_INJECTION(cam_event_register, NULL);

/**
 * enum_entity() - Entity enumeration callback
 * @nsobj: pointer to CAM object that represents a CAM entity
 * @ctl: auxiliary data
 *
 * This callback increases the entry counter of the output and, if necessary,
 * copy the entity name to the user space.
 *
 * This function may sleep.
 *
 * Return: True on success or false otherwise.
 */
static bool enum_entity(struct cam_obj *nsobj, struct cam_graph_walk *ctl)
{
	struct cam_query_entity_entry *qent;
	struct cam_obj_entity *entity;
	struct cam_koutput *output;

	output = ctl->data;

	/* User just want the size, not the data. */
	if (!cam_output_has_buffer(output))
		goto out;

	cam_output_next_entry(output, qent);
	if (!qent)
		return false;

	if (put_user(cam_obj_id(nsobj), &qent->id))
		return false;

	entity = nsobj_to_cam_entity(nsobj);
	if (copy_to_user(&qent->name, entity->name, strlen(entity->name)))
		return false;

	if (put_user(cam_obj_link_id(nsobj), &qent->parent))
		return false;

out:
	output->num_entries++;
	return true;
}

/**
 * enum_event() - Event enumeration callback
 * @nsobj: pointer to CAM object that represents a CAM event
 * @ctl: auxiliary data
 *
 * This callback increases the entry counter of the output and, if necessary,
 * copy the event name to the user space.
 *
 * This function may sleep.
 *
 * Return: True on success or false otherwise.
 */
static bool enum_event(struct cam_obj *nsobj, struct cam_graph_walk *ctl)
{
	struct cam_query_event_entry *qent;
	struct cam_obj_event *event;
	struct cam_koutput *output;

	output = ctl->data;

	/* User just want the size, not the data. */
	if (!cam_output_has_buffer(output))
		goto out;

	cam_output_next_entry(output, qent);
	if (!qent)
		return false;

	if (put_user(cam_obj_id(nsobj), &qent->id))
		return false;

	event = nsobj_to_cam_event(nsobj);
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
static u32 entity_depth_limit(struct cam_query_entities *query)
{
	return min_t(u32, query->maxdepth, CAM_GRAPH_STACK_DEPTH);
}

/**
 * cam_enum_entities() - Enumerate objects that belong to an entity
 * @cam: pointer to CAM device
 * @query: input query from user space
 * @output: output data to user space
 *
 * Return: 0 on success or error value otherwise.
 */
int cam_enum_entities(struct cam_device *cam,
		      struct cam_query_entities *query,
		      struct cam_koutput *output)
{
	struct cam_graph_walk ctl = {};
	struct cam_obj_entity *entity;
	size_t depth;
	int ret;

	query->num_entities = 0;

	entity = cam_entity_lookup(cam, query->id);
	if (!entity)
		return -ENOENT;

	ctl.data	= output;
	ctl.cb		= enum_entity;
	ctl.match_type	= CAM_OBJ_TYPE_ENTITY;
	ctl.match_id	= CAM_QUERY_ALL_OBJECTS;

	depth = entity_depth_limit(query);
	ret = cam_enum_graph_objects(&ctl, &entity->nsobj, depth);
	query->num_entities = output->num_entries;
	cam_entity_put(entity);
	return ret;
}

/**
 * cam_enum_events() - Enumerate objects that belong to an event
 * @cam: pointer to CAM device
 * @query: input query from user space
 * @output: output data to user space
 *
 * Return: 0 on success or error value otherwise.
 */
int cam_enum_events(struct cam_device *cam,
		    struct cam_query_events *query,
		    struct cam_koutput *output)
{
	struct cam_graph_walk ctl = {};
	struct cam_obj_entity *entity;
	int ret;

	query->num_events = 0;

	entity = cam_entity_lookup(cam, query->entity);
	if (!entity)
		return -ENOENT;

	ctl.data	= output;
	ctl.cb		= enum_event;
	ctl.match_type	= CAM_OBJ_TYPE_EVENT;
	/* query->id is either a specific object ID or CAM_QUERY_ALL_OBJECTS */
	ctl.match_id	= query->id;

	ret = cam_enum_graph_objects(&ctl, &entity->nsobj,
				     CAM_GRAPH_STACK_DEPTH);
	query->num_events = output->num_entries;
	cam_entity_put(entity);
	/*
	 * We queried a particular event on a particular entity but could not
	 * find it.
	 */
	if (ctl.match_id != CAM_QUERY_ALL_OBJECTS && !output->num_entries)
		ret = -ENOENT;
	return ret;
}

/**
 * cam_drain_instances() - Drains pipeline entities instances. This must be used
 * only from the pipeline (emergency) termination path.
 * @pipeline: pointer to CAM pipeline
 *
 * Return: 0 on success or negative error code otherwise.
 */
int cam_drain_instances(struct cam_pipeline *pipeline)
{
	struct cam_obj *nsobj;
	struct cam_obj *save;
	int ret;

	cam_ns_for_each_obj_safe(nsobj, save, &pipeline->objs) {
		if (cam_obj_type(nsobj) != CAM_OBJ_TYPE_INSTANCE)
			continue;

		ret = cam_obj_remove_id(&pipeline->objs,
					CAM_OBJ_TYPE_INSTANCE,
					cam_obj_id(nsobj));
		if (ret)
			return ret;
	}
	return 0;
}
