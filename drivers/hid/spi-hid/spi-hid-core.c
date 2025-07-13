// SPDX-License-Identifier: GPL-2.0
/*
 * HID over SPI protocol implementation
 *
 * Copyright (c) 2021 Microsoft Corporation
 *
 * This code is partly based on "HID over I2C protocol implementation:
 *
 *  Copyright (c) 2012 Benjamin Tissoires <benjamin.tissoires@gmail.com>
 *  Copyright (c) 2012 Ecole Nationale de l'Aviation Civile, France
 *  Copyright (c) 2012 Red Hat, Inc
 *
 *  which in turn is partly based on "USB HID support for Linux":
 *
 *  Copyright (c) 1999 Andreas Gal
 *  Copyright (c) 2000-2005 Vojtech Pavlik <vojtech@suse.cz>
 *  Copyright (c) 2005 Michael Haboustak <mike-@cinci.rr.com> for Concept2, Inc
 *  Copyright (c) 2007-2008 Oliver Neukum
 *  Copyright (c) 2006-2010 Jiri Kosina
 */

#include <asm/unaligned.h>
#include <linux/crc32.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/hid.h>
#include <linux/hid-over-spi.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/string.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include "../hid-ids.h"
#include "spi-hid-core.h"

#define CREATE_TRACE_POINTS
#include <trace/events/spi_hid.h>

/* quirks to control the device */
#define SPI_HID_QUIRK_MODE_SWITCH	BIT(0)

/* Protocol constants */
#define SPI_HID_READ_APPROVAL_CONSTANT		0xff
#define SPI_HID_INPUT_HEADER_SYNC_BYTE		0x5a
#define SPI_HID_INPUT_HEADER_VERSION		0x03
#define SPI_HID_SUPPORTED_VERSION		0x0300

#define SPI_HID_OUTPUT_REPORT_CONTENT_ID_DESC_REQUEST	0x00

#define SPI_HID_MAX_RESET_ATTEMPTS	3
#define SPI_HID_RESPONSE_TIMEOUT_MS	1000

static const struct spi_hid_quirks {
	__u16 idVendor;
	__u16 idProduct;
	__u32 quirks;
} spi_hid_quirks[] = {
	{ USB_VENDOR_ID_ILITEK, HID_ANY_ID,
		SPI_HID_QUIRK_MODE_SWITCH },
	{ 0, 0 }
};

static struct hid_ll_driver spi_hid_ll_driver;

/*
 * spi_hid_lookup_quirk: return any quirks associated with a SPI HID device
 * @idVendor: the 16-bit vendor ID
 * @idProduct: the 16-bit product ID
 *
 * Returns: a u32 quirks value.
 */
static u32 spi_hid_lookup_quirk(const u16 idVendor, const u16 idProduct)
{
	u32 quirks = 0;
	int n;

	for (n = 0; spi_hid_quirks[n].idVendor; n++)
		if (spi_hid_quirks[n].idVendor == idVendor &&
		    (spi_hid_quirks[n].idProduct == (__u16)HID_ANY_ID ||
		     spi_hid_quirks[n].idProduct == idProduct))
			quirks = spi_hid_quirks[n].quirks;

	return quirks;
}


static void spi_hid_populate_read_approvals(const struct spi_hid_conf *conf,
					    u8 *header_buf, u8 *body_buf)
{
	header_buf[0] = conf->read_opcode;
	put_unaligned_be24(conf->input_report_header_address, &header_buf[1]);
	header_buf[4] = SPI_HID_READ_APPROVAL_CONSTANT;

	body_buf[0] = conf->read_opcode;
	put_unaligned_be24(conf->input_report_body_address, &body_buf[1]);
	body_buf[4] = SPI_HID_READ_APPROVAL_CONSTANT;
}

static void spi_hid_parse_dev_desc(const struct hidspi_dev_descriptor *raw,
				   struct spi_hid_device_descriptor *desc)
{
	desc->hid_version = le16_to_cpu(raw->bcd_ver);
	desc->report_descriptor_length = le16_to_cpu(raw->rep_desc_len);
	desc->max_input_length = le16_to_cpu(raw->max_input_len);
	desc->max_output_length = le16_to_cpu(raw->max_output_len);

	/* FIXME: multi-fragment not supported, field below not used */
	desc->max_fragment_length = le16_to_cpu(raw->max_frag_len);

	desc->vendor_id = le16_to_cpu(raw->vendor_id);
	desc->product_id = le16_to_cpu(raw->product_id);
	desc->version_id = le16_to_cpu(raw->version_id);
	desc->no_output_report_ack = le16_to_cpu(raw->flags) & BIT(0);
}

static void spi_hid_populate_input_header(const u8 *buf,
					  struct spi_hid_input_header *header)
{
	header->version            = buf[0] & 0xf;
	header->report_length      = (buf[1] | ((buf[2] & 0x3f) << 8)) * 4;
	header->last_fragment_flag = (buf[2] & 0x40) >> 6;
	header->sync_const         = buf[3];
}

static void spi_hid_populate_input_body(const u8 *buf,
					struct input_report_body_header *body)
{
	body->input_report_type = buf[0];
	body->content_len = buf[1] | (buf[2] << 8);
	body->content_id = buf[3];
}

static void spi_hid_input_report_prepare(struct spi_hid_input_buf *buf,
					 struct spi_hid_input_report *report)
{
	struct spi_hid_input_header header;
	struct input_report_body_header body;

	spi_hid_populate_input_header(buf->header, &header);
	spi_hid_populate_input_body(buf->body, &body);
	report->report_type = body.input_report_type;
	report->content_length = body.content_len;
	report->content_id = body.content_id;
	report->content = buf->content;
}

static void spi_hid_populate_output_header(u8 *buf,
				const struct spi_hid_conf *conf,
				const struct spi_hid_output_report *report)
{
	buf[0] = conf->write_opcode;
	put_unaligned_be24(conf->output_report_address, &buf[1]);
	buf[4] = report->report_type;
	buf[5] = report->content_length & 0xff;
	buf[6] = (report->content_length >> 8) & 0xff;
	buf[7] = report->content_id;
}

static int spi_hid_input_sync(struct spi_hid *shid, void *buf, u16 length,
			      bool is_header)
{
	int ret;
	struct device *dev = &shid->spi->dev;

	shid->input_transfer[0].tx_buf = is_header ?
					 shid->read_approval_header :
					 shid->read_approval_body;
	shid->input_transfer[0].len = SPI_HID_READ_APPROVAL_LEN;

	shid->input_transfer[1].rx_buf = buf;
	shid->input_transfer[1].len = length;

	spi_message_init_with_transfers(&shid->input_message,
					shid->input_transfer, 2);

	trace_spi_hid_input_sync(shid,	shid->input_transfer[0].tx_buf,
				 shid->input_transfer[0].len,
				 shid->input_transfer[1].rx_buf,
				 shid->input_transfer[1].len, 0);

	ret = spi_sync(shid->spi, &shid->input_message);
	if (ret) {
		dev_err(dev, "Error starting sync transfer: %d, resetting.",
			ret);
		shid->bus_error_count++;
		shid->bus_last_error = ret;
		schedule_work(&shid->error_work);
	}

	return ret;
}

static int spi_hid_output(struct spi_hid *shid, const void *buf, u16 length)
{
	struct spi_transfer transfer;
	struct spi_message message;
	int ret;

	memset(&transfer, 0, sizeof(transfer));

	transfer.tx_buf = buf;
	transfer.len = length;

	spi_message_init_with_transfers(&message, &transfer, 1);

	trace_spi_hid_output_begin(shid, transfer.tx_buf, transfer.len, NULL,
				   0, 0);

	ret = spi_sync(shid->spi, &message);

	trace_spi_hid_output_end(shid, transfer.tx_buf, transfer.len, NULL, 0,
				 ret);

	if (ret) {
		shid->bus_error_count++;
		shid->bus_last_error = ret;
	}

	return ret;
}

static const char *spi_hid_power_mode_string(enum hidspi_power_state power_state)
{
	switch (power_state) {
	case HIDSPI_ON:
		return "d0";
	case HIDSPI_SLEEP:
		return "d2";
	case HIDSPI_OFF:
		return "d3";
	default:
		return "unknown";
	}
}

static void spi_hid_suspend(struct spi_hid *shid)
{

}

static void spi_hid_resume(struct spi_hid *shid)
{

}

static struct hid_device *spi_hid_disconnect_hid(struct spi_hid *shid)
{
	struct hid_device *hid = shid->hid;

	shid->hid = NULL;
	shid->ready = false;

	return hid;
}

static void spi_hid_stop_hid(struct spi_hid *shid)
{
	struct hid_device *hid;

	cancel_work_sync(&shid->create_device_work);
	cancel_work_sync(&shid->refresh_device_work);

	guard(mutex)(&shid->power_lock);
	hid = spi_hid_disconnect_hid(shid);
	if (hid)
		hid_destroy_device(hid);
}

static void spi_hid_error_work(struct work_struct *work)
{
	struct spi_hid *shid = container_of(work, struct spi_hid, error_work);
	struct device *dev = &shid->spi->dev;
	int error;

	mutex_lock(&shid->power_lock);
	if (shid->power_state == HIDSPI_OFF)
		goto out;

	if (shid->reset_attempts++ >= SPI_HID_MAX_RESET_ATTEMPTS) {
		dev_err(dev, "unresponsive device, aborting.");
		/* Drop the lock before calling the synchronous stop function */
		mutex_unlock(&shid->power_lock);
		spi_hid_stop_hid(shid);
		shid->ops->assert_reset(shid->ops);
		error = shid->ops->power_down(shid->ops);
		if (error) {
			dev_err(dev, "failed to disable regulator.");
			shid->regulator_error_count++;
			shid->regulator_last_error = error;
		}
		return;
	}

	shid->ready = false;
	shid->reset_pending = true;

	shid->ops->assert_reset(shid->ops);

	shid->power_state = HIDSPI_OFF;

	cancel_work_sync(&shid->reset_response_work);
	cancel_work(&shid->error_work);

	shid->ops->sleep_minimal_reset_delay(shid->ops);

	shid->power_state = HIDSPI_ON;

	shid->ops->deassert_reset(shid->ops);

out:
	mutex_unlock(&shid->power_lock);
}

static int spi_hid_send_output_report(struct spi_hid *shid,
				      struct spi_hid_output_report *report)
{
	struct spi_hid_output_buf *buf = shid->output;
	struct device *dev = &shid->spi->dev;
	u16 report_length;
	u16 padded_length;
	u8 padding;
	int ret;

	guard(mutex)(&shid->output_lock);
	if (report->content_length > shid->desc.max_output_length) {
		dev_err(dev, "Output report too big, content_length 0x%x.",
			report->content_length);
		ret = -E2BIG;
		goto out;
	}

	spi_hid_populate_output_header(buf->header, shid->conf, report);

	if (report->content_length)
		memcpy(&buf->content, report->content, report->content_length);

	report_length = sizeof(buf->header) + report->content_length;
	padded_length = round_up(report_length,	4);
	padding = padded_length - report_length;
	memset(&buf->content[report->content_length], 0, padding);

	ret = spi_hid_output(shid, buf, padded_length);
	if (ret)
		dev_err(dev, "Failed output transfer.");

out:
	return ret;
}

static const u32 spi_hid_get_timeout(struct spi_hid *shid)
{
	struct device *dev = &shid->spi->dev;
	u32 timeout;

	timeout = READ_ONCE(shid->ops->response_timeout_ms);

	if (timeout < SPI_HID_RESPONSE_TIMEOUT_MS || timeout > 10000) {
		dev_warn(dev, "Response timeout is out of range, using default %d",
			SPI_HID_RESPONSE_TIMEOUT_MS);
		timeout = SPI_HID_RESPONSE_TIMEOUT_MS;
	}

	return timeout;
}

static int spi_hid_sync_request(struct spi_hid *shid,
				struct spi_hid_output_report *report)
{
	struct device *dev = &shid->spi->dev;
	u32 timeout;
	int ret = 0;

	ret = spi_hid_send_output_report(shid, report);
	if (ret)
		return ret;

	if (shid->quirks & SPI_HID_QUIRK_MODE_SWITCH)
		timeout = spi_hid_get_timeout(shid);
	else
		timeout = SPI_HID_RESPONSE_TIMEOUT_MS;

	mutex_lock(&shid->data_lock);
	ret = wait_for_completion_interruptible_timeout(&shid->output_done,
							msecs_to_jiffies(timeout));
	mutex_unlock(&shid->data_lock);
	if (ret == 0) {
		dev_err(dev, "Response timed out.");
		return -ETIMEDOUT;
	}

	return 0;
}

/**
 * Handle the reset response from the FW by sending a request for the device
 * descriptor.
 */
static void spi_hid_reset_response_work(struct work_struct *work)
{
	struct spi_hid *shid =
		container_of(work, struct spi_hid, reset_response_work);
	struct device *dev = &shid->spi->dev;
	struct spi_hid_output_report report = {
		.report_type = DEVICE_DESCRIPTOR,
		.content_length = 0x0,
		.content_id = SPI_HID_OUTPUT_REPORT_CONTENT_ID_DESC_REQUEST,
		.content = NULL,
	};
	int ret;

	trace_spi_hid_reset_response_work(shid);

	if (shid->ready) {
		dev_err(dev, "Spontaneous FW reset!");
		shid->ready = false;
		shid->dir_count++;
	}

	if (shid->power_state == HIDSPI_OFF)
		return;

	if (flush_work(&shid->create_device_work))
		dev_err(dev, "Reset handler waited for create_device_work");

	if (flush_work(&shid->refresh_device_work))
		dev_err(dev, "Reset handler waited for refresh_device_work");

	ret = spi_hid_sync_request(shid, &report);
	if (ret) {
		dev_WARN_ONCE(dev, true,
			      "Failed to send device descriptor request.");
		schedule_work(&shid->error_work);
	}
}

static int spi_hid_input_report_handler(struct spi_hid *shid,
		struct spi_hid_input_buf *buf)
{
	struct device *dev = &shid->spi->dev;
	struct spi_hid_input_report r;
	int error = 0;

	trace_spi_hid_input_report_handler(shid);

	if (!shid->ready || shid->refresh_in_progress || !shid->hid) {
		dev_err(dev, "HID not ready");
		return 0;
	}

	spi_hid_input_report_prepare(buf, &r);

	error = hid_input_report(shid->hid, HID_INPUT_REPORT,
					r.content - 1, r.content_length + 1, 1);

	if (error == -ENODEV || error == -EBUSY) {
		dev_err(dev, "ignoring report --> %d.", error);
		return 0;
	} else if (error) {
		dev_err(dev, "Bad input report, error %d.", error);
	}

	return error;
}

static void spi_hid_response_handler(struct spi_hid *shid,
				     struct input_report_body_header *body)
{
	trace_spi_hid_response_handler(shid);

	shid->response_length = body->content_len;
	/* completion_done returns 0 if there are waiters, otherwise 1 */
	if (completion_done(&shid->output_done)) {
		dev_err(&shid->spi->dev, "Unexpected response report.");
	} else {
		if (body->input_report_type == REPORT_DESCRIPTOR_RESPONSE ||
			body->input_report_type == GET_FEATURE_RESPONSE) {
			memcpy(shid->response->body, shid->input->body,
			       sizeof(shid->input->body));
			memcpy(shid->response->content, shid->input->content,
			       body->content_len);
		}
		complete(&shid->output_done);
	}
}

/*
 * This function returns the length of the report descriptor, or a negative
 * error code if something went wrong.
 */
static int spi_hid_report_descriptor_request(struct spi_hid *shid)
{
	int ret;
	struct device *dev = &shid->spi->dev;
	struct spi_hid_output_report report = {
		.report_type = REPORT_DESCRIPTOR,
		.content_length = 0,
		.content_id = SPI_HID_OUTPUT_REPORT_CONTENT_ID_DESC_REQUEST,
		.content = NULL,
	};

	ret =  spi_hid_sync_request(shid, &report);
	if (ret) {
		dev_err(dev,
			"Expected report descriptor not received! Error %d.",
			ret);
		schedule_work(&shid->error_work);
		goto out;
	}

	ret = shid->response_length;
	if (ret != shid->desc.report_descriptor_length) {
		dev_err(dev, "Received report descriptor length doesn't match device descriptor field, using min of the two: %d.",
			ret);
		schedule_work(&shid->error_work);
		ret = -EINVAL;
	}
out:
	return ret;
}

static int spi_hid_create_device(struct spi_hid *shid)
{
	struct hid_device *hid;
	struct device *dev = &shid->spi->dev;
	int ret;

	hid = hid_allocate_device();

	if (IS_ERR(hid)) {
		dev_err(dev, "Failed to allocate hid device: %ld.",
			PTR_ERR(hid));
		ret = PTR_ERR(hid);
		return ret;
	}

	hid->driver_data = shid->spi;
	hid->ll_driver = &spi_hid_ll_driver;
	hid->dev.parent = &shid->spi->dev;
	hid->bus = BUS_SPI;
	hid->version = shid->desc.hid_version;
	hid->vendor = shid->desc.vendor_id;
	hid->product = shid->desc.product_id;

	shid->quirks = spi_hid_lookup_quirk(hid->vendor, hid->product);

	snprintf(hid->name, sizeof(hid->name), "spi %04X:%04X",
		 hid->vendor, hid->product);
	strscpy(hid->phys, dev_name(&shid->spi->dev), sizeof(hid->phys));

	shid->hid = hid;

	ret = hid_add_device(hid);
	if (ret) {
		dev_err(dev, "Failed to add hid device: %d.", ret);
		/*
		 * We likely got here because report descriptor request timed
		 * out. Let's disconnect and destroy the hid_device structure.
		 */
		hid = spi_hid_disconnect_hid(shid);
		if (hid)
			hid_destroy_device(hid);
		return ret;
	}

	return 0;
}

static void spi_hid_create_device_work(struct work_struct *work)
{
	struct spi_hid *shid =
		container_of(work, struct spi_hid, create_device_work);
	struct device *dev = &shid->spi->dev;
	u8 prev_state;
	int ret = 0;

	trace_spi_hid_create_device_work(shid);

	guard(mutex)(&shid->power_lock);
	prev_state = shid->power_state;
	if (prev_state == HIDSPI_OFF) {
		dev_err(dev, "%s: Powered off, returning", __func__);
		goto out;
	}

	ret = spi_hid_create_device(shid);
	if (ret) {
		dev_err(dev, "%s: Failed to create hid device.", __func__);
		goto out;
	}

out:
	dev_dbg(dev, "%s: %s -> %s.", __func__,
		spi_hid_power_mode_string(prev_state),
		spi_hid_power_mode_string(shid->power_state));
}

static void spi_hid_refresh_device_work(struct work_struct *work)
{
	struct spi_hid *shid =
		container_of(work, struct spi_hid, refresh_device_work);
	struct device *dev = &shid->spi->dev;
	struct hid_device *hid;
	u32 new_crc32 = 0;
	int error = 0;

	trace_spi_hid_refresh_device_work(shid);

	guard(mutex)(&shid->power_lock);
	if (shid->power_state == HIDSPI_OFF) {
		dev_err(dev, "%s: Powered off, returning", __func__);
		return;
	}

	error = spi_hid_report_descriptor_request(shid);
	if (error < 0) {
		dev_err(dev,
			"Refresh: failed report descriptor request, error %d",
			error);
		return;
	}
	new_crc32 = crc32_le(0, (unsigned char const *)shid->response->content,
					(size_t)error);

	/* Same report descriptor, so no need to create a new hid device. */
	if (new_crc32 == shid->report_descriptor_crc32) {
		shid->ready = true;
		return;
	}

	shid->report_descriptor_crc32 = new_crc32;

	shid->refresh_in_progress = true;
	hid = spi_hid_disconnect_hid(shid);

	if (hid) {
		/* Function hid_destroy_device() must wait for spi_hid_ll_open()
		 * to finish, because hid_hw_open() and hid_hw_close()
		 * use the same mutex. That is why we mark ready_done above.
		 */
		hid_destroy_device(hid);
	}

	error = spi_hid_create_device(shid);
	if (error) {
		dev_err(dev, "%s: Failed to create hid device.", __func__);
		return;
	}

	shid->refresh_in_progress = false;
}

static void spi_hid_process_input_report(struct spi_hid *shid,
					 struct spi_hid_input_buf *buf)
{
	struct spi_hid_input_header header;
	struct input_report_body_header body;
	struct device *dev = &shid->spi->dev;
	struct hidspi_dev_descriptor *raw;
	int ret;

	trace_spi_hid_process_input_report(shid);

	spi_hid_populate_input_header(buf->header, &header);
	spi_hid_populate_input_body(buf->body, &body);

	if (body.content_len > header.report_length) {
		dev_err(dev, "Bad body length %d > %d.", body.content_len,
			header.report_length);
		schedule_work(&shid->error_work);
		return;
	}

	switch (body.input_report_type) {
	case DATA:
		ret = spi_hid_input_report_handler(shid, buf);
		if (ret)
			schedule_work(&shid->error_work);
		break;
	case RESET_RESPONSE:
		shid->reset_pending = false;
		schedule_work(&shid->reset_response_work);
		break;
	case DEVICE_DESCRIPTOR_RESPONSE:
		/* Mark the completion done to avoid timeout */
		spi_hid_response_handler(shid, &body);

		/* Reset attempts at every device descriptor fetch */
		shid->reset_attempts = 0;
		raw = (struct hidspi_dev_descriptor *)buf->content;

		/* Validate device descriptor length before parsing */
		if (body.content_len != HIDSPI_DEVICE_DESCRIPTOR_SIZE) {
			dev_err(dev, "Invalid content length %d, expected %zu.",
				body.content_len,
				HIDSPI_DEVICE_DESCRIPTOR_SIZE);
			schedule_work(&shid->error_work);
			break;
		}

		if (le16_to_cpu(raw->dev_desc_len) !=
		    HIDSPI_DEVICE_DESCRIPTOR_SIZE) {
			dev_err(dev,
				"Invalid wDeviceDescLength %d, expected %zu.",
				raw->dev_desc_len,
				HIDSPI_DEVICE_DESCRIPTOR_SIZE);
			schedule_work(&shid->error_work);
			break;
		}

		spi_hid_parse_dev_desc(raw, &shid->desc);

		if (shid->desc.hid_version != SPI_HID_SUPPORTED_VERSION) {
			dev_err(dev,
				"Unsupported device descriptor version %4x.",
				shid->desc.hid_version);
			schedule_work(&shid->error_work);
			break;
		}

		if (!shid->hid)
			schedule_work(&shid->create_device_work);
		else
			schedule_work(&shid->refresh_device_work);

		break;
	case OUTPUT_REPORT_RESPONSE:
		if (shid->desc.no_output_report_ack) {
			dev_err(dev, "Unexpected output report response.");
			break;
		}
		fallthrough;
	case GET_FEATURE_RESPONSE:
	case SET_FEATURE_RESPONSE:
	case REPORT_DESCRIPTOR_RESPONSE:
		spi_hid_response_handler(shid, &body);
		break;
	/*
	 * FIXME: sending GET_INPUT and COMMAND reports not supported, thus
	 * throw away responses to those, they should never come.
	 */
	case GET_INPUT_REPORT_RESPONSE:
	case COMMAND_RESPONSE:
		dev_err(dev, "Not a supported report type: 0x%x.",
			body.input_report_type);
		break;
	default:
		dev_err(dev, "Unknown input report: 0x%x.", body.input_report_type);
		schedule_work(&shid->error_work);
		break;
	}
}

static int spi_hid_bus_validate_header(struct spi_hid *shid,
				       struct spi_hid_input_header *header)
{
	struct device *dev = &shid->spi->dev;

	if (header->version != SPI_HID_INPUT_HEADER_VERSION) {
		dev_err(dev, "Unknown input report version (v 0x%x).",
			header->version);
		return -EINVAL;
	}

	if (shid->desc.max_input_length != 0 &&
	    header->report_length > shid->desc.max_input_length) {
		dev_err(dev, "Input report body size %u > max expected of %u.",
			header->report_length, shid->desc.max_input_length);
		return -EMSGSIZE;
	}

	if (header->last_fragment_flag != 1) {
		dev_err(dev, "Multi-fragment reports not supported.");
		return -EOPNOTSUPP;
	}

	if (header->sync_const != SPI_HID_INPUT_HEADER_SYNC_BYTE) {
		dev_err(dev, "Invalid input report sync constant (0x%x).",
			header->sync_const);
		return -EINVAL;
	}

	return 0;
}

static int spi_hid_get_request(struct spi_hid *shid, u8 content_id)
{
	int ret;
	struct device *dev = &shid->spi->dev;
	struct spi_hid_output_report report = {
		.report_type = GET_FEATURE,
		.content_length = 0,
		.content_id = content_id,
		.content = NULL,
	};

	ret = spi_hid_sync_request(shid, &report);
	if (ret) {
		dev_err(dev,
			"Expected get request response not received! Error %d.",
			ret);
		schedule_work(&shid->error_work);
	}

	return ret;
}

static int spi_hid_set_request(struct spi_hid *shid, u8 *arg_buf, u16 arg_len,
			       u8 content_id)
{
	struct spi_hid_output_report report = {
		.report_type = SET_FEATURE,
		.content_length = arg_len,
		.content_id = content_id,
		.content = arg_buf,
	};

	return spi_hid_sync_request(shid, &report);
}

static irqreturn_t spi_hid_dev_irq(int irq, void *_shid)
{
	struct spi_hid *shid = _shid;
	struct device *dev = &shid->spi->dev;
	struct spi_hid_input_header header;
	int ret = 0;

	trace_spi_hid_dev_irq(shid, irq);
	trace_spi_hid_header_transfer(shid);

	ret = spi_hid_input_sync(shid, shid->input->header,
				 sizeof(shid->input->header), true);
	if (ret) {
		dev_err(dev, "Failed to transfer header: %d.", ret);
		goto out;
	}

	if (shid->power_state == HIDSPI_OFF) {
		dev_warn(dev, "Device is off after header was received.");
		goto out;
	}

	if (shid->quirks & SPI_HID_QUIRK_MODE_SWITCH) {
		u32 timeout = spi_hid_get_timeout(shid);

		if (timeout > SPI_HID_RESPONSE_TIMEOUT_MS)
			shid->reset_pending = true;
		else
			shid->reset_pending = false;
	}

	trace_spi_hid_input_header_complete(shid,
					    shid->input_transfer[0].tx_buf,
					    shid->input_transfer[0].len,
					    shid->input_transfer[1].rx_buf,
					    shid->input_transfer[1].len,
					    shid->input_message.status);

	if (shid->input_message.status < 0) {
		dev_warn(dev, "Error reading header: %d.",
			 shid->input_message.status);
		shid->bus_error_count++;
		shid->bus_last_error = shid->input_message.status;
		schedule_work(&shid->error_work);
		goto out;
	}

	spi_hid_populate_input_header(shid->input->header, &header);

	ret = spi_hid_bus_validate_header(shid, &header);
	if (ret) {
		if (!shid->reset_pending) {
			dev_err(dev, "Failed to validate header: %d.", ret);
			print_hex_dump(KERN_ERR, "spi_hid: header buffer: ",
					DUMP_PREFIX_NONE, 16, 1, shid->input->header,
					sizeof(shid->input->header), false);
			shid->bus_error_count++;
			shid->bus_last_error = ret;
			schedule_work(&shid->error_work);
		}
		goto out;
	}

	ret = spi_hid_input_sync(shid, shid->input->body, header.report_length,
				 false);
	if (ret)
		dev_err(dev, "Failed to transfer body: %d.", ret);

	if (shid->power_state == HIDSPI_OFF) {
		dev_warn(dev, "Device is off after body was received.");
		goto out;
	}

	trace_spi_hid_input_body_complete(shid, shid->input_transfer[0].tx_buf,
					  shid->input_transfer[0].len,
					  shid->input_transfer[1].rx_buf,
					  shid->input_transfer[1].len,
					  shid->input_message.status);

	if (shid->input_message.status < 0) {
		dev_warn(dev, "Error reading body: %d.",
			 shid->input_message.status);
		shid->bus_error_count++;
		shid->bus_last_error = shid->input_message.status;
		schedule_work(&shid->error_work);
		goto out;
	}

	spi_hid_process_input_report(shid, shid->input);

out:
	return IRQ_HANDLED;
}

static void spi_hid_free_buffers(struct spi_hid *shid)
{
	kfree(shid->output);
	kfree(shid->input);
	kfree(shid->response);
	shid->output = NULL;
	shid->input = NULL;
	shid->response = NULL;
	shid->bufsize = 0;
}

static int spi_hid_alloc_buffers(struct spi_hid *shid, size_t report_size)
{
	int inbufsize = sizeof(shid->input->header) + sizeof(shid->input->body) + report_size;
	int outbufsize = sizeof(shid->output->header) + report_size;

	shid->output = kzalloc(outbufsize, GFP_KERNEL);
	shid->input = kzalloc(inbufsize, GFP_KERNEL);
	shid->response = kzalloc(inbufsize, GFP_KERNEL);

	if (!shid->output || !shid->input || !shid->response) {
		spi_hid_free_buffers(shid);
		return -ENOMEM;
	}

	shid->bufsize = report_size;

	return 0;
}

static int spi_hid_get_report_length(struct hid_report *report)
{
	return ((report->size - 1) >> 3) + 1 +
		report->device->report_enum[report->type].numbered + 2;
}

/*
 * Traverse the supplied list of reports and find the longest
 */
static void spi_hid_find_max_report(struct hid_device *hid, u32 type,
		u16 *max)
{
	struct hid_report *report;
	u16 size;

	/* We should not rely on wMaxInputLength, as some devices may set it to
	 * a wrong length.
	 */
	list_for_each_entry(report, &hid->report_enum[type].report_list, list) {
		size = spi_hid_get_report_length(report);
		if (*max < size)
			*max = size;
	}
}

/* hid_ll_driver interface functions */

static int spi_hid_ll_start(struct hid_device *hid)
{
	struct spi_device *spi = hid->driver_data;
	struct spi_hid *shid = spi_get_drvdata(spi);
	int ret = 0;
	u16 bufsize = 0;

	spi_hid_find_max_report(hid, HID_INPUT_REPORT, &bufsize);
	spi_hid_find_max_report(hid, HID_OUTPUT_REPORT, &bufsize);
	spi_hid_find_max_report(hid, HID_FEATURE_REPORT, &bufsize);

	if (bufsize < HID_MIN_BUFFER_SIZE) {
		dev_err(&spi->dev,
			"HID_MIN_BUFFER_SIZE > max_input_length (%d).",
			bufsize);
		return -EINVAL;
	}

	if (bufsize > shid->bufsize) {
		shid->irq_enabled = false;
		disable_irq(shid->spi->irq);
		spi_hid_free_buffers(shid);

		ret = spi_hid_alloc_buffers(shid, bufsize);
		enable_irq(shid->spi->irq);
		shid->irq_enabled = true;
	}

	return ret;
}

static void spi_hid_ll_stop(struct hid_device *hid)
{
	hid->claimed = 0;
}

static int spi_hid_ll_open(struct hid_device *hid)
{
	struct spi_device *spi = hid->driver_data;
	struct spi_hid *shid = spi_get_drvdata(spi);

	if (shid->refresh_in_progress)
		return -EAGAIN;

	shid->ready = true;
	return 0;
}

static void spi_hid_ll_close(struct hid_device *hid)
{
	struct spi_device *spi = hid->driver_data;
	struct spi_hid *shid = spi_get_drvdata(spi);

	if (shid->refresh_in_progress)
		return;

	shid->ready = false;
	shid->reset_attempts = 0;
}

static int spi_hid_ll_power(struct hid_device *hid, int level)
{
	struct spi_device *spi = hid->driver_data;
	struct spi_hid *shid = spi_get_drvdata(spi);
	int ret = 0;

	if (!shid->hid)
		ret = -ENODEV;

	return ret;
}

static int spi_hid_ll_parse(struct hid_device *hid)
{
	struct spi_device *spi = hid->driver_data;
	struct spi_hid *shid = spi_get_drvdata(spi);
	struct device *dev = &spi->dev;
	int ret, len;

	len = spi_hid_report_descriptor_request(shid);
	if (len < 0) {
		dev_err(dev, "Report descriptor request failed, %d.", len);
		ret = len;
		goto out;
	}

	/*
	 * FIXME: below call returning 0 doesn't mean that the report descriptor
	 * is good. We might be caching a crc32 of a corrupted r. d. or who
	 * knows what the FW sent. Need to have a feedback loop about r. d.
	 * being ok and only then cache it.
	 */
	ret = hid_parse_report(hid, (u8 *)shid->response->content, len);
	if (ret)
		dev_err(dev, "failed parsing report: %d.", ret);
	else
		shid->report_descriptor_crc32 = crc32_le(0,
				(unsigned char const *)shid->response->content,
				len);

out:
	return ret;
}

static int spi_hid_ll_raw_request(struct hid_device *hid,
				  unsigned char reportnum, __u8 *buf,
				  size_t len, unsigned char rtype, int reqtype)
{
	struct spi_device *spi = hid->driver_data;
	struct spi_hid *shid = spi_get_drvdata(spi);
	struct device *dev = &spi->dev;
	int ret;

	switch (reqtype) {
	case HID_REQ_SET_REPORT:
		if (buf[0] != reportnum) {
			dev_err(dev, "report id mismatch.");
			ret = -EINVAL;
			break;
		}

		ret = spi_hid_set_request(shid, &buf[1], len - 1,
				reportnum);
		if (ret) {
			dev_err(dev, "failed to set report.");
			break;
		}

		ret = len;
		break;
	case HID_REQ_GET_REPORT:
		ret = spi_hid_get_request(shid, reportnum);
		if (ret) {
			dev_err(dev, "failed to get report.");
			break;
		}

		ret = min_t(size_t, len,
			(shid->response->body[1] | (shid->response->body[2] << 8)) + 1);
		buf[0] = shid->response->body[3];
		memcpy(&buf[1], &shid->response->content, ret);
		break;
	default:
		dev_err(dev, "invalid request type.");
		ret = -EIO;
	}

	return ret;
}

static int spi_hid_ll_output_report(struct hid_device *hid, __u8 *buf,
				    size_t len)
{
	int ret;
	struct spi_device *spi = hid->driver_data;
	struct spi_hid *shid = spi_get_drvdata(spi);
	struct device *dev = &spi->dev;
	struct spi_hid_output_report report = {
		.report_type = OUTPUT_REPORT,
		.content_length = len - 1,
		.content_id = buf[0],
		.content = &buf[1],
	};

	if (!shid->ready) {
		dev_err(dev, "%s called in unready state", __func__);
		ret = -ENODEV;
		goto out;
	}

	if (shid->desc.no_output_report_ack)
		ret = spi_hid_send_output_report(shid, &report);
	else
		ret = spi_hid_sync_request(shid, &report);

	if (ret)
		dev_err(dev, "failed to send output report.");

out:
	if (ret)
		return ret;

	return len;
}

static struct hid_ll_driver spi_hid_ll_driver = {
	.start = spi_hid_ll_start,
	.stop = spi_hid_ll_stop,
	.open = spi_hid_ll_open,
	.close = spi_hid_ll_close,
	.power = spi_hid_ll_power,
	.parse = spi_hid_ll_parse,
	.output_report = spi_hid_ll_output_report,
	.raw_request = spi_hid_ll_raw_request,
};

static ssize_t bus_error_count_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct spi_hid *shid = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d (%d)\n",
			shid->bus_error_count, shid->bus_last_error);
}
static DEVICE_ATTR_RO(bus_error_count);

static ssize_t regulator_error_count_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct spi_hid *shid = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d (%d)\n",
			shid->regulator_error_count,
			shid->regulator_last_error);
}
static DEVICE_ATTR_RO(regulator_error_count);

static ssize_t device_initiated_reset_count_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct spi_hid *shid = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", shid->dir_count);
}
static DEVICE_ATTR_RO(device_initiated_reset_count);

static struct attribute *spi_hid_attrs[] = {
	&dev_attr_bus_error_count.attr,
	&dev_attr_regulator_error_count.attr,
	&dev_attr_device_initiated_reset_count.attr,
	NULL	/* Terminator */
};
ATTRIBUTE_GROUPS(spi_hid);

int spi_hid_core_probe(struct spi_device *spi, struct spihid_ops *ops,
		       struct spi_hid_conf *conf)
{
	struct device *dev = &spi->dev;
	struct spi_hid *shid;
	unsigned long irqflags;
	int error;

	if (spi->irq <= 0) {
		dev_err(dev, "Missing IRQ.");
		error = spi->irq ?: -EINVAL;
		goto err;
	}

	shid = devm_kzalloc(dev, sizeof(*shid), GFP_KERNEL);
	if (!shid) {
		error = -ENOMEM;
		goto err;
	}

	shid->spi = spi;
	shid->power_state = HIDSPI_ON;
	shid->ops = ops;
	shid->conf = conf;
	shid->reset_pending = true;

	spi_set_drvdata(spi, shid);

	/* Assign attribute groups */
	error = devm_device_add_groups(dev, spi_hid_groups);
	if (error) {
		dev_err(dev, "Failed to add attribute groups: %d", error);
		goto err;
	}

	/* Using now populated conf let's pre-calculate the read approvals */
	spi_hid_populate_read_approvals(shid->conf, shid->read_approval_header,
					shid->read_approval_body);

	mutex_init(&shid->output_lock);
	mutex_init(&shid->power_lock);
	mutex_init(&shid->data_lock);
	init_completion(&shid->output_done);
	//init_completion(&shid->ready_done);

	INIT_WORK(&shid->reset_response_work, spi_hid_reset_response_work);
	INIT_WORK(&shid->create_device_work, spi_hid_create_device_work);
	INIT_WORK(&shid->refresh_device_work, spi_hid_refresh_device_work);
	INIT_WORK(&shid->error_work, spi_hid_error_work);

	/* we need to allocate the buffer without knowing the maximum
	 * size of the reports. Let's use SZ_2K, then we do the
	 * real computation later.
	 */
	error = spi_hid_alloc_buffers(shid, SZ_2K);
	if (error < 0)
		goto err;

	/*
	 * At the end of probe we initialize the device:
	 *   0) Default pinctrl in DT: assert reset, bias the interrupt line
	 *   1) sleep minimal reset delay
	 *   2) request IRQ
	 *   3) power up the device
	 *   4) deassert reset (high)
	 * After this we expect an IRQ with a reset response.
	 */

	shid->ops->assert_reset(shid->ops);

	shid->ops->sleep_minimal_reset_delay(shid->ops);

	irqflags = irq_get_trigger_type(spi->irq) | IRQF_ONESHOT;
	error = devm_request_threaded_irq(dev, spi->irq, NULL, spi_hid_dev_irq,
			irqflags, dev_name(&spi->dev), shid);
	if (error) {
		dev_err(dev, "%s: unable to request threaded IRQ.", __func__);
		goto err_free_buffers;
	}
	shid->irq_enabled = true;

	error = shid->ops->power_up(shid->ops);
	if (error) {
		dev_err(dev, "%s: could not power up.", __func__);
		shid->regulator_error_count++;
		shid->regulator_last_error = error;
		goto err_free_buffers;
	}

	shid->ops->deassert_reset(shid->ops);

	dev_dbg(dev, "%s: d3 -> %s.", __func__,
		spi_hid_power_mode_string(shid->power_state));

	return 0;

err_free_buffers:
	spi_hid_free_buffers(shid);
err:
	return error;
}
EXPORT_SYMBOL_GPL(spi_hid_core_probe);

void spi_hid_core_remove(struct spi_device *spi)
{
	struct spi_hid *shid = spi_get_drvdata(spi);
	struct device *dev = &spi->dev;

	int error;

	spi_hid_stop_hid(shid);

	shid->ops->assert_reset(shid->ops);
	error = shid->ops->power_down(shid->ops);
	if (error) {
		dev_err(dev, "failed to disable regulator.");
		shid->regulator_error_count++;
		shid->regulator_last_error = error;
	}

	if (shid->bufsize)
		spi_hid_free_buffers(shid);
}
EXPORT_SYMBOL_GPL(spi_hid_core_remove);

MODULE_DESCRIPTION("HID over SPI transport driver");
MODULE_AUTHOR("Dmitry Antipov <dmanti@microsoft.com>");
MODULE_LICENSE("GPL");
