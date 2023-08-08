/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ISP file handle namespace
 *
 * Copyright (C) Google LLC
 */

#ifndef __LINUX_ISP_NAMESPACE_H__
#define __LINUX_ISP_NAMESPACE_H__

#include <linux/isp/isp-graph.h>
#include <linux/rwsem.h>
#include <linux/types.h>
#include <linux/kref.h>
#include <linux/xarray.h>

#include <uapi/linux/isp.h>

/**
 * Namespaces map object IDs to object pointers (ISP objects).
 *
 * A namespace consists of:
 * 1) IDA which tracks allocated IDs
 * 2) Hash table that associates allocated IDs to ISP objects
 *
 * All namespace objects should embed &struct isp_obj, which holds the
 * internal data associated with the each namespace objects, e.g. reference
 * counter, object type, and so on. All objects are ref-counted and when
 * the object's reference counter reaches 0 object's release() function,
 * which is provided by the namespace user, is invoked for cleanup, which
 * also includes kfree() for objects that have been dynamically allocated,
 * since namespace does not kfree() released objects, it only holds the
 * ID:pointer mapping.
 *
 * Each object should be initialized by invoking isp_obj_init() on it
 * before it can be added to the namespace. To add object to a namespace
 * invoke isp_obj_insert(), which will allocate unused ID and associate it
 * with the object. Once added objects should be looked up by this ID.
 * Lookup always returns alive object with incremented reference counter.
 * Hence when the user doesn't need that object anymore isp_obj_put()
 * shall be called on it, in order to decrement the reference counter.
 *
 * In order to remove object from namespace, call isp_obj_remove(),
 * which removed the object form the hash table and puts object's reference
 * counter. Note that at this point there can be active users of that object,
 * so isp_obj_remove() does not necessarily invokes user provided
 * release on that object. It's the last isp_obj_put() on that object
 * that triggers the object release/cleanup. There is also isp_obj_deinit()
 * function which does final object put and, unlike isp_obj_put(), checks
 * that object ref counter is 1 before at the time of invocation.
 */

struct isp_obj;

/**
 * enum isp_obj_type - ISP object types
 *
 * @ISP_OBJ_TYPE_ENTITY:	Entity
 * @ISP_OBJ_TYPE_INSTANCE:	Entity instance (context)
 * @ISP_OBJ_TYPE_EVENT:		Event
 * @ISP_OBJ_TYPE_OPERATION:	Operation
 * @ISP_OBJ_TYPE_BUFFER:	Buffer
 * @ISP_OBJ_TYPE_IN_SYNCFILE:	Imported (in) sync file
 * @ISP_OBJ_TYPE_OUT_SYNCFILE:	Exported (out) sync file
 * @ISP_OBJ_TYPE_ROOT:		Root node
 */
enum isp_obj_type {
	ISP_OBJ_TYPE_ENTITY		= BIT(0),
	ISP_OBJ_TYPE_INSTANCE		= BIT(1),
	ISP_OBJ_TYPE_EVENT		= BIT(2),
	ISP_OBJ_TYPE_OPERATION		= BIT(3),
	ISP_OBJ_TYPE_BUFFER		= BIT(4),
	ISP_OBJ_TYPE_IN_SYNCFILE	= BIT(5),
	ISP_OBJ_TYPE_OUT_SYNCFILE	= BIT(6),
	ISP_OBJ_TYPE_ROOT		= BIT(12),
};

/**
 * enum isp_ns_policy - ID allocation policy
 *
 * @ISP_NS_POL_REUSE_ID:	Reuses released IDs immediately
 * @ISP_NS_POL_USER_ID:		Use user-supplied ID
 */
enum isp_id_policy {
	ISP_NS_POL_UNIQUE_ID	= BIT(0),
	ISP_NS_POL_USER_ID	= BIT(1),
};

/*
 * ISP pipeline objects namespace has USER_ID policy. To avoid ID
 * conflicts (e.g. between DMA buffers and entity instances objects)
 * we split the ID range (transparently for user-space).
 */
#define ISP_OBJS_NS_BUFFER_ID_START	0x00000000UL
#define ISP_OBJS_NS_BUFFER_ID_END	0x0001ffffUL
#define ISP_OBJS_NS_INSTANCE_ID_START	0x00020000UL
#define ISP_OBJS_NS_INSTANCE_ID_END	0x0003ffffUL

/**
 * isp_ns - ISP file handle namespace
 */
struct isp_ns {
	/** @objs: XArray to lookup namespace objects in */
	struct xarray		objs_table;
	/** @lock: objs_list lock */
	struct rw_semaphore	lock;
	/** @objs_list: List of namespace objects */
	struct list_head	objs_list;
	/** @id_pol: ID Allocation policy */
	enum isp_id_policy	id_pol;
	/** @next_id: Next id to be used for unique ids */
	u32			next_id;
};

/**
 * This does not take the namespace lock, so should be used with caution
 */
#define isp_ns_for_each_obj_safe(obj, tmp, ns)			\
	list_for_each_entry_safe((obj), (tmp), &(ns)->objs_list, list_entry)

/**
 * isp_ns_walk_control - ISP namespace walk control
 */
struct isp_ns_walk_control {
	/** @data: pointer to auxiliary data */
	void		*data;
	/** @flags: auxiliary flags */
	u64		flags;
	/** @cb: callback operation to the (matched) namespace objects */
	bool (*cb)(struct isp_obj *nsobj, struct isp_ns_walk_control *ctl);
};

void isp_ns_for_each(struct isp_ns *ns, struct isp_ns_walk_control *ctl);
int isp_ns_init(struct isp_ns *ns, enum isp_id_policy id_policy);
void isp_ns_release(struct isp_ns *ns);

/**
 * isp_obj - Base ISP object
 *
 * Rules of ISP graph objects:
 *
 * - Every user has to use isp_obj_get to get a reference to the object and put
 *   it using isp_obj_put
 * - Object lifetime is managed by embedded kref in the object
 */
struct isp_obj {
	/** @id: lookup ID of this object */
	unsigned long		id;
	/** @type: object type */
	enum isp_obj_type	type;
	/** @flags: flags associated with this object */
	unsigned int		flags;
	/** @kref: refcounter of this object */
	struct kref		kref;
	/** @release: pointer to a release function for this object type */
	void			(*release)(struct isp_obj *nsobj);
	/** @gnode: graph node of this object */
	struct isp_graph_node	gnode;
	/** @ns: pointer to the namespace this object belongs to */
	struct isp_ns		*ns;
	/** @list_entry: Object's list entry in namespace objects list */
	struct list_head	list_entry;
};

unsigned long isp_obj_id(struct isp_obj *nsobj);
enum isp_obj_type isp_obj_type(struct isp_obj *nsobj);
int isp_obj_set_id(struct isp_obj *nsobj, unsigned long id);

void isp_obj_init(struct isp_obj *nsobj,
		  enum isp_obj_type type,
		  void (*release)(struct isp_obj  *nsobj),
		  struct isp_ns *ns);
void isp_obj_deinit(struct isp_obj *nsobj);
int isp_obj_put(struct isp_obj *nsobj);

struct isp_obj *isp_obj_get(struct isp_obj *nsobj);

int isp_obj_insert(struct isp_obj *nsobj);
int isp_obj_move(struct isp_obj *nsobj, unsigned long *id);

void isp_obj_remove(struct isp_obj *nsobj);
int isp_obj_remove_id(struct isp_ns *ns, enum isp_obj_type type,
		      unsigned long id);

struct isp_obj *isp_obj_lookup(struct isp_ns *ns,
			       enum isp_obj_type type,
			       unsigned long id);

bool isp_obj_check_type(struct isp_obj *nsobj, enum isp_obj_type type);

#endif /* __LINUX_ISP_NAMESPACE_H__ */
