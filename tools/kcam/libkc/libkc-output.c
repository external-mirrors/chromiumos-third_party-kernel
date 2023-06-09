/* SPDX-License-Identifier: GPL-2.0 */
/*
 * libkc
 *
 * Copyright (C) 2022 Google LLC
 */

#include <libkc/libkc.h>

void libkc_iterator_init(struct cam_header *hdr, struct libkc_iterator *iter)
{
	iter->base = (void *)hdr->output.address;
	iter->offt = 0;
}

int libkc_output_get(struct cam_header *hdr, uint32_t sz)
{
	void *buf;

	if (!sz)
		return 0;

	buf = calloc(1, sz);
	if (!buf) {
		pr_err("OOM\n");
		return -ENOMEM;
	}

	hdr->output.address	= (uint64_t)buf;
	hdr->output.size	= sz;
	return 0;
}

void libkc_output_put(struct cam_header *hdr)
{
	free((void *)hdr->output.address);
}
