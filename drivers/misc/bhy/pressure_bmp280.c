/*
 *  Copyright (C) 2012, Samsung Electronics Co. Ltd. All Rights Reserved.
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
 *
 */
#include "bhy_core.h"

#define	VENDOR		"BOSCH"
#define	CHIP_ID		"BMP280"

#define PRESSURE_CALIBRATION_FILE_PATH	"/efs/FactoryApp/baro_delta"

#define	PR_ABS_MAX	8388607		/* 24 bit 2'compl */
#define	PR_ABS_MIN	-8388608

/*************************************************************************/
/* factory Sysfs                                                         */
/*************************************************************************/
static ssize_t sea_level_pressure_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct bhy_client_data *data = dev_get_drvdata(dev);

	sscanf(buf, "%d", &data->pressure_sealevel);

	if (data->pressure_sealevel == 0) {
		pr_info("[bhy] <%s>, our->temperature = 0\n", __func__);
		data->pressure_sealevel = -1;
	}

	pr_info("[bhy] <%s> sea_level_pressure = %d\n",
		__func__, data->pressure_sealevel);
	return size;
}

int pressure_open_calibration(struct bhy_client_data *data)
{
	char buf[10] = {0,};
	int err = 0;
	mm_segment_t old_fs;
	struct file *cal_filp = NULL;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	cal_filp = filp_open(PRESSURE_CALIBRATION_FILE_PATH, O_RDONLY, 0666);
	if (IS_ERR(cal_filp)) {
		err = PTR_ERR(cal_filp);
		if (err != -ENOENT)
			pr_err("[bhy] <%s> Can't open calibration file(%d)\n",
				__func__, err);
		set_fs(old_fs);
		return err;
	}
	err = cal_filp->f_op->read(cal_filp,
		buf, 10 * sizeof(char), &cal_filp->f_pos);
	if (err < 0) {
		pr_err("[bhy] <%s> Can't read the cal data from file (%d)\n",
			__func__, err);
		return err;
	}
	filp_close(cal_filp, current->files);
	set_fs(old_fs);

	err = kstrtoint(buf, 10, &data->pressure_cal);
	if (err < 0) {
		pr_err("[bhy] <%s> kstrtoint failed. %d", __func__, err);
		return err;
	}

	pr_info("[bhy] <%s> open barometer calibration %d",
		__func__, data->pressure_cal);

	if (data->pressure_cal < PR_ABS_MIN
		|| data->pressure_cal > PR_ABS_MAX)
		pr_err("[SSP]: %s - wrong offset value!!!\n", __func__);

	return err;
}

static ssize_t pressure_cabratioin_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct bhy_client_data *data = dev_get_drvdata(dev);
	int pressure_cal = 0, err = 0;

	err = kstrtoint(buf, 10, &pressure_cal);
	if (err < 0) {
		pr_err("[bhy] <%s> kstrtoint failed.(%d)", __func__, err);
		return err;
	}

	if (pressure_cal < PR_ABS_MIN || pressure_cal > PR_ABS_MAX)
		return -EINVAL;

	data->pressure_cal = (s32)pressure_cal;

	return size;
}

static ssize_t pressure_cabratioin_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct bhy_client_data *data = dev_get_drvdata(dev);

	pressure_open_calibration(data);

	return sprintf(buf, "%d\n", data->pressure_cal);
}

/* sysfs for vendor & name */
static ssize_t pressure_vendor_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", VENDOR);
}

static ssize_t pressure_name_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", CHIP_ID);
}

static DEVICE_ATTR(vendor,  S_IRUGO, pressure_vendor_show, NULL);
static DEVICE_ATTR(name,  S_IRUGO, pressure_name_show, NULL);
static DEVICE_ATTR(calibration,  S_IRUGO | S_IWUSR | S_IWGRP,
	pressure_cabratioin_show, pressure_cabratioin_store);
static DEVICE_ATTR(sea_level_pressure, S_IWUSR | S_IWGRP,
	NULL, sea_level_pressure_store);

static struct device_attribute *pressure_attrs[] = {
	&dev_attr_vendor,
	&dev_attr_name,
	&dev_attr_calibration,
	&dev_attr_sea_level_pressure,
	NULL,
};

void initialize_pressure_factorytest(struct bhy_client_data*data)
{
	sensors_register(data->prs_device, data, pressure_attrs,
		"barometer_sensor");
}

void remove_pressure_factorytest(struct bhy_client_data *data)
{
	sensors_unregister(data->prs_device, pressure_attrs);
}
