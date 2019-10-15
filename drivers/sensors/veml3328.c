/* VEML3328 Optical Sensor Driver
 *
 * Copyright (C) 2018 Samsung Electronics. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/errno.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/of_gpio.h>
#include <linux/types.h>
#include <linux/regulator/consumer.h>
#include <linux/sensor/sensors_core.h>

#include "veml3328.h"

#define I2C_M_WR          0 /* for i2c Write */
#define I2c_M_RD          1 /* for i2c Read */
#define I2C_RETRY_CNT     5

#define REL_RED           REL_HWHEEL
#define REL_GREEN         REL_DIAL
#define REL_BLUE          REL_WHEEL
#define REL_CLEAR         REL_MISC
#define REL_IR            REL_Z

#define DEFAULT_DELAY_MS  200

#define VENDOR_NAME       "CAPELLA"
#define CHIP_NAME         "VEML3328"
#define MODULE_NAME       "light_sensor"

struct veml3328_data {
	struct device *ls_dev;
	struct input_dev *input_dev;

	struct i2c_client *i2c_client;

	u8 als_enable;

	struct hrtimer light_timer;
	struct workqueue_struct *light_wq;
	struct work_struct light_work;
	ktime_t light_poll_delay;

	u16 red;
	u16 green;
	u16 blue;
	u16 clear;
	u16 ir;

	struct mutex control_mutex;
};

int veml3328_i2c_read(struct i2c_client *client, u8 reg, u8 *val, int len)
{
	int ret;
	struct i2c_msg msg[2];
	int retry = 0;

	if ((client == NULL) || (!client->adapter))
		return -ENODEV;

	/* Write slave address */
	msg[0].addr = client->addr;
	msg[0].flags = I2C_M_WR;
	msg[0].len = 1;
	msg[0].buf = &reg;

	/* Read data */
	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = len;
	msg[1].buf = val;

	for (retry = 0; retry < I2C_RETRY_CNT; retry++) {
		ret = i2c_transfer(client->adapter, msg, 2);
		if (ret == 2)
			break;

		SENSOR_ERR("i2c read error, ret=%d retry=%d\n", ret, retry);

		if (retry < I2C_RETRY_CNT - 1)
			usleep_range(2000, 2000);
	}

	return (ret == 2) ? 0 : ret;
}

int veml3328_i2c_read_word(struct veml3328_data *drv_data, u8 reg, u16 *val)
{
	u8 buf[2] = {0,0};
	int ret = 0;

	ret = veml3328_i2c_read(drv_data->i2c_client, reg, buf, 2);
	if (!ret)
		*val = (buf[1] << 8) | buf[0];

	return ret;
}

int veml3328_i2c_write(struct i2c_client *client, u8 reg, u8 *val, int len)
{
	int ret;
	struct i2c_msg msg;
	unsigned char data[11];
	int retry = 0;

	if ((client == NULL) || (!client->adapter))
		return -ENODEV;

	if (len >= 10) {
		SENSOR_ERR("Exceeded length limit, len=%d\n", len);
		return -EINVAL;
	}

	data[0] = reg;
	memcpy(&data[1], val, len);

	msg.addr = client->addr;
	msg.flags = I2C_M_WR;
	msg.len = len + 1;
	msg.buf = data;

	for (retry = 0; retry < I2C_RETRY_CNT; retry++) {
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (ret == 1)
			break;

		SENSOR_ERR("i2c write error, ret=%d retry=%d\n", ret, retry);

		if (retry < I2C_RETRY_CNT - 1)
			usleep_range(2000, 2000);
	}

	return (ret == 1) ? 0 : ret;
}

int veml3328_i2c_write_word(struct veml3328_data *drv_data, u8 reg, u16 val)
{
	u8 buf[2];
	int ret = 0;
    
    buf[0] = (val & 0x00FF);
    buf[1] = (val & 0xFF00) >> 8;

	ret = veml3328_i2c_write(drv_data->i2c_client, reg, buf, 2);

	return ret;
}

static void light_work_func(struct work_struct *work)
{
	struct veml3328_data *drv_data = container_of(work,
			struct veml3328_data, light_work);

	veml3328_i2c_read_word(drv_data, VEML3328_R_DATA, &drv_data->red);
	veml3328_i2c_read_word(drv_data, VEML3328_G_DATA, &drv_data->green);
	veml3328_i2c_read_word(drv_data, VEML3328_B_DATA, &drv_data->blue);
	veml3328_i2c_read_word(drv_data, VEML3328_IR_DATA, &drv_data->ir);
	veml3328_i2c_read_word(drv_data, VEML3328_C_DATA, &drv_data->clear);

	input_report_rel(drv_data->input_dev, REL_RED, drv_data->red + 1);
	input_report_rel(drv_data->input_dev, REL_GREEN, drv_data->green + 1);
	input_report_rel(drv_data->input_dev, REL_BLUE, drv_data->blue + 1);
	input_report_rel(drv_data->input_dev, REL_CLEAR, drv_data->clear + 1);
	input_report_rel(drv_data->input_dev, REL_IR, drv_data->ir + 1);
	input_sync(drv_data->input_dev);

	SENSOR_INFO("red=%d green=%d blue=%d clear=%d ir=%d\n",
			drv_data->red, drv_data->green, drv_data->blue, drv_data->clear, drv_data->ir);
}

static enum hrtimer_restart light_timer_func(struct hrtimer *timer)
{
	struct veml3328_data *drv_data = container_of(timer,
			struct veml3328_data, light_timer);

	SENSOR_INFO("\n");

	queue_work(drv_data->light_wq, &drv_data->light_work);
	hrtimer_forward_now(&drv_data->light_timer, drv_data->light_poll_delay);

	return HRTIMER_RESTART;
}

static int lightsensor_enable(struct veml3328_data *drv_data)
{
	int ret = 0;
	u16 val;

	mutex_lock(&drv_data->control_mutex);

	SENSOR_INFO("\n");

	drv_data->als_enable = 1;

	val = VEML3328_CONF_IT_100MS | VEML3328_CONF_GAIN1_X4 | VEML3328_CONF_GAIN2_X4;

	ret = veml3328_i2c_write_word(drv_data, VEML3328_CONF, val);
	if (ret) {
		SENSOR_ERR("failed, ret=%d", ret);
	} else {
		hrtimer_start(&drv_data->light_timer, drv_data->light_poll_delay, HRTIMER_MODE_REL);	
	}

	mutex_unlock(&drv_data->control_mutex);

	return ret;
}

static int lightsensor_disable(struct veml3328_data *drv_data)
{
	int ret = 0;
	u16 val;

	mutex_lock(&drv_data->control_mutex);

	SENSOR_INFO("\n");

	hrtimer_cancel(&drv_data->light_timer);
	cancel_work_sync(&drv_data->light_work);

	val = VEML3328_CONF_SD;

	ret = veml3328_i2c_write_word(drv_data, VEML3328_CONF, val);
	if (ret)
		SENSOR_ERR("failed, ret=%d", ret);

	drv_data->als_enable = 0;

	mutex_unlock(&drv_data->control_mutex);

	return ret;
}

static ssize_t light_enable_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct veml3328_data *drv_data = dev_get_drvdata(dev);
	int ret = 0;

	ret = sprintf(buf, "%d\n", drv_data->als_enable);

	return ret;
}

static ssize_t light_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct veml3328_data *drv_data = dev_get_drvdata(dev);
	int ret = 0;
	u8 enable;

	ret = kstrtou8(buf, 2, &enable);
	if (ret) {
		pr_err("[SENSOR]: %s - Invalid Argument\n", __func__);
		return ret;
	}

	if (enable != 0 && enable != 1)
		return -EINVAL;

	SENSOR_INFO("old_en=%d en=%d\n", drv_data->als_enable, enable);

	if (enable && !drv_data->als_enable) {
		ret = lightsensor_enable(drv_data);
	} else if (!enable && drv_data->als_enable) {
		ret = lightsensor_disable(drv_data);
	}

	return ret;
}

static ssize_t light_poll_delay_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct veml3328_data *drv_data = dev_get_drvdata(dev);
	int ret = 0;

	ret = sprintf(buf, "%llu\n", ktime_to_ns(drv_data->light_poll_delay));

	return ret;
}

static ssize_t light_poll_delay_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct veml3328_data *drv_data = dev_get_drvdata(dev);
	u64 new_delay;
	int ret;

	ret = kstrtoull(buf, 10, &new_delay);
	if (ret) {
		pr_err("[SENSOR]: %s - Invalid Argument\n", __func__);
		return ret;
	}

	SENSOR_INFO("delay=%llu ns\n", new_delay);

	drv_data->light_poll_delay = ns_to_ktime(new_delay);

	return ret;
}

static ssize_t light_name_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", CHIP_NAME);
}

static ssize_t light_vendor_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", VENDOR_NAME);
}

static ssize_t light_lux_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct veml3328_data *drv_data = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%u, %u, %u, %u, %u\n",
		drv_data->red, drv_data->green, drv_data->blue, drv_data->clear, drv_data->ir);
}

static ssize_t light_data_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct veml3328_data *drv_data = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%u, %u, %u, %u, %u\n",
			drv_data->red, drv_data->green, drv_data->blue, drv_data->clear, drv_data->ir);
}

static ssize_t light_reg_data_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct veml3328_data *drv_data = dev_get_drvdata(dev);
	u8 reg = 0;
	int offset = 0;
	u16 val = 0;

	for (reg = 0x00; reg <= 0x08; reg++) {
		veml3328_i2c_read_word(drv_data, reg, &val);
		SENSOR_INFO("Read Reg: 0x%2x Value: 0x%4x\n", reg, val);
		offset += snprintf(buf + offset, PAGE_SIZE - offset,
			"Reg: 0x%2x Value: 0x%4x\n", reg, val);
	}

	return offset;
}

static ssize_t light_reg_data_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct veml3328_data *drv_data = dev_get_drvdata(dev);
	int reg, val, ret;

	if (sscanf(buf, "%2x,%4x", &reg, &val) != 2) {
		SENSOR_ERR("invalid value\n");
		return count;
	}

	ret = veml3328_i2c_write_word(drv_data, reg, val);
	if(!ret)
		SENSOR_INFO("Register(0x%2x) data(0x%4x)\n", reg, val);
	else
		SENSOR_ERR("failed %d\n", ret);

	return count;
}

static DEVICE_ATTR(name, S_IRUGO, light_name_show, NULL);
static DEVICE_ATTR(vendor, S_IRUGO, light_vendor_show, NULL);
static DEVICE_ATTR(lux, S_IRUGO, light_lux_show, NULL);
static DEVICE_ATTR(raw_data, S_IRUGO, light_data_show, NULL);
static DEVICE_ATTR(reg_data, S_IRUGO, light_reg_data_show, light_reg_data_store);

static struct device_attribute *sensor_attrs[] = {
	&dev_attr_name,
	&dev_attr_vendor,
	&dev_attr_lux,
	&dev_attr_raw_data,
	&dev_attr_reg_data,
	NULL,
};

static DEVICE_ATTR(poll_delay, S_IRUGO | S_IWUSR | S_IWGRP,
		light_poll_delay_show, light_poll_delay_store);
static DEVICE_ATTR(enable, S_IRUGO | S_IWUSR | S_IWGRP,
		light_enable_show, light_enable_store);

static struct attribute *light_sysfs_attrs[] = {
	&dev_attr_enable.attr,
	&dev_attr_poll_delay.attr,
	NULL,
};

static struct attribute_group light_attribute_group = {
	.attrs = light_sysfs_attrs,
};

static int veml3328_init_input_device(struct veml3328_data *drv_data)
{
	int ret = 0;
	struct input_dev *dev;

	/* allocate lightsensor input_device */
	dev = input_allocate_device();
	if (!dev)
		return -ENOMEM;

	dev->name = MODULE_NAME;
	dev->id.bustype = BUS_I2C;

	input_set_capability(dev, EV_REL, REL_RED);
	input_set_capability(dev, EV_REL, REL_GREEN);
	input_set_capability(dev, EV_REL, REL_BLUE);
	input_set_capability(dev, EV_REL, REL_CLEAR);
	input_set_capability(dev, EV_REL, REL_IR);
	input_set_drvdata(dev, drv_data);

	ret = input_register_device(dev);
	if (ret < 0) {
		input_free_device(dev);
		return ret;
	}

	ret = sensors_create_symlink(&dev->dev.kobj, dev->name);
	if (ret < 0) {
		input_unregister_device(dev);
		return ret;
	}

	ret = sysfs_create_group(&dev->dev.kobj, &light_attribute_group);
	if (ret < 0) {
		sensors_remove_symlink(&dev->dev.kobj, dev->name);
		input_unregister_device(dev);
		return ret;
	}

	drv_data->input_dev = dev;
	return 0;
}

static int veml3328_init_registers(struct veml3328_data *drv_data)
{
	int ret = 0;
	u16 val = 0;

	// Set integration time
	val = VEML3328_CONF_IT_100MS;

	// Set gain
	val |= VEML3328_CONF_GAIN1_X4;
	val |= VEML3328_CONF_GAIN2_X4;

	// Shut down VEML3328
	val |= VEML3328_CONF_SD;

	ret = veml3328_i2c_write_word(drv_data, VEML3328_CONF, val);
	if(ret)
		SENSOR_INFO("failed, ret=%d\n", ret);

	return ret;
}

static int veml3328_device_id_check(struct veml3328_data *drv_data)
{
	int ret = 0;
	u16 val = 0;

	ret = veml3328_i2c_read_word(drv_data, VEML3328_DEVICE_ID_REG, &val);

	if (!ret) {
		val = val & 0x00FF;
		if (val == VEML3328_DEVICE_ID_VAL) {
			SENSOR_INFO("device matched, id=%d\n", val);
			return 0;
		} else {
			SENSOR_INFO("device not matched, id=%d\n", val);
			return -ENODEV;
		}
	}

	return ret;
}

static int veml3328_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int ret = 0;
	struct veml3328_data *drv_data;

	SENSOR_INFO("\n");

	drv_data = kzalloc(sizeof(struct veml3328_data), GFP_KERNEL);
	if (!drv_data)
		return -ENOMEM;

	drv_data->i2c_client = client;
	i2c_set_clientdata(client, drv_data);

	// Check if VEML3328 IC exists
	ret = veml3328_device_id_check(drv_data);
	if (ret) {
		SENSOR_ERR("VEML3328 not found, ret=%d\n", ret);
		goto err_device_id_check;
	}

	ret = veml3328_init_registers(drv_data);
	if (ret) {
		SENSOR_ERR("Register init failed, ret=%d\n", ret);
		goto err_init_registers;
	}

	ret = veml3328_init_input_device(drv_data);
	if (ret) {
		SENSOR_ERR("Input device init failed, ret=%d\n", ret);
		goto err_init_input_device;
	}

	hrtimer_init(&drv_data->light_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	drv_data->light_poll_delay = ns_to_ktime(DEFAULT_DELAY_MS * NSEC_PER_MSEC);
	drv_data->light_timer.function = light_timer_func;

	drv_data->light_wq = create_singlethread_workqueue("veml3328_light_wq");
	if (!drv_data->light_wq) {
		SENSOR_ERR("Can't create workqueue\n");
		ret = -ENOMEM;
		goto err_create_singlethread_workqueue;
	}

	INIT_WORK(&drv_data->light_work, light_work_func);

	mutex_init(&drv_data->control_mutex);

	/* set sysfs for light sensor */
	ret = sensors_register(&drv_data->ls_dev, drv_data, sensor_attrs, MODULE_NAME);
	if (ret < 0) {
		SENSOR_ERR("Sensor registration failed, ret=%d\n", ret);
		goto err_sensors_register;
	}

	drv_data->als_enable = 0;
	SENSOR_INFO("Probe success!\n");

	return ret;

err_sensors_register:
	mutex_destroy(&drv_data->control_mutex);
	destroy_workqueue(drv_data->light_wq);
err_create_singlethread_workqueue:
sensors_remove_symlink(&drv_data->input_dev->dev.kobj, drv_data->input_dev->name);
input_unregister_device(drv_data->input_dev);
err_init_input_device:
err_init_registers:
err_device_id_check:
	kfree(drv_data);
	return ret;
}

static const struct i2c_device_id veml3328_i2c_id[] = {
	{CHIP_NAME, 0},
	{}
};

#ifdef CONFIG_OF
  static struct of_device_id veml3328_match_table[] = {
		  { .compatible = "capella,veml3328",},
		  { },
  };
#else
  #define veml3328_match_table NULL
#endif

static struct i2c_driver veml3328_driver = {
	.id_table = veml3328_i2c_id,
	.probe = veml3328_probe,
	.driver = {
		.name = CHIP_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(veml3328_match_table),
	},
};

static int __init veml3328_init(void)
{
	return i2c_add_driver(&veml3328_driver);
}

static void __exit veml3328_exit(void)
{
	i2c_del_driver(&veml3328_driver);
}

module_init(veml3328_init);
module_exit(veml3328_exit);

MODULE_AUTHOR("Samsung Electronics");
MODULE_DESCRIPTION("Ambient light sensor driver for capella veml3328");
MODULE_LICENSE("GPL");
