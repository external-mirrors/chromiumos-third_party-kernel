/* SPDX-License-Identifier: GPL-2.0 */
/*
 * libkc events
 *
 * Copyright (C) 2022 Google LLC
 */

#include <unistd.h>

#include <libkc/libkc.h>

struct cam_completion *libkc_completion_at(struct libkc_completion *lcc,
					   u32 idx)
{
	if (lcc->num_entries == 0) {
		LIBKC_BUG();
		return NULL;
	}

	if (idx >= lcc->num_entries) {
		LIBKC_BUG();
		return NULL;
	}

	return &lcc->entries[idx];
}

void libkc_completion_put(struct libkc_completion *lcc)
{
	if (!lcc)
		return;

	free(lcc);
}

struct libkc_completion *libkc_completion_get(uint32_t num_events)
{
	struct libkc_completion *lce;
	size_t sz;

	if (num_events < 1)
		return NULL;

	sz = sizeof(struct libkc_completion) +
		num_events * sizeof(struct cam_completion);
	lce = calloc(1, sz);
	if (!lce) {
		pr_err("OOM\n");
		return NULL;
	}
	lce->entries_size = num_events * sizeof(struct cam_completion);
	return lce;
}

int libkc_completion_read(struct libkc *cam, struct libkc_completion *lcc)
{
	struct cam_completion *entries;
	size_t entries_sz;
	ssize_t len, ret;

	entries_sz = lcc->entries_size;
	entries = lcc->entries;
	lcc->num_entries = 0;

	while (entries_sz != 0 && (ret = read(cam->fd, entries, entries_sz))) {
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			pr_err("read() error %s\n", strerror(errno));
			return ret;
		}

		if (ret && ret % sizeof(struct cam_completion) != 0) {
			pr_err("Partial read of size: %ld, struct size: %lu\n",
			       ret, sizeof(struct cam_completion));
			return -EINVAL;
		}

		entries_sz -= ret;
		entries += ret / sizeof(struct cam_completion);
		lcc->num_entries += ret / sizeof(struct cam_completion);
	}

	return 0;
}
