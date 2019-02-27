/*
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#ifndef _COMMON_H
#define _COMMON_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/pinctrl/consumer.h>

#define SI47XX_VOLUME_NUM 16

struct si47xx_platform_data {
	u32 rx_vol[SI47XX_VOLUME_NUM];
	int rst_gpio;
	int int_gpio;
	int si47xx_irq;
	u32 mode;
};

/* VNVS:18-JUN'10 : For testing RDS */
/* Enable only for debugging RDS */
#ifdef RDS_DEBUG
#define GROUP_TYPE_2A     (2 * 2 + 0)
#define GROUP_TYPE_2B     (2 * 2 + 1)
#endif

extern wait_queue_head_t si47xx_waitq;
extern void si47xx_dev_digitalmode(bool onoff);
#endif
