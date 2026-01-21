// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 * Author: jitao.shi <jitao.shi@mediatek.com>
 */

#include "phy-mtk-io.h"
#include "phy-mtk-mipi-dsi.h"

#define MIPITX_LANE_CON		0x000c
#define RG_DSI_CPHY_T1DRV_EN		BIT(0)
#define RG_DSI_ANA_CK_SEL		BIT(1)
#define RG_DSI_PHY_CK_SEL		BIT(2)
#define RG_DSI_CPHY_EN			BIT(3)
#define RG_DSI_PHYCK_INV_EN		BIT(4)
#define RG_DSI_PWR04_EN			BIT(5)
#define RG_DSI_BG_LPF_EN		BIT(6)
#define RG_DSI_BG_CORE_EN		BIT(7)
#define RG_DSI_PAD_TIEL_SEL		BIT(8)
#define RG_DSI_PRE_EMPHASIS_EN		BIT(9)

#define MIPITX_VOLTAGE_SEL	0x0010
#define RG_DSI_HSTX_LDO_REF_SEL		GENMASK(9, 6)

#define MIPITX_PLL_PWR		0x0028
#define MIPITX_PLL_CON0		0x002c
#define MIPITX_PLL_CON1		0x0030
#define MIPITX_PLL_CON2		0x0034
#define MIPITX_PLL_CON3		0x0038
#define MIPITX_PLL_CON4		0x003c
#define RG_DSI_PLL_IBIAS		GENMASK(11, 10)
#define MIPITX_PHY_SEL0		0x0040
#define MIPITX_PHY_SEL0_VAL	0x65432101
#define MIPITX_PHY_SEL1		0x0044
#define MIPITX_PHY_SEL1_VAL	0x24210987
#define MIPITX_PHY_SEL2		0x0048
#define MIPITX_PHY_SEL2_VAL	0x68543102
#define MIPITX_PHY_SEL3		0x004c
#define MIPITX_PHY_SEL3_VAL	0x00000007

#define MIPITX_D2P_RTCODE	0x0100
#define MIPITX_D2_SW_CTL_EN	0x0144
#define MIPITX_D0_SW_CTL_EN	0x0244
#define MIPITX_CK_CKMODE_EN	0x0328
#define DSI_CK_CKMODE_EN		BIT(0)
#define MIPITX_CK_SW_CTL_EN	0x0344
#define MIPITX_D1_SW_CTL_EN	0x0444
#define MIPITX_D3_SW_CTL_EN	0x0544
#define DSI_SW_CTL_EN			BIT(0)
#define AD_DSI_PLL_SDM_PWR_ON		BIT(0)
#define AD_DSI_PLL_SDM_ISO_EN		BIT(1)

#define RG_DSI_PLL_EN			BIT(4)
#define RG_DSI_PLL_POSDIV		GENMASK(10, 8)

static int mtk_mipi_tx_pll_enable(struct clk_hw *hw)
{
	struct mtk_mipi_tx *mipi_tx = mtk_mipi_tx_from_clk_hw(hw);
	void __iomem *base = mipi_tx->regs;
	unsigned int txdiv, txdiv0;
	u64 pcw;

	dev_dbg(mipi_tx->dev, "enable: %u bps\n", mipi_tx->data_rate);

	if (mipi_tx->data_rate >= 2000000000) {
		txdiv = 1;
		txdiv0 = 0;
	} else if (mipi_tx->data_rate >= 1000000000) {
		txdiv = 2;
		txdiv0 = 1;
	} else if (mipi_tx->data_rate >= 500000000) {
		txdiv = 4;
		txdiv0 = 2;
	} else if (mipi_tx->data_rate > 250000000) {
		txdiv = 8;
		txdiv0 = 3;
	} else if (mipi_tx->data_rate >= 125000000) {
		txdiv = 16;
		txdiv0 = 4;
	} else {
		return -EINVAL;
	}
	if (mipi_tx->is_cphy) {
		writel(MIPITX_PHY_SEL0_VAL, base + MIPITX_PHY_SEL0);
		writel(MIPITX_PHY_SEL1_VAL, base + MIPITX_PHY_SEL1);
		writel(MIPITX_PHY_SEL2_VAL, base + MIPITX_PHY_SEL2);
		writel(MIPITX_PHY_SEL3_VAL, base + MIPITX_PHY_SEL3);
	}
	mtk_phy_clear_bits(base + MIPITX_PLL_CON4, RG_DSI_PLL_IBIAS);

	mtk_phy_set_bits(base + MIPITX_PLL_PWR, AD_DSI_PLL_SDM_PWR_ON);
	mtk_phy_clear_bits(base + MIPITX_PLL_CON1, RG_DSI_PLL_EN);
	udelay(1);
	mtk_phy_clear_bits(base + MIPITX_PLL_PWR, AD_DSI_PLL_SDM_ISO_EN);
	pcw = div_u64(((u64)mipi_tx->data_rate * txdiv) << 24, 26000000);
	writel(pcw, base + MIPITX_PLL_CON0);
	mtk_phy_update_field(base + MIPITX_PLL_CON1, RG_DSI_PLL_POSDIV, txdiv0);
	mtk_phy_set_bits(base + MIPITX_PLL_CON1, RG_DSI_PLL_EN);

	return 0;
}

static void mtk_mipi_tx_pll_disable(struct clk_hw *hw)
{
	struct mtk_mipi_tx *mipi_tx = mtk_mipi_tx_from_clk_hw(hw);
	void __iomem *base = mipi_tx->regs;

	mtk_phy_clear_bits(base + MIPITX_PLL_CON1, RG_DSI_PLL_EN);

	mtk_phy_set_bits(base + MIPITX_PLL_PWR, AD_DSI_PLL_SDM_ISO_EN);
	mtk_phy_clear_bits(base + MIPITX_PLL_PWR, AD_DSI_PLL_SDM_PWR_ON);
}

static long mtk_mipi_tx_pll_round_rate(struct clk_hw *hw, unsigned long rate,
				       unsigned long *prate)
{
	return clamp_val(rate, 125000000, 2500000000);
}

static const struct clk_ops mtk_mipi_tx_pll_ops = {
	.enable = mtk_mipi_tx_pll_enable,
	.disable = mtk_mipi_tx_pll_disable,
	.round_rate = mtk_mipi_tx_pll_round_rate,
	.set_rate = mtk_mipi_tx_pll_set_rate,
	.recalc_rate = mtk_mipi_tx_pll_recalc_rate,
};

/*
 * mtk_mipi_tx_set_lane_mode - Set signal mode for a single MIPI lane
 * @base: Base address of MIPI TX registers
 * @ctl_en_offset: Offset of the SW_CTL_EN register for this lane
 * @submode: Signal mode to set (DISABLE/LP00/LP11)
 *
 * This function controls the signal output state of a single MIPI lane
 * through software mode registers.
 */
static void mtk_mipi_tx_set_lane_mode(void __iomem *base, u32 ctl_en_offset,
				      int submode)
{
	u32 pre_oe_offset = ctl_en_offset + 0x04;
	u32 oe_offset = ctl_en_offset + 0x08;
	u32 dp_offset = ctl_en_offset + 0x0c;
	u32 dn_offset = ctl_en_offset + 0x10;
	u32 c_pre_oe_offset = ctl_en_offset + 0x24;
	u32 c_oe_offset = ctl_en_offset + 0x28;

	switch (submode) {
	case MTK_MIPI_TX_SUBMODE_SW_MODE_DISABLE:
		/* Restore hardware control */
		mtk_phy_clear_bits(base + ctl_en_offset, DSI_SW_CTL_EN);
		break;

	case MTK_MIPI_TX_SUBMODE_SW_MODE_LP00:
		/* LP00: DP=0, DN=0, OE=1, CTL_EN=1 */
		writel(0, base + dp_offset);
		writel(0, base + dn_offset);
		writel(1, base + pre_oe_offset);
		writel(1, base + oe_offset);
		writel(1, base + c_pre_oe_offset);
		writel(1, base + c_oe_offset);
		mtk_phy_set_bits(base + ctl_en_offset, DSI_SW_CTL_EN);
		break;

	case MTK_MIPI_TX_SUBMODE_SW_MODE_LP11:
		/* LP11: DP=1, DN=1, OE=1, CTL_EN=1 */
		writel(1, base + dp_offset);
		writel(1, base + dn_offset);
		writel(1, base + pre_oe_offset);
		writel(1, base + oe_offset);
		writel(1, base + c_pre_oe_offset);
		writel(1, base + c_oe_offset);
		mtk_phy_set_bits(base + ctl_en_offset, DSI_SW_CTL_EN);
		break;
	}
}

/*
 * mtk_mipi_tx_mt8189_set_mode - Set signal mode for MT8189 MIPI PHY
 * @phy: PHY instance
 * @mode: PHY mode (not validated - works for both D-PHY and C-PHY)
 * @submode: Signal control submode (DISABLE/LP00/LP11)
 *
 * Controls all 5 MIPI lanes (D0, D1, D2, D3, CK) signal output state.
 * This function works for both D-PHY and C-PHY as they use the same
 * signal control registers.
 *
 * Note: The mode parameter is not validated because:
 * - Linux kernel has no separate PHY_MODE for C-PHY
 * - C-PHY devices register as PHY_MODE_MIPI_DPHY
 * - Signal control registers (SW_CTL_EN, SW_LPTX_DP/DN/OE) are
 *   identical for both D-PHY and C-PHY hardware
 *
 * Return: 0 on success, -EINVAL if submode is invalid
 */
static int mtk_mipi_tx_mt8189_set_mode(struct phy *phy, enum phy_mode mode,
				       int submode)
{
	struct mtk_mipi_tx *mipi_tx = phy_get_drvdata(phy);
	void __iomem *base = mipi_tx->regs;

	/* Validate submode */
	if (submode < MTK_MIPI_TX_SUBMODE_SW_MODE_DISABLE ||
	    submode > MTK_MIPI_TX_SUBMODE_SW_MODE_LP11)
		return -EINVAL;

	dev_dbg(mipi_tx->dev, "set_mode: mode=%d, submode=%d, is_cphy=%d\n",
		mode, submode, mipi_tx->is_cphy);

	/* Configure all 5 lanes with the same submode */
	mtk_mipi_tx_set_lane_mode(base, MIPITX_D0_SW_CTL_EN, submode);
	mtk_mipi_tx_set_lane_mode(base, MIPITX_D1_SW_CTL_EN, submode);
	mtk_mipi_tx_set_lane_mode(base, MIPITX_D2_SW_CTL_EN, submode);
	mtk_mipi_tx_set_lane_mode(base, MIPITX_D3_SW_CTL_EN, submode);
	mtk_mipi_tx_set_lane_mode(base, MIPITX_CK_SW_CTL_EN, submode);

	return 0;
}

static void mtk_mipi_tx_config_calibration_data(struct mtk_mipi_tx *mipi_tx)
{
	int i, j;

	for (i = 0; i < 5; i++) {
		if ((mipi_tx->rt_code[i] & 0x1f) == 0)
			mipi_tx->rt_code[i] |= 0x10;

		if ((mipi_tx->rt_code[i] >> 5 & 0x1f) == 0)
			mipi_tx->rt_code[i] |= 0x10 << 5;

		for (j = 0; j < 10; j++)
			mtk_phy_update_bits(mipi_tx->regs +
				MIPITX_D2P_RTCODE * (i + 1) + j * 4,
				1, mipi_tx->rt_code[i] >> j & 1);
	}
}

static void mtk_mipi_tx_power_on_signal(struct phy *phy)
{
	struct mtk_mipi_tx *mipi_tx = phy_get_drvdata(phy);
	void __iomem *base = mipi_tx->regs;

	/* BG_LPF_EN / BG_CORE_EN */
	writel(RG_DSI_PAD_TIEL_SEL | RG_DSI_BG_CORE_EN, base + MIPITX_LANE_CON);
	usleep_range(30, 100);
	writel(RG_DSI_BG_CORE_EN | RG_DSI_BG_LPF_EN, base + MIPITX_LANE_CON);
	if (mipi_tx->pre_emphasis_en)
		mtk_phy_set_bits(base + MIPITX_LANE_CON, RG_DSI_PRE_EMPHASIS_EN);
	if (mipi_tx->is_cphy)
		mtk_phy_set_bits(base + MIPITX_LANE_CON, RG_DSI_CPHY_EN);
	/* Keep output signal LP00 for each Lane to match power on sequence stage*/
	mtk_mipi_tx_mt8189_set_mode(phy, PHY_MODE_MIPI_DPHY, MTK_MIPI_TX_SUBMODE_SW_MODE_LP00);

	mtk_phy_update_field(base + MIPITX_VOLTAGE_SEL, RG_DSI_HSTX_LDO_REF_SEL,
			     (mipi_tx->mipitx_drive - 3000) / 200);

	mtk_mipi_tx_config_calibration_data(mipi_tx);
	if (mipi_tx->is_cphy)
		mtk_phy_clear_bits(base + MIPITX_CK_CKMODE_EN, DSI_CK_CKMODE_EN);
	else
		mtk_phy_set_bits(base + MIPITX_CK_CKMODE_EN, DSI_CK_CKMODE_EN);
}

static void mtk_mipi_tx_power_off_signal(struct phy *phy)
{
	struct mtk_mipi_tx *mipi_tx = phy_get_drvdata(phy);
	void __iomem *base = mipi_tx->regs;

	/* Keep output signal LP00 for each Lane to match power off sequence stage*/
	mtk_mipi_tx_mt8189_set_mode(phy, PHY_MODE_MIPI_DPHY, MTK_MIPI_TX_SUBMODE_SW_MODE_LP00);

	writel(RG_DSI_PAD_TIEL_SEL | RG_DSI_BG_CORE_EN, base + MIPITX_LANE_CON);
	writel(RG_DSI_PAD_TIEL_SEL, base + MIPITX_LANE_CON);
}

const struct mtk_mipitx_data mt8189_mipitx_data = {
	.mipi_tx_clk_ops = &mtk_mipi_tx_pll_ops,
	.mipi_tx_enable_signal = mtk_mipi_tx_power_on_signal,
	.mipi_tx_disable_signal = mtk_mipi_tx_power_off_signal,
	.mipi_tx_set_mode = mtk_mipi_tx_mt8189_set_mode,
};
