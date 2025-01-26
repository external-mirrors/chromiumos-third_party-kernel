/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019-2024 MediaTek Inc.
 */

#ifndef _MTK_DP_HDCP1X_H_
#define _MTK_DP_HDCP1X_H_

#include "tlc_dp_hdcp.h"

#define HDCP_VERSION_1X 1

void mtk_dp_hdcp1x_get_info(struct mtk_hdcp_info *hdcp_info);

#if IS_ENABLED(CONFIG_TEE)
int mtk_dp_hdcp1x_enable(struct mtk_hdcp_info *hdcp_info);
int mtk_dp_hdcp1x_disable(struct mtk_hdcp_info *hdcp_info);
int mtk_dp_hdcp1x_check_link(struct mtk_hdcp_info *hdcp_info);
#else
static inline int mtk_dp_hdcp1x_enable(struct mtk_hdcp_info *hdcp_info)
{
	return -EFAULT;
}
static inline int mtk_dp_hdcp1x_disable(struct mtk_hdcp_info *hdcp_info)
{
	return -EFAULT;
}
static inline int mtk_dp_hdcp1x_check_link(struct mtk_hdcp_info *hdcp_info)
{
	return -EFAULT;
}
#endif
#endif /* _MTK_DP_HDCP1X_H_ */
