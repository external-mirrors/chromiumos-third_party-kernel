/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 MediaTek Inc.
 */

#ifndef __MTK_VCP_RPROC_H__
#define __MTK_VCP_RPROC_H__

#include <linux/arm-smccc.h>
#include <linux/firmware.h>
#include <linux/io.h>
#include <linux/soc/mediatek/mtk_sip_svc.h>
#include <linux/remoteproc/mtk_vcp_public.h>
#include <linux/remoteproc.h>

/* vcp mcu executor code via static iova */
#define IMG_MEMORY_STATIC_IOVA		(0x180600000)

/* vcp timeout definition */
#define VCP_READY_TIMEOUT_MS 3000
#define VCP_AWAKE_TIMEOUT 1000
#define WAKE_POLL_TIMES 100
#define USDELAY_RANGE_MIN 1000
#define USDELAY_RANGE_MAX 2000
#define SUSPEND_WAIT_TIMEOUT_MS 100

/* vcp platform define */
#define SUSPEND_IPI_MAGIC 0x87654321
#define RESUME_IPI_MAGIC 0x12345678
#define MBOX_COUNT 5
#define DMA_MAX_MASK_BIT 33
#define PIN_OUT_C_SIZE_SLEEP_0           2
#define PIN_OUT_SIZE_TEST_0              3
#define PIN_OUT_C_SIZE_SLEEP_1           2
#define PIN_OUT_SIZE_TEST_1              3

#define VCP_PACK_IOVA(addr)     ((uint32_t)((addr) | (((uint64_t)(addr) >> 32) & 0xF)))
#define VCP_UNPACK_IOVA(addr)   \
	((uint64_t)(addr & 0xFFFFFFF0) | (((uint64_t)(addr) & 0xF) << 32))

/* vcp Core ID definition */
enum vcp_core_id {
	VCP_ID          = 0,
	MMUP_ID         = 1,
	VCP_CORE_TOTAL  = 2,
};

enum mtk_ipi_dev {
	IPI_DEV_VCP = 0,
	IPI_DEV_TOTAL,
};

/* vcp kernel smc server id */
enum mtk_tinysys_vcp_kernel_op {
	MTK_TINYSYS_VCP_KERNEL_OP_RESET_SET = 0,
	MTK_TINYSYS_VCP_KERNEL_OP_RESET_RELEASE,
	MTK_TINYSYS_VCP_KERNEL_OP_COLD_BOOT_VCP,
	MTK_TINYSYS_MMUP_KERNEL_OP_RESET_SET,
	MTK_TINYSYS_MMUP_KERNEL_OP_RESET_RELEASE,
	MTK_TINYSYS_MMUP_KERNEL_OP_SET_L2TCM_OFFSET,
	MTK_TINYSYS_MMUP_KERNEL_OP_SET_FW_SIZE,
	MTK_TINYSYS_MMUP_KERNEL_OP_COLD_BOOT_MMUP,
	MTK_TINYSYS_VCP_KERNEL_OP_NUM,
};

enum {
	SLP_WAKE_LOCK = 0,
	SLP_WAKE_UNLOCK,
	SLP_STATUS_DBG,
	SLP_SUSPEND,
	SLP_RESUME,
};

struct slp_ctrl_data {
	u32 feature;
	u32 cmd;
};

/* vcp work struct definition */
struct vcp_work_struct {
	struct work_struct work;
	struct device *dev;
	u32 flags;
	u32 id;
};

struct mtk_vcp_of_cluster {
	void __iomem *sram_base;
	void __iomem *cfg;
	void __iomem *mbox_base;
	void __iomem *mbox_init;
	void __iomem *cfg_core;
	void __iomem *vcp_rdy;
	u32 sram_size;
	u32 core_nums;
	u32 msg_vcp_ready0;
	u32 msg_vcp_ready1;
	u32 slp_ipi_ack_data;
	u32 twohart[VCP_CORE_TOTAL];
	u32 sram_offset[VCP_CORE_TOTAL];
	int pwclkcnt;
	bool is_suspending;
	struct vcp_work_struct vcp_ready_notify_wk[VCP_CORE_TOTAL];
};

struct vcp_reserve_mblock {
	enum vcp_reserve_mem_id_t num;
	phys_addr_t start_phys;
	dma_addr_t start_iova;
	void __iomem *start_virt;
	size_t size;
};

struct vcp_feature_tb {
	u32 feature;
	enum vcp_core_id core_id;
	u32 enable;
};

struct vcp_region_info_st {
	u32 ap_loader_start; /* store loader addr[35:32] in addr[3:0] */
	u32 ap_loader_size;
	u32 ap_firmware_start; /* store firmware addr[35:32] in addr[3:0] */
	u32 ap_firmware_size;
	u32 ap_dram_start; /* store dram img addr[35:32] in addr[3:0] */
	u32 ap_dram_size;
	u32 ap_dram_backup_start; /* store backup dram img addr[35:32] in addr[3:0] */
	/*
	 * This is the size of the structure.
	 * It can act as a version number if entries can only be
	 * added to (not deleted from) the structure.
	 * It should be the first entry of the structure, but for
	 * compatibility reason, it is appended here.
	 */
	u32 struct_size;
	u32 l2tcm_offset;
	u32 TaskContext_ptr;
	u32 vcpctl;
	u32 regdump_start;
	u32 regdump_size;
	u32 ap_params_start;
	u32 sramlog_buf_offset;
	u32 sramlog_end_idx_offset;
	u32 sramlog_buf_maxlen;
	u32 ap_loader_start_pa;
	u32 coredump_offset;
	u32 coredump_dram_offset;
};

/* vcp ipi awake callback */
int vcp_awake_lock(void *_vcp_id);
int vcp_awake_unlock(void *_vcp_id);

/* vcp loader */
int mtk_vcp_load(struct rproc *rproc, const struct firmware *fw);

/* vcp irq */
int vcp_wdt_irq_init(struct mtk_vcp_device *vcp);

#endif
