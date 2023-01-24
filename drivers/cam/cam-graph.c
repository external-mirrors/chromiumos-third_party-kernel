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

	node->linked_to = NULL;
	init_rwsem(&node->lock);
	INIT_LIST_HEAD(&node->link_entry);
	INIT_LIST_HEAD(&node->links);
}

/**
 * cam_obj_link() - Link two namespace objects
 * @nsobj: namespace object to link
 * @link: namespace object to link to
 *
 * Return: 0 on success or error value otherwise
 */
int cam_obj_link(struct cam_obj *nsobj, struct cam_obj *link)
{
	struct cam_graph_node *curr_node = &nsobj->gnode;
	struct cam_graph_node *link_node;

	if (WARN_ON(curr_node->linked_to))
		return -EINVAL;

	if (!link)
		return -EINVAL;

	if (!cam_obj_get(link))
		return -EINVAL;

	/*
	 * We don't need to protect ->linked_to assignment as it should be done
	 * when object is created and before it's inserted into namespace and
	 * it's cleared from object ->release() path when object has 0 ref
	 * counter.
	 *
	 * So we should not race with anything.
	 */
	link_node = &link->gnode;
	curr_node->linked_to = link;

	down_write(&link_node->lock);
	list_add(&curr_node->link_entry, &link_node->links);
	up_write(&link_node->lock);

	return 0;
}
ALLOW_ERROR_INJECTION(cam_obj_link, ERRNO);

/**
 * cam_obj_unlink() - Unlink objects
 * @nsobj: object to unlink
 */
void cam_obj_unlink(struct cam_obj *nsobj)
{
	struct cam_graph_node *curr_node = &nsobj->gnode;
	struct cam_graph_node *link_node;
	struct cam_obj *link;

	if (!curr_node->linked_to)
		return;

	link = curr_node->linked_to;
	curr_node->linked_to = NULL;
	link_node = &link->gnode;

	down_write(&link_node->lock);
	list_del(&curr_node->link_entry);
	up_write(&link_node->lock);

	cam_obj_put(link);
}

/**
 * cam_graph_node_link_id() - ID of the linked_to object
 * @nsobj: object to get linked_to ID of
 *
 * Return: ID of the linked_to object.
 */
u32 cam_obj_link_id(struct cam_obj *nsobj)
{
	struct cam_graph_node *curr_node = &nsobj->gnode;
	u32 pair_id = CAM_OBJ_ID_ROOT;

	/*
	 * If this object is not paired with anything explicitly then return
	 * CAM_OBJ_ID_ROOT.
	 */
	if (curr_node->linked_to)
		pair_id = cam_obj_id(curr_node->linked_to);
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
		struct cam_obj *link;

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
		cam_obj_for_each_link(link, nsobj) {
			if (abort)
				continue;
			/*
			 * Do not add objects that have mismatching type
			 * or/and mismatching ID.
			 */
			if (!cam_graph_walk_match_obj(link, ctl))
				continue;

			if (!cam_obj_get(link))
				continue;

			ret = cam_graph_stack_push(&st, link);
			if (ret) {
				cam_obj_put(link);
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
