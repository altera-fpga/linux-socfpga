// SPDX-License-Identifier: GPL-2.0
/* Altera FPGA HSSI SS debugfs
 * Copyright (C) 2026 Altera Corporation. All rights reserved
 *
 * Contributors:
 *   Subhransu S. Prusty
 *   Preetam Narayan
 *
 */
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/debugfs.h>
#include <linux/completion.h>
#include <linux/refcount.h>
#include <linux/uaccess.h>
#include <linux/ctype.h>
#include "../altera_utils.h"
#include "altera_hssiss.h"
#include "altera_hssiss_csr.h"

#define HSSISS_DBG_CMD_MAX	128
#define HSSISS_DBG_BUF_SIZE	PAGE_SIZE

struct altera_hssiss_dbg_read_data {
	u32 addr_offs; /* get_hss_val */
	u32 val; /* get_hss_val */
	u32 data;
	u32 max_tx_frame_size;
	u32 max_rx_frame_size;
};

struct altera_hssiss_dbg {
	struct altera_hssiss_dbg_read_data read;
	enum altera_hssiss_sal_usrcmd sal_cmd;
	struct altera_hssiss *hssiss;
	struct dentry *dbgfs;
	refcount_t refs;
	struct completion released;
};

static int hssiss_debugfs_open(struct inode *inode, struct file *filep)
{
	struct altera_hssiss_dbg *d = inode->i_private;
	int ret;

	if (!refcount_inc_not_zero(&d->refs))
		return -ENODEV;

	ret = simple_open(inode, filep);
	if (ret)
		refcount_dec(&d->refs);

	return ret;
}

static int hssiss_debugfs_release(struct inode *inode, struct file *filep)
{
	struct altera_hssiss_dbg *d = filep->private_data;

	if (refcount_dec_and_test(&d->refs))
		complete(&d->released);

	return 0;
}

static bool hssiss_dbg_offset_valid(struct altera_hssiss *priv, u32 offset)
{
	return priv->sscsr_size >= sizeof(u32) &&
	       offset <= priv->sscsr_size - sizeof(u32);
}

/* Common lookup + error log used by every sal_write case below */
static struct altera_eth_intf *
altera_hssiss_intf_from_port(struct altera_hssiss *priv,
			     u32 port)
{
	struct altera_eth_intf *intf = NULL;

	for (int i = 0; i < ALTERA_HSSISS_NUM_INTF; i++) {
		if (priv->intf_map[i].port == port)
			intf = priv->intf_map[i].intf;
	}

	if (!intf)
		dev_err(priv->dev,
			"sal: port %u not registered on this hssiss (see registered_ports)\n",
			port);

	return intf;
}

static int hssiss_dbg_copy_user_cmd(const char __user *ubuf, size_t count,
				    char *kbuf, size_t kbuf_size)
{
	if (!count || count >= kbuf_size)
		return -EINVAL;

	if (copy_from_user(kbuf, ubuf, count))
		return -EFAULT;

	kbuf[count] = '\0';
	while (count > 0) {
		unsigned char c = (unsigned char)kbuf[count - 1];

		if (c != ' ' && c != '\t' && c != '\n' && c != '\r' &&
		    c != '\f' && c != '\v')
			break;
		kbuf[--count] = '\0';
	}

	return count ? 0 : -EINVAL;
}

/*
 * for read:
 *	echo "ch type offset word" > hssi_reg
 * word: 1 for word read/write, 0 for byte read/write
 */
static ssize_t csr_read(struct file *filep, char __user *ubuf,
			size_t count, loff_t *offp)
{
	struct altera_hssiss_dbg *d = filep->private_data;
	char buf[10];
	int size;

	size = snprintf(buf, sizeof(buf), "%x\n", d->read.data);
	return simple_read_from_buffer(ubuf, count, offp, buf, size);
}

/*
 * for write:
 *	echo "ch type offset word data" > hssi_reg
 * word: 1 for word read/write, 0 for byte read/write
 */
static ssize_t csr_write(struct file *filep, const char __user *ubuf,
			 size_t count, loff_t *offp)
{
	struct altera_hssiss_dbg *d = filep->private_data;
	struct altera_hssiss *priv = d->hssiss;
	struct get_set_csr_data data;
	char buf[HSSISS_DBG_CMD_MAX];
	int word, type, ch, ret;
	u32 offset, val;

	ret = hssiss_dbg_copy_user_cmd(ubuf, count, buf, sizeof(buf));
	if (ret)
		return ret;
	ret = sscanf(buf, "%d %d %x %d %x", &ch, &type, &offset, &word, &val);
	if (ret < 4)
		return -EINVAL;
	if (!hssiss_dbg_offset_valid(priv, offset))
		return -EINVAL;
	data.ch = ch;
	data.reg_type = type;
	data.offs = offset;
	data.word = word ? true : false;
	data.data = val;
	if (ret == 4) {
		ret = priv->ops.get_set_csr(priv, &data, true);
		if (ret == 0)
			d->read.data = data.data;
	} else {
		ret = priv->ops.get_set_csr(priv, &data, false);
	}
	return ret < 0 ? ret : (ssize_t)count;
}

/*
 * sal_read() - hssiss debugfs-node SAL read callback
 * Note: Except get/set csr. Use get/set csr dbgfs to read csr registers.
 */
static ssize_t sal_read(struct file *filep, char __user *ubuf,
			size_t count, loff_t *offp)
{
	struct altera_hssiss_dbg *d = filep->private_data;
	char buf[100];
	int size;

	switch (d->sal_cmd) {
	case SAL_GET_HSSI_PROFILE:
		size = scnprintf(buf, sizeof(buf),
				 "addr_offs: %x val: %x",
				d->read.addr_offs, d->read.val);
		break;
	case SAL_READ_MAC_STAT:
		size = scnprintf(buf, sizeof(buf), "%x", d->read.data);
		break;
	case SAL_GET_MTU:
		size = scnprintf(buf, sizeof(buf),
				 "max_tx_frame_size: %x max_rx_frame_size:%x",
				 d->read.max_tx_frame_size,
				 d->read.max_rx_frame_size);
		break;
	case SAL_NCSI_GET_LINK_STS:
	case SAL_FW_VERSION:
		size = scnprintf(buf, sizeof(buf), "%x", d->read.data);
		break;
	default:
		size = scnprintf(buf, sizeof(buf), "No command in progress\n");
		break;
	}
	return simple_read_from_buffer(ubuf, count, offp, buf, size);
}

/*
 * sal_write() - hssiss debugfs-node sal write callback
 * Note: Except get/set csr. Use get/set csr dbgfs to read csr registers.
 */
static ssize_t sal_write(struct file *filep, const char __user *ubuf,
			 size_t count, loff_t *offp)
{
	struct altera_hssiss_dbg *d = filep->private_data;
	struct altera_hssiss *priv = d->hssiss;
	struct altera_eth_intf *intf;
	char kbuf[HSSISS_DBG_CMD_MAX];
	u32 port;
	unsigned int type, lsb;
	enum altera_hssiss_sal_usrcmd sal_cmd;
	int ret, nargs;
	u32 cmd;

	ret = hssiss_dbg_copy_user_cmd(ubuf, count, kbuf, sizeof(kbuf));
	if (ret) {
		dev_err(priv->dev, "sal: bad user buffer (count=%zu)\n", count);
		return ret;
	}

	nargs = sscanf(kbuf, "%x", &cmd);
	if (nargs != 1) {
		dev_err(priv->dev, "sal: invalid command token in \"%s\"\n", kbuf);
		return -EINVAL;
	}

	sal_cmd = (enum altera_hssiss_sal_usrcmd)cmd;
	if (cmd == 0xff)
		sal_cmd = SAL_FW_VERSION;
	d->sal_cmd = sal_cmd;

	switch (sal_cmd) {
	case SAL_GET_HSSI_PROFILE:
	case SAL_SET_HSSI_PROFILE:
	{
		struct get_set_dr_data data;

		nargs = sscanf(kbuf, "%x %x %x %x", &cmd, &data.addr_offs,
			       &data.val, &port);
		if (nargs != 4) {
			dev_err(priv->dev,
				"sal: profile needs {cmd addr_offs val port} (%d fields in {%s})\n",
				nargs, kbuf);
			return -EINVAL;
		}

		if (data.addr_offs > HSSI_SAL_CTRLADDR_ADDRBITS_MASK)
			return -EINVAL;

		intf = altera_hssiss_intf_from_port(priv, port);
		if (!intf)
			return -EINVAL;

		if (sal_cmd == SAL_SET_HSSI_PROFILE)
			ret = intf->ops.get_set_dr_profile(intf, &data, false);
		else
			ret = intf->ops.get_set_dr_profile(intf, &data, true);

		if (ret)
			return ret;

		d->read.addr_offs = data.addr_offs;
		d->read.val = data.val;

		break;
	}

	case SAL_READ_MAC_STAT:
	{
		u64 stats[HSSISS_MAC_STATS_MAX];
		u32 data;

		nargs = sscanf(kbuf, "%x %x %u %x", &cmd, &type, &lsb, &port);
		if (nargs != 4) {
			dev_err(priv->dev,
				"sal: mac_stat needs {3 type lsb port} (%d fields in {%s})\n",
				nargs, kbuf);
			return -EINVAL;
		}

		intf = altera_hssiss_intf_from_port(priv, port);
		if (!intf)
			return -EINVAL;

		if (type >= HSSISS_MAC_STATS_MAX)
			return -EINVAL;

		intf->ops.ehip_read_mac_stats64(intf, stats);
		data = lsb ? stats[type] : stats[type] >> 32;
		d->read.data = data;

		break;
	}

	case SAL_GET_MTU:
	{
		u16 max_tx_frame_size, max_rx_frame_size;

		nargs = sscanf(kbuf, "%x %x", &cmd, &port);
		if (nargs != 2) {
			dev_err(priv->dev, "sal: get_mtu needs {4 port} (%d fields)\n",
				nargs);
			return -EINVAL;
		}

		intf = altera_hssiss_intf_from_port(priv, port);
		if (!intf)
			return -EINVAL;

		ret = intf->ops.get_mtu(intf, &max_tx_frame_size, &max_rx_frame_size);
		if (ret)
			return ret;

		d->read.max_tx_frame_size = max_tx_frame_size;
		d->read.max_rx_frame_size = max_rx_frame_size;

		break;
	}

	case SAL_SET_MTU:
	{
		u16 max_tx_frame_size, max_rx_frame_size;

		nargs = sscanf(kbuf, "%x %hu %hu %x",
			       &cmd, &max_tx_frame_size, &max_rx_frame_size, &port);
		if (nargs != 4) {
			dev_err(priv->dev, "sal: set_mtu needs 4 fields\n");
			return -EINVAL;
		}

		intf = altera_hssiss_intf_from_port(priv, port);
		if (!intf)
			return -EINVAL;

		ret = intf->ops.set_mtu(intf, max_tx_frame_size, max_rx_frame_size);
		if (ret)
			return ret;

		break;
	}

	case SAL_RESET_MAC_STAT:
	{
		int tx, rx;

		nargs = sscanf(kbuf, "%x %d %d %x", &cmd, &tx, &rx, &port);
		if (nargs != 4) {
			dev_err(priv->dev, "sal: reset_stat needs 4 fields\n");
			return -EINVAL;
		}

		intf = altera_hssiss_intf_from_port(priv, port);
		if (!intf)
			return -EINVAL;

		ret = intf->ops.reset_mac_stat(intf, !!tx, !!rx);
		if (ret)
			return ret;

		break;
	}

	case SAL_NCSI_GET_LINK_STS:
	{
		union ncsi_link_status_data data;

		nargs = sscanf(kbuf, "%x %x", &cmd, &port);
		if (nargs != 2) {
			dev_err(priv->dev, "sal: link_sts needs {b port}\n");
			return -EINVAL;
		}

		intf = altera_hssiss_intf_from_port(priv, port);
		if (!intf)
			return -EINVAL;

		data.full = port;
		ret = intf->ops.get_ncsi_link_status(intf, &data);
		if (ret)
			return ret;

		d->read.data = data.full;

		break;
	}

	case SAL_FW_VERSION:
	{
		u32 data;

		nargs = sscanf(kbuf, "%x %x", &cmd, &port);
		if (nargs != 2) {
			dev_err(priv->dev, "sal: fw_version needs \"ff port\"\n");
			return -EINVAL;
		}

		intf = altera_hssiss_intf_from_port(priv, port);
		if (!intf)
			return -EINVAL;

		ret = intf->ops.get_fw_version(intf, &data);
		if (ret)
			return ret;

		d->read.data = data;

		break;
	}

	case SAL_DISABLE_LOOPBACK:
	case SAL_ENABLE_LOOPBACK:
		nargs = sscanf(kbuf, "%x %x", &cmd, &port);
		if (nargs != 2) {
			dev_err(priv->dev, "sal: loopback needs \"7|8 port\"\n");
			return -EINVAL;
		}

		intf = altera_hssiss_intf_from_port(priv, port);
		if (!intf)
			return -EINVAL;

		ret = intf->ops.enable_disable_loopback(intf, cmd == SAL_ENABLE_LOOPBACK);
		if (ret)
			return ret;

		break;

	default:
		dev_err(priv->dev, "sal: unhandled command %u\n", cmd);
		return -EINVAL;
	}

	*offp += count;

	return (ssize_t)count;
}

static ssize_t registered_ports_read(struct file *filep, char __user *ubuf,
				     size_t count, loff_t *offp)
{
	struct altera_hssiss_dbg *d = filep->private_data;
	struct altera_hssiss *priv = d->hssiss;
	char *buf;
	int ret, i, n = 0;

	buf = kzalloc(HSSISS_DBG_BUF_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = scnprintf(buf, HSSISS_DBG_BUF_SIZE,
			"Ports registered on this HSSI SS (from DT hssi_port):\n");

	for (i = 0; i < ALTERA_HSSISS_NUM_INTF; i++) {
		if (!priv->intf_map[i].intf)
			continue;

		ret += scnprintf(buf + ret, HSSISS_DBG_BUF_SIZE - ret,
				 "  port %u\n", priv->intf_map[i].port);
		n++;
	}
	if (!n)
		ret += scnprintf(buf + ret, HSSISS_DBG_BUF_SIZE - ret,
				 "  (none - eth/ftile driver not registered yet)\n");

	ret = simple_read_from_buffer(ubuf, count, offp, buf, ret);
	kfree(buf);

	return ret;
}

static ssize_t readme_read(struct file *filep, char __user *ubuf,
			   size_t count, loff_t *offp)
{
	char *buf;
	int ret;

	buf = kzalloc(HSSISS_DBG_BUF_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = scnprintf(buf, HSSISS_DBG_BUF_SIZE,
			"Start here:\n");
	ret += scnprintf(buf + ret, HSSISS_DBG_BUF_SIZE - ret,
			 "  cat registered_ports   # valid port numbers for sal\n");
	ret += scnprintf(buf + ret, HSSISS_DBG_BUF_SIZE - ret,
			 "  cat dumpcsr            # CSR / port status snapshot\n\n");
	ret += scnprintf(buf + ret, HSSISS_DBG_BUF_SIZE - ret,
			 "SAL commands (write sal, then cat sal):\n");
	ret += scnprintf(buf + ret, HSSISS_DBG_BUF_SIZE - ret,
			 "  echo \"ff <port>\" > sal           # firmware version\n");
	ret += scnprintf(buf + ret, HSSISS_DBG_BUF_SIZE - ret,
			 "  echo \"4 <port>\" > sal            # get MTU\n");
	ret += scnprintf(buf + ret, HSSISS_DBG_BUF_SIZE - ret,
			 "  echo \"3 <type> <lsb> <port>\" > sal# MAC stat (type 0=tx pkts, 1=rx pkts)\n");
	ret += scnprintf(buf + ret, HSSISS_DBG_BUF_SIZE - ret,
			 "  echo \"b <port>\" > sal            # link status\n");
	ret += scnprintf(buf + ret, HSSISS_DBG_BUF_SIZE - ret,
			 "  Example: echo \"3 1 0 8\" > sal && cat sal\n\n");
	ret += scnprintf(buf + ret, HSSISS_DBG_BUF_SIZE - ret,
			 "CSR via SAL (write csr, then cat csr):\n");
	ret += scnprintf(buf + ret, HSSISS_DBG_BUF_SIZE - ret,
			 "  read:  echo \"ch type offset 1\" > csr\n");
	ret += scnprintf(buf + ret, HSSISS_DBG_BUF_SIZE - ret,
			 "  write: echo \"ch type offset 1 data\" > csr\n");
	ret += scnprintf(buf + ret, HSSISS_DBG_BUF_SIZE - ret,
			 "  type: 0=ETH_RECONFIG 1=RSFEC 2=PMACAP 3=PMAAVMM 5=PTP\n\n");
	ret += scnprintf(buf + ret, HSSISS_DBG_BUF_SIZE - ret,
			 "Direct mailbox (expert): ctrladdr, cmdsts, wr, rd\n");
	ret += scnprintf(buf + ret, HSSISS_DBG_BUF_SIZE - ret,
			 "Direct CSR offset: echo \"<offset> 0 0\" > direct_reg && cat direct_reg\n");
	ret = simple_read_from_buffer(ubuf, count, offp, buf, ret);
	kfree(buf);

	return ret;
}

static ssize_t dumpcsr_read(struct file *filep, char __user *ubuf,
			    size_t count, loff_t *offp)
{
	struct altera_hssiss_dbg *d = filep->private_data;
	struct altera_hssiss *priv = d->hssiss;
	void __iomem *base = priv->sscsr;
	char *buf;
	int ret;
	int i;
	u32 val;

	buf = kzalloc(HSSISS_DBG_BUF_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = scnprintf(buf, HSSISS_DBG_BUF_SIZE, "Dumping device feature registers\n");
	for (i = 0; i < 10; i++)
		ret += scnprintf(buf + ret, HSSISS_DBG_BUF_SIZE - ret, "\t%x: %x\n",
				(i * 4), csrrd32(base, (i * 4)));

	ret += scnprintf(buf + ret, HSSISS_DBG_BUF_SIZE - ret, "Dumping other CSR registers\n");
	val = csrrd32_withoffset(priv, HSSISS_CSR_VER);
	ret += scnprintf(buf + ret, HSSISS_DBG_BUF_SIZE - ret, "HSSISS_CSR_VER: %x\n", val);
	val = csrrd32_withoffset(priv, HSSISS_CSR_COMMON_FEATURE_LIST);
	ret += scnprintf(buf + ret, HSSISS_DBG_BUF_SIZE - ret,
			 "HSSISS_CSR_COMMON_FEATURE_LIST: %x\n", val);
	ret += scnprintf(buf + ret, HSSISS_DBG_BUF_SIZE - ret, "Dumping port attributes\n");
	for (i = 0; i < HSSISS_FHT_PORT_START; i++) { /* E-tile and FGT in F-tile */
		val = csrrd32_withoffset(priv,
					 HSSISS_CSR_INTER_ATTRIB_PORT + (i * 4));
		ret += scnprintf(buf + ret, HSSISS_DBG_BUF_SIZE - ret, "\t%x: %x\n", i, val);
	}

	if (priv->ver == HSSISS_FTILE) { /* For F-tile FHT only */
		for (i = HSSISS_FHT_PORT_START; i < HSSISS_FHT_PORT_END; i++) {
			val = csrrd32(base,
				      HSSISS_CSR_INTER_ATTRIB_PORT_FHT +
				      ((i - HSSISS_FHT_PORT_START) * 4));
			ret += scnprintf(buf + ret, HSSISS_DBG_BUF_SIZE - ret,
					 "\t%x: %x\n", i, val);
		}
	}
	val = csrrd32_withoffset(priv, HSSISS_CSR_CMDSTS);
	ret += scnprintf(buf + ret, HSSISS_DBG_BUF_SIZE - ret, "HSSISS_CSR_CMDSTS: %x\n", val);
	val = csrrd32_withoffset(priv, HSSISS_CSR_CTRLADDR);
	ret += scnprintf(buf + ret, HSSISS_DBG_BUF_SIZE - ret, "HSSISS_CSR_CTRLADDR: %x\n", val);
	val = csrrd32_withoffset(priv, HSSISS_CSR_RD_DATA);
	ret += scnprintf(buf + ret, HSSISS_DBG_BUF_SIZE - ret,
			 "HSSISS_CSR_RD_DATA: %x\n", val);
	val = csrrd32_withoffset(priv, HSSISS_CSR_WR_DATA);
	ret += scnprintf(buf + ret, HSSISS_DBG_BUF_SIZE - ret,
			 "HSSISS_CSR_WR_DATA: %x\n", val);
	val = csrrd32_withoffset(priv, HSSISS_CSR_GMII_TX_LATENCY);
	ret += scnprintf(buf + ret, HSSISS_DBG_BUF_SIZE - ret,
			 "HSSISS_CSR_GMII_TX_LATENCY: %x\n", val);
	val = csrrd32_withoffset(priv, HSSISS_CSR_GMII_RX_LATENCY);
	ret += scnprintf(buf + ret, HSSISS_DBG_BUF_SIZE - ret,
			 "HSSISS_CSR_GMII_RX_LATENCY: %x\n", val);
	ret += scnprintf(buf + ret, HSSISS_DBG_BUF_SIZE - ret, "Dumping port status\n");
	for (i = 0; i < HSSISS_FHT_PORT_START; i++) { /* E-tile and FGT in F-tile */
		val = csrrd32_withoffset(priv,
					 HSSISS_CSR_ETH_PORT_STS + (i * 4));
		ret += scnprintf(buf + ret, HSSISS_DBG_BUF_SIZE - ret, "\t%x: %x\n", i, val);
	}
	if (priv->ver == HSSISS_FTILE) { /* For F-tile FHT only */
		for (i = HSSISS_FHT_PORT_START; i < HSSISS_FHT_PORT_END; i++) {
			val = csrrd32(base, HSSISS_CSR_ETH_PORT_STS_FHT +
				      (i - HSSISS_FHT_PORT_START) * 4);
			ret += scnprintf(buf + ret,
					 HSSISS_DBG_BUF_SIZE - ret, "\t%x: %x\n", i, val);
		}
	}
	val = csrrd32_withoffset(priv, HSSISS_CSR_TSE_CTRL);
	ret += scnprintf(buf + ret, HSSISS_DBG_BUF_SIZE - ret, "HSSISS_CSR_TSE_CTRL: %x\n", val);
	val = csrrd32_withoffset(priv, HSSISS_CSR_DBG_CTRL);
	ret += scnprintf(buf + ret, HSSISS_DBG_BUF_SIZE - ret, "HSSISS_CSR_DBG_CTRL: %x\n", val);
	val = csrrd32_withoffset(priv, HSSISS_CSR_HOTPLUG_DBG_CTRL);
	ret += scnprintf(buf + ret, HSSISS_DBG_BUF_SIZE - ret,
			 "HSSISS_CSR_HOTPLUG_DBG_CTRL: %x\n", val);
	val = csrrd32_withoffset(priv, HSSISS_CSR_HOTPLUG_DBG_STS);
	ret += scnprintf(buf + ret, HSSISS_DBG_BUF_SIZE - ret,
			 "HSSISS_CSR_HOTPLUG_DBG_STS: %x\n", val);
	ret = simple_read_from_buffer(ubuf, count, offp, buf, ret);
	kfree(buf);

	return ret;
}

static ssize_t mailbox_reg_write(struct file *filep, const char __user *ubuf,
				 size_t count, loff_t *offp, u32 reg)
{
	struct altera_hssiss_dbg *d = filep->private_data;
	struct altera_hssiss *priv = d->hssiss;
	u32 val;
	char buf[HSSISS_DBG_CMD_MAX];
	int ret;

	ret = hssiss_dbg_copy_user_cmd(ubuf, count, buf, sizeof(buf));
	if (ret)
		return ret;

	ret = kstrtou32(buf, 0, &val);
	if (ret < 0)
		return ret;

	csrwr32_withoffset(priv, val, reg);

	return (ssize_t)count;
}

static ssize_t mailbox_reg_read(struct file *filep, char __user *ubuf,
				size_t count, loff_t *offp, u32 reg)
{
	struct altera_hssiss_dbg *d = filep->private_data;
	struct altera_hssiss *priv = d->hssiss;
	char buf[10];
	u32 val;
	int size;

	val = csrrd32_withoffset(priv, reg);
	size = snprintf(buf, sizeof(buf), "%x\n", val);

	return (ssize_t)simple_read_from_buffer(ubuf, count, offp, buf, size);
}

static ssize_t ctrladdr_read(struct file *filep, char __user *ubuf,
			     size_t count, loff_t *offp)
{
	return mailbox_reg_read(filep, ubuf, count, offp, HSSISS_CSR_CTRLADDR);
}

static ssize_t ctrladdr_write(struct file *filep, const char __user *ubuf,
			      size_t count, loff_t *offp)
{
	return mailbox_reg_write(filep, ubuf, count, offp, HSSISS_CSR_CTRLADDR);
}

static ssize_t cmdsts_read(struct file *filep, char __user *ubuf,
			   size_t count, loff_t *offp)
{
	return mailbox_reg_read(filep, ubuf, count, offp, HSSISS_CSR_CMDSTS);
}

static ssize_t cmdsts_write(struct file *filep, const char __user *ubuf,
			    size_t count, loff_t *offp)
{
	return  mailbox_reg_write(filep, ubuf, count, offp, HSSISS_CSR_CMDSTS);
}

static ssize_t wr_reg_read(struct file *filep, char __user *ubuf,
			   size_t count, loff_t *offp)
{
	return mailbox_reg_read(filep, ubuf, count, offp, HSSISS_CSR_WR_DATA);
}

static ssize_t wr_reg_write(struct file *filep, const char __user *ubuf,
			    size_t count, loff_t *offp)
{
	return  mailbox_reg_write(filep, ubuf, count, offp, HSSISS_CSR_WR_DATA);
}

static ssize_t rd_reg_read(struct file *filep, char __user *ubuf,
			   size_t count, loff_t *offp)
{
	return mailbox_reg_read(filep, ubuf, count, offp, HSSISS_CSR_RD_DATA);
}

static ssize_t rd_reg_write(struct file *filep, const char __user *ubuf,
			    size_t count, loff_t *offp)
{
	return  mailbox_reg_write(filep, ubuf, count, offp, HSSISS_CSR_RD_DATA);
}

static ssize_t direct_reg_read(struct file *filep, char __user *ubuf,
			       size_t count, loff_t *offp)
{
	struct altera_hssiss_dbg *d = filep->private_data;
	char buf[10];
	int size;

	size = snprintf(buf, sizeof(buf), "%x\n", d->read.data);
	return simple_read_from_buffer(ubuf, count, offp, buf, size);
}

static ssize_t direct_reg_write(struct file *filep, const char __user *ubuf,
				size_t count, loff_t *offp)
{
	struct altera_hssiss_dbg *d = filep->private_data;
	struct altera_hssiss *priv = d->hssiss;
	u32 base, offset, val;
	char buf[HSSISS_DBG_CMD_MAX];
	int ret, direct;

	ret = hssiss_dbg_copy_user_cmd(ubuf, count, buf, sizeof(buf));
	if (ret)
		return ret;

	ret = sscanf(buf, "%x %x %d %x", &base, &offset, &direct, &val);
	if (ret < 3)
		return -EINVAL;

	if (!hssiss_dbg_offset_valid(priv, offset) ||
	    (direct && (base > priv->sscsr_size - sizeof(u32) - offset)))
		return -EINVAL;

	if (ret == 4) {
		if (direct)
			csrwr32_direct_withoffset(priv, val, (base + offset));
		else
			csrwr32_withoffset(priv, val, offset);
	} else {
		if (direct)
			d->read.data = csrrd32_direct_withoffset(priv, (base + offset));
		else
			d->read.data = csrrd32_withoffset(priv, offset);
	}

	return count;
}

static const struct file_operations ctrladdr_ops = {
	.owner = THIS_MODULE,
	.open = hssiss_debugfs_open,
	.release = hssiss_debugfs_release,
	.write = ctrladdr_write,
	.read = ctrladdr_read
};

static const struct file_operations cmdsts_ops = {
	.owner = THIS_MODULE,
	.open = hssiss_debugfs_open,
	.release = hssiss_debugfs_release,
	.write = cmdsts_write,
	.read = cmdsts_read
};

static const struct file_operations csr_ops = {
	.owner = THIS_MODULE,
	.open = hssiss_debugfs_open,
	.release = hssiss_debugfs_release,
	.write = csr_write,
	.read = csr_read
};

static const struct file_operations wr_reg_ops = {
	.owner = THIS_MODULE,
	.open = hssiss_debugfs_open,
	.release = hssiss_debugfs_release,
	.write = wr_reg_write,
	.read = wr_reg_read
};

static const struct file_operations rd_reg_ops = {
	.owner = THIS_MODULE,
	.open = hssiss_debugfs_open,
	.release = hssiss_debugfs_release,
	.write = rd_reg_write,
	.read = rd_reg_read
};

static const struct file_operations sal_ops = {
	.owner = THIS_MODULE,
	.open = hssiss_debugfs_open,
	.release = hssiss_debugfs_release,
	.write = sal_write,
	.read = sal_read
};

static const struct file_operations registered_ports_ops = {
	.owner = THIS_MODULE,
	.open = hssiss_debugfs_open,
	.release = hssiss_debugfs_release,
	.read = registered_ports_read,
};

static const struct file_operations readme_ops = {
	.owner = THIS_MODULE,
	.open = hssiss_debugfs_open,
	.release = hssiss_debugfs_release,
	.read = readme_read
};

static const struct file_operations dumpcsr_ops = {
	.owner = THIS_MODULE,
	.open = hssiss_debugfs_open,
	.release = hssiss_debugfs_release,
	.read = dumpcsr_read
};

static const struct file_operations direct_reg_ops = {
	.owner = THIS_MODULE,
	.open = hssiss_debugfs_open,
	.release = hssiss_debugfs_release,
	.write = direct_reg_write,
	.read = direct_reg_read
};

struct altera_hssiss_dbg *altera_hssiss_dbgfs_init(struct platform_device *pdev,
						   struct altera_hssiss *data)
{
	struct altera_hssiss_dbg *d;

	d = kzalloc(sizeof(*d), GFP_KERNEL);
	if (!d)
		return NULL;

	d->hssiss = data;
	refcount_set(&d->refs, 1);
	init_completion(&d->released);

	d->dbgfs = debugfs_create_dir(dev_name(&pdev->dev), NULL);
	if (IS_ERR(d->dbgfs)) {
		dev_warn(&pdev->dev, "Failed to create debugfs dir\n");
		goto err_free;
	}

	/* unsafe low-level escape hatches */
	debugfs_create_file("csr", 0644, d->dbgfs, d, &csr_ops);
	debugfs_create_file("ctrladdr", 0644, d->dbgfs, d, &ctrladdr_ops);
	debugfs_create_file("cmdsts", 0644, d->dbgfs, d, &cmdsts_ops);
	debugfs_create_file("wr", 0644, d->dbgfs, d, &wr_reg_ops);
	debugfs_create_file("rd", 0644, d->dbgfs, d, &rd_reg_ops);
	debugfs_create_file("sal", 0644, d->dbgfs, d, &sal_ops);
	debugfs_create_file("registered_ports", 0444, d->dbgfs, d, &registered_ports_ops);
	debugfs_create_file("dumpcsr", 0444, d->dbgfs, d, &dumpcsr_ops);
	debugfs_create_file("readme", 0444, d->dbgfs, d, &readme_ops);
	debugfs_create_file("direct_reg", 0600, d->dbgfs, d, &direct_reg_ops);

	return d;

err_free:
	kfree(d);

	return NULL;
}

void altera_hssiss_dbgfs_remove(struct altera_hssiss_dbg *d)
{
	debugfs_remove_recursive(d->dbgfs);

	if (!refcount_dec_and_test(&d->refs))
		wait_for_completion(&d->released);

	kfree(d);
}
