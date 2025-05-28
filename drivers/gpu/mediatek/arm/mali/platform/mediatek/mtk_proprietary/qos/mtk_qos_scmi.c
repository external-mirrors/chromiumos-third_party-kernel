// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2025 MediaTek Inc.
 */

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/scmi_protocol.h>
#include "mtk_qos_common.h"
#include "mtk_qos_ipi.h"
#include "tinysys-scmi.h"

#define TO_STRING(CMD) #CMD
#define CHECK_COMMAND(CMD) \
	if (cmd == CMD)    \
	return TO_STRING(CMD)

static inline const char *to_string(enum QOS_IPI_COMMAND cmd)
{
	CHECK_COMMAND(QOS_IPI_QOS_ENABLE);
	CHECK_COMMAND(QOS_IPI_SETUP_GPU_INFO);
	return "UNDEFINED";
}

inline static int enable_gpu_qos(void)
{
	return qos_ipi_to_sspm_scmi_command(QOS_IPI_QOS_ENABLE, 0, 0, 0,
					    QOS_IPI_SCMI_SET);
}

int qos_ipi_to_sspm_scmi_command(enum QOS_IPI_COMMAND cmd, unsigned int p1,
				 unsigned int p2, unsigned int p3,
				 enum QOS_IPI_SCMI_COMMAND p4)
{
	int ret, ackdata = 0;
	struct mtk_qos *qos = qos_get_handle();
	struct scmi_tinysys_status rvalue = { 0 };

	if (unlikely(!qos)) {
		return -EAGAIN;
	}

	mutex_lock(&qos->qos_ipi_mutex);

	if (cmd >= NR_QOS_IPI) {
		dev_err(qos->dev, "@%s: qos ipi cmd get error %d\n", __func__,
			cmd);
		ret = -EACCES;
		goto ERROR;
	}

	if (!qos->qos_sspm_ready) {
		dev_err(qos->dev, "@%s: qos ipi not ready, skip cmd=%d",
			__func__, cmd);
		ret = -ENODEV;
		goto ERROR;
	}

	switch (p4) {
	case QOS_IPI_SCMI_SET:
		ret = scmi_tinysys_common_set(qos->tinfo->ph, qos->scmi_qos_id,
					      cmd, p1, p2, p3, p4);
		if (ret) {
			dev_err(qos->dev,
				"@%s: qos ipi cmd %d send fail ret %d",
				__func__, cmd, ret);
			ret = -EINVAL;
			goto ERROR;
		}
		dev_info(qos->dev,
			 "@%s: qos send ipi to sspm cmd %d (%s) success",
			 __func__, cmd, to_string(cmd));
		ackdata = rvalue.r1;
		break;
	case QOS_IPI_SCMI_GET:
		ret = scmi_tinysys_common_get(qos->tinfo->ph, qos->scmi_qos_id,
					      cmd, &rvalue);
		if (ret) {
			dev_err(qos->dev,
				"@%s: qos ipi cmd %d ack fail ret: %d, return_val: %d %d",
				__func__, cmd, ret, rvalue.r1, rvalue.r2);
			ret = -EINVAL;
			goto ERROR;
		}
		ackdata = rvalue.r1;
		dev_dbg(qos->dev,
			"@%s: QOS_IPI_SCMI_GET, ackdata %d, %d, %d, ret: %d",
			__func__, rvalue.r1, rvalue.r2, rvalue.r3, ret);

		if (!ackdata) {
			dev_err(qos->dev,
				"@%s: qos ipi cmd %d ack fail, ackdata: %d",
				__func__, cmd, ackdata);
			ret = -ENODATA;
			goto ERROR;
		}
		break;
	}

	mutex_unlock(&qos->qos_ipi_mutex);
	return ackdata;
ERROR:
	mutex_unlock(&qos->qos_ipi_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(qos_ipi_to_sspm_scmi_command);

int qos_ipi_init(struct mtk_qos *qos)
{
	int ret;

	if (!qos || !qos->dev) {
		dev_err(qos->dev, "@%s: input parameter is invalid", __func__);
		goto fail;
	}

	qos->tinfo = get_scmi_tinysys_info();
	ret = of_property_read_u32(qos->tinfo->sdev->dev.of_node, "scmi-qos",
				   &qos->scmi_qos_id);
	if (ret) {
		dev_err(qos->dev, "@%s: get scmi-qos fail, ret %d", __func__,
			ret);
		goto fail;
	}

	ret = scmi_tinysys_event_notify(qos->scmi_qos_id, 1);
	if (ret) {
		dev_err(qos->dev, "@%s: qos event notify fail", __func__);
		goto fail;
	}

	qos->qos_sspm_ready = true;
	if (enable_gpu_qos()) {
		dev_err(qos->dev, "@%s: enable gpu qos fail", __func__);
		goto fail;
	}
	dev_info(qos->dev, "@%s: done", __func__);
	return 0;
fail:
	qos->qos_sspm_ready = false;
	dev_err(qos->dev, "@%s: fail", __func__);
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(qos_ipi_init);
