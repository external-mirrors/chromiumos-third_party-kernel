/* SPDX-License-Identifier: GPL-2.0 */
/*
 * libkc output
 *
 * Copyright (C) 2022 Google LLC
 */

#ifndef LIBKC_OUTPUT_H_
#define LIBKC_OUTPUT_H_

struct libkc_iterator {
	void		*base;
	uint32_t	offt;
};

void libkc_iterator_init(struct cam_header *hdr, struct libkc_iterator *iter);

int libkc_output_get(struct cam_header *hdr, uint32_t sz);
void libkc_output_put(struct cam_header *hdr);

#endif /* LIBKC_OUTPUT_H_ */
