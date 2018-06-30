/* linux/drivers/video/exynos_decon/panel/dsim_panel.h
 *
 * Header file for Samsung MIPI-DSI LCD Panel driver.
 *
 * Copyright (c) 2013 Samsung Electronics
 * Minwoo Kim <minwoo7945.kim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __DSIM_PANEL__
#define __DSIM_PANEL__


extern unsigned int lcdtype;
#if defined(CONFIG_PANEL_EA8064G_DYNAMIC)
extern struct mipi_dsim_lcd_driver ea8064g_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_S6E3FA3_J7XE)
extern struct mipi_dsim_lcd_driver s6e3fa3_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_S6E3FA3_A7MAX)
extern struct mipi_dsim_lcd_driver s6e3fa3_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_S6E3FA3_J7Y17)
extern struct mipi_dsim_lcd_driver s6e3fa3_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_EA8061S_J7XE)
extern struct mipi_dsim_lcd_driver ea8061_mipi_lcd_driver;
extern struct mipi_dsim_lcd_driver ea8061s_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_S6E3AA2_AMS474KF09)
extern struct mipi_dsim_lcd_driver s6e3aa2_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_S6E3AA2_A3Y17)
extern struct mipi_dsim_lcd_driver s6e3aa2_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_LTL101AL06)
extern struct mipi_dsim_lcd_driver ltl101al06_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_S6D7AA0)
extern struct mipi_dsim_lcd_driver s6d7aa0_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_HX8279D)
extern struct mipi_dsim_lcd_driver hx8279d_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_S6D7AA0_BV055HDM)
extern struct mipi_dsim_lcd_driver s6d7aa0_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_TD4300)
extern struct mipi_dsim_lcd_driver td4300_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_TD4100_J7POP)
extern struct mipi_dsim_lcd_driver td4100_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_S6E8AA5X01)
extern struct mipi_dsim_lcd_driver s6e8aa5x01_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_EA8061S_J7VE)
extern struct mipi_dsim_lcd_driver ea8061_mipi_lcd_driver;
extern struct mipi_dsim_lcd_driver ea8061s_mipi_lcd_driver;
#elif defined(CONFIG_PANEL_S6D7AA0_GTACTIVE2)
extern struct mipi_dsim_lcd_driver s6d7aa0_mipi_lcd_driver;
#endif

int dsim_panel_ops_init(struct dsim_device *dsim);


#endif
