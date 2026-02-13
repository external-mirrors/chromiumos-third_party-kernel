// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ILITEK Touch IC driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "spi-hid-core.h"
#include "spi-hid-of-ilitek.h"

/* Debug level */
bool debug_en = DEBUG_OUTPUT;

unsigned char CTPM_FW_DEF[] = {
	0xFF,
};

struct ilitek_ts_hid_data *ilits;
struct touch_fw_data tfd;
struct flash_block_info fbi[FW_BLOCK_INFO_NUM];
struct ilitek_ic_info chip;

static u8 *pfw;
static u8 *CTPM_FW;

/* ic.c */
static struct ilitek_protocol_info protocol_info[PROTOCL_VER_NUM] = {
	/* length -> fw, protocol, tp, key, panel, core, func, window, cdc */
	[0] = {PROTOCOL_VER_500, 4, 4, 14, 30, 5, 5, 2, 8, 3},
	[1] = {PROTOCOL_VER_510, 4, 3, 14, 30, 5, 5, 3, 8, 3},
	[2] = {PROTOCOL_VER_520, 4, 4, 14, 30, 5, 5, 3, 8, 3},
	[3] = {PROTOCOL_VER_530, 9, 4, 14, 30, 5, 5, 3, 8, 3},
	[4] = {PROTOCOL_VER_540, 9, 4, 14, 30, 5, 5, 3, 8, 15},
	[5] = {PROTOCOL_VER_550, 9, 4, 14, 30, 5, 5, 3, 8, 15},
	[6] = {PROTOCOL_VER_560, 9, 4, 14, 30, 5, 5, 3, 8, 15},
	[7] = {PROTOCOL_VER_570, 9, 4, 14, 30, 5, 5, 3, 8, 15},
};

struct ilitek_ic_func_ctrl func_ctrl[FUNC_CTRL_NUM] = {
	/* cmd[3] = cmd, func, ctrl */
	/* rec_state 0:disable, 1: enable, 2: ignore record */
	[0] = {"sense", {0x1, 0x1, 0x0}, 3, 0x0, 2, 0xFF},
	[1] = {"sleep", {0x1, 0x2, 0x0}, 3, 0x0, 2, 0xFF},
	[2] = {"glove", {0x1, 0x6, 0x0}, 3, 0x0, 0, 0xFF},
	[3] = {"stylus", {0x1, 0x7, 0x0}, 3, 0x0, 0, 0xFF},
	[4] = {"lpwg", {0x1, 0xA, 0x0}, 3, 0x0, 2, 0xFF},
	[5] = {"plug", {0x1, 0x11, 0x0}, 3, 0x1, 0, 0xFF},
	[6] = { "lock_point", { 0x1, 0x13, 0x0 }, 3, 0x0, 0, 0xFF },
	[7] = {"game_mode", {0x1, 0x13, 0x0}, 3, 0x1, 0, 0xFF},
	[8] = {"active", {0x1, 0x14, 0x0}, 3, 0x0, 2, 0xFF},
	[9] = {"idle", {0x1, 0x19, 0x0}, 3, 0x1, 0, 0xFF},
	[10] = {"gesture_demo_en", {0x1, 0x16, 0x0}, 3, 0x0, 0, 0xFF},
	[11] = {"tp_recore", {0x1, 0x18, 0x0}, 3, 0x0, 2, 0xFF},
	[12] = {"knock_en", {0x1, 0xA, 0x8, 0x03, 0x0, 0x0}, 6, 0xFF, 0, 0xFF},
	[13] = {"int_trigger", {0x1, 0x1B, 0x0}, 3, 0x0, 2, 0xFF},
	[14] = {"ear_phone", {0x1, 0x17, 0x0}, 3, 0x0, 0, 0xFF},
	[15] = {"knuckle", {0x1, 0xF, 0x0}, 3, 0x0, 0, 0xFF},
	[16] = {"nfc", {0x1, 0x3, 0x0}, 3, 0x0, 0, 0xFF},
};

static int ilitek_tddi_ic_check_info(u32 pid, u32 id)
{
	ILI_INFO("ILITEK CHIP found.\n");

	if (((pid & 0xFFFFFF00) == ILI9881N_AA) ||
	    ((pid & 0xFFFFFF00) == ILI9881O_AA))
		ilits->chip->dma_reset = ENABLE;
	else
		ilits->chip->dma_reset = DISABLE;

	ilits->chip->no_bk_shift = RAWDATA_NO_BK_SHIFT;
	ilits->chip->max_count = 0x2FFFF;
	ilits->chip->pid = pid;
	ilits->chip->reset_key = TDDI_WHOLE_CHIP_RST_WITH_FLASH_KEY;
	ilits->chip->wtd_key = 0x9881;

	return 0;
}

int ili_ice_mode_bit_mask_write(u32 addr, u32 mask, u32 value)
{
	int ret = 0;
	u32 data = 0;

	if (ili_ice_mode_read(addr, &data, sizeof(u32)) < 0) {
		ILI_ERR("Read data error\n");
		return -1;
	}

	data &= (~mask);
	data |= (value & mask);

	ILI_DBG("mask value data = %x\n", data);

	ret = ili_ice_mode_write(addr, data, sizeof(u32));
	if (ret < 0)
		ILI_ERR("Failed to re-write data in ICE mode, ret = %d\n", ret);

	return ret;
}

int ili_ice_mode_bit_mask_write_cascade(u32 addr, u32 mask, u32 value, int mode)
{
	int ret = 0;
	u32 data = 0;

	if (ili_ice_mode_read_by_mode(addr, &data, sizeof(u32), MASTER) < 0) {
		ILI_ERR("Read data error\n");
		return -1;
	}

	data &= (~mask);
	data |= (value & mask);

	ILI_DBG("mask value data = %x\n", data);

	ret = ili_ice_mode_write_by_mode(addr, data, sizeof(u32), mode);
	if (ret < 0)
		ILI_ERR("Failed to re-write data in ICE mode, ret = %d\n", ret);

	return ret;
}

int ili_ice_mode_write(u32 addr, u32 data, int len)
{
	int ret = 0, i = 0;
	u8 txbuf[64] = { 0 };

	if (!atomic_read(&ilits->ice_stat)) {
		ILI_ERR("ice mode not enabled\n");
		return -1;
	}

	txbuf[0] = 0x25;
	txbuf[1] = (char)((addr & 0x000000FF) >> 0);
	txbuf[2] = (char)((addr & 0x0000FF00) >> 8);
	txbuf[3] = (char)((addr & 0x00FF0000) >> 16);

	for (i = 0; i < len; i++)
		txbuf[i + 4] = (char)(data >> (8 * i));

	ret = ilits->wrapper(txbuf, len + 4, NULL, 0, OFF, OFF);
	if (ret < 0)
		ILI_ERR("Failed to write data in ice mode, ret = %d\n", ret);

	return ret;
}

int ili_ice_mode_read(u32 addr, u32 *data, int len)
{
	int ret = 0;
	u8 rxbuf[4] = { 0 };
	u8 txbuf[4] = { 0 };

	if (len > sizeof(u32)) {
		ILI_ERR("ice mode read length = %d, must less than or equal to 4 bytes\n",
			len);
		len = 4;
	}

	if (!atomic_read(&ilits->ice_stat)) {
		ILI_ERR("ice mode not enabled\n");
		return -1;
	}

	txbuf[0] = 0x25;
	txbuf[1] = (char)((addr & 0x000000FF) >> 0);
	txbuf[2] = (char)((addr & 0x0000FF00) >> 8);
	txbuf[3] = (char)((addr & 0x00FF0000) >> 16);

	ret = ilits->wrapper(txbuf, sizeof(txbuf), NULL, 0, OFF, OFF);
	if (ret < 0)
		goto out;

	ret = ilits->wrapper(NULL, 0, rxbuf, len, OFF, OFF);
	if (ret < 0)
		goto out;

	*data = 0;
	if (len == 1)
		*data = rxbuf[0];
	else if (len == 2)
		*data = (rxbuf[0] | rxbuf[1] << 8);
	else if (len == 3)
		*data = (rxbuf[0] | rxbuf[1] << 8 | rxbuf[2] << 16);
	else
		*data = (rxbuf[0] | rxbuf[1] << 8 | rxbuf[2] << 16 |
			 rxbuf[3] << 24);

out:
	if (ret < 0)
		ILI_ERR("Failed to read data in ice mode, ret = %d\n", ret);

	return ret;
}

void ili_cascade_sync_ctrl(bool mode)
{
	bool stop = false;
	u8 cmd[2] = { 0 };

	if (ilits->skip_sync_cmd) {
		ILI_INFO("skip cascade sync, sync_mode = %d\n", mode);
		return;
	}

	if (mode == stop) {
		cmd[0] = 0x10;
		cmd[1] = 0x10;
		if (ilits->wrapper(cmd, sizeof(cmd), NULL, 0, OFF, OFF) < 0) {
			ILI_ERR("Failed to stop Sync Cascade\n");
		} else {
			atomic_set(&ilits->stop_sync_stat, ENABLE);
			ILI_DBG("Stop Sync Cascade, Set atomic sync stat = %d\n",
				atomic_read(&ilits->stop_sync_stat));
		}

		mdelay(1);
	} else {
		cmd[0] = 0x10;
		cmd[1] = 0x11;
		if (ilits->wrapper(cmd, sizeof(cmd), NULL, 0, OFF, OFF) < 0) {
			ILI_ERR("Failed to start Sync Cascade\n");
		} else {
			atomic_set(&ilits->stop_sync_stat, DISABLE);
			ILI_DBG("Start Sync Cascade, Set atomic sync stat = %d\n",
				atomic_read(&ilits->stop_sync_stat));
		}
	}
}

int ili_ice_mode_ctrl(bool enable, bool mcu)
{
	int ret = 0;
	u8 cmd_open[4] = {0x25, 0x62, 0x10, 0x18};
	u8 cmd_close[4] = {0x1B, 0x62, 0x10, 0x18};

	if (enable) {
		if (atomic_read(&ilits->ice_stat)) {
			ILI_INFO("ice mode already enabled\n");
			return 0;
		}

		/* Cascade Stop Sync */
#if TDDI_INTERFACE == BUS_SPI
#if ENABLE_CASCADE
		if (atomic_read(&ilits->stop_sync_stat) == DISABLE)
			ili_cascade_sync_ctrl(false);
#endif
#endif

		if (mcu)
			cmd_open[0] = ICE_MCU_ON_HEADER;
		if (ilits->change_ice_key)
			cmd_open[0] = ICE_FAK_HEADER;
		atomic_set(&ilits->ice_stat, ENABLE);

		if (ilits->wrapper(cmd_open, sizeof(cmd_open), NULL, 0, OFF, OFF) < 0) {
			ILI_ERR("write ice mode cmd error\n");
			atomic_set(&ilits->ice_stat, DISABLE);
		}
		ilits->pll_clk_wakeup = false;
	} else {
		if (!atomic_read(&ilits->ice_stat)) {
			ILI_INFO("ice mode already disabled\n");
			return 0;
		}
		ret = ilits->wrapper(cmd_close, sizeof(cmd_close), NULL, 0, OFF, OFF);
		if (ret < 0) {
			ILI_ERR("Exit to ICE Mode failed !!\n");
			atomic_set(&ilits->ice_stat, ENABLE);
		} else {
			atomic_set(&ilits->ice_stat, DISABLE);
#if (!ENABLE_CASCADE)
			ilits->pll_clk_wakeup = true;
#endif
			atomic_set(&ilits->ignore_report, END);
		}

		/* Cascade Start Sync */
#if ENABLE_CASCADE
		if (atomic_read(&ilits->stop_sync_stat) == ENABLE) {
			ilits->pll_clk_wakeup = true;
			ili_cascade_sync_ctrl(true);
		}
#endif
	}
	ILI_INFO("%s ICE mode, mcu on = %d\n", (enable ? "Enable" : "Disable"), mcu);

	return ret;
}

int ili_ice_mode_ctrl_by_mode_spi(bool enable, bool mcu, int mode)
{
	int ret = 0;

	if (ilits->cascade_info_block.nNum != 0) {
		if (mode == BOTH && enable == ENABLE) {
			ret = ili_set_bypass_mode(mcu);
		} else {
			ret = ili_ice_mode_ctrl(enable, mcu);
			if (ret < 0)
				ILI_ERR("Failed to write ice mode\n");
		}
	} else {
		ret = ili_ice_mode_ctrl(enable, mcu);
		if (ret < 0)
			ILI_ERR("Failed to write ice mode\n");
	}

	return ret;
}

int ili_ice_mode_write_by_mode(u32 addr, u32 data, int len, int mode)
{
	int ret = 1;
#if (ENABLE_SPICASCADE_V2)
	switch (mode) {
	case CLIENT:
		ili_ice_client_write_register(addr, data, len);
		break;
	case BOTH:
		ili_ice_both_write_register(addr, data, len);
		break;
	case MASTER:
		ili_ice_master_write_register(addr, data, len);
		break;
	default:
		break;
	}
#else
	if (ili_ice_mode_write(addr, data, len) < 0) {
		ILI_ERR("Failed to write addr: 0x%X\n", addr);
		ret = -1;
	}
#endif
	return ret;
}

int ili_ice_mode_read_by_mode(u32 addr, u32 *data, int len, int mode)
{
	int ret = 1;

	switch (mode) {
	case CLIENT:
		if (ili_spi_ice_mode_read(addr, data, len, CLIENT) < 0) {
			ILI_ERR("Failed to read Client, addr: 0x%x\n", addr);
			ret = -1;
		} else {
			ILI_DBG("Client [READ]:addr = 0x%06x, read = 0x%08x\n", addr, *data);
		}
		break;
	case BOTH:
		if (ili_spi_ice_mode_read(addr, data, len, CLIENT) < 0) {
			ILI_ERR("Failed to read Client, addr: 0x%x\n", addr);
			ret = -1;
		} else {
			ILI_DBG("Client [READ]:addr = 0x%06x, read = 0x%08x\n", addr, *data);
		}
		if (ili_spi_ice_mode_read(addr, data, len, MASTER) < 0) {
			ILI_ERR("Failed to read Master, addr: 0x%x\n", addr);
			ret = -1;
		} else {
			ILI_DBG("Master [READ]:addr = 0x%06x, read = 0x%08x\n", addr, *data);
		}
		break;
	case MASTER:
		if (ili_spi_ice_mode_read(addr, data, len, MASTER) < 0) {
			ILI_ERR("Failed to read Master, addr: 0x%x\n", addr);
			ret = -1;
		} else {
			ILI_DBG("Master [READ]:addr = 0x%06x, read = 0x%08x\n", addr, *data);
		}
		break;
	default:
		break;
	}
	return ret;
}

#if ENABLE_SPICASCADE_V2
int ili_ice_client_write_register(u32 addr, u32 data, int len)
{
	int ret = 0;
	bool ice = atomic_read(&ilits->ice_stat);
	bool mcu = atomic_read(&ilits->spi_slave_write_mcu_on);
	int ice_header_addr = 0, ice_head = 0;

	if (mcu) {
		/* (ICE_HEADER_REG + 0x03) is nonstop enter ice mode setting*/
		ice_header_addr = ICE_HEADER_REG + 0x03;
		ice_head = ICE_MCU_ON_HEADER;
	} else {
		/* (ICE_HEADER_REG) is stop enter ice mode setting*/
		ice_header_addr = ICE_HEADER_REG;
		ice_head = ICE_HEADER;
	}

	ILI_INFO("[CLIENT write]:addr = 0x%06x, write = 0x%08x, len = %d byte\n", addr, data, len);

	if (!ice)
		ilits->ice_mode_ctrl(ENABLE, mcu, BOTH);

	if (ili_ice_mode_write(MSPI_REG, 0x1, 1) < 0)
		ILI_ERR("Write MSPI_REG failed\n");

	if (ili_ice_mode_write(ice_header_addr, ICE_FAK_HEADER, 1) < 0)
		ILI_ERR("Write ICE_HEADER_REG failed\n");

	if (ili_ice_mode_write(MSPI_REG, 0x0, 1) < 0)
		ILI_ERR("Write MSPI_REG failed\n");

	ilits->skip_sync_cmd = ENABLE;
	ilits->ice_mode_ctrl(DISABLE, mcu, BOTH);

	ilits->ice_mode_ctrl(ENABLE, mcu, BOTH);
	ilits->skip_sync_cmd = DISABLE;

	ret = ili_ice_mode_write(addr, data, len);

	ilits->ice_mode_ctrl(DISABLE, mcu, BOTH);

	if (mcu)
		ilits->change_ice_key = true;

	ilits->ice_mode_ctrl(ENABLE, ON, BOTH);

	if (mcu)
		ilits->change_ice_key = false;

	if (ili_ice_mode_write(MSPI_REG, 0x1, 1) < 0)
		ILI_ERR("Write MSPI_REG failed\n");

	if (ili_ice_mode_write(ice_header_addr, ice_head, 1) < 0)
		ILI_ERR("Write ICE_HEADER_REG failed\n");

	if (ili_ice_mode_write(MSPI_REG, 0x0, 1) < 0)
		ILI_ERR("Write MSPI_REG failed\n");

	ilits->ice_mode_ctrl(DISABLE, mcu, BOTH);

	if (ice)
		ilits->ice_mode_ctrl(ENABLE, mcu, BOTH);

	return ret;
}

int ili_ice_both_write_register(u32 addr, u32 data, int len)
{
	int ret = 0;
	u32 mspi_val = 0;

	ILI_INFO("[BOTH write]:addr = 0x%06x, write = 0x%08x, len = %d byte\n", addr, data, len);

	/* reserve origin mspi reg */
	ili_ice_mode_read_by_mode(MSPI_REG, &mspi_val, sizeof(u8), MASTER);

	if ((mspi_val & MSPI_reg_bypass_off) == MSPI_reg_bypass_off) {
		/* set mspi 0 : bypass */
		if (ili_ice_mode_write(MSPI_REG, mspi_val & (~MSPI_reg_bypass_off), 1) < 0)
			ILI_ERR("Disable MSPI_REG failed\n");
	}

	/* write data */
	if (ili_ice_mode_write(addr, data, len) < 0) {
		ILI_ERR("Failed to write addr: 0x%X\n", addr);
		ret = -1;
	}

	if ((mspi_val & MSPI_reg_bypass_off) == MSPI_reg_bypass_off) {
		/* recover origin mspi reg */
		if (ili_ice_mode_write(MSPI_REG, mspi_val, 1) < 0)
			ILI_ERR("Recover MSPI_REG : %u, failed\n", mspi_val);
	}

	return ret;
}

int ili_ice_master_write_register(u32 addr, u32 data, int len)
{
	int ret = 0;
	u32 mspi_val = 0;

	ILI_INFO("[MASTER write]:addr = 0x%06x, write = 0x%08x, len = %d byte\n", addr, data, len);

	/* reserve origin mspi reg */
	ili_ice_mode_read_by_mode(MSPI_REG, &mspi_val, sizeof(u8), MASTER);

	if ((mspi_val & MSPI_reg_bypass_off) != MSPI_reg_bypass_off) {
		/* set mspi 1 : don't bypass */
		if (ili_ice_mode_write(MSPI_REG, mspi_val | MSPI_reg_bypass_off, 1) < 0)
			ILI_ERR("Enable MSPI_REG failed\n");
	}

	/* write data */
	if (ili_ice_mode_write(addr, data, len) < 0) {
		ILI_ERR("Failed to write addr: 0x%X\n", addr);
		ret = -1;
	}

	if ((mspi_val & MSPI_reg_bypass_off) != MSPI_reg_bypass_off) {
		/* recover origin mspi reg */
		if (ili_ice_mode_write(MSPI_REG, mspi_val, 1) < 0)
			ILI_ERR("Recover MSPI_REG : %u, failed\n", mspi_val);
	}

	return ret;
}

int ili_spi_ms_register_read(u32 addr, u32 *data, int len, u8 msmode)
{
	int ret = 0;

	if (msmode == CLIENT) {
		if (ili_ice_mode_write(MS_MISO_SEL_REG, 0x0, 1) < 0) {
			ILI_ERR("Write miso sel to client failed\n");
			ret = -1;
		}
		ilits->spi_ms_mode = CLIENT;
	} else {
		if (ili_ice_mode_write(MS_MISO_SEL_REG, 0x1, 1) < 0) {
			ILI_ERR("Write miso sel to master failed\n");
			ret = -1;
		}
		ilits->spi_ms_mode = MASTER;
	}

	if (ili_ice_mode_read(addr, data, len) < 0)
		ret = -1;

	if (ilits->spi_ms_mode == CLIENT) {
		ilits->spi_ms_mode = MASTER;
		if (ili_ice_mode_write(MS_MISO_SEL_REG, 0x1, 1) < 0) {
			ILI_ERR("Write miso sel to master failed\n");
			ret = -1;
		}
	}

	ILI_DBG("read %s addr = 0x%08x, read = 0x%08x\n",
		(msmode == MASTER) ? "MASTER" : "CLIENT", (u32)addr, (u32) *data);

	return ret;
}

#endif

int ili_spi_ice_mode_read(u32 addr, u32 *data, int len, u8 msmode)
{
	int ret = 0;

#if ENABLE_SPICASCADE_V2
	ret = ili_spi_ms_register_read(addr, data, len, msmode);
#else
	if (msmode != CLIENT)
		ret = ili_ice_mode_read(addr, data, len);
#endif
	return ret;
}

int ili_set_bypass_mode(bool mcu)
{
	if (atomic_read(&ilits->ice_stat)) {
		ILI_INFO("ice mode already enabled\n");
		return 0;
	}

	if (ili_ice_mode_ctrl(ENABLE, mcu) < 0)
		ILI_ERR("Failed to write ice mode\n");

	/* set mspi 0 */
	if (ili_ice_mode_write_by_mode(MSPI_REG, 0x00, 1, MASTER) < 0)
		ILI_ERR("Failed to write MSPI_REG in ice mode\n");

	/* set s_to_q bit[1] = 1 */
	ili_ice_mode_bit_mask_write(SINGLE_TO_QUAL_REG, BIT(1), BIT(1));

	/* set client ic to ice mode */
	atomic_set(&ilits->ice_stat, DISABLE);

	if (ili_ice_mode_ctrl(ENABLE, mcu) < 0)
		ILI_ERR("Failed to write ice mode\n");

	ILI_INFO("Set ByPass Mode\n");

#if (ENABLE_SPICASCADE_V2)
	if (ili_ice_mode_bit_mask_write(CMD_MODE_EN_REG, BIT(1) | BIT(5), BIT(1) | BIT(5)) < 0)
		ILI_ERR("Failed to enable cmd mode\n");
#endif
	return 0;
}

int ili_ic_func_ctrl_export(const char *name, int ctrl)
{
	int ret;

	if (ERR_ALLOC_MEM(ilits)) {
		ILI_ERR("Failed to allocate ts memory, ilits is NULL.\n");
		return -ENOMEM;
	}

	ret = ili_ic_func_ctrl(name, ctrl);

	return ret;
}
EXPORT_SYMBOL(ili_ic_func_ctrl_export);

int ili_ic_func_ctrl(const char *name, int ctrl)
{
	int i = 0, ret = 0;

	for (i = 0; i < FUNC_CTRL_NUM; i++) {
		if (ipio_strcmp(name, func_ctrl[i].name) == 0) {
			if (strlen(name) != strlen(func_ctrl[i].name))
				continue;
			break;
		}
	}

	if (i >= FUNC_CTRL_NUM) {
		ILI_ERR("Not found function ctrl, %s\n", name);
		ret = -1;
		goto out;
	}

	if (ilits->protocol->ver == PROTOCOL_VER_500) {
		ILI_ERR("Non support function ctrl with protocol v5.0\n");
		ret = -1;
		goto out;
	}

	if (ilits->protocol->ver >= PROTOCOL_VER_560) {
		if (ipio_strcmp(func_ctrl[i].name, "gesture") == 0 ||
		    ipio_strcmp(func_ctrl[i].name, "phone_cover_window") == 0) {
			ILI_INFO("Non support %s function ctrl\n", func_ctrl[i].name);
			ret = -1;
			goto out;
		}
	}

	if (ilits->chip->core_ver >= CORE_VER_1700) {
		if (ipio_strcmp(func_ctrl[i].name, "sense") == 0) {
			ILI_INFO("Non support %s function ctrl after core ver %x\n",
				func_ctrl[i].name, ilits->chip->core_ver);
			ret = -1;
			goto out;
		}
	}

	func_ctrl[i].cmd[2] = ctrl;

	ILI_INFO("func = %s, len = %d, cmd = 0x%x, 0x%x, 0x%x\n",
		 func_ctrl[i].name, func_ctrl[i].len, func_ctrl[i].cmd[0],
		 func_ctrl[i].cmd[1], func_ctrl[i].cmd[2]);

	ret = ilits->wrapper(func_ctrl[i].cmd, func_ctrl[i].len, NULL, 0, OFF, OFF);
	if (ret < 0)
		ILI_ERR("Write TP function failed\n");

	if (func_ctrl[i].rec_state < 2) {
		if (ctrl == func_ctrl[i].def_cmd)
			func_ctrl[i].rec_state = DISABLE;
		else
			func_ctrl[i].rec_state = ENABLE;

		func_ctrl[i].rec_cmd = ctrl;
	}

	ILI_DBG("record %s func cmd %d, rec_state %d\n", func_ctrl[i].name,
		func_ctrl[i].rec_cmd, func_ctrl[i].rec_state);
out:
	return ret;
}

void ili_ic_func_ctrl_reset(void)
{
	int i = 0;

	for (i = 0; i < FUNC_CTRL_NUM; i++) {
		if (func_ctrl[i].rec_state == ENABLE) {
			ILI_DBG("reset func ctrl %s, record status = %d, cmd = %d\n",
				func_ctrl[i].name,
				func_ctrl[i].rec_state,
				func_ctrl[i].rec_cmd);

			if (ili_ic_func_ctrl(func_ctrl[i].name, func_ctrl[i].rec_cmd) < 0)
				ILI_ERR("reset ic func ctrl %s failed\n", func_ctrl[i].name);
		}
	}
}

int ili_ic_code_reset(bool mcu)
{
	int ret;
	bool ice = atomic_read(&ilits->ice_stat);

	if (!ice)
		ilits->ice_mode_ctrl(ENABLE, mcu, BOTH);

	ret = ili_ice_mode_write_by_mode(0x40040, 0xAE, 1, BOTH);
	if (ret < 0)
		ILI_ERR("ic code reset failed\n");

	if (!ice)
		ilits->ice_mode_ctrl(DISABLE, mcu, BOTH);

	return ret;
}

void ili_get_dma1_config(struct ilitek_dma_config *dma)
{
	/* dma1 src1 address */
	if (ili_ice_mode_read(0x072104, &dma->src_addr, 4) < 0)
		ILI_ERR("read dma1 src1 address failed\n");
	/* dma1 src1 format */
	if (ili_ice_mode_read(0x072108, &dma->src_fmt, 4) < 0)
		ILI_ERR("read dma1 src1 format failed\n");
	/* dma1 dest address */
	if (ili_ice_mode_read(0x072114, &dma->dest_addr, 4) < 0)
		ILI_ERR("read dma1 src1 format failed\n");
	/* dma1 dest format */
	if (ili_ice_mode_read(0x072118, &dma->dest_fmt, 4) < 0)
		ILI_ERR("read dma1 dest format failed\n");
	/* Block size */
	if (ili_ice_mode_read(0x07211C, &dma->block_size, 4) < 0)
		ILI_ERR("read block size (%d) failed\n", dma->block_size);
	ILI_DBG("src_addr=0x%x, src_fmt=0x%x, dest_addr=0x%x,dest_fmt=0x%x, block_size=0x%x\n",
		dma->src_addr, dma->src_fmt, dma->dest_addr, dma->dest_fmt, dma->block_size);
	/* DMA Control Switch */
	if (ilits->chip->id == ILI9882_CHIP && ilits->chip->type == ILI_V) {
		if (ili_ice_mode_read(0x0722B8, &dma->dmaControlSwitch, 1) < 0)
			ILI_ERR("read dma control switch (%d) failed\n", dma->dmaControlSwitch);
		ILI_DBG("dma.dma_control_switch=0x%x\n", dma->dmaControlSwitch);
		if (ili_ice_mode_write(0x0722B8, dma->dmaControlSwitch & 0xFD, 1) < 0)
			ILI_ERR("Write dma control switch (%d) failed\n",
				dma->dmaControlSwitch & 0xFD);
		ILI_DBG("dma.dma_control_switch=0x%x\n", dma->dmaControlSwitch & 0xFD);
	}
}

void ili_set_dma1_config(struct ilitek_dma_config *dma)
{
	ILI_DBG("src_addr=0x%x, src_fmt=0x%x, dest_addr=0x%x,dest_fmt=0x%x, block_size=0x%x\n",
		dma->src_addr, dma->src_fmt, dma->dest_addr, dma->dest_fmt, dma->block_size);
	/* dma1 src1 address */
	if (ili_ice_mode_write(0x072104, dma->src_addr, 4) < 0)
		ILI_ERR("Write dma1 src1 address failed\n");
	/* dma1 src1 format */
	if (ili_ice_mode_write(0x072108, dma->src_fmt, 4) < 0)
		ILI_ERR("Write dma1 src1 format failed\n");
	/* dma1 dest address */
	if (ili_ice_mode_write(0x072114, dma->dest_addr, 4) < 0)
		ILI_ERR("Write dma1 src1 format failed\n");
	/* dma1 dest format */
	if (ili_ice_mode_write(0x072118, dma->dest_fmt, 4) < 0)
		ILI_ERR("Write dma1 dest format failed\n");
	/* Block size*/
	if (ili_ice_mode_write(0x07211C, dma->block_size, 4) < 0)
		ILI_ERR("Write block size (%d) failed\n", dma->block_size);
	/* Disable CRC calc settings */
	if (ili_ice_mode_write(0x041014, 0x0, 4) < 0)
		ILI_ERR("Write dma CRC calc settings failed\n");
	/* Dma1 stop */
	if (ili_ice_mode_write(0x072100, 0x02040000, 4) < 0)
		ILI_ERR("Write dma1 stop failed\n");
	/* clr int */
	if (ili_ice_mode_write(0x048006, 0x2, 1) < 0)
		ILI_ERR("Write clr int failed\n");
	/* Dma1 start */
	if (ili_ice_mode_write(0x072100, 0x01040000, 4) < 0)
		ILI_ERR("Write dma1 start failed\n");
	/* DMA Control Switch */
	if (ilits->chip->id == ILI9882_CHIP && ilits->chip->type == ILI_V) {
		if (ili_ice_mode_write(0x0722B8, dma->dmaControlSwitch, 1) < 0)
			ILI_ERR("Write dma control switch (%d) failed\n", dma->dmaControlSwitch);
		ILI_DBG("dma.dma_control_switch=0x%x\n", dma->dmaControlSwitch);
	}
}

int ili_ic_whole_reset(bool mcu, bool withflash)
{
	int ret = 0, rst_edge_delay = 0;
	bool ice = atomic_read(&ilits->ice_stat);

	if (withflash)
		ilits->chip->reset_key = TDDI_WHOLE_CHIP_RST_WITH_FLASH_KEY;
	else
		ilits->chip->reset_key = TDDI_WHOLE_CHIP_RST_WITHOUT_FLASH_KEY;

	if (!ice)
		ilits->ice_mode_ctrl(ENABLE, mcu, BOTH);

	if (!withflash)
		ili_ice_mode_write_by_mode(
			ilits->chip->reset_addr,
			TDDI_WHOLE_CHIP_RST_WITHOUT_FLASH_PRE_KEY,
			sizeof(u32),
			BOTH);

	ILI_INFO("ic whole reset key = 0x%x, edge_delay = %d, withflash = %d\n",
		ilits->chip->reset_key, ilits->rst_edge_delay, withflash);

	ret = ili_ice_mode_write_by_mode(ilits->chip->reset_addr,
									ilits->chip->reset_key,
									sizeof(u32),
									BOTH);
	if (ret < 0) {
		ILI_ERR("ic whole reset failed\n");
		/*only fail need disable ice*/
		if (!ice)
			ilits->ice_mode_ctrl(DISABLE, mcu, BOTH);
	} else {
		/* Need accurate power sequence, do not change it to msleep */
		if (ilits->fast_enter_ice_mode) {
			rst_edge_delay = ilits->rst_edge_delay;
			ilits->rst_edge_delay = EDGE_DELAY_FOR_FAST_ENTER_ICE;
			ILI_INFO("fast_enter_ice_mode = %d, modified edge_delay = %d\n",
				ilits->fast_enter_ice_mode, ilits->rst_edge_delay);
		}
		mdelay(ilits->rst_edge_delay);
		if (ilits->fast_enter_ice_mode)
			ilits->rst_edge_delay = rst_edge_delay;
	}

	return ret;
}

int ili_ic_check_int_pulse(bool pulse)
{
	if (!wait_event_interruptible_timeout(
		    ilits->inq, !atomic_read(&ilits->cmd_int_check),
		    msecs_to_jiffies(ilits->wait_int_timeout))) {
		ILI_ERR("Error! INT pulse no detected. Timeout = %d ms\n",
			ilits->wait_int_timeout);
		atomic_set(&ilits->cmd_int_check, DISABLE);
		return -1;
	}

	ILI_DBG("INT pulse detected.\n");
	return 0;
}

int ili_ic_get_core_ver(void)
{
	int ret = 0;
	u8 buf[10] = { 0 };

	ilits->protocol->core_ver_len = P5_X_CORE_VER_FOUR_LENGTH;

	buf[1] = ilits->fw_info[68];
	buf[2] = ilits->fw_info[69];
	buf[3] = ilits->fw_info[70];
	buf[4] = ilits->fw_info[71];
	ILI_INFO("Core version = %d.%d.%d.%d\n", buf[1], buf[2], buf[3],
		 buf[4]);
	ilits->chip->core_ver =
		buf[1] << 24 | buf[2] << 16 | buf[3] << 8 | buf[4];

	return ret;
}

int ili_ic_get_fw_ver(void)
{
	int ret = 0;
	u8 buf[10] = { 0 };

	buf[1] = ilits->fw_info[48];
	buf[2] = ilits->fw_info[49];
	buf[3] = ilits->fw_info[50];
	buf[4] = ilits->fw_info[51];
	buf[5] = ilits->fw_mp_ver[0];
	buf[6] = ilits->fw_mp_ver[1];
	buf[7] = ilits->fw_mp_ver[2];
	buf[8] = ilits->fw_mp_ver[3];

	ILI_INFO("Firmware version = %d.%d.%d.%d\n", buf[1], buf[2], buf[3],
		 buf[4]);
	ILI_INFO("Firmware MP version = %d.%d.%d.%d\n", buf[5], buf[6], buf[7],
		 buf[8]);
	ilits->chip->fw_ver =
		buf[1] << 24 | buf[2] << 16 | buf[3] << 8 | buf[4];
	ilits->chip->fw_mp_ver =
		buf[5] << 24 | buf[6] << 16 | buf[7] << 8 | buf[8];

	return ret;
}

int ili_ic_get_panel_info(void)
{
	int ret = 0;
	u8 buf[10] = { 0 };

	buf[1] = ilits->fw_info[16];
	buf[2] = ilits->fw_info[17];
	buf[3] = ilits->fw_info[18];
	buf[4] = ilits->fw_info[19];
	ilits->panel_wid = buf[2] << 8 | buf[1];
	ilits->panel_hei = buf[4] << 8 | buf[3];
	ilits->trans_xy = (ilits->chip->core_ver >= CORE_VER_1430 &&
				(ilits->rib.nReportByPixel > 0)) ?
					ON :
					OFF;

	ILI_INFO("Panel info: width = %d, height = %d\n", ilits->panel_wid, ilits->panel_hei);
	ILI_INFO("Transfer touch coordinate = %s\n", ilits->trans_xy ? "ON" : "OFF");
	ILI_INFO("Customer Type = %X\n", ilits->rib.nCustomerType);
	ILI_INFO("Report Resolution Format Mode = %X\n", ilits->rib.nReportResolutionMode);

	if (ilits->chip->core_ver >= CORE_VER_1700) {
		ILI_INFO("Pen Type = %X\n", ilits->PenType);
		ILI_INFO("Pen: PxRaw=%d PyRaw=%d PxVa=%d PyVa=%d X_MP=%d ChipNum=%d SampNum=%d\n",
			ilits->pen_info_block.nPxRaw,
			ilits->pen_info_block.nPyRaw,
			ilits->pen_info_block.nPxVa,
			ilits->pen_info_block.nPyVa,
			ilits->pen_info_block.nPenX_MP,
			ilits->pen_info_block.nPenChipnum,
			ilits->pen_info_block.nPenSamplenum);
		ILI_INFO("Cascade Info Data, nDisable = %d, nNum = %d\n",
			ilits->cascade_info_block.nDisable, ilits->cascade_info_block.nNum);
	}

	return ret;
}

int ili_ic_get_tp_info(void)
{
	int ret = 0;
	u8 buf[20] = { 0 };

	buf[1] = ilits->fw_info[5];
	buf[2] = ilits->fw_info[7];
	buf[3] = ilits->fw_info[8];
	buf[4] = ilits->fw_info[9];
	buf[5] = ilits->fw_info[10];
	buf[6] = ilits->fw_info[11];
	buf[7] = ilits->fw_info[12];
	buf[8] = ilits->fw_info[14];
	if (ilits->chip->core_ver >= CORE_VER_1700) {
		buf[11] = ilits->fw_info[13];
		buf[12] = ilits->fw_info[15];
	} else {
		buf[11] = buf[7];
		buf[12] = buf[8];
	}

	ilits->min_x = buf[1];
	ilits->min_y = buf[2];
	ilits->max_x = buf[4] << 8 | buf[3];
	ilits->max_y = buf[6] << 8 | buf[5];
	ilits->xch_num = buf[7];
	ilits->ych_num = buf[8];
	ilits->stx = buf[11];
	ilits->srx = buf[12];

	ILI_DBG("TP Info: min_x = %d, min_y = %d, max_x = %d, max_y = %d\n",
		ilits->min_x, ilits->min_y, ilits->max_x, ilits->max_y);
	ILI_INFO("TP Info: xch = %d, ych = %d, stx = %d, srx = %d\n",
		ilits->xch_num, ilits->ych_num, ilits->stx, ilits->srx);
	return ret;
}

void ili_ic_get_report_info(void)
{
	u8 buf[10] = { 0 };

	memset(buf, 0xFF, sizeof(buf));

	buf[1] = ilits->fw_info[0];
	buf[3] = ilits->fw_info[2];
	buf[4] = ilits->fw_info[3];
	ilits->rib.nDemoPacketID = buf[3];
	ilits->rib.nDemoFingerType = buf[4] & 0x07;
	ilits->rib.nDemoCustomerType = (buf[4] >> 3) & 0x07;
	ilits->rib.nDemoPenType = (buf[4] >> 6) & 0x03;
	ILI_INFO("Report Info: PacketID=%x FingerType=%x CustomerType=%x PenType=%x\n",
		ilits->rib.nDemoPacketID,
		ilits->rib.nDemoFingerType,
		ilits->rib.nDemoCustomerType,
		ilits->rib.nDemoPenType);
}

int ili_ic_get_all_info(void)
{
	int ret = 0;

	ili_ic_get_protocl_ver();
	ili_ic_get_core_ver();
	ili_ic_get_fw_ver();
	ili_ic_get_tp_info();
	ili_ic_get_panel_info();
	ili_ic_get_report_info();

	return ret;
}

static void ilitek_tddi_ic_check_protocol_ver(u32 pver)
{
	int i = 0;

	if (ilits->protocol->ver == pver) {
		ILI_DBG("same procotol version, do nothing\n");
		return;
	}

	for (i = 0; i < PROTOCL_VER_NUM - 1; i++) {
		if (protocol_info[i].ver == pver) {
			ilits->protocol = &protocol_info[i];
			ILI_INFO("update protocol version = %x\n",
				 ilits->protocol->ver);
			return;
		}
	}

	ILI_ERR("Not found a correct protocol version in list, use newest version\n");
	ilits->protocol = &protocol_info[PROTOCL_VER_NUM - 1];
}

int ili_ic_get_protocl_ver(void)
{
	int ret = 0;
	u8 buf[10] = { 0 };
	u32 ver = 0;

	buf[1] = ilits->fw_info[72];
	buf[2] = ilits->fw_info[73];
	buf[3] = ilits->fw_info[74];
	ver = buf[1] << 16 | buf[2] << 8 | buf[3];

	ilitek_tddi_ic_check_protocol_ver(ver);

	ILI_INFO("Protocol version = %d.%d.%d\n", ilits->protocol->ver >> 16,
		(ilits->protocol->ver >> 8) & 0xFF, ilits->protocol->ver & 0xFF);

	return ret;
}

static u8 ili_chip_id_translate_to_ascii(u8 data)
{
	u8 ret = 0;

	if (data >= 0 && data <= 9)
		ret = data + 48;
	else if (data >= 0x0A && data <= 0x23)
		ret = data - 0x0A + 65;

	return ret;
}

int ili_ic_get_info(void)
{
	int ret = 0;
	u8 tmp1 = 0, tmp2 = 0;

	if (!atomic_read(&ilits->ice_stat)) {
		ILI_ERR("ice mode doesn't enable\n");
		return -1;
	}

	if (ili_ice_mode_read(ilits->chip->pid_addr, &ilits->chip->pid,
				  sizeof(u32)) < 0)
		ILI_ERR("Read chip pid error\n");

	if (((ilits->chip->pid >> 28) & 0xF) == 0xF) {
		/* Need to Read Second Chip ID */
		if (ili_ice_mode_read(ilits->chip->second_pid_addr,
							&ilits->chip->second_pid,
							sizeof(u32)) < 0)
			ILI_ERR("Read second chip pid error\n");

		ilits->chip->id =
			((ilits->chip->second_pid & 0x0000FFFF) << 12) +
			((ilits->chip->pid & 0x0FFF0000) >> 16);

		tmp1 = (ilits->chip->second_pid & 0xFF00) >> 8;
		tmp2 = (ilits->chip->pid & 0x00FF0000) >> 16;
		tmp1 = ili_chip_id_translate_to_ascii(tmp1);
		tmp2 = ili_chip_id_translate_to_ascii(tmp2);
		if (tmp1 == 0 || tmp2 == 0)
			ILI_ERR("Chip id translate error\n");
		snprintf(ilits->chip->product_id,
				sizeof(ilits->chip->product_id),
				"%c%02X%X%c",
				tmp1,
				ilits->chip->second_pid & 0xFF,
				(ilits->chip->pid & 0x0F000000) >> 24,
				tmp2);
	} else {
		ilits->chip->id = ilits->chip->pid >> 16;
		snprintf(ilits->chip->product_id,
				sizeof(ilits->chip->product_id),
				"%04X",
				ilits->chip->id);
	}

	if (ili_ice_mode_read(ilits->chip->otp_addr, &ilits->chip->otp_id,
				  sizeof(u32)) < 0)
		ILI_ERR("Read otp id error\n");
	if (ili_ice_mode_read(ilits->chip->ana_addr, &ilits->chip->ana_id,
				  sizeof(u32)) < 0)
		ILI_ERR("Read ana id error\n");

	ilits->chip->type = (ilits->chip->pid & 0x0000FF00) >> 8;
	ilits->chip->ver = ilits->chip->pid & 0xFF;
	ilits->chip->otp_id &= 0xFF;
	ilits->chip->ana_id &= 0xFF;

	ILI_INFO("CHIP ID = %s\n", ilits->chip->product_id);

	ret = ilitek_tddi_ic_check_info(ilits->chip->pid, ilits->chip->id);

	return ret;
}

int ili_cascade_ic_get_info(bool enter_ice, bool exit_ice, bool mcu, bool reset)
{
	int ret = 0;
	bool needSecondClientChipID = false;

	if (enter_ice)
		ilits->ice_mode_ctrl(ENABLE, mcu, BOTH);

	ili_ic_get_info();

	ili_spi_ice_mode_read(TDDI_PID_ADDR, &ilits->chip->client_pid, sizeof(u32), CLIENT);

	if (((ilits->chip->client_pid >> 28) & 0xF) == 0xF) {
		needSecondClientChipID = true;
		ili_spi_ice_mode_read(TDDI_SECOND_PID_ADDR,
							&ilits->chip->client_second_pid,
							sizeof(u32),
							CLIENT);
	}

	/* set mspi 0 */
	if (ili_ice_mode_write_by_mode(MSPI_REG, 0x00, 1, MASTER) < 0)
		ILI_ERR("Failed to write MSPI_REG in ice mode\n");

	if (exit_ice) {
		if (reset) {
			if (ilits->cascade_info_block.nNum != 0)
				ili_cascade_reset_ctrl(TP_IC_WHOLE_RST_WITHOUT_FLASH, false);
			else
				ili_reset_ctrl(TP_IC_WHOLE_RST_WITHOUT_FLASH);
		} else {
			ilits->ice_mode_ctrl(DISABLE, mcu, BOTH);
		}
	}

	ilits->chip->client_id = ilits->chip->client_pid >> 16;

	/* Compare chip id and chip type.*/
	if (((ilits->chip->pid & 0xFFFFFF00) !=
			(ilits->chip->client_pid & 0xFFFFFF00)) ||
		(needSecondClientChipID &&
			((ilits->chip->second_pid & 0xFFFF) !=
				(ilits->chip->client_second_pid & 0xFFFF)))) {
		ILI_ERR("Client chip pid different from master\n");
		ret = -EINVAL;
	}

	ILI_INFO("Read Client CHIP PID = 0x%X, CHIP ID = 0x%X\n",
		ilits->chip->client_pid, ilits->chip->client_id);
	if (needSecondClientChipID)
		ILI_INFO("Read Client Second CHIP PID = 0x%X\n",
			ilits->chip->second_pid);

	return ret;
}

int ili_ic_dummy_check(void)
{
	int ret = 0;
	u32 wdata = 0xA55A5AA5;
	u32 rdata = 0;
	int i = 0;

	if (!atomic_read(&ilits->ice_stat)) {
		ILI_ERR("ice mode doesn't enable\n");
		return -1;
	}

	for (i = 0; i < 3; i++) {
		if (ili_ice_mode_write(WDT9_DUMMY2, wdata, sizeof(u32)) < 0)
			ILI_ERR("Write dummy error\n");

		if (ili_ice_mode_read(WDT9_DUMMY2, &rdata, sizeof(u32)) < 0)
			ILI_ERR("Read dummy error\n");

		if (rdata == wdata || rdata == (u32)-wdata) {
			if (rdata == -wdata)
				ilits->eng_flow = true;
			else
				ilits->eng_flow = false;

			break;
		}
		mdelay(30);
	}
	if (i >= 3) {
		ILI_ERR("Dummy check incorrect, rdata = %x wdata = %x\n",
			rdata, wdata);
		return -1;
	}
	ILI_INFO("Ilitek IC check successe ilits->eng_flow = %d\n",
		 ilits->eng_flow);

	return ret;
}

void ili_ic_init(void)
{
	chip.pid_addr = TDDI_PID_ADDR;
	chip.second_pid_addr = TDDI_SECOND_PID_ADDR;
	chip.pc_counter_addr = TDDI_PC_COUNTER_ADDR;
	chip.pc_latch_addr = TDDI_PC_LATCH_ADDR;
	chip.otp_addr = TDDI_OTP_ID_ADDR;
	chip.ana_addr = TDDI_ANA_ID_ADDR;
	chip.reset_addr = TDDI_CHIP_RESET_ADDR;

	ilits->chip = &chip;
	ilits->protocol = &protocol_info[PROTOCL_VER_NUM - 1];
}

/* hostdl.c */
static u32 HexToDec(char *phex, s32 len)
{
	u32 ret = 0, temp = 0, i;
	s32 shift = (len - 1) * 4;

	for (i = 0; i < len; shift -= 4, i++) {
		if ((phex[i] >= '0') && (phex[i] <= '9'))
			temp = phex[i] - '0';
		else if ((phex[i] >= 'a') && (phex[i] <= 'f'))
			temp = (phex[i] - 'a') + 10;
		else if ((phex[i] >= 'A') && (phex[i] <= 'F'))
			temp = (phex[i] - 'A') + 10;

		ret |= (temp << shift);
	}
	return ret;
}

static int CalculateCRC32(u32 start_addr, u32 len, u8 *pfw)
{
	int i = 0, j = 0;
	int crc_poly = 0x04C11DB7;
	int tmp_crc = 0xFFFFFFFF;

	for (i = start_addr; i < start_addr + len; i++) {
		tmp_crc ^= (pfw[i] << 24);

		for (j = 0; j < 8; j++) {
			if ((tmp_crc & 0x80000000) != 0)
				tmp_crc = tmp_crc << 1 ^ crc_poly;
			else
				tmp_crc = tmp_crc << 1;
		}
	}
	return tmp_crc;
}

static int calc_hw_dma_crc(u32 start_addr, u32 block_size)
{
	int count = 50;
	u32 busy = 0;

	if (ilits->chip->dma_reset) {
		ILI_DBG("operate dma reset in reg after tp reset\n");
		if (ili_ice_mode_write(0x40040, 0x00800000, 4) < 0)
			ILI_ERR("Failed to open DMA reset\n");
		if (ili_ice_mode_write(0x40040, 0x00000000, 4) < 0)
			ILI_ERR("Failed to close DMA reset\n");
	}
	/* dma1 src1 address */
	if (ili_ice_mode_write(0x072104, start_addr, 4) < 0)
		ILI_ERR("Write dma1 src1 address failed\n");
	/* dma1 src1 format */
	if (ili_ice_mode_write(0x072108, 0x80000001, 4) < 0)
		ILI_ERR("Write dma1 src1 format failed\n");
	/* dma1 dest address */
	if (ili_ice_mode_write(0x072114, 0x0004101C, 4) < 0)
		ILI_ERR("Write dma1 src1 format failed\n");
	/* dma1 dest format */
	if (ili_ice_mode_write(0x072118, 0x80000000, 4) < 0)
		ILI_ERR("Write dma1 dest format failed\n");
	/* Block size*/
	if (ili_ice_mode_write(0x07211C, block_size, 4) < 0)
		ILI_ERR("Write block size (%d) failed\n", block_size);
	/* crc off */
	if (ili_ice_mode_write(0x041016, 0x00, 1) < 0)
		ILI_INFO("Write crc of failed\n");
	/* dma crc */
	if (ili_ice_mode_write(0x041017, 0x03, 1) < 0)
		ILI_ERR("Write dma 1 crc failed\n");
	/* crc on */
	if (ili_ice_mode_write(0x041016, 0x01, 1) < 0)
		ILI_ERR("Write crc on failed\n");
	/* Dma1 stop */
	if (ili_ice_mode_write(0x072100, 0x02000000, 4) < 0)
		ILI_ERR("Write dma1 stop failed\n");
	/* clr int */
	if (ili_ice_mode_write(0x048006, 0x2, 1) < 0)
		ILI_ERR("Write clr int failed\n");
	/* Dma1 start */
	if (ili_ice_mode_write(0x072100, 0x01000000, 4) < 0)
		ILI_ERR("Write dma1 start failed\n");

	/* Polling BIT0 */
	while (count > 0) {
		mdelay(1);
		ili_spi_ice_mode_read(0x048006, &busy, sizeof(u8), MASTER);
		ILI_DBG("busy = %x\n", busy);
		if ((busy & 0x02) == 2)
			break;
		count--;
	}

	if (count <= 0) {
		ILI_ERR("BIT0 is busy\n");
		return -1;
	}

	ili_spi_ice_mode_read(0x04101C, &busy, sizeof(u32), MASTER);

	return busy;
}

static int ilitek_tddi_fw_iram_read(u8 *buf, u32 start, int len)
{
	int limit = 3 * K;	/* SPI transfer data length must less than 4K */
	int addr = 0, loop = 0, tmp_len = len, cnt = 0;
	u8 cmd[4] = { 0 };

	if (!buf) {
		ILI_ERR("buf is null\n");
		return -ENOMEM;
	}

	if (len % limit)
		loop = (len / limit) + 1;
	else
		loop = len / limit;

	for (cnt = 0, addr = start; cnt < loop; cnt++, addr += limit) {
		tmp_len = len - cnt * limit;
		ilits->fw_update_stat = ((len - tmp_len) * 100) / len;
		ILI_DBG("Reading iram data .... %d%c", ilits->fw_update_stat, '%');

		if (tmp_len > limit)
			tmp_len = limit;

		cmd[0] = 0x25;
		cmd[3] = (char)((addr & 0x00FF0000) >> 16);
		cmd[2] = (char)((addr & 0x0000FF00) >> 8);
		cmd[1] = (char)((addr & 0x000000FF));

		if (ilits->wrapper(cmd, 4, NULL, 0, OFF, OFF) < 0) {
			ILI_ERR("Failed to write iram data\n");
			return -ENODEV;
		}

		if (ilits->wrapper(NULL, 0, buf + cnt * limit, tmp_len, OFF, OFF) < 0) {
			ILI_ERR("Failed to Read iram data\n");
			return -ENODEV;
		}

	}
	return 0;
}

int ili_fw_dump_iram_data(u32 start, u32 end, bool mcu)
{
	int i, ret = 0;
	int len, tmp = debug_en;
	bool ice = atomic_read(&ilits->ice_stat);

	if (!ice) {
		ret = ili_ice_mode_ctrl(ENABLE, mcu);
		if (ret < 0) {
			ILI_ERR("Enable ice mode failed\n");
			return ret;
		}
	}

	len = end - start + 1;

	if (len > MAX_HEX_FILE_SIZE) {
		ILI_ERR("len is larger than buffer, abort\n");
		ret = -EINVAL;
		goto out;
	}

	for (i = 0; i < MAX_HEX_FILE_SIZE; i++)
		ilits->update_buf[i] = 0xFF;

	ret = ilitek_tddi_fw_iram_read(ilits->update_buf, start, len);
	if (ret < 0) {
		ILI_ERR("Read IRAM data failed\n");
		goto out;
	}

	debug_en = DEBUG_ALL;
	print_hex_dump(KERN_INFO,
				"IRAM: ",
				DUMP_PREFIX_OFFSET,
				16,
				1,
				ilits->update_buf,
				len,
				false);
	debug_en = tmp;

out:
	if (!ice) {
		if (ili_ice_mode_ctrl(DISABLE, mcu) < 0)
			ILI_ERR("Enable ice mode failed after code reset\n");
	}

	ILI_INFO("Dump IRAM %s\n", (ret < 0) ? "FAIL" : "SUCCESS");
	return ret;
}

static int ilitek_tddi_fw_iram_program(u32 start, u8 *w_buf, u32 w_len, u32 split_len)
{
	int i = 0, j = 0, addr = 0;
	u32 end = start + w_len;
	bool fix_4_alignment = false;

	if (split_len % 4 > 0)
		ILI_ERR("Since split_len must be four-aligned, it must be a multiple of four");

	if (split_len != 0) {
		for (addr = start, i = 0; addr < end; addr += split_len, i += split_len) {
			if ((addr + split_len) > end) {
				split_len = end - addr;
				if (split_len % 4 != 0)
					fix_4_alignment = true;
			}

			ilits->update_buf[0] = SPI_WRITE;
			ilits->update_buf[1] = 0x25;
			ilits->update_buf[2] = (char)((addr & 0x000000FF));
			ilits->update_buf[3] = (char)((addr & 0x0000FF00) >> 8);
			ilits->update_buf[4] = (char)((addr & 0x00FF0000) >> 16);

			for (j = 0; j < split_len; j++)
				ilits->update_buf[5 + j] = w_buf[i + j];

			if (fix_4_alignment) {
				ILI_INFO("org split_len = 0x%X\n", split_len);
				ILI_INFO("idev->update_buf[5 + 0x%X] = 0x%X\n",
					split_len - 4, ilits->update_buf[5 + split_len - 4]);
				ILI_INFO("idev->update_buf[5 + 0x%X] = 0x%X\n",
					split_len - 3, ilits->update_buf[5 + split_len - 3]);
				ILI_INFO("idev->update_buf[5 + 0x%X] = 0x%X\n",
					split_len - 2, ilits->update_buf[5 + split_len - 2]);
				ILI_INFO("idev->update_buf[5 + 0x%X] = 0x%X\n",
					split_len - 1, ilits->update_buf[5 + split_len - 1]);
				for (j = 0; j < (4 - (split_len % 4)); j++) {
					ilits->update_buf[5 + j + split_len] = 0xFF;
					ILI_INFO("idev->update_buf[5 + 0x%X] = 0x%X\n",
						j + split_len,
						ilits->update_buf[5 + j + split_len]);
				}

				ILI_INFO("split_len %% 4 = %d\n", split_len % 4);
				split_len = split_len + (4 - (split_len % 4));
				ILI_INFO("fix split_len = 0x%X\n", split_len);
			}
			if (ilits->spi_write_then_read(ilits->spi,
										ilits->update_buf,
										split_len + 5,
										NULL,
										0)) {
				ILI_ERR("Failed to write data via SPI in host download (%x)\n",
					split_len + 5);
				return -EIO;
			}
			ilits->fw_update_stat = (i * 100) / w_len;
		}
	} else {
		for (i = 0; i < MAX_HEX_FILE_SIZE; i++)
			ilits->update_buf[i] = 0xFF;

		ilits->update_buf[0] = SPI_WRITE;
		ilits->update_buf[1] = 0x25;
		ilits->update_buf[2] = (char)((start & 0x000000FF));
		ilits->update_buf[3] = (char)((start & 0x0000FF00) >> 8);
		ilits->update_buf[4] = (char)((start & 0x00FF0000) >> 16);

		memcpy(&ilits->update_buf[5], w_buf, w_len);
		if (w_len % 4 != 0) {
			ILI_INFO("org w_len = %d\n", w_len);
			w_len = w_len + (4 - (w_len % 4));
			ILI_INFO("w_len = %d w_len %% 4 = %d\n", w_len, w_len % 4);
		}
		/*
		 * It must be supported by platforms that have the ability
		 * to transfer all data at once.
		 */
		if (ilits->spi_write_then_read(ilits->spi,
									ilits->update_buf,
									w_len + 5,
									NULL,
									0) < 0) {
			ILI_ERR("Failed to write data via SPI in host download (%x)\n",
				w_len + 5);
			return -EIO;
		}
	}
	return 0;
}

static int ilitek_tddi_fw_iram_upgrade(u8 *pfw, bool mcu)
{
	int i, ret = UPDATE_PASS;
	u32 mode, hex_crc, dma, iram_crc = 0;
	u8 *fw_ptr = NULL, crc_temp[4], crc_len = 4;
	bool iram_crc_err = false;
	bool dma_crc_err = false;
	bool dma_crc_err_client = false;
	struct ilitek_dma_config dma1;
	int slave_polling_cnt = 50;

	if (!ilits->ddi_rest_done) {
		if (ilits->actual_tp_mode != P5_X_FW_GESTURE_MODE) {
			if (ilits->cascade_info_block.nNum != 0)
				ili_cascade_reset_ctrl(ilits->reset, true);
			else
				ili_reset_ctrl(ilits->reset);
		}

		ilits->ice_mode_ctrl(ENABLE, mcu, BOTH);

		if (ret < 0)
			return -EFW_ICE_MODE;
	} else {
		/* Restore it if the wq of load_fw_ddi has been called. */
		ilits->ddi_rest_done = false;
	}

	/*
	 * Point to pfw with different addresses
	 * for getting its block data.
	 */
	fw_ptr = pfw;
	if (ilits->actual_tp_mode == P5_X_FW_TEST_MODE) {
		mode = NEED_UPGRADE_MP;
	} else if (ilits->actual_tp_mode == P5_X_FW_GESTURE_MODE) {
		mode = NEED_UPGRADE_GESTURE;
		crc_len = 0;
	} else {
		mode = NEED_UPGRADE_AP;
	}

	/* backup dma1 parameter */
	ili_get_dma1_config(&dma1);

	/* Program data to iram acorrding to each block */
	for (i = 0; i < FW_BLOCK_INFO_NUM; i++) {
		if ((fbi[i].mode & (0x01 << mode)) && fbi[i].len != 0) {
			ILI_DBG("Download %s code from hex 0x%x to IRAM 0x%x, len = 0x%x\n",
					fbi[i].name,
					fbi[i].start,
					fbi[i].fix_mem_start_multi_buf[mode],
					fbi[i].len);

			if (ilitek_tddi_fw_iram_program(
					fbi[i].fix_mem_start_multi_buf[mode],
					(fw_ptr + fbi[i].start),
					fbi[i].len,
					SPI_UPGRADE_LEN) < 0)
				ILI_ERR("IRAM program failed\n");

			hex_crc = CalculateCRC32(fbi[i].start, fbi[i].len - crc_len, fw_ptr);
			dma = calc_hw_dma_crc(
				fbi[i].fix_mem_start_multi_buf[mode],
				fbi[i].len - crc_len);

			if (hex_crc != dma)
				dma_crc_err = true;

			if (mode != NEED_UPGRADE_GESTURE) {
				ilitek_tddi_fw_iram_read(
					crc_temp,
					(fbi[i].fix_mem_start_multi_buf[mode] +
						fbi[i].len - crc_len),
					sizeof(crc_temp));
				iram_crc =
					crc_temp[0] << 24 |
					crc_temp[1] << 16 |
					crc_temp[2] << 8 |
					crc_temp[3];
				if (iram_crc != dma)
					iram_crc_err = true;
			}

			ILI_INFO("%s CRC is %s hex(%x):dma(%x):iram(%x), calculation len is 0x%x\n",
				fbi[i].name,
				(dma_crc_err || iram_crc_err) ? "Invalid !" : "Correct !",
				hex_crc,
				dma,
				iram_crc,
				fbi[i].len - crc_len);

			if (ilits->cascade_info_block.nNum != 0) {

				/* Slave crc polling done */
				while (slave_polling_cnt > 0) {
					ili_spi_ice_mode_read(0x048006, &dma, sizeof(u8), CLIENT);
					ILI_DBG("busy = %x\n", dma);
					if ((dma & 0x02) == 2)
						break;
					slave_polling_cnt--;
					mdelay(1);
				}

				if (slave_polling_cnt <= 0) {
					ILI_ERR("slave BIT0 is busy\n");
					dma_crc_err_client = true;
					goto out;
				}
				/* read slave dma crc */
				ili_spi_ice_mode_read(DMA_CRC_ADDR, &dma, sizeof(u32), CLIENT);

				if (hex_crc != dma)
					dma_crc_err_client = true;

				if (ili_ice_mode_write_by_mode(MSPI_REG, 0x00, 1, MASTER) < 0)
					ILI_ERR("Failed to write MSPI_REG in ice mode\n");

				ILI_INFO("Client %s CRC is %s hex(%x) : dma(%x) : iram(%x)\n",
					fbi[i].name,
					(dma_crc_err_client) ? "Invalid !" : "Correct !",
					hex_crc,
					dma,
					iram_crc);
			}
out:
			if (dma_crc_err || dma_crc_err_client || iram_crc_err) {
				ILI_ERR("CRC Error! print iram data with first 16 bytes\n");
				ili_fw_dump_iram_data(0x0, 0xF, OFF);
				return -EFW_CRC;
			}
		}
	}

	/* recovery dma1 parameter */
	ILI_INFO("recovery DMA 1 parameters\n");
	ili_set_dma1_config(&dma1);

	if (ilits->actual_tp_mode != P5_X_FW_GESTURE_MODE) {
		if (ilits->cascade_info_block.nNum != 0) {
			if (ili_cascade_reset_ctrl(TP_IC_WHOLE_RST_WITHOUT_FLASH,
									false) < 0) {
				ILI_ERR("TP Whole reset without flash failed(iram programming)\n");
				ret = -EFW_REST;
				return ret;
			}
		} else {
			if (ili_reset_ctrl(TP_IC_WHOLE_RST_WITHOUT_FLASH) < 0) {
				ILI_ERR("TP Whole reset without flash failed(iram programming)\n");
				ret = -EFW_REST;
				return ret;
			}
		}
	}

	if (ili_ice_mode_ctrl(DISABLE, mcu) < 0) {
		ILI_ERR("Disable ice mode failed after code reset\n");
		ret = -EFW_ICE_MODE;
	}

	mdelay(65);/* for fw ready */

	return ret;
}

static int ilitek_fw_calc_file_crc(u8 *pfw)
{
	int i, block_num = 0;
	u32 ex_addr, data_crc, file_crc;

	for (i = 0; i < ARRAY_SIZE(fbi); i++) {
		if (fbi[i].len >= MAX_HEX_FILE_SIZE) {
			ILI_ERR("Content of fw file is invalid. (fbi[%d].len=0x%x)\n",
				i, fbi[i].len);
			return -1;
		}

		if (fbi[i].end <= 4)
			continue;
		block_num++;
		ex_addr = fbi[i].end;
		data_crc = CalculateCRC32(fbi[i].start, fbi[i].len - 4, pfw);
		file_crc =
			pfw[ex_addr - 3] << 24 |
			pfw[ex_addr - 2] << 16 |
			pfw[ex_addr - 1] << 8 |
			pfw[ex_addr];
		ILI_DBG("data crc = %x, file crc = %x\n", data_crc, file_crc);
		if (data_crc != file_crc) {
			ILI_ERR("Content of fw file is broken. (%d, %x, %x)\n",
				i, data_crc, file_crc);
			return -1;
		}
	}

	if (fbi[MP].end <= 1 * K ||
			fbi[AP].end <= 1 * K ||
			(block_num == 0)) {
		ILI_ERR("fw broken. fbi[AP].end = 0x%x, fbi[MP].end = 0x%x, block_num = %d\n",
			fbi[AP].end, fbi[MP].end, block_num);
		return -1;
	}

	ILI_INFO("Content of fw file is correct\n");
	return 0;
}

static int ilitek_tddi_fw_update_block_info(u8 *pfw)
{
	u32 ges_area_section = 0, ges_info_addr = 0;
	u32 ges_fw_start = 0, ges_fw_end = 0, ges_mem_start_recalculate = 0;
	u32 ap_end = 0, ap_len = 0;
	u32 fw_info_addr = 0, fw_mp_ver_addr = 0, blk_iram_addr_default = 0;
	int i = 0, j = 0;

	if (tfd.hex_tag != BLOCK_TAG_AF) {
		ILI_ERR("HEX TAG is invalid (0x%X)\n", tfd.hex_tag);
		return -EINVAL;
	}

	/* Parsing gesture info form AP code */
	ges_info_addr = (fbi[AP].end + 1 - 60);
	ges_area_section =
		(pfw[ges_info_addr + 3] << 24) +
		(pfw[ges_info_addr + 2] << 16) +
		(pfw[ges_info_addr + 1] << 8) +
		pfw[ges_info_addr];
	ges_mem_start_recalculate =
		(pfw[ges_info_addr + 7] << 24) +
		(pfw[ges_info_addr + 6] << 16) +
		(pfw[ges_info_addr + 5] << 8) +
		pfw[ges_info_addr + 4];
	ap_end =
		(pfw[ges_info_addr + 11] << 24) +
		(pfw[ges_info_addr + 10] << 16) +
		(pfw[ges_info_addr + 9] << 8) +
		pfw[ges_info_addr + 8];

	if (ap_end != ges_mem_start_recalculate)
		ap_len = ap_end - ges_mem_start_recalculate + 1;

	ges_fw_start =
		(pfw[ges_info_addr + 15] << 24) +
		(pfw[ges_info_addr + 14] << 16) +
		(pfw[ges_info_addr + 13] << 8) +
		pfw[ges_info_addr + 12];
	ges_fw_end =
		(pfw[ges_info_addr + 19] << 24) +
		(pfw[ges_info_addr + 18] << 16) +
		(pfw[ges_info_addr + 17] << 8) +
		pfw[ges_info_addr + 16];

	if (ges_fw_end != ges_fw_start)
		fbi[GESTURE].len = ges_fw_end - ges_fw_start;

	/* update gesture address */
	fbi[GESTURE].start = ges_fw_start;

	ILI_INFO("==== Gesture loader info ====\n");
	ILI_INFO("to ap_addr, start = 0x%x, ap_end = 0x%x, ap_len = 0x%x\n",
		ges_mem_start_recalculate,
		ap_end, ap_len);
	ILI_INFO("hex_addr, start = 0x%x, ges_end = 0x%x, ges_len = 0x%x, ges_area = 0x%x\n",
		ges_fw_start,
		ges_fw_end,
		fbi[GESTURE].len, ges_area_section);
	ILI_INFO("=============================\n");

	if (tfd.mapping_tag == BLOCK_TAG_B0) {

		fbi[AP].mem_start = (fbi[AP].fix_mem_start != INT_MAX) ? fbi[AP].fix_mem_start : 0;
		fbi[DATA_BLOCK].mem_start =
			(fbi[DATA_BLOCK].fix_mem_start != INT_MAX) ?
			fbi[DATA_BLOCK].fix_mem_start :
			DLM_START_ADDRESS;
		fbi[TUNING].mem_start =
			(fbi[TUNING].fix_mem_start != INT_MAX) ?
			fbi[TUNING].fix_mem_start :
			fbi[DATA_BLOCK].mem_start + fbi[DATA_BLOCK].len;
		fbi[MP].mem_start =
			(fbi[MP].fix_mem_start != INT_MAX) ?
			fbi[MP].fix_mem_start :
			0;
		fbi[GESTURE].mem_start = ges_mem_start_recalculate;
		fbi[TAG].mem_start =
			(fbi[TAG].fix_mem_start != INT_MAX) ?
			fbi[TAG].fix_mem_start :
			0;
		fbi[PARA_BACKUP].mem_start =
			(fbi[PARA_BACKUP].fix_mem_start != INT_MAX) ?
			fbi[PARA_BACKUP].fix_mem_start :
			0;
		fbi[DDI].mem_start =
			(fbi[DDI].fix_mem_start != INT_MAX) ?
			fbi[DDI].fix_mem_start :
			0;
		fbi[PEN].mem_start =
			(fbi[PEN].fix_mem_start != INT_MAX) ?
			fbi[PEN].fix_mem_start :
			0;

		/* upgrade mode define */
		fbi[DATA_BLOCK].mode =
			fbi[AP].mode =
			fbi[TUNING].mode =
			fbi[PEN].mode =
			0x01 << NEED_UPGRADE_AP;
		fbi[MP].mode = 0x01 << NEED_UPGRADE_MP;
		fbi[GESTURE].mode = 0x01 << NEED_UPGRADE_GESTURE;

		for (i = 0; i < FW_BLOCK_INFO_NUM; i++) {
			fbi[i].name = "UNDEFINED";
			for (j = 0; j < DEFINED_MODE_NUM; j++) {
				if (fbi[i].mode & (0x01 << j) && (fbi[i].len != 0))
					fbi[i].fix_mem_start_multi_buf[j] = fbi[i].mem_start;
			}
		}
	} else if (tfd.mapping_tag == BLOCK_TAG_B2) {
		for (i = 0; i < FW_BLOCK_INFO_NUM; i++) {
			fbi[i].name = "UNDEFINED";
			for (j = 0; j < DEFINED_MODE_NUM; j++) {
				if (fbi[i].mode & (0x01 << j)) {
					switch (i) {
					case DATA_BLOCK:
						blk_iram_addr_default = DLM_START_ADDRESS;
						break;
					case TUNING:
						blk_iram_addr_default =
							fbi[DATA_BLOCK].fix_mem_start_multi_buf[j] +
							fbi[DATA_BLOCK].len;
						break;
					case GESTURE:
						blk_iram_addr_default = 0;
						fbi[i].fix_mem_start_multi_buf[j] =
							ges_mem_start_recalculate;
						break;
					default:
						blk_iram_addr_default = 0;
						break;
					}
					fbi[i].fix_mem_start_multi_buf[j] =
						(fbi[i].fix_mem_start_multi_buf[j] != INT_MAX) ?
						fbi[i].fix_mem_start_multi_buf[j] :
						blk_iram_addr_default;
				}
			}
		}
	}

	ILI_INFO("=======================update block mem_start over=======================");
	for (i = 0; i < FW_BLOCK_INFO_NUM; i++) {
		ILI_INFO("Block[%d]: AP=0x%x, MP=0x%x, GEST=0x%x\n",
			i,
			fbi[i].fix_mem_start_multi_buf[NEED_UPGRADE_AP],
			fbi[i].fix_mem_start_multi_buf[NEED_UPGRADE_MP],
			fbi[i].fix_mem_start_multi_buf[NEED_UPGRADE_GESTURE]);
	}
	ILI_INFO("===========================================================");

	fbi[AP].name = "AP";
	fbi[DATA_BLOCK].name = "DATA_BLOCK";
	fbi[TUNING].name = "TUNING";
	fbi[MP].name = "MP";
	fbi[GESTURE].name = "GESTURE";
	fbi[TAG].name = "TAG";
	fbi[PARA_BACKUP].name = "PARA_BACKUP";
	fbi[DDI].name = "DDI";
	fbi[PEN].name = "PEN";

	if (fbi[AP].end > (64*K))
		tfd.is80k = true;

	/* Copy fw info  */
	fw_info_addr = fbi[AP].end - INFO_HEX_ST_ADDR;
	ILI_INFO("Parsing hex info start addr = 0x%x\n",
		fw_info_addr);
	ipio_memcpy(
		ilits->fw_info,
		(pfw + fw_info_addr),
		sizeof(ilits->fw_info),
		sizeof(ilits->fw_info));

	/* copy fw mp ver */
	fw_mp_ver_addr = fbi[MP].end - INFO_MP_HEX_ADDR;
	ILI_INFO("Parsing hex mp ver addr = 0x%x\n", fw_mp_ver_addr);
	ipio_memcpy(
		ilits->fw_mp_ver,
		pfw + fw_mp_ver_addr,
		sizeof(ilits->fw_mp_ver),
		sizeof(ilits->fw_mp_ver));

	/* copy fw core ver */
	ilits->chip->core_ver =
		(ilits->fw_info[68] << 24) |
		(ilits->fw_info[69] << 16) |
		(ilits->fw_info[70] << 8) |
		ilits->fw_info[71];
	ILI_INFO("New FW Core version = %x\n", ilits->chip->core_ver);

	/* Get hex fw vers */
	tfd.new_fw_cb =
		(ilits->fw_info[48] << 24) |
		(ilits->fw_info[49] << 16) |
		(ilits->fw_info[50] << 8) |
		ilits->fw_info[51];

	/* Get hex report info block*/
	ipio_memcpy(
		&ilits->rib,
		ilits->fw_info,
		sizeof(ilits->rib),
		sizeof(ilits->rib));
	/* 1 byte, Resolution 0-2 bits,
	 * CustomType 3-5 bits, PenType 6-7 bits
	 */
	ilits->rib.nReportResolutionMode =
		(ilits->chip->core_ver >= CORE_VER_1470) ?
		(ilits->rib.nReportResolutionMode & 0x07) :
		POSITION_LOW_RESOLUTION;
	ilits->PenType =
		(ilits->chip->core_ver >= CORE_VER_1700) ?
		(ilits->rib.nCustomerType >> 3) :
		POSITION_PEN_TYPE_OFF;

	if (ilits->chip->core_ver >= CORE_VER_1700) {
		/*CustomerType 3 bits*/
		ilits->rib.nCustomerType =
			ilits->rib.nCustomerType & 0x07;
		ilits->customertype_off =
			POSITION_CUSTOMER_TYPE_OFF_3BITS;
	} else {
		/*CustomerType 5 bits*/
		ilits->rib.nCustomerType =
			(ilits->chip->core_ver >= CORE_VER_1470) ?
			ilits->rib.nCustomerType :
			POSITION_CUSTOMER_TYPE_OFF;
		ilits->customertype_off =
			POSITION_CUSTOMER_TYPE_OFF;
	}

	ILI_INFO("ReportInfoBlock: ReportByPixel=%d HostDL=%d Ice=%d Client=%d I2c=%d\n",
		ilits->rib.nReportByPixel,
		ilits->rib.nIsHostDownload,
		ilits->rib.nIsSPIICE,
		ilits->rib.nIsSPICLIENT,
		ilits->rib.nIsI2C);

	ILI_INFO("ReportInfoBlock: Reserved00=%d ReportResolutionMode=%d CustomerType=%d\n",
		ilits->rib.nReserved00,
		ilits->rib.nReportResolutionMode,
		ilits->rib.nCustomerType);

	ILI_INFO("PenType = 0x%x, Customer Type OFF = 0x%x\n",
		ilits->PenType,
		ilits->customertype_off);

#if ENABLE_PEN_MODE
	/* Get hex Pen info block*/
	fw_info_addr = fbi[AP].end - INFO_PEN_ST_ADDR;
	ILI_INFO("Parsing PenInfoBlock start addr = 0x%x\n",
		fw_info_addr);
	ipio_memcpy(
		&ilits->pen_info_block,
		(pfw + fw_info_addr),
		sizeof(ilits->pen_info_block),
		sizeof(ilits->pen_info_block));

	ILI_INFO("PenInfoBlock: PxRaw=%d PyRaw=%d PxVa=%d PyVa=%d X_MP=%d Chip=%d Samp=%d Rsv=%d\n",
		ilits->pen_info_block.nPxRaw,
		ilits->pen_info_block.nPyRaw,
		ilits->pen_info_block.nPxVa,
		ilits->pen_info_block.nPyVa,
		ilits->pen_info_block.nPenX_MP,
		ilits->pen_info_block.nPenChipnum,
		ilits->pen_info_block.nPenSamplenum,
		ilits->pen_info_block.nReserved03);
#endif

#if ENABLE_CASCADE
	/* Get hex Cascade info block*/
	fw_info_addr =
		fbi[AP].end -
		INFO_CASCADE_ST_ADDR;
	ILI_INFO("Parsing CascadeInfoBlock start addr = 0x%x\n",
		fw_info_addr);
	ipio_memcpy(
		&ilits->cascade_info_block,
		(pfw + fw_info_addr),
		sizeof(ilits->cascade_info_block),
		sizeof(ilits->cascade_info_block));

	ILI_INFO("CascadeInfoBlock: Disable=%d Num=%d Rsv0=%d Rsv1=%d Rsv2=%d Rsv3=%d\n",
		ilits->cascade_info_block.nDisable,
		ilits->cascade_info_block.nNum,
		ilits->cascade_info_block.nReserved00,
		ilits->cascade_info_block.nReserved01,
		ilits->cascade_info_block.nReserved02,
		ilits->cascade_info_block.nReserved03);
#else
	ilits->cascade_info_block.nDisable = ENABLE;
	ilits->cascade_info_block.nNum = 0;
	ILI_INFO("CascadeInfoBlock : nDisable = %d, nNum = %d\n",
		ilits->cascade_info_block.nDisable,
		ilits->cascade_info_block.nNum);
#endif

	/* Calculate update address */
	ILI_INFO("New Firmware AP Version = 0x%x\n",
		tfd.new_fw_cb);
	ILI_INFO("star_addr = 0x%06X, end_addr = 0x%06X, Block Num = %d\n",
		tfd.start_addr,
		tfd.end_addr,
		tfd.block_number);

	return 0;
}

static int ilitek_tddi_upgrade_mode_return(int hex_mode)
{
	int mode_need_upgrade = 0;

	if (hex_mode != 0 && hex_mode <= DEFINED_MODE_NUM) {
		mode_need_upgrade = hex_mode - 1;
	} else {
		if (hex_mode == 0) {
			mode_need_upgrade = -2;
			ILI_DBG("Number of B2 tag hex_mode is equal to 0!\n");
		} else {
			mode_need_upgrade = -1;
			ILI_DBG("Number of B2 tag hex_mode is equal to %x!\n",
				hex_mode);
		}
	}
	return mode_need_upgrade;
}

static int ilitek_tddi_fw_ili_convert(u8 *pfw)
{
	int i = 0, j = 0, size, blk_num = 0, blk_map = 0, num;
	int b0_addr = 0, b0_num = 0;
	/*b2 tag*/
	int ili_ver = 0, b2_addr = 0, b2_hex_mode = 0;
	int af_block_num = 0, b2_block_num = 0, mode_need_upgrade = 0;
	bool b2_parse_finish = false;
	int ili_file_header_len = 0;

	if (ERR_ALLOC_MEM(ilits->md_fw_ili))
		return -ENOMEM;

	CTPM_FW = ilits->md_fw_ili;
	size = ilits->md_fw_ili_size;

	if (size < ILI_FILE_HEADER) {
		ILI_ERR("size of ILI file is invalid\n");
		return -EINVAL;
	}
	blk_num = CTPM_FW[131];

	if ((blk_num & BIT(6)) != BIT(6)) {
		tfd.mapping_tag = BLOCK_TAG_B0;
		ili_file_header_len = ILI_FILE_HEADER;
	} else {
		tfd.mapping_tag = BLOCK_TAG_B2;
		ili_file_header_len = (CTPM_FW[129] << 8) | CTPM_FW[130];
	}

	if (size < ili_file_header_len ||
			size > (MAX_HEX_FILE_SIZE + ili_file_header_len)) {
		ILI_ERR("check again, size of ILI file is invalid\n");
		return -EINVAL;
	}

	/* Check if it's old version of ILI format. */
	if (CTPM_FW[22] == 0xFF && CTPM_FW[23] == 0xFF &&
		CTPM_FW[24] == 0xFF && CTPM_FW[25] == 0xFF) {
		ILI_ERR("Invalid ILI format, abort!\n");
		return -EINVAL;
	}

	if (tfd.mapping_tag == BLOCK_TAG_B0) {
		blk_map = (CTPM_FW[129] << 8) | CTPM_FW[130];
		ILI_INFO("Parsing ILI file, block num = %d, block mapping = %x\n",
			blk_num, blk_map);

		if (blk_num > (FW_BLOCK_INFO_NUM - 1) || !blk_num || !blk_map) {
			ILI_ERR("Number of block or block mapping is invalid, abort!\n");
			return -EINVAL;
		}
	} else if (tfd.mapping_tag == BLOCK_TAG_B2) {
		ili_ver = CTPM_FW[128];
		blk_num = CTPM_FW[131] &
			(BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5));
		ILI_INFO("ILI file, block num = %d, ili_file_header_len = %d, ili_ver = %d\n",
			blk_num, ili_file_header_len, ili_ver);

		if (blk_num > (FW_BLOCK_INFO_NUM - 1) || !blk_num) {
			ILI_ERR("Number of block or block mapping is invalid, abort!\n");
			return -EINVAL;
		}
	}

	memset(fbi, 0x0, sizeof(fbi));

	for (i = 0; i < FW_BLOCK_INFO_NUM; i++) {
		for (j = 0; j < DEFINED_MODE_NUM; j++)
			fbi[i].fix_mem_start_multi_buf[j] = INT_MAX;
	}

	tfd.start_addr = 0;
	tfd.end_addr = 0;
	tfd.hex_tag = BLOCK_TAG_AF;

	/* Parsing block info */
	if (tfd.mapping_tag == BLOCK_TAG_B0) {
		for (i = 0; i < FW_BLOCK_INFO_NUM; i++) {
			/* B0 tag */
			b0_addr =
				(CTPM_FW[4 + i * 4] << 16) |
				(CTPM_FW[5 + i * 4] << 8) |
				(CTPM_FW[6 + i * 4]);
			b0_num = CTPM_FW[7 + i * 4];
			if ((b0_num != 0) && (b0_addr != 0x000000))
				fbi[b0_num].fix_mem_start = b0_addr;

			/* AF tag */
			num = i + 1;
			if (num >= FW_BLOCK_INFO_NUM)
				break;
			if (((blk_map >> i) & 0x01) == 0x01) {
				fbi[num].start =
					(CTPM_FW[132 + i * 6] << 16) |
					(CTPM_FW[133 + i * 6] << 8) |
					CTPM_FW[134 + i * 6];
				fbi[num].end =
					(CTPM_FW[135 + i * 6] << 16) |
					(CTPM_FW[136 + i * 6] << 8) |
					CTPM_FW[137 + i * 6];

				if (fbi[num].fix_mem_start == 0)
					fbi[num].fix_mem_start = INT_MAX;

				fbi[num].len = fbi[num].end - fbi[num].start + 1;
				 ILI_DBG("Block[%d]:start_addr=%x, end=%x, fix_mem_start=0x%x\n",
					num,
					fbi[num].start,
					fbi[num].end,
					fbi[num].fix_mem_start);
				if (num == GESTURE)
					ilits->gesture_load_code = true;
			}
		}
	} else if (tfd.mapping_tag == BLOCK_TAG_B2) {
		i = 0;
		while (i < blk_num || !b2_parse_finish) {
			/* AF tag */
			if (i < blk_num) {
				af_block_num = CTPM_FW[6 + i * 7];
				if (af_block_num >= FW_BLOCK_INFO_NUM) {
					ILI_ERR("AF is out of range of defined block array!\n");
					return -EINVAL;
				}
				fbi[af_block_num].start =
					(CTPM_FW[0 + i * 7] << 16) |
					(CTPM_FW[1 + i * 7] << 8) |
					CTPM_FW[2 + i * 7];
				fbi[af_block_num].end =
					(CTPM_FW[3 + i * 7] << 16) |
					(CTPM_FW[4 + i * 7] << 8) |
					CTPM_FW[5 + i * 7];
				fbi[af_block_num].len =
					fbi[af_block_num].end - fbi[af_block_num].start + 1;

				ILI_INFO("Block[%d]: start_addr = %x, end = %x\n",
					af_block_num,
					fbi[af_block_num].start,
					fbi[af_block_num].end);
				if (af_block_num == GESTURE)
					ilits->gesture_load_code = true;
			}

			/* B2 tag */
			if (!b2_parse_finish) {
				if (256 + (i + 1) * 5 < 512) {
					b2_addr =
						(CTPM_FW[256 + i * 5] << 16) |
						(CTPM_FW[257 + i * 5] << 8) |
						CTPM_FW[258 + i * 5];
					b2_block_num = CTPM_FW[259 + i * 5];
					b2_hex_mode = CTPM_FW[260 + i * 5];
					mode_need_upgrade =
						ilitek_tddi_upgrade_mode_return(b2_hex_mode);
					if (mode_need_upgrade == -2) {
						b2_parse_finish = true;
						goto next_iter;
					}

					if (mode_need_upgrade == -1) {
						ILI_ERR("Num of B2 Tag blk is invalid, abort!\n");
						return -EINVAL;
					}

					fbi[b2_block_num].mode |= 0x01 << mode_need_upgrade;
					fbi[b2_block_num].fix_mem_start_multi_buf
						[mode_need_upgrade] = b2_addr;

				} else {
					b2_parse_finish = true;
				}
			}

next_iter:
			i++;
		}
	}

	ILI_DBG("=======================ili read over=======================\n");
	for (i = 0; i < FW_BLOCK_INFO_NUM; i++) {
		ILI_DBG("Block[%d]: AP=0x%x, MP=0x%x, GEST=0x%x\n",
			i,
			fbi[i].fix_mem_start_multi_buf[NEED_UPGRADE_AP],
			fbi[i].fix_mem_start_multi_buf[NEED_UPGRADE_MP],
			fbi[i].fix_mem_start_multi_buf[NEED_UPGRADE_GESTURE]);
	}
	ILI_DBG("===========================================================\n");

	memcpy(pfw, CTPM_FW + ili_file_header_len, size - ili_file_header_len);

	if (ilitek_fw_calc_file_crc(pfw) < 0)
		return -1;

	tfd.block_number = blk_num;
	tfd.end_addr = size - ili_file_header_len;

	return 0;
}

static int ilitek_tddi_fw_hex_convert(u8 *phex, int size, u8 *pfw)
{
	int block = 0;
	u32 i = 0, j = 0, k = 0, m = 0, n = 0, num = 0;
	u32 len = 0, addr = 0, type = 0;
	u32 start_addr = 0x0, end_addr = 0x0, ex_addr = 0;
	u32 offset;
	int b2_hex_mode = 0;
	int mode_need_upgrade = 0;

	tfd.mapping_tag = -1;

	memset(fbi, 0x0, sizeof(fbi));

	for (m = 0; m < FW_BLOCK_INFO_NUM; m++) {
		for (n = 0; n < DEFINED_MODE_NUM; n++)
			fbi[m].fix_mem_start_multi_buf[n] = INT_MAX;
	}

	/* Parsing HEX file */
	for (i = 0; i < size;) {
		len = HexToDec(&phex[i + 1], 2);
		addr = HexToDec(&phex[i + 3], 4);
		type = HexToDec(&phex[i + 7], 2);

		if (type == 0x04) {
			ex_addr = HexToDec(&phex[i + 9], 4);
		} else if (type == 0x02) {
			ex_addr = HexToDec(&phex[i + 9], 4);
			ex_addr = ex_addr >> 12;
		} else if (type == BLOCK_TAG_AF) {
			/* insert block info extracted from hex */
			tfd.hex_tag = type;
			if (tfd.hex_tag == BLOCK_TAG_AF)
				num = HexToDec(&phex[i + 9 + 6 + 6], 2);
			else
				num = 0xFF;

			if (num > (FW_BLOCK_INFO_NUM - 1)) {
				ILI_ERR("ERROR! block num is larger than its define (%d, %d)\n",
						num, FW_BLOCK_INFO_NUM - 1);
				return -EINVAL;
			}

			fbi[num].start = HexToDec(&phex[i + 9], 6);
			fbi[num].end = HexToDec(&phex[i + 9 + 6], 6);
			fbi[num].fix_mem_start = INT_MAX;
			fbi[num].len = fbi[num].end - fbi[num].start + 1;
			ILI_DBG("Block[%d]: start_addr = %x, end = %x",
				num, fbi[num].start, fbi[num].end);

			if (num == GESTURE)
				ilits->gesture_load_code = true;

			block++;
		} else if (type == BLOCK_TAG_B0 && tfd.hex_tag == BLOCK_TAG_AF) {
			if (tfd.mapping_tag != type)
				tfd.mapping_tag = type;

			num = HexToDec(&phex[i + 9 + 6], 2);

			if (num > (FW_BLOCK_INFO_NUM - 1)) {
				ILI_ERR("ERROR! block num is larger than its define (%d, %d)\n",
						num, FW_BLOCK_INFO_NUM - 1);
				return -EINVAL;
			}

			fbi[num].fix_mem_start = HexToDec(&phex[i + 9], 6);
			ILI_DBG("Tag 0xB0: change Block[%d] to addr = 0x%x\n",
				num,
				fbi[num].fix_mem_start);
		} else if (type == BLOCK_TAG_B2 && tfd.hex_tag == BLOCK_TAG_AF) {
			if (tfd.mapping_tag != type)
				tfd.mapping_tag = type;

			b2_hex_mode = HexToDec(&phex[i + 9 + 6], 2);
			num = HexToDec(&phex[i + 9 + 6 + 2], 2);
			if (num > (FW_BLOCK_INFO_NUM - 1)) {
				ILI_ERR("[b2] block num is larger than its define (%d, %d)\n",
						num, FW_BLOCK_INFO_NUM - 1);
				return -EINVAL;
			}
			mode_need_upgrade = ilitek_tddi_upgrade_mode_return(b2_hex_mode);
			if (mode_need_upgrade == -2) {
				ILI_DBG("mode_need_upgrade is equal to -2, end!\n");
				break;
			} else if (mode_need_upgrade == -1) {
				ILI_ERR("Number of B2 Tag block is invalid, abort!\n");
				return -EINVAL;
			}
			fbi[num].mode |= 0x01 << mode_need_upgrade;
			fbi[num].fix_mem_start_multi_buf[mode_need_upgrade] =
				HexToDec(&phex[i + 9], 6);
			ILI_DBG("Tag 0xB2: change Block[%d] to addr = 0x%x , mode = %d\n",
				num,
				fbi[num].fix_mem_start_multi_buf[mode_need_upgrade],
				b2_hex_mode);
		}

		addr = addr + (ex_addr << 16);

		if (phex[i + 1 + 2 + 4 + 2 + (len * 2) + 2] == 0x0D)
			offset = 2;
		else
			offset = 1;

		if (addr >= MAX_HEX_FILE_SIZE) {
			ILI_ERR("Invalid hex format %d\n", addr);
			return -1;
		}

		if (type == 0x00) {
			end_addr = addr + len;
			if (addr < start_addr)
				start_addr = addr;
			/* fill data */
			for (j = 0, k = 0; j < (len * 2); j += 2, k++)
				pfw[addr + k] = HexToDec(&phex[i + 9 + j], 2);
		}
		i += 1 + 2 + 4 + 2 + (len * 2) + 2 + offset;
	}

	if (ilitek_fw_calc_file_crc(pfw) < 0)
		return -1;

	tfd.start_addr = start_addr;
	tfd.end_addr = end_addr;
	tfd.block_number = block;
	return 0;
}

static int ilitek_tdd_fw_hex_open(u8 op, u8 *pfw)
{
	int ret = 0, fsize = 0;
	const struct firmware *fw = NULL;

	ILI_INFO("Request firmware name is %s\n", ilits->md_fw_rq_path);

	switch (op) {
	case REQUEST_FIRMWARE:
		if (request_firmware(&fw, ilits->md_fw_rq_path, ilits->dev) < 0) {
			ILI_ERR("Request firmware %s failed, try again\n", ilits->md_fw_rq_path);
			if (request_firmware(&fw, ilits->md_fw_rq_path, ilits->dev) < 0) {
				ILI_ERR("Request firmware %s failed, after retryn\n",
					ilits->md_fw_rq_path);
				ret = -1;
				goto out;
			}
		}

		fsize = fw->size;
		ILI_INFO("fsize = %d\n", fsize);
		if (fsize <= 0) {
			ILI_ERR("The size of file is invalid\n");
			release_firmware(fw);
			ret = -1;
			goto out;
		}

		ilits->tp_fw.size = 0;
		ilits->tp_fw.data = vmalloc(fsize);
		if (ERR_ALLOC_MEM(ilits->tp_fw.data)) {
			ILI_ERR("Failed to allocate tp_fw by vmalloc, try again\n");
			ilits->tp_fw.data = vmalloc(fsize);
			if (ERR_ALLOC_MEM(ilits->tp_fw.data)) {
				ILI_ERR("Failed to allocate tp_fw after retry\n");
				release_firmware(fw);
				ret = -ENOMEM;
				goto out;
			}
		}

		/* Copy fw data got from request_firmware to global */
		ipio_memcpy((u8 *)ilits->tp_fw.data, fw->data, fsize * sizeof(*fw->data), fsize);
		ilits->tp_fw.size = fsize;
		release_firmware(fw);
		break;
	default:
		ILI_ERR("Unknown open file method, %d\n", op);
		break;
	}

	if (ERR_ALLOC_MEM(ilits->tp_fw.data) || ilits->tp_fw.size <= 0) {
		ILI_ERR("fw data/size is invalid\n");
		ret = -1;
		goto out;
	}

	/* Convert hex and copy data from tp_fw.data to pfw */
	if (ilitek_tddi_fw_hex_convert((u8 *)ilits->tp_fw.data, ilits->tp_fw.size, pfw) < 0) {
		ILI_ERR("Convert hex file failed\n");
		ret = -1;
	}

out:
	ipio_vfree((void **)&(ilits->tp_fw.data));
	return ret;
}

int ili_fw_upgrade(int op)
{
	int i, ret = 0, retry = 3;
	static bool get_firmware;

	if (!ilits->boot || ilits->force_fw_update || ERR_ALLOC_MEM(pfw)) {
		ilits->gesture_load_code = false;
		get_firmware = false;

		if (ERR_ALLOC_MEM(pfw)) {
			ipio_vfree((void **)&pfw);
			pfw = vmalloc(MAX_HEX_FILE_SIZE * sizeof(u8));
			if (ERR_ALLOC_MEM(pfw)) {
				ILI_ERR("Failed to allocate pfw memory, %ld\n", PTR_ERR(pfw));
				ipio_vfree((void **)&pfw);
				ret = -ENOMEM;
				goto out;
			}
		}

		for (i = 0; i < MAX_HEX_FILE_SIZE; i++)
			pfw[i] = 0xFF;

		if (ilitek_tdd_fw_hex_open(op, pfw) < 0) {
			ILI_ERR("Open hex file fail, try upgrade from ILI file\n");

			/*
			 * Users might not be aware of a broken hex file when recovering
			 * fw from ILI file. We should force them to check
			 * hex files if they attempt to update via device node.
			 */
			if (ilits->node_update) {
				ILI_ERR("Ignore update from ILI file\n");
				ipio_vfree((void **)&pfw);
				return -EFW_CONVERT_FILE;
			}

			if (ilitek_tddi_fw_ili_convert(pfw) < 0) {
				ILI_ERR("Convert ILI file error\n");
				ret = -EFW_CONVERT_FILE;
				goto out;
			}
		}

		if (ilitek_tddi_fw_update_block_info(pfw) < 0) {
			ret = -EFW_CONVERT_FILE;
			goto out;
		}

		if (ilits->chip->core_ver >= CORE_VER_1470 && ilits->rib.nIsHostDownload == 0) {
			ILI_ERR("hex file interface no match error\n");
			return -EFW_INTERFACE;
		}
		get_firmware = true;
	}

	if (!get_firmware) {
		ILI_ERR("Convert ILI file error\n");
		return -EFW_CONVERT_FILE;
	}

#if (ENGINEER_FLOW)
	if (!ilits->eng_flow) {
		do {
			ret = ilitek_tddi_fw_iram_upgrade(pfw, OFF);
			if (ret == UPDATE_PASS)
				break;

			ILI_ERR("Upgrade failed, do retry!\n");
		} while (--retry > 0);

		if (ret != UPDATE_PASS) {
			ILI_ERR("Failed to upgrade fw %d times, erasing iram\n", retry);
			if (ilits->cascade_info_block.nNum != 0) {
				if (ili_cascade_reset_ctrl(ilits->reset, false) < 0)
					ILI_ERR("TP cascade reset failed while erasing data\n");
			} else {
				ili_reset_ctrl(ilits->reset);
			}
			ilits->xch_num = 0;
			ilits->ych_num = 0;
			return ret;
		}
	} else {
		ILI_ERR("eng_flow do reset!\n");
		if (ilits->cascade_info_block.nNum != 0) {
			if (ili_cascade_reset_ctrl(ilits->reset, true) < 0)
				ILI_ERR("TP cascade reset failed while erasing data\n");
		} else {
			ili_reset_ctrl(ilits->reset);
		}
		mdelay(50);
	}
#else
	do {
		ret = ilitek_tddi_fw_iram_upgrade(pfw, OFF);
		if (ret == UPDATE_PASS)
			break;

		ILI_ERR("Upgrade failed, do retry!\n");
	} while (--retry > 0);

	if (ret != UPDATE_PASS) {
		ILI_ERR("Failed to upgrade fw %d times, erasing iram\n", retry);
		if (ilits->cascade_info_block.nNum != 0) {
			if (ili_cascade_reset_ctrl(ilits->reset, false) < 0)
				ILI_ERR("TP cascade reset failed while erasing data\n");
		} else {
			ili_reset_ctrl(ilits->reset);
		}

		ilits->xch_num = 0;
		ilits->ych_num = 0;
		return ret;
	}
#endif
out:
	ili_ic_get_all_info();
	ili_ic_func_ctrl_reset();

	return ret;
}

/* spi.c */
int ili_core_spi_setup(int num)
{
	u32 freq[] = {
		TP_SPI_CLK_1M,
		TP_SPI_CLK_2M,
		TP_SPI_CLK_3M,
		TP_SPI_CLK_4M,
		TP_SPI_CLK_5M,
		TP_SPI_CLK_6M,
		TP_SPI_CLK_7M,
		TP_SPI_CLK_8M,
		TP_SPI_CLK_9M,
		TP_SPI_CLK_10M,
		TP_SPI_CLK_11M,
		TP_SPI_CLK_12M,
		TP_SPI_CLK_13M,
		TP_SPI_CLK_14M,
		TP_SPI_CLK_15M
	};

	if (num >= ARRAY_SIZE(freq)) {
		ILI_ERR("Invalid clk freq, set default clk freq\n");
		num = 7;
	}

	ILI_INFO("spi clock = %d\n", freq[num]);

	ilits->spi->mode = SPI_MODE_0;
	ilits->spi->bits_per_word = 8;
	ilits->spi->max_speed_hz = freq[num];

	if (spi_setup(ilits->spi) < 0) {
		ILI_ERR("Failed to setup spi device\n");
		return -ENODEV;
	}

	ILI_INFO("name = %s, bus_num = %d,cs = %d, mode = %d, speed = %d\n",
			ilits->spi->modalias,
			ilits->spi->master->bus_num,
			ilits->spi->chip_select,
			ilits->spi->mode,
			ilits->spi->max_speed_hz);

	return 0;
}

#define DMA_TRANSFER_MAX_CHUNK 4	/* number of chunks to be transferred. */
#define DMA_TRANSFER_MAX_LEN 4096	/* length of a chunk. */

int ili_spi_write_then_read_split(struct spi_device *spi,
		const void *txbuf, unsigned int n_tx,
		void *rxbuf, unsigned int n_rx)
{
	int status = -1, duplex_len = 0;
	int xfercnt = 0, xferlen = 0, xferloop = 0;
	int offset = 0;
	u8 cmd = 0;
	struct spi_message message;
	struct spi_transfer xfer[DMA_TRANSFER_MAX_CHUNK];

	if (n_rx > SPI_RX_BUF_SIZE) {
		ILI_ERR("Rx length is greater than spi local buf, abort\n");
		status = -ENOMEM;
		goto out;
	}

	spi_message_init(&message);
	memset(xfer, 0, sizeof(xfer));
	memset(ilits->spi_tx, 0x0, SPI_TX_BUF_SIZE);
	memset(ilits->spi_rx, 0x0, SPI_RX_BUF_SIZE);

	if ((n_tx > 0) && (n_rx > 0))
		cmd = SPI_READ;
	else
		cmd = SPI_WRITE;

	switch (cmd) {
	case SPI_WRITE:
		if (n_tx % DMA_TRANSFER_MAX_LEN)
			xferloop = (n_tx / DMA_TRANSFER_MAX_LEN) + 1;
		else
			xferloop = n_tx / DMA_TRANSFER_MAX_LEN;

		if (xferloop > DMA_TRANSFER_MAX_CHUNK) {
			ILI_ERR("xferloop = %d > %d\n", xferloop, DMA_TRANSFER_MAX_CHUNK);
			status = -EINVAL;
			break;
		}

		xferlen = n_tx;
		memcpy(ilits->spi_tx, (u8 *)txbuf, xferlen);

		for (xfercnt = 0; xfercnt < xferloop; xfercnt++) {
			if (xferlen > DMA_TRANSFER_MAX_LEN)
				xferlen = DMA_TRANSFER_MAX_LEN;

			xfer[xfercnt].len = xferlen;
			xfer[xfercnt].tx_buf = ilits->spi_tx + xfercnt * DMA_TRANSFER_MAX_LEN;
			spi_message_add_tail(&xfer[xfercnt], &message);
			xferlen = n_tx - (xfercnt+1) * DMA_TRANSFER_MAX_LEN;
		}
		status = spi_sync(spi, &message);
		break;
	case SPI_READ:
		if (n_tx > DMA_TRANSFER_MAX_LEN) {
			ILI_ERR("Tx length must be lower than dma length (%d).\n",
				DMA_TRANSFER_MAX_LEN);
			status = -EINVAL;
			break;
		}

		if (!atomic_read(&ilits->ice_stat))
			offset = 2;

		memcpy(ilits->spi_tx, txbuf, n_tx);
		duplex_len = n_tx + n_rx + offset;

		if (duplex_len % DMA_TRANSFER_MAX_LEN)
			xferloop = (duplex_len / DMA_TRANSFER_MAX_LEN) + 1;
		else
			xferloop = duplex_len / DMA_TRANSFER_MAX_LEN;

		if (xferloop > DMA_TRANSFER_MAX_CHUNK) {
			ILI_ERR("xferloop = %d > %d\n", xferloop, DMA_TRANSFER_MAX_CHUNK);
			status = -EINVAL;
			break;
		}

		xferlen = duplex_len;
		for (xfercnt = 0; xfercnt < xferloop; xfercnt++) {
			if (xferlen > DMA_TRANSFER_MAX_LEN)
				xferlen = DMA_TRANSFER_MAX_LEN;

			xfer[xfercnt].len = xferlen;
			xfer[xfercnt].tx_buf = ilits->spi_tx;
			xfer[xfercnt].rx_buf = ilits->spi_rx + xfercnt * DMA_TRANSFER_MAX_LEN;
			spi_message_add_tail(&xfer[xfercnt], &message);
			xferlen = duplex_len - (xfercnt + 1) * DMA_TRANSFER_MAX_LEN;
		}

		status = spi_sync(spi, &message);
		if (status == 0) {
			if (ilits->spi_rx[1] != SPI_ACK && !atomic_read(&ilits->ice_stat)) {
				status = DO_SPI_RECOVER;
				ILI_ERR("Do spi recovery: rxbuf[1] = 0x%x, ice = %d\n",
					ilits->spi_rx[1],
					atomic_read(&ilits->ice_stat));
				break;
			}

			memcpy((u8 *)rxbuf, ilits->spi_rx + offset + 1, n_rx);
		} else {
			ILI_ERR("spi read fail, status = %d\n", status);
		}
		break;
	default:
		ILI_INFO("Unknown command 0x%x\n", cmd);
		break;
	}

out:
	return status;
}

static int ili_spi_mp_pre_cmd(u8 cdc)
{
	u8 pre[5] = { 0 };

	if (!atomic_read(&ilits->mp_stat) || cdc != P5_X_SET_CDC_INIT ||
		ilits->chip->core_ver >= CORE_VER_1430)
		return 0;

	ILI_DBG("mp test with pre commands\n");

	pre[0] = SPI_WRITE;
	pre[1] = 0x0;/* dummy byte */
	pre[2] = 0x2;/* Write len byte */
	pre[3] = P5_X_READ_DATA_CTRL;
	pre[4] = P5_X_GET_CDC_DATA;
	if (ilits->spi_write_then_read(ilits->spi, pre, 5, NULL, 0) < 0) {
		ILI_ERR("Failed to write pre commands\n");
		return -1;
	}

	pre[0] = SPI_WRITE;
	pre[1] = 0x0;/* dummy byte */
	pre[2] = 0x1;/* Write len byte*/
	pre[3] = P5_X_GET_CDC_DATA;
	if (ilits->spi_write_then_read(ilits->spi, pre, 4, NULL, 0) < 0) {
		ILI_ERR("Failed to write pre commands\n");
		return -1;
	}
	return 0;
}

static int ili_spi_pll_clk_wakeup(void)
{
	int index = 0;
	u8 wdata[32] = { 0 };
	u8 wakeup[9] = {0xA3, 0xA3, 0xA3, 0xA3, 0xA3, 0xA3, 0xA3, 0xA3, 0xA3};
	u32 wlen = sizeof(wakeup);

	wdata[0] = SPI_WRITE;
	wdata[1] = wlen >> 8;
	wdata[2] = wlen & 0xff;
	index = 3;

	ipio_memcpy(&wdata[index], wakeup, wlen, wlen);

	wlen += index;

	mdelay(2);
	ILI_INFO("Write dummy to wake up spi pll clk\n");
	if (ilits->spi_write_then_read(ilits->spi, wdata, wlen, NULL, 0) < 0) {
		ILI_INFO("spi client write error\n");
		return -1;
	}

	return 0;
}

static int ili_spi_wrapper(u8 *txbuf, u32 wlen, u8 *rxbuf, u32 rlen, bool spi_irq, bool i2c_irq)
{
	int ret = 0;
	int mode = 0, index = 0;
	u8 wdata[128] = { 0 };
	u8 checksum = 0;
	bool ice = atomic_read(&ilits->ice_stat);

	if (wlen > 0) {
		if (!txbuf) {
			ILI_ERR("txbuf is null\n");
			return -ENOMEM;
		}

		/* 3 bytes data consist of length and header */
		if (((wlen + 4) > sizeof(wdata)) || (wlen > (UINT_MAX - 4))) {
			ILI_ERR("WARNING! wlen(%d) > wdata(%d), using wdata length to transfer\n",
				wlen,
				(int)sizeof(wdata));
			wlen = sizeof(wdata) - 4;
		}
	}

	if (rlen > 0) {
		if (!rxbuf) {
			ILI_ERR("rxbuf is null\n");
			return -ENOMEM;
		}
	}

	if (rlen > 0 && !wlen)
		mode = SPI_READ;
	else
		mode = SPI_WRITE;

	if (ilits->int_pulse)
		ilits->detect_int_stat = ili_ic_check_int_pulse;

	if (spi_irq)
		atomic_set(&ilits->cmd_int_check, ENABLE);

	switch (mode) {
	case SPI_WRITE:
		if (!ice && (rlen > 0))
			atomic_set(&ilits->ignore_report, START);
#if (PLL_CLK_WAKEUP_TP_RESUME == ENABLE)
		if (ilits->pll_clk_wakeup == true) {
#else
		if ((ilits->pll_clk_wakeup == true) && (ilits->tp_suspend == true)) {
#endif
			ret = ili_spi_pll_clk_wakeup();
			if (ret < 0) {
				ILI_ERR("Wakeup pll clk error\n");
				break;
			}
		}
		if (ice) {
#if ENABLE_SPICASCADE_V2
			if (ilits->spi_ms_mode == CLIENT)
				wdata[0] = SPI_WRITE_CLIENT0;
			else
				wdata[0] = SPI_WRITE;
#else
			wdata[0] = SPI_WRITE;
#endif
			index = 1;
		} else {
			wdata[0] = SPI_WRITE;
			wdata[1] = wlen >> 8;
			wdata[2] = wlen & 0xff;
			index = 3;
		}

		ipio_memcpy(&wdata[index], txbuf, wlen, wlen);

		wlen += index;

		/*
		 * NOTE: If TP driver is doing MP test and commanding 0xF1 to FW,
		 * we add a checksum to the last index and plus 1 with size.
		 */
		if (atomic_read(&ilits->mp_stat) && wdata[index] == P5_X_SET_CDC_INIT) {
			checksum = ili_calc_packet_checksum(&wdata[index], wlen - index);
			wdata[wlen] = checksum;
			wlen++;
			wdata[1] = (wlen - index) >> 8;
			wdata[2] = (wlen - index) & 0xff;
			print_hex_dump(KERN_INFO,
						"mp cdc cmd with checksum: ",
						DUMP_PREFIX_OFFSET,
						16,
						1,
						wdata,
						wlen,
						false);
		}

		if (!ice) {
			ILI_DBG("send cmd delay 2ms\n");
			mdelay(2);
		}
		ret = ilits->spi_write_then_read(ilits->spi, wdata, wlen, txbuf, 0);

		if (ret < 0) {
			ILI_INFO("spi-wrapper write error\n");
			break;
		}

		/* Won't break if it needs to read data following with writing. */
		if (!rlen)
			break;

		if (!ice && spi_irq) {
			/* Check INT triggered by FW when sending cmds. */
			if (ilits->detect_int_stat(false) < 0) {
				ILI_ERR("ERROR! Check INT timeout\n");
				ret = -ETIME;
				break;
			}
		}

		ret = ili_spi_mp_pre_cmd(wdata[3]);
		if (ret < 0)
			ILI_ERR("spi-wrapper mp pre cmd error\n");

		mdelay(2);
		wdata[0] = SPI_READ;
		ret = ilits->spi_write_then_read(ilits->spi, wdata, 1, rxbuf, rlen);
		if (ret < 0)
			ILI_ERR("spi-wrapper read error\n");

		break;
	case SPI_READ:
		if (!ice && spi_irq) {
			/* Check INT triggered by FW when sending cmds. */
			if (ilits->detect_int_stat(false) < 0) {
				ILI_ERR("ERROR! Check INT timeout\n");
				ret = -ETIME;
				break;
			}
		}

		ret = ili_spi_mp_pre_cmd(wdata[3]);
		if (ret < 0)
			ILI_ERR("spi-wrapper mp pre cmd error\n");

		wdata[0] = SPI_READ;

		ret = ilits->spi_write_then_read(ilits->spi, wdata, 1, rxbuf, rlen);
		if (ret < 0)
			ILI_ERR("spi-wrapper read error\n");

		break;
	default:
		ILI_ERR("Unknown spi mode (%d)\n", mode);
		ret = -EINVAL;
		break;
	}

	atomic_set(&ilits->ignore_report, END);

	if (spi_irq)
		atomic_set(&ilits->cmd_int_check, DISABLE);

	return ret;
}

int ilitek_spi_probe(struct spi_device *client)
{
	int ret = 0;
	struct spi_device *spi = client;

	ILI_INFO("ilitek spi probe\n");

	if (!spi) {
		ILI_ERR("spi device is NULL\n");
		return -ENODEV;
	}

	ilits->update_buf = kzalloc(MAX_HEX_FILE_SIZE, GFP_KERNEL | GFP_DMA);
	if (ERR_ALLOC_MEM(ilits->update_buf)) {
		ILI_ERR("fw kzalloc error\n");
		goto err_update_buf;
	}

	/* Used for receiving touch data only, do not mix up with others. */
	ilits->tr_buf = kzalloc(TR_BUF_SIZE, GFP_ATOMIC);
	if (ERR_ALLOC_MEM(ilits->tr_buf)) {
		ILI_ERR("failed to allocate touch report buffer\n");
		goto err_platform_trbuf;
	}

	ilits->spi_tx = kzalloc(SPI_TX_BUF_SIZE, GFP_KERNEL | GFP_DMA);
	if (ERR_ALLOC_MEM(ilits->spi_tx)) {
		ILI_ERR("Failed to allocate spi tx buffer\n");
		goto err_platform_spitx;
	}

	ilits->spi_rx = kzalloc(SPI_RX_BUF_SIZE, GFP_KERNEL | GFP_DMA);
	if (ERR_ALLOC_MEM(ilits->spi_rx)) {
		ILI_ERR("Failed to allocate spi rx buffer\n");
		goto err_platform_spirx;
	}

	ilits->spi = spi;
	ilits->dev = &spi->dev;
	ilits->wrapper = ili_spi_wrapper;
	ilits->detect_int_stat = ili_ic_check_int_pulse;
	ilits->int_pulse = true;
	ilits->spi_write_then_read = ili_spi_write_then_read_split;

	ilits->actual_tp_mode = P5_X_FW_AP_MODE;
	ilits->tp_data_format = DATA_FORMAT_DEMO;

	if (TDDI_RST_BIND)
		ilits->reset = TP_IC_WHOLE_RST_WITH_FLASH;
	else
		ilits->reset = TP_HW_RST_ONLY;

	ilits->rst_edge_delay = 10;
	ilits->fw_open = REQUEST_FIRMWARE;
	ilits->gesture_mode = DATA_FORMAT_GESTURE_INFO;
	ilits->wait_int_timeout = AP_INT_TIMEOUT;
	ilits->ice_mode_ctrl = ili_ice_mode_ctrl_by_mode_spi;
	ilits->cascade_info_block.nNum = 0;
#if ENABLE_CASCADE
	ilits->spi_ms_mode = MASTER;
	ilits->cascade_info_block.nNum = 2;
	ilits->skip_sync_cmd = DISABLE;
#endif

	if (ili_core_spi_setup(SPI_CLK) < 0)
		return -EINVAL;

	ret = ilitek_plat_probe();
	if (ret < 0) {
		ILI_ERR("plat probe fail,ret = %d.\n", ret);
		goto err_platform_probe;
	}

	return ret;

err_platform_probe:
	kfree(ilits->spi_rx);
	ilits->spi_rx = NULL;
err_platform_spirx:
	kfree(ilits->spi_tx);
	ilits->spi_tx = NULL;
err_platform_spitx:
	kfree(ilits->tr_buf);
	ilits->tr_buf = NULL;
err_platform_trbuf:
	kfree(ilits->update_buf);
	ilits->update_buf = NULL;
err_update_buf:
	if (ilits) {
		devm_kfree(&spi->dev, ilits);
		ilits = NULL;
	}
	return -ENOMEM;
}

void ilitek_spi_remove(struct spi_device *spi)
{
	ILI_INFO("ilitek spi remove");

	kfree(ilits->spi_rx);
	ilits->spi_rx = NULL;
	kfree(ilits->spi_tx);
	ilits->spi_tx = NULL;
	kfree(ilits->tr_buf);
	ilits->tr_buf = NULL;
	kfree(ilits->update_buf);
	ilits->update_buf = NULL;

	if (ilits) {
		devm_kfree(&spi->dev, ilits);
		ilits = NULL;
	}
}

static int ili_tp_data_mode_ctrl(u8 *cmd)
{
	int ret = 0;

	ILI_INFO("cmd = %d\n", cmd[0]);

	switch (cmd[0]) {
	case AP_MODE:
		if (ilits->actual_tp_mode == P5_X_FW_TEST_MODE) {
			if (ili_switch_tp_mode(P5_X_FW_AP_MODE) < 0) {
				ILI_ERR("Failed to switch demo mode\n");
				ret = -ENOTTY;
			}
		} else {
			if (ili_set_tp_data_len(DATA_FORMAT_DEMO, false, &cmd[1]) < 0) {
				ILI_ERR("Failed to switch demo mode\n");
				ret = -ENOTTY;
			}
		}
		break;
	case TEST_MODE:
		if (ili_switch_tp_mode(P5_X_FW_TEST_MODE) < 0) {
			ILI_ERR("Failed to switch test mode\n");
			ret = -ENOTTY;
		}
		break;
	default:
		ILI_ERR("Unknown TP mode ctrl\n");
		ret = -ENOTTY;
		break;
	}
	ilits->tp_data_mode = cmd[0];

	return ret;
}

static long ilitek_node_ioctl(struct file *filp, unsigned int cmd,
			unsigned long arg)
{
	int ret = 0;
	u8 *szBuf = NULL;
	static u16 i2c_rw_length;
	u8 *wrap_rbuf = NULL, wrap_int = 0;
	u16 wrap_wlen = 0, wrap_rlen = 0;
	bool spi_irq = false, i2c_irq = false;

	if (atomic_read(&ilits->tp_reset) == START) {
		ILI_ERR("ignore request! tp reset atomic is START.\n");
		return -EINVAL;
	}

	if (_IOC_TYPE(cmd) != ILITEK_IOCTL_MAGIC) {
		ILI_ERR("The Magic number doesn't match\n");
		return -ENOTTY;
	}

	if (_IOC_NR(cmd) > ILITEK_IOCTL_MAXNR) {
		ILI_ERR("The number of ioctl doesn't match\n");
		return -ENOTTY;
	}

	ILI_DBG("cmd = %d\n", _IOC_NR(cmd));

	szBuf = kcalloc(IOCTL_I2C_BUFF, sizeof(u8), GFP_KERNEL);
	if (ERR_ALLOC_MEM(szBuf)) {
		ILI_ERR("Failed to allocate mem\n");
		ret = -ENOMEM;
		goto out;
	}
	switch (cmd) {
	case ILITEK_IOCTL_I2C_SET_WRITE_LENGTH:
	case ILITEK_IOCTL_I2C_SET_READ_LENGTH:
		i2c_rw_length = arg;
		ILI_INFO("i2c_rw_length=%d\n", i2c_rw_length);
		break;
	case ILITEK_IOCTL_TP_MODE_CTRL:
		if (copy_from_user(szBuf, (u8 *) arg, 12)) {
			ILI_ERR("Failed to copy data from user space\n");
			ret = -ENOTTY;
			break;
		}
		ILI_DBG("ioctl: switch fw format = %d\n", szBuf[0]);
		ret = ili_tp_data_mode_ctrl(szBuf);
		break;
	case ILITEK_IOCTL_WRAPPER_RW:
		ILI_DBG("ioctl: wrapper rw\n");

		if (i2c_rw_length > IOCTL_I2C_BUFF || i2c_rw_length == 0) {
			ILI_ERR("ERROR! i2c_rw_length is invalid\n");
			ret = -ENOTTY;
			break;
		}

		if (copy_from_user(szBuf, (u8 *)arg, i2c_rw_length)) {
			ILI_ERR("Failed to copy data from user space\n");
			ret = -ENOTTY;
			break;
		}

		wrap_int = szBuf[0];
		wrap_rlen = (szBuf[1] << 8) | szBuf[2];
		wrap_wlen = (szBuf[3] << 8) | szBuf[4];

		ILI_DBG("wrap_int = %d, wrap_rlen = %d, wrap_wlen = %d\n",
					wrap_int, wrap_rlen, wrap_wlen);

		if (wrap_wlen > IOCTL_I2C_BUFF || wrap_rlen > IOCTL_I2C_BUFF) {
			ILI_ERR("ERROR! R/W len is largn than ioctl buf\n");
			ret = -ENOTTY;
			break;
		}

		if (wrap_rlen > 0) {
			wrap_rbuf =
				kcalloc(IOCTL_I2C_BUFF, sizeof(u8), GFP_KERNEL);
			if (ERR_ALLOC_MEM(wrap_rbuf)) {
				ILI_ERR("Failed to allocate mem\n");
				ret = -ENOMEM;
				break;
			}
		}

		if (wrap_int == 1) {
			i2c_irq = ON;
			spi_irq = ON;
		} else if (wrap_int == 2) {
			i2c_irq = OFF;
			spi_irq = OFF;
		} else {
			i2c_irq = OFF;
			spi_irq = (wrap_rlen > 0 ? ON : OFF);
		}

		ILI_DBG("i2c_irq = %d, spi_irq = %d\n", i2c_irq, spi_irq);

		ilits->wrapper(szBuf + 5, wrap_wlen, wrap_rbuf, wrap_rlen,
					spi_irq, i2c_irq);

		print_hex_dump(KERN_INFO, "wrap_wbuf: ",
			DUMP_PREFIX_OFFSET,
			16,
			1,
			szBuf + 5,
			wrap_wlen,
			false);
		print_hex_dump(KERN_INFO, "wrap_rbuf: ",
			DUMP_PREFIX_OFFSET,
			16,
			1,
			wrap_rbuf,
			wrap_rlen,
			false);

		if (copy_to_user((u8 *)arg, wrap_rbuf, wrap_rlen)) {
			ILI_ERR("Failed to copy driver ver to user space\n");
			ret = -ENOTTY;
		}
		break;
	case ILITEK_IOCTL_INTERFACE_GET:
		szBuf[0] = 0x01;
		ILI_INFO("ioctl: get interface is %d\n", szBuf[0]);
		if (copy_to_user((u8 *) arg, szBuf, 1)) {
			ILI_ERR("Failed to copy data to user space\n");
			ret = -ENOTTY;
		}
		break;
	default:
		ret = -ENOTTY;
		break;
	}

	ipio_kfree((void **)&szBuf);
	ipio_kfree((void **)&wrap_rbuf);

out:
	return ret;
}

static struct proc_dir_entry *proc_dir_ilitek;

struct ilitek_proc_node  {
	char *name;
	struct proc_dir_entry *node;
	const struct proc_ops *fops;
	bool isCreated;
};

static const struct proc_ops proc_ioctl_fops = {
		.proc_ioctl = ilitek_node_ioctl,
		.proc_lseek = default_llseek,
};

static struct ilitek_proc_node iliproc[] = {
	{ "ioctl", NULL, &proc_ioctl_fops, false },
};

void ili_node_init(void)
{
	int i = 0;

	proc_dir_ilitek = proc_mkdir("ilitek", NULL);

	for (; i < ARRAY_SIZE(iliproc); i++) {
		iliproc[i].node = proc_create(
								iliproc[i].name,
								0644,
								proc_dir_ilitek,
								iliproc[i].fops);

		if (iliproc[i].node == NULL) {
			iliproc[i].isCreated = false;
			ILI_ERR("Failed to create %s under /proc\n", iliproc[i].name);
		} else {
			iliproc[i].isCreated = true;
			ILI_INFO("Succeed to create %s under /proc\n", iliproc[i].name);
		}
	}
}

void ili_node_remove(struct ilitek_ts_hid_data *ts)
{
	int i = 0;

	/* Remove nodes under /proc/ilitek */
	for (; i < ARRAY_SIZE(iliproc); i++) {
		if (iliproc[i].isCreated && iliproc[i].node) {
			proc_remove(iliproc[i].node);
			iliproc[i].node = NULL;
			iliproc[i].isCreated = false;
		}
	}

	if (proc_dir_ilitek) {
		proc_remove(proc_dir_ilitek);
		proc_dir_ilitek = NULL;
	}

	ILI_INFO("Remove TP filesystem node.\n");
}

/* core.c */
void ili_tp_reset(void)
{
	int rst_edge_delay = 0;

	if (ilits->fast_enter_ice_mode) {
		rst_edge_delay = ilits->rst_edge_delay;
		ilits->rst_edge_delay = EDGE_DELAY_FOR_FAST_ENTER_ICE;
	}

	ILI_INFO("Reset edge delay = %d ms\n", ilits->rst_edge_delay);

	/* Reset sequence: HIGH LOW HIGH */
	gpiod_direction_output(ilits->reset_gpiod, 1);
	mdelay(1);
	gpiod_set_value(ilits->reset_gpiod, 0);
	mdelay(5);
	gpiod_set_value(ilits->reset_gpiod, 1);
	mdelay(ilits->rst_edge_delay);
	if (ilits->fast_enter_ice_mode)
		ilits->rst_edge_delay = rst_edge_delay;
}

int ilitek_plat_gpio_register(void)
{
	struct device *dev = ilits->dev;
	struct gpio_desc *reset_gpiod;

	reset_gpiod = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(reset_gpiod)) {
		ILI_ERR("Failed to get reset-gpios: %ld\n", PTR_ERR(reset_gpiod));
		return PTR_ERR(reset_gpiod);
	}

	ilits->reset_gpiod = reset_gpiod;

	return 0;
}

int ilitek_plat_probe(void)
{
	ILI_INFO("platform probe\n");

	if (ilitek_plat_gpio_register() < 0) {
		ILI_ERR("Register gpio failed\n");
		return -ENODEV;
	}

	if (ili_tddi_init() < 0) {
		ILI_ERR("ILITEK Driver probe failed\n");
		return -ENODEV;
	}

	ILI_INFO("ILITEK Driver loaded successfully!\n");
	return 0;
}

int ilitek_plat_remove(void)
{
	ILI_INFO("remove plat dev\n");

	ili_dev_remove();

	ilitek_spi_remove(ilits->spi);

	return 0;
}

int ilitek_plat_dev_init(struct spi_device *client)
{
	ILI_INFO("ILITEK TP spi driver init\n");
	if (ilitek_spi_probe(client) < 0) {
		ILI_ERR("ILITEK TP spi driver probe fail\n");
		return -ENODEV;
	}
	return 0;
}

u8 ili_calc_packet_checksum(u8 *packet, int len)
{
	int i;
	s32 sum = 0;

	for (i = 0; i < len; i++)
		sum += packet[i];

	return (u8)((-sum) & 0xFF);
}

int ili_switch_tp_mode(u8 mode)
{
	int ret = 0;
	struct spi_hid_of_ilitek_config *config = &ilits->config;

	atomic_set(&ilits->tp_sw_mode, START);

	ilits->actual_tp_mode = mode;

	switch (ilits->actual_tp_mode) {
	case P5_X_FW_AP_MODE:
		ILI_INFO("Switch to AP mode\n");
		ilits->wait_int_timeout = AP_INT_TIMEOUT;
		ret = ili_fw_upgrade_handler(NULL);
		WRITE_ONCE(config->ops.response_timeout_ms, AP_INT_TIMEOUT);
		ILI_INFO("response_timeout_ms = %u\n", READ_ONCE(config->ops.response_timeout_ms));
		break;
	case P5_X_FW_TEST_MODE:
		ILI_INFO("Switch to MP mode\n");
		ilits->wait_int_timeout = MP_INT_TIMEOUT;
		WRITE_ONCE(config->ops.response_timeout_ms, MP_INT_TIMEOUT);
		ILI_INFO("response_timeout_ms = %u\n", READ_ONCE(config->ops.response_timeout_ms));
		ret = ili_fw_upgrade_handler(NULL);
		break;
	default:
		ILI_ERR("Unknown TP mode: %x\n", mode);
		ret = -1;
		break;
	}

	if (ret < 0)
		ILI_ERR("Switch TP mode (%d) failed\n", mode);

	ILI_DBG("Actual TP mode = %d\n", ilits->actual_tp_mode);
	atomic_set(&ilits->tp_sw_mode, END);
	return ret;
}

int ili_fw_upgrade_handler(void *data)
{
	int ret = 0;

	atomic_set(&ilits->fw_stat, START);

	ilits->fw_update_stat = FW_STAT_INIT;
	__pm_stay_awake(ilits->ili_upg_wakelock);

	ret = ili_fw_upgrade(ilits->fw_open);
	if (ret != 0) {
		ILI_ERR("FW upgrade fail\n");
		ilits->fw_update_stat = FW_UPDATE_FAIL;
	} else {
		ILI_INFO("FW upgrade pass\n");
		ilits->fw_update_stat = FW_UPDATE_PASS;
	}

	__pm_relax(ilits->ili_upg_wakelock);

	atomic_set(&ilits->fw_stat, END);
	if (!ilits->boot)
		ilits->boot = true;
	return ret;
}

int ili_set_tp_data_len(int format, bool send, u8 *data)
{
	u8 cmd[12] = {0}, ctrl = 0, data_type = 0;
	int ret = 0, tp_mode = ilits->actual_tp_mode;

	if (data == NULL) {
		data_type = P5_X_FW_SIGNAL_DATA_MODE;
		ILI_ERR("Data Type is Null, Set Single data type\n");
	} else {
		data_type = data[0];
		ILI_INFO("Set data type = 0x%X\n", data[0]);
	}

	switch (format) {
	case DATA_FORMAT_DEMO:
		ctrl = DATA_FORMAT_DEMO_CMD;
		break;
	default:
		ILI_ERR("Unknown TP data format\n");
		return -1;
	}

	if ((atomic_read(&ilits->tp_reset) == END) &&
		   (format == DATA_FORMAT_DEMO)) {
		if (ilits->chip->core_ver >= CORE_VER_1700) {
			cmd[0] = P5_X_NEW_CONTROL_FORMAT;
			cmd[1] = ctrl;
			cmd[2] = data_type;
			ret = ilits->wrapper(cmd, 3, NULL, 0, ON, OFF);
		} else {
			cmd[0] = P5_X_MODE_CONTROL;
			cmd[1] = ctrl;
			ret = ilits->wrapper(cmd, 2, NULL, 0, ON, OFF);
		}

		if ((ret < 0) &&
			!(atomic_read(&ilits->fw_stat) || atomic_read(&ilits->tp_sw_mode))) {
			ILI_ERR("switch to format %d failed\n", format);
			ili_switch_tp_mode(P5_X_FW_AP_MODE);
		}
	}

	ilits->tp_data_format = format;
	ILI_INFO("TP mode = %d, format = %d\n", tp_mode, ilits->tp_data_format);

	if (ilits->PenType == POSITION_PEN_TYPE_ON) {
		ILI_INFO("Pen Type = 0x%X, Max Touch Num = %d, Pen Data Mode = %d\n",
			ilits->pen_info.report_type, ilits->touch_num,
			ilits->pen_info.pen_data_mode);
	}
	return ret;
}

int ili_cascade_reset_ctrl(int reset_mode, bool enter_ice)
{
	int ret = 0;

	if (ili_reset_ctrl(reset_mode) < 0) {
		ILI_ERR("TP Reset failed during init\n");
		ret = -EFW_REST;
	}

	if (reset_mode == TP_IC_CODE_RST)
		ilits->ice_mode_ctrl(DISABLE, OFF, BOTH);

	/* No need start sync cmd after reset. */
	atomic_set(&ilits->stop_sync_stat, DISABLE);
	ILI_DBG("Set atomic sync stat = %d\n", atomic_read(&ilits->stop_sync_stat));

	return ret;
}

int ili_reset_ctrl(int mode)
{
	int ret = 0;
	u8 data_type = P5_X_FW_SIGNAL_DATA_MODE;

	atomic_set(&ilits->tp_reset, START);

	switch (mode) {
	case TP_IC_CODE_RST:
		ILI_INFO("TP IC Code RST\n");
		ret = ili_ic_code_reset(OFF);
		ilits->pll_clk_wakeup = false;
		if (ret < 0)
			ILI_ERR("IC Code reset failed\n");
		break;
	case TP_IC_WHOLE_RST_WITH_FLASH:
		ILI_INFO("TP IC whole RST\n");
		ret = ili_ic_whole_reset(OFF, ON);
		if (ret < 0)
			ILI_ERR("IC whole reset failed\n");
		ilits->pll_clk_wakeup = true;
		break;
	case TP_IC_WHOLE_RST_WITHOUT_FLASH:
		ILI_INFO("TP IC whole RST without flash\n");
		ret = ili_ic_whole_reset(OFF, OFF);
		if (ret < 0)
			ILI_ERR("IC whole reset without flash failed\n");
		ilits->pll_clk_wakeup = true;
		break;
	case TP_HW_RST_ONLY:
		ILI_INFO("TP HW RST\n");
		ili_tp_reset();
		ilits->pll_clk_wakeup = true;
		break;
	default:
		ILI_ERR("Unknown reset mode, %d\n", mode);
		ret = -EINVAL;
		break;
	}
	/*
	 * Since OTP must be following with reset, except for code rest,
	 * the stat of ice mode should be set as 0.
	 */
	if (mode != TP_IC_CODE_RST)
		atomic_set(&ilits->ice_stat, DISABLE);

	if (ilits->fast_enter_ice_mode) {
		ilits->fast_enter_ice_mode = false;
	} else {
		if (ili_set_tp_data_len(DATA_FORMAT_DEMO, false, &data_type) < 0)
			ILI_ERR("Failed to set tp data length\n");
	}

	atomic_set(&ilits->tp_reset, END);
	return ret;
}

static void ili_update_tp_module_info(void)
{
	int module = 0;

	ilits->md_name = "DEF";
	ilits->md_fw_rq_path = DEF_FW_REQUEST_PATH;
	ilits->md_fw_ili = CTPM_FW_DEF;
	ilits->md_fw_ili_size = sizeof(CTPM_FW_DEF);

	ILI_INFO("Found %s module: fw path = (%s, %d)\n",
		 ilits->md_name, ilits->md_fw_rq_path, ilits->md_fw_ili_size);

	ilits->tp_module = module;
}

int ili_tddi_init(void)
{
	int ret = 0, retry = ENTER_ICE_MODE_RETRY_COUNT;
#if !(BOOT_FW_UPDATE | HOST_DOWN_LOAD)
	u8 data_type = P5_X_FW_SIGNAL_DATA_MODE;
#endif

	ILI_INFO("driver version = %s\n", DRIVER_VERSION);

	init_waitqueue_head(&(ilits->inq));
	spin_lock_init(&ilits->irq_spin);

	atomic_set(&ilits->ice_stat, DISABLE);
	atomic_set(&ilits->tp_reset, END);
	atomic_set(&ilits->fw_stat, END);
	atomic_set(&ilits->mp_stat, DISABLE);
	atomic_set(&ilits->cmd_int_check, DISABLE);
	atomic_set(&ilits->tp_sw_mode, END);
	atomic_set(&ilits->ignore_report, END);
	atomic_set(&ilits->irq_stat, ENABLE);
	atomic_set(&ilits->spi_slave_write_mcu_on, ENABLE);

	ili_ic_init();

	ilits->tp_data_format = DATA_FORMAT_DEMO;
	ilits->boot = false;
	ilits->PenType = POSITION_PEN_TYPE_OFF;

	/* Must do hw reset once in first time for work normally if tp reset is available */
	/* Ensure to fw not running */
	ilits->fast_enter_ice_mode = true;
	if (ilits->cascade_info_block.nNum != 0) {
		if (ili_cascade_reset_ctrl(ilits->reset, true) < 0)
			ILI_ERR("TP Reset failed during init\n");
	} else {
		if (ili_reset_ctrl(ilits->reset) < 0)
			ILI_ERR("TP Reset failed during init\n");
	}

	/*
	 * This status of ice enable will be reset until process of fw upgrade runs.
	 * it might cause unknown problems if we disable ice mode without any
	 * codes inside touch ic.
	 */
	do {
		if (ilits->ice_mode_ctrl(ENABLE, OFF, BOTH) < 0)
			ILI_ERR("Failed to enable ice mode during %s\n", __func__);

		ret = ili_ic_dummy_check();
		if (ret == 0)
			break;

		if (ilits->ice_mode_ctrl(DISABLE, OFF, BOTH) < 0)
			ILI_ERR("Failed to disable ice mode during %s\n", __func__);

		msleep(1000); //Add 1-second retry delay to avoid 4-second WDT conflict

		retry--;
		ILI_INFO("IC dummy check retry = %d\n", retry);
	} while (retry > 0);

	if (retry <= 0) {
		ILI_ERR("Not found ilitek chip\n");
		return -ENODEV;
	}

#if ENABLE_CASCADE
	if (ili_cascade_ic_get_info(false, false, false, false) < 0)
		ILI_ERR("Client Chip info is incorrect\n");
#else
	if (ili_ic_get_info() < 0)
		ILI_ERR("Chip info is incorrect\n");
#endif

	ili_update_tp_module_info();

	ili_node_init();

	ilits->ili_upg_wakelock = wakeup_source_register(NULL, "ili_upg_wakelock");
	if (!ilits->ili_upg_wakelock)
		ILI_ERR("wakeup source request failed\n");

	return 0;
}

void ili_dev_remove(void)
{
	ILI_INFO("remove ilitek dev\n");

	if (!ilits)
		return;

	ili_node_remove(ilits);

	if (ilits->ili_upg_wakelock) {
		wakeup_source_unregister(ilits->ili_upg_wakelock);
		ilits->ili_upg_wakelock = NULL;
	}
}

static int spi_hid_of_ilitek_populate_config(struct spi_hid_of_ilitek_config *conf,
				      struct device *dev)
{
	int ret;
	u32 val;

	ret = device_property_read_u32(dev, "input-report-header-address",
									&val);
	if (ret) {
		dev_err(dev, "Input report header address not provided.");
		return -ENODEV;
	}
	conf->property_conf.input_report_header_address = val;

	ret = device_property_read_u32(dev, "input-report-body-address", &val);
	if (ret) {
		dev_err(dev, "Input report body address not provided.");
		return -ENODEV;
	}
	conf->property_conf.input_report_body_address = val;

	ret = device_property_read_u32(dev, "output-report-address", &val);
	if (ret) {
		dev_err(dev, "Output report address not provided.");
		return -ENODEV;
	}
	conf->property_conf.output_report_address = val;

	ret = device_property_read_u32(dev, "read-opcode", &val);
	if (ret) {
		dev_err(dev, "Read opcode not provided.");
		return -ENODEV;
	}
	conf->property_conf.read_opcode = val;

	ret = device_property_read_u32(dev, "write-opcode", &val);
	if (ret) {
		dev_err(dev, "Write opcode not provided.");
		return -ENODEV;
	}
	conf->property_conf.write_opcode = val;

	ret = device_property_read_u32(dev, "post-power-on-delay-ms", &val);
	if (ret) {
		dev_err(dev, "post-power-on-delay-ms not provided, using 10.");
		val = 10;
	}
	conf->post_power_on_delay_ms = val;

	ret = device_property_read_u32(dev, "minimal-reset-delay-ms", &val);
	if (ret) {
		dev_err(dev, "minimal-reset-delay-ms not provided, using 100.");
		val = 100;
	}
	conf->minimal_reset_delay_ms = val;

	/* FIXME: not reading hid-over-spi-flags, multi-SPI not supported */

	conf->supply = devm_regulator_get_optional(dev, "vdd");
	if (IS_ERR(conf->supply)) {
		if (PTR_ERR(conf->supply) == -ENODEV) {
			/* No regulator found; assume the power supply is always on. */
			conf->supply = NULL;
			conf->supply_enabled = true;
			dev_info(dev, "No vdd regulator, assume always powered.\n");
		} else {
			dev_err(dev, "Failed to get regulator: %ld\n",
					PTR_ERR(conf->supply));
			return PTR_ERR(conf->supply);
		}
	} else {
		conf->supply_enabled = false;
	}

	conf->reset_gpio = ilits->reset_gpiod;

	return 0;
}

static int spi_hid_of_ilitek_power_down(struct spihid_ops *ops)
{
	struct spi_hid_of_ilitek_config *conf = container_of(ops,
						      struct spi_hid_of_ilitek_config,
						      ops);
	int ret;

	if (!conf->supply_enabled)
		return 0;

	if (conf->supply) {
		ret = regulator_disable(conf->supply);
		if (ret == 0)
			conf->supply_enabled = false;
		return ret;
	}
	return 0;
}

static int spi_hid_of_ilitek_power_up(struct spihid_ops *ops)
{
	struct spi_hid_of_ilitek_config *conf = container_of(ops,
						      struct spi_hid_of_ilitek_config,
						      ops);
	int ret;

	if (conf->supply_enabled)
		return 0;

	ret = regulator_enable(conf->supply);

	if (ret == 0)
		conf->supply_enabled = true;

	usleep_range(1000 * conf->post_power_on_delay_ms,
			1000 * (conf->post_power_on_delay_ms + 1));

	return ret;
}

static int device_reset_request(struct spihid_ops *ops)
{
	struct spi_hid_of_ilitek_config *config =
		container_of(ops, struct spi_hid_of_ilitek_config, ops);
	struct device *dev = &ilits->spi->dev;
	struct spi_transfer transfer;
	struct spi_message message;
	u8 buf[8] = { 0 };
	int ret = 0;

	buf[0] = (config->property_conf.write_opcode);
	buf[1] = (config->property_conf.output_report_address >> 16) & 0xff;
	buf[2] = (config->property_conf.output_report_address >> 8) & 0xff;
	buf[3] = (config->property_conf.output_report_address >> 0) & 0xff;
	buf[4] = 0xFF;
	buf[5] = 0x0;
	buf[6] = 0x0;
	buf[7] = 0x0;

	memset(&transfer, 0, sizeof(transfer));
	transfer.tx_buf = buf;
	transfer.len = sizeof(buf);

	spi_message_init_with_transfers(&message, &transfer, 1);

	ret = spi_sync(ilits->spi, &message);
	if (ret)
		dev_err(dev, "%s: failed to transfer\n", __func__);

	return ret;
}

static int spi_hid_of_ilitek_assert_reset(struct spihid_ops *ops)
{
	gpiod_set_value(ilits->reset_gpiod, 0);
	return 0;
}

static int spi_hid_of_ilitek_deassert_reset(struct spihid_ops *ops)
{
	int ret = 0;

	ilits->actual_tp_mode = P5_X_FW_AP_MODE;

	if (ili_fw_upgrade_handler(NULL) < 0)
		ILI_ERR("Upgrade firmware fail");

	ret = device_reset_request(ops);
	if (ret) {
		ILI_ERR("Call device_reset_request fail\n");
		return ret;
	}

	return 0;
}

static int spi_hid_of_ilitek_plat_init(struct spihid_ops *ops)
{
	int ret;
	struct spi_hid_of_ilitek_config *conf = container_of(ops,
							struct spi_hid_of_ilitek_config,
							ops);
	ret = ilitek_plat_dev_init(conf->spi);
	if (ret < 0) {
		ILI_ERR("Ilitek spi plat probe error, remove registered resources\n");
		goto err;
	}

	return 0;
err:
	ilitek_plat_remove();
	return ret;
}

static void spi_hid_of_ilitek_sleep_minimal_reset_delay(struct spihid_ops *ops)
{
	struct spi_hid_of_ilitek_config *conf = container_of(ops,
						      struct spi_hid_of_ilitek_config,
						      ops);
	usleep_range(1000 * conf->minimal_reset_delay_ms,
			1000 * (conf->minimal_reset_delay_ms + 1));
}

static int spi_hid_of_delayed_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	int ret;
	struct spi_hid_of_ilitek_config *config = &ilits->config;

	config->spi = spi;
	config->ops.power_up = spi_hid_of_ilitek_power_up;
	config->ops.power_down = spi_hid_of_ilitek_power_down;
	config->ops.assert_reset = spi_hid_of_ilitek_assert_reset;
	config->ops.deassert_reset = spi_hid_of_ilitek_deassert_reset;
	config->ops.sleep_minimal_reset_delay = spi_hid_of_ilitek_sleep_minimal_reset_delay;
	config->ops.plat_init = spi_hid_of_ilitek_plat_init;
	config->ops.response_timeout_ms = AP_INT_TIMEOUT;

	ret = spi_hid_of_ilitek_populate_config(config, dev);
	if (ret) {
		dev_err(dev, "%s: unable to populate config data.", __func__);
		goto err_plat_dev_fail;
	}

	ret = spi_hid_core_probe(spi, &config->ops, &config->property_conf);
	if (ret < 0) {
		ILI_ERR("Hid spi plat probe error, remove registered resources\n");
		goto err_plat_dev_fail;
	}

	return 0;

err_plat_dev_fail:
	return ret;
}

static int spi_hid_of_ilitek_probe(struct spi_device *spi)
{
	ilits = devm_kzalloc(&spi->dev, sizeof(*ilits), GFP_KERNEL);
	if (!ilits)
		return -ENOMEM;

	ilits->dp.spi = spi;
	ilits->dp.core_probed = false;

	if (spi_hid_of_delayed_probe(ilits->dp.spi) == 0)
		ilits->dp.core_probed = true;

	return 0;
}

static void spi_hid_of_remove(struct spi_device *spi)
{
	if (!ilits)
		return;

	ilits->dp.spi = NULL;

	if (ilits->dp.core_probed) {
		spi_hid_core_remove(spi);
		ilitek_plat_remove();
	}
}

static const struct of_device_id ilitek_spi_hid_of_match[] = {
	{ .compatible = "ilitek,ili79900a" },
	{ },
};
MODULE_DEVICE_TABLE(of, ilitek_spi_hid_of_match);

static struct spi_driver ilitek_spi_hid_ts_driver = {
	.driver = {
		.name	= "spi_hid_of_ilitek",
		.owner	= THIS_MODULE,
		.pm	= &spi_hid_core_pm,
		.of_match_table = of_match_ptr(ilitek_spi_hid_of_match),
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe		= spi_hid_of_ilitek_probe,
	.remove		= spi_hid_of_remove,
};

module_spi_driver(ilitek_spi_hid_ts_driver);

MODULE_AUTHOR("Dmitry Antipov <dmanti@microsoft.com>");
MODULE_DESCRIPTION("Ilitek spi-hid touchscreen driver");
MODULE_LICENSE("GPL");
