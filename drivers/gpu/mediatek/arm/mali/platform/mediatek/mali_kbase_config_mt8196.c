// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 MediaTek Inc.
 */

#include <linux/ioport.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/pm_qos.h>
#include <linux/pm_runtime.h>
#include <linux/arm-smccc.h>
#include <linux/soc/mediatek/mtk_sip_svc.h>
#include <device/mali_kbase_device.h>
#include "ged_dvfs.h"
#ifdef MALI_MTK_GHPM_STAGE1_ENABLE
#include "ged_notify_sw_vsync.h"
#include "ghpm_wrapper.h"
#include "gpueb_debug.h"
#endif /* MALI_MTK_GHPM_STAGE1_ENABLE */
#include "mali_kbase.h"
#include "mali_kbase_defs.h"
#include "mali_kbase_config.h"
#include "mali_kbase_config_platform.h"
#include "mtk_gpufreq.h"
#include "mtk_gpu_utility.h"
#include "mtk_platform_common.h"
#include "mtk_platform_debug.h"

/* KBASE_PLATFORM_DEBUG_ENABLE, 1 for debug log enable, 0 for disable */
#define KBASE_PLATFORM_DEBUG_ENABLE  (0)

/* KBASE_PLATFORM_SUSPEND_DELAY, the ms for autosuspend timeout */
#define KBASE_PLATFORM_SUSPEND_DELAY (100) /* ms */

DEFINE_MUTEX(g_mfg_lock);

static int pm_callback_power_on_nolock(struct kbase_device *kbdev)
{
	if (mtk_common_gpufreq_bringup()) {
		mtk_common_pm_mfg_active();
		return 0;
	}

	if (!gpufreq_power_ctrl_enable()) {
		mtk_common_pm_mfg_active();
		ged_dvfs_gpu_clock_switch_notify(GED_POWER_ON);
		return 0;
	}

	if (mtk_common_pm_is_mfg_active())
		return 0;

	/* update required frequency from GED to sysram */
	mtk_common_ged_dvfs_write_sysram_last_commit_top_idx();
	mtk_common_ged_dvfs_write_sysram_last_commit_dual();

	/* on,off/ SWCG(BG3D)/ MTCMOS/ BUCK */
	if (gpufreq_power_control(GPU_PWR_ON) < 0) {
		KBASE_PLATFORM_LOGE("Power On Failed");
		return 0;
	}

	/* set a flag to enable GPU DVFS */
	mtk_common_pm_mfg_active();

	ged_dvfs_gpu_clock_switch_notify(GED_POWER_ON);
	return 1;
}

static void pm_callback_power_off_nolock(struct kbase_device *kbdev)
{
	if (mtk_common_gpufreq_bringup())
		return;

	if (!gpufreq_power_ctrl_enable())
		return;

	if (!mtk_common_pm_is_mfg_active())
		return;

	ged_dvfs_gpu_clock_switch_notify(GED_POWER_OFF);

	/* set a flag to disable GPU DVFS */
	mtk_common_pm_mfg_idle();

	/* on,off/ SWCG(BG3D)/ MTCMOS/ BUCK */
	if (gpufreq_power_control(GPU_PWR_OFF) < 0) {
		KBASE_PLATFORM_LOGE("Power Off Failed");
		return;
	}
}

static int pm_callback_power_on(struct kbase_device *kbdev)
{
	int ret = 1;
	unsigned long flags;

	dev_dbg(kbdev->dev, "%s %pK\n", __func__, (void *)kbdev->dev->pm_domain);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	WARN_ON(kbdev->pm.backend.gpu_powered);

	if (likely(kbdev->csf.firmware_inited)) {
		WARN_ON(!kbdev->pm.active_count);
		WARN_ON(kbdev->pm.runtime_active);
	}
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

#ifdef MALI_MTK_GHPM_STAGE1_ENABLE
	mutex_lock(&kbdev->ghpm_lock);

	/* Stage-1 Vcore-off-allow, ghpm on
	 * Kbase prevent repeat trigger ghpm on
	 */
	ret = check_pm_callback_state(GED_POWER_ON);
	if (!ret) {
		ret = ghpm_ctrl(GHPM_ON, 0);
		if (ret) {
			dev_info(kbdev->dev, "%s,ghpm power on fail,returned %d\n",
				 __func__, ret);
			dump_pm_callback_kbase_info();
			dump_ghpm_info();
			mutex_unlock(&kbdev->ghpm_lock);
			return ret;
		}
		dev_dbg(kbdev->dev, "%s,ghpm power on success returned %d\n", __func__, ret);

		/* wait gpueb resume */
		ret = wait_gpueb(SUSPEND_POWER_ON);
		if (ret) {
			dev_info(kbdev->dev, "%s,gpueb resume fail,returned %d\n",
				 __func__, ret);
			gpueb_dump_status(NULL, NULL, 0);
			mutex_unlock(&kbdev->ghpm_lock);
			return ret;
		}
		dev_dbg(kbdev->dev, "%s,gpueb resume success,returned %d\n", __func__, ret);

		mutex_lock(&g_mfg_lock);
		ret = pm_callback_power_on_nolock(kbdev);
		mtk_notify_gpu_power_change(1);
		mutex_unlock(&g_mfg_lock);
	} else {
		/* Repat power on detected */
		dump_pm_callback_kbase_info();
		dev_info(kbdev->dev, "%s, previously already powered-on, returned %d\n",
			 __func__, ret);
	}
	mutex_unlock(&kbdev->ghpm_lock);
#else
	mutex_lock(&g_mfg_lock);
	ret = pm_callback_power_on_nolock(kbdev);
	mtk_notify_gpu_power_change(1);
	mutex_unlock(&g_mfg_lock);
#endif /* MALI_MTK_GHPM_STAGE1_ENABLE */

	return ret;
}

static void pm_callback_power_off(struct kbase_device *kbdev)
{
	unsigned long flags;
#ifdef MALI_MTK_GHPM_STAGE1_ENABLE
	int ret = 1;
#endif /* MALI_MTK_GHPM_STAGE1_ENABLE */

	struct arm_smccc_res res;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	WARN_ON(kbdev->pm.backend.gpu_powered);

	if (likely(kbdev->csf.firmware_inited)) {
		WARN_ON(kbase_csf_scheduler_get_nr_active_csgs(kbdev));
		WARN_ON(kbdev->pm.backend.mcu_state != KBASE_MCU_OFF);
	}
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

#ifdef MALI_MTK_GHPM_STAGE1_ENABLE
	mutex_lock(&kbdev->ghpm_lock);
	ret = check_pm_callback_state(GED_POWER_OFF);
	if (!ret) {
		/* Power down the GPU immediately */
		mutex_lock(&g_mfg_lock);
		mtk_notify_gpu_power_change(0);
		pm_callback_power_off_nolock(kbdev);
		mutex_unlock(&g_mfg_lock);
		/* Stage-1 Vcore-off-allow, ghpm off
		 * Kbase prevent repeat trigger ghpm off
		 */
		ret = ghpm_ctrl(GHPM_OFF, 0);
		if (ret) {
			dev_info(kbdev->dev, "%s,ghpm power off fail,returned %d\n",
				 __func__, ret);
			dump_pm_callback_kbase_info();
			dump_ghpm_info();
		} else {
			dev_dbg(kbdev->dev, "%s,ghpm power off success returned %d\n",
				  __func__, ret);

			/* wait gpueb suspend */
			ret = wait_gpueb(SUSPEND_POWER_OFF);
			if (ret) {
				dev_info(kbdev->dev, "%s,gpueb suspend fail,returned %d\n",
					 __func__, ret);
				gpueb_dump_status(NULL, NULL, 0);
			} else
				dev_dbg(kbdev->dev, "%s,gpueb suspend success,returned %d\n",
					  __func__, ret);
		}
	} else {
		/* Repat power off detected */
		dump_pm_callback_kbase_info();
		dev_info(kbdev->dev, "%s, previously already powered-off, returned %d\n",
			 __func__, ret);
	}
	mutex_unlock(&kbdev->ghpm_lock);
#else
	/* Power down the GPU immediately */
	mutex_lock(&g_mfg_lock);
	mtk_notify_gpu_power_change(0);
	pm_callback_power_off_nolock(kbdev);
	mutex_unlock(&g_mfg_lock);
#endif /* MALI_MTK_GHPM_STAGE1_ENABLE */

}

static void pm_callback_runtime_gpu_active(struct kbase_device *kbdev)
{
	int error;
	unsigned long flags;

	lockdep_assert_held(&kbdev->pm.lock);

	mtk_common_ged_dvfs_write_sysram_last_commit_top_idx();
	mtk_common_ged_dvfs_write_sysram_last_commit_dual();

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	WARN_ON(!kbdev->pm.backend.gpu_powered);
	WARN_ON(!kbdev->pm.active_count);
	WARN_ON(kbdev->pm.runtime_active);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	if (pm_runtime_status_suspended(kbdev->dev)) {
		error = pm_runtime_get_sync(kbdev->dev);
		KBASE_PLATFORM_LOGD("pm_runtime_get_sync returned %d", error);
	} else {
		/* Call the async version here, otherwise there could be
		 * a deadlock if the runtime suspend operation is ongoing.
		 * Caller would have taken the kbdev->pm.lock and/or the
		 * scheduler lock, and the runtime suspend callback function
		 * will also try to acquire the same lock(s).
		 */
		error = pm_runtime_get(kbdev->dev);
		KBASE_PLATFORM_LOGD("pm_runtime_get returned %d", error);
	}

	kbdev->pm.runtime_active = true;

	ged_dvfs_gpu_clock_switch_notify(GED_POWER_ON);
}

static void pm_callback_runtime_gpu_idle(struct kbase_device *kbdev)
{
	unsigned long flags;

	lockdep_assert_held(&kbdev->pm.lock);

	mtk_common_ged_dvfs_write_sysram_last_commit_top_idx();
	mtk_common_ged_dvfs_write_sysram_last_commit_dual();

	ged_dvfs_gpu_clock_switch_notify(GED_SLEEP);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	WARN_ON(!kbdev->pm.backend.gpu_powered);
	WARN_ON(kbdev->pm.backend.l2_state != KBASE_L2_OFF);
	WARN_ON(kbdev->pm.active_count);
	WARN_ON(!kbdev->pm.runtime_active);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	pm_runtime_mark_last_busy(kbdev->dev);
	pm_runtime_put_autosuspend(kbdev->dev);
	kbdev->pm.runtime_active = false;
}

static int kbase_device_runtime_init(struct kbase_device *kbdev)
{
	int ret = 0;

	pm_runtime_set_autosuspend_delay(kbdev->dev, KBASE_PLATFORM_SUSPEND_DELAY);
	pm_runtime_use_autosuspend(kbdev->dev);
	pm_runtime_set_active(kbdev->dev);
	pm_runtime_enable(kbdev->dev);

	if (!pm_runtime_enabled(kbdev->dev)) {
		KBASE_PLATFORM_LOGE("pm_runtime not enabled");
		ret = -EINVAL;
	} else if (atomic_read(&kbdev->dev->power.usage_count)) {
		KBASE_PLATFORM_LOGE("%s: Device runtime usage count unexpectedly non zero %d",
			__func__, atomic_read(&kbdev->dev->power.usage_count));
		ret = -EINVAL;
	}

	return ret;
}

static void kbase_device_runtime_disable(struct kbase_device *kbdev)
{
	if (atomic_read(&kbdev->dev->power.usage_count))
		KBASE_PLATFORM_LOGE("%s: Device runtime usage count unexpectedly non zero %d",
			__func__, atomic_read(&kbdev->dev->power.usage_count));

	pm_runtime_disable(kbdev->dev);
}

static int pm_callback_runtime_on(struct kbase_device *kbdev)
{
	return 0;
}

static void pm_callback_runtime_off(struct kbase_device *kbdev)
{
}

static void pm_callback_resume(struct kbase_device *kbdev)
{
	mutex_lock(&g_mfg_lock);
	mutex_unlock(&g_mfg_lock);
}

static void pm_callback_suspend(struct kbase_device *kbdev)
{
	mutex_lock(&g_mfg_lock);
	mutex_unlock(&g_mfg_lock);
}

struct kbase_pm_callback_conf pm_callbacks = {
	.power_on_callback = pm_callback_power_on,
	.power_off_callback = pm_callback_power_off,
	.power_suspend_callback = pm_callback_suspend,
	.power_resume_callback = pm_callback_resume,
	.power_runtime_init_callback = NULL,
	.power_runtime_term_callback = NULL,
	.power_runtime_on_callback = NULL,
	.power_runtime_off_callback = NULL,
	.power_runtime_gpu_idle_callback = NULL,
	.power_runtime_gpu_active_callback = NULL,
};

int mtk_platform_pm_init(struct kbase_device *kbdev)
{
	struct device_node *np = kbdev->dev->of_node;
	bool sleep_mode_enable = false;

	if (IS_ERR_OR_NULL(kbdev))
		return -1;

	sleep_mode_enable = of_property_read_bool(np, "sleep-mode-enable");
	if (sleep_mode_enable) {
		pm_callbacks.power_runtime_init_callback = kbase_device_runtime_init;
		pm_callbacks.power_runtime_term_callback = kbase_device_runtime_disable;
		pm_callbacks.power_runtime_on_callback = pm_callback_runtime_on;
		pm_callbacks.power_runtime_off_callback = pm_callback_runtime_off;
		pm_callbacks.power_runtime_gpu_idle_callback = pm_callback_runtime_gpu_idle;
		pm_callbacks.power_runtime_gpu_active_callback = pm_callback_runtime_gpu_active;
	} else
		dev_info(kbdev->dev, "Sleep mode: No dts property setting, default disabled");

	dev_info(kbdev->dev, "GPU PM Callback - Initialize Done");
	return 0;
}

void mtk_platform_pm_term(struct kbase_device *kbdev)
{
	if (IS_ERR_OR_NULL(kbdev))
		return;
}
