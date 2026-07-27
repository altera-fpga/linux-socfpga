/* SPDX-License-Identifier: GPL-2.0 */
/* Altera FPGA HSSI SS driver
 * Copyright (C) 2022, 2026 Altera Corporation. All rights reserved
 *
 * Contributors:
 *   Subhransu S. Prusty
 *   Preetam Narayan
 */
#ifndef __ALTERA_FPGA_HSSISS_CSR_H__
#define __ALTERA_FPGA_HSSISS_CSR_H__

#include "../altera_utils.h"

#define HSSISS_FTILE 2
#define HSSISS_ETILE 1

/* Registers and macros */

/* csr_offset = value@csr_addr_offset + offset
 * In v0: csr_addr_offset = 0
 * In v5: read from feature_csr_addr register
 *
 * eth_port_sts = 0x68 + X (0x00 .. 0x0F)*4 + CSR_ADDROFF
 */
#define HSSISS_CSR_VER                          0x8
#define HSSISS_CSR_COMMON_FEATURE_LIST          0xc
/* Port attr For E-tile: 0x10 + x * 4 + CSR_ADDROFF */
#define HSSISS_CSR_INTER_ATTRIB_PORT            0x10
#define HSSISS_CSR_CMDSTS                       0x50
#define HSSISS_CSR_CTRLADDR                     0x54
#define HSSISS_CSR_RD_DATA                      0x58
#define HSSISS_CSR_WR_DATA                      0x5C
#define HSSISS_CSR_GMII_TX_LATENCY              0x60
#define HSSISS_CSR_GMII_RX_LATENCY              0x64
#define HSSISS_CSR_ETH_PORT_STS                 0x68
#define HSSISS_CSR_TSE_CTRL                     0xa8
#define HSSISS_CSR_DBG_CTRL                     0xb0
#define HSSISS_CSR_HOTPLUG_DBG_CTRL             0xb4
#define HSSISS_CSR_HOTPLUG_DBG_STS              0xb8
#define HSSISS_CSR_GENERAL_STATUS               0xbc

/*F-tile specific */
#define HSSISS_CSR_INTER_ATTRIB_PORT_FHT        0x300 /* + x * 4 */
#define HSSISS_CSR_ETH_PORT_STS_FHT             0x200 /* + x * 4 */

/* Ftile only: PFC offset + x * 4, read returns 0 if port doesn't exit */
#define HSSISS_CSR_PFC_CTRL                     0x400
#define HSSISS_CSR_PFC_ERR_STS                  0x450
#define HSSISS_CSR_PFC_RX_PARSE_CFG             0x4A0

/* Ftile only: ANLT: ANLTx = ANLT_BASE + x * RANGE */
#define HSSISS_CSR_ANLT_BASE                    0x10000
#define HSSISS_CSR_ANLT_RANGE                   0x400

/* Ftile only: PTP tile adaptor */
#define HSSISS_CSR_PTP_ASYMMETRY_BASE           0x20000
#define HSSISS_CSR_PTP_ASYMMETRY_RANGE          0x20000
#define HSSISS_CSR_PTP_PEER_TO_PEER_MPD         0x40000
#define HSSISS_CSR_PTP_PEER_TO_PEER_MPD_RANGE   0x20000

/* DFH */
#define HSSISS_DFHLO_DFHV0_FEA_REV_MASK         GENMASK(15, 12)
#define HSSISS_DFHLO_DFHV0_FEA_REV_SHIFT        12

/* Command status bits */
#define HSSI_SAL_CMDSTS_RD			BIT(0)
#define HSSI_SAL_CMDSTS_WR			BIT(1)
#define HSSI_SAL_CMDSTS_ACK			BIT(2)
#define HSSI_SAL_CMDSTS_BUSY			BIT(3)
#define HSSI_SAL_CMDSTS_ERR			BIT(4)
#define HSSI_SAL_CMDSTS_REG_OFFS_MASK		GENMASK(6, 5)
#define HSSI_SAL_CMDSTS_REG_OFFS_SHIFT		5

/* Control address bits */
#define HSSI_SAL_CTRLADDR_SALCMD		0xFF
#define HSSI_SAL_CTRLADDR_PORT_SHIFT		8
#define HSSI_SAL_CTRLADDR_COUNTER_SHIFT		16
#define HSSI_SAL_CTRLADDR_LSB_SHIFT		31
#define HSSI_SAL_CTRLADDR_ADDRBITS_MASK		0xFFFFF
#define HSSI_SAL_CTRLADDR_ADDRBITS_SHIFT	8
#define HSSI_SAL_CTRLADDR_TX			BIT(16)
#define HSSI_SAL_CTRLADDR_RX			BIT(17)

#define HSSI_HOTPLUG_DBG_STS_DISABLE_SHIFT	4

/* Feature CSR v5 only */
#define HSSISS_FEATURE_CSR_ADDR_MASK		GENMASK(31, 1)
#define HSSISS_FEATURE_CSR_ADDR_SHIFT		1

/* Bit index and mask */
#define HSSI_SAL_RESET_MAC_STAT_TX		BIT(16)
#define HSSI_SAL_RESET_MAC_STAT_RX		BIT(17)
#define HSSI_HOTPLUG_DBG_CTRL_DISABLE           BIT(0)

#define DR_GRP_INDEX				4
#define HSSI_DR_GRP_MASK			GENMASK(6, 4)
#define HSSI_DR_PROFILE_MASK			GENMASK(3, 0)
#define HSSISS_VER_CSR_ADDR_MASK		GENMASK(31, 16)
#define HSSISS_VER_CSR_ADDR_SHIFT		16

#define HSSISS_MTU_TX_FRAME_SIZE_SHIFT		16

/*
 * Bestcase: 100ns, max: 10ms, driver interval: 10us
 * For DR and enable/disable loopback SAL sequences, the whole operation might
 * take more than 10ms and timeout doesn't apply for these sequences, instead
 * polling method is implemeted where a polling counter is used to poll the DR
 * status and it will exit error when the polling counter expires.
 */
#define FW_ACK_POLL_INTERVAL_US			10
#define FW_ACK_POLL_TIMEOUT_US			10000

/* CSR read/write macros */
#define csrrd32_direct_withoffset(hssi, offs) \
	csrrd32((hssi)->sscsr, (offs))

#define csrwr32_direct_withoffset(hssi, val, offs) \
	csrwr32((val), (hssi)->sscsr, (offs))

#define csrrd32_withoffset(hssi, offs) \
({ \
	typeof(hssi) __hssi = (hssi); \
	csrrd32((__hssi)->sscsr, (offs) + (__hssi)->csr_addroff); \
})

#define csrwr32_withoffset(hssi, val, offs) \
({ \
	typeof(hssi) __hssi = (hssi); \
	csrwr32((val), (__hssi)->sscsr, (__hssi)->csr_addroff + (offs)); \
})

#define hssiss_mask(idx, nr) \
({ \
	typeof(idx) __idx = (idx); \
	GENMASK((__idx), (__idx) + 1 - (nr)); \
})

#define test_reg_bits(val, idx, numbits) \
	(!!((val) & hssiss_mask((idx), (numbits))))

#endif /* __ALTERA_FPGA_HSSISS_CSR_H__ */
