/* SPDX-License-Identifier: GPL-2.0 */
/*
* Copyright (c) 2016 MediaTek Inc.
* Author: Tiffany Lin <tiffany.lin@mediatek.com>
*/

#ifndef _MTK_VCODEC_ENC_PM_H_
#define _MTK_VCODEC_ENC_PM_H_

#include "mtk_vcodec_enc_drv.h"

int mtk_vcodec_init_enc_clk(struct mtk_vcodec_enc_dev *dev);
int mtk_vcodec_enc_enable_hardware(struct mtk_vcodec_enc_ctx *ctx);
void mtk_vcodec_enc_disable_hardware(struct mtk_vcodec_enc_ctx *ctx);
void mtk_vcodec_enc_pm_frame_req(struct mtk_vcodec_enc_ctx *ctx);

#endif /* _MTK_VCODEC_ENC_PM_H_ */
