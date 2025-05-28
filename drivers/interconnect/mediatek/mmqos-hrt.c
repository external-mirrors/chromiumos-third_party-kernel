// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2025 MediaTek Inc.
 * Author: Echo.Zhang <echo.zhang@mediatek.com>
 */

#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/sched/clock.h>

#if IS_ENABLED(CONFIG_MTK_MMDVFS)
#include <soc/mediatek/mmdvfs_v3.h>
#endif

#include "mmqos-global.h"
#include "mmqos-mtk.h"

#define MULTIPLY_W_DRAM_WEIGHT(value)    ((value) * 6 / 5)
#define MULTIPLY_RATIO(value)            ((value) * 1000)
#define DIVIDE_RATIO(value)              ((value) / 1000)

static void record_hrt_bw(u32 avail_bw, struct mtk_mmqos *mmqos)
{
	struct hrt_record *rec;
	u32 pre_idx, idx;

	rec = &mmqos->hrt->hrt_rec;

	mutex_lock(&rec->lock);
	idx = rec->idx;
	pre_idx = (idx + RECORD_NUM - 1) % RECORD_NUM;
	if (rec->avail_hrt[pre_idx] != avail_bw) {
		rec->time[idx] = sched_clock();
		rec->avail_hrt[idx] = avail_bw;
		rec->idx = (idx + 1) % RECORD_NUM;
	}
	mutex_unlock(&rec->lock);
}

s32 mtk_mmqos_get_avail_hrt_bw(enum hrt_type type)
{
	struct mtk_mmqos *mmqos = mtk_mmqos_get_drv_data();
	struct mmqos_hrt *hrt = NULL;
	u32 i, used_bw = 0;
	u32 result = 0;

	if (type >= HRT_TYPE_NUM) {
		mmqos_err(mmqos->dev, "invalid type:%d.", type);
		return -ENOENT;
	}

	if (IS_ERR_OR_NULL(mmqos)) {
		mmqos_err(mmqos->dev, "not ready.");
		return -ENOENT;
	}

	hrt = mmqos->hrt;
	if (!hrt) {
		mmqos_err(mmqos->dev, "invalid hrt.");
		return -ENOENT;
	}


	for (i = 0; i < HRT_TYPE_NUM; i++) {
		if (i != type)
			used_bw += (MULTIPLY_RATIO(hrt->hrt_bw[i]) / hrt->hrt_ratio[i]);
	}

	result = DIVIDE_RATIO(hrt->hrt_total_bw * hrt->emi_ratio) - used_bw;

	result = DIVIDE_RATIO(result * hrt->hrt_ratio[type]);

	record_hrt_bw(result, mmqos);

	return result;
}
EXPORT_SYMBOL_GPL(mtk_mmqos_get_avail_hrt_bw);

s32 mtk_mmqos_set_hrt_bw(enum hrt_type type, u32 bw)
{
	struct mtk_mmqos *mmqos = mtk_mmqos_get_drv_data();

	if (IS_ERR_OR_NULL(mmqos)) {
		mmqos_err(mmqos->dev, "not ready");
		return -ENOENT;
	}

	if (type >= HRT_TYPE_NUM) {
		mmqos_err(mmqos->dev, "wrong type:%d\n", type);
		return -EINVAL;
	}

	if (!mmqos->hrt)
		return -ENOENT;

	if (mmqos->hrt->hrt_bw[type] != bw) {
		mmqos->hrt->hrt_bw[type] = bw;
		mmqos_dbg(mmqos->dev, LOG_BW, "type=%d bw=%d\n", type, bw);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mtk_mmqos_set_hrt_bw);

s32 mtk_mmqos_get_hrt_ratio(enum hrt_type type)
{
	struct mtk_mmqos *mmqos = mtk_mmqos_get_drv_data();

	if (IS_ERR_OR_NULL(mmqos)) {
		mmqos_err(mmqos->dev, "not ready");
		return 0;
	}

	if (type >= HRT_TYPE_NUM)
		return 1000;

	return mmqos->hrt->hrt_ratio[type];
}
EXPORT_SYMBOL_GPL(mtk_mmqos_get_hrt_ratio);

MODULE_LICENSE("GPL");
