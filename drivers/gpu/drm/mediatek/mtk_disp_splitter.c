// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024 MediaTek Inc.
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/soc/mediatek/mtk-cmdq.h>
#include <linux/soc/mediatek/mtk-mmsys.h>
#include <linux/soc/mediatek/mtk-mutex.h>

#include "mtk_ddp_comp.h"
#include "mtk_drm_drv.h"
#include "mtk_disp_drv.h"

#define DISP_REG_SPLITTER_CTL			0x00
#define SPLITTER_ENABLE					BIT(0)
#define SPLITTER_OUT_MODE(n)				((n) << 16)
#define DISP_REG_SPLITTER_SRC_SIZE		0x04
#define DISP_REG_SPLITTER_OUT0_OFFSET		0x10
#define DISP_REG_SPLITTER_OUT0_SIZE		0x14
#define DISP_REG_SPLITTER_OUT1_OFFSET		0x18
#define DISP_REG_SPLITTER_OUT1_SIZE		0x1c
#define DISP_REG_SPLITTER_SHADOW_CTL		0x4c
#define SPLITTER_FORCE_COMMIT				BIT(0)
#define SPLITTER_BYPASS_SHADOW				BIT(1)

enum mtk_splitter_out_1tnp_mode {
	MTK_SPLITTER_OUT_1T1P,
	MTK_SPLITTER_OUT_1T2P,
	MTK_SPLITTER_OUT_1T3P,
	MTK_SPLITTER_OUT_1T4P,
};

/*
 * The splitter forks its single input into several DSC engines that are not
 * part of the CRTC ddp path. They are looked up from the device tree by
 * alias and their clock/config/start lifecycle is driven from here, so the
 * whole "fork into N DSC" block appears to the CRTC as the single splitter
 * component. What the DSC outputs merge/route into is left to the path.
 */
enum mtk_splitter_comp_type {
	MTK_SPLITTER_COMP_TYPE_DSC,
	MTK_SPLITTER_COMP_TYPE_NUM,
};

enum mtk_splitter_comp_id {
	MTK_SPLITTER_DSC0,
	MTK_SPLITTER_DSC1,
	MTK_SPLITTER_COMP_ID_MAX,
};

struct mtk_splitter_comp_match {
	enum mtk_splitter_comp_type type;
	int alias_id;
	enum mtk_ddp_comp_id comp_id;
};

static const char * const mtk_splitter_comp_stem[MTK_SPLITTER_COMP_TYPE_NUM] = {
	[MTK_SPLITTER_COMP_TYPE_DSC] = "dsc",
};

static const struct mtk_splitter_comp_match
mtk_splitter_comp_matches[MTK_SPLITTER_COMP_ID_MAX] = {
	[MTK_SPLITTER_DSC0] = { MTK_SPLITTER_COMP_TYPE_DSC, 0, DDP_COMPONENT_DSC0 },
	[MTK_SPLITTER_DSC1] = { MTK_SPLITTER_COMP_TYPE_DSC, 1, DDP_COMPONENT_DSC1 },
};

static const struct of_device_id mtk_splitter_comp_dt_ids[] = {
	{ .compatible = "mediatek,mt8196-disp-dsc",
	  .data = (void *)MTK_SPLITTER_COMP_TYPE_DSC },
	{},
};

/* mt8196 splitter fans its input out to these DSC engines. */
static const unsigned int mt8196_splitter_dsc[] = {
	MTK_SPLITTER_DSC0,
	MTK_SPLITTER_DSC1,
};

struct mtk_disp_splitter_data {
	u8 out_1tnp;
	const unsigned int *path;
	unsigned int path_size;
};

struct mtk_disp_splitter {
	struct device *dev;
	struct cmdq_client_reg cmdq_reg;
	void __iomem *regs;
	struct clk *clk;
	const struct mtk_disp_splitter_data *data;
	/* DSC engines fanned out from the splitter, indexed by comp id. */
	struct device *comp_dev[MTK_SPLITTER_COMP_ID_MAX];
	struct dsc_info dsc_info;
};

static enum mtk_ddp_comp_id mtk_splitter_get_ddp_comp_id(unsigned int id)
{
	return mtk_splitter_comp_matches[id].comp_id;
}

void mtk_splitter_start(struct device *dev)
{
	struct mtk_disp_splitter *priv = dev_get_drvdata(dev);
	u32 val = SPLITTER_OUT_MODE(priv->data->out_1tnp) | SPLITTER_ENABLE;
	int i;

	writel(val, priv->regs + DISP_REG_SPLITTER_CTL);

	for (i = 0; i < priv->data->path_size; i++)
		mtk_dsc_start(priv->comp_dev[priv->data->path[i]]);
}

void mtk_splitter_stop(struct device *dev)
{
	struct mtk_disp_splitter *priv = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < priv->data->path_size; i++)
		mtk_dsc_stop(priv->comp_dev[priv->data->path[i]]);

	writel(0, priv->regs + DISP_REG_SPLITTER_CTL);
}

void mtk_splitter_config(struct device *dev, unsigned int w, unsigned int h,
			 unsigned int vrefresh, unsigned int bpc, struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_disp_splitter *priv = dev_get_drvdata(dev);
	unsigned int half_w = w / 2;
	int i;

	mtk_ddp_write(cmdq_pkt, SPLITTER_FORCE_COMMIT | SPLITTER_BYPASS_SHADOW,
		      &priv->cmdq_reg, priv->regs, DISP_REG_SPLITTER_SHADOW_CTL);
	mtk_ddp_write(cmdq_pkt, w | (h << 16),
		      &priv->cmdq_reg, priv->regs, DISP_REG_SPLITTER_SRC_SIZE);
	mtk_ddp_write(cmdq_pkt, 0,
		      &priv->cmdq_reg, priv->regs, DISP_REG_SPLITTER_OUT0_OFFSET);
	mtk_ddp_write(cmdq_pkt, half_w | (h << 16),
		      &priv->cmdq_reg, priv->regs, DISP_REG_SPLITTER_OUT0_SIZE);
	mtk_ddp_write(cmdq_pkt, half_w,
		      &priv->cmdq_reg, priv->regs, DISP_REG_SPLITTER_OUT1_OFFSET);
	mtk_ddp_write(cmdq_pkt, half_w | (h << 16),
		      &priv->cmdq_reg, priv->regs, DISP_REG_SPLITTER_OUT1_SIZE);

	dev_dbg(dev, "splitter config: src w=%u -> per-DSC w=%u h=%u (dsc_num=%u)\n",
		w, half_w, h, priv->data->path_size);

	for (i = 0; i < priv->data->path_size; i++)
		mtk_dsc_config(priv->comp_dev[priv->data->path[i]], half_w, h,
			       vrefresh, bpc, cmdq_pkt);
}

int mtk_splitter_clk_enable(struct device *dev)
{
	struct mtk_disp_splitter *priv = dev_get_drvdata(dev);
	int i, j, ret;

	ret = clk_prepare_enable(priv->clk);
	if (ret)
		return ret;

	for (i = 0; i < priv->data->path_size; i++) {
		ret = mtk_dsc_clk_enable(priv->comp_dev[priv->data->path[i]]);
		if (ret)
			goto err_disable;
	}

	return 0;

err_disable:
	for (j = 0; j < i; j++)
		mtk_dsc_clk_disable(priv->comp_dev[priv->data->path[j]]);
	clk_disable_unprepare(priv->clk);
	return ret;
}

void mtk_splitter_clk_disable(struct device *dev)
{
	struct mtk_disp_splitter *priv = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < priv->data->path_size; i++)
		mtk_dsc_clk_disable(priv->comp_dev[priv->data->path[i]]);

	clk_disable_unprepare(priv->clk);
}

void mtk_splitter_connect(struct device *dev, struct device *mmsys_dev,
			  unsigned int next)
{
	struct mtk_disp_splitter *priv = dev_get_drvdata(dev);
	enum mtk_ddp_comp_id dsc;
	int i;

	for (i = 0; i < priv->data->path_size; i++) {
		dsc = mtk_splitter_get_ddp_comp_id(priv->data->path[i]);
		mtk_mmsys_ddp_connect(mmsys_dev, DDP_COMPONENT_SPLITTER0, dsc);
		mtk_mmsys_ddp_connect(mmsys_dev, dsc, next);
	}
}

void mtk_splitter_disconnect(struct device *dev, struct device *mmsys_dev,
			     unsigned int next)
{
	struct mtk_disp_splitter *priv = dev_get_drvdata(dev);
	enum mtk_ddp_comp_id dsc;
	int i;

	for (i = 0; i < priv->data->path_size; i++) {
		dsc = mtk_splitter_get_ddp_comp_id(priv->data->path[i]);
		mtk_mmsys_ddp_disconnect(mmsys_dev, DDP_COMPONENT_SPLITTER0, dsc);
		mtk_mmsys_ddp_disconnect(mmsys_dev, dsc, next);
	}
}

void mtk_splitter_add(struct device *dev, struct mtk_mutex *mutex)
{
	struct mtk_disp_splitter *priv = dev_get_drvdata(dev);
	int i;

	mtk_mutex_add_comp(mutex, mtk_ddp_comp_get_id(dev->of_node, MTK_DISP_SPLITTER));

	for (i = 0; i < priv->data->path_size; i++)
		mtk_mutex_add_comp(mutex, mtk_splitter_get_ddp_comp_id(priv->data->path[i]));
}

void mtk_splitter_remove(struct device *dev, struct mtk_mutex *mutex)
{
	struct mtk_disp_splitter *priv = dev_get_drvdata(dev);
	int i;

	mtk_mutex_remove_comp(mutex, mtk_ddp_comp_get_id(dev->of_node, MTK_DISP_SPLITTER));

	for (i = 0; i < priv->data->path_size; i++)
		mtk_mutex_remove_comp(mutex, mtk_splitter_get_ddp_comp_id(priv->data->path[i]));
}

void mtk_splitter_set_dsc_info(struct device *dev,
			       const struct dsc_info *dsc_info)
{
	struct mtk_disp_splitter *priv = dev_get_drvdata(dev);
	unsigned int dsc_num = priv->data->path_size;
	int i;

	if (!dsc_info) {
		dev_err(dev, "dsc_info is NULL\n");
		return;
	}

	priv->dsc_info = *dsc_info;

	if (priv->dsc_info.dsc_config.slice_count >= dsc_num)
		priv->dsc_info.dsc_config.slice_count /= dsc_num;
	else
		dev_warn(dev, "slice_count %u < dsc_num %u, cannot split evenly\n",
			 priv->dsc_info.dsc_config.slice_count, dsc_num);

	dev_dbg(dev, "set_dsc: dsc_num=%u compression=%d slice_count=%u\n",
		dsc_num, priv->dsc_info.compression_enable,
		priv->dsc_info.dsc_config.slice_count);

	for (i = 0; i < dsc_num; i++)
		mtk_dsc_set_dsc_info(
			priv->comp_dev[priv->data->path[i]],
			&priv->dsc_info);
}

void mtk_splitter_get_dsc_comps(struct device *dev, struct device **dsc_dev,
				u32 max, u32 *count)
{
	struct mtk_disp_splitter *priv = dev_get_drvdata(dev);
	u32 n = 0;
	int i;

	for (i = 0; i < priv->data->path_size && n < max; i++)
		dsc_dev[n++] = priv->comp_dev[priv->data->path[i]];

	*count = n;
}

/* Resolve @node to its internal splitter comp id via type + DT alias. */
static int mtk_splitter_comp_get_id(struct device_node *node,
				    enum mtk_splitter_comp_type type)
{
	int alias_id = of_alias_get_id(node, mtk_splitter_comp_stem[type]);
	int i;

	for (i = 0; i < ARRAY_SIZE(mtk_splitter_comp_matches); i++)
		if (mtk_splitter_comp_matches[i].type == type &&
		    mtk_splitter_comp_matches[i].alias_id == alias_id)
			return i;

	return -EINVAL;
}

static void mtk_splitter_put_dev(void *data)
{
	put_device(data);
}

/* Look up the DSC devices this splitter fans out to from the device tree. */
static int mtk_splitter_comp_init(struct mtk_disp_splitter *priv)
{
	struct device *dev = priv->dev;
	struct device_node *parent = dev->of_node->parent;
	int i, ret;

	for_each_child_of_node_scoped(parent, node) {
		const struct of_device_id *of_id;
		enum mtk_splitter_comp_type type;
		struct platform_device *comp_pdev;
		bool found = false;
		int id;

		of_id = of_match_node(mtk_splitter_comp_dt_ids, node);
		if (!of_id)
			continue;

		if (!of_device_is_available(node))
			continue;

		type = (enum mtk_splitter_comp_type)(uintptr_t)of_id->data;
		id = mtk_splitter_comp_get_id(node, type);
		if (id < 0)
			continue;

		for (i = 0; i < priv->data->path_size; i++)
			if (priv->data->path[i] == id)
				found = true;
		if (!found)
			continue;

		comp_pdev = of_find_device_by_node(node);
		if (!comp_pdev)
			return -EPROBE_DEFER;

		ret = devm_add_action_or_reset(dev, mtk_splitter_put_dev, &comp_pdev->dev);
		if (ret)
			return ret;

		priv->comp_dev[id] = &comp_pdev->dev;
	}

	for (i = 0; i < priv->data->path_size; i++)
		if (!priv->comp_dev[priv->data->path[i]]) {
			dev_err(dev, "Missing DSC comp for splitter path %d\n", i);
			return -ENODEV;
		}

	return 0;
}

static int mtk_disp_splitter_bind(struct device *dev, struct device *master, void *data)
{
	return 0;
}

static void mtk_disp_splitter_unbind(struct device *dev, struct device *master, void *data)
{
}

static const struct component_ops mtk_disp_splitter_component_ops = {
	.bind = mtk_disp_splitter_bind,
	.unbind = mtk_disp_splitter_unbind,
};

static int mtk_disp_splitter_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_disp_splitter *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;

	priv->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->regs))
		return dev_err_probe(dev, PTR_ERR(priv->regs),
				    "Failed to ioremap mem resource\n");

#if IS_ENABLED(CONFIG_MTK_CMDQ)
	ret = cmdq_dev_get_client_reg(dev, &priv->cmdq_reg, 0);
	if (ret)
		dev_dbg(dev, "get mediatek,gce-client-reg fail!\n");
#endif

	priv->data = of_device_get_match_data(dev);
	platform_set_drvdata(pdev, priv);

	priv->clk = devm_clk_get(priv->dev, NULL);
	if (IS_ERR(priv->clk))
		return dev_err_probe(dev, PTR_ERR(priv->clk), "Failed to get clock\n");

	ret = mtk_splitter_comp_init(priv);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to init splitter sub-components\n");

	ret = component_add(dev, &mtk_disp_splitter_component_ops);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to add component.\n");

	return ret;
}

static void mtk_disp_splitter_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &mtk_disp_splitter_component_ops);
}

static const struct mtk_disp_splitter_data mt8196_splitter_driver_data = {
	.out_1tnp = MTK_SPLITTER_OUT_1T2P,
	.path = mt8196_splitter_dsc,
	.path_size = ARRAY_SIZE(mt8196_splitter_dsc),
};

static const struct of_device_id mtk_disp_splitter_driver_dt_match[] = {
	{ .compatible = "mediatek,mt8196-disp-splitter",
	  .data = &mt8196_splitter_driver_data,	},
	{},
};
MODULE_DEVICE_TABLE(of, mtk_disp_splitter_driver_dt_match);

struct platform_driver mtk_disp_splitter_driver = {
	.probe = mtk_disp_splitter_probe,
	.remove_new = mtk_disp_splitter_remove,
	.driver = {
		.name = "mediatek-disp-splitter",
		.of_match_table = mtk_disp_splitter_driver_dt_match,
	},
};
