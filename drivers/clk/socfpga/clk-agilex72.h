/* SPDX-License-Identifier:    GPL-2.0 */
/*
 * Copyright (C) 2026, Altera Corporation
 */

#ifndef	__AGILEX72_CLK_H
#define	__AGILEX72_CLK_H

#include <linux/clk-provider.h>

struct agilex72_clock_data {
	/*
	 * MMIO bases ioremapped from DT resources with "reg-names" property
	 * in probe:
	 *   [0] - "clkmgr"  : main clock manager register block
	 *   [1] - "gppll0"  : GP PLL 0 register block
	 *   [2] - "gppll1"  : GP PLL 1 register block
	 *   [3] - "gppll2"  : GP PLL 2 register block
	 */
	void __iomem *base[4];

	/* Must be last */
	struct clk_hw_onecell_data	clk_data;
};

struct agilex72_pll {
	struct clk_gate     hw;
	void __iomem       *pll_base;
};

struct agilex72_periph_clk {
	struct clk_gate  hw;
	void __iomem    *div_reg;
	u8               div_lo_shift;
	u8               div_hi_shift;
};

struct agilex72_gate_clk {
	struct clk_gate hw;
	bool div_linear;
	void __iomem *div_reg;
	void __iomem *bypass_reg;
	u8  div_width;	/* only valid if div_reg != 0 */
	u8  div_shift;	/* only valid if div_reg != 0 */
	u8  bypass_shift;      /* only valid if bypass_reg != 0 */
};

struct agilex72_pll_clock {
	unsigned int	id;
	const char	*name;
	const char	* const *parent_names;
	u8	num_parents;
	unsigned long   offset;
};

struct agilex72_perip_c_clock {
	unsigned int		id;
	const char		*name;
	const char		*parent_name;
	u8			num_parents;
	unsigned long		div_offset;
	u8			div_lo_shift;
	u8			div_hi_shift;
	u8			div_lo_width;
	u8			div_hi_width;
};

struct agilex72_perip_cnt_clock {
	unsigned int		id;
	const char		*name;
	const char	* const *parent_names;
	u8			num_parents;
	unsigned long		offset;
};

struct agilex72_gate_clock {
	unsigned int		id;
	const char		*name;
	const char	* const *parent_names;
	u8			num_parents;
	unsigned long		gate_reg;
	u8			gate_idx;
	unsigned long		div_reg;
	u8			div_offset;
	u8			div_width;
	unsigned long		bypass_reg;
	u8			bypass_shift;
	bool			div_linear;
};

#endif	/* __AGILEX72_CLK_H */
