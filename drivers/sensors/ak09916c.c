/*
 * Copyright (C) 2013 Samsung Electronics. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/errno.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>

#include <linux/sensor/sensors_core.h>
#include "ak09916c_reg.h"

/* Rx buffer size. i.e ST,TMPS,H1X,H1Y,H1Z*/
#define SENSOR_DATA_SIZE                9
#define AK09916C_DEFAULT_DELAY          200000000LL
#define AK09916C_DRDY_TIMEOUT_MS        100
#define AK09916C_WIA1_VALUE             0x48
#define AK09916C_WIA2_VALUE             0x09

#define I2C_M_WR                        0 /* for i2c Write */
#define I2c_M_RD                        1 /* for i2c Read */

#define VENDOR_NAME                     "AKM"
#define MODEL_NAME                      "AK09916C"
#define MODULE_NAME                     "magnetic_sensor"

#define AK09916C_TOP_LOWER_RIGHT         0
#define AK09916C_TOP_LOWER_LEFT          1
#define AK09916C_TOP_UPPER_LEFT          2
#define AK09916C_TOP_UPPER_RIGHT         3
#define AK09916C_BOTTOM_LOWER_RIGHT      4
#define AK09916C_BOTTOM_LOWER_LEFT       5
#define AK09916C_BOTTOM_UPPER_LEFT       6
#define AK09916C_BOTTOM_UPPER_RIGHT      7

#define AK09916C_MAX_DELAY               200000000LL
#define AK09916C_MIN_DELAY               10000000LL

#define MAG_LOG_TIME                15 /* 15 sec */

enum {
	OFF = 0,
	ON = 1,
};

struct ak09916c_v {
	union {
		s16 v[3];
		struct {
			s16 x;
			s16 y;
			s16 z;
		};
	};
};

struct ak09916c_p {
	struct i2c_client *client;
	struct input_dev *input;
	struct device *factory_device;
	struct ak09916c_v magdata;
	struct mutex lock;
	struct mutex enable_lock;

	ktime_t delay;
	struct work_struct mag_work;
	struct workqueue_struct *wq;
	struct hrtimer mag_timer;
	int time_count;

	atomic_t enable;
#if defined(CONFIG_SENSORS_BHA250_DEFENCE_SW_RESET)
	int reset_state;
#endif
	int cal_reset_flag;
	u8 asa[3];
	u32 chip_pos;
	u64 timestamp;
	int ak09916c_ldo_pin;
	u32 si_p[27];
};

static int ak09916c_i2c_read(struct i2c_client *client,
		unsigned char reg_addr, unsigned char *buf)
{
	int ret;
	struct i2c_msg msg[2];
#if defined(CONFIG_SENSORS_BHA250_DEFENCE_SW_RESET)
	struct ak09916c_p *data =
			(struct ak09916c_p *)i2c_get_clientdata(client);

	if (data->reset_state)
		return -EPERM;
#endif

	msg[0].addr = client->addr;
	msg[0].flags = I2C_M_WR;
	msg[0].len = 1;
	msg[0].buf = &reg_addr;

	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 1;
	msg[1].buf = buf;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret < 0) {
		SENSOR_ERR("i2c bus read error %d\n", ret);
		return ret;
	}

	return 0;
}

static int ak09916c_i2c_write(struct i2c_client *client,
		unsigned char reg_addr, unsigned char buf)
{
	int ret;
	struct i2c_msg msg;
	unsigned char w_buf[2];

#if defined(CONFIG_SENSORS_BHA250_DEFENCE_SW_RESET)
	struct ak09916c_p *data =
			(struct ak09916c_p *)i2c_get_clientdata(client);

	if (data->reset_state) {
		SENSOR_INFO("reset_state %d start!!!\n", data->reset_state);
		return -EPERM;
	}
#endif

	w_buf[0] = reg_addr;
	w_buf[1] = buf;

	msg.addr = client->addr;
	msg.flags = I2C_M_WR;
	msg.len = 2;
	msg.buf = (char *)w_buf;

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0) {
		SENSOR_ERR("i2c bus write error %d\n", ret);
		return ret;
	}

	return 0;
}

static int ak09916c_i2c_read_block(struct i2c_client *client,
	unsigned char reg_addr, unsigned char *buf, unsigned char len)
{
	int ret;
	struct i2c_msg msg[2];

#if defined(CONFIG_SENSORS_BHA250_DEFENCE_SW_RESET)
	struct ak09916c_p *data =
			(struct ak09916c_p *)i2c_get_clientdata(client);

	if (data->reset_state)
		return -EPERM;
#endif

	msg[0].addr = client->addr;
	msg[0].flags = I2C_M_WR;
	msg[0].len = 1;
	msg[0].buf = &reg_addr;

	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = len;
	msg[1].buf = buf;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret < 0) {
		SENSOR_ERR("i2c bus read error %d\n", ret);
		return ret;
	}

	return 0;
}

static int ak09916c_ecs_set_mode_power_down(struct ak09916c_p *data)
{
	unsigned char reg;
	int ret;

	reg = AK09916C_MODE_POWERDOWN;
	ret = ak09916c_i2c_write(data->client, AK09916C_REG_CNTL2, reg);

	return ret;
}

static int ak09916c_ecs_set_mode(struct ak09916c_p *data, char mode)
{
	u8 reg;
	int ret;

	switch (mode & 0x1F) {
	case AK09916C_MODE_SNG_MEASURE:
		reg = AK09916C_MODE_SNG_MEASURE;
		ret = ak09916c_i2c_write(data->client, AK09916C_REG_CNTL2, reg);
		break;
	case AK09916C_MODE_POWERDOWN:
		reg = AK09916C_MODE_SNG_MEASURE;
		ret = ak09916c_ecs_set_mode_power_down(data);
		break;
	case AK09916C_MODE_SELF_TEST:
		reg = AK09916C_MODE_SELF_TEST;
		ret = ak09916c_i2c_write(data->client, AK09916C_REG_CNTL2, reg);
		break;
	default:
		return -EINVAL;
	}

	if (ret < 0)
		return ret;

	/* Wait at least 300us after changing mode. */
	udelay(100);

	return 0;
}

static int ak09916c_read_mag_xyz(struct ak09916c_p *data,
	struct ak09916c_v *mag)
{
	u8 temp[SENSOR_DATA_SIZE] = {0, };
	int ret = 0, retries = 0;

	mutex_lock(&data->lock);
	ret = ak09916c_ecs_set_mode(data, AK09916C_MODE_SNG_MEASURE);
	if (ret < 0)
		goto exit_i2c_read_err;

again:
	ret = ak09916c_i2c_read(data->client, AK09916C_REG_ST1, &temp[0]);
	if (ret < 0)
		goto exit_i2c_read_err;

	/* Check ST bit */
	if (!(temp[0] & 0x01)) {
		if ((retries++ < 5) && (temp[0] == 0)) {
			goto again;
		} else {
			ret = -1;
			goto exit_i2c_read_err;
		}
	}

	ret = ak09916c_i2c_read_block(data->client, AK09916C_REG_ST1 + 1,
			&temp[1], SENSOR_DATA_SIZE - 1);
	if (ret < 0)
		goto exit_i2c_read_err;

	mag->x = temp[1] | (temp[2] << 8);
	mag->y = temp[3] | (temp[4] << 8);
	mag->z = temp[5] | (temp[6] << 8);

	remap_sensor_data(mag->v, data->chip_pos);

	goto exit;

exit_i2c_read_err:
	SENSOR_ERR("ST1 = %u, ST2 = %u\n", temp[0], temp[8]);
exit:
	mutex_unlock(&data->lock);
	return ret;
}

static enum hrtimer_restart ak09916c_timer_func(struct hrtimer *timer)
{
	struct ak09916c_p *data = container_of(timer,
					struct ak09916c_p, mag_timer);

	if (!work_pending(&data->mag_work))
		queue_work(data->wq, &data->mag_work);

	hrtimer_forward_now(&data->mag_timer, data->delay);

	return HRTIMER_RESTART;
}

static void ak09916c_work_func(struct work_struct *work)
{
	struct ak09916c_p *data = container_of(work,
			struct ak09916c_p, mag_work);
	struct ak09916c_v mag;
	struct timespec ts = ktime_to_timespec(ktime_get_boottime());
	u64 timestamp_new = ts.tv_sec * 1000000000ULL + ts.tv_nsec;
	int time_hi, time_lo;
	int ret;

#ifdef CONFIG_SENSORS_BHA250_DEFENCE_SW_RESET
	if (data->reset_state) {
		SENSOR_ERR("reset state (%d)\n", data->reset_state);
		return;
	}
#endif

	ret = ak09916c_read_mag_xyz(data, &mag);
	if (ret >= 0) {
		time_hi = (int)((timestamp_new & TIME_HI_MASK) >> TIME_HI_SHIFT);
		time_lo = (int)(timestamp_new & TIME_LO_MASK);

		input_report_rel(data->input, REL_X, mag.x);
		input_report_rel(data->input, REL_Y, mag.y);
		input_report_rel(data->input, REL_Z, mag.z);
		if (data->cal_reset_flag) {
			SENSOR_INFO("Magnetic cal reset!\n");
			input_report_rel(data->input, REL_RZ, data->cal_reset_flag);
			data->cal_reset_flag = 0;
		}
		input_report_rel(data->input, REL_RX, time_hi);
		input_report_rel(data->input, REL_RY, time_lo);
		input_sync(data->input);
		data->magdata = mag;
	}

	if ((ktime_to_ns(data->delay) * data->time_count)
			>= ((int64_t)MAG_LOG_TIME * NSEC_PER_SEC)) {
		SENSOR_INFO("x = %d, y = %d, z = %d\n", mag.x, mag.y, mag.z);
		data->time_count = 0;
	} else {
		data->time_count++;
	}

}

static void ak09916c_set_enable(struct ak09916c_p *data, int enable)
{
	int pre_enable = atomic_read(&data->enable);

	if (enable) {
		if (pre_enable == 0) {
			ak09916c_ecs_set_mode(data, AK09916C_MODE_SNG_MEASURE);
			hrtimer_start(&data->mag_timer, data->delay,
							HRTIMER_MODE_REL);
			atomic_set(&data->enable, 1);
		}
	} else {
		if (pre_enable == 1) {
			ak09916c_ecs_set_mode(data, AK09916C_MODE_POWERDOWN);
			cancel_work_sync(&data->mag_work);
			hrtimer_cancel(&data->mag_timer);
			atomic_set(&data->enable, 0);
		}
	}
}

static ssize_t ak09916c_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct ak09916c_p *data = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", atomic_read(&data->enable));
}

static ssize_t ak09916c_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	u8 enable;
	int ret;
	struct ak09916c_p *data = dev_get_drvdata(dev);

	ret = kstrtou8(buf, 2, &enable);
	if (ret) {
		SENSOR_ERR("Invalid Argument\n");
		return ret;
	}

	SENSOR_INFO("new_value = %u\n", enable);

#if defined(CONFIG_SENSORS_BHA250_DEFENCE_SW_RESET)
	if (data->reset_state) {
		SENSOR_INFO("sw reset come enable is = %u\n", enable);
		atomic_set(&data->enable, enable);
		return size;
	}
#endif
	mutex_lock(&data->enable_lock);
	if ((enable == 0) || (enable == 1))
		ak09916c_set_enable(data, (int)enable);

	mutex_unlock(&data->enable_lock);

	return size;
}

static ssize_t ak09916c_delay_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct ak09916c_p *data = dev_get_drvdata(dev);

	return snprintf(buf, 16, "%lld\n", ktime_to_ns(data->delay));
}

static ssize_t ak09916c_delay_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t size)
{
	int ret;
	int64_t delay;
	struct ak09916c_p *data = dev_get_drvdata(dev);

	ret = kstrtoll(buf, 10, &delay);
	if (ret) {
		SENSOR_ERR("Invalid Argument\n");
		return ret;
	}

	if (delay > AK09916C_MAX_DELAY) {
		SENSOR_INFO("%lld > AK09916C_MAX_DELAY\n", delay);
		delay = AK09916C_MAX_DELAY;
	} else if (delay < AK09916C_MIN_DELAY) {
		SENSOR_INFO("%lld < AK09916C_MAX_DELAY\n", delay);
		delay = AK09916C_MIN_DELAY;
	}

	mutex_lock(&data->enable_lock);
	data->delay = ns_to_ktime(delay);
	mutex_unlock(&data->enable_lock);
	SENSOR_INFO("poll_delay = %lld\n", ktime_to_ns(data->delay));

	return size;
}

static DEVICE_ATTR(poll_delay, S_IRUGO | S_IWUSR | S_IWGRP,
		ak09916c_delay_show, ak09916c_delay_store);
static DEVICE_ATTR(enable, S_IRUGO | S_IWUSR | S_IWGRP,
		ak09916c_enable_show, ak09916c_enable_store);

static struct attribute *ak09916c_attributes[] = {
	&dev_attr_poll_delay.attr,
	&dev_attr_enable.attr,
	NULL
};

static struct attribute_group ak09916c_attribute_group = {
	.attrs = ak09916c_attributes
};

static int ak09916c_selftest(struct ak09916c_p *data, int *dac_ret, int *sf)
{
	u8 temp[6], reg;
	s16 x, y, z;
	int retry_count = 0;
	int ready_count = 0;
	int ret;

	data->cal_reset_flag = 1;
retry:
	mutex_lock(&data->lock);
	/* power down */
	reg = AK09916C_MODE_POWERDOWN;
	*dac_ret = ak09916c_i2c_write(data->client, AK09916C_REG_CNTL2, reg);
	udelay(100);
	*dac_ret += ak09916c_i2c_read(data->client, AK09916C_REG_CNTL2, &reg);

	/* read device info */
	ak09916c_i2c_read_block(data->client, AK09916C_REG_WIA1, temp, 2);
	SENSOR_INFO("device id = 0x%x, info = 0x%x\n", temp[0], temp[1]);

	/* start self test */
	reg = AK09916C_MODE_SELF_TEST;
	ak09916c_i2c_write(data->client, AK09916C_REG_CNTL2, reg);

	/* wait for data ready */
	while (ready_count < 10) {
		usleep_range(20000, 21000);
		ret = ak09916c_i2c_read(data->client, AK09916C_REG_ST1, &reg);
		if ((reg == 1) && (ret == 0))
			break;
		ready_count++;
	}

	ak09916c_i2c_read_block(data->client, AK09916C_REG_HXL,
		temp, sizeof(temp));
	mutex_unlock(&data->lock);

	x = temp[0] | (temp[1] << 8);
	y = temp[2] | (temp[3] << 8);
	z = temp[4] | (temp[5] << 8);

	SENSOR_INFO("self test x = %d, y = %d, z = %d\n", x, y, z);
	if ((x >= -200) && (x <= 200))
		SENSOR_INFO("x passed self test, -200<=x<=200\n");
	else
		SENSOR_INFO("x failed self test, -200<=x<=200\n");
	if ((y >= -200) && (y <= 200))
		SENSOR_INFO("y passed self test, -200<=y<=200\n");
	else
		SENSOR_INFO("y failed self test, -200<=y<=200\n");
	if ((z >= -1000) && (z <= -200))
		SENSOR_INFO("z passed self test, -1000<=z<=-200\n");
	else
		SENSOR_INFO("z failed self test, -1000<=z<=-200\n");

	sf[0] = x;
	sf[1] = y;
	sf[2] = z;

	if (((x >= -200) && (x <= 200)) &&
		((y >= -200) && (y <= 200)) &&
		((z >= -1000) && (z <= -200))) {
		SENSOR_INFO("successful.\n");
		ret = 0;
	} else {
		if (retry_count < 5) {
			retry_count++;
			pr_warn("############################################");
			pr_warn("%s, retry_count=%d\n", __func__, retry_count);
			pr_warn("############################################");
			goto retry;
		} else {
			SENSOR_ERR("failed.\n");
			ret = -1;
		}
	}
	return ret;
}

static ssize_t ak09916c_vendor_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", VENDOR_NAME);
}

static ssize_t ak09916c_name_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", MODEL_NAME);
}

static ssize_t ak09916c_get_asa(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct ak09916c_p *data  = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%u,%u,%u\n",
			data->asa[0], data->asa[1], data->asa[2]);
}

static ssize_t ak09916c_dhr_sensor_info_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int i = 0;
	struct ak09916c_p *data = dev_get_drvdata(dev);

	if (sscanf(buf,
		"%10u %10u %10u %10u %10u %10u %10u %10u %10u "\
		"%10u %10u %10u %10u %10u %10u %10u %10u %10u "\
		"%10u %10u %10u %10u %10u %10u %10u %10u %10u",
		&data->si_p[0], &data->si_p[1], &data->si_p[2], &data->si_p[3],
		&data->si_p[4], &data->si_p[5], &data->si_p[6], &data->si_p[7],
		&data->si_p[8], &data->si_p[9], &data->si_p[10], &data->si_p[11],
		&data->si_p[12], &data->si_p[13], &data->si_p[14], &data->si_p[15],
		&data->si_p[16], &data->si_p[17], &data->si_p[18], &data->si_p[19],
		&data->si_p[20], &data->si_p[21], &data->si_p[22], &data->si_p[23],
		&data->si_p[24], &data->si_p[25], &data->si_p[26]) != 27) {
		SENSOR_ERR("The number of data are wrong\n");
		for (i = 0; i < 27; i++)
			data->si_p[i] = 0;
	}

	return size;
}

static ssize_t ak09916c_dhr_sensor_info_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ak09916c_p *data = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE,
		"\"SI_PARAMETER\":\""\
		"%u %u %u %u %u %u %u %u %u "\
		"%u %u %u %u %u %u %u %u %u "\
		"%u %u %u %u %u %u %u %u %u\"\n",
		data->si_p[0], data->si_p[1], data->si_p[2], data->si_p[3],
		data->si_p[4], data->si_p[5], data->si_p[6], data->si_p[7],
		data->si_p[8], data->si_p[9], data->si_p[10], data->si_p[11],
		data->si_p[12], data->si_p[13], data->si_p[14], data->si_p[15],
		data->si_p[16], data->si_p[17], data->si_p[18], data->si_p[19],
		data->si_p[20], data->si_p[21], data->si_p[22], data->si_p[23],
		data->si_p[24], data->si_p[25], data->si_p[26]);
}

static ssize_t ak09916c_get_selftest(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int status, dac_ret = -1, adc_ret = -1;
	int sf_ret, sf[3] = {0,}, retries;
	struct ak09916c_v mag;
	struct ak09916c_p *data = dev_get_drvdata(dev);

	/* STATUS */
	if ((data->asa[0] == 0) | (data->asa[0] == 0xff)
		| (data->asa[1] == 0) | (data->asa[1] == 0xff)
		| (data->asa[2] == 0) | (data->asa[2] == 0xff))
		status = -1;
	else
		status = 0;

	if (atomic_read(&data->enable) == 1) {
		ak09916c_ecs_set_mode(data, AK09916C_MODE_POWERDOWN);
		cancel_work_sync(&data->mag_work);
		hrtimer_cancel(&data->mag_timer);
	}

	sf_ret = ak09916c_selftest(data, &dac_ret, sf);

	for (retries = 0; retries < 5; retries++) {
		if (ak09916c_read_mag_xyz(data, &mag) == 0) {
			if ((mag.x < 6500) && (mag.x > -6500)
					&& (mag.y < 6500) && (mag.y > -6500)
					&& (mag.z < 6500) && (mag.z > -6500))
				adc_ret = 0;
			else
				SENSOR_ERR("adc specout %d, %d, %d\n",
						mag.x, mag.y, mag.z);
			break;
		}

		usleep_range(20000, 21000);
		SENSOR_ERR("adc retries %d", retries);
	}

	if (atomic_read(&data->enable) == 1) {
		ak09916c_ecs_set_mode(data, AK09916C_MODE_SNG_MEASURE);
		hrtimer_start(&data->mag_timer, data->delay,
						HRTIMER_MODE_REL);
	}

	return snprintf(buf, PAGE_SIZE, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
			status, sf_ret, sf[0], sf[1], sf[2], dac_ret,
			adc_ret, mag.x, mag.y, mag.z);
}

static ssize_t ak09916c_check_registers(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u8 temp[13], reg;
	struct ak09916c_p *data = dev_get_drvdata(dev);

	mutex_lock(&data->lock);
	/* power down */
	reg = AK09916C_MODE_POWERDOWN;
	ak09916c_i2c_write(data->client, AK09916C_REG_CNTL2, reg);
	/* get the value */
	ak09916c_i2c_read_block(data->client, AK09916C_REG_WIA1, temp, 13);

	mutex_unlock(&data->lock);

	return snprintf(buf, PAGE_SIZE,
			"%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
			temp[0], temp[1], temp[2], temp[3], temp[4], temp[5],
			temp[6], temp[7], temp[8], temp[9], temp[10], temp[11],
			temp[12]);
}

static ssize_t ak09916c_check_cntl(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u8 reg;
	int ret = 0;
	struct ak09916c_p *data = dev_get_drvdata(dev);

	mutex_lock(&data->lock);
	/* power down */
	reg = AK09916C_MODE_POWERDOWN;

	ret = ak09916c_i2c_write(data->client, AK09916C_REG_CNTL2, reg);
	udelay(100);
	ret += ak09916c_i2c_read(data->client, AK09916C_REG_CNTL2, &reg);
	mutex_unlock(&data->lock);

	return snprintf(buf, PAGE_SIZE, "%s\n",
		(((reg == AK09916C_MODE_POWERDOWN) &&
			(ret == 0)) ? "OK" : "NG"));
}

static ssize_t ak09916c_get_status(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	bool success;
	struct ak09916c_p *data = dev_get_drvdata(dev);

	if ((data->asa[0] == 0) | (data->asa[0] == 0xff)
		| (data->asa[1] == 0) | (data->asa[1] == 0xff)
		| (data->asa[2] == 0) | (data->asa[2] == 0xff))
		success = false;
	else
		success = true;

	return snprintf(buf, PAGE_SIZE, "%s\n", (success ? "OK" : "NG"));
}

static ssize_t ak09916c_adc(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	bool success = false;
	int ret;
	struct ak09916c_p *data = dev_get_drvdata(dev);
	struct ak09916c_v mag = data->magdata;

	if (atomic_read(&data->enable) == 1) {
		success = true;
		usleep_range(20000, 21000);
		goto exit;
	}

	ret = ak09916c_read_mag_xyz(data, &mag);
	if (ret < 0)
		success = false;
	else
		success = true;

	data->magdata = mag;

exit:
	return snprintf(buf, PAGE_SIZE, "%s,%d,%d,%d\n",
			(success ? "OK" : "NG"), mag.x, mag.y, mag.z);
}

static ssize_t ak09916c_raw_data_read(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ak09916c_p *data = dev_get_drvdata(dev);
	struct ak09916c_v mag = data->magdata;

	if (atomic_read(&data->enable) == 1) {
		usleep_range(20000, 21000);
		goto exit;
	}

	ak09916c_read_mag_xyz(data, &mag);
	data->magdata = mag;

exit:
	return snprintf(buf, PAGE_SIZE, "%d,%d,%d\n", mag.x, mag.y, mag.z);
}

static ssize_t ak09916c_raw_data_write(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	u8 enable;
	int ret;
	struct ak09916c_p *data = dev_get_drvdata(dev);

	ret = kstrtou8(buf, 2, &enable);
	if (ret) {
		SENSOR_ERR("Invalid Argument\n");
		return size;
	}

	SENSOR_INFO("%u\n", enable);
	if (enable == 1)
		data->cal_reset_flag = 1;

	return size;
}

static int ak09916c_check_device(struct ak09916c_p *data)
{
	unsigned char reg, buf[2];
	int ret;

	ret = ak09916c_i2c_read_block(data->client, AK09916C_REG_WIA1, buf, 2);
	if (ret < 0) {
		SENSOR_ERR("unable to read AK09916C_REG_WIA1\n");
		return ret;
	}

	reg = AK09916C_MODE_POWERDOWN;
	ret = ak09916c_i2c_write(data->client, AK09916C_REG_CNTL2, reg);
	if (ret < 0) {
		SENSOR_ERR("Error in setting power down mode\n");
		return ret;
	}

	if ((buf[0] != AK09916C_WIA1_VALUE)
		|| (buf[1] != AK09916C_WIA2_VALUE)) {
		SENSOR_ERR("Wrong Device. Value = %u, %u\n", buf[0], buf[1]);
		return -ENXIO;
	}

	return 0;
}

#ifdef CONFIG_SENSORS_BHA250_DEFENCE_SW_RESET
static ssize_t ak09916c_power_reset_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct ak09916c_p *data = dev_get_drvdata(dev);
	int enabled = atomic_read(&data->enable);

	SENSOR_INFO("magnetic power reset start!!!\n");

	data->reset_state = 1;
	mutex_lock(&data->enable_lock);

	if (enabled) {
		cancel_work_sync(&data->mag_work);
		hrtimer_cancel(&data->mag_timer);
		SENSOR_INFO("cancel_delayed_work_sync done!!!\n");
	}

	mutex_unlock(&data->enable_lock);

	SENSOR_INFO("magnetic power reset end!!!\n");

	return snprintf(buf, PAGE_SIZE, "%d\n", enabled);
}


static ssize_t ak09916c_sw_reset_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct ak09916c_p *data = dev_get_drvdata(dev);
	int enabled = atomic_read(&data->enable);

	SENSOR_INFO("magnetic sw reset start!!!\n");

	mutex_lock(&data->enable_lock);
	data->reset_state = 0;
	if (enabled == 1) {
		SENSOR_INFO("magnetic was enabled so make enable!!!\n");
		ak09916c_ecs_set_mode(data, AK09916C_MODE_SNG_MEASURE);
		hrtimer_start(&data->mag_timer, data->delay,
						HRTIMER_MODE_REL);
	} else {
		SENSOR_INFO("magnetic was disabled so make disable!!!\n");
		ak09916c_ecs_set_mode(data, AK09916C_MODE_POWERDOWN);
	}

	mutex_unlock(&data->enable_lock);

	SENSOR_INFO("magnetic sw reset end!!!\n");

	return snprintf(buf, PAGE_SIZE, "%d\n", enabled);
}
#endif

static DEVICE_ATTR(name, S_IRUGO, ak09916c_name_show, NULL);
static DEVICE_ATTR(vendor, S_IRUGO, ak09916c_vendor_show, NULL);
static DEVICE_ATTR(raw_data, S_IRUGO | S_IWUSR | S_IWGRP,
	ak09916c_raw_data_read, ak09916c_raw_data_write);
static DEVICE_ATTR(adc, S_IRUGO, ak09916c_adc, NULL);
static DEVICE_ATTR(dac, S_IRUGO, ak09916c_check_cntl, NULL);
static DEVICE_ATTR(chk_registers, S_IRUGO, ak09916c_check_registers, NULL);
static DEVICE_ATTR(selftest, S_IRUGO, ak09916c_get_selftest, NULL);
static DEVICE_ATTR(asa, S_IRUGO, ak09916c_get_asa, NULL);
static DEVICE_ATTR(status, S_IRUGO, ak09916c_get_status, NULL);
#ifdef CONFIG_SENSORS_BHA250_DEFENCE_SW_RESET
static DEVICE_ATTR(power_reset, S_IRUSR | S_IRGRP,
	ak09916c_power_reset_show, NULL);
static DEVICE_ATTR(sw_reset, S_IRUSR | S_IRGRP,
	ak09916c_sw_reset_show, NULL);
#endif
static DEVICE_ATTR(dhr_sensor_info, S_IRUGO | S_IWUSR | S_IWGRP,
	ak09916c_dhr_sensor_info_show, ak09916c_dhr_sensor_info_store);

static struct device_attribute *sensor_attrs[] = {
	&dev_attr_name,
	&dev_attr_vendor,
	&dev_attr_raw_data,
	&dev_attr_adc,
	&dev_attr_dac,
	&dev_attr_chk_registers,
	&dev_attr_selftest,
	&dev_attr_asa,
	&dev_attr_status,
#ifdef CONFIG_SENSORS_BHA250_DEFENCE_SW_RESET
	&dev_attr_power_reset,
	&dev_attr_sw_reset,
#endif
	&dev_attr_dhr_sensor_info,
	NULL,
};

static int ak09916c_input_init(struct ak09916c_p *data)
{
	int ret = 0;
	struct input_dev *dev;

	dev = input_allocate_device();
	if (!dev)
		return -ENOMEM;

	dev->name = MODULE_NAME;
	dev->id.bustype = BUS_I2C;

	input_set_capability(dev, EV_REL, REL_X);
	input_set_capability(dev, EV_REL, REL_Y);
	input_set_capability(dev, EV_REL, REL_Z);
	input_set_capability(dev, EV_REL, REL_RZ); /* cal reset flag */
	input_set_capability(dev, EV_REL, REL_RX); /* time_hi */
	input_set_capability(dev, EV_REL, REL_RY); /* time_lo */
	input_set_drvdata(dev, data);

	ret = input_register_device(dev);
	if (ret < 0) {
		input_free_device(dev);
		return ret;
	}

	ret = sensors_create_symlink(&dev->dev.kobj, dev->name);
	if (ret < 0) {
		input_unregister_device(dev);
		return ret;
	}

	/* sysfs node creation */
	ret = sysfs_create_group(&dev->dev.kobj, &ak09916c_attribute_group);
	if (ret < 0) {
		sensors_remove_symlink(&data->input->dev.kobj,
			data->input->name);
		input_unregister_device(dev);
		return ret;
	}

	data->input = dev;
	return 0;
}

static int ak09916c_vdd_onoff(struct ak09916c_p *data, int onoff)
{
	/* ldo control */
	if (data->ak09916c_ldo_pin) {
		gpio_set_value(data->ak09916c_ldo_pin, onoff);
		if (onoff)
			msleep(20);
		return 0;
	}

	return 0;
}

static int ak09916c_parse_dt(struct ak09916c_p *data, struct device *dev)
{
	struct device_node *d_node = dev->of_node;
	enum of_gpio_flags flags;
	int ret = 0;

	if (d_node == NULL)
		return -ENODEV;

	data->ak09916c_ldo_pin = of_get_named_gpio_flags(d_node,
				"ak09916c,vdd_ldo_pin", 0, &flags);
	if (data->ak09916c_ldo_pin < 0) {
		SENSOR_INFO("Cannot set vdd_ldo_pin through DTSI\n");
		data->ak09916c_ldo_pin = 0;
	} else {
		ret = gpio_request(data->ak09916c_ldo_pin,
			"ak09916c,vdd_ldo_pin");
		if (ret < 0)
			SENSOR_ERR("gpio %d request failed %d\n",
				data->ak09916c_ldo_pin, ret);
		else
			gpio_direction_output(data->ak09916c_ldo_pin, 0);
	}


	if (of_property_read_u32(d_node,
			"ak09916c-i2c,chip_pos", &data->chip_pos) < 0)
		data->chip_pos = AK09916C_TOP_LOWER_RIGHT;

	return 0;
}

static int ak09916c_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	int ret = -ENODEV;
	struct ak09916c_p *data = NULL;

	SENSOR_INFO("Start!\n");

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		SENSOR_ERR("i2c_check_functionality error\n");
		goto exit;
	}

	data = kzalloc(sizeof(struct ak09916c_p), GFP_KERNEL);
	if (data == NULL) {
		SENSOR_ERR("kzalloc error\n");
		ret = -ENOMEM;
		goto exit_kzalloc;
	}

	ret = ak09916c_parse_dt(data, &client->dev);
	if (ret < 0) {
		SENSOR_ERR("of_node error\n");
		goto exit_of_node;
	}

	ak09916c_vdd_onoff(data, ON);

	i2c_set_clientdata(client, data);
	data->client = client;

	ret = ak09916c_check_device(data);
	if (ret < 0) {
		SENSOR_ERR("check_device fail (err=%d)\n", ret);
		goto exit_set_mode_check_device;
	}

	/* input device init */
	ret = ak09916c_input_init(data);
	if (ret < 0)
		goto exit_input_init;

	sensors_register(&data->factory_device, data, sensor_attrs,
		MODULE_NAME);

	/* workqueue init */
	hrtimer_init(&data->mag_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	data->delay = ns_to_ktime(AK09916C_DEFAULT_DELAY);
	data->mag_timer.function = ak09916c_timer_func;

	data->wq = create_singlethread_workqueue("mag_wq");
	if (!data->wq) {
		ret = -ENOMEM;
		SENSOR_ERR("could not create accel workqueue\n");
		goto exit_input_init;
	}

	INIT_WORK(&data->mag_work, ak09916c_work_func);

	mutex_init(&data->lock);
#ifdef CONFIG_SENSORS_BHA250_DEFENCE_SW_RESET
	data->reset_state = 0;
#endif
	mutex_init(&data->enable_lock);

	atomic_set(&data->enable, 0);

	data->asa[0] = 128;
	data->asa[1] = 128;
	data->asa[2] = 128;
	data->cal_reset_flag = 0;

	SENSOR_INFO("done!(chip pos : %d)\n", data->chip_pos);

	return 0;

exit_input_init:
exit_set_mode_check_device:
	if (data->ak09916c_ldo_pin)
		gpio_free(data->ak09916c_ldo_pin);
exit_of_node:
	kfree(data);
exit_kzalloc:
exit:
	SENSOR_ERR("fail\n");
	return ret;
}

static void ak09916c_shutdown(struct i2c_client *client)
{
	struct ak09916c_p *data =
			(struct ak09916c_p *)i2c_get_clientdata(client);

	SENSOR_INFO("\n");
	if (atomic_read(&data->enable) == 1) {
		ak09916c_ecs_set_mode(data, AK09916C_MODE_POWERDOWN);
		cancel_work_sync(&data->mag_work);
		hrtimer_cancel(&data->mag_timer);
	}
}

static int ak09916c_remove(struct i2c_client *client)
{
	SENSOR_INFO("\n");
	return 0;
}

static int ak09916c_suspend(struct device *dev)
{
	struct ak09916c_p *data = dev_get_drvdata(dev);

	if (atomic_read(&data->enable) == 1) {
		cancel_work_sync(&data->mag_work);
		hrtimer_cancel(&data->mag_timer);
		ak09916c_ecs_set_mode(data, AK09916C_MODE_POWERDOWN);
	}

	return 0;
}

static int ak09916c_resume(struct device *dev)
{
	struct ak09916c_p *data = dev_get_drvdata(dev);

	if (atomic_read(&data->enable) == 1) {
		ak09916c_ecs_set_mode(data, AK09916C_MODE_SNG_MEASURE);
		hrtimer_start(&data->mag_timer, data->delay,
						HRTIMER_MODE_REL);
	}

	return 0;
}

static struct of_device_id ak09916c_match_table[] = {
	{ .compatible = "ak09916c-i2c",},
	{},
};

static const struct i2c_device_id ak09916c_id[] = {
	{ "ak09916c_match_table", 0 },
	{ }
};

static const struct dev_pm_ops ak09916c_pm_ops = {
	.suspend = ak09916c_suspend,
	.resume = ak09916c_resume
};

static struct i2c_driver ak09916c_driver = {
	.driver = {
		.name	= MODEL_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = ak09916c_match_table,
		.pm = &ak09916c_pm_ops
	},
	.probe		= ak09916c_probe,
	.shutdown	= ak09916c_shutdown,
	.remove		= ak09916c_remove,
	.id_table	= ak09916c_id,
};

static int __init ak09916c_init(void)
{
	return i2c_add_driver(&ak09916c_driver);
}

static void __exit ak09916c_exit(void)
{
	i2c_del_driver(&ak09916c_driver);
}

module_init(ak09916c_init);
module_exit(ak09916c_exit);

MODULE_DESCRIPTION("AK09916C compass driver");
MODULE_AUTHOR("Samsung Electronics");
MODULE_LICENSE("GPL");
