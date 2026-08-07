// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024 MediaTek Inc.
 */

#include <drm/display/drm_dsc_helper.h>

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/ratelimit.h>
#include <linux/delay.h>

#include "mtk_drm_drv.h"
#include "mtk_ddp_comp.h"

#define DISP_REG_DSC_CON		0x0000
#define DSC_EN					BIT(0)
#define DSC_DUAL_INOUT				BIT(2)
#define DSC_IN_SRC_SEL				BIT(3)
#define DSC_BYPASS				BIT(4)
#define DSC_RELAY				BIT(5)
#define DSC_EMPTY_FLAG_SEL			GENMASK(15, 14)
#define DSC_EMPTY_FLAG_ALWAYS_LOW		BIT(15)
#define DSC_UFOE_SEL				BIT(16)
#define DSC_INPUT_SWITCH_SWAP			BIT(17)
#define DSC_PT_MEM_EN				BIT(7)
#define DSC_OUTPUT_SWA				BIT(18)
#define DSC_ZERO_FIFO_STALL_DISABLE		BIT(20)
#define DISP_REG_DSC_INTEN		0x0004
#define DSC_INTEN_SEL				GENMASK(6, 0)
#define DISP_REG_DSC_INTSTA		0x0008
#define DSC_DONE				BIT(0)
#define DSC_ERR					BIT(1)
#define DSC_ZERO_FIFO				BIT(2)
#define DSC_ABN_EOF				BIT(3)
#define DISP_REG_DSC_INTACK		0x000c
#define DSC_INTACK_SEL				GENMASK(6, 0)
#define DSC_INTACK_BUF_UNDERFLOW		BIT(6)
#define DISP_REG_DSC_SPR		0x0014
#define CFG_FLD_DSC_SPR_EN			BIT(26)
#define CFG_FLD_DSC_SPR_FORMAT_SEL		BIT(24)
#define DISP_REG_DSC_PIC_W		0x0018
#define CFG_FLD_PIC_WIDTH			GENMASK(15, 0)
#define CFG_FLD_PIC_HEIGHT_M1			GENMASK(31, 16)
#define DISP_REG_DSC_PIC_H		0x001c
#define DISP_REG_DSC_SLICE_W		0x0020
#define CFG_FLD_SLICE_WIDTH			GENMASK(15, 0)
#define DISP_REG_DSC_SLICE_H		0x0024
#define DISP_REG_DSC_CHUNK_SIZE		0x0028
#define DISP_REG_DSC_BUF_SIZE		0x002c
#define DISP_REG_DSC_MODE		0x0030
#define DSC_SLICE_MODE				BIT(0)
#define DSC_RGB_SWAP				BIT(2)
#define DISP_REG_DSC_CFG		0x0034
#define DSC_CONFIG_8BIT_SETTING			(0x22)
#define DSC_CONFIG_10BIT_SETTING		(0x828)
#define DISP_REG_DSC_PAD		0x0038
#define DISP_REG_DSC_ENC_WIDTH		0x003c
#define DISP_REG_DSC_PIC_PRE_PAD_SIZE	0x0040
#define DSC_PIC_PREPAD_HEIGHT_SEL		GENMASK(15, 0)
#define DSC_PIC_PREPAD_WIDTH_SEL		GENMASK(31, 16)
#define DISP_REG_DSC_DBG_CON		0x0060
#define DSC_CKSM_CAL_EN				BIT(9)
#define DISP_REG_DSC_OBUF		0x0070
#define DISP_REG_DSC_PPS0		0x0080
#define DISP_REG_DSC_PPS1		0x0084
#define DISP_REG_DSC_PPS2		0x0088
#define DISP_REG_DSC_PPS3		0x008c
#define DISP_REG_DSC_PPS4		0x0090
#define DISP_REG_DSC_PPS5		0x0094
#define DISP_REG_DSC_PPS6		0x0098
#define DISP_REG_DSC_PPS7		0x009c
#define DISP_REG_DSC_PPS8		0x00a0
#define DISP_REG_DSC_PPS9		0x00a4
#define DISP_REG_DSC_PPS10		0x00a8
#define DISP_REG_DSC_PPS11		0x00ac
#define DISP_REG_DSC_PPS12		0x00b0
#define DISP_REG_DSC_PPS13		0x00b4
#define DISP_REG_DSC_PPS14		0x00b8
#define DISP_REG_DSC_PPS15		0x00bc
#define DISP_REG_DSC_PPS16		0x00c0
#define DISP_REG_DSC_PPS17		0x00c4
#define DISP_REG_DSC_PPS18		0x00c8
#define DISP_REG_DSC_PPS19		0x00cc

#define DISP_REG_DSC_SHADOW		0x0200
#define DISP_DSC_VERSION_MINOR			(0x000001e0)
#define DSC_FORCE_COMMIT			BIT(0)
#define DSC_BYPASS_SHADOW			BIT(1)
#define DSC_READ_WORKING			BIT(2)
#define DISP_REG_DSC1_OFFSET		0x0400

struct mtk_dsc_data {
	bool dsc_bypass_enable;
	bool pt_mem_en;
	bool supports_10bit;
	u32 obuf;
};

struct mtk_dsc {
	struct device *dev;
	void __iomem *regs;
	struct clk *clk;
	struct mtk_ddp_comp ddp_comp;
	const struct mtk_dsc_data *data;
	struct dsc_info dsc_info;
};

static void mtk_dsc_write(struct mtk_dsc *dsc, u32 offset, u32 data)
{
	writel(data, dsc->regs + offset);
}

static void mtk_dsc_write_mask(struct mtk_dsc *dsc, u32 offset, u32 data, u32 mask)
{
	u32 temp = readl(dsc->regs + offset);

	writel((temp & ~mask) | (data & mask), dsc->regs + offset);
}

static u32 mtk_dsc_read(struct mtk_dsc *dsc, u32 offset)
{
	u32 val = readl(dsc->regs + offset);

	return val;
}

void mtk_dsc_start(struct device *dev)
{
	struct mtk_dsc *dsc = dev_get_drvdata(dev);

	mtk_dsc_write_mask(dsc, DISP_REG_DSC_CON, DSC_EN, DSC_EN);

	dev_dbg(dsc->dev, "DSC_CON:0x%x", mtk_dsc_read(dsc, DISP_REG_DSC_CON));
}

void mtk_dsc_stop(struct device *dev)
{
	struct mtk_dsc *dsc = dev_get_drvdata(dev);

	mtk_dsc_write_mask(dsc, DISP_REG_DSC_CON, 0x0, DSC_EN);
}

void mtk_dsc_set_dsc_info(struct device *dev, const struct dsc_info *dsc_info)
{
	struct mtk_dsc *mtk_dsc = dev_get_drvdata(dev);

	if (!dsc_info) {
		dev_err(dev, "dsc_info is NULL\n");
		return;
	}

	dev_dbg(dev, "%s: compression_enable=%d slice_count=%u\n",
		__func__, dsc_info->compression_enable, dsc_info->dsc_config.slice_count);

	mtk_dsc->dsc_info = *dsc_info;
}

void mtk_dsc_config(struct device *dev, unsigned int w, unsigned int h,
		unsigned int vrefresh, unsigned int bpc, struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_dsc *dsc = dev_get_drvdata(dev);
	struct dsc_info *dsc_info = &dsc->dsc_info;
	const struct drm_dsc_config *cfg = &dsc_info->dsc_config;
	unsigned int init_delay_limit, init_delay_height_min, init_delay_height;
	unsigned int pic_group_width, pic_height_ext_num;
	unsigned int slice_group_width;
	unsigned int dsc_cfg = DSC_CONFIG_8BIT_SETTING;
	unsigned int pad_num;
	unsigned int bp_enable;
	unsigned int slice_mode;
	unsigned int rgb_swap = 0;
	unsigned int con = 0;

	if (!dsc->data) {
		dev_err(dev, "%s: driver data is not set\n", __func__);
		return;
	}

	dev_dbg(dsc->dev, "w:%d, h:%d, dsc_bypass_en:%d, dsc_compression_en:%d\n",
		w, h, dsc->data->dsc_bypass_enable, dsc_info->compression_enable);

	if (dsc->data->dsc_bypass_enable) {
		mtk_dsc_write_mask(dsc, DISP_REG_DSC_CON,
				   DSC_BYPASS | DSC_UFOE_SEL | DSC_DUAL_INOUT,
				   DSC_BYPASS | DSC_UFOE_SEL | DSC_DUAL_INOUT);
		return;
	}

	if (!dsc_info->compression_enable) {
		mtk_dsc_write_mask(dsc, DISP_REG_DSC_CON, DSC_RELAY, DSC_RELAY);
		mtk_dsc_write_mask(dsc, DISP_REG_DSC_CHUNK_SIZE, w << 16, GENMASK(31, 16));
		mtk_dsc_write_mask(dsc, DISP_REG_DSC_PIC_W, w, GENMASK(15, 0));
		mtk_dsc_write_mask(dsc, DISP_REG_DSC_PIC_H, h, GENMASK(15, 0));
		return;
	}

	bp_enable = cfg->block_pred_enable ? 1 : 0;
	slice_mode = cfg->pic_width / cfg->slice_width - 1;
	pic_group_width = (cfg->pic_width + 2) / 3;
	pic_height_ext_num = (h + cfg->slice_height - 1) / cfg->slice_height;
	slice_group_width = (cfg->slice_width + 2) / 3;
	pad_num = (cfg->slice_chunk_size * (slice_mode + 1) + 2) / 3 * 3
		- cfg->slice_chunk_size * (slice_mode + 1);
	init_delay_limit = ((128 + (cfg->initial_xmit_delay + 2) / 3) * 3
		+ cfg->slice_width - 1) / cfg->slice_width;
	init_delay_height_min = (init_delay_limit > 15) ? 15 : init_delay_limit;
	init_delay_height = init_delay_height_min;

	if (dsc->data->pt_mem_en) {
		con |= DSC_PT_MEM_EN | DSC_EMPTY_FLAG_ALWAYS_LOW |
		       DSC_UFOE_SEL | DSC_ZERO_FIFO_STALL_DISABLE;

		mtk_dsc_write_mask(dsc, DISP_REG_DSC_INTEN, 0x7F, DSC_INTEN_SEL);
		mtk_dsc_write_mask(dsc, DISP_REG_DSC_INTACK,
				   DSC_INTACK_BUF_UNDERFLOW, DSC_INTACK_SEL);
		mtk_dsc_write(dsc, DISP_REG_DSC_SPR, 0x0);
	}
	mtk_dsc_write(dsc, DISP_REG_DSC_CON, con);

	mtk_dsc_write(dsc, DISP_REG_DSC_PIC_W,
		      w | ((pic_group_width - 1) << 16));

	mtk_dsc_write(dsc, DISP_REG_DSC_PIC_H,
		      (h - 1) | ((pic_height_ext_num * cfg->slice_height - 1) << 16));

	mtk_dsc_write(dsc, DISP_REG_DSC_SLICE_W,
		      cfg->slice_width | ((slice_group_width - 1) << 16));

	mtk_dsc_write(dsc, DISP_REG_DSC_SLICE_H,
		      (cfg->slice_height - 1) |
		      ((pic_height_ext_num - 1) << 16) |
		      ((cfg->slice_width % 3) << 30));

	mtk_dsc_write(dsc, DISP_REG_DSC_CHUNK_SIZE,
		      cfg->slice_chunk_size |
		      ((((cfg->slice_chunk_size << slice_mode) + 2) / 3) << 16));

	mtk_dsc_write_mask(dsc, DISP_REG_DSC_BUF_SIZE,
			   cfg->slice_chunk_size * cfg->slice_height, GENMASK(23, 0));

	mtk_dsc_write(dsc, DISP_REG_DSC_MODE,
		      (slice_mode & BIT(0)) | (rgb_swap << 2) | (init_delay_height << 8));

	if (cfg->bits_per_component == 10 && dsc->data->supports_10bit)
		dsc_cfg = DSC_CONFIG_10BIT_SETTING;
	mtk_dsc_write(dsc, DISP_REG_DSC_CFG, dsc_cfg);

	mtk_dsc_write_mask(dsc, DISP_REG_DSC_PAD, pad_num, GENMASK(2, 0));

	mtk_dsc_write(dsc, DISP_REG_DSC_ENC_WIDTH,
		      cfg->slice_width | (cfg->pic_width << 16));

	mtk_dsc_write(dsc, DISP_REG_DSC_PIC_PRE_PAD_SIZE, h | (w << 16));

	mtk_dsc_write_mask(dsc, DISP_REG_DSC_DBG_CON, DSC_CKSM_CAL_EN, DSC_CKSM_CAL_EN);

	mtk_dsc_write(dsc, DISP_REG_DSC_OBUF, dsc->data->obuf);

	mtk_dsc_write(dsc, DISP_REG_DSC_PPS0,
		      ((cfg->line_buf_depth == 0) ? 0x9 : cfg->line_buf_depth) |
		      ((cfg->bits_per_component == 0) ? 0x8 << 4 : cfg->bits_per_component << 4) |
		      ((cfg->bits_per_pixel == 0) ? 0x80 << 8 : cfg->bits_per_pixel << 8) |
		      ((cfg->convert_rgb == 0) ? BIT(18) : cfg->convert_rgb << 18) |
		      (bp_enable << 19));

	mtk_dsc_write(dsc, DISP_REG_DSC_PPS1,
		      ((cfg->initial_xmit_delay == 0) ? 0x200 : cfg->initial_xmit_delay) |
		      ((cfg->initial_dec_delay == 0) ? 0x268 << 16 : cfg->initial_dec_delay << 16));

	mtk_dsc_write(dsc, DISP_REG_DSC_PPS2,
		      ((cfg->initial_scale_value == 0) ? 0x20 : cfg->initial_scale_value) |
		      ((cfg->scale_increment_interval == 0) ?
		       0x387 << 16 : cfg->scale_increment_interval << 16));

	mtk_dsc_write(dsc, DISP_REG_DSC_PPS3,
		      ((cfg->scale_decrement_interval == 0) ? 0xa : cfg->scale_decrement_interval) |
		      ((cfg->first_line_bpg_offset == 0) ?
		       0xc << 16 : cfg->first_line_bpg_offset << 16));

	mtk_dsc_write(dsc, DISP_REG_DSC_PPS4,
		      ((cfg->nfl_bpg_offset == 0) ? 0x319 : cfg->nfl_bpg_offset) |
		      ((cfg->slice_bpg_offset == 0) ?
		       0x263 << 16 : cfg->slice_bpg_offset << 16));

	mtk_dsc_write(dsc, DISP_REG_DSC_PPS5,
		      ((cfg->initial_offset == 0) ? 0x1800 : cfg->initial_offset) |
		      ((cfg->final_offset == 0) ? 0x10f0 << 16 : cfg->final_offset << 16));

	mtk_dsc_write(dsc, DISP_REG_DSC_PPS6,
		      ((cfg->flatness_min_qp == 0) ? 0x3 : cfg->flatness_min_qp) |
		      ((cfg->flatness_max_qp == 0) ? 0xc << 8 : cfg->flatness_max_qp << 8) |
		      ((cfg->rc_model_size == 0) ? 0x2000 << 16 : cfg->rc_model_size << 16));

	mtk_dsc_write(dsc, DISP_REG_DSC_PPS7,
		      cfg->rc_edge_factor |
		      (cfg->rc_quant_incr_limit0 << 8) |
		      (cfg->rc_quant_incr_limit1 << 16) |
		      (cfg->rc_tgt_offset_high << 24) |
		      (cfg->rc_tgt_offset_low << 28));

	mtk_dsc_write(dsc, DISP_REG_DSC_PPS8,
		      cfg->rc_buf_thresh[0] |
		      (cfg->rc_buf_thresh[1] << 8) |
		      (cfg->rc_buf_thresh[2] << 16) |
		      (cfg->rc_buf_thresh[3] << 24));
	mtk_dsc_write(dsc, DISP_REG_DSC_PPS9,
		      cfg->rc_buf_thresh[4] |
		      (cfg->rc_buf_thresh[5] << 8) |
		      (cfg->rc_buf_thresh[6] << 16) |
		      (cfg->rc_buf_thresh[7] << 24));
	mtk_dsc_write(dsc, DISP_REG_DSC_PPS10,
		      cfg->rc_buf_thresh[8] |
		      (cfg->rc_buf_thresh[9] << 8) |
		      (cfg->rc_buf_thresh[10] << 16) |
		      (cfg->rc_buf_thresh[11] << 24));
	mtk_dsc_write(dsc, DISP_REG_DSC_PPS11,
		      cfg->rc_buf_thresh[12] |
		      (cfg->rc_buf_thresh[13] << 8));
	mtk_dsc_write(dsc, DISP_REG_DSC_PPS12,
		      cfg->rc_range_params[0].range_min_qp |
		      (cfg->rc_range_params[0].range_max_qp << 5) |
		      (cfg->rc_range_params[0].range_bpg_offset << 10) |
		      (cfg->rc_range_params[1].range_min_qp << 16) |
		      (cfg->rc_range_params[1].range_max_qp << 21) |
		      (cfg->rc_range_params[1].range_bpg_offset << 26));
	mtk_dsc_write(dsc, DISP_REG_DSC_PPS13,
		      cfg->rc_range_params[2].range_min_qp |
		      (cfg->rc_range_params[2].range_max_qp << 5) |
		      (cfg->rc_range_params[2].range_bpg_offset << 10) |
		      (cfg->rc_range_params[3].range_min_qp << 16) |
		      (cfg->rc_range_params[3].range_max_qp << 21) |
		      (cfg->rc_range_params[3].range_bpg_offset << 26));
	mtk_dsc_write(dsc, DISP_REG_DSC_PPS14,
		      cfg->rc_range_params[4].range_min_qp |
		      (cfg->rc_range_params[4].range_max_qp << 5) |
		      (cfg->rc_range_params[4].range_bpg_offset << 10) |
		      (cfg->rc_range_params[5].range_min_qp << 16) |
		      (cfg->rc_range_params[5].range_max_qp << 21) |
		      (cfg->rc_range_params[5].range_bpg_offset << 26));
	mtk_dsc_write(dsc, DISP_REG_DSC_PPS15,
		      cfg->rc_range_params[6].range_min_qp |
		      (cfg->rc_range_params[6].range_max_qp << 5) |
		      (cfg->rc_range_params[6].range_bpg_offset << 10) |
		      (cfg->rc_range_params[7].range_min_qp << 16) |
		      (cfg->rc_range_params[7].range_max_qp << 21) |
		      (cfg->rc_range_params[7].range_bpg_offset << 26));
	mtk_dsc_write(dsc, DISP_REG_DSC_PPS16,
		      cfg->rc_range_params[8].range_min_qp |
		      (cfg->rc_range_params[8].range_max_qp << 5) |
		      (cfg->rc_range_params[8].range_bpg_offset << 10) |
		      (cfg->rc_range_params[9].range_min_qp << 16) |
		      (cfg->rc_range_params[9].range_max_qp << 21) |
		      (cfg->rc_range_params[9].range_bpg_offset << 26));
	mtk_dsc_write(dsc, DISP_REG_DSC_PPS17,
		      cfg->rc_range_params[10].range_min_qp |
		      (cfg->rc_range_params[10].range_max_qp << 5) |
		      (cfg->rc_range_params[10].range_bpg_offset << 10) |
		      (cfg->rc_range_params[11].range_min_qp << 16) |
		      (cfg->rc_range_params[11].range_max_qp << 21) |
		      (cfg->rc_range_params[11].range_bpg_offset << 26));
	mtk_dsc_write(dsc, DISP_REG_DSC_PPS18,
		      cfg->rc_range_params[12].range_min_qp |
		      (cfg->rc_range_params[12].range_max_qp << 5) |
		      (cfg->rc_range_params[12].range_bpg_offset << 10) |
		      (cfg->rc_range_params[13].range_min_qp << 16) |
		      (cfg->rc_range_params[13].range_max_qp << 21) |
		      (cfg->rc_range_params[13].range_bpg_offset << 26));
	mtk_dsc_write(dsc, DISP_REG_DSC_PPS19,
		      cfg->rc_range_params[14].range_min_qp |
		      (cfg->rc_range_params[14].range_max_qp << 5) |
		      (cfg->rc_range_params[14].range_bpg_offset << 10));

	if (cfg->dsc_version_minor == 1)
		mtk_dsc_write(dsc, DISP_REG_DSC_SHADOW, 0x20);
	else if (cfg->dsc_version_minor == 2)
		mtk_dsc_write(dsc, DISP_REG_DSC_SHADOW, 0x40);
	else
		dev_dbg(dev, "wrong version minor:%d", cfg->dsc_version_minor);
}

int mtk_dsc_clk_enable(struct device *dev)
{
	struct mtk_dsc *dsc = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(dsc->clk);
	if (ret < 0)
		dev_err(dsc->dev, "Failed to enable clk:%d\n", ret);

	return ret;
}

void mtk_dsc_clk_disable(struct device *dev)
{
	struct mtk_dsc *dsc = dev_get_drvdata(dev);

	clk_disable_unprepare(dsc->clk);
}

static int mtk_dsc_bind(struct device *dev, struct device *master,
			void *data)
{
	return 0;
}

static void mtk_dsc_unbind(struct device *dev, struct device *master,
				void *data)
{
}

static const struct component_ops mtk_dsc_component_ops = {
	.bind = mtk_dsc_bind,
	.unbind = mtk_dsc_unbind,
};

static int mtk_dsc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_dsc *dsc;
	enum mtk_ddp_comp_id comp_id;
	int ret;

	dsc = devm_kzalloc(dev, sizeof(*dsc), GFP_KERNEL);
	if (!dsc)
		return -ENOMEM;

	dsc->dev = dev;

	dsc->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dsc->regs))
		return dev_err_probe(dev, PTR_ERR(dsc->regs),
					 "Failed to ioremap mem resource\n");

	comp_id = mtk_ddp_comp_get_id(dev->of_node, MTK_DISP_DSC);
	if (comp_id < 0) {
		dev_err(dev, "Failed to identify by alias: %d\n", comp_id);
		return comp_id;
	}

	ret = mtk_ddp_comp_init(dev, dev->of_node, &dsc->ddp_comp, comp_id);
	if (ret) {
		dev_err(dev, "Failed to initialize component: %d\n", ret);
		return ret;
	}

	dsc->data = of_device_get_match_data(dev);

	platform_set_drvdata(pdev, dsc);

	ret = component_add(dev, &mtk_dsc_component_ops);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to add component.\n");

	dsc->clk = devm_clk_get(dsc->dev, NULL);
	if (IS_ERR(dsc->clk))
		return dev_err_probe(dev, PTR_ERR(dsc->clk),
				"Failed to get clock\n");

	dev_dbg(dsc->dev, "done\n");

	return ret;
}

static int mtk_dsc_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &mtk_dsc_component_ops);

	return 0;
}

static const struct mtk_dsc_data mt8189_dsc_driver_conf = {
	.obuf = 0x7c1,
};

static const struct mtk_dsc_data mt8195_dsc_driver_conf = {
	.dsc_bypass_enable = true,
};

static const struct mtk_dsc_data mt8196_dsc_driver_conf = {
	.pt_mem_en = true,
	.supports_10bit = true,
	.obuf = 0x410,
};

static const struct of_device_id mtk_dsc_driver_dt_match[] = {
	{ .compatible = "mediatek,mt8189-disp-dsc",
	  .data = &mt8189_dsc_driver_conf},
	{ .compatible = "mediatek,mt8195-disp-dsc",
	  .data = &mt8195_dsc_driver_conf},
	{ .compatible = "mediatek,mt8196-disp-dsc",
	  .data = &mt8196_dsc_driver_conf},
	{},
};

MODULE_DEVICE_TABLE(of, mtk_dsc_driver_dt_match);

struct platform_driver mtk_dsc_driver = {
	.probe = mtk_dsc_probe,
	.remove = mtk_dsc_remove,
	.driver = {
		.name = "mediatek-disp-dsc",
		.owner = THIS_MODULE,
		.of_match_table = mtk_dsc_driver_dt_match,
	},
};
