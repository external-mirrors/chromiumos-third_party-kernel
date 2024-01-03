/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2023-2024 Google LLC.
 */

#ifndef __INTEL_DPRX_SINK_H__
#define __INTEL_DPRX_SINK_H__

#include <linux/kernel.h>

#define DP_SINK_MAX_EDID_BLOCKS 4

struct msg_transaction {
	u8 buf[256];
	int len;
	int written;
	u8 rad[16];
	int link_count_total;
};

struct dpcd_mem {
	u8 caps[0x10];        /* 00000 - 0000f */
	u8 mstm_cap;          /* 00021         */
	u8 guid[0x10];        /* 00030 - 0003f */
	u8 link_conf[0x3];    /* 00100 - 00102 */
	u8 mstm_ctrl;         /* 00111         */
	u8 vc_alloc[0x3];     /* 001c0 - 001c2 */
	u8 sink_count;        /* 00200         */
	u8 irq_vector;        /* 00201         */
	u8 lane_align_status; /* 00204         */
	u8 vc_table_status;   /* 0x2c0         */
	u8 vc_table[0x40];    /* 002c1 - 002ff */
	u8 sink_spec[0xc];    /* 00400 - 0040b */
	u8 down_req[0x30];    /* 01000 - 01030 */
	u8 down_rep[0x30];    /* 01400 - 01430 */
};

struct dp_sink {
	u8 edid[128 * DP_SINK_MAX_EDID_BLOCKS];
	int blocks;
	int offset;
	int segment;
};

struct intel_dprx;

struct dp_sink_device {
	int mst;
	int sink_count;
	struct intel_dprx *dprx;

	struct dp_sink sinks[4];
	u8 vc_id[4];

	int total_pbn;
	int sum_pbn;

	struct dpcd_mem dpcd;

	struct msg_transaction mt_req[2];
	struct msg_transaction mt_rep[2];
	bool mt_seq_no;
};

struct dp_aux_buf {
	u8 data[20];
	int len;
};

void dp_sink_device_init(struct dp_sink_device *sink_dev,
			 struct intel_dprx *dprx, int mst_count);

void dp_sink_device_reset(struct dp_sink_device *sink_dev);

void dp_sink_device_handle_request(struct dp_sink_device *sink_dev,
				   struct dp_aux_buf *req,
				   struct dp_aux_buf *rep);

void intel_dprx_pulse_hpd(struct intel_dprx *dprx);
void intel_dprx_set_link_rate(struct intel_dprx *dprx, int val);
void intel_dprx_set_lane_count(struct intel_dprx *dprx, int val);
void intel_dprx_set_training_pattern(struct intel_dprx *dprx, int val);
void intel_dprx_set_scrambler(struct intel_dprx *dprx, int val);
int intel_dprx_get_cr_lock(struct intel_dprx *dprx);
int intel_dprx_get_sym_lock(struct intel_dprx *dprx);
int intel_dprx_get_interlane_align(struct intel_dprx *dprx);
int intel_dprx_get_sink_status(struct intel_dprx *dprx);
void intel_dprx_set_mst(struct intel_dprx *dprx, int val);
void intel_dprx_clear_vc_payload_table(struct intel_dprx *dprx);
void intel_dprx_set_vc_payload_table(struct intel_dprx *dprx, u8 *table);
void intel_dprx_set_vc_ids(struct intel_dprx *dprx, u8 *ids);
int intel_dprx_get_act(struct intel_dprx *dprx);
void intel_dprx_clear_act(struct intel_dprx *dprx);

#endif
