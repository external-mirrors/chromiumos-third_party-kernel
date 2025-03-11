// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 MediaTek Inc.
 */
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/firmware.h>
#include <linux/kernel.h>

#include "mtk_vcp_reg.h"
#include "mtk_vcp_rproc.h"

#define VCM_IMAGE_MAGIC             (0x58881688)
#define VCM_IMAGE_NAME_MAXSZ        (32)
#define VCP_IMAGE_HEADER_SIZE       (0x200)

#define VCP_LOADER_OFFSET           (0x0)
#define VCP_LOADER_SIZE             (0x2000)
#define VCP_FW_OFFSET               (0x2000)
#define VCP_DRAM_OFFSET             (0x200000)
#define VCP_DRAM_IMG_OFFSET         (VCP_DRAM_OFFSET)
#define MMUP_DRAM_IMG_OFFSET        (0x1200000)
#define MMUP_SRAM_OFFSET            (0x31000)

#define REGION_OFFSET               (0x4)
#define ALIGN_1024                  (1024)
#define ALIGN_16                    (16)
#define VCP_HFRP_SECTION_NAME       "tinysys-vcp-RV55_A"
#define VCP_MMUP_SECTION_NAME       "tinysys-mmup-RV33_A"
#define VCP_HFRP_DRAM_SECTION_NAME  "tinysys-vcp-RV55_A_dram"
#define VCP_MMUP_DRAM_SECTION_NAME  "tinysys-mmup-RV33_A_dram"

struct mkimg_hdr {
	u32 magic;
	u32 dsz;
	char name[VCM_IMAGE_NAME_MAXSZ];
};

static size_t loader_partition(void __iomem *image_buf,
			       const u8 *fw_src,
			       size_t size,
			       const char *section_name)
{
	const u8 *fw_ptr = fw_src;
	u32 offset = 0;
	u32 align_size = 0;
	const struct mkimg_hdr *img_hdr_info;

	if (!fw_src || !image_buf || size < VCP_IMAGE_HEADER_SIZE)
		return 0;

	while (offset < size) {
		img_hdr_info = (const struct mkimg_hdr *)(fw_ptr + offset);
		align_size = round_up(img_hdr_info->dsz, ALIGN_16);
		offset += VCP_IMAGE_HEADER_SIZE;
		if (img_hdr_info->magic != VCM_IMAGE_MAGIC ||
			strncmp(img_hdr_info->name,
				section_name, VCM_IMAGE_NAME_MAXSZ - 1) != 0)
			offset += align_size;
		else {
			memcpy_toio(image_buf, fw_ptr + offset, img_hdr_info->dsz);
			offset += align_size;
			return img_hdr_info->dsz;
		}
	}

	return 0;
}

static int load_vcp_bin(const u8 *fw_src,
			size_t size,
			void __iomem *img_buf_va,
			phys_addr_t img_buf_pa,
			dma_addr_t img_buf_iova,
			struct mtk_vcp_device *vcp)
{
	void __iomem *ld_ptr;
	void __iomem *dram_img_ptr;
	void __iomem *dram_img_backup_ptr;
	phys_addr_t ld_ptr_phy;
	phys_addr_t dram_img_ptr_phy;
	u32 fw_size;
	u32 ld_size;
	u32 dram_img_size;
	u32 dram_img_offset = 0;
	u32 dram_backup_img_offset = 0;
	struct vcp_region_info_st vcp_region_info = {};
	struct arm_smccc_res res;

	/*
	 * Layout of VCP reserved DRAM memory.
	 ************************
	 * +------------------+ *
	 * |  VCP Loader      | *
	 * +------------------+ *
	 * |  VCP Firmware    | *
	 * +------------------+ *
	 ************************
	 * +------------------+ *
	 * |  VCP DRAM        | *
	 * +------------------+ *
	 * |  VCP DRAM backup | *
	 * +------------------+ *
	 ************************
	 */

	/* step 2: load/verify firmware */
	ld_ptr = img_buf_va;
	ld_ptr_phy = img_buf_pa;
	ld_size = VCP_LOADER_SIZE;
	fw_size = loader_partition(img_buf_va, fw_src, size, VCP_HFRP_SECTION_NAME);
	if (!fw_size) {
		dev_err(vcp->dev, "load %s failed\n", VCP_HFRP_SECTION_NAME);
		return -EINVAL;
	}

	/* step 3: load/verify vcp dram section binary */
	dram_img_size = loader_partition(img_buf_va + VCP_DRAM_OFFSET,
					 fw_src, size, VCP_HFRP_DRAM_SECTION_NAME);
	if (!dram_img_size) {
		dev_err(vcp->dev, "load %s failed\n", VCP_HFRP_DRAM_SECTION_NAME);
		return -EINVAL;
	}

	dram_img_ptr = img_buf_va + VCP_DRAM_OFFSET;
	dram_img_ptr_phy = img_buf_pa + VCP_DRAM_OFFSET;
	dram_img_backup_ptr = dram_img_ptr + round_up(dram_img_size, ALIGN_1024);

	/* copy loader and fw to sram */
	memcpy_toio(vcp->vcp_cluster->sram_base, (void *)img_buf_va, fw_size);

	/* Let vcp check if the struct matches the one in tinysys */
	vcp_region_info.struct_size = sizeof(struct vcp_region_info_st);

	/* keep *_addr[35:4] in region info for region info accommodate 32 bits data only */
	vcp_region_info.ap_loader_start_pa = (u32)img_buf_pa;
	vcp_region_info.ap_loader_start   = VCP_PACK_IOVA(img_buf_iova);
	vcp_region_info.ap_firmware_start = VCP_PACK_IOVA(img_buf_iova + ld_size);

	/* set size to 0 to prevent vcp loader load fw, because fw already load-in SRAM */
	vcp_region_info.ap_loader_size = 0;
	vcp_region_info.ap_firmware_size = 0;

	dram_img_offset = dram_img_ptr - img_buf_va;
	dram_backup_img_offset = dram_img_offset + round_up(dram_img_size, ALIGN_1024);

	vcp_region_info.ap_dram_start = VCP_PACK_IOVA(img_buf_iova + dram_img_offset);
	vcp_region_info.ap_dram_backup_start = VCP_PACK_IOVA(img_buf_iova + dram_backup_img_offset);
	vcp_region_info.ap_dram_size  = (u32)dram_img_size;

	vcp_region_info.l2tcm_offset = (u32)MMUP_SRAM_OFFSET;

	memcpy_toio(vcp->vcp_cluster->sram_base + REGION_OFFSET,
		    &vcp_region_info, sizeof(vcp_region_info));

	arm_smccc_smc(MTK_SIP_TINYSYS_VCP_CONTROL,
		      MTK_TINYSYS_MMUP_KERNEL_OP_SET_L2TCM_OFFSET,
		      MMUP_SRAM_OFFSET, 0, 0, 0, 0, 0, &res);

	return 0;
}

static int load_mmup_bin(const u8 *fw_src,
			 size_t size,
			 void __iomem *img_buf_va,
			 phys_addr_t img_buf_pa,
			 dma_addr_t img_buf_iova,
			 struct mtk_vcp_device *vcp)
{
	void __iomem *ld_ptr;
	void __iomem *dram_img_ptr;
	void __iomem *dram_img_backup_ptr;
	phys_addr_t ld_ptr_phy;
	phys_addr_t dram_img_ptr_phy;
	u32 fw_size;
	u32 ld_size;
	u32 dram_img_size;
	u32 dram_img_offset = 0;
	u32 dram_backup_img_offset = 0;
	struct vcp_region_info_st vcp_region_info = {};
	struct arm_smccc_res res;

	/*
	 * Layout of MMUP reserved DRAM memory.
	 ************************
	 * +------------------+ *
	 * |  MMUP Loader     | *
	 * +------------------+ *
	 * |  MMUP Firmware   | *
	 * +------------------+ *
	 ************************
	 * +------------------+ *
	 * |  MMUP DRAM       | *
	 * +------------------+ *
	 * |  MMUP DRAM backup| *
	 * +------------------+ *
	 ************************
	 */

	/* step 2: load/verify firmware */
	ld_ptr = img_buf_va;
	ld_ptr_phy = img_buf_pa + MMUP_SRAM_OFFSET;
	ld_size = VCP_LOADER_SIZE;
	fw_size = loader_partition(img_buf_va + MMUP_SRAM_OFFSET, fw_src, size,
				   VCP_MMUP_SECTION_NAME);
	if (!fw_size) {
		dev_err(vcp->dev, "load %s failed\n", VCP_MMUP_SECTION_NAME);
		return -EINVAL;
	}

	/* step 3: load/verify vcp dram section binary */
	dram_img_size = loader_partition(img_buf_va + MMUP_DRAM_IMG_OFFSET, fw_src, size,
					 VCP_MMUP_DRAM_SECTION_NAME);
	if (!dram_img_size) {
		dev_err(vcp->dev, "load %s failed\n", VCP_MMUP_DRAM_SECTION_NAME);
		return -EINVAL;
	}
	dram_img_ptr = img_buf_va + MMUP_DRAM_IMG_OFFSET;
	dram_img_ptr_phy = img_buf_pa + MMUP_DRAM_IMG_OFFSET;
	dram_img_backup_ptr = dram_img_ptr + round_up(dram_img_size, ALIGN_1024);

	/* copy loader and fw to sram */
	memcpy_toio(vcp->vcp_cluster->sram_base + MMUP_SRAM_OFFSET,
		    img_buf_va + MMUP_SRAM_OFFSET, fw_size);

	/* Let vcp check if the struct matches the one in tinysys */
	vcp_region_info.struct_size = sizeof(struct vcp_region_info_st);
	/* keep *_addr[35:4] in region info for region info accommodate 32 bits data only */
	vcp_region_info.ap_loader_start_pa = (u32)(img_buf_pa + MMUP_SRAM_OFFSET);
	vcp_region_info.ap_loader_start   = VCP_PACK_IOVA(img_buf_iova + MMUP_SRAM_OFFSET);
	vcp_region_info.ap_firmware_start = VCP_PACK_IOVA(img_buf_iova + MMUP_SRAM_OFFSET + ld_size);

	/* set size to 0 to prevent vcp loader load fw, because fw already load-in SRAM */
	vcp_region_info.ap_loader_size = 0;
	vcp_region_info.ap_firmware_size = 0;

	dram_img_offset = dram_img_ptr - img_buf_va;
	dram_backup_img_offset = dram_img_offset + round_up(dram_img_size, ALIGN_1024);
	vcp_region_info.ap_dram_start = VCP_PACK_IOVA(img_buf_iova + dram_img_offset);
	vcp_region_info.ap_dram_backup_start = VCP_PACK_IOVA(img_buf_iova + dram_backup_img_offset);
	vcp_region_info.ap_dram_size  = (u32)dram_img_size;

	memcpy_toio(vcp->vcp_cluster->sram_base + MMUP_SRAM_OFFSET + REGION_OFFSET,
		    &vcp_region_info, sizeof(vcp_region_info));

	arm_smccc_smc(MTK_SIP_TINYSYS_VCP_CONTROL,
		      MTK_TINYSYS_MMUP_KERNEL_OP_SET_FW_SIZE,
		      fw_size, 0, 0, 0, 0, 0, &res);

	return 0;
}

int mtk_vcp_load(struct rproc *rproc, const struct firmware *fw)
{
	struct arm_smccc_res res;
	struct mtk_vcp_device *vcp = (struct mtk_vcp_device *)rproc->priv;
	dma_addr_t img_buf_iova;
	phys_addr_t img_buf_phys;
	void __iomem *img_buf_va;
	u32 img_buf_size;
	int ret = 0;

	if (!vcp) {
		dev_err(vcp->dev, "vcp device is no exist!\n");
		return -EINVAL;
	}

	if (fw->size < VCP_IMAGE_HEADER_SIZE ||
		fw->size > vcp->data->vcp_get_mem_size(VCP_RTOS_MEM_ID)) {
		dev_err(vcp->dev, "firmware is oversize/undersize\n");
		return -EINVAL;
	}

	/* step 0: set core0/1 RSTN */
	writel(0x1, vcp->vcp_cluster->cfg_core + VCP_R_CORE0_SW_RSTN_SET);
	writel(0x1, vcp->vcp_cluster->cfg_core + VCP_R_CORE1_SW_RSTN_SET);

	/* step 1: enable sram, enable 1 block per time */
	/* No sram PD register in MMup, power on with mtcmos */
	/* Clean SRAM. */
	memset_io(vcp->vcp_cluster->sram_base, 0, vcp->vcp_cluster->sram_size);

	img_buf_iova = vcp->data->vcp_get_mem_iova(VCP_RTOS_MEM_ID);
	img_buf_phys = vcp->data->vcp_get_mem_phys(VCP_RTOS_MEM_ID);
	img_buf_va = vcp->data->vcp_get_mem_virt(VCP_RTOS_MEM_ID);
	img_buf_size = vcp->data->vcp_get_mem_size(VCP_RTOS_MEM_ID);

	arm_smccc_smc(MTK_SIP_TINYSYS_VCP_CONTROL,
		      MTK_TINYSYS_VCP_KERNEL_OP_COLD_BOOT_VCP,
		      0, 0, 0, 0, 0, 0, &res);

	ret = load_vcp_bin(fw->data, fw->size,
			   img_buf_va, img_buf_phys,
			   img_buf_iova, vcp);
	if (ret) {
		dev_err(vcp->dev, "load vcp bin failed\n");
		return -EINVAL;
	}

	ret = load_mmup_bin(fw->data, fw->size,
			    img_buf_va, img_buf_phys,
			    img_buf_iova, vcp);
	if (ret) {
		dev_err(vcp->dev, "load mmup bin failed\n");
		return -EINVAL;
	}

	return 0;
}
