/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015 MediaTek Inc.
 */

#ifndef MTK_CRTC_H
#define MTK_CRTC_H

#include <linux/completion.h>
#include <drm/drm_crtc.h>
#include <drm/drm_vblank_work.h>
#include "mtk_ddp_comp.h"
#include "mtk_drm_drv.h"
#include "mtk_plane.h"

#define MTK_DEFAULT_MAX_BPC	10
#define MTK_MIN_BPC		5

/**
 * struct mtk_crtc_crc - crc related information
 * @ofs: register offset of crc
 * @rst_ofs: register offset of crc reset
 * @rst_msk: register mask of crc reset
 * @cnt: count of crc
 * @va: pointer to the start of crc array
 * @pa: physical address of the crc for gct to access
 * @cmdq_event: the event to trigger the cmdq
 * @cmdq_reg: address of the register that cmdq is going to access
 * @cmdq_client: handler to control cmdq (mbox channel, thread ... etc.)
 * @cmdq_handle: cmdq packet to store the commands
 */
struct mtk_crtc_crc {
	const u32 *ofs;
	u32 rst_ofs;
	u32 rst_msk;
	size_t cnt;
	u32 *va;
#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	dma_addr_t pa;
	u32 cmdq_event;
	struct cmdq_client_reg *cmdq_reg;
	struct cmdq_client cmdq_client;
	struct cmdq_pkt cmdq_handle;
#endif
};

enum DISP_ATF_CMD {
	DISP_ATF_CMD_CONFIG_DISP_CONFIG, /* For display master to disable secure */
	DISP_ATF_CMD_COUNT,
};

/*
 * struct mtk_crtc - MediaTek specific crtc structure.
 * @base: crtc object.
 * @enabled: records whether crtc_enable succeeded
 * @planes: array of 4 drm_plane structures, one for each overlay plane
 * @pending_planes: whether any plane has pending changes to be applied
 * @mmsys_dev: pointer to the mmsys device for configuration registers
 * @mutex: handle to one of the ten disp_mutex streams
 * @ddp_comp_nr: number of components in ddp_comp
 * @ddp_comp: array of pointers the mtk_ddp_comp structures used by this crtc
 *
 * TODO: Needs update: this header is missing a bunch of member descriptions.
 */
struct mtk_crtc {
	struct drm_crtc			base;
	bool				enabled;

	bool				pending_needs_vblank;
	struct drm_pending_vblank_event	*event;

	struct drm_plane		*planes;
	unsigned int			layer_nr;
	bool				pending_planes;
	bool				pending_async_planes;

#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	struct cmdq_client		cmdq_client;
	struct cmdq_pkt			cmdq_handle;
	u32				cmdq_event;
	u32				cmdq_vblank_cnt;
	wait_queue_head_t		cb_blocking_queue;
	struct task_struct		*cmdq_done_task;
	atomic_t			cmdq_done;
	struct completion               fast_modeset_done;

	struct cmdq_client		sec_cmdq_client;
	bool				sec_cmdq_working;
	wait_queue_head_t		sec_cb_blocking_queue;
#endif

	struct device			*mmsys_dev[MAX_MMSYS];
	struct device			*dma_dev;
	struct device			*vdisp_ao_dev;
	struct device			*dpc_dev;
	struct device			*pmqos_dev;
	struct mtk_mutex		*mutex[MAX_MMSYS];
	unsigned int			ddp_comp_nr;
	struct mtk_ddp_comp		**ddp_comp;
	enum mtk_drm_mmsys		*ddp_comp_sys;
	bool				exist[MAX_MMSYS];
	unsigned int			num_conn_routes;
	const struct mtk_drm_route	*conn_routes;
	enum mtk_drm_mmsys		conn_routes_sys;

	/* lock for display hardware access */
	struct mutex			hw_lock;
	bool				config_updating;
	/* lock for config_updating to cmd buffer */
	spinlock_t			config_lock;

	/* support crc */
	struct mtk_ddp_comp		*crc_provider;
	struct drm_vblank_work		crc_work;

	bool				sec_on;
	/* QoS setting*/
	struct mtk_crtc_qos_ctx		*qos_ctx;
};

void mtk_crtc_check_fast_modeset(struct drm_crtc_state *old_crtc_state,
				 struct drm_crtc_state *new_crtc_state);
void mtk_crtc_commit(struct drm_crtc *crtc);
int mtk_crtc_create(struct drm_device *drm_dev,
		    enum mtk_crtc_path path_sel);
void mtk_crtc_disable_secure_state(struct drm_crtc *crtc);
int mtk_crtc_plane_check(struct drm_crtc *crtc, struct drm_plane *plane,
			 struct mtk_plane_state *state);
void mtk_crtc_plane_disable(struct drm_crtc *crtc, struct drm_plane *plane);
void mtk_crtc_async_update(struct drm_crtc *crtc, struct drm_plane *plane,
			   struct drm_atomic_state *plane_state);
struct device *mtk_crtc_dma_dev_get(struct drm_crtc *crtc);

#if IS_REACHABLE(CONFIG_MTK_CMDQ)
void mtk_crtc_destroy_crc_cmdq(struct mtk_crtc_crc *crc);
void mtk_crtc_create_crc_cmdq(struct mtk_crtc_crc *crc, struct mtk_crtc *data);
void mtk_crtc_stop_crc_cmdq(struct mtk_crtc_crc *crc);
void mtk_crtc_start_crc_cmdq(struct mtk_crtc_crc *crc);
void mtk_crtc_read_crc(struct mtk_crtc_crc *crc);
#endif

#endif /* MTK_CRTC_H */
