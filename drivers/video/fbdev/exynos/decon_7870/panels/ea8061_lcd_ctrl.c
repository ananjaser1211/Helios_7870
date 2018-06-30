/*
 * drivers/video/decon_7580/panels/ea8061_lcd_ctrl.c
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
#include <video/mipi_display.h>
#include "../dsim.h"
#include "dsim_panel.h"


#include "ea8061_param.h"

#ifdef CONFIG_PANEL_AID_DIMMING
#include "aid_dimming.h"
#include "dimming_core.h"
#include "ea8061_aid_dimming.h"
#endif

struct lcd_info {
	struct lcd_device *ld;
	struct backlight_device *bd;
	unsigned char id[3];
	unsigned char code[5];
	unsigned char tset[8];
	unsigned char elvss[4];
	unsigned char DB[56];
	unsigned char B2[7];
	unsigned char D4[18];

	int	temperature;
	unsigned int coordinate[2];
	unsigned char date[7];
	unsigned int state;
	unsigned int brightness;
	unsigned int br_index;
	unsigned int acl_enable;
	unsigned int current_acl;
	unsigned int current_hbm;
	unsigned int siop_enable;
	unsigned char dump_info[3];

	void *dim_data;
	void *dim_info;
	unsigned int *br_tbl;
	unsigned char **hbm_tbl;
	unsigned char **acl_cutoff_tbl;
	unsigned char **acl_opr_tbl;
	struct mutex lock;
	struct dsim_device *dsim;
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

static int ea8061_read_id(struct lcd_info *lcd)
{
	struct panel_private *priv = &lcd->dsim->priv;
	int ret = 0;
	int i = 0;

	priv->lcdConnected = PANEL_CONNECTED;

	dsim_write_hl_data(lcd, SEQ_EA8061_READ_ID, ARRAY_SIZE(SEQ_EA8061_READ_ID));
	ret = dsim_read_hl_data(lcd, EA8061_READ_RX_REG, EA8061_ID_LEN, lcd->id);
	if (ret != EA8061_ID_LEN) {
		dev_err(&lcd->ld->dev, "%s: can't find connected panel. check panel connection\n", __func__);
		priv->lcdConnected = PANEL_DISCONNEDTED;
		goto exit;
	}

	dev_info(&lcd->ld->dev, "READ ID : ");
	for (i = 0; i < EA8061_ID_LEN; i++)
		dev_info(&lcd->ld->dev, "%02x, ", lcd->id[i]);
	dev_info(&lcd->ld->dev, "\n");

exit:
	return ret;
}

#ifdef CONFIG_PANEL_AID_DIMMING
static unsigned int get_actual_br_value(struct lcd_info *lcd, int index)
{
	struct SmtDimInfo *dimming_info = (struct SmtDimInfo *)lcd->dim_info;

	if (dimming_info == NULL) {
		dev_err(&lcd->ld->dev, "%s: dimming info is NULL\n", __func__);
		goto get_br_err;
	}

	if (index > MAX_BR_INFO)
		index = MAX_BR_INFO;

	return dimming_info[index].br;

get_br_err:
	return 0;
}

static unsigned char *get_gamma_from_index(struct lcd_info *lcd, int index)
{
	struct SmtDimInfo *dimming_info = (struct SmtDimInfo *)lcd->dim_info;

	if (dimming_info == NULL) {
		dev_err(&lcd->ld->dev, "%s: dimming info is NULL\n", __func__);
		goto get_gamma_err;
	}

	if (index > MAX_BR_INFO)
		index = MAX_BR_INFO;

	return (unsigned char *)dimming_info[index].gamma;

get_gamma_err:
	return NULL;
}

static unsigned char *get_aid_from_index(struct lcd_info *lcd, int index)
{
	struct SmtDimInfo *dimming_info = (struct SmtDimInfo *)lcd->dim_info;

	if (dimming_info == NULL) {
		dev_err(&lcd->ld->dev, "%s: dimming info is NULL\n", __func__);
		goto get_aid_err;
	}

	if (index > MAX_BR_INFO)
		index = MAX_BR_INFO;

	return (u8 *)dimming_info[index].aid;

get_aid_err:
	return NULL;
}

static unsigned char *get_elvss_from_index(struct lcd_info *lcd, int index, int acl)
{
	struct SmtDimInfo *dimming_info = (struct SmtDimInfo *)lcd->dim_info;

	if (dimming_info == NULL) {
		dev_err(&lcd->ld->dev, "%s: dimming info is NULL\n", __func__);
		goto get_elvess_err;
	}

	if (acl)
		return (unsigned char *)dimming_info[index].elvAcl;
	else
		return (unsigned char *)dimming_info[index].elv;

get_elvess_err:
	return NULL;
}

static void dsim_panel_gamma_ctrl(struct lcd_info *lcd)
{
	int level = LEVEL_IS_HBM(lcd->brightness), i = 0;
	u8 HBM_W[33] = {0xCA, };
	u8 *gamma = NULL;

	if (level) {
		memcpy(&HBM_W[1], lcd->DB, 21);
		for (i = 22; i <= 30; i++)
			HBM_W[i] = 0x80;
		HBM_W[31] = 0x00;
		HBM_W[32] = 0x00;

		if (dsim_write_hl_data(lcd, HBM_W, ARRAY_SIZE(HBM_W)) < 0)
			dev_err(&lcd->ld->dev, "%s: failed to write gamma\n", __func__);

	} else {
		gamma = get_gamma_from_index(lcd, lcd->br_index);
		if (gamma == NULL) {
			dev_err(&lcd->ld->dev, "%s: failed to get gamma\n", __func__);
			return;
		}
		if (dsim_write_hl_data(lcd, gamma, GAMMA_CMD_CNT) < 0)
			dev_err(&lcd->ld->dev, "%s: failed to write gamma\n", __func__);
	}

}

static void dsim_panel_aid_ctrl(struct lcd_info *lcd)
{
	u8 *aid = NULL;

	aid = get_aid_from_index(lcd, lcd->br_index);
	if (aid == NULL) {
		dev_err(&lcd->ld->dev, "%s: failed to get aid value\n", __func__);
		return;
	}
	if (dsim_write_hl_data(lcd, aid, AID_CMD_CNT) < 0)
		dev_err(&lcd->ld->dev, "%s: failed to write gamma\n", __func__);
}

static void dsim_panel_set_elvss(struct lcd_info *lcd)
{
	u8 *elvss = NULL;
	u8 B2_W[8] = {0xB2, };
	u8 D4_W[19] = {0xD4, };
	int tset = 0;
	int real_br = get_actual_br_value(lcd, lcd->br_index);
	int level = LEVEL_IS_HBM(lcd->brightness);

	tset = ((lcd->temperature >= 0) ? 0 : BIT(7)) | abs(lcd->temperature);

	elvss = get_elvss_from_index(lcd, lcd->br_index, lcd->acl_enable);
	if (elvss == NULL) {
		dev_err(&lcd->ld->dev, "%s: failed to get elvss value\n", __func__);
		return;
	}

	if (level) {
		memcpy(&D4_W[1], SEQ_EA8061_ELVSS_SET_HBM_D4, ARRAY_SIZE(SEQ_EA8061_ELVSS_SET_HBM_D4));
		if (lcd->temperature > 0)
			D4_W[3] = 0x48;
		else
			D4_W[3] = 0x4C;

		D4_W[18] = lcd->DB[33];

		if (dsim_write_hl_data(lcd, SEQ_EA8061_ELVSS_SET_HBM, ARRAY_SIZE(SEQ_EA8061_ELVSS_SET_HBM)) < 0)
			dev_err(&lcd->ld->dev, "%s: failed to SEQ_EA8061_ELVSS_SET_HBMelvss\n", __func__);

		if (dsim_write_hl_data(lcd, SEQ_TEST_KEY_ON_F1, ARRAY_SIZE(SEQ_TEST_KEY_ON_F1)) < 0)
			dev_err(&lcd->ld->dev, "%s: failed to SEQ_TEST_KEY_ON_F1\n", __func__);

		if (dsim_write_hl_data(lcd, D4_W, EA8061_MTP_D4_SIZE + 1) < 0)
			dev_err(&lcd->ld->dev, "%s: failed to write elvss\n", __func__);

		if (dsim_write_hl_data(lcd, SEQ_TEST_KEY_OFF_F1, ARRAY_SIZE(SEQ_TEST_KEY_OFF_F1)) < 0)
			dev_err(&lcd->ld->dev, "%s: failed to SEQ_TEST_KEY_OFF_F1\n", __func__);

	} else {
		memcpy(&B2_W[1], lcd->B2, EA8061_MTP_B2_SIZE);
		memcpy(&B2_W[1], SEQ_CAPS_ELVSS_DEFAULT, ARRAY_SIZE(SEQ_CAPS_ELVSS_DEFAULT));
		memcpy(&D4_W[1], lcd->D4, EA8061_MTP_D4_SIZE);

		B2_W[1] = elvss[0];
		B2_W[7] = tset;

		if (lcd->temperature > 0)
			D4_W[3] = 0x48;
		else
			D4_W[3] = 0x4C;

		if (real_br <= 29) {
			if (lcd->temperature > 0)
				D4_W[18] = elvss[1];
			else if (lcd->temperature > -20 && lcd->temperature < 0)
				D4_W[18] = elvss[2];
			else
				D4_W[18] = elvss[3];
		}
		if (dsim_write_hl_data(lcd, SEQ_TEST_KEY_ON_F1, ARRAY_SIZE(SEQ_TEST_KEY_ON_F1)) < 0)
			dev_err(&lcd->ld->dev, "%s: failed to SEQ_TEST_KEY_ON_F1\n", __func__);

		if (dsim_write_hl_data(lcd, B2_W, EA8061_MTP_B2_SIZE + 1) < 0)
			dev_err(&lcd->ld->dev, "%s: failed to write elvss\n", __func__);

		if (dsim_write_hl_data(lcd, D4_W, EA8061_MTP_D4_SIZE + 1) < 0)
			dev_err(&lcd->ld->dev, "%s: failed to write elvss\n", __func__);

		if (dsim_write_hl_data(lcd, SEQ_TEST_KEY_OFF_F1, ARRAY_SIZE(SEQ_TEST_KEY_OFF_F1)) < 0)
			dev_err(&lcd->ld->dev, "%s: failed to SEQ_TEST_KEY_OFF_F1\n", __func__);
	}

	dev_info(&lcd->ld->dev, "%s: %d Tset: %x Temp: %d\n", __func__, level, D4_W[3], lcd->temperature);
}

static int dsim_panel_set_acl(struct lcd_info *lcd, int force)
{
	int ret = 0, level = ACL_STATUS_8P;

	if (lcd->siop_enable || LEVEL_IS_HBM(lcd->brightness))
		goto acl_update;

	if (!lcd->acl_enable)
		level = ACL_STATUS_0P;

acl_update:
	if (force || lcd->current_acl != lcd->acl_cutoff_tbl[level][1]) {
		if (dsim_write_hl_data(lcd, lcd->acl_cutoff_tbl[level], 2) < 0) {
			dev_err(&lcd->ld->dev, "failed to write acl command.\n");
			goto exit;
		}

		lcd->current_acl = lcd->acl_cutoff_tbl[level][1];
		dev_info(&lcd->ld->dev, "acl: %d, brightness: %d\n", lcd->current_acl, lcd->brightness);
	}
exit:
	if (!ret)
		ret = -EPERM;
	return ret;
}

static int low_level_set_brightness(struct lcd_info *lcd, int force)
{
	int ret;

	ret = dsim_write_hl_data(lcd, SEQ_TEST_KEY_ON_F0, ARRAY_SIZE(SEQ_TEST_KEY_ON_F0));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_TEST_KEY_ON_F0\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_EA8061_LTPS_STOP, ARRAY_SIZE(SEQ_EA8061_LTPS_STOP));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_EA8061_LTPS_STOP\n", __func__);
		goto init_exit;
	}
	dsim_panel_gamma_ctrl(lcd);

	dsim_panel_aid_ctrl(lcd);

	ret = dsim_write_hl_data(lcd, SEQ_EA8061_LTPS_UPDATE, ARRAY_SIZE(SEQ_EA8061_LTPS_UPDATE));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_EA8061_LTPS_UPDATE\n", __func__);
		goto init_exit;
	}

	dsim_panel_set_elvss(lcd);

	dsim_panel_set_acl(lcd, force);

	if (dsim_write_hl_data(lcd, SEQ_TEST_KEY_OFF_F0, ARRAY_SIZE(SEQ_TEST_KEY_OFF_F0)) < 0)
		dev_err(&lcd->ld->dev, "%s: failed to write F0 on command\n", __func__);

init_exit:
	return 0;
}

static int get_acutal_br_index(struct lcd_info *lcd, int br)
{
	int i;
	int min;
	int gap;
	int index = 0;
	struct SmtDimInfo *dimming_info = lcd->dim_info;

	if (dimming_info == NULL) {
		dev_err(&lcd->ld->dev, "%s: dimming_info is NULL\n", __func__);
		return 0;
	}

	min = MAX_BRIGHTNESS;

	for (i = 0; i < MAX_BR_INFO; i++) {
		if (br > dimming_info[i].br)
			gap = br - dimming_info[i].br;
		else
			gap = dimming_info[i].br - br;

		if (gap == 0) {
			index = i;
			break;
		}

		if (gap < min) {
			min = gap;
			index = i;
		}
	}
	return index;
}
#endif

static int dsim_panel_set_brightness(struct lcd_info *lcd, int force)
{
	int ret = 0;

#ifdef CONFIG_PANEL_AID_DIMMING
	int p_br = lcd->bd->props.brightness;
	int acutal_br = 0;
	int real_br = 0;
	int prev_index = lcd->br_index;
	struct dim_data *dimming;
#endif

	/* for ea8061 */
	dimming = (struct dim_data *)lcd->dim_data;
	if ((dimming == NULL) || (lcd->br_tbl == NULL)) {
		dev_info(&lcd->ld->dev, "%s: this panel does not support dimming\n", __func__);
		return ret;
	}

	mutex_lock(&lcd->lock);

	lcd->brightness = p_br = lcd->bd->props.brightness;
	acutal_br = lcd->br_tbl[p_br];
	lcd->br_index = get_acutal_br_index(lcd, acutal_br);
	real_br = get_actual_br_value(lcd, lcd->br_index);
	lcd->acl_enable = ACL_IS_ON(real_br);

	if (lcd->siop_enable)					/* check auto acl */
		lcd->acl_enable = 1;

	if (lcd->state != PANEL_STATE_RESUMED) {
		dev_info(&lcd->ld->dev, "%s: panel is not active state\n", __func__);
		goto set_br_exit;
	}

	dev_info(&lcd->ld->dev, "%s: platform : %d, : mapping : %d, real : %d, index : %d\n",
		__func__, p_br, acutal_br, real_br, lcd->br_index);

	if (!force && lcd->br_index == prev_index)
		goto set_br_exit;

	if ((acutal_br == 0) || (real_br == 0))
		goto set_br_exit;

	ret = low_level_set_brightness(lcd, force);

	if (ret)
		dev_err(&lcd->ld->dev, "%s: failed to set brightness : %d\n", __func__, acutal_br);

set_br_exit:
	mutex_unlock(&lcd->lock);
	return ret;
}


static int panel_get_brightness(struct backlight_device *bd)
{
	struct lcd_info *lcd = bl_get_data(bd);

	return get_actual_br_value(lcd, lcd->br_index);
}


static int panel_set_brightness(struct backlight_device *bd)
{
	int ret = 0;
	struct lcd_info *lcd = bl_get_data(bd);

	ret = dsim_panel_set_brightness(lcd, 0);
	if (ret) {
		dev_err(&lcd->ld->dev, "%s: failed to set brightness\n", __func__);
		goto exit_set;
	}
exit_set:
	return ret;

}

static const struct backlight_ops panel_backlight_ops = {
	.get_brightness = panel_get_brightness,
	.update_status = panel_set_brightness,
};


#ifdef CONFIG_PANEL_AID_DIMMING
static const unsigned char *ACL_CUTOFF_TABLE_15[ACL_STATUS_MAX] = {SEQ_ACL_OFF, SEQ_ACL_15};

static const unsigned int br_tbl[EXTEND_BRIGHTNESS + 1] = {
	5, 5, 5, 5, 5, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 19, 20, 21, 22,
	24, 25, 27, 29, 30, 32, 34, 37, 39, 41, 41, 44, 44, 47, 47, 50, 50, 53, 53, 56,
	56, 56, 60, 60, 60, 64, 64, 64, 68, 68, 68, 72, 72, 72, 72, 77, 77, 77, 82,
	82, 82, 82, 87, 87, 87, 87, 93, 93, 93, 93, 98, 98, 98, 98, 98, 105, 105,
	105, 105, 111, 111, 111, 111, 111, 111, 119, 119, 119, 119, 119, 126, 126, 126,
	126, 126, 126, 134, 134, 134, 134, 134, 134, 134, 143, 143, 143, 143, 143, 143,
	152, 152, 152, 152, 152, 152, 152, 152, 162, 162, 162, 162, 162, 162, 162, 172,
	172, 172, 172, 172, 172, 172, 172, 183, 183, 183, 183, 183, 183, 183, 183, 183,
	195, 195, 195, 195, 195, 195, 195, 195, 207, 207, 207, 207, 207, 207, 207, 207,
	207, 207, 220, 220, 220, 220, 220, 220, 220, 220, 220, 220, 234, 234, 234, 234,
	234, 234, 234, 234, 234, 234, 234, 249, 249, 249, 249, 249, 249, 249, 249, 249,
	249, 249, 249, 265, 265, 265, 265, 265, 265, 265, 265, 265, 265, 265, 265, 282,
	282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 300, 300, 300, 300,
	300, 300, 300, 300, 300, 300, 300, 300, 316, 316, 316, 316, 316, 316, 316, 316,
	316, 316, 316, 316, 333, 333, 333, 333, 333, 333, 333, 333, 333, 333, 333, 333, 350,
	[UI_MAX_BRIGHTNESS + 1 ... EXTEND_BRIGHTNESS - 1] = 350,
	[EXTEND_BRIGHTNESS] = 500
};

static const short center_gamma[NUM_VREF][CI_MAX] = {
	{0x000, 0x000, 0x000},
	{0x080, 0x080, 0x080},
	{0x080, 0x080, 0x080},
	{0x080, 0x080, 0x080},
	{0x080, 0x080, 0x080},
	{0x080, 0x080, 0x080},
	{0x080, 0x080, 0x080},
	{0x080, 0x080, 0x080},
	{0x080, 0x080, 0x080},
	{0x100, 0x100, 0x100},
};

struct SmtDimInfo ea8061_dimming_info[MAX_BR_INFO] = {				/* add hbm array */
	{.br = 5, .cTbl = D_ctbl5nit, .aid = D_aid9685, .elvAcl = elv0, .elv = elv0, .m_gray = m_gray_005},
	{.br = 6, .cTbl = D_ctbl6nit, .aid = D_aid9585, .elvAcl = elv0, .elv = elv0, .m_gray = m_gray_006},
	{.br = 7, .cTbl = D_ctbl7nit, .aid = D_aid9523, .elvAcl = elv0, .elv = elv0, .m_gray = m_gray_007},
	{.br = 8, .cTbl = D_ctbl8nit, .aid = D_aid9438, .elvAcl = elv0, .elv = elv0, .m_gray = m_gray_008},
	{.br = 9, .cTbl = D_ctbl9nit, .aid = D_aid9338, .elvAcl = elv0, .elv = elv0, .m_gray = m_gray_009},
	{.br = 10, .cTbl = D_ctbl10nit, .aid = D_aid9285, .elvAcl = elv0, .elv = elv0, .m_gray = m_gray_010},
	{.br = 11, .cTbl = D_ctbl11nit, .aid = D_aid9200, .elvAcl = elv0, .elv = elv0, .m_gray = m_gray_011},
	{.br = 12, .cTbl = D_ctbl12nit, .aid = D_aid9100, .elvAcl = elv0, .elv = elv0, .m_gray = m_gray_012},
	{.br = 13, .cTbl = D_ctbl13nit, .aid = D_aid9046, .elvAcl = elv0, .elv = elv0, .m_gray = m_gray_013},
	{.br = 14, .cTbl = D_ctbl14nit, .aid = D_aid8954, .elvAcl = elv0, .elv = elv0, .m_gray = m_gray_014},
	{.br = 15, .cTbl = D_ctbl15nit, .aid = D_aid8923, .elvAcl = elv0, .elv = elv0, .m_gray = m_gray_015},
	{.br = 16, .cTbl = D_ctbl16nit, .aid = D_aid8800, .elvAcl = elv0, .elv = elv0, .m_gray = m_gray_016},
	{.br = 17, .cTbl = D_ctbl17nit, .aid = D_aid8715, .elvAcl = elv0, .elv = elv0, .m_gray = m_gray_017},
	{.br = 19, .cTbl = D_ctbl19nit, .aid = D_aid8546, .elvAcl = elv0, .elv = elv0, .m_gray = m_gray_019},
	{.br = 20, .cTbl = D_ctbl20nit, .aid = D_aid8462, .elvAcl = elv0, .elv = elv0, .m_gray = m_gray_020},
	{.br = 21, .cTbl = D_ctbl21nit, .aid = D_aid8346, .elvAcl = elv1, .elv = elv1, .m_gray = m_gray_021},
	{.br = 22, .cTbl = D_ctbl22nit, .aid = D_aid8246, .elvAcl = elv2, .elv = elv2, .m_gray = m_gray_022},
	{.br = 24, .cTbl = D_ctbl24nit, .aid = D_aid8085, .elvAcl = elv3, .elv = elv3, .m_gray = m_gray_024},
	{.br = 25, .cTbl = D_ctbl25nit, .aid = D_aid7969, .elvAcl = elv4, .elv = elv4, .m_gray = m_gray_025},
	{.br = 27, .cTbl = D_ctbl27nit, .aid = D_aid7769, .elvAcl = elv5, .elv = elv5, .m_gray = m_gray_027},
	{.br = 29, .cTbl = D_ctbl29nit, .aid = D_aid7577, .elvAcl = elv6, .elv = elv6, .m_gray = m_gray_029},
	{.br = 30, .cTbl = D_ctbl30nit, .aid = D_aid7508, .elvAcl = elv7, .elv = elv7, .m_gray = m_gray_030},
	{.br = 32, .cTbl = D_ctbl32nit, .aid = D_aid7323, .elvAcl = elv7, .elv = elv7, .m_gray = m_gray_032},
	{.br = 34, .cTbl = D_ctbl34nit, .aid = D_aid7138, .elvAcl = elv7, .elv = elv7, .m_gray = m_gray_034},
	{.br = 37, .cTbl = D_ctbl37nit, .aid = D_aid6892, .elvAcl = elv7, .elv = elv7, .m_gray = m_gray_037},
	{.br = 39, .cTbl = D_ctbl39nit, .aid = D_aid6715, .elvAcl = elv7, .elv = elv7, .m_gray = m_gray_039},
	{.br = 41, .cTbl = D_ctbl41nit, .aid = D_aid6531, .elvAcl = elv7, .elv = elv7, .m_gray = m_gray_041},
	{.br = 44, .cTbl = D_ctbl44nit, .aid = D_aid6262, .elvAcl = elv7, .elv = elv7, .m_gray = m_gray_044},
	{.br = 47, .cTbl = D_ctbl47nit, .aid = D_aid6000, .elvAcl = elv7, .elv = elv7, .m_gray = m_gray_047},
	{.br = 50, .cTbl = D_ctbl50nit, .aid = D_aid5731, .elvAcl = elv7, .elv = elv7, .m_gray = m_gray_050},
	{.br = 53, .cTbl = D_ctbl53nit, .aid = D_aid5454, .elvAcl = elv7, .elv = elv7, .m_gray = m_gray_053},
	{.br = 56, .cTbl = D_ctbl56nit, .aid = D_aid5177, .elvAcl = elv7, .elv = elv7, .m_gray = m_gray_056},
	{.br = 60, .cTbl = D_ctbl60nit, .aid = D_aid4800, .elvAcl = elv7, .elv = elv7, .m_gray = m_gray_060},
	{.br = 64, .cTbl = D_ctbl64nit, .aid = D_aid4438, .elvAcl = elv7, .elv = elv7, .m_gray = m_gray_064},
	{.br = 68, .cTbl = D_ctbl68nit, .aid = D_aid4062, .elvAcl = elv7, .elv = elv7, .m_gray = m_gray_068},
	{.br = 72, .cTbl = D_ctbl72nit, .aid = D_aid3662, .elvAcl = elv7, .elv = elv7, .m_gray = m_gray_072},
	{.br = 77, .cTbl = D_ctbl77nit, .aid = D_aid3662, .elvAcl = elv7, .elv = elv7, .m_gray = m_gray_077},
	{.br = 82, .cTbl = D_ctbl82nit, .aid = D_aid3662, .elvAcl = elv7, .elv = elv7, .m_gray = m_gray_082},
	{.br = 87, .cTbl = D_ctbl87nit, .aid = D_aid3662, .elvAcl = elv7, .elv = elv7, .m_gray = m_gray_087},
	{.br = 93, .cTbl = D_ctbl93nit, .aid = D_aid3662, .elvAcl = elv7, .elv = elv7, .m_gray = m_gray_093},
	{.br = 98, .cTbl = D_ctbl98nit, .aid = D_aid3662, .elvAcl = elv7, .elv = elv7, .m_gray = m_gray_098},
	{.br = 105, .cTbl = D_ctbl105nit, .aid = D_aid3662, .elvAcl = elv7, .elv = elv7, .m_gray = m_gray_105},
	{.br = 111, .cTbl = D_ctbl111nit, .aid = D_aid3662, .elvAcl = elv8, .elv = elv8, .m_gray = m_gray_111},
	{.br = 119, .cTbl = D_ctbl119nit, .aid = D_aid3662, .elvAcl = elv8, .elv = elv8, .m_gray = m_gray_119},
	{.br = 126, .cTbl = D_ctbl126nit, .aid = D_aid3662, .elvAcl = elv9, .elv = elv9, .m_gray = m_gray_126},
	{.br = 134, .cTbl = D_ctbl134nit, .aid = D_aid3662, .elvAcl = elv9, .elv = elv9, .m_gray = m_gray_134},
	{.br = 143, .cTbl = D_ctbl143nit, .aid = D_aid3662, .elvAcl = elv9, .elv = elv9, .m_gray = m_gray_143},
	{.br = 152, .cTbl = D_ctbl152nit, .aid = D_aid3662, .elvAcl = elv9, .elv = elv9, .m_gray = m_gray_152},
	{.br = 162, .cTbl = D_ctbl162nit, .aid = D_aid3662, .elvAcl = elv9, .elv = elv9, .m_gray = m_gray_162},
	{.br = 172, .cTbl = D_ctbl172nit, .aid = D_aid3662, .elvAcl = elv9, .elv = elv9, .m_gray = m_gray_172},
	{.br = 183, .cTbl = D_ctbl183nit, .aid = D_aid3200, .elvAcl = elv9, .elv = elv9, .m_gray = m_gray_183},
	{.br = 195, .cTbl = D_ctbl195nit, .aid = D_aid2708, .elvAcl = elv10, .elv = elv10, .m_gray = m_gray_195},
	{.br = 207, .cTbl = D_ctbl207nit, .aid = D_aid2185, .elvAcl = elv11, .elv = elv11, .m_gray = m_gray_207},
	{.br = 220, .cTbl = D_ctbl220nit, .aid = D_aid1654, .elvAcl = elv12, .elv = elv12, .m_gray = m_gray_220},
	{.br = 234, .cTbl = D_ctbl234nit, .aid = D_aid1038, .elvAcl = elv13, .elv = elv13, .m_gray = m_gray_234},
	{.br = 249, .cTbl = D_ctbl249nit, .aid = D_aid0369, .elvAcl = elv14, .elv = elv14, .m_gray = m_gray_249},
	{.br = 265, .cTbl = D_ctbl265nit, .aid = D_aid0369, .elvAcl = elv15, .elv = elv15, .m_gray = m_gray_265},
	{.br = 282, .cTbl = D_ctbl282nit, .aid = D_aid0369, .elvAcl = elv16, .elv = elv16, .m_gray = m_gray_282},
	{.br = 300, .cTbl = D_ctbl300nit, .aid = D_aid0369, .elvAcl = elv17, .elv = elv17, .m_gray = m_gray_300},
	{.br = 316, .cTbl = D_ctbl316nit, .aid = D_aid0369, .elvAcl = elv18, .elv = elv18, .m_gray = m_gray_316},
	{.br = 333, .cTbl = D_ctbl333nit, .aid = D_aid0369, .elvAcl = elv19, .elv = elv19, .m_gray = m_gray_333},
	{.br = 350, .cTbl = D_ctbl350nit, .aid = D_aid0369, .elvAcl = elv20, .elv = elv20, .m_gray = m_gray_350, .way = W2},
	{.br = 500, .cTbl = D_ctbl350nit, .aid = D_aid0369, .elvAcl = elv20, .elv = elv20, .m_gray = m_gray_350, .way = W2}
};

static int init_dimming(struct lcd_info *lcd, u8 *mtp)
{
	int i, j;
	int pos = 0;
	int ret = 0;
	short temp;
	struct dim_data *dimming;
	struct SmtDimInfo *diminfo = NULL;

	dimming = kzalloc(sizeof(struct dim_data), GFP_KERNEL);
	if (!dimming) {
		dev_err(&lcd->ld->dev, "failed to allocate memory for dim data\n");
		ret = -ENOMEM;
		goto error;
	}

	diminfo = ea8061_dimming_info;
	lcd->dim_data = (void *)dimming;
	lcd->dim_info = (void *)diminfo;
	lcd->br_tbl = (unsigned int *)br_tbl;
	lcd->acl_cutoff_tbl = (unsigned char **)ACL_CUTOFF_TABLE_15;

	for (j = 0; j < CI_MAX; j++) {
		if (mtp[pos] & 0x01)
			temp = mtp[pos+1] - 256;
		else
			temp = mtp[pos+1];

		dimming->t_gamma[V255][j] = (int)center_gamma[V255][j] + temp;
		dimming->mtp[V255][j] = temp;
		pos += 2;
	}

/* if ddi have V0 offset, plz modify to   for (i = V203; i >= V0; i--) {    */
	for (i = V203; i > V0; i--) {
		for (j = 0; j < CI_MAX; j++) {
			temp = ((mtp[pos] & 0x80) ? -1 : 1) * ((mtp[pos] & 0x80) ? 128 - (mtp[pos] & 0x7f) : (mtp[pos] & 0x7f));
			dimming->t_gamma[i][j] = (int)center_gamma[i][j] + temp;
			dimming->mtp[i][j] = temp;
			pos++;
		}
	}
	/* for vt */
	temp = (mtp[pos+1]) << 8 | mtp[pos];

	for (i = 0; i < CI_MAX; i++)
		dimming->vt_mtp[i] = (temp >> (i*4)) & 0x0f;
#ifdef SMART_DIMMING_DEBUG
	dimm_info("Center Gamma Info :\n");
	for (i = 0; i < VMAX; i++) {
		dev_info(&lcd->ld->dev, "Gamma : %3d %3d %3d : %3x %3x %3x\n",
			dimming->t_gamma[i][CI_RED], dimming->t_gamma[i][CI_GREEN], dimming->t_gamma[i][CI_BLUE],
			dimming->t_gamma[i][CI_RED], dimming->t_gamma[i][CI_GREEN], dimming->t_gamma[i][CI_BLUE]);
	}
#endif
	dimm_info("VT MTP :\n");
	dimm_info("Gamma : %3d %3d %3d : %3x %3x %3x\n",
			dimming->vt_mtp[CI_RED], dimming->vt_mtp[CI_GREEN], dimming->vt_mtp[CI_BLUE],
			dimming->vt_mtp[CI_RED], dimming->vt_mtp[CI_GREEN], dimming->vt_mtp[CI_BLUE]);

	dimm_info("MTP Info :\n");
	for (i = 0; i < VMAX; i++) {
		dimm_info("Gamma : %3d %3d %3d : %3x %3x %3x\n",
			dimming->mtp[i][CI_RED], dimming->mtp[i][CI_GREEN], dimming->mtp[i][CI_BLUE],
			dimming->mtp[i][CI_RED], dimming->mtp[i][CI_GREEN], dimming->mtp[i][CI_BLUE]);
	}

	ret = generate_volt_table(dimming);
	if (ret) {
		dimm_err("[ERR:%s] failed to generate volt table\n", __func__);
		goto error;
	}

	for (i = 0; i < MAX_BR_INFO; i++) {
		if (diminfo[i].way == DIMMING_METHOD_AID) {
			ret = cal_gamma_from_index(dimming, &diminfo[i]);
			if (ret) {
				dev_err(&lcd->ld->dev, "failed to calculate gamma : index : %d\n", i);
				goto error;
			}
		} else if (diminfo[i].way == DIMMING_METHOD_FILL_CENTER) {
			memcpy(diminfo[i].gamma, SEQ_GAMMA_CONDITION_SET, ARRAY_SIZE(SEQ_GAMMA_CONDITION_SET));
		} else {
			dev_err(&lcd->ld->dev, "%s: %dth way(%d) is unknown method\n", __func__, i, diminfo[i].way);
		}
	}
error:
	return ret;

}
#endif


static int ea8061_read_init_info(struct lcd_info *lcd, unsigned char *mtp)
{
	int i = 0;
	int ret;
	struct panel_private *priv = &lcd->dsim->priv;
	unsigned char buf[60] = {0, };

	dev_info(&lcd->ld->dev, "MDD : %s was called\n", __func__);

	dsim_write_hl_data(lcd, SEQ_EA8061_READ_ID, ARRAY_SIZE(SEQ_EA8061_READ_ID));
	ret = dsim_read_hl_data(lcd, EA8061_READ_RX_REG, EA8061_ID_LEN, lcd->id);
	if (ret != EA8061_ID_LEN) {
		dev_err(&lcd->ld->dev, "%s: can't find connected panel. check panel connection\n", __func__);
		priv->lcdConnected = PANEL_DISCONNEDTED;
		goto read_exit;
	}

	dev_info(&lcd->ld->dev, "READ ID : ");
	for (i = 0; i < EA8061_ID_LEN; i++)
		dev_info(&lcd->ld->dev, "%02x, ", lcd->id[i]);
	dev_info(&lcd->ld->dev, "\n");

	ret = dsim_write_hl_data(lcd, SEQ_TEST_KEY_ON_F0, ARRAY_SIZE(SEQ_TEST_KEY_ON_F0));
	if (ret < 0)
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_TEST_KEY_ON_F0\n", __func__);

	/* mtp SEQ_EA8061_READ_MTP */
	dsim_write_hl_data(lcd, SEQ_EA8061_READ_MTP, ARRAY_SIZE(SEQ_EA8061_READ_MTP));
	ret = dsim_read_hl_data(lcd, EA8061_READ_RX_REG, EA8061_MTP_DATE_SIZE, buf);
	if (ret != EA8061_MTP_DATE_SIZE) {
		dev_err(&lcd->ld->dev, "failed to read mtp, check panel connection\n");
		goto read_fail;
	}
	memcpy(mtp, buf, EA8061_MTP_SIZE);

	/* read DB */
	dsim_write_hl_data(lcd, SEQ_EA8061_READ_DB, ARRAY_SIZE(SEQ_EA8061_READ_DB));
	ret = dsim_read_hl_data(lcd, EA8061_READ_RX_REG, EA8061_MTP_DB_SIZE, buf);
	if (ret != EA8061_MTP_DB_SIZE) {
		dev_err(&lcd->ld->dev, "failed to read mtp, check panel connection\n");
		goto read_fail;
	}
	memcpy(lcd->DB, buf, EA8061_MTP_DB_SIZE);

	memcpy(lcd->date, &buf[50], EA8061_DATE_SIZE);
	lcd->coordinate[0] = buf[52] << 8 | buf[53];	/* X */
	lcd->coordinate[1] = buf[54] << 8 | buf[55];

	/* read B2 */
	dsim_write_hl_data(lcd, SEQ_EA8061_READ_B2, ARRAY_SIZE(SEQ_EA8061_READ_B2));
	ret = dsim_read_hl_data(lcd, EA8061_READ_RX_REG, EA8061_MTP_B2_SIZE, buf);
	if (ret != EA8061_MTP_B2_SIZE) {
		dev_err(&lcd->ld->dev, "failed to read mtp, check panel connection\n");
		goto read_fail;
	}
	memcpy(lcd->B2, buf, EA8061_MTP_B2_SIZE);

	/* read D4 */
	dsim_write_hl_data(lcd, SEQ_EA8061_READ_D4, ARRAY_SIZE(SEQ_EA8061_READ_D4));
	ret = dsim_read_hl_data(lcd, EA8061_READ_RX_REG, EA8061_MTP_D4_SIZE, buf);
	if (ret != EA8061_MTP_D4_SIZE) {
		dev_err(&lcd->ld->dev, "failed to read mtp, check panel connection\n");
		goto read_fail;
	}
	memcpy(lcd->D4, buf, EA8061_MTP_D4_SIZE);

read_exit:
	return 0;

read_fail:
	return -ENODEV;
}

static int ea8061_displayon(struct lcd_info *lcd)
{
	int ret = 0;

	dev_info(&lcd->ld->dev, "MDD : %s was called\n", __func__);

	goto displayon_err;


displayon_err:
	return ret;

}

static int ea8061_exit(struct lcd_info *lcd)
{
	int ret = 0;

	dev_info(&lcd->ld->dev, "MDD : %s was called\n", __func__);
	ret = dsim_write_hl_data(lcd, SEQ_DISPLAY_OFF, ARRAY_SIZE(SEQ_DISPLAY_OFF));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : DISPLAY_OFF\n", __func__);
		goto exit_err;
	}

	msleep(35);

	ret = dsim_write_hl_data(lcd, SEQ_SLEEP_IN, ARRAY_SIZE(SEQ_SLEEP_IN));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SLEEP_IN\n", __func__);
		goto exit_err;
	}

	msleep(150);

exit_err:
	return ret;
}

static int ea8061_init(struct lcd_info *lcd)
{
	int ret;

	dev_info(&lcd->ld->dev, "MDD : %s was called\n", __func__);

	usleep_range(5000, 6000);

	ea8061_read_id(lcd);

	ret = dsim_write_hl_data(lcd, SEQ_TEST_KEY_ON_F0, ARRAY_SIZE(SEQ_TEST_KEY_ON_F0));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_TEST_KEY_ON_F0\n", __func__);
		goto init_exit;
	}

	ret = dsim_write_hl_data(lcd, SEQ_EA8061_LTPS_STOP, ARRAY_SIZE(SEQ_EA8061_LTPS_STOP));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_EA8061_LTPS_STOP\n", __func__);
		goto init_exit;
	}

	ret = dsim_write_hl_data(lcd, SEQ_EA8061_LTPS_TIMING, ARRAY_SIZE(SEQ_EA8061_LTPS_TIMING));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_EA8061_LTPS_TIMING\n", __func__);
		goto init_exit;
	}

	ret = dsim_write_hl_data(lcd, SEQ_EA8061_LTPS_UPDATE, ARRAY_SIZE(SEQ_EA8061_LTPS_UPDATE));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_EA8061_LTPS_UPDATE\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_EA8061_SCAN_DIRECTION, ARRAY_SIZE(SEQ_EA8061_SCAN_DIRECTION));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_EA8061_SCAN_DIRECTION\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_EA8061_AID_SET_DEFAULT, ARRAY_SIZE(SEQ_EA8061_AID_SET_DEFAULT));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_EA8061_AID_SET_DEFAULT\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_EA8061_SLEW_CONTROL, ARRAY_SIZE(SEQ_EA8061_SLEW_CONTROL));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_EA8061_SLEW_CONTROL\n", __func__);
		goto init_exit;
	}

	ret = dsim_write_hl_data(lcd, SEQ_SLEEP_OUT, ARRAY_SIZE(SEQ_SLEEP_OUT));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_SLEEP_OUT\n", __func__);
		goto init_exit;
	}

	msleep(120);

	dsim_panel_set_brightness(lcd, 1);

	ret = dsim_write_hl_data(lcd, SEQ_DISPLAY_ON, ARRAY_SIZE(SEQ_DISPLAY_ON));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_EA8061_MPS_SET_MAX\n", __func__);
		goto init_exit;
	}

	ret = dsim_write_hl_data(lcd, SEQ_TEST_KEY_OFF_F0, ARRAY_SIZE(SEQ_TEST_KEY_OFF_F0));
	if (ret < 0)
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_TEST_KEY_OFF_F0\n", __func__);

init_exit:
	return ret;
}

static int ea8061_probe(struct dsim_device *dsim)
{
	int ret = 0;
	struct panel_private *priv = &dsim->priv;
	struct lcd_info *lcd = dsim->priv.par;
	unsigned char mtp[EA8061_MTP_SIZE] = {0, };

	dev_info(&lcd->ld->dev, "MDD : %s was called\n", __func__);

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

	ret = ea8061_read_init_info(lcd, mtp);
	if (priv->lcdConnected == PANEL_DISCONNEDTED) {
		dev_err(&lcd->ld->dev, "dsim : %s lcd was not connected\n", __func__);
		goto probe_exit;
	}

#ifdef CONFIG_PANEL_AID_DIMMING
	ret = init_dimming(lcd, mtp);
	if (ret)
		dev_err(&lcd->ld->dev, "%s: failed to generate gamma table\n", __func__);
#endif

probe_exit:
	return ret;
}


static ssize_t lcd_type_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);

	sprintf(buf, "SDC_%02X%02X%02X\n", lcd->id[0], lcd->id[1], lcd->id[2]);

	return strlen(buf);
}

static ssize_t window_type_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);

	sprintf(buf, "%x %x %x\n", lcd->id[0], lcd->id[1], lcd->id[2]);

	return strlen(buf);
}

static ssize_t brightness_table_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);

	char *pos = buf;
	int nit, i, br_index;

	if (IS_ERR_OR_NULL(lcd->br_tbl))
		return strlen(buf);

	for (i = 0; i <= EXTEND_BRIGHTNESS; i++) {
		nit = lcd->br_tbl[i];
		br_index = get_acutal_br_index(lcd, nit);
		nit = get_actual_br_value(lcd, br_index);
		pos += sprintf(pos, "%3d %3d\n", i, nit);
	}
	return pos - buf;
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
	struct lcd_info *lcd = dev_get_drvdata(dev);
	int value;
	int rc;

	rc = kstrtoint(buf, 10, &value);

	if (rc < 0)
		return rc;
	else {
		mutex_lock(&lcd->lock);
		lcd->temperature = value;
		mutex_unlock(&lcd->lock);

		dsim_panel_set_brightness(lcd, 1);
		dev_info(dev, "%s: %d, %d\n", __func__, value, lcd->temperature);
	}

	return size;
}

static ssize_t color_coordinate_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);

	sprintf(buf, "%u, %u\n", lcd->coordinate[0], lcd->coordinate[1]);
	return strlen(buf);
}

static ssize_t manufacture_date_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);
	u16 year;
	u8 month, day, hour, min;

	year = ((lcd->date[0] & 0xF0) >> 4) + 2011;
	month = lcd->date[0] & 0xF;
	day = lcd->date[1] & 0x1F;
	hour = lcd->date[2] & 0x1F;
	min = lcd->date[3] & 0x3F;

	sprintf(buf, "%d, %d, %d, %d:%d\n", year, month, day, hour, min);
	return strlen(buf);
}

#ifdef CONFIG_PANEL_AID_DIMMING
static void show_aid_log(struct lcd_info *lcd)
{
	int i, j, k;
	struct dim_data *dim_data = NULL;
	struct SmtDimInfo *dimming_info = NULL;
	u8 temp[256];


	dim_data = (struct dim_data *)(lcd->dim_data);
	if (dim_data == NULL) {
		dev_info(&lcd->ld->dev, "%s:dimming is NULL\n", __func__);
		return;
	}

	dimming_info = (struct SmtDimInfo *)(lcd->dim_info);
	if (dimming_info == NULL) {
		dev_info(&lcd->ld->dev, "%s:dimming is NULL\n", __func__);
		return;
	}

	dev_info(&lcd->ld->dev, "MTP VT : %d %d %d\n",
			dim_data->vt_mtp[CI_RED], dim_data->vt_mtp[CI_GREEN], dim_data->vt_mtp[CI_BLUE]);

	for (i = 0; i < VMAX; i++) {
		dev_info(&lcd->ld->dev, "MTP V%d : %4d %4d %4d\n",
			vref_index[i], dim_data->mtp[i][CI_RED], dim_data->mtp[i][CI_GREEN], dim_data->mtp[i][CI_BLUE]);
	}

	for (i = 0; i < MAX_BR_INFO; i++) {
		memset(temp, 0, sizeof(temp));
		for (j = 1; j < OLED_CMD_GAMMA_CNT; j++) {
			if (j == 1 || j == 3 || j == 5)
				k = dimming_info[i].gamma[j++] * 256;
			else
				k = 0;
			snprintf(temp + strnlen(temp, 256), 256, " %d", dimming_info[i].gamma[j] + k);
		}
		dev_info(&lcd->ld->dev, "nit :%3d %s\n", dimming_info[i].br, temp);
	}
}


static ssize_t aid_log_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);

	show_aid_log(lcd);
	return strlen(buf);
}
#endif

static ssize_t manufacture_code_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);

	sprintf(buf, "%02X%02X%02X%02X%02X\n",
		lcd->code[0], lcd->code[1], lcd->code[2], lcd->code[3], lcd->code[4]);

	return strlen(buf);
}

static ssize_t dump_register_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);
	char *pos = buf;
	u8 reg, len, offset;
	int i;
	u8 *dump = NULL;
	unsigned char read_reg[] = {
		EA8061_READ_TX_REG,
		0x00,
	};

	reg = lcd->dump_info[0];
	len = lcd->dump_info[1];
	offset = lcd->dump_info[2];

	read_reg[1] = reg;

	if (!reg || !len || reg > 0xff || len > 255 || offset > 255)
		goto exit;

	dump = kcalloc(len, sizeof(u8), GFP_KERNEL);

	if (lcd->state == PANEL_STATE_RESUMED) {
		if (dsim_write_hl_data(lcd, SEQ_TEST_KEY_ON_F0, ARRAY_SIZE(SEQ_TEST_KEY_ON_F0)) < 0)
			dev_err(&lcd->ld->dev, "failed to write test on f0 command.\n");

		dsim_write_hl_data(lcd, read_reg, ARRAY_SIZE(read_reg));
		dsim_read_hl_data(lcd, EA8061_READ_RX_REG, len, dump);

		if (dsim_write_hl_data(lcd, SEQ_TEST_KEY_OFF_F0, ARRAY_SIZE(SEQ_TEST_KEY_OFF_F0)) < 0)
			dev_err(&lcd->ld->dev, "failed to write test off f0 command.\n");
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
static DEVICE_ATTR(manufacture_code, 0444, manufacture_code_show, NULL);
static DEVICE_ATTR(brightness_table, 0444, brightness_table_show, NULL);
static DEVICE_ATTR(siop_enable, 0664, siop_enable_show, siop_enable_store);
static DEVICE_ATTR(power_reduce, 0664, power_reduce_show, power_reduce_store);
static DEVICE_ATTR(temperature, 0664, temperature_show, temperature_store);
static DEVICE_ATTR(color_coordinate, 0444, color_coordinate_show, NULL);
static DEVICE_ATTR(manufacture_date, 0444, manufacture_date_show, NULL);
#ifdef CONFIG_PANEL_AID_DIMMING
static DEVICE_ATTR(aid_log, 0444, aid_log_show, NULL);
#endif
static DEVICE_ATTR(dump_register, 0644, dump_register_show, dump_register_store);

static struct attribute *lcd_sysfs_attributes[] = {
	&dev_attr_lcd_type.attr,
	&dev_attr_window_type.attr,
	&dev_attr_manufacture_code.attr,
	&dev_attr_brightness_table.attr,
	&dev_attr_siop_enable.attr,
	&dev_attr_power_reduce.attr,
	&dev_attr_temperature.attr,
	&dev_attr_color_coordinate.attr,
	&dev_attr_manufacture_date.attr,
	&dev_attr_aid_log.attr,
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

	ret = ea8061_probe(dsim);
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
	int ret = 0;

	if (lcd->state == PANEL_STATE_SUSPENED) {
		ret = ea8061_init(lcd);
		lcd->state = PANEL_STATE_RESUMED;
		if (ret) {
			dev_err(&lcd->ld->dev, "%s: failed to panel init\n", __func__);
			lcd->state = PANEL_STATE_SUSPENED;
			goto displayon_err;
		}
	}

	dsim_panel_set_brightness(lcd, 1);

	ret = ea8061_displayon(lcd);
	if (ret) {
		dev_err(&lcd->ld->dev, "%s: failed to panel display on\n", __func__);
		goto displayon_err;
	}

displayon_err:
	return ret;
}

static int dsim_panel_suspend(struct dsim_device *dsim)
{
	struct lcd_info *lcd = dsim->priv.par;
	int ret = 0;

	if (lcd->state == PANEL_STATE_SUSPENED)
		goto suspend_err;

	lcd->state = PANEL_STATE_SUSPENDING;

	ret = ea8061_exit(lcd);
	if (ret) {
		dev_err(&lcd->ld->dev, "%s: failed to panel exit\n", __func__);
		goto suspend_err;
	}

	lcd->state = PANEL_STATE_SUSPENED;

suspend_err:
	return ret;
}

struct mipi_dsim_lcd_driver ea8061_mipi_lcd_driver = {
	.probe		= dsim_panel_probe,
	.displayon	= dsim_panel_displayon,
	.suspend	= dsim_panel_suspend,
};

