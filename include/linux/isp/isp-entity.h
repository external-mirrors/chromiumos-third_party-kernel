/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ISP entity
 *
 * Copyright (C) Google LLC
 */

#ifndef __LINUX_ISP_ENTITY_H__
#define __LINUX_ISP_ENTITY_H__

#include <linux/isp/isp-device.h>
#include <linux/isp/isp-namespace.h>
#include <linux/dma-buf.h>
#include <linux/workqueue.h>

struct isp_obj_entity;

#define ISP_ENTITY_MIN_INSTANCES	1
#define ISP_ENTITY_UNLIMITED_INSTANCES	INT_MAX

struct isp_obj_instance;

/**
 * isp_entity_ops - ISP entity operation list
 *
 * The available register read/write execution callback on an entity.
 */
struct isp_entity_ops {
	/**
	 * instance_read(): entity instance register read callback
	 *
	 * Return: 0 on success, or negative error code on failure
	 */
	int (*instance_read)(void *dev,
			     struct isp_obj_instance *instance,
			     struct isp_read_instruction *rw);
	/**
	 * instance_write(): entity instance register write callback
	 *
	 * Return: 0 on success, or negative error code on failure
	 */
	int (*instance_write)(void *dev,
			      struct isp_obj_instance *instance,
			      struct isp_write_instruction *rw);
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
 * isp_obj_entity - ISP entity structure
 *
 * The structure that represents a specific entity.
 * An entity refers to a "node" in the ISP graph tree, which can be, for
 * example, a device, a bus, a sensor, csi-2 receiver or the graph root.
 */
struct isp_obj_entity {
	/** @nsobj: namespace object */
	struct isp_obj		nsobj;
	/** @ops: Read/Write execution callbacks */
	struct isp_entity_ops	*ops;
	/** @instances_avail: number of instances an entity can have */
	atomic_t		instances_avail;
	/** @flags: Entity flags */
	u32			flags;
	/** @driver_data: Driver specific data */
	void			*driver_data;
	/** @name: entity name */
	char			name[ISP_ENTITY_NAME_SZ];
};

/**
 * isp_obj_instance - ISP entity instance (execution context)
 *
 * This structure holds driver's execution context data (if any).
 */
struct isp_obj_instance {
	/** @nsobj: namespace object */
	struct isp_obj		nsobj;
	/** @driver_data: Driver specific data */
	void			*driver_data;
	/** @release_work: Deferred instance release */
	struct work_struct	release_work;
};

/**
 * isp_obj_event - The pipeline event structure
 *
 * Pipeline event that generates signals but is never executed on its own.
 */
struct isp_obj_event {
	/** @nsobj: namespace object */
	struct isp_obj		nsobj;
	/** @notify_lock: protects list of signals i.e. @notify_active_chain */
	rwlock_t		notify_lock;
	/** @notify_chain: list of pipeline objects that are blocked on us */
	struct list_head	notify_active_chain;
	/** @name: event name */
	char			name[ISP_EVENT_NAME_SZ];
};

/* For ISP (main) root entity only */
struct isp_obj_entity *isp_root_entity_register(struct isp_device *isp);

struct isp_obj_entity *isp_entity_register(struct isp_device *isp,
					   u32 parent_id,
					   void *driver_data,
					   struct isp_entity_ops *ops,
					   s32 num_instances,
					   const char *namefmt,
					   ...);
void isp_entity_unregister(struct isp_obj_entity *ce);

u32 isp_entity_id(struct isp_obj_entity *ce);

void *isp_entity_driver_data(struct isp_obj_entity *entity);

struct isp_obj_entity *isp_entity_lookup(struct isp_device *isp, u32 id);
bool isp_entity_get(struct isp_obj_entity *entity);
void isp_entity_put(struct isp_obj_entity *entity);

struct isp_obj_event *isp_event_register(struct isp_device *isp,
					 u32 entity_id,
					 const char *namefmt,
					 ...);
void isp_event_unregister(struct isp_obj_event *ce);

u32 isp_event_id(struct isp_obj_event *ce);

bool isp_event_activate_signal(struct isp_op_signal *sig);
void isp_event_deactivate_signal(struct isp_op_signal *sig);

void isp_event_trigger_signals(struct isp_obj_entity *entity,
			       struct isp_obj_event *event);

void isp_instance_event_trigger_signals(struct isp_obj_entity *entity,
					struct isp_obj_instance *instance,
					struct isp_obj_event *event);

struct isp_obj_event *isp_event_lookup(struct isp_device *isp, u32 event_id);
void isp_event_put(struct isp_obj_event *ce);

int isp_enum_entities(struct isp_device *isp,
		      struct isp_query_entities *query,
		      struct isp_koutput *output);

int isp_enum_events(struct isp_device *isp,
		    struct isp_query_events *query,
		    struct isp_koutput *output);

int isp_instance_destroy(struct isp_ns *ns, u32 id);
struct isp_obj_instance *isp_instance_create(struct isp_ns *ns,
					     struct isp_obj_entity *entity,
					     u32 id);
struct isp_obj_instance *isp_instance_lookup(struct isp_ns *ns, u32 id);
bool isp_instance_verify(struct isp_obj_entity *entity,
			 struct isp_obj_instance *instance);
bool isp_instance_get(struct isp_obj_instance *instance);
void isp_instance_put(struct isp_obj_instance *instance);

void *isp_instance_driver_data(struct isp_obj_instance *instance);

struct isp_pipeline;

int isp_drain_instances(struct isp_pipeline *pipeline);

#endif /* __LINUX_ISP_ENTITY_H__ */
