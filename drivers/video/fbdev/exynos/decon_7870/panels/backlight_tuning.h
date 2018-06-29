/* linux/drivers/video/fbdev/exynos/decon/panels/backlight_tuning.c
 *
 * Copyright (c) 2017 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/backlight.h>
#include <linux/i2c.h>

extern int init_bl_curve_debugfs(struct backlight_device *bd, unsigned int *table, struct i2c_client **clients);

