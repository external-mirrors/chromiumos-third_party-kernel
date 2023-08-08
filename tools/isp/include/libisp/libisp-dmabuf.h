// SPDX-License-Identifier: GPL-2.0
/*
 * libisp dmabuf (udmabuf)
 *
 * Copyright (C) Google LLC
 */

#ifndef LIBISP_DMABUF_H_
#define LIBISP_DMABUF_H_

#include "../../../include/uapi/linux/isp.h"

struct libisp;

struct libisp_dmabuf {
	char		*va;
	int32_t		fd;
	u32		num_pages;
};

struct libisp_buffers_list {
	u32			size;
	u64			*ids;
	struct libisp_dmabuf	**bufs;
};

void libisp_dmabuf_put(struct libisp_dmabuf *buf);
struct libisp_dmabuf *libisp_dmabuf_get(struct libisp *isp, u32 num_pages);

struct libisp_buffers_list *libisp_buffers_list_get(struct libisp *isp,
						    u32 num_buffers,
						    u32 num_pages);
void libisp_buffers_list_put(struct libisp_buffers_list *list);

#endif /* LIBISP_DMABUF_H_ */
