// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2025 MediaTek Inc.
 */

#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/interconnect.h>
#include "dvfsrc-helper.h"

static ssize_t dvfsrc_req_bw_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	u32 val;
	struct mtk_dvfsrc *dvfsrc = dev_get_drvdata(dev);
	int ret;

	if (kstrtou32(buf, 10, &val))
		return -EINVAL;

	if (!dvfsrc->bw_path)
		return -EINVAL;

	ret = icc_set_bw(dvfsrc->bw_path, MBps_to_icc(val), 0);
	if (ret)
		return ret;

	return count;
}
static DEVICE_ATTR_WO(dvfsrc_req_bw);

static ssize_t dvfsrc_req_hrtbw_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	u32 val;
	struct mtk_dvfsrc *dvfsrc = dev_get_drvdata(dev);
	int ret;

	if (kstrtou32(buf, 10, &val))
		return -EINVAL;

	if (!dvfsrc->hrt_path)
		return -EINVAL;

	ret = icc_set_bw(dvfsrc->hrt_path, MBps_to_icc(val), 0);
	if (ret)
		return ret;

	return count;
}
static DEVICE_ATTR_WO(dvfsrc_req_hrtbw);

static ssize_t dvfsrc_req_ddr_opp_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	u32 val;
	struct mtk_dvfsrc *dvfsrc = dev_get_drvdata(dev);
	int ret;

	if (kstrtou32(buf, 10, &val))
		return -EINVAL;

	if (!dvfsrc->perf_path)
		return -EINVAL;

	if (val < dvfsrc->num_perf)
		ret = icc_set_bw(dvfsrc->perf_path, 0, dvfsrc->perfs_peak_bw[val]);
	else
		ret = icc_set_bw(dvfsrc->perf_path, 0, 0);

	if (ret)
		return ret;

	return count;
}
static DEVICE_ATTR_WO(dvfsrc_req_ddr_opp);

static struct attribute *dvfsrc_sysfs_attrs[] = {
	&dev_attr_dvfsrc_req_bw.attr,
	&dev_attr_dvfsrc_req_hrtbw.attr,
	&dev_attr_dvfsrc_req_ddr_opp.attr,
	NULL,
};

static struct attribute_group dvfsrc_sysfs_attr_group = {
	.attrs = dvfsrc_sysfs_attrs,
};

int dvfsrc_register_sysfs(struct device *dev)
{
	int ret;
	ret = sysfs_create_group(&dev->kobj, &dvfsrc_sysfs_attr_group);
	if (ret)
		return ret;

	ret = sysfs_create_link(&dev->parent->kobj, &dev->kobj, "helio-dvfsrc");
	return ret;
}

void dvfsrc_unregister_sysfs(struct device *dev)
{
	sysfs_remove_link(&dev->parent->kobj, "helio-dvfsrc");
	sysfs_remove_group(&dev->kobj, &dvfsrc_sysfs_attr_group);
}
