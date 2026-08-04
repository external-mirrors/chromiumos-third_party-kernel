// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015 MediaTek Inc.
 * Authors:
 *	YT Shen <yt.shen@mediatek.com>
 *	CK Hu <ck.hu@mediatek.com>
 */

#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/soc/mediatek/mtk-cmdq.h>
#include <drm/drm_print.h>

#include "mtk_crtc.h"
#include "mtk_ddp_comp.h"
#include "mtk_disp_drv.h"
#include "mtk_drm_drv.h"
#include "mtk_plane.h"

#define DISP_REG_MDP_RSZ_EN			0x0000
#define DISP_REG_MDP_RSZ_INPUT_SIZE		0x0010
#define DISP_REG_MDP_RSZ_OUTPUT_SIZE		0x0014

#define DISP_REG_OD_EN				0x0000
#define DISP_REG_OD_CFG				0x0020
#define OD_RELAYMODE				BIT(0)
#define OD_DITHERING					BIT(2)
#define DISP_REG_OD_SIZE			0x0030

#define DISP_REG_POSTMASK_EN			0x0000
#define POSTMASK_EN					BIT(0)
#define DISP_REG_POSTMASK_CFG			0x0020
#define POSTMASK_RELAY_MODE				BIT(0)
#define DISP_REG_POSTMASK_SIZE			0x0030

#define DISP_REG_TDSHP_EN			0x0000
#define DISP_TDSHP_TDS_EN			BIT(31)
#define DISP_REG_TDSHP_CTRL			0x0100
#define DISP_TDSHP_CTRL_EN			BIT(0)
#define DISP_TDSHP_PWR_SCL_EN			BIT(2)
#define DISP_REG_TDSHP_CFG			0x0110
#define DISP_REG_TDSHP_INPUT_SIZE		0x0120
#define DISP_REG_TDSHP_OUTPUT_OFFSET		0x0124
#define DISP_REG_TDSHP_OUTPUT_SIZE		0x0128

#define DISP_REG_UFO_START			0x0000
#define UFO_BYPASS				BIT(2)

struct mtk_ddp_comp_dev {
	struct clk *clk;
	void __iomem *regs;
	struct cmdq_client_reg cmdq_reg;
};

#if IS_REACHABLE(CONFIG_MTK_CMDQ)
static void mtk_ddp_write_cmdq_pkt(struct cmdq_pkt *cmdq_pkt, struct cmdq_client_reg *cmdq_reg,
				   unsigned int offset, unsigned int value, unsigned int mask)
{
	offset += cmdq_reg->offset;

	if (cmdq_reg->subsys != CMDQ_SUBSYS_INVALID) {
		if (mask == GENMASK(31, 0))
			cmdq_pkt_write(cmdq_pkt, cmdq_reg->subsys, offset, value);
		else
			cmdq_pkt_write_mask(cmdq_pkt, cmdq_reg->subsys, offset, value, mask);
	} else {
		/* only MMIO access, no need to check mminfro_offset */
		cmdq_pkt_assign(cmdq_pkt, 0, CMDQ_ADDR_HIGH(cmdq_reg->pa_base));
		if (mask == GENMASK(31, 0))
			cmdq_pkt_write_s_value(cmdq_pkt, CMDQ_THR_SPR_IDX0,
					       CMDQ_ADDR_LOW(offset), value);
		else
			cmdq_pkt_write_s_mask_value(cmdq_pkt, CMDQ_THR_SPR_IDX0,
						    CMDQ_ADDR_LOW(offset), value, mask);
	}
}
#endif

void mtk_ddp_write(struct cmdq_pkt *cmdq_pkt, unsigned int value,
		   struct cmdq_client_reg *cmdq_reg, void __iomem *regs,
		   unsigned int offset)
{
#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	if (cmdq_pkt)
		mtk_ddp_write_cmdq_pkt(cmdq_pkt, cmdq_reg, offset, value, GENMASK(31, 0));
	else
#endif
		writel(value, regs + offset);
}

void mtk_ddp_write_relaxed(struct cmdq_pkt *cmdq_pkt, unsigned int value,
			   struct cmdq_client_reg *cmdq_reg, void __iomem *regs,
			   unsigned int offset)
{
#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	if (cmdq_pkt)
		mtk_ddp_write_cmdq_pkt(cmdq_pkt, cmdq_reg, offset, value, GENMASK(31, 0));
	else
#endif
		writel_relaxed(value, regs + offset);
}

void mtk_ddp_write_mask(struct cmdq_pkt *cmdq_pkt, unsigned int value,
			struct cmdq_client_reg *cmdq_reg, void __iomem *regs,
			unsigned int offset, unsigned int mask)
{
#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	if (cmdq_pkt) {
		mtk_ddp_write_cmdq_pkt(cmdq_pkt, cmdq_reg, offset, value, mask);
	} else {
#endif
		u32 tmp = readl(regs + offset);

		tmp = (tmp & ~mask) | (value & mask);
		writel(tmp, regs + offset);
#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	}
#endif
}

void mtk_ddp_sec_write(struct cmdq_pkt *cmdq_pkt,
		       enum cmdq_iwc_addr_metadata_type type,
		       unsigned int base, unsigned int base_offset,
		       struct cmdq_client_reg *cmdq_reg, unsigned int offset)
{
#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	if (!cmdq_pkt)
		return;

	cmdq_sec_pkt_write(cmdq_pkt, cmdq_reg->subsys, cmdq_reg->pa_base,
			   cmdq_reg->offset + offset,
			   type, base, base_offset);
#endif
}

static int mtk_ddp_clk_enable(struct device *dev)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	return clk_prepare_enable(priv->clk);
}

static void mtk_ddp_clk_disable(struct device *dev)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	clk_disable_unprepare(priv->clk);
}

static void mtk_od_config(struct device *dev, unsigned int w,
			  unsigned int h, unsigned int vrefresh,
			  unsigned int bpc, struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	mtk_ddp_write(cmdq_pkt, w << 16 | h, &priv->cmdq_reg, priv->regs, DISP_REG_OD_SIZE);
	mtk_ddp_write(cmdq_pkt, OD_RELAYMODE, &priv->cmdq_reg, priv->regs, DISP_REG_OD_CFG);
	mtk_dither_set_common(priv->regs, &priv->cmdq_reg, MTK_DEFAULT_MAX_BPC, bpc,
			      DISP_REG_OD_CFG, OD_DITHERING, cmdq_pkt);
}

static void mtk_od_start(struct device *dev)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	writel(1, priv->regs + DISP_REG_OD_EN);
}

static void mtk_mdp_rsz_config(struct device *dev, unsigned int w,
			       unsigned int h, unsigned int vrefresh,
			       unsigned int bpc, struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	mtk_ddp_write(cmdq_pkt, 0, &priv->cmdq_reg, priv->regs,
		      DISP_REG_MDP_RSZ_INPUT_SIZE);
	mtk_ddp_write(cmdq_pkt, 0, &priv->cmdq_reg, priv->regs,
		      DISP_REG_MDP_RSZ_OUTPUT_SIZE);
}

static void mtk_postmask_config(struct device *dev, unsigned int w,
				unsigned int h, unsigned int vrefresh,
				unsigned int bpc, struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	mtk_ddp_write(cmdq_pkt, w << 16 | h, &priv->cmdq_reg, priv->regs,
		      DISP_REG_POSTMASK_SIZE);
	mtk_ddp_write(cmdq_pkt, POSTMASK_RELAY_MODE, &priv->cmdq_reg,
		      priv->regs, DISP_REG_POSTMASK_CFG);
}

static void mtk_postmask_start(struct device *dev)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	writel(POSTMASK_EN, priv->regs + DISP_REG_POSTMASK_EN);
}

static void mtk_postmask_stop(struct device *dev)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	writel_relaxed(0x0, priv->regs + DISP_REG_POSTMASK_EN);
}

static void mtk_disp_tdshp_config(struct device *dev, unsigned int w,
				  unsigned int h, unsigned int vrefresh,
				  unsigned int bpc, struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	if (bpc == 8)
		mtk_ddp_write_mask(cmdq_pkt, DISP_TDSHP_PWR_SCL_EN | DISP_TDSHP_CTRL_EN,
				   &priv->cmdq_reg, priv->regs, DISP_REG_TDSHP_CTRL, ~0);
	else if (bpc == 10)
		mtk_ddp_write_mask(cmdq_pkt, DISP_TDSHP_CTRL_EN, &priv->cmdq_reg, priv->regs,
				   DISP_REG_TDSHP_CTRL, ~0);
	else
		DRM_INFO("Invalid bpc %u\n", bpc);

	mtk_ddp_write(cmdq_pkt, w << 16 | h, &priv->cmdq_reg, priv->regs,
		      DISP_REG_TDSHP_INPUT_SIZE);
	mtk_ddp_write(cmdq_pkt, w << 16 | h, &priv->cmdq_reg, priv->regs,
		      DISP_REG_TDSHP_OUTPUT_SIZE);
	mtk_ddp_write(cmdq_pkt, 0x0, &priv->cmdq_reg, priv->regs,
		      DISP_REG_TDSHP_OUTPUT_OFFSET);

	mtk_ddp_write(cmdq_pkt, 0x1, &priv->cmdq_reg,
		      priv->regs, DISP_REG_TDSHP_CFG);

	mtk_ddp_write_mask(cmdq_pkt, DISP_TDSHP_TDS_EN, &priv->cmdq_reg, priv->regs,
			   DISP_REG_TDSHP_EN, DISP_TDSHP_TDS_EN);
}

static void mtk_disp_tdshp_start(struct device *dev)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	writel(DISP_TDSHP_CTRL_EN, priv->regs + DISP_REG_TDSHP_CTRL);
}

static void mtk_disp_tdshp_stop(struct device *dev)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	writel(0x0, priv->regs + DISP_REG_TDSHP_CTRL);
}

static void mtk_ufoe_start(struct device *dev)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	writel(UFO_BYPASS, priv->regs + DISP_REG_UFO_START);
}

static const struct mtk_ddp_comp_funcs ddp_tdshp = {
	.clk_enable = mtk_ddp_clk_enable,
	.clk_disable = mtk_ddp_clk_disable,
	.config = mtk_disp_tdshp_config,
	.start = mtk_disp_tdshp_start,
	.stop = mtk_disp_tdshp_stop,
};

static const struct mtk_ddp_comp_funcs ddp_aal = {
	.clk_enable = mtk_aal_clk_enable,
	.clk_disable = mtk_aal_clk_disable,
	.gamma_get_lut_size = mtk_aal_gamma_get_lut_size,
	.gamma_set = mtk_aal_gamma_set,
	.config = mtk_aal_config,
	.start = mtk_aal_start,
	.stop = mtk_aal_stop,
};

static const struct mtk_ddp_comp_funcs ddp_ccorr = {
	.clk_enable = mtk_ccorr_clk_enable,
	.clk_disable = mtk_ccorr_clk_disable,
	.config = mtk_ccorr_config,
	.start = mtk_ccorr_start,
	.stop = mtk_ccorr_stop,
	.ctm_set = mtk_ccorr_ctm_set,
};

static const struct mtk_ddp_comp_funcs ddp_color = {
	.clk_enable = mtk_color_clk_enable,
	.clk_disable = mtk_color_clk_disable,
	.config = mtk_color_config,
	.start = mtk_color_start,
};

static const struct mtk_ddp_comp_funcs ddp_dither = {
	.clk_enable = mtk_dither_clk_enable,
	.clk_disable = mtk_dither_clk_disable,
	.config = mtk_dither_config,
	.start = mtk_dither_start,
	.stop = mtk_dither_stop,
	.bypass = mtk_dither_bypass,
	.crc_cnt = mtk_dither_crc_cnt,
	.crc_entry = mtk_dither_crc_entry,
	.crc_read = mtk_dither_crc_read,
	.crc_reset = mtk_dither_crc_reset,
	.crc_attach = mtk_dither_crc_attach,
	.crc_detach = mtk_dither_crc_detach,
};

static const struct mtk_ddp_comp_funcs ddp_dpi = {
	.start = mtk_dpi_start,
	.stop = mtk_dpi_stop,
	.encoder_index = mtk_dpi_encoder_index,
};

static const struct mtk_ddp_comp_funcs ddp_dpi_v2 = {
	.start = mtk_dpi_start_v2,
	.stop = mtk_dpi_stop_v2,
	.encoder_index = mtk_dpi_encoder_index_v2,
	.get_dsc_info = mtk_dpi_get_dsc_info_v2,
	.get_hrt_bw_by_datarate = mtk_dpi_get_hrt_bw_by_datarate,
};

static const struct mtk_ddp_comp_funcs ddp_dsc = {
	.clk_enable = mtk_dsc_clk_enable,
	.clk_disable = mtk_dsc_clk_disable,
	.config = mtk_dsc_config,
	.start = mtk_dsc_start,
	.stop = mtk_dsc_stop,
	.set_dsc_info = mtk_dsc_set_dsc_info,
};

static const struct mtk_ddp_comp_funcs ddp_dsi = {
	.start = mtk_dsi_ddp_start,
	.stop = mtk_dsi_ddp_stop,
	.encoder_index = mtk_dsi_encoder_index,
	.get_dsc_info = mtk_dsi_get_dsc_info,
	.get_hrt_bw_by_datarate = mtk_dsi_get_hrt_bw_by_datarate,
};

static const struct mtk_ddp_comp_funcs ddp_dvo = {
	.start = mtk_dvo_start,
	.stop = mtk_dvo_stop,
	.get_hrt_bw_by_datarate = mtk_dvo_get_hrt_bw_by_datarate,
	.encoder_index = mtk_dvo_encoder_index,
};

static const struct mtk_ddp_comp_funcs ddp_gamma = {
	.clk_enable = mtk_gamma_clk_enable,
	.clk_disable = mtk_gamma_clk_disable,
	.gamma_get_lut_size = mtk_gamma_get_lut_size,
	.gamma_set = mtk_gamma_set,
	.config = mtk_gamma_config,
	.start = mtk_gamma_start,
	.stop = mtk_gamma_stop,
};

static const struct mtk_ddp_comp_funcs ddp_merge = {
	.clk_enable = mtk_merge_clk_enable,
	.clk_disable = mtk_merge_clk_disable,
	.start = mtk_merge_start,
	.stop = mtk_merge_stop,
	.config = mtk_merge_config,
	.set_dsc_info = mtk_merge_set_dsc_info,
};

static const struct mtk_ddp_comp_funcs ddp_od = {
	.clk_enable = mtk_ddp_clk_enable,
	.clk_disable = mtk_ddp_clk_disable,
	.config = mtk_od_config,
	.start = mtk_od_start,
};

static const struct mtk_ddp_comp_funcs ddp_ovl = {
	.clk_enable = mtk_ovl_clk_enable,
	.clk_disable = mtk_ovl_clk_disable,
	.config = mtk_ovl_config,
	.start = mtk_ovl_start,
	.stop = mtk_ovl_stop,
	.register_vblank_cb = mtk_ovl_register_vblank_cb,
	.unregister_vblank_cb = mtk_ovl_unregister_vblank_cb,
	.enable_vblank = mtk_ovl_enable_vblank,
	.disable_vblank = mtk_ovl_disable_vblank,
	.supported_rotations = mtk_ovl_supported_rotations,
	.layer_nr = mtk_ovl_layer_nr,
	.layer_check = mtk_ovl_layer_check,
	.layer_config = mtk_ovl_layer_config,
	.bgclr_in_on = mtk_ovl_bgclr_in_on,
	.bgclr_in_off = mtk_ovl_bgclr_in_off,
	.get_blend_modes = mtk_ovl_get_blend_modes,
	.get_formats = mtk_ovl_get_formats,
	.get_num_formats = mtk_ovl_get_num_formats,
};

static const struct mtk_ddp_comp_funcs ddp_ovlsys_adaptor = {
	.clk_enable = mtk_ovlsys_adaptor_clk_enable,
	.clk_disable = mtk_ovlsys_adaptor_clk_disable,
	.config = mtk_ovlsys_adaptor_config,
	.crc_cnt = mtk_ovlsys_adaptor_crc_cnt,
	.crc_entry = mtk_ovlsys_adaptor_crc_entry,
	.crc_read = mtk_ovlsys_adaptor_crc_read,
	.crc_reset = mtk_ovlsys_adaptor_crc_reset,
	.crc_attach = mtk_ovlsys_adaptor_crc_attach,
	.crc_detach = mtk_ovlsys_adaptor_crc_detach,
	.start = mtk_ovlsys_adaptor_start,
	.stop = mtk_ovlsys_adaptor_stop,
	.layer_check = mtk_ovlsys_adaptor_layer_check,
	.layer_nr = mtk_ovlsys_adaptor_layer_nr,
	.layer_config = mtk_ovlsys_adaptor_layer_config,
	.register_vblank_cb = mtk_ovlsys_adaptor_register_vblank_cb,
	.unregister_vblank_cb = mtk_ovlsys_adaptor_unregister_vblank_cb,
	.enable_vblank = mtk_ovlsys_adaptor_enable_vblank,
	.disable_vblank = mtk_ovlsys_adaptor_disable_vblank,
	.dma_dev_get = mtk_ovlsys_adaptor_dma_dev_get,
	.connect = mtk_ovlsys_adaptor_connect,
	.disconnect = mtk_ovlsys_adaptor_disconnect,
	.add = mtk_ovlsys_adaptor_add_comp,
	.remove = mtk_ovlsys_adaptor_remove_comp,
	.get_blend_modes = mtk_ovlsys_adaptor_get_blend_modes,
	.get_formats = mtk_ovlsys_adaptor_get_formats,
	.get_num_formats = mtk_ovlsys_adaptor_get_num_formats,
	.fifo_sel = mtk_ovlsys_adaptor_fifo_sel,
	.get_smi_channel_id = mtk_ovlsys_adaptor_get_channel_id,
	.set_hrt_bw = mtk_ovlsys_adaptor_set_hrt_bw,
	.set_srt_bw = mtk_ovlsys_adaptor_set_srt_bw,
};

static const struct mtk_ddp_comp_funcs ddp_mdp_rsz = {
	.clk_enable = mtk_ddp_clk_enable,
	.clk_disable = mtk_ddp_clk_disable,
	.config = mtk_mdp_rsz_config,
};

static const struct mtk_ddp_comp_funcs ddp_postmask = {
	.clk_enable = mtk_ddp_clk_enable,
	.clk_disable = mtk_ddp_clk_disable,
	.config = mtk_postmask_config,
	.start = mtk_postmask_start,
	.stop = mtk_postmask_stop,
};

static const struct mtk_ddp_comp_funcs ddp_rdma = {
	.clk_enable = mtk_rdma_clk_enable,
	.clk_disable = mtk_rdma_clk_disable,
	.config = mtk_rdma_config,
	.start = mtk_rdma_start,
	.stop = mtk_rdma_stop,
	.register_vblank_cb = mtk_rdma_register_vblank_cb,
	.unregister_vblank_cb = mtk_rdma_unregister_vblank_cb,
	.enable_vblank = mtk_rdma_enable_vblank,
	.disable_vblank = mtk_rdma_disable_vblank,
	.layer_nr = mtk_rdma_layer_nr,
	.layer_config = mtk_rdma_layer_config,
	.get_formats = mtk_rdma_get_formats,
	.get_num_formats = mtk_rdma_get_num_formats,
};

static const struct mtk_ddp_comp_funcs ddp_splitter = {
	.clk_enable = mtk_splitter_clk_enable,
	.clk_disable = mtk_splitter_clk_disable,
	.config = mtk_splitter_config,
	.start = mtk_splitter_start,
	.stop = mtk_splitter_stop,
	.connect = mtk_splitter_connect,
	.disconnect = mtk_splitter_disconnect,
	.add = mtk_splitter_add,
	.remove = mtk_splitter_remove,
	.set_dsc_info = mtk_splitter_set_dsc_info,
};

static const struct mtk_ddp_comp_funcs ddp_ufoe = {
	.clk_enable = mtk_ddp_clk_enable,
	.clk_disable = mtk_ddp_clk_disable,
	.start = mtk_ufoe_start,
};

static const struct mtk_ddp_comp_funcs ddp_ovl_adaptor = {
	.power_on = mtk_ovl_adaptor_power_on,
	.power_off = mtk_ovl_adaptor_power_off,
	.clk_enable = mtk_ovl_adaptor_clk_enable,
	.clk_disable = mtk_ovl_adaptor_clk_disable,
	.config = mtk_ovl_adaptor_config,
	.start = mtk_ovl_adaptor_start,
	.stop = mtk_ovl_adaptor_stop,
	.layer_nr = mtk_ovl_adaptor_layer_nr,
	.layer_config = mtk_ovl_adaptor_layer_config,
	.register_vblank_cb = mtk_ovl_adaptor_register_vblank_cb,
	.unregister_vblank_cb = mtk_ovl_adaptor_unregister_vblank_cb,
	.enable_vblank = mtk_ovl_adaptor_enable_vblank,
	.disable_vblank = mtk_ovl_adaptor_disable_vblank,
	.dma_dev_get = mtk_ovl_adaptor_dma_dev_get,
	.connect = mtk_ovl_adaptor_connect,
	.disconnect = mtk_ovl_adaptor_disconnect,
	.add = mtk_ovl_adaptor_add_comp,
	.remove = mtk_ovl_adaptor_remove_comp,
	.get_blend_modes = mtk_ovl_adaptor_get_blend_modes,
	.get_formats = mtk_ovl_adaptor_get_formats,
	.get_num_formats = mtk_ovl_adaptor_get_num_formats,
	.mode_valid = mtk_ovl_adaptor_mode_valid,
};

static const char * const mtk_ddp_comp_stem[MTK_DDP_COMP_TYPE_MAX] = {
	[MTK_DISP_AAL] = "aal",
	[MTK_DISP_BLS] = "bls",
	[MTK_DISP_CCORR] = "ccorr",
	[MTK_DISP_COLOR] = "color",
	[MTK_DISP_DITHER] = "dither",
	[MTK_DISP_DSC] = "dsc",
	[MTK_DISP_GAMMA] = "gamma",
	[MTK_DISP_MDP_RSZ] = "mdp-rsz",
	[MTK_DISP_MERGE] = "merge",
	[MTK_DISP_MUTEX] = "mutex",
	[MTK_DISP_OD] = "od",
	[MTK_DISP_OVL] = "ovl",
	[MTK_DISP_OVL_2L] = "ovl-2l",
	[MTK_DISP_OVL_ADAPTOR] = "ovl_adaptor",
	[MTK_DISP_OVLSYS_ADAPTOR] = "ovlsys_adaptor",
	[MTK_DISP_POSTMASK] = "postmask",
	[MTK_DISP_PWM] = "pwm",
	[MTK_DISP_RDMA] = "rdma",
	[MTK_DISP_SPLITTER] = "splitter",
	[MTK_DISP_TDSHP] = "tdshp",
	[MTK_DISP_UFOE] = "ufoe",
	[MTK_DISP_VIRTUAL] = "virtual",
	[MTK_DISP_WDMA] = "wdma",
	[MTK_DP_INTF] = "dp-intf",
	[MTK_DPI] = "dpi",
	[MTK_DSI] = "dsi",
	[MTK_DVO] = "dvo",
	[MTK_OVL_BLENDER] = "blender",
	[MTK_OVL_EXDMA] = "exdma",
	[MTK_OVL_OUTPROC] = "outproc",
};

struct mtk_ddp_comp_match {
	enum mtk_ddp_comp_type type;
	int alias_id;
	const struct mtk_ddp_comp_funcs *funcs;
};

static const struct mtk_ddp_comp_match mtk_ddp_matches[DDP_COMPONENT_DRM_ID_MAX] = {
	[DDP_COMPONENT_AAL0]		= { MTK_DISP_AAL,		0, &ddp_aal },
	[DDP_COMPONENT_AAL1]		= { MTK_DISP_AAL,		1, &ddp_aal },
	[DDP_COMPONENT_BLS]		= { MTK_DISP_BLS,		0, NULL },
	[DDP_COMPONENT_CCORR0]		= { MTK_DISP_CCORR,		0, &ddp_ccorr },
	[DDP_COMPONENT_CCORR1]		= { MTK_DISP_CCORR,		1, &ddp_ccorr },
	[DDP_COMPONENT_CCORR2]		= { MTK_DISP_CCORR,		2, &ddp_ccorr },
	[DDP_COMPONENT_CCORR3]		= { MTK_DISP_CCORR,		3, &ddp_ccorr },
	[DDP_COMPONENT_COLOR0]		= { MTK_DISP_COLOR,		0, &ddp_color },
	[DDP_COMPONENT_COLOR1]		= { MTK_DISP_COLOR,		1, &ddp_color },
	[DDP_COMPONENT_DITHER0]		= { MTK_DISP_DITHER,		0, &ddp_dither },
	[DDP_COMPONENT_DITHER1]		= { MTK_DISP_DITHER,		1, &ddp_dither },
	[DDP_COMPONENT_DLI_ASYNC0]      = { MTK_DISP_VIRTUAL,           -1, NULL },
	[DDP_COMPONENT_DLI_ASYNC1]      = { MTK_DISP_VIRTUAL,           -1, NULL },
	[DDP_COMPONENT_DLI_ASYNC8]      = { MTK_DISP_VIRTUAL,           -1, NULL },
	[DDP_COMPONENT_DLI_ASYNC21]     = { MTK_DISP_VIRTUAL,           -1, NULL },
	[DDP_COMPONENT_DLI_ASYNC22]     = { MTK_DISP_VIRTUAL,           -1, NULL },
	[DDP_COMPONENT_DLI_ASYNC23]     = { MTK_DISP_VIRTUAL,           -1, NULL },
	[DDP_COMPONENT_DLO_ASYNC1]      = { MTK_DISP_VIRTUAL,           -1, NULL },
	[DDP_COMPONENT_DLO_ASYNC2]      = { MTK_DISP_VIRTUAL,           -1, NULL },
	[DDP_COMPONENT_DLO_ASYNC3]      = { MTK_DISP_VIRTUAL,           -1, NULL },
	[DDP_COMPONENT_DP_INTF0]	= { MTK_DP_INTF,		0, &ddp_dpi },
	[DDP_COMPONENT_DP_INTF1]	= { MTK_DP_INTF,		1, &ddp_dpi },
	[DDP_COMPONENT_DPI0]		= { MTK_DPI,			0, &ddp_dpi },
	[DDP_COMPONENT_DPI1]		= { MTK_DPI,			1, &ddp_dpi },
	[DDP_COMPONENT_DRM_OVL_ADAPTOR]	= { MTK_DISP_OVL_ADAPTOR,	0, &ddp_ovl_adaptor },
	[DDP_COMPONENT_DRM_OVLSYS_ADAPTOR0] = { MTK_DISP_OVLSYS_ADAPTOR, 0, &ddp_ovlsys_adaptor},
	[DDP_COMPONENT_DRM_OVLSYS_ADAPTOR1] = { MTK_DISP_OVLSYS_ADAPTOR, 1, &ddp_ovlsys_adaptor},
	[DDP_COMPONENT_DRM_OVLSYS_ADAPTOR2] = { MTK_DISP_OVLSYS_ADAPTOR, 2, &ddp_ovlsys_adaptor},
	[DDP_COMPONENT_DSC0]		= { MTK_DISP_DSC,		0, &ddp_dsc },
	[DDP_COMPONENT_DSC1]		= { MTK_DISP_DSC,		1, &ddp_dsc },
	[DDP_COMPONENT_DSI0]		= { MTK_DSI,			0, &ddp_dsi },
	[DDP_COMPONENT_DSI1]		= { MTK_DSI,			1, &ddp_dsi },
	[DDP_COMPONENT_DSI2]		= { MTK_DSI,			2, &ddp_dsi },
	[DDP_COMPONENT_DSI3]		= { MTK_DSI,			3, &ddp_dsi },
	[DDP_COMPONENT_DVO0]            = { MTK_DVO,                    0, &ddp_dvo },
	[DDP_COMPONENT_DVO1]            = { MTK_DVO,                    1, &ddp_dvo },
	[DDP_COMPONENT_GAMMA]		= { MTK_DISP_GAMMA,		0, &ddp_gamma },
	[DDP_COMPONENT_GAMMA1]		= { MTK_DISP_GAMMA,		1, &ddp_gamma },
	[DDP_COMPONENT_MDP_RSZ0]	= { MTK_DISP_MDP_RSZ,		0, &ddp_mdp_rsz},
	[DDP_COMPONENT_MERGE0]		= { MTK_DISP_MERGE,		0, &ddp_merge },
	[DDP_COMPONENT_MERGE1]		= { MTK_DISP_MERGE,		1, &ddp_merge },
	[DDP_COMPONENT_MERGE2]		= { MTK_DISP_MERGE,		2, &ddp_merge },
	[DDP_COMPONENT_MERGE3]		= { MTK_DISP_MERGE,		3, &ddp_merge },
	[DDP_COMPONENT_MERGE4]		= { MTK_DISP_MERGE,		4, &ddp_merge },
	[DDP_COMPONENT_MERGE5]		= { MTK_DISP_MERGE,		5, &ddp_merge },
	[DDP_COMPONENT_OD0]		= { MTK_DISP_OD,		0, &ddp_od },
	[DDP_COMPONENT_OD1]		= { MTK_DISP_OD,		1, &ddp_od },
	[DDP_COMPONENT_OVL0]		= { MTK_DISP_OVL,		0, &ddp_ovl },
	[DDP_COMPONENT_OVL0_DLO_ASYNC5] = { MTK_DISP_VIRTUAL,           -1, NULL },
	[DDP_COMPONENT_OVL0_DLO_ASYNC6] = { MTK_DISP_VIRTUAL,           -1, NULL },
	[DDP_COMPONENT_OVL1]		= { MTK_DISP_OVL,		1, &ddp_ovl },
	[DDP_COMPONENT_OVL1_DLO_ASYNC5] = { MTK_DISP_VIRTUAL,           -1, NULL },
	[DDP_COMPONENT_OVL_2L0]		= { MTK_DISP_OVL_2L,		0, &ddp_ovl },
	[DDP_COMPONENT_OVL_2L1]		= { MTK_DISP_OVL_2L,		1, &ddp_ovl },
	[DDP_COMPONENT_OVL_2L2]		= { MTK_DISP_OVL_2L,		2, &ddp_ovl },
	[DDP_COMPONENT_POSTMASK0]	= { MTK_DISP_POSTMASK,		0, &ddp_postmask },
	[DDP_COMPONENT_PWM0]		= { MTK_DISP_PWM,		0, NULL },
	[DDP_COMPONENT_PWM1]		= { MTK_DISP_PWM,		1, NULL },
	[DDP_COMPONENT_PWM2]		= { MTK_DISP_PWM,		2, NULL },
	[DDP_COMPONENT_RDMA0]		= { MTK_DISP_RDMA,		0, &ddp_rdma },
	[DDP_COMPONENT_RDMA1]		= { MTK_DISP_RDMA,		1, &ddp_rdma },
	[DDP_COMPONENT_RDMA2]		= { MTK_DISP_RDMA,		2, &ddp_rdma },
	[DDP_COMPONENT_RDMA4]		= { MTK_DISP_RDMA,		4, &ddp_rdma },
	[DDP_COMPONENT_SPLITTER0]	= { MTK_DISP_SPLITTER,		0, &ddp_splitter },
	[DDP_COMPONENT_TDSHP0]		= { MTK_DISP_TDSHP,		0, &ddp_tdshp },
	[DDP_COMPONENT_UFOE]		= { MTK_DISP_UFOE,		0, &ddp_ufoe },
	[DDP_COMPONENT_WDMA0]		= { MTK_DISP_WDMA,		0, NULL },
	[DDP_COMPONENT_WDMA1]		= { MTK_DISP_WDMA,		1, NULL },
	[DDP_COMPONENT_COMP0_OUT_CB0]	= { MTK_DISP_VIRTUAL,		-1, NULL },
	[DDP_COMPONENT_COMP0_OUT_CB1]	= { MTK_DISP_VIRTUAL,		-1, NULL },
	[DDP_COMPONENT_COMP0_OUT_CB2]	= { MTK_DISP_VIRTUAL,		-1, NULL },
	[DDP_COMPONENT_COMP0_OUT_CB3]	= { MTK_DISP_VIRTUAL,		-1, NULL },
	[DDP_COMPONENT_COMP0_OUT_CB4]	= { MTK_DISP_VIRTUAL,		-1, NULL },
	[DDP_COMPONENT_COMP0_OUT_CB5]	= { MTK_DISP_VIRTUAL,		-1, NULL },
};

static void mtk_ddp_comp_override_dpi_v2_funcs(struct mtk_ddp_comp *ddp_comp)
{
	if (mtk_ddp_matches[ddp_comp->id].type != MTK_DP_INTF)
		return;

	/* Overriding v2 funcs for mtk_dpi_v2 */
	ddp_comp->funcs = &ddp_dpi_v2;
}

static bool mtk_ddp_comp_find(struct device *dev,
			      const unsigned int *path,
			      unsigned int path_len,
			      struct mtk_ddp_comp *ddp_comp)
{
	unsigned int i;

	if (path == NULL)
		return false;

	for (i = 0U; i < path_len; i++) {
		if (mtk_dpi_v2)
			mtk_ddp_comp_override_dpi_v2_funcs(&ddp_comp[path[i]]);

		if (dev == ddp_comp[path[i]].dev)
			return true;
	}

	return false;
}

static int mtk_ddp_comp_find_in_route(struct device *dev,
				      const struct mtk_drm_route *routes,
				      unsigned int num_routes,
				      struct mtk_ddp_comp *ddp_comp)
{
	unsigned int i;

	if (!routes)
		return -EINVAL;

	for (i = 0; i < num_routes; i++) {
		if (mtk_dpi_v2)
			mtk_ddp_comp_override_dpi_v2_funcs(&ddp_comp[routes[i].route_ddp]);

		if (dev == ddp_comp[routes[i].route_ddp].dev)
			return BIT(routes[i].crtc_id);
	}

	return -ENODEV;
}

int mtk_ddp_comp_get_id(struct device_node *node,
			enum mtk_ddp_comp_type comp_type)
{
	int id = of_alias_get_id(node, mtk_ddp_comp_stem[comp_type]);
	int i;

	for (i = 0; i < ARRAY_SIZE(mtk_ddp_matches); i++) {
		if (comp_type == mtk_ddp_matches[i].type &&
		    (id < 0 || id == mtk_ddp_matches[i].alias_id))
			return i;
	}

	return -EINVAL;
}

enum mtk_ddp_comp_type mtk_ddp_comp_get_type(unsigned int comp_id)
{
	return mtk_ddp_matches[comp_id].type;
}

int mtk_find_possible_crtcs(struct drm_device *drm, struct device *dev)
{
	struct mtk_drm_private *private = drm->dev_private;
	int ret;

	if (mtk_ddp_comp_find(dev,
			      private->data->main_path,
			      private->data->main_len,
			      private->ddp_comp))
		ret = BIT(0);
	else if (mtk_ddp_comp_find(dev,
				   private->data->ext_path,
				   private->data->ext_len,
				   private->ddp_comp))
		ret = BIT(1);
	else if (mtk_ddp_comp_find(dev,
				   private->data->third_path,
				   private->data->third_len,
				   private->ddp_comp))
		ret = BIT(2);
	else
		ret = mtk_ddp_comp_find_in_route(dev,
						 private->data->conn_routes,
						 private->data->num_conn_routes,
						 private->ddp_comp);

	return ret;
}

static void mtk_ddp_comp_clk_put(void *_clk)
{
	struct clk *clk = _clk;

	clk_put(clk);
}

int mtk_ddp_comp_init(struct device *dev, struct device_node *node, struct mtk_ddp_comp *comp,
		      unsigned int comp_id)
{
	struct platform_device *comp_pdev;
	enum mtk_ddp_comp_type type;
	struct mtk_ddp_comp_dev *priv;
	int ret;

	if (comp_id >= DDP_COMPONENT_DRM_ID_MAX)
		return -EINVAL;

	type = mtk_ddp_matches[comp_id].type;

	comp->id = comp_id;
	comp->funcs = mtk_ddp_matches[comp_id].funcs;
	/* Not all drm components have a DTS device node, such as ovl_adaptor,
	 * which is the drm bring up sub driver
	 */
	if (!node)
		return 0;

	comp_pdev = of_find_device_by_node(node);
	if (!comp_pdev) {
		DRM_INFO("Waiting for device %s\n", node->full_name);
		return -EPROBE_DEFER;
	}
	comp->dev = &comp_pdev->dev;

	if (type == MTK_DISP_AAL ||
	    type == MTK_DISP_BLS ||
	    type == MTK_DISP_CCORR ||
	    type == MTK_DISP_COLOR ||
	    type == MTK_DISP_DITHER ||
	    type == MTK_DISP_DSC ||
	    type == MTK_DISP_GAMMA ||
	    type == MTK_DISP_MERGE ||
	    type == MTK_DISP_OVL ||
	    type == MTK_DISP_OVL_2L ||
	    type == MTK_DISP_PWM ||
	    type == MTK_DISP_RDMA ||
	    type == MTK_DISP_SPLITTER ||
	    type == MTK_DPI ||
	    type == MTK_DP_INTF ||
	    type == MTK_DSI ||
	    type == MTK_DVO)
		return 0;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->regs = devm_of_iomap(dev, node, 0, NULL);
	if (IS_ERR(priv->regs))
		return PTR_ERR(priv->regs);

	priv->clk = of_clk_get(node, 0);
	if (IS_ERR(priv->clk))
		return PTR_ERR(priv->clk);

	ret = devm_add_action_or_reset(dev, mtk_ddp_comp_clk_put, priv->clk);
	if (ret)
		return ret;

#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	ret = cmdq_dev_get_client_reg(comp->dev, &priv->cmdq_reg, 0);
	if (ret)
		dev_dbg(comp->dev, "get mediatek,gce-client-reg fail!\n");
#endif

	platform_set_drvdata(comp_pdev, priv);

	return 0;
}
