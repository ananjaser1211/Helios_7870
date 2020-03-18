/*
 * Copyright (c) Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/lcd.h>
#include <linux/backlight.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/module.h>
#include <video/mipi_display.h>
#include <linux/i2c.h>
#include <linux/pwm.h>

#include "../dsim.h"
#include "dsim_panel.h"

#include "ltl101al06_param.h"

#define PANEL_STATE_SUSPENED	0
#define PANEL_STATE_RESUMED	1
#define PANEL_STATE_SUSPENDING	2

struct lcd_info {
	unsigned int			connected;
	unsigned int			brightness;
	unsigned int			state;

	struct lcd_device		*ld;
	struct backlight_device		*bd;

	struct dsim_device		*dsim;
	struct mutex			lock;

	struct pinctrl			*pins;
	struct pinctrl_state		*pins_state[2];

	struct pwm_device		*pwm;
	unsigned int			pwm_period;
	unsigned int			pwm_min;
	unsigned int			pwm_max;
	unsigned int			pwm_outdoor;

	struct i2c_client		*tc358764_client;
	unsigned int			panel_power_gpio;
	unsigned int			panel_pwm_gpio;
};

static int tc358764_array_write(struct lcd_info *lcd, u16 addr, u32 w_data)
{
	int ret = 0;
	char buf[6] = {0, };

	//I2C Format
	//addr [15:8]:addr [7:0]:data[7:0]:data[15:8]:data[23:16]:data[31:24]

	//addr [7:0] addr[15:8]
	buf[0] = (u8)(addr >> 8) & 0xff;
	buf[1] = (u8)addr & 0xff;

	//data
	buf[2] = w_data & 0xff;
	buf[3] = (w_data >> 8) & 0xff;
	buf[4] = (w_data >> 16) & 0xff;
	buf[5] = (w_data >> 24) & 0xff;

	ret = i2c_smbus_write_i2c_block_data(lcd->tc358764_client, buf[0], 5, &buf[1]);
	if (ret < 0)
		dev_info(&lcd->ld->dev, "%s: fail. %d, %4x, %8x\n", __func__, ret, addr, w_data);

	return 0;
}

static int pinctrl_enable(struct lcd_info *lcd, int enable)
{
	struct device *dev = &lcd->ld->dev;
	int ret = 0;

	if (!IS_ERR_OR_NULL(lcd->pins_state[enable])) {
		ret = pinctrl_select_state(lcd->pins, lcd->pins_state[enable]);
		if (ret < 0) {
			dev_err(dev, "%s: pinctrl_select_state for %s\n", __func__, enable ? "on" : "off");
			return ret;
		}
	}

	return ret;
}

static int dsim_panel_set_brightness(struct lcd_info *lcd, int force)
{
	int ret = 0;
	unsigned int duty = 0;

	mutex_lock(&lcd->lock);

	lcd->brightness = lcd->bd->props.brightness;

	if (!force && lcd->state != PANEL_STATE_RESUMED) {
		dev_info(&lcd->ld->dev, "%s: panel is not active state\n", __func__);
		goto exit;
	}

	duty = brightness_table[lcd->brightness];

	pwm_config(lcd->pwm, duty, lcd->pwm_period);

	dev_info(&lcd->ld->dev, "%s: brightness: %3d, duty: %d\n", __func__, lcd->brightness, duty);

	lcd->current_bl = lcd->bl;
exit:
	mutex_unlock(&lcd->lock);

	return ret;
}

static int panel_get_brightness(struct backlight_device *bd)
{
	struct lcd_info *lcd = bl_get_data(bd);

	return brightness_table[lcd->brightness];
}

static int panel_set_brightness(struct backlight_device *bd)
{
	int ret = 0;
	struct lcd_info *lcd = bl_get_data(bd);

	if (lcd->state == PANEL_STATE_RESUMED) {
		ret = dsim_panel_set_brightness(lcd, 0);
		if (ret < 0)
			dev_info(&lcd->ld->dev, "%s: failed to set brightness\n", __func__);
	}

	return ret;
}

static const struct backlight_ops panel_backlight_ops = {
	.get_brightness = panel_get_brightness,
	.update_status = panel_set_brightness,
};


static int ltl101al06_exit(struct lcd_info *lcd)
{
	int ret = 0;

	dev_info(&lcd->ld->dev, "%s\n", __func__);

	pwm_disable(lcd->pwm);
	pinctrl_enable(lcd, 0);

	return ret;
}

static int ltl101al06_displayon(struct lcd_info *lcd)
{
	int ret = 0;

	dev_info(&lcd->ld->dev, "%s\n", __func__);
	/*Before pwm_enable, pwm_config should be called*/
	dsim_panel_set_brightness(lcd, 1);
	pwm_enable(lcd->pwm);
	msleep(200);
	pinctrl_enable(lcd, 1);

	return ret;
}

static int ltl101al06_init(struct lcd_info *lcd)
{
	int ret = 0;

	dev_info(&lcd->ld->dev, "%s\n", __func__);

//	msleep(300); //?? internal PLL boosting time...

	ret = gpio_request_one(lcd->panel_power_gpio, GPIOF_OUT_INIT_HIGH, "BLIC_ON");
	gpio_free(lcd->panel_power_gpio);

	//TC358764_65XBG_Tv12p_ParameterSetting_SS_1280x800_noMSF_SEC_151211.xls

	//TC358764/65XBG DSI Basic Parameters.  Following 10 setting should be pefromed in LP mode
	tc358764_array_write(lcd, 0x013C,	0x00050006);
	tc358764_array_write(lcd, 0x0114,	0x00000004);
	tc358764_array_write(lcd, 0x0164,	0x00000004);
	tc358764_array_write(lcd, 0x0168,	0x00000004);
	tc358764_array_write(lcd, 0x016C,	0x00000004);
	tc358764_array_write(lcd, 0x0170,	0x00000004);
	tc358764_array_write(lcd, 0x0134,	0x0000001F);
	tc358764_array_write(lcd, 0x0210,	0x0000001F);
	tc358764_array_write(lcd, 0x0104,	0x00000001);
	tc358764_array_write(lcd, 0x0204,	0x00000001);

	//TC358764/65XBG Timing and mode setting (LP or HS)
	tc358764_array_write(lcd, 0x0450,	0x03F00120);
	tc358764_array_write(lcd, 0x0454,    0x00580032);
	tc358764_array_write(lcd, 0x0458,    0x00590500);
	tc358764_array_write(lcd, 0x045C,	0x00420006);
	tc358764_array_write(lcd, 0x0460,	0x00440320);
	tc358764_array_write(lcd, 0x0464,	0x00000001);
	tc358764_array_write(lcd, 0x04A0,	0x00448006);
	usleep_range(1000, 1100);	//More than 100us
	tc358764_array_write(lcd, 0x04A0,	0x00048006);
	tc358764_array_write(lcd, 0x0504,	0x00000004);

	//TC358764/65XBG LVDS Color mapping setting (LP or HS)
	tc358764_array_write(lcd, 0x0480,	0x03020100);
	tc358764_array_write(lcd, 0x0484,	0x08050704);
	tc358764_array_write(lcd, 0x0488,	0x0F0E0A09);
	tc358764_array_write(lcd, 0x048C,	0x100D0C0B);
	tc358764_array_write(lcd, 0x0490,	0x12111716);
	tc358764_array_write(lcd, 0x0494,	0x1B151413);
	tc358764_array_write(lcd, 0x0498,	0x061A1918);

	//TC358764/65XBG LVDS enable (LP or HS)
	tc358764_array_write(lcd, 0x049C,	0x00000001);

	return ret;
}

static int tc358764_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	struct lcd_info *lcd = NULL;
	int ret = 0;

	if (id && id->driver_data)
		lcd = (struct lcd_info *)id->driver_data;

	if (!lcd) {
		dsim_err("%s: failed to find driver_data for lcd\n", __func__);
		ret = -EINVAL;
		goto exit;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_info(&lcd->ld->dev, "%s: need I2C_FUNC_I2C\n", __func__);
		ret = -ENODEV;
		goto exit;
	}

	i2c_set_clientdata(client, lcd);
	lcd->tc358764_client = client;
	lcd->panel_power_gpio = of_get_gpio(client->dev.of_node, 0);
	lcd->panel_pwm_gpio = of_get_gpio(client->dev.of_node, 1);

	dev_info(&lcd->ld->dev, "%s: %s %s\n", __func__, dev_name(&client->adapter->dev), of_node_full_name(client->dev.of_node));

exit:
	return ret;
}

static struct i2c_device_id tc358764_id[] = {
	{"tc358764", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, tc358764_id);

static const struct of_device_id tc358764_i2c_dt_ids[] = {
	{ .compatible = "i2c,tc358764" },
	{ }
};

MODULE_DEVICE_TABLE(of, tc358764_i2c_dt_ids);

static struct i2c_driver tc358764_i2c_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "tc358764",
		.of_match_table	= of_match_ptr(tc358764_i2c_dt_ids),
	},
	.id_table = tc358764_id,
	.probe = tc358764_probe,
};

static int pwm_probe(struct lcd_info *lcd)
{
	struct device_node *np_lcd;
	struct device_node *np_pwm;
	struct platform_device *pdev;
	u32 pwm_id = -1;
	int ret = 0;

	np_lcd = of_find_node_with_property(NULL, "lcd_info");
	np_lcd = of_parse_phandle(np_lcd, "lcd_info", 0);
	pdev = of_platform_device_create(np_lcd, NULL, lcd->dsim->dev);

	lcd->pins = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(lcd->pins))
		dev_info(&lcd->ld->dev, "%s: devm_pinctrl_get fail\n", __func__);
	else {
		lcd->pins_state[0] = pinctrl_lookup_state(lcd->pins, "pwm_off");
		lcd->pins_state[1] = pinctrl_lookup_state(lcd->pins, "pwm_on");
		if (IS_ERR_OR_NULL(lcd->pins_state[0]) || IS_ERR_OR_NULL(lcd->pins_state[1])) {
			dev_info(&lcd->ld->dev, "%s: pinctrl_lookup_state fail\n", __func__);
			lcd->pins_state[0] = lcd->pins_state[1] = NULL;
		}
	}

	np_pwm = of_parse_phandle(np_lcd, "pwm_info", 0);
	if (!np_pwm) {
		dev_info(&lcd->ld->dev, "%s: %s node does not exist!!!\n", __func__, "pwm_info");
		ret = -ENODEV;
	}

	of_property_read_u32(np_pwm, "pwm_id", &pwm_id);
	of_property_read_u32(np_pwm, "duty_period", &lcd->pwm_period);
	of_property_read_u32(np_pwm, "duty_min", &lcd->pwm_min);
	of_property_read_u32(np_pwm, "duty_max", &lcd->pwm_max);
	of_property_read_u32(np_pwm, "duty_outdoor", &lcd->pwm_outdoor);

	dev_info(&lcd->ld->dev, "%s: id: %d duty_period: %d duty_min: %d max: %d outdoor: %d\n",
		__func__, pwm_id, lcd->pwm_period, lcd->pwm_min, lcd->pwm_max, lcd->pwm_outdoor);

	lcd->pwm = pwm_request(pwm_id, "lcd_pwm");
	if (IS_ERR(lcd->pwm)) {
		dev_info(&lcd->ld->dev, "%s: error : setting fail : %d\n", __func__, ret);
		ret = -EFAULT;
	}

	/*Before pwm_enable, pwm_config should be called*/
	dsim_panel_set_brightness(lcd, 1);
	pwm_enable(lcd->pwm);

	return 0;
}

static int ltl101al06_probe(struct lcd_info *lcd)
{
	int ret = 0;
	struct panel_private *priv = &lcd->dsim->priv;

	dev_info(&lcd->ld->dev, "+ %s\n", __func__);

	lcd->bd->props.max_brightness = EXTEND_BRIGHTNESS;
	lcd->bd->props.brightness = UI_DEFAULT_BRIGHTNESS;

	lcd->state = PANEL_STATE_RESUMED;

	lcd->current_hbm = 0;

	/* there is nothing to read from lcd */
	priv->lcdconnected = lcd->connected = 1;

	ret = pwm_probe(lcd);
	if (ret < 0)
		dev_info(&lcd->ld->dev, "%s: add_PWM_driver fail.\n", __func__);

	tc358764_id->driver_data = (kernel_ulong_t)lcd;
	ret = i2c_add_driver(&tc358764_i2c_driver);
	if (ret < 0)
		dev_info(&lcd->ld->dev, "%s: add_i2c_driver fail.\n", __func__);

	dev_info(&lcd->ld->dev, "- %s\n", __func__);

	return 0;
}

static ssize_t lcd_type_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	sprintf(buf, "SMD_LTL101AL06\n");

	return strlen(buf);
}

static DEVICE_ATTR(lcd_type, 0444, lcd_type_show, NULL);

static struct attribute *lcd_sysfs_attributes[] = {
	&dev_attr_lcd_type.attr,
	NULL,
};

static const struct attribute_group lcd_sysfs_attr_group = {
	.attrs = lcd_sysfs_attributes,
};

static void lcd_init_sysfs(struct lcd_info *lcd)
{
	int ret = 0;

	ret = sysfs_create_group(&lcd->ld->dev.kobj, &lcd_sysfs_attr_group);
	if (ret < 0)
		dev_info(&lcd->ld->dev, "failed to add lcd sysfs\n");
}


static int dsim_panel_probe(struct dsim_device *dsim)
{
	int ret = 0;
	struct lcd_info *lcd;

	dsim->priv.par = lcd = kzalloc(sizeof(struct lcd_info), GFP_KERNEL);
	if (!lcd) {
		pr_err("%s: failed to allocate for lcd\n", __func__);
		ret = -ENOMEM;
		goto probe_err;
	}

	lcd->ld = lcd_device_register("panel", dsim->dev, lcd, NULL);
	if (IS_ERR(lcd->ld)) {
		pr_err("%s: failed to register lcd device\n", __func__);
		ret = PTR_ERR(lcd->ld);
		goto probe_err;
	}

	lcd->bd = backlight_device_register("panel", dsim->dev, lcd, &panel_backlight_ops, NULL);
	if (IS_ERR(lcd->bd)) {
		pr_err("%s: failed to register backlight device\n", __func__);
		ret = PTR_ERR(lcd->bd);
		goto probe_err;
	}

	mutex_init(&lcd->lock);

	lcd->dsim = dsim;
	ret = ltl101al06_probe(lcd);
	if (ret < 0)
		dev_info(&lcd->ld->dev, "%s: failed to probe panel\n", __func__);

	lcd_init_sysfs(lcd);

	dev_info(&lcd->ld->dev, "%s: %s: done\n", kbasename(__FILE__), __func__);
probe_err:
	return ret;
}

static int dsim_panel_displayon(struct dsim_device *dsim)
{
	struct lcd_info *lcd = dsim->priv.par;

	dev_info(&lcd->ld->dev, "+ %s: %d\n", __func__, lcd->state);

	if (lcd->state == PANEL_STATE_SUSPENED)
		ltl101al06_init(lcd);

	ltl101al06_displayon(lcd);

	mutex_lock(&lcd->lock);
	lcd->state = PANEL_STATE_RESUMED;
	mutex_unlock(&lcd->lock);

	dev_info(&lcd->ld->dev, "- %s: %d, %d\n", __func__, lcd->state, lcd->connected);

	return 0;
}

static int dsim_panel_suspend(struct dsim_device *dsim)
{
	struct lcd_info *lcd = dsim->priv.par;

	dev_info(&lcd->ld->dev, "+ %s: %d\n", __func__, lcd->state);

	if (lcd->state == PANEL_STATE_SUSPENED)
		goto exit;

	mutex_lock(&lcd->lock);
	lcd->state = PANEL_STATE_SUSPENDING;
	mutex_unlock(&lcd->lock);

	ltl101al06_exit(lcd);

	mutex_lock(&lcd->lock);
	lcd->state = PANEL_STATE_SUSPENED;
	mutex_unlock(&lcd->lock);

	dev_info(&lcd->ld->dev, "- %s: %d, %d\n", __func__, lcd->state, lcd->connected);

exit:
	return 0;
}

struct mipi_dsim_lcd_driver ltl101al06_mipi_lcd_driver = {
	.name		= "ltl101al06",
	.probe		= dsim_panel_probe,
	.displayon	= dsim_panel_displayon,
	.suspend	= dsim_panel_suspend,
};

