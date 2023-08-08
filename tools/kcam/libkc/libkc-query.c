/* SPDX-License-Identifier: GPL-2.0 */
/*
 * libkc query
 *
 * Copyright (C) Google LLC
 */

#include <libkc/libkc.h>

static u32 query_num_entries(struct cam_query *q, u32 query_type)
{
	if (q->query_type != query_type) {
		pr_err("Mismatch. Query type: %d expected: %d\n",
		       q->query_type,
		       query_type);
		LIBKC_BUG();
		return 0;
	}

	switch (query_type) {
	case CAM_QUERY_TYPE_ENTITIES:
		return q->query_entities.num_entities;
	case CAM_QUERY_TYPE_EVENTS:
		return q->query_events.num_events;
	case CAM_QUERY_TYPE_OPERATIONS:
		return q->query_operations.num_ops;
	case CAM_QUERY_TYPE_DMABUF:
		return 1;
	}

	LIBKC_BUG();
	return 0;
}

u32 libkc_query_num_entities(struct cam_query *q)
{
	return query_num_entries(q, CAM_QUERY_TYPE_ENTITIES);
}

u32 libkc_query_num_events(struct cam_query *q)
{
	return query_num_entries(q, CAM_QUERY_TYPE_EVENTS);
}

u32 libkc_query_num_ops(struct cam_query *q)
{
	return query_num_entries(q, CAM_QUERY_TYPE_OPERATIONS);
}

u32 libkc_query_num_dmabufs(struct cam_query *q)
{
	return query_num_entries(q, CAM_QUERY_TYPE_DMABUF);
}

struct cam_query *libkc_query_at(struct libkc_query *lcq, u32 idx)
{
	if (lcq->hdr.num_requests == 0) {
		LIBKC_BUG();
		return NULL;
	}

	if (idx >= lcq->hdr.num_requests) {
		pr_err("BOOM\n");
		LIBKC_BUG();
		return NULL;
	}

	return &lcq->ents[idx];
}

void libkc_query_put(struct libkc_query *lcq)
{
	if (!lcq)
		return;

	libkc_output_put(&lcq->hdr);
	free(lcq);
}

struct libkc_query *libkc_query_get(uint32_t num_requests,
				    uint32_t output_sz)
{
	struct libkc_query *lcq;
	struct cam_query *q;
	size_t sz;
	int i;

	if (num_requests < 1)
		return NULL;

	sz = sizeof(struct libkc_query) +
		num_requests * sizeof(struct cam_query);
	lcq = calloc(1, sz);

	if (!lcq) {
		pr_err("OOM\n");
		return NULL;
	}

	lcq->hdr.num_requests	= num_requests;

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
		lcq->hdr.num_requests * sizeof(struct cam_query);
	return libkc_ioctl(cam, CAM_IOC_QUERY(sz), lcq);
}
