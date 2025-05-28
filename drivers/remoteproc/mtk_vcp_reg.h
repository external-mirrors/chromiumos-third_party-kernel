/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 MediaTek Inc.
 */

#ifndef __MTK_VCP_REG_H
#define __MTK_VCP_REG_H

#define VCP_R_CORE0_SW_RSTN_SET         (0x0004)
#define VCP_R_CORE1_SW_RSTN_SET         (0x000C)
#define R_GIPC_IN_SET                   (0x0028)
#define R_GIPC_IN_CLR                   (0x002C)
#define GIPC_MMUP_SHUT                  (1 << 10)
#define GIPC_VCP_HART0_SHUT             (1 << 14)
#define B_GIPC4_SETCLR_3                (1 << 19)
#define R_CORE0_WDT_IRQ                 (0x0050)
#define R_CORE1_WDT_IRQ                 (0x0054)
#define B_WDT_IRQ                       (1 << 0)
#define AP_R_GPR2                       (0x0068)
#define B_CORE0_SUSPEND                 (1 << 0)
#define B_CORE0_RESUME                  (1 << 1)
#define AP_R_GPR3                       (0x006C)
#define B_CORE1_SUSPEND                 (1 << 0)
#define B_CORE1_RESUME                  (1 << 1)

#define R_CORE0_STATUS                  (0x6070)
#define B_CORE_GATED                    (1 << 0)
#define B_HART0_HALT                    (1 << 1)
#define B_HART1_HALT                    (1 << 2)
#define B_CORE_AXIS_BUSY                (1 << 4)
#define R_CORE1_STATUS                  (0x9070)
#define VCP_C0_GPR0_SUSPEND_RESUME_FLAG (0x6040)
#define VCP_C0_GPR1_DRAM_RESV_ADDR      (0x6044)
#define VCP_C0_GPR2_DRAM_RESV_SIZE      (0x6048)
#define VCP_C0_GPR3_DRAM_RESV_LOGGER    (0x604C)
#define VCP_C0_GPR5_H0_REBOOT           (0x6054)
#define CORE_RDY_TO_REBOOT              (0x0034)
#define VCP_C0_GPR6_H1_REBOOT           (0x6058)
#define VCP_C1_GPR0_SUSPEND_RESUME_FLAG (0x9040)
#define VCP_C1_GPR1_DRAM_RESV_ADDR      (0x9044)
#define VCP_C1_GPR2_DRAM_RESV_SIZE      (0x9048)
#define VCP_C1_GPR3_DRAM_RESV_LOGGER    (0x904C)
#define VCP_C1_GPR5_H0_REBOOT           (0x9054)
#define VCP_C1_GPR6_H1_REBOOT           (0x9058)

/* sec GPR */
#define R_GPR2_CFGREG_SEC               (0x0028)
#define MMUP_AP_SUSPEND                 (1U << 0)
#define R_GPR3_CFGREG_SEC               (0x002C)
#define VCP_AP_SUSPEND                  (1U << 0)

/* vcp rdy */
#define VLP_AO_RSVD7                    (0x0000)
#define READY_BIT                       (1U << 1)

#endif
