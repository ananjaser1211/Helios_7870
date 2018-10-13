/*
 * driver/ice40xx_iris IR Led driver
 *
 * Copyright (C) 2012 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/gpio.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/clk.h>
#include <linux/firmware.h>
#include <linux/regulator/consumer.h>
#include <linux/of_gpio.h>
#include <linux/err.h>

#include <linux/ice40_iris.h>

#include <linux/pinctrl/consumer.h>

#include <linux/sec_sysfs.h>

struct ice40_iris_data {
	struct i2c_client		*client;
	struct workqueue_struct		*firmware_dl;
	struct delayed_work		fw_dl;
	const struct firmware		*fw;
	struct mutex			mutex;
	struct pinctrl			*fw_pinctrl;
	struct ice40_iris_platform_data *pdata;
};

#ifdef CONFIG_LEDS_IRIS_IRLED_SUPPORT
static struct ice40_iris_platform_data *g_pdata;
static struct ice40_iris_data *g_data;
extern int ice40_ir_led_pulse_delay(uint32_t delay);
extern int ice40_ir_led_pulse_width(uint32_t width);
#endif

bool ice40_fpga_enabled = false;
static void ice40_fpga_on(struct ice40_iris_data *data);

static void fpga_enable(struct ice40_iris_platform_data *pdata, int enable)
{
	pr_info("%s - %s \n", __func__, enable ?	"on" : "off");

	gpio_set_value(pdata->fpga_clk,
			enable ? GPIO_CON_CLKOUT : GPIO_CON_INPUT);

	gpio_set_value(pdata->rst_n,
			enable ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW);

	usleep_range(1000, 2000);
}

#ifdef CONFIG_OF
static int ice40_iris_parse_dt(struct device *dev,
			struct ice40_iris_platform_data *pdata)
{
	struct device_node *np = dev->of_node;
	int ret;

	ret = of_property_read_u32(np, "ice40,fw_ver", &pdata->fw_ver);
	if (ret < 0) {
		pr_err("[%s]: failed to read fw_ver\n", __func__);
		return ret;
	}
	pdata->spi_si = of_get_named_gpio(np, "ice40,sda-gpio", 0);
	pdata->spi_clk = of_get_named_gpio(np, "ice40,scl-gpio", 0);
	pdata->cresetb = of_get_named_gpio(np, "ice40,cresetb", 0);
	pdata->cdone = of_get_named_gpio(np, "ice40,cdone", 0);
	pdata->rst_n = of_get_named_gpio(np, "ice40,reset_n", 0);
	pdata->fpga_clk = of_get_named_gpio(np, "ice40,fpga_clk", 0);
#if defined(CONFIG_LEDS_ICE40XX_POWER_CONTROL)
	pdata->gpio_iris_1p2_en = of_get_named_gpio(np, "ice40,gpio_iris_1p2_en", 0);

	if (!gpio_is_valid(pdata->gpio_iris_1p2_en)) {
		pr_err("failed to get gpio_iris_1p2_en\n");
		return 0;
	} else {
		ret = gpio_request(pdata->gpio_iris_1p2_en, "gpio_iris_1p2_en");

		if (ret) {
			pr_err("Failed to requeset gpio_iris_1p2_en\n");
			return ret;
		}

		gpio_direction_output(pdata->gpio_iris_1p2_en, GPIO_LEVEL_HIGH);
		usleep_range(1000, 2000);

		gpio_free(pdata->gpio_iris_1p2_en);
	}
#endif
	return 0;
}
#else
static int ice40_iris_parse_dt(struct device *dev,
			struct ice40_iris_platform_data *pdata)
{
	return -ENODEV;
}
#endif

static int iris_pinctrl_configure(struct ice40_iris_data *data, int active)
{
	struct pinctrl_state *set_state;
	int retval;

	if (active) {
		set_state = pinctrl_lookup_state(data->fw_pinctrl,
					"ice40_iris_fw_ready");
		if (IS_ERR(set_state)) {
			pr_info("cannot get ts pinctrl active state\n");
			return PTR_ERR(set_state);
		}
	} else {
		pr_info("not support inactive state\n");
		return 0;
	}
	retval = pinctrl_select_state(data->fw_pinctrl, set_state);
	if (retval) {
		pr_info("cannot get ts pinctrl active state\n");
		return retval;
	}

	return 0;
}

static int ice40_iris_config(struct ice40_iris_data *data)
{
	struct ice40_iris_platform_data *pdata = data->pdata;
	int rc;

	pr_info("ice40_fw_ver[%d] spi_si[%d] spi_clk[%d] cresetb[%d] cdone[%d] rst_n[%d]\n",\
			pdata->fw_ver, pdata->spi_si, pdata->spi_clk,\
			pdata->cresetb, pdata->cdone, pdata->rst_n);

	iris_pinctrl_configure(data, 1);
	gpio_direction_output(pdata->spi_si, GPIO_LEVEL_LOW);
	gpio_direction_output(pdata->spi_clk, GPIO_LEVEL_LOW);
	rc = gpio_request(pdata->cresetb, "iris_creset");
	if (rc < 0) {
		pr_err("%s: cresetb error : %d\n", __func__, rc);
		return rc;
	}
	gpio_direction_output(pdata->cresetb, GPIO_LEVEL_HIGH);
	rc = gpio_request(pdata->rst_n, "iris_rst_n");
	if (rc < 0) {
		pr_err("%s: rst_n error : %d\n", __func__, rc);
		return rc;
	}
	gpio_direction_output(pdata->rst_n, GPIO_LEVEL_LOW);
	rc = gpio_request(pdata->cdone, "iris_cdone");
	if (rc < 0) {
		pr_err("%s: cdone error : %d\n", __func__, rc);
		return rc;
	}

	return 0;
}

/*
 * Send ice40 fpga firmware data thougth spi communication
 */
static int ice40_fpga_send_firmware_data(
		struct ice40_iris_platform_data *pdata, const u8 *fw_data, int len)
{
	unsigned int i, j;
	int cdone = 0;
	unsigned char spibit;

	cdone = gpio_get_value(pdata->cdone);
	pr_info("%s check f/w loading state[%d]\n", __func__, cdone);

	for (i = 0; i < len; i++){
		spibit = fw_data[i];
		for (j = 0; j < 8; j++){
			gpio_set_value_cansleep(pdata->spi_clk,
						GPIO_LEVEL_LOW);

			gpio_set_value_cansleep(pdata->spi_si,
					spibit & 0x80 ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW);

			gpio_set_value_cansleep(pdata->spi_clk,
						GPIO_LEVEL_HIGH);
			spibit = spibit<<1;
		}
	}

	gpio_set_value_cansleep(pdata->spi_si, GPIO_LEVEL_HIGH);
	for (i = 0; i < 200; i++){
		gpio_set_value_cansleep(pdata->spi_clk, GPIO_LEVEL_LOW);
		gpio_set_value_cansleep(pdata->spi_clk, GPIO_LEVEL_HIGH);
	}
	usleep_range(1000, 1300);

	cdone = gpio_get_value(pdata->cdone);
	pr_info("%s check f/w loading state[%d]\n", __func__, cdone);

	return cdone;
}

static int ice40_fpga_fimrware_update_start(
		struct ice40_iris_data *data, const u8 *fw_data, int len)
{
	struct ice40_iris_platform_data *pdata = data->pdata;
	int retry = 0, ret_fw = 0;

	pr_info("%s\n", __func__);
	iris_pinctrl_configure(data, 1);

	fpga_enable(pdata, FPGA_DISABLE);
	gpio_direction_output(pdata->spi_si, GPIO_LEVEL_LOW);
	gpio_direction_output(pdata->spi_clk, GPIO_LEVEL_LOW);

	do {
		gpio_set_value_cansleep(pdata->cresetb, GPIO_LEVEL_LOW);
		usleep_range(30, 50);

		gpio_set_value_cansleep(pdata->cresetb, GPIO_LEVEL_HIGH);
		usleep_range(1000, 1300);

		ret_fw = ice40_fpga_send_firmware_data(pdata, fw_data, len);
		usleep_range(50, 70);
		if (ret_fw) {
			pr_info("%s : FPGA firmware update success \n", __func__);
			break;
		}
	} while (retry++ < FIRMWARE_MAX_RETRY);

	return 0;
}

void ice40_fpga_firmware_update(struct ice40_iris_data *data)
{
	struct ice40_iris_platform_data *pdata = data->pdata;
	struct i2c_client *client = data->client;

	switch (pdata->fw_ver) {
	case 1:
		pr_info("%s[%d] fw_ver %d\n", __func__,
				__LINE__, pdata->fw_ver);
		if (request_firmware(&data->fw,
				"ice40xx/ice40_fpga_iris_V01.fw", &client->dev)) {
			pr_err("%s: Can't open firmware file\n", __func__);
		} else {
			ice40_fpga_fimrware_update_start(data, data->fw->data,
					data->fw->size);
			release_firmware(data->fw);
		}
		break;
	default:
		pr_err("[%s] Not supported [fw_ver = %d]\n",
				__func__, pdata->fw_ver);
		break;
	}
	usleep_range(10000, 12000);
}

static ssize_t ice40_fpga_fw_update_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t size)
{
	struct ice40_iris_data *data = dev_get_drvdata(dev);
	struct file *fp = NULL;
	long fsize = 0, nread = 0;
	const u8 *buff = 0;
	char fw_path[SEC_FPGA_MAX_FW_PATH];
	int locate, ret;
	mm_segment_t old_fs = get_fs();

	pr_info("%s\n", __func__);

	ret = sscanf(buf, "%1d", &locate);
	if (!ret) {
		pr_err("[%s] force select extSdCard\n", __func__);
		locate = 0;
	}

	old_fs = get_fs();
	set_fs(get_ds());

	if (locate) {
		snprintf(fw_path, SEC_FPGA_MAX_FW_PATH,
				"/sdcard/%s", SEC_FPGA_FW_FILENAME);
	} else {
		snprintf(fw_path, SEC_FPGA_MAX_FW_PATH,
				"/extSdCard/%s", SEC_FPGA_FW_FILENAME);
	}

	fp = filp_open(fw_path, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		pr_err("file %s open error\n", fw_path);
		goto err_open;
	}

	fsize = fp->f_path.dentry->d_inode->i_size;
	pr_info("fpga firmware size: %ld\n", fsize);

	buff = kzalloc((size_t)fsize, GFP_KERNEL);
	if (!buff) {
		pr_err("fail to alloc buffer for fw\n");
		goto err_alloc;
	}

	nread = vfs_read(fp, (char __user *)buff, fsize, &fp->f_pos);
	if (nread != fsize) {
		pr_err("fail to read file %s (nread = %ld)\n",
				fw_path, nread);
		goto err_fw_size;
	}

	ice40_fpga_fimrware_update_start(data, (unsigned char *)buff, fsize);

err_fw_size:
	kfree(buff);
err_alloc:
	filp_close(fp, NULL);
err_open:
	set_fs(old_fs);

	return size;
}

static ssize_t ice40_fpga_fw_update_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return strlen(buf);
}

static void fw_work(struct work_struct *work)
{
	struct ice40_iris_data *data =
		container_of(work, struct ice40_iris_data, fw_dl.work);

	ice40_fpga_firmware_update(data);
}

static ssize_t fpga_i2c_write_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct ice40_iris_data *data = dev_get_drvdata(dev);
	int _addr, _data;
	int ret;

	if (!ice40_fpga_enabled)
		ice40_fpga_on(data);

	mutex_lock(&data->mutex);

	sscanf(buf, "%1d %3d", &_addr, &_data);
	ret = i2c_smbus_write_byte_data(data->client, _addr, _data);
	if (ret < 0) {
		pr_info("%s iris i2c write failed %d\n", __func__, ret);
	}

	ret = i2c_smbus_read_byte_data(data->client, _addr);
	pr_info("%s iris_write data %d\n", __func__, ret);

	mutex_unlock(&data->mutex);

	return count;
}

static ssize_t fpga_i2c_read_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct ice40_iris_data *data = dev_get_drvdata(dev);
	char *bufp = buf;
	u8 read_val;

	if (!ice40_fpga_enabled)
		ice40_fpga_on(data);

	mutex_lock(&data->mutex);

	read_val = i2c_smbus_read_byte_data(data->client, 0x00);
	bufp += snprintf(bufp, SNPRINT_BUF_SIZE, "0x00 read val %d\n", read_val);
	read_val = i2c_smbus_read_byte_data(data->client, 0x01);
	bufp += snprintf(bufp, SNPRINT_BUF_SIZE, "0x01 read val %d\n", read_val);
	read_val = i2c_smbus_read_byte_data(data->client, 0x02);
	bufp += snprintf(bufp, SNPRINT_BUF_SIZE, "0x02 read val %d\n", read_val);
	read_val = i2c_smbus_read_byte_data(data->client, 0x03);
	bufp += snprintf(bufp, SNPRINT_BUF_SIZE, "0x03 read val %d\n", read_val);

	mutex_unlock(&data->mutex);

	return strlen(buf);
}

#ifdef CONFIG_LEDS_IRIS_IRLED_SUPPORT
unsigned int fpga_reg[4]={0x00,0x32,0x00,0xC8}; //Td = 5.0ms, Tp = 20.0ms

static void ice40_fpga_on(struct ice40_iris_data *data)
{
	int i;
	int _addr, _data;
	int ret;

	fpga_enable(data->pdata,FPGA_ENABLE);

	mutex_lock(&data->mutex);

	for (i = 0; i < 4; i++) { 
		_addr = i;
		_data = fpga_reg[i];
		ret = i2c_smbus_write_byte_data(data->client, _addr, _data);
		if (ret < 0)
			pr_info("%s iris i2c write failed %d\n", __func__, ret);
		else
			pr_info("%s iris i2c write reg[0x%2X], data[0x%2X]\n", __func__, _addr, _data);
	}

	ice40_fpga_enabled = true;
	mutex_unlock(&data->mutex);
}

static void ice40_fpga_off(struct ice40_iris_data *data)
{
	fpga_enable(data->pdata,FPGA_DISABLE);
	ice40_fpga_enabled = false;
}

static ssize_t ice40_fpga_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	char *bufp = buf;

	bufp += snprintf(bufp, SNPRINT_BUF_SIZE,
		ice40_fpga_enabled ? "Enabled\n" : "Disabled\n");

	return strlen(buf);
}

static ssize_t ice40_fpga_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	char *after;
	struct ice40_iris_data *data = dev_get_drvdata(dev);
	unsigned long status = simple_strtoul(buf, &after, 10);

	if (status == 1) {
		pr_info("[%s] : On", __func__);
		ice40_fpga_on(data);
	} else if (status == 0) {
		pr_info("[%s] : Off", __func__);	
		ice40_fpga_off(data);
	}
	return size;
}

static ssize_t ice40_fpga_register_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct ice40_iris_data *data = dev_get_drvdata(dev);
	char *bufp = buf;
	u8 read_val;

	if (!ice40_fpga_enabled)
		ice40_fpga_on(data);

	mutex_lock(&data->mutex);

	read_val = i2c_smbus_read_byte_data(data->client, 0x00);
	bufp += snprintf(bufp, SNPRINT_BUF_SIZE, "0x00 : %d\n", read_val);
	read_val = i2c_smbus_read_byte_data(data->client, 0x01);
	bufp += snprintf(bufp, SNPRINT_BUF_SIZE, "0x01 : %d\n", read_val);
	read_val = i2c_smbus_read_byte_data(data->client, 0x02);
	bufp += snprintf(bufp, SNPRINT_BUF_SIZE, "0x02 : %d\n", read_val);
	read_val = i2c_smbus_read_byte_data(data->client, 0x03);
	bufp += snprintf(bufp, SNPRINT_BUF_SIZE, "0x03 : %d\n", read_val);

	mutex_unlock(&data->mutex);

	return strlen(buf);
}

static ssize_t ice40_fpga_register_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	struct ice40_iris_data *data = dev_get_drvdata(dev);
	int _addr, _data;
	int temp_td, temp_tp;
	int ret;
	int i;

	if (!ice40_fpga_enabled)
		ice40_fpga_on(data);

	mutex_lock(&data->mutex);

	sscanf(buf, "%4d %4d", &temp_td, &temp_tp); //Td(*0.1ms) Tp(*0.1ms) 

	if (temp_td > FPGA_TD_TP_MAX_TIME)
		temp_td = FPGA_TD_TP_MAX_TIME;
	fpga_reg[0] = temp_td >> 8;
	fpga_reg[1] = temp_td & 0xFF;

	if (temp_tp > FPGA_TD_TP_MAX_TIME)
		temp_tp = FPGA_TD_TP_MAX_TIME;
	fpga_reg[2] = temp_tp >> 8;
	fpga_reg[3] = temp_tp & 0xFF;

	for (i = 0; i < 4; i++) { 
		_addr = i;
		_data = fpga_reg[i];
		ret = i2c_smbus_write_byte_data(data->client, _addr, _data);
		if (ret < 0)
			pr_info("%s iris i2c write failed %d\n", __func__, ret);
		else
			pr_info("%s iris i2c write reg[0x%2X], data[0x%2X]\n", __func__, _addr, _data);
	}

	mutex_unlock(&data->mutex);

	return size;
}

int ice40_ir_led_pulse_delay(uint32_t delay) /* Td */
{
	/*         |-- Tp --|      */
	/* __ Td __|        |_____ */

	struct i2c_client *client = g_data->client;
	int ret = 0;
	unsigned char _data[4];
	unsigned int temp;

	if (!ice40_fpga_enabled)
		ice40_fpga_on(g_data);

	pr_info("%s : set delay = %d ms\n", __func__, delay / 10);

	mutex_lock(&g_data->mutex);

	if (delay > FPGA_TD_TP_MAX_TIME)
		delay = FPGA_TD_TP_MAX_TIME;

	temp = delay;
	client->addr = 0x6c;

	/* register set td high*/
	_data[0] = 0x00;
	_data[1] = (unsigned char) temp >> 8;
	ret = i2c_smbus_write_byte_data(g_data->client, _data[0], _data[1]);
	if(ret < 0)
		pr_err("set ice40_ir_led_pulse_delay Td high: i2c error");

	/* register set td low*/
	_data[2] = 0x01;
	_data[3] = (unsigned char) temp & 0xFF;
	ret = i2c_smbus_write_byte_data(g_data->client, _data[2], _data[3]);
	if(ret < 0)
		pr_err("set ice40_ir_led_pulse_delay Td low: i2c error");

	mutex_unlock(&g_data->mutex);

	return ret;
}

int ice40_ir_led_pulse_width(uint32_t width)  /* Tp */
{
	/*         |-- Tp --|      */
	/* __ Td __|        |_____ */

	struct i2c_client *client = g_data->client;
	int ret = 0;
	unsigned char _data[4];
	unsigned int temp;

	if (!ice40_fpga_enabled)
		ice40_fpga_on(g_data);

	pr_info("%s : set width = %d ms\n", __func__, width / 10);

	mutex_lock(&g_data->mutex);

	if (width > FPGA_TD_TP_MAX_TIME)
		width = FPGA_TD_TP_MAX_TIME;

	temp = width;
	client->addr = 0x6c;

	/* register set tp high*/
	_data[0] = 0x02;
	_data[1] = (unsigned char) temp >> 8;
	ret = i2c_smbus_write_byte_data(g_data->client, _data[0], _data[1]);
	if(ret < 0)
		pr_err("set ice40_ir_led_pulse_width Tp high: i2c error");

	/* register set tp low*/
	_data[2] = 0x03;
	_data[3] = (unsigned char) temp & 0xFF;
	ret = i2c_smbus_write_byte_data(g_data->client, _data[2], _data[3]);
	if(ret < 0)
		pr_err("set ice40_ir_led_pulse_width Tp low: i2c error");

	mutex_unlock(&g_data->mutex);

	return ret;
}
#endif

static struct device_attribute ice40_attrs[] = {
	__ATTR(ice40_fpga_fw_update, S_IRUGO|S_IWUSR|S_IWGRP,
			ice40_fpga_fw_update_show, ice40_fpga_fw_update_store),
	__ATTR(fpga_i2c_check, S_IRUGO|S_IWUSR|S_IWGRP,
			fpga_i2c_read_show, fpga_i2c_write_store),
	__ATTR(ice40_fpga_register, S_IRUGO|S_IWUSR|S_IWGRP,
			ice40_fpga_register_show, ice40_fpga_register_store),
	__ATTR(ice40_fpga_enable, S_IRUGO|S_IWUSR|S_IWGRP,
			ice40_fpga_enable_show, ice40_fpga_enable_store),
};

static int ice40_iris_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct ice40_iris_data *data;
	struct ice40_iris_platform_data *pdata;
	struct device *ice40_iris_dev;
	int i, error;

	pr_info("%s probe!\n", __func__);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		error = -EIO;
		goto err_return;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		pr_err("%s: i2c functionality check error\n", __func__);
		dev_err(&client->dev, "need I2C_FUNC_SMBUS_BYTE_DATA.\n");
		error = -EIO;
		goto err_return;
	}

	if (client->dev.of_node) {
		pdata = devm_kzalloc(&client->dev,
				sizeof(*pdata), GFP_KERNEL);
		if (!pdata) {
			dev_err(&client->dev, "Failed to allocate memory\n");
			error = -ENOMEM;
			goto err_return;
		}
		error = ice40_iris_parse_dt(&client->dev, pdata);
		if (error) {
			goto err_data_mem;
		}
	} else {
		pdata = client->dev.platform_data;
	}
#ifdef CONFIG_LEDS_IRIS_IRLED_SUPPORT
	g_pdata = pdata;
#endif
	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (NULL == data) {
		pr_err("Failed to data allocate %s\n", __func__);
		error = -ENOMEM;
		goto err_data_mem;
	}

	i2c_set_clientdata(client, data);
	data->client = client;
	data->pdata = pdata;
	mutex_init(&data->mutex);
#ifdef CONFIG_LEDS_IRIS_IRLED_SUPPORT
		g_data = data;
#endif
	data->fw_pinctrl = devm_pinctrl_get(&client->dev);
	if (IS_ERR(data->fw_pinctrl)) {
		if (PTR_ERR(data->fw_pinctrl) == -EPROBE_DEFER) {
			error = -EPROBE_DEFER;
			goto err_free_mem;
		}

		pr_info("Target does not use pinctrl\n");
		data->fw_pinctrl = NULL;
	}

	error = ice40_iris_config(data);
	if (error < 0) {
		pr_err("ice40_iris_config failed\n");
		goto err_pinctrl_put;
	}

	ice40_iris_dev = sec_device_create(data, "sec_iris");
	if (IS_ERR(ice40_iris_dev)) {
		pr_err("Failed to create ice40_iris_dev device in sec_ir\n");
		error = -ENODEV;
		goto err_pinctrl_put;
	}

	/* sysfs entries */
	for (i = 0; i < ARRAY_SIZE(ice40_attrs); i++) {
		error = device_create_file(ice40_iris_dev, &ice40_attrs[i]);
		if (error < 0) {
			pr_err("Failed to create device file(%s)!\n", ice40_attrs[i].attr.name);
			goto err_dev_destroy;
		}
	}

	/* Create dedicated thread so that
	 the delay of our work does not affect others */
	data->firmware_dl =
		create_singlethread_workqueue("ice40_firmware_dl");
	INIT_DELAYED_WORK(&data->fw_dl, fw_work);
	/* min 20ms is needed */
	queue_delayed_work(data->firmware_dl, &data->fw_dl, msecs_to_jiffies(20));

	/*ice40_fpga_firmware_update(data);*/

	pr_info("%s complete[%d]\n", __func__, __LINE__);

	return 0;

err_dev_destroy:
	sec_device_destroy(ice40_iris_dev->devt);
err_pinctrl_put:
	devm_pinctrl_put(data->fw_pinctrl);	
err_free_mem:
	mutex_destroy(&data->mutex);
	kfree(data);
err_data_mem:
	devm_kfree(&client->dev, pdata);
err_return:
	return error;
}

static int ice40_iris_remove(struct i2c_client *client)
{
	struct ice40_iris_data *data = i2c_get_clientdata(client);

	i2c_set_clientdata(client, NULL);

	destroy_workqueue(data->firmware_dl);
	mutex_destroy(&data->mutex);
	kfree(data);
	return 0;
}

static const struct i2c_device_id ice40_iris_id[] = {
	{"ice40_iris", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, barcode_id);

#ifdef CONFIG_OF
static struct of_device_id ice40_iris_match_table[] = {
	{ .compatible = "ice40_iris",},
	{ },
};
#else
#define ice40_iris_match_table	NULL
#endif

static struct i2c_driver ice40_i2c_driver = {
	.driver = {
		.name = "ice40_iris",
		.owner = THIS_MODULE,
		.of_match_table = ice40_iris_match_table,
	},
	.probe = ice40_iris_probe,
	.remove = ice40_iris_remove,
	.id_table = ice40_iris_id,
};

static int __init ice40_iris_init(void)
{
	return i2c_add_driver(&ice40_i2c_driver);
}
late_initcall(ice40_iris_init);

static void __exit ice40_iris_exit(void)
{
	i2c_del_driver(&ice40_i2c_driver);
}
module_exit(ice40_iris_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SEC IRIS");
