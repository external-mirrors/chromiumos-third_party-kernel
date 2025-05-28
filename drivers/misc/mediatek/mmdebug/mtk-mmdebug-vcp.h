/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2025 MediaTek Inc.
 */

#ifndef _MTK_MMDEBUG_VCP_H_
#define _MTK_MMDEBUG_VCP_H_

#define MMDEBUG_DBG(dev, fmt, args...) \
	dev_dbg(dev, "[mmdebug][dbg]%s: " fmt "\n", __func__, ##args)
#define MMDEBUG_ERR(dev, fmt, args...) \
	dev_err(dev, "[mmdebug][err]%s: " fmt "\n", __func__, ##args)

enum MMDEBUG_FUNC {
	MMDEBUG_FUNC_SMI_DUMP,
	MMDEBUG_FUNC_KERNEL_WARN,
	MMDEBUG_FUNC_NUM
};

struct mmdebug_ipi_data {
	u8 func;
	u8 idx;
	u8 ack;
	u32 base;
};

struct mmdebug_dev {
	struct platform_device *pdev;
	bool mmdebug_init_done;
	bool mmdebug_ena;
	struct mtk_vcp_device  *vcp_device;
	struct mmdebug_ipi_data mmdebug_vcp_ipi_data;
	struct workqueue_struct *mmdebug_workq;
	struct work_struct mmdebug_work;
	struct raw_notifier_head mmdebug_status_dump_notifier_list;
};

static inline struct mmdebug_dev *get_mmdebug_dev(void)
{
	struct platform_device *pdev;
	struct device_node *node;

	node = of_find_compatible_node(NULL, NULL, "mediatek,mmdebug-vcp");
	if (!node)
		return NULL;

	pdev = of_find_device_by_node(node);
	if (!pdev)
		return NULL;

	return platform_get_drvdata(pdev);
}
#endif

