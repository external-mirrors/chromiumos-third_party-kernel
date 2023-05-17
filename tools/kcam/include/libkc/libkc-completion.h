/* SPDX-License-Identifier: GPL-2.0 */
/*
 * libkc completion event
 *
 * Copyright (C) 2022 Google LLC
 */

#ifndef LIBKC_COMPLETION_H_
#define LIBKC_COMPLETION_H_

#include "../../../include/uapi/linux/cam.h"

struct libkc;

struct libkc_completion {
	unsigned int		num_entries;
	size_t			entries_size;
	struct cam_completion	entries[];
};

void libkc_completion_put(struct libkc_completion *lcc);
struct libkc_completion *libkc_completion_get(uint32_t num_events);
int libkc_completion_read(struct libkc *cam, struct libkc_completion *lcc);

struct cam_completion *libkc_completion_at(struct libkc_completion *lcc,
					    u32 idx);

#define for_each_cam_completion(c,i,e)				\
	for ((i) = 0, (e) = libkc_completion_at((c), (i));	\
	     (i) < (c)->num_entries &&				\
	     ((e) = libkc_completion_at((c), (i)));		\
	     (i)++)

#endif /* LIBKC_OPERATION_H_ */
