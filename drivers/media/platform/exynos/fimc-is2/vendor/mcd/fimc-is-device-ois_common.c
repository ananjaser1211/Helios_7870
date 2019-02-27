/*
 * Samsung Exynos5 SoC series FIMC-IS OIS driver
 *
 * exynos5 fimc-is core functions
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/vmalloc.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>
#include <exynos-fimc-is-sensor.h>
#include "fimc-is-core.h"
#include "fimc-is-interface.h"
#include "fimc-is-sec-define.h"
#include "fimc-is-device-ischain.h"
#include "fimc-is-dt.h"
#include "fimc-is-device-ois.h"
#include "fimc-is-vender-specific.h"
#ifdef CONFIG_AF_HOST_CONTROL
#include "fimc-is-device-af.h"
#endif
#include <linux/pinctrl/pinctrl.h>
#if defined(CONFIG_OIS_USE_BU24219)
#include "fimc-is-device-ois_bu24219.h"
#endif

#define FIMC_IS_OIS_DEV_NAME		"exynos-fimc-is-ois"

struct fimc_is_ois_info ois_minfo;
struct fimc_is_ois_info ois_pinfo;
struct fimc_is_ois_info ois_uinfo;
struct fimc_is_ois_exif ois_exif_data;

int fimc_is_ois_reset(struct i2c_client *client)
{
	struct fimc_is_device_ois *ois_device;
	struct fimc_is_ois_gpio *gpio;
	int gpio_pin;

	info("%s : E\n", __func__);

	if (!client) {
		pr_info("%s: client is null\n", __func__);
		return -ENODEV;
	}

	ois_device = i2c_get_clientdata(client);
	gpio = &ois_device->gpio;
	gpio_pin = gpio->reset;

	gpio_direction_output(gpio_pin, 0);
	msleep(2);
	gpio_direction_output(gpio_pin, 1);
	msleep(2);

	gpio_free(gpio_pin);
	pr_info("%s : gpio free\n", __func__);

	return 0;
}

int fimc_is_ois_i2c_config(struct i2c_client *client, bool onoff)
{
	/* don't use i2c configration on DICO AP*/
	return 0;
}

int fimc_is_ois_i2c_read(struct i2c_client *client, u16 addr, u8 *data)
{
	int err;
	u8 txbuf[2], rxbuf[1];
	struct i2c_msg msg[2];

	*data = 0;
	txbuf[0] = (addr & 0xff00) >> 8;
	txbuf[1] = (addr & 0xff);

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = 2;
	msg[0].buf = txbuf;

	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 1;
	msg[1].buf = rxbuf;

	err = i2c_transfer(client->adapter, msg, 2);
	if (unlikely(err != 2)) {
		err("%s: register read fail err = %d\n", __func__, err);
		return -EIO;
	}

	*data = rxbuf[0];
	return 0;
}

int fimc_is_ois_i2c_write(struct i2c_client *client ,u16 addr, u8 data)
{
	int retries = I2C_RETRY_COUNT;
	int ret = 0, err = 0;
	u8 buf[3] = {0,};
	struct i2c_msg msg = {
		.addr	= client->addr,
		.flags	= 0,
		.len	= 3,
		.buf	= buf,
	};

	buf[0] = (addr & 0xff00) >> 8;
	buf[1] = addr & 0xff;
	buf[2] = data;

#ifdef OIS_I2C_DEBUG
	info("%s : Slave Addr(0x%02X), W(0x%02X%02X %02X)\n",
		__func__, client->addr, buf[0], buf[1], buf[2]);
#endif

	do {
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (likely(ret == 1))
		break;

		usleep_range(10000,11000);
		err = ret;
	} while (--retries > 0);

	/* Retry occured */
	if (unlikely(retries < I2C_RETRY_COUNT)) {
		err("i2c_write: error %d, write (%04X, %04X), retry %d\n",
			err, addr, data, I2C_RETRY_COUNT - retries);
	}

	if (unlikely(ret != 1)) {
		err("I2C does not work\n\n");
		return -EIO;
	}

	return 0;
}

u8 i2c_write_buf[5000] = {0,};
int fimc_is_ois_i2c_write_multi(struct i2c_client *client ,u16 addr, u8 *data, size_t size)
{
	int retries = I2C_RETRY_COUNT;
	int ret = 0, err = 0;
	ulong i = 0;
	struct i2c_msg msg = {
		.addr	= client->addr,
		.flags	= 0,
		.len	= size + 2,
		.buf	= i2c_write_buf,
	};

	i2c_write_buf[0] = (addr & 0xFF00) >> 8;
	i2c_write_buf[1] = addr & 0xFF;

	for (i = 0; i < size; i++) {
		i2c_write_buf[i + 2] = *(data + i);
	}
#ifdef OIS_I2C_DEBUG
	info("OISLOG %s : W(0x%02X%02X, start:%02X, end:%02X)\n", __func__,
		i2c_write_buf[0], i2c_write_buf[1], i2c_write_buf[2], i2c_write_buf[i + 1]);
#endif
	do {
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (likely(ret == 1))
		break;

		usleep_range(10000,11000);
		err = ret;
	} while (--retries > 0);

	/* Retry occured */
	if (unlikely(retries < I2C_RETRY_COUNT)) {
		err("i2c_write: error %d, write (%04X, %04X), retry %d\n",
			err, addr, *data, I2C_RETRY_COUNT - retries);
	}

	if (unlikely(ret != 1)) {
		err("I2C does not work\n\n");
		return -EIO;
	}

	return 0;
}

int fimc_is_ois_i2c_read_multi(struct i2c_client *client, u16 addr, u8 *data, size_t size)
{
	int err;
	u8 rxbuf[256], txbuf[2];
	struct i2c_msg msg[2];

	txbuf[0] = (addr & 0xff00) >> 8;
	txbuf[1] = (addr & 0xff);

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = 2;
	msg[0].buf = txbuf;

	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = size;
	msg[1].buf = rxbuf;

	err = i2c_transfer(client->adapter, msg, 2);
	if (unlikely(err != 2)) {
		err("%s: register read fail", __func__);
		return -EIO;
	}

	memcpy(data, rxbuf, size);
	return 0;
}

bool fimc_is_ois_check_reload_fw(struct fimc_is_core *core)
{
	struct fimc_is_device_ois *ois_device;

	if (!core->client1) {
		pr_info("%s: client is null\n", __func__);
		return false;
	}

	ois_device = i2c_get_clientdata(core->client1);

	info(" Is it necessary updating OIS FW ? (%s)",
		ois_device->use_reload_ois_fw? "YES" : "NO");

	return ois_device->use_reload_ois_fw;
}

bool fimc_is_ois_check_sensor(struct fimc_is_core *core)
{
	bool ret = true;

	return ret;
}

int fimc_is_ois_gpio_on(struct fimc_is_core *core)
{
	return fimc_is_ois_gpio_on_impl(core);
}

int fimc_is_ois_gpio_off(struct fimc_is_core *core)
{
	return fimc_is_ois_gpio_off_impl(core);
}

void fimc_is_ois_enable(struct fimc_is_core *core)
{
	info("%s : X\n", __FUNCTION__);
}

int fimc_is_ois_sine_mode(struct fimc_is_core *core, int mode)
{
	int ret = 0;

	ret = fimc_is_ois_sine_mode_impl(core, mode);
	return ret;
}
EXPORT_SYMBOL(fimc_is_ois_sine_mode);

void fimc_is_ois_offset_test(struct fimc_is_core *core, long *raw_data_x, long *raw_data_y)
{
	fimc_is_ois_offset_test_impl( core, raw_data_x, raw_data_y);
}

void fimc_is_ois_get_offset_data(struct fimc_is_core *core, long *raw_data_x, long *raw_data_y)
{
	fimc_is_ois_get_offset_data_impl( core, raw_data_x, raw_data_y);
}

int fimc_is_ois_self_test(struct fimc_is_core *core)
{
	return fimc_is_ois_self_test_impl(core);
}

bool fimc_is_ois_diff_test(struct fimc_is_core *core, int *x_diff, int *y_diff)
{
	return fimc_is_ois_diff_test_impl(core, x_diff, y_diff);
}

u16 fimc_is_ois_calc_checksum(u8 *data, int size)
{
	int i = 0;
	u16 result = 0;

	for(i = 0; i < size; i += 2) {
		result = result + (0xFFFF & (((*(data + i + 1)) << 8) | (*(data + i))));
	}

	return result;
}

void fimc_is_ois_exif_data(struct fimc_is_core *core)
{
/* To do */
	ois_exif_data.error_data = 0;
	ois_exif_data.status_data = 0;
}

int fimc_is_ois_get_exif_data(struct fimc_is_ois_exif **exif_info)
{
	*exif_info = &ois_exif_data;
	return 0;
}

int fimc_is_ois_get_module_version(struct fimc_is_ois_info **minfo)
{
	*minfo = &ois_minfo;
	return 0;
}

int fimc_is_ois_get_phone_version(struct fimc_is_ois_info **pinfo)
{
	*pinfo = &ois_pinfo;
	return 0;
}

int fimc_is_ois_get_user_version(struct fimc_is_ois_info **uinfo)
{
	*uinfo = &ois_uinfo;
	return 0;
}

bool fimc_is_ois_check_fw(struct fimc_is_core *core)
{
	fimc_is_ois_check_fw_impl(core);

	return true;
}

void fimc_is_ois_fw_status(struct fimc_is_core *core)
{
	fimc_is_ois_fw_status_impl(core);

	return;
}

void fimc_is_ois_fw_update(struct fimc_is_core *core)
{
	fimc_is_ois_fw_update_impl(core);

	return;
}

void fimc_is_ois_factory_read_IC_ROM_checksum(struct fimc_is_core *core)
{
	fimc_is_ois_factory_read_IC_ROM_checksum_impl(core);

	return;
}

int fimc_is_ois_mode_change(struct fimc_is_core *core, int mode)
{
	return fimc_is_ois_set_mode_impl(core, mode);
}

int fimc_is_ois_shift_compensation(struct fimc_is_core *core, u32 value)
{
	return fimc_is_ois_set_shift_compensation_impl(core, value);
}

int fimc_is_ois_init(struct fimc_is_core *core)
{
	return fimc_is_ois_init_impl(core);
}

int fimc_is_ois_parse_dt(struct i2c_client *client)
{
	int ret = 0;
	struct fimc_is_device_ois *device = i2c_get_clientdata(client);
	struct fimc_is_ois_gpio *gpio;
	struct device_node *np = client->dev.of_node;

	gpio = &device->gpio;
	device->use_reload_ois_fw =  of_property_read_bool(np, "use_reload_ois_fw");

	ret = of_property_read_u32(np, "ois_reset", &gpio->reset);
	if (ret) {
		err("ois gpio: fail to read, ois_parse_dt\n");
		ret = -ENODEV;
		goto p_err;
	}

	info("[OIS] reload = %d, reset = %d\n",  device->use_reload_ois_fw, gpio->reset);

p_err:
	return ret;
}

static int fimc_is_ois_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct fimc_is_device_ois *device;
	struct fimc_is_core *core;
	struct device *i2c_dev;
	struct pinctrl *pinctrl_i2c = NULL;
	int ret = 0;

	if (fimc_is_dev == NULL) {
		warn("fimc_is_dev is not yet probed");
		client->dev.init_name = FIMC_IS_OIS_DEV_NAME;
		return -EPROBE_DEFER;
	}

	core = (struct fimc_is_core *)dev_get_drvdata(fimc_is_dev);
	if (!core)
		return -EINVAL;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		err("No I2C functionality found\n");
		return -ENODEV;
	}

	device = kzalloc(sizeof(struct fimc_is_device_ois), GFP_KERNEL);
	if (!device) {
		err("fimc_is_device_ois is NULL");
		return -ENOMEM;
	}

	core->client1 = client;
	device->client = client;
	device->ois_hsi2c_status = false;
	core->ois_ver_read = false;

	i2c_set_clientdata(client, device);

	if (client->dev.of_node) {
		ret = fimc_is_ois_parse_dt(client);
		if (ret) {
			err("parsing device tree is fail(%d)", ret);
			return -ENODEV;
		}
	}

	/* Initial i2c pin */
	i2c_dev = client->dev.parent->parent;
	pinctrl_i2c = devm_pinctrl_get_select(i2c_dev, "off_i2c");
	if (IS_ERR_OR_NULL(pinctrl_i2c)) {
		printk(KERN_ERR "%s: Failed to configure i2c pin\n", __func__);
	} else {
		devm_pinctrl_put(pinctrl_i2c);
	}

	return 0;
}

static int fimc_is_ois_remove(struct i2c_client *client)
{
	return 0;
}

static const struct i2c_device_id ois_id[] = {
	{FIMC_IS_OIS_DEV_NAME, 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, ois_id);

#ifdef CONFIG_OF
static struct of_device_id ois_dt_ids[] = {
	{ .compatible = "rohm,ois",},
	{},
};
#endif

static struct i2c_driver ois_i2c_driver = {
	.driver = {
		.name = FIMC_IS_OIS_DEV_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = ois_dt_ids,
#endif
	},
	.probe = fimc_is_ois_probe,
	.remove = fimc_is_ois_remove,
	.id_table = ois_id,
};
module_i2c_driver(ois_i2c_driver);

MODULE_DESCRIPTION("OIS driver for Rohm");
MODULE_AUTHOR("Roen Lee");
MODULE_LICENSE("GPL v2");
