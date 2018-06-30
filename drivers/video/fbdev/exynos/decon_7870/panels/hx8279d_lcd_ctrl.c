/*
 * drivers/video/decon_7870/panels/hx8279d_lcd_ctrl.c
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
#include "../dsim.h"
#include "../decon.h"
#include "dsim_panel.h"

#include "hx8279d_param.h"

#include <linux/clk.h>

#define POWER_IS_ON(pwr)			(pwr <= FB_BLANK_NORMAL)
#define LEVEL_IS_HBM(brightness)		(brightness == EXTEND_BRIGHTNESS)

struct lcd_info {
	unsigned int			bl;
	unsigned int			brightness;
	unsigned int			acl_enable;
	unsigned int			siop_enable;
	unsigned int			current_acl;
	unsigned int			current_bl;
	unsigned int			current_hbm;
	unsigned int			ldi_enable;

	struct lcd_device		*ld;
	struct backlight_device		*bd;

	int				temperature;
	unsigned int			temperature_index;

	unsigned char			id[3];
	unsigned char			dump_info[3];

	struct dsim_device		*dsim;

	unsigned int			state;
	struct mutex			lock;

	unsigned int			pwm_max;
	unsigned int			pwm_outdoor;

	/* temporary sysfs to panel parameter tuning */
	unsigned int			write_disable;
	int			tp_mode;
};


static int dsim_write_hl_data(struct lcd_info *lcd, const u8 *cmd, u32 cmdSize)
{
	int ret;
	int retry;
	struct panel_private *priv = &lcd->dsim->priv;

	if (priv->lcdConnected == PANEL_DISCONNEDTED)
		return cmdSize;

	retry = 5;

try_write:
	if (cmdSize == 1)
		ret = dsim_write_data(lcd->dsim, MIPI_DSI_DCS_SHORT_WRITE, cmd[0], 0);
	else if (cmdSize == 2)
		ret = dsim_write_data(lcd->dsim, MIPI_DSI_DCS_SHORT_WRITE_PARAM, cmd[0], cmd[1]);
	else
		ret = dsim_write_data(lcd->dsim, MIPI_DSI_DCS_LONG_WRITE, (unsigned long)cmd, cmdSize);

	if (ret != 0) {
		if (--retry)
			goto try_write;
		else
			dev_err(&lcd->ld->dev, "%s: fail. cmd: %x, ret: %d\n", __func__, cmd[0], ret);
	}

	return ret;
}

static int dsim_read_hl_data(struct lcd_info *lcd, u8 addr, u32 size, u8 *buf)
{
	int ret;
	int retry = 4;
	struct panel_private *priv = &lcd->dsim->priv;

	if (priv->lcdConnected == PANEL_DISCONNEDTED)
		return size;

try_read:
	ret = dsim_read_data(lcd->dsim, MIPI_DSI_DCS_READ, (u32)addr, size, buf);
	dev_info(&lcd->ld->dev, "%s: addr: %x, ret: %d\n", __func__, addr, ret);
	if (ret != size) {
		if (--retry)
			goto try_read;
		else
			dev_err(&lcd->ld->dev, "%s: fail. addr: %x, ret: %d\n", __func__, addr, ret);
	}

	return ret;
}

static int dsim_read_hl_data_offset(struct lcd_info *lcd, u8 addr, u32 size, u8 *buf, u32 offset)
{
	unsigned char wbuf[] = {0xB0, 0};
	int ret = 0;

	wbuf[1] = offset;

	ret = dsim_write_hl_data(lcd, wbuf, ARRAY_SIZE(wbuf));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : wbuf\n", __func__);
		goto exit;
	}

	ret = dsim_read_hl_data(lcd, addr, size, buf);

	if (ret < 1)
		dev_err(&lcd->ld->dev, "%s: fail\n", __func__);

exit:
	return ret;
}

static int dsim_panel_set_brightness(struct lcd_info *lcd, int force)
{
	int ret = 0;
	unsigned char bl_reg[2] = {0xB8, 0x00};

	mutex_lock(&lcd->lock);

	lcd->brightness = lcd->bd->props.brightness;

	lcd->bl = lcd->brightness;

	if (!force && lcd->state != PANEL_STATE_RESUMED) {
		dev_info(&lcd->ld->dev, "%s: panel is not active state\n", __func__);
		goto exit;
	}

	bl_reg[1] = lcd->bl * lcd->pwm_max / 0xFF;

	/* OUTDOOR MODE */
	if (LEVEL_IS_HBM(lcd->brightness))
		bl_reg[1] = lcd->pwm_outdoor;

	ret = dsim_write_hl_data(lcd, SEQ_TABLE_4, ARRAY_SIZE(SEQ_TABLE_4));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_TABLE_04\n", __func__);
		goto exit;
	}

	ret = dsim_write_hl_data(lcd, bl_reg, ARRAY_SIZE(bl_reg));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : bl_reg\n", __func__);
		goto exit;
	}

	dev_info(&lcd->ld->dev, "%s: %02d [0x%02x]\n", __func__, lcd->bl, bl_reg[1]);

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


static int hx8279d_exit(struct lcd_info *lcd)
{
	int ret = 0;

	dev_info(&lcd->ld->dev, "%s\n", __func__);

	ret = dsim_write_hl_data(lcd, SEQ_TABLE_0, ARRAY_SIZE(SEQ_TABLE_0));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_TABLE_0\n", __func__);
		goto exit_err;
	}

	ret = dsim_write_hl_data(lcd, SEQ_DISPLAY_OFF, ARRAY_SIZE(SEQ_DISPLAY_OFF));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_DISPLAY_OFF\n", __func__);
		goto exit_err;
	}
	msleep(50);

	ret = dsim_write_hl_data(lcd, SEQ_SLEEP_IN, ARRAY_SIZE(SEQ_SLEEP_IN));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_SLEEP_IN\n", __func__);
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
			dev_err(&lcd->ld->dev, "%s: failed to write CMD : 0x%02x, 0x%02x\n", __func__, table_ptr[i].cmd[0], table_ptr[i].cmd[1]);
			goto write_exit;
		}
	}

write_exit:
	return ret;
}

static int hx8279d_get_id(struct lcd_info *lcd)
{
	int i = 0;
	struct panel_private *priv = &lcd->dsim->priv;

	dev_info(&lcd->ld->dev, "MDD : %s was called\n", __func__);

	if (lcdtype == 0) {
		priv->lcdConnected = PANEL_DISCONNEDTED;
		goto read_exit;
	}

	lcd->id[0] = (lcdtype & 0xFF0000) >> 16;
	lcd->id[1] = (lcdtype & 0x00FF00) >> 8;
	lcd->id[2] = (lcdtype & 0x0000FF) >> 0;

	dev_info(&lcd->ld->dev, "READ ID : ");
	for (i = 0; i < 3; i++)
		dev_info(&lcd->ld->dev, "%02x, ", lcd->id[i]);
	dev_info(&lcd->ld->dev, "\n");

read_exit:
	return 0;
}


static int hx8279d_read_id(struct lcd_info *lcd)
{
	int i = 0, ret = 0;
	struct panel_private *priv = &lcd->dsim->priv;
	char buf = 0;

	dev_info(&lcd->ld->dev, "MDD : %s was called\n", __func__);

	if (lcdtype == 0) {
		priv->lcdConnected = PANEL_DISCONNEDTED;
		goto read_exit;
	}

	priv->lcdConnected = PANEL_CONNECTED;

	ret = dsim_read_hl_data(lcd, HX8279D_ID_REG, 1, &lcd->id[0]);
	ret = dsim_read_hl_data(lcd, HX8279D_ID_REG + 1, 1, &lcd->id[1]);
	ret = dsim_read_hl_data(lcd, HX8279D_ID_REG + 2, 1, &lcd->id[2]);
	ret = dsim_read_hl_data(lcd, HX8279D_DUAL_REG, 1, &buf);

	if (ret <= 0)
		priv->lcdConnected = PANEL_DISCONNEDTED;

	lcd->id[2] = lcd->id[2] + buf;

	dev_info(&lcd->ld->dev, "READ ID : ");
	for (i = 0; i < 3; i++)
		dev_info(&lcd->ld->dev, "%02x, ", lcd->id[i]);
	dev_info(&lcd->ld->dev, "\n");

read_exit:
	return 0;
}

static int hx8279d_displayon(struct lcd_info *lcd)
{
	int ret = 0;

	dev_info(&lcd->ld->dev, "%s\n", __func__);

	ret = dsim_write_hl_data(lcd, SEQ_TABLE_0, ARRAY_SIZE(SEQ_TABLE_0));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_TABLE_0\n", __func__);
		goto display_err;
	}

	ret = dsim_write_hl_data(lcd, SEQ_DISPLAY_ON, ARRAY_SIZE(SEQ_DISPLAY_ON));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_DISPLAY_ON\n", __func__);
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

	dev_info(&lcd->ld->dev, "%s\n", __func__);

	dev_info(&lcd->ld->dev, "%s: porch: real: vclk:%lu hbp:%d hfp:%d hsa:%d vbp:%d vfp:%d vsa:%d\n",
				__func__, clk_get_rate(decon->res.vclk_leaf),
				dsim->lcd_info.hbp, dsim->lcd_info.hfp, dsim->lcd_info.hsa,
				dsim->lcd_info.vbp, dsim->lcd_info.vfp, dsim->lcd_info.vsa);

	ret = dsim_write_hl_data(lcd, SEQ_TABLE_5, ARRAY_SIZE(SEQ_TABLE_5));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_TABLE_5\n", __func__);
		goto init_err;
	}

	ret = dsim_write_hl_data(lcd, SEQ_EOTP_DISABLE, ARRAY_SIZE(SEQ_EOTP_DISABLE));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_EOTP_DISABLE\n", __func__);
		goto init_err;
	}

	ret = dsim_write_hl_data(lcd, SEQ_TLPX_80NS, ARRAY_SIZE(SEQ_TLPX_80NS));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_TLPX_80NS\n", __func__);
		goto init_err;
	}

	ret = dsim_write_hl_data(lcd, SEQ_TABLE_0, ARRAY_SIZE(SEQ_TABLE_0));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_TABLE_5\n", __func__);
		goto init_err;
	}

	ret = dsim_write_hl_data(lcd, SEQ_SCREEN_OFF, ARRAY_SIZE(SEQ_SCREEN_OFF));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_SCREEN_OFF\n", __func__);
		goto init_err;
	}

	ret = dsim_write_hl_data(lcd, SEQ_TABLE_4, ARRAY_SIZE(SEQ_TABLE_4));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_TABLE_4\n", __func__);
		goto init_err;
	}

	ret = dsim_write_hl_data(lcd, SEQ_BL_00, ARRAY_SIZE(SEQ_BL_00));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_BL_00\n", __func__);
		goto init_err;
	}

	ret = dsim_write_hl_data(lcd, SEQ_TABLE_0, ARRAY_SIZE(SEQ_TABLE_0));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_TABLE_5\n", __func__);
		goto init_err;
	}

	hx8279d_read_id(lcd);

	if (lcdtype == BOE_PANEL_ID) {
		dev_info(&lcd->ld->dev, "%s: BOE PANEL. [0x%x]\n", __func__, lcdtype);
		ret = dsi_write_table(lcd, SEQ_CMD_TABLE, ARRAY_SIZE(SEQ_CMD_TABLE));
		if (ret < 0) {
			dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_CMD_TABLE\n", __func__);
			goto init_err;
		}
	} else {
		dev_info(&lcd->ld->dev, "%s: CPT PANEL. [0x%x]\n", __func__, lcdtype);
		ret = dsi_write_table(lcd, SEQ_CMD_TABLE_CPT, ARRAY_SIZE(SEQ_CMD_TABLE_CPT));
		if (ret < 0) {
			dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_CMD_TABLE_CPT\n", __func__);
			goto init_err;
		}
	}

	if (lcd->tp_mode == 1) {
		dev_info(&lcd->ld->dev, "%s: tp_code. [0xD6 => 0x03]\n", __func__);
		ret = dsi_write_table(lcd, SEQ_CMD_TP_3, ARRAY_SIZE(SEQ_CMD_TP_3));
		if (ret < 0) {
			dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_CMD_TP_3\n", __func__);
			goto init_err;
		}
	}

	if (lcd->tp_mode == 2) {
		dev_info(&lcd->ld->dev, "%s: tp_code. [0xD6 => 0x04]\n", __func__);
		ret = dsi_write_table(lcd, SEQ_CMD_TP_4, ARRAY_SIZE(SEQ_CMD_TP_4));
		if (ret < 0) {
			dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_CMD_TP_4\n", __func__);
			goto init_err;
		}
	}

	ret = dsi_write_table(lcd, SEQ_CMD_TABLE_BL, ARRAY_SIZE(SEQ_CMD_TABLE_BL));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_CMD_TABLE_BL\n", __func__);
		goto init_err;
	}

	msleep(110);

init_err:
	return ret;
}

static int hx8279d_probe(struct dsim_device *dsim)
{
	int ret = 0;
	struct panel_private *priv = &dsim->priv;
	struct lcd_info *lcd = dsim->priv.par;
	struct device_node *np;


	dev_info(&lcd->ld->dev, "%s: was called\n", __func__);

	lcd->bd->props.max_brightness = EXTEND_BRIGHTNESS;
	lcd->bd->props.brightness = UI_DEFAULT_BRIGHTNESS;

	priv->lcdConnected = PANEL_CONNECTED;

	lcd->dsim = dsim;
	lcd->state = PANEL_STATE_RESUMED;
	lcd->temperature = NORMAL_TEMPERATURE;
	lcd->acl_enable = 0;
	lcd->current_acl = 0;
	lcd->siop_enable = 0;
	lcd->current_hbm = 0;
	lcd->tp_mode = 0;

	np = of_find_node_with_property(NULL, "lcd_info");
	np = of_parse_phandle(np, "lcd_info", 0);
	ret = of_property_read_u32(np, "duty_max", &lcd->pwm_max);
	if (ret)
		dev_err(&lcd->ld->dev, "%s: %d: of_property_read_u32_duty_max\n", __func__, __LINE__);
	ret = of_property_read_u32(np, "duty_outdoor", &lcd->pwm_outdoor);
	if (ret)
		dev_err(&lcd->ld->dev, "%s: %d: of_property_read_u32_duty_outdoor\n", __func__, __LINE__);

	hx8279d_get_id(lcd);

	return ret;
}


static ssize_t lcd_type_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);

	sprintf(buf, "BOE_%02X%02X%02X\n", lcd->id[0], lcd->id[1], lcd->id[2]);

	return strlen(buf);
}

static ssize_t window_type_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);

	sprintf(buf, "%x %x %x\n", lcd->id[0], lcd->id[1], lcd->id[2]);

	return strlen(buf);
}

static ssize_t dump_register_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);
	char *pos = buf;
	u8 reg, len, table;
	int i;
	u8 *dump = NULL;

	reg = lcd->dump_info[0];
	len = lcd->dump_info[1];
	table = lcd->dump_info[2];

	if (!reg || !len || reg > 0xff || len > 255 || table > 255)
		goto exit;

	dump = kcalloc(len, sizeof(u8), GFP_KERNEL);

	if (lcd->state == PANEL_STATE_RESUMED) {
		if (table)
			dsim_read_hl_data_offset(lcd, reg, len, dump, table);
		else
			dsim_read_hl_data(lcd, reg, len, dump);
	}

	pos += sprintf(pos, "+ [%02X]\n", reg);
	for (i = 0; i < len; i++)
		pos += sprintf(pos, "%2d: %02x\n", i + 1, dump[i]);
	pos += sprintf(pos, "- [%02X]\n", reg);

	dev_info(&lcd->ld->dev, "+ [%02X]\n", reg);
	for (i = 0; i < len; i++)
		dev_info(&lcd->ld->dev, "%2d: %02x\n", i + 1, dump[i]);
	dev_info(&lcd->ld->dev, "- [%02X]\n", reg);

	kfree(dump);
exit:
	return pos - buf;
}

static ssize_t dump_register_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);
	unsigned int reg, len, offset;
	int ret;

	ret = sscanf(buf, "%8x %8d %8d", &reg, &len, &offset);

	if (ret == 2)
		offset = 0;

	dev_info(dev, "%s: %x %d %d\n", __func__, reg, len, offset);

	if (ret < 0)
		return ret;
	else {
		if (!reg || !len || reg > 0xff || len > 255 || offset > 255)
			return -EINVAL;

		lcd->dump_info[0] = reg;
		lcd->dump_info[1] = len;
		lcd->dump_info[2] = offset;
	}

	return size;
}

static ssize_t porch_change_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);
	struct dsim_device *dsim = lcd->dsim;
	unsigned int hfp, hbp, hsa, vfp, vbp, vsa;
	unsigned long vclk;
	struct decon_device *decon = NULL;
	decon = (struct decon_device *)dsim->decon;

	sscanf(buf, "%8lu %8d %8d %8d %8d %8d %8d", &vclk, &hbp, &hfp, &hsa, &vbp, &vfp, &vsa);

	dev_info(dev, "%s: input: vclk:%lu hbp:%d hfp:%d hsa:%d vbp:%d vfp:%d vsa:%d\n",
		__func__, vclk, hbp, hfp, hsa, vbp, vfp, vsa);

	/* dsim */
	dsim->lcd_info.hbp = hbp;
	dsim->lcd_info.hfp = hfp;
	dsim->lcd_info.hsa = hsa;

	dsim->lcd_info.vbp = vbp;
	dsim->lcd_info.vfp = vfp;
	dsim->lcd_info.vsa = vsa;

	/* decon */
	decon->lcd_info->hbp = hbp;
	decon->lcd_info->hfp = hfp;
	decon->lcd_info->hsa = hsa;

	decon->lcd_info->vbp = vbp;
	decon->lcd_info->vfp = vfp;
	decon->lcd_info->vsa = vsa;

	decon->pdata->disp_pll_clk = vclk;
	decon->pdata->disp_vclk = vclk;

	return size;
}

static ssize_t clk_change_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);
	unsigned int p, m, s;

	sscanf(buf, "%8d %8d %8d", &p, &m, &s);

	dev_info(dev, "%s: p:%d m:%d s:%d\n", __func__, p, m, s);

	lcd->dsim->lcd_info.dphy_pms.p = p;
	lcd->dsim->lcd_info.dphy_pms.m = m;
	lcd->dsim->lcd_info.dphy_pms.s = s;

	return size;
}

static ssize_t tp_change_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);
	int mode;

	sscanf(buf, "%8d", &mode);

	if (mode < 0 || mode > 2)
		mode = 0;

	dev_info(dev, "%s: %d\n", __func__, mode);

	lcd->tp_mode = mode;

	return size;
}

static ssize_t write_register_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);
	int ret, i, data, count = 0;
	unsigned char seqbuf[255] = {0,};
	unsigned char *printbuf = NULL;
	char *pos, *token;

	pos = (char *)buf;
	while ((token = strsep(&pos, " ")) != NULL) {
		ret = sscanf(token, "%8x", &data);
		if (ret) {
			seqbuf[count] = data;
			count++;
		}
		if (count == ARRAY_SIZE(seqbuf))
			break;
	}

	pos = printbuf = kcalloc(size, sizeof(u8), GFP_KERNEL);
	for (i = 0; i < count; i++) {
		pos += sprintf(pos, "%02x ", seqbuf[i]);
	}
	pos += sprintf(pos, "\n");

	if (count <= 1) {
		dev_info(&lcd->ld->dev, "%s: invalid input, %s\n", __func__, printbuf);
		goto exit;
	} else
		dev_info(&lcd->ld->dev, "%s: %s\n", __func__, printbuf);

	if (lcd->state == PANEL_STATE_RESUMED)
		ret = dsim_write_hl_data(lcd, seqbuf, count);

exit:
	kfree(printbuf);

	return size;
}

static ssize_t write_disable_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);

	sprintf(buf, "%u\n", lcd->write_disable);

	return strlen(buf);
}

static ssize_t write_disable_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);
	int value;
	int rc;

	rc = kstrtoul(buf, (unsigned int)0, (unsigned long *)&value);
	if (rc < 0)
		return rc;
	else {
		if (lcd->write_disable != value) {
			dev_info(&lcd->ld->dev, "%s: %d, %d\n", __func__, lcd->write_disable, value);
			mutex_lock(&lcd->lock);
			lcd->write_disable = value;
			mutex_unlock(&lcd->lock);
		}
	}
	return size;
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
	return size;
}

static DEVICE_ATTR(lcd_type, 0444, lcd_type_show, NULL);
static DEVICE_ATTR(window_type, 0444, window_type_show, NULL);
static DEVICE_ATTR(siop_enable, 0664, siop_enable_show, siop_enable_store);
static DEVICE_ATTR(power_reduce, 0664, power_reduce_show, power_reduce_store);
static DEVICE_ATTR(temperature, 0664, temperature_show, temperature_store);
static DEVICE_ATTR(dump_register, 0644, dump_register_show, dump_register_store);
static DEVICE_ATTR(write_register, 0220, NULL, write_register_store);
static DEVICE_ATTR(write_disable, 0644, write_disable_show, write_disable_store);
static DEVICE_ATTR(porch_change, 0220, NULL, porch_change_store);
static DEVICE_ATTR(clk_change, 0220, NULL, clk_change_store);
static DEVICE_ATTR(tp_change, 0220, NULL, tp_change_store);

static struct attribute *lcd_sysfs_attributes[] = {
	&dev_attr_lcd_type.attr,
	&dev_attr_window_type.attr,
	&dev_attr_siop_enable.attr,
	&dev_attr_power_reduce.attr,
	&dev_attr_temperature.attr,
	&dev_attr_dump_register.attr,
	&dev_attr_write_register.attr,
	&dev_attr_write_disable.attr,
	&dev_attr_porch_change.attr,
	&dev_attr_clk_change.attr,
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

	ret = hx8279d_probe(dsim);
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
		ret = hx8279d_init(lcd);
		if (ret) {
			dev_info(&lcd->ld->dev, "%s: failed to panel init\n", __func__);
			goto displayon_err;
		}
	}

	ret = hx8279d_displayon(lcd);
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

	ret = hx8279d_exit(lcd);
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

struct mipi_dsim_lcd_driver hx8279d_mipi_lcd_driver = {
	.probe		= dsim_panel_probe,
	.displayon	= dsim_panel_displayon,
	.suspend	= dsim_panel_suspend,
};
