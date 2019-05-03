/*
 * Copyright (c) 2010 SAMSUNG
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
 */
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/i2c.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/gpio.h>
#include <linux/wakelock.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/uaccess.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>

#include <linux/sensor/sensors_core.h>
#include "tmd3782.h"

/* Note about power vs enable/disable:
 *  The chip has two functions, proximity and ambient light sensing.
 *  There is no separate power enablement to the two functions (unlike
 *  the Capella CM3602/3623).
 *  This module implements two drivers: /dev/proximity and /dev/light.
 *  When either driver is enabled (via sysfs attributes), we give power
 *  to the chip.  When both are disabled, we remove power from the chip.
 *  In suspend, we remove power if light is disabled but not if proximity is
 *  enabled (proximity is allowed to wakeup from suspend).
 *
 *  There are no ioctls for either driver interfaces.  Output is via
 *  input device framework and control via sysfs attributes.
 */

#define MODULE_NAME_PROX   "proximity_sensor"
#define MODULE_NAME_LIGHT  "light_sensor"

#define VENDOR_NAME     "TAOS"
#define CHIP_NAME       "TMD3782"
#define CHIP_ID         0x69

#define RETRY_REBOOT    3

/*lightsnesor log time 6SEC 200mec X 30*/
#define LIGHT_LOG_TIME  30

enum {
	LIGHT_ENABLED = BIT(0),
	PROXIMITY_ENABLED = BIT(1),
};

enum {
	STATE_CLOSE = 0,
	STATE_FAR = 1,
};

enum {
	OFF = 0,
	ON = 1,
};

#define Atime_ms                504 /* 50.4 ms */
#define DGF                     642
#define R_Coef1                 (330)
#define G_Coef1                 (1000)
#define B_Coef1                 (150)
#define CT_Coef1                (3210)
#define CT_Offset1              (1788)
#define IR_R_Coef1              (-1)
#define IR_G_Coef1              (109)
#define IR_B_Coef1              (-29)
#define IR_C_Coef1              (57)
#define IR_Coef1                (38)
#define INTEGRATION_CYCLE       240

#define CAL_SKIP_ADC    325
#define CAL_FAIL_ADC    440

#define ADC_BUFFER_NUM  6
#define PROX_AVG_COUNT  40
#define MAX_LUX         150000
#define TAOS_PROX_MAX   1023
#define TAOS_PROX_MIN   0

#define OFFSET_ARRAY_LENGTH     10
#define OFFSET_FILE_PATH        "/efs/FactoryApp/prox_cal"

#ifdef CONFIG_PROX_WINDOW_TYPE
#define WINDOW_TYPE_FILE_PATH   "/sys/class/sec/sec_touch_ic/window_type"
#endif

/* driver data */
struct taos_data {
	struct i2c_client *i2c_client;
	struct taos_platform_data *pdata;
	struct input_dev *proximity_input_dev;
	struct input_dev *light_input_dev;
	struct device *light_dev;
	struct device *proximity_dev;
	struct work_struct work_light;
	struct work_struct work_prox;
	struct work_struct work_prox_avg;
	struct mutex prox_mutex;
	struct mutex power_lock;
	struct wake_lock prx_wake_lock;
	struct hrtimer timer;
	struct hrtimer prox_avg_timer;
	struct workqueue_struct *wq;
	struct workqueue_struct *wq_avg;
	struct regulator *vled;
	ktime_t light_poll_delay;
	ktime_t prox_polling_time;
	u8 power_state;
	int irq;
	bool adc_buf_initialized;
	int adc_value_buf[ADC_BUFFER_NUM];
	int adc_index_count;
	int avg[3];
	int prox_avg_enable;
	s32 clrdata;
	s32 reddata;
	s32 grndata;
	s32 bludata;
	s32 irdata;
	int lux;
	/* Auto Calibration */
	u16 offset_value;
	int cal_result;
	int threshold_high;
	int threshold_low;
	int proximity_value;
	bool set_manual_thd;
	int count_log_time;
#ifdef CONFIG_SENSORS_BHA250_DEFENCE_SW_RESET
	u8 vdd_reset;
#endif
#ifdef CONFIG_PROX_WINDOW_TYPE
	char windowtype[2];
#endif
};

static int proximity_open_offset(struct taos_data *data);
#ifdef CONFIG_PROX_WINDOW_TYPE
static int proximity_open_window_type(struct taos_data *data);
#endif

static int proximity_vled_onoff(struct taos_data *data, int onoff)
{
	int err;

	SENSOR_INFO("%s, ldo:%d\n",
		(onoff) ? "on" : "off", data->pdata->vled_ldo);

	/* ldo control */
	if (data->pdata->vled_ldo) {
		gpio_set_value(data->pdata->vled_ldo, onoff);
		if (onoff)
			msleep(20);
		return 0;
	}

	/* regulator(PMIC) control */
	if (!data->vled) {
		SENSOR_INFO("VLED get regulator\n");
		data->vled = regulator_get(&data->i2c_client->dev,
			"taos,vled");
		if (IS_ERR(data->vled)) {
			SENSOR_ERR("regulator_get fail\n");
			data->vled = NULL;
			return -ENODEV;
		}
	}

	if (onoff) {
		if (regulator_is_enabled(data->vled)) {
			SENSOR_INFO("Regulator already enabled\n");
			return 0;
		}

		err = regulator_enable(data->vled);
		if (err)
			SENSOR_ERR("Failed to enable vled.\n");
		usleep_range(10000, 11000);
	} else {
		err = regulator_disable(data->vled);
		if (err)
			SENSOR_ERR("Failed to disable vled.\n");
	}

	return 0;
}

static int opt_i2c_read(struct taos_data *taos, u8 reg , u8 *val)
{
	int ret;

	i2c_smbus_write_byte(taos->i2c_client, (CMD_REG | reg));
	ret = i2c_smbus_read_byte(taos->i2c_client);
	*val = ret;

	return ret;
}
static int opt_i2c_write_command(struct taos_data *taos, u8 val)
{
	int ret;

	ret = i2c_smbus_write_byte(taos->i2c_client, val);
	SENSOR_INFO("[TAOS Command] val=[0x%x] - ret=[0x%x]\n", val, ret);

	return ret;
}

static int opt_i2c_write(struct taos_data *taos, u8 reg, u8 *val)
{
	int ret;
	int retry = 3;

	do {
		ret = i2c_smbus_write_byte_data(taos->i2c_client,
			(CMD_REG | reg), *val);

		if (ret < 0) {
			SENSOR_ERR("%d\n", ret);
			usleep_range(1000, 1100);
		} else
			return ret;
	} while (retry--);

	SENSOR_ERR("i2c write failed, ret = %d\n", ret);
	return ret;
}

static int proximity_get_adc(struct taos_data *taos)
{
	int adc = 0;
#ifdef CONFIG_SENSORS_BHA250_DEFENCE_SW_RESET
	if (taos->vdd_reset) {
		SENSOR_INFO("vdd: turned off.\n");
		return -EPERM;
	}
#endif

	adc = i2c_smbus_read_word_data(taos->i2c_client,
		CMD_REG | PRX_LO);
	if (adc < taos->pdata->prox_rawdata_trim)
		return TAOS_PROX_MIN;
	if (adc > TAOS_PROX_MAX)
		adc = TAOS_PROX_MAX;

	return adc - taos->pdata->prox_rawdata_trim;
}

static int taos_proximity_get_threshold(struct taos_data *taos, u8 buf)
{
	int threshold;

	threshold = i2c_smbus_read_word_data(taos->i2c_client,
		(CMD_REG | buf));
	SENSOR_INFO("threshold = %d,trim = %d,buf=%x\n",
		threshold, taos->pdata->prox_rawdata_trim, buf);

	if ((threshold == 0xFFFF) || (threshold == 0))
		return (int)threshold;

	return ((int)threshold - taos->pdata->prox_rawdata_trim);
}

static void taos_thresh_set(struct taos_data *taos)
{
	int ret = 0;
	u16 trim = (u16)taos->pdata->prox_rawdata_trim;
	/* Setting for proximity interrupt */
	u8 i, prox_int_thresh[4] = {0, };

	if (taos->proximity_value == STATE_CLOSE) {
		prox_int_thresh[0] = ((u16)taos->threshold_low+trim) & 0xFF;
		prox_int_thresh[1] = ((taos->threshold_low+trim) >> 8) & 0xFF;
		prox_int_thresh[2] = (0xFFFF) & 0xFF;
		prox_int_thresh[3] = (0xFFFF >> 8) & 0xFF;
	} else {
		prox_int_thresh[0] = (0x0000) & 0xFF;
		prox_int_thresh[1] = (0x0000 >> 8) & 0xFF;
		prox_int_thresh[2] = ((u16)taos->threshold_high+trim) & 0xFF;
		prox_int_thresh[3] = (((u16)taos->threshold_high+trim) >> 8) & 0xFF;
	}

	for (i = 0; i < 4; i++) {
		ret = opt_i2c_write(taos, (CMD_REG | (PRX_MINTHRESHLO + i)),
			&prox_int_thresh[i]);
		if (ret < 0)
			SENSOR_ERR("opt_i2c_write failed, err = %d\n", ret);
	}
}

static int taos_chip_on(struct taos_data *taos, bool prox_en)
{
	int ret = 0;
	u8 temp_val;
	u8 reg_cntrl;

	proximity_vled_onoff(taos, ON);
	temp_val = CNTL_PWRON;
	ret = opt_i2c_write(taos, (CMD_REG | CNTRL), &temp_val);
	if (ret < 0)
		SENSOR_ERR("opt_i2c_write to clr ctrl reg failed\n");

	/* A minimum interval of 2.4ms must pass after PON is enabled */
	usleep_range(3000, 3100);
	temp_val = taos->pdata->als_time;
	ret = opt_i2c_write(taos, (CMD_REG | ALS_TIME), &temp_val);
	if (ret < 0)
		SENSOR_ERR("opt_i2c_write to als time reg failed\n");

	temp_val = 0xff;
	ret = opt_i2c_write(taos, (CMD_REG | WAIT_TIME), &temp_val);
	if (ret < 0)
		SENSOR_ERR("opt_i2c_write to wait time reg failed\n");

	temp_val = taos->pdata->intr_filter;
	ret = opt_i2c_write(taos, (CMD_REG | INTERRUPT), &temp_val);
	if (ret < 0)
		SENSOR_ERR("opt_i2c_write to interrupt reg failed\n");

	temp_val = 0x0;
	ret = opt_i2c_write(taos, (CMD_REG | PRX_CFG), &temp_val);
	if (ret < 0)
		SENSOR_ERR("opt_i2c_write to prox cfg reg failed\n");

	temp_val = taos->pdata->prox_pulsecnt;
	ret = opt_i2c_write(taos, (CMD_REG | PRX_COUNT), &temp_val);
	if (ret < 0)
		SENSOR_ERR("opt_i2c_write to prox cnt reg failed\n");

	temp_val = taos->pdata->als_gain;
	ret = opt_i2c_write(taos, (CMD_REG | GAIN), &temp_val);
	if (ret < 0)
		SENSOR_ERR("opt_i2c_write to prox gain reg failed\n");

	/* Enable light sensor separately to avoid running proximity sensor at hardware level.
	 * Enabling proximity sensor separately at hardware level will lead to erroneous readings.
	 */
	if (prox_en == false)
		reg_cntrl = CNTL_ALS_ONLY_ENBL;
	else
		reg_cntrl = CNTL_INTPROXPON_ENBL;

	ret = opt_i2c_write(taos, (CMD_REG | CNTRL), &reg_cntrl);
	if (ret < 0)
		SENSOR_ERR("opt_i2c_write to ctrl reg failed\n");

	/* Minimum 58 ms delay before reading data */
	msleep(60);

	return ret;
}

static int taos_chip_off(struct taos_data *taos)
{
	int ret;
	u8 reg_cntrl;

	SENSOR_INFO("\n");

	reg_cntrl = CNTL_REG_CLEAR;
	ret = opt_i2c_write(taos, (CMD_REG | CNTRL), &reg_cntrl);
	if (ret < 0) {
		SENSOR_ERR("opt_i2c_write to ctrl reg failed\n");
		return ret;
	}

	proximity_vled_onoff(taos, OFF);
	return ret;
}

static int taos_get_cct(struct taos_data *taos)
{
	int bp1 = taos->bludata - taos->irdata;
	int rp1 = taos->reddata - taos->irdata;
	int cct = 0;

	if (rp1 != 0)
		cct = taos->pdata->cct_coef * bp1 / rp1 + taos->pdata->cct_offset;

	return cct;
}

static int taos_get_lux(struct taos_data *taos)
{
	s32 rp1, gp1, bp1;
	s32 clrdata = 0;
	s32 reddata = 0;
	s32 grndata = 0;
	s32 bludata = 0;
	s32 calculated_lux = 0;
	u8 reg_gain = 0x0;
	u16 temp_gain = 0x0;
	int gain = 1;
	int ret = 0;

	temp_gain = i2c_smbus_read_word_data(taos->i2c_client,
		(CMD_REG | GAIN));
	reg_gain = temp_gain & 0xff;

	clrdata = i2c_smbus_read_word_data(taos->i2c_client,
		(CMD_REG | CLR_CHAN0LO));
	reddata = i2c_smbus_read_word_data(taos->i2c_client,
		(CMD_REG | RED_CHAN1LO));
	grndata = i2c_smbus_read_word_data(taos->i2c_client,
		(CMD_REG | GRN_CHAN1LO));
	bludata = i2c_smbus_read_word_data(taos->i2c_client,
		(CMD_REG | BLU_CHAN1LO));

	taos->clrdata = clrdata;
	taos->reddata = reddata;
	taos->grndata = grndata;
	taos->bludata = bludata;

	switch (reg_gain & 0x03) {
	case 0x00:
		gain = 1;
		break;
	case 0x01:
		gain = 4;
		break;
	case 0x02:
		gain = 16;
		break;
/*	case 0x03:
		gain = 64;
		break;
*/
	default:
		break;
	}

	if (gain == 1 && clrdata < 25) {
		reg_gain = 0x22;
		ret = opt_i2c_write(taos, (CMD_REG | GAIN), &reg_gain);
		if (ret < 0)
			SENSOR_ERR("opt_i2c_write failed, err = %d\n", ret);

		return taos->lux;
	} else if (gain == 16 && clrdata > 15000) {
		reg_gain = 0x20;
		ret = opt_i2c_write(taos, (CMD_REG | GAIN), &reg_gain);
		if (ret < 0)
			SENSOR_ERR("opt_i2c_write failed, err = %d\n", ret);
		return taos->lux;
	}

	if ((clrdata >= 18500) && (gain == 1))
		return MAX_LUX;

	/* calculate lux */
	taos->irdata = (reddata + grndata + bludata - clrdata) / 2;
	if (taos->irdata < 0)
		taos->irdata = 0;

	/* remove ir from counts*/
	rp1 = taos->reddata - taos->irdata;
	gp1 = taos->grndata - taos->irdata;
	bp1 = taos->bludata - taos->irdata;

	calculated_lux = (rp1 * taos->pdata->coef_r + gp1 * taos->pdata->coef_g + bp1 * taos->pdata->coef_b) / 1000;

	if (calculated_lux < 0)
		calculated_lux = 0;
	else {
		/* divide by CPL, CPL = (Atime_ms * ALS_GAIN / DGF); */
		calculated_lux = calculated_lux * taos->pdata->dgf;
		calculated_lux *= taos->pdata->lux_multiple;
		calculated_lux /= Atime_ms;
		calculated_lux /= gain;
	}

	taos->lux = (int)calculated_lux;

	/* SENSOR_INFO("Lux = %d\n", taos->lux); */
	return taos->lux;
}

static void taos_light_enable(struct taos_data *taos)
{
	int cct = 0;
	int adc = 0;

	taos->count_log_time = LIGHT_LOG_TIME;

	SENSOR_INFO("starting poll timer, delay %lldns\n",
		ktime_to_ns(taos->light_poll_delay));
	taos_get_lux(taos);

	msleep(60);/*first lux value need not update to hal*/
	adc = taos_get_lux(taos);
	cct = taos_get_cct(taos);

	input_report_rel(taos->light_input_dev, REL_MISC, adc + 1);
	input_report_rel(taos->light_input_dev, REL_WHEEL, cct);
	input_sync(taos->light_input_dev);

	SENSOR_INFO("light_enable, adc: %d, cct: %d\n", adc, cct);

	hrtimer_start(&taos->timer, taos->light_poll_delay, HRTIMER_MODE_REL);
}

static void taos_light_disable(struct taos_data *taos)
{
	SENSOR_INFO("cancelling poll timer\n");
	hrtimer_cancel(&taos->timer);
	cancel_work_sync(&taos->work_light);
}

static ssize_t poll_delay_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct taos_data *taos = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%lld\n",
		ktime_to_ns(taos->light_poll_delay));
}

static ssize_t poll_delay_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct taos_data *taos = dev_get_drvdata(dev);
	int64_t new_delay;
	int err;

	err = kstrtoll(buf, 10, &new_delay);
	if (err < 0)
		return err;

	SENSOR_INFO("new delay = %lldns, old delay = %lldns\n",
		new_delay, ktime_to_ns(taos->light_poll_delay));

#ifdef CONFIG_SENSORS_BHA250_DEFENCE_SW_RESET
	if (taos->vdd_reset) {
		taos->light_poll_delay = ns_to_ktime(new_delay);
		return size;
	}
#endif
	mutex_lock(&taos->power_lock);
	if (new_delay != ktime_to_ns(taos->light_poll_delay)) {
		taos->light_poll_delay = ns_to_ktime(new_delay);
		if (taos->power_state & LIGHT_ENABLED) {
			taos_light_disable(taos);
			taos_light_enable(taos);
		}
	}
	mutex_unlock(&taos->power_lock);

	return size;
}

static ssize_t light_enable_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct taos_data *taos = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		(taos->power_state & LIGHT_ENABLED) ? 1 : 0);
}

static ssize_t proximity_enable_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct taos_data *taos = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		(taos->power_state & PROXIMITY_ENABLED) ? 1 : 0);
}

static ssize_t light_enable_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct taos_data *taos = dev_get_drvdata(dev);
	bool new_value;

	if (sysfs_streq(buf, "1"))
		new_value = true;
	else if (sysfs_streq(buf, "0"))
		new_value = false;
	else {
		SENSOR_ERR("invalid value %d\n", *buf);
		return -EINVAL;
	}

#ifdef CONFIG_SENSORS_BHA250_DEFENCE_SW_RESET
	if (taos->vdd_reset) {
		if (new_value)
			taos->power_state |= LIGHT_ENABLED;
		else
			taos->power_state &= ~LIGHT_ENABLED;

		return size;
	}
#endif

	mutex_lock(&taos->power_lock);
	SENSOR_INFO("new_value = %d, old state = %d\n",
		new_value, (taos->power_state & LIGHT_ENABLED) ? 1 : 0);

	if (new_value && !(taos->power_state & LIGHT_ENABLED)) {
		if (!taos->power_state)
			taos_chip_on(taos, false);
		taos->power_state |= LIGHT_ENABLED;
		taos_light_enable(taos);
	} else if (!new_value && (taos->power_state & LIGHT_ENABLED)) {
		taos_light_disable(taos);
		taos->power_state &= ~LIGHT_ENABLED;
		if (!taos->power_state)
			taos_chip_off(taos);
	}
	mutex_unlock(&taos->power_lock);
	return size;
}

static ssize_t proximity_enable_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct taos_data *taos = dev_get_drvdata(dev);
	bool new_value;
	int ret = 0;
	u8 reg_cntrl = 0;

	if (sysfs_streq(buf, "1"))
		new_value = true;
	else if (sysfs_streq(buf, "0"))
		new_value = false;
	else {
		SENSOR_ERR("invalid value %d\n", *buf);
		return -EINVAL;
	}

#ifdef CONFIG_SENSORS_BHA250_DEFENCE_SW_RESET
	if (taos->vdd_reset) {
		if (new_value)
			taos->power_state |= PROXIMITY_ENABLED;
		else
			taos->power_state &= ~PROXIMITY_ENABLED;

		return size;
	}
#endif
	SENSOR_INFO("new_value = %d, old state = %d\n",
	    new_value, (taos->power_state & PROXIMITY_ENABLED) ? 1 : 0);

	mutex_lock(&taos->power_lock);
	if (new_value && !(taos->power_state & PROXIMITY_ENABLED)) {
		if (taos->set_manual_thd == false) {
			ret = proximity_open_offset(taos);
			if (ret < 0 && ret != -ENOENT)
				SENSOR_ERR("proximity_open_offset() failed\n");
#ifdef CONFIG_PROX_WINDOW_TYPE
			ret = proximity_open_window_type(taos);
#endif
			taos->threshold_high = taos->pdata->prox_thresh_hi
				+ taos->offset_value;
			taos->threshold_low = taos->pdata->prox_thresh_low
				+ taos->offset_value;
			SENSOR_INFO("th_hi = %d, th_low = %d\n",
				taos->threshold_high, taos->threshold_low);
		}

		if (!taos->power_state)
			taos_chip_on(taos, true);
		else {
			/* Proximity registers are already initialized
			* so enable proximity hardware logic only. */
			reg_cntrl = CNTL_INTPROXPON_ENBL;
			ret = opt_i2c_write(taos, (CMD_REG | CNTRL), &reg_cntrl);
			if (ret < 0)
				SENSOR_ERR("Failed to write ctrl reg(%d)\n", ret);
			msleep(60);
		}

		taos->power_state |= PROXIMITY_ENABLED;
		taos->proximity_value = STATE_FAR;
		taos_thresh_set(taos);
		usleep_range(10000, 11000);

		SENSOR_INFO("Thes_high = %d, Thes_low = %d\n",
			taos_proximity_get_threshold(taos, PRX_MAXTHRESHLO),
			taos_proximity_get_threshold(taos, PRX_MINTHRESHLO));

		/* interrupt clearing */
		ret = opt_i2c_write_command(taos,
			(CMD_REG | CMD_SPL_FN | CMD_PROXALS_INTCLR));
		if (ret < 0)
			SENSOR_ERR("opt_i2c_write failed, err = %d\n", ret);

#if defined(CONFIG_SENSORS_TMD3782_PROX_ABS)
		input_report_abs(taos->proximity_input_dev, ABS_DISTANCE, 1);
#else
		input_report_rel(taos->proximity_input_dev, REL_MISC, 1 + 1);
#endif
		input_sync(taos->proximity_input_dev);

		enable_irq(taos->irq);
		enable_irq_wake(taos->irq);
	} else if (!new_value && (taos->power_state & PROXIMITY_ENABLED)) {
		disable_irq_wake(taos->irq);
		disable_irq(taos->irq);

		taos->power_state &= ~PROXIMITY_ENABLED;
		if (!taos->power_state)
			taos_chip_off(taos);
		else {
			/* Light sensor is still running
			* so disable proximity hardware logic only. */
			reg_cntrl = CNTL_ALS_ONLY_ENBL;
			ret = opt_i2c_write(taos, (CMD_REG | CNTRL), &reg_cntrl);
			if (ret < 0)
				SENSOR_ERR("Failed to write ctrl reg(%d)\n", ret);
			msleep(60);
		}
	}
	mutex_unlock(&taos->power_lock);

	return size;
}

static ssize_t proximity_state_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct taos_data *taos = dev_get_drvdata(dev);
	int adc = 0;

	adc = proximity_get_adc(taos);
	return snprintf(buf, PAGE_SIZE, "%d\n", adc);
}

#ifdef CONFIG_PROX_WINDOW_TYPE
static void change_proximity_default_threshold(struct taos_data *data)
{
	int trim = data->pdata->prox_rawdata_trim;

	switch (data->windowtype[1]) {
	case WINTYPE_WHITE:
		data->pdata->prox_thresh_hi = WHITEWINDOW_HI_THRESHOLD-trim;
		data->pdata->prox_thresh_low = WHITEWINDOW_LOW_THRESHOLD-trim;
		break;
	case WINTYPE_OTHERS:
		data->pdata->prox_thresh_hi = BLACKWINDOW_HI_THRESHOLD-trim;
		data->pdata->prox_thresh_low = BLACKWINDOW_LOW_THRESHOLD-trim;
		break;
	default:
		data->pdata->prox_thresh_hi = data->pdata->prox_thresh_hi;
		data->pdata->prox_thresh_low = data->pdata->prox_thresh_low;
		break;
	}
}

static int proximity_open_window_type(struct taos_data *data)
{
	struct file *wintype_filp = NULL;
	int err = 0;
	mm_segment_t old_fs;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	wintype_filp = filp_open(WINDOW_TYPE_FILE_PATH, O_RDONLY, 0);
	if (IS_ERR(wintype_filp)) {
		SENSOR_INFO("no window_type file\n");
		err = PTR_ERR(wintype_filp);
		if (err != -ENOENT)
			SENSOR_ERR("Can't open window_type file\n");
		set_fs(old_fs);

		data->windowtype[0] = 0;
		data->windowtype[1] = 0;
		goto exit;
	}

	err = wintype_filp->f_op->read(wintype_filp,
		(u8 *)&data->windowtype, sizeof(u8) * 2, &wintype_filp->f_pos);
	if (err != sizeof(u8) * 2) {
		SENSOR_ERR("Can't read the window_type data from file\n");
		err = -EIO;
	}

	SENSOR_INFO("%c%c\n", data->windowtype[0], data->windowtype[1]);
	filp_close(wintype_filp, current->files);
	set_fs(old_fs);
exit:
	change_proximity_default_threshold(data);
	return err;
}
#endif

static int proximity_open_offset(struct taos_data *data)
{
	struct file *offset_filp = NULL;
	int err = 0;
	mm_segment_t old_fs;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	offset_filp = filp_open(OFFSET_FILE_PATH, O_RDONLY, 0);
	if (IS_ERR(offset_filp)) {
		SENSOR_ERR("no offset file\n");
		err = PTR_ERR(offset_filp);
		if (err != -ENOENT)
			SENSOR_ERR("Can't open offset file\n");
		set_fs(old_fs);
		return err;
	}

	err = offset_filp->f_op->read(offset_filp,
		(char *)&data->offset_value, sizeof(u16), &offset_filp->f_pos);
	if (err != sizeof(u16)) {
		SENSOR_ERR("Can't read the offset data from file\n");
		err = -EIO;
	}

	SENSOR_INFO("data->offset_value = %d\n", data->offset_value);

	if (data->offset_value < (CAL_SKIP_ADC / 2)
			|| data->offset_value > (CAL_FAIL_ADC / 2))
		data->offset_value = 0;

	filp_close(offset_filp, current->files);
	set_fs(old_fs);

	return err;
}

static int proximity_adc_read(struct taos_data *taos)
{
	int sum[OFFSET_ARRAY_LENGTH];
	int i = 0;
	int avg = 0;
	int min = 0;
	int max = 0;
	int total = 0;

	mutex_lock(&taos->prox_mutex);
	for (i = 0; i < OFFSET_ARRAY_LENGTH; i++) {
		usleep_range(10000, 11000);
		sum[i] = proximity_get_adc(taos);
		if (i == 0) {
			min = sum[i];
			max = sum[i];
		} else {
			if (sum[i] < min)
				min = sum[i];
			else if (sum[i] > max)
				max = sum[i];
		}
		total += sum[i];
	}
	mutex_unlock(&taos->prox_mutex);
	total -= (min + max);
	avg = (int)(total / (OFFSET_ARRAY_LENGTH - 2));

	return avg;
}

static int proximity_store_offset(struct device *dev, bool do_calib)
{
	struct taos_data *taos = dev_get_drvdata(dev);
	struct file *offset_filp = NULL;
	mm_segment_t old_fs;
	int err = 0;
	u16 abnormal_ct = proximity_adc_read(taos);
	u16 offset = 0;

	if (do_calib) {
		/* tap offset button */
		SENSOR_INFO("calibration start\n");
		if (abnormal_ct < CAL_SKIP_ADC) {
			taos->offset_value = 0;
			taos->threshold_high = taos->pdata->prox_thresh_hi;
			taos->threshold_low = taos->pdata->prox_thresh_low;
			taos_thresh_set(taos);
			taos->set_manual_thd = false;
			taos->cal_result = 2;
			SENSOR_INFO("crosstalk < %d, skip calibration\n",
				CAL_SKIP_ADC);
		} else if ((abnormal_ct >= CAL_SKIP_ADC)
			&& (abnormal_ct <= CAL_FAIL_ADC)) {
			offset = abnormal_ct / 2;
			taos->offset_value = offset;
			taos->threshold_high = taos->pdata->prox_thresh_hi
				+ offset;
			taos->threshold_low = taos->pdata->prox_thresh_low
				+ offset;
			taos_thresh_set(taos);
			taos->set_manual_thd = false;
			taos->cal_result = 1;
		} else {
			taos->offset_value = 0;
			taos->threshold_high = taos->pdata->prox_thresh_hi;
			taos->threshold_low = taos->pdata->prox_thresh_low;
			taos_thresh_set(taos);
			taos->set_manual_thd = false;
			taos->cal_result = 0;
			SENSOR_INFO("crosstalk > %d, calibration failed\n",
				CAL_FAIL_ADC);
		}
	} else {
		/* tap reset button */
		SENSOR_INFO("reset\n");
		taos->threshold_high = taos->pdata->prox_thresh_hi;
		taos->threshold_low = taos->pdata->prox_thresh_low;
		taos_thresh_set(taos);
		taos->offset_value = 0;
		taos->cal_result = 2;
		taos->set_manual_thd = false;
	}
	SENSOR_INFO("abnormal_ct : %d, offset : %d\n",
		abnormal_ct, taos->offset_value);
	/* store offset in file */
	old_fs = get_fs();
	set_fs(KERNEL_DS);

	offset_filp = filp_open(OFFSET_FILE_PATH,
			O_CREAT | O_TRUNC | O_WRONLY, 0660);
	if (IS_ERR(offset_filp)) {
		SENSOR_ERR("Can't open prox_offset file\n");
		set_fs(old_fs);
		err = PTR_ERR(offset_filp);
		return err;
	}

	err = offset_filp->f_op->write(offset_filp,
		(char *)&taos->offset_value, sizeof(u16), &offset_filp->f_pos);
	if (err != sizeof(u16))
		SENSOR_ERR("Can't write the offset data to file\n");

	filp_close(offset_filp, current->files);
	set_fs(old_fs);

	return 1;
}

static ssize_t proximity_cal_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	bool do_calib;
	int err;

	if (sysfs_streq(buf, "1")) /* calibrate cancelation value */
		do_calib = true;
	else if (sysfs_streq(buf, "0")) /* reset cancelation value */
		do_calib = false;
	else {
		SENSOR_ERR("invalid value %d\n", *buf);
		return -EINVAL;
	}

	err = proximity_store_offset(dev, do_calib);
	if (err < 0) {
		SENSOR_ERR("proximity_store_offset() failed\n");
		return err;
	}

	return size;
}

static ssize_t proximity_cal_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct taos_data *taos = dev_get_drvdata(dev);

	proximity_open_offset(taos);

	return snprintf(buf, PAGE_SIZE, "%d,%d,%d\n",
		taos->offset_value, taos->threshold_high, taos->threshold_low);
}

static ssize_t prox_offset_pass_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct taos_data *taos = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", taos->cal_result);
}

static ssize_t proximity_avg_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct taos_data *taos = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d,%d,%d\n",
		taos->avg[0], taos->avg[1], taos->avg[2]);
}

static ssize_t proximity_avg_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct taos_data *taos = dev_get_drvdata(dev);
	int new_value = 0;

	if (sysfs_streq(buf, "1"))
		new_value = true;
	else if (sysfs_streq(buf, "0"))
		new_value = false;
	else {
		SENSOR_ERR("invalid value %d\n", *buf);
		return -EINVAL;
	}

	if (taos->prox_avg_enable == new_value)
		SENSOR_INFO(" same status\n");
	else if (new_value == 1) {
		SENSOR_INFO("starting poll timer, delay %lldns\n",
		ktime_to_ns(taos->prox_polling_time));
		hrtimer_start(&taos->prox_avg_timer,
			taos->prox_polling_time, HRTIMER_MODE_REL);
		taos->prox_avg_enable = 1;
	} else {
		SENSOR_INFO("cancelling prox avg poll timer\n");
		hrtimer_cancel(&taos->prox_avg_timer);
		cancel_work_sync(&taos->work_prox_avg);
		taos->prox_avg_enable = 0;
	}

	return size;
}

static ssize_t proximity_thresh_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct taos_data *taos = dev_get_drvdata(dev);
	int thresh_hi = 0;

	msleep(20);
	thresh_hi = taos_proximity_get_threshold(taos, PRX_MAXTHRESHLO);

	SENSOR_INFO("THRESHOLD = %d\n", thresh_hi);
	return snprintf(buf, PAGE_SIZE, "prox_threshold = %d\n", thresh_hi);
}

static ssize_t proximity_thresh_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct taos_data *taos = dev_get_drvdata(dev);
	int thresh_value = (u8)(taos->pdata->prox_thresh_hi);
	int err = 0;

	err = kstrtoint(buf, 10, &thresh_value);
	SENSOR_INFO("value = %d\n", thresh_value);
	if (err < 0)
		SENSOR_ERR("kstrtoint failed.");

	taos->threshold_high = thresh_value;
	taos_thresh_set(taos);
	msleep(20);

	return size;
}

static ssize_t thresh_high_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct taos_data *taos = dev_get_drvdata(dev);
	int thresh_hi = 0, thresh_low = 0;

	msleep(20);
	thresh_low = taos_proximity_get_threshold(taos, PRX_MINTHRESHLO);
	thresh_hi = taos_proximity_get_threshold(taos, PRX_MAXTHRESHLO);

	SENSOR_INFO("thresh_hi = %d, thresh_low = %d\n", thresh_hi, thresh_low);
	return snprintf(buf, PAGE_SIZE, "%d,%d\n", thresh_hi, thresh_low);
}

static ssize_t thresh_high_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct taos_data *taos = dev_get_drvdata(dev);
	int thresh_value = (u8)(taos->pdata->prox_thresh_hi);
	int err = 0;

	err = kstrtoint(buf, 10, &thresh_value);
	SENSOR_INFO("thresh_value = %d\n", thresh_value);
	if (err < 0)
		SENSOR_ERR("kstrtoint failed.");

	taos->threshold_high = thresh_value;
	taos_thresh_set(taos);
	msleep(20);
	taos->set_manual_thd = true;

	return size;
}
static ssize_t thresh_low_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct taos_data *taos = dev_get_drvdata(dev);
	int thresh_hi = 0, thresh_low = 0;

	msleep(20);
	thresh_hi = taos_proximity_get_threshold(taos, PRX_MAXTHRESHLO);
	thresh_low = taos_proximity_get_threshold(taos, PRX_MINTHRESHLO);

	SENSOR_INFO("thresh_hi = %d, thresh_low = %d\n", thresh_hi, thresh_low);
	return snprintf(buf, PAGE_SIZE, "%d,%d\n", thresh_hi, thresh_low);
}

static ssize_t thresh_low_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct taos_data *taos = dev_get_drvdata(dev);
	int thresh_value = (u8)(taos->pdata->prox_thresh_low);
	int err = 0;

	err = kstrtoint(buf, 10, &thresh_value);
	SENSOR_INFO("thresh_value = %d\n", thresh_value);
	if (err < 0)
		SENSOR_ERR("kstrtoint failed.");

	taos->threshold_low = thresh_value;
	taos_thresh_set(taos);
	msleep(20);
	taos->set_manual_thd = true;

	return size;
}

static ssize_t prox_trim_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct taos_data *taos = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", taos->pdata->prox_rawdata_trim);
}

static ssize_t prox_trim_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct taos_data *taos = dev_get_drvdata(dev);
	int trim_value = (u8)(taos->pdata->prox_rawdata_trim);
	int err = 0;

	err = kstrtoint(buf, 10, &trim_value);
	SENSOR_INFO(" trim_value = %d\n", trim_value);
	if (err < 0)
		SENSOR_ERR(" kstrtoint failed.");

	taos->pdata->prox_rawdata_trim = trim_value;

	return size;
}

#ifdef CONFIG_SENSORS_BHA250_DEFENCE_SW_RESET
static ssize_t taos_power_reset_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct taos_data *taos = dev_get_drvdata(dev);

	SENSOR_INFO("\n");
	taos->vdd_reset = 1;

	mutex_lock(&taos->power_lock);

	if (taos->power_state & PROXIMITY_ENABLED) {
		disable_irq_wake(taos->irq);
		disable_irq(taos->irq);
	}

	if (taos->power_state & LIGHT_ENABLED)
		taos_light_disable(taos);

	taos_chip_off(taos);
	mutex_unlock(&taos->power_lock);

	return snprintf(buf, PAGE_SIZE, "%d\n", 1);
}

static ssize_t taos_sw_reset_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct taos_data *taos = dev_get_drvdata(dev);

	SENSOR_INFO("\n");

	mutex_lock(&taos->power_lock);

	taos_chip_on(taos, true);
	if (!taos->power_state)
		taos_chip_off(taos);

	if (taos->power_state & LIGHT_ENABLED)
		taos_light_enable(taos);

	if (taos->power_state & PROXIMITY_ENABLED) {
		if (taos->set_manual_thd == false) {
			proximity_open_offset(taos);
#ifdef CONFIG_PROX_WINDOW_TYPE
			proximity_open_window_type(taos);
#endif
			taos->threshold_high = taos->pdata->prox_thresh_hi
				+ taos->offset_value;
			taos->threshold_low = taos->pdata->prox_thresh_low
				+ taos->offset_value;
			SENSOR_INFO("th_hi = %d, th_low = %d\n",
				taos->threshold_high, taos->threshold_low);
		}

		proximity_vled_onoff(taos, ON);
		taos->proximity_value = STATE_FAR;
		taos_thresh_set(taos);
		mdelay(10);

		SENSOR_INFO("Thes_high = %d,Thes_low = %d\n",
			taos_proximity_get_threshold(taos, PRX_MAXTHRESHLO),
			taos_proximity_get_threshold(taos, PRX_MINTHRESHLO));

		/* interrupt clearing */
		opt_i2c_write_command(taos,
			(CMD_REG | CMD_SPL_FN | CMD_PROXALS_INTCLR));

		enable_irq(taos->irq);
		enable_irq_wake(taos->irq);
	}

	mutex_unlock(&taos->power_lock);
	taos->vdd_reset = 0;

	return snprintf(buf, PAGE_SIZE, "%d\n", 1);
}
#endif

static ssize_t get_vendor_name(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", VENDOR_NAME);
}

static ssize_t get_chip_name(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", CHIP_NAME);
}

static DEVICE_ATTR(vendor, S_IRUGO, get_vendor_name, NULL);
static DEVICE_ATTR(name, S_IRUGO, get_chip_name, NULL);

static int lightsensor_get_adcvalue(struct taos_data *taos)
{
	int i = 0;
	int j = 0;
	unsigned int adc_total = 0;
	int adc_avr_value;
	unsigned int adc_index = 0;
	unsigned int adc_max = 0;
	unsigned int adc_min = 0;
	int value = 0;

#ifdef CONFIG_SENSORS_BHA250_DEFENCE_SW_RESET
	if (taos->vdd_reset) {
		SENSOR_INFO("vdd: turned off.\n");
		return -EPERM;
	}
#endif
	/* get ADC */
	value = taos_get_lux(taos);

	adc_index = (taos->adc_index_count++) % ADC_BUFFER_NUM;

	/*ADC buffer initialize (light sensor off ---> light sensor on) */
	if (!taos->adc_buf_initialized) {
		taos->adc_buf_initialized = true;
		for (j = 0; j < ADC_BUFFER_NUM; j++)
			taos->adc_value_buf[j] = value;
	} else
		taos->adc_value_buf[adc_index] = value;

	adc_max = taos->adc_value_buf[0];
	adc_min = taos->adc_value_buf[0];

	for (i = 0; i < ADC_BUFFER_NUM; i++) {
		adc_total += taos->adc_value_buf[i];

		if (adc_max < taos->adc_value_buf[i])
			adc_max = taos->adc_value_buf[i];

		if (adc_min > taos->adc_value_buf[i])
			adc_min = taos->adc_value_buf[i];
	}
	adc_avr_value = (adc_total-(adc_max+adc_min))/(ADC_BUFFER_NUM - 2);

	if (taos->adc_index_count == ADC_BUFFER_NUM)
		taos->adc_index_count = 0;

	return adc_avr_value;
}

static ssize_t lightsensor_file_state_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct taos_data *taos = dev_get_drvdata(dev);
	int adc;

	adc = lightsensor_get_adcvalue(taos);

	return snprintf(buf, PAGE_SIZE, "%d\n", adc);
}

static ssize_t lightsensor_raw_data_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct taos_data *taos = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%u,%u,%u,%u\n",
		taos->reddata, taos->grndata, taos->bludata, taos->clrdata);
}

static struct device_attribute dev_attr_light_raw_data =
	__ATTR(raw_data, S_IRUGO, lightsensor_raw_data_show, NULL);

static DEVICE_ATTR(adc, S_IRUGO, lightsensor_file_state_show, NULL);
static DEVICE_ATTR(lux, S_IRUGO, lightsensor_file_state_show, NULL);
static DEVICE_ATTR(poll_delay, S_IRUGO | S_IWUSR | S_IWGRP,
		   poll_delay_show, poll_delay_store);

#ifdef CONFIG_SENSORS_BHA250_DEFENCE_SW_RESET
static DEVICE_ATTR(power_reset, S_IRUSR | S_IRGRP,
	taos_power_reset_show, NULL);
static DEVICE_ATTR(sw_reset, S_IRUSR | S_IRGRP,
	taos_sw_reset_show, NULL);
#endif
static struct device_attribute dev_attr_light_enable =
	__ATTR(enable, S_IRUGO | S_IWUSR | S_IWGRP,
	       light_enable_show, light_enable_store);

static struct attribute *light_sysfs_attrs[] = {
	&dev_attr_light_enable.attr,
	&dev_attr_poll_delay.attr,
	NULL
};

static struct attribute_group light_attribute_group = {
	.attrs = light_sysfs_attrs,
};

static struct device_attribute *lightsensor_additional_attributes[] = {
	&dev_attr_adc,
	&dev_attr_lux,
	&dev_attr_vendor,
	&dev_attr_name,
	&dev_attr_light_raw_data,
#ifdef CONFIG_SENSORS_BHA250_DEFENCE_SW_RESET
	&dev_attr_power_reset,
	&dev_attr_sw_reset,
#endif
	NULL
};

static struct device_attribute dev_attr_proximity_enable =
	__ATTR(enable, S_IRUGO | S_IWUSR | S_IWGRP,
	       proximity_enable_show, proximity_enable_store);

static struct attribute *proximity_sysfs_attrs[] = {
	&dev_attr_proximity_enable.attr,
	NULL
};

static struct attribute_group proximity_attribute_group = {
	.attrs = proximity_sysfs_attrs,
};

static struct device_attribute dev_attr_proximity_raw_data =
	__ATTR(raw_data, S_IRUGO, proximity_state_show, NULL);

static DEVICE_ATTR(prox_cal, S_IRUGO | S_IWUSR, proximity_cal_show,
	proximity_cal_store);
static DEVICE_ATTR(prox_avg, S_IRUGO | S_IWUSR, proximity_avg_show,
	proximity_avg_store);
static DEVICE_ATTR(state, S_IRUGO, proximity_state_show, NULL);
static DEVICE_ATTR(prox_offset_pass, S_IRUGO,
	prox_offset_pass_show, NULL);
static DEVICE_ATTR(prox_thresh, 0644, proximity_thresh_show,
	proximity_thresh_store);
static DEVICE_ATTR(thresh_high, 0644, thresh_high_show,
	thresh_high_store);
static DEVICE_ATTR(thresh_low, 0644, thresh_low_show,
	thresh_low_store);
static DEVICE_ATTR(prox_trim, S_IRUGO | S_IWUSR | S_IWGRP,
	prox_trim_show, prox_trim_store);

static struct device_attribute *prox_sensor_attrs[] = {
	&dev_attr_state,
	&dev_attr_prox_avg,
	&dev_attr_vendor,
	&dev_attr_name,
	&dev_attr_proximity_raw_data,
	&dev_attr_prox_cal,
	&dev_attr_prox_offset_pass,
	&dev_attr_prox_thresh,
	&dev_attr_thresh_high,
	&dev_attr_thresh_low,
	&dev_attr_prox_trim,
	NULL
};

static void taos_work_func_light(struct work_struct *work)
{
	struct taos_data *taos = container_of(work, struct taos_data,
					      work_light);
	int adc = taos_get_lux(taos);
	int cct = taos_get_cct(taos);

	input_report_rel(taos->light_input_dev, REL_MISC, adc + 1);
	input_report_rel(taos->light_input_dev, REL_WHEEL, cct);
	input_sync(taos->light_input_dev);

	if (taos->count_log_time >= LIGHT_LOG_TIME) {
		SENSOR_INFO("R = %d, G = %d, B = %d, C = %d, ir = %d, lux = %d, cct = %d\n",
			taos->reddata, taos->grndata, taos->bludata,
			taos->clrdata, taos->irdata, adc, cct);
		taos->count_log_time = 0;
	} else
		taos->count_log_time++;
}

static void taos_work_func_prox(struct work_struct *work)
{
	struct taos_data *taos =
		container_of(work, struct taos_data, work_prox);
	int adc_data;
	int threshold_high;
	int threshold_low;
	int proximity_value = 0;
	u8 chipid = 0x00;
	int ret = 0;
	int i = 0;

	/* disable INT */
	disable_irq_nosync(taos->irq);

	while (chipid != 0x69 && i < 10) {
		usleep_range(5000, 6000);
		ret = opt_i2c_read(taos, CHIPID, &chipid);
		i++;
	}
	if (ret < 0)
		SENSOR_ERR("opt_i2c_read failed, err = %d\n", ret);

	/* change Threshold */
	mutex_lock(&taos->prox_mutex);
	adc_data = proximity_get_adc(taos);
	mutex_unlock(&taos->prox_mutex);

	threshold_high = taos_proximity_get_threshold(taos, PRX_MAXTHRESHLO);
	threshold_low = taos_proximity_get_threshold(taos, PRX_MINTHRESHLO);
	SENSOR_INFO("hi = %d, low = %d, adc_data = %d\n",
		taos->threshold_high, taos->threshold_low, adc_data);
	if ((threshold_high == (taos->threshold_high))
			&& (adc_data >= (taos->threshold_high))) {
		proximity_value = STATE_CLOSE;
#if defined(CONFIG_SENSORS_TMD3782_PROX_ABS)
		input_report_abs(taos->proximity_input_dev,
			ABS_DISTANCE, proximity_value);
#else
		input_report_rel(taos->proximity_input_dev,
			REL_MISC, proximity_value + 1);
#endif
		input_sync(taos->proximity_input_dev);
		SENSOR_INFO("prox value = %d\n", proximity_value);
	} else if ((threshold_high == (0xFFFF))
			&& (adc_data <= (taos->threshold_low))) {
		proximity_value = STATE_FAR;
#if defined(CONFIG_SENSORS_TMD3782_PROX_ABS)
		input_report_abs(taos->proximity_input_dev,
			ABS_DISTANCE, proximity_value);
#else
		input_report_rel(taos->proximity_input_dev,
			REL_MISC, proximity_value + 1);
#endif
		input_sync(taos->proximity_input_dev);
		SENSOR_INFO("prox value = %d\n", proximity_value);
	} else {
		SENSOR_ERR("Error Case!adc=[%X], th_high=[%d], th_min=[%d]\n",
			adc_data, threshold_high, threshold_low);
		goto exit;
	}

	taos->proximity_value = proximity_value;
	taos_thresh_set(taos);
	/* reset Interrupt pin */
	/* to active Interrupt, TMD2771x Interuupt pin shoud be reset. */
exit:
	i2c_smbus_write_byte(taos->i2c_client,
		(CMD_REG | CMD_SPL_FN | CMD_PROXALS_INTCLR));
	/* enable INT */
	enable_irq(taos->irq);
}

static void taos_work_func_prox_avg(struct work_struct *work)
{
	struct taos_data *taos = container_of(work, struct taos_data,
		work_prox_avg);
	int proximity_value = 0;
	int min = 0, max = 0, avg = 0;
	int i = 0;

	for (i = 0; i < PROX_AVG_COUNT; i++) {
		mutex_lock(&taos->prox_mutex);

		proximity_value = proximity_get_adc(taos);
		mutex_unlock(&taos->prox_mutex);
		if (proximity_value > TAOS_PROX_MIN) {
			avg += proximity_value;
			if (!i)
				min = proximity_value;
			if (proximity_value < min)
				min = proximity_value;
			if (proximity_value > max)
				max = proximity_value;
		} else {
			proximity_value = TAOS_PROX_MIN;
		}
		msleep(40);
	}
	avg /= i;
	taos->avg[0] = min;
	taos->avg[1] = avg;
	taos->avg[2] = max;
}


/* This function is for light sensor.  It operates every a few seconds.
 * It asks for work to be done on a thread because i2c needs a thread
 * context (slow and blocking) and then reschedules the timer to run again.
 */
static enum hrtimer_restart taos_timer_func(struct hrtimer *timer)
{
	struct taos_data *taos = container_of(timer, struct taos_data, timer);

	queue_work(taos->wq, &taos->work_light);
	hrtimer_forward_now(&taos->timer, taos->light_poll_delay);
	return HRTIMER_RESTART;
}

static enum hrtimer_restart taos_prox_timer_func(struct hrtimer *timer)
{
	struct taos_data *taos = container_of(timer, struct taos_data,
		prox_avg_timer);
	queue_work(taos->wq_avg, &taos->work_prox_avg);
	hrtimer_forward_now(&taos->prox_avg_timer, taos->prox_polling_time);

	return HRTIMER_RESTART;
}

/* interrupt happened due to transition/change of near/far proximity state */
irqreturn_t taos_irq_handler(int irq, void *data)
{
	struct taos_data *ip = data;

	if (ip->irq != -1) {
		wake_lock_timeout(&ip->prx_wake_lock, 3*HZ);
		queue_work(ip->wq, &ip->work_prox);
	}
	SENSOR_INFO("taos interrupt handler is called\n");
	return IRQ_HANDLED;
}

static int taos_setup_irq(struct taos_data *taos)
{
	int rc = -EIO;
	struct taos_platform_data *pdata = taos->pdata;
	int irq;

	rc = gpio_request(pdata->als_int, "gpio_proximity_out");
	if (rc < 0) {
		SENSOR_ERR("gpio %d request failed (%d)\n",
			pdata->als_int, rc);
		return rc;
	}

	rc = gpio_direction_input(pdata->als_int);
	if (rc < 0) {
		SENSOR_ERR("failed to set gpio %d as input (%d)\n",
			pdata->als_int, rc);
		goto err_gpio_direction_input;
	}

	irq = gpio_to_irq(pdata->als_int);

	rc = request_threaded_irq(irq, NULL, taos_irq_handler,
		IRQF_TRIGGER_FALLING | IRQF_ONESHOT, "proximity_int", taos);
	if (rc < 0) {
		SENSOR_ERR("request_irq(%d) failed for gpio %d (%d)\n",
			irq, pdata->als_int, rc);
		goto err_request_irq;
	}

	/* start with interrupts disabled */
	disable_irq(irq);
	taos->irq = irq;

	SENSOR_INFO("success\n");

	goto done;

err_request_irq:
err_gpio_direction_input:
	gpio_free(pdata->als_int);
done:
	return rc;
}

static int taos_get_initial_offset(struct taos_data *taos)
{
	int ret = 0;
	u8 p_offset = 0;

	ret = proximity_open_offset(taos);
	if (ret < 0) {
		p_offset = 0;
		taos->offset_value = 0;
	} else
		p_offset = taos->offset_value;

	SENSOR_ERR("initial offset = %d\n", p_offset);

	return p_offset;
}

#ifdef CONFIG_OF
/* device tree parsing function */
static int taos_parse_dt(struct device *dev, struct taos_platform_data *pdata)
{
	int ret = 0;
	u32 sign[6] = {2, 2, 2, 2, 2, 2};
	enum of_gpio_flags flags;
	struct device_node *np = dev->of_node;

	if (!pdata) {
		SENSOR_ERR("missing pdata\n");
		return -ENOMEM;
	}

	pdata->vled_ldo = of_get_named_gpio_flags(np, "taos,vled_ldo", 0,
		&flags);
	if (pdata->vled_ldo < 0) {
		SENSOR_ERR("fail to get vled_ldo: means to use regulator as vLED\n");
		pdata->vled_ldo = 0;
	} else {
		ret = gpio_request(pdata->vled_ldo, "prox_vled_en");
		if (ret < 0) {
			SENSOR_ERR("gpio %d request failed (%d)\n",
				pdata->vled_ldo, ret);
			return ret;
		} else
			gpio_direction_output(pdata->vled_ldo, 0);
	}

	pdata->als_int = of_get_named_gpio_flags(np, "taos,irq_gpio", 0,
		&flags);

	ret = of_property_read_u32(np, "taos,prox_rawdata_trim",
		&pdata->prox_rawdata_trim);
	ret = of_property_read_u32(np, "taos,prox_thresh_hi",
		&pdata->prox_thresh_hi);
	ret = of_property_read_u32(np, "taos,prox_thresh_low",
		&pdata->prox_thresh_low);
	ret = of_property_read_u32(np, "taos,als_time",
		&pdata->als_time);
	ret = of_property_read_u32(np, "taos,intr_filter",
		&pdata->intr_filter);
	ret = of_property_read_u32(np, "taos,prox_pulsecnt",
		&pdata->prox_pulsecnt);
	ret = of_property_read_u32(np, "taos,als_gain",
		&pdata->als_gain);

	pdata->dgf = DGF;
	pdata->cct_coef = CT_Coef1;
	pdata->cct_offset = CT_Offset1;
	pdata->coef_r = R_Coef1;
	pdata->coef_g = G_Coef1;
	pdata->coef_b = B_Coef1;
	pdata->lux_multiple = 10;	/* lux * 1 */

	ret = of_property_read_u32(np, "taos,lux_multiple",
		&pdata->lux_multiple);

	if (of_property_read_u32_array(np, "taos,coef_sign", sign, 6) >= 0) {
		if (of_property_read_u32(np, "taos,dgf", &pdata->dgf) >= 0)
			pdata->dgf *= (sign[0]-1);
		if (of_property_read_u32(np, "taos,cct_coef", &pdata->cct_coef) >= 0)
			pdata->cct_coef *= (sign[1]-1);
		if (of_property_read_u32(np, "taos,cct_offset", &pdata->cct_offset) >= 0)
			pdata->cct_offset *= (sign[2]-1);
		if (of_property_read_u32(np, "taos,coef_r", &pdata->coef_r) >= 0)
			pdata->coef_r *= (sign[3]-1);
		if (of_property_read_u32(np, "taos,coef_g", &pdata->coef_g) >= 0)
			pdata->coef_g *= (sign[4]-1);
		if (of_property_read_u32(np, "taos,coef_b", &pdata->coef_b) >= 0)
			pdata->coef_b *= (sign[5]-1);
	}

	SENSOR_INFO("irq_gpio:%d, dgf: %d, cct_coef:%d, cct_offset:%d, r:%d, g:%d, b:%d\n",
		pdata->als_int, pdata->dgf, pdata->cct_coef, pdata->cct_offset,
		pdata->coef_r, pdata->coef_g, pdata->coef_b);

	return 0;
}
#else
static int taos_parse_dt(struct device *dev, struct taos_platform_data *pdata)
{
	return -ENODEV;
}
#endif

static int taos_i2c_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int ret = -ENODEV;
	int chipid = 0;
	int retry = 0;
	struct taos_data *taos;
	struct taos_platform_data *pdata = NULL;

	SENSOR_INFO("Start\n");

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		SENSOR_ERR("i2c functionality check failed!\n");
		return ret;
	}

	taos = kzalloc(sizeof(struct taos_data), GFP_KERNEL);
	if (!taos) {
		SENSOR_ERR("failed to alloc memory for module data\n");
		ret = -ENOMEM;
		goto done;
	}

	/* parsing data */
	if (client->dev.of_node) {
		pdata = devm_kzalloc(&client->dev,
			sizeof(struct taos_platform_data), GFP_KERNEL);
		ret = taos_parse_dt(&client->dev, pdata);
		if (ret) {
			SENSOR_ERR("error in device tree\n");
			goto err_taos_data_free;
		}
	} else
		pdata = client->dev.platform_data;

	if (!pdata) {
		SENSOR_ERR("missing pdata!\n");
		goto err_taos_data_free;
	}

	taos->i2c_client = client;

sreboot:
	proximity_vled_onoff(taos, ON);
	usleep_range(9000, 10000);

	/* ID Check */
	chipid = i2c_smbus_read_byte_data(client, CMD_REG | CHIPID);
	if (chipid != CHIP_ID) {
		SENSOR_ERR("i2c read error 0x[%X]\n", chipid);
		proximity_vled_onoff(taos, OFF);
		usleep_range(9000, 10000);
		if (retry < RETRY_REBOOT) {
			retry++;
			goto sreboot;
		}
		ret = -ENXIO;
		SENSOR_ERR("chip ID read error\n");
		goto err_chip_id_or_i2c_error;
	}

#ifdef CONFIG_SENSORS_BHA250_DEFENCE_SW_RESET
	taos->vdd_reset = 0;
#endif
	taos->pdata = pdata;
	i2c_set_clientdata(client, taos);
	taos->lux = 0;
	taos->offset_value = taos_get_initial_offset(taos);
#ifdef CONFIG_PROX_WINDOW_TYPE
	proximity_open_window_type(taos);
#endif
	taos->set_manual_thd = false;
	taos->threshold_high = taos->pdata->prox_thresh_hi + taos->offset_value;
	taos->threshold_low = taos->pdata->prox_thresh_low + taos->offset_value;

	/* wake lock init */
	wake_lock_init(&taos->prx_wake_lock, WAKE_LOCK_SUSPEND,
		"prx_wake_lock");
	mutex_init(&taos->prox_mutex);
	mutex_init(&taos->power_lock);

	/* allocate proximity input_device */
	taos->proximity_input_dev = input_allocate_device();
	if (!taos->proximity_input_dev) {
		SENSOR_ERR("could not allocate input device\n");
		goto err_input_allocate_device_proximity;
	}

	input_set_drvdata(taos->proximity_input_dev, taos);
	taos->proximity_input_dev->name = MODULE_NAME_PROX;
#if defined(CONFIG_SENSORS_TMD3782_PROX_ABS)
	input_set_capability(taos->proximity_input_dev, EV_ABS, ABS_DISTANCE);
	input_set_abs_params(taos->proximity_input_dev, ABS_DISTANCE,
		0, 1, 0, 0);
#else
	input_set_capability(taos->proximity_input_dev, EV_REL, REL_MISC);
#endif

	SENSOR_INFO("registering proximity input device\n");
	ret = input_register_device(taos->proximity_input_dev);
	if (ret < 0) {
		SENSOR_ERR("could not register input device\n");
		goto err_input_register_device_proximity;
	}
	ret = sensors_register(&taos->proximity_dev, taos, prox_sensor_attrs,
		MODULE_NAME_PROX); /* factory attributs */
	if (ret < 0) {
		SENSOR_ERR("could not registersensors_register\n");
		goto err_sensor_register_device_proximity;
	}
	ret = sensors_create_symlink(&taos->proximity_input_dev->dev.kobj,
		taos->proximity_input_dev->name);
	if (ret < 0) {
		SENSOR_ERR("fail: sensors_create_symlink\n");
		goto err_symlink_device_proximity;
	}
	ret = sysfs_create_group(&taos->proximity_input_dev->dev.kobj,
		&proximity_attribute_group);
	if (ret < 0) {
		SENSOR_ERR("could not create sysfs group\n");
		goto err_create_sensorgoup_proximity;
	}

	/* hrtimer settings.  we poll for light values using a timer. */
	hrtimer_init(&taos->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	taos->light_poll_delay = ns_to_ktime(200 * NSEC_PER_MSEC);
	taos->timer.function = taos_timer_func;

	/* the timer just fires off a work queue request.  we need a thread
	   to read the i2c (can be slow and blocking). */
	taos->wq = create_singlethread_workqueue("taos_wq");
	if (!taos->wq) {
		ret = -ENOMEM;
		SENSOR_ERR("could not create workqueue\n");
		goto err_create_workqueue;
	}

	taos->wq_avg = create_singlethread_workqueue("taos_wq_avg");
	if (!taos->wq_avg) {
		ret = -ENOMEM;
		SENSOR_ERR("could not create workqueue\n");
		goto err_create_avg_workqueue;
	}

	/* this is the thread function we run on the work queue */
	INIT_WORK(&taos->work_light, taos_work_func_light);
	INIT_WORK(&taos->work_prox, taos_work_func_prox);
	INIT_WORK(&taos->work_prox_avg, taos_work_func_prox_avg);
	taos->prox_avg_enable = 0;

	hrtimer_init(&taos->prox_avg_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	taos->prox_polling_time = ns_to_ktime(2000 * NSEC_PER_MSEC);
	taos->prox_avg_timer.function = taos_prox_timer_func;

	/* allocate lightsensor-level input_device */
	taos->light_input_dev = input_allocate_device();
	if (!taos->light_input_dev) {
		SENSOR_ERR("could not allocate input device\n");
		ret = -ENOMEM;
		goto err_input_allocate_device_light;
	}
	input_set_drvdata(taos->light_input_dev, taos);
	taos->light_input_dev->name = MODULE_NAME_LIGHT;
	input_set_capability(taos->light_input_dev, EV_REL, REL_MISC);
	input_set_capability(taos->light_input_dev, EV_REL, REL_WHEEL);

	SENSOR_INFO("registering lightsensor-level input device\n");
	ret = input_register_device(taos->light_input_dev);
	if (ret < 0) {
		SENSOR_ERR("could not register input device\n");
		goto err_input_register_device_light;
	}
	ret = sensors_register(&taos->light_dev, taos,
		lightsensor_additional_attributes, MODULE_NAME_LIGHT);
	if (ret < 0) {
		SENSOR_ERR("cound not register light sensor device(%d)\n",
			ret);
		goto err_sensor_register_device_light;
	}

	ret = sensors_create_symlink(&taos->light_input_dev->dev.kobj,
		taos->light_input_dev->name);
	if (ret < 0) {
		SENSOR_ERR("cound not sensors_create_symlink(%d).\n", ret);
		goto err_symlink_device_light;
	}

	ret = sysfs_create_group(&taos->light_input_dev->dev.kobj,
		&light_attribute_group);
	if (ret < 0) {
		SENSOR_ERR("could not create sysfs group\n");
		goto err_create_sensorgoup_light;
	}

	/* setup irq */
	ret = taos_setup_irq(taos);
	if (ret < 0) {
		SENSOR_ERR("could not setup irq\n");
		goto err_setup_irq;
	}

	SENSOR_INFO("success\n");
	return ret;

/* error, unwind it all */
err_setup_irq:
err_create_sensorgoup_light:
	sensors_remove_symlink(&taos->light_input_dev->dev.kobj,
		taos->proximity_input_dev->name);
err_symlink_device_light:
	sensors_unregister(taos->light_dev, lightsensor_additional_attributes);
err_sensor_register_device_light:
	input_unregister_device(taos->light_input_dev);
err_input_register_device_light:
	input_free_device(taos->light_input_dev);
err_input_allocate_device_light:
	destroy_workqueue(taos->wq_avg);
err_create_avg_workqueue:
	destroy_workqueue(taos->wq);
err_create_workqueue:
	sysfs_remove_group(&taos->proximity_input_dev->dev.kobj,
		&proximity_attribute_group);
err_create_sensorgoup_proximity:
	sensors_remove_symlink(&taos->proximity_input_dev->dev.kobj,
		taos->proximity_input_dev->name);
err_symlink_device_proximity:
	sensors_unregister(taos->proximity_dev, prox_sensor_attrs);
err_sensor_register_device_proximity:
	input_unregister_device(taos->proximity_input_dev);
err_input_register_device_proximity:
	input_free_device(taos->proximity_input_dev);
err_input_allocate_device_proximity:
	mutex_destroy(&taos->power_lock);
	mutex_destroy(&taos->prox_mutex);
	wake_lock_destroy(&taos->prx_wake_lock);
err_chip_id_or_i2c_error:
	if (pdata->vled_ldo)
		gpio_free(pdata->vled_ldo);
err_taos_data_free:
	kfree(taos);
done:
	SENSOR_ERR("failed\n");
	return ret;
}

static int taos_suspend(struct device *dev)
{
	/* We disable power only if proximity is disabled.  If proximity
	   is enabled, we leave power on because proximity is allowed
	   to wake up device.  We remove power without changing
	   taos->power_state because we use that state in resume.
	*/
	struct i2c_client *client = to_i2c_client(dev);
	struct taos_data *taos = i2c_get_clientdata(client);

	if (taos->power_state & LIGHT_ENABLED)
		taos_light_disable(taos);
	if (taos->power_state == LIGHT_ENABLED)
		taos_chip_off(taos);

	if (taos->power_state & PROXIMITY_ENABLED)
		disable_irq(taos->irq);

	return 0;
}

static int taos_resume(struct device *dev)
{
	/* Turn power back on if we were before suspend. */
	struct i2c_client *client = to_i2c_client(dev);
	struct taos_data *taos = i2c_get_clientdata(client);

	if (taos->power_state == LIGHT_ENABLED)
		taos_chip_on(taos, false);

	if (taos->power_state & LIGHT_ENABLED)
		taos_light_enable(taos);

	if (taos->power_state & PROXIMITY_ENABLED)
		enable_irq(taos->irq);

	SENSOR_INFO("is called\n");

	return 0;
}

static int taos_i2c_remove(struct i2c_client *client)
{
	SENSOR_INFO("\n");
	return 0;
}

static const struct i2c_device_id taos_device_id[] = {
	{"taos", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, taos_device_id);

static const struct dev_pm_ops taos_pm_ops = {
	.suspend = taos_suspend,
	.resume = taos_resume
};

#ifdef CONFIG_OF
static struct of_device_id tm3782_match_table[] = {
	{ .compatible = "taos,tmd3782",},
};
#else
#define tm3782_match_table NULL
#endif

static struct i2c_driver taos_i2c_driver = {
	.driver = {
		.name = "taos",
		.owner = THIS_MODULE,
		.pm = &taos_pm_ops,
		.of_match_table = tm3782_match_table,
	},
	.probe		= taos_i2c_probe,
	.remove		= taos_i2c_remove,
	.id_table	= taos_device_id,
};


static int __init taos_init(void)
{
	return i2c_add_driver(&taos_i2c_driver);
}

static void __exit taos_exit(void)
{
	i2c_del_driver(&taos_i2c_driver);
}

module_init(taos_init);
module_exit(taos_exit);

MODULE_AUTHOR("SAMSUNG");
MODULE_DESCRIPTION("Optical Sensor driver for taos");
MODULE_LICENSE("GPL");
