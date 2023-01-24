/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CAM graph object
 *
 * Copyright (C) 2022 Google LLC
 */

#ifndef __LINUX_CAM_GRAPH_H__
#define __LINUX_CAM_GRAPH_H__

#include <linux/rwsem.h>

struct cam_ns;
struct cam_obj;

/**
 * cam_graph_node - CAM graph node structure
 *
 * This is used to represent the associated parent and children of the node
 * in which this structure is embedded.
 */
struct cam_graph_node {
	/** @linked_to: the parent of this node */
	struct cam_obj		*linked_to;
	/**
	 * @lock: any manipulations to any other fields in this struct should
	 *        be protected by this lock
	 */
	struct rw_semaphore	lock;
	/** @link_entry: our entry in linked object's links list */
	struct list_head	link_entry;
	/** @links: the list of objects linked to us  */
	struct list_head	links;
};

struct cam_device;

void cam_graph_node_init(struct cam_obj *nsobj);

int cam_obj_link(struct cam_obj *nsobj, struct cam_obj *link);
void cam_obj_unlink(struct cam_obj *nsobj);
u32 cam_obj_link_id(struct cam_obj *nsobj);

#define cam_obj_for_each_link(link, obj)		\
	list_for_each_entry((link),			\
			    &(obj)->gnode.links,	\
			    gnode.link_entry)

#define cam_obj_for_each_link_safe(link, tmp, obj)	\
	list_for_each_entry_safe((link),		\
				 (tmp),			\
				 &(obj)->gnode.links,	\
				 gnode.link_entry)

/*
 * CAM object graph traversal
 */

#define CAM_GRAPH_STACK_DEPTH		32
/**
 * cam_graph_stack - the stack for graph traversal
 *
 * Mainly used in graph object enumeration or pipeline walk.
 */
struct cam_graph_stack {
	/** @objs: array of CAM object pointer for stack implementation */
	struct cam_obj	**objs;
	/** @top: the top of the stack */
	s32		top;
	/** @end: the size of the stack, determined on stack allocation */
	s32		end;
};

/* Process only one graph object and terminate, do not walk the graph */
#define CAM_GRAPH_WALK_ONESHOT		BIT(0)
/* Walk all sub-graph objects */
#define CAM_GRAPH_WALK_RECURSIVE	BIT(1)

/**
 * cam_graph_walk - the graph walk controller
 *
 * This wraps the necessary flag and callback for better control over pipeline
 * walk.
 */
struct cam_graph_walk {
	/** @data: pointer to auxiliary data e.g. output of a pipeline query */
	void		*data;
	/** @flags: auxiliary flag */
	u32		flags;

	/** @match_type: the target type of a query e.g. entity or event */
	u32		match_type;
	/** @match_id: the target ID of a query, or CAM_QUERY_ALL_OBJECTS */
	u32		match_id;

	/** @cb: callback operation to the (matched) namespace objects */
	bool		(*cb)(struct cam_obj *nsobj,
			      struct cam_graph_walk *ctl);
};

int cam_graph_stack_alloc(struct cam_graph_stack *stack, u32 depth);
void cam_graph_stack_free(struct cam_graph_stack *stack);
struct cam_obj *cam_graph_stack_front(struct cam_graph_stack *stack);
void cam_graph_stack_pop(struct cam_graph_stack *stack);
int cam_graph_stack_push(struct cam_graph_stack *stack, struct cam_obj *obj);
bool cam_graph_stack_empty(struct cam_graph_stack *stack);

bool cam_graph_walk_match_obj(struct cam_obj *nsobj,
			      struct cam_graph_walk *ctl);

int cam_enum_graph_objects(struct cam_graph_walk *ctl,
			   struct cam_obj *nsobj,
			   size_t depth);

#endif /* __LINUX_CAM_GRAPH_H__ */
