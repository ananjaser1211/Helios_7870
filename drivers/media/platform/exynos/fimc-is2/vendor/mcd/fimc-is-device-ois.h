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
#include "fimc-is-sec-define.h"

struct fimc_is_ois_gpio {
	char *sda;
	char *scl;
	char *pinname;
	int reset;
};

struct fimc_is_device_ois {
	struct v4l2_device				v4l2_dev;
	struct platform_device				*pdev;
	unsigned long					state;
	struct exynos_platform_fimc_is_sensor		*pdata;
	struct i2c_client			*client;
	int ois_en;
	bool ois_hsi2c_status;
	struct fimc_is_ois_gpio gpio;
	bool use_reload_ois_fw;
};

struct fimc_is_ois_exif {
	int error_data;
	int status_data;
};

struct fimc_is_ois_info {
	char	header_ver[FIMC_IS_OIS_FW_VER_SIZE+1];
	char	cal_ver[FIMC_IS_OIS_CAL_VER_SIZE+1];
	char	load_fw_name[50];
	u8		checksum;
	u8		caldata;
};

int fimc_is_ois_reset(struct i2c_client *client);
int fimc_is_ois_i2c_config(struct i2c_client *client, bool onoff);
int fimc_is_ois_i2c_read(struct i2c_client *client, u16 addr, u8 *data);
int fimc_is_ois_i2c_write(struct i2c_client *client ,u16 addr, u8 data);
int fimc_is_ois_i2c_read_multi(struct i2c_client *client, u16 addr, u8 *data, size_t size);
int fimc_is_ois_i2c_write_multi(struct i2c_client *client ,u16 addr, u8 *data, size_t size);

void fimc_is_ois_enable(struct fimc_is_core *core);
void fimc_is_ois_offset_test(struct fimc_is_core *core, long *raw_data_x, long *raw_data_y);
int fimc_is_ois_self_test(struct fimc_is_core *core);
bool fimc_is_ois_check_sensor(struct fimc_is_core *core);
int fimc_is_ois_gpio_on(struct fimc_is_core *core);
int fimc_is_ois_gpio_off(struct fimc_is_core *core);
void fimc_is_ois_fw_update(struct fimc_is_core *core);
int fimc_is_ois_get_module_version(struct fimc_is_ois_info **minfo);
int fimc_is_ois_get_phone_version(struct fimc_is_ois_info **minfo);
int fimc_is_ois_get_user_version(struct fimc_is_ois_info **uinfo);
void fimc_is_ois_get_offset_data(struct fimc_is_core *core, long *raw_data_x, long *raw_data_y);
bool fimc_is_ois_check_fw(struct fimc_is_core *core);
bool fimc_is_ois_diff_test(struct fimc_is_core *core, int *x_diff, int *y_diff);
void fimc_is_ois_exif_data(struct fimc_is_core *core);
int fimc_is_ois_get_exif_data(struct fimc_is_ois_exif **exif_info);
void fimc_is_ois_fw_status(struct fimc_is_core *core);
bool fimc_is_ois_check_reload_fw(struct fimc_is_core *core);
void fimc_is_ois_factory_read_IC_ROM_checksum(struct fimc_is_core *core);
int fimc_is_ois_mode_change(struct fimc_is_core *core, int mode);
int fimc_is_ois_shift_compensation(struct fimc_is_core *core, u32 value);
int fimc_is_ois_init(struct fimc_is_core *core);