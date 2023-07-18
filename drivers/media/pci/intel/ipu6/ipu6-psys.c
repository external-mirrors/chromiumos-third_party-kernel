// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020 Intel Corporation

#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/pm_runtime.h>
#include <linux/kthread.h>
#include <linux/init_task.h>
#include <linux/version.h>
#include <uapi/linux/sched/types.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cam/cam-entity.h>
#include <linux/cam/cam-buffer.h>

#include "ipu.h"
#include "ipu-psys.h"
#include "ipu6-ppg.h"
#include "ipu-platform-regs.h"
#include "ipu-trace.h"
#include "ipu-kcam.h"

MODULE_IMPORT_NS(DMA_BUF);

static bool early_pg_transfer;
module_param(early_pg_transfer, bool, 0664);
MODULE_PARM_DESC(early_pg_transfer,
		 "Copy PGs back to user after resource allocation");

bool enable_power_gating = true;
module_param(enable_power_gating, bool, 0664);
MODULE_PARM_DESC(enable_power_gating, "enable power gating");

struct ipu_trace_block psys_trace_blocks[] = {
	{
		.offset = IPU_TRACE_REG_PS_TRACE_UNIT_BASE,
		.type = IPU_TRACE_BLOCK_TUN,
	},
	{
		.offset = IPU_TRACE_REG_PS_SPC_EVQ_BASE,
		.type = IPU_TRACE_BLOCK_TM,
	},
	{
		.offset = IPU_TRACE_REG_PS_SPP0_EVQ_BASE,
		.type = IPU_TRACE_BLOCK_TM,
	},
	{
		.offset = IPU_TRACE_REG_PS_SPC_GPC_BASE,
		.type = IPU_TRACE_BLOCK_GPC,
	},
	{
		.offset = IPU_TRACE_REG_PS_SPP0_GPC_BASE,
		.type = IPU_TRACE_BLOCK_GPC,
	},
	{
		.offset = IPU_TRACE_REG_PS_MMU_GPC_BASE,
		.type = IPU_TRACE_BLOCK_GPC,
	},
	{
		.offset = IPU_TRACE_REG_PS_GPREG_TRACE_TIMER_RST_N,
		.type = IPU_TRACE_TIMER_RST,
	},
	{
		.type = IPU_TRACE_BLOCK_END,
	}
};

static void ipu6_set_sp_info_bits(void *base)
{
	int i;

	writel(IPU_INFO_REQUEST_DESTINATION_IOSF,
	       base + IPU_REG_PSYS_INFO_SEG_0_CONFIG_ICACHE_MASTER);

	for (i = 0; i < 4; i++)
		writel(IPU_INFO_REQUEST_DESTINATION_IOSF,
		       base + IPU_REG_PSYS_INFO_SEG_CMEM_MASTER(i));
	for (i = 0; i < 4; i++)
		writel(IPU_INFO_REQUEST_DESTINATION_IOSF,
		       base + IPU_REG_PSYS_INFO_SEG_XMEM_MASTER(i));
}

#define PSYS_SUBDOMAINS_STATUS_WAIT_COUNT        1000
void ipu_psys_subdomains_power(struct ipu_psys *psys, bool on)
{
	unsigned int i;
	u32 val;

	/* power domain req */
	dev_dbg(psys_to_device(psys), "power %s psys sub-domains",
		on ? "UP" : "DOWN");
	if (on)
		writel(IPU_PSYS_SUBDOMAINS_POWER_MASK,
		       psys->adev->isp->base + IPU_PSYS_SUBDOMAINS_POWER_REQ);
	else
		writel(0x0,
		       psys->adev->isp->base + IPU_PSYS_SUBDOMAINS_POWER_REQ);

	i = 0;
	do {
		usleep_range(10, 20);
		val = readl(psys->adev->isp->base +
			    IPU_PSYS_SUBDOMAINS_POWER_STATUS);
		if (!(val & BIT(31))) {
			dev_dbg(psys_to_device(psys),
				"PS sub-domains req done with status 0x%x",
				val);
			break;
		}
		i++;
	} while (i < PSYS_SUBDOMAINS_STATUS_WAIT_COUNT);

	if (i == PSYS_SUBDOMAINS_STATUS_WAIT_COUNT)
		dev_warn(psys_to_device(psys), "Psys sub-domains %s req timeout!",
			 on ? "UP" : "DOWN");
}

void ipu_psys_setup_hw(struct ipu_psys *psys)
{
	void __iomem *base = psys->pdata->base;
	void __iomem *spc_regs_base =
	    base + psys->pdata->ipdata->hw_variant.spc_offset;
	void *psys_iommu0_ctrl;
	u32 irqs;
	const u8 r3 = IPU_DEVICE_AB_GROUP1_TARGET_ID_R3_SPC_STATUS_REG;
	const u8 r4 = IPU_DEVICE_AB_GROUP1_TARGET_ID_R4_SPC_MASTER_BASE_ADDR;
	const u8 r5 = IPU_DEVICE_AB_GROUP1_TARGET_ID_R5_SPC_PC_STALL;

	if (!psys->adev->isp->secure_mode) {
		/* configure access blocker for non-secure mode */
		writel(NCI_AB_ACCESS_MODE_RW,
		       base + IPU_REG_DMA_TOP_AB_GROUP1_BASE_ADDR +
		       IPU_REG_DMA_TOP_AB_RING_ACCESS_OFFSET(r3));
		writel(NCI_AB_ACCESS_MODE_RW,
		       base + IPU_REG_DMA_TOP_AB_GROUP1_BASE_ADDR +
		       IPU_REG_DMA_TOP_AB_RING_ACCESS_OFFSET(r4));
		writel(NCI_AB_ACCESS_MODE_RW,
		       base + IPU_REG_DMA_TOP_AB_GROUP1_BASE_ADDR +
		       IPU_REG_DMA_TOP_AB_RING_ACCESS_OFFSET(r5));
	}
	psys_iommu0_ctrl = base +
		psys->pdata->ipdata->hw_variant.mmu_hw[0].offset +
		IPU_MMU_INFO_OFFSET;
	writel(IPU_INFO_REQUEST_DESTINATION_IOSF, psys_iommu0_ctrl);

	ipu6_set_sp_info_bits(spc_regs_base + IPU_PSYS_REG_SPC_STATUS_CTRL);
	ipu6_set_sp_info_bits(spc_regs_base + IPU_PSYS_REG_SPP0_STATUS_CTRL);

	/* Enable FW interrupt #0 */
	writel(0, base + IPU_REG_PSYS_GPDEV_FWIRQ(0));
	irqs = IPU_PSYS_GPDEV_IRQ_FWIRQ(IPU_PSYS_GPDEV_FWIRQ0);
	writel(irqs, base + IPU_REG_PSYS_GPDEV_IRQ_EDGE);
	writel(irqs, base + IPU_REG_PSYS_GPDEV_IRQ_LEVEL_NOT_PULSE);
	writel(0xffffffff, base + IPU_REG_PSYS_GPDEV_IRQ_CLEAR);
	writel(irqs, base + IPU_REG_PSYS_GPDEV_IRQ_MASK);
	writel(irqs, base + IPU_REG_PSYS_GPDEV_IRQ_ENABLE);
}

static struct ipu_psys_ppg *ipu_psys_identify_kppg(struct ipu_psys_kcmd *kcmd)
{
	struct ipu_kcam_psys_instance *instance;
	struct ipu_psys_scheduler *sched;
	struct ipu_psys_ppg *kppg, *tmp;

	instance = cam_instance_driver_data(kcmd->kcam_instance);
	sched = &instance->sched;

	mutex_lock(&instance->mutex);
	if (list_empty(&sched->ppgs))
		goto not_found;

	list_for_each_entry_safe(kppg, tmp, &sched->ppgs, list) {
		if (ipu_fw_psys_pg_get_token(kcmd->kpg) != kppg->token)
			continue;
		mutex_unlock(&instance->mutex);
		return kppg;
	}

not_found:
	mutex_unlock(&instance->mutex);
	return NULL;
}

/*
 * Called to free up all resources associated with a kcmd.
 * After this the kcmd doesn't anymore exist in the driver.
 */
static void ipu_psys_kcmd_free(struct ipu_psys_kcmd *kcmd)
{
	struct ipu_kcam_psys_instance *instance;
	struct ipu_psys_ppg *kppg;
	struct ipu_psys_scheduler *sched;
	int i;

	if (!kcmd)
		return;

	instance = cam_instance_driver_data(kcmd->kcam_instance);

	kppg = ipu_psys_identify_kppg(kcmd);
	sched = &instance->sched;

	if (kcmd->kbuf_set) {
		mutex_lock(&sched->bs_mutex);
		kcmd->kbuf_set->buf_set_size = 0;
		mutex_unlock(&sched->bs_mutex);
		kcmd->kbuf_set = NULL;
	}

	if (kppg) {
		mutex_lock(&kppg->mutex);
		if (!list_empty(&kcmd->list))
			list_del(&kcmd->list);
		mutex_unlock(&kppg->mutex);
	}

	if (kcmd->kcam_instance)
		cam_instance_put(kcmd->kcam_instance);

	if (kcmd->kcam_pg_buffer)
		cam_buffer_put(kcmd->kcam_pg_buffer);

	if (kcmd->kcam_buffers) {
		for (i = 0; i < kcmd->nbuffers; i++) {
			if (!kcmd->kcam_buffers[i])
				break;
			cam_buffer_put(kcmd->kcam_buffers[i]);
		}
		kfree(kcmd->kcam_buffers);
	}

	kfree(kcmd->pg_manifest);
	kfree(kcmd->buffers);
	kfree(kcmd);
}

static struct ipu_psys_kcmd *
ipu_psys_copy_cmd(struct ipu_psys_command *cmd,
		  struct cam_obj_instance *kcam_instance,
		  struct cam_obj_buffer **kcam_buffers)
{
	struct ipu_kcam_psys_instance *psys_instance;
	struct ipu_psys *psys = NULL;
	struct ipu_psys_kcmd *kcmd;
	struct ipu_kcam_psys_dbuf *psys_pg_buf, *psys_buf;
	unsigned int i;
	int ret;

	if (cmd->bufcount > IPU_MAX_PSYS_CMD_BUFFERS)
		return NULL;

	if (!cmd->pg_manifest_size)
		return NULL;

	if (!kcam_buffers)
		return NULL;

	kcmd = kzalloc(sizeof(*kcmd), GFP_KERNEL);
	if (!kcmd)
		return NULL;

	if (!cam_instance_get(kcam_instance))
		goto error;
	kcmd->kcam_instance = kcam_instance;

	psys_instance = cam_instance_driver_data(kcam_instance);
	psys = psys_instance->psys;

	kcmd->state = KCMD_STATE_PPG_NEW;
	INIT_LIST_HEAD(&kcmd->list);

	/*
	 * increment the reference count of the buffer object to indirectly keep
	 * the mapping alive
	 */
	if (!cam_buffer_get(kcam_buffers[0]))
		goto error;
	kcmd->kcam_pg_buffer = kcam_buffers[0];

	psys_pg_buf = cam_buffer_driver_data(kcam_buffers[0]);
	kcmd->pg_user = psys_pg_buf->va;
	kcmd->kpg = __get_pg_buf(psys, psys_pg_buf->dma_buf->size);
	if (!kcmd->kpg)
		goto error;
	memcpy(kcmd->kpg->pg, kcmd->pg_user, kcmd->kpg->pg_size);

	kcmd->pg_manifest = kzalloc(cmd->pg_manifest_size, GFP_KERNEL);
	if (!kcmd->pg_manifest)
		goto error;
	ret = copy_from_user(kcmd->pg_manifest, cmd->pg_manifest,
			     cmd->pg_manifest_size);
	if (ret)
		goto error;
	kcmd->pg_manifest_size = cmd->pg_manifest_size;

	kcmd->user_token = cmd->user_token;
	kcmd->issue_id = cmd->issue_id;
	kcmd->priority = cmd->priority;
	if (kcmd->priority >= IPU_PSYS_CMD_PRIORITY_NUM)
		goto error;

	/*
	 * Kernel enable bitmap be used only.
	 */
	memcpy(kcmd->kernel_enable_bitmap, cmd->kernel_enable_bitmap,
	       sizeof(cmd->kernel_enable_bitmap));

	/* stopping doesn't need any buffers */
	if (cmd->type == CMD_TYPE_STOP) {
		kcmd->state = KCMD_STATE_PPG_STOP;
		return kcmd;
	}

	kcmd->nbuffers = ipu_fw_psys_pg_get_terminal_count(kcmd->kpg);
	kcmd->buffers = kcalloc(kcmd->nbuffers, sizeof(*kcmd->buffers),
				GFP_KERNEL);
	if (!kcmd->buffers)
		goto error;

	if (!cmd->bufcount || kcmd->nbuffers > cmd->bufcount)
		goto error;
	ret = copy_from_user(kcmd->buffers, cmd->buffers,
			     kcmd->nbuffers * sizeof(*kcmd->buffers));
	if (ret)
		goto error;

	/* starting needs buffer information, but there is no actual buffer */
	if (cmd->type == CMD_TYPE_START) {
		kcmd->state = KCMD_STATE_PPG_START;
		return kcmd;
	}

	kcmd->kcam_buffers = kcalloc(kcmd->nbuffers,
				     sizeof(kcmd->kcam_buffers[0]),
				     GFP_KERNEL);
	if (!kcmd->kcam_buffers)
		goto error;

	for (i = 0; i < kcmd->nbuffers; i++) {
		struct ipu_fw_psys_terminal *terminal;

		terminal = ipu_fw_psys_pg_get_terminal(kcmd->kpg, i);
		if (!terminal)
			continue;

		if (!cam_buffer_get(kcam_buffers[i + 1]))
			goto error;
		kcmd->kcam_buffers[i] = kcam_buffers[i + 1];
		psys_buf = cam_buffer_driver_data(kcam_buffers[i + 1]);
		if (!psys_buf ||
		    !psys_buf->dma_sgt ||
		    psys_buf->dma_buf->size < kcmd->buffers[i].bytes_used)
			goto error;
		if (kcmd->buffers[i].flags & IPU_BUFFER_FLAG_NO_FLUSH)
			continue;

		dma_sync_sg_for_device(psys_to_device(psys),
				       psys_buf->dma_sgt->sgl,
				       psys_buf->dma_sgt->orig_nents,
				       DMA_BIDIRECTIONAL);
	}

	kcmd->state = KCMD_STATE_PPG_ENQUEUE;

	return kcmd;

error:
	if (psys)
		dev_err(psys_to_device(psys), "failed to copy cmd\n");

	ipu_psys_kcmd_free(kcmd);

	return NULL;
}

static struct ipu_psys_buffer_set *
ipu_psys_lookup_kbuffer_set(struct ipu_psys *psys, u32 addr)
{
	struct ipu_kcam_psys_instance *instance;
	struct ipu_psys_buffer_set *kbuf_set;
	struct ipu_psys_scheduler *sched;

	list_for_each_entry(instance, &psys->instances, list) {
		sched = &instance->sched;
		mutex_lock(&sched->bs_mutex);
		list_for_each_entry(kbuf_set, &sched->buf_sets, list) {
			if (kbuf_set->buf_set &&
			    kbuf_set->buf_set->ipu_virtual_address == addr) {
				mutex_unlock(&sched->bs_mutex);
				return kbuf_set;
			}
		}
		mutex_unlock(&sched->bs_mutex);
	}

	return NULL;
}

static struct ipu_psys_ppg *ipu_psys_lookup_ppg(struct ipu_psys *psys,
						dma_addr_t pg_addr)
{
	struct ipu_kcam_psys_instance *instance;
	struct ipu_psys_scheduler *sched;
	struct ipu_psys_ppg *kppg, *tmp;

	list_for_each_entry(instance, &psys->instances, list) {
		sched = &instance->sched;
		mutex_lock(&instance->mutex);
		if (list_empty(&sched->ppgs)) {
			mutex_unlock(&instance->mutex);
			continue;
		}

		list_for_each_entry_safe(kppg, tmp, &sched->ppgs, list) {
			if (pg_addr != kppg->kpg->pg_dma_addr)
				continue;
			mutex_unlock(&instance->mutex);
			return kppg;
		}
		mutex_unlock(&instance->mutex);
	}

	return NULL;
}

/*
 * Move kcmd into completed state (due to running finished or failure).
 * Fill up the event struct
 */
void ipu_psys_kcmd_complete(struct ipu_psys_ppg *kppg,
			    struct ipu_psys_kcmd *kcmd, int error)
{
	struct ipu_kcam_psys_instance *instance;
	struct ipu_psys *psys;

	instance = cam_instance_driver_data(kcmd->kcam_instance);
	psys = instance->psys;

	kcmd->ev.type = IPU_PSYS_EVENT_TYPE_CMD_COMPLETE;
	kcmd->ev.user_token = kcmd->user_token;
	kcmd->ev.issue_id = kcmd->issue_id;
	kcmd->ev.error = error;
	list_move_tail(&kcmd->list, &kppg->kcmds_finished_list);

	if (kcmd->constraint.min_freq)
		ipu_buttress_remove_psys_constraint(psys->adev->isp,
						    &kcmd->constraint);

	/*
	 * the mapping for the buffer should be alive, since we keep the
	 * reference count > 0 until kcmd is freed
	 */
	if (!early_pg_transfer && kcmd->pg_user && kcmd->kpg->pg)
		memcpy(kcmd->pg_user, kcmd->kpg->pg, kcmd->kpg->pg_size);

	kcmd->state = KCMD_STATE_PPG_COMPLETE;
	cam_instance_event_trigger_signals(psys->kcam_entity,
					   kcmd->kcam_instance,
					   psys->kcam_event);
}

/*
 * Submit kcmd into psys queue. If running fails, complete the kcmd
 * with an error.
 *
 * Found a runnable PG. Move queue to the list tail for round-robin
 * scheduling and run the PG. Start the watchdog timer if the PG was
 * started successfully. Enable PSYS power if requested.
 */
int ipu_psys_kcmd_start(struct ipu_psys *psys, struct ipu_psys_kcmd *kcmd)
{
	int ret;

	if (psys->adev->isp->flr_done)
		return -EIO;

	if (early_pg_transfer && kcmd->pg_user && kcmd->kpg->pg)
		memcpy(kcmd->pg_user, kcmd->kpg->pg, kcmd->kpg->pg_size);

	ret = ipu_fw_psys_pg_start(kcmd->kpg);
	if (ret) {
		dev_err(psys_to_device(psys), "failed to start kcmd!\n");
		return ret;
	}

	ipu_fw_psys_pg_dump(psys, kcmd->kpg, "run");

	ret = ipu_fw_psys_pg_disown(kcmd->kpg, psys);
	if (ret) {
		if (ret == -ENODATA)
			kcmd->pg_user = NULL;
		dev_err(psys_to_device(psys), "failed to start kcmd!\n");
		return ret;
	}

	return 0;
}

static int ipu_psys_kcmd_send_to_ppg_start(struct ipu_psys_kcmd *kcmd)
{
	struct ipu_kcam_psys_instance *instance;
	struct ipu_psys_scheduler *sched;
	struct ipu_psys *psys;
	struct ipu_psys_ppg *kppg;
	struct ipu_psys_resource_pool *rpr;
	int queue_id;
	int ret;

	instance = cam_instance_driver_data(kcmd->kcam_instance);
	sched = &instance->sched;
	psys = instance->psys;

	rpr = &psys->resource_pool_running;

	kppg = kzalloc(sizeof(*kppg), GFP_KERNEL);
	if (!kppg)
		return -ENOMEM;

	kppg->kpg = kcmd->kpg;
	kppg->state = PPG_STATE_START;
	kppg->pri_base = kcmd->priority;
	kppg->pri_dynamic = 0;
	kppg->psys = psys;
	INIT_LIST_HEAD(&kppg->list);

	mutex_init(&kppg->mutex);
	INIT_LIST_HEAD(&kppg->kcmds_new_list);
	INIT_LIST_HEAD(&kppg->kcmds_processing_list);
	INIT_LIST_HEAD(&kppg->kcmds_finished_list);
	INIT_LIST_HEAD(&kppg->sched_list);

	kppg->manifest = kzalloc(kcmd->pg_manifest_size, GFP_KERNEL);
	if (!kppg->manifest) {
		kfree(kppg);
		return -ENOMEM;
	}
	memcpy(kppg->manifest, kcmd->pg_manifest,
	       kcmd->pg_manifest_size);

	queue_id = ipu_psys_allocate_cmd_queue_resource(rpr);
	if (queue_id == -ENOSPC) {
		dev_err(psys_to_device(psys), "no available queue\n");
		kfree(kppg->manifest);
		kfree(kppg);
		return -ENOMEM;
	}

	/*
	 * set token as start cmd will immediately be followed by a
	 * enqueue cmd so that kppg could be retrieved.
	 */
	kppg->token = (u64)kcmd->kpg;
	ipu_fw_psys_pg_set_token(kcmd->kpg, kppg->token);
	ipu_fw_psys_ppg_set_base_queue_id(kcmd, queue_id);
	ret = ipu_fw_psys_pg_set_ipu_vaddress(kcmd->kpg,
					      kcmd->kpg->pg_dma_addr);
	if (ret) {
		ipu_psys_free_cmd_queue_resource(rpr, queue_id);
		kfree(kppg->manifest);
		kfree(kppg);
		return -EIO;
	}
	memcpy(kcmd->pg_user, kcmd->kpg->pg, kcmd->kpg->pg_size);

	mutex_lock(&instance->mutex);
	list_add_tail(&kppg->list, &sched->ppgs);
	mutex_unlock(&instance->mutex);

	mutex_lock(&kppg->mutex);
	list_add(&kcmd->list, &kppg->kcmds_new_list);
	mutex_unlock(&kppg->mutex);

	dev_dbg(psys_to_device(psys),
		"START ppg(%d, 0x%p) kcmd 0x%p, queue %d\n",
		ipu_fw_psys_pg_get_id(kcmd->kpg), kppg, kcmd, queue_id);

	/* Kick l-scheduler thread */
	atomic_set(&psys->wakeup_count, 1);
	wake_up_interruptible(&psys->sched_cmd_wq);

	return 0;
}

static int ipu_psys_kcmd_send_to_ppg(struct ipu_psys_kcmd *kcmd)
{
	struct ipu_kcam_psys_instance *instance;
	struct ipu_psys *psys;
	struct ipu_psys_ppg *kppg;
	struct ipu_psys_resource_pool *rpr;
	unsigned long flags;
	u8 id;
	bool resche = true;

	if (kcmd->state == KCMD_STATE_PPG_START)
		return ipu_psys_kcmd_send_to_ppg_start(kcmd);

	instance = cam_instance_driver_data(kcmd->kcam_instance);
	psys = instance->psys;
	rpr = &psys->resource_pool_running;

	kppg = ipu_psys_identify_kppg(kcmd);
	spin_lock_irqsave(&psys->pgs_lock, flags);
	kcmd->kpg->pg_size = 0;
	spin_unlock_irqrestore(&psys->pgs_lock, flags);
	if (!kppg) {
		dev_err(psys_to_device(psys), "token not match\n");
		return -EINVAL;
	}

	kcmd->kpg = kppg->kpg;

	dev_dbg(psys_to_device(psys), "%s ppg(%d, 0x%p) kcmd %p\n",
		(kcmd->state == KCMD_STATE_PPG_STOP) ?
		"STOP" : "ENQUEUE",
		ipu_fw_psys_pg_get_id(kcmd->kpg), kppg, kcmd);

	if (kcmd->state == KCMD_STATE_PPG_STOP) {
		mutex_lock(&kppg->mutex);
		if (kppg->state == PPG_STATE_STOPPED) {
			dev_dbg(psys_to_device(psys),
				"kppg 0x%p  stopped!\n", kppg);
			id = ipu_fw_psys_ppg_get_base_queue_id(kppg);
			ipu_psys_free_cmd_queue_resource(rpr, id);
			ipu_psys_kcmd_complete(kppg, kcmd, 0);
			pm_runtime_put(psys_to_device(psys));
			resche = false;
		} else {
			list_add(&kcmd->list, &kppg->kcmds_new_list);
		}
		mutex_unlock(&kppg->mutex);
	} else {
		int ret;

		ret = ipu_psys_ppg_get_bufset(kcmd);
		if (ret)
			return ret;

		mutex_lock(&kppg->mutex);
		list_add_tail(&kcmd->list, &kppg->kcmds_new_list);
		mutex_unlock(&kppg->mutex);
	}

	if (resche) {
		/* Kick l-scheduler thread */
		atomic_set(&psys->wakeup_count, 1);
		wake_up_interruptible(&psys->sched_cmd_wq);
	}
	return 0;
}

int ipu_psys_kcmd_new(struct ipu_psys_command *cmd,
		      struct ipu_bus_device *adev,
		      struct cam_obj_instance *kcam_instance,
		      struct cam_obj_buffer **kcam_buffers)
{
	struct ipu_psys_kcmd *kcmd;
	size_t pg_size;
	int ret;

	if (adev->isp->flr_done)
		return -EIO;

	kcmd = ipu_psys_copy_cmd(cmd, kcam_instance, kcam_buffers);
	if (!kcmd) {
		ret = -EINVAL;
		goto error;
	}

	pg_size = ipu_fw_psys_pg_get_size(kcmd->kpg);
	if (pg_size > kcmd->kpg->pg_size) {
		dev_dbg(&adev->auxdev.dev, "pg size mismatch %lu %lu\n",
			pg_size, kcmd->kpg->pg_size);
		ret = -EINVAL;
		goto error;
	}

	if (ipu_fw_psys_pg_get_protocol(kcmd->kpg) !=
			IPU_FW_PSYS_PROCESS_GROUP_PROTOCOL_PPG) {
		dev_err(&adev->auxdev.dev, "No support legacy pg now\n");
		ret = -EINVAL;
		goto error;
	}

	if (cmd->min_psys_freq) {
		kcmd->constraint.min_freq = cmd->min_psys_freq;
		ipu_buttress_add_psys_constraint(adev->isp,
						 &kcmd->constraint);
	}

	ret = ipu_psys_kcmd_send_to_ppg(kcmd);
	if (ret)
		goto error;

	dev_dbg(&adev->auxdev.dev,
		"IOC_QCMD: user_token:%llx issue_id:0x%llx pri:%d\n",
		cmd->user_token, cmd->issue_id, cmd->priority);

	return 0;

error:
	ipu_psys_kcmd_free(kcmd);

	return ret;
}

static bool ipu_psys_kcmd_is_valid(struct ipu_psys *psys,
				   struct ipu_psys_kcmd *kcmd)
{
	struct ipu_kcam_psys_instance *instance;
	struct ipu_psys_kcmd *kcmd0;
	struct ipu_psys_ppg *kppg, *tmp;
	struct ipu_psys_scheduler *sched;

	list_for_each_entry(instance, &psys->instances, list) {
		sched = &instance->sched;
		mutex_lock(&instance->mutex);
		if (list_empty(&sched->ppgs)) {
			mutex_unlock(&instance->mutex);
			continue;
		}
		list_for_each_entry_safe(kppg, tmp, &sched->ppgs, list) {
			mutex_lock(&kppg->mutex);
			list_for_each_entry(kcmd0,
					    &kppg->kcmds_processing_list,
					    list) {
				if (kcmd0 == kcmd) {
					mutex_unlock(&kppg->mutex);
					mutex_unlock(&instance->mutex);
					return true;
				}
			}
			mutex_unlock(&kppg->mutex);
		}
		mutex_unlock(&instance->mutex);
	}

	return false;
}

void ipu_psys_handle_events(struct ipu_psys *psys)
{
	struct ipu_psys_kcmd *kcmd;
	struct ipu_fw_psys_event event;
	struct ipu_psys_ppg *kppg;
	bool error;
	u32 hdl;
	u16 cmd, status;
	int res;

	do {
		memset(&event, 0, sizeof(event));
		if (!ipu_fw_psys_rcv_event(psys, &event))
			break;

		if (!event.context_handle)
			break;

		dev_dbg(psys_to_device(psys),
			"ppg event: 0x%x, %d, status %d\n",
			event.context_handle, event.command, event.status);

		error = false;
		/*
		 * event.command == CMD_RUN shows this is fw processing frame
		 * done as pPG mode, and event.context_handle should be pointer
		 * of buffer set; so we make use of this pointer to lookup
		 * kbuffer_set and kcmd
		 */
		hdl = event.context_handle;
		cmd = event.command;
		status = event.status;

		kppg = NULL;
		kcmd = NULL;
		if (cmd == IPU_FW_PSYS_PROCESS_GROUP_CMD_RUN) {
			struct ipu_psys_buffer_set *kbuf_set;
			/*
			 * Need change ppg state when the 1st running is done
			 * (after PPG started/resumed)
			 */
			kbuf_set = ipu_psys_lookup_kbuffer_set(psys, hdl);
			if (kbuf_set)
				kcmd = kbuf_set->kcmd;
			if (!kbuf_set || !kcmd)
				error = true;
			else
				kppg = ipu_psys_identify_kppg(kcmd);
		} else if (cmd == IPU_FW_PSYS_PROCESS_GROUP_CMD_STOP ||
			   cmd == IPU_FW_PSYS_PROCESS_GROUP_CMD_SUSPEND ||
			   cmd == IPU_FW_PSYS_PROCESS_GROUP_CMD_RESUME) {
			/*
			 * STOP/SUSPEND/RESUME cmd event would run this branch;
			 * only stop cmd queued by user has stop_kcmd and need
			 * to notify user to dequeue.
			 */
			kppg = ipu_psys_lookup_ppg(psys, hdl);
			if (kppg) {
				mutex_lock(&kppg->mutex);
				if (kppg->state == PPG_STATE_STOPPING) {
					kcmd = ipu_psys_ppg_get_stop_kcmd(kppg);
					if (!kcmd)
						error = true;
				}
				mutex_unlock(&kppg->mutex);
			}
		} else {
			dev_err(psys_to_device(psys), "invalid event\n");
			continue;
		}

		if (error || !kppg) {
			dev_err(psys_to_device(psys), "event error, command %d\n",
				cmd);
			break;
		}

		dev_dbg(psys_to_device(psys), "event to kppg 0x%p, kcmd 0x%p\n",
			kppg, kcmd);

		ipu_psys_ppg_complete(psys, kppg);

		if (kcmd && ipu_psys_kcmd_is_valid(psys, kcmd)) {
			res = (status == IPU_PSYS_EVENT_CMD_COMPLETE ||
			       status == IPU_PSYS_EVENT_FRAGMENT_COMPLETE) ?
				0 : -EIO;
			mutex_lock(&kppg->mutex);
			ipu_psys_kcmd_complete(kppg, kcmd, res);
			mutex_unlock(&kppg->mutex);
		}
	} while (1);
}

int ipu_psys_instance_init(struct ipu_kcam_psys_instance *instance)
{
	struct ipu_psys *psys = instance->psys;
	struct ipu_psys_buffer_set *kbuf_set, *kbuf_set_tmp;
	struct ipu_psys_scheduler *sched = &instance->sched;
	int i;

	mutex_init(&sched->bs_mutex);
	INIT_LIST_HEAD(&sched->buf_sets);
	INIT_LIST_HEAD(&sched->ppgs);
	pm_runtime_dont_use_autosuspend(psys_to_device(psys));
	/* allocate and map memory for buf_sets */
	for (i = 0; i < IPU_PSYS_BUF_SET_POOL_SIZE; i++) {
		kbuf_set = kzalloc(sizeof(*kbuf_set), GFP_KERNEL);
		if (!kbuf_set)
			goto out_free_buf_sets;
		kbuf_set->kaddr = dma_alloc_attrs(psys_to_device(psys),
						  IPU_PSYS_BUF_SET_MAX_SIZE,
						  &kbuf_set->dma_addr,
						  GFP_KERNEL,
						  0);
		if (!kbuf_set->kaddr) {
			kfree(kbuf_set);
			goto out_free_buf_sets;
		}
		kbuf_set->size = IPU_PSYS_BUF_SET_MAX_SIZE;
		list_add(&kbuf_set->list, &sched->buf_sets);
	}

	return 0;

out_free_buf_sets:
	list_for_each_entry_safe(kbuf_set, kbuf_set_tmp,
				 &sched->buf_sets, list) {
		dma_free_attrs(psys_to_device(psys),
			       kbuf_set->size, kbuf_set->kaddr,
			       kbuf_set->dma_addr, 0);
		list_del(&kbuf_set->list);
		kfree(kbuf_set);
	}
	mutex_destroy(&sched->bs_mutex);

	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(ipu_psys_instance_init);

int ipu_psys_instance_deinit(struct ipu_kcam_psys_instance *instance)
{
	struct ipu_psys *psys = instance->psys;
	struct ipu_psys_ppg *kppg, *kppg0;
	struct ipu_psys_kcmd *kcmd, *kcmd0;
	struct ipu_psys_buffer_set *kbuf_set, *kbuf_set0;
	struct ipu_psys_scheduler *sched = &instance->sched;
	struct ipu_psys_resource_pool *rpr;
	struct ipu_psys_resource_alloc *alloc;
	u8 id;

	mutex_lock(&instance->mutex);
	if (!list_empty(&sched->ppgs)) {
		list_for_each_entry_safe(kppg, kppg0, &sched->ppgs, list) {
			unsigned long flags;

			mutex_lock(&kppg->mutex);
			if (!(kppg->state &
			      (PPG_STATE_STOPPED | PPG_STATE_STOPPING))) {

				rpr = &psys->resource_pool_running;
				alloc = &kppg->kpg->resource_alloc;
				id = ipu_fw_psys_ppg_get_base_queue_id(kppg);
				ipu_psys_ppg_stop(kppg);
				ipu_psys_free_resources(alloc, rpr);
				ipu_psys_free_cmd_queue_resource(rpr, id);
				dev_dbg(psys_to_device(psys),
					"s_change:%s %p %d -> %d\n", __func__,
					kppg, kppg->state, PPG_STATE_STOPPED);
				kppg->state = PPG_STATE_STOPPED;
				if (psys->power_gating != PSYS_POWER_GATED)
					pm_runtime_put(psys_to_device(psys));
			}
			list_del(&kppg->list);
			mutex_unlock(&kppg->mutex);

			list_for_each_entry_safe(kcmd, kcmd0,
						 &kppg->kcmds_new_list, list) {
				kcmd->pg_user = NULL;
				mutex_unlock(&instance->mutex);
				ipu_psys_kcmd_free(kcmd);
				mutex_lock(&instance->mutex);
			}

			list_for_each_entry_safe(kcmd, kcmd0,
						 &kppg->kcmds_processing_list,
						 list) {
				kcmd->pg_user = NULL;
				mutex_unlock(&instance->mutex);
				ipu_psys_kcmd_free(kcmd);
				mutex_lock(&instance->mutex);
			}

			list_for_each_entry_safe(kcmd, kcmd0,
						 &kppg->kcmds_finished_list,
						 list) {
				kcmd->pg_user = NULL;
				mutex_unlock(&instance->mutex);
				ipu_psys_kcmd_free(kcmd);
				mutex_lock(&instance->mutex);
			}

			spin_lock_irqsave(&psys->pgs_lock, flags);
			kppg->kpg->pg_size = 0;
			spin_unlock_irqrestore(&psys->pgs_lock, flags);

			mutex_destroy(&kppg->mutex);
			kfree(kppg->manifest);
			kfree(kppg);
		}
	}
	mutex_unlock(&instance->mutex);

	mutex_lock(&sched->bs_mutex);
	list_for_each_entry_safe(kbuf_set, kbuf_set0, &sched->buf_sets, list) {
		dma_free_attrs(psys_to_device(psys),
			       kbuf_set->size, kbuf_set->kaddr,
			       kbuf_set->dma_addr, 0);
		list_del(&kbuf_set->list);
		kfree(kbuf_set);
	}
	mutex_unlock(&sched->bs_mutex);
	mutex_destroy(&sched->bs_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(ipu_psys_instance_deinit);

struct ipu_psys_kcmd *
ipu_get_completed_kcmd(struct ipu_kcam_psys_instance *instance)
{
	struct ipu_psys_scheduler *sched = &instance->sched;
	struct ipu_psys_kcmd *kcmd;
	struct ipu_psys_ppg *kppg;

	mutex_lock(&instance->mutex);
	if (list_empty(&sched->ppgs)) {
		mutex_unlock(&instance->mutex);
		return NULL;
	}

	list_for_each_entry(kppg, &sched->ppgs, list) {
		mutex_lock(&kppg->mutex);
		if (list_empty(&kppg->kcmds_finished_list)) {
			mutex_unlock(&kppg->mutex);
			continue;
		}

		kcmd = list_first_entry(&kppg->kcmds_finished_list,
					struct ipu_psys_kcmd, list);
		mutex_unlock(&kppg->mutex);
		mutex_unlock(&instance->mutex);
		dev_dbg(psys_to_device(instance->psys),
			"get completed kcmd 0x%p\n", kcmd);
		return kcmd;
	}
	mutex_unlock(&instance->mutex);

	return NULL;
}

long ipu_ioctl_dqevent(struct ipu_psys_event *event,
		       struct ipu_kcam_psys_instance *instance)
{
	struct ipu_psys *psys = instance->psys;
	struct ipu_psys_kcmd *kcmd = NULL;

	dev_dbg(psys_to_device(psys), "IOC_DQEVENT\n");

	kcmd = ipu_get_completed_kcmd(instance);
	if (!kcmd)
		return -ERESTARTSYS;

	*event = kcmd->ev;
	ipu_psys_kcmd_free(kcmd);

	return 0;
}
