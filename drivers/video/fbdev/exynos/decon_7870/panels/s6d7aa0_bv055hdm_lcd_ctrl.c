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

#include "s6d7aa0_bv055hdm_param.h"
#include "dd.h"

#define PANEL_STATE_SUSPENED	0
#define PANEL_STATE_RESUMED	1
#define PANEL_STATE_SUSPENDING	2

#define S6D7AA0_ID_REG			0xDA
#define S6D7AA0_ID_LEN			3
#define BRIGHTNESS_REG			0x51
#define LEVEL_IS_HBM(brightness)	(brightness == EXTEND_BRIGHTNESS)

#define DSI_WRITE(cmd, size)		do {				\
	ret = dsim_write_hl_data(lcd, cmd, size);			\
	if (ret < 0)							\
		dev_info(&lcd->ld->dev, "%s: failed to write %s\n", __func__, #cmd);	\
} while (0)

struct lcd_info {
	unsigned int			connected;
	unsigned int			brightness;
	unsigned int			current_hbm;
	unsigned int			state;
	int				lux;

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

	struct i2c_client		*blic_client;
	int				backlight_reset[3];
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

static int lm3632_array_write(struct i2c_client *client, u8 *ptr, u8 len)
{
	unsigned int i = 0;
	int ret = 0;
	u8 command = 0, value = 0;
	struct lcd_info *lcd = NULL;

	if (!client)
		return ret;

	lcd = i2c_get_clientdata(client);
	if (!lcd)
		return ret;

	if (!lcdtype) {
		dev_info(&lcd->ld->dev, "%s: lcdtype: %d\n", __func__, lcdtype);
		return ret;
	}

	if (len % 2) {
		dev_info(&lcd->ld->dev, "%s: length(%d) invalid\n", __func__, len);
		return ret;
	}

	for (i = 0; i < len; i += 2) {
		command = ptr[i + 0];
		value = ptr[i + 1];

		ret = i2c_smbus_write_byte_data(client, command, value);
		if (ret < 0)
			dev_info(&lcd->ld->dev, "%s: fail. %2x, %2x, %d\n", __func__, command, value, ret);
	}

	return ret;
}

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

	if (force || LEVEL_IS_HBM(lcd->brightness) != lcd->current_hbm) {
		lcd->current_hbm = LEVEL_IS_HBM(lcd->brightness);
		if (LEVEL_IS_HBM(lcd->brightness)) {
			lm3632_array_write(lcd->blic_client, backlight_ic_tuning_outdoor, ARRAY_SIZE(backlight_ic_tuning_outdoor));
			dsim_write_hl_data(lcd, SEQ_PASSWD1, sizeof(SEQ_PASSWD1));
			dsim_write_hl_data(lcd, OUTDOOR_MDNIE_1, sizeof(OUTDOOR_MDNIE_1));
			dsim_write_hl_data(lcd, OUTDOOR_MDNIE_2, sizeof(OUTDOOR_MDNIE_2));
			dsim_write_hl_data(lcd, OUTDOOR_MDNIE_3, sizeof(OUTDOOR_MDNIE_3));
			dsim_write_hl_data(lcd, OUTDOOR_MDNIE_4, sizeof(OUTDOOR_MDNIE_4));
			dsim_write_hl_data(lcd, OUTDOOR_MDNIE_5, sizeof(OUTDOOR_MDNIE_5));
			dsim_write_hl_data(lcd, OUTDOOR_MDNIE_6, sizeof(OUTDOOR_MDNIE_6));
			dsim_write_hl_data(lcd, SEQ_PASSWD1_LOCK, sizeof(SEQ_PASSWD1_LOCK));

			dev_info(&lcd->ld->dev, "%s: Back light IC outdoor mode : %d\n", __func__, lcd->current_hbm);
		} else {
			dsim_write_hl_data(lcd, SEQ_PASSWD1, sizeof(SEQ_PASSWD1));
			dsim_write_hl_data(lcd, UI_MDNIE_1, sizeof(UI_MDNIE_1));
			dsim_write_hl_data(lcd, UI_MDNIE_2, sizeof(UI_MDNIE_2));
			dsim_write_hl_data(lcd, UI_MDNIE_3, sizeof(UI_MDNIE_3));
			dsim_write_hl_data(lcd, UI_MDNIE_4, sizeof(UI_MDNIE_4));
			dsim_write_hl_data(lcd, UI_MDNIE_5, sizeof(UI_MDNIE_5));
			dsim_write_hl_data(lcd, UI_MDNIE_6, sizeof(UI_MDNIE_6));
			dsim_write_hl_data(lcd, SEQ_PASSWD1_LOCK, sizeof(SEQ_PASSWD1_LOCK));
			lm3632_array_write(lcd->blic_client, backlight_ic_tuning_normal, ARRAY_SIZE(backlight_ic_tuning_normal));

			dev_info(&lcd->ld->dev, "%s: Back light IC outdoor mode : %d\n", __func__, lcd->current_hbm);
		}
	}

	if (dsim_write_hl_data(lcd, bl_reg, ARRAY_SIZE(bl_reg)) < 0)
		dev_info(&lcd->ld->dev, "%s: failed to write brightness cmd.\n", __func__);

	dev_info(&lcd->ld->dev, "%s: brightness: %3d, %3d(%2x), lx: %d\n", __func__,
		lcd->brightness, bl_reg[1], bl_reg[1], lcd->lux);
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

#if defined(CONFIG_SEC_FACTORY)
	s6d7aa0_read_id(lcd);
#endif

	ret = dsim_write_hl_data(lcd, SEQ_DISPLAY_ON, ARRAY_SIZE(SEQ_DISPLAY_ON));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : DISPLAY_ON\n", __func__);
		goto displayon_err;
	}

	usleep_range(20000, 21000); // wait 20ms

	ret = dsim_write_hl_data(lcd, SEQ_PASSWD1, sizeof(SEQ_PASSWD1));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_PASSWD1\n", __func__);
		goto displayon_err;
	}
	ret = dsim_write_hl_data(lcd, UI_MDNIE_1, sizeof(UI_MDNIE_1));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : UI_MDNIE_1\n", __func__);
		goto displayon_err;
	}
	ret = dsim_write_hl_data(lcd, UI_MDNIE_2, sizeof(UI_MDNIE_2));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : UI_MDNIE_2\n", __func__);
		goto displayon_err;
	}
	ret = dsim_write_hl_data(lcd, UI_MDNIE_3, sizeof(UI_MDNIE_3));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : UI_MDNIE_3\n", __func__);
		goto displayon_err;
	}
	ret = dsim_write_hl_data(lcd, UI_MDNIE_4, sizeof(UI_MDNIE_4));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : UI_MDNIE_4\n", __func__);
		goto displayon_err;
	}
	ret = dsim_write_hl_data(lcd, UI_MDNIE_5, sizeof(UI_MDNIE_5));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : UI_MDNIE_5\n", __func__);
		goto displayon_err;
	}
	ret = dsim_write_hl_data(lcd, UI_MDNIE_6, sizeof(UI_MDNIE_6));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : UI_MDNIE_6\n", __func__);
		goto displayon_err;
	}
	ret = dsim_write_hl_data(lcd, SEQ_PASSWD1_LOCK, sizeof(SEQ_PASSWD1_LOCK));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_PASSWD1_LOCK\n", __func__);
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

	msleep(50);

	ret = dsim_write_hl_data(lcd, SEQ_SLEEP_IN, ARRAY_SIZE(SEQ_SLEEP_IN));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SLEEP_IN\n", __func__);
		goto exit_err;
	}

	msleep(120);

	if (lcd->backlight_reset[0])
		lm3632_array_write(lcd->blic_client, backlight_ic_tuning, ARRAY_SIZE(backlight_ic_tuning));

exit_err:
	return ret;
}

static int s6d7aa0_init(struct lcd_info *lcd)
{
	int ret = 0;

	dev_info(&lcd->ld->dev, "%s\n", __func__);

	msleep(20);

	/* init seq 1 */
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
	ret = dsim_write_hl_data(lcd, SEQ_OTP_CTRL, ARRAY_SIZE(SEQ_OTP_CTRL));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_OTP_CTRL\n", __func__);
		goto init_exit;
	}

	/* init seq 2 */
	ret = dsim_write_hl_data(lcd, SEQ_PENEL_CONTROL, ARRAY_SIZE(SEQ_PENEL_CONTROL));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_PENEL_CONTROL\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_INTERFACE_CONTROL, ARRAY_SIZE(SEQ_INTERFACE_CONTROL));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_INTERFACE_CONTROL\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_CLK_CONTROL, ARRAY_SIZE(SEQ_CLK_CONTROL));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_CLK_CONTROL\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_SMPSCTL, ARRAY_SIZE(SEQ_SMPSCTL));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_SMPSCTL\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_BAT_CONTROL, ARRAY_SIZE(SEQ_BAT_CONTROL));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_BAT_CONTROL\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_MIE_CONTROL1, ARRAY_SIZE(SEQ_MIE_CONTROL1));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_MIE_CONTROL1\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_BACKLIGHT_CONTROL, ARRAY_SIZE(SEQ_BACKLIGHT_CONTROL));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_PSEQ_BACKLIGHT_CONTROLORCH_CTL\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_MIE_CONTROL2, ARRAY_SIZE(SEQ_MIE_CONTROL2));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_MIE_CONTROL2\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_BL_CONTROL, ARRAY_SIZE(SEQ_BL_CONTROL));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_BL_CONTROL\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_ASG_EQ, ARRAY_SIZE(SEQ_ASG_EQ));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_ASG_EQ\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_DISPLAY_CONTROL, ARRAY_SIZE(SEQ_DISPLAY_CONTROL));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_DISPLAY_CONTROL\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_MANPWR_SETTING, ARRAY_SIZE(SEQ_MANPWR_SETTING));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_MANPWR_SETTING\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_PANEL_GATE_CONTROL, ARRAY_SIZE(SEQ_PANEL_GATE_CONTROL));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_PANEL_GATE_CONTROL\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_ASG_CTL, ARRAY_SIZE(SEQ_ASG_CTL));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_ASG_CTL\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_SOURCE_CONTROL, ARRAY_SIZE(SEQ_SOURCE_CONTROL));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_SOURCE_CONTROL\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_MIE_CTRL1, ARRAY_SIZE(SEQ_MIE_CTRL1));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_MIE_CTRL1\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_MIE_CTRL2, ARRAY_SIZE(SEQ_MIE_CTRL2));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_MIE_CTRL2\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_WRITE_DISPLAY_BRIGHTNESS, ARRAY_SIZE(SEQ_WRITE_DISPLAY_BRIGHTNESS));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_WRITE_DISPLAY_BRIGHTNESS\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_WRITE_CONTROL_DISPLAY, ARRAY_SIZE(SEQ_WRITE_CONTROL_DISPLAY));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_WRITE_CONTROL_DISPLAY\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_WRCABC, ARRAY_SIZE(SEQ_WRCABC));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_WRCABC\n", __func__);
		goto init_exit;
	}

	/* init seq 3 */

	ret = dsim_write_hl_data(lcd, SEQ_POSITIVE_GAMMA_CONTROL, ARRAY_SIZE(SEQ_POSITIVE_GAMMA_CONTROL));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_POSITIVE_GAMMA_CONTROL\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_NEGATIVE_GAMMA_CONTROL, ARRAY_SIZE(SEQ_NEGATIVE_GAMMA_CONTROL));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_NEGATIVE_GAMMA_CONTROL\n", __func__);
		goto init_exit;
	}

	/* init seq 1-1 */

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

	ret = dsim_write_hl_data(lcd, SEQ_SLEEP_OUT, ARRAY_SIZE(SEQ_SLEEP_OUT));
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: failed to write CMD : SEQ_SLEEP_OUT\n", __func__);
		goto init_exit;
	}
	msleep(120);

init_exit:
	return ret;
}

static int lm3632_probe(struct i2c_client *client,
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

	lcd->blic_client = client;

	lcd->backlight_reset[0] = of_get_gpio(client->dev.of_node, 0);
	lcd->backlight_reset[1] = of_get_gpio(client->dev.of_node, 1);
	lcd->backlight_reset[2] = of_get_gpio(client->dev.of_node, 2);

	dev_info(&lcd->ld->dev, "%s: %s %s\n", __func__, dev_name(&client->adapter->dev), of_node_full_name(client->dev.of_node));

exit:
	return ret;
}

static struct i2c_device_id lm3632_i2c_id[] = {
	{"lm3632", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, lm3632_i2c_id);

static const struct of_device_id lm3632_i2c_dt_ids[] = {
	{ .compatible = "i2c,lm3632" },
	{ }
};

MODULE_DEVICE_TABLE(of, lm3632_i2c_dt_ids);

static struct i2c_driver lm3632_i2c_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "lm3632",
		.of_match_table	= of_match_ptr(lm3632_i2c_dt_ids),
	},
	.id_table = lm3632_i2c_id,
	.probe = lm3632_probe,
};

static int s6d7aa0_probe(struct lcd_info *lcd)
{
	int ret = 0;

	dev_info(&lcd->ld->dev, "+ %s\n", __func__);

	lm3632_i2c_id->driver_data = (kernel_ulong_t)lcd;
	i2c_add_driver(&lm3632_i2c_driver);

	lcd->bd->props.max_brightness = EXTEND_BRIGHTNESS;
	lcd->bd->props.brightness = UI_DEFAULT_BRIGHTNESS;

	lcd->state = PANEL_STATE_RESUMED;
	lcd->lux = -1;

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
	struct i2c_client *clients[] = {lcd->blic_client, NULL};

	ret = sysfs_create_group(&lcd->ld->dev.kobj, &lcd_sysfs_attr_group);
	if (ret < 0)
		dev_info(&lcd->ld->dev, "failed to add lcd sysfs\n");

	init_debugfs_backlight(lcd->bd, NULL, clients);
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
	ret = s6d7aa0_probe(lcd);
	if (ret < 0)
		dev_info(&lcd->ld->dev, "%s: failed to probe panel\n", __func__);

	lcd_init_sysfs(lcd);

	dev_info(&lcd->ld->dev, "%s: %s: done\n", kbasename(__FILE__), __func__);
probe_err:
	return ret;
}

static int dsim_panel_resume(struct dsim_device *dsim)
{
	struct lcd_info *lcd = dsim->priv.par;
	int ret = 0;

	dev_info(&lcd->ld->dev, "+ %s\n", __func__);

	if (lcd->state == PANEL_STATE_SUSPENED) {
		ret = gpio_request_one(lcd->backlight_reset[0], GPIOF_OUT_INIT_HIGH, "BL_power");
		gpio_free(lcd->backlight_reset[0]);
		usleep_range(10000, 11000);
		// blic_setting > enp > enn
		lm3632_array_write(lcd->blic_client, backlight_ic_tuning, ARRAY_SIZE(backlight_ic_tuning));

		ret = gpio_request_one(lcd->backlight_reset[1], GPIOF_OUT_INIT_HIGH, "PANEL_ENP");
		gpio_free(lcd->backlight_reset[1]);
		usleep_range(1100, 1200); //min 1ms

		ret = gpio_request_one(lcd->backlight_reset[2], GPIOF_OUT_INIT_HIGH, "PANEL_ENN");
		gpio_free(lcd->backlight_reset[2]);

		msleep(40);

		if (ret < 0)
			dev_info(&lcd->ld->dev, "%s: failed to panel init\n", __func__);
	}

	dev_info(&lcd->ld->dev, "- %s: %d, %d\n", __func__, lcd->state, lcd->connected);

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
	.resume		= dsim_panel_resume,
	.suspend	= dsim_panel_suspend,
};

