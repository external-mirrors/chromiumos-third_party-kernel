// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for Chameleon v3 framebuffer
 *
 * Copyright 2022 Google LLC.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of.h>

#include <linux/dma-mapping.h>
#include <linux/interrupt.h>

#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-dma-contig.h>

#include "chv3-core.h"

#define MODULE_NAME "chv3-video"

static const struct chv3_fb_cfg fb0_cfg = {
	.reg_core = "fb0",
	.reg_irq = "fb0_irq",
	.irq = "fb0",
	.index = 0,
};

static const struct chv3_fb_cfg fb_mst_cfg[4] = {
{
	.reg_core = "fb_mst1",
	.reg_irq = "fb_mst1_irq",
	.irq = "fb_mst1",
	.index = 1,
},
{
	.reg_core = "fb_mst2",
	.reg_irq = "fb_mst2_irq",
	.irq = "fb_mst2",
	.index = 2,
},
{
	.reg_core = "fb_mst3",
	.reg_irq = "fb_mst3_irq",
	.irq = "fb_mst3",
	.index = 3,
},
{
	.reg_core = "fb_mst4",
	.reg_irq = "fb_mst4_irq",
	.irq = "fb_mst4",
	.index = 4,
},
};

static const struct chv3_fb_cfg fb_sst_cfg = {
	.reg_core = "fb_sst",
	.reg_irq = "fb_sst_irq",
	.irq = "fb_sst",
	.index = 5,
};

static const struct dprx_dp_cfg dp_mst_cfg = {
	.reg_core = "dp_mst",
	.reg_irq = "dp_mst_irq",
	.irq = "dp_mst",
	.has_mst = 1,
	.sink_count = 4,
};

static const struct dprx_dp_cfg dp_sst_cfg = {
	.reg_core = "dp_sst",
	.reg_irq = "dp_sst_irq",
	.irq = "dp_sst",
	.has_mst = 0,
	.sink_count = 1,
};

int chv3_g_edid(struct chv3_video *video, int index, struct v4l2_edid *edid)
{
	u32 end_block = edid->start_block + edid->blocks;
	struct sink *sink;

	if (index == 0 || index > 5)
		return -ENOTTY;
	if (edid->pad)
		return -EINVAL;

	if (1 <= index && index <= 4)
		sink = &video->dp_mst.sinks[index-1];
	else
		sink = &video->dp_sst.sinks[0];

	if (edid->start_block == 0 && edid->blocks == 0) {
		edid->blocks = sink->blocks;
		return 0;
	}

	if (edid->start_block > sink->blocks)
		return -EINVAL;
	if (end_block > sink->blocks) {
		end_block = sink->blocks;
		edid->blocks = end_block - edid->start_block;
	}

	memcpy(edid->edid, sink->edid + edid->start_block * 128, edid->blocks * 128);

	return 0;
}

int chv3_s_edid(struct chv3_video *video, int index, struct v4l2_edid *edid)
{
	struct sink *sink;

	if (index == 0 || index > 5)
		return -ENOTTY;
	if (edid->pad)
		return -EINVAL;

	if (1 <= index && index <= 4)
		sink = &video->dp_mst.sinks[index-1];
	else
		sink = &video->dp_sst.sinks[0];

	if (edid->start_block != 0)
		return -EINVAL;
	if (edid->blocks > DPRX_MAX_EDID_BLOCKS) {
		edid->blocks = DPRX_MAX_EDID_BLOCKS;
		return -E2BIG;
	}

	sink->blocks = edid->blocks;
	memcpy(sink->edid, edid->edid, edid->blocks * 128);

	return 0;
}


static ssize_t dp_hpd_show(struct device *dev, struct device_attribute *attr,
			   char *buf);
static ssize_t dp_hpd_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count);

static struct device_attribute dev_attr_dp0_hpd = {
	.attr = { .name = "hpd", .mode = 0644 },
	.show = dp_hpd_show,
	.store = dp_hpd_store,
};

static struct device_attribute dev_attr_dp1_hpd = {
	.attr = { .name = "hpd", .mode = 0644 },
	.show = dp_hpd_show,
	.store = dp_hpd_store,
};

static struct attribute *dp0_attrs[] = {
	&dev_attr_dp0_hpd.attr,
	NULL,
};

static struct attribute *dp1_attrs[] = {
	&dev_attr_dp1_hpd.attr,
	NULL,
};

static struct attribute_group dp0_attr_group = {
	.name = "dp0",
	.attrs = dp0_attrs,
};

static struct attribute_group dp1_attr_group = {
	.name = "dp1",
	.attrs = dp1_attrs,
};

static ssize_t dp_hpd_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct chv3_video *video = dev_get_drvdata(dev);
	struct dprx_dp *dp;

	if (attr == &dev_attr_dp0_hpd)
		dp = &video->dp_mst;
	else
		dp = &video->dp_sst;

	return sprintf(buf, "%d\n", dprx_dprx_get_hpd(dp));
}

static void dp_reset_fbs(struct chv3_video *video, struct dprx_dp *dp)
{
	int i;

	if (dp == &video->dp_sst) {
		chv3_fb_runtime_reset(&video->fb_sst);
	} else {
		for (i = 0; i < 4; i++)
			chv3_fb_runtime_reset(&video->fb_mst[i]);
	}
}

static ssize_t dp_hpd_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct chv3_video *video = dev_get_drvdata(dev);
	struct dprx_dp *dp;
	unsigned long val, prev_val;
	int res;

	if (attr == &dev_attr_dp0_hpd)
		dp = &video->dp_mst;
	else
		dp = &video->dp_sst;

	res = kstrtoul(buf, 10, &val);
	if (res)
		return res;

	prev_val = dprx_dprx_get_hpd(dp);
	if (!prev_val && val)
		dp_reset_fbs(video, dp);

	dprx_dprx_set_hpd(dp, val);
	return count;
}

static int chv3_video_probe(struct platform_device *pdev)
{
	struct chv3_video *video;
	int res;
	int i;

	video = devm_kzalloc(&pdev->dev, sizeof(*video), GFP_KERNEL);
	if (!video)
		return -ENOMEM;
	video->dev = &pdev->dev;
	platform_set_drvdata(pdev, video);

	/* register v4l2_device */
	res = v4l2_device_register(video->dev, &video->v4l2_dev);
	if (res)
		return res;

	/* initialize fb devices */
	res = chv3_fb_register(&video->fb0, video, &fb0_cfg);
	if (res)
		return res;

	for (i = 0; i < 4; i++) {
		res = chv3_fb_register(&video->fb_mst[i], video, &fb_mst_cfg[i]);
		if (res)
			return res;
	}

	res = chv3_fb_register(&video->fb_sst, video, &fb_sst_cfg);
	if (res)
		return res;

	/* initialize dp devices */
	res = dprx_dp_init(&video->dp_mst, video->dev, &dp_mst_cfg);
	if (res)
		return res;

	res = dprx_dp_init(&video->dp_sst, video->dev, &dp_sst_cfg);
	if (res)
		return res;

	/* create sysfs files */
	res = sysfs_create_group(&video->dev->kobj, &dp0_attr_group);
	if (res)
		return res;

	res = sysfs_create_group(&video->dev->kobj, &dp1_attr_group);
	if (res)
		return res;

	return 0;
}

static int chv3_video_remove(struct platform_device *pdev)
{
	struct chv3_video *video = platform_get_drvdata(pdev);

	v4l2_device_unregister(&video->v4l2_dev);

	return 0;
}

static const struct of_device_id chv3_video_match_table[] = {
	{ .compatible = "google,chv3-video" },
	{ },
};

static struct platform_driver chv3_video_platform_driver = {
	.probe = chv3_video_probe,
	.remove = chv3_video_remove,
	.driver = {
		.name = MODULE_NAME,
		.of_match_table = chv3_video_match_table,
	},
};

module_platform_driver(chv3_video_platform_driver);

MODULE_LICENSE("GPL");

