/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * ILITEK Touch IC driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __HID_ILITEK_H
#define __HID_ILITEK_H

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <linux/device.h>
#include <linux/vmalloc.h>
#include <linux/jiffies.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <linux/firmware.h>

#ifdef CONFIG_OF
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#endif

/* define names and paths for the variety of tp modules */
#define DEF_FW_FILP_PATH		"/sdcard/ILITEK_FW"
#define DEF_FW_REQUEST_PATH "ilitek_fff1.bin"
extern unsigned char CTPM_FW_DEF[];

#define UPDATE_PASS 0
#define UPDATE_FAIL -1

#define PROTOCL_VER_NUM 8

#define DRIVER_VERSION "3.0.11.0.260127"

#define ENABLE 1
#define DISABLE 0
#define K (1024)
#define M (K * K)
#define START 1
#define ON 1
#define END 0
#define OFF 0

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
#define SPI_CLK 4 /* follow by clk list */
#define SPI_WRITE 0x82
#define SPI_WRITE_CLIENT0 0x87
#define SPI_READ 0x83
#define SPI_ACK 0xA3
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
#define TR_BUF_SIZE (6 * K) /* Buffer size of touch report */
#define SPI_TX_BUF_SIZE (6*K)
#define SPI_RX_BUF_SIZE (10*K)
#define AP_INT_TIMEOUT 1000	/*1s*/
#define MP_INT_TIMEOUT 5000	/*5s*/
#define BOOT_FW_UPDATE ENABLE
#define PLL_CLK_WAKEUP_TP_RESUME DISABLE
#define ENGINEER_FLOW ENABLE

/*if current interface is spi, must to hostdownload */
#define HOST_DOWN_LOAD ENABLE

/* Debug messages */
#define DEBUG_NONE 0
#define DEBUG_ALL 1
#define DEBUG_OUTPUT DEBUG_NONE

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
#define DO_SPI_RECOVER -2

/* Cascade mode */
enum CASCADE_MODE {
	MASTER = 0,
	CLIENT,
	BOTH,
};

enum TP_RST_METHOD {
	TP_IC_WHOLE_RST_WITH_FLASH = 0,
	TP_IC_WHOLE_RST_WITHOUT_FLASH,
	TP_IC_CODE_RST,
	TP_HW_RST_ONLY,
};

enum TP_FW_UPGRADE_STATUS {
	FW_STAT_INIT = 0,
	FW_UPDATING = 90,
	FW_UPDATE_PASS = 100,
	FW_UPDATE_FAIL = -1
};
enum TP_FW_OPEN_METHOD {
	REQUEST_FIRMWARE = 0,
};

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

enum pen_data_mode {
	HAND_PEN_DEMO_MODE = 0,
	HAND_PEN_SIGNAL_MODE,
	HAND_PEN_RAW_DATA_MODE,
	PEN_ONLY_SIGNAL_MODE,
	PEN_ONLY_RAW_DATA_MODE,
	HAND_ONLY_SIGNAL_MODE,
	HAND_ONLY_RAW_DATA_MODE,
};

enum report_type {
	P5_X_HAND_PEN_TYPE = 0,   /* 0x39: FigType 0-2 bits, CustomType 3-5, PenType 6-7 */
	P5_X_ONLY_PEN_TYPE = 1,  /* 0x3F */
	P5_X_ONLY_HAND_TYPE = 2, /* 0xF9 */
};

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
	enum report_type report_type;
	enum pen_data_mode pen_data_mode;
};

struct ilitek_delayed_probe {
	struct spi_device *spi;
	struct delayed_work dwork;
	bool core_probed;
};

/* Config structure is filled with data from Device Tree */
struct spi_hid_of_ilitek_config {
	struct spihid_ops ops;

	struct spi_hid_conf property_conf;
	u32 post_power_on_delay_ms;
	u32 minimal_reset_delay_ms;
	struct gpio_desc *reset_gpio;
	struct regulator *supply;
	bool supply_enabled;
};

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
#define ILI_FILE_HEADER 256
#define DLM_START_ADDRESS 0x20610
#define FW_BLOCK_INFO_NUM 17
#define FW_BLOCK_INFO_B1_NUM 4
#define DEFINED_MODE_NUM 3
#if SPI_DMA_TRANSFER_SPLIT
#define SPI_UPGRADE_LEN	2048
#else
#define SPI_UPGRADE_LEN	(16*K)
#endif
#define INFO_HEX_ST_ADDR 0x4F
#define INFO_MP_HEX_ADDR 0x1F
#define INFO_PEN_ST_ADDR 0x67
#define INFO_CASCADE_ST_ADDR 0x6B

/* Dummy Registers */
#define WDT_DUMMY_BASED_ADDR 0x5101C
#define WDT9_DUMMY2 (WDT_DUMMY_BASED_ADDR + 0x08)

/* Cascade Register */
#define DMA_CRC_ADDR 0x4101C
#define ICE_HEADER_REG 0x44000

#define MS_MISO_SEL_REG 0x62021
#define MSPI_REG 0x62084
#define MSPI_reg_bypass_off		BIT(0)
#define SINGLE_TO_QUAL_REG 0x6202C
#define CMD_MODE_EN_REG 0x6202C

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

#define P5_X_CORE_VER_FOUR_LENGTH 5

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
#define P5_X_MODE_CONTROL 0xF0
#define P5_X_NEW_CONTROL_FORMAT 0xF2
#define P5_X_SET_CDC_INIT 0xF1
#define P5_X_GET_CDC_DATA 0xF2
#define P5_X_FW_AP_MODE 0x00
#define P5_X_FW_TEST_MODE 0x01
#define P5_X_FW_GESTURE_MODE 0x0F
#define P5_X_FW_SIGNAL_DATA_MODE 0x03

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

/* Report Format Resolution */
#define POSITION_LOW_RESOLUTION 0X00
#define POSITION_CUSTOMER_TYPE_OFF 0x1F
#define POSITION_PEN_TYPE_ON 0x00
#define POSITION_PEN_TYPE_OFF 0x03
#define POSITION_CUSTOMER_TYPE_OFF_3BITS 0x07	/*core ver 1700, CustomerType 3 bits*/

#define EDGE_DELAY_FOR_FAST_ENTER_ICE 10

struct ilitek_ts_hid_data {
	struct spi_device *spi;
	struct device *dev;
	struct wakeup_source *ili_upg_wakelock;

	struct ilitek_ic_info *chip;
	struct ilitek_protocol_info *protocol;
	spinlock_t irq_spin;

	bool boot;

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
	int tp_data_format;
	int wait_int_timeout;
	struct gpio_desc *reset_gpiod;
	u8 customertype_off;

	/* pen report */
	u8 PenType;
	struct pen_info_block pen_info_block;
	struct ilitek_pen_info pen_info;
	u8 touch_num;

	int fw_update_stat;
	int fw_open;
	u8 fw_info[75];
	u8 fw_mp_ver[4];

	/* Cascade */
	struct cascade_info_block cascade_info_block;

	bool knuckle;
	int gesture_mode;
	struct report_info_block rib;

	wait_queue_head_t inq;
	struct ilitek_delayed_probe dp;
	struct spi_hid_of_ilitek_config config;

	int reset;
	int rst_edge_delay;
	bool force_fw_update;
	bool ddi_rest_done;
	bool tp_suspend;
	bool gesture_load_code;
	bool trans_xy;
	bool node_update;
	bool int_pulse;
	bool pll_clk_wakeup;
	bool eng_flow;
	bool skip_sync_cmd;
	bool change_ice_key;
	bool fast_enter_ice_mode;
	u8 spi_ms_mode;
	/* module info */
	int tp_module;
	int md_fw_ili_size;
	char *md_name;
	char *md_fw_rq_path;
	u8 *md_fw_ili;
	atomic_t irq_stat;
	atomic_t tp_reset;
	atomic_t ice_stat;
	atomic_t fw_stat;
	atomic_t mp_stat;
	atomic_t tp_sw_mode;
	atomic_t cmd_int_check;
	atomic_t ignore_report;
	atomic_t stop_sync_stat;
	atomic_t spi_slave_write_mcu_on;

	int (*spi_write_then_read)(struct spi_device *spi, const void *txbuf,
		unsigned int n_tx, void *rxbuf, unsigned int n_rx);
	int (*wrapper)(u8 *wdata, u32 wlen, u8 *rdata, u32 rlen, bool spi_irq,
		bool i2c_irq);
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
int ilitek_plat_probe(void);
int ilitek_plat_remove(void);
int ili_fw_dump_iram_data(u32 start, u32 end, bool mcu);
int ili_fw_upgrade(int op);
int ili_ic_whole_reset(bool mcu, bool withflash);
int ili_ic_func_ctrl(const char *name, int ctrl);
int ili_ic_func_ctrl_export(const char *name, int ctrl);
void ili_ic_func_ctrl_reset(void);
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
int ili_switch_tp_mode(u8 data);
int ili_set_tp_data_len(int format, bool send, u8 *data);
int ili_fw_upgrade_handler(void *data);
int ili_reset_ctrl(int mode);
int ili_tddi_init(void);
void ili_dev_remove(void);
/* Prototypes for platform level */
void ili_tp_reset(void);
int ilitek_plat_gpio_register(void);

/* Prototypes for miscs */
u8 ili_calc_packet_checksum(u8 *packet, int len);

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
