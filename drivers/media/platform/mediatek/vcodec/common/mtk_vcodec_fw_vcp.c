// SPDX-License-Identifier: GPL-2.0

#include <linux/delay.h>
#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>
#include <linux/iommu.h>
#include <linux/rpmsg/mtk_vcp_rpmsg.h>
#include <linux/remoteproc/mtk_vcp_public.h>

#include "../decoder/mtk_vcodec_dec_drv.h"
#include "../encoder/mtk_vcodec_enc_drv.h"
#include "../decoder/vdec_ipi_msg.h"
#include "mtk_vcodec_fw_priv.h"
#include "mtk_vcodec_fw_vcp.h"

#define IPI_SEND_TIMEOUT_MS	1000U
#define IPI_TIMEOUT_MS          10000U

void *mtk_vcodec_vcp_get_vsi(struct mtk_vcodec_fw *fw, int is_lat)
{
	if (fw->fw_use == ENCODER)
		return fw->vcp->vsi_addr;

	if (is_lat)
		return fw->vcp->vsi_addr;
	else
		return fw->vcp->vsi_core_addr;
}
EXPORT_SYMBOL_GPL(mtk_vcodec_vcp_get_vsi);

dma_addr_t mtk_vcodec_vcp_get_vsi_iova(struct mtk_vcodec_fw *fw)
{
	return fw->vcp->iova_addr;
}
EXPORT_SYMBOL_GPL(mtk_vcodec_vcp_get_vsi_iova);

static void vcp_ipi_lock(struct mtk_vcp *vcp, u32 ipi_id)
{
	if (WARN_ON(ipi_id >= VCP_IPI_MAX))
		return;

	mutex_lock(&vcp->ipi_desc[ipi_id].lock);
}

static void vcp_ipi_unlock(struct mtk_vcp *vcp, u32 ipi_id)
{
	if (WARN_ON(ipi_id >= VCP_IPI_MAX))
		return;

	mutex_unlock(&vcp->ipi_desc[ipi_id].lock);
}

static int vcp_notify_callback(struct notifier_block *this, unsigned long event, void *ptr)
{
	return NOTIFY_DONE;
}

static void vcp_free_msg_node(struct mtk_vcodec_fw *fw, struct mtk_vcodec_msg_node *msg_node)
{
	unsigned long flags;

	spin_lock_irqsave(&fw->vcp->msg_queue.lock, flags);
	list_add(&msg_node->list, &fw->vcp->msg_queue.node_list);
	spin_unlock_irqrestore(&fw->vcp->msg_queue.lock, flags);
}

static int vcp_ipi_register(struct mtk_vcp *vcp, u32 ipi_id, vcp_ipi_handler_t handler, void *priv)
{
	if (!vcp)
		return -EPROBE_DEFER;

	if (WARN_ON(ipi_id >= VCP_IPI_MAX) || WARN_ON(handler == NULL))
		return -EINVAL;

	vcp_ipi_lock(vcp, ipi_id);
	vcp->ipi_desc[ipi_id].handler = handler;
	vcp->ipi_desc[ipi_id].priv = priv;
	vcp_ipi_unlock(vcp, ipi_id);

	return 0;
}

static int vcp_msg_process_thread(void *arg)
{
	struct mtk_vcodec_fw *fw = arg;
	struct vdec_vpu_ipi_ack *msg = NULL;
	struct share_obj *obj;
	struct mtk_vcodec_msg_node *msg_node;
	unsigned long flags;
	vcp_ipi_handler_t handler;
	int ret = 0;

	do {
		ret = wait_event_interruptible(fw->vcp->msg_queue.wq,
					       atomic_read(&fw->vcp->msg_queue.cnt) > 0);
		if (ret < 0) {
			dev_err(&fw->pdev->dev, "wait ack msg queue timeout %d %d\n",
				ret, atomic_read(&fw->vcp->msg_queue.cnt));
			continue;
		}

		spin_lock_irqsave(&fw->vcp->msg_queue.lock, flags);
		msg_node = list_entry(fw->vcp->msg_queue.msg_list.next,
				      struct mtk_vcodec_msg_node, list);
		list_del(&(msg_node->list));
		atomic_dec(&fw->vcp->msg_queue.cnt);
		spin_unlock_irqrestore(&fw->vcp->msg_queue.lock, flags);

		obj = &msg_node->ipi_data;
		msg = (struct vdec_vpu_ipi_ack *)obj->share_buf;

		if (msg == NULL || !msg->ap_inst_addr) {
			dev_err(&fw->pdev->dev, "invalid ack msg\n");
			vcp_free_msg_node(fw, msg_node);
			continue;
		}

		dev_dbg(&fw->pdev->dev, "ack msg id %d len %d msg_id 0x%x\n", obj->id, obj->len,
			msg->msg_id);

		vcp_ipi_lock(fw->vcp, obj->id);
		handler = fw->vcp->ipi_desc[obj->id].handler;
		if (!handler) {
			dev_err(&fw->pdev->dev, "invalid ack ipi handler id = %d\n", obj->id);
			vcp_ipi_unlock(fw->vcp, obj->id);
			vcp_free_msg_node(fw, msg_node);
			return -EINVAL;
		}

		handler(msg, obj->len, fw->vcp->ipi_desc[obj->id].priv);
		vcp_ipi_unlock(fw->vcp, obj->id);

		fw->vcp->msg_signaled[obj->id] = true;
		wake_up(&fw->vcp->msg_wq[obj->id]);

		vcp_free_msg_node(fw, msg_node);
	} while (!kthread_should_stop());

	return ret;
}

static int vcp_msg_ack_isr(unsigned int id, void *prdata, void *data, unsigned int len)
{
	struct mtk_vcodec_fw *fw = prdata;
	struct mtk_vcodec_msg_queue *msg_queue = &fw->vcp->msg_queue;
	struct mtk_vcodec_msg_node *msg_node;
	struct vdec_vpu_ipi_ack *msg = NULL;
	struct share_obj *obj = data;
	unsigned long flags;

	msg = (struct vdec_vpu_ipi_ack *)obj->share_buf;

	spin_lock_irqsave(&msg_queue->lock, flags);
	if (!list_empty(&msg_queue->node_list)) {
		msg_node = list_entry(msg_queue->node_list.next, struct mtk_vcodec_msg_node, list);

		memcpy(&msg_node->ipi_data, obj, sizeof(struct share_obj));
		list_move_tail(&msg_node->list, &msg_queue->msg_list);
		atomic_inc(&msg_queue->cnt);
		spin_unlock_irqrestore(&msg_queue->lock, flags);

		dev_dbg(&fw->pdev->dev, "push ipi_id %x msg_id %x, ml_cnt %d\n",
			obj->id, msg->msg_id, atomic_read(&msg_queue->cnt));

		wake_up(&msg_queue->wq);
	} else {
		spin_unlock_irqrestore(&msg_queue->lock, flags);
		dev_err(&fw->pdev->dev, "msg queue no free nodes\n");
	}

	return 0;
}

static int vcp_msg_ipi_send(struct mtk_vcodec_fw *fw, int id, void *buf,
			    unsigned int len, unsigned int wait)
{
	struct mtk_vcp *vcp = fw->vcp;
	struct mtk_vcp_device *vcp_device;
	struct mutex *msg_mutex = &vcp->ipi_mutex;
	unsigned int *msg_signaled = &vcp->msg_signaled[id];
	wait_queue_head_t *msg_wq = &vcp->msg_wq[id];
	int ret, ipi_size, feature_id, mailbox_id;
	unsigned long timeout = 0;
	struct share_obj obj;
	unsigned int *data;

	vcp_device = vcp_get(fw->pdev);
	if (!vcp_device) {
		dev_dbg(&fw->pdev->dev, "get vcp device failed\n");
		return 0;
	}

	mutex_lock(msg_mutex);
	feature_id = (fw->fw_use == ENCODER) ? VENC_FEATURE_ID : VDEC_FEATURE_ID;
	mailbox_id = (fw->fw_use == ENCODER) ? IPI_OUT_VENC_0 : IPI_OUT_VDEC_1;
	while (!vcp_device->data->vcp_is_ready(feature_id)) {
		mdelay(1);
		timeout++;
		if (timeout > VCP_SYNC_TIMEOUT_MS) {
			dev_err(&fw->pdev->dev, "vcp is not ready: %d\n", fw->type);
			vcp->ipi_id_ack[id] = -EINVAL;
			goto error;
		}
	}

	if (len > (sizeof(struct share_obj) - sizeof(int32_t) - sizeof(uint32_t))) {
		dev_err(&fw->pdev->dev, "ipi data size wrong %d > %zu\n", len, sizeof(obj));
		vcp->ipi_id_ack[id] = -EINVAL;
		goto error;
	}

	memset(&obj, 0, sizeof(obj));
	memcpy(obj.share_buf, buf, len);

	obj.id = id;
	obj.len = len;

	ipi_size = ((sizeof(u32) * 2) + len + 3) / 4;
	data = (unsigned int *)obj.share_buf;
	dev_dbg(&fw->pdev->dev, "begin to send message id %d len %d data 0x%x\n",
		obj.id, obj.len, data[0]);

	ret = vcp_ipi_send(vcp_get_ipidev(vcp_device), mailbox_id, &obj,
			   ipi_size, IPI_SEND_TIMEOUT_MS);
	if (ret != IPI_ACTION_DONE) {
		dev_err(&fw->pdev->dev, "mtk_ipi_send fail %d\n", ret);
		vcp->ipi_id_ack[id] = -EIO;
		goto error;
	}

wait_ack:
	/* wait for VCP's ACK */
	timeout = msecs_to_jiffies(IPI_TIMEOUT_MS);
	ret = wait_event_timeout(*msg_wq, *msg_signaled, timeout);
	if (ret == 0) {
		vcp->ipi_id_ack[id] = VDEC_IPI_MSG_STATUS_FAIL;
		dev_err(&fw->pdev->dev, "wait ipi ack timeout! %d %d\n", ret, vcp->ipi_id_ack[id]);
	} else if (-ERESTARTSYS == ret) {
		dev_err(&fw->pdev->dev, "wait ipi ack err (%d)\n", vcp->ipi_id_ack[id]);
		goto wait_ack;
	} else if (ret < 0) {
		dev_err(&fw->pdev->dev, "wait ipi ack fail ret %d %d\n", ret, vcp->ipi_id_ack[id]);
		vcp->ipi_id_ack[id] = VDEC_IPI_MSG_STATUS_FAIL;
	}

	dev_dbg(&fw->pdev->dev, "end to receive message: id %d len %d data 0x%x\n",
		obj.id, obj.len, data[0]);
	*msg_signaled = false;

error:
	mutex_unlock(msg_mutex);

	return vcp->ipi_id_ack[id];
}

static int mtk_vcodec_vcp_load_firmware(struct mtk_vcodec_fw *fw)
{
	struct mtk_vcp_device *vcp_device;
	int ret, feature_id, mem_id, mailbox_id, ipi_id;
	void *ipi_device;

	if (fw->vcp->is_init_done) {
		dev_dbg(&fw->pdev->dev, "vcp has already been initialized done.\n");
		return 0;
	}

	vcp_device = vcp_get(fw->pdev);
	if (!vcp_device) {
		dev_dbg(&fw->pdev->dev, "get vcp device failed\n");
		return 0;
	}

	feature_id = (fw->fw_use == ENCODER) ? VENC_FEATURE_ID : VDEC_FEATURE_ID;
	mem_id = (fw->fw_use == ENCODER) ? VENC_MEM_ID : VDEC_MEM_ID;
	mailbox_id = (fw->fw_use == ENCODER) ? IPI_IN_VENC_0 : IPI_IN_VDEC_1;
	ipi_id = (fw->fw_use == ENCODER) ? VCP_IPI_ENCODER : VCP_IPI_LAT_DECODER;

	ipi_device = vcp_get_ipidev(vcp_device);
	if (!ipi_device) {
		dev_dbg(&fw->pdev->dev, "vcodec ipi_device is NULL.\n");
		return -EINVAL;
	}

	ret = vcp_mbox_ipi_register(ipi_device, mailbox_id, vcp_msg_ack_isr, fw, &fw->vcp->share_data);
	if (ret)
		dev_dbg(&fw->pdev->dev, "ipi register fail %d %d %d %d\n", ret, feature_id,
			mem_id, mailbox_id);

	fw->vcp->vcp_notify.notifier_call = vcp_notify_callback;
	fw->vcp->vcp_notify.priority = 1;
	vcp_device->data->vcp_register_notify(feature_id, &fw->vcp->vcp_notify);

	fw->vcp->is_init_done = true;

	mutex_init(&fw->vcp->ipi_desc[ipi_id].lock);

	mutex_init(&fw->vcp->ipi_mutex);

	if (fw->fw_use == ENCODER) {
		kthread_run(vcp_msg_process_thread, fw, "enc_msq_queue_proc");

		fw->vcp->vsi_addr = (char *)vcp_device->data->vcp_get_mem_virt(mem_id);
		fw->vcp->vsi_size = vcp_device->data->vcp_get_mem_size(mem_id);
		fw->vcp->iova_addr = vcp_device->data->vcp_get_mem_iova(mem_id);

		dev_dbg(&fw->pdev->dev, "enc vcp init done => va: %p size:0x%x iova:%pad.\n",
			fw->vcp->vsi_addr, fw->vcp->vsi_size, &fw->vcp->iova_addr);

		init_waitqueue_head(&fw->vcp->msg_wq[VCP_IPI_ENCODER]);

		return 0;
	}

	kthread_run(vcp_msg_process_thread, fw, "dec_msq_queue_proc");

	fw->vcp->vsi_addr = (char *)vcp_device->data->vcp_get_mem_virt(mem_id);
	fw->vcp->vsi_core_addr = fw->vcp->vsi_addr + VCODEC_VSI_LEN;
	fw->vcp->vsi_size = vcp_device->data->vcp_get_mem_size(mem_id);
	fw->vcp->iova_addr = vcp_device->data->vcp_get_mem_iova(mem_id);

	init_waitqueue_head(&fw->vcp->msg_wq[VCP_IPI_LAT_DECODER]);
	init_waitqueue_head(&fw->vcp->msg_wq[VCP_IPI_CORE_DECODER]);

	dev_dbg(&fw->pdev->dev, "dec vcp init done => va: %p size:0x%x iova:%p.\n",
		fw->vcp->vsi_addr, fw->vcp->vsi_size, &fw->vcp->iova_addr);

	return 0;
}

static unsigned int mtk_vcodec_vcp_get_vdec_capa(struct mtk_vcodec_fw *fw)
{
	return 0xC1D20;
}

static unsigned int mtk_vcodec_vcp_get_venc_capa(struct mtk_vcodec_fw *fw)
{
	return 0x1;
}

static void *mtk_vcodec_vcp_dm_addr(struct mtk_vcodec_fw *fw, u32 dtcm_dmem_addr)
{
	return scp_mapping_dm_addr(fw->scp, dtcm_dmem_addr);
}

static int mtk_vcodec_vcp_set_ipi_register(struct mtk_vcodec_fw *fw, int id,
					   mtk_vcodec_ipi_handler handler,
					   const char *name, void *priv)
{
	return vcp_ipi_register(fw->vcp, id, handler, priv);
}

static int mtk_vcodec_vcp_ipi_send(struct mtk_vcodec_fw *fw, int id, void *buf,
				   unsigned int len, unsigned int wait)
{
	return vcp_msg_ipi_send(fw, id, buf, len, wait);
}

static void mtk_vcodec_vcp_release(struct mtk_vcodec_fw *fw)
{

}

static struct device *mtk_vcodec_vcp_get_io_dev(struct mtk_vcodec_fw *fw)
{
	struct mtk_vcp_device *vcp_device;

	vcp_device = vcp_get(fw->pdev);
	if (!vcp_device) {
		dev_dbg(&fw->pdev->dev, "get vcp device failed\n");
		return 0;
	}

	return vcp_device->dev;
}

static const struct mtk_vcodec_fw_ops mtk_vcodec_vcp_msg = {
	.load_firmware = mtk_vcodec_vcp_load_firmware,
	.get_vdec_capa = mtk_vcodec_vcp_get_vdec_capa,
	.get_venc_capa = mtk_vcodec_vcp_get_venc_capa,
	.map_dm_addr = mtk_vcodec_vcp_dm_addr,
	.ipi_register = mtk_vcodec_vcp_set_ipi_register,
	.ipi_send = mtk_vcodec_vcp_ipi_send,
	.release = mtk_vcodec_vcp_release,
	.get_io_dev = mtk_vcodec_vcp_get_io_dev,
};

struct mtk_vcodec_fw *mtk_vcodec_fw_vcp_init(void *priv, enum mtk_vcodec_fw_use fw_use)
{
	struct mtk_vcodec_fw *fw;
	struct platform_device *plat_dev;
	struct mtk_vcodec_msg_node *msg_node;
	int i;

	if (fw_use == ENCODER) {
		struct mtk_vcodec_enc_dev *enc_dev = priv;

		plat_dev = enc_dev->plat_dev;
	} else if (fw_use == DECODER) {
		struct mtk_vcodec_dec_dev *dec_dev = priv;

		plat_dev = dec_dev->plat_dev;
	} else {
		pr_err("Invalid fw_use %d (use a reasonable fw id here)\n", fw_use);
		return ERR_PTR(-EINVAL);
	}

	fw = devm_kzalloc(&plat_dev->dev, sizeof(*fw), GFP_KERNEL);
	if (!fw)
		return ERR_PTR(-ENOMEM);

	fw->type = VCP;
	fw->pdev = plat_dev;
	fw->fw_use = fw_use;
	fw->ops = &mtk_vcodec_vcp_msg;
	fw->vcp = devm_kzalloc(&plat_dev->dev, sizeof(*(fw->vcp)), GFP_KERNEL);
	if (!fw->vcp) {
		dev_dbg(&fw->pdev->dev, "vcodec vcp initialized fail.\n");
		return ERR_PTR(-ENOMEM);
	}

	INIT_LIST_HEAD(&fw->vcp->msg_queue.msg_list);
	spin_lock_init(&fw->vcp->msg_queue.lock);
	init_waitqueue_head(&fw->vcp->msg_queue.wq);
	atomic_set(&fw->vcp->msg_queue.cnt, 0);

	INIT_LIST_HEAD(&fw->vcp->msg_queue.node_list);
	for (i = 0; i < MTK_VCODEC_MAX_MQ_NODE_CNT; i++) {
		msg_node = devm_kzalloc(&plat_dev->dev, sizeof(*msg_node), GFP_KERNEL);
		if (!msg_node) {
			dev_dbg(&fw->pdev->dev, "vcp msg queue initialized fail %d\n", i);
			return ERR_PTR(-ENOMEM);
		}
		list_add(&msg_node->list, &fw->vcp->msg_queue.node_list);
	}

	return fw;
}
