/* SPDX-License-Identifier: GPL-2.0-only OR BSD-2-Clause */
/*
 * Copyright (C) 2026, Altera Corporation
 */

#ifndef __DT_BINDINGS_ALTR_AGILEX72_CLKMGR_H
#define __DT_BINDINGS_ALTR_AGILEX72_CLKMGR_H

/* fixed rate clocks */
#define AGILEX72_OSC1                  0
#define AGILEX72_CB_INTOSC_DIV2_CLK    1
#define AGILEX72_CB_INTOSC_DIV10_CLK   2
#define AGILEX72_F2S_FREE_CLK          3

/* gppll clocks */
#define AGILEX72_GPPLL0_CLK            4
#define AGILEX72_GPPLL1_CLK            5
#define AGILEX72_GPPLL2_CLK            6
#define AGILEX72_GPPLL0_C0_CLK         7
#define AGILEX72_GPPLL0_C1_CLK         8
#define AGILEX72_GPPLL0_C2_CLK         9
#define AGILEX72_GPPLL0_C3_CLK         10
#define AGILEX72_GPPLL0_C4_CLK         11
#define AGILEX72_GPPLL0_C5_CLK         12
#define AGILEX72_GPPLL0_C6_CLK         13
#define AGILEX72_GPPLL1_C0_CLK         14
#define AGILEX72_GPPLL1_C1_CLK         15
#define AGILEX72_GPPLL2_C0_CLK         16
#define AGILEX72_GPPLL2_C1_CLK         17

#define AGILEX72_BOOT_CLK              18

/* fixed factor clocks */
#define AGILEX72_COMP0_FREE_CLK        19
#define AGILEX72_CORE2_FREE_CLK        20
#define AGILEX72_CORE3_FREE_CLK        21
#define AGILEX72_DSU_FREE_CLK          22
#define AGILEX72_CCU_FREE_CLK          23
#define AGILEX72_HSP_NOC_FREE_CLK      24
#define AGILEX72_LSP_NOC_FREE_CLK      25
#define AGILEX72_TRACE_FREE_CLK        26
#define AGILEX72_EMAC_A_FREE_CLK       27
#define AGILEX72_EMAC_B_FREE_CLK       28
#define AGILEX72_EMAC_PTP_FREE_CLK     29
#define AGILEX72_GPIO_DB_FREE_CLK      30
#define AGILEX72_USB31_FREE_CLK        31
#define AGILEX72_S2F_USER0_FREE_CLK    32
#define AGILEX72_S2F_USER1_FREE_CLK    33
#define AGILEX72_XSPI_PHY_FREE_CLK     34
#define AGILEX72_MEMDEVICE_PHY_FREE_CLK 35

/* Gate clocks */
#define AGILEX72_COMP0_CLK             36
#define AGILEX72_CORE2_CLK             37
#define AGILEX72_CORE3_CLK             38
#define AGILEX72_MPU_CLK               39
#define AGILEX72_CCU_CLK               40
#define AGILEX72_APU_SYS_FREE_CLK      41
#define AGILEX72_HSP_SYS_FREE_CLK      42
#define AGILEX72_HSP_MAIN_FREE_CLK     43
#define AGILEX72_HSP_MAIN_CLK          44
#define AGILEX72_HSP_MP_CLK            45
#define AGILEX72_HSP_SP_CLK            46
#define AGILEX72_USB2OTG_HCLK          47
#define AGILEX72_LSP_SYS_FREE_CLK      48
#define AGILEX72_LSP_MAIN_FREE_CLK     49
#define AGILEX72_LSP_MAIN_CLK          50
#define AGILEX72_LSP_MP_CLK            51
#define AGILEX72_LSP_SP_CLK            52
#define AGILEX72_SPIM_0_CLK            53
#define AGILEX72_SPIM_1_CLK            54
#define AGILEX72_SPIS_0_CLK            55
#define AGILEX72_SPIS_1_CLK            56
#define AGILEX72_DMA_0_CORE_CLK        57
#define AGILEX72_DMA_0_HS_CLK          58
#define AGILEX72_DMA_1_CORE_CLK        59
#define AGILEX72_DMA_1_HS_CLK          60
#define AGILEX72_I3C_0_CORE_CLK        61
#define AGILEX72_I3C_1_CORE_CLK        62
#define AGILEX72_I2C_0_PCLK            63
#define AGILEX72_I2C_1_PCLK            64
#define AGILEX72_I2C_EMAC0_PCLK        65
#define AGILEX72_I2C_EMAC1_PCLK        66
#define AGILEX72_I2C_EMAC2_PCLK        67
#define AGILEX72_UART_0_PCLK           68
#define AGILEX72_UART_1_PCLK           69
#define AGILEX72_UART_2_PCLK           70
#define AGILEX72_SPTIMER_0_PCLK        71
#define AGILEX72_SPTIMER_1_PCLK        72
#define AGILEX72_CS_AT_CLK             73
#define AGILEX72_CS_PDBG_CLK           74
#define AGILEX72_CS_TRACE_CLK          75
#define AGILEX72_EMACA_DIV_CLK         76
#define AGILEX72_EMACB_DIV_CLK         77
#define AGILEX72_EMAC0_CLK             78
#define AGILEX72_EMAC1_CLK             79
#define AGILEX72_EMAC2_CLK             80
#define AGILEX72_EMAC_PTP_CLK          81
#define AGILEX72_GPIO_DB_CLK           82
#define AGILEX72_USB31_SUSPEND_CLK     83
#define AGILEX72_USB31_BUS_CLK_EARLY   84
#define AGILEX72_S2F_USER0_CLK         85
#define AGILEX72_S2F_USER1_CLK         86
#define AGILEX72_XSPI_PCLK             87
#define AGILEX72_XSPI_CLK              88
#define AGILEX72_XSPI_PHY_CLK          89
#define AGILEX72_SDMMC0_SDPHY_REG_CLK  90
#define AGILEX72_SDMMC1_SDPHY_REG_CLK  91
#define AGILEX72_SDMMC0_SDMCLK         92
#define AGILEX72_SDMMC1_SDMCLK         93
#define AGILEX72_SDMMC0_PHY_CLK        94
#define AGILEX72_SDMMC1_PHY_CLK        95
#define AGILEX72_USB31_REF_CLK         96
#define AGILEX72_NUM_CLKS              97

#endif  /* __DT_BINDINGS_ALTR_AGILEX72_CLKMGR_H */
