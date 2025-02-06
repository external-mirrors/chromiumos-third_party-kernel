/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _MTK_VCODEC_FW_VCP_H_
#define _MTK_VCODEC_FW_VCP_H_

#include "../decoder/mtk_vcodec_dec_drv.h"
#include "../encoder/mtk_vcodec_enc_drv.h"
#include "mtk_vcodec_fw_priv.h"

typedef void (*vcp_ipi_handler_t) (void *data, unsigned int len, void *priv);
#define MTK_VCODEC_MAX_MQ_NODE_CNT  6

enum mtk_vcp_ipi_max {
	VCP_IPI_ENCODER,
	VCP_IPI_LAT_DECODER,
	VCP_IPI_CORE_DECODER,
	VCP_IPI_MAX,
};

struct mtk_vcodec_msg_queue {
	struct list_head msg_list;
	wait_queue_head_t wq;
	spinlock_t lock;
	atomic_t cnt;
	struct list_head node_list;
};

struct vcp_ipi_desc {
	struct mutex lock;
	vcp_ipi_handler_t handler;
	void *priv;
};

struct share_obj {
	unsigned int id;
	unsigned int len;
	unsigned char share_buf[64];
};

enum vdec_ipi_msg_status {
	VDEC_IPI_MSG_STATUS_OK      = 0,
	VDEC_IPI_MSG_STATUS_FAIL    = -1,
	VDEC_IPI_MSG_STATUS_MAX_INST    = -2,
	VDEC_IPI_MSG_STATUS_ILSEQ   = -3,
	VDEC_IPI_MSG_STATUS_INVALID_ID  = -4,
	VDEC_IPI_MSG_STATUS_DMA_FAIL    = -5,
};

#define VDEC_MSGQ_NUM (2)

struct mtk_vcodec_msg_node {
	struct share_obj ipi_data;
	struct list_head list;
};

struct mtk_vcp {
	bool is_init_done;

	struct mutex ipi_mutex;
	unsigned int msg_signaled[VCP_IPI_MAX];
	wait_queue_head_t msg_wq[VCP_IPI_MAX];

	struct vcp_ipi_desc ipi_desc[VCP_IPI_MAX];
	bool ipi_id_ack[VCP_IPI_MAX];

	struct mtk_vcodec_msg_queue msg_queue;
	struct share_obj share_data;

	struct notifier_block vcp_notify;

	void *vsi_addr;
	void *vsi_core_addr;
	dma_addr_t iova_addr;
	int vsi_size;
};

#if IS_ENABLED(CONFIG_VIDEO_MEDIATEK_VCODEC_VCP)
void *mtk_vcodec_vcp_get_vsi(struct mtk_vcodec_fw *fw, int is_lat);

struct mtk_vcodec_fw *mtk_vcodec_fw_vcp_init(void *priv, enum mtk_vcodec_fw_use fw_use);

dma_addr_t mtk_vcodec_vcp_get_vsi_iova(struct mtk_vcodec_fw *fw);
#else
static inline void *mtk_vcodec_vcp_get_vsi(struct mtk_vcodec_fw *fw, int is_lat)
{
	return NULL;
}

static inline struct mtk_vcodec_fw *
mtk_vcodec_fw_vcp_init(void *priv, enum mtk_vcodec_fw_use fw_use)
{
	return ERR_PTR(-ENODEV);
}

static inline dma_addr_t mtk_vcodec_vcp_get_vsi_iova(struct mtk_vcodec_fw *fw)
{
	return 0;
}
#endif /* CONFIG_VIDEO_MEDIATEK_VCODEC_VCP */

#endif
