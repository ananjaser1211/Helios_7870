/*
 * Samsung Exynos5 SoC series FIMC-IS OIS driver
 *
 * exynos5 fimc-is core functions
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/vmalloc.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>
#include <exynos-fimc-is-sensor.h>
#include <linux/pinctrl/pinctrl.h>

#include "fimc-is-core.h"
#include "fimc-is-interface.h"
#include "fimc-is-sec-define.h"
#include "fimc-is-device-ischain.h"
#include "fimc-is-dt.h"
#include "fimc-is-device-ois.h"
#include "fimc-is-device-ois_bu24219.h"
#include "fimc-is-vender-specific.h"
#ifdef CONFIG_AF_HOST_CONTROL
#include "fimc-is-device-af.h"
#endif

extern struct fimc_is_ois_info ois_minfo;
extern struct fimc_is_ois_info ois_pinfo;
extern struct fimc_is_ois_info ois_uinfo;
extern struct fimc_is_ois_exif ois_exif_data;

static char ois_buf[FIMC_IS_MAX_OIS_SIZE];
static int ois_fw_base_addr;
static u32 checksum_fw_backup;
#if defined(CONFIG_SEC_FACTORY)
bool is_factory_mode = true;
#else
bool is_factory_mode = false;
#endif

static int OIS_Shift_X[512] = {0,};
static int OIS_Shift_Y[512] = {0,};

int fimc_is_ois_gpio_on_impl(struct fimc_is_core *core)
{
	int ret = 0;
	struct exynos_platform_fimc_is_module *module_pdata;
	struct fimc_is_module_enum *module = NULL;
	struct fimc_is_vender_specific *specific = core->vender.private_data;
	struct device *dev = &core->ischain[0].pdev->dev;

	int sensor_id = 0;
	int i = 0;

	sensor_id = specific->rear_sensor_id;

	for (i = 0; i < FIMC_IS_SENSOR_COUNT; i++) {
		fimc_is_search_sensor_module(&core->sensor[i], sensor_id, &module);
		if (module)
			break;
		else {
			err("%s: Could not find sensor id.", __func__);
			ret = -EINVAL;
			goto p_err;
		}
	}

	ret = fimc_is_sec_run_fw_sel(dev, SENSOR_POSITION_REAR);
	if (ret) {
		err("fimc_is_sec_run_fw_sel for rear is fail(%d)", ret);
	}

	module_pdata = module->pdata;

	if (!module_pdata->gpio_cfg) {
		err("gpio_cfg is NULL");
		ret = -EINVAL;
		goto p_err;
	}

	ret = module_pdata->gpio_cfg(module->dev, SENSOR_SCENARIO_OIS_FACTORY, GPIO_SCENARIO_ON);
	if (ret) {
		err("gpio_cfg is fail(%d)", ret);
		goto p_err;
	}

#if !defined(CONFIG_SEC_FACTORY)
	is_factory_mode = true; // For Gyro self tet of SENSOR in *#0*# test mode
#endif

	fimc_is_ois_fw_update(core);

#if !defined(CONFIG_SEC_FACTORY)
	is_factory_mode = false; // For Gyro self tet of SENSOR in *#0*# test mode
#endif

p_err:
	return ret;
}

int fimc_is_ois_gpio_off_impl(struct fimc_is_core *core)
{
	int ret = 0;
	struct exynos_platform_fimc_is_module *module_pdata;
	struct fimc_is_module_enum *module = NULL;
	struct fimc_is_vender_specific *specific = core->vender.private_data;
	int sensor_id = 0;
	int i = 0;

	sensor_id = specific->rear_sensor_id;

	for (i = 0; i < FIMC_IS_SENSOR_COUNT; i++) {
		fimc_is_search_sensor_module(&core->sensor[i], sensor_id, &module);
		if (module)
			break;
		else {
			err("%s: Could not find sensor id.", __func__);
			ret = -EINVAL;
			goto p_err;
		}
	}

	module_pdata = module->pdata;

	if (!module_pdata->gpio_cfg) {
		err("gpio_cfg is NULL");
		ret = -EINVAL;
		goto p_err;
	}

	ret = module_pdata->gpio_cfg(module->dev, SENSOR_SCENARIO_OIS_FACTORY, GPIO_SCENARIO_OFF);
	if (ret) {
		err("gpio_cfg is fail(%d)", ret);
		goto p_err;
	}

p_err:
	return ret;
}

int fimc_is_ois_sine_mode_impl(struct fimc_is_core *core, int mode)
{
/* sine test will be implemented by ISP firmware  */
	return 0;
}

void fimc_is_ois_version(struct fimc_is_core *core)
{
/* Do Nothing */
}

void fimc_is_ois_offset_test_impl(struct fimc_is_core *core, long *raw_data_x, long *raw_data_y)
{
	int ret = 0, i = 0;
	u8 val = 0;
	u8 m_val[2] = {0,};
	int scale_factor = 0;
	int16_t x_sum = 0, y_sum = 0, sum = 0;
	uint16_t x_gyro = 0, y_gyro = 0;
	int retries = 10, retry = 10, avg_count = 3;
	struct exynos_platform_fimc_is *core_pdata = NULL;

	core_pdata = dev_get_platdata(fimc_is_dev);
	if (!core_pdata) {
		err("core->pdata is null");
		return;
	}

	info("%s : E\n", __FUNCTION__);

	if(fimc_is_ois_cal_revision(ois_minfo.cal_ver) < 4)
		scale_factor = OIS_GYRO_SCALE_FACTOR_V003;
	else
		scale_factor = OIS_GYRO_SCALE_FACTOR_V004;

	ret = fimc_is_ois_i2c_write(core->client1, 0x6020, 0x01);  // OIS Servo ON
	if (ret) {
		err("i2c write fail\n");
	}

	msleep(100);

	do {
		ret = fimc_is_ois_i2c_read(core->client1, 0x6024, &val);
		if (ret != 0) {
			break;
		}
		msleep(10);
		if (--retries < 0) {
			err("Read register failed!!!!, data = 0x%04x\n", val);
			break;
		}
	} while (val != 1);

	ret = fimc_is_ois_i2c_write(core->client1, 0x6023, 0x00);  // Set Gyro ON for OIS
	if (ret) {
		err("i2c write fail\n");
	}

	retries = avg_count;
	for (i = 1; i <= retries; i++) {  // Process avg
		fimc_is_ois_i2c_write(core->client1, 0x6088, 0x00);  // X Offset

		retry = avg_count;
		do {
			ret = fimc_is_ois_i2c_read(core->client1, 0x6024, &val);
			if (ret != 0) {
				break;
			}
			msleep(10);
			if (--retry < 0) {
				err("Read register failed!!!!, data = 0x%04x\n", val);
				break;
			}
		} while (val != 1);

		ret = fimc_is_ois_i2c_read_multi(core->client1, 0x608A, m_val, 2);

		x_gyro = (m_val[0] << 8) + m_val[1];
		info("OIS[x_gyro - read_val_0x608A::0x%04x]\n", x_gyro);

		sum = *((int16_t *)(&x_gyro));
		x_sum = x_sum + sum;
		info("OIS[x_sum - read_val_0x608A::0x%04x]\n", x_sum);

		fimc_is_ois_i2c_write(core->client1, 0x6088, 0x01);  // Y Offset

		retry = avg_count;
		do {
			ret = fimc_is_ois_i2c_read(core->client1, 0x6024, &val);
			if (ret != 0) {
				break;
			}
			msleep(10);
			if (--retry < 0) {
				err("Read register failed!!!!, data = 0x%04x\n", val);
				break;
			}
		} while (val != 1);

		ret = fimc_is_ois_i2c_read_multi(core->client1, 0x608A, m_val, 2);

		y_gyro = (m_val[0] << 8) + m_val[1];
		info("OIS[y_gyro - read_val_0x608A::0x%04x]\n", y_gyro);

		sum = *((int16_t *)(&y_gyro));
		y_sum = y_sum + sum;
		info("OIS[y_sum - read_val_0x608A::0x%04x]\n", y_sum);
	}

	*raw_data_x = x_sum * 1000 / avg_count / scale_factor;
	*raw_data_y = y_sum * 1000 / avg_count / scale_factor;

	info("%s : X\n", __FUNCTION__);
	return;
}

void fimc_is_ois_get_offset_data_impl(struct fimc_is_core *core, long *raw_data_x, long *raw_data_y)
{
	char *buf = NULL;
	uint16_t x_temp = 0, y_temp = 0;
	int16_t x_gyro = 0, y_gyro = 0;
	struct exynos_platform_fimc_is *core_pdata = NULL;

	core_pdata = dev_get_platdata(fimc_is_dev);
	if (!core_pdata) {
		err("core->pdata is null");
		return;
	}

	info("%s : E\n", __FUNCTION__);

	fimc_is_sec_get_cal_buf(&buf);

	x_temp = (buf[OIS_FW_EEPROM_BASE_ADDR + OIS_GYRO_X_ADDR_OFFSET] << 8)
		+ buf[OIS_FW_EEPROM_BASE_ADDR + OIS_GYRO_X_ADDR_OFFSET + 1];
	x_gyro = *((int16_t *)(&x_temp));

	y_temp = (buf[OIS_FW_EEPROM_BASE_ADDR + OIS_GYRO_Y_ADDR_OFFSET] << 8)
		+ buf[OIS_FW_EEPROM_BASE_ADDR + OIS_GYRO_Y_ADDR_OFFSET + 1];
	y_gyro = *((int16_t *)(&y_temp));

	*raw_data_x = x_gyro;
	*raw_data_y = y_gyro;

	info("%s : X\n", __FUNCTION__);
	return;
}

int fimc_is_ois_self_test_impl(struct fimc_is_core *core)
{
	int ret = 0;
	u8 val = 0;
	int retries = 20;
	struct exynos_platform_fimc_is *core_pdata = NULL;

	core_pdata = dev_get_platdata(fimc_is_dev);
	if (!core_pdata) {
		err("core->pdata is null");
		return -EINVAL;
	}

	info("%s : E\n", __FUNCTION__);

	ret = fimc_is_ois_i2c_write(core->client1, 0x6020, 0x04);  // Calibration Mode
	if (ret) {
		err("i2c write fail\n");
	}

	msleep(10);

	ret = fimc_is_ois_i2c_write(core->client1, 0x6023, 0x02);
	if (ret) {
		err("i2c write fail\n");
	}

	msleep(10);

	ret = fimc_is_ois_i2c_write(core->client1, 0x6138, 0x00);
	if (ret) {
		err("i2c write fail\n");
	}

	msleep(100);

	do {
		ret = fimc_is_ois_i2c_read(core->client1, 0x6024, &val);
		if (ret != 0) {
			break;
		}
		msleep(10);
		if (--retries < 0) {
			err("Read register failed!!!!, data = 0x%04x\n", val);
			break;
		}
	} while (val != 1);

	ret = fimc_is_ois_i2c_read(core->client1, 0x6139, &val);
	if (ret != 0) {
		val = -EIO;
	}

	info("%s(%d) : X\n", __FUNCTION__, val);
	if (val == 3) {
		return 0;
	} else {
		return 1;
	}
}

bool fimc_is_ois_diff_test_impl(struct fimc_is_core *core, int *x_diff, int *y_diff)
{
	int ret = 0;
	int hall_adjust_factor = 0;
	char *buf = NULL;
	u8 m_val[2] = {0,};
	u8 pos_val[2] = {0,};
	u8 neg_val[2] = {0,};
	u16 x_min = 0, y_min = 0, x_max = 0, y_max = 0;
	int default_diff = 900;
	struct exynos_platform_fimc_is *core_pdata = NULL;

	core_pdata = dev_get_platdata(fimc_is_dev);
	if (!core_pdata) {
		err("core->pdata is null");
		return false;
	}

#ifdef CONFIG_AF_HOST_CONTROL
	fimc_is_af_move_lens(core);
	msleep(100);
#endif

	info("(%s) : E\n", __FUNCTION__);

	fimc_is_sec_get_cal_buf(&buf);

	hall_adjust_factor = *((u8 *)(&buf[0x2060])) >> 4;
	info("OIS hall_adjust_factor [read_val_0x2060::%d]\n", hall_adjust_factor);

	if (hall_adjust_factor == 1) {
		pos_val[0] = 0x01;
		pos_val[1] = 0x00;
		neg_val[0] = 0xFF;
		neg_val[1] = 0x00;
	} else if (hall_adjust_factor == 2) {
		pos_val[0] = 0x02;
		pos_val[1] = 0x00;
		neg_val[0] = 0xFE;
		neg_val[1] = 0x00;
	} else if (hall_adjust_factor == 3) {
		pos_val[0] = 0x03;
		pos_val[1] = 0x00;
		neg_val[0] = 0xFD;
		neg_val[1] = 0x00;
	} else if (hall_adjust_factor == 4) {
		pos_val[0] = 0x04;
		pos_val[1] = 0x00;
		neg_val[0] = 0xFC;
		neg_val[1] = 0x00;
	} else if (hall_adjust_factor == 5) {
		pos_val[0] = 0x05;
		pos_val[1] = 0x00;
		neg_val[0] = 0xFB;
		neg_val[1] = 0x00;
	}

	ret = fimc_is_ois_i2c_write(core->client1, 0x6020, 0x04);  // Calibration Mode
	if (ret) {
		err("i2c write fail\n");
	}

	ret = fimc_is_ois_i2c_write_multi(core->client1, 0x6064, pos_val, 2);
	if (ret) {
		err("i2c write fail\n");
	}

	msleep(100);

	ret = fimc_is_ois_i2c_write(core->client1, 0x6060, 0x00);
	if (ret) {
		err("i2c write fail\n");
	}

	ret = fimc_is_ois_i2c_read_multi(core->client1, 0x6062, m_val, 2);
	x_max = (m_val[0] << 8) + m_val[1];
	info("OIS[read_val_0x6062::0x%08x]\n", x_max);

	ret = fimc_is_ois_i2c_write_multi(core->client1, 0x6064, neg_val, 2);
	if (ret) {
		err("i2c write fail\n");
	}

	msleep(100);

	ret = fimc_is_ois_i2c_write(core->client1, 0x6060, 0x00);
	if (ret) {
		err("i2c write fail\n");
	}

	fimc_is_ois_i2c_read_multi(core->client1, 0x6062, m_val, 2);
	x_min = (m_val[0] << 8) + m_val[1];
	info("OIS[read_val_0x6062::0x%08x]\n", x_min);

	m_val[0] = 0x00;
	m_val[1] = 0x00;
	ret = fimc_is_ois_i2c_write_multi(core->client1, 0x6064, m_val, 2);
	if (ret) {
		err("i2c write fail\n");
	}

	msleep(100);

	ret = fimc_is_ois_i2c_write_multi(core->client1, 0x6066, pos_val, 2);
	if (ret) {
		err("i2c write fail\n");
	}

	msleep(100);

	ret = fimc_is_ois_i2c_write(core->client1, 0x6060, 0x01);
	if (ret) {
		err("i2c write fail\n");
	}

	fimc_is_ois_i2c_read_multi(core->client1, 0x6062, m_val, 2);
	y_max = (m_val[0] << 8) + m_val[1];
	info("OIS[read_val_0x6062::0x%08x]\n", y_max);

	ret = fimc_is_ois_i2c_write_multi(core->client1, 0x6066, neg_val, 2);
	if (ret) {
		err("i2c write fail\n");
	}

	msleep(100);

	ret = fimc_is_ois_i2c_write(core->client1, 0x6060, 0x01);
	if (ret) {
		err("i2c write fail\n");
	}

	fimc_is_ois_i2c_read_multi(core->client1, 0x6062, m_val, 2);
	y_min = (m_val[0] << 8) + m_val[1];
	info("OIS[read_val_0x6062::0x%08x]\n", y_min);

	m_val[0] = 0x00;
	m_val[1] = 0x00;
	ret = fimc_is_ois_i2c_write_multi(core->client1, 0x6066, m_val, 2);
	if (ret) {
		err("i2c write fail\n");
	}

	msleep(100);

	ret = fimc_is_ois_i2c_write(core->client1, 0x6020, 0x00);  // Standby Mode
	if (ret) {
		err("i2c write fail\n");
	}

	*x_diff = abs(x_max - x_min);
	*y_diff = abs(y_max - y_min);

	info("(%s) : X (default_diff:%d)(%d,%d)\n", __FUNCTION__,
			default_diff, *x_diff, *y_diff);

	if (*x_diff > default_diff	&& *y_diff > default_diff) {
		return true;
	} else {
		return false;
	}
}

void fimc_is_ois_fw_version(struct fimc_is_core *core)
{
/* Do Nothing */
}

int fimc_is_ois_fw_revision(char *fw_ver)
{
	int revision = 0;
	revision = revision + ((int)fw_ver[FW_RELEASE_YEAR] - 64) * 1000;
	revision = revision + ((int)fw_ver[FW_RELEASE_MONTH] - 64) * 100;
	revision = revision + ((int)fw_ver[FW_RELEASE_COUNT] - 48) * 10;
	revision = revision + (int)fw_ver[FW_RELEASE_COUNT + 1] - 48;

	return revision;
}

int fimc_is_ois_cal_revision(char *cal_ver)
{
	int revision = 0;

	if( cal_ver[0] != 'V'
		|| cal_ver[1] < '0' || cal_ver[1] > '9'
		|| cal_ver[2] < '0' || cal_ver[2] > '9'
		|| cal_ver[3] < '0' || cal_ver[3] > '9' ) {
		return 0;
	}

	revision = ((cal_ver[1] - '0') * 100) + ((cal_ver[2] - '0') * 10) + (cal_ver[3] - '0');

	return revision;
}

bool fimc_is_ois_version_compare(struct fimc_is_ois_info *m_info, struct fimc_is_ois_info *p_info)
{
	int m_fw_ver, p_fw_ver;
	int m_cal_ver, p_cal_ver;

	m_fw_ver = fimc_is_ois_fw_revision(m_info->header_ver);
	p_fw_ver = fimc_is_ois_fw_revision(p_info->header_ver);

	info("ois_fw_ver (eeprom: %d vs phone : %d)\n", m_fw_ver, p_fw_ver);

	/* if both versions are same, download PHONE OIS FW */
	if (m_fw_ver > p_fw_ver) {
		ois_fw_base_addr = OIS_FW_EEPROM_BASE_ADDR;
	} else {
		m_cal_ver = fimc_is_ois_cal_revision(m_info->cal_ver);
		p_cal_ver = fimc_is_ois_cal_revision(p_info->cal_ver);

		info("ois_cal_ver (eeprom: %d vs phone : %d)\n", m_cal_ver, p_cal_ver);

		if (m_cal_ver != p_cal_ver)
			ois_fw_base_addr = OIS_FW_EEPROM_BASE_ADDR;
		else
			ois_fw_base_addr = OIS_FW_PHONE_BASE_ADDR;
	}

	return true;
}

bool fimc_is_ois_read_userdata(struct fimc_is_core *core)
{
	return true;
}

int fimc_is_ois_open_fw(struct fimc_is_core *core, char *name)
{
	int ret = 0;
	long fsize = 0;
	u8 *read_buf = NULL;
	u8 *temp_buf = NULL;

	static char fw_name[100];
	struct file *fp = NULL;
	mm_segment_t old_fs;
	long nread;
	int fw_requested = 1;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	snprintf(fw_name, sizeof(fw_name), "%s%s", FIMC_IS_OIS_SDCARD_PATH, name);
	fp = filp_open(fw_name, O_RDONLY, 0);
	if (IS_ERR_OR_NULL(fp)) {
		info("failed to open SDCARD fw!!!\n");
		goto request_fw;
	}
	fw_requested = 0;

request_fw:
	if (fw_requested) {
		snprintf(fw_name, sizeof(fw_name), "%s%s", FIMC_IS_FW_PATH, name);
		fp = filp_open(fw_name, O_RDONLY, 0);
		if (IS_ERR(fp)) {
			err("Camera: Failed open phone firmware");
			ret = -EIO;
			fp = NULL;
			goto p_err;
		}
	}

	fsize = fp->f_path.dentry->d_inode->i_size;
	info("start, file path %s, size %ld Bytes\n", fw_name, fsize);

	if (fsize > FIMC_IS_MAX_OIS_SIZE) {
		info("OIS FW size is larger than FW buffer. Use vmalloc.\n");
		read_buf = vmalloc(fsize);
		if (!read_buf) {
			err("failed to allocate memory");
			ret = -ENOMEM;
			goto p_err;
		}
		temp_buf = read_buf;
	} else {
		memset(ois_buf, 0x0, FIMC_IS_MAX_OIS_SIZE);
		temp_buf = ois_buf;
	}

	nread = vfs_read(fp, (char __user *)temp_buf, fsize, &fp->f_pos);
	if (nread != fsize) {
		err("failed to read firmware file, %ld Bytes", nread);
		ret = -EIO;
		goto p_err;
	}

	memcpy(ois_pinfo.header_ver, &ois_buf[OIS_FW_VERSION_ADDR_OFFSET],
		FIMC_IS_OIS_FW_VER_SIZE);
	ois_pinfo.header_ver[FIMC_IS_OIS_FW_VER_SIZE] = '\0';

	memcpy(ois_pinfo.cal_ver, &ois_buf[OIS_CAL_VERSION_ADDR_OFFSET],
		FIMC_IS_OIS_CAL_VER_SIZE);
	ois_pinfo.cal_ver[FIMC_IS_OIS_CAL_VER_SIZE] = '\0';

	core->ois_ver_read = true;

	info("OIS firmware is loaded from Phone binary.\n");

p_err:
	if (read_buf) {
		vfree(read_buf);
		read_buf = NULL;
		temp_buf = NULL;
	}
	if (fp) {
		filp_close(fp, current->files);
		fp = NULL;
	}
	set_fs(old_fs);

	return ret;
}

bool fimc_is_ois_check_fw_impl(struct fimc_is_core *core)
{
	int ret = 0;
	struct fimc_is_from_info *sysfs_finfo;
#ifdef OIS_DEBUG
	int i = 0;
	char *buf = NULL;

	fimc_is_sec_get_cal_buf(&buf);
#endif
	fimc_is_sec_get_sysfs_finfo(&sysfs_finfo);
	memcpy(ois_minfo.header_ver, sysfs_finfo->ois_fw_ver, FIMC_IS_OIS_FW_VER_SIZE);
	memcpy(ois_minfo.cal_ver, sysfs_finfo->ois_cal_ver, FIMC_IS_OIS_CAL_VER_SIZE);
	memcpy(ois_uinfo.header_ver, sysfs_finfo->ois_cal_ver, FIMC_IS_OIS_CAL_VER_SIZE);

	if (ois_minfo.header_ver[0] == 'A') {
		ret = fimc_is_ois_open_fw(core, FIMC_OIS_FW_NAME_SEC);
	} else if (ois_minfo.header_ver[0] == 'B') {
		ret = fimc_is_ois_open_fw(core, FIMC_OIS_FW_NAME_SEM);
	} else if (ois_minfo.header_ver[0] == 'C') {
		ret = fimc_is_ois_open_fw(core, FIMC_OIS_FW_NAME_VE);
	} else {
		info("OIS Module FW version does not matched with phone FW version.\n");
		strcpy(ois_pinfo.header_ver, "NULL");
		return true;
	}

	if (ret) {
		err("fimc_is_ois_open_fw failed(%d)\n", ret);
	}

	fimc_is_ois_version_compare(&ois_minfo, &ois_pinfo);

	fimc_is_ois_fw_status(core);

#ifdef OIS_DEBUG
	info("++ OIS DEBUG ++ \n");
	info("ois fw info : %s\n", sysfs_finfo->ois_fw_ver);
	info("ois cal info : %s\n", sysfs_finfo->ois_cal_ver);
	info("ois minfo header_ver : %s\n", ois_minfo.header_ver);
	info("ois pinfo header_ver : %s\n", ois_pinfo.header_ver);
	info("ois fw base_addr : 0x%4x\n", ois_fw_base_addr);
	info("[DEBUG] OIS Cal Data(h) %d byte\n", FIMC_IS_OIS_CAL_DATA_SIZE);
	for (i = 0; i < FIMC_IS_OIS_CAL_DATA_SIZE; i++) {
		printk("%02X ", buf[EEP_HEADER_OIS_CAL_DATA_ADDR + i]);
		if (((i + 1) % 16) == 0)
			printk("\n");
	}
	info("[DEBUG] OIS Shift Data(h) %d byte\n", FIMC_IS_OIS_SHIFT_DATA_SIZE);
	for (i = 0; i < FIMC_IS_OIS_SHIFT_DATA_SIZE; i++) {
		printk("%02X ", buf[EEP_HEADER_OIS_SHIFT_DATA_ADDR + i]);
		if (((i + 1) % 16) == 0)
			printk("\n");
	}
	info("-- OIS DEBUG -- \n");
#endif

	return true;
}

int fimc_is_ois_read_status(struct fimc_is_core *core)
{
	int ret = 0;
	u8 status = 0;
	int wait_ready_cnt = 0;
	struct exynos_platform_fimc_is *core_pdata = NULL;

	core_pdata = dev_get_platdata(fimc_is_dev);
	if (!core_pdata) {
		err("core->pdata is null");
		return -EINVAL;
	}

	wait_ready_cnt = 100;
	do {
		ret = fimc_is_ois_i2c_read(core->client1, 0x6024, &status);
		if (ret) {
			err("i2c read fail\n");
		}
		mdelay(1);
		wait_ready_cnt --;
	} while((status == 0) && (wait_ready_cnt > 0));

	if (status) {
		info("ois status ready(%d), wait(%d ms)\n", status, 100-wait_ready_cnt);
	} else {
		err("ois status NOT ready(%d), wait(%d ms)\n", status, 100-wait_ready_cnt);
		ret = -EINVAL;
	}

	return ret;
}

u32 fimc_is_ois_read_checksum(struct fimc_is_core *core)
{
	int ret = 0;
	u8 read_data[4] = {0,};
	u32 checksum = 0;
	struct exynos_platform_fimc_is *core_pdata = NULL;

	core_pdata = dev_get_platdata(fimc_is_dev);
	if (!core_pdata) {
		err("core->pdata is null");
		return -EINVAL;
	}

	ret = fimc_is_ois_i2c_read_multi(core->client1, 0xF008, read_data, 4);
	if (ret) {
		err("i2c read fail\n");
	}

	checksum = (read_data[0] << 24) | (read_data[1] << 16) | (read_data[2] << 8) | (read_data[3]);

#ifdef OIS_DEBUG
	info("%s : R(0x%02X%02X%02X%02X)\n",
		__func__, read_data[0], read_data[1], read_data[2], read_data[3]);
#endif

	return checksum;
}

/* Byte swap short - change big-endian to little-endian */
uint16_t swap_uint16( uint16_t val )
{
	return (val << 8) | ((val >> 8) & 0xFF);
}

uint32_t swap_uint32( uint32_t val )
{
	val = ((val << 8) & 0xFF00FF00) | ((val >> 8) & 0xFF00FF);
	return (val << 16) | (val >> 16);
}

u32 fimc_is_ois_facotry_read_cal_checksum(struct fimc_is_core *core)
{
	char *buf = NULL;
	u32 checksum = 0;
	struct exynos_platform_fimc_is *core_pdata = NULL;

	core_pdata = dev_get_platdata(fimc_is_dev);
	if (!core_pdata) {
		err("core->pdata is null");
		return -EINVAL;
	}

	fimc_is_sec_get_cal_buf(&buf);

	checksum = swap_uint32(*((int32_t *)(&buf[OIS_FW_EEPROM_CHECKSUM_ADDR])));
	info("OIS[facotry_read_cal_checksum - R(0x%08X)]\n", checksum);

	return checksum;
}

u32 fimc_is_ois_factory_read_cal_checksum32(struct fimc_is_core *core)
{
	char *buf = NULL;
	uint16_t addr = 0;
	u32 checksum32 = 0;
	struct exynos_platform_fimc_is *core_pdata = NULL;

	core_pdata = dev_get_platdata(fimc_is_dev);
	if (!core_pdata) {
		err("core->pdata is null");
		return -EINVAL;
	}

	fimc_is_sec_get_cal_buf(&buf);

	addr = OIS_FW_EEPROM_CAL_DATA_START_ADDR;
	do {
		checksum32 += (u32)buf[addr];
		// info("OIS[checksum32 - read_addr : 0x%04x, checksum32 : 0x%08x]\n", addr, checksum32);
		addr++;
	} while (addr != OIS_FW_EEPROM_CAL_DATA_END_ADDR + 1);

	return checksum32;
}

int fimc_is_ois_factory_read_IC_ROM_checksum_impl(struct fimc_is_core *core)
{
	int ret = 0;
	u32 i = 0;
	u32 j = 0;
	u32 data = 0;
	u32 IC_checksum = 0;
	u8 write_data[4] = {0,};
	u8 read_data[4] = {0,};
	struct exynos_platform_fimc_is *core_pdata = NULL;
	struct fimc_is_ois_info *ois_minfo = NULL;

	core_pdata = dev_get_platdata(fimc_is_dev);
	if (!core_pdata) {
		err("core->pdata is null");
		return false;
	}

	fimc_is_ois_get_module_version(&ois_minfo);

	ret = fimc_is_ois_i2c_write(core->client1, 0x6080, 0x50);
	if (ret) {
		err("i2c write fail\n");
		goto p_err;
	}

	msleep(100);

	ret = fimc_is_ois_i2c_write(core->client1, 0x6083, 0x0F);
	if (ret) {
		err("i2c write fail\n");
		goto p_err;
	}

	msleep(100);

	for (i = 0x0000; i <= 0xB000; i += 0x1000) {
		if (i == 0x4000)
			i = 0x8000;

		for (j = 0x0000; j <= 0x1FC; j += 0x04) {
			write_data[0] = (u8)(((i+j) & 0xFF00) >> 8);
			write_data[1] = (u8)((i+j) & 0x00FF);

			ret = fimc_is_ois_i2c_write_multi(core->client1, 0x6081, write_data, 2);
			if (ret) {
				err("i2c write fail\n");
				goto p_err;
			}

			write_data[0] = 0x00;
			write_data[1] = 0x00;
			write_data[2] = 0x00;
			write_data[3] = 0x00;

			ret = fimc_is_ois_i2c_write_multi(core->client1, 0x6084, write_data, 4);
			if (ret) {
				err("i2c write fail\n");
				goto p_err;
			}

			ret = fimc_is_ois_i2c_read_multi(core->client1, 0x6084, read_data, 4);
			if (ret) {
				err("i2c read fail\n");
				goto p_err;
			}

			data = (u32)(read_data[0]) + (u32)(read_data[1]) + (u32)(read_data[2]) + (u32)(read_data[3]);

			IC_checksum += data;
		}
	}

p_err:

	info("OIS[IC_ROM_checksum - R(0x%08X)]\n", IC_checksum);
	if (IC_checksum != OIS_IC_ROM_CHECKSUM_CONSTANT) {
		err("OIS SelfTest in 15 - IC ROM data check fail\n");
		ois_minfo->caldata = 0xff;
	} else {
		info("OIS SelfTest in 15 - IC ROM data check pass\n");
		ois_minfo->caldata = 0x00;
	}
	info("OIS SelfTest in 15 - 0x%02X\n", ois_minfo->caldata);

	return 0;
}

int fimc_is_ois_set_mode_impl(struct fimc_is_core *core, int mode)
{
	int ret = 0;
	u8 mode_value = 0;
	u8 angle_data = 0;
	bool factory_test_mode = false;

	info("OIS Change Mode = %d", mode);

	switch(mode) {
	case OPTICAL_STABILIZATION_MODE_STILL:
		mode_value = 0x7B;
		angle_data = 0x50;
		break;
	case OPTICAL_STABILIZATION_MODE_VIDEO:
		mode_value = 0x61;
		angle_data = 0xE0;
		break;
	case OPTICAL_STABILIZATION_MODE_VIDEO_RATIO_4_3:
		mode_value = 0x61;
		angle_data = 0x70;
		break;
	case OPTICAL_STABILIZATION_MODE_SINE_X:
		factory_test_mode = true;
		mode_value = 0x01; /* Sin X*/
		break;
	case OPTICAL_STABILIZATION_MODE_SINE_Y:
		factory_test_mode = true;
		mode_value = 0x02; /* Sin Y*/
		break;
	default:
		info("OIS Mode OFF\n");
		break;
	}

	if(factory_test_mode) {
		ret = fimc_is_ois_i2c_write(core->client1, 0x6130, 0x00);
		if (ret) {
			err("i2c write fail\n");
			goto p_err;
		}
		ret |= fimc_is_ois_i2c_write(core->client1, 0x6020, 0x01);
		ret |= fimc_is_ois_i2c_write(core->client1, 0x6131, 0x01);
		ret |= fimc_is_ois_i2c_write(core->client1, 0x6132, 0x30);
		ret |= fimc_is_ois_i2c_write(core->client1, 0x6020, 0x03);
		ret |= fimc_is_ois_i2c_write(core->client1, 0x6130, mode_value);
	} else if (angle_data) {
		/* OIS Servo On / OIS Off */
		ret = fimc_is_ois_i2c_write(core->client1, 0x6020, 0x01);
		ret |= fimc_is_ois_read_status(core);
		if (ret) {
			err("i2c write fail\n");
			goto p_err;
		}
		/* OIS Mode */
		ret |= fimc_is_ois_i2c_write(core->client1, 0x6021, mode_value);
		ret |= fimc_is_ois_read_status(core);
		/* Compensation angle */
		ret |= fimc_is_ois_i2c_write(core->client1, 0x6025, angle_data);
		ret |= fimc_is_ois_read_status(core);
		/* OIS On */
		ret |= fimc_is_ois_i2c_write(core->client1, 0x6020, 0x02);
		ret |= fimc_is_ois_read_status(core);
	}

	if (ret) {
		err("i2c write fail\n");
		goto p_err;
	}

p_err:
	return ret;
}

int fimc_is_ois_set_shift_compensation_impl(struct fimc_is_core *core, u32 position)
{
	int ret = 0;
	u8 write_data[4] = {0,};

	if (position >= 512) {
		err("position(%d) is over max value 512, skip ois shift \n", position);
		goto p_err;
	}

	write_data[0] = (u8)(((OIS_Shift_X[position] * (-1)) & 0xFF00) >> 8);
	write_data[1] = (u8)((OIS_Shift_X[position] * (-1)) & 0x00FF);
	write_data[2] = (u8)(((OIS_Shift_Y[position] * (-1)) & 0xFF00) >> 8);
	write_data[3] = (u8)((OIS_Shift_Y[position] * (-1)) & 0x00FF);

	ret = fimc_is_ois_i2c_write_multi(core->client1, 0x613A, write_data, 4);
	if (ret)
		err("i2c write fail\n");

p_err:
	return ret;
}

void fimc_is_ois_fw_status_impl(struct fimc_is_core *core)
{
	u32 checksum = 0;
	u32 checksum_32= 0;
	struct fimc_is_ois_info *ois_minfo = NULL;

	checksum = fimc_is_ois_facotry_read_cal_checksum(core);
	checksum_32 = fimc_is_ois_factory_read_cal_checksum32(core);

	info("OIS cal data check = (checksum-0x%08X : checksum_32-0x%08X)\n", checksum, checksum_32);

	fimc_is_ois_get_module_version(&ois_minfo);

	if (checksum != checksum_32) {
		err("OIS SelfTest in 15 - cal data check fail\n");
		ois_minfo->caldata = ois_minfo->caldata | 0xff;
	} else {
		info("OIS SelfTest in 15 - cal data check pass\n");
		ois_minfo->caldata = ois_minfo->caldata | 0x00;
	}

	info("OIS SelfTest in 15 - 0x%02X\n", ois_minfo->caldata);
}

int fimc_is_ois_download_fw(struct fimc_is_core *core)
{
	int ret = 0;
	char *buf = NULL;
	uint16_t ois_fw_addr = 0;
	uint16_t ois_fw_target_addr1 = 0;
	uint16_t ois_fw_offset1 = 0;
	uint16_t ois_fw_size1 = 0;

	uint16_t ois_fw_target_addr2 = 0;
	uint16_t ois_fw_offset2 = 0;
	uint16_t ois_fw_size2 = 0;

	uint16_t ois_fw_target_addr3 = 0;
	uint16_t ois_fw_offset3 = 0;
	uint16_t ois_fw_size3 = 0;

	u32 checksum_fw = 0;
	u32 checksum_ic = 0;

#ifdef CONFIG_USE_VENDER_FEATURE
	struct fimc_is_from_info *sysfs_finfo;
	fimc_is_sec_get_sysfs_finfo(&sysfs_finfo);
#endif

	info("%s : E \n", __func__);

	/* fw address from eeprom fw buf or phone fw buf */
	if (ois_fw_base_addr == OIS_FW_EEPROM_BASE_ADDR) { /* EEPROM FW */
		fimc_is_sec_get_cal_buf(&buf);
		if (is_factory_mode) {
			ois_fw_addr = OIS_FW_EEPROM_BASE_ADDR + OIS_FACTORY_FW_EEPROM_ADDR_OFFSET;
			info("OIS FACTORY FW Loading from EEPROM.\n");
		} else {
			ois_fw_addr = OIS_FW_EEPROM_BASE_ADDR + OIS_SET_FW_EEPROM_ADDR_OFFSET;
			info("OIS SET FW Loading from EEPROM.\n");
		}
	} else { /* PHONE FW */
		buf = ois_buf;
		if (is_factory_mode) {
			ois_fw_addr = OIS_FW_PHONE_BASE_ADDR + OIS_FACTORY_FW_PHONE_ADDR_OFFSET;
			info("OIS FACTORY FW Loading from PHONE.\n");
		} else {
			ois_fw_addr = OIS_FW_PHONE_BASE_ADDR + OIS_SET_FW_PHONE_ADDR_OFFSET;
			info("OIS SET FW Loading from PHONE.\n");
		}
	}

	ois_fw_target_addr1 =
		swap_uint16(*((uint16_t *)&buf[ois_fw_addr+OIS_FW_D1_TARGET_ADDR_OFFSET]));
	ois_fw_offset1 =
		swap_uint16(*((uint16_t *)&buf[ois_fw_addr+OIS_FW_D1_OFFSET_ADDR_OFFSET]));
	ois_fw_size1 =
		swap_uint16(*((uint16_t *)&buf[ois_fw_addr+OIS_FW_D1_SIZE_ADDR_OFFSET]));
	if (ois_fw_size1 > 0) {
		if (ois_fw_addr < FIMC_IS_MAX_CAL_SIZE) {
			ret = fimc_is_ois_i2c_write_multi(core->client1, ois_fw_target_addr1,
								&buf[ois_fw_addr+ois_fw_offset1], ois_fw_size1);
		} else {
			err("OIS_FW_D1 Cal range over fail\n");
			ret = -EINVAL;
		}
	}

	ois_fw_target_addr2 =
		swap_uint16(*((uint16_t *)&buf[ois_fw_addr+OIS_FW_D2_TARGET_ADDR_OFFSET]));
	ois_fw_offset2 =
		swap_uint16(*((uint16_t *)&buf[ois_fw_addr+OIS_FW_D2_OFFSET_ADDR_OFFSET]));
	ois_fw_size2 =
		swap_uint16(*((uint16_t *)&buf[ois_fw_addr+OIS_FW_D2_SIZE_ADDR_OFFSET]));
	if (ois_fw_size2 > 0) {
		if (ois_fw_addr < FIMC_IS_MAX_CAL_SIZE) {
			ret = fimc_is_ois_i2c_write_multi(core->client1, ois_fw_target_addr2,
								&buf[ois_fw_addr+ois_fw_offset2], ois_fw_size2);
		} else {
			err("OIS_FW_D2 Cal range over fail\n");
			ret = -EINVAL;
		}
	}

	ois_fw_target_addr3 =
		swap_uint16(*((uint16_t *)&buf[ois_fw_addr+OIS_FW_D3_TARGET_ADDR_OFFSET]));
	ois_fw_offset3 =
		swap_uint16(*((uint16_t *)&buf[ois_fw_addr+OIS_FW_D3_OFFSET_ADDR_OFFSET]));
	ois_fw_size3 =
		swap_uint16(*((uint16_t *)&buf[ois_fw_addr+OIS_FW_D3_SIZE_ADDR_OFFSET]));
	if (ois_fw_size3 > 0) {
		if (ois_fw_addr < FIMC_IS_MAX_CAL_SIZE) {
			ret = fimc_is_ois_i2c_write_multi(core->client1, ois_fw_target_addr3,
								&buf[ois_fw_addr+ois_fw_offset3], ois_fw_size3);
		} else {
			err("OIS_FW_D3 Cal range over fail\n");
			ret = -EINVAL;
		}
	}

	checksum_fw = swap_uint32(*((int32_t *)&buf[ois_fw_addr]));
	checksum_ic= fimc_is_ois_read_checksum(core);
	checksum_fw_backup = checksum_fw;

	if (checksum_fw != checksum_ic)
		ret = -EINVAL;

#ifdef OIS_DEBUG
	info("OIS SET FW addr = 0x%04X\n", ois_fw_addr);
	info("OIS SET FW #1 target reg. = 0x%04X, offset = 0x%04X, size = 0x%04X\n",
		ois_fw_target_addr1, ois_fw_offset1, ois_fw_size1);
	info("OIS SET FW #2 target reg. = 0x%04X, offset = 0x%04X, size = 0x%04X\n",
		ois_fw_target_addr2, ois_fw_offset2, ois_fw_size2);
	info("OIS SET FW #3 target reg. = 0x%04X, offset = 0x%04X, size = 0x%04X\n",
		ois_fw_target_addr3, ois_fw_offset3, ois_fw_size3);
	info("OIS SET FW checksum = (Phone-0x%08X : Module-0x%08X)\n",
		checksum_fw, checksum_ic);
#endif

	return ret;
}

int fimc_is_ois_download_cal_data(struct fimc_is_core *core)
{
	int ret = 0;
	char *buf = NULL;
	struct fimc_is_from_info *sysfs_finfo;

	uint16_t ois_cal_target_addr;
	uint16_t ois_cal_offset = 0;
	uint16_t ois_cal_size = 0;

	u32 checksum_fw = 0;
	u32 checksum_ic = 0;

	fimc_is_sec_get_sysfs_finfo(&sysfs_finfo);
	fimc_is_sec_get_cal_buf(&buf);

	ois_cal_target_addr = swap_uint16(*((uint16_t *)&buf[EEP_HEADER_OIS_CAL_TARGET_ADDR]));
	ois_cal_offset = swap_uint16(*((uint16_t *)&buf[EEP_HEADER_OIS_CAL_OFFSET_ADDR]));
	ois_cal_size = swap_uint16(*((uint16_t *)&buf[EEP_HEADER_OIS_CAL_SIZE_ADDR]));

	if (ois_cal_size > 0) {
		if (sysfs_finfo->ois_cal_start_addr < FIMC_IS_MAX_CAL_SIZE) {
			ret = fimc_is_ois_i2c_write_multi(core->client1, ois_cal_target_addr,
						&buf[sysfs_finfo->ois_cal_start_addr + ois_cal_offset], ois_cal_size);

			checksum_fw = swap_uint32(*((int32_t *)&buf[sysfs_finfo->ois_cal_start_addr]))
					+ checksum_fw_backup;
			checksum_ic = fimc_is_ois_read_checksum(core);
		} else {
			err("OIS download Cal range over fail\n");
			ret = -EINVAL;
		}
	}

	if (checksum_fw != checksum_ic)
		ret = -EINVAL;

#ifdef OIS_DEBUG
	info("OIS Cal FW addr = 0x%04X\n", sysfs_finfo->ois_cal_start_addr);
	info("OIS Cal target Reg. = 0x%04X, offset = 0x%04X, size = 0x%04X\n",
		ois_cal_target_addr, ois_cal_offset, ois_cal_size);
	info("OIS Cal FW checksum = (Phone-0x%08X : Module-0x%08X)\n",
		checksum_fw, checksum_ic);
#endif

	return ret;
}

void fimc_is_ois_fw_update_impl(struct fimc_is_core *core)
{
	struct exynos_platform_fimc_is *core_pdata = NULL;
	int ret = 0;
	int retry_cnt = 0;
	bool need_reset = false;

	info("fimc_is_ois_fw_update.\n");

	/* Select FW (PHONE or EEPROM) */
	fimc_is_ois_check_fw_impl(core);

	core_pdata = dev_get_platdata(fimc_is_dev);
	if (!core_pdata) {
		err("core->pdata is null");
		return;
	}

	retry_cnt = 3;
	do {
		if (need_reset == true) {
			info("ois fw/cal download failed, retry(%d).\n", retry_cnt);
			fimc_is_ois_reset(core->client1);
			checksum_fw_backup = 0;
			retry_cnt--;
		}

		/* Access to "OIS FW DL" */
		ret = fimc_is_ois_i2c_write(core->client1, 0xF010, 0x00);
		if (ret) {
			err("i2c write fail\n");
			need_reset = true;
			continue;
		}

		/* Download FW (SET or FACTORY) */
		ret = fimc_is_ois_download_fw(core);
		if (ret) {
			err("ois fw write fail, ret(%d)\n", ret);
			need_reset = true;
			continue;
		}

		/* Dwonload OIS Cal data */
		ret = fimc_is_ois_download_cal_data(core);
		if (ret) {
			err("ois caldata write fail, ret(%d)\n", ret);
			need_reset = true;
			continue;
		}
	} while (ret && (retry_cnt > 0));

	/* OIS Download Complete */
	ret = fimc_is_ois_i2c_write(core->client1, 0xF006, 0x00);
	if (ret) {
		err("i2c write fail\n");
	}

	if (!ret)
		info("ois fw download success.\n");
	else
		info("ois fw download failed.\n");

}

int fimc_is_ois_init_impl(struct fimc_is_core *core)
{
	int ret = 0;
	char *buf = NULL;
	uint16_t dataCalX;
	uint16_t dataCalY;
	int16_t dataX[10] = {0,};
	int16_t dataY[10] = {0,};
	int i,j;

	info("OIS init.: Start ois still mode \n");

	/* Read Shift Cal data from EEPROM*/
	fimc_is_sec_get_cal_buf(&buf);
	for(i = 0; i <= 8; i++) {
		dataCalX = swap_uint16(*((uint16_t *)&buf[0x21A0 + i*2]));
		dataCalY = swap_uint16(*((uint16_t *)&buf[0x21B2 + i*2]));
		dataX[i] = *((int16_t *)&dataCalX);
		dataY[i] = *((int16_t *)&dataCalY);
	}

	/* Make OIS Shift Table */
	for(j = 0; j <= 7; j++) {
		for(i = 0; i < 64; i++) {
			OIS_Shift_X[i + (j << 6)] = (((dataX[j + 1] - dataX[j]) * i) >> 6) + dataX[j];
			OIS_Shift_Y[i + (j << 6)] = (((dataY[j + 1] - dataY[j]) * i) >> 6) + dataY[j];
		}
	}
	/* OIS Init : Still Shot */
	/* OIS Servo On, OIS Off, Gyro On */
	ret = fimc_is_ois_read_status(core);
	ret |= fimc_is_ois_i2c_write(core->client1, 0x6020, 0x01);
	ret |= fimc_is_ois_i2c_write(core->client1, 0x6023, 0x00);
	ret |= fimc_is_ois_read_status(core);

	if (ret) {
		err("OIS Start fail\n");
	}

	return ret;
}

MODULE_DESCRIPTION("OIS driver for Rohm");
MODULE_AUTHOR("Roen Lee");
MODULE_LICENSE("GPL v2");
