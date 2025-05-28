// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2025 MediaTek Inc.
 */

#include <linux/interconnect.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include "dvfsrc-helper.h"

static void mtk_dvfsrc_get_perf_bw(struct mtk_dvfsrc *dvfsrc,
				   struct device_node *np)
{
	int i;

	for (i = 0; i < dvfsrc->num_perf; i++) {
		dvfsrc->perfs_peak_bw[i] =
			dvfsrc_get_required_opp_peak_bw(np, i);
	}
}

static int mtk_dvfsrc_debug_setting(struct mtk_dvfsrc *dvfsrc)
{
	struct device_node *np = dvfsrc->dev->of_node;

	dvfsrc->num_perf =
		of_count_phandle_with_args(np, "required-opps", NULL);

	if (dvfsrc->num_perf > 0) {
		dvfsrc->perfs_peak_bw = devm_kzalloc(
			dvfsrc->dev, dvfsrc->num_perf * sizeof(u32),
			GFP_KERNEL);

		if (!dvfsrc->perfs_peak_bw) {
			dev_err(dvfsrc->dev, "allocate memory failed\n");
			return -ENOMEM;
		}

		mtk_dvfsrc_get_perf_bw(dvfsrc, np);
	} else {
		dvfsrc->num_perf = 0;
	}

	dvfsrc->bw_path = devm_of_icc_get(dvfsrc->dev, "icc-bw");
	if (IS_ERR(dvfsrc->bw_path)) {
		dev_info(dvfsrc->dev, "get icc-bw failed = %ld\n",
			 PTR_ERR(dvfsrc->bw_path));
		dvfsrc->bw_path = NULL;
	}

	dvfsrc->hrt_path = devm_of_icc_get(dvfsrc->dev, "icc-hrt-bw");
	if (IS_ERR(dvfsrc->hrt_path)) {
		dev_info(dvfsrc->dev, "get icc-hrt-bw failed = %ld\n",
			 PTR_ERR(dvfsrc->hrt_path));
		dvfsrc->hrt_path = NULL;
	}

	dvfsrc->perf_path = devm_of_icc_get(dvfsrc->dev, "icc-perf-bw");
	if (IS_ERR(dvfsrc->perf_path)) {
		dev_info(dvfsrc->dev, "get icc-perf-bw failed = %ld\n",
			 PTR_ERR(dvfsrc->perf_path));
		dvfsrc->perf_path = NULL;
	}
	return 0;
}

static int mtk_dvfsrc_helper_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_dvfsrc *dvfsrc;
	int ret;

	dvfsrc = devm_kzalloc(&pdev->dev, sizeof(*dvfsrc), GFP_KERNEL);
	if (!dvfsrc)
		return -ENOMEM;

	dvfsrc->dev = &pdev->dev;

	ret = mtk_dvfsrc_debug_setting(dvfsrc);
	if (ret) {
		dev_err(dev, "dvfsrc debug setting fail\n");
		return ret;
	}
	platform_set_drvdata(pdev, dvfsrc);
	ret = dvfsrc_register_sysfs(dev);
	if (ret) {
		dev_err(dev, "dvfsrc register sysfs fail\n");
		return ret;
	}
	return 0;
}

static int mtk_dvfsrc_helper_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	dvfsrc_unregister_sysfs(dev);
	return 0;
}

static const struct of_device_id mtk_dvfsrc_helper_of_match[] = {
	{
		.compatible = "mediatek,mt8196-dvfsrc-helper",
	},
	{
		/* sentinel */
	},
};
MODULE_DEVICE_TABLE(of, mtk_dvfsrc_helper_of_match);

static struct platform_driver mtk_dvfsrc_helper_driver = {
	.probe	= mtk_dvfsrc_helper_probe,
	.remove	= mtk_dvfsrc_helper_remove,
	.driver = {
		.name = "mtk-dvfsrc-helper",
		.of_match_table = of_match_ptr(mtk_dvfsrc_helper_of_match),
	},
};

module_platform_driver(mtk_dvfsrc_helper_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MTK DVFSRC helper driver");
