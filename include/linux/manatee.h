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

#endif	/* _MANATEE_H */
