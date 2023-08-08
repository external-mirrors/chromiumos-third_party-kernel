// SPDX-License-Identifier: GPL-2.0
/*
 * ISP objects namespace
 *
 * Copyright (C) Google LLC
 */

#define pr_fmt(fmt) "isp-namespace: " fmt

#include <linux/isp/isp-namespace.h>
#include <linux/isp/isp-device.h>
#include <uapi/linux/isp.h>
#include <linux/kernel.h>
#include <linux/slab.h>

/* Set when object holds an allocated IDA ID */
#define ISP_OBJ_FLAG_ACTIVE		BIT(0)

#define ISP_NS_UNIQUE_ID_START		0x00000000UL
#define ISP_NS_UNIQUE_ID_END		0x01ffffffUL

/*
 * Namespace does not keep released objects. All objects are removed
 * from the namespace first before the ->release() call. ->release()
 * callback must take care of allocated resources and kfree() the
 * containing object (if needed).
 */
static void isp_obj_final_put(struct kref *kref)
{
	struct isp_obj *nsobj = container_of(kref, struct isp_obj, kref);

	/* This should kfree() the namespace object */
	nsobj->release(nsobj);
}

/**
 * isp_obj_put() - decrement object refcount
 * @nsobj: pointer to object.
 *
 * There is no corresponding isp_obj_get() method, all objects
 * should be looked up by their IDs and type.
 *
 * Return: 1 if it was final put and the object was scheduled for delayed
 * destruction, 0 otherwise.
 */
int isp_obj_put(struct isp_obj *nsobj)
{
	if (!nsobj)
		return 0;

	return kref_put(&nsobj->kref, isp_obj_final_put);
}

/**
 * isp_obj_insert() - Adds object to its namespace and assigns it an
 * automatically generated ID.
 * @nsobj: object to add
 *
 * Return: 0 on success, negative error code otherwise.
 */
int isp_obj_insert(struct isp_obj *nsobj)
{
	struct isp_ns *ns;
	int ret;
	u32 id;

	if (WARN_ON(!nsobj))
		return -EINVAL;

	ns = nsobj->ns;
	if (WARN_ON(!ns))
		return -EINVAL;

	if (WARN_ON(nsobj->flags & ISP_OBJ_FLAG_ACTIVE))
		return -EEXIST;

	/*
	 * Increment the refcount before we make the object
	 * publicly available.
	 */
	isp_obj_get(nsobj);

	/* The object is not ACTIVE yet */
	down_write(&ns->lock);
	list_add(&nsobj->list_entry, &ns->objs_list);
	up_write(&ns->lock);

	switch (ns->id_pol) {
	case ISP_NS_POL_UNIQUE_ID:
		ret = xa_alloc(&ns->objs_table, &id, nsobj,
			       XA_LIMIT(ns->next_id, ISP_NS_UNIQUE_ID_END),
			       GFP_KERNEL);
		if (!ret) {
			nsobj->id = id;
			ns->next_id++;
			if (ns->next_id > ISP_NS_UNIQUE_ID_END)
				ns->next_id = ISP_NS_UNIQUE_ID_START;
		}
		break;
	case ISP_NS_POL_USER_ID:
		ret = xa_insert(&ns->objs_table, isp_obj_id(nsobj), nsobj,
				GFP_KERNEL);
		/*
		 * Unify error codes for double insert case. See
		 * ISP_OBJ_FLAG_ACTIVE branch earlier.
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
		nsobj->flags |= ISP_OBJ_FLAG_ACTIVE;
		up_write(&ns->lock);
		return 0;
	}

	down_write(&ns->lock);
	list_del_init(&nsobj->list_entry);
	up_write(&ns->lock);

	isp_obj_put(nsobj);
	return ret;
}
ALLOW_ERROR_INJECTION(isp_obj_insert, ERRNO);

/**
 * isp_obj_move() - Move an object to its namespace.
 * @nsobj: object to add
 * @id: id of the object
 *
 * Return: 0 on success, negative error code otherwise.
 * Once the object is moved to the namespace, its refcount decreases,
 * and therefore the caller to this function, shall not use its
 * reference.
 */
int isp_obj_move(struct isp_obj *nsobj, unsigned long *id)
{
	int ret;

	if (WARN_ON(!nsobj))
		return -EINVAL;

	ret = isp_obj_insert(nsobj);
	if (ret)
		return ret;

	if (id)
		*id = isp_obj_id(nsobj);

	isp_obj_put(nsobj);
	return 0;
}
ALLOW_ERROR_INJECTION(isp_obj_move, ERRNO);

/**
 * isp_obj_lookup() - Lookup object by ID and return a pointer to it if
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
struct isp_obj *isp_obj_lookup(struct isp_ns *ns,
			       enum isp_obj_type type,
			       unsigned long id)
{
	struct isp_obj *nsobj;

	/*
	 * It's not enough to just safely xa_load() the object as we need to
	 * test its type/etc. before we return the pointer. This requires
	 * guarantees that object is not released concurrently as long as
	 * isp_obj_lookup() access its flags. We can protect this entire
	 * function with RCU read side lock and queue_rcu_work() deferred
	 * isp_obj_final_put(), but locking the objs_table should work just
	 * fine.
	 */
	xa_lock(&ns->objs_table);
	nsobj = xa_load(&ns->objs_table, id);
	if (!nsobj)
		goto out;
	if (!(nsobj->flags & ISP_OBJ_FLAG_ACTIVE)) {
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
 * isp_obj_remove() - Removed object from the namespace and decrements
 * its ref-counter
 * @nsobj: object to remove
 */
void isp_obj_remove(struct isp_obj *nsobj)
{
	struct isp_ns *ns = nsobj->ns;
	unsigned long id;

	if (WARN_ON(!ns))
		return;

	if (WARN_ON(!(nsobj->flags & ISP_OBJ_FLAG_ACTIVE)))
		return;

	id = isp_obj_id(nsobj);
	nsobj->flags &= ~ISP_OBJ_FLAG_ACTIVE;
	xa_erase(&ns->objs_table, id);

	down_write(&ns->lock);
	list_del_init(&nsobj->list_entry);
	up_write(&ns->lock);

	isp_obj_put(nsobj);
}

/**
 * isp_obj_remove_id() - Removes object from the namespace corresponding
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
int isp_obj_remove_id(struct isp_ns *ns, enum isp_obj_type type,
		      unsigned long id)
{
	struct isp_obj *nsobj;

	if (WARN_ON(ns == NULL))
		return -EINVAL;

	nsobj = isp_obj_lookup(ns, type, id);
	if (!nsobj)
		return -ENOENT;

	nsobj->flags &= ~ISP_OBJ_FLAG_ACTIVE;
	xa_erase(&ns->objs_table, id);

	down_write(&ns->lock);
	list_del_init(&nsobj->list_entry);
	up_write(&ns->lock);

	/* Put additional isp_obj_lookup() refcount increment */
	isp_obj_put(nsobj);
	/* Put initial isp_obj_insert() refcount */
	isp_obj_put(nsobj);
	return 0;
}

/**
 * isp_obj_check_type() - Check whether isp_obj has the correct type
 * @nsobj: the namespace object
 * @type: the type that the object is expected to be
 *
 * Return: True if the type matches, or false otherwise.
 */
bool isp_obj_check_type(struct isp_obj *nsobj, enum isp_obj_type type)
{
	if (WARN_ON(nsobj == NULL))
		return false;
	if (WARN_ON(nsobj->type != type))
		return false;
	return true;
}

/**
 * isp_obj_get() - Increments refcount of a valid NS object.
 * @nsobj: namespace object.
 *
 * Return: NULL if the object's refcount was not incremented.
 */
struct isp_obj *isp_obj_get(struct isp_obj *nsobj)
{
	struct isp_ns *ns = nsobj->ns;

	if (WARN_ON(!ns))
		return NULL;

	if (!kref_get_unless_zero(&nsobj->kref))
		return NULL;
	return nsobj;
}

/**
 * isp_obj_id() - Return ID of the namespace object
 * @nsobj: namespace object
 *
 * Return: Object ID.
 */
unsigned long isp_obj_id(struct isp_obj *nsobj)
{
	WARN_ON((nsobj->ns->id_pol != ISP_NS_POL_USER_ID)
		&& !(nsobj->flags & ISP_OBJ_FLAG_ACTIVE));

	return nsobj->id;
}
EXPORT_SYMBOL_GPL(isp_obj_id);

enum isp_obj_type isp_obj_type(struct isp_obj *nsobj)
{
	WARN_ON(!(nsobj->flags & ISP_OBJ_FLAG_ACTIVE));
	return nsobj->type;
}

/**
 * isp_obj_set_id() - Set object ID
 * @nsobj: namespace object
 * @id: lookup id of the object
 *
 * Return: 0 if id was set, -EINVAL otherwise.
 */
int isp_obj_set_id(struct isp_obj *nsobj, unsigned long id)
{
	if (WARN_ON(nsobj->ns->id_pol != ISP_NS_POL_USER_ID))
		return -EINVAL;
	if (WARN_ON(nsobj->flags & ISP_OBJ_FLAG_ACTIVE))
		return -EINVAL;

	nsobj->id = id;
	return 0;
}

/**
 * isp_obj_init() - Initialise newly created namespace object
 * @nsobj: object to init
 * @type: type of the object
 * @release: clean-out callback, called when refcount goes to 0
 * @ns: namespace this object belongs to
 */
void isp_obj_init(struct isp_obj *nsobj,
		  enum isp_obj_type type,
		  void (*release)(struct isp_obj *nsobj),
		  struct isp_ns *ns)
{
	WARN_ON(!release);
	WARN_ON(!ns);

	memset(nsobj, 0, sizeof(*nsobj));
	kref_init(&nsobj->kref);
	nsobj->type = type;
	nsobj->release = release;
	nsobj->ns = ns;
	isp_graph_node_init(nsobj);
	INIT_LIST_HEAD(&nsobj->list_entry);
}

/**
 * isp_obj_deinit() - De-initialise namespace object
 * @nsobj: object to deinit
 */
void isp_obj_deinit(struct isp_obj *nsobj)
{
	if (WARN_ON(!nsobj))
		return;

	if (WARN_ON(kref_read(&nsobj->kref) != 1))
		return;

	isp_obj_put(nsobj);
}

/**
 * isp_ns_for_each() - Walks the namespace and calls callback on all active and
 * valid namespace objects
 * @ns: namespace to walk
 * @ctl: auxiliary data
 *
 * Callback will be called only on ACTIVE objects, with properly incremented
 * ref-counter.
 */
void isp_ns_for_each(struct isp_ns *ns, struct isp_ns_walk_control *ctl)
{
	struct isp_obj *nsobj;
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
	 * are on the objs_list list with ISP_OBJ_FLAG_ACTIVE bit set.
	 */
	down_read(&ns->lock);
	list_for_each_entry(nsobj, &ns->objs_list, list_entry) {
		if (!kref_get_unless_zero(&nsobj->kref))
			continue;
		if (nsobj->flags & ISP_OBJ_FLAG_ACTIVE)
			ret = ctl->cb(nsobj, ctl);
		isp_obj_put(nsobj);
		if (ret)
			break;
	}
	up_read(&ns->lock);
}

/**
 * isp_ns_init() - Initialises namespace
 * @ns: namespace to initialise
 * @id_policy: ID allocation policy
 *
 * Return: 0 on success.
 */
int isp_ns_init(struct isp_ns *ns, enum isp_id_policy id_policy)
{
	memset(ns, 0, sizeof(*ns));
	if (id_policy != ISP_NS_POL_UNIQUE_ID &&
	    id_policy != ISP_NS_POL_USER_ID) {
		pr_err("Unknown namespace id policy: %u\n", id_policy);
		return -EINVAL;
	}

	xa_init(&ns->objs_table);
	xa_init_flags(&ns->objs_table, XA_FLAGS_ALLOC);
	ns->next_id	= ISP_NS_UNIQUE_ID_START;
	ns->id_pol	= id_policy;
	init_rwsem(&ns->lock);
	INIT_LIST_HEAD(&ns->objs_list);
	return 0;
}

/**
 * isp_ns_release() - Release namespace
 * @ns: namespace to release
 */
void isp_ns_release(struct isp_ns *ns)
{
	struct isp_obj *nsobj;
	unsigned long id;

	if (WARN_ON(!ns))
		return;

	down_write(&ns->lock);
	while (!list_empty(&ns->objs_list)) {
		nsobj = list_first_entry(&ns->objs_list,
					 struct isp_obj,
					 list_entry);

		list_del_init(&nsobj->list_entry);

		if (!(nsobj->flags & ISP_OBJ_FLAG_ACTIVE))
			continue;

		id = isp_obj_id(nsobj);
		nsobj->flags &= ~ISP_OBJ_FLAG_ACTIVE;
		xa_erase(&ns->objs_table, id);
		isp_obj_put(nsobj);
	}
	up_write(&ns->lock);

	WARN_ON(!xa_empty(&ns->objs_table));
	xa_destroy(&ns->objs_table);
}

#ifdef CONFIG_ISP_KUNIT_TESTS
#include "isp-namespace-test.c"
#endif
