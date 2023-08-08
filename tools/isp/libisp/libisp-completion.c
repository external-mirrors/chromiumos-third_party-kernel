// SPDX-License-Identifier: GPL-2.0
/*
 * libisp events
 *
 * Copyright (C) Google LLC
 */

#include <unistd.h>

#include <libisp/libisp.h>

struct isp_completion *libisp_completion_at(struct libisp_completion *lic,
					    u32 idx)
{
	if (lic->num_entries == 0) {
		LIBISP_BUG();
		return NULL;
	}

	if (idx >= lic->num_entries) {
		LIBISP_BUG();
		return NULL;
	}

	return &lic->entries[idx];
}

void libisp_completion_put(struct libisp_completion *lic)
{
	if (!lic)
		return;

	free(lic);
}

struct libisp_completion *libisp_completion_get(uint32_t num_events)
{
	struct libisp_completion *lce;
	size_t sz;

	if (num_events < 1)
		return NULL;

	sz = sizeof(struct libisp_completion) +
		num_events * sizeof(struct isp_completion);
	lce = calloc(1, sz);
	if (!lce) {
		pr_err("OOM\n");
		return NULL;
	}
	lce->entries_size = num_events * sizeof(struct isp_completion);
	return lce;
}

int libisp_completion_read(struct libisp *isp, struct libisp_completion *lic)
{
	struct isp_completion *entries;
	size_t entries_sz;
	ssize_t len, ret;

	entries_sz = lic->entries_size;
	entries = lic->entries;
	lic->num_entries = 0;

	while (entries_sz != 0 && (ret = read(isp->fd, entries, entries_sz))) {
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			pr_err("read() error %s\n", strerror(errno));
			return ret;
		}

		if (ret && ret % sizeof(struct isp_completion) != 0) {
			pr_err("Partial read of size: %ld, struct size: %lu\n",
			       ret, sizeof(struct isp_completion));
			return -EINVAL;
		}

		entries_sz -= ret;
		entries += ret / sizeof(struct isp_completion);
		lic->num_entries += ret / sizeof(struct isp_completion);
	}

	return 0;
}
