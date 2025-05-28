/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019-2024 MediaTek Inc.
 */

#ifndef _MTK_dp_HDCP2_H_
#define _MTK_dp_HDCP2_H_

#include "tlc_dp_hdcp.h"

#define HDCP_VERSION_2X	2

enum check_link {
	LINK_PROTECTED,
	TOPOLOGY_CHANGE,
	LINK_INTEGRITY_FAILURE,
	REAUTH_REQUEST
};

void mtk_dp_hdcp2x_get_info(struct mtk_hdcp_info *hdcp_info);
void mtk_dp_hdcp2x_irq(struct mtk_hdcp_info *hdcp_info);

#if IS_ENABLED(CONFIG_TEE)
int mtk_dp_hdcp2x_enable(struct mtk_hdcp_info *hdcp_info);
int mtk_dp_hdcp2x_disable(struct mtk_hdcp_info *hdcp_info);
int mtk_dp_hdcp2x_check_link(struct mtk_hdcp_info *hdcp_info);
#else
static inline int mtk_dp_hdcp2x_enable(struct mtk_hdcp_info *hdcp_info)
{
	return -EFAULT;
}
static inline int mtk_dp_hdcp2x_disable(struct mtk_hdcp_info *hdcp_info)
{
	return -EFAULT;
}
static inline int mtk_dp_hdcp2x_check_link(struct mtk_hdcp_info *hdcp_info)
{
	return -EFAULT;
}
#endif
#endif /* _MTK_dp_HDCP2_H_ */
