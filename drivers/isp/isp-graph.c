// SPDX-License-Identifier: GPL-2.0
/*
 * ISP object graph
 *
 * Copyright (C) Google LLC
 * Copyright (C) Intel Corporation
 */

#define pr_fmt(fmt) "isp-graph: " fmt

#include <linux/isp/isp-device.h>
#include <linux/isp/isp-entity.h>
#include <linux/isp/isp-graph.h>
#include <linux/isp/isp-namespace.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>

/**
 * isp_graph_node_init() - Initialise graph node
 * @nsobj: pointer to namespace object that holds graph object
 */
void isp_graph_node_init(struct isp_obj *nsobj)
{
	struct isp_graph_node *node = &nsobj->gnode;

	node->linked_to = NULL;
	init_rwsem(&node->lock);
	INIT_LIST_HEAD(&node->link_entry);
	INIT_LIST_HEAD(&node->links);
}

/**
 * isp_obj_link() - Link two namespace objects
 * @nsobj: namespace object to link
 * @link: namespace object to link to
 *
 * Return: 0 on success or error value otherwise
 */
int isp_obj_link(struct isp_obj *nsobj, struct isp_obj *link)
{
	struct isp_graph_node *curr_node = &nsobj->gnode;
	struct isp_graph_node *link_node;

	if (WARN_ON(curr_node->linked_to))
		return -EINVAL;

	if (!link)
		return -EINVAL;

	if (!isp_obj_get(link))
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
ALLOW_ERROR_INJECTION(isp_obj_link, ERRNO);

/**
 * isp_obj_unlink() - Unlink objects
 * @nsobj: object to unlink
 */
void isp_obj_unlink(struct isp_obj *nsobj)
{
	struct isp_graph_node *curr_node = &nsobj->gnode;
	struct isp_graph_node *link_node;
	struct isp_obj *link;

	if (!curr_node->linked_to)
		return;

	link = curr_node->linked_to;
	curr_node->linked_to = NULL;
	link_node = &link->gnode;

	down_write(&link_node->lock);
	list_del(&curr_node->link_entry);
	up_write(&link_node->lock);

	isp_obj_put(link);
}

/**
 * isp_obj_link_id() - ID of the linked_to object
 * @nsobj: object to get linked_to ID of
 *
 * Return: ID of the linked_to object.
 */
u32 isp_obj_link_id(struct isp_obj *nsobj)
{
	struct isp_graph_node *curr_node = &nsobj->gnode;
	u32 pair_id = ISP_OBJ_ID_ROOT;

	/*
	 * If this object is not paired with anything explicitly then return
	 * ISP_OBJ_ID_ROOT.
	 */
	if (curr_node->linked_to)
		pair_id = isp_obj_id(curr_node->linked_to);
	return pair_id;
}


/**
 * isp_obj_linked_to() - Get object we are linked to
 * @nsobj: current object
 *
 * Return: NULL on error or pointer to object we are linked to with
 * incremented ref-count.
 */
struct isp_obj *isp_obj_linked_to(struct isp_obj *nsobj)
{
	struct isp_graph_node *curr_node = &nsobj->gnode;

	if (!curr_node->linked_to)
		return NULL;
	if (!isp_obj_get(curr_node->linked_to))
		return NULL;
	return curr_node->linked_to;
}

/**
 * isp_graph_stack_alloc() - Allocate array to serve as stack storage for graph
 * traversal. Stack holds pointers to namespace objects.
 * @stack: pointer to graph stack
 * @depth: size of the stack array
 *
 * Return: 0 on success or error value otherwise.
 */
int isp_graph_stack_alloc(struct isp_graph_stack *stack, u32 depth)
{
	if (!depth)
		return -ENOMEM;

	stack->objs = kmalloc_array(depth, sizeof(struct isp_obj *),
				    GFP_KERNEL);
	if (!stack->objs)
		return -ENOMEM;
	stack->top = -1;
	stack->end = depth;
	return 0;
}
ALLOW_ERROR_INJECTION(isp_graph_stack_alloc, ERRNO);

/**
 * isp_graph_stack_free() - Free stack array
 * @stack: pointer to graph stack
 */
void isp_graph_stack_free(struct isp_graph_stack *stack)
{
	WARN_ON(stack->top >= 0);
	kvfree(stack->objs);
}

/**
 * isp_graph_stack_front() - Return stack front (top) element.
 * @stack: pointer to graph stack
 *
 * Return: Pointer to front (top) stack element or NULL if stack is empty.
 *
 */
struct isp_obj *isp_graph_stack_front(struct isp_graph_stack *stack)
{
	if (WARN_ON(stack->top < 0))
		return NULL;
	return stack->objs[stack->top];
}
ALLOW_ERROR_INJECTION(isp_graph_stack_front, NULL);

/**
 * isp_graph_stack_pop() - Pop (remove) stack front (top) element
 * @stack: pointer to graph stack
 */
void isp_graph_stack_pop(struct isp_graph_stack *stack)
{
	if (WARN_ON(stack->top < 0))
		return;
	stack->objs[stack->top--] = NULL;
}

/**
 * isp_graph_stack_push() - Add (push) element to stack
 * @stack: pointer to graph stack
 * @obj: object to push
 *
 * Return: 0 on success or error value on error (e.g. when stack is full).
 */
int isp_graph_stack_push(struct isp_graph_stack *stack, struct isp_obj *obj)
{
	if (stack->top + 1 >= stack->end)
		return -EFBIG;
	stack->objs[++stack->top] = obj;
	return 0;
}
ALLOW_ERROR_INJECTION(isp_graph_stack_push, ERRNO);

/**
 * isp_graph_stack_empty() - Check if stack is empty
 * @stack: pointer to graph stack
 *
 * Return: True if stack is empty of false otherwise.
 */
bool isp_graph_stack_empty(struct isp_graph_stack *stack)
{
	return stack->top < 0;
}

/**
 * isp_graph_walk_match_obj() - ISP object matching helper function
 * @nsobj: pointer to ISP object
 * @ctl: auxiliary data
 *
 * Return: true if the object has the expected type and ID (or use
 * ISP_QUERY_ALL_OBJECTS for arbitrary IDs).
 */
bool isp_graph_walk_match_obj(struct isp_obj *nsobj,
			      struct isp_graph_walk *ctl)
{
	if (!(ctl->match_type & nsobj->type))
		return false;
	if (ctl->match_id == ISP_QUERY_ALL_OBJECTS)
		return true;
	return ctl->match_id == isp_obj_id(nsobj);
}

/**
 * isp_enum_graph_objects() - Walk through the sub-graph of the ISP object and
 * enumerate its children
 * @ctl: auxiliary data
 * @nsobj: pointer to ISP object
 * @depth: depth limit of graph walk
 *
 * Return: 0 on success or error value otherwise.
 */
int isp_enum_graph_objects(struct isp_graph_walk *ctl,
			   struct isp_obj *nsobj,
			   size_t depth)
{
	struct isp_graph_stack st;
	bool abort;
	int ret;

	ret = isp_graph_stack_alloc(&st, depth);
	if (ret)
		return ret;

	if (!isp_obj_get(nsobj)) {
		ret = -EINVAL;
		goto out;
	}

	isp_graph_stack_push(&st, nsobj);
	abort = false;

	while (!abort && !isp_graph_stack_empty(&st)) {
		struct isp_obj *link;

		nsobj = isp_graph_stack_front(&st);
		isp_graph_stack_pop(&st);

		/*
		 * We need to match object here as well for the case when
		 * we look for a particular child object
		 */
		if (isp_graph_walk_match_obj(nsobj, ctl) &&
		    !ctl->cb(nsobj, ctl)) {
			ret = -EFAULT;
			abort = true;
		}

		if (ctl->flags & ISP_GRAPH_WALK_ONESHOT) {
			ret = 0;
			abort = true;
		}

		down_read(&nsobj->gnode.lock);
		isp_obj_for_each_link(link, nsobj) {
			if (abort)
				continue;
			/*
			 * Do not add objects that have mismatching type
			 * or/and mismatching ID.
			 */
			if (!isp_graph_walk_match_obj(link, ctl))
				continue;

			if (!isp_obj_get(link))
				continue;

			ret = isp_graph_stack_push(&st, link);
			if (ret) {
				isp_obj_put(link);
				abort = true;
			}
		}
		up_read(&nsobj->gnode.lock);
		isp_obj_put(nsobj);
	}

out:
	while (!isp_graph_stack_empty(&st)) {
		isp_obj_put(isp_graph_stack_front(&st));
		isp_graph_stack_pop(&st);
	}
	isp_graph_stack_free(&st);
	return ret;
}

#ifdef CONFIG_ISP_KUNIT_TESTS
#include "isp-graph-test.c"
#endif
