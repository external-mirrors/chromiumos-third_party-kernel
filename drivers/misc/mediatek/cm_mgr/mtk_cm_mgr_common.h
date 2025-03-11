/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 MediaTek Inc.
 */

#ifndef __MTK_CM_MGR_H__
#define __MTK_CM_MGR_H__

#include <linux/kernel.h>

#define CM_MGR_D_LEN (2)

enum {
	IPI_CM_MGR_INIT,
	IPI_CM_MGR_ENABLE,
	IPI_CM_MGR_OPP_ENABLE,
	IPI_CM_MGR_SSPM_ENABLE,
	IPI_CM_MGR_BLANK,
	IPI_CM_MGR_DISABLE_FB,
	IPI_CM_MGR_DRAM_TYPE,
	IPI_CM_MGR_CPU_POWER_RATIO_UP,
	IPI_CM_MGR_CPU_POWER_RATIO_DOWN,
	IPI_CM_MGR_VCORE_POWER_RATIO_UP,
	IPI_CM_MGR_VCORE_POWER_RATIO_DOWN,
	IPI_CM_MGR_DEBOUNCE_UP,
	IPI_CM_MGR_DEBOUNCE_DOWN,
	IPI_CM_MGR_DEBOUNCE_TIMES_RESET_ADB = 16,
	IPI_CM_MGR_DRAM_LEVEL,
	IPI_CM_MGR_LIGHT_LOAD_CPS,
	IPI_CM_MGR_LOADING_ENABLE,
	IPI_CM_MGR_LOADING_LEVEL,
	IPI_CM_MGR_EMI_DEMAND_CHECK,
	IPI_CM_MGR_OPP_FREQ_SET,
	IPI_CM_MGR_OPP_VOLT_SET,
	IPI_CM_MGR_BCPU_WEIGHT_MAX_SET,
	IPI_CM_MGR_BCPU_WEIGHT_MIN_SET,
	IPI_CM_MGR_BBCPU_WEIGHT_MAX_SET,
	IPI_CM_MGR_BBCPU_WEIGHT_MIN_SET,
	IPI_CM_MGR_DSU_DEBOUNCE_UP_SET,
	IPI_CM_MGR_DSU_DEBOUNCE_DOWN_SET,
	IPI_CM_MGR_DSU_DIFF_PWR_UP_SET,
	IPI_CM_MGR_DSU_DIFF_PWR_DOWN_SET,
	IPI_CM_MGR_DSU_L_PWR_RATIO_SET,
	IPI_CM_MGR_DSU_B_PWR_RATIO_SET,
	IPI_CM_MGR_DSU_BB_PWR_RATIO_SET,
	IPI_CM_MGR_DSU_ENABLE = 38,
	IPI_CM_MGR_DSU_OPP_SEND = 39,
	IPI_CM_MGR_DSU_MODE = 40,
	IPI_CM_MGR_HINT = 41,
	IPI_CM_MGR_AGGRESSIVE = 42,
	IPI_CM_MGR_DRAM_OPP_CEILING = 43,
	IPI_CM_MGR_DRAM_OPP_FLOOR = 44,
	IPI_CM_MGR_DSU_PERF_HINT = 45,
	IPI_CM_MGR_PASSIVE = 46,
	IPI_CM_MGR_SSPM_VER = 47,
	IPI_CM_MGR_CPU_MAP_DRAM_ENABLE = 48,
	IPI_CM_MGR_PERF_MODE_ENABLE = 49,
	IPI_CM_MGR_PERF_MODE_CEILING_OPP = 50,
	IPI_CM_MGR_PERF_MODE_THD = 51,
	IPI_CM_MGR_CHIP_VER = 52,
	NR_IPI_CM_MGR,
};

enum {
	CM_MGR_ARCH_V1,
	CM_MGR_ARCH_V1P,
};

struct mtk_cm_mgr {
	struct device *dev;
	int num_perf;
	int idx;
	void __iomem *base;
	int *buf;
};

struct tracepoints_table {
	const char *name;
	void *func;
	struct tracepoint *tp;
	bool registered;
};

struct cm_mgr_data {
	unsigned int cmd;
	unsigned int arg;
};

struct cm_mgr_hook {
	u32 (*cm_mgr_get_perfs)(int enable);
	void (*cm_mgr_perf_set_force_status)(int enable);
	void (*check_cm_mgr_status)(unsigned int cluster, unsigned int freq,
				    unsigned int idx);
	void (*cm_mgr_perf_platform_set_status)(int enable);
	void (*cm_mgr_perf_set_status)(int enable);
	int (*cm_mgr_to_sspm_command) (unsigned int, unsigned int);
};

struct cm_mgr_common_data {
	struct device *dev;
	void __iomem *cm_mgr_base;
	unsigned int cm_work_flag;
	int cm_mgr_blank_status;
	int *cm_mgr_buf;
	int cm_mgr_cpu_to_dram_opp;
	int *cm_mgr_cpu_opp_to_dram;
	int cm_mgr_cpu_opp_size;
	int cm_mgr_emi_demand_check;
	int cm_mgr_disable_fb;
	int cm_mgr_dram_opp_base;
	int cm_mgr_dram_opp_ceiling;
	int cm_mgr_dram_opp_floor;
	int cm_mgr_loading_enable;
	int cm_mgr_loading_level;
	int cm_mgr_num_array;
	int cm_mgr_num_perf;
	int debounce_times_perf_down;
	int debounce_times_perf_force_down;
	int debounce_times_perf_down_local;
	int debounce_times_perf_down_force_local;
	int debounce_times_reset_adb;
	unsigned int cm_hint;
	unsigned int cm_mgr_sspm_version;
	unsigned int cm_perf_mode_enable;
	unsigned int cm_perf_mode_ceiling_opp;
	unsigned int cm_perf_mode_thd;
	unsigned int *cpu_power_ratio_down;
	unsigned int *cpu_power_ratio_up;
	unsigned int *vcore_power_ratio_down;
	unsigned int *vcore_power_ratio_up;
	unsigned int *debounce_times_down_adb;
	unsigned int *debounce_times_up_adb;
	struct cm_mgr_hook hk;
	struct delayed_work cm_mgr_work;
	struct icc_path *cm_mgr_bw_path;
	struct kobject *cm_mgr_kobj;
};

struct cm_mgr_cp_data {
	int cm_mgr_arch;
	int cm_mgr_cpu_map_dram_enable;
	int cm_mgr_enable;
	int cm_mgr_use_bcpu_weight;
	int cm_mgr_use_cpu_to_dram_map;
	int cm_mgr_use_cpu_to_dram_map_new;
	int cpu_power_bcpu_weight_max;
	int cpu_power_bcpu_weight_min;
	int cpu_power_bbcpu_weight_max;
	int cpu_power_bbcpu_weight_min;
};

/* common api */
void cm_mgr_update_dram_by_cpu_opp(int cpu_opp);
int cm_mgr_check_dts_setting(struct platform_device *pdev);
int cm_mgr_common_init(struct platform_device *pdev);
int cm_mgr_get_enable(void);
void cm_mgr_set_pdev(struct platform_device *pdev);
int cm_mgr_get_num_array(void);
void cm_mgr_set_num_array(int num);
void cm_mgr_get_sspm_version(void);
void cm_mgr_set_num_perf(int num);
int debounce_times_perf_down_get(void);
int debounce_times_perf_force_down_get(void);
struct icc_path *cm_mgr_get_bw_path(void);
struct icc_path *cm_mgr_set_bw_path(struct icc_path *bw_path);
void cm_mgr_perf_set_status(int enable);
void cm_mgr_register_hook(struct cm_mgr_hook *hook);
void cm_mgr_unregister_hook(struct cm_mgr_hook *hook);
void cm_mgr_set_perf_mode_enable(int enable);
int cm_mgr_get_perf_mode_enable(void);
void cm_mgr_set_perf_mode_ceiling_opp(int opp);
int cm_mgr_get_perf_mode_ceiling_opp(void);
void cm_mgr_set_perf_mode_thd(int thd);
int cm_mgr_get_perf_mode_thd(void);
int cm_mgr_to_sspm_command_common(unsigned int, unsigned int);

#endif /* __MTK_CM_MGR_H__ */
