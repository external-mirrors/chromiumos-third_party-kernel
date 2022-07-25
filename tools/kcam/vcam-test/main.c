/* SPDX-License-Identifier: GPL-2.0 */
/*
 * libkc and VCAM test tool
 *
 * Copyright (C) 2022 Google LLC
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <libkc/libkc.h>
#include <vcam_objects.h>

/* @FIXME */
#include "../../../include/uapi/linux/vcam.h"

static LIST_HEAD(entities);
static int num_entities;

static LIST_HEAD(events);
static int num_events;

static LIST_HEAD(buffers);
static int num_buffers;

#define CAM_NO_DEP	0xffffffff

#define CAM_DEFAULT_OUT_SZ	4096

static char *exit_at_functions[] = {
	"wait_for_slow_entity_timer",
	"test_add_valid_operations",
	"test_add_valid_rw_operations",
	"test_export_import_operations",
	"test_add_buffers"
};

const char *exit_at_function = NULL;

static void __may_exit_at(const char *func)
{
	if (exit_at_function && strcmp(func, exit_at_function) == 0) {
		pr_err(" *** Simulate crash at %s() ***\n",
		       exit_at_function);
		exit(0);
	}
}

static void show_exit_at_functions(void)
{
	int idx = 0;

	pr_err("Supported exit functions:\n");
	pr_err("\t--exit_at or -e\n");

	for (idx = 0; idx < ARRAY_SIZE(exit_at_functions); idx++) {
		pr_err("\t%s\n", exit_at_functions[idx]);
	}

	exit(0);
}

static void parse_exit_at_function(char *command)
{
	int idx = 0;

	if (!command || !strlen(command))
		return;

	for (idx = 0; idx < ARRAY_SIZE(exit_at_functions); idx++) {
		if (strcmp(exit_at_functions[idx], command) == 0) {
			exit_at_function = command;
			break;
		}
	}
}

#define MAY_EXIT_AT()	__may_exit_at(__func__)

static struct obj_entity *entity_lookup(unsigned int id)
{
	struct obj_entity *entry;

	/*
	 * We have very few entities created by VCAM, so a simple linear
	 * search is just fine
	 */
	list_for_each_entry(entry, &entities, obj_list) {
		if (entry->type == OBJ_TYPE_ENTITY && entry->id == id)
			return entry;
	}

	return NULL;
}

static struct obj_entity *entity_lookup_by_name(const char *name)
{
	struct obj_entity *entry;

	/*
	 * We have very few entities created by VCAM, so a simple linear
	 * search is just fine
	 */
	list_for_each_entry(entry, &entities, obj_list) {
		if (entry->type == OBJ_TYPE_ENTITY &&
		    !strcmp(entry->name, name))
			return entry;
	}

	return NULL;
}

static struct obj_event *entity_first_event(struct obj_entity *entity)
{
	struct obj_event *event;

	list_for_each_entry(event, &entity->children, parent_entry) {
		if (event->type != OBJ_TYPE_EVENT)
			continue;
		return event;
	}
	return NULL;
}

static int entity_register(struct cam_query_entity_entry *entry)
{
	struct obj_entity *obj;
	struct obj_entity *parent;

	obj = malloc(sizeof(*obj));
	if (!obj) {
		pr_err("OOM\n");
		return -ENOMEM;
	}

	num_entities++;
	obj->id = entry->id;
	obj->type = OBJ_TYPE_ENTITY;
	strcpy(obj->name, entry->name);
	INIT_LIST_HEAD(&obj->children);
	list_add_tail(&obj->obj_list, &entities);

	if (obj->id == CAM_OBJ_ID_ROOT)
		return 0;

	parent = entity_lookup(entry->parent);
	if (!parent) {
		pr_err("Unable to find entity ID: %d\n", entry->parent);
		return -EINVAL;
	}

	list_add(&obj->parent_entry, &parent->children);
	return 0;
}

static int event_register(struct cam_query_event_entry *entry,
			  unsigned int entity_id)
{
	struct obj_event *obj;
	struct obj_entity *parent;

	obj = malloc(sizeof(*obj));
	if (!obj) {
		pr_err("OOM\n");
		return -ENOMEM;
	}

	num_events++;
	obj->id = entry->id;
	obj->type = OBJ_TYPE_EVENT;
	strcpy(obj->name, entry->name);
	list_add_tail(&obj->obj_list, &events);

	parent = entity_lookup(entity_id);
	if (!parent) {
		pr_err("Unable to find entity ID: %d\n", entity_id);
		return -EINVAL;
	}

	list_add(&obj->parent_entry, &parent->children);
	return 0;
}

static int buffer_register(struct obj_entity *parent,
			   struct libkc_dmabuf *buf)
{
	struct obj_buffer *obj;

	obj = malloc(sizeof(*obj));
	if (!obj) {
		pr_err("OOM\n");
		return -ENOMEM;
	}

	num_buffers++;
	obj->type = OBJ_TYPE_BUFFER;
	obj->id = buf->fd;
	obj->dmabuf = buf;
	list_add_tail(&obj->obj_list, &buffers);

	list_add(&obj->parent_entry, &parent->children);
	return 0;
}

static void buffer_unregister(struct obj_buffer *buf)
{
	num_buffers--;
	list_del(&buf->parent_entry);
	list_del(&buf->obj_list);
	libkc_dmabuf_put(buf->dmabuf);
	free(buf);
}

static int test_query_unknown_entity(struct libkc *cam,
				     struct libkc_query *lcq)
{
	struct libkc_iterator iter;
	struct cam_query *q;
	int ret;
	int i;

	pr_info("Test test_query_unknown_entity()\n");

	/* We should never have entity with this ID */
	for_each_cam_query(lcq, i, q) {
		q->query_type			= CAM_QUERY_TYPE_ENTITIES;
		q->query_entities.id		= 0xfffffff;
		q->query_entities.maxdepth	= CAM_QUERY_ALL_OBJECTS;
	}

	ret = libkc_query_ioctl(cam, lcq);
	if (ret) {
		/* Failure is expected here */
		ret = 0;
		goto out;
	}

	pr_err("Unexpected entities\n");
	ret = -EINVAL;

	libkc_iterator_init(&lcq->hdr, &iter);
	for_each_cam_query(lcq, i, q) {
		struct cam_query_entity_entry *entry;

		if (q->query_type != CAM_QUERY_TYPE_ENTITIES) {
			pr_err("Unexpected query return type: %d\n",
			       q->query_type);
			ret = -EINVAL;
			goto out;
		}

		for_each_query_entity(q, &iter, entry) {
			pr_info("[UNEXPECTED] Entity ID: %d, Name: %s, Parent: %d\n",
				entry->id,
				entry->name,
				entry->parent);
		}
	}
out:
	return ret;
}

static int test_query_all_entities(struct libkc *cam,
				   struct libkc_query *lcq,
				   u32 max_depth)
{
	struct libkc_iterator iter;
	struct cam_query *q;
	int ret;
	int i;

	pr_info("Test test_query_all_entities()\n");

	for_each_cam_query(lcq, i, q) {
		q->query_type			= CAM_QUERY_TYPE_ENTITIES;
		q->query_entities.id		= CAM_OBJ_ID_ROOT;
		q->query_entities.maxdepth	= max_depth;
	}

	ret = libkc_query_ioctl(cam, lcq);
	if (ret)
		goto out;

	libkc_iterator_init(&lcq->hdr, &iter);
	for_each_cam_query(lcq, i, q) {
		struct cam_query_entity_entry *entry;

		if (q->query_type != CAM_QUERY_TYPE_ENTITIES) {
			pr_err("Unexpected query return type: %d\n",
			       q->query_type);
			ret = -EINVAL;
			goto out;
		}

		for_each_query_entity(q, &iter, entry) {
			pr_info("Entity ID: %d, Name: %s, Parent: %d\n",
				entry->id,
				entry->name,
				entry->parent);

			ret = entity_register(entry);
			if (ret)
				goto out;
		}
	}
out:
	return ret;
}

#define VCAM_ENTITIES_COUNT	5
#define VCAM_EVENTS_COUNT	2

static int test_compound_query_count(struct libkc *cam,
				     struct libkc_query *lcq)
{
	struct obj_entity *entity;
	struct cam_query *q;
	int ret;
	int i;

	pr_info("Test test_compound_query_count()\n");

	q = libkc_query_at(lcq, 0);
	if (!q) {
		pr_err("Unable to get query\n");
		return -EINVAL;
	}

	q->query_type			= CAM_QUERY_TYPE_ENTITIES;
	q->query_entities.id		= CAM_OBJ_ID_ROOT;
	q->query_entities.maxdepth	= CAM_QUERY_ALL_OBJECTS;

	entity = entity_lookup_by_name(VCAM_FAST_IRQ_ENTITY_NAME);
	if (!entity) {
		pr_err("Unable to lookup %s entity\n",
		       VCAM_FAST_IRQ_ENTITY_NAME);
		return -EINVAL;
	}

	q = libkc_query_at(lcq, 1);
	if (!q) {
		pr_err("Unable to get query\n");
		return -EINVAL;
	}

	q->query_type			= CAM_QUERY_TYPE_EVENTS;
	q->query_events.entity		= entity->id;
	q->query_events.id		= CAM_QUERY_ALL_OBJECTS;

	entity = entity_lookup_by_name(VCAM_SLOW_IRQ_ENTITY_NAME);
	if (!entity) {
		pr_err("Unable to lookup %s entity\n",
		       VCAM_SLOW_IRQ_ENTITY_NAME);
		return -EINVAL;
	}

	q = libkc_query_at(lcq, 2);
	if (!q) {
		pr_err("Unable to get query\n");
		return -EINVAL;
	}

	q->query_type			= CAM_QUERY_TYPE_EVENTS;
	q->query_events.entity		= entity->id;
	q->query_events.id		= CAM_QUERY_ALL_OBJECTS;

	ret = libkc_query_ioctl(cam, lcq);
	if (ret)
		return ret;

	q = libkc_query_at(lcq, 0);
	if (libkc_query_num_entities(q) != VCAM_ENTITIES_COUNT) {
		pr_err("Unexpected number of entities: %d expected: %d\n",
		       q->query_entities.num_entities,
		       VCAM_ENTITIES_COUNT);
		return -EINVAL;
	}

	q = libkc_query_at(lcq, 1);
	if (libkc_query_num_events(q) != VCAM_EVENTS_COUNT) {
		pr_err("Unexpected number of events: %d expected: %d\n",
		       q->query_events.num_events,
		       VCAM_EVENTS_COUNT);
		return -EINVAL;
	}

	q = libkc_query_at(lcq, 2);
	if (libkc_query_num_events(q) != VCAM_EVENTS_COUNT) {
		pr_err("Unexpected number of events: %d expected: %d\n",
		       q->query_events.num_events,
		       VCAM_EVENTS_COUNT);
		return -EINVAL;
	}

	return 0;
}

static int test_compound_query(struct libkc *cam, struct libkc_query *lcq)
{
	struct libkc_iterator iter;
	struct obj_entity *entity;
	struct cam_query *q;
	int ret;
	int i;

	pr_info("Test test_compound_query()\n");

	q = libkc_query_at(lcq, 0);
	if (!q) {
		pr_err("Unable to get query\n");
		return -EINVAL;
	}

	q->query_type			= CAM_QUERY_TYPE_ENTITIES;
	q->query_entities.id		= CAM_OBJ_ID_ROOT;
	q->query_entities.maxdepth	= CAM_QUERY_ALL_OBJECTS;

	entity = entity_lookup_by_name(VCAM_FAST_IRQ_ENTITY_NAME);
	if (!entity) {
		pr_err("Unable to lookup %s entity\n",
		       VCAM_FAST_IRQ_ENTITY_NAME);
		return -EINVAL;
	}

	q = libkc_query_at(lcq, 1);
	if (!q) {
		pr_err("Unable to get query\n");
		return -EINVAL;
	}

	q->query_type			= CAM_QUERY_TYPE_EVENTS;
	q->query_events.entity		= entity->id;
	q->query_events.id		= CAM_QUERY_ALL_OBJECTS;

	entity = entity_lookup_by_name(VCAM_SLOW_IRQ_ENTITY_NAME);
	if (!entity) {
		pr_err("Unable to lookup %s entity\n",
		       VCAM_SLOW_IRQ_ENTITY_NAME);
		return -EINVAL;
	}

	q = libkc_query_at(lcq, 2);
	if (!q) {
		pr_err("Unable to get query\n");
		return -EINVAL;
	}

	q->query_type			= CAM_QUERY_TYPE_EVENTS;
	q->query_events.entity		= entity->id;
	q->query_events.id		= CAM_QUERY_ALL_OBJECTS;

	ret = libkc_query_ioctl(cam, lcq);
	if (ret)
		return ret;

	libkc_iterator_init(&lcq->hdr, &iter);
	for_each_cam_query(lcq, i, q) {
		if (q->query_type == CAM_QUERY_TYPE_ENTITIES) {
			struct cam_query_entity_entry *entry;

			if (libkc_query_num_entities(q) != VCAM_ENTITIES_COUNT) {
				pr_err("Invalid number of entities: %d\n",
				       libkc_query_num_entities(q));
				return -EINVAL;
			}

			for_each_query_entity(q, &iter, entry) {
				pr_info("Entity ID: %d, Name: %s, Parent: %d\n",
					entry->id,
					entry->name,
					entry->parent);
			}
			continue;
		}

		if (q->query_type == CAM_QUERY_TYPE_EVENTS) {
			struct cam_query_event_entry *entry;

			if (libkc_query_num_events(q) != VCAM_EVENTS_COUNT) {
				pr_err("Invalid number of events: %d\n",
				       libkc_query_num_events(q));
				return -EINVAL;
			}

			for_each_query_event(q, &iter, entry) {
				pr_info("Event ID: %d, Name: %s\n",
					entry->id,
					entry->name);
			}
			continue;
		}

		pr_err("[UNEXPECTED] type of query: %d\n", q->query_type);
		return -EINVAL;
	}

	return 0;
}

static int test_query_exact_entity(struct libkc *cam,
				   struct libkc_query *lcq)
{
	struct libkc_iterator iter;
	struct obj_entity *entity;
	struct cam_query *q;
	int ret;
	int i;

	pr_info("Test test_query_exact_entity()\n");

	entity = entity_lookup_by_name(VCAM_FAST_IRQ_ENTITY_NAME);
	if (!entity) {
		pr_err("Entity lookup has failed\n");
		return -EINVAL;
	}

	for_each_cam_query(lcq, i, q) {
		q->query_type			= CAM_QUERY_TYPE_ENTITIES;
		q->query_entities.id		= entity->id;
		q->query_entities.maxdepth	= CAM_QUERY_EXACT_OBJECT;
	}

	ret = libkc_query_ioctl(cam, lcq);
	if (ret)
		goto out;

	libkc_iterator_init(&lcq->hdr, &iter);
	for_each_cam_query(lcq, i, q) {
		struct cam_query_entity_entry *entry;

		if (q->query_type != CAM_QUERY_TYPE_ENTITIES) {
			pr_err("Unexpected query return type: %d\n",
			       q->query_type);
			ret = -EINVAL;
			goto out;
		}

		for_each_query_entity(q, &iter, entry) {
			pr_info("Entity ID: %d, Name: %s, Parent: %d\n",
				entry->id,
				entry->name,
				entry->parent);
			if (entry->id != entity->id) {
				pr_err("Unexpected Entity ID: %d, Name: %s\n",
				       entry->id,
				       entry->name);
				ret = -EINVAL;
				goto out;
			}
		}
	}
out:
	return ret;
}

static int test_query_entities(struct libkc *cam)
{
	struct libkc_query *lcq;
	int ret;

	lcq = libkc_query_get(1, CAM_DEFAULT_OUT_SZ);
	if (!lcq)
		return -EINVAL;

	ret = test_query_unknown_entity(cam, lcq);
	if (ret) {
		pr_err("FAIL: test_query_unknown_entity()\n");
		goto out;
	}

	ret = test_query_all_entities(cam, lcq, 0);
	if (!ret) {
		pr_err("FAIL: test_query_all_entities(0) should fail\n");
		ret = -EINVAL;
		goto out;
	}

	ret = test_query_all_entities(cam, lcq, CAM_QUERY_ALL_OBJECTS);
	if (ret) {
		pr_err("FAIL: test_query_all_entities(CAM_QUERY_ALL_OBJECTS)\n");
		goto out;
	}

	ret = test_query_exact_entity(cam, lcq);
	if (ret) {
		pr_err("FAIL: test_query_exact_entity()\n");
		goto out;
	}

	libkc_query_put(lcq);
	lcq = libkc_query_get(3, 0);
	if (!lcq)
		return -EINVAL;

	ret = test_compound_query_count(cam, lcq);
	if (ret) {
		pr_err("FAIL: test_compound_query_count() failed\n");
		goto out;
	}

	libkc_query_put(lcq);
	lcq = libkc_query_get(3, CAM_DEFAULT_OUT_SZ);
	if (!lcq)
		return -EINVAL;

	ret = test_compound_query(cam, lcq);
	if (ret) {
		pr_err("FAIL: test_compound_query() failed\n");
		goto out;
	}

out:
	libkc_query_put(lcq);
	return ret;
}

static int test_query_unknown_event(struct libkc *cam,
				    struct libkc_query *lcq,
				    unsigned int entity_id)
{
	struct libkc_iterator iter;
	struct cam_query *q;
	int ret;
	int i;

	pr_info("Test test_query_unknown_event() on entity: %d\n",
		entity_id);

	/* We should never have event with this ID */
	for_each_cam_query(lcq, i, q) {
		q->query_type			= CAM_QUERY_TYPE_EVENTS;
		q->query_events.entity		= entity_id;
		q->query_events.id		= 0xfffffff;
	}

	ret = libkc_query_ioctl(cam, lcq);
	if (ret) {
		/* Failure is expected here */
		ret = 0;
		goto out;
	}

	pr_err("Unexpected events\n");
	ret = -EINVAL;

	libkc_iterator_init(&lcq->hdr, &iter);
	for_each_cam_query(lcq, i, q) {
		struct cam_query_event_entry *entry;

		if (q->query_type != CAM_QUERY_TYPE_EVENTS) {
			pr_err("Unexpected query return type: %d\n",
			       q->query_type);
			ret = -EINVAL;
			goto out;
		}

		for_each_query_event(q, &iter, entry) {
			pr_info("[UNEXPECTED] Event ID: %d, Name: %s\n",
				entry->id,
				entry->name);
			ret = -EINVAL;
		}
	}

out:
	return ret;
}

static int test_query_specific_event(struct libkc *cam,
				     struct libkc_query *lcq,
				     unsigned int entity_id,
				     unsigned int event_id)
{
	struct libkc_iterator iter;
	struct cam_query *q;
	int ret;
	int i;

	pr_info("Test test_query_specific_event() event: %d on entity: %d\n",
		event_id,
		entity_id);

	/* We should never have event with this ID */
	for_each_cam_query(lcq, i, q) {
		q->query_type			= CAM_QUERY_TYPE_EVENTS;
		q->query_events.entity		= entity_id;
		q->query_events.id		= event_id;
	}

	ret = libkc_query_ioctl(cam, lcq);
	if (ret)
		goto out;

	libkc_iterator_init(&lcq->hdr, &iter);
	for_each_cam_query(lcq, i, q) {
		struct cam_query_event_entry *entry;

		if (q->query_type != CAM_QUERY_TYPE_EVENTS) {
			pr_err("Unexpected query return type: %d\n",
			       q->query_type);
			ret = -EINVAL;
			goto out;
		}

		for_each_query_event(q, &iter, entry) {
			bool ok = false;

			if (entry->id == event_id)
				ok = true;

			pr_info("[%s] Event ID: %d, Name: %s\n",
				ok ? "OK" : "UNEXPECTED",
				entry->id,
				entry->name);

			if (!ok) {
				ret = -EINVAL;
				goto out;
			}
		}
	}

out:
	return ret;
}

static int test_query_entity_events(struct libkc *cam,
				    struct libkc_query *lcq,
				    unsigned int entity_id)
{
	struct libkc_iterator iter;
	struct cam_query *q;
	int ret;
	int i;

	pr_info("Test test_query_entity_events() for entity: %d\n",
		entity_id);

	for_each_cam_query(lcq, i, q) {
		q->query_type			= CAM_QUERY_TYPE_EVENTS;
		q->query_events.entity		= entity_id;
		q->query_events.id		= CAM_QUERY_ALL_OBJECTS;
	}

	ret = libkc_query_ioctl(cam, lcq);
	if (ret)
		goto out;

	libkc_iterator_init(&lcq->hdr, &iter);
	for_each_cam_query(lcq, i, q) {
		struct cam_query_event_entry *entry;

		if (q->query_type != CAM_QUERY_TYPE_EVENTS) {
			pr_err("Unexpected query return type: %d\n",
			       q->query_type);
			ret = -EINVAL;
			goto out;
		}

		for_each_query_event(q, &iter, entry) {
			pr_info("Event ID: %d, Name: %s\n",
				entry->id,
				entry->name);

			ret = event_register(entry, entity_id);
			if (ret)
				goto out;
		}
	}

out:
	return ret;
}

static int test_query_events(struct libkc *cam)
{
	struct libkc_query *lcq;
	struct obj_entity *entity;
	int ret;

	pr_info("Test test_query_events()\n");
	lcq = libkc_query_get(1, CAM_DEFAULT_OUT_SZ);
	if (!lcq)
		return -EINVAL;

	list_for_each_entry(entity, &entities, obj_list) {
		struct obj_event *child;

		ret = test_query_unknown_event(cam, lcq, entity->id);
		if (ret)
			break;

		ret = test_query_entity_events(cam, lcq, entity->id);
		if (ret)
			break;

		list_for_each_entry(child, &entity->children, parent_entry) {
			if (child->type != OBJ_TYPE_EVENT)
				continue;

			ret = test_query_specific_event(cam,
							lcq,
							entity->id,
							child->id);
			if (ret)
				goto out;
		}
	}

out:
	libkc_query_put(lcq);
	return ret;
}

#define TEST_NUM_OPERATIONS	3
#define TEST_NUM_RW_OPERATIONS	2

static int read_operations_completion_events(struct libkc *cam, u32 num_events)
{
	struct cam_completion *completion;
	struct libkc_completion *lcc;
	int ret;
	int i;

	lcc = libkc_completion_get(num_events);
	if (!lcc)
		return -ENOMEM;

	ret = libkc_completion_read(cam, lcc);
	if (ret < 0)
		goto out;

	for_each_cam_completion(lcc, i, completion) {
		pr_info("Completion event timestamp: %llu id: %u\n",
			completion->ts,
			completion->id);
		ret++;
	}

	if (ret != num_events) {
		pr_err("FATAL: did not read expected number "
		       "of completion events: %d (expected %d)\n",
		       ret,
		       num_events);
		ret = -EINVAL;
	}

out:
	libkc_completion_put(lcc);
	return ret;
}

/*
 * We depend on slow entity flush timings which are critical for query/remove
 * tests. vcamtest execution and vcam driver initialization are independent
 * steps and vcamtest tests that use slow triggering entity race with vcam
 * timeouts. So we need to start executing tests in the beginning of slow
 * timer interval, which should give us enough time to complete all the tests.
 */
static int wait_for_slow_entity_timer(struct libkc *cam)
{
	struct libkc_operation *lco;
	struct obj_entity *entity;
	struct cam_operation *op;
	struct obj_event *event;
	int ret;
	int i;

	pr_info("wait_for_slow_entity_timer()\n");

	entity = entity_lookup_by_name(VCAM_SLOW_IRQ_ENTITY_NAME);
	if (!entity) {
		pr_err("Entity lookup has failed\n");
		return -EINVAL;
	}

	event = entity_first_event(entity);
	if (!event) {
		pr_err("Unable to find event\n");
		return -EINVAL;
	}

	lco = libkc_operation_get(1);
	if (!lco)
		return -EINVAL;

	for_each_cam_operation(lco, i, op) {
		op->operation_type		= CAM_OPERATION_TYPE_ADD;
		op->operation_add.id		= i;
		op->operation_add.fence_out	= 0;
		op->operation_add.flags		= 0;
		op->operation_add.delay_ns	= 0;
		op->operation_add.rd_wr_list	= CAM_NO_RD_WR;
		op->operation_add.entity	= entity->id;

		op->operation_add.mode		= CAM_DEPENDENCY_WEAK_ORDER;
		op->operation_add.deps[0].type	= CAM_DEPENDENCY_EVENT;
		op->operation_add.deps[0].id	= event->id;
	}

	ret = libkc_operation_ioctl(cam, lco);
	if (ret) {
		pr_err("FATAL: failed to add operation: %d\n", ret);
		goto out;
	}

	MAY_EXIT_AT();

	ret = read_operations_completion_events(cam, 1);
	if (ret != 1) {
		pr_err("FATAL: unexpected completion read error: %d\n", ret);
		ret = -EINVAL;
	} else {
		pr_info("OK: Synced with slow entity timer flush\n");
		ret = 0;
	}

out:
	libkc_operation_put(lco);
	return ret;
}

static int add_single_invalid_operation(struct libkc *cam,
					struct libkc_operation *lco,
					u32 id_dep,
					u32 event_entity,
					u32 event_id,
					u32 fence_in)
{
	struct cam_operation *op;
	int d = 0;
	int i;

	pr_info("Test add_single_invalid_operation(): "
		"id_dep: %x entity: %x event: %x fence_in: %x\n",
		id_dep, event_entity, event_id, fence_in);

	for_each_cam_operation(lco, i, op) {
		op->operation_type		= CAM_OPERATION_TYPE_ADD;
		op->operation_add.id		= i;
		op->operation_add.fence_out	= 0;
		op->operation_add.flags		= 0;
		op->operation_add.delay_ns	= 0;
		op->operation_add.rd_wr_list	= CAM_NO_RD_WR;
		op->operation_add.entity	= event_entity;
		op->operation_add.mode		= CAM_DEPENDENCY_WEAK_ORDER;

		if (id_dep != CAM_NO_DEP) {
			op->operation_add.deps[d].type	= CAM_DEPENDENCY_OP;
			op->operation_add.deps[d].id	= id_dep;
			d++;
		}

		if (event_id != CAM_NO_ENTITY) {
			op->operation_add.deps[d].type	= CAM_DEPENDENCY_EVENT;
			op->operation_add.deps[d].id	= event_id;
			d++;
		}

		if (fence_in != CAM_NO_FENCE) {
			op->operation_add.deps[d].type	= CAM_DEPENDENCY_FENCE_IN;
			op->operation_add.deps[d].id	= fence_in;
			d++;
		}
	}

	return libkc_operation_ioctl(cam, lco);
}

static int add_many_invalid_operations(struct libkc *cam,
				       struct libkc_operation *lco)
{
	struct obj_entity *entity;
	struct cam_operation *op;
	struct obj_event *event;
	int i, ret;

	pr_info("Test add_many_invalid_operations()\n");

	entity = entity_lookup_by_name(VCAM_SLOW_IRQ_ENTITY_NAME);
	if (!entity) {
		pr_err("Entity lookup has failed\n");
		return -EINVAL;
	}

	event = entity_first_event(entity);
	if (!event) {
		pr_err("Unable to find event\n");
		return -EINVAL;
	}

	for_each_cam_operation(lco, i, op) {
		op->operation_type		= CAM_OPERATION_TYPE_ADD;
		op->operation_add.id		= i;
		op->operation_add.fence_out	= 0;
		op->operation_add.flags		= 0;
		op->operation_add.delay_ns	= 0;
		op->operation_add.rd_wr_list	= CAM_NO_RD_WR;
		op->operation_add.mode		= CAM_DEPENDENCY_WEAK_ORDER;
		op->operation_add.entity	= entity->id;

		/*
		 * The first operation is blocked on entity event, and is
		 * competely valid.
		 *
		 * The second operation is blocked on the first one, has
		 * valid entity/event dependency but invalid fence_in
		 * dependency. We test DEP_OP/DEP_EVENT signals rollback
		 * here.
		 */
		if (i == 0) {
			op->operation_add.deps[0].type	= CAM_DEPENDENCY_EVENT;
			op->operation_add.deps[0].id	= event->id;
		} else {
			op->operation_add.rd_wr_list	= 0xC0FFEE;

			op->operation_add.deps[0].type	= CAM_DEPENDENCY_OP;
			op->operation_add.deps[0].id	= i - 1;

			op->operation_add.deps[1].type	= CAM_DEPENDENCY_EVENT;
			op->operation_add.deps[1].id	= event->id;

			op->operation_add.deps[2].type	= CAM_DEPENDENCY_FENCE_IN;
			op->operation_add.deps[2].id	= 255;
		}
	}

	ret = libkc_operation_ioctl(cam, lco);
	/* Otherwise we will free(0xC0FFEE) in libkc_operation_put() */
	for_each_cam_operation(lco, i, op) {
		op->operation_add.rd_wr_list		= CAM_NO_RD_WR;
	}
	return ret;
}

static int test_add_invalid_operations(struct libkc *cam)
{
	struct libkc_operation *lco;
	struct cam_operation *op;
	int ret;
	int i;

	lco = libkc_operation_get(1);
	if (!lco)
		return -ENOMEM;

	ret = add_single_invalid_operation(cam, lco, 255, CAM_NO_ENTITY,
					   CAM_NO_ENTITY, CAM_NO_FENCE);
	if (ret) {
		pr_err("FATAL: unmet operation dependency should succeed\n");
		goto out;
	}

	ret = read_operations_completion_events(cam, 1);
	if (ret != 1) {
		pr_err("FATAL: unmet operation dependency should succeed\n");
		goto out;
	}

	ret = add_single_invalid_operation(cam, lco, CAM_NO_DEP,
					   255, CAM_NO_ENTITY, CAM_NO_FENCE);
	if (!ret) {
		pr_err("FATAL: unmet entity dependency should fail\n");
		ret = -EINVAL;
		goto out;
	}

	ret = add_single_invalid_operation(cam, lco, CAM_NO_DEP,
					   CAM_NO_ENTITY, 255, CAM_NO_FENCE);
	if (!ret) {
		pr_err("FATAL: unmet event dependency should fail\n");
		ret = -EINVAL;
		goto out;
	}

	ret = add_single_invalid_operation(cam, lco, CAM_NO_DEP, 255, 255,
					   CAM_NO_FENCE);
	if (!ret) {
		pr_err("FATAL: unmet entity:event dependency should fail\n");
		ret = -EINVAL;
		goto out;
	}

	ret = add_single_invalid_operation(cam, lco, 255, 255, 255,
					   CAM_NO_FENCE);
	if (!ret) {
		pr_err("FATAL: unmet operation:entity:event dependency "
		       "should fail\n");
		ret = -EINVAL;
		goto out;
	}

	ret = add_single_invalid_operation(cam, lco, CAM_NO_DEP, CAM_NO_ENTITY,
					   CAM_NO_ENTITY, 255);
	if (!ret) {
		pr_err("FATAL: unmet fence_in dependency should fail\n");
		ret = -EINVAL;
		goto out;
	}

	libkc_operation_put(lco);
	lco = libkc_operation_get(2);
	if (!lco)
		return -ENOMEM;

	ret = add_many_invalid_operations(cam, lco);
	if (!ret) {
		pr_err("FATAL: unmet fence_in depency should fail\n");
		ret = -EINVAL;
		goto out;
	}

	ret = read_operations_completion_events(cam, 1);
	if (ret != 1) {
		pr_err("FATAL: first operation should complete\n");
		ret = -EINVAL;
		goto out;
	}

	ret = 0;
out:
	libkc_operation_put(lco);
	return ret;
}

static int test_add_instant_operations(struct libkc *cam, u32 num_ops)
{
	struct libkc_operation *lco;
	struct cam_operation *op;
	int ret;
	int i;

	pr_info("Test test_add_instant_operations()\n");

	lco = libkc_operation_get(num_ops);
	if (!lco)
		return -EINVAL;

	for_each_cam_operation(lco, i, op) {
		op->operation_type		= CAM_OPERATION_TYPE_ADD;
		op->operation_add.id		= i;
		op->operation_add.fence_out	= 0;
		op->operation_add.flags		= 0;
		op->operation_add.delay_ns	= 0;
		op->operation_add.rd_wr_list	= CAM_NO_RD_WR;
		op->operation_add.mode		= CAM_DEPENDENCY_WEAK_ORDER;
	}

	ret = libkc_operation_ioctl(cam, lco);
	libkc_operation_put(lco);
	return ret;
}

static int test_add_valid_operations(struct libkc *cam,
				     const char *entity_name,
				     u32 num_ops,
				     u32 mode)
{
	static char *modes[] = {"weak", "strict"};
	struct libkc_operation *lco;
	struct obj_entity *entity;
	struct cam_operation *op;
	struct obj_event *event;
	int ret;
	int i;

	pr_info("Test test_add_valid_operations() entity: %s mode: %s\n",
		entity_name, modes[mode]);

	entity = entity_lookup_by_name(entity_name);
	if (!entity) {
		pr_err("Entity lookup has failed\n");
		return -EINVAL;
	}

	event = entity_first_event(entity);
	if (!event) {
		pr_err("Unable to find event\n");
		return -EINVAL;
	}

	lco = libkc_operation_get(num_ops);
	if (!lco)
		return -EINVAL;

	for_each_cam_operation(lco, i, op) {
		op->operation_type		= CAM_OPERATION_TYPE_ADD;
		op->operation_add.id		= i;
		op->operation_add.fence_out	= 0;
		op->operation_add.flags		= 0;
		op->operation_add.delay_ns	= 0;
		op->operation_add.rd_wr_list	= CAM_NO_RD_WR;
		op->operation_add.mode		= mode;

		/*
		 * The first operation is blocked on entity event. The rest
		 * of operations are blocked on the first one.
		 */
		if (i == 0) {
			op->operation_add.entity	= entity->id;

			op->operation_add.deps[0].type	= CAM_DEPENDENCY_EVENT;
			op->operation_add.deps[0].id	= event->id;
		} else {
			op->operation_add.deps[0].type	= CAM_DEPENDENCY_OP;
			op->operation_add.deps[0].id	= i - 1;
		}
	}

	ret = libkc_operation_ioctl(cam, lco);

	MAY_EXIT_AT();

	libkc_operation_put(lco);
	return ret;
}

static int test_add_valid_complex_operations(struct libkc *cam,
					     const char *entity_name,
					     u32 num_ops)
{
	struct libkc_operation *lco;
	struct obj_entity *entity;
	struct cam_operation *op;
	struct obj_event *event;
	int ret;
	int i;

	pr_info("Test test_add_valid_complex_operations() entity: %s\n",
		entity_name);

	entity = entity_lookup_by_name(entity_name);
	if (!entity) {
		pr_err("Entity lookup has failed\n");
		return -EINVAL;
	}

	event = entity_first_event(entity);
	if (!event) {
		pr_err("Unable to find event\n");
		return -EINVAL;
	}

	lco = libkc_operation_get(num_ops);
	if (!lco)
		return -EINVAL;

	for_each_cam_operation(lco, i, op) {
		op->operation_type		= CAM_OPERATION_TYPE_ADD;
		op->operation_add.id		= i;
		op->operation_add.fence_out	= 0;
		op->operation_add.flags		= 0;
		op->operation_add.delay_ns	= 0;
		op->operation_add.rd_wr_list	= CAM_NO_RD_WR;
		op->operation_add.mode		= CAM_DEPENDENCY_STRICT_ORDER;

		/*
		 * The first operation is blocked on entity event.
		 * The second is blocked on the first one.
		 * The third one is blocked on the second and then on the
		 * first (reverse order in strict mode).
		 */
		if (i == 0) {
			op->operation_add.entity	= entity->id;

			op->operation_add.deps[0].type	= CAM_DEPENDENCY_EVENT;
			op->operation_add.deps[0].id	= event->id;
			continue;
		}

		if (i == 1) {
			op->operation_add.deps[0].type	= CAM_DEPENDENCY_OP;
			op->operation_add.deps[0].id	= 0;
			continue;
		}

		if (i == 2) {
			op->operation_add.deps[0].type	= CAM_DEPENDENCY_OP;
			op->operation_add.deps[0].id	= 1;
			op->operation_add.deps[1].type	= CAM_DEPENDENCY_OP;
			op->operation_add.deps[1].id	= 0;
			continue;
		}
	}

	ret = libkc_operation_ioctl(cam, lco);
	libkc_operation_put(lco);
	return ret;
}

#define READ_BUFFER_POISON	"POISON"
#define READ_BUFFER_SIZE	64

static int test_add_valid_rw_operations(struct libkc *cam,
					const char *entity_name,
					u32 num_ops)
{
	char write_buffer[] = "From vcamtest";
	struct cam_rw_instruction *rw;
	struct libkc_rw_list *rw_list;
	struct libkc_operation *lco;
	struct obj_entity *entity;
	struct cam_operation *op;
	struct obj_event *event;
	int op_idx, rw_idx;
	char *read_buffer;
	int ret;

	pr_info("Test test_add_valid_rw_operations() entity: %s\n",
		entity_name);

	entity = entity_lookup_by_name(entity_name);
	if (!entity) {
		pr_err("Entity lookup has failed\n");
		return -EINVAL;
	}

	event = entity_first_event(entity);
	if (!event) {
		pr_err("Unable to find event\n");
		return -EINVAL;
	}

	lco = libkc_operation_get(num_ops);
	if (!lco)
		return -EINVAL;

	read_buffer = calloc(1, READ_BUFFER_SIZE);
	if (!read_buffer) {
		pr_err("Unable to allocate read buffer\n");
		return -ENOMEM;
	}

	strcpy(read_buffer, READ_BUFFER_POISON);

	for_each_cam_operation(lco, op_idx, op) {
		op->operation_type		= CAM_OPERATION_TYPE_ADD;
		op->operation_add.id		= op_idx;
		op->operation_add.fence_out	= 0;
		op->operation_add.flags		= 0;
		op->operation_add.delay_ns	= 0;
		op->operation_add.entity	= entity->id;
		op->operation_add.mode		= CAM_DEPENDENCY_WEAK_ORDER;

		rw_list = libkc_rw_list_get(2);
		if (!rw_list) {
			ret = -ENOMEM;
			goto out;
		}

		op->operation_add.rd_wr_list	= (uint64_t)rw_list;

		/*
		 * The first operation is blocked on entity event. The rest
		 * of operations are blocked on the first one.
		 */
		if (op_idx == 0) {
			op->operation_add.deps[0].type	= CAM_DEPENDENCY_EVENT;
			op->operation_add.deps[0].id	= event->id;
			/*
			 * RW 0 is register read
			 */
			for_each_rw_instruction(rw_list, rw_idx, rw) {
				rw->type	= CAM_READ_INSTRUCTION;
				rw->rd.reg	= 42;
				rw->rd.size	= READ_BUFFER_SIZE;
				rw->rd.ptr	= (uint64_t)read_buffer;
			}
			continue;
		}

		op->operation_add.deps[0].type	= CAM_DEPENDENCY_OP;
		op->operation_add.deps[0].id	= op_idx - 1;

		if (op_idx == 1) {
			/*
			 * RW 1 is register write
			 */
			for_each_rw_instruction(rw_list, rw_idx, rw) {
				rw->type	= CAM_WRITE_INSTRUCTION;
				rw->wr.reg	= 42;
				rw->wr.size	= sizeof(write_buffer);
				rw->wr.ptr	= (uint64_t)write_buffer;
			}
			continue;
		}
	}

	ret = libkc_operation_ioctl(cam, lco);
	if (ret)
		goto out;

	MAY_EXIT_AT();

	ret = read_operations_completion_events(cam, num_ops);
	if (ret != TEST_NUM_RW_OPERATIONS) {
		pr_err("FATAL: read_operations_completion_events() failed\n");
		ret = -EINVAL;
		goto out;
	} else {
		ret = 0;
	}

	ret = 0;
	if (!strcmp(read_buffer, READ_BUFFER_POISON)) {
		ret = -EINVAL;
		pr_err("REGISTER_READ invalid value: '%s'\n", read_buffer);
	} else {
		pr_info("RW: write buffer content: %s\n", write_buffer);
		pr_info("RW: read buffer content: %s\n", read_buffer);
	}

out:
	libkc_operation_put(lco);
	free(read_buffer);
	return ret;
}

static int test_add_invalid_rw_num_entries(struct libkc *cam,
					   const char *entity_name)
{
	struct cam_rw_instruction *rw;
	struct libkc_rw_list *rw_list;
	struct libkc_operation *lco;
	struct obj_entity *entity;
	struct cam_operation *op;
	struct obj_event *event;
	int op_idx, rw_idx;
	char *read_buffer;
	int ret;

	pr_info("Test test_add_invalid_rw_num_entries() entity: %s\n",
		entity_name);

	entity = entity_lookup_by_name(entity_name);
	if (!entity) {
		pr_err("Entity lookup has failed\n");
		return -EINVAL;
	}

	event = entity_first_event(entity);
	if (!event) {
		pr_err("Unable to find event\n");
		return -EINVAL;
	}

	lco = libkc_operation_get(1);
	if (!lco)
		return -EINVAL;

	for_each_cam_operation(lco, op_idx, op) {
		op->operation_type		= CAM_OPERATION_TYPE_ADD;
		op->operation_add.id		= op_idx;
		op->operation_add.fence_out	= 0;
		op->operation_add.flags		= 0;
		op->operation_add.delay_ns	= 0;
		op->operation_add.entity	= entity->id;
		op->operation_add.mode		= CAM_DEPENDENCY_WEAK_ORDER;

		rw_list = libkc_rw_list_get(1);
		if (!rw_list) {
			ret = -ENOMEM;
			goto out;
		}

		op->operation_add.rd_wr_list	= (uint64_t)rw_list;
	}

	rw_list->num_ents = 0;
	ret = libkc_operation_ioctl(cam, lco);
	rw_list->num_ents = 1;

	if (ret) {
		ret = 0;
	} else {
		pr_err("RW-list with zero num_entries should fail\n");
		ret = -EINVAL;
	}

out:
	libkc_operation_put(lco);
	return ret;
}

static int test_query_operations(struct libkc *cam, u32 id, u32 mode)
{
	struct libkc_query *lcq;
	struct libkc_iterator iter;
	struct cam_query *q;
	char *mode_name;
	int ret;
	int i;

	switch (mode) {
	case CAM_OP_QUERY_ALL:
		mode_name = "CAM_OP_QUERY_ALL";
		break;
	case CAM_OP_QUERY_SLEEP:
		mode_name = "CAM_OP_QUERY_SLEEP";
		break;
	case CAM_OP_QUERY_QUEUED:
		mode_name = "CAM_OP_QUERY_QUEUED";
		break;
	case CAM_OP_QUERY_UNIQUE:
		mode_name = "CAM_OP_QUERY_UNIQUE";
		break;
	case CAM_OP_QUERY_UNIQUE_AND_DEPS:
		mode_name = "CAM_OP_QUERY_UNIQUE_AND_DEPS";
		break;
	default:
		mode_name = "Unknown CAM_OP_QUERY mode";
		break;
	}

	pr_info("Test test_query_operations(%x, %s)\n", id, mode_name);

	lcq = libkc_query_get(1, CAM_DEFAULT_OUT_SZ);
	if (!lcq)
		return -EINVAL;

	for_each_cam_query(lcq, i, q) {
		q->query_type			= CAM_QUERY_TYPE_OPERATIONS;
		q->query_operations.id		= id;
		q->query_operations.mode	= mode;
	}

	ret = libkc_query_ioctl(cam, lcq);
	if (ret)
		goto out;

	/*
	 * Note this function returns negative error value of the number
	 * of queries entries.
	 */
	ret = 0;

	libkc_iterator_init(&lcq->hdr, &iter);
	for_each_cam_query(lcq, i, q) {
		struct cam_query_operation_entry *entry;

		if (q->query_type != CAM_QUERY_TYPE_OPERATIONS) {
			pr_err("Unexpected query return type: %d\n",
			       q->query_type);
			ret = -EINVAL;
			goto out;
		}

		for_each_query_operation(q, &iter, entry) {
			pr_info("Operation ID: %d\n", entry->id);
			ret++;
		}
	}

out:
	libkc_query_put(lcq);
	return ret;
}

static int test_remove_operations(struct libkc *cam)
{
	struct libkc_operation *lco;
	struct libkc_iterator iter;
	struct cam_operation *op;
	int ret;
	int i;

	pr_info("Test test_remove_operations()\n");

	lco = libkc_operation_get(1);
	if (!lco)
		return -EINVAL;

	for_each_cam_operation(lco, i, op) {
		op->operation_type		= CAM_OPERATION_TYPE_REMOVE;
		op->operation_remove.id		= i;
		op->operation_remove.mode	= CAM_REMOVE_RECURSIVE;
	}

	ret = libkc_operation_ioctl(cam, lco);
	libkc_operation_put(lco);
	return ret;
}

static int test_operations(struct libkc *cam)
{
	int ret;

	ret = wait_for_slow_entity_timer(cam);
	if (ret) {
		pr_err("FATAL: can't sync with slow entity timer\n");
		return ret;
	}

	ret = test_add_invalid_operations(cam);
	if (ret) {
		pr_err("FATAL: failure test_add_invalid_operations()\n");
		return ret;
	}

	ret = test_add_instant_operations(cam, TEST_NUM_OPERATIONS);
	if (ret) {
		pr_err("FATAL: failure test_add_instant_operations()\n");
		return ret;
	}

	ret = read_operations_completion_events(cam, TEST_NUM_OPERATIONS);
	if (ret != TEST_NUM_OPERATIONS) {
		pr_err("FATAL: read_operations_completion_events() failed\n");
		return -EINVAL;
	}

	ret = test_add_valid_operations(cam, VCAM_FAST_IRQ_ENTITY_NAME,
					TEST_NUM_OPERATIONS,
					CAM_DEPENDENCY_WEAK_ORDER);
	if (ret) {
		pr_err("FATAL: failure test_add_valid_operations()\n");
		return ret;
	}

	ret = read_operations_completion_events(cam, TEST_NUM_OPERATIONS);
	if (ret != TEST_NUM_OPERATIONS) {
		pr_err("FATAL: read_operations_completion_events() failed\n");
		return -EINVAL;
	}

	ret = test_add_valid_operations(cam, VCAM_FAST_IRQ_ENTITY_NAME,
					TEST_NUM_OPERATIONS,
					CAM_DEPENDENCY_STRICT_ORDER);
	if (ret) {
		pr_err("FATAL: failure test_add_valid_operations()\n");
		return ret;
	}

	ret = read_operations_completion_events(cam, TEST_NUM_OPERATIONS);
	if (ret != TEST_NUM_OPERATIONS) {
		pr_err("FATAL: read_operations_completion_events() failed\n");
		return -EINVAL;
	}

	ret = test_add_valid_complex_operations(cam, VCAM_SLOW_IRQ_ENTITY_NAME,
						TEST_NUM_OPERATIONS);
	if (ret) {
		pr_err("FATAL: test_add_valid_complex_operations() failed\n");
		return ret;
	}

	ret = test_query_operations(cam, 0, CAM_OP_QUERY_UNIQUE);
	if (ret != 1) {
		pr_err("FATAL: failure test_query_operation(): %d\n", ret);
		ret = -EINVAL;
		return ret;
	}

	ret = test_query_operations(cam, 0, CAM_OP_QUERY_UNIQUE_AND_DEPS);
	if (ret != 3) {
		pr_err("FATAL: failure test_query_operation(): %d\n", ret);
		ret = -EINVAL;
		return ret;
	}

	ret = read_operations_completion_events(cam, TEST_NUM_OPERATIONS);
	if (ret != TEST_NUM_OPERATIONS) {
		pr_err("FATAL: read_operations_completion_events() failed\n");
		return -EINVAL;
	}

	ret = test_add_invalid_rw_num_entries(cam, VCAM_FAST_IRQ_ENTITY_NAME);
	if (ret) {
		pr_err("FATAL: failure test_add_invalid_rw_num_entries()\n");
		return ret;
	}

	ret = test_add_valid_rw_operations(cam, VCAM_FAST_IRQ_ENTITY_NAME,
					   TEST_NUM_RW_OPERATIONS);
	if (ret) {
		pr_err("FATAL: failure test_add_valid_rw_operations()\n");
		return ret;
	}

	ret = test_add_valid_operations(cam, VCAM_SLOW_IRQ_ENTITY_NAME,
					TEST_NUM_OPERATIONS,
					CAM_DEPENDENCY_STRICT_ORDER);
	if (ret) {
		pr_err("FATAL: failure test_add_valid_operations()\n");
		return ret;
	}

	ret = test_query_operations(cam, CAM_OP_ID_ALL_OP, CAM_OP_QUERY_ALL);
	if (ret <= 0) {
		pr_err("FATAL: failure test_query_operations()\n");
		return ret;
	}

	ret = test_query_operations(cam, CAM_OP_ID_ALL_OP, CAM_OP_QUERY_SLEEP);
	if (ret <= 0) {
		pr_err("FATAL: failure test_query_operations()\n");
		return ret;
	}

	ret = test_query_operations(cam, CAM_OP_ID_ALL_OP, CAM_OP_QUERY_QUEUED);
	if (ret != 0) {
		pr_err("FATAL: failure test_query_operations()\n");
		return ret;
	}

	ret = test_remove_operations(cam);
	if (ret) {
		pr_err("FATAL: failure test_remove_operations()\n");
		return ret;
	}

	ret = read_operations_completion_events(cam, TEST_NUM_OPERATIONS);
	if (ret != TEST_NUM_OPERATIONS) {
		pr_err("FATAL: read_operations_completion_events() failed\n");
		return -EINVAL;
	}

	return 0;
}

static int test_export_import_operations(struct libkc *cam)
{
	struct libkc_operation *lco;
	struct obj_entity *entity;
	struct cam_operation *op;
	struct obj_event *event;
	int fence_out;
	int ret;
	int i;

	pr_info("Test fence export/import\n");

	entity = entity_lookup_by_name(VCAM_SLOW_IRQ_ENTITY_NAME);
	if (!entity)
		return -EINVAL;

	event = entity_first_event(entity);
	if (!event)
		return -EINVAL;

	lco = libkc_operation_get(1);
	if (!lco)
		return -EINVAL;

	op = libkc_operation_at(lco, 0);
	if (!op) {
		ret = EINVAL;
		goto out;
	}

	op->operation_type		= CAM_OPERATION_TYPE_ADD;
	op->operation_add.id		= 0;
	op->operation_add.fence_out	= 0;
	op->operation_add.flags		= CAM_OPERATION_FLAG_EXPORT_FENCE;
	op->operation_add.delay_ns	= 8888;
	op->operation_add.rd_wr_list	= CAM_NO_RD_WR;
	op->operation_add.entity	= entity->id;

	op->operation_add.mode		= CAM_DEPENDENCY_WEAK_ORDER;
	op->operation_add.deps[0].type	= CAM_DEPENDENCY_EVENT;
	op->operation_add.deps[0].id	= event->id;

	ret = libkc_operation_ioctl(cam, lco);
	if (ret) {
		pr_err("FATAL: failed to add operation: %d\n", ret);
		goto out;
	}

	if (op->operation_add.fence_out == CAM_NO_FENCE) {
		pr_err("Unexpected fence out fd value: %d\n",
		       op->operation_add.fence_out);
		ret = -EINVAL;
		goto out;
	}

	pr_info("Export syncfile fd: %d\n", op->operation_add.fence_out);
	fence_out = op->operation_add.fence_out;

	libkc_operation_put(lco);
	lco = libkc_operation_get(3);
	if (!lco)
		return -EINVAL;

	for_each_cam_operation(lco, i, op) {
		op->operation_type		= CAM_OPERATION_TYPE_ADD;
		op->operation_add.id		= i + 1;
		op->operation_add.fence_out	= 0;
		op->operation_add.flags		= 0;
		op->operation_add.delay_ns	= 0;
		op->operation_add.rd_wr_list	= CAM_NO_RD_WR;
		op->operation_add.entity	= entity->id;

		op->operation_add.mode		= CAM_DEPENDENCY_WEAK_ORDER;
		op->operation_add.deps[0].type	= CAM_DEPENDENCY_FENCE_IN;
		op->operation_add.deps[0].id	= fence_out;
	}

	ret = libkc_operation_ioctl(cam, lco);
	if (ret) {
		pr_err("FATAL: failed to add operation: %d\n", ret);
		goto out;
	}

	MAY_EXIT_AT();

	ret = read_operations_completion_events(cam, 4);
	if (ret != 4) {
		pr_err("FATAL: unexpected completion read error: %d\n", ret);
		ret = -EINVAL;
		goto out;
	} else {
		ret = 0;
	}
	close(fence_out);
out:
	libkc_operation_put(lco);
	return ret;
}

static int test_add_buffers(struct libkc *cam)
{
	struct vcam_dmabuf_instruction insn;
	struct libkc_operation *lco = NULL;
	struct libkc_dmabuf *buf = NULL;
	struct cam_rw_instruction *rw;
	struct libkc_rw_list *rw_list;
	struct obj_entity *entity;
	struct cam_operation *op;
	int ret;

	pr_info("Test ADD buffers\n");

	entity = entity_lookup_by_name(VCAM_DMA_IMPORT_ENTITY_NAME);
	if (!entity) {
		pr_err("Unable to lookup `%s` entity\n",
		       VCAM_DMA_IMPORT_ENTITY_NAME);
		return -EINVAL;
	}

	buf = libkc_dmabuf_get(cam, 4);
	if (!buf) {
		pr_err("Failed to create buffer\n");
		ret = -EINVAL;
		goto out;
	}

	lco = libkc_operation_get(1);
	if (!lco) {
		ret = -EINVAL;
		goto out;
	}

	op = libkc_operation_at(lco, 0);
	if (!op) {
		ret = -EINVAL;
		goto out;
	}

	op->operation_type		= CAM_OPERATION_TYPE_ADD;
	op->operation_add.id		= 1;
	op->operation_add.fence_out	= 0;
	op->operation_add.flags		= 0;
	op->operation_add.delay_ns	= 0;
	op->operation_add.entity	= entity->id;
	op->operation_add.mode		= CAM_DEPENDENCY_WEAK_ORDER;

	rw_list = libkc_rw_list_get(1);
	if (!rw_list) {
		ret = -ENOMEM;
		goto out;
	}

	op->operation_add.rd_wr_list	= (uint64_t)rw_list;
	rw = libkc_rw_instruction_at(rw_list, 0);
	if (!rw) {
		ret = -EINVAL;
		goto out;
	}

	insn.type	= VCAM_DMABUF_ADD;
	insn.fd		= buf->fd;

	rw->type	= CAM_WRITE_INSTRUCTION;
	rw->wr.reg	= 0;
	rw->wr.size	= sizeof(insn);
	rw->wr.ptr	= (uint64_t)&insn;

	ret = libkc_operation_ioctl(cam, lco);
	if (ret)
		goto out;

	MAY_EXIT_AT();

	ret = read_operations_completion_events(cam, 1);
	if (ret != 1) {
		pr_err("Invalid number of completions %d\n", ret);
		ret = -EINVAL;
		goto out;
	}

	ret = buffer_register(entity, buf);
	if (ret) {
		pr_err("Failed to register buffer-%d\n", buf->fd);
		ret = -EINVAL;
		goto out;
	}
out:
	libkc_operation_put(lco);
	return ret;
}

static int test_remove_buffer(struct libkc *cam, struct obj_buffer *buf)
{
	struct vcam_dmabuf_instruction insn;
	struct libkc_operation *lco = NULL;
	struct cam_rw_instruction *rw;
	struct libkc_rw_list *rw_list;
	struct obj_entity *entity;
	struct cam_operation *op;
	int ret;

	pr_info("Test REMOVE buffers\n");

	entity = entity_lookup_by_name(VCAM_DMA_IMPORT_ENTITY_NAME);
	if (!entity) {
		pr_err("Unable to lookup `%s` entity\n",
		       VCAM_DMA_IMPORT_ENTITY_NAME);
		return -EINVAL;
	}

	lco = libkc_operation_get(1);
	if (!lco) {
		ret = -EINVAL;
		goto out;
	}

	op = libkc_operation_at(lco, 0);
	if (!op) {
		ret = -EINVAL;
		goto out;
	}

	op->operation_type		= CAM_OPERATION_TYPE_ADD;
	op->operation_add.id		= 2;
	op->operation_add.fence_out	= 0;
	op->operation_add.flags		= 0;
	op->operation_add.delay_ns	= 0;
	op->operation_add.entity	= entity->id;
	op->operation_add.mode		= CAM_DEPENDENCY_WEAK_ORDER;

	rw_list = libkc_rw_list_get(1);
	if (!rw_list) {
		ret = -ENOMEM;
		goto out;
	}

	op->operation_add.rd_wr_list	= (uint64_t)rw_list;
	rw = libkc_rw_instruction_at(rw_list, 0);
	if (!rw) {
		ret = -EINVAL;
		goto out;
	}

	insn.type	= VCAM_DMABUF_REMOVE;
	insn.fd		= buf->id;

	rw->type	= CAM_WRITE_INSTRUCTION;
	rw->wr.reg	= 0;
	rw->wr.size	= sizeof(insn);
	rw->wr.ptr	= (uint64_t)&insn;

	ret = libkc_operation_ioctl(cam, lco);
	if (ret)
		goto out;

	ret = read_operations_completion_events(cam, 1);
	if (ret != 1) {
		pr_err("Invalid number of completions %d\n", ret);
		ret = -EINVAL;
		goto out;
	} else {
		ret = 0;
	}
out:
	libkc_operation_put(lco);
	return ret;
}

static int test_remove_buffers(struct libkc *cam)
{
	struct obj_buffer *buf;
	int ret;

	ret = -EINVAL;
	while (!list_empty(&buffers)) {
		buf = list_first_entry(&buffers, struct obj_buffer, obj_list);

		ret = test_remove_buffer(cam, buf);
		if (ret)
			goto out;
		buffer_unregister(buf);
	}
out:
	return ret;
}

int main(int argc, char *argv[])
{
	static struct option long_options[] = {
		{ "exit_at", required_argument, 0, 'e' },
		{ 0, 0, 0, 0 }
	};

	struct libkc *cam;
	const char *cam_path = "/dev/cam";
	int option_index = 0;
	int opt;
	int ret;

	log_level = PR_DEBUG;

	while (1) {
		opt = getopt_long_only(argc, argv, "e:h", long_options, &option_index);

		if (opt == -1)
			break;

		switch (opt) {
		case 'e':
			parse_exit_at_function(optarg);
			break;
		case 'h':
			show_exit_at_functions();
			break;
		}
	}

	cam = libkc_open(cam_path);
	if (!cam) {
		pr_err("FATAL: cannot open %s\n", cam_path);
		return -EINVAL;
	}

	if (libkc_open(cam_path)) {
		pr_err("FATAL: more than one active CAM users\n");
		return -EINVAL;
	}

	ret = test_query_entities(cam);
	if (ret) {
		pr_err("FATAL: failure test_query_entities()\n");
		return ret;
	}

	ret = test_query_events(cam);
	if (ret) {
		pr_err("FATAL: failure test_query_events()\n");
		return ret;
	}

	ret = test_operations(cam);
	if (ret) {
		pr_err("FATAL: failure test_operations()\n");
		return ret;
	}

	ret = test_export_import_operations(cam);
	if (ret) {
		pr_err("FATAL: failure test_export_import_operations()\n");
		return ret;
	}

	ret = test_add_buffers(cam);
	if (ret) {
		pr_err("FATAL: failure test_add_buffers()\n");
		return ret;
	}

	ret = test_remove_buffers(cam);
	if (ret) {
		pr_err("FATAL: failure test_remove_buffers()\n");
		return ret;
	}

	libkc_close(cam);
	return EXIT_SUCCESS;
}
