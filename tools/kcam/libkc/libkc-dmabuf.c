/* SPDX-License-Identifier: GPL-2.0 */
/*
 * libkc dmabuf
 *
 * Copyright (C) 2022 Google LLC
 */

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/udmabuf.h>

#include <libkc/libkc.h>

void libkc_dmabuf_put(struct libkc_dmabuf *buf)
{
	if (!buf)
		return;

	if (buf->va != MAP_FAILED)
		munmap(buf->va, getpagesize() * buf->num_pages);
	close(buf->fd);
	free(buf);
}

struct libkc_dmabuf *libkc_dmabuf_get(struct libkc *cam, u32 num_pages)
{
	struct udmabuf_create create = {};
	struct libkc_dmabuf *buf;
	size_t size;
	int ret;

	if (num_pages <= 0) {
		pr_err("Invalid dmabuf size\n");
		return NULL;
	}

	size = getpagesize() * num_pages;
	ret = ftruncate(cam->mem_fd, size);
	if (ret < 0) {
		pr_err("Failed to truncate memfd\n");
		return NULL;
	}

	create.memfd	= cam->mem_fd;
	create.offset	= 0;
	create.size	= size;

	ret = ioctl(cam->udmabuf_fd, UDMABUF_CREATE, &create);
	if (ret < 0) {
		pr_err("UDMABUF_CREATE failed: %d\n", ret);
		return NULL;
	}

	buf = calloc(1, sizeof(*buf));
	if (!buf) {
		pr_err("OOM\n");
		close(ret);
		return NULL;
	}

	buf->fd		= ret;
	buf->num_pages	= num_pages;
	buf->va		= mmap(NULL, size, PROT_READ | PROT_WRITE,
			       MAP_SHARED, ret, 0);

	if (buf->va == MAP_FAILED) {
		pr_err("Buffer mmap() failed\n");
		libkc_dmabuf_put(buf);
		buf = NULL;
	}

	return buf;
}
