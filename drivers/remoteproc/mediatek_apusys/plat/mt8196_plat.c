// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024 MediaTek Inc.
 */

#include <linux/delay.h>
#include <linux/firmware/mediatek/mtk-apu.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <linux/remoteproc/mtk_apu.h>
#include <linux/soc/mediatek/mtk_apu_pwr.h>
#include "../mtk_apu_rproc.h"

static int apu_setup_apummu(struct mtk_apu *apu)
{
	if (!(apu->platdata->flags.secure_boot)) {
		dev_err(apu->dev, "Not support in non-secure boot\n");
		return -EINVAL;
	}

	return mtk_apu_rv_smc_call(apu->dev, MTK_APUSYS_KERNEL_OP_APUSYS_RV_SETUP_APUMMU, 0);
}

static int apu_setup_devapc(struct mtk_apu *apu)
{
	return mtk_apu_rv_smc_call(apu->dev, MTK_APUSYS_KERNEL_OP_DEVAPC_INIT_RCX, 0);
}

static int apu_reset_mp(struct mtk_apu *apu)
{
	if (!(apu->platdata->flags.secure_boot)) {
		dev_err(apu->dev, "Not support in non-secure boot\n");
		return -EINVAL;
	}

	return mtk_apu_rv_smc_call(apu->dev, MTK_APUSYS_KERNEL_OP_APUSYS_RV_RESET_MP, 0);
}

static int apu_setup_boot(struct mtk_apu *apu)
{
	if (!(apu->platdata->flags.secure_boot)) {
		dev_err(apu->dev, "Not support in non-secure boot\n");
		return -EINVAL;
	}

	return mtk_apu_rv_smc_call(apu->dev, MTK_APUSYS_KERNEL_OP_APUSYS_RV_SETUP_BOOT, 0);
}

static int mt8196_rproc_start(struct mtk_apu *apu)
{
	if (!(apu->platdata->flags.secure_boot)) {
		dev_err(apu->dev, "Not support in non-secure boot\n");
		return -EINVAL;
	}

	return mtk_apu_rv_smc_call(apu->dev, MTK_APUSYS_KERNEL_OP_APUSYS_RV_START_MP, 0);
}

static int mt8196_rproc_setup(struct mtk_apu *apu)
{
	int ret;

	ret = apu_setup_devapc(apu);
	if (ret) {
		dev_err(apu->dev, "Failed to setup devapc\n");
		return ret;
	}

	ret = apu_setup_apummu(apu);
	if (ret) {
		dev_err(apu->dev, "Failed to setup apummu\n");
		return ret;
	}

	ret = apu_reset_mp(apu);
	if (ret) {
		dev_err(apu->dev, "Failed to reset mp\n");
		return ret;
	}

	ret = apu_setup_boot(apu);
	if (ret) {
		dev_err(apu->dev, "Failed to setup boot\n");
		return ret;
	}

	return ret;
}

static int mt8196_rproc_stop(struct mtk_apu *apu)
{
	if (!(apu->platdata->flags.secure_boot)) {
		dev_err(apu->dev, "Not support in non-secure boot\n");
		return -EINVAL;
	}

	return mtk_apu_rv_smc_call(apu->dev, MTK_APUSYS_KERNEL_OP_APUSYS_RV_STOP_MP, 0);
}

static int mt8196_cold_boot_power_on(struct mtk_apu *apu)
{
	int ret;
	struct device *dev = apu->dev;
	struct device *power_dev = &(apu->power_pdev)->dev;

	if (!(apu->platdata->flags.secure_boot)) {
		dev_err(dev, "Not support in non-secure boot\n");
		return -EINVAL;
	}

	ret = pm_runtime_resume_and_get(power_dev);
	if (ret < 0) {
		dev_err(dev, "Failed to power on APU, ret=%d\n", ret);
	} else {
		dev_dbg(dev, "Successfully powered on APU\n");
		ret = 0;
	}

	return ret;
}

static int mt8196_power_on_off_locked(struct mtk_apu *apu, u32 id, u32 on, u32 off)
{
	int ret = 0;
	struct device *dev = apu->dev;
	struct device *power_dev = &(apu->power_pdev)->dev;

	if (on == 1 && off == 0) {
		if (apu->is_under_lp_scp_recovery_flow)
			return -EBUSY;

		ret = pm_runtime_resume_and_get(power_dev);
		if (ret < 0)
			dev_err(dev, "%s: after power on fail id=%u, ret=%d\n", __func__, id, ret);
		dev_dbg(dev, "%s: after power on id=%u, ret=%d\n", __func__, id, ret);
	} else if (on == 0 && off == 1) {
		ret = pm_runtime_put_sync(power_dev);
		if (ret != 0)
			dev_err(dev, "%s: after power off fail id=%u, ret=%d\n", __func__, id, ret);

		dev_dbg(dev, "%s: after power off id=%u, ret=%d\n", __func__, id, ret);
	} else {
		dev_err(dev, "%s: invalid operation: id(%d), on(%d), off(%d)\n",
			__func__, id, on, off);
		ret = -EINVAL;
	}

	return ret;
}

static int mt8196_power_on_off(struct mtk_apu *apu, u32 id, u32 on, u32 off)
{
	int ret = 0;
	struct device *dev = apu->dev;
	uint32_t retry_cnt = 500, i = 0;

	for (i = 0; i < retry_cnt; i++) {
		ret = mt8196_power_on_off_locked(apu, id, on, off);

		if (ret == -EBUSY) {
			/*
			 * Retry the power on/off if the returned value is -EBUSY, because
			 * the hw semaphore might be blocked by other host or apu under lp mode
			 */
			if (i!=0 && (i%10)==0)
				dev_warn(dev, "%s: retry on(%u) off(%u)(%u/%u)\n", __func__,
					 on, off, i, retry_cnt);
			if (i < 10)
				usleep_range(200, 500);
			else if (i < 50)
				usleep_range(1000, 2000);
			else
				usleep_range(10000, 11000);
			continue;
		}
		break;
	}

	return ret;
}

static int mt8196_apu_memmap_init(struct mtk_apu *apu)
{
	struct device *dev = apu->dev;

	apu->md32_tcm = NULL;

	apu->apu_infra_hwsem = devm_ioremap(dev, 0x190b0e00, 0xff);
	if (IS_ERR((void const *)apu->apu_infra_hwsem)) {
		dev_err(dev, "%s: apu_infra_hwsem remap base fail\n", __func__);
		return -ENOMEM;
	}

	return 0;
}

static int mt8196_rproc_init(struct mtk_apu *apu)
{
	int ret;

	apu->is_under_lp_scp_recovery_flow = false;

	ret = mt8196_cold_boot_power_on(apu);
	if (ret)
		dev_err(apu->dev, "%s: call mt8196_cold_boot_power_on fail(%d)\n",
			__func__, ret);

	return ret;
}

static int mt8196_apu_resume(struct mtk_apu *apu)
{
	mutex_lock(&apu->forbid_ipi_lock);
	apu->forbid_ipi_send = false;
	mutex_unlock(&apu->forbid_ipi_lock);

	return 0;
}

static int mt8196_apu_suspend(struct mtk_apu *apu)
{
	int pwr_status = mtk_apu_get_rpc_pwr_status(apu->power_pdev) & 0x1;

	if (pwr_status) {
		// Deny any incoming IPI
		mutex_lock(&apu->forbid_ipi_lock);
		apu->forbid_ipi_send = true;
		mutex_unlock(&apu->forbid_ipi_lock);

		/*
		 * Cancel any pending delayed power-off. If we cancelled a
		 * still-pending work, the runtime PM reference it would have
		 * dropped is still held, so do the synchronous power-off here.
		 * If the work already ran (returns 0), the device is (or will
		 * shortly be) powered off and no further action is needed.
		 */
		if (cancel_delayed_work_sync(&apu->power_off_work))
			mt8196_power_on_off(apu, MTK_APU_IPI_MIDDLEWARE, 0, 1);
	}

	return 0;
}

const struct mtk_apu_platdata mt8196_platdata = {
	.flags	= {
		.auto_boot = true,
		.fast_on_off = true,
		.infra_wa = true,
		.kernel_load_image = true,
		.map_iova = true,
		.preload_firmware = true,
		.secure_boot = true,
		.secure_coredump = true,
		.smmu_support = true,
	},
	.config = {
		.up_code_buf_sz = 0x100000,
		.up_coredump_buf_sz = 0x160000,
		.regdump_buf_sz	= 0x10000,
		.mdla_coredump_buf_sz = 0x0,
		.mvpu_coredump_buf_sz = 0x0,
		.mvpu_sec_coredump_buf_sz = 0x0,
		.up_tcm_sz = 0x50000,
		.ce_coredump_buf_sz = 0x10000
	},
	.ops		= {
		.init	= mt8196_rproc_init,
		.start	= mt8196_rproc_start,
		.setup = mt8196_rproc_setup,
		.stop	= mt8196_rproc_stop,
		.mtk_apu_memmap_init = mt8196_apu_memmap_init,
		.power_on_off = mt8196_power_on_off,
		.suspend = mt8196_apu_suspend,
		.resume = mt8196_apu_resume,
	},
	.fw_name = "mediatek/mt8196/apusys.img",
};
