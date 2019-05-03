/*
 * PCIe phy driver for Samsung EXYNOS8890
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Kyoungil Kim <ki0351.kim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

void exynos_pcie_phy_config(void *phy_base_regs, void *phy_pcs_base_regs, void *sysreg_base_regs, void *elbi_bsae_regs)
{
	/* 26MHz gen1 */
	u32 cmn_config_val[26] = {0x01, 0x0F, 0xA6, 0x31, 0x90, 0x62, 0x20, 0x00, 0x00, 0xA7, 0x0A,
				  0x37, 0x20, 0x08, 0xEF, 0xFC, 0x96, 0x14, 0x00, 0x10, 0x60, 0x01,
				  0x00, 0x00, 0x04, 0x10};
	u32 trsv_config_val[41] = {0x31, 0xF4, 0xF4, 0x80, 0x25, 0x40, 0xD8, 0x03, 0x35, 0x55, 0x4C,
				   0xC3, 0x10, 0x54, 0x70, 0xC5, 0x00, 0x2F, 0x38, 0xA4, 0x00, 0x3B,
				   0x30, 0x9A, 0x64, 0x00, 0x1F, 0x83, 0x1B, 0x01, 0xE0, 0x00, 0x00,
				   0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1D, 0x00};
	int i;

	writel(readl(sysreg_base_regs) & ~(0x1 << 1), sysreg_base_regs);
	writel((((readl(sysreg_base_regs + 0xC) & ~(0xf << 4)) & ~(0xf << 2)) | (0x3 << 2)) & ~(0x1 << 1), sysreg_base_regs + 0xC);

	/* pcs_g_rst */
	writel(0x1, elbi_bsae_regs + 0x288);
	udelay(10);
	writel(0x0, elbi_bsae_regs + 0x288);
	udelay(10);
	writel(0x1, elbi_bsae_regs + 0x288);
	udelay(10);

	/* PHY Common block Setting */
	for (i = 0; i < 26; i++)
		writel(cmn_config_val[i], phy_base_regs + (i * 4));

	/* PHY Tranceiver/Receiver block Setting */
	for (i = 0; i < 41; i++)
		writel(trsv_config_val[i], phy_base_regs + ((0x30 + i) * 4));

	/* tx amplitude control */
	writel(0x14, phy_base_regs + (0x5C * 4));

	/* tx latency */
	writel(0x70, phy_pcs_base_regs + 0xF8);

	/* PRGM_TIMEOUT_L1SS_VAL Setting */
	writel(readl(phy_pcs_base_regs + 0xC) | (0x1 << 4), phy_pcs_base_regs + 0xC);

	/* PCIE_MAC CMN_RST */
	writel(0x1, elbi_bsae_regs + 0x290);
	udelay(10);
	writel(0x0, elbi_bsae_regs + 0x290);
	udelay(10);
	writel(0x1, elbi_bsae_regs + 0x290);
	udelay(10);

	/* PCIE_PHY PCS&PMA(CMN)_RST */
	writel(0x1, elbi_bsae_regs + 0x28C);
	udelay(10);
	writel(0x0, elbi_bsae_regs + 0x28C);
	udelay(10);
	writel(0x1, elbi_bsae_regs + 0x28C);
	udelay(10);
}
