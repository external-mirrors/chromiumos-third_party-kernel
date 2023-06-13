// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2013 - 2020 Intel Corporation

#include <linux/compat.h>
#include <linux/errno.h>
#include <linux/uaccess.h>

#include <uapi/linux/ipu-psys.h>

#include "ipu-psys.h"

long ipu_psys_compat_ioctl32(struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	WARN_ON(1);
	return -ENOTTY;
}
