// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2025 MediaTek Inc.
 */

#include <dt-bindings/clock/mmdvfs-clk.h>
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/sched/clock.h>
#include <linux/soc/mediatek/mtk_mmdvfs.h>
#include <linux/workqueue.h>
#include <soc/mediatek/mmdebug.h>
#include <soc/mediatek/mmdvfs_v3.h>
#include <soc/mediatek/mmdvfs_v3_memory.h>

#include "mtk-mmdvfs-debug.h"

#define MTK_MMDEBUG_MAX_OPP             (8)
#define MMDVFS_DEBUG_MAX_RETRY          (100)

#define mtk_mmdebug_err(dev, fmt, args...)                                              \
	dev_err(dev, "[mmdebug err]%s %d: " fmt "\n", __func__, __LINE__, ##args)

#define mtk_mmdebug_dbg(dev, fmt, args...)                    \
do {                                                                      \
	dev_dbg(dev, "[mmdebug dbg]%s %d: " fmt "\n", \
		 __func__, __LINE__, ##args);                     \
} while (0)

#define MMDVFS_DBG_VER1	BIT(0)
#define MMDVFS_DBG_VER3	BIT(1)

#define mmdvfs_debug_dump_line(file, fmt, args...)	\
({							\
	if (file)					\
		seq_printf(file, fmt "\n", ##args);	\
})

struct mmdvfs_record {
	u64 sec;
	u64 nsec;
	u8 opp;
};

struct mtk_mmdebug_dev {
	struct device *dev;
	struct dentry *file;
	u32 debug_version;

	/* MMDVFS_DBG_VER1 */
	u32 force_step0;
	u32 release_step0;
	u32 vote_step;
	u32 force_step;

	spinlock_t lock;
	u8 rec_cnt;
	struct mmdvfs_record rec[MEM_REC_CNT_MAX];

	/* MMDVFS_DBG_VER3 */
	u32 use_v3_pwr;

	/* regulator */
	struct regulator *reg_vcore;
	struct regulator *reg_vmm;
	struct regulator *reg_vdisp;
	struct notifier_block mmdebug_nb;

	/* user & power id mapping*/
	u8 user_pwr[USER_NUM];
	bool mmdvfs_v3_debug_init_done;
};

static inline struct mtk_mmdebug_dev *mtk_mmdebug_get_drv_data(void)
{
	struct device_node *np;
	struct platform_device *pdev;

	np = of_find_compatible_node(NULL, NULL, "mediatek,mmdvfs-debug");
	if (!np) {
		pr_err("can't get mmdvfs debug node.\n");
		return NULL;
	}

	pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (!pdev) {
		pr_err("can't get mmdvfs debug device.\n");
		return NULL;
	}

	return platform_get_drvdata(pdev);
}

static int mmdvfs_debug_set_force_step(const char *val, const struct kernel_param *kp)
{
	struct mtk_mmdebug_dev *mmdebug_dev = mtk_mmdebug_get_drv_data();
	u8 idx = 0;
	s8 opp = 0;
	int ret;

	ret = sscanf(val, "%hhu %hhd", &idx, &opp);
	if (ret != 2 || idx >= PWR_MMDVFS_NUM) {
		mtk_mmdebug_err(mmdebug_dev->dev, "failed:%d idx:%u opp:%d", ret, idx, opp);
		return -EINVAL;
	}

	if (!mmdvfs_is_init_done())
		return -ENODEV;

	mtk_mmdebug_dbg(mmdebug_dev->dev, "idx:%x mmdebug_dev->debug_version:%d opp:%d",
			idx, mmdebug_dev->debug_version, opp);
	if (mmdebug_dev->debug_version & MMDVFS_DBG_VER3)
		mtk_mmdvfs_v3_set_force_step(idx, opp, true);

	return 0;
}

const struct kernel_param_ops mmdvfs_debug_set_force_step_ops = {
	.set = mmdvfs_debug_set_force_step,
};
module_param_cb(force_step, &mmdvfs_debug_set_force_step_ops, NULL, 0644);
MODULE_PARM_DESC(force_step, "force mmdvfs to specified step");

static int mmdvfs_debug_set_vote_step(const char *val, const struct kernel_param *kp)
{
	struct mtk_mmdebug_dev *mmdebug_dev = mtk_mmdebug_get_drv_data();
	u8 idx = 0;
	s8 opp = 0;
	int ret;

	ret = sscanf(val, "%hhu %hhd", &idx, &opp);
	if (ret != 2 || idx > PWR_MMDVFS_NUM) {
		mtk_mmdebug_err(mmdebug_dev->dev, "failed:%d idx:%u opp:%d", ret, idx, opp);
		return -EINVAL;
	}

	if (!mmdvfs_is_init_done())
		return -ENODEV;

	mtk_mmdebug_dbg(mmdebug_dev->dev, "idx:%x debug_version:%d",
			idx, mmdebug_dev->debug_version);
	if (mmdebug_dev->debug_version & MMDVFS_DBG_VER3)
		mtk_mmdvfs_v3_set_vote_step(idx, opp, true);

	return 0;
}

const struct kernel_param_ops mmdvfs_debug_set_vote_step_ops = {
	.set = mmdvfs_debug_set_vote_step,
};
module_param_cb(vote_step, &mmdvfs_debug_set_vote_step_ops, NULL, 0644);
MODULE_PARM_DESC(vote_step, "vote mmdvfs to specified step");

void mmdvfs_debug_status_dump(struct seq_file *file)
{
	struct mtk_mmdebug_dev *mmdebug_dev = mtk_mmdebug_get_drv_data();
	unsigned long flags;
	u32 i, j, k, val;

	if (!mmdebug_dev)
		return;

	spin_lock_irqsave(&mmdebug_dev->lock, flags);
	mmdvfs_debug_dump_line(file, "VER1: mux controlled by vcore regulator:");

	if (mmdebug_dev->rec[mmdebug_dev->rec_cnt].sec) {
		for (i = mmdebug_dev->rec_cnt; i < ARRAY_SIZE(mmdebug_dev->rec); i++)
			mmdvfs_debug_dump_line(file, "[%5llu.%06llu] opp:%u",
					       mmdebug_dev->rec[i].sec, mmdebug_dev->rec[i].nsec,
					       mmdebug_dev->rec[i].opp);
	}

	for (i = 0; i < mmdebug_dev->rec_cnt; i++)
		mmdvfs_debug_dump_line(file, "[%5llu.%06llu] opp:%u",
				       mmdebug_dev->rec[i].sec, mmdebug_dev->rec[i].nsec,
				       mmdebug_dev->rec[i].opp);

	spin_unlock_irqrestore(&mmdebug_dev->lock, flags);

	if (!MEM_BASE)
		return;

	mmdvfs_debug_dump_line(file, "VER3: mux controlled by vcp:");

	/* power opp */
	i = readl(MEM_REC_PWR_CNT) % MEM_REC_CNT_MAX;
	if (readl(MEM_REC_PWR_SEC(i))) {
		for (j = i; j < MEM_REC_CNT_MAX; j++)
			mmdvfs_debug_dump_line(file, "[%5u.%3u] pwr:%u opp:%u",
					       readl(MEM_REC_PWR_SEC(j)),
					       readl(MEM_REC_PWR_NSEC(j)),
					       readl(MEM_REC_PWR_ID(j)), readl(MEM_REC_PWR_OPP(j)));
	}

	for (j = 0; j < i; j++)
		mmdvfs_debug_dump_line(file, "[%5u.%3u] pwr:%u opp:%u",
				       readl(MEM_REC_PWR_SEC(j)), readl(MEM_REC_PWR_NSEC(j)),
				       readl(MEM_REC_PWR_ID(j)), readl(MEM_REC_PWR_OPP(j)));

	/* user opp */
	i = readl(MEM_REC_USR_CNT) % MEM_REC_CNT_MAX;
	if (readl(MEM_REC_USR_SEC(i))) {
		for (j = i; j < MEM_REC_CNT_MAX; j++)
			mmdvfs_debug_dump_line(file, "[%5u.%3u] pwr:%u usr:%u opp:%u",
					       readl(MEM_REC_USR_SEC(j)),
					       readl(MEM_REC_USR_NSEC(j)),
					       readl(MEM_REC_USR_PWR(j)),
					       readl(MEM_REC_USR_ID(j)),
					       readl(MEM_REC_USR_OPP(j)));
	}

	for (j = 0; j < i; j++)
		mmdvfs_debug_dump_line(file, "[%5u.%3u] pwr:%u usr:%u opp:%u",
				       readl(MEM_REC_USR_SEC(j)), readl(MEM_REC_USR_NSEC(j)),
				       readl(MEM_REC_USR_PWR(j)),
				       readl(MEM_REC_USR_ID(j)), readl(MEM_REC_USR_OPP(j)));

	/* user latest request opp */
	mmdvfs_debug_dump_line(file, "user latest request opp");
	for (i = 0; i < USER_NUM; i++)
		mmdvfs_debug_dump_line(file, "user: %u opp: %u", i, readl(MEM_VOTE_OPP_USR(i)));

	mmdvfs_debug_dump_line(file, "clkmux:%#x clkmux_done:%#x",
			       readl(MEM_CLKMUX_ENABLE), readl(MEM_CLKMUX_ENABLE_DONE));

	/* MMDVFS_DBG_VER3.5 */
	mmdvfs_debug_dump_line(file, "VER3.5: mux controlled by vcp:");

	/* user latest request freq/opp */
	mmdvfs_debug_dump_line(file, "user latest request opp/freq");
	for (i = 0; i < MMDVFS_USER_NUM; i++) {
		bool vcp = readl(MEM_USR_OPP(i, true)) != MTK_MMDEBUG_MAX_OPP;

		mmdvfs_debug_dump_line(file, "[%5u.%6u] user: %2u opp: %u freq: %u",
				       readl(MEM_USR_OPP_SEC(i, vcp)),
				       readl(MEM_USR_OPP_USEC(i, vcp)), i,
				       readl(MEM_USR_OPP(i, vcp)),
				       readl(MEM_USR_FREQ(i, vcp)));
	}

	if (mmdvfs_get_mmup_sram_enable())
		goto sram_dump;

	/* power opp/gear */
	mmdvfs_debug_dump_line(file, "power latest opp/gear");
	for (i = 0; i < PWR_MMDVFS_NUM; i++)
		mmdvfs_debug_dump_line(file, "[%5u.%6u] power: %u opp: %u gear: %u",
				       readl(MEM_PWR_OPP_SEC(i)), readl(MEM_PWR_OPP_USEC(i)), i,
				       readl(MEM_PWR_OPP(i)), readl(MEM_PWR_CUR_GEAR(i)));

	/* mux opp/min */
	mmdvfs_debug_dump_line(file, "mux latest opp/min");
	for (i = 0; i < USER_NUM; i++)
		mmdvfs_debug_dump_line(file, "[%5u.%6u] mux: %2u opp: %u min: %u",
				       readl(MEM_MUX_OPP_SEC(i)), readl(MEM_MUX_OPP_USEC(i)), i,
				       readl(MEM_MUX_OPP(i)), readl(MEM_MUX_MIN(i)));

	/* mux opp records */
	mmdvfs_debug_dump_line(file, "mux record opp/min/level");
	i = readl(MEM_REC_MUX_CNT) % MEM_REC_CNT_MAX;
	if (readl(MEM_REC_MUX_SEC(i))) {
		for (j = i; j < MEM_REC_CNT_MAX; j++) {
			val = readl(MEM_REC_MUX_VAL(j));
			mmdvfs_debug_dump_line(file, "[%5u.%6u] mux:%lu opp:%lu min:%lu level:%lu",
					       readl(MEM_REC_MUX_SEC(j)),
					       readl(MEM_REC_MUX_USEC(j)),
					       (val >> 24) & GENMASK(7, 0),
					       (val >> 16) & GENMASK(7, 0),
					       (val >> 8) & GENMASK(7, 0),
					       val & GENMASK(7, 0));
		}
	}

	for (j = 0; j < i; j++) {
		val = readl(MEM_REC_MUX_VAL(j));
		mmdvfs_debug_dump_line(file, "[%5u.%6u] mux:%lu opp:%lu min:%lu level:%lu",
				       readl(MEM_REC_MUX_SEC(j)), readl(MEM_REC_MUX_USEC(j)),
				       (val >> 24) & GENMASK(7, 0), (val >> 16) & GENMASK(7, 0),
				       (val >> 8) & GENMASK(7, 0), val & GENMASK(7, 0));
	}

	/* power/alone records */
	mmdvfs_debug_dump_line(file, "power/alone opp records");
	for (i = 0; i < MMDVFS_OPP_RECORD_NUM; i++) {
		j = readl(MEM_REC_PWR_ALN_CNT(i)) % MEM_REC_CNT_MAX;
		if (readl(MEM_REC_PWR_ALN_SEC(i, j))) {
			for (k = j; k < MEM_REC_CNT_MAX; k++)
				mmdvfs_debug_dump_line(file, "[%5u.%6u] pwr:%u opp:%u",
						       readl(MEM_REC_PWR_ALN_SEC(i, k)),
						       readl(MEM_REC_PWR_ALN_NSEC(i, k)),
						       i, readl(MEM_REC_PWR_ALN_OPP(i, k)));
		}

		for (k = 0; k < j; k++)
			mmdvfs_debug_dump_line(file, "[%5u.%6u] pwr:%u opp:%u",
					       readl(MEM_REC_PWR_ALN_SEC(i, k)),
					       readl(MEM_REC_PWR_ALN_NSEC(i, k)),
					       i, readl(MEM_REC_PWR_ALN_OPP(i, k)));
	}

	/* latest mux cb */
	mmdvfs_debug_dump_line(file, "latest mux cb mux/opp");
	val = readl(MEM_MUX_CB_MUX_OPP);
	mmdvfs_debug_dump_line(file, "[%5u.%6u] mux:%lu opp:%lu", readl(MEM_MUX_CB_SEC),
			       readl(MEM_MUX_CB_USEC), (val >> 8) & GENMASK(7, 0),
			       val & GENMASK(7, 0));
	mmdvfs_debug_dump_line(file, "[%5u.%6u] mux:%lu opp:%lu", readl(MEM_MUX_CB_END_SEC),
			       readl(MEM_MUX_CB_END_USEC), (val >> 8) & GENMASK(7, 0),
			       val & GENMASK(7, 0));

	/* vcore ceil */
	mmdvfs_debug_dump_line(file, "vcore ceil:%#x", readl(MEM_CEIL_LEVEL(PWR_MMDVFS_VCORE)));

	/* vmm ceil records */
	i = readl(MEM_REC_VMM_CEIL_CNT) % MEM_REC_CNT_MAX;
	if (readl(MEM_REC_VMM_CEIL_SEC(i))) {
		for (j = i; j < MEM_REC_CNT_MAX; j++)
			mmdvfs_debug_dump_line(file, "[%5u.%6u] vmm_ceil_enable:%#x",
					       readl(MEM_REC_VMM_CEIL_SEC(j)),
					       readl(MEM_REC_VMM_CEIL_USEC(j)),
					       readl(MEM_REC_VMM_CEIL_VAL(j)));
	}

	for (j = 0; j < i; j++)
		mmdvfs_debug_dump_line(file, "[%5u.%6u] vmm_ceil_enable:%#x",
				       readl(MEM_REC_VMM_CEIL_SEC(j)),
				       readl(MEM_REC_VMM_CEIL_USEC(j)),
				       readl(MEM_REC_VMM_CEIL_VAL(j)));

	/* vcp exception */
	if (readl(MEM_VCP_EXC_SEC)) {
		val = readl(MEM_VCP_EXC_VAL);
		mmdvfs_debug_dump_line(file, "[%5u.%6u] exception_handler",
				       readl(MEM_VCP_EXC_SEC), readl(MEM_VCP_EXC_USEC));
		mmdvfs_debug_dump_line(file, " vcore:%lu, vmm:%lu, vdisp:%lu",
				       val & GENMASK(7, 0), (val >> 8) & GENMASK(7, 0),
				       (val >> 16) & GENMASK(7, 0));
	}

	/* vmm debug */
	mmdvfs_debug_dump_line(file, "VMM Efuse_low:%#x, Efuse_high:%#x",
			       readl(MEM_VMM_EFUSE_LOW), readl(MEM_VMM_EFUSE_HIGH));
	for (j = 0; j < 8; j++)
		mmdvfs_debug_dump_line(file, "VMM voltage level%d:%u",
				       j, readl(MEM_VMM_OPP_VOLT(j)));

	i = readl(MEM_REC_VMM_DBG_CNT) % MEM_REC_CNT_MAX;
	if (readl(MEM_REC_VMM_SEC(i))) {
		for (j = i; j < MEM_REC_CNT_MAX; j++) {
			mmdvfs_debug_dump_line(file, "[%5u.%3u] volt:%u temp:%#x avs:%#x",
					       readl(MEM_REC_VMM_SEC(j)),
					       readl(MEM_REC_VMM_NSEC(j)),
					       readl(MEM_REC_VMM_VOLT(j)),
					       readl(MEM_REC_VMM_TEMP(j)),
					       readl(MEM_REC_VMM_AVS(j)));
		}
	}

	for (j = 0; j < i; j++)
		mmdvfs_debug_dump_line(file, "[%5u.%3u] volt:%u temp:%#x avs:%#x",
				       readl(MEM_REC_VMM_SEC(j)), readl(MEM_REC_VMM_NSEC(j)),
				       readl(MEM_REC_VMM_VOLT(j)),
				       readl(MEM_REC_VMM_TEMP(j)), readl(MEM_REC_VMM_AVS(j)));
	return;

sram_dump:
	mtk_mmdvfs_enable_vcp(true, VCP_PWR_USR_MMDVFS_RST);
	mmdvfs_vcp_cb_mutex_lock();
	if (!mmdvfs_vcp_cb_ready_get()) {
		mmdvfs_vcp_cb_mutex_unlock();
		return;
	}

	mmdvfs_debug_dump_line(file, "VER3.5: mux controlled by vcp sram:%#lx",
			       (unsigned long)(void *)SRAM_BASE);
	/* usr */
	for (k = 0; k < SRAM_USR_NUM; k++) {
		i = readl(SRAM_REC_CNT_USR(k)) % SRAM_REC_CNT;
		for (j = i; j < SRAM_REC_CNT; j++)
			mmdvfs_debug_dump_line(file, "[%5u.%3u] usr:%u opp:%u",
					       readl(SRAM_USR_SEC(k, j)),
					       readl(SRAM_USR_USEC(k, j)),
					       k, readl(SRAM_USR_VAL(k, j)));
		for (j = 0; j < i; j++)
			mmdvfs_debug_dump_line(file, "[%5u.%3u] usr:%u opp:%u",
					       readl(SRAM_USR_SEC(k, j)),
					       readl(SRAM_USR_USEC(k, j)),
					       k, readl(SRAM_USR_VAL(k, j)));
	}

	/* mux */
	i = readl(SRAM_REC_CNT_MUX) % SRAM_REC_CNT;
	for (j = i; j < SRAM_REC_CNT; j++) {
		val = readl(SRAM_MUX_VAL(j));
		mmdvfs_debug_dump_line(file, "[%5u.%3u] mux:%lu opp:%lu min:%lu level:%lu",
				       readl(SRAM_MUX_SEC(j)), readl(SRAM_MUX_USEC(j)),
				       (val >> 24) & GENMASK(7, 0), (val >> 16) & GENMASK(7, 0),
				       (val >> 8) & GENMASK(7, 0), val & GENMASK(7, 0));
	}
	for (j = 0; j < i; j++) {
		val = readl(SRAM_MUX_VAL(j));
		mmdvfs_debug_dump_line(file, "[%5u.%3u] mux:%lu opp:%lu min:%lu level:%lu",
				       readl(SRAM_MUX_SEC(j)), readl(SRAM_MUX_USEC(j)),
				       (val >> 24) & GENMASK(7, 0), (val >> 16) & GENMASK(7, 0),
				       (val >> 8) & GENMASK(7, 0), val & GENMASK(7, 0));
	}

	/* pwr */
	for (k = 0; k < SRAM_PWR_CNT; k++) {
		i = readl(SRAM_REC_CNT_PWR(k)) % SRAM_REC_CNT;
		for (j = i; j < SRAM_REC_CNT; j++)
			mmdvfs_debug_dump_line(file, "[%5u.%3u] pwr:%u opp:%u",
					       readl(SRAM_PWR_SEC(k, j)),
					       readl(SRAM_PWR_USEC(k, j)),
					       k, readl(SRAM_PWR_VAL(k, j)));
		for (j = 0; j < i; j++)
			mmdvfs_debug_dump_line(file, "[%5u.%3u] pwr:%u opp:%u",
					       readl(SRAM_PWR_SEC(k, j)),
					       readl(SRAM_PWR_USEC(k, j)),
					       k, readl(SRAM_PWR_VAL(k, j)));
	}

	/* clk */
	for (k = 0; k < SRAM_PWR_CNT; k++) {
		i = readl(SRAM_REC_CNT_CLK(k)) % SRAM_REC_CNT;
		for (j = i; j < SRAM_REC_CNT; j++)
			mmdvfs_debug_dump_line(file, "[%5u.%3u] clk:%u opp:%u",
					       readl(SRAM_CLK_SEC(k, j)),
					       readl(SRAM_CLK_USEC(k, j)),
					       k, readl(SRAM_CLK_VAL(k, j)));
		for (j = 0; j < i; j++)
			mmdvfs_debug_dump_line(file, "[%5u.%3u] clk:%u opp:%u",
					       readl(SRAM_CLK_SEC(k, j)),
					       readl(SRAM_CLK_USEC(k, j)),
					       k, readl(SRAM_CLK_VAL(k, j)));
	}

	/* rate */
	i = readl(SRAM_REC_CNT_RATE) % SRAM_REC_CNT;
	for (j = i; j < SRAM_REC_CNT; j++)
		mmdvfs_debug_dump_line(file, "[%5u.%3u] rate:%u",
				       readl(SRAM_RATE_SEC(j)), readl(SRAM_RATE_USEC(j)),
				       readl(SRAM_RATE_VAL(j)));
	for (j = 0; j < i; j++)
		mmdvfs_debug_dump_line(file, "[%5u.%3u] rate:%u",
				       readl(SRAM_RATE_SEC(j)), readl(SRAM_RATE_USEC(j)),
				       readl(SRAM_RATE_VAL(j)));

	/* ceil */
	i = readl(SRAM_REC_CNT_CEIL) % SRAM_REC_CNT;
	for (j = i; j < SRAM_REC_CNT; j++)
		mmdvfs_debug_dump_line(file, "[%5u.%3u] ceil:%u",
				       readl(SRAM_CEIL_SEC(j)), readl(SRAM_CEIL_USEC(j)),
				       readl(SRAM_CEIL_VAL(j)));
	for (j = 0; j < i; j++)
		mmdvfs_debug_dump_line(file, "[%5u.%3u] ceil:%u",
				       readl(SRAM_CEIL_SEC(j)), readl(SRAM_CEIL_USEC(j)),
				       readl(SRAM_CEIL_VAL(j)));

	/* vmm */
	i = readl(SRAM_REC_CNT_VMM) % SRAM_REC_CNT;
	for (j = i; j < SRAM_REC_CNT; j++)
		mmdvfs_debug_dump_line(file, "[%5u.%3u] vmm val:%u hw:%u volt:%u",
				       readl(SRAM_VMM_SEC(j)), readl(SRAM_VMM_USEC(j)),
				       readl(SRAM_VMM_VAL(j)), readl(SRAM_VMM_HW_VAL(j)),
				       readl(SRAM_VMM_VOLT(j)));
	for (j = 0; j < i; j++)
		mmdvfs_debug_dump_line(file, "[%5u.%3u] vmm val:%u hw:%u volt:%u",
				       readl(SRAM_VMM_SEC(j)), readl(SRAM_VMM_USEC(j)),
				       readl(SRAM_VMM_VAL(j)), readl(SRAM_VMM_HW_VAL(j)),
				       readl(SRAM_VMM_VOLT(j)));
	mmdvfs_debug_dump_line(file, "vmm efuse high:%u low:%u", readl(SRAM_VMM_EFUSE_HIGH),
			       readl(SRAM_VMM_EFUSE_LOW));

	/* vdisp */
	i = readl(SRAM_REC_CNT_VDISP) % SRAM_REC_CNT;
	for (j = i; j < SRAM_REC_CNT; j++)
		mmdvfs_debug_dump_line(file, "[%5u.%3u] vdisp:%u", readl(SRAM_VDISP_SEC(j)),
				       readl(SRAM_VDISP_USEC(j)), readl(SRAM_VDISP_VAL(j)));
	for (j = 0; j < i; j++)
		mmdvfs_debug_dump_line(file, "[%5u.%3u] vdisp:%u", readl(SRAM_VDISP_SEC(j)),
				       readl(SRAM_VDISP_USEC(j)), readl(SRAM_VDISP_VAL(j)));

	/* mux min, pwr gear, vmm ceil */
	for (i = 0; i < SRAM_MUX_CNT; i++)
		mmdvfs_debug_dump_line(file, "mux:%d min:%u", i, readl(SRAM_MUX_MIN(i)));
	for (i = 0; i < SRAM_PWR_CNT - 1; i++)
		mmdvfs_debug_dump_line(file, "pwr:%d gear:%u", i, readl(SRAM_PWR_GEAR(i)));
	mmdvfs_debug_dump_line(file, "pwr:%d ceil:%u", i, readl(SRAM_VMM_CEIL));

	mmdvfs_vcp_cb_mutex_unlock();
	mtk_mmdvfs_enable_vcp(false, VCP_PWR_USR_MMDVFS_RST);
}
EXPORT_SYMBOL_GPL(mmdvfs_debug_status_dump);

static int mmdvfs_debug_opp_show(struct seq_file *file, void *data)
{
	u32 i, j;

	mmdvfs_debug_status_dump(file);

	if (!MEM_BASE)
		return 0;

	/* power total time */
	mmdvfs_debug_dump_line(file, "power/alone total time(ms)");
	for (i = 0; i < MMDVFS_OPP_RECORD_NUM; i++) {
		for (j = 0; j < MTK_MMDEBUG_MAX_OPP; j++)
			mmdvfs_debug_dump_line(file, "pwr:%u opp:%u total_time:%llu", i, j,
					       readq(!mmdvfs_get_mmup_sram_enable() ?
					       MEM_PWR_TOTAL_TIME(i, j) : SRAM_PWR_TOTAL(i, j)));
	}

	return 0;
}

static int mmdvfs_debug_opp_open(struct inode *inode, struct file *file)
{
	return single_open(file, mmdvfs_debug_opp_show, inode->i_private);
}

static struct file_operations mmdvfs_debug_opp_fops = {
	.open = mmdvfs_debug_opp_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

int mtk_mmdvfs_debug_force_vcore_notify(const u32 val)
{
	return mtk_mmdvfs_force_vcore_notify(val);
}
EXPORT_SYMBOL_GPL(mtk_mmdvfs_debug_force_vcore_notify);

bool mtk_is_mmdvfs_v3_debug_init_done(void)
{
	struct mtk_mmdebug_dev *mmdebug_dev = mtk_mmdebug_get_drv_data();

	return mmdebug_dev->mmdvfs_v3_debug_init_done;
}
EXPORT_SYMBOL_GPL(mtk_is_mmdvfs_v3_debug_init_done);

static int mmdvfs_v3_debug_thread(void *data)
{
	struct mtk_mmdebug_dev *mmdebug_dev = data;
	int retry = 0, i;

	while (!mmdvfs_is_init_done()) {
		if (++retry > MMDVFS_DEBUG_MAX_RETRY) {
			mtk_mmdebug_err(mmdebug_dev->dev, "mmdvfs_v3 init not ready");
			return 0;
		}
		ssleep(2);
	}

	if (mmdebug_dev->use_v3_pwr & (1 << PWR_MMDVFS_VCORE))
		mtk_mmdvfs_v3_set_vote_step(PWR_MMDVFS_VCORE, mmdebug_dev->force_step0, false);

	if (mmdebug_dev->use_v3_pwr & (1 << PWR_MMDVFS_VMM))
		mtk_mmdvfs_v3_set_vote_step(PWR_MMDVFS_VMM, mmdebug_dev->force_step0, false);

	if (!mmdebug_dev->release_step0)
		goto init_done;

	if (mmdebug_dev->use_v3_pwr & (1 << PWR_MMDVFS_VCORE))
		mtk_mmdvfs_v3_set_vote_step(PWR_MMDVFS_VCORE, -1, false);

	if (mmdebug_dev->use_v3_pwr & (1 << PWR_MMDVFS_VMM))
		mtk_mmdvfs_v3_set_vote_step(PWR_MMDVFS_VMM, -1, false);

	if (mmdebug_dev->vote_step != 0xff) {
		for (i = 0; i < PWR_MMDVFS_NUM; i++) {
			mtk_mmdvfs_v3_set_vote_step(mmdebug_dev->vote_step >> 4 & 0xf,
						    mmdebug_dev->vote_step & 0xf, false);
			mmdebug_dev->vote_step = mmdebug_dev->vote_step >> 8;
		}
	}

	if (mmdebug_dev->force_step != 0xff) {
		for (i = 0; i < PWR_MMDVFS_NUM; i++) {
			mtk_mmdvfs_v3_set_force_step(mmdebug_dev->force_step >> 4 & 0xf,
						     mmdebug_dev->force_step & 0xf, false);
			mmdebug_dev->force_step = mmdebug_dev->force_step >> 8;
		}
	}

init_done:
	mmdebug_dev->mmdvfs_v3_debug_init_done = true;
	return 0;
}

static int mmdvfs_debug_mmdebug_cb(struct notifier_block *nb, unsigned long action, void *data)
{
	mmdvfs_debug_status_dump(NULL);
	return 0;
}

static int mmdvfs_debug_probe(struct platform_device *pdev)
{
	struct mtk_mmdebug_dev *mmdebug_dev;
	struct device *dev = &pdev->dev;
	struct task_struct *kthr;
	struct regulator *reg;
	struct dentry *file;
	int ret;

	mmdebug_dev = kzalloc(sizeof(*mmdebug_dev), GFP_KERNEL);
	if (!mmdebug_dev)
		return -ENOMEM;

	mmdebug_dev->dev = &pdev->dev;
	platform_set_drvdata(pdev, mmdebug_dev);

	struct dentry *dir;
	dir = debugfs_create_dir("mmdvfs", NULL);
	if (IS_ERR_OR_NULL(dir))
		mtk_mmdebug_err(dev, "debugfs_create_dir failed:%ld", PTR_ERR(dir));

	file = debugfs_create_file("mmdvfs_opp", 0440, dir, &mmdvfs_debug_opp_fops, NULL);
	if (IS_ERR_OR_NULL(file))
		mtk_mmdebug_err(dev, "proc_create failed:%ld", PTR_ERR(file));
	else
		mmdebug_dev->file = file;

	ret = of_property_read_u32(mmdebug_dev->dev->of_node, "debug-version",
				   &mmdebug_dev->debug_version);
	if (ret)
		mtk_mmdebug_err(dev, "debug_version:%u failed:%d", mmdebug_dev->debug_version, ret);

	spin_lock_init(&mmdebug_dev->lock);
	reg = devm_regulator_get(mmdebug_dev->dev, "vcore");
	if (IS_ERR_OR_NULL(reg))
		mtk_mmdebug_err(dev, "devm_regulator_get vcore failed:%ld", PTR_ERR(reg));
	else
		mmdebug_dev->reg_vcore = reg;

	reg = devm_regulator_get(mmdebug_dev->dev, "vmm-pmic");
	if (IS_ERR_OR_NULL(reg))
		mtk_mmdebug_err(dev, "devm_regulator_get vmm-pmic failed:%ld", PTR_ERR(reg));
	else
		mmdebug_dev->reg_vmm = reg;

	reg = devm_regulator_get(mmdebug_dev->dev, "vdisp-pmic");
	if (IS_ERR_OR_NULL(reg))
		mtk_mmdebug_err(dev, "devm_regulator_get vdisp-pmic failed:%ld", PTR_ERR(reg));
	else
		mmdebug_dev->reg_vdisp = reg;

	mmdebug_dev->mmdebug_nb.notifier_call = mmdvfs_debug_mmdebug_cb;
	mtk_mmdebug_status_dump_register_notifier(&mmdebug_dev->mmdebug_nb);

	mmdebug_dev->vote_step = 0xff;
	ret= of_property_read_u32(mmdebug_dev->dev->of_node, "vote-step", &mmdebug_dev->vote_step);
	if (ret)
		mtk_mmdebug_dbg(dev, "vote-step not found, use default 0xff");
	mmdebug_dev->force_step = 0xff;
	ret= of_property_read_u32(mmdebug_dev->dev->of_node, "force-step", &mmdebug_dev->force_step);
	if (ret)
		mtk_mmdebug_dbg(dev, "force-step not found, use default 0");
	of_property_read_u32(mmdebug_dev->dev->of_node, "release-step0",
			     &mmdebug_dev->release_step0);
	if (ret)
		mtk_mmdebug_dbg(dev, "release-step0 not found, use default 0");
	ret = of_property_read_u32(mmdebug_dev->dev->of_node, "use-v3-pwr",
				   &mmdebug_dev->use_v3_pwr);
	if (ret)
		mtk_mmdebug_dbg(dev, "release-step0 not found, use default 0");
	if (mmdebug_dev->debug_version & MMDVFS_DBG_VER3) {
		kthr = kthread_run(mmdvfs_v3_debug_thread, mmdebug_dev, "mmdvfs-dbg-vcp");
		if (IS_ERR(kthr))
			mtk_mmdebug_err(dev, "create kthread mmdvfs_v3_debug_thread failed");
	}

	return 0;
}

static int mmdvfs_debug_remove(struct platform_device *pdev)
{
	struct mtk_mmdebug_dev *mmdebug_dev = mtk_mmdebug_get_drv_data();

	kfree(mmdebug_dev);
	return 0;
}

static const struct of_device_id of_mmdvfs_debug_match_tbl[] = {
	{
		.compatible = "mediatek,mmdvfs-debug",
	},
	{}
};

static struct platform_driver mmdvfs_debug_drv = {
	.probe = mmdvfs_debug_probe,
	.remove = mmdvfs_debug_remove,
	.driver = {
		.name = "mtk-mmdvfs-debug",
		.of_match_table = of_mmdvfs_debug_match_tbl,
	},
};

module_platform_driver(mmdvfs_debug_drv);

MODULE_DESCRIPTION("MMDVFS Debug Driver");
MODULE_LICENSE("GPL");
