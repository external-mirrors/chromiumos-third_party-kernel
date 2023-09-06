/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * VISP test driver
 *
 * Copyright (C) Google LLC
 */

#ifndef __UAPI_LINUX_VISP_H__
#define __UAPI_LINUX_VISP_H__

#include <linux/compiler.h>
#include <linux/const.h>
#include <linux/types.h>

#define VISP_ROOT_ENTITY_NAME		"VISP main"

/*
 * Per VISP implementation, FAST_IRQ entity is the one that triggers events
 * frequently (hrtimer), we use it for OP execution tests.
 *
 * SLOW_IRQ entity, on the other hand, works on a much slower pace (also
 * hrtimer), so that we can test query_operations and remove_operations.
 *
 * BM_IRQ is a very fast hrtimer, that triggers 5K times per-second.
 * Used for benchmarking/performance testing.
 */
#define VISP_FAST_IRQ_ENTITY_NAME	"Fast IRQ"
#define VISP_SLOW_IRQ_ENTITY_NAME	"Slow IRQ"
#define VISP_BM_IRQ_ENTITY_NAME		"BM IRQ"

#define BM_NUM_OPS			88788

#endif /* __UAPI_LINUX_VISP_H__ */
