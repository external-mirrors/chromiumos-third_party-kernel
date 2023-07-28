/* SPDX-License-Identifier: GPL-2.0 */
/*
 * libkc and VCAM test tool
 *
 * Copyright (C) Google LLC
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include <libkc/libkc.h>

/* @FIXME */
#include "../../../include/uapi/linux/vcam.h"

#define CAM_NO_DEP	0xffffffff

#define CAM_DEFAULT_OUT_SZ	4096

#define VCAM_FAST_IRQ_INSTANCE_ID	1
#define VCAM_SLOW_IRQ_INSTANCE_ID	100

static char *exit_at_functions[] = {
	"wait_for_slow_entity_timer",
	"test_add_valid_operations",
	"test_add_valid_rw_operations",
	"test_export_import_operations",
	"test_add_buffer"
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

			ret = libkc_entity_register(cam, entry);
			if (ret)
				goto out;
		}
	}
out:
	return ret;
}

#define VCAM_ENTITIES_COUNT	4
#define VCAM_EVENTS_COUNT	1

static int test_compound_query_count(struct libkc *cam,
				     struct libkc_query *lcq)
{
	struct obj_entity *entity;
	struct cam_query *q;
	int ret;
	int i;

	pr_info("Test test_compound_query_count()\n");

	q = libkc_query_at(lcq, 0);

	q->query_type			= CAM_QUERY_TYPE_ENTITIES;
	q->query_entities.id		= CAM_OBJ_ID_ROOT;
	q->query_entities.maxdepth	= CAM_QUERY_ALL_OBJECTS;

	entity = libkc_entity_lookup_by_name(cam, VCAM_FAST_IRQ_ENTITY_NAME);
	if (!entity) {
		pr_err("Unable to lookup %s entity\n",
		       VCAM_FAST_IRQ_ENTITY_NAME);
		return -EINVAL;
	}

	q = libkc_query_at(lcq, 1);

	q->query_type			= CAM_QUERY_TYPE_EVENTS;
	q->query_events.entity		= entity->id;
	q->query_events.id		= CAM_QUERY_ALL_OBJECTS;

	entity = libkc_entity_lookup_by_name(cam, VCAM_SLOW_IRQ_ENTITY_NAME);
	if (!entity) {
		pr_err("Unable to lookup %s entity\n",
		       VCAM_SLOW_IRQ_ENTITY_NAME);
		return -EINVAL;
	}

	q = libkc_query_at(lcq, 2);

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

	q->query_type			= CAM_QUERY_TYPE_ENTITIES;
	q->query_entities.id		= CAM_OBJ_ID_ROOT;
	q->query_entities.maxdepth	= CAM_QUERY_ALL_OBJECTS;

	entity = libkc_entity_lookup_by_name(cam, VCAM_FAST_IRQ_ENTITY_NAME);
	if (!entity) {
		pr_err("Unable to lookup %s entity\n",
		       VCAM_FAST_IRQ_ENTITY_NAME);
		return -EINVAL;
	}

	q = libkc_query_at(lcq, 1);

	q->query_type			= CAM_QUERY_TYPE_EVENTS;
	q->query_events.entity		= entity->id;
	q->query_events.id		= CAM_QUERY_ALL_OBJECTS;

	entity = libkc_entity_lookup_by_name(cam, VCAM_SLOW_IRQ_ENTITY_NAME);
	if (!entity) {
		pr_err("Unable to lookup %s entity\n",
		       VCAM_SLOW_IRQ_ENTITY_NAME);
		return -EINVAL;
	}

	q = libkc_query_at(lcq, 2);

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

	entity = libkc_entity_lookup_by_name(cam, VCAM_FAST_IRQ_ENTITY_NAME);
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

			ret = libkc_event_register(cam, entry, entity_id);
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

	list_for_each_entry(entity, &cam->entities, obj_list) {
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
		pr_info("Completion seqno: %llu id: %u\n",
			completion->seqno,
			completion->id);
		if (completion->seqno - cam->completion_seqno > 1) {
			pr_err("Lost %llu completion events\n",
			       completion->seqno - cam->completion_seqno);
			ret = -EINVAL;
			goto out;
		}
		cam->completion_seqno++;
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

	entity = libkc_entity_lookup_by_name(cam, VCAM_SLOW_IRQ_ENTITY_NAME);
	if (!entity) {
		pr_err("Entity lookup has failed\n");
		return -EINVAL;
	}

	event = libkc_entity_first_event(entity);
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
		op->operation_add.delay_ns	= 0;
		op->operation_add.rd_wr_list	= CAM_OP_NO_RW_LIST;
		op->operation_add.entity	= entity->id;
		op->operation_add.instance	= CAM_OP_NO_INSTANCE;

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

	op = libkc_operation_at(lco, 0);

	op->operation_type		= CAM_OPERATION_TYPE_ADD;
	op->operation_add.id		= 0;
	op->operation_add.delay_ns	= 0;
	op->operation_add.rd_wr_list	= CAM_OP_NO_RW_LIST;
	op->operation_add.entity	= event_entity;
	op->operation_add.instance	= CAM_OP_NO_INSTANCE;
	op->operation_add.mode		= CAM_DEPENDENCY_WEAK_ORDER;

	if (id_dep != CAM_NO_DEP) {
		op->operation_add.deps[d].type	= CAM_DEPENDENCY_OP;
		op->operation_add.deps[d].id	= id_dep;
		d++;
	}

	if (event_id != CAM_OP_NO_ENTITY) {
		op->operation_add.deps[d].type	= CAM_DEPENDENCY_EVENT;
		op->operation_add.deps[d].id	= event_id;
		d++;
	}

	if (fence_in != CAM_OP_NO_FENCE) {
		op->operation_add.deps[d].type	= CAM_DEPENDENCY_FENCE_IN;
		op->operation_add.deps[d].id	= fence_in;
		d++;
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

	entity = libkc_entity_lookup_by_name(cam, VCAM_SLOW_IRQ_ENTITY_NAME);
	if (!entity) {
		pr_err("Entity lookup has failed\n");
		return -EINVAL;
	}

	event = libkc_entity_first_event(entity);
	if (!event) {
		pr_err("Unable to find event\n");
		return -EINVAL;
	}

	for_each_cam_operation(lco, i, op) {
		op->operation_type		= CAM_OPERATION_TYPE_ADD;
		op->operation_add.id		= i;
		op->operation_add.delay_ns	= 0;
		op->operation_add.rd_wr_list	= CAM_OP_NO_RW_LIST;
		op->operation_add.mode		= CAM_DEPENDENCY_WEAK_ORDER;
		op->operation_add.entity	= entity->id;
		op->operation_add.instance	= CAM_OP_NO_INSTANCE;

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
		op->operation_add.rd_wr_list		= CAM_OP_NO_RW_LIST;
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

	ret = add_single_invalid_operation(cam, lco, 255, CAM_OP_NO_ENTITY,
					   CAM_OP_NO_ENTITY, CAM_OP_NO_FENCE);
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
					   255, CAM_OP_NO_ENTITY,
					   CAM_OP_NO_FENCE);
	if (!ret) {
		pr_err("FATAL: unmet entity dependency should fail\n");
		ret = -EINVAL;
		goto out;
	}

	ret = add_single_invalid_operation(cam, lco, CAM_NO_DEP,
					   CAM_OP_NO_ENTITY, 255,
					   CAM_OP_NO_FENCE);
	if (!ret) {
		pr_err("FATAL: unmet event dependency should fail\n");
		ret = -EINVAL;
		goto out;
	}

	ret = add_single_invalid_operation(cam, lco, CAM_NO_DEP, 255, 255,
					   CAM_OP_NO_FENCE);
	if (!ret) {
		pr_err("FATAL: unmet entity:event dependency should fail\n");
		ret = -EINVAL;
		goto out;
	}

	ret = add_single_invalid_operation(cam, lco, 255, 255, 255,
					   CAM_OP_NO_FENCE);
	if (!ret) {
		pr_err("FATAL: unmet operation:entity:event dependency "
		       "should fail\n");
		ret = -EINVAL;
		goto out;
	}

	ret = add_single_invalid_operation(cam, lco, CAM_NO_DEP,
					   CAM_OP_NO_ENTITY,
					   CAM_OP_NO_ENTITY, 255);
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
		op->operation_add.delay_ns	= 0;
		op->operation_add.entity	= CAM_OP_NO_ENTITY;
		op->operation_add.instance	= CAM_OP_NO_INSTANCE;
		op->operation_add.rd_wr_list	= CAM_OP_NO_RW_LIST;
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

	entity = libkc_entity_lookup_by_name(cam, entity_name);
	if (!entity) {
		pr_err("Entity lookup has failed\n");
		return -EINVAL;
	}

	event = libkc_entity_first_event(entity);
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
		op->operation_add.delay_ns	= 0;
		op->operation_add.rd_wr_list	= CAM_OP_NO_RW_LIST;
		op->operation_add.mode		= mode;
		op->operation_add.instance	= CAM_OP_NO_INSTANCE;

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

	entity = libkc_entity_lookup_by_name(cam, entity_name);
	if (!entity) {
		pr_err("Entity lookup has failed\n");
		return -EINVAL;
	}

	event = libkc_entity_first_event(entity);
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
		op->operation_add.delay_ns	= 0;
		op->operation_add.rd_wr_list	= CAM_OP_NO_RW_LIST;
		op->operation_add.mode		= CAM_DEPENDENCY_STRICT_ORDER;
		op->operation_add.instance	= CAM_OP_NO_INSTANCE;

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
					u32 instance_id,
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

	entity = libkc_entity_lookup_by_name(cam, entity_name);
	if (!entity) {
		pr_err("Entity lookup has failed\n");
		return -EINVAL;
	}

	event = libkc_entity_first_event(entity);
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
		op->operation_add.delay_ns	= 0;
		op->operation_add.entity	= entity->id;
		op->operation_add.instance	= instance_id;
		op->operation_add.mode		= CAM_DEPENDENCY_WEAK_ORDER;
		op->operation_add.rd_wr_list	= CAM_OP_NO_RW_LIST;

		/*
		 * The first operation is instance write, the second
		 * operation is blocked on instance event. The rest
		 * of operations are blocked on previous operations.
		 */
		if (op_idx == 0) {
			op->operation_add.instance	= instance_id;

			rw_list = libkc_rw_list_get(1);
			if (!rw_list) {
				ret = -ENOMEM;
				goto out;
			}

			op->operation_add.rd_wr_list	= (uint64_t)rw_list;
			rw = libkc_rw_instruction_at(rw_list, 0);

			rw->type		= CAM_WRITE_INSTRUCTION;
			rw->error		= 0;
			rw->wr.reg		= 42;
			rw->wr.size		= sizeof(write_buffer);
			rw->wr.num_buffers	= 0;
			rw->wr.buffers_list	= 0x00;
			rw->wr.ptr		= (uint64_t)write_buffer;
			continue;
		}

		op->operation_add.deps[0].type	= CAM_DEPENDENCY_OP;
		op->operation_add.deps[0].id	= op_idx - 1;

		if (op_idx == 1) {
			op->operation_add.instance	= instance_id;
			op->operation_add.deps[1].type	= CAM_DEPENDENCY_EVENT;
			op->operation_add.deps[1].id	= event->id;

			rw_list = libkc_rw_list_get(1);
			if (!rw_list) {
				ret = -ENOMEM;
				goto out;
			}

			op->operation_add.rd_wr_list	= (uint64_t)rw_list;
			rw = libkc_rw_instruction_at(rw_list, 0);

			rw->type		= CAM_READ_INSTRUCTION;
			rw->error		= 0;
			rw->rd.reg		= 42;
			rw->rd.size		= READ_BUFFER_SIZE;
			rw->rd.num_buffers	= 0;
			rw->rd.buffers_list	= 0x00;
			rw->rd.ptr		= (uint64_t)read_buffer;
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

	for_each_cam_operation(lco, op_idx, op) {
		rw_idx = 0;
		if (libkc_failed_instruction(op, &rw_idx)) {
			ret = -EINVAL;
			goto out;
		}
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
					   const char *entity_name,
					   u32 instance_id)
{
	struct cam_rw_instruction *rw;
	struct libkc_rw_list *rw_list;
	struct libkc_operation *lco;
	struct obj_entity *entity;
	struct cam_operation *op;
	struct obj_event *event;
	int rw_idx;
	char *read_buffer;
	int ret;

	pr_info("Test test_add_invalid_rw_num_entries() entity: %s\n",
		entity_name);

	entity = libkc_entity_lookup_by_name(cam, entity_name);
	if (!entity) {
		pr_err("Entity lookup has failed\n");
		return -EINVAL;
	}

	event = libkc_entity_first_event(entity);
	if (!event) {
		pr_err("Unable to find event\n");
		return -EINVAL;
	}

	lco = libkc_operation_get(1);
	if (!lco)
		return -EINVAL;

	op = libkc_operation_at(lco, 0);

	op->operation_type		= CAM_OPERATION_TYPE_ADD;
	op->operation_add.id		= 0;
	op->operation_add.delay_ns	= 0;
	op->operation_add.entity	= entity->id;
	op->operation_add.instance	= CAM_OP_NO_INSTANCE;
	op->operation_add.mode		= CAM_DEPENDENCY_WEAK_ORDER;

	rw_list = libkc_rw_list_get(1);
	if (!rw_list) {
		ret = -ENOMEM;
		goto out;
	}

	op->operation_add.rd_wr_list	= (uint64_t)rw_list;

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

static int test_add_too_many_rw_instructions(struct libkc *cam,
					     const char *entity_name,
					     u32 instance_id)
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

	pr_info("Test test_add_too_many_rw_instructions() entity: %s\n",
		entity_name);

	entity = libkc_entity_lookup_by_name(cam, entity_name);
	if (!entity) {
		pr_err("Entity lookup has failed\n");
		return -EINVAL;
	}

	event = libkc_entity_first_event(entity);
	if (!event) {
		pr_err("Unable to find event\n");
		return -EINVAL;
	}

	lco = libkc_operation_get(1);
	if (!lco)
		return -EINVAL;

	op = libkc_operation_at(lco, 0);

	op->operation_type		= CAM_OPERATION_TYPE_ADD;
	op->operation_add.id		= op_idx;
	op->operation_add.delay_ns	= 0;
	op->operation_add.entity	= entity->id;
	op->operation_add.instance	= instance_id;
	op->operation_add.mode		= CAM_DEPENDENCY_WEAK_ORDER;

	rw_list = libkc_rw_list_get(1);
	if (!rw_list) {
		ret = -ENOMEM;
		goto out;
	}

	op->operation_add.rd_wr_list	= (uint64_t)rw_list;

	rw_list->num_ents = 0xC0FFEE;
	ret = libkc_operation_ioctl(cam, lco);
	rw_list->num_ents = 1;

	if (ret) {
		ret = 0;
	} else {
		pr_err("RW-list with too many entries should fail\n");
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

static int test_remove_operations(struct libkc *cam, u32 num_operations)
{
	struct libkc_operation *lco;
	struct libkc_iterator iter;
	struct cam_operation *op;
	int ret;
	int i;

	pr_info("Test test_remove_operations()\n");

	lco = libkc_operation_get(num_operations);
	if (!lco)
		return -EINVAL;

	for_each_cam_operation(lco, i, op) {
		op->operation_type		= CAM_OPERATION_TYPE_REMOVE;
		op->operation_remove.id		= i;
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

	ret = test_add_instant_operations(cam, 4096 * TEST_NUM_OPERATIONS);
	if (!ret) {
		pr_err("FATAL: very large IOCTL payload should fail\n");
		ret = -EINVAL;
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

	ret = test_add_invalid_rw_num_entries(cam, VCAM_FAST_IRQ_ENTITY_NAME,
					      VCAM_FAST_IRQ_INSTANCE_ID);
	if (ret) {
		pr_err("FATAL: failure test_add_invalid_rw_num_entries()\n");
		return ret;
	}

	ret = test_add_too_many_rw_instructions(cam, VCAM_FAST_IRQ_ENTITY_NAME,
						VCAM_FAST_IRQ_INSTANCE_ID);
	if (ret) {
		pr_err("FATAL: failure test_add_too_many_rw_instructions()\n");
		return ret;
	}

	ret = test_add_valid_rw_operations(cam, VCAM_FAST_IRQ_ENTITY_NAME,
					   VCAM_FAST_IRQ_INSTANCE_ID,
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

	ret = test_remove_operations(cam, TEST_NUM_OPERATIONS);
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
	struct cam_rw_instruction *rw;
	struct libkc_rw_list *rw_list;
	struct libkc_operation *lco;
	struct obj_entity *entity;
	struct cam_operation *op;
	struct obj_event *event;
	int fence_out;
	int ret, i, rw_idx;

	pr_info("Test fence export/import\n");

	entity = libkc_entity_lookup_by_name(cam, VCAM_SLOW_IRQ_ENTITY_NAME);
	if (!entity)
		return -EINVAL;

	event = libkc_entity_first_event(entity);
	if (!event)
		return -EINVAL;

	lco = libkc_operation_get(1);
	if (!lco)
		return -EINVAL;

	op = libkc_operation_at(lco, 0);

	op->operation_type		= CAM_OPERATION_TYPE_ADD;
	op->operation_add.id		= 0;
	op->operation_add.delay_ns	= 8888;
	op->operation_add.entity	= entity->id;
	op->operation_add.instance	= CAM_OP_NO_INSTANCE;
	op->operation_add.mode		= CAM_DEPENDENCY_WEAK_ORDER;
	op->operation_add.deps[0].type	= CAM_DEPENDENCY_EVENT;
	op->operation_add.deps[0].id	= event->id;

	rw_list = libkc_rw_list_get(1);
	if (!rw_list) {
		ret = -ENOMEM;
		goto out;
	}

	op->operation_add.rd_wr_list	= (uint64_t)rw_list;
	rw = libkc_rw_instruction_at(rw_list, 0);

	rw->type			= CAM_OUT_FENCE_INSTRUCTION;
	rw->error			= 0;

	ret = libkc_operation_ioctl(cam, lco);
	if (ret) {
		pr_err("FATAL: failed to add operation: %d\n", ret);
		goto out;
	}

	ret = read_operations_completion_events(cam, 1);
	if (ret != 1) {
		pr_err("FATAL: unexpected completion read error: %d\n", ret);
		ret = -EINVAL;
		goto out;
	} else {
		ret = 0;
	}

	if (rw->of.id == CAM_OP_NO_FENCE) {
		pr_err("Unexpected fence out fd value: %d\n", rw->of.id);
		ret = -EINVAL;
		goto out;
	}

	pr_info("Export syncfile fd: %d\n", rw->of.id);
	fence_out = rw->of.id;

	libkc_operation_put(lco);
	lco = libkc_operation_get(3);
	if (!lco)
		return -EINVAL;

	for_each_cam_operation(lco, i, op) {
		op->operation_type		= CAM_OPERATION_TYPE_ADD;
		op->operation_add.id		= i + 1;
		op->operation_add.delay_ns	= 0;
		op->operation_add.rd_wr_list	= CAM_OP_NO_RW_LIST;
		op->operation_add.entity	= entity->id;
		op->operation_add.instance	= CAM_OP_NO_INSTANCE;

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

	ret = read_operations_completion_events(cam, 3);
	if (ret != 3) {
		pr_err("FATAL: unexpected completion read error: %d\n", ret);
		ret = -EINVAL;
		goto out;
	} else {
		ret = 0;
	}

	for_each_cam_operation(lco, i, op) {
		rw_idx = 0;
		if (libkc_failed_instruction(op, &rw_idx)) {
			ret = -EINVAL;
			break;
		}
	}

	close(fence_out);
out:
	libkc_operation_put(lco);
	return ret;
}

static int test_compound_buffer_operations(struct libkc *cam)
{
	struct libkc_buffers_list *buf_list = NULL;
	struct libkc_operation *lco = NULL;
	struct cam_rw_instruction *rw;
	struct libkc_rw_list *rw_list;
	struct obj_entity *entity;
	struct cam_operation *op;
	char *read_buffer;
	int ret, op_idx, rw_idx;

	pr_info("Test compound buffer operations\n");

	entity = libkc_entity_lookup_by_name(cam, VCAM_FAST_IRQ_ENTITY_NAME);
	if (!entity) {
		pr_err("Unable to lookup `%s` entity\n",
		       VCAM_FAST_IRQ_ENTITY_NAME);
		return -EINVAL;
	}

	buf_list = libkc_buffers_list_get(cam, 2, 2);
	if (!buf_list) {
		pr_err("Failed to create buffers\n");
		ret = -EINVAL;
		goto out;
	}

	lco = libkc_operation_get(3);
	if (!lco) {
		ret = -EINVAL;
		goto out;
	}

	read_buffer = calloc(1, READ_BUFFER_SIZE);
	if (!read_buffer) {
		ret = -ENOMEM;
		goto out;
	}

	/* Import (ADD) buffer */
	op = libkc_operation_at(lco, 0);

	op->operation_type		= CAM_OPERATION_TYPE_ADD;
	op->operation_add.id		= 1;
	op->operation_add.delay_ns	= 0;
	op->operation_add.entity	= entity->id;
	op->operation_add.instance	= CAM_OP_NO_INSTANCE;
	op->operation_add.mode		= CAM_DEPENDENCY_WEAK_ORDER;

	rw_list = libkc_rw_list_get(2);
	if (!rw_list) {
		ret = -ENOMEM;
		goto out;
	}

	op->operation_add.rd_wr_list	= (uint64_t)rw_list;
	rw = libkc_rw_instruction_at(rw_list, 0);

	rw->type		= CAM_DMABUF_INSTRUCTION;
	rw->error		= 0;
	rw->db.op		= CAM_OP_DMABUF_ADD;
	rw->db.dma_fd		= buf_list->bufs[0]->fd;
	rw->db.buf_id		= 1;
	buf_list->ids[0]	= 1;

	rw = libkc_rw_instruction_at(rw_list, 1);

	rw->type		= CAM_DMABUF_INSTRUCTION;
	rw->error		= 0;
	rw->db.op		= CAM_OP_DMABUF_ADD;
	rw->db.dma_fd		= buf_list->bufs[1]->fd;
	rw->db.buf_id		= 2;
	buf_list->ids[1]	= 2;

	/* Use imported buffer */
	op = libkc_operation_at(lco, 1);

	op->operation_type		= CAM_OPERATION_TYPE_ADD;
	op->operation_add.id		= 2;
	op->operation_add.delay_ns	= 0;
	op->operation_add.entity	= entity->id;
	op->operation_add.instance	= VCAM_FAST_IRQ_INSTANCE_ID;
	op->operation_add.mode		= CAM_DEPENDENCY_STRICT_ORDER;
	op->operation_add.deps[0].type	= CAM_DEPENDENCY_OP;
	op->operation_add.deps[0].id	= 1;

	rw_list = libkc_rw_list_get(1);
	if (!rw_list) {
		ret = -ENOMEM;
		goto out;
	}

	op->operation_add.rd_wr_list	= (uint64_t)rw_list;
	rw = libkc_rw_instruction_at(rw_list, 0);

	rw->type		= CAM_READ_INSTRUCTION;
	rw->error		= 0;
	rw->rd.reg		= 42;
	rw->rd.size		= READ_BUFFER_SIZE;
	rw->rd.num_buffers	= buf_list->size;
	rw->rd.buffers_list	= (uint64_t)buf_list->ids;
	rw->rd.ptr		= (uint64_t)read_buffer;

	/* Release (REMOVE) buffer */
	op = libkc_operation_at(lco, 2);

	op->operation_type		= CAM_OPERATION_TYPE_ADD;
	op->operation_add.id		= 3;
	op->operation_add.delay_ns	= 0;
	op->operation_add.entity	= entity->id;
	op->operation_add.instance	= CAM_OP_NO_INSTANCE;
	op->operation_add.mode		= CAM_DEPENDENCY_STRICT_ORDER;
	op->operation_add.deps[0].type	= CAM_DEPENDENCY_OP;
	op->operation_add.deps[0].id	= 2;

	rw_list = libkc_rw_list_get(2);
	if (!rw_list) {
		ret = -ENOMEM;
		goto out;
	}

	op->operation_add.rd_wr_list	= (uint64_t)rw_list;
	rw = libkc_rw_instruction_at(rw_list, 0);

	rw->type	= CAM_DMABUF_INSTRUCTION;
	rw->error	= 0;
	rw->db.op	= CAM_OP_DMABUF_REMOVE;
	rw->db.dma_fd	= CAM_DMABUF_INSTRUCTION_NO_BUFFER;
	rw->db.buf_id	= buf_list->ids[0];

	rw = libkc_rw_instruction_at(rw_list, 1);

	rw->type	= CAM_DMABUF_INSTRUCTION;
	rw->error	= 0;
	rw->db.op	= CAM_OP_DMABUF_REMOVE;
	rw->db.dma_fd	= CAM_DMABUF_INSTRUCTION_NO_BUFFER;
	rw->db.buf_id	= buf_list->ids[1];

	ret = libkc_operation_ioctl(cam, lco);
	if (ret)
		goto out;

	ret = read_operations_completion_events(cam, 3);
	if (ret != 3) {
		pr_err("Invalid number of completions %d\n", ret);
		ret = -EINVAL;
		goto out;
	} else {
		ret = 0;
	}

	for_each_cam_operation(lco, op_idx, op) {
		rw_idx = 0;
		if (libkc_failed_instruction(op, &rw_idx)) {
			ret = -EINVAL;
			break;
		}
	}

out:
	libkc_buffers_list_put(buf_list);
	libkc_operation_put(lco);
	free(read_buffer);
	return ret;
}

static int test_add_buffer_cancellation(struct libkc *cam)
{
	struct libkc_operation *lco = NULL;
	struct libkc_dmabuf *buf = NULL;
	struct cam_rw_instruction *rw;
	struct libkc_rw_list *rw_list;
	struct obj_entity *entity;
	struct cam_operation *op;
	int ret, rw_idx;

	pr_info("Test ADD buffer cancellation\n");

	entity = libkc_entity_lookup_by_name(cam, VCAM_FAST_IRQ_ENTITY_NAME);
	if (!entity) {
		pr_err("Unable to lookup `%s` entity\n",
		       VCAM_FAST_IRQ_ENTITY_NAME);
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

	op->operation_type		= CAM_OPERATION_TYPE_ADD;
	op->operation_add.id		= 1;
	op->operation_add.delay_ns	= 0;
	op->operation_add.entity	= entity->id;
	op->operation_add.instance	= CAM_OP_NO_INSTANCE;
	op->operation_add.mode		= CAM_DEPENDENCY_WEAK_ORDER;

	rw_list = libkc_rw_list_get(2);
	if (!rw_list) {
		ret = -ENOMEM;
		goto out;
	}

	op->operation_add.rd_wr_list	= (uint64_t)rw_list;
	/* Conflicting buffer ID */
	for_each_rw_instruction(rw_list, rw_idx, rw) {
		rw->type	= CAM_DMABUF_INSTRUCTION;
		rw->error	= 0;
		rw->db.op	= CAM_OP_DMABUF_ADD;
		rw->db.dma_fd	= buf->fd;
		rw->db.buf_id	= 1;
	}

	ret = libkc_operation_ioctl(cam, lco);
	if (ret == 0) {
		pr_err("Conflicting buffer ID test should fail %d\n", ret);
		ret = -EINVAL;
	} else {
		ret = 0;
	}

	rw_idx = 0;
	if (!libkc_failed_instruction(op, &rw_idx)) {
		pr_err("Instruction error code is not set\n");
		ret = -EINVAL;
	}

out:
	libkc_dmabuf_put(buf);
	libkc_operation_put(lco);
	return ret;
}

static int test_buffer_enumeration(struct libkc *cam)
{
	struct libkc_operation *lco = NULL;
	struct libkc_dmabuf *buf = NULL;
	struct cam_rw_instruction *rw;
	struct libkc_rw_list *rw_list;
	struct libkc_iterator iter;
	struct obj_entity *entity;
	struct cam_operation *op;
	struct libkc_query *lcq;
	struct cam_query *q;
	int ret, op_idx, rw_idx;

	pr_info("Test buffer enumeration\n");

	entity = libkc_entity_lookup_by_name(cam, VCAM_FAST_IRQ_ENTITY_NAME);
	if (!entity) {
		pr_err("Unable to lookup `%s` entity\n",
		       VCAM_FAST_IRQ_ENTITY_NAME);
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

	/* Import (ADD) buffer */
	op = libkc_operation_at(lco, 0);

	op->operation_type		= CAM_OPERATION_TYPE_ADD;
	op->operation_add.id		= 1;
	op->operation_add.delay_ns	= 0;
	op->operation_add.entity	= entity->id;
	op->operation_add.instance	= CAM_OP_NO_INSTANCE;
	op->operation_add.mode		= CAM_DEPENDENCY_WEAK_ORDER;

	rw_list = libkc_rw_list_get(1);
	if (!rw_list) {
		ret = -ENOMEM;
		goto out;
	}

	op->operation_add.rd_wr_list	= (uint64_t)rw_list;
	rw = libkc_rw_instruction_at(rw_list, 0);

	rw->type	= CAM_DMABUF_INSTRUCTION;
	rw->error	= 0;
	rw->db.op	= CAM_OP_DMABUF_ADD;
	rw->db.dma_fd	= buf->fd;
	rw->db.buf_id	= 101;

	ret = libkc_operation_ioctl(cam, lco);
	if (ret)
		goto out;

	ret = read_operations_completion_events(cam, 1);
	if (ret != 1) {
		ret = -EINVAL;
		goto out;
	}

	lcq = libkc_query_get(1, CAM_DEFAULT_OUT_SZ);
	if (!lcq) {
		ret = -ENOMEM;
		goto out;
	}

	q = libkc_query_at(lcq, 0);

	/* Query valid DMA-buffer fd */
	q->query_type			= CAM_QUERY_TYPE_DMABUF;
	q->query_dmabuf.fd		= buf->fd;

	ret = libkc_query_ioctl(cam, lcq);
	if (ret)
		goto out;

	libkc_iterator_init(&lcq->hdr, &iter);
	for_each_cam_query(lcq, op_idx, q) {
		struct cam_query_dmabuf_entry *entry;

		if (q->query_type != CAM_QUERY_TYPE_DMABUF) {
			pr_err("Unexpected query return type: %d\n",
			       q->query_type);
			ret = -EINVAL;
			goto out;
		}

		for_each_query_dmabuf(q, &iter, entry) {
			pr_info("DMA-buffer ID: %d\n", entry->id);
			if (entry->id != 101) {
				pr_err("Unexpected DMA-buffer ID\n");
				ret = -EINVAL;
				goto out;
			}
		}
	}

	/* Test invalid DMA-buffer fd */
	q = libkc_query_at(lcq, 0);

	q->query_type		= CAM_QUERY_TYPE_DMABUF;
	q->query_dmabuf.fd	= 101;

	ret = libkc_query_ioctl(cam, lcq);
	if (ret == 0) {
		ret = -EINVAL;
		goto out;
	}

	/* Release (REMOVE) buffer */
	op->operation_type		= CAM_OPERATION_TYPE_ADD;
	op->operation_add.id		= 1;
	op->operation_add.delay_ns	= 0;
	op->operation_add.entity	= entity->id;
	op->operation_add.instance	= CAM_OP_NO_INSTANCE;
	op->operation_add.mode		= CAM_DEPENDENCY_WEAK_ORDER;

	rw->type	= CAM_DMABUF_INSTRUCTION;
	rw->error	= 0;
	rw->db.op	= CAM_OP_DMABUF_REMOVE;
	rw->db.dma_fd	= CAM_DMABUF_INSTRUCTION_NO_BUFFER;
	rw->db.buf_id	= 101;

	ret = libkc_operation_ioctl(cam, lco);
	if (ret)
		goto out;

	ret = read_operations_completion_events(cam, 1);
	if (ret != 1) {
		ret = -EINVAL;
		goto out;
	} else {
		ret = 0;
	}

out:
	libkc_dmabuf_put(buf);
	libkc_operation_put(lco);
	libkc_query_put(lcq);
	return ret;
}

static int test_entity_instance_avail_limit(struct libkc *cam)
{
	struct libkc_operation *lco = NULL;
	struct cam_rw_instruction *rw;
	struct libkc_rw_list *rw_list;
	struct obj_entity *entity;
	struct cam_operation *op;
	int ret, rw_idx;

	pr_info("Test entity instances_avail limit\n");

	entity = libkc_entity_lookup_by_name(cam, VCAM_FAST_IRQ_ENTITY_NAME);
	if (!entity) {
		pr_err("Unable to lookup `%s` entity\n",
		       VCAM_FAST_IRQ_ENTITY_NAME);
		return -EINVAL;
	}

	lco = libkc_operation_get(1);
	if (!lco) {
		ret = -EINVAL;
		goto out;
	}

	op = libkc_operation_at(lco, 0);

	op->operation_type		= CAM_OPERATION_TYPE_ADD;
	op->operation_add.id		= 1;
	op->operation_add.delay_ns	= 0;
	op->operation_add.entity	= entity->id;
	op->operation_add.instance	= CAM_OP_NO_INSTANCE;
	op->operation_add.mode		= CAM_DEPENDENCY_WEAK_ORDER;

	rw_list = libkc_rw_list_get(101);
	op->operation_add.rd_wr_list	= (uint64_t)rw_list;
	if (!rw_list) {
		ret = -ENOMEM;
		goto out;
	}

	for_each_rw_instruction(rw_list, rw_idx, rw) {
		rw->type	= CAM_INSTANCE_INSTRUCTION;
		rw->error	= 0;
		rw->in.op	= CAM_OP_INSTANCE_CREATE;
		rw->in.id	= rw_idx;
	}

	ret = libkc_operation_ioctl(cam, lco);
	if (ret)
		ret = 0;
	else
		ret = -EINVAL;

	rw_idx = 0;
	if (!libkc_failed_instruction(op, &rw_idx)) {
		pr_err("Instruction error code is not set\n");
		ret = -EINVAL;
	}

out:
	libkc_operation_put(lco);
	return ret;
}

static int test_create_entity_instance(struct libkc *cam,
				       const char *entity_name,
				       u32 instance_id)
{
	struct libkc_operation *lco = NULL;
	struct cam_rw_instruction *rw;
	struct libkc_rw_list *rw_list;
	struct obj_entity *entity;
	struct obj_event *event;
	struct cam_operation *op;
	int ret, op_idx, rw_idx;

	pr_info("Test create entity instance\n");

	entity = libkc_entity_lookup_by_name(cam, entity_name);
	if (!entity) {
		pr_err("Unable to lookup `%s` entity\n", entity_name);
		return -EINVAL;
	}

	event = libkc_entity_first_event(entity);
	if (!event) {
		pr_err("Unable to find event\n");
		return -EINVAL;
	}

	lco = libkc_operation_get(1);
	if (!lco) {
		ret = -EINVAL;
		goto out;
	}

	op = libkc_operation_at(lco, 0);

	op->operation_type		= CAM_OPERATION_TYPE_ADD;
	op->operation_add.id		= 1;
	op->operation_add.delay_ns	= 0;
	op->operation_add.entity	= entity->id;
	op->operation_add.instance	= CAM_OP_NO_INSTANCE;
	op->operation_add.mode		= CAM_DEPENDENCY_WEAK_ORDER;

	rw_list = libkc_rw_list_get(1);
	op->operation_add.rd_wr_list	= (uint64_t)rw_list;
	if (!rw_list) {
		ret = -ENOMEM;
		goto out;
	}

	rw = libkc_rw_instruction_at(rw_list, 0);

	rw->type	= CAM_INSTANCE_INSTRUCTION;
	rw->error	= 0;
	rw->in.op	= CAM_OP_INSTANCE_CREATE;
	rw->in.id	= instance_id;

	ret = libkc_operation_ioctl(cam, lco);
	if (ret)
		goto out;

	ret = read_operations_completion_events(cam, 1);
	if (ret != 1) {
		pr_err("FATAL: read_operations_completion_events() failed\n");
		ret = -EINVAL;
		goto out;
	} else {
		ret = 0;
	}

	for_each_cam_operation(lco, op_idx, op) {
		rw_idx = 0;
		if (libkc_failed_instruction(op, &rw_idx)) {
			ret = -EINVAL;
			goto out;
		}
	}

out:
	libkc_operation_put(lco);
	return ret;
}

static int test_compound_instance_operations(struct libkc *cam)
{
	char write_buffer[] = "From vcamtest";
	struct libkc_operation *lco = NULL;
	struct cam_rw_instruction *rw;
	struct libkc_rw_list *rw_list;
	struct obj_entity *entity;
	struct obj_event *event;
	struct cam_operation *op;
	int ret, op_idx, rw_idx;

	pr_info("Test compound entity instance\n");

	entity = libkc_entity_lookup_by_name(cam, VCAM_FAST_IRQ_ENTITY_NAME);
	if (!entity) {
		pr_err("Unable to lookup `%s` entity\n",
		       VCAM_FAST_IRQ_ENTITY_NAME);
		return -EINVAL;
	}

	event = libkc_entity_first_event(entity);
	if (!event) {
		pr_err("Unable to find event\n");
		return -EINVAL;
	}

	lco = libkc_operation_get(3);
	if (!lco) {
		ret = -EINVAL;
		goto out;
	}

	op = libkc_operation_at(lco, 0);

	op->operation_type		= CAM_OPERATION_TYPE_ADD;
	op->operation_add.id		= 1;
	op->operation_add.delay_ns	= 0;
	op->operation_add.entity	= entity->id;
	op->operation_add.instance	= CAM_OP_NO_INSTANCE;
	op->operation_add.mode		= CAM_DEPENDENCY_WEAK_ORDER;

	rw_list = libkc_rw_list_get(1);
	op->operation_add.rd_wr_list	= (uint64_t)rw_list;
	if (!rw_list) {
		ret = -ENOMEM;
		goto out;
	}

	rw = libkc_rw_instruction_at(rw_list, 0);

	rw->type	= CAM_INSTANCE_INSTRUCTION;
	rw->error	= 0;
	rw->in.op	= CAM_OP_INSTANCE_CREATE;
	rw->in.id	= 42;

	op = libkc_operation_at(lco, 1);

	op->operation_type		= CAM_OPERATION_TYPE_ADD;
	op->operation_add.id		= 2;
	op->operation_add.delay_ns	= 0;
	op->operation_add.entity	= entity->id;
	op->operation_add.instance	= 42;
	op->operation_add.mode		= CAM_DEPENDENCY_WEAK_ORDER;
	op->operation_add.deps[0].type	= CAM_DEPENDENCY_OP;
	op->operation_add.deps[0].id	= 1;

	rw_list = libkc_rw_list_get(1);
	op->operation_add.rd_wr_list	= (uint64_t)rw_list;
	if (!rw_list) {
		ret = -ENOMEM;
		goto out;
	}

	rw = libkc_rw_instruction_at(rw_list, 0);

	rw->type		= CAM_WRITE_INSTRUCTION;
	rw->error		= 0;
	rw->rd.reg		= 42;
	rw->rd.size		= sizeof(write_buffer);
	rw->rd.num_buffers	= 0;
	rw->rd.buffers_list	= 0x00;
	rw->rd.ptr		= (uint64_t)write_buffer;

	op = libkc_operation_at(lco, 2);

	op->operation_type		= CAM_OPERATION_TYPE_ADD;
	op->operation_add.id		= 3;
	op->operation_add.delay_ns	= 0;
	op->operation_add.entity	= entity->id;
	op->operation_add.instance	= 42;
	op->operation_add.mode		= CAM_DEPENDENCY_WEAK_ORDER;
	op->operation_add.deps[0].type	= CAM_DEPENDENCY_OP;
	op->operation_add.deps[0].id	= 2;
	op->operation_add.deps[0].type	= CAM_DEPENDENCY_EVENT;
	op->operation_add.deps[0].id	= event->id;

	rw_list = libkc_rw_list_get(1);
	op->operation_add.rd_wr_list	= (uint64_t)rw_list;
	if (!rw_list) {
		ret = -ENOMEM;
		goto out;
	}

	rw = libkc_rw_instruction_at(rw_list, 0);

	rw->type	= CAM_INSTANCE_INSTRUCTION;
	rw->error	= 0;
	rw->in.op	= CAM_OP_INSTANCE_DESTROY;
	rw->in.id	= 42;

	ret = libkc_operation_ioctl(cam, lco);
	if (ret)
		goto out;

	ret = read_operations_completion_events(cam, 3);
	if (ret != 3) {
		pr_err("FATAL: read_operations_completion_events() failed\n");
		ret = -EINVAL;
		goto out;
	} else {
		ret = 0;
	}

	for_each_cam_operation(lco, op_idx, op) {
		rw_idx = 0;
		if (libkc_failed_instruction(op, &rw_idx)) {
			ret = -EINVAL;
			goto out;
		}
	}

out:
	libkc_operation_put(lco);
	return ret;
}

static int test_destroy_unknown_instance(struct libkc *cam)
{
	struct libkc_operation *lco = NULL;
	struct cam_rw_instruction *rw;
	struct libkc_rw_list *rw_list;
	struct obj_entity *entity;
	struct cam_operation *op;
	int ret, rw_idx;

	pr_info("Test destroy unknown instance\n");

	entity = libkc_entity_lookup_by_name(cam, VCAM_FAST_IRQ_ENTITY_NAME);
	if (!entity) {
		pr_err("Unable to lookup `%s` entity\n",
		       VCAM_FAST_IRQ_ENTITY_NAME);
		return -EINVAL;
	}

	lco = libkc_operation_get(1);
	if (!lco) {
		ret = -EINVAL;
		goto out;
	}

	op = libkc_operation_at(lco, 0);

	op->operation_type		= CAM_OPERATION_TYPE_ADD;
	op->operation_add.id		= 2;
	op->operation_add.delay_ns	= 0;
	op->operation_add.entity	= entity->id;
	op->operation_add.instance	= CAM_OP_NO_INSTANCE;
	op->operation_add.mode		= CAM_DEPENDENCY_WEAK_ORDER;

	rw_list = libkc_rw_list_get(1);
	if (!rw_list) {
		ret = -ENOMEM;
		goto out;
	}

	op->operation_add.rd_wr_list	= (uint64_t)rw_list;
	rw = libkc_rw_instruction_at(rw_list, 0);

	rw->type	= CAM_INSTANCE_INSTRUCTION;
	rw->error	= 0;
	rw->in.op	= CAM_OP_INSTANCE_DESTROY;
	rw->in.id	= 777;

	ret = libkc_operation_ioctl(cam, lco);
	if (!ret) {
		ret = 0;
	} else {
		pr_err("Unknown instance destruction should fail\n");
		ret = -EINVAL;
		goto out;
	}

	ret = read_operations_completion_events(cam, 1);
	if (ret != 1) {
		pr_err("Invalid number of completions %d\n", ret);
		ret = -EINVAL;
		goto out;
	} else {
		ret = 0;
	}

	rw_idx = 0;
	rw = libkc_failed_instruction(op, &rw_idx);
	if (!rw) {
		pr_err("Instruction error code is not set\n");
		ret = -EINVAL;
		goto out;
	}
	if (rw->error != -ENOENT) {
		pr_err("Unexpected RW error code: %d\n", rw->error);
		ret = -EINVAL;
	}

out:
	libkc_operation_put(lco);
	return ret;
}

static int test_add_buffer(struct libkc *cam)
{
	struct libkc_operation *lco = NULL;
	struct libkc_dmabuf *buf = NULL;
	struct cam_rw_instruction *rw;
	struct libkc_rw_list *rw_list;
	struct obj_entity *entity;
	struct cam_operation *op;
	char *read_buffer;
	int ret, rw_idx;

	pr_info("Test ADD buffer\n");

	entity = libkc_entity_lookup_by_name(cam, VCAM_FAST_IRQ_ENTITY_NAME);
	if (!entity) {
		pr_err("Unable to lookup `%s` entity\n",
		       VCAM_FAST_IRQ_ENTITY_NAME);
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

	read_buffer = calloc(1, READ_BUFFER_SIZE);
	if (!read_buffer) {
		ret = -ENOMEM;
		goto out;
	}

	op = libkc_operation_at(lco, 0);

	op->operation_type		= CAM_OPERATION_TYPE_ADD;
	op->operation_add.id		= 1;
	op->operation_add.delay_ns	= 0;
	op->operation_add.entity	= entity->id;
	op->operation_add.instance	= CAM_OP_NO_INSTANCE;
	op->operation_add.mode		= CAM_DEPENDENCY_WEAK_ORDER;

	rw_list = libkc_rw_list_get(1);
	if (!rw_list) {
		ret = -ENOMEM;
		goto out;
	}

	op->operation_add.rd_wr_list	= (uint64_t)rw_list;
	rw = libkc_rw_instruction_at(rw_list, 0);

	rw->type	= CAM_DMABUF_INSTRUCTION;
	rw->error	= 0;
	rw->db.op	= CAM_OP_DMABUF_ADD;
	rw->db.dma_fd	= buf->fd;
	rw->db.buf_id	= 1;

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

	rw_idx = 0;
	if (libkc_failed_instruction(op, &rw_idx)) {
		ret = -EINVAL;
		goto out;
	}

	pr_info("DMA buffer %d imported under ID %d\n",
		buf->fd, rw->db.buf_id);

	ret = libkc_buffer_register(cam, entity, rw->db.buf_id, buf);
	if (ret) {
		pr_err("Failed to register buffer-%d\n", buf->fd);
		ret = -EINVAL;
		goto out;
	}
out:
	if (ret)
		libkc_dmabuf_put(buf);
	libkc_operation_put(lco);
	free(read_buffer);
	return ret;
}

static int test_remove_buffer(struct libkc *cam, struct obj_buffer *buf)
{
	struct libkc_operation *lco = NULL;
	struct cam_rw_instruction *rw;
	struct libkc_rw_list *rw_list;
	struct obj_entity *entity;
	struct cam_operation *op;
	int ret, rw_idx;

	pr_info("Test REMOVE buffers\n");

	entity = libkc_entity_lookup_by_name(cam, VCAM_FAST_IRQ_ENTITY_NAME);
	if (!entity) {
		pr_err("Unable to lookup `%s` entity\n",
		       VCAM_FAST_IRQ_ENTITY_NAME);
		return -EINVAL;
	}

	lco = libkc_operation_get(1);
	if (!lco) {
		ret = -EINVAL;
		goto out;
	}

	op = libkc_operation_at(lco, 0);

	op->operation_type		= CAM_OPERATION_TYPE_ADD;
	op->operation_add.id		= 2;
	op->operation_add.delay_ns	= 0;
	op->operation_add.entity	= entity->id;
	op->operation_add.instance	= CAM_OP_NO_INSTANCE;
	op->operation_add.mode		= CAM_DEPENDENCY_WEAK_ORDER;

	rw_list = libkc_rw_list_get(1);
	if (!rw_list) {
		ret = -ENOMEM;
		goto out;
	}

	op->operation_add.rd_wr_list	= (uint64_t)rw_list;
	rw = libkc_rw_instruction_at(rw_list, 0);

	rw->type	= CAM_DMABUF_INSTRUCTION;
	rw->error	= 0;
	rw->db.op	= CAM_OP_DMABUF_REMOVE;
	rw->db.dma_fd	= CAM_DMABUF_INSTRUCTION_NO_BUFFER;
	rw->db.buf_id	= buf->id;

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

	rw_idx = 0;
	if (libkc_failed_instruction(op, &rw_idx))
		ret = -EINVAL;

out:
	libkc_operation_put(lco);
	return ret;
}

static int test_remove_unknown_buffer(struct libkc *cam)
{
	struct libkc_operation *lco = NULL;
	struct cam_rw_instruction *rw;
	struct libkc_rw_list *rw_list;
	struct obj_entity *entity;
	struct cam_operation *op;
	int ret, rw_idx;

	pr_info("Test REMOVE unknown buffer\n");

	entity = libkc_entity_lookup_by_name(cam, VCAM_FAST_IRQ_ENTITY_NAME);
	if (!entity) {
		pr_err("Unable to lookup `%s` entity\n",
		       VCAM_FAST_IRQ_ENTITY_NAME);
		return -EINVAL;
	}

	lco = libkc_operation_get(1);
	if (!lco) {
		ret = -EINVAL;
		goto out;
	}

	op = libkc_operation_at(lco, 0);

	op->operation_type		= CAM_OPERATION_TYPE_ADD;
	op->operation_add.id		= 2;
	op->operation_add.delay_ns	= 0;
	op->operation_add.entity	= entity->id;
	op->operation_add.instance	= CAM_OP_NO_INSTANCE;
	op->operation_add.mode		= CAM_DEPENDENCY_WEAK_ORDER;

	rw_list = libkc_rw_list_get(1);
	if (!rw_list) {
		ret = -ENOMEM;
		goto out;
	}

	op->operation_add.rd_wr_list	= (uint64_t)rw_list;
	rw = libkc_rw_instruction_at(rw_list, 0);

	rw->type	= CAM_DMABUF_INSTRUCTION;
	rw->error	= 0;
	rw->db.op	= CAM_OP_DMABUF_REMOVE;
	rw->db.dma_fd	= CAM_DMABUF_INSTRUCTION_NO_BUFFER;
	rw->db.buf_id	= 777;

	ret = libkc_operation_ioctl(cam, lco);
	if (!ret) {
		ret = 0;
	} else {
		pr_err("Unknown buffer removal should fail\n");
		ret = -EINVAL;
		goto out;
	}

	ret = read_operations_completion_events(cam, 1);
	if (ret != 1) {
		pr_err("Invalid number of completions %d\n", ret);
		ret = -EINVAL;
		goto out;
	} else {
		ret = 0;
	}

	rw_idx = 0;
	rw = libkc_failed_instruction(op, &rw_idx);
	if (!rw) {
		pr_err("Instruction error code is not set\n");
		ret = -EINVAL;
		goto out;
	}
	if (rw->error != -ENOENT) {
		pr_err("Unexpected RW error code: %d\n", rw->error);
		ret = -EINVAL;
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
	while (!list_empty(&cam->buffers)) {
		buf = list_first_entry(&cam->buffers,
				       struct obj_buffer,
				       obj_list);

		ret = test_remove_buffer(cam, buf);
		if (ret)
			goto out;
		libkc_buffer_unregister(cam, buf);
	}
out:
	return ret;
}

static int cam_test_instances(struct libkc *cam)
{
	int ret;

	ret = test_destroy_unknown_instance(cam);
	if (ret) {
		pr_err("FATAL: failure test_destroy_unknown_instance()\n");
		return ret;
	}

	ret = test_entity_instance_avail_limit(cam);
	if (ret) {
		pr_err("FATAL: failure test_entity_instance_avail_limit()\n");
		return ret;
	}

	ret = test_compound_instance_operations(cam);
	if (ret) {
		pr_err("FATAL: failure test_compound_instance_operations()\n");
		return ret;
	}

	/*
	 * Note:
	 * The following two will create instances that we use for other
	 * tests as well as for pipeline emergency drain (instances drain).
	 */
	ret = test_create_entity_instance(cam, VCAM_FAST_IRQ_ENTITY_NAME,
					  VCAM_FAST_IRQ_INSTANCE_ID);
	if (ret) {
		pr_err("FATAL: can't create instance\n");
		return ret;
	}

	ret = test_create_entity_instance(cam, VCAM_SLOW_IRQ_ENTITY_NAME,
					  VCAM_SLOW_IRQ_INSTANCE_ID);
	if (ret) {
		pr_err("FATAL: can't create instance\n");
		return ret;
	}

	return 0;
}

static void *thread_fn(void *arg)
{
	struct obj_buffer *buf;
	struct libkc *cam;
	const char *cam_path = "/dev/cam";
	int ret;

	cam = libkc_open(cam_path);
	if (!cam) {
		pr_err("FATAL: cannot open %s\n", cam_path);
		goto out;
	}

	ret = test_query_entities(cam);
	if (ret) {
		pr_err("FATAL: failure test_query_entities()\n");
		goto out;
	}

	ret = test_query_events(cam);
	if (ret) {
		pr_err("FATAL: failure test_query_events()\n");
		goto out;
	}

	ret = cam_test_instances(cam);
	if (ret) {
		pr_err("FATAL: failure cam_test_instances()\n");
		goto out;
	}

	ret = wait_for_slow_entity_timer(cam);
	if (ret) {
		pr_err("FATAL: can't sync with slow entity timer\n");
		goto out;
	}

	ret = test_add_instant_operations(cam, 32 * TEST_NUM_OPERATIONS);
	if (ret) {
		pr_err("FATAL: failure test_add_instant_operations()\n");
		goto out;
	}

	ret = read_operations_completion_events(cam, 32 * TEST_NUM_OPERATIONS);
	if (ret != 32 * TEST_NUM_OPERATIONS) {
		pr_err("FATAL: read_operations_completion_events() failed\n");
		goto out;
	}

	ret = test_add_valid_operations(cam, VCAM_FAST_IRQ_ENTITY_NAME,
					TEST_NUM_OPERATIONS,
					CAM_DEPENDENCY_WEAK_ORDER);
	if (ret) {
		pr_err("FATAL: failure test_add_valid_operations()\n");
		goto out;
	}

	ret = read_operations_completion_events(cam, TEST_NUM_OPERATIONS);
	if (ret != TEST_NUM_OPERATIONS) {
		pr_err("FATAL: read_operations_completion_events() failed\n");
		goto out;
	}

	ret = test_add_valid_operations(cam, VCAM_FAST_IRQ_ENTITY_NAME,
					TEST_NUM_OPERATIONS,
					CAM_DEPENDENCY_STRICT_ORDER);
	if (ret) {
		pr_err("FATAL: failure test_add_valid_operations()\n");
		goto out;
	}

	ret = read_operations_completion_events(cam, TEST_NUM_OPERATIONS);
	if (ret != TEST_NUM_OPERATIONS) {
		pr_err("FATAL: read_operations_completion_events() failed\n");
		goto out;
	}

	ret = test_add_valid_complex_operations(cam, VCAM_SLOW_IRQ_ENTITY_NAME,
						TEST_NUM_OPERATIONS);
	if (ret) {
		pr_err("FATAL: test_add_valid_complex_operations() failed\n");
		goto out;
	}

	ret = test_query_operations(cam, 0, CAM_OP_QUERY_UNIQUE);
	if (ret != 1) {
		pr_err("FATAL: failure test_query_operation(): %d\n", ret);
		goto out;
	}

	ret = test_query_operations(cam, 0, CAM_OP_QUERY_UNIQUE_AND_DEPS);
	if (ret != 3) {
		pr_err("FATAL: failure test_query_operation(): %d\n", ret);
		goto out;
	}

	ret = read_operations_completion_events(cam, TEST_NUM_OPERATIONS);
	if (ret != TEST_NUM_OPERATIONS) {
		pr_err("FATAL: read_operations_completion_events() failed\n");
		goto out;
	}

	ret = test_add_valid_rw_operations(cam, VCAM_FAST_IRQ_ENTITY_NAME,
					   VCAM_FAST_IRQ_INSTANCE_ID,
					   TEST_NUM_RW_OPERATIONS);
	if (ret) {
		pr_err("FATAL: failure test_add_valid_rw_operations()\n");
		goto out;
	}

	ret = test_add_valid_operations(cam, VCAM_SLOW_IRQ_ENTITY_NAME,
					TEST_NUM_OPERATIONS,
					CAM_DEPENDENCY_STRICT_ORDER);
	if (ret) {
		pr_err("FATAL: failure test_add_valid_operations()\n");
		goto out;
	}

	ret = test_query_operations(cam, CAM_OP_ID_ALL_OP, CAM_OP_QUERY_ALL);
	if (ret <= 0) {
		pr_err("FATAL: failure test_query_operations()\n");
		goto out;
	}

	ret = test_query_operations(cam, CAM_OP_ID_ALL_OP, CAM_OP_QUERY_SLEEP);
	if (ret <= 0) {
		pr_err("FATAL: failure test_query_operations()\n");
		goto out;
	}

	ret = test_query_operations(cam, CAM_OP_ID_ALL_OP, CAM_OP_QUERY_QUEUED);
	if (ret != 0) {
		pr_err("FATAL: failure test_query_operations()\n");
		goto out;
	}

	ret = test_remove_operations(cam, TEST_NUM_OPERATIONS);
	if (ret) {
		pr_err("FATAL: failure test_remove_operations()\n");
		goto out;
	}

	ret = read_operations_completion_events(cam, TEST_NUM_OPERATIONS);
	if (ret != TEST_NUM_OPERATIONS) {
		pr_err("FATAL: read_operations_completion_events() failed\n");
		goto out;
	}

	ret = test_export_import_operations(cam);
	if (ret) {
		pr_err("FATAL: failure test_export_import_operations()\n");
		goto out;
	}

	ret = test_compound_buffer_operations(cam);
	if (ret) {
		pr_err("FATAL: failure test_compound_buffer_operations()\n");
		goto out;
	}

	ret = test_add_buffer_cancellation(cam);
	if (ret) {
		pr_err("FATAL: failure test_add_buffer_cancellation()\n");
		goto out;
	}

	ret = test_buffer_enumeration(cam);
	if (ret) {
		pr_err("FATAL: failure test_buffer_enumeration()\n");
		goto out;
	}

	ret = test_add_buffer(cam);
	if (ret) {
		pr_err("FATAL: failure test_add_buffer()\n");
		goto out;
	}

	if (!list_empty(&cam->buffers)) {
		buf = list_first_entry(&cam->buffers,
				       struct obj_buffer,
				       obj_list);

		ret = test_remove_buffer(cam, buf);
		if (ret)
			pr_err("FATAL: failure test_remove_buffer()\n");
		libkc_buffer_unregister(cam, buf);
	}

	ret = test_remove_unknown_buffer(cam);
	if (ret) {
		pr_err("FATAL: failure test_remove_unknown_buffer()\n");
		goto out;
	}

	pr_info("Test emergency pipeline drain\n");

	/*
	 * We don't wait for OPs execution and don't consume completions.
	 * Quite the contrary - we enqueue operations and immediately
	 * close out /dev/cam file handle, forcing pipeline destruction
	 */
	ret = wait_for_slow_entity_timer(cam);
	if (ret) {
		pr_err("FATAL: can't sync with slow entity timer\n");
		goto out;
	}

	ret = test_add_valid_operations(cam, VCAM_SLOW_IRQ_ENTITY_NAME,
					41 * TEST_NUM_OPERATIONS,
					CAM_DEPENDENCY_STRICT_ORDER);
	if (ret) {
		pr_err("FATAL: failure test_add_valid_operations()\n");
		goto out;
	}

out:
	libkc_close(cam);

	if (ret)
		pr_err("Thread terminates (last error: %d)\n", ret);
	else
		pr_info("Thread terminates\n");

	return NULL;
}

#define NUM_THREADS	3

static int multi_threaded_test(void)
{
	pthread_t threads[NUM_THREADS];
	int i;

	pr_info("--- MULTI THREADED TEST ---\n");

	for (i = 0; i < NUM_THREADS; i++) {
		int ret;

		pr_info("Starting thread %d\n", i);
		ret = pthread_create(&threads[i], NULL, thread_fn, NULL);
		if (ret) {
			pr_err("FATAL: failed to create thread: %d\n", ret);
			return ret;
		}
	}

	for (i = 0; i < NUM_THREADS; i++)
		pthread_join(threads[i], NULL);

	return 0;
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

	ret = cam_test_instances(cam);
	if (ret) {
		pr_err("FATAL: failure cam_test_instances()\n");
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

	ret = test_compound_buffer_operations(cam);
	if (ret) {
		pr_err("FATAL: failure test_compound_buffer_operations()\n");
		return ret;
	}

	ret = test_add_buffer(cam);
	if (ret) {
		pr_err("FATAL: failure test_add_buffer()\n");
		return ret;
	}

	ret = test_remove_buffers(cam);
	if (ret) {
		pr_err("FATAL: failure test_remove_buffers()\n");
		return ret;
	}

	ret = test_remove_unknown_buffer(cam);
	if (ret) {
		pr_err("FATAL: failure test_remove_unknown_buffer()\n");
		return ret;
	}

	/* Add buffer for pipeline buffer-drain test */
	ret = test_add_buffer(cam);
	if (ret) {
		pr_err("FATAL: can't add buffer\n");
		return ret;
	}

	libkc_close(cam);

	ret = multi_threaded_test();
	if (ret) {
		pr_err("FATAL: failure multi_threaded_test()\n");
		return ret;
	}

	pr_info("Success\n");
	return EXIT_SUCCESS;
}
