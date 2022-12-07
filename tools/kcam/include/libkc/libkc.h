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

struct libkc {
	int32_t			fd;
	int32_t			mem_fd;
	int32_t			udmabuf_fd;
	uint64_t		completion_seqno;
};

struct libkc *libkc_open(const char *dev);
void libkc_close(struct libkc *cam);
int libkc_ioctl(struct libkc *cam, int cmd, void *payload);

#endif /* LIBKC_H_ */
