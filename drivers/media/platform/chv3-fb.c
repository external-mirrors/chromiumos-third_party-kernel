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

#define MODULE_NAME	"chv3-fb"

#define FB_BYTES_PER_PIXEL	4

#define FB_HEIGHT	0x004
#define FB_WIDTH	0x008
#define FB_EN		0x080
#define FB_BUFFERA	0x084
#define FB_BUFFERB	0x088
#define FB_BUFFERSIZE	0x08c
#define FB_RESET	0x118
#define FB_ERRORSTATUS	0x124
#define FB_IOCOLOR	0x300
#define FB_IODATARATE	0x304
#define FB_IOPIXELMODE	0x308
#define FB_VERSION	0xff0
#define FB_VERSION_CURRENT	0xc0fb0000
#define FB_IRQ_MASK	0x1008
#define FB_IRQ_CLR	0x100c
#define FB_IRQ_ALL		0xf
#define FB_IRQ_BUFF0		(1 << 0)
#define FB_IRQ_BUFF1		(1 << 1)
#define FB_IRQ_RESOLUTION	(1 << 2)
#define FB_IRQ_ERROR		(1 << 3)

struct chv3_fb_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head link;
};

struct chv3_fb {
	struct device *dev;
	void __iomem *iobase;
	struct v4l2_device v4l2_dev;
	struct vb2_queue queue;
	struct video_device vdev;
	struct v4l2_pix_format fmt;

	u32 sequence;
	bool streaming;
	bool writing_to_a;

	struct list_head bufs;
	spinlock_t bufs_lock;

	struct mutex fb_lock;
};

/* v4l2 ioctls */

static int vidioc_querycap(struct file *file, void *data,
			    struct v4l2_capability *cap)
{
	strscpy(cap->driver, MODULE_NAME, sizeof(cap->driver));
	strscpy(cap->card, "Chameleonv3 framebuffer", sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s",
		 MODULE_NAME);

	return 0;
}

/*
 * We can't control the resolution, we can only read what it currently is from
 * the framebuffer. In order not to confuse the application, the resolution is
 * saved in fb->fmt, and is only updated when the application calls open() and
 * there are no other applications that have the file opened.
 */

static int vidioc_g_fmt_vid_cap(struct file *file, void *data,
				struct v4l2_format *fmt)
{
	struct chv3_fb *fb = video_drvdata(file);

	fmt->fmt.pix = fb->fmt;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void *data,
				   struct v4l2_fmtdesc *fmt)
{
	if (fmt->index > 0)
		return -EINVAL;
	fmt->flags = 0;
	fmt->pixelformat = V4L2_PIX_FMT_BGR32;
	return 0;
}

static int vidioc_enum_framesizes(struct file *file, void *data,
				  struct v4l2_frmsizeenum *frm)
{
	struct chv3_fb *fb = video_drvdata(file);

	if (frm->index != 0)
		return -EINVAL;
	if (frm->pixel_format != V4L2_PIX_FMT_BGR32)
		return -EINVAL;

	frm->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	frm->discrete.width  = fb->fmt.width;
	frm->discrete.height = fb->fmt.height;
	return 0;
}

static int vidioc_g_input(struct file *file, void *data, unsigned int *index)
{
	*index = 0;
	return 0;
}

static int vidioc_s_input(struct file *file, void *data, unsigned int index)
{
	if (index != 0)
		return -EINVAL;
	return 0;
}

static int vidioc_enum_input(struct file *file, void *data,
			     struct v4l2_input *input)
{
	if (input->index != 0)
		return -EINVAL;
	strcpy(input->name, "Vin-0");
	input->type = V4L2_INPUT_TYPE_CAMERA;
	return 0;
}

static const struct v4l2_ioctl_ops fb_v4l2_ioctl_ops = {
	.vidioc_querycap = vidioc_querycap,

	.vidioc_enum_fmt_vid_cap = vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap = vidioc_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap = vidioc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap = vidioc_g_fmt_vid_cap,

	.vidioc_enum_framesizes = vidioc_enum_framesizes,

	.vidioc_enum_input = vidioc_enum_input,
	.vidioc_g_input = vidioc_g_input,
	.vidioc_s_input = vidioc_s_input,

	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
};

/* videobuf2 operations */

static int fb_queue_setup(struct vb2_queue *q,
		       unsigned int *nbuffers, unsigned int *nplanes,
		       unsigned int sizes[], struct device *alloc_devs[])
{
	struct chv3_fb *fb = vb2_get_drv_priv(q);

	if (*nplanes) {
		if (sizes[0] < fb->fmt.sizeimage)
			return -EINVAL;
		return 0;
	}
	*nplanes = 1;
	sizes[0] = fb->fmt.sizeimage;
	return 0;
}

/*
 * There are two address registers: BUFFERA and BUFFERB. The framebuffer
 * alternates writing between them (i.e. even frames go to BUFFERA, odd
 * ones to BUFFERB).
 *
 *  (buffer queue) >     QUEUED ---> QUEUED ---> QUEUED ---> ...
 *                       BUFFERA     BUFFERB
 *  (hw writing to this) ^
 *                (and then to this) ^
 *
 * The buffer swapping happens at irq time. When an irq comes, the next
 * frame is already assigned an address in the buffer queue. This gives
 * the irq handler a whole frame's worth of time to update the buffer
 * address register.
 */

static dma_addr_t fb_buffer_dma_addr(struct chv3_fb_buffer *buf)
{
	return vb2_dma_contig_plane_dma_addr(&buf->vb.vb2_buf, 0);
}

static void fb_start_frame(struct chv3_fb *fb, struct chv3_fb_buffer *buf)
{
	fb->writing_to_a = 1;
	writel(fb_buffer_dma_addr(buf), fb->iobase + FB_BUFFERA);
	writel(1, fb->iobase + FB_EN);
}

static void fb_next_frame(struct chv3_fb *fb, struct chv3_fb_buffer *buf)
{
	u32 reg = fb->writing_to_a ? FB_BUFFERB : FB_BUFFERA;

	writel(fb_buffer_dma_addr(buf), fb->iobase + reg);
}

static int fb_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct chv3_fb *fb = vb2_get_drv_priv(q);
	struct chv3_fb_buffer *buf;
	unsigned long flags;

	fb->streaming = 1;
	fb->sequence = 0;

	spin_lock_irqsave(&fb->bufs_lock, flags);
	buf = list_first_entry_or_null(&fb->bufs, struct chv3_fb_buffer, link);
	if (buf) {
		fb_start_frame(fb, buf);
		if (!list_is_last(&buf->link, &fb->bufs))
			fb_next_frame(fb, list_next_entry(buf, link));
	}
	spin_unlock_irqrestore(&fb->bufs_lock, flags);

	return 0;
}

static void fb_stop_streaming(struct vb2_queue *q)
{
	struct chv3_fb *fb = vb2_get_drv_priv(q);

	vb2_wait_for_all_buffers(q);
	fb->streaming = 0;
	writel(0, fb->iobase + FB_EN);
}

static struct chv3_fb_buffer *to_chv3_fb_buffer(struct vb2_v4l2_buffer *b)
{
	return container_of(b, struct chv3_fb_buffer, vb);
}

static void fb_buf_queue(struct vb2_buffer *vb)
{
	struct chv3_fb *fb = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *v4l2_buf = to_vb2_v4l2_buffer(vb);
	struct chv3_fb_buffer *buf = to_chv3_fb_buffer(v4l2_buf);
	bool first, second;
	unsigned long flags;

	spin_lock_irqsave(&fb->bufs_lock, flags);
	first = list_empty(&fb->bufs);
	second = list_is_singular(&fb->bufs);
	list_add_tail(&buf->link, &fb->bufs);
	if (fb->streaming) {
		if (first)
			fb_start_frame(fb, buf);
		else if (second)
			fb_next_frame(fb, buf);
	}
	spin_unlock_irqrestore(&fb->bufs_lock, flags);
}

static const struct vb2_ops fb_vb2_ops = {
	.queue_setup = fb_queue_setup,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.start_streaming = fb_start_streaming,
	.stop_streaming = fb_stop_streaming,
	.buf_queue = fb_buf_queue,
};

/* file operations */

static void fb_update_fmt(struct chv3_fb *fb)
{
	fb->fmt.width  = readl(fb->iobase + FB_WIDTH);
	fb->fmt.height = readl(fb->iobase + FB_HEIGHT);
	fb->fmt.pixelformat = V4L2_PIX_FMT_BGR32;
	fb->fmt.field = V4L2_FIELD_NONE;
	fb->fmt.bytesperline = fb->fmt.width * FB_BYTES_PER_PIXEL;
	fb->fmt.sizeimage = fb->fmt.bytesperline * fb->fmt.height;
	fb->fmt.colorspace = V4L2_COLORSPACE_SRGB;
}

static int fb_open(struct file *file)
{
	struct chv3_fb *fb = video_drvdata(file);
	int res;

	mutex_lock(&fb->fb_lock);
	res = v4l2_fh_open(file);
	if (!res) {
		if (v4l2_fh_is_singular_file(file)) {
			fb_update_fmt(fb);
			writel(fb->fmt.sizeimage, fb->iobase + FB_BUFFERSIZE);
		}
	}
	mutex_unlock(&fb->fb_lock);

	return res;
}

static const struct v4l2_file_operations fb_v4l2_fops = {
	.owner = THIS_MODULE,
	.open = fb_open,
	.release = vb2_fop_release,
	.unlocked_ioctl = video_ioctl2,
	.mmap = vb2_fop_mmap,
	.poll = vb2_fop_poll,
};

/* irq handling */

static void fb_frame_irq(struct chv3_fb *fb)
{
	struct chv3_fb_buffer *buf;

	spin_lock(&fb->bufs_lock);

	buf = list_first_entry_or_null(&fb->bufs, struct chv3_fb_buffer, link);
	if (!buf)
		goto empty;
	list_del(&buf->link);

	vb2_set_plane_payload(&buf->vb.vb2_buf, 0, fb->fmt.sizeimage);
	buf->vb.vb2_buf.timestamp = ktime_get_ns();
	buf->vb.sequence = fb->sequence++;
	buf->vb.field = V4L2_FIELD_NONE;
	vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);

	buf = list_first_entry_or_null(&fb->bufs, struct chv3_fb_buffer, link);
	if (buf) {
		fb->writing_to_a = !fb->writing_to_a;
		if (!list_is_last(&buf->link, &fb->bufs))
			fb_next_frame(fb, list_next_entry(buf, link));
	} else {
		writel(0, fb->iobase + FB_EN);
	}
empty:
	spin_unlock(&fb->bufs_lock);
}

static irqreturn_t fb_isr(int irq, void *data)
{
	struct chv3_fb *fb = data;
	unsigned int reg;

	reg = readl(fb->iobase + FB_IRQ_CLR);
	if (!reg)
		return IRQ_NONE;

	if (reg & (FB_IRQ_BUFF0 | FB_IRQ_BUFF1))
		fb_frame_irq(fb);
	if (reg & FB_IRQ_RESOLUTION)
		dev_info(fb->dev, "resolution changed\n");
	if (reg & FB_IRQ_ERROR) {
		dev_warn(fb->dev, "framebuffer error: 0x%x\n",
		       readl(fb->iobase + FB_ERRORSTATUS));
	}

	writel(reg, fb->iobase + FB_IRQ_CLR);

	return IRQ_HANDLED;
}

/* driver probe & remove */

static int fb_check_version(struct chv3_fb *fb)
{
	u32 version;

	version = readl(fb->iobase + FB_VERSION);
	if (version != FB_VERSION_CURRENT) {
		dev_warn(fb->dev,
			 "wrong framebuffer version: expected %x, got %x\n",
			 FB_VERSION_CURRENT, version);
		return -1;
	}
	return 0;
}

static int fb_probe(struct platform_device *pdev)
{
	struct chv3_fb *fb;
	int res;
	int irq;

	fb = devm_kzalloc(&pdev->dev, sizeof(*fb), GFP_KERNEL);
	if (!fb)
		return -ENOMEM;
	fb->dev = &pdev->dev;
	platform_set_drvdata(pdev, fb);

	/* map register space */
	fb->iobase = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(fb->iobase))
		return -ENOMEM;

	/* check hw version */
	if (fb_check_version(fb))
		return -ENODEV;

	/* setup interrupts */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -ENXIO;
	res = devm_request_irq(fb->dev, irq, fb_isr, 0, MODULE_NAME, fb);
	if (res)
		return res;

	/* setup dma */
	dma_set_coherent_mask(fb->dev, DMA_BIT_MASK(32));

	/* register v4l2_device */
	res = v4l2_device_register(fb->dev, &fb->v4l2_dev);
	if (res)
		return res;

	/* initialize vb2 queue */
	fb->queue.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fb->queue.io_modes = VB2_MMAP;
	fb->queue.dev = fb->dev;
	fb->queue.lock = &fb->fb_lock;
	fb->queue.ops = &fb_vb2_ops;
	fb->queue.mem_ops = &vb2_dma_contig_memops;
	fb->queue.drv_priv = fb;
	fb->queue.buf_struct_size = sizeof(struct chv3_fb_buffer);
	fb->queue.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	res = vb2_queue_init(&fb->queue);
	if (res) {
		v4l2_device_unregister(&fb->v4l2_dev);
		return res;
	}

	/* register video_device */
	strcpy(fb->vdev.name, MODULE_NAME);
	fb->vdev.fops = &fb_v4l2_fops;
	fb->vdev.ioctl_ops = &fb_v4l2_ioctl_ops;
	fb->vdev.lock = &fb->fb_lock;
	fb->vdev.release = video_device_release_empty;
	fb->vdev.device_caps =
		V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	fb->vdev.v4l2_dev = &fb->v4l2_dev;
	fb->vdev.queue = &fb->queue;
	video_set_drvdata(&fb->vdev, fb);
	res = video_register_device(&fb->vdev, VFL_TYPE_VIDEO, -1);
	if (res) {
		v4l2_device_unregister(&fb->v4l2_dev);
		return res;
	}

	/* initialize rest of driver struct */
	INIT_LIST_HEAD(&fb->bufs);
	spin_lock_init(&fb->bufs_lock);
	mutex_init(&fb->fb_lock);

	/* initialize hw */
	writel(1, fb->iobase + FB_RESET);
	writel(1, fb->iobase + FB_IODATARATE);
	writel(1, fb->iobase + FB_IOPIXELMODE);
	writel(FB_IRQ_ALL, fb->iobase + FB_IRQ_MASK);

	dev_info(fb->dev, "probed\n");
	return 0;
}

static int fb_remove(struct platform_device *pdev)
{
	struct chv3_fb *fb = platform_get_drvdata(pdev);

	video_unregister_device(&fb->vdev);
	v4l2_device_unregister(&fb->v4l2_dev);

	dev_info(fb->dev, "removed\n");
	return 0;
}

static const struct of_device_id fb_match_table[] = {
	{ .compatible = "google,fb-chameleonv3" },
};

static struct platform_driver fb_platform_driver = {
	.probe = fb_probe,
	.remove = fb_remove,
	.driver = {
		.name = MODULE_NAME,
		.of_match_table = fb_match_table,
	},
};

module_platform_driver(fb_platform_driver);

MODULE_LICENSE("GPL");
