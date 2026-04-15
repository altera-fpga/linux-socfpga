/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2026, Altera Corporation. All rights reserved
 */

#ifndef _ALTERA_DMA_METADATA_H
#define _ALTERA_DMA_METADATA_H

#include <linux/types.h>

struct altera_dma_metadata {
	u64 timestamp_u64;
	u32 transfer_len;
};

#endif
