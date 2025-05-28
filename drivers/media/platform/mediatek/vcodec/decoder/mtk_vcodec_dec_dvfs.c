// SPDX-License-Identifier: GPL-2.0

#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/timekeeping.h>
#include <linux/types.h>

#include <soc/mediatek/mtk-interconnect.h>
#include "mtk_vcodec_dec_dvfs.h"
#include "mtk_vcodec_dec_drv.h"
#include "mtk_vcodec_dec_hw.h"

static struct vcodec_perf *vcodec_dvfs_find_perf(struct vcodec_inst *inst,
						 struct mtk_vcodec_dec_dvfs *vdec_dvfs)
{
	struct device *dev = &vdec_dvfs->sub_dev->plat_dev->dev;
	int i;

	for (i = 0 ; i < vdec_dvfs->vdec_tput_cnt; i++) {
		if (inst->codec_fmt == vdec_dvfs->vdec_tput[i].codec_fmt)
			return &vdec_dvfs->vdec_tput[i];
	}

	mtk_dvfs_err(dev, "%u found no throughput", inst->codec_fmt);

	return NULL;
}

static u64 vcodec_dvfs_match_avail_freq(struct mtk_vcodec_dec_dvfs *vdec_dvfs, u64 freq)
{
	u64 freq_candidate = vdec_dvfs->vdec_freqs[0];
	int i;

	for (i = 0; i < vdec_dvfs->vdec_freq_cnt; i++) {
		if (vdec_dvfs->vdec_freqs[i] == 0)
			break;

		if (vdec_dvfs->vdec_freqs[i] >= freq)
			return vdec_dvfs->vdec_freqs[i];
		else
			freq_candidate = vdec_dvfs->vdec_freqs[i];

	}

	return freq_candidate;
}

static u32 vcodec_dvfs_calc_freq(struct vcodec_inst *inst, struct mtk_vcodec_dec_dvfs *vdec_dvfs)
{
	struct device *dev = &vdec_dvfs->sub_dev->plat_dev->dev;
	struct vcodec_perf *perf;
	u32 freq;

	perf = vcodec_dvfs_find_perf(inst, vdec_dvfs);
	if (perf)
		freq = ((inst->width * inst->height) / 256) * perf->cy_per_mb_1 * 30 * 2;
	else
		freq = 100000000;

	mtk_dvfs_debug(dev, 6, "w:%u x h:%u / 256 x 30 x mb(186) =  freq(%u)",
		       inst->width, inst->height, freq);
	return freq;
}

static struct vcodec_inst *vcodec_dvfs_get_inst(struct mtk_vcodec_dec_ctx *ctx,
						struct mtk_vcodec_dec_dvfs *vdec_dvfs)
{
	struct device *dev = &vdec_dvfs->sub_dev->plat_dev->dev;
	struct vcodec_inst *inst;

	if (ctx->type != MTK_INST_DECODER) {
		mtk_dvfs_err(dev, "not supported codec type: %d", ctx->type);
		return NULL;
	}

	if (list_empty(&vdec_dvfs->vdec_dvfs_inst)) {
		mtk_dvfs_debug(dev, 2, "inst not exist in dvfs list!\n");
		return NULL;
	}

	list_for_each_entry(inst, &vdec_dvfs->vdec_dvfs_inst, list) {
		if (inst->id == ctx->id)
			return inst;
	}

	return NULL;
}

static int vcodec_dvfs_add_inst(struct mtk_vcodec_dec_ctx *ctx,
				struct mtk_vcodec_dec_dvfs *vdec_dvfs)
{
	struct vcodec_inst *new_inst;
	struct device *dev = &vdec_dvfs->sub_dev->plat_dev->dev;

	new_inst = vzalloc(sizeof(*new_inst));
	if (!new_inst)
		return -ENOMEM;

	new_inst->id = ctx->id;
	new_inst->codec_type = ctx->type;

	new_inst->codec_fmt = ctx->current_codec;
	new_inst->capture_fourcc = ctx->capture_fourcc;

	new_inst->width = ctx->picinfo.pic_w;
	new_inst->height = ctx->picinfo.pic_h;

	new_inst->last_access = ktime_get_boottime_ns();
	new_inst->ctx = ctx;
	new_inst->freq_value = vcodec_dvfs_calc_freq(new_inst, vdec_dvfs);

	mtk_dvfs_debug(dev, 4, "new inst id %d, type %u, fmt: 0x%x, width %u, height %u",
		       new_inst->id, new_inst->codec_type, new_inst->codec_fmt,
		       new_inst->width, new_inst->height);

	list_add_tail(&new_inst->list, &vdec_dvfs->vdec_dvfs_inst);

	return 0;
}

static bool vcodec_dvfs_picinfo_changed(struct vcodec_inst *inst, struct mtk_vcodec_dec_ctx *ctx,
					struct mtk_vcodec_dec_dvfs *vdec_dvfs)
{
	inst->last_access = ktime_get_boottime_ns();

	if (inst->width == ctx->picinfo.pic_w && inst->height == ctx->picinfo.pic_h)
		return false;

	inst->width = ctx->picinfo.pic_w;
	inst->height = ctx->picinfo.pic_h;
	inst->freq_value = vcodec_dvfs_calc_freq(inst, vdec_dvfs);

	return true;
}

static inline u32 vcodec_dvfs_get_max_freq(struct mtk_vcodec_dec_dvfs *vdec_dvfs)
{
	u32 max_freq = 546000000;

	switch (vdec_dvfs->main_dev->chip_name) {
		case MTK_VDEC_MT8196:
			max_freq = 546000000;
			break;
		default:
			break;
	}

	return max_freq;
}

static inline u32 vcodec_dvfs_get_min_freq(struct mtk_vcodec_dec_dvfs *vdec_dvfs)
{
	u32 min_freq = 218000000;

	switch (vdec_dvfs->main_dev->chip_name) {
		case MTK_VDEC_MT8196:
			min_freq =  218000000;
			break;
		default:
			break;
	}

	return min_freq;
}

int mtk_vcodec_dvfs_tbl_init(struct mtk_vcodec_dec_dvfs *vdec_dvfs)
{
	struct platform_device *pdev = vdec_dvfs->sub_dev->plat_dev;
	const char *path_strs[32];
	const int inter_item = 4;
	int i, ret, larb_num = 0;

	vdec_dvfs->vdec_dvfs_params.per_frame_adjust = 1;
	vdec_dvfs->vdec_dvfs_params.codec_type = MTK_INST_DECODER;
	vdec_dvfs->vdec_dvfs_params.min_freq = vcodec_dvfs_get_min_freq(vdec_dvfs);
	vdec_dvfs->vdec_dvfs_params.normal_max_freq = vcodec_dvfs_get_max_freq(vdec_dvfs);
	vdec_dvfs->vdec_dvfs_params.allow_oc = 0;

	for (i = 0; i < MTK_VDEC_DVFS_HW_NUM; i++)
		vdec_dvfs->vdec_dvfs_params.lock_cnt[i] = 0;

	vdec_dvfs->vdec_tput_cnt = VDEC_TPUT_CNT;
	vdec_dvfs->vdec_tput = vzalloc(sizeof(*vdec_dvfs->vdec_tput) * vdec_dvfs->vdec_tput_cnt);
	if (!vdec_dvfs->vdec_tput) {
		ret = -ENOMEM;
		goto err_vdec_tput;
	}

	for (i = 0; i < VDEC_LARB_NUM; i++)
		vdec_dvfs->vdec_qos_req[i] = NULL;

	larb_num = of_property_count_u32_elems(pdev->dev.of_node, "interconnects") / inter_item;
	if (larb_num < 0 || !larb_num || larb_num > VDEC_LARB_NUM) {
		mtk_dvfs_debug(&pdev->dev, 0, "larb num size is wrong:%d.", larb_num);
		ret = -EINVAL;
		goto err_value;
	}

	ret = of_property_read_string_array(pdev->dev.of_node, "interconnect-names",
					    path_strs, larb_num);
	if (ret < 0) {
		mtk_dvfs_debug(&pdev->dev, 0, "cannot get interconnect names, skip");
		goto err_value;
	} else if (ret != (int)larb_num) {
		mtk_dvfs_debug(&pdev->dev, 0, "interconnect name count not match %u %d",
			       larb_num, ret);
		goto err_value;
	}

	for (i = 0; i < larb_num; i++) {
		vdec_dvfs->vdec_qos_req[i] = of_mtk_icc_get(&pdev->dev, path_strs[i]);
		mtk_dvfs_debug(&pdev->dev, 4, "qos larb[%d] name %s", i, path_strs[i]);
	}

	/* bw */
	ret = of_property_read_u32(pdev->dev.of_node, "bandwidth-larb-cnt",
				   &vdec_dvfs->vdec_larb_cnt);
	if (ret) {
		mtk_dvfs_debug(&pdev->dev, 0, "cannot get bandwidth-larb-cnt");
		goto err_value;
	}

	if (vdec_dvfs->vdec_larb_cnt > VDEC_LARB_NUM) {
		mtk_dvfs_debug(&pdev->dev, 0, "port over limit %d > %d",
			       vdec_dvfs->vdec_larb_cnt, VDEC_LARB_NUM);
		vdec_dvfs->vdec_larb_cnt = VDEC_LARB_NUM;
	}

	if (!vdec_dvfs->vdec_larb_cnt) {
		mtk_dvfs_debug(&pdev->dev, 0, "bandwidth table not exist");
		ret = -EINVAL;
		goto err_value;
	}

	vdec_dvfs->vdec_larb_bw = vzalloc(sizeof(*vdec_dvfs->vdec_larb_bw) *
					  vdec_dvfs->vdec_larb_cnt);
	if (!vdec_dvfs->vdec_larb_bw) {
		ret = -ENOMEM;
		goto err_value;
	}

	mtk_dvfs_debug(&pdev->dev, 0, "tput_cnt %d, larb_cnt %d",
		       vdec_dvfs->vdec_tput_cnt, vdec_dvfs->vdec_larb_cnt);

	vdec_dvfs->vdec_pc_bw = vzalloc(VDEC_LARB_HW_NUM * sizeof(struct vcodec_pc_bw));
	if (!vdec_dvfs->vdec_pc_bw) {
		ret = -ENOMEM;
		goto err_pc_bw;
	}

	vdec_dvfs->dvfs_params_updated = false;

	return ret;
err_pc_bw:
	vfree(vdec_dvfs->vdec_larb_bw);
err_value:
	vfree(vdec_dvfs->vdec_tput);
err_vdec_tput:

	return ret;
}

void mtk_vcodec_dvfs_tbl_deinit(struct mtk_vcodec_dec_dvfs *vdec_dvfs)
{
	if (vdec_dvfs->vdec_tput) {
		vfree(vdec_dvfs->vdec_tput);
		vdec_dvfs->vdec_tput = NULL;
	}

	if (vdec_dvfs->vdec_larb_bw) {
		vfree(vdec_dvfs->vdec_larb_bw);
		vdec_dvfs->vdec_larb_bw = NULL;
	}

	if (vdec_dvfs->vdec_pc_bw) {
		vfree(vdec_dvfs->vdec_pc_bw);
		vdec_dvfs->vdec_pc_bw = NULL;
	}
}

bool mtk_vcodec_dvfs_update_picinfo(struct mtk_vcodec_dec_ctx *ctx,
				    struct mtk_vcodec_dec_dvfs *vdec_dvfs)
{
	struct vcodec_inst *inst = 0;

	if (!vdec_dvfs)
		return false;

	inst = vcodec_dvfs_get_inst(ctx, vdec_dvfs);
	if (!inst) {
		vcodec_dvfs_add_inst(ctx, vdec_dvfs);
		return true;
	}

	if (vcodec_dvfs_picinfo_changed(inst, ctx, vdec_dvfs))
		return true;

	return false;
}

bool mtk_vcodec_dvfs_remove_picinfo(struct mtk_vcodec_dec_ctx *ctx,
				    struct mtk_vcodec_dec_dvfs *vdec_dvfs)
{
	struct vcodec_inst *inst = 0;

	if (!ctx)
		return false;

	inst = vcodec_dvfs_get_inst(ctx, vdec_dvfs);
	if (!inst)
		return false;

	list_del(&inst->list);
	vfree(inst);

	return true;
}

void mtk_vcodec_dvfs_update_freq(struct mtk_vcodec_dec_dvfs *vdec_dvfs, bool is_removed)
{
	struct device *dev = &vdec_dvfs->sub_dev->plat_dev->dev;
	struct dvfs_params *dvfs_params = &vdec_dvfs->vdec_dvfs_params;
	struct vcodec_inst *inst;
	u32 inst_num = 0;
	u64 freq_sum = 0;

	dvfs_params->allow_oc = 0;
	if (list_empty(&vdec_dvfs->vdec_dvfs_inst)) {
		if (!is_removed)
			freq_sum = vcodec_dvfs_match_avail_freq(vdec_dvfs, 0);
		dvfs_params->freq_sum = freq_sum;
		dvfs_params->target_freq = freq_sum;
		return;
	}

	list_for_each_entry(inst, &vdec_dvfs->vdec_dvfs_inst, list) {
		inst_num++;
		if (freq_sum + inst->freq_value > dvfs_params->normal_max_freq) {
			freq_sum = dvfs_params->normal_max_freq;
			break;
		} else {
			freq_sum += inst->freq_value;
		}
	}

	dvfs_params->freq_sum = freq_sum;
	dvfs_params->per_frame_adjust = 1;
	dvfs_params->target_freq = vcodec_dvfs_match_avail_freq(vdec_dvfs, freq_sum);

	mtk_dvfs_debug(dev, 1, "inst_count:%d work: %u sum: %llu target: %u", inst_num,
		       dvfs_params->work_freq, freq_sum, dvfs_params->target_freq);
}
