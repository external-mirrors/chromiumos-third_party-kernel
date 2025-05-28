/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2025 MediaTek Inc.
 */

#ifndef __MTK_QOS_COMMON_H__
#define __MTK_QOS_COMMON_H__

struct platform_device;

struct mtk_qos_soc {
	const struct qos_ipi_cmd *ipi_pin;
	const struct qos_sram_addr *sram_pin;
};

struct mtk_qos {
	struct device *dev;
	const struct mtk_qos_soc *soc;
	int scmi_qos_id;
	bool qos_sspm_ready;
	struct scmi_tinysys_info_st *tinfo;
	struct mutex qos_ipi_mutex;
};

int mtk_qos_probe(struct platform_device *pdev);
int qos_ipi_init(struct mtk_qos *qos);
struct mtk_qos *qos_get_handle(void);

#endif
