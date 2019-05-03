/* drivers/battery/sm5703_fuelgauge.c
 * SM5703 Voltage Tracking Fuelgauge Driver
 *
 * Copyright (C) 2013
 * Author: Dongik Sin <dongik.sin@samsung.com>
 * Modified by SW Jung
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */
#include <linux/battery/fuelgauge/sm5703_fuelgauge.h>
#include <linux/battery/fuelgauge/sm5703_fuelgauge_impl.h>
#if defined(CONFIG_STMP_SUPPORT_FG_ALERT)
#include <linux/input/stmpe1801_key.h>
#endif
#include <linux/uaccess.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/fs.h>
#include <linux/math64.h>
#include <linux/compiler.h>

#define FG_DET_BAT_PRESENT 1

#define MINVAL(a, b) ((a <= b) ? a : b)

static void sm5703_fuelgauge_fuelalert_init(struct sm5703_fuelgauge_data *fuelgauge,
		int soc);

static void fg_vbatocv_check(struct i2c_client *client);

enum battery_table_type {
	DISCHARGE_TABLE = 0,
	CHARGE_TABLE,
	Q_TABLE,
	TABLE_MAX,
};

static enum power_supply_property sm5703_fuelgauge_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_AVG,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_ENERGY_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TEMP_AMBIENT,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
};

static bool sm5703_fg_reg_init(struct sm5703_fuelgauge_data *fuelgauge,
                int manual_ocv_write);

static inline int sm5703_fg_read_device(struct i2c_client *client,
		int reg, int bytes, void *dest)
{
	int ret;

	if (bytes > 1)
		ret = i2c_smbus_read_i2c_block_data(client, reg, bytes, dest);
	else {
		ret = i2c_smbus_read_byte_data(client, reg);
		if (ret < 0)
			return ret;
		*(unsigned char *)dest = (unsigned char)ret;
	}
	return ret;
}

static int32_t sm5703_fg_i2c_read_word(struct i2c_client *client,
		uint8_t reg_addr)
{
	uint16_t data = 0;
	int ret;
	ret = sm5703_fg_read_device(client, reg_addr, 2, &data);
	/* dev_dbg(&client->dev, "%s: ret = %d, addr = 0x%x, data = 0x%x\n",
			__func__, ret, reg_addr, data); */

	if (ret < 0)
		return ret;
	else
		return data;

	/* not use big endian */
	/* return (int32_t)be16_to_cpu(data); */
}

static int32_t sm5703_fg_i2c_write_word(struct i2c_client *client,
		uint8_t reg_addr,uint16_t data)
{
	int ret;

	/* not use big endian */
	/* data = cpu_to_be16(data); */
	ret = i2c_smbus_write_i2c_block_data(client, reg_addr,
		2, (uint8_t *)&data);

	/* dev_dbg(&client->dev, "%s: ret = %d, addr = 0x%x, data = 0x%x\n",
			__func__, ret, reg_addr, data);
	*/

	return ret;
}

#if 0
static void sm5703_pr_ver_info(struct i2c_client *client)
{
	dev_info(&client->dev, "SM5703 Fuel-Gauge Ver %s\n", FG_DRIVER_VER);
}
#endif

static void sm5703_fg_test_read(struct i2c_client *client)
{
	int ret, ret1, ret2, ret3, ret4;

	ret = sm5703_fg_i2c_read_word(client, SM5703_REG_CNTL);
	dev_info(&client->dev, "%s: sm5703 FG 0x01 = 0x%x \n", __func__, ret);
	ret = sm5703_fg_i2c_read_word(client, 0x30);
	dev_info(&client->dev, "%s: sm5703 FG 0x30 = 0x%x \n", __func__, ret);
	ret = sm5703_fg_i2c_read_word(client, 0x31);
	dev_info(&client->dev, "%s: sm5703 FG 0x31 = 0x%x \n", __func__, ret);
	ret = sm5703_fg_i2c_read_word(client, 0x32);
	dev_info(&client->dev, "%s: sm5703 FG 0x32 = 0x%x \n", __func__, ret);
	ret = sm5703_fg_i2c_read_word(client, 0x33);
	dev_info(&client->dev, "%s: sm5703 FG 0x33 = 0x%x \n", __func__, ret);
	ret = sm5703_fg_i2c_read_word(client, 0x34);
	dev_info(&client->dev, "%s: sm5703 FG 0x34 = 0x%x \n", __func__, ret);
	ret = sm5703_fg_i2c_read_word(client, 0x35);
	dev_info(&client->dev, "%s: sm5703 FG 0x35 = 0x%x \n", __func__, ret);
	ret = sm5703_fg_i2c_read_word(client, 0x36);
	dev_info(&client->dev, "%s: sm5703 FG 0x36 = 0x%x \n", __func__, ret);
	ret = sm5703_fg_i2c_read_word(client, 0x37);
	dev_info(&client->dev, "%s: sm5703 FG 0x37 = 0x%x \n", __func__, ret);

	ret = sm5703_fg_i2c_read_word(client, 0x40);
	dev_info(&client->dev, "%s: sm5703 FG 0x40 = 0x%x \n", __func__, ret);
	ret = sm5703_fg_i2c_read_word(client, 0x41);
	dev_info(&client->dev, "%s: sm5703 FG 0x41 = 0x%x \n", __func__, ret);
	ret = sm5703_fg_i2c_read_word(client, 0x42);
	dev_info(&client->dev, "%s: sm5703 FG 0x42 = 0x%x \n", __func__, ret);
	ret = sm5703_fg_i2c_read_word(client, 0x43);
	dev_info(&client->dev, "%s: sm5703 FG 0x43 = 0x%x \n", __func__, ret);

	ret1 = sm5703_fg_i2c_read_word(client, 0xAC);
	ret2 = sm5703_fg_i2c_read_word(client, 0xAD);
	ret3 = sm5703_fg_i2c_read_word(client, 0xAE);
	ret4 = sm5703_fg_i2c_read_word(client, 0xAF);
	pr_info("0xAC=0x%04x, 0xAD=0x%04x, 0xAE=0x%04x, 0xAF=0x%04x \n", ret1, ret2, ret3, ret4);

	ret1 = sm5703_fg_i2c_read_word(client, 0xBC);
	ret2 = sm5703_fg_i2c_read_word(client, 0xBD);
	ret3 = sm5703_fg_i2c_read_word(client, 0xBE);
	ret4 = sm5703_fg_i2c_read_word(client, 0xBF);
	pr_info("0xBC=0x%04x, 0xBD=0x%04x, 0xBE=0x%04x, 0xBF=0x%04x \n", ret1, ret2, ret3, ret4);

	ret1 = sm5703_fg_i2c_read_word(client, 0xCC);
	ret2 = sm5703_fg_i2c_read_word(client, 0xCD);
	ret3 = sm5703_fg_i2c_read_word(client, 0xCE);
	ret4 = sm5703_fg_i2c_read_word(client, 0xCF);
	pr_info("0xCC=0x%04x, 0xCD=0x%04x, 0xCE=0x%04x, 0xCF=0x%04x \n", ret1, ret2, ret3, ret4);

	ret1 = sm5703_fg_i2c_read_word(client, 0x85);
	ret2 = sm5703_fg_i2c_read_word(client, 0x86);
	ret3 = sm5703_fg_i2c_read_word(client, 0x87);
	ret4 = sm5703_fg_i2c_read_word(client, 0x28);
	pr_info("0x85=0x%04x, 0x86=0x%04x, 0x87=0x%04x, 0x28=0x%04x \n", ret1, ret2, ret3, ret4);
}

static int sm5703_get_temperature(struct sm5703_fuelgauge_data *fuelgauge)
{
	int ret;

	int temp;/* = 250; 250 means 25.0oC*/
	ret = sm5703_fg_i2c_read_word(fuelgauge->i2c, SM5703_REG_TEMPERATURE);
	if (ret<0) {
		pr_err("%s: read temp reg fail", __func__);
		temp = 0;
	} else {
		/* integer bit */
		temp = ((ret & 0x7F00) >> 8) * 10;
		/* integer + fractional bit */
		temp = temp + (((ret & 0x00ff) * 10) / 256);
		if (ret & 0x8000) {
			temp *= -1;
		}
	}
	fuelgauge->info.temperature = temp;

	dev_info(&fuelgauge->i2c->dev,
		"%s: read = 0x%x, temperature = %d\n", __func__, ret, temp);

	return temp;
}

static unsigned int sm5703_get_ocv(struct sm5703_fuelgauge_data *fuelgauge)
{
	int ret;
	unsigned int ocv;/* = 3500; 3500 means 3500mV */
	ret = sm5703_fg_i2c_read_word(fuelgauge->i2c, SM5703_REG_OCV);
	if (ret<0) {
		pr_err("%s: read ocv reg fail\n", __func__);
		ocv = 4000;
	} else {
		/* integer bit */
		ocv = ((ret&0x0700)>>8) * 1000;
		/* integer + fractional bit */
		ocv = ocv + (((ret&0x00ff)*1000)/256);
	}

	fuelgauge->info.batt_ocv = ocv;

	dev_info(&fuelgauge->i2c->dev, "%s: read = 0x%x, ocv = %d\n", __func__, ret, ocv);

	return ocv;
}

static u32 sm5703_get_soc(struct sm5703_fuelgauge_data *fuelgauge)
{
	int ret;
	u32 soc;
	int ta_exist;
	int curr_cal;
	int temp_cal_fact;
	union power_supply_propval value;

	fg_vbatocv_check(fuelgauge->i2c);

	ta_exist = fuelgauge->is_charging && (fuelgauge->info.batt_current >= 0);

	dev_dbg(&fuelgauge->i2c->dev, "%s: is_charging = %d, ta_exist = %d\n", __func__, fuelgauge->is_charging, ta_exist);

	if(ta_exist)
		curr_cal = fuelgauge->info.curr_cal + (fuelgauge->info.charge_offset_cal << 8);
	else
		curr_cal = fuelgauge->info.curr_cal;
	dev_dbg(&fuelgauge->i2c->dev, "%s: curr_cal = 0x%x\n", __func__, curr_cal);

	/* abnormal case.... SW reset */
	ret = sm5703_fg_i2c_read_word(fuelgauge->i2c, SM5703_REG_FG_OP_STATUS);
	if ((ret & 0x00FF) != DISABLE_RE_INIT) {
		ret = sm5703_fg_i2c_read_word(fuelgauge->i2c, SM5703_REG_CNTL);
		pr_info( "%s: SM5703 FG abnormal case!!!! SM5703_REG_CNTL : 0x%x\n", __func__, ret);
		if (ret == 0x2008) {
			pr_info( "%s: SM5703 FG abnormal case.... SW reset\n", __func__);
			/* SW reset code */
			sm5703_fg_i2c_write_word(fuelgauge->i2c, 0x90, 0x0008);
			/* delay 200ms */
			msleep(200);
			/* init code */
			sm5703_fg_reg_init(fuelgauge, 1);
		}
	}

	sm5703_get_temperature(fuelgauge);
	sm5703_get_ocv(fuelgauge);
	temp_cal_fact = fuelgauge->info.temp_std - (fuelgauge->info.temperature / 10);
	temp_cal_fact = temp_cal_fact / fuelgauge->info.temp_offset;
	temp_cal_fact = temp_cal_fact * fuelgauge->info.temp_offset_cal;
	curr_cal = curr_cal + (temp_cal_fact << 8);

	/* compensate soc in case of low bat_temp */
	psy_do_property("battery", get, POWER_SUPPLY_PROP_TEMP, value);
	if ((value.intval / 10) < 25) {
		curr_cal = curr_cal + ((((25 - (value.intval / 10)) / 6) * 3) << 8);
	}

	dev_info(&fuelgauge->i2c->dev, "%s: fg_get_soc : temp_std = %d, temperature = %d, temp_offset = %d, temp_offset_cal = 0x%x, curr_cal = 0x%x, bat_temp = %d\n",
		__func__, fuelgauge->info.temp_std, fuelgauge->info.temperature, fuelgauge->info.temp_offset, fuelgauge->info.temp_offset_cal, curr_cal, value.intval);

	sm5703_fg_i2c_write_word(fuelgauge->i2c, SM5703_REG_CURR_CAL, curr_cal);

	ret = sm5703_fg_i2c_read_word(fuelgauge->i2c, SM5703_REG_SOC);
	if (ret < 0) {
		pr_err("%s: read soc reg fail\n", __func__);
		soc = 500;
	} else {
		/* integer bit */
		soc = ((ret & 0xff00) >> 8) * 10;
		/* integer + fractional bit */
		soc = soc + (((ret & 0x00ff) * 10) / 256);
	}

	fuelgauge->info.batt_soc = soc;

	dev_info(&fuelgauge->i2c->dev, "%s: read = 0x%x, soc = %d\n", __func__, ret, soc);

	/* temp for SM5703 FG debug */
	sm5703_fg_test_read(fuelgauge->i2c);

	return soc;
}

static unsigned int sm5703_get_vbat(struct sm5703_fuelgauge_data *fuelgauge)
{
	int ret;

	unsigned int vbat;/* = 3500; 3500 means 3500mV*/
	ret = sm5703_fg_i2c_read_word(fuelgauge->i2c, SM5703_REG_VOLTAGE);
	if (ret < 0) {
		pr_err("%s: read vbat reg fail", __func__);
		vbat = 4000;
	} else {
		/* integer bit */
		vbat = ((ret & 0x0700) >> 8) * 1000;
		/* integer + fractional bit */
		vbat = vbat + (((ret&0x00ff) * 1000) / 256);
	}

	fuelgauge->info.batt_voltage = vbat;

	if ((fuelgauge->force_dec_mode == SM5703_COLD_MODE) && vbat > 3400) {
		fuelgauge->force_dec_mode = SM5703_RECOVERY_MODE;
		wake_unlock(&fuelgauge->fuel_alert_wake_lock);
		sm5703_fuelgauge_fuelalert_init(fuelgauge,
				fuelgauge->pdata->fuel_alert_soc);
		pr_info("%s : COLD MODE DISABLE\n", __func__);
	}

	dev_dbg(&fuelgauge->i2c->dev, "%s: read = 0x%x, vbat = %d\n", __func__, ret, vbat);

	return vbat;
}

static unsigned int sm5703_get_avgvbat(struct sm5703_fuelgauge_data *fuelgauge)
{
	int ret, cnt;
	u32 vbat; /* = 3500; 3500 means 3500mV*/
	u32 old_vbat = 0;

	for (cnt = 0; cnt < 5; cnt++) {
		ret = sm5703_fg_i2c_read_word(fuelgauge->i2c, SM5703_REG_VOLTAGE);
		if (ret < 0) {
			pr_err("%s: read vbat reg fail", __func__);
			vbat = 4000;
		} else {
			/* integer bit */
			vbat = ((ret & 0x0700) >> 8) * 1000;
			/* integer + fractional bit */
			vbat = vbat + (((ret&0x00ff) * 1000) / 256);
		}

		if (cnt == 0)
			old_vbat = vbat;
		else
			old_vbat = vbat / 2 + old_vbat / 2;
	}

	fuelgauge->info.batt_avgvoltage = old_vbat;
	dev_dbg(&fuelgauge->i2c->dev, "%s: batt_avgvoltage = %d\n",
			__func__, fuelgauge->info.batt_avgvoltage);

	return old_vbat;
}

static int sm5703_get_curr(struct sm5703_fuelgauge_data *fuelgauge)
{
	int ret;
	int curr;/* = 1000; 1000 means 1000mA*/

	ret = sm5703_fg_i2c_read_word(fuelgauge->i2c, SM5703_REG_CURRENT);
	if (ret<0) {
		pr_err("%s: read curr reg fail", __func__);
		curr = 0;
	} else {
		/* integer bit */
		curr = ((ret&0x0700) >> 8) * 1000;
		/* integer + fractional bit */
		curr = curr + (((ret & 0x00ff) * 1000) / 256);
		if(ret & 0x8000) {
			curr *= -1;
		}
	}

	fuelgauge->info.batt_current = curr;
	dev_dbg(&fuelgauge->i2c->dev, "%s: read = 0x%x, curr = %d\n",
		__func__, ret, curr);

	return curr;
}

static int sm5703_get_avgcurr(struct sm5703_fuelgauge_data *fuelgauge)
{
	int ret, cnt;
	int curr;/* = 1000; 1000 means 1000mA*/
	int old_curr = 0;

	for (cnt = 0; cnt < 5; cnt++) {
		ret = sm5703_fg_i2c_read_word(fuelgauge->i2c, SM5703_REG_CURRENT);
		if (ret < 0) {
			pr_err("%s: read curr reg fail", __func__);
			curr = 0;
		} else {
			/* integer bit */
			curr = ((ret&0x0700) >> 8) * 1000;
			/* integer + fractional bit */
			curr = curr + (((ret & 0x00ff) * 1000) / 256);
			if(ret & 0x8000) {
				curr *= -1;
			}
		}

		if (cnt == 0)
			old_curr = curr;
		else
			old_curr = curr / 2 + old_curr / 2;
	}

	fuelgauge->info.batt_avgcurrent = old_curr;
	dev_dbg(&fuelgauge->i2c->dev, "%s: batt_avgcurrent = %d\n",
		__func__, fuelgauge->info.batt_avgcurrent);

	return old_curr;
}


static unsigned int sm5703_get_device_id(struct i2c_client *client)
{
	int ret;
	ret = sm5703_fg_i2c_read_word(client, SM5703_REG_DEVICE_ID);
	/* ret &= 0x00ff; */

	dev_info(&client->dev, "%s: device_id = 0x%x\n", __func__, ret);

	return ret;
}

static bool sm5703_fg_check_reg_init_need(struct i2c_client *client)
{
	int ret;

	ret = sm5703_fg_i2c_read_word(client, SM5703_REG_FG_OP_STATUS);

	if((ret & 0x00FF) == DISABLE_RE_INIT) {
		dev_dbg(&client->dev, "%s: return 0\n", __func__);
		return 0;
	} else {
		dev_dbg(&client->dev, "%s: return 1\n", __func__);
		return 1;
	}
}

static int calculate_iocv(struct i2c_client *client)
{
	int i;
	int max=0, min=0, sum=0, l_avg=0, s_avg=0, l_minmax_offset=0;
	int ret=0;

	for (i = SM5703_REG_IOCV_B_L_MIN; i <= SM5703_REG_IOCV_B_L_MAX; i++) {
		ret = sm5703_fg_i2c_read_word(client, i);
		if (i == SM5703_REG_IOCV_B_L_MIN) {
			max = ret;
			min = ret;
			sum = ret;
		} else {
			if(ret > max)
				max = ret;
			else if(ret < min)
				min = ret;
			sum = sum + ret;
		}
	}
	sum = sum - max - min;
	l_minmax_offset = max - min;
	l_avg = sum / (SM5703_REG_IOCV_B_L_MAX-SM5703_REG_IOCV_B_L_MIN-1);
	dev_info(&client->dev,
		"%s: iocv_l_max=0x%x, iocv_l_min=0x%x, iocv_l_sum=0x%x, iocv_l_avg=0x%x \n",
		__func__, max, min, sum, l_avg);

	ret = sm5703_fg_i2c_read_word(client, SM5703_REG_END_V_IDX);
	pr_info("%s: iocv_status_read = addr : 0x%x , data : 0x%x\n",
		__func__, SM5703_REG_END_V_IDX, ret);

	if ((ret & 0x0030) == 0x0030) {
		for (i = SM5703_REG_IOCV_B_S_MIN; i <= SM5703_REG_IOCV_B_S_MAX; i++) {
			ret = sm5703_fg_i2c_read_word(client, i);
			if (i == SM5703_REG_IOCV_B_S_MIN) {
				max = ret;
				min = ret;
				sum = ret;
			} else {
				if(ret > max)
					max = ret;
				else if(ret < min)
					min = ret;
				sum = sum + ret;
			}
		}
		sum = sum - max - min;
		s_avg = sum / (SM5703_REG_IOCV_B_S_MAX-SM5703_REG_IOCV_B_S_MIN-1);
		dev_info(&client->dev,
			"%s: iocv_s_max=0x%x, iocv_s_min=0x%x, iocv_s_sum=0x%x, iocv_s_avg=0x%x \n",
			__func__, max, min, sum, s_avg);
	}

	if (((abs(l_avg - s_avg) > 0x29) && (l_minmax_offset < 0xCC)) || (s_avg == 0)){
		ret = l_avg;
	} else {
		ret = s_avg;
	}

	return ret;
}

static void fg_vbatocv_check(struct i2c_client *client)
{
	int ret;
	int ta_exist;
	union power_supply_propval value;
	struct sm5703_fuelgauge_data *fuelgauge = i2c_get_clientdata(client);

	ta_exist = fuelgauge->is_charging && (fuelgauge->info.batt_current >= 0);

	/* iocv error case cover start */
	if ((abs(fuelgauge->info.batt_current) < 40) ||
		((ta_exist) &&
		(abs(fuelgauge->info.batt_current) < 100))) {
		/* 30mV over */
		if(abs(fuelgauge->info.batt_ocv-fuelgauge->info.batt_voltage) > 30) {
			fuelgauge->info.iocv_error_count ++;
		}
		if(fuelgauge->info.iocv_error_count > 5) /* prevent to overflow */
			fuelgauge->info.iocv_error_count = 6;
	} else {
		fuelgauge->info.iocv_error_count = 0;
	}

	dev_info(&client->dev, "%s: iocv_error_count (%d)\n",
		__func__, fuelgauge->info.iocv_error_count);

	if (fuelgauge->info.iocv_error_count > 5) {
		dev_info(&client->dev,
			"%s: p_v - v = (%d)\n", __func__,
			fuelgauge->info.p_batt_voltage - fuelgauge->info.batt_voltage);

		if (abs(fuelgauge->info.p_batt_voltage - fuelgauge->info.batt_voltage)>15) { /* 15mV over */
			fuelgauge->info.iocv_error_count = 0;
		} else {
			/* mode change to mix RS manual mode */
			dev_info(&client->dev, "%s: mode change to mix RS manual mode\n", __func__);
			/* RS manual value write */
			sm5703_fg_i2c_write_word(client, SM5703_REG_RS_MAN, fuelgauge->info.rs_value[0]);
			/* run update */
			sm5703_fg_i2c_write_word(client, SM5703_REG_PARAM_RUN_UPDATE, 0);
			sm5703_fg_i2c_write_word(client, SM5703_REG_PARAM_RUN_UPDATE, 1);
			/* mode change */
			ret = sm5703_fg_i2c_read_word(client, SM5703_REG_CNTL);
			ret = (ret | ENABLE_MIX_MODE) | ENABLE_RS_MAN_MODE; /* +RS_MAN_MODE */
			sm5703_fg_i2c_write_word(client, SM5703_REG_CNTL, ret);
		}
	} else {
		psy_do_property("battery", get,	POWER_SUPPLY_PROP_TEMP, value);
		if((value.intval / 10) > 15)
		{
			if((fuelgauge->info.p_batt_voltage < fuelgauge->info.n_tem_poff) &&
				(fuelgauge->info.batt_voltage < fuelgauge->info.n_tem_poff) &&
				(!ta_exist)) {
				dev_info(&client->dev,
					"%s: mode change to normal tem mix RS manual mode\n", __func__);
				/* mode change to mix RS manual mode */
				/* RS manual value write */
				if((fuelgauge->info.p_batt_voltage < (fuelgauge->info.n_tem_poff - fuelgauge->info.n_tem_poff_offset)) &&
					(fuelgauge->info.batt_voltage < (fuelgauge->info.n_tem_poff - fuelgauge->info.n_tem_poff_offset))) {
					sm5703_fg_i2c_write_word(client, SM5703_REG_RS_MAN, fuelgauge->info.rs_value[0]>>1);
				} else {
					sm5703_fg_i2c_write_word(client, SM5703_REG_RS_MAN, fuelgauge->info.rs_value[0]);
				}
				/* run update*/
				sm5703_fg_i2c_write_word(client, SM5703_REG_PARAM_RUN_UPDATE, 0);
				sm5703_fg_i2c_write_word(client, SM5703_REG_PARAM_RUN_UPDATE, 1);

				/* mode change */
				ret = sm5703_fg_i2c_read_word(client, SM5703_REG_CNTL);
				ret = (ret | ENABLE_MIX_MODE) | ENABLE_RS_MAN_MODE; // +RS_MAN_MODE
				sm5703_fg_i2c_write_word(client, SM5703_REG_CNTL, ret);
			} else {
				dev_info(&client->dev, "%s: mode change to mix RS auto mode\n", __func__);

				/* mode change to mix RS auto mode */
				ret = sm5703_fg_i2c_read_word(client, SM5703_REG_CNTL);
				ret = (ret | ENABLE_MIX_MODE) & ~ENABLE_RS_MAN_MODE; // -RS_MAN_MODE
				sm5703_fg_i2c_write_word(client, SM5703_REG_CNTL, ret);
			}
		} else {
			if((fuelgauge->info.p_batt_voltage < fuelgauge->info.l_tem_poff) &&
				(fuelgauge->info.batt_voltage < fuelgauge->info.l_tem_poff) &&
				(!ta_exist)) {
				dev_info(&client->dev,
				"%s: mode change to normal tem mix RS manual mode\n", __func__);
				/* mode change to mix RS manual mode */
				/* RS manual value write */
				if((fuelgauge->info.p_batt_voltage < (fuelgauge->info.l_tem_poff - fuelgauge->info.l_tem_poff_offset)) &&
					(fuelgauge->info.batt_voltage < (fuelgauge->info.l_tem_poff - fuelgauge->info.l_tem_poff_offset))) {
					sm5703_fg_i2c_write_word(client, SM5703_REG_RS_MAN, fuelgauge->info.rs_value[0]>>1);
				} else {
					sm5703_fg_i2c_write_word(client, SM5703_REG_RS_MAN, fuelgauge->info.rs_value[0]);
				}
				/* run update */
				sm5703_fg_i2c_write_word(client, SM5703_REG_PARAM_RUN_UPDATE, 0);
				sm5703_fg_i2c_write_word(client, SM5703_REG_PARAM_RUN_UPDATE, 1);

				/* mode change */
				ret = sm5703_fg_i2c_read_word(client, SM5703_REG_CNTL);
				ret = (ret | ENABLE_MIX_MODE) | ENABLE_RS_MAN_MODE; /* +RS_MAN_MODE */
				sm5703_fg_i2c_write_word(client, SM5703_REG_CNTL, ret);
			} else {
				dev_info(&client->dev, "%s: mode change to mix RS auto mode\n", __func__);

				/* mode change to mix RS auto mode */
				ret = sm5703_fg_i2c_read_word(client, SM5703_REG_CNTL);
				ret = (ret | ENABLE_MIX_MODE) & ~ENABLE_RS_MAN_MODE; /* -RS_MAN_MODE */
				sm5703_fg_i2c_write_word(client, SM5703_REG_CNTL, ret);
			}
		}
	}
	fuelgauge->info.p_batt_voltage = fuelgauge->info.batt_voltage;
	fuelgauge->info.p_batt_current = fuelgauge->info.batt_current;
	/* iocv error case cover end */
}

/* capacity is  0.1% unit */
static void sm5703_fg_get_scaled_capacity(
	struct sm5703_fuelgauge_data *fuelgauge,
	union power_supply_propval *val)
{
	val->intval = (val->intval < fuelgauge->pdata->capacity_min) ?
		0 : ((val->intval - fuelgauge->pdata->capacity_min) * 1000 /
		(fuelgauge->capacity_max - fuelgauge->pdata->capacity_min));

	dev_dbg(&fuelgauge->i2c->dev,
			"%s: scaled capacity (%d.%d)\n",
			__func__, val->intval/10, val->intval%10);
}

/* capacity is integer */
static void sm5703_fg_get_atomic_capacity(
	struct sm5703_fuelgauge_data *fuelgauge,
	union power_supply_propval *val)
{
	if (fuelgauge->pdata->capacity_calculation_type &
			SEC_FUELGAUGE_CAPACITY_TYPE_ATOMIC) {
		if (fuelgauge->capacity_old < val->intval)
			val->intval = fuelgauge->capacity_old + 1;
		else if (fuelgauge->capacity_old > val->intval)
			val->intval = fuelgauge->capacity_old - 1;
	}

	/* keep SOC stable in abnormal status */
	if (fuelgauge->pdata->capacity_calculation_type &
			SEC_FUELGAUGE_CAPACITY_TYPE_SKIP_ABNORMAL) {
		if (!fuelgauge->is_charging &&
				fuelgauge->capacity_old < val->intval) {
			dev_err(&fuelgauge->i2c->dev,
					"%s: capacity (old %d : new %d)\n",
					__func__, fuelgauge->capacity_old, val->intval);
			val->intval = fuelgauge->capacity_old;
		}
	}

	/* updated old capacity */
	fuelgauge->capacity_old = val->intval;
}

static int sm5703_fg_check_capacity_max(
		struct sm5703_fuelgauge_data *fuelgauge, int capacity_max)
{
	int new_capacity_max = capacity_max;

	if (new_capacity_max < (fuelgauge->pdata->capacity_max -
				fuelgauge->pdata->capacity_max_margin - 10)) {
		new_capacity_max =
			(fuelgauge->pdata->capacity_max -
			 fuelgauge->pdata->capacity_max_margin);

		dev_info(&fuelgauge->i2c->dev, "%s: set capacity max(%d --> %d)\n",
				__func__, capacity_max, new_capacity_max);
	} else if (new_capacity_max > (fuelgauge->pdata->capacity_max +
				fuelgauge->pdata->capacity_max_margin)) {
		new_capacity_max =
			(fuelgauge->pdata->capacity_max +
			 fuelgauge->pdata->capacity_max_margin);

		dev_info(&fuelgauge->i2c->dev, "%s: set capacity max(%d --> %d)\n",
				__func__, capacity_max, new_capacity_max);
	}

	return new_capacity_max;
}

static int sm5703_fg_calculate_dynamic_scale(
		struct sm5703_fuelgauge_data *fuelgauge, int capacity)
{
	union power_supply_propval raw_soc_val;
	raw_soc_val.intval = sm5703_get_soc(fuelgauge);

	if (raw_soc_val.intval <
			fuelgauge->pdata->capacity_max -
			fuelgauge->pdata->capacity_max_margin) {
		fuelgauge->capacity_max =
			fuelgauge->pdata->capacity_max -
			fuelgauge->pdata->capacity_max_margin;
		pr_info("%s: capacity_max (%d)", __func__,
				fuelgauge->capacity_max);
	} else {
		fuelgauge->capacity_max =
			(raw_soc_val.intval >
			 fuelgauge->pdata->capacity_max +
			 fuelgauge->pdata->capacity_max_margin) ?
			(fuelgauge->pdata->capacity_max +
			 fuelgauge->pdata->capacity_max_margin) :
			raw_soc_val.intval;
		pr_info("%s: raw soc (%d)", __func__,
				fuelgauge->capacity_max);
	}

	if (capacity != 100) {
		fuelgauge->capacity_max = sm5703_fg_check_capacity_max(
			fuelgauge, (fuelgauge->capacity_max * 100 / (capacity + 1)));
		fuelgauge->capacity_old = capacity;
	} else {
		fuelgauge->capacity_max =
			(fuelgauge->capacity_max * 99 / 100);

		sm5703_fg_get_scaled_capacity(fuelgauge, &raw_soc_val);
		fuelgauge->capacity_old = min((raw_soc_val.intval / 10), 100);
	}

	pr_info("%s: %d is used for capacity_max, capacity(%d)\n",
			__func__, fuelgauge->capacity_max, capacity);

	return fuelgauge->capacity_max;
}

static void sm5703_fuelgauge_fuelalert_init(struct sm5703_fuelgauge_data *fuelgauge,
		int soc)
{
	int ret;

	dev_dbg(&fuelgauge->i2c->dev, "%s: sec_hal_fg_fuelalert_init\n", __func__);

	/* remove interrupt */
	ret = sm5703_fg_i2c_read_word(fuelgauge->i2c, SM5703_REG_INTFG);

	/* check status ? need add action */
	ret = sm5703_fg_i2c_read_word(fuelgauge->i2c, SM5703_REG_STATUS);

	/* remove all mask */
	sm5703_fg_i2c_write_word(fuelgauge->i2c, SM5703_REG_INTFG_MASK, 0);

	/* set volt and soc alert threshold */
	ret = 0x0320; /* 3125mV */
	sm5703_fg_i2c_write_word(fuelgauge->i2c, SM5703_REG_V_ALARM, ret);

	ret = soc << 8;
	sm5703_fg_i2c_write_word(fuelgauge->i2c, SM5703_REG_SOC_ALARM, ret);

	/* update parameters */
	sm5703_fg_i2c_write_word(fuelgauge->i2c, SM5703_REG_PARAM_RUN_UPDATE, 0);
	sm5703_fg_i2c_write_word(fuelgauge->i2c, SM5703_REG_PARAM_RUN_UPDATE, 1);

	/* enable low soc, low voltage alert */
	fuelgauge->info.irq_ctrl = ENABLE_L_SOC_INT | ENABLE_L_VOL_INT;
	ret = sm5703_fg_i2c_read_word(fuelgauge->i2c, SM5703_REG_CNTL);
	ret = (ret & 0xFFF0) | fuelgauge->info.irq_ctrl;
	sm5703_fg_i2c_write_word(fuelgauge->i2c, SM5703_REG_CNTL, ret);

	/* reset soc alert flag */
	fuelgauge->info.soc_alert_flag = false;
	fuelgauge->is_fuel_alerted = false;

	return;
}

#ifdef ENABLE_BATT_LONG_LIFE
int get_v_max_index_by_cycle(struct sm5703_fuelgauge_data *fuelgauge)
{
	int cycle_index=0, len;

	for (len = fuelgauge->pdata->num_age_step-1; len >= 0; --len) {
		if(fuelgauge->chg_full_soc == fuelgauge->pdata->age_data[len].full_condition_soc) {
			cycle_index=len;
			break;
		}
	}
	pr_info("%s: chg_full_soc = %d, index = %d \n", __func__, fuelgauge->chg_full_soc, cycle_index);

	return cycle_index;
}
#endif

static bool sm5703_fg_reg_init(struct sm5703_fuelgauge_data *fuelgauge,
		int manual_ocv_write)
{
	int i, j, value, ret;
	uint8_t table_reg;
	int write_table[3][16];

	dev_info(&fuelgauge->i2c->dev, "%s: sm5703_fg_reg_init START!!\n", __func__);

	/* start first param_ctrl unlock */
	sm5703_fg_i2c_write_word(fuelgauge->i2c,
		SM5703_REG_PARAM_CTRL, SM5703_FG_PARAM_UNLOCK_CODE);

	/* RCE write */
	for (i = 0; i < 3; i++) {
		sm5703_fg_i2c_write_word(fuelgauge->i2c,
			SM5703_REG_RCE0+i, fuelgauge->info.rce_value[i]);
		dev_dbg(&fuelgauge->i2c->dev,
			"%s: RCE write RCE%d = 0x%x : 0x%x\n",
			__func__,
			i, SM5703_REG_RCE0+i, fuelgauge->info.rce_value[i]);
	}

	/* DTCD write */
	sm5703_fg_i2c_write_word(fuelgauge->i2c,
		SM5703_REG_DTCD, fuelgauge->info.dtcd_value);
	dev_dbg(&fuelgauge->i2c->dev,
		"%s: DTCD write DTCD = 0x%x : 0x%x\n",
		__func__,
		SM5703_REG_DTCD, fuelgauge->info.dtcd_value);

	/* RS write */
	sm5703_fg_i2c_write_word(fuelgauge->i2c,
		SM5703_REG_RS, fuelgauge->info.rs_value[0]);
	dev_dbg(&fuelgauge->i2c->dev,
		"%s: RS write RS = 0x%x : 0x%x\n",
		__func__,
		SM5703_REG_RS, fuelgauge->info.rs_value[0]);


	/* VIT_PERIOD write */
	sm5703_fg_i2c_write_word(fuelgauge->i2c,
		SM5703_REG_VIT_PERIOD, fuelgauge->info.vit_period);
	dev_dbg(&fuelgauge->i2c->dev,
		"%s: VIT_PERIOD write VIT_PERIOD = 0x%x : 0x%x\n",
		__func__,
		SM5703_REG_VIT_PERIOD, fuelgauge->info.vit_period);

	/* TABLE_LEN write & pram unlock */
	sm5703_fg_i2c_write_word(fuelgauge->i2c,
		SM5703_REG_PARAM_CTRL,
		SM5703_FG_PARAM_UNLOCK_CODE | SM5703_FG_TABLE_LEN);

#ifdef ENABLE_BATT_LONG_LIFE
	i = get_v_max_index_by_cycle(fuelgauge);
	pr_info("%s: v_max_now is change %x -> %x \n", __func__, fuelgauge->info.v_max_now, fuelgauge->info.v_max_table[i]);
	pr_info("%s: q_max_now is change %x -> %x \n", __func__, fuelgauge->info.q_max_now, fuelgauge->info.q_max_table[i]);
	fuelgauge->info.v_max_now = fuelgauge->info.v_max_table[i];
	fuelgauge->info.q_max_now = fuelgauge->info.q_max_table[i];
#endif

	for (i=TABLE_MAX-1; i >= 0; i--){
		for(j=0; j <= SM5703_FG_TABLE_LEN; j++){
#ifdef ENABLE_BATT_LONG_LIFE
			if(i == Q_TABLE){
				write_table[i][j] = fuelgauge->info.battery_table[i][j];
				if(j == SM5703_FG_TABLE_LEN){
					write_table[i][SM5703_FG_TABLE_LEN-1] = fuelgauge->info.q_max_now;
					write_table[i][SM5703_FG_TABLE_LEN] = fuelgauge->info.q_max_now + (fuelgauge->info.q_max_now/1000);
				}
			}else{
				write_table[i][j] = fuelgauge->info.battery_table[i][j];
				if(j == SM5703_FG_TABLE_LEN-1){
					write_table[i][SM5703_FG_TABLE_LEN-1] = fuelgauge->info.v_max_now;

					if(write_table[i][SM5703_FG_TABLE_LEN-1] < write_table[i][SM5703_FG_TABLE_LEN-2]){
						write_table[i][SM5703_FG_TABLE_LEN-2] = write_table[i][SM5703_FG_TABLE_LEN-1] - 0x18; // ~11.7mV
						write_table[Q_TABLE][SM5703_FG_TABLE_LEN-2] = (write_table[Q_TABLE][SM5703_FG_TABLE_LEN-1]*99)/100;
					}
				}
			}
#else
		write_table[i][j] = fuelgauge->info.battery_table[i][j];
#endif

		}
	}

	for (i=0; i < 3; i++) {
		table_reg = SM5703_REG_TABLE_START + (i<<4);
		for(j=0; j <= SM5703_FG_TABLE_LEN; j++) {
			sm5703_fg_i2c_write_word(fuelgauge->i2c, (table_reg + j), write_table[i][j]);
			msleep(10);
			if(write_table[i][j] != sm5703_fg_i2c_read_word(fuelgauge->i2c, (table_reg + j))){
				pr_info("%s: TABLE write FAIL retry[%d][%d] = 0x%x : 0x%x\n",
					__func__, i, j, (table_reg + j), write_table[i][j]);
					sm5703_fg_i2c_write_word(fuelgauge->i2c, (table_reg + j), write_table[i][j]);
			}
			pr_info("%s: TABLE write OK [%d][%d] = 0x%x : 0x%x\n",
				__func__, i, j, (table_reg + j), write_table[i][j]);
		}
	}

	/* MIX_MODE write */
	sm5703_fg_i2c_write_word(fuelgauge->i2c,
		SM5703_REG_RS_MIX_FACTOR, fuelgauge->info.rs_value[1]);
	sm5703_fg_i2c_write_word(fuelgauge->i2c,
		SM5703_REG_RS_MAX, fuelgauge->info.rs_value[2]);
	sm5703_fg_i2c_write_word(fuelgauge->i2c,
		SM5703_REG_RS_MIN, fuelgauge->info.rs_value[3]);
	sm5703_fg_i2c_write_word(fuelgauge->i2c,
		SM5703_REG_MIX_RATE, fuelgauge->info.mix_value[0]);
	sm5703_fg_i2c_write_word(fuelgauge->i2c,
		SM5703_REG_MIX_INIT_BLANK, fuelgauge->info.mix_value[1]);

	dev_dbg(&fuelgauge->i2c->dev,
		"%s: RS_MIX_FACTOR = 0x%x, RS_MAX = 0x%x, RS_MIN = 0x%x,\
		MIX_RATE = 0x%x, MIX_INIT_BLANK = 0x%x\n",		\
		__func__, fuelgauge->info.rs_value[1],
		fuelgauge->info.rs_value[2],
		fuelgauge->info.rs_value[3],
		fuelgauge->info.mix_value[0],
		fuelgauge->info.mix_value[1]);

	/* CAL write */
	sm5703_fg_i2c_write_word(fuelgauge->i2c,
		SM5703_REG_VOLT_CAL, fuelgauge->info.volt_cal);
	sm5703_fg_i2c_write_word(fuelgauge->i2c,
		SM5703_REG_CURR_CAL, fuelgauge->info.curr_cal);

	dev_dbg(&fuelgauge->i2c->dev, "%s: VOLT_CAL = 0x%x, CURR_CAL = 0x%x\n",
		__func__, fuelgauge->info.volt_cal, fuelgauge->info.curr_cal);

	/* top off soc set */
	if(sm5703_get_device_id(fuelgauge->i2c) < 3) {
		if(fuelgauge->info.topoff_soc >= 5)
			fuelgauge->info.topoff_soc = 5; /* 93% */
		else if(fuelgauge->info.topoff_soc >= 3)
			fuelgauge->info.topoff_soc = fuelgauge->info.topoff_soc - 3;
		else if(fuelgauge->info.topoff_soc >= 0)
			fuelgauge->info.topoff_soc = fuelgauge->info.topoff_soc + 5;
	}
	sm5703_fg_i2c_write_word(fuelgauge->i2c,
		SM5703_REG_TOPOFFSOC, fuelgauge->info.topoff_soc);

	/* INIT_last -  control register set */
	value = sm5703_fg_i2c_read_word(fuelgauge->i2c, SM5703_REG_CNTL);
	value &= 0xDFFF;
	value |= ENABLE_MIX_MODE | ENABLE_TEMP_MEASURE | (fuelgauge->info.enable_topoff_soc << 13);

	/* surge reset defence */
	if (manual_ocv_write) {
		value = value | ENABLE_MANUAL_OCV;
	}

	pr_info("%s: SM5703_REG_CNTL reg : 0x%x\n", __func__, value);

	ret = sm5703_fg_i2c_write_word(fuelgauge->i2c, SM5703_REG_CNTL, value);
	if (ret < 0)
		dev_dbg(&fuelgauge->i2c->dev,
		"%s: fail control register set(%d)\n", __func__, ret);

	/* Lock */
	value = SM5703_FG_PARAM_LOCK_CODE | SM5703_FG_TABLE_LEN;
	sm5703_fg_i2c_write_word(fuelgauge->i2c,
		SM5703_REG_PARAM_CTRL, value);
	dev_info(&fuelgauge->i2c->dev,
		"%s: LAST PARAM CTRL VALUE = 0x%x : 0x%x\n",
		__func__, SM5703_REG_PARAM_CTRL, value);

	/* surge reset defence */
	if (manual_ocv_write)
		value = ((fuelgauge->info.batt_ocv << 8) / 125);
	else
		value = calculate_iocv(fuelgauge->i2c);

	sm5703_fg_i2c_write_word(fuelgauge->i2c, SM5703_REG_IOCV_MAN, value);
	pr_info( "%s: IOCV_MAN_WRITE = %d : 0x%x\n",
			__func__, fuelgauge->info.batt_ocv, value);

	return 1;
}

static bool sm5703_fg_init(struct sm5703_fuelgauge_data *fuelgauge)
{
	int ret;
	int ta_exist, reg_val;
	union power_supply_propval value;

	/* SM5703 i2c read check */
	ret = sm5703_get_device_id(fuelgauge->i2c);
	if (ret < 0) {
		dev_dbg(&fuelgauge->i2c->dev,
			"%s: fail to do i2c read(%d)\n", __func__, ret);

		return false;
	}

	/* enable_topoff set */
	reg_val = sm5703_fg_i2c_read_word(fuelgauge->i2c, SM5703_REG_CNTL);
	reg_val &= 0xDFFF;
	reg_val |= (fuelgauge->info.enable_topoff_soc << 13);

	pr_info("%s: SM5703_REG_CNTL reg : 0x%x\n", __func__, reg_val);

	sm5703_fg_i2c_write_word(fuelgauge->i2c, SM5703_REG_CNTL, reg_val);

#ifdef ENABLE_BATT_LONG_LIFE
	fuelgauge->info.q_max_now = sm5703_fg_i2c_read_word(fuelgauge->i2c, 0xCE);
	pr_info("%s: q_max_now = 0x%x\n", __func__, fuelgauge->info.q_max_now);
	fuelgauge->info.q_max_now = sm5703_fg_i2c_read_word(fuelgauge->i2c, 0xCE);
	pr_info("%s: q_max_now = 0x%x\n", __func__, fuelgauge->info.q_max_now);
#endif

	value.intval = POWER_SUPPLY_HEALTH_UNKNOWN;
	psy_do_property("sm5703-charger", get,
			POWER_SUPPLY_PROP_HEALTH, value);
	dev_dbg(&fuelgauge->i2c->dev,
		"%s: get POWER_SUPPLY_PROP_HEALTH = 0x%x\n",
		__func__, value.intval);

	ta_exist = fuelgauge->is_charging && (fuelgauge->info.batt_current >= 0);
	dev_dbg(&fuelgauge->i2c->dev,
		"%s: is_charging = %d, ta_exist = %d\n",
		__func__, fuelgauge->is_charging, ta_exist);

	/* get first voltage measure to avgvoltage */
	fuelgauge->info.batt_avgvoltage = sm5703_get_avgvbat(fuelgauge);

	/* get first temperature */
	fuelgauge->info.temperature = sm5703_get_temperature(fuelgauge);

	ret = sm5703_fg_i2c_read_word(fuelgauge->i2c, 0x30);
	pr_info("%s: sm5703 FG 0x30 = 0x%x \n", __func__, ret);
	ret = sm5703_fg_i2c_read_word(fuelgauge->i2c, 0x31);
	pr_info("%s: sm5703 FG 0x31 = 0x%x \n", __func__, ret);
	ret = sm5703_fg_i2c_read_word(fuelgauge->i2c, 0x32);
	pr_info("%s: sm5703 FG 0x32 = 0x%x \n", __func__, ret);
	ret = sm5703_fg_i2c_read_word(fuelgauge->i2c, 0x33);
	pr_info("%s: sm5703 FG 0x33 = 0x%x \n", __func__, ret);
	ret = sm5703_fg_i2c_read_word(fuelgauge->i2c, 0x34);
	pr_info("%s: sm5703 FG 0x34 = 0x%x \n", __func__, ret);
	ret = sm5703_fg_i2c_read_word(fuelgauge->i2c, 0x35);
	pr_info("%s: sm5703 FG 0x35 = 0x%x \n", __func__, ret);
	ret = sm5703_fg_i2c_read_word(fuelgauge->i2c, 0x36);
	pr_info("%s: sm5703 FG 0x36 = 0x%x \n", __func__, ret);
	ret = sm5703_fg_i2c_read_word(fuelgauge->i2c, 0x37);
	pr_info("%s: sm5703 FG 0x37 = 0x%x \n", __func__, ret);

	ret = sm5703_fg_i2c_read_word(fuelgauge->i2c, 0x40);
	pr_info("%s: sm5703 FG 0x40 = 0x%x \n", __func__, ret);
	ret = sm5703_fg_i2c_read_word(fuelgauge->i2c, 0x41);
	pr_info("%s: sm5703 FG 0x41 = 0x%x \n", __func__, ret);
	ret = sm5703_fg_i2c_read_word(fuelgauge->i2c, 0x42);
	pr_info("%s: sm5703 FG 0x42 = 0x%x \n", __func__, ret);
	ret = sm5703_fg_i2c_read_word(fuelgauge->i2c, 0x43);
	pr_info("%s: sm5703 FG 0x43 = 0x%x \n", __func__, ret);

	return true;
}

static bool sm5703_fg_reset(struct sm5703_fuelgauge_data *fuelgauge)
{
	int value;

	dev_info(&fuelgauge->i2c->dev, "%s: sec_hal_fg_reset\n", __func__);

	value = sm5703_fg_i2c_read_word(fuelgauge->i2c, SM5703_REG_CNTL);
	value &= 0xF0;

	/* SW reset code */
	sm5703_fg_i2c_write_word(fuelgauge->i2c, 0x90, 0x0008);

	/* delay 400ms */
	msleep(400);

	/* Restore for SM5703_REG_CNTL[7:4] */
	value |= sm5703_fg_i2c_read_word(fuelgauge->i2c, SM5703_REG_CNTL);
	sm5703_fg_i2c_write_word(fuelgauge->i2c, SM5703_REG_CNTL, value);

	/* init code */
	if(sm5703_fg_check_reg_init_need(fuelgauge->i2c))
		sm5703_fg_reg_init(fuelgauge, 0);

	return true;
}

static void sm5703_fg_reset_capacity_by_jig_connection(struct sm5703_fuelgauge_data *fuelgauge)
{
	int ret;

#ifndef ENABLE_BATT_LONG_LIFE
	if (fuelgauge->pdata->model_type == J2LTE) {
#ifdef USE_SUSPEND_LATE
		int retry = 0;

		while(fuelgauge->is_sleep_state == true){
			pr_info("%s sleep_state retry=%d\n", __func__, retry);
			usleep_range(10 * 1000, 10 * 1000);
			if (++retry > 5)
				break;
		}
#endif
	}
#endif

	ret = sm5703_fg_i2c_read_word(fuelgauge->i2c, SM5703_REG_CNTL);
	ret &= 0xFFEF;
	sm5703_fg_i2c_write_word(fuelgauge->i2c, SM5703_REG_CNTL, ret);
}

static int sm5703_fg_get_property(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct sm5703_fuelgauge_data *fuelgauge =
		container_of(psy, struct sm5703_fuelgauge_data, psy_fg);

	switch (psp) {
		case POWER_SUPPLY_PROP_STATUS:
		case POWER_SUPPLY_PROP_CHARGE_FULL:
		case POWER_SUPPLY_PROP_ENERGY_NOW:
			return -ENODATA;
		/* Cell voltage (VCELL, mV) */
		case POWER_SUPPLY_PROP_VOLTAGE_NOW:
			val->intval = sm5703_get_vbat(fuelgauge);
			break;
		/* Additional Voltage Information (mV) */
		case POWER_SUPPLY_PROP_VOLTAGE_AVG:
			switch (val->intval) {
				case SEC_BATTERY_VOLTAGE_AVERAGE:
					val->intval = sm5703_get_avgvbat(fuelgauge);
					break;
				case SEC_BATTERY_VOLTAGE_OCV:
					val->intval = sm5703_get_ocv(fuelgauge);
					break;
			}
			break;
		/* Current (mA) */
		case POWER_SUPPLY_PROP_CURRENT_NOW:
			val->intval = sm5703_get_curr(fuelgauge);
			break;
		/* Average Current (mA) */
		case POWER_SUPPLY_PROP_CURRENT_AVG:
			val->intval = sm5703_get_avgcurr(fuelgauge);
			break;
		case POWER_SUPPLY_PROP_CAPACITY:
			if (val->intval == SEC_FUELGAUGE_CAPACITY_TYPE_RAW) {
				val->intval = sm5703_get_soc(fuelgauge) * 10;
			} else {
				val->intval = sm5703_get_soc(fuelgauge);

				if (fuelgauge->pdata->capacity_calculation_type &
						(SEC_FUELGAUGE_CAPACITY_TYPE_SCALE |
						 SEC_FUELGAUGE_CAPACITY_TYPE_DYNAMIC_SCALE))
					sm5703_fg_get_scaled_capacity(fuelgauge, val);

				/* capacity should be between 0% and 100%
				 * (0.1% degree)
				 */
				if (val->intval > 1000)
					val->intval = 1000;
				if (val->intval < 0)
					val->intval = 0;

				/* get only integer part */
				val->intval /= 10;

				if (!fuelgauge->is_charging &&
						(fuelgauge->force_dec_mode == SM5703_COLD_MODE)) {
					pr_info("%s : SW V EMPTY. Decrease SOC\n", __func__);
					val->intval = 0;
				} else if ((fuelgauge->force_dec_mode == SM5703_RECOVERY_MODE) &&
						(val->intval == fuelgauge->capacity_old)) {
					fuelgauge->force_dec_mode = SM5703_NORMAL_MODE;
				}

				/* check whether doing the wake_unlock */
				if ((val->intval > fuelgauge->pdata->fuel_alert_soc) &&
						fuelgauge->is_fuel_alerted) {
					wake_unlock(&fuelgauge->fuel_alert_wake_lock);
					sm5703_fuelgauge_fuelalert_init(fuelgauge,
							fuelgauge->pdata->fuel_alert_soc);
				}

				/* (Only for atomic capacity)
				 * In initial time, capacity_old is 0.
				 * and in resume from sleep,
				 * capacity_old is too different from actual soc.
				 * should update capacity_old
				 * by val->intval in booting or resume.
				 */
				if (fuelgauge->initial_update_of_soc &&
						fuelgauge->force_dec_mode == SM5703_NORMAL_MODE) {
					/* updated old capacity */
					fuelgauge->capacity_old = val->intval;
					fuelgauge->initial_update_of_soc = false;
					break;
				}

				if (fuelgauge->pdata->capacity_calculation_type &
						(SEC_FUELGAUGE_CAPACITY_TYPE_ATOMIC |
						 SEC_FUELGAUGE_CAPACITY_TYPE_SKIP_ABNORMAL))
					sm5703_fg_get_atomic_capacity(fuelgauge, val);
			}
			break;
		/* Battery Temperature */
		case POWER_SUPPLY_PROP_TEMP:
		/* Target Temperature */
		case POWER_SUPPLY_PROP_TEMP_AMBIENT:
			val->intval = sm5703_get_temperature(fuelgauge);
			break;
		case POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN:
			val->intval = fuelgauge->capacity_max;
			break;
		default:
			return -EINVAL;
	}

	return 0;
}

static int sm5703_fg_set_property(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	struct sm5703_fuelgauge_data *fuelgauge =
		container_of(psy, struct sm5703_fuelgauge_data, psy_fg);

	switch (psp) {
		case POWER_SUPPLY_PROP_STATUS:
#ifdef ENABLE_BATT_LONG_LIFE
			if (val->intval == POWER_SUPPLY_STATUS_FULL) {
				pr_info("%s: POWER_SUPPLY_PROP_CHARGE_FULL : q_max_now = 0x%x \n", __func__, fuelgauge->info.q_max_now);
				if(fuelgauge->info.q_max_now != 
					fuelgauge->info.q_max_table[get_v_max_index_by_cycle(fuelgauge)]){
					if (!sm5703_fg_reset(fuelgauge))
						return -EINVAL;
				}
			}
#endif
			break;
		case POWER_SUPPLY_PROP_CHARGE_FULL:
			if (fuelgauge->pdata->capacity_calculation_type &
					SEC_FUELGAUGE_CAPACITY_TYPE_DYNAMIC_SCALE) {
				sm5703_fg_calculate_dynamic_scale(fuelgauge, val->intval);
			}
			break;
		case POWER_SUPPLY_PROP_ONLINE:
			fuelgauge->cable_type = val->intval;
			if (val->intval == POWER_SUPPLY_TYPE_BATTERY) {
				fuelgauge->is_charging = false;
			} else {
				fuelgauge->is_charging = true;
				if (fuelgauge->force_dec_mode != SM5703_NORMAL_MODE) {
					fuelgauge->force_dec_mode = SM5703_NORMAL_MODE;
					fuelgauge->initial_update_of_soc = true;
					wake_unlock(&fuelgauge->fuel_alert_wake_lock);
					sm5703_fuelgauge_fuelalert_init(fuelgauge,
							fuelgauge->pdata->fuel_alert_soc);
				}

				if (fuelgauge->info.is_low_batt_alarm) {
					pr_info("%s: Reset low_batt_alarm\n",
							__func__);
					fuelgauge->info.is_low_batt_alarm = false;
				}
			}
			break;
		case POWER_SUPPLY_PROP_CAPACITY:
			if (val->intval == SEC_FUELGAUGE_CAPACITY_TYPE_RESET) {
				fuelgauge->initial_update_of_soc = true;

				if (!sm5703_fg_reset(fuelgauge))
					return -EINVAL;
				else
					break;

			}
			break;
		case POWER_SUPPLY_PROP_TEMP:
			break;
		case POWER_SUPPLY_PROP_TEMP_AMBIENT:
			break;
		case POWER_SUPPLY_PROP_ENERGY_NOW:
			sm5703_fg_reset_capacity_by_jig_connection(fuelgauge);
			break;
		case POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN:
			dev_info(&fuelgauge->i2c->dev,
					"%s: capacity_max changed, %d -> %d\n",
					__func__, fuelgauge->capacity_max, val->intval);
			fuelgauge->capacity_max = sm5703_fg_check_capacity_max(fuelgauge, val->intval);
			fuelgauge->initial_update_of_soc = true;
			break;
#if defined(CONFIG_BATTERY_AGE_FORECAST)
		case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
			pr_info("%s: full condition soc changed, %d -> %d\n",
				__func__, fuelgauge->chg_full_soc, val->intval);
			fuelgauge->chg_full_soc = val->intval;
			break;
#endif
		default:
			return -EINVAL;
	}
	return 0;
}

static void sm5703_fg_isr_work(struct work_struct *work)
{
	struct sm5703_fuelgauge_data *fuelgauge =
		container_of(work, struct sm5703_fuelgauge_data, isr_work.work);
	int fg_alert_status;

	fg_alert_status = sm5703_fg_i2c_read_word(fuelgauge->i2c, SM5703_REG_STATUS);
	dev_info(&fuelgauge->i2c->dev, "%s: fg_alert_status(0x%x)\n",
		__func__, fg_alert_status);

	fg_alert_status &= fuelgauge->info.irq_ctrl;
	if (!fg_alert_status) {
		wake_unlock(&fuelgauge->fuel_alert_wake_lock);
	}

	if (fg_alert_status & ENABLE_L_VOL_INT) {
		pr_info("%s : Battery Voltage is Very Low!! SW V EMPTY ENABLE\n", __func__);
		fuelgauge->force_dec_mode = SM5703_COLD_MODE;
	}
}

#if defined(CONFIG_STMP_SUPPORT_FG_ALERT)
static void sm5703_fg_isr(void *irq_data)
{
	struct sm5703_fuelgauge_data *fuelgauge = irq_data;
	int fg_irq;

	/* clear interrupt */
	fg_irq = sm5703_fg_i2c_read_word(fuelgauge->i2c, SM5703_REG_INTFG);
	dev_info(&fuelgauge->i2c->dev, "%s: fg_irq(0x%x)\n", __func__, fg_irq);

	if (!fuelgauge->is_fuel_alerted) {
		wake_lock(&fuelgauge->fuel_alert_wake_lock);
		fuelgauge->is_fuel_alerted = true;
		schedule_delayed_work(&fuelgauge->isr_work, 0);
	}
}
#else
static irqreturn_t sm5703_fg_irq_thread(int irq, void *irq_data)
{
	struct sm5703_fuelgauge_data *fuelgauge = irq_data;
	int fg_irq;

	/* clear interrupt */
	fg_irq = sm5703_fg_i2c_read_word(fuelgauge->i2c, SM5703_REG_INTFG);
	dev_info(&fuelgauge->i2c->dev, "%s: fg_irq(0x%x)\n",
		__func__, fg_irq);

	if (!fuelgauge->is_fuel_alerted) {
		wake_lock(&fuelgauge->fuel_alert_wake_lock);
		fuelgauge->is_fuel_alerted = true;
		schedule_delayed_work(&fuelgauge->isr_work, 0);
	}
	return IRQ_HANDLED;
}
#endif

#ifdef CONFIG_OF
#define PROPERTY_NAME_SIZE 128

#define PINFO(format, args...) \
	printk(KERN_INFO "%s() line-%d: " format, \
			__func__, __LINE__, ## args)

#define DECL_PARAM_PROP(_id, _name) {.id = _id, .name = _name,}

static int get_battery_id(struct sm5703_fuelgauge_data *fuelgauge)
{
    /* sm5703fg does not support this function */
    return 0;
}

#if defined(CONFIG_BATTERY_AGE_FORECAST)
static int temp_parse_dt(struct sm5703_fuelgauge_data *fuelgauge)
{
	struct device_node *np = of_find_node_by_name(NULL, "battery");
	int len=0, ret;
	const u32 *p;

	if (np == NULL) {
		pr_err("%s np NULL\n", __func__);
	} else {
		p = of_get_property(np, "battery,age_data", &len);
		if (p) {

			pr_info("%s --------- 1  %d \n", __func__,len);
			fuelgauge->pdata->num_age_step = len / sizeof(sec_age_data_t);
			pr_info("%s --------- 2\n", __func__);
			fuelgauge->pdata->age_data = kzalloc(len, GFP_KERNEL);
			pr_info("%s --------- 3\n", __func__);
			ret = of_property_read_u32_array(np, "battery,age_data",
					 (u32 *)fuelgauge->pdata->age_data, len/sizeof(u32));
			pr_info("%s --------- 4\n", __func__);
			if (ret) {
				pr_err("%s failed to read battery->pdata->age_data: %d\n",
						__func__, ret);
				kfree(fuelgauge->pdata->age_data);
				fuelgauge->pdata->age_data = NULL;
				fuelgauge->pdata->num_age_step = 0;
			}
			pr_info("%s num_age_step : %d\n", __func__, fuelgauge->pdata->num_age_step);
			for (len = 0; len < fuelgauge->pdata->num_age_step; ++len) {
				pr_info("[%d/%d]cycle:%d, float:%d, full_v:%d, recharge_v:%d, soc:%d\n",
					len, fuelgauge->pdata->num_age_step-1,
					fuelgauge->pdata->age_data[len].cycle,
					fuelgauge->pdata->age_data[len].float_voltage,
					fuelgauge->pdata->age_data[len].full_condition_vcell,
					fuelgauge->pdata->age_data[len].recharge_condition_vcell,
					fuelgauge->pdata->age_data[len].full_condition_soc);
			}
		} else {
			fuelgauge->pdata->num_age_step = 0;
			pr_err("%s there is not age_data\n", __func__);
		}
	}
	return 0;
}
#endif

static int sm5703_fg_parse_dt(struct sm5703_fuelgauge_data *fuelgauge)
{
	char prop_name[PROPERTY_NAME_SIZE];
	int battery_id = -1;
#ifdef ENABLE_BATT_LONG_LIFE
	int v_max_table[5];
	int q_max_table[5];
#endif
	int table[16];
	int rce_value[3];
	int rs_value[4];
	int mix_value[2];
	int topoff_soc[2];
	int set_temp_poff[4] = {3400,100,3300,80};

	int ret;
	int i, j;

	struct device_node *np = of_find_node_by_name(NULL, "sm5703-fuelgauge");

	if (np == NULL) {
		pr_err("%s np NULL\n", __func__);
	} else {
		fuelgauge->pdata->fg_irq = of_get_named_gpio(np, "fuelgauge,fuel_int", 0);
		if (fuelgauge->pdata->fg_irq < 0)
			pr_err("%s error reading fg_irq = %d\n",
				__func__, fuelgauge->pdata->fg_irq);

		ret = of_property_read_u32(np, "fuelgauge,capacity_max",
				&fuelgauge->pdata->capacity_max);
		if (ret < 0)
			pr_err("%s error reading capacity_max %d\n", __func__, ret);

		ret = of_property_read_u32(np, "fuelgauge,capacity_max_margin",
				&fuelgauge->pdata->capacity_max_margin);
		if (ret < 0)
			pr_err("%s error reading capacity_max_margin %d\n", __func__, ret);

		ret = of_property_read_u32(np, "fuelgauge,capacity_min",
				&fuelgauge->pdata->capacity_min);
		if (ret < 0)
			pr_err("%s error reading capacity_min %d\n", __func__, ret);

		ret = of_property_read_u32(np, "fuelgauge,capacity_calculation_type",
				&fuelgauge->pdata->capacity_calculation_type);
		if (ret < 0)
			pr_err("%s error reading capacity_calculation_type %d\n",
					__func__, ret);

		ret = of_property_read_u32(np, "fuelgauge,fuel_alert_soc",
				&fuelgauge->pdata->fuel_alert_soc);
		if (ret < 0)
			pr_err("%s error reading pdata->fuel_alert_soc %d\n",
					__func__, ret);

#ifndef ENABLE_BATT_LONG_LIFE
		ret = of_property_read_u32(np, "fuelgauge,model_type",
			&fuelgauge->pdata->model_type);
		if (ret < 0)
			pr_err("%s error reading pdata->model_type %d\n",
					__func__, ret);
#endif
	}

	pr_info("%s: fg_irq : %d, capacity_max : %d, capacity_max_margin : %d, capacity_min : %d\n",
		__func__, fuelgauge->pdata->fg_irq, fuelgauge->pdata->capacity_max,
		fuelgauge->pdata->capacity_max_margin, fuelgauge->pdata->capacity_min);

	/* get battery_params node */
	np = of_find_node_by_name(of_node_get(np), "battery_params");
	if (np == NULL) {
		PINFO("Cannot find child node \"battery_params\"\n");
		return -EINVAL;
	}

	/* get battery_id */
	if (of_property_read_u32(np, "battery,id", &battery_id) < 0)
		PINFO("not battery,id property\n");
	if (battery_id == -1)
		battery_id = get_battery_id(fuelgauge);
	PINFO("battery id = %d\n", battery_id);

#ifdef ENABLE_BATT_LONG_LIFE
	snprintf(prop_name, PROPERTY_NAME_SIZE, "battery%d,%s", battery_id, "v_max_table");
	ret = of_property_read_u32_array(np, prop_name, v_max_table, fuelgauge->pdata->num_age_step);

	if(ret < 0){
		PINFO("Can get prop %s (%d)\n", prop_name, ret);

		for (i = 0; i <fuelgauge->pdata->num_age_step; i++){
			fuelgauge->info.v_max_table[i] = fuelgauge->info.battery_table[DISCHARGE_TABLE][SM5703_FG_TABLE_LEN-1];
			PINFO("%s = <v_max_table[%d] 0x%x>\n", prop_name, i, fuelgauge->info.v_max_table[i]);
		}
	}else{
		for (i = 0; i < fuelgauge->pdata->num_age_step; i++){
			fuelgauge->info.v_max_table[i] = v_max_table[i];
			PINFO("%s = <v_max_table[%d] 0x%x>\n", prop_name, i, fuelgauge->info.v_max_table[i]);
		}
	}

	snprintf(prop_name, PROPERTY_NAME_SIZE, "battery%d,%s", battery_id, "q_max_table");
	ret = of_property_read_u32_array(np, prop_name, q_max_table,fuelgauge->pdata->num_age_step);

	if(ret < 0){
		PINFO("Can get prop %s (%d)\n", prop_name, ret);

		for (i = 0; i < fuelgauge->pdata->num_age_step; i++){
			fuelgauge->info.q_max_table[i] = 100;
			PINFO("%s = <q_max_table[%d] %d>\n", prop_name, i, fuelgauge->info.q_max_table[i]);
		}
	}else{
		for (i = 0; i < fuelgauge->pdata->num_age_step; i++){
			fuelgauge->info.q_max_table[i] = q_max_table[i];
			PINFO("%s = <q_max_table[%d] %d>\n", prop_name, i, fuelgauge->info.q_max_table[i]);
		}
	}
	fuelgauge->chg_full_soc = fuelgauge->pdata->age_data[0].full_condition_soc;
	fuelgauge->info.v_max_now = fuelgauge->info.v_max_table[0];
	fuelgauge->info.q_max_now = fuelgauge->info.q_max_table[0];
	PINFO("%s = <v_max_now = 0x%x>, <q_max_now = 0x%x>, <chg_full_soc = %d>\n", prop_name, fuelgauge->info.v_max_now, fuelgauge->info.q_max_now, fuelgauge->chg_full_soc);
#endif

	/* get battery_table */
	for (i = DISCHARGE_TABLE; i < TABLE_MAX; i++) {
		snprintf(prop_name, PROPERTY_NAME_SIZE,
				"battery%d,%s%d", battery_id, "battery_table", i);

		ret = of_property_read_u32_array(np, prop_name, table, 16);
		if (ret < 0) {
			PINFO("Can get prop %s (%d)\n", prop_name, ret);
		}
		for (j = 0; j <= SM5703_FG_TABLE_LEN; j++) {
			fuelgauge->info.battery_table[i][j] = table[j];
			PINFO("%s = <table[%d][%d] 0x%x>\n", prop_name, i, j, table[j]);
		}
	}

	/* get rce */
	for (i = 0; i < 3; i++) {
		snprintf(prop_name, PROPERTY_NAME_SIZE, "battery%d,%s", battery_id, "rce_value");
		ret = of_property_read_u32_array(np, prop_name, rce_value, 3);
		if (ret < 0) {
			PINFO("Can get prop %s (%d)\n", prop_name, ret);
		}
		fuelgauge->info.rce_value[i] = rce_value[i];
	}
	PINFO("%s = <0x%x 0x%x 0x%x>\n", prop_name, rce_value[0], rce_value[1], rce_value[2]);

	/* get dtcd_value */
	snprintf(prop_name, PROPERTY_NAME_SIZE, "battery%d,%s", battery_id, "dtcd_value");
	ret = of_property_read_u32_array(np, prop_name, &fuelgauge->info.dtcd_value, 1);
	if (ret < 0)
		PINFO("Can get prop %s (%d)\n", prop_name, ret);
	PINFO("%s = <0x%x>\n",prop_name, fuelgauge->info.dtcd_value);

	/* get rs_value */
	for (i = 0; i < 4; i++) {
		snprintf(prop_name, PROPERTY_NAME_SIZE, "battery%d,%s", battery_id, "rs_value");
		ret = of_property_read_u32_array(np, prop_name, rs_value, 4);
		if (ret < 0) {
			PINFO("Can get prop %s (%d)\n", prop_name, ret);
		}
		fuelgauge->info.rs_value[i] = rs_value[i];
	}
	PINFO("%s = <0x%x 0x%x 0x%x 0x%x>\n", prop_name, rs_value[0], rs_value[1], rs_value[2], rs_value[3]);

	/* get vit_period */
	snprintf(prop_name, PROPERTY_NAME_SIZE, "battery%d,%s", battery_id, "vit_period");
	ret = of_property_read_u32_array(np, prop_name, &fuelgauge->info.vit_period, 1);
	if (ret < 0)
		PINFO("Can get prop %s (%d)\n", prop_name, ret);
	PINFO("%s = <0x%x>\n",prop_name, fuelgauge->info.vit_period);

	/* get mix_value */
	for (i = 0; i < 2; i++) {
		snprintf(prop_name, PROPERTY_NAME_SIZE, "battery%d,%s", battery_id, "mix_value");
		ret = of_property_read_u32_array(np, prop_name, mix_value, 2);
		if (ret < 0) {
			PINFO("Can get prop %s (%d)\n", prop_name, ret);
		}
		fuelgauge->info.mix_value[i] = mix_value[i];
	}
	PINFO("%s = <0x%x 0x%x>\n", prop_name, mix_value[0], mix_value[1]);

	/* battery_type */
	snprintf(prop_name, PROPERTY_NAME_SIZE, "battery%d,%s", battery_id, "battery_type");
	ret = of_property_read_u32_array(np, prop_name, &fuelgauge->info.battery_type, 1);
	if (ret < 0)
		PINFO("Can get prop %s (%d)\n", prop_name, ret);
	PINFO("%s = <%d>\n", prop_name, fuelgauge->info.battery_type);

	/* TOP OFF SOC */
	snprintf(prop_name, PROPERTY_NAME_SIZE, "battery%d,%s", battery_id, "topoff_soc");
	ret = of_property_read_u32_array(np, prop_name, topoff_soc, 2);
	if (ret < 0)
		PINFO("Can get prop %s (%d)\n", prop_name, ret);
	fuelgauge->info.enable_topoff_soc = topoff_soc[0];
	fuelgauge->info.topoff_soc = topoff_soc[1];
	PINFO("%s = <0x%x 0x%x>\n", prop_name, fuelgauge->info.enable_topoff_soc, fuelgauge->info.topoff_soc);

	/* VOL & CURR CAL */
	snprintf(prop_name, PROPERTY_NAME_SIZE, "battery%d,%s", battery_id, "volt_cal");
	ret = of_property_read_u32_array(np, prop_name, &fuelgauge->info.volt_cal, 1);
	if (ret < 0)
		PINFO("Can get prop %s (%d)\n", prop_name, ret);
	PINFO("%s = <0x%x>\n", prop_name, fuelgauge->info.volt_cal);

	snprintf(prop_name, PROPERTY_NAME_SIZE, "battery%d,%s", battery_id, "curr_cal");
	ret = of_property_read_u32_array(np, prop_name, &fuelgauge->info.curr_cal, 1);
	if (ret < 0)
		PINFO("Can get prop %s (%d)\n", prop_name, ret);
	PINFO("%s = <0x%x>\n", prop_name, fuelgauge->info.curr_cal);

	/* temp_std */
	snprintf(prop_name, PROPERTY_NAME_SIZE, "battery%d,%s", battery_id, "temp_std");
	ret = of_property_read_u32_array(np, prop_name, &fuelgauge->info.temp_std, 1);
	if (ret < 0)
		PINFO("Can get prop %s (%d)\n", prop_name, ret);
	PINFO("%s = <%d>\n", prop_name, fuelgauge->info.temp_std);

	/* temp_offset */
	snprintf(prop_name, PROPERTY_NAME_SIZE, "battery%d,%s", battery_id, "temp_offset");
	ret = of_property_read_u32_array(np, prop_name, &fuelgauge->info.temp_offset, 1);
	if (ret < 0)
		PINFO("Can get prop %s (%d)\n", prop_name, ret);
	PINFO("%s = <%d>\n", prop_name, fuelgauge->info.temp_offset);

	/* temp_offset_cal */
	snprintf(prop_name, PROPERTY_NAME_SIZE, "battery%d,%s", battery_id, "temp_offset_cal");
	ret = of_property_read_u32_array(np, prop_name, &fuelgauge->info.temp_offset_cal, 1);
	if (ret < 0)
		PINFO("Can get prop %s (%d)\n", prop_name, ret);
	PINFO("%s = <0x%x>\n", prop_name, fuelgauge->info.temp_offset_cal);

	/* charge_offset_cal */
	snprintf(prop_name, PROPERTY_NAME_SIZE, "battery%d,%s", battery_id, "charge_offset_cal");
	ret = of_property_read_u32_array(np, prop_name, &fuelgauge->info.charge_offset_cal, 1);
	if (ret < 0)
		PINFO("Can get prop %s (%d)\n", prop_name, ret);
	PINFO("%s = <0x%x>\n", prop_name, fuelgauge->info.charge_offset_cal);

	/* tem poff level */
	snprintf(prop_name, PROPERTY_NAME_SIZE, "battery%d,%s", battery_id, "tem_poff");
	ret = of_property_read_u32_array(np, prop_name, set_temp_poff, 4);
	if (ret < 0)
		PINFO("Can get prop %s (%d)\n", prop_name, ret);
	fuelgauge->info.n_tem_poff = set_temp_poff[0];
	fuelgauge->info.n_tem_poff_offset = set_temp_poff[1];
	fuelgauge->info.l_tem_poff = set_temp_poff[2];
	fuelgauge->info.l_tem_poff_offset = set_temp_poff[3];

	PINFO("%s = <%d, %d, %d, %d>\n", prop_name,
		fuelgauge->info.n_tem_poff, fuelgauge->info.n_tem_poff_offset,
		fuelgauge->info.l_tem_poff, fuelgauge->info.l_tem_poff_offset);

	np = of_find_node_by_name(NULL, "battery");
	if (!np) {
		pr_info("%s : np NULL\n", __func__);
		return -ENODATA;
	}

	ret = of_property_read_string(np, "battery,fuelgauge_name",
		(char const **)&fuelgauge->pdata->fuelgauge_name);
	if (ret)
		pr_info("%s: fuelgauge name is Empty.\n", __func__);

	return 0;
}

static struct of_device_id sm5703_fuelgauge_match_table[] = {
	{ .compatible = "samsung,sm5703-fuelgauge",},
	{},
};
#else
static int sm5703_fg_parse_dt(struct sm5703_fuelgauge_data *fuelgauge)
{
	return -ENOSYS;
}

#define sm5703_fuelgauge_match_table NULL
#endif /* CONFIG_OF */

static int sm5703_fuelgauge_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct sm5703_fuelgauge_data *fuelgauge;
#ifdef ENABLE_BATT_LONG_LIFE
	sec_battery_platform_data_t *pdata = NULL;
#endif
	union power_supply_propval raw_soc_val;
	int ret = 0;

	pr_info("%s: SM5703 Fuelgauge Driver Loading\n", __func__);

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE))
		return -EIO;

	fuelgauge = kzalloc(sizeof(*fuelgauge), GFP_KERNEL);
	if (!fuelgauge)
		return -ENOMEM;

	mutex_init(&fuelgauge->fg_lock);

	fuelgauge->i2c = client;

#ifdef ENABLE_BATT_LONG_LIFE
	if (client->dev.of_node) {
		pdata = devm_kzalloc(&client->dev, sizeof(sec_battery_platform_data_t),GFP_KERNEL);
		if (!pdata) {
			dev_err(&client->dev, "Failed to allocate memory\n");
			ret = -ENOMEM;
			goto err_parse_dt_nomem;
		}
		fuelgauge->pdata = pdata;
#else
	if (client->dev.of_node) {
		fuelgauge->pdata = devm_kzalloc(&client->dev, sizeof(*(fuelgauge->pdata)),
				GFP_KERNEL);
		if (!fuelgauge->pdata) {
			dev_err(&client->dev, "Failed to allocate memory\n");
			ret = -ENOMEM;
			goto err_parse_dt_nomem;
		}
#endif

#if defined(CONFIG_BATTERY_AGE_FORECAST)
		temp_parse_dt(fuelgauge);
#endif
		ret = sm5703_fg_parse_dt(fuelgauge);
		if (ret < 0)
			goto err_parse_dt;
	} else {
		fuelgauge->pdata = client->dev.platform_data;
	}

	i2c_set_clientdata(client, fuelgauge);

	if (fuelgauge->pdata->fuelgauge_name == NULL)
		fuelgauge->pdata->fuelgauge_name = "sm5703-fuelgauge";

	fuelgauge->psy_fg.name          = fuelgauge->pdata->fuelgauge_name;
	fuelgauge->psy_fg.type          = POWER_SUPPLY_TYPE_UNKNOWN;
	fuelgauge->psy_fg.get_property  = sm5703_fg_get_property;
	fuelgauge->psy_fg.set_property  = sm5703_fg_set_property;
	fuelgauge->psy_fg.properties    = sm5703_fuelgauge_props;
	fuelgauge->psy_fg.num_properties =
		ARRAY_SIZE(sm5703_fuelgauge_props);

	fuelgauge->capacity_max = fuelgauge->pdata->capacity_max;
	raw_soc_val.intval = sm5703_get_soc(fuelgauge);

	if (raw_soc_val.intval > fuelgauge->capacity_max)
		sm5703_fg_calculate_dynamic_scale(fuelgauge, 100);

	ret = sm5703_fg_init(fuelgauge);
	if (ret < 0) {
		pr_err("%s: Failed to Initialize Fuelgauge\n", __func__); 
		/* goto err_data_free; */
	}

	ret = power_supply_register(&client->dev, &fuelgauge->psy_fg);
	if (ret) {
		pr_err("%s: Failed to Register psy_fg\n", __func__);
		goto err_data_free;
	}

	if (fuelgauge->pdata->fuel_alert_soc >= 0) {
		sm5703_fuelgauge_fuelalert_init(fuelgauge,
					fuelgauge->pdata->fuel_alert_soc);
		wake_lock_init(&fuelgauge->fuel_alert_wake_lock,
					WAKE_LOCK_SUSPEND, "fuel_alerted");

#if defined(CONFIG_STMP_SUPPORT_FG_ALERT)
		INIT_DELAYED_WORK(
					&fuelgauge->isr_work, sm5703_fg_isr_work);
		ret = stmpe_request_irq(6, sm5703_fg_isr,
							STMPE_TRIGGER_FALLING,
							"SM5703-Fuelgauge", fuelgauge);
		if(ret) {
			pr_err("%s: Failed register to STMPE (%d)\n", __func__, ret);
			goto err_supply_unreg;
		}
#else
		if (fuelgauge->pdata->fg_irq > 0) {
			INIT_DELAYED_WORK(
					&fuelgauge->isr_work, sm5703_fg_isr_work);

			fuelgauge->fg_irq = gpio_to_irq(fuelgauge->pdata->fg_irq);
			dev_info(&client->dev,
					"%s: fg_irq = %d\n", __func__, fuelgauge->fg_irq);
			if (fuelgauge->fg_irq > 0) {
				ret = request_threaded_irq(fuelgauge->fg_irq,
						NULL, sm5703_fg_irq_thread,
						IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING
						| IRQF_ONESHOT,
						"fuelgauge-irq", fuelgauge);
				if (ret) {
					dev_err(&client->dev,
							"%s: Failed to Reqeust IRQ\n", __func__);
					goto err_supply_unreg;
				}

				ret = enable_irq_wake(fuelgauge->fg_irq);
				if (ret < 0)
					dev_err(&client->dev,
							"%s: Failed to Enable Wakeup Source(%d)\n",
							__func__, ret);
			} else {
				dev_err(&client->dev, "%s: Failed gpio_to_irq(%d)\n",
						__func__, fuelgauge->fg_irq);
				goto err_supply_unreg;
			}
		}
#endif
	}

	fuelgauge->initial_update_of_soc = true;
	fuelgauge->force_dec_mode = SM5703_NORMAL_MODE;

#ifdef USE_SUSPEND_LATE
	fuelgauge->is_sleep_state = false;
#endif

	pr_info("%s: SM5703 Fuelgauge Driver Loaded\n", __func__);
	return 0;

err_supply_unreg:
	power_supply_unregister(&fuelgauge->psy_fg);
err_data_free:
	if (client->dev.of_node)
		kfree(fuelgauge->pdata);

err_parse_dt:
err_parse_dt_nomem:
	mutex_destroy(&fuelgauge->fg_lock);
	kfree(fuelgauge);

	return ret;
}

static const struct i2c_device_id sm5703_fuelgauge_id[] = {
	{"sm5703-fuelgauge", 0},
	{}
};

static void sm5703_fuelgauge_shutdown(struct i2c_client *client)
{
}

static int sm5703_fuelgauge_remove(struct i2c_client *client)
{
	struct sm5703_fuelgauge_data *fuelgauge = i2c_get_clientdata(client);

	if (fuelgauge->pdata->fuel_alert_soc >= 0)
		wake_lock_destroy(&fuelgauge->fuel_alert_wake_lock);

	return 0;
}

#if defined CONFIG_PM
static int sm5703_fuelgauge_suspend(struct device *dev)
{
	return 0;
}

static int sm5703_fuelgauge_resume(struct device *dev)
{
	struct sm5703_fuelgauge_data *fuelgauge = dev_get_drvdata(dev);

	fuelgauge->initial_update_of_soc = true;

	return 0;
}

#ifdef USE_SUSPEND_LATE
static int sm5703_suspend_late(struct device *dev)
{
	struct sm5703_fuelgauge_data *fuelgauge = dev_get_drvdata(dev);

	fuelgauge->is_sleep_state = true;

	return 0;
}

static int sm5703_resume_noirq(struct device *dev)
{
	struct sm5703_fuelgauge_data *fuelgauge = dev_get_drvdata(dev);

	fuelgauge->is_sleep_state = false;

	return 0;
}
#endif

#else
#define sm5703_fuelgauge_suspend NULL
#define sm5703_fuelgauge_resume NULL
#endif

const struct dev_pm_ops sm5703_fuelgauge_pm_ops = {
	.suspend = sm5703_fuelgauge_suspend,
	.resume = sm5703_fuelgauge_resume,
#ifdef USE_SUSPEND_LATE
	.suspend_late = sm5703_suspend_late,
	.resume_noirq = sm5703_resume_noirq,
#endif
};

static struct i2c_driver sm5703_fuelgauge_driver = {
	.driver = {
		.name = "sm5703-fuelgauge",
		.owner = THIS_MODULE,
		.pm = &sm5703_fuelgauge_pm_ops,
		.of_match_table = sm5703_fuelgauge_match_table,
	},
	.probe  = sm5703_fuelgauge_probe,
	.remove = sm5703_fuelgauge_remove,
	.shutdown   = sm5703_fuelgauge_shutdown,
	.id_table   = sm5703_fuelgauge_id,
};

static int __init sm5703_fuelgauge_init(void)
{
	pr_info("%s: SM5703 Fuelgauge Init\n", __func__);
	return i2c_add_driver(&sm5703_fuelgauge_driver);
}

static void __exit sm5703_fuelgauge_exit(void)
{
	i2c_del_driver(&sm5703_fuelgauge_driver);
}
module_init(sm5703_fuelgauge_init);
module_exit(sm5703_fuelgauge_exit);

MODULE_DESCRIPTION("Samsung SM5703 Fuel Gauge Driver");
MODULE_AUTHOR("Samsung Electronics");
MODULE_LICENSE("GPL");
