// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2022 Google LLC.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

#include "dprx.h"

#define DPRX_RX_CONTROL   0x000
#define DPRX_RX_STATUS    0x001
#define DPRX0_VBID        0x02f
#define DPRX_MST_CONTROL1 0x0a0
#define DPRX_MST_STATUS1  0x0a1
#define DPRX_MST_VCPTAB0  0x0a2
#define DPRX_AUX_CONTROL  0x100
#define DPRX_AUX_STATUS   0x101
#define DPRX_AUX_COMMAND  0x102
#define DPRX_AUX_HPD      0x119

static void dp_wr(struct dprx_dp *dp, int addr, u32 val)
{
	writel(val, dp->iobase + (addr * 4));
}

static u32 dp_rd(struct dprx_dp *dp, int addr)
{
	return readl(dp->iobase + (addr * 4));
}

/* HPD */

void dprx_dprx_set_hpd(struct dprx_dp *dp, int val)
{
	u32 reg;

	reg = dp_rd(dp, DPRX_AUX_HPD);
	reg &= ~(1 << 11);
	reg |= (val & 1) << 11;
	dp_wr(dp, DPRX_AUX_HPD, reg);
}

int dprx_dprx_get_hpd(struct dprx_dp *dp)
{
       return (dp_rd(dp, DPRX_AUX_HPD) >> 11) & 1;
}

void dprx_dprx_pulse_hpd(struct dprx_dp *dp)
{
	u32 reg;

	reg = dp_rd(dp, DPRX_AUX_HPD);
	reg |= 1 << 12;
	dp_wr(dp, DPRX_AUX_HPD, reg);
}

/* Receiver Control */

void dprx_dprx_set_link_rate(struct dprx_dp *dp, int val)
{
	u32 reg;

	reg = dp_rd(dp, DPRX_RX_CONTROL);
	reg &= ~(0xff << 16);
	reg |= (val & 0xff) << 16;
	reg |= 1 << 13;
	dp_wr(dp, DPRX_RX_CONTROL, reg);
}

void dprx_dprx_set_lane_count(struct dprx_dp *dp, int val)
{
	u32 reg;

	reg = dp_rd(dp, DPRX_RX_CONTROL);
	reg &= ~0x1f;
	reg |= (val & 0x1f);
	dp_wr(dp, DPRX_RX_CONTROL, reg);
}

void dprx_dprx_set_training_pattern(struct dprx_dp *dp, int val)
{
	u32 reg;

	reg = dp_rd(dp, DPRX_RX_CONTROL);
	reg &= ~(0x7 << 8);
	reg |= (val & 0x7) << 8;
	dp_wr(dp, DPRX_RX_CONTROL, reg);
}

void dprx_dprx_set_scrambler(struct dprx_dp *dp, int val)
{
	u32 reg;

	reg = dp_rd(dp, DPRX_RX_CONTROL);
	reg &= ~(1 << 7);
	reg |= (~val & 1) << 7;
	dp_wr(dp, DPRX_RX_CONTROL, reg);
}

/* Receiver Status */

int dprx_dprx_get_cr_lock(struct dprx_dp *dp)
{
	return dp_rd(dp, DPRX_RX_STATUS) & 0xf;
}

int dprx_dprx_get_sym_lock(struct dprx_dp *dp)
{
	return (dp_rd(dp, DPRX_RX_STATUS) >> 4) & 0xf;
}

int dprx_dprx_get_interlane_align(struct dprx_dp *dp)
{
	return (dp_rd(dp, DPRX_RX_STATUS) >> 8) & 0x1;
}

int dprx_dprx_get_sink_status(struct dprx_dp *dp)
{
	return (dp_rd(dp, DPRX0_VBID) >> 7) & 0x1;
}

int dprx_dprx_get_rx_busy(struct dprx_dp *dp)
{
	return (dp_rd(dp, DPRX_RX_STATUS) >> 17) & 0x1;
}

/* MST */

void dprx_dprx_set_mst(struct dprx_dp *dp, int val)
{
	u32 reg;

	reg = dp_rd(dp, DPRX_MST_CONTROL1);
	reg &= ~0x1;
	reg |= (val & 0x1);
	dp_wr(dp, DPRX_MST_CONTROL1, reg);
}

void dprx_dprx_clear_vc_payload_table(struct dprx_dp *dp)
{
	u32 reg;
	int i;

	for (i = 0; i < 8; i++)
		dp_wr(dp, DPRX_MST_VCPTAB0 + i, 0);

	reg = dp_rd(dp, DPRX_MST_CONTROL1);
	reg &= ~(0xffff << 4);
	reg |= 1 << 31;
	dp_wr(dp, DPRX_MST_CONTROL1, reg);
}

void dprx_dprx_set_vc_payload_table(struct dprx_dp *dp, u8 *table, u8 *id)
{
	u8 map[64];
	int i, j;
	u32 reg;

	memset(map, 0, 64);
	for (i = 0; i < 4; i++) {
		if (id[i] != 0 && id[i] < 64)
			map[id[i]] = i + 1;
	}

	for (i = 0; i < 8; i++) {
		reg = 0;
		for (j = 0; j < 8; j++)
			reg |= map[table[i*8+j]] << (j * 4);
		dp_wr(dp, DPRX_MST_VCPTAB0 + i, reg);
	}

	reg = dp_rd(dp, DPRX_MST_CONTROL1);
	reg &= ~(0xffff << 4);
	for (i = 0; i < 4; i++)
		if (id[i] != 0 && id[i] < 64)
			reg |= (i + 1) << ((i + 1) * 4);
	reg |= 1 << 30;
	dp_wr(dp, DPRX_MST_CONTROL1, reg);
}

int dprx_dprx_get_act(struct dprx_dp *dp)
{
	return (dp_rd(dp, DPRX_MST_STATUS1) >> 30) & 1;
}

void dprx_dprx_clear_act(struct dprx_dp *dp)
{
	u32 reg;

	reg = dp_rd(dp, DPRX_MST_CONTROL1);
	reg &= ~(1 << 30);
	dp_wr(dp, DPRX_MST_CONTROL1, reg);
}

/* AUX CH */

int dprx_dprx_read_aux(struct dprx_dp *dp, u8 *data)
{
	int length;
	u32 reg;
	int i;

	/* check MSG_READY */
	reg = dp_rd(dp, DPRX_AUX_STATUS);
	if (!(reg & (1 << 31)))
		return 0;

	/* read LENGTH */
	length = dp_rd(dp, DPRX_AUX_CONTROL) & 0x1f;
	if (length > 20)
		length = 20;

	/* read request */
	for (i = 0; i < length; i++)
		data[i] = dp_rd(dp, DPRX_AUX_COMMAND + i);

	return length;
}

void dprx_dprx_write_aux(struct dprx_dp *dp, u8 *data, int length)
{
	u32 reg;
	int i;

	/* check READY_TO_TX */
	reg = dp_rd(dp, DPRX_AUX_STATUS);
	if (!(reg & (1 << 30)))
		return;

	/* write request */
	if (length > 17)
		length = 17;
	for (i = 0; i < length; i++)
		dp_wr(dp, DPRX_AUX_COMMAND + i, data[i]);

	/* write LENGTH and TX_STROBE */
	reg = dp_rd(dp, DPRX_AUX_CONTROL);
	reg &= ~0x1f;
	reg |= length | (1 << 7);
	dp_wr(dp, DPRX_AUX_CONTROL, reg);
}

/* Misc */

void dprx_dprx_init(struct dprx_dp *dp)
{
	u32 reg;

	/* Enable AUX_IRQ_EN */
	reg = dp_rd(dp, DPRX_AUX_CONTROL);
	reg |= 1 << 8;
	dp_wr(dp, DPRX_AUX_CONTROL, reg);

	/* Set CHANNEL_CODING_SET to 8b/10b */
	reg = dp_rd(dp, DPRX_RX_CONTROL);
	reg |= 1 << 5;
	dp_wr(dp, DPRX_RX_CONTROL, reg);
}
