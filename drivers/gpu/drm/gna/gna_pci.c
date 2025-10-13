// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2017-2022 Intel Corporation

#include <linux/module.h>
#include <linux/pci.h>

#include "gna_device.h"
#include "gna_pci.h"

int gna_pci_probe(struct pci_dev *pcidev, const struct pci_device_id *pci_id)
{
	int err;

	err = pcim_enable_device(pcidev);
	if (err)
		return err;

	err = pcim_iomap_regions(pcidev, BIT(0), pci_name(pcidev));
	if (err)
		return err;

	pci_set_master(pcidev);

	return 0;
}

static struct pci_driver gna_pci_driver = {
	.name = DRIVER_NAME,
	.probe = gna_pci_probe,
};

module_pci_driver(gna_pci_driver);
