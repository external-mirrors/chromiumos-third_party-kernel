/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 Microsoft Corporation
 */


#ifndef SPI_HID_CORE_H
#define SPI_HID_CORE_H

#include <linux/completion.h>
#include <linux/hid-over-spi.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <drm/drm_panel.h>

/* Protocol message size constants */
#define SPI_HID_READ_APPROVAL_LEN		5
#define SPI_HID_OUTPUT_HEADER_LEN		8

/* Raw input buffer with data from the bus */
struct spi_hid_input_buf {
	u8 header[HIDSPI_INPUT_HEADER_SIZE];
	u8 body[HIDSPI_INPUT_BODY_HEADER_SIZE];
	u8 content[];
};

/* Processed data from input report header */
struct spi_hid_input_header {
	u8 version;
	u16 report_length;
	u8 last_fragment_flag;
	u8 sync_const;
};

/* Processed data from an input report */
struct spi_hid_input_report {
	u8 report_type;
	u16 content_length;
	u8 content_id;
	u8 *content;
};

/* Raw output report buffer to be put on the bus */
struct spi_hid_output_buf {
	u8 header[SPI_HID_OUTPUT_HEADER_LEN];
	u8 content[];
};

/* Data necessary to send an output report */
struct spi_hid_output_report {
	u8 report_type;
	u16 content_length;
	u8 content_id;
	u8 *content;
};

/* Processed data from a device descriptor */
struct spi_hid_device_descriptor {
	u16 hid_version;
	u16 report_descriptor_length;
	u16 max_input_length;
	u16 max_output_length;
	u16 max_fragment_length;
	u16 vendor_id;
	u16 product_id;
	u16 version_id;
	u8 no_output_report_ack;
};

/* struct spi_hid_conf - Conf provided to the core */
struct spi_hid_conf {
	u32 input_report_header_address;
	u32 input_report_body_address;
	u32 output_report_address;
	u8 read_opcode;
	u8 write_opcode;
};

/* struct spihid_ops - Ops provided to the core
 *
 * @power_up: do sequencing to power up the device
 * @power_down: do sequencing to power down the device
 * @assert_reset: do sequencing to assert the reset line
 * @deassert_reset: do sequencing to deassert the reset line
 */
struct spihid_ops {
	int (*power_up)(struct spihid_ops *ops);
	int (*power_down)(struct spihid_ops *ops);
	int (*assert_reset)(struct spihid_ops *ops);
	int (*deassert_reset)(struct spihid_ops *ops);
	void (*sleep_minimal_reset_delay)(struct spihid_ops *ops);
	int (*plat_init)(struct spihid_ops *ops);

	u32 response_timeout_ms;	/* Output report response timeout in ms. */
};

/* Driver context */
struct spi_hid {
	struct spi_device	*spi;	/* pointer to spi device. */
	struct hid_device	*hid;	/* pointer to corresponding HID dev. */

	struct spi_transfer	input_transfer[2];	/* Transfer buffer for read and write. */
	struct spi_message	input_message;	/* used to execute a sequence of spi transfers. */

	struct spihid_ops	*ops;
	struct spi_hid_conf	*conf;
	struct spi_hid_device_descriptor desc;	/* HID device descriptor. */
	struct spi_hid_output_buf *output;	/* Output buffer. */
	struct spi_hid_input_buf *input;	/* Input buffer. */
	struct spi_hid_input_buf *response;	/* Response buffer. */

	struct drm_panel_follower panel_follower;
	bool	is_panel_follower;
	bool	panel_follower_work_finished;

	u16 response_length;
	u16 bufsize;

	unsigned long quirks;	/* Various quirks. */

	enum hidspi_power_state power_state;

	u8 reset_attempts;	/* The number of reset attempts. */

	/*
	 * ready flag indicates that the FW is ready to accept commands and
	 * requests. The FW becomes ready after sending the report descriptor.
	 */
	bool ready;
	/*
	 * refresh_in_progress is set to true while the refresh_device worker
	 * thread is destroying and recreating the hidraw device. When this flag
	 * is set to true, the ll_close and ll_open functions will not cause
	 * power state changes.
	 */
	bool refresh_in_progress;
	/*
	 * reset_pending indicates that the device is being reset. When this flag
	 * is set to true, garbage interrupts triggered during reset will be
	 * dropped and will not cause error handling.
	 */
	bool reset_pending;

	bool prev_mode_active;	/* Previous device mode state for transition tracking */

	bool irq_enabled;

	struct work_struct reset_response_work;
	struct work_struct create_device_work;
	struct work_struct refresh_device_work;
	struct work_struct error_work;
	struct work_struct panel_follower_work;

	/* Control lock to make sure one output transaction at a time. */
	struct mutex output_lock;
	/* Power lock to make sure one power state change at a time. */
	struct mutex power_lock;
	/* Data lock to prevent race conditions of the writable device attribute */
	struct mutex data_lock;
	struct completion output_done;

	u8 read_approval_header[SPI_HID_READ_APPROVAL_LEN];
	u8 read_approval_body[SPI_HID_READ_APPROVAL_LEN];

	u32 report_descriptor_crc32;	/* HID report descriptor. */

	u32 regulator_error_count;
	int regulator_last_error;
	u32 bus_error_count;
	int bus_last_error;
	u32 dir_count;
	bool device_init_status;
};

int spi_hid_core_probe(struct spi_device *spi, struct spihid_ops *ops,
		       struct spi_hid_conf *conf);
void spi_hid_core_remove(struct spi_device *spi);

extern const struct dev_pm_ops spi_hid_core_pm;

#endif
