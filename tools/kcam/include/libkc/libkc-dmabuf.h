/* SPDX-License-Identifier: GPL-2.0 */
/*
 * libkc dmabuf (udmabuf)
 *
 * Copyright (C) 2022 Google LLC
 */

#ifndef LIBKC_DMABUF_H_
#define LIBKC_DMABUF_H_

#include "../../../include/uapi/linux/cam.h"

struct libkc;

struct libkc_dmabuf {
	char		*va;
	int32_t		fd;
	u32		num_pages;
};

struct libkc_buffers_list {
	u32			size;
	u64			*ids;
	struct libkc_dmabuf	**bufs;
};

void libkc_dmabuf_put(struct libkc_dmabuf *buf);
struct libkc_dmabuf *libkc_dmabuf_get(struct libkc *cam, u32 num_pages);

struct libkc_buffers_list *libkc_buffers_list_get(struct libkc *cam,
						  u32 num_buffers,
						  u32 num_pages);
void libkc_buffers_list_put(struct libkc_buffers_list *list);

#endif /* LIBKC_DMABUF_H_ */
