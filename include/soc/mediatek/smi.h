/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015-2016 MediaTek Inc.
 * Author: Yong Wu <yong.wu@mediatek.com>
 */
#ifndef MTK_IOMMU_SMI_H
#define MTK_IOMMU_SMI_H

#include <linux/bitops.h>
#include <linux/device.h>

#if IS_ENABLED(CONFIG_MTK_SMI)

enum iommu_atf_cmd {
	IOMMU_ATF_CMD_CONFIG_SMI_LARB,		/* For mm master to en/disable iommu */
	IOMMU_ATF_CMD_CONFIG_INFRA_IOMMU,	/* For infra master to enable iommu */
	IOMMU_ATF_CMD_MAX,
};

#define MTK_SMI_MMU_EN(port)	BIT(port)

struct mtk_smi_larb_iommu {
	struct device *dev;
	unsigned int   mmu;
	unsigned char  bank[32];
};

/* 0: successful; Others: fail. */
int mtk_smi_larb_bw_set(struct device *larbdev, const u8 port, const u8 val);
int mtk_smi_common_bw_set(struct device *larbdev, const u8 larbid, bool hard_limit, const u32 grant_val);
int mtk_smi_common_ostdl_set(struct device *larbdev, const u8 larbid, bool is_write, const u32 limit_val);
int mtk_smi_set_hrt_perm(struct device *larbdev, const u8 port, bool is_hrt);

#endif

#endif
