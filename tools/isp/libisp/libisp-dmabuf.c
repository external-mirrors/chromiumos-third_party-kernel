// SPDX-License-Identifier: GPL-2.0
/*
 * libisp dmabuf
 *
 * Copyright (C) Google LLC
 */

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/udmabuf.h>

#include <libisp/libisp.h>

void libisp_dmabuf_put(struct libisp_dmabuf *buf)
{
	if (!buf)
		return;

	if (buf->va != MAP_FAILED)
		munmap(buf->va, getpagesize() * buf->num_pages);
	close(buf->fd);
	free(buf);
}

struct libisp_dmabuf *libisp_dmabuf_get(struct libisp *isp, u32 num_pages)
{
	struct udmabuf_create create = {};
	struct libisp_dmabuf *buf;
	size_t size;
	int ret;

	if (num_pages <= 0) {
		pr_err("Invalid dmabuf size\n");
		return NULL;
	}

	size = getpagesize() * num_pages;
	ret = ftruncate(isp->mem_fd, size);
	if (ret < 0) {
		pr_err("Failed to truncate memfd\n");
		return NULL;
	}

	create.memfd	= isp->mem_fd;
	create.offset	= 0;
	create.size	= size;

	ret = ioctl(isp->udmabuf_fd, UDMABUF_CREATE, &create);
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
		libisp_dmabuf_put(buf);
		buf = NULL;
	}

	return buf;
}

void libisp_buffers_list_put(struct libisp_buffers_list *list)
{
	u32 i;

	for (i = 0; i < list->size; i++) {
		if (!list->bufs[i])
			break;

		libisp_dmabuf_put(list->bufs[i]);
	}

	free(list->ids);
	free(list->bufs);
	free(list);
}

struct libisp_buffers_list *libisp_buffers_list_get(struct libisp *isp,
						    u32 num_buffers,
						    u32 num_pages)
{
	struct libisp_buffers_list *list;
	u32 i;

	if (num_buffers == 0 || num_buffers > ISP_RW_INSN_MAX_NUM_BUFFERS)
		return NULL;

	list = calloc(1, sizeof(*list));
	if (!list)
		return NULL;

	list->size	= num_buffers;
	list->ids	= calloc(num_buffers, sizeof(u64));
	list->bufs	= calloc(num_buffers, sizeof(struct libisp_dmabuf));
	if (!list->ids || !list->bufs)
		goto error;

	for (i = 0; i < num_buffers; i++) {
		list->bufs[i] = libisp_dmabuf_get(isp, num_pages);
		if (!list->bufs[i])
			goto error;
	}

	return list;

error:
	libisp_buffers_list_put(list);
	return NULL;
}
