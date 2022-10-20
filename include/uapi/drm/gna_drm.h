/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/* Copyright(c) 2017-2022 Intel Corporation */

#ifndef _GNA_DRM_H_
#define _GNA_DRM_H_

#include <linux/types.h>

#include "drm.h"

#define GNA_DDI_VERSION_3 3

#define GNA_PARAM_RECOVERY_TIMEOUT	1
#define GNA_PARAM_DEVICE_TYPE		2
#define GNA_PARAM_INPUT_BUFFER_S	3
#define GNA_PARAM_DDI_VERSION		4

#define GNA_DEV_TYPE_0_9	0x09
#define GNA_DEV_TYPE_1_0	0x10
#define GNA_DEV_TYPE_2_0	0x20
#define GNA_DEV_TYPE_3_0	0x30
#define GNA_DEV_TYPE_3_5	0x35

typedef __u64 gna_param_id;

union gna_parameter {
	struct {
		gna_param_id id;
	} in;

	struct {
		__u64 value;
	} out;
};

#define DRM_GNA_GET_PARAMETER		0x00

#define DRM_IOCTL_GNA_GET_PARAMETER	DRM_IOWR(DRM_COMMAND_BASE + DRM_GNA_GET_PARAMETER, union gna_parameter)

#endif /* _GNA_DRM_H_ */
