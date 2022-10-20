// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2017-2022 Intel Corporation

#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_managed.h>

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>

#include "gna_device.h"

static void gna_drm_dev_fini(struct drm_device *dev, void *ptr)
{
	drm_dev_unregister(dev);
}

static int gna_drm_dev_init(struct drm_device *dev)
{
	int err;

	err = drm_dev_register(dev, 0);
	if (err)
		return err;

	return drmm_add_action_or_reset(dev, gna_drm_dev_fini, NULL);
}

static const struct drm_driver gna_drm_driver = {
	.driver_features = DRIVER_RENDER,

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};

int gna_probe(struct device *parent, struct gna_dev_info *dev_info, void __iomem *iobase)
{
	struct gna_device *gna_priv;
	struct drm_device *drm_dev;
	u32 bld_reg;
	int err;

	gna_priv = devm_drm_dev_alloc(parent, &gna_drm_driver, struct gna_device, drm);
	if (IS_ERR(gna_priv))
		return PTR_ERR(gna_priv);

	drm_dev = &gna_priv->drm;
	gna_priv->iobase = iobase;
	gna_priv->info = *dev_info;

	if (!(sizeof(dma_addr_t) > 4) ||
		dma_set_mask(parent, DMA_BIT_MASK(64))) {
		err = dma_set_mask(parent, DMA_BIT_MASK(32));
		if (err)
			return err;
	}

	bld_reg = gna_reg_read(gna_priv, GNA_MMIO_IBUFFS);
	gna_priv->hw_info.in_buf_s = bld_reg & GENMASK(7, 0);

	dev_set_drvdata(parent, drm_dev);

	err = gna_drm_dev_init(drm_dev);
	if (err)
		return err;

	return 0;
}

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Intel(R) Gaussian & Neural Accelerator (Intel(R) GNA) Driver");
MODULE_LICENSE("GPL");
