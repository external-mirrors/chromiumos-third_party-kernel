// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 MediaTek Inc.
 */

#include <soc/mediatek/smpu.h>
#include <linux/arm-smccc.h>
#include <linux/bits.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/kthread.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/soc/mediatek/mtk_sip_svc.h>
#include <linux/ratelimit.h>
#include <linux/soc/mediatek/mtk_sip_svc.h>
#include <linux/delay.h>

#define aee_kernel_exception(module, msg...) WARN(1, msg)

struct smpu *global_ssmpu;
struct smpu *global_nsmpu;
struct smpu *global_skp, *global_nkp;

static void set_regs(struct smpu_reg_info_t *reg_list, unsigned int reg_cnt,
		     void __iomem *smpu_base)
{
	unsigned int i, j;

	for (i = 0; i < reg_cnt; i++) {
		for (j = 0; j < reg_list[i].leng; j++)
			writel(reg_list[i].value,
			       smpu_base + reg_list[i].offset + 4 * j);
	}
	/*
	 * Use the memory barrier to make sure the interrupt signal is
	 * de-asserted (by programming registers) before exiting the
	 * ISR and re-enabling the interrupt.
	 */
	mb();
}
static void clear_violation(struct smpu *mpu)
{
	void __iomem *mpu_base;

	mpu_base = mpu->mpu_base;

	set_regs(mpu->clear_reg, mpu->clear_cnt, mpu_base);
}

static void mask_irq(struct smpu *mpu)
{
	void __iomem *mpu_base;

	mpu_base = mpu->mpu_base;
	set_regs(mpu->mask_reg, mpu->mask_cnt, mpu_base);
}

static void clear_kp_violation(unsigned int emi_id)
{
	struct arm_smccc_res smc_res;

	arm_smccc_smc(MTK_SIP_EMIMPU_CONTROL, MTK_EMIMPU_CLEAR_KP, emi_id, 0, 0,
		      0, 0, 0, &smc_res);
}

static bool axi_id_is_gpu(unsigned int axi_id, int vio_type)
{
	unsigned int port;
	unsigned int id;
	unsigned int i;
	struct smpu *mpu = NULL;

	port = axi_id & (BIT_MASK(3) - 1);
	id = axi_id >> 3;

	switch (vio_type) {
	case VIO_TYPE_NSMPU:
			mpu = global_nsmpu;
			break;
	case VIO_TYPE_SSMPU:
			mpu = global_ssmpu;
			break;
	default:
			break;
	}
	if (!mpu)
		return false;

	for (i = 0; i < mpu->bypass_axi_num; i++) {
		if (port == mpu->bypass_axi[i].port && ((id & mpu->bypass_axi[i].axi_mask)
					== mpu->bypass_axi[i].axi_value))
			return true;
	}

	return false;
}
static int bypass_info(unsigned int offset, int vio_type)
{
	unsigned int i;
	struct smpu *mpu = NULL;

	switch (vio_type) {
	case VIO_TYPE_NSMPU:
			mpu = global_nsmpu;
			break;
	case VIO_TYPE_SSMPU:
			mpu = global_ssmpu;
			break;
	default:
			break;
	}
	if (!mpu)
		return -1;

	for (i = 0; i < mpu->bypass_miu_reg_num; i++) {
		if (offset == mpu->bypass_miu_reg[i])
			return i;
	}
	return -1;

}

static bool aid_is_gpu_w(int vio_type, struct smpu_reg_info_t *dump)
{
	int i;
	ssize_t msg_len = 0;
	struct smpu *mpu = NULL;

	switch (vio_type) {
	case VIO_TYPE_NSMPU:
			mpu = global_nsmpu;
			break;
	case VIO_TYPE_SSMPU:
			mpu = global_ssmpu;
			break;
	default:
			break;
	}
	if (!mpu)
		return -1;

	if (!mpu->gpu_bypass_list)
		return false;

	if (dump[mpu->gpu_bypass_list[0]].value == mpu->gpu_bypass_list[1]) {
		WARN(1, "[SMPU] gpu write violation occurs\n");
		for (i = 0; i < mpu->dump_cnt; i++) {
			if (msg_len < MAX_GPU_VIO_LEN)
				msg_len += scnprintf(mpu->vio_msg_gpu + msg_len, MAX_GPU_VIO_LEN - msg_len,
					"[%x]%x;", dump[i].offset,
					dump[i].value);
		}
		WARN(1, "%s", mpu->vio_msg_gpu);
		return true;
	}
	return false;


}

static irqreturn_t smpu_isr_vio_hook(struct smpu_reg_info_t *dump, unsigned int leng, int vio_type)
{
	int i;
	unsigned int srinfo_r = 0, axi_id_r = 0;
	unsigned int srinfo_w = 0, axi_id_w = 0;
	bool bypass, result;
	static DEFINE_RATELIMIT_STATE(ratelimit, 1*HZ, 3);

	for (i = 0; i < leng; i++) {
		switch (bypass_info(dump[i].offset, vio_type)) {
		case WRITE_SRINFO:
			srinfo_w = dump[i].value;
			break;
		case READ_SRINFO:
			srinfo_r = dump[i].value;
			break;
		case WRITE_AXI:
			if (srinfo_w == 3)
				axi_id_w |= (dump[i].value & (BIT_MASK(20) - 1));
			break;
		case READ_AXI:
			if (srinfo_r == 3)
				axi_id_r |= (dump[i].value & (BIT_MASK(20) - 1));
			break;
		case WRITE_AXI_MSB:
			if (srinfo_w == 3) {
				axi_id_w &= (BIT_MASK(16) - 1);
				axi_id_w |= ((dump[i].value &
				(BIT_MASK(4) - 1)) << 16);
			}
			break;
		case READ_AXI_MSB:
			if (srinfo_r == 3) {
				axi_id_r &= (BIT_MASK(16) - 1);
				axi_id_r |= ((dump[i].value &
				(BIT_MASK(4) - 1)) << 16);
			}
			break;
		default:
			break;
		}

		if (srinfo_r == 3 && !axi_id_is_gpu(axi_id_r, vio_type))
			bypass = true;
		else if (srinfo_w == 3 && !axi_id_is_gpu(axi_id_w, vio_type))
			bypass = true;
		else
			bypass = false;

		/* bypass gpu w vio */
		 result = aid_is_gpu_w(vio_type, dump);
		 bypass = bypass ||  result;

		if (bypass == true) {
			if (__ratelimit(&ratelimit)) {
				WARN(1, "srinfo_r %d, axi_id_r 0x%x\n", srinfo_r, axi_id_r);
				WARN(1, "srinfo_w %d, axi_id_w 0x%x\n", srinfo_w, axi_id_w);
			}
		}

	}
	return (bypass) ? IRQ_HANDLED : IRQ_NONE;
}

static irqreturn_t smpu_violation_thread(int irq, void *dev_id)
{
	struct smpu *mpu = (struct smpu *)dev_id;
	struct arm_smccc_res smc_res;
	unsigned int prefetch_mask = 0x200000; /* e00/e80's b[21] = 1 -> prefetch */

	/*
	 * CPU will cause WCE violation
	 */
	int by_pass_aid[3] = { 240, 241, 243 };
	int by_pass_region[10] = { 22, 28, 39, 41, 44, 45, 57, 59, 61, 62 };
	int i, j, by_pass_flag = 0;
	/* var for WCE violation end */

	/* check vio region addr */
	if (!(strcmp(mpu->name, "nsmpu")) || !(strcmp(mpu->name, "ssmpu"))) {
		if (mpu->dump_reg[7].value != 0) {
			/* type(0 start_addr, 1 end_addr) region aid_shift */
			arm_smccc_smc(MTK_SIP_EMIMPU_CONTROL, MTK_EMIMPU_READ,
				      0, mpu->dump_reg[7].value, 0, 0, 0, 0,
				      &smc_res);
			arm_smccc_smc(MTK_SIP_EMIMPU_CONTROL, MTK_EMIMPU_READ,
				      1, mpu->dump_reg[7].value, 0, 0, 0, 0,
				      &smc_res);
		}
		if (mpu->dump_reg[16].value != 0) {
			arm_smccc_smc(MTK_SIP_EMIMPU_CONTROL, MTK_EMIMPU_READ,
				      0, mpu->dump_reg[16].value, 0, 0, 0, 0,
				      &smc_res);
			arm_smccc_smc(MTK_SIP_EMIMPU_CONTROL, MTK_EMIMPU_READ,
				      1, mpu->dump_reg[16].value, 0, 0, 0, 0,
				      &smc_res);
		}

		msleep(30);

		/* by pass WCE, this will be temp patch */
		for (i = 0; i < 3; i++) {
			for (j = 0; j < 10; j++) {
				if (mpu->dump_reg[5].value == by_pass_aid[i] &&
				    mpu->dump_reg[7].value ==
					    by_pass_region[j]) {
					by_pass_flag++;
					break;
				}
			}
			if (by_pass_flag > 0)
				break;
		}
		/* add prefetch mask */
		if ((mpu->dump_reg[0].value & prefetch_mask) ||
		    (mpu->dump_reg[9].value & prefetch_mask) ||
		    (mpu->is_prefetch == true)) {
			WARN(1, "Prefetch without KERNEL_API!!\n");
		} else if (by_pass_flag > 0) {
			WARN(1, "AID == 0x%x && region = 0x%x without KERNEL_API!!\n",
				mpu->dump_reg[5].value,
				mpu->dump_reg[7].value);
		} else if (!mpu->is_bypass) /* by pass GPU write vio */
			aee_kernel_exception("SMPU",
					     mpu->vio_msg); /* for smpu_vio case */
	} else
		aee_kernel_exception("SMPU", mpu->vio_msg); /* for KP case */

	/* for chip might need to remove the kp clear node in dts */
	clear_violation(mpu);

	mpu->is_bypass = false;
	mpu->is_vio = false;

	return IRQ_HANDLED;
}

static irqreturn_t smpu_violation(int irq, void *dev_id)
{
	struct smpu *mpu = (struct smpu *)dev_id;
	struct smpu_reg_info_t *dump_reg = mpu->dump_reg;
	void __iomem *mpu_base;
	int i, vio_dump_idx, vio_dump_pos, prefetch;
	int vio_type = 6;
	bool violation;
	ssize_t msg_len = 0;
	unsigned int prefetch_mask = 0x200000;

	irqreturn_t irqret;
	static DEFINE_RATELIMIT_STATE(ratelimit, 1 * HZ, 3);

	violation = false;
	mpu_base = mpu->mpu_base;

	if (!(strcmp(mpu->name, "nsmpu")))
		vio_type = VIO_TYPE_NSMPU;
	else if (!(strcmp(mpu->name, "ssmpu")))
		vio_type = VIO_TYPE_SSMPU;
	else if (!(strcmp(mpu->name, "nkp")))
		vio_type = VIO_TYPE_NKP;
	else if (!(strcmp(mpu->name, "skp")))
		vio_type = VIO_TYPE_SKP;
	else
		goto clear_violation;

	/* record dump reg */
	for (i = 0; i < mpu->dump_cnt; i++)
		dump_reg[i].value = readl(mpu_base + dump_reg[i].offset);

	/* record vioinfo */
	for (i = 0; i < mpu->vio_dump_cnt; i++) {
		vio_dump_idx = mpu->vio_reg_info[i].vio_dump_idx;
		vio_dump_pos = mpu->vio_reg_info[i].vio_dump_pos;
		if (CHECK_BIT(dump_reg[vio_dump_idx].value, vio_dump_pos)) {
			violation = true;
			mpu->is_vio = true;
		}
	}

	if (!violation) {
		if (__ratelimit(&ratelimit))
			;
		clear_violation(mpu);
		return IRQ_HANDLED;
	}

	if (violation) {
		if (vio_type == VIO_TYPE_NSMPU || vio_type == VIO_TYPE_SSMPU) {
			/* smpu violation */
			if (mpu->by_plat_isr_hook) {
				irqret = mpu->by_plat_isr_hook(
					dump_reg, mpu->dump_cnt, vio_type);

				if (irqret == IRQ_HANDLED) {
					violation = true;
					mpu->is_vio = true;
					mpu->is_bypass = true;
					goto clear_violation;
				}
			}
		}

		if (msg_len < MTK_SMPU_MAX_CMD_LEN) {
			prefetch = (prefetch_mask & readl(mpu_base + 0xe00)) ||
			(prefetch_mask & readl(mpu_base + 0xe80));
			mpu->is_prefetch = prefetch == 1 ? true : false;
			msg_len += scnprintf(mpu->vio_msg + msg_len,
					     MTK_SMPU_MAX_CMD_LEN - msg_len,
					     "\ncpu-prefetch:%d", prefetch);
			msg_len += scnprintf(mpu->vio_msg + msg_len,
					     MTK_SMPU_MAX_CMD_LEN - msg_len,
					     "\n[SMPU]%s\n", mpu->name);
		}

		for (i = 0; i < mpu->dump_cnt; i++) {
			if (msg_len < MTK_SMPU_MAX_CMD_LEN)
				msg_len += scnprintf(
					mpu->vio_msg + msg_len,
					MTK_SMPU_MAX_CMD_LEN - msg_len,
					"[%x]%x;", dump_reg[i].offset,
					dump_reg[i].value);
		}
	}

clear_violation:
	mask_irq(mpu);
	if (vio_type == VIO_TYPE_NKP || vio_type == VIO_TYPE_SKP)
		clear_kp_violation(vio_type % 2);

	/* if there is violation happened, wake up the thread */
	if (violation)
		return IRQ_WAKE_THREAD;

	return IRQ_HANDLED;
}

static const struct of_device_id smpu_of_ids[] = {
	{
		.compatible = "mediatek,smpu",
	},
	{}
};
MODULE_DEVICE_TABLE(of, smpu_of_ids);

/* As SLC b mode enable CPU will write clean evict, which may trigger SMPU violation.
 * and those data may cached in CPU L3 cache through CPU prefetch.
 */
static void smpu_clean_cpu_write_vio(struct platform_device *pdev, struct smpu *mpu)
{
	int sec_cpu_aid = 240;
	int ns_cpu_aid = 241;
	int hyp_cpu_aid = 243;
	bool slc_enable = mpu->slc_b_mode;
	int i;
	void __iomem *mpu_base = mpu->mpu_base;
	struct smpu_reg_info_t *dump_reg = mpu->dump_reg;
	int vio_type = 6, prefetch = 0;
	ssize_t msg_len = 0;
	unsigned int prefetch_mask = 0x200000;

	/* smpu check violation */
	if (slc_enable) {
		if (!(strcmp(mpu->name, "nsmpu")))
			vio_type = VIO_TYPE_NSMPU;
		else if (!(strcmp(mpu->name, "ssmpu")))
			vio_type = VIO_TYPE_SSMPU;
		else if (!(strcmp(mpu->name, "nkp")))
			vio_type = VIO_TYPE_NKP;
		else if (!(strcmp(mpu->name, "skp")))
			vio_type = VIO_TYPE_SKP;

		/* read SMPU/KP vio reg */
		for (i = 0; i < mpu->dump_cnt; i++)
			dump_reg[i].value =
				readl(mpu_base + dump_reg[i].offset);

		/* check whether cpu type master lead this smpu violation */
		if (!(strcmp(mpu->name, "nsmpu")) ||
		    !(strcmp(mpu->name, "ssmpu"))) {
			/* check smpu write violation aid reg */
			if ((mpu->dump_reg[5].value == sec_cpu_aid) ||
			    (mpu->dump_reg[5].value == ns_cpu_aid) ||
			    (mpu->dump_reg[5].value == hyp_cpu_aid)) {
				if (msg_len < MTK_SMPU_MAX_CMD_LEN) {
					prefetch = (prefetch_mask & readl(mpu_base + 0xe00)) ||
					(prefetch_mask & readl(mpu_base + 0xe80));
					mpu->is_prefetch = prefetch == 1 ? true : false;
					msg_len += scnprintf(mpu->vio_msg + msg_len,
					     MTK_SMPU_MAX_CMD_LEN - msg_len,
					     "\ncpu-prefetch:%d", prefetch);
					msg_len += scnprintf(mpu->vio_msg + msg_len,
					     MTK_SMPU_MAX_CMD_LEN - msg_len,
					     "\n[SMPU]%s\n", mpu->name);
				}
				for (i = 0; i < mpu->dump_cnt; i++) {
					if (msg_len < MTK_SMPU_MAX_CMD_LEN)
						msg_len += scnprintf(
						mpu->vio_msg + msg_len,
						MTK_SMPU_MAX_CMD_LEN - msg_len,
						"[%x]%x;", dump_reg[i].offset,
						dump_reg[i].value);
				}
				dev_err(&pdev->dev, "%s", mpu->vio_msg);
				clear_violation(mpu);
			}
		} else {
			/* check kp write violation aid reg */
			if ((MTK_SMPU_KP_AID(mpu->dump_reg[1].value) ==
			     sec_cpu_aid) ||
			    (MTK_SMPU_KP_AID(mpu->dump_reg[1].value) ==
			     ns_cpu_aid) ||
			    (MTK_SMPU_KP_AID(mpu->dump_reg[1].value) ==
			     hyp_cpu_aid)) {
				dev_err(&pdev->dev, "%s", mpu->vio_msg);
				clear_violation(mpu);
			}
		}
	}
}

static int smpu_probe(struct platform_device *pdev)
{
	struct device_node *smpu_node = pdev->dev.of_node;
	struct smpu *mpu;
	const char *name = NULL;

	int ret, i, size, axi_set_num;
	unsigned int *dump_list, *miumpu_bypass_list, *gpu_bypass_list;

	dev_info(&pdev->dev, "driver probe");
	if (!smpu_node) {
		dev_err(&pdev->dev, "No smpu-reg");
		return -ENXIO;
	}

	mpu = devm_kzalloc(&pdev->dev, sizeof(struct smpu), GFP_KERNEL);
	if (!mpu)
		return -ENOMEM;

	platform_set_drvdata(pdev, mpu);

	if (!of_property_read_string(smpu_node, "name", &name))
		mpu->name = name;
	/* is_vio default value */
	mpu->is_vio = false;
	mpu->is_bypass = false;

	/* dump_reg */
	size = of_property_count_elems_of_size(smpu_node, "dump", sizeof(char));
	if (size <= 0) {
		dev_err(&pdev->dev, "No smpu node dump\n");
		return -ENXIO;
	}
	dump_list = devm_kmalloc(&pdev->dev, size, GFP_KERNEL);
	if (!dump_list)
		return -ENXIO;

	size >>= 2;
	mpu->dump_cnt = size;
	ret = of_property_read_u32_array(smpu_node, "dump", dump_list, size);
	if (ret) {
		dev_err(&pdev->dev, "no smpu dump\n");
		return -ENXIO;
	}

	mpu->dump_reg = devm_kmalloc(
		&pdev->dev, size * sizeof(struct smpu_reg_info_t), GFP_KERNEL);
	if (!(mpu->dump_reg))
		return -ENOMEM;

	for (i = 0; i < mpu->dump_cnt; i++) {
		mpu->dump_reg[i].offset = dump_list[i];
		mpu->dump_reg[i].value = 0;
		mpu->dump_reg[i].leng = 0;
	}
	/* dump_reg end
	 * dump_clear
	 */
	size = of_property_count_elems_of_size(smpu_node, "clear",
					       sizeof(char));
	if (size <= 0) {
		dev_err(&pdev->dev, "No clear smpu");
		return -ENXIO;
	}
	mpu->clear_reg = devm_kmalloc(&pdev->dev, size, GFP_KERNEL);
	if (!(mpu->clear_reg))
		return -ENOMEM;

	mpu->clear_cnt = size / sizeof(struct smpu_reg_info_t);
	size >>= 2;
	ret = of_property_read_u32_array(
		smpu_node, "clear", (unsigned int *)(mpu->clear_reg), size);
	if (ret) {
		dev_err(&pdev->dev, "No clear reg");
		return -ENXIO;
	}
	/* dump_clear end
	 * dump_clear_md
	 */
	size = of_property_count_elems_of_size(smpu_node, "clear-md",
					       sizeof(char));
	if (size <= 0)
		dev_info(&pdev->dev, "No clear_md smpu");
	if (size > 0) {
		mpu->clear_md_reg = devm_kmalloc(&pdev->dev, size, GFP_KERNEL);
		if (!(mpu->clear_md_reg))
			return -ENOMEM;

		mpu->clear_md_cnt = size / sizeof(struct smpu_reg_info_t);
		size >>= 2;
		ret = of_property_read_u32_array(
			smpu_node, "clear-md",
			(unsigned int *)(mpu->clear_md_reg), size);
		if (ret) {
			dev_err(&pdev->dev, "No clear-md reg");
			return -ENXIO;
		}
	}

	/* dump_clear_md_end
	 * dump vio info
	 */
	size = of_property_count_elems_of_size(smpu_node, "vio-info",
					       sizeof(char));

	if (size <= 0)
		return -ENXIO;

	mpu->vio_dump_cnt = size / sizeof(struct smpu_vio_dump_info_t);
	mpu->vio_reg_info = devm_kmalloc(&pdev->dev, size, GFP_KERNEL);
	if (!(mpu->vio_reg_info))
		return -ENOMEM;
	size >>= 2;
	ret = of_property_read_u32_array(smpu_node, "vio-info",
					 (unsigned int *)(mpu->vio_reg_info),
					 size);
	if (ret)
		return -ENXIO;
	/* dump vio-info end */
	size = of_property_count_elems_of_size(smpu_node, "mask", sizeof(char));
	if (size <= 0) {
		dev_err(&pdev->dev, "No clear smpu\n");
		return -ENXIO;
	}
	mpu->mask_reg = devm_kmalloc(&pdev->dev, size, GFP_KERNEL);
	if (!(mpu->clear_reg))
		return -ENOMEM;

	mpu->mask_cnt = size / sizeof(struct smpu_reg_info_t);
	size >>= 2;
	ret = of_property_read_u32_array(smpu_node, "mask",
					 (unsigned int *)(mpu->mask_reg), size);
	if (ret) {
		dev_err(&pdev->dev, "No mask reg\n");
		return -ENXIO;
	}

	/* only for smpu node */
	if ((!(strcmp(mpu->name, "ssmpu"))) ||
	    (!(strcmp(mpu->name, "nsmpu")))) {
		/*
		 *  get the md violation register content.
		 */
		size = of_property_count_elems_of_size(smpu_node, "dump-md",
						       sizeof(char));
		if (size <= 0){
			dev_info(&pdev->dev, "No smpu node dump-md\n");
			mpu->dump_md_cnt = 0;
		}else{
			dump_list = devm_kmalloc(&pdev->dev, size, GFP_KERNEL);
			if (!dump_list)
				return -ENXIO;

			size >>= 2;
			mpu->dump_md_cnt = size;
			ret = of_property_read_u32_array(smpu_node, "dump-md", dump_list, size);
			if (ret) {
				dev_err(&pdev->dev, "no smpu dump-md\n");
				return -ENXIO;
			}

			mpu->dump_md_reg = devm_kmalloc(
				&pdev->dev, size * sizeof(struct smpu_reg_info_t), GFP_KERNEL);
			if (!(mpu->dump_md_reg))
				return -ENOMEM;

			for (i = 0; i < mpu->dump_cnt; i++) {
				mpu->dump_md_reg[i].offset = dump_list[i];
				mpu->dump_md_reg[i].value = 0;
				mpu->dump_md_reg[i].leng = 0;
			}
		}
		/* get md reg content end*/
		/* bypass_axi */
		size = of_property_count_elems_of_size(smpu_node, "bypass-axi",
						       sizeof(char));
		if (size <= 0)
			return -ENXIO;

		miumpu_bypass_list = devm_kmalloc(&pdev->dev, size, GFP_KERNEL);
		if (!miumpu_bypass_list)
			return -ENOMEM;

		size /= sizeof(unsigned int);
		axi_set_num = AXI_SET_NUM(size);
		mpu->bypass_axi_num = axi_set_num;
		ret = of_property_read_u32_array(smpu_node, "bypass-axi",
						 miumpu_bypass_list, size);
		if (ret) {
			dev_err(&pdev->dev, "No bypass miu mpu\n");
			return -ENXIO;
		}
		mpu->bypass_axi = devm_kmalloc(
			&pdev->dev,
			axi_set_num * sizeof(struct bypass_axi_info_t),
			GFP_KERNEL);
		if (!(mpu->bypass_axi))
			return -ENOMEM;

		for (i = 0; i < mpu->bypass_axi_num; i++) {
			mpu->bypass_axi[i].port =
				miumpu_bypass_list[(i * 3) + 0];
			mpu->bypass_axi[i].axi_mask =
				miumpu_bypass_list[(i * 3) + 1];
			mpu->bypass_axi[i].axi_value =
				miumpu_bypass_list[(i * 3) + 2];
		}

		/* bypass_axi end
		 * bypass miumpu start
		 */
		size = of_property_count_elems_of_size(smpu_node, "bypass",
						       sizeof(char));

		if (size <= 0)
			return -ENXIO;

		miumpu_bypass_list = devm_kmalloc(&pdev->dev, size, GFP_KERNEL);
		if (!miumpu_bypass_list)
			return -ENOMEM;

		size /= sizeof(unsigned int);
		mpu->bypass_miu_reg_num = size;
		ret = of_property_read_u32_array(smpu_node, "bypass",
						 miumpu_bypass_list, size);
		if (ret) {
			dev_err(&pdev->dev, "No bypass miu mpu\n");
			return -ENXIO;
		}
		mpu->bypass_miu_reg = devm_kmalloc(
			&pdev->dev, size * sizeof(unsigned int), GFP_KERNEL);
		if (!(mpu->bypass_miu_reg))
			return -ENOMEM;

		for (i = 0; i < mpu->bypass_miu_reg_num; i++)
			mpu->bypass_miu_reg[i] = miumpu_bypass_list[i];

		size = of_property_count_elems_of_size(smpu_node, "bypass-gpu",
						       sizeof(char));
		if (size > 0) {
			gpu_bypass_list =
				devm_kmalloc(&pdev->dev, size, GFP_KERNEL);
			if (!gpu_bypass_list)
				return -ENOMEM;

			size >>= 2;
			ret = of_property_read_u32_array(
				smpu_node, "bypass-gpu", gpu_bypass_list, size);
			if (!gpu_bypass_list) {
				dev_err(&pdev->dev, "no gpu-bypass\n");
				return -ENXIO;
			}

			mpu->gpu_bypass_list =
				devm_kmalloc(&pdev->dev,
					     size * sizeof(unsigned int),
					     GFP_KERNEL);

			if (!mpu->gpu_bypass_list)
				return -ENOMEM;

			for (i = 0; i < 2; i++)
				mpu->gpu_bypass_list[i] = gpu_bypass_list[i];
		}
		/* bypass_miumpu end */

	} /* only for smpu end */

	/* reg base */
	mpu->mpu_base = of_iomap(smpu_node, 0);
	if (IS_ERR(mpu->mpu_base)) {
		dev_err(&pdev->dev, "Failed to map smpu range base");
		return -EIO;
	}
	/* reg base end */

	mpu->vio_msg =
		devm_kmalloc(&pdev->dev, MTK_SMPU_MAX_CMD_LEN, GFP_KERNEL);
	if (!(mpu->vio_msg))
		return -ENOMEM;

	mpu->vio_msg_gpu =
		devm_kmalloc(&pdev->dev, MAX_GPU_VIO_LEN, GFP_KERNEL);
	if (!mpu->vio_msg_gpu)
		return -ENOMEM;

	mpu->by_plat_isr_hook = smpu_isr_vio_hook;
	/* transt global */

	if (!strcmp(mpu->name, "ssmpu"))
		global_ssmpu = mpu;
	if (!strcmp(mpu->name, "nsmpu"))
		global_nsmpu = mpu;
	if (!strcmp(mpu->name, "skp"))
		global_skp = mpu;
	if (!strcmp(mpu->name, "nkp"))
		global_nkp = mpu;

	if (of_property_read_bool(smpu_node, "mediatek,slc-b-mode"))
		mpu->slc_b_mode = true;

	smpu_clean_cpu_write_vio(pdev, mpu);

	mpu->irq = irq_of_parse_and_map(smpu_node, 0);
	if (mpu->irq == 0) {
		dev_err(&pdev->dev, "Failed to get irq resource\n");
		return -ENXIO;
	}
	/*
	 * change it to threaded irq
	 */
	ret = request_threaded_irq(mpu->irq, (irq_handler_t)smpu_violation,
				   (irq_handler_t)smpu_violation_thread,
				   IRQF_ONESHOT, "smpu", mpu);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request irq");
		return -EINVAL;
	}

	devm_kfree(&pdev->dev, dump_list);

	return 0;
}
static int smpu_remove(struct platform_device *pdev)
{
	struct smpu *mpu = platform_get_drvdata(pdev);

	dev_info(&pdev->dev, "driver removed\n");

	free_irq(mpu->irq, mpu);

	global_ssmpu = NULL;
	global_nsmpu = NULL;
	global_skp = NULL;
	global_nkp = NULL;

	return 0;
}

static struct platform_driver smpu_driver = {
	.probe = smpu_probe,
	.remove = smpu_remove,
	.driver = {
		.name = "smpu_driver",
		.owner = THIS_MODULE,
		.of_match_table = smpu_of_ids,
	},
};

module_platform_driver(smpu_driver);

MODULE_DESCRIPTION("MediaTek SMPU Driver");
MODULE_LICENSE("GPL");
