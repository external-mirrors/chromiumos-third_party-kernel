// SPDX-License-Identifier: GPL-2.0
/*
 * libisp
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

#include <libisp/libisp.h>

int log_level = 0;

static int memfd_create(const char *name, unsigned int flags)
{
	return syscall(__NR_memfd_create, name, flags);
}

void libisp_close(struct libisp *isp)
{
	close(isp->udmabuf_fd);
	close(isp->mem_fd);
	close(isp->fd);
	free(isp);
}

struct libisp *libisp_open(const char *dev)
{
	struct libisp *isp;
	int ret;

	isp = calloc(1, sizeof(struct libisp));
	if (!isp) {
		pr_err("OOM\n");
		return NULL;
	}

	isp->fd = open(dev, O_RDWR);
	if (isp->fd < 0) {
		pr_err("Cannot open device: %s\n", dev);
		goto error;
	}

	isp->udmabuf_fd = open("/dev/udmabuf", O_RDWR);
	if (isp->udmabuf_fd < 0) {
		pr_err("Cannot open udmabuf\n");
		goto error;
	}

	isp->mem_fd = memfd_create("visptest-udmabuf", MFD_ALLOW_SEALING);
	if (isp->mem_fd < 0) {
		pr_err("Cannot create memfd\n");
		goto error;
	}

	ret = fcntl(isp->mem_fd, F_ADD_SEALS, F_SEAL_SHRINK);
	if (ret) {
		pr_err("Cannot add memfs seals: %d\n", ret);
		goto error;
	}

	isp->completion_seqno = 0;

	INIT_LIST_HEAD(&isp->entities);
	isp->num_entities = 0;
	INIT_LIST_HEAD(&isp->events);
	isp->num_events = 0;
	INIT_LIST_HEAD(&isp->buffers);
	isp->num_buffers = 0;

	return isp;

error:
	if (isp->udmabuf_fd > 0)
		close(isp->udmabuf_fd);
	if (isp->mem_fd > 0)
		close(isp->mem_fd);
	if (isp->fd > 0)
		close(isp->fd);
	free(isp);
	return NULL;
}

int libisp_ioctl(struct libisp *isp, int cmd, void *payload)
{
	int ret;

	if (isp->fd < 0) {
		pr_err("Wrong ISP file descriptor\n");
		return -EINVAL;
	}

	ret = ioctl(isp->fd, cmd, payload);
	if (ret)
		pr_err("ISP ioctl() returned: %d (errno: %s)\n",
		       ret, strerror(errno));

	return ret;
}

struct obj_entity *libisp_entity_lookup(struct libisp *isp, unsigned int id)
{
	struct obj_entity *entry;

	/*
	 * We have very few entities created by VISP, so a simple linear
	 * search is just fine
	 */
	list_for_each_entry(entry, &isp->entities, obj_list) {
		if (entry->type == OBJ_TYPE_ENTITY && entry->id == id)
			return entry;
	}

	return NULL;
}

struct obj_entity *libisp_entity_lookup_by_name(struct libisp *isp,
						const char *name)
{
	struct obj_entity *entry;

	/*
	 * We have very few entities created by VISP, so a simple linear
	 * search is just fine
	 */
	list_for_each_entry(entry, &isp->entities, obj_list) {
		if (entry->type == OBJ_TYPE_ENTITY &&
		    !strcmp(entry->name, name))
			return entry;
	}

	return NULL;
}

struct obj_event *libisp_entity_first_event(struct obj_entity *entity)
{
	struct obj_event *event;

	list_for_each_entry(event, &entity->children, parent_entry) {
		if (event->type != OBJ_TYPE_EVENT)
			continue;
		return event;
	}
	return NULL;
}

int libisp_entity_register(struct libisp *isp,
			   struct isp_query_entity_entry *entry)
{
	struct obj_entity *obj;
	struct obj_entity *parent;

	obj = malloc(sizeof(*obj));
	if (!obj) {
		pr_err("OOM\n");
		return -ENOMEM;
	}

	isp->num_entities++;
	obj->id = entry->id;
	obj->type = OBJ_TYPE_ENTITY;
	strcpy(obj->name, entry->name);
	INIT_LIST_HEAD(&obj->children);
	INIT_LIST_HEAD(&obj->obj_list);
	list_add_tail(&obj->obj_list, &isp->entities);

	if (obj->id == ISP_OBJ_ID_ROOT)
		return 0;

	parent = libisp_entity_lookup(isp, entry->parent);
	if (!parent) {
		pr_err("Unable to find entity ID: %d\n", entry->parent);
		return -EINVAL;
	}

	list_add(&obj->parent_entry, &parent->children);
	return 0;
}

int libisp_event_register(struct libisp *isp,
			  struct isp_query_event_entry *entry,
			  unsigned int entity_id)
{
	struct obj_event *obj;
	struct obj_entity *parent;

	obj = malloc(sizeof(*obj));
	if (!obj) {
		pr_err("OOM\n");
		return -ENOMEM;
	}

	isp->num_events++;
	obj->id = entry->id;
	obj->type = OBJ_TYPE_EVENT;
	strcpy(obj->name, entry->name);
	INIT_LIST_HEAD(&obj->obj_list);
	list_add_tail(&obj->obj_list, &isp->events);

	parent = libisp_entity_lookup(isp, entity_id);
	if (!parent) {
		pr_err("Unable to find entity ID: %d\n", entity_id);
		return -EINVAL;
	}

	list_add(&obj->parent_entry, &parent->children);
	return 0;
}

int libisp_buffer_register(struct libisp *isp,
			   struct obj_entity *parent,
			   u32 id,
			   struct libisp_dmabuf *buf)
{
	struct obj_buffer *obj;

	obj = malloc(sizeof(*obj));
	if (!obj) {
		pr_err("OOM\n");
		return -ENOMEM;
	}

	isp->num_buffers++;
	obj->type = OBJ_TYPE_BUFFER;
	obj->id = id;
	obj->dmabuf = buf;
	INIT_LIST_HEAD(&obj->obj_list);

	list_add_tail(&obj->obj_list, &isp->buffers);
	return 0;
}

void libisp_buffer_unregister(struct libisp *isp, struct obj_buffer *buf)
{
	isp->num_buffers--;
	list_del(&buf->obj_list);
	libisp_dmabuf_put(buf->dmabuf);
	free(buf);
}
