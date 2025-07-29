// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015 MediaTek Inc.
 */

#include <drm/drm_fourcc.h>

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/regmap.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/mfd/syscon.h>
#include <linux/pm_runtime.h>
#include <linux/soc/mediatek/mtk-cmdq.h>

#include "mtk_crtc.h"
#include "mtk_ddp_comp.h"
#include "mtk_disp_drv.h"
#include "mtk_drm_drv.h"

#define DISP_REG_RDMA_INT_ENABLE		0x0000
#define DISP_REG_RDMA_INT_STATUS		0x0004
#define RDMA_TARGET_LINE_INT				BIT(5)
#define RDMA_FIFO_UNDERFLOW_INT				BIT(4)
#define RDMA_EOF_ABNORMAL_INT				BIT(3)
#define RDMA_FRAME_END_INT				BIT(2)
#define RDMA_FRAME_START_INT				BIT(1)
#define RDMA_REG_UPDATE_INT				BIT(0)
#define DISP_REG_RDMA_GLOBAL_CON		0x0010
#define RDMA_ENGINE_EN					BIT(0)
#define RDMA_MODE_MEMORY				BIT(1)
#define DISP_REG_RDMA_SIZE_CON_0		0x0014
#define RDMA_MATRIX_ENABLE				BIT(17)
#define RDMA_MATRIX_INT_MTX_SEL				GENMASK(23, 20)
#define RDMA_MATRIX_INT_MTX_BT601_to_RGB		(6 << 20)
#define DISP_REG_RDMA_SIZE_CON_1		0x0018
#define DISP_REG_RDMA_TARGET_LINE		0x001c
#define DISP_RDMA_MEM_CON			0x0024
#define MEM_MODE_INPUT_FORMAT_RGB565			(0x000 << 4)
#define MEM_MODE_INPUT_FORMAT_RGB888			(0x001 << 4)
#define MEM_MODE_INPUT_FORMAT_RGBA8888			(0x002 << 4)
#define MEM_MODE_INPUT_FORMAT_ARGB8888			(0x003 << 4)
#define MEM_MODE_INPUT_FORMAT_UYVY			(0x004 << 4)
#define MEM_MODE_INPUT_FORMAT_YUYV			(0x005 << 4)
#define MEM_MODE_INPUT_SWAP				BIT(8)
#define DISP_RDMA_MEM_SRC_PITCH			0x002c
#define DISP_RDMA_MEM_GMC_SETTING_0		0x0030
#define DISP_REG_RDMA_FIFO_CON			0x0040
#define DISP_RDMA_DEBUG				0x0090
#define DEBUG_OUT_SEL				0x0300
#define DISP_REG_RDMA_CRC			0x00f4
#define DISP_REG_RDMA_DATA_SEL			0x0018
#define DISP_REG_RDMA_SEL			0x000a
#define RDMA_CRC_MASK			GENMASK(15, 0)
#define RDMA_FIFO_UNDERFLOW_EN				BIT(31)
#define RDMA_FIFO_PSEUDO_SIZE(bytes)			(((bytes) / 16) << 16)
#define RDMA_OUTPUT_VALID_FIFO_THRESHOLD(bytes)		((bytes) / 16)
#define RDMA_FIFO_SIZE(rdma)			((rdma)->data->fifo_size)
#define RDMA_THRESHOLD_SIZE(rdma)		((rdma)->data->threshold)
#define DISP_RDMA_MEM_START_ADDR		0x0f00

#define RDMA_MEM_GMC				0x40402020

static const u32 mt8173_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_BGRX8888,
	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_YUYV,
};

struct mtk_disp_rdma_data {
	unsigned int fifo_size;
	const u32 *formats;
	size_t num_formats;
	unsigned int threshold;
	bool to_mminfra_out;
	unsigned int crc_cnt;
	const u32 *ofs;
};

/*
 * struct mtk_disp_rdma - DISP_RDMA driver structure
 * @data: local driver data
 */
struct mtk_disp_rdma {
	struct clk			*clk;
	void __iomem			*regs;
	struct cmdq_client_reg		cmdq_reg;
	const struct mtk_disp_rdma_data	*data;
	void				(*vblank_cb)(void *data);
	void				*vblank_cb_data;
	u32				fifo_size;
	struct mtk_crtc_crc		crc;
	struct regmap *mminfra_reg;
	struct regmap *dispsys_reg;
};

static const u32 rdma_crc_ofs[] = {
	DISP_REG_RDMA_CRC,
};

static irqreturn_t mtk_disp_rdma_irq_handler(int irq, void *dev_id)
{
	struct mtk_disp_rdma *priv = dev_id;

	/* Clear frame completion interrupt */
	writel(0x0, priv->regs + DISP_REG_RDMA_INT_STATUS);

	if (!priv->vblank_cb)
		return IRQ_NONE;

	priv->vblank_cb(priv->vblank_cb_data);

	return IRQ_HANDLED;
}

static void rdma_update_bits(struct device *dev, unsigned int reg,
			     unsigned int mask, unsigned int val)
{
	struct mtk_disp_rdma *rdma = dev_get_drvdata(dev);
	unsigned int tmp = readl(rdma->regs + reg);

	tmp = (tmp & ~mask) | (val & mask);
	writel(tmp, rdma->regs + reg);
}

size_t mtk_rdma_crc_cnt(struct device *dev)
{
	struct mtk_disp_rdma *priv = dev_get_drvdata(dev);

	return priv->crc.cnt;
}

u32 *mtk_rdma_crc_entry(struct device *dev)
{
	struct mtk_disp_rdma *priv = dev_get_drvdata(dev);

	return priv->crc.va;
}

void mtk_rdma_crc_read(struct device *dev)
{
	struct mtk_disp_rdma *priv = dev_get_drvdata(dev);

	mtk_crtc_read_crc(&priv->crc);
}

void mtk_rdma_register_vblank_cb(struct device *dev,
				 void (*vblank_cb)(void *),
				 void *vblank_cb_data)
{
	struct mtk_disp_rdma *rdma = dev_get_drvdata(dev);

	rdma->vblank_cb = vblank_cb;
	rdma->vblank_cb_data = vblank_cb_data;
}

void mtk_rdma_unregister_vblank_cb(struct device *dev)
{
	struct mtk_disp_rdma *rdma = dev_get_drvdata(dev);

	rdma->vblank_cb = NULL;
	rdma->vblank_cb_data = NULL;
}

void mtk_rdma_enable_vblank(struct device *dev)
{
	rdma_update_bits(dev, DISP_REG_RDMA_INT_ENABLE, RDMA_FRAME_END_INT,
			 RDMA_FRAME_END_INT);
}

void mtk_rdma_disable_vblank(struct device *dev)
{
	rdma_update_bits(dev, DISP_REG_RDMA_INT_ENABLE, RDMA_FRAME_END_INT, 0);
}

const u32 *mtk_rdma_get_formats(struct device *dev)
{
	struct mtk_disp_rdma *rdma = dev_get_drvdata(dev);

	return rdma->data->formats;
}

size_t mtk_rdma_get_num_formats(struct device *dev)
{
	struct mtk_disp_rdma *rdma = dev_get_drvdata(dev);

	return rdma->data->num_formats;
}

int mtk_rdma_clk_enable(struct device *dev)
{
	struct mtk_disp_rdma *rdma = dev_get_drvdata(dev);

	return clk_prepare_enable(rdma->clk);
}

void mtk_rdma_clk_disable(struct device *dev)
{
	struct mtk_disp_rdma *rdma = dev_get_drvdata(dev);

	clk_disable_unprepare(rdma->clk);
}

void mtk_rdma_start(struct device *dev)
{
	struct mtk_disp_rdma *priv = dev_get_drvdata(dev);

	rdma_update_bits(dev, DISP_REG_RDMA_GLOBAL_CON, RDMA_ENGINE_EN,
			 RDMA_ENGINE_EN);

	if (priv->crc.cnt && priv->crc.crc_sel && priv->crc.crc_out) {
		regmap_update_bits(priv->mminfra_reg,
			priv->crc.crc_sel + DEBUG_OUT_SEL, RDMA_CRC_MASK, DISP_REG_RDMA_SEL);
		regmap_update_bits(priv->dispsys_reg,
			priv->crc.crc_out + DEBUG_OUT_SEL, RDMA_CRC_MASK, DISP_REG_RDMA_DATA_SEL);

#if IS_REACHABLE(CONFIG_MTK_CMDQ)
		mtk_crtc_start_crc_cmdq(&priv->crc);
#endif
	}
}

void mtk_rdma_stop(struct device *dev)
{
	struct mtk_disp_rdma *priv = dev_get_drvdata(dev);

	rdma_update_bits(dev, DISP_REG_RDMA_GLOBAL_CON, RDMA_ENGINE_EN, 0);

#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	if (priv->crc.cnt)
		mtk_crtc_stop_crc_cmdq(&priv->crc);
#endif
}

void mtk_rdma_config(struct device *dev, unsigned int width,
		     unsigned int height, unsigned int vrefresh,
		     unsigned int bpc, struct cmdq_pkt *cmdq_pkt)
{
	unsigned int threshold;
	unsigned int reg;
	struct mtk_disp_rdma *rdma = dev_get_drvdata(dev);
	u32 rdma_fifo_size;

	mtk_ddp_write_mask(cmdq_pkt, width, &rdma->cmdq_reg, rdma->regs,
			   DISP_REG_RDMA_SIZE_CON_0, 0xfff);
	mtk_ddp_write_mask(cmdq_pkt, height, &rdma->cmdq_reg, rdma->regs,
			   DISP_REG_RDMA_SIZE_CON_1, 0xfffff);

	if (rdma->fifo_size)
		rdma_fifo_size = rdma->fifo_size;
	else
		rdma_fifo_size = RDMA_FIFO_SIZE(rdma);

	/*
	 * Enable FIFO underflow since DSI and DPI can't be blocked.
	 * Keep the FIFO pseudo size reset default of 8 KiB. Set the
	 * output threshold to 70% of max fifo size to make sure the
	 * threhold will not overflow
	 */
	if (RDMA_THRESHOLD_SIZE(rdma))
		threshold = RDMA_THRESHOLD_SIZE(rdma);
	else
		threshold = rdma_fifo_size * 7 / 10;
	reg = RDMA_FIFO_UNDERFLOW_EN |
	      RDMA_FIFO_PSEUDO_SIZE(rdma_fifo_size) |
	      RDMA_OUTPUT_VALID_FIFO_THRESHOLD(threshold);
	mtk_ddp_write(cmdq_pkt, reg, &rdma->cmdq_reg, rdma->regs, DISP_REG_RDMA_FIFO_CON);
}

static unsigned int rdma_fmt_convert(struct mtk_disp_rdma *rdma,
				     unsigned int fmt)
{
	/* The return value in switch "MEM_MODE_INPUT_FORMAT_XXX"
	 * is defined in mediatek HW data sheet.
	 * The alphabet order in XXX is no relation to data
	 * arrangement in memory.
	 */
	switch (fmt) {
	default:
	case DRM_FORMAT_RGB565:
		return MEM_MODE_INPUT_FORMAT_RGB565;
	case DRM_FORMAT_BGR565:
		return MEM_MODE_INPUT_FORMAT_RGB565 | MEM_MODE_INPUT_SWAP;
	case DRM_FORMAT_RGB888:
		return MEM_MODE_INPUT_FORMAT_RGB888;
	case DRM_FORMAT_BGR888:
		return MEM_MODE_INPUT_FORMAT_RGB888 | MEM_MODE_INPUT_SWAP;
	case DRM_FORMAT_RGBX8888:
	case DRM_FORMAT_RGBA8888:
		return MEM_MODE_INPUT_FORMAT_ARGB8888;
	case DRM_FORMAT_BGRX8888:
	case DRM_FORMAT_BGRA8888:
		return MEM_MODE_INPUT_FORMAT_ARGB8888 | MEM_MODE_INPUT_SWAP;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
		return MEM_MODE_INPUT_FORMAT_RGBA8888;
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
		return MEM_MODE_INPUT_FORMAT_RGBA8888 | MEM_MODE_INPUT_SWAP;
	case DRM_FORMAT_UYVY:
		return MEM_MODE_INPUT_FORMAT_UYVY;
	case DRM_FORMAT_YUYV:
		return MEM_MODE_INPUT_FORMAT_YUYV;
	}
}

unsigned int mtk_rdma_layer_nr(struct device *dev)
{
	return 1;
}

void mtk_rdma_layer_config(struct device *dev, unsigned int idx,
			   struct mtk_plane_state *state,
			   struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_disp_rdma *rdma = dev_get_drvdata(dev);
	struct mtk_plane_pending_state *pending = &state->pending;
	unsigned int addr = pending->addr;
	unsigned int pitch = pending->pitch & 0xffff;
	unsigned int fmt = pending->format;
	unsigned int con;

	con = rdma_fmt_convert(rdma, fmt);
	mtk_ddp_write_relaxed(cmdq_pkt, con, &rdma->cmdq_reg, rdma->regs, DISP_RDMA_MEM_CON);

	if (fmt == DRM_FORMAT_UYVY || fmt == DRM_FORMAT_YUYV) {
		mtk_ddp_write_mask(cmdq_pkt, RDMA_MATRIX_ENABLE, &rdma->cmdq_reg, rdma->regs,
				   DISP_REG_RDMA_SIZE_CON_0,
				   RDMA_MATRIX_ENABLE);
		mtk_ddp_write_mask(cmdq_pkt, RDMA_MATRIX_INT_MTX_BT601_to_RGB,
				   &rdma->cmdq_reg, rdma->regs, DISP_REG_RDMA_SIZE_CON_0,
				   RDMA_MATRIX_INT_MTX_SEL);
	} else {
		mtk_ddp_write_mask(cmdq_pkt, 0, &rdma->cmdq_reg, rdma->regs,
				   DISP_REG_RDMA_SIZE_CON_0,
				   RDMA_MATRIX_ENABLE);
	}
	mtk_ddp_write_relaxed(cmdq_pkt, addr, &rdma->cmdq_reg, rdma->regs,
			      DISP_RDMA_MEM_START_ADDR);
	mtk_ddp_write_relaxed(cmdq_pkt, pitch, &rdma->cmdq_reg, rdma->regs,
			      DISP_RDMA_MEM_SRC_PITCH);
	mtk_ddp_write(cmdq_pkt, RDMA_MEM_GMC, &rdma->cmdq_reg, rdma->regs,
		      DISP_RDMA_MEM_GMC_SETTING_0);
	mtk_ddp_write_mask(cmdq_pkt, RDMA_MODE_MEMORY, &rdma->cmdq_reg, rdma->regs,
			   DISP_REG_RDMA_GLOBAL_CON, RDMA_MODE_MEMORY);

}

static int mtk_disp_rdma_bind(struct device *dev, struct device *master,
			      void *data)
{
	return 0;

}

static void mtk_disp_rdma_unbind(struct device *dev, struct device *master,
				 void *data)
{
}

static const struct component_ops mtk_disp_rdma_component_ops = {
	.bind	= mtk_disp_rdma_bind,
	.unbind = mtk_disp_rdma_unbind,
};

static int mtk_disp_rdma_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct device_node *np;
	struct resource crc_res;
	struct mtk_disp_rdma *priv;
	struct resource *res;
	int irq;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(dev, "failed to get rdma clk\n");
		return PTR_ERR(priv->clk);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->regs)) {
		dev_err(dev, "failed to ioremap rdma\n");
		return PTR_ERR(priv->regs);
	}
#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	ret = cmdq_dev_get_client_reg(dev, &priv->cmdq_reg, 0);
	if (ret)
		dev_dbg(dev, "get mediatek,gce-client-reg fail!\n");
#endif

	if (of_find_property(dev->of_node, "mediatek,rdma-fifo-size", &ret)) {
		ret = of_property_read_u32(dev->of_node,
					   "mediatek,rdma-fifo-size",
					   &priv->fifo_size);
		if (ret) {
			dev_err(dev, "Failed to get rdma fifo size\n");
			return ret;
		}
	}

	/* Disable and clear pending interrupts */
	writel(0x0, priv->regs + DISP_REG_RDMA_INT_ENABLE);
	writel(0x0, priv->regs + DISP_REG_RDMA_INT_STATUS);

	ret = devm_request_irq(dev, irq, mtk_disp_rdma_irq_handler,
			       IRQF_TRIGGER_NONE, dev_name(dev), priv);
	if (ret < 0) {
		dev_err(dev, "Failed to request irq %d: %d\n", irq, ret);
		return ret;
	}

	priv->data = of_device_get_match_data(dev);

	platform_set_drvdata(pdev, priv);

	pm_runtime_enable(dev);

	ret = component_add(dev, &mtk_disp_rdma_component_ops);
	if (ret) {
		pm_runtime_disable(dev);
		dev_err(dev, "Failed to add component: %d\n", ret);
	}

	/* set crc source */
	priv->crc.cnt = priv->data->crc_cnt;
	priv->crc.to_mminfra_out = priv->data->to_mminfra_out;

	if (priv->crc.cnt) {
		np = of_parse_phandle(node, "mediatek,mminfra", 0);
		if (!np) {
			dev_warn(dev, "fail to get mediatek,mminfra for crc\n");
			goto disable_crc;
		}

		ret = of_address_to_resource(np, 0, &crc_res);
		priv->crc.crc_out = crc_res.start;
		priv->mminfra_reg = syscon_node_to_regmap(np);
		of_node_put(np);
		if (IS_ERR(priv->mminfra_reg)) {
			dev_warn(dev,
				"could not find mminfra node\n");
			goto disable_crc;
		}
		np = of_parse_phandle(node, "mediatek,dispsys-config", 0);
		if (!np) {
			dev_warn(dev, "fail to get mediatek,dispsys-config for crc\n");
		goto disable_crc;
		}

		ret = of_address_to_resource(np, 0, &crc_res);
		priv->crc.crc_sel = crc_res.start;
		priv->dispsys_reg = syscon_node_to_regmap(np);
		of_node_put(np);
		if (IS_ERR(priv->dispsys_reg)) {
			dev_warn(dev,
				"could not find dispsys node\n");
			goto disable_crc;
		}
		priv->crc.ofs = priv->data->ofs;
		priv->crc.rst_ofs = DISP_RDMA_DEBUG;
		priv->crc.rst_msk = RDMA_ENGINE_EN;
		priv->crc.va = kcalloc(priv->crc.cnt, sizeof(*priv->crc.va), GFP_KERNEL);
		if (!priv->crc.va) {
			dev_err(dev, "failed to allocate memory for crc\n");
			goto disable_crc;
		}

		if (of_property_read_u32_index(dev->of_node, "mediatek,gce-events", 0,
					       &priv->crc.cmdq_event)) {
			dev_warn(dev, "failed to get gce-events for crc\n");
			kfree(priv->crc.va);
			goto disable_crc;
		}

		priv->crc.cmdq_reg = &priv->cmdq_reg;
		mtk_crtc_create_crc_cmdq(dev, &priv->crc);
	}

	return ret;

disable_crc:
	priv->crc.cnt = 0;

	return 0;
}

static void mtk_disp_rdma_remove(struct platform_device *pdev)
{
	struct mtk_disp_rdma *priv = dev_get_drvdata(&pdev->dev);

	component_del(&pdev->dev, &mtk_disp_rdma_component_ops);
	if (priv->crc.cnt) {
		kfree(priv->crc.va);
		memset(&priv->crc, 0, sizeof(struct mtk_crtc_crc));
	}
	pm_runtime_disable(&pdev->dev);
}

static const struct mtk_disp_rdma_data mt2701_rdma_driver_data = {
	.fifo_size = SZ_4K,
	.formats = mt8173_formats,
	.num_formats = ARRAY_SIZE(mt8173_formats),
};

static const struct mtk_disp_rdma_data mt8173_rdma_driver_data = {
	.fifo_size = SZ_8K,
	.formats = mt8173_formats,
	.num_formats = ARRAY_SIZE(mt8173_formats),
};

static const struct mtk_disp_rdma_data mt8183_rdma_driver_data = {
	.fifo_size = 5 * SZ_1K,
	.formats = mt8173_formats,
	.num_formats = ARRAY_SIZE(mt8173_formats),
};

static const struct mtk_disp_rdma_data mt8195_rdma_driver_data = {
	.fifo_size = 1920,
	.formats = mt8173_formats,
	.num_formats = ARRAY_SIZE(mt8173_formats),
};

static const struct mtk_disp_rdma_data mt8189_rdma_driver_data = {
	.fifo_size = 2200,
	.formats = mt8173_formats,
	.num_formats = ARRAY_SIZE(mt8173_formats),
	.to_mminfra_out = true,
	.crc_cnt = ARRAY_SIZE(rdma_crc_ofs),
	.ofs = rdma_crc_ofs,
};

static const struct of_device_id mtk_disp_rdma_driver_dt_match[] = {
	{ .compatible = "mediatek,mt2701-disp-rdma",
	  .data = &mt2701_rdma_driver_data},
	{ .compatible = "mediatek,mt8173-disp-rdma",
	  .data = &mt8173_rdma_driver_data},
	{ .compatible = "mediatek,mt8183-disp-rdma",
	  .data = &mt8183_rdma_driver_data},
	{ .compatible = "mediatek,mt8189-disp-rdma",
	  .data = &mt8189_rdma_driver_data},
	{ .compatible = "mediatek,mt8195-disp-rdma",
	  .data = &mt8195_rdma_driver_data},
	{},
};
MODULE_DEVICE_TABLE(of, mtk_disp_rdma_driver_dt_match);

struct platform_driver mtk_disp_rdma_driver = {
	.probe		= mtk_disp_rdma_probe,
	.remove_new	= mtk_disp_rdma_remove,
	.driver		= {
		.name	= "mediatek-disp-rdma",
		.owner	= THIS_MODULE,
		.of_match_table = mtk_disp_rdma_driver_dt_match,
	},
};
