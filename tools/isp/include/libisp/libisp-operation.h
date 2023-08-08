// SPDX-License-Identifier: GPL-2.0
/*
 * libisp operation
 *
 * Copyright (C) Google LLC
 */

#ifndef LIBISP_OPERATION_H_
#define LIBISP_OPERATION_H_

#include "../../../include/uapi/linux/isp.h"

struct libisp;

struct libisp_operation {
	struct isp_header	hdr;
	struct isp_operation	ents[];
} __attribute__((packed));

struct libisp_rw_list {
	u32				num_ents;
	struct isp_rw_instruction	ents[];
} __attribute__((packed));

struct libisp_operation *libisp_operation_get(uint32_t num_operations);
int libisp_operation_ioctl(struct libisp *isp, struct libisp_operation *lio);
void libisp_operation_put(struct libisp_operation *lio);

struct libisp_rw_list *libisp_rw_list_get(uint32_t num_rw_entries);
void libisp_rw_list_put(struct libisp_rw_list *rwl);

struct isp_operation *libisp_operation_at(struct libisp_operation *lio,
					  u32 idx);

#define for_each_isp_operation(q, i, e)					\
	for ((i) = 0, (e) = libisp_operation_at((q), (i));		\
	     (i) < (q)->hdr.num_requests &&				\
	     ((e) = libisp_operation_at((q), (i)));			\
	     (i)++)

struct isp_rw_instruction *libisp_rw_instruction_at(struct libisp_rw_list *rw,
						    u32 idx);

#define for_each_rw_instruction(q, i, e)				\
	for ((i) = (0), (e) = libisp_rw_instruction_at((q), (i));	\
	     (i) < (q)->num_ents &&					\
	     ((e) = libisp_rw_instruction_at((q), (i)));		\
	     (i)++)

struct isp_rw_instruction *libisp_failed_instruction(struct isp_operation *op,
						     u32 *idx);
#endif /* LIBISP_OPERATION_H_ */
