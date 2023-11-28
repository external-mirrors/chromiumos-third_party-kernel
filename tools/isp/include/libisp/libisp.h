// SPDX-License-Identifier: GPL-2.0
/*
 * libisp
 *
 * Copyright (C) 2022 Google LLC
 */

#ifndef LIBISP_H_
#define LIBISP_H_

#include <stddef.h>
#include <stdlib.h>

/* @FIXME */
#include "../../../include/uapi/linux/isp.h"

/* ISP logging */
#include <libisp/libisp-log.h>

/* ISP Query */
#include <libisp/libisp-query.h>

/* ISP Operation */
#include <libisp/libisp-operation.h>

/* ISP Output */
#include <libisp/libisp-output.h>

/* ISP completion events */
#include <libisp/libisp-completion.h>

/* ISP dmabuf (udmabuf) */
#include <libisp/libisp-dmabuf.h>

#include <libisp/visp_objects.h>

#define LIBISP_BUG()					\
	do {						\
		pr_err("BUG. Assertion failed\n");	\
		abort();				\
	} while (0)

struct libisp {
	int32_t			fd;
	int32_t			mem_fd;
	int32_t			udmabuf_fd;

	uint64_t		completion_seqno;

	struct list_head	entities;
	int32_t			num_entities;

	struct list_head	events;
	int32_t			num_events;

	struct list_head	buffers;
	int32_t			num_buffers;
};

struct libisp *libisp_open(const char *dev);
void libisp_close(struct libisp *isp);
int libisp_ioctl(struct libisp *isp, int cmd, void *payload);

struct obj_entity *libisp_entity_lookup(struct libisp *isp, unsigned int id);

struct obj_entity *libisp_entity_lookup_by_name(struct libisp *isp,
						const char *name);

struct obj_event *libisp_entity_first_event(struct obj_entity *entity);

int libisp_entity_register(struct libisp *isp,
			   struct isp_query_entity_entry *entry);

int libisp_event_register(struct libisp *isp,
			  struct isp_query_event_entry *entry,
			  unsigned int entity_id);

int libisp_buffer_register(struct libisp *isp,
			   struct obj_entity *parent,
			   u32 id,
			   struct libisp_dmabuf *buf);

void libisp_buffer_unregister(struct libisp *isp, struct obj_buffer *buf);

#endif /* LIBISP_H_ */
