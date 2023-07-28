/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CAM entity
 *
 * Copyright (C) Google LLC
 */

#ifndef __LINUX_CAM_ENTITY_H__
#define __LINUX_CAM_ENTITY_H__

#include <linux/cam/cam-device.h>
#include <linux/cam/cam-namespace.h>
#include <linux/dma-buf.h>
#include <linux/workqueue.h>

struct cam_obj_entity;

#define CAM_ENTITY_MIN_INSTANCES	1
#define CAM_ENTITY_UNLIMITED_INSTANCES	INT_MAX

struct cam_obj_instance;

/**
 * cam_entity_ops - CAM entity operation list
 *
 * The available register read/write execution callback on an entity.
 */
struct cam_entity_ops {
	/**
	 * instance_read(): entity instance register read callback
	 *
	 * Return: 0 on success, or negative error code on failure
	 */
	int (*instance_read)(void *dev,
			     struct cam_obj_instance *instance,
			     struct cam_read_instruction *rw);
	/**
	 * instance_write(): entity instance register write callback
	 *
	 * Return: 0 on success, or negative error code on failure
	 */
	int (*instance_write)(void *dev,
			      struct cam_obj_instance *instance,
			      struct cam_write_instruction *rw);
	/**
	 * instance_create(): callback to create entity instance
	 *
	 * Return: pointer to instance or NULL
	 */
	void *(*instance_create)(void *dev);
	/**
	 * instance_destroy(): callback to destroy entity instance
	 */
	void (*instance_destroy)(void *dev, void *data);
	/**
	 * dmabuf_add(): callback to create device-specific DMA-buffer mapping
	 *
	 * Return: pointer to buffer mapping or NULL
	 */
	void *(*dmabuf_add)(void *dev, struct dma_buf *dma_buf);
	/**
	 * dmabuf_remove(): callback to destroy device-specific DMA-buffer
	 * mapping
	 */
	void (*dmabuf_remove)(void *dev, void *data, struct dma_buf *dma_buf);
};

/**
 * cam_obj_entity - CAM entity structure
 *
 * The structure that represents a specific entity.
 * An entity refers to a "node" in the CAM graph tree, which can be, for
 * example, a device, a bus, a sensor, csi-2 receiver or the graph root.
 */
struct cam_obj_entity {
	/** @nsobj: namespace object */
	struct cam_obj		nsobj;
	/** @ops: Read/Write execution callbacks */
	struct cam_entity_ops	*ops;
	/** @instances_avail: number of instances an entity can have */
	atomic_t		instances_avail;
	/** @flags: Entity flags */
	u32			flags;
	/** @driver_data: Driver specific data */
	void			*driver_data;
	/** @name: entity name */
	char			name[CAM_ENTITY_NAME_SZ];
};

/**
 * cam_obj_instance - CAM entity instance (execution context)
 *
 * This structure holds driver's execution context data (if any).
 */
struct cam_obj_instance {
	/** @nsobj: namespace object */
	struct cam_obj		nsobj;
	/** @driver_data: Driver specific data */
	void			*driver_data;
	/** @release_work: Deferred instance release */
	struct work_struct	release_work;
};

/**
 * cam_obj_event - The pipeline event structure
 *
 * Pipeline event that generates signals but is never executed on its own.
 */
struct cam_obj_event {
	/** @nsobj: namespace object */
	struct cam_obj		nsobj;
	/** @notify_lock: protects list of signals i.e. @notify_active_chain */
	rwlock_t		notify_lock;
	/** @notify_chain: list of pipeline objects that are blocked on us */
	struct list_head	notify_active_chain;
	/** @name: event name */
	char			name[CAM_EVENT_NAME_SZ];
};

/* For CAM (main) root entity only */
struct cam_obj_entity *cam_root_entity_register(struct cam_device *cam);

struct cam_obj_entity *cam_entity_register(struct cam_device *cam,
					   u32 parent_id,
					   void *driver_data,
					   struct cam_entity_ops *ops,
					   s32 num_instances,
					   const char *namefmt,
					   ...);
void cam_entity_unregister(struct cam_obj_entity *ce);

u32 cam_entity_id(struct cam_obj_entity *ce);

void *cam_entity_driver_data(struct cam_obj_entity *entity);

struct cam_obj_entity *cam_entity_lookup(struct cam_device *cam, u32 id);
bool cam_entity_get(struct cam_obj_entity *entity);
void cam_entity_put(struct cam_obj_entity *entity);

struct cam_obj_event *cam_event_register(struct cam_device *cam,
					 u32 entity_id,
					 const char *namefmt,
					 ...);
void cam_event_unregister(struct cam_obj_event *ce);

u32 cam_event_id(struct cam_obj_event *ce);

bool cam_event_activate_signal(struct cam_op_signal *sig);
void cam_event_deactivate_signal(struct cam_op_signal *sig);

void cam_event_trigger_signals(struct cam_obj_entity *entity,
			       struct cam_obj_event *event);

void cam_instance_event_trigger_signals(struct cam_obj_entity *entity,
					struct cam_obj_instance *instance,
					struct cam_obj_event *event);

struct cam_obj_event *cam_event_lookup(struct cam_device *cam, u32 event_id);
void cam_event_put(struct cam_obj_event *ce);

int cam_enum_entities(struct cam_device *cam,
		      struct cam_query_entities *query,
		      struct cam_koutput *output);

int cam_enum_events(struct cam_device *cam,
		    struct cam_query_events *query,
		    struct cam_koutput *output);

int cam_instance_destroy(struct cam_ns *ns, u32 id);
struct cam_obj_instance *cam_instance_create(struct cam_ns *ns,
					     struct cam_obj_entity *entity,
					     u32 id);
struct cam_obj_instance *cam_instance_lookup(struct cam_ns *ns, u32 id);
bool cam_instance_verify(struct cam_obj_entity *entity,
			 struct cam_obj_instance *instance);
bool cam_instance_get(struct cam_obj_instance *instance);
void cam_instance_put(struct cam_obj_instance *instance);

void *cam_instance_driver_data(struct cam_obj_instance *instance);

struct cam_pipeline;

int cam_drain_instances(struct cam_pipeline *pipeline);

#endif /* __LINUX_CAM_ENTITY_H__ */
