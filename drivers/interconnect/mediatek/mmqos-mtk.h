/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2025 MediaTek Inc.
 * Author: Echo.Zhang <echo.zhang@mediatek.com>
 */
#ifndef __MMQOS_MTK_H__
#define __MMQOS_MTK_H__

#include <linux/kernel.h>
#include <linux/notifier.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <soc/mediatek/mmqos.h>

#include <soc/mediatek/mtk-interconnect-provider.h>

#define MMQOS_NO_LINK                 (0xffffffff)
#define MMQOS_MAX_COMM_NUM            (3)
#define MMQOS_MAX_COMM_PORT_NUM       (10)
#define MMQOS_COMM_CHANNEL_NUM        (2)
#define MMQOS_MAX_DUAL_PIPE_LARB_NUM  (2)
#define MMQOS_MAX_REPORT_LARB_NUM     (13)
#define MMQOS_MAX_DISP_VIRT_LARB_NUM  (3)
#define RECORD_NUM                    (10)
#define MMQOS_TRACE_MSG_LEN           (1024)

#define MAX_RECORD_COMM_NUM           (2)
#define MAX_RECORD_PORT_NUM           (9)
#define MAX_RECORD_LARB_NUM           (50)
#define MAX_BW_VALUE_NUM              (24)
#define MAX_REG_VALUE_NUM             (8)

enum {
	MD_SCEN_NONE,
	MD_SCEN_SUB6_EXT,
};

struct hrt_record {
	u8 idx;
	u64 time[RECORD_NUM];
	u32 avail_hrt[RECORD_NUM];
	/* Mutex to protect hrt bandwidth-related data */
	struct mutex lock;
};

struct mmqos_hrt {
	u32 hrt_bw[HRT_TYPE_NUM];
	u32 hrt_ratio[HRT_TYPE_NUM];
	u32 hrt_total_bw;
	u32 md_speech_bw[4];
	u32 emi_ratio;
	bool blocking;
	struct hrt_record hrt_rec;
};

struct mmqos_mmpc {
	u32 mmpc_hw_bw;
	u32 mmpc_hw_bw_hrt;
	u32 mmpc_total_mmqos_bw;
	u32 mmpc_total_pmqos_bw;
	u32 mmpc_sw_en_all_on;
};

struct mmqos_vmmrc {
	u32 vmmrc_level_hex;
	u32 vmmrc_mask;
	u32 vmmrc_on_table;
	u32 vmmrc_mask_bit;
	u32 vmmrc_off_table;
};

struct mmqos_base_node {
	struct icc_node *icc_node;
	u32	mix_bw;
};

struct common_node {
	struct mmqos_base_node *base;
	struct device *comm_dev;
	struct regulator *comm_reg;
	const char *clk_name;
	struct clk *clk;
	u64 freq;
	u64 smi_clk;
	u32 volt;
	struct list_head list;
	struct icc_path *icc_path;
	struct icc_path *icc_hrt_path;
	struct work_struct work;
	struct list_head comm_port_list;
};

struct common_port_node {
	struct mmqos_base_node *base;
	struct common_node *common;
	struct device *larb_dev;
	/* Mutex to protect common port bandwidth-related data */
	struct mutex bw_lock;
	u32 latest_mix_bw;
	u64 latest_peak_bw;
	u32 latest_avg_bw;
	u32 old_avg_w_bw;
	u32 old_avg_r_bw;
	u32 old_peak_w_bw;
	u32 old_peak_r_bw;
	struct list_head list;
	u8 channel;
	u8 channel_v2;
	u8 hrt_type;
	u32 write_peak_bw;
	u32 write_avg_bw;
};

struct larb_node {
	struct mmqos_base_node *base;
	struct device *larb_dev;
	struct work_struct work;
	struct icc_path *icc_path;
	u32 old_avg_bw;
	u32 old_peak_bw;
	u8 channel;
	u8 channel_v2;
	u8 dual_pipe_id;
	u16 bw_ratio;
	bool is_write;
	bool is_report_bw_larbs;
};

struct mtk_node_desc {
	const char *name;
	u32 id;
	u32 link;
	u16 bw_ratio;
	u8 channel;
	bool is_write;
};

struct mtk_mmqos_desc {
	const struct mtk_node_desc *nodes;
	const size_t num_nodes;
	const char * const *comm_muxes;
	const char * const *comm_icc_path_names;
	const char * const *comm_icc_hrt_path_names;
	const char * const *larb_icc_path_names;
	const u32 max_ratio;
	const u32 max_disp_ostdl;
	const struct mmqos_hrt hrt;
	const struct mmqos_hrt hrt_LPDDR4;
	const u32 dual_pipe_larbs[MMQOS_MAX_DUAL_PIPE_LARB_NUM];
	const u8 comm_port_channels[MMQOS_MAX_COMM_NUM][MMQOS_MAX_COMM_PORT_NUM];
	const u8 comm_port_hrt_types[MMQOS_MAX_COMM_NUM][MMQOS_MAX_COMM_PORT_NUM];
	const u32 mmqos_state;
	const u32 report_bw_larbs[MMQOS_MAX_REPORT_LARB_NUM];
	const u32 report_bw_real_larbs[MMQOS_MAX_REPORT_LARB_NUM];
	const u32 disp_virt_larbs[MMQOS_MAX_DISP_VIRT_LARB_NUM];
	const u32 freq_mode;
	const struct mmqos_mmpc mmpc_setting;
	const struct mmqos_vmmrc vmmrc_setting;
	const u32 mmqos_log_level;
};

struct last_record {
	u64 larb_port_update_time;
	u32 larb_port_node_id;
	u32 larb_port_avg_bw;
	u32 larb_port_peak_bw;
	u64 larb_update_time;
	u32 larb_node_id;
	u32 larb_avg_bw;
	u32 larb_peak_bw;
};

struct comm_port_bw_record {
	u8 idx[MAX_RECORD_COMM_NUM][MAX_RECORD_PORT_NUM];
	u64 time[MAX_RECORD_COMM_NUM][MAX_RECORD_PORT_NUM][RECORD_NUM];
	u32 larb_id[MAX_RECORD_COMM_NUM][MAX_RECORD_PORT_NUM][RECORD_NUM];
	u32 avg_bw[MAX_RECORD_COMM_NUM][MAX_RECORD_PORT_NUM][RECORD_NUM];
	u32 peak_bw[MAX_RECORD_COMM_NUM][MAX_RECORD_PORT_NUM][RECORD_NUM];
	u32 l_avg_bw[MAX_RECORD_COMM_NUM][MAX_RECORD_PORT_NUM][RECORD_NUM];
	u32 l_peak_bw[MAX_RECORD_COMM_NUM][MAX_RECORD_PORT_NUM][RECORD_NUM];
};

struct chn_bw_record {
	u8 idx[MAX_RECORD_COMM_NUM][MMQOS_COMM_CHANNEL_NUM];
	u64 time[MAX_RECORD_COMM_NUM][MMQOS_COMM_CHANNEL_NUM][RECORD_NUM];
	u32 srt_r_bw[MAX_RECORD_COMM_NUM][MMQOS_COMM_CHANNEL_NUM][RECORD_NUM];
	u32 srt_w_bw[MAX_RECORD_COMM_NUM][MMQOS_COMM_CHANNEL_NUM][RECORD_NUM];
	u32 hrt_r_bw[MAX_RECORD_COMM_NUM][MMQOS_COMM_CHANNEL_NUM][RECORD_NUM];
	u32 hrt_w_bw[MAX_RECORD_COMM_NUM][MMQOS_COMM_CHANNEL_NUM][RECORD_NUM];
};

struct larb_port_bw_record {
	u8 idx[MAX_RECORD_LARB_NUM];
	u64 time[MAX_RECORD_LARB_NUM][RECORD_NUM];
	u32 port_id[MAX_RECORD_LARB_NUM][RECORD_NUM];
	u32 avg_bw[MAX_RECORD_LARB_NUM][RECORD_NUM];
	u32 peak_bw[MAX_RECORD_LARB_NUM][RECORD_NUM];
	u32 mix_bw[MAX_RECORD_LARB_NUM][RECORD_NUM];
	u8 ostdl[MAX_RECORD_LARB_NUM][RECORD_NUM];
};

struct disp_bw {
	u32 disp_srt_r_bw[MMQOS_MAX_COMM_NUM][MMQOS_COMM_CHANNEL_NUM];
	u32 disp_srt_w_bw[MMQOS_MAX_COMM_NUM][MMQOS_COMM_CHANNEL_NUM];
	u32 disp_hrt_r_bw[MMQOS_MAX_COMM_NUM][MMQOS_COMM_CHANNEL_NUM];
	u32 disp_hrt_w_bw[MMQOS_MAX_COMM_NUM][MMQOS_COMM_CHANNEL_NUM];
};

struct chn_bw {
	u32 chn_hrt_r_bw[MMQOS_MAX_COMM_NUM][MMQOS_COMM_CHANNEL_NUM];
	u32 chn_srt_r_bw[MMQOS_MAX_COMM_NUM][MMQOS_COMM_CHANNEL_NUM];
	u32 chn_hrt_w_bw[MMQOS_MAX_COMM_NUM][MMQOS_COMM_CHANNEL_NUM];
	u32 chn_srt_w_bw[MMQOS_MAX_COMM_NUM][MMQOS_COMM_CHANNEL_NUM];
	u32 v2_chn_hrt_r_bw[MMQOS_MAX_COMM_NUM][MMQOS_COMM_CHANNEL_NUM];
	u32 v2_chn_srt_r_bw[MMQOS_MAX_COMM_NUM][MMQOS_COMM_CHANNEL_NUM];
	u32 v2_chn_hrt_w_bw[MMQOS_MAX_COMM_NUM][MMQOS_COMM_CHANNEL_NUM];
	u32 v2_chn_srt_w_bw[MMQOS_MAX_COMM_NUM][MMQOS_COMM_CHANNEL_NUM];
};

struct mtk_mmqos {
	struct platform_device *pdev;
	struct device *dev;
	struct mtk_vcp_device  *vcp_device;
	struct icc_provider prov;
	struct notifier_block nb;
	struct list_head comm_list;
	u32 max_ratio;
	u32 max_disp_ostdl;
	bool dual_pipe_enable;
	bool qos_bound;
	u32 disp_virt_larbs[MMQOS_MAX_DISP_VIRT_LARB_NUM];
	struct proc_dir_entry *proc;
	struct proc_dir_entry *last_proc;
	void __iomem *vmmrc_base;
	u32 apmcu_mask_offset;
	u32 apmcu_mask_bit;
	u32 apmcu_on_bw_offset;
	u32 apmcu_off_bw_offset;
	void __iomem *mminfra_base;
	u32 mmpc_sw_en_all_on;
	u32 mmpc_hw_bw;
	u32 mmpc_hw_bw_hrt;
	u32 mmpc_total_mmqos_bw;
	u32 mmpc_total_pmqos_bw;
	u32 vmmrc_level_hex;
	/* Mutex to protect bandwidth-related data */
	struct mutex bw_lock;
	u32 mmqos_state;
	u32 log_level;
	struct mmqos_hrt *hrt;
	struct last_record *last_rec;
	struct comm_port_bw_record *comm_port_bw_rec;
	struct chn_bw_record *chn_bw_rec;
	struct larb_port_bw_record *larb_port_bw_rec;
	struct disp_bw *disp_bw;
	struct chn_bw *chn_bw;

	u32 on_bw_value[MAX_BW_VALUE_NUM];
	u32 old_on_bw_value[MAX_BW_VALUE_NUM];
	u32 off_bw_value[MAX_BW_VALUE_NUM];
	u32 old_off_bw_value[MAX_BW_VALUE_NUM];
	u32 on_reg_value[MAX_REG_VALUE_NUM];
	u32 off_reg_value[MAX_REG_VALUE_NUM];

	u32 r_hrt_ostdl;
	u32 w_hrt_ostdl;

	phys_addr_t mmqos_memory_iova;
	phys_addr_t mmqos_memory_pa;
	void *mmqos_memory_va;
	bool mmqos_vcp_cb_ready;
	bool mmqos_vcp_init_done;
	struct notifier_block vcp_ready_notifier;
	int vcp_power;
	int mmqos_ipi_status;
};

enum MMQOS_PROFILE_LEVEL {
	MMQOS_PROFILE_SYSTRACE = 1,
	MMQOS_PROFILE_MAX /* Always keep at the end */
};

enum mminfra_freq_mode {
	BY_REGULATOR,
	BY_MMDVFS,
	BY_VMMRC,
};

#define DEFINE_MNODE(_name, _id, _bw_ratio, _is_write, _channel, _link) {	\
	.name = #_name,	\
	.id = _id,	\
	.bw_ratio = _bw_ratio,	\
	.is_write = _is_write,	\
	.channel = _channel,	\
	.link = _link,	\
	}

int mtk_mmqos_v2_probe(struct platform_device *pdev);
int mtk_mmqos_remove(struct platform_device *pdev);

static inline struct mtk_mmqos *mtk_mmqos_get_drv_data(void)
{
	struct platform_device *pdev;
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "mediatek,mt8196-mmqos");
	if (!np) {
		pr_err("can't get mmqos interconnect node\n");
		return NULL;
	}

	pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (!pdev) {
		pr_err("can't get mmqos device\n");
		return NULL;
	}

	return platform_get_drvdata(pdev);
}
#endif /* MMQOS_MTK_H */
