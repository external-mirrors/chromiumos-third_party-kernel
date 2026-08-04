# SPDX-License-Identifier: GPL-2.0

ccflags-$(CONFIG_DRM_MEDIATEK_DEBUG) := -DDEBUG

mediatek-drm-y := mtk_crtc.o \
		  mtk_ddp_comp.o \
		  mtk_disp_aal.o \
		  mtk_disp_blender.o \
		  mtk_disp_ccorr.o \
		  mtk_disp_color.o \
		  mtk_disp_dither.o \
		  mtk_disp_exdma.o \
		  mtk_disp_gamma.o \
		  mtk_disp_merge.o \
		  mtk_disp_ovlsys_adaptor.o \
		  mtk_disp_ovl.o \
		  mtk_disp_outproc.o \
		  mtk_disp_ovl_adaptor.o \
		  mtk_disp_pmqos.o \
		  mtk_disp_rdma.o \
		  mtk_disp_splitter.o \
		  mtk_drm_drv.o \
		  mtk_dsi.o \
		  mtk_dp_dsc.o \
		  mtk_dpi.o \
		  mtk_dpi_v2.o \
		  mtk_dvo.o \
		  mtk_ethdr.o \
		  mtk_gem.o \
		  mtk_mdp_rdma.o \
		  mtk_padding.o \
		  mtk_plane.o

obj-$(CONFIG_DRM_MEDIATEK) += mediatek-drm.o

mediatek-drm-hdmi-objs := mtk_cec.o \
			  mtk_hdmi.o \
			  mtk_hdmi_ddc.o

obj-$(CONFIG_DRM_MEDIATEK_HDMI) += mediatek-drm-hdmi.o

obj-$(CONFIG_DRM_MEDIATEK_DP) += mtk_dp.o mtk-dp-v2.o
mtk-dp-v2-objs := mtk_dp_v2.o \
		  mtk_dp_mst.o \
		  tlc_dp_hdcp.o \
		  mtk_dp_hdcp1x.o \
		  mtk_dp_hdcp2.o
