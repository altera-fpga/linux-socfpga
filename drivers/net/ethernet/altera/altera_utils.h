/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2014,2026 Altera Corporation. All rights reserved
 */

#ifndef __ALTERA_UTILS_H__
#define __ALTERA_UTILS_H__

#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/io.h>

void tse_set_bit(void __iomem *ioaddr, size_t offs, u32 bit_mask);
void tse_clear_bit(void __iomem *ioaddr, size_t offs, u32 bit_mask);
int tse_bit_is_set(void __iomem *ioaddr, size_t offs, u32 bit_mask);
int tse_bit_is_clear(void __iomem *ioaddr, size_t offs, u32 bit_mask);

static inline u32 csrrd32(const void __iomem *mac, size_t offs)
{
	return readl((const u8 __iomem *)mac + offs);
}

static inline u16 csrrd16(const void __iomem *mac, size_t offs)
{
	return readw((const u8 __iomem *)mac + offs);
}

static inline u8 csrrd8(const void __iomem *mac, size_t offs)
{
	return readb((const u8 __iomem *)mac + offs);
}

static inline void csrwr32(u32 val, void __iomem *mac, size_t offs)
{
	writel(val, (u8 __iomem *)mac + offs);
}

static inline void csrwr16(u16 val, void __iomem *mac, size_t offs)
{
	writew(val, (u8 __iomem *)mac + offs);
}

static inline void csrwr8(u8 val, void __iomem *mac, size_t offs)
{
	writeb(val, (u8 __iomem *)mac + offs);
}
#endif /* __ALTERA_UTILS_H__*/
