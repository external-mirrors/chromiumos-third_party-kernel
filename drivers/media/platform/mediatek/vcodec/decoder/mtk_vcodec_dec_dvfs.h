/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _MTK_VCODEC_DEC_DVFS_H_
#define _MTK_VCODEC_DEC_DVFS_H_

#include <linux/types.h>
#include <linux/list.h>
#include <linux/timer.h>

#include "mtk_vcodec_dec_drv.h"
#include "vdec_ipi_msg.h"

#define VDEC_DUMP_MMPC 0

#define MAX_OP_CNT 5

#define VDEC_TPUT_ITEM_NUM 4
#define VDEC_OPP_NUM 6
#define VDEC_TPUT_CNT 4

#define VDEC_LARB_HW_NUM 8
#define VDEC_LARB_PORT_ITEM 5

#define VDEC_LARB_NUM	6

#define MTK_VCODEC_QOS_TYPE 2
#define MTK_SMI_MAX_MON_REQ 4
#define MTK_SMI_MAX_MON_FRM 8

#define VDEC_MMPC_BW_CTRL_LAT   0x54
#define VDEC_MMPC_BW_CTRL_CORE  0x5c
#define VDEC_MMPC_BW_SET_DONE   0x50
#define VDEC_HFRP_HW_BW_0       0xA94
#define VDEC_HFRP_HW_BW_SRT     0xAE0
#define VDEC_HFRP_HW_BW_NUM     8

enum vcodec_larb_type {
	VCODEC_LARB_READ = 0,
	VCODEC_LARB_WRITE = 1,
	VCODEC_LARB_READ_WRITE = 2,
	VCODEC_LARB_SUM = 3
};

struct vcodec_larb_bw {
	u32 hw_id;
	u32 bw_r;
	u32 bw_w;
	u32 larb_id;
};

enum mtk_vdec_dvfs_hw_id {
	MTK_VDEC_DVFS_CORE = 0,
	MTK_VDEC_DVFS_LAT = 1,
	MTK_VDEC_DVFS_CORE1 = 2,
	MTK_VDEC_DVFS_LAT1 = 3,
	MTK_VDEC_DVFS_LINE_COUNT = 4,
	MTK_VDEC_DVFS_HW_NUM = 5,
};

/* performance point per config */
struct vcodec_perf {
	u8 codec_type;
	u32 codec_fmt;
	s32 config;
	u32 cy_per_mb_1; /* for I/P */
	u32 cy_per_mb_2; /* for I/P/B */
};

struct bw_node {
	unsigned int r;
	unsigned int w;
};

struct vcodec_pc_bw {
	unsigned int hw_id;
	unsigned int reg_offset;
	struct bw_node base;
	struct bw_node target;
};

/* config selection criteria */
struct vcodec_config {
	u8 codec_type;
	u32 codec_fmt;
	u32 mb_thresh; /* applicable mb threshold */
	s32 config_1; /* low power config */
	s32 config_2; /* hgih quality config */
};

/* instance info for dvfs */
struct vcodec_inst {
	int id;
	u8 codec_type;
	u32 codec_fmt;
	u32 capture_fourcc;
	u32 width;
	u32 height;
	u64 last_access;
	u32 freq_value;
	struct list_head list;
	struct mtk_vcodec_dec_ctx *ctx;
};

/* dvfs policies  */
struct dvfs_params {
	u8 codec_type;
	u8 allow_oc;		/* allow oc freq */
	u8 per_frame_adjust;	/* do per frame adjust dvfs / pmqos */
	u32 min_freq;		/* min freq */
	u32 normal_max_freq;	/* normal max freq (no oc) */
	u32 freq_sum;		/* summation of all instances */
	u32 target_freq;	/* target freq */
	u32 work_freq;	        /* the hardware working freq */
	u8 lock_cnt[MTK_VDEC_DVFS_HW_NUM]; /* lock cnt */
	u32 target_bw_factor; /* target bw = base bw * target_bw_factor*/
	u32 os_bw[2];		/* additional bw for overspec, (> 0 => support overspec) */
	struct timer_list vdec_active_checker;
	u8 has_timer;
	u8 trans_inst; /* transcode scenario: vdec/venc */
	u8 init_boost;
	u32 last_boost_time;
};

struct mtk_vdec_hw_dev;
struct mtk_vcodec_dec_dvfs {
	struct mtk_vcodec_dec_dev *main_dev;
	struct mtk_vdec_hw_dev *sub_dev;

	struct vcodec_perf *vdec_tput;

	struct regulator *vdec_reg;
	struct clk *vdec_mmdvfs_clk;
	int vdec_freq_cnt;

	int vdec_tput_cnt;
	int vdec_larb_cnt;

	unsigned long vdec_freqs[VDEC_OPP_NUM];

	struct dvfs_params vdec_dvfs_params;

	struct vcodec_larb_bw *vdec_larb_bw;

	struct vcodec_pc_bw *vdec_pc_bw;

	struct plist_head vdec_rlist[MTK_VDEC_DVFS_HW_NUM];
	struct icc_path *vdec_qos_req[VDEC_LARB_NUM];

	struct list_head vdec_dvfs_inst;

	bool dvfs_params_updated;
};

extern int mtk_mmdvfs_level;
#if defined(CONFIG_DEBUG_FS)
#define mtk_dvfs_debug(dev, level, fmt, args...)                                         \
do {                                                                                     \
	if (level <= mtk_mmdvfs_level)                                                    \
		dev_err(dev, "[mtk_dvfs] %s, %d: " fmt "\n", __func__, __LINE__, ##args); \
} while (0)

#define mtk_dvfs_err(dev, fmt, args...)                                              \
	dev_err(dev, "[mtk_dvfs] %s, %d: " fmt "\n", __func__, __LINE__, ##args)

#else
#define mtk_dvfs_debug(dev, level, fmt, args...)
#define mtk_dvfs_err(dev, fmt, args...)
#endif

int mtk_vcodec_dvfs_tbl_init(struct mtk_vcodec_dec_dvfs *vdec_dvfs);
void mtk_vcodec_dvfs_tbl_deinit(struct mtk_vcodec_dec_dvfs *vdec_dvfs);

bool mtk_vcodec_dvfs_update_picinfo(struct mtk_vcodec_dec_ctx *ctx,
				    struct mtk_vcodec_dec_dvfs *vdec_dvfs);
bool mtk_vcodec_dvfs_remove_picinfo(struct mtk_vcodec_dec_ctx *ctx,
				    struct mtk_vcodec_dec_dvfs *vdec_dvfs);

void mtk_vcodec_dvfs_update_freq(struct mtk_vcodec_dec_dvfs *vdec_dvfs, bool is_removed);
int mtk_prepare_vdec_dvfs(struct mtk_vcodec_dec_dvfs *vdec_dvfs);
void vcodec_dvfs_helper_set_opp(struct mtk_vcodec_dec_ctx *ctx, int hw_id);

void mtk_vdec_dvfs_begin_inst(struct mtk_vcodec_dec_ctx *ctx, int hw_id);
void mtk_vdec_dvfs_end_inst(struct mtk_vcodec_dec_ctx *ctx, int hw_id);

void vcodec_pmqos_helper_update(struct mtk_vcodec_dec_ctx *ctx, int hw_idx);

void mtk_vdec_mmpc_update(struct mtk_vcodec_dec_ctx *ctx, int hw_id);

void mtk_vdec_fill_dvfs_params(struct mtk_vcodec_dec_dev *main_dev, uint32_t *data);

#endif
