/*
 * core.h  --  MFD includes for Cirrus Logic CS35L40 codecs
 *
 * Copyright 2017 Cirrus Logic
 *
 * Author:	David Rhodes	<david.rhodes@cirrus.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef CS35L40_MFD_CORE_H
#define CS35L40_MFD_CORE_H

#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

struct cs35l40_data {
	struct cs35l40_platform_data *pdata;
	struct device *dev;
	struct regmap *regmap;
	struct class *mfd_class;
	struct regulator_bulk_data supplies[2];
	struct gpio_desc *reset_gpio;
	int num_supplies;
	int irq;
};

#endif