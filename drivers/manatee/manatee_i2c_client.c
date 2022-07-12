// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/i2c.h>
#include <linux/module.h>

static int manatee_probe(struct i2c_client *client)
{
	return 0;
}

static const struct of_device_id manatee_pwron_of_ids[] = {
	/* dw9768 */
	{ .compatible = "dongwoon,dw9768" },
	{ /* END OF LIST */ },
};
MODULE_DEVICE_TABLE(of, manatee_pwron_of_ids);

static const struct of_device_id manatee_of_ids[] = {
	/* at24 */
	{ .compatible = "atmel,24c00" },
	{ .compatible = "atmel,24c01" },
	{ .compatible = "atmel,24cs01" },
	{ .compatible = "atmel,24c02" },
	{ .compatible = "atmel,24cs02" },
	{ .compatible = "atmel,24mac402" },
	{ .compatible = "atmel,24mac602" },
	{ .compatible = "atmel,spd" },
	{ .compatible = "atmel,24c04" },
	{ .compatible = "atmel,24cs04" },
	{ .compatible = "atmel,24c08" },
	{ .compatible = "atmel,24cs08" },
	{ .compatible = "atmel,24c16" },
	{ .compatible = "atmel,24cs16" },
	{ .compatible = "atmel,24c32" },
	{ .compatible = "atmel,24cs32" },
	{ .compatible = "atmel,24c64" },
	{ .compatible = "atmel,24cs64" },
	{ .compatible = "atmel,24c128" },
	{ .compatible = "atmel,24c256" },
	{ .compatible = "atmel,24c512" },
	{ .compatible = "atmel,24c1024" },
	{ .compatible = "atmel,24c2048" },
	{ /* END OF LIST */ },
};
MODULE_DEVICE_TABLE(of, manatee_of_ids);

static const struct acpi_device_id manatee_pwron_acpi_ids[] = {
	/* i2c_hid_acpi */
	{"PNP0C50"},
	/* elan_i2c */
	{"ELAN0000"},
	/* elants_i2c */
	{"ELAN0001"},
	/* raydium_ts */
	{"RAYD0001"},
	{}
};
MODULE_DEVICE_TABLE(acpi, manatee_pwron_acpi_ids);

static const struct acpi_device_id manatee_acpi_ids[] = {
	/* hi556 */
	{"INT3537"},
	/* ov2740 */
	{"INT3474"},
	/* ov8856 */
	{"OVTI8856"},
	{}
};
MODULE_DEVICE_TABLE(acpi, manatee_acpi_ids);

static struct i2c_driver manatee_pwron_i2c_driver = {
	.driver = {
		.name = "manatee_pwron_i2c",
		.acpi_match_table = ACPI_PTR(manatee_pwron_acpi_ids),
		.of_match_table = manatee_pwron_of_ids,
	},
	.probe_new = manatee_probe,
};

static struct i2c_driver manatee_i2c_driver = {
	.driver = {
		.name = "manatee_i2c",
		.acpi_match_table = ACPI_PTR(manatee_acpi_ids),
		.of_match_table = manatee_of_ids,
	},
	.probe_new = manatee_probe,
	.flags = I2C_DRV_ACPI_WAIVE_D0_PROBE,
};

module_i2c_driver(manatee_pwron_i2c_driver);
module_i2c_driver(manatee_i2c_driver);
