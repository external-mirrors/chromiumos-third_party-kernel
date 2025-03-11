// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2024 MediaTek Inc.

#include <linux/clk.h>
#include <linux/iopoll.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/sched/clock.h>
#include <linux/sched/mm.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/spmi.h>
#include <linux/irq.h>

#define SWINF_IDLE	0x00
#define SWINF_WFVLDCLR	0x06

#define GET_SWINF(x)	(((x) >> 1) & 0x7)

#define PMIF_CMD_REG_0		0
#define PMIF_CMD_REG		1
#define PMIF_CMD_EXT_REG	2
#define PMIF_CMD_EXT_REG_LONG	3

#define PMIF_DELAY_US   10
#define PMIF_TIMEOUT_US (100 * 1000)

#define PMIF_CHAN_OFFSET 0x5

#define PMIF_CAPS_VER_1 1
#define PMIF_CAPS_VER_2 2

#define PMIF_MAX_CLKS	3

#define SPMI_OP_ST_BUSY 1

#define SPMI_SOC_CHANNEL 2

#define PMIF_IRQDESC(name) { #name, pmif_##name##_irq_handler, -1}

struct ch_reg {
	u32 ch_sta;
	u32 wdata;
	u32 rdata;
	u32 ch_send;
	u32 ch_rdy;
};

struct pmif_data {
	const u32	*regs;
	const u32	*spmimst_regs;
	u32	soc_chan;
	u32 caps;
};

struct pmif {
	void __iomem	*pmif_base[2];
	void __iomem	*spmimst_base[2];
	struct ch_reg	chan;
	struct clk_bulk_data clks[PMIF_MAX_CLKS];
	size_t nclks;
	const struct pmif_data *data;
	raw_spinlock_t	lock_m;
	raw_spinlock_t	lock_p;
	struct spmi_controller  *spmic;
	struct wakeup_source *pmif_m_Thread_lock;
	struct wakeup_source *pmif_p_Thread_lock;
	struct mutex pmif_m_mutex;
	struct mutex pmif_p_mutex;
	int		irq;
	int		irq_p;
	struct irq_domain	*domain;
	struct irq_chip		irq_chip;
	struct irq_chip		irq_chip_p;
	int			rcs_irq;
	int			rcs_irq_p;
	struct mutex		rcs_m_irqlock;
	struct mutex		rcs_p_irqlock;
	bool	   *rcs_enable_hwirq;
	int spmi_nack_irq;
	int spmi_p_nack_irq;
};

static const char * const pmif_clock_names[] = {
	"pmif_sys_ck", "pmif_tmr_ck", "spmimst_clk_mux",
};

struct pmif_irq_desc {
	const char *name;
	irq_handler_t irq_handler;
	int irq;
};

enum pmif_regs {
	PMIF_INIT_DONE,
	PMIF_INF_EN,
	MD_AUXADC_RDATA_0_ADDR,
	MD_AUXADC_RDATA_1_ADDR,
	MD_AUXADC_RDATA_2_ADDR,
	PMIF_ARB_EN,
	PMIF_CMDISSUE_EN,
	PMIF_TIMER_CTRL,
	PMIF_SPI_MODE_CTRL,
	PMIF_IRQ_EVENT_EN_0,
	PMIF_IRQ_FLAG_0,
	PMIF_IRQ_CLR_0,
	PMIF_IRQ_EVENT_EN_1,
	PMIF_IRQ_FLAG_1,
	PMIF_IRQ_CLR_1,
	PMIF_IRQ_EVENT_EN_2,
	PMIF_IRQ_FLAG_2,
	PMIF_IRQ_CLR_2,
	PMIF_IRQ_EVENT_EN_3,
	PMIF_IRQ_FLAG_3,
	PMIF_IRQ_CLR_3,
	PMIF_IRQ_EVENT_EN_4,
	PMIF_IRQ_FLAG_4,
	PMIF_IRQ_CLR_4,
	PMIF_WDT_EVENT_EN_0,
	PMIF_WDT_FLAG_0,
	PMIF_WDT_EVENT_EN_1,
	PMIF_WDT_FLAG_1,
	PMIF_SWINF_0_STA,
	PMIF_SWINF_0_WDATA_31_0,
	PMIF_SWINF_0_RDATA_31_0,
	PMIF_SWINF_0_ACC,
	PMIF_SWINF_0_VLD_CLR,
	PMIF_SWINF_1_STA,
	PMIF_SWINF_1_WDATA_31_0,
	PMIF_SWINF_1_RDATA_31_0,
	PMIF_SWINF_1_ACC,
	PMIF_SWINF_1_VLD_CLR,
	PMIF_SWINF_2_STA,
	PMIF_SWINF_2_WDATA_31_0,
	PMIF_SWINF_2_RDATA_31_0,
	PMIF_SWINF_2_ACC,
	PMIF_SWINF_2_VLD_CLR,
	PMIF_SWINF_3_STA,
	PMIF_SWINF_3_WDATA_31_0,
	PMIF_SWINF_3_RDATA_31_0,
	PMIF_SWINF_3_ACC,
	PMIF_SWINF_3_VLD_CLR,
	PMIF_PMIC_SWINF_0_PER,
	PMIF_PMIC_SWINF_1_PER,
	PMIF_ACC_VIO_INFO_0,
	PMIF_ACC_VIO_INFO_1,
	PMIF_ACC_VIO_INFO_2,
};

static const u32 mt6873_regs[] = {
	[PMIF_INIT_DONE] = 0x0000,
	[PMIF_INF_EN] = 0x0024,
	[PMIF_ARB_EN] = 0x0150,
	[PMIF_CMDISSUE_EN] = 0x03B4,
	[PMIF_TIMER_CTRL] = 0x03E0,
	[PMIF_SPI_MODE_CTRL] = 0x0400,
	[PMIF_IRQ_EVENT_EN_0] = 0x0418,
	[PMIF_IRQ_FLAG_0] = 0x0420,
	[PMIF_IRQ_CLR_0] = 0x0424,
	[PMIF_IRQ_EVENT_EN_1] = 0x0428,
	[PMIF_IRQ_FLAG_1] = 0x0430,
	[PMIF_IRQ_CLR_1] = 0x0434,
	[PMIF_IRQ_EVENT_EN_2] = 0x0438,
	[PMIF_IRQ_FLAG_2] = 0x0440,
	[PMIF_IRQ_CLR_2] = 0x0444,
	[PMIF_IRQ_EVENT_EN_3] = 0x0448,
	[PMIF_IRQ_FLAG_3] = 0x0450,
	[PMIF_IRQ_CLR_3] = 0x0454,
	[PMIF_IRQ_EVENT_EN_4] = 0x0458,
	[PMIF_IRQ_FLAG_4] = 0x0460,
	[PMIF_IRQ_CLR_4] = 0x0464,
	[PMIF_WDT_EVENT_EN_0] = 0x046C,
	[PMIF_WDT_FLAG_0] = 0x0470,
	[PMIF_WDT_EVENT_EN_1] = 0x0474,
	[PMIF_WDT_FLAG_1] = 0x0478,
	[PMIF_SWINF_0_ACC] = 0x0C00,
	[PMIF_SWINF_0_WDATA_31_0] = 0x0C04,
	[PMIF_SWINF_0_RDATA_31_0] = 0x0C14,
	[PMIF_SWINF_0_VLD_CLR] = 0x0C24,
	[PMIF_SWINF_0_STA] = 0x0C28,
	[PMIF_SWINF_1_ACC] = 0x0C40,
	[PMIF_SWINF_1_WDATA_31_0] = 0x0C44,
	[PMIF_SWINF_1_RDATA_31_0] = 0x0C54,
	[PMIF_SWINF_1_VLD_CLR] = 0x0C64,
	[PMIF_SWINF_1_STA] = 0x0C68,
	[PMIF_SWINF_2_ACC] = 0x0C80,
	[PMIF_SWINF_2_WDATA_31_0] = 0x0C84,
	[PMIF_SWINF_2_RDATA_31_0] = 0x0C94,
	[PMIF_SWINF_2_VLD_CLR] = 0x0CA4,
	[PMIF_SWINF_2_STA] = 0x0CA8,
	[PMIF_SWINF_3_ACC] = 0x0CC0,
	[PMIF_SWINF_3_WDATA_31_0] = 0x0CC4,
	[PMIF_SWINF_3_RDATA_31_0] = 0x0CD4,
	[PMIF_SWINF_3_VLD_CLR] = 0x0CE4,
	[PMIF_SWINF_3_STA] = 0x0CE8,
};

static const u32 mt8195_regs[] = {
	[PMIF_INIT_DONE] = 0x0000,
	[PMIF_INF_EN] = 0x0024,
	[PMIF_ARB_EN] = 0x0150,
	[PMIF_CMDISSUE_EN] = 0x03B8,
	[PMIF_TIMER_CTRL] = 0x03E4,
	[PMIF_SPI_MODE_CTRL] = 0x0408,
	[PMIF_IRQ_EVENT_EN_0] = 0x0420,
	[PMIF_IRQ_FLAG_0] = 0x0428,
	[PMIF_IRQ_CLR_0] = 0x042C,
	[PMIF_IRQ_EVENT_EN_1] = 0x0430,
	[PMIF_IRQ_FLAG_1] = 0x0438,
	[PMIF_IRQ_CLR_1] = 0x043C,
	[PMIF_IRQ_EVENT_EN_2] = 0x0440,
	[PMIF_IRQ_FLAG_2] = 0x0448,
	[PMIF_IRQ_CLR_2] = 0x044C,
	[PMIF_IRQ_EVENT_EN_3] = 0x0450,
	[PMIF_IRQ_FLAG_3] = 0x0458,
	[PMIF_IRQ_CLR_3] = 0x045C,
	[PMIF_IRQ_EVENT_EN_4] = 0x0460,
	[PMIF_IRQ_FLAG_4] = 0x0468,
	[PMIF_IRQ_CLR_4] = 0x046C,
	[PMIF_WDT_EVENT_EN_0] = 0x0474,
	[PMIF_WDT_FLAG_0] = 0x0478,
	[PMIF_WDT_EVENT_EN_1] = 0x047C,
	[PMIF_WDT_FLAG_1] = 0x0480,
	[PMIF_SWINF_0_ACC] = 0x0800,
	[PMIF_SWINF_0_WDATA_31_0] = 0x0804,
	[PMIF_SWINF_0_RDATA_31_0] = 0x0814,
	[PMIF_SWINF_0_VLD_CLR] = 0x0824,
	[PMIF_SWINF_0_STA] = 0x0828,
	[PMIF_SWINF_1_ACC] = 0x0840,
	[PMIF_SWINF_1_WDATA_31_0] = 0x0844,
	[PMIF_SWINF_1_RDATA_31_0] = 0x0854,
	[PMIF_SWINF_1_VLD_CLR] = 0x0864,
	[PMIF_SWINF_1_STA] = 0x0868,
	[PMIF_SWINF_2_ACC] = 0x0880,
	[PMIF_SWINF_2_WDATA_31_0] = 0x0884,
	[PMIF_SWINF_2_RDATA_31_0] = 0x0894,
	[PMIF_SWINF_2_VLD_CLR] = 0x08A4,
	[PMIF_SWINF_2_STA] = 0x08A8,
	[PMIF_SWINF_3_ACC] = 0x08C0,
	[PMIF_SWINF_3_WDATA_31_0] = 0x08C4,
	[PMIF_SWINF_3_RDATA_31_0] = 0x08D4,
	[PMIF_SWINF_3_VLD_CLR] = 0x08E4,
	[PMIF_SWINF_3_STA] = 0x08E8,
};

static const u32 mt6xxx_regs[] = {
	[PMIF_INIT_DONE] =			0x0000,
	[PMIF_INF_EN] =				0x0024,
	[MD_AUXADC_RDATA_0_ADDR] =		0x0080,
	[MD_AUXADC_RDATA_1_ADDR] =		0x0084,
	[MD_AUXADC_RDATA_2_ADDR] =		0x0088,
	[PMIF_ARB_EN] =				0x0150,
	[PMIF_CMDISSUE_EN] =			0x03B8,
	[PMIF_TIMER_CTRL] =			0x03E4,
	[PMIF_SPI_MODE_CTRL] =			0x0408,
	[PMIF_IRQ_EVENT_EN_0] =			0x0420,
	[PMIF_IRQ_FLAG_0] =			0x0428,
	[PMIF_IRQ_CLR_0] =			0x042C,
	[PMIF_IRQ_EVENT_EN_1] =			0x0430,
	[PMIF_IRQ_FLAG_1] =			0x0438,
	[PMIF_IRQ_CLR_1] =			0x043C,
	[PMIF_IRQ_EVENT_EN_2] =			0x0440,
	[PMIF_IRQ_FLAG_2] =			0x0448,
	[PMIF_IRQ_CLR_2] =			0x044C,
	[PMIF_IRQ_EVENT_EN_3] =			0x0450,
	[PMIF_IRQ_FLAG_3] =			0x0458,
	[PMIF_IRQ_CLR_3] =			0x045C,
	[PMIF_IRQ_EVENT_EN_4] =			0x0460,
	[PMIF_IRQ_FLAG_4] =			0x0468,
	[PMIF_IRQ_CLR_4] =			0x046C,
	[PMIF_WDT_EVENT_EN_0] =			0x0474,
	[PMIF_WDT_FLAG_0] =			0x0478,
	[PMIF_WDT_EVENT_EN_1] =			0x047C,
	[PMIF_WDT_FLAG_1] =			0x0480,
	[PMIF_SWINF_0_ACC] =			0x0800,
	[PMIF_SWINF_0_WDATA_31_0] =		0x0804,
	[PMIF_SWINF_0_RDATA_31_0] =		0x0814,
	[PMIF_SWINF_0_VLD_CLR] =		0x0824,
	[PMIF_SWINF_0_STA] =			0x0828,
	[PMIF_SWINF_1_ACC] =			0x0840,
	[PMIF_SWINF_1_WDATA_31_0] =		0x0844,
	[PMIF_SWINF_1_RDATA_31_0] =		0x0854,
	[PMIF_SWINF_1_VLD_CLR] =		0x0864,
	[PMIF_SWINF_1_STA] =			0x0868,
	[PMIF_SWINF_2_ACC] =			0x0880,
	[PMIF_SWINF_2_WDATA_31_0] =		0x0884,
	[PMIF_SWINF_2_RDATA_31_0] =		0x0894,
	[PMIF_SWINF_2_VLD_CLR] =		0x08A4,
	[PMIF_SWINF_2_STA] =			0x08A8,
	[PMIF_SWINF_3_ACC] =			0x08C0,
	[PMIF_SWINF_3_WDATA_31_0] =		0x08C4,
	[PMIF_SWINF_3_RDATA_31_0] =		0x08D4,
	[PMIF_SWINF_3_VLD_CLR] =		0x08E4,
	[PMIF_SWINF_3_STA] =			0x08E8,
	[PMIF_PMIC_SWINF_0_PER] =		0x093C,
	[PMIF_PMIC_SWINF_1_PER] =		0x0940,
	[PMIF_ACC_VIO_INFO_0] =			0x0980,
	[PMIF_ACC_VIO_INFO_1] =			0x0984,
	[PMIF_ACC_VIO_INFO_2] =			0x0988,

};

enum spmi_regs {
	SPMI_OP_ST_CTRL,
	SPMI_GRP_ID_EN,
	SPMI_OP_ST_STA,
	SPMI_MST_SAMPL,
	SPMI_MST_REQ_EN,
	SPMI_REC_CTRL,
	SPMI_REC0,
	SPMI_REC1,
	SPMI_REC2,
	SPMI_REC3,
	SPMI_REC4,
	SPMI_MST_DBG,

	/* MT8195 spmi regs */
	SPMI_MST_RCS_CTRL,
	SPMI_SLV_3_0_EINT,
	SPMI_SLV_7_4_EINT,
	SPMI_SLV_B_8_EINT,
	SPMI_SLV_F_C_EINT,
	SPMI_REC_CMD_DEC,
	SPMI_DEC_DBG,
};

static const u32 mt6873_spmi_regs[] = {
	[SPMI_OP_ST_CTRL] = 0x0000,
	[SPMI_GRP_ID_EN] = 0x0004,
	[SPMI_OP_ST_STA] = 0x0008,
	[SPMI_MST_SAMPL] = 0x000c,
	[SPMI_MST_REQ_EN] = 0x0010,
	[SPMI_REC_CTRL] = 0x0040,
	[SPMI_REC0] = 0x0044,
	[SPMI_REC1] = 0x0048,
	[SPMI_REC2] = 0x004c,
	[SPMI_REC3] = 0x0050,
	[SPMI_REC4] = 0x0054,
	[SPMI_MST_DBG] = 0x00fc,
};

static const u32 mt8195_spmi_regs[] = {
	[SPMI_OP_ST_CTRL] = 0x0000,
	[SPMI_GRP_ID_EN] = 0x0004,
	[SPMI_OP_ST_STA] = 0x0008,
	[SPMI_MST_SAMPL] = 0x000C,
	[SPMI_MST_REQ_EN] = 0x0010,
	[SPMI_MST_RCS_CTRL] = 0x0014,
	[SPMI_SLV_3_0_EINT] = 0x0020,
	[SPMI_SLV_7_4_EINT] = 0x0024,
	[SPMI_SLV_B_8_EINT] = 0x0028,
	[SPMI_SLV_F_C_EINT] = 0x002C,
	[SPMI_REC_CTRL] = 0x0040,
	[SPMI_REC0] = 0x0044,
	[SPMI_REC1] = 0x0048,
	[SPMI_REC2] = 0x004C,
	[SPMI_REC3] = 0x0050,
	[SPMI_REC4] = 0x0054,
	[SPMI_REC_CMD_DEC] = 0x005C,
	[SPMI_DEC_DBG] = 0x00F8,
	[SPMI_MST_DBG] = 0x00FC,
};

static const u32 mt6853_spmi_regs[] = {
	[SPMI_OP_ST_CTRL] =	0x0000,
	[SPMI_GRP_ID_EN] =	0x0004,
	[SPMI_OP_ST_STA] =	0x0008,
	[SPMI_MST_SAMPL] =	0x000C,
	[SPMI_MST_REQ_EN] =	0x0010,
	[SPMI_MST_RCS_CTRL] =	0x0014,
	[SPMI_SLV_3_0_EINT] =	0x0020,
	[SPMI_SLV_7_4_EINT] =	0x0024,
	[SPMI_SLV_B_8_EINT] =	0x0028,
	[SPMI_SLV_F_C_EINT] =	0x002C,
	[SPMI_REC_CTRL] =	0x0040,
	[SPMI_REC0] =		0x0044,
	[SPMI_REC1] =		0x0048,
	[SPMI_REC2] =		0x004C,
	[SPMI_REC3] =		0x0050,
	[SPMI_REC4] =		0x0054,
	[SPMI_REC_CMD_DEC] =	0x005C,
	[SPMI_DEC_DBG] =	0x00F8,
	[SPMI_MST_DBG] =	0x00FC,
};

enum {
	IRQ_LAT_LIMIT_REACHED = 6,
	IRQ_HW_MONITOR = 7,
	IRQ_WDT = 8,
	/* MT6833/MT6877 series */
	IRQ_HW_MONITOR_V3 = 12,
	IRQ_WDT_V3 = 13,
	IRQ_ALL_PMIC_MPU_VIO_V3 = 14,
	/* MT6885/MT6873 series */
	IRQ_PMIC_CMD_ERR_PARITY_ERR = 17,
	IRQ_PMIF_ACC_VIO = 20,
	IRQ_PMIC_ACC_VIO = 21,
	/* MT6985/MT6897 */
	IRQ_PMIF_ACC_VIO_V3 = 27,
	/* MT6853 series */
	IRQ_PMIF_ACC_VIO_V2 = 31,
};

enum {
	IRQ_PMIC_ACC_VIO_V2 = 0,
	IRQ_PMIF_SWINF_ACC_ERR_0 = 3,
	IRQ_PMIF_SWINF_ACC_ERR_1 = 4,
	IRQ_PMIF_SWINF_ACC_ERR_2 = 5,
	IRQ_PMIF_SWINF_ACC_ERR_3 = 6,
	IRQ_PMIF_SWINF_ACC_ERR_4 = 7,
	IRQ_PMIF_SWINF_ACC_ERR_5 = 8,
	IRQ_HW_MONITOR_V2 = 18,
	IRQ_WDT_V2 = 19,
	IRQ_ALL_PMIC_MPU_VIO_V2 = 20,
	IRQ_PMIF_SWINF_ACC_ERR_0_V2 = 23,
	IRQ_PMIF_SWINF_ACC_ERR_1_V2 = 24,
	IRQ_PMIF_SWINF_ACC_ERR_2_V2 = 25,
	IRQ_PMIF_SWINF_ACC_ERR_3_V2 = 26,
	IRQ_PMIF_SWINF_ACC_ERR_4_V2 = 27,
	IRQ_PMIF_SWINF_ACC_ERR_5_V2 = 28,
	/* MT6983/MT6879 */
	IRQ_HW_MONITOR_V4 = 29,
	IRQ_WDT_V4 = 30,
	IRQ_ALL_PMIC_MPU_VIO_V4 = 31,
};

struct spmi_dev {
	int exist;
	int slvid;
	unsigned int path;
	unsigned short hwcid_addr;
	unsigned char hwcid_val;
};

static struct spmi_dev spmidev[16];

static void spmi_dev_parse(struct platform_device *pdev)
{
	int i = 0, j = 0, ret = 0;
	u32 spmi_dev_mask = 0;

	ret = of_property_read_u32(pdev->dev.of_node, "spmi-dev-mask",
				  &spmi_dev_mask);
	if (ret)
		dev_info(&pdev->dev, "No spmi-dev-mask found in dts, default all PMIC on SPMI-M\n");

	j = spmi_dev_mask;
	for (i = 0; i < 16; i++) {
		if (j & BIT(i))
			spmidev[i].path = 1; /* slvid i is in path p */
		else
			spmidev[i].path = 0;
	}
}

unsigned long long get_current_time_ms(void)
{
	unsigned long long cur_ts;

	cur_ts = sched_clock();
	do_div(cur_ts, 1000000);
	return cur_ts;
}

static u32 pmif_readl(void __iomem *addr, struct pmif *arb, enum pmif_regs reg)
{
	return readl(addr + arb->data->regs[reg]);
}

static void pmif_writel(void __iomem *addr, struct pmif *arb, u32 val, enum pmif_regs reg)
{
	writel(val, addr + arb->data->regs[reg]);
}

static void mtk_spmi_writel(void __iomem *addr, struct pmif *arb, u32 val, enum spmi_regs reg)
{
	writel(val, addr + arb->data->spmimst_regs[reg]);
}

static u32 mtk_spmi_readl(void __iomem *addr, struct pmif *arb, enum spmi_regs reg)
{
	return readl(addr + arb->data->spmimst_regs[reg]);
}

static bool pmif_is_fsm_vldclr(void __iomem *addr, struct pmif *arb)
{
	u32 reg_rdata;

	reg_rdata = pmif_readl(addr, arb, arb->chan.ch_sta);

	return GET_SWINF(reg_rdata) == SWINF_WFVLDCLR;
}

static int pmif_arb_cmd(struct spmi_controller *ctrl, u8 opc, u8 sid)
{
	struct pmif *arb = spmi_controller_get_drvdata(ctrl);
	u32 rdata, cmd;
	int ret;

	/* Check the opcode */
	if (opc < SPMI_CMD_RESET || opc > SPMI_CMD_WAKEUP)
		return -EINVAL;

	cmd = opc - SPMI_CMD_RESET;

	mtk_spmi_writel(arb->spmimst_base[spmidev[sid].path], arb, (cmd << 0x4) | sid, SPMI_OP_ST_CTRL);
	ret = readl_poll_timeout_atomic(arb->spmimst_base[spmidev[sid].path] + arb->data->spmimst_regs[SPMI_OP_ST_STA],
					rdata, (rdata & SPMI_OP_ST_BUSY) == SPMI_OP_ST_BUSY,
					PMIF_DELAY_US, PMIF_TIMEOUT_US);
	if (ret < 0)
		dev_err(&ctrl->dev, "timeout, err = %d\n", ret);

	return ret;
}

static int pmif_spmi_read_cmd(struct spmi_controller *ctrl, u8 opc, u8 sid,
			      u16 addr, u8 *buf, size_t len)
{
	struct pmif *arb = spmi_controller_get_drvdata(ctrl);
	struct ch_reg *inf_reg;
	int ret;
	u32 data, cmd;
	unsigned long flags_m;

	/* Check for argument validation. */
	if (sid & ~0xf) {
		dev_err(&ctrl->dev, "exceed the max slv id\n");
		return -EINVAL;
	}

	if (len > 4) {
		dev_err(&ctrl->dev, "pmif supports 1..4 bytes per trans, but:%zu requested", len);

		return -EINVAL;
	}

	if (opc >= 0x60 && opc <= 0x7f)
		opc = PMIF_CMD_REG;
	else if ((opc >= 0x20 && opc <= 0x2f) || (opc >= 0x38 && opc <= 0x3f))
		opc = PMIF_CMD_EXT_REG_LONG;
	else
		return -EINVAL;

	raw_spin_lock_irqsave(&arb->lock_m, flags_m);
	/* Wait for Software Interface FSM state to be IDLE. */
	inf_reg = &arb->chan;
	ret = readl_poll_timeout_atomic(arb->pmif_base[spmidev[sid].path] + arb->data->regs[inf_reg->ch_sta],
					data, GET_SWINF(data) == SWINF_IDLE,
					PMIF_DELAY_US, PMIF_TIMEOUT_US);
	if (ret < 0) {
		/* set channel ready if the data has transferred */
		if (pmif_is_fsm_vldclr(arb->pmif_base[spmidev[sid].path], arb))
			pmif_writel(arb->pmif_base[spmidev[sid].path], arb, 1, inf_reg->ch_rdy);
		raw_spin_unlock_irqrestore(&arb->lock_m, flags_m);
		dev_err(&ctrl->dev, "failed to wait for SWINF_IDLE\n");
		return ret;
	}

	/* Send the command. */
	cmd = (opc << 30) | (sid << 24) | ((len - 1) << 16) | addr;
	pmif_writel(arb->pmif_base[spmidev[sid].path], arb, cmd, inf_reg->ch_send);
	raw_spin_unlock_irqrestore(&arb->lock_m, flags_m);

	/*
	 * Wait for Software Interface FSM state to be WFVLDCLR,
	 * read the data and clear the valid flag.
	 */
	ret = readl_poll_timeout_atomic(arb->pmif_base[spmidev[sid].path]  + arb->data->regs[inf_reg->ch_sta],
					data, GET_SWINF(data) == SWINF_WFVLDCLR,
					PMIF_DELAY_US, PMIF_TIMEOUT_US);
	if (ret < 0) {
		dev_err(&ctrl->dev, "failed to wait for SWINF_WFVLDCLR\n");
		return ret;
	}

	data = pmif_readl(arb->pmif_base[spmidev[sid].path], arb, inf_reg->rdata);
	memcpy(buf, &data, len);
	pmif_writel(arb->pmif_base[spmidev[sid].path], arb, 1, inf_reg->ch_rdy);

	return 0;
}

static int pmif_spmi_write_cmd(struct spmi_controller *ctrl, u8 opc, u8 sid,
			       u16 addr, const u8 *buf, size_t len)
{
	struct pmif *arb = spmi_controller_get_drvdata(ctrl);
	struct ch_reg *inf_reg;
	int ret;
	u32 data, wdata, cmd;
	unsigned long flags_m;

	/* Check for argument validation. */
	if (unlikely(sid & ~0xf)) {
		dev_err(&ctrl->dev, "exceed the max slv id\n");
		return -EINVAL;
	}

	if (len > 4) {
		dev_err(&ctrl->dev, "pmif supports 1..4 bytes per trans, but:%zu requested", len);

		return -EINVAL;
	}

	/* Check the opcode */
	if (opc >= 0x40 && opc <= 0x5F)
		opc = PMIF_CMD_REG;
	else if ((opc <= 0xF) || (opc >= 0x30 && opc <= 0x37))
		opc = PMIF_CMD_EXT_REG_LONG;
	else if (opc >= 0x80)
		opc = PMIF_CMD_REG_0;
	else
		return -EINVAL;

	/* Set the write data. */
	memcpy(&wdata, buf, len);

	raw_spin_lock_irqsave(&arb->lock_m, flags_m);
	/* Wait for Software Interface FSM state to be IDLE. */
	inf_reg = &arb->chan;
	ret = readl_poll_timeout_atomic(arb->pmif_base[spmidev[sid].path] + arb->data->regs[inf_reg->ch_sta],
					data, GET_SWINF(data) == SWINF_IDLE,
					PMIF_DELAY_US, PMIF_TIMEOUT_US);
	if (ret < 0) {
		/* set channel ready if the data has transferred */
		if (pmif_is_fsm_vldclr(arb->pmif_base[spmidev[sid].path], arb))
			pmif_writel(arb->pmif_base[spmidev[sid].path], arb, 1, inf_reg->ch_rdy);
		raw_spin_unlock_irqrestore(&arb->lock_m, flags_m);
		dev_err(&ctrl->dev, "failed to wait for SWINF_IDLE\n");
		return ret;
	}

	pmif_writel(arb->pmif_base[spmidev[sid].path], arb, wdata, inf_reg->wdata);

	/* Send the command. */
	cmd = (opc << 30) | BIT(29) | (sid << 24) | ((len - 1) << 16) | addr;
	pmif_writel(arb->pmif_base[spmidev[sid].path], arb, cmd, inf_reg->ch_send);
	raw_spin_unlock_irqrestore(&arb->lock_m, flags_m);

	return 0;
}

static const struct pmif_data mt6873_pmif_arb = {
	.regs = mt6873_regs,
	.spmimst_regs = mt6873_spmi_regs,
	.soc_chan = SPMI_SOC_CHANNEL,
	.caps = PMIF_CAPS_VER_1,
};

static const struct pmif_data mt8195_pmif_arb = {
	.regs = mt8195_regs,
	.spmimst_regs = mt8195_spmi_regs,
	.soc_chan = SPMI_SOC_CHANNEL,
	.caps = PMIF_CAPS_VER_1,
};

static const struct pmif_data mt6xxx_pmif_arb = {
	.regs = mt6xxx_regs,
	.spmimst_regs = mt6853_spmi_regs,
	.soc_chan = SPMI_SOC_CHANNEL,
	.caps = PMIF_CAPS_VER_2,
};

static void mtk_spmi_readl_datas(struct pmif *arb, int id, unsigned int *spmi_nack,
		unsigned int *spmi_nack_data, unsigned int *spmi_rcs_nack,
		unsigned int *spmi_debug_nack,unsigned int *spmi_mst_nack)
{
	*spmi_nack = mtk_spmi_readl(arb->spmimst_base[id], arb, SPMI_REC0);
	*spmi_nack_data = mtk_spmi_readl(arb->spmimst_base[id], arb, SPMI_REC1);
	*spmi_rcs_nack = mtk_spmi_readl(arb->spmimst_base[id], arb, SPMI_REC_CMD_DEC);
	*spmi_debug_nack = mtk_spmi_readl(arb->spmimst_base[id], arb, SPMI_DEC_DBG);
	*spmi_mst_nack = mtk_spmi_readl(arb->spmimst_base[id], arb, SPMI_MST_DBG);
}

static irqreturn_t spmi_nack_irq_handler(int irq, void *data)
{
	struct pmif *arb = data;
	int sid = 0;
	unsigned int spmi_nack = 0, spmi_p_nack = 0, spmi_nack_data = 0, spmi_p_nack_data = 0;
	unsigned int spmi_rcs_nack = 0, spmi_debug_nack = 0, spmi_mst_nack = 0,
		spmi_p_rcs_nack = 0, spmi_p_debug_nack = 0, spmi_p_mst_nack = 0;
	u8 rdata = 0, rdata1 = 0;
	unsigned short mt6316INTSTA = 0x240, hwcidaddr_mt6316 = 0x209, VIO18_SWITCH_6363 = 0x53,
		hwcidaddr_mt6363 = 0x9;

	__pm_stay_awake(arb->pmif_m_Thread_lock);
	mutex_lock(&arb->pmif_m_mutex);

	mtk_spmi_readl_datas(arb, 0, &spmi_nack, &spmi_nack_data, &spmi_rcs_nack,
		&spmi_debug_nack, &spmi_mst_nack);
	if (!IS_ERR(arb->spmimst_base[1])) {
		mtk_spmi_readl_datas(arb, 1, &spmi_p_nack, &spmi_p_nack_data, &spmi_p_rcs_nack,
			&spmi_p_debug_nack, &spmi_p_mst_nack);
	}
	if ((spmi_nack & 0xD8) || (spmi_p_nack & 0xD8)) {
		if (spmi_p_nack & 0xD8) {
			for (sid = 0x8; sid >= 0x6; sid--) {
				arb->spmic->read_cmd(arb->spmic, SPMI_CMD_EXT_READL, sid,
					mt6316INTSTA, &rdata, 1);
				arb->spmic->read_cmd(arb->spmic, SPMI_CMD_EXT_READL, sid,
					hwcidaddr_mt6316, &rdata1, 1);
				pr_notice("%s slvid 0x%x INT_RAW_STA 0x%x cid 0x%x\n",
					__func__, sid, rdata, rdata1);
			}
		}
		pr_notice("%s spmi transaction fail irq triggered", __func__);
		pr_notice("SPMI_REC0 m/p:0x%x/0x%x SPMI_REC1 m/p 0x%x/0x%x\n",
			spmi_nack, spmi_p_nack, spmi_nack_data, spmi_p_nack_data);
	}
	if ((spmi_rcs_nack & 0xC0000) || (spmi_p_rcs_nack & 0xC0000)) {
		pr_notice("%s spmi_rcs transaction fail irq triggered SPMI_REC_CMD_DEC m/p:0x%x/0x%x\n",
			__func__, spmi_rcs_nack, spmi_p_rcs_nack);
	}
	if ((spmi_debug_nack & 0xF0000) || (spmi_p_debug_nack & 0xF0000)) {
		pr_notice("%s spmi_debug_nack transaction fail irq triggered SPMI_DEC_DBG m/p: 0x%x/0x%x\n",
			__func__, spmi_debug_nack, spmi_p_debug_nack);
	}
	if ((spmi_mst_nack & 0xC0000) || (spmi_p_mst_nack & 0xC0000)) {
		pr_notice("%s spmi_mst_nack transaction fail irq triggered SPMI_MST_DBG m/p: 0x%x/0x%x\n",
		__func__, spmi_mst_nack, spmi_p_mst_nack);
	}

	/* clear irq*/
	if ((spmi_nack & 0xF8) || (spmi_rcs_nack & 0xC0000) ||
		(spmi_debug_nack & 0xF0000) || (spmi_mst_nack & 0xC0000)) {
		mtk_spmi_writel(arb->spmimst_base[0], arb, 0x3, SPMI_REC_CTRL);
	} else if ((spmi_p_nack & 0xF8) || (spmi_p_rcs_nack & 0xC0000) ||
		(spmi_p_debug_nack & 0xF0000) || (spmi_p_mst_nack & 0xC0000)) {
		if (spmi_p_nack & 0xD8) {
			pr_notice("%s SPMI_REC0 m/p:0x%x/0x%x, SPMI_REC1 m/p:0x%x/0x%x\n",
				__func__, spmi_nack, spmi_p_nack, spmi_nack_data, spmi_p_nack_data);
			pr_notice("%s SPMI_REC_CMD_DEC m/p:0x%x/0x%x\n", __func__,
				spmi_rcs_nack, spmi_p_rcs_nack);
			pr_notice("%s SPMI_DEC_DBG m/p:0x%x/0x%x\n", __func__,
				spmi_debug_nack, spmi_p_debug_nack);
			pr_notice("%s SPMI_MST_DBG m/p:0x%x/0x%x\n", __func__,
				spmi_mst_nack, spmi_p_mst_nack);
			arb->spmic->read_cmd(arb->spmic, SPMI_CMD_EXT_READL, 0x4,
				hwcidaddr_mt6363, &rdata, 1);
			arb->spmic->read_cmd(arb->spmic, SPMI_CMD_EXT_READL, 0x4,
				VIO18_SWITCH_6363, &rdata1, 1);
			pr_notice("%s 6363 CID/VIO18_SWITCH 0x%x/0x%x\n", __func__, rdata, rdata1);
		}
		mtk_spmi_writel(arb->spmimst_base[1], arb, 0x3, SPMI_REC_CTRL);
	} else
		pr_notice("%s IRQ not cleared\n", __func__);

	mutex_unlock(&arb->pmif_m_mutex);
	__pm_relax(arb->pmif_m_Thread_lock);

	return IRQ_HANDLED;
}

static int spmi_nack_irq_register(struct platform_device *pdev,
		struct pmif *arb, int irq)
{
	int ret = 0;

	ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
				spmi_nack_irq_handler,
				IRQF_TRIGGER_HIGH | IRQF_ONESHOT | IRQF_SHARED,
				"spmi_nack_irq", arb);
	if (ret < 0) {
		dev_notice(&pdev->dev, "request %s irq fail\n",
			"spmi_nack_irq");
	}
	return ret;
}

static void rcs_irq_lock(struct irq_data *data)
{
	struct pmif *arb = irq_data_get_irq_chip_data(data);

	mutex_lock(&arb->rcs_m_irqlock);
}

static void rcs_irq_sync_unlock(struct irq_data *data)
{
	struct pmif *arb = irq_data_get_irq_chip_data(data);

	mutex_unlock(&arb->rcs_m_irqlock);
}

static void rcs_irq_enable(struct irq_data *data)
{
	unsigned int hwirq = irqd_to_hwirq(data);
	struct pmif *arb = irq_data_get_irq_chip_data(data);

	arb->rcs_enable_hwirq[hwirq] = true;
}

static void rcs_irq_disable(struct irq_data *data)
{
	unsigned int hwirq = irqd_to_hwirq(data);
	struct pmif *arb = irq_data_get_irq_chip_data(data);

	arb->rcs_enable_hwirq[hwirq] = false;
}

static int rcs_irq_set_wake(struct irq_data *data, unsigned int on)
{
	struct pmif *arb = irq_data_get_irq_chip_data(data);

	return irq_set_irq_wake(arb->rcs_irq, on);
}

static int rcs_irq_p_set_wake(struct irq_data *data, unsigned int on)
{
	struct pmif *arb = irq_data_get_irq_chip_data(data);

	return irq_set_irq_wake(arb->rcs_irq_p, on);
}

static const struct irq_chip rcs_irq_chip = {
	.name			= "rcs_irq",
	.irq_bus_lock		= rcs_irq_lock,
	.irq_bus_sync_unlock	= rcs_irq_sync_unlock,
	.irq_enable		= rcs_irq_enable,
	.irq_disable		= rcs_irq_disable,
	.irq_set_wake 		= rcs_irq_set_wake,
};

static const struct irq_chip rcs_irq_chip_p = {
	.name			= "rcs_irq_p",
	.irq_bus_lock		= rcs_irq_lock,
	.irq_bus_sync_unlock	= rcs_irq_sync_unlock,
	.irq_enable		= rcs_irq_enable,
	.irq_disable		= rcs_irq_disable,
	.irq_set_wake 		= rcs_irq_p_set_wake,
};

static int rcs_irq_map(struct irq_domain *d, unsigned int virq,
			irq_hw_number_t hw)
{
	struct pmif *arb = d->host_data;

	irq_set_chip_data(virq, arb);
	irq_set_chip(virq, &arb->irq_chip);
	irq_set_nested_thread(virq, 1);
	irq_set_parent(virq, arb->rcs_irq);
	irq_set_noprobe(virq);

	return 0;
}

static const struct irq_domain_ops rcs_irq_domain_ops = {
	.map	= rcs_irq_map,
	.xlate	= irq_domain_xlate_onetwocell,
};

static void pmif_lat_limit_reached_irq_handler(int irq, void *data)
{
}

static void pmif_acc_vio_irq_handler(int irq, void *data)
{
	struct pmif *arb = data;

	pr_notice("[PMIF]:PMIF ACC violation debug info\n");
	pr_notice("[PMIF]:PMIF-M\n");
	pr_notice("[PMIF]:PMIF_ACC_VIO_INFO_0 = 0x%x\n", pmif_readl(arb->pmif_base[0],
		arb, PMIF_ACC_VIO_INFO_0));
	pr_notice("[PMIF]:PMIF_ACC_VIO_INFO_1 = 0x%x\n", pmif_readl(arb->pmif_base[0],
		arb, PMIF_ACC_VIO_INFO_1));
	pr_notice("[PMIF]:PMIF_ACC_VIO_INFO_2 = 0x%x\n", pmif_readl(arb->pmif_base[0],
		arb, PMIF_ACC_VIO_INFO_2));

	if (!IS_ERR(arb->pmif_base[1])) {
		pr_notice("[PMIF]:PMIF-P\n");
		pr_notice("[PMIF]:PMIF_ACC_VIO_INFO_0 = 0x%x\n", pmif_readl(arb->pmif_base[1],
			arb, PMIF_ACC_VIO_INFO_0));
		pr_notice("[PMIF]:PMIF_ACC_VIO_INFO_1 = 0x%x\n", pmif_readl(arb->pmif_base[1],
			arb, PMIF_ACC_VIO_INFO_1));
		pr_notice("[PMIF]:PMIF_ACC_VIO_INFO_2 = 0x%x\n", pmif_readl(arb->pmif_base[1],
			arb, PMIF_ACC_VIO_INFO_2));
	}
}

static void pmif_wdt_irq_handler(int irq, void *data)
{
	struct pmif *arb = data;

	pmif_writel(arb->pmif_base[0], arb, 0x40000000, PMIF_IRQ_CLR_0);
	if (!IS_ERR(arb->pmif_base[1]))
		pmif_writel(arb->pmif_base[1], arb, 0x40000000, PMIF_IRQ_CLR_0);
	pr_notice("[PMIF]:WDT IRQ HANDLER DONE\n");
}

static void pmif_pmif_acc_vio_irq_handler(int irq, void *data)
{
#if (IS_ENABLED(CONFIG_MTK_AEE_FEATURE))
	aee_kernel_warning("PMIF", "PMIF:pmif_acc_vio");
#endif
	pr_notice("[PMIF]:pmif_acc_vio\n");
}

static void pmif_pmic_acc_vio_irq_handler(int irq, void *data)
{
#if (IS_ENABLED(CONFIG_MTK_AEE_FEATURE))
	aee_kernel_warning("PMIF", "PMIF:pmic_acc_vio");
#endif
	pr_notice("[PMIF]:pmic_acc_vio\n");
}

/* PMIF Exception IRQ Handler */
static void pmif_cmd_err_parity_err_irq_handler(int irq, void *data)
{
#if (IS_ENABLED(CONFIG_MTK_AEE_FEATURE))
	aee_kernel_warning("PMIF", "PMIF:parity error");
#endif
	pr_notice("[PMIF]:parity error\n");
}

static void pmif_hw_monitor_irq_handler(int irq, void *data)
{
#if (IS_ENABLED(CONFIG_MTK_AEE_FEATURE))
	aee_kernel_warning("PMIF", "PMIF:pmif_hw_monitor_match");
#endif
	pr_notice("[PMIF]:pmif_hw_monitor_match\n");
}

static void pmif_swinf_acc_err_irq_handler(int irq, void *data, u32 val)
{
	struct pmif *arb = data;

	pmif_writel(arb->pmif_base[0], arb, val, MD_AUXADC_RDATA_0_ADDR);
	pmif_writel(arb->pmif_base[0], arb, (u32)get_current_time_ms(), MD_AUXADC_RDATA_1_ADDR);
	if (!IS_ERR(arb->pmif_base[1])) {
		pmif_writel(arb->pmif_base[1], arb, val, MD_AUXADC_RDATA_0_ADDR);
		pmif_writel(arb->pmif_base[1], arb, (u32)get_current_time_ms(), MD_AUXADC_RDATA_1_ADDR);
	}
	pr_notice("[PMIF]:SWINF_ACC_ERR:%d\n", val);
}

static irqreturn_t pmif_event_0_irq_handler(int irq, void *data)
{
	struct pmif *arb = data;
	int irq_f = 0, irq_f_p = 0, idx = 0;

	__pm_stay_awake(arb->pmif_m_Thread_lock);
	mutex_lock(&arb->pmif_m_mutex);

	irq_f = pmif_readl(arb->pmif_base[0], arb, PMIF_IRQ_FLAG_0);
	if (!IS_ERR(arb->pmif_base[1]))
		irq_f_p = pmif_readl(arb->pmif_base[1], arb, PMIF_IRQ_FLAG_0);

	if ((irq_f == 0) && (irq_f_p == 0)) {
		mutex_unlock(&arb->pmif_m_mutex);
		__pm_relax(arb->pmif_m_Thread_lock);
		return IRQ_NONE;
	}

	for (idx = 0; idx < 32; idx++) {
		if (((irq_f & (0x1 << idx)) != 0) || ((irq_f_p & (0x1 << idx)) != 0)) {
			switch (idx) {
			case IRQ_PMIF_ACC_VIO_V3:
				pmif_acc_vio_irq_handler(irq, data);
			break;
			case IRQ_WDT_V4:
				pmif_wdt_irq_handler(irq, data);
			break;
			case IRQ_ALL_PMIC_MPU_VIO_V4:
				pmif_pmif_acc_vio_irq_handler(irq, data);
			break;
			default:
				pr_notice("%s IRQ[%d] triggered\n",
					__func__, idx);
			break;
			}
			if (irq_f)
				pmif_writel(arb->pmif_base[0], arb, irq_f, PMIF_IRQ_CLR_0);
			else if (irq_f_p)
				pmif_writel(arb->pmif_base[1], arb, irq_f_p, PMIF_IRQ_CLR_0);
			else
				pr_notice("%s IRQ[%d] is not cleared due to empty flags\n",
					__func__, idx);
			break;
		}
	}
	mutex_unlock(&arb->pmif_m_mutex);
	__pm_relax(arb->pmif_m_Thread_lock);

	return IRQ_HANDLED;
}

static irqreturn_t pmif_event_1_irq_handler(int irq, void *data)
{
	struct pmif *arb = data;
	int irq_f = 0, irq_f_p = 0, idx = 0;

	__pm_stay_awake(arb->pmif_m_Thread_lock);
	mutex_lock(&arb->pmif_m_mutex);

	irq_f = pmif_readl(arb->pmif_base[0], arb, PMIF_IRQ_FLAG_1);
	if (!IS_ERR(arb->pmif_base[1]))
		irq_f_p = pmif_readl(arb->pmif_base[1], arb, PMIF_IRQ_FLAG_1);

	if ((irq_f == 0) && (irq_f_p == 0)) {
		mutex_unlock(&arb->pmif_m_mutex);
		__pm_relax(arb->pmif_m_Thread_lock);
		return IRQ_NONE;
	}

	for (idx = 0; idx < 32; idx++) {
		if (((irq_f & (0x1 << idx)) != 0) || ((irq_f_p & (0x1 << idx)) != 0)) {
			switch (idx) {
			default:
				pr_notice("%s IRQ[%d] triggered\n",
					__func__, idx);
			break;
			}
			if (irq_f)
				pmif_writel(arb->pmif_base[0], arb, irq_f, PMIF_IRQ_CLR_1);
			else if (irq_f_p)
				pmif_writel(arb->pmif_base[1], arb, irq_f_p, PMIF_IRQ_CLR_1);
			else
				pr_notice("%s IRQ[%d] is not cleared due to empty flags\n",
					__func__, idx);
			break;
		}
	}
	mutex_unlock(&arb->pmif_m_mutex);
	__pm_relax(arb->pmif_m_Thread_lock);

	return IRQ_HANDLED;
}

static irqreturn_t pmif_event_2_irq_handler(int irq, void *data)
{
	struct pmif *arb = data;
	int irq_f = 0, irq_f_p = 0, idx = 0;

	__pm_stay_awake(arb->pmif_m_Thread_lock);
	mutex_lock(&arb->pmif_m_mutex);

	irq_f = pmif_readl(arb->pmif_base[0], arb, PMIF_IRQ_FLAG_2);
	if (!IS_ERR(arb->pmif_base[1]))
		irq_f_p = pmif_readl(arb->pmif_base[1], arb, PMIF_IRQ_FLAG_2);

	if ((irq_f == 0) && (irq_f_p == 0)) {
		mutex_unlock(&arb->pmif_m_mutex);
		__pm_relax(arb->pmif_m_Thread_lock);
		return IRQ_NONE;
	}

	for (idx = 0; idx < 32; idx++) {
		if (((irq_f & (0x1 << idx)) != 0) || ((irq_f_p & (0x1 << idx)) != 0)) {
			switch (idx) {
			case IRQ_PMIC_CMD_ERR_PARITY_ERR:
				pmif_cmd_err_parity_err_irq_handler(irq, data);
			break;
			case IRQ_PMIF_ACC_VIO_V2:
				pmif_pmif_acc_vio_irq_handler(irq, data);
			break;
			case IRQ_PMIC_ACC_VIO:
				pmif_pmic_acc_vio_irq_handler(irq, data);
			break;
			default:
				pr_notice("%s IRQ[%d] triggered\n",
					__func__, idx);
			break;
			}
			if (irq_f)
				pmif_writel(arb->pmif_base[0], arb, irq_f, PMIF_IRQ_CLR_2);
			else if (irq_f_p)
				pmif_writel(arb->pmif_base[1], arb, irq_f_p, PMIF_IRQ_CLR_2);
			else
				pr_notice("%s IRQ[%d] is not cleared due to empty flags\n",
					__func__, idx);
			break;
		}
	}
	mutex_unlock(&arb->pmif_m_mutex);
	__pm_relax(arb->pmif_m_Thread_lock);

	return IRQ_HANDLED;
}

static irqreturn_t pmif_event_3_irq_handler(int irq, void *data)
{
	struct pmif *arb = data;
	int irq_f = 0, irq_f_p = 0, idx = 0;

	__pm_stay_awake(arb->pmif_m_Thread_lock);
	mutex_lock(&arb->pmif_m_mutex);

	irq_f = pmif_readl(arb->pmif_base[0], arb, PMIF_IRQ_FLAG_3);
	if (!IS_ERR(arb->pmif_base[1]))
		irq_f_p = pmif_readl(arb->pmif_base[1], arb, PMIF_IRQ_FLAG_3);

	if ((irq_f == 0) && (irq_f_p == 0)) {
		mutex_unlock(&arb->pmif_m_mutex);
		__pm_relax(arb->pmif_m_Thread_lock);
		return IRQ_NONE;
	}

	for (idx = 0; idx < 32; idx++) {
		if (((irq_f & (0x1 << idx)) != 0) || ((irq_f_p & (0x1 << idx)) != 0)) {
			switch (idx) {
			case IRQ_PMIF_SWINF_ACC_ERR_0:
			case IRQ_PMIF_SWINF_ACC_ERR_0_V2:
				pmif_swinf_acc_err_irq_handler(irq, data, 0x0);
				break;
			case IRQ_PMIF_SWINF_ACC_ERR_1:
			case IRQ_PMIF_SWINF_ACC_ERR_1_V2:
				pmif_swinf_acc_err_irq_handler(irq, data, 0x1);
				break;
			case IRQ_PMIF_SWINF_ACC_ERR_2:
			case IRQ_PMIF_SWINF_ACC_ERR_2_V2:
				pmif_swinf_acc_err_irq_handler(irq, data, 0x2);
				break;
			/* Use caps to distinguish platform if they have same irq number */
			case IRQ_LAT_LIMIT_REACHED:
			case IRQ_PMIF_SWINF_ACC_ERR_3_V2:
				if (arb->data->caps == 1)
					pmif_lat_limit_reached_irq_handler(irq, data);
				else
					pmif_swinf_acc_err_irq_handler(irq, data, 0x3);
				break;
			case IRQ_PMIF_SWINF_ACC_ERR_4:
			case IRQ_PMIF_SWINF_ACC_ERR_4_V2:
				pmif_swinf_acc_err_irq_handler(irq, data, 0x4);
				break;
			case IRQ_PMIF_SWINF_ACC_ERR_5:
			case IRQ_PMIF_SWINF_ACC_ERR_5_V2:
				pmif_swinf_acc_err_irq_handler(irq, data, 0x5);
				break;
			case IRQ_HW_MONITOR_V2:
			case IRQ_HW_MONITOR_V3:
				pmif_hw_monitor_irq_handler(irq, data);
				break;
			case IRQ_WDT_V2:
			case IRQ_WDT_V3:
				pmif_wdt_irq_handler(irq, data);
				break;
			case IRQ_PMIC_ACC_VIO_V2:
				pmif_pmic_acc_vio_irq_handler(irq, data);
				break;
			case IRQ_ALL_PMIC_MPU_VIO_V2:
			case IRQ_ALL_PMIC_MPU_VIO_V3:
				pmif_pmif_acc_vio_irq_handler(irq, data);
				break;
			default:
				pr_notice("%s IRQ[%d] triggered\n", __func__, idx);
				break;
			}
			/* Don't clear MD SW SWINF ACC ERR flag for re-send mechanism */
			if (irq_f) {
				if ((!(irq_f & (0x1 << IRQ_PMIF_SWINF_ACC_ERR_0))) &&
					(!(irq_f & (0x1 << IRQ_PMIF_SWINF_ACC_ERR_0_V2))))
					pmif_writel(arb->pmif_base[0], arb, irq_f, PMIF_IRQ_CLR_3);
			} else if (irq_f_p) {
				if ((!(irq_f_p & (0x1 << IRQ_PMIF_SWINF_ACC_ERR_0))) &&
					(!(irq_f & (0x1 << IRQ_PMIF_SWINF_ACC_ERR_0_V2))))
					pmif_writel(arb->pmif_base[1], arb, irq_f_p, PMIF_IRQ_CLR_3);
			} else
				pr_notice("%s IRQ[%d] is not cleared due to empty flags\n",
					__func__, idx);
			break;
		}
	}
	mutex_unlock(&arb->pmif_m_mutex);
	__pm_relax(arb->pmif_m_Thread_lock);

	return IRQ_HANDLED;
}

static irqreturn_t pmif_event_4_irq_handler(int irq, void *data)
{
	struct pmif *arb = data;
	int irq_f = 0, irq_f_p = 0, idx = 0;

	__pm_stay_awake(arb->pmif_m_Thread_lock);
	mutex_lock(&arb->pmif_m_mutex);

	irq_f = pmif_readl(arb->pmif_base[0], arb, PMIF_IRQ_FLAG_4);
	if (!IS_ERR(arb->pmif_base[1]))
		irq_f_p = pmif_readl(arb->pmif_base[1], arb, PMIF_IRQ_FLAG_4);

	if ((irq_f == 0) && (irq_f_p == 0)) {
		mutex_unlock(&arb->pmif_m_mutex);
		__pm_relax(arb->pmif_m_Thread_lock);
		return IRQ_NONE;
	}

	for (idx = 0; idx < 32; idx++) {
		if (((irq_f & (0x1 << idx)) != 0) || ((irq_f_p & (0x1 << idx)) != 0)) {
			switch (idx) {
			default:
				pr_notice("%s IRQ[%d] triggered\n",
					__func__, idx);
			break;
			}
			if (irq_f)
				pmif_writel(arb->pmif_base[0], arb, irq_f, PMIF_IRQ_CLR_4);
			else if (irq_f_p)
				pmif_writel(arb->pmif_base[1], arb, irq_f_p, PMIF_IRQ_CLR_4);
			else
				pr_notice("%s IRQ[%d] is not cleared due to empty flags\n",
					__func__, idx);
			break;
		}
	}
	mutex_unlock(&arb->pmif_m_mutex);
	__pm_relax(arb->pmif_m_Thread_lock);

	return IRQ_HANDLED;
}

static struct pmif_irq_desc pmif_event_irq[] = {
	PMIF_IRQDESC(event_0),
	PMIF_IRQDESC(event_1),
	PMIF_IRQDESC(event_2),
	PMIF_IRQDESC(event_3),
	PMIF_IRQDESC(event_4),
};

static void pmif_irq_register(struct platform_device *pdev,
		struct pmif *arb, int irq)
{
	int i = 0, ret = 0;
	u32 irq_event_en[5] = {0};

	for (i = 0; i < ARRAY_SIZE(pmif_event_irq); i++) {
		if (!pmif_event_irq[i].name)
			continue;
		ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
				pmif_event_irq[i].irq_handler,
				IRQF_TRIGGER_HIGH | IRQF_ONESHOT | IRQF_SHARED,
				pmif_event_irq[i].name, arb);
		if (ret < 0) {
			dev_notice(&pdev->dev, "request %s irq fail\n",
				pmif_event_irq[i].name);
			continue;
		}
		pmif_event_irq[i].irq = irq;
	}

	ret = of_property_read_u32_array(pdev->dev.of_node, "irq-event-en",
		irq_event_en, ARRAY_SIZE(irq_event_en));

	pmif_writel(arb->pmif_base[0], arb, irq_event_en[0] | pmif_readl(arb->pmif_base[0],
		arb, PMIF_IRQ_EVENT_EN_0), PMIF_IRQ_EVENT_EN_0);
	pmif_writel(arb->pmif_base[0], arb, irq_event_en[1] | pmif_readl(arb->pmif_base[0],
		arb, PMIF_IRQ_EVENT_EN_1), PMIF_IRQ_EVENT_EN_1);
	pmif_writel(arb->pmif_base[0], arb, irq_event_en[2] | pmif_readl(arb->pmif_base[0],
		arb, PMIF_IRQ_EVENT_EN_2), PMIF_IRQ_EVENT_EN_2);
	pmif_writel(arb->pmif_base[0], arb, irq_event_en[3] | pmif_readl(arb->pmif_base[0],
		arb, PMIF_IRQ_EVENT_EN_3), PMIF_IRQ_EVENT_EN_3);
	pmif_writel(arb->pmif_base[0], arb, irq_event_en[4] | pmif_readl(arb->pmif_base[0],
		arb, PMIF_IRQ_EVENT_EN_4), PMIF_IRQ_EVENT_EN_4);

	if (!IS_ERR(arb->pmif_base[1])) {
		pmif_writel(arb->pmif_base[1], arb, irq_event_en[0] | pmif_readl(arb->pmif_base[1],
			arb, PMIF_IRQ_EVENT_EN_0), PMIF_IRQ_EVENT_EN_0);
		pmif_writel(arb->pmif_base[1], arb, irq_event_en[1] | pmif_readl(arb->pmif_base[1],
			arb, PMIF_IRQ_EVENT_EN_1), PMIF_IRQ_EVENT_EN_1);
		pmif_writel(arb->pmif_base[1], arb, irq_event_en[2] | pmif_readl(arb->pmif_base[1],
			arb, PMIF_IRQ_EVENT_EN_2), PMIF_IRQ_EVENT_EN_2);
		pmif_writel(arb->pmif_base[1], arb, irq_event_en[3] | pmif_readl(arb->pmif_base[1],
			arb, PMIF_IRQ_EVENT_EN_3), PMIF_IRQ_EVENT_EN_3);
		pmif_writel(arb->pmif_base[1], arb, irq_event_en[4] | pmif_readl(arb->pmif_base[1],
			arb, PMIF_IRQ_EVENT_EN_4), PMIF_IRQ_EVENT_EN_4);
	}
}

static irqreturn_t rcs_irq_handler(int irq, void *data)
{
	struct pmif *arb = data;
	unsigned int slv_irq_sta, slv_irq_sta_p;
	int i;

	pr_err("songdebug: %s\n", __func__);
	for (i = 0; i < SPMI_MAX_SLAVE_ID; i++) {
		slv_irq_sta = mtk_spmi_readl(arb->spmimst_base[0], arb, SPMI_SLV_3_0_EINT + (i / 4));
		slv_irq_sta = (slv_irq_sta >> ((i % 4) * 8)) & 0xFF;

		if (!IS_ERR(arb->spmimst_base[1])) {
			slv_irq_sta_p = mtk_spmi_readl(arb->spmimst_base[1], arb, SPMI_SLV_3_0_EINT + (i / 4));
			slv_irq_sta_p = (slv_irq_sta_p >> ((i % 4) * 8)) & 0xFF;
		}

		/* need to clear using 0xFF to avoid new irq happen
		 * after read SPMI_SLV_3_0_EINT + (i/4) value then use
		 * this value to clean
		 */
		if (slv_irq_sta) {
			mtk_spmi_writel(arb->spmimst_base[0], arb, (0xFF << ((i % 4) * 8)),
					SPMI_SLV_3_0_EINT + (i / 4));
			if (arb->rcs_enable_hwirq[i] && slv_irq_sta) {
				dev_info(&arb->spmic->dev,
					"hwirq=%d, sta=0x%x\n", i, slv_irq_sta);
				handle_nested_irq(irq_find_mapping(arb->domain, i));
			}
		} else {
			if (!IS_ERR(arb->spmimst_base[1])) {
				mtk_spmi_writel(arb->spmimst_base[1], arb, (0xFF << ((i % 4) * 8)),
					SPMI_SLV_3_0_EINT + (i / 4));
				if (arb->rcs_enable_hwirq[i] && slv_irq_sta_p) {
					dev_info(&arb->spmic->dev,
						"hwirq=%d, sta=0x%x\n", i, slv_irq_sta_p);
					handle_nested_irq(irq_find_mapping(arb->domain, i));
				}
			}
		}
	}
	return IRQ_HANDLED;
}

static int rcs_irq_register(struct platform_device *pdev,
			    struct pmif *arb, int irq)
{
	int i, ret = 0;

	mutex_init(&arb->rcs_m_irqlock);
	mutex_init(&arb->rcs_p_irqlock);
	arb->rcs_enable_hwirq = devm_kcalloc(&pdev->dev, SPMI_MAX_SLAVE_ID,
					     sizeof(*arb->rcs_enable_hwirq),
					     GFP_KERNEL);
	if (!arb->rcs_enable_hwirq)
		return -ENOMEM;

	if (arb->rcs_irq == irq)
		arb->irq_chip = rcs_irq_chip;
	else if (arb->rcs_irq_p == irq)
		arb->irq_chip_p = rcs_irq_chip_p;
	else
		dev_notice(&pdev->dev, "no rcs irq %d registered\n", irq);

	arb->domain = irq_domain_add_linear(pdev->dev.of_node,
					    SPMI_MAX_SLAVE_ID,
					    &rcs_irq_domain_ops, arb);
	if (!arb->domain) {
		dev_notice(&pdev->dev, "Failed to create IRQ domain\n");
		return -ENODEV;
	}
	/* clear all slave irq status */
	for (i = 0; i < SPMI_MAX_SLAVE_ID; i++) {
		mtk_spmi_writel(arb->spmimst_base[0], arb, (0xFF << ((i % 4) * 8)),
				SPMI_SLV_3_0_EINT + (i / 4));
		if (!IS_ERR(arb->spmimst_base[1])) {
			mtk_spmi_writel(arb->spmimst_base[1], arb, (0xFF << ((i % 4) * 8)),
				SPMI_SLV_3_0_EINT + (i / 4));
		}
	}
	ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
					rcs_irq_handler, IRQF_ONESHOT,
					rcs_irq_chip.name, arb);
	if (ret < 0) {
		dev_notice(&pdev->dev, "Failed to request IRQ=%d, ret = %d\n",
			   irq, ret);
		return ret;
	}
	enable_irq_wake(irq);

	return ret;
}


static int mtk_spmi_probe(struct platform_device *pdev)
{
	struct pmif *arb;
	struct spmi_controller *ctrl;
	int err, i;
	u32 chan_offset;

	ctrl = spmi_controller_alloc(&pdev->dev, sizeof(*arb));
	if (!ctrl)
		return -ENOMEM;

	ctrl->cmd = pmif_arb_cmd;
	ctrl->read_cmd = pmif_spmi_read_cmd;
	ctrl->write_cmd = pmif_spmi_write_cmd;

	arb = spmi_controller_get_drvdata(ctrl);
	arb->spmic = ctrl;
	arb->data = device_get_match_data(&pdev->dev);
	if (!arb->data) {
		err = -EINVAL;
		dev_err(&pdev->dev, "Cannot get drv_data\n");
		goto err_put_ctrl;
	}

	arb->pmif_base[0] = devm_platform_ioremap_resource_byname(pdev, "pmif");
	if (IS_ERR(arb->pmif_base[0])) {
		err = PTR_ERR(arb->pmif_base[0]);
		goto err_put_ctrl;
	}

	arb->spmimst_base[0] = devm_platform_ioremap_resource_byname(pdev, "spmimst");
	if (IS_ERR(arb->spmimst_base[0])) {
		err = PTR_ERR(arb->spmimst_base[0]);
		goto err_put_ctrl;
	}

	arb->pmif_base[1] = devm_platform_ioremap_resource_byname(pdev, "pmif-p");
	if (IS_ERR(arb->pmif_base[1]))
		dev_notice(&pdev->dev, "[PMIF]:no pmif-p found\n");

	arb->spmimst_base[1] = devm_platform_ioremap_resource_byname(pdev, "spmimst-p");
	if (IS_ERR(arb->spmimst_base[1]))
		dev_notice(&pdev->dev, "[PMIF]:no spmimst-p found\n");

	if (arb->data->caps == 1) {

		arb->nclks = ARRAY_SIZE(pmif_clock_names);
		for (i = 0; i < arb->nclks; i++)
			arb->clks[i].id = pmif_clock_names[i];

		err = clk_bulk_get(&pdev->dev, arb->nclks, arb->clks);
		if (err) {
			dev_err(&pdev->dev, "Failed to get clocks: %d\n", err);
			goto err_put_ctrl;
		}

		err = clk_bulk_prepare_enable(arb->nclks, arb->clks);
		if (err) {
			dev_err(&pdev->dev, "Failed to enable clocks: %d\n", err);
			goto err_put_clks;
		}
	}

	chan_offset = PMIF_CHAN_OFFSET * arb->data->soc_chan;
	arb->chan.ch_sta = PMIF_SWINF_0_STA + chan_offset;
	arb->chan.wdata = PMIF_SWINF_0_WDATA_31_0 + chan_offset;
	arb->chan.rdata = PMIF_SWINF_0_RDATA_31_0 + chan_offset;
	arb->chan.ch_send = PMIF_SWINF_0_ACC + chan_offset;
	arb->chan.ch_rdy = PMIF_SWINF_0_VLD_CLR + chan_offset;

	raw_spin_lock_init(&arb->lock_m);
	raw_spin_lock_init(&arb->lock_p);

	arb->pmif_m_Thread_lock =
		wakeup_source_register(NULL, "pmif_m wakelock");
	arb->pmif_p_Thread_lock =
		wakeup_source_register(NULL, "pmif_p wakelock");
	mutex_init(&arb->pmif_m_mutex);
	mutex_init(&arb->pmif_p_mutex);

	if (arb->data->caps == 2) {
		arb->irq = platform_get_irq_byname(pdev, "pmif_irq");
		if (arb->irq < 0) {
			dev_notice(&pdev->dev,
				   "Failed to get pmif_irq, ret = %d\n", arb->irq);
		}
		pmif_irq_register(pdev, arb, arb->irq);

		arb->irq_p = platform_get_irq_byname(pdev, "pmif_p_irq");
		if (arb->irq_p < 0) {
			dev_notice(&pdev->dev,
				   "Failed to get pmif_p_irq, ret = %d\n", arb->irq_p);
		}
		pmif_irq_register(pdev, arb, arb->irq_p);

		arb->rcs_irq = platform_get_irq_byname(pdev, "rcs_irq");
		if (arb->rcs_irq < 0) {
			dev_notice(&pdev->dev,
				   "Failed to get rcs_irq, ret = %d\n", arb->rcs_irq);
		} else {
			err = rcs_irq_register(pdev, arb, arb->rcs_irq);
			if (err)
				dev_notice(&pdev->dev,
				   "Failed to register rcs_irq, ret = %d\n", arb->rcs_irq);
		}
		arb->rcs_irq_p = platform_get_irq_byname(pdev, "rcs_irq_p");
		if (arb->rcs_irq_p < 0) {
			dev_notice(&pdev->dev,
				   "Failed to get rcs_irq_p, ret = %d\n", arb->rcs_irq_p);
		} else {
			err = rcs_irq_register(pdev, arb, arb->rcs_irq_p);
			if (err)
				dev_notice(&pdev->dev,
				   "Failed to register rcs_irq_p, ret = %d\n", arb->rcs_irq_p);
		}

		arb->spmi_nack_irq = platform_get_irq_byname(pdev, "spmi_nack_irq");
		if (arb->spmi_nack_irq < 0) {
			dev_notice(&pdev->dev,
				"Failed to get spmi_nack_irq, ret = %d\n", arb->spmi_nack_irq);
		} else {
			err = spmi_nack_irq_register(pdev, arb, arb->spmi_nack_irq);
			if (err)
				dev_notice(&pdev->dev,
					"Failed to register spmi_nack_irq, ret = %d\n",
						 arb->spmi_nack_irq);
		}

		arb->spmi_p_nack_irq = platform_get_irq_byname(pdev, "spmi_p_nack_irq");
		if (arb->spmi_p_nack_irq < 0) {
			dev_notice(&pdev->dev,
				"Failed to get spmi_p_nack_irq, ret = %d\n", arb->spmi_p_nack_irq);
		} else {
			err = spmi_nack_irq_register(pdev, arb, arb->spmi_p_nack_irq);
			if (err)
				dev_notice(&pdev->dev,
					"Failed to register spmi_p_nack_irq, ret = %d\n",
						 arb->spmi_p_nack_irq);
		}
	}
	spmi_dev_parse(pdev);

	platform_set_drvdata(pdev, ctrl);

	err = spmi_controller_add(ctrl);
	if (err)
		goto err_domain_remove;

	return 0;

err_domain_remove:
	clk_bulk_disable_unprepare(arb->nclks, arb->clks);
err_put_clks:
	clk_bulk_put(arb->nclks, arb->clks);
err_put_ctrl:
	spmi_controller_put(ctrl);
	return err;
}

static void mtk_spmi_remove(struct platform_device *pdev)
{
	struct spmi_controller *ctrl = platform_get_drvdata(pdev);
	struct pmif *arb = spmi_controller_get_drvdata(ctrl);

	clk_bulk_disable_unprepare(arb->nclks, arb->clks);
	clk_bulk_put(arb->nclks, arb->clks);
	spmi_controller_remove(ctrl);
	spmi_controller_put(ctrl);
}

static const struct of_device_id mtk_spmi_match_table[] = {
	{
		.compatible = "mediatek,mt6873-spmi",
		.data = &mt6873_pmif_arb,
	}, {
		.compatible = "mediatek,mt8195-spmi",
		.data = &mt8195_pmif_arb,
	}, {
		.compatible = "mediatek,mt8196-spmi",
		.data = &mt6xxx_pmif_arb,
	}, {
		/* sentinel */
	},
};
MODULE_DEVICE_TABLE(of, mtk_spmi_match_table);

static struct platform_driver mtk_spmi_driver = {
	.driver		= {
		.name	= "spmi-mtk",
		.of_match_table = mtk_spmi_match_table,
	},
	.probe		= mtk_spmi_probe,
	.remove_new	= mtk_spmi_remove,
};
module_platform_driver(mtk_spmi_driver);

MODULE_AUTHOR("Lu Tang <lu.tang@mediatek.com>");
MODULE_DESCRIPTION("MediaTek SPMI Driver");
MODULE_LICENSE("GPL");
