// SPDX-License-Identifier: GPL-2.0
/*
 * libisp
 *
 * Copyright (C) Google LLC
 */

#ifndef LIBISP_QUERY_H_
#define LIBISP_QUERY_H_

/* @FIXME */
#include "../../../include/uapi/linux/isp.h"

struct libisp;

struct libisp_query {
	struct isp_header	hdr;
	struct isp_query	ents[];
} __attribute__((packed));

struct libisp_query *libisp_query_get(uint32_t num_requests,
				      uint32_t output_sz);
int libisp_query_ioctl(struct libisp *isp, struct libisp_query *lkq);
void libisp_query_put(struct libisp_query *lkq);

struct isp_query *libisp_query_at(struct libisp_query *lkq, u32 idx);

#define for_each_isp_query(q, i, e)					\
	for ((i) = 0, (e) = libisp_query_at((q), (i));			\
	     (i) < (q)->hdr.num_requests &&				\
	     ((e) = libisp_query_at((q), (i)));				\
	     (i)++)

#define __each_query_entry(q, i, n, e)					\
	for ((i)->offt = 0;						\
	     ((i)->offt < (n)) && ((e) = (i)->base) &&			\
	     ((i)->base += sizeof(*(e)));				\
	     (i)->offt++)

u32 libisp_query_num_entities(struct isp_query *q);

#define for_each_query_entity(q, i, e)					\
	__each_query_entry((q), (i), libisp_query_num_entities((q)), (e))

u32 libisp_query_num_events(struct isp_query *q);

#define for_each_query_event(q, i, e)					\
	__each_query_entry((q), (i), libisp_query_num_events((q)), (e))

u32 libisp_query_num_ops(struct isp_query *q);

#define for_each_query_operation(q, i, e)				\
	__each_query_entry((q), (i), libisp_query_num_ops((q)), (e))

u32 libisp_query_num_dmabufs(struct isp_query *q);

#define for_each_query_dmabuf(q, i, e)					\
	__each_query_entry((q), (i), libisp_query_num_dmabufs((q)), (e))

#endif /* LIBISP_QUERY_H_ */
