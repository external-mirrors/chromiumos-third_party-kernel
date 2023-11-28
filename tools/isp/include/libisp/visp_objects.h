// SPDX-License-Identifier: GPL-2.0
/*
 * visp objects
 *
 * Copyright (C) 2022 Google LLC
 */

#ifndef __KISP_OBJECTS_H_
#define __KISP_OBJECTS_H_

#include <linux/list.h>

/* @FIXME */
#include "../../../include/uapi/linux/isp.h"

enum obj_type {
	OBJ_TYPE_ENTITY,
	OBJ_TYPE_EVENT,
	OBJ_TYPE_BUFFER,
};

struct obj_entity {
	/* Do not move these fields */
	struct list_head	parent_entry;
	enum obj_type		type;

	unsigned int		id;
	char			name[ISP_ENTITY_NAME_SZ];

	struct list_head	children;

	struct list_head	obj_list;
};

struct obj_event {
	/* Do not move these fields */
	struct list_head	parent_entry;
	enum obj_type		type;

	unsigned int		id;
	char			name[ISP_EVENT_NAME_SZ];

	struct list_head	obj_list;
};

struct obj_buffer {
	/* Do not move these fields */
	enum obj_type		type;

	unsigned int		id;
	void			*dmabuf;

	struct list_head	obj_list;
};

#endif /* __KISP_OBJECTS_H_ */
