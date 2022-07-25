/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CAM entity/events
 *
 * Copyright (C) 2022 Google LLC
 */

#define pr_fmt(fmt) "cam-entity: " fmt

#include <linux/cam/cam-device.h>
#include <linux/cam/cam-entity.h>
#include <linux/cam/cam-graph.h>
#include <linux/cam/cam-output.h>
#include <linux/cam/cam-pipeline.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/rculist.h>
#include <linux/slab.h>

#include <trace/events/cam.h>

static int root_entity_read(struct cam_obj_entity *entity,
			    struct cam_read_instruction *rw)
{
	WARN_ON(1);
	return -EINVAL;
}

static int root_entity_write(struct cam_obj_entity *entity,
			     struct cam_write_instruction *rw)
{
	WARN_ON(1);
	return -EINVAL;
}

static struct cam_entity_ops root_entity_ops = {
	.read		= root_entity_read,
	.write		= root_entity_write,
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

	cam_graph_node_unlink(nsobj);
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
 * @namefmt: entity name format string
 *
 * Return: NULL on error or CAM entity pointer otherwise.
 */
__printf(5, 6)
struct cam_obj_entity *cam_entity_register(struct cam_device *cam,
					   u32 parent_id,
					   void *driver_data,
					   struct cam_entity_ops *ops,
					   const char *namefmt,
					   ...)
{
	char name[CAM_ENTITY_NAME_SZ];
	struct cam_obj_entity *entity;
	va_list args;

	if (WARN_ON(!ops))
		return NULL;

	if (!ops->read)
		ops->read = root_entity_read;
	if (!ops->write)
		ops->write = root_entity_write;

	entity = kzalloc(sizeof(*entity), GFP_KERNEL);
	if (!entity)
		return NULL;

	va_start(args, namefmt);
	vsnprintf(name, sizeof(name), namefmt, args);
	va_end(args);

	entity->ops = ops;
	entity->driver_data = driver_data;
	strlcpy(entity->name, name, CAM_ENTITY_NAME_SZ);
	cam_obj_init(&entity->nsobj, CAM_OBJ_TYPE_ENTITY, cam_entity_release,
		     &cam->ns);

	if (cam_graph_node_link(cam, &entity->nsobj, parent_id))
		goto error;

	if (cam_obj_insert(&entity->nsobj))
		goto error;

	return entity;

error:
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

	entity->ops = &root_entity_ops;
	strlcpy(entity->name, "CAM root entity", CAM_ENTITY_NAME_SZ);
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

	cam_graph_node_unlink(nsobj);
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

	trace_cam_event_trigger(entity, event);
	write_lock_irqsave(&event->notify_lock, flags);
	cam_fire_active_signals(&event->notify_active_chain);
	write_unlock_irqrestore(&event->notify_lock, flags);
}
EXPORT_SYMBOL_GPL(cam_event_trigger_signals);

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
	char name[CAM_EVENT_NAME_SZ];
	struct cam_obj_event *event;
	va_list args;

	event = kzalloc(sizeof(*event), GFP_KERNEL);
	if (!event)
		return NULL;

	va_start(args, namefmt);
	vsnprintf(name, sizeof(name), namefmt, args);
	va_end(args);

	strlcpy(event->name, name, CAM_EVENT_NAME_SZ);
	INIT_LIST_HEAD(&event->notify_active_chain);
	rwlock_init(&event->notify_lock);
	cam_obj_init(&event->nsobj, CAM_OBJ_TYPE_EVENT, cam_event_release,
		     &cam->ns);

	if (cam_graph_node_link(cam, &event->nsobj, entity_id))
		goto error;

	if (cam_obj_insert(&event->nsobj))
		goto error;

	return event;

error:
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

	if (put_user(cam_graph_node_link_id(nsobj), &qent->parent))
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
	return min((u32)query->maxdepth, (u32)CAM_GRAPH_STACK_DEPTH);
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
 * cam_drain_event_callback() - Callback to exhaust all the active event signals
 * @nsobj: pointer to CAM object that represents a CAM event
 * @ctl: auxiliary data
 */
static void cam_drain_event_callback(struct cam_obj *nsobj,
				     struct cam_ns_walk_control *ctl)
{
	struct cam_obj_event *event;
	unsigned long flags;

	if (!(nsobj->type & CAM_OBJ_TYPE_EVENT))
		return;

	event = nsobj_to_cam_event(nsobj);
	if (WARN_ON(!event))
		return;

	write_lock_irqsave(&event->notify_lock, flags);
	cam_drain_active_signals(&event->notify_active_chain);
	write_unlock_irqrestore(&event->notify_lock, flags);
}

/**
 * cam_drain_events() - Exhaust all active event signals under a CAM device.
 * @cam: pointer to CAM device
 *
 * Return: 0 on success.
 */
int cam_drain_events(struct cam_device *cam)
{
	struct cam_ns_walk_control ctl = {};

	ctl.cb		= cam_drain_event_callback;
	cam_ns_for_each(&cam->ns, &ctl);
	return 0;
}
