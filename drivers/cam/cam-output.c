// SPDX-License-Identifier: GPL-2.0
/*
 * CAM output
 *
 * Copyright (C) 2022 Google LLC
 * Copyright (c) 2021 Intel Corporation
 */

#define pr_fmt(fmt) "cam-output: " fmt

#include <linux/cam/cam-device.h>
#include <linux/cam/cam-output.h>
#include <linux/uaccess.h>

void *__cam_output_next_entry(struct cam_koutput *output, size_t sz)
{
	void *ptr;

	output->length += sz;
	if (output->base + sz >= output->end)
		return NULL;

	ptr = output->base;
	output->base += sz;

	return ptr;
}
ALLOW_ERROR_INJECTION(__cam_output_next_entry, NULL);

int cam_output_clear(struct cam_koutput *output, size_t sz)
{
	if (!access_ok(output->base, sz))
		return -EFAULT;

	if (clear_user(output->base, sz))
		return -EFAULT;

	return 0;
}
