// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024 MediaTek Inc.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/of.h>
#include <linux/pm_opp.h>
#include <linux/regulator/consumer.h>
#include <linux/regmap.h>
#include <soc/mediatek/mmdvfs_v3.h>
#include <soc/mediatek/mtk-interconnect.h>

#include "mtk_vcodec_enc_drv.h"
#include "mtk_vcodec_enc_dvfs.h"

#define BW_FACTOR_DENOMINATOR		1000000
#define VENC_BW_MON_CTRL		0x2c
#define VENC_BW_SET_DONE		0x74
#define VENC_DELAY_US			5
#define VENC_TIMEOUT_US			100

static void mtk_venc_set_opp(struct mtk_vcodec_enc_dev *dev, unsigned long freq)
{
	int ret = 0;
	struct dev_pm_opp *opp;
	int volt = 0;
	unsigned long freq_64 = freq;
	struct mtk_vcodec_enc_dvfs *venc_dvfs = &dev->venc_dvfs;

	opp = dev_pm_opp_find_freq_ceil(&dev->plat_dev->dev, &freq_64);
	volt = dev_pm_opp_get_voltage(opp);
	dev_pm_opp_put(opp);

	if (venc_dvfs->venc_mmdvfs_clk) {
		mtk_mmdvfs_enable_vcp(true, VCP_PWR_USR_VENC);
		ret = clk_set_rate(venc_dvfs->venc_mmdvfs_clk, freq_64);
		if (ret)
			dev_err(&dev->plat_dev->dev, "%s: failed to set rate %lu\n",
				__func__, freq_64);
		mtk_mmdvfs_enable_vcp(false, VCP_PWR_USR_VENC);
		dev_dbg(&dev->plat_dev->dev, "%s: freq %lu, find_freq %lu\n",
			__func__, freq, freq_64);
	} else if (venc_dvfs->venc_reg) {
		ret = regulator_set_voltage(venc_dvfs->venc_reg, volt, INT_MAX);
		if (ret)
			dev_err(&dev->plat_dev->dev, "failed to set voltage %d\n", volt);
		dev_dbg(&dev->plat_dev->dev, "%s: freq %lu, find_freq %lu\n",
			__func__, freq, freq_64);
	}
}

void mtk_venc_prepare_dvfs(struct mtk_vcodec_enc_dev *dev)
{
	int ret;
	unsigned long freq = 0;
	int i = 0;
	struct dev_pm_opp *opp;
	struct mtk_vcodec_enc_dvfs *venc_dvfs = &dev->venc_dvfs;

	INIT_LIST_HEAD(&venc_dvfs->venc_dvfs_inst_list);

	ret = dev_pm_opp_of_add_table(&dev->plat_dev->dev);
	if (ret < 0) {
		venc_dvfs->venc_reg = NULL;
		dev_err(&dev->plat_dev->dev, "%s: no venc opp table found\n", __func__);
		return;
	}

	venc_dvfs->venc_reg = devm_regulator_get_optional(&dev->plat_dev->dev,
							  "mmdvfs-dvfsrc-vcore");
	if (IS_ERR_OR_NULL(venc_dvfs->venc_reg)) {
		dev_info(&dev->plat_dev->dev, "%s: no venc regulator found\n", __func__);
		venc_dvfs->venc_reg = NULL;
		venc_dvfs->venc_mmdvfs_clk = devm_clk_get(&dev->plat_dev->dev, "mmdvfs_clk");
		if (IS_ERR_OR_NULL(venc_dvfs->venc_mmdvfs_clk)) {
			dev_err(&dev->plat_dev->dev, "%s: no venc mmdvfs_clk found\n", __func__);
			venc_dvfs->venc_mmdvfs_clk = NULL;
			return;
		}
		dev_info(&dev->plat_dev->dev, "%s: get venc mmdvfs_clk successfully\n", __func__);
	} else {
		dev_info(&dev->plat_dev->dev, "%s: get venc regulator successfully\n", __func__);
	}

	ret = dev_pm_opp_get_opp_count(&dev->plat_dev->dev);
	if (ret <= 0) {
		dev_err(&dev->plat_dev->dev, "%s: failed add venc opp\n", __func__);
		return;
	}
	venc_dvfs->venc_freq_cnt = min(ret, MAX_CODEC_FREQ_STEP);
	while (i < venc_dvfs->venc_freq_cnt) {
		opp = dev_pm_opp_find_freq_ceil(&dev->plat_dev->dev, &freq);
		if (IS_ERR(opp))
			break;
		venc_dvfs->venc_freqs[i] = freq;
		dev_info(&dev->plat_dev->dev, "%s: get venc_freqs: %lu MHz\n",
			 __func__, venc_dvfs->venc_freqs[i]);
		freq++;
		i++;
		dev_pm_opp_put(opp);
	}

	mtk_venc_dvfs_tbl_init(dev);
}

void mtk_venc_unprepare_dvfs(struct mtk_vcodec_enc_dev *dev)
{
	struct venc_dvfs_inst *inst, *tmp;
	struct mtk_vcodec_enc_dvfs *venc_dvfs = &dev->venc_dvfs;

	list_for_each_entry_safe(inst, tmp, &venc_dvfs->venc_dvfs_inst_list, list) {
		list_del(&inst->list);
		vfree(inst);
	}

	mtk_venc_dvfs_tbl_deinit(dev);
}

void mtk_venc_prepare_emi_bw(struct mtk_vcodec_enc_dev *dev)
{
	int i, ret, tmp_qos_cnt;
	const struct mtk_vcodec_enc_pdata *pdata = dev->venc_pdata;
	struct mtk_vcodec_enc_dvfs *venc_dvfs = &dev->venc_dvfs;
	const char *path_strs[MTK_VENC_PORT_NUM];

	if (!pdata->dvfs_cfg.larb_num)
		return;

	tmp_qos_cnt = pdata->dvfs_cfg.larb_num * 2;

	ret = of_property_read_string_array(dev->plat_dev->dev.of_node,
					    "interconnect-names",
					    path_strs, tmp_qos_cnt);
	if (ret < 0) {
		dev_info(&dev->plat_dev->dev, "%s: cannot get interconnect, skip\n", __func__);
		return;
	} else if (ret != tmp_qos_cnt) {
		dev_err(&dev->plat_dev->dev, "%s: interconnect count not match %d %d\n",
			__func__, tmp_qos_cnt, ret);
		return;
	}

	for (i = 0; i < pdata->dvfs_cfg.larb_num; i++) {
		venc_dvfs->venc_larb_bw[i].larb_qos_req[0] =
			of_mtk_icc_get(&dev->plat_dev->dev, path_strs[i * 2]);
		venc_dvfs->venc_larb_bw[i].larb_qos_req[1] =
			of_mtk_icc_get(&dev->plat_dev->dev, path_strs[i * 2 + 1]);
	}
}

void mtk_venc_dvfs_begin_inst(struct mtk_vcodec_enc_ctx *ctx)
{
	struct mtk_vcodec_enc_dev *dev = ctx->dev;
	struct mtk_vcodec_enc_dvfs *venc_dvfs = &dev->venc_dvfs;

	if (!venc_dvfs->venc_reg && !venc_dvfs->venc_mmdvfs_clk)
		return;

	if (mtk_venc_need_update(ctx)) {
		mtk_venc_update_freq(dev);
		dev_dbg(&dev->plat_dev->dev, "%s: freq %u\n",
			__func__, venc_dvfs->venc_dvfs_params.target_freq);
		mtk_venc_set_opp(dev, venc_dvfs->venc_dvfs_params.target_freq);
	}
}

void mtk_venc_dvfs_end_inst(struct mtk_vcodec_enc_ctx *ctx)
{
	struct mtk_vcodec_enc_dev *dev = ctx->dev;
	struct mtk_vcodec_enc_dvfs *venc_dvfs = &dev->venc_dvfs;

	if (!venc_dvfs->venc_reg && !venc_dvfs->venc_mmdvfs_clk)
		return;

	if (mtk_venc_remove_update(ctx)) {
		mtk_venc_update_freq(dev);
		dev_dbg(&dev->plat_dev->dev, "%s: freq %u\n",
			__func__, venc_dvfs->venc_dvfs_params.target_freq);
		mtk_venc_set_opp(dev, venc_dvfs->venc_dvfs_params.target_freq);
	}
}

static void mtk_venc_pmqos_set_target_bw(struct mtk_vcodec_enc_dvfs *venc_dvfs,
					 unsigned int i, bool is_end_inst)
{
	unsigned int target_bw_r = 0, target_bw_w = 0;
	unsigned int bw_factor = venc_dvfs->venc_dvfs_params.target_bw_factor;
	unsigned int larb_bw_r = venc_dvfs->venc_larb_bw[i].larb_base_bw_r;
	unsigned int larb_bw_w = venc_dvfs->venc_larb_bw[i].larb_base_bw_w;

	target_bw_r = larb_bw_r * bw_factor / BW_FACTOR_DENOMINATOR;
	target_bw_w = larb_bw_w * bw_factor / BW_FACTOR_DENOMINATOR;

	/* check no more instances */
	if (is_end_inst && list_empty(&venc_dvfs->venc_dvfs_inst_list)) {
		target_bw_r = 0;
		target_bw_w = 0;
	}

	venc_dvfs->venc_larb_bw[i].target_bw_r = target_bw_r;
	venc_dvfs->venc_larb_bw[i].target_bw_w = target_bw_w;

	mtk_icc_set_bw(venc_dvfs->venc_larb_bw[i].larb_qos_req[0],
		       MBps_to_icc(target_bw_r), 0);
	mtk_icc_set_bw(venc_dvfs->venc_larb_bw[i].larb_qos_req[1],
		       MBps_to_icc(target_bw_w), 0);
}

void mtk_venc_pmqos_begin_inst(struct mtk_vcodec_enc_ctx *ctx)
{
	int i;
	const struct mtk_vcodec_enc_pdata *pdata = ctx->dev->venc_pdata;
	struct mtk_vcodec_enc_dvfs *venc_dvfs = &ctx->dev->venc_dvfs;
	unsigned int r_bw = 0, w_bw = 0, total_r = 0, total_w = 0;

	if (!venc_dvfs->venc_reg && !venc_dvfs->venc_mmdvfs_clk)
		return;

	for (i = 0; i < pdata->dvfs_cfg.larb_num; i++) {
		mtk_venc_pmqos_set_target_bw(venc_dvfs, i, false);
		r_bw = venc_dvfs->venc_larb_bw[i].target_bw_r;
		w_bw = venc_dvfs->venc_larb_bw[i].target_bw_w;
		venc_dvfs->venc_pc_bw[i].target_bw_r = min((r_bw * 10 / 7) >> 4, 1023);
		venc_dvfs->venc_pc_bw[i].target_bw_w = min((w_bw * 10 / 7) >> 4, 1023);
		total_r += r_bw;
		total_w += w_bw;
	}
	venc_dvfs->venc_pc_bw[i].target_bw_r = min((total_r * 137 / 100) >> 6, 1023);
	venc_dvfs->venc_pc_bw[i].target_bw_w = min((total_w * 137 / 100) >> 6, 1023);
}

void mtk_venc_pmqos_end_inst(struct mtk_vcodec_enc_ctx *ctx)
{
	int i;
	const struct mtk_vcodec_enc_pdata *pdata = ctx->dev->venc_pdata;
	struct mtk_vcodec_enc_dvfs *venc_dvfs = &ctx->dev->venc_dvfs;

	if (!venc_dvfs->venc_reg && !venc_dvfs->venc_mmdvfs_clk)
		return;

	for (i = 0; i < pdata->dvfs_cfg.larb_num; i++)
		mtk_venc_pmqos_set_target_bw(venc_dvfs, i, true);
}

void mtk_venc_pmqos_begin_frame(struct mtk_vcodec_enc_ctx *ctx)
{
	int i, ret;
	struct mtk_vcodec_enc_dev *dev = ctx->dev;
	const struct mtk_vcodec_enc_pdata *pdata = ctx->dev->venc_pdata;
	struct mtk_vcodec_enc_dvfs *venc_dvfs = &dev->venc_dvfs;
	unsigned int mmpc_state, mmpc_ctrl, mmpc_init;

	if (!dev->vencsys || (!venc_dvfs->venc_reg && !venc_dvfs->venc_mmdvfs_clk))
		return;

	for (i = 0; i < pdata->dvfs_cfg.larb_num; i++) {
		dev_dbg(&dev->plat_dev->dev, "%s: pc target bw: w: %d, r: %d\n",
			__func__,
			venc_dvfs->venc_pc_bw[i].target_bw_w,
			venc_dvfs->venc_pc_bw[i].target_bw_r);
		regmap_write(dev->vencsys, venc_dvfs->venc_pc_bw[i].reg_offset,
			     (venc_dvfs->venc_pc_bw[i].target_bw_w << 16) +
			     venc_dvfs->venc_pc_bw[i].target_bw_r);
	}
	dev_dbg(&dev->plat_dev->dev, "%s: pc total bw: w: %d, r: %d\n",
		__func__,
		venc_dvfs->venc_pc_bw[i].target_bw_w,
		venc_dvfs->venc_pc_bw[i].target_bw_r);
	regmap_write(dev->vencsys, venc_dvfs->venc_pc_bw[i].reg_offset,
		     venc_dvfs->venc_pc_bw[i].target_bw_w + venc_dvfs->venc_pc_bw[i].target_bw_r);
	regmap_write(dev->vencsys, VENC_BW_MON_CTRL, 0x1);
	regmap_write(dev->vencsys, VENC_BW_MON_CTRL, 0xB);

	ret = regmap_read_poll_timeout_atomic(dev->vencsys, VENC_BW_SET_DONE,
					      mmpc_state, (mmpc_state & 0xf) > 1,
					      VENC_DELAY_US, VENC_TIMEOUT_US);
	if (ret) {
		regmap_read(dev->vencsys, VENC_BW_MON_CTRL, &mmpc_ctrl);
		regmap_read(dev->vencsys, venc_dvfs->venc_pc_bw[i].reg_offset, &mmpc_init);
		dev_err(&dev->plat_dev->dev, "%s: polling MMPC timeout\n", __func__);
		dev_err(&dev->plat_dev->dev, "%s: state: %d, ctrl: %d, init: %x\n",
			__func__, mmpc_state & 0xf, mmpc_ctrl, mmpc_init);
	}
}
