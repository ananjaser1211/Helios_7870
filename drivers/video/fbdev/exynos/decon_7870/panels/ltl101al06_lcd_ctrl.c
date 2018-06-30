/*
 * drivers/video/decon_7870/panels/ltl101al06_lcd_ctrl.c
 *
 * Samsung SoC MIPI LCD CONTROL functions
 *
 * Copyright (c) 2015 Samsung Electronics
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

#define POWER_IS_ON(pwr)			(pwr <= FB_BLANK_NORMAL)
#define LEVEL_IS_HBM(brightness)		(brightness == EXTEND_BRIGHTNESS)

struct i2c_client *tc358764_client;

unsigned int			panel_power_gpio;
unsigned int			panel_pwm_gpio;

struct lcd_info {
	unsigned int			bl;
	unsigned int			brightness;
	unsigned int			acl_enable;
	unsigned int			siop_enable;
	unsigned int			current_acl;
	unsigned int			current_bl;
	unsigned int			current_hbm;
	unsigned int			state;

	struct lcd_device		*ld;
	struct backlight_device		*bd;

	int				temperature;
	unsigned int			temperature_index;

	unsigned char			dump_info[3];

	struct dsim_device		*dsim;
	struct mutex			lock;

	struct pinctrl			*pins;
	struct pinctrl_state	*pins_state[2];

	struct pwm_device		*pwm;
	unsigned int			pwm_period;
	unsigned int			pwm_min;
	unsigned int			pwm_max;
	unsigned int			pwm_outdoor;
};

static int tc358764_array_write(u16 addr, u32 w_data)
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

	ret = i2c_smbus_write_i2c_block_data(tc358764_client, buf[0], 5, &buf[1]);
	if (ret < 0)
		dsim_err("%s: error : setting fail : %d\n", __func__, ret);

	return 0;
}

static int pinctrl_enable(struct lcd_info *lcd, int enable)
{
	struct device *dev = &lcd->ld->dev;
	int ret = 0;

	if (!IS_ERR_OR_NULL(lcd->pins_state[enable])) {
		ret = pinctrl_select_state(lcd->pins, lcd->pins_state[enable]);
		if (ret) {
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

	lcd->bl = lcd->brightness;

	if (!force && lcd->state != PANEL_STATE_RESUMED) {
		dev_info(&lcd->ld->dev, "%s: panel is not active state\n", __func__);
		goto exit;
	}

	lcd->bl = (lcd->bl > UI_MAX_BRIGHTNESS) ? UI_MAX_BRIGHTNESS : lcd->bl;

	if (LEVEL_IS_HBM(lcd->brightness))
		duty = lcd->pwm_outdoor;
	else
		duty = lcd->bl * (lcd->pwm_max - lcd->pwm_min) / 255 + lcd->pwm_min;

	if (duty <= lcd->pwm_min)
		duty = 0;	// duty must set over 0.7 percent.

	pwm_config(lcd->pwm, duty, lcd->pwm_period);

	dev_info(&lcd->ld->dev, "%s: brightness: %d, bl: %d, duty: %d/%d\n", __func__, lcd->brightness, lcd->bl, duty, lcd->pwm_period);

	lcd->current_bl = lcd->bl;
exit:
	mutex_unlock(&lcd->lock);

	return ret;
}

static int panel_get_brightness(struct backlight_device *bd)
{
	return bd->props.brightness;
}

static int panel_set_brightness(struct backlight_device *bd)
{
	int ret = 0;
	struct lcd_info *lcd = bl_get_data(bd);

	if (lcd->state == PANEL_STATE_RESUMED) {
		ret = dsim_panel_set_brightness(lcd, 0);
		if (ret) {
			dev_err(&lcd->ld->dev, "%s: failed to set brightness\n", __func__);
			goto exit;
		}
	}

exit:
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

	ret = gpio_request_one(panel_power_gpio, GPIOF_OUT_INIT_HIGH, "BLIC_ON");
	gpio_free(panel_power_gpio);

	//TC358764_65XBG_Tv12p_ParameterSetting_SS_1280x800_noMSF_SEC_151211.xls

	//TC358764/65XBG DSI Basic Parameters.  Following 10 setting should be pefromed in LP mode
	tc358764_array_write(0x013C,	0x00050006);
	tc358764_array_write(0x0114,	0x00000004);
	tc358764_array_write(0x0164,	0x00000004);
	tc358764_array_write(0x0168,	0x00000004);
	tc358764_array_write(0x016C,	0x00000004);
	tc358764_array_write(0x0170,	0x00000004);
	tc358764_array_write(0x0134,	0x0000001F);
	tc358764_array_write(0x0210,	0x0000001F);
	tc358764_array_write(0x0104,	0x00000001);
	tc358764_array_write(0x0204,	0x00000001);

	//TC358764/65XBG Timing and mode setting (LP or HS)
	tc358764_array_write(0x0450,	0x03F00120);
	tc358764_array_write(0x0454,    0x00580032);
	tc358764_array_write(0x0458,    0x00590500);
	tc358764_array_write(0x045C,	0x00420006);
	tc358764_array_write(0x0460,	0x00440320);
	tc358764_array_write(0x0464,	0x00000001);
	tc358764_array_write(0x04A0,	0x00448006);
	usleep_range(1000, 1100);	//More than 100us
	tc358764_array_write(0x04A0,	0x00048006);
	tc358764_array_write(0x0504,	0x00000004);

	//TC358764/65XBG LVDS Color mapping setting (LP or HS)
	tc358764_array_write(0x0480,	0x03020100);
	tc358764_array_write(0x0484,	0x08050704);
	tc358764_array_write(0x0488,	0x0F0E0A09);
	tc358764_array_write(0x048C,	0x100D0C0B);
	tc358764_array_write(0x0490,	0x12111716);
	tc358764_array_write(0x0494,	0x1B151413);
	tc358764_array_write(0x0498,	0x061A1918);

	//TC358764/65XBG LVDS enable (LP or HS)
	tc358764_array_write(0x049C,	0x00000001);

	return ret;
}

static int tc358764_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int ret = 0;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dsim_err("%s: fail.\n", __func__);
		ret = -ENODEV;
		goto err_i2c;
	}

	tc358764_client = client;
	panel_power_gpio = of_get_gpio(client->dev.of_node, 0);
	panel_pwm_gpio = of_get_gpio(client->dev.of_node, 1);

err_i2c:
	return ret;
}

static struct i2c_device_id tc358764_id[] = {
	{"tc358764", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, tc358764_id);

static struct of_device_id tc358764_i2c_dt_ids[] = {
	{ .compatible = "tc358764,i2c" },
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

static int pwm_probe(struct lcd_info *lcd, struct device_node *parent)
{
	struct device_node *np = NULL;
	u32 pwm_id = -1;
	int ret = 0;

	np = of_parse_phandle(parent, "pwm_info", 0);

	if (!np) {
		dev_info(&lcd->ld->dev, "%s: %s node does not exist!!!\n", __func__, "pwm_info");
		ret = -ENODEV;
	}

	of_property_read_u32(np, "pwm_id", &pwm_id);
	of_property_read_u32(np, "duty_period", &lcd->pwm_period);
	of_property_read_u32(np, "duty_min", &lcd->pwm_min);
	of_property_read_u32(np, "duty_max", &lcd->pwm_max);
	of_property_read_u32(np, "duty_outdoor", &lcd->pwm_outdoor);

	dev_info(&lcd->ld->dev, "%s: id: %d duty_period: %d duty_min: %d max: %d outdoor: %d\n",
		__func__, pwm_id, lcd->pwm_period, lcd->pwm_min, lcd->pwm_max, lcd->pwm_outdoor);

	lcd->pwm = pwm_request(pwm_id, "lcd_pwm");
	if (IS_ERR(lcd->pwm)) {
		dev_err(&lcd->ld->dev, "%s: error : setting fail : %d\n", __func__, ret);
		ret = -EFAULT;
	}

	/*Before pwm_enable, pwm_config should be called*/
	dsim_panel_set_brightness(lcd, 1);
	pwm_enable(lcd->pwm);

	return 0;
}

static int ltl101al06_probe(struct dsim_device *dsim)
{
	int ret = 0;
	struct panel_private *priv = &dsim->priv;
	struct lcd_info *lcd = dsim->priv.par;
	struct device_node *np;
	struct platform_device *pdev;

	dev_info(&lcd->ld->dev, "%s: was called\n", __func__);

	priv->lcdConnected = PANEL_CONNECTED;

	lcd->bd->props.max_brightness = EXTEND_BRIGHTNESS;
	lcd->bd->props.brightness = UI_DEFAULT_BRIGHTNESS;

	lcd->dsim = dsim;
	lcd->state = PANEL_STATE_RESUMED;

	lcd->temperature = NORMAL_TEMPERATURE;
	lcd->acl_enable = 0;
	lcd->current_acl = 0;
	lcd->siop_enable = 0;
	lcd->current_hbm = 0;

	if (lcdtype == 0) {
		priv->lcdConnected = PANEL_DISCONNEDTED;
		dev_err(&lcd->ld->dev, "dsim : %s lcd was not connected\n", __func__);
		goto exit;
	}

	np = of_find_node_with_property(NULL, "lcd_info");
	np = of_parse_phandle(np, "lcd_info", 0);
	pdev = of_platform_device_create(np, NULL, dsim->dev);

	lcd->pins = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(lcd->pins)) {
		pr_err("%s: devm_pinctrl_get fail\n", __func__);
		goto exit;
	}

	lcd->pins_state[0] = pinctrl_lookup_state(lcd->pins, "pwm_off");
	lcd->pins_state[1] = pinctrl_lookup_state(lcd->pins, "pwm_on");
	if (IS_ERR_OR_NULL(lcd->pins_state[0]) || IS_ERR_OR_NULL(lcd->pins_state[1])) {
		pr_err("%s: pinctrl_lookup_state fail\n", __func__);
		goto exit;
	}

	ret = i2c_add_driver(&tc358764_i2c_driver);
	if (ret) {
		pr_err("%s: add_i2c_driver fail.\n", __func__);
		goto exit;
	}

	ret = pwm_probe(lcd, np);
	if (ret) {
		pr_err("%s: add_PWM_driver fail.\n", __func__);
		goto exit;
	}
	dev_info(&lcd->ld->dev, "%s: done\n", __func__);
exit:
	return ret;
}


static ssize_t lcd_type_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	sprintf(buf, "SMD_LTL101AL06\n");

	return strlen(buf);
}

static ssize_t siop_enable_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);

	sprintf(buf, "%u\n", lcd->siop_enable);

	return strlen(buf);
}

static ssize_t siop_enable_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);
	int value;
	int rc;

	rc = kstrtoul(buf, (unsigned int)0, (unsigned long *)&value);
	if (rc < 0)
		return rc;
	else {
		if (lcd->siop_enable != value) {
			dev_info(dev, "%s: %d, %d\n", __func__, lcd->siop_enable, value);
			mutex_lock(&lcd->lock);
			lcd->siop_enable = value;
			mutex_unlock(&lcd->lock);
			if (lcd->state == PANEL_STATE_RESUMED)
				dsim_panel_set_brightness(lcd, 1);
		}
	}
	return size;
}

static ssize_t power_reduce_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);

	sprintf(buf, "%u\n", lcd->acl_enable);

	return strlen(buf);
}

static ssize_t power_reduce_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);
	int value;
	int rc;

	rc = kstrtoul(buf, (unsigned int)0, (unsigned long *)&value);

	if (rc < 0)
		return rc;
	else {
		if (lcd->acl_enable != value) {
			dev_info(dev, "%s: %d, %d\n", __func__, lcd->acl_enable, value);
			mutex_lock(&lcd->lock);
			lcd->acl_enable = value;
			mutex_unlock(&lcd->lock);
			if (lcd->state == PANEL_STATE_RESUMED)
				dsim_panel_set_brightness(lcd, 1);
		}
	}
	return size;
}

static ssize_t temperature_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	char temp[] = "-20, -19, 0, 1\n";

	strcat(buf, temp);
	return strlen(buf);
}

static ssize_t temperature_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int value, rc = 0;

	rc = kstrtoint(buf, 10, &value);

	return size;
}

static DEVICE_ATTR(lcd_type, 0444, lcd_type_show, NULL);
static DEVICE_ATTR(siop_enable, 0664, siop_enable_show, siop_enable_store);
static DEVICE_ATTR(power_reduce, 0664, power_reduce_show, power_reduce_store);
static DEVICE_ATTR(temperature, 0664, temperature_show, temperature_store);

static struct attribute *lcd_sysfs_attributes[] = {
	&dev_attr_lcd_type.attr,
	&dev_attr_siop_enable.attr,
	&dev_attr_power_reduce.attr,
	&dev_attr_temperature.attr,
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
		dev_err(&lcd->ld->dev, "failed to add lcd sysfs\n");
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

	dsim->lcd = lcd->ld = lcd_device_register("panel", dsim->dev, lcd, NULL);
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

	ret = ltl101al06_probe(dsim);
	if (ret) {
		dev_err(&lcd->ld->dev, "%s: failed to probe panel\n", __func__);
		goto probe_err;
	}

#if defined(CONFIG_EXYNOS_DECON_LCD_SYSFS)
	lcd_init_sysfs(lcd);
#endif

	dev_info(&lcd->ld->dev, "%s: %s: done\n", kbasename(__FILE__), __func__);
probe_err:
	return ret;
}

static int dsim_panel_displayon(struct dsim_device *dsim)
{
	struct lcd_info *lcd = dsim->priv.par;
	struct panel_private *priv = &lcd->dsim->priv;
	int ret = 0;

	dev_info(&lcd->ld->dev, "+%s\n", __func__);

	if (lcd->state == PANEL_STATE_SUSPENED) {
		ret = ltl101al06_init(lcd);
		if (ret) {
			dev_info(&lcd->ld->dev, "%s: failed to panel init\n", __func__);
			goto displayon_err;
		}
	}

	ret = ltl101al06_displayon(lcd);
	if (ret) {
		dev_info(&lcd->ld->dev, "%s: failed to panel display on\n", __func__);
		goto displayon_err;
	}

displayon_err:
	mutex_lock(&lcd->lock);
	lcd->state = PANEL_STATE_RESUMED;
	mutex_unlock(&lcd->lock);

	dev_info(&lcd->ld->dev, "-%s: %d\n", __func__, priv->lcdConnected);

	return ret;
}

static int dsim_panel_suspend(struct dsim_device *dsim)
{
	struct lcd_info *lcd = dsim->priv.par;
	struct panel_private *priv = &lcd->dsim->priv;
	int ret = 0;

	dev_info(&lcd->ld->dev, "+%s\n", __func__);

	if (lcd->state == PANEL_STATE_SUSPENED)
		goto suspend_err;

	lcd->state = PANEL_STATE_SUSPENDING;

	ret = ltl101al06_exit(lcd);
	if (ret) {
		dev_info(&lcd->ld->dev, "%s: failed to panel exit\n", __func__);
		goto suspend_err;
	}

suspend_err:
	mutex_lock(&lcd->lock);
	lcd->state = PANEL_STATE_SUSPENED;
	mutex_unlock(&lcd->lock);

	dev_info(&lcd->ld->dev, "-%s: %d\n", __func__, priv->lcdConnected);

	return ret;
}

struct mipi_dsim_lcd_driver ltl101al06_mipi_lcd_driver = {
	.probe		= dsim_panel_probe,
	.displayon	= dsim_panel_displayon,
	.suspend	= dsim_panel_suspend,
};

