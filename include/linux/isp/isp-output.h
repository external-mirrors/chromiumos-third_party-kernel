/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ISP user space output
 *
 * Copyright (C) Google LLC
 * Copyright (c) Intel Corporation
 */

#ifndef __LINUX_ISP_OUTPUT_H__
#define __LINUX_ISP_OUTPUT_H__

#include <linux/types.h>

/**
 * isp_koutput - Describe user output data buffer
 *
 * This struct mirrors struct isp_output in UAPI.
 */
struct isp_koutput {
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

struct isp_header;

int isp_output_init(struct isp_header *hdr, struct isp_koutput *output);
bool isp_output_has_buffer(struct isp_koutput *output);

void *__isp_output_next_entry(struct isp_koutput *output, size_t sz);

#define isp_output_next_entry(output, obj)			\
	((obj) = __isp_output_next_entry((output), sizeof(*(obj))))

#endif /* __LINUX_ISP_OUTPUT_H__ */
