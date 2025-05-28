/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2025 MediaTek Inc.
 */

#ifndef _MMDVFS_V3_H_
#define _MMDVFS_V3_H_

#include <dt-bindings/clock/mmdvfs-clk.h>
#include <linux/clk-provider.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

typedef int (*rc_enable)(const bool enable, const bool wdt);

#define MMDVFS_RST_CLK_NUM	(3)
#define MMDVFS_MAX_OPP		(8)

struct mtk_mux_user;

enum VCP_PWR_USR {
	VCP_PWR_USR_MMDVFS_INIT,
	VCP_PWR_USR_MMDVFS_GENPD,
	VCP_PWR_USR_MMDVFS_FORCE,
	VCP_PWR_USR_MMDVFS_VOTE,
	VCP_PWR_USR_MMDVFS_CCU,
	VCP_PWR_USR_MMDVFS_RST,
	VCP_PWR_USR_MMQOS,
	VCP_PWR_USR_CAM,
	VCP_PWR_USR_IMG,
	VCP_PWR_USR_PDA,
	VCP_PWR_USR_SENIF,
	VCP_PWR_USR_VDEC,
	VCP_PWR_USR_VFMT,
	VCP_PWR_USR_SMI,
	VCP_PWR_USR_DISP,
	VCP_PWR_USR_MDP,
	VCP_PWR_USR_MML,
	VCP_PWR_USR_VENC,
	VCP_PWR_USR_JPEGDEC,
	VCP_PWR_USR_JPEGENC,
	VCP_PWR_USR_NUM
};

enum {
	VMM_USR_CAM,
	VMM_USR_IMG,
	VMM_USR_VDE = VMM_USR_IMG,
	VMM_USR_NUM
};

enum {
	VMM_CEIL_USR_CAM,
	VMM_CEIL_USR_ADB,
	VMM_CEIL_USR_NUM
};

enum {
	VCORE_CEIL_USR_VCORE,
	VCORE_CEIL_USR_ADB,
	VCORE_CEIL_USR_NUM
};

enum {
	USER_DISP,
	USER_DISP_AP,
	USER_MDP,
	USER_MML,
	USER_MMINFRA,
	USER_VENC,
	USER_VENC_AP,
	USER_VDEC,
	USER_VDEC_AP,
	USER_IMG,
	USER_CAM,
	USER_AOV,
	USER_VCORE,
	USER_VMM,
	USER_NUM
};

struct mtk_mmdvfs_clk {
	const char *name;
	u8 clk_id;
	u8 pwr_id;
	u8 user_id;
	u8 ipi_type;
	u8 spec_type;
	u8 opp;
	u8 freq_num;
	u32 freqs[MMDVFS_MAX_OPP];
	struct clk_hw clk_hw;
};

struct mmdvfs_mux {
	u8 id;
	const char *name;
	const char *target_name;
	u8 freq_num;
	u64 freq[MMDVFS_MAX_OPP];
	u64 rate;
	u8 opp;
	u8 last;
	u8 user_num;
	u8 vcp_mux_id;
	struct mtk_mux_user *user[MMDVFS_USER_NUM];
};

#if IS_ENABLED(CONFIG_MTK_MMDVFS) && IS_ENABLED(CONFIG_MTK_VCP_RPROC)
int mtk_mmdvfs_enable_vcp(const bool enable, enum VCP_PWR_USR idx);
int mtk_mmdvfs_force_vcore_notify(const u32 val);
int mtk_mmdvfs_genpd_notify(const u8 idx, const bool enable);
int mtk_mmdvfs_set_avs(const u8 idx, const u32 aging, const u32 fresh);
int mtk_mmdvfs_camsv_dc_enable(const u8 idx, const bool enable);
int mtk_mmdvfs_v3_set_vote_step(const u16 pwr_idx, const s16 opp, const bool cmd);
void mmdvfs_set_lp_mode(bool lp_mode);
void mmdvfs_rc_enable_set_fp(rc_enable fp);
int mmdvfs_set_lp_mode_by_vcp(const bool enable);
bool mmdvfs_is_mux_version(void);
int mmdvfs_force_step_by_vcp(const u8 pwr_idx, const s8 opp);
int mmdvfs_force_voltage_by_vcp(const u8 pwr_idx, const s8 opp);
int mmdvfs_force_rc_clock_by_vcp(const u8 pwr_idx, const s8 opp);
int mmdvfs_force_single_clock_by_vcp(const u8 mux_idx, const s8 opp);
int mmdvfs_vote_step_by_vcp(const u8 pwr_idx, const s8 opp);
int mmdvfs_mux_set_opp(const char *name, unsigned long rate);
void mmdvfs_vcp_cb_mutex_lock(void);
void mmdvfs_vcp_cb_mutex_unlock(void);
bool mmdvfs_vcp_cb_ready_get(void);
int mtk_mmdvfs_v3_set_force_step(const u16 pwr_idx, const s16 opp, const bool cmd);
#else
static inline int mtk_mmdvfs_enable_vcp(const bool enable, enum VCP_PWR_USR idx) { return 0; }
static inline int mtk_mmdvfs_force_vcore_notify(const u32 val) { return 0; }
static inline int mtk_mmdvfs_genpd_notify(const u8 idx, const bool enable) { return 0; }
static inline int mtk_mmdvfs_set_avs(const u8 idx, const u32 aging, const u32 fresh) { return 0; }
static inline int mtk_mmdvfs_camsv_dc_enable(const u8 idx, const bool enable) { return 0; }
static inline
int mtk_mmdvfs_v3_set_vote_step(const u16 pwr_idx, const s16 opp, const bool cmd) { return 0; }
static inline void mmdvfs_rc_enable_set_fp(rc_enable fp) { return; }
static inline int mmdvfs_set_lp_mode_by_vcp(const bool enable) { return 0; }
static inline bool mmdvfs_is_mux_version(void) { return false; }
static inline int mmdvfs_force_step_by_vcp(const u8 pwr_idx, const s8 opp) { return 0; }
static inline int mmdvfs_force_voltage_by_vcp(const u8 pwr_idx, const s8 opp) { return 0; }
static inline int mmdvfs_force_rc_clock_by_vcp(const u8 pwr_idx, const s8 opp) { return 0; }
static inline int mmdvfs_force_single_clock_by_vcp(const u8 mux_idx, const s8 opp) { return 0; }
static inline int mmdvfs_vote_step_by_vcp(const u8 pwr_idx, const s8 opp) { return 0; }
static inline int mmdvfs_mux_set_opp(const char *name, unsigned long rate) { return 0; }
static inline void mmdvfs_vcp_cb_mutex_lock(void) { return; }
static inline void mmdvfs_vcp_cb_mutex_unlock(void) { return; }
static inline bool mmdvfs_vcp_cb_ready_get(void) { return false; }
static inline
int mtk_mmdvfs_v3_set_force_step(const u16 pwr_idx, const s16 opp, const bool cmd) { return 0; }
#endif

static inline struct mtk_mmdvfs_dev *mtk_mmdvfs_get_drv_data(void)
{
	struct platform_device *pdev;
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "mediatek,mtk-mmdvfs-mux");
	if (!np) {
		pr_err("can't get mmdvfs mux node\n");
		return NULL;
	}

	pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (!pdev) {
		pr_err("can't get mmdvfs mux device\n");
		return NULL;
	}

	return platform_get_drvdata(pdev);
}

#endif /* MTK_MMDVFS_V3_H */
