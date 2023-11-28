// SPDX-License-Identifier: GPL-2.0
/*
 * libisp query
 *
 * Copyright (C) Google LLC
 */

#include <libisp/libisp.h>

static u32 query_num_entries(struct isp_query *q, u32 query_type)
{
	if (q->query_type != query_type) {
		pr_err("Mismatch. Query type: %d expected: %d\n",
		       q->query_type,
		       query_type);
		LIBISP_BUG();
		return 0;
	}

	switch (query_type) {
	case ISP_QUERY_TYPE_ENTITIES:
		return q->query_entities.num_entities;
	case ISP_QUERY_TYPE_EVENTS:
		return q->query_events.num_events;
	case ISP_QUERY_TYPE_OPERATIONS:
		return q->query_operations.num_ops;
	case ISP_QUERY_TYPE_DMABUF:
		return 1;
	}

	LIBISP_BUG();
	return 0;
}

u32 libisp_query_num_entities(struct isp_query *q)
{
	return query_num_entries(q, ISP_QUERY_TYPE_ENTITIES);
}

u32 libisp_query_num_events(struct isp_query *q)
{
	return query_num_entries(q, ISP_QUERY_TYPE_EVENTS);
}

u32 libisp_query_num_ops(struct isp_query *q)
{
	return query_num_entries(q, ISP_QUERY_TYPE_OPERATIONS);
}

u32 libisp_query_num_dmabufs(struct isp_query *q)
{
	return query_num_entries(q, ISP_QUERY_TYPE_DMABUF);
}

struct isp_query *libisp_query_at(struct libisp_query *liq, u32 idx)
{
	if (liq->hdr.num_requests == 0) {
		LIBISP_BUG();
		return NULL;
	}

	if (idx >= liq->hdr.num_requests) {
		pr_err("BOOM\n");
		LIBISP_BUG();
		return NULL;
	}

	return &liq->ents[idx];
}

void libisp_query_put(struct libisp_query *liq)
{
	if (!liq)
		return;

	libisp_output_put(&liq->hdr);
	free(liq);
}

struct libisp_query *libisp_query_get(uint32_t num_requests,
				      uint32_t output_sz)
{
	struct libisp_query *liq;
	struct isp_query *q;
	size_t sz;
	int i;

	if (num_requests < 1)
		return NULL;

	sz = sizeof(struct libisp_query) +
		num_requests * sizeof(struct isp_query);
	liq = calloc(1, sz);

	if (!liq) {
		pr_err("OOM\n");
		return NULL;
	}

	liq->hdr.num_requests	= num_requests;

	if (libisp_output_get(&liq->hdr, output_sz)) {
		free(liq);
		return NULL;
	}

	return liq;
}

int libisp_query_ioctl(struct libisp *isp, struct libisp_query *liq)
{
	size_t sz;

	sz = sizeof(struct isp_header) +
		liq->hdr.num_requests * sizeof(struct isp_query);
	return libisp_ioctl(isp, ISP_IOC_QUERY(sz), liq);
}
