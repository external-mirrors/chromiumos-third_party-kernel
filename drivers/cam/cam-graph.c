// SPDX-License-Identifier: GPL-2.0
/*
 * CAM object graph
 *
 * Copyright (C) 2022 Google LLC
 * Copyright (C) 2020 Intel Corporation
 */

#define pr_fmt(fmt) "cam-graph: " fmt

#include <linux/cam/cam-device.h>
#include <linux/cam/cam-entity.h>
#include <linux/cam/cam-graph.h>
#include <linux/cam/cam-namespace.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>

/**
 * cam_graph_node_init() - Init graph node
 * @nsobj: pointer to namespace object that holds graph object
 */
void cam_graph_node_init(struct cam_obj *nsobj)
{
	struct cam_graph_node *node = &nsobj->gnode;

	node->parent = NULL;
	init_rwsem(&node->lock);
	INIT_LIST_HEAD(&node->link_entry);
	INIT_LIST_HEAD(&node->links);
}

/**
 * cam_graph_node_link() - Link namespace object to (parent) entity
 * @cam: CAM device
 * @nsobj: namespace object to link
 * @parent_id: ID of parent entity object
 *
 * Return: 0 on success or error value otherwise
 */
int cam_graph_node_link(struct cam_device *cam,
			struct cam_obj *nsobj,
			u32 parent_id)
{
	struct cam_graph_node *child = &nsobj->gnode;
	struct cam_obj_entity *entity;
	struct cam_graph_node *parent;

	if (WARN_ON(child->parent))
		return -EINVAL;

	/*
	 * At this point, child can be of any type,
	 * parent - only of CAM_OBJ_TYPE_ENTITY.
	 */
	entity = cam_entity_lookup(cam, parent_id);
	if (!entity)
		return -EINVAL;

	/*
	 * We don't need to protect ->parent assignment as it should be done
	 * when object is created and before it's inserted into namespace and
	 * it's cleared from object ->release() path when object has 0 ref
	 * counter.
	 *
	 * So we should not race with anything.
	 */
	parent = &entity->nsobj.gnode;
	child->parent = &entity->nsobj;

	down_write(&parent->lock);
	list_add(&child->link_entry, &parent->links);
	up_write(&parent->lock);

	return 0;
}
ALLOW_ERROR_INJECTION(cam_graph_node_link, ERRNO);

/**
 * cam_graph_node_unlink() - Unlink objects (remove from parents child list)
 * @nsobj: object to unlink
 */
void cam_graph_node_unlink(struct cam_obj *nsobj)
{
	struct cam_graph_node *child = &nsobj->gnode;
	struct cam_obj *parent;

	if (!child->parent)
		return;

	parent = child->parent;
	child->parent = NULL;

	down_write(&parent->gnode.lock);
	list_del(&child->link_entry);
	up_write(&parent->gnode.lock);

	cam_obj_put(parent);
}

/**
 * cam_graph_node_link_id() - ID of the parent object
 * @nsobj: object to get parent ID of
 *
 * Return: ID of the parent object.
 */
u32 cam_graph_node_link_id(struct cam_obj *nsobj)
{
	struct cam_graph_node *child = &nsobj->gnode;
	u32 pair_id = CAM_OBJ_ID_ROOT;

	/*
	 * If this object is not paired with anything explicitly then return
	 * CAM_OBJ_ID_ROOT.
	 */
	if (child->parent)
		pair_id = cam_obj_id(child->parent);
	return pair_id;
}

/**
 * cam_graph_stack_alloc() - Allocate array to serve as stack storage for graph
 * traversal. Stack holds pointers to namespace objects.
 * @stack: pointer to graph stack
 * @depth: size of the stack array
 *
 * Return: 0 on success or error value otherwise.
 */
int cam_graph_stack_alloc(struct cam_graph_stack *stack, u32 depth)
{
	if (!depth)
		return -ENOMEM;

	stack->objs = kmalloc_array(depth, sizeof(struct cam_obj *),
				    GFP_KERNEL);
	if (!stack->objs)
		return -ENOMEM;
	stack->top = -1;
	stack->end = depth;
	return 0;
}
ALLOW_ERROR_INJECTION(cam_graph_stack_alloc, ERRNO);

/**
 * cam_graph_stack_free() - Free stack array
 * @stack: pointer to graph stack
 */
void cam_graph_stack_free(struct cam_graph_stack *stack)
{
	WARN_ON(stack->top >= 0);
	kvfree(stack->objs);
}

/**
 * cam_graph_stack_front() - Return stack front (top) element.
 * @stack: pointer to graph stack
 *
 * Return: Pointer to front (top) stack element or NULL if stack is empty.
 *
 */
struct cam_obj *cam_graph_stack_front(struct cam_graph_stack *stack)
{
	if (WARN_ON(stack->top < 0))
		return NULL;
	return stack->objs[stack->top];
}
ALLOW_ERROR_INJECTION(cam_graph_stack_front, NULL);

/**
 * cam_graph_stack_pop() - Pop (remove) stack front (top) element
 * @stack: pointer to graph stack
 */
void cam_graph_stack_pop(struct cam_graph_stack *stack)
{
	if (WARN_ON(stack->top < 0))
		return;
	stack->objs[stack->top--] = NULL;
}

/**
 * cam_graph_stack_push() - Add (push) element to stack
 * @stack: pointer to graph stack
 * @obj: object to push
 *
 * Return: 0 on success or error value on error (e.g. when stack is full).
 */
int cam_graph_stack_push(struct cam_graph_stack *stack, struct cam_obj *obj)
{
	if (stack->top + 1 >= stack->end)
		return -EFBIG;
	stack->objs[++stack->top] = obj;
	return 0;
}
ALLOW_ERROR_INJECTION(cam_graph_stack_push, ERRNO);

/**
 * cam_graph_stack_empty() - Check if stack is empty
 * @stack: pointer to graph stack
 *
 * Return: True if stack is empty of false otherwise.
 */
bool cam_graph_stack_empty(struct cam_graph_stack *stack)
{
	return stack->top < 0;
}

/**
 * cam_graph_walk_match_obj() - CAM object matching helper function
 * @nsobj: pointer to CAM object
 * @ctl: auxiliary data
 *
 * Return: true if the object has the expected type and ID (or use
 * CAM_QUERY_ALL_OBJECTS for arbitrary IDs).
 */
bool cam_graph_walk_match_obj(struct cam_obj *nsobj,
			      struct cam_graph_walk *ctl)
{
	if (!(ctl->match_type & nsobj->type))
		return false;
	if (ctl->match_id == CAM_QUERY_ALL_OBJECTS)
		return true;
	return ctl->match_id == cam_obj_id(nsobj);
}

/**
 * cam_enum_graph_objects() - Walk through the subgraph of a CAM object and
 * enumerate its children
 * @ctl: auxiliary data
 * @nsobj: pointer to CAM object
 * @depth: depth limit of graph walk
 *
 * Return: 0 on success or error value otherwise.
 */
int cam_enum_graph_objects(struct cam_graph_walk *ctl,
			   struct cam_obj *nsobj,
			   size_t depth)
{
	struct cam_graph_stack st;
	bool abort;
	int ret;

	ret = cam_graph_stack_alloc(&st, depth);
	if (ret)
		return ret;

	if (!cam_obj_get(nsobj)) {
		ret = -EINVAL;
		goto out;
	}

	cam_graph_stack_push(&st, nsobj);
	abort = false;

	while (!abort && !cam_graph_stack_empty(&st)) {
		struct cam_obj *child;

		nsobj = cam_graph_stack_front(&st);
		cam_graph_stack_pop(&st);

		/*
		 * Object enumeration might_fault() so we need to unlock RCU,
		 * we hold object's refcounter.
		 *
		 * We need to match object here as well for the case when
		 * we look for a particular child object
		 */
		if (cam_graph_walk_match_obj(nsobj, ctl) &&
		    !ctl->cb(nsobj, ctl)) {
			ret = -EFAULT;
			abort = true;
		}

		if (ctl->flags & CAM_GRAPH_WALK_ONESHOT) {
			ret = 0;
			abort = true;
		}

		down_read(&nsobj->gnode.lock);
		cam_obj_for_each_link(child, nsobj) {
			if (abort)
				continue;
			/*
			 * Do not add objects that have mismatching type
			 * or/and mismatching ID.
			 */
			if (!cam_graph_walk_match_obj(child, ctl))
				continue;

			if (!cam_obj_get(child))
				continue;

			ret =  cam_graph_stack_push(&st, child);
			if (ret) {
				cam_obj_put(child);
				abort = true;
			}
		}
		up_read(&nsobj->gnode.lock);
		cam_obj_put(nsobj);
	}

out:
	while (!cam_graph_stack_empty(&st)) {
		cam_obj_put(cam_graph_stack_front(&st));
		cam_graph_stack_pop(&st);
	}
	cam_graph_stack_free(&st);
	return ret;
}

#ifdef CONFIG_CAM_KUNIT_TESTS
#include "cam-graph-test.c"
#endif
