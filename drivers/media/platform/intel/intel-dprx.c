// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2023-2024 Google LLC.
 * Author: Paweł Anikiel <panikiel@google.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-dv-timings.h>
#include "intel-dprx-sink.h"

#define DPRX_RX_CONTROL   0x000
#define DPRX_RX_STATUS    0x001
#define DPRX0_MSA         0x020
#define DPRX1_MSA         0x040
#define DPRX2_MSA         0x060
#define DPRX3_MSA         0x080
#define MSA_HTOTAL  0x002
#define MSA_VTOTAL  0x003
#define MSA_HSP     0x004
#define MSA_HSW     0x005
#define MSA_HSTART  0x006
#define MSA_VSTART  0x007
#define MSA_VSP     0x008
#define MSA_VSW     0x009
#define MSA_HWIDTH  0x00a
#define MSA_VHEIGHT 0x00b
#define MSA_VBID    0x00f
#define DPRX_MST_CONTROL1 0x0a0
#define DPRX_MST_STATUS1  0x0a1
#define DPRX_MST_VCPTAB0  0x0a2
#define DPRX_AUX_CONTROL  0x100
#define DPRX_AUX_STATUS   0x101
#define DPRX_AUX_COMMAND  0x102
#define DPRX_AUX_HPD      0x119

#define DPRX_IRQ_MASK	0x8
#define DPRX_IRQ_CLR	0xc
#define DPRX_IRQ_AUX	0x1

#define DPRX_SYNC_LOSS_TIMER_JIFFIES (HZ / 4) /* 250ms */

struct intel_dprx {
	struct device *dev;
	void __iomem *iobase;
	void __iomem *iobase_irq;

	struct v4l2_subdev subdev;
	struct media_pad pads[4];

	struct dp_sink_device sink_dev;

	struct fwnode_handle *fbs[4];
	struct timer_list sync_loss_timer;
};

static void dprx_write(struct intel_dprx *dprx, int addr, u32 val)
{
	writel(val, dprx->iobase + (addr * 4));
}

static u32 dprx_read(struct intel_dprx *dprx, int addr)
{
	return readl(dprx->iobase + (addr * 4));
}

static void dprx_set_hpd(struct intel_dprx *dprx, int val)
{
	u32 reg;

	reg = dprx_read(dprx, DPRX_AUX_HPD);
	reg &= ~(1 << 11);
	reg |= (val & 1) << 11;
	dprx_write(dprx, DPRX_AUX_HPD, reg);
}

static void dprx_wait_rx_busy(struct intel_dprx *dprx)
{
	while ((dprx_read(dprx, DPRX_RX_STATUS) >> 17) & 0x1)
		;
}

void intel_dprx_pulse_hpd(struct intel_dprx *dprx)
{
	u32 reg;

	reg = dprx_read(dprx, DPRX_AUX_HPD);
	reg |= 1 << 12;
	dprx_write(dprx, DPRX_AUX_HPD, reg);
}

void intel_dprx_set_link_rate(struct intel_dprx *dprx, int val)
{
	u32 reg;

	reg = dprx_read(dprx, DPRX_RX_CONTROL);
	reg &= ~(0xff << 16);
	reg |= (val & 0xff) << 16;
	reg |= 1 << 13;
	dprx_write(dprx, DPRX_RX_CONTROL, reg);

	dprx_wait_rx_busy(dprx);
}

void intel_dprx_set_lane_count(struct intel_dprx *dprx, int val)
{
	u32 reg;

	reg = dprx_read(dprx, DPRX_RX_CONTROL);
	reg &= ~0x1f;
	reg |= (val & 0x1f);
	dprx_write(dprx, DPRX_RX_CONTROL, reg);

	dprx_wait_rx_busy(dprx);
}

void intel_dprx_set_training_pattern(struct intel_dprx *dprx, int val)
{
	u32 reg;

	reg = dprx_read(dprx, DPRX_RX_CONTROL);
	reg &= ~(0x7 << 8);
	reg |= (val & 0x7) << 8;
	dprx_write(dprx, DPRX_RX_CONTROL, reg);

	dprx_wait_rx_busy(dprx);
}

void intel_dprx_set_scrambler(struct intel_dprx *dprx, int val)
{
	u32 reg;

	reg = dprx_read(dprx, DPRX_RX_CONTROL);
	reg &= ~(1 << 7);
	reg |= (~val & 1) << 7;
	dprx_write(dprx, DPRX_RX_CONTROL, reg);

	dprx_wait_rx_busy(dprx);
}

int intel_dprx_get_cr_lock(struct intel_dprx *dprx)
{
	return dprx_read(dprx, DPRX_RX_STATUS) & 0xf;
}

int intel_dprx_get_sym_lock(struct intel_dprx *dprx)
{
	return (dprx_read(dprx, DPRX_RX_STATUS) >> 4) & 0xf;
}

int intel_dprx_get_interlane_align(struct intel_dprx *dprx)
{
	return (dprx_read(dprx, DPRX_RX_STATUS) >> 8) & 0x1;
}

static int intel_dprx_get_sync_loss(struct intel_dprx *dprx)
{
	return (dprx_read(dprx, DPRX_RX_STATUS) >> 16) & 0x1;
}

static void intel_dprx_reset_sync_loss(struct intel_dprx *dprx)
{
	u32 reg;

	reg = dprx_read(dprx, DPRX_RX_STATUS);
	reg |= 0x1 << 16;
	dprx_write(dprx, DPRX_RX_STATUS, reg);
}

int intel_dprx_get_sink_status(struct intel_dprx *dprx)
{
	return (dprx_read(dprx, DPRX0_MSA + MSA_VBID) >> 7) & 0x1;
}

void intel_dprx_set_mst(struct intel_dprx *dprx, int val)
{
	u32 reg;

	reg = dprx_read(dprx, DPRX_MST_CONTROL1);
	reg &= ~0x1;
	reg |= (val & 0x1);
	dprx_write(dprx, DPRX_MST_CONTROL1, reg);
}

void intel_dprx_clear_vc_payload_table(struct intel_dprx *dprx)
{
	u32 reg;
	int i;

	for (i = 0; i < 8; i++)
		dprx_write(dprx, DPRX_MST_VCPTAB0 + i, 0);

	reg = dprx_read(dprx, DPRX_MST_CONTROL1);
	reg &= ~(0xffff << 4);
	reg |= 1 << 31;
	dprx_write(dprx, DPRX_MST_CONTROL1, reg);
}

/*
 * The IP core only accepts VC payload IDs of 1-4. Thus, we need to remap
 * the 1-63 range allowed by DisplayPort into 1-4. However, some hosts
 * first set the VC payload table and then allocate the VC payload IDs,
 * which means we can't remap the range immediately.
 *
 * It is probably possible to force a VC payload table update (without
 * waiting for a ACT trigger) when the IDs change, but for now we just
 * ignore IDs higher than 4.
 */

void intel_dprx_set_vc_payload_table(struct intel_dprx *dprx, u8 *table)
{
	int i, j;
	u32 reg;
	u8 val;

	for (i = 0; i < 8; i++) {
		reg = 0;
		for (j = 0; j < 8; j++) {
			val = table[i*8+j];
			if (val <= 4)
				reg |= val << (j * 4);
		}
		dprx_write(dprx, DPRX_MST_VCPTAB0 + i, reg);
	}

	reg = dprx_read(dprx, DPRX_MST_CONTROL1);
	reg |= 1 << 30;
	dprx_write(dprx, DPRX_MST_CONTROL1, reg);
}

void intel_dprx_set_vc_ids(struct intel_dprx *dprx, u8 *ids)
{
	u32 reg;
	int i;

	reg = dprx_read(dprx, DPRX_MST_CONTROL1);
	reg &= ~(0xffff << 4);
	for (i = 0; i < 4; i++) {
		if (ids[i] <= 4)
			reg |= ids[i] << ((i + 1) * 4);
	}
	dprx_write(dprx, DPRX_MST_CONTROL1, reg);
}

int intel_dprx_get_act(struct intel_dprx *dprx)
{
	return (dprx_read(dprx, DPRX_MST_STATUS1) >> 30) & 1;
}

void intel_dprx_clear_act(struct intel_dprx *dprx)
{
	u32 reg;

	reg = dprx_read(dprx, DPRX_MST_CONTROL1);
	reg &= ~(1 << 30);
	dprx_write(dprx, DPRX_MST_CONTROL1, reg);
}

#define to_intel_dprx(sd) container_of(sd, struct intel_dprx, subdev)

static int intel_dprx_get_edid(struct v4l2_subdev *sd, struct v4l2_edid *edid)
{
	struct intel_dprx *dprx = to_intel_dprx(sd);
	struct dp_sink *sink;
	u32 end_block = edid->start_block + edid->blocks;

	memset(edid->reserved, 0, sizeof(edid->reserved));

	if (edid->pad >= dprx->sink_dev.sink_count)
		return -EINVAL;
	sink = &dprx->sink_dev.sinks[edid->pad];

	if (edid->start_block == 0 && edid->blocks == 0) {
		edid->blocks = sink->blocks;
		return 0;
	}
	if (sink->blocks == 0)
		return -ENODATA;
	if (edid->start_block >= sink->blocks)
		return -EINVAL;
	if (end_block > sink->blocks) {
		end_block = sink->blocks;
		edid->blocks = end_block - edid->start_block;
	}

	memcpy(edid->edid, sink->edid + edid->start_block * 128, edid->blocks * 128);

	return 0;
}

static int intel_dprx_set_edid(struct v4l2_subdev *sd, struct v4l2_edid *edid)
{
	struct intel_dprx *dprx = to_intel_dprx(sd);
	struct dp_sink *sink;

	memset(edid->reserved, 0, sizeof(edid->reserved));

	if (edid->pad >= dprx->sink_dev.sink_count)
		return -EINVAL;
	sink = &dprx->sink_dev.sinks[edid->pad];

	if (edid->start_block != 0)
		return -EINVAL;
	if (edid->blocks > DP_SINK_MAX_EDID_BLOCKS) {
		edid->blocks = DP_SINK_MAX_EDID_BLOCKS;
		return -E2BIG;
	}

	sink->blocks = edid->blocks;
	memcpy(sink->edid, edid->edid, edid->blocks * 128);

	/*
	 * This is an MST DisplayPort device, which means that one HPD
	 * line controls all the video streams. The way this is handled
	 * in s_edid is that the HPD line is controlled by the presence
	 * of only the first stream's EDID. This allows, for example, to
	 * first set the second streams's EDID and then the first one in
	 * order to reduce the amount of AUX communication.
	 */

	if (dprx->sink_dev.sinks[0].blocks == 0) {
		dprx_set_hpd(dprx, 0);
	} else {
		/*
		 * DisplayPort specifies that a HPD pulse is anything
		 * longer than 2ms
		 */
		dprx_set_hpd(dprx, 0);
		usleep_range(2500, 5000);
		dp_sink_device_reset(&dprx->sink_dev);
		dprx_set_hpd(dprx, 1);
	}

	return 0;
}

struct dprx_msa {
	u32 htotal, vtotal;
	u32 hsp, hsw;
	u32 hstart, vstart;
	u32 vsp, vsw;
	u32 hwidth, vheight;
};

static int dprx_read_msa(struct intel_dprx *dprx, int index, struct dprx_msa *msa)
{
	u32 msa_base;

	switch (index) {
	case 0:
		msa_base = DPRX0_MSA;
		break;
	case 1:
		msa_base = DPRX1_MSA;
		break;
	case 2:
		msa_base = DPRX2_MSA;
		break;
	case 3:
		msa_base = DPRX3_MSA;
		break;
	default:
		return -EINVAL;
	}

	if (!(dprx_read(dprx, msa_base + MSA_VBID) & 0x80))
		return -ENOLINK;

	msa->htotal  = dprx_read(dprx, msa_base + MSA_HTOTAL);
	msa->vtotal  = dprx_read(dprx, msa_base + MSA_VTOTAL);
	msa->hsp     = dprx_read(dprx, msa_base + MSA_HSP);
	msa->hsw     = dprx_read(dprx, msa_base + MSA_HSW);
	msa->hstart  = dprx_read(dprx, msa_base + MSA_HSTART);
	msa->vstart  = dprx_read(dprx, msa_base + MSA_VSTART);
	msa->vsp     = dprx_read(dprx, msa_base + MSA_VSP);
	msa->vsw     = dprx_read(dprx, msa_base + MSA_VSW);
	msa->hwidth  = dprx_read(dprx, msa_base + MSA_HWIDTH);
	msa->vheight = dprx_read(dprx, msa_base + MSA_VHEIGHT);

	return 0;
}

static int intel_dprx_query_dv_timings(struct v4l2_subdev *sd, unsigned int pad,
				       struct v4l2_dv_timings *timings)
{
	struct intel_dprx *dprx = to_intel_dprx(sd);
	struct dprx_msa msa;
	int res;

	res = dprx_read_msa(dprx, pad, &msa);
	if (res)
		return res;

	memset(timings, 0, sizeof(*timings));
	timings->type = V4L2_DV_BT_656_1120;
	timings->bt.width = msa.hwidth;
	timings->bt.height = msa.vheight;
	timings->bt.polarities = (!msa.vsp) | (!msa.hsp) << 1;
	timings->bt.hfrontporch = msa.htotal - msa.hstart - msa.hwidth;
	timings->bt.hsync = msa.hsw;
	timings->bt.hbackporch = msa.hstart - msa.hsw;
	timings->bt.vfrontporch = msa.vtotal - msa.vstart - msa.vheight;
	timings->bt.vsync = msa.vsw;
	timings->bt.vbackporch = msa.vstart - msa.vsw;

	return 0;
}

/* DisplayPort 1.4 capabilities */

static const struct v4l2_dv_timings_cap dprx_dv_timings_cap = {
	.type = V4L2_DV_BT_656_1120,
	.bt = {
		.min_width = 0,
		.max_width = 7680,
		.min_height = 0,
		.max_height = 4320,
		.min_pixelclock = 0,
		.max_pixelclock = 1350000000, /* 8.1Gbps * 4lanes / 24bpp */
		.standards = V4L2_DV_BT_STD_CEA861 | V4L2_DV_BT_STD_DMT |
			V4L2_DV_BT_STD_CVT | V4L2_DV_BT_STD_GTF,
		.capabilities = V4L2_DV_BT_CAP_PROGRESSIVE |
			V4L2_DV_BT_CAP_REDUCED_BLANKING |
			V4L2_DV_BT_CAP_CUSTOM,
	},
};

static int intel_dprx_enum_dv_timings(struct v4l2_subdev *sd,
				      struct v4l2_enum_dv_timings *timings)
{
	return v4l2_enum_dv_timings_cap(timings, &dprx_dv_timings_cap,
					NULL, NULL);
}

static int intel_dprx_dv_timings_cap(struct v4l2_subdev *sd,
				     struct v4l2_dv_timings_cap *cap)
{
	*cap = dprx_dv_timings_cap;

	return 0;
}

static const struct v4l2_subdev_pad_ops intel_dprx_pad_ops = {
	.get_edid = intel_dprx_get_edid,
	.set_edid = intel_dprx_set_edid,
	.dv_timings_cap = intel_dprx_dv_timings_cap,
	.enum_dv_timings = intel_dprx_enum_dv_timings,
	.query_dv_timings = intel_dprx_query_dv_timings,
};

static const struct v4l2_subdev_ops intel_dprx_subdev_ops = {
	.pad    = &intel_dprx_pad_ops,
};

static int dprx_read_aux(struct intel_dprx *dprx, struct dp_aux_buf *buf)
{
	int i;

	/* check MSG_READY */
	if (!(dprx_read(dprx, DPRX_AUX_STATUS) & (1 << 31)))
		return -1;

	/* read LENGTH */
	buf->len = dprx_read(dprx, DPRX_AUX_CONTROL) & 0x1f;
	if (buf->len > 20)
		buf->len = 20;

	/* read request */
	for (i = 0; i < buf->len; i++)
		buf->data[i] = dprx_read(dprx, DPRX_AUX_COMMAND + i);

	return 0;
}

static void dprx_write_aux(struct intel_dprx *dprx, struct dp_aux_buf *buf)
{
	u32 reg;
	int i;

	/* check READY_TO_TX */
	reg = dprx_read(dprx, DPRX_AUX_STATUS);
	if (!(reg & (1 << 30)))
		return;

	/* write request */
	if (buf->len > 17)
		buf->len = 17;
	for (i = 0; i < buf->len; i++)
		dprx_write(dprx, DPRX_AUX_COMMAND + i, buf->data[i]);

	/* write LENGTH and TX_STROBE */
	reg = dprx_read(dprx, DPRX_AUX_CONTROL);
	reg &= ~0x1f;
	reg |= buf->len | (1 << 7);
	dprx_write(dprx, DPRX_AUX_CONTROL, reg);
}

static irqreturn_t dprx_isr(int irq, void *data)
{
	struct intel_dprx *dprx = data;
	struct dp_aux_buf request;
	struct dp_aux_buf reply;
	unsigned int reg;

	reg = readl(dprx->iobase_irq + DPRX_IRQ_CLR);
	if (!reg)
		return IRQ_NONE;

	if (!dprx_read_aux(dprx, &request)) {
		dp_sink_device_handle_request(&dprx->sink_dev, &request, &reply);
		dprx_write_aux(dprx, &reply);
	}

	writel(reg, dprx->iobase_irq + DPRX_IRQ_CLR);

	return IRQ_HANDLED;
}

void chv3_fb_fwnode_runtime_reset(struct fwnode_handle *node);

static void dprx_sync_loss_handler(struct timer_list *timer)
{
	struct intel_dprx *dprx = container_of(timer, struct intel_dprx,
					       sync_loss_timer);
	int i;

	if (intel_dprx_get_sync_loss(dprx)) {
		intel_dprx_reset_sync_loss(dprx);
		for (i = 0; i < 4; i++) {
			if (dprx->fbs[i] != NULL)
				chv3_fb_fwnode_runtime_reset(dprx->fbs[i]);
		}
	}

	mod_timer(&dprx->sync_loss_timer,
		  jiffies + DPRX_SYNC_LOSS_TIMER_JIFFIES);
}

static void dprx_init(struct intel_dprx *dprx)
{
	u32 reg;

	/* Enable AUX_IRQ_EN */
	reg = dprx_read(dprx, DPRX_AUX_CONTROL);
	reg |= 1 << 8;
	dprx_write(dprx, DPRX_AUX_CONTROL, reg);

	/* Set CHANNEL_CODING_SET to 8b/10b */
	reg = dprx_read(dprx, DPRX_RX_CONTROL);
	reg |= 1 << 5;
	dprx_write(dprx, DPRX_RX_CONTROL, reg);
}

static const struct media_entity_operations intel_dprx_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
	.get_fwnode_pad = v4l2_subdev_get_fwnode_pad_1_to_1,
};

static int dprx_init_pads(struct intel_dprx *dprx, int count)
{
	int i;

	for (i = 0; i < count; i++)
		dprx->pads[i].flags = MEDIA_PAD_FL_SOURCE;

	return media_entity_pads_init(&dprx->subdev.entity, count, dprx->pads);
}

static void dprx_init_fbs(struct intel_dprx *dprx)
{
	struct fwnode_handle *fb;
	int i = 0;

	fwnode_graph_for_each_endpoint(dev_fwnode(dprx->dev), fb) {
		if (i < 4) {
			dprx->fbs[i] = fwnode_graph_get_remote_endpoint(fb);
			i++;
		}
	}

	for (; i < 4; i++)
		dprx->fbs[i] = NULL;
}

int intel_dprx_probe(struct platform_device *pdev)
{
	struct intel_dprx *dprx;
	int has_mst;
	int irq;
	int res;

	dprx = devm_kzalloc(&pdev->dev, sizeof(*dprx), GFP_KERNEL);
	if (!dprx)
		return -ENOMEM;
	dprx->dev = &pdev->dev;

	dprx->iobase = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dprx->iobase))
		return PTR_ERR(dprx->iobase);

	dprx->iobase_irq = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(dprx->iobase_irq))
		return PTR_ERR(dprx->iobase_irq);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	res = devm_request_irq(dprx->dev, irq, dprx_isr, 0, "intel-dprx", dprx);
	if (res)
		return res;

	has_mst = of_property_read_bool(pdev->dev.of_node, "intel,has-mst");

	dprx->subdev.owner = THIS_MODULE;
	dprx->subdev.dev = &pdev->dev;
	v4l2_subdev_init(&dprx->subdev, &intel_dprx_subdev_ops);
	v4l2_set_subdevdata(&dprx->subdev, &pdev->dev);
	snprintf(dprx->subdev.name, sizeof(dprx->subdev.name), "%s %s",
	         KBUILD_MODNAME, dev_name(&pdev->dev));
	dprx->subdev.flags = V4L2_SUBDEV_FL_HAS_DEVNODE;

	dprx->subdev.entity.function = MEDIA_ENT_F_PROC_VIDEO_PIXEL_FORMATTER;
	dprx->subdev.entity.ops = &intel_dprx_entity_ops;

	res = dprx_init_pads(dprx, has_mst ? 4 : 1);
	if (res)
		return res;

	dprx_init_fbs(dprx);

	res = v4l2_async_register_subdev(&dprx->subdev);
	if (res)
		return res;

	dprx_init(dprx);

	dp_sink_device_init(&dprx->sink_dev, dprx, has_mst ? 4 : 0);

	timer_setup(&dprx->sync_loss_timer, dprx_sync_loss_handler, 0);
	mod_timer(&dprx->sync_loss_timer,
		  jiffies + DPRX_SYNC_LOSS_TIMER_JIFFIES);

	writel(DPRX_IRQ_AUX, dprx->iobase_irq + DPRX_IRQ_MASK);

	return 0;
}

static void intel_dprx_remove(struct platform_device *pdev)
{
	struct intel_dprx *dprx = platform_get_drvdata(pdev);
	int i;

	del_timer_sync(&dprx->sync_loss_timer);

	for (i = 0; i < 4; i++) {
		if (dprx->fbs[i] != NULL)
			fwnode_handle_put(dprx->fbs[i]);
	}
}

static const struct of_device_id intel_dprx_match_table[] = {
	{ .compatible = "intel,dprx" },
	{ },
};

static struct platform_driver intel_dprx_platform_driver = {
	.probe = intel_dprx_probe,
	.remove_new = intel_dprx_remove,
	.driver = {
		.name = "intel-dprx",
		.of_match_table = intel_dprx_match_table,
	},
};

module_platform_driver(intel_dprx_platform_driver);

MODULE_AUTHOR("Paweł Anikiel <panikiel@google.com>");
MODULE_DESCRIPTION("Intel DisplayPort RX IP core driver");
MODULE_LICENSE("GPL");
