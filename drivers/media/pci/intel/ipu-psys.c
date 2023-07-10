// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2013 - 2020 Intel Corporation

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/firmware.h>
#include <linux/fs.h>
#include <linux/highmem.h>
#include <linux/init_task.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/version.h>
#include <linux/poll.h>
#include <uapi/linux/sched/types.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/dma-mapping.h>

#include <uapi/linux/ipu-psys.h>

#include "ipu.h"
#include "ipu-mmu.h"
#include "ipu-bus.h"
#include "ipu-platform.h"
#include "ipu-buttress.h"
#include "ipu-cpd.h"
#include "ipu-fw-psys.h"
#include "ipu-psys.h"
#include "ipu-platform-psys.h"
#include "ipu-platform-regs.h"
#include "ipu-fw-com.h"
#include "ipu-kcam.h"

static bool async_fw_init;
module_param(async_fw_init, bool, 0664);
MODULE_PARM_DESC(async_fw_init, "Enable asynchronous firmware initialization");

#define IPU_PSYS_NUM_DEVICES		4
#define IPU_PSYS_AUTOSUSPEND_DELAY	2000

#ifdef CONFIG_PM
static int psys_runtime_pm_resume(struct device *dev);
static int psys_runtime_pm_suspend(struct device *dev);
#else
#define pm_runtime_dont_use_autosuspend(d)
#define pm_runtime_use_autosuspend(d)
#define pm_runtime_set_autosuspend_delay(d, f)	0
#define pm_runtime_get_sync(d)			0
#define pm_runtime_put(d)			0
#define pm_runtime_put_sync(d)			0
#define pm_runtime_put_noidle(d)		0
#define pm_runtime_put_autosuspend(d)		0
#endif

static DECLARE_BITMAP(ipu_psys_devices, IPU_PSYS_NUM_DEVICES);
static DEFINE_MUTEX(ipu_psys_mutex);

static struct fw_init_task {
	struct delayed_work work;
	struct ipu_psys *psys;
} fw_init_task;

static void ipu_psys_remove(struct ipu_bus_device *adev);

struct ipu_psys_pg *__get_pg_buf(struct ipu_psys *psys, size_t pg_size)
{
	struct ipu_psys_pg *kpg;
	unsigned long flags;

	spin_lock_irqsave(&psys->pgs_lock, flags);
	list_for_each_entry(kpg, &psys->pgs, list) {
		if (!kpg->pg_size && kpg->size >= pg_size) {
			kpg->pg_size = pg_size;
			spin_unlock_irqrestore(&psys->pgs_lock, flags);
			return kpg;
		}
	}
	spin_unlock_irqrestore(&psys->pgs_lock, flags);
	/* no big enough buffer available, allocate new one */
	kpg = kzalloc(sizeof(*kpg), GFP_KERNEL);
	if (!kpg)
		return NULL;

	kpg->pg = dma_alloc_attrs(&psys->adev->dev, pg_size,
				  &kpg->pg_dma_addr, GFP_KERNEL, 0);
	if (!kpg->pg) {
		kfree(kpg);
		return NULL;
	}

	kpg->pg_size = pg_size;
	kpg->size = pg_size;
	spin_lock_irqsave(&psys->pgs_lock, flags);
	list_add(&kpg->list, &psys->pgs);
	spin_unlock_irqrestore(&psys->pgs_lock, flags);

	return kpg;
}

long ipu_get_manifest(struct ipu_psys_manifest *manifest,
		      struct ipu_psys *psys)
{
	struct ipu_device *isp = psys->adev->isp;
	struct ipu_cpd_client_pkg_hdr *client_pkg;
	u32 entries;
	void *host_fw_data;
	dma_addr_t dma_fw_data;
	u32 client_pkg_offset;

	host_fw_data = (void *)isp->cpd_fw->data;
	dma_fw_data = sg_dma_address(psys->fw_sgt.sgl);

	entries = ipu_cpd_pkg_dir_get_num_entries(psys->pkg_dir);
	if (!manifest || manifest->index > entries - 1) {
		dev_err(&psys->adev->dev, "invalid argument\n");
		return -EINVAL;
	}

	if (!ipu_cpd_pkg_dir_get_size(psys->pkg_dir, manifest->index) ||
	    ipu_cpd_pkg_dir_get_type(psys->pkg_dir, manifest->index) <
	    IPU_CPD_PKG_DIR_CLIENT_PG_TYPE) {
		dev_dbg(&psys->adev->dev, "invalid pkg dir entry\n");
		return -ENOENT;
	}

	client_pkg_offset = ipu_cpd_pkg_dir_get_address(psys->pkg_dir,
							manifest->index);
	client_pkg_offset -= dma_fw_data;

	client_pkg = host_fw_data + client_pkg_offset;
	manifest->size = client_pkg->pg_manifest_size;

	if (!manifest->manifest)
		return 0;

	if (copy_to_user(manifest->manifest,
			 (uint8_t *)client_pkg + client_pkg->pg_manifest_offs,
			 manifest->size)) {
		dev_err(&psys->adev->dev, "copy_to_user error");
		return -EFAULT;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(ipu_get_manifest);

#ifdef CONFIG_PM
static int psys_runtime_pm_resume(struct device *dev)
{
	struct ipu_bus_device *adev = to_ipu_bus_device(dev);
	struct ipu_psys *psys = ipu_bus_get_drvdata(adev);
	unsigned long flags;
	int retval;

	if (!psys)
		return 0;

	/*
	 * In runtime autosuspend mode, if the psys is in power on state, no
	 * need to resume again.
	 */
	spin_lock_irqsave(&psys->ready_lock, flags);
	if (psys->ready) {
		spin_unlock_irqrestore(&psys->ready_lock, flags);
		return 0;
	}
	spin_unlock_irqrestore(&psys->ready_lock, flags);

	retval = ipu_mmu_hw_init(adev->mmu);
	if (retval)
		return retval;

	if (async_fw_init && !psys->fwcom) {
		dev_err(dev,
			"%s: asynchronous firmware init not finished, skipping\n",
			__func__);
		return 0;
	}

	if (!ipu_buttress_auth_done(adev->isp)) {
		dev_dbg(dev, "%s: not yet authenticated, skipping\n", __func__);
		return 0;
	}

	ipu_psys_setup_hw(psys);

	ipu_psys_subdomains_power(psys, 1);
	ipu_trace_restore(&psys->adev->dev);

	ipu_configure_spc(adev->isp,
			  &psys->pdata->ipdata->hw_variant,
			  IPU_CPD_PKG_DIR_PSYS_SERVER_IDX,
			  psys->pdata->base, psys->pkg_dir,
			  psys->pkg_dir_dma_addr);

	retval = ipu_fw_psys_open(psys);
	if (retval) {
		dev_err(&psys->adev->dev, "Failed to open abi.\n");
		return retval;
	}

	spin_lock_irqsave(&psys->ready_lock, flags);
	psys->ready = 1;
	spin_unlock_irqrestore(&psys->ready_lock, flags);

	return 0;
}

static int psys_runtime_pm_suspend(struct device *dev)
{
	struct ipu_bus_device *adev = to_ipu_bus_device(dev);
	struct ipu_psys *psys = ipu_bus_get_drvdata(adev);
	unsigned long flags;
	int rval;

	if (!psys)
		return 0;

	if (!psys->ready)
		return 0;

	spin_lock_irqsave(&psys->ready_lock, flags);
	psys->ready = 0;
	spin_unlock_irqrestore(&psys->ready_lock, flags);

	/*
	 * We can trace failure but better to not return an error.
	 * At suspend we are progressing towards psys power gated state.
	 * Any hang / failure inside psys will be forgotten soon.
	 */
	rval = ipu_fw_psys_close(psys);
	if (rval)
		dev_err(dev, "Device close failure: %d\n", rval);

	ipu_psys_subdomains_power(psys, 0);

	ipu_mmu_hw_cleanup(adev->mmu);

	return 0;
}

/* The following PM callbacks are needed to enable runtime PM in IPU PCI
 * device resume, otherwise, runtime PM can't work in PCI resume from
 * S3 state.
 */
static int psys_resume(struct device *dev)
{
	return 0;
}

static int psys_suspend(struct device *dev)
{
	return 0;
}

static const struct dev_pm_ops psys_pm_ops = {
	.runtime_suspend = psys_runtime_pm_suspend,
	.runtime_resume = psys_runtime_pm_resume,
	.suspend = psys_suspend,
	.resume = psys_resume,
};

#define PSYS_PM_OPS (&psys_pm_ops)
#else
#define PSYS_PM_OPS NULL
#endif

static int cpd_fw_reload(struct ipu_device *isp)
{
	struct ipu_psys *psys = ipu_bus_get_drvdata(isp->psys);
	int rval;

	if (!isp->secure_mode) {
		dev_warn(&isp->pdev->dev,
			 "CPD firmware reload was only supported for secure mode.\n");
		return -EINVAL;
	}

	if (isp->cpd_fw) {
		ipu_cpd_free_pkg_dir(isp->psys, psys->pkg_dir,
				     psys->pkg_dir_dma_addr,
				     psys->pkg_dir_size);

		ipu_buttress_unmap_fw_image(isp->psys, &psys->fw_sgt);
		release_firmware(isp->cpd_fw);
		isp->cpd_fw = NULL;
		dev_info(&isp->pdev->dev, "Old FW removed\n");
	}

	rval = request_cpd_fw(&isp->cpd_fw, isp->cpd_fw_name,
			      &isp->pdev->dev);
	if (rval) {
		dev_err(&isp->pdev->dev, "Requesting firmware(%s) failed\n",
			isp->cpd_fw_name);
		return rval;
	}

	rval = ipu_cpd_validate_cpd_file(isp, isp->cpd_fw->data,
					 isp->cpd_fw->size);
	if (rval) {
		dev_err(&isp->pdev->dev, "Failed to validate cpd file\n");
		goto out_release_firmware;
	}

	rval = ipu_buttress_map_fw_image(isp->psys, isp->cpd_fw, &psys->fw_sgt);
	if (rval)
		goto out_release_firmware;

	psys->pkg_dir = ipu_cpd_create_pkg_dir(isp->psys,
					       isp->cpd_fw->data,
					       sg_dma_address(psys->fw_sgt.sgl),
					       &psys->pkg_dir_dma_addr,
					       &psys->pkg_dir_size);

	if (!psys->pkg_dir) {
		rval = -EINVAL;
		goto out_unmap_fw_image;
	}

	isp->pkg_dir = psys->pkg_dir;
	isp->pkg_dir_dma_addr = psys->pkg_dir_dma_addr;
	isp->pkg_dir_size = psys->pkg_dir_size;

	if (!isp->secure_mode)
		return 0;

	rval = ipu_fw_authenticate(isp, 1);
	if (rval)
		goto out_free_pkg_dir;

	return 0;

out_free_pkg_dir:
	ipu_cpd_free_pkg_dir(isp->psys, psys->pkg_dir,
			     psys->pkg_dir_dma_addr, psys->pkg_dir_size);
out_unmap_fw_image:
	ipu_buttress_unmap_fw_image(isp->psys, &psys->fw_sgt);
out_release_firmware:
	release_firmware(isp->cpd_fw);
	isp->cpd_fw = NULL;

	return rval;
}

#ifdef CONFIG_DEBUG_FS
static int ipu_psys_icache_prefetch_sp_get(void *data, u64 *val)
{
	struct ipu_psys *psys = data;

	*val = psys->icache_prefetch_sp;
	return 0;
}

static int ipu_psys_icache_prefetch_sp_set(void *data, u64 val)
{
	struct ipu_psys *psys = data;

	if (val != !!val)
		return -EINVAL;

	psys->icache_prefetch_sp = val;

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(psys_icache_prefetch_sp_fops,
			ipu_psys_icache_prefetch_sp_get,
			ipu_psys_icache_prefetch_sp_set, "%llu\n");

static int ipu_psys_icache_prefetch_isp_get(void *data, u64 *val)
{
	struct ipu_psys *psys = data;

	*val = psys->icache_prefetch_isp;
	return 0;
}

static int ipu_psys_icache_prefetch_isp_set(void *data, u64 val)
{
	struct ipu_psys *psys = data;

	if (val != !!val)
		return -EINVAL;

	psys->icache_prefetch_isp = val;

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(psys_icache_prefetch_isp_fops,
			ipu_psys_icache_prefetch_isp_get,
			ipu_psys_icache_prefetch_isp_set, "%llu\n");

static int ipu_psys_init_debugfs(struct ipu_psys *psys)
{
	struct dentry *file;
	struct dentry *dir;

	dir = debugfs_create_dir("psys", psys->adev->isp->ipu_dir);
	if (IS_ERR(dir))
		return -ENOMEM;

	file = debugfs_create_file("icache_prefetch_sp", 0600,
				   dir, psys, &psys_icache_prefetch_sp_fops);
	if (IS_ERR(file))
		goto err;

	file = debugfs_create_file("icache_prefetch_isp", 0600,
				   dir, psys, &psys_icache_prefetch_isp_fops);
	if (IS_ERR(file))
		goto err;

	psys->debugfsdir = dir;

#ifdef IPU_PSYS_GPC
	if (ipu_psys_gpc_init_debugfs(psys))
		return -ENOMEM;
#endif

	return 0;
err:
	debugfs_remove_recursive(dir);
	return -ENOMEM;
}
#endif

static int ipu_psys_sched_cmd(void *ptr)
{
	struct ipu_psys *psys = ptr;
	size_t pending = 0;

	while (1) {
		wait_event_interruptible(psys->sched_cmd_wq,
					 (kthread_should_stop() ||
					  (pending =
					   atomic_read(&psys->wakeup_count))));

		if (kthread_should_stop())
			break;

		if (pending == 0)
			continue;

		mutex_lock(&psys->mutex);
		atomic_set(&psys->wakeup_count, 0);
		ipu_psys_run_next(psys);
		mutex_unlock(&psys->mutex);
	}

	return 0;
}

static void start_sp(struct ipu_bus_device *adev)
{
	struct ipu_psys *psys = ipu_bus_get_drvdata(adev);
	void __iomem *spc_regs_base = psys->pdata->base +
	    psys->pdata->ipdata->hw_variant.spc_offset;
	u32 val = 0;

	val |= IPU_PSYS_SPC_STATUS_START |
	    IPU_PSYS_SPC_STATUS_RUN |
	    IPU_PSYS_SPC_STATUS_CTRL_ICACHE_INVALIDATE;
	val |= psys->icache_prefetch_sp ?
	    IPU_PSYS_SPC_STATUS_ICACHE_PREFETCH : 0;
	writel(val, spc_regs_base + IPU_PSYS_REG_SPC_STATUS_CTRL);
}

static int query_sp(struct ipu_bus_device *adev)
{
	struct ipu_psys *psys = ipu_bus_get_drvdata(adev);
	void __iomem *spc_regs_base = psys->pdata->base +
	    psys->pdata->ipdata->hw_variant.spc_offset;
	u32 val = readl(spc_regs_base + IPU_PSYS_REG_SPC_STATUS_CTRL);

	/* return true when READY == 1, START == 0 */
	val &= IPU_PSYS_SPC_STATUS_READY | IPU_PSYS_SPC_STATUS_START;

	return val == IPU_PSYS_SPC_STATUS_READY;
}

static int ipu_psys_fw_init(struct ipu_psys *psys)
{
	unsigned int size;
	struct ipu_fw_syscom_queue_config *queue_cfg;
	struct ipu_fw_syscom_queue_config fw_psys_event_queue_cfg[] = {
		{
			IPU_FW_PSYS_EVENT_QUEUE_SIZE,
			sizeof(struct ipu_fw_psys_event)
		}
	};
	struct ipu_fw_psys_srv_init server_init = {
		.ddr_pkg_dir_address = 0,
		.host_ddr_pkg_dir = NULL,
		.pkg_dir_size = 0,
		.icache_prefetch_sp = psys->icache_prefetch_sp,
		.icache_prefetch_isp = psys->icache_prefetch_isp,
	};
	struct ipu_fw_com_cfg fwcom = {
		.num_output_queues = IPU_FW_PSYS_N_PSYS_EVENT_QUEUE_ID,
		.output = fw_psys_event_queue_cfg,
		.specific_addr = &server_init,
		.specific_size = sizeof(server_init),
		.cell_start = start_sp,
		.cell_ready = query_sp,
		.buttress_boot_offset = SYSCOM_BUTTRESS_FW_PARAMS_PSYS_OFFSET,
	};
	int i;

	size = IPU6SE_FW_PSYS_N_PSYS_CMD_QUEUE_ID;
	if (ipu_ver == IPU_VER_6 || ipu_ver == IPU_VER_6EP || ipu_ver == IPU_VER_6EP_MTL)
		size = IPU6_FW_PSYS_N_PSYS_CMD_QUEUE_ID;

	queue_cfg = devm_kzalloc(&psys->adev->dev, sizeof(*queue_cfg) * size,
				 GFP_KERNEL);
	if (!queue_cfg)
		return -ENOMEM;

	for (i = 0; i < size; i++) {
		queue_cfg[i].queue_size = IPU_FW_PSYS_CMD_QUEUE_SIZE;
		queue_cfg[i].token_size = sizeof(struct ipu_fw_psys_cmd);
	}

	fwcom.input = queue_cfg;
	fwcom.num_input_queues = size;
	fwcom.dmem_addr = psys->pdata->ipdata->hw_variant.dmem_offset;

	psys->fwcom = ipu_fw_com_prepare(&fwcom, psys->adev, psys->pdata->base);
	if (!psys->fwcom) {
		dev_err(&psys->adev->dev, "psys fw com prepare failed\n");
		return -EIO;
	}

	return 0;
}

static void run_fw_init_work(struct work_struct *work)
{
	struct fw_init_task *task = (struct fw_init_task *)work;
	struct ipu_psys *psys = task->psys;
	int rval;

	rval = ipu_psys_fw_init(psys);

	if (rval) {
		dev_err(&psys->adev->dev, "FW init failed(%d)\n", rval);
		ipu_psys_remove(psys->adev);
	} else {
		dev_info(&psys->adev->dev, "FW init done\n");
	}
}

static int ipu_psys_probe(struct ipu_bus_device *adev)
{
	struct ipu_device *isp = adev->isp;
	struct ipu_psys_pg *kpg, *kpg0;
	struct ipu_psys *psys;
	unsigned int id;
	int i, rval = -E2BIG;

	/* firmware is not ready, so defer the probe */
	if (!isp->pkg_dir)
		return -EPROBE_DEFER;

	rval = ipu_mmu_hw_init(adev->mmu);
	if (rval)
		return rval;

	mutex_lock(&ipu_psys_mutex);

	id = find_next_zero_bit(ipu_psys_devices, IPU_PSYS_NUM_DEVICES, 0);
	if (id == IPU_PSYS_NUM_DEVICES) {
		dev_err(&adev->dev, "too many devices\n");
		goto out_unlock;
	}

	psys = devm_kzalloc(&adev->dev, sizeof(*psys), GFP_KERNEL);
	if (!psys) {
		rval = -ENOMEM;
		goto out_unlock;
	}

	psys->id = id;
	psys->adev = adev;
	psys->pdata = adev->pdata;
	psys->icache_prefetch_sp = 0;

	psys->power_gating = 0;

	ipu_trace_init(adev->isp, psys->pdata->base, &adev->dev,
		       psys_trace_blocks);

	spin_lock_init(&psys->ready_lock);
	spin_lock_init(&psys->pgs_lock);
	psys->ready = 0;
	psys->timeout = IPU_PSYS_CMD_TIMEOUT_MS;

	mutex_init(&psys->mutex);
	INIT_LIST_HEAD(&psys->instances);
	INIT_LIST_HEAD(&psys->pgs);
	INIT_LIST_HEAD(&psys->started_kcmds_list);

	init_waitqueue_head(&psys->sched_cmd_wq);
	atomic_set(&psys->wakeup_count, 0);
	/*
	 * Create a thread to schedule commands sent to IPU firmware.
	 * The thread reduces the coupling between the command scheduler
	 * and queueing commands from the user to driver.
	 */
	psys->sched_cmd_thread = kthread_run(ipu_psys_sched_cmd, psys,
					     "psys_sched_cmd");

	if (IS_ERR(psys->sched_cmd_thread)) {
		psys->sched_cmd_thread = NULL;
		mutex_destroy(&psys->mutex);
		goto out_unlock;
	}

	ipu_bus_set_drvdata(adev, psys);

	rval = ipu_psys_resource_pool_init(&psys->resource_pool_running);
	if (rval < 0) {
		dev_err(&psys->adev->dev,
			"unable to alloc process group resources\n");
		goto out_mutex_destroy;
	}

	ipu6_psys_hw_res_variant_init();
	psys->pkg_dir = isp->pkg_dir;
	psys->pkg_dir_dma_addr = isp->pkg_dir_dma_addr;
	psys->pkg_dir_size = isp->pkg_dir_size;
	psys->fw_sgt = isp->fw_sgt;

	/* allocate and map memory for process groups */
	for (i = 0; i < IPU_PSYS_PG_POOL_SIZE; i++) {
		kpg = kzalloc(sizeof(*kpg), GFP_KERNEL);
		if (!kpg)
			goto out_free_pgs;
		kpg->pg = dma_alloc_attrs(&adev->dev,
					  IPU_PSYS_PG_MAX_SIZE,
					  &kpg->pg_dma_addr,
					  GFP_KERNEL, 0);
		if (!kpg->pg) {
			kfree(kpg);
			goto out_free_pgs;
		}
		kpg->size = IPU_PSYS_PG_MAX_SIZE;
		list_add(&kpg->list, &psys->pgs);
	}

	psys->caps.pg_count = ipu_cpd_pkg_dir_get_num_entries(psys->pkg_dir);

	dev_info(&adev->dev, "pkg_dir entry count:%d\n", psys->caps.pg_count);
	if (async_fw_init) {
		INIT_DELAYED_WORK((struct delayed_work *)&fw_init_task,
				  run_fw_init_work);
		fw_init_task.psys = psys;
		schedule_delayed_work((struct delayed_work *)&fw_init_task, 0);
	} else {
		rval = ipu_psys_fw_init(psys);
	}

	if (rval) {
		dev_err(&adev->dev, "FW init failed(%d)\n", rval);
		goto out_free_pgs;
	}

	/* Add the hw stepping information to caps */
	strlcpy(psys->caps.dev_model, IPU_MEDIA_DEV_MODEL_NAME,
		sizeof(psys->caps.dev_model));

	pm_runtime_set_autosuspend_delay(&psys->adev->dev,
					 IPU_PSYS_AUTOSUSPEND_DELAY);
	pm_runtime_use_autosuspend(&psys->adev->dev);
	pm_runtime_mark_last_busy(&psys->adev->dev);

	set_bit(id, ipu_psys_devices);

	rval = ipu_kcam_init(adev, id);
	if (rval < 0) {
		dev_err(&adev->dev, "kcam initialization failed\n");
		goto out_release;
	}

	mutex_unlock(&ipu_psys_mutex);

#ifdef CONFIG_DEBUG_FS
	/* Debug fs failure is not fatal. */
	ipu_psys_init_debugfs(psys);
#endif

	adev->isp->cpd_fw_reload = &cpd_fw_reload;

	dev_info(&adev->dev, "psys probe id: %d\n", id);

	ipu_mmu_hw_cleanup(adev->mmu);

	return 0;

out_release:
	clear_bit(psys->id, ipu_psys_devices);
	ipu_fw_com_release(psys->fwcom, 1);
out_free_pgs:
	list_for_each_entry_safe(kpg, kpg0, &psys->pgs, list) {
		dma_free_attrs(&adev->dev, kpg->size, kpg->pg,
			       kpg->pg_dma_addr, 0);
		kfree(kpg);
	}

	ipu_psys_resource_pool_cleanup(&psys->resource_pool_running);
out_mutex_destroy:
	mutex_destroy(&psys->mutex);
	if (psys->sched_cmd_thread) {
		kthread_stop(psys->sched_cmd_thread);
		psys->sched_cmd_thread = NULL;
	}
out_unlock:
	/* Safe to call even if the init is not called */
	ipu_trace_uninit(&adev->dev);
	mutex_unlock(&ipu_psys_mutex);

	ipu_mmu_hw_cleanup(adev->mmu);

	return rval;
}

static void ipu_psys_remove(struct ipu_bus_device *adev)
{
	struct ipu_device *isp = adev->isp;
	struct ipu_psys *psys = ipu_bus_get_drvdata(adev);
	struct ipu_psys_pg *kpg, *kpg0;

	ipu_kcam_exit(adev);

#ifdef CONFIG_DEBUG_FS
	if (isp->ipu_dir)
		debugfs_remove_recursive(psys->debugfsdir);
#endif

	if (psys->sched_cmd_thread) {
		kthread_stop(psys->sched_cmd_thread);
		psys->sched_cmd_thread = NULL;
	}

	pm_runtime_dont_use_autosuspend(&psys->adev->dev);

	mutex_lock(&ipu_psys_mutex);

	list_for_each_entry_safe(kpg, kpg0, &psys->pgs, list) {
		dma_free_attrs(&adev->dev, kpg->size, kpg->pg,
			       kpg->pg_dma_addr, 0);
		kfree(kpg);
	}

	if (psys->fwcom && ipu_fw_com_release(psys->fwcom, 1))
		dev_err(&adev->dev, "fw com release failed.\n");

	kfree(psys->server_init);
	kfree(psys->syscom_config);

	ipu_trace_uninit(&adev->dev);

	ipu_psys_resource_pool_cleanup(&psys->resource_pool_running);

	clear_bit(psys->id, ipu_psys_devices);

	mutex_unlock(&ipu_psys_mutex);

	mutex_destroy(&psys->mutex);

	dev_info(&adev->dev, "removed\n");
}

static irqreturn_t psys_isr_threaded(struct ipu_bus_device *adev)
{
	struct ipu_psys *psys = ipu_bus_get_drvdata(adev);
	void __iomem *base = psys->pdata->base;
	u32 status;
	int r;

	mutex_lock(&psys->mutex);
#ifdef CONFIG_PM
	r = pm_runtime_get_if_in_use(&psys->adev->dev);
	if (!r || WARN_ON_ONCE(r < 0)) {
		mutex_unlock(&psys->mutex);
		return IRQ_NONE;
	}
#endif

	status = readl(base + IPU_REG_PSYS_GPDEV_IRQ_STATUS);
	writel(status, base + IPU_REG_PSYS_GPDEV_IRQ_CLEAR);

	if (status & IPU_PSYS_GPDEV_IRQ_FWIRQ(IPU_PSYS_GPDEV_FWIRQ0)) {
		writel(0, base + IPU_REG_PSYS_GPDEV_FWIRQ(0));
		ipu_psys_handle_events(psys);
	}

	pm_runtime_mark_last_busy(&psys->adev->dev);
	pm_runtime_put_autosuspend(&psys->adev->dev);
	mutex_unlock(&psys->mutex);

	return status ? IRQ_HANDLED : IRQ_NONE;
}

static struct ipu_bus_driver psys_driver = {
	.probe = ipu_psys_probe,
	.remove = ipu_psys_remove,
	.isr_threaded = psys_isr_threaded,
	.wanted = IPU_PSYS_NAME,
	.drv = {
		.name = IPU_PSYS_NAME,
		.owner = THIS_MODULE,
		.pm = PSYS_PM_OPS,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};

module_ipu_bus_driver(psys_driver);

static const struct pci_device_id ipu_pci_tbl[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, IPU6_PCI_ID)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, IPU6SE_PCI_ID)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, IPU6EP_ADL_P_PCI_ID)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, IPU6EP_ADL_N_PCI_ID)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, IPU6EP_RPL_P_PCI_ID)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, IPU6EP_MTL_PCI_ID)},
	{0,}
};
MODULE_DEVICE_TABLE(pci, ipu_pci_tbl);

MODULE_AUTHOR("Antti Laakso <antti.laakso@intel.com>");
MODULE_AUTHOR("Bin Han <bin.b.han@intel.com>");
MODULE_AUTHOR("Renwei Wu <renwei.wu@intel.com>");
MODULE_AUTHOR("Jianxu Zheng <jian.xu.zheng@intel.com>");
MODULE_AUTHOR("Xia Wu <xia.wu@intel.com>");
MODULE_AUTHOR("Bingbu Cao <bingbu.cao@intel.com>");
MODULE_AUTHOR("Zaikuo Wang <zaikuo.wang@intel.com>");
MODULE_AUTHOR("Yunliang Ding <yunliang.ding@intel.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Intel ipu processing system driver");
