/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2025 MediaTek Inc.
 */

#ifndef __MTK_SLBC_SRAM_H__
#define __MTK_SLBC_SRAM_H__

#define SLBC_UID_USED                   0x0
#define SLBC_SID_MASK                   0x4
#define SLBC_SID_REQ_Q                  0x8
#define SLBC_SID_REL_Q                  0xC
#define SLBC_SLOT_USED                  0x10
#define SLBC_FORCE                      0x14
#define SLBC_BUFFER_REF                 0x18
#define SLBC_REF                        0x1C
#define SLBC_DEBUG_0                    0x20
#define SLBC_DEBUG_1                    0x24
#define SLBC_DEBUG_2                    0x28
#define SLBC_DEBUG_3                    0x2C
#define SLBC_DEBUG_4                    0x30
#define SLBC_DEBUG_5                    0x34
#define SLBC_DEBUG_6                    0x38
#define SLBC_DEBUG_7                    0x3C
#define SLBC_APU_BW                     0x40
#define SLBC_MM_BW                      0x44
#define SLBC_MM_EST_BW                  0x48
#define SLBC_CACHE_USED                 0x4C
#define SLBC_PMU_0                      0x50
#define SLBC_PMU_1                      0x54
#define SLBC_PMU_2                      0x58
#define SLBC_PMU_3                      0x5C
#define SLBC_PMU_4                      0x60
#define SLBC_PMU_5                      0x64
#define SLBC_PMU_6                      0x68
#define SLBC_SCMI_AP                    0x6C
#define SLBC_SCMI_SSPM                  0x70
#define SLBC_SCMI_RET1                  0x74
#define SLBC_SCMI_RET2                  0x78
#define SLBC_SCMI_RET3                  0x7C
#define SLBC_SRAM_CON                   0x80
#define SLBC_L3CTL                      0x84
#define SLBC_CPU_DEBUG0                 0x88
#define SLBC_CPU_DEBUG1                 0x8C
#define SLBC_STA                        0x90
#define SLBC_ACK_C                      0x94
#define SLBC_ACK_G                      0x98
#define CPUQOS_MODE                     0x9C
#define SLBC_DEBUG_8                    0xA0
#define SLBC_DEBUG_9                    0xA4
#define SLBC_DEBUG_10                   0xA8
#define SLBC_DEBUG_11                   0xAC
#define SLBC_DEBUG_12                   0xB0
#define SLBC_DEBUG_13                   0xB4
#define SLBC_DEBUG_14                   0xB8
#define SLBC_DEBUG_15                   0xBC
#define SLBC_UID_USED2                  0xC0
#define SLBC_SCMI_RET4                  0xC4
#define SLBC_SCMI_RET_VAL               0xC8
#define SLBC_TOTAL_CEIL                 0xCC
#define SLBC_CG_PRIORITY                0xD0
#define SLBC_DCC_COUNT                  0xD4
#define SLBC_DCC_CTRL                   0xD8

#endif
