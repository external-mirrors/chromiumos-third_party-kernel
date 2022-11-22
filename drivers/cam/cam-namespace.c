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

/*
 * Statically defined object ID ranges for different types of namespaces.
 * This is useful for several things:
 *  1) different namespaces can have different ID allocation policies:
 *     either re-use freed IDs immediately or always allocate 'next' ID
 *     and re-use IDs only if the counter wraps around
 *  2) as a quick way to distinguish user-space created objects and CAM
 *     internal objects
 */
#define CAM_NS_UNIQUE_ID_START		0x00000000UL
#define CAM_NS_UNIQUE_ID_END		0x0001ffffUL
#define CAM_NS_REUSE_ID_START		0x01000000UL
#define CAM_NS_REUSE_ID_END		0x01ffffffUL

/* Set when object holds an allocated IDA ID */
#define CAM_OBJ_FLAG_ACTIVE		BIT(0)

static const struct rhashtable_params nsobj_params = {
	.key_offset		= offsetof(struct cam_obj, id),
	.head_offset		= offsetof(struct cam_obj, node),
	.key_len		= sizeof(unsigned long),
	.automatic_shrinking	= true,
};

static int id_ops_alloc_unique_id(struct cam_ns *ns)
{
	int ret;

	ret = ida_alloc_range(&ns->ids, ns->next_id,
			      CAM_NS_UNIQUE_ID_END, GFP_KERNEL);
	if (ret == -ENOSPC) {
		ns->next_id = CAM_NS_UNIQUE_ID_START;
		ret = ida_alloc_range(&ns->ids, ns->next_id,
				      CAM_NS_UNIQUE_ID_END, GFP_KERNEL);
	}

	if (ret == CAM_NS_UNIQUE_ID_END)
		ns->next_id = CAM_NS_UNIQUE_ID_START;
	else if (ret >= 0)
		ns->next_id = ret + 1;

	return ret;
}

static int id_create(struct cam_obj *nsobj)
{
	struct cam_ns *ns = nsobj->ns;
	int ret;

	if (WARN_ON(in_atomic()))
		return -EINVAL;

	switch (ns->id_pol) {
	case CAM_NS_POL_UNIQUE_ID:
		ret = id_ops_alloc_unique_id(ns);
		break;
	case CAM_NS_POL_REUSE_ID:
		ret = ida_alloc_range(&ns->ids, CAM_NS_REUSE_ID_START,
				      CAM_NS_REUSE_ID_END, GFP_KERNEL);
		break;
	case CAM_NS_POL_USER_ID:
		ret = cam_obj_id(nsobj);
		break;
	}

	if (ret < 0)
		return ret;

	nsobj->flags |= CAM_OBJ_FLAG_ACTIVE;
	nsobj->id = ret;
	return 0;
}

static void id_ops_release_unique_id(struct cam_obj *nsobj)
{
	struct cam_ns *ns = nsobj->ns;
	int id = cam_obj_id(nsobj);

	ida_free(&ns->ids, id);
}

static void id_release(struct cam_obj *nsobj)
{
	struct cam_ns *ns = nsobj->ns;

	if (!(nsobj->flags & CAM_OBJ_FLAG_ACTIVE))
		return;

	switch (ns->id_pol) {
	case CAM_NS_POL_UNIQUE_ID:
		id_ops_release_unique_id(nsobj);
		break;
	case CAM_NS_POL_REUSE_ID:
		ida_free(&ns->ids, cam_obj_id(nsobj));
		break;
	case CAM_NS_POL_USER_ID:
		break;
	}

	nsobj->flags &= ~CAM_OBJ_FLAG_ACTIVE;
}

/*
 * Namespace does not keep released objects. All objects are removed
 * from the namespace first before the ->release() call. ->release()
 * callback must take care of allocated resources and kfree() the
 * containing object (if needed).
 */
static void cam_obj_final_put(struct kref *kref)
{
	struct cam_obj *nsobj = container_of(kref, struct cam_obj, kref);

	id_release(nsobj);
	/* This should kfree() the namespace object */
	nsobj->release(nsobj);
}

/**
 * cam_obj_put() - decrement object refcounter
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

	if (WARN_ON(!nsobj))
		return -EINVAL;

	ns = nsobj->ns;
	if (WARN_ON(!ns))
		return -EINVAL;

	if (WARN_ON(nsobj->flags & CAM_OBJ_FLAG_ACTIVE))
		return -EEXIST;

	ret = id_create(nsobj);
	if (ret < 0)
		return ret;

	/*
	 * Increment the refcount before we make the object
	 * publicly available.
	 */
	cam_obj_get(nsobj);

	ret = rhashtable_lookup_insert_fast(&ns->objs,
					    &nsobj->node,
					    nsobj_params);
	if (!ret)
		return 0;

	cam_obj_put(nsobj);
	id_release(nsobj);

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

static struct cam_obj *__cam_obj_lookup(struct cam_ns *ns,
					enum cam_obj_type type,
					unsigned long id)
{
	struct cam_obj *nsobj;

	/*
	 * Note that this function is internal and it does not increment
	 * object's refcounter. The caller of this function should do it
	 * when needed.
	 */
	nsobj = rhashtable_lookup(&ns->objs, &id, nsobj_params);
	if (!nsobj)
		return NULL;
	if (!(nsobj->flags & CAM_OBJ_FLAG_ACTIVE))
		return NULL;
	if (!(nsobj->type & type))
		return NULL;

	return nsobj;
}

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
 * Returned object has its refcounter incremented.
 *
 * Return: Pointer to object, or NULL otherwise.
 */
struct cam_obj *cam_obj_lookup(struct cam_ns *ns,
			       enum cam_obj_type type,
			       unsigned long id)
{
	struct cam_obj *nsobj;

	rcu_read_lock();
	nsobj = __cam_obj_lookup(ns, type, id);
	if (nsobj && !kref_get_unless_zero(&nsobj->kref))
		nsobj = NULL;
	rcu_read_unlock();

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

	if (WARN_ON(!ns))
		return;

	if (WARN_ON(!(nsobj->flags & CAM_OBJ_FLAG_ACTIVE)))
		return;

	rhashtable_remove_fast(&ns->objs, &nsobj->node, nsobj_params);
	synchronize_rcu();

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
	int ret;

	if (WARN_ON(ns == NULL))
		return -EINVAL;

	rcu_read_lock();
	nsobj = __cam_obj_lookup(ns, type, id);
	if (nsobj) {
		rhashtable_remove_fast(&ns->objs, &nsobj->node, nsobj_params);
		ret = 0;
	} else {
		ret = -EINVAL;
	}
	rcu_read_unlock();

	synchronize_rcu();
	if (nsobj)
		cam_obj_put(nsobj);
	return ret;
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
 * cam_obj_get() - Increments refcounter of a valid NS object.
 * @nsobj: namespace object to do the unsafe and wrong action on.
 *
 * Return: NULL if the object's refcounter was not incremented.
 */
struct cam_obj *cam_obj_get(struct cam_obj *nsobj)
{
	bool ret;

	rcu_read_lock();
	ret = kref_get_unless_zero(&nsobj->kref);
	rcu_read_unlock();

	if (!ret)
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
 * @release: cleanout callback, called when refcount goes to 0
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
 * @ns: nemspace to walk
 * @ctl: auxilary data
 *
 * Callback will be called only on ACTIVE objects, with properly incremented
 * ref-counter.
 */
void cam_ns_for_each(struct cam_ns *ns, struct cam_ns_walk_control *ctl)
{
	struct rhashtable_iter iter;
	struct cam_obj *nsobj;

	if (WARN_ON(!ns))
		return;

	if (WARN_ON(!ctl))
		return;

	if (!ctl->cb)
		return;

	rhashtable_walk_enter(&ns->objs, &iter);
	rhashtable_walk_start(&iter);
	while ((nsobj = rhashtable_walk_next(&iter))) {
		if (IS_ERR(nsobj))
			continue;
		if (!kref_get_unless_zero(&nsobj->kref))
			continue;
		if (nsobj->flags & CAM_OBJ_FLAG_ACTIVE)
			ctl->cb(nsobj, ctl);
		cam_obj_put(nsobj);
	}
	rhashtable_walk_stop(&iter);
	rhashtable_walk_exit(&iter);
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
	int ret;

	memset(ns, 0, sizeof(*ns));

	switch (id_policy) {
	case CAM_NS_POL_UNIQUE_ID:
		ns->next_id = CAM_NS_UNIQUE_ID_START;
		ida_init(&ns->ids);
		break;
	case CAM_NS_POL_REUSE_ID:
		ida_init(&ns->ids);
		break;
	case CAM_NS_POL_USER_ID:
		break;
	default:
		pr_err("Unknown namespace id policy: %u\n", id_policy);
		return -EINVAL;
	}

	ret = rhashtable_init(&ns->objs, &nsobj_params);
	if (!ret) {
		ns->id_pol = id_policy;
		return 0;
	}

	if (id_policy != CAM_NS_POL_USER_ID)
		ida_destroy(&ns->ids);

	return ret;
}

static void ns_objs_cleanup(void *ptr, void *arg)
{
	struct cam_obj *nsobj = ptr;

	/*
	 * Deactivate dangling objects: we are about to destroy the
	 * namespace so release() function, which can be called after
	 * cam_ns_release(), cannot access IDA.
	 */
	nsobj->flags &= ~CAM_OBJ_FLAG_ACTIVE;
	cam_obj_put(nsobj);
}

/**
 * cam_ns_release() - Release namespace
 * @ns: namespace to release
 */
void cam_ns_release(struct cam_ns *ns)
{
	if (WARN_ON(!ns))
		return;

	if (WARN_ON(!ns->id_pol))
		return;

	rhashtable_free_and_destroy(&ns->objs, ns_objs_cleanup, NULL);
	if (ns->id_pol != CAM_NS_POL_USER_ID)
		ida_destroy(&ns->ids);
}

#ifdef CONFIG_CAM_KUNIT_TESTS
#include "cam-namespace-test.c"
#endif
