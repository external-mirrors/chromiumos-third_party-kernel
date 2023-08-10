// SPDX-License-Identifier: GPL-2.0
/*
 * libisp
 *
 * Copyright (C) Google LLC
 */

#include <libisp/libisp.h>

void libisp_iterator_init(struct isp_header *hdr,
			  struct libisp_iterator *iter)
{
	iter->base = (void *)hdr->qd.output.address;
	iter->offt = 0;
}

int libisp_output_get(struct isp_header *hdr, uint32_t sz)
{
	void *buf;

	if (!sz)
		return 0;

	buf = calloc(1, sz);
	if (!buf) {
		pr_err("OOM\n");
		return -ENOMEM;
	}

	hdr->qd.output.address	= (uint64_t)buf;
	hdr->qd.output.size	= sz;
	return 0;
}

void libisp_output_put(struct isp_header *hdr)
{
	free((void *)hdr->qd.output.address);
}
