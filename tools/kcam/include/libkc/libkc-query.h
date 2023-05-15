/* SPDX-License-Identifier: GPL-2.0 */
/*
 * libkc
 *
 * Copyright (C) 2022 Google LLC
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

struct libkc_query *libkc_query_get(uint32_t num_queries,
				      uint32_t output_sz);
int libkc_query_ioctl(struct libkc *cam, struct libkc_query *lcq);
void libkc_query_put(struct libkc_query *lcq);

struct cam_query *libkc_query_at(struct libkc_query *lcq, u32 idx);

#define for_each_cam_query(q, i, e)					\
	for ((i) = 0, (e) = libkc_query_at((q), (i));			\
	     (e) != NULL && (i) < (q)->hdr.num_queries;			\
	     (i)++, (e) = libkc_query_at((q), (i)))

u32 libkc_query_num_entities(struct cam_query *q);

#define for_each_query_entity(q, i, e)					\
	for ((i)->offt = 0, (e) = (i)->base;				\
	     (i)->offt < libkc_query_num_entities((q));			\
	     (i)->offt++, (i)->base += sizeof(*(e)), (e) = (i)->base)

u32 libkc_query_num_events(struct cam_query *q);

#define for_each_query_event(q, i, e)					\
	for ((i)->offt = 0, (e) = (i)->base;				\
	     (i)->offt < libkc_query_num_events((q));			\
	     (i)->offt++, (i)->base += sizeof(*(e)), (e) = (i)->base)

u32 libkc_query_num_ops(struct cam_query *q);

#define for_each_query_operation(q, i, e)				\
	for ((i)->offt = 0, (e) = (i)->base;				\
	     (i)->offt < libkc_query_num_ops((q));			\
	     (i)->offt++, (i)->base += sizeof(*(e)), (e) = (i)->base)

u32 libkc_query_num_dmabufs(struct cam_query *q);

#define for_each_query_dmabuf(q, i, e)					\
	for ((i)->offt = 0, (e) = (i)->base;				\
	     (i)->offt < libkc_query_num_dmabufs((q));			\
	     (i)->offt++, (i)->base += sizeof(*(e)), (e) = (i)->base)

#endif /* LIBKC_QUERY_H_ */
