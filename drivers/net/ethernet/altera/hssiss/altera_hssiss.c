// SPDX-License-Identifier: GPL-2.0
/* Altera FPGA HSSI platform driver
 * Copyright (C) 2026 Altera Corporation. All rights reserved
 *
 * Contributors:
 *   Subhransu S. Prusty
 *   Preetam Narayan
 *
 */
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/of.h>
#include <linux/property.h>
#include "../altera_utils.h"
#include "../altera_eth_intf.h"
#include "altera_hssiss_csr.h"
#include "altera_hssiss.h"

#define ALTERA_FPGA_HSSISS_NAME "altera_hssiss"
#define ADDR_OFFSET_INCR 0x200000
#define HSSISS_MAX_CSR_CHANNELS 16
#define HSSISS_INVALID_PORT U32_MAX

static const u32 ftile_addrmap[] = {
	0x0200000, 0, 0x0300000, 0, 0, 0x0261000, 0};
static const u32 etile_addrmap[] = {
	0x0200000, 0x0204000, 0x0240000, 0x0250000, 0x0260000, 0x0261000, 0x0262000};

static struct altera_hssiss_salcmd salcmd[] = {
	{SAL_NOP, 0x0, "SAL_NOP"},
	{SAL_GET_HSSI_PROFILE, 0x1, "SAL_GET_HSSI_PROFILE"},
	{SAL_SET_HSSI_PROFILE, 0x2, "SAL_SET_HSSI_PROFILE"},
	{SAL_READ_MAC_STAT, 0x3, "SAL_READ_MAC_STAT"},
	{SAL_GET_MTU, 0x4, "SAL_GET_MTU"},
	{SAL_SET_CSR, 0x5, "SAL_SET_CSR"},
	{SAL_GET_CSR, 0x6, "SAL_GET_CSR"},
	{SAL_ENABLE_LOOPBACK, 0x7, "SAL_ENABLE_LOOPBACK"},
	{SAL_DISABLE_LOOPBACK, 0x8, "SAL_DISABLE_LOOPBACK"},
	{SAL_RESET_MAC_STAT, 0x9, "SAL_RESET_MAC_STAT"},
	{SAL_SET_MTU, 0xA, "SAL_SET_MTU"},
	{SAL_NCSI_GET_LINK_STS, 0xB, "SAL_NCSI_GET_LINK_STS"},
	{SAL_RSVD, 0xFE, "SAL_RSVD"},
	{SAL_FW_VERSION, 0xFF, "SAL_FW_VERSION"},
};

static u32 hssiss_get_port_from_intf(struct altera_hssiss *priv,
				     const struct altera_eth_intf *intf)
{
	for (int i = 0; i < ALTERA_HSSISS_NUM_INTF; i++) {
		if (priv->intf_map[i].intf == intf)
			return priv->intf_map[i].port;
	}

	return HSSISS_INVALID_PORT;
}

static u32 hssiss_get_cmdid(enum altera_hssiss_sal_usrcmd cmd)
{
	return salcmd[cmd].cmdid;
}

static const char *hssiss_get_cmd(u32 cmdid)
{
	for (int i = 0; i < ARRAY_SIZE(salcmd); i++) {
		if (salcmd[i].cmdid == cmdid)
			return salcmd[i].name;
	}

	return NULL;
}

static int hssiss_read_poll_timeout(struct altera_hssiss *priv, u32 offs,
				    u32 match, u32 *val)
{
	u32 tmp;
	int ret;

	ret = read_poll_timeout(csrrd32_withoffset, tmp, tmp & match,
				FW_ACK_POLL_INTERVAL_US, FW_ACK_POLL_TIMEOUT_US,
				false, priv, offs);
	if (val)
		*val = tmp;

	return ret;
}

static int hssiss_mailbox_reg_set(struct altera_hssiss *priv, u32 offs, u32 setval)
{
	u32 val;
	int ret;

	csrwr32_withoffset(priv, setval, offs);
	ret = read_poll_timeout(csrrd32_withoffset, val, val == setval,
				FW_ACK_POLL_INTERVAL_US, FW_ACK_POLL_TIMEOUT_US,
				false, priv, offs);

	return ret;
}

static int hssiss_sal_execute(struct altera_hssiss *priv, u32 ctrl_addr,
			      u32 cmd_sts, u32 *val)
{
	int ret;
	u32 cmdid = ctrl_addr & HSSI_SAL_CTRLADDR_SALCMD;
	u32 data = 0;

	/* unlock only after reading the RD register */
	mutex_lock(&priv->sal_mutex);

	if ((cmd_sts & HSSI_SAL_CMDSTS_WR) && val) {
		ret = hssiss_mailbox_reg_set(priv, HSSISS_CSR_WR_DATA, *val);
		if (ret < 0) {
			dev_err(priv->dev, "failed to set cmd %s with err: %d\n",
				hssiss_get_cmd(cmdid), ret);
			goto unlock;
		}
	}

	csrwr32_withoffset(priv, ctrl_addr, HSSISS_CSR_CTRLADDR);
	csrwr32_withoffset(priv, cmd_sts, HSSISS_CSR_CMDSTS);
	ret = hssiss_read_poll_timeout(priv, HSSISS_CSR_CMDSTS,
				       HSSI_SAL_CMDSTS_ACK | HSSI_SAL_CMDSTS_ERR,
				       &data);
	if (ret) {
		if (data & HSSI_SAL_CMDSTS_BUSY) {
			dev_err(priv->dev, "FW hung for cmd(%s)\n",
				hssiss_get_cmd(cmdid));
			ret = -EBUSY;
		}
		goto unlock;
	}

	if (data) {
		/*
		 * This is for a specific WA where both ACK and ERR are
		 * set for successful HOTPLUG command completion.
		 */
		if (data & HSSI_SAL_CMDSTS_ACK) {
			if (priv->hssi_err_wa && (data & HSSI_SAL_CMDSTS_ERR))
				/* nothing to read for hotplug, so unlock and return */
				goto unlock;
		} else {
			if (data & HSSI_SAL_CMDSTS_BUSY) {
				dev_err(priv->dev, "FW hung for cmd(%s)\n", hssiss_get_cmd(cmdid));
				ret = -EBUSY;
				goto unlock;
			} else if (data & HSSI_SAL_CMDSTS_ERR) {
				dev_err(priv->dev, "Command(%s) execution error\n",
					hssiss_get_cmd(cmdid));
				ret = -EINVAL;
				goto unlock;
			} else {
				dev_err(priv->dev, "Unexpected status for cmd(%s) data: %x\n",
					hssiss_get_cmd(cmdid), data);
				ret = -EIO;
				goto unlock;
			}
		}
	}

	if ((cmd_sts & HSSI_SAL_CMDSTS_RD) && val)
		*val = csrrd32_withoffset(priv, HSSISS_CSR_RD_DATA);

unlock:
	mutex_unlock(&priv->sal_mutex);

	return ret;
}

static int altera_hssiss_enable_disable_loopback(struct altera_eth_intf *intf,
						 bool en)
{
	struct altera_hssiss *priv = intf->priv;
	u32 ctrl_addr = 0;
	u32 cmd_sts = 0;
	u32 port;

	port = hssiss_get_port_from_intf(priv, intf);
	if (port == HSSISS_INVALID_PORT)
		return -EINVAL;

	if (!test_reg_bits(priv->feature_list.part.port_enable_mask, port, 1))
		return -EIO;

	ctrl_addr |= hssiss_get_cmdid(en ? SAL_ENABLE_LOOPBACK : SAL_DISABLE_LOOPBACK);
	ctrl_addr |= port << HSSI_SAL_CTRLADDR_PORT_SHIFT;
	cmd_sts |= HSSI_SAL_CMDSTS_WR;

	return hssiss_sal_execute(priv, ctrl_addr, cmd_sts, NULL);
}

/* Calculate ctrl address field for get/set csr */
static u32 hssiss_make_get_set_csr_addr(u32 base, u32 offs, bool word)
{
	if (word)
		return (base + (offs * sizeof(u32))) / sizeof(u32);
	else
		return (base + offs) / sizeof(u32);
}

static int altera_hssiss_get_set_csr(struct altera_hssiss *priv, void *csr_data, bool rd)
{
	struct get_set_csr_data *data = csr_data;
	const u32 *addrmap;
	size_t addrmap_size;
	u32 ctrl_addr = 0;
	u32 cmd_sts = 0;
	int ret;
	u32 addr;
	u32 base;

	switch (priv->ver) {
	case HSSISS_FTILE:
		addrmap = ftile_addrmap;
		addrmap_size = ARRAY_SIZE(ftile_addrmap);
		break;
	case HSSISS_ETILE:
		addrmap = etile_addrmap;
		addrmap_size = ARRAY_SIZE(etile_addrmap);
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (data->reg_type < 0 || data->reg_type >= addrmap_size)
		return -EINVAL;

	if (data->ch >= HSSISS_MAX_CSR_CHANNELS)
		return -EINVAL;

	base = (data->ch * ADDR_OFFSET_INCR) + addrmap[data->reg_type];

	addr = hssiss_make_get_set_csr_addr(base, data->offs, data->word);

	ctrl_addr |= addr << HSSI_SAL_CTRLADDR_ADDRBITS_SHIFT;
	ctrl_addr |= hssiss_get_cmdid(rd ? SAL_GET_CSR : SAL_SET_CSR);
	cmd_sts |= (data->offs % sizeof(u32)) << HSSI_SAL_CMDSTS_REG_OFFS_SHIFT;

	cmd_sts |= rd ? HSSI_SAL_CMDSTS_RD : HSSI_SAL_CMDSTS_WR;

	ret = hssiss_sal_execute(priv, ctrl_addr, cmd_sts, &data->data);

	return ret;
}

static int altera_hssiss_get_set_dr_profile(struct altera_eth_intf *intf, void *data, bool get)
{
	struct altera_hssiss *priv = intf->priv;
	struct get_set_dr_data *dr_data = data;
	u32 ctrl_addr = 0;
	u32 cmd_sts = 0;

	ctrl_addr = hssiss_get_cmdid(get ? SAL_GET_HSSI_PROFILE : SAL_SET_HSSI_PROFILE) |
		    (dr_data->addr_offs << HSSI_SAL_CTRLADDR_ADDRBITS_SHIFT);
	cmd_sts |= get ? HSSI_SAL_CMDSTS_RD : HSSI_SAL_CMDSTS_WR;

	return hssiss_sal_execute(priv, ctrl_addr, cmd_sts, &dr_data->val);
}

static int altera_hssiss_reset_mac_stat(struct altera_eth_intf *intf, bool tx, bool rx)
{
	struct altera_hssiss *priv = intf->priv;
	u32 ctrl_addr = 0;
	u32 cmd_sts = 0;
	u32 port;
	int ret;

	port = hssiss_get_port_from_intf(priv, intf);
	if (port == HSSISS_INVALID_PORT)
		return -EINVAL;

	ctrl_addr |= port << HSSI_SAL_CTRLADDR_PORT_SHIFT;

	if (tx)
		ctrl_addr |= HSSI_SAL_RESET_MAC_STAT_TX;

	if (rx)
		ctrl_addr |= HSSI_SAL_RESET_MAC_STAT_RX;

	ctrl_addr |= hssiss_get_cmdid(SAL_RESET_MAC_STAT);
	cmd_sts |= HSSI_SAL_CMDSTS_WR;

	ret = hssiss_sal_execute(priv, ctrl_addr, cmd_sts, NULL);

	return ret;
}

static int altera_hssiss_get_mtu(struct altera_eth_intf *intf,
				 u16 *max_tx_frame_size, u16 *max_rx_frame_size)
{
	struct altera_hssiss *priv = intf->priv;
	u32 ctrl_addr = 0;
	u32 cmd_sts = 0;
	u32 val, port;
	int ret;

	port = hssiss_get_port_from_intf(priv, intf);
	if (port == HSSISS_INVALID_PORT)
		return -EINVAL;

	ctrl_addr |= port << HSSI_SAL_CTRLADDR_PORT_SHIFT;
	ctrl_addr |= hssiss_get_cmdid(SAL_GET_MTU);
	cmd_sts |= HSSI_SAL_CMDSTS_RD;

	ret = hssiss_sal_execute(priv, ctrl_addr, cmd_sts, &val);
	if (ret == 0) {
		*max_tx_frame_size = FIELD_GET(GENMASK(31, 16), val);
		*max_rx_frame_size = FIELD_GET(GENMASK(15, 0), val);
	}

	return ret;
}

static int altera_hssiss_set_mtu(struct altera_eth_intf *intf,
				 u16 max_tx_frame_size, u16 max_rx_frame_size)
{
	struct altera_hssiss *priv = intf->priv;
	u32 ctrl_addr = 0;
	u32 cmd_sts = 0;
	u32 val = 0, port;

	port = hssiss_get_port_from_intf(priv, intf);
	if (port == HSSISS_INVALID_PORT)
		return -EINVAL;

	ctrl_addr |= port << HSSI_SAL_CTRLADDR_PORT_SHIFT;
	ctrl_addr |= hssiss_get_cmdid(SAL_SET_MTU);
	cmd_sts |= HSSI_SAL_CMDSTS_WR;

	val |= max_rx_frame_size;
	val |= max_tx_frame_size << HSSISS_MTU_TX_FRAME_SIZE_SHIFT;

	return hssiss_sal_execute(priv, ctrl_addr, cmd_sts, &val);
}

static int altera_hssiss_read_mac_stat(struct altera_eth_intf *intf,
				       enum altera_hssiss_mac_stat_counter_type type,
				       bool lsb, u32 *val)
{
	struct altera_hssiss *priv = intf->priv;
	u32 ctrl_addr = 0;
	u32 cmd_sts = 0;
	u32 data = 0, port;
	int ret;

	port = hssiss_get_port_from_intf(priv, intf);
	if (port == HSSISS_INVALID_PORT) {
		dev_err(priv->dev, "Port not valid\n");
		return -EINVAL;
	}

	ctrl_addr |= port << HSSI_SAL_CTRLADDR_PORT_SHIFT;
	ctrl_addr |= hssiss_get_cmdid(SAL_READ_MAC_STAT);
	ctrl_addr |= type << HSSI_SAL_CTRLADDR_COUNTER_SHIFT;
	ctrl_addr |= (lsb ? BIT(HSSI_SAL_CTRLADDR_LSB_SHIFT) : 0x0);
	cmd_sts |= HSSI_SAL_CMDSTS_RD;

	ret = hssiss_sal_execute(priv, ctrl_addr, cmd_sts, &data);
	if (!ret)
		*val = data;

	return ret;
}

static int hssiss_read_one_mac_stat(struct altera_eth_intf *intf,
				    enum altera_hssiss_mac_stat_counter_type stat_type,
				    u64 *val)
{
	u32 msb, lsb;
	int ret;

	/*
	 * Note: Stats not latched in the firmware as of now,
	 * trusting the rollover doesn't happen.
	 */
	ret = altera_hssiss_read_mac_stat(intf, stat_type, false, &msb);
	if (ret)
		return ret;

	ret = altera_hssiss_read_mac_stat(intf, stat_type, true, &lsb);
	if (!ret)
		*val = ((u64)msb << 32) | lsb;

	return ret;
}

static void altera_hssiss_read_mac_stats64(struct altera_eth_intf *intf, u64 *buf)
{
	struct altera_hssiss *priv = intf->priv;
	int ret;

	for (enum altera_hssiss_mac_stat_counter_type t = MACSTAT_TX_PACKETS;
	     t < HSSISS_MAC_STATS_MAX; t++) {
		buf[t] = 0;
		ret = hssiss_read_one_mac_stat(intf, t, &buf[t]);
		if (ret)
			dev_err(priv->dev, "Failed to read MAC stat %u: %d\n", t, ret);
	}
}

static int altera_hssiss_ncsi_link_status(struct altera_eth_intf *intf,
					  void *priv_data)
{
	struct altera_hssiss *priv = intf->priv;
	union ncsi_link_status_data *data =
		(union ncsi_link_status_data *)priv_data;
	u32 ctrl_addr = 0;
	u32 cmd_sts = 0;
	int ret;

	ctrl_addr |= data->full << HSSI_SAL_CTRLADDR_PORT_SHIFT;
	ctrl_addr |= hssiss_get_cmdid(SAL_NCSI_GET_LINK_STS);
	cmd_sts |= HSSI_SAL_CMDSTS_RD;

	ret = hssiss_sal_execute(priv, ctrl_addr, cmd_sts, &data->full);

	return ret;
}

static int altera_hssiss_get_fw_version(struct altera_eth_intf *intf,
					void *priv_data)
{
	struct altera_hssiss *priv = intf->priv;
	u32 ctrl_addr = 0;
	u32 cmd_sts = 0;
	u32 *data = (u32 *)priv_data;
	int ret;

	ctrl_addr |= hssiss_get_cmdid(SAL_FW_VERSION);
	cmd_sts |= HSSI_SAL_CMDSTS_RD;

	ret = hssiss_sal_execute(priv, ctrl_addr, cmd_sts, data);

	return ret;
}

static int hssiss_cold_reset(struct altera_eth_intf *intf)
{
	struct altera_hssiss *priv = intf->priv;
	struct cold_reset_register *cold_rst  = &priv->cold_rst_reg;
	int ret;

	if (!priv->cold_rst_valid)
		return -EOPNOTSUPP;

	if (!mutex_trylock(&priv->coldrst_mutex))
		return -EBUSY;

	csrwr32_withoffset(priv, BIT(cold_rst->rst_bit), cold_rst->ofs);
	ret = hssiss_read_poll_timeout(priv, cold_rst->ofs, BIT(cold_rst->rst_ack), NULL);
	csrwr32_withoffset(priv, 0, cold_rst->ofs);

	mutex_unlock(&priv->coldrst_mutex);

	return ret;
}

static u32 hssiss_get_ethport_status(struct altera_eth_intf *intf)
{
	struct altera_hssiss *priv = intf->priv;
	union eth_port_status port_sts;
	u32 port;

	port_sts.full = 0;

	port = hssiss_get_port_from_intf(priv, intf);
	if (port == HSSISS_INVALID_PORT)
		return port_sts.full;

	/* E-tile and FGT in F-tile */
	if (port < HSSISS_FHT_PORT_START) {
		port_sts.full = csrrd32_withoffset(priv, (HSSISS_CSR_ETH_PORT_STS + port * 4));

		return port_sts.full;
	} else if (priv->ver == HSSISS_FTILE && port < HSSISS_FHT_PORT_END) {
		/* For F-tile FHT only. Offset is from 0x200 and doesn't use csr_addroff */
		port_sts.full = csrrd32(priv->sscsr,
					(HSSISS_CSR_ETH_PORT_STS_FHT +
					 (port - HSSISS_FHT_PORT_START) * 4));
	}

	return port_sts.full;
}

static bool hssiss_is_link_stable(struct altera_eth_intf *intf)
{
	union eth_port_status pstatus;

	pstatus.full = hssiss_get_ethport_status(intf);
	return pstatus.part.tx_lanes_stable &&
			pstatus.part.rx_pcs_ready &&
			pstatus.part.tx_pll_locked;
}

static union eth_port_attr hssiss_get_ethport_attr(struct altera_hssiss *priv, u32 port)
{
	union eth_port_attr port_attr;
	u32 offs;

	port_attr.full = 0;

	/* E-tile and FGT in F-tile */
	if (port < HSSISS_FHT_PORT_START) {
		offs = HSSISS_CSR_INTER_ATTRIB_PORT + port * 4;
		port_attr.full = csrrd32_withoffset(priv, offs);

		return port_attr;
	}

	/* For F-tile FHT only */
	if (priv->ver == HSSISS_FTILE &&
	    port >= HSSISS_FHT_PORT_START && port < HSSISS_FHT_PORT_END) {
		offs = HSSISS_CSR_INTER_ATTRIB_PORT_FHT + (port - HSSISS_FHT_PORT_START) * 4;
		port_attr.full = csrrd32(priv->sscsr, offs);
	}

	return port_attr;
}

static int hssiss_get_profile_lane_speed(struct altera_hssiss *priv, u32 port)
{
	union eth_port_attr port_attr;
	int lane_speed = -EINVAL;

	port_attr = hssiss_get_ethport_attr(priv, port);

	switch (port_attr.part.profile) {
	case HSSI_PORT_PROFILE_10GBE:
		lane_speed = LANE_10G;
		break;
	case HSSI_PORT_PROFILE_200GAUI_8:
	case HSSI_PORT_PROFILE_100GCAUI_4:
	case HSSI_PORT_PROFILE_25GBE:
	case HSSI_PORT_PROFILE_50GAUI_2:
		lane_speed = LANE_25G;
		break;
	case HSSI_PORT_PROFILE_400GAUI_8:
	case HSSI_PORT_PROFILE_200GAUI_4:
	case HSSI_PORT_PROFILE_100GAUI_2:
	case HSSI_PORT_PROFILE_50GAUI_1:
		lane_speed = LANE_50G;
		break;
	case HSSI_PORT_PROFILE_100GAUI_1:
	case HSSI_PORT_PROFILE_200GAUI_2:
	case HSSI_PORT_PROFILE_400GAUI_4:
		lane_speed = LANE_100G;
		break;
	default:
		break;
	}

	return lane_speed;
}

static int hssiss_get_pma_lane_count(struct altera_hssiss *priv, u32 port)
{
	union eth_port_attr port_attr;
	int lane_count = 1;

	port_attr = hssiss_get_ethport_attr(priv, port);

	switch (port_attr.part.profile) {
	case HSSI_PORT_PROFILE_200GAUI_8:
	case HSSI_PORT_PROFILE_400GAUI_8:
		lane_count = 8;
		break;
	case HSSI_PORT_PROFILE_400GAUI_4:
	case HSSI_PORT_PROFILE_200GAUI_4:
	case HSSI_PORT_PROFILE_100GCAUI_4:
		lane_count = 4;
		break;
	case HSSI_PORT_PROFILE_100GAUI_2:
	case HSSI_PORT_PROFILE_200GAUI_2:
	case HSSI_PORT_PROFILE_50GAUI_2:
		lane_count = 2;
		break;
	default:
		break;
	}
	return lane_count;
}

/* Enable/disable hotplug */
static void hssiss_hotplug_enable(struct altera_hssiss *priv, bool enable)
{
	u32 val;

	val = csrrd32_withoffset(priv, HSSISS_CSR_HOTPLUG_DBG_CTRL);

	if (enable)
		val &= ~HSSI_HOTPLUG_DBG_CTRL_DISABLE;
	else
		val |= HSSI_HOTPLUG_DBG_CTRL_DISABLE;

	csrwr32_withoffset(priv, val, HSSISS_CSR_HOTPLUG_DBG_CTRL);
}

static bool hssiss_hotplug_disable_status(struct altera_hssiss *priv)
{
	u32 val;

	val = csrrd32_withoffset(priv, HSSISS_CSR_HOTPLUG_DBG_STS);

	return !!((val >> HSSI_HOTPLUG_DBG_STS_DISABLE_SHIFT) & 1);
}

static ssize_t hssiss_hotplug_disable_show(struct device *dev,
					   struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct altera_hssiss *priv = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%s\n", hssiss_hotplug_disable_status(priv) ? "true" : "false");
}

static ssize_t hssiss_hotplug_disable_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t len)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct altera_hssiss *priv = platform_get_drvdata(pdev);
	int disable, ret;

	ret = kstrtoint(buf, 10, &disable);
	if (ret)
		return ret;

	hssiss_hotplug_enable(priv, (disable ? false : true));

	return len;
}

static ssize_t hssiss_err_wa_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct altera_hssiss *priv = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", priv->hssi_err_wa);
}

static ssize_t hssiss_err_wa_store(struct device *dev,
				   struct device_attribute *attr, const char *buf, size_t len)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct altera_hssiss *priv = platform_get_drvdata(pdev);
	int ret;

	ret = kstrtoint(buf, 10, &priv->hssi_err_wa);
	if (ret)
		return ret;

	return len;
}

static DEVICE_ATTR_RW(hssiss_hotplug_disable);
static DEVICE_ATTR_RW(hssiss_err_wa);

static struct attribute *hssiss_sysfs_attrs[] = {
	&dev_attr_hssiss_hotplug_disable.attr,
	&dev_attr_hssiss_err_wa.attr,
	NULL
};

static const struct attribute_group hssiss_attr_group = {
	.name = "hssiss",
	.attrs = hssiss_sysfs_attrs,
};

static const struct attribute_group *hssiss_attr_groups[] = {
	&hssiss_attr_group,
	NULL,
};

/* Utility functions for hssi driver */
static unsigned int get_dfh_feature_rev(void __iomem *addr)
{
	u32 val;

	val = csrrd32(addr, feature_offs(dfh_lo));

	return ((val & HSSISS_DFHLO_DFHV0_FEA_REV_MASK) >> HSSISS_DFHLO_DFHV0_FEA_REV_SHIFT);
}

static unsigned int get_csr_addroff(void __iomem *base,
				    unsigned int feature_rev)
{
	u32 val;

	if (feature_rev == 0x0 || feature_rev == 0x1)
		return 0;

	/* feature_rev == 0x2 */
	val = csrrd32(base, feature_offs(feature_csr_addr_lsb));

	/* csr_addroff valid from offset 0x8 */
	return (((val & HSSISS_FEATURE_CSR_ADDR_MASK) >> HSSISS_FEATURE_CSR_ADDR_SHIFT) - 0x8);
}

static void hssiss_feature_init(struct altera_hssiss *priv)
{
	unsigned int version;

	priv->dfh_feature_rev = get_dfh_feature_rev(priv->sscsr);
	priv->csr_addroff = get_csr_addroff(priv->sscsr, priv->dfh_feature_rev);
	dev_info(priv->dev, "csr_addr offset: %x, dfh_feature_rev: %x\n",
		 priv->csr_addroff, priv->dfh_feature_rev);

	priv->feature_list.full =
		csrrd32_withoffset(priv, HSSISS_CSR_COMMON_FEATURE_LIST);
	version = csrrd32_withoffset(priv, HSSISS_CSR_VER);
	priv->ver = (version & HSSISS_VER_CSR_ADDR_MASK) >> HSSISS_VER_CSR_ADDR_SHIFT;
}

static int hssiss_register_ethsubsys_intf(struct altera_eth_intf *intf,
					  struct altera_hssiss *priv, u32 port)
{
	int i;
	int ret = 0;

	mutex_lock(&priv->intf_mutex);

	for (i = 0; i < ALTERA_HSSISS_NUM_INTF; i++) {
		if (priv->intf_map[i].intf == intf ||
		    (priv->intf_map[i].port == port && priv->intf_map[i].intf)) {
			ret = -EEXIST;
			goto unlock;

		} else if (priv->intf_map[i].port != HSSISS_INVALID_PORT) {
			continue;
		}

		priv->intf_map[i].intf = intf;
		priv->intf_map[i].port = port;

		break;
	}

	if (i == ALTERA_HSSISS_NUM_INTF) {
		ret = -ENOSPC;
		goto unlock;
	}

	intf->priv = priv;
	intf->ops.enable_disable_loopback = altera_hssiss_enable_disable_loopback;
	intf->ops.get_fw_version = altera_hssiss_get_fw_version;
	intf->ops.get_ncsi_link_status = altera_hssiss_ncsi_link_status;
	intf->ops.reset_mac_stat = altera_hssiss_reset_mac_stat;
	intf->ops.get_set_dr_profile = altera_hssiss_get_set_dr_profile;
	intf->ops.get_mtu = altera_hssiss_get_mtu;
	intf->ops.set_mtu = altera_hssiss_set_mtu;
	intf->ops.ehip_read_mac_stats64 = altera_hssiss_read_mac_stats64;
	intf->ops.cold_rst = hssiss_cold_reset;

unlock:
	mutex_unlock(&priv->intf_mutex);
	return ret;
}

static int hssiss_unregister_ethsubsys_intf(struct altera_eth_intf *intf,
					    struct altera_hssiss *priv)
{
	int i;

	mutex_lock(&priv->intf_mutex);

	for (i = 0; i < ALTERA_HSSISS_NUM_INTF; i++) {
		if (priv->intf_map[i].intf != intf)
			continue;

		intf->ops = (struct altera_eth_intf_ops){};
		intf->priv = NULL;
		priv->intf_map[i].intf = NULL;
		priv->intf_map[i].port = HSSISS_INVALID_PORT;

		mutex_unlock(&priv->intf_mutex);
		return 0;
	}

	mutex_unlock(&priv->intf_mutex);
	return -ENOENT;
}

static int altera_hssiss_probe(struct platform_device *pdev)
{
	struct altera_hssiss *priv;
	struct fwnode_handle *cold_rst;
	struct resource *res;
	const char *rm;
	int ret = 0;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &pdev->dev;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "sscsr");
	if (!res)
		return -EINVAL;

	priv->sscsr_size = resource_size(res);
	priv->sscsr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->sscsr))
		return (int)PTR_ERR(priv->sscsr);

	mutex_init(&priv->intf_mutex);
	mutex_init(&priv->sal_mutex);
	mutex_init(&priv->coldrst_mutex);
	hssiss_feature_init(priv);

	for (int i = 0; i < ALTERA_HSSISS_NUM_INTF; i++)
		priv->intf_map[i].port = HSSISS_INVALID_PORT;

	ret = device_property_read_string(&pdev->dev, "reset-mode", &rm);
	if (!ret) {
		if (!strcasecmp(rm, "reg")) {
			cold_rst = fwnode_get_named_child_node(pdev->dev.fwnode, "cold-reset");
			if (cold_rst) {
				ret = fwnode_property_read_u32(cold_rst, "ofs",
							       &priv->cold_rst_reg.ofs);
				if (ret)
					goto put_coldrst;

				ret = fwnode_property_read_u32(cold_rst, "rst-bit",
							       &priv->cold_rst_reg.rst_bit);
				if (ret)
					goto put_coldrst;

				ret = fwnode_property_read_u32(cold_rst, "rst-ack",
							       &priv->cold_rst_reg.rst_ack);
				if (ret)
					goto put_coldrst;

				if (!priv->cold_rst_reg.ofs ||
				    priv->cold_rst_reg.ofs > priv->sscsr_size - sizeof(u32) ||
				    priv->cold_rst_reg.rst_bit >= sizeof(u32) * BITS_PER_BYTE ||
				    priv->cold_rst_reg.rst_ack >= sizeof(u32) * BITS_PER_BYTE) {
					ret = -EINVAL;
					goto put_coldrst;
				}

				priv->cold_rst_valid = true;

put_coldrst:
				fwnode_handle_put(cold_rst);

				if (ret)
					return ret;
			}
		}
	} else {
		dev_info(priv->dev, "reset-mode not defined, ret = %d\n", ret);
		ret = 0;
	}

	priv->ops.register_intf = hssiss_register_ethsubsys_intf;
	priv->ops.unregister_intf = hssiss_unregister_ethsubsys_intf;
	priv->ops.get_set_csr = altera_hssiss_get_set_csr;
	priv->ops.is_link_stable = hssiss_is_link_stable;
	priv->ops.get_profile_lane_speed = hssiss_get_profile_lane_speed;
	priv->ops.get_pma_lane_count = hssiss_get_pma_lane_count;

	priv->dbgfs = altera_hssiss_dbgfs_init(pdev, priv);
	if (!priv->dbgfs)
		dev_warn(priv->dev, "Error creating dbgfs");

	platform_set_drvdata(pdev, priv);

	return ret;
}

static const struct of_device_id hssiss_ids[] = {
	{ .compatible = "altr,hssiss-1.0"},
	{},
};

MODULE_DEVICE_TABLE(of, hssiss_ids);

static void altera_hssiss_remove(struct platform_device *pdev)
{
	struct altera_hssiss *priv = platform_get_drvdata(pdev);

	if (priv->dbgfs)
		altera_hssiss_dbgfs_remove(priv->dbgfs);

	mutex_destroy(&priv->sal_mutex);
	mutex_destroy(&priv->coldrst_mutex);
	mutex_destroy(&priv->intf_mutex);
	platform_set_drvdata(pdev, NULL);
}

static struct platform_driver hssiss_driver = {
	.probe		= altera_hssiss_probe,
	.remove		= altera_hssiss_remove,
	.driver		= {
		.name	= ALTERA_FPGA_HSSISS_NAME,
		.of_match_table = hssiss_ids,
		.suppress_bind_attrs = IS_BUILTIN(CONFIG_ALTERA_HSSISS),
		.dev_groups = hssiss_attr_groups,
	},
};

module_platform_driver(hssiss_driver);

MODULE_AUTHOR("Altera Corporation");
MODULE_DESCRIPTION("Altera HSSI SS interface driver");
MODULE_LICENSE("GPL");
