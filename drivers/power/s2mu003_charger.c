/*
 * S2MU003 Charger Driver
 *
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 * Author: Junhan Bae <junhan84.bae@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <linux/mfd/samsung/s2mu003.h>
#include <linux/power/s2mu003_charger.h>
#include <linux/version.h>
#include <linux/iio/consumer.h>

#if defined(CONFIG_MUIC_NOTIFIER)
#include <linux/muic/muic.h>
#include <linux/muic/muic_notifier.h>
#endif

#define ENABLE_MIVR 1

#define EN_OVP_IRQ 1
#define EN_IEOC_IRQ 1
#define EN_TOPOFF_IRQ 1
#define EN_BATP_IRQ 1
#define EN_RECHG_REQ_IRQ 1
#define EN_TR_IRQ 0
#define EN_MIVR_SW_REGULATION 0
#define EN_BST_IRQ 0
#define MINVAL(a, b) ((a <= b) ? a : b)

#define EOC_DEBOUNCE_CNT 2
#define HEALTH_DEBOUNCE_CNT 3

#define EOC_SLEEP 200
#define EOC_TIMEOUT (EOC_SLEEP * 6)
#ifndef EN_TEST_READ
#define EN_TEST_READ 0
#endif

#define MAX_FG_CHECK 100000

struct s2mu003_charger_data {
	struct i2c_client	*client;
	struct device		*dev;
	s2mu003_mfd_chip_t	*s2mu003;
	struct delayed_work	charger_work;
	struct workqueue_struct	*charger_wqueue;
	struct power_supply	psy_chg;
	struct power_supply	psy_battery;
	struct power_supply	psy_usb;
	struct power_supply	psy_ac;
	s2mu003_charger_platform_data_t *pdata;
	int charging_current;
	int siop_level;
	int cable_type;
	int battery_cable_type;
	bool is_charging;
	struct mutex io_lock;
	bool noti_check;
	bool adc_check;
	int rev_id;

	/* register programming */
	int reg_addr;
	int reg_data;

	bool full_charged;
	bool ovp;
	bool battery_valid;
	int unhealth_cnt;
	int status;
	int health;
	struct delayed_work polling_work;
#if defined(CONFIG_SEC_FUELGAUGE_S2MU003)
	int voltage_now;
	int voltage_avg;
	int voltage_ocv;
	unsigned int capacity;
	int temp_val;
#endif
#if defined(CONFIG_MUIC_NOTIFIER)
	struct notifier_block cable_check;
#endif
	struct iio_channel *adc_val;
};

static char *s2mu003_supplied_to[] = {
	"s2mu003-battery",
};

static enum power_supply_property s2mu003_power_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static enum power_supply_property s2mu003_charger_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CHARGE_OTG_CONTROL,
};

static enum power_supply_property s2mu003_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_SERIAL_NUMBER,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
};

static int s2mu003_get_charging_health(struct s2mu003_charger_data *charger);

static void s2mu003_test_read(struct i2c_client *i2c)
{
	int data;
	char str[1024] = {0,};
	int i;

	/* S2MU003 REG: 0x00 ~ 0x0E */
	for (i = 0x0; i <= 0x0E; i++) {
		data = s2mu003_reg_read(i2c, i);
		snprintf(str+strlen(str), sizeof(str) - strlen(str),
						  "0x%02x, ", data);
	}

	pr_info("%s: %s\n", __func__, str);
}

static void s2mu003_enable_charging_termination(struct i2c_client *i2c,
		int onoff)
{
	pr_info("%s:[BATT] Do termination set(%d)\n", __func__, onoff);

	if (onoff)
		s2mu003_set_bits(i2c, S2MU003_CHG_CTRL1, S2MU003_TEEN_MASK);
	else
		s2mu003_clr_bits(i2c, S2MU003_CHG_CTRL1, S2MU003_TEEN_MASK);
}

static int s2mu003_input_current_limit[] = {
	100,
	500,
	700,
	900,
	1000,
	1500,
	2000,
};

static void s2mu003_set_input_current_limit(
		struct s2mu003_charger_data *charger, int current_limit)
{
	int i, curr_reg = 0, curr_limit = 0;

	for (i = 0; i < ARRAY_SIZE(s2mu003_input_current_limit); i++) {
		if (current_limit <= s2mu003_input_current_limit[i]) {
			curr_reg = i + 1;
			break;
		}
	}

	if (charger->rev_id == 0xB) {
		curr_limit = current_limit >= 1500 ? 0xA : 0x0;
		s2mu003_assign_bits(charger->client, 0x92, 0xF, curr_limit);
	}

	mutex_lock(&charger->io_lock);
	s2mu003_assign_bits(charger->client, S2MU003_CHG_CTRL1,
		S2MU003_AICR_LIMIT_MASK, curr_reg << S2MU003_AICR_LIMIT_SHIFT);
	mutex_unlock(&charger->io_lock);
}

static int s2mu003_get_input_current_limit(struct i2c_client *i2c)
{
	int ret = s2mu003_reg_read(i2c, S2MU003_CHG_CTRL1);

	if (ret < 0)
		return ret;

	ret &= S2MU003_AICR_LIMIT_MASK;
	ret >>= S2MU003_AICR_LIMIT_SHIFT;
	if (ret == 0)
		return 2000 + 1; /* no limitation */

	return s2mu003_input_current_limit[ret - 1];
}

static void s2mu003_set_regulation_voltage(struct s2mu003_charger_data *charger,
					   int float_voltage)
{
	int data;

	if (float_voltage < 3650)
		data = 0;
	else if (float_voltage >= 3650 && float_voltage <= 4375)
		data = (float_voltage - 3650) / 25;
	else
		data = 0x37;

	mutex_lock(&charger->io_lock);
	s2mu003_assign_bits(charger->client,
			S2MU003_CHG_CTRL2, S2MU003_VOREG_MASK,
			data << S2MU003_VOREG_SHIFT);
	mutex_unlock(&charger->io_lock);
}

static void s2mu003_set_fast_charging_current(struct i2c_client *i2c,
					      int charging_current)
{
	int data;

	if (charging_current < 700)
		data = 0;
	else if (charging_current >= 700 && charging_current <= 2000)
		data = (charging_current - 700) / 100;
	else
		data = 0xd;

	s2mu003_assign_bits(i2c, S2MU003_CHG_CTRL5, S2MU003_ICHRG_MASK,
			data << S2MU003_ICHRG_SHIFT);
}

static int s2mu003_eoc_level[] = {
	0,
	150,
	200,
	250,
	300,
	400,
	500,
	600,
};

static int s2mu003_get_eoc_level(int eoc_current)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(s2mu003_eoc_level); i++) {
		if (eoc_current < s2mu003_eoc_level[i]) {
			if (i == 0)
				return 0;
			return i - 1;
		}
	}

	return ARRAY_SIZE(s2mu003_eoc_level) - 1;
}

static int s2mu003_get_current_eoc_setting(struct s2mu003_charger_data *charger)
{
	int ret;

	mutex_lock(&charger->io_lock);
	ret = s2mu003_reg_read(charger->client, S2MU003_CHG_CTRL4);
	mutex_unlock(&charger->io_lock);

	if (ret < 0) {
		pr_info("%s: warning --> fail to read i2c register(%d)\n",
							__func__, ret);
		return ret;
	}
	return s2mu003_eoc_level[(S2MU003_IEOC_MASK & ret)
					>> S2MU003_IEOC_SHIFT];
}

static int s2mu003_get_fast_charging_current(struct i2c_client *i2c)
{
	int data = s2mu003_reg_read(i2c, S2MU003_CHG_CTRL5);

	if (data < 0)
		return data;

	data = (data >> 4) & 0x0f;

	if (data > 0xd)
		data = 0xd;

	return data * 100 + 700;
}

static void s2mu003_set_termination_current_limit(struct i2c_client *i2c,
						  int current_limit)
{
	int data = s2mu003_get_eoc_level(current_limit);

	pr_info("%s : Set Termination\n", __func__);

	s2mu003_assign_bits(i2c, S2MU003_CHG_CTRL4, S2MU003_IEOC_MASK,
			    data << S2MU003_IEOC_SHIFT);
}
/* eoc re set */
static void s2mu003_set_charging_current(struct s2mu003_charger_data *charger,
		int eoc)
{
	int adj_current;

	adj_current = charger->charging_current * charger->siop_level / 100;

	mutex_lock(&charger->io_lock);
	s2mu003_set_fast_charging_current(charger->client, adj_current);
	if (eoc) /* set EOC RESET */
		s2mu003_set_termination_current_limit(charger->client, eoc);
	mutex_unlock(&charger->io_lock);
}

enum {
	S2MU003_MIVR_DISABLE = 0,
	S2MU003_MIVR_4200MV,
	S2MU003_MIVR_4300MV,
	S2MU003_MIVR_4400MV,
	S2MU003_MIVR_4500MV,
	S2MU003_MIVR_4600MV,
	S2MU003_MIVR_4700MV,
	S2MU003_MIVR_4800MV,
};

#if ENABLE_MIVR
/* charger input regulation voltage setting */
static void s2mu003_set_mivr_level(struct s2mu003_charger_data *charger)
{
	int mivr = S2MU003_MIVR_4600MV;

	mutex_lock(&charger->io_lock);
	s2mu003_assign_bits(charger->client, S2MU003_CHG_CTRL4,
			S2MU003_MIVR_MASK, mivr << S2MU003_MIVR_SHIFT);
	mutex_unlock(&charger->io_lock);
}
#endif /*ENABLE_MIVR*/

static void s2mu003_charger_otg_control(struct s2mu003_charger_data *charger,
					bool enable)
{
	pr_info("%s: called charger otg control : %s\n", __func__,
			enable ? "on" : "off");

	if (!enable) {
		/* turn off OTG */
		s2mu003_clr_bits(charger->client, S2MU003_CHG_CTRL8, 0x80);
		s2mu003_set_bits(charger->client, S2MU003_CHG_CTRL1,
						    S2MU003_SEL_SWFREQ_MASK);
		s2mu003_clr_bits(charger->client,
			S2MU003_CHG_CTRL1, S2MU003_OPAMODE_MASK);
	} else {
		/* Set OTG boost vout = 5V, turn on OTG */
		s2mu003_assign_bits(charger->client,
				S2MU003_CHG_CTRL2, S2MU003_VOREG_MASK,
				0x37 << S2MU003_VOREG_SHIFT);
		s2mu003_set_bits(charger->client,
				S2MU003_CHG_CTRL1, S2MU003_OPAMODE_MASK);
		s2mu003_clr_bits(charger->client, S2MU003_CHG_CTRL1,
						    S2MU003_SEL_SWFREQ_MASK);
		s2mu003_set_bits(charger->client, S2MU003_CHG_CTRL8, 0x80);
		charger->cable_type = POWER_SUPPLY_TYPE_OTG;
	}
}

/* this function will work well on CHIP_REV = 3 or later */
static void s2mu003_enable_charger_switch(struct s2mu003_charger_data *charger,
					  int onoff)
{
	int prev_charging_status = charger->is_charging;
	union power_supply_propval val;

	charger->is_charging = onoff ? true : false;
	if ((onoff > 0) && (prev_charging_status == false)) {
		pr_info("%s: turn on charger\n", __func__);
		s2mu003_set_bits(charger->client,
			S2MU003_CHG_CTRL3, S2MU003_CHG_EN_MASK);
	} else if (onoff == 0) {
		psy_do_property("battery", get,
				POWER_SUPPLY_PROP_STATUS, val);
		if (val.intval != POWER_SUPPLY_STATUS_FULL)
			charger->full_charged = false;
		pr_info("%s: turn off charger\n", __func__);
		charger->charging_current = 1700;
		s2mu003_set_input_current_limit(charger, 500);
		s2mu003_set_charging_current(charger, 250);
		charger->charging_current = 0;
	} else {
		pr_info("%s: repeated to set charger switch(%d), prev stat = %d\n",
				__func__, onoff, prev_charging_status ? 1 : 0);
	}
}

static void s2mu003_configure_charger(struct s2mu003_charger_data *charger)
{
	int eoc;
	union power_supply_propval val;

	pr_info("%s : Set config charging\n", __func__);
	if (charger->charging_current < 0) {
		pr_info("%s : OTG is activated. Ignore command!\n",
				__func__);
		return;
	}

	s2mu003_clr_bits(charger->client,
			S2MU003_CHG_CTRL3, S2MU003_CHG_EN_MASK);
#if ENABLE_MIVR
	s2mu003_set_mivr_level(charger);
#endif /*DISABLE_MIVR*/
	psy_do_property("battery", get,
			POWER_SUPPLY_PROP_CHARGE_NOW, val);

	/* Input current limit */
	pr_info("%s : input current (%dmA)\n",
			__func__, charger->pdata->charging_current_table
			[charger->cable_type].input_current_limit);

	s2mu003_set_input_current_limit(charger,
			charger->pdata->charging_current_table
			[charger->cable_type].input_current_limit);

	/* Float voltage */
	pr_info("%s : float voltage (%dmV)\n",
			__func__, charger->pdata->chg_float_voltage);

	s2mu003_set_regulation_voltage(charger,
			charger->pdata->chg_float_voltage);

	charger->charging_current = charger->pdata->charging_current_table
		[charger->cable_type].fast_charging_current;
	eoc = charger->pdata->charging_current_table
		[charger->cable_type].full_check_current_1st;
	/* Fast charge and Termination current */
	pr_info("%s : fast charging current (%dmA)\n",
			__func__, charger->charging_current);

	pr_info("%s : termination current (%dmA)\n",
			__func__, eoc);

	s2mu003_set_charging_current(charger, eoc);
	s2mu003_set_bits(charger->client,
			S2MU003_CHG_CTRL3, S2MU003_CHG_EN_MASK);

	s2mu003_enable_charger_switch(charger, 1);
}
#if defined(CONFIG_SEC_FUELGAUGE_S2MU003)
static void s2mu003_adc_init(struct s2mu003_charger_data *charger)
{
	charger->adc_val = iio_channel_get_all(charger->dev);
}
#endif
/* here is set init charger data */
static bool s2mu003_chg_init(struct s2mu003_charger_data *charger)
{
	int ret = 0;

	dev_info(&charger->client->dev, "%s : DEV ID : 0x%x\n", __func__,
			charger->rev_id);

	if (charger->pdata->is_1MHz_switching)
		ret = s2mu003_set_bits(charger->client,
			S2MU003_CHG_CTRL1, S2MU003_SEL_SWFREQ_MASK);
	else
		ret = s2mu003_clr_bits(charger->client,
			S2MU003_CHG_CTRL1, S2MU003_SEL_SWFREQ_MASK);

	ret = s2mu003_set_bits(charger->client, 0x8D, 0x80);

	s2mu003_assign_bits(charger->client, 0x95, 0x07, 0x0);
	s2mu003_assign_bits(charger->client, 0x8C, 0x70, 0x4 << 4);
	/* disable ic reset even if battery removed */
	s2mu003_clr_bits(charger->client, 0x8A, 0x80);

	s2mu003_clr_bits(charger->client, 0x8A, 0x1 << 5);

	/* Disable Timer function (Charging timeout fault) */
	s2mu003_clr_bits(charger->client,
			S2MU003_CHG_CTRL3, S2MU003_TIMEREN_MASK);

	/* Disable TE */
	s2mu003_enable_charging_termination(charger->client, 0);

	/* EMI improvement , let reg0x18 bit2~5 be 1100*/
	/* s2mu003_assign_bits(charger->s2mu003->i2c_client, 0x18, 0x3C, 0x30); */

	/* MUST set correct regulation voltage first
	 * Before MUIC pass cable type information to charger
	 * charger would be already enabled (default setting)
	 * it might cause EOC event by incorrect regulation voltage */
	s2mu003_set_regulation_voltage(charger,
			charger->pdata->chg_float_voltage);
#if !(ENABLE_MIVR)
	s2mu003_assign_bits(charger->client,
			S2MU003_CHG_CTRL4, S2MU003_MIVR_MASK,
			S2MU003_MIVR_DISABLE << S2MU003_MIVR_SHIFT);
#endif
	/* TOP-OFF debounce time set 256us */
	s2mu003_assign_bits(charger->client, S2MU003_CHG_CTRL2, 0x3, 0x3);

	s2mu003_clr_bits(charger->client,
			S2MU003_CHG_CTRL1, S2MU003_EN_CHGT_MASK);

	s2mu003_assign_bits(charger->client, 0x8A, 0x07, 0x03);

	return true;

}

static int s2mu003_get_charging_status(struct s2mu003_charger_data *charger)
{
	int status = POWER_SUPPLY_STATUS_UNKNOWN;
	int ret;

	ret = s2mu003_reg_read(charger->client, S2MU003_CHG_STATUS1);
	if (ret < 0)
		pr_info("Error : can't get charging status (%d)\n", ret);

	if (charger->full_charged)
		return POWER_SUPPLY_STATUS_FULL;

	switch (ret & 0x30) {
	case 0x00:
		status = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	case 0x20:
		status = POWER_SUPPLY_STATUS_CHARGING;
		break;
	case 0x30:
		status = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	}

	if (charger->charging_current < 0) {
		/* For OTG mode, S2MU003 would still report "charging" */
		status = POWER_SUPPLY_STATUS_DISCHARGING;
		ret = s2mu003_reg_read(charger->client, S2MU003_CHG_IRQ3);
		if (ret & 0x80) {
			pr_info("%s: otg overcurrent limit\n", __func__);
			s2mu003_charger_otg_control(charger, false);
		}

	}

	return status;
}

static int s2mu003_get_charge_type(struct i2c_client *iic)
{
	int status = POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
	int ret;

	ret = s2mu003_reg_read(iic, S2MU003_CHG_STATUS1);
	if (ret < 0)
		dev_err(&iic->dev, "%s fail\n", __func__);

	switch (ret&0x40) {
	case 0x40:
		status = POWER_SUPPLY_CHARGE_TYPE_FAST;
		break;
	default:
		/* pre-charge mode */
		status = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
		break;
	}

	return status;
}

static bool s2mu003_get_batt_present(struct i2c_client *iic)
{
	int ret = s2mu003_reg_read(iic, S2MU003_CHG_STATUS2);
	if (ret < 0)
		return false;
	return (ret & 0x08) ? false : true;
}

static int s2mu003_get_charging_health(struct s2mu003_charger_data *charger)
{
	int ret = s2mu003_reg_read(charger->client, S2MU003_CHG_STATUS1);

	if (ret < 0)
		return POWER_SUPPLY_HEALTH_UNKNOWN;

	if (ret & (0x03 << 2)) {
		charger->ovp = false;
		charger->unhealth_cnt = 0;
		return POWER_SUPPLY_HEALTH_GOOD;
	}
	charger->unhealth_cnt++;
	if (charger->unhealth_cnt < HEALTH_DEBOUNCE_CNT)
		return POWER_SUPPLY_HEALTH_GOOD;

	charger->unhealth_cnt = HEALTH_DEBOUNCE_CNT;
	if (charger->ovp)
		return POWER_SUPPLY_HEALTH_OVERVOLTAGE;
	return POWER_SUPPLY_HEALTH_UNDERVOLTAGE;
}

static int sec_chg_get_property(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	int chg_curr, aicr;
	struct s2mu003_charger_data *charger =
		container_of(psy, struct s2mu003_charger_data, psy_chg);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = charger->charging_current ? 1 : 0;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = s2mu003_get_charging_status(charger);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = s2mu003_get_charging_health(charger);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = 2000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		if (charger->charging_current) {
			aicr = s2mu003_get_input_current_limit(charger->client);
			chg_curr =
			s2mu003_get_fast_charging_current(charger->client);
			val->intval = MINVAL(aicr, chg_curr);
		} else
			val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = s2mu003_get_charge_type(charger->client);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = s2mu003_get_batt_present(charger->client);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int sec_chg_set_property(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	struct s2mu003_charger_data *charger =
		container_of(psy, struct s2mu003_charger_data, psy_chg);

	int eoc;
	int previous_cable_type = charger->cable_type;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		charger->status = val->intval;
		break;
		/* val->intval : type */
	case POWER_SUPPLY_PROP_ONLINE:
		charger->cable_type = val->intval;
		if (charger->cable_type == POWER_SUPPLY_TYPE_BATTERY ||
				charger->cable_type ==
				POWER_SUPPLY_TYPE_UNKNOWN) {
			pr_info("%s:[BATT] Type Battery\n", __func__);
			s2mu003_enable_charger_switch(charger, 0);

			if (previous_cable_type == POWER_SUPPLY_TYPE_OTG)
				s2mu003_charger_otg_control(charger, false);
			s2mu003_clr_bits(charger->client, 0x8A, 0x1 << 5);
		} else if (charger->cable_type == POWER_SUPPLY_TYPE_OTG) {
			pr_info("%s: OTG mode\n", __func__);
			s2mu003_clr_bits(charger->client, 0x8A, 0x1 << 5);
			s2mu003_charger_otg_control(charger, true);
		} else {
			pr_info("%s:[BATT] Set charging"", Cable type = %d\n",
						__func__, charger->cable_type);
			s2mu003_set_bits(charger->client, 0x8A, 0x1 << 5);
			/* Enable charger */
			s2mu003_configure_charger(charger);
		}
#if EN_TEST_READ
		msleep(100);
		s2mu003_test_read(charger->s2mu003->i2c_client);
#endif
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		/* set charging current */
		if (charger->is_charging) {
			/* decrease the charging current
					according to siop level */
			charger->siop_level = val->intval;
			pr_info("%s:SIOP level = %d, chg current = %d\n",
			__func__, val->intval, charger->charging_current);
			eoc = s2mu003_get_current_eoc_setting(charger);
			s2mu003_set_charging_current(charger, 0);
		}
		break;
	case POWER_SUPPLY_PROP_POWER_NOW:
		eoc = s2mu003_get_current_eoc_setting(charger);
		pr_info("%s:Set Power Now -> chg current = %d mA, eoc = %d mA\n",
						__func__, val->intval, eoc);
		s2mu003_set_charging_current(charger, 0);
		break;
	case POWER_SUPPLY_PROP_CHARGE_OTG_CONTROL:
		s2mu003_charger_otg_control(charger, val->intval);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

struct s2mu003_chg_irq_handler {
	char *name;
	int irq_index;
	irqreturn_t (*handler)(int irq, void *data);
};

#if EN_BATP_IRQ
static irqreturn_t s2mu003_chg_batp_irq_handler(int irq, void *data)
{
	struct s2mu003_charger_data *charger = data;

	pr_info("%s : battery is Disconnected\n", __func__);
	if (charger->rev_id == 0x02 || charger->rev_id == 0x0a ||
			charger->rev_id == 0x0b || charger->rev_id == 0x0c) {
		s2mu003_set_bits(charger->client, 0x64, 0x1 << 3);
		s2mu003_set_bits(charger->client, 0x75, 0x1);
		s2mu003_assign_bits(charger->client, 0x8A, 0x7, 0x1);
		charger->battery_valid = false;
		if (charger->rev_id == 0x0c)
			s2mu003_clr_bits(charger->client, 0x8A, 0x1 << 5);
	}

	return IRQ_HANDLED;
}
#endif /* EN_BATP_IRQ */

#if EN_OVP_IRQ
static void s2mu003_ovp_work(struct work_struct *work)
{
	struct s2mu003_charger_data *charger =
	container_of(work, struct s2mu003_charger_data, charger_work.work);
	union power_supply_propval value;
	int status;

	status = s2mu003_reg_read(charger->client, S2MU003_CHG_STATUS1);

	/* PWR ready = 0*/
	if ((status & (0x04)) == 0) {
		/* No need to disable charger,
		 * H/W will do it automatically */
		charger->unhealth_cnt = HEALTH_DEBOUNCE_CNT;
		charger->ovp = true;
		pr_info("%s: OVP triggered\n", __func__);
		value.intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		psy_do_property("battery", set,
				POWER_SUPPLY_PROP_HEALTH, value);
	} else {
		charger->unhealth_cnt = 0;
		charger->ovp = false;
	}
}

static irqreturn_t s2mu003_chg_vin_ovp_irq_handler(int irq, void *data)
{
	struct s2mu003_charger_data *charger = data;

	/* Delay 100ms for debounce */
	queue_delayed_work(charger->charger_wqueue,
				&charger->charger_work, msecs_to_jiffies(100));

	return IRQ_HANDLED;
}
#endif /* EN_OVP_IRQ */

#if EN_IEOC_IRQ
static irqreturn_t s2mu003_chg_ieoc_irq_handler(int irq, void *data)
{
	struct s2mu003_charger_data *charger = data;

	pr_info("%s : Full charged\n", __func__);
	charger->full_charged = true;

	return IRQ_HANDLED;
}
#endif /* EN_IEOC_IRQ */

#if EN_TOPOFF_IRQ
static irqreturn_t s2mu003_chg_topoff_irq_handler(int irq, void *data)
{
	struct s2mu003_charger_data *charger = data;

	pr_info("%s : Full charged\n", __func__);
	charger->full_charged = true;
	return IRQ_HANDLED;
}
#endif /* EN_IEOC_IRQ */

#if EN_RECHG_REQ_IRQ
static irqreturn_t s2mu003_chg_rechg_request_irq_handler(int irq, void *data)
{
	struct s2mu003_charger_data *charger = data;
	pr_info("%s: Recharging requesting\n", __func__);

	charger->full_charged = false;

	return IRQ_HANDLED;
}
#endif /* EN_RECHG_REQ_IRQ */

#if EN_TR_IRQ
static irqreturn_t s2mu003_chg_otp_tr_irq_handler(int irq, void *data)
{
	pr_info("%s : Over temperature : thermal regulation loop active\n",
			__func__);
	/* if needs callback, do it here */
	return IRQ_HANDLED;
}
#endif

const struct s2mu003_chg_irq_handler s2mu003_chg_irq_handlers[] = {
#if EN_BATP_IRQ
	{
		.name = "chg_batp",
		.handler = s2mu003_chg_batp_irq_handler,
		.irq_index = S2MU003_BATP_IRQ,
	},
#endif /* EN_BATP_IRQ */
#if EN_OVP_IRQ
	{
		.name = "chg_cinovp",
		.handler = s2mu003_chg_vin_ovp_irq_handler,
		.irq_index = S2MU003_CINOVP_IRQ,
	},
#endif /* EN_OVP_IRQ */
#if EN_IEOC_IRQ
	{
		.name = "chg_eoc",
		.handler = s2mu003_chg_ieoc_irq_handler,
		.irq_index = S2MU003_EOC_IRQ,
	},
#endif /* EN_IEOC_IRQ */
#if EN_TOPOFF_IRQ
	{
		.name = "chg_topoff",
		.handler = s2mu003_chg_topoff_irq_handler,
		.irq_index = S2MU003_TOPOFF_IRQ,
	},
#endif /* EN_IEOC_IRQ */
#if EN_RECHG_REQ_IRQ
	{
		.name = "chg_rechg",
		.handler = s2mu003_chg_rechg_request_irq_handler,
		.irq_index = S2MU003_RECHG_IRQ,
	},
#endif /* EN_RECHG_REQ_IRQ*/
#if EN_TR_IRQ
	{
		.name = "chg_chgtr",
		.handler = s2mu003_chg_otp_tr_irq_handler,
		.irq_index = S2MU003_CHGTR_IRQ,
	},
#endif /* EN_TR_IRQ */

#if EN_MIVR_SW_REGULATION
	{
		.name = "chg_chgvinvr",
		.handler = s2mu003_chg_mivr_irq_handler,
		.irq_index = S2MU003_CHGVINVR_IRQ,
	},
#endif /* EN_MIVR_SW_REGULATION */
#if EN_BST_IRQ
	{
		.name = "chg_bstinlv",
		.handler = s2mu003_chg_otg_fail_irq_handler,
		.irq_index = S2MU003_BSTINLV_IRQ,
	},
	{
		.name = "chg_bstilim",
		.handler = s2mu003_chg_otg_fail_irq_handler,
		.irq_index = S2MU003_BSTILIM_IRQ,
	},
	{
		.name = "chg_vmidovp",
		.handler = s2mu003_chg_otg_fail_irq_handler,
		.irq_index = S2MU003_VMIDOVP_IRQ,
	},
#endif /* EN_BST_IRQ */
};

static int register_irq(struct platform_device *pdev,
		struct s2mu003_charger_data *charger)
{
	int irq;
	int i, j;
	int ret;
	const struct s2mu003_chg_irq_handler *irq_handler =
					s2mu003_chg_irq_handlers;
	const char *irq_name;
	for (i = 0; i < ARRAY_SIZE(s2mu003_chg_irq_handlers); i++) {
		irq_name = s2mu003_get_irq_name_by_index
				(irq_handler[i].irq_index);
		irq = platform_get_irq_byname(pdev, irq_name);
		ret = request_threaded_irq(irq, NULL, irq_handler[i].handler,
				IRQF_ONESHOT | IRQF_TRIGGER_RISING |
				IRQF_NO_SUSPEND, irq_name, charger);
		if (ret < 0) {
			pr_err("%s : Failed to request IRQ (%s): #%d: %d\n",
					__func__, irq_name, irq, ret);
			goto err_irq;
		}

		pr_info("%s : Register IRQ%d(%s) successfully\n",
				__func__, irq, irq_name);
	}

	return 0;
err_irq:
	for (j = 0; j < i; j++) {
		irq_name = s2mu003_get_irq_name_by_index
				(irq_handler[j].irq_index);
		irq = platform_get_irq_byname(pdev, irq_name);
		free_irq(irq, charger);
	}

	return ret;
}

static void unregister_irq(struct platform_device *pdev,
		struct s2mu003_charger_data *charger)
{
	int irq;
	int i;
	const char *irq_name;
	const struct s2mu003_chg_irq_handler *irq_handler =
					s2mu003_chg_irq_handlers;

	for (i = 0; i < ARRAY_SIZE(s2mu003_chg_irq_handlers); i++) {
		irq_name = s2mu003_get_irq_name_by_index
				(irq_handler[i].irq_index);
		irq = platform_get_irq_byname(pdev, irq_name);
		free_irq(irq, charger);
	}
}

static int s2mu003_usb_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct s2mu003_charger_data *charger =
		container_of(psy, struct s2mu003_charger_data, psy_usb);

	if (psp != POWER_SUPPLY_PROP_ONLINE)
		return -EINVAL;

	/* Set enable=1 only if the USB charger is connected */
	switch (charger->battery_cable_type) {
	case POWER_SUPPLY_TYPE_USB:
	case POWER_SUPPLY_TYPE_USB_DCP:
	case POWER_SUPPLY_TYPE_USB_CDP:
	case POWER_SUPPLY_TYPE_USB_ACA:
		val->intval = 1;
		break;
	default:
		val->intval = 0;
		break;
	}

	return 0;
}

static int s2mu003_ac_get_property(struct power_supply *psy,
			       enum power_supply_property psp,
			       union power_supply_propval *val)
{
	struct s2mu003_charger_data *charger =
		container_of(psy, struct s2mu003_charger_data, psy_ac);

	if (psp != POWER_SUPPLY_PROP_ONLINE)
		return -EINVAL;

	/* Set enable=1 only if the AC charger is connected */
	switch (charger->battery_cable_type) {
	case POWER_SUPPLY_TYPE_MAINS:
	case POWER_SUPPLY_TYPE_UARTOFF:
	case POWER_SUPPLY_TYPE_LAN_HUB:
	case POWER_SUPPLY_TYPE_UNKNOWN:
	case POWER_SUPPLY_TYPE_HV_PREPARE_MAINS:
	case POWER_SUPPLY_TYPE_HV_ERR:
	case POWER_SUPPLY_TYPE_HV_UNKNOWN:
	case POWER_SUPPLY_TYPE_HV_MAINS:
		val->intval = 1;
		break;
	default:
		val->intval = 0;
		break;
	}

	return 0;
}

static int s2mu003_battery_get_property(struct power_supply *psy,
		enum power_supply_property psp, union power_supply_propval *val)
{
	struct s2mu003_charger_data *charger =
		container_of(psy, struct s2mu003_charger_data, psy_battery);
#if defined(CONFIG_SEC_FUELGAUGE_S2MU003)
	union power_supply_propval value;
	int charger_status = 0;
#endif
	int ret = 0;

	dev_dbg(&charger->client->dev, "prop: %d\n", psp);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = s2mu003_get_charging_status(charger);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = s2mu003_get_charging_health(charger);
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = charger->battery_cable_type;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = charger->battery_valid;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = POWER_SUPPLY_CHARGE_TYPE_FAST;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LIPO;
		break;
	case POWER_SUPPLY_PROP_SCOPE:
		val->intval = POWER_SUPPLY_SCOPE_SYSTEM;
		break;
#if defined(CONFIG_SEC_FUELGAUGE_S2MU003)
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		if (!charger->battery_valid)
			val->intval = FAKE_BAT_LEVEL;
		else {
			psy_do_property_dup(charger->pdata->fuelgauge_name, get,
					POWER_SUPPLY_PROP_VOLTAGE_NOW, value);
			charger->voltage_now = value.intval;
			dev_err(&charger->client->dev,
				"%s: voltage now(%d)\n", __func__,
							charger->voltage_now);
			val->intval = charger->voltage_now * 1000;
		}
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_AVG:
		value.intval = SEC_BATTERY_VOLTAGE_AVERAGE;
		psy_do_property_dup(charger->pdata->fuelgauge_name, get,
				POWER_SUPPLY_PROP_VOLTAGE_AVG, value);
		charger->voltage_avg = value.intval;
		dev_err(&charger->client->dev,
			"%s: voltage avg(%d)\n", __func__,
						charger->voltage_avg);
		val->intval = charger->voltage_now * 1000;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		if (!charger->battery_valid)
			val->intval = FAKE_BAT_LEVEL;
		else
			val->intval = charger->temp_val;
		break;
#endif
	case POWER_SUPPLY_PROP_CAPACITY:
#if defined(CONFIG_SEC_FUELGAUGE_S2MU003)
		if (!charger->battery_valid)
			val->intval = FAKE_BAT_LEVEL;
		else {
			charger_status = s2mu003_get_charging_status(charger);
			if (charger_status
					== POWER_SUPPLY_STATUS_FULL)
				val->intval = 100;
			else
				val->intval = charger->capacity;
		}
#else
		val->intval = FAKE_BAT_LEVEL;
#endif
		break;
	default:
		ret = -ENODATA;
	}

	return ret;
}

static int s2mu003_battery_set_property(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	struct s2mu003_charger_data *charger =
		container_of(psy, struct s2mu003_charger_data, psy_battery);
	int ret = 0;

	dev_dbg(&charger->client->dev, "prop: %d\n", psp);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		charger->status = val->intval;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		charger->health = val->intval;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		charger->battery_cable_type = val->intval;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

#if defined(CONFIG_MUIC_NOTIFIER)
static int s2mu003_bat_cable_check(struct s2mu003_charger_data *charger,
				muic_attached_dev_t attached_dev)
{
	int current_cable_type = -1;

	pr_debug("[%s]ATTACHED(%d)\n", __func__, attached_dev);

	switch (attached_dev) {
	case ATTACHED_DEV_JIG_UART_OFF_MUIC:
		break;
	case ATTACHED_DEV_SMARTDOCK_MUIC:
	case ATTACHED_DEV_DESKDOCK_MUIC:
		current_cable_type = POWER_SUPPLY_TYPE_BATTERY;
		break;
	case ATTACHED_DEV_OTG_MUIC:
	case ATTACHED_DEV_JIG_UART_OFF_VB_OTG_MUIC:
	case ATTACHED_DEV_HMT_MUIC:
		current_cable_type = POWER_SUPPLY_TYPE_OTG;
		break;
	case ATTACHED_DEV_USB_MUIC:
	case ATTACHED_DEV_JIG_USB_OFF_MUIC:
	case ATTACHED_DEV_JIG_USB_ON_MUIC:
	case ATTACHED_DEV_SMARTDOCK_USB_MUIC:
	case ATTACHED_DEV_UNOFFICIAL_ID_USB_MUIC:
		current_cable_type = POWER_SUPPLY_TYPE_USB;
		break;
	case ATTACHED_DEV_JIG_UART_OFF_VB_MUIC:
	case ATTACHED_DEV_JIG_UART_OFF_VB_FG_MUIC:
		current_cable_type = POWER_SUPPLY_TYPE_UARTOFF;
		break;
	case ATTACHED_DEV_TA_MUIC:
	case ATTACHED_DEV_CARDOCK_MUIC:
	case ATTACHED_DEV_DESKDOCK_VB_MUIC:
	case ATTACHED_DEV_SMARTDOCK_TA_MUIC:
	case ATTACHED_DEV_AFC_CHARGER_5V_MUIC:
	case ATTACHED_DEV_UNOFFICIAL_TA_MUIC:
	case ATTACHED_DEV_UNOFFICIAL_ID_TA_MUIC:
	case ATTACHED_DEV_UNOFFICIAL_ID_ANY_MUIC:
	case ATTACHED_DEV_QC_CHARGER_5V_MUIC:
	case ATTACHED_DEV_UNSUPPORTED_ID_VB_MUIC:
		current_cable_type = POWER_SUPPLY_TYPE_MAINS;
		break;
	case ATTACHED_DEV_CDP_MUIC:
	case ATTACHED_DEV_UNOFFICIAL_ID_CDP_MUIC:
		current_cable_type = POWER_SUPPLY_TYPE_USB_CDP;
		break;
	case ATTACHED_DEV_USB_LANHUB_MUIC:
		current_cable_type = POWER_SUPPLY_TYPE_LAN_HUB;
		break;
	case ATTACHED_DEV_CHARGING_CABLE_MUIC:
		current_cable_type = POWER_SUPPLY_TYPE_POWER_SHARING;
		break;
	case ATTACHED_DEV_AFC_CHARGER_PREPARE_MUIC:
	case ATTACHED_DEV_QC_CHARGER_PREPARE_MUIC:
		current_cable_type = POWER_SUPPLY_TYPE_HV_PREPARE_MAINS;
		break;
	case ATTACHED_DEV_AFC_CHARGER_9V_MUIC:
	case ATTACHED_DEV_QC_CHARGER_9V_MUIC:
		current_cable_type = POWER_SUPPLY_TYPE_HV_MAINS;
		break;
	case ATTACHED_DEV_AFC_CHARGER_ERR_V_MUIC:
	case ATTACHED_DEV_QC_CHARGER_ERR_V_MUIC:
		current_cable_type = POWER_SUPPLY_TYPE_HV_ERR;
		break;
	case ATTACHED_DEV_UNDEFINED_CHARGING_MUIC:
		current_cable_type = POWER_SUPPLY_TYPE_UNKNOWN;
		break;
	case ATTACHED_DEV_HV_ID_ERR_UNDEFINED_MUIC:
	case ATTACHED_DEV_HV_ID_ERR_UNSUPPORTED_MUIC:
	case ATTACHED_DEV_HV_ID_ERR_SUPPORTED_MUIC:
		current_cable_type = POWER_SUPPLY_TYPE_HV_UNKNOWN;
		break;
	default:
		pr_err("%s: invalid type for charger:%d\n",
			__func__, attached_dev);
	}

	return current_cable_type;

}

static int charger_handle_notification(struct notifier_block *nb,
		unsigned long action, void *data)
{
	muic_attached_dev_t attached_dev = *(muic_attached_dev_t *)data;
	const char *cmd;
	int cable_type;
	struct s2mu003_charger_data *charger =
		container_of(nb, struct s2mu003_charger_data,
			     cable_check);
	union power_supply_propval value;

	if (attached_dev == ATTACHED_DEV_MHL_MUIC)
		return 0;

	switch (action) {
	case MUIC_NOTIFY_CMD_DETACH:
	case MUIC_NOTIFY_CMD_LOGICALLY_DETACH:
		cmd = "DETACH";
		cable_type = POWER_SUPPLY_TYPE_BATTERY;
		break;
	case MUIC_NOTIFY_CMD_ATTACH:
	case MUIC_NOTIFY_CMD_LOGICALLY_ATTACH:
		cmd = "ATTACH";
		cable_type = s2mu003_bat_cable_check(charger, attached_dev);
		break;
	default:
		cmd = "ERROR";
		cable_type = -1;
		break;
	}

	pr_info("%s: current_cable(%d) former cable_type(%d) battery_valid(%d)\n",
			__func__, cable_type, charger->battery_cable_type,
						   charger->battery_valid);
	if (charger->battery_valid == false) {
		pr_info("%s: Battery is disconnected\n",
						__func__);
		return 0;
	}

	if (attached_dev == ATTACHED_DEV_OTG_MUIC) {
		if (!strcmp(cmd, "ATTACH")) {
			value.intval = true;
			charger->battery_cable_type = cable_type;
			psy_do_property(charger->pdata->charger_name, set,
					POWER_SUPPLY_PROP_CHARGE_OTG_CONTROL,
					value);
			pr_info("%s: OTG cable attached\n", __func__);
		} else {
			value.intval = false;
			charger->battery_cable_type = cable_type;
			psy_do_property(charger->pdata->charger_name, set,
					POWER_SUPPLY_PROP_CHARGE_OTG_CONTROL,
					value);
			pr_info("%s: OTG cable detached\n", __func__);
		}
	}

	if ((cable_type >= 0) &&
	    cable_type <= SEC_SIZEOF_POWER_SUPPLY_TYPE) {
		if (cable_type != charger->battery_cable_type) {
			value.intval = charger->battery_cable_type = cable_type;
			psy_do_property(charger->pdata->charger_name, set,
					POWER_SUPPLY_PROP_ONLINE,
					value);
		} else {
			pr_info("%s: Cable is Not Changed(%d)\n",
				__func__, charger->battery_cable_type);
		}
	}
	power_supply_changed(&charger->psy_battery);

	pr_info("%s: CMD=%s, attached_dev=%d battery_cable=%d\n",
		__func__, cmd, attached_dev, charger->battery_cable_type);

	return 0;
}
#endif /* CONFIG_MUIC_NOTIFIER */
#if defined(CONFIG_SEC_FUELGAUGE_S2MU003)
static int sec_bat_adc_to_data(struct s2mu003_charger_data *charger,
							    int data)
{
	const sec_bat_adc_table_data_t *temp_adc_table =
				charger->pdata->temp_adc_table;
	unsigned int temp_adc_table_size =
				charger->pdata->temp_adc_table_size;
	int i;
	int temp_adc;

	for (i = 0; i < temp_adc_table_size; i++) {
		if (data <= temp_adc_table[i].adc)
			break;
	}

	if (i == 0)
		return temp_adc_table[0].data;
	else if (i == temp_adc_table_size)
		return temp_adc_table[temp_adc_table_size - 1].data;

	temp_adc = temp_adc_table[i].data -
		   ((temp_adc_table[i].data - temp_adc_table[i - 1].data) *
		   (temp_adc_table[i].adc - data) /
		   (temp_adc_table[i].adc - temp_adc_table[i - 1].adc));

	return temp_adc;
}
static void sec_bat_get_adc_info(struct s2mu003_charger_data *charger,
				 union power_supply_propval *value)
{
	int data = -1;
	int ret = iio_read_channel_raw(&charger->adc_val[0], &data);
	if (ret < 0)
		pr_err("%s: read channel error[%d]\n", __func__, ret);
	else
		pr_debug("TEMP ADC(%d)\n", data);
	value->intval = sec_bat_adc_to_data(charger, data);
}
#endif /* CONFIG_SEC_FUELGAUGE_S2MU003 */
#if defined(CONFIG_SEC_FUELGAUGE_S2MU003) || defined(CONFIG_MUIC_NOTIFIER)
static void sec_bat_get_battery_info(struct work_struct *work)
{
	struct s2mu003_charger_data *charger =
	container_of(work, struct s2mu003_charger_data, polling_work.work);

#if defined(CONFIG_SEC_FUELGAUGE_S2MU003)
	int ret = 0;
	union power_supply_propval value;

	if (!charger->adc_check) {
		s2mu003_adc_init(charger);
		if (IS_ERR_OR_NULL(charger->adc_val))
			charger->adc_check = false;
		else
			charger->adc_check = true;
	}

	if (charger->adc_check) {
		sec_bat_get_adc_info(charger, &value);
		charger->temp_val = value.intval;
	}

	psy_do_property(charger->pdata->fuelgauge_name, get,
		POWER_SUPPLY_PROP_VOLTAGE_NOW, value);
	charger->voltage_now = value.intval;

	value.intval = SEC_BATTERY_VOLTAGE_AVERAGE;
	psy_do_property(charger->pdata->fuelgauge_name, get,
		POWER_SUPPLY_PROP_VOLTAGE_AVG, value);
	charger->voltage_avg = value.intval;

	value.intval = SEC_BATTERY_VOLTAGE_OCV;
	psy_do_property(charger->pdata->fuelgauge_name, get,
		POWER_SUPPLY_PROP_VOLTAGE_AVG, value);
	charger->voltage_ocv = value.intval;

	/* To get SOC value (NOT raw SOC), need to reset value */
	value.intval = 0;
	psy_do_property(charger->pdata->fuelgauge_name, get,
		POWER_SUPPLY_PROP_CAPACITY, value);
	charger->capacity = value.intval;

	pr_info("%s, battery_info: voltage_now: (%d), voltage_avg: (%d),"
		"voltage_ocv: (%d), capacity: (%d), temp_val: (%d)\n",
		__func__, charger->voltage_now, charger->voltage_avg,
		charger->voltage_ocv, charger->capacity, charger->temp_val);

	if (!charger->battery_valid) {
		ret = s2mu003_reg_read(charger->client,
				   S2MU003_CHG_STATUS2);
		charger->battery_valid =
		(ret & S2MU003_CHG_BATP) ? false : true;

		if (charger->rev_id == 0x02 || charger->rev_id == 0x0a ||
						charger->rev_id == 0x0b) {
			if (charger->battery_valid) {
				s2mu003_clr_bits(charger->client, 0x64, 0x1 << 3);
				s2mu003_clr_bits(charger->client, 0x75, 0x1);
			}
		}
	}

	s2mu003_test_read(charger->client);

	power_supply_changed(&charger->psy_battery);
	schedule_delayed_work(&charger->polling_work, HZ * 10);
#endif

#if defined(CONFIG_MUIC_NOTIFIER)
	if (!charger->noti_check)
		muic_notifier_register(&charger->cable_check,
				       charger_handle_notification,
					       MUIC_NOTIFY_DEV_CHARGER);
	charger->noti_check = true;
#endif
}
#endif

#ifdef CONFIG_OF
static int s2mu003_charger_parse_dt(struct device *dev,
		struct s2mu003_charger_platform_data *pdata)
{
	s2mu003_mfd_chip_t *chip = dev_get_drvdata(dev->parent);
	struct device_node *np = of_find_node_by_name(NULL, "s2mu003-charger");
	const u32 *p;
	int ret, i, len, temp;

	if (of_find_property(np, "battery,is_1MHz_switching", NULL))
		pdata->is_1MHz_switching = 1;
	pr_info("%s : is_1MHz_switching = %d\n", __func__,
			pdata->is_1MHz_switching);

	if (np == NULL)
		pr_err("%s np NULL\n", __func__);
	else
		ret = of_property_read_u32(np, "battery,chg_float_voltage",
				&pdata->chg_float_voltage);

	np = of_find_node_by_name(NULL, "battery");
	if (!np) {
		pr_err("%s np NULL\n", __func__);
	} else {
		ret = of_property_read_string(np,
			"battery,charger_name",
			(char const **)&pdata->charger_name);

#if defined(CONFIG_SEC_FUELGAUGE_S2MU003)
		ret = of_property_read_string(np,
			"battery,fuelgauge_name",
			(char const **)&pdata->fuelgauge_name);
#endif
		ret = of_property_read_u32(np, "battery,thermal_source",
			&pdata->thermal_source);
		if (ret)
			pr_info("%s : Thermal source is Empty\n", __func__);

		if (pdata->thermal_source == SEC_BATTERY_THERMAL_SOURCE_ADC) {
			p = of_get_property(np, "battery,temp_table_adc", &len);
			if (!p)
				return 1;

			len = len / sizeof(u32);

			pdata->temp_adc_table_size = len;

			pdata->temp_adc_table =
				kzalloc(sizeof(sec_bat_adc_table_data_t) *
					pdata->temp_adc_table_size, GFP_KERNEL);

			for (i = 0; i < pdata->temp_adc_table_size; i++) {
				ret = of_property_read_u32_index(np,
					"battery,temp_table_adc", i, &temp);
				pdata->temp_adc_table[i].adc = (int)temp;
				if (ret)
					pr_info("%s : Temp_adc_table(adc) is Empty\n",
						__func__);

				ret = of_property_read_u32_index(np,
					"battery,temp_table_data", i, &temp);
				pdata->temp_adc_table[i].data = (int)temp;
				if (ret)
					pr_info("%s : Temp_adc_table(data) is Empty\n",
						__func__);
			}
		}

		p = of_get_property(np, "battery,input_current_limit", &len);
		if (!p)
			return 1;

		len = len / sizeof(u32);

		pdata->charging_current_table =
		kzalloc(sizeof(sec_charging_current_t) * len, GFP_KERNEL);

		for (i = 0; i < len; i++) {
			if (chip->dev_id >= 0x0C) {
				ret = of_property_read_u32_index(np,
					"battery,input_current_limit_new", i,
					&pdata->charging_current_table[i].
							input_current_limit);
				if (ret)
					pr_info("%s: input_current_limit is Empty\n", __func__);
				ret = of_property_read_u32_index(np,
					"battery,fast_charging_current_new", i,
					&pdata->charging_current_table[i].
							fast_charging_current);
				if (ret)
					pr_info("%s: fast_charging_current is Empty\n", __func__);
			} else {
				ret = of_property_read_u32_index(np,
					"battery,input_current_limit", i,
					&pdata->charging_current_table[i].
							input_current_limit);
				if (ret)
					pr_info("%s: input_current_limit is Empty\n", __func__);
				ret = of_property_read_u32_index(np,
					"battery,fast_charging_current", i,
					&pdata->charging_current_table[i].
							fast_charging_current);
				if (ret)
					pr_info("%s: fast_charging_current is Empty\n", __func__);
			}
			ret = of_property_read_u32_index(np,
					"battery,full_check_current_1st", i,
					&pdata->charging_current_table[i].
							full_check_current_1st);
			if (ret)
				pr_info("%s: full_check_current_1st is Empty\n", __func__);
			ret = of_property_read_u32_index(np,
					"battery,full_check_current_2nd", i,
					&pdata->charging_current_table[i].
							full_check_current_2nd);
			if (ret)
				pr_info("%s: full_check_current_2nd is Empty\n", __func__);
		}
	}

	dev_info(dev, "s2mu003 charger parse dt retval = %d\n", ret);
	return ret;
}

static struct of_device_id s2mu003_charger_match_table[] = {
	{ .compatible = "samsung,s2mu003-charger",},
	{},
};
#else
static int s2mu003_charger_parse_dt(struct device *dev,
		struct s2mu003_charger_platform_data *pdata)
{
	return -ENOSYS;
}

#define s2mu003_charger_match_table NULL
#endif /* CONFIG_OF */

static int s2mu003_charger_probe(struct platform_device *pdev)
{
	s2mu003_mfd_chip_t *chip = dev_get_drvdata(pdev->dev.parent);
#ifndef CONFIG_OF
	struct s2mu003_mfd_platform_data *mfd_pdata =
				dev_get_platdata(chip->dev);
#endif
	struct s2mu003_charger_data *charger;
	int ret = 0;

	pr_info("%s:[BATT] S2MU003 Charger driver probe\n", __func__);
	charger = kzalloc(sizeof(*charger), GFP_KERNEL);
	if (!charger)
		return -ENOMEM;

	mutex_init(&charger->io_lock);

	charger->s2mu003 = chip;
	charger->client = chip->i2c_client;
	charger->dev = chip->dev;
#if defined(CONFIG_SEC_FUELGAUGE_S2MU003)
	charger->temp_val = 0;
#endif
	charger->rev_id = chip->dev_id;

#ifdef CONFIG_OF
	charger->pdata = devm_kzalloc(&pdev->dev, sizeof(*(charger->pdata)),
			GFP_KERNEL);
	if (!charger->pdata) {
		dev_err(&pdev->dev, "Failed to allocate memory\n");
		ret = -ENOMEM;
		goto err_parse_dt_nomem;
	}
	ret = s2mu003_charger_parse_dt(&pdev->dev, charger->pdata);
	if (ret < 0)
		goto err_parse_dt;
#else
	charger->pdata = mfd_pdata->charger_platform_data;
#endif

	platform_set_drvdata(pdev, charger);

	if (charger->pdata->charger_name == NULL)
		charger->pdata->charger_name = "s2mu003-charger";

	charger->psy_chg.name		= charger->pdata->charger_name;
	charger->psy_chg.type		= POWER_SUPPLY_TYPE_UNKNOWN;
	charger->psy_chg.get_property	= sec_chg_get_property;
	charger->psy_chg.set_property	= sec_chg_set_property;
	charger->psy_chg.properties	= s2mu003_charger_props;
	charger->psy_chg.num_properties	= ARRAY_SIZE(s2mu003_charger_props);

	charger->siop_level = 100;
	s2mu003_chg_init(charger);

	ret = power_supply_register(&pdev->dev, &charger->psy_chg);
	if (ret) {
		pr_err("%s: Failed to Register psy_chg\n", __func__);
		goto err_power_supply_register;
	}
#ifdef CONFIG_SEC_FUELGAUGE_S2MU003
	if (charger->pdata->fuelgauge_name == NULL)
		charger->pdata->fuelgauge_name = "s2mu003-fuelgauge";
#endif
	charger->psy_battery.name = "s2mu003-battery";
	charger->psy_battery.type = POWER_SUPPLY_TYPE_BATTERY;
	charger->psy_battery.properties =
			s2mu003_battery_props;
	charger->psy_battery.num_properties =
			ARRAY_SIZE(s2mu003_battery_props);
	charger->psy_battery.get_property =
			s2mu003_battery_get_property;
	charger->psy_battery.set_property =
			s2mu003_battery_set_property;

	ret = power_supply_register(&pdev->dev, &charger->psy_battery);
	if (ret) {
		pr_err("%s: Failed to Register psy_battery\n", __func__);
		goto err_power_supply_register;
	}
#if defined(CONFIG_SEC_FUELGAUGE_S2MU003)
	charger->capacity = 0;
#endif
	charger->psy_usb.name = "s2mu003-usb";
	charger->psy_usb.type = POWER_SUPPLY_TYPE_USB;
	charger->psy_usb.supplied_to = s2mu003_supplied_to;
	charger->psy_usb.num_supplicants =
			ARRAY_SIZE(s2mu003_supplied_to),
	charger->psy_usb.properties =
			s2mu003_power_props;
	charger->psy_usb.num_properties =
			ARRAY_SIZE(s2mu003_power_props);
	charger->psy_usb.get_property =
			s2mu003_usb_get_property;

	ret = power_supply_register(&pdev->dev, &charger->psy_usb);
	if (ret) {
		pr_err("%s: Failed to Register psy_usb\n", __func__);
		goto err_power_supply_register;
	}

	charger->psy_ac.name = "s2mu003-ac";
	charger->psy_ac.type = POWER_SUPPLY_TYPE_MAINS;
	charger->psy_ac.supplied_to = s2mu003_supplied_to;
	charger->psy_ac.num_supplicants =
			ARRAY_SIZE(s2mu003_supplied_to),
	charger->psy_ac.properties =
			s2mu003_power_props;
	charger->psy_ac.num_properties =
			ARRAY_SIZE(s2mu003_power_props);
	charger->psy_ac.get_property =
			s2mu003_ac_get_property;

	ret = power_supply_register(&pdev->dev, &charger->psy_ac);
	if (ret) {
		pr_err("%s: Failed to Register psy_usb\n", __func__);
		goto err_power_supply_register;
	}

	ret = register_irq(pdev, charger);
	if (ret < 0)
		goto err_reg_irq;

	s2mu003_test_read(charger->client);

	charger->battery_cable_type = POWER_SUPPLY_TYPE_BATTERY;
	charger->cable_type = POWER_SUPPLY_TYPE_BATTERY;

	charger->charger_wqueue = create_singlethread_workqueue("charger-wq");
	if (!charger->charger_wqueue) {
		dev_info(chip->dev, "%s: failed to create wq.\n", __func__);
		ret = -ESRCH;
		goto err_create_wq;
	}

	INIT_DELAYED_WORK(&charger->charger_work, s2mu003_ovp_work);
	charger->noti_check = false;
	charger->adc_check = false;
	msleep(500);

#if defined(CONFIG_SEC_FUELGAUGE_S2MU003) || defined(CONFIG_MUIC_NOTIFIER)
	INIT_DELAYED_WORK(&charger->polling_work,
				sec_bat_get_battery_info);
	schedule_delayed_work(&charger->polling_work, HZ * 5);
#endif

	pr_info("%s:[BATT] S2MU003 charger driver loaded OK\n", __func__);

	return 0;
err_create_wq:
err_reg_irq:
	power_supply_unregister(&charger->psy_chg);
	power_supply_unregister(&charger->psy_battery);
err_power_supply_register:
err_parse_dt:
err_parse_dt_nomem:
	mutex_destroy(&charger->io_lock);
	kfree(charger);
	return ret;
}

static int s2mu003_charger_remove(struct platform_device *pdev)
{
	struct s2mu003_charger_data *charger =
		platform_get_drvdata(pdev);

	unregister_irq(pdev, charger);
	power_supply_unregister(&charger->psy_chg);
	power_supply_unregister(&charger->psy_battery);
	mutex_destroy(&charger->io_lock);
	kfree(charger);
	return 0;
}

#if defined CONFIG_PM
static int s2mu003_charger_suspend(struct device *dev)
{
	struct s2mu003_charger_data *charger = dev_get_drvdata(dev);
	cancel_delayed_work_sync(&charger->polling_work);

	return 0;
}

static int s2mu003_charger_resume(struct device *dev)
{
	struct s2mu003_charger_data *charger = dev_get_drvdata(dev);
	schedule_delayed_work(&charger->polling_work, 0);

	return 0;
}
#else
#define s2mu003_charger_suspend NULL
#define s2mu003_charger_resume NULL
#endif

static void s2mu003_charger_shutdown(struct device *dev)
{
	pr_info("%s: S2MU003 Charger driver shutdown\n", __func__);
}

static SIMPLE_DEV_PM_OPS(s2mu003_charger_pm_ops, s2mu003_charger_suspend,
		s2mu003_charger_resume);

static struct platform_driver s2mu003_charger_driver = {
	.driver		= {
		.name	= "s2mu003-charger",
		.owner	= THIS_MODULE,
		.of_match_table = s2mu003_charger_match_table,
		.pm	= &s2mu003_charger_pm_ops,
		.shutdown = s2mu003_charger_shutdown,
	},
	.probe		= s2mu003_charger_probe,
	.remove		= s2mu003_charger_remove,
};

static int __init s2mu003_charger_init(void)
{
	int ret = 0;

	ret = platform_driver_register(&s2mu003_charger_driver);

	return ret;
}
subsys_initcall(s2mu003_charger_init);

static void __exit s2mu003_charger_exit(void)
{
	platform_driver_unregister(&s2mu003_charger_driver);
}
module_exit(s2mu003_charger_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Junhan Bae <junhan84.bae@samsung.com");
MODULE_DESCRIPTION("S2MU003 Charger driver");
