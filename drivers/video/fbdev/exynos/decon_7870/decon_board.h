/* linux/drivers/video/exynos/decon_display/decon_board.c
 *
 * Copyright (c) 2015 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __DECON_DISPLAY_BOARD_HEADER__
#define __DECON_DISPLAY_BOARD_HEADER__

#include <linux/device.h>

extern unsigned int lcdtype;

void run_list(struct device *dev, const char *name);

#endif
