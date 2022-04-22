/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * API definition for Platform IRQ Forwarding to KVM guests
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Copyright 2020 Google LLC
 *
 */
#ifndef _UAPI_LINUX_PLATIRQFORWARD_H
#define _UAPI_LINUX_PLATIRQFORWARD_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define PLAT_IRQ_FORWARD_API_VERSION	0

#define PLAT_IRQ_FORWARD_TYPE       (';')
#define PLAT_IRQ_FORWARD_BASE       100
#define ACPI_EVT_FORWARD_BASE       130

/**
 *
 * Set masking and unmasking of interrupts.  Caller provides
 * struct plat_irq_forward_set with all fields set.
 *
 */
struct plat_irq_forward_set {
	__u32	argsz;
	__u32	action_flags;
#define PLAT_IRQ_FORWARD_SET_LEVEL_TRIGGER_EVENTFD	(1 << 0)
#define PLAT_IRQ_FORWARD_SET_LEVEL_UNMASK_EVENTFD	(1 << 1)
#define PLAT_IRQ_FORWARD_SET_EDGE_TRIGGER		(1 << 2)
#define PLAT_IRQ_FORWARD_SET_LEVEL_ACPI_SCI_TRIGGER_EVENTFD	(1 << 3)
#define PLAT_IRQ_FORWARD_SET_LEVEL_ACPI_SCI_UNMASK_EVENTFD	(1 << 4)
	__u32	irq_number_host;
	__u32	count;
	__u8	eventfd[];
};

/* ---- IOCTLs for Platform IRQ Forwarding fd (/dev/plat-irq-forward) ---- */
#define PLAT_IRQ_FORWARD_SET _IO(PLAT_IRQ_FORWARD_TYPE, PLAT_IRQ_FORWARD_BASE)

/**
 *
 * Register ACPI event and combine with SCI.
 *
 */
struct acpi_evt_forward_set {
	__u32	argsz;
	__u32	action_flags;
#define ACPI_EVT_FORWARD_SET_GPE_TRIGGER		(1 << 0)
#define ACPI_EVT_FORWARD_CLEAR_GPE_TRIGGER		(1 << 1)
#define ACPI_EVT_FORWARD_SET_FIXED_EVENT_TRIGGER	(1 << 2)
#define ACPI_EVT_FORWARD_CLEAR_FIXED_EVENT_TRIGGER	(1 << 3)
	union {
		__u32	gpe_host_nr;
		__u32	fixed_evt_nr;
	};
};

/* ---- IOCTLs for ACPI Events Forwarding ---- */
#define ACPI_EVT_FORWARD_SET		_IO(PLAT_IRQ_FORWARD_TYPE, ACPI_EVT_FORWARD_BASE)

#endif /* _UAPI_LINUX_PLATIRQFORWARD_H */
