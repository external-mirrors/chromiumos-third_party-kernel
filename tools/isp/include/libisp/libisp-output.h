// SPDX-License-Identifier: GPL-2.0
/*
 * libisp output
 *
 * Copyright (C) Google LLC
 */

#ifndef LIBISP_OUTPUT_H_
#define LIBISP_OUTPUT_H_

struct libisp_iterator {
	void		*base;
	uint32_t	offt;
};

void libisp_iterator_init(struct isp_header *hdr,
			  struct libisp_iterator *iter);

int libisp_output_get(struct isp_header *hdr, uint32_t sz);
void libisp_output_put(struct isp_header *hdr);

#endif /* LIBISP_OUTPUT_H_ */
