/*
 * Copyright (c) Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/lcd.h>
#include "../dsim.h"

#include "dsim_panel.h"

#if defined(CONFIG_PANEL_EA8064G_DYNAMIC)
struct mipi_dsim_lcd_driver *mipi_lcd_driver = &ea8064g_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_S6E3FA3_J7XE)
struct mipi_dsim_lcd_driver *mipi_lcd_driver = &s6e3fa3_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_EA8061S_J7XE)
struct mipi_dsim_lcd_driver *mipi_lcd_driver = &ea8061s_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_LTL101AL06)
struct mipi_dsim_lcd_driver *mipi_lcd_driver = &ltl101al06_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_S6D7AA0)
struct mipi_dsim_lcd_driver *mipi_lcd_driver = &s6d7aa0_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_HX8279D)
struct mipi_dsim_lcd_driver *mipi_lcd_driver = &hx8279d_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_S6D7AA0_BV055HDM)
struct mipi_dsim_lcd_driver *mipi_lcd_driver = &s6d7aa0_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_S6E3AA2_AMS474KF09)
struct mipi_dsim_lcd_driver *mipi_lcd_driver = &s6e3aa2_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_TD4300)
struct mipi_dsim_lcd_driver *mipi_lcd_driver = &td4300_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_S6E3AA2_A3Y17)
struct mipi_dsim_lcd_driver *mipi_lcd_driver = &s6e3aa2_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_S6E8AA5X01)
struct mipi_dsim_lcd_driver *mipi_lcd_driver = &s6e8aa5x01_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_TD4100_J7POP)
struct mipi_dsim_lcd_driver *mipi_lcd_driver = &td4100_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_S6E3FA3_J7Y17)
struct mipi_dsim_lcd_driver *mipi_lcd_driver = &s6e3fa3_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_EA8061S_J7VE)
struct mipi_dsim_lcd_driver *mipi_lcd_driver = &ea8061s_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_S6D7AA0_GTACTIVE2)
struct mipi_dsim_lcd_driver *mipi_lcd_driver = &s6d7aa0_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_S6D78A0_GPPIRIS)
struct mipi_dsim_lcd_driver *mipi_lcd_driver = &s6d78a0_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_S6D7AT0B_J7TOP)
struct mipi_dsim_lcd_driver *mipi_lcd_driver = &s6d7at0b_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_S6E8AA5X01_A6LTE)
struct mipi_dsim_lcd_driver *mipi_lcd_driver = &s6e8aa5x01_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_S6E8AA5X01_J6Y18)
struct mipi_dsim_lcd_driver *mipi_lcd_driver = &s6e8aa5x01_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_S6D7AT0B_M10LTE)
struct mipi_dsim_lcd_driver *mipi_lcd_driver = &s6d7at0b_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_TD4101_A2CORELTE)
struct mipi_dsim_lcd_driver *mipi_lcd_driver = &td4101_mipi_lcd_driver;
#endif

int dsim_panel_ops_init(struct dsim_device *dsim)
{
	int ret = 0;
#if defined(CONFIG_PANEL_EA8061S_J7XE)
	if (dsim) {
		if (dsim->octa_id)
			mipi_lcd_driver = &ea8061_mipi_lcd_driver;
	}
#endif
#if defined(CONFIG_PANEL_EA8061S_J7VE)
	if (dsim) {
		if (!dsim->octa_id)
			mipi_lcd_driver = &ea8061_mipi_lcd_driver;
	}
#endif
	if (dsim)
		dsim->panel_ops = mipi_lcd_driver;

	return ret;
}

int replace_lcd_driver(struct mipi_dsim_lcd_driver *drv)
{
	struct device_node *node;
	int count = 0;
	char *dts_name = "lcd_info";

	node = of_find_node_with_property(NULL, dts_name);
	if (!node) {
		dsim_info("%s: of_find_node_with_property\n", __func__);
		goto exit;
	}

	count = of_count_phandle_with_args(node, dts_name, NULL);
	if (!count) {
		dsim_info("%s: of_count_phandle_with_args\n", __func__);
		goto exit;
	}

	node = of_parse_phandle(node, dts_name, 0);
	if (!node) {
		dsim_info("%s: of_parse_phandle\n", __func__);
		goto exit;
	}

	if (count != 1) {
		dsim_info("%s: we need only one phandle in lcd_info\n", __func__);
		goto exit;
	}

	if (IS_ERR_OR_NULL(drv) || IS_ERR_OR_NULL(drv->name)) {
		dsim_info("%s: we need lcd_drv name to compare with device tree name(%s)\n", __func__, node->name);
		goto exit;
	}

	if (strstarts(node->name, drv->name)) {
		mipi_lcd_driver = drv;
		dsim_info("%s: %s is registered\n", __func__, mipi_lcd_driver->name);
	} else
		dsim_info("%s: %s is not with prefix: %s\n", __func__, node->name, drv->name);

exit:
	return 0;
}


unsigned int lcdtype;
EXPORT_SYMBOL(lcdtype);

static int __init get_lcd_type(char *arg)
{
	get_option(&arg, &lcdtype);

	dsim_info("%s: lcdtype: %6X\n", __func__, lcdtype);

	return 0;
}
early_param("lcdtype", get_lcd_type);

