// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2017-2022 Intel Corporation

#include <drm/drm_device.h>
#include <drm/drm_file.h>

#include <uapi/drm/gna_drm.h>

#include "gna_device.h"

int gna_getparam_ioctl(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	struct gna_device *gna_priv = to_gna_device(dev);
	union gna_parameter *param = data;

	return gna_getparam(gna_priv, param);
}
