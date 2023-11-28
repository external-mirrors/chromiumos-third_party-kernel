// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit test for isp graph
 */

#include <kunit/test.h>

static void stack_alloc(struct kunit *test)
{
	struct isp_graph_stack stack;
	int ret;

	ret = isp_graph_stack_alloc(&stack, 0);
	KUNIT_EXPECT_EQ(test, ret, -ENOMEM);

	ret = isp_graph_stack_alloc(&stack, 1);
	KUNIT_EXPECT_EQ(test, ret, 0);

	isp_graph_stack_free(&stack);
}

static void stack_push(struct kunit *test)
{
	struct isp_graph_stack stack;
	struct isp_obj objs[3];
	int depth;
	int i;
	int ret;

	depth = ARRAY_SIZE(objs);
	ret = isp_graph_stack_alloc(&stack, depth);
	KUNIT_EXPECT_EQ(test, ret, 0);

	for (i = 0; i < depth; i++) {
		struct isp_obj *front;

		ret = isp_graph_stack_push(&stack, &objs[i]);
		KUNIT_EXPECT_EQ(test, ret, 0);

		front = isp_graph_stack_front(&stack);
		KUNIT_EXPECT_PTR_EQ(test, front, &objs[i]);
	}

	ret = isp_graph_stack_empty(&stack);
	KUNIT_EXPECT_EQ(test, ret, false);

	ret = isp_graph_stack_push(&stack, &objs[0]);
	KUNIT_EXPECT_EQ(test, ret, -EFBIG);

	/* This should trigger WARN_ON() */
	isp_graph_stack_free(&stack);
}

static void stack_front(struct kunit *test)
{
	struct isp_graph_stack stack;
	struct isp_obj *front;
	struct isp_obj obj;
	int ret;

	ret = isp_graph_stack_alloc(&stack, 1);
	KUNIT_EXPECT_EQ(test, ret, 0);

	ret = isp_graph_stack_empty(&stack);
	KUNIT_EXPECT_EQ(test, ret, true);

	front = isp_graph_stack_front(&stack);
	KUNIT_EXPECT_PTR_EQ(test, front, NULL);

	ret = isp_graph_stack_push(&stack, &obj);
	KUNIT_EXPECT_EQ(test, ret, 0);

	front = isp_graph_stack_front(&stack);
	KUNIT_EXPECT_PTR_EQ(test, front, &obj);

	isp_graph_stack_pop(&stack);

	front = isp_graph_stack_front(&stack);
	KUNIT_EXPECT_PTR_EQ(test, front, NULL);

	isp_graph_stack_free(&stack);
}

static void stack_pop(struct kunit *test)
{
	struct isp_graph_stack stack;
	struct isp_obj objs[3];
	int depth;
	int i;
	int ret;

	depth = ARRAY_SIZE(objs);
	ret = isp_graph_stack_alloc(&stack, depth);
	KUNIT_EXPECT_EQ(test, ret, 0);

	for (i = 0; i < depth; i++) {
		struct isp_obj *front;

		ret = isp_graph_stack_push(&stack, &objs[i]);
		KUNIT_EXPECT_EQ(test, ret, 0);

		front = isp_graph_stack_front(&stack);
		KUNIT_EXPECT_PTR_EQ(test, front, &objs[i]);
	}

	ret = isp_graph_stack_empty(&stack);
	KUNIT_EXPECT_EQ(test, ret, false);

	for (i = depth - 1; i >= 0; i--) {
		struct isp_obj *front;

		front = isp_graph_stack_front(&stack);
		KUNIT_EXPECT_PTR_EQ(test, front, &objs[i]);

		isp_graph_stack_pop(&stack);
	}

	ret = isp_graph_stack_empty(&stack);
	KUNIT_EXPECT_EQ(test, ret, true);

	isp_graph_stack_pop(&stack);
	ret = isp_graph_stack_empty(&stack);
	KUNIT_EXPECT_EQ(test, ret, true);

	isp_graph_stack_free(&stack);
}

static struct kunit_case isp_graph_test_cases[] = {
	KUNIT_CASE(stack_alloc),
	KUNIT_CASE(stack_push),
	KUNIT_CASE(stack_front),
	KUNIT_CASE(stack_pop),
	{}
};

static struct kunit_suite isp_graph_test_suite = {
	.name = "isp_graph_test",
	.test_cases = isp_graph_test_cases,
};

kunit_test_suites(&isp_graph_test_suite);

MODULE_LICENSE("GPL v2");
