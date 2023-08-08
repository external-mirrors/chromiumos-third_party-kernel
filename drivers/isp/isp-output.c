// SPDX-License-Identifier: GPL-2.0
/*
 * ISP output
 *
 * Copyright (C) Google LLC
 * Copyright (c) Intel Corporation
 */

#define pr_fmt(fmt) "isp-output: " fmt

#include <linux/isp/isp-device.h>
#include <linux/isp/isp-output.h>
#include <linux/uaccess.h>

#include <uapi/linux/isp.h>

int isp_output_init(struct isp_header *hdr, struct isp_koutput *output)
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

void *__isp_output_next_entry(struct isp_koutput *output, size_t sz)
{
	void *ptr;

	output->length += sz;
	if (output->base + sz >= output->end)
		return NULL;

	ptr = output->base;
	output->base += sz;

	return ptr;
}
ALLOW_ERROR_INJECTION(__isp_output_next_entry, NULL);

bool isp_output_has_buffer(struct isp_koutput *output)
{
	return output->origin;
}
