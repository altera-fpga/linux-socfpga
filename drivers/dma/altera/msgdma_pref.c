// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026, Altera Corporation. All rights reserved
 *
 * Contributors:
 *      Preetam Narayan
 */

#include <linux/dma-mapping.h>
#include <linux/iopoll.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_dma.h>
#include <linux/overflow.h>
#include <linux/build_bug.h>
#include "dma_utils.h"
#include "msgdma_pref.h"

static unsigned int msgdma_debug_level;
module_param(msgdma_debug_level, uint, 0600);

static bool altera_msgdma_pref_isirq(struct altera_msgdma_private *priv)
{
	return dma_bit_is_set(priv->pref_csr, msgdma_pref_csroffs(status),
			      MSGDMA_PREF_STAT_IRQ);
}

static void altera_msgdma_pref_clear_irq(struct altera_msgdma_private *priv)
{
	dma_write(MSGDMA_PREF_STAT_IRQ, priv->pref_csr,
		  msgdma_pref_csroffs(status));
}

static inline void altera_msgdma_dmaintr_enable(struct altera_msgdma_private *priv)
{
	altera_msgdma_pref_clear_irq(priv);
	dma_set_bit(priv->pref_csr, msgdma_pref_csroffs(control),
		    MSGDMA_PREF_CTL_GLOBAL_INTR);
}

static inline void altera_msgdma_dmaintr_disable(struct altera_msgdma_private *priv)
{
	dma_clear_bit(priv->pref_csr, msgdma_pref_csroffs(control),
		      MSGDMA_PREF_CTL_GLOBAL_INTR);
	altera_msgdma_pref_clear_irq(priv);
}

static int altera_request_and_map(struct platform_device *pdev, const char *name,
				  void __iomem **ptr)
{
	struct device *dev = &pdev->dev;

	*ptr = devm_platform_ioremap_resource_byname(pdev, name);
	if (IS_ERR(*ptr)) {
		dev_err(dev, "could not map resource %s: %ld\n", name, PTR_ERR(*ptr));
		return PTR_ERR(*ptr);
	}

	return 0;
}

static void *mgdma_get_metadata_ptr(struct dma_async_tx_descriptor *tx,
				    size_t *payload_len, size_t *max_len)
{
	struct altera_msgdma_private *priv = container_of(tx->chan,
							  struct altera_msgdma_private,
							  dma_chan);
	ptrdiff_t desc_index;

	desc_index = tx - priv->async_tx;

	if (!(desc_index >= 0 && desc_index < DMA_RING_SIZE))
		return NULL;

	if (payload_len)
		*payload_len = sizeof(struct altera_dma_metadata);

	if (max_len)
		*max_len = sizeof(struct altera_dma_metadata);

	return &priv->dma_metadata[desc_index];
}

static u32 free_desc_available_locked(struct altera_msgdma_private *priv)
{
	const u32 chan_size = DMA_RING_SIZE;
	const u64 pending = priv->pref_pending;
	const u64 prod = priv->pref_prod + pending;
	u64 cons = 0;

	/* coverity[missing_lock] */
	cons = READ_ONCE(priv->pref_cons);

	if (pending >= chan_size)
		return 0;

	return cons + chan_size - prod - 1;
}

static int msgdma_desc_free(struct dma_async_tx_descriptor *tx)
{
	async_tx_clear_ack(tx);

	tx->cookie = DMA_MIN_COOKIE;
	tx->callback = NULL;
	tx->callback_param = NULL;
	tx->callback_result = NULL;
	tx->flags = 0;

	return 0;
}

static struct dma_descriptor_metadata_ops msgaltera_dma_metadata_ops = {
	.get_ptr = mgdma_get_metadata_ptr,
};

static int msgdma_pref_initialize(struct altera_msgdma_private *priv)
{
	struct msgdma_pref_extended_desc *descs;
	struct altera_dma_metadata meta;
	dma_addr_t descsphys;
	int i;

	BUILD_BUG_ON(!is_power_of_2(DMA_RING_SIZE));

	priv->async_tx = devm_kcalloc(priv->dma_dev.dev, DMA_RING_SIZE,
				      sizeof(struct dma_async_tx_descriptor), GFP_KERNEL);
	if (!priv->async_tx)
		return -ENOMEM;

	priv->dma_metadata = devm_kcalloc(priv->dma_dev.dev, DMA_RING_SIZE,
					  sizeof(meta), GFP_KERNEL);
	if (!priv->dma_metadata)
		return -ENOMEM;

	priv->pref_desc = dmam_alloc_coherent(priv->dma_dev.dev,
					      array_size(DMA_RING_SIZE, sizeof(*descs)),
					      &priv->pref_descphys, GFP_KERNEL);
	if (!priv->pref_desc)
		return -ENOMEM;

	descs = priv->pref_desc;

	priv->pref_prod    = 0;
	priv->pref_cons    = 0;
	priv->pref_pending = 0;

	for (i = 0; i < DMA_RING_SIZE; i++) {
		descsphys = priv->pref_descphys +
			    (((i + 1) & (DMA_RING_SIZE - 1)) *
			     sizeof(struct msgdma_pref_extended_desc));

		descs[i].next_desc_lo = lower_32_bits(descsphys);
		descs[i].next_desc_hi = upper_32_bits(descsphys);
		descs[i].burst_seq_num = i;

		dma_async_tx_descriptor_init(&priv->async_tx[i], &priv->dma_chan);
		priv->async_tx[i].tx_submit = dma_cookie_assign;
		priv->async_tx[i].metadata_ops = &msgaltera_dma_metadata_ops;
		priv->async_tx[i].desc_free = msgdma_desc_free;
		priv->async_tx[i].cookie = DMA_MIN_COOKIE;
	}

	return 0;
}

static void altera_msgdma_confirm_fill_levels(struct altera_msgdma_private *priv)
{
	int ret;
	u32 val;

	ret = readl_poll_timeout(priv->dma_csr + msgdma_csroffs(rw_fill_level),
				 val, (val & MSGDMA_CSR_FILL_LEVEL_VALID) == 0,
				 1, MSGDMA_SW_RESET_TIMEOUT_US);
	if (ret)
		dev_dbg(priv->dma_dev.dev,
			"DMA RW Fill level never cleared! 0x%lX\n",
			(val & MSGDMA_CSR_FILL_LEVEL_VALID));

	ret = readl_poll_timeout(priv->dma_csr + msgdma_csroffs(resp_fill_level),
				 val, MSGDMA_CSR_RESP_FILL_LEVEL_GET(val) == 0,
				 1, MSGDMA_SW_RESET_TIMEOUT_US);
	if (ret)
		dev_warn(priv->dma_dev.dev,
			 "DMA Resp Fill level never cleared! 0x%lX\n",
			 MSGDMA_CSR_RESP_FILL_LEVEL_GET(val));
}

static int altera_msgdma_pref_quiesce(struct altera_msgdma_private *priv)
{
	int ret;
	u32 val;

	dma_set_bit(priv->dma_csr, msgdma_csroffs(control), MSGDMA_CSR_CTL_STOP_DESCS);

	ret = readl_poll_timeout(priv->dma_csr + msgdma_csroffs(status), val,
				 val & MSGDMA_CSR_STAT_STOPPED, 1,
				 MSGDMA_SW_RESET_TIMEOUT_US);

	return ret;
}

static int altera_msgdma_reset(struct altera_msgdma_private *priv)
{
	int ret;
	u32 val;

	dma_write(MSGDMA_CSR_STAT_MASK, priv->dma_csr, msgdma_csroffs(status));
	dma_write(MSGDMA_CSR_CTL_RESET, priv->dma_csr, msgdma_csroffs(control));

	ret = readl_poll_timeout(priv->dma_csr + msgdma_csroffs(status), val,
				 (val & MSGDMA_CSR_STAT_RESETTING) == 0, 1,
				 MSGDMA_SW_RESET_TIMEOUT_US);
	if (ret)
		dev_warn(priv->dma_dev.dev,
			 "mSGDMA resetting bit never cleared!\n");

	dma_write(MSGDMA_CSR_STAT_MASK, priv->dma_csr, msgdma_csroffs(status));

	return ret;
}

static int altera_msgdma_pref_reset(struct altera_msgdma_private *priv)
{
	int ret;
	u32 val;

	dma_clear_bit(priv->pref_csr, msgdma_pref_csroffs(control),
		      MSGDMA_PREF_CTL_DESC_POLL_EN);

	altera_msgdma_pref_quiesce(priv);

	altera_msgdma_confirm_fill_levels(priv);

	dma_write(MSGDMA_PREF_STAT_IRQ, priv->pref_csr, msgdma_pref_csroffs(status));
	dma_write(MSGDMA_PREF_CTL_RESET, priv->pref_csr, msgdma_pref_csroffs(control));

	ret = readl_poll_timeout(priv->pref_csr + msgdma_pref_csroffs(control),
				 val, (val & MSGDMA_PREF_CTL_RESET) == 0, 1,
				 MSGDMA_SW_RESET_TIMEOUT_US);
	if (ret)
		dev_warn(priv->dma_dev.dev,
			 "Prefetcher reset bit never cleared!\n");

	return altera_msgdma_reset(priv);
}

static void altera_msgdma_pref_start_dma(struct altera_msgdma_private *priv)
{
	dma_write(priv->chan_poll_freq, priv->pref_csr,
		  msgdma_pref_csroffs(desc_poll_freq));
	dma_write(lower_32_bits(priv->pref_descphys), priv->pref_csr,
		  msgdma_pref_csroffs(next_desc_lo));
	dma_write(upper_32_bits(priv->pref_descphys), priv->pref_csr,
		  msgdma_pref_csroffs(next_desc_hi));
	dma_set_bit(priv->pref_csr, msgdma_pref_csroffs(control),
		    MSGDMA_PREF_CTL_DESC_POLL_EN | MSGDMA_PREF_CTL_RUN);
}

static u64 timestamp_to_ns(struct msgdma_pref_extended_desc *desc)
{
	u32 tmp;
	u64 second;
	u64 prod, ns;

	tmp = desc->timestamp_96b[0] >> 16;
	tmp |= (desc->timestamp_96b[1] << 16);

	second = desc->timestamp_96b[2];
	second <<= 16;
	second |= ((desc->timestamp_96b[1] & GENMASK(31, 16)) >> 16);

	/*
	 * second can span up to 48 bits; second * NSEC_PER_SEC can exceed
	 * U64_MAX. A u64 cannot represent larger instants; saturate.
	 */
	if (check_mul_overflow(second, (u64)NSEC_PER_SEC, &prod))
		return U64_MAX;
	if (check_add_overflow(prod, (u64)tmp, &ns))
		return U64_MAX;

	return ns;
}

static enum dma_status altera_msgdma_tx_status(struct dma_chan *chan,
					       dma_cookie_t cookie,
					       struct dma_tx_state *state)
{
	struct altera_msgdma_private *priv;
	enum dma_status status;

	priv = chantopriv(chan);
	status = dma_cookie_status(chan, cookie, state);

	if (atomic_read_acquire(&priv->dma_paused))
		status = DMA_PAUSED;

	return status;
}

static struct dma_async_tx_descriptor *
msgdma_pref_prepare_desc(struct altera_msgdma_private *priv,
			 struct altera_msgdma_buffer *buffer,
			 enum dma_transfer_direction dir)
{
	struct msgdma_pref_extended_desc *descs = priv->pref_desc;
	const char *dir_tag = (dir == DMA_MEM_TO_DEV) ? "Tx" : "Rx";
	u32 desc_entry;

	spin_lock(&priv->prod_lock);

	if (free_desc_available_locked(priv) < 1) {
		spin_unlock(&priv->prod_lock);
		dev_err(priv->dma_dev.dev,
			"No free descriptor are available");
		return NULL;
	}

	desc_entry = (priv->pref_prod + priv->pref_pending) & (DMA_RING_SIZE - 1);

	dma_rmb();

	if (unlikely(descs[desc_entry].desc_control & MSGDMA_PREF_DESC_CTL_OWNED_BY_HW)) {
		spin_unlock(&priv->prod_lock);
		dev_err(priv->dma_dev.dev,
			"%s: desc_entry %u MSGDMA_PREF_DESC_CTL_OWNED_BY_HW set\n",
			dir_tag, desc_entry);
		return NULL;
	}

	descs[desc_entry].len = buffer->len;

	if (dir == DMA_MEM_TO_DEV) {
		descs[desc_entry].read_addr_lo = lower_32_bits(buffer->dma_addr);
		descs[desc_entry].read_addr_hi = upper_32_bits(buffer->dma_addr);
		descs[desc_entry].stride = MSGDMA_DESC_TX_STRIDE;
		priv->async_tx[desc_entry].flags = buffer->flags;
		descs[desc_entry].desc_control = MSGDMA_DESC_CTL_TX_SINGLE;
	} else {
		descs[desc_entry].write_addr_lo = lower_32_bits(buffer->dma_addr);
		descs[desc_entry].write_addr_hi = upper_32_bits(buffer->dma_addr);
		descs[desc_entry].stride = MSGDMA_DESC_RX_STRIDE;
		descs[desc_entry].desc_control = (MSGDMA_DESC_CTL_END_ON_EOP  |
						  MSGDMA_DESC_CTL_END_ON_LEN  |
						  MSGDMA_DESC_CTL_TR_COMP_IRQ |
						  MSGDMA_DESC_CTL_TR_ERR_IRQ  |
						  MSGDMA_DESC_CTL_GO);
	}

	priv->pref_pending++;

	spin_unlock(&priv->prod_lock);

	return &priv->async_tx[desc_entry];
}

static struct dma_async_tx_descriptor *
altera_msgdma_prep_slave_sg(struct dma_chan *dchan,
			    struct scatterlist *sgl,
			    unsigned int sg_len,
			    enum dma_transfer_direction dir,
			    unsigned long dma_flags, void *context)
{
	struct altera_msgdma_private *priv;
	struct altera_msgdma_buffer buf;
	struct dma_async_tx_descriptor *client_desc;

	priv = chantopriv(dchan);
	client_desc = NULL;

	if (!sgl || sg_len != 1 ||
	    (dir != DMA_MEM_TO_DEV && dir != DMA_DEV_TO_MEM) ||
	    sg_dma_len(&sgl[0]) > MSGDMA_MAX_TRANS_LEN)
		return NULL;

	buf.dma_addr = sg_dma_address(&sgl[0]);
	buf.len = sg_dma_len(&sgl[0]);
	buf.flags = 0;

	client_desc = msgdma_pref_prepare_desc(priv, &buf, dir);

	if (client_desc)
		client_desc->flags = dma_flags;

	return client_desc;
}

static void copy_metadata_info(struct msgdma_pref_extended_desc *desc,
			       struct altera_dma_metadata *metadata)
{
	metadata->timestamp_u64 = timestamp_to_ns(desc);
	metadata->transfer_len = desc->bytes_transferred;
}

static u32 msgdma_pref_process_completions(struct altera_msgdma_private *priv,
					   struct dmaengine_desc_callback *irq_cbs,
					   bool *irq_need_cb)
{
	struct msgdma_pref_extended_desc *desc = priv->pref_desc;
	struct dma_async_tx_descriptor *async_tx = priv->async_tx;
	u64 snap_prod;
	u64 snap_cons;
	u32 n_cbs = 0;

	/* coverity[missing_lock] */
	snap_prod = READ_ONCE(priv->pref_prod);

	spin_lock(&priv->cons_lock);

	snap_cons = READ_ONCE(priv->pref_cons);

	while (snap_cons < snap_prod) {
		u32 real_cons = snap_cons & (DMA_RING_SIZE - 1);
		u32 ctl;

		/* coverity[missing_lock] */
		ctl = data_race(desc[real_cons].desc_control);

		if (ctl & MSGDMA_PREF_DESC_CTL_OWNED_BY_HW)
			break;

		dma_rmb();

		copy_metadata_info(&desc[real_cons], &priv->dma_metadata[real_cons]);
		dma_cookie_complete(&async_tx[real_cons]);

		dmaengine_desc_get_callback(&async_tx[real_cons], &irq_cbs[n_cbs]);
		irq_need_cb[n_cbs] = !!(async_tx[real_cons].flags & DMA_PREP_INTERRUPT);
		n_cbs++;
		msgdma_desc_free(&async_tx[real_cons]);

		WRITE_ONCE(priv->pref_cons, snap_cons + 1);

		snap_cons++;
	}

	spin_unlock(&priv->cons_lock);

	return n_cbs;
}

static irqreturn_t altera_msgdma_irq_thread(int irq, void *dev_id)
{
	struct altera_msgdma_private *priv;
	struct dmaengine_result dma_result = {.result = DMA_TRANS_NOERROR};
	struct dma_chan *dchan = dev_id;
	struct dmaengine_desc_callback *irq_cbs;
	bool *irq_need_cb;
	u32 n_cbs;
	bool new_irq;

	priv = chantopriv(dchan);
	irq_cbs = priv->irq_cbs;
	irq_need_cb = priv->irq_need_cb;

	do {
		n_cbs = msgdma_pref_process_completions(priv, irq_cbs, irq_need_cb);

		for (u32 i = 0; i < n_cbs; i++) {
			if (irq_need_cb[i])
				dmaengine_desc_callback_invoke(&irq_cbs[i], &dma_result);
		}

		new_irq = altera_msgdma_pref_isirq(priv);

		if (new_irq)
			altera_msgdma_pref_clear_irq(priv);

	} while (new_irq);

	return IRQ_HANDLED;
}

static irqreturn_t altera_msgdma_hard_irq(int irq, void *dev_id)
{
	struct dma_chan *dchan = dev_id;
	struct altera_msgdma_private *priv = chantopriv(dchan);

	if (!altera_msgdma_pref_isirq(priv))
		return IRQ_NONE;

	altera_msgdma_pref_clear_irq(priv);

	return IRQ_WAKE_THREAD;
}

static void altera_msgdma_issue_pending(struct dma_chan *chan)
{
	struct altera_msgdma_private *priv;
	struct msgdma_pref_extended_desc *desc;
	u32 idx;

	priv = chantopriv(chan);
	desc = priv->pref_desc;

	spin_lock(&priv->prod_lock);

	dma_wmb();
	for (unsigned int ix = 0, pend_transfer = priv->pref_pending;
	     ix < pend_transfer; ix++) {
		idx = priv->pref_prod & (DMA_RING_SIZE - 1);

		desc[idx].desc_control |= MSGDMA_PREF_DESC_CTL_OWNED_BY_HW;

		priv->pref_prod++;
		priv->pref_pending--;
	}

	spin_unlock(&priv->prod_lock);
}

static int altera_msgdma_alloc_chan_resources(struct dma_chan *dchan)
{
	int ret;
	struct altera_msgdma_private *priv;

	priv = chantopriv(dchan);

	ret = altera_msgdma_pref_reset(priv);
	if (ret)
		return ret;

	altera_msgdma_dmaintr_disable(priv);

	altera_msgdma_pref_start_dma(priv);
	enable_irq(priv->irq);

	altera_msgdma_dmaintr_enable(priv);
	atomic_set_release(&priv->dma_paused, 0);

	return 0;
}

static int altera_msgdma_device_pause(struct dma_chan *chan)
{
	struct altera_msgdma_private *priv;

	priv = chantopriv(chan);

	if (atomic_read_acquire(&priv->dma_paused))
		return 0;

	disable_irq(priv->irq);

	spin_lock(&priv->prod_lock);

	dma_clear_bit(priv->pref_csr, msgdma_pref_csroffs(control),
		      MSGDMA_PREF_CTL_RUN | MSGDMA_PREF_CTL_DESC_POLL_EN);

	spin_unlock(&priv->prod_lock);

	atomic_set_release(&priv->dma_paused, 1);

	return 0;
}

static int altera_msgdma_device_resume(struct dma_chan *chan)
{
	struct altera_msgdma_private *priv;

	priv = chantopriv(chan);

	if (!atomic_read_acquire(&priv->dma_paused))
		return 0;

	enable_irq(priv->irq);

	spin_lock(&priv->prod_lock);

	dma_clear_bit(priv->dma_csr, msgdma_csroffs(control),
		      MSGDMA_CSR_CTL_STOP_DESCS);
	dma_set_bit(priv->pref_csr, msgdma_pref_csroffs(control),
		    MSGDMA_PREF_CTL_RUN | MSGDMA_PREF_CTL_DESC_POLL_EN);

	altera_msgdma_dmaintr_enable(priv);

	spin_unlock(&priv->prod_lock);

	atomic_set_release(&priv->dma_paused, 0);

	return 0;
}

static void altera_msgdma_slave_caps(struct dma_chan *dchan,
				     struct dma_slave_caps *caps)
{
	struct altera_msgdma_private *priv;

	priv = chantopriv(dchan);

	caps->directions =  BIT(DMA_MEM_TO_DEV) |
			    BIT(DMA_DEV_TO_MEM);

	caps->src_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_4_BYTES) |
				BIT(DMA_SLAVE_BUSWIDTH_8_BYTES);

	caps->dst_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_4_BYTES) |
				BIT(DMA_SLAVE_BUSWIDTH_8_BYTES);

	caps->residue_granularity = DMA_RESIDUE_GRANULARITY_DESCRIPTOR;
	caps->descriptor_reuse = false;
	caps->min_burst = 1;
	caps->max_burst = priv->prefetch_capability;
	caps->max_sg_burst = SG_NO_SUPPORT;
}

static int altera_msgdma_device_terminate_all(struct dma_chan *dchan)
{
	struct altera_msgdma_private *priv;
	struct msgdma_pref_extended_desc *desc;
	int ret;

	priv = chantopriv(dchan);
	desc = priv->pref_desc;

	altera_msgdma_dmaintr_disable(priv);
	disable_irq(priv->irq);

	atomic_set_release(&priv->dma_paused, 1);

	dma_clear_bit(priv->pref_csr, msgdma_pref_csroffs(control),
		      MSGDMA_PREF_CTL_RUN | MSGDMA_PREF_CTL_DESC_POLL_EN);

	ret = altera_msgdma_pref_quiesce(priv);
	if (ret)
		dev_warn(priv->dma_dev.dev,
			 "DMA quiesce timeout during terminate\n");

	spin_lock(&priv->prod_lock);

	for (u64 ix = 0; ix < DMA_RING_SIZE; ix++) {
		desc[ix].desc_control = 0;
		msgdma_desc_free(&priv->async_tx[ix]);
	}

	priv->pref_cons = 0;
	priv->pref_prod = 0;
	priv->pref_pending = 0;
	spin_unlock(&priv->prod_lock);

	return 0;
}

static void altera_msgdma_synchronize(struct dma_chan *dchan)
{
	struct dmaengine_result abort = {.result = DMA_TRANS_ABORTED};
	struct altera_msgdma_private *priv = chantopriv(dchan);
	struct dmaengine_desc_callback *cbs = priv->irq_cbs;
	bool *need_cb = priv->irq_need_cb;
	struct dma_async_tx_descriptor *tx;
	u64 snap_cons, snap_prod;
	u32 pend_count, n_cbs = 0;
	u32 mask;

	disable_irq(priv->irq);

	mask = DMA_RING_SIZE - 1;

	spin_lock_bh(&priv->prod_lock);

	snap_cons  = priv->pref_cons;
	snap_prod  = priv->pref_prod;
	pend_count = priv->pref_pending;

	for (u64 ix = snap_cons; ix < snap_prod; ix++) {
		u32 slot = ix & mask;

		tx = &priv->async_tx[slot];
		dma_cookie_complete(tx);
		dmaengine_desc_get_callback(tx, &cbs[n_cbs]);
		need_cb[n_cbs++] = !!(tx->flags & DMA_PREP_INTERRUPT);
		msgdma_desc_free(tx);
	}

	for (u32 i = 0; i < pend_count; i++) {
		u32 slot = (snap_prod + i) & mask;

		tx = &priv->async_tx[slot];
		dma_cookie_complete(tx);
		dmaengine_desc_get_callback(tx, &cbs[n_cbs]);
		need_cb[n_cbs++] = !!(tx->flags & DMA_PREP_INTERRUPT);
		msgdma_desc_free(tx);
	}

	priv->pref_cons    = snap_prod;
	priv->pref_pending = 0;

	spin_unlock_bh(&priv->prod_lock);

	for (u32 i = 0; i < n_cbs; i++) {
		if (need_cb[i])
			dmaengine_desc_callback_invoke(&cbs[i], &abort);
	}

	enable_irq(priv->irq);
}

static void altera_msgdma_free_chan_resources(struct dma_chan *chan)
{
	altera_msgdma_device_terminate_all(chan);
}

static void altera_msgdma_remove(struct platform_device *pdev)
{
	struct altera_msgdma_private *priv;

	priv = platform_get_drvdata(pdev);

	altera_msgdma_free_chan_resources(&priv->dma_chan);

	dma_async_device_unregister(&priv->dma_dev);

	if (pdev->dev.of_node)
		of_dma_controller_free(pdev->dev.of_node);
}

static int altera_msgdma_registration(struct platform_device *pdev)
{
	struct altera_msgdma_private *priv;
	int ret = 0;

	priv = platform_get_drvdata(pdev);

	dma_cookie_init(&priv->dma_chan);
	dma_cap_zero(priv->dma_dev.cap_mask);
	dma_cap_set(DMA_SLAVE, priv->dma_dev.cap_mask);

	priv->dma_dev.directions =  BIT(DMA_MEM_TO_DEV) |
				    BIT(DMA_DEV_TO_MEM);

	priv->dma_dev.max_sg_burst = SG_NO_SUPPORT;

	INIT_LIST_HEAD(&priv->dma_dev.channels);

	priv->dma_chan.device = &priv->dma_dev;
	priv->dma_dev.dev = &pdev->dev;
	priv->dma_chan.slave = &pdev->dev;
	list_add_tail(&priv->dma_chan.device_node, &priv->dma_dev.channels);

	priv->dma_dev.desc_metadata_modes = DESC_METADATA_ENGINE;
	priv->dma_dev.device_alloc_chan_resources = altera_msgdma_alloc_chan_resources;
	priv->dma_dev.device_free_chan_resources = altera_msgdma_free_chan_resources;
	priv->dma_dev.device_prep_slave_sg = altera_msgdma_prep_slave_sg;
	priv->dma_dev.device_tx_status = altera_msgdma_tx_status;
	priv->dma_dev.device_issue_pending = altera_msgdma_issue_pending;
	priv->dma_dev.device_pause = altera_msgdma_device_pause;
	priv->dma_dev.device_resume = altera_msgdma_device_resume;
	priv->dma_dev.device_synchronize = altera_msgdma_synchronize;
	priv->dma_dev.device_terminate_all = altera_msgdma_device_terminate_all;
	priv->dma_dev.device_caps = altera_msgdma_slave_caps;
	priv->dma_dev.copy_align = DMAENGINE_ALIGN_4_BYTES;

	ret = dma_async_device_register(&priv->dma_dev);
	if (ret) {
		dev_err(&pdev->dev, "dma async device register failed!\n");
		return ret;
	}

	ret = of_dma_controller_register(pdev->dev.of_node,
					 of_dma_xlate_by_chan_id,
					 &priv->dma_dev);
	if (ret) {
		dev_warn(&pdev->dev, "DMA controller registration failed\n");
		dma_async_device_unregister(&priv->dma_dev);
		return ret;
	}

	return 0;
}

static int altera_msgdma_dts_config(struct platform_device *pdev)
{
	struct altera_msgdma_private *priv;
	struct device_node *dmanp;
	const char *chan_name;
	int ret = 0;

	priv = platform_get_drvdata(pdev);

	dmanp = pdev->dev.of_node;

	priv->irq = platform_get_irq_byname(pdev, "dma-irq");
	if (priv->irq < 0)
		return priv->irq;

	ret = altera_request_and_map(pdev, "dma_csr",
				     (void __iomem **)(&priv->dma_csr));
	if (ret)
		return ret;

	ret = altera_request_and_map(pdev, "pref_csr",
				     (void __iomem **)(&priv->pref_csr));
	if (ret)
		return ret;

	if (of_property_read_u32(dmanp, "fifo-almost-full", &priv->fifo_almost_full)) {
		dev_info(&pdev->dev, "cannot obtain fifo-almost-full\n");
		priv->fifo_almost_full = 0x4000;
	}

	if (of_property_read_u32(dmanp, "fifo-almost-empty", &priv->fifo_almost_empty)) {
		dev_info(&pdev->dev, "cannot obtain fifo-almost-empty\n");
		priv->fifo_almost_empty = 0x3000;
	}

	if (of_property_read_u32(dmanp, "poll-freq", &priv->chan_poll_freq)) {
		dev_info(&pdev->dev, "Defaulting Poll Frequency to 128\n");
		priv->chan_poll_freq = 128;
	}

	ret = altera_request_and_map(pdev, "fifo_info",
				     (void __iomem **)(&priv->fifo_info));

	if (ret)
		return ret;

	dma_write(priv->fifo_almost_full, priv->fifo_info,
		  fifo_csroffs(almost_full_threshold));

	dma_write(priv->fifo_almost_empty, priv->fifo_info,
		  fifo_csroffs(almost_empty_threshold));

	if (of_property_read_u32(dmanp, "prefetch-capability",
				 &priv->prefetch_capability)) {
		dev_info(&pdev->dev, "cannot obtain pref capability defaulting\n");
		priv->prefetch_capability = 0x10;
	}

	if (!of_property_read_string(dmanp, "channel-name", &chan_name))
		priv->dma_chan.name = devm_kstrdup(&pdev->dev, chan_name, GFP_KERNEL);
	else
		priv->dma_chan.name = devm_kstrdup(&pdev->dev,
						   dev_name(&pdev->dev), GFP_KERNEL);

	return 0;
}

static int altera_msgdma_probe(struct platform_device *pdev)
{
	struct altera_msgdma_private *priv;
	const char *chan_name = NULL;
	int ret = -ENOMEM;

	priv = devm_kzalloc(&pdev->dev, sizeof(struct altera_msgdma_private),
			    GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);

	if (!pdev->dev.of_node) {
		dev_err(&pdev->dev, "device tree node is required\n");
		return -ENODEV;
	}

	priv->msg_enable = msgdma_debug_level;
	if (!msgdma_debug_level)
		priv->msg_enable = dma_msg_init(DMA_MSG_DEFAULT);

	spin_lock_init(&priv->prod_lock);
	spin_lock_init(&priv->cons_lock);
	atomic_set(&priv->dma_paused, 0);

	ret = altera_msgdma_dts_config(pdev);
	if (ret) {
		dev_err(&pdev->dev, "DMA basic DTS entry are missing\n");
		return ret;
	}

	ret = altera_msgdma_registration(pdev);
	if (ret) {
		dev_err(&pdev->dev, "DMA engine registration failed\n");
		return ret;
	}

	ret = msgdma_pref_initialize(priv);
	if (ret) {
		dma_async_device_unregister(&priv->dma_dev);
		if (pdev->dev.of_node)
			of_dma_controller_free(pdev->dev.of_node);
		dev_err(&pdev->dev, "Failure in pref initialization err(%d)\n", ret);
		return ret;
	}

	chan_name = dev_name(priv->dma_dev.dev);

	ret = devm_request_threaded_irq(priv->dma_dev.dev, priv->irq,
					altera_msgdma_hard_irq,
					altera_msgdma_irq_thread,
					IRQF_ONESHOT | IRQF_NO_AUTOEN,
					priv->dma_chan.name,
					&priv->dma_chan);

	if (ret) {
		dma_async_device_unregister(&priv->dma_dev);
		if (pdev->dev.of_node)
			of_dma_controller_free(pdev->dev.of_node);
		dev_err(&pdev->dev, "Unable to register interrupt %d\n", priv->irq);
		return ret;
	}

	if (dma_msg_probe(priv))
		dev_info(&pdev->dev, "%s channel ready to operate\n", chan_name);

	return 0;
}

static const struct of_device_id altera_msgdma_match[] = {
	{ .compatible = "altr,socfpga-pref-msgdma",
	},
	{ }
};

MODULE_DEVICE_TABLE(of, altera_msgdma_match);

static struct platform_driver msgdma_driver = {
	.driver = {
		.name = "altera-msgdma-prefetch",
		.of_match_table = altera_msgdma_match,
	},
	.probe = altera_msgdma_probe,
	.remove = altera_msgdma_remove,
};

module_platform_driver(msgdma_driver);

MODULE_ALIAS("altera-msgdma-prefetcher");
MODULE_AUTHOR("Preetam Narayan");
MODULE_DESCRIPTION("Altera mSGDMA Prefetcher DMA Engine Driver");
MODULE_LICENSE("GPL");
