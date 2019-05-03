/*
 * Core MFD support for Cirrus Logic CS35L40 codec
 *
 * Copyright 2017 Cirrus Logic
 *
 * Author:	David Rhodes	<david.rhodes@cirrus.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/platform_device.h>

#include <sound/cs35l40.h>
#include <linux/mfd/cs35l40/core.h>

#define CS35L40_MFD_SYSFS_CLASS_NAME "cirrus"

static const struct mfd_cell cs35l40_devs[] = {
	{ .name = "cs35l40-codec", },
	{ .name = "cs35l40-cal", },
	{ .name = "cs35l40-bd", },
};

static const char * const cs35l40_supplies[] = {
	"VA",
	"VP",
};

int cs35l40_dev_init(struct cs35l40_data *cs35l40)
{
	int ret, i;

	dev_set_drvdata(cs35l40->dev, cs35l40);
	dev_info(cs35l40->dev, "Prince MFD core probe\n");

	if (dev_get_platdata(cs35l40->dev))
		memcpy(&cs35l40->pdata, dev_get_platdata(cs35l40->dev),
		       sizeof(cs35l40->pdata));

	cs35l40->mfd_class = class_create(THIS_MODULE,
				CS35L40_MFD_SYSFS_CLASS_NAME);
	if (IS_ERR(cs35l40->mfd_class)) {
		ret = PTR_ERR(cs35l40->mfd_class);
		dev_err(cs35l40->dev, "err class create\n");
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(cs35l40_supplies); i++)
		cs35l40->supplies[i].supply = cs35l40_supplies[i];

	cs35l40->num_supplies = ARRAY_SIZE(cs35l40_supplies);

	ret = devm_regulator_bulk_get(cs35l40->dev, cs35l40->num_supplies,
					cs35l40->supplies);
	if (ret != 0) {
		dev_err(cs35l40->dev,
			"Failed to request core supplies: %d\n",
			ret);
		return ret;
	}

	ret = regulator_bulk_enable(cs35l40->num_supplies, cs35l40->supplies);
	if (ret != 0) {
		dev_err(cs35l40->dev,
			"Failed to enable core supplies: %d\n", ret);
		return ret;
	}

	/* returning NULL can be an option if in stereo mode */
	cs35l40->reset_gpio = devm_gpiod_get_optional(cs35l40->dev, "reset",
							GPIOD_OUT_LOW);
	if (IS_ERR(cs35l40->reset_gpio)) {
		ret = PTR_ERR(cs35l40->reset_gpio);
		cs35l40->reset_gpio = NULL;
		if (ret == -EBUSY) {
			dev_info(cs35l40->dev,
				 "Reset line busy, assuming shared reset\n");
		} else {
			dev_err(cs35l40->dev,
				"Failed to get reset GPIO: %d\n", ret);
			goto err;
		}
	}

	if (cs35l40->reset_gpio) {
		usleep_range(1000, 1100);
		gpiod_set_value_cansleep(cs35l40->reset_gpio, 1);
	}

	usleep_range(2000, 2100);

	ret = mfd_add_devices(cs35l40->dev, PLATFORM_DEVID_NONE, cs35l40_devs,
				ARRAY_SIZE(cs35l40_devs),
				NULL, 0, NULL);
	if (ret) {
		dev_err(cs35l40->dev, "Failed to add subdevices: %d\n", ret);
		ret = -EINVAL;
		goto err;
	}

	return 0;

err:
	regulator_bulk_disable(cs35l40->num_supplies, cs35l40->supplies);
	return ret;
}

int cs35l40_dev_exit(struct cs35l40_data *cs35l40)
{
	mfd_remove_devices(cs35l40->dev);
	regulator_bulk_disable(cs35l40->num_supplies, cs35l40->supplies);
	return 0;
}
