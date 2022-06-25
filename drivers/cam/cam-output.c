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

#include <uapi/linux/cam.h>

int cam_output_init(struct cam_header *hdr, struct cam_koutput *output)
{
	output->origin = output->base = u64_to_user_ptr(hdr->output.address);
	output->end = output->origin + hdr->output.size;

	if (!output->origin)
		return 0;

	if (!access_ok(output->base, hdr->output.size))
		return -EFAULT;

	if (clear_user(output->base, hdr->output.size))
		return -EFAULT;

	return 0;
}

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

bool cam_output_has_buffer(struct cam_koutput *output)
{
	return output->origin;
}
