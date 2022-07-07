/*
 * ILITEK Touch IC driver
 *
 * Copyright (C) 2011-2025 ILI Technology Corporation.
 *
 * Author: Aaron Ye <aaron_ye@ilitek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */


#ifndef __HID_ILITEK_H
#define __HID_ILITEK_H

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/kobject.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/regulator/consumer.h>
#include <linux/power_supply.h>
#include <linux/fs.h>
#include <linux/fb.h>
#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif
#include <linux/uaccess.h>

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/ctype.h>

#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/time.h>

#include <linux/namei.h>
#include <linux/vmalloc.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>

#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <linux/rtc.h>
#include <linux/syscalls.h>
#include <linux/security.h>
#include <linux/mount.h>
#include <linux/firmware.h>

#include <linux/hid.h>

#ifdef CONFIG_OF
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#endif

#if IS_ENABLED(CONFIG_DEVICE_MODULES_DRM_MEDIATEK)
#include "mtk_disp_notify.h"
#include "mtk_panel_ext.h"
#endif

#if IS_ENABLED(CONFIG_STYLUS_BATTERY_ALGO)
#include "stylus_battery_algo.h"
#endif

/* define names and paths for the variety of tp modules */
#define DEF_FW_FILP_PATH		"/sdcard/ILITEK_FW"
#define DEF_FW_REQUEST_PATH "ilitek_fff1.bin"
extern unsigned char CTPM_FW_DEF[];

#define ILITEK_IOCTL_MAGIC 				100
#define ILITEK_IOCTL_MAXNR 				41

#define ILITEK_IOCTL_I2C_WRITE_DATA			_IOWR(ILITEK_IOCTL_MAGIC, 0, u8 *)
#define ILITEK_IOCTL_I2C_SET_WRITE_LENGTH		_IOWR(ILITEK_IOCTL_MAGIC, 1, int)
#define ILITEK_IOCTL_I2C_READ_DATA			_IOWR(ILITEK_IOCTL_MAGIC, 2, u8 *)
#define ILITEK_IOCTL_I2C_SET_READ_LENGTH		_IOWR(ILITEK_IOCTL_MAGIC, 3, int)

#define ILITEK_IOCTL_TP_HW_RESET			_IOWR(ILITEK_IOCTL_MAGIC, 4, int)
#define ILITEK_IOCTL_TP_POWER_SWITCH			_IOWR(ILITEK_IOCTL_MAGIC, 5, int)
#define ILITEK_IOCTL_TP_REPORT_SWITCH			_IOWR(ILITEK_IOCTL_MAGIC, 6, int)
#define ILITEK_IOCTL_TP_IRQ_SWITCH			_IOWR(ILITEK_IOCTL_MAGIC, 7, int)

#define ILITEK_IOCTL_TP_DEBUG_LEVEL			_IOWR(ILITEK_IOCTL_MAGIC, 8, int)
#define ILITEK_IOCTL_TP_FUNC_MODE			_IOWR(ILITEK_IOCTL_MAGIC, 9, int)

#define ILITEK_IOCTL_TP_FW_VER				_IOWR(ILITEK_IOCTL_MAGIC, 10, u8 *)
#define ILITEK_IOCTL_TP_PL_VER				_IOWR(ILITEK_IOCTL_MAGIC, 11, u8 *)
#define ILITEK_IOCTL_TP_CORE_VER			_IOWR(ILITEK_IOCTL_MAGIC, 12, u8 *)
#define ILITEK_IOCTL_TP_DRV_VER				_IOWR(ILITEK_IOCTL_MAGIC, 13, u8 *)
#define ILITEK_IOCTL_TP_CHIP_ID				_IOWR(ILITEK_IOCTL_MAGIC, 14, u32 *)

#define ILITEK_IOCTL_TP_MODE_CTRL			_IOWR(ILITEK_IOCTL_MAGIC, 17, u8 *)
#define ILITEK_IOCTL_TP_MODE_STATUS			_IOWR(ILITEK_IOCTL_MAGIC, 18, int *)
#define ILITEK_IOCTL_ICE_MODE_SWITCH			_IOWR(ILITEK_IOCTL_MAGIC, 19, int)

#define ILITEK_IOCTL_TP_INTERFACE_TYPE			_IOWR(ILITEK_IOCTL_MAGIC, 20, u8 *)
#define ILITEK_IOCTL_TP_DUMP_FLASH			_IOWR(ILITEK_IOCTL_MAGIC, 21, int)
#define ILITEK_IOCTL_TP_FW_UART_CTRL			_IOWR(ILITEK_IOCTL_MAGIC, 22, u8 *)
#define ILITEK_IOCTL_TP_PANEL_INFO			_IOWR(ILITEK_IOCTL_MAGIC, 23, u32 *)
#define ILITEK_IOCTL_TP_INFO				_IOWR(ILITEK_IOCTL_MAGIC, 24, u32 *)
#define ILITEK_IOCTL_WRAPPER_RW				_IOWR(ILITEK_IOCTL_MAGIC, 25, u8 *)
#define ILITEK_IOCTL_DDI_WRITE				_IOWR(ILITEK_IOCTL_MAGIC, 26, u8 *)
#define ILITEK_IOCTL_DDI_READ				_IOWR(ILITEK_IOCTL_MAGIC, 27, u8 *)
#define ILITEK_IOCTL_REPORT_RATE_SET			_IOWR(ILITEK_IOCTL_MAGIC, 28, u8 *)
#define ILITEK_IOCTL_REPORT_RATE_GET			_IOWR(ILITEK_IOCTL_MAGIC, 29, u8 *)
#define ILITEK_IOCTL_MP_LCM_OFF_ENV			_IOWR(ILITEK_IOCTL_MAGIC, 30, u8 *)
#define ILITEK_IOCTL_RELEASE_TOUCH			_IOWR(ILITEK_IOCTL_MAGIC, 31, u8 *)
#define ILITEK_IOCTL_LCM_STATUS				_IOWR(ILITEK_IOCTL_MAGIC, 32, u8 *)
#define ILITEK_IOCTL_CLIENT_CTRL			_IOWR(ILITEK_IOCTL_MAGIC, 33, u8*)
#define ILITEK_IOCTL_CASCADE_REG_RW			_IOWR(ILITEK_IOCTL_MAGIC, 34, u8 *)
#define ILITEK_IOCTL_INTERFACE_GET			_IOWR(ILITEK_IOCTL_MAGIC, 35, uint8_t *)
#define ILITEK_IOCTL_ICE_MODE_FLAG_SET			_IOWR(ILITEK_IOCTL_MAGIC, 36, uint8_t *)
#define ILITEK_IOCTL_WRAPPER_RW_OLED_FORMAT		_IOWR(ILITEK_IOCTL_MAGIC, 38, u8*)
#define ILITEK_IOCTL_SWITCH_SYNC_CTL			_IOWR(ILITEK_IOCTL_MAGIC, 39, u8*)
#define ILITEK_IOCTL_SWITCH_ICE_FLASH_CS_CTL		_IOWR(ILITEK_IOCTL_MAGIC, 40, uint8_t *)
#define ILITEK_IOCTL_MP_RUNNING_BY_DAEMON		_IOWR(ILITEK_IOCTL_MAGIC, 41, uint8_t *)

#ifdef CONFIG_COMPAT
#define ILITEK_COMPAT_IOCTL_I2C_WRITE_DATA		_IOWR(ILITEK_IOCTL_MAGIC, 0, compat_uptr_t)
#define ILITEK_COMPAT_IOCTL_I2C_SET_WRITE_LENGTH	_IOWR(ILITEK_IOCTL_MAGIC, 1, compat_uptr_t)
#define ILITEK_COMPAT_IOCTL_I2C_READ_DATA		_IOWR(ILITEK_IOCTL_MAGIC, 2, compat_uptr_t)
#define ILITEK_COMPAT_IOCTL_I2C_SET_READ_LENGTH		_IOWR(ILITEK_IOCTL_MAGIC, 3, compat_uptr_t)

#define ILITEK_COMPAT_IOCTL_TP_HW_RESET			_IOWR(ILITEK_IOCTL_MAGIC, 4, compat_uptr_t)
#define ILITEK_COMPAT_IOCTL_TP_POWER_SWITCH		_IOWR(ILITEK_IOCTL_MAGIC, 5, compat_uptr_t)
#define ILITEK_COMPAT_IOCTL_TP_REPORT_SWITCH		_IOWR(ILITEK_IOCTL_MAGIC, 6, compat_uptr_t)
#define ILITEK_COMPAT_IOCTL_TP_IRQ_SWITCH		_IOWR(ILITEK_IOCTL_MAGIC, 7, compat_uptr_t)

#define ILITEK_COMPAT_IOCTL_TP_DEBUG_LEVEL		_IOWR(ILITEK_IOCTL_MAGIC, 8, compat_uptr_t)
#define ILITEK_COMPAT_IOCTL_TP_FUNC_MODE		_IOWR(ILITEK_IOCTL_MAGIC, 9, compat_uptr_t)

#define ILITEK_COMPAT_IOCTL_TP_FW_VER			_IOWR(ILITEK_IOCTL_MAGIC, 10, compat_uptr_t)
#define ILITEK_COMPAT_IOCTL_TP_PL_VER			_IOWR(ILITEK_IOCTL_MAGIC, 11, compat_uptr_t)
#define ILITEK_COMPAT_IOCTL_TP_CORE_VER			_IOWR(ILITEK_IOCTL_MAGIC, 12, compat_uptr_t)
#define ILITEK_COMPAT_IOCTL_TP_DRV_VER			_IOWR(ILITEK_IOCTL_MAGIC, 13, compat_uptr_t)
#define ILITEK_COMPAT_IOCTL_TP_CHIP_ID			_IOWR(ILITEK_IOCTL_MAGIC, 14, compat_uptr_t)

#define ILITEK_COMPAT_IOCTL_TP_MODE_CTRL		_IOWR(ILITEK_IOCTL_MAGIC, 17, compat_uptr_t)
#define ILITEK_COMPAT_IOCTL_TP_MODE_STATUS		_IOWR(ILITEK_IOCTL_MAGIC, 18, compat_uptr_t)
#define ILITEK_COMPAT_IOCTL_ICE_MODE_SWITCH		_IOWR(ILITEK_IOCTL_MAGIC, 19, compat_uptr_t)

#define ILITEK_COMPAT_IOCTL_TP_INTERFACE_TYPE		_IOWR(ILITEK_IOCTL_MAGIC, 20, compat_uptr_t)
#define ILITEK_COMPAT_IOCTL_TP_DUMP_FLASH		_IOWR(ILITEK_IOCTL_MAGIC, 21, compat_uptr_t)
#define ILITEK_COMPAT_IOCTL_TP_FW_UART_CTRL		_IOWR(ILITEK_IOCTL_MAGIC, 22, compat_uptr_t)
#define ILITEK_COMPAT_IOCTL_TP_PANEL_INFO		_IOWR(ILITEK_IOCTL_MAGIC, 23, compat_uptr_t)
#define ILITEK_COMPAT_IOCTL_TP_INFO			_IOWR(ILITEK_IOCTL_MAGIC, 24, compat_uptr_t)
#define ILITEK_COMPAT_IOCTL_WRAPPER_RW			_IOWR(ILITEK_IOCTL_MAGIC, 25, compat_uptr_t)
#define ILITEK_COMPAT_IOCTL_DDI_WRITE	 		_IOWR(ILITEK_IOCTL_MAGIC, 26, compat_uptr_t)
#define ILITEK_COMPAT_IOCTL_DDI_READ	 		_IOWR(ILITEK_IOCTL_MAGIC, 27, compat_uptr_t)
#define ILITEK_COMPAT_IOCTL_REPORT_RATE_SET 		_IOWR(ILITEK_IOCTL_MAGIC, 28, compat_uptr_t)
#define ILITEK_COMPAT_IOCTL_REPORT_RATE_GET 		_IOWR(ILITEK_IOCTL_MAGIC, 29, compat_uptr_t)
#define ILITEK_COMPAT_IOCTL_MP_LCM_OFF_ENV 		_IOWR(ILITEK_IOCTL_MAGIC, 30, compat_uptr_t)
#define ILITEK_COMPAT_IOCTL_RELEASE_TOUCH		_IOWR(ILITEK_IOCTL_MAGIC, 31, compat_uptr_t)
#define ILITEK_COMPAT_IOCTL_LCM_STATUS			_IOWR(ILITEK_IOCTL_MAGIC, 32, compat_uptr_t)
#define ILITEK_COMPAT_IOCTL_CLIENT_CTRL			_IOWR(ILITEK_IOCTL_MAGIC, 33, compat_uptr_t)
#define ILITEK_COMPAT_IOCTL_CASCADE_REG_RW		_IOWR(ILITEK_IOCTL_MAGIC, 34, compat_uptr_t)
#define ILITEK_COMPAT_IOCTL_INTERFACE_GET		_IOWR(ILITEK_IOCTL_MAGIC, 35, compat_uptr_t)
#define ILITEK_COMPAT_IOCTL_ICE_MODE_FLAG_SET		_IOWR(ILITEK_IOCTL_MAGIC, 36, compat_uptr_t)
#define ILITEK_COMPAT_IOCTL_WRAPPER_RW_OLED_FORMAT 	_IOWR(ILITEK_IOCTL_MAGIC, 38, compat_uptr_t)
#define ILITEK_COMPAT_IOCTL_SWITCH_SYNC_CTL 		_IOWR(ILITEK_IOCTL_MAGIC, 39, compat_uptr_t)
#define ILITEK_COMPAT_IOCTL_SWITCH_ICE_FLASH_CS_CTL	_IOWR(ILITEK_IOCTL_MAGIC, 40, compat_uptr_t)
#define ILITEK_COMPAT_IOCTL_MP_RUNNING_BY_DAEMON	_IOWR(ILITEK_IOCTL_MAGIC, 41, compat_uptr_t)
#endif


#define RW_SYNC 0
#define R_ONLY 1
#define W_ONLY 2

#define UPDATE_PASS 0
#define UPDATE_FAIL -1
#define NO_NEED_UPDATE 0
#define NEED_UPDATE -1
#define TIMEOUT_SECTOR 500
#define TIMEOUT_PAGE 3500
#define TIMEOUT_PROGRAM 50

#define PROTOCL_VER_NUM 8

#define DRIVER_VERSION "3.0.11.0.251210"

#define ENABLE 1
#define DISABLE 0
#define K (1024)
#define M (K * K)
#define START 1
#define ON 1
#define ILI_WRITE 1
#define ILI_READ 0
#define END 0
#define OFF 0
#define NONE -1

#define COMPRESS_PACKET_LEN 4093

#define TDDI_RST_BIND	DISABLE
#define ENABLE_CASCADE	ENABLE
#define ENABLE_PEN_MODE	ENABLE	/*Pen mode options*/

#if ENABLE_PEN_MODE
#define USER_STR_BUFF		(PAGE_SIZE * 2)
#define IOCTL_I2C_BUFF		(PAGE_SIZE * 2)
#else
#define USER_STR_BUFF		PAGE_SIZE
#define IOCTL_I2C_BUFF		PAGE_SIZE
#endif

#define TDDI_INTERFACE BUS_SPI
#define SPI_CLK 9 /* follow by clk list */
#define SPI_WRITE 0x82
#define SPI_WRITE_CLIENT0 0x87
#define SPI_READ 0x83
#define SPI_ACK 0xA3
#define FW_STATUS_ALIVE SPI_ACK
#if (ENABLE_CASCADE && TDDI_INTERFACE == BUS_SPI)
#define ENABLE_SPICASCADE_V2 ENABLE /* if 9882T should set DISABLE*/
#else
#define ENABLE_SPICASCADE_V2 DISABLE
#endif
#if (ENABLE_CASCADE == ENABLE)
#define SPI_DMA_TRANSFER_SPLIT ENABLE
#else
#define SPI_DMA_TRANSFER_SPLIT DISABLE
#endif
#define SPI_DMA_TRANSFER_SPLIT_OLD DISABLE
enum TP_SPI_CLK_LIST {
	TP_SPI_CLK_1M = 1000000,
	TP_SPI_CLK_2M = 2000000,
	TP_SPI_CLK_3M = 3000000,
	TP_SPI_CLK_4M =	4000000,
	TP_SPI_CLK_5M =	5000000,
	TP_SPI_CLK_6M =	6000000,
	TP_SPI_CLK_7M =	7000000,
	TP_SPI_CLK_8M = 8000000,
	TP_SPI_CLK_9M = 9000000,
	TP_SPI_CLK_10M = 10000000,
	TP_SPI_CLK_11M = 11000000,
	TP_SPI_CLK_12M = 12000000,
	TP_SPI_CLK_13M = 13000000,
	TP_SPI_CLK_14M = 14000000,
	TP_SPI_CLK_15M = 15000000,
};

/* Options */
#define VDD_VOLTAGE 1800000
#define VCC_VOLTAGE 1800000
#define IRQ_GPIO_NUM 312
#define TR_BUF_SIZE (6 * K) /* Buffer size of touch report */
#define TR_BUF_LIST_SIZE (256) /* Buffer size of touch report for debug data */
#define SPI_TX_BUF_SIZE (6*K)
#define SPI_RX_BUF_SIZE (10*K)
#define WQ_ESD_DELAY 4000
#define WQ_BAT_DELAY 2000
#define AP_INT_TIMEOUT 1000	/*1s*/
#define MP_INT_TIMEOUT 5000	/*5s*/
#define MT_PRESSURE ENABLE
#define REGULATOR_POWER DISABLE
#define TP_SUSPEND_PRIO ENABLE
#define RESUME_BY_DDI DISABLE
#define BOOT_FW_UPDATE ENABLE
#define MP_INT_LEVEL DISABLE
#define PLL_CLK_WAKEUP_TP_RESUME DISABLE
#define ENGINEER_FLOW ENABLE
#define DMESG_SEQ_FILE ENABLE
#define ENABLE_GET_ALL_INFO ENABLE	/*GET_ALL_INFO options*/

#define BOOT_FW_VER_DIFF 0
#define BOOT_FW_VER_UPGRADE 1
#define BOOT_FW_VER_DOWNGRADE 2
#define BOOT_FW_UPDATE_MODE BOOT_FW_VER_UPGRADE
#define ENABLE_COMPRESS_MODE DISABLE	/*Compress mode options*/

/*if current interface is spi, must to hostdownload */
#define HOST_DOWN_LOAD ENABLE

/* Platform compatibility */
#define CONFIG_PLAT_SPRD DISABLE
#define I2C_DMA_TRANSFER DISABLE

/* Path */
#define DEBUG_DATA_FILE_SIZE (10 * K)
/* Debug messages */
#define DEBUG_NONE 0
#define DEBUG_ALL 1
#define DEBUG_OUTPUT DEBUG_NONE

#define ENTER_ICE_MODE_MAX_BUF_LEN 60
#define MAX_FRAME_CNT 10
#define DATA_BASE_TYPE_16 16
#define PRE_CMD_LEN 11
#define CMD_FW_MP_PRE_SET_LEN 2

#define ILI_DEBUG_INFO(fmt, arg...)                                            \
	({ pr_debug("ILITEK[DBG]: (%s, %d): " fmt, __func__, __LINE__, ##arg); })

#define ILI_INFO(fmt, arg...)                                                  \
	({ pr_info("ILITEK[INFO]: (%s, %d): " fmt, __func__, __LINE__, ##arg); })

#define ILI_ERR(fmt, arg...)                                                   \
	({ pr_err("ILITEK[ERR]: (%s, %d): " fmt, __func__, __LINE__, ##arg); })

extern bool debug_en;
#define ILI_DBG(fmt, arg...)                                                   \
	do {                                                                   \
		if (debug_en)                                                  \
			pr_info("ILITEK: (%s, %d): " fmt, __func__, __LINE__,  \
				##arg);                                        \
	} while (0)

#define ERR_ALLOC_MEM(X) ((IS_ERR(X) || X == NULL) ? 1 : 0)
#define K (1024)
#define M (K * K)
#define START 1
#define ON 1
#define ILI_WRITE 1
#define ILI_READ 0
#define END 0
#define OFF 0
#define NONE -1
#define DO_SPI_RECOVER -2
#define DO_I2C_RECOVER -3

#define HID_TOUCH_INPUT_DEVICE "ili_ts_wkg"
#define HEX_SIGN_KEY "ilitekTP"

/* Path */
#define TXT_FILE_SIZE (1*K)
#define PEN_ID_NODE_TEST_PATH "/sdcard/Get_Wacom_PenID.txt"
/* Cascade mode */
enum CASCADE_MODE {
	MASTER = 0,
	CLIENT,
	BOTH,
};

/* reach SAMPLE_FREQ=10 in different touch modules. 240/10=24. */
#define PEN_FRAME_COUNT 24

/* input_dev register state */
#define INPUT_DEV_STATUS_UNREGISTER -1
#define INPUT_DEV_STATUS_REGISTER 0

/* The association between TP notifier function and LCM notification chain  */
#define TP_NOTIFIER_STATUS_UNREGISTER -1
#define TP_NOTIFIER_STATUS_REGISTER 0

enum TP_PLAT_TYPE { TP_PLAT_MTK = 0, TP_PLAT_QCOM };

enum TP_RST_METHOD {
	TP_IC_WHOLE_RST_WITH_FLASH = 0,
	TP_IC_WHOLE_RST_WITHOUT_FLASH,
	TP_IC_CODE_RST,
	TP_HW_RST_ONLY,
};

enum TP_FW_UPGRADE_TYPE { UPGRADE_FLASH = 0, UPGRADE_IRAM };

enum TP_FW_UPGRADE_STATUS {
	FW_STAT_INIT = 0,
	FW_UPDATING = 90,
	FW_UPDATE_PASS = 100,
	FW_UPDATE_FAIL = -1
};
enum TP_FW_OPEN_METHOD {
	REQUEST_FIRMWARE = 0,
};
enum TP_SLEEP_STATUS { TP_SUSPEND = 0, TP_DEEP_SLEEP = 1, TP_RESUME = 2 };
enum TP_SLEEP_CTRL { SLEEP_IN = 0x0, SLEEP_OUT = 0x1, DEEP_SLEEP_IN = 0x3 };

enum TP_FW_BLOCK_NUM {
	AP = 1,
	DATA_BLOCK = 2,
	TUNING = 3,
	GESTURE = 4,
	MP = 5,
	DDI = 6,
	TAG = 7,
	PARA_BACKUP = 8,
	RESERVE_BLOCK3 = 9,
	PEN = 10,
	RESERVE_BLOCK5 = 11,
	RESERVE_BLOCK6 = 12,
	RESERVE_BLOCK7 = 13,
	RESERVE_BLOCK8 = 14,
	RESERVE_BLOCK9 = 15,
	RESERVE_BLOCK10 = 16,
};

/* Tag Info 0xB1 */
enum TP_FW_B1_BLOCK_NUM {
	CUSTOMER = 1,
	MPDATA = 2,
	TRIMCODE = 3,
};

enum TP_FW_BLOCK_MODES_NEED_UPGRADE {
	NEED_UPGRADE_AP = 0,
	NEED_UPGRADE_MP = 1,
	NEED_UPGRADE_GESTURE = 2,
};
enum TP_FW_BLOCK_TAG {
	BLOCK_TAG_AF = 0xAF,
	BLOCK_TAG_B0 = 0xB0,
	BLOCK_TAG_B2 = 0xB2,
	BLOCK_TAG_SIGN = 0xEE,
};

enum TP_WQ_TYPE {
	WQ_ESD = 0,
	WQ_BAT,
};

enum TP_DATA_FORMAT {
	DATA_FORMAT_DEMO = 0,
	DATA_FORMAT_DEBUG,
	DATA_FORMAT_DEMO_DEBUG_INFO,
	DATA_FORMAT_GESTURE_SPECIAL_DEMO,
	DATA_FORMAT_GESTURE_INFO,
	DATA_FORMAT_GESTURE_NORMAL,
	DATA_FORMAT_GESTURE_DEMO,
	DATA_FORMAT_GESTURE_DEBUG,
	DATA_FORMAT_DEBUG_LITE_ROI,
	DATA_FORMAT_DEBUG_LITE_WINDOW,
	DATA_FORMAT_DEBUG_LITE_AREA,
	DATA_FORMAT_DEBUG_LITE_PEN,
	DATA_FORMAT_DEBUG_LITE_PEN_AREA,
};

enum NODE_MODE_SWITCH {
	AP_MODE = 0,
	TEST_MODE,
	DEBUG_MODE,
	DEBUG_LITE_ROI,
	DEBUG_LITE_WINDOW,
	DEBUG_LITE_AREA,
	DEBUG_PEN_ONLY_MODE,
	DEBUG_HAND_ONLY_MODE,
	DEBUG_LITE_PEN,
	DEBUG_LITE_PEN_AREA,
};
enum TP_MODEL {
	MODEL_DEF = 0,
	MODEL_CSOT,
	MODEL_AUO,
	MODEL_BOE,
	MODEL_INX,
	MODEL_DJ,
	MODEL_TXD,
	MODEL_TM
};

enum TP_ERR_CODE {
		EMP_CMD = 100,
	EMP_PROTOCOL,
	EMP_FILE,
	EMP_INI,
	EMP_TIMING_INFO,
	EMP_INVAL,
	EMP_PARSE,
	EMP_NOMEM,
	EMP_GET_CDC,
	EMP_INT,
	EMP_CHECK_BUY,
	EMP_MODE,
	EMP_FW_PROC,
	EMP_FORMUL_NULL,
	EMP_PARA_NULL,
	EFW_CONVERT_FILE,
	EFW_ICE_MODE,
	EFW_CRC,
	EFW_REST,
	EFW_ERASE,
	EFW_PROGRAM,
	EFW_INTERFACE,
	EFW_BACKUP,
	EFW_FP,
	EFW_HEXSIGH,
	EFW_GESRUN_FAIL,/*Can't polling gesture run key after gesture recovery*/
};

enum TP_IC_TYPE {
	ILI_A = 0x0A,
	ILI_B,
	ILI_C,
	ILI_D,
	ILI_E,
	ILI_F,
	ILI_G,
	ILI_H,
	ILI_I,
	ILI_J,
	ILI_K,
	ILI_L,
	ILI_M,
	ILI_N,
	ILI_O,
	ILI_P,
	ILI_Q,
	ILI_R,
	ILI_S,
	ILI_T,
	ILI_U,
	ILI_V,
	ILI_W,
	ILI_X,
	ILI_Y,
	ILI_Z,
};

typedef enum cmd_types {
	CMD_DISABLE = 0x00,
	CMD_ENABLE = 0x01,
	CMD_STATUS = 0x02,
	CMD_ROI_DATA = 0x03,
} cmd_types;

struct report_info_block {
	u8 nReportByPixel	:1;
	u8 nIsHostDownload	:1;
	u8 nIsSPIICE		:1;
	u8 nIsSPICLIENT		:1;
	u8 nIsI2C		:1;
	u8 nReserved00		:3;
	u8 nReportResolutionMode:3;
	u8 nCustomerType	:5;
	u8 nDemoPacketID	:8;
	u8 nDemoFingerType	:3;
	u8 nDemoCustomerType:3;
	u8 nDemoPenType		:2;
};

struct cascade_info_block {
	u8 nDisable : 1;
	u8 nNum : 3;
	u8 nReserved00 : 4;
	u8 nReserved01 : 8;
	u8 nReserved02 : 8;
	u8 nReserved03 : 8;
};

struct pen_info_block {
	u8 nPxRaw  :8;
	u8 nPyRaw  :8;
	u8 nPxVa   :8;
	u8 nPyVa   :8;
	u8 nPenX_MP  :8;
	u8 nPenChipnum	:8;
	u8 nPenSamplenum  :8;
	u8 nReserved03	:8;
};

typedef enum pen_data_modes {
	HAND_PEN_DEMO_MODE = 0,
	HAND_PEN_SIGNAL_MODE,
	HAND_PEN_RAW_DATA_MODE,
	PEN_ONLY_SIGNAL_MODE,
	PEN_ONLY_RAW_DATA_MODE,
	HAND_ONLY_SIGNAL_MODE,
	HAND_ONLY_RAW_DATA_MODE,
} pen_data_modes;

typedef enum report_types {
	P5_X_HAND_PEN_TYPE =
		0, /*0x39, FigType 0-2 bits, CustomType 3-5, PenType 6-7*/
	P5_X_ONLY_PEN_TYPE = 1, /*0x3F*/
	P5_X_ONLY_HAND_TYPE = 2, /*0xF9*/
} report_types;

struct ilitek_pen_info {
	u16 x;
	u16 y;
	u16 pressure;
	s32 tilt_x;
	s32 tilt_y;
	u8 pen_id[8];
	u8 distance_cnt;
	bool TipSwitch;
	bool BarelSwitch;
	bool Eraser;
	bool InRange;
	bool active;
	bool finger_touch;
	report_types report_type;
	pen_data_modes pen_data_mode;
};

struct ilitek_delayed_probe {
	struct device *dev;
	struct spi_device *spi;
	struct delayed_work dwork;
	bool core_probed;
};

#define TDDI_I2C_ADDR 0x41
#define TDDI_I2C_CLIENT_ADDR 0x4f
#define TDDI_DEV_ID "ILITEK_TDDI"

#define WRAP_RW_BYTES_OFFSET 4
#define DUMP_DATA_ROW_LEN_16 16
#define DUMP_DATA_TYPE_LEN_8 8
#define FW_CMD_BUFF_LEN 256

/* define the width and height of a screen. */
#define TOUCH_SCREEN_X_MIN 0
#define TOUCH_SCREEN_Y_MIN 0
#define TOUCH_SCREEN_X_MAX 720
#define TOUCH_SCREEN_Y_MAX 1440
#define MAX_TOUCH_NUM 10
#define MAX_PEN_NUM 1
#define PEN_INDEX 10
#define ILITEK_KNUCKLE_ROI_FINGERS 2

/* Firmware upgrade */
#define CORE_VER_1410 0x01040100
#define CORE_VER_1420 0x01040200
#define CORE_VER_1430 0x01040300
#define CORE_VER_1460 0x01040600
#define CORE_VER_1470 0x01040700
#define CORE_VER_1600 0x01060000
#define CORE_VER_1700 0x01070000
#define CORE_VER_2100 0x02010000
#define MAX_HEX_FILE_SIZE (1024 * K)
#define UPDATE_BUF_SIZE	(256*K)
#define ILI_FILE_HEADER 256
#define DLM_START_ADDRESS 0x20610
#define DLM_HEX_ADDRESS 0x10000
#define MP_HEX_ADDRESS 0x13000
#define DDI_RSV_BK_ST_ADDR 0x1E000
#define DDI_RSV_BK_END_ADDR 0x1FFFF
#define DDI_RSV_BK_SIZE (1 * K)
#define RSV_BK_ST_ADDR 0x1E000
#define RSV_BK_END_ADDR 0x1E3FF
#define FW_BLOCK_INFO_NUM 17
#define FW_BLOCK_INFO_B1_NUM 4
#define DEFINED_MODE_NUM 3
#if SPI_DMA_TRANSFER_SPLIT
#define SPI_UPGRADE_LEN	2048
#else
#define SPI_UPGRADE_LEN	(16*K)
#endif
#define SPI_BUF_SIZE MAX_HEX_FILE_SIZE
#define INFO_HEX_ST_ADDR 0x4F
#define INFO_MP_HEX_ADDR 0x1F
#define INFO_PEN_ST_ADDR 0x67
#define INFO_CASCADE_ST_ADDR 0x6B

/* INT Function Registers */
#define INTR_BASED_ADDR 0x48000
#define INTR1_ADDR (INTR_BASED_ADDR + 0x4)
#define INTR1_FLASH_INT_FLAG (INTR_BASED_ADDR + 0x7)
#define INTR1_reg_uart_tx_int_flag INTR1_ADDR
#define INTR1_reserved_0 (BIT(1)|BIT(2)|BIT(3)|BIT(4)|BIT(5)|BIT(6)|BIT(7))
#define INTR1_reg_wdt_alarm_int_flag BIT(8)
#define INTR1_reserved_1 (BIT(9)|BIT(10)|BIT(11)|BIT(12)|BIT(13)|BIT(14)|BIT(15))
#define INTR1_reg_dma_ch0_int_flag BIT(16)
#define INTR1_reg_dma_ch1_int_flag BIT(17)
#define INTR1_reg_dma_frame_done_int_flag BIT(18)
#define INTR1_reg_dma_tdi_done_int_flag BIT(19)
#define INTR1_reserved_2 (BIT(20) | BIT(21) | BIT(22) | BIT(23))
#define INTR1_reg_flash_error_flag BIT(24)
#define INTR1_reg_flash_int_flag BIT(25)
#define INTR1_reserved_3 BIT(26)

#define INTR2_ADDR (INTR_BASED_ADDR + 0x8)
#define INTR2_td_int_flag_clear INTR2_ADDR
#define INTR2_td_timeout_int_flag_clear BIT(1)
#define INTR2_td_debug_frame_done_int_flag_clear BIT(2)
#define INTR2_td_frame_start_scan_int_flag_clear BIT(3)
#define INTR2_log_int_flag_clear BIT(4)
#define INTR2_d2t_crc_err_int_flag_clear BIT(8)
#define INTR2_d2t_flash_req_int_flag_clear BIT(9)
#define INTR2_d2t_ddi_int_flag_clear BIT(10)
#define INTR2_wr_done_int_flag_clear BIT(16)
#define INTR2_rd_done_int_flag_clear BIT(17)
#define INTR2_tdi_err_int_flag_clear BIT(18)
#define INTR2_d2t_slpout_rise_flag_clear BIT(24)
#define INTR2_d2t_slpout_fall_flag_clear BIT(25)
#define INTR2_d2t_dstby_flag_clear BIT(26)
#define INTR2_ddi_pwr_rdy_flag_clear BIT(27)

#define INTR32_ADDR (INTR_BASED_ADDR + 0x80)
#define INTR32_reg_t0_int_en BIT(24)
#define INTR32_reg_t1_int_en BIT(25)

#define INTR33_ADDR (INTR_BASED_ADDR + 0x84)
#define INTR33_reg_dma_ch0_int_en BIT(16)

/* Flash */
#define FLASH_BASED_ADDR 0x41000
#define FLASH0_ADDR (FLASH_BASED_ADDR + 0x0)
#define FLASH0_reg_flash_csb FLASH0_ADDR
#define FLASH0_reg_preclk_sel (BIT(16)|BIT(17)|BIT(18)|BIT(19))
#define FLASH0_reg_tx_dual BIT(25)
#define FLASH0_reg_rx_dual BIT(24)
#define FLASH0_dual_mode (FLASH0_ADDR + 0x3)
#define FLASH1_ADDR (FLASH_BASED_ADDR + 0x4)
#define FLASH1_reg_flash_key1 FLASH1_ADDR
#define FLASH1_reg_flash_key2 (FLASH1_ADDR + 0x01)
#define FLASH1_reg_flash_key3 (FLASH1_ADDR + 0x02)
#define FLASH1_reserved_0 (FLASH1_ADDR + 0x03)
#define FLASH2_ADDR (FLASH_BASED_ADDR + 0x8)
#define FLASH2_reg_tx_data FLASH2_ADDR
#define FLASH3_ADDR (FLASH_BASED_ADDR + 0xC)
#define FLASH3_reg_rcv_cnt FLASH3_ADDR
#define FLASH4_ADDR (FLASH_BASED_ADDR + 0x10)
#define FLASH4_reg_rcv_data FLASH4_ADDR
#define FLASH4_reg_flash_dma_trigger_en (BIT(24)|BIT(25)|BIT(26)|BIT(27)|BIT(28)|BIT(29)|BIT(30)|BIT(31))
#define FLASH_CASCADE_ADDR 0x4104B
#define FLASH_reg_cas_fw_trigger_en BIT(7)

/* Dummy Registers */
#define WDT_DUMMY_BASED_ADDR 0x5101C
#define WDT7_DUMMY0 WDT_DUMMY_BASED_ADDR
#define WDT8_DUMMY1 (WDT_DUMMY_BASED_ADDR + 0x04)
#define WDT9_DUMMY2 (WDT_DUMMY_BASED_ADDR + 0x08)

/* Cascade Register */
#define WDT_ADDR 0x4004D
#define DMA_CRC_ADDR 0x4101C
#define ICE_HEADER_REG 0x44000
#define MSPI_DONE_FLAG_ADDR 0x48028
#define MSPI_INIT_EN_ADDR 0x480C4

#define MS_MISO_SEL_REG 0x62021
#define MSPI_REG 0x62084
#define MSPI_reg_bypass_off		BIT(0)
#define SINGLE_TO_QUAL_REG 0x6202C
#define CMD_MODE_EN_REG 0x6202C
#define WRITE_BUF_ADDR 0x6205C
#define MSPI_TRIG_BY_DMA_ADDR 0x62070
#define WRITE_BUF_SIZE_ADDR 0x62080
#define READ_BUF_SIZE_ADDR 0x62082
#define MSPI_CLOCK_ADDR 0x62086
#define MSPI_CS_ADDR 0x62087
#define MSPI_TRIGGER_ADDR 0x62085

#define TRIG_SEL_MSPI_ADDR 0x72102
#define DMA_ONE_CLEAR_ADDR 0x72103
#define SRC_ONE_ADDR 0x72104
#define SRC_ONE_SET_ADDR 0x72108
#define SRC_TWO_EN_ADDR 0x72113
#define DEST_ADDR 0x72114
#define DEST_SET_ADDR 0x72118
#define TRANSFER_CNT_ADDR 0x7211C
#define DMA_ONE_GROUP_ADDR 0x72318

#define CONTROL_CLIENT_EXIT_ICE_ADDR 0x181062

/* FW data format */
#define DATA_FORMAT_DEMO_CMD 0x00
#define DATA_FORMAT_DEBUG_CMD 0x02
#define DATA_FORMAT_DEMO_DEBUG_INFO_CMD 0x04
#define DATA_FORMAT_GESTURE_NORMAL_CMD 0x01
#define DATA_FORMAT_GESTURE_INFO_CMD 0x02
#define DATA_FORMAT_DEBUG_LITE_CMD 0x05
#define DATA_FORMAT_DEBUG_LITE_ROI_CMD 0x01
#define DATA_FORMAT_DEBUG_LITE_WINDOW_CMD 0x02
#define DATA_FORMAT_DEBUG_LITE_AREA_CMD 0x03
#define DATA_FORMAT_DEBUG_LITE_PEN_CMD 0x04
#define DATA_FORMAT_DEBUG_LITE_PEN_AREA_CMD 0x06

/* Pen data format */
#define DATA_FORMAT_DEBUG_PEN_CMD 0x06
#define DATA_FORMAT_DEBUG_HAND_CMD 0x07

#define P5_X_DEMO_MODE_PACKET_INFO_LEN 3
#define P5_X_DEMO_MODE_PACKET_LEN 43
#define P5_X_DEMO_MODE_PACKET_LEN_HIGH_RESOLUTION 72
#define P5_X_DEMO_LOW_RESOLUTION_FINGER_DATA_LENGTH 44
#define P5_X_DEMO_HIGH_RESOLUTION_FINGER_DATA_LENGTH 54
#define P5_X_DEMO_81_LENGTH_SIMPLE 74
#define P5_X_DEMO_81_LENGTH_ABUNDANT 90
#define P5_X_DEMO_MODE_AXIS_LEN 50
#define P5_X_DEMO_MODE_STATE_INFO 6
#define P5_X_INFO_HEADER_LENGTH 3
#define P5_X_P_SENSOR_KEY_LENGTH 1
#define P5_X_INFO_CHECKSUM_LENGTH 1
#define P5_X_DEMO_DEBUG_INFO_ID0_LENGTH 14
#define P5_X_DEBUG_MODE_PACKET_LENGTH 1280
#define P5_X_TEST_MODE_PACKET_LENGTH 1180
#define P5_X_GESTURE_NORMAL_LENGTH 8
#define P5_X_GESTURE_INFO_LENGTH 170
#define P5_X_GESTURE_INFO_LENGTH_HIGH_RESOLUTION 221
#define P5_X_DEBUG_LITE_LENGTH 300
#define P5_X_DEBUG_LITE_PEN_LENGTH 136
#define P5_X_CORE_VER_THREE_LENGTH 4
#define P5_X_CORE_VER_FOUR_LENGTH 5
#define P5_X_DEBUG_LOW_RESOLUTION_FINGER_DATA_LENGTH 35
#define P5_X_DEBUG_HIGH_RESOLUTION_FINGER_DATA_LENGTH 5
#define P5_X_5B_LOW_RESOLUTION_LENGTH 62
#define P5_X_CUSTOMER_AXIS_LENGTH 50
#define P5_X_CUSTOMER_ALS_LENGTH 10
#define P5_X_DEBUG_MODE_PACKET_INFO_LEN 4

/* Pen data info len */
#define P5_X_PEN_INFO_X_DATA_LEN 6
#define P5_X_PEN_INFO_Y_DATA_LEN 4
#define P5_X_PEN_DATA_LEN 24
#define P5_X_OTHER_DATA_LEN 16
#define P5_X_PEN_ID_LEN 8

/* enter ice mode retry */
#define ENTER_ICE_MODE_RETRY_COUNT 5

/* Protocol */
#define PROTOCOL_VER_500 0x050000
#define PROTOCOL_VER_510 0x050100
#define PROTOCOL_VER_520 0x050200
#define PROTOCOL_VER_530 0x050300
#define PROTOCOL_VER_540 0x050400
#define PROTOCOL_VER_550 0x050500
#define PROTOCOL_VER_560 0x050600
#define PROTOCOL_VER_570 0x050700
#define P5_X_READ_DATA_CTRL 0xF6
#define P5_X_GET_TP_INFORMATION 0x20
#define P5_X_GET_KEY_INFORMATION 0x27
#define P5_X_GET_TOOL_VERSION 0x28
#define P5_X_GET_PANEL_INFORMATION 0x29
#define P5_X_GET_FW_VERSION 0x21
#define P5_X_GET_PROTOCOL_VERSION 0x22
#define P5_X_GET_CORE_VERSION 0x23
#define P5_X_GET_CORE_VERSION_NEW 0x24
#define P5_X_GET_REPORT_INFORMATION 0x2B
#define P5_X_GET_ALL_INFORMATION 0x2F
#define P5_X_GET_REPORT_FORMAT 0x37
#define P5_X_GET_BLOCK_INFOMATION 0x38
#define P5_X_MODE_CONTROL 0xF0
#define P5_X_NEW_CONTROL_FORMAT 0xF2
#define P5_X_SET_CDC_INIT 0xF1
#define P5_X_GET_CDC_DATA 0xF2
#define P5_X_CDC_BUSY_STATE 0xF3
#define P5_X_I2C_UART 0x40
#define CMD_GET_FLASH_DATA 0x41
#define CMD_CTRL_INT_ACTION 0x1B
#define P5_X_FW_UNKNOWN_MODE 0xFF
#define P5_X_FW_AP_MODE 0x00
#define P5_X_FW_TEST_MODE 0x01
#define P5_X_FW_GESTURE_MODE 0x0F
#define P5_X_FW_SIGNAL_DATA_MODE 0x03
#define P5_X_FW_RAW_DATA_MODE 0x08
#define P5_X_DEMO_PACKET_ID 0x5A
#define P5_X_DEMO_AXIS_PACKET_ID 0x5B
#define P5_X_DEBUG_PACKET_ID 0xA7
#define P5_X_DEBUG_AXIS_PACKET_ID 0xA8
#define P5_X_TEST_PACKET_ID 0xF2
#define P5_X_GESTURE_PACKET_ID 0xAA
#define P5_X_GESTURE_FAIL_ID 0xAE
#define P5_X_I2CUART_PACKET_ID 0x7A
#define P5_X_DEBUG_LITE_PACKET_ID 0x9A
#define P5_X_CLIENT_MODE_CMD_ID 0x5F
#define P5_X_INFO_HEADER_PACKET_ID 0xB7
#define P5_X_DEMO_DEBUG_INFO_PACKET_ID 0x5C
#define P5_X_DEMO_PROXIMITY_ID 0xBC
#define P5_X_DEMO_HIGH_RESOLUTION_PACKET_ID 0x5B
#define P5_X_DEBUG_HIGH_RESOLUTION_PACKET_ID 0xA8
#define P5_X_DEMO_FINGER_PACKET_ID 0x71
#define P5_X_DEMO_PEN_PACKET_ID 0x72

/*Pen & Cascade info cmd*/
#define P5_X_GET_PEN_INFO 0x27
#define P5_X_GET_CASCADE_INFO 0x2A

/* Pen Type */
#define P5_X_HAND_PEN_TYPE 0	/* 0x39, FigType 0-2 bits, CustomType 3-5, PenType 6-7 */
#define P5_X_ONLY_PEN_TYPE 1	/* 0x3F */
#define P5_X_ONLY_HAND_TYPE 2	/* 0xF9 */

#define	ICE_HEADER						0x25
#define	ICE_FAK_HEADER					0x66
#define ICE_MCU_ON_HEADER				0x1F

#define ILI9881N_AA 0x98811700
#define ILI9881O_AA 0x98811800
#define ILI9882_CHIP 0x9882
#define TDDI_PID_ADDR 0x4009C
#define TDDI_SECOND_PID_ADDR 0x40098
#define TDDI_OTP_ID_ADDR 0x400A0
#define TDDI_ANA_ID_ADDR 0x400A4
#define TDDI_PC_COUNTER_ADDR 0x44008
#define TDDI_PC_LATCH_ADDR 0x51010
#define TDDI_CHIP_RESET_ADDR 0x40050
#define RAWDATA_NO_BK_SHIFT 8192
#define TDDI_WHOLE_CHIP_RST_WITH_FLASH_KEY 0x00019878
#define TDDI_WHOLE_CHIP_RST_WITHOUT_FLASH_KEY 0xA0019878
#define TDDI_WHOLE_CHIP_RST_WITHOUT_FLASH_PRE_KEY 0xA0009878

/* DDI */
#define PAGE00_CMD10_SLEEPIN 0x10
#define PAGE00_CMD11_SLEEPOUT 0x11
#define PAGE00_CMD28_DISPLAYOFF 0x28
#define PAGE00_CMD29_DISPLAYON 0x29

/* Report Format Resolution */
#define POSITION_LOW_RESOLUTION 0X00
#define POSITION_HIGH_RESOLUTION 0x01
#define POSITION_CUSTOMER_TYPE_AXIS 0x00
#define POSITION_CUSTOMER_TYPE_ALS 0x01
#define POSITION_CUSTOMER_TYPE_OFF 0x1F
#define POSITION_PEN_TYPE_ON 0x00
#define POSITION_PEN_TYPE_OFF 0x03
#define POSITION_CUSTOMER_TYPE_OFF_3BITS 0x07	/*core ver 1700, CustomerType 3 bits*/

#define EDGE_DELAY_FOR_FAST_ENTER_ICE		10

struct ilitek_ts_hid_data {
	struct i2c_client *i2c;
	struct spi_device *spi;
	struct input_dev *input;
	struct input_dev *input_pen;
	struct device *dev;
	struct wakeup_source *ili_upg_wakelock;

	struct ilitek_ic_info *chip;
	struct ilitek_protocol_info *protocol;
	struct regulator *vdd;
	struct regulator *vcc;
	/* HID over I2C */
	struct input_dev *input_hid;

#if IS_ENABLED(CONFIG_DEVICE_MODULES_DRM_MEDIATEK)
	struct notifier_block disp_notifier;
#endif

	struct mutex touch_mutex;
	struct mutex bus_lock;
	spinlock_t irq_spin;
	/* physical path to the input device in the system hierarchy */
	const char *phys;

	bool boot;
	u32 fw_pc;
	u32 fw_latch;

	u16 max_x;
	u16 max_y;
	u16 min_x;
	u16 min_y;
	u16 panel_wid;
	u16 panel_hei;
	u8 xch_num;
	u8 ych_num;
	u8 stx;
	u8 srx;
	u8 *update_buf;
	u8 *tr_buf;
	u8 *spi_tx;
	u8 *spi_rx;
	struct firmware tp_fw;

	int actual_tp_mode;
	int tp_data_mode;
	int tp_data_format;
	int tp_data_len;

	int irq_num;
	int irq_tirgger_type;
	int wait_int_timeout;
	int tp_rst;
	int tp_int;
	struct gpio_desc *reset_gpiod;
	struct gpio_desc *irq_gpiod;

	int finger;
	u8 customertype_off;
	u32 cdc_data_len;
	u8 gesture_data_type;
	bool compress_disable;
	bool compress_handonly_disable;
	bool compress_penonly_disable;

	/* pen report */
	u8 PenType;
	int pen;
	struct pen_info_block pen_info_block;
	struct ilitek_pen_info pen_info;
	int curt_touch[MAX_TOUCH_NUM + MAX_PEN_NUM];
	int prev_touch[MAX_TOUCH_NUM + MAX_PEN_NUM];
	u8 touch_num;

	int last_touch;
	int fw_retry;
	int fw_update_stat;
	int fw_open;
	int input_reg_state;
	int tp_notifier_reg_status;
	u8 fw_info[75];
	u8 fw_mp_ver[4];

	bool irq_wake_enabled;

	/* Cascade */
	struct cascade_info_block cascade_info_block;
	bool client_wr_ctrl;
	bool enable_cascade;
	bool i2c_addr_change;
	int cascade_rst_edge_delay;
	u8 cascade_ctrl_mode;

	u16 addr_value;

	bool report;
	//bool gesture;
	bool knuckle;
	int gesture_mode;
	int gesture_demo_ctrl;
	struct report_info_block rib;

	char *flashName;
	u16 flash_mid;
	u16 flash_devid;
	u8 current_report_rate_mode;
	int program_page;
	int flash_sector;

	ktime_t pen_touch_time;
	int supportFlashIndex;
	bool isSupportFlash;
	wait_queue_head_t inq;

	int reset;
	int rst_edge_delay;
	int fw_upgrade_mode;
	int mp_ret_len;
	bool force_fw_update;
	bool ddi_rest_done;
	bool resume_by_ddi;
	bool tp_suspend;
	bool info_from_hex;
	bool prox_near;
	bool gesture_load_code;
	bool trans_xy;
	bool ss_ctrl;
	bool node_update;
	bool int_pulse;
	bool pll_clk_wakeup;
	bool power_status;
	bool proxmity_face;
	bool eng_flow;
	bool ice_flash_cs_ctrl;
	bool skip_sync_cmd;
	bool change_ice_key;
	bool fast_enter_ice_mode;
	u8 prox_face_mode;
	u8 spi_ms_mode;
	/* module info */
	int tp_module;
	int md_fw_ili_size;
	char *md_name;
	char *md_fw_filp_path;
	char *md_fw_rq_path;
	char *outputStrArr;
	u8 *md_fw_ili;
	char *mp_result;
	atomic_t gesture;
	atomic_t irq_stat;
	atomic_t tp_reset;
	atomic_t ice_stat;
	atomic_t fw_stat;
	atomic_t mp_stat;
	atomic_t tp_sleep;
	atomic_t tp_sw_mode;
	atomic_t cmd_int_check;
	atomic_t esd_stat;
	atomic_t ignore_report;
	atomic_t stop_sync_stat;
	atomic_t irq_after_recovery;
	atomic_t init_stat;
	atomic_t spi_slave_write_mcu_on;

	int (*spi_write_then_read)(struct spi_device *spi, const void *txbuf,
		unsigned n_tx, void *rxbuf, unsigned n_rx);
	int (*wrapper)(u8 *wdata, u32 wlen, u8 *rdata, u32 rlen, bool spi_irq,
		bool i2c_irq);
	int (*ges_recover)(void);
	int (*detect_int_stat)(bool status);
	int (*ice_mode_ctrl)(bool enable, bool mcu, int mode);
};
extern struct ilitek_ts_hid_data *ilits;

struct ilitek_dma_config {
	u32 src_addr;
	u32 src_fmt;
	u32 dest_addr;
	u32 dest_fmt;
	u32 block_size;
	u32 dmaControlSwitch;
};

struct ilitek_protocol_info {
	u32 ver;
	int fw_ver_len;
	int pro_ver_len;
	int tp_info_len;
	int key_info_len;
	int panel_info_len;
	int core_ver_len;
	int func_ctrl_len;
	int window_len;
	int cdc_len;
};

#define FUNC_CTRL_NUM	17
struct ilitek_ic_func_ctrl {
	const char *name;
	u8 cmd[6];
	int len;
	u8 def_cmd;
	u8 rec_state; /*0:disable, 1: enable, 2: ignore record*/
	u8 rec_cmd;
};
extern struct ilitek_ic_func_ctrl func_ctrl[FUNC_CTRL_NUM];

struct touch_fw_data {
	u8 block_number;
	u32 start_addr;
	u32 end_addr;
	u32 new_fw_cb;
	int delay_after_upgrade;
	bool isCRC;
	bool isboot;
	bool is80k;
	int hex_tag;
	int mapping_tag;
};
extern struct touch_fw_data tfd;

struct flash_block_info {
	char *name;
	u32 start;
	u32 end;
	u32 len;
	u32 mem_start;
	u32 fix_mem_start;
	u32 fix_mem_start_multi_buf[DEFINED_MODE_NUM];
	u8 mode;
};
extern struct flash_block_info fbi[FW_BLOCK_INFO_NUM];

struct ilitek_ic_info {
	u8 type;
	u8 ver;
	u32 id;
	u8 client_ver;
	u16 client_id;
	u32 client_pid;
	u32 client_second_pid;
	u32 pid;
	u32 second_pid;
	u32 pid_addr;
	u32 second_pid_addr;
	u32 pc_counter_addr;
	u32 pc_latch_addr;
	u32 reset_addr;
	u32 otp_addr;
	u32 ana_addr;
	u32 otp_id;
	u32 ana_id;
	u32 fw_ver;
	u32 core_ver;
	u32 fw_mp_ver;
	u32 max_count;
	u32 reset_key;
	u16 wtd_key;
	int no_bk_shift;
	bool dma_reset;
	int (*dma_crc)(u32 start_addr, u32 block_size);
	u8 product_id[8];
};

int ilitek_spi_probe(struct spi_device *client);
void ilitek_spi_remove(struct spi_device *spi);
int ilitek_plat_dev_init(struct spi_device *client);
int ili_set_bypass_mode(bool mcu);
int ili_core_spi_setup(int num);
void ili_get_dma1_config(struct ilitek_dma_config *dma);
void ili_set_dma1_config(struct ilitek_dma_config *dma);
int ili_ice_mode_ctrl_by_mode_spi(bool enable, bool mcu, int mode);
int ili_ice_client_write_register(u32 addr, u32 data, int len);
int ili_ice_both_write_register(u32 addr, u32 data, int len);
int ili_ice_master_write_register(u32 addr, u32 data, int len);
int ili_spi_ice_mode_read(u32 addr, u32 *data, int len, u8 msmode);
void ili_spi_recovery(void);
int ilitek_plat_probe(void);
int ilitek_plat_remove(void);
int ilitek_notifier_register(void);
void ilitek_notifier_unregister(void);
int ili_fw_dump_iram_data(u32 start, u32 end, bool mcu);
int ili_fw_upgrade(int op);
int ili_ic_whole_reset(bool mcu, bool withflash);
int ili_ic_func_ctrl(const char *name, int ctrl);
int ili_ic_func_ctrl_export(const char *name, int ctrl);
void ili_ic_func_ctrl_reset(void);
void ili_ic_get_pc_counter(int stat);
int ili_ic_check_int_level(bool level);
int ili_ic_check_int_pulse(bool pulse);
int ili_ic_get_panel_info(void);
int ili_ic_get_tp_info(void);
int ili_ic_get_core_ver(void);
int ili_ic_get_protocl_ver(void);
int ili_ic_get_fw_ver(void);
int ili_ic_get_info(void);
int ili_ic_get_all_info(void);
void ili_ic_get_report_info(void);
int ili_cascade_ic_get_info(bool enter_ice, bool exit_ice, bool mcu, bool reset);
int ili_cascade_reset_ctrl(int reset_mode, bool enter_ice);
void ili_wdt_reset_status( bool enter_ice, bool exit_ice, bool mcu);
int ili_cascade_rw_tp_reg(u32 mcu, u32 type, u32 addr, u32 wdata, u32 wlen, u32 *rdata, bool bypass_exit_ice);
int ili_cascade_rw_tp_reg_v2(u32 mcu, u32 type, u32 addr, u32 wdata, u32 wlen, u32 *rdata);
int ili_ic_dummy_check(void);
int ili_ice_mode_bit_mask_write(u32 addr, u32 mask, u32 value);
int ili_ice_mode_bit_mask_write_cascade(u32 addr, u32 mask, u32 value, int mode);
int ili_ice_mode_write(u32 addr, u32 data, int len);
void ili_cascade_sync_ctrl(bool mode);
int ili_ice_mode_write_by_mode(u32 addr, u32 data, int len,
					  int mode);
int ili_ice_mode_read_by_mode(u32 addr, u32 *data, int len,
					 int mode);
int ili_ice_mode_read(u32 addr, u32 *data, int len);
int ili_ice_mode_ctrl(bool enable, bool mcu);
void ili_ic_init(void);
/* Prototypes for tddi events */
#if RESUME_BY_DDI
void ili_resume_by_ddi(void);
#endif
int ili_switch_tp_mode(u8 data);
int ili_set_tp_data_len(int format, bool send, u8 *data);
int ili_set_pen_data_len(u8 header, u8 ctrl, u8 type);
int ili_fw_upgrade_handler(void *data);
int ili_sleep_handler(int mode);
int ili_reset_ctrl(int mode);
int ili_tddi_init(void);
void ili_dev_remove(void);
/* Prototypes for platform level */
void ili_tp_reset(void);
int ilitek_plat_gpio_register(void);
void ili_irq_disable(void);
void ili_irq_enable(void);
/* Prototypes for miscs */
void ili_node_init(void);
void ili_dump_data(void *data, int type, int len, int row_len,
			      const char *name);
u8 ili_calc_packet_checksum(u8 *packet, int len);
void ili_node_remove(struct ilitek_ts_hid_data *ts);


static inline void ipio_kfree(void **mem)
{
	if (*mem != NULL) {
		kfree(*mem);
		*mem = NULL;
	}
}

static inline void ipio_vfree(void **mem)
{
	if (*mem != NULL) {
		vfree(*mem);
		*mem = NULL;
	}
}

static inline void *ipio_memcpy(void *dest, const void *src, int n,
				int dest_size)
{
	if (n > dest_size)
		n = dest_size;

	return memcpy(dest, src, n);
}

static inline int ipio_strcmp(const char *s1, const char *s2)
{
	return (strlen(s1) != strlen(s2)) ? -1 : strncmp(s1, s2, strlen(s1));
}
#endif /* __HID_ILITEK_H */
