/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _ALTERA_MSGDMA_PREF_H
#define _ALTERA_MSGDMA_PREF_H

#include <linux/dma/altera_dma_metadata.h>
#include <linux/dmaengine.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include "../dmaengine.h"

#define DMA_RING_SIZE					    512
#define SG_NO_SUPPORT					      1

#define MSGDMA_PREF_CTL_GLOBAL_INTR                      BIT(3)
#define MSGDMA_PREF_CTL_RESET                            BIT(2)
#define MSGDMA_PREF_CTL_DESC_POLL_EN                     BIT(1)
#define MSGDMA_PREF_CTL_RUN                              BIT(0)

#define MSGDMA_PREF_STAT_IRQ                             BIT(0)

#define MSGDMA_DESC_TX_STRIDE                      (0x00010001)
#define MSGDMA_DESC_RX_STRIDE                      (0x00010001)
#define MSGDMA_PREF_DESC_CTL_OWNED_BY_HW                BIT(30)
#define MSGDMA_DESC_CTL_GEN_SOP                          BIT(8)
#define MSGDMA_DESC_CTL_GEN_EOP                          BIT(9)
#define MSGDMA_DESC_CTL_END_ON_EOP			BIT(12)
#define MSGDMA_DESC_CTL_END_ON_LEN		        BIT(13)
#define MSGDMA_DESC_CTL_TR_COMP_IRQ		        BIT(14)
#define MSGDMA_DESC_CTL_GO				BIT(31)
#define MSGDMA_DESC_CTL_EARLY_IRQ			BIT(15)
#define MSGDMA_DESC_CTL_TR_ERR_IRQ              GENMASK(23, 16)
#define MSGDMA_DESC_CTL_TX_SINGLE       (MSGDMA_DESC_CTL_GEN_SOP |      \
					 MSGDMA_DESC_CTL_GEN_EOP |      \
					 MSGDMA_DESC_CTL_TR_COMP_IRQ |  \
					 MSGDMA_DESC_CTL_GO)

#define MSGDMA_SW_RESET_TIMEOUT_US                       10000
#define MSGDMA_CSR_FILL_LEVEL_VALID               GENMASK(31, 0)
#define MSGDMA_CSR_RESP_FILL_LEVEL_GET(v)   ((v) & GENMASK(15, 0))

#define MSGDMA_CSR_STAT_STOPPED                         BIT(5)
#define MSGDMA_CSR_CTL_STOP_DESCS                       BIT(5)

#define MSGDMA_CSR_STAT_MASK                             GENMASK(9, 0)
#define MSGDMA_CSR_CTL_RESET                             BIT(1)
#define MSGDMA_CSR_STAT_RESETTING                        BIT(6)

#define MSGDMA_MAX_TRANS_LEN				U32_MAX

struct msgdma_pref_extended_desc {
	u32 read_addr_lo;
	u32 write_addr_lo;
	u32 len;
	u32 next_desc_lo;
	u32 bytes_transferred;
	u32 desc_status;
	u32 reserved_18;
	u32 burst_seq_num;
	u32 stride;
	u32 read_addr_hi;
	u32 write_addr_hi;
	u32 next_desc_hi;
	u32 timestamp_96b[3];
	u32 desc_control;
};

struct msgdma_pref_csr {
	u32 control;
	u32 next_desc_lo;
	u32 next_desc_hi;
	u32 desc_poll_freq;
	u32 status;
};

#define msgdma_pref_csroffs(a) (offsetof(struct msgdma_pref_csr, a))

struct msgdma_csr {
	u32 status;
	u32 control;
	u32 rw_fill_level;
	u32 resp_fill_level;
	u32 rw_seq_num;
	u32 reserved[3];
};

#define msgdma_csroffs(a) (offsetof(struct msgdma_csr, a))

struct altera_fpga_chan_fifo {
	u32 fill_level;
	u32 reserved;
	u32 almost_full_threshold;
	u32 almost_empty_threshold;
	u32 cut_through_threshold;
	u32 drop_on_error;
};

#define fifo_csroffs(a) (offsetof(struct altera_fpga_chan_fifo, a))

struct altera_msgdma_private {
	struct dmaengine_desc_callback irq_cbs[DMA_RING_SIZE];
	bool                           irq_need_cb[DMA_RING_SIZE];

	/* Protects pref_prod, pref_pending and async_tx on the producer side. */
	spinlock_t prod_lock;
	/* Protects pref_cons and async_tx on the consumer (IRQ thread) side. */
	spinlock_t cons_lock;

	u64 pref_prod;
	u32 pref_pending;
	u64 pref_cons;
	void __iomem *pref_csr;
	void __iomem *dma_csr;
	int irq;
	atomic_t dma_paused;

	struct dma_device dma_dev;
	struct dma_chan dma_chan;
	struct msgdma_pref_extended_desc *pref_desc;
	struct dma_async_tx_descriptor *async_tx;
	struct altera_dma_metadata *dma_metadata;
	dma_addr_t pref_descphys;
	void __iomem *fifo_info;
	u32 prefetch_capability;
	u32 fifo_almost_empty;
	u32 fifo_almost_full;
	u32 chan_poll_freq;
	unsigned int msg_enable;
};

#define chantopriv(dchan) container_of(dchan, struct altera_msgdma_private, dma_chan)

struct altera_msgdma_buffer {
	u32 len;
	dma_addr_t dma_addr;
	unsigned long flags;
};

#endif
