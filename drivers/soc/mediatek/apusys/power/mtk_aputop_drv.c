// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024 MediaTek Inc.
 */

#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/soc/mediatek/mtk_apu_pwr.h>
#include "mtk_apu_top.h"

static int mtk_apu_top_probe(struct platform_device *pdev)
{
	const struct apupwr_plat_data *pwr_data = of_device_get_match_data(&pdev->dev);

	dev_dbg(&pdev->dev, "%s ++\n", __func__);

	if (!pwr_data)
		return dev_err_probe(&pdev->dev, -ENODEV, "no platform data found\n");

	devm_pm_runtime_enable(&pdev->dev);
	dev_dbg(&pdev->dev, "%s pm_runtime_enable done\n", __func__);

	return pwr_data->probe(pdev);
}

static void mtk_apu_top_remove(struct platform_device *pdev)
{
	const struct apupwr_plat_data *pwr_data = of_device_get_match_data(&pdev->dev);

	if (pwr_data->remove)
		pwr_data->remove(pdev);
}

static int aputop_pwr_on_rpm_cb(struct device *dev)
{
	int ret = 0;
	const struct apupwr_plat_data *pwr_data = of_device_get_match_data(dev);

	ret =  pwr_data->plat_aputop_on(dev);
	return ret;
}

static int aputop_pwr_off_rpm_cb(struct device *dev)
{
	int ret = 0;
	const struct apupwr_plat_data *pwr_data = of_device_get_match_data(dev);

	ret = pwr_data->plat_aputop_off(dev);
	return ret;
}

static const struct of_device_id of_match_mtk_apu_top[] = {
	{ .compatible = "mt8196,apu-top-3", .data = &mt8196_plat_data },
	{},
};
MODULE_DEVICE_TABLE(of, of_match_mtk_apu_top);

static const struct dev_pm_ops mtk_aputop_pm_ops = {
	SET_RUNTIME_PM_OPS(aputop_pwr_off_rpm_cb, aputop_pwr_on_rpm_cb, NULL)
};

static struct platform_driver mtk_apu_top_drv = {
	.probe = mtk_apu_top_probe,
	// TODO: will change the name to ".remove" when sending upstream
	.remove_new = mtk_apu_top_remove,
	.driver = {
		.name = "apu_top_3",
		.pm = &mtk_aputop_pm_ops,
		.of_match_table = of_match_mtk_apu_top,
	},
};
module_platform_driver(mtk_apu_top_drv);

int mtk_apu_get_rpc_status(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct apupwr_plat_data *pwr_data;

	if (dev->driver != &mtk_apu_top_drv.driver) {
		dev_err(dev, "Device not supported by MTK APU TOP driver\n");
		return -EINVAL;
	}

	pwr_data = of_device_get_match_data(dev);
	if (!pwr_data->get_rpc_status) {
		dev_err(dev, "No get_rpc_status function defined for this platform\n");
		return -EINVAL;
	}

	return pwr_data->get_rpc_status(pdev);
}
EXPORT_SYMBOL_NS(mtk_apu_get_rpc_status, MTK_APU_PWR);

int mtk_apu_get_rpc_pwr_status(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct apupwr_plat_data *pwr_data;

	if (dev->driver != &mtk_apu_top_drv.driver) {
		dev_err(dev, "Device not supported by MTK APU TOP driver\n");
		return -EINVAL;
	}

	pwr_data = of_device_get_match_data(dev);

	if (!pwr_data->get_rpc_pwr_status) {
		dev_err(dev, "No get_rpc_pwr_status function defined for this platform\n");
		return -EINVAL;
	}

	return pwr_data->get_rpc_pwr_status(pdev);
}
EXPORT_SYMBOL_NS(mtk_apu_get_rpc_pwr_status, MTK_APU_PWR);

MODULE_SOFTDEP("pre: mtk-apu-mailbox");

MODULE_DESCRIPTION("MediaTek APU Power Driver");
MODULE_LICENSE("GPL");
