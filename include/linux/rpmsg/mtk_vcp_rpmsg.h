/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/*
 * Copyright (c) 2024 MediaTek Inc.
 */

#ifndef __MTK_VCP_IPI_H__
#define __MTK_VCP_IPI_H__

#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/rpmsg.h>
#include <linux/spinlock.h>

#define RPMSG_POLL_DEVICE_TIMEOUT_MS	1000

/* mbox slot size definition: 1 slot for 4 bytes */
#define MBOX_BASE_OFFSET	0x10000
#define MBOX_BASE_SIZE		0x100
#define MBOX_SLOT_SIZE		0x4
#define MBOX_IRQ_SET		0x100
#define MBOX_IRQ_CLR		0x10C

/* IPI result definition */
#define IPI_ACTION_DONE	  0
#define IPI_DEV_ILLEGAL	 -1 /* ipi device is not initial */
#define IPI_DUPLEX		 -2 /* the ipi has be registered */
#define IPI_UNAVAILABLE	 -3 /* can't find this ipi pin define */
#define IPI_NO_MSGBUF		 -4 /* receiver doesn't has message buffer */
#define IPI_NO_MEMORY		 -5 /* message length is large than defined */
#define IPI_PIN_BUSY		 -6 /* send message timeout */
#define IPI_COMPL_TIMEOUT	 -7 /* polling or wait for ack ipi timeout */
#define IPI_PRE_CB_FAIL	 -8 /* pre-callback fail */
#define IPI_POST_CB_FAIL	 -9 /* post-callback fail */
#define IPI_RPMSG_ERR		-99 /* some error from rpmsg layer */

/* mbox table item number definition */
#define send_item_num	3
#define recv_item_num	4

struct mtk_mbox_pin_recv;

typedef int (*ipi_tx_cb_t)(void *);
typedef int (*mbox_rx_cb_t)(void *);
typedef int (*mbox_pin_cb_t)(u32 ipi_id, void *prdata, void *data, u32 len);
typedef void (*mbox_ipi_cb_t)(struct mtk_mbox_pin_recv *, void *);

/* mbox recv action definition */
enum MBOX_RECV_OPT {
	MBOX_RECV_MESSAGE  = 0,
	MBOX_RECV_ACK      = 1,
};

/* mbox send action definition */
enum MBOX_RETURN {
	MBOX_POST_CB_ERR  = -8,
	MBOX_PRE_CB_ERR   = -7,
	MBOX_READ_SZ_ERR  = -6,
	MBOX_WRITE_SZ_ERR = -5,
	MBOX_PARA_ERR     = -4,
	MBOX_CONFIG_ERR   = -3,
	MBOX_IRQ_ERR      = -2,
	MBOX_PLT_ERR      = -1,
	MBOX_DONE         =  0,
	MBOX_PIN_BUSY     =  1,
};

/**
 * mbox pin structure, this is for send definition,
 * ipi=endpoint=pin
 * @mbox: mbox number id of the pin, up to 16
 * @offset: message offset in the slots of a mbox
 * @send_opt: send option, 0:send ,1: send for response
 * @lock: polling lock 0:unuse,1:used
 * @msg_size: message used slots in the mbox, 4 bytes alignment
 * @pin_index: bit offset in the mbox
 * @chan_id: ipc channel id
 */
struct mtk_mbox_pin_send {
	u32 mbox     :  4,
	    offset   : 20,
	    send_opt :  2,
	    lock     :  2;
	u32 msg_size;
	u32 pin_index;
	u32 chan_id;
	/* define a mutex for remote response */
	struct mutex mutex_send;
};

/**
 * mbox pin structure, this is for receive definition,
 * ipi=endpoint=pin
 * @mbox: mbox number of the pin, up to 16
 * @offset: message offset in the slots of a mbox
 * @recv_opt: recv option,  0:receive ,1: response
 * @lock: polling lock 0:unuse,1:used
 * @msg_size: message used slots in the mbox, 4 bytes alignment
 * @pin_index: bit offset in the mbox
 * @chan_id: ipc channel id
 * @notify: completion notify process
 * @mbox_pin_cb: callback function
 * @pin_buf: buffer point
 * @prdata: private data
 */
struct mtk_mbox_pin_recv {
	u32 mbox     :  4,
	    offset   : 20,
	    recv_opt :  2,
	    lock     :  2;
	u32 msg_size;
	u32 pin_index;
	u32 chan_id;
	struct completion notify;
	mbox_pin_cb_t mbox_pin_cb;
	void *pin_buf;
	void *prdata;
};

/**
 * Mbox is a dedicate hardware of a tinysys consists of:
 * 1) a share memory tightly coupled to the tinysys
 * 2) several IRQs
 *
 * Following are platform specific interface
 * @dev: vcp device
 * @name: identity of the device
 * @ipi_cb: the callback handler for synchronization layer
 * @ipi_priv: private data for synchronization layer
 * @pre_cb: the callback handler in the begin of mbox receiving ipi
 * @post_cb: the callback handler in the end of mbox receiving ipi
 * @prdata: private data for the callback use
 */
struct mtk_mbox_device {
	struct device *dev;
	const char *name;
	struct mtk_mbox_pin_recv *pin_recv_table;
	struct mtk_mbox_pin_send *pin_send_table;
	struct mtk_mbox_info *info_table;
	u32 count;
	u32 recv_count;
	u32 send_count;
	mbox_ipi_cb_t ipi_cb;
	void *ipi_priv;
	mbox_rx_cb_t pre_cb;
	mbox_rx_cb_t post_cb;
	void *prdata;
};

struct mtk_mbox_channel_info {
	struct rpmsg_channel_info info;
	unsigned int send_slot;
	unsigned int recv_slot;
	unsigned int send_slot_size;
	unsigned int recv_slot_size;
	unsigned int send_pin_index;
	unsigned int recv_pin_index;
	unsigned int send_pin_offset;
	unsigned int recv_pin_offset;
	unsigned int mbox;
	spinlock_t channel_lock;
};

/**
 * struct mtk_ipi_chan_table - channel table that belong to mtk_ipi_device
 * @ept: the rpmsg endpoint of this channel
 * @chinfo: the rpmsg channel info
 * @rpchan: info used to create the endpoint
 * @pin_send: the mbox send pin table address of this channel
 * @pin_recv: the mbox receive pin table address of this channel
 * @holder: keep 1 if there are ipi waiters (to wait the reply)
 * @ipi_record: timestamp of each ipi transmission stage
 *
 * All of these data should be initialized by mtk_ipi_device_register()
 */
struct mtk_ipi_chan_table {
	struct rpmsg_endpoint *ept;
	struct rpmsg_channel_info chinfo;
	struct mtk_mbox_channel_info *rpchan;
	struct mtk_mbox_pin_send *pin_send;
	struct mtk_mbox_pin_recv *pin_recv;
	atomic_t holder;
};

/**
 * struct mtk_ipi_device - device for represent the tinysys using mtk ipi
 * @name: name of tinysys device
 * @id: device id (used to match between rpmsg drivers and devices)
 * @mbdev: mtk mbox device
 * @table: channel table with endpoint & channel_info & mbox_pin info
 * @chinfo: rpmsg channel information for tinysys device
 * @pre_cb: the callback handler before ipi send data
 * @post_cb: the callback handler after ipi send data
 * @prdata: private data for the callback use
 * @ipi_inited: set when vcp_ipi_device_register() done
 *
 * Tinysys platform has necessary to define the vcalue of 'name', 'id', 'mbdev';
 * and optional to define the 'pre_cb', 'post_cb', 'prdata'.
 * Othes would be initialized by vcp_ipi_device_register().
 */
struct mtk_ipi_device  {
	const char *name;
	int id;
	struct mtk_mbox_device *mbdev;
	struct mtk_ipi_chan_table *table;
	struct rpmsg_channel_info *chinfo;
	ipi_tx_cb_t pre_cb;
	ipi_tx_cb_t post_cb;
	void *prdata;
	int ipi_inited;
};

/**
 * mbox information
 *
 * @mbdev: mbox device
 * @irq_num: identity of mbox irq
 * @id: mbox id
 * @slot: how many slots that mbox used
 * @opt: option for tx mode, 0:mbox, 1:share memory 2:queue
 * @is64d: mbox is64d status, 0:32d, 1: 64d
 * @base: mbox base address
 * @set_irq_reg: mbox set irq register
 * @clr_irq_reg: mbox clear irq register
 * @init_base_reg: mbox initialize register
 */
struct mtk_mbox_info {
	struct mtk_mbox_device *mbdev;
	int irq_num;
	u32 id;
	u32 slot;
	u32 opt;
	bool is64d;
	void __iomem *base;
	void __iomem *set_irq_reg;
	void __iomem *clr_irq_reg;
	void __iomem *init_base_reg;
	/* lock of mbox */
	spinlock_t mbox_lock;
};

int vcp_ipi_device_register(struct mtk_ipi_device *ipidev, u32 ipi_chan_count,
			    struct platform_device *pdev, struct mtk_mbox_device *mbox);
int vcp_mbox_send(struct mtk_ipi_device *ipidev, u32 id, void *buf,
		  u32 len, u32 wait);
int vcp_ipi_send(struct mtk_ipi_device *ipidev, u32 ipi_id,
		 void *data, u32 len, u32 timeout_ms);
int vcp_ipi_send_compl(struct mtk_ipi_device *ipidev, u32 ipi_id,
		       void *data, u32 len, u32 timeout_ms);
int vcp_mbox_ipi_register(struct mtk_ipi_device *ipidev, int ipi_id,
			  mbox_pin_cb_t cb, void *prdata, void *msg);
int vcp_mbox_ipi_unregister(struct mtk_ipi_device *ipidev, int ipi_id);
int vcp_ipi_table_init(void __iomem *mbox_base, void __iomem *mbox_init, u32 count,
		       struct mtk_mbox_device *mbdev, struct platform_device *pdev);

#endif
