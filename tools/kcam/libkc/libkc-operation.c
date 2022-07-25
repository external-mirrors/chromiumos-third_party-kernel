/* SPDX-License-Identifier: GPL-2.0 */
/*
 * libkc operation
 *
 * Copyright (C) 2022 Google LLC
 */

#include <libkc/libkc.h>

struct cam_operation *libkc_operation_at(struct libkc_operation *lco, u32 idx)
{
	if (lco->hdr.num_queries == 0)
		return NULL;

	if (idx > lco->hdr.num_queries)
		return NULL;

	return &lco->ents[idx];
}

void libkc_operation_put(struct libkc_operation *lco)
{
	struct cam_operation *op;
	int i;

	if (!lco)
		return;

	for_each_cam_operation(lco, i, op) {
		if (op->operation_type != CAM_OPERATION_TYPE_ADD)
			break;
		if (op->operation_add.rd_wr_list == CAM_NO_RD_WR)
			continue;
		libkc_rw_list_put((void *)op->operation_add.rd_wr_list);
	}

	free(lco);
}

struct libkc_operation *libkc_operation_get(uint32_t num_operations)
{
	struct libkc_operation *lco;
	struct cam_operation *o;
	size_t sz;
	int i;

	if (num_operations < 1)
		return NULL;

	sz = sizeof(struct libkc_operation) +
		num_operations * sizeof(struct cam_operation);
	lco = calloc(1, sz);

	if (!lco) {
		pr_err("OOM\n");
		return NULL;
	}

	lco->hdr.num_queries	= num_operations;
	lco->hdr.length		= sz;

	return lco;
}

struct cam_rw_instruction *libkc_rw_instruction_at(struct libkc_rw_list *rw,
						   u32 idx)
{
	if (rw->num_ents == 0)
		return NULL;

	if (idx > rw->num_ents)
		return NULL;

	return &rw->ents[idx];
}

struct libkc_rw_list *libkc_rw_list_get(uint32_t num_ents)
{
	struct libkc_rw_list *rwl;
	size_t sz;

	if (!num_ents)
		return NULL;

	sz = sizeof(struct libkc_rw_list) +
		num_ents * sizeof(struct cam_rw_instruction);
	rwl = calloc(1, sz);
	if (!rwl)
		return NULL;

	rwl->num_ents = num_ents;
	return rwl;
}

void libkc_rw_list_put(struct libkc_rw_list *rwl)
{
	if (!rwl)
		return;

	free(rwl);
}

int libkc_operation_ioctl(struct libkc *cam, struct libkc_operation *lco)
{
	size_t sz;

	sz = sizeof(struct cam_header) +
		lco->hdr.num_queries * sizeof(struct cam_operation);
	return libkc_ioctl(cam, CAM_IOC_OPERATION(sz), lco);
}
