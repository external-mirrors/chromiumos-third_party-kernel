/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CAM file handle namespace
 *
 * Copyright (C) 2022 Google LLC
 */

#ifndef __LINUX_CAM_NAMESPACE_H__
#define __LINUX_CAM_NAMESPACE_H__

#include <linux/cam/cam-graph.h>
#include <linux/rwsem.h>
#include <linux/types.h>
#include <linux/kref.h>
#include <linux/xarray.h>

#include <uapi/linux/cam.h>

/**
 * Namespaces map object IDs to object pointers (CAM objects).
 *
 * A namespace consists of:
 * 1) IDA which tracks allocated IDs
 * 2) Hash table that associates allocated IDs to CAM objects
 *
 * All namespace objects should embed &struct cam_obj, which holds the
 * internal data associated with the each namespace objects, e.g. reference
 * counter, object type, and so on. All objects are ref-counted and when
 * the object's reference counter reaches 0 object's release() function,
 * which is provided by the namespace user, is invoked for cleanup, which
 * also includes kfree() for objects that have been dynamically allocated,
 * since namespace does not kfree() released objects, it only holds the
 * ID:pointer mapping.
 *
 * Each object should be initialized by invoking cam_obj_init() on it
 * before it can be added to the namespace. To add object to a namespace
 * invoke cam_obj_insert(), which will allocate unused ID and associate it
 * with the object. Once added objects should be looked up by this ID.
 * Lookup always returns alive object with incremented reference counter.
 * Hence when the user doesn't need that object anymore cam_obj_put()
 * shall be called on it, in order to decrement the reference counter.
 *
 * In order to remove object from namespace, call cam_obj_remove(),
 * which removed the object form the hash table and puts object's reference
 * counter. Note that at this point there can be active users of that object,
 * so cam_obj_remove() does not necessarily invokes user provided
 * release on that object. It's the last cam_obj_put() on that object
 * that triggers the object release/cleanup. There is also cam_obj_deinit()
 * function which does final object put and, unlike cam_obj_put(), checks
 * that object ref counter is 1 before at the time of invocation.
 */

struct cam_obj;

/**
 * enum cam_obj_type - CAM object types
 *
 * @CAM_OBJ_TYPE_ENTITY:	Entity
 * @CAM_OBJ_TYPE_INSTANCE:	Entity instance (context)
 * @CAM_OBJ_TYPE_EVENT:		Event
 * @CAM_OBJ_TYPE_OPERATION:	Operation
 * @CAM_OBJ_TYPE_BUFFER:	Buffer
 * @CAM_OBJ_TYPE_IN_SYNCFILE:	Imported (in) sync file
 * @CAM_OBJ_TYPE_OUT_SYNCFILE:	Exported (out) sync file
 * @CAM_OBJ_TYPE_ROOT:		Root node
 */
enum cam_obj_type {
	CAM_OBJ_TYPE_ENTITY		= BIT(0),
	CAM_OBJ_TYPE_INSTANCE		= BIT(1),
	CAM_OBJ_TYPE_EVENT		= BIT(2),
	CAM_OBJ_TYPE_OPERATION		= BIT(3),
	CAM_OBJ_TYPE_BUFFER		= BIT(4),
	CAM_OBJ_TYPE_IN_SYNCFILE	= BIT(5),
	CAM_OBJ_TYPE_OUT_SYNCFILE	= BIT(6),
	CAM_OBJ_TYPE_ROOT		= BIT(12),
};

/**
 * enum cam_ns_policy - ID allocation policy
 *
 * @CAM_NS_POL_REUSE_ID:	Reuses released IDs immediately
 * @CAM_NS_POL_USER_ID:		Use user-supplied ID
 */
enum cam_id_policy {
	CAM_NS_POL_UNIQUE_ID	= BIT(0),
	CAM_NS_POL_USER_ID	= BIT(1),
};

/*
 * CAM pipeline objects namespace has USER_ID policy. To avoid ID
 * conflicts (e.g. between DMA buffers and entity instances objects)
 * we split the ID range (transparently for user-space).
 */
#define CAM_OBJS_NS_BUFFER_ID_START	0x00000000UL
#define CAM_OBJS_NS_BUFFER_ID_END	0x0001ffffUL
#define CAM_OBJS_NS_INSTANCE_ID_START	0x00020000UL
#define CAM_OBJS_NS_INSTANCE_ID_END	0x0003ffffUL

/**
 * cam_ns - CAM file handle namespace
 */
struct cam_ns {
	/** @objs: XArray to lookup namespace objects in */
	struct xarray		objs_table;
	/** @lock: objs_list lock */
	struct rw_semaphore	lock;
	/** @objs_list: List of namespace objects */
	struct list_head	objs_list;
	/** @id_pol: ID Allocation policy */
	enum cam_id_policy	id_pol;
	/** @next_id: Next id to be used for unique ids */
	u32			next_id;
};

/**
 * cam_ns_walk_control - CAM namespace walk control
 */
struct cam_ns_walk_control {
	/** @data: pointer to auxiliary data */
	void		*data;
	/** @flags: auxiliary flags */
	u64		flags;
	/** @cb: callback operation to the (matched) namespace objects */
	bool (*cb)(struct cam_obj *nsobj, struct cam_ns_walk_control *ctl);
};

void cam_ns_for_each(struct cam_ns *ns, struct cam_ns_walk_control *ctl);
int cam_ns_init(struct cam_ns *ns, enum cam_id_policy id_policy);
void cam_ns_release(struct cam_ns *ns);

/**
 * cam_obj - Base CAM object
 *
 * Rules of CAM graph objects:
 *
 * - Every user has to use cam_obj_get to get a reference to the object and put
 *   it using cam_obj_put
 * - Object lifetime is managed by embedded kref in the object
 */
struct cam_obj {
	/** @node: rhash table entry */
	struct rhash_head	node;
	/** @id: lookup ID of this object */
	unsigned long		id;
	/** @type: object type */
	enum cam_obj_type	type;
	/** @flags: flags associated with this object */
	unsigned int		flags;
	/** @kref: refcounter of this object */
	struct kref		kref;
	/** @release: pointer to a release function for this object type */
	void			(*release)(struct cam_obj *nsobj);
	/** @gnode: graph node of this object */
	struct cam_graph_node	gnode;
	/** @ns: pointer to the namespace this object belongs to */
	struct cam_ns		*ns;
	/** @list_entry: Object's list entry in namespace objects list */
	struct list_head	list_entry;
};

unsigned long cam_obj_id(struct cam_obj *nsobj);
enum cam_obj_type cam_obj_type(struct cam_obj *nsobj);
int cam_obj_set_id(struct cam_obj *nsobj, unsigned long id);

void cam_obj_init(struct cam_obj *nsobj,
		  enum cam_obj_type type,
		  void (*release)(struct cam_obj  *nsobj),
		  struct cam_ns *ns);
void cam_obj_deinit(struct cam_obj *nsobj);
int cam_obj_put(struct cam_obj *nsobj);

struct cam_obj *cam_obj_get(struct cam_obj *nsobj);

int cam_obj_insert(struct cam_obj *nsobj);
int cam_obj_move(struct cam_obj *nsobj, unsigned long *id);

void cam_obj_remove(struct cam_obj *nsobj);
int cam_obj_remove_id(struct cam_ns *ns, enum cam_obj_type type,
		      unsigned long id);

struct cam_obj *cam_obj_lookup(struct cam_ns *ns,
			       enum cam_obj_type type,
			       unsigned long id);

bool cam_obj_check_type(struct cam_obj *nsobj, enum cam_obj_type type);

#endif /* __LINUX_CAM_NAMESPACE_H__ */
