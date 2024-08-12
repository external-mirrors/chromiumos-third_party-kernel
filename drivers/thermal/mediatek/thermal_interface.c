// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 MediaTek Inc.
 */
#include <linux/bits.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#define TTJ_OFFSET                 (0x100)
#define TCM_BUF_OFFSET             (0xC8)
#define TTJ_TCM_OFFSET             (0xA0)
#define REBOOT_TEMPERATURE_ADDR_OFFSET (0x39c)

struct therm_intf_info {
	int sw_ready;
	unsigned int cpu_cluster_num;
	int is_cputcm;
	struct device *dev;
	struct mutex lock;
	void __iomem *thermal_csram_base;
	void __iomem *thermal_cputcm_base;
};

static struct therm_intf_info tm_data;

static void therm_intf_write_cputcm(unsigned int val, int offset)
{
	if (tm_data.thermal_cputcm_base)
		writel(val, (void __iomem *)(tm_data.thermal_cputcm_base + offset));
}

static void therm_intf_write_csram(unsigned int val, int offset)
{
	writel(val, (void __iomem *)(tm_data.thermal_csram_base + offset));
}

static int therm_intf_read_cputcm_s32(int offset)
{
	void __iomem *addr_cputcm = tm_data.thermal_cputcm_base + offset;

	return sign_extend32(readl(addr_cputcm), 31);
}

static int therm_intf_read_csram_s32(int offset)
{
	void __iomem *addr = tm_data.thermal_csram_base + offset;

	return sign_extend32(readl(addr), 31);
}

int set_reboot_temperature(int temp)
{
	if (!tm_data.sw_ready)
		return -ENODEV;

	therm_intf_write_csram(temp, REBOOT_TEMPERATURE_ADDR_OFFSET);

	return 0;
}
EXPORT_SYMBOL(set_reboot_temperature);

static void write_ttj(unsigned int cpu_ttj, unsigned int gpu_ttj,
	unsigned int apu_ttj)
{
	mutex_lock(&tm_data.lock);

	if (tm_data.is_cputcm)
		therm_intf_write_cputcm(cpu_ttj, TTJ_TCM_OFFSET);

	therm_intf_write_csram(cpu_ttj, TTJ_OFFSET);
	therm_intf_write_csram(gpu_ttj, TTJ_OFFSET + 4);
	therm_intf_write_csram(apu_ttj, TTJ_OFFSET + 8);

	mutex_unlock(&tm_data.lock);
}

static ssize_t ttj_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int len = 0;

	if (tm_data.is_cputcm) {
		len += snprintf(buf + len, PAGE_SIZE - len, "%u, %u, %u\n",
			therm_intf_read_cputcm_s32(TTJ_TCM_OFFSET),
			therm_intf_read_csram_s32(TTJ_OFFSET + 4),
			therm_intf_read_csram_s32(TTJ_OFFSET + 8));
	} else {
		len += snprintf(buf + len, PAGE_SIZE - len, "%u, %u, %u\n",
			therm_intf_read_csram_s32(TTJ_OFFSET),
			therm_intf_read_csram_s32(TTJ_OFFSET + 4),
			therm_intf_read_csram_s32(TTJ_OFFSET + 8));
	}
	return len;
}

static ssize_t ttj_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	char cmd[10];
	unsigned int cpu_ttj, gpu_ttj, apu_ttj;

	if (sscanf(buf, "%4s %u %u %u", cmd, &cpu_ttj, &gpu_ttj, &apu_ttj) == 4) {
		if (strncmp(cmd, "TTJ", 3) == 0) {
			write_ttj(cpu_ttj, gpu_ttj, apu_ttj);

			return count;
		}
	}

	dev_err(dev, "[thermal_ttj] invalid input\n");

	return -EINVAL;
}

DEVICE_ATTR_RW(ttj);

static struct attribute *mtk_thermal_attrs[] = {
	&dev_attr_ttj.attr,
	NULL
};
ATTRIBUTE_GROUPS(mtk_thermal);

static const struct of_device_id therm_intf_of_match[] = {
	{ .compatible = "mediatek,therm_intf", },
	{},
};
MODULE_DEVICE_TABLE(of, therm_intf_of_match);

static int therm_intf_probe(struct platform_device *pdev)
{
	struct resource *res;
	void __iomem *addr;
	struct device_node *cpu_np;
	struct of_phandle_args args;
	unsigned int cpu, max_perf_domain = 0;
	int ret;
	unsigned int init_ttj_cpu;
	unsigned int init_ttj_gpu;
	unsigned int init_ttj_npu;

	if (!pdev->dev.of_node) {
		dev_info(&pdev->dev, "Only DT based supported\n");
		return -ENODEV;
	}

	tm_data.dev = &pdev->dev;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "sram");
	addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(addr))
		return PTR_ERR(addr);

	tm_data.thermal_csram_base = addr;

	/* Some projects don't support CPU TCM */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cputcm");
	if (res) {
		addr = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(addr))
			return PTR_ERR(addr);

		tm_data.thermal_cputcm_base = addr;
		tm_data.is_cputcm = 1;

		therm_intf_write_csram(1, TCM_BUF_OFFSET);

		dev_info(&pdev->dev, "cpu tcm resource ready\n");
	} else {
		dev_info(&pdev->dev, "Failed to get cpu tcm resource\n");
		tm_data.is_cputcm = 0;
		therm_intf_write_csram(0, TCM_BUF_OFFSET);
	}

	/* get CPU cluster num */
	for_each_possible_cpu(cpu) {
		cpu_np = of_cpu_device_node_get(cpu);
		if (!cpu_np) {
			dev_info(&pdev->dev, "Failed to get cpu %d device\n", cpu);
			return -ENODEV;
		}

		ret = of_parse_phandle_with_args(cpu_np, "performance-domains",
						 "#performance-domain-cells", 0,
						 &args);

		if (ret < 0)
			return ret;

		max_perf_domain = max(max_perf_domain, args.args[0]);
	}

	tm_data.cpu_cluster_num = max_perf_domain + 1;
	dev_info(&pdev->dev, "cpu_cluster_num = %d\n", tm_data.cpu_cluster_num);

	/* get from dts */
	ret = of_property_read_u32(pdev->dev.of_node, "mediatek,init-ttj-cpu", &init_ttj_cpu);
	if (ret) {
		dev_err(&pdev->dev, "Failed to get init-ttj-cpu from DT\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(pdev->dev.of_node, "mediatek,init-ttj-gpu", &init_ttj_gpu);
	if (ret) {
		dev_err(&pdev->dev, "Failed to get init-ttj-gpu from DT\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(pdev->dev.of_node, "mediatek,init-ttj-npu", &init_ttj_npu);
	if (ret) {
		dev_err(&pdev->dev, "Failed to get init-ttj-npu from DT\n");
		return -EINVAL;
	}

	/* apply init value to ttj */
	write_ttj(init_ttj_cpu, init_ttj_gpu, init_ttj_npu);

	mutex_init(&tm_data.lock);

	tm_data.sw_ready = 1;

	return 0;
}

static struct platform_driver therm_intf_driver = {
	.probe = therm_intf_probe,
	.driver = {
		.name = "mtk-thermal-interface",
		.of_match_table = therm_intf_of_match,
		.dev_groups = mtk_thermal_groups,
	},
};

module_platform_driver(therm_intf_driver);
MODULE_AUTHOR("Zhaoqing Jiu <zhaoqing.jiu@mediatek.com>");
MODULE_DESCRIPTION("Mediatek thermal interface driver");
MODULE_LICENSE("GPL");

