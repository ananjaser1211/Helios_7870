
/* linux/drivers/video/fbdev/exynos/decon/panels/dsim_panel.c
 *
 * Copyright (c) 2015 Samsung Electronics
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
#elif defined(CONFIG_PANEL_S6E3FA3_A7MAX)
struct mipi_dsim_lcd_driver *mipi_lcd_driver = &s6e3fa3_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_S6E3FA3_J7Y17)
struct mipi_dsim_lcd_driver *mipi_lcd_driver = &s6e3fa3_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_EA8061S_J7XE)
struct mipi_dsim_lcd_driver *mipi_lcd_driver = &ea8061s_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_S6E3AA2_AMS474KF09)
struct mipi_dsim_lcd_driver *mipi_lcd_driver = &s6e3aa2_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_S6E3AA2_A3Y17)
struct mipi_dsim_lcd_driver *mipi_lcd_driver = &s6e3aa2_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_LTL101AL06)
struct mipi_dsim_lcd_driver *mipi_lcd_driver = &ltl101al06_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_S6D7AA0)
struct mipi_dsim_lcd_driver *mipi_lcd_driver = &s6d7aa0_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_HX8279D)
struct mipi_dsim_lcd_driver *mipi_lcd_driver = &hx8279d_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_S6D7AA0_BV055HDM)
struct mipi_dsim_lcd_driver *mipi_lcd_driver = &s6d7aa0_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_TD4300)
struct mipi_dsim_lcd_driver *mipi_lcd_driver = &td4300_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_TD4100_J7POP)
struct mipi_dsim_lcd_driver *mipi_lcd_driver = &td4100_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_S6E8AA5X01)
struct mipi_dsim_lcd_driver *mipi_lcd_driver = &s6e8aa5x01_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_EA8061S_J7VE)
struct mipi_dsim_lcd_driver *mipi_lcd_driver = &ea8061s_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_S6D7AA0_GTACTIVE2)
struct mipi_dsim_lcd_driver *mipi_lcd_driver = &s6d7aa0_mipi_lcd_driver;
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


unsigned int lcdtype;
EXPORT_SYMBOL(lcdtype);

static int __init get_lcd_type(char *arg)
{
	get_option(&arg, &lcdtype);

	dsim_info("--- Parse LCD TYPE ---\n");
	dsim_info("LCDTYPE : %08x\n", lcdtype);

	return 0;
}
early_param("lcdtype", get_lcd_type);

