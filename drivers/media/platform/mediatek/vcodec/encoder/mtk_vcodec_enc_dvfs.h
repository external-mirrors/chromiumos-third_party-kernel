/* SPDX-License-Identifier: GPL-2.0*/
/*
 * Copyright (c) 2024 MediaTek Inc.
 */

#ifndef _MTK_VCODEC_ENC_DVFS_H_
#define _MTK_VCODEC_ENC_DVFS_H_

#include <linux/list.h>
#include <linux/minmax.h>
#include <linux/types.h>

#include "../common/mtk_vcodec_cmn_drv.h"

struct mtk_vcodec_enc_ctx;
struct mtk_vcodec_enc_dev;

#define MAX_CODEC_FREQ_STEP	10
#define MTK_VENC_PORT_NUM	128

struct venc_dvfs_config {
	u16 tput_num;
	u16 larb_num;
	u16 larb_hw_num;
};

struct venc_dvfs_perf {
	unsigned int codec_type;
	unsigned int codec_fmt;
	unsigned int config;
	unsigned int cy_per_mb;
	unsigned int mb_thresh;
	unsigned int base_freq;
	unsigned int bw_factor;
};

struct venc_dvfs_larb_bw {
	unsigned int larb_id;
	unsigned int larb_base_bw_r;
	unsigned int larb_base_bw_w;
	unsigned int target_bw_r;
	unsigned int target_bw_w;
	struct icc_path *larb_qos_req[2];
};

struct venc_dvfs_pc_bw {
	unsigned int hw_id;
	unsigned int target_bw_r;
	unsigned int target_bw_w;
	unsigned int reg_offset;
};

struct venc_dvfs_params {
	u8 codec_type;
	u32 freq_sum;
	u32 target_freq;
	u32 target_bw_factor;
};

struct venc_dvfs_inst {
	int id;
	u8 codec_type;
	u32 codec_fmt;
	u32 yuv_fmt;
	s32 fps;
	u32 width;
	u32 height;
	s32 config;
	struct list_head list;
	struct mtk_vcodec_enc_ctx *ctx;
};

struct mtk_vcodec_enc_dvfs {
	bool dvfs_data_filled;
	int venc_freq_cnt;
	unsigned long venc_freqs[MAX_CODEC_FREQ_STEP];

	struct clk *venc_mmdvfs_clk;
	struct regulator *venc_reg;
	struct venc_dvfs_params venc_dvfs_params;
	struct list_head venc_dvfs_inst_list;
	struct venc_dvfs_larb_bw *venc_larb_bw;
	struct venc_dvfs_pc_bw *venc_pc_bw;
	struct venc_dvfs_perf *venc_tput;
};

void mtk_venc_dvfs_tbl_init(struct mtk_vcodec_enc_dev *dev);
void mtk_venc_dvfs_tbl_deinit(struct mtk_vcodec_enc_dev *dev);

void mtk_venc_prepare_dvfs(struct mtk_vcodec_enc_dev *dev);
void mtk_venc_unprepare_dvfs(struct mtk_vcodec_enc_dev *dev);
void mtk_venc_prepare_emi_bw(struct mtk_vcodec_enc_dev *dev);

void mtk_venc_dvfs_begin_inst(struct mtk_vcodec_enc_ctx *ctx);
void mtk_venc_dvfs_end_inst(struct mtk_vcodec_enc_ctx *ctx);

void mtk_venc_pmqos_begin_inst(struct mtk_vcodec_enc_ctx *ctx);
void mtk_venc_pmqos_end_inst(struct mtk_vcodec_enc_ctx *ctx);
void mtk_venc_pmqos_begin_frame(struct mtk_vcodec_enc_ctx *ctx);

bool mtk_venc_need_update(struct mtk_vcodec_enc_ctx *ctx);
void mtk_venc_update_freq(struct mtk_vcodec_enc_dev *dev);
bool mtk_venc_remove_update(struct mtk_vcodec_enc_ctx *ctx);

void mtk_venc_fill_dvfs_config(struct mtk_vcodec_enc_dev *dev, unsigned int *dvfs_data,
			       unsigned int length);
#endif
