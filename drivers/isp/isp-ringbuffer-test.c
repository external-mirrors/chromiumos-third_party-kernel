// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit test for isp ringbuffer
 */

#include <kunit/test.h>

#define ENTRY_CNT (4)

static void ringbuffer_alloc(struct kunit *test)
{
	struct isp_ringbuffer rb;
	int ret;

	/* Buffer size is not a power of 2 */
	ret = isp_ringbuffer_init(&rb, sizeof(struct isp_completion), PAGE_SIZE - 1);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	/* Entry size is not a power of 2 */
	ret = isp_ringbuffer_init(&rb, sizeof(struct isp_completion) - 1, PAGE_SIZE);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	/* Buffer size is too small */
	ret = isp_ringbuffer_init(&rb, PAGE_SIZE, PAGE_SIZE);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	ret = isp_ringbuffer_init(&rb, sizeof(struct isp_completion), PAGE_SIZE);
	KUNIT_EXPECT_EQ(test, ret, 0);

	isp_ringbuffer_release(&rb);
}

static void ringbuffer_write(struct kunit *test)
{
	struct isp_ringbuffer rb;
	struct isp_completion objs[ENTRY_CNT];
	int i, ret;
	off_t head;

	ret = isp_ringbuffer_init(&rb, sizeof(struct isp_completion),
				  sizeof(struct isp_completion) * ENTRY_CNT);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/*
	 * Only (buffer_sz/entry_sz)-1 slots are available in the circular
	 * buffer due to its design, so we can only push ENTRY_CNT-1 objects at
	 * a time.
	 */
	for (i = 0; i < ENTRY_CNT - 1; i++) {
		objs[i].id = i;
		objs[i].type = ISP_COMPLETION_TYPE_EXECUTED;
		ret = isp_ringbuffer_write(&rb, &objs[i]);
		KUNIT_EXPECT_EQ(test, ret, 0);
	}

	head = rb.head;
	 /* Now the buffer is full */
	ret = isp_ringbuffer_write(&rb, &objs[0]);
	KUNIT_EXPECT_EQ(test, ret, 0);
	/* Ringbuffer head is supposed to advance */
	KUNIT_EXPECT_NE(test, head, rb.head);

	isp_ringbuffer_release(&rb);
}

static void ringbuffer_read(struct kunit *test)
{
	struct isp_ringbuffer rb;
	struct isp_completion objs[ENTRY_CNT * 2];
	struct isp_completion readback;
	int i, ret;

	ret = isp_ringbuffer_init(&rb, sizeof(struct isp_completion),
				  sizeof(struct isp_completion) * ENTRY_CNT);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* No objects in the buffer */
	ret = isp_ringbuffer_read(&rb, &readback, IOCB_NOWAIT);
	KUNIT_EXPECT_EQ(test, ret, -EAGAIN);

	/*
	 * Write an object and read that back immediately.
	 * The buffer pointers should circle back when they reach the end.
	 */
	for (i = 0; i < ENTRY_CNT * 2; i++) {
		objs[i].id = i;
		objs[i].type = ISP_COMPLETION_TYPE_EXECUTED;
		ret = isp_ringbuffer_write(&rb, &objs[i]);
		KUNIT_EXPECT_EQ(test, ret, 0);

		ret = isp_ringbuffer_read(&rb, &readback, IOCB_NOWAIT);
		KUNIT_EXPECT_EQ(test, ret, 0);
		KUNIT_EXPECT_EQ(test, objs[i].id, readback.id);
		KUNIT_EXPECT_EQ(test, objs[i].type, readback.type);
	}

	/* Again, no objects in the buffer */
	ret = isp_ringbuffer_read(&rb, &readback, IOCB_NOWAIT);
	KUNIT_EXPECT_EQ(test, ret, -EAGAIN);

	isp_ringbuffer_release(&rb);
}

static struct kunit_case isp_ringbuffer_test_cases[] = {
	KUNIT_CASE(ringbuffer_alloc),
	KUNIT_CASE(ringbuffer_write),
	KUNIT_CASE(ringbuffer_read),
	{}
};

static struct kunit_suite isp_ringbuffer_test_suite = {
	.name = "isp_ringbuffer_test",
	.test_cases = isp_ringbuffer_test_cases,
};

kunit_test_suites(&isp_ringbuffer_test_suite);

MODULE_LICENSE("GPL v2");
