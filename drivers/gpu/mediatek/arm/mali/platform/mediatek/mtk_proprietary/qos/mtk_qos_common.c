// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2025 MediaTek Inc.
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include "mtk_qos_common.h"
#include "mtk_qos_ipi.h"

static struct mtk_qos *qos;

struct mtk_qos *qos_get_handle(void)
{
	return qos;
}

int mtk_qos_probe(struct platform_device *pdev)
{
	int ret;
	struct device_node *node = pdev->dev.of_node;
	bool qos_enable;

	if (!pdev)
		return -EINVAL;

	qos = devm_kzalloc(&pdev->dev, sizeof(*qos), GFP_KERNEL);
	if (!qos)
		return -ENOMEM;

	qos->dev = &pdev->dev;
	mutex_init(&qos->qos_ipi_mutex);

	qos->soc = of_device_get_match_data(&pdev->dev);
	if (!qos->soc)
		return -EINVAL;

	qos_enable = of_property_read_bool(node, "mediatek,enable");
	if (!qos_enable) {
		dev_err(qos->dev, "@%s: gpu qos is not enable", __func__);
		return -EINVAL;
	}

	ret = qos_ipi_init(qos);
	platform_set_drvdata(pdev, qos);
	dev_info(qos->dev, "@%s: done (qos_ipi_init: %d)", __func__, ret);
	return ret;
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MediaTek GPU QoS driver");
