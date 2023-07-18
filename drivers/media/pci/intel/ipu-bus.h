/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2013 - 2020 Intel Corporation */

#ifndef IPU_BUS_H
#define IPU_BUS_H

#include <linux/auxiliary_bus.h>
#include <linux/irqreturn.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/pci.h>

#define IPU_BUS_NAME	IPU_NAME "-bus"

struct ipu_buttress_ctrl;
struct ipu_subsystem_trace_config;

struct ipu_bus_device {
	struct auxiliary_device auxdev;
	struct list_head list;
	void *pdata;
	struct ipu_mmu *mmu;
	struct ipu_device *isp;
	struct ipu_subsystem_trace_config *trace_cfg;
	struct ipu_buttress_ctrl *ctrl;
	u64 dma_mask;
};

#define to_ipu_bus_device(_dev) \
	container_of(to_auxiliary_dev(_dev), struct ipu_bus_device, auxdev)

struct ipu_bus_driver {
	struct auxiliary_driver auxdrv;
	irqreturn_t (*isr)(struct ipu_bus_device *adev);
	irqreturn_t (*isr_threaded)(struct ipu_bus_device *adev);
	bool wake_isr_thread;
};

#define to_ipu_bus_driver(_drv) \
	container_of(to_auxiliary_drv(_drv), struct ipu_bus_driver, auxdrv)

struct ipu_bus_device *ipu_bus_initialize_device(struct pci_dev *pdev,
						 struct device *parent, void *pdata,
						 struct ipu_buttress_ctrl *ctrl,
						 char *name, unsigned int nr);
int ipu_bus_add_device(struct ipu_bus_device *adev);
void ipu_bus_put_device(struct ipu_bus_device *adev);
void ipu_bus_del_devices(struct pci_dev *pdev);

#define ipu_bus_set_drvdata(adev, data) dev_set_drvdata(&(adev)->auxdev.dev, data)
#define ipu_bus_get_drvdata(adev) dev_get_drvdata(&(adev)->auxdev.dev)

struct pci_dev;
int ipu_bus_flr_recovery(struct pci_dev *pdev);

#endif
