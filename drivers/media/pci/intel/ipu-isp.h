/* SPDX-License-Identifier: GPL-2.0 */
/*
 * IPU PSYS KCAM
 *
 * Copyright (C) 2022 Google LLC
 */

#ifndef IPU_KCAM_H
#define IPU_KCAM_H

struct ipu_bus_device;

int ipu_isp_init(struct ipu_bus_device *adev, unsigned int minor);
void ipu_isp_exit(struct ipu_bus_device *adev);

#endif /* IPU_KCAM_H */
