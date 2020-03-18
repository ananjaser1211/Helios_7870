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

#include "s6e3fa3_a7xe_param.h"

#if defined(CONFIG_EXYNOS_DECON_MDNIE)
#include "mdnie.h"
#include "mdnie_lite_table_a7xe.h"
#endif

#define PANEL_STATE_SUSPENED	0
#define PANEL_STATE_RESUMED	1
#define PANEL_STATE_SUSPENDING	2

#define LEVEL_IS_HBM(brightness)		(brightness == EXTEND_BRIGHTNESS)
#define LEVEL_IS_ACL_OFF(brightness)		(brightness >= UI_MAX_BRIGHTNESS && brightness <= 281)

#define DSI_WRITE(cmd, size)		do {				\
	ret = dsim_write_hl_data(lcd, cmd, size);			\
	if (ret < 0)							\
		dev_info(&lcd->ld->dev, "%s: failed to write %s\n", __func__, #cmd);	\
} while (0)

#ifdef SMART_DIMMING_DEBUG
#define smtd_dbg(format, arg...)	printk(format, ##arg)
#else
#define smtd_dbg(format, arg...)
#endif

#define get_bit(value, shift, width)	((value >> shift) & (GENMASK(width - 1, 0)))

union aor_info {
	u32 value;
	struct {
		u8 aor_1;
		u8 aor_2;
		u16 reserved;
	};
};

union elvss_info {
	u32 value;
	struct {
		u8 mpscon;
		u8 offset;
		u8 offset_base;
		u8 reserved;
	};
};

struct hbm_interpolation_t {
	int		*hbm;
	const int	*gamma_default;

	const int	*ibr_tbl;
	int		idx_ref;
	int		idx_hbm;
};

struct lcd_info {
	unsigned int			connected;
	unsigned int			bl;
	unsigned int			brightness;
	unsigned int			acl_enable;
	unsigned int			current_acl;
	unsigned int			current_bl;
	union elvss_info		current_elvss;
	union aor_info			current_aor;
	unsigned int			current_hbm;
	unsigned int			state;

	struct lcd_device		*ld;
	struct backlight_device		*bd;
	struct dynamic_aid_param_t	daid;

	unsigned char			*tset_param;
	unsigned char			elvss_table[IBRIGHTNESS_HBM_MAX][TEMP_MAX][ELVSS_CMD_CNT];
	unsigned char			gamma_table[IBRIGHTNESS_HBM_MAX][GAMMA_CMD_CNT];

	unsigned char			(*aor_table)[AID_CMD_CNT];
	unsigned char			**acl_table;
	unsigned char			**opr_table;
	unsigned char			**hbm_table;

	int				temperature;
	unsigned int			temperature_index;

	union {
		struct {
			u8		reserved;
			u8		id[LDI_LEN_ID];
		};
		u32			value;
	} id_info;
	unsigned char			code[LDI_LEN_CHIP_ID];
	unsigned char			date[LDI_LEN_DATE];
	unsigned int			coordinate[2];

	unsigned int			adaptive_control;
	int				lux;
	struct class			*mdnie_class;

	struct dsim_device		*dsim;
	struct mutex			lock;

	struct hbm_interpolation_t	hitp;
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

static int dsim_read_info(struct lcd_info *lcd, u8 reg, u32 len, u8 *buf)
{
	int ret = 0, i;

	ret = dsim_read_hl_data(lcd, reg, len, buf);
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: fail. %02x, ret: %d\n", __func__, reg, ret);
		goto exit;
	}

	smtd_dbg("%s: %02xh\n", __func__, reg);
	for (i = 0; i < len; i++)
		smtd_dbg("%02dth value is %02x, %3d\n", i + 1, buf[i], buf[i]);

exit:
	return ret;
}

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

static void dsim_panel_gamma_ctrl(struct lcd_info *lcd, u8 force)
{
	u8 *gamma = NULL;
	int ret = 0;

	gamma = lcd->gamma_table[lcd->bl];
	if (gamma == NULL) {
		dev_info(&lcd->ld->dev, "%s: failed to get gamma\n", __func__);
		goto exit;
	}

	if (force)
		goto gamma_update;
	else if (lcd->current_bl != lcd->bl)
		goto gamma_update;
	else
		goto exit;

gamma_update:
	DSI_WRITE(gamma, GAMMA_CMD_CNT);

exit:
	return;
}

static void dsim_panel_aid_ctrl(struct lcd_info *lcd, u8 force)
{
	u8 *aid = NULL;
	int ret = 0;
	union aor_info aor_value;

	aid = lcd->aor_table[lcd->brightness];
	if (aid == NULL) {
		dev_info(&lcd->ld->dev, "%s: failed to get aid value\n", __func__);
		goto exit;
	}

	aor_value.aor_1 = aid[LDI_OFFSET_AOR_1];
	aor_value.aor_2 = aid[LDI_OFFSET_AOR_2];

	DSI_WRITE(aid, AID_CMD_CNT);
	lcd->current_aor.value = aor_value.value;
	dev_info(&lcd->ld->dev, "aor: %x\n", lcd->current_aor.value);

exit:
	return;
}

static int dsim_panel_set_tset(struct lcd_info *lcd, int force)
{
	int ret = 0;
	unsigned char tset = 0;

	tset = ((lcd->temperature < 0) ? BIT(7) : 0) | abs(lcd->temperature);

	if (force || lcd->tset_param[LDI_OFFSET_TSET] != tset) {
		lcd->tset_param[LDI_OFFSET_TSET] = tset;
		DSI_WRITE(lcd->tset_param, TSET_CMD_CNT);
		dev_info(&lcd->ld->dev, "temperature: %d, tset: %d\n", lcd->temperature, tset);
	}

	return ret;
}

static void dsim_panel_set_elvss(struct lcd_info *lcd, u8 force)
{
	u8 *elvss = NULL;
	int ret = 0;
	union elvss_info elvss_value;

	elvss = lcd->elvss_table[lcd->bl][lcd->temperature_index];
	if (elvss == NULL) {
		dev_info(&lcd->ld->dev, "%s: failed to get elvss value\n", __func__);
		goto exit;
	}

	elvss_value.mpscon = elvss[LDI_OFFSET_ELVSS_1];
	elvss_value.offset = elvss[LDI_OFFSET_ELVSS_2];
	elvss_value.offset_base = elvss[LDI_OFFSET_ELVSS_3];

	if (force)
		goto elvss_update;
	else if (lcd->current_elvss.value != elvss_value.value)
		goto elvss_update;
	else
		goto exit;

elvss_update:
	DSI_WRITE(elvss, ELVSS_CMD_CNT);
	lcd->current_elvss.value = elvss_value.value;
	dev_info(&lcd->ld->dev, "elvss: %x\n", lcd->current_elvss.value);

exit:
	return;
}

static int dsim_panel_set_acl(struct lcd_info *lcd, int force)
{
	int ret = 0, level = ACL_STATUS_8P;

	if (lcd->acl_enable)
		goto acl_update;

	if (LEVEL_IS_ACL_OFF(lcd->brightness) && !lcd->adaptive_control)
		level = ACL_STATUS_0P;

acl_update:
	if (force || lcd->current_acl != lcd->acl_table[level][LDI_OFFSET_ACL]) {
		DSI_WRITE(lcd->acl_table[level], ACL_CMD_CNT);
		DSI_WRITE(lcd->opr_table[level], OPR_CMD_CNT);
		lcd->current_acl = lcd->acl_table[level][LDI_OFFSET_ACL];
		dev_info(&lcd->ld->dev, "acl: %d, brightness: %d, adaptive_control: %d\n", lcd->current_acl, lcd->brightness, lcd->adaptive_control);
	}

	return ret;
}

static int dsim_panel_set_hbm(struct lcd_info *lcd, int force)
{
	int ret = 0, level = LEVEL_IS_HBM(lcd->brightness);

	if (force || lcd->current_hbm != lcd->hbm_table[level][LDI_OFFSET_HBM]) {
		DSI_WRITE(lcd->hbm_table[level], HBM_CMD_CNT);
		lcd->current_hbm = lcd->hbm_table[level][LDI_OFFSET_HBM];
		dev_info(&lcd->ld->dev, "hbm: %d, brightness: %d\n", lcd->current_hbm, lcd->brightness);
	}

	return ret;
}

static int low_level_set_brightness(struct lcd_info *lcd, int force)
{
	int ret = 0;

	DSI_WRITE(SEQ_TEST_KEY_ON_F0, ARRAY_SIZE(SEQ_TEST_KEY_ON_F0));
	DSI_WRITE(SEQ_TEST_KEY_ON_FC, ARRAY_SIZE(SEQ_TEST_KEY_ON_FC));

	dsim_panel_gamma_ctrl(lcd, force);

	dsim_panel_aid_ctrl(lcd, force);

	dsim_panel_set_tset(lcd, force);

	dsim_panel_set_elvss(lcd, force);

	DSI_WRITE(SEQ_GAMMA_UPDATE, ARRAY_SIZE(SEQ_GAMMA_UPDATE));

	dsim_panel_set_acl(lcd, force);

	dsim_panel_set_hbm(lcd, force);

	DSI_WRITE(SEQ_TEST_KEY_OFF_F0, ARRAY_SIZE(SEQ_TEST_KEY_OFF_F0));
	DSI_WRITE(SEQ_TEST_KEY_OFF_FC, ARRAY_SIZE(SEQ_TEST_KEY_OFF_FC));

	return 0;
}

static int get_backlight_level_from_brightness(int brightness)
{
	return brightness_table[brightness];
}

static int dsim_panel_set_brightness(struct lcd_info *lcd, int force)
{
	int ret = 0;

	mutex_lock(&lcd->lock);

	lcd->brightness = lcd->bd->props.brightness;

	lcd->bl = get_backlight_level_from_brightness(lcd->brightness);

	if (!force && lcd->state != PANEL_STATE_RESUMED) {
		dev_info(&lcd->ld->dev, "%s: panel is not active state\n", __func__);
		goto exit;
	}

	ret = low_level_set_brightness(lcd, force);
	if (ret < 0)
		dev_info(&lcd->ld->dev, "%s: failed to set brightness : %d\n", __func__, index_brightness_table[lcd->bl]);

	lcd->current_bl = lcd->bl;

	dev_info(&lcd->ld->dev, "brightness: %d, bl: %d, nit: %d, lx: %d\n", lcd->brightness, lcd->bl, index_brightness_table[lcd->bl], lcd->lux);
exit:
	mutex_unlock(&lcd->lock);

	return ret;
}

static int panel_get_brightness(struct backlight_device *bd)
{
	struct lcd_info *lcd = bl_get_data(bd);

	return index_brightness_table[lcd->bl];
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


static void init_dynamic_aid(struct lcd_info *lcd)
{
	lcd->daid.vreg = VREG_OUT_X1000;
	lcd->daid.iv_tbl = index_voltage_table;
	lcd->daid.iv_max = IV_MAX;
	lcd->daid.mtp = kzalloc(IV_MAX * CI_MAX * sizeof(int), GFP_KERNEL);
	lcd->daid.gamma_default = gamma_default;
	lcd->daid.formular = gamma_formula;
	lcd->daid.vt_voltage_value = vt_voltage_value;

	lcd->daid.ibr_tbl = index_brightness_table;
	lcd->daid.ibr_max = IBRIGHTNESS_MAX;

	lcd->daid.offset_color = (const struct rgb_t(*)[])offset_color;
	lcd->daid.iv_ref = index_voltage_reference;
	lcd->daid.m_gray = m_gray;
}

/* V255(msb is separated) ~ VT -> VT ~ V255(msb is not separated) and signed bit */
static void reorder_reg2mtp(u8 *reg, int *mtp)
{
	int j, c, v;

	for (c = 0, j = 0; c < CI_MAX; c++, j++) {
		if (reg[j++] & 0x01)
			mtp[(IV_MAX-1)*CI_MAX+c] = reg[j] * (-1);
		else
			mtp[(IV_MAX-1)*CI_MAX+c] = reg[j];
	}

	for (v = IV_MAX - 2; v >= 0; v--) {
		for (c = 0; c < CI_MAX; c++, j++) {
			if (reg[j] & 0x80)
				mtp[CI_MAX*v+c] = (reg[j] & 0x7F) * (-1);
			else
				mtp[CI_MAX*v+c] = reg[j];
		}
	}
}

/* V255(msb is separated) ~ VT -> VT ~ V255(msb is not separated) */
static void reorder_reg2gamma(u8 *reg, int *gamma)
{
	int j, c, v;

	for (c = 0, j = 0; c < CI_MAX; c++, j++) {
		if (reg[j++] & 0x01)
			gamma[(IV_MAX-1)*CI_MAX+c] = reg[j] | BIT(8);
		else
			gamma[(IV_MAX-1)*CI_MAX+c] = reg[j];
	}

	for (v = IV_MAX - 2; v >= 0; v--) {
		for (c = 0; c < CI_MAX; c++, j++) {
			if (reg[j] & 0x80)
				gamma[CI_MAX*v+c] = (reg[j] & 0x7F) | BIT(7);
			else
				gamma[CI_MAX*v+c] = reg[j];
		}
	}
}

/* VT ~ V255(msb is not separated) -> V255(msb is separated) ~ VT */
/* array idx zero (reg[0]) is reserved for gamma command address (0xCA) */
static void reorder_gamma2reg(int *gamma, u8 *reg)
{
	int j, c, v;
	int *pgamma;

	v = IV_MAX - 1;
	pgamma = &gamma[v * CI_MAX];
	for (c = 0, j = 1; c < CI_MAX; c++, pgamma++) {
		if (*pgamma & 0x100)
			reg[j++] = 1;
		else
			reg[j++] = 0;

		reg[j++] = *pgamma & 0xff;
	}

	for (v = IV_MAX - 2; v > IV_VT; v--) {
		pgamma = &gamma[v * CI_MAX];
		for (c = 0; c < CI_MAX; c++, pgamma++)
			reg[j++] = *pgamma;
	}

	v = IV_VT;
	pgamma = &gamma[v * CI_MAX];
	reg[j++] = pgamma[CI_RED] << 4 | pgamma[CI_GREEN];
	reg[j++] = pgamma[CI_BLUE];
}

static void init_mtp_data(struct lcd_info *lcd, u8 *mtp_data)
{
	int i, c;
	int *mtp = lcd->daid.mtp;
	u8 tmp[IV_MAX * CI_MAX + CI_MAX] = {0, };

	memcpy(tmp, mtp_data, LDI_LEN_MTP);

	/* C8h 34th Para: VT R / VT G */
	/* C8h 35th Para: VT B */
	tmp[33] = get_bit(mtp_data[33], 4, 4);
	tmp[34] = get_bit(mtp_data[33], 0, 4);
	tmp[35] = get_bit(mtp_data[34], 0, 4);

	reorder_reg2mtp(tmp, mtp);

	smtd_dbg("MTP_Offset_Value\n");
	for (i = 0; i < IV_MAX; i++) {
		for (c = 0; c < CI_MAX; c++)
			smtd_dbg("%4d ", mtp[i*CI_MAX+c]);
		smtd_dbg("\n");
	}
}

static int init_gamma(struct lcd_info *lcd, u8 *mtp_data)
{
	int i, j;
	int ret = 0;
	int **gamma;

	/* allocate memory for local gamma table */
	gamma = kcalloc(IBRIGHTNESS_MAX, sizeof(int *), GFP_KERNEL);
	if (!gamma) {
		pr_err("failed to allocate gamma table\n");
		ret = -ENOMEM;
		goto err_alloc_gamma_table;
	}

	for (i = 0; i < IBRIGHTNESS_MAX; i++) {
		gamma[i] = kcalloc(IV_MAX*CI_MAX, sizeof(int), GFP_KERNEL);
		if (!gamma[i]) {
			pr_err("failed to allocate gamma\n");
			ret = -ENOMEM;
			goto err_alloc_gamma;
		}
	}

	/* pre-allocate memory for gamma table */
	for (i = 0; i < IBRIGHTNESS_MAX; i++)
		memcpy(&lcd->gamma_table[i], SEQ_GAMMA_CONDITION_SET, GAMMA_CMD_CNT);

	/* calculate gamma table */
	init_mtp_data(lcd, mtp_data);
	dynamic_aid(lcd->daid, gamma);

	/* relocate gamma order */
	for (i = 0; i < IBRIGHTNESS_MAX; i++)
		reorder_gamma2reg(gamma[i], lcd->gamma_table[i]);

	for (i = 0; i < IBRIGHTNESS_MAX; i++) {
		smtd_dbg("Gamma [%3d] = ", lcd->daid.ibr_tbl[i]);
		for (j = 0; j < GAMMA_CMD_CNT; j++)
			smtd_dbg("%4d ", lcd->gamma_table[i][j]);
		smtd_dbg("\n");
	}

	/* free local gamma table */
	for (i = 0; i < IBRIGHTNESS_MAX; i++)
		kfree(gamma[i]);
	kfree(gamma);

	return 0;

err_alloc_gamma:
	while (i > 0) {
		kfree(gamma[i-1]);
		i--;
	}
	kfree(gamma);
err_alloc_gamma_table:
	return ret;
}

static int s6e3fa3_read_id(struct lcd_info *lcd)
{
	struct panel_private *priv = &lcd->dsim->priv;
	int ret = 0;
	struct decon_device *decon = get_decon_drvdata(0);
	static char *LDI_BIT_DESC_ID[BITS_PER_BYTE * LDI_LEN_ID] = {
		[0 ... 23] = "ID Read Fail",
	};

	lcd->id_info.value = 0;
	priv->lcdconnected = lcd->connected = lcdtype ? 1 : 0;

	ret = dsim_read_info(lcd, LDI_REG_ID, LDI_LEN_ID, lcd->id_info.id);
	if (ret < 0 || !lcd->id_info.value) {
		priv->lcdconnected = lcd->connected = 0;
		dev_info(&lcd->ld->dev, "%s: connected lcd is invalid\n", __func__);

		if (!lcdtype && decon)
			decon_abd_save_bit(&decon->abd, BITS_PER_BYTE * LDI_LEN_ID, cpu_to_be32(lcd->id_info.value), LDI_BIT_DESC_ID);
	}

	dev_info(&lcd->ld->dev, "%s: %x\n", __func__, cpu_to_be32(lcd->id_info.value));

	return ret;
}

static int s6e3fa3_read_mtp(struct lcd_info *lcd, unsigned char *mtp)
{
	int ret = 0;
	unsigned char buf[LDI_GPARA_DATE + LDI_LEN_DATE] = {0, };

	ret = dsim_read_info(lcd, LDI_REG_MTP, ARRAY_SIZE(buf), buf);
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: fail\n", __func__);
		goto exit;
	}

	memcpy(mtp, buf, LDI_LEN_MTP);
	memcpy(lcd->date, &buf[LDI_GPARA_DATE], LDI_LEN_DATE);

exit:
	return ret;
}

static int s6e3fa3_read_coordinate(struct lcd_info *lcd)
{
	int ret = 0;
	unsigned char buf[LDI_LEN_COORDINATE] = {0, };

	ret = dsim_read_info(lcd, LDI_REG_COORDINATE, LDI_LEN_COORDINATE, buf);
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: fail\n", __func__);
		goto exit;
	}

	lcd->coordinate[0] = buf[0] << 8 | buf[1];	/* X */
	lcd->coordinate[1] = buf[2] << 8 | buf[3];	/* Y */

exit:
	return ret;
}

static int s6e3fa3_read_chip_id(struct lcd_info *lcd)
{
	int ret = 0;

	ret = dsim_read_info(lcd, LDI_REG_CHIP_ID, LDI_LEN_CHIP_ID, lcd->code);
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: fail\n", __func__);
		goto exit;
	}

exit:
	return ret;
}

static int s6e3fa3_read_elvss(struct lcd_info *lcd, unsigned char *buf)
{
	int ret = 0;

	ret = dsim_read_info(lcd, LDI_REG_ELVSS, LDI_LEN_ELVSS, buf);
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: fail\n", __func__);
		goto exit;
	}

exit:
	return ret;
}

static int s6e3fa3_init_elvss(struct lcd_info *lcd, u8 *elvss_data)
{
	int i, temp, ret = 0;

	for (i = 0; i < IBRIGHTNESS_MAX; i++) {
		for (temp = 0; temp < TEMP_MAX; temp++) {
			/* Duplicate with reading value from DDI */
			memcpy(&lcd->elvss_table[i][temp][1], elvss_data, LDI_LEN_ELVSS);

			lcd->elvss_table[i][temp][0] = elvss_mpscon_offset_data[i][0];
			lcd->elvss_table[i][temp][LDI_OFFSET_ELVSS_1] = elvss_mpscon_offset_data[i][LDI_OFFSET_ELVSS_1];
			lcd->elvss_table[i][temp][LDI_OFFSET_ELVSS_2] = elvss_mpscon_offset_data[i][LDI_OFFSET_ELVSS_2];

			if (elvss_otp_data[i].nit && elvss_otp_data[i].not_otp[temp] != -1)
				lcd->elvss_table[i][temp][LDI_OFFSET_ELVSS_3] = elvss_otp_data[i].not_otp[temp];
		}
	}

	return ret;
}

static int s6e3fa3_read_hbm(struct lcd_info *lcd, unsigned char *buf)
{
	int ret = 0;

	ret = dsim_read_info(lcd, LDI_REG_HBM, LDI_LEN_HBM, buf);
	if (ret < 0) {
		dev_info(&lcd->ld->dev, "%s: fail\n", __func__);
		goto exit;
	}

exit:
	return ret;
}

static int s6e3fa3_init_hbm_elvss(struct lcd_info *lcd, u8 *elvss_data)
{
	int i, temp, ret = 0;

	for (i = IBRIGHTNESS_MAX; i < IBRIGHTNESS_HBM_MAX; i++) {
		for (temp = 0; temp < TEMP_MAX; temp++) {
			/* Duplicate with reading value from DDI */
			memcpy(&lcd->elvss_table[i][temp][1], elvss_data, LDI_LEN_ELVSS);

			lcd->elvss_table[i][temp][0] = elvss_mpscon_offset_data[i][0];
			lcd->elvss_table[i][temp][1] = elvss_mpscon_offset_data[i][1];
			lcd->elvss_table[i][temp][LDI_OFFSET_ELVSS_1] = elvss_mpscon_offset_data[i][LDI_OFFSET_ELVSS_1];
			lcd->elvss_table[i][temp][LDI_OFFSET_ELVSS_2] = elvss_mpscon_offset_data[i][LDI_OFFSET_ELVSS_2];
			if (i != IBRIGHTNESS_600NIT)
				lcd->elvss_table[i][temp][LDI_OFFSET_ELVSS_3] = elvss_data[LDI_GPARA_HBM_ELVSS];
		}
	}

	return ret;
}

static void init_hbm_interpolation(struct lcd_info *lcd)
{
	lcd->hitp.hbm = kzalloc(IV_MAX * CI_MAX * sizeof(int), GFP_KERNEL);
	lcd->hitp.gamma_default = gamma_default;

	lcd->hitp.ibr_tbl = index_brightness_table;
	lcd->hitp.idx_ref = IBRIGHTNESS_420NIT;
	lcd->hitp.idx_hbm = IBRIGHTNESS_600NIT;
}

static void init_hbm_data(struct lcd_info *lcd, u8 *hbm_data)
{
	int i, c;
	int *hbm = lcd->hitp.hbm;
	u8 tmp[IV_MAX * CI_MAX + CI_MAX] = {0, };
	u8 v255[CI_MAX][2] = {{0,}, };

	memcpy(&tmp[2], hbm_data, LDI_LEN_HBM);

	/* V255 */
	/* 1st: 0x0 & B4h 1st para D[2] */
	/* 2nd: B4h 2nd para */
	/* 3rd: 0x0 & B4h 1st para D[1] */
	/* 4th: B4h 3rd para */
	/* 5th: 0x0 & B4h 1st para D[0] */
	/* 6th: B4h 4th para */
	v255[CI_RED][0] = get_bit(hbm_data[0], 2, 1);
	v255[CI_RED][1] = hbm_data[1];

	v255[CI_GREEN][0] = get_bit(hbm_data[0], 1, 1);
	v255[CI_GREEN][1] = hbm_data[2];

	v255[CI_BLUE][0] = get_bit(hbm_data[0], 0, 1);
	v255[CI_BLUE][1] = hbm_data[3];

	tmp[0] = v255[CI_RED][0];
	tmp[1] = v255[CI_RED][1];
	tmp[2] = v255[CI_GREEN][0];
	tmp[3] = v255[CI_GREEN][1];
	tmp[4] = v255[CI_BLUE][0];
	tmp[5] = v255[CI_BLUE][1];

	reorder_reg2gamma(tmp, hbm);

	smtd_dbg("HBM_Gamma_Value\n");
	for (i = 0; i < IV_MAX; i++) {
		for (c = 0; c < CI_MAX; c++)
			smtd_dbg("%4d ", hbm[i*CI_MAX+c]);
		smtd_dbg("\n");
	}
}

static int init_hbm_gamma(struct lcd_info *lcd)
{
	int i, v, c, ret = 0;
	int *pgamma_def, *pgamma_hbm, *pgamma;
	s64 t1, t2, ratio;
	int gamma[IV_MAX * CI_MAX] = {0, };
	struct hbm_interpolation_t *hitp = &lcd->hitp;

	for (i = IBRIGHTNESS_MAX; i < IBRIGHTNESS_HBM_MAX; i++)
		memcpy(&lcd->gamma_table[i], SEQ_GAMMA_CONDITION_SET, GAMMA_CMD_CNT);

	for (i = IBRIGHTNESS_MAX; i < IBRIGHTNESS_HBM_MAX; i++) {
		t1 = hitp->ibr_tbl[i] - hitp->ibr_tbl[hitp->idx_ref];
		t2 = hitp->ibr_tbl[hitp->idx_hbm] - hitp->ibr_tbl[hitp->idx_ref];

		ratio = (t1 << 10) / t2;

		for (v = 0; v < IV_MAX; v++) {
			pgamma_def = (int *)&hitp->gamma_default[v*CI_MAX];
			pgamma_hbm = &hitp->hbm[v*CI_MAX];
			pgamma = &gamma[v*CI_MAX];

			for (c = 0; c < CI_MAX; c++) {
				t1 = pgamma_def[c];
				t1 = t1 << 10;
				t2 = pgamma_hbm[c] - pgamma_def[c];
				pgamma[c] = (t1 + (t2 * ratio)) >> 10;
			}
		}

		reorder_gamma2reg(gamma, lcd->gamma_table[i]);
	}

	for (i = IBRIGHTNESS_MAX; i < IBRIGHTNESS_HBM_MAX; i++) {
		smtd_dbg("Gamma [%3d] = ", lcd->hitp.ibr_tbl[i]);
		for (v = 0; v < GAMMA_CMD_CNT; v++)
			smtd_dbg("%4d ", lcd->gamma_table[i][v]);
		smtd_dbg("\n");
	}

	return ret;
}

static int s6e3fa3_init_hbm_interpolation(struct lcd_info *lcd)
{
	unsigned char hbm_data[LDI_LEN_HBM] = {0, };
	int ret = 0;

	s6e3fa3_read_hbm(lcd, hbm_data);

	init_hbm_interpolation(lcd);
	init_hbm_data(lcd, hbm_data);
	init_hbm_gamma(lcd);

	return ret;
}

static int s6e3fa3_exit(struct lcd_info *lcd)
{
	int ret = 0;

	dev_info(&lcd->ld->dev, "%s\n", __func__);

	/* 2. Display Off (28h) */
	DSI_WRITE(SEQ_DISPLAY_OFF, ARRAY_SIZE(SEQ_DISPLAY_OFF));

	/* 3. Wait 10ms */
	usleep_range(10000, 11000);

	/* 4. Sleep In (10h) */
	DSI_WRITE(SEQ_SLEEP_IN, ARRAY_SIZE(SEQ_SLEEP_IN));

	/* 5. Wait 150ms */
	msleep(150);

	return ret;
}

static int s6e3fa3_displayon(struct lcd_info *lcd)
{
	int ret = 0;

	dev_info(&lcd->ld->dev, "%s\n", __func__);

	/* 14. Display On(29h) */
	DSI_WRITE(SEQ_DISPLAY_ON, ARRAY_SIZE(SEQ_DISPLAY_ON));

	return ret;
}

static int s6e3fa3_init(struct lcd_info *lcd)
{
	int ret = 0;

	dev_info(&lcd->ld->dev, "%s\n", __func__);

	/* 7. Sleep Out(11h) */
	DSI_WRITE(SEQ_SLEEP_OUT, ARRAY_SIZE(SEQ_SLEEP_OUT));

	/* 8. Wait 20ms */
	msleep(20);

	/* 9. ID READ */
	s6e3fa3_read_id(lcd);

	/* Test Key Enable */
	DSI_WRITE(SEQ_TEST_KEY_ON_F0, ARRAY_SIZE(SEQ_TEST_KEY_ON_F0));
	DSI_WRITE(SEQ_TEST_KEY_ON_FC, ARRAY_SIZE(SEQ_TEST_KEY_ON_FC));

	/* 10. Common Setting */
	/* 4.1.2 PCD Setting */
	DSI_WRITE(SEQ_PCD_SET_DET_LOW, ARRAY_SIZE(SEQ_PCD_SET_DET_LOW));
	/* 4.1.3 ERR_FG Setting */
	DSI_WRITE(SEQ_ERR_FG_SETTING, ARRAY_SIZE(SEQ_ERR_FG_SETTING));
	/* 4.1.4 AVC Setting */
	DSI_WRITE(SEQ_AVC_SETTING_1, ARRAY_SIZE(SEQ_AVC_SETTING_1));
	DSI_WRITE(SEQ_AVC_SETTING_2, ARRAY_SIZE(SEQ_AVC_SETTING_2));

	DSI_WRITE(SEQ_SCALER_ON, ARRAY_SIZE(SEQ_SCALER_ON));
	DSI_WRITE(SEQ_COLUMN_ADDRESS, ARRAY_SIZE(SEQ_COLUMN_ADDRESS));
	DSI_WRITE(SEQ_PAGE_ADDRESS, ARRAY_SIZE(SEQ_PAGE_ADDRESS));

	dsim_panel_set_brightness(lcd, 1);

	/* Test Key Disable */
	DSI_WRITE(SEQ_TEST_KEY_OFF_F0, ARRAY_SIZE(SEQ_TEST_KEY_OFF_F0));
	DSI_WRITE(SEQ_TEST_KEY_OFF_FC, ARRAY_SIZE(SEQ_TEST_KEY_OFF_FC));

	/* 4.1.1 TE(Vsync) ON/OFF */
	DSI_WRITE(SEQ_TE_ON, ARRAY_SIZE(SEQ_TE_ON));

	/* 12. Wait 120ms */
	msleep(120);

	return ret;
}

static int s6e3fa3_read_init_info(struct lcd_info *lcd, unsigned char *mtp)
{
	int ret = 0;
	unsigned char elvss_data[LDI_LEN_ELVSS] = {0, };

	s6e3fa3_read_id(lcd);

	DSI_WRITE(SEQ_TEST_KEY_ON_F0, ARRAY_SIZE(SEQ_TEST_KEY_ON_F0));

	s6e3fa3_read_mtp(lcd, mtp);
	s6e3fa3_read_coordinate(lcd);
	s6e3fa3_read_chip_id(lcd);
	s6e3fa3_read_elvss(lcd, elvss_data);

	s6e3fa3_init_elvss(lcd, elvss_data);
	s6e3fa3_init_hbm_elvss(lcd, elvss_data);
	s6e3fa3_init_hbm_interpolation(lcd);

	DSI_WRITE(SEQ_TEST_KEY_OFF_F0, ARRAY_SIZE(SEQ_TEST_KEY_OFF_F0));

	return ret;
}

static int s6e3fa3_probe(struct lcd_info *lcd)
{
	int ret = 0;
	unsigned char mtp[LDI_LEN_MTP] = {0, };

	dev_info(&lcd->ld->dev, "+ %s\n", __func__);

	lcd->bd->props.max_brightness = EXTEND_BRIGHTNESS;
	lcd->bd->props.brightness = UI_DEFAULT_BRIGHTNESS;

	lcd->state = PANEL_STATE_RESUMED;

	lcd->temperature = NORMAL_TEMPERATURE;
	lcd->acl_enable = 0;
	lcd->current_acl = 0;
	lcd->current_hbm = 0;
	lcd->adaptive_control = ACL_STATUS_8P;
	lcd->lux = -1;

	lcd->acl_table = ACL_TABLE;
	lcd->opr_table = OPR_TABLE;
	lcd->hbm_table = HBM_TABLE;
	lcd->aor_table = AOR_TABLE;
	lcd->tset_param = SEQ_TSET_SETTING;

	ret = s6e3fa3_read_init_info(lcd, mtp);
	if (ret < 0)
		dev_info(&lcd->ld->dev, "%s: failed to init information\n", __func__);

	init_dynamic_aid(lcd);
	init_gamma(lcd, mtp);

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
	int i, bl;
	char *pos = buf;

	for (i = 0; i <= EXTEND_BRIGHTNESS; i++) {
		bl = get_backlight_level_from_brightness(i);
		pos += sprintf(pos, "%3d %3d\n", i, index_brightness_table[bl]);
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
	int value, rc, temperature_index = 0;

	rc = kstrtoint(buf, 10, &value);
	if (rc < 0)
		return rc;

	switch (value) {
	case 1:
		temperature_index = TEMP_ABOVE_MINUS_00_DEGREE;
		break;
	case 0:
	case -19:
		temperature_index = TEMP_ABOVE_MINUS_20_DEGREE;
		break;
	case -20:
		temperature_index = TEMP_BELOW_MINUS_20_DEGREE;
		break;
	}

	mutex_lock(&lcd->lock);
	lcd->temperature = value;
	lcd->temperature_index = temperature_index;
	mutex_unlock(&lcd->lock);

	if (lcd->state == PANEL_STATE_RESUMED)
		dsim_panel_set_brightness(lcd, 1);

	dev_info(dev, "%s: %d, %d, %d\n", __func__, value, lcd->temperature, lcd->temperature_index);

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
	u8 month, day, hour, min, sec;
	u16 ms;

	year = ((lcd->date[0] & 0xF0) >> 4) + 2011;
	month = lcd->date[0] & 0xF;
	day = lcd->date[1] & 0x1F;
	hour = lcd->date[2] & 0x1F;
	min = lcd->date[3] & 0x3F;
	sec = lcd->date[4];
	ms = (lcd->date[5] << 8) | lcd->date[6];

	sprintf(buf, "%04d, %02d, %02d, %02d:%02d:%02d.%04d\n", year, month, day, hour, min, sec, ms);

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

static ssize_t cell_id_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);

	sprintf(buf, "%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X\n",
		lcd->date[0], lcd->date[1], lcd->date[2], lcd->date[3], lcd->date[4],
		lcd->date[5], lcd->date[6], (lcd->coordinate[0] & 0xFF00) >> 8, lcd->coordinate[0] & 0x00FF,
		(lcd->coordinate[1] & 0xFF00) >> 8, lcd->coordinate[1] & 0x00FF);

	return strlen(buf);
}

static void show_aid_log(struct lcd_info *lcd)
{
	u8 temp[256];
	int i, j, k;
	int *mtp;

	mtp = lcd->daid.mtp;
	for (i = 0, j = 0; i < IV_MAX; i++, j += CI_MAX) {
		if (i == 0)
			dev_info(&lcd->ld->dev, "MTP Offset VT   : %4d %4d %4d\n",
				mtp[j + CI_RED], mtp[j + CI_GREEN], mtp[j + CI_BLUE]);
		else
			dev_info(&lcd->ld->dev, "MTP Offset V%3d : %4d %4d %4d\n",
				lcd->daid.iv_tbl[i], mtp[j + CI_RED], mtp[j + CI_GREEN], mtp[j + CI_BLUE]);
	}

	for (i = 0; i < IBRIGHTNESS_MAX; i++) {
		memset(temp, 0, sizeof(temp));
		for (j = 1; j < GAMMA_CMD_CNT; j++) {
			if (j == 1 || j == 3 || j == 5)
				k = lcd->gamma_table[i][j++] * 256;
			else
				k = 0;
			snprintf(temp + strnlen(temp, 256), 256, " %3d", lcd->gamma_table[i][j] + k);
		}

		dev_info(&lcd->ld->dev, "nit : %3d  %s\n", lcd->daid.ibr_tbl[i], temp);
	}

	mtp = lcd->hitp.hbm;
	for (i = 0, j = 0; i < IV_MAX; i++, j += CI_MAX) {
		if (i == 0)
			dev_info(&lcd->ld->dev, "HBM Gamma VT   : %4d %4d %4d\n",
				mtp[j + CI_RED], mtp[j + CI_GREEN], mtp[j + CI_BLUE]);
		else
			dev_info(&lcd->ld->dev, "HBM Gamma V%3d : %4d %4d %4d\n",
				lcd->daid.iv_tbl[i], mtp[j + CI_RED], mtp[j + CI_GREEN], mtp[j + CI_BLUE]);
	}

	for (i = IBRIGHTNESS_MAX; i < IBRIGHTNESS_HBM_MAX; i++) {
		memset(temp, 0, sizeof(temp));
		for (j = 1; j < GAMMA_CMD_CNT; j++) {
			if (j == 1 || j == 3 || j == 5)
				k = lcd->gamma_table[i][j++] * 256;
			else
				k = 0;
			snprintf(temp + strnlen(temp, 256), 256, " %3d", lcd->gamma_table[i][j] + k);
		}

		dev_info(&lcd->ld->dev, "nit : %3d  %s\n", lcd->daid.ibr_tbl[i], temp);
	}

	dev_info(&lcd->ld->dev, "%s\n", __func__);
}

static ssize_t aid_log_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info *lcd = dev_get_drvdata(dev);

	show_aid_log(lcd);

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

#if defined(CONFIG_EXYNOS_DECON_MDNIE)
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

static struct attribute *lcd_sysfs_attributes[] = {
	&dev_attr_lcd_type.attr,
	&dev_attr_window_type.attr,
	&dev_attr_manufacture_code.attr,
	&dev_attr_cell_id.attr,
	&dev_attr_temperature.attr,
	&dev_attr_color_coordinate.attr,
	&dev_attr_manufacture_date.attr,
	&dev_attr_aid_log.attr,
	&dev_attr_brightness_table.attr,
	&dev_attr_adaptive_control.attr,
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
		dev_info(&lcd->ld->dev, "failed to add lcd sysfs\n");
}


#if defined(CONFIG_EXYNOS_DECON_MDNIE)
int mdnie_send_seq(struct lcd_info *lcd, struct lcd_seq_info *seq, u32 num)
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
	ret = s6e3fa3_probe(lcd);
	if (ret < 0)
		dev_info(&lcd->ld->dev, "%s: failed to probe panel\n", __func__);

	lcd_init_sysfs(lcd);

#if defined(CONFIG_EXYNOS_DECON_MDNIE)
	mdnie_register(&lcd->ld->dev, lcd, (mdnie_w)mdnie_send_seq, (mdnie_r)mdnie_read, lcd->coordinate, &tune_info);
	lcd->mdnie_class = get_mdnie_class();
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
		s6e3fa3_init(lcd);

	s6e3fa3_displayon(lcd);

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

	s6e3fa3_exit(lcd);

	mutex_lock(&lcd->lock);
	lcd->state = PANEL_STATE_SUSPENED;
	mutex_unlock(&lcd->lock);

	dev_info(&lcd->ld->dev, "- %s: %d, %d\n", __func__, lcd->state, lcd->connected);

exit:
	return 0;
}

struct mipi_dsim_lcd_driver s6e3fa3_mipi_lcd_driver = {
	.name		= "s6e3fa3",
	.probe		= dsim_panel_probe,
	.displayon	= dsim_panel_displayon,
	.suspend	= dsim_panel_suspend,
};

