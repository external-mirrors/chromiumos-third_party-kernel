// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2013 - 2020 Intel Corporation

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/sizes.h>

#include "ipu.h"
#include "ipu-platform.h"
#include "ipu-dma.h"

static void ipu_bus_release(struct device *dev)
{
	struct ipu_bus_device *adev = to_ipu_bus_device(dev);

	kfree(adev->pdata);
	kfree(adev);
}

struct ipu_bus_device *ipu_bus_initialize_device(struct pci_dev *pdev,
						 struct device *parent,
						 void *pdata,
						 struct ipu_buttress_ctrl *ctrl,
						 char *name, unsigned int nr)
{
	struct ipu_bus_device *adev;
	struct auxiliary_device *auxdev;
	struct ipu_device *isp = pci_get_drvdata(pdev);
	int rval;

	adev = kzalloc(sizeof(*adev), GFP_KERNEL);
	if (!adev)
		return ERR_PTR(-ENOMEM);

	adev->ctrl = ctrl;
	adev->pdata = pdata;
	adev->isp = isp;

	auxdev = &adev->auxdev;
	auxdev->name = name;
	auxdev->id = nr;

	auxdev->dev.parent = parent;
	auxdev->dev.release = ipu_bus_release;

	rval = auxiliary_device_init(auxdev);
	if (rval) {
		kfree(adev);
		return ERR_PTR(rval);
	}

	adev->dma_mask = DMA_BIT_MASK(isp->secure_mode ?
				      IPU_MMU_ADDRESS_BITS :
				      IPU_MMU_ADDRESS_BITS_NON_SECURE);
	auxdev->dev.dma_mask = &adev->dma_mask;
	rval = dma_set_coherent_mask(&auxdev->dev, adev->dma_mask);
	if (rval) {
		auxiliary_device_uninit(auxdev);
		return ERR_PTR(rval);
	}
	auxdev->dev.dma_parms = pdev->dev.dma_parms;
	auxdev->dev.dma_ops = &ipu_dma_ops;

	mutex_lock(&isp->mutex);
	list_add(&adev->list, &isp->devices);
	mutex_unlock(&isp->mutex);

	return adev;
}

int ipu_bus_add_device(struct ipu_bus_device *adev)
{
	struct auxiliary_device *auxdev = &adev->auxdev;
	int rval;

	rval = auxiliary_device_add(auxdev);
	if (rval) {
		auxiliary_device_uninit(auxdev);
		return rval;
	}

	return 0;
}

void ipu_bus_put_device(struct ipu_bus_device *adev)
{
	auxiliary_device_uninit(&adev->auxdev);
}

void ipu_bus_del_devices(struct pci_dev *pdev)
{
	struct ipu_device *isp = pci_get_drvdata(pdev);
	struct ipu_bus_device *adev, *save;

	mutex_lock(&isp->mutex);

	list_for_each_entry_safe(adev, save, &isp->devices, list) {
		list_del(&adev->list);
		auxiliary_device_delete(&adev->auxdev);
		auxiliary_device_uninit(&adev->auxdev);
	}

	mutex_unlock(&isp->mutex);
}

static int flr_rpm_recovery(struct device *dev)
{
	dev_dbg(dev, "FLR recovery call\n");
	/*
	 * We are not necessarily going through device from child to
	 * parent. runtime PM refuses to change state for parent if the child
	 * is still active. At FLR (full reset for whole IPU) that doesn't
	 * matter. Everything has been power gated by HW during the FLR cycle
	 * and we are just cleaning up SW state. Thus, ignore child during
	 * set_suspended.
	 */
	pm_suspend_ignore_children(dev, true);
	pm_runtime_set_suspended(dev);
	pm_suspend_ignore_children(dev, false);

	return 0;
}

int ipu_bus_flr_recovery(struct pci_dev *pdev)
{
	struct ipu_device *isp = pci_get_drvdata(pdev);
	struct ipu_bus_device *adev;

	mutex_lock(&isp->mutex);

	list_for_each_entry(adev, &isp->devices, list)
		flr_rpm_recovery(&adev->auxdev.dev);

	mutex_unlock(&isp->mutex);

	return 0;
}
