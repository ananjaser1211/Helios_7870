/* linux/drivers/video/backlight/td4100_j7pop_lcd_ctrl.c
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
#include <linux/of_device.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <video/mipi_display.h>
#include "../dsim.h"
#include "../decon.h"
#include "../decon_board.h"
#include "dsim_panel.h"
#include "../decon_notify.h"

#include "td4100_j7pop_param.h"

#if defined(CONFIG_EXYNOS_DECON_MDNIE_LITE)
#include "mdnie.h"
#include "mdnie_lite_table_j7pop.h"
#endif

#define TD4100_ID_REG			0xDA	/* LCD ID1,ID2,ID3 */
#define TD4100_ID_LEN			3
#define BRIGHTNESS_REG			0x51

#if defined(CONFIG_SEC_INCELL)
#include <linux/sec_incell.h>
#endif

#define DSI_WRITE(cmd, size)		do {				\
	ret = dsim_write_hl_data(lcd, cmd, size);			\
	if (ret < 0) {							\
		dev_err(&lcd->ld->dev, "%s: failed to write %s\n", __func__, #cmd);	\
		ret = -EPERM;						\
		goto exit;						\
	}								\
} while (0)

#define DSI_WRITE_G(cmd, size)		do {				\
	ret = dsim_write_hl_data_generic(lcd, cmd, size);			\
	if (ret < 0) {							\
		dev_err(&lcd->ld->dev, "%s: failed to write %s\n", __func__, #cmd); \
		ret = -EPERM;						\
		goto exit;						\
	}								\
} while (0)

struct lcd_info {
	unsigned int			bl;
	unsigned int			brightness;
	unsigned int			current_bl;
	unsigned int			state;

	struct lcd_device		*ld;
	struct backlight_device		*bd;

	unsigned char			id[3];
	unsigned char			dump_info[3];
	unsigned int 			data_type;

	int						lux;
	struct class			*mdnie_class;

	struct dsim_device		*dsim;
	struct mutex			lock;

	struct kobject			*dsi_access;
	struct kobj_attribute		dsi_access_r;
	struct kobj_attribute		dsi_access_w;

	struct notifier_block		fb_notif_panel;
};

struct i2c_client *backlight_client;

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

static int dsim_write_hl_data_generic(struct lcd_info *lcd, const u8 *cmd, u32 cmdSize)
{
	int ret;
	int retry;
	struct panel_private *priv = &lcd->dsim->priv;

	if (priv->lcdConnected == PANEL_DISCONNEDTED)
		return cmdSize;

	retry = 5;

try_write:
	if (cmdSize == 1)
		ret = dsim_write_data(lcd->dsim, MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM, cmd[0], 0);
	else if (cmdSize == 2)
		ret = dsim_write_data(lcd->dsim, MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM, cmd[0], cmd[1]);
	else
		ret = dsim_write_data(lcd->dsim, MIPI_DSI_GENERIC_LONG_WRITE, (unsigned long)cmd, cmdSize);

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

	DSI_WRITE_G(SEQ_TD4100_B0, ARRAY_SIZE(SEQ_TD4100_B0));
try_read:
	ret = dsim_read_data(lcd->dsim, MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM, (u32)addr, size, buf);
	dev_info(&lcd->ld->dev, "%s: addr: %x, ret: %d\n", __func__, addr, ret);
	if (ret != size) {
		if (--retry)
			goto try_read;
		else
			dev_err(&lcd->ld->dev, "%s: fail. addr: %x, ret: %d\n", __func__, addr, ret);
	}
exit:
	return ret;
}

static int ISL98611_array_write(const struct ISL98611_rom_data *eprom_ptr, int eprom_size)
{
	int i = 0;
	int ret = 0;

	if (!backlight_client)
		return 0;

	for (i = 0; i < eprom_size; i++) {
		ret = i2c_smbus_write_byte_data(backlight_client, eprom_ptr[i].addr, eprom_ptr[i].val);
		if (ret < 0)
			dsim_err("%s: error : BL DEVICE_CTRL setting fail\n", __func__);
	}

	return ret;
}

static int dsim_panel_set_brightness(struct lcd_info *lcd, int force)
{
	int ret = 0, temp=0;
	unsigned char bl_reg[2];

	mutex_lock(&lcd->lock);

	lcd->brightness = lcd->bd->props.brightness;

	lcd->bl = lcd->brightness;

	if (!force && lcd->state != PANEL_STATE_RESUMED) {
		dev_info(&lcd->ld->dev, "%s: panel is not active state\n", __func__);
		goto exit;
	}
	bl_reg[0] = BRIGHTNESS_REG;

	if (lcd->bl > UI_MAX_BRIGHTNESS)
		bl_reg[1] = TDDI_OUTDOOR_BRIGHTNESS;
	else if(lcd->bl >= UI_DEFAULT_BRIGHTNESS) {	// 128~255
		temp = (TDDI_MAX_BRIGHTNESS - TDDI_DEFAULT_BRIGHTNESS)*(lcd->bl - UI_DEFAULT_BRIGHTNESS);
		temp /=(UI_MAX_BRIGHTNESS - UI_DEFAULT_BRIGHTNESS);
		temp += TDDI_DEFAULT_BRIGHTNESS;
		bl_reg[1] = temp;
	} else if(lcd->bl >= UI_DIM_BRIGHTNESS) {	//10~127
		temp = (TDDI_DEFAULT_BRIGHTNESS - TDDI_DIM_BRIGHTNESS)* (lcd->bl - UI_DIM_BRIGHTNESS);
		temp /= (UI_DEFAULT_BRIGHTNESS-UI_DIM_BRIGHTNESS);
		temp += TDDI_DIM_BRIGHTNESS;
		bl_reg[1] = temp;
	} else if(lcd->bl >= UI_MIN_BRIGHTNESS){	//4~9
		temp = (TDDI_DIM_BRIGHTNESS - TDDI_MIN_BRIGHTNESS)* (lcd->bl - UI_MIN_BRIGHTNESS);
		temp /= (UI_DIM_BRIGHTNESS-UI_MIN_BRIGHTNESS);
		temp += TDDI_MIN_BRIGHTNESS;
		bl_reg[1] = temp;
	} else {	//0~3
		bl_reg[1] = lcd->bl;
	}

	DSI_WRITE(bl_reg, ARRAY_SIZE(bl_reg));
	dev_info(&lcd->ld->dev, "%s: platform BL : %d panel BL reg : %d\n", __func__, lcd->bd->props.brightness, bl_reg[1]);

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


static int td4100_read_init_info(struct lcd_info *lcd)
{
	struct panel_private *priv = &lcd->dsim->priv;
	int i = 0;

	dev_info(&lcd->ld->dev, "MDD : %s was called\n", __func__);

	if (lcdtype == 0) {
		priv->lcdConnected = PANEL_DISCONNEDTED;
		goto read_exit;
	}

	lcd->id[0] = (lcdtype & 0xFF0000) >> 16;
	lcd->id[1] = (lcdtype & 0x00FF00) >> 8;
	lcd->id[2] = (lcdtype & 0x0000FF) >> 0;

	dev_info(&lcd->ld->dev, "READ ID : ");
	for (i = 0; i < TD4100_ID_LEN; i++)
		dev_info(&lcd->ld->dev, "%02x, ", lcd->id[i]);
	dev_info(&lcd->ld->dev, "\n");

read_exit:
	return 0;
}

static int td4100_displayon(struct lcd_info *lcd)
{
	int ret = 0;

	dev_info(&lcd->ld->dev, "%s\n", __func__);

	return ret;
}

static int td4100_displayon_late(struct lcd_info *lcd)
{
	int ret = 0;

	dev_info(&lcd->ld->dev, "%s\n", __func__);

	run_list(lcd->dsim->dev, __func__);

	DSI_WRITE(SEQ_DISPLAY_ON, ARRAY_SIZE(SEQ_DISPLAY_ON));
	DSI_WRITE(SEQ_TD4100_53, ARRAY_SIZE(SEQ_TD4100_53));

	dsim_panel_set_brightness(lcd, 1);

exit:
	return ret;
}

static int td4100_exit(struct lcd_info *lcd)
{
	int ret = 0;

	dev_info(&lcd->ld->dev, "%s\n", __func__);
	DSI_WRITE(SEQ_DISPLAY_OFF, ARRAY_SIZE(SEQ_DISPLAY_OFF));
	DSI_WRITE(SEQ_SLEEP_IN, ARRAY_SIZE(SEQ_SLEEP_IN));
	msleep(100);

exit:
	return ret;
}

static int td4100_init(struct lcd_info *lcd)
{
	int ret = 0;

	dev_info(&lcd->ld->dev, "%s: ++\n", __func__);


	DSI_WRITE(SEQ_TD4100_B0, ARRAY_SIZE(SEQ_TD4100_B0));
	DSI_WRITE(SEQ_TD4100_B3, ARRAY_SIZE(SEQ_TD4100_B3));
	DSI_WRITE(SEQ_TD4100_B6, ARRAY_SIZE(SEQ_TD4100_B6));
	DSI_WRITE(SEQ_TD4100_BA, ARRAY_SIZE(SEQ_TD4100_BA));
	DSI_WRITE(SEQ_TD4100_BB, ARRAY_SIZE(SEQ_TD4100_BB));
	DSI_WRITE(SEQ_TD4100_BC, ARRAY_SIZE(SEQ_TD4100_BC));
	DSI_WRITE(SEQ_TD4100_BD, ARRAY_SIZE(SEQ_TD4100_BD));
	DSI_WRITE(SEQ_TD4100_BE, ARRAY_SIZE(SEQ_TD4100_BE));
	DSI_WRITE(SEQ_TD4100_C0, ARRAY_SIZE(SEQ_TD4100_C0));
	DSI_WRITE(SEQ_TD4100_C1, ARRAY_SIZE(SEQ_TD4100_C1));
	DSI_WRITE(SEQ_TD4100_C2, ARRAY_SIZE(SEQ_TD4100_C2));
	DSI_WRITE(SEQ_TD4100_C3, ARRAY_SIZE(SEQ_TD4100_C3));
	DSI_WRITE(SEQ_TD4100_C4, ARRAY_SIZE(SEQ_TD4100_C4));
	DSI_WRITE(SEQ_TD4100_C5, ARRAY_SIZE(SEQ_TD4100_C5));
	DSI_WRITE(SEQ_TD4100_C6, ARRAY_SIZE(SEQ_TD4100_C6));
	DSI_WRITE(SEQ_TD4100_C7, ARRAY_SIZE(SEQ_TD4100_C7));
	DSI_WRITE(SEQ_TD4100_C8, ARRAY_SIZE(SEQ_TD4100_C8));
	DSI_WRITE(SEQ_TD4100_C9, ARRAY_SIZE(SEQ_TD4100_C9));
	DSI_WRITE(SEQ_TD4100_CA, ARRAY_SIZE(SEQ_TD4100_CA));
	DSI_WRITE(SEQ_TD4100_CC, ARRAY_SIZE(SEQ_TD4100_CC));
	DSI_WRITE(SEQ_TD4100_CD, ARRAY_SIZE(SEQ_TD4100_CD));
	DSI_WRITE(SEQ_TD4100_CE, ARRAY_SIZE(SEQ_TD4100_CE));
	DSI_WRITE(SEQ_TD4100_CF, ARRAY_SIZE(SEQ_TD4100_CF));
	DSI_WRITE(SEQ_TD4100_D0, ARRAY_SIZE(SEQ_TD4100_D0));
	DSI_WRITE(SEQ_TD4100_D1, ARRAY_SIZE(SEQ_TD4100_D1));
	DSI_WRITE(SEQ_TD4100_D3, ARRAY_SIZE(SEQ_TD4100_D3));
	DSI_WRITE(SEQ_TD4100_D4, ARRAY_SIZE(SEQ_TD4100_D4));
	DSI_WRITE(SEQ_TD4100_D6_41, ARRAY_SIZE(SEQ_TD4100_D6_41));
	DSI_WRITE(SEQ_TD4100_D7, ARRAY_SIZE(SEQ_TD4100_D7));
	DSI_WRITE(SEQ_TD4100_D8, ARRAY_SIZE(SEQ_TD4100_D8));
	DSI_WRITE(SEQ_TD4100_DD, ARRAY_SIZE(SEQ_TD4100_DD));

	DSI_WRITE(SEQ_TD4100_BL, ARRAY_SIZE(SEQ_TD4100_BL));
	DSI_WRITE(SEQ_TD4100_55, ARRAY_SIZE(SEQ_TD4100_55));

	DSI_WRITE(SEQ_SLEEP_OUT, ARRAY_SIZE(SEQ_SLEEP_OUT));

	dev_info(&lcd->ld->dev, "%s: --\n", __func__);

exit:
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
		return 0;
	}

	lcd = container_of(self, struct lcd_info, fb_notif_panel);

	fb_blank = *(int *)evdata->data;

	dev_info(&lcd->ld->dev, "%s: %d\n", __func__, fb_blank);

	if (evdata->info->node != 0)
		return 0;

	if (fb_blank == FB_BLANK_UNBLANK)
		td4100_displayon_late(lcd);

	return 0;
}

#if defined(CONFIG_SEC_INCELL)
void incell_blank_unblank(void *drv_data)
{
    struct fb_info *info = registered_fb[0];

    dsim_info("+%s\n", __func__);

    if (!lock_fb_info(info))
        return;
    info->flags |= FBINFO_MISC_USEREVENT;
    fb_blank(info, FB_BLANK_POWERDOWN);
    fb_blank(info, FB_BLANK_UNBLANK);
    info->flags &= ~FBINFO_MISC_USEREVENT;
    unlock_fb_info(info);

    dsim_info("-%s\n", __func__);
}
#endif

static int isl98611_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int ret = 0;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dsim_err("need I2C_FUNC_I2C.\n");
		ret = -ENODEV;
		goto err_i2c;
	}

	backlight_client = client;

err_i2c:
	return ret;
}

static struct i2c_device_id isl98611_id[] = {
	{"isl98611", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, isl98611_id);

static struct of_device_id isl98611_i2c_dt_ids[] = {
	{ .compatible = "isl98611,i2c" },
	{ }
};

MODULE_DEVICE_TABLE(of, isl98611_i2c_dt_ids);

static struct i2c_driver isl98611_i2c_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "isl98611",
		.of_match_table	= of_match_ptr(isl98611_i2c_dt_ids),
	},
	.id_table = isl98611_id,
	.probe = isl98611_probe,
};


static int td4100_probe(struct dsim_device *dsim)
{
	int ret = 0;
	struct panel_private *priv = &dsim->priv;
	struct lcd_info *lcd = dsim->priv.par;

	dev_info(&lcd->ld->dev, "%s: was called\n", __func__);

	priv->lcdConnected = PANEL_CONNECTED;

	lcd->bd->props.max_brightness = EXTEND_BRIGHTNESS;
	lcd->bd->props.brightness = UI_DEFAULT_BRIGHTNESS;

	lcd->dsim = dsim;
	lcd->state = PANEL_STATE_RESUMED;
	lcd->lux = -1;

	td4100_read_init_info(lcd);
	if (priv->lcdConnected == PANEL_DISCONNEDTED) {
		dev_err(&lcd->ld->dev, "dsim : %s lcd was not connected\n", __func__);
		goto exit;
	}

	memset(&lcd->fb_notif_panel, 0, sizeof(lcd->fb_notif_panel));
	lcd->fb_notif_panel.notifier_call = fb_notifier_callback;
	decon_register_notifier(&lcd->fb_notif_panel);

#if defined(CONFIG_SEC_INCELL)
	incell_data.blank_unblank = incell_blank_unblank;
#endif

	i2c_add_driver(&isl98611_i2c_driver);

	dev_info(&lcd->ld->dev, "%s: done\n", __func__);
exit:
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

static ssize_t lux_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);

	sprintf(buf, "%d\n", lcd->lux);

	return strlen(buf);
}

static ssize_t lux_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);
	int value;
	int rc;

	rc = kstrtoint(buf, 0, &value);

	if (rc < 0)
		return rc;

	if (lcd->lux != value) {
		mutex_lock(&lcd->lock);
		lcd->lux = value;
		mutex_unlock(&lcd->lock);

		attr_store_for_each(lcd->mdnie_class, attr->attr.name, buf, size);
	}

	return size;
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

	if (lcd->state == PANEL_STATE_RESUMED)
		dsim_read_hl_data(lcd, reg, len, dump);

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

static DEVICE_ATTR(lcd_type, 0444, lcd_type_show, NULL);
static DEVICE_ATTR(window_type, 0444, window_type_show, NULL);
static DEVICE_ATTR(dump_register, 0644, dump_register_show, dump_register_store);
static DEVICE_ATTR(lux, 0644, lux_show, lux_store);

static struct attribute *lcd_sysfs_attributes[] = {
	&dev_attr_lcd_type.attr,
	&dev_attr_window_type.attr,
	&dev_attr_dump_register.attr,
	&dev_attr_lux.attr,
	NULL,
};

static const struct attribute_group lcd_sysfs_attr_group = {
	.attrs = lcd_sysfs_attributes,
};

static ssize_t read_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	struct lcd_info *lcd = container_of(attr, struct lcd_info, dsi_access_r);
	char *pos = buf;
	u8 reg, len, param;
	int i;
	u8 *dump = NULL;
	unsigned int data_type;

	reg = lcd->dump_info[0];
	len = lcd->dump_info[1];
	param = lcd->dump_info[2];
	data_type = lcd->data_type;

	if (!reg || !len || reg > 0xff || len > 255 || param > 0xff)
		goto exit;

	dump = kcalloc(len, sizeof(u8), GFP_KERNEL);

	if (lcd->state == PANEL_STATE_RESUMED)
		dsim_read_data(lcd->dsim, data_type, reg, len, dump);

	for (i = 0; i < len; i++)
		pos += sprintf(pos, "%02x ", dump[i]);
	pos += sprintf(pos, "\n");

	dev_info(&lcd->ld->dev, "+ [%02X]\n", reg);
	for (i = 0; i < len; i++)
		dev_info(&lcd->ld->dev, "%2d: %02x\n", i + 1, dump[i]);
	dev_info(&lcd->ld->dev, "- [%02X]\n", reg);

	kfree(dump);
exit:
	return pos - buf;
}

static ssize_t read_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t size)
{
	struct lcd_info *lcd = container_of(attr, struct lcd_info, dsi_access_r);
	unsigned int reg, len, param;
	unsigned int data_type, return_packet_type;
	int ret;

	ret = sscanf(buf, "%8x %8x %8x %8x %8x", &data_type, &reg, &param, &return_packet_type, &len);

	if (ret != 5)
		return -EINVAL;

	dev_info(&lcd->ld->dev, "%s: %x %x %x %x %x", __func__, data_type, reg, param, return_packet_type, len);

	if (!reg || !len || reg > 0xff || len > 255 || param > 255)
		return -EINVAL;

	lcd->data_type = data_type;
	lcd->dump_info[0] = reg;
	lcd->dump_info[1] = len;
	lcd->dump_info[2] = param;

	return size;
}

static ssize_t write_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t size)
{
	struct lcd_info *lcd = container_of(attr, struct lcd_info, dsi_access_w);
	int ret, i, val, len = 0;
	unsigned char seqbuf[255] = {0,};
	unsigned char *printbuf = NULL;
	char *pos, *token;

	pos = (char *)buf;
	while ((token = strsep(&pos, " ")) != NULL) {
		ret = kstrtouint(token, 16, &val);
		if (!ret) {
			seqbuf[len] = val;
			len++;
		}
		if (len == ARRAY_SIZE(seqbuf))
			break;
	}

	pos = printbuf = kcalloc(size, sizeof(u8), GFP_KERNEL);
	for (i = 0; i < len; i++)
		pos += sprintf(pos, "%02x ", seqbuf[i]);
	pos += sprintf(pos, "\n");

	len--;
	if (len < 1) {
		dev_info(&lcd->ld->dev, "%s: invalid input, %s\n", __func__, printbuf);
		goto exit;
	} else
		dev_info(&lcd->ld->dev, "%s: %d, %s\n", __func__, len, printbuf);

	if (lcd->state == PANEL_STATE_RESUMED) {
		if ((seqbuf[0] == 0x29) || (seqbuf[0] == 0x39))
			ret = dsim_write_data(lcd->dsim, (unsigned int)seqbuf[0], (unsigned long)&seqbuf[1], len);
		else if (len == 1)
			ret = dsim_write_data(lcd->dsim, (unsigned int)seqbuf[0], seqbuf[1], len);
		else if (len == 2)
			ret = dsim_write_data(lcd->dsim, (unsigned int)seqbuf[0], seqbuf[1], seqbuf[2]);
		else
			ret = dsim_write_data(lcd->dsim, (unsigned int)seqbuf[0], (unsigned long)&seqbuf[1], len);
	}

exit:
	kfree(printbuf);

	return size;
}

static void lcd_init_dsi_access(struct lcd_info *lcd)
{
	int ret = 0;

	lcd->dsi_access = kobject_create_and_add("dsi_access", NULL);
	if (!lcd->dsi_access)
		return;

	sysfs_attr_init(&lcd->dsi_access_r.attr);
	lcd->dsi_access_r.attr.name = "read";
	lcd->dsi_access_r.attr.mode = 0644;
	lcd->dsi_access_r.store = read_store;
	lcd->dsi_access_r.show = read_show;
	ret = sysfs_create_file(lcd->dsi_access, &lcd->dsi_access_r.attr);
	if (ret < 0)
		dev_err(&lcd->ld->dev, "failed to add kobj_attribute_r\n");

	sysfs_attr_init(&lcd->dsi_access_w.attr);
	lcd->dsi_access_w.attr.name = "write";
	lcd->dsi_access_w.attr.mode = 0220;
	lcd->dsi_access_w.store = write_store;
	ret = sysfs_create_file(lcd->dsi_access, &lcd->dsi_access_w.attr);
	if (ret < 0)
		dev_err(&lcd->ld->dev, "failed to add kobj_attribute_w\n");
}

static void lcd_init_sysfs(struct lcd_info *lcd)
{
	int ret = 0;

	ret = sysfs_create_group(&lcd->ld->dev.kobj, &lcd_sysfs_attr_group);
	if (ret < 0)
		dev_err(&lcd->ld->dev, "failed to add lcd sysfs\n");

	lcd_init_dsi_access(lcd);
}

#if defined(CONFIG_EXYNOS_DECON_MDNIE_LITE)
static int mdnie_lite_write_set(struct lcd_info *lcd, struct lcd_seq_info *seq, u32 num)
{
	int ret = 0, i;

	for (i = 0; i < num; i++) {
		if (seq[i].cmd) {
			if (seq[i].cmd[0] < 0xA0)
				ret = dsim_write_hl_data(lcd, seq[i].cmd, seq[i].len);
			else
				ret = dsim_write_hl_data_generic(lcd, seq[i].cmd, seq[i].len);
			if (ret != 0) {
				dev_info(&lcd->ld->dev, "%s: %dth fail\n", __func__, i);
				return ret;
			}
		}
		if (seq[i].sleep)
			usleep_range(seq[i].sleep * 1000, seq[i].sleep * 1000);
	}
	return ret;
}

static int mdnie_lite_send_seq(struct lcd_info *lcd, struct lcd_seq_info *seq, u32 num)
{
	int ret = 0;

	if (lcd->state != PANEL_STATE_RESUMED) {
		dev_info(&lcd->ld->dev, "%s: panel is not active\n", __func__);
		return -EIO;
	}

	mutex_lock(&lcd->lock);
	ret = mdnie_lite_write_set(lcd, seq, num);
	mutex_unlock(&lcd->lock);

	return ret;
}

static int mdnie_lite_read(struct lcd_info *lcd, u8 addr, u8 *buf, u32 size)
{
	int ret = 0;

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

	ret = td4100_probe(dsim);
	if (ret) {
		dev_err(&lcd->ld->dev, "%s: failed to probe panel\n", __func__);
		goto probe_err;
	}

#if defined(CONFIG_EXYNOS_DECON_LCD_SYSFS)
	lcd_init_sysfs(lcd);
#endif

#if defined(CONFIG_EXYNOS_DECON_MDNIE_LITE)
	mdnie_register(&lcd->ld->dev, lcd, (mdnie_w)mdnie_lite_send_seq, (mdnie_r)mdnie_lite_read, NULL, &tune_info);
	lcd->mdnie_class = get_mdnie_class();
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
		ret = td4100_init(lcd);
		if (ret) {
			dev_info(&lcd->ld->dev, "%s: failed to panel init\n", __func__);
			goto displayon_err;
		}
	}

	ret = td4100_displayon(lcd);
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

static int dsim_panel_displayon_late(struct dsim_device *dsim)
{
	struct lcd_info *lcd = dsim->priv.par;
	struct panel_private *priv = &lcd->dsim->priv;
	int ret = 0;

	dev_info(&lcd->ld->dev, "+%s\n", __func__);

	ret = td4100_displayon_late(lcd);
	if (ret) {
		dev_info(&lcd->ld->dev, "%s: failed to panel display on\n", __func__);
	}

	dev_info(&lcd->ld->dev, "-%s: %d\n", __func__, priv->lcdConnected);

	return ret;
}
static int dsim_panel_resume(struct dsim_device *dsim)
{
	struct lcd_info *lcd = dsim->priv.par;
	struct panel_private *priv = &lcd->dsim->priv;
	int ret = 0;
		dev_info(&lcd->ld->dev, "+%s\n", __func__);

	if (lcd->state == PANEL_STATE_SUSPENED) {

		/* VSP VSN setting, So, It should be called before power enabling */

		ret = ISL98611_array_write(ISL98611_INIT, ARRAY_SIZE(ISL98611_INIT));
		if (ret < 0)
			dev_err(&lcd->ld->dev, "DEVICE_CTRL: error : BL DEVICE_CTRL setting fail\n");

		run_list(lcd->dsim->dev, __func__);

	}

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

	ret = td4100_exit(lcd);
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

struct mipi_dsim_lcd_driver td4100_mipi_lcd_driver = {
	.probe		= dsim_panel_probe,
	.displayon	= dsim_panel_displayon,
	.displayon_late = dsim_panel_displayon_late,
	.resume		= dsim_panel_resume,
	.suspend	= dsim_panel_suspend,
};


