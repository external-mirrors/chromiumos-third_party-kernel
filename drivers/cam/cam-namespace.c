// SPDX-License-Identifier: GPL-2.0
/*
 * CAM objects namespace
 *
 * Copyright (C) 2022 Google LLC
 */

#define pr_fmt(fmt) "cam-namespace: " fmt

#include <linux/cam/cam-namespace.h>
#include <linux/cam/cam-device.h>
#include <uapi/linux/cam.h>
#include <linux/rcupdate.h>
#include <linux/kernel.h>
#include <linux/slab.h>

/* Set when object holds an allocated IDA ID */
#define CAM_OBJ_FLAG_ACTIVE		BIT(0)

#define CAM_NS_UNIQUE_ID_START		0x00000000UL
#define CAM_NS_UNIQUE_ID_END		0x01ffffffUL

/*
 * Namespace does not keep released objects. All objects are removed
 * from the namespace first before the ->release() call. ->release()
 * callback must take care of allocated resources and kfree() the
 * containing object (if needed).
 */
static void cam_obj_final_put(struct kref *kref)
{
	struct cam_obj *nsobj = container_of(kref, struct cam_obj, kref);

	/* This should kfree() the namespace object */
	nsobj->release(nsobj);
}

/**
 * cam_obj_put() - decrement object refcount
 * @nsobj: pointer to object.
 *
 * There is no corresponding cam_obj_get() method, all objects
 * should be looked up by their IDs and type.
 *
 * Return: 1 if it was final put and the object was scheduled for delayed
 * destruction, 0 otherwise.
 */
int cam_obj_put(struct cam_obj *nsobj)
{
	if (!nsobj)
		return 0;

	return kref_put(&nsobj->kref, cam_obj_final_put);
}

/**
 * cam_obj_insert() - Adds object to its namespace and assigns it an
 * automatically generated ID.
 * @nsobj: object to add
 *
 * Return: 0 on success, negative error code otherwise.
 */
int cam_obj_insert(struct cam_obj *nsobj)
{
	struct cam_ns *ns;
	int ret;
	u32 id;

	if (WARN_ON(!nsobj))
		return -EINVAL;

	ns = nsobj->ns;
	if (WARN_ON(!ns))
		return -EINVAL;

	if (WARN_ON(nsobj->flags & CAM_OBJ_FLAG_ACTIVE))
		return -EEXIST;

	/*
	 * Increment the refcount before we make the object
	 * publicly available.
	 */
	cam_obj_get(nsobj);

	/* The object is not ACTIVE yet */
	down_write(&ns->lock);
	list_add(&nsobj->list_entry, &ns->objs_list);
	up_write(&ns->lock);

	switch (ns->id_pol) {
	case CAM_NS_POL_UNIQUE_ID:
		ret = xa_alloc(&ns->objs_table, &id, nsobj,
			       XA_LIMIT(ns->next_id, CAM_NS_UNIQUE_ID_END),
			       GFP_KERNEL);
		if (!ret) {
			nsobj->id = id;
			ns->next_id++;
			if (ns->next_id > CAM_NS_UNIQUE_ID_END)
				ns->next_id = CAM_NS_UNIQUE_ID_START;
		}
		break;
	case CAM_NS_POL_USER_ID:
		ret = xa_insert(&ns->objs_table, cam_obj_id(nsobj), nsobj,
				GFP_KERNEL);
		/*
		 * Unify error codes for double insert case. See
		 * CAM_OBJ_FLAG_ACTIVE branch earlier.
		 */
		if (ret == -EBUSY)
			ret = -EEXIST;
		break;
	default:
		ret = -EINVAL;
	}

	if (!ret) {
		/* Mark object as ACTIVE */
		down_write(&ns->lock);
		nsobj->flags |= CAM_OBJ_FLAG_ACTIVE;
		up_write(&ns->lock);
		return 0;
	}

	down_write(&ns->lock);
	list_del_init(&nsobj->list_entry);
	up_write(&ns->lock);

	cam_obj_put(nsobj);
	return ret;
}
ALLOW_ERROR_INJECTION(cam_obj_insert, ERRNO);

/**
 * cam_obj_move() - Move an object to its namespace.
 * @nsobj: object to add
 * @id: id of the object
 *
 * Return: 0 on success, negative error code otherwise.
 * Once the object is moved to the namespace, its refcount decreases,
 * and therefore the caller to this function, shall not use its
 * reference.
 */
int cam_obj_move(struct cam_obj *nsobj, unsigned long *id)
{
	int ret;

	if (WARN_ON(!nsobj))
		return -EINVAL;

	ret = cam_obj_insert(nsobj);
	if (ret)
		return ret;

	if (id)
		*id = cam_obj_id(nsobj);

	cam_obj_put(nsobj);
	return 0;
}
ALLOW_ERROR_INJECTION(cam_obj_move, ERRNO);

/**
 * cam_obj_lookup() - Lookup object by ID and return a pointer to it if
 * found
 * @ns: namespace to lookup in
 * @type: type the object for sanity check
 * @id: lookup id
 *
 * Lookup ID may come from user space so we have to sanity check if type
 * of the found object and intended object type match, just to make sure
 * that intentions are aligned with the actions.
 *
 * Returned object has its refcount incremented.
 *
 * Return: Pointer to object, or NULL otherwise.
 */
struct cam_obj *cam_obj_lookup(struct cam_ns *ns,
			       enum cam_obj_type type,
			       unsigned long id)
{
	struct cam_obj *nsobj;

	/*
	 * It's not enough to just safely xa_load() the object as we need to
	 * test its type/etc. before we return the pointer. This requires
	 * guarantees that object is not released concurrently as long as
	 * cam_obj_lookup() access its flags. We can protect this entire
	 * function with RCU read side lock and queue_rcu_work() deferred
	 * cam_obj_final_put(), but locking the objs_table should work just
	 * fine.
	 */
	xa_lock(&ns->objs_table);
	nsobj = xa_load(&ns->objs_table, id);
	if (!nsobj)
		goto out;
	if (!(nsobj->flags & CAM_OBJ_FLAG_ACTIVE)) {
		nsobj = NULL;
		goto out;
	}
	if (!(nsobj->type & type)) {
		nsobj = NULL;
		goto out;
	}
	if (!kref_get_unless_zero(&nsobj->kref))
		nsobj = NULL;
out:
	xa_unlock(&ns->objs_table);
	return nsobj;
}

/**
 * cam_obj_remove() - Removed object from the namespace and decrements
 * its ref-counter
 * @nsobj: object to remove
 */
void cam_obj_remove(struct cam_obj *nsobj)
{
	struct cam_ns *ns = nsobj->ns;
	unsigned long id;

	if (WARN_ON(!ns))
		return;

	if (WARN_ON(!(nsobj->flags & CAM_OBJ_FLAG_ACTIVE)))
		return;

	id = cam_obj_id(nsobj);
	nsobj->flags &= ~CAM_OBJ_FLAG_ACTIVE;
	xa_erase(&ns->objs_table, id);

	down_write(&ns->lock);
	list_del_init(&nsobj->list_entry);
	up_write(&ns->lock);

	cam_obj_put(nsobj);
}

/**
 * cam_obj_remove_id() - Removes object from the namespace corresponding
 * to the given id and decrements its refcount
 * @ns: namespace to remove object from
 * @type: object type for sanity check
 * @id: object id
 *
 * Lookup ID may come from user space so we have to sanity check if type
 * of the found object and intended object type match, just to make sure
 * that intentions are aligned with the actions.
 *
 * Return: 0 if the object was removed and its refcount was decremented, or
 * negative error code otherwise.
 */
int cam_obj_remove_id(struct cam_ns *ns, enum cam_obj_type type,
		      unsigned long id)
{
	struct cam_obj *nsobj;

	if (WARN_ON(ns == NULL))
		return -EINVAL;

	nsobj = cam_obj_lookup(ns, type, id);
	if (!nsobj)
		return -ENOENT;

	nsobj->flags &= ~CAM_OBJ_FLAG_ACTIVE;
	xa_erase(&ns->objs_table, id);

	down_write(&ns->lock);
	list_del_init(&nsobj->list_entry);
	up_write(&ns->lock);

	/* Put additional cam_obj_lookup() refcount increment */
	cam_obj_put(nsobj);
	/* Put initial cam_obj_insert() refcount */
	cam_obj_put(nsobj);
	return 0;
}

/**
 * cam_obj_check_type() - Check whether cam_obj has the correct type
 * @nsobj: the namespace object
 * @type: the type that the object is expected to be
 *
 * Return: True if the type matches, or false otherwise.
 */
bool cam_obj_check_type(struct cam_obj *nsobj, enum cam_obj_type type)
{
	if (WARN_ON(nsobj == NULL))
		return false;
	if (WARN_ON(nsobj->type != type))
		return false;
	return true;
}

/**
 * cam_obj_get() - Increments refcount of a valid NS object.
 * @nsobj: namespace object.
 *
 * Return: NULL if the object's refcount was not incremented.
 */
struct cam_obj *cam_obj_get(struct cam_obj *nsobj)
{
	struct cam_ns *ns = nsobj->ns;

	if (WARN_ON(!ns))
		return NULL;

	if (!kref_get_unless_zero(&nsobj->kref))
		return NULL;
	return nsobj;
}

/**
 * cam_obj_id() - Return ID of the namespace object
 * @nsobj: namespace object
 *
 * Return: Object ID.
 */
unsigned long cam_obj_id(struct cam_obj *nsobj)
{
	WARN_ON((nsobj->ns->id_pol != CAM_NS_POL_USER_ID)
		&& !(nsobj->flags & CAM_OBJ_FLAG_ACTIVE));

	return nsobj->id;
}
EXPORT_SYMBOL_GPL(cam_obj_id);

enum cam_obj_type cam_obj_type(struct cam_obj *nsobj)
{
	WARN_ON(!(nsobj->flags & CAM_OBJ_FLAG_ACTIVE));
	return nsobj->type;
}

/**
 * cam_obj_set_id() - Set object ID
 * @nsobj: namespace object
 * @id: lookup id of the object
 *
 * Return: 0 if id was set, -EINVAL otherwise.
 */
int cam_obj_set_id(struct cam_obj *nsobj, unsigned long id)
{
	if (WARN_ON(nsobj->ns->id_pol != CAM_NS_POL_USER_ID))
		return -EINVAL;
	if (WARN_ON(nsobj->flags & CAM_OBJ_FLAG_ACTIVE))
		return -EINVAL;

	nsobj->id = id;
	return 0;
}

/**
 * cam_obj_init() - Initialise newly created namespace object
 * @nsobj: object to init
 * @type: type of the object
 * @release: clean-out callback, called when refcount goes to 0
 * @ns: namespace this object belongs to
 */
void cam_obj_init(struct cam_obj *nsobj,
		  enum cam_obj_type type,
		  void (*release)(struct cam_obj *nsobj),
		  struct cam_ns *ns)
{
	WARN_ON(!release);
	WARN_ON(!ns);

	memset(nsobj, 0, sizeof(*nsobj));
	kref_init(&nsobj->kref);
	nsobj->type = type;
	nsobj->release = release;
	nsobj->ns = ns;
	cam_graph_node_init(nsobj);
	INIT_LIST_HEAD(&nsobj->list_entry);
}

/**
 * cam_obj_deinit() - De-initialise namespace object
 * @nsobj: object to deinit
 */
void cam_obj_deinit(struct cam_obj *nsobj)
{
	if (WARN_ON(!nsobj))
		return;

	if (WARN_ON(kref_read(&nsobj->kref) != 1))
		return;

	cam_obj_put(nsobj);
}

/**
 * cam_ns_for_each() - Walks the namespace and calls callback on all active and
 * valid namespace objects
 * @ns: namespace to walk
 * @ctl: auxiliary data
 *
 * Callback will be called only on ACTIVE objects, with properly incremented
 * ref-counter.
 */
void cam_ns_for_each(struct cam_ns *ns, struct cam_ns_walk_control *ctl)
{
	struct cam_obj *nsobj;
	bool ret = false;

	if (WARN_ON(!ns))
		return;

	if (WARN_ON(!ctl))
		return;

	if (!ctl->cb)
		return;

	/**
	 * This iterates only over enumerate-able objects (not all
	 * namespace objects are enumerate-able), IOW the objects that
	 * are on the objs_list list with CAM_OBJ_FLAG_ACTIVE bit set.
	 */
	down_read(&ns->lock);
	list_for_each_entry(nsobj, &ns->objs_list, list_entry) {
		if (!kref_get_unless_zero(&nsobj->kref))
			continue;
		if (nsobj->flags & CAM_OBJ_FLAG_ACTIVE)
			ret = ctl->cb(nsobj, ctl);
		cam_obj_put(nsobj);
		if (ret)
			break;
	}
	up_read(&ns->lock);
}

/**
 * cam_ns_init() - Initialises namespace
 * @ns: namespace to initialise
 * @id_policy: ID allocation policy
 *
 * Return: 0 on success.
 */
int cam_ns_init(struct cam_ns *ns, enum cam_id_policy id_policy)
{
	memset(ns, 0, sizeof(*ns));
	if (id_policy != CAM_NS_POL_UNIQUE_ID &&
	    id_policy != CAM_NS_POL_USER_ID) {
		pr_err("Unknown namespace id policy: %u\n", id_policy);
		return -EINVAL;
	}

	xa_init(&ns->objs_table);
	xa_init_flags(&ns->objs_table, XA_FLAGS_ALLOC);
	ns->next_id	= CAM_NS_UNIQUE_ID_START;
	ns->id_pol	= id_policy;
	init_rwsem(&ns->lock);
	INIT_LIST_HEAD(&ns->objs_list);
	return 0;
}

/**
 * cam_ns_release() - Release namespace
 * @ns: namespace to release
 */
void cam_ns_release(struct cam_ns *ns)
{
	struct cam_obj *nsobj;
	unsigned long id;

	if (WARN_ON(!ns))
		return;

	down_write(&ns->lock);
	while (!list_empty(&ns->objs_list)) {
		nsobj = list_first_entry(&ns->objs_list,
					 struct cam_obj,
					 list_entry);

		list_del_init(&nsobj->list_entry);

		if (!(nsobj->flags & CAM_OBJ_FLAG_ACTIVE))
			continue;

		id = cam_obj_id(nsobj);
		nsobj->flags &= ~CAM_OBJ_FLAG_ACTIVE;
		xa_erase(&ns->objs_table, id);
		cam_obj_put(nsobj);
	}
	up_write(&ns->lock);

	WARN_ON(!xa_empty(&ns->objs_table));
	xa_destroy(&ns->objs_table);
}

#ifdef CONFIG_CAM_KUNIT_TESTS
#include "cam-namespace-test.c"
#endif
