// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit test for cam ioctl
 */

#include <kunit/test.h>

static void noop(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, 0, 1 - 1);
}

static struct kunit_case cam_ioctl_test_cases[] = {
	KUNIT_CASE(noop),
	{}
};

static struct kunit_suite cam_ioctl_test_suite = {
	.name = "cam_ioctl_test",
	.test_cases = cam_ioctl_test_cases,
};

kunit_test_suites(&cam_ioctl_test_suite);

MODULE_LICENSE("GPL v2");
