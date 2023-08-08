/* SPDX-License-Identifier: GPL-2.0 */
/*
 * libkc
 *
 * Copyright (C) Google LLC
 */

#ifndef LIBKC_QUERY_H_
#define LIBKC_QUERY_H_

/* @FIXME */
#include "../../../include/uapi/linux/cam.h"

struct libkc;

struct libkc_query {
	struct cam_header	hdr;
	struct cam_query	ents[];
} __attribute__((packed));

struct libkc_query *libkc_query_get(uint32_t num_requests,
				    uint32_t output_sz);
int libkc_query_ioctl(struct libkc *cam, struct libkc_query *lcq);
void libkc_query_put(struct libkc_query *lcq);

struct cam_query *libkc_query_at(struct libkc_query *lcq, u32 idx);

#define for_each_cam_query(q, i, e)					\
	for ((i) = 0, (e) = libkc_query_at((q), (i));			\
	     (i) < (q)->hdr.num_requests &&				\
	     ((e) = libkc_query_at((q), (i)));				\
	     (i)++)

#define __each_query_entry(q, i, n, e)					\
	for ((i)->offt = 0;						\
	     ((i)->offt < (n)) && ((e) = (i)->base) &&			\
	     ((i)->base += sizeof(*(e)));				\
	     (i)->offt++)

u32 libkc_query_num_entities(struct cam_query *q);

#define for_each_query_entity(q, i, e)					\
	__each_query_entry((q), (i), libkc_query_num_entities((q)), (e))

u32 libkc_query_num_events(struct cam_query *q);

#define for_each_query_event(q, i, e)					\
	__each_query_entry((q), (i), libkc_query_num_events((q)), (e))

u32 libkc_query_num_ops(struct cam_query *q);

#define for_each_query_operation(q, i, e)				\
	__each_query_entry((q), (i), libkc_query_num_ops((q)), (e))

u32 libkc_query_num_dmabufs(struct cam_query *q);

#define for_each_query_dmabuf(q, i, e)					\
	__each_query_entry((q), (i), libkc_query_num_dmabufs((q)), (e))

#endif /* LIBKC_QUERY_H_ */
