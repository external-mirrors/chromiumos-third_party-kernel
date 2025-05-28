// SPDX-License-Identifier: GPL-2.0

#include <linux/clk.h>
#include <linux/iopoll.h>
#include <linux/list.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/pm_opp.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <soc/mediatek/mmdvfs_v3.h>
#include <soc/mediatek/mtk-interconnect.h>
#include <soc/mediatek/smi.h>

#include "../common/mtk_vcodec_util.h"
#include "mtk_vcodec_dec_hw.h"
#include "mtk_vcodec_dec_dvfs.h"
#include "mtk_vcodec_dec_pm.h"
#include "vdec_drv_if.h"

#define VDEC_MMPC_POLL_DELAY_US 5
#define VDEC_MMPC_POLL_TIMEOUT 10000

static
struct mtk_vcodec_dec_dvfs *mtk_vdec_get_dvfs(struct mtk_vcodec_dec_dev *main_dev, int hw_idx)
{
	struct mtk_vdec_hw_dev *subdev_dev;

	subdev_dev = mtk_vcodec_get_hw_dev(main_dev, hw_idx);
	if (!subdev_dev)
		return NULL;

	return &subdev_dev->vdec_dvfs;
}

int mtk_prepare_vdec_dvfs(struct mtk_vcodec_dec_dvfs *vdec_dvfs)
{
	struct device *dev = &vdec_dvfs->sub_dev->plat_dev->dev;
	struct dev_pm_opp *opp = 0;
	unsigned long freq = 0;
	int i = 0, ret;

	INIT_LIST_HEAD(&vdec_dvfs->vdec_dvfs_inst);
	ret = mtk_vcodec_dvfs_tbl_init(vdec_dvfs);
	if (ret < 0)
		return ret;

	ret = dev_pm_opp_of_add_table(dev);
	if (ret < 0) {
		vdec_dvfs->vdec_reg = 0;
		mtk_dvfs_err(dev, "failed to get opp table (%d)", ret);
		return ret;
	}

	vdec_dvfs->vdec_reg = devm_regulator_get_optional(dev, "mmdvfs-dvfsrc-vcore");
	if (IS_ERR_OR_NULL(vdec_dvfs->vdec_reg)) {
		mtk_dvfs_err(dev, "failed to get regulator");
		vdec_dvfs->vdec_reg = 0;
		vdec_dvfs->vdec_mmdvfs_clk = devm_clk_get(dev, "mmdvfs_clk");
		if (IS_ERR_OR_NULL(vdec_dvfs->vdec_mmdvfs_clk)) {
			mtk_dvfs_err(dev, "failed to get mmdvfs_clk.");
			vdec_dvfs->vdec_mmdvfs_clk = 0;

			return -EINVAL;
		} else {
			mtk_dvfs_debug(dev, 4, "get vdec_mmdvfs_clk successfully");
		}
	} else {
		mtk_dvfs_debug(dev, 4, "get regulator successfully");
	}

	/* Not support the highest frequency. */
	vdec_dvfs->vdec_freq_cnt = dev_pm_opp_get_opp_count(dev);
	if (vdec_dvfs->vdec_freq_cnt <= 0 || vdec_dvfs->vdec_freq_cnt > VDEC_OPP_NUM) {
		mtk_dvfs_err(dev, "opp freqs cnt is error value %d %d.", vdec_dvfs->vdec_freq_cnt,
			     VDEC_OPP_NUM);

		return -EINVAL;
	} else {
		mtk_dvfs_debug(dev, 1, "dev->vdec_freq_cnt:%d", vdec_dvfs->vdec_freq_cnt);
	}

	for (i = 0; i < vdec_dvfs->vdec_freq_cnt; i++) {
		opp = dev_pm_opp_find_freq_ceil(dev, &freq);
		if (IS_ERR(opp)) {
			vdec_dvfs->vdec_freqs[i] = vdec_dvfs->vdec_dvfs_params.min_freq;
			mtk_dvfs_err(dev, "find freq ceil fail: %ld", vdec_dvfs->vdec_freqs[i]);
			ret = -EINVAL;
			break;
		}

		vdec_dvfs->vdec_freqs[i] = freq;
		dev_pm_opp_put(opp);

		mtk_dvfs_debug(dev, 1, "vdec_freqs[%d]:%ld", i, vdec_dvfs->vdec_freqs[i]);
		freq++;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(mtk_prepare_vdec_dvfs);

void mtk_unprepare_vdec_dvfs(struct mtk_vcodec_dec_dvfs *vdec_dvfs)
{
	/* Set to lowest clock before leaving */
	mtk_vcodec_dvfs_tbl_deinit(vdec_dvfs);
}

static void set_vdec_opp(struct mtk_vcodec_dec_dvfs *vdec_dvfs, unsigned long freq)
{
	struct device *dev = &vdec_dvfs->sub_dev->plat_dev->dev;
	unsigned long freq_64 = freq;
	struct dev_pm_opp *opp;
	int volt = 0;
	int ret;

	opp = dev_pm_opp_find_freq_ceil(dev, &freq_64);
	if (IS_ERR(opp)) {
		mtk_dvfs_err(dev, "Failed to get dev_pm_opp");
		return;
	}

	volt = dev_pm_opp_get_voltage(opp);
	dev_pm_opp_put(opp);

	if (vdec_dvfs->vdec_mmdvfs_clk) {
		mtk_mmdvfs_enable_vcp(true, VCP_PWR_USR_VDEC);
		ret = clk_set_rate(vdec_dvfs->vdec_mmdvfs_clk, freq_64);
		if (ret)
			mtk_dvfs_err(dev, "failed to set mmdvfs rate %lu\n", freq_64);
		mtk_mmdvfs_enable_vcp(false, VCP_PWR_USR_VDEC);

		mtk_dvfs_debug(dev, 8, "freq %lu, find_freq %lu", freq, freq_64);
	} else if (vdec_dvfs->vdec_reg) {
		ret = regulator_set_voltage(vdec_dvfs->vdec_reg, volt, INT_MAX);
		if (ret)
			mtk_dvfs_err(dev, "failed to set regulator voltage %d\n", volt);

		mtk_dvfs_debug(dev, 8, "freq %lu, voltage %d", freq, volt);
	}
}

static void vcodec_pmqos_get_target_bw(struct mtk_vcodec_dec_dvfs *vdec_dvfs, u64 *target_bw_r,
				       u64 *target_bw_w, int id)
{
	u64 target_freq = vdec_dvfs->vdec_dvfs_params.target_freq;
	u64 total_bw;

	if (list_empty(&vdec_dvfs->vdec_dvfs_inst)) {
		*target_bw_r = 0;
		*target_bw_w = 0;

		return;
	}

	total_bw = (u64)vdec_dvfs->vdec_larb_bw[id].bw_r * target_freq;
	*target_bw_r = div_u64(total_bw, vdec_dvfs->vdec_dvfs_params.min_freq);

	total_bw = (u64)vdec_dvfs->vdec_larb_bw[id].bw_w * target_freq;
	*target_bw_w = div_u64(total_bw, vdec_dvfs->vdec_dvfs_params.min_freq);
}

void vcodec_pmqos_helper_update(struct mtk_vcodec_dec_ctx *ctx, int hw_idx)
{
	struct mtk_vcodec_dec_dvfs *vdec_dvfs = mtk_vdec_get_dvfs(ctx->dev, hw_idx);
	struct device *dev = &vdec_dvfs->sub_dev->plat_dev->dev;
	u64 bw_r, bw_w, icc_path_r_id, icc_path_w_id;
	u32 i, larb_id, icc_couont = 0;
	struct icc_path **icc_path = vdec_dvfs->vdec_qos_req;

	for (i = 0; i < vdec_dvfs->vdec_larb_cnt; i++) {
		if (hw_idx != vdec_dvfs->vdec_larb_bw[i].hw_id)
			continue;

		larb_id = vdec_dvfs->vdec_larb_bw[i].larb_id;
		icc_path_r_id = icc_couont * 2;
		icc_path_w_id = icc_couont * 2 + 1;

		vcodec_pmqos_get_target_bw(vdec_dvfs, &bw_r, &bw_w, i);

		mtk_icc_set_bw(icc_path[icc_path_r_id], MBps_to_icc((u32)bw_r), 0);
		mtk_icc_set_bw(icc_path[icc_path_w_id], MBps_to_icc((u32)bw_w), 0);

		mtk_dvfs_debug(dev, 5, "hw_id:%d larb %d bw %llu %llu MB/s",
			       hw_idx, larb_id, bw_r, bw_w);
		icc_couont++;
	}
}
EXPORT_SYMBOL_GPL(vcodec_pmqos_helper_update);

static void vcodec_dvfs_helper_update_freq(struct mtk_vcodec_dec_ctx *ctx, int hw_id)
{
	struct mtk_vcodec_dec_dvfs *vdec_dvfs = mtk_vdec_get_dvfs(ctx->dev, hw_id);
	struct device *dev;

	dev = &vdec_dvfs->sub_dev->plat_dev->dev;
	if (!mtk_vcodec_dvfs_update_picinfo(ctx, vdec_dvfs))
		return;

	mtk_vcodec_dvfs_update_freq(vdec_dvfs, false);
	if (vdec_dvfs->vdec_dvfs_params.target_freq == vdec_dvfs->vdec_dvfs_params.work_freq)
		return;

	vdec_dvfs->vdec_dvfs_params.work_freq = vdec_dvfs->vdec_dvfs_params.target_freq;
}

static void vcodec_dvfs_helper_remove_freq(struct mtk_vcodec_dec_ctx *ctx, int hw_id)
{
	struct mtk_vcodec_dec_dvfs *vdec_dvfs = mtk_vdec_get_dvfs(ctx->dev, hw_id);
	struct device *dev;

	dev = &vdec_dvfs->sub_dev->plat_dev->dev;
	if (!mtk_vcodec_dvfs_remove_picinfo(ctx, vdec_dvfs))
		return;

	mtk_vcodec_dvfs_update_freq(vdec_dvfs, true);
	if (vdec_dvfs->vdec_dvfs_params.target_freq == vdec_dvfs->vdec_dvfs_params.work_freq)
		return;

	vdec_dvfs->vdec_dvfs_params.work_freq = vdec_dvfs->vdec_dvfs_params.target_freq;
}

void vcodec_dvfs_helper_set_opp(struct mtk_vcodec_dec_ctx *ctx, int hw_id)
{
	struct mtk_vcodec_dec_dvfs *vdec_dvfs = mtk_vdec_get_dvfs(ctx->dev, hw_id);

	set_vdec_opp(vdec_dvfs, vdec_dvfs->vdec_dvfs_params.work_freq);
}
EXPORT_SYMBOL_GPL(vcodec_dvfs_helper_set_opp);

void mtk_vdec_dvfs_begin_inst(struct mtk_vcodec_dec_ctx *ctx, int hw_id)
{
	struct mtk_vcodec_dec_dvfs *vdec_dvfs = mtk_vdec_get_dvfs(ctx->dev, hw_id);
	struct device *dev;

	if (!vdec_dvfs) {
		mtk_dvfs_err(dev, "can not get dvfs: %d", hw_id);
		return;
	}

	dev = &vdec_dvfs->sub_dev->plat_dev->dev;
	vcodec_dvfs_helper_update_freq(ctx, hw_id);
}
EXPORT_SYMBOL_GPL(mtk_vdec_dvfs_begin_inst);

void mtk_vdec_dvfs_end_inst(struct mtk_vcodec_dec_ctx *ctx, int hw_id)
{
	struct mtk_vcodec_dec_dvfs *vdec_dvfs = mtk_vdec_get_dvfs(ctx->dev, hw_id);
	struct device *dev = &vdec_dvfs->sub_dev->plat_dev->dev;

	if (!vdec_dvfs) {
		mtk_dvfs_err(dev, "can not get dvfs: %d", hw_id);
		return;
	}

	dev = &vdec_dvfs->sub_dev->plat_dev->dev;
	vcodec_dvfs_helper_remove_freq(ctx, hw_id);
}
EXPORT_SYMBOL_GPL(mtk_vdec_dvfs_end_inst);

void mtk_vdec_mmpc_update(struct mtk_vcodec_dec_ctx *ctx, int hw_id)
{
	struct mtk_vcodec_dec_dvfs *vdec_dvfs = mtk_vdec_get_dvfs(ctx->dev, hw_id);
	void __iomem *xpc_addr = vdec_dvfs->sub_dev->reg_base[VDEC_HW_XPC];
	struct device *dev = &vdec_dvfs->sub_dev->plat_dev->dev;
	struct vcodec_pc_bw *larb_bw = vdec_dvfs->vdec_pc_bw;
	unsigned int read_val, set_ctrl, done_offset;
	unsigned long read_pc, write_pc, total_pc, min_freq, freq;
	void __iomem *target_addr;
	int i, ret;

	freq = vdec_dvfs->vdec_dvfs_params.work_freq;
	if (!freq) {
		freq = vdec_dvfs->vdec_dvfs_params.min_freq;
		mtk_dvfs_err(dev, "hw_id: %d freq is zero", hw_id);
	}

	for (i = 0; i < VDEC_LARB_HW_NUM; i++) {
		if (hw_id != vdec_dvfs->vdec_pc_bw[i].hw_id)
			continue;

		read_pc = vdec_dvfs->vdec_pc_bw[i].base.r;
		write_pc = vdec_dvfs->vdec_pc_bw[i].base.w;

		min_freq = vdec_dvfs->vdec_dvfs_params.min_freq;
		vdec_dvfs->vdec_pc_bw[i].target.r = read_pc * freq / min_freq;
		vdec_dvfs->vdec_pc_bw[i].target.w = write_pc * freq / min_freq;
	}

	done_offset = (hw_id == MTK_VDEC_CORE) ? 0x400000 : 0x2000000;
	set_ctrl = (hw_id == MTK_VDEC_CORE) ? VDEC_MMPC_BW_CTRL_CORE : VDEC_MMPC_BW_CTRL_LAT;

	for (i = 0; i < VDEC_LARB_HW_NUM; i++) {
		if (hw_id != larb_bw[i].hw_id)
			continue;

		target_addr = xpc_addr + larb_bw[i].reg_offset;
		total_pc = (larb_bw[i].target.w << 16) + larb_bw[i].target.r;

		writel(total_pc, target_addr);
		mtk_dvfs_debug(dev, 9, "hw:%d %d 0x%lx write_val:0x%lx read_val:0x%x\n",
			       hw_id, i, (unsigned long)target_addr, total_pc,
			       readl(target_addr));
	}

	writel(0x1, xpc_addr + set_ctrl);
	ret = readl_poll_timeout(xpc_addr + VDEC_MMPC_BW_SET_DONE, read_val,
				 !(read_val & done_offset),
				 VDEC_MMPC_POLL_DELAY_US,
				 VDEC_MMPC_POLL_TIMEOUT);
	if (ret)
		mtk_dvfs_err(dev, "polling mmpc %d BW timeout\n", hw_id);
}
EXPORT_SYMBOL_GPL(mtk_vdec_mmpc_update);

static void fill_dvfs_params(struct mtk_vcodec_dec_dev *main_dev, int hw_idx, uint32_t *data)
{
	struct platform_device *pdev;
	struct mtk_vdec_hw_dev *subdev_dev;
	struct mtk_vcodec_dec_dvfs *vdec_dvfs;
	u32 *share_data = data;
	int data_count = 12, i;

	subdev_dev = mtk_vcodec_get_hw_dev(main_dev, hw_idx);
	if (!subdev_dev)
		return;

	vdec_dvfs = &subdev_dev->vdec_dvfs;
	if (vdec_dvfs->dvfs_params_updated)
		return;

	if (!vdec_dvfs->vdec_tput)
		return;

	pdev = vdec_dvfs->sub_dev->plat_dev;
	for (i = 0; i < vdec_dvfs->vdec_tput_cnt; i++) {
		vdec_dvfs->vdec_tput[i].codec_type = MTK_INST_DECODER;
		vdec_dvfs->vdec_tput[i].codec_fmt = share_data[data_count];
		vdec_dvfs->vdec_tput[i].config = share_data[data_count + 1];
		vdec_dvfs->vdec_tput[i].cy_per_mb_1 = share_data[data_count + 2];
		vdec_dvfs->vdec_tput[i].cy_per_mb_2 = share_data[data_count + 3];
		data_count += 4;
	}

	if (!vdec_dvfs->vdec_pc_bw)
		return;

	for (i = 0; i < VDEC_LARB_HW_NUM; i++) {
		vdec_dvfs->vdec_pc_bw[i].hw_id = share_data[data_count];
		vdec_dvfs->vdec_pc_bw[i].base.r = share_data[data_count + 1];
		vdec_dvfs->vdec_pc_bw[i].base.w = share_data[data_count + 2];
		vdec_dvfs->vdec_pc_bw[i].reg_offset = share_data[data_count + 3];
		data_count += 4;
	}

	for (i = 0; i < vdec_dvfs->vdec_larb_cnt; i++) {
		vdec_dvfs->vdec_larb_bw[i].hw_id = share_data[data_count];
		vdec_dvfs->vdec_larb_bw[i].bw_r = share_data[data_count + 1];
		vdec_dvfs->vdec_larb_bw[i].bw_w = share_data[data_count + 2];
		vdec_dvfs->vdec_larb_bw[i].larb_id = share_data[data_count + 3];
		data_count += 4;
	}

	for (i = 0; i < vdec_dvfs->vdec_tput_cnt; i++)
		mtk_dvfs_debug(&pdev->dev, 3, "tput[%d] fmt 0x%x, cfg %d, cy1 %u, cy2 %u", i,
			       vdec_dvfs->vdec_tput[i].codec_fmt,
			       vdec_dvfs->vdec_tput[i].config,
			       vdec_dvfs->vdec_tput[i].cy_per_mb_1,
			       vdec_dvfs->vdec_tput[i].cy_per_mb_2);

	for (i = 0; i < VDEC_LARB_HW_NUM; i++)
		mtk_dvfs_debug(&pdev->dev, 3, "pc bw[%d]: hw_id %u r %d w %u offset %u", i,
			       vdec_dvfs->vdec_pc_bw[i].hw_id,
			       vdec_dvfs->vdec_pc_bw[i].base.r,
			       vdec_dvfs->vdec_pc_bw[i].base.w,
			       vdec_dvfs->vdec_pc_bw[i].reg_offset);

	for (i = 0; i < vdec_dvfs->vdec_larb_cnt; i++)
		mtk_dvfs_debug(&pdev->dev, 3, "larb bw[%d]: hw_id %d bw_r %u bw_w %d, larb_id %u",
			       i, vdec_dvfs->vdec_larb_bw[i].hw_id,
			       vdec_dvfs->vdec_larb_bw[i].bw_r,
			       vdec_dvfs->vdec_larb_bw[i].bw_w,
			       vdec_dvfs->vdec_larb_bw[i].larb_id);

	vdec_dvfs->dvfs_params_updated = true;
}

void mtk_vdec_fill_dvfs_params(struct mtk_vcodec_dec_dev *main_dev, uint32_t *data)
{
	fill_dvfs_params(main_dev, MTK_VDEC_CORE, data);
	fill_dvfs_params(main_dev, MTK_VDEC_LAT0, data);
}
EXPORT_SYMBOL_GPL(mtk_vdec_fill_dvfs_params);
