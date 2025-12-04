// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#include <drm/drm_blend.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/soc/mediatek/mtk-cmdq.h>

#include "mtk_disp_drv.h"
#include "mtk_disp_pmqos.h"
#include "mtk_drm_drv.h"

#define DISP_REG_OVL_EXDMA_EN_CON		0xc
#define OVL_EXDMA_OP_8BIT_MODE				BIT(4)
#define OVL_EXDMA_HG_FOVL_CK_ON				BIT(8)
#define OVL_EXDMA_HF_FOVL_CK_ON				BIT(10)
#define DISP_REG_OVL_EXDMA_DATAPATH_CON		0x014
#define OVL_EXDMA_DATAPATH_CON_LAYER_SMI_ID_EN		BIT(0)
#define OVL_EXDMA_DATAPATH_CON_GCLAST_EN		BIT(24)
#define OVL_EXDMA_DATAPATH_CON_HDR_GCLAST_EN		BIT(25)
#define DISP_REG_OVL_EXDMA_EN			0x020
#define OVL_EXDMA_EN					BIT(0)
#define DISP_REG_OVL_EXDMA_RST			0x024
#define OVL_EXDMA_RST					BIT(0)
#define DISP_REG_OVL_EXDMA_ROI_SIZE		0x030
#define DISP_REG_OVL_EXDMA_L0_EN		0x040
#define OVL_EXDMA_L0_EN					BIT(0)
#define DISP_REG_OVL_EXDMA_L0_OFFSET		0x044
#define DISP_REG_OVL_EXDMA_SRC_SIZE		0x048
#define DISP_REG_OVL_EXDMA_L0_CLRFMT		0x050
#define OVL_EXDMA_CON_FLD_CLRFMT			GENMASK(3, 0)
#define OVL_EXDMA_CON_CLRFMT_MAN			BIT(4)
#define OVL_EXDMA_CON_FLD_CLRFMT_NB			GENMASK(9, 8)
#define OVL_EXDMA_CON_CLRFMT_NB_10_BIT			BIT(8)
#define OVL_EXDMA_CON_BYTE_SWAP				BIT(16)
#define OVL_EXDMA_CON_RGB_SWAP				BIT(17)
#define OVL_EXDMA_CON_CLRFMT_RGB565			0x000
#define OVL_EXDMA_CON_CLRFMT_RGB888			0x001
#define OVL_EXDMA_CON_CLRFMT_BGRA8888			0x002
#define OVL_EXDMA_CON_CLRFMT_ABGR8888			0x003
#define OVL_EXDMA_CON_CLRFMT_UYVY			0x004
#define OVL_EXDMA_CON_CLRFMT_YUYV			0x005
#define OVL_EXDMA_CON_CLRFMT_BGR565			(0x000 | OVL_EXDMA_CON_BYTE_SWAP)
#define OVL_EXDMA_CON_CLRFMT_BGR888			(0x001 | OVL_EXDMA_CON_BYTE_SWAP)
#define OVL_EXDMA_CON_CLRFMT_RGBA8888			(0x002 | OVL_EXDMA_CON_BYTE_SWAP)
#define OVL_EXDMA_CON_CLRFMT_ARGB8888			(0x003 | OVL_EXDMA_CON_BYTE_SWAP)
#define OVL_EXDMA_CON_CLRFMT_VYUY			(0x004 | OVL_EXDMA_CON_BYTE_SWAP)
#define OVL_EXDMA_CON_CLRFMT_YVYU			(0x005 | OVL_EXDMA_CON_BYTE_SWAP)
#define OVL_EXDMA_CON_CLRFMT_PBGRA8888			(0x003 | OVL_EXDMA_CON_CLRFMT_MAN)
#define OVL_EXDMA_CON_CLRFMT_PARGB8888			(OVL_EXDMA_CON_CLRFMT_PBGRA8888 | \
							 OVL_EXDMA_CON_BYTE_SWAP)
#define OVL_EXDMA_CON_CLRFMT_PRGBA8888			(OVL_EXDMA_CON_CLRFMT_PBGRA8888 | \
							 OVL_EXDMA_CON_RGB_SWAP)
#define OVL_EXDMA_CON_CLRFMT_PABGR8888			(OVL_EXDMA_CON_CLRFMT_PBGRA8888 | \
							 OVL_EXDMA_CON_RGB_SWAP | \
							 OVL_EXDMA_CON_BYTE_SWAP)
#define DISP_REG_OVL_EXDMA_RDMA0_CTRL		0x100
#define OVL_EXDMA_RDMA0_EN				BIT(0)
#define DISP_REG_OVL_EXDMA_RDMA_BURST_CON1	0x1f4
#define OVL_EXDMA_RDMA_BURST_CON1_BURST16_EN		BIT(28)
#define OVL_EXDMA_RDMA_BURST_CON1_DDR_EN		BIT(30)
#define OVL_EXDMA_RDMA_BURST_CON1_DDR_ACK_EN		BIT(31)
#define DISP_REG_OVL_EXDMA_DUMMY_REG		0x200
#define OVL_EXDMA_EXT_DDR_EN_OPT			BIT(2)
#define OVL_EXDMA_FORCE_EXT_DDR_EN			BIT(3)
#define DISP_REG_OVL_EXDMA_GDRDY_PRD		0x208
#define DISP_REG_OVL_EXDMA_PITCH_MSB		0x2f0
#define OVL_EXDMA_L0_SRC_PITCH_MSB_MASK			GENMASK(3, 0)
#define DISP_REG_OVL_EXDMA_PITCH		0x2f4
#define OVL_EXDMA_L0_SRC_PITCH				GENMASK(15, 0)
#define OVL_EXDMA_L0_CONST_BLD				BIT(28)
#define OVL_EXDMA_L0_SRC_PITCH_MASK			GENMASK(15, 0)
#define DISP_REG_OVL_EXDMA_L0_GUSER_EXT		0x2fc
#define OVL_EXDMA_RDMA0_L0_VCSEL			BIT(5)
#define DISP_REG_OVL_EXDMA_CON			0x300
#define OVL_EXDMA_CON_FLD_INT_MTX_SEL			GENMASK(19, 16)
#define OVL_EXDMA_CON_INT_MTX_BT601_TO_RGB		(6 << 16)
#define OVL_EXDMA_CON_INT_MTX_BT709_TO_RGB		(7 << 16)
#define OVL_EXDMA_CON_INT_MTX_EN			BIT(27)
#define DISP_REG_OVL_EXDMA_ADDR			0xf40
#define DISP_REG_OVL_EXDMA_MOUT			0xff0
#define OVL_EXDMA_MOUT_OUT_DATA				BIT(0)
#define OVL_EXDMA_MOUT_BGCLR_OUT			BIT(1)

#define OVL_EXDMA_MAX_SIZE			(8191)

static const u32 formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_BGRX8888,
	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_RGBX8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_ARGB2101010,
	DRM_FORMAT_RGBX1010102,
	DRM_FORMAT_RGBA1010102,
	DRM_FORMAT_XBGR2101010,
	DRM_FORMAT_ABGR2101010,
	DRM_FORMAT_BGRX1010102,
	DRM_FORMAT_BGRA1010102,
};

struct mtk_disp_exdma {
	void __iomem		*regs;
	struct clk		*clk;
	struct cmdq_client_reg	cmdq_reg;
	struct device		*larb;
	struct icc_path		*qos_req;
	struct icc_path		*hrt_qos_req;
};

static inline bool is_10bit_rgb(u32 fmt)
{
	switch (fmt) {
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_ARGB2101010:
	case DRM_FORMAT_RGBX1010102:
	case DRM_FORMAT_RGBA1010102:
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_ABGR2101010:
	case DRM_FORMAT_BGRX1010102:
	case DRM_FORMAT_BGRA1010102:
		return true;
	}
	return false;
}

static unsigned int mtk_disp_exdma_fmt_convert(unsigned int fmt, unsigned int blend_mode)
{
	/*
	 * DRM_FORMAT: bit 32->0, OVL_FMT: bit 0->32,
	 * so DRM_FORMAT_RGB888 = OVL_CON_CLRFMT_BGR888
	 */
	switch (fmt) {
	default:
	case DRM_FORMAT_RGB565:
		return OVL_EXDMA_CON_CLRFMT_RGB565;
	case DRM_FORMAT_BGR565:
		return OVL_EXDMA_CON_CLRFMT_BGR565;
	case DRM_FORMAT_RGB888:
		return OVL_EXDMA_CON_CLRFMT_RGB888;
	case DRM_FORMAT_BGR888:
		return OVL_EXDMA_CON_CLRFMT_BGR888;
	case DRM_FORMAT_RGBX8888:
	case DRM_FORMAT_RGBA8888:
	case DRM_FORMAT_RGBA1010102:
	case DRM_FORMAT_RGBX1010102:
		return ((blend_mode == DRM_MODE_BLEND_PREMULTI) ?
			OVL_EXDMA_CON_CLRFMT_PABGR8888 : OVL_EXDMA_CON_CLRFMT_ABGR8888) |
			(is_10bit_rgb(fmt) ? OVL_EXDMA_CON_CLRFMT_NB_10_BIT : 0);
	case DRM_FORMAT_BGRX8888:
	case DRM_FORMAT_BGRA8888:
	case DRM_FORMAT_BGRA1010102:
	case DRM_FORMAT_BGRX1010102:
		return ((blend_mode == DRM_MODE_BLEND_PREMULTI) ?
			OVL_EXDMA_CON_CLRFMT_PARGB8888 : OVL_EXDMA_CON_CLRFMT_ARGB8888) |
			(is_10bit_rgb(fmt) ? OVL_EXDMA_CON_CLRFMT_NB_10_BIT : 0);
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_ARGB2101010:
	case DRM_FORMAT_XRGB2101010:
		return ((blend_mode == DRM_MODE_BLEND_PREMULTI) ?
			OVL_EXDMA_CON_CLRFMT_PBGRA8888 : OVL_EXDMA_CON_CLRFMT_BGRA8888) |
			(is_10bit_rgb(fmt) ? OVL_EXDMA_CON_CLRFMT_NB_10_BIT : 0);
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_ABGR2101010:
	case DRM_FORMAT_XBGR2101010:
		return ((blend_mode == DRM_MODE_BLEND_PREMULTI) ?
			OVL_EXDMA_CON_CLRFMT_PRGBA8888 : OVL_EXDMA_CON_CLRFMT_RGBA8888) |
			(is_10bit_rgb(fmt) ? OVL_EXDMA_CON_CLRFMT_NB_10_BIT : 0);
	case DRM_FORMAT_UYVY:
		return OVL_EXDMA_CON_CLRFMT_UYVY;
	case DRM_FORMAT_YUYV:
		return OVL_EXDMA_CON_CLRFMT_YUYV;
	}
}

static unsigned int mtk_disp_exdma_color_convert(unsigned int color_encoding)
{
	switch (color_encoding) {
	default:
	case DRM_COLOR_YCBCR_BT709:
		return OVL_EXDMA_CON_INT_MTX_BT709_TO_RGB;
	case DRM_COLOR_YCBCR_BT601:
		return OVL_EXDMA_CON_INT_MTX_BT601_TO_RGB;
	}
}

void mtk_disp_exdma_start(struct device *dev)
{
	struct mtk_disp_exdma *priv = dev_get_drvdata(dev);
	unsigned int val = OVL_EXDMA_DATAPATH_CON_LAYER_SMI_ID_EN |
			   OVL_EXDMA_DATAPATH_CON_HDR_GCLAST_EN |
			   OVL_EXDMA_DATAPATH_CON_GCLAST_EN;

	writel(val, priv->regs + DISP_REG_OVL_EXDMA_DATAPATH_CON);
	writel(OVL_EXDMA_EN, priv->regs + DISP_REG_OVL_EXDMA_EN);
}

void mtk_disp_exdma_stop(struct device *dev)
{
	struct mtk_disp_exdma *priv = dev_get_drvdata(dev);

	writel(0, priv->regs + DISP_REG_OVL_EXDMA_EN);
	writel(0, priv->regs + DISP_REG_OVL_EXDMA_DATAPATH_CON);
	writel(OVL_EXDMA_RST, priv->regs + DISP_REG_OVL_EXDMA_RST);
	writel(0, priv->regs + DISP_REG_OVL_EXDMA_RST);
}

void mtk_disp_exdma_layer_config(struct device *dev, struct mtk_plane_state *state,
				 struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_disp_exdma *priv = dev_get_drvdata(dev);
	struct mtk_plane_pending_state *pending = &state->pending;
	const struct drm_format_info *fmt_info = drm_format_info(pending->format);
	bool csc_enable = (fmt_info) ? fmt_info->is_yuv : false;
	unsigned int blend_mode = DRM_MODE_BLEND_PIXEL_NONE;
	unsigned int val;

	if (!pending->enable || pending->height == 0 || pending->width == 0 ||
	    pending->x > OVL_EXDMA_MAX_SIZE || pending->y > OVL_EXDMA_MAX_SIZE) {
		mtk_ddp_write_mask(cmdq_pkt, 0, &priv->cmdq_reg, priv->regs,
				   DISP_REG_OVL_EXDMA_RDMA0_CTRL, OVL_EXDMA_RDMA0_EN);
		mtk_ddp_write_mask(cmdq_pkt, 0, &priv->cmdq_reg, priv->regs,
				   DISP_REG_OVL_EXDMA_L0_EN, OVL_EXDMA_L0_EN);
		return;
	}

	mtk_ddp_write(cmdq_pkt, pending->height << 16 | pending->width, &priv->cmdq_reg,
		      priv->regs, DISP_REG_OVL_EXDMA_ROI_SIZE);
	mtk_ddp_write(cmdq_pkt, pending->height << 16 | pending->width, &priv->cmdq_reg,
		      priv->regs, DISP_REG_OVL_EXDMA_SRC_SIZE);

	mtk_ddp_write(cmdq_pkt, pending->y << 16 | pending->x, &priv->cmdq_reg, priv->regs,
		      DISP_REG_OVL_EXDMA_L0_OFFSET);
	if (pending->is_secure)
		mtk_ddp_sec_write(cmdq_pkt, CMDQ_IWC_H_2_MVA, pending->addr, 0,
				  &priv->cmdq_reg, DISP_REG_OVL_EXDMA_ADDR);
	else
		mtk_ddp_write(cmdq_pkt, pending->addr, &priv->cmdq_reg,
			      priv->regs, DISP_REG_OVL_EXDMA_ADDR);

	/* alpha blend setting */
	if (state->base.fb && state->base.fb->format->has_alpha)
		blend_mode = state->base.pixel_blend_mode;

	val = pending->pitch;
	if (blend_mode == DRM_MODE_BLEND_PIXEL_NONE)
		val |= OVL_EXDMA_L0_CONST_BLD;
	mtk_ddp_write_mask(cmdq_pkt, val, &priv->cmdq_reg, priv->regs, DISP_REG_OVL_EXDMA_PITCH,
			   OVL_EXDMA_L0_CONST_BLD | OVL_EXDMA_L0_SRC_PITCH_MASK);
	mtk_ddp_write_mask(cmdq_pkt, pending->pitch >> 16, &priv->cmdq_reg, priv->regs,
			   DISP_REG_OVL_EXDMA_PITCH_MSB, OVL_EXDMA_L0_SRC_PITCH_MSB_MASK);

	val = mtk_disp_exdma_color_convert(pending->color_encoding);
	if (csc_enable)
		val |= OVL_EXDMA_CON_INT_MTX_EN;
	mtk_ddp_write_mask(cmdq_pkt, val, &priv->cmdq_reg, priv->regs, DISP_REG_OVL_EXDMA_CON,
			   OVL_EXDMA_CON_FLD_INT_MTX_SEL | OVL_EXDMA_CON_INT_MTX_EN);

	val = mtk_disp_exdma_fmt_convert(pending->format, blend_mode);
	mtk_ddp_write_mask(cmdq_pkt, val, &priv->cmdq_reg, priv->regs,
			   DISP_REG_OVL_EXDMA_L0_CLRFMT,
			   OVL_EXDMA_CON_RGB_SWAP | OVL_EXDMA_CON_BYTE_SWAP |
			   OVL_EXDMA_CON_CLRFMT_MAN | OVL_EXDMA_CON_FLD_CLRFMT |
			   OVL_EXDMA_CON_FLD_CLRFMT_NB);

	mtk_ddp_write_mask(cmdq_pkt, OVL_EXDMA_RDMA0_EN, &priv->cmdq_reg, priv->regs,
			   DISP_REG_OVL_EXDMA_RDMA0_CTRL, OVL_EXDMA_RDMA0_EN);
	mtk_ddp_write_mask(cmdq_pkt, OVL_EXDMA_L0_EN, &priv->cmdq_reg, priv->regs,
			   DISP_REG_OVL_EXDMA_L0_EN, OVL_EXDMA_L0_EN);
}

void mtk_disp_exdma_config(struct device *dev)
{
	struct mtk_disp_exdma *priv = dev_get_drvdata(dev);
	unsigned int tmp, val, mask;

	/*
	 * This configuration enables dynamic power switching mechanism for EXDMA,
	 * also known as "SRT mode".
	 * Such configuration allows the system to achieve better power efficiency.
	 */
	val = OVL_EXDMA_RDMA_BURST_CON1_BURST16_EN | OVL_EXDMA_RDMA_BURST_CON1_DDR_ACK_EN;
	mask = OVL_EXDMA_RDMA_BURST_CON1_BURST16_EN | OVL_EXDMA_RDMA_BURST_CON1_DDR_EN |
	       OVL_EXDMA_RDMA_BURST_CON1_DDR_ACK_EN;
	tmp = readl(priv->regs + DISP_REG_OVL_EXDMA_RDMA_BURST_CON1);
	tmp = (tmp & ~mask) | val;
	writel(tmp, priv->regs + DISP_REG_OVL_EXDMA_RDMA_BURST_CON1);

	/*
	 * The dummy register is used in the configuration of the EXDMA engine to
	 * signal ddren_request, and get ddren_ack before accessing the DRAM to
	 * ensure data transfers occur normally.
	 */
	val = OVL_EXDMA_EXT_DDR_EN_OPT | OVL_EXDMA_FORCE_EXT_DDR_EN;
	writel(val, priv->regs + DISP_REG_OVL_EXDMA_DUMMY_REG);

	val = OVL_EXDMA_MOUT_BGCLR_OUT;
	mask = OVL_EXDMA_MOUT_BGCLR_OUT | OVL_EXDMA_MOUT_OUT_DATA;
	tmp = readl(priv->regs + DISP_REG_OVL_EXDMA_MOUT);
	tmp = (tmp & ~mask) | val;
	writel(tmp, priv->regs + DISP_REG_OVL_EXDMA_MOUT);

	writel(GENMASK(31, 0), priv->regs + DISP_REG_OVL_EXDMA_GDRDY_PRD);

	val = OVL_EXDMA_HG_FOVL_CK_ON | OVL_EXDMA_HF_FOVL_CK_ON | OVL_EXDMA_OP_8BIT_MODE;
	writel(val, priv->regs + DISP_REG_OVL_EXDMA_EN_CON);

	writel(OVL_EXDMA_RDMA0_L0_VCSEL, priv->regs + DISP_REG_OVL_EXDMA_L0_GUSER_EXT);
}

const u32 *mtk_disp_exdma_get_formats(struct device *dev)
{
	return formats;
}

size_t mtk_disp_exdma_get_num_formats(struct device *dev)
{
	return ARRAY_SIZE(formats);
}

int mtk_disp_exdma_clk_enable(struct device *dev)
{
	struct mtk_disp_exdma *exdma = dev_get_drvdata(dev);

	return clk_prepare_enable(exdma->clk);
}

void mtk_disp_exdma_clk_disable(struct device *dev)
{
	struct mtk_disp_exdma *exdma = dev_get_drvdata(dev);

	clk_disable_unprepare(exdma->clk);
}

void mtk_disp_exdma_set_hrt_bw(struct device *dev, unsigned int bw)
{
	struct mtk_disp_exdma *priv = dev_get_drvdata(dev);

	if (IS_ERR(priv->hrt_qos_req))
		return;

	mtk_disp_pmqos_set_module_hrt(priv->hrt_qos_req, dev, bw);
}

void mtk_disp_exdma_set_srt_bw(struct device *dev, unsigned int bw)
{
	struct mtk_disp_exdma *priv = dev_get_drvdata(dev);

	if (IS_ERR(priv->qos_req))
		return;

	mtk_disp_pmqos_set_module_srt(priv->qos_req, dev, bw);
}

static int mtk_disp_exdma_bind(struct device *dev, struct device *master,
			       void *data)
{
#if IS_REACHABLE(CONFIG_INTERCONNECT_MTK_EXTENSION)
	struct mtk_disp_exdma *priv = dev_get_drvdata(dev);

	priv->hrt_qos_req = of_mtk_icc_get(dev, "hrt_qos");
	priv->qos_req = of_mtk_icc_get(dev, "srt_qos");
#endif
	return 0;
}

static void mtk_disp_exdma_unbind(struct device *dev, struct device *master,
				  void *data)
{
}

static const struct component_ops mtk_disp_exdma_component_ops = {
	.bind	= mtk_disp_exdma_bind,
	.unbind = mtk_disp_exdma_unbind,
};

static int mtk_disp_exdma_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct platform_device *larb_pdev = NULL;
	struct device_node *larb_node = NULL;
	struct resource *res;
	struct mtk_disp_exdma *priv;
	int ret = 0;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->regs)) {
		dev_err(dev, "failed to ioremap exdma\n");
		return PTR_ERR(priv->regs);
	}

	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(dev, "failed to get exdma clk\n");
		return PTR_ERR(priv->clk);
	}

	larb_node = of_parse_phandle(dev->of_node, "mediatek,larb", 0);
	if (larb_node) {
		larb_pdev = of_find_device_by_node(larb_node);
		if (larb_pdev)
			priv->larb = &larb_pdev->dev;
		of_node_put(larb_node);
	}

	if (!priv->larb) {
		dev_dbg(dev, "not find larb dev");
		return -EPROBE_DEFER;
	}
	device_link_add(dev, priv->larb, DL_FLAG_PM_RUNTIME | DL_FLAG_STATELESS);

#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	ret = cmdq_dev_get_client_reg(dev, &priv->cmdq_reg, 0);
	if (ret)
		dev_dbg(dev, "No mediatek,gce-client-reg\n");
#endif
	platform_set_drvdata(pdev, priv);

	pm_runtime_enable(dev);

	ret = component_add(dev, &mtk_disp_exdma_component_ops);
	if (ret != 0) {
		pm_runtime_disable(dev);
		dev_err(dev, "Failed to add component: %d\n", ret);
	}
	return ret;
}

static int mtk_disp_exdma_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &mtk_disp_exdma_component_ops);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static const struct of_device_id mtk_disp_exdma_driver_dt_match[] = {
	{ .compatible = "mediatek,mt8196-exdma", },
	{},
};
MODULE_DEVICE_TABLE(of, mtk_disp_exdma_driver_dt_match);

struct platform_driver mtk_disp_exdma_driver = {
	.probe = mtk_disp_exdma_probe,
	.remove = mtk_disp_exdma_remove,
	.driver = {
		.name = "mediatek-disp-exdma",
		.owner = THIS_MODULE,
		.of_match_table = mtk_disp_exdma_driver_dt_match,
	},
};

MODULE_AUTHOR("Nancy Lin <nancy.lin@mediatek.com>");
MODULE_DESCRIPTION("MediaTek Exdma Driver");
MODULE_LICENSE("GPL");
