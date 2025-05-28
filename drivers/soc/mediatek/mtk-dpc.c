// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 MediaTek Inc.
 */

#include <linux/delay.h>
#include <linux/minmax.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/pm_opp.h>
#include <linux/soc/mediatek/mtk-dpc.h>

#define DISP_REG_DPC_ENABLE					0x000
#define DPC_EN								BIT(0)
#define DISP_DPC_MMQOS_ALWAYS_SCAN_EN					BIT(4)
#define DPC_VDO_MODE							BIT(16)
#define DISP_REG_DPC_RESET					0x004
#define DISP_REG_DPC_DISP_DDRSRC_EMIREQ_CFG			0x068
#define DDRSRC_DISP_MUX							BIT(0)
#define EMIREQ_DISP_MUX							BIT(16)
#define DDRSRC_DISP_SW_CTRL						0xc
#define EMIREQ_DISP_SW_CTRL						(0xc << 16)
#define DISP_REG_DPC_MML_DDRSRC_EMIREQ_CFG			0x06c
#define DISP_REG_DPC_DISP_HRTBW_SRTBW_CFG			0x070
#define DPC_HRTBW_DISP_KEEP						BIT(0)
#define DPC_SRTBW_DISP_KEEP						BIT(16)
#define DISP_REG_DPC_MML_HRTBW_SRTBW_CFG			0x074
#define DISP_REG_DPC_DISP_HIGH_HRT_BW				0x078
#define DISP_REG_DPC_DISP_LOW_HRT_BW				0x07c
#define DISP_REG_DPC_DISP_SW_SRT_BW				0x080
#define DISP_REG_DPC_MML_SW_HRT_BW				0x084
#define DISP_REG_DPC_MML_SW_SRT_BW				0x088
#define DISP_REG_DPC_DISP_VDISP_DVFS_CFG			0x090
#define DISP_REG_DPC_MML_VDISP_DVFS_CFG				0x094
#define DISP_REG_DPC_DISP_VDISP_DVFS_VAL			0x098
#define DISP_REG_DPC_MML_VDISP_DVFS_VAL				0x09c
#define DISP_REG_DPC_DISP_INFRA_PLL_OFF_CFG			0x0a0
#define MAINPLL_OFF_DISP_SW_CTRL_VAL					(0x3 << 3)
#define MMINFRA_OFF_DISP_SW_CTRL_VAL					(0x3 << 11)
#define INFRA_OFF_DISP_SW_CTRL_VAL					(0x3 << 19)
#define DISP_REG_DPC_MML_INFRA_PLL_OFF_CFG			0x0a4
#define DISP_REG_DPC_DDREN_ACK_SEL				0x0c0
#define WLA_DDREN_ACK							BIT(0)
#define DISP_DPC_DISPSYS_DISP_SUBCOM_SRT_READ_AXI_HIGH_BW	0xa10
#define DISP_DPC_DISPSYS_MDP_SUBCOM_SRT_READ_AXI_HIGH_BW	0xa14
#define DISP_DPC_DISPSYS_SLB_SRT_READ_AXI_HIGH_BW		0xa18
#define DISP_DPC_DISPSYS_DISP_SUBCOM_SRT_WRITE_AXI_HIGH_BW	0xa1c
#define DISP_DPC_DISPSYS_MDP_SUBCOM_SRT_WRITE_AXI_HIGH_BW	0xa20
#define DISP_DPC_DISPSYS_SLB_SRT_WRITE_AXI_HIGH_BW		0xa24
#define DISP_DPC_DISPSYS_DISP_SUBCOM_HRT_READ_AXI_HIGH_BW	0xa28
#define DISP_DPC_DISPSYS_MDP_SUBCOM_HRT_READ_AXI_HIGH_BW	0xa2c
#define DISP_DPC_DISPSYS_SLB_HRT_READ_AXI_HIGH_BW		0xa30
#define DISP_DPC_DISPSYS_DISP_SUBCOM_HRT_WRITE_AXI_HIGH_BW	0xa34
#define DISP_DPC_DISPSYS_MDP_SUBCOM_HRT_WRITE_AXI_HIGH_BW	0xa38
#define DISP_DPC_DISPSYS_SLB_HRT_WRITE_AXI_HIGH_BW		0xa3c

#define DPC_CHANNEL_NUM						24
#define DPC_TOTAL_SRT_UNIT					64
#define DPC_TOTAL_HRT_UNIT					64
#define DPC_SRT_EMI_EFFICIENCY					13715
#define DPC_HRT_EMI_EFFICIENCY					12132
#define DPC_CH_BW_URATE						70

#define BW_LEVEL_1						3057
#define BW_LEVEL_2						4076
#define BW_LEVEL_3						5129
#define BW_LEVEL_4						6988

#define DPC_CH_BW_MASK(idx) (GENMASK(9, 0) << chan_bw_cfg[idx].shift)
#define DPC_CH_BW_OFFSET(idx) (chan_bw_cfg[idx].offset)
#define DPC_CH_BW_SHIFT(idx) (chan_bw_cfg[idx].shift)
#define DPC_CONVERT_TO_CH_BW(ch_bw) ((ch_bw) * 100 / DPC_CH_BW_URATE / 16)

enum mtk_dpc_bw_type {
	DPC_TOTAL_HRT,
	DPC_TOTAL_SRT,
	DPC_BW_TYPE_CNT,
};

struct mtk_dpc_dvfs_bw {
	u32 disp_bw[DPC_BW_TYPE_CNT];
	u32 mml0_bw[DPC_BW_TYPE_CNT];
	u32 mml1_bw[DPC_BW_TYPE_CNT];
	u8 bw_level;
	u8 mml_level;
	u8 disp_level;
	/* lock for dpc bandwidth and dvfs control */
	struct mutex lock;
};

struct mtk_dpc {
	struct device		*dev;
	void __iomem		*regs;
	bool			enabled;
	u32			ref_cnt[DPC_SUBSYS_CNT];
	struct mtk_dpc_dvfs_bw	dvfs_bw;
	unsigned long		*freq_steps;
	int			step_size;
	int			dvfs_level;
};

struct mtk_dpc_channel_bw_cfg {
	u16 offset;
	u8 shift;
	u16 disp_bw;
	u16 mml_bw;
};

static struct mtk_dpc_channel_bw_cfg chan_bw_cfg[DPC_CHANNEL_NUM] = {
	{DISP_DPC_DISPSYS_DISP_SUBCOM_SRT_READ_AXI_HIGH_BW,	0, 0, 0},
	{DISP_DPC_DISPSYS_DISP_SUBCOM_SRT_WRITE_AXI_HIGH_BW,	0, 0, 0},
	{DISP_DPC_DISPSYS_DISP_SUBCOM_HRT_READ_AXI_HIGH_BW,	0, 0, 0},
	{DISP_DPC_DISPSYS_DISP_SUBCOM_HRT_WRITE_AXI_HIGH_BW,	0, 0, 0},
	{DISP_DPC_DISPSYS_DISP_SUBCOM_SRT_READ_AXI_HIGH_BW,	12, 0, 0},
	{DISP_DPC_DISPSYS_DISP_SUBCOM_SRT_WRITE_AXI_HIGH_BW,	12, 0, 0},
	{DISP_DPC_DISPSYS_DISP_SUBCOM_HRT_READ_AXI_HIGH_BW,	12, 0, 0},
	{DISP_DPC_DISPSYS_DISP_SUBCOM_HRT_WRITE_AXI_HIGH_BW,	12, 0, 0},
	{DISP_DPC_DISPSYS_MDP_SUBCOM_SRT_READ_AXI_HIGH_BW,	0, 0, 0},
	{DISP_DPC_DISPSYS_MDP_SUBCOM_SRT_WRITE_AXI_HIGH_BW,	0, 0, 0},
	{DISP_DPC_DISPSYS_MDP_SUBCOM_HRT_READ_AXI_HIGH_BW,	0, 0, 0},
	{DISP_DPC_DISPSYS_MDP_SUBCOM_HRT_WRITE_AXI_HIGH_BW,	0, 0, 0},
	{DISP_DPC_DISPSYS_MDP_SUBCOM_SRT_READ_AXI_HIGH_BW,	12, 0, 0},
	{DISP_DPC_DISPSYS_MDP_SUBCOM_SRT_WRITE_AXI_HIGH_BW,	12, 0, 0},
	{DISP_DPC_DISPSYS_MDP_SUBCOM_HRT_READ_AXI_HIGH_BW,	12, 0, 0},
	{DISP_DPC_DISPSYS_MDP_SUBCOM_HRT_WRITE_AXI_HIGH_BW,	12, 0, 0},
	{DISP_DPC_DISPSYS_SLB_SRT_READ_AXI_HIGH_BW,		0, 0, 0},
	{DISP_DPC_DISPSYS_SLB_SRT_WRITE_AXI_HIGH_BW,		0, 0, 0},
	{DISP_DPC_DISPSYS_SLB_HRT_READ_AXI_HIGH_BW,		0, 0, 0},
	{DISP_DPC_DISPSYS_SLB_HRT_WRITE_AXI_HIGH_BW,		0, 0, 0},
	{DISP_DPC_DISPSYS_SLB_SRT_READ_AXI_HIGH_BW,		12, 0, 0},
	{DISP_DPC_DISPSYS_SLB_SRT_WRITE_AXI_HIGH_BW,		12, 0, 0},
	{DISP_DPC_DISPSYS_SLB_HRT_READ_AXI_HIGH_BW,		12, 0, 0},
	{DISP_DPC_DISPSYS_SLB_HRT_WRITE_AXI_HIGH_BW,		12, 0, 0},
};

static u8 dpc_max_dvfs_level(struct device *dev)
{
	struct mtk_dpc *priv = dev_get_drvdata(dev);

	return max3(priv->dvfs_bw.disp_level, priv->dvfs_bw.mml_level, priv->dvfs_bw.bw_level);
}

static void dpc_ch_bw_set(struct device *dev, const enum mtk_dpc_subsys subsys,
			  const u8 idx, const u32 bw_in_mb)
{
	struct mtk_dpc *priv = dev_get_drvdata(dev);
	u32 value = 0;
	u32 ch_bw = bw_in_mb;

	value = readl(priv->regs + DPC_CH_BW_OFFSET(idx)) & ~DPC_CH_BW_MASK(idx);
	value |= DPC_CONVERT_TO_CH_BW(ch_bw) << DPC_CH_BW_SHIFT(idx);
	writel(value, priv->regs + DPC_CH_BW_OFFSET(idx));
	dev_dbg(dev, "subsys(%u) idx(%u) bw(%u)MB", subsys, idx, ch_bw);
}

static u8 bw_to_level(const u32 total_bw)
{
	if (total_bw > BW_LEVEL_4)
		return 4;
	else if (total_bw > BW_LEVEL_3)
		return 3;
	else if (total_bw > BW_LEVEL_2)
		return 2;
	else if (total_bw > BW_LEVEL_1)
		return 1;
	else
		return 0;
}

static u32 dpc_update_bw(struct device *dev, enum mtk_dpc_bw_type type,
			 const enum mtk_dpc_subsys subsys, const u32 bw_in_mb)
{
	struct mtk_dpc *priv = dev_get_drvdata(dev);
	u32 total_bw;

	mutex_lock(&priv->dvfs_bw.lock);
	if (subsys == DPC_SUBSYS_DISP)
		priv->dvfs_bw.disp_bw[type] = bw_in_mb;
	else if (subsys == DPC_SUBSYS_MML0)
		priv->dvfs_bw.mml0_bw[type] = bw_in_mb;
	else if (subsys == DPC_SUBSYS_MML1)
		priv->dvfs_bw.mml1_bw[type] = bw_in_mb;

	total_bw = priv->dvfs_bw.disp_bw[type] +
		   priv->dvfs_bw.mml0_bw[type] + priv->dvfs_bw.mml1_bw[type];
	mutex_unlock(&priv->dvfs_bw.lock);
	return total_bw;
}

void dpc_hrt_bw_set(struct device *dev, const enum mtk_dpc_subsys subsys, const u32 bw_in_mb)
{
	struct mtk_dpc *priv = dev_get_drvdata(dev);
	u32 total_bw = dpc_update_bw(dev, DPC_TOTAL_HRT, subsys, bw_in_mb);

	/* trigger dram dvfs first */
	writel(total_bw * DPC_HRT_EMI_EFFICIENCY / 10000 / DPC_TOTAL_HRT_UNIT,
	       priv->regs + DISP_REG_DPC_DISP_HIGH_HRT_BW);

	/* trigger vdisp dvfs */
	dpc_dvfs_set(dev, DPC_SUBSYS_DISP, 0, false);
}
EXPORT_SYMBOL_GPL(dpc_hrt_bw_set);

void dpc_srt_bw_set(struct device *dev, const enum mtk_dpc_subsys subsys, const u32 bw_in_mb)
{
	struct mtk_dpc *priv = dev_get_drvdata(dev);
	u32 total_bw = dpc_update_bw(dev, DPC_TOTAL_SRT, subsys, bw_in_mb);

	writel(total_bw * DPC_SRT_EMI_EFFICIENCY / 10000 / DPC_TOTAL_SRT_UNIT,
	       priv->regs + DISP_REG_DPC_DISP_SW_SRT_BW);
}
EXPORT_SYMBOL_GPL(dpc_srt_bw_set);

void dpc_dvfs_set(struct device *dev, const enum mtk_dpc_subsys subsys, const u8 level,
		  bool update_level)
{
	struct mtk_dpc *priv = dev_get_drvdata(dev);
	u8 max_level;

	if (update_level && level >= priv->step_size) {
		dev_dbg(dev, "vdisp support only %d levels", priv->step_size);
		return;
	}

	mutex_lock(&priv->dvfs_bw.lock);

	if (update_level) {
		if (subsys == DPC_SUBSYS_DISP)
			priv->dvfs_bw.disp_level = level;
		else
			priv->dvfs_bw.mml_level = level;
	}

	max_level = max(dpc_max_dvfs_level(dev), level);

	if (priv->dvfs_level != max_level)
		writel(max_level, priv->regs + DISP_REG_DPC_DISP_VDISP_DVFS_VAL);

	priv->dvfs_level = max_level;
	mutex_unlock(&priv->dvfs_bw.lock);

	dev_dbg(dev, "subsys(%u) level(%u,%u,%u)", subsys, priv->dvfs_bw.disp_level,
		priv->dvfs_bw.mml_level, priv->dvfs_bw.bw_level);
}
EXPORT_SYMBOL_GPL(dpc_dvfs_set);

void dpc_channel_bw_set_by_idx(struct device *dev, const enum mtk_dpc_subsys subsys,
			       const enum channel_type type, const u8 idx, const u32 bw_in_mb)
{
	struct mtk_dpc *priv = dev_get_drvdata(dev);
	u32 ch_bw = bw_in_mb;
	u32 cur_ch_bw;
	u32 max_ch_bw = 0;
	u8 index;
	int i;

	if (type == CHANNEL_HRT_READ)
		index = idx * 4 + 2;
	else if (type == CHANNEL_SRT_READ)
		index = idx * 4;
	else
		return;

	if (index >= DPC_CHANNEL_NUM)
		return;

	mutex_lock(&priv->dvfs_bw.lock);
	cur_ch_bw = chan_bw_cfg[index].disp_bw + chan_bw_cfg[index].mml_bw;

	if (subsys == DPC_SUBSYS_DISP)
		chan_bw_cfg[index].disp_bw = bw_in_mb;
	else
		chan_bw_cfg[index].mml_bw = bw_in_mb;

	ch_bw = chan_bw_cfg[index].disp_bw + chan_bw_cfg[index].mml_bw;
	mutex_unlock(&priv->dvfs_bw.lock);

	if (ch_bw == cur_ch_bw)
		return;

	dpc_ch_bw_set(dev, subsys, index, ch_bw);

	mutex_lock(&priv->dvfs_bw.lock);
	for (i = 0; i < DPC_CHANNEL_NUM; i++) {
		ch_bw = chan_bw_cfg[i].disp_bw + chan_bw_cfg[i].mml_bw;
		if (ch_bw > max_ch_bw)
			max_ch_bw = ch_bw;
	}
	priv->dvfs_bw.bw_level = bw_to_level(max_ch_bw);
	mutex_unlock(&priv->dvfs_bw.lock);
}
EXPORT_SYMBOL_GPL(dpc_channel_bw_set_by_idx);

static void dpc_disp_group_enable(struct device *dev)
{
	struct mtk_dpc *priv = dev_get_drvdata(dev);

	writel(DDRSRC_DISP_SW_CTRL | EMIREQ_DISP_SW_CTRL | DDRSRC_DISP_MUX | EMIREQ_DISP_MUX,
	       priv->regs + DISP_REG_DPC_DISP_DDRSRC_EMIREQ_CFG);
	writel(DPC_HRTBW_DISP_KEEP | DPC_SRTBW_DISP_KEEP,
	       priv->regs + DISP_REG_DPC_DISP_HRTBW_SRTBW_CFG);
	writel(1, priv->regs + DISP_REG_DPC_DISP_VDISP_DVFS_CFG);
	writel(MAINPLL_OFF_DISP_SW_CTRL_VAL | MMINFRA_OFF_DISP_SW_CTRL_VAL |
	       INFRA_OFF_DISP_SW_CTRL_VAL, priv->regs + DISP_REG_DPC_DISP_INFRA_PLL_OFF_CFG);
}

static void dpc_disp_group_disable(struct device *dev)
{
	struct mtk_dpc *priv = dev_get_drvdata(dev);

	writel(DDRSRC_DISP_MUX | EMIREQ_DISP_MUX, priv->regs + DISP_REG_DPC_DISP_DDRSRC_EMIREQ_CFG);
	writel(0, priv->regs + DISP_REG_DPC_DISP_HRTBW_SRTBW_CFG);
	writel(0, priv->regs + DISP_REG_DPC_DISP_VDISP_DVFS_CFG);
	writel(0, priv->regs + DISP_REG_DPC_DISP_INFRA_PLL_OFF_CFG);
}

void dpc_disable(struct device *dev, const enum mtk_dpc_subsys subsys)
{
	struct mtk_dpc *priv = dev_get_drvdata(dev);
	int i;

	if (WARN_ON(priv->ref_cnt[subsys] == 0))
		return;

	if (--priv->ref_cnt[subsys] != 0)
		return;

	for (i = 0; i < DPC_SUBSYS_CNT; i++) {
		if (priv->ref_cnt[i] != 0)
			return;
	}

	dpc_disp_group_disable(dev);

	writel(0, priv->regs + DISP_REG_DPC_ENABLE);
	writel(1, priv->regs + DISP_REG_DPC_RESET);
	writel(0, priv->regs + DISP_REG_DPC_RESET);
	priv->enabled = false;
	priv->dvfs_level = 0;
	dev_dbg(dev, "disable DPC");
}
EXPORT_SYMBOL_GPL(dpc_disable);

void dpc_enable(struct device *dev, const enum mtk_dpc_subsys subsys)
{
	struct mtk_dpc *priv = dev_get_drvdata(dev);

	if (priv->enabled) {
		priv->ref_cnt[subsys]++;
		return;
	}

	dpc_disp_group_enable(dev);

	writel(WLA_DDREN_ACK, priv->regs + DISP_REG_DPC_DDREN_ACK_SEL);
	writel(DPC_EN | DISP_DPC_MMQOS_ALWAYS_SCAN_EN | DPC_VDO_MODE,
	       priv->regs + DISP_REG_DPC_ENABLE);

	priv->ref_cnt[subsys]++;
	priv->enabled = true;
	dev_dbg(dev, "Enable DPC");
}
EXPORT_SYMBOL_GPL(dpc_enable);

int dpc_get_freq_step(struct device *dev)
{
	struct mtk_dpc *priv = dev_get_drvdata(dev);

	return priv->step_size;
}
EXPORT_SYMBOL_GPL(dpc_get_freq_step);

unsigned long dpc_get_freq_by_idx(struct device *dev, u32 index)
{
	struct mtk_dpc *priv = dev_get_drvdata(dev);

	return priv->freq_steps[index];
}
EXPORT_SYMBOL_GPL(dpc_get_freq_by_idx);

static int dpc_mmdvfs_get_avail_freq(struct device *dev)
{
	struct mtk_dpc *priv = dev_get_drvdata(dev);
	int i = 0;
	int ret;
	struct dev_pm_opp *opp;
	unsigned long freq;

	ret = dev_pm_opp_of_add_table(dev);
	if (ret) {
		dev_err(dev, "Failed to add OPP table");
		return ret;
	}

	priv->step_size = dev_pm_opp_get_opp_count(dev);
	if (priv->step_size <= 0) {
		dev_err(dev, "can't find opp table");
		return -EINVAL;
	}

	priv->freq_steps = devm_kcalloc(dev, priv->step_size, sizeof(unsigned long), GFP_KERNEL);
	if (!priv->freq_steps)
		return -ENOMEM;

	freq = 0;
	while (i < priv->step_size) {
		opp = dev_pm_opp_find_freq_ceil(dev, &freq);
		if (IS_ERR(opp))
			return -EINVAL;

		priv->freq_steps[i] = freq;
		freq++;
		i++;
		dev_pm_opp_put(opp);
	}

	return 0;
}

static const struct of_device_id mtk_dpc_driver_dt_match[] = {
	{.compatible = "mediatek,mt8196-disp-dpc"},
	{},
};
MODULE_DEVICE_TABLE(of, mtk_dpc_driver_dt_match);

static int mtk_dpc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_dpc *priv;
	struct resource *res;
	int ret;

	dev_dbg(dev, "%s probe start", __func__);
	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->regs)) {
		dev_err(dev, "failed to ioremap exdma\n");
		ret = PTR_ERR(priv->regs);
		goto free_priv;
	}

	mutex_init(&priv->dvfs_bw.lock);
	platform_set_drvdata(pdev, priv);
	if (dpc_mmdvfs_get_avail_freq(dev) < 0) {
		dev_err(dev, "fail to get opp table");
		ret = -EINVAL;
		goto free_priv;
	}
	pm_runtime_enable(dev);

	return 0;

free_priv:
	devm_kfree(dev, priv);
	return ret;
}

static int mtk_dpc_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
	return 0;
}

struct platform_driver mtk_dpc_driver = {
	.probe = mtk_dpc_probe,
	.remove = mtk_dpc_remove,
	.driver = {
		.name = "mediatek-disp-dpc",
		.owner = THIS_MODULE,
		.of_match_table = mtk_dpc_driver_dt_match,
	},
};
module_platform_driver(mtk_dpc_driver);

MODULE_AUTHOR("Nancy Lin <nancy.lin@mediatek.com>");
MODULE_DESCRIPTION("MediaTek DPC Driver");
MODULE_LICENSE("GPL");
