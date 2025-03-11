// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 MediaTek Inc.
 */

#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/of.h>

#include "mcupm_plt.h"
#include "mcupm_driver.h"
#include "mcupm_ipi_id.h"

/* import from mcupm_driver */
extern int mcupm_plt_ackdata;

static ssize_t mcupm_alive_show(struct device *kobj,
				 struct device_attribute *attr, char *buf)
{

	struct mcupm_ipi_data_s ipi_data;
	int ret = 0;

	ipi_data.cmd = 0xDEAD;
	mcupm_plt_ackdata = 0;

	ret = mtk_ipi_send_compl(&mcupm_ipidev, CH_S_PLATFORM, IPI_SEND_WAIT,
		&ipi_data,
		sizeof(struct mcupm_ipi_data_s) / MCUPM_MBOX_SLOT_SIZE,
		2000);

	return snprintf(buf, PAGE_SIZE, "%s MBOX%d ret=%d XXX\n",
			mcupm_plt_ackdata ? "Alive" : "Dead", CH_S_PLATFORM, ret);
}
DEVICE_ATTR_RO(mcupm_alive);

static ssize_t mcdi_dynamic_finegrain_show(struct device *kobj,
				 struct device_attribute *attr, char *buf)
{

	struct mcupm_ipi_data_s mcdi_ipi_data;
	int ret = 0;

	mcdi_ipi_data.cmd = 0xA1;
	mcupm_plt_ackdata = 0;

	ret = mtk_ipi_send_compl(&mcupm_ipidev, CH_S_PLATFORM, IPI_SEND_WAIT,
		&mcdi_ipi_data,
		sizeof(struct mcupm_ipi_data_s) / MCUPM_MBOX_SLOT_SIZE,
		2000);

	if (ret < 0)
		dev_err(kobj, "[MCUPM]mtk_ipi_send_compl ret %d\n", ret);

	return snprintf(buf, PAGE_SIZE, "%s / MBOX%d.\n",
			mcupm_plt_ackdata ? "Finegrain status: disable" : "Finegrain status: enable", CH_S_FG);
}

static ssize_t mcdi_dynamic_finegrain_store(struct device *kobj,
	struct device_attribute *attr, const char *buf, size_t n)
{

	struct mcupm_ipi_data_s mcdi_ipi_data;
	int ret = 0;

	mcdi_ipi_data.cmd = 0xB1;

	ret = kstrtou32(buf, 0, &mcdi_ipi_data.u.logger.enable);
	if (ret != 0) {
		free_page((unsigned long)buf);
		return -EINVAL;
	}

	ret = mtk_ipi_send_compl(&mcupm_ipidev, CH_S_PLATFORM, IPI_SEND_WAIT,
		&mcdi_ipi_data,
		sizeof(struct mcupm_ipi_data_s) / MCUPM_MBOX_SLOT_SIZE,
		2000);

	return n;
}

DEVICE_ATTR_RW(mcdi_dynamic_finegrain);


int mcupm_plt_module_init(struct device *dev)
{
	struct mcupm_ipi_data_s ipi_data;
	int ret = 0;
	ipi_data.cmd = 0xDEAD;
	mcupm_plt_ackdata = 0;
	ret = mtk_ipi_send_compl(&mcupm_ipidev, CH_S_PLATFORM, IPI_SEND_WAIT,
		&ipi_data,
		sizeof(struct mcupm_ipi_data_s) / MCUPM_MBOX_SLOT_SIZE,
		2000);
	dev_dbg(dev, "MCUPM: %s ret(%d)\n",
		mcupm_plt_ackdata ? "Alive" : "Dead", ret);
	return 0;
}
void mcupm_plt_module_exit(struct device *dev)
{
	/* TODO: release resource */
	dev_dbg(dev, "[MCUPM] mcupm plt module exit.\n");
}
