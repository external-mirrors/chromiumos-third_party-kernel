/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * VCAM test driver
 *
 * Copyright (C) Google LLC
 */

#ifndef __UAPI_LINUX_VCAM_H__
#define __UAPI_LINUX_VCAM_H__

#include <linux/compiler.h>
#include <linux/const.h>
#include <linux/types.h>

#define VCAM_ROOT_ENTITY_NAME		"VCAM main"

/*
 * Per VCAM implementation, FAST_IRQ entity is the one that triggers events
 * frequently (hrtimer), we use it for OP execution tests.
 *
 * SLOW_IRQ entity, on the other hand, works on a much slower pace (also
 * hrtimer), so that we can test query_operations and remove_operations.
 */
#define VCAM_FAST_IRQ_ENTITY_NAME	"Fast IRQ"
#define VCAM_SLOW_IRQ_ENTITY_NAME	"Slow IRQ"

#endif /* __UAPI_LINUX_VCAM_H__ */
