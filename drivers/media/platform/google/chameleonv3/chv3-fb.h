struct chv3_fb {
	struct device *dev;
	void __iomem *iobase;
	void __iomem *iobase_irq;
	struct chv3_video *parent;
	int index;

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

struct chv3_fb_cfg {
	const char *reg_core;
	const char *reg_irq;
	const char *irq;
	int index;
};

int chv3_fb_register(struct chv3_fb *fb,
		     struct chv3_video *video,
		     const struct chv3_fb_cfg *cfg);


void chv3_fb_unregister(struct chv3_fb *fb);
