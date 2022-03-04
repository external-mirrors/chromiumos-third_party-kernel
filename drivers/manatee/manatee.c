// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/manatee.h>

enum manatee_domain_type manatee_domain_type = MANATEE_NATIVE;

static int __init manatee_detect_mode(void)
{
	if (IS_ENABLED(CONFIG_MANATEE_HYP))
		manatee_domain_type = MANATEE_HYP;
	else if (IS_ENABLED(CONFIG_MANATEE_CHROMEOS))
		manatee_domain_type = MANATEE_CHROMEOS;
	else
		manatee_domain_type = MANATEE_NATIVE;

	return 0;
}
core_initcall(manatee_detect_mode);
