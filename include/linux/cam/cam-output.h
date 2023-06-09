/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CAM user space output
 *
 * Copyright (C) 2022 Google LLC
 * Copyright (c) 2021 Intel Corporation
 */

#ifndef __LINUX_CAM_OUTPUT_H__
#define __LINUX_CAM_OUTPUT_H__

#include <linux/types.h>

/**
 * cam_koutput - Describe user output data buffer
 *
 * This struct mirrors struct cam_output in UAPI.
 */
struct cam_koutput {
	/** @origin: The memory buffer */
	void __user	*origin;
	/** @base: Current available pointer to the buffer */
	void __user	*base;
	/** @end: The end of the memory buffer */
	void __user	*end;
	/** @length: Length of the data available (may be larger than size) */
	__u32		length;
	/** @num_entries: The number of written entries */
	__u32		num_entries;
};

struct cam_header;

int cam_output_init(struct cam_header *hdr, struct cam_koutput *output);
bool cam_output_has_buffer(struct cam_koutput *output);

void *__cam_output_next_entry(struct cam_koutput *output, size_t sz);

#define cam_output_next_entry(output, obj)			\
	(obj) = __cam_output_next_entry((output), sizeof(*(obj)))

#endif /* __LINUX_CAM_OUTPUT_H__ */
