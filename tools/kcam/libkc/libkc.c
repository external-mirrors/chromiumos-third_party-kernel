/* SPDX-License-Identifier: GPL-2.0 */
/*
 * libkc
 *
 * Copyright (C) 2022 Google LLC
 */

#define _GNU_SOURCE

#include <fcntl.h>
#include <stdio.h>
#include <memory.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <linux/memfd.h>

#include <libkc/libkc.h>

int log_level = 0;

static int memfd_create(const char *name, unsigned int flags)
{
	return syscall(__NR_memfd_create, name, flags);
}

void libkc_close(struct libkc *cam)
{
	close(cam->udmabuf_fd);
	close(cam->mem_fd);
	close(cam->fd);
	free(cam);
}

struct libkc *libkc_open(const char *dev)
{
	struct libkc *cam;
	int ret;

	cam = calloc(1, sizeof(struct libkc));
	if (!cam) {
		pr_err("OOM\n");
		return NULL;
	}

	cam->fd = open(dev, O_RDWR);
	if (cam->fd < 0) {
		pr_err("Cannot open device: %s\n", dev);
		goto error;
	}

	cam->udmabuf_fd = open("/dev/udmabuf", O_RDWR);
	if (cam->udmabuf_fd < 0) {
		pr_err("Cannot open udmabuf\n");
		goto error;
	}

	cam->mem_fd = memfd_create("vcamtest-udmabuf", MFD_ALLOW_SEALING);
	if (cam->mem_fd < 0) {
		pr_err("Cannot create memfd\n");
		goto error;
	}

	ret = fcntl(cam->mem_fd, F_ADD_SEALS, F_SEAL_SHRINK);
	if (ret) {
		pr_err("Cannot add memfs seals: %d\n", ret);
		goto error;
	}

	cam->completion_seqno = 0;

	INIT_LIST_HEAD(&cam->entities);
	cam->num_entities = 0;
	INIT_LIST_HEAD(&cam->events);
	cam->num_events = 0;
	INIT_LIST_HEAD(&cam->buffers);
	cam->num_buffers = 0;

	return cam;

error:
	if (cam->udmabuf_fd > 0)
		close(cam->udmabuf_fd);
	if (cam->mem_fd > 0)
		close(cam->mem_fd);
	if (cam->fd > 0)
		close(cam->fd);
	free(cam);
	return NULL;
}

int libkc_ioctl(struct libkc *cam, int cmd, void *payload)
{
	int ret;

	if (cam->fd < 0) {
		pr_err("Wrong CAM file descriptor\n");
		return -EINVAL;
	}

	ret = ioctl(cam->fd, cmd, payload);
	if (ret)
		pr_err("CAM ioctl() returned: %d (errno: %s)\n",
		       ret, strerror(errno));

	return ret;
}

struct obj_entity *libkc_entity_lookup(struct libkc *cam, unsigned int id)
{
	struct obj_entity *entry;

	/*
	 * We have very few entities created by VCAM, so a simple linear
	 * search is just fine
	 */
	list_for_each_entry(entry, &cam->entities, obj_list) {
		if (entry->type == OBJ_TYPE_ENTITY && entry->id == id)
			return entry;
	}

	return NULL;
}

struct obj_entity *libkc_entity_lookup_by_name(struct libkc *cam,
					       const char *name)
{
	struct obj_entity *entry;

	/*
	 * We have very few entities created by VCAM, so a simple linear
	 * search is just fine
	 */
	list_for_each_entry(entry, &cam->entities, obj_list) {
		if (entry->type == OBJ_TYPE_ENTITY &&
		    !strcmp(entry->name, name))
			return entry;
	}

	return NULL;
}

struct obj_event *libkc_entity_first_event(struct obj_entity *entity)
{
	struct obj_event *event;

	list_for_each_entry(event, &entity->children, parent_entry) {
		if (event->type != OBJ_TYPE_EVENT)
			continue;
		return event;
	}
	return NULL;
}

int libkc_entity_register(struct libkc *cam,
			  struct cam_query_entity_entry *entry)
{
	struct obj_entity *obj;
	struct obj_entity *parent;

	obj = malloc(sizeof(*obj));
	if (!obj) {
		pr_err("OOM\n");
		return -ENOMEM;
	}

	cam->num_entities++;
	obj->id = entry->id;
	obj->type = OBJ_TYPE_ENTITY;
	strcpy(obj->name, entry->name);
	INIT_LIST_HEAD(&obj->children);
	INIT_LIST_HEAD(&obj->obj_list);
	list_add_tail(&obj->obj_list, &cam->entities);

	if (obj->id == CAM_OBJ_ID_ROOT)
		return 0;

	parent = libkc_entity_lookup(cam, entry->parent);
	if (!parent) {
		pr_err("Unable to find entity ID: %d\n", entry->parent);
		return -EINVAL;
	}

	list_add(&obj->parent_entry, &parent->children);
	return 0;
}

int libkc_event_register(struct libkc *cam,
			 struct cam_query_event_entry *entry,
			 unsigned int entity_id)
{
	struct obj_event *obj;
	struct obj_entity *parent;

	obj = malloc(sizeof(*obj));
	if (!obj) {
		pr_err("OOM\n");
		return -ENOMEM;
	}

	cam->num_events++;
	obj->id = entry->id;
	obj->type = OBJ_TYPE_EVENT;
	strcpy(obj->name, entry->name);
	INIT_LIST_HEAD(&obj->obj_list);
	list_add_tail(&obj->obj_list, &cam->events);

	parent = libkc_entity_lookup(cam, entity_id);
	if (!parent) {
		pr_err("Unable to find entity ID: %d\n", entity_id);
		return -EINVAL;
	}

	list_add(&obj->parent_entry, &parent->children);
	return 0;
}

int libkc_buffer_register(struct libkc *cam,
			  struct obj_entity *parent,
			  u32 id,
			  struct libkc_dmabuf *buf)
{
	struct obj_buffer *obj;

	obj = malloc(sizeof(*obj));
	if (!obj) {
		pr_err("OOM\n");
		return -ENOMEM;
	}

	cam->num_buffers++;
	obj->type = OBJ_TYPE_BUFFER;
	obj->id = id;
	obj->dmabuf = buf;
	INIT_LIST_HEAD(&obj->obj_list);

	list_add_tail(&obj->obj_list, &cam->buffers);
	return 0;
}

void libkc_buffer_unregister(struct libkc *cam, struct obj_buffer *buf)
{
	cam->num_buffers--;
	list_del(&buf->obj_list);
	libkc_dmabuf_put(buf->dmabuf);
	free(buf);
}
