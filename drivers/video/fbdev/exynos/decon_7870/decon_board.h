/*
 * Copyright (c) Samsung Electronics Co., Ltd.
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
int of_gpio_get_active(const char *gpioname);
int of_gpio_get_value(const char *gpioname);
int of_gpio_set_value(const char *gpioname, int value);
int of_get_gpio_with_name(const char *gpioname);

#endif
