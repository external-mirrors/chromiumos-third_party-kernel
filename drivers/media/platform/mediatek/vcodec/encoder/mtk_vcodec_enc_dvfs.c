// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024 MediaTek Inc.
 */

#include <linux/list.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/vmalloc.h>

#include "mtk_vcodec_enc_drv.h"
#include "mtk_vcodec_enc_dvfs.h"

#define BW_FACTOR_8_BIT		100
#define BW_FACTOR_NONAFBC	114

#define DEFAULT_FREQ		100000000ULL
#define VENC_MB_SIZE		256
#define VENC_TPUT_OFFSET	6
#define VENC_LARB_OFFSET	3
#define VENC_LARB_HW_OFSSET	4

void mtk_venc_dvfs_tbl_init(struct mtk_vcodec_enc_dev *dev)
{
	const struct mtk_vcodec_enc_pdata *pdata = dev->venc_pdata;
	struct mtk_vcodec_enc_dvfs *venc_dvfs = &dev->venc_dvfs;

	venc_dvfs->venc_tput =
		vzalloc(pdata->dvfs_cfg.tput_num * sizeof(struct venc_dvfs_perf));
	if (!venc_dvfs->venc_tput)
		return;

	venc_dvfs->venc_larb_bw =
		vzalloc(pdata->dvfs_cfg.larb_num * sizeof(struct venc_dvfs_larb_bw));
	if (!venc_dvfs->venc_larb_bw)
		goto init_failed;

	venc_dvfs->venc_pc_bw =
		vzalloc(pdata->dvfs_cfg.larb_hw_num * sizeof(struct venc_dvfs_pc_bw));
	if (!venc_dvfs->venc_pc_bw)
		goto init_failed;

	venc_dvfs->dvfs_data_filled = false;
	return;

init_failed:
	mtk_venc_dvfs_tbl_deinit(dev);
}

void mtk_venc_dvfs_tbl_deinit(struct mtk_vcodec_enc_dev *dev)
{
	struct mtk_vcodec_enc_dvfs *venc_dvfs = &dev->venc_dvfs;

	if (venc_dvfs->venc_tput) {
		vfree(venc_dvfs->venc_tput);
		venc_dvfs->venc_tput = NULL;
	}

	if (venc_dvfs->venc_larb_bw) {
		vfree(venc_dvfs->venc_larb_bw);
		venc_dvfs->venc_larb_bw = NULL;
	}

	if (venc_dvfs->venc_pc_bw) {
		vfree(venc_dvfs->venc_pc_bw);
		venc_dvfs->venc_pc_bw = NULL;
	}
}

void mtk_venc_fill_dvfs_config(struct mtk_vcodec_enc_dev *dev, unsigned int *dvfs_data,
			       unsigned int length)
{
	const struct mtk_vcodec_enc_pdata *pdata = dev->venc_pdata;
	struct mtk_vcodec_enc_dvfs *venc_dvfs = &dev->venc_dvfs;
	unsigned int i;
	unsigned int offset = 0;

	if (!dvfs_data || venc_dvfs->dvfs_data_filled)
		return;

	if ((pdata->dvfs_cfg.tput_num * VENC_TPUT_OFFSET +
	     pdata->dvfs_cfg.larb_num * VENC_LARB_OFFSET +
	     pdata->dvfs_cfg.larb_hw_num * VENC_LARB_HW_OFSSET) >= length)
		return;

	for (i = 0; i < pdata->dvfs_cfg.tput_num; i++) {
		venc_dvfs->venc_tput[i].codec_fmt = dvfs_data[offset];
		venc_dvfs->venc_tput[i].config = dvfs_data[offset + 1];
		venc_dvfs->venc_tput[i].cy_per_mb = dvfs_data[offset + 2];
		venc_dvfs->venc_tput[i].mb_thresh = dvfs_data[offset + 3];
		venc_dvfs->venc_tput[i].base_freq = dvfs_data[offset + 4];
		venc_dvfs->venc_tput[i].bw_factor = dvfs_data[offset + 5];
		offset += VENC_TPUT_OFFSET;
	}

	for (i = 0; i < pdata->dvfs_cfg.larb_num; i++) {
		venc_dvfs->venc_larb_bw[i].larb_id = dvfs_data[offset];
		venc_dvfs->venc_larb_bw[i].larb_base_bw_r = dvfs_data[offset + 1];
		venc_dvfs->venc_larb_bw[i].larb_base_bw_w = dvfs_data[offset + 2];
		offset += VENC_LARB_OFFSET;
	}

	for (i = 0; i < pdata->dvfs_cfg.larb_hw_num; i++) {
		venc_dvfs->venc_pc_bw[i].hw_id = dvfs_data[offset];
		venc_dvfs->venc_pc_bw[i].target_bw_r = dvfs_data[offset + 1];
		venc_dvfs->venc_pc_bw[i].target_bw_w = dvfs_data[offset + 2];
		venc_dvfs->venc_pc_bw[i].reg_offset = dvfs_data[offset + 3];
		offset += VENC_LARB_HW_OFSSET;
	}

	venc_dvfs->dvfs_data_filled = true;
}

static struct venc_dvfs_inst *mtk_venc_get_inst(struct mtk_vcodec_enc_ctx *ctx)
{
	struct venc_dvfs_inst *inst;
	struct mtk_vcodec_enc_dvfs *venc_dvfs = &ctx->dev->venc_dvfs;

	if (list_empty(&venc_dvfs->venc_dvfs_inst_list))
		return NULL;

	list_for_each_entry(inst, &venc_dvfs->venc_dvfs_inst_list, list) {
		if (inst->id == ctx->id)
			return inst;
	}

	return NULL;
}

static void mtk_venc_get_cfg(struct venc_dvfs_inst *inst,
			     struct mtk_vcodec_enc_ctx *ctx)
{
	int i;
	struct device *dev = &ctx->dev->plat_dev->dev;
	const struct mtk_vcodec_enc_pdata *pdata = ctx->dev->venc_pdata;
	struct mtk_vcodec_enc_dvfs *venc_dvfs = &ctx->dev->venc_dvfs;
	unsigned int mb_per_sec = inst->width * inst->height / VENC_MB_SIZE * inst->fps;
	unsigned int codec_fmt, mb_thresh;

	for (i = 0; i < pdata->dvfs_cfg.tput_num; i++) {
		codec_fmt = venc_dvfs->venc_tput[i].codec_fmt;
		mb_thresh = venc_dvfs->venc_tput[i].mb_thresh;
		dev_dbg(dev, "%s: cfg %d, fmt %x, mb %u\n", __func__, i, codec_fmt, mb_thresh);
		dev_dbg(dev, "%s: t_fmt %u, t_mb %u\n", __func__, inst->codec_fmt, mb_per_sec);
		if (codec_fmt == inst->codec_fmt && mb_per_sec <= mb_thresh) {
			inst->config = venc_dvfs->venc_tput[i].config;
			dev_dbg(dev, "%s: found cfg[%d] = %d\n",
				__func__, i, venc_dvfs->venc_tput[i].config);
			return;
		}
	}

	if (i == pdata->dvfs_cfg.tput_num)
		inst->config = venc_dvfs->venc_tput[i - 1].config;
}

static int mtk_venc_add_inst(struct mtk_vcodec_enc_ctx *ctx)
{
	struct venc_dvfs_inst *new_inst;
	struct device *dev = &ctx->dev->plat_dev->dev;
	struct mtk_vcodec_enc_dvfs *venc_dvfs = &ctx->dev->venc_dvfs;
	unsigned int fps;

	new_inst = vzalloc(sizeof(*new_inst));
	if (!new_inst)
		return -ENOMEM;

	new_inst->id = ctx->id;
	new_inst->codec_fmt = ctx->q_data[MTK_Q_DATA_DST].fmt->fourcc;
	new_inst->yuv_fmt = ctx->q_data[MTK_Q_DATA_SRC].fmt->fourcc;
	fps = ctx->enc_params.framerate_num / ctx->enc_params.framerate_denom;
	new_inst->fps = ctx->enc_params.framerate_denom == 0 ? 0 : fps;
	new_inst->width = ctx->q_data[MTK_Q_DATA_SRC].visible_width;
	new_inst->height = ctx->q_data[MTK_Q_DATA_SRC].visible_height;
	new_inst->ctx = ctx;

	/* Calculate config */
	mtk_venc_get_cfg(new_inst, ctx);

	dev_dbg(dev, "%s: width %u, height %u, fps %d\n",
		__func__, new_inst->width, new_inst->height, new_inst->fps);

	list_add_tail(&new_inst->list, &venc_dvfs->venc_dvfs_inst_list);
	return 0;
}

static bool mtk_venc_setting_changed(struct venc_dvfs_inst *inst,
				     struct mtk_vcodec_enc_ctx *ctx)
{
	if (inst->width != ctx->q_data[MTK_Q_DATA_SRC].visible_width ||
	    inst->height != ctx->q_data[MTK_Q_DATA_SRC].visible_height) {
		inst->width = ctx->q_data[MTK_Q_DATA_SRC].visible_width;
		inst->height = ctx->q_data[MTK_Q_DATA_SRC].visible_height;
		mtk_venc_get_cfg(inst, ctx);
		return true;
	}
	return false;
}

bool mtk_venc_need_update(struct mtk_vcodec_enc_ctx *ctx)
{
	struct venc_dvfs_inst *inst = NULL;

	if (!ctx)
		return false;

	inst = mtk_venc_get_inst(ctx);
	if (!inst) {
		mtk_venc_add_inst(ctx);
		return true;
	}

	if (mtk_venc_setting_changed(inst, ctx))
		return true;

	return false;
}

bool mtk_venc_remove_update(struct mtk_vcodec_enc_ctx *ctx)
{
	struct venc_dvfs_inst *inst = NULL;

	if (!ctx)
		return false;

	inst = mtk_venc_get_inst(ctx);
	if (!inst)
		return false;

	list_del(&inst->list);
	vfree(inst);

	return true;
}

static struct venc_dvfs_perf *mtk_venc_find_perf(struct venc_dvfs_inst *inst,
						 struct mtk_vcodec_enc_dev *dev)
{
	int i;
	const struct mtk_vcodec_enc_pdata *pdata = dev->venc_pdata;
	struct mtk_vcodec_enc_dvfs *venc_dvfs = &dev->venc_dvfs;
	unsigned int codec_fmt, config;

	for (i = 0 ; i < pdata->dvfs_cfg.tput_num; i++) {
		codec_fmt = venc_dvfs->venc_tput[i].codec_fmt;
		config = venc_dvfs->venc_tput[i].config;
		dev_dbg(&dev->plat_dev->dev, "%s: %d ,fmt %u, perf fmt %u\n",
			__func__, i, inst->codec_fmt, codec_fmt);

		if (inst->codec_fmt == codec_fmt && inst->config == config)
			return &venc_dvfs->venc_tput[i];
	}
	dev_err(&dev->plat_dev->dev, "%s: fmt %u cfg %d found no tput\n",
		__func__, inst->codec_fmt, inst->config);

	return NULL;
}

static u64 mtk_venc_calc_freq(struct venc_dvfs_inst *inst,
			      struct mtk_vcodec_enc_dev *dev)
{
	u64 freq;
	u32 mb_cnt;
	struct venc_dvfs_perf *perf = NULL;

	perf = mtk_venc_find_perf(inst, dev);
	if (perf) {
		mb_cnt = inst->width * inst->height / VENC_MB_SIZE;
		freq = (u64)(mb_cnt * inst->fps * perf->cy_per_mb);
		dev_dbg(&dev->plat_dev->dev, "%s: w:%u, h:%u, fps: %d, mb %u\n",
			__func__, inst->width, inst->height, inst->fps, perf->cy_per_mb);
	} else {
		freq = DEFAULT_FREQ;
	}

	dev_dbg(&dev->plat_dev->dev, "%s: freq = %llu\n", __func__, freq);
	return freq;
}

static u64 mtk_venc_match_avail_freq(struct mtk_vcodec_enc_dev *dev, u64 freq)
{
	int i;
	struct mtk_vcodec_enc_dvfs *venc_dvfs = &dev->venc_dvfs;
	unsigned long match_freq = 0;

	match_freq = venc_dvfs->venc_freqs[0];

	for (i = 0; i < venc_dvfs->venc_freq_cnt; i++) {
		dev_dbg(&dev->plat_dev->dev, "%s: i %d, freq %lu, in_freq %llu\n",
			__func__, i, venc_dvfs->venc_freqs[i], freq);

		if (venc_dvfs->venc_freqs[i] >= freq) {
			match_freq = venc_dvfs->venc_freqs[i];
			break;
		}

		match_freq = venc_dvfs->venc_freqs[i];
	}

	dev_dbg(&dev->plat_dev->dev, "%s: match_freq %lu\n", __func__, match_freq);

	return match_freq;
}

static u32 mtk_venc_get_bw_factor(struct mtk_vcodec_enc_dev *dev)
{
	int i;
	const struct mtk_vcodec_enc_pdata *pdata = dev->venc_pdata;
	struct mtk_vcodec_enc_dvfs *venc_dvfs = &dev->venc_dvfs;
	struct venc_dvfs_inst *inst;
	u32 inst_bw_factor, target_bw_factor = 0;
	u32 freq_sum = venc_dvfs->venc_dvfs_params.target_freq;
	u32 freq_scale = 1;
	unsigned int config, codec_fmt, base_freq, bw_factor;

	/*get encoder bw factor*/
	list_for_each_entry(inst, &venc_dvfs->venc_dvfs_inst_list, list) {
		for (i = 0 ; i < pdata->dvfs_cfg.tput_num; i++) {
			config = venc_dvfs->venc_tput[i].config;
			codec_fmt = venc_dvfs->venc_tput[i].codec_fmt;
			if (inst->config == config && inst->codec_fmt == codec_fmt) {
				base_freq = venc_dvfs->venc_tput[i].base_freq;
				bw_factor = venc_dvfs->venc_tput[i].bw_factor;
				freq_scale = freq_sum / base_freq;
				inst_bw_factor = bw_factor * freq_scale
					* BW_FACTOR_8_BIT * BW_FACTOR_NONAFBC;
				dev_dbg(&dev->plat_dev->dev, "%s: %d, %d, %d, %d\n",
					__func__, bw_factor, freq_scale, freq_sum, base_freq);
				dev_dbg(&dev->plat_dev->dev, "%s: %d, %d\n",
					__func__, inst_bw_factor, target_bw_factor);

				target_bw_factor = max(inst_bw_factor, target_bw_factor);
			}
		}
	}

	return target_bw_factor;
}

void mtk_venc_update_freq(struct mtk_vcodec_enc_dev *dev)
{
	struct venc_dvfs_inst *inst;
	struct mtk_vcodec_enc_dvfs *venc_dvfs = &dev->venc_dvfs;
	u64 freq = 0;
	u64 freq_sum = 0;
	u32 target_bw_factor;

	if (list_empty(&venc_dvfs->venc_dvfs_inst_list)) {
		freq_sum = mtk_venc_match_avail_freq(dev, 0);
		venc_dvfs->venc_dvfs_params.freq_sum = (u32)freq_sum;
		venc_dvfs->venc_dvfs_params.target_freq = (u32)freq_sum;
		return;
	}

	list_for_each_entry(inst, &venc_dvfs->venc_dvfs_inst_list, list) {
		freq = mtk_venc_calc_freq(inst, dev);
		freq_sum += freq;
	}
	dev_dbg(&dev->plat_dev->dev, "%s: freq_sum = %llu\n", __func__, freq_sum);

	venc_dvfs->venc_dvfs_params.freq_sum = freq_sum;
	freq_sum = mtk_venc_match_avail_freq(dev, freq_sum);

	venc_dvfs->venc_dvfs_params.target_freq = freq_sum;
	dev_dbg(&dev->plat_dev->dev, "%s: freq = %u\n",
		__func__, venc_dvfs->venc_dvfs_params.target_freq);

	target_bw_factor = mtk_venc_get_bw_factor(dev);
	venc_dvfs->venc_dvfs_params.target_bw_factor = target_bw_factor;
	dev_dbg(&dev->plat_dev->dev, "%s: bw_factor = %u\n",
		__func__, venc_dvfs->venc_dvfs_params.target_bw_factor);
}
