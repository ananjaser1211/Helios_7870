/*
 * Big-data logging support for Cirrus Logic CS35L40 codec
 *
 * Copyright 2017 Cirrus Logic
 *
 * Author:	David Rhodes	<david.rhodes@cirrus.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/file.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/firmware.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <linux/power_supply.h>
#include <linux/fs.h>
#include <linux/ktime.h>

#include <linux/mfd/cs35l40/core.h>
#include <linux/mfd/cs35l40/registers.h>
#include <linux/mfd/cs35l40/big_data.h>
#include <linux/mfd/cs35l40/wmfw.h>

#define CS35L40_BD_VERSION "5.00.6"

#define CS35L40_BD_CLASS_NAME		"cs35l40"
#define CS35L40_BD_DIR_NAME		"cirrus_bd"

struct cs35l40_bd_t {
	struct class *bd_class;
	struct device *dev;
	struct regmap *regmap;
	unsigned int max_exc;
	unsigned int over_exc_count;
	unsigned int max_temp;
	unsigned int over_temp_count;
	unsigned int abnm_mute;
	unsigned long long int last_update;
};

struct cs35l40_bd_t *cs35l40_bd;

void cs35l40_bd_store_values(void)
{
	unsigned int max_exc, over_exc_count, max_temp,
				over_temp_count, abnm_mute;

	regmap_read(cs35l40_bd->regmap, CS35L40_BD_MAX_EXC,
			&max_exc);
	regmap_read(cs35l40_bd->regmap, CS35L40_BD_OVER_EXC_COUNT,
			&over_exc_count);
	regmap_read(cs35l40_bd->regmap, CS35L40_BD_MAX_TEMP,
			&max_temp);
	regmap_read(cs35l40_bd->regmap, CS35L40_BD_OVER_TEMP_COUNT,
			&over_temp_count);
	regmap_read(cs35l40_bd->regmap, CS35L40_BD_ABNORMAL_MUTE,
			&abnm_mute);

	cs35l40_bd->over_temp_count += over_temp_count;
	cs35l40_bd->over_exc_count += over_exc_count;
	if (max_exc > cs35l40_bd->max_exc)
		cs35l40_bd->max_exc = max_exc;
	if (max_temp > cs35l40_bd->max_temp)
		cs35l40_bd->max_temp = max_temp;
	cs35l40_bd->abnm_mute += abnm_mute;
	cs35l40_bd->last_update = ktime_to_ns(ktime_get());

	dev_info(cs35l40_bd->dev, "Values stored:\n");
	dev_info(cs35l40_bd->dev, "Max Excursion:\t\t%d.%d\n",
				cs35l40_bd->max_exc >> CS35L40_BD_EXC_RADIX,
				(cs35l40_bd->max_exc &
				(((1 << CS35L40_BD_EXC_RADIX) - 1))) *
				10000 / (1 << CS35L40_BD_EXC_RADIX));
	dev_info(cs35l40_bd->dev, "Over Excursion Count:\t%d\n",
				cs35l40_bd->over_exc_count);
	dev_info(cs35l40_bd->dev, "Max Temp:\t\t\t%d.%d\n",
				cs35l40_bd->max_temp >> CS35L40_BD_TEMP_RADIX,
				(cs35l40_bd->max_temp &
				(((1 << CS35L40_BD_TEMP_RADIX) - 1))) *
				10000 / (1 << CS35L40_BD_TEMP_RADIX));
	dev_info(cs35l40_bd->dev, "Over Temp Count:\t\t%d\n",
				cs35l40_bd->over_temp_count);
	dev_info(cs35l40_bd->dev, "Abnormal Mute:\t\t%d\n",
				cs35l40_bd->abnm_mute);
	dev_info(cs35l40_bd->dev, "Timestamp:\t\t%llu\n",
				cs35l40_bd->last_update);

	regmap_write(cs35l40_bd->regmap, CS35L40_BD_MAX_EXC, 0);
	regmap_write(cs35l40_bd->regmap, CS35L40_BD_OVER_EXC_COUNT, 0);
	regmap_write(cs35l40_bd->regmap, CS35L40_BD_MAX_TEMP, 0);
	regmap_write(cs35l40_bd->regmap, CS35L40_BD_OVER_TEMP_COUNT, 0);
	regmap_write(cs35l40_bd->regmap, CS35L40_BD_ABNORMAL_MUTE, 0);

}
EXPORT_SYMBOL_GPL(cs35l40_bd_store_values);

/***** SYSFS Interfaces *****/

static ssize_t cs35l40_bd_version_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	return sprintf(buf, CS35L40_BD_VERSION "\n");
}

static ssize_t cs35l40_bd_version_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	return 0;
}

static ssize_t cs35l40_bd_max_exc_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	int ret = sprintf(buf, "%d.%d\n",
			cs35l40_bd->max_exc >> CS35L40_BD_EXC_RADIX,
			(cs35l40_bd->max_exc &
			(((1 << CS35L40_BD_EXC_RADIX) - 1))) *
			10000 / (1 << CS35L40_BD_EXC_RADIX));

	cs35l40_bd->max_exc = 0;
	return ret;
}

static ssize_t cs35l40_bd_max_exc_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	return 0;
}

static ssize_t cs35l40_bd_over_exc_count_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	int ret = sprintf(buf, "%d\n", cs35l40_bd->over_exc_count);

	cs35l40_bd->over_exc_count = 0;
	return ret;
}

static ssize_t cs35l40_bd_over_exc_count_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	return 0;
}

static ssize_t cs35l40_bd_max_temp_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	int ret = sprintf(buf, "%d.%d\n",
			cs35l40_bd->max_temp >> CS35L40_BD_TEMP_RADIX,
			(cs35l40_bd->max_temp &
			(((1 << CS35L40_BD_TEMP_RADIX) - 1))) *
			10000 / (1 << CS35L40_BD_TEMP_RADIX));

	cs35l40_bd->max_temp = 0;
	return ret;
}

static ssize_t cs35l40_bd_max_temp_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	return 0;
}

static ssize_t cs35l40_bd_over_temp_count_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	int ret = sprintf(buf, "%d\n", cs35l40_bd->over_temp_count);

	cs35l40_bd->over_temp_count = 0;
	return ret;
}

static ssize_t cs35l40_bd_over_temp_count_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	return 0;
}

static ssize_t cs35l40_bd_abnm_mute_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	int ret = sprintf(buf, "%d\n", cs35l40_bd->abnm_mute);

	cs35l40_bd->abnm_mute = 0;
	return ret;
}

static ssize_t cs35l40_bd_abnm_mute_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	return 0;
}

static ssize_t cs35l40_bd_store_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	return 0;
}

static ssize_t cs35l40_bd_store_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	int store;
	int ret = kstrtos32(buf, 10, &store);

	if (ret == 0) {
		if (store == 1)
			cs35l40_bd_store_values();
	}

	return size;
}

static DEVICE_ATTR(version, 0444, cs35l40_bd_version_show,
				cs35l40_bd_version_store);
static DEVICE_ATTR(max_exc, 0444, cs35l40_bd_max_exc_show,
				cs35l40_bd_max_exc_store);
static DEVICE_ATTR(over_exc_count, 0444, cs35l40_bd_over_exc_count_show,
				cs35l40_bd_over_exc_count_store);
static DEVICE_ATTR(max_temp, 0444, cs35l40_bd_max_temp_show,
				cs35l40_bd_max_temp_store);
static DEVICE_ATTR(over_temp_count, 0444, cs35l40_bd_over_temp_count_show,
				cs35l40_bd_over_temp_count_store);
static DEVICE_ATTR(abnm_mute, 0444, cs35l40_bd_abnm_mute_show,
				cs35l40_bd_abnm_mute_store);
static DEVICE_ATTR(store, 0644, cs35l40_bd_store_show,
				cs35l40_bd_store_store);

static struct attribute *cs35l40_bd_attr[] = {
	&dev_attr_version.attr,
	&dev_attr_max_exc.attr,
	&dev_attr_over_exc_count.attr,
	&dev_attr_max_temp.attr,
	&dev_attr_over_temp_count.attr,
	&dev_attr_abnm_mute.attr,
	&dev_attr_store.attr,
	NULL,
};

static struct attribute_group cs35l40_bd_attr_grp = {
	.attrs = cs35l40_bd_attr,
};

static int cs35l40_bd_probe(struct platform_device *pdev)
{
	struct cs35l40_data *cs35l40 = dev_get_drvdata(pdev->dev.parent);
	int ret;
	unsigned int temp;

	cs35l40_bd = kzalloc(sizeof(struct cs35l40_bd_t), GFP_KERNEL);
	if (cs35l40_bd == NULL)
		return -ENOMEM;

	cs35l40_bd->dev = device_create(cs35l40->mfd_class, NULL, 1, NULL,
						CS35L40_BD_DIR_NAME);
	if (IS_ERR(cs35l40_bd->dev)) {
		ret = PTR_ERR(cs35l40_bd->dev);
		goto err_dev;
	}

	cs35l40_bd->regmap = cs35l40->regmap;
	regmap_read(cs35l40_bd->regmap, 0x00000000, &temp);
	dev_info(&pdev->dev,
		"Prince Big Data Driver probe, Dev ID = %x\n", temp);

	ret = sysfs_create_group(&cs35l40_bd->dev->kobj, &cs35l40_bd_attr_grp);
	if (ret) {
		dev_err(cs35l40_bd->dev, "Failed to create sysfs group\n");
		goto err_dev;
	}

	cs35l40_bd->max_exc = 0;
	cs35l40_bd->max_temp = 0;
	cs35l40_bd->over_temp_count = 0;
	cs35l40_bd->over_exc_count = 0;
	cs35l40_bd->abnm_mute = 0;

	return 0;

err_dev:
	kfree(cs35l40_bd);
	return ret;
}

static int cs35l40_bd_remove(struct platform_device *pdev)
{
	kfree(cs35l40_bd);
	return 0;
}

struct platform_driver cs35l40_bd_driver = {
	.driver = {
		.name = "cs35l40-bd",
		.owner = THIS_MODULE,
	},
	.probe = cs35l40_bd_probe,
	.remove = cs35l40_bd_remove,
};
module_platform_driver(cs35l40_bd_driver);
