/*
 * Copyright (c) Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <video/mipi_display.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/lcd.h>
#include <linux/backlight.h>

#include "../dsim.h"
#include "../decon.h"
#include "dsim_panel.h"

#include "s6e8aa5x01_param.h"

#if defined(CONFIG_EXYNOS_DECON_MDNIE_LITE)
#include "mdnie.h"
#include "mdnie_lite_table_java.h"
#endif


struct lcd_info {
	unsigned int			connected;
	struct lcd_device *ld;
	struct backlight_device *bd;
	union {
		struct {
			u8		reserved;
			u8		id[S6E8AA5X01_ID_LEN];
		};
		u32			value;
	} id_info;
	unsigned char code[5];
	unsigned char tset[8];
	unsigned char elvss[4];

	unsigned char hbm_gamma[35];
	unsigned char elvss22th;
	unsigned char chip_id[5];

	int	temperature;
	unsigned int coordinate[2];
	unsigned char date[5];
	unsigned int state;
	unsigned int brightness;
	unsigned int br_index;
	unsigned int acl_enable;
	unsigned int current_acl;
	unsigned int current_acl_opr;
	unsigned int current_hbm;
	unsigned int adaptive_control;
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
	struct device svc_dev;
};


#include "aid_dimming.h"
#include "dimming_core.h"
#include "s6e8aa5x01_aid_dimming.h"

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
			dev_err(&lcd->ld->dev, "%s: fail. %02x, ret: %d\n", __func__, cmd[0], ret);
	}

	return ret;
}

static int dsim_read_hl_data(struct lcd_info *lcd, u8 addr, u32 size, u8 *buf)
{
	int ret = 0, rx_size = 0;
	int retry = 2;

	if (!lcd->connected)
		return ret;

try_read:
	rx_size = dsim_read_data(lcd->dsim, MIPI_DSI_DCS_READ, (u32)addr, size, buf);
	dev_info(&lcd->ld->dev, "%s: %02x, %d, %d\n", __func__, addr, size, rx_size);
	if (rx_size != size) {
		if (--retry)
			goto try_read;
		else {
			dev_err(&lcd->ld->dev, "%s: fail. %02x, %d\n", __func__, addr, rx_size);
			ret = -EPERM;
		}
	}

	return ret;
}


static const unsigned char *HBM_TABLE[HBM_STATUS_MAX] = {SEQ_HBM_OFF, SEQ_HBM_ON};
static const unsigned char *ACL_CUTOFF_TABLE[ACL_STATUS_MAX] = {SEQ_ACL_OFF, SEQ_ACL_15};
static const unsigned char *ACL_OPR_TABLE[ACL_OPR_MAX] = {SEQ_ACL_OFF_OPR_AVR, SEQ_ACL_ON_OPR_AVR};

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

static struct SmtDimInfo dimming_info[MAX_BR_INFO] = {				/* add hbm array */
	{.br = 5, .cTbl = ctbl5nit, .aid = aid5, .elvAcl = elv_5, .elv = elv_5, .m_gray = m_gray_005},
	{.br = 6, .cTbl = ctbl6nit, .aid = aid6, .elvAcl = elv_5, .elv = elv_5, .m_gray = m_gray_006},
	{.br = 7, .cTbl = ctbl7nit, .aid = aid7, .elvAcl = elv_5, .elv = elv_5, .m_gray = m_gray_007},
	{.br = 8, .cTbl = ctbl8nit, .aid = aid8, .elvAcl = elv_5, .elv = elv_5, .m_gray = m_gray_008},
	{.br = 9, .cTbl = ctbl9nit, .aid = aid9, .elvAcl = elv_5, .elv = elv_5, .m_gray = m_gray_009},
	{.br = 10, .cTbl = ctbl10nit, .aid = aid10, .elvAcl = elv_5, .elv = elv_5, .m_gray = m_gray_010},
	{.br = 11, .cTbl = ctbl11nit, .aid = aid11, .elvAcl = elv_5, .elv = elv_5, .m_gray = m_gray_011},
	{.br = 12, .cTbl = ctbl12nit, .aid = aid12, .elvAcl = elv_5, .elv = elv_5, .m_gray = m_gray_012},
	{.br = 13, .cTbl = ctbl13nit, .aid = aid13, .elvAcl = elv_5, .elv = elv_5, .m_gray = m_gray_013},
	{.br = 14, .cTbl = ctbl14nit, .aid = aid14, .elvAcl = elv_5, .elv = elv_5, .m_gray = m_gray_014},
	{.br = 15, .cTbl = ctbl15nit, .aid = aid15, .elvAcl = elv_5, .elv = elv_5, .m_gray = m_gray_015},
	{.br = 16, .cTbl = ctbl16nit, .aid = aid16, .elvAcl = elv_5, .elv = elv_5, .m_gray = m_gray_016},
	{.br = 17, .cTbl = ctbl17nit, .aid = aid17, .elvAcl = elv_5, .elv = elv_5, .m_gray = m_gray_017},
	{.br = 19, .cTbl = ctbl19nit, .aid = aid19, .elvAcl = elv_5, .elv = elv_5, .m_gray = m_gray_019},
	{.br = 20, .cTbl = ctbl20nit, .aid = aid20, .elvAcl = elv_5, .elv = elv_5, .m_gray = m_gray_020},
	{.br = 21, .cTbl = ctbl21nit, .aid = aid21, .elvAcl = elv_21, .elv = elv_21, .m_gray = m_gray_021},
	{.br = 22, .cTbl = ctbl22nit, .aid = aid22, .elvAcl = elv_22, .elv = elv_22, .m_gray = m_gray_022},
	{.br = 24, .cTbl = ctbl24nit, .aid = aid24, .elvAcl = elv_24, .elv = elv_24, .m_gray = m_gray_024},
	{.br = 25, .cTbl = ctbl25nit, .aid = aid25, .elvAcl = elv_25, .elv = elv_25, .m_gray = m_gray_025},
	{.br = 27, .cTbl = ctbl27nit, .aid = aid27, .elvAcl = elv_27, .elv = elv_27, .m_gray = m_gray_027},
	{.br = 29, .cTbl = ctbl29nit, .aid = aid29, .elvAcl = elv_29, .elv = elv_29, .m_gray = m_gray_029},
	{.br = 30, .cTbl = ctbl30nit, .aid = aid30, .elvAcl = elv_30, .elv = elv_30, .m_gray = m_gray_030},
	{.br = 32, .cTbl = ctbl32nit, .aid = aid32, .elvAcl = elv_30, .elv = elv_30, .m_gray = m_gray_032},
	{.br = 34, .cTbl = ctbl34nit, .aid = aid34, .elvAcl = elv_30, .elv = elv_30, .m_gray = m_gray_034},
	{.br = 37, .cTbl = ctbl37nit, .aid = aid37, .elvAcl = elv_30, .elv = elv_30, .m_gray = m_gray_037},
	{.br = 39, .cTbl = ctbl39nit, .aid = aid39, .elvAcl = elv_30, .elv = elv_30, .m_gray = m_gray_039},
	{.br = 41, .cTbl = ctbl41nit, .aid = aid41, .elvAcl = elv_41, .elv = elv_41, .m_gray = m_gray_041},
	{.br = 44, .cTbl = ctbl44nit, .aid = aid44, .elvAcl = elv_41, .elv = elv_41, .m_gray = m_gray_044},
	{.br = 47, .cTbl = ctbl47nit, .aid = aid47, .elvAcl = elv_41, .elv = elv_41, .m_gray = m_gray_047},
	{.br = 50, .cTbl = ctbl50nit, .aid = aid50, .elvAcl = elv_41, .elv = elv_41, .m_gray = m_gray_050},
	{.br = 53, .cTbl = ctbl53nit, .aid = aid53, .elvAcl = elv_41, .elv = elv_41, .m_gray = m_gray_053},
	{.br = 56, .cTbl = ctbl56nit, .aid = aid56, .elvAcl = elv_41, .elv = elv_41, .m_gray = m_gray_056},
	{.br = 60, .cTbl = ctbl60nit, .aid = aid60, .elvAcl = elv_41, .elv = elv_41, .m_gray = m_gray_060},
	{.br = 64, .cTbl = ctbl64nit, .aid = aid64, .elvAcl = elv_41, .elv = elv_41, .m_gray = m_gray_064},
	{.br = 68, .cTbl = ctbl68nit, .aid = aid68, .elvAcl = elv_41, .elv = elv_41, .m_gray = m_gray_068},
	{.br = 72, .cTbl = ctbl72nit, .aid = aid72, .elvAcl = elv_41, .elv = elv_41, .m_gray = m_gray_072},
	{.br = 77, .cTbl = ctbl77nit, .aid = aid77, .elvAcl = elv_41, .elv = elv_41, .m_gray = m_gray_077},
	{.br = 82, .cTbl = ctbl82nit, .aid = aid82, .elvAcl = elv_41, .elv = elv_41, .m_gray = m_gray_082},
	{.br = 87, .cTbl = ctbl87nit, .aid = aid87, .elvAcl = elv_41, .elv = elv_41, .m_gray = m_gray_087},
	{.br = 93, .cTbl = ctbl93nit, .aid = aid93, .elvAcl = elv_41, .elv = elv_41, .m_gray = m_gray_093},
	{.br = 98, .cTbl = ctbl98nit, .aid = aid98, .elvAcl = elv_41, .elv = elv_41, .m_gray = m_gray_098},
	{.br = 105, .cTbl = ctbl105nit, .aid = aid105, .elvAcl = elv_41, .elv = elv_41, .m_gray = m_gray_105},
	{.br = 111, .cTbl = ctbl111nit, .aid = aid110, .elvAcl = elv_111, .elv = elv_111, .m_gray = m_gray_111},
	{.br = 119, .cTbl = ctbl119nit, .aid = aid119, .elvAcl = elv_111, .elv = elv_111, .m_gray = m_gray_119},
	{.br = 126, .cTbl = ctbl126nit, .aid = aid126, .elvAcl = elv_111, .elv = elv_111, .m_gray = m_gray_126},
	{.br = 134, .cTbl = ctbl134nit, .aid = aid134, .elvAcl = elv_111, .elv = elv_111, .m_gray = m_gray_134},
	{.br = 143, .cTbl = ctbl143nit, .aid = aid143, .elvAcl = elv_143, .elv = elv_143, .m_gray = m_gray_143},
	{.br = 152, .cTbl = ctbl152nit, .aid = aid152, .elvAcl = elv_143, .elv = elv_143, .m_gray = m_gray_152},
	{.br = 162, .cTbl = ctbl162nit, .aid = aid162, .elvAcl = elv_143, .elv = elv_143, .m_gray = m_gray_162},
	{.br = 172, .cTbl = ctbl172nit, .aid = aid172, .elvAcl = elv_143, .elv = elv_143, .m_gray = m_gray_172},
	{.br = 183, .cTbl = ctbl183nit, .aid = aid183, .elvAcl = elv_183, .elv = elv_183, .m_gray = m_gray_183},
	{.br = 195, .cTbl = ctbl195nit, .aid = aid195, .elvAcl = elv_195, .elv = elv_195, .m_gray = m_gray_195},
	{.br = 207, .cTbl = ctbl207nit, .aid = aid207, .elvAcl = elv_207, .elv = elv_207, .m_gray = m_gray_207},
	{.br = 220, .cTbl = ctbl220nit, .aid = aid220, .elvAcl = elv_220, .elv = elv_220, .m_gray = m_gray_220},
	{.br = 234, .cTbl = ctbl234nit, .aid = aid234, .elvAcl = elv_234, .elv = elv_234, .m_gray = m_gray_234},
	{.br = 249, .cTbl = ctbl249nit, .aid = aid249, .elvAcl = elv_249, .elv = elv_249, .m_gray = m_gray_249},
	{.br = 265, .cTbl = ctbl265nit, .aid = aid265, .elvAcl = elv_265, .elv = elv_265, .m_gray = m_gray_265},
	{.br = 282, .cTbl = ctbl282nit, .aid = aid282, .elvAcl = elv_282, .elv = elv_282, .m_gray = m_gray_282},
	{.br = 300, .cTbl = ctbl300nit, .aid = aid300, .elvAcl = elv_300, .elv = elv_300, .m_gray = m_gray_300},
	{.br = 316, .cTbl = ctbl316nit, .aid = aid316, .elvAcl = elv_316, .elv = elv_316, .m_gray = m_gray_316},
	{.br = 333, .cTbl = ctbl333nit, .aid = aid333, .elvAcl = elv_333, .elv = elv_333, .m_gray = m_gray_333},
	{.br = 360, .cTbl = ctbl360nit, .aid = aid360, .elvAcl = elv_360, .elv = elv_360, .m_gray = m_gray_360, .way = W2},
	{.br = 500, .cTbl = ctbl360nit, .aid = aid360, .elvAcl = elv_HBM, .elv = elv_HBM, .m_gray = m_gray_360, .way = W2}
};

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
	u8 elvss_val[3] = {0, };
	u8 B6_GW[2] = {0xB6, };
	int real_br = 0;

	real_br = get_actual_br_value(lcd, lcd->br_index);
	elvss = get_elvss_from_index(lcd, lcd->br_index, lcd->acl_enable);

	if (elvss == NULL) {
		dev_err(&lcd->ld->dev, "%s: failed to get elvss value\n", __func__);
		return;
	}

	elvss_val[0] = elvss[0];
	elvss_val[1] = elvss[1];
	elvss_val[2] = elvss[2];

	B6_GW[1] = lcd->elvss22th;

	if (real_br <= 29) {
		if (lcd->temperature <= -20)
			B6_GW[1] = elvss[5];
		else if (lcd->temperature > -20 && lcd->temperature <= 0)
			B6_GW[1] = elvss[4];
		else
			B6_GW[1] = elvss[3];
	}


	if (dsim_write_hl_data(lcd, elvss_val, ELVSS_LEN) < 0)
		dev_err(&lcd->ld->dev, "%s: failed to write elvss\n", __func__);

	if (dsim_write_hl_data(lcd, SEQ_ELVSS_GLOBAL, ARRAY_SIZE(SEQ_ELVSS_GLOBAL)) < 0)
		dev_err(&lcd->ld->dev, "%s: failed to SEQ_ELVSS_GLOBAL\n", __func__);

	if (dsim_write_hl_data(lcd, B6_GW, 2) < 0)
		dev_err(&lcd->ld->dev, "%s: failed to write elvss\n", __func__);

}

static int dsim_panel_set_hbm(struct lcd_info *lcd, int force)
{
	int ret = 0, level = LEVEL_IS_HBM(lcd->brightness);

	if (force || lcd->current_hbm != lcd->hbm_tbl[level][1]) {
		lcd->current_hbm = lcd->hbm_tbl[level][1];
		if (dsim_write_hl_data(lcd, lcd->hbm_tbl[level], ARRAY_SIZE(SEQ_HBM_OFF)) < 0) {
			dev_err(&lcd->ld->dev, "failed to write hbm command.\n");
			ret = -EPERM;
		}

		if (level) {
			if (dsim_write_hl_data(lcd, lcd->hbm_gamma, HBM_GAMMA_CMD_CNT) < 0) {
				dev_err(&lcd->ld->dev, "failed to write hbm gamma command.\n");
				ret = -EPERM;
			}
		}

		dev_info(&lcd->ld->dev, "hbm: %d, brightness: %d\n", lcd->current_hbm, lcd->brightness);
	}

	return ret;
}

static int dsim_panel_set_acl(struct lcd_info *lcd, int force)
{
	int ret = 0, level = ACL_STATUS_15P, opr = ACL_OPR_32_FRAME;

	if (LEVEL_IS_HBM(lcd->brightness))
		goto acl_update;

	if (!lcd->acl_enable && !lcd->adaptive_control) {
		level = ACL_STATUS_0P;
		opr = ACL_OPR_16_FRAME;
	}

acl_update:
	if (force || lcd->current_acl_opr != opr) {
		lcd->current_acl_opr = opr;
		if (dsim_write_hl_data(lcd, lcd->acl_opr_tbl[opr], ARRAY_SIZE(SEQ_ACL_ON_OPR_AVR)) < 0) {
			dev_err(&lcd->ld->dev, "failed to write SEQ_ACL_ON/OFF_OPR_AVR command.\n");
			ret = -EPERM;
			goto exit;
		}
		dev_info(&lcd->ld->dev, "acl_opr: %d, adaptive_control: %d\n", lcd->current_acl_opr, lcd->adaptive_control);
	}

	if (force || lcd->current_acl != lcd->acl_cutoff_tbl[level][1]) {
		if (dsim_write_hl_data(lcd, lcd->acl_cutoff_tbl[level], 2) < 0) {
			dev_err(&lcd->ld->dev, "failed to write acl command.\n");
			ret = -EPERM;
			goto exit;
		}
		lcd->current_acl = lcd->acl_cutoff_tbl[level][1];
		dev_info(&lcd->ld->dev, "acl: %d, brightness: %d\n", lcd->current_acl, lcd->brightness);
	}
exit:
	return ret;
}

static int dsim_panel_set_tset(struct lcd_info *lcd, int force)
{
	int ret = 0;
	int tset = 0;
	unsigned char SEQ_TSET[TSET_LEN] = {TSET_REG, 0x00};

	tset = ((lcd->temperature >= 0) ? 0 : BIT(7)) | abs(lcd->temperature);

	if (force || (lcd->tset[0] != tset)) {
		lcd->tset[0] = SEQ_TSET[1] = tset;

		if (dsim_write_hl_data(lcd, SEQ_TSET_GP, ARRAY_SIZE(SEQ_TSET_GP)) < 0) {
			dev_err(&lcd->ld->dev, "failed to write SEQ_TSET_GP command.\n");
			ret = -EPERM;
		}

		if (dsim_write_hl_data(lcd, SEQ_TSET, ARRAY_SIZE(SEQ_TSET)) < 0) {
			dev_err(&lcd->ld->dev, "failed to write SEQ_TSET command.\n");
			ret = -EPERM;
		}

		dev_info(&lcd->ld->dev, "temperature: %d, tset: %d\n", lcd->temperature, SEQ_TSET[1]);
	}
	return ret;
}

static int low_level_set_brightness(struct lcd_info *lcd, int force)
{
	if (dsim_write_hl_data(lcd, SEQ_TEST_KEY_ON_F0, ARRAY_SIZE(SEQ_TEST_KEY_ON_F0)) < 0)
		dev_err(&lcd->ld->dev, "%s: failed to write F0 on command.\n", __func__);

	dsim_panel_gamma_ctrl(lcd);

	dsim_panel_aid_ctrl(lcd);

	dsim_panel_set_elvss(lcd);

	dsim_panel_set_hbm(lcd, force);

	dsim_panel_set_acl(lcd, force);

	if (dsim_write_hl_data(lcd, SEQ_GAMMA_UPDATE, ARRAY_SIZE(SEQ_GAMMA_UPDATE)) < 0)
		dev_err(&lcd->ld->dev, "%s: failed to write SEQ_GAMMA_UPDATE on command.\n", __func__);

	if (dsim_write_hl_data(lcd, SEQ_GAMMA_UPDATE_L, ARRAY_SIZE(SEQ_GAMMA_UPDATE_L)) < 0)
		dev_err(&lcd->ld->dev, "%s: failed to write SEQ_GAMMA_UPDATE_L on command.\n", __func__);

	dsim_panel_set_tset(lcd, force);

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

static int dsim_panel_set_brightness(struct lcd_info *lcd, int force)
{
	int ret = 0;

	struct dim_data *dimming;
	int p_br = lcd->bd->props.brightness;
	int acutal_br = 0;
	int real_br = 0;
	int prev_index = lcd->br_index;

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
	if (ret < 0)
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
	if (ret < 0)
		dev_err(&lcd->ld->dev, "%s: failed to set brightness\n", __func__);

	return ret;
}

static const struct backlight_ops panel_backlight_ops = {
	.get_brightness = panel_get_brightness,
	.update_status = panel_set_brightness,
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

	diminfo = dimming_info;

	lcd->dim_data = (void *)dimming;
	lcd->dim_info = (void *)diminfo; /* dimming info */

	lcd->br_tbl = (unsigned int *)br_tbl; /* backlight table */
	lcd->hbm_tbl = (unsigned char **)HBM_TABLE; /* command hbm on and off */
	lcd->acl_cutoff_tbl = (unsigned char **)ACL_CUTOFF_TABLE; /* ACL on and off command */
	lcd->acl_opr_tbl = (unsigned char **)ACL_OPR_TABLE;

	/* CENTER GAMMA V255 */
	for (j = 0; j < CI_MAX; j++) {
		temp = ((mtp[pos] & 0x01) ? -1 : 1) * mtp[pos+1];
		dimming->t_gamma[V255][j] = (int)center_gamma[V255][j] + temp;
		dimming->mtp[V255][j] = temp;
		pos += 2;
	}

	/* CENTER GAMME FOR V3 ~ V203 */
	for (i = V203; i > V0; i--) {
		for (j = 0; j < CI_MAX; j++) {
			temp = ((mtp[pos] & 0x80) ? -1 : 1) * (mtp[pos] & 0x7f);
			dimming->t_gamma[i][j] = (int)center_gamma[i][j] + temp;
			dimming->mtp[i][j] = temp;
			pos++;
		}
	}

	/* CENTER GAMMA FOR V0 */
	for (j = 0; j < CI_MAX; j++) {
		dimming->t_gamma[V0][j] = (int)center_gamma[V0][j] + temp;
		dimming->mtp[V0][j] = 0;
	}

	for (j = 0; j < CI_MAX; j++) {
		dimming->vt_mtp[j] = mtp[pos];
		pos++;
	}

	/* Center gamma */
	dimm_info("Center Gamma Info :\n");
	for (i = 0; i < VMAX; i++) {
		dev_info(&lcd->ld->dev, "Gamma : %3d %3d %3d : %3x %3x %3x\n",
			dimming->t_gamma[i][CI_RED], dimming->t_gamma[i][CI_GREEN], dimming->t_gamma[i][CI_BLUE],
			dimming->t_gamma[i][CI_RED], dimming->t_gamma[i][CI_GREEN], dimming->t_gamma[i][CI_BLUE]);
	}


	dimm_info("VT MTP :\n");
	dimm_info("Gamma : %3d %3d %3d : %3x %3x %3x\n",
			dimming->vt_mtp[CI_RED], dimming->vt_mtp[CI_GREEN], dimming->vt_mtp[CI_BLUE],
			dimming->vt_mtp[CI_RED], dimming->vt_mtp[CI_GREEN], dimming->vt_mtp[CI_BLUE]);

	/* MTP value get from ddi */
	dimm_info("MTP Info from ddi:\n");
	for (i = 0; i < VMAX; i++) {
		dimm_info("Gamma : %3d %3d %3d : %3x %3x %3x\n",
			dimming->mtp[i][CI_RED], dimming->mtp[i][CI_GREEN], dimming->mtp[i][CI_BLUE],
			dimming->mtp[i][CI_RED], dimming->mtp[i][CI_GREEN], dimming->mtp[i][CI_BLUE]);
	}

	ret = generate_volt_table(dimming);
	if (ret < 0) {
		dimm_err("[ERR:%s] failed to generate volt table\n", __func__);
		goto error;
	}

	for (i = 0; i < MAX_BR_INFO; i++) {
		if (diminfo[i].way == DIMMING_METHOD_AID) {
			ret = cal_gamma_from_index(dimming, &diminfo[i]);
			if (ret < 0) {
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

static int s6e8aa5x01_read_id(struct lcd_info *lcd)
{
	struct panel_private *priv = &lcd->dsim->priv;
	int ret = 0;

	lcd->id_info.value = 0;
	priv->lcdconnected = lcd->connected = lcdtype ? 1 : 0;

	/* id */
	mdelay(1);
	ret = dsim_read_hl_data(lcd, S6E8AA5X01_ID_REG, S6E8AA5X01_ID_LEN, lcd->id_info.id);
	if (ret < 0 || !lcd->id_info.value) {
		priv->lcdconnected = lcd->connected = 0;
		dev_err(&lcd->ld->dev, "%s: connected lcd is invalid\n", __func__);
	}

	dev_info(&lcd->ld->dev, "%s: %x\n", __func__, cpu_to_be32(lcd->id_info.value));

	return ret;
}

static int s6e8aa5x01_read_init_info(struct lcd_info *lcd, unsigned char *mtp)
{
	int i = 0;
	int ret;
	unsigned char bufForCoordi[S6E8AA5X01_COORDINATE_LEN] = {0, };
	unsigned char buf[S6E8AA5X01_MTP_DATE_SIZE] = {0, };

	dev_info(&lcd->ld->dev, "%s\n", __func__);

	s6e8aa5x01_read_id(lcd);

	ret = dsim_write_hl_data(lcd, SEQ_TEST_KEY_ON_F0, ARRAY_SIZE(SEQ_TEST_KEY_ON_F0));
	if (ret < 0)
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_TEST_KEY_ON_F0\n", __func__);

	/* chip id */
	msleep(1);
	ret = dsim_read_hl_data(lcd, S6E8AA5X01_CHIP_ID_REG, S6E8AA5X01_CHIP_ID_LEN, lcd->chip_id);
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to read chip id, check panel connection\n", __func__);
		goto read_fail;
	}

// 0xC8h Info
// 01~39th : gamma mtp
// 41~45th : manufacture date
// 73~87th : HBM gamma

#if 0
	/* mtp */
	ret = dsim_read_hl_data(lcd, S6E8AA5X01_MTP_ADDR, S6E8AA5X01_MTP_DATE_SIZE, buf);
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "failed to read mtp, check panel connection\n");
		goto read_fail;
	}
#else
	/* mtp */
	msleep(1);
	ret = dsim_read_hl_data(lcd, S6E8AA5X01_MTP_ADDR, S6E8AA5X01_MTP_SIZE_GAMMA, buf);
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "failed to read mtp, check panel connection\n");
		goto read_fail;
	}

	ret = dsim_write_hl_data(lcd, SEQ_MTP_READ_DATE_GP, ARRAY_SIZE(SEQ_MTP_READ_DATE_GP));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_MTP_READ_DATE_GP\n", __func__);
		goto read_fail;
	}

	/* mtp */
	msleep(1);
	ret = dsim_read_hl_data(lcd, S6E8AA5X01_MTP_ADDR, S6E8AA5X01_MTP_SIZE_DATE, buf + S6E8AA5X01_MTP_OFFSET_DATE);
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "failed to read mtp, check panel connection\n");
		goto read_fail;
	}

	ret = dsim_write_hl_data(lcd, SEQ_MTP_READ_HBM_GP, ARRAY_SIZE(SEQ_MTP_READ_HBM_GP));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_MTP_READ_HBM_GP\n", __func__);
		goto read_fail;
	}

	/* mtp */
	msleep(1);
	ret = dsim_read_hl_data(lcd, S6E8AA5X01_MTP_ADDR, S6E8AA5X01_MTP_SIZE_HBM, buf + S6E8AA5X01_MTP_OFFSET_HBM);
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "failed to read mtp, check panel connection\n");
		goto read_fail;
	}

#endif

	memcpy(mtp, buf, S6E8AA5X01_MTP_SIZE);

	dev_info(&lcd->ld->dev, "READ MTP SIZE : %d\n", S6E8AA5X01_MTP_SIZE);
	dev_info(&lcd->ld->dev, "=========== MTP INFO ===========\n");
	for (i = 0; i < S6E8AA5X01_MTP_SIZE; i++)
		dev_info(&lcd->ld->dev, "MTP[%2d] : %2d : %2x\n", i, mtp[i], mtp[i]);

	/* hbm gamma 34~39 and 73~87 */
	lcd->hbm_gamma[0] = 0xB4;
	lcd->hbm_gamma[1] = 0x0B;
	for (i = 0; i < 6; i++)
		lcd->hbm_gamma[i+2] = buf[i+33];
	for (i = 0; i < 15; i++)
		lcd->hbm_gamma[i+8] = buf[i+72];
	for (i = 23; i < 32; i++)
		lcd->hbm_gamma[i] = 0x80;
	for (i = 32; i < 35; i++)
		lcd->hbm_gamma[i] = 0x00;


	dev_info(&lcd->ld->dev, "READ MTP SIZE : %d\n", S6E8AA5X01_MTP_SIZE);
	dev_info(&lcd->ld->dev, "=========== HBM GAMMA INFO ===========\n");
	for (i = 0; i < S6E8AA5X01_MTP_HBM_GAMMA_SIZE; i++)
		dev_info(&lcd->ld->dev, "MTP[%2d] : %2d : %2x\n", i, lcd->hbm_gamma[i+2], lcd->hbm_gamma[i+2]);

	/* date */
	lcd->date[0] = buf[40]; /*year*/ /*month*/
	lcd->date[1] = buf[41]; /*day*/
	lcd->date[2] = buf[42]; /*hour*/
	lcd->date[3] = buf[43]; /*minute*/
	lcd->date[4] = buf[44]; /*second*/

	/* elvss */
	msleep(1);
	ret = dsim_read_hl_data(lcd, ELVSS_REG, ELVSS_MTP_LEN, buf);
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "failed to read mtp, check panel connection\n");
		goto read_fail;
	}

	lcd->elvss22th = buf[21]; /*reserve for turn off HBM */

	/* coordinate */
	msleep(1);
	ret = dsim_read_hl_data(lcd, S6E8AA5X01_COORDINATE_REG, S6E8AA5X01_COORDINATE_LEN, bufForCoordi);
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "failed to read coordinate on command.\n");
		goto read_fail;
	}
	lcd->coordinate[0] = bufForCoordi[3] << 8 | bufForCoordi[4];	/* X */
	lcd->coordinate[1] = bufForCoordi[5] << 8 | bufForCoordi[6];	/* Y */
	dev_info(&lcd->ld->dev, "READ coordi : ");
	for (i = 0; i < 2; i++)
		dev_info(&lcd->ld->dev, "%d, ", lcd->coordinate[i]);
	dev_info(&lcd->ld->dev, "\n");

	/* chip id */
	msleep(1);
	ret = dsim_read_hl_data(lcd, S6E8AA5X01_CODE_REG, S6E8AA5X01_CODE_LEN, lcd->code);
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "failed to read code on command.\n");
		goto read_fail;
	}
	dev_info(&lcd->ld->dev, "READ code : ");
	for (i = 0; i < S6E8AA5X01_CODE_LEN; i++)
		dev_info(&lcd->ld->dev, "%x, ", lcd->code[i]);
	dev_info(&lcd->ld->dev, "\n");

	ret = dsim_write_hl_data(lcd, SEQ_TEST_KEY_OFF_F0, ARRAY_SIZE(SEQ_TEST_KEY_OFF_F0));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_TEST_KEY_OFF_F0\n", __func__);
		goto read_fail;
	}

read_fail:
	return ret;
}

static int s6e8aa5x01_displayon(struct lcd_info *lcd)
{
	int ret = 0;

	dev_info(&lcd->ld->dev, "%s\n", __func__);

	ret = dsim_write_hl_data(lcd, SEQ_DISPLAY_ON, ARRAY_SIZE(SEQ_DISPLAY_ON));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : DISPLAY_ON\n", __func__);
		goto displayon_err;
	}

displayon_err:
	return ret;
}

static int s6e8aa5x01_exit(struct lcd_info *lcd)
{
	int ret = 0;

	dev_info(&lcd->ld->dev, "%s\n", __func__);
	ret = dsim_write_hl_data(lcd, SEQ_DISPLAY_OFF, ARRAY_SIZE(SEQ_DISPLAY_OFF));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : DISPLAY_OFF\n", __func__);
		goto exit_err;
	}

	msleep(20);

	ret = dsim_write_hl_data(lcd, SEQ_SLEEP_IN, ARRAY_SIZE(SEQ_SLEEP_IN));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SLEEP_IN\n", __func__);
		goto exit_err;
	}

	msleep(120);

exit_err:
	return ret;
}

static int s6e8aa5x01_init(struct lcd_info *lcd)
{
	int ret = 0;

	dev_info(&lcd->ld->dev, "%s\n", __func__);

	usleep_range(5000, 6000);

	s6e8aa5x01_read_id(lcd);

	ret = dsim_write_hl_data(lcd, SEQ_TEST_KEY_ON_F0, ARRAY_SIZE(SEQ_TEST_KEY_ON_F0));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_TEST_KEY_ON_F0\n", __func__);
		goto init_exit;
	}

	ret = dsim_write_hl_data(lcd, SEQ_SLEEP_OUT, ARRAY_SIZE(SEQ_SLEEP_OUT));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_SLEEP_OUT\n", __func__);
		goto init_exit;
	}

	msleep(20);

	/* 2. Brightness Setting */
	ret = dsim_write_hl_data(lcd, SEQ_GAMMA_CONDITION_SET, ARRAY_SIZE(SEQ_GAMMA_CONDITION_SET));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_GAMMA_CONDITION_SET\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_AID_360NIT, ARRAY_SIZE(SEQ_AID_360NIT));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_AID_360NIT\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_ELVSS_360NIT, ARRAY_SIZE(SEQ_ELVSS_360NIT));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_ELVSS_360NIT\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_ACL_OFF_OPR, ARRAY_SIZE(SEQ_ACL_OFF_OPR));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_ACL_OFF_OPR\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_ACL_OFF, ARRAY_SIZE(SEQ_ACL_OFF));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_ACL_OFF\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_GAMMA_UPDATE, ARRAY_SIZE(SEQ_GAMMA_UPDATE));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_GAMMA_UPDATE\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_TSET_GP, ARRAY_SIZE(SEQ_TSET_GP));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_TSET_GP\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_TSET, ARRAY_SIZE(SEQ_TSET));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_TSET\n", __func__);
		goto init_exit;
	}


	/* 3. Common Setting */
	ret = dsim_write_hl_data(lcd, SEQ_PENTILE_SETTING, ARRAY_SIZE(SEQ_PENTILE_SETTING));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_PENTILE_SETTING\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_DE_DIM_GP, ARRAY_SIZE(SEQ_DE_DIM_GP));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_DE_DIM_GP\n", __func__);
		goto init_exit;
	}
	ret = dsim_write_hl_data(lcd, SEQ_DE_DIM_SETTING, ARRAY_SIZE(SEQ_DE_DIM_SETTING));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_DE_DIM_SETTING\n", __func__);
		goto init_exit;
	}

	ret = dsim_write_hl_data(lcd, SEQ_TEST_KEY_OFF_F0, ARRAY_SIZE(SEQ_TEST_KEY_OFF_F0));
	if (ret < 0) {
		dev_err(&lcd->ld->dev, "%s: failed to write CMD : SEQ_TEST_KEY_OFF_F0\n", __func__);
		goto init_exit;
	}

	msleep(120);

init_exit:
	return ret;
}

static int s6e8aa5x01_probe(struct lcd_info *lcd)
{
	int ret = 0;
	unsigned char mtp[S6E8AA5X01_MTP_DATE_SIZE] = {0, };

	dev_info(&lcd->ld->dev, "+ %s\n", __func__);

	lcd->bd->props.max_brightness = EXTEND_BRIGHTNESS;
	lcd->bd->props.brightness = UI_DEFAULT_BRIGHTNESS;

	lcd->state = PANEL_STATE_RESUMED;
	lcd->temperature = NORMAL_TEMPERATURE;
	lcd->acl_enable = 0;
	lcd->current_acl = 0;
	lcd->current_hbm = 0;
	lcd->current_acl_opr = ACL_OPR_16_FRAME;
	lcd->adaptive_control = ACL_STATUS_15P;
	lcd->lux = -1;

	s6e8aa5x01_read_init_info(lcd, mtp);

	ret = init_dimming(lcd, mtp);
	if (ret < 0)
		dev_err(&lcd->ld->dev, "%s: failed to generate gamma table\n", __func__);

	dsim_panel_set_brightness(lcd, 1);

	dev_info(&lcd->ld->dev, "- %s\n", __func__);

	return 0;
}

static ssize_t lcd_type_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);

	sprintf(buf, "SDC_%02X%02X%02X\n", lcd->id_info.id[0], lcd->id_info.id[1], lcd->id_info.id[2]);

	return strlen(buf);
}

static ssize_t window_type_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);

	sprintf(buf, "%02x %02x %02x\n", lcd->id_info.id[0], lcd->id_info.id[1], lcd->id_info.id[2]);

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

	mutex_lock(&lcd->lock);
	lcd->temperature = value;
	mutex_unlock(&lcd->lock);

	dsim_panel_set_brightness(lcd, 1);

	dev_info(dev, "%s: %d, %d\n", __func__, value, lcd->temperature);

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

	year = lcd->date[0] + 2011;
	month = lcd->date[1];
	day = lcd->date[2];
	hour = lcd->date[3];
	min = lcd->date[4];

	sprintf(buf, "%d, %d, %d, %d:%d\n", year, month, day, hour, min);
	return strlen(buf);
}

static ssize_t cell_id_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);

	sprintf(buf, "%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X\n",
		lcd->date[0], lcd->date[1], lcd->date[2],
		lcd->date[3], lcd->date[4], 0x00, 0x00,
		(lcd->coordinate[0]&0xFF00)>>8, lcd->coordinate[0]&0x00FF,
		(lcd->coordinate[1]&0xFF00)>>8, lcd->coordinate[1]&0x00FF);

	return strlen(buf);
}


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

static ssize_t manufacture_code_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);

	sprintf(buf, "%02X%02X%02X%02X%02X\n",
		lcd->code[0], lcd->code[1], lcd->code[2], lcd->code[3], lcd->code[4]);

	return strlen(buf);
}

static ssize_t adaptive_control_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);

	sprintf(buf, "%d\n", lcd->adaptive_control);

	return strlen(buf);
}

static ssize_t adaptive_control_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);
	int rc;
	unsigned int value;

	rc = kstrtouint(buf, 0, &value);
	if (rc < 0)
		return rc;

	if (lcd->adaptive_control != value) {
		dev_info(&lcd->ld->dev, "%s: %d, %d\n", __func__, lcd->adaptive_control, value);
		mutex_lock(&lcd->lock);
		lcd->adaptive_control = value;
		mutex_unlock(&lcd->lock);
		if (lcd->state == PANEL_STATE_RESUMED)
			dsim_panel_set_brightness(lcd, 1);
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

#if defined(CONFIG_EXYNOS_DECON_MDNIE_LITE)
		attr_store_for_each(lcd->mdnie_class, attr->attr.name, buf, size);
#endif
	}

	return size;
}

static DEVICE_ATTR(lcd_type, 0444, lcd_type_show, NULL);
static DEVICE_ATTR(window_type, 0444, window_type_show, NULL);
static DEVICE_ATTR(manufacture_code, 0444, manufacture_code_show, NULL);
static DEVICE_ATTR(cell_id, 0444, cell_id_show, NULL);
static DEVICE_ATTR(brightness_table, 0444, brightness_table_show, NULL);
static DEVICE_ATTR(temperature, 0664, temperature_show, temperature_store);
static DEVICE_ATTR(color_coordinate, 0444, color_coordinate_show, NULL);
static DEVICE_ATTR(manufacture_date, 0444, manufacture_date_show, NULL);
static DEVICE_ATTR(aid_log, 0444, aid_log_show, NULL);
static DEVICE_ATTR(adaptive_control, 0664, adaptive_control_show, adaptive_control_store);
static DEVICE_ATTR(lux, 0644, lux_show, lux_store);
static DEVICE_ATTR(SVC_OCTA, 0444, cell_id_show, NULL);

static struct attribute *lcd_sysfs_attributes[] = {
	&dev_attr_lcd_type.attr,
	&dev_attr_window_type.attr,
	&dev_attr_manufacture_code.attr,
	&dev_attr_brightness_table.attr,
	&dev_attr_temperature.attr,
	&dev_attr_color_coordinate.attr,
	&dev_attr_manufacture_date.attr,
	&dev_attr_cell_id.attr,
	&dev_attr_aid_log.attr,
	&dev_attr_adaptive_control.attr,
	&dev_attr_lux.attr,
	NULL,
};

static const struct attribute_group lcd_sysfs_attr_group = {
	.attrs = lcd_sysfs_attributes,
};

static void lcd_init_svc(struct lcd_info *lcd)
{
	struct device *dev = &lcd->svc_dev;
	struct kobject *top_kobj = &lcd->ld->dev.kobj.kset->kobj;
	struct kernfs_node *kn = kernfs_find_and_get(top_kobj->sd, "svc");
	struct kobject *svc_kobj = NULL;
	char *buf, *path = NULL;
	int ret = 0;

	svc_kobj = kn ? kn->priv : kobject_create_and_add("svc", top_kobj);
	if (!svc_kobj)
		return;

	buf = kzalloc(PATH_MAX, GFP_KERNEL);
	if (buf) {
		path = kernfs_path(svc_kobj->sd, buf, PATH_MAX);
		dev_info(&lcd->ld->dev, "%s: %s %s\n", __func__, buf, !kn ? "create" : "");
		kfree(buf);
	}

	dev->kobj.parent = svc_kobj;
	dev_set_name(dev, "OCTA");
	dev_set_drvdata(dev, lcd);
	ret = device_register(dev);
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: device_register fail\n", __func__);
		return;
	}

	device_create_file(dev, &dev_attr_SVC_OCTA);

	if (kn)
		kernfs_put(kn);
}

static void lcd_init_sysfs(struct lcd_info *lcd)
{
	int ret = 0;

	ret = sysfs_create_group(&lcd->ld->dev.kobj, &lcd_sysfs_attr_group);
	if (ret < 0)
		dev_err(&lcd->ld->dev, "failed to add lcd sysfs\n");

	lcd_init_svc(lcd);
}


#if defined(CONFIG_EXYNOS_DECON_MDNIE_LITE)
static int mdnie_lite_write_set(struct lcd_info *lcd, struct lcd_seq_info *seq, u32 num)
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

	lcd->dsim = dsim;
	ret = s6e8aa5x01_probe(lcd);
	if (ret < 0)
		dev_err(&lcd->ld->dev, "%s: failed to probe panel\n", __func__);

	lcd_init_sysfs(lcd);

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

	if (lcd->state == PANEL_STATE_SUSPENED)
		s6e8aa5x01_init(lcd);

	s6e8aa5x01_displayon(lcd);

	lcd->state = PANEL_STATE_RESUMED;
	dsim_panel_set_brightness(lcd, 1);

	return 0;
}

static int dsim_panel_suspend(struct dsim_device *dsim)
{
	struct lcd_info *lcd = dsim->priv.par;

	mutex_lock(&lcd->lock);

	if (lcd->state == PANEL_STATE_SUSPENED)
		goto exit;

	lcd->state = PANEL_STATE_SUSPENDING;

	s6e8aa5x01_exit(lcd);

	lcd->state = PANEL_STATE_SUSPENED;

exit:
	mutex_unlock(&lcd->lock);

	return 0;
}

struct mipi_dsim_lcd_driver s6e8aa5x01_mipi_lcd_driver = {
	.probe		= dsim_panel_probe,
	.displayon	= dsim_panel_displayon,
	.suspend	= dsim_panel_suspend,
};

