// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024 MediaTek Inc.
 */

#include <linux/module.h>
#include <linux/soc/mediatek/mtk-dpc.h>
#include "mtk_disp_pmqos.h"
#include "mtk_drm_drv.h"

struct mtk_disp_pmqos {
#if IS_REACHABLE(CONFIG_MTK_DPC)
	struct device *dpc;
#endif
	unsigned long *freq_steps;
	int freq_level[MAX_CRTC];
	int step_size;
	unsigned int req_hrt[MAX_CRTC];
	unsigned int req_srt[MAX_CRTC];
	unsigned int req_hrt_channel_bw[MAX_CRTC][BW_CHANNEL_NR];
	unsigned int req_srt_channel_bw[MAX_CRTC][BW_CHANNEL_NR];
};

void mtk_disp_pmqos_set_module_srt(struct icc_path *request, struct device *dev,
				   unsigned int bandwidth)
{
#if IS_REACHABLE(CONFIG_INTERCONNECT_MTK_EXTENSION)
	mtk_icc_set_bw(request, MBps_to_icc(bandwidth), 0);
	dev_dbg(dev, "Set srt module bw: %d\n", bandwidth);
#endif
}

void mtk_disp_pmqos_set_module_hrt(struct icc_path *request, struct device *dev,
				   unsigned int bandwidth)
{
#if IS_REACHABLE(CONFIG_INTERCONNECT_MTK_EXTENSION)
	mtk_icc_set_bw(request, 0, MBps_to_icc(bandwidth));
	dev_dbg(dev, "Set peak module bw: %d\n", bandwidth);
#endif
}

void mtk_disp_pmqos_set_channel_hrt_bw(struct device *dev, int crtc_id, unsigned int bw, int i)
{
#if IS_REACHABLE(CONFIG_MTK_DPC)
	struct mtk_disp_pmqos *priv = dev_get_drvdata(dev);
	unsigned int total = 0;
	int j;

	if (!priv->dpc)
		return;

	if (priv->req_hrt_channel_bw[crtc_id][i] == bw)
		return;

	priv->req_hrt_channel_bw[crtc_id][i] = bw;

	for (j = 0; j < MAX_CRTC; j++)
		total += priv->req_hrt_channel_bw[j][i];

	dpc_channel_bw_set_by_idx(priv->dpc, DPC_SUBSYS_DISP, CHANNEL_HRT_READ, i, total);

	dev_dbg(dev, "%s, CRTC%d chan[%d] bw=%u, total=%u\n",
		__func__, crtc_id, i, bw, total);
#endif
}

void mtk_disp_pmqos_set_channel_srt_bw(struct device *dev, int crtc_id, unsigned int bw, int i)
{
#if IS_REACHABLE(CONFIG_MTK_DPC)
	struct mtk_disp_pmqos *priv = dev_get_drvdata(dev);
	unsigned int total = 0;
	int j;

	if (!priv->dpc)
		return;

	priv->req_srt_channel_bw[crtc_id][i] = bw;
	for (j = 0; j < MAX_CRTC; j++)
		total += priv->req_srt_channel_bw[j][i];

	dpc_channel_bw_set_by_idx(priv->dpc, DPC_SUBSYS_DISP, CHANNEL_SRT_READ, i, total);
	dev_dbg(dev, "%s, CRTC%d chan[%d] srt bw=%u, total=%u\n",
		__func__, crtc_id, i, bw, total);
#endif
}

void mtk_disp_pmqos_clear_channel_srt_bw(struct device *dev, int crtc_id)
{
	struct mtk_disp_pmqos *priv = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < BW_CHANNEL_NR; i++)
		priv->req_srt_channel_bw[crtc_id][i] = 0;
}

void mtk_disp_pmqos_set_srt_bw(struct device *dev, int crtc_id, unsigned int bw)
{
#if IS_REACHABLE(CONFIG_MTK_DPC)
	struct mtk_disp_pmqos *priv = dev_get_drvdata(dev);
	unsigned int total = 0;

	if (!priv->dpc)
		return;

	if (priv->req_srt[crtc_id] == bw)
		return;

	priv->req_srt[crtc_id] = bw;
	for (int i = 0; i < MAX_CRTC; i++)
		total += priv->req_srt[i];

	dpc_srt_bw_set(priv->dpc, DPC_SUBSYS_DISP, total);
	dev_dbg(dev, "set CRTC %d SRT bw: %uMB, total: %uMB\n", crtc_id, bw, total);
#endif
}

void mtk_disp_pmqos_set_hrt_bw(struct device *dev, int crtc_id, unsigned int bw)
{
#if IS_REACHABLE(CONFIG_MTK_DPC)
	struct mtk_disp_pmqos *priv = dev_get_drvdata(dev);
	unsigned int total = 0;
	int i;

	if (!priv->dpc)
		return;
	if (priv->req_hrt[crtc_id] == bw)
		return;

	priv->req_hrt[crtc_id] = bw;

	for (i = 0; i < MAX_CRTC; ++i)
		total += priv->req_hrt[i];

	dpc_hrt_bw_set(priv->dpc, DPC_SUBSYS_DISP, total);
	dev_dbg(dev, "set CRTC %d HRT bw: %uMB, total: %uMB\n", crtc_id, bw, total);
#endif
}

void mtk_disp_pmqos_mmdvfs_init(struct device *dev, struct device *dpc_dev)
{
#if IS_REACHABLE(CONFIG_MTK_DPC)
	struct mtk_disp_pmqos *priv = dev_get_drvdata(dev);
	int i;

	priv->dpc = dpc_dev;
	priv->step_size = dpc_get_freq_step(priv->dpc);
	if (priv->step_size > 0) {
		priv->freq_steps = kcalloc(priv->step_size, sizeof(unsigned long), GFP_KERNEL);
		if (!priv->freq_steps)
			return;

		for (i = 0; i < priv->step_size; i++)
			priv->freq_steps[i] = dpc_get_freq_by_idx(priv->dpc, i);
	}
#endif
}

static void mtk_disp_pmqos_set_mmclk(struct device *dev, int crtc_id, int level,
				     const char *caller)
{
#if IS_REACHABLE(CONFIG_MTK_DPC)
	struct mtk_disp_pmqos *priv = dev_get_drvdata(dev);
	unsigned long freq;
	int i;
	int final_level = -1;

	if (!priv->dpc)
		return;

	dev_dbg(dev, "%s[%d] freq_level[%d]: %d\n",
		__func__, __LINE__, crtc_id, level);

	if (level < 0 || level > (priv->step_size - 1))
		level = -1;

	if (level == priv->freq_level[crtc_id])
		return;

	priv->freq_level[crtc_id] = level;

	for (i = 0; i < MAX_CRTC; i++)
		if (priv->freq_level[i] > final_level)
			final_level = priv->freq_level[i];

	if (final_level >= 0) {
		freq = priv->freq_steps[final_level];
	} else {
		freq = priv->freq_steps[0];
		final_level = 0;
	}

	dev_info(dev, "%s[%d] final_level(freq=%d, %lu)\n",
		 __func__, __LINE__, final_level, freq);

	dpc_dvfs_set(priv->dpc, DPC_SUBSYS_DISP, final_level, true);
#endif
}

void mtk_disp_pmqos_set_mmclk_by_pixclk(struct device *dev, int crtc_id, unsigned int pixclk,
					const char *caller)
{
	struct mtk_disp_pmqos *priv = dev_get_drvdata(dev);
	unsigned long freq = pixclk * 1000000;
	int i;

	if (freq > priv->freq_steps[priv->step_size - 1]) {
		dev_dbg(dev, "%s:pixleclk (%lu) is to big for mmclk (%lu)\n",
			caller, freq, priv->freq_steps[priv->step_size - 1]);
		mtk_disp_pmqos_set_mmclk(dev, crtc_id, priv->step_size - 1, caller);
		return;
	}
	if (freq == 0) {
		/* reset level for current crtc */
		mtk_disp_pmqos_set_mmclk(dev, crtc_id, -1, caller);
		return;
	}

	for (i = priv->step_size - 2 ; i >= 0; i--) {
		if (freq > priv->freq_steps[i]) {
			mtk_disp_pmqos_set_mmclk(dev, crtc_id, i + 1, caller);
			return;
		}
	}
	mtk_disp_pmqos_set_mmclk(dev, crtc_id, 0, caller);
}

static int mtk_disp_pmqos_probe(struct platform_device *pdev)
{
	struct mtk_disp_pmqos *priv;
	struct device *dev = &pdev->dev;
	int i;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->step_size = 1;
	for (i = 0; i < MAX_CRTC; i++)
		priv->freq_level[i] = -1;

	platform_set_drvdata(pdev, priv);

	return 0;
}

static int mtk_disp_pmqos_remove(struct platform_device *pdev)
{
	return 0;
}

struct platform_driver mtk_disp_pmqos_driver = {
	.probe		= mtk_disp_pmqos_probe,
	.remove		= mtk_disp_pmqos_remove,
	.driver		= {
		.name	= "mediatek-disp-pmqos",
		.owner	= THIS_MODULE,
	},
};

MODULE_AUTHOR("Nancy Lin <nancy.lin@mediatek.com>");
MODULE_DESCRIPTION("MediaTek Display QoS Driver");
MODULE_LICENSE("GPL");
