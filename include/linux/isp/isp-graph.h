/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ISP graph object
 *
 * Copyright (C) Google LLC
 */

#ifndef __LINUX_ISP_GRAPH_H__
#define __LINUX_ISP_GRAPH_H__

#include <linux/rwsem.h>

struct isp_ns;
struct isp_obj;

/**
 * struct isp_graph_node - ISP graph node structure
 *
 * This is used to represent the associated parent and children of the node
 * in which this structure is embedded.
 */
struct isp_graph_node {
	/** @linked_to: the parent of this node */
	struct isp_obj		*linked_to;
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

struct isp_device;

void isp_graph_node_init(struct isp_obj *nsobj);

int isp_obj_link(struct isp_obj *nsobj, struct isp_obj *link);
void isp_obj_unlink(struct isp_obj *nsobj);
u32 isp_obj_link_id(struct isp_obj *nsobj);
struct isp_obj *isp_obj_linked_to(struct isp_obj *nsobj);

#define isp_obj_for_each_link(link, obj)		\
	list_for_each_entry((link),			\
			    &(obj)->gnode.links,	\
			    gnode.link_entry)

#define isp_obj_for_each_link_safe(link, tmp, obj)	\
	list_for_each_entry_safe((link),		\
				 (tmp),			\
				 &(obj)->gnode.links,	\
				 gnode.link_entry)

/*
 * ISP object graph traversal
 */

#define ISP_GRAPH_STACK_DEPTH		32
/**
 * struct isp_graph_stack - the stack for graph traversal
 *
 * Mainly used in graph object enumeration or pipeline walk.
 */
struct isp_graph_stack {
	/** @objs: array of ISP object pointer for stack implementation */
	struct isp_obj	**objs;
	/** @top: the top of the stack */
	s32		top;
	/** @end: the size of the stack, determined on stack allocation */
	s32		end;
};

/* Enumerate the single matching object */
#define ISP_GRAPH_ENUM_SINGLE	BIT(0)
/* Enumerate objects sub-tree (multiple objects) */
#define ISP_GRAPH_ENUM_SUBTREE	BIT(1)

/**
 * struct isp_graph_walk - the graph walk controller
 *
 * This wraps the necessary flag and callback for better control over pipeline
 * walk.
 */
struct isp_graph_walk {
	/** @output: pointer to query output buffer */
	struct isp_koutput	*output;
	/** @flags: auxiliary flag */
	u64			flags;
	/** @match_type: the target type of a query e.g. entity or event */
	u32			match_type;
	/** @match_id: the target ID of a query, or ISP_QUERY_ALL_OBJECTS */
	u32			match_id;
	/** @cb: callback operation to the (matched) namespace objects */
	bool			(*cb)(struct isp_obj *nsobj,
				      struct isp_graph_walk *ctl);
};

int isp_graph_stack_alloc(struct isp_graph_stack *stack, u32 depth);
void isp_graph_stack_free(struct isp_graph_stack *stack);
struct isp_obj *isp_graph_stack_front(struct isp_graph_stack *stack);
void isp_graph_stack_pop(struct isp_graph_stack *stack);
int isp_graph_stack_push(struct isp_graph_stack *stack, struct isp_obj *obj);
bool isp_graph_stack_empty(struct isp_graph_stack *stack);

int isp_enum_graph_objects(struct isp_graph_walk *ctl,
			   struct isp_obj *nsobj);
int isp_enum_object_graph_links(struct isp_graph_walk *ctl,
				struct isp_obj *nsobj);

#endif /* __LINUX_ISP_GRAPH_H__ */
