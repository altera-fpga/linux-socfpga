/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026, Altera Corporation. All rights reserved
 */

#ifndef _DMAENGINE_ALTERA_DMA_UTILS_H
#define _DMAENGINE_ALTERA_DMA_UTILS_H

#include <linux/io.h>
#include <linux/types.h>

#define dma_msg_probe(p)    ((p)->msg_enable & DMA_MSG_PROBE)

enum {
	DMA_MSG_ERR      = 0x0001,
	DMA_MSG_PROBE    = 0x0002,
	DMA_MSG_DEFAULT  = DMA_MSG_ERR | DMA_MSG_PROBE,
};

static inline u32 dma_msg_init(int default_msg_enable)
{
	return default_msg_enable;
}

static inline u32 dma_read(void __iomem *ioaddr, size_t offs)
{
	u32 value = ioread32((void __iomem *)((u8 __iomem *)ioaddr + offs));

	return value;
}

static inline void dma_write(u32 value, void __iomem *ioaddr, size_t offs)
{
	iowrite32(value, (void __iomem *)((u8 __iomem *)ioaddr + offs));
}

static inline void dma_set_bit(void __iomem *ioaddr, size_t offs, u32 bit_mask)
{
	u32 value = dma_read(ioaddr, offs);

	value |= bit_mask;
	dma_write(value, ioaddr, offs);
}

static inline void dma_clear_bit(void __iomem *ioaddr, size_t offs, u32 bit_mask)
{
	u32 value = dma_read(ioaddr, offs);

	value &= ~bit_mask;
	dma_write(value, ioaddr, offs);
}

static inline bool dma_bit_is_set(void __iomem *ioaddr, size_t offs, u32 bit_mask)
{
	u32 value = dma_read(ioaddr, offs);

	return !!(value & bit_mask);
}

#endif
