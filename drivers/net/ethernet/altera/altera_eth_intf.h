/* SPDX-License-Identifier: GPL-2.0 */
/* Altera netdev interface to ethernet subystem
 * Copyright (C) 2026 Altera Corporation. All rights reserved
 *
 * Contributors:
 *   Subhransu S. Prusty
 */

#ifndef __ALTERA_ETH_INTF_H__
#define __ALTERA_ETH_INTF_H__

#include <linux/phy.h>
#include <linux/ethtool.h>

struct altera_eth_intf;
struct altera_drv_params;

/**
 * struct altera_eth_intf_ops - callback for  netdev driver to configure the
 *				ethernet subsystem.

 * @drv_init: ethernet subystem driver initialization with netdev data
 * @set_drv_param: Runtime setting driver parameter from netdev
 * @get_fw_version: Get firmware version of HSSI SS
 * @reset_mac_stat: Reset mac statistics
 * @get_mtu: Get MTU value
 * @set_mtu: Set MTU to user defined value
 * @enable_disable_loopback: Debug feature to enable/disable loopback at various
 *			     stages of ethernet hw.
 * @get_set_dr_profile: Rate or profile change
 * @get_ncsi_link_status: Get sideband link status
 * @hotplug_enable: Enable hotplug
 * @hotplug_disable_status: Get status of hotplug
 * @cold_rst: Reset all the components in the design
 * @reset_port: Reset a particular port
 * @get_link_status: Read the ethernet link status
 * @ehip_reset: Selectively reset ethernet subystem path
 * @ehip_deassert_reset: Bring the ethernet subsystem out of reset
 * @ehip_init: Initialize ethernet subsystem
 * @ehip_uninit: Clean up function
 * @ehip_start: Start the subsystem to process data
 * @ehip_stop: Stop the subsystem from processing data
 * @ehip_run_check: addtional initialization after ethernet init
 * @ehip_update_macaddr: Update mac address with user value
 * @ehip_enable_flowctrl: Enable flow and pause quanta
 * @ehip_read_mac_stats64: Read all mac stats into user buffer
 * @ehip_get_regs: Read registers for ethtool
 * @ehip_anlt_configure: Configure anlt
 * @ehip_anlt_get_status: Get anlt status
 */
struct altera_eth_intf_ops {
	void (*drv_init)(struct altera_eth_intf *intf,
			 struct net_device *ndev,
			 struct altera_drv_params *params,
			 bool ptp_enable);
	void (*set_drv_param)(struct altera_eth_intf *intf,
			      struct altera_drv_params *params);
	int (*get_fw_version)(struct altera_eth_intf *intf, void *data);
	int (*reset_mac_stat)(struct altera_eth_intf *intf, bool tx, bool rx);
	int (*get_mtu)(struct altera_eth_intf *intf,
		       u16 *max_tx_frame_size, u16 *max_rx_frame_size);
	int (*set_mtu)(struct altera_eth_intf *intf,
		       u16 max_tx_frame_size, u16 max_rx_frame_size);
	int (*enable_disable_loopback)(struct altera_eth_intf *intf, bool en);
	int (*get_set_dr_profile)(struct altera_eth_intf *intf, void *data, bool get);
	int (*get_ncsi_link_status)(struct altera_eth_intf *intf, void *data);
	void (*hotplug_enable)(struct altera_eth_intf *intf, bool enable);
	int (*hotplug_disable_status)(struct altera_eth_intf *intf);
	int (*cold_rst)(struct altera_eth_intf *intf);
	void (*reset_port)(struct altera_eth_intf *intf);
	bool (*get_link_status)(struct altera_eth_intf *intf);
	int (*ehip_reset)(struct altera_eth_intf *intf, bool tx, bool rx, bool sys);
	int (*ehip_deassert_reset)(struct altera_eth_intf *intf);
	int (*ehip_init)(struct altera_eth_intf *intf);
	int (*ehip_uninit)(struct altera_eth_intf *intf);
	int (*ehip_start)(struct altera_eth_intf *intf);
	int (*ehip_stop)(struct altera_eth_intf *intf);
	int (*ehip_run_check)(struct altera_eth_intf *intf);
	void (*ehip_update_macaddr)(struct altera_eth_intf *intf);
	void (*ehip_enable_flowctrl)(struct altera_eth_intf *intf,
				     bool tx, bool rx, u32 flow_ctrl);
	void (*ehip_read_mac_stats64)(struct altera_eth_intf *intf, u64 *buf);
	void (*ehip_get_regs)(struct altera_eth_intf *intf,
			      struct ethtool_regs *regs, void *regbuf);
	int (*ehip_anlt_configure)(struct altera_eth_intf *intf, bool enable);
	int (*ehip_anlt_get_status)(struct altera_eth_intf *intf);
};

struct altera_ethtool_intf {
	char *statstring_buf;
	unsigned int statstring_len;
	int reglen;
	char driver[ETH_GSTRING_LEN];
	char version[ETH_GSTRING_LEN];
	char bus_info[ETHTOOL_BUSINFO_LEN];
};

#define ALTERA_ETH_PARAM_LINK_SPEED		BIT(0)
#define ALTERA_ETH_PARAM_FLOW_CTRL		BIT(1)
#define ALTERA_ETH_PARAM_PAUSE_QUANTA	BIT(2)
#define ALTERA_ETH_PARAM_MSG_ENABLE		BIT(3)
#define ALTERA_ETH_PARAM_AUTONEG		BIT(4)

struct altera_drv_params {
	u32 flag;	/* Set to ALTERA_ETH_PARAM_xxx */
	u32 link_speed;
	u32 flow_ctrl;
	u32 pause;
	u32 msg_enable;
	bool autoneg;
};

struct altera_eth_intf {
	struct altera_eth_intf_ops ops;
	struct altera_ethtool_intf ethtool_intf;
	void *priv;
};
#endif /* __ALTERA_ETH_INTF_H__ */
