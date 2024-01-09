// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2022 Google LLC.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include "dprx.h"

#define DPRX_IRQ_MASK	0x8
#define DPRX_IRQ_CLR	0xc
#define DPRX_IRQ_AUX 	0x1

static irqreturn_t dprx_dp_isr(int irq, void *data)
{
	struct dprx_dp *dp = data;
	unsigned int reg;
	struct aux_msg request;
	struct aux_msg response;

	reg = readl(dp->iobase_irq + DPRX_IRQ_CLR);
	if (!reg)
		return IRQ_NONE;
	if (dprx_aux_read_request(dp, &request)) {
		dprx_aux_handle_request(dp, &request, &response);
		dprx_aux_write_response(dp, &response);
	}
	writel(reg, dp->iobase_irq + DPRX_IRQ_CLR);
	return IRQ_HANDLED;
}

static void dprx_sink_init(struct dprx_dp *dp)
{
	int i;

	for (i = 0; i < 4; i++) {
		memcpy(dp->sinks[i].edid, default_edid, 128 * default_edid_blocks);
		dp->sinks[i].blocks = default_edid_blocks;
	}
}

int dprx_dp_init(struct dprx_dp *dp, struct device *dev,
		 const struct dprx_dp_cfg *cfg)
{
	struct platform_device *pdev = to_platform_device(dev);
	int irq;
	int res;

	dp->dev = &pdev->dev;

	dp->iobase = devm_platform_ioremap_resource_byname(pdev, cfg->reg_core);
	if (IS_ERR(dp->iobase))
		return PTR_ERR(dp->iobase);

	dp->iobase_irq = devm_platform_ioremap_resource_byname(pdev, cfg->reg_irq);
	if (IS_ERR(dp->iobase_irq))
		return PTR_ERR(dp->iobase_irq);

	irq = platform_get_irq_byname(pdev, cfg->irq);
	if (irq < 0)
		return irq;

	res = devm_request_irq(dp->dev, irq, dprx_dp_isr, 0, cfg->irq, dp);
	if (res)
		return res;

	writel(DPRX_IRQ_AUX, dp->iobase_irq + DPRX_IRQ_MASK);

	dp->has_mst = cfg->has_mst;
	dp->sink_count = cfg->sink_count;

	dprx_dprx_init(dp);
	dprx_dpcd_init(dp);
	dprx_sink_init(dp);

	dprx_dprx_set_hpd(dp, 1);

	return 0;
}
