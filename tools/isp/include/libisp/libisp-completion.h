// SPDX-License-Identifier: GPL-2.0
/*
 * libisp completion event
 *
 * Copyright (C) Google LLC
 */

#ifndef LIBISP_COMPLETION_H_
#define LIBISP_COMPLETION_H_

#include "../../../include/uapi/linux/isp.h"

struct libisp;

struct libisp_completion {
	unsigned int		num_entries;
	size_t			entries_size;
	struct isp_completion	entries[];
};

void libisp_completion_put(struct libisp_completion *lic);
struct libisp_completion *libisp_completion_get(uint32_t num_events);
int libisp_completion_read(struct libisp *isp,
			   struct libisp_completion *lic);

struct isp_completion *libisp_completion_at(struct libisp_completion *lic,
					    u32 idx);

#define for_each_isp_completion(c, i, e)			\
	for ((i) = 0, (e) = libisp_completion_at((c), (i));	\
	     (i) < (c)->num_entries &&				\
	     ((e) = libisp_completion_at((c), (i)));		\
	     (i)++)

#endif /* LIBISP_OPERATION_H_ */
