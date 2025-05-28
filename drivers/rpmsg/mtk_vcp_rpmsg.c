// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Copyright (c) 2024 MediaTek Inc.
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/freezer.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rpmsg/mtk_rpmsg.h>
#include <linux/rpmsg/mtk_vcp_rpmsg.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/sched/clock.h>
#include <linux/time64.h>

#include "rpmsg_internal.h"

int vcp_mbox_send(struct mtk_ipi_device *ipidev, u32 id, void *buf,
		  u32 len, u32 wait)
{
	struct mtk_mbox_device *mbdev;
	struct mtk_mbox_channel_info *mchan;
	struct mtk_mbox_info *minfo;
	u32 status;
	unsigned long flags;

	mbdev = ipidev->mbdev;
	if (WARN_ON(!mbdev) || WARN_ON(!ipidev->table))
		return -EINVAL;
	mchan = ipidev->table[id].rpchan;
	if (WARN_ON(!mchan))
		return -EINVAL;
	if (WARN_ON(len > mchan->send_slot_size) || WARN_ON(!buf))
		return -EINVAL;

	spin_lock_irqsave(&mchan->channel_lock, flags);
	minfo = &mbdev->info_table[mchan->mbox];
	status = readl(minfo->set_irq_reg) & (0x1 << mchan->send_pin_index);

	if (status != 0) {
		spin_unlock_irqrestore(&mchan->channel_lock, flags);
		return MBOX_PIN_BUSY;
	}
	spin_unlock_irqrestore(&mchan->channel_lock, flags);

	if (mchan->send_slot > minfo->slot)
		return MBOX_WRITE_SZ_ERR;

	__iowrite32_copy(minfo->base + mchan->send_slot * MBOX_SLOT_SIZE,
			 buf, len);

	/*
	 * Ensure that all writes to SRAM are committed before sending the
	 * interrupt to mbox.
	 */
	mb();

	spin_lock_irqsave(&minfo->mbox_lock, flags);
	writel(0x1 << mchan->send_pin_index, minfo->set_irq_reg);
	spin_unlock_irqrestore(&minfo->mbox_lock, flags);

	return MBOX_DONE;
}
EXPORT_SYMBOL(vcp_mbox_send);

static irqreturn_t vcp_mbox_isr(int irq, void *dev_id)
{
	struct mtk_mbox_pin_recv *pin_recv;
	struct mtk_mbox_info *minfo = (struct mtk_mbox_info *)dev_id;
	struct mtk_mbox_device *mbdev = minfo->mbdev;
	unsigned long flags;
	u32 pin, mbox, irq_status, irq_temp = 0;
	int ret = MBOX_DONE;

	mbox = minfo->id;

	/* get irq status */
	spin_lock_irqsave(&minfo->mbox_lock, flags);
	irq_status = readl(minfo->clr_irq_reg);
	spin_unlock_irqrestore(&minfo->mbox_lock, flags);

	if (mbdev->pre_cb && mbdev->pre_cb(mbdev->prdata)) {
		ret = MBOX_PRE_CB_ERR;
		goto skip;
	}

	/* execute all receive pin handler */
	for (pin = 0; pin < mbdev->recv_count; pin++) {
		pin_recv = &mbdev->pin_recv_table[pin];
		if (pin_recv->mbox != mbox)
			continue;

		/* recv irq trigger */
		if (BIT(pin_recv->pin_index) & irq_status) {
			irq_temp |= BIT(pin_recv->pin_index);

			/* check user buf */
			if (!pin_recv->pin_buf) {
				dev_err(mbdev->dev, "%s:null ptr dev=%s ipi_id=%d", __func__,
					mbdev->name, pin_recv->chan_id);
				continue;
			}
			/* direct mode */
			if (pin_recv->offset >  minfo->slot) {
				ret = MBOX_READ_SZ_ERR;
				dev_err(mbdev->dev, "%s cp to buf fail, mbox=%s chan=%d ret=%d",
					__func__, mbdev->name, pin_recv->chan_id, ret);
			} else {
				__ioread32_copy(pin_recv->pin_buf,
						minfo->base + pin_recv->offset * MBOX_SLOT_SIZE,
						pin_recv->msg_size);
				if (pin_recv->recv_opt == MBOX_RECV_MESSAGE &&
				    pin_recv->mbox_pin_cb)
					pin_recv->mbox_pin_cb(pin_recv->chan_id,
							      pin_recv->prdata,
							      pin_recv->pin_buf,
							      pin_recv->msg_size * MBOX_SLOT_SIZE);
			}
		}
	}

	if (mbdev->post_cb && mbdev->post_cb(mbdev->prdata))
		ret = MBOX_POST_CB_ERR;

skip:
	if (ret == MBOX_PRE_CB_ERR)
		dev_warn(mbdev->dev, "%s pre_cb error, skip cb handle, name=%s ret=%d",
			 __func__, mbdev->name, ret);
	else if (ret == MBOX_POST_CB_ERR)
		dev_warn(mbdev->dev, "%s post_cb error, dev=%s ret=%d",
			 __func__, mbdev->name, ret);

	/* clear irq status */
	spin_lock_irqsave(&minfo->mbox_lock, flags);
	writel(irq_temp, minfo->clr_irq_reg);
	spin_unlock_irqrestore(&minfo->mbox_lock, flags);

	if (irq_temp == 0 && irq_status != 0) {
		dev_err(mbdev->dev, "%s name=%s pin table err, status=%x",
			__func__, mbdev->name, irq_status);
		for (pin = 0; pin < mbdev->recv_count; pin++)
			pin_recv = &mbdev->pin_recv_table[pin];
	}

	/* notify all receive pin handler */
	for (pin = 0; pin < mbdev->recv_count; pin++) {
		pin_recv = &mbdev->pin_recv_table[pin];
		if (pin_recv->mbox != mbox)
			continue;

		/* recv irq trigger */
		if (BIT(pin_recv->pin_index) & irq_status) {
			/* notify task */
			if (mbdev->ipi_cb)
				mbdev->ipi_cb(pin_recv, mbdev->ipi_priv);
		}
	}

	return IRQ_HANDLED;
}

static void ipi_isr_cb(struct mtk_mbox_pin_recv *pin, void *priv)
{
	struct mtk_ipi_device *ipidev = priv;
	u32 ipi_id = pin->chan_id;
	atomic_t holder = ipidev->table[ipi_id].holder;

	if (pin->recv_opt == MBOX_RECV_MESSAGE || atomic_read(&holder))
		complete(&pin->notify);
}

int vcp_ipi_send(struct mtk_ipi_device *ipidev, u32 id,
		 void *data, u32 len, u32 timeout_ms)
{
	struct mtk_mbox_pin_send *pin;
	struct device *dev;
	int ret, rpmsg_ret;

	if (!ipidev || !ipidev->ipi_inited || !ipidev->mbdev)
		return IPI_UNAVAILABLE;

	pin = ipidev->table[id].pin_send;
	if (!pin)
		return IPI_UNAVAILABLE;
	dev = ipidev->mbdev->dev;
	if (!dev)
		return IPI_UNAVAILABLE;

	if (len > pin->msg_size)
		return IPI_NO_MEMORY;
	else if (!len)
		len = pin->msg_size;

	if (ipidev->pre_cb && ipidev->pre_cb(ipidev->prdata)) {
		dev_err(dev, "IPI [%s] pre_cb fail.\n",
			ipidev->table[id].rpchan->info.name);
		return IPI_PRE_CB_FAIL;
	}

	/* WAIT Mode: NOT be allowed in atomic/interrupt/IRQ disabled */
	if (preempt_count() || in_interrupt() || irqs_disabled()) {
		dev_err(dev, "%s pin# %d.\n", ipidev->name, id);
		WARN_ON_ONCE(1);
	}

	mutex_lock(&pin->mutex_send);
	ret = read_poll_timeout_atomic(rpmsg_trysend, rpmsg_ret, !rpmsg_ret,
				       USEC_PER_MSEC, timeout_ms * USEC_PER_MSEC,
				       false, ipidev->table[id].ept, data, len);
	mutex_unlock(&pin->mutex_send);

	if (ipidev->post_cb && ipidev->post_cb(ipidev->prdata)) {
		dev_err(dev, "[%s] post_cb fail\n",
			ipidev->table[id].rpchan->info.name);
		return IPI_POST_CB_FAIL;
	}

	if (ret == -ETIMEDOUT) {
		dev_err(dev, "%s IPI %d send fail %d\n", ipidev->name, id, rpmsg_ret);
		return IPI_RPMSG_ERR;
	}

	return IPI_ACTION_DONE;
}
EXPORT_SYMBOL(vcp_ipi_send);

int vcp_ipi_send_compl(struct mtk_ipi_device *ipidev, u32 id,
		       void *data, u32 len, u32 timeout_ms)
{
	struct mtk_mbox_pin_send *pin_s;
	struct mtk_mbox_pin_recv *pin_r;
	struct device *dev;
	int ret, rpmsg_ret;

	if (!ipidev || !ipidev->ipi_inited || !ipidev->mbdev)
		return IPI_DEV_ILLEGAL;

	pin_s = ipidev->table[id].pin_send;
	pin_r = ipidev->table[id].pin_recv;
	if (!pin_s || !pin_r)
		return IPI_UNAVAILABLE;
	dev = ipidev->mbdev->dev;
	if (!dev)
		return IPI_UNAVAILABLE;

	if (len > pin_s->msg_size)
		return IPI_NO_MEMORY;
	else if (!len)
		len = pin_s->msg_size;

	if (ipidev->pre_cb && ipidev->pre_cb(ipidev->prdata)) {
		dev_err(dev, "IPI [%s] pre_cb fail.\n",
			ipidev->table[id].rpchan->info.name);
		return IPI_PRE_CB_FAIL;
	}

	/* WAIT Mode: NOT be allowed in atomic/interrupt/IRQ disabled */
	if (preempt_count() || in_interrupt() || irqs_disabled()) {
		dev_err(dev, "%s pin# %d\n", ipidev->name, id);
		WARN_ON_ONCE(1);
	}

	mutex_lock(&pin_s->mutex_send);

	atomic_inc(&ipidev->table[id].holder);

	ret = read_poll_timeout_atomic(rpmsg_trysend, rpmsg_ret, !rpmsg_ret,
				       USEC_PER_MSEC, timeout_ms * USEC_PER_MSEC,
				       false, ipidev->table[id].ept, data, len);
	if (ret) {
		mutex_unlock(&pin_s->mutex_send);
		atomic_set(&ipidev->table[id].holder, 0);

		if (ipidev->post_cb && ipidev->post_cb(ipidev->prdata)) {
			dev_err(dev, "IPI [%s] post_cb fail.\n",
				ipidev->table[id].rpchan->info.name);
			return IPI_POST_CB_FAIL;
		}

		dev_err(dev, "%s IPI %d send fail %d\n", ipidev->name, id, rpmsg_ret);
		return IPI_PIN_BUSY;
	}

	/* wait for completion */
	ret = wait_for_completion_timeout(&pin_r->notify,
					  msecs_to_jiffies(timeout_ms));
	atomic_set(&ipidev->table[id].holder, 0);
	if (ret > 0)
		ret = IPI_ACTION_DONE;

	mutex_unlock(&pin_s->mutex_send);

	if (ipidev->post_cb && ipidev->post_cb(ipidev->prdata)) {
		dev_err(dev, "IPI [%s] post_cb fail\n",
			ipidev->table[id].rpchan->info.name);
		return IPI_POST_CB_FAIL;
	}

	return ret;
}
EXPORT_SYMBOL(vcp_ipi_send_compl);

int vcp_mbox_ipi_register(struct mtk_ipi_device *ipidev, int id,
			  mbox_pin_cb_t cb, void *prdata, void *msg)
{
	struct mtk_mbox_pin_recv *pin_recv;

	if (!ipidev || !ipidev->ipi_inited)
		return IPI_DEV_ILLEGAL;
	if (!msg)
		return IPI_NO_MSGBUF;

	pin_recv = ipidev->table[id].pin_recv;
	if (!pin_recv)
		return IPI_UNAVAILABLE;

	if (pin_recv->pin_buf)
		return IPI_DUPLEX;
	pin_recv->mbox_pin_cb = cb;
	pin_recv->pin_buf = msg;
	pin_recv->prdata = prdata;

	return IPI_ACTION_DONE;
}
EXPORT_SYMBOL(vcp_mbox_ipi_register);

int vcp_mbox_ipi_unregister(struct mtk_ipi_device *ipidev, int id)
{
	struct mtk_mbox_pin_recv *pin_recv;

	if (!ipidev || !ipidev->ipi_inited)
		return IPI_DEV_ILLEGAL;

	pin_recv = ipidev->table[id].pin_recv;
	if (!pin_recv)
		return IPI_UNAVAILABLE;

	/* Drop the ipi and reset the record */
	complete(&pin_recv->notify);

	pin_recv->mbox_pin_cb = NULL;
	pin_recv->pin_buf = NULL;
	pin_recv->prdata = NULL;

	return IPI_ACTION_DONE;
}
EXPORT_SYMBOL(vcp_mbox_ipi_unregister);

static void mtk_create_channel(struct mtk_mbox_channel_info *mchan,
			       struct mtk_mbox_device *mbdev, u32 chan_id, char *name)
{
	struct mtk_mbox_pin_send *msend;
	struct mtk_mbox_pin_recv *mrecv;
	u32 pin;

	spin_lock_init(&mchan->channel_lock);
	mchan->info.src = chan_id;
	strscpy(mchan->info.name, name, RPMSG_NAME_SIZE);

	for (pin = 0; pin < mbdev->recv_count; ++pin) {
		mrecv = &mbdev->pin_recv_table[pin];
		if (chan_id == mrecv->chan_id) {
			mchan->mbox = mrecv->mbox;
			mchan->recv_slot = mrecv->offset;
			mchan->recv_slot_size = mrecv->msg_size;
			mchan->recv_pin_index = mrecv->pin_index;
			mchan->recv_pin_offset = pin;
		}
	}

	for (pin = 0; pin < mbdev->send_count; ++pin) {
		msend = &mbdev->pin_send_table[pin];
		if (chan_id == msend->chan_id) {
			mchan->mbox = msend->mbox;
			mchan->send_slot = msend->offset;
			mchan->send_slot_size = msend->msg_size;
			mchan->send_pin_index = msend->pin_index;
			mchan->send_pin_offset = pin;
		}
	}
}

int vcp_ipi_device_register(struct mtk_ipi_device *ipidev, u32 ipi_chan_count,
			    struct platform_device *pdev, struct mtk_mbox_device *mbdev)
{
	struct device *dev;
	struct mtk_ipi_chan_table *ipi_chan_table;
	struct mtk_mbox_channel_info *mtk_rpchan = NULL;
	char chan_name[RPMSG_NAME_SIZE];
	u32 index;
	int ret;

	if (!mbdev || !pdev || !ipidev)
		return -EINVAL;

	ipi_chan_table = kcalloc(ipi_chan_count,
				 sizeof(struct mtk_ipi_chan_table), GFP_KERNEL);
	if (!ipi_chan_table)
		return -ENOMEM;

	mbdev->ipi_priv = (void *)ipidev;
	ipidev->table = ipi_chan_table;
	ipidev->mbdev = mbdev;

	read_poll_timeout_atomic(rpmsg_find_device, dev, dev,
				 USEC_PER_MSEC,
				 RPMSG_POLL_DEVICE_TIMEOUT_MS * USEC_PER_MSEC,
				 false, &pdev->dev, ipidev->chinfo);
	if (!dev) {
		dev_err(&pdev->dev, "%s find devices failed.\n", __func__);
		return -ENOENT;
	}

	for (index = 0; index < ipi_chan_count; index++) {
		ret = snprintf(chan_name, RPMSG_NAME_SIZE, "%s_ipi#%d",
			       ipidev->name, index);
		if (ret < 0 || ret > RPMSG_NAME_SIZE)
			return IPI_RPMSG_ERR;

		/* malloc rpmsg channel info mchan */
		mtk_rpchan = kzalloc(sizeof(*mtk_rpchan), GFP_KERNEL);
		if (!mtk_rpchan) {
			kfree(ipi_chan_table);
			return IPI_RPMSG_ERR;
		}
		mtk_create_channel(mtk_rpchan, mbdev, index, chan_name);
		ipi_chan_table[index].rpchan = mtk_rpchan;
		ipi_chan_table[index].chinfo.src = index;
		ipi_chan_table[index].ept =
					  rpmsg_create_ept(to_rpmsg_device(dev), NULL, mbdev,
							   ipi_chan_table[index].chinfo);
		atomic_set(&ipi_chan_table[index].holder, 0);
	}

	for (index = 0; index < mbdev->send_count; index++) {
		mutex_init(&mbdev->pin_send_table[index].mutex_send);
		ipi_chan_table[mbdev->pin_send_table[index].chan_id].pin_send =
						&mbdev->pin_send_table[index];
	}

	for (index = 0; index < mbdev->recv_count; index++) {
		init_completion(&mbdev->pin_recv_table[index].notify);
		ipi_chan_table[mbdev->pin_recv_table[index].chan_id].pin_recv =
						&mbdev->pin_recv_table[index];
	}

	mbdev->ipi_cb = ipi_isr_cb;
	ipidev->ipi_inited = 1;

	dev_dbg(&pdev->dev, "%s (with %d IPI) has registered.\n",
		ipidev->name, ipi_chan_count);

	return IPI_ACTION_DONE;
}
EXPORT_SYMBOL(vcp_ipi_device_register);

static int send_table_init(struct mtk_mbox_device *mbdev,
			   struct platform_device *pdev)
{
	struct device_node *np, *node;
	u32 i, mbox_id;
	int ret;

	if (!mbdev)
		return -EINVAL;
	np = of_find_node_by_name(pdev->dev.of_node, "mtk-send-table");
	if (!np)
		return -ENODEV;
	mbdev->send_count = of_get_child_count(np);
	mbdev->pin_send_table =
		vzalloc(sizeof(struct mtk_mbox_pin_send) * mbdev->send_count);
	if (!mbdev->pin_send_table) {
		dev_err(&pdev->dev, "pin_send_table alloc failed.\n");
		return -ENOMEM;
	}

	i = 0;
	for_each_child_of_node(np, node) {
		ret = of_property_read_u32(node, "mbox-id", &mbox_id);
		if (ret) {
			dev_err(&pdev->dev, "Cannot get send mbox id:%d\n", i);
			return ret;
		}
		ret = of_property_read_u32(node, "chan-id",
					   &mbdev->pin_send_table[i].chan_id);
		if (ret) {
			dev_err(&pdev->dev, "Cannot get send ipi id:%d\n", i);
			return ret;
		}
		ret = of_property_read_u32(node, "msg-size",
					   &mbdev->pin_send_table[i].msg_size);
		if (ret) {
			dev_err(&pdev->dev, "Cannot get send pin size:%d\n", i);
			return ret;
		}
		/* because mbox and recv_opt is a bit-field */
		mbdev->pin_send_table[i].mbox = mbox_id;
		i++;
	}

	return ret;
}

static int recv_table_init(struct mtk_mbox_device *mbdev,
			   struct platform_device *pdev)
{
	struct device_node *np, *node;
	u32 i, mbox_id, recv_opt;
	int ret;

	if (!mbdev)
		return -EINVAL;
	np = of_find_node_by_name(pdev->dev.of_node, "mtk-recv-table");
	if (!np)
		return -ENODEV;
	mbdev->recv_count = of_get_child_count(np);
	mbdev->pin_recv_table =
		vzalloc(sizeof(struct mtk_mbox_pin_recv) * mbdev->recv_count);
	if (!mbdev->pin_recv_table) {
		dev_err(&pdev->dev, "pin_recv_table allocation is failed.\n");
		return -ENOMEM;
	}

	i = 0;
	for_each_child_of_node(np, node) {
		ret = of_property_read_u32(node, "mbox-id", &mbox_id);
		if (ret) {
			dev_err(&pdev->dev, "Cannot get recv mbox id:%d\n", i);
			return ret;
		}
		ret = of_property_read_u32(node, "chan-id",
					   &mbdev->pin_recv_table[i].chan_id);
		if (ret) {
			dev_err(&pdev->dev, "Cannot get recv ipi id:%d\n", i);
			return ret;
		}
		ret = of_property_read_u32(node, "msg-size",
					   &mbdev->pin_recv_table[i].msg_size);
		if (ret) {
			dev_err(&pdev->dev, "Cannot get recv pin size:%d\n", i);
			return ret;
		}
		ret = of_property_read_u32(node, "recv-opt", &recv_opt);
		if (ret) {
			dev_err(&pdev->dev, "Cannot get recv opt:%d\n", i);
			return ret;
		}

		/* because mbox and recv_opt is a bit-field */
		mbdev->pin_recv_table[i].mbox = mbox_id;
		mbdev->pin_recv_table[i].recv_opt = recv_opt;
		i++;
	}

	return ret;
}

static int mbox_setup_pin_table(struct mtk_mbox_device *mbdev, u32 mbox)
{
	struct mtk_mbox_pin_send *mbox_pin_send;
	struct mtk_mbox_pin_recv *mbox_pin_recv;
	u32 i, last_ofs = 0, last_idx = 0, last_slot = 0, last_sz = 0;

	mbox_pin_send = mbdev->pin_send_table;
	mbox_pin_recv = mbdev->pin_recv_table;

	for (i = 0; i < mbdev->send_count; i++) {
		if (mbox == mbox_pin_send[i].mbox) {
			mbox_pin_send[i].offset = last_ofs + last_slot;
			mbox_pin_send[i].pin_index = last_idx + last_sz;
			last_idx = mbox_pin_send[i].pin_index;
			if (mbdev->info_table[mbox].is64d == 1) {
				last_sz = DIV_ROUND_UP(mbox_pin_send[i].msg_size, 2);
				last_ofs = last_sz * 2;
				last_slot = last_idx * 2;
			} else {
				last_sz = mbox_pin_send[i].msg_size;
				last_ofs = last_sz;
				last_slot = last_idx;
			}
		} else if (mbox < mbox_pin_send[i].mbox) {
			/* no need to search the rest ipi */
			break;
		}
	}

	for (i = 0; i < mbdev->recv_count; i++) {
		if (mbox == mbox_pin_recv[i].mbox) {
			mbox_pin_recv[i].offset = last_ofs + last_slot;
			mbox_pin_recv[i].pin_index = last_idx + last_sz;
			last_idx = mbox_pin_recv[i].pin_index;
			if (mbdev->info_table[mbox].is64d == 1) {
				last_sz = DIV_ROUND_UP(mbox_pin_recv[i].msg_size, 2);
				last_ofs = last_sz * 2;
				last_slot = last_idx * 2;
			} else {
				last_sz = mbox_pin_recv[i].msg_size;
				last_ofs = last_sz;
				last_slot = last_idx;
			}
		} else if (mbox < mbox_pin_recv[i].mbox) {
			/* no need to search the rest ipi */
			break;
		}
	}

	if (last_idx > 32 ||
	    (last_ofs + last_slot) > (mbdev->info_table[mbox].is64d + 1) * 32) {
		dev_err(mbdev->dev, "mbox%d ofs(%d)/slot(%d) exceed the maximum\n",
			mbox, last_idx, last_ofs + last_slot);
		return MBOX_CONFIG_ERR;
	}

	return MBOX_DONE;
}

static int vcp_mbox_init(void __iomem *mbox_base, void __iomem *mbox_init,
			 struct platform_device *pdev, struct mtk_mbox_device *mbdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_mbox_info *minfo;
	char name[32];
	int ret;
	u32 mbox;

	for (mbox = 0; mbox < mbdev->count; mbox++) {
		minfo = &mbdev->info_table[mbox];
		minfo->id = mbox;
		minfo->mbdev = mbdev;
		minfo->slot = MBOX_BASE_SIZE / MBOX_SLOT_SIZE;
		minfo->base = mbox_base + MBOX_BASE_OFFSET * mbox;
		minfo->set_irq_reg = minfo->base + MBOX_IRQ_SET;
		minfo->clr_irq_reg = minfo->base + MBOX_IRQ_CLR;
		minfo->init_base_reg = mbox_init + MBOX_SLOT_SIZE * mbox;
		spin_lock_init(&minfo->mbox_lock);

		ret = snprintf(name, sizeof(name), "mbox%d", mbox);
		if (ret < 0 || ret > sizeof(name))
			return MBOX_CONFIG_ERR;
		minfo->irq_num = platform_get_irq_byname(pdev, name);
		if (minfo->irq_num < 0) {
			dev_err(dev, "mbox%d can't find IRQ\n", mbox);
			return MBOX_CONFIG_ERR;
		}
		ret = request_irq(minfo->irq_num, vcp_mbox_isr,
				  IRQF_NO_SUSPEND | IRQF_TRIGGER_NONE,
				  mbdev->name, (void *)minfo);
		if (ret) {
			dev_err(dev, "mbox%d request irq Failed\n", mbox);
			return MBOX_CONFIG_ERR;
		}

		ret = mbox_setup_pin_table(mbdev, mbox);
		if (ret)
			return ret;
	}

	return MBOX_DONE;
}

int vcp_ipi_table_init(void *mbox_base, void *mbox_init, u32 count,
		       struct mtk_mbox_device *mbdev, struct platform_device *pdev)
{
	u32 i;
	int ret;

	if (!mbdev)
		return -EINVAL;
	mbdev->count = count;
	mbdev->dev = &pdev->dev;
	if (!mbox_base || !mbox_init) {
		dev_err(&pdev->dev, "base:%p, init:%p\n", mbox_base, mbox_init);
		return -EINVAL;
	}

	/* alloc and init mmup_mbox_info */
	mbdev->info_table = vzalloc(sizeof(*mbdev->info_table) * mbdev->count);
	if (!mbdev->info_table)
		return -ENOMEM;
	for (i = 0; i < mbdev->count; ++i) {
		mbdev->info_table[i].id = i;
		mbdev->info_table[i].slot = 64;
		mbdev->info_table[i].is64d = 1;
	}

	/* alloc and init send table */
	ret = send_table_init(mbdev, pdev);
	if (ret)
		goto err_free;

	/* alloc and init recv table */
	ret = recv_table_init(mbdev, pdev);
	if (ret)
		goto err_free;

	/* create mbox dev */
	ret = vcp_mbox_init(mbox_base, mbox_init, pdev, mbdev);
	if (ret)
		goto err_free;

	return ret;

err_free:
	if (mbdev->pin_send_table)
		vfree(mbdev->pin_send_table);
	if (mbdev->pin_recv_table)
		vfree(mbdev->pin_recv_table);
	vfree(mbdev->info_table);
	return ret;
}
EXPORT_SYMBOL_GPL(vcp_ipi_table_init);

MODULE_AUTHOR("Jjian Zhou <jjian.zhou@mediatek.com>");
MODULE_DESCRIPTION("MediaTek VCP IPI Controller");
MODULE_LICENSE("GPL");
