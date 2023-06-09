// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit test for cam graph
 */

#include <kunit/test.h>

static void stack_alloc(struct kunit *test)
{
	struct cam_graph_stack stack;
	int ret;

	ret = cam_graph_stack_alloc(&stack, 0);
	KUNIT_EXPECT_EQ(test, ret, -ENOMEM);

	ret = cam_graph_stack_alloc(&stack, 1);
	KUNIT_EXPECT_EQ(test, ret, 0);

	cam_graph_stack_free(&stack);
}

static void stack_push(struct kunit *test)
{
	struct cam_graph_stack stack;
	struct cam_obj objs[3];
	int depth;
	int i;
	int ret;

	depth = ARRAY_SIZE(objs);
	ret = cam_graph_stack_alloc(&stack, depth);
	KUNIT_EXPECT_EQ(test, ret, 0);

	for (i = 0; i < depth; i++) {
		struct cam_obj *front;

		ret = cam_graph_stack_push(&stack, &objs[i]);
		KUNIT_EXPECT_EQ(test, ret, 0);

		front = cam_graph_stack_front(&stack);
		KUNIT_EXPECT_PTR_EQ(test, front, &objs[i]);
	}

	ret = cam_graph_stack_empty(&stack);
	KUNIT_EXPECT_EQ(test, ret, false);

	ret = cam_graph_stack_push(&stack, &objs[0]);
	KUNIT_EXPECT_EQ(test, ret, -EFBIG);

	/* This should trigger WARN_ON() */
	cam_graph_stack_free(&stack);
}

static void stack_front(struct kunit *test)
{
	struct cam_graph_stack stack;
	struct cam_obj *front;
	struct cam_obj obj;
	int ret;

	ret = cam_graph_stack_alloc(&stack, 1);
	KUNIT_EXPECT_EQ(test, ret, 0);

	ret = cam_graph_stack_empty(&stack);
	KUNIT_EXPECT_EQ(test, ret, true);

	front = cam_graph_stack_front(&stack);
	KUNIT_EXPECT_PTR_EQ(test, front, NULL);

	ret = cam_graph_stack_push(&stack, &obj);
	KUNIT_EXPECT_EQ(test, ret, 0);

	front = cam_graph_stack_front(&stack);
	KUNIT_EXPECT_PTR_EQ(test, front, &obj);

	cam_graph_stack_pop(&stack);

	front = cam_graph_stack_front(&stack);
	KUNIT_EXPECT_PTR_EQ(test, front, NULL);

	cam_graph_stack_free(&stack);
}

static void stack_pop(struct kunit *test)
{
	struct cam_graph_stack stack;
	struct cam_obj objs[3];
	int depth;
	int i;
	int ret;

	depth = ARRAY_SIZE(objs);
	ret = cam_graph_stack_alloc(&stack, depth);
	KUNIT_EXPECT_EQ(test, ret, 0);

	for (i = 0; i < depth; i++) {
		struct cam_obj *front;

		ret = cam_graph_stack_push(&stack, &objs[i]);
		KUNIT_EXPECT_EQ(test, ret, 0);

		front = cam_graph_stack_front(&stack);
		KUNIT_EXPECT_PTR_EQ(test, front, &objs[i]);
	}

	ret = cam_graph_stack_empty(&stack);
	KUNIT_EXPECT_EQ(test, ret, false);

	for (i = depth - 1; i >= 0; i--) {
		struct cam_obj *front;

		front = cam_graph_stack_front(&stack);
		KUNIT_EXPECT_PTR_EQ(test, front, &objs[i]);

		cam_graph_stack_pop(&stack);
	}

	ret = cam_graph_stack_empty(&stack);
	KUNIT_EXPECT_EQ(test, ret, true);

	cam_graph_stack_pop(&stack);
	ret = cam_graph_stack_empty(&stack);
	KUNIT_EXPECT_EQ(test, ret, true);

	cam_graph_stack_free(&stack);
}

static struct kunit_case cam_graph_test_cases[] = {
	KUNIT_CASE(stack_alloc),
	KUNIT_CASE(stack_push),
	KUNIT_CASE(stack_front),
	KUNIT_CASE(stack_pop),
	{}
};

static struct kunit_suite cam_graph_test_suite = {
	.name = "cam_graph_test",
	.test_cases = cam_graph_test_cases,
};

kunit_test_suites(&cam_graph_test_suite);

MODULE_LICENSE("GPL v2");
