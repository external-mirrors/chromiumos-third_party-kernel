/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2025 MediaTek Inc.
 */

#ifndef _MTK_MMDVFS_V3_H_
#define _MTK_MMDVFS_V3_H_

#include <dt-bindings/clock/mmdvfs-clk.h>
#include <linux/workqueue.h>
#include <soc/mediatek/mmdvfs_v3.h>
#include <soc/mediatek/mtk_clk_user.h>

#define IPI_TIMEOUT_MS	(200U)
#define PIN_OUT_SIZE_MMDVFS 2

extern int mmdvfs_log_level;

#define mtk_mmdvfs_err(plat_dev, fmt, args...)                                               \
	dev_err(&(plat_dev)->dev, "[mmdvfs err]%s %d: " fmt "\n", __func__, __LINE__, ##args)

#define mtk_mmdvfs_debug(plat_dev, level, fmt, args...)                   \
do {                                                                      \
	if (mmdvfs_log_level & (1 << (level)))                      \
		dev_dbg(&(plat_dev)->dev, "[mmdvfs dbg]%s %d: " fmt "\n", \
			 __func__, __LINE__, ##args);                     \
} while (0)

enum ipi_func_id {
	FUNC_MMDVFS_INIT,
	FUNC_CLKMUX_ENABLE,
	FUNC_VMM_BUCK_ENABLE,
	FUNC_VMM_CEIL_ENABLE,
	FUNC_VMM_GENPD_NOTIFY,
	FUNC_VMM_AVS_UPDATE,
	FUNC_CAMSV_DC_ENABLE,
	FUNC_FORCE_OPP,
	FUNC_VOTE_OPP,
	FUNC_MMDVFSRC_INIT,
	FUNC_MMDVFS_LP_MODE,
	FUNC_SWRGO_INIT,

	FUNC_FORCE_VOL = 11,
	FUNC_FORCE_CLK,
	FUNC_FORCE_SINGLE_CLK,
	FUNC_MMDVFS_PROFILE,
	FUNC_VCORE_CEIL_LEVEL,

	FUNC_NUM
};


struct mmdvfs_ipi_data {
	u8 func;
	u8 idx;
	u8 opp;
	u8 ack;
	u32 base;
};

struct mmdvfs_vmm_notify_work {
	struct work_struct vmm_notify_work;
	bool enable;
};

struct mtk_mmdvfs_dev {
	struct platform_device *pdev;

	struct workqueue_struct *vmm_notify_wq;
	struct mtk_vcp_device  *vcp_device;

	rc_enable dpc_fp;
	bool mmdvfs_lp_mode;
	bool mmdvfs_vcp_stop;

	bool mmdvfs_mux_version;
	bool mmup_ena;

	u32 force_vol;
	u32 force_rc_clk;
	u32 force_single_clk;

	int vcp_pwr_usage[VCP_PWR_USR_NUM];
	struct notifier_block mmdvfs_mmup_notifier;
	int dpsw_thr;
	int vmm_ceil_step;

	void __iomem *vcore_check_rg;
	u32 vcore_check_offset;
	u8 vcore_level_count;
	u8 *vcore_level;

	int last_vote_step[PWR_MMDVFS_NUM];
	int last_force_step[PWR_MMDVFS_NUM];
	int last_force_volt[PWR_MMDVFS_NUM];

	bool mmdvfs_free_run;
	bool mmdvfs_init_done;
	bool mmdvfs_restore_step;
	bool mmdvfs_release_step_done;

	int vcp_power;
	int vmm_power;

	u8 mmdvfs_rst_clk_num;
	bool mmdvfs_rst_clk_done;

	phys_addr_t mmdvfs_mmup_iova;
	void *mmdvfs_mmup_va;
	phys_addr_t mmdvfs_vcp_iova;
	void *mmdvfs_vcp_va;
	bool mmdvfs_mmup_sram;
	void __iomem *mmdvfs_mmup_sram_va;

	u8 mmdvfs_pwr_opp[PWR_MMDVFS_NUM];
	struct clk *mmdvfs_pwr_clk[PWR_MMDVFS_NUM];
	struct clk *mmdvfs_rst_clk[MMDVFS_RST_CLK_NUM];

	u8 mmdvfs_clk_num;
	struct mtk_mmdvfs_clk *mtk_mmdvfs_clks;
	bool mmdvfs_vcp_cb_ready;
	int mmdvfs_ipi_status;
	u64 cb_timestamp[3];
	struct device *mmdvfs_v3_dev;

	int vcp_log_level;
	int vmrc_log_level;
	u32 hqa_enable;

	struct mmdvfs_mux mmdvfs_mux[MMDVFS_MUX_NUM];
	struct mtk_mux_user mmdvfs_user[MMDVFS_USER_NUM];
};
#endif
