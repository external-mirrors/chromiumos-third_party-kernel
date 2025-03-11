/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2025 MediaTek Inc.
 * Author: Yunfei Dong <yunfei.dong@mediatek.com>
 */

#ifndef _MTK_VMM_DRV_H_
#define _MTK_VMM_DRV_H_

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kthread.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#if IS_ENABLED(CONFIG_MTK_VCP_RPROC)
#include <linux/remoteproc.h>
#include <linux/remoteproc/mtk_vcp_public.h>
#endif

#define mtk_vmm_dbg(dev, fmt, args...) \
	dev_dbg(dev, "[vmm] %s(%d): " fmt "\n", __func__, __LINE__, ##args)

#define mtk_vmm_err(dev, fmt, args...) \
	dev_err(dev, "[vmm] err %s(%d): " fmt "\n", __func__, __LINE__, ##args)

#define HW_CCF_AP_VOTER_BIT         (1)
#define HW_CCF_BACKUP2_DONE         (0x144c)
#define HW_CCF_XPU0_BACKUP2_SET     (0x238)
#define HW_CCF_BACKUP2_SET_STATUS   (0x148c)
#define HW_CCF_XPU0_BACKUP2_CLR     (0x23c)
#define HW_CCF_BACKUP2_CLR_STATUS   (0x1490)
#define HW_CCF_BACKUP2_ENABLE       (0x1440)
#define HW_CCF_BACKUP2_STATUS       (0x1444)
#define HW_CCF_BACKUP2_STATUS_DBG   (0x1448)

struct mtk_vmm_drv {
	struct mtk_vcp_device  *vcp_device;
	struct notifier_block vcp_nb;
	struct regmap          *hwccf_regmap;
	struct mutex           ctrl_mutex;
	struct device          *dev;
	struct task_struct     *kthr_vcp;
	phandle                vcp_phandle;
	bool                   vcp_is_active;
	bool                   need_update;
};

#endif /* _MTK_VMM_DRV_H_ */
