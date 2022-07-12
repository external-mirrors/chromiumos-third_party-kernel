/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MANATEE_H
#define _MANATEE_H

enum manatee_domain_type {
	MANATEE_NATIVE,		/* default, no ManaTEE */
	MANATEE_HYP,		/* running as hypervisor      */
	MANATEE_CHROMEOS,	/* running as ChromeOS VM */
};

#ifdef CONFIG_MANATEE
extern enum manatee_domain_type manatee_domain_type;
#else
#define manatee_domain_type		MANATEE_NATIVE
#endif

#define manatee_domain()		(manatee_domain_type != MANATEE_NATIVE)
#define manatee_hyp_domain()		(manatee_domain_type == MANATEE_HYP)
#define manatee_chromeos_domain()	(manatee_domain_type == MANATEE_CHROMEOS)

#define PM_OP_CALL_PREPARE		0
#define PM_OP_CALL_SUSPEND		1
#define PM_OP_CALL_SUSPEND_LATE		2
#define PM_OP_CALL_SUSPEND_NOIRQ	3
#define PM_OP_CALL_RESUME		4
#define PM_OP_CALL_RESUME_EARLY		5
#define PM_OP_CALL_RESUME_NOIRQ		6
#define PM_OP_CALL_COMPLETE		7
#define PM_OP_CALL_RPM_SUSPEND		8
#define PM_OP_CALL_RPM_RESUME		9
#define PM_OP_CALL_RPM_IDLE		10

#endif	/* _MANATEE_H */
