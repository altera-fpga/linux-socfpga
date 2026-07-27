/* SPDX-License-Identifier: GPL-2.0 */
/* Altera FPGA HSSI SS interface driver
 * Copyright (C) 2022, 2026 Altera Corporation. All rights reserved
 *
 * Contributors:
 *   Subhransu S. Prusty
 *   Preetam Narayan
 */

#ifndef __ALTERA_HSSISS_H__
#define __ALTERA_HSSISS_H__

#include <linux/platform_device.h>
#include "../altera_eth_intf.h"

enum altera_lane_speed {
	LANE_10G,
	LANE_25G,
	LANE_50G,
	LANE_100G
};

enum altera_hssiss_sal_usrcmd {
	SAL_NOP,
	SAL_GET_HSSI_PROFILE,
	SAL_SET_HSSI_PROFILE,
	SAL_READ_MAC_STAT,
	SAL_GET_MTU,
	SAL_SET_CSR,
	SAL_GET_CSR,
	SAL_ENABLE_LOOPBACK,
	SAL_DISABLE_LOOPBACK,
	SAL_RESET_MAC_STAT,
	SAL_SET_MTU,
	SAL_NCSI_GET_LINK_STS,
	SAL_RSVD,
	SAL_FW_VERSION,

};

enum altera_hssiss_mac_stat_counter_type {
#define HSSISS_MAC_STATS_MAX	(MACSTAT_RX_RUNTS + 1)
	MACSTAT_TX_PACKETS,
	MACSTAT_RX_PACKETS,
	MACSTAT_RX_CRC_ERRORS,
	MACSTAT_RX_ALIGN_ERRORS,
	MACSTAT_TX_BYTES,
	MACSTAT_RX_BYTES,
	MACSTAT_TX_PAUSE,
	MACSTAT_RX_PAUSE,
	MACSTAT_RX_ERRORS,
	MACSTAT_TX_ERRORS,
	MACSTAT_RX_UNICAST,
	MACSTAT_RX_MULTICAST,
	MACSTAT_RX_BROADCAST,
	MACSTAT_TX_DISCARDS,
	MACSTAT_TX_UNICAST,
	MACSTAT_TX_MULTICAST,
	MACSTAT_TX_BROADCAST,
	MACSTAT_ETHER_DROPS,
	MACSTAT_RX_TOTAL_BYTES,
	MACSTAT_RX_TOTAL_PACKETS,
	MACSTAT_RX_UNDERSIZE,
	MACSTAT_RX_OVERSIZE,
	MACSTAT_RX_64_BYTES,
	MACSTAT_RX_65_127_BYTES,
	MACSTAT_RX_128_255_BYTES,
	MACSTAT_RX_256_511_BYTES,
	MACSTAT_RX_512_1023_BYTES,
	MACSTAT_RX_1024_1518_BYTES,
	MACSTAT_RX_GTE_1519_BYTES,
	MACSTAT_RX_JABBERS,
	MACSTAT_RX_RUNTS,
};

struct altera_hssiss_salcmd {
	enum altera_hssiss_sal_usrcmd cmd;
	u32 cmdid;
	char name[32];
};

struct get_set_dr_data {
	u32 addr_offs;
	u32 val;
};

union ncsi_link_status_data {
	struct {
		u32 link_flag:1;
		u32 speed_duplex:4;
		u32 auto_neg_flag:1;
		u32 auto_neg_complete:1;
		u32 parallel_det_flag:1;
		u32 res:1;
		u32 link_partner_advert_speed_duplex_100TFD:1;
		u32 link_partner_advert_speed_duplex_100THD:1;
		u32 link_partner_advert_speed_duplex_100T4:1;
		u32 link_partner_advert_speed_duplex_100TXFD:1;
		u32 link_partner_advert_speed_duplex_100TXHD:1;
		u32 link_partner_advert_speed_duplex_10TFD:1;
		u32 link_partner_advert_speed_duplex_10THD:1;
		u32 tx_flow_ctrl:1;
		u32 rx_flow_ctrl:1;
		u32 link_partner_advert_flow_ctrl:2;
		u32 serdes_link:1;
		u32 oem_link_speed_valid:1;
	} part;
	u32 full;		/* used for both port number and return data */
};

union eth_port_status {
	struct {
		u32 o_ehip_ready:1;
		u32 o_rx_hi_ber:1;
		u32 o_cdr_lock:1;
		u32 rx_am_lock:1;
		u32 rx_block_lock:1;
		u32 link_fault_gen_en:1;
		u32 unidirectional_en:1;
		u32 local_fault_status:1;
		u32 remote_fault_status:1;
		u32 unidirectional_force_remote_faul:1;
		u32 unidirectional_remote_fault_dis:1;
		u32 pcs_eccstatus:2;
		u32 mac_eccstatus:2;
		u32 set_10:1;
		u32 set_1000:1;
		u32 ena_10:1;
		u32 eth_mode:1;
		u32 load_recipe_error:1;
		u32 ical_pcal_errors:1;
		u32 tx_lanes_stable:1;
		u32 rx_pcs_ready:1;
		u32 tx_pll_locked:1;
		u32 ptp_tx_pll_locked:1;
		u32 reserved:5;
	} part;
	u32 full;
};

union eth_port_attr {
	struct {
		u32 profile:6;
		u32 rdy_latency:4;
		u32 data_bus_width:3;
		u32 low_speed_eth:2;
		u32 dr_en:1;
		u32 sub_profile:5;
		u32 rsfec_en:1;
		u32 anlt_en:1;
		u32 ptp_en:1;
		u32 reserved:8;
	} part;
	u32 full;
};

enum hssi_port_profile {
	HSSI_PORT_PROFILE_10GBE = 20,
	HSSI_PORT_PROFILE_25GBE,
	HSSI_PORT_PROFILE_40GCAUI_4,
	HSSI_PORT_PROFILE_50GAUI_2,
	HSSI_PORT_PROFILE_50GAUI_1,
	HSSI_PORT_PROFILE_100GAUI_1,
	HSSI_PORT_PROFILE_100GAUI_2,
	HSSI_PORT_PROFILE_100GCAUI_4,
	HSSI_PORT_PROFILE_200GAUI_2,
	HSSI_PORT_PROFILE_200GAUI_4,
	HSSI_PORT_PROFILE_200GAUI_8,
	HSSI_PORT_PROFILE_400GAUI_4,
	HSSI_PORT_PROFILE_400GAUI_8,
};

/* CAUTION: Do not change the order of the enum, maintaining the order is
 * important for backward compatibility
 */
enum altera_hssiss_tile_regbank {
	HSSI_ETH_RECONFIG,
	HSSI_RSFEC,
	HSSI_PHY_XCVR_PMACAP,
	HSSI_PHY_XCVR_PMAAVMM,
	HSSI_SOFTIP,
	HSSI_PTP_PACKET_CLASSIFIER,
	HSSI_RSVD,
};

struct get_set_csr_data {
	u32 offs;
	u32 data; /* wr for set_csr, read for get_csr */
	unsigned int ch;
	enum altera_hssiss_tile_regbank reg_type;
	bool word;
};

union altera_hssiss_feature_list {
	struct {
		u32 axi4_support:1;
		u32 num_ports:5;
		u32 port_enable_mask:16;
		u32 res:12;
	} part;
	u32 full;
};

union altera_hssiss_cmd_sts {
	struct {
		u32 rdcmd:1;
		u32 wrcmd:1;
		u32 ack:1;
		u32 busy:1;
		u32 err:1;
		u32 regoffset:2; /* Applicable only on e-tile */
		u32 rsvd:25;
	} part;
	u32 full;
};

union altera_hssiss_ctrl_addr {
	struct {
		u32 salcmd:8;
		u32 port_addr:4;
		u32 ch_addr:4;
		u32 misc:16;
	} part;
	u32 full;
};

struct altera_hssiss_csr_v5_only {
	u32 dfh_lo;			/* 0x0 */
	u32 dfh_hi;			/* 0x4 */
	u32 feature_guid_lo_lsb;	/* 0x8 */
	u32 feature_guid_lo_msb;	/* 0xc */
	u32 feature_guid_hi_lsb;	/* 0x10 */
	u32 feature_guid_hi_msb;	/* 0x14 */
	u32 feature_csr_addr_lsb;	/* 0x18 */
	u32 feature_csr_addr_msb;	/* 0x1c */
	u32 feature_csr_size_lsb;	/* 0x20 */
	u32 feature_csr_size_msb;	/* 0x24 */
};

#define feature_offs(x) (offsetof(struct altera_hssiss_csr_v5_only, x))

enum access_type {
	BYTE_ACCESS,
	WORD_ACCESS
};

struct altera_hssiss_sysfs_data {
	u32 regdata;
};

struct altera_hssiss;
#define ALTERA_HSSISS_NUM_INTF		20
#define HSSISS_ETILE_PORT_COUNT		16
#define HSSISS_FHT_PORT_START		HSSISS_ETILE_PORT_COUNT
#define HSSISS_FHT_PORT_COUNT		(ALTERA_HSSISS_NUM_INTF - HSSISS_FHT_PORT_START)
#define HSSISS_FHT_PORT_END		(HSSISS_FHT_PORT_START + HSSISS_FHT_PORT_COUNT)

struct altera_hssiss_intf_port_map {
	struct altera_eth_intf *intf;
	u32 port;
};

struct cold_reset_register {
	u32 ofs;
	u32 rst_bit;
	u32 rst_ack;
};

/* driver APIs to be called by other subsystem drivers or hssiss debugfs */
struct altera_hssiss_ops {
	int (*register_intf)(struct altera_eth_intf *intf,
			     struct altera_hssiss *priv, u32 port);
	int (*unregister_intf)(struct altera_eth_intf *intf,
			       struct altera_hssiss *priv);
	int (*get_set_csr)(struct altera_hssiss *priv, void *csr_data, bool rd);
	bool (*is_link_stable)(struct altera_eth_intf *intf);
	int (*get_profile_lane_speed)(struct altera_hssiss *priv, u32 port);
	int (*get_pma_lane_count)(struct altera_hssiss *priv, u32 port);
};

struct altera_hssiss_dbg;
struct altera_hssiss {
	bool cold_rst_valid;
	struct device *dev;
	void __iomem *sscsr;
	resource_size_t sscsr_size;
	struct altera_hssiss_intf_port_map intf_map[ALTERA_HSSISS_NUM_INTF];
	unsigned int dfh_feature_rev;
	unsigned int ver; /* 1: etile, 2: ftile */
	unsigned int csr_addroff;
	union altera_hssiss_feature_list feature_list;
	/*
	 * For F-tile, loopback enable sets error bit in status reg.
	 * WA: Ignore the error bit if ACK is set.
	 */
	int hssi_err_wa;

	/* To synchronize mailbox access */
	struct mutex sal_mutex;
	/* To synchronize registration from multiple subscribers */
	struct mutex intf_mutex;
	struct altera_hssiss_ops ops;

	/* To synchronize coldrst triggered from multiple ports */
	struct mutex coldrst_mutex;
	struct cold_reset_register cold_rst_reg;
	struct altera_hssiss_dbg *dbgfs;
};

#ifdef CONFIG_DEBUG_FS
struct altera_hssiss_dbg *altera_hssiss_dbgfs_init(struct platform_device *pdev,
						   struct altera_hssiss *data);
void altera_hssiss_dbgfs_remove(struct altera_hssiss_dbg *d);
#else
static inline struct altera_hssiss_dbg
*altera_hssiss_dbgfs_init(struct platform_device *pdev,
			  struct altera_hssiss *data)
{
	return NULL;
}

static inline void altera_hssiss_dbgfs_remove(struct altera_hssiss_dbg *d) {}
#endif /* CONFIG_DEBUG_FS */

#endif /* __ALTERA_HSSISS_H__ */
