#include "chv3-fb.h"
#include "dprx.h"

struct chv3_video {
	struct device *dev;
	struct v4l2_device v4l2_dev;

	struct chv3_fb fb0;
	struct chv3_fb fb_mst[4];
	struct chv3_fb fb_sst;

	struct dprx_dp dp_mst;
	struct dprx_dp dp_sst;
};

int chv3_g_edid(struct chv3_video *video, int index, struct v4l2_edid *edid);
int chv3_s_edid(struct chv3_video *video, int index, struct v4l2_edid *edid);
