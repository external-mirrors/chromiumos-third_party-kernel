// SPDX-License-Identifier: GPL-2.0
/*
 * libisp operation
 *
 * Copyright (C) Google LLC
 */

#include <libisp/libisp.h>

struct isp_operation *libisp_operation_at(struct libisp_operation *lio,
					  u32 idx)
{
	if (lio->hdr.num_requests == 0) {
		LIBISP_BUG();
		return NULL;
	}

	if (idx >= lio->hdr.num_requests) {
		LIBISP_BUG();
		return NULL;
	}

	return &lio->ents[idx];
}

void libisp_operation_put(struct libisp_operation *lio)
{
	struct isp_operation *op;
	int i;

	if (!lio)
		return;

	for_each_isp_operation(lio, i, op) {
		if (op->operation_type != ISP_OPERATION_TYPE_ADD)
			break;
		if (op->operation_add.rd_wr_list == ISP_OP_NO_RW_LIST)
			continue;
		libisp_rw_list_put((void *)op->operation_add.rd_wr_list);
	}

	free(lio);
}

struct libisp_operation *libisp_operation_get(uint32_t num_operations)
{
	struct libisp_operation *lio;
	struct isp_operation *o;
	size_t sz;
	int i;

	if (num_operations < 1)
		return NULL;

	sz = sizeof(struct libisp_operation) +
		num_operations * sizeof(struct isp_operation);
	lio = calloc(1, sz);

	if (!lio) {
		pr_err("OOM\n");
		return NULL;
	}

	lio->hdr.num_requests	= num_operations;

	return lio;
}

struct isp_rw_instruction *libisp_rw_instruction_at(struct libisp_rw_list *rw,
						    u32 idx)
{
	if (rw->num_ents == 0) {
		LIBISP_BUG();
		return NULL;
	}

	if (idx >= rw->num_ents) {
		LIBISP_BUG();
		return NULL;
	}

	return &rw->ents[idx];
}

struct isp_rw_instruction *libisp_failed_instruction(struct isp_operation *op,
						     u32 *idx)
{
	struct libisp_rw_list *rw;

	if (op->operation_type != ISP_OPERATION_TYPE_ADD)
		return NULL;

	if (op->operation_add.rd_wr_list == ISP_OP_NO_RW_LIST)
		return NULL;

	rw = (struct libisp_rw_list *)op->operation_add.rd_wr_list;
	if (rw->num_ents == 0)
		return NULL;

	if (*idx >= rw->num_ents)
		return NULL;

	while (*idx < rw->num_ents) {
		struct isp_rw_instruction *insn = &rw->ents[*idx];

		*idx = *idx + 1;
		if (insn->error == 0)
			continue;
		pr_err("Failed instruction at: %d, error: %d\n",
		       *idx - 1, insn->error);
		return insn;
	}

	return NULL;
}

struct libisp_rw_list *libisp_rw_list_get(uint32_t num_ents)
{
	struct libisp_rw_list *rwl;
	size_t sz;

	if (!num_ents)
		return NULL;

	sz = sizeof(struct libisp_rw_list) +
		num_ents * sizeof(struct isp_rw_instruction);
	rwl = calloc(1, sz);
	if (!rwl)
		return NULL;

	rwl->num_ents = num_ents;
	return rwl;
}

void libisp_rw_list_put(struct libisp_rw_list *rwl)
{
	if (!rwl)
		return;

	free(rwl);
}

int libisp_operation_ioctl(struct libisp *isp, struct libisp_operation *lio)
{
	size_t sz;

	sz = sizeof(struct isp_header) +
		lio->hdr.num_requests * sizeof(struct isp_operation);

	if ((sz & _IOC_SIZEMASK) != sz)
		pr_err("IOCTL payload size overflow: %u\n", sz);

	return libisp_ioctl(isp, ISP_IOC_OPERATION(sz), lio);
}
