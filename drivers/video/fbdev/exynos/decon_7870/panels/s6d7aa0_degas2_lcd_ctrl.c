/*
 * Copyright (c) Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/lcd.h>
#include <linux/backlight.h>
#include <linux/of_device.h>
#include <video/mipi_display.h>

#include "../decon.h"
#include "../dsim.h"
#include "dsim_panel.h"

#include "s6d7aa0_degas2_param.h"

#if defined(CONFIG_EXYNOS_DECON_MDNIE)
#include "mdnie.h"
#include "mdnie_lite_table_degas2.h"
#endif

#define PANEL_STATE_SUSPENED	0
#define PANEL_STATE_RESUMED	1
#define PANEL_STATE_SUSPENDING	2

#define S6D7AA0_ID_REG		0xDA
#define S6D7AA0_ID_LEN		3
#define BRIGHTNESS_REG		0x51

struct lcd_info {
	unsigned int			connected;
	unsigned int			brightness;
	unsigned int			state;

	struct lcd_device		*ld;
	struct backlight_device		*bd;

	union {
		struct {
			u8		reserved;
			u8		id[S6D7AA0_ID_LEN];
		};
		u32			value;
	} id_info;

	struct dsim_device		*dsim;
	struct mutex			lock;
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

#if defined(CONFIG_SEC_FACTORY) || defined(CONFIG_EXYNOS_DECON_MDNIE)
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

#if defined(CONFIG_EXYNOS_DECON_MDNIE)
static int dsim_write_set(struct lcd_info *lcd, struct lcd_seq_info *seq, u32 num)
{
	int ret = 0, i;

	for (i = 0; i < num; i++) {
		if (seq[i].cmd) {
			ret = dsim_write_hl_data(lcd, seq[i].cmd, seq[i].len);
			if (ret < 0) {
				dev_info(&lcd->ld->dev, "%s: %dth fail\n", __func__, i);
				return ret;
			}
		}
		if (seq[i].sleep)
			usleep_range(seq[i].sleep * 1000, seq[i].sleep * 1100);
	}
	return ret;
}
#endif

static int dsim_panel_set_brightness(struct lcd_info *lcd, int force)
{
	int ret = 0;
	unsigned char bl_reg[2] = {0, };

	mutex_lock(&lcd->lock);

	lcd->brightness = lcd->bd->props.brightness;

	if (!force && lcd->state != PANEL_STATE_RESUMED) {
		dev_info(&lcd->ld->dev, "%s: panel is not active state\n", __func__);
		goto exit;
	}

	bl_reg[0] = BRIGHTNESS_REG;
	bl_reg[1] = brightness_table[lcd->brightness];

	if (dsim_write_hl_data(lcd, bl_reg, ARRAY_SIZE(bl_reg)) < 0)
		dev_info(&lcd->ld->dev, "%s: failed to write brightness cmd.\n", __func__);

	dev_info(&lcd->ld->dev, "%s: brightness: %3d, %3d(%2x)\n", __func__,
		lcd->brightness, bl_reg[1], bl_reg[1]);
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

static int s6d7aa0_read_init_info(struct lcd_info *lcd)
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
static int s6d7aa0_read_id(struct lcd_info *lcd)
{
	struct panel_private *priv = &lcd->dsim->priv;
	int i, ret = 0;
	struct decon_device *decon = get_decon_drvdata(0);
	static char *LDI_BIT_DESC_ID[BITS_PER_BYTE * S6D7AA0_ID_LEN] = {
		[0 ... 23] = "ID Read Fail",
	};

	dev_info(&lcd->ld->dev, "%s\n", __func__);

	lcd->id_info.value = 0;
	priv->lcdconnected = lcd->connected = lcdtype ? 1 : 0;

	for (i = 0; i < S6D7AA0_ID_LEN; i++) {
		ret = dsim_read_hl_data(lcd, S6D7AA0_ID_REG + i, 1, &lcd->id_info.id[i]);
		if (ret < 0)
			break;
	}

	if (ret < 0 || !lcd->id_info.value) {
		priv->lcdconnected = lcd->connected = 0;
		dev_info(&lcd->ld->dev, "%s: connected lcd is invalid\n", __func__);

		if (!lcdtype && decon)
			decon_abd_save_bit(&decon->abd, BITS_PER_BYTE * S6D7AA0_ID_LEN, cpu_to_be32(lcd->id_info.value), LDI_BIT_DESC_ID);
	}

	dev_info(&lcd->ld->dev, "%s: %x\n", __func__, cpu_to_be32(lcd->id_info.value));

	return ret;
}
#endif

static int s6d7aa0_displayon(struct lcd_info *lcd)
{
	int ret = 0;

	dev_info(&lcd->ld->dev, "%s\n", __func__);

	ret = dsim_write_hl_data(lcd, SEQ_DISPLAY_ON, ARRAY_SIZE(SEQ_DISPLAY_ON));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : DISPLAY_ON\n", __func__);
		goto displayon_err;
	}

	dsim_panel_set_brightness(lcd, 1);


displayon_err:
	return ret;
}

static int s6d7aa0_exit(struct lcd_info *lcd)
{
	int ret = 0;

	dev_info(&lcd->ld->dev, "%s\n", __func__);
	ret = dsim_write_hl_data(lcd, SEQ_DISPLAY_OFF, ARRAY_SIZE(SEQ_DISPLAY_OFF));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : DISPLAY_OFF\n", __func__);
		goto exit_err;
	}

	msleep(35);

	ret = dsim_write_hl_data(lcd, SEQ_SLEEP_IN, ARRAY_SIZE(SEQ_SLEEP_IN));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SLEEP_IN\n", __func__);
		goto exit_err;
	}

	msleep(150);

exit_err:
	return ret;
}

static int s6d7aa0_init(struct lcd_info *lcd)
{
	int ret = 0;

	dev_info(&lcd->ld->dev, "%s\n", __func__);

#if defined(CONFIG_SEC_FACTORY)
	s6d7aa0_read_id(lcd);
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
	ret = dsim_write_hl_data(lcd, SEQ_OTP_RELOAD, ARRAY_SIZE(SEQ_OTP_RELOAD));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_OTP_RELOAD\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_BL_ON_CTL, ARRAY_SIZE(SEQ_BL_ON_CTL));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_BL_ON_CTL\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_BC_PARAM_MDNIE, ARRAY_SIZE(SEQ_BC_PARAM_MDNIE));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_BC_PARAM_MDNIE\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_FD_PARAM_MDNIE, ARRAY_SIZE(SEQ_FD_PARAM_MDNIE));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_FD_PARAM_MDNIE\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_FE_PARAM_MDNIE, ARRAY_SIZE(SEQ_FE_PARAM_MDNIE));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_FE_PARAM_MDNIE\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_B3_PARAM, ARRAY_SIZE(SEQ_B3_PARAM));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_B3_PARAM\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_BACKLIGHT_CTL, ARRAY_SIZE(SEQ_BACKLIGHT_CTL));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_BACKLIGHT_CTL\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_PORCH_CTL, ARRAY_SIZE(SEQ_PORCH_CTL));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_PORCH_CTL\n", __func__);
		goto init_exit;
	}
	usleep_range(10000, 11000);
	ret = dsim_write_hl_data(lcd, SEQ_SLEEP_OUT, ARRAY_SIZE(SEQ_SLEEP_OUT));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_SLEEP_OUT\n", __func__);
		goto init_exit;
	}
	msleep(120);
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
	ret = dsim_write_hl_data(lcd, SEQ_TEON_CTL, ARRAY_SIZE(SEQ_TEON_CTL));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_TEON_CTL\n", __func__);
		goto init_exit;
	}

init_exit:
	return ret;
}

static int s6d7aa0_probe(struct lcd_info *lcd)
{
	int ret = 0;

	dev_info(&lcd->ld->dev, "+ %s\n", __func__);

	lcd->bd->props.max_brightness = UI_MAX_BRIGHTNESS;
	lcd->bd->props.brightness = UI_DEFAULT_BRIGHTNESS;

	lcd->state = PANEL_STATE_RESUMED;

	ret = s6d7aa0_read_init_info(lcd);
	if (ret < 0)
		dev_info(&lcd->ld->dev, "%s: failed to init information\n", __func__);

	dev_info(&lcd->ld->dev, "- %s\n", __func__);

	return 0;
}

static ssize_t lcd_type_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);

	sprintf(buf, "BOE_%02X%02X%02X\n", lcd->id_info.id[0], lcd->id_info.id[1], lcd->id_info.id[2]);

	return strlen(buf);
}

static ssize_t window_type_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);

	sprintf(buf, "%02x %02x %02x\n", lcd->id_info.id[0], lcd->id_info.id[1], lcd->id_info.id[2]);

	return strlen(buf);
}

static DEVICE_ATTR(lcd_type, 0444, lcd_type_show, NULL);
static DEVICE_ATTR(window_type, 0444, window_type_show, NULL);

static struct attribute *lcd_sysfs_attributes[] = {
	&dev_attr_lcd_type.attr,
	&dev_attr_window_type.attr,
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

#if defined(CONFIG_EXYNOS_DECON_MDNIE)
static int mdnie_send_seq(struct lcd_info *lcd, struct lcd_seq_info *seq, u32 num)
{
	int ret = 0;

	mutex_lock(&lcd->lock);

	if (lcd->state != PANEL_STATE_RESUMED) {
		dev_info(&lcd->ld->dev, "%s: panel is not active\n", __func__);
		ret = -EIO;
		goto exit;
	}

	ret = dsim_write_set(lcd, seq, num);

exit:
	mutex_unlock(&lcd->lock);

	return ret;
}

static int mdnie_read(struct lcd_info *lcd, u8 addr, u8 *buf, u32 size)
{
	int ret = 0;

	mutex_lock(&lcd->lock);

	if (lcd->state != PANEL_STATE_RESUMED) {
		dev_info(&lcd->ld->dev, "%s: panel is not active\n", __func__);
		ret = -EIO;
		goto exit;
	}

	ret = dsim_read_hl_data(lcd, addr, size, buf);

exit:
	mutex_unlock(&lcd->lock);

	return ret;
}
#endif

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
	ret = s6d7aa0_probe(lcd);
	if (ret < 0)
		dev_info(&lcd->ld->dev, "%s: failed to probe panel\n", __func__);

	lcd_init_sysfs(lcd);

#if defined(CONFIG_EXYNOS_DECON_MDNIE)
	mdnie_register(&lcd->ld->dev, lcd, (mdnie_w)mdnie_send_seq, (mdnie_r)mdnie_read, NULL, &tune_info);
#endif

	dev_info(&lcd->ld->dev, "%s: %s: done\n", kbasename(__FILE__), __func__);

probe_err:
	return ret;
}

static int dsim_panel_displayon(struct dsim_device *dsim)
{
	struct lcd_info *lcd = dsim->priv.par;

	dev_info(&lcd->ld->dev, "+ %s: %d\n", __func__, lcd->state);

	if (lcd->state == PANEL_STATE_SUSPENED)
		s6d7aa0_init(lcd);

	s6d7aa0_displayon(lcd);

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

	s6d7aa0_exit(lcd);

	mutex_lock(&lcd->lock);
	lcd->state = PANEL_STATE_SUSPENED;
	mutex_unlock(&lcd->lock);

	dev_info(&lcd->ld->dev, "- %s: %d, %d\n", __func__, lcd->state, lcd->connected);

exit:
	return 0;
}

struct mipi_dsim_lcd_driver s6d7aa0_mipi_lcd_driver = {
	.name		= "s6d7aa0",
	.probe		= dsim_panel_probe,
	.displayon	= dsim_panel_displayon,
	.suspend	= dsim_panel_suspend,
};

