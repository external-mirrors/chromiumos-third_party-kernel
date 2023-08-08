/* SPDX-License-Identifier: GPL-2.0 */
/*
 * libkc operation
 *
 * Copyright (C) Google LLC
 */

#ifndef LIBKC_OPERATION_H_
#define LIBKC_OPERATION_H_

#include "../../../include/uapi/linux/cam.h"

struct libkc;

struct libkc_operation {
	struct cam_header	hdr;
	struct cam_operation	ents[];
} __attribute__((packed));

struct libkc_rw_list {
	u32				num_ents;
	struct cam_rw_instruction	ents[];
} __attribute__((packed));

struct libkc_operation *libkc_operation_get(uint32_t num_operations);
int libkc_operation_ioctl(struct libkc *cam, struct libkc_operation *lco);
void libkc_operation_put(struct libkc_operation *lco);

struct libkc_rw_list *libkc_rw_list_get(uint32_t num_rw_entries);
void libkc_rw_list_put(struct libkc_rw_list *rwl);

struct cam_operation *libkc_operation_at(struct libkc_operation *lco,
					 u32 idx);

#define for_each_cam_operation(q,i,e)					\
	for ((i) = 0, (e) = libkc_operation_at((q), (i));		\
	     (i) < (q)->hdr.num_requests &&				\
	     ((e) = libkc_operation_at((q), (i)));			\
	     (i)++)

struct cam_rw_instruction *libkc_rw_instruction_at(struct libkc_rw_list *rw,
						   u32 idx);

#define for_each_rw_instruction(q,i,e)					\
	for ((i) = (0), (e) = libkc_rw_instruction_at((q), (i));	\
	     (i) < (q)->num_ents &&					\
	     ((e) = libkc_rw_instruction_at((q), (i)));			\
	     (i)++)

struct cam_rw_instruction *libkc_failed_instruction(struct cam_operation *op,
						    u32 *idx);
#endif /* LIBKC_OPERATION_H_ */
