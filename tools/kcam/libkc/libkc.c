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
