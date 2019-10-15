/*
 * Samsung EXYNOS SoC series USB DRD PHY driver
 *
 * Phy provider for USB 3.0 DRD controller on Exynos SoC series
 *
 * Copyright (C) 2014 Samsung Electronics Co., Ltd.
 * Author: Vivek Gautam <gautam.vivek@samsung.com>
 *	   Minho Lee <minho55.lee@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/mfd/syscon.h>
#include <linux/mfd/syscon/exynos5-pmu.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/usb/samsung_usb.h>
#include <linux/usb/otg.h>

#include "phy-exynos-usbdrd.h"

static const char *exynos8890_usbdrd_clk_names[] = {"aclk", "sclk", "phyclock",
						"pipe_pclk", NULL};

static const char *exynos8890_usbhost_clk_names[] = {"aclk", "sclk", "phyclock",
						"phy_ref", NULL};
static const char *exynos7870_usbdrd_clk_names[] = {"usb_pll", "usbdrd20", NULL};
static const char *exynos7870_usbphy_clk_names[] = {"phyumux", NULL};

static int exynos_usbdrd_clk_prepare(struct exynos_usbdrd_phy *phy_drd)
{
	int i;
	int ret;

	for (i = 0; phy_drd->clocks[i] != NULL; i++) {
		ret = clk_prepare(phy_drd->clocks[i]);
		if (ret)
			goto err;
	}
	for (i = 0; phy_drd->phy_clocks[i] != NULL; i++) {
		ret = clk_prepare(phy_drd->phy_clocks[i]);
		if (ret)
			goto err1;
	}
	return 0;
err:
	for (i = i - 1; i >= 0; i--)
		clk_unprepare(phy_drd->clocks[i]);
	return ret;
err1:
	for (i = i - 1; i >= 0; i--)
		clk_unprepare(phy_drd->phy_clocks[i]);
	return ret;
}

static int exynos_usbdrd_clk_enable(struct exynos_usbdrd_phy *phy_drd,
					bool umux)
{
	int i;
	int ret;

	if (!umux) {
		for (i = 0; phy_drd->clocks[i] != NULL; i++) {
				ret = clk_enable(phy_drd->clocks[i]);
				if (ret)
					goto err;
		}
	} else {
		for (i = 0; phy_drd->phy_clocks[i] != NULL; i++) {
				ret = clk_enable(phy_drd->phy_clocks[i]);
				if (ret)
					goto err1;
			}
	}
	return 0;
err:
	for (i = i - 1; i >= 0; i--)
		clk_disable(phy_drd->clocks[i]);
	return ret;
err1:
	for (i = i -1; i >= 0; i--)
		clk_disable(phy_drd->phy_clocks[i]);
	return ret;
}

static void exynos_usbdrd_clk_unprepare(struct exynos_usbdrd_phy *phy_drd)
{
	int i;

	for (i = 0; phy_drd->clocks[i] != NULL; i++)
		clk_unprepare(phy_drd->clocks[i]);
	for (i = 0; phy_drd->phy_clocks[i] != NULL; i++)
		clk_unprepare(phy_drd->phy_clocks[i]);
}

static void exynos_usbdrd_clk_disable(struct exynos_usbdrd_phy *phy_drd, bool umux)
{
	int i;

	if (!umux) {
		for (i = 0; phy_drd->clocks[i] != NULL; i++) {
				clk_disable(phy_drd->clocks[i]);
		}
	} else {
		for (i = 0; phy_drd->phy_clocks[i] != NULL; i++) {
				clk_disable(phy_drd->phy_clocks[i]);
		}
	}
}

static int exynos_usbdrd_clk_get(struct exynos_usbdrd_phy *phy_drd)
{
	const char	**clk_ids, **phy_clk_ids;
	struct clk	*clk;
	int		clk_count;
	int		phy_clk_count = 0;
	int		i;

	switch (phy_drd->drv_data->cpu_type) {
	case TYPE_EXYNOS8890:
		if (phy_drd->drv_data->ip_type == TYPE_USB3DRD) {
			clk_ids = exynos8890_usbdrd_clk_names;
			clk_count =
				(int)ARRAY_SIZE(exynos8890_usbdrd_clk_names);
		} else {
			clk_ids = exynos8890_usbhost_clk_names;
			clk_count =
				(int)ARRAY_SIZE(exynos8890_usbhost_clk_names);
		}
		break;
	case TYPE_EXYNOS7870:
		clk_ids = exynos7870_usbdrd_clk_names;
		clk_count =
			(int)ARRAY_SIZE(exynos7870_usbdrd_clk_names);
		phy_clk_ids = exynos7870_usbphy_clk_names;
		phy_clk_count =
			(int)ARRAY_SIZE(exynos7870_usbphy_clk_names);
		break;
	default:
		dev_err(phy_drd->dev, "couldn't get clock: unknown cpu type\n");
		return -EINVAL;
	}

	phy_drd->clocks = (struct clk **) devm_kmalloc(phy_drd->dev,
			clk_count * sizeof(struct clk *), GFP_KERNEL);
	if (!phy_drd->clocks) {
		dev_err(phy_drd->dev, "failed to alloc : drd clocks\n");
		return -ENOMEM;
	}

	for (i = 0; clk_ids[i] != NULL; i++) {
		clk = devm_clk_get(phy_drd->dev, clk_ids[i]);
		if (IS_ERR_OR_NULL(clk)) {
			dev_err(phy_drd->dev,
				"couldn't get %s clock\n", clk_ids[i]);
			return -EINVAL;
		}
		phy_drd->clocks[i] = clk;
	}
	phy_drd->clocks[i] = NULL;

	if (!phy_clk_count)
		return 0;

	phy_drd->phy_clocks = (struct clk **) devm_kmalloc(phy_drd->dev,
			phy_clk_count * sizeof(struct clk *), GFP_KERNEL);
	if (!phy_drd->phy_clocks) {
		dev_err(phy_drd->dev, "failed to alloc : phy clocks\n");
		return -ENOMEM;
	}

	for (i = 0; phy_clk_ids[i] != NULL; i++) {
		clk = devm_clk_get(phy_drd->dev, phy_clk_ids[i]);
		if (IS_ERR_OR_NULL(clk)) {
			dev_err(phy_drd->dev,
				"couldn't get %s clock\n", phy_clk_ids[i]);
			return -EINVAL;
		}
		phy_drd->phy_clocks[i] = clk;
	}
	phy_drd->phy_clocks[i] = NULL;

	return 0;
}

static inline
struct exynos_usbdrd_phy *to_usbdrd_phy(struct phy_usb_instance *inst)
{
	return container_of((inst), struct exynos_usbdrd_phy,
			    phys[(inst)->index]);
}

/*
 * exynos_rate_to_clk() converts the supplied clock rate to the value that
 * can be written to the phy register.
 */
static unsigned int exynos_rate_to_clk(struct exynos_usbdrd_phy *phy_drd)
{
	const char **clk_ids;
	int ret, i;

	switch (phy_drd->drv_data->cpu_type) {
	case TYPE_EXYNOS7870:
		clk_ids = exynos7870_usbdrd_clk_names;
		for (i = 0; clk_ids[i] != NULL; i++) {
			if (!strcmp("usb_pll", clk_ids[i])) {
				phy_drd->ref_clk = phy_drd->clocks[i];
				break;
			}
		}
		break;
	case TYPE_EXYNOS8890:
		if (phy_drd->drv_data->ip_type == TYPE_USB2HOST) {
			clk_ids = exynos8890_usbhost_clk_names;
			for (i = 0; clk_ids[i] != NULL; i++) {
				if (!strcmp("phy_ref", clk_ids[i])) {
					phy_drd->ref_clk = phy_drd->clocks[i];
					break;
				}
			}
			phy_drd->extrefclk = EXYNOS_FSEL_12MHZ;
			return 0;
		}
		phy_drd->ref_clk = devm_clk_get(phy_drd->dev, "ext_xtal");
		break;
	default:
		phy_drd->ref_clk = devm_clk_get(phy_drd->dev, "ext_xtal");
		break;
	}
	if (IS_ERR_OR_NULL(phy_drd->ref_clk)) {
		dev_err(phy_drd->dev, "%s failed to get ref_clk", __func__);
		return 0;
	}
	ret = clk_prepare_enable(phy_drd->ref_clk);
	if (ret) {
		dev_err(phy_drd->dev, "%s failed to enable ref_clk", __func__);
		return 0;
	}

	/* EXYNOS_FSEL_MASK */
	switch (clk_get_rate(phy_drd->ref_clk)) {
	case 9600 * KHZ:
		phy_drd->extrefclk = EXYNOS_FSEL_9MHZ6;
		break;
	case 10 * MHZ:
		phy_drd->extrefclk = EXYNOS_FSEL_10MHZ;
		break;
	case 12 * MHZ:
		phy_drd->extrefclk = EXYNOS_FSEL_12MHZ;
		break;
	case 19200 * KHZ:
		phy_drd->extrefclk = EXYNOS_FSEL_19MHZ2;
		break;
	case 20 * MHZ:
		phy_drd->extrefclk = EXYNOS_FSEL_20MHZ;
		break;
	case 24 * MHZ:
		phy_drd->extrefclk = EXYNOS_FSEL_24MHZ;
		break;
	case 26 * MHZ:
		phy_drd->extrefclk = EXYNOS_FSEL_26MHZ;
		break;
	case 50 * MHZ:
		phy_drd->extrefclk = EXYNOS_FSEL_50MHZ;
		break;
	default:
		phy_drd->extrefclk = 0;
		clk_disable_unprepare(phy_drd->ref_clk);
		return -EINVAL;
	}

	clk_disable_unprepare(phy_drd->ref_clk);

	return 0;
}

static void exynos_usbdrd_pipe3_phy_isol(struct phy_usb_instance *inst,
					unsigned int on, unsigned int mask)
{
	unsigned int val;

	if (!inst->reg_pmu)
		return;

	val = on ? 0 : mask;

	regmap_update_bits(inst->reg_pmu, inst->pmu_offset,
			   mask, val);
}

static void exynos_usbdrd_utmi_phy_isol(struct phy_usb_instance *inst,
					unsigned int on, unsigned int mask)
{
	return;
}

/*
 * Sets the pipe3 phy's clk as EXTREFCLK (XXTI) which is internal clock
 * from clock core. Further sets multiplier values and spread spectrum
 * clock settings for SuperSpeed operations.
 */
static unsigned int
exynos_usbdrd_pipe3_set_refclk(struct phy_usb_instance *inst)
{
	static u32 reg;
	struct exynos_usbdrd_phy *phy_drd = to_usbdrd_phy(inst);

	/* restore any previous reference clock settings */
	reg = readl(phy_drd->reg_phy + EXYNOS_DRD_PHYCLKRST);

	/* Use EXTREFCLK as ref clock */
	reg &= ~PHYCLKRST_REFCLKSEL_MASK;
	reg |=	PHYCLKRST_REFCLKSEL_EXT_REFCLK;

	/* FSEL settings corresponding to reference clock */
	reg &= ~PHYCLKRST_FSEL_PIPE_MASK |
		PHYCLKRST_MPLL_MULTIPLIER_MASK |
		PHYCLKRST_SSC_REFCLKSEL_MASK;
	switch (phy_drd->extrefclk) {
	case EXYNOS_FSEL_50MHZ:
		reg |= (PHYCLKRST_MPLL_MULTIPLIER_50M_REF |
			PHYCLKRST_SSC_REFCLKSEL(0x00));
		break;
	case EXYNOS_FSEL_24MHZ:
		reg |= (PHYCLKRST_MPLL_MULTIPLIER_24MHZ_REF |
			PHYCLKRST_SSC_REFCLKSEL(0x88));
		break;
	case EXYNOS_FSEL_20MHZ:
		reg |= (PHYCLKRST_MPLL_MULTIPLIER_20MHZ_REF |
			PHYCLKRST_SSC_REFCLKSEL(0x00));
		break;
	case EXYNOS_FSEL_19MHZ2:
		reg |= (PHYCLKRST_MPLL_MULTIPLIER_19200KHZ_REF |
			PHYCLKRST_SSC_REFCLKSEL(0x88));
		break;
	default:
		dev_dbg(phy_drd->dev, "unsupported ref clk\n");
		break;
	}

	return reg;
}

/*
 * Sets the utmi phy's clk as EXTREFCLK (XXTI) which is internal clock
 * from clock core. Further sets the FSEL values for HighSpeed operations.
 */
static unsigned int
exynos_usbdrd_utmi_set_refclk(struct phy_usb_instance *inst)
{
	static u32 reg;
	struct exynos_usbdrd_phy *phy_drd = to_usbdrd_phy(inst);

	/* restore any previous reference clock settings */
	reg = readl(phy_drd->reg_phy + EXYNOS_DRD_PHYCLKRST);

	reg &= ~PHYCLKRST_REFCLKSEL_MASK;
	reg |=	PHYCLKRST_REFCLKSEL_EXT_REFCLK;

	reg &= ~PHYCLKRST_FSEL_UTMI_MASK |
		PHYCLKRST_MPLL_MULTIPLIER_MASK |
		PHYCLKRST_SSC_REFCLKSEL_MASK;
	reg |= PHYCLKRST_FSEL(phy_drd->extrefclk);

	return reg;
}

/*
 * Sets the default PHY tuning values for high-speed connection.
 */
static void exynos_usbdrd_fill_hstune(struct exynos_usbdrd_phy *phy_drd,
				struct exynos_usbphy_hs_tune *hs_tune)
{
	switch (phy_drd->drv_data->cpu_type) {
	case TYPE_EXYNOS8890:
		if (phy_drd->drv_data->ip_type == TYPE_USB3DRD) {
			hs_tune->tx_vref	 = 0x3;
			hs_tune->tx_pre_emp	 = 0x2;
			hs_tune->tx_pre_emp_plus = 0x0;
			hs_tune->tx_res		 = 0x2;
			hs_tune->tx_rise	 = 0x3;
			hs_tune->tx_hsxv	 = 0x0;
			hs_tune->tx_fsls	 = 0x3;
			hs_tune->rx_sqrx	 = 0x7;
			hs_tune->compdis	 = 0x0;
			hs_tune->otg		 = 0x4;
			hs_tune->enalbe_user_imp = false;
			hs_tune->utim_clk	 = USBPHY_UTMI_PHYCLOCK;
		}
		break;
	case TYPE_EXYNOS7870:
			hs_tune->tx_vref	 = 0x3;
			hs_tune->tx_pre_emp	 = 0x0;
			hs_tune->tx_pre_emp_plus = 0x0;
			hs_tune->tx_res		 = 0x2;
			hs_tune->tx_rise	 = 0x1;
			hs_tune->tx_hsxv	 = 0x0;
			hs_tune->tx_fsls	 = 0x3;
			hs_tune->rx_sqrx	 = 0x5;
			hs_tune->compdis	 = 0x3;
			hs_tune->otg		 = 0x2;
			hs_tune->enalbe_user_imp = false;
			hs_tune->utim_clk	 = USBPHY_UTMI_PHYCLOCK;
		break;
	default:
		break;
	}
}
static int exynos_usbdrd_set_hstune_from_dt(struct exynos_usbdrd_phy *phy_drd,
				enum exynos_usbphy_mode phy_mode)
{
	struct device *dev = phy_drd->dev;
	struct device_node *node = dev->of_node;
	struct exynos_usbphy_hs_tune *hs_tune = phy_drd->usbphy_info.hs_tune;
	u8 usb_phy_tune_values[10] =  {0xff}; 
	int ret, i;
	if (phy_mode == USBPHY_MODE_DEV) {
		ret = of_property_read_u8_array(node, "device_usbphy_hstune_parameter",
					 &usb_phy_tune_values[0], 10);
	} else {
		ret = of_property_read_u8_array(node, "host_usbphy_hstune_parameter",
					 &usb_phy_tune_values[0], 10);
		
	}
	if (ret) {
		pr_err("usb:%s dt devnode is not defined \n ",__func__);
		return ret;
	}
	pr_err("usb: %s Updating tune values from dt for phy_mode %d\n", __func__, phy_mode);
	/*tx_vref,tx_pre_emp,tx_pre_emp_plus,tx_res,tx_rise,tx_hsxv,tx_fsls,rx_sqrx,compdis,otg*/
	/* 0        1          2              3       4       5       6       7       8      9*/
	for (i = 0; i < 10; i++) {
		if (usb_phy_tune_values[i] != 0xFF) {
			switch(i) {
				case 0:
				 hs_tune->tx_vref = usb_phy_tune_values[i];
				break;
				case 1:
				 hs_tune->tx_pre_emp = usb_phy_tune_values[i];
				break;
				case 2:
				 hs_tune->tx_pre_emp_plus = usb_phy_tune_values[i];
				break;
				case 3:
				 hs_tune->tx_res = usb_phy_tune_values[i];
				break;
				case 4:
				 hs_tune->tx_rise = usb_phy_tune_values[i];
				break;
				case 5:
				 hs_tune->tx_hsxv = usb_phy_tune_values[i];
				break;
				case 6:
				 hs_tune->tx_fsls = usb_phy_tune_values[i];
				break;
				case 7:
				 hs_tune->rx_sqrx = usb_phy_tune_values[i];
				break;
				case 8:
				 hs_tune->compdis = usb_phy_tune_values[i];
				break;
				case 9:
				 hs_tune->otg = usb_phy_tune_values[i];
				break;
				default:
				break;	
			} 
		}
	}
	return ret;
}

static void exynos_usbdrd_set_hstune(struct exynos_usbdrd_phy *phy_drd,
				enum exynos_usbphy_mode phy_mode)
{
	struct exynos_usbphy_hs_tune *hs_tune = phy_drd->usbphy_info.hs_tune;

	if (phy_mode == USBPHY_MODE_DEV) {
		switch (phy_drd->drv_data->cpu_type) {
		case TYPE_EXYNOS7870:
			hs_tune->tx_vref	= 0xE;
			hs_tune->tx_res		= 0x3;
			hs_tune->rx_sqrx    = 0x6;
			break;
		default:
			break;
		}
	} else { /* USBPHY_MODE_HOST */
		switch (phy_drd->drv_data->cpu_type) {
		case TYPE_EXYNOS8890:
			if (phy_drd->drv_data->ip_type == TYPE_USB3DRD) {
				hs_tune->tx_vref	= 0x1;
				hs_tune->tx_pre_emp	= 0x0;
				hs_tune->compdis	= 0x7;
			}
			break;
		case TYPE_EXYNOS7870:
			hs_tune->tx_vref	= 0x5;
			hs_tune->tx_pre_emp	= 0x0;
			hs_tune->compdis	= 0x7;
			break;
		default:
			break;
		}

	}
	exynos_usbdrd_set_hstune_from_dt(phy_drd,phy_mode);
}

/*
 * Sets the default PHY tuning values for super-speed connection.
 */
static void exynos_usbdrd_fill_sstune(struct exynos_usbdrd_phy *phy_drd,
				struct exynos_usbphy_ss_tune *ss_tune)
{
	switch (phy_drd->drv_data->cpu_type) {
	case TYPE_EXYNOS8890:
		if (phy_drd->drv_data->ip_type == TYPE_USB3DRD) {
			ss_tune->tx_boost_level	= 0x4;
			ss_tune->tx_swing_level	= 0x1;
			ss_tune->tx_swing_full	= 0x7F;
			ss_tune->tx_swing_low	= 0x7F;
			ss_tune->tx_deemphasis_mode	= 0x1;
			ss_tune->tx_deemphasis_3p5db	= 0x18;
			ss_tune->tx_deemphasis_6db	= 0x18;
			ss_tune->enable_ssc	= 0x1; /* TRUE */
			ss_tune->ssc_range	= 0x0;
			ss_tune->los_bias	= 0x5;
			ss_tune->los_mask_val	= 0x104;
			ss_tune->enable_fixed_rxeq_mode	= 0x0;
			ss_tune->fix_rxeq_value	= 0x4;
		}
		break;
	default:
		break;
	}
}

static int exynos_usbdrd_get_phyinfo(struct exynos_usbdrd_phy *phy_drd)
{
	struct device *dev = phy_drd->dev;
	struct device_node *node = dev->of_node;

	switch (phy_drd->drv_data->cpu_type) {
	case TYPE_EXYNOS8890:
		if (phy_drd->drv_data->ip_type == TYPE_USB3DRD) {
			phy_drd->usbphy_info.version = EXYNOS_USBCON_VER_01_0_1;
			phy_drd->usbphy_info.refsel =
						USBPHY_REFSEL_DIFF_INTERNAL;
			phy_drd->usbphy_info.use_io_for_ovc = true;
			phy_drd->usbphy_info.common_block_enable = false;
		} else {
			phy_drd->usbphy_info.version = EXYNOS_USBCON_VER_02_1_1;
			phy_drd->usbphy_info.refsel =
						USBPHY_REFCLK_EXT_12MHZ;
			phy_drd->usbphy_info.use_io_for_ovc = false;
			phy_drd->usbphy_info.common_block_enable = false;
		}
		break;
	case TYPE_EXYNOS7870:
		phy_drd->usbphy_info.version = EXYNOS_USBCON_VER_02_1_0,
		phy_drd->usbphy_info.refsel = USBPHY_REFSEL_CLKCORE,
		phy_drd->usbphy_info.use_io_for_ovc = false;
		break;
	default:
		dev_err(phy_drd->dev, "%s: unknown cpu type\n", __func__);
		return -EINVAL;
	}

	phy_drd->usbphy_info.refclk = phy_drd->extrefclk;
	phy_drd->usbphy_info.regs_base = phy_drd->reg_phy;
	phy_drd->usbphy_info.not_used_vbus_pad = of_property_read_bool(node,
							"is_not_vbus_pad");
	phy_drd->use_additional_tuning = of_property_read_bool(node,
						"use_additional_tuning");

	if (phy_drd->drv_data->cpu_type == TYPE_EXYNOS8890 &&
		phy_drd->drv_data->ip_type == TYPE_USB2HOST) {
		phy_drd->usbphy_info.ss_tune = NULL;
		phy_drd->usbphy_info.hs_tune = NULL;
		goto done;
	}

	phy_drd->usbphy_info.hs_tune = devm_kmalloc(phy_drd->dev,
			sizeof(struct exynos_usbphy_hs_tune), GFP_KERNEL);
	if (!phy_drd->usbphy_info.hs_tune) {
		dev_err(phy_drd->dev, "%s: failed to alloc for hs tune\n",
								__func__);
		return -ENOMEM;
	}

	phy_drd->usbphy_info.ss_tune = devm_kmalloc(phy_drd->dev,
			sizeof(struct exynos_usbphy_ss_tune), GFP_KERNEL);
	if (!phy_drd->usbphy_info.ss_tune) {
		dev_err(phy_drd->dev, "%s: failed to alloc for ss tune\n",
				__func__);

		return -ENOMEM;
	}

	exynos_usbdrd_fill_hstune(phy_drd, phy_drd->usbphy_info.hs_tune);
	exynos_usbdrd_fill_sstune(phy_drd, phy_drd->usbphy_info.ss_tune);

done:
	dev_info(phy_drd->dev, "usbphy info: version:0x%x, refclk:0x%x\n",
		phy_drd->usbphy_info.version, phy_drd->usbphy_info.refclk);

	return 0;
}

static void exynos_usbdrd_pipe3_init(struct exynos_usbdrd_phy *phy_drd)
{
	int ret;

	ret = exynos_usbdrd_clk_enable(phy_drd, false);
	if (ret) {
		dev_err(phy_drd->dev, "%s: Failed to enable clk\n", __func__);
		return;
	}

	samsung_exynos_cal_usb3phy_enable(&phy_drd->usbphy_info);

	if (phy_drd->drv_data->phy_usermux) {
		/* USB User MUX enable */
		ret = exynos_usbdrd_clk_enable(phy_drd, true);
		if (ret) {
			dev_err(phy_drd->dev, "%s: Failed to enable clk\n", __func__);
			return;
		}
	}
}

static void exynos_usbdrd_utmi_init(struct exynos_usbdrd_phy *phy_drd)
{
	return;
}

static int exynos_usbdrd_phy_init(struct phy *phy)
{
	struct phy_usb_instance *inst = phy_get_drvdata(phy);
	struct exynos_usbdrd_phy *phy_drd = to_usbdrd_phy(inst);

	/* UTMI or PIPE3 specific init */
	inst->phy_cfg->phy_init(phy_drd);

	return 0;
}

static void __exynos_usbdrd_phy_shutdown(struct exynos_usbdrd_phy *phy_drd)
{
	samsung_exynos_cal_usb3phy_disable(&phy_drd->usbphy_info);
}

static void exynos_usbdrd_pipe3_exit(struct exynos_usbdrd_phy *phy_drd)
{
	if (phy_drd->drv_data->phy_usermux) {
		/*USB User MUX disable */
		exynos_usbdrd_clk_disable(phy_drd, true);
	}
	__exynos_usbdrd_phy_shutdown(phy_drd);

	exynos_usbdrd_clk_disable(phy_drd, false);
}

static void exynos_usbdrd_utmi_exit(struct exynos_usbdrd_phy *phy_drd)
{
	return;
}

static int exynos_usbdrd_phy_exit(struct phy *phy)
{
	struct phy_usb_instance *inst = phy_get_drvdata(phy);
	struct exynos_usbdrd_phy *phy_drd = to_usbdrd_phy(inst);

	/* UTMI or PIPE3 specific exit */
	inst->phy_cfg->phy_exit(phy_drd);

	return 0;
}

static void exynos_usbdrd_pipe3_tune(struct exynos_usbdrd_phy *phy_drd,
							int phy_state)
{
	exynos_usbdrd_fill_hstune(phy_drd, phy_drd->usbphy_info.hs_tune);

	if (phy_state >= OTG_STATE_A_IDLE) {
		/* for host mode */
		if (phy_drd->use_additional_tuning)
			exynos_usbdrd_set_hstune(phy_drd, USBPHY_MODE_HOST);

		samsung_exynos_cal_usb3phy_tune_host(&phy_drd->usbphy_info);
	} else {
		/* for device mode */
		if (phy_drd->use_additional_tuning)
			exynos_usbdrd_set_hstune(phy_drd, USBPHY_MODE_DEV);

		samsung_exynos_cal_usb3phy_tune_dev(&phy_drd->usbphy_info);
	}
}

static void exynos_usbdrd_utmi_tune(struct exynos_usbdrd_phy *phy_drd,
							int phy_state)
{
	exynos_usbdrd_fill_hstune(phy_drd, phy_drd->usbphy_info.hs_tune);

	if (phy_state >= OTG_STATE_A_IDLE) {
		/* for host mode */
		if (phy_drd->use_additional_tuning)
			exynos_usbdrd_set_hstune(phy_drd, USBPHY_MODE_HOST);

		samsung_exynos_cal_usb3phy_tune_host(&phy_drd->usbphy_info);
	} else {
		/* for device mode */
		if (phy_drd->use_additional_tuning)
			exynos_usbdrd_set_hstune(phy_drd, USBPHY_MODE_DEV);

		samsung_exynos_cal_usb3phy_tune_dev(&phy_drd->usbphy_info);
	}
}

static int exynos_usbdrd_phy_tune(struct phy *phy, int phy_state)
{
	struct phy_usb_instance *inst = phy_get_drvdata(phy);
	struct exynos_usbdrd_phy *phy_drd = to_usbdrd_phy(inst);

	inst->phy_cfg->phy_tune(phy_drd, phy_state);

	return 0;
}

static void exynos_usbdrd_pipe3_set(struct exynos_usbdrd_phy *phy_drd,
						int option, void *info)
{
	switch (option) {
	case SET_DPPULLUP_ENABLE:
		samsung_exynos_cal_usb3phy_enable_dp_pullup(
					&phy_drd->usbphy_info);
		break;
	case SET_DPPULLUP_DISABLE:
		samsung_exynos_cal_usb3phy_disable_dp_pullup(
					&phy_drd->usbphy_info);
		break;
	case SET_DPDM_PULLDOWN:
		samsung_exynos_cal_usb3phy_config_host_mode(
					&phy_drd->usbphy_info);
	default:
		break;
	}
}

static void exynos_usbdrd_utmi_set(struct exynos_usbdrd_phy *phy_drd,
						int option, void *info)
{
	return;
}

static int exynos_usbdrd_phy_set(struct phy *phy, int option, void *info)
{
	struct phy_usb_instance *inst = phy_get_drvdata(phy);
	struct exynos_usbdrd_phy *phy_drd = to_usbdrd_phy(inst);

	inst->phy_cfg->phy_set(phy_drd, option, info);

	return 0;
}

static int exynos_usbdrd_phy_power_on(struct phy *phy)
{
	int ret;
	struct phy_usb_instance *inst = phy_get_drvdata(phy);
	struct exynos_usbdrd_phy *phy_drd = to_usbdrd_phy(inst);

	dev_dbg(phy_drd->dev, "Request to power_on usbdrd_phy phy\n");

	/* Enable VBUS supply */
	if (phy_drd->vbus) {
		ret = regulator_enable(phy_drd->vbus);
		if (ret) {
			dev_err(phy_drd->dev, "Failed to enable VBUS supply\n");
			return ret;
		}
	}

	/* Power-on PHY*/
	switch (phy_drd->drv_data->cpu_type) {
	case TYPE_EXYNOS8890:
		if (phy_drd->drv_data->ip_type == TYPE_USB3DRD)
			inst->phy_cfg->phy_isol(inst, 0, EXYNOS_USB3PHY_ENABLE);
		else
			inst->phy_cfg->phy_isol(inst, 0, EXYNOS_USB2PHY_ENABLE);
		break;
	case TYPE_EXYNOS7870:
		inst->phy_cfg->phy_isol(inst, 0, EXYNOS_USB2PHY_ENABLE);
		break;
	default:
		inst->phy_cfg->phy_isol(inst, 0, EXYNOS5_PHY_ENABLE);
		break;
	}

	return 0;
}

static int exynos_usbdrd_phy_power_off(struct phy *phy)
{
	struct phy_usb_instance *inst = phy_get_drvdata(phy);
	struct exynos_usbdrd_phy *phy_drd = to_usbdrd_phy(inst);

	dev_dbg(phy_drd->dev, "Request to power_off usbdrd_phy phy\n");

	/* Power-off the PHY */
	switch (phy_drd->drv_data->cpu_type) {
	case TYPE_EXYNOS8890:
		if (phy_drd->drv_data->ip_type == TYPE_USB3DRD)
			inst->phy_cfg->phy_isol(inst, 1, EXYNOS_USB3PHY_ENABLE);
		else
			inst->phy_cfg->phy_isol(inst, 1, EXYNOS_USB2PHY_ENABLE);
		break;
	case TYPE_EXYNOS7870:
		inst->phy_cfg->phy_isol(inst, 1, EXYNOS_USB2PHY_ENABLE);
		break;
	default:
		inst->phy_cfg->phy_isol(inst, 1, EXYNOS5_PHY_ENABLE);
		break;
	}

	/* Disable VBUS supply */
	if (phy_drd->vbus)
		regulator_disable(phy_drd->vbus);

	return 0;
}

static struct phy *exynos_usbdrd_phy_xlate(struct device *dev,
					struct of_phandle_args *args)
{
	struct exynos_usbdrd_phy *phy_drd = dev_get_drvdata(dev);

	if (WARN_ON(args->args[0] > EXYNOS_DRDPHYS_NUM))
		return ERR_PTR(-ENODEV);

	return phy_drd->phys[args->args[0]].phy;
}

static struct phy_ops exynos_usbdrd_phy_ops = {
	.init		= exynos_usbdrd_phy_init,
	.exit		= exynos_usbdrd_phy_exit,
	.tune		= exynos_usbdrd_phy_tune,
	.set		= exynos_usbdrd_phy_set,
	.power_on	= exynos_usbdrd_phy_power_on,
	.power_off	= exynos_usbdrd_phy_power_off,
	.owner		= THIS_MODULE,
};

static const struct exynos_usbdrd_phy_config phy_cfg_exynos[] = {
	{
		.id		= EXYNOS_DRDPHY_UTMI,
		.phy_isol	= exynos_usbdrd_utmi_phy_isol,
		.phy_init	= exynos_usbdrd_utmi_init,
		.phy_exit	= exynos_usbdrd_utmi_exit,
		.phy_tune	= exynos_usbdrd_utmi_tune,
		.phy_set	= exynos_usbdrd_utmi_set,
		.set_refclk	= exynos_usbdrd_utmi_set_refclk,
	},
	{
		.id		= EXYNOS_DRDPHY_PIPE3,
		.phy_isol	= exynos_usbdrd_pipe3_phy_isol,
		.phy_init	= exynos_usbdrd_pipe3_init,
		.phy_exit	= exynos_usbdrd_pipe3_exit,
		.phy_tune	= exynos_usbdrd_pipe3_tune,
		.phy_set	= exynos_usbdrd_pipe3_set,
		.set_refclk	= exynos_usbdrd_pipe3_set_refclk,
	},
};

static const struct exynos_usbdrd_phy_drvdata exynos8890_usbdrd_phy = {
	.phy_cfg		= phy_cfg_exynos,
	.pmu_offset_usbdrd0_phy	= EXYNOS_USBDEV_PHY_CONTROL,
	.cpu_type		= TYPE_EXYNOS8890,
	.ip_type		= TYPE_USB3DRD,
	.phy_usermux		= false,
};

static const struct exynos_usbdrd_phy_drvdata exynos8890_usbhost_phy = {
	.phy_cfg		= phy_cfg_exynos,
	.pmu_offset_usbdrd0_phy	= EXYNOS_USBDEV_PHY_CONTROL,
	.cpu_type		= TYPE_EXYNOS8890,
	.ip_type		= TYPE_USB2HOST,
	.phy_usermux		= false,
};

static const struct exynos_usbdrd_phy_drvdata exynos7870_usbdrd_phy = {
	.phy_cfg		= phy_cfg_exynos,
	.pmu_offset_usbdrd0_phy	= EXYNOS_USBDEV_PHY_CONTROL,
	.cpu_type		= TYPE_EXYNOS7870,
	.phy_usermux		= true,
};

static const struct of_device_id exynos_usbdrd_phy_of_match[] = {
	{
		.compatible = "samsung,exynos8890-usbdrd-phy",
		.data = &exynos8890_usbdrd_phy
	}, {
		.compatible = "samsung,exynos8890-usbhost-phy",
		.data = &exynos8890_usbhost_phy
	}, {
		.compatible = "samsung,exynos7870-usbdrd-phy",
		.data = &exynos7870_usbdrd_phy
	},
	{ },
};
MODULE_DEVICE_TABLE(of, exynos5_usbdrd_phy_of_match);

static int exynos_usbdrd_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct exynos_usbdrd_phy *phy_drd;
	struct phy_provider *phy_provider;
	struct resource *res;
	const struct of_device_id *match;
	const struct exynos_usbdrd_phy_drvdata *drv_data;
	struct regmap *reg_pmu;
	u32 pmu_offset;
	int i, ret;
	int channel;

	phy_drd = devm_kzalloc(dev, sizeof(*phy_drd), GFP_KERNEL);
	if (!phy_drd)
		return -ENOMEM;

	dev_set_drvdata(dev, phy_drd);
	phy_drd->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	phy_drd->reg_phy = devm_ioremap_resource(dev, res);
	if (IS_ERR(phy_drd->reg_phy))
		return PTR_ERR(phy_drd->reg_phy);

	match = of_match_node(exynos_usbdrd_phy_of_match, pdev->dev.of_node);

	drv_data = match->data;
	phy_drd->drv_data = drv_data;

	ret = exynos_usbdrd_clk_get(phy_drd);
	if (ret) {
		dev_err(dev, "%s: Failed to get clocks\n", __func__);
		return ret;
	}

	ret = exynos_usbdrd_clk_prepare(phy_drd);
	if (ret) {
		dev_err(dev, "%s: Failed to prepare clocks\n", __func__);
		return ret;
	}

	ret = exynos_rate_to_clk(phy_drd);
	if (ret) {
		dev_err(phy_drd->dev, "%s: Not supported ref clock\n",
				__func__);
		goto err1;
	}

	reg_pmu = syscon_regmap_lookup_by_phandle(dev->of_node,
						   "samsung,pmu-syscon");
	if (IS_ERR(reg_pmu)) {
		dev_err(dev, "Failed to lookup PMU regmap\n");
		goto err1;
	}

	channel = of_alias_get_id(node, "usbdrdphy");
	if (channel < 0)
		dev_dbg(dev, "Not a multi-controller usbdrd phy\n");

	switch (channel) {
	case 1:
		pmu_offset = phy_drd->drv_data->pmu_offset_usbdrd1_phy;
		break;
	case 0:
	default:
		pmu_offset = phy_drd->drv_data->pmu_offset_usbdrd0_phy;
		break;
	}

	dev_vdbg(dev, "Creating usbdrd_phy phy\n");

	ret = exynos_usbdrd_get_phyinfo(phy_drd);
	if (ret)
		goto err1;

	for (i = 0; i < EXYNOS_DRDPHYS_NUM; i++) {
		struct phy *phy = devm_phy_create(dev, NULL,
						  &exynos_usbdrd_phy_ops,
						  NULL);
		if (IS_ERR(phy)) {
			dev_err(dev, "Failed to create usbdrd_phy phy\n");
			goto err1;
		}

		phy_drd->phys[i].phy = phy;
		phy_drd->phys[i].index = i;
		phy_drd->phys[i].reg_pmu = reg_pmu;
		phy_drd->phys[i].pmu_offset = pmu_offset;
		phy_drd->phys[i].phy_cfg = &drv_data->phy_cfg[i];
		phy_set_drvdata(phy, &phy_drd->phys[i]);
	}

	phy_provider = devm_of_phy_provider_register(dev,
						     exynos_usbdrd_phy_xlate);
	if (IS_ERR(phy_provider)) {
		dev_err(phy_drd->dev, "Failed to register phy provider\n");
		goto err1;
	}

	return 0;
err1:
	exynos_usbdrd_clk_unprepare(phy_drd);

	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int exynos_usbdrd_phy_resume(struct device *dev)
{
	int ret;
	struct exynos_usbdrd_phy *phy_drd = dev_get_drvdata(dev);

	/*
	 * There is issue, when USB3.0 PHY is in active state
	 * after resume. This leads to increased power consumption
	 * if no USB drivers use the PHY.
	 *
	 * The following code shutdowns the PHY, so it is in defined
	 * state (OFF) after resume. If any USB driver already got
	 * the PHY at this time, we do nothing and just exit.
	 */

	dev_dbg(dev, "%s\n", __func__);

	ret = exynos_usbdrd_clk_enable(phy_drd, false);
	if (ret) {
		dev_err(phy_drd->dev, "%s: Failed to enable clk\n", __func__);
		return ret;
	}

	__exynos_usbdrd_phy_shutdown(phy_drd);

	exynos_usbdrd_clk_disable(phy_drd, false);

	return 0;
}

static const struct dev_pm_ops exynos_usbdrd_phy_dev_pm_ops = {
	.resume	= exynos_usbdrd_phy_resume,
};

#define EXYNOS_USBDRD_PHY_PM_OPS	&(exynos_usbdrd_phy_dev_pm_ops)
#else
#define EXYNOS_USBDRD_PHY_PM_OPS	NULL
#endif

static struct platform_driver phy_exynos_usbdrd = {
	.probe	= exynos_usbdrd_phy_probe,
	.driver = {
		.of_match_table	= exynos_usbdrd_phy_of_match,
		.name		= "phy_exynos_usbdrd",
		.pm		= EXYNOS_USBDRD_PHY_PM_OPS,
	}
};

module_platform_driver(phy_exynos_usbdrd);
MODULE_DESCRIPTION("Samsung EXYNOS SoCs USB DRD controller PHY driver");
MODULE_AUTHOR("Vivek Gautam <gautam.vivek@samsung.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:phy_exynos_usbdrd");
