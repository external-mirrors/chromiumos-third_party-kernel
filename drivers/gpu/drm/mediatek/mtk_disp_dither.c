// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/soc/mediatek/mtk-cmdq.h>

#include "mtk_crtc.h"
#include "mtk_ddp_comp.h"
#include "mtk_disp_drv.h"
#include "mtk_drm_drv.h"

#define DISP_REG_DITHER_EN			0x0000
#define DITHER_EN				BIT(0)
#define DISP_REG_DITHER_CFG			0x0020
#define DITHER_RELAY_MODE			BIT(0)
#define DITHER_ENGINE_EN			BIT(1)
#define DISP_DITHERING				BIT(2)
#define DISP_REG_DITHER_SIZE			0x0030
#define DISP_REG_DITHER(x) 			(0x100 + (x)*4)
#define DITHER_LSB_ERR_SHIFT_R(x)		(((x) & 0x7) << 28)
#define DITHER_ADD_LSHIFT_R(x)			(((x) & 0x7) << 20)
#define DITHER_NEW_BIT_MODE			BIT(0)
#define DITHER_LSB_ERR_SHIFT_B(x)		(((x) & 0x7) << 28)
#define DITHER_ADD_LSHIFT_B(x)			(((x) & 0x7) << 20)
#define DITHER_LSB_ERR_SHIFT_G(x)		(((x) & 0x7) << 12)
#define DITHER_ADD_LSHIFT_G(x)			(((x) & 0x7) << 4)

struct mtk_disp_dither {
	struct clk *clk;
	void __iomem *regs;
	struct cmdq_client_reg cmdq_reg;
};

int mtk_dither_clk_enable(struct device *dev)
{
	struct mtk_disp_dither *dither = dev_get_drvdata(dev);

	return clk_prepare_enable(dither->clk);
}

void mtk_dither_clk_disable(struct device *dev)
{
	struct mtk_disp_dither *dither = dev_get_drvdata(dev);

	clk_disable_unprepare(dither->clk);
}

void mtk_dither_set_common(void __iomem *regs, struct cmdq_client_reg *cmdq_reg,
			   unsigned int bpc, unsigned int cfg,
			   unsigned int dither_en, struct cmdq_pkt *cmdq_pkt)
{
	/* If bpc equal to 0, the dithering function didn't be enabled */
	if (bpc == 0)
		return;

	if (bpc >= MTK_MIN_BPC) {
		mtk_ddp_write(cmdq_pkt, 0, cmdq_reg, regs, DISP_REG_DITHER(5));
		mtk_ddp_write(cmdq_pkt, 0, cmdq_reg, regs, DISP_REG_DITHER(7));
		mtk_ddp_write(cmdq_pkt,
			      DITHER_LSB_ERR_SHIFT_R(MTK_MAX_BPC - bpc) |
			      DITHER_ADD_LSHIFT_R(MTK_MAX_BPC - bpc) |
			      DITHER_NEW_BIT_MODE,
			      cmdq_reg, regs, DISP_REG_DITHER(15));
		mtk_ddp_write(cmdq_pkt,
			      DITHER_LSB_ERR_SHIFT_B(MTK_MAX_BPC - bpc) |
			      DITHER_ADD_LSHIFT_B(MTK_MAX_BPC - bpc) |
			      DITHER_LSB_ERR_SHIFT_G(MTK_MAX_BPC - bpc) |
			      DITHER_ADD_LSHIFT_G(MTK_MAX_BPC - bpc),
			      cmdq_reg, regs, DISP_REG_DITHER(16));
		mtk_ddp_write(cmdq_pkt, dither_en, cmdq_reg, regs, cfg);
	}
}

void mtk_dither_config(struct device *dev, unsigned int w,
		       unsigned int h, unsigned int vrefresh,
		       unsigned int bpc, struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_disp_dither *dither = dev_get_drvdata(dev);

	mtk_ddp_write(cmdq_pkt, w << 16 | h, &dither->cmdq_reg, dither->regs, DISP_REG_DITHER_SIZE);
	mtk_ddp_write(cmdq_pkt, DITHER_RELAY_MODE, &dither->cmdq_reg, dither->regs,
		      DISP_REG_DITHER_CFG);
	mtk_dither_set_common(dither->regs, &dither->cmdq_reg, bpc, DISP_REG_DITHER_CFG,
			      DITHER_ENGINE_EN, cmdq_pkt);
}

void mtk_dither_start(struct device *dev)
{
	struct mtk_disp_dither *dither = dev_get_drvdata(dev);

	writel(DITHER_EN, dither->regs + DISP_REG_DITHER_EN);
}

void mtk_dither_stop(struct device *dev)
{
	struct mtk_disp_dither *dither = dev_get_drvdata(dev);

	writel_relaxed(0x0, dither->regs + DISP_REG_DITHER_EN);
}

void mtk_dither_set(struct device *dev, unsigned int bpc,
		    unsigned int cfg, struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_disp_dither *dither = dev_get_drvdata(dev);

	mtk_dither_set_common(dither->regs, &dither->cmdq_reg, bpc, cfg,
			      DISP_DITHERING, cmdq_pkt);
}

static int mtk_disp_dither_bind(struct device *dev, struct device *master,
			        void *data)
{
	return 0;
}

static void mtk_disp_dither_unbind(struct device *dev, struct device *master,
				   void *data)
{
}

static const struct component_ops mtk_disp_dither_component_ops = {
	.bind	= mtk_disp_dither_bind,
	.unbind = mtk_disp_dither_unbind,
};

static int mtk_disp_dither_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_disp_dither *priv;
	struct resource *res;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(dev, "failed to get dither clk\n");
		return PTR_ERR(priv->clk);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->regs)) {
		dev_err(dev, "failed to ioremap dither\n");
		return PTR_ERR(priv->regs);
	}
#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	ret = cmdq_dev_get_client_reg(dev, &priv->cmdq_reg, 0);
	if (ret)
		dev_dbg(dev, "get mediatek,gce-client-reg fail!\n");
#endif

	platform_set_drvdata(pdev, priv);

	ret = component_add(dev, &mtk_disp_dither_component_ops);
	if (ret)
		dev_err(dev, "Failed to add component: %d\n", ret);

	return ret;
}

static void mtk_disp_dither_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &mtk_disp_dither_component_ops);
}

static const struct of_device_id mtk_disp_dither_driver_dt_match[] = {
	{ .compatible = "mediatek,mt8167-disp-dither", },
	{ .compatible = "mediatek,mt8183-disp-dither", },
	{ .compatible = "mediatek,mt8189-disp-dither", },
	{},
};
MODULE_DEVICE_TABLE(of, mtk_disp_dither_driver_dt_match);

struct platform_driver mtk_disp_dither_driver = {
	.probe		= mtk_disp_dither_probe,
	.remove_new	= mtk_disp_dither_remove,
	.driver		= {
		.name	= "mediatek-disp-dither",
		.owner	= THIS_MODULE,
		.of_match_table = mtk_disp_dither_driver_dt_match,
	},
};
