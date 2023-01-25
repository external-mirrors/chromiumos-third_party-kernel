/* SPDX-License-Identifier: GPL-2.0 */
/*
 * libkc
 *
 * Copyright (C) 2022 Google LLC
 */

#ifndef LIBKC_H_
#define LIBKC_H_

#include <stddef.h>
#include <stdlib.h>

/* @FIXME */
#include "../../../include/uapi/linux/cam.h"

/* CAM logging */
#include <libkc/libkc-log.h>

/* CAM Query */
#include <libkc/libkc-query.h>

/* CAM Operation */
#include <libkc/libkc-operation.h>

/* CAM Output */
#include <libkc/libkc-output.h>

/* CAM completion events */
#include <libkc/libkc-completion.h>

/* CAM dmabuf (udmabuf) */
#include <libkc/libkc-dmabuf.h>

#include <libkc/vcam_objects.h>

struct libkc {
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

struct libkc *libkc_open(const char *dev);
void libkc_close(struct libkc *cam);
int libkc_ioctl(struct libkc *cam, int cmd, void *payload);

struct obj_entity *libkc_entity_lookup(struct libkc *cam, unsigned int id);

struct obj_entity *libkc_entity_lookup_by_name(struct libkc *cam,
					       const char *name);

struct obj_event *libkc_entity_first_event(struct obj_entity *entity);

int libkc_entity_register(struct libkc *cam,
			  struct cam_query_entity_entry *entry);

int libkc_event_register(struct libkc *cam,
			 struct cam_query_event_entry *entry,
			 unsigned int entity_id);

int libkc_buffer_register(struct libkc *cam,
			  struct obj_entity *parent,
			  u32 id,
			  struct libkc_dmabuf *buf);

void libkc_buffer_unregister(struct libkc *cam, struct obj_buffer *buf);

#endif /* LIBKC_H_ */
