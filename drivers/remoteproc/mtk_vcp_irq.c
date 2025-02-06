// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 MediaTek Inc.
 */

#include <linux/interrupt.h>

#include "mtk_vcp_reg.h"
#include "mtk_vcp_rproc.h"

static inline void vcp_wdt_clear(struct mtk_vcp_device *vcp,
				 enum vcp_core_id core_id)
{
	core_id == VCP_ID ?
		   writel(B_WDT_IRQ, vcp->vcp_cluster->cfg_core + R_CORE0_WDT_IRQ) :
		   writel(B_WDT_IRQ, vcp->vcp_cluster->cfg_core + R_CORE1_WDT_IRQ);
}

/*
 * handler for wdt irq for vcp
 */
static irqreturn_t vcp_wdt_handler(struct mtk_vcp_device *vcp)
{
	u32 reg0;
	u32 reg1;

	reg0 = readl(vcp->vcp_cluster->cfg_core + R_CORE0_WDT_IRQ);
	reg1 = vcp->vcp_cluster->core_nums > VCP_ID ?
	       readl(vcp->vcp_cluster->cfg_core + R_CORE1_WDT_IRQ) : 0;

	if (reg0)
		vcp_wdt_clear(vcp, VCP_ID);
	if (reg1)
		vcp_wdt_clear(vcp, MMUP_ID);

	if (reg0 || reg1)
		return IRQ_HANDLED;
	return IRQ_NONE;
}

/*
 * dispatch vcp irq
 * reset vcp and generate exception if needed
 * @param irq:   irq id
 * @param pri:   struct mtk_vcp_device
 */
static irqreturn_t vcp_irq_handler(int irq, void *priv)
{
	struct mtk_vcp_device *vcp = priv;

	disable_irq_nosync(irq);
	return vcp_wdt_handler(vcp);
}

int vcp_wdt_irq_init(struct mtk_vcp_device *vcp)
{
	int ret;

	ret = devm_request_irq(vcp->dev, platform_get_irq_byname(vcp->pdev, "wdt"),
			       vcp_irq_handler, IRQF_ONESHOT,
			       vcp->pdev->name, vcp);
	if (ret) {
		dev_err(vcp->dev, "failed to request wdt irq\n");
		return ret;
	}

	return ret;
}
