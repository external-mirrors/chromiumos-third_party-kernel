/* SPDX-License-Identifier: GPL-2.0 */
/*
 * libkc query
 *
 * Copyright (C) 2022 Google LLC
 */

#include <libkc/libkc.h>

struct cam_query *libkc_query_at(struct libkc_query *lcq, u32 idx)
{
	if (lcq->hdr.num_queries == 0)
		return NULL;

	if (idx > lcq->hdr.num_queries)
		return NULL;

	return &lcq->ents[idx];
}

void libkc_query_put(struct libkc_query *lcq)
{
	if (!lcq)
		return;

	libkc_output_put(&lcq->hdr);
	free(lcq);
}

struct libkc_query *libkc_query_get(uint32_t num_queries,
				    uint32_t output_sz)
{
	struct libkc_query *lcq;
	struct cam_query *q;
	size_t sz;
	int i;

	if (num_queries < 1)
		return NULL;

	sz = sizeof(struct libkc_query) +
		num_queries * sizeof(struct cam_query);
	lcq = calloc(1, sz);

	if (!lcq) {
		pr_err("OOM\n");
		return NULL;
	}

	lcq->hdr.num_queries	= num_queries;
	lcq->hdr.length		= sz;

	if (libkc_output_get(&lcq->hdr, output_sz)) {
		free(lcq);
		return NULL;
	}

	return lcq;
}

int libkc_query_ioctl(struct libkc *cam, struct libkc_query *lcq)
{
	size_t sz;

	sz = sizeof(struct cam_header) +
		lcq->hdr.num_queries * sizeof(struct cam_query);
	return libkc_ioctl(cam, CAM_IOC_QUERY(sz), lcq);
}
