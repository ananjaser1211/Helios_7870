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

#include "../decon.h"
#include "../dsim.h"
#include "dsim_panel.h"

#include "hx8279d_param.h"
#include "dd.h"

#define PANEL_STATE_SUSPENED	0
#define PANEL_STATE_RESUMED	1
#define PANEL_STATE_SUSPENDING	2

struct lcd_info {
	unsigned int			connected;
	unsigned int			brightness;

	struct lcd_device		*ld;
	struct backlight_device		*bd;

	union {
		struct {
			u8		reserved;
			u8		id[HX8279D_ID_LEN];
		};
		u32			value;
	} id_info;

	struct dsim_device		*dsim;

	unsigned int			state;
	struct mutex			lock;

	unsigned int			pwm_max;
	unsigned int			pwm_outdoor;

	unsigned int			tp_mode;
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

static int dsim_panel_set_brightness(struct lcd_info *lcd, int force)
{
	int ret = 0;
	unsigned char bl_reg[2] = {0xB8, 0x00};

	mutex_lock(&lcd->lock);

	lcd->brightness = lcd->bd->props.brightness;

	if (!force && lcd->state != PANEL_STATE_RESUMED) {
		dev_info(&lcd->ld->dev, "%s: panel is not active state\n", __func__);
		goto exit;
	}

	bl_reg[1] = brightness_table[lcd->brightness];

	ret = dsim_write_hl_data(lcd, SEQ_TABLE_4, ARRAY_SIZE(SEQ_TABLE_4));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_TABLE_04\n", __func__);
		goto exit;
	}

	ret = dsim_write_hl_data(lcd, bl_reg, ARRAY_SIZE(bl_reg));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : bl_reg\n", __func__);
		goto exit;
	}

	dev_info(&lcd->ld->dev, "%s: %03d: %3d (0x%02x)\n", __func__, lcd->brightness, bl_reg[1], bl_reg[1]);

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

static int hx8279d_exit(struct lcd_info *lcd)
{
	int ret = 0;

	dev_info(&lcd->ld->dev, "%s\n", __func__);

	ret = dsim_write_hl_data(lcd, SEQ_TABLE_0, ARRAY_SIZE(SEQ_TABLE_0));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_TABLE_0\n", __func__);
		goto exit_err;
	}

	ret = dsim_write_hl_data(lcd, SEQ_DISPLAY_OFF, ARRAY_SIZE(SEQ_DISPLAY_OFF));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_DISPLAY_OFF\n", __func__);
		goto exit_err;
	}
	msleep(50);

	ret = dsim_write_hl_data(lcd, SEQ_SLEEP_IN, ARRAY_SIZE(SEQ_SLEEP_IN));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_SLEEP_IN\n", __func__);
		goto exit_err;
	}
	msleep(120);

exit_err:
	return ret;
}

static int dsi_write_table(struct lcd_info *lcd, const struct mipi_cmd *table, int size)
{
	int i, table_size, ret = 0;
	const struct mipi_cmd *table_ptr;

	table_ptr = table;
	table_size = size;

	for (i = 0; i < table_size; i++) {
		ret = dsim_write_hl_data(lcd, table_ptr[i].cmd, ARRAY_SIZE(table_ptr[i].cmd));

		if (ret < 0) {
			dev_info(&lcd->ld->dev, "%s: failed to write CMD : 0x%02x, 0x%02x\n", __func__, table_ptr[i].cmd[0], table_ptr[i].cmd[1]);
			goto write_exit;
		}
	}

write_exit:
	return ret;
}

static int hx8279d_read_init_info(struct lcd_info *lcd)
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
static int hx8279d_read_id(struct lcd_info *lcd)
{
	struct panel_private *priv = &lcd->dsim->priv;
	int i, ret = 0;
	char buf = 0;
	struct decon_device *decon = get_decon_drvdata(0);
	static char *LDI_BIT_DESC_ID[BITS_PER_BYTE * HX8279D_ID_LEN] = {
		[0 ... 23] = "ID Read Fail",
	};

	lcd->id_info.value = 0;
	priv->lcdconnected = lcd->connected = lcdtype ? 1 : 0;

	for (i = 0; i < HX8279D_ID_LEN; i++) {
		ret = dsim_read_hl_data(lcd, HX8279D_ID_REG + i, 1, &lcd->id_info.id[i]);
		if (ret < 0)
			goto stop;
	}

	ret = dsim_read_hl_data(lcd, HX8279D_DUAL_REG, 1, &buf);
	if (ret < 0)
		goto stop;

	lcd->id_info.id[2] = buf;

stop:
	if (ret < 0 || !lcd->id_info.value) {
		priv->lcdconnected = lcd->connected = 0;
		dev_info(&lcd->ld->dev, "%s: connected lcd is invalid\n", __func__);

		if (!lcdtype && decon)
			decon_abd_save_bit(&decon->abd, BITS_PER_BYTE * HX8279D_ID_LEN, cpu_to_be32(lcd->id_info.value), LDI_BIT_DESC_ID);
	}

	dev_info(&lcd->ld->dev, "%s: %x\n", __func__, cpu_to_be32(lcd->id_info.value));

	return 0;
}
#endif

static int hx8279d_displayon(struct lcd_info *lcd)
{
	int ret = 0;

	dev_info(&lcd->ld->dev, "%s\n", __func__);

	ret = dsim_write_hl_data(lcd, SEQ_TABLE_0, ARRAY_SIZE(SEQ_TABLE_0));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_TABLE_0\n", __func__);
		goto display_err;
	}

	ret = dsim_write_hl_data(lcd, SEQ_DISPLAY_ON, ARRAY_SIZE(SEQ_DISPLAY_ON));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_DISPLAY_ON\n", __func__);
		goto display_err;
	}

	msleep(40);

	dsim_panel_set_brightness(lcd, 1);

display_err:
	return ret;
}

static int hx8279d_init(struct lcd_info *lcd)
{
	int ret = 0;
	struct dsim_device *dsim = lcd->dsim;
	struct decon_device *decon = NULL;

	decon = (struct decon_device *)dsim->decon;

	dev_info(&lcd->ld->dev, "%s: ++\n", __func__);

	dev_info(&lcd->ld->dev, "%s: porch: real: vclk:%lu hbp:%d hfp:%d hsa:%d vbp:%d vfp:%d vsa:%d\n",
				__func__, clk_get_rate(decon->res.vclk_leaf),
				dsim->lcd_info.hbp, dsim->lcd_info.hfp, dsim->lcd_info.hsa,
				dsim->lcd_info.vbp, dsim->lcd_info.vfp, dsim->lcd_info.vsa);

	ret = dsim_write_hl_data(lcd, SEQ_TABLE_5, ARRAY_SIZE(SEQ_TABLE_5));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_TABLE_5\n", __func__);
		goto init_err;
	}

	ret = dsim_write_hl_data(lcd, SEQ_EOTP_DISABLE, ARRAY_SIZE(SEQ_EOTP_DISABLE));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_EOTP_DISABLE\n", __func__);
		goto init_err;
	}

	ret = dsim_write_hl_data(lcd, SEQ_TLPX_80NS, ARRAY_SIZE(SEQ_TLPX_80NS));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_TLPX_80NS\n", __func__);
		goto init_err;
	}

	ret = dsim_write_hl_data(lcd, SEQ_TABLE_0, ARRAY_SIZE(SEQ_TABLE_0));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_TABLE_5\n", __func__);
		goto init_err;
	}

	ret = dsim_write_hl_data(lcd, SEQ_SCREEN_OFF, ARRAY_SIZE(SEQ_SCREEN_OFF));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_SCREEN_OFF\n", __func__);
		goto init_err;
	}

	ret = dsim_write_hl_data(lcd, SEQ_TABLE_4, ARRAY_SIZE(SEQ_TABLE_4));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_TABLE_4\n", __func__);
		goto init_err;
	}

	ret = dsim_write_hl_data(lcd, SEQ_BL_00, ARRAY_SIZE(SEQ_BL_00));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_BL_00\n", __func__);
		goto init_err;
	}

	ret = dsim_write_hl_data(lcd, SEQ_TABLE_0, ARRAY_SIZE(SEQ_TABLE_0));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_TABLE_5\n", __func__);
		goto init_err;
	}

#if defined(CONFIG_SEC_FACTORY)
	hx8279d_read_id(lcd);
#endif

	if (lcd->id_info.id[2] == CRT_PANEL_ID) {
		dev_info(&lcd->ld->dev, "%s: CPT PANEL. [0x%x]\n", __func__, cpu_to_be32(lcd->id_info.value));
		ret = dsi_write_table(lcd, SEQ_CMD_TABLE_CPT, ARRAY_SIZE(SEQ_CMD_TABLE_CPT));
		if (ret < 0) {
			dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_CMD_TABLE_CPT\n", __func__);
			goto init_err;
		}
	} else if (lcd->id_info.id[2] == BOE2_PANEL_ID) {
		dev_info(&lcd->ld->dev, "%s: BOE2 PANEL. [0x%x]\n", __func__, cpu_to_be32(lcd->id_info.value));
		ret = dsi_write_table(lcd, SEQ_CMD_TABLE_BOE2, ARRAY_SIZE(SEQ_CMD_TABLE_BOE2));
		if (ret < 0) {
			dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_CMD_TABLE_BOE2\n", __func__);
			goto init_err;
		}
	} else {
		dev_info(&lcd->ld->dev, "%s: BOE PANEL. [0x%x]\n", __func__, cpu_to_be32(lcd->id_info.value));
		ret = dsi_write_table(lcd, SEQ_CMD_TABLE, ARRAY_SIZE(SEQ_CMD_TABLE));
		if (ret < 0) {
			dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_CMD_TABLE\n", __func__);
			goto init_err;
		}
	}

	if (lcd->tp_mode == 1) {
		dev_info(&lcd->ld->dev, "%s: tp_code. [0xD6 => 0x03]\n", __func__);
		ret = dsi_write_table(lcd, SEQ_CMD_TP_3, ARRAY_SIZE(SEQ_CMD_TP_3));
		if (ret < 0) {
			dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_CMD_TP_3\n", __func__);
			goto init_err;
		}
	}

	if (lcd->tp_mode == 2) {
		dev_info(&lcd->ld->dev, "%s: tp_code. [0xD6 => 0x04]\n", __func__);
		ret = dsi_write_table(lcd, SEQ_CMD_TP_4, ARRAY_SIZE(SEQ_CMD_TP_4));
		if (ret < 0) {
			dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_CMD_TP_4\n", __func__);
			goto init_err;
		}
	}

	ret = dsi_write_table(lcd, SEQ_CMD_TABLE_BL, ARRAY_SIZE(SEQ_CMD_TABLE_BL));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_CMD_TABLE_BL\n", __func__);
		goto init_err;
	}

	msleep(110);

	dev_info(&lcd->ld->dev, "%s: --\n", __func__);

init_err:
	return ret;
}

static int hx8279d_probe(struct lcd_info *lcd)
{
	int ret = 0;
	struct device_node *np;

	dev_info(&lcd->ld->dev, "+ %s\n", __func__);

	lcd->bd->props.max_brightness = EXTEND_BRIGHTNESS;
	lcd->bd->props.brightness = UI_DEFAULT_BRIGHTNESS;

	lcd->state = PANEL_STATE_RESUMED;
	lcd->tp_mode = 0;

	np = of_find_node_with_property(NULL, "lcd_info");
	np = of_parse_phandle(np, "lcd_info", 0);
	ret = of_property_read_u32(np, "duty_max", &lcd->pwm_max);
	if (ret < 0)
		dev_info(&lcd->ld->dev, "%s: %d: of_property_read_u32_duty_max\n", __func__, __LINE__);
	ret = of_property_read_u32(np, "duty_outdoor", &lcd->pwm_outdoor);
	if (ret < 0)
		dev_info(&lcd->ld->dev, "%s: %d: of_property_read_u32_duty_outdoor\n", __func__, __LINE__);

	if (lcd->pwm_max == 168)
		memcpy(brightness_table, brightness_table_note, sizeof(brightness_table));

	ret = hx8279d_read_init_info(lcd);
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

static ssize_t tp_change_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);
	int mode, rc;

	rc = kstrtouint(buf, 0, &mode);
	if (rc < 0)
		return rc;

	if (mode < 0 || mode > 2)
		mode = 0;

	dev_info(dev, "%s: %d\n", __func__, mode);

	lcd->tp_mode = mode;

	return size;
}

static DEVICE_ATTR(lcd_type, 0444, lcd_type_show, NULL);
static DEVICE_ATTR(window_type, 0444, window_type_show, NULL);
static DEVICE_ATTR(tp_change, 0220, NULL, tp_change_store);

static struct attribute *lcd_sysfs_attributes[] = {
	&dev_attr_lcd_type.attr,
	&dev_attr_window_type.attr,
	&dev_attr_tp_change.attr,
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
	ret = hx8279d_probe(lcd);
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
		hx8279d_init(lcd);

	hx8279d_displayon(lcd);

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

	hx8279d_exit(lcd);

	mutex_lock(&lcd->lock);
	lcd->state = PANEL_STATE_SUSPENED;
	mutex_unlock(&lcd->lock);

	dev_info(&lcd->ld->dev, "- %s: %d, %d\n", __func__, lcd->state, lcd->connected);

exit:
	return 0;
}

struct mipi_dsim_lcd_driver hx8279d_mipi_lcd_driver = {
	.name		= "hx8279d",
	.probe		= dsim_panel_probe,
	.displayon	= dsim_panel_displayon,
	.suspend	= dsim_panel_suspend,
};

