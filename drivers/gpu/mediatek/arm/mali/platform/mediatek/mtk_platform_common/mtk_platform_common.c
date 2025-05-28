// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 MediaTek Inc.
 */

#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include "ged_dvfs.h"
#include "ged_base.h"
#include "ged_gpu_bm.h"
#include "ged_type.h"
#include "mali_kbase.h"
#include "mali_kbase_hwaccess_gpuprops.h"
#include "mali_kbase_gpuprops_private_types.h"
#include "mali_kbase_pm_internal.h"
#include "mtk_platform_common.h"
#include "mtk_platform_dvfs.h"
#include "mtk_gpufreq.h"
#ifdef MALI_MTK_DEVFREQ_GOVERNOR
#include "mtk_platform_devfreq_governor.h"
#endif /* MALI_MTK_DEVFREQ_GOVERNOR */

static bool mfg_powered;
static DEFINE_MUTEX(mfg_pm_lock);
static DEFINE_MUTEX(common_debug_lock);
static struct kbase_device *mali_kbdev;

struct kbase_device *mtk_common_get_kbdev(void)
{
	return mali_kbdev;
}

bool mtk_common_pm_is_mfg_active(void)
{
	return mfg_powered;
}

void mtk_common_pm_mfg_active(void)
{
	mutex_lock(&mfg_pm_lock);
	mfg_powered = true;
	mutex_unlock(&mfg_pm_lock);
}

void mtk_common_pm_mfg_idle(void)
{
	mutex_lock(&mfg_pm_lock);
	mfg_powered = false;
	mutex_unlock(&mfg_pm_lock);
}

int mtk_common_gpufreq_bringup(void)
{
	static int bringup = -1;

	if (bringup == -1) {
#if IS_ENABLED(CONFIG_MTK_GPUFREQ_V2)
		bringup = gpufreq_bringup();
#else
		bringup = mt_gpufreq_bringup();
#endif /* CONFIG_MTK_GPUFREQ_V2 */
	}

	return bringup;
}

int mtk_common_gpufreq_commit(int opp_idx)
{
	int ret = -1;

	mutex_lock(&mfg_pm_lock);
	if (opp_idx >= 0 && mtk_common_pm_is_mfg_active()) {
#if IS_ENABLED(CONFIG_MTK_GPUFREQ_V2)
		ret = mtk_common_gpufreq_bringup() ?
			-1 : gpufreq_commit(TARGET_DEFAULT, opp_idx);
#else
		ret = mtk_common_gpufreq_bringup() ?
			-1 : mt_gpufreq_target(opp_idx, KIR_POLICY);
#endif /* CONFIG_MTK_GPUFREQ_V2 */
	}
	mutex_unlock(&mfg_pm_lock);

	return ret;
}

int mtk_common_gpufreq_dual_commit(int gpu_oppidx, int stack_oppidx)
{
	int ret = -1;

	mutex_lock(&mfg_pm_lock);
	if (stack_oppidx >= 0 && mtk_common_pm_is_mfg_active()) {
#if IS_ENABLED(CONFIG_MTK_GPUFREQ_V2)
		ret = mtk_common_gpufreq_bringup() ?
			-1 : gpufreq_dual_commit(gpu_oppidx, stack_oppidx);
#else
		ret = mtk_common_gpufreq_bringup() ?
			-1 : mt_gpufreq_target(stack_oppidx, KIR_POLICY);
#endif /* CONFIG_MTK_GPUFREQ_V2 */
	}
	mutex_unlock(&mfg_pm_lock);

	return ret;
}

int mtk_common_ged_dvfs_get_last_commit_idx(void)
{
	return (int)ged_dvfs_get_last_commit_idx();
}

int mtk_common_ged_dvfs_get_last_commit_top_idx(void)
{
	return (int)ged_dvfs_get_last_commit_top_idx();
}

int mtk_common_ged_dvfs_get_last_commit_stack_idx(void)
{
	return (int)ged_dvfs_get_last_commit_stack_idx();
}
unsigned long mtk_common_ged_dvfs_write_sysram_last_commit_idx(void)
{
	return (unsigned long)ged_dvfs_write_sysram_last_commit_idx();
}

unsigned long mtk_common_ged_dvfs_write_sysram_last_commit_top_idx(void)
{
	return (unsigned long)ged_dvfs_write_sysram_last_commit_top_idx();
}

unsigned long mtk_common_ged_dvfs_write_sysram_last_commit_stack_idx(void)
{
	return (unsigned long)ged_dvfs_write_sysram_last_commit_stack_idx();
}

unsigned long mtk_common_ged_dvfs_write_sysram_last_commit_dual(void)
{
	return (unsigned long)ged_dvfs_write_sysram_last_commit_dual();
}

unsigned long mtk_common_ged_dvfs_write_sysram_last_commit_idx_test(int commit_idx)
{
	return (unsigned long)ged_dvfs_write_sysram_last_commit_idx_test(commit_idx);
}

unsigned long mtk_common_ged_dvfs_write_sysram_last_commit_top_idx_test(int commit_idx)
{
	return (unsigned long)ged_dvfs_write_sysram_last_commit_top_idx_test(commit_idx);
}

unsigned long mtk_common_ged_dvfs_write_sysram_last_commit_stack_idx_test(int commit_idx)
{
	return (unsigned long)ged_dvfs_write_sysram_last_commit_stack_idx_test(commit_idx);
}

unsigned long mtk_common_ged_dvfs_write_sysram_last_commit_dual_test(int top_idx, int stack_idx)
{
	return (unsigned long)ged_dvfs_write_sysram_last_commit_dual_test(top_idx, stack_idx);
}

int mtk_common_ged_dvfs_update_step_size(int low_step, int med_step, int high_step)
{
	return ged_dvfs_update_step_size(low_step, med_step, high_step);
}

#ifdef MALI_MTK_DEBUG_FS
static void mtk_register_dump(struct seq_file *file, void *va, u32 pa, size_t size)
{
	const int LINE_COUNT_LIMIT = 32; /*512 bytes*/
	u64 addr_va = (u64)va;
	u32 addr_pa = pa;
	int count = 0;

	seq_printf(file, "==== dump register start (VA: 0x%llx, PA: 0x%08x, SIZE: 0x%zx) ====\n",
		addr_va, addr_pa, size);
	seq_puts(file, "                                          3 2 1 0  7 6 5 4  B A 9 8  F E D C\n");
	while (addr_va < (addr_va + size) ) {
		seq_printf(file, "[VA: 0x%llx, PA: 0x%08x] %08x %08x %08x %08x\n",
			addr_va, addr_pa,
			readl((u8 *)(addr_va + 0x0)),
			readl((u8 *)(addr_va + 0x4)),
			readl((u8 *)(addr_va + 0x8)),
			readl((u8 *)(addr_va + 0xc)));
		addr_va += 16;
		addr_pa += 16;

		count++;
		if (count > LINE_COUNT_LIMIT || count*16 > size)
			break;
	}
	seq_printf(file, "==== dump register end (VA: 0x%llx, PA: 0x%08x, SIZE: 0x%zx), data-dumped: 0x%x(%d) bytes ====\n",
				addr_va, addr_pa, size, (count * 16), (count * 16));
}

static u32 mtk_debug_dump_gpu_register_value(struct seq_file *file, bool dump_all)
{
	struct kbase_device *kbdev = file->private;
	u32 value;

	value = readl(kbdev->reg);

	if (dump_all)
		mtk_register_dump(file, kbdev->reg, kbdev->reg_start, kbdev->reg_size);

	return value;
}

static bool mtk_debug_set_gpu_always_on(struct seq_file *file)
{
	static bool is_always_on;

	if (is_always_on)
		return true;

	struct kbase_device *kbdev = file->private;
	int ret_val;

	kbase_pm_set_policy(kbdev, &kbase_pm_always_on_policy_ops);

	ret_val = kbase_pm_wait_for_desired_state(kbdev);

	if (ret_val)
		seq_puts(file, "wait for always on failed.\n");
	else
		is_always_on = true;

	return is_always_on;

}

static int mtk_debug_dump_gpu_register(struct seq_file *file, void *data)
{
#define LABEL_FORMAT	"[%-20s]: "
	struct kbase_device *kbdev = file->private;

	if (IS_ERR_OR_NULL(kbdev))
		return -1;

	/* set gpu always-on */
	seq_printf(file, LABEL_FORMAT "%s\n", "GPU ALWAYS-ON",
		mtk_debug_set_gpu_always_on(file) ? "ENABLED" : "DISABLED");
	seq_printf(file, LABEL_FORMAT "0x%08x\n", "CORE_MASK", gpufreq_get_shader_present());

	/* dump gpu register */
#define READ_GPU_REGISTER_32(REG)\
	seq_printf(file, LABEL_FORMAT "0x%08x\n", #REG, (u32)kbase_reg_read64(kbdev, GPU_CONTROL_ENUM(REG)))

	READ_GPU_REGISTER_32(SHADER_PRESENT);
	READ_GPU_REGISTER_32(SHADER_READY);

#undef READ_GPU_REGISTER_32
#undef LABEL_FORMAT
	mtk_debug_dump_gpu_register_value(file, true);
	return 0;
}

static int mtk_gpu_debugfs_open(struct inode *in, struct file *file)
{
	return single_open(file, mtk_debug_dump_gpu_register, in->i_private);
}

static const struct file_operations mtk_gpu_debugfs_fops = {
	.open = mtk_gpu_debugfs_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release
};

void mtk_dump_gpu_register_debugfs_init(struct kbase_device *kbdev)
{
	if (IS_ERR_OR_NULL(kbdev))
		return;

	debugfs_create_file("gpu_dump_register", 0440,
		kbdev->mali_debugfs_directory, kbdev,
		&mtk_gpu_debugfs_fops);
}

static int mtk_debug_sleep_mode(struct seq_file *file, void *data)
{
	struct kbase_device *kbdev = file->private;
	struct device_node *np;
	bool sleep_mode_enable = false;

	if (IS_ERR_OR_NULL(kbdev))
		return -1;

	np = kbdev->dev->of_node;

	sleep_mode_enable = of_property_read_bool(np, "sleep-mode-enable");
	return 0;
}

static int mtk_sleep_mode_debugfs_open(struct inode *in, struct file *file)
{
	return single_open(file, mtk_debug_sleep_mode, in->i_private);
}

static const struct file_operations mtk_sleep_mode_debugfs_fops = {
	.open = mtk_sleep_mode_debugfs_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release
};

int mtk_debug_sleep_mode_debugfs_init(struct kbase_device *kbdev)
{
	if (IS_ERR_OR_NULL(kbdev))
		return -1;

	debugfs_create_file("sleep_mode", 0440,
		kbdev->mali_debugfs_directory, kbdev,
		&mtk_sleep_mode_debugfs_fops);

	return 0;
}

void mtk_common_debugfs_init(struct kbase_device *kbdev)
{
	if (IS_ERR_OR_NULL(kbdev))
		return;

	mtk_dump_gpu_register_debugfs_init(kbdev);
	mtk_debug_sleep_mode_debugfs_init(kbdev);
	mtk_bandwidth_resource_init();
}
EXPORT_SYMBOL(mtk_common_debugfs_init);
#endif /* MALI_MTK_DEBUG_FS */

int mtk_common_platform_coremask_init(struct kbase_device *kbdev)
{
	int ret = 0;
#if IS_ENABLED(CONFIG_MTK_GPUFREQ_V2)
	u32 shader_present;
	struct kbasep_gpuprops_regdump regdump;

	shader_present = gpufreq_get_shader_present();
	if (shader_present) {
		if (!(ret = kbase_backend_gpuprops_get(kbdev, &regdump))) {
			/* To check whether the platform shader_present is reasonable.
			 * If the check fails, it indicates that the IC is problematic
			 * and cannot be used.
			 */
			if ((regdump.shader_present | shader_present) !=
			    regdump.shader_present) {
				dev_alert(kbdev->dev,
					  "@%s: illegal shader present (HW: %#llX, SW: %#X)",
					  __func__, regdump.shader_present, shader_present);
				return -EINVAL;
			}
			kbase_devfreq_set_core_mask(kbdev, shader_present);
		}
	}
#endif /* CONFIG_MTK_GPUFREQ_V2*/
	return ret;
}

int mtk_common_device_init(struct kbase_device *kbdev)
{
	if (IS_ERR_OR_NULL(kbdev)) {
		dev_info(kbdev->dev, "@%s: invalid kbdev", __func__);
		return -1;
	}

	mali_kbdev = kbdev;

	if (mtk_platform_pm_init(kbdev)) {
		dev_info(kbdev->dev, "@%s: Failed to init Platform PM", __func__);
		return -1;
	}

#ifdef MALI_MTK_DEVFREQ_GOVERNOR
	mtk_devfreq_governor_init(kbdev);
#endif /* MALI_MTK_DEVFREQ_GOVERNOR */

	mtk_dvfs_init(kbdev);
	return 0;
}
EXPORT_SYMBOL(mtk_common_device_init);

void mtk_common_device_term(struct kbase_device *kbdev)
{
	if (IS_ERR_OR_NULL(kbdev)) {
		dev_info(kbdev->dev, "@%s: invalid kbdev", __func__);
		return;
	}

#ifdef MALI_MTK_DEVFREQ_GOVERNOR
	mtk_devfreq_governor_term(kbdev);
#endif /* MALI_MTK_DEVFREQ_GOVERNOR */

	mtk_dvfs_term(kbdev);
	mtk_platform_pm_term(kbdev);
}
EXPORT_SYMBOL(mtk_common_device_term);
