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

#define OIS_DEBUG
#define OIS_I2C_DEBUG
/*#define OIS_TEST_WITHOUT_ISP_CONTROL*/

#define FIMC_IS_OIS_SDCARD_PATH		"/data/media/0/"
#define FIMC_IS_OIS_DEV_NAME		"exynos-fimc-is-ois"
#define FIMC_OIS_FW_NAME_SEC		"ois_fw_sec.bin"
#define FIMC_OIS_FW_NAME_SEM		"ois_fw_sem.bin"
#define FIMC_OIS_FW_NAME_VE			"ois_fw_ve.bin"

#define FW_GYRO_SENSOR		0
#define FW_DRIVER_IC		1
#define FW_CORE_VERSION		2
#define FW_RELEASE_YEAR		3
#define FW_RELEASE_MONTH		4
#define FW_RELEASE_COUNT		5

#define OIS_FW_EEPROM_BASE_ADDR		0x2000
#define OIS_FW_PHONE_BASE_ADDR		0x0
#define OIS_FW_VERSION_ADDR_OFFSET	0x40
#define OIS_CAL_VERSION_ADDR_OFFSET	0x48

#define OIS_SET_FW_EEPROM_ADDR_OFFSET		0x200
#define OIS_FACTORY_FW_EEPROM_ADDR_OFFSET	0x600

#define OIS_FW_EEPROM_CHECKSUM_ADDR			0x2100

#define OIS_FW_EEPROM_CAL_DATA_START_ADDR	0x2110
#define OIS_FW_EEPROM_CAL_DATA_END_ADDR		0x215F

#define OIS_SET_FW_PHONE_ADDR_OFFSET		0x100
#define OIS_FACTORY_FW_PHONE_ADDR_OFFSET	0x500

#define OIS_GYRO_X_ADDR_OFFSET		0x118
#define OIS_GYRO_Y_ADDR_OFFSET		0x11A

#define OIS_FW_D1_OFFSET_ADDR_OFFSET	0x04
#define OIS_FW_D1_SIZE_ADDR_OFFSET		0x06
#define OIS_FW_D1_TARGET_ADDR_OFFSET	0x08

#define OIS_FW_D2_OFFSET_ADDR_OFFSET	0x0A
#define OIS_FW_D2_SIZE_ADDR_OFFSET		0x0C
#define OIS_FW_D2_TARGET_ADDR_OFFSET	0x0E

#define OIS_FW_D3_OFFSET_ADDR_OFFSET	0x10
#define OIS_FW_D3_SIZE_ADDR_OFFSET		0x12
#define OIS_FW_D3_TARGET_ADDR_OFFSET	0x14

#define OIS_GYRO_SCALE_FACTOR_V003		262
#define OIS_GYRO_SCALE_FACTOR_V004		131

#define OIS_IC_ROM_CHECKSUM_CONSTANT	0x0001ED98

int fimc_is_ois_sine_mode_impl(struct fimc_is_core *core, int mode);
void fimc_is_ois_offset_test_impl(struct fimc_is_core *core, long *raw_data_x, long *raw_data_y);
int fimc_is_ois_self_test_impl(struct fimc_is_core *core);
void fimc_is_ois_fw_update_impl(struct fimc_is_core *core);
void fimc_is_ois_get_offset_data_impl(struct fimc_is_core *core, long *raw_data_x, long *raw_data_y);
bool fimc_is_ois_check_fw_impl(struct fimc_is_core *core);
u32 fimc_is_ois_factory_read_cal_checksum32(struct fimc_is_core *core);
u32 fimc_is_ois_facotry_read_cal_checksum(struct fimc_is_core *core);
int fimc_is_ois_factory_read_IC_ROM_checksum_impl(struct fimc_is_core *core);
void fimc_is_ois_fw_status_impl(struct fimc_is_core *core);
bool fimc_is_ois_diff_test_impl(struct fimc_is_core *core, int *x_diff, int *y_diff);
int fimc_is_ois_gpio_on_impl(struct fimc_is_core *core);
int fimc_is_ois_gpio_off_impl(struct fimc_is_core *core);
int fimc_is_ois_cal_revision(char *cal_ver);
void fimc_is_ois_fw_version(struct fimc_is_core *core);
bool fimc_is_ois_read_userdata(struct fimc_is_core *core);
int fimc_is_ois_set_mode_impl(struct fimc_is_core *core, int mode);
int fimc_is_ois_set_shift_compensation_impl(struct fimc_is_core *core, u32 value);
int fimc_is_ois_init_impl(struct fimc_is_core *core);