/*
 * drivers/video/decon_7580/panels/ea8064g_lcd_ctrl.c
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

#include "ea8064g_param.h"

#ifdef CONFIG_PANEL_AID_DIMMING
#include "aid_dimming.h"
#include "dimming_core.h"
#include "ea8064g_aid_dimming.h"
#endif

#if defined(CONFIG_EXYNOS_DECON_MDNIE_LITE)
#include "mdnie.h"
#include "mdnie_lite_table_s5neo.h"
#endif

struct lcd_info {
	struct lcd_device *ld;
	struct backlight_device *bd;
	unsigned char id[3];
	unsigned char code[5];
	unsigned char tset[8];
	unsigned char elvss[4];

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
	int lux;
	struct class *mdnie_class;

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

static int dsim_read_hl_data_offset(struct lcd_info *lcd, u8 addr, u32 size, u8 *buf, u32 offset)
{
	unsigned char wbuf[] = {0xB0, 0};
	int ret = 0;

	wbuf[1] = offset;

	ret = dsim_write_hl_data(lcd, wbuf, ARRAY_SIZE(wbuf));
	if (ret < 0)
		dev_err(&lcd->ld->dev, "%s: failed to write wbuf\n", __func__);

	ret = dsim_read_hl_data(lcd, addr, size, buf);
	if (ret < 1)
		dev_err(&lcd->ld->dev, "%s: fail\n", __func__);

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
	u8 *gamma = NULL;

	gamma = get_gamma_from_index(lcd, lcd->br_index);
	if (gamma == NULL) {
		dev_err(&lcd->ld->dev, "%s: failed to get gamma\n", __func__);
		return;
	}

	if (dsim_write_hl_data(lcd, gamma, GAMMA_CMD_CNT) < 0)
		dev_err(&lcd->ld->dev, "%s: failed to write gamma\n", __func__);
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
	u8 elvss_val[5] = {0, };
	int real_br = 0;

	real_br = get_actual_br_value(lcd, lcd->br_index);

	elvss = get_elvss_from_index(lcd, lcd->br_index, lcd->acl_enable);
	if (elvss == NULL) {
		dev_err(&lcd->ld->dev, "%s: failed to get elvss value\n", __func__);
		return;
	}

	elvss_val[0] = elvss[0];

	if (real_br >= 41)
		elvss_val[1] = 0x5C;
	else
		elvss_val[1] = 0x4C;

	elvss_val[2] = elvss[2];
	elvss_val[3] = lcd->elvss[2];
	elvss_val[4] = lcd->elvss[3];

	if (real_br <= 29) {
		if (lcd->temperature <= -20)
			elvss_val[4] = elvss[5];
		else if (lcd->temperature > -20 && lcd->temperature <= 0)
			elvss_val[4] = elvss[4];
		else
			elvss_val[4] = elvss[3];
	}

	if (dsim_write_hl_data(lcd, elvss_val, ELVSS_LEN + 1) < 0)
		dev_err(&lcd->ld->dev, "%s: failed to write elvss\n", __func__);
}

static int dsim_panel_set_acl(struct lcd_info *lcd, int force)
{
	int ret = 0, level = ACL_STATUS_15P;

	if (lcd->siop_enable || LEVEL_IS_HBM(lcd->brightness))
		goto acl_update;

	if (!lcd->acl_enable)
		level = ACL_STATUS_0P;

acl_update:
	if (force || lcd->current_acl != lcd->acl_cutoff_tbl[level][1]) {
		if (dsim_write_hl_data(lcd,  lcd->acl_opr_tbl[level], 2) < 0) {
			dev_err(&lcd->ld->dev, "failed to write acl opr command.\n");
			ret = -EPERM;
			goto exit;
		}
		if (dsim_write_hl_data(lcd, lcd->acl_cutoff_tbl[level], 2) < 0) {
			dev_err(&lcd->ld->dev, "failed to write acl command.\n");
			ret = -EPERM;
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

static int dsim_panel_set_tset(struct lcd_info *lcd, int force)
{
	int ret = 0;
	int tset = 0;
	unsigned char SEQ_TSET[TSET_LEN] = {TSET_REG, };

	tset = ((lcd->temperature >= 0) ? 0 : BIT(7)) | abs(lcd->temperature);

	if (force || lcd->tset[0] != tset)
		lcd->tset[0] = SEQ_TSET[1] = tset;
	else
		return ret;

	if (dsim_write_hl_data(lcd, SEQ_TSET, ARRAY_SIZE(SEQ_TSET)) < 0) {
		dev_err(&lcd->ld->dev, "failed to write tset command.\n");
		ret = -EPERM;

	}
	dev_info(&lcd->ld->dev, "temperature: %d, tset: %d\n", lcd->temperature, SEQ_TSET[1]);

	return ret;
}

static int dsim_panel_set_hbm(struct lcd_info *lcd, int force)
{
	int ret = 0, level = 0;

	level = LEVEL_IS_HBM(lcd->brightness);

	if (force || lcd->current_hbm != lcd->hbm_tbl[level][1]) {
		lcd->current_hbm = lcd->hbm_tbl[level][1];
		if (dsim_write_hl_data(lcd, lcd->hbm_tbl[level], ARRAY_SIZE(SEQ_HBM_OFF)) < 0) {
			dev_err(&lcd->ld->dev, "failed to write hbm command.\n");
			ret = -EPERM;
		}
		dev_info(&lcd->ld->dev, "hbm: %d, brightness: %d\n", lcd->current_hbm, lcd->brightness);
	}

	return ret;
}

static int low_level_set_brightness(struct lcd_info *lcd, int force)
{

	if (dsim_write_hl_data(lcd, SEQ_TEST_KEY_ON_F0, ARRAY_SIZE(SEQ_TEST_KEY_ON_F0)) < 0)
		dev_err(&lcd->ld->dev, "%s: failed to write F0 on command.\n", __func__);
	if (dsim_write_hl_data(lcd, SEQ_TEST_KEY_ON_F1, ARRAY_SIZE(SEQ_TEST_KEY_ON_F1)) < 0)
		dev_err(&lcd->ld->dev, "%s: failed to write F1 on command.\n", __func__);
	if (dsim_write_hl_data(lcd, SEQ_TEST_KEY_ON_FC, ARRAY_SIZE(SEQ_TEST_KEY_ON_FC)) < 0)
		dev_err(&lcd->ld->dev, "%s: failed to write FC on command.\n", __func__);

	dsim_panel_gamma_ctrl(lcd);

	dsim_panel_aid_ctrl(lcd);

	dsim_panel_set_elvss(lcd);

	if (dsim_write_hl_data(lcd, SEQ_GAMMA_UPDATE_EA8064G, ARRAY_SIZE(SEQ_GAMMA_UPDATE_EA8064G)) < 0)
		dev_err(&lcd->ld->dev, "%s: failed to write gamma\n", __func__);

	dsim_panel_set_acl(lcd, force);

	dsim_panel_set_tset(lcd, force);

	dsim_panel_set_hbm(lcd, force);

	if (dsim_write_hl_data(lcd, SEQ_TEST_KEY_OFF_FC, ARRAY_SIZE(SEQ_TEST_KEY_OFF_FC)) < 0)
			dev_err(&lcd->ld->dev, "%s: failed to write FC on command.\n", __func__);
	if (dsim_write_hl_data(lcd, SEQ_TEST_KEY_OFF_F1, ARRAY_SIZE(SEQ_TEST_KEY_OFF_F1)) < 0)
			dev_err(&lcd->ld->dev, "%s: failed to write F1 on command.\n", __func__);
	if (dsim_write_hl_data(lcd, SEQ_TEST_KEY_OFF_F0, ARRAY_SIZE(SEQ_TEST_KEY_OFF_F0)) < 0)
		dev_err(&lcd->ld->dev, "%s: failed to write F0 on command\n", __func__);

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
	struct dim_data *dimming;
	int p_br = lcd->bd->props.brightness;
	int acutal_br = 0;
	int real_br = 0;
	int prev_index = lcd->br_index;
	int ret = 0;

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

	dev_info(&lcd->ld->dev, "%s: platform : %d, : mapping : %d, real : %d, index : %d force : %d\n",
		__func__, p_br, acutal_br, real_br, lcd->br_index, force);

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
static const unsigned char *HBM_TABLE[HBM_STATUS_MAX] = {SEQ_HBM_OFF, SEQ_HBM_ON};
static const unsigned char *ACL_CUTOFF_TABLE[ACL_STATUS_MAX] = {SEQ_ACL_OFF, SEQ_ACL_15};
static const unsigned char *ACL_OPR_TABLE[ACL_OPR_MAX] = {SEQ_ACL_OFF_OPR_AVR, SEQ_ACL_ON_OPR_AVR};

static const unsigned int br_tbl[EXTEND_BRIGHTNESS + 1] = {
	2, 2, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 19, 20, 21, 22,
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
	316, 316, 316, 316, 333, 333, 333, 333, 333, 333, 333, 333, 333, 333, 333, 333, 360,
	[UI_MAX_BRIGHTNESS + 1 ... EXTEND_BRIGHTNESS - 1] = 360,
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

struct SmtDimInfo lily_dimming_info[MAX_BR_INFO] = {				/* add hbm array */
	{.br = 5, .cTbl = ctbl5nit_B, .aid = aid5_B, .elvAcl = elv84, .elv = elv84, .m_gray = m_gray_005},
	{.br = 6, .cTbl = ctbl6nit_B, .aid = aid6_B, .elvAcl = elv84, .elv = elv84, .m_gray = m_gray_006},
	{.br = 7, .cTbl = ctbl7nit_B, .aid = aid7_B, .elvAcl = elv84, .elv = elv84, .m_gray = m_gray_007},
	{.br = 8, .cTbl = ctbl8nit_B, .aid = aid8_B, .elvAcl = elv84, .elv = elv84, .m_gray = m_gray_008},
	{.br = 9, .cTbl = ctbl9nit_B, .aid = aid9_B, .elvAcl = elv84, .elv = elv84, .m_gray = m_gray_009},
	{.br = 10, .cTbl = ctbl10nit_B, .aid = aid10_B, .elvAcl = elv84, .elv = elv84, .m_gray = m_gray_010},
	{.br = 11, .cTbl = ctbl11nit_B, .aid = aid11_B, .elvAcl = elv84, .elv = elv84, .m_gray = m_gray_011},
	{.br = 12, .cTbl = ctbl12nit_B, .aid = aid12_B, .elvAcl = elv84, .elv = elv84, .m_gray = m_gray_012},
	{.br = 13, .cTbl = ctbl13nit_B, .aid = aid13_B, .elvAcl = elv84, .elv = elv84, .m_gray = m_gray_013},
	{.br = 14, .cTbl = ctbl14nit_B, .aid = aid14_B, .elvAcl = elv84, .elv = elv84, .m_gray = m_gray_014},
	{.br = 15, .cTbl = ctbl15nit_B, .aid = aid15_B, .elvAcl = elv84, .elv = elv84, .m_gray = m_gray_015},
	{.br = 16, .cTbl = ctbl16nit_B, .aid = aid16_B, .elvAcl = elv84, .elv = elv84, .m_gray = m_gray_016},
	{.br = 17, .cTbl = ctbl17nit_B, .aid = aid17_B, .elvAcl = elv84, .elv = elv84, .m_gray = m_gray_017},
	{.br = 19, .cTbl = ctbl19nit_B, .aid = aid19_B, .elvAcl = elv84, .elv = elv84, .m_gray = m_gray_019},
	{.br = 20, .cTbl = ctbl20nit_B, .aid = aid20_B, .elvAcl = elv84, .elv = elv84, .m_gray = m_gray_020},
	{.br = 21, .cTbl = ctbl21nit_B, .aid = aid21_B, .elvAcl = elv86, .elv = elv86, .m_gray = m_gray_021},
	{.br = 22, .cTbl = ctbl22nit_B, .aid = aid22_B, .elvAcl = elv88, .elv = elv88, .m_gray = m_gray_022},
	{.br = 24, .cTbl = ctbl24nit_B, .aid = aid24_B, .elvAcl = elv8a, .elv = elv8a, .m_gray = m_gray_024},
	{.br = 25, .cTbl = ctbl25nit_B, .aid = aid25_B, .elvAcl = elv8c, .elv = elv8c, .m_gray = m_gray_025},
	{.br = 27, .cTbl = ctbl27nit_B, .aid = aid27_B, .elvAcl = elv8e, .elv = elv8e, .m_gray = m_gray_027},
	{.br = 29, .cTbl = ctbl29nit_B, .aid = aid29_B, .elvAcl = elv90, .elv = elv90, .m_gray = m_gray_029},
	{.br = 30, .cTbl = ctbl30nit_B, .aid = aid30_B, .elvAcl = elv92, .elv = elv92, .m_gray = m_gray_030},
	{.br = 32, .cTbl = ctbl32nit_B, .aid = aid32_B, .elvAcl = elv92, .elv = elv92, .m_gray = m_gray_032},
	{.br = 34, .cTbl = ctbl34nit_B, .aid = aid34_B, .elvAcl = elv92, .elv = elv92, .m_gray = m_gray_034},
	{.br = 37, .cTbl = ctbl37nit_B, .aid = aid37_B, .elvAcl = elv92, .elv = elv92, .m_gray = m_gray_037},
	{.br = 39, .cTbl = ctbl39nit_B, .aid = aid39_B, .elvAcl = elv92, .elv = elv92, .m_gray = m_gray_039},
	{.br = 41, .cTbl = ctbl41nit_B, .aid = aid41_B, .elvAcl = elv92, .elv = elv92, .m_gray = m_gray_041},
	{.br = 44, .cTbl = ctbl44nit_B, .aid = aid44_B, .elvAcl = elv92, .elv = elv92, .m_gray = m_gray_044},
	{.br = 47, .cTbl = ctbl47nit_B, .aid = aid47_B, .elvAcl = elv92, .elv = elv92, .m_gray = m_gray_047},
	{.br = 50, .cTbl = ctbl50nit_B, .aid = aid50_B, .elvAcl = elv92, .elv = elv92, .m_gray = m_gray_050},
	{.br = 53, .cTbl = ctbl53nit_B, .aid = aid53_B, .elvAcl = elv92, .elv = elv92, .m_gray = m_gray_053},
	{.br = 56, .cTbl = ctbl56nit_B, .aid = aid56_B, .elvAcl = elv92, .elv = elv92, .m_gray = m_gray_056},
	{.br = 60, .cTbl = ctbl60nit_B, .aid = aid60_B, .elvAcl = elv92, .elv = elv92, .m_gray = m_gray_060},
	{.br = 64, .cTbl = ctbl64nit_B, .aid = aid64_B, .elvAcl = elv92, .elv = elv92, .m_gray = m_gray_064},
	{.br = 68, .cTbl = ctbl68nit_B, .aid = aid68_B, .elvAcl = elv92, .elv = elv92, .m_gray = m_gray_068},
	{.br = 72, .cTbl = ctbl72nit_B, .aid = aid68_B, .elvAcl = elv92, .elv = elv92, .m_gray = m_gray_072},
	{.br = 77, .cTbl = ctbl77nit_B, .aid = aid68_B, .elvAcl = elv91, .elv = elv91, .m_gray = m_gray_077},
	{.br = 82, .cTbl = ctbl82nit_B, .aid = aid68_B, .elvAcl = elv91, .elv = elv91, .m_gray = m_gray_082},
	{.br = 87, .cTbl = ctbl87nit_B, .aid = aid68_B, .elvAcl = elv91, .elv = elv91, .m_gray = m_gray_087},
	{.br = 93, .cTbl = ctbl93nit_B, .aid = aid68_B, .elvAcl = elv90, .elv = elv90, .m_gray = m_gray_093},
	{.br = 98, .cTbl = ctbl98nit_B, .aid = aid68_B, .elvAcl = elv90, .elv = elv90, .m_gray = m_gray_098},
	{.br = 105, .cTbl = ctbl105nit_B, .aid = aid68_B, .elvAcl = elv90, .elv = elv90, .m_gray = m_gray_105},
	{.br = 111, .cTbl = ctbl111nit_B, .aid = aid68_B, .elvAcl = elv8f, .elv = elv8f, .m_gray = m_gray_111},
	{.br = 119, .cTbl = ctbl119nit_B, .aid = aid68_B, .elvAcl = elv8f, .elv = elv8f, .m_gray = m_gray_119},
	{.br = 126, .cTbl = ctbl126nit_B, .aid = aid68_B, .elvAcl = elv8e, .elv = elv8e, .m_gray = m_gray_126},
	{.br = 134, .cTbl = ctbl134nit_B, .aid = aid68_B, .elvAcl = elv8e, .elv = elv8e, .m_gray = m_gray_134},
	{.br = 143, .cTbl = ctbl143nit_B, .aid = aid68_B, .elvAcl = elv8d, .elv = elv8d, .m_gray = m_gray_143},
	{.br = 152, .cTbl = ctbl152nit_B, .aid = aid68_B, .elvAcl = elv8c, .elv = elv8c, .m_gray = m_gray_152},
	{.br = 162, .cTbl = ctbl162nit_B, .aid = aid68_B, .elvAcl = elv8c, .elv = elv8c, .m_gray = m_gray_162},
	{.br = 172, .cTbl = ctbl172nit_B, .aid = aid172_B, .elvAcl = elv8c, .elv = elv8c, .m_gray = m_gray_172},
	{.br = 183, .cTbl = ctbl183nit_B, .aid = aid183_B, .elvAcl = elv8c, .elv = elv8c, .m_gray = m_gray_183},
	{.br = 195, .cTbl = ctbl195nit_B, .aid = aid195_B, .elvAcl = elv8b, .elv = elv8b, .m_gray = m_gray_195},
	{.br = 207, .cTbl = ctbl207nit_B, .aid = aid207_B, .elvAcl = elv8a, .elv = elv8a, .m_gray = m_gray_207},
	{.br = 220, .cTbl = ctbl220nit_B, .aid = aid220_B, .elvAcl = elv8a, .elv = elv8a, .m_gray = m_gray_220},
	{.br = 234, .cTbl = ctbl234nit_B, .aid = aid234_B, .elvAcl = elv8a, .elv = elv8a, .m_gray = m_gray_234},
	{.br = 249, .cTbl = ctbl249nit_B, .aid = aid249_B, .elvAcl = elv89, .elv = elv89, .m_gray = m_gray_249},
	{.br = 265, .cTbl = ctbl265nit_B, .aid = aid249_B, .elvAcl = elv88, .elv = elv88, .m_gray = m_gray_265},
	{.br = 282, .cTbl = ctbl282nit_B, .aid = aid249_B, .elvAcl = elv87, .elv = elv87, .m_gray = m_gray_282},
	{.br = 300, .cTbl = ctbl300nit_B, .aid = aid249_B, .elvAcl = elv86, .elv = elv86, .m_gray = m_gray_300},
	{.br = 316, .cTbl = ctbl316nit_B, .aid = aid249_B, .elvAcl = elv86, .elv = elv86, .m_gray = m_gray_316},
	{.br = 333, .cTbl = ctbl333nit_B, .aid = aid249_B, .elvAcl = elv85, .elv = elv85, .m_gray = m_gray_333},
	{.br = 360, .cTbl = ctbl360nit_B, .aid = aid249_B, .elvAcl = elv84, .elv = elv84, .m_gray = m_gray_360, .way = W2},
	{.br = 500, .cTbl = ctbl360nit_B, .aid = aid249_B, .elvAcl = elv84, .elv = elv84, .m_gray = m_gray_360, .way = W2}
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

	diminfo = lily_dimming_info;

	lcd->dim_data = (void *)dimming;
	lcd->dim_info = (void *)diminfo;

	lcd->br_tbl = (unsigned int *)br_tbl;
	lcd->hbm_tbl = (unsigned char **)HBM_TABLE;
	lcd->acl_cutoff_tbl = (unsigned char **)ACL_CUTOFF_TABLE;
	lcd->acl_opr_tbl = (unsigned char **)ACL_OPR_TABLE;

	for (j = 0; j < CI_MAX; j++) {
		temp = ((mtp[pos] & 0x01) ? -1 : 1) * mtp[pos+1];
		dimming->t_gamma[V255][j] = (int)center_gamma[V255][j] + temp;
		dimming->mtp[V255][j] = temp;
		pos += 2;
	}

	for (i = V203; i > V0; i--) {
		for (j = 0; j < CI_MAX; j++) {
			temp = ((mtp[pos] & 0x80) ? -1 : 1) * (mtp[pos] & 0x7f);
			dimming->t_gamma[i][j] = (int)center_gamma[i][j] + temp;
			dimming->mtp[i][j] = temp;
			pos++;
		}
	}

	for (j = 0; j < CI_MAX; j++) {
		dimming->t_gamma[V0][j] = (int)center_gamma[V0][j] + temp;
		dimming->mtp[V0][j] = 0;
	}

	for (j = 0; j < CI_MAX; j++) {
		dimming->vt_mtp[j] = mtp[pos];
		pos++;
	}

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

static int ea8064g_read_init_info(struct lcd_info *lcd, unsigned char *mtp)
{
	int i = 0;
	int ret;
	struct panel_private *priv = &lcd->dsim->priv;
	unsigned char bufForCoordi[EA8064G_COORDINATE_LEN] = {0,};
	unsigned char buf[EA8064G_MTP_DATE_SIZE] = {0, };

	dev_info(&lcd->ld->dev, "MDD : %s was called\n", __func__);

	ret = dsim_write_hl_data(lcd, SEQ_TEST_KEY_ON_F0, ARRAY_SIZE(SEQ_TEST_KEY_ON_F0));
	if (ret < 0)
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_TEST_KEY_ON_F0\n", __func__);

	/* id */
	ret = dsim_read_hl_data(lcd, EA8064G_ID_REG, EA8064G_ID_LEN, lcd->id);
	if (ret != EA8064G_ID_LEN) {
		dev_err(&lcd->ld->dev, "%s: can't find connected panel. check panel connection\n", __func__);
		priv->lcdConnected = PANEL_DISCONNEDTED;
		goto read_exit;
	}

	dev_info(&lcd->ld->dev, "READ ID : ");
	for (i = 0; i < EA8064G_ID_LEN; i++)
		dev_info(&lcd->ld->dev, "%02x, ", lcd->id[i]);
	dev_info(&lcd->ld->dev, "\n");

	/* mtp */
	ret = dsim_read_hl_data(lcd, EA8064G_MTP_ADDR, EA8064G_MTP_DATE_SIZE, buf);
	if (ret != EA8064G_MTP_DATE_SIZE) {
		dev_err(&lcd->ld->dev, "failed to read mtp, check panel connection\n");
		goto read_fail;
	}
	memcpy(mtp, buf, EA8064G_MTP_SIZE);
	dev_info(&lcd->ld->dev, "READ MTP SIZE : %d\n", EA8064G_MTP_SIZE);
	dev_info(&lcd->ld->dev, "=========== MTP INFO ===========\n");
	for (i = 0; i < EA8064G_MTP_SIZE; i++)
		dev_info(&lcd->ld->dev, "MTP[%2d] : %2d : %2x\n", i, mtp[i], mtp[i]);

	/* elvss */
	ret = dsim_read_hl_data(lcd, ELVSS_REG, ELVSS_LEN, lcd->elvss);
	if (ret != ELVSS_LEN) {
		dev_err(&lcd->ld->dev, "failed to read mtp, check panel connection\n");
		goto read_fail;
	}

	/* coordinate */
	ret = dsim_read_hl_data(lcd, EA8064G_COORDINATE_REG, EA8064G_COORDINATE_LEN, bufForCoordi);
	if (ret != EA8064G_COORDINATE_LEN) {
		dev_err(&lcd->ld->dev, "failed to read coordinate on command.\n");
		goto read_fail;
	}
	lcd->coordinate[0] = bufForCoordi[0] << 8 | bufForCoordi[1];	/* X */
	lcd->coordinate[1] = bufForCoordi[2] << 8 | bufForCoordi[3];	/* Y */
	lcd->date[0] = bufForCoordi[4];  /* year, month */
	lcd->date[1] = bufForCoordi[5];  /* day */

	dev_info(&lcd->ld->dev, "READ coordi : ");
	for (i = 0; i < 2; i++)
		dev_info(&lcd->ld->dev, "%d, ", lcd->coordinate[i]);
	dev_info(&lcd->ld->dev, "\n");

	ret = dsim_write_hl_data(lcd, SEQ_TEST_KEY_OFF_F0, ARRAY_SIZE(SEQ_TEST_KEY_OFF_F0));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_TEST_KEY_OFF_F0\n", __func__);
		goto read_fail;
	}
read_exit:
	return 0;

read_fail:
	return -ENODEV;

}

static int ea8064g_probe(struct dsim_device *dsim)
{
	int ret = 0;
	struct panel_private *priv = &dsim->priv;
	struct lcd_info *lcd = dsim->priv.par;
	unsigned char mtp[EA8064G_MTP_SIZE] = {0, };

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
	lcd->lux = -1;

	ret = ea8064g_read_init_info(lcd, mtp);
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


static int ea8064g_displayon(struct lcd_info *lcd)
{
	int ret = 0;

	dev_info(&lcd->ld->dev, "MDD : %s was called\n", __func__);

	ret = dsim_write_hl_data(lcd, SEQ_DISPLAY_ON, ARRAY_SIZE(SEQ_DISPLAY_ON));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : DISPLAY_ON\n", __func__);
		goto displayon_err;
	}

displayon_err:
	return ret;

}

static int ea8064g_exit(struct lcd_info *lcd)
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

	msleep(130);

exit_err:
	return ret;
}

static int ea8064g_init(struct lcd_info *lcd)
{
	int ret = 0;

	dev_info(&lcd->ld->dev, "MDD : %s was called\n", __func__);

	ret = dsim_panel_set_brightness(lcd, 1);
	if (ret)
		dev_err(&lcd->ld->dev, "%s: failed to set brightness\n", __func__);

	ret = dsim_write_hl_data(lcd, SEQ_TEST_KEY_ON_F0, ARRAY_SIZE(SEQ_TEST_KEY_ON_F0));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_TEST_KEY_ON_F0\n", __func__);
		goto init_exit;
	}

	ret = dsim_write_hl_data(lcd, SEQ_TEST_KEY_ON_FC, ARRAY_SIZE(SEQ_TEST_KEY_ON_FC));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_TEST_KEY_ON_FC\n", __func__);
		goto init_exit;
	}

	ret = dsim_write_hl_data(lcd, SEQ_SLEEP_OUT, ARRAY_SIZE(SEQ_SLEEP_OUT));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_SLEEP_OUT\n", __func__);
		goto init_exit;
	}

	msleep(25);

	ret = dsim_write_hl_data(lcd, SEQ_DCDC1_GP, ARRAY_SIZE(SEQ_DCDC1_GP));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_SOURCE_CONTROL\n", __func__);
		goto init_exit;
	}

	ret = dsim_write_hl_data(lcd, SEQ_DCDC1_SET, ARRAY_SIZE(SEQ_DCDC1_SET));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_SOURCE_CONTROL\n", __func__);
		goto init_exit;
	}

	ret = dsim_write_hl_data(lcd, SEQ_SOURCE_CONTROL, ARRAY_SIZE(SEQ_SOURCE_CONTROL));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_SOURCE_CONTROL\n", __func__);
		goto init_exit;
	}

	ret = dsim_write_hl_data(lcd, SEQ_PCD_CONTROL, ARRAY_SIZE(SEQ_PCD_CONTROL));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_PCD_CONTROL\n", __func__);
		goto init_exit;
	}

	ret = dsim_write_hl_data(lcd, SEQ_TEST_KEY_OFF_FC, ARRAY_SIZE(SEQ_TEST_KEY_OFF_FC));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_TEST_KEY_OFF_FC\n", __func__);
		goto init_exit;
	}

	msleep(120);

	ret = dsim_write_hl_data(lcd, SEQ_TE_OUT, ARRAY_SIZE(SEQ_TE_OUT));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_TE_OUT\n", __func__);
		goto init_exit;
	}

	ret = dsim_write_hl_data(lcd, SEQ_TEST_KEY_OFF_F0, ARRAY_SIZE(SEQ_TEST_KEY_OFF_F0));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_TEST_KEY_OFF_F0\n", __func__);
		goto init_exit;
	}

init_exit:
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

static ssize_t cell_id_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);

	sprintf(buf, "%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X\n",
		lcd->date[0], lcd->date[1], lcd->date[2], lcd->date[3], lcd->date[4],
		lcd->date[5], lcd->date[6], (lcd->coordinate[0] & 0xFF00) >> 8, lcd->coordinate[0] & 0x00FF,
		(lcd->coordinate[1]&0xFF00)>>8, lcd->coordinate[1]&0x00FF);

	return strlen(buf);
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
		if (dsim_write_hl_data(lcd, SEQ_TEST_KEY_ON_F0, ARRAY_SIZE(SEQ_TEST_KEY_ON_F0)) < 0)
			dev_err(&lcd->ld->dev, "failed to write test on f0 command.\n");
		if (dsim_write_hl_data(lcd, SEQ_TEST_KEY_ON_FC, ARRAY_SIZE(SEQ_TEST_KEY_ON_FC)) < 0)
			dev_err(&lcd->ld->dev, "failed to write test on fc command.\n");

		if (offset)
			ret = dsim_read_hl_data_offset(lcd, reg, len, dump, offset);
		else
			ret = dsim_read_hl_data(lcd, reg, len, dump);

		if (dsim_write_hl_data(lcd, SEQ_TEST_KEY_OFF_FC, ARRAY_SIZE(SEQ_TEST_KEY_OFF_FC)) < 0)
			dev_err(&lcd->ld->dev, "failed to write test off fc command.\n");
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

static DEVICE_ATTR(lcd_type, 0444, lcd_type_show, NULL);
static DEVICE_ATTR(window_type, 0444, window_type_show, NULL);
static DEVICE_ATTR(manufacture_code, 0444, manufacture_code_show, NULL);
static DEVICE_ATTR(cell_id, 0444, cell_id_show, NULL);
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
static DEVICE_ATTR(lux, 0644, lux_show, lux_store);

static struct attribute *lcd_sysfs_attributes[] = {
	&dev_attr_lcd_type.attr,
	&dev_attr_window_type.attr,
	&dev_attr_manufacture_code.attr,
	&dev_attr_cell_id.attr,
	&dev_attr_brightness_table.attr,
	&dev_attr_siop_enable.attr,
	&dev_attr_power_reduce.attr,
	&dev_attr_temperature.attr,
	&dev_attr_color_coordinate.attr,
	&dev_attr_manufacture_date.attr,
	&dev_attr_aid_log.attr,
	&dev_attr_dump_register.attr,
	&dev_attr_lux.attr,
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


#if defined(CONFIG_EXYNOS_DECON_MDNIE_LITE)
static int mdnie_lite_write_set(struct lcd_info *lcd, struct lcd_seq_info *seq, u32 num)
{
	int ret = 0, i;

	for (i = 0; i < num; i++) {
		if (seq[i].cmd) {
			ret = dsim_write_hl_data(lcd, seq[i].cmd, seq[i].len);
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

int mdnie_lite_send_seq(struct lcd_info *lcd, struct lcd_seq_info *seq, u32 num)
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

int mdnie_lite_read(struct lcd_info *lcd, u8 addr, u8 *buf, u32 size)
{
	int ret = 0;

	if (lcd->state != PANEL_STATE_RESUMED) {
		dev_info(&lcd->ld->dev, "%s: panel is not active\n", __func__);
		return -EIO;
	}

	mutex_lock(&lcd->lock);
	ret = dsim_read_hl_data(lcd, addr, size, buf);
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

	ret = ea8064g_probe(dsim);
	if (ret) {
		dev_err(&lcd->ld->dev, "%s: failed to probe panel\n", __func__);
		goto probe_err;
	}

#if defined(CONFIG_EXYNOS_DECON_LCD_SYSFS)
	lcd_init_sysfs(lcd);
#endif

#if defined(CONFIG_EXYNOS_DECON_MDNIE_LITE)
	mdnie_register(&lcd->ld->dev, lcd, (mdnie_w)mdnie_lite_send_seq, (mdnie_r)mdnie_lite_read, lcd->coordinate, &tune_info);
	lcd->mdnie_class = get_mdnie_class();
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
		lcd->state = PANEL_STATE_RESUMED;
		ret = ea8064g_init(lcd);
		if (ret) {
			dev_err(&lcd->ld->dev, "%s: failed to panel init\n", __func__);
			lcd->state = PANEL_STATE_SUSPENED;
			goto displayon_err;
		}
	}

	dsim_panel_set_brightness(lcd, 1);

	ret = ea8064g_displayon(lcd);
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

	ret = ea8064g_exit(lcd);
	if (ret) {
		dev_err(&lcd->ld->dev, "%s: failed to panel exit\n", __func__);
		goto suspend_err;
	}

	lcd->state = PANEL_STATE_SUSPENED;

suspend_err:
	return ret;
}

struct mipi_dsim_lcd_driver ea8064g_mipi_lcd_driver = {
	.probe		= dsim_panel_probe,
	.displayon	= dsim_panel_displayon,
	.suspend	= dsim_panel_suspend,
};

