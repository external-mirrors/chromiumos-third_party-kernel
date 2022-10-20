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

struct gna_mem_id {
	__u32 handle;
	__u32 pad;
	__u64 vma_fake_offset;
	__u64 size_granted;
};

union gna_gem_new {
	struct {
		__u64 size;
	} in;

	struct gna_mem_id out;
};

struct gna_gem_free {
	__u32 handle;
};

#define DRM_GNA_GET_PARAMETER		0x00
#define DRM_GNA_GEM_NEW			0x01
#define DRM_GNA_GEM_FREE		0x02

#define DRM_IOCTL_GNA_GET_PARAMETER	DRM_IOWR(DRM_COMMAND_BASE + DRM_GNA_GET_PARAMETER, union gna_parameter)
#define DRM_IOCTL_GNA_GEM_NEW		DRM_IOWR(DRM_COMMAND_BASE + DRM_GNA_GEM_NEW, union gna_gem_new)
#define DRM_IOCTL_GNA_GEM_FREE		DRM_IOWR(DRM_COMMAND_BASE + DRM_GNA_GEM_FREE, struct gna_gem_free)

#endif /* _GNA_DRM_H_ */
