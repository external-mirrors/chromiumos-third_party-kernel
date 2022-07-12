// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

struct manatee_i2c_dev {
	struct i2c_adapter adapter;
};

static u32 i2c_manatee_func(struct i2c_adapter *adap)
{
	return 0;
}

static const struct i2c_algorithm i2c_manatee_algo = {
	.functionality = i2c_manatee_func,
};

static int manatee_i2c_plat_probe(struct platform_device *pdev)
{
	struct manatee_i2c_dev *dev;
	struct i2c_adapter *adap;
	int ret;

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	platform_set_drvdata(pdev, dev);

	adap = &dev->adapter;
	adap->owner = THIS_MODULE;
	adap->class = I2C_CLASS_DEPRECATED;
	ACPI_COMPANION_SET(&adap->dev, ACPI_COMPANION(&pdev->dev));

	/* The code below assumes runtime PM to be disabled. */
	WARN_ON(pm_runtime_enabled(&pdev->dev));

	snprintf(adap->name, sizeof(adap->name), "ManaTEE I2C adapter");
	adap->algo = &i2c_manatee_algo;
	adap->dev.parent = &pdev->dev;

	ret = i2c_add_adapter(adap);
	if (ret)
		dev_err(&pdev->dev, "failure adding adapter: %d\n", ret);

	return ret;
}

static int manatee_i2c_plat_remove(struct platform_device *pdev)
{
	struct manatee_i2c_dev *dev = platform_get_drvdata(pdev);

	/* The code below assumes runtime PM to be disabled. */
	WARN_ON(pm_runtime_enabled(&pdev->dev));

	i2c_del_adapter(&dev->adapter);

	return 0;
}

/* Work with hotplug and coldplug */
MODULE_ALIAS("platform:i2c_designware");

static struct platform_driver manatee_i2c_driver = {
	.probe = manatee_i2c_plat_probe,
	.remove = manatee_i2c_plat_remove,
	.driver		= {
		.name	= "i2c_designware",
	},
};

module_platform_driver(manatee_i2c_driver);
