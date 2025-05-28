// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2025 MediaTek Inc.
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include "mtk-mmdvfs-v3.h"

struct mmdvfs_user_dev {
	struct platform_device *pdev;
	struct clk *mmdvfs_user_clk[MMDVFS_USER_NUM];
};

static inline struct mmdvfs_user_dev *get_mmdvfs_user_dev(void)
{
	struct platform_device *pdev;
	struct device_node *node;

	node = of_find_compatible_node(NULL, NULL, "mtk-mmdvfs-user");
	if (!node)
		return NULL;

	pdev = of_find_device_by_node(node);
	if (!pdev)
		return NULL;

	return platform_get_drvdata(pdev);
}

int mmdvfs_user_set_rate(u8 idx, u64 freq)
{
	struct mmdvfs_user_dev *mmdvfs_user = get_mmdvfs_user_dev();
	int ret;

	if (!mmdvfs_user)
		return -ENODEV;

	ret = clk_set_rate(mmdvfs_user->mmdvfs_user_clk[idx], freq);
	return ret;
}
EXPORT_SYMBOL_GPL(mmdvfs_user_set_rate);

static int mmdvfs_user_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct mmdvfs_user_dev *mmdvfs_user;
	struct mtk_mmdvfs_dev *mmdvfs_dev;
	struct platform_device *mux_pdev;
	struct device *dev = &pdev->dev;
	struct device_node *mux_node;
	struct clk_bulk_data *clks;
	int i, j, clk_num, ret;
	const char *clk_name;

	mmdvfs_user = devm_kzalloc(&pdev->dev, sizeof(struct mmdvfs_user_dev), GFP_KERNEL);
	if (!mmdvfs_user)
		return -ENOMEM;

	mux_node = of_parse_phandle(dev->of_node, "mediatek,mmdvfs-mux", 0);
	if (!mux_node) {
		dev_err(dev, "can't get mmdvfs mux node.\n");
		return -EINVAL;
	}

	mux_pdev = of_find_device_by_node(mux_node);
	of_node_put(mux_node);
	if (WARN_ON(!mux_pdev)) {
		dev_err(dev, "mux pdev failed\n");
		return -EINVAL;
	}

	mmdvfs_dev = platform_get_drvdata(mux_pdev);
	if (!mmdvfs_dev) {
		dev_err(dev, "can't get mmdvfs mux device data.\n");
		return -EPROBE_DEFER;
	}

	clk_num = of_property_count_strings(node, "clock-names");
	if (clk_num <= 0) {
		dev_err(dev, "clock-names invalid:%d", clk_num);
		return -EINVAL;
	}

	clks = devm_kcalloc(&pdev->dev, clk_num, sizeof(*clks), GFP_KERNEL);
	if (!clks)
		return -ENOMEM;


	for (i = 0; i < clk_num; i++) {
		ret = of_property_read_string_index(node, "clock-names", i, &clk_name);
		if (ret) {
			dev_err(dev, "failed to read clock name at index %d: %d", i, ret);
			return ret;
		}
		clks[i].id = clk_name;
	}

	ret = devm_clk_bulk_get(&pdev->dev, clk_num, clks);
	if (ret) {
		dev_err(&pdev->dev, "devm_clk_get (%d)%s fail", i, clk_name);
		return ret;
	}

	for (i = 0; i < clk_num; i++) {
		clk_name = clks[i].id;
		for (j = 0; j < MMDVFS_USER_NUM; j++) {
			if (!strncmp(mmdvfs_dev->mmdvfs_user[j].name, clk_name, 16)) {
				mmdvfs_user->mmdvfs_user_clk[j] = clks[i].clk;
				dev_dbg(&pdev->dev, "user: %d name: %12s", j, clk_name);
				break;
			}
		}
	}

	mmdvfs_user->pdev = pdev;
	platform_set_drvdata(pdev, mmdvfs_user);

	return 0;
}

static const struct of_device_id of_match_mmdvfs_user[] = {
	{
		.compatible = "mediatek,mtk-mmdvfs-user",
	},
	{}
};

static struct platform_driver mmdvfs_user_drv = {
	.probe = mmdvfs_user_probe,
	.driver = {
		.name = "mtk-mmdvfs-user",
		.of_match_table = of_match_mmdvfs_user,
	},
};

module_platform_driver(mmdvfs_user_drv)

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MediaTek MMDVFS User");
MODULE_AUTHOR("MediaTek Inc.");
