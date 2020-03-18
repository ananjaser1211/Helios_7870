/*
 * Copyright (c) Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/lcd.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/backlight.h>
#include <linux/of_device.h>
#include <video/mipi_display.h>
#include <linux/pwm.h>

#include "../decon.h"
#include "../decon_notify.h"
#include "../dsim.h"
#include "dsim_panel.h"

#include "s6d78a0_qhd_gppiris_param.h"
#include "dd.h"

#define PANEL_STATE_SUSPENED	0
#define PANEL_STATE_RESUMED	1
#define PANEL_STATE_SUSPENDING	2

#define LEVEL_IS_HBM(brightness)		(brightness == EXTEND_BRIGHTNESS)

static DEFINE_SPINLOCK(bl_ctrl_lock);

struct lcd_info {
	unsigned int			connected;
	unsigned int bl;
	unsigned int brightness;
	unsigned int current_bl;
	unsigned int state;
	struct lcd_device *ld;
	struct backlight_device	*bd;
	union {
		struct {
			u8		reserved;
			u8		id[S6D78A0_ID_LEN];
		};
		u32 value;
	} id_info;
	struct dsim_device *dsim;
	struct mutex lock;
	int lux;
	struct notifier_block fb_notif_panel;
	/*ktd3102 backlight*/
	int bl_pin_ctrl;
	int bl_pin_pwm;
};

static int dsim_write_hl_data(struct lcd_info *lcd, const u8 *cmd, u32 cmdsize)
{
	int ret = 0;
	int retry = 2;

	if (!lcd->connected)
		return ret;

try_write:
	if (cmdsize == 1)
		ret = dsim_write_data(lcd->dsim, MIPI_DSI_DCS_SHORT_WRITE, cmd[0], 0);
	else if (cmdsize == 2)
		ret = dsim_write_data(lcd->dsim, MIPI_DSI_DCS_SHORT_WRITE_PARAM, cmd[0], cmd[1]);
	else
		ret = dsim_write_data(lcd->dsim, MIPI_DSI_DCS_LONG_WRITE, (unsigned long)cmd, cmdsize);

	if (ret < 0) {
		if (--retry)
			goto try_write;
		else
			dev_info(&lcd->ld->dev, "%s: fail. %02x, ret: %d\n", __func__, cmd[0], ret);
	}

	return ret;
}

#if defined(CONFIG_SEC_FACTORY)
static int dsim_read_hl_data(struct lcd_info *lcd, u8 addr, u32 size, u8 *buf)
{
	int ret = 0, rx_size = 0;
	int retry = 2;

	if (!lcd->connected)
		return ret;

try_read:
	rx_size = dsim_read_data(lcd->dsim, MIPI_DSI_DCS_READ, (u32)addr, size, buf);
	dev_info(&lcd->ld->dev, "%s: %2d(%2d), %02x, %*ph%s\n", __func__, size, rx_size, addr,
		min_t(u32, min_t(u32, size, rx_size), 5), buf, (rx_size > 5) ? "..." : "");
	if (rx_size != size) {
		if (--retry)
			goto try_read;
		else {
			dev_info(&lcd->ld->dev, "%s: fail. %02x, %d(%d)\n", __func__, addr, size, rx_size);
			ret = -EPERM;
		}
	}

	return ret;
}
#endif

static void ktd3102_set_bl_level(struct lcd_info *lcd)
{
	unsigned long flags;
	int i, bl_ctrl = lcd->bl_pin_ctrl;
	int bl_pwm = lcd->bl_pin_pwm;
	int pulse;

	dev_info(&lcd->ld->dev, "%s, level(%d)\n", __func__, lcd->bl);

	if (lcd->bl == 0) {
		/* power off */
		gpio_set_value(bl_ctrl, 0);
		gpio_set_value(bl_pwm, 0);
		mdelay(3);
		return;
	}

	if (lcd->current_bl == 0) {
		/* TODO : if bl works normally it's unnecessary */
		gpio_set_value(bl_ctrl, 0);
		gpio_set_value(bl_pwm, 0);
		mdelay(3);

		/* power on */
		gpio_set_value(bl_pwm, 1);
		gpio_set_value(bl_ctrl, 1);
		udelay(100);
	}

	pulse = (lcd->current_bl - lcd->bl + EXTEND_TUNE_LEVEL)	% EXTEND_TUNE_LEVEL;
	dev_info(&lcd->ld->dev, "%s: pre lev=%d, cur lev=%d, pulse=%d\n",
			__func__, lcd->current_bl, lcd->bl, pulse);

	spin_lock_irqsave(&bl_ctrl_lock, flags);
	for (i = 0; i < pulse; i++) {
		udelay(2);
		gpio_set_value(bl_ctrl, 0);
		udelay(2);
		gpio_set_value(bl_ctrl, 1);
	}
	spin_unlock_irqrestore(&bl_ctrl_lock, flags);

}

static int ktd3102_backlight_set_brightness(struct lcd_info *lcd, unsigned int brightness)
{
	mutex_lock(&lcd->lock);
	lcd->bl = brightness_table[brightness];
	dev_info(&lcd->ld->dev, "%s: brightness(%d), tune_level(%d)\n",
			__func__, brightness,  lcd->bl);
	if (lcd->current_bl != lcd->bl) {
		ktd3102_set_bl_level(lcd);
		lcd->current_bl = lcd->bl;
	}
	mutex_unlock(&lcd->lock);
	return 0;
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

	lcd->brightness = lcd->bd->props.brightness;

	if (lcd->state == PANEL_STATE_RESUMED) {
		ret = ktd3102_backlight_set_brightness(lcd, lcd->brightness);
		if (ret < 0)
			dev_info(&lcd->ld->dev, "%s: failed to set brightness\n", __func__);
	}

	return ret;
}

static const struct backlight_ops panel_backlight_ops = {
	.get_brightness = panel_get_brightness,
	.update_status = panel_set_brightness,
};

static int s6d78a0_read_init_info(struct lcd_info *lcd)
{
	struct panel_private *priv = &lcd->dsim->priv;

	priv->lcdconnected = lcd->connected = lcdtype ? 1 : 0;

	lcd->id_info.id[0] = (lcdtype & 0xFF0000) >> 16;
	lcd->id_info.id[1] = (lcdtype & 0x00FF00) >> 8;
	lcd->id_info.id[2] = (lcdtype & 0x0000FF) >> 0;

	dev_info(&lcd->ld->dev, "%s: %x\n", __func__, cpu_to_be32(lcd->id_info.value));

	return 0;
}

#if defined(CONFIG_SEC_FACTORY)
static int s6d78a0_read_id(struct lcd_info *lcd)
{
	struct panel_private *priv = &lcd->dsim->priv;
	int i, ret = 0;
	struct decon_device *decon = get_decon_drvdata(0);
	static char *LDI_BIT_DESC_ID[BITS_PER_BYTE * S6D78A0_ID_LEN] = {
		[0 ... 23] = "ID Read Fail",
	};

	lcd->id_info.value = 0;
	priv->lcdconnected = lcd->connected = lcdtype ? 1 : 0;

	for (i = 0; i < S6D78A0_ID_LEN; i++) {
		ret = dsim_read_hl_data(lcd, S6D78A0_ID_REG + i, 1, &lcd->id_info.id[i]);
		if (ret < 0)
			break;
	}

	if (ret < 0 || !lcd->id_info.value) {
		priv->lcdconnected = lcd->connected = 0;
		dev_info(&lcd->ld->dev, "%s: connected lcd is invalid\n", __func__);

		if (!lcdtype && decon)
			decon_abd_save_bit(&decon->abd, BITS_PER_BYTE * S6D78A0_ID_LEN, cpu_to_be32(lcd->id_info.value), LDI_BIT_DESC_ID);
	}

	dev_info(&lcd->ld->dev, "%s: %x\n", __func__, cpu_to_be32(lcd->id_info.value));

	return 0;
}
#endif

static int s6d78a0_displayon(struct lcd_info *lcd)
{
	int ret = 0;

	dev_info(&lcd->ld->dev, "%s\n", __func__);

	ret = dsim_write_hl_data(lcd, SEQ_DISPLAY_ON, ARRAY_SIZE(SEQ_DISPLAY_ON));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : DISPLAY_ON\n", __func__);
		goto displayon_err;
	}

displayon_err:
	return ret;

}

static int s6d78a0_exit(struct lcd_info *lcd)
{
	int ret = 0;

	dev_info(&lcd->ld->dev, "%s\n", __func__);

	ret = dsim_write_hl_data(lcd, SEQ_DISPLAY_OFF, ARRAY_SIZE(SEQ_DISPLAY_OFF));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : DISPLAY_OFF\n", __func__);
		goto exit_err;
	}
	msleep(50);
	ret = dsim_write_hl_data(lcd, SEQ_SLEEP_IN, ARRAY_SIZE(SEQ_SLEEP_IN));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SLEEP_IN\n", __func__);
		goto exit_err;
	}
	msleep(50);

	/*turn backligt gpio down*/

exit_err:
	return ret;
}

static int s6d78a0_init(struct lcd_info *lcd)
{
	int ret = 0;

	dev_info(&lcd->ld->dev, "%s\n", __func__);

#if defined(CONFIG_SEC_FACTORY)
	s6d78a0_read_id(lcd);
#endif

	ret = dsim_write_hl_data(lcd, SEQ_PASSWD1, ARRAY_SIZE(SEQ_PASSWD1));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_PASSWD1\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_PASSWD2, ARRAY_SIZE(SEQ_PASSWD2));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_PASSWD2\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_PASSWD3, ARRAY_SIZE(SEQ_PASSWD3));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_PASSWD3\n", __func__);
		goto init_exit;
	}

	ret = dsim_write_hl_data(lcd, SEQ_SLEEP_OUT, ARRAY_SIZE(SEQ_SLEEP_OUT));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_SLEEP_OUT_1\n", __func__);
		goto init_exit;
	}

	msleep(120);

	ret = dsim_write_hl_data(lcd, SEQ_INTERNAL_CLK, ARRAY_SIZE(SEQ_INTERNAL_CLK));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_INTERNAL_CLK\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_PANEL_PROTECTION, ARRAY_SIZE(SEQ_PANEL_PROTECTION));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_PANEL_PROTECTION\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_INTERNAL_POWER, ARRAY_SIZE(SEQ_INTERNAL_POWER));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_INTERNAL_POWER\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_GOA_TIMING, ARRAY_SIZE(SEQ_GOA_TIMING));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_GOA_TIMING\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_INTERNAL_PORCH, ARRAY_SIZE(SEQ_INTERNAL_PORCH));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_INTERNAL_PORCH\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_SOURCE_CTL, ARRAY_SIZE(SEQ_SOURCE_CTL));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_SOURCE_CTL\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_MIPI_ABNORMAL_DETECT, ARRAY_SIZE(SEQ_MIPI_ABNORMAL_DETECT));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_MIPI_ABNORMAL_DETECT\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_MIPI_RECOVER, ARRAY_SIZE(SEQ_MIPI_RECOVER));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_MIPI_RECOVER\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_GOA_OUTPUT, ARRAY_SIZE(SEQ_GOA_OUTPUT));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_GOA_OUTPUT\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_MEM_DATA_ACCESS, ARRAY_SIZE(SEQ_MEM_DATA_ACCESS));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_MEM_DATA_ACCESS\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_POSITIVE_GAMMA, ARRAY_SIZE(SEQ_POSITIVE_GAMMA));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_POSITIVE_GAMMA\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_NEGATIVE_GAMMA, ARRAY_SIZE(SEQ_NEGATIVE_GAMMA));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_NEGATIVE_GAMMA\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_DISPLAY_ON, ARRAY_SIZE(SEQ_DISPLAY_ON));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_DISPLAY_ON\n", __func__);
		goto init_exit;
	}

	msleep(20);

	ret = dsim_write_hl_data(lcd, SEQ_PASSWD1_LOCK, ARRAY_SIZE(SEQ_PASSWD1_LOCK));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_PASSWD1_LOCK\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_PASSWD2_LOCK, ARRAY_SIZE(SEQ_PASSWD2_LOCK));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_PASSWD2_LOCK\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_PASSWD3_LOCK, ARRAY_SIZE(SEQ_PASSWD3_LOCK));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_PASSWD3_LOCK\n", __func__);
		goto init_exit;
	}

init_exit:
	return ret;
}

static int fb_notifier_callback(struct notifier_block *self,
				unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	struct lcd_info *lcd = NULL;
	int fb_blank;

	switch (event) {
	case FB_EVENT_BLANK:
		break;
	default:
		return NOTIFY_DONE;
	}

	lcd = container_of(self, struct lcd_info, fb_notif_panel);

	fb_blank = *(int *)evdata->data;

	dev_info(&lcd->ld->dev, "%s: %d\n", __func__, fb_blank);

	if (evdata->info->node)
		return NOTIFY_DONE;

	if (fb_blank == FB_BLANK_UNBLANK) {
		mutex_lock(&lcd->lock);
		s6d78a0_displayon(lcd);
		mutex_unlock(&lcd->lock);
	}

	return NOTIFY_DONE;
}


static int ktd3102_parse_dt_gpio(struct device_node *np, const char *prop)
{
	int gpio;

	gpio = of_get_named_gpio(np, prop, 0);
	if (unlikely(gpio < 0)) {
		pr_err("%s: of_get_named_gpio failed: %d\n",
				__func__, gpio);
		return -EINVAL;
	}

	pr_info("%s, get gpio(%d)\n", __func__, gpio);

	return gpio;
}

static int ktd3102_probe(struct lcd_info *lcd, struct device_node *parent)
{
	struct device_node *np = NULL;
	int ret = 0;

	np = of_parse_phandle(parent, "backlight_info", 0);

	if (!np) {
		dev_info(&lcd->ld->dev, "%s: %s node does not exist!!!\n", __func__, "backlight_info");
		ret = -ENODEV;
	}

	lcd->bl_pin_ctrl = ktd3102_parse_dt_gpio(np, "bl-ctrl");
	if (lcd->bl_pin_ctrl < 0) {
		dev_info(&lcd->ld->dev, "%s, failed to parse dt\n", __func__);
		return -EINVAL;
	}

	lcd->bl_pin_pwm = ktd3102_parse_dt_gpio(np, "bl-pwm");
	if (lcd->bl_pin_pwm < 0) {
		dev_info(&lcd->ld->dev, "%s, failed to parse dt\n", __func__);
		return -EINVAL;
	}

	ret = gpio_request(lcd->bl_pin_ctrl, "BL_CTRL");
	if (unlikely(ret < 0)) {
		dev_info(&lcd->ld->dev, "%s, request gpio(%d) failed\n", __func__, lcd->bl_pin_ctrl);
		goto err_bl_gpio_request;
	}

	ret = gpio_request(lcd->bl_pin_pwm, "BL_PWM");
	if (unlikely(ret < 0)) {
		dev_info(&lcd->ld->dev, "%s, request gpio(%d) failed\n", __func__, lcd->bl_pin_pwm);
		goto err_bl_gpio_request;
	}

err_bl_gpio_request:
	return 0;
}

static int s6d78a0_probe(struct lcd_info *lcd)
{
	int ret = 0;
	struct device_node *np;

	dev_info(&lcd->ld->dev, "+ %s\n", __func__);

	lcd->bd->props.max_brightness = EXTEND_BRIGHTNESS;
	lcd->bd->props.brightness = UI_DEFAULT_BRIGHTNESS;

	lcd->state = PANEL_STATE_RESUMED;

	ret = s6d78a0_read_init_info(lcd);
	if (ret < 0)
		dev_info(&lcd->ld->dev, "%s: failed to init information\n", __func__);

	lcd->fb_notif_panel.notifier_call = fb_notifier_callback;
	decon_register_notifier(&lcd->fb_notif_panel);

	np = of_find_node_with_property(NULL, "lcd_info");
	np = of_parse_phandle(np, "lcd_info", 0);

	ret = ktd3102_probe(lcd, np);
	if (ret < 0)
		dev_info(&lcd->ld->dev, "%s: failed to probe panel\n", __func__);

	dev_info(&lcd->ld->dev, "- %s\n", __func__);

	return 0;
}

static ssize_t lcd_type_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);

	sprintf(buf, "BOE_%02X%02X%02X\n", lcd->id_info.id[0], lcd->id_info.id[1], lcd->id_info.id[2]);

	return strlen(buf);
}

static ssize_t window_type_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);

	sprintf(buf, "%02x %02x %02x\n", lcd->id_info.id[0], lcd->id_info.id[1], lcd->id_info.id[2]);

	return strlen(buf);
}

static ssize_t brightness_table_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int i;
	char *pos = buf;

	for (i = 0; i <= EXTEND_BRIGHTNESS; i++)
		pos += sprintf(pos, "%3d %3d\n", i, brightness_table[i]);

	return pos - buf;
}

static DEVICE_ATTR(lcd_type, 0444, lcd_type_show, NULL);
static DEVICE_ATTR(window_type, 0444, window_type_show, NULL);
static DEVICE_ATTR(brightness_table, 0444, brightness_table_show, NULL);

static struct attribute *lcd_sysfs_attributes[] = {
	&dev_attr_lcd_type.attr,
	&dev_attr_window_type.attr,
	&dev_attr_brightness_table.attr,
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

	init_debugfs_backlight(lcd->bd, brightness_table, NULL);
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
	ret = s6d78a0_probe(lcd);
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
		s6d78a0_init(lcd);

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

	s6d78a0_exit(lcd);

	mutex_lock(&lcd->lock);
	lcd->state = PANEL_STATE_SUSPENED;
	mutex_unlock(&lcd->lock);

	dev_info(&lcd->ld->dev, "- %s: %d, %d\n", __func__, lcd->state, lcd->connected);

exit:
	return 0;
}

struct mipi_dsim_lcd_driver s6d78a0_mipi_lcd_driver = {
	.name		= "s6d78a0",
	.probe		= dsim_panel_probe,
	.displayon	= dsim_panel_displayon,
	.suspend	= dsim_panel_suspend,
};

