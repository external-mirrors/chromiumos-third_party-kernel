// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 MediaTek Inc.
 */
#include <linux/arm-smccc.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <soc/mediatek/mtk_dvfsrc.h>
#include <linux/soc/mediatek/mtk_sip_svc.h>
#include <dt-bindings/soc/mtk,dvfsrc.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>

/* Private */
#define DVFSRC_OPP_BW_QUERY
#define DVFSRC_FORCE_OPP_SUPPORT
#define DVFSRC_PROPERTY_ENABLE
#define CREATE_TRACE_POINTS
#include <trace/events/mtk_qos_trace.h>
EXPORT_TRACEPOINT_SYMBOL_GPL(mtk_pm_qos_update_request);
/* End */

#define DVFSRC_IDLE     0x00
#define DVFSRC_GET_TARGET_LEVEL(x)  (((x) >> 0) & 0x0000ffff)
#define DVFSRC_GET_CURRENT_LEVEL(x) (((x) >> 16) & 0x0000ffff)
#define kbps_to_mbps(x) (div_u64(x, 1000))

#define MT8183_DVFSRC_OPP_LP4   0
#define MT8183_DVFSRC_OPP_LP4X  1
#define MT8183_DVFSRC_OPP_LP3   2

#define POLL_TIMEOUT        1000
#define STARTUP_TIME        1

#define MTK_SIP_DVFSRC_INIT		0x00
#define MTK_SIP_MEM_RS_REQ		0x40
#define MTK_SIP_MEM_RS_REL		0x41


#define DVFSRC_OPP_DESC(_opp_table)	\
{	\
	.opps = _opp_table,	\
	.num_opp = ARRAY_SIZE(_opp_table),	\
}

struct dvfsrc_opp {
	u32 vcore_opp;
	u32 dram_opp;
};

struct dvfsrc_domain {
	u32 id;
	u32 state;
};

struct dvfsrc_opp_desc {
	const struct dvfsrc_opp *opps;
	u32 num_opp;
};

struct mtk_dvfsrc;
struct dvfsrc_soc_data {
	const int *regs;
	u32 num_domains;
	struct dvfsrc_domain *domains;
	u32 num_opp_desc;
	u32 hrt_bw_unit;
	u32 qos_bw_unit;
	u32 force_ver;
	bool dis_ddr_check;
	bool mem_res_req_en;
	bool emi_ddr_bw_en;
	const struct dvfsrc_opp_desc *opps_desc;
	int (*get_target_level)(struct mtk_dvfsrc *dvfsrc);
	int (*get_current_level)(struct mtk_dvfsrc *dvfsrc);
	u32 (*get_vcore_level)(struct mtk_dvfsrc *dvfsrc);
	u32 (*get_vcp_level)(struct mtk_dvfsrc *dvfsrc);
	u32 (*get_dram_level)(struct mtk_dvfsrc *dvfsrc);
	void (*set_dram_bw)(struct mtk_dvfsrc *dvfsrc, u64 bw);
	void (*set_dram_peak_bw)(struct mtk_dvfsrc *dvfsrc, u64 bw);
	void (*set_dram_hrtbw)(struct mtk_dvfsrc *dvfsrc, u64 bw);
	void (*set_dram_level)(struct mtk_dvfsrc *dvfsrc, u32 level);
	void (*set_opp_level)(struct mtk_dvfsrc *dvfsrc, u32 level);
	void (*set_vcore_level)(struct mtk_dvfsrc *dvfsrc, u32 level);
	void (*set_emi_level)(struct mtk_dvfsrc *dvfsrc, u32 level);
	void (*set_vscp_level)(struct mtk_dvfsrc *dvfsrc, u32 level);
	int (*wait_for_opp_level)(struct mtk_dvfsrc *dvfsrc, u32 level);
	int (*wait_for_vcore_level)(struct mtk_dvfsrc *dvfsrc, u32 level);
	int (*wait_for_dram_level)(struct mtk_dvfsrc *dvfsrc, u32 level);
	u32 (*query_opp_count)(struct mtk_dvfsrc *dvfsrc);
#ifdef DVFSRC_FORCE_OPP_SUPPORT
	void (*set_force_opp_level)(struct mtk_dvfsrc *dvfsrc, u32 level);
#endif
};

struct mtk_dvfsrc {
	struct device *dev;
	struct platform_device *dvfsrc_start;
	struct platform_device *devfreq;
	struct platform_device *regulator;
	struct platform_device *icc;
	const struct dvfsrc_soc_data *dvd;
	int dram_type;
	int irq_counter;
	const struct dvfsrc_opp_desc *curr_opps;
	void __iomem *regs;
	spinlock_t req_lock;
	struct mutex pstate_lock;
	struct notifier_block scpsys_notifier;
	bool dvfsrc_enable;
#ifdef DVFSRC_FORCE_OPP_SUPPORT
	bool opp_forced;
	bool opp_force_lock;
	spinlock_t force_lock;
#endif
	bool disable_wait_level;
	u32 num_opp;
};

#ifdef DVFSRC_OPP_BW_QUERY
u32 dram_type;
static inline struct device_node *dvfsrc_parse_required_opp(
	struct device_node *np, int index)
{
	struct device_node *required_np;

	required_np = of_parse_phandle(np, "required-opps", index);
	if (unlikely(!required_np)) {
		pr_notice("%s: Unable to parse required-opps: %pOF, index: %d\n",
		       __func__, np, index);
	}
	return required_np;
}
u32 dvfsrc_get_required_opp_peak_bw(struct device_node *np, int index)
{
	struct device_node *required_np;
	u32 peak_bw = 0;

	required_np = dvfsrc_parse_required_opp(np, index);
	if (!required_np)
		return 0;

	if (of_property_read_u32_index(required_np, "opp-peak-KBps", dram_type, &peak_bw))
		pr_info("%s: get fail\n", __func__);

	of_node_put(required_np);
	return peak_bw;
}
EXPORT_SYMBOL(dvfsrc_get_required_opp_peak_bw);
#endif

static u32 dvfsrc_read(struct mtk_dvfsrc *dvfs, u32 offset)
{
	return readl(dvfs->regs + dvfs->dvd->regs[offset]);
}

static void dvfsrc_write(struct mtk_dvfsrc *dvfs, u32 offset, u32 val)
{
	writel(val, dvfs->regs + dvfs->dvd->regs[offset]);
}

#define dvfsrc_rmw(dvfs, offset, val, mask, shift) \
	dvfsrc_write(dvfs, offset, \
		(dvfsrc_read(dvfs, offset) & ~(mask << shift)) | (val << shift))

enum dvfsrc_regs {
	DVFSRC_BASIC_CONTROL,
	DVFSRC_SW_REQ,
	DVFSRC_SW_REQ2,
	DVFSRC_LEVEL,
	DVFSRC_TARGET_LEVEL,
	DVFSRC_SW_BW,
	DVFSRC_SW_PEAK_BW,
	DVFSRC_SW_HRT_BW,
	DVFSRC_VCORE_REQUEST,
	DVFSRC_LAST,
	DVFSRC_TARGET_FORCE,
	DVFSRC_FORCE_MASK,
	DVFSRC_TARGET_FORCE_H,
	DVFSRC_SW_FORCE_BW,
	DVFSRC_INT,
	DVFSRC_INT_EN,
	DVFSRC_INT_CLR,
	DVFSRC_DEFAULT_OPP_2,
	DVFSRC_DEFAULT_OPP_1,
	DVFSRC_HALT_CONTROL,
	DVFSRC_DEFAULT_OPP_3,
	DVFSRC_DEFAULT_OPP_4,
	DVFSRC_TARGET_FORCE_H1,
	DVFSRC_TARGET_FORCE_H2,
	DVFSRC_CUR_TAR_GEAR,
	DVFSRC_DEFAULT_OPP_5,
	DVFSRC_DEFAULT_OPP_6,
	DVFSRC_DEFAULT_OPP_7,
	DVFSRC_DEFAULT_OPP_8,
	DVFSRC_SW_EMI_BW,
};

static const int mt8196_regs[] = {
	[DVFSRC_BASIC_CONTROL] =    0x0,
	[DVFSRC_SW_REQ] =           0x18,
	[DVFSRC_SW_REQ2] =          0x604,
	[DVFSRC_LEVEL] =            0x5f0,
	[DVFSRC_SW_PEAK_BW] =       0x1f4,
	[DVFSRC_SW_BW] =            0x1e8,
	[DVFSRC_SW_HRT_BW] =        0x20c,
	[DVFSRC_TARGET_LEVEL] =     0x5f0,
	[DVFSRC_VCORE_REQUEST] =    0x80,
	[DVFSRC_TARGET_FORCE] =     0x774,
	[DVFSRC_TARGET_FORCE_H] =   0x770,
	[DVFSRC_TARGET_FORCE_H1] =  0x5e0,
	[DVFSRC_TARGET_FORCE_H2] =  0x5dc,
	[DVFSRC_FORCE_MASK] =       0x5ec,
	[DVFSRC_SW_FORCE_BW] =      0x200,
	[DVFSRC_INT] =              0xc8,
	[DVFSRC_INT_EN] =           0xcc,
	[DVFSRC_INT_CLR] =          0xd0,
	[DVFSRC_CUR_TAR_GEAR] =     0x6ac,
	[DVFSRC_DEFAULT_OPP_1] =    0x22c,
	[DVFSRC_DEFAULT_OPP_2] =    0x230,
	[DVFSRC_DEFAULT_OPP_3] =    0x740,
	[DVFSRC_DEFAULT_OPP_4] =    0x744,
	[DVFSRC_HALT_CONTROL]  =    0xc4,
	[DVFSRC_DEFAULT_OPP_5] =    0x828,
	[DVFSRC_DEFAULT_OPP_6] =    0x82c,
	[DVFSRC_DEFAULT_OPP_7] =    0x830,
	[DVFSRC_DEFAULT_OPP_8] =    0x834,
	[DVFSRC_SW_EMI_BW]     =    0x60c,
};

static int dvfsrc_is_idle(struct mtk_dvfsrc *dvfsrc)
{
	if (!dvfsrc->dvd->get_target_level)
		return true;

	return dvfsrc->dvd->get_target_level(dvfsrc);
}

static int dvfsrc_wait_for_idle(struct mtk_dvfsrc *dvfsrc)
{
	int state;

	return readx_poll_timeout_atomic(dvfsrc_is_idle, dvfsrc,
		state, state == DVFSRC_IDLE,
		STARTUP_TIME, POLL_TIMEOUT);
}

static u32 mt8196_get_vcore_level(struct mtk_dvfsrc *dvfsrc)
{
	return (dvfsrc_read(dvfsrc, DVFSRC_SW_REQ) >> 4) & 0x7;
}

static u32 mt8196_get_vcp_level(struct mtk_dvfsrc *dvfsrc)
{
	return (dvfsrc_read(dvfsrc, DVFSRC_VCORE_REQUEST) >> 12) & 0x7;
}

static void mt8196_set_vcore_level(struct mtk_dvfsrc *dvfsrc, u32 level)
{
	spin_lock(&dvfsrc->req_lock);
	dvfsrc_rmw(dvfsrc, DVFSRC_SW_REQ, level, 0x7, 4);
	spin_unlock(&dvfsrc->req_lock);
}

static u32 mt8196_get_dram_level(struct mtk_dvfsrc *dvfsrc)
{
	return (dvfsrc_read(dvfsrc, DVFSRC_SW_REQ) >> 12) & 0xf;
}

static void mt8196_set_dram_level(struct mtk_dvfsrc *dvfsrc, u32 level)
{
	spin_lock(&dvfsrc->req_lock);
	dvfsrc_rmw(dvfsrc, DVFSRC_SW_REQ, level, 0xf, 12);
	spin_unlock(&dvfsrc->req_lock);
}

static void mt8196_set_emi_level(struct mtk_dvfsrc *dvfsrc, u32 level)
{
	spin_lock(&dvfsrc->req_lock);
	dvfsrc_rmw(dvfsrc, DVFSRC_SW_REQ, level, 0xf, 0);
	spin_unlock(&dvfsrc->req_lock);
}

static int mt8196_get_target_level(struct mtk_dvfsrc *dvfsrc)
{
	u32 reg;

	reg = dvfsrc_read(dvfsrc, DVFSRC_TARGET_LEVEL);
	if (reg & (1 << 16))
		return ((reg >> 8) & 0xff) + 1;
	else
		return 0;
}

static int mt8196_get_current_level(struct mtk_dvfsrc *dvfsrc)
{
	u32 curr_level;

	curr_level = (dvfsrc_read(dvfsrc, DVFSRC_LEVEL) & 0xff) + 1;
	if (curr_level > dvfsrc->num_opp)
		curr_level = 0;
	else
		curr_level = dvfsrc->num_opp - curr_level;

	return curr_level;
}

static int mt8196_wait_for_vcore_level(struct mtk_dvfsrc *dvfsrc, u32 level)
{
	int val;

	return readl_poll_timeout_atomic(
			dvfsrc->regs + dvfsrc->dvd->regs[DVFSRC_CUR_TAR_GEAR],
			val, ((val >> 8) & 0x7) >= level, STARTUP_TIME, POLL_TIMEOUT);
}

static int mt8196_wait_for_dram_level(struct mtk_dvfsrc *dvfsrc, u32 level)
{
	int val;

	return readl_poll_timeout_atomic(
			dvfsrc->regs + dvfsrc->dvd->regs[DVFSRC_CUR_TAR_GEAR],
			val, (val & 0xf) >= level, STARTUP_TIME, POLL_TIMEOUT);
}

/* mt8196 specific */
static void mt8196_set_dram_bw(struct mtk_dvfsrc *dvfsrc, u64 bw)
{
	u32 qos_bw_unit = dvfsrc->dvd->qos_bw_unit;

	if (qos_bw_unit)
		bw = div_u64((kbps_to_mbps(bw) + qos_bw_unit - 1), qos_bw_unit);
	else
		bw = div_u64((kbps_to_mbps(bw) + 63), 64);

	bw = min_t(u64, bw, 0xffff);
	dvfsrc_write(dvfsrc, DVFSRC_SW_BW, bw);

	if (dvfsrc->dvd->emi_ddr_bw_en)
		dvfsrc_write(dvfsrc, DVFSRC_SW_EMI_BW, bw);
}

static void mt8196_set_dram_peak_bw(struct mtk_dvfsrc *dvfsrc, u64 bw)
{
	u32 qos_bw_unit = dvfsrc->dvd->qos_bw_unit;

	if (qos_bw_unit)
		bw = div_u64((kbps_to_mbps(bw) + qos_bw_unit - 1), qos_bw_unit);
	else
		bw = div_u64((kbps_to_mbps(bw) + 63), 64);

	bw = min_t(u64, bw, 0xffff);
	dvfsrc_write(dvfsrc, DVFSRC_SW_PEAK_BW, bw);
}

static void mt8196_set_dram_hrtbw(struct mtk_dvfsrc *dvfsrc, u64 bw)
{
	u32 hrt_bw_unit = dvfsrc->dvd->hrt_bw_unit;

	if (hrt_bw_unit)
		bw = div_u64((kbps_to_mbps(bw) + hrt_bw_unit - 1), hrt_bw_unit);
	else
		bw = div_u64((kbps_to_mbps(bw) + 29), 30);
	bw = min_t(u64, bw, 0x3ff);
	dvfsrc_write(dvfsrc, DVFSRC_SW_HRT_BW, bw);
}

static u32 mt8196_get_opp_count(struct mtk_dvfsrc *dvfsrc)
{
	u32 val = 0;

	val = ((dvfsrc_read(dvfsrc, DVFSRC_BASIC_CONTROL) >> 20) & 0xff) + 1;

	return val;
}

#ifdef DVFSRC_FORCE_OPP_SUPPORT
static const int mt8196_dfopp_reg[] = {
	DVFSRC_DEFAULT_OPP_8,
	DVFSRC_DEFAULT_OPP_7,
	DVFSRC_DEFAULT_OPP_6,
	DVFSRC_DEFAULT_OPP_5,
	DVFSRC_DEFAULT_OPP_4,
	DVFSRC_DEFAULT_OPP_3,
	DVFSRC_DEFAULT_OPP_2,
	DVFSRC_DEFAULT_OPP_1,
};

static void __mt8196_set_force_opp_level(struct mtk_dvfsrc *dvfsrc, u32 level)
{
	u32 i, idx, shift;

	idx   = level / 32;
	shift = level % 32;
	dvfsrc_write(dvfsrc, mt8196_dfopp_reg[idx], (1 << shift));

	for (i = 0; i < ARRAY_SIZE(mt8196_dfopp_reg); i++) {
		if (i == idx)
			continue;
		else
			dvfsrc_write(dvfsrc, mt8196_dfopp_reg[i], 0);
	}
}

static void mt8196_set_force_opp_level(struct mtk_dvfsrc *dvfsrc, u32 level)
{
	unsigned long flags;
	int val;
	int ret = 0;
	struct arm_smccc_res ares;

	if (dvfsrc->num_opp == 0)
		return;

	spin_lock_irqsave(&dvfsrc->force_lock, flags);
	if (dvfsrc->opp_force_lock) {
		dvfsrc->opp_force_lock = true;
		spin_unlock_irqrestore(&dvfsrc->force_lock, flags);
		dev_info(dvfsrc->dev, "[Lock] force mode change\n");
		return;
	}
	if (level > dvfsrc->num_opp - 1) {
		if (dvfsrc->opp_forced) {
			dvfsrc_rmw(dvfsrc, DVFSRC_HALT_CONTROL, 1, 0x1, 1);
			udelay(STARTUP_TIME);
			dvfsrc_wait_for_idle(dvfsrc);
			if (dvfsrc->dvd->force_ver == 0x8196)
				__mt8196_set_force_opp_level(dvfsrc, 0);
			dvfsrc_write(dvfsrc, DVFSRC_SW_REQ2, 0x0);
			dvfsrc_rmw(dvfsrc, DVFSRC_HALT_CONTROL, 0, 0x1, 1);
			dvfsrc->opp_forced = false;
		}
		goto out;
	}
	dvfsrc->opp_forced = true;
	if (dvfsrc->dvd->mem_res_req_en)
		arm_smccc_smc(MTK_SIP_VCOREFS_CONTROL, MTK_SIP_MEM_RS_REQ,
			0, 0, 0, 0, 0, 0, &ares);

	dvfsrc_rmw(dvfsrc, DVFSRC_HALT_CONTROL, 1, 0x1, 1);
	udelay(STARTUP_TIME);
	dvfsrc_wait_for_idle(dvfsrc);
	dvfsrc_write(dvfsrc, DVFSRC_SW_REQ2, 0xffffffff);
	if (dvfsrc->dvd->force_ver == 0x8196)
		__mt8196_set_force_opp_level(dvfsrc, level);
	dvfsrc_rmw(dvfsrc, DVFSRC_HALT_CONTROL, 0, 0x1, 1);
	ret = readl_poll_timeout_atomic(
			dvfsrc->regs + dvfsrc->dvd->regs[DVFSRC_LEVEL],
			val, (val & 0x7f) == level, STARTUP_TIME, POLL_TIMEOUT);

	if (dvfsrc->dvd->mem_res_req_en)
		arm_smccc_smc(MTK_SIP_VCOREFS_CONTROL, MTK_SIP_MEM_RS_REL,
			0, 0, 0, 0, 0, 0, &ares);
out:
	spin_unlock_irqrestore(&dvfsrc->force_lock, flags);
	if (ret < 0) {
		dev_info(dvfsrc->dev,
			"[%s] wait idle, level: %d, last: %d -> %x\n",
			__func__, level,
			dvfsrc->dvd->get_current_level(dvfsrc),
			dvfsrc->dvd->get_target_level(dvfsrc));
#ifdef DVFSRC_DEBUG_ENHANCE
		mtk_dvfsrc_dump_notify(dvfsrc, 0);
		mtk_dvfsrc_aee_notify(dvfsrc, DVFSRC_AEE_FORCE_ERROR);
#endif
	}
}
#endif	/* DVFSRC_FORCE_OPP_SUPPORT */

/* Request handler */
void mtk_dvfsrc_send_request(const struct device *dev, u32 cmd, u64 data)
{
	int ret = 0;
	struct arm_smccc_res ares;
	struct mtk_dvfsrc *dvfsrc = dev_get_drvdata(dev);

	switch (cmd) {
	case MTK_DVFSRC_CMD_BW_REQUEST:
		dvfsrc->dvd->set_dram_bw(dvfsrc, data);
		goto out;
	case MTK_DVFSRC_CMD_PEAK_BW_REQUEST:
		if (dvfsrc->dvd->set_dram_peak_bw)
			dvfsrc->dvd->set_dram_peak_bw(dvfsrc, data);
		goto out;
	case MTK_DVFSRC_CMD_OPP_REQUEST:
		if (dvfsrc->dvd->set_opp_level)
			dvfsrc->dvd->set_opp_level(dvfsrc, data);
		break;
	case MTK_DVFSRC_CMD_VCORE_REQUEST:
		dvfsrc->dvd->set_vcore_level(dvfsrc, data);
		break;
	case MTK_DVFSRC_CMD_HRTBW_REQUEST:
		if (dvfsrc->dvd->set_dram_hrtbw)
			dvfsrc->dvd->set_dram_hrtbw(dvfsrc, data);
		else
			goto out;
		break;
	case MTK_DVFSRC_CMD_EMICLK_REQUEST:
		if (dvfsrc->dvd->set_emi_level)
			dvfsrc->dvd->set_emi_level(dvfsrc, data);
		else
			goto out;
		break;
	case MTK_DVFSRC_CMD_DRAM_REQUEST:
		dvfsrc->dvd->set_dram_level(dvfsrc, data);
		break;
	case MTK_DVFSRC_CMD_VSCP_REQUEST:
		dvfsrc->dvd->set_vscp_level(dvfsrc, data);
		break;
#ifdef DVFSRC_FORCE_OPP_SUPPORT
	case MTK_DVFSRC_CMD_FORCEOPP_REQUEST:
		if ((dvfsrc->dvd->set_force_opp_level) && dvfsrc->dvfsrc_enable)
			dvfsrc->dvd->set_force_opp_level(dvfsrc, data);
		goto out;
#endif
	default:
		dev_err(dvfsrc->dev, "unknown command: %d\n", cmd);
		goto out;
	}

	if (!dvfsrc->dvfsrc_enable)
		return;

#ifdef DVFSRC_FORCE_OPP_SUPPORT
	if (dvfsrc->opp_forced)
		return;
#endif
	/* DVFSRC need to wait at least 2T(~196ns) to handle request
	 * after receiving command
	 */
	if (dvfsrc->dvd->mem_res_req_en && (cmd == MTK_DVFSRC_CMD_VCORE_REQUEST))
		arm_smccc_smc(MTK_SIP_VCOREFS_CONTROL, MTK_SIP_MEM_RS_REQ,
		0, 0, 0, 0, 0, 0, &ares);

	udelay(STARTUP_TIME);
	dvfsrc_wait_for_idle(dvfsrc);
	/* The previous change may be requested by previous request.
	 * So we delay 1us , then start checking opp is reached enough.
	 */
	udelay(STARTUP_TIME);

	switch (cmd) {
	case MTK_DVFSRC_CMD_OPP_REQUEST:
		if (dvfsrc->dvd->wait_for_opp_level)
			ret = dvfsrc->dvd->wait_for_opp_level(dvfsrc, data);
		break;
	case MTK_DVFSRC_CMD_VCORE_REQUEST:
	case MTK_DVFSRC_CMD_VSCP_REQUEST:
		ret = dvfsrc->dvd->wait_for_vcore_level(dvfsrc, data);
#ifdef DVFSRC_DEBUG_ENHANCE
		mtk_dvfsrc_vcore_check(dvfsrc, data);
#endif
		break;
	case MTK_DVFSRC_CMD_DRAM_REQUEST:
		if (!dvfsrc->disable_wait_level)
			ret = dvfsrc->dvd->wait_for_dram_level(dvfsrc, data);
		break;
	}

	if (dvfsrc->dvd->mem_res_req_en && (cmd == MTK_DVFSRC_CMD_VCORE_REQUEST))
		arm_smccc_smc(MTK_SIP_VCOREFS_CONTROL, MTK_SIP_MEM_RS_REL,
		0, 0, 0, 0, 0, 0, &ares);
out:
#ifdef DVFSRC_FORCE_OPP_SUPPORT
	if (dvfsrc->opp_forced)
		return;
#endif

	if (ret < 0) {
		dev_warn(dvfsrc->dev,
			 "%d: idle timeout, data: %llu, last: %d -> %d\n",
			 cmd, data,
			 dvfsrc->dvd->get_current_level(dvfsrc),
			 dvfsrc->dvd->get_target_level(dvfsrc));
#ifdef DVFSRC_DEBUG_ENHANCE
		mtk_dvfsrc_dump_notify(dvfsrc, 0);
		mtk_dvfsrc_aee_notify(dvfsrc, DVFSRC_AEE_LEVEL_ERROR);
#endif
	}
}
EXPORT_SYMBOL(mtk_dvfsrc_send_request);

int mtk_dvfsrc_query_info(const struct device *dev, u32 cmd, int *data)
{
	struct mtk_dvfsrc *dvfsrc = dev_get_drvdata(dev);

	switch (cmd) {
	case MTK_DVFSRC_CMD_VCORE_LEVEL_QUERY:
		*data = dvfsrc->dvd->get_vcore_level(dvfsrc);
		break;
	case MTK_DVFSRC_CMD_VSCP_LEVEL_QUERY:
		*data = dvfsrc->dvd->get_vcp_level(dvfsrc);
		break;
	case MTK_DVFSRC_CMD_DRAM_LEVEL_QUERY:
		*data = dvfsrc->dvd->get_dram_level(dvfsrc);
		break;
	case MTK_DVFSRC_CMD_CURR_LEVEL_QUERY:
		*data = dvfsrc->dvd->get_current_level(dvfsrc);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(mtk_dvfsrc_query_info);

static irqreturn_t mtk_dvfsrc_irq_handler_thread(int irq, void *data)
{
	struct mtk_dvfsrc *dvfsrc = data;
	u32 val;

	val = dvfsrc_read(dvfsrc, DVFSRC_INT);
	dvfsrc_write(dvfsrc, DVFSRC_INT_CLR, val);
	dvfsrc_write(dvfsrc, DVFSRC_INT_CLR, 0x0);

	if (val & 0x2) {
		dev_info(dvfsrc->dev, "DVFSRC Timeout Handler %d !\n", dvfsrc->irq_counter);
		if (dvfsrc->irq_counter == 3) {
			dvfsrc_write(dvfsrc, DVFSRC_INT_EN,
					dvfsrc_read(dvfsrc, DVFSRC_INT_EN) & ~0x2);
			dev_info(dvfsrc->dev, "DVFSRC Timeout IRQ Disable\n");
		}
		dvfsrc->irq_counter++;
	}

	return IRQ_HANDLED;
}

static int mtk_dvfsrc_probe(struct platform_device *pdev)
{
	struct arm_smccc_res ares;
	struct mtk_dvfsrc *dvfsrc;
	int ret;
#ifdef DVFSRC_PROPERTY_ENABLE
	struct device_node *np = pdev->dev.of_node;
#endif
	dvfsrc = devm_kzalloc(&pdev->dev, sizeof(*dvfsrc), GFP_KERNEL);
	if (!dvfsrc)
		return -ENOMEM;

	dvfsrc->dvd = of_device_get_match_data(&pdev->dev);
	if (dvfsrc->dvd == NULL) {
		ret = -EINVAL;
		goto err;
	}
	dvfsrc->dev = &pdev->dev;

	dvfsrc->regs = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(dvfsrc->regs))
		return PTR_ERR(dvfsrc->regs);

	spin_lock_init(&dvfsrc->req_lock);
	mutex_init(&dvfsrc->pstate_lock);
#ifdef DVFSRC_FORCE_OPP_SUPPORT
	spin_lock_init(&dvfsrc->force_lock);
#endif

#ifdef DVFSRC_PROPERTY_ENABLE
	if (dvfsrc->dvd->dis_ddr_check)
		dvfsrc->disable_wait_level = dvfsrc->dvd->dis_ddr_check;
	else
		dvfsrc->disable_wait_level = of_property_read_bool(np, "disable-wait-level");

	arm_smccc_smc(MTK_SIP_VCOREFS_CONTROL, MTK_SIP_DVFSRC_INIT,
			  0, 0, 0, 0, 0, 0, &ares);
	if (!ares.a0) {
		dvfsrc->dram_type = ares.a1;
		dvfsrc->dvfsrc_enable = true;
	} else
		dev_info(dvfsrc->dev, "dvfs mode is disabled\n");
#else
	arm_smccc_smc(MTK_SIP_VCOREFS_CONTROL, MTK_SIP_DVFSRC_INIT, 0, 0, 0,
		0, 0, 0, &ares);

	if (!ares.a0) {
		dvfsrc->dram_type = ares.a1;
		dvfsrc->dvfsrc_enable = true;
	} else
		dev_info(dvfsrc->dev, "dvfs mode is disabled\n");
#endif

#ifdef DVFSRC_OPP_BW_QUERY
	dram_type = dvfsrc->dram_type;
#endif
	if (dvfsrc->dvd->query_opp_count)
		dvfsrc->num_opp = dvfsrc->dvd->query_opp_count(dvfsrc);

	dvfsrc->curr_opps = &dvfsrc->dvd->opps_desc[dvfsrc->dram_type];
	platform_set_drvdata(pdev, dvfsrc);

	ret = devm_request_threaded_irq(dvfsrc->dev, platform_get_irq(pdev, 0),
					NULL, mtk_dvfsrc_irq_handler_thread,
					IRQF_ONESHOT | IRQF_TRIGGER_NONE,
					"dvfsrc", dvfsrc);
	if (!ret) {
		dvfsrc->irq_counter = 0;
		dvfsrc_write(dvfsrc, DVFSRC_INT_EN, dvfsrc_read(dvfsrc, DVFSRC_INT_EN) | 0x2);
		dev_info(dvfsrc->dev, "DVFSRC IRQ Enable\n");
	}

	dvfsrc->regulator = platform_device_register_data(dvfsrc->dev,
			"mtk-dvfsrc-regulator", -1, NULL, 0);
	if (IS_ERR(dvfsrc->regulator)) {
		dev_err(dvfsrc->dev, "Failed create regulator device\n");
		ret = PTR_ERR(dvfsrc->regulator);
		goto err;
	}

	dvfsrc->icc = platform_device_register_data(dvfsrc->dev,
			"mediatek-emi-icc", -1, NULL, 0);
	if (IS_ERR(dvfsrc->icc)) {
		dev_err(dvfsrc->dev, "Failed create icc device\n");
		ret = PTR_ERR(dvfsrc->icc);
		goto unregister_regulator;
	}

	dvfsrc->dvfsrc_start = platform_device_register_data(dvfsrc->dev,
			"mtk-dvfsrc-start", -1, NULL, 0);
	if (IS_ERR(dvfsrc->dvfsrc_start)) {
		dev_err(dvfsrc->dev, "Failed create dvfsrc-start device\n");
		ret = PTR_ERR(dvfsrc->dvfsrc_start);
		goto unregister_icc;
	}

	ret = devm_of_platform_populate(dvfsrc->dev);
	if (ret < 0)
		goto unregister_start;

	return 0;

unregister_start:
	platform_device_unregister(dvfsrc->dvfsrc_start);
unregister_icc:
	platform_device_unregister(dvfsrc->icc);
unregister_regulator:
	platform_device_unregister(dvfsrc->regulator);
err:
	return ret;
}

#define DVFSRC_MT8196_SERIES_OPS			\
	.get_target_level = mt8196_get_target_level,	\
	.get_current_level = mt8196_get_current_level,	\
	.get_vcore_level = mt8196_get_vcore_level,	\
	.get_vcp_level = mt8196_get_vcp_level,		\
	.get_dram_level = mt8196_get_dram_level,	\
	.set_dram_bw = mt8196_set_dram_bw,		\
	.set_dram_peak_bw = mt8196_set_dram_peak_bw,	\
	.set_dram_level = mt8196_set_dram_level,	\
	.set_emi_level = mt8196_set_emi_level,		\
	.set_dram_hrtbw = mt8196_set_dram_hrtbw,	\
	.set_vcore_level = mt8196_set_vcore_level,	\
	.wait_for_vcore_level = mt8196_wait_for_vcore_level,	\
	.wait_for_dram_level = mt8196_wait_for_dram_level

static const struct dvfsrc_opp_desc dvfsrc_opp_mt8196_desc[] = {
	{
		.opps = NULL,
	}
};

static const struct dvfsrc_soc_data mt8196_data = {
	DVFSRC_MT8196_SERIES_OPS,
	.opps_desc = dvfsrc_opp_mt8196_desc,
	.num_opp_desc = ARRAY_SIZE(dvfsrc_opp_mt8196_desc),
	.regs = mt8196_regs,
#ifdef DVFSRC_FORCE_OPP_SUPPORT
	.set_force_opp_level = mt8196_set_force_opp_level,
	.force_ver = 0x8196,
#endif
	.query_opp_count = mt8196_get_opp_count,
	.dis_ddr_check = true,
	.mem_res_req_en = true,
	.emi_ddr_bw_en = true,
};

static int mtk_dvfsrc_remove(struct platform_device *pdev)
{
	struct mtk_dvfsrc *dvfsrc = platform_get_drvdata(pdev);

	platform_device_unregister(dvfsrc->regulator);
	platform_device_unregister(dvfsrc->icc);

	return 0;
}

static const struct of_device_id mtk_dvfsrc_of_match[] = {
	{
		.compatible = "mediatek,mt8196-dvfsrc",
		.data = &mt8196_data,
	}, {
		/* sentinel */
	},
};

static struct platform_driver mtk_dvfsrc_driver = {
	.probe	= mtk_dvfsrc_probe,
	.remove	= mtk_dvfsrc_remove,
	.driver = {
		.name = "mtk-dvfsrc",
		.of_match_table = of_match_ptr(mtk_dvfsrc_of_match),
	},
};

module_platform_driver(mtk_dvfsrc_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MTK DVFSRC driver");
MODULE_SOFTDEP("post: mtk-dvfsrc-regulator");
MODULE_SOFTDEP("post: mtk-dvfsrc-devfreq");
MODULE_SOFTDEP("post: mtk-emi");
