/* SPDX-License-Identifier: GPL-2.0-only OR BSD-2-Clause */
/*
 * Copyright (C) 2026 Altera Corporation. All rights reserved
 *
 */

#ifndef _DT_BINDINGS_RESET_ALTR_RST_MGR_AGILEX72_H
#define _DT_BINDINGS_RESET_ALTR_RST_MGR_AGILEX72_H

/* PER0MODRST */
#define EMAC0_RESET           32
#define EMAC1_RESET           33
#define EMAC2_RESET           34
#define USB20TG_RESET         35
#define USB31_RESET           36
#define USB31PHY_RESET        37
#define SDMMC0_SOFTPHY_RESET  38
#define SDMMC0_RESET          39
#define SDMMC1_SOFTPHY_RESET  40
#define SDMMC1_RESET          41
/* 42-47 reserved */
#define DMA_RESET             48
#define SPIM0_RESET           49
#define SPIM1_RESET           50
#define SPIS0_RESET           51
#define SPIS1_RESET           52
/* 53 reserved */
#define EMAC_PTP_RESET        54
/* 55 reserved */
#define DMAIF0_RESET          56
#define DMAIF1_RESET          57
#define DMAIF2_RESET          58
#define DMAIF3_RESET          59
#define DMAIF4_RESET          60
#define DMAIF5_RESET          61
#define DMAIF6_RESET          62
#define DMAIF7_RESET          63

/* PER1MODRST */
#define WATCHDOG0_RESET       64
#define WATCHDOG1_RESET       65
#define WATCHDOG2_RESET       66
#define WATCHDOG3_RESET       67
#define WATCHDOG4_RESET       68
#define L4SYSTIMER0_RESET     69
#define L4SYSTIMER1_RESET     70
#define SPTIMER0_RESET        71
#define SPTIMER1_RESET        72
#define I2C0_RESET            73
#define I2C1_RESET            74
#define I2C_EMAC0_RESET       75
#define I2C_EMAC1_RESET       76
#define I2C_EMAC2_RESET       77
#define I3C0_RESET            78
#define I3C1_RESET            79
/* 80 reserved */
#define UART0_RESET           81
#define UART1_RESET           82
#define UART2_RESET           83
/* 84-87 reserved */
#define GPIO0_RESET           88
#define GPIO1_RESET           89
#define OSPISYS_RESET         90
#define OSPIREG_RESET         91
#define OSPIPHY_RESET         92

/* BRGMODRST */
#define FPGA2SOC_RESET        98

/* DBGMODRST */
#define DBG_RESET             224

#endif
