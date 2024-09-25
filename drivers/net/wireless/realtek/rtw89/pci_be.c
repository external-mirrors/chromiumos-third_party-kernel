// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2023  Realtek Corporation
 */

#include <linux/pci.h>

#include "pci.h"
#include "reg.h"

static int __maybe_unused rtw89_pci_suspend_be(struct device *dev)
{
	struct ieee80211_hw *hw = dev_get_drvdata(dev);
	struct rtw89_dev *rtwdev = hw->priv;

	rtw89_write32_set(rtwdev, R_BE_RSV_CTRL, B_BE_WLOCK_1C_BIT6);
	rtw89_write32_set(rtwdev, R_BE_RSV_CTRL, B_BE_R_DIS_PRST);
	rtw89_write32_clr(rtwdev, R_BE_RSV_CTRL, B_BE_WLOCK_1C_BIT6);
	rtw89_write32_set(rtwdev, R_BE_PCIE_FRZ_CLK, B_BE_PCIE_FRZ_REG_RST);
	rtw89_write32_clr(rtwdev, R_BE_REG_PL1_MASK, B_BE_SER_PM_MASTER_IMR);
	return 0;
}

static int __maybe_unused rtw89_pci_resume_be(struct device *dev)
{
	struct ieee80211_hw *hw = dev_get_drvdata(dev);
	struct rtw89_dev *rtwdev = hw->priv;
	u32 polling;
	int ret;

	rtw89_write32_set(rtwdev, R_BE_RSV_CTRL, B_BE_WLOCK_1C_BIT6);
	rtw89_write32_clr(rtwdev, R_BE_RSV_CTRL, B_BE_R_DIS_PRST);
	rtw89_write32_clr(rtwdev, R_BE_RSV_CTRL, B_BE_WLOCK_1C_BIT6);
	rtw89_write32_clr(rtwdev, R_BE_PCIE_FRZ_CLK, B_BE_PCIE_FRZ_REG_RST);
	rtw89_write32_clr(rtwdev, R_BE_SER_PL1_CTRL, B_BE_PL1_SER_PL1_EN);

	ret = read_poll_timeout_atomic(rtw89_read32, polling, !polling, 1, 1000,
				       false, rtwdev, R_BE_REG_PL1_ISR);
	if (ret)
		rtw89_warn(rtwdev, "[ERR] PCIE SER clear polling fail\n");

	rtw89_write32_set(rtwdev, R_BE_SER_PL1_CTRL, B_BE_PL1_SER_PL1_EN);
	rtw89_write32_set(rtwdev, R_BE_REG_PL1_MASK, B_BE_SER_PM_MASTER_IMR);

	rtw89_pci_basic_cfg(rtwdev, true);

	return 0;
}

SIMPLE_DEV_PM_OPS(rtw89_pm_ops_be, rtw89_pci_suspend_be, rtw89_pci_resume_be);
EXPORT_SYMBOL(rtw89_pm_ops_be);

const struct rtw89_pci_gen_def rtw89_pci_gen_be = {
};
EXPORT_SYMBOL(rtw89_pci_gen_be);
