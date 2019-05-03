/*
 * SPI bus interface to Cirrus Logic CS35L40 codec
 *
 * Copyright 2017 Cirrus Logic
 *
 * Author:	David Rhodes	<david.rhodes@cirrus.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/spi/spi.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/of.h>

#include "sound/cs35l40.h"
#include "cs35l40.h"
#include <linux/mfd/cs35l40/registers.h>

static struct regmap_config cs35l40_regmap_spi = {
	.reg_bits = 32,
	.val_bits = 32,
	.pad_bits = 16,
	.reg_stride = 4,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG,
	.max_register = CS35L40_LASTREG,
	.reg_defaults = cs35l40_reg,
	.num_reg_defaults = ARRAY_SIZE(cs35l40_reg),
	.volatile_reg = cs35l40_volatile_reg,
	.readable_reg = cs35l40_readable_reg,
	.cache_type = REGCACHE_RBTREE,
};

static const struct of_device_id cs35l40_of_match[] = {
	{.compatible = "cirrus,cs35l40"},
	{.compatible = "cirrus,cs35l41"},
	{},
};
MODULE_DEVICE_TABLE(of, cs35l40_of_match);

static const struct spi_device_id cs35l40_id_spi[] = {
	{"cs35l40", 0},
	{"cs35l41", 0},
	{}
};

MODULE_DEVICE_TABLE(spi, cs35l40_id_spi);

static int cs35l40_spi_probe(struct spi_device *spi)
{
	const struct regmap_config *regmap_config = &cs35l40_regmap_spi;
	struct cs35l40_platform_data *pdata =
					dev_get_platdata(&spi->dev);
	struct cs35l40_data *cs35l40;
	int ret;

	cs35l40 = devm_kzalloc(&spi->dev,
			       sizeof(struct cs35l40_data),
			       GFP_KERNEL);
	if (cs35l40 == NULL)
		return -ENOMEM;

	spi_set_drvdata(spi, cs35l40);
	cs35l40->regmap = devm_regmap_init_spi(spi, regmap_config);
	if (IS_ERR(cs35l40->regmap)) {
		ret = PTR_ERR(cs35l40->regmap);
		dev_err(&spi->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	cs35l40->dev = &spi->dev;
	cs35l40->irq = spi->irq;
	cs35l40->pdata = pdata;

	dev_info(&spi->dev, "CS35L40 SPI MFD probe\n");

	return cs35l40_dev_init(cs35l40);
}

static int cs35l40_spi_remove(struct spi_device *spi)
{
	struct cs35l40_data *cs35l40 = spi_get_drvdata(spi);

	return cs35l40_dev_exit(cs35l40);
}

static struct spi_driver cs35l40_spi_driver = {
	.driver = {
		.name		= "cs35l40",
		.of_match_table = cs35l40_of_match,
	},
	.id_table	= cs35l40_id_spi,
	.probe		= cs35l40_spi_probe,
	.remove		= cs35l40_spi_remove,
};

module_spi_driver(cs35l40_spi_driver);

MODULE_DESCRIPTION("SPI CS35L40 driver");
MODULE_AUTHOR("David Rhodes, Cirrus Logic Inc, <david.rhodes@cirrus.com>");
MODULE_LICENSE("GPL");