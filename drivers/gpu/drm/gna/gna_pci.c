// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2017-2022 Intel Corporation

#include <linux/module.h>
#include <linux/pci.h>

#include "gna_device.h"
#include "gna_hw.h"
#include "gna_pci.h"

int gna_pci_probe(struct pci_dev *pcidev, const struct pci_device_id *pci_id)
{
	struct gna_dev_info *dev_info;
	void __iomem *iobase;
	int err;

	err = pcim_enable_device(pcidev);
	if (err)
		return err;

	err = pcim_iomap_regions(pcidev, BIT(0), pci_name(pcidev));
	if (err)
		return err;

	iobase = pcim_iomap_table(pcidev)[0];

	pci_set_master(pcidev);

	dev_info = (struct gna_dev_info *)pci_id->driver_data;

	err = gna_probe(&pcidev->dev, dev_info, iobase);
	if (err)
		return err;

	return 0;
}

static struct pci_driver gna_pci_driver = {
	.name = DRIVER_NAME,
	.probe = gna_pci_probe,
};

module_pci_driver(gna_pci_driver);
