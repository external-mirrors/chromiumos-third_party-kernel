/* SPDX-License-Identifier: GPL-2.0 */
/*
 * libkc operation
 *
 * Copyright (C) Google LLC
 */

#include <libkc/libkc.h>

struct cam_operation *libkc_operation_at(struct libkc_operation *lco, u32 idx)
{
	if (lco->hdr.num_requests == 0) {
		LIBKC_BUG();
		return NULL;
	}

	if (idx >= lco->hdr.num_requests) {
		LIBKC_BUG();
		return NULL;
	}

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
		if (op->operation_add.rd_wr_list == CAM_OP_NO_RW_LIST)
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

	lco->hdr.num_requests	= num_operations;

	return lco;
}

struct cam_rw_instruction *libkc_rw_instruction_at(struct libkc_rw_list *rw,
						   u32 idx)
{
	if (rw->num_ents == 0) {
		LIBKC_BUG();
		return NULL;
	}

	if (idx >= rw->num_ents) {
		LIBKC_BUG();
		return NULL;
	}

	return &rw->ents[idx];
}

struct cam_rw_instruction *libkc_failed_instruction(struct cam_operation *op,
						    u32 *idx)
{
	struct libkc_rw_list *rw;

	if (op->operation_type != CAM_OPERATION_TYPE_ADD)
		return NULL;

	if (op->operation_add.rd_wr_list == CAM_OP_NO_RW_LIST)
		return NULL;

	rw = (struct libkc_rw_list *)op->operation_add.rd_wr_list;
	if (rw->num_ents == 0)
		return NULL;

	if (*idx >= rw->num_ents)
		return NULL;

	while (*idx < rw->num_ents) {
		struct cam_rw_instruction *insn = &rw->ents[*idx];

		*idx = *idx + 1;
		if (insn->error == 0)
			continue;
		pr_err("Failed instruction at: %d, error: %d\n",
		       *idx - 1, insn->error);
		return insn;
	}

	return NULL;
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
		lco->hdr.num_requests * sizeof(struct cam_operation);

	if ((sz & _IOC_SIZEMASK) != sz)
		pr_err("IOCTL payload size overflow: %u\n", sz);

	return libkc_ioctl(cam, CAM_IOC_OPERATION(sz), lco);
}
