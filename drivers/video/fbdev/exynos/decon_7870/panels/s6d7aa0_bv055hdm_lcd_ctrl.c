/* linux/drivers/video/backlight/s6d7aa0_degas2_mipi_lcd.c
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

#include "s6d7aa0_bv055hdm_param.h"


struct i2c_client *tc358764_client;

#define S6D7AA0_ID_REG			0xD8	/* LCD ID1,ID2,ID3 */
#define S6D7AA0_ID_LEN			3
#define BRIGHTNESS_REG			0x51
#define LEVEL_IS_HBM(brightness)	(brightness == EXTEND_BRIGHTNESS)

#define FIRST_BOOT 3

#define DSI_WRITE(cmd, size)		do {				\
	ret = dsim_write_hl_data(lcd, cmd, size);			\
	if (ret < 0) {							\
		dev_err(&lcd->ld->dev, "%s: failed to write %s\n", __func__, #cmd);	\
		ret = -EPERM;						\
		goto exit;						\
	}								\
} while (0)


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

	unsigned char			id[3];
	unsigned char			dump_info[3];

	struct dsim_device		*dsim;
	struct mutex			lock;

	struct pwm_device		*pwm;
	unsigned int			pwm_period;
	unsigned int			pwm_min;
	unsigned int			pwm_max;
};

struct i2c_client *backlight_client;
int	backlight_reset[3];


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

	DSI_WRITE(wbuf, ARRAY_SIZE(wbuf));

	ret = dsim_read_hl_data(lcd, addr, size, buf);

	if (ret < 1)
		dev_err(&lcd->ld->dev, "%s: fail\n", __func__);

exit:
	return ret;
}

static int lm3632_array_write(const struct LM3632_rom_data *eprom_ptr, int eprom_size)
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

	return 0;
}
static int dsim_panel_set_brightness(struct lcd_info *lcd, int force)
{
	int ret = 0, temp = 0;
	unsigned char bl_reg[2];

	mutex_lock(&lcd->lock);

	lcd->brightness = lcd->bd->props.brightness;

	lcd->bl = lcd->brightness;
	lcd->bl = (lcd->bl > UI_MAX_BRIGHTNESS) ? UI_MAX_BRIGHTNESS : lcd->bl;

	if (!force && lcd->state != PANEL_STATE_RESUMED) {
		dev_info(&lcd->ld->dev, "%s: panel is not active state\n", __func__);
		goto exit;
	}

	bl_reg[0] = BRIGHTNESS_REG;

	if (LEVEL_IS_HBM(lcd->brightness)) { //HBM
		lm3632_array_write(backlight_ic_tuning_outdoor, ARRAY_SIZE(backlight_ic_tuning_outdoor));
		lcd->current_hbm = 1;
		dsim_write_hl_data(lcd, SEQ_PASSWD1, sizeof(SEQ_PASSWD1));
		dsim_write_hl_data(lcd, OUTDOOR_MDNIE_1, sizeof(OUTDOOR_MDNIE_1));
		dsim_write_hl_data(lcd, OUTDOOR_MDNIE_2, sizeof(OUTDOOR_MDNIE_2));
		dsim_write_hl_data(lcd, OUTDOOR_MDNIE_3, sizeof(OUTDOOR_MDNIE_3));
		dsim_write_hl_data(lcd, OUTDOOR_MDNIE_4, sizeof(OUTDOOR_MDNIE_4));
		dsim_write_hl_data(lcd, OUTDOOR_MDNIE_5, sizeof(OUTDOOR_MDNIE_5));
		dsim_write_hl_data(lcd, OUTDOOR_MDNIE_6, sizeof(OUTDOOR_MDNIE_6));
		dsim_write_hl_data(lcd, SEQ_PASSWD1_LOCK, sizeof(SEQ_PASSWD1_LOCK));

		bl_reg[1] = 0xff ;
		dev_info(&lcd->ld->dev, "%s: Back light IC outdoor mode : %d\n", __func__, lcd->current_hbm);
	} else {
		if (lcd->current_hbm == 1 || lcd->current_hbm == FIRST_BOOT) {
			dsim_write_hl_data(lcd, SEQ_PASSWD1, sizeof(SEQ_PASSWD1));
			dsim_write_hl_data(lcd, UI_MDNIE_1, sizeof(UI_MDNIE_1));
			dsim_write_hl_data(lcd, UI_MDNIE_2, sizeof(UI_MDNIE_2));
			dsim_write_hl_data(lcd, UI_MDNIE_3, sizeof(UI_MDNIE_3));
			dsim_write_hl_data(lcd, UI_MDNIE_4, sizeof(UI_MDNIE_4));
			dsim_write_hl_data(lcd, UI_MDNIE_5, sizeof(UI_MDNIE_5));
			dsim_write_hl_data(lcd, UI_MDNIE_6, sizeof(UI_MDNIE_6));
			dsim_write_hl_data(lcd, SEQ_PASSWD1_LOCK, sizeof(SEQ_PASSWD1_LOCK));
			lm3632_array_write(backlight_ic_tuning_normal, ARRAY_SIZE(backlight_ic_tuning_normal));
			lcd->current_hbm = 0;
			dev_info(&lcd->ld->dev, "%s: Back light IC outdoor mode : %d\n", __func__, lcd->current_hbm);
		}

		if (lcd->bl >= 3)
			if(lcd->bl <= DIM_BRIGHTNESS){
				bl_reg[1] = lcd->bl;
			}
			else if(lcd->bl <= UI_DEFAULT_BRIGHTNESS){ //92lv == 191cd
				temp = (lcd->bl - DIM_BRIGHTNESS) * (DEFAULT_CANDELA - DIM_BRIGHTNESS);
				temp /= (UI_DEFAULT_BRIGHTNESS - DIM_BRIGHTNESS);
				temp += DIM_BRIGHTNESS;
				bl_reg[1] = temp;
			}
			else{
				temp = (lcd->bl - UI_DEFAULT_BRIGHTNESS) * (0xCD - DEFAULT_CANDELA);
				temp /= (0xFF - UI_DEFAULT_BRIGHTNESS);
				temp += DEFAULT_CANDELA;
				if(temp > 0xCD)
					bl_reg[1] = 0xCD;
				else
					bl_reg[1] = temp;
			}

		else if(lcd->bl >= 1)
			bl_reg[1] = 0x02;
		else
			bl_reg[1] = 0x00;

		dev_info(&lcd->ld->dev, "%s: platform BL : %d panel BL reg : %d\n", __func__, lcd->bd->props.brightness, bl_reg[1]);
	}

	if (dsim_write_hl_data(lcd, bl_reg, ARRAY_SIZE(bl_reg)) < 0)
		dev_err(&lcd->ld->dev, "%s: failed to write brightness cmd.\n", __func__);
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


static int s6d7aa0_read_init_info(struct lcd_info *lcd)
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
	for (i = 0; i < S6D7AA0_ID_LEN; i++)
		dev_info(&lcd->ld->dev, "%02x, ", lcd->id[i]);
	dev_info(&lcd->ld->dev, "\n");

read_exit:
	return 0;
}

static int s6d7aa0_displayon(struct lcd_info *lcd)
{
	int ret = 0;

	dev_info(&lcd->ld->dev, "%s\n", __func__);

	ret = dsim_write_hl_data(lcd, SEQ_DISPLAY_ON, ARRAY_SIZE(SEQ_DISPLAY_ON));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : DISPLAY_ON\n", __func__);
		goto displayon_err;
	}

	usleep_range(20000, 21000); // wait 20ms


	ret = dsim_write_hl_data(lcd, SEQ_PASSWD1, sizeof(SEQ_PASSWD1));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_PASSWD1\n", __func__);
		goto displayon_err;
	}
	ret = dsim_write_hl_data(lcd, UI_MDNIE_1, sizeof(UI_MDNIE_1));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : UI_MDNIE_1\n", __func__);
		goto displayon_err;
	}
	ret = dsim_write_hl_data(lcd, UI_MDNIE_2, sizeof(UI_MDNIE_2));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : UI_MDNIE_2\n", __func__);
		goto displayon_err;
	}
	ret = dsim_write_hl_data(lcd, UI_MDNIE_3, sizeof(UI_MDNIE_3));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : UI_MDNIE_3\n", __func__);
		goto displayon_err;
	}
	ret = dsim_write_hl_data(lcd, UI_MDNIE_4, sizeof(UI_MDNIE_4));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : UI_MDNIE_4\n", __func__);
		goto displayon_err;
	}
	ret = dsim_write_hl_data(lcd, UI_MDNIE_5, sizeof(UI_MDNIE_5));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : UI_MDNIE_5\n", __func__);
		goto displayon_err;
	}
	ret = dsim_write_hl_data(lcd, UI_MDNIE_6, sizeof(UI_MDNIE_6));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : UI_MDNIE_6\n", __func__);
		goto displayon_err;
	}
	ret = dsim_write_hl_data(lcd, SEQ_PASSWD1_LOCK, sizeof(SEQ_PASSWD1_LOCK));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_PASSWD1_LOCK\n", __func__);
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
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : DISPLAY_OFF\n", __func__);
		goto exit_err;
	}

	msleep(50);

	ret = dsim_write_hl_data(lcd, SEQ_SLEEP_IN, ARRAY_SIZE(SEQ_SLEEP_IN));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SLEEP_IN\n", __func__);
		goto exit_err;
	}

	msleep(120);

//	lm3632_array_write(LM3632_eprom_drv_arr_off, ARRAY_SIZE(LM3632_eprom_drv_arr_off));
	if (backlight_reset[0]) {

		lm3632_array_write(backlight_ic_tuning, ARRAY_SIZE(backlight_ic_tuning));


	}

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
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_PASSWD1\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_PASSWD2, ARRAY_SIZE(SEQ_PASSWD2));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_PASSWD2\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_PASSWD3, ARRAY_SIZE(SEQ_PASSWD3));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_PASSWD3\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_OTP_CTRL, ARRAY_SIZE(SEQ_OTP_CTRL));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_OTP_CTRL\n", __func__);
		goto init_exit;
	}

	/* init seq 2 */
	ret = dsim_write_hl_data(lcd, SEQ_PENEL_CONTROL, ARRAY_SIZE(SEQ_PENEL_CONTROL));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_PENEL_CONTROL\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_INTERFACE_CONTROL, ARRAY_SIZE(SEQ_INTERFACE_CONTROL));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_INTERFACE_CONTROL\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_CLK_CONTROL, ARRAY_SIZE(SEQ_CLK_CONTROL));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_CLK_CONTROL\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_SMPSCTL, ARRAY_SIZE(SEQ_SMPSCTL));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_SMPSCTL\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_BAT_CONTROL, ARRAY_SIZE(SEQ_BAT_CONTROL));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_BAT_CONTROL\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_MIE_CONTROL1, ARRAY_SIZE(SEQ_MIE_CONTROL1));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_MIE_CONTROL1\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_BACKLIGHT_CONTROL, ARRAY_SIZE(SEQ_BACKLIGHT_CONTROL));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_PSEQ_BACKLIGHT_CONTROLORCH_CTL\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_MIE_CONTROL2, ARRAY_SIZE(SEQ_MIE_CONTROL2));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_MIE_CONTROL2\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_BL_CONTROL, ARRAY_SIZE(SEQ_BL_CONTROL));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_BL_CONTROL\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_ASG_EQ, ARRAY_SIZE(SEQ_ASG_EQ));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_ASG_EQ\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_DISPLAY_CONTROL, ARRAY_SIZE(SEQ_DISPLAY_CONTROL));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_DISPLAY_CONTROL\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_MANPWR_SETTING, ARRAY_SIZE(SEQ_MANPWR_SETTING));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_MANPWR_SETTING\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_PANEL_GATE_CONTROL, ARRAY_SIZE(SEQ_PANEL_GATE_CONTROL));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_PANEL_GATE_CONTROL\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_ASG_CTL, ARRAY_SIZE(SEQ_ASG_CTL));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_ASG_CTL\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_SOURCE_CONTROL, ARRAY_SIZE(SEQ_SOURCE_CONTROL));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_SOURCE_CONTROL\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_MIE_CTRL1, ARRAY_SIZE(SEQ_MIE_CTRL1));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_MIE_CTRL1\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_MIE_CTRL2, ARRAY_SIZE(SEQ_MIE_CTRL2));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_MIE_CTRL2\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_WRITE_DISPLAY_BRIGHTNESS, ARRAY_SIZE(SEQ_WRITE_DISPLAY_BRIGHTNESS));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_WRITE_DISPLAY_BRIGHTNESS\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_WRITE_CONTROL_DISPLAY, ARRAY_SIZE(SEQ_WRITE_CONTROL_DISPLAY));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_WRITE_CONTROL_DISPLAY\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_WRCABC, ARRAY_SIZE(SEQ_WRCABC));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_WRCABC\n", __func__);
		goto init_exit;
	}

	/* init seq 3 */

	ret = dsim_write_hl_data(lcd, SEQ_POSITIVE_GAMMA_CONTROL, ARRAY_SIZE(SEQ_POSITIVE_GAMMA_CONTROL));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_POSITIVE_GAMMA_CONTROL\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_NEGATIVE_GAMMA_CONTROL, ARRAY_SIZE(SEQ_NEGATIVE_GAMMA_CONTROL));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_NEGATIVE_GAMMA_CONTROL\n", __func__);
		goto init_exit;
	}

	/* init seq 1-1 */

	ret = dsim_write_hl_data(lcd, SEQ_PASSWD1_LOCK, ARRAY_SIZE(SEQ_PASSWD1_LOCK));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_PASSWD1_LOCK\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_PASSWD2_LOCK, ARRAY_SIZE(SEQ_PASSWD2_LOCK));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_PASSWD2_LOCK\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_PASSWD3_LOCK, ARRAY_SIZE(SEQ_PASSWD3_LOCK));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_PASSWD3_LOCK\n", __func__);
		goto init_exit;
	}

	ret = dsim_write_hl_data(lcd, SEQ_SLEEP_OUT, ARRAY_SIZE(SEQ_SLEEP_OUT));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_SLEEP_OUT\n", __func__);
		goto init_exit;
	}
	msleep(120);

init_exit:
	return ret;
}

static int lm3632_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int ret = 0;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dsim_err("led : need I2C_FUNC_I2C.\n");
		ret = -ENODEV;
		goto err_i2c;
	}

	backlight_client = client;

	backlight_reset[0] = of_get_gpio(client->dev.of_node, 0);
	backlight_reset[1] = of_get_gpio(client->dev.of_node, 1);
	backlight_reset[2] = of_get_gpio(client->dev.of_node, 2);

err_i2c:
	return ret;
}

static struct i2c_device_id lm3632_id[] = {
	{"lm3632", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, lm3632_id);

static struct of_device_id lm3632_i2c_dt_ids[] = {
	{ .compatible = "lm3632,i2c" },
	{ }
};

MODULE_DEVICE_TABLE(of, lm3632_i2c_dt_ids);

static struct i2c_driver lm3632_i2c_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "lm3632",
		.of_match_table	= of_match_ptr(lm3632_i2c_dt_ids),
	},
	.id_table = lm3632_id,
	.probe = lm3632_probe,
};

static int s6d7aa0_probe(struct dsim_device *dsim)
{
	int ret = 0;
	struct panel_private *priv = &dsim->priv;
	struct lcd_info *lcd = dsim->priv.par;

	dev_info(&lcd->ld->dev, "%s: was called\n", __func__);

	i2c_add_driver(&lm3632_i2c_driver);

	priv->lcdConnected = PANEL_CONNECTED;

	lcd->bd->props.max_brightness = EXTEND_BRIGHTNESS;
	lcd->bd->props.brightness = UI_DEFAULT_BRIGHTNESS;

	lcd->dsim = dsim;
	lcd->state = PANEL_STATE_RESUMED;

	lcd->temperature = NORMAL_TEMPERATURE;
	lcd->acl_enable = 0;
	lcd->current_acl = 0;
	lcd->siop_enable = 0;
	lcd->current_hbm = FIRST_BOOT;

	s6d7aa0_read_init_info(lcd);
	if (priv->lcdConnected == PANEL_DISCONNEDTED) {
		dev_err(&lcd->ld->dev, "dsim : %s lcd was not connected\n", __func__);
		goto exit;
	}

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

static ssize_t dump_register_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);
	char *pos = buf;
	u8 reg, len, offset;
	int ret, i;
	u8 *dump = NULL;

	reg = lcd->dump_info[0];
	len = lcd->dump_info[1];
	offset = lcd->dump_info[2];

	if (!reg || !len || reg > 0xff || len > 255 || offset > 255)
		goto exit;

	dump = kcalloc(len, sizeof(u8), GFP_KERNEL);

	if (lcd->state == PANEL_STATE_RESUMED) {
		DSI_WRITE(SEQ_PASSWD1, ARRAY_SIZE(SEQ_PASSWD1));
		DSI_WRITE(SEQ_PASSWD3, ARRAY_SIZE(SEQ_PASSWD3));

		if (offset)
			ret = dsim_read_hl_data_offset(lcd, reg, len, dump, offset);
		else
			ret = dsim_read_hl_data(lcd, reg, len, dump);

		DSI_WRITE(SEQ_PASSWD3_LOCK, ARRAY_SIZE(SEQ_PASSWD3_LOCK));
		DSI_WRITE(SEQ_PASSWD1_LOCK, ARRAY_SIZE(SEQ_PASSWD1_LOCK));
	}

	pos += sprintf(pos, "+ [%02X]\n", reg);
	for (i = 0; i < len; i++)
		pos += sprintf(pos, "%2d: %02x\n", i + offset + 1, dump[i]);
	pos += sprintf(pos, "- [%02X]\n", reg);

	dev_info(&lcd->ld->dev, "+ [%02X]\n", reg);
	for (i = 0; i < len; i++)
		dev_info(&lcd->ld->dev, "%2d: %02x\n", i + offset + 1, dump[i]);
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

	ret = sscanf(buf, "%x %d %d", &reg, &len, &offset);

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
static DEVICE_ATTR(siop_enable, 0664, siop_enable_show, siop_enable_store);
static DEVICE_ATTR(power_reduce, 0664, power_reduce_show, power_reduce_store);
static DEVICE_ATTR(temperature, 0664, temperature_show, temperature_store);
static DEVICE_ATTR(dump_register, 0644, dump_register_show, dump_register_store);

static struct attribute *lcd_sysfs_attributes[] = {
	&dev_attr_siop_enable.attr,
	&dev_attr_power_reduce.attr,
	&dev_attr_temperature.attr,
	&dev_attr_lcd_type.attr,
	&dev_attr_window_type.attr,
	&dev_attr_dump_register.attr,
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

	ret = s6d7aa0_probe(dsim);
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

static int dsim_panel_resume(struct dsim_device *dsim)
{
	struct lcd_info *lcd = dsim->priv.par;
	struct panel_private *priv = &lcd->dsim->priv;
	int ret = 0;

	dev_info(&lcd->ld->dev, "+%s\n", __func__);
	if (lcd->state == PANEL_STATE_SUSPENED) {


		ret = gpio_request_one(backlight_reset[0], GPIOF_OUT_INIT_HIGH, "BL_power");
		gpio_free(backlight_reset[0]);
		usleep_range(10000, 11000);
		// blic_setting > enp > enn
		lm3632_array_write(backlight_ic_tuning, ARRAY_SIZE(backlight_ic_tuning));

		ret = gpio_request_one(backlight_reset[1], GPIOF_OUT_INIT_HIGH, "PANEL_ENP");
		gpio_free(backlight_reset[1]);
		usleep_range(1100, 1200); //min 1ms

		ret = gpio_request_one(backlight_reset[2], GPIOF_OUT_INIT_HIGH, "PANEL_ENN");
		gpio_free(backlight_reset[2]);

		msleep(40);

		if (ret) {
			dev_info(&lcd->ld->dev, "%s: failed to panel init\n", __func__);
			goto resume_err;
		}
	}

resume_err:
	dev_info(&lcd->ld->dev, "-%s: %d\n", __func__, priv->lcdConnected);
	return ret;
}
static int dsim_panel_displayon(struct dsim_device *dsim)
{
	struct lcd_info *lcd = dsim->priv.par;
	struct panel_private *priv = &lcd->dsim->priv;
	int ret = 0;

	dev_info(&lcd->ld->dev, "+%s\n", __func__);

	if (lcd->state == PANEL_STATE_SUSPENED) {
		ret = s6d7aa0_init(lcd);
		if (ret) {
			dev_info(&lcd->ld->dev, "%s: failed to panel init\n", __func__);
			goto displayon_err;
		}
	}

	ret = s6d7aa0_displayon(lcd);
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

	ret = s6d7aa0_exit(lcd);
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

struct mipi_dsim_lcd_driver s6d7aa0_mipi_lcd_driver = {
	.probe		= dsim_panel_probe,
	.displayon	= dsim_panel_displayon,
	.resume		= dsim_panel_resume,
	.suspend	= dsim_panel_suspend,
};

