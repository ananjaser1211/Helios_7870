/*
 * Samsung Exynos5 SoC series FIMC-IS driver
 *
 * exynos5 fimc-is video functions
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "fimc-is-sec-define.h"
#include "fimc-is-vender-specific.h"
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_OIS) && defined(CONFIG_SEC_FACTORY)
#include "fimc-is-device-ois.h"
#endif
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR) || defined(CONFIG_CAMERA_EEPROM_SUPPORT_FRONT)\
	|| defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_FRONT)
#include <linux/i2c.h>
#include "fimc-is-device-eeprom.h"
#if defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_FRONT)
#include "fimc-is-helper-i2c.h"
#include "fimc-is-device-sensor.h"
#endif
#endif

bool crc32_fw_check = true;
bool crc32_setfile_check = true;
bool crc32_check = true;
bool crc32_check_factory = true;
bool crc32_header_check = true;
bool crc32_header_check_front = true;
bool crc32_check_factory_front = true;

/* Rear 2 */
bool crc32_check_factory_rear2 = true;
bool crc32_check_rear2 = true;
bool crc32_header_check_rear2 = true;
bool crc32_setfile_check_rear2 = true;
bool is_latest_cam_module_rear2 = false;
bool is_final_cam_module_rear2 = false;

/* Rear 3 */
bool crc32_check_factory_rear3 = true;
bool crc32_check_rear3 = true;
bool crc32_header_check_rear3 = true;
bool crc32_setfile_check_rear3 = true;
bool is_latest_cam_module_rear3 = false;
bool is_final_cam_module_rear3 = false;

bool fw_version_crc_check = true;
bool is_latest_cam_module = false;
bool is_final_cam_module = false;
#if defined(CONFIG_SOC_EXYNOS5433)
bool is_right_prj_name = true;
#endif
#ifdef CONFIG_COMPANION_USE
bool crc32_c1_fw_check = true;
bool crc32_c1_check = true;
bool crc32_c1_check_factory = true;
bool companion_lsc_isvalid = false;
bool companion_coef_isvalid = false;
#if defined(CONFIG_COMPANION_C2_USE) || defined(CONFIG_COMPANION_C3_USE)
bool crc32_c1_check_front = true;
bool companion_front_lsc_isvalid = false;
#endif
#endif

#define FIMC_IS_LATEST_FROM_VERSION_M	'M'
#define FIMC_IS_DUMP_EEPROM_CAL_SIZE	(16 * 1024)
#ifdef CONFIG_CAMERA_EEPROM_SUPPORT_OIS
bool crc32_ois_check = true;
bool crc32_ois_fw_check = true;
bool crc32_ois_cal_check = true;
#endif

#define FIMC_IS_DEFAULT_CAL_SIZE	(20 * 1024)
#define FIMC_IS_DUMP_CAL_SIZE	(172 * 1024)
#define FIMC_IS_LATEST_ROM_VERSION_M	'M'

//static bool is_caldata_read = false;
//static bool is_c1_caldata_read = false;
bool force_caldata_dump = false;

static int cam_id = CAMERA_SINGLE_REAR;
bool is_dumped_fw_loading_needed = false;
bool is_dumped_c1_fw_loading_needed = false;
char fw_core_version;
//struct class *camera_class;
//struct device *camera_front_dev; /*sys/class/camera/front*/
//struct device *camera_rear_dev; /*sys/class/camera/rear*/
static struct fimc_is_from_info sysfs_finfo;
static struct fimc_is_from_info sysfs_pinfo;
bool crc32_check_front = true;
bool is_final_cam_module_front = false;
static struct fimc_is_from_info sysfs_finfo_front;
static struct fimc_is_from_info sysfs_pinfo_front;
static struct fimc_is_from_info sysfs_finfo_rear2;
static struct fimc_is_from_info sysfs_pinfo_rear2;
static struct fimc_is_from_info sysfs_finfo_rear3;
static struct fimc_is_from_info sysfs_pinfo_rear3;

#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_FRONT) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_FRONT)
static char cal_buf_front[FIMC_IS_MAX_CAL_SIZE_FRONT];
#endif

#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR2) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR2)
static char cal_buf_rear2[FIMC_IS_MAX_CAL_SIZE_REAR2];
#endif

#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR3) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR3)
static char cal_buf_rear3[FIMC_IS_MAX_CAL_SIZE_REAR3];
#endif

static char cal_buf[FIMC_IS_MAX_CAL_SIZE];
#ifdef CAMERA_MODULE_DUALIZE
static char fw_buf[FIMC_IS_MAX_FW_SIZE];
#endif
char loaded_fw[FIMC_IS_HEADER_VER_SIZE + 1] = {0, };
char loaded_companion_fw[30] = {0, };

bool fimc_is_sec_get_force_caldata_dump(void)
{
	return force_caldata_dump;
}

int fimc_is_sec_set_force_caldata_dump(bool fcd)
{
	force_caldata_dump = fcd;
	if (fcd)
		info("forced caldata dump enabled!!\n");
	return 0;
}

int fimc_is_sec_get_max_cal_size(int position)
{
	int size = 0;

	switch (position) {
	case SENSOR_POSITION_REAR:
#ifdef CONFIG_CAMERA_EEPROM_SUPPORT_REAR
		size = FIMC_IS_MAX_CAL_SIZE;
#endif
		break;
	case SENSOR_POSITION_REAR2:
#if defined(FIMC_IS_MAX_CAL_SIZE_REAR2)
		size = FIMC_IS_MAX_CAL_SIZE_REAR2;
#elif defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR) && defined(CAMERA_REAR2)
		size = FIMC_IS_MAX_CAL_SIZE;
#endif
		break;
	case SENSOR_POSITION_REAR3:
#if defined(FIMC_IS_MAX_CAL_SIZE_REAR3)
		size = FIMC_IS_MAX_CAL_SIZE_REAR3;
#elif defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR) && defined(CAMERA_REAR3)
		size = FIMC_IS_MAX_CAL_SIZE;
#endif
		break;
	case SENSOR_POSITION_FRONT:
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_FRONT) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_FRONT)
		size = FIMC_IS_MAX_CAL_SIZE_FRONT;
#endif
		break;
	default:
		err("Invalid postion %d. Check the position", position);
		break;
	}

	if (!size) {
		err("Cal size is 0 (postion %d). Check cal size", position);
		/* WARN(true, "Cal size is 0\n"); */
	}

	return size;
}

int fimc_is_sec_get_sysfs_finfo_by_position(int position, struct fimc_is_from_info **finfo)
{
	*finfo = NULL;

	switch (position) {
	case SENSOR_POSITION_REAR:
		*finfo = &sysfs_finfo; /* default */
		break;
#ifdef CAMERA_REAR2
	case SENSOR_POSITION_REAR2:
#ifdef CONFIG_CAMERA_EEPROM_SUPPORT_REAR2
		*finfo = &sysfs_finfo_rear2;
#endif
		break;
#endif /* CAMERA_REAR2 */
#ifdef CAMERA_REAR3
	case SENSOR_POSITION_REAR3:
#ifdef CONFIG_CAMERA_EEPROM_SUPPORT_REAR3
		*finfo = &sysfs_finfo_rear3;
#endif
		break;
#endif /* CAMERA_REAR3 */
	case SENSOR_POSITION_FRONT:
		*finfo = &sysfs_finfo_front;
		break;
#ifdef CAMERA_FRONT2
	case SENSOR_POSITION_FRONT2:
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_FRONT) && defined(CAMERA_FRONT2)
		*finfo = &sysfs_finfo_front;
#endif
		break;
#endif /* CAMERA_FRONT2 */
	default:
		err("Invalid postion %d. Check the position", position);
		break;
	}

	if (*finfo == NULL) {
		err("finfo addr is null. postion %d", position);
		/*WARN(true, "finfo is null\n");*/
		return -EINVAL;
	}

	return 0;
}

int fimc_is_sec_get_sysfs_finfo(struct fimc_is_from_info **finfo)
{
	*finfo = &sysfs_finfo;
	return 0;
}

int fimc_is_sec_get_sysfs_pinfo(struct fimc_is_from_info **pinfo)
{
	*pinfo = &sysfs_pinfo;
	return 0;
}

int fimc_is_sec_get_sysfs_finfo_front(struct fimc_is_from_info **finfo)
{
	*finfo = &sysfs_finfo_front;
	return 0;
}

int fimc_is_sec_get_sysfs_pinfo_front(struct fimc_is_from_info **pinfo)
{
	*pinfo = &sysfs_pinfo_front;
	return 0;
}

int fimc_is_sec_get_sysfs_finfo_rear2(struct fimc_is_from_info **finfo)
{
	*finfo = &sysfs_finfo_rear2;
	return 0;
}

int fimc_is_sec_get_sysfs_pinfo_rear2(struct fimc_is_from_info **pinfo)
{
	*pinfo = &sysfs_pinfo_rear2;
	return 0;
}

int fimc_is_sec_get_sysfs_finfo_rear3(struct fimc_is_from_info **finfo)
{
	*finfo = &sysfs_finfo_rear3;
	return 0;
}

int fimc_is_sec_get_sysfs_pinfo_rear3(struct fimc_is_from_info **pinfo)
{
	*pinfo = &sysfs_pinfo_rear3;
	return 0;
}

int fimc_is_sec_get_front_cal_buf(char **buf)
{
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_FRONT) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_FRONT)
	*buf = &cal_buf_front[0];
#else
	*buf = NULL;
#endif
	return 0;
}

int fimc_is_sec_get_rear2_cal_buf(char **buf)
{
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR2) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR2)
	*buf = &cal_buf_rear2[0];
#else
	*buf = NULL;
#endif
	return 0;
}

int fimc_is_sec_get_rear3_cal_buf(char **buf)
{
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR3) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR3)
	*buf = &cal_buf_rear3[0];
#else
	*buf = NULL;
#endif
	return 0;
}

int fimc_is_sec_get_cal_buf(char **buf)
{
	*buf = &cal_buf[0];
	return 0;
}

int fimc_is_sec_get_loaded_fw(char **buf)
{
	*buf = &loaded_fw[0];
	return 0;
}

int fimc_is_sec_set_loaded_fw(char *buf)
{
	strncpy(loaded_fw, buf, FIMC_IS_HEADER_VER_SIZE);
	return 0;
}

int fimc_is_sec_get_loaded_c1_fw(char **buf)
{
	*buf = &loaded_companion_fw[0];
	return 0;
}

int fimc_is_sec_set_loaded_c1_fw(char *buf)
{
	strncpy(loaded_companion_fw, buf, FIMC_IS_HEADER_VER_SIZE);
	return 0;
}

int fimc_is_sec_set_camid(int id)
{
	cam_id = id;
	return 0;
}

int fimc_is_sec_get_camid(void)
{
	return cam_id;
}

int fimc_is_sec_get_camid_from_hal(char *fw_name, char *setf_name)
{
#if 0
	char buf[1];
	loff_t pos = 0;
	int pixelSize;

	read_data_from_file("/data/CameraID.txt", buf, 1, &pos);
	if (buf[0] == '0')
		cam_id = CAMERA_SINGLE_REAR;
	else if (buf[0] == '1')
		cam_id = CAMERA_SINGLE_FRONT;
	else if (buf[0] == '2')
		cam_id = CAMERA_DUAL_REAR;
	else if (buf[0] == '3')
		cam_id = CAMERA_DUAL_FRONT;

	if (fimc_is_sec_fw_module_compare(sysfs_finfo.header_ver, FW_3L2)) {
		snprintf(fw_name, sizeof(FIMC_IS_FW_3L2), "%s", FIMC_IS_FW_3L2);
		snprintf(setf_name, sizeof(FIMC_IS_3L2_SETF), "%s", FIMC_IS_3L2_SETF);
	} else if (fimc_is_sec_fw_module_compare(sysfs_finfo.header_ver, FW_IMX135)) {
		snprintf(fw_name, sizeof(FIMC_IS_FW), "%s", FIMC_IS_FW);
		snprintf(setf_name, sizeof(FIMC_IS_IMX135_SETF), "%s", FIMC_IS_IMX135_SETF);
	} else if (fimc_is_sec_fw_module_compare(sysfs_finfo.header_ver, FW_IMX134)) {
		snprintf(fw_name, sizeof(FIMC_IS_FW_IMX134), "%s", FIMC_IS_FW_IMX134);
		snprintf(setf_name, sizeof(FIMC_IS_IMX134_SETF), "%s", FIMC_IS_IMX134_SETF);
	} else {
		pixelSize = fimc_is_sec_get_pixel_size(sysfs_finfo.header_ver);
		if (pixelSize == 13) {
			snprintf(fw_name, sizeof(FIMC_IS_FW), "%s", FIMC_IS_FW);
			snprintf(setf_name, sizeof(FIMC_IS_IMX135_SETF), "%s", FIMC_IS_IMX135_SETF);
		} else if (pixelSize == 8) {
			snprintf(fw_name, sizeof(FIMC_IS_FW_IMX134), "%s", FIMC_IS_FW_IMX134);
			snprintf(setf_name, sizeof(FIMC_IS_IMX134_SETF), "%s", FIMC_IS_IMX134_SETF);
		} else {
			snprintf(fw_name, sizeof(FIMC_IS_FW), "%s", FIMC_IS_FW);
			snprintf(setf_name, sizeof(FIMC_IS_IMX135_SETF), "%s", FIMC_IS_IMX135_SETF);
		}
	}

	if (cam_id == CAMERA_SINGLE_FRONT ||
		cam_id == CAMERA_DUAL_FRONT) {
		snprintf(setf_name, sizeof(FIMC_IS_6B2_SETF), "%s", FIMC_IS_6B2_SETF);
	}
#else
	err("%s: waring, you're calling the disabled func!", __func__);
#endif
	return 0;
}

int fimc_is_sec_fw_revision(char *fw_ver)
{
	int revision = 0;
	revision = revision + ((int)fw_ver[FW_PUB_YEAR] - 58) * 10000;
	revision = revision + ((int)fw_ver[FW_PUB_MON] - 64) * 100;
	revision = revision + ((int)fw_ver[FW_PUB_NUM] - 48) * 10;
	revision = revision + (int)fw_ver[FW_PUB_NUM + 1] - 48;

	return revision;
}

bool fimc_is_sec_fw_module_compare(char *fw_ver1, char *fw_ver2)
{
	if (fw_ver1[FW_CORE_VER] != fw_ver2[FW_CORE_VER]
		|| fw_ver1[FW_PIXEL_SIZE] != fw_ver2[FW_PIXEL_SIZE]
		|| fw_ver1[FW_PIXEL_SIZE + 1] != fw_ver2[FW_PIXEL_SIZE + 1]
		|| fw_ver1[FW_ISP_COMPANY] != fw_ver2[FW_ISP_COMPANY]
		|| fw_ver1[FW_SENSOR_MAKER] != fw_ver2[FW_SENSOR_MAKER]) {
		return false;
	}

	return true;
}

u8 fimc_is_sec_compare_ver(int position)
{
	u32 from_ver = 0, def_ver = 0, def_ver2 = 0;
	u8 ret = 0;
	char ver[3] = {'V', '0', '0'};
	char ver2[3] ={'V', 'F', '0'};
	struct fimc_is_from_info *finfo = NULL;

	if (fimc_is_sec_get_sysfs_finfo_by_position(position, &finfo)) {
		err("failed get finfo. plz check position %d", position);
		return 0;
	}
	def_ver = ver[0] << 16 | ver[1] << 8 | ver[2];
	def_ver2 = ver2[0] << 16 | ver2[1] << 8 | ver2[2];
	from_ver = finfo->cal_map_ver[0] << 16 | finfo->cal_map_ver[1] << 8 | finfo->cal_map_ver[2];
	if ((from_ver == def_ver) || (from_ver == def_ver2)) {
		return finfo->cal_map_ver[3];
	} else {
		err("FROM core version is invalid. version is %c%c%c%c",
			finfo->cal_map_ver[0], finfo->cal_map_ver[1], finfo->cal_map_ver[2], finfo->cal_map_ver[3]);
		return 0;
	}

	return ret;
}

bool fimc_is_sec_check_from_ver(struct fimc_is_core *core, int position)
{
	struct fimc_is_from_info *finfo = NULL;
	struct fimc_is_vender_specific *specific = core->vender.private_data;
	char compare_version = 0;
	u8 from_ver;
	u8 latest_from_ver = 0;

	if (specific->skip_cal_loading) {
		err("skip_cal_loading implemented");
		return false;
	}

	if (fimc_is_sec_get_sysfs_finfo_by_position(position, &finfo)) {
		err("failed get finfo. plz check position %d", position);
		return false;
	}

	if (position == SENSOR_POSITION_FRONT) {
#if defined(CAL_MAP_ES_VERSION_FRONT) && defined(CAMERA_MODULE_ES_VERSION_FRONT)
		latest_from_ver = CAL_MAP_ES_VERSION_FRONT;
		compare_version = CAMERA_MODULE_ES_VERSION_FRONT;
#endif
	} else if (position == SENSOR_POSITION_REAR2) {
#if defined(CAMERA_MODULE_ES_VERSION_REAR2)
	latest_from_ver = CAL_MAP_ES_VERSION_REAR2;
	compare_version = CAMERA_MODULE_ES_VERSION_REAR2;
#endif
	} else if (position == SENSOR_POSITION_REAR3) {
#if defined(CAMERA_MODULE_ES_VERSION_REAR3)
	latest_from_ver = CAL_MAP_ES_VERSION_REAR3;
	compare_version = CAMERA_MODULE_ES_VERSION_REAR3;
#endif
	} else {
#if defined(CAL_MAP_ES_VERSION_REAR) && defined(CAMERA_MODULE_ES_VERSION_REAR)
		latest_from_ver = CAL_MAP_ES_VERSION_REAR;
		compare_version = CAMERA_MODULE_ES_VERSION_REAR;
#endif
	}

	from_ver = fimc_is_sec_compare_ver(position);

	if ((from_ver < latest_from_ver) ||
		(finfo->header_ver[10] < compare_version)) {
		err("invalid from version. from_ver %c, header_ver[10] %c", from_ver, finfo->header_ver[10]);
		return false;
	} else {
		return true;
	}
}

#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR2)
bool fimc_is_sec_check_rear2_cal_crc32(char *buf, int position)
{
	u32 *buf32 = NULL;
	u32 checksum;
	u32 check_base;
	u32 check_length;
	u32 checksum_base;
	u32 address_boundary;
	bool crc32_temp, crc32_temp2, crc32_header_temp;
	bool crc32_oem_check, crc32_awb_check, crc32_shading_check;
	struct fimc_is_from_info *finfo = NULL;
	struct fimc_is_companion_retention *ret_data;
	struct fimc_is_core *core;
	struct fimc_is_vender_specific *specific;

	core = (struct fimc_is_core *)dev_get_drvdata(fimc_is_dev);
	specific = core->vender.private_data;
	ret_data = &specific->retention_data;
	buf32 = (u32 *)buf;

	info("+++ %s\n", __func__);

	/***** Variable Initial *****/
	crc32_temp = true;
	crc32_temp2 = true;
	crc32_check = true;
	crc32_check_rear2 = true;

	/***** SKIP CHECK CRC *****/
#if defined(SKIP_CHECK_CRC)
	pr_warning("%s: rear & rear2 skip to check crc\n", __func__);
	return crc32_check && crc32_check_rear2;
#endif

	/***** START CHECK CRC *****/

	address_boundary = fimc_is_sec_get_max_cal_size(position);

	/* HEADER DATA CRC CHECK */
	check_base = 0;
	checksum = 0;
	fimc_is_sec_get_sysfs_finfo_by_position(position, &finfo);
	check_length = HEADER_CRC32_LEN_REAR2;

	checksum_base = finfo->header_section_crc_addr / 4;
	checksum = (u32)getCRC((u16 *)&buf32[check_base], check_length, NULL, NULL);
	if (checksum != buf32[checksum_base]) {
		err("Camera: CRC32 error at the header (0x%08X != 0x%08X)", checksum, buf32[checksum_base]);
		crc32_temp = false;
		crc32_temp2 = false;
		crc32_header_temp = false;
		goto out;
	} else {
		crc32_header_temp = true;
	}

	/* MAIN OEM DATA CRC CHECK */
	crc32_oem_check = false;
	check_length = 0;

#if defined(EEP_HEADER_OEM_START_ADDR_REAR2)
	crc32_oem_check = true;
#if defined(OEM_CRC32_LEN_REAR2)
	check_length = OEM_CRC32_LEN_REAR2;
#else
	check_length = (finfo->oem_end_addr - finfo->oem_start_addr + 1);
#endif
#endif

	if (crc32_oem_check) {
		check_base = finfo->oem_start_addr / 4;
		checksum = 0;
		checksum_base = finfo->oem_section_crc_addr / 4;

		if (check_base > address_boundary || checksum_base > address_boundary || check_length <= 0) {
			err("Camera: OEM address has error: start(0x%08X), end(0x%08X)",
				finfo->oem_start_addr, finfo->oem_end_addr);
			crc32_temp = false;
			goto out;
		}

		checksum = (u32)getCRC((u16 *)&buf32[check_base], check_length, NULL, NULL);
		if (checksum != buf32[checksum_base]) {
			err("Camera: CRC32 error at the OEM (0x%08X != 0x%08X)", checksum, buf32[checksum_base]);
			crc32_temp = false;
			goto out;
		}
	} else {
		pr_warning("%s: rear skip to check oem crc32\n", __func__);
	}

	/* MAIN AWB DATA CRC CHECK */
	crc32_awb_check = false;
	check_length = 0;

#if defined(EEP_HEADER_AWB_START_ADDR_REAR2)
	crc32_awb_check = true;
#if defined(AWB_CRC32_LEN_REAR2)
	check_length = AWB_CRC32_LEN_REAR2;
#else
	check_length = (finfo->awb_end_addr - finfo->awb_start_addr + 1) ;
#endif
#endif

	if (crc32_awb_check) {
		check_base = finfo->awb_start_addr / 4;
		checksum = 0;
		checksum_base = finfo->awb_section_crc_addr / 4;

		if (check_base > address_boundary || checksum_base > address_boundary || check_length <= 0) {
			err("Camera: AWB address has error: start(0x%08X), end(0x%08X)",
				finfo->awb_start_addr, finfo->awb_end_addr);
			crc32_temp = false;
			goto out;
		}

		checksum = (u32)getCRC((u16 *)&buf32[check_base], check_length, NULL, NULL);
		if (checksum != buf32[checksum_base]) {
			err("Camera: CRC32 error at the AWB (0x%08X != 0x%08X)", checksum, buf32[checksum_base]);
			crc32_temp = false;
			goto out;
		}
	} else {
		pr_warning("%s: rear skip to check awb crc32\n", __func__);
	}

	/* MAIN SHADING DATA CRC CHECK*/
	crc32_shading_check = false;
	check_length = 0;

#if defined(EEP_HEADER_AP_SHADING_START_ADDR_REAR2)
	crc32_shading_check = true;
#if defined(SHADING_CRC32_LEN_REAR2)
	check_length = SHADING_CRC32_LEN_REAR2;
#else
	check_length = (finfo->shading_end_addr - finfo->shading_start_addr + 1) ;
#endif
#endif

	if (crc32_shading_check) {
		check_base = finfo->shading_start_addr / 4;
		checksum = 0;
		checksum_base = finfo->shading_section_crc_addr / 4;

		if (check_base > address_boundary || checksum_base > address_boundary || check_length <= 0) {
			err("Camera: Shading address has error: start(0x%08X), end(0x%08X)",
				finfo->shading_start_addr, finfo->shading_end_addr);
			crc32_temp = false;
			goto out;
		}

		checksum = (u32)getCRC((u16 *)&buf32[check_base], check_length, NULL, NULL);
		if (checksum != buf32[checksum_base]) {
			err("Camera: CRC32 error at the Shading (0x%08X != 0x%08X)", checksum, buf32[checksum_base]);
			crc32_temp = false;
			goto out;
		}
	} else {
		pr_warning("%s: rear skip to check shading crc32\n", __func__);
	}

#if defined(EEP_HEADER_AP_PDAF_START_ADDR_REAR2)
	/* MAIN PDAF CAL DATA CRC CHECK */
	check_base = finfo->ap_pdaf_start_addr / 4;
	checksum = 0;
	check_length = (finfo->ap_pdaf_end_addr - finfo->ap_pdaf_start_addr + 1);
	checksum_base = finfo->ap_pdaf_section_crc_addr / 4;

	if (check_base > address_boundary || checksum_base > address_boundary || check_length <= 0) {
		err("Camera: pdaf address has error: start(0x%08X), end(0x%08X)",
			finfo->ap_pdaf_start_addr, finfo->ap_pdaf_end_addr);
		crc32_temp = false;
		goto out;
	}

	checksum = (u32)getCRC((u16 *)&buf32[check_base], check_length, NULL, NULL);
	if (checksum != buf32[checksum_base]) {
		err("Camera: CRC32 error at the pdaf cal (0x%08X != 0x%08X)", checksum, buf32[checksum_base]);
		crc32_temp = false;
		goto out;
	}
#endif

out:
	crc32_check = crc32_temp;
	crc32_check_rear2 = crc32_temp2;
	crc32_header_check = crc32_header_temp;
	info("crc32_header_check=%d, crc32_check=%d, crc32_check_rear2=%d\n", crc32_header_check, crc32_check, crc32_check_rear2);

	return crc32_check && crc32_check_rear2;
}
#endif

#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR3)
bool fimc_is_sec_check_rear3_cal_crc32(char *buf, int position)
{
	u32 *buf32 = NULL;
	u32 checksum;
	u32 check_base;
	u32 check_length;
	u32 checksum_base;
	u32 address_boundary;
	bool crc32_temp, crc32_temp2, crc32_header_temp;
	bool crc32_oem_check, crc32_awb_check, crc32_shading_check;
	struct fimc_is_from_info *finfo = NULL;
	struct fimc_is_companion_retention *ret_data;
	struct fimc_is_core *core;
	struct fimc_is_vender_specific *specific;

	core = (struct fimc_is_core *)dev_get_drvdata(fimc_is_dev);
	specific = core->vender.private_data;
	ret_data = &specific->retention_data;
	buf32 = (u32 *)buf;

	info("+++ %s\n", __func__);

	/***** Variable Initial *****/
	crc32_temp = true;
	crc32_temp2 = true;
	crc32_check = true;
	crc32_check_rear3 = true;

	/***** SKIP CHECK CRC *****/
#if defined(SKIP_CHECK_CRC)
	pr_warning("%s: rear & rear3 skip to check crc\n", __func__);
	return crc32_check && crc32_check_rear3;
#endif

	/***** START CHECK CRC *****/

	address_boundary = fimc_is_sec_get_max_cal_size(position);

	/* HEADER DATA CRC CHECK */
	check_base = 0;
	checksum = 0;
	fimc_is_sec_get_sysfs_finfo_by_position(position, &finfo);
	check_length = HEADER_CRC32_LEN_REAR3;

	checksum_base = finfo->header_section_crc_addr / 4;
	checksum = (u32)getCRC((u16 *)&buf32[check_base], check_length, NULL, NULL);
	if (checksum != buf32[checksum_base]) {
		err("Camera: CRC32 error at the header (0x%08X != 0x%08X)", checksum, buf32[checksum_base]);
		crc32_temp = false;
		crc32_temp2 = false;
		crc32_header_temp = false;
		goto out;
	} else {
		crc32_header_temp = true;
	}

	/* MAIN OEM DATA CRC CHECK */
	crc32_oem_check = false;
	check_length = 0;

#if defined(EEP_HEADER_OEM_START_ADDR_REAR3)
	crc32_oem_check = true;
#if defined(OEM_CRC32_LEN_REAR3)
	check_length = OEM_CRC32_LEN_REAR3;
#else
	check_length = (finfo->oem_end_addr - finfo->oem_start_addr + 1);
#endif
#endif

	if (crc32_oem_check) {
		check_base = finfo->oem_start_addr / 4;
		checksum = 0;
		checksum_base = finfo->oem_section_crc_addr / 4;

		if (check_base > address_boundary || checksum_base > address_boundary || check_length <= 0) {
			err("Camera: OEM address has error: start(0x%08X), end(0x%08X)",
				finfo->oem_start_addr, finfo->oem_end_addr);
			crc32_temp = false;
			goto out;
		}

		checksum = (u32)getCRC((u16 *)&buf32[check_base], check_length, NULL, NULL);
		if (checksum != buf32[checksum_base]) {
			err("Camera: CRC32 error at the OEM (0x%08X != 0x%08X)", checksum, buf32[checksum_base]);
			crc32_temp = false;
			goto out;
		}
	} else {
		pr_warning("%s: rear skip to check oem crc32\n", __func__);
	}

	/* MAIN AWB DATA CRC CHECK */
	crc32_awb_check = false;
	check_length = 0;

#if defined(EEP_HEADER_AWB_START_ADDR_REAR3)
	crc32_awb_check = true;
#if defined(AWB_CRC32_LEN_REAR3)
	check_length = AWB_CRC32_LEN_REAR3;
#else
	check_length = (finfo->awb_end_addr - finfo->awb_start_addr + 1) ;
#endif
#endif

	if (crc32_awb_check) {
		check_base = finfo->awb_start_addr / 4;
		checksum = 0;
		checksum_base = finfo->awb_section_crc_addr / 4;

		if (check_base > address_boundary || checksum_base > address_boundary || check_length <= 0) {
			err("Camera: AWB address has error: start(0x%08X), end(0x%08X)",
				finfo->awb_start_addr, finfo->awb_end_addr);
			crc32_temp = false;
			goto out;
		}

		checksum = (u32)getCRC((u16 *)&buf32[check_base], check_length, NULL, NULL);
		if (checksum != buf32[checksum_base]) {
			err("Camera: CRC32 error at the AWB (0x%08X != 0x%08X)", checksum, buf32[checksum_base]);
			crc32_temp = false;
			goto out;
		}
	} else {
		pr_warning("%s: rear skip to check awb crc32\n", __func__);
	}

	/* MAIN SHADING DATA CRC CHECK*/
	crc32_shading_check = false;
	check_length = 0;

#if defined(EEP_HEADER_AP_SHADING_START_ADDR_REAR3)
	crc32_shading_check = true;
#if defined(SHADING_CRC32_LEN_REAR3)
	check_length = SHADING_CRC32_LEN_REAR3;
#else
	check_length = (finfo->shading_end_addr - finfo->shading_start_addr + 1) ;
#endif
#endif

	if (crc32_shading_check) {
		check_base = finfo->shading_start_addr / 4;
		checksum = 0;
		checksum_base = finfo->shading_section_crc_addr / 4;

		if (check_base > address_boundary || checksum_base > address_boundary || check_length <= 0) {
			err("Camera: Shading address has error: start(0x%08X), end(0x%08X)",
				finfo->shading_start_addr, finfo->shading_end_addr);
			crc32_temp = false;
			goto out;
		}

		checksum = (u32)getCRC((u16 *)&buf32[check_base], check_length, NULL, NULL);
		if (checksum != buf32[checksum_base]) {
			err("Camera: CRC32 error at the Shading (0x%08X != 0x%08X)", checksum, buf32[checksum_base]);
			crc32_temp = false;
			goto out;
		}
	} else {
		pr_warning("%s: rear skip to check shading crc32\n", __func__);
	}

#if defined(EEP_HEADER_AP_PDAF_START_ADDR_REAR3)
	/* MAIN PDAF CAL DATA CRC CHECK */
	check_base = finfo->ap_pdaf_start_addr / 4;
	checksum = 0;
	check_length = (finfo->ap_pdaf_end_addr - finfo->ap_pdaf_start_addr + 1);
	checksum_base = finfo->ap_pdaf_section_crc_addr / 4;

	if (check_base > address_boundary || checksum_base > address_boundary || check_length <= 0) {
		err("Camera: pdaf address has error: start(0x%08X), end(0x%08X)",
			finfo->ap_pdaf_start_addr, finfo->ap_pdaf_end_addr);
		crc32_temp = false;
		goto out;
	}

	checksum = (u32)getCRC((u16 *)&buf32[check_base], check_length, NULL, NULL);
	if (checksum != buf32[checksum_base]) {
		err("Camera: CRC32 error at the pdaf cal (0x%08X != 0x%08X)", checksum, buf32[checksum_base]);
		crc32_temp = false;
		goto out;
	}
#endif

out:
	crc32_check = crc32_temp;
	crc32_check_rear3 = crc32_temp2;
	crc32_header_check = crc32_header_temp;
	info("crc32_header_check=%d, crc32_check=%d, crc32_check_rear3=%d\n", crc32_header_check, crc32_check, crc32_check_rear3);

	return crc32_check && crc32_check_rear3;
}
#endif

bool fimc_is_sec_check_cal_crc32(char *buf, int id)
{
	u32 *buf32 = NULL;
	u32 checksum;
	u32 check_base;
	u32 check_length;
	u32 checksum_base;
	u32 address_boundary;
	bool crc32_temp, crc32_header_temp;
	struct fimc_is_from_info *finfo = NULL;
	struct fimc_is_companion_retention *ret_data;
	struct fimc_is_core *core;
	struct fimc_is_vender_specific *specific;

	core = (struct fimc_is_core *)dev_get_drvdata(fimc_is_dev);
	specific = core->vender.private_data;
	ret_data = &specific->retention_data;
	buf32 = (u32 *)buf;

	printk(KERN_INFO "+++ %s\n", __func__);

	crc32_temp = true;

#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR2)
	if (id == SENSOR_POSITION_REAR2)
		return fimc_is_sec_check_rear2_cal_crc32(buf, id);
#endif /* defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR2) */

#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR3)
	if (id == SENSOR_POSITION_REAR3)
		return fimc_is_sec_check_rear3_cal_crc32(buf, id);
#endif /* defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR3) */

#ifdef CONFIG_COMPANION_USE
	if (id == SENSOR_POSITION_REAR)
		crc32_c1_check = true;
#endif

#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_FRONT) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_FRONT)
	if (id == SENSOR_POSITION_FRONT) {
		address_boundary = FIMC_IS_MAX_CAL_SIZE_FRONT;
	} else
#endif
	{
		address_boundary = FIMC_IS_MAX_CAL_SIZE;
	}

	/* Header data */
	check_base = 0;
	checksum = 0;
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_FRONT) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_FRONT)
	if (id == SENSOR_POSITION_FRONT) {
		finfo = &sysfs_finfo_front;
		check_length = HEADER_CRC32_LEN_FRONT;
	} else
#endif
	{
		finfo = &sysfs_finfo;
		check_length = HEADER_CRC32_LEN;
	}

	checksum_base = finfo->header_section_crc_addr / 4;
	checksum = (u32)getCRC((u16 *)&buf32[check_base], check_length, NULL, NULL);
	if (checksum != buf32[checksum_base]) {
		err("Camera: CRC32 error at the header (0x%08X != 0x%08X), check_length(%d)",
			checksum, buf32[checksum_base], check_length);
		crc32_temp = false;
		crc32_header_temp = false;
		goto out;
	} else {
		crc32_header_temp = true;
	}

#if defined(CONFIG_COMPANION_C3_USE)
	if(id == SENSOR_POSITION_REAR) {
		/* Calibration data : for HERO with 73C3 */
		check_base = finfo->cal_data_start_addr / 4;
		checksum = 0;
		check_length = (finfo->cal_data_end_addr - finfo->cal_data_start_addr + 1) ;
		checksum_base = finfo->cal_data_section_crc_addr / 4;

		if (check_base > address_boundary || checksum_base > address_boundary || check_length <= 0) {
			err("Camera: cal data address has error: start(0x%08X), end(0x%08X)",
				finfo->cal_data_start_addr, finfo->cal_data_end_addr);
			crc32_temp = false;
			crc32_c1_check = false;
			goto out;
		}

		checksum = (u32)getCRC((u16 *)&buf32[check_base], check_length, NULL, NULL);
		if (checksum != buf32[checksum_base]) {
			err("Camera: CRC32 error at the cal data (0x%08X != 0x%08X)", checksum, buf32[checksum_base]);
			crc32_temp = false;
			crc32_c1_check = false;
			goto out;
		}
		goto out;
	}
#endif

#if defined(EEP_HEADER_OEM_START_ADDR_FRONT) || defined(OTP_HEADER_OEM_START_ADDR_FRONT)
	if (id == SENSOR_POSITION_FRONT) {
		/* OEM */
		check_base = finfo->oem_start_addr / 4;
		checksum = 0;
#if defined(OEM_CRC32_LEN_FRONT)
		check_length = OEM_CRC32_LEN_FRONT;
#else
		check_length = (finfo->oem_end_addr - finfo->oem_start_addr + 1);
#endif
		checksum_base = finfo->oem_section_crc_addr / 4;

		if (check_base > address_boundary || checksum_base > address_boundary || check_length <= 0) {
			err("Camera: OEM address has error: start(0x%08X), end(0x%08X)",
				finfo->oem_start_addr, finfo->oem_end_addr);
			crc32_temp = false;
			goto out;
		}

		checksum = (u32)getCRC((u16 *)&buf32[check_base], check_length, NULL, NULL);
		if (checksum != buf32[checksum_base]) {
			err("Camera: CRC32 error at the OEM (0x%08X != 0x%08X)", checksum, buf32[checksum_base]);
			crc32_temp = false;
			goto out;
		}
	}
#endif
	if (id == SENSOR_POSITION_REAR) {
		/* OEM */
		check_base = finfo->oem_start_addr / 4;
		checksum = 0;
#if defined(OEM_CRC32_LEN)
		check_length = OEM_CRC32_LEN;
#else
		check_length = (finfo->oem_end_addr - finfo->oem_start_addr + 1);
#endif
		checksum_base = finfo->oem_section_crc_addr / 4;

		if (check_base > address_boundary || checksum_base > address_boundary || check_length <= 0) {
			err("Camera: OEM address has error: start(0x%08X), end(0x%08X)",
				finfo->oem_start_addr, finfo->oem_end_addr);
			crc32_temp = false;
			goto out;
		}

		checksum = (u32)getCRC((u16 *)&buf32[check_base], check_length, NULL, NULL);
		if (checksum != buf32[checksum_base]) {
			err("Camera: CRC32 error at the OEM (0x%08X != 0x%08X)", checksum, buf32[checksum_base]);
			crc32_temp = false;
			goto out;
		}
	}

#if defined(EEP_HEADER_OEM_START_ADDR_FRONT) || defined(OTP_HEADER_OEM_START_ADDR_FRONT)
	if (id == SENSOR_POSITION_FRONT) {
		/* AWB */
		check_base = finfo->awb_start_addr / 4;
		checksum = 0;
#if defined(AWB_CRC32_LEN_FRONT)
		check_length = AWB_CRC32_LEN_FRONT;
#else
		check_length = (finfo->awb_end_addr - finfo->awb_start_addr + 1);
#endif
		checksum_base = finfo->awb_section_crc_addr / 4;

		if (check_base > address_boundary || checksum_base > address_boundary || check_length <= 0) {
			err("Camera: AWB address has error: start(0x%08X), end(0x%08X)",
				finfo->awb_start_addr, finfo->awb_end_addr);
			crc32_temp = false;
			goto out;
		}

		checksum = (u32)getCRC((u16 *)&buf32[check_base], check_length, NULL, NULL);
		if (checksum != buf32[checksum_base]) {
			err("Camera: CRC32 error at the OEM (0x%08X != 0x%08X)", checksum, buf32[checksum_base]);
			crc32_temp = false;
			goto out;
		}
	}
#endif
	if (id == SENSOR_POSITION_REAR) {
		/* AWB */
		check_base = finfo->awb_start_addr / 4;
		checksum = 0;
#if defined(AWB_CRC32_LEN)
		check_length = AWB_CRC32_LEN;
#else
		check_length = (finfo->awb_end_addr - finfo->awb_start_addr + 1) ;
#endif
		checksum_base = finfo->awb_section_crc_addr / 4;

		if (check_base > address_boundary || checksum_base > address_boundary || check_length <= 0) {
			err("Camera: AWB address has error: start(0x%08X), end(0x%08X)",
				finfo->awb_start_addr, finfo->awb_end_addr);
			crc32_temp = false;
			goto out;
		}

		checksum = (u32)getCRC((u16 *)&buf32[check_base], check_length, NULL, NULL);
		if (checksum != buf32[checksum_base]) {
			err("Camera: CRC32 error at the AWB (0x%08X != 0x%08X)", checksum, buf32[checksum_base]);
			crc32_temp = false;
			goto out;
		}
	}

#if defined(EEP_HEADER_OEM_START_ADDR_FRONT) || defined(OTP_HEADER_OEM_START_ADDR_FRONT)
	if (id == SENSOR_POSITION_FRONT) {
		/* Shading */
		check_base = finfo->shading_start_addr / 4;
		checksum = 0;
#if defined(SHADING_CRC32_LEN_FRONT)
		check_length = SHADING_CRC32_LEN_FRONT;
#else
		check_length = (finfo->shading_end_addr - finfo->shading_start_addr + 1);
#endif
		checksum_base = finfo->shading_section_crc_addr / 4;

		if (check_base > address_boundary || checksum_base > address_boundary || check_length <= 0) {
			err("Camera: Shading address has error: start(0x%08X), end(0x%08X)",
				finfo->shading_start_addr, finfo->shading_end_addr);
			crc32_temp = false;
			goto out;
		}

		checksum = (u32)getCRC((u16 *)&buf32[check_base], check_length, NULL, NULL);
		if (checksum != buf32[checksum_base]) {
			err("Camera: CRC32 error at the Shading (0x%08X != 0x%08X)", checksum, buf32[checksum_base]);
			crc32_temp = false;
			goto out;
		}
	}
#endif
	if (id == SENSOR_POSITION_REAR) {
		/* Shading */
		check_base = finfo->shading_start_addr / 4;
		checksum = 0;
#if defined(SHADING_CRC32_LEN)
		check_length = SHADING_CRC32_LEN;
#else
		check_length = (finfo->shading_end_addr - finfo->shading_start_addr + 1) ;
#endif
		checksum_base = finfo->shading_section_crc_addr / 4;

		if (check_base > address_boundary || checksum_base > address_boundary || check_length <= 0) {
			err("Camera: Shading address has error: start(0x%08X), end(0x%08X)",
				finfo->shading_start_addr, finfo->shading_end_addr);
			crc32_temp = false;
			goto out;
		}

		checksum = (u32)getCRC((u16 *)&buf32[check_base], check_length, NULL, NULL);
		if (checksum != buf32[checksum_base]) {
			err("Camera: CRC32 error at the Shading (0x%08X != 0x%08X)", checksum, buf32[checksum_base]);
			crc32_temp = false;
			goto out;
		}
	}

#if defined(CONFIG_COMPANION_C2_USE) && !defined(CONFIG_FRONT_COMPANION_C2_DISABLE)
	/* c2 Shading */
	if (id == SENSOR_POSITION_FRONT) {
		check_base = finfo->c2_shading_start_addr / 4;
		checksum = 0;
		check_length = (finfo->c2_shading_end_addr - finfo->c2_shading_start_addr + 1);
		checksum_base = finfo->comp_shading_section_crc_addr / 4;

		if (check_base > address_boundary || checksum_base > address_boundary || check_length <= 0) {
			err("Camera: C2 Shading address has error: start(0x%08X), end(0x%08X)",
				finfo->c2_shading_start_addr, finfo->c2_shading_end_addr);
			crc32_temp = false;
			crc32_c1_check_front = false;
			goto out;
		}

		checksum = (u32)getCRC((u16 *)&buf32[check_base], check_length, NULL, NULL);
		if (checksum != buf32[checksum_base]) {
			err("Camera: CRC32 error at the C2 Shading (0x%08X != 0x%08X)", checksum, buf32[checksum_base]);
			crc32_temp = false;
			crc32_c1_check_front = false;
			goto out;
		}
	}
#endif

#ifdef CONFIG_COMPANION_USE
	/* pdaf cal */
	if (id == SENSOR_POSITION_REAR) {
		check_base = finfo->pdaf_cal_start_addr / 4;
		checksum = 0;
		check_length = (finfo->pdaf_cal_end_addr - finfo->pdaf_cal_start_addr + 1);
		checksum_base = finfo->paf_cal_section_crc_addr / 4;

		if (check_base > address_boundary || checksum_base > address_boundary || check_length <= 0) {
			err("Camera: pdaf address has error: start(0x%08X), end(0x%08X)",
				finfo->pdaf_start_addr, finfo->pdaf_end_addr);
			crc32_temp = false;
			goto out;
		}

		checksum = (u32)getCRC((u16 *)&buf32[check_base], check_length, NULL, NULL);
		if (checksum != buf32[checksum_base]) {
			err("Camera: CRC32 error at the pdaf cal (0x%08X != 0x%08X)", checksum, buf32[checksum_base]);
			crc32_temp = false;
			goto out;
		}

		/* concord cal */
		check_base = finfo->concord_cal_start_addr / 4;
		checksum = 0;
		check_length = (finfo->concord_cal_end_addr - finfo->concord_cal_start_addr + 1);
		checksum_base = finfo->concord_cal_section_crc_addr / 4;

		if (check_base > address_boundary || checksum_base > address_boundary || check_length <= 0) {
			err("Camera: concord cal address has error: start(0x%08X), end(0x%08X)",
				finfo->concord_cal_start_addr, finfo->concord_cal_end_addr);
			crc32_c1_check = false;
			goto out;
		}

		checksum = (u32)getCRC((u16 *)&buf32[check_base], check_length, NULL, NULL);
		if (checksum != buf32[checksum_base]) {
			err("Camera: CRC32 error at the concord cal (0x%08X != 0x%08X)", checksum, buf32[checksum_base]);
			crc32_c1_check = false;
			goto out;
		}
	}
#endif

#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_OIS)
	if (id == SENSOR_POSITION_REAR) {
		/* OIS Header  */
		check_base = EEP_HEADER_OIS_START_ADDR / 4;
		checksum = 0;
		check_length = OIS_HEADER_CRC32_LEN;
		checksum_base = EEP_CHECKSUM_OIS_HEADER_ADDR / 4;

		checksum = (u32)getCRC((u16 *)&buf32[check_base], check_length, NULL, NULL);
		if (checksum != buf32[checksum_base]) {
			err("Camera: CRC32 error at the OIS Header(0x%08X != 0x%08X)",
				checksum, buf32[checksum_base]);
			crc32_ois_check = false;
			goto out;
		}

		/* OIS Cal */
		check_base = finfo->ois_cal_start_addr / 4;
		checksum = 0;
		check_length = OIS_HEADER_CRC32_CAL_LEN;
		checksum_base = EEP_CHECKSUM_OIS_CAL_ADDR / 4;

		checksum = (u32)getCRC((u16 *)&buf32[check_base], check_length, NULL, NULL);
		if (checksum != buf32[checksum_base]) {
			err("Camera: CRC32 error at the OIS Cal(0x%08X != 0x%08X)",
				checksum, buf32[checksum_base]);
			crc32_ois_check = false;
			crc32_ois_cal_check = false;
			goto out;
		}

		/* OIS Shift Data */
		check_base = finfo->ois_shift_start_addr / 4;
		checksum = 0;
		check_length = OIS_HEADER_CRC32_SHIFT_LEN;
		checksum_base = EEP_CHECKSUM_OIS_SHIFT_ADDR / 4;

		checksum = (u32)getCRC((u16 *)&buf32[check_base], check_length, NULL, NULL);
		if (checksum != buf32[checksum_base]) {
			err("Camera: CRC32 error at the OIS Shift Data(0x%08X != 0x%08X)",
				checksum, buf32[checksum_base]);
			crc32_ois_check = false;
			crc32_ois_cal_check = false;
			goto out;
		}

		/* OIS FW Set */
		check_base = finfo->ois_fw_set_start_addr / 4;
		checksum = 0;
		check_length = OIS_HEADER_CRC32_FW_SET_LEN;
		checksum_base = EEP_CHECKSUM_OIS_FW_SET_ADDR / 4;

		checksum = (u32)getCRC((u16 *)&buf32[check_base], check_length, NULL, NULL);
		if (checksum != buf32[checksum_base]) {
			err("Camera: CRC32 error at the OIS FW Set(0x%08X != 0x%08X)",
				checksum, buf32[checksum_base]);
			crc32_ois_check = false;
			crc32_ois_fw_check = false;
			goto out;
		}

		/* OIS FW Factory */
		check_base = finfo->ois_fw_factory_start_addr / 4;
		checksum = 0;
		check_length = OIS_HEADER_CRC32_FW_FACTORY_LEN;
		checksum_base = EEP_CHECKSUM_OIS_FW_FACTORY_ADDR / 4;

		checksum = (u32)getCRC((u16 *)&buf32[check_base], check_length, NULL, NULL);
		if (checksum != buf32[checksum_base]) {
			err("Camera: CRC32 error at the OIS FW Factory(0x%08X != 0x%08X)",
				checksum, buf32[checksum_base]);
			crc32_ois_check = false;
			crc32_ois_fw_check = false;
			goto out;
		}
	}
#endif
out:
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_FRONT)
	if (id == SENSOR_POSITION_FRONT) {
		crc32_check_front = crc32_temp;
		crc32_header_check_front = crc32_header_temp;
		return crc32_check_front;
	} else
#endif
	{
		crc32_check = crc32_temp;
		crc32_header_check = crc32_header_temp;
#ifdef CONFIG_COMPANION_USE
		return crc32_check && crc32_c1_check;
#elif defined(CONFIG_CAMERA_EEPROM_SUPPORT_OIS)
		return crc32_check && crc32_ois_check;
#else
		return crc32_check;
#endif
	}
}

#if defined(CONFIG_CAMERA_FROM_SUPPORT_REAR)
bool fimc_is_sec_check_fw_crc32(char *buf)
{
	u32 *buf32 = NULL;
	u32 checksum;
	u32 checksum_base;
	u32 checksum_seed;

	buf32 = (u32 *)buf;

	if (fimc_is_sec_fw_module_compare(sysfs_finfo.header_ver, FW_2P2_B))
		checksum_seed = CHECKSUM_SEED_ISP_FW_2P2_PLUS;
	else
		checksum_seed = CHECKSUM_SEED_ISP_FW_IMX240;

	info("Camera: Start checking CRC32 FW\n");

	checksum = (u32)getCRC((u16 *)&buf32[0], sysfs_finfo.fw_size, NULL, NULL);
	checksum_base = (checksum_seed & 0xffffffff) / 4;
	if (checksum != buf32[checksum_base]) {
		err("Camera: CRC32 error at the binary section (0x%08X != 0x%08X)",
					checksum, buf32[checksum_base]);
		crc32_fw_check = false;
	} else {
		crc32_fw_check = true;
	}

	info("Camera: End checking CRC32 FW\n");

	return crc32_fw_check;
}
#endif

#if defined(CONFIG_CAMERA_OTPROM_SUPPORT_FRONT)
bool fimc_is_sec_check_front_otp_crc32(char *buf)
{
	u32 *buf32 = NULL;
	u32 checksum;
	bool crc32_temp, crc32_header_temp;
	u32 checksumFromOTP;

	buf32 = (u32 *)buf;
	checksumFromOTP = buf[OTP_CHECKSUM_HEADER_ADDR_FRONT] +( buf[OTP_CHECKSUM_HEADER_ADDR_FRONT+1] << 8)
			+( buf[OTP_CHECKSUM_HEADER_ADDR_FRONT+2] << 16) + (buf[OTP_CHECKSUM_HEADER_ADDR_FRONT+3] << 24);

	/* Header data */
	checksum = (u32)getCRC((u16 *)&buf32[HEADER_START_ADDR_FRONT], HEADER_CRC32_LEN_FRONT, NULL, NULL);

	if(checksum != checksumFromOTP) {
		crc32_temp = crc32_header_temp = false;
		err("Camera: CRC32 error at the header data section (0x%08X != 0x%08X)",
					checksum, checksumFromOTP);
	} else {
		crc32_temp = crc32_header_temp = true;
		pr_info("Camera: End checking CRC32 (0x%08X = 0x%08X)",
					checksum, checksumFromOTP);
	}

	crc32_check_front = crc32_temp;
	crc32_header_check_front = crc32_header_temp;

	return crc32_temp;
}
#endif

#ifdef CAMERA_MODULE_DUALIZE
bool fimc_is_sec_check_setfile_crc32(char *buf)
{
	u32 *buf32 = NULL;
	u32 checksum;
	u32 checksum_base;
	u32 checksum_seed;

	buf32 = (u32 *)buf;

	if (fimc_is_sec_fw_module_compare(sysfs_finfo.header_ver, FW_2P2_B))
		checksum_seed = CHECKSUM_SEED_SETF_2P2_PLUS;
	else
		checksum_seed = CHECKSUM_SEED_SETF_IMX240;

	info("Camera: Start checking CRC32 Setfile\n");

	checksum = (u32)getCRC((u16 *)&buf32[0], sysfs_finfo.setfile_size, NULL, NULL);
	checksum_base = (checksum_seed & 0xffffffff) / 4;
	if (checksum != buf32[checksum_base]) {
		err("Camera: CRC32 error at the binary section (0x%08X != 0x%08X)",
					checksum, buf32[checksum_base]);
		crc32_setfile_check = false;
	} else {
		crc32_setfile_check = true;
	}

	info("Camera: End checking CRC32 Setfile\n");

	return crc32_setfile_check;
}
#endif

#ifdef CONFIG_COMPANION_USE
bool fimc_is_sec_check_companion_fw_crc32(char *buf)
{
	u32 *buf32 = NULL;
	u32 checksum;
	u32 checksum_base;
	u32 checksum_seed;

	buf32 = (u32 *)buf;

	if (fimc_is_sec_fw_module_compare(sysfs_finfo.header_ver, FW_2P2_B))
		checksum_seed = CHECKSUM_SEED_COMP_FW_2P2_PLUS;
	else
		checksum_seed = CHECKSUM_SEED_COMP_FW_IMX240;

	info("Camera: Start checking CRC32 Companion FW\n");

	checksum = (u32)getCRC((u16 *)&buf32[0], sysfs_finfo.comp_fw_size, NULL, NULL);
	checksum_base = ((checksum_seed & 0xffffffff)) / 4;
	if (checksum != buf32[checksum_base]) {
		err("Camera: CRC32 error at the binary section (0x%08X != 0x%08X)",
					checksum, buf32[checksum_base]);
		crc32_c1_fw_check = false;
	} else {
		crc32_c1_fw_check = true;
	}

	info("Camera: End checking CRC32 Companion FW\n");

	return crc32_c1_fw_check;
}
#endif

ssize_t write_data_to_file(char *name, char *buf, size_t count, loff_t *pos)
{
	struct file *fp;
	mm_segment_t old_fs;
	ssize_t tx = -ENOENT;
	int fd, old_mask;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	old_mask = sys_umask(0);

	if (force_caldata_dump) {
		sys_rmdir(name);
		fd = sys_open(name, O_WRONLY | O_CREAT | O_TRUNC | O_SYNC, 0666);
	} else {
		fd = sys_open(name, O_WRONLY | O_CREAT | O_TRUNC | O_SYNC, 0664);
	}
	if (fd < 0) {
		err("open file error: %s", name);
		sys_umask(old_mask);
		set_fs(old_fs);
		return -EINVAL;
	}

	fp = fget(fd);
	if (fp) {
		tx = vfs_write(fp, buf, count, pos);
		if (tx != count) {
			err("fail to write %s. ret %zd", name, tx);
			tx = -ENOENT;
		}

		vfs_fsync(fp, 0);
		fput(fp);
	} else {
		err("fail to get file *: %s", name);
	}

	sys_close(fd);
	sys_umask(old_mask);
	set_fs(old_fs);

	return tx;
}

ssize_t read_data_from_file(char *name, char *buf, size_t count, loff_t *pos)
{
	struct file *fp;
	mm_segment_t old_fs;
	ssize_t tx;
	int fd;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	fd = sys_open(name, O_RDONLY, 0664);
	if (fd < 0) {
		if (-ENOENT == fd)
			info("%s: file(%s) not exist!\n", __func__, name);
		else
			info("%s: error %d, failed to open %s\n", __func__, fd, name);

		set_fs(old_fs);
		return -EINVAL;
	}
	fp = fget(fd);
	if (fp) {
		tx = vfs_read(fp, buf, count, pos);
		fput(fp);
	}
	sys_close(fd);
	set_fs(old_fs);

	return count;
}

bool fimc_is_sec_file_exist(char *name)
{
	mm_segment_t old_fs;
	bool exist = true;
	int ret;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	ret = sys_access(name, 0);
	if (ret) {
		exist = false;
		if (-ENOENT == ret)
			info("%s: file(%s) not exist!\n", __func__, name);
		else
			info("%s: error %d, failed to access %s\n", __func__, ret, name);
	}

	set_fs(old_fs);
	return exist;
}

void fimc_is_sec_make_crc32_table(u32 *table, u32 id)
{
	u32 i, j, k;

	for (i = 0; i < 256; ++i) {
		k = i;
		for (j = 0; j < 8; ++j) {
			if (k & 1)
				k = (k >> 1) ^ id;
			else
				k >>= 1;
		}
		table[i] = k;
	}
}

#if 0 /* unused */
static void fimc_is_read_sensor_version(void)
{
	int ret;
	char buf[0x50];

	memset(buf, 0x0, 0x50);

	printk(KERN_INFO "+++ %s\n", __func__);

	ret = fimc_is_spi_read(buf, 0x0, 0x50);

	printk(KERN_INFO "--- %s\n", __func__);

	if (ret) {
		err("fimc_is_spi_read - can't read sensor version\n");
	}

	err("Manufacturer ID(0x40): 0x%02x\n", buf[0x40]);
	err("Pixel Number(0x41): 0x%02x\n", buf[0x41]);
}

static void fimc_is_read_sensor_version2(void)
{
	char *buf;
	char *cal_data;
	u32 cur;
	u32 count = SETFILE_SIZE/READ_SIZE;
	u32 extra = SETFILE_SIZE%READ_SIZE;

	printk(KERN_ERR "%s\n", __func__);

	buf = (char *)kmalloc(READ_SIZE, GFP_KERNEL);
	cal_data = (char *)kmalloc(SETFILE_SIZE, GFP_KERNEL);

	memset(buf, 0x0, READ_SIZE);
	memset(cal_data, 0x0, SETFILE_SIZE);

	for (cur = 0; cur < SETFILE_SIZE; cur += READ_SIZE) {
		fimc_is_spi_read(buf, cur, READ_SIZE);
		memcpy(cal_data+cur, buf, READ_SIZE);
		memset(buf, 0x0, READ_SIZE);
	}

	if (extra != 0) {
		fimc_is_spi_read(buf, cur, extra);
		memcpy(cal_data+cur, buf, extra);
		memset(buf, 0x0, extra);
	}

	info("Manufacturer ID(0x40): 0x%02x\n", cal_data[0x40]);
	info("Pixel Number(0x41): 0x%02x\n", cal_data[0x41]);

	info("Manufacturer ID(0x4FE7): 0x%02x\n", cal_data[0x4FE7]);
	info("Pixel Number(0x4FE8): 0x%02x\n", cal_data[0x4FE8]);
	info("Manufacturer ID(0x4FE9): 0x%02x\n", cal_data[0x4FE9]);
	info("Pixel Number(0x4FEA): 0x%02x\n", cal_data[0x4FEA]);

	kfree(buf);
	kfree(cal_data);
}

static int fimc_is_get_cal_data(void)
{
	int err = 0;
	struct file *fp = NULL;
	mm_segment_t old_fs;
	long ret = 0;
	u8 mem0 = 0, mem1 = 0;
	u32 CRC = 0;
	u32 DataCRC = 0;
	u32 IntOriginalCRC = 0;
	u32 crc_index = 0;
	int retryCnt = 2;
	u32 header_crc32 =	0x1000;
	u32 oem_crc32 =		0x2000;
	u32 awb_crc32 =		0x3000;
	u32 shading_crc32 = 0x6000;
	u32 shading_header = 0x22C0;

	char *cal_data;

	crc32_check = true;
	printk(KERN_INFO "%s\n", __func__);
	printk(KERN_INFO "+++ %s\n", __func__);

	fimc_is_spi_read(cal_map_version, 0x60, 0x4);
	printk(KERN_INFO "cal_map_version = %.4s\n", cal_map_version);

	if (cal_map_version[3] == '5') {
		shading_crc32 = 0x6000;
		shading_header = 0x22C0;
	} else if (cal_map_version[3] == '6') {
		shading_crc32 = 0x4000;
		shading_header = 0x920;
	} else {
		shading_crc32 = 0x5000;
		shading_header = 0x22C0;
	}

	/* Make CRC Table */
	fimc_is_sec_make_crc32_table((u32 *)&crc_table, 0xEDB88320);


	retry:
		cal_data = (char *)kmalloc(SETFILE_SIZE, GFP_KERNEL);

		memset(cal_data, 0x0, SETFILE_SIZE);

		mem0 = 0, mem1 = 0;
		CRC = 0;
		DataCRC = 0;
		IntOriginalCRC = 0;
		crc_index = 0;

		fimc_is_spi_read(cal_data, 0, SETFILE_SIZE);

		CRC = ~CRC;
		for (crc_index = 0; crc_index < (0x80)/2; crc_index++) {
			/*low byte*/
			mem0 = (unsigned char)(cal_data[crc_index*2] & 0x00ff);
			/*high byte*/
			mem1 = (unsigned char)((cal_data[crc_index*2+1]) & 0x00ff);
			CRC = crc_table[(CRC ^ (mem0)) & 0xFF] ^ (CRC >> 8);
			CRC = crc_table[(CRC ^ (mem1)) & 0xFF] ^ (CRC >> 8);
		}
		CRC = ~CRC;


		DataCRC = (CRC&0x000000ff)<<24;
		DataCRC += (CRC&0x0000ff00)<<8;
		DataCRC += (CRC&0x00ff0000)>>8;
		DataCRC += (CRC&0xff000000)>>24;
		printk(KERN_INFO "made HEADER CSC value by S/W = 0x%x\n", DataCRC);

		IntOriginalCRC = (cal_data[header_crc32-4]&0x00ff)<<24;
		IntOriginalCRC += (cal_data[header_crc32-3]&0x00ff)<<16;
		IntOriginalCRC += (cal_data[header_crc32-2]&0x00ff)<<8;
		IntOriginalCRC += (cal_data[header_crc32-1]&0x00ff);
		printk(KERN_INFO "Original HEADER CRC Int = 0x%x\n", IntOriginalCRC);

		if (IntOriginalCRC != DataCRC)
			crc32_check = false;

		CRC = 0;
		CRC = ~CRC;
		for (crc_index = 0; crc_index < (0xC0)/2; crc_index++) {
			/*low byte*/
			mem0 = (unsigned char)(cal_data[0x1000 + crc_index*2] & 0x00ff);
			/*high byte*/
			mem1 = (unsigned char)((cal_data[0x1000 + crc_index*2+1]) & 0x00ff);
			CRC = crc_table[(CRC ^ (mem0)) & 0xFF] ^ (CRC >> 8);
			CRC = crc_table[(CRC ^ (mem1)) & 0xFF] ^ (CRC >> 8);
		}
		CRC = ~CRC;


		DataCRC = (CRC&0x000000ff)<<24;
		DataCRC += (CRC&0x0000ff00)<<8;
		DataCRC += (CRC&0x00ff0000)>>8;
		DataCRC += (CRC&0xff000000)>>24;
		printk(KERN_INFO "made OEM CSC value by S/W = 0x%x\n", DataCRC);

		IntOriginalCRC = (cal_data[oem_crc32-4]&0x00ff)<<24;
		IntOriginalCRC += (cal_data[oem_crc32-3]&0x00ff)<<16;
		IntOriginalCRC += (cal_data[oem_crc32-2]&0x00ff)<<8;
		IntOriginalCRC += (cal_data[oem_crc32-1]&0x00ff);
		printk(KERN_INFO "Original OEM CRC Int = 0x%x\n", IntOriginalCRC);

		if (IntOriginalCRC != DataCRC)
			crc32_check = false;


		CRC = 0;
		CRC = ~CRC;
		for (crc_index = 0; crc_index < (0x20)/2; crc_index++) {
			/*low byte*/
			mem0 = (unsigned char)(cal_data[0x2000 + crc_index*2] & 0x00ff);
			/*high byte*/
			mem1 = (unsigned char)((cal_data[0x2000 + crc_index*2+1]) & 0x00ff);
			CRC = crc_table[(CRC ^ (mem0)) & 0xFF] ^ (CRC >> 8);
			CRC = crc_table[(CRC ^ (mem1)) & 0xFF] ^ (CRC >> 8);
		}
		CRC = ~CRC;


		DataCRC = (CRC&0x000000ff)<<24;
		DataCRC += (CRC&0x0000ff00)<<8;
		DataCRC += (CRC&0x00ff0000)>>8;
		DataCRC += (CRC&0xff000000)>>24;
		printk(KERN_INFO "made AWB CSC value by S/W = 0x%x\n", DataCRC);

		IntOriginalCRC = (cal_data[awb_crc32-4]&0x00ff)<<24;
		IntOriginalCRC += (cal_data[awb_crc32-3]&0x00ff)<<16;
		IntOriginalCRC += (cal_data[awb_crc32-2]&0x00ff)<<8;
		IntOriginalCRC += (cal_data[awb_crc32-1]&0x00ff);
		printk(KERN_INFO "Original AWB CRC Int = 0x%x\n", IntOriginalCRC);

		if (IntOriginalCRC != DataCRC)
			crc32_check = false;


		CRC = 0;
		CRC = ~CRC;
		for (crc_index = 0; crc_index < (shading_header)/2; crc_index++) {

			/*low byte*/
			mem0 = (unsigned char)(cal_data[0x3000 + crc_index*2] & 0x00ff);
			/*high byte*/
			mem1 = (unsigned char)((cal_data[0x3000 + crc_index*2+1]) & 0x00ff);
			CRC = crc_table[(CRC ^ (mem0)) & 0xFF] ^ (CRC >> 8);
			CRC = crc_table[(CRC ^ (mem1)) & 0xFF] ^ (CRC >> 8);
		}
		CRC = ~CRC;


		DataCRC = (CRC&0x000000ff)<<24;
		DataCRC += (CRC&0x0000ff00)<<8;
		DataCRC += (CRC&0x00ff0000)>>8;
		DataCRC += (CRC&0xff000000)>>24;
		printk(KERN_INFO "made SHADING CSC value by S/W = 0x%x\n", DataCRC);

		IntOriginalCRC = (cal_data[shading_crc32-4]&0x00ff)<<24;
		IntOriginalCRC += (cal_data[shading_crc32-3]&0x00ff)<<16;
		IntOriginalCRC += (cal_data[shading_crc32-2]&0x00ff)<<8;
		IntOriginalCRC += (cal_data[shading_crc32-1]&0x00ff);
		printk(KERN_INFO "Original SHADING CRC Int = 0x%x\n", IntOriginalCRC);

		if (IntOriginalCRC != DataCRC)
			crc32_check = false;


		old_fs = get_fs();
		set_fs(KERNEL_DS);

		if (crc32_check == true) {
			printk(KERN_INFO "make cal_data.bin~~~~ \n");
			fp = filp_open(FIMC_IS_CAL_SDCARD, O_WRONLY|O_CREAT|O_TRUNC, 0644);
			if (IS_ERR(fp) || fp == NULL) {
				printk(KERN_INFO "failed to open %s, err %ld\n",
					FIMC_IS_CAL_SDCARD, PTR_ERR(fp));
				err = -EINVAL;
				goto out;
			}

			ret = vfs_write(fp, (char __user *)cal_data,
				SETFILE_SIZE, &fp->f_pos);

		} else {
			if (retryCnt > 0) {
				set_fs(old_fs);
				retryCnt--;
				goto retry;
			}
		}

/*
		{
			fp = filp_open(FIMC_IS_CAL_SDCARD, O_WRONLY|O_CREAT|O_TRUNC, 0644);
			if (IS_ERR(fp) || fp == NULL) {
				printk(KERN_INFO "failed to open %s, err %ld\n",
					FIMC_IS_CAL_SDCARD, PTR_ERR(fp));
				err = -EINVAL;
				goto out;
			}

			ret = vfs_write(fp, (char __user *)cal_data,
				SETFILE_SIZE, &fp->f_pos);

		}
*/

		if (fp != NULL)
			filp_close(fp, current->files);

	out:
		set_fs(old_fs);
		kfree(cal_data);
		return err;

}

#endif

/**
 * fimc_is_sec_ldo_enabled: check whether the ldo has already been enabled.
 *
 * @ return: true, false or error value
 */
int fimc_is_sec_ldo_enabled(struct device *dev, char *name) {
	struct regulator *regulator = NULL;
	int enabled = 0;

	regulator = regulator_get_optional(dev, name);
	if (IS_ERR_OR_NULL(regulator)) {
		err("%s : regulator_get(%s) fail", __func__, name);
		return -EINVAL;
	}

	enabled = regulator_is_enabled(regulator);

	regulator_put(regulator);

	return enabled;
}

int fimc_is_sec_ldo_enable(struct device *dev, char *name, bool on)
{
	struct regulator *regulator = NULL;
	int ret = 0;

	regulator = regulator_get(dev, name);
	if (IS_ERR_OR_NULL(regulator)) {
		err("%s : regulator_get(%s) fail", __func__, name);
		return -EINVAL;
	}

	if (on) {
		if (regulator_is_enabled(regulator)) {
			pr_warning("%s: regulator is already enabled\n", name);
			goto exit;
		}

		ret = regulator_enable(regulator);
		if (ret) {
			err("%s : regulator_enable(%s) fail", __func__, name);
			goto exit;
		}
	} else {
		if (!regulator_is_enabled(regulator)) {
			pr_warning("%s: regulator is already disabled\n", name);
			goto exit;
		}

		ret = regulator_disable(regulator);
		if (ret) {
			err("%s : regulator_disable(%s) fail", __func__, name);
			goto exit;
		}
	}

exit:
	regulator_put(regulator);

	return ret;
}

int fimc_is_sec_rom_power_on(struct fimc_is_core *core, int position)
{
	int ret = 0;
	struct exynos_platform_fimc_is_module *module_pdata;
	struct fimc_is_module_enum *module = NULL;
	int sensor_id = 0;
	int i = 0;
	struct fimc_is_vender_specific *specific = core->vender.private_data;
#if defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_FRONT)
	struct fimc_is_device_sensor *device_sensor = NULL;
#endif

	info("%s: Sensor position = %d.", __func__, position);

	if(position == SENSOR_POSITION_REAR)
		sensor_id = specific->rear_sensor_id;
	else if (position == SENSOR_POSITION_REAR2)
		sensor_id = specific->rear_second_sensor_id;
	else if (position == SENSOR_POSITION_REAR3)
		sensor_id = specific->rear_third_sensor_id;
	else
		sensor_id = specific->front_sensor_id;

	for (i = 0; i < FIMC_IS_SENSOR_COUNT; i++) {
		fimc_is_search_sensor_module_with_position(&core->sensor[i], sensor_id, position, &module);
		if (module) {
#if defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_FRONT)
			device_sensor = &core->sensor[i];
#endif
			break;
		}
	}

	if (!module) {
		err("%s: Could not find sensor id.", __func__);
		ret = -EINVAL;
		goto p_err;
	}

	module_pdata = module->pdata;

	if (!module_pdata->gpio_cfg) {
		err("gpio_cfg is NULL");
		ret = -EINVAL;
		goto p_err;
	}

#if defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_FRONT)
	if (device_sensor) {
		ret = fimc_is_sensor_mclk_on(device_sensor, GPIO_SCENARIO_ON, module->pdata->mclk_ch);
		if (ret) {
			err("mclk_on is fail(%d)", ret);
			goto p_err;
		}
	}
#endif

	ret = module_pdata->gpio_cfg(module->dev, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_ON);
	if (ret) {
		err("gpio_cfg is fail(%d)", ret);
		goto p_err;
	}

p_err:
	return ret;
}

int fimc_is_sec_rom_power_off(struct fimc_is_core *core, int position)
{
	int ret = 0;
	struct exynos_platform_fimc_is_module *module_pdata;
	struct fimc_is_vender_specific *specific = core->vender.private_data;
	struct fimc_is_module_enum *module = NULL;
	int sensor_id = 0;
	int i = 0;
#if defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_FRONT)
	struct fimc_is_device_sensor *device_sensor = NULL;
#endif

	info("%s: Sensor position = %d.", __func__, position);

	if(position == SENSOR_POSITION_REAR)
		sensor_id = specific->rear_sensor_id;
	else if (position == SENSOR_POSITION_REAR2)
		sensor_id = specific->rear_second_sensor_id;
	else if (position == SENSOR_POSITION_REAR3)
		sensor_id = specific->rear_third_sensor_id;
	else
		sensor_id = specific->front_sensor_id;

	for (i = 0; i < FIMC_IS_SENSOR_COUNT; i++) {
		fimc_is_search_sensor_module_with_position(&core->sensor[i], sensor_id, position, &module);
		if (module) {
#if defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_FRONT)
			device_sensor = &core->sensor[i];
#endif
			break;
		}
	}

	if (!module) {
		err("%s: Could not find sensor id.", __func__);
		ret = -EINVAL;
		goto p_err;
	}

	module_pdata = module->pdata;

	if (!module_pdata->gpio_cfg) {
		err("gpio_cfg is NULL");
		ret = -EINVAL;
		goto p_err;
	}

	ret = module_pdata->gpio_cfg(module->dev, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_OFF);
	if (ret) {
		err("gpio_cfg is fail(%d)", ret);
		goto p_err;
	}

#if defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_FRONT)
	if (device_sensor) {
		ret = fimc_is_sensor_mclk_off(device_sensor, GPIO_SCENARIO_OFF, module->pdata->mclk_ch);
		if (ret) {
			err("mclk_on is fail(%d)", ret);
			goto p_err;
		}
	}
#endif

p_err:
	return ret;
}

#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR) || defined(CONFIG_CAMERA_EEPROM_SUPPORT_FRONT) || defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR2) || defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR3) \
	|| defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_FRONT)
int fimc_is_i2c_read(struct i2c_client *client, void *buf, u32 addr, size_t size)
{
	const u32 addr_size = 2, max_retry = 5;
	u8 addr_buf[addr_size];
	int retries = max_retry;
	int ret = 0;

	if (!client) {
		info("%s: client is null\n", __func__);
		return -ENODEV;
	}

	/* Send addr */
	addr_buf[0] = ((u16)addr) >> 8;
	addr_buf[1] = (u8)addr;

	for (retries = max_retry; retries > 0; retries--) {
		ret = i2c_master_send(client, addr_buf, addr_size);
		if (likely(addr_size == ret))
			break;

		info("%s: i2c_master_send failed(%d), try %d\n", __func__, ret, retries);
		usleep_range(1000, 1000);
	}

	if (unlikely(ret <= 0)) {
		err("%s: error %d, fail to write 0x%04X", __func__, ret, addr);
		return ret ? ret : -ETIMEDOUT;
	}

	/* Receive data */
	for (retries = max_retry; retries > 0; retries--) {
		ret = i2c_master_recv(client, buf, size);
		if (likely(ret == size))
			break;

		info("%s: i2c_master_recv failed(%d), try %d\n", __func__,  ret, retries);
		usleep_range(1000, 1000);
	}

	if (unlikely(ret <= 0)) {
		err("%s: error %d, fail to read 0x%04X", __func__, ret, addr);
		return ret ? ret : -ETIMEDOUT;
	}

	return 0;
}

int fimc_is_i2c_write(struct i2c_client *client, u16 addr, u8 data)
{
	const u32 write_buf_size = 3, max_retry = 5;
	u8 write_buf[write_buf_size];
	int retries = max_retry;
	int ret = 0;

	if (!client) {
		pr_info("%s: client is null\n", __func__);
		return -ENODEV;
	}

	/* Send addr+data */
	write_buf[0] = ((u16)addr) >> 8;
	write_buf[1] = (u8)addr;
	write_buf[2] = data;


	for (retries = max_retry; retries > 0; retries--) {
		ret = i2c_master_send(client, write_buf, write_buf_size);
		if (likely(write_buf_size == ret))
			break;

		pr_info("%s: i2c_master_send failed(%d), try %d\n", __func__, ret, retries);
		usleep_range(1000, 1000);
	}

	if (unlikely(ret <= 0)) {
		pr_err("%s: error %d, fail to write 0x%04X\n", __func__, ret, addr);
		return ret ? ret : -ETIMEDOUT;
	}

	return 0;
}

static int fimc_is_i2c_config(struct i2c_client *client, bool onoff)
{
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR) || defined(CONFIG_CAMERA_EEPROM_SUPPORT_FRONT) \
	|| defined (CONFIG_CAMERA_EEPROM_SUPPORT_REAR2) || defined (CONFIG_CAMERA_EEPROM_SUPPORT_REAR3)
	struct device *i2c_dev = client->dev.parent->parent;
	struct pinctrl *pinctrl_i2c = NULL;

	info("(%s):onoff(%d)\n", __func__, onoff);
	if (onoff) {
		/* ON */
		pinctrl_i2c = devm_pinctrl_get_select(i2c_dev, "on_i2c");
		if (IS_ERR_OR_NULL(pinctrl_i2c)) {
			printk(KERN_ERR "%s: Failed to configure i2c pin\n", __func__);
		} else {
			devm_pinctrl_put(pinctrl_i2c);
		}
	} else {
		/* OFF */
		pinctrl_i2c = devm_pinctrl_get_select(i2c_dev, "off_i2c");
		if (IS_ERR_OR_NULL(pinctrl_i2c)) {
			printk(KERN_ERR "%s: Failed to configure i2c pin\n", __func__);
		} else {
			devm_pinctrl_put(pinctrl_i2c);
		}
	}
#endif
	return 0;
}
#endif

#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR) || defined(CONFIG_CAMERA_EEPROM_SUPPORT_FRONT) \
	|| defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR2) || defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR3)
int fimc_is_sec_read_eeprom_header(struct device *dev, int position)
{
	int ret = 0;
	struct fimc_is_core *core = dev_get_drvdata(fimc_is_dev);
	struct fimc_is_vender_specific *specific;
	u8 header_version[FIMC_IS_HEADER_VER_SIZE + 1] = {0, };
	struct i2c_client *client;

	specific = core->vender.private_data;
	client = specific->eeprom_client[position];

	if (!client) {
		err("eeprom i2c client is NULL\n");
		ret = -EINVAL;
		goto exit;
	}

#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_FRONT)
	if(position == SENSOR_POSITION_FRONT) {
		if (specific->use_ois_hsi2c) {
			fimc_is_i2c_config(client, true);
		}
	} else
#endif
	{
		fimc_is_i2c_config(client, true);
	}

	if(position == SENSOR_POSITION_FRONT) {
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_FRONT)
		ret = fimc_is_i2c_read(client, header_version,
				EEP_I2C_HEADER_VERSION_START_ADDR_FRONT, FIMC_IS_HEADER_VER_SIZE);
#endif
	} else if (position == SENSOR_POSITION_REAR2) {
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR2)
		ret = fimc_is_i2c_read(client, header_version,
				EEP_I2C_HEADER_VERSION_START_ADDR_REAR2, FIMC_IS_HEADER_VER_SIZE);
#endif
	} else if (position == SENSOR_POSITION_REAR3) {
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR3)
		ret = fimc_is_i2c_read(client, header_version,
				EEP_I2C_HEADER_VERSION_START_ADDR_REAR3, FIMC_IS_HEADER_VER_SIZE);
#endif
	} else {
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR)
		ret = fimc_is_i2c_read(client, header_version,
				EEP_I2C_HEADER_VERSION_START_ADDR, FIMC_IS_HEADER_VER_SIZE);
#endif

	}

#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_FRONT)
	if(position == SENSOR_POSITION_FRONT) {
		if (specific->use_ois_hsi2c) {
			fimc_is_i2c_config(client, false);
		}
	} else
#endif
	{
		fimc_is_i2c_config(client, false);
	}

	if (unlikely(ret)) {
		err("failed to fimc_is_i2c_read for header version (%d)\n", ret);
		ret = -EINVAL;
	}

	memcpy(sysfs_finfo.header_ver, header_version, FIMC_IS_HEADER_VER_SIZE);
	sysfs_finfo.header_ver[FIMC_IS_HEADER_VER_SIZE] = '\0';

exit:
	return ret;
}

int fimc_is_sec_readcal_eeprom(struct device *dev, int position)
{
	int ret = 0;
	char *buf = NULL;
	int retry = FIMC_IS_CAL_RETRY_CNT;
	struct fimc_is_core *core = dev_get_drvdata(fimc_is_dev);
	struct fimc_is_from_info *finfo = NULL;
	struct fimc_is_vender_specific *specific = core->vender.private_data;
	int cal_size = 0;
	struct i2c_client *client = NULL;
	struct file *key_fp = NULL;
	struct file *dump_fp = NULL;
	mm_segment_t old_fs;
	loff_t pos = 0;

#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_FRONT)
	if (position == SENSOR_POSITION_FRONT) {
		finfo = &sysfs_finfo_front;
		fimc_is_sec_get_front_cal_buf(&buf);
		cal_size = FIMC_IS_MAX_CAL_SIZE_FRONT;
	} else
#endif
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR2)
	if (position == SENSOR_POSITION_REAR2) {
		finfo = &sysfs_finfo_rear2;
		fimc_is_sec_get_rear2_cal_buf(&buf);
		cal_size = FIMC_IS_MAX_CAL_SIZE_REAR2;
	} else
#endif
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR3)
	if (position == SENSOR_POSITION_REAR3) {
		finfo = &sysfs_finfo_rear3;
		fimc_is_sec_get_rear3_cal_buf(&buf);
		cal_size = FIMC_IS_MAX_CAL_SIZE_REAR3;
	} else
#endif
	if (position == SENSOR_POSITION_REAR) {
		finfo = &sysfs_finfo;
		fimc_is_sec_get_cal_buf(&buf);
		cal_size = FIMC_IS_MAX_CAL_SIZE;
	}

	client = specific->eeprom_client[position];

	if (!client) {
		err("eeprom i2c client is NULL\n");
		ret = -EINVAL;
		goto exit;
	}

#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_FRONT)
	if(position == SENSOR_POSITION_FRONT) {
		if (specific->use_ois_hsi2c) {
			fimc_is_i2c_config(client, true);
		}
	} else
#endif
	{
		fimc_is_i2c_config(client, true);
	}

	if (position == SENSOR_POSITION_FRONT) {
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_FRONT)
		ret = fimc_is_i2c_read(client, finfo->cal_map_ver,
						EEP_I2C_HEADER_CAL_MAP_VER_START_ADDR_FRONT,
						FIMC_IS_CAL_MAP_VER_SIZE);
		ret = fimc_is_i2c_read(client, finfo->header_ver,
						EEP_I2C_HEADER_VERSION_START_ADDR_FRONT,
						FIMC_IS_HEADER_VER_SIZE);
#endif
	} else if (position == SENSOR_POSITION_REAR2){
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR2)
		ret = fimc_is_i2c_read(client, finfo->cal_map_ver,
					EEP_I2C_HEADER_CAL_MAP_VER_START_ADDR_REAR2,
					FIMC_IS_CAL_MAP_VER_SIZE);
		ret = fimc_is_i2c_read(client, finfo->header_ver,
					EEP_I2C_HEADER_VERSION_START_ADDR_REAR2,
					FIMC_IS_HEADER_VER_SIZE);
#endif
	} else if (position == SENSOR_POSITION_REAR3){
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR3)
		ret = fimc_is_i2c_read(client, finfo->cal_map_ver,
					EEP_I2C_HEADER_CAL_MAP_VER_START_ADDR_REAR3,
					FIMC_IS_CAL_MAP_VER_SIZE);
		ret = fimc_is_i2c_read(client, finfo->header_ver,
					EEP_I2C_HEADER_VERSION_START_ADDR_REAR3,
					FIMC_IS_HEADER_VER_SIZE);
#endif
	} else {
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR)
		ret = fimc_is_i2c_read(client, finfo->cal_map_ver,
						EEP_I2C_HEADER_CAL_MAP_VER_START_ADDR,
						FIMC_IS_CAL_MAP_VER_SIZE);
		ret = fimc_is_i2c_read(client, finfo->header_ver,
						EEP_I2C_HEADER_VERSION_START_ADDR,
						FIMC_IS_HEADER_VER_SIZE);
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_OIS)
		ret = fimc_is_i2c_read(client, finfo->ois_cal_ver,
						EEP_HEADER_OIS_CAL_VER_START_ADDR,
						FIMC_IS_OIS_CAL_VER_SIZE);
		ret = fimc_is_i2c_read(client, finfo->ois_fw_ver,
						EEP_HEADER_OIS_FW_VER_START_ADDR,
						FIMC_IS_OIS_FW_VER_SIZE);
#if defined(CONFIG_SEC_FACTORY)
		fimc_is_ois_factory_read_IC_ROM_checksum(core);
#endif
#endif
#endif
	}

#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_FRONT)
	if(position == SENSOR_POSITION_FRONT) {
		if (specific->use_ois_hsi2c) {
			fimc_is_i2c_config(client, false);
		}
	} else
#endif
	{
		fimc_is_i2c_config(client, false);
	}

	if (unlikely(ret)) {
		err("failed to fimc_is_i2c_read (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}

	if(!finfo) {
		err("%s:finfo is null\n", __func__);
		ret = -EINVAL;
		goto exit;
	}

	printk(KERN_INFO "Camera: EEPROM Cal map_version = %c%c%c%c\n", finfo->cal_map_ver[0],
			finfo->cal_map_ver[1], finfo->cal_map_ver[2], finfo->cal_map_ver[3]);

	if (!fimc_is_sec_check_from_ver(core, position)) {
		info("Camera: Do not read eeprom cal data. EEPROM version is low.\n");
		return 0;
	}

#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_OIS)
	if (position == SENSOR_POSITION_REAR) {
		printk(KERN_INFO "Camera: EEPROM OIS Cal version = %c%c%c%c\n", finfo->ois_cal_ver[0],
				finfo->ois_cal_ver[1], finfo->ois_cal_ver[2], finfo->ois_cal_ver[3]);
		printk(KERN_INFO "Camera: EEPROM OIS FW version =  %s\n", finfo->ois_fw_ver);
	}
#endif
crc_retry:

	/* read cal data */
	info("Camera: I2C read cal data\n");
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_FRONT)
	if(position == SENSOR_POSITION_FRONT) {
		if (specific->use_ois_hsi2c) {
			fimc_is_i2c_config(client, true);
		}
	} else
#endif
	{
		fimc_is_i2c_config(client, true);
	}

	fimc_is_i2c_read(client, buf, 0x0, cal_size);

#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_FRONT)
	if(position == SENSOR_POSITION_FRONT) {
		if (specific->use_ois_hsi2c) {
			fimc_is_i2c_config(client, false);
		}
	} else
#endif
	{
		fimc_is_i2c_config(client, false);
	}

	if (position == SENSOR_POSITION_FRONT) {
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_FRONT)
		info("FRONT EEPROM header version = %s\n", finfo->header_ver);
#if defined(EEP_HEADER_OEM_START_ADDR_FRONT)
		finfo->oem_start_addr = *((u32 *)&buf[EEP_HEADER_OEM_START_ADDR_FRONT]);
		finfo->oem_end_addr = *((u32 *)&buf[EEP_HEADER_OEM_END_ADDR_FRONT]);
		info("OEM start = 0x%08x, end = 0x%08x\n",
			(finfo->oem_start_addr), (finfo->oem_end_addr));
#endif
		finfo->awb_start_addr = *((u32 *)&buf[EEP_HEADER_AWB_START_ADDR_FRONT]);
		finfo->awb_end_addr = *((u32 *)&buf[EEP_HEADER_AWB_END_ADDR_FRONT]);
		info("AWB start = 0x%08x, end = 0x%08x\n",
			(finfo->awb_start_addr), (finfo->awb_end_addr));
		finfo->shading_start_addr = *((u32 *)&buf[EEP_HEADER_AP_SHADING_START_ADDR_FRONT]);
		finfo->shading_end_addr = *((u32 *)&buf[EEP_HEADER_AP_SHADING_END_ADDR_FRONT]);
		info("Shading start = 0x%08x, end = 0x%08x\n",
			(finfo->shading_start_addr), (finfo->shading_end_addr));
		if (finfo->shading_end_addr > 0x3AFF) {
			err("Shading end_addr has error!! 0x%08x", finfo->shading_end_addr);
			finfo->shading_end_addr = 0x3AFF;
		}
#if (defined(CONFIG_COMPANION_C2_USE) || defined(CONFIG_COMPANION_C3_USE)) && !defined(CONFIG_FRONT_COMPANION_C2_DISABLE)
		finfo->c2_shading_start_addr = *((u32 *)&buf[EEP_HEADER_C2_SHADING_START_ADDR_FRONT]);
		finfo->c2_shading_end_addr = *((u32 *)&buf[EEP_HEADER_C2_SHADING_END_ADDR_FRONT]);
		info("c2_shading start = 0x%08x, end = 0x%08x\n",
				(finfo->c2_shading_start_addr), (finfo->c2_shading_end_addr));
		if (finfo->c2_shading_end_addr > 0x1CFF) {
			err("C2 Shading end_addr has error!! 0x%08x", finfo->c2_shading_end_addr);
			finfo->c2_shading_end_addr = 0x1CFF;
		}

		/* C2 SHADING Data : Module/Manufacturer Information */
		memcpy(finfo->c2_shading_ver, &buf[EEP_C2_SHADING_VER_START_ADDR_FRONT], FIMC_IS_SHADING_VER_SIZE);
		finfo->shading_ver[FIMC_IS_SHADING_VER_SIZE] = '\0';
		finfo->comp_shading_section_crc_addr = EEP_CHECKSUM_C2_SHADING_ADDR_FRONT;

		finfo->lsc_i0_gain_addr = EEP_C2_SHADING_LSC_I0_GAIN_ADDR_FRONT;
		info("Shading lsc_i0 start = 0x%08x\n", finfo->lsc_i0_gain_addr);
		finfo->lsc_j0_gain_addr = EEP_C2_SHADING_LSC_J0_GAIN_ADDR_FRONT;
		info("Shading lsc_j0 start = 0x%08x\n", finfo->lsc_j0_gain_addr);
		finfo->lsc_a_gain_addr = EEP_C2_SHADING_LSC_A_GAIN_ADDR_FRONT;
		info("Shading lsc_a start = 0x%08x\n", finfo->lsc_a_gain_addr);
		finfo->lsc_k4_gain_addr = EEP_C2_SHADING_LSC_K4_GAIN_ADDR_FRONT;
		info("Shading lsc_k4 start = 0x%08x\n", finfo->lsc_k4_gain_addr);
		finfo->lsc_scale_gain_addr = EEP_C2_SHADING_LSC_SCALE_GAIN_ADDR_FRONT;
		info("Shading lsc_scale start = 0x%08x\n", finfo->lsc_scale_gain_addr);
		finfo->grasTuning_AwbAshCord_N_addr = EEP_C2_SHADING_GRASTUNING_AWB_ASH_CORD_ADDR_FRONT;
		info("Shading grasTuning_AwbAshCord_N start = 0x%08x\n", finfo->grasTuning_AwbAshCord_N_addr);
		finfo->grasTuning_awbAshCordIndexes_N_addr = EEP_C2_SHADING_GRASTUNING_AWB_ASH_CORD_INDEX_ADDR_FRONT;
		info("Shading grasTuning_awbAshCordIndexes_N start = 0x%08x\n",
				finfo->grasTuning_awbAshCordIndexes_N_addr);
		finfo->grasTuning_GASAlpha_M__N_addr = EEP_C2_SHADING_GRASTUNING_GAS_ALPHA_ADDR_FRONT;
		info("Shading grasTuning_GASAlpha_M__N_addr start = 0x%08x\n", finfo->grasTuning_GASAlpha_M__N_addr);
		finfo->grasTuning_GASBeta_M__N_addr = EEP_C2_SHADING_GRASTUNING_GAS_BETA_ADDR_FRONT;
		info("Shading grasTuning_GASBeta_M__N start = 0x%08x\n", finfo->grasTuning_GASBeta_M__N_addr);
		finfo->grasTuning_GASOutdoorAlpha_N_addr = EEP_C2_SHADING_GRASTUNING_GAS_OUTDOOR_ALPHA_ADDR_FRONT;
		info("Shading grasTuning_GASOutdoorAlpha_N start = 0x%08x\n",
				finfo->grasTuning_GASOutdoorAlpha_N_addr);
		finfo->grasTuning_GASOutdoorBeta_N_addr = EEP_C2_SHADING_GRASTUNING_GAS_OUTDOOR_BETA_ADDR_FRONT;
		info("Shading grasTuning_GASOutdoorBeta_N start = 0x%08x\n", finfo->grasTuning_GASOutdoorBeta_N_addr);
		finfo->grasTuning_GASIndoorAlpha_N_addr = EEP_C2_SHADING_GRASTUNING_GAS_INDOOR_ALPHA_ADDR_FRONT;
		info("Shading grasTuning_GASIndoorAlpha_N start = 0x%08x\n", finfo->grasTuning_GASIndoorAlpha_N_addr);
		finfo->grasTuning_GASIndoorBeta_N_addr = EEP_C2_SHADING_GRASTUNING_GAS_INDOOR_BETA_ADDR_FRONT;
		info("Shading grasTuning_GASIndoorBeta_N start = 0x%08x\n", finfo->grasTuning_GASIndoorBeta_N_addr);

		finfo->lsc_gain_start_addr = EEP_C2_SHADING_LSC_GAIN_START_ADDR_FRONT;
		finfo->lsc_gain_end_addr = EEP_C2_SHADING_LSC_GAIN_END_ADDR_FRONT;
		info("LSC start = 0x%04x, end = 0x%04x\n", finfo->lsc_gain_start_addr, finfo->lsc_gain_end_addr);
		finfo->lsc_gain_crc_addr = EEP_C2_SHADING_LSC_GAIN_CRC_ADDR_FRONT;
		info("lsc_gain_crc_addr = 0x%04x,\n", finfo->lsc_gain_crc_addr);
		finfo->lsc_parameter_crc_addr = EEP_C2_SHADING_LSC_PARAMETER_CRC_ARRD_FRONT;
		info("lsc_parameter_crc_addr = 0x%04x,\n", finfo->lsc_parameter_crc_addr);
		info(" Module ver : %c\n", finfo->header_ver[FW_VERSION_INFO]);
#endif
		/* HEARDER Data : Module/Manufacturer Information */
		memcpy(finfo->header_ver, &buf[EEP_HEADER_VERSION_START_ADDR_FRONT], FIMC_IS_HEADER_VER_SIZE);
		finfo->header_ver[FIMC_IS_HEADER_VER_SIZE] = '\0';
		/* HEARDER Data : Cal Map Version */
		memcpy(finfo->cal_map_ver,
		       &buf[EEP_HEADER_CAL_MAP_VER_START_ADDR_FRONT], FIMC_IS_CAL_MAP_VER_SIZE);
#ifdef EEP_HEADER_MODULE_ID_ADDR_FRONT
		memcpy(finfo->module_id, &buf[EEP_HEADER_MODULE_ID_ADDR_FRONT], FIMC_IS_MODULE_ID_SIZE);
#else
		memset(finfo->module_id, 0x0, FIMC_IS_MODULE_ID_SIZE);
#endif
		memcpy(finfo->project_name,
		       &buf[EEP_HEADER_PROJECT_NAME_START_ADDR_FRONT], FIMC_IS_PROJECT_NAME_SIZE);
		finfo->project_name[FIMC_IS_PROJECT_NAME_SIZE] = '\0';
		finfo->header_section_crc_addr = EEP_CHECKSUM_HEADER_ADDR_FRONT;
#if defined(EEP_HEADER_OEM_START_ADDR_FRONT)
		/* OEM Data : Module/Manufacturer Information */
		memcpy(finfo->oem_ver, &buf[EEP_OEM_VER_START_ADDR_FRONT], FIMC_IS_OEM_VER_SIZE);
		finfo->oem_ver[FIMC_IS_OEM_VER_SIZE] = '\0';
		finfo->oem_section_crc_addr = EEP_CHECKSUM_OEM_ADDR_FRONT;
#endif
		/* AWB Data : Module/Manufacturer Information */
		memcpy(finfo->awb_ver, &buf[EEP_AWB_VER_START_ADDR_FRONT], FIMC_IS_AWB_VER_SIZE);
		finfo->awb_ver[FIMC_IS_AWB_VER_SIZE] = '\0';
		finfo->awb_section_crc_addr = EEP_CHECKSUM_AWB_ADDR_FRONT;

		/* SHADING Data : Module/Manufacturer Information */
		memcpy(finfo->shading_ver, &buf[EEP_AP_SHADING_VER_START_ADDR_FRONT], FIMC_IS_SHADING_VER_SIZE);
		finfo->shading_ver[FIMC_IS_SHADING_VER_SIZE] = '\0';
		finfo->shading_section_crc_addr = EEP_CHECKSUM_AP_SHADING_ADDR_FRONT;
#endif
	} else if (position == SENSOR_POSITION_REAR2) {
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR2)
		info("REAR2 EEPROM header version = %s,\n", finfo->header_ver);
#if defined(EEP_HEADER_OEM_START_ADDR_REAR2)
		finfo->oem_start_addr = *((u32 *)&buf[EEP_HEADER_OEM_START_ADDR_REAR2]);
		finfo->oem_end_addr = *((u32 *)&buf[EEP_HEADER_OEM_END_ADDR_REAR2]);
		info("OEM start = 0x%08x, end = 0x%08x\n",
			(finfo->oem_start_addr), (finfo->oem_end_addr));
#endif
#if defined(EEP_HEADER_AWB_START_ADDR_REAR2)
		finfo->awb_start_addr = *((u32 *)&buf[EEP_HEADER_AWB_START_ADDR_REAR2]);
		finfo->awb_end_addr = *((u32 *)&buf[EEP_HEADER_AWB_END_ADDR_REAR2]);
		info("AWB start = 0x%08x, end = 0x%08x\n",
			(finfo->awb_start_addr), (finfo->awb_end_addr));
#endif
#if defined(EEP_HEADER_AP_SHADING_START_ADDR_REAR2)
		finfo->shading_start_addr = *((u32 *)&buf[EEP_HEADER_AP_SHADING_START_ADDR_REAR2]);
		finfo->shading_end_addr = *((u32 *)&buf[EEP_HEADER_AP_SHADING_END_ADDR_REAR2]);
		if (finfo->shading_end_addr > 0x1fff) {
			err("Shading end_addr has error!! 0x%08x", finfo->shading_end_addr);
			finfo->setfile_end_addr = 0x1fff;
		}
		info("Shading start = 0x%08x, end = 0x%08x\n",
			(finfo->shading_start_addr), (finfo->shading_end_addr));
#endif
#if defined(EEP_HEADER_AP_PDAF_START_ADDR_REAR2)
		finfo->ap_pdaf_start_addr = *((u32 *)&buf[EEP_HEADER_AP_PDAF_START_ADDR_REAR2]);
		finfo->ap_pdaf_end_addr = *((u32 *)&buf[EEP_HEADER_AP_PDAF_END_ADDR_REAR2]);
		if (finfo->ap_pdaf_end_addr > 0x1fff) {
			err("AP PDAF end_addr has error!! 0x%08x", finfo->ap_pdaf_end_addr);
			finfo->ap_pdaf_end_addr = 0x1fff;
		}
		info("AP PDAF start = 0x%08x, end = 0x%08x\n",
			(finfo->ap_pdaf_start_addr), (finfo->ap_pdaf_end_addr));
#endif

		/* HEARDER Data : Module/Manufacturer Information */
		memcpy(finfo->header_ver, &buf[EEP_HEADER_VERSION_START_ADDR_REAR2], FIMC_IS_HEADER_VER_SIZE);
		finfo->header_ver[FIMC_IS_HEADER_VER_SIZE] = '\0';
		/* HEARDER Data : Cal Map Version */
		memcpy(finfo->cal_map_ver, &buf[EEP_HEADER_CAL_MAP_VER_START_ADDR_REAR2], FIMC_IS_CAL_MAP_VER_SIZE);
		/* MODULE ID : Module ID Information */
#if defined(EEP_HEADER_MODULE_ID_ADDR_REAR2)
		memcpy(finfo->module_id, &buf[EEP_HEADER_MODULE_ID_ADDR_REAR2], FIMC_IS_MODULE_ID_SIZE);
#else
		memset(finfo->module_id, 0x0, FIMC_IS_MODULE_ID_SIZE);
#endif
#if defined(EEP_HEADER_SENSOR_ID_ADDR_REAR2)
		memcpy(finfo->from_sensor_id, &buf[EEP_HEADER_SENSOR_ID_ADDR_REAR2], FIMC_IS_SENSOR_ID_SIZE);
		finfo->from_sensor_id[FIMC_IS_SENSOR_ID_SIZE] = '\0';
#endif

		memcpy(finfo->project_name, &buf[EEP_HEADER_PROJECT_NAME_START_ADDR_REAR2], FIMC_IS_PROJECT_NAME_SIZE);
		finfo->project_name[FIMC_IS_PROJECT_NAME_SIZE] = '\0';
		finfo->header_section_crc_addr = EEP_CHECKSUM_HEADER_ADDR_REAR2;

		/* OEM Data : Module/Manufacturer Information */
		memcpy(finfo->oem_ver, &buf[EEP_OEM_VER_START_ADDR_REAR2], FIMC_IS_OEM_VER_SIZE);
		finfo->oem_ver[FIMC_IS_OEM_VER_SIZE] = '\0';
		finfo->oem_section_crc_addr = EEP_CHECKSUM_OEM_ADDR_REAR2;

		/* AWB Data : Module/Manufacturer Information */
		memcpy(finfo->awb_ver, &buf[EEP_AWB_VER_START_ADDR_REAR2], FIMC_IS_AWB_VER_SIZE);
		finfo->awb_ver[FIMC_IS_AWB_VER_SIZE] = '\0';
		finfo->awb_section_crc_addr = EEP_CHECKSUM_AWB_ADDR_REAR2;

		/* SHADING Data : Module/Manufacturer Information */
		memcpy(finfo->shading_ver, &buf[EEP_AP_SHADING_VER_START_ADDR_REAR2], FIMC_IS_SHADING_VER_SIZE);
		finfo->shading_ver[FIMC_IS_SHADING_VER_SIZE] = '\0';
		finfo->shading_section_crc_addr = EEP_CHECKSUM_AP_SHADING_ADDR_REAR2;

		/* READ AF CAL : PAN & MACRO */
#if defined(EEPROM_AF_CAL_PAN_ADDR_REAR2)
		finfo->af_cal_pan = *((u32 *)&buf[EEPROM_AF_CAL_PAN_ADDR_REAR2]);
#endif
#if defined(EEPROM_AF_CAL_MACRO_ADDR_REAR2)
		finfo->af_cal_macro = *((u32 *)&buf[EEPROM_AF_CAL_MACRO_ADDR_REAR2]);
#endif
#if defined(EEP_AP_PDAF_VER_START_ADDR_REAR2)
		/* PDAF Data : Module/Manufacturer Information */
		memcpy(finfo->ap_pdaf_ver, &buf[EEP_AP_PDAF_VER_START_ADDR_REAR2], FIMC_IS_AP_PDAF_VER_SIZE);
		finfo->ap_pdaf_ver[FIMC_IS_AP_PDAF_VER_SIZE] = '\0';
		finfo->ap_pdaf_section_crc_addr = EEP_CHECKSUM_AP_PDAF_ADDR_REAR2;
#endif
		/* MTF Data : AF Position & Resolution */
#if defined(EEP_HEADER_MTF_DATA_ADDR_REAR2)
		finfo->mtf_data_addr = EEP_HEADER_MTF_DATA_ADDR_REAR2;
#endif
#endif /* CONFIG_CAMERA_EEPROM_SUPPORT_REAR2 */
	} else if (position == SENSOR_POSITION_REAR3) {
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR3)
		info("REAR3 EEPROM header version = %s,\n", finfo->header_ver);
#if defined(EEP_HEADER_OEM_START_ADDR_REAR3)
		finfo->oem_start_addr = *((u32 *)&buf[EEP_HEADER_OEM_START_ADDR_REAR3]);
		finfo->oem_end_addr = *((u32 *)&buf[EEP_HEADER_OEM_END_ADDR_REAR3]);
		info("OEM start = 0x%08x, end = 0x%08x\n",
			(finfo->oem_start_addr), (finfo->oem_end_addr));
#endif
#if defined(EEP_HEADER_AWB_START_ADDR_REAR3)
		finfo->awb_start_addr = *((u32 *)&buf[EEP_HEADER_AWB_START_ADDR_REAR3]);
		finfo->awb_end_addr = *((u32 *)&buf[EEP_HEADER_AWB_END_ADDR_REAR3]);
		info("AWB start = 0x%08x, end = 0x%08x\n",
			(finfo->awb_start_addr), (finfo->awb_end_addr));
#endif
#if defined(EEP_HEADER_AP_SHADING_START_ADDR_REAR3)
		finfo->shading_start_addr = *((u32 *)&buf[EEP_HEADER_AP_SHADING_START_ADDR_REAR3]);
		finfo->shading_end_addr = *((u32 *)&buf[EEP_HEADER_AP_SHADING_END_ADDR_REAR3]);
		if (finfo->shading_end_addr > 0x1fff) {
			err("Shading end_addr has error!! 0x%08x", finfo->shading_end_addr);
			finfo->setfile_end_addr = 0x1fff;
		}
		info("Shading start = 0x%08x, end = 0x%08x\n",
			(finfo->shading_start_addr), (finfo->shading_end_addr));
#endif
#if defined(EEP_HEADER_AP_PDAF_START_ADDR_REAR3)
		finfo->ap_pdaf_start_addr = *((u32 *)&buf[EEP_HEADER_AP_PDAF_START_ADDR_REAR3]);
		finfo->ap_pdaf_end_addr = *((u32 *)&buf[EEP_HEADER_AP_PDAF_END_ADDR_REAR3]);
		if (finfo->ap_pdaf_end_addr > 0x1fff) {
			err("AP PDAF end_addr has error!! 0x%08x", finfo->ap_pdaf_end_addr);
			finfo->ap_pdaf_end_addr = 0x1fff;
		}
		info("AP PDAF start = 0x%08x, end = 0x%08x\n",
			(finfo->ap_pdaf_start_addr), (finfo->ap_pdaf_end_addr));
#endif

		/* HEARDER Data : Module/Manufacturer Information */
		memcpy(finfo->header_ver, &buf[EEP_HEADER_VERSION_START_ADDR_REAR3], FIMC_IS_HEADER_VER_SIZE);
		finfo->header_ver[FIMC_IS_HEADER_VER_SIZE] = '\0';
		/* HEARDER Data : Cal Map Version */
		memcpy(finfo->cal_map_ver, &buf[EEP_HEADER_CAL_MAP_VER_START_ADDR_REAR3], FIMC_IS_CAL_MAP_VER_SIZE);
		/* MODULE ID : Module ID Information */
#if defined(EEP_HEADER_MODULE_ID_ADDR_REAR3)
		memcpy(finfo->module_id, &buf[EEP_HEADER_MODULE_ID_ADDR_REAR3], FIMC_IS_MODULE_ID_SIZE);
#else
		memset(finfo->module_id, 0x0, FIMC_IS_MODULE_ID_SIZE);
#endif
#if defined(EEP_HEADER_SENSOR_ID_ADDR_REAR3)
		memcpy(finfo->from_sensor_id, &buf[EEP_HEADER_SENSOR_ID_ADDR_REAR3], FIMC_IS_SENSOR_ID_SIZE);
		finfo->from_sensor_id[FIMC_IS_SENSOR_ID_SIZE] = '\0';
#endif

		memcpy(finfo->project_name, &buf[EEP_HEADER_PROJECT_NAME_START_ADDR_REAR3], FIMC_IS_PROJECT_NAME_SIZE);
		finfo->project_name[FIMC_IS_PROJECT_NAME_SIZE] = '\0';
		finfo->header_section_crc_addr = EEP_CHECKSUM_HEADER_ADDR_REAR3;

		/* OEM Data : Module/Manufacturer Information */
		memcpy(finfo->oem_ver, &buf[EEP_OEM_VER_START_ADDR_REAR3], FIMC_IS_OEM_VER_SIZE);
		finfo->oem_ver[FIMC_IS_OEM_VER_SIZE] = '\0';
		finfo->oem_section_crc_addr = EEP_CHECKSUM_OEM_ADDR_REAR3;

		/* AWB Data : Module/Manufacturer Information */
		memcpy(finfo->awb_ver, &buf[EEP_AWB_VER_START_ADDR_REAR3], FIMC_IS_AWB_VER_SIZE);
		finfo->awb_ver[FIMC_IS_AWB_VER_SIZE] = '\0';
		finfo->awb_section_crc_addr = EEP_CHECKSUM_AWB_ADDR_REAR3;

		/* SHADING Data : Module/Manufacturer Information */
		memcpy(finfo->shading_ver, &buf[EEP_AP_SHADING_VER_START_ADDR_REAR3], FIMC_IS_SHADING_VER_SIZE);
		finfo->shading_ver[FIMC_IS_SHADING_VER_SIZE] = '\0';
		finfo->shading_section_crc_addr = EEP_CHECKSUM_AP_SHADING_ADDR_REAR3;

		/* READ AF CAL : PAN & MACRO */
#if defined(EEPROM_AF_CAL_PAN_ADDR_REAR3)
		finfo->af_cal_pan = *((u32 *)&buf[EEPROM_AF_CAL_PAN_ADDR_REAR3]);
#endif
#if defined(EEPROM_AF_CAL_MACRO_ADDR_REAR3)
		finfo->af_cal_macro = *((u32 *)&buf[EEPROM_AF_CAL_MACRO_ADDR_REAR3]);
#endif
#if defined(EEP_AP_PDAF_VER_START_ADDR_REAR3)
		/* PDAF Data : Module/Manufacturer Information */
		memcpy(finfo->ap_pdaf_ver, &buf[EEP_AP_PDAF_VER_START_ADDR_REAR3], FIMC_IS_AP_PDAF_VER_SIZE);
		finfo->ap_pdaf_ver[FIMC_IS_AP_PDAF_VER_SIZE] = '\0';
		finfo->ap_pdaf_section_crc_addr = EEP_CHECKSUM_AP_PDAF_ADDR_REAR3;
#endif
		/* MTF Data : AF Position & Resolution */
#if defined(EEP_HEADER_MTF_DATA_ADDR_REAR3)
		finfo->mtf_data_addr = EEP_HEADER_MTF_DATA_ADDR_REAR3;
#endif
#endif /* CONFIG_CAMERA_EEPROM_SUPPORT_REAR3 */
	} else {
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR)
		info("REAR EEPROM header version = %s\n", finfo->header_ver);
		finfo->oem_start_addr = *((u32 *)&buf[EEP_HEADER_OEM_START_ADDR]);
		finfo->oem_end_addr = *((u32 *)&buf[EEP_HEADER_OEM_END_ADDR]);
		info("OEM start = 0x%08x, end = 0x%08x\n",
			(finfo->oem_start_addr), (finfo->oem_end_addr));
		finfo->awb_start_addr = *((u32 *)&buf[EEP_HEADER_AWB_START_ADDR]);
		finfo->awb_end_addr = *((u32 *)&buf[EEP_HEADER_AWB_END_ADDR]);
		info("AWB start = 0x%08x, end = 0x%08x\n",
			(finfo->awb_start_addr), (finfo->awb_end_addr));
		finfo->shading_start_addr = *((u32 *)&buf[EEP_HEADER_AP_SHADING_START_ADDR]);
		finfo->shading_end_addr = *((u32 *)&buf[EEP_HEADER_AP_SHADING_END_ADDR]);
		if (finfo->shading_end_addr > 0x1fff) {
			err("Shading end_addr has error!! 0x%08x", finfo->shading_end_addr);
			finfo->setfile_end_addr = 0x1fff;
		}
		info("Shading start = 0x%08x, end = 0x%08x\n",
			(finfo->shading_start_addr), (finfo->shading_end_addr));

		/* HEARDER Data : Module/Manufacturer Information */
		memcpy(finfo->header_ver, &buf[EEP_HEADER_VERSION_START_ADDR], FIMC_IS_HEADER_VER_SIZE);
		finfo->header_ver[FIMC_IS_HEADER_VER_SIZE] = '\0';
		/* HEARDER Data : Cal Map Version */
		memcpy(finfo->cal_map_ver, &buf[EEP_HEADER_CAL_MAP_VER_START_ADDR], FIMC_IS_CAL_MAP_VER_SIZE);

		memcpy(finfo->project_name, &buf[EEP_HEADER_PROJECT_NAME_START_ADDR], FIMC_IS_PROJECT_NAME_SIZE);
		finfo->project_name[FIMC_IS_PROJECT_NAME_SIZE] = '\0';
		finfo->header_section_crc_addr = EEP_CHECKSUM_HEADER_ADDR;

		/* OEM Data : Module/Manufacturer Information */
		memcpy(finfo->oem_ver, &buf[EEP_OEM_VER_START_ADDR], FIMC_IS_OEM_VER_SIZE);
		finfo->oem_ver[FIMC_IS_OEM_VER_SIZE] = '\0';
		finfo->oem_section_crc_addr = EEP_CHECKSUM_OEM_ADDR;

		/* AWB Data : Module/Manufacturer Information */
		memcpy(finfo->awb_ver, &buf[EEP_AWB_VER_START_ADDR], FIMC_IS_AWB_VER_SIZE);
		finfo->awb_ver[FIMC_IS_AWB_VER_SIZE] = '\0';
		finfo->awb_section_crc_addr = EEP_CHECKSUM_AWB_ADDR;

		/* SHADING Data : Module/Manufacturer Information */
		memcpy(finfo->shading_ver, &buf[EEP_AP_SHADING_VER_START_ADDR], FIMC_IS_SHADING_VER_SIZE);
		finfo->shading_ver[FIMC_IS_SHADING_VER_SIZE] = '\0';
		finfo->shading_section_crc_addr = EEP_CHECKSUM_AP_SHADING_ADDR;

		finfo->af_cal_pan = *((u32 *)&buf[EEPROM_AF_CAL_PAN_ADDR]);
		finfo->af_cal_macro = *((u32 *)&buf[EEPROM_AF_CAL_MACRO_ADDR]);

#ifdef EEP_HEADER_MODULE_ID_ADDR
		memcpy(finfo->module_id, &buf[EEP_HEADER_MODULE_ID_ADDR], FIMC_IS_MODULE_ID_SIZE);
#else
		memset(finfo->module_id, 0x0, FIMC_IS_MODULE_ID_SIZE);
#endif

#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_OIS)
		finfo->ois_cal_start_addr = *((u32 *)&buf[EEP_HEADER_OIS_CAL_START_ADDR]);
		finfo->ois_cal_end_addr = *((u32 *)&buf[EEP_HEADER_OIS_CAL_END_ADDR]);
		info("OIS Cal start = 0x%08x, end = 0x%08x\n", (finfo->ois_cal_start_addr), (finfo->ois_cal_end_addr));

		finfo->ois_shift_start_addr = *((u32 *)&buf[EEP_HEADER_OIS_SHIFT_START_ADDR]);
		finfo->ois_shift_end_addr = *((u32 *)&buf[EEP_HEADER_OIS_SHIFT_END_ADDR]);
		info("OIS Shift start = 0x%08x, end = 0x%08x\n", (finfo->ois_shift_start_addr), (finfo->ois_shift_end_addr));

		finfo->ois_fw_set_start_addr = *((u32 *)&buf[EEP_HEADER_OIS_FW_SET_START_ADDR]);
		finfo->ois_fw_set_end_addr = *((u32 *)&buf[EEP_HEADER_OIS_FW_SET_END_ADDR]);
		info("OIS Set FW start = 0x%08x, end = 0x%08x\n", (finfo->ois_fw_set_start_addr), (finfo->ois_fw_set_end_addr));

		finfo->ois_fw_factory_start_addr = *((u32 *)&buf[EEP_HEADER_OIS_FW_FACTORY_START_ADDR]);
		finfo->ois_fw_factory_end_addr = *((u32 *)&buf[EEP_HEADER_OIS_FW_FACTORY_END_ADDR]);
		info("OIS Factory FW start = 0x%08x, end = 0x%08x\n", (finfo->ois_fw_factory_start_addr), (finfo->ois_fw_factory_end_addr));

		/* HEARDER Data : OIS F/W Version[SEC Info] */
		memcpy(finfo->ois_fw_ver, &buf[EEP_HEADER_OIS_FW_VER_START_ADDR], FIMC_IS_OIS_FW_VER_SIZE);
		finfo->ois_fw_ver[FIMC_IS_OIS_FW_VER_SIZE] = '\0';
		/* HEARDER Data : OIS Cal Version */
		memcpy(finfo->ois_cal_ver, &buf[EEP_HEADER_OIS_CAL_VER_START_ADDR], FIMC_IS_OIS_CAL_VER_SIZE);
		finfo->ois_cal_ver[FIMC_IS_OIS_CAL_VER_SIZE] = '\0';
		/* HEARDER Data : OIS Chip Info */
		memcpy(finfo->ois_chip_info, &buf[EEP_HEADER_OIS_CHIP_INFO_START_ADDR], FIMC_IS_OIS_CHIP_INFO_SIZE);
		finfo->ois_chip_info[FIMC_IS_OIS_CHIP_INFO_SIZE] = '\0';
		/* HEARDER Data : OIS Adjust Factor */
		memcpy(finfo->ois_adjust_factor, &buf[EEP_HEADER_OIS_ADJ_FACTOR_START_ADDR], FIMC_IS_OIS_ADJ_FACTOR_SIZE);
		finfo->ois_adjust_factor[FIMC_IS_OIS_ADJ_FACTOR_SIZE] = '\0';
#endif
#endif
	}

	/* debug info dump */
#if defined(EEPROM_DEBUG)
	info("++++ EEPROM data info\n");
	info("1. Header info\n");
	info("Module info : %s\n", finfo->header_ver);
	info(" ID : %c\n", finfo->header_ver[FW_CORE_VER]);
	info(" Pixel num : %c%c\n", finfo->header_ver[FW_PIXEL_SIZE],
		finfo->header_ver[FW_PIXEL_SIZE+1]);
	info(" ISP ID : %c\n", finfo->header_ver[FW_ISP_COMPANY]);
	info(" Sensor Maker : %c\n", finfo->header_ver[FW_SENSOR_MAKER]);
	info(" Year : %c\n", finfo->header_ver[FW_PUB_YEAR]);
	info(" Month : %c\n", finfo->header_ver[FW_PUB_MON]);
	info(" Release num : %c%c\n", finfo->header_ver[FW_PUB_NUM],
		finfo->header_ver[FW_PUB_NUM+1]);
	info(" Manufacturer ID : %c\n", finfo->header_ver[FW_MODULE_COMPANY]);
	info(" Module ver : %c\n", finfo->header_ver[FW_VERSION_INFO]);
	info("project_name : %s\n", finfo->project_name);
	info("Cal data map ver : %s\n", finfo->cal_map_ver);
	info("Module ID : %c%c%c%c%c%02X%02X%02X%02X%02X\n",
		finfo->module_id[0], finfo->module_id[1], finfo->module_id[2],
		finfo->module_id[3], finfo->module_id[4], finfo->module_id[5],
		finfo->module_id[6], finfo->module_id[7], finfo->module_id[8],
		finfo->module_id[9]);
	info("2. OEM info\n");
	info("Module info : %s\n", finfo->oem_ver);
	info("3. AWB info\n");
	info("Module info : %s\n", finfo->awb_ver);
	info("4. Shading info\n");
	info("Module info : %s\n", finfo->shading_ver);
	info("---- EEPROM data info\n");
#endif

	/* CRC check */
	if (!fimc_is_sec_check_cal_crc32(buf, position) && (retry > 0)) {
		retry--;
		goto crc_retry;
	}

	if (position == SENSOR_POSITION_FRONT) {
		if (finfo->header_ver[3] == 'L') {
			crc32_check_factory_front = crc32_check_front;
		} else {
			crc32_check_factory_front = false;
		}
	} else if (position == SENSOR_POSITION_REAR2) {
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR2)
		if (finfo->header_ver[3] == 'L')
			crc32_check_factory_rear2 = crc32_check_rear2;
		else
			crc32_check_factory_rear2 = false;
#endif
	} else if (position == SENSOR_POSITION_REAR3) {
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR3)
		if (finfo->header_ver[3] == 'L')
			crc32_check_factory_rear3 = crc32_check_rear3;
		else
			crc32_check_factory_rear3 = false;
#endif
	} else {
		if (finfo->header_ver[3] == 'L') {
			crc32_check_factory = crc32_check;
		} else {
			crc32_check_factory = false;
		}
	}

#if (defined(CONFIG_COMPANION_C2_USE) || defined(CONFIG_COMPANION_C3_USE)) && !defined(CONFIG_FRONT_COMPANION_C2_DISABLE)
	if (fimc_is_sec_check_from_ver(core, position)) {
		/* If FROM LSC value is not valid, loading default lsc data */
		if (*((u32 *)&cal_buf_front[sysfs_finfo_front.lsc_gain_start_addr]) == 0x00000000) {
			companion_front_lsc_isvalid = false;
		} else {
			companion_front_lsc_isvalid = true;
		}
	}
#endif

	if (!specific->use_module_check) {
		is_latest_cam_module = true;
	} else {
		if (sysfs_finfo.header_ver[10] >= CAMERA_MODULE_ES_VERSION_REAR) {
			is_latest_cam_module = true;
		} else {
			is_latest_cam_module = false;
		}
	}

#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR2)
	if (position == SENSOR_POSITION_REAR2) {
		if (specific->use_module_check) {
			if (finfo->header_ver[10] >= CAMERA_MODULE_ES_VERSION_REAR2)
				is_latest_cam_module_rear2 = true;
			else
				is_latest_cam_module_rear2 = false;
		} else {
			is_latest_cam_module_rear2 = true;
		}
	}
#endif

#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR3)
	if (position == SENSOR_POSITION_REAR3) {
		if (specific->use_module_check) {
			if (finfo->header_ver[10] >= CAMERA_MODULE_ES_VERSION_REAR3)
				is_latest_cam_module_rear3 = true;
			else
				is_latest_cam_module_rear3 = false;
		} else {
			is_latest_cam_module_rear3 = true;
		}
	}
#endif

	if (position == SENSOR_POSITION_REAR) {
		if (specific->use_module_check) {
			if (finfo->header_ver[10] == FIMC_IS_LATEST_ROM_VERSION_M) {
				is_final_cam_module = true;
			} else {
				is_final_cam_module = false;
			}
		} else {
			is_final_cam_module = true;
		}
	}
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR2)
	else if (position == SENSOR_POSITION_REAR2) {
		if (specific->use_module_check) {
			if (finfo->header_ver[10] == FIMC_IS_LATEST_ROM_VERSION_M)
				is_final_cam_module_rear2 = true;
			else
				is_final_cam_module_rear2 = false;
		} else {
			is_final_cam_module_rear2 = true;
		}
	}
#endif
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR3)
	else if (position == SENSOR_POSITION_REAR3) {
		if (specific->use_module_check) {
			if (finfo->header_ver[10] == FIMC_IS_LATEST_ROM_VERSION_M)
				is_final_cam_module_rear3 = true;
			else
				is_final_cam_module_rear3 = false;
		} else {
			is_final_cam_module_rear3 = true;
		}
	}
#endif
	else {
		if (specific->use_module_check) {
			if (finfo->header_ver[10] == FIMC_IS_LATEST_ROM_VERSION_M) {
				is_final_cam_module_front = true;
			} else {
				is_final_cam_module_front = false;
			}
		} else {
			is_final_cam_module_front = true;
		}
	}

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	key_fp = filp_open("/data/media/0/1q2w3e4r.key", O_RDONLY, 0);
	if (IS_ERR(key_fp)) {
		pr_info("KEY does not exist.\n");
		key_fp = NULL;
		goto key_err;
	} else {
		dump_fp = filp_open("/data/media/0/dump", O_RDONLY, 0);
		if (IS_ERR(dump_fp)) {
			pr_info("dump folder does not exist.\n");
			dump_fp = NULL;
			goto key_err;
		} else {
			pr_info("dump folder exist, Dump EEPROM cal data.\n");
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_FRONT) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_FRONT)
			if (position == SENSOR_POSITION_FRONT) {
				if (write_data_to_file("/data/media/0/dump/eeprom_cal_front.bin", cal_buf_front,
										FIMC_IS_DUMP_EEPROM_CAL_SIZE, &pos) < 0) {
					pr_info("Failed to dump cal data.\n");
					goto dump_err;
				}
			} else
#endif
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR2)
			if (position == SENSOR_POSITION_REAR2) {
				if (write_data_to_file("/data/media/0/dump/eeprom_rear2_cal.bin", buf, FIMC_IS_DUMP_EEPROM_CAL_SIZE, &pos) < 0) {
					pr_info("Failed to rear dump cal data.\n");
					goto dump_err;
				}
			} else
#endif
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR3)
			if (position == SENSOR_POSITION_REAR3) {
				if (write_data_to_file("/data/media/0/dump/eeprom_rear3_cal.bin", buf, FIMC_IS_DUMP_EEPROM_CAL_SIZE, &pos) < 0) {
					pr_info("Failed to rear dump cal data.\n");
					goto dump_err;
				}
			} else
#endif
			{
				if (write_data_to_file("/data/media/0/dump/eeprom_cal.bin", cal_buf,
										FIMC_IS_DUMP_EEPROM_CAL_SIZE, &pos) < 0) {
					pr_info("Failed to dump cal data.\n");
					goto dump_err;
				}
			}
		}
	}

dump_err:
	if (dump_fp)
		filp_close(dump_fp, current->files);
key_err:
	if (key_fp)
		filp_close(key_fp, current->files);
	set_fs(old_fs);
exit:
	return ret;
}
#endif

#if defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_FRONT)
#define I2C_WRITE 3
#define I2C_BYTE  2
#define I2C_DATA  1
#define I2C_ADDR  0

enum i2c_write {
	I2C_WRITE_ADDR8_DATA8 = 0x0,
	I2C_WRITE_ADDR16_DATA8,
	I2C_WRITE_ADDR16_DATA16
};

int fimc_is_sec_set_registers(struct i2c_client *client, const u32 *regs, const u32 size)
{
	int ret = 0;
	int i = 0;

	BUG_ON(!regs);

	for (i = 0; i < size; i += I2C_WRITE) {
		if (regs[i + I2C_BYTE] == I2C_WRITE_ADDR8_DATA8) {
			ret = fimc_is_sensor_addr8_write8(client, regs[i + I2C_ADDR], regs[i + I2C_DATA]);
			if (ret < 0) {
				err("fimc_is_sensor_addr8_write8 fail, ret(%d), addr(%#x), data(%#x)",
						ret, regs[i + I2C_ADDR], regs[i + I2C_DATA]);
				break;
			}
		} else if (regs[i + I2C_BYTE] == I2C_WRITE_ADDR16_DATA8) {
			ret = fimc_is_sensor_write8(client, regs[i + I2C_ADDR], regs[i + I2C_DATA]);
			if (ret < 0) {
				err("fimc_is_sensor_write8 fail, ret(%d), addr(%#x), data(%#x)",
						ret, regs[i + I2C_ADDR], regs[i + I2C_DATA]);
				break;
			}
		} else if (regs[i + I2C_BYTE] == I2C_WRITE_ADDR16_DATA16) {
			ret = fimc_is_sensor_write16(client, regs[i + I2C_ADDR], regs[i + I2C_DATA]);
			if (ret < 0) {
				err("fimc_is_sensor_write16 fail, ret(%d), addr(%#x), data(%#x)",
						ret, regs[i + I2C_ADDR], regs[i + I2C_DATA]);
				break;
			}
		}
	}

	return ret;
}

#ifdef CONFIG_CAMERA_OTPROM_SUPPORT_REAR
int fimc_is_sec_read_otprom_header(struct device *dev, int position)
{
	int ret = 0;
	struct fimc_is_core *core = dev_get_drvdata(fimc_is_dev);
	struct fimc_is_vender_specific *specific;
	u8 header_version[FIMC_IS_HEADER_VER_SIZE + 1] = {0, };
	struct i2c_client *client;
#if defined(OTP_BANK) || defined(OTP_SINGLE_READ_ADDR)
	u8 data8 = 0;
	int otp_bank = 0;
	u8 start_addr_h = 0;
	u8 start_addr_l= 0;
	u16 start_addr = 0;
	int i = 0;
#endif

	specific = core->vender.private_data;
#if defined(CONFIG_USE_DIRECT_IS_CONTROL)
	client = specific->rear_cis_client;
#else
	client = specific->eeprom_client[position];
#endif

	if (!client) {
		err("eeprom i2c client is NULL\n");
		ret = -EINVAL;
		goto exit;
	}
	fimc_is_i2c_config(client, true);

#if defined(OTP_NEED_INIT_SETTING)
	ret = specific->cis_init_reg_write();
	if (unlikely(ret)) {
		err("failed to fimc_is_i2c_write (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}
#endif

#if defined(OTP_MODE_CHANGE)
	ret = fimc_is_sec_set_registers(client, sensor_mode_change_to_OTP_reg, sensor_mode_change_to_OTP_reg_size);
	if (unlikely(ret)) {
		err("failed to fimc_is_sec_set_registers (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}
#endif

#if defined(OTP_BANK) || defined(OTP_SINGLE_READ_ADDR)
	fimc_is_sensor_write8(client, OTP_START_ADDR_HIGH, OTP_BANK_ADDR_HIGH);
	fimc_is_sensor_write8(client, OTP_START_ADDR_LOW, OTP_BANK_ADDR_LOW);
	fimc_is_sensor_write8(client, OTP_SINGLE_READ, 0x01);
	fimc_is_sensor_read8(client, OTP_SINGLE_READ_ADDR, &data8);

	otp_bank = data8;

	pr_info("Camera: otp_bank = %d\n", otp_bank);

	switch(otp_bank) {
	case 1 :
		start_addr_h = OTP_BANK1_START_ADDR_HIGH;
		start_addr_l = OTP_BANK1_START_ADDR_LOW;
		break;
	case 2 :
		start_addr_h = OTP_BANK2_START_ADDR_HIGH;
		start_addr_l = OTP_BANK2_START_ADDR_LOW;
		break;
	case 3 :
		start_addr_h = OTP_BANK3_START_ADDR_HIGH;
		start_addr_l = OTP_BANK3_START_ADDR_LOW;
		break;
	default :
		start_addr_h = OTP_BANK1_START_ADDR_HIGH;
		start_addr_l = OTP_BANK1_START_ADDR_LOW;
		break;
	}
	start_addr = ((start_addr_h << 8)&0xff00) | (start_addr_l&0xff);
#endif

#if defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR)
#if defined(OTP_BANK) && defined(OTP_SINGLE_READ_ADDR)
	fimc_is_sensor_write8(client, OTP_START_ADDR_HIGH,
		start_addr_h + ((OTP_I2C_HEADER_VERSION_START_ADDR>>8)&0xff));
	fimc_is_sensor_write8(client, OTP_START_ADDR_LOW,
			start_addr_l + (OTP_I2C_HEADER_VERSION_START_ADDR&0xff));
	fimc_is_sensor_write8(client, OTP_SINGLE_READ, 0x01);

	for (i = 0; i < FIMC_IS_HEADER_VER_SIZE; i++) {
		fimc_is_sensor_read8(client, OTP_SINGLE_READ_ADDR, &data8);
		header_version[i] = data8;
	}
#else
	ret = fimc_is_i2c_read(client, header_version,
			OTP_I2C_HEADER_VERSION_START_ADDR, FIMC_IS_HEADER_VER_SIZE);
#endif
#endif

	fimc_is_i2c_config(client, false);

	if (unlikely(ret)) {
		err("failed to fimc_is_i2c_read for header version (%d)\n", ret);
		ret = -EINVAL;
	}

	memcpy(sysfs_finfo.header_ver, header_version, FIMC_IS_HEADER_VER_SIZE);
	sysfs_finfo.header_ver[FIMC_IS_HEADER_VER_SIZE] = '\0';

exit:
	return ret;
}
#endif
#endif

#if defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_FRONT)
#if defined(CAMERA_OTPROM_SUPPORT_FRONT_5E9)
int otprom_5e9_check_to_read(struct i2c_client *client)
{
	u8 data8=0;
	int ret;
	ret = fimc_is_sensor_write8(client, 0x0A00, 0x01);
	msleep(1);
	ret = fimc_is_sensor_read8(client, 0x0A01, &data8);
	return ret;
}

int fimc_is_i2c_read_otp_5e9(struct i2c_client *client, void *buf, u32 start_addr, size_t size)
{
	int page_num = 0;
	int reg_count = 0;
	int index = 0;
	int ret = 0;

	page_num = OTP_5E9_GET_PAGE(start_addr,OTP_PAGE_START_ADDR,HEADER_START_ADDR);
	reg_count = OTP_5E9_GET_REG(start_addr,OTP_PAGE_START_ADDR,HEADER_START_ADDR);
	fimc_is_sensor_write8(client, OTP_PAGE_ADDR, page_num);
	ret = otprom_5e9_check_to_read(client);

	for(index = 0; index<size ;index++)
	{
		if(reg_count >= 64)
		{
			page_num++;
			reg_count = 0;
			fimc_is_sensor_write8(client, OTP_PAGE_ADDR, page_num);
			ret = otprom_5e9_check_to_read(client);
		}
		fimc_is_sensor_read8(client, OTP_REG_ADDR_START+reg_count, buf+index);
		reg_count++;
	}

	return ret;
}

int fimc_is_sec_readcal_otprom_5e9(struct device *dev, int position)
{
	int ret = 0;
	char *buf = NULL;
	int retry = FIMC_IS_CAL_RETRY_CNT;
	struct fimc_is_core *core = dev_get_drvdata(dev);
	struct fimc_is_from_info *finfo = NULL;
	struct fimc_is_vender_specific *specific = core->vender.private_data;
	int cal_size = 0;
	struct i2c_client *client = NULL;
	struct file *key_fp = NULL;
	struct file *dump_fp = NULL;
	mm_segment_t old_fs;
	loff_t pos = 0;
#ifdef OTP_BANK
	u8 otp_bank = 0;
#endif
	u16 start_addr = 0;

	pr_info("fimc_is_sec_readcal_otprom_5e9 [%d] E\n", position);

	fimc_is_sec_get_sysfs_finfo_by_position(position, &finfo);
	fimc_is_sec_get_front_cal_buf(&buf);
	cal_size = fimc_is_sec_get_max_cal_size(position);

	if (position == SENSOR_POSITION_FRONT) {
#if defined(CONFIG_CAMERA_OTPROM_SUPPORT_FRONT)
#if defined(CONFIG_USE_DIRECT_IS_CONTROL)
	client = specific->front_cis_client;
#else
	client = specific->eeprom_client[SENSOR_POSITION_FRONT];
#endif
#endif
	} else {
#if defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR)
#if defined(CONFIG_USE_DIRECT_IS_CONTROL)
		client = specific->rear_cis_client;
#else
		client = specific->eeprom_client[position];
#endif
#endif
	}

	if (!client) {
		err("cis i2c client is NULL\n");
		return -EINVAL;
	}

	fimc_is_i2c_config(client, true);
	msleep(10);

	/* 0. write Sensor Init(global) */
	ret = fimc_is_sec_set_registers(client,
	sensor_Global, sensor_Global_size);
	if (unlikely(ret)) {
		err("failed to fimc_is_sec_set_registers (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}

	/* 1. write stream on */
	fimc_is_sensor_write8(client, 0x0100, 0x01);
	msleep(50);

	/* 2. write OTP page */
	fimc_is_sensor_write8(client, OTP_PAGE_ADDR, 0x11);
	ret = otprom_5e9_check_to_read(client);

	fimc_is_sensor_read8(client, OTP_REG_ADDR_START, &otp_bank);
	pr_info("Camera: otp_bank = %d\n", otp_bank);

	/* 3. selected page setting */
	switch(otp_bank) {
	case 0x1 :
		start_addr = OTP_START_ADDR;
		break;
	case 0x3 :
		start_addr = OTP_START_ADDR_BANK2;
		break;
	case 0x7 :
		start_addr = OTP_START_ADDR_BANK3;
		break;
	default :
		start_addr = OTP_START_ADDR;
		break;
	}

	pr_info("Camera: otp_start_addr = %x\n", start_addr);
	if (unlikely(ret)) {
		err("failed to fimc_is_sec_set_registers (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}

	ret = fimc_is_sec_set_registers(client, OTP_Init_reg, OTP_Init_size);
	if (unlikely(ret)) {
		err("failed to fimc_is_sec_set_registers (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}

crc_retry:

	/* read cal data
	 * 5E9 use page per 64byte */
	pr_info("Camera: I2C read cal data\n\n");
	fimc_is_i2c_read_otp_5e9(client, buf, start_addr, OTP_USED_CAL_SIZE);

	if (position == SENSOR_POSITION_FRONT) {
#if defined(CONFIG_CAMERA_OTPROM_SUPPORT_FRONT)

		/* HEARDER Data : Module/Manufacturer Information */
#if defined(OTP_HEADER_VERSION_START_ADDR_FRONT)
		memcpy(finfo->header_ver, &buf[OTP_HEADER_VERSION_START_ADDR_FRONT], FIMC_IS_HEADER_VER_SIZE);
		finfo->header_ver[FIMC_IS_HEADER_VER_SIZE] = '\0';
#endif /* OTP_HEADER_VERSION_START_ADDR_FRONT */
		pr_info("FRONT OTPROM header version = %s\n", finfo->header_ver);

#if defined(OTP_HEADER_OEM_START_ADDR_FRONT)
		finfo->oem_start_addr = *((u32 *)&buf[OTP_HEADER_OEM_START_ADDR_FRONT]) - start_addr;
		finfo->oem_end_addr = *((u32 *)&buf[OTP_HEADER_OEM_END_ADDR_FRONT]) - start_addr;
		pr_info("OEM start = 0x%08x, end = 0x%08x\n",
			(finfo->oem_start_addr), (finfo->oem_end_addr));
#endif
#if defined(OTP_HEADER_AWB_START_ADDR_FRONT)
#ifdef OTP_HEADER_DIRECT_ADDR_FRONT
		finfo->awb_start_addr = OTP_HEADER_AWB_START_ADDR_FRONT - start_addr;
		finfo->awb_end_addr = OTP_HEADER_AWB_END_ADDR_FRONT - start_addr;
#else
		finfo->awb_start_addr = *((u32 *)&buf[OTP_HEADER_AWB_START_ADDR_FRONT]) - start_addr;
		finfo->awb_end_addr = *((u32 *)&buf[OTP_HEADER_AWB_END_ADDR_FRONT]) - start_addr;
#endif
		pr_info("AWB start = 0x%08x, end = 0x%08x\n",
			(finfo->awb_start_addr), (finfo->awb_end_addr));
#endif
#if defined(OTP_HEADER_SHADING_START_ADDR_FRONT)
		finfo->shading_start_addr = *((u32 *)&buf[OTP_HEADER_AP_SHADING_START_ADDR_FRONT]) - start_addr;
		finfo->shading_end_addr = *((u32 *)&buf[OTP_HEADER_AP_SHADING_END_ADDR_FRONT]) - start_addr;
		pr_info("Shading start = 0x%08x, end = 0x%08x\n",
			(finfo->shading_start_addr), (finfo->shading_end_addr));
		if (finfo->shading_end_addr > 0x3AFF) {
			err("Shading end_addr has error!! 0x%08x", finfo->shading_end_addr);
			finfo->shading_end_addr = 0x3AFF;
		}
#endif
		/* HEARDER Data : Cal Map Version */
		memcpy(finfo->cal_map_ver,
			   &buf[OTP_HEADER_CAL_MAP_VER_START_ADDR_FRONT], FIMC_IS_CAL_MAP_VER_SIZE);
#if defined(OTP_HEADER_MODULE_ID_ADDR_FRONT)
		memcpy(finfo->module_id,
			   &buf[OTP_HEADER_MODULE_ID_ADDR_FRONT], FIMC_IS_MODULE_ID_SIZE);
		finfo->module_id[FIMC_IS_MODULE_ID_SIZE] = '\0';
#endif

#if defined(OTP_HEADER_PROJECT_NAME_START_ADDR_FRONT)
		memcpy(finfo->project_name,
			   &buf[OTP_HEADER_PROJECT_NAME_START_ADDR_FRONT], FIMC_IS_PROJECT_NAME_SIZE);
		finfo->project_name[FIMC_IS_PROJECT_NAME_SIZE] = '\0';
#endif
		finfo->header_section_crc_addr = OTP_CHECKSUM_HEADER_ADDR_FRONT;
#if defined(OTP_HEADER_OEM_START_ADDR_FRONT)
		/* OEM Data : Module/Manufacturer Information */
		memcpy(finfo->oem_ver, &buf[OTP_OEM_VER_START_ADDR_FRONT], FIMC_IS_OEM_VER_SIZE);
		finfo->oem_ver[FIMC_IS_OEM_VER_SIZE] = '\0';
		finfo->oem_section_crc_addr = OTP_CHECKSUM_OEM_ADDR_FRONT;
#endif
#if defined(OTP_AWB_VER_START_ADDR_FRONT)
		/* AWB Data : Module/Manufacturer Information */
		memcpy(finfo->awb_ver, &buf[OTP_AWB_VER_START_ADDR_FRONT], FIMC_IS_AWB_VER_SIZE);
		finfo->awb_ver[FIMC_IS_AWB_VER_SIZE] = '\0';
		finfo->awb_section_crc_addr = OTP_CHECKSUM_AWB_ADDR_FRONT;
#endif
#if defined(OTP_AP_SHADING_VER_START_ADDR_FRONT)
		/* SHADING Data : Module/Manufacturer Information */
		memcpy(finfo->shading_ver, &buf[OTP_AP_SHADING_VER_START_ADDR_FRONT], FIMC_IS_SHADING_VER_SIZE);
		finfo->shading_ver[FIMC_IS_SHADING_VER_SIZE] = '\0';
		finfo->shading_section_crc_addr = OTP_CHECKSUM_AP_SHADING_ADDR_FRONT;
#endif
#endif
	} else {
		/* HEARDER Data : Module/Manufacturer Information */
#if defined(OTP_HEADER_VERSION_START_ADDR)
		memcpy(finfo->header_ver, &buf[OTP_HEADER_VERSION_START_ADDR], FIMC_IS_HEADER_VER_SIZE);
		finfo->header_ver[FIMC_IS_HEADER_VER_SIZE] = '\0';
#endif
		/* HEARDER Data : Cal Map Version */
#if defined (OTP_HEADER_CAL_MAP_VER_START_ADDR)
		memcpy(finfo->cal_map_ver, &buf[OTP_HEADER_CAL_MAP_VER_START_ADDR], FIMC_IS_CAL_MAP_VER_SIZE);
#endif

		pr_info("REAR OTPROM header version = %s\n", finfo->header_ver);
#if defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR)
		finfo->oem_start_addr = *((u32 *)&buf[OTP_HEADER_OEM_START_ADDR]) - start_addr;
		finfo->oem_end_addr = *((u32 *)&buf[OTP_HEADER_OEM_END_ADDR]) - start_addr;
		pr_info("OEM start = 0x%08x, end = 0x%08x\n",
			(finfo->oem_start_addr), (finfo->oem_end_addr));
#if defined(OTP_HEADER_AWB_START_ADDR)
		finfo->awb_start_addr = *((u32 *)&buf[OTP_HEADER_AWB_START_ADDR]) - start_addr;
		finfo->awb_end_addr = *((u32 *)&buf[OTP_HEADER_AWB_END_ADDR]) - start_addr;
		pr_info("AWB start = 0x%08x, end = 0x%08x\n",
			(finfo->awb_start_addr), (finfo->awb_end_addr));
#endif
#if defined(OTP_HEADER_AP_SHADING_START_ADDR)
		finfo->shading_start_addr = *((u32 *)&buf[OTP_HEADER_AP_SHADING_START_ADDR]) - start_addr;
		finfo->shading_end_addr = *((u32 *)&buf[OTP_HEADER_AP_SHADING_END_ADDR]) - start_addr;
		if (finfo->shading_end_addr > 0x1fff) {
			err("Shading end_addr has error!! 0x%08x", finfo->shading_end_addr);
			finfo->setfile_end_addr = 0x1fff;
		}
		pr_info("Shading start = 0x%08x, end = 0x%08x\n",
			(finfo->shading_start_addr), (finfo->shading_end_addr));
#endif


#if defined(OTP_HEADER_PROJECT_NAME_START_ADDR)
		memcpy(finfo->project_name, &buf[OTP_HEADER_PROJECT_NAME_START_ADDR], FIMC_IS_PROJECT_NAME_SIZE);
		finfo->project_name[FIMC_IS_PROJECT_NAME_SIZE] = '\0';
		finfo->header_section_crc_addr = OTP_CHECKSUM_HEADER_ADDR;
#endif
#if defined(OTP_OEM_VER_START_ADDR)
		/* OEM Data : Module/Manufacturer Information */
		memcpy(finfo->oem_ver, &buf[OTP_OEM_VER_START_ADDR], FIMC_IS_OEM_VER_SIZE);
		finfo->oem_ver[FIMC_IS_OEM_VER_SIZE] = '\0';
		finfo->oem_section_crc_addr = OTP_CHECKSUM_OEM_ADDR;
#endif
#if defined(OTP_AWB_VER_START_ADDR)
		/* AWB Data : Module/Manufacturer Information */
		memcpy(finfo->awb_ver, &buf[OTP_AWB_VER_START_ADDR], FIMC_IS_AWB_VER_SIZE);
		finfo->awb_ver[FIMC_IS_AWB_VER_SIZE] = '\0';
		finfo->awb_section_crc_addr = OTP_CHECKSUM_AWB_ADDR;
#endif
#if defined(OTP_AP_SHADING_VER_START_ADDR)
		/* SHADING Data : Module/Manufacturer Information */
		memcpy(finfo->shading_ver, &buf[OTP_AP_SHADING_VER_START_ADDR], FIMC_IS_SHADING_VER_SIZE);
		finfo->shading_ver[FIMC_IS_SHADING_VER_SIZE] = '\0';
		finfo->shading_section_crc_addr = OTP_CHECKSUM_AP_SHADING_ADDR;

		finfo->af_cal_pan = *((u32 *)&buf[OTPROM_AF_CAL_PAN_ADDR]);
		finfo->af_cal_macro = *((u32 *)&buf[OTPROM_AF_CAL_MACRO_ADDR]);
#endif
#endif
	}

	if(finfo->cal_map_ver[0] != 'V') {
		pr_info("Camera: Cal Map version read fail or there's no available data.\n");
		crc32_check_factory_front = false;
		goto exit;
	}

	pr_info("Camera: OTPROM Cal map_version = %c%c%c%c\n", finfo->cal_map_ver[0],
			finfo->cal_map_ver[1], finfo->cal_map_ver[2], finfo->cal_map_ver[3]);

	/* debug info dump */
	pr_info("++++ OTPROM data info\n");
	pr_info(" Header info\n");
	pr_info(" Module info : %s\n", finfo->header_ver);
	pr_info(" ID : %c\n", finfo->header_ver[FW_CORE_VER]);
	pr_info(" Pixel num : %c%c\n", finfo->header_ver[FW_PIXEL_SIZE], finfo->header_ver[FW_PIXEL_SIZE+1]);
	pr_info(" ISP ID : %c\n", finfo->header_ver[FW_ISP_COMPANY]);
	pr_info(" Sensor Maker : %c\n", finfo->header_ver[FW_SENSOR_MAKER]);
	pr_info(" Year : %c\n", finfo->header_ver[FW_PUB_YEAR]);
	pr_info(" Month : %c\n", finfo->header_ver[FW_PUB_MON]);
	pr_info(" Release num : %c%c\n", finfo->header_ver[FW_PUB_NUM], finfo->header_ver[FW_PUB_NUM+1]);
	pr_info(" Manufacturer ID : %c\n", finfo->header_ver[FW_MODULE_COMPANY]);
	pr_info(" Module ver : %c\n", finfo->header_ver[FW_VERSION_INFO]);
	pr_info(" Module ID : %c%c%c%c%c%X%X%X%X%X\n",
			finfo->module_id[0], finfo->module_id[1], finfo->module_id[2],
			finfo->module_id[3], finfo->module_id[4], finfo->module_id[5],
			finfo->module_id[6], finfo->module_id[7], finfo->module_id[8],
			finfo->module_id[9]);

	pr_info("---- OTPROM data info\n");

	/* CRC check */
#if defined(CONFIG_CAMERA_OTPROM_SUPPORT_FRONT)
	if (!fimc_is_sec_check_front_otp_crc32(buf) && (retry > 0)) {
		retry--;
		goto crc_retry;
	}
#else
	if (!fimc_is_sec_check_cal_crc32(buf, position) && (retry > 0)) {
		retry--;
		goto crc_retry;
	}
#endif

#if defined(OTP_MODE_CHANGE)
	/* 6. return to original mode */
	ret = fimc_is_sec_set_registers(client,
	sensor_mode_change_from_OTP_reg, sensor_mode_change_from_OTP_reg_size);
	if (unlikely(ret)) {
		err("failed to fimc_is_sec_set_registers (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}
#endif

	if (position == SENSOR_POSITION_FRONT) {
		if (finfo->header_ver[3] == 'L') {
			crc32_check_factory_front = crc32_check_front;
		} else {
			crc32_check_factory_front = false;
		}
	} else if (position == SENSOR_POSITION_REAR2) {
		if (finfo->header_ver[3] == 'L') {
			crc32_check_factory_rear2 = crc32_check;
		} else {
			crc32_check_factory_rear2 = false;
		}

	} else if (position == SENSOR_POSITION_REAR3) {
		if (finfo->header_ver[3] == 'L') {
			crc32_check_factory_rear3 = crc32_check;
		} else {
			crc32_check_factory_rear3 = false;
		}
	} else {
		if (finfo->header_ver[3] == 'L') {
			crc32_check_factory = crc32_check;
		} else {
			crc32_check_factory = false;
		}
	}

	if (!specific->use_module_check) {
		if (position == SENSOR_POSITION_REAR2) {
			is_latest_cam_module_rear2 = true;
		} else if (position == SENSOR_POSITION_REAR3) {
			is_latest_cam_module_rear3 = true;
		} else {
			is_latest_cam_module = true;
		}
	} else {
#if defined(CAMERA_MODULE_ES_VERSION_REAR)
		if (position == SENSOR_POSITION_REAR && sysfs_finfo.header_ver[10] >= CAMERA_MODULE_ES_VERSION_REAR) {
			is_latest_cam_module = true;
		//} else if (position == SENSOR_POSITION_REAR2 && sysfs_finfo.header_ver[10] >= CAMERA_MODULE_ES_VERSION_REAR2) { //It need to check about rear2
		//	is_latest_cam_module_rear2 = true;
		} else
#endif
		{
			is_latest_cam_module = false;
		}
	}

	if (position == SENSOR_POSITION_REAR) {
		if (specific->use_module_check) {
			if (finfo->header_ver[10] == FIMC_IS_LATEST_FROM_VERSION_M) {
				is_final_cam_module = true;
			} else {
				is_final_cam_module = false;
			}
		} else {
			is_final_cam_module = true;
		}
	 } else if (position == SENSOR_POSITION_REAR2) {
		if (specific->use_module_check) {
			if (finfo->header_ver[10] == FIMC_IS_LATEST_FROM_VERSION_M) {
				is_final_cam_module_rear2 = true;
			} else {
				is_final_cam_module_rear2 = false;
			}
		} else {
			is_final_cam_module = true;
		}
	 } else if (position == SENSOR_POSITION_REAR3) {
		if (specific->use_module_check) {
			if (finfo->header_ver[10] == FIMC_IS_LATEST_FROM_VERSION_M) {
				is_final_cam_module_rear3 = true;
			} else {
				is_final_cam_module_rear3 = false;
			}
		} else {
			is_final_cam_module = true;
		}
	} else {
		if (specific->use_module_check) {
			if (finfo->header_ver[10] == FIMC_IS_LATEST_FROM_VERSION_M) {
				is_final_cam_module_front = true;
			} else {
				is_final_cam_module_front = false;
			}
		} else {
			is_final_cam_module_front = true;
		}
	}

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	key_fp = filp_open("/data/media/0/1q2w3e4r.key", O_RDONLY, 0);
	if (IS_ERR(key_fp)) {
		pr_info("KEY does not exist.\n");
		key_fp = NULL;
		goto key_err;
	} else {
		dump_fp = filp_open("/data/media/0/dump", O_RDONLY, 0);
		if (IS_ERR(dump_fp)) {
			pr_info("dump folder does not exist.\n");
			dump_fp = NULL;
			goto key_err;
		} else {
			pr_info("dump folder exist, Dump OTPROM cal data.\n");
			if (write_data_to_file("/data/media/0/dump/otprom_cal.bin", buf,
									OTP_USED_CAL_SIZE, &pos) < 0) {
				pr_info("Failed to dump cal data.\n");
				goto dump_err;
			}
		}
	}

dump_err:
	if (dump_fp)
		filp_close(dump_fp, current->files);
key_err:
	if (key_fp)
		filp_close(key_fp, current->files);
	set_fs(old_fs);

exit:
	fimc_is_i2c_config(client, false);

	pr_info("fimc_is_sec_readcal_otprom_5e9 X\n");
	return ret;
}
#else //CAMERA_OTPROM_SUPPORT_FRONT_5E9 end

#if defined(CONFIG_CAMERA_CIS_GC5035_OBJ)
int fimc_is_i2c_read_otp_gc5035(struct i2c_client *client, char *buf)
{
	int index_h = 0;
	int index_l = 0;
	u8 start_addr_h = 0;
	u8 start_addr_l = 0;
	int ret = 0;

	ret = fimc_is_sec_set_registers(client,sensor_Global_gc5035, sensor_Global_gc5035_size);
	
	if (unlikely(ret)) {
		err("failed to fimc_is_sec_set_registers (%d)\n", ret);
		ret = -EINVAL;
	}

	fimc_is_sec_set_registers(client, sensor_mode_read_initial_setting,sensor_mode_read_initial_setting_size);

	start_addr_h = 0x10;//otp start address high in bits
	start_addr_l = 0x00;//otp start address low in bits
	
	for(index_h = 0; index_h < 8 ; index_h++)
	{
		start_addr_l = 0x00;
		for(index_l = 0; index_l < 32 ; index_l++)
		{
			fimc_is_sensor_addr8_write8(client, 0x69, start_addr_h);//addr High Bit
			fimc_is_sensor_addr8_write8(client, 0x6a, start_addr_l);//addr Low Bit
			fimc_is_sensor_addr8_write8(client, 0xf3, 0x20);//OTP Read pulse
			msleep(1);
			fimc_is_sensor_addr8_read8(client, 0x6c, buf+ (index_h*32 + index_l));
			//pr_info("Camera otp data = 0x%x  0x%x %d %c \n", start_addr_h,start_addr_l,(index_h*32 + index_l),buf[index_h*32 + index_l]);
			start_addr_l = start_addr_l + 8;
		}
		start_addr_h++ ;
	}

	return ret;
}
#endif

int fimc_is_sec_readcal_otprom_legacy(struct device *dev, int position)
{
	int ret = 0;
	char *buf = NULL;
	int retry = FIMC_IS_CAL_RETRY_CNT;
	struct fimc_is_core *core = dev_get_drvdata(dev);
	struct fimc_is_from_info *finfo = NULL;
	struct fimc_is_vender_specific *specific = core->vender.private_data;
	struct i2c_client *client = NULL;
	struct file *key_fp = NULL;
	struct file *dump_fp = NULL;
	mm_segment_t old_fs;
	loff_t pos = 0;
	int cal_size = 0;
#ifdef OTP_BANK
	u8 data8 = 0;
	int otp_bank = 0;
#endif
#ifdef OTP_SINGLE_READ_ADDR
	int i = 0;
	u8 start_addr_h = 0;
	u8 start_addr_l= 0;
#endif
	u16 start_addr = 0;

	pr_info("fimc_is_sec_readcal_otprom E\n");

	if (position == SENSOR_POSITION_FRONT) {
#if defined(CONFIG_CAMERA_OTPROM_SUPPORT_FRONT)
		finfo = &sysfs_finfo_front;
		fimc_is_sec_get_front_cal_buf(&buf);
		cal_size = FIMC_IS_MAX_CAL_SIZE_FRONT;
#if defined(CONFIG_USE_DIRECT_IS_CONTROL)
		client = specific->front_cis_client;
#else
		client = specific->eeprom_client[position];
#endif
#endif /* defined(CONFIG_CAMERA_OTPROM_SUPPORT_FRONT) */
	} else {
#if defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR)
		finfo = &sysfs_finfo;
		fimc_is_sec_get_cal_buf(&buf);
		cal_size = FIMC_IS_MAX_CAL_SIZE;
#if defined(CONFIG_USE_DIRECT_IS_CONTROL)
		client = specific->rear_cis_client;
#else
		client = specific->eeprom_client[position];
#endif
#endif /* defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR) */
	}

	if (!client) {
		err("eeprom i2c client is NULL\n");
		return -EINVAL;
	}

	fimc_is_i2c_config(client, true);
	msleep(10);

#if defined(OTP_NEED_INIT_SETTING)
	/* 0. sensor init */
	if (!force_caldata_dump) {
		ret = specific->cis_init_reg_write();
		if (unlikely(ret)) {
			err("failed to fimc_is_i2c_write (%d)\n", ret);
			ret = -EINVAL;
			goto exit;
		}
	}
#endif

#if defined(OTP_MODE_CHANGE)
	/* 1. mode change to OTP */
	ret = fimc_is_sec_set_registers(client,
		sensor_mode_change_to_OTP_reg, sensor_mode_change_to_OTP_reg_size);
	if (unlikely(ret)) {
		err("failed to fimc_is_sec_set_registers (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}
#endif

#if defined(OTP_BANK)
#if defined(OTP_SINGLE_READ_ADDR)
	/* 2. single read OTP Bank */
	fimc_is_sensor_write8(client, OTP_START_ADDR_HIGH, OTP_BANK_ADDR_HIGH);
	fimc_is_sensor_write8(client, OTP_START_ADDR_LOW, OTP_BANK_ADDR_LOW);
	fimc_is_sensor_write8(client, OTP_SINGLE_READ, 0x01);
	fimc_is_sensor_read8(client, OTP_SINGLE_READ_ADDR, &data8);

	otp_bank = data8;

	pr_info("Camera: otp_bank = %d\n", otp_bank);

	/* 3. selected page setting */
	switch(otp_bank) {
	case 1 :
		start_addr_h = OTP_BANK1_START_ADDR_HIGH;
		start_addr_l = OTP_BANK1_START_ADDR_LOW;
		break;
	case 3 :
		start_addr_h = OTP_BANK2_START_ADDR_HIGH;
		start_addr_l = OTP_BANK2_START_ADDR_LOW;
		break;
	case 7 :
		start_addr_h = OTP_BANK3_START_ADDR_HIGH;
		start_addr_l = OTP_BANK3_START_ADDR_LOW;
		break;
	default :
		start_addr_h = OTP_BANK1_START_ADDR_HIGH;
		start_addr_l = OTP_BANK1_START_ADDR_LOW;
		break;
	}
	start_addr = ((start_addr_h << 8)&0xff00) | (start_addr_l&0xff);
#else
	/* 2. read OTP Bank */
	fimc_is_sensor_read8(client, OTP_BANK_ADDR, &data8);

	otp_bank = data8;

	pr_info("Camera: otp_bank = %d\n", otp_bank);
	start_addr = OTP_START_ADDR;

	/* 3. selected page setting */
	switch(otp_bank) {
	case 1 :
		ret = fimc_is_sec_set_registers(client,
				OTP_first_page_select_reg, OTP_first_page_select_reg_size);
		break;
	case 3 :
		ret = fimc_is_sec_set_registers(client,
				OTP_second_page_select_reg, OTP_second_page_select_reg_size);
		break;
	default :
		ret = fimc_is_sec_set_registers(client,
				OTP_first_page_select_reg, OTP_first_page_select_reg_size);
		break;
	}
	if (unlikely(ret)) {
		err("failed to fimc_is_sec_set_registers (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}
#endif
#endif

crc_retry:

#if defined(OTP_BANK)
#if defined(OTP_SINGLE_READ_ADDR)
	fimc_is_sensor_write8(client, OTP_START_ADDR_HIGH, start_addr_h);
	fimc_is_sensor_write8(client, OTP_START_ADDR_LOW, start_addr_l);
	fimc_is_sensor_write8(client, OTP_SINGLE_READ, 0x01);

	/* 5. full read cal data */
	pr_info("Camera: I2C read full cal data\n\n");
	for (i = 0; i < OTP_USED_CAL_SIZE; i++) {
		fimc_is_sensor_read8(client, OTP_SINGLE_READ_ADDR, &data8);
		buf[i] = data8;
	}
#else
	/* read cal data */
	pr_info("Camera: I2C read cal data\n\n");
	fimc_is_i2c_read(client, buf, start_addr, OTP_USED_CAL_SIZE);
#endif
#endif
#if defined(CONFIG_CAMERA_CIS_GC5035_OBJ)
	ret = fimc_is_i2c_read_otp_gc5035(client,buf);
#endif

	if (position == SENSOR_POSITION_FRONT) {
#if defined(CONFIG_CAMERA_OTPROM_SUPPORT_FRONT)
#if defined(OTP_HEADER_OEM_START_ADDR_FRONT)
		finfo->oem_start_addr = *((u32 *)&buf[OTP_HEADER_OEM_START_ADDR_FRONT]) - start_addr;
		finfo->oem_end_addr = *((u32 *)&buf[OTP_HEADER_OEM_END_ADDR_FRONT]) - start_addr;
		pr_info("OEM start = 0x%08x, end = 0x%08x\n",
			(finfo->oem_start_addr), (finfo->oem_end_addr));
#endif
#if defined(OTP_HEADER_AWB_START_ADDR_FRONT)
#ifdef OTP_HEADER_DIRECT_ADDR_FRONT
		finfo->awb_start_addr = OTP_HEADER_AWB_START_ADDR_FRONT - start_addr;
		finfo->awb_end_addr = OTP_HEADER_AWB_END_ADDR_FRONT - start_addr;
#else
		finfo->awb_start_addr = *((u32 *)&buf[OTP_HEADER_AWB_START_ADDR_FRONT]) - start_addr;
		finfo->awb_end_addr = *((u32 *)&buf[OTP_HEADER_AWB_END_ADDR_FRONT]) - start_addr;
#endif
		pr_info("AWB start = 0x%08x, end = 0x%08x\n",
			(finfo->awb_start_addr), (finfo->awb_end_addr));
#endif
#if defined(OTP_HEADER_SHADING_START_ADDR_FRONT)
		finfo->shading_start_addr = *((u32 *)&buf[OTP_HEADER_AP_SHADING_START_ADDR_FRONT]) - start_addr;
		finfo->shading_end_addr = *((u32 *)&buf[OTP_HEADER_AP_SHADING_END_ADDR_FRONT]) - start_addr;
		pr_info("Shading start = 0x%08x, end = 0x%08x\n",
			(finfo->shading_start_addr), (finfo->shading_end_addr));
		if (finfo->shading_end_addr > 0x3AFF) {
			err("Shading end_addr has error!! 0x%08x", finfo->shading_end_addr);
			finfo->shading_end_addr = 0x3AFF;
		}
#endif
		/* HEARDER Data : Module/Manufacturer Information */
		memcpy(finfo->header_ver, &buf[OTP_HEADER_VERSION_START_ADDR_FRONT], FIMC_IS_HEADER_VER_SIZE);
		finfo->header_ver[FIMC_IS_HEADER_VER_SIZE] = '\0';
		/* HEARDER Data : Cal Map Version */
		memcpy(finfo->cal_map_ver,
		       &buf[OTP_HEADER_CAL_MAP_VER_START_ADDR_FRONT], FIMC_IS_CAL_MAP_VER_SIZE);
		pr_info("FRONT OTPROM header version = %s\n", finfo->header_ver);
#ifdef OTP_HEADER_MODULE_ID_ADDR_FRONT
		memcpy(finfo->module_id, &buf[OTP_HEADER_MODULE_ID_ADDR_FRONT], FIMC_IS_MODULE_ID_SIZE);
#else
		memset(finfo->module_id, 0x0, FIMC_IS_MODULE_ID_SIZE);
#endif
#if defined(OTP_HEADER_PROJECT_NAME_START_ADDR_FRONT)
		memcpy(finfo->project_name,
		       &buf[OTP_HEADER_PROJECT_NAME_START_ADDR_FRONT], FIMC_IS_PROJECT_NAME_SIZE);
		finfo->project_name[FIMC_IS_PROJECT_NAME_SIZE] = '\0';
#endif
		finfo->header_section_crc_addr = OTP_CHECKSUM_HEADER_ADDR_FRONT;
#if defined(OTP_HEADER_OEM_START_ADDR_FRONT)
		/* OEM Data : Module/Manufacturer Information */
		memcpy(finfo->oem_ver, &buf[OTP_OEM_VER_START_ADDR_FRONT], FIMC_IS_OEM_VER_SIZE);
		finfo->oem_ver[FIMC_IS_OEM_VER_SIZE] = '\0';
		finfo->oem_section_crc_addr = OTP_CHECKSUM_OEM_ADDR_FRONT;
#endif
#if defined(OTP_AWB_VER_START_ADDR_FRONT)
		/* AWB Data : Module/Manufacturer Information */
		memcpy(finfo->awb_ver, &buf[OTP_AWB_VER_START_ADDR_FRONT], FIMC_IS_AWB_VER_SIZE);
		finfo->awb_ver[FIMC_IS_AWB_VER_SIZE] = '\0';
		finfo->awb_section_crc_addr = OTP_CHECKSUM_AWB_ADDR_FRONT;
#endif
#if defined(OTP_AP_SHADING_VER_START_ADDR_FRONT)
		/* SHADING Data : Module/Manufacturer Information */
		memcpy(finfo->shading_ver, &buf[OTP_AP_SHADING_VER_START_ADDR_FRONT], FIMC_IS_SHADING_VER_SIZE);
		finfo->shading_ver[FIMC_IS_SHADING_VER_SIZE] = '\0';
		finfo->shading_section_crc_addr = OTP_CHECKSUM_AP_SHADING_ADDR_FRONT;
#endif
#endif
	} else {
#if defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR)
		finfo->oem_start_addr = *((u32 *)&buf[OTP_HEADER_OEM_START_ADDR]) - start_addr;
		finfo->oem_end_addr = *((u32 *)&buf[OTP_HEADER_OEM_END_ADDR]) - start_addr;
		pr_info("OEM start = 0x%08x, end = 0x%08x\n",
			(finfo->oem_start_addr), (finfo->oem_end_addr));
		finfo->awb_start_addr = *((u32 *)&buf[OTP_HEADER_AWB_START_ADDR]) - start_addr;
		finfo->awb_end_addr = *((u32 *)&buf[OTP_HEADER_AWB_END_ADDR]) - start_addr;
		pr_info("AWB start = 0x%08x, end = 0x%08x\n",
			(finfo->awb_start_addr), (finfo->awb_end_addr));
		finfo->shading_start_addr = *((u32 *)&buf[OTP_HEADER_AP_SHADING_START_ADDR]) - start_addr;
		finfo->shading_end_addr = *((u32 *)&buf[OTP_HEADER_AP_SHADING_END_ADDR]) - start_addr;
		if (finfo->shading_end_addr > 0x1fff) {
			err("Shading end_addr has error!! 0x%08x", finfo->shading_end_addr);
			finfo->setfile_end_addr = 0x1fff;
		}
		pr_info("Shading start = 0x%08x, end = 0x%08x\n",
			(finfo->shading_start_addr), (finfo->shading_end_addr));

		/* HEARDER Data : Module/Manufacturer Information */
		memcpy(finfo->header_ver, &buf[OTP_HEADER_VERSION_START_ADDR], FIMC_IS_HEADER_VER_SIZE);
		finfo->header_ver[FIMC_IS_HEADER_VER_SIZE] = '\0';
		/* HEARDER Data : Cal Map Version */
		memcpy(finfo->cal_map_ver, &buf[OTP_HEADER_CAL_MAP_VER_START_ADDR], FIMC_IS_CAL_MAP_VER_SIZE);
#ifdef OTP_HEADER_MODULE_ID_ADDR
		memcpy(finfo->module_id, &buf[OTP_HEADER_MODULE_ID_ADDR], FIMC_IS_MODULE_ID_SIZE);
#else
		memset(finfo->module_id, 0x0, FIMC_IS_MODULE_ID_SIZE);
#endif
		memcpy(finfo->project_name, &buf[OTP_HEADER_PROJECT_NAME_START_ADDR], FIMC_IS_PROJECT_NAME_SIZE);
		finfo->project_name[FIMC_IS_PROJECT_NAME_SIZE] = '\0';
		finfo->header_section_crc_addr = OTP_CHECKSUM_HEADER_ADDR;

		/* OEM Data : Module/Manufacturer Information */
		memcpy(finfo->oem_ver, &buf[OTP_OEM_VER_START_ADDR], FIMC_IS_OEM_VER_SIZE);
		finfo->oem_ver[FIMC_IS_OEM_VER_SIZE] = '\0';
		finfo->oem_section_crc_addr = OTP_CHECKSUM_OEM_ADDR;

		/* AWB Data : Module/Manufacturer Information */
		memcpy(finfo->awb_ver, &buf[OTP_AWB_VER_START_ADDR], FIMC_IS_AWB_VER_SIZE);
		finfo->awb_ver[FIMC_IS_AWB_VER_SIZE] = '\0';
		finfo->awb_section_crc_addr = OTP_CHECKSUM_AWB_ADDR;

		/* SHADING Data : Module/Manufacturer Information */
		memcpy(finfo->shading_ver, &buf[OTP_AP_SHADING_VER_START_ADDR], FIMC_IS_SHADING_VER_SIZE);
		finfo->shading_ver[FIMC_IS_SHADING_VER_SIZE] = '\0';
		finfo->shading_section_crc_addr = OTP_CHECKSUM_AP_SHADING_ADDR;

		sysfs_finfo.af_cal_pan = *((u32 *)&cal_buf[OTPROM_AF_CAL_PAN_ADDR]);
		sysfs_finfo.af_cal_macro = *((u32 *)&cal_buf[OTPROM_AF_CAL_MACRO_ADDR]);
#endif
	}

	if(finfo->cal_map_ver[0] != 'V') {
		pr_info("Camera: Cal Map version read fail or there's no available data.\n");
		crc32_check_factory_front = false;
		goto exit;
	}

	pr_info("Camera: OTPROM Cal map_version = %c%c%c%c\n", finfo->cal_map_ver[0],
			finfo->cal_map_ver[1], finfo->cal_map_ver[2], finfo->cal_map_ver[3]);

	/* debug info dump */
	pr_info("++++ OTPROM data info\n");
	pr_info(" Header info\n");
	pr_info(" Module info : %s\n", finfo->header_ver);
	pr_info(" ID : %c\n", finfo->header_ver[FW_CORE_VER]);
	pr_info(" Pixel num : %c%c\n", finfo->header_ver[FW_PIXEL_SIZE], finfo->header_ver[FW_PIXEL_SIZE+1]);
	pr_info(" ISP ID : %c\n", finfo->header_ver[FW_ISP_COMPANY]);
	pr_info(" Sensor Maker : %c\n", finfo->header_ver[FW_SENSOR_MAKER]);
	pr_info(" Year : %c\n", finfo->header_ver[FW_PUB_YEAR]);
	pr_info(" Month : %c\n", finfo->header_ver[FW_PUB_MON]);
	pr_info(" Release num : %c%c\n", finfo->header_ver[FW_PUB_NUM],	finfo->header_ver[FW_PUB_NUM+1]);
	pr_info(" Manufacturer ID : %c\n", finfo->header_ver[FW_MODULE_COMPANY]);
	pr_info(" Module ver : %c\n", finfo->header_ver[FW_VERSION_INFO]);
	pr_info("---- OTPROM data info\n");

	/* CRC check */
#if defined(CONFIG_CAMERA_OTPROM_SUPPORT_FRONT)
	if (!fimc_is_sec_check_front_otp_crc32(buf) && (retry > 0)) {
		retry--;
		goto crc_retry;
	}
#else
	if (!fimc_is_sec_check_cal_crc32(buf, position) && (retry > 0)) {
		retry--;
		goto crc_retry;
	}
#endif

#if defined(OTP_MODE_CHANGE)
	/* 6. return to original mode */
	ret = fimc_is_sec_set_registers(client,
		sensor_mode_change_from_OTP_reg, sensor_mode_change_from_OTP_reg_size);
	if (unlikely(ret)) {
		err("failed to fimc_is_sec_set_registers (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}
#endif

	if (position == SENSOR_POSITION_FRONT) {
		if (finfo->header_ver[3] == 'L') {
			crc32_check_factory_front = crc32_check_front;
		} else {
			crc32_check_factory_front = false;
		}
	} else {
		if (finfo->header_ver[3] == 'L') {
			crc32_check_factory = crc32_check;
		} else {
			crc32_check_factory = false;
		}
	}

	if (!specific->use_module_check) {
		is_latest_cam_module = true;
	} else {
		if (sysfs_finfo.header_ver[10] >= CAMERA_MODULE_ES_VERSION_REAR) {
			is_latest_cam_module = true;
		} else {
			is_latest_cam_module = false;
		}
	}

	if (position == SENSOR_POSITION_REAR) {
		if (specific->use_module_check) {
			if (finfo->header_ver[10] == FIMC_IS_LATEST_ROM_VERSION_M) {
				is_final_cam_module = true;
			} else {
				is_final_cam_module = false;
			}
		} else {
			is_final_cam_module = true;
		}
	} else {
		if (specific->use_module_check) {
			if (finfo->header_ver[10] == FIMC_IS_LATEST_ROM_VERSION_M) {
				is_final_cam_module_front = true;
			} else {
				is_final_cam_module_front = false;
			}
		} else {
			is_final_cam_module_front = true;
		}
	}
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	key_fp = filp_open("/data/media/0/1q2w3e4r.key", O_RDONLY, 0);
	if (IS_ERR(key_fp)) {
		pr_info("KEY does not exist.\n");
		key_fp = NULL;
		goto key_err;
	} else {
		dump_fp = filp_open("/data/media/0/dump", O_RDONLY, 0);
		if (IS_ERR(dump_fp)) {
			pr_info("dump folder does not exist.\n");
			dump_fp = NULL;
			goto key_err;
		} else {
			pr_info("dump folder exist, Dump OTPROM cal data.\n");
			if (write_data_to_file("/data/media/0/dump/otprom_cal.bin", buf,
									OTP_USED_CAL_SIZE, &pos) < 0) {
				pr_info("Failed to dump cal data.\n");
				goto dump_err;
			}
		}
	}

dump_err:
	if (dump_fp)
		filp_close(dump_fp, current->files);
key_err:
	if (key_fp)
		filp_close(key_fp, current->files);
	set_fs(old_fs);

exit:
	fimc_is_i2c_config(client, false);

	pr_info("fimc_is_sec_readcal_otprom X\n");

	return ret;
}
#endif	//CAMERA_OTPROM_SUPPORT_FRONT_5E9

int fimc_is_sec_readcal_otprom(struct device *dev, int position)
{
	int ret = 0;
#if defined(CAMERA_OTPROM_SUPPORT_FRONT_5E9)
	ret = fimc_is_sec_readcal_otprom_5e9(dev, position);
#else
	ret = fimc_is_sec_readcal_otprom_legacy(dev, position);
#endif
	return ret;
}
#endif /* CONFIG_CAMERA_OTPROM_SUPPORT_REAR || CONFIG_CAMERA_OTPROM_SUPPORT_FRONT */

#if !defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR) && !defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR)
int fimc_is_sec_read_from_header(struct device *dev)
{
	int ret = 0;
	struct fimc_is_core *core = dev_get_drvdata(fimc_is_dev);
	u8 header_version[FIMC_IS_HEADER_VER_SIZE + 1] = {0, };

	ret = fimc_is_spi_read(&core->spi0, header_version, FROM_HEADER_VERSION_START_ADDR, FIMC_IS_HEADER_VER_SIZE);
	if (ret < 0) {
		printk(KERN_ERR "failed to fimc_is_spi_read for header version (%d)\n", ret);
		ret = -EINVAL;
	}

	memcpy(sysfs_finfo.header_ver, header_version, FIMC_IS_HEADER_VER_SIZE);
	sysfs_finfo.header_ver[FIMC_IS_HEADER_VER_SIZE] = '\0';

	return ret;
}

int fimc_is_sec_check_status(struct fimc_is_core *core)
{
	int retry_read = 50;
	u8 temp[5] = {0x0, };
	int ret = 0;

	do {
		memset(temp, 0x0, sizeof(temp));
		fimc_is_spi_read_status_bit(&core->spi0, &temp[0]);
		if (retry_read < 0) {
			ret = -EINVAL;
			err("check status failed.");
			break;
		}
		retry_read--;
		msleep(3);
	} while (temp[0]);

	return ret;
}

#ifdef CAMERA_MODULE_DUALIZE
int fimc_is_sec_read_fw_from_sdcard(char *name, unsigned long *size)
{
	struct file *fw_fp = NULL;
	mm_segment_t old_fs;
	loff_t pos = 0;
	char data_path[100];
	int ret = 0;
	unsigned long fsize;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	snprintf(data_path, sizeof(data_path), "%s", name);
	memset(fw_buf, 0x0, FIMC_IS_MAX_FW_SIZE);

	fw_fp = filp_open(data_path, O_RDONLY, 0);
	if (IS_ERR_OR_NULL(fw_fp)) {
		info("%s does not exist.\n", data_path);
		fw_fp = NULL;
		ret = -EIO;
		goto fw_err;
	} else {
		info("%s exist, Dump from sdcard.\n", name);
		fsize = fw_fp->f_path.dentry->d_inode->i_size;
		read_data_from_file(name, fw_buf, fsize, &pos);
		*size = fsize;
	}

fw_err:
	if (fw_fp)
		filp_close(fw_fp, current->files);
	set_fs(old_fs);

	return ret;
}

u32 fimc_is_sec_get_fw_crc32(char *buf, size_t size)
{
	u32 *buf32 = NULL;
	u32 checksum;

	buf32 = (u32 *)buf;
	checksum = (u32)getCRC((u16 *)&buf32[0], size, NULL, NULL);

	return checksum;
}

int fimc_is_sec_change_from_header(struct fimc_is_core *core)
{
	int ret = 0;
	u8 crc_value[4];
	u32 crc_result = 0;

	/* read header data */
	info("Camera: Start SPI read header data\n");
	memset(fw_buf, 0x0, FIMC_IS_MAX_FW_SIZE);

	ret = fimc_is_spi_read(&core->spi0, fw_buf, 0x0, HEADER_CRC32_LEN);
	if (ret) {
		err("failed to fimc_is_spi_read (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}

	fw_buf[0x7] = (sysfs_finfo.bin_end_addr & 0xFF000000) >> 24;
	fw_buf[0x6] = (sysfs_finfo.bin_end_addr & 0xFF0000) >> 16;
	fw_buf[0x5] = (sysfs_finfo.bin_end_addr & 0xFF00) >> 8;
	fw_buf[0x4] = (sysfs_finfo.bin_end_addr & 0xFF);
	fw_buf[0x27] = (sysfs_finfo.setfile_end_addr & 0xFF000000) >> 24;
	fw_buf[0x26] = (sysfs_finfo.setfile_end_addr & 0xFF0000) >> 16;
	fw_buf[0x25] = (sysfs_finfo.setfile_end_addr & 0xFF00) >> 8;
	fw_buf[0x24] = (sysfs_finfo.setfile_end_addr & 0xFF);
	fw_buf[0x37] = (sysfs_finfo.concord_bin_end_addr & 0xFF000000) >> 24;
	fw_buf[0x36] = (sysfs_finfo.concord_bin_end_addr & 0xFF0000) >> 16;
	fw_buf[0x35] = (sysfs_finfo.concord_bin_end_addr & 0xFF00) >> 8;
	fw_buf[0x34] = (sysfs_finfo.concord_bin_end_addr & 0xFF);

	strncpy(&fw_buf[0x40], sysfs_finfo.header_ver, 9);
	strncpy(&fw_buf[0x50], sysfs_finfo.concord_header_ver, FIMC_IS_HEADER_VER_SIZE);
	strncpy(&fw_buf[0x64], sysfs_finfo.setfile_ver, FIMC_IS_ISP_SETFILE_VER_SIZE);

	fimc_is_spi_write_enable(&core->spi0);
	ret = fimc_is_spi_erase_sector(&core->spi0, 0x0);
	if (ret) {
		err("failed to fimc_is_spi_erase_sector (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}
	ret = fimc_is_sec_check_status(core);
	if (ret) {
		err("failed to fimc_is_sec_check_status (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}

	ret = fimc_is_spi_write_enable(&core->spi0);
	ret = fimc_is_spi_write(&core->spi0, 0x0, fw_buf, HEADER_CRC32_LEN);
	if (ret) {
		err("failed to fimc_is_spi_write (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}
	ret = fimc_is_sec_check_status(core);
	if (ret) {
		err("failed to fimc_is_sec_check_status (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}

	crc_result = fimc_is_sec_get_fw_crc32(fw_buf, HEADER_CRC32_LEN);
	crc_value[3] = (crc_result & 0xFF000000) >> 24;
	crc_value[2] = (crc_result & 0xFF0000) >> 16;
	crc_value[1] = (crc_result & 0xFF00) >> 8;
	crc_value[0] = (crc_result & 0xFF);

	ret = fimc_is_spi_write_enable(&core->spi0);
	ret = fimc_is_spi_write(&core->spi0, 0x0FFC, crc_value, 0x4);
	if (ret) {
		err("failed to fimc_is_spi_write (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}
	ret = fimc_is_sec_check_status(core);
	if (ret) {
		err("failed to fimc_is_sec_check_status (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}

	info("Camera: End SPI read header data\n");

exit:
	return ret;
}

int fimc_is_sec_write_fw_to_from(struct fimc_is_core *core, char *name, bool first_section)
{
	int ret = 0;
	unsigned long i = 0;
	unsigned long size = 0;
	u32 start_addr = 0, erase_addr = 0, end_addr = 0;
	u32 checksum_addr = 0, crc_result = 0, erase_end_addr = 0;
	u8 crc_value[4];

	if (!strcmp(name, FIMC_IS_FW_FROM_SDCARD)) {
		ret = fimc_is_sec_read_fw_from_sdcard(FIMC_IS_FW_FROM_SDCARD, &size);
		start_addr = sysfs_finfo.bin_start_addr;
		end_addr = (u32)size + start_addr - 1;
		sysfs_finfo.bin_end_addr = end_addr;
		checksum_addr = 0x3FFFFF;
		sysfs_finfo.fw_size = size;
		strncpy(sysfs_finfo.header_ver, &fw_buf[size - 11], 9);
	} else if (!strcmp(name, FIMC_IS_SETFILE_FROM_SDCARD)) {
		ret = fimc_is_sec_read_fw_from_sdcard(FIMC_IS_SETFILE_FROM_SDCARD, &size);
		start_addr = sysfs_finfo.setfile_start_addr;
		end_addr = (u32)size + start_addr - 1;
		sysfs_finfo.setfile_end_addr = end_addr;
		if (fimc_is_sec_fw_module_compare(sysfs_finfo.header_ver, FW_2P2_B))
			checksum_addr = FROM_WRITE_CHECKSUM_SETF_2P2_PLUS;
		else
			checksum_addr = FROM_WRITE_CHECKSUM_SETF_IMX240;
		sysfs_finfo.setfile_size = size;
		strncpy(sysfs_finfo.setfile_ver, &fw_buf[size - 64], 6);
	} else if (!strcmp(name, FIMC_IS_COMPANION_FROM_SDCARD)) {
		ret = fimc_is_sec_read_fw_from_sdcard(FIMC_IS_COMPANION_FROM_SDCARD, &size);
		start_addr = sysfs_finfo.concord_bin_start_addr;
		end_addr = (u32)size + start_addr - 1;
		sysfs_finfo.concord_bin_end_addr = end_addr;
		if (fimc_is_sec_fw_module_compare(sysfs_finfo.header_ver, FW_2P2_B))
			checksum_addr = FROM_WRITE_CHECKSUM_COMP_2P2_PLUS;
		else
			checksum_addr = FROM_WRITE_CHECKSUM_COMP_IMX240;
		erase_end_addr = 0x3FFFFF;
		sysfs_finfo.comp_fw_size = size;
		strncpy(sysfs_finfo.concord_header_ver, &fw_buf[size - 16], FIMC_IS_HEADER_VER_SIZE);
	} else {
		err("Not supported binary type.");
		return -EIO;
	}

	if (ret < 0) {
		err("FW is not exist in sdcard.");
		return -EIO;
	}

	info("Start %s write to FROM.\n", name);

	if (first_section) {
		for (erase_addr = start_addr; erase_addr < erase_end_addr; erase_addr += FIMC_IS_FROM_ERASE_SIZE) {
			ret = fimc_is_spi_write_enable(&core->spi0);
			ret |= fimc_is_spi_erase_block(&core->spi0, erase_addr);
			if (ret) {
				err("failed to fimc_is_spi_erase_block (%d)\n", ret);
				ret = -EINVAL;
				goto exit;
			}
			ret = fimc_is_sec_check_status(core);
			if (ret) {
				err("failed to fimc_is_sec_check_status (%d)\n", ret);
				ret = -EINVAL;
				goto exit;
			}
		}
	}

	for (i = 0; i < size; i += 256) {
		ret = fimc_is_spi_write_enable(&core->spi0);
		if (size - i >= 256) {
			ret = fimc_is_spi_write(&core->spi0, start_addr + i, fw_buf + i, 256);
			if (ret) {
				err("failed to fimc_is_spi_write (%d)\n", ret);
				ret = -EINVAL;
				goto exit;
			}
		} else {
			ret = fimc_is_spi_write(&core->spi0, start_addr + i, fw_buf + i, size - i);
			if (ret) {
				err("failed to fimc_is_spi_write (%d)\n", ret);
				ret = -EINVAL;
				goto exit;
			}
		}
		ret = fimc_is_sec_check_status(core);
		if (ret) {
			err("failed to fimc_is_sec_check_status (%d)\n", ret);
			ret = -EINVAL;
			goto exit;
		}
	}

	crc_result = fimc_is_sec_get_fw_crc32(fw_buf, size);
	crc_value[3] = (crc_result & 0xFF000000) >> 24;
	crc_value[2] = (crc_result & 0xFF0000) >> 16;
	crc_value[1] = (crc_result & 0xFF00) >> 8;
	crc_value[0] = (crc_result & 0xFF);

	ret = fimc_is_spi_write_enable(&core->spi0);
	if (ret) {
		err("failed to fimc_is_spi_write_enable (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}

	ret = fimc_is_spi_write(&core->spi0, checksum_addr -4 + 1, crc_value, 0x4);
	if (ret) {
		err("failed to fimc_is_spi_write (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}
	ret = fimc_is_sec_check_status(core);
	if (ret) {
		err("failed to fimc_is_sec_check_status (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}

	info("End %s write to FROM.\n", name);

exit:
	return ret;
}

int fimc_is_sec_write_fw(struct fimc_is_core *core, struct device *dev)
{
	int ret = 0;
#ifdef CONFIG_COMPANION_USE
	struct fimc_is_spi *spi = &core->spi0;
#endif
	struct file *key_fp = NULL;
	struct file *comp_fw_fp = NULL;
	struct file *setfile_fp = NULL;
	struct file *isp_fw_fp = NULL;
	mm_segment_t old_fs;
	struct fimc_is_vender_specific *specific = core->vender.private_data;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	key_fp = filp_open(FIMC_IS_KEY_FROM_SDCARD, O_RDONLY, 0);
	if (IS_ERR_OR_NULL(key_fp)) {
		info("KEY does not exist.\n");
		key_fp = NULL;
		ret = -EIO;
		goto key_err;
	} else {
		comp_fw_fp = filp_open(FIMC_IS_COMPANION_FROM_SDCARD, O_RDONLY, 0);
		if (IS_ERR_OR_NULL(comp_fw_fp)) {
			info("Companion FW does not exist.\n");
			comp_fw_fp = NULL;
			ret = -EIO;
			goto comp_fw_err;
		}

		setfile_fp = filp_open(FIMC_IS_SETFILE_FROM_SDCARD, O_RDONLY, 0);
		if (IS_ERR_OR_NULL(setfile_fp)) {
			info("setfile does not exist.\n");
			setfile_fp = NULL;
			ret = -EIO;
			goto setfile_err;
		}

		isp_fw_fp = filp_open(FIMC_IS_FW_FROM_SDCARD, O_RDONLY, 0);
		if (IS_ERR_OR_NULL(isp_fw_fp)) {
			info("ISP FW does not exist.\n");
			isp_fw_fp = NULL;
			ret = -EIO;
			goto isp_fw_err;
		}
	}

	info("FW file exist, Write Firmware to FROM .\n");

	if (fimc_is_sec_ldo_enabled(dev, "VDDIO_1.8V_CAM") <= 0)
		fimc_is_sec_rom_power_on(core, SENSOR_POSITION_REAR);

#ifdef CONFIG_COMPANION_USE
	fimc_is_spi_s_pin(spi, false);
#endif
	ret = fimc_is_sec_write_fw_to_from(core, FIMC_IS_COMPANION_FROM_SDCARD, true);
	if (ret) {
		err("fimc_is_sec_write_fw_to_from failed.");
		ret = -EIO;
		goto isp_fw_err;
	}

	ret = fimc_is_sec_write_fw_to_from(core, FIMC_IS_SETFILE_FROM_SDCARD, false);
	if (ret) {
		err("fimc_is_sec_write_fw_to_from failed.");
		ret = -EIO;
		goto isp_fw_err;
	}

	ret = fimc_is_sec_write_fw_to_from(core, FIMC_IS_FW_FROM_SDCARD, false);
	if (ret) {
		err("fimc_is_sec_write_fw_to_from failed.");
		ret = -EIO;
		goto isp_fw_err;
	}
#ifdef CONFIG_COMPANION_USE
	fimc_is_spi_s_pin(spi, true);
#endif

	/* Off to reset FROM operation. Without this routine, spi read does not work. */
	if (!specific->running_rear_camera)
		fimc_is_sec_rom_power_off(core, SENSOR_POSITION_REAR);

#ifdef CAMERA_REAR2
	if (!specific->running_rear2_camera)
		fimc_is_sec_rom_power_off(core, SENSOR_POSITION_REAR2);
#endif

#ifdef CAMERA_REAR3
	if (!specific->running_rear3_camera)
		fimc_is_sec_rom_power_off(core, SENSOR_POSITION_REAR3);
#endif

	if (fimc_is_sec_ldo_enabled(dev, "VDDIO_1.8V_CAM") <= 0)
		fimc_is_sec_rom_power_on(core, SENSOR_POSITION_REAR);

#ifdef CONFIG_COMPANION_USE
	fimc_is_spi_s_pin(spi, false);
#endif
	ret = fimc_is_sec_change_from_header(core);
	if (ret) {
		err("fimc_is_sec_change_from_header failed.");
		ret = -EIO;
		goto isp_fw_err;
	}
#ifdef CONFIG_COMPANION_USE
	fimc_is_spi_s_pin(spi, true);
#endif

	if (!specific->running_rear_camera)
		fimc_is_sec_rom_power_off(core, SENSOR_POSITION_REAR);

#ifdef CAMERA_REAR2
	if (!specific->running_rear2_camera)
		fimc_is_sec_rom_power_off(core, SENSOR_POSITION_REAR2);
#endif

#ifdef CAMERA_REAR3
#if defined(CAMERA_SHARED_IO_POWER_UW)
	if (id == SENSOR_POSITION_REAR3) {
		if (!specific->running_rear3_camera && !specific->running_front_camera) {
			fimc_is_sec_rom_power_off(core, SENSOR_POSITION_REAR3);
		} else {
			info("front is running so don't power off \n");
		}
	} else
#else /* defined(CAMERA_SHARED_IO_POWER_UW) */
	if (id == SENSOR_POSITION_REAR3) {
		if (!specific->running_rear3_camera)
			fimc_is_sec_rom_power_off(core, SENSOR_POSITION_REAR3);
	} else
#endif /* defined(CAMERA_SHARED_IO_POWER_UW) */
#endif

isp_fw_err:
	if (isp_fw_fp)
		filp_close(isp_fw_fp, current->files);

setfile_err:
	if (setfile_fp)
		filp_close(setfile_fp, current->files);

comp_fw_err:
	if (comp_fw_fp)
		filp_close(comp_fw_fp, current->files);

key_err:
	if (key_fp)
		filp_close(key_fp, current->files);
	set_fs(old_fs);

	return ret;
}
#endif

#if !defined(CONFIG_COMPANION_USE)
int fimc_is_sec_readcal(struct fimc_is_core *core)
{
	int ret = 0;
	int retry = FIMC_IS_CAL_RETRY_CNT;
	struct file *key_fp = NULL;
	struct file *dump_fp = NULL;
	mm_segment_t old_fs;
	loff_t pos = 0;
	u16 id = 0;

	struct fimc_is_vender_specific *specific = core->vender.private_data;

	/* reset spi */
	if (!core->spi0.device) {
		err("spi0 device is not available");
		goto exit;
	}

	ret = fimc_is_spi_reset(&core->spi0);
	if (ret) {
		err("failed to fimc_is_spi_read (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}

	ret = fimc_is_spi_read_module_id(&core->spi0, &id, FROM_HEADER_MODULE_ID_START_ADDR, FROM_HEADER_MODULE_ID_SIZE);
	if (ret) {
		printk(KERN_ERR "fimc_is_spi_read_module_id (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}
	info("Camera: FROM Module ID = 0x%04x\n", id);

	ret = fimc_is_spi_read(&core->spi0, sysfs_finfo.cal_map_ver,
			       FROM_HEADER_CAL_MAP_VER_START_ADDR, FIMC_IS_CAL_MAP_VER_SIZE);
	if (ret) {
		printk(KERN_ERR "failed to fimc_is_spi_read (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}
	info("Camera: FROM Cal map_version = %c%c%c%c\n", sysfs_finfo.cal_map_ver[0],
			sysfs_finfo.cal_map_ver[1], sysfs_finfo.cal_map_ver[2], sysfs_finfo.cal_map_ver[3]);

crc_retry:
	/* read cal data */
	info("Camera: SPI read cal data\n");
	ret = fimc_is_spi_read(&core->spi0, cal_buf, 0x0, FIMC_IS_MAX_CAL_SIZE);
	if (ret) {
		err("failed to fimc_is_spi_read (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}

	sysfs_finfo.bin_start_addr = *((u32 *)&cal_buf[FROM_HEADER_ISP_BINARY_START_ADDR]);
	sysfs_finfo.bin_end_addr = *((u32 *)&cal_buf[FROM_HEADER_ISP_BINARY_END_ADDR]);
	info("Binary start = 0x%08x, end = 0x%08x\n",
		(sysfs_finfo.bin_start_addr), (sysfs_finfo.bin_end_addr));
	sysfs_finfo.shading_start_addr = *((u32 *)&cal_buf[FROM_HEADER_SHADING_START_ADDR]);
	sysfs_finfo.shading_end_addr = *((u32 *)&cal_buf[FROM_HEADER_SHADING_END_ADDR]);
	info("Shading start = 0x%08x, end = 0x%08x\n",
		(sysfs_finfo.shading_start_addr), (sysfs_finfo.shading_end_addr));
	sysfs_finfo.setfile_start_addr = *((u32 *)&cal_buf[FROM_HEADER_ISP_SETFILE_START_ADDR]);
	sysfs_finfo.setfile_end_addr = *((u32 *)&cal_buf[FROM_HEADER_ISP_SETFILE_END_ADDR]);
	info("Setfile start = 0x%08x, end = 0x%08x\n",
		(sysfs_finfo.setfile_start_addr), (sysfs_finfo.setfile_end_addr));

	if (sysfs_finfo.setfile_end_addr < FROM_ISP_BINARY_SETFILE_START_ADDR
	|| sysfs_finfo.setfile_end_addr > FROM_ISP_BINARY_SETFILE_END_ADDR) {
		info("setfile end_addr has error!!  0x%08x\n", sysfs_finfo.setfile_end_addr);
		sysfs_finfo.setfile_end_addr = 0x1fffff;
	}
	info("Setfile start = 0x%08x, end = 0x%08x\n",
		(sysfs_finfo.setfile_start_addr), (sysfs_finfo.setfile_end_addr));

	memcpy(sysfs_finfo.header_ver, &cal_buf[FROM_HEADER_VERSION_START_ADDR], FIMC_IS_HEADER_VER_SIZE);
	sysfs_finfo.header_ver[FIMC_IS_HEADER_VER_SIZE] = '\0';
	memcpy(sysfs_finfo.cal_map_ver, &cal_buf[FROM_HEADER_CAL_MAP_VER_START_ADDR], FIMC_IS_CAL_MAP_VER_SIZE);
	memcpy(sysfs_finfo.setfile_ver, &cal_buf[FROM_HEADER_ISP_SETFILE_VER_START_ADDR], FIMC_IS_ISP_SETFILE_VER_SIZE);
	sysfs_finfo.setfile_ver[FIMC_IS_ISP_SETFILE_VER_SIZE] = '\0';
	memcpy(sysfs_finfo.project_name, &cal_buf[FROM_HEADER_PROJECT_NAME_START_ADDR], FIMC_IS_PROJECT_NAME_SIZE);
	sysfs_finfo.project_name[FIMC_IS_PROJECT_NAME_SIZE] = '\0';
	sysfs_finfo.header_section_crc_addr = FROM_CHECKSUM_HEADER_ADDR;

	sysfs_finfo.oem_start_addr = *((u32 *)&cal_buf[FROM_HEADER_OEM_START_ADDR]);
	sysfs_finfo.oem_end_addr = *((u32 *)&cal_buf[FROM_HEADER_OEM_END_ADDR]);
	info("OEM start = 0x%08x, end = 0x%08x\n",
		(sysfs_finfo.oem_start_addr), (sysfs_finfo.oem_end_addr));
	memcpy(sysfs_finfo.oem_ver, &cal_buf[FROM_OEM_VER_START_ADDR], FIMC_IS_OEM_VER_SIZE);
	sysfs_finfo.oem_ver[FIMC_IS_OEM_VER_SIZE] = '\0';
	sysfs_finfo.oem_section_crc_addr = FROM_CHECKSUM_OEM_ADDR;

	sysfs_finfo.awb_start_addr = *((u32 *)&cal_buf[FROM_HEADER_AWB_START_ADDR]);
	sysfs_finfo.awb_end_addr = *((u32 *)&cal_buf[FROM_HEADER_AWB_END_ADDR]);
	info("AWB start = 0x%08x, end = 0x%08x\n",
		(sysfs_finfo.awb_start_addr), (sysfs_finfo.awb_end_addr));
	memcpy(sysfs_finfo.awb_ver, &cal_buf[FROM_AWB_VER_START_ADDR], FIMC_IS_AWB_VER_SIZE);
	sysfs_finfo.awb_ver[FIMC_IS_AWB_VER_SIZE] = '\0';
	sysfs_finfo.awb_section_crc_addr = FROM_CHECKSUM_AWB_ADDR;

	memcpy(sysfs_finfo.shading_ver, &cal_buf[FROM_SHADING_VER_START_ADDR], FIMC_IS_SHADING_VER_SIZE);
	sysfs_finfo.shading_ver[FIMC_IS_SHADING_VER_SIZE] = '\0';
	sysfs_finfo.shading_section_crc_addr = FROM_CHECKSUM_SHADING_ADDR;

	fw_core_version = sysfs_finfo.header_ver[0];
	sysfs_finfo.fw_size = sysfs_finfo.bin_end_addr - sysfs_finfo.bin_start_addr + 1;
	sysfs_finfo.setfile_size = sysfs_finfo.setfile_end_addr - sysfs_finfo.setfile_start_addr + 1;
	sysfs_finfo.comp_fw_size = sysfs_finfo.concord_bin_end_addr - sysfs_finfo.concord_bin_start_addr + 1;
	info("fw_size = %ld\n", sysfs_finfo.fw_size);
	info("setfile_size = %ld\n", sysfs_finfo.setfile_size);
	info("comp_fw_size = %ld\n", sysfs_finfo.comp_fw_size);

	/* debug info dump */
	info("++++ FROM data info\n");
	info("1. Header info\n");
	info("Module info : %s\n", sysfs_finfo.header_ver);
	info(" ID : %c\n", sysfs_finfo.header_ver[FW_CORE_VER]);
	info(" Pixel num : %c%c\n", sysfs_finfo.header_ver[FW_PIXEL_SIZE],
							sysfs_finfo.header_ver[FW_PIXEL_SIZE + 1]);
	info(" ISP ID : %c\n", sysfs_finfo.header_ver[FW_ISP_COMPANY]);
	info(" Sensor Maker : %c\n", sysfs_finfo.header_ver[FW_SENSOR_MAKER]);
	info(" Year : %c\n", sysfs_finfo.header_ver[FW_PUB_YEAR]);
	info(" Month : %c\n", sysfs_finfo.header_ver[FW_PUB_MON]);
	info(" Release num : %c%c\n", sysfs_finfo.header_ver[FW_PUB_NUM],
							sysfs_finfo.header_ver[FW_PUB_NUM + 1]);
	info(" Manufacturer ID : %c\n", sysfs_finfo.header_ver[FW_MODULE_COMPANY]);
	info(" Module ver : %c\n", sysfs_finfo.header_ver[FW_VERSION_INFO]);
	info("Cal data map ver : %s\n", sysfs_finfo.cal_map_ver);
	info("Setfile ver : %s\n", sysfs_finfo.setfile_ver);
	info("Project name : %s\n", sysfs_finfo.project_name);
	info("2. OEM info\n");
	info("Module info : %s\n", sysfs_finfo.oem_ver);
	info("3. AWB info\n");
	info("Module info : %s\n", sysfs_finfo.awb_ver);
	info("4. Shading info\n");
	info("Module info : %s\n", sysfs_finfo.shading_ver);
	info("---- FROM data info\n");

	/* CRC check */
	if (!fimc_is_sec_check_cal_crc32(cal_buf, SENSOR_POSITION_REAR) && (retry > 0)) {
		retry--;
		goto crc_retry;
	}

	if (sysfs_finfo.header_ver[3] == 'L') {
		crc32_check_factory = crc32_check;
	} else {
		crc32_check_factory = false;
	}

	if (!specific->use_module_check) {
		is_latest_cam_module = true;
	} else {
		if (sysfs_finfo.header_ver[10] >= CAMERA_MODULE_ES_VERSION_REAR) {
			is_latest_cam_module = true;
		} else {
			is_latest_cam_module = false;
		}
	}

	if (specific->use_module_check) {
		if (sysfs_finfo.header_ver[10] == FIMC_IS_LATEST_ROM_VERSION_M
#if defined(CAMERA_MODULE_CORE_CS_VERSION)
		    && sysfs_finfo.header_ver[0] == CAMERA_MODULE_CORE_CS_VERSION
#endif
		) {
			is_final_cam_module = true;
		} else {
			is_final_cam_module = false;
		}
	} else {
		is_final_cam_module = true;
	}

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	key_fp = filp_open("/data/media/0/1q2w3e4r.key", O_RDONLY, 0);
	if (IS_ERR(key_fp)) {
		info("KEY does not exist.\n");
		key_fp = NULL;
		goto key_err;
	} else {
		dump_fp = filp_open("/data/media/0/dump", O_RDONLY, 0);
		if (IS_ERR(dump_fp)) {
			info("dump folder does not exist.\n");
			dump_fp = NULL;
			goto key_err;
		} else {
			info("dump folder exist, Dump FROM cal data.\n");
			if (write_data_to_file("/data/media/0/dump/from_cal.bin", cal_buf, FIMC_IS_DUMP_CAL_SIZE, &pos) < 0) {
				info("Failedl to dump cal data.\n");
				goto dump_err;
			}
		}
	}

dump_err:
	if (dump_fp)
		filp_close(dump_fp, current->files);
key_err:
	if (key_fp)
		filp_close(key_fp, current->files);
	set_fs(old_fs);
exit:
	return ret;
}
#endif

#if defined(CONFIG_COMPANION_C1_USE)
int fimc_is_sec_readcal(struct fimc_is_core *core)
{
	int ret = 0;
	int retry = FIMC_IS_CAL_RETRY_CNT;
	struct file *key_fp = NULL;
	struct file *dump_fp = NULL;
	mm_segment_t old_fs;
	loff_t pos = 0;
	u16 id;

	struct fimc_is_vender_specific *specific = core->vender.private_data;

	/* reset spi */
	if (!core->spi0.device) {
		err("spi0 device is not available");
		goto exit;
	}

	ret = fimc_is_spi_reset(&core->spi0);
	if (ret) {
		err("failed to fimc_is_spi_read (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}

	ret = fimc_is_spi_read_module_id(&core->spi0, &id, FROM_HEADER_MODULE_ID_START_ADDR, FROM_HEADER_MODULE_ID_SIZE);
	if (ret) {
		printk(KERN_ERR "fimc_is_spi_read_module_id (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}
	info("Camera: FROM Module ID = 0x%04x\n", id);

	ret = fimc_is_spi_read(&core->spi0, sysfs_finfo.cal_map_ver,
			       FROM_HEADER_CAL_MAP_VER_START_ADDR, FIMC_IS_CAL_MAP_VER_SIZE);
	if (ret) {
		printk(KERN_ERR "failed to fimc_is_spi_read (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}
	info("Camera: FROM Cal map_version = %c%c%c%c\n", sysfs_finfo.cal_map_ver[0],
			sysfs_finfo.cal_map_ver[1], sysfs_finfo.cal_map_ver[2], sysfs_finfo.cal_map_ver[3]);

crc_retry:
	/* read cal data */
	info("Camera: SPI read cal data\n");
	ret = fimc_is_spi_read(&core->spi0, cal_buf, 0x0, FIMC_IS_MAX_CAL_SIZE);
	if (ret) {
		err("failed to fimc_is_spi_read (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}

	sysfs_finfo.bin_start_addr = *((u32 *)&cal_buf[FROM_HEADER_ISP_BINARY_START_ADDR]);
	sysfs_finfo.bin_end_addr = *((u32 *)&cal_buf[FROM_HEADER_ISP_BINARY_END_ADDR]);
	info("Binary start = 0x%08x, end = 0x%08x\n",
		(sysfs_finfo.bin_start_addr), (sysfs_finfo.bin_end_addr));
	sysfs_finfo.shading_start_addr = *((u32 *)&cal_buf[FROM_HEADER_SHADING_START_ADDR]);
	sysfs_finfo.shading_end_addr = *((u32 *)&cal_buf[FROM_HEADER_SHADING_END_ADDR]);
	info("Shading start = 0x%08x, end = 0x%08x\n",
		(sysfs_finfo.shading_start_addr), (sysfs_finfo.shading_end_addr));
	sysfs_finfo.setfile_start_addr = *((u32 *)&cal_buf[FROM_HEADER_ISP_SETFILE_START_ADDR]);
	sysfs_finfo.setfile_end_addr = *((u32 *)&cal_buf[FROM_HEADER_ISP_SETFILE_END_ADDR]);
	info("Setfile start = 0x%08x, end = 0x%08x\n",
		(sysfs_finfo.setfile_start_addr), (sysfs_finfo.setfile_end_addr));

	if (sysfs_finfo.setfile_end_addr < FROM_ISP_BINARY_SETFILE_START_ADDR
	|| sysfs_finfo.setfile_end_addr > FROM_ISP_BINARY_SETFILE_END_ADDR) {
		info("setfile end_addr has error!!  0x%08x\n", sysfs_finfo.setfile_end_addr);
		sysfs_finfo.setfile_end_addr = 0x1fffff;
	}
	info("Setfile start = 0x%08x, end = 0x%08x\n",
		(sysfs_finfo.setfile_start_addr), (sysfs_finfo.setfile_end_addr));

	memcpy(sysfs_finfo.header_ver, &cal_buf[FROM_HEADER_VERSION_START_ADDR], FIMC_IS_HEADER_VER_SIZE);
	sysfs_finfo.header_ver[FIMC_IS_HEADER_VER_SIZE] = '\0';
	memcpy(sysfs_finfo.cal_map_ver, &cal_buf[FROM_HEADER_CAL_MAP_VER_START_ADDR], FIMC_IS_CAL_MAP_VER_SIZE);
	memcpy(sysfs_finfo.setfile_ver, &cal_buf[FROM_HEADER_ISP_SETFILE_VER_START_ADDR], FIMC_IS_ISP_SETFILE_VER_SIZE);
	sysfs_finfo.setfile_ver[FIMC_IS_ISP_SETFILE_VER_SIZE] = '\0';
	memcpy(sysfs_finfo.project_name, &cal_buf[FROM_HEADER_PROJECT_NAME_START_ADDR], FIMC_IS_PROJECT_NAME_SIZE);
	sysfs_finfo.project_name[FIMC_IS_PROJECT_NAME_SIZE] = '\0';
	sysfs_finfo.header_section_crc_addr = FROM_CHECKSUM_HEADER_ADDR;

	sysfs_finfo.oem_start_addr = *((u32 *)&cal_buf[FROM_HEADER_OEM_START_ADDR]);
	sysfs_finfo.oem_end_addr = *((u32 *)&cal_buf[FROM_HEADER_OEM_END_ADDR]);
	info("OEM start = 0x%08x, end = 0x%08x\n",
		(sysfs_finfo.oem_start_addr), (sysfs_finfo.oem_end_addr));
	memcpy(sysfs_finfo.oem_ver, &cal_buf[FROM_OEM_VER_START_ADDR], FIMC_IS_OEM_VER_SIZE);
	sysfs_finfo.oem_ver[FIMC_IS_OEM_VER_SIZE] = '\0';
	sysfs_finfo.oem_section_crc_addr = FROM_CHECKSUM_OEM_ADDR;

	sysfs_finfo.awb_start_addr = *((u32 *)&cal_buf[FROM_HEADER_AWB_START_ADDR]);
	sysfs_finfo.awb_end_addr = *((u32 *)&cal_buf[FROM_HEADER_AWB_END_ADDR]);
	info("AWB start = 0x%08x, end = 0x%08x\n",
		(sysfs_finfo.awb_start_addr), (sysfs_finfo.awb_end_addr));
	memcpy(sysfs_finfo.awb_ver, &cal_buf[FROM_AWB_VER_START_ADDR], FIMC_IS_AWB_VER_SIZE);
	sysfs_finfo.awb_ver[FIMC_IS_AWB_VER_SIZE] = '\0';
	sysfs_finfo.awb_section_crc_addr = FROM_CHECKSUM_AWB_ADDR;

	memcpy(sysfs_finfo.shading_ver, &cal_buf[FROM_SHADING_VER_START_ADDR], FIMC_IS_SHADING_VER_SIZE);
	sysfs_finfo.shading_ver[FIMC_IS_SHADING_VER_SIZE] = '\0';
	sysfs_finfo.shading_section_crc_addr = FROM_CHECKSUM_SHADING_ADDR;

	sysfs_finfo.concord_bin_start_addr = *((u32 *)&cal_buf[FROM_HEADER_CONCORD_BINARY_START_ADDR]);
	sysfs_finfo.concord_bin_end_addr = *((u32 *)&cal_buf[FROM_HEADER_CONCORD_BINARY_END_ADDR]);
	info("concord bin start = 0x%08x, end = 0x%08x\n",
		sysfs_finfo.concord_bin_start_addr, sysfs_finfo.concord_bin_end_addr);
	sysfs_finfo.pdaf_cal_start_addr = *((u32 *)&cal_buf[FROM_HEADER_PDAF_CAL_START_ADDR]);
	sysfs_finfo.pdaf_cal_end_addr = *((u32 *)&cal_buf[FROM_HEADER_PDAF_CAL_END_ADDR]);
	info("pdaf start = 0x%08x, end = 0x%08x\n", sysfs_finfo.pdaf_cal_start_addr, sysfs_finfo.pdaf_cal_end_addr);
	sysfs_finfo.lsc_i0_gain_addr = FROM_SHADING_LSC_I0_GAIN_ADDR;
	info("Shading lsc_i0 start = 0x%08x\n", sysfs_finfo.lsc_i0_gain_addr);
	sysfs_finfo.lsc_j0_gain_addr = FROM_SHADING_LSC_J0_GAIN_ADDR;
	info("Shading lsc_j0 start = 0x%08x\n", sysfs_finfo.lsc_j0_gain_addr);
	sysfs_finfo.lsc_a_gain_addr = FROM_SHADING_LSC_A_GAIN_ADDR;
	info("Shading lsc_a start = 0x%08x\n", sysfs_finfo.lsc_a_gain_addr);
	sysfs_finfo.lsc_k4_gain_addr = FROM_SHADING_LSC_K4_GAIN_ADDR;
	info("Shading lsc_k4 start = 0x%08x\n", sysfs_finfo.lsc_k4_gain_addr);
	sysfs_finfo.lsc_scale_gain_addr = FROM_SHADING_LSC_SCALE_GAIN_ADDR;
	info("Shading lsc_scale start = 0x%08x\n", sysfs_finfo.lsc_scale_gain_addr);
	sysfs_finfo.lsc_gain_start_addr = FROM_SHADING_LSC_GAIN_START_ADDR;
	sysfs_finfo.lsc_gain_end_addr = FROM_SHADING_LSC_GAIN_END_ADDR;
	info("LSC start = 0x%04x, end = 0x%04x\n",
		sysfs_finfo.lsc_gain_start_addr, sysfs_finfo.lsc_gain_end_addr);

	memcpy(sysfs_finfo.concord_header_ver,
		&cal_buf[FROM_HEADER_CONCORD_HEADER_VER_START_ADDR], FIMC_IS_HEADER_VER_SIZE);
	sysfs_finfo.concord_header_ver[FIMC_IS_HEADER_VER_SIZE] = '\0';
	sysfs_finfo.lsc_gain_crc_addr = FROM_SHADING_LSC_GAIN_CRC_ADDR;
	sysfs_finfo.pdaf_crc_addr = FROM_CONCORD_PDAF_CRC_ADDR;

	sysfs_finfo.coef1_start = FROM_CONCORD_XTALK_10_START_ADDR;
	sysfs_finfo.coef1_end = FROM_CONCORD_XTALK_10_END_ADDR;
	info("coefficient1_cal_addr start = 0x%04x, end = 0x%04x\n",
		sysfs_finfo.coef1_start, sysfs_finfo.coef1_end);
	sysfs_finfo.coef2_start = FROM_CONCORD_XTALK_20_START_ADDR;
	sysfs_finfo.coef2_end = FROM_CONCORD_XTALK_20_END_ADDR;
	info("coefficient2_cal_addr start = 0x%04x, end = 0x%04x\n",
		sysfs_finfo.coef2_start, sysfs_finfo.coef2_end);
	sysfs_finfo.coef3_start = FROM_CONCORD_XTALK_30_START_ADDR;
	sysfs_finfo.coef3_end = FROM_CONCORD_XTALK_30_END_ADDR;
	info("coefficient3_cal_addr start = 0x%04x, end = 0x%04x\n",
		sysfs_finfo.coef3_start, sysfs_finfo.coef3_end);
	sysfs_finfo.coef4_start = FROM_CONCORD_XTALK_40_START_ADDR;
	sysfs_finfo.coef4_end = FROM_CONCORD_XTALK_40_END_ADDR;
	info("coefficient4_cal_addr start = 0x%04x, end = 0x%04x\n",
		sysfs_finfo.coef4_start, sysfs_finfo.coef4_end);
	sysfs_finfo.coef5_start = FROM_CONCORD_XTALK_50_START_ADDR;
	sysfs_finfo.coef5_end = FROM_CONCORD_XTALK_50_END_ADDR;
	info("coefficient5_cal_addr start = 0x%04x, end = 0x%04x\n",
		sysfs_finfo.coef5_start, sysfs_finfo.coef5_end);
	sysfs_finfo.coef6_start = FROM_CONCORD_XTALK_60_START_ADDR;
	sysfs_finfo.coef6_end = FROM_CONCORD_XTALK_60_END_ADDR;
	info("coefficient6_cal_addr start = 0x%04x, end = 0x%04x\n",
		sysfs_finfo.coef6_start, sysfs_finfo.coef6_end);
	sysfs_finfo.wcoefficient1_addr = FROM_CONCORD_WCOEF_ADDR;
	info("Shading wcoefficient1 start = 0x%04x\n", sysfs_finfo.wcoefficient1_addr);

	sysfs_finfo.af_inf_addr = FROM_OEM_AF_INF_ADDR;
	sysfs_finfo.af_macro_addr = FROM_OEM_AF_INF_ADDR;
	sysfs_finfo.coef1_crc_addr= FROM_CONCORD_XTALK_10_CHECKSUM_ADDR;
	sysfs_finfo.coef2_crc_addr = FROM_CONCORD_XTALK_20_CHECKSUM_ADDR;
	sysfs_finfo.coef3_crc_addr = FROM_CONCORD_XTALK_30_CHECKSUM_ADDR;
	sysfs_finfo.coef4_crc_addr = FROM_CONCORD_XTALK_40_CHECKSUM_ADDR;
	sysfs_finfo.coef5_crc_addr = FROM_CONCORD_XTALK_50_CHECKSUM_ADDR;
	sysfs_finfo.coef6_crc_addr = FROM_CONCORD_XTALK_60_CHECKSUM_ADDR;

	sysfs_finfo.concord_cal_start_addr = *((u32 *)&cal_buf[FROM_HEADER_CONCORD_CAL_START_ADDR]);
	sysfs_finfo.concord_cal_end_addr = *((u32 *)&cal_buf[FROM_HEADER_CONCORD_CAL_END_ADDR]);
	info("concord cal start = 0x%08x, end = 0x%08x\n",
		sysfs_finfo.concord_cal_start_addr, sysfs_finfo.concord_cal_end_addr);
	sysfs_finfo.concord_master_setfile_start_addr = *((u32 *)&cal_buf[FROM_HEADER_CONCORD_MASTER_SETFILE_START_ADDR]);
	sysfs_finfo.concord_master_setfile_end_addr = *((u32 *)&cal_buf[FROM_HEADER_CONCORD_MASTER_SETFILE_START_ADDR]);
	sysfs_finfo.concord_mode_setfile_start_addr = *((u32 *)&cal_buf[FROM_HEADER_CONCORD_MODE_SETFILE_START_ADDR]);
	sysfs_finfo.concord_mode_setfile_end_addr = *((u32 *)&cal_buf[FROM_HEADER_CONCORD_MODE_SETFILE_END_ADDR]);
	sysfs_finfo.pdaf_start_addr = FROM_CONCORD_CAL_PDAF_START_ADDR;
	sysfs_finfo.pdaf_end_addr = FROM_CONCORD_CAL_PDAF_END_ADDR;
	info("pdaf start = 0x%04x, end = 0x%04x\n", sysfs_finfo.pdaf_start_addr, sysfs_finfo.pdaf_end_addr);

	sysfs_finfo.paf_cal_section_crc_addr = FROM_CHECKSUM_PAF_CAL_ADDR;
	sysfs_finfo.concord_cal_section_crc_addr = FROM_CHECKSUM_CONCORD_CAL_ADDR;

	fw_core_version = sysfs_finfo.header_ver[0];
	sysfs_finfo.fw_size = sysfs_finfo.bin_end_addr - sysfs_finfo.bin_start_addr + 1;
	sysfs_finfo.setfile_size = sysfs_finfo.setfile_end_addr - sysfs_finfo.setfile_start_addr + 1;
	sysfs_finfo.comp_fw_size = sysfs_finfo.concord_bin_end_addr - sysfs_finfo.concord_bin_start_addr + 1;
	info("fw_size = %ld\n", sysfs_finfo.fw_size);
	info("setfile_size = %ld\n", sysfs_finfo.setfile_size);
	info("comp_fw_size = %ld\n", sysfs_finfo.comp_fw_size);

	/* debug info dump */
	info("++++ FROM data info\n");
	info("1. Header info\n");
	info("Module info : %s\n", sysfs_finfo.header_ver);
	info("Companion version info : %s\n", sysfs_finfo.concord_header_ver);
	info(" ID : %c\n", sysfs_finfo.header_ver[FW_CORE_VER]);
	info(" Pixel num : %c%c\n", sysfs_finfo.header_ver[FW_PIXEL_SIZE],
							sysfs_finfo.header_ver[FW_PIXEL_SIZE + 1]);
	info(" ISP ID : %c\n", sysfs_finfo.header_ver[FW_ISP_COMPANY]);
	info(" Sensor Maker : %c\n", sysfs_finfo.header_ver[FW_SENSOR_MAKER]);
	info(" Year : %c\n", sysfs_finfo.header_ver[FW_PUB_YEAR]);
	info(" Month : %c\n", sysfs_finfo.header_ver[FW_PUB_MON]);
	info(" Release num : %c%c\n", sysfs_finfo.header_ver[FW_PUB_NUM],
							sysfs_finfo.header_ver[FW_PUB_NUM + 1]);
	info(" Manufacturer ID : %c\n", sysfs_finfo.header_ver[FW_MODULE_COMPANY]);
	info(" Module ver : %c\n", sysfs_finfo.header_ver[FW_VERSION_INFO]);
	info("Cal data map ver : %s\n", sysfs_finfo.cal_map_ver);
	info("Setfile ver : %s\n", sysfs_finfo.setfile_ver);
	info("Project name : %s\n", sysfs_finfo.project_name);
	info("2. OEM info\n");
	info("Module info : %s\n", sysfs_finfo.oem_ver);
	info("3. AWB info\n");
	info("Module info : %s\n", sysfs_finfo.awb_ver);
	info("4. Shading info\n");
	info("Module info : %s\n", sysfs_finfo.shading_ver);
	info("---- FROM data info\n");

	/* CRC check */
	if (fimc_is_sec_check_from_ver(core, SENSOR_POSITION_REAR)) {
		if (!fimc_is_sec_check_cal_crc32(cal_buf, SENSOR_POSITION_REAR) && (retry > 0)) {
			retry--;
			goto crc_retry;
		}
	} else {
		fw_version_crc_check = false;
		crc32_check = false;
		crc32_c1_check = false;
	}

	if (sysfs_finfo.header_ver[3] == 'L') {
		crc32_check_factory = crc32_check;
		crc32_c1_check_factory = crc32_c1_check;
	} else {
		crc32_check_factory = false;
		crc32_c1_check_factory = false;
	}

	if (fimc_is_sec_check_from_ver(core, SENSOR_POSITION_REAR)) {
		if (crc32_check && crc32_c1_check) {
			/* If FROM LSC value is not valid, loading default lsc data */
			if (*((u32 *)&cal_buf[sysfs_finfo.lsc_gain_start_addr]) == 0x00000000) {
				companion_lsc_isvalid = false;
			} else {
				companion_lsc_isvalid = true;
			}

			if (*((u32 *)&cal_buf[sysfs_finfo.coef1_start]) == 0x00000000) {
				companion_coef_isvalid = false;
			} else {
				companion_coef_isvalid = true;
			}
		} else {
			companion_lsc_isvalid = false;
			companion_coef_isvalid = false;
		}
	} else {
		companion_lsc_isvalid = true;
		companion_coef_isvalid = true;
	}

	if (!specific->use_module_check) {
		is_latest_cam_module = true;
	} else {
		if (sysfs_finfo.header_ver[10] >= CAMERA_MODULE_ES_VERSION_REAR) {
			is_latest_cam_module = true;
		} else {
			is_latest_cam_module = false;
		}
	}

	if (specific->use_module_check) {
		if (sysfs_finfo.header_ver[10] == FIMC_IS_LATEST_ROM_VERSION_M
#if defined(CAMERA_MODULE_CORE_CS_VERSION)
		    && sysfs_finfo.header_ver[0] == CAMERA_MODULE_CORE_CS_VERSION
#endif
		) {
			is_final_cam_module = true;
		} else {
			is_final_cam_module = false;
		}
	} else {
		is_final_cam_module = true;
	}

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	key_fp = filp_open("/data/media/0/1q2w3e4r.key", O_RDONLY, 0);
	if (IS_ERR(key_fp)) {
		info("KEY does not exist.\n");
		key_fp = NULL;
		goto key_err;
	} else {
		dump_fp = filp_open("/data/media/0/dump", O_RDONLY, 0);
		if (IS_ERR(dump_fp)) {
			info("dump folder does not exist.\n");
			dump_fp = NULL;
			goto key_err;
		} else {
			info("dump folder exist, Dump FROM cal data.\n");
			if (write_data_to_file("/data/media/0/dump/from_cal.bin", cal_buf, FIMC_IS_DUMP_CAL_SIZE, &pos) < 0) {
				info("Failedl to dump cal data.\n");
				goto dump_err;
			}
		}
	}

dump_err:
	if (dump_fp)
		filp_close(dump_fp, current->files);
key_err:
	if (key_fp)
		filp_close(key_fp, current->files);
	set_fs(old_fs);
exit:
	return ret;
}
#endif

#if defined(CONFIG_COMPANION_C2_USE)
int fimc_is_sec_readcal(struct fimc_is_core *core)
{
	int ret = 0;
	int retry = FIMC_IS_CAL_RETRY_CNT;
	struct file *key_fp = NULL;
	struct file *dump_fp = NULL;
	mm_segment_t old_fs;
	loff_t pos = 0;
	u16 id;

	struct fimc_is_vender_specific *specific = core->vender.private_data;

	/* reset spi */
	if (!core->spi0.device) {
		err("spi0 device is not available");
		goto exit;
	}

	ret = fimc_is_spi_reset(&core->spi0);
	if (ret) {
		err("failed to fimc_is_spi_read (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}

	ret = fimc_is_spi_read_module_id(&core->spi0, &id, FROM_HEADER_MODULE_ID_START_ADDR, FROM_HEADER_MODULE_ID_SIZE);
	if (ret) {
		printk(KERN_ERR "fimc_is_spi_read_module_id (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}
	info("Camera: FROM Module ID = 0x%04x\n", id);

	ret = fimc_is_spi_read(&core->spi0, sysfs_finfo.cal_map_ver,
			       FROM_HEADER_CAL_MAP_VER_START_ADDR, FIMC_IS_CAL_MAP_VER_SIZE);
	if (ret) {
		printk(KERN_ERR "failed to fimc_is_spi_read (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}
	info("Camera: FROM Cal map_version = %c%c%c%c\n", sysfs_finfo.cal_map_ver[0],
			sysfs_finfo.cal_map_ver[1], sysfs_finfo.cal_map_ver[2], sysfs_finfo.cal_map_ver[3]);

crc_retry:
	/* read cal data */
	info("Camera: SPI read cal data\n");
	ret = fimc_is_spi_read(&core->spi0, cal_buf, 0x0, FIMC_IS_MAX_CAL_SIZE);
	if (ret) {
		err("failed to fimc_is_spi_read (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}

	sysfs_finfo.bin_start_addr = *((u32 *)&cal_buf[FROM_HEADER_ISP_BINARY_START_ADDR]);
	sysfs_finfo.bin_end_addr = *((u32 *)&cal_buf[FROM_HEADER_ISP_BINARY_END_ADDR]);
	info("Binary start = 0x%08x, end = 0x%08x\n",
		(sysfs_finfo.bin_start_addr), (sysfs_finfo.bin_end_addr));
	sysfs_finfo.shading_start_addr = *((u32 *)&cal_buf[FROM_HEADER_SHADING_START_ADDR]);
	sysfs_finfo.shading_end_addr = *((u32 *)&cal_buf[FROM_HEADER_SHADING_END_ADDR]);
	info("Shading start = 0x%08x, end = 0x%08x\n",
		(sysfs_finfo.shading_start_addr), (sysfs_finfo.shading_end_addr));
	sysfs_finfo.setfile_start_addr = *((u32 *)&cal_buf[FROM_HEADER_ISP_SETFILE_START_ADDR]);
	sysfs_finfo.setfile_end_addr = *((u32 *)&cal_buf[FROM_HEADER_ISP_SETFILE_END_ADDR]);
	info("Setfile start = 0x%08x, end = 0x%08x\n",
		(sysfs_finfo.setfile_start_addr), (sysfs_finfo.setfile_end_addr));

	if (sysfs_finfo.setfile_end_addr < FROM_ISP_BINARY_SETFILE_START_ADDR
	|| sysfs_finfo.setfile_end_addr > FROM_ISP_BINARY_SETFILE_END_ADDR) {
		info("setfile end_addr has error!!  0x%08x\n", sysfs_finfo.setfile_end_addr);
		sysfs_finfo.setfile_end_addr = 0x1fffff;
	}
	info("Setfile start = 0x%08x, end = 0x%08x\n",
		(sysfs_finfo.setfile_start_addr), (sysfs_finfo.setfile_end_addr));

	memcpy(sysfs_finfo.header_ver, &cal_buf[FROM_HEADER_VERSION_START_ADDR], FIMC_IS_HEADER_VER_SIZE);
	sysfs_finfo.header_ver[FIMC_IS_HEADER_VER_SIZE] = '\0';
	memcpy(sysfs_finfo.cal_map_ver, &cal_buf[FROM_HEADER_CAL_MAP_VER_START_ADDR], FIMC_IS_CAL_MAP_VER_SIZE);
	memcpy(sysfs_finfo.setfile_ver, &cal_buf[FROM_HEADER_ISP_SETFILE_VER_START_ADDR], FIMC_IS_ISP_SETFILE_VER_SIZE);
	sysfs_finfo.setfile_ver[FIMC_IS_ISP_SETFILE_VER_SIZE] = '\0';
	memcpy(sysfs_finfo.project_name, &cal_buf[FROM_HEADER_PROJECT_NAME_START_ADDR], FIMC_IS_PROJECT_NAME_SIZE);
	sysfs_finfo.project_name[FIMC_IS_PROJECT_NAME_SIZE] = '\0';
	sysfs_finfo.header_section_crc_addr = FROM_CHECKSUM_HEADER_ADDR;

	sysfs_finfo.oem_start_addr = *((u32 *)&cal_buf[FROM_HEADER_OEM_START_ADDR]);
	sysfs_finfo.oem_end_addr = *((u32 *)&cal_buf[FROM_HEADER_OEM_END_ADDR]);
	info("OEM start = 0x%08x, end = 0x%08x\n",
		(sysfs_finfo.oem_start_addr), (sysfs_finfo.oem_end_addr));
	memcpy(sysfs_finfo.oem_ver, &cal_buf[FROM_OEM_VER_START_ADDR], FIMC_IS_OEM_VER_SIZE);
	sysfs_finfo.oem_ver[FIMC_IS_OEM_VER_SIZE] = '\0';
	sysfs_finfo.oem_section_crc_addr = FROM_CHECKSUM_OEM_ADDR;

	sysfs_finfo.awb_start_addr = *((u32 *)&cal_buf[FROM_HEADER_AWB_START_ADDR]);
	sysfs_finfo.awb_end_addr = *((u32 *)&cal_buf[FROM_HEADER_AWB_END_ADDR]);
	info("AWB start = 0x%08x, end = 0x%08x\n",
		(sysfs_finfo.awb_start_addr), (sysfs_finfo.awb_end_addr));
	memcpy(sysfs_finfo.awb_ver, &cal_buf[FROM_AWB_VER_START_ADDR], FIMC_IS_AWB_VER_SIZE);
	sysfs_finfo.awb_ver[FIMC_IS_AWB_VER_SIZE] = '\0';
	sysfs_finfo.awb_section_crc_addr = FROM_CHECKSUM_AWB_ADDR;

	memcpy(sysfs_finfo.shading_ver, &cal_buf[FROM_SHADING_VER_START_ADDR], FIMC_IS_SHADING_VER_SIZE);
	sysfs_finfo.shading_ver[FIMC_IS_SHADING_VER_SIZE] = '\0';
	sysfs_finfo.shading_section_crc_addr = FROM_CHECKSUM_SHADING_ADDR;

	sysfs_finfo.concord_bin_start_addr = *((u32 *)&cal_buf[FROM_HEADER_CONCORD_BINARY_START_ADDR]);
	sysfs_finfo.concord_bin_end_addr = *((u32 *)&cal_buf[FROM_HEADER_CONCORD_BINARY_END_ADDR]);
	info("concord bin start = 0x%08x, end = 0x%08x\n",
		sysfs_finfo.concord_bin_start_addr, sysfs_finfo.concord_bin_end_addr);
	sysfs_finfo.pdaf_cal_start_addr = *((u32 *)&cal_buf[FROM_HEADER_PDAF_CAL_START_ADDR]);
	sysfs_finfo.pdaf_cal_end_addr = *((u32 *)&cal_buf[FROM_HEADER_PDAF_CAL_END_ADDR]);
	info("pdaf start = 0x%08x, end = 0x%08x\n", sysfs_finfo.pdaf_cal_start_addr, sysfs_finfo.pdaf_cal_end_addr);
	sysfs_finfo.lsc_i0_gain_addr = FROM_SHADING_LSC_I0_GAIN_ADDR;
	info("Shading lsc_i0 start = 0x%08x\n", sysfs_finfo.lsc_i0_gain_addr);
	sysfs_finfo.lsc_j0_gain_addr = FROM_SHADING_LSC_J0_GAIN_ADDR;
	info("Shading lsc_j0 start = 0x%08x\n", sysfs_finfo.lsc_j0_gain_addr);
	sysfs_finfo.lsc_a_gain_addr = FROM_SHADING_LSC_A_GAIN_ADDR;
	info("Shading lsc_a start = 0x%08x\n", sysfs_finfo.lsc_a_gain_addr);
	sysfs_finfo.lsc_k4_gain_addr = FROM_SHADING_LSC_K4_GAIN_ADDR;
	info("Shading lsc_k4 start = 0x%08x\n", sysfs_finfo.lsc_k4_gain_addr);
	sysfs_finfo.lsc_scale_gain_addr = FROM_SHADING_LSC_SCALE_GAIN_ADDR;
	info("Shading lsc_scale start = 0x%08x\n", sysfs_finfo.lsc_scale_gain_addr);
	sysfs_finfo.lsc_gain_start_addr = FROM_SHADING_LSC_GAIN_START_ADDR;
	sysfs_finfo.lsc_gain_end_addr = FROM_SHADING_LSC_GAIN_END_ADDR;
	info("LSC start = 0x%04x, end = 0x%04x\n",
		sysfs_finfo.lsc_gain_start_addr, sysfs_finfo.lsc_gain_end_addr);

	memcpy(sysfs_finfo.concord_header_ver,
		&cal_buf[FROM_HEADER_CONCORD_HEADER_VER_START_ADDR], FIMC_IS_HEADER_VER_SIZE);
	sysfs_finfo.concord_header_ver[FIMC_IS_HEADER_VER_SIZE] = '\0';
	sysfs_finfo.lsc_gain_crc_addr = FROM_SHADING_LSC_GAIN_CRC_ADDR;
	sysfs_finfo.pdaf_crc_addr = FROM_CONCORD_PDAF_CRC_ADDR;

	sysfs_finfo.xtalk_coef_start = FROM_CONCORD_XTALK_COEF_ADDR;
	sysfs_finfo.coef_offset_R = FROM_CONCORD_COEF_OFFSET_R_ADDR;
	sysfs_finfo.coef_offset_G = FROM_CONCORD_COEF_OFFSET_G_ADDR;
	sysfs_finfo.coef_offset_B = FROM_CONCORD_COEF_OFFSET_B_ADDR;
	sysfs_finfo.xtalk_coef_crc_addr = FROM_CONCORD_XTALK_COEF_CRC_ADDR;

	sysfs_finfo.concord_cal_start_addr = *((u32 *)&cal_buf[FROM_HEADER_CONCORD_CAL_START_ADDR]);
	sysfs_finfo.concord_cal_end_addr = *((u32 *)&cal_buf[FROM_HEADER_CONCORD_CAL_END_ADDR]);
	info("concord cal start = 0x%08x, end = 0x%08x\n",
		sysfs_finfo.concord_cal_start_addr, sysfs_finfo.concord_cal_end_addr);
	sysfs_finfo.concord_master_setfile_start_addr = *((u32 *)&cal_buf[FROM_HEADER_CONCORD_MASTER_SETFILE_START_ADDR]);
	sysfs_finfo.concord_master_setfile_end_addr = *((u32 *)&cal_buf[FROM_HEADER_CONCORD_MASTER_SETFILE_START_ADDR]);
	sysfs_finfo.concord_mode_setfile_start_addr = *((u32 *)&cal_buf[FROM_HEADER_CONCORD_MODE_SETFILE_START_ADDR]);
	sysfs_finfo.concord_mode_setfile_end_addr = *((u32 *)&cal_buf[FROM_HEADER_CONCORD_MODE_SETFILE_END_ADDR]);
	sysfs_finfo.pdaf_start_addr = FROM_CONCORD_CAL_PDAF_START_ADDR;
	sysfs_finfo.pdaf_end_addr = FROM_CONCORD_CAL_PDAF_END_ADDR;
	info("pdaf start = 0x%04x, end = 0x%04x\n", sysfs_finfo.pdaf_start_addr, sysfs_finfo.pdaf_end_addr);

	sysfs_finfo.paf_cal_section_crc_addr = FROM_CHECKSUM_PAF_CAL_ADDR;
	sysfs_finfo.concord_cal_section_crc_addr = FROM_CHECKSUM_CONCORD_CAL_ADDR;

	sysfs_finfo.grasTuning_AwbAshCord_N_addr = FROM_SHADING_GRASTUNING_AWB_ASH_CORD_ADDR;
	info("Shading grasTuning_AwbAshCord_N start = 0x%08x\n", sysfs_finfo.grasTuning_AwbAshCord_N_addr);
	sysfs_finfo.grasTuning_awbAshCordIndexes_N_addr = FROM_SHADING_GRASTUNING_AWB_ASH_CORD_INDEX_ADDR;
	info("Shading grasTuning_awbAshCordIndexes_N start = 0x%08x\n", sysfs_finfo.grasTuning_awbAshCordIndexes_N_addr);
	sysfs_finfo.grasTuning_GASAlpha_M__N_addr = FROM_SHADING_GRASTUNING_GAS_ALPHA_ADDR;
	info("Shading lsc_scale start = 0x%08x\n", sysfs_finfo.grasTuning_GASAlpha_M__N_addr);
	sysfs_finfo.grasTuning_GASBeta_M__N_addr = FROM_SHADING_GRASTUNING_GAS_BETA_ADDR;
	info("Shading grasTuning_GASBeta_M__N start = 0x%08x\n", sysfs_finfo.grasTuning_GASBeta_M__N_addr);
	sysfs_finfo.grasTuning_GASOutdoorAlpha_N_addr = FROM_SHADING_GRASTUNING_GAS_OUTDOOR_ALPHA_ADDR;
	info("Shading grasTuning_GASOutdoorAlpha_N start = 0x%08x\n", sysfs_finfo.grasTuning_GASOutdoorAlpha_N_addr);
	sysfs_finfo.grasTuning_GASOutdoorBeta_N_addr = FROM_SHADING_GRASTUNING_GAS_OUTDOOR_BETA_ADDR;
	info("Shading grasTuning_GASOutdoorBeta_N start = 0x%08x\n", sysfs_finfo.grasTuning_GASOutdoorBeta_N_addr);
	sysfs_finfo.grasTuning_GASIndoorAlpha_N_addr = FROM_SHADING_GRASTUNING_GAS_INDOOR_ALPHA_ADDR;
	info("Shading grasTuning_GASIndoorAlpha_N start = 0x%08x\n", sysfs_finfo.grasTuning_GASIndoorAlpha_N_addr);
	sysfs_finfo.grasTuning_GASIndoorBeta_N_addr = FROM_SHADING_GRASTUNING_GAS_INDOOR_BETA_ADDR;
	info("Shading grasTuning_GASIndoorBeta_N start = 0x%08x\n", sysfs_finfo.grasTuning_GASIndoorBeta_N_addr);

	sysfs_finfo.pdaf_shad_start_addr = FROM_CONCORD_CAL_PDAF_SHADING_START_ADDR;
	sysfs_finfo.pdaf_shad_end_addr = FROM_CONCORD_CAL_PDAF_SHADING_END_ADDR;
	info("pdaf_shad start = 0x%04x, end = 0x%04x\n", sysfs_finfo.pdaf_shad_start_addr,
		sysfs_finfo.pdaf_shad_end_addr);
	sysfs_finfo.pdaf_shad_crc_addr = FROM_CONCORD_PDAF_SHAD_CRC_ADDR;
	sysfs_finfo.lsc_parameter_crc_addr = FROM_SHADING_LSC_PARAMETER_CRC_ARRD;
	info("xtalk_coef_start = 0x%04x\n", sysfs_finfo.xtalk_coef_start);
	info("lsc_gain_crc_addr = 0x%04x\n", sysfs_finfo.lsc_gain_crc_addr);
	info("pdaf_crc_addr = 0x%04x\n", sysfs_finfo.pdaf_crc_addr);
	info("pdaf_shad_crc_addr = 0x%04x\n", sysfs_finfo.pdaf_shad_crc_addr);
	info("xtalk_coef_crc_addr= 0x%04x\n", sysfs_finfo.xtalk_coef_crc_addr);
	info("lsc_parameter_crc_addr = 0x%04x\n", sysfs_finfo.lsc_parameter_crc_addr);

	fw_core_version = sysfs_finfo.header_ver[0];
	sysfs_finfo.fw_size = sysfs_finfo.bin_end_addr - sysfs_finfo.bin_start_addr + 1;
	sysfs_finfo.setfile_size = sysfs_finfo.setfile_end_addr - sysfs_finfo.setfile_start_addr + 1;
	sysfs_finfo.comp_fw_size = sysfs_finfo.concord_bin_end_addr - sysfs_finfo.concord_bin_start_addr + 1;
	info("fw_size = %ld\n", sysfs_finfo.fw_size);
	info("setfile_size = %ld\n", sysfs_finfo.setfile_size);
	info("comp_fw_size = %ld\n", sysfs_finfo.comp_fw_size);

	sysfs_finfo.af_cal_pan = *((u32 *)&cal_buf[FROM_AF_CAL_PAN_ADDR]);
	sysfs_finfo.af_cal_macro = *((u32 *)&cal_buf[FROM_AF_CAL_MACRO_ADDR]);

	/* debug info dump */
	info("++++ FROM data info\n");
	info("1. Header info\n");
	info("Module info : %s\n", sysfs_finfo.header_ver);
	info("Companion version info : %s\n", sysfs_finfo.concord_header_ver);
	info(" ID : %c\n", sysfs_finfo.header_ver[FW_CORE_VER]);
	info(" Pixel num : %c%c\n", sysfs_finfo.header_ver[FW_PIXEL_SIZE],
							sysfs_finfo.header_ver[FW_PIXEL_SIZE + 1]);
	info(" ISP ID : %c\n", sysfs_finfo.header_ver[FW_ISP_COMPANY]);
	info(" Sensor Maker : %c\n", sysfs_finfo.header_ver[FW_SENSOR_MAKER]);
	info(" Year : %c\n", sysfs_finfo.header_ver[FW_PUB_YEAR]);
	info(" Month : %c\n", sysfs_finfo.header_ver[FW_PUB_MON]);
	info(" Release num : %c%c\n", sysfs_finfo.header_ver[FW_PUB_NUM],
							sysfs_finfo.header_ver[FW_PUB_NUM + 1]);
	info(" Manufacturer ID : %c\n", sysfs_finfo.header_ver[FW_MODULE_COMPANY]);
	info(" Module ver : %c\n", sysfs_finfo.header_ver[FW_VERSION_INFO]);
	info("Cal data map ver : %s\n", sysfs_finfo.cal_map_ver);
	info("Setfile ver : %s\n", sysfs_finfo.setfile_ver);
	info("Project name : %s\n", sysfs_finfo.project_name);
	info("2. OEM info\n");
	info("Module info : %s\n", sysfs_finfo.oem_ver);
	info("3. AWB info\n");
	info("Module info : %s\n", sysfs_finfo.awb_ver);
	info("4. Shading info\n");
	info("Module info : %s\n", sysfs_finfo.shading_ver);
	info("---- FROM data info\n");

	/* CRC check */
	if (fimc_is_sec_check_from_ver(core, SENSOR_POSITION_REAR)) {
		if (!fimc_is_sec_check_cal_crc32(cal_buf, SENSOR_POSITION_REAR) && (retry > 0)) {
			retry--;
			goto crc_retry;
		}
	} else {
		fw_version_crc_check = false;
		crc32_check = false;
		crc32_c1_check = false;
	}

	if (sysfs_finfo.header_ver[3] == 'L') {
		crc32_check_factory = crc32_check;
		crc32_c1_check_factory = crc32_c1_check;
	} else {
		crc32_check_factory = false;
		crc32_c1_check_factory = false;
	}

	if (fimc_is_sec_check_from_ver(core, SENSOR_POSITION_REAR)) {
		if (crc32_check && crc32_c1_check) {
			/* If FROM LSC value is not valid, loading default lsc data */
			if (*((u32 *)&cal_buf[sysfs_finfo.lsc_gain_start_addr]) == 0x00000000) {
				companion_lsc_isvalid = false;
			} else {
				companion_lsc_isvalid = true;
			}

			if (*((u32 *)&cal_buf[sysfs_finfo.xtalk_coef_start]) == 0x00000000) {
				companion_coef_isvalid = false;
			} else {
				companion_coef_isvalid = true;
			}
		} else {
			companion_lsc_isvalid = false;
			companion_coef_isvalid = false;
		}
	} else {
		companion_lsc_isvalid = true;
		companion_coef_isvalid = true;
	}

	if (!specific->use_module_check) {
		is_latest_cam_module = true;
	} else {
		if (sysfs_finfo.header_ver[10] >= CAMERA_MODULE_ES_VERSION_REAR) {
			is_latest_cam_module = true;
		} else {
			is_latest_cam_module = false;
		}
	}

	if (specific->use_module_check) {
		if (sysfs_finfo.header_ver[10] == FIMC_IS_LATEST_ROM_VERSION_M
#if defined(CAMERA_MODULE_CORE_CS_VERSION)
		    && sysfs_finfo.header_ver[0] == CAMERA_MODULE_CORE_CS_VERSION
#endif
		) {
			is_final_cam_module = true;
		} else {
			is_final_cam_module = false;
		}
	} else {
		is_final_cam_module = true;
	}

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	key_fp = filp_open("/data/media/0/1q2w3e4r.key", O_RDONLY, 0);
	if (IS_ERR(key_fp)) {
		info("KEY does not exist.\n");
		key_fp = NULL;
		goto key_err;
	} else {
		dump_fp = filp_open("/data/media/0/dump", O_RDONLY, 0);
		if (IS_ERR(dump_fp)) {
			info("dump folder does not exist.\n");
			dump_fp = NULL;
			goto key_err;
		} else {
			info("dump folder exist, Dump FROM cal data.\n");
			if (write_data_to_file("/data/media/0/dump/from_cal.bin", cal_buf, FIMC_IS_DUMP_CAL_SIZE, &pos) < 0) {
				info("Failedl to dump cal data.\n");
				goto dump_err;
			}
		}
	}

dump_err:
	if (dump_fp)
		filp_close(dump_fp, current->files);
key_err:
	if (key_fp)
		filp_close(key_fp, current->files);
	set_fs(old_fs);
exit:
	return ret;
}
#endif

#if defined(CONFIG_COMPANION_C3_USE)
int fimc_is_sec_readcal(struct fimc_is_core *core)
{
	int ret = 0;
	int retry = FIMC_IS_CAL_RETRY_CNT;
	struct file *key_fp = NULL;
	struct file *dump_fp = NULL;
	mm_segment_t old_fs;
	loff_t pos = 0;
	u16 id;

	struct fimc_is_vender_specific *specific = core->vender.private_data;

	/* reset spi */
	if (!core->spi0.device) {
		err("spi0 device is not available");
		goto exit;
	}

	ret = fimc_is_spi_reset(&core->spi0);
	if (ret) {
		err("failed to fimc_is_spi_read (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}

	ret = fimc_is_spi_read_module_id(&core->spi0, &id, FROM_HEADER_MODULE_ID_START_ADDR, FROM_HEADER_MODULE_ID_SIZE);
	if (ret) {
		printk(KERN_ERR "fimc_is_spi_read_module_id (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}
	info("Camera: FROM Module ID = 0x%04x\n", id);

	ret = fimc_is_spi_read(&core->spi0, sysfs_finfo.cal_map_ver,
			       FROM_HEADER_CAL_MAP_VER_START_ADDR, FIMC_IS_CAL_MAP_VER_SIZE);
	if (ret) {
		printk(KERN_ERR "failed to fimc_is_spi_read (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}
	info("Camera: FROM Cal map_version = %c%c%c%c\n", sysfs_finfo.cal_map_ver[0],
			sysfs_finfo.cal_map_ver[1], sysfs_finfo.cal_map_ver[2], sysfs_finfo.cal_map_ver[3]);

crc_retry:
	/* read cal data */
	info("Camera: SPI read cal data\n");
	ret = fimc_is_spi_read(&core->spi0, cal_buf, 0x0, FIMC_IS_MAX_CAL_SIZE);
	if (ret) {
		err("failed to fimc_is_spi_read (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}

	sysfs_finfo.bin_start_addr = *((u32 *)&cal_buf[FROM_HEADER_ISP_BINARY_START_ADDR]);
	sysfs_finfo.bin_end_addr = *((u32 *)&cal_buf[FROM_HEADER_ISP_BINARY_END_ADDR]);
	info("Binary start = 0x%08x, end = 0x%08x\n",
		(sysfs_finfo.bin_start_addr), (sysfs_finfo.bin_end_addr));
	sysfs_finfo.shading_start_addr = *((u32 *)&cal_buf[FROM_HEADER_SHADING_START_ADDR]);
	sysfs_finfo.shading_end_addr = *((u32 *)&cal_buf[FROM_HEADER_SHADING_END_ADDR]);
	info("Shading start = 0x%08x, end = 0x%08x\n",
		(sysfs_finfo.shading_start_addr), (sysfs_finfo.shading_end_addr));
	sysfs_finfo.setfile_start_addr = *((u32 *)&cal_buf[FROM_HEADER_ISP_SETFILE_START_ADDR]);
	sysfs_finfo.setfile_end_addr = *((u32 *)&cal_buf[FROM_HEADER_ISP_SETFILE_END_ADDR]);
	info("Setfile start = 0x%08x, end = 0x%08x\n",
		(sysfs_finfo.setfile_start_addr), (sysfs_finfo.setfile_end_addr));

	if (sysfs_finfo.setfile_end_addr < FROM_ISP_BINARY_SETFILE_START_ADDR
	|| sysfs_finfo.setfile_end_addr > FROM_ISP_BINARY_SETFILE_END_ADDR) {
		info("setfile end_addr has error!!  0x%08x\n", sysfs_finfo.setfile_end_addr);
		sysfs_finfo.setfile_end_addr = 0x1fffff;
	}
	info("Setfile start = 0x%08x, end = 0x%08x\n",
		(sysfs_finfo.setfile_start_addr), (sysfs_finfo.setfile_end_addr));

	memcpy(sysfs_finfo.header_ver, &cal_buf[FROM_HEADER_VERSION_START_ADDR], FIMC_IS_HEADER_VER_SIZE);
	sysfs_finfo.header_ver[FIMC_IS_HEADER_VER_SIZE] = '\0';
	memcpy(sysfs_finfo.cal_map_ver, &cal_buf[FROM_HEADER_CAL_MAP_VER_START_ADDR], FIMC_IS_CAL_MAP_VER_SIZE);
	memcpy(sysfs_finfo.setfile_ver, &cal_buf[FROM_HEADER_ISP_SETFILE_VER_START_ADDR], FIMC_IS_ISP_SETFILE_VER_SIZE);
	sysfs_finfo.setfile_ver[FIMC_IS_ISP_SETFILE_VER_SIZE] = '\0';
	memcpy(sysfs_finfo.project_name, &cal_buf[FROM_HEADER_PROJECT_NAME_START_ADDR], FIMC_IS_PROJECT_NAME_SIZE);
	sysfs_finfo.project_name[FIMC_IS_PROJECT_NAME_SIZE] = '\0';
	sysfs_finfo.header_section_crc_addr = FROM_CHECKSUM_HEADER_ADDR;

	sysfs_finfo.concord_bin_start_addr = *((u32 *)&cal_buf[FROM_HEADER_CONCORD_BINARY_START_ADDR]);
	sysfs_finfo.concord_bin_end_addr = *((u32 *)&cal_buf[FROM_HEADER_CONCORD_BINARY_END_ADDR]);
	info("concord bin start = 0x%08x, end = 0x%08x\n",
		sysfs_finfo.concord_bin_start_addr, sysfs_finfo.concord_bin_end_addr);
	sysfs_finfo.pdaf_cal_start_addr = *((u32 *)&cal_buf[FROM_HEADER_PDAF_CAL_START_ADDR]);
	sysfs_finfo.pdaf_cal_end_addr = *((u32 *)&cal_buf[FROM_HEADER_PDAF_CAL_END_ADDR]);
	info("pdaf start = 0x%08x, end = 0x%08x\n", sysfs_finfo.pdaf_cal_start_addr, sysfs_finfo.pdaf_cal_end_addr);
	sysfs_finfo.lsc_i0_gain_addr = FROM_SHADING_LSC_I0_GAIN_ADDR;
	info("Shading lsc_i0 start = 0x%08x\n", sysfs_finfo.lsc_i0_gain_addr);
	sysfs_finfo.lsc_j0_gain_addr = FROM_SHADING_LSC_J0_GAIN_ADDR;
	info("Shading lsc_j0 start = 0x%08x\n", sysfs_finfo.lsc_j0_gain_addr);
	sysfs_finfo.lsc_a_gain_addr = FROM_SHADING_LSC_A_GAIN_ADDR;
	info("Shading lsc_a start = 0x%08x\n", sysfs_finfo.lsc_a_gain_addr);
	sysfs_finfo.lsc_k4_gain_addr = FROM_SHADING_LSC_K4_GAIN_ADDR;
	info("Shading lsc_k4 start = 0x%08x\n", sysfs_finfo.lsc_k4_gain_addr);
	sysfs_finfo.lsc_scale_gain_addr = FROM_SHADING_LSC_SCALE_GAIN_ADDR;
	info("Shading lsc_scale start = 0x%08x\n", sysfs_finfo.lsc_scale_gain_addr);
	sysfs_finfo.lsc_gain_start_addr = FROM_SHADING_LSC_GAIN_START_ADDR;
	sysfs_finfo.lsc_gain_end_addr = FROM_SHADING_LSC_GAIN_END_ADDR;
	info("LSC start = 0x%04x, end = 0x%04x\n",
		sysfs_finfo.lsc_gain_start_addr, sysfs_finfo.lsc_gain_end_addr);

	memcpy(sysfs_finfo.concord_header_ver,
		&cal_buf[FROM_HEADER_CONCORD_HEADER_VER_START_ADDR], FIMC_IS_HEADER_VER_SIZE);
	sysfs_finfo.concord_header_ver[FIMC_IS_HEADER_VER_SIZE] = '\0';
	sysfs_finfo.lsc_gain_crc_addr = FROM_SHADING_LSC_GAIN_CRC_ADDR;
	sysfs_finfo.pdaf_crc_addr = FROM_CONCORD_PDAF_CRC_ADDR;

	sysfs_finfo.cal_data_section_crc_addr = FROM_CHECKSUM_CAL_DATA_ADDR;
	sysfs_finfo.cal_data_start_addr = FROM_CAL_DATA_START_ADDR;
	sysfs_finfo.cal_data_end_addr = FROM_CAL_DATA_END_ADDR;

	sysfs_finfo.grasTuning_AwbAshCord_N_addr = FROM_SHADING_GRASTUNING_AWB_ASH_CORD_ADDR;
	info("Shading grasTuning_AwbAshCord_N start = 0x%08x\n", sysfs_finfo.grasTuning_AwbAshCord_N_addr);
	sysfs_finfo.grasTuning_awbAshCordIndexes_N_addr = FROM_SHADING_GRASTUNING_AWB_ASH_CORD_INDEX_ADDR;
	info("Shading grasTuning_awbAshCordIndexes_N start = 0x%08x\n", sysfs_finfo.grasTuning_awbAshCordIndexes_N_addr);
	sysfs_finfo.grasTuning_GASAlpha_M__N_addr = FROM_SHADING_GRASTUNING_GAS_ALPHA_ADDR;
	info("Shading lsc_scale start = 0x%08x\n", sysfs_finfo.grasTuning_GASAlpha_M__N_addr);
	sysfs_finfo.grasTuning_GASBeta_M__N_addr = FROM_SHADING_GRASTUNING_GAS_BETA_ADDR;
	info("Shading grasTuning_GASBeta_M__N start = 0x%08x\n", sysfs_finfo.grasTuning_GASBeta_M__N_addr);
	sysfs_finfo.grasTuning_GASOutdoorAlpha_N_addr = FROM_SHADING_GRASTUNING_GAS_OUTDOOR_ALPHA_ADDR;
	info("Shading grasTuning_GASOutdoorAlpha_N start = 0x%08x\n", sysfs_finfo.grasTuning_GASOutdoorAlpha_N_addr);
	sysfs_finfo.grasTuning_GASOutdoorBeta_N_addr = FROM_SHADING_GRASTUNING_GAS_OUTDOOR_BETA_ADDR;
	info("Shading grasTuning_GASOutdoorBeta_N start = 0x%08x\n", sysfs_finfo.grasTuning_GASOutdoorBeta_N_addr);
	sysfs_finfo.grasTuning_GASIndoorAlpha_N_addr = FROM_SHADING_GRASTUNING_GAS_INDOOR_ALPHA_ADDR;
	info("Shading grasTuning_GASIndoorAlpha_N start = 0x%08x\n", sysfs_finfo.grasTuning_GASIndoorAlpha_N_addr);
	sysfs_finfo.grasTuning_GASIndoorBeta_N_addr = FROM_SHADING_GRASTUNING_GAS_INDOOR_BETA_ADDR;
	info("Shading grasTuning_GASIndoorBeta_N start = 0x%08x\n", sysfs_finfo.grasTuning_GASIndoorBeta_N_addr);

	sysfs_finfo.pdaf_shad_start_addr = FROM_CONCORD_CAL_PDAF_SHADING_START_ADDR;
	sysfs_finfo.pdaf_shad_end_addr = FROM_CONCORD_CAL_PDAF_SHADING_END_ADDR;
	info("pdaf_shad start = 0x%04x, end = 0x%04x\n", sysfs_finfo.pdaf_shad_start_addr,
		sysfs_finfo.pdaf_shad_end_addr);
	sysfs_finfo.pdaf_shad_crc_addr = FROM_CONCORD_PDAF_SHAD_CRC_ADDR;
	sysfs_finfo.lsc_parameter_crc_addr = FROM_SHADING_LSC_PARAMETER_CRC_ARRD;
	info("lsc_gain_crc_addr = 0x%04x\n", sysfs_finfo.lsc_gain_crc_addr);
	info("pdaf_crc_addr = 0x%04x\n", sysfs_finfo.pdaf_crc_addr);
	info("pdaf_shad_crc_addr = 0x%04x\n", sysfs_finfo.pdaf_shad_crc_addr);
	info("lsc_parameter_crc_addr = 0x%04x\n", sysfs_finfo.lsc_parameter_crc_addr);

	fw_core_version = sysfs_finfo.header_ver[0];
	sysfs_finfo.fw_size = sysfs_finfo.bin_end_addr - sysfs_finfo.bin_start_addr + 1;
	sysfs_finfo.setfile_size = sysfs_finfo.setfile_end_addr - sysfs_finfo.setfile_start_addr + 1;
	sysfs_finfo.comp_fw_size = sysfs_finfo.concord_bin_end_addr - sysfs_finfo.concord_bin_start_addr + 1;
	info("fw_size = %ld\n", sysfs_finfo.fw_size);
	info("setfile_size = %ld\n", sysfs_finfo.setfile_size);
	info("comp_fw_size = %ld\n", sysfs_finfo.comp_fw_size);

	sysfs_finfo.af_cal_pan = *((u32 *)&cal_buf[FROM_AF_CAL_PAN_ADDR]);
	sysfs_finfo.af_cal_macro = *((u32 *)&cal_buf[FROM_AF_CAL_MACRO_ADDR]);

	/* debug info dump */
	info("++++ FROM data info\n");
	info("1. Header info\n");
	info("Module info : %s\n", sysfs_finfo.header_ver);
	info("Companion version info : %s\n", sysfs_finfo.concord_header_ver);
	info(" ID : %c\n", sysfs_finfo.header_ver[FW_CORE_VER]);
	info(" Pixel num : %c%c\n", sysfs_finfo.header_ver[FW_PIXEL_SIZE],
							sysfs_finfo.header_ver[FW_PIXEL_SIZE + 1]);
	info(" ISP ID : %c\n", sysfs_finfo.header_ver[FW_ISP_COMPANY]);
	info(" Sensor Maker : %c\n", sysfs_finfo.header_ver[FW_SENSOR_MAKER]);
	info(" Year : %c\n", sysfs_finfo.header_ver[FW_PUB_YEAR]);
	info(" Month : %c\n", sysfs_finfo.header_ver[FW_PUB_MON]);
	info(" Release num : %c%c\n", sysfs_finfo.header_ver[FW_PUB_NUM],
							sysfs_finfo.header_ver[FW_PUB_NUM + 1]);
	info(" Manufacturer ID : %c\n", sysfs_finfo.header_ver[FW_MODULE_COMPANY]);
	info(" Module ver : %c\n", sysfs_finfo.header_ver[FW_VERSION_INFO]);
	info("Cal data map ver : %s\n", sysfs_finfo.cal_map_ver);
	info("Setfile ver : %s\n", sysfs_finfo.setfile_ver);
	info("Project name : %s\n", sysfs_finfo.project_name);
	info("---- FROM data info\n");

	/* CRC check */
	if (fimc_is_sec_check_from_ver(core, SENSOR_POSITION_REAR)) {
		if (!fimc_is_sec_check_cal_crc32(cal_buf, SENSOR_POSITION_REAR) && (retry > 0)) {
			retry--;
			goto crc_retry;
		}
	} else {
		fw_version_crc_check = false;
		crc32_check = false;
		crc32_c1_check = false;
	}

	if (sysfs_finfo.header_ver[3] == 'L') {
		crc32_check_factory = crc32_check;
		crc32_c1_check_factory = crc32_c1_check;
	} else {
		crc32_check_factory = false;
		crc32_c1_check_factory = false;
	}

	if (fimc_is_sec_check_from_ver(core, SENSOR_POSITION_REAR)) {
		if (crc32_check && crc32_c1_check) {
			/* If FROM LSC value is not valid, loading default lsc data */
			if (*((u32 *)&cal_buf[sysfs_finfo.lsc_gain_start_addr]) == 0x00000000) {
				companion_lsc_isvalid = false;
			} else {
				companion_lsc_isvalid = true;
			}
		} else {
			companion_lsc_isvalid = false;
		}
	} else {
		companion_lsc_isvalid = true;
	}

	if (!specific->use_module_check) {
		is_latest_cam_module = true;
	} else {
		if (sysfs_finfo.header_ver[10] >= CAMERA_MODULE_ES_VERSION_REAR) {
			is_latest_cam_module = true;
		} else {
			is_latest_cam_module = false;
		}
	}

	if (specific->use_module_check) {
		if (sysfs_finfo.header_ver[10] == FIMC_IS_LATEST_ROM_VERSION_M
#if defined(CAMERA_MODULE_CORE_CS_VERSION)
		    && sysfs_finfo.header_ver[0] == CAMERA_MODULE_CORE_CS_VERSION
#endif
		) {
			is_final_cam_module = true;
		} else {
			is_final_cam_module = false;
		}
	} else {
		is_final_cam_module = true;
	}

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	key_fp = filp_open("/data/media/0/1q2w3e4r.key", O_RDONLY, 0);
	if (IS_ERR(key_fp)) {
		info("KEY does not exist.\n");
		key_fp = NULL;
		goto key_err;
	} else {
		dump_fp = filp_open("/data/media/0/dump", O_RDONLY, 0);
		if (IS_ERR(dump_fp)) {
			info("dump folder does not exist.\n");
			dump_fp = NULL;
			goto key_err;
		} else {
			info("dump folder exist, Dump FROM cal data.\n");
			if (write_data_to_file("/data/media/0/dump/from_cal.bin", cal_buf, FIMC_IS_DUMP_CAL_SIZE, &pos) < 0) {
				info("Failedl to dump cal data.\n");
				goto dump_err;
			}
		}
	}

dump_err:
	if (dump_fp)
		filp_close(dump_fp, current->files);
key_err:
	if (key_fp)
		filp_close(key_fp, current->files);
	set_fs(old_fs);
exit:
	return ret;
}
#endif

#ifdef CAMERA_MODULE_DUALIZE
int fimc_is_sec_readfw(struct fimc_is_core *core)
{
	int ret = 0;
	loff_t pos = 0;
	char fw_path[100];
	int retry = FIMC_IS_FW_RETRY_CNT;

	info("Camera: FW need to be dumped\n");

crc_retry:
	/* read fw data */
	info("Camera: Start SPI read fw data\n");
	memset(fw_buf, 0x0, FIMC_IS_MAX_FW_SIZE);
	ret = fimc_is_spi_read(&core->spi0, fw_buf, sysfs_finfo.bin_start_addr, FIMC_IS_MAX_FW_SIZE);
	if (ret) {
		err("failed to fimc_is_spi_read (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}
	info("Camera: End SPI read fw data\n");

	/* CRC check */
	if (!fimc_is_sec_check_fw_crc32(fw_buf) && (retry > 0)) {
		retry--;
		goto crc_retry;
	} else if (!retry) {
		ret = -EINVAL;
		goto exit;
	}

	snprintf(fw_path, sizeof(fw_path), "%s%s", FIMC_IS_FW_DUMP_PATH, sysfs_finfo.load_fw_name);

	if (write_data_to_file(fw_path, fw_buf, sysfs_finfo.fw_size, &pos) < 0) {
		ret = -EIO;
		goto exit;
	}

	info("Camera: FW Data has dumped successfully\n");

exit:
	return ret;
}

int fimc_is_sec_read_setfile(struct fimc_is_core *core)
{
	int ret = 0;
	loff_t pos = 0;
	char setfile_path[100];
	int retry = FIMC_IS_FW_RETRY_CNT;

	info("Camera: Setfile need to be dumped\n");

setfile_crc_retry:
	/* read setfile data */
	info("Camera: Start SPI read setfile data\n");
	memset(fw_buf, 0x0, FIMC_IS_MAX_FW_SIZE);
	if (fimc_is_sec_fw_module_compare(sysfs_finfo.header_ver, FW_2P2_B))
		ret = fimc_is_spi_read(&core->spi0, fw_buf, sysfs_finfo.setfile_start_addr,
			FIMC_IS_MAX_SETFILE_SIZE_2P2_PLUS);
	else
		ret = fimc_is_spi_read(&core->spi0, fw_buf, sysfs_finfo.setfile_start_addr,
			FIMC_IS_MAX_SETFILE_SIZE_IMX240);
	if (ret) {
		err("failed to fimc_is_spi_read (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}
	info("Camera: End SPI read setfile data\n");

	/* CRC check */
	if (!fimc_is_sec_check_setfile_crc32(fw_buf) && (retry > 0)) {
		retry--;
		goto setfile_crc_retry;
	} else if (!retry) {
		ret = -EINVAL;
		goto exit;
	}

	snprintf(setfile_path, sizeof(setfile_path), "%s%s", FIMC_IS_FW_DUMP_PATH, sysfs_finfo.load_setfile_name);
	pos = 0;

	if (write_data_to_file(setfile_path, fw_buf, sysfs_finfo.setfile_size, &pos) < 0) {
		ret = -EIO;
		goto exit;
	}

	info("Camera: Setfile has dumped successfully\n");

exit:
	return ret;
}
#endif
#endif

int fimc_is_sec_check_reload(struct fimc_is_core *core)
{
	struct file *reload_key_fp = NULL;
	struct file *supend_resume_key_fp = NULL;
	mm_segment_t old_fs;
	struct fimc_is_vender_specific *specific = core->vender.private_data;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	reload_key_fp = filp_open("/data/media/0/reload/r1e2l3o4a5d.key", O_RDONLY, 0);
	if (IS_ERR(reload_key_fp)) {
		reload_key_fp = NULL;
	} else {
		info("Reload KEY exist, reload cal data.\n");
		force_caldata_dump = true;
		specific->suspend_resume_disable = true;
	}

	if (reload_key_fp)
		filp_close(reload_key_fp, current->files);

	supend_resume_key_fp = filp_open("/data/media/0/i1s2p3s4r.key", O_RDONLY, 0);
	if (IS_ERR(supend_resume_key_fp)) {
		supend_resume_key_fp = NULL;
	} else {
		info("Supend_resume KEY exist, disable runtime supend/resume. \n");
		specific->suspend_resume_disable = true;
	}

	if (supend_resume_key_fp)
		filp_close(supend_resume_key_fp, current->files);

	set_fs(old_fs);

	return 0;
}

#ifdef CAMERA_MODULE_DUALIZE
#ifdef CONFIG_COMPANION_USE
int fimc_is_sec_read_companion_fw(struct fimc_is_core *core)
{
	int ret = 0;
	loff_t pos = 0;
	char fw_path[100];
	//char master_setfile_path[100];
	//char mode_setfile_path[100];
	int retry = FIMC_IS_FW_RETRY_CNT;

	info("Camera: Companion FW, Setfile need to be dumped\n");

crc_retry:
	/* read companion fw data */
	info("Camera: Start SPI read companion fw data\n");
	memset(fw_buf, 0x0, FIMC_IS_MAX_FW_SIZE);
	ret = fimc_is_spi_read(&core->spi0, fw_buf, sysfs_finfo.concord_bin_start_addr, FIMC_IS_MAX_COMPANION_FW_SIZE);
	if (ret) {
		err("failed to fimc_is_spi_read (%d)\n", ret);
		ret = -EINVAL;
		goto exit;
	}
	info("Camera: End SPI read companion fw data\n");

	/* CRC check */
	if (!fimc_is_sec_check_companion_fw_crc32(fw_buf) && (retry > 0)) {
		retry--;
		goto crc_retry;
	} else if (!retry) {
		ret = -EINVAL;
		goto exit;
	}

	snprintf(fw_path, sizeof(fw_path), "%s%s",
		FIMC_IS_FW_DUMP_PATH, sysfs_finfo.load_c1_fw_name);

	if (write_data_to_file(fw_path, fw_buf, sysfs_finfo.comp_fw_size, &pos) < 0) {
		ret = -EIO;
		goto exit;
	}

	info("Camera: Companion FW Data has dumped successfully\n");
#if 0
	snprintf(master_setfile_path, sizeof(master_setfile_path), "%s%s", FIMC_IS_FW_DUMP_PATH, sysfs_finfo.load_c1_mastersetf_name);
	snprintf(mode_setfile_path, sizeof(mode_setfile_path), "%s%s", FIMC_IS_FW_DUMP_PATH, sysfs_finfo.load_c1_modesetf_name);
	pos = 0;

	if (write_data_to_file(master_setfile_path,
			comp_fw_buf + sysfs_finfo.concord_master_setfile_start_addr,
			sysfs_finfo.concord_master_setfile_end_addr - sysfs_finfo.concord_master_setfile_start_addr + 1, &pos) < 0) {
		ret = -EIO;
		goto exit;
	}
	pos = 0;
	if (write_data_to_file(mode_setfile_path,
			comp_fw_buf + sysfs_finfo.concord_mode_setfile_start_addr,
			sysfs_finfo.concord_mode_setfile_end_addr - sysfs_finfo.concord_mode_setfile_start_addr + 1, &pos) < 0) {
		ret = -EIO;
		goto exit;
	}

	info("Camera: Companion Setfile has dumped successfully\n");
#endif

exit:
	return ret;
}
#endif
#endif

#if 0
int fimc_is_sec_gpio_enable(struct exynos_platform_fimc_is *pdata, char *name, bool on)
{
	struct gpio_set *gpio;
	int ret = 0;
	int i = 0;

	for (i = 0; i < FIMC_IS_MAX_GPIO_NUM; i++) {
			gpio = &pdata->gpio_info->cfg[i];
			if (strcmp(gpio->name, name) == 0)
				break;
			else
				continue;
	}

	if (i == FIMC_IS_MAX_GPIO_NUM) {
		err("GPIO %s is not found!!", name);
		ret = -EINVAL;
		goto exit;
	}

	ret = gpio_request(gpio->pin, gpio->name);
	if (ret) {
		err("Request GPIO error(%s)", gpio->name);
		goto exit;
	}

	if (on) {
		switch (gpio->act) {
		case GPIO_PULL_NONE:
			s3c_gpio_cfgpin(gpio->pin, gpio->value);
			s3c_gpio_setpull(gpio->pin, S3C_GPIO_PULL_NONE);
			break;
		case GPIO_OUTPUT:
			s3c_gpio_cfgpin(gpio->pin, S3C_GPIO_OUTPUT);
			s3c_gpio_setpull(gpio->pin, S3C_GPIO_PULL_NONE);
			gpio_set_value(gpio->pin, gpio->value);
			break;
		case GPIO_INPUT:
			s3c_gpio_cfgpin(gpio->pin, S3C_GPIO_INPUT);
			s3c_gpio_setpull(gpio->pin, S3C_GPIO_PULL_NONE);
			gpio_set_value(gpio->pin, gpio->value);
			break;
		case GPIO_RESET:
			s3c_gpio_setpull(gpio->pin, S3C_GPIO_PULL_NONE);
			gpio_direction_output(gpio->pin, 0);
			gpio_direction_output(gpio->pin, 1);
			break;
		default:
			err("unknown act for gpio");
			break;
		}
	} else {
		s3c_gpio_cfgpin(gpio->pin, S3C_GPIO_INPUT);
		s3c_gpio_setpull(gpio->pin, S3C_GPIO_PULL_DOWN);
	}

	gpio_free(gpio->pin);

exit:
	return ret;
}
#endif

int fimc_is_sec_get_pixel_size(char *header_ver)
{
	int pixelsize = 0;

	pixelsize += (int) (header_ver[FW_PIXEL_SIZE] - 0x30) * 10;
	pixelsize += (int) (header_ver[FW_PIXEL_SIZE + 1] - 0x30);

	return pixelsize;
}

int fimc_is_sec_core_voltage_select(struct device *dev, char *header_ver)
{
	struct regulator *regulator = NULL;
	int ret = 0;
	int minV, maxV;
	int pixelSize = 0;

	regulator = regulator_get(dev, "cam_sensor_core_1.2v");
	if (IS_ERR_OR_NULL(regulator)) {
		err("%s : regulator_get fail",
			__func__);
		return -EINVAL;
	}
	pixelSize = fimc_is_sec_get_pixel_size(header_ver);

	if (header_ver[FW_SENSOR_MAKER] == FW_SENSOR_MAKER_SONY) {
		if (pixelSize == 13) {
			minV = 1050000;
			maxV = 1050000;
		} else if (pixelSize == 8) {
			minV = 1100000;
			maxV = 1100000;
		} else {
			minV = 1050000;
			maxV = 1050000;
		}
	} else if (header_ver[FW_SENSOR_MAKER] == FW_SENSOR_MAKER_SLSI) {
		minV = 1200000;
		maxV = 1200000;
	} else {
		minV = 1050000;
		maxV = 1050000;
	}

	ret = regulator_set_voltage(regulator, minV, maxV);

	if (ret >= 0)
		info("%s : set_core_voltage %d, %d successfully\n",
				__func__, minV, maxV);
	regulator_put(regulator);

	return ret;
}

int fimc_is_sec_fw_find(struct fimc_is_core *core, int position)
{
	int sensor_id = 0;
	int *pSensor_id = NULL;
	struct fimc_is_vender_specific *specific = core->vender.private_data;

	if (position == SENSOR_POSITION_REAR2)
		pSensor_id = &specific->rear_second_sensor_id;
	else if (position == SENSOR_POSITION_REAR3)
		pSensor_id = &specific->rear_third_sensor_id;
	else
		pSensor_id = &specific->rear_sensor_id;

	if (fimc_is_sec_fw_module_compare(sysfs_finfo.header_ver, FW_2P2_F) ||
	    fimc_is_sec_fw_module_compare(sysfs_finfo.header_ver, FW_2P2_I) ||
	    fimc_is_sec_fw_module_compare(sysfs_finfo.header_ver, FW_2P2_A) ||
	    fimc_is_sec_fw_module_compare(sysfs_finfo.header_ver, FW_2P2_B) ||
	    fimc_is_sec_fw_module_compare(sysfs_finfo.header_ver, FW_2P2_C)) {
		snprintf(sysfs_finfo.load_fw_name, sizeof(FIMC_IS_FW_2P2), "%s", FIMC_IS_FW_2P2);
		snprintf(sysfs_finfo.load_setfile_name, sizeof(FIMC_IS_2P2_SETF), "%s", FIMC_IS_2P2_SETF);
		*pSensor_id = SENSOR_NAME_S5K2P2;
	} else if (fimc_is_sec_fw_module_compare(sysfs_finfo.header_ver, FW_2P2_12M)) {
		snprintf(sysfs_finfo.load_fw_name, sizeof(FIMC_IS_FW_2P2_12M), "%s", FIMC_IS_FW_2P2_12M);
		snprintf(sysfs_finfo.load_setfile_name, sizeof(FIMC_IS_2P2_12M_SETF), "%s", FIMC_IS_2P2_12M_SETF);
		*pSensor_id = SENSOR_NAME_S5K2P2_12M;
	} else if (fimc_is_sec_fw_module_compare(sysfs_finfo.header_ver, FW_2P3)) {
		snprintf(sysfs_finfo.load_fw_name, sizeof(FIMC_IS_FW_2P3), "%s", FIMC_IS_FW_2P3);
		snprintf(sysfs_finfo.load_setfile_name, sizeof(FIMC_IS_2P3_SETF), "%s", FIMC_IS_2P3_SETF);
		*pSensor_id = SENSOR_NAME_S5K2P3;
	} else if (fimc_is_sec_fw_module_compare(sysfs_finfo.header_ver, FW_3P8)) {
		snprintf(sysfs_finfo.load_fw_name, sizeof(FIMC_IS_FW_3P8), "%s", FIMC_IS_FW_3P8);
		snprintf(sysfs_finfo.load_setfile_name, sizeof(FIMC_IS_3P8_SETF), "%s", FIMC_IS_3P8_SETF);
		*pSensor_id = SENSOR_NAME_S5K3P8;
	} else if (fimc_is_sec_fw_module_compare(sysfs_finfo.header_ver, FW_3L2_V)) {
		snprintf(sysfs_finfo.load_fw_name, sizeof(FIMC_IS_FW_3L2), "%s", FIMC_IS_FW_3L2);
		snprintf(sysfs_finfo.load_setfile_name, sizeof(FIMC_IS_3L2_SETF), "%s", FIMC_IS_3L2_SETF);
		*pSensor_id = SENSOR_NAME_S5K3L2;
	} else if (fimc_is_sec_fw_module_compare(sysfs_finfo.header_ver, FW_3M3)) {
		snprintf(sysfs_finfo.load_fw_name, sizeof(FIMC_IS_FW_3M3), "%s", FIMC_IS_FW_3M3);
		snprintf(sysfs_finfo.load_setfile_name, sizeof(FIMC_IS_3M3_SETF), "%s", FIMC_IS_3M3_SETF);
		*pSensor_id = SENSOR_NAME_S5K3M3;
	} else if (fimc_is_sec_fw_module_compare(sysfs_finfo.header_ver, FW_4H5)) {
		snprintf(sysfs_finfo.load_fw_name, sizeof(FIMC_IS_FW_4H5), "%s", FIMC_IS_FW_4H5);
		snprintf(sysfs_finfo.load_setfile_name, sizeof(FIMC_IS_4H5_SETF), "%s", FIMC_IS_4H5_SETF);
		*pSensor_id = SENSOR_NAME_S5K4H5;
	} else if (fimc_is_sec_fw_module_compare(sysfs_finfo.header_ver, FW_IMX240_A) ||
		fimc_is_sec_fw_module_compare(sysfs_finfo.header_ver, FW_IMX240_B) ||
		fimc_is_sec_fw_module_compare(sysfs_finfo.header_ver, FW_IMX240_C)) {
		snprintf(sysfs_finfo.load_fw_name, sizeof(FIMC_IS_FW_IMX240), "%s", FIMC_IS_FW_IMX240);
		snprintf(sysfs_finfo.load_setfile_name, sizeof(FIMC_IS_IMX240_SETF), "%s", FIMC_IS_IMX240_SETF);
		*pSensor_id = SENSOR_NAME_IMX240;
	} else if (fimc_is_sec_fw_module_compare(sysfs_finfo.header_ver, FW_IMX219)) {
		snprintf(sysfs_finfo.load_fw_name, sizeof(FIMC_IS_FW_IMX219), "%s", FIMC_IS_FW_IMX219);
		snprintf(sysfs_finfo.load_setfile_name, sizeof(FIMC_IS_IMX219_SETF), "%s", FIMC_IS_IMX219_SETF);
		*pSensor_id = SENSOR_NAME_IMX219;
	} else if (fimc_is_sec_fw_module_compare(sysfs_finfo.header_ver, FW_IMX258)) {
		snprintf(sysfs_finfo.load_fw_name, sizeof(FIMC_IS_FW_IMX258), "%s", FIMC_IS_FW_IMX258);
		snprintf(sysfs_finfo.load_setfile_name, sizeof(FIMC_IS_IMX258_SETF), "%s", FIMC_IS_IMX258_SETF);
		*pSensor_id = SENSOR_NAME_IMX258;
	} else if (fimc_is_sec_fw_module_compare(sysfs_finfo.header_ver, FW_IMX258_F1P7)) {
		snprintf(sysfs_finfo.load_fw_name, sizeof(FIMC_IS_FW_IMX258), "%s", FIMC_IS_FW_IMX258);
		snprintf(sysfs_finfo.load_setfile_name, sizeof(FIMC_IS_IMX258_SETF), "%s", FIMC_IS_IMX258_SETF);
		*pSensor_id = SENSOR_NAME_IMX258;
	}  else if (fimc_is_sec_fw_module_compare(sysfs_finfo.header_ver, FW_IMX260)) {
		snprintf(sysfs_finfo.load_fw_name, sizeof(FIMC_IS_FW_IMX260), "%s", FIMC_IS_FW_IMX260);
		snprintf(sysfs_finfo.load_setfile_name, sizeof(FIMC_IS_IMX260_SETF), "%s", FIMC_IS_IMX260_SETF);
		*pSensor_id = SENSOR_NAME_IMX260;
	} else if (fimc_is_sec_fw_module_compare(sysfs_finfo.header_ver, FW_IMX228)) {
		snprintf(sysfs_finfo.load_fw_name, sizeof(FIMC_IS_FW_IMX228), "%s", FIMC_IS_FW_IMX228);
		snprintf(sysfs_finfo.load_setfile_name, sizeof(FIMC_IS_IMX228_SETF), "%s", FIMC_IS_IMX228_SETF);
		*pSensor_id = SENSOR_NAME_IMX228;
	} else if (fimc_is_sec_fw_module_compare(sysfs_finfo.header_ver, FW_2T2)) {
		snprintf(sysfs_finfo.load_fw_name, sizeof(FIMC_IS_FW_2T2_EVT1), "%s", FIMC_IS_FW_2T2_EVT1);
		snprintf(sysfs_finfo.load_setfile_name, sizeof(FIMC_IS_2T2_SETF), "%s", FIMC_IS_2T2_SETF);
		*pSensor_id = SENSOR_NAME_S5K2T2;
	} else if (fimc_is_sec_fw_module_compare(sysfs_finfo.header_ver, FW_SR544)) {
		snprintf(sysfs_finfo.load_fw_name, sizeof(FIMC_IS_FW_SR544), "%s", FIMC_IS_FW_SR544);
		snprintf(sysfs_finfo.load_setfile_name, sizeof(FIMC_IS_SR544_SETF), "%s", FIMC_IS_SR544_SETF);
		*pSensor_id = SENSOR_NAME_SR544;
	} else if (fimc_is_sec_fw_module_compare(sysfs_finfo.header_ver, FW_2P6)) {
		snprintf(sysfs_finfo.load_fw_name, sizeof(FIMC_IS_FW_2P6), "%s", FIMC_IS_FW_2P6);
		snprintf(sysfs_finfo.load_setfile_name, sizeof(FIMC_IS_2P6_SETF), "%s", FIMC_IS_2P6_SETF);
		*pSensor_id = SENSOR_NAME_S5K2P6;
	} else if (fimc_is_sec_fw_module_compare(sysfs_finfo.header_ver, FW_2P6_FRONT)) {
		snprintf(sysfs_finfo.load_fw_name, sizeof(FIMC_IS_FW_2P6_FRONT), "%s", FIMC_IS_FW_2P6_FRONT);
		snprintf(sysfs_finfo.load_setfile_name, sizeof(FIMC_IS_2P6_FRONT_SETF), "%s", FIMC_IS_2P6_FRONT_SETF);
		*pSensor_id = SENSOR_NAME_S5K2P6;
	} else {
		/* default firmware and setfile */
		sensor_id = *pSensor_id;
		if (sensor_id == SENSOR_NAME_IMX240) {
			/* IMX240 */
			snprintf(sysfs_finfo.load_fw_name, sizeof(FIMC_IS_FW_IMX240), "%s", FIMC_IS_FW_IMX240);
			snprintf(sysfs_finfo.load_setfile_name, sizeof(FIMC_IS_IMX240_SETF), "%s", FIMC_IS_IMX240_SETF);
		} else if (sensor_id == SENSOR_NAME_IMX228) {
			/* IMX228 */
			snprintf(sysfs_finfo.load_fw_name, sizeof(FIMC_IS_FW_IMX228), "%s", FIMC_IS_FW_IMX228);
			snprintf(sysfs_finfo.load_setfile_name, sizeof(FIMC_IS_IMX228_SETF), "%s", FIMC_IS_IMX228_SETF);
		} else if (sensor_id == SENSOR_NAME_S5K3P3) {
			/* 3P3*/
			snprintf(sysfs_finfo.load_fw_name, sizeof(FIMC_IS_FW_3P3), "%s", FIMC_IS_FW_3P3);
			snprintf(sysfs_finfo.load_setfile_name, sizeof(FIMC_IS_3P3_SETF), "%s", FIMC_IS_3P3_SETF);
		} else if (sensor_id == SENSOR_NAME_S5K3P8) {
			/* 3P8*/
			snprintf(sysfs_finfo.load_fw_name, sizeof(FIMC_IS_FW_3P8), "%s", FIMC_IS_FW_3P8);
			snprintf(sysfs_finfo.load_setfile_name, sizeof(FIMC_IS_3P8_SETF), "%s", FIMC_IS_3P8_SETF);
		} else if (sensor_id == SENSOR_NAME_S5K3L2) {
			/* 3L2*/
			snprintf(sysfs_finfo.load_fw_name, sizeof(FIMC_IS_FW_3L2), "%s", FIMC_IS_FW_3L2);
			snprintf(sysfs_finfo.load_setfile_name, sizeof(FIMC_IS_3L2_SETF), "%s", FIMC_IS_3L2_SETF);
		} else if (sensor_id == SENSOR_NAME_IMX134) {
			/* IMX134 */
			snprintf(sysfs_finfo.load_fw_name, sizeof(FIMC_IS_FW_IMX134), "%s", FIMC_IS_FW_IMX134);
			snprintf(sysfs_finfo.load_setfile_name, sizeof(FIMC_IS_IMX134_SETF), "%s", FIMC_IS_IMX134_SETF);
		} else if (sensor_id == SENSOR_NAME_S5K2P2_12M) {
			/* 2P2_12M */
			snprintf(sysfs_finfo.load_fw_name, sizeof(FIMC_IS_FW_2P2_12M), "%s", FIMC_IS_FW_2P2_12M);
			snprintf(sysfs_finfo.load_setfile_name, sizeof(FIMC_IS_2P2_12M_SETF), "%s", FIMC_IS_2P2_12M_SETF);
		} else if (sensor_id == SENSOR_NAME_S5K2P2) {
			/* 2P2 */
			snprintf(sysfs_finfo.load_fw_name, sizeof(FIMC_IS_FW_2P2), "%s", FIMC_IS_FW_2P2);
			snprintf(sysfs_finfo.load_setfile_name, sizeof(FIMC_IS_2P2_SETF), "%s", FIMC_IS_2P2_SETF);
		} else if (sensor_id == SENSOR_NAME_S5K4H5) {
			/* 4H5 */
			snprintf(sysfs_finfo.load_fw_name, sizeof(FIMC_IS_FW_4H5), "%s", FIMC_IS_FW_4H5);
			snprintf(sysfs_finfo.load_setfile_name, sizeof(FIMC_IS_4H5_SETF), "%s", FIMC_IS_4H5_SETF);
		} else if (sensor_id == SENSOR_NAME_S5K4H5YC) {
			/* 4H5YC */
			snprintf(sysfs_finfo.load_fw_name, sizeof(FIMC_IS_FW_4H5YC), "%s", FIMC_IS_FW_4H5YC);
			snprintf(sysfs_finfo.load_setfile_name, sizeof(FIMC_IS_4H5YC_SETF), "%s", FIMC_IS_4H5YC_SETF);
		} else if ( sensor_id == SENSOR_NAME_S5K2P3 ) {
			/* 2P3 */
			snprintf(sysfs_finfo.load_fw_name, sizeof(FIMC_IS_FW_2P3), "%s", FIMC_IS_FW_2P3);
			snprintf(sysfs_finfo.load_setfile_name, sizeof(FIMC_IS_2P3_SETF), "%s", FIMC_IS_2P3_SETF);
		} else if (sensor_id == SENSOR_NAME_S5K2T2) {
			/* 2T2 */
			snprintf(sysfs_finfo.load_fw_name, sizeof(FIMC_IS_FW_2T2_EVT1), "%s", FIMC_IS_FW_2T2_EVT1);
			snprintf(sysfs_finfo.load_setfile_name, sizeof(FIMC_IS_2T2_SETF), "%s", FIMC_IS_2T2_SETF);
		} else if (sensor_id == SENSOR_NAME_IMX219) {
			/* IMX219 */
			snprintf(sysfs_finfo.load_fw_name, sizeof(FIMC_IS_FW_IMX219), "%s", FIMC_IS_FW_IMX219);
			snprintf(sysfs_finfo.load_setfile_name, sizeof(FIMC_IS_IMX219_SETF), "%s", FIMC_IS_IMX219_SETF);
		} else if (sensor_id == SENSOR_NAME_IMX258) {
			/* IMX258 */
			snprintf(sysfs_finfo.load_fw_name, sizeof(FIMC_IS_FW_IMX258), "%s", FIMC_IS_FW_IMX258);
			snprintf(sysfs_finfo.load_setfile_name, sizeof(FIMC_IS_IMX258_SETF), "%s", FIMC_IS_IMX258_SETF);
		} else if (sensor_id == SENSOR_NAME_IMX260) {
			/* IMX260 */
			snprintf(sysfs_finfo.load_fw_name, sizeof(FIMC_IS_FW_IMX260), "%s", FIMC_IS_FW_IMX260);
			snprintf(sysfs_finfo.load_setfile_name, sizeof(FIMC_IS_IMX260_SETF), "%s", FIMC_IS_IMX260_SETF);
		} else if (sensor_id == SENSOR_NAME_SR544) {
			/* SR544 */
			snprintf(sysfs_finfo.load_fw_name, sizeof(FIMC_IS_FW_SR544), "%s", FIMC_IS_FW_SR544);
			snprintf(sysfs_finfo.load_setfile_name, sizeof(FIMC_IS_SR544_SETF), "%s", FIMC_IS_SR544_SETF);
	   } else if (sensor_id == SENSOR_NAME_S5K5E9) {
			/* SENSOR_NAME_S5K5E9 */
			snprintf(sysfs_finfo.load_fw_name, sizeof(FIMC_IS_FW_S5K5E9), "%s", FIMC_IS_FW_S5K5E9);
			snprintf(sysfs_finfo.load_setfile_name, sizeof(FIMC_IS_5E9_SETF), "%s", FIMC_IS_5E9_SETF);	
	   } else if (sensor_id == SENSOR_NAME_GC5035) {
			/* GC5035 */
			snprintf(sysfs_finfo.load_fw_name, sizeof(FIMC_IS_FW_GC5035), "%s", FIMC_IS_FW_GC5035);
			snprintf(sysfs_finfo.load_setfile_name, sizeof(FIMC_IS_GC5035_SETF), "%s", FIMC_IS_GC5035_SETF);
		} else {
			snprintf(sysfs_finfo.load_fw_name, sizeof(FIMC_IS_FW_2P2), "%s", FIMC_IS_FW_2P2);
			snprintf(sysfs_finfo.load_setfile_name, sizeof(FIMC_IS_2P2_SETF), "%s", FIMC_IS_2P2_SETF);
		}
	}

	return 0;
}

int fimc_is_sec_run_fw_sel(struct device *dev, int position)
{
	struct fimc_is_core *core = (struct fimc_is_core *)dev_get_drvdata(fimc_is_dev);
	int ret = 0;
	struct fimc_is_vender_specific *specific = core->vender.private_data;

#ifdef CONFIG_CAMERA_USE_SOC_SENSOR
	info("SOC_SENSOR!");
	return -EINVAL;
#endif
	/* Check reload cal data enabled */
	if (!sysfs_finfo.is_check_cal_reload) {
		if (fimc_is_sec_file_exist("/data/media/0/")) {
			/* Check reload cal data enabled */
			fimc_is_sec_check_reload(core);
			sysfs_finfo.is_check_cal_reload = true;
		}
	}

#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_FRONT) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_FRONT)
	if (position == SENSOR_POSITION_FRONT) {
		if (!sysfs_finfo.is_caldata_read || force_caldata_dump) {
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR)
			ret = fimc_is_sec_fw_sel_eeprom(dev, SENSOR_POSITION_REAR, true);
#else
/* When using C2 retention, Cal loading for both front and rear cam will be done at a time */
#if !defined(CONFIG_COMPANION_C2_USE) && !defined(CONFIG_COMPANION_C3_USE)
			ret = fimc_is_sec_fw_sel(core, dev, true);
#endif
#endif /* defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR) */
		}

		if ((!sysfs_finfo_front.is_caldata_read || force_caldata_dump) && !specific->running_front_camera) {
			ret = fimc_is_sec_fw_sel_eeprom(dev, SENSOR_POSITION_FRONT, false);
			if (ret < 0) {
				err("failed to select firmware (%d)", ret);
				goto p_err;
			}
		} else {
			warn("not reading from front otprom because front sensor is running");
		}
	} else
#endif /* defined(CONFIG_CAMERA_EEPROM_SUPPORT_FRONT) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_FRONT) */

#if defined (CONFIG_CAMERA_EEPROM_SUPPORT_REAR2)
	if ( position == SENSOR_POSITION_REAR2) {
		if (!sysfs_finfo_rear2.is_caldata_read || force_caldata_dump) {
			info("eeprom : reading eeprom for rear2");
			ret = fimc_is_sec_fw_sel_eeprom(dev, position, false);
			if (ret < 0) {
				err("failed to select firmware (%d)", ret);
				goto p_err;
			}
		}
	}
	else
#endif

#if defined (CONFIG_CAMERA_EEPROM_SUPPORT_REAR3)
	if ( position == SENSOR_POSITION_REAR3) {
		if (!sysfs_finfo_rear3.is_caldata_read || force_caldata_dump) {
			info("eeprom : reading eeprom for rear3");
			ret = fimc_is_sec_fw_sel_eeprom(dev, position, false);
			if (ret < 0) {
				err("failed to select firmware (%d)", ret);
				goto p_err;
			}
		}
	}
	else
#endif

	if (position == SENSOR_POSITION_REAR) {
		if (!sysfs_finfo.is_caldata_read || force_caldata_dump) {
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR)
			ret = fimc_is_sec_fw_sel_eeprom(dev, position, false);
#else
			ret = fimc_is_sec_fw_sel(core, dev, false);
#endif
			if (ret < 0) {
				err("failed to select firmware (%d)", ret);
				goto p_err;
			}
		}
	}

p_err:
	if (position == SENSOR_POSITION_REAR) {
		if (specific->check_sensor_vendor) {
			if (fimc_is_sec_check_from_ver(core, position)) {
				if (sysfs_finfo.header_ver[3] != 'L') {
					err("Not supported module. Module ver = %s", sysfs_finfo.header_ver);
					return  -EIO;
				}
			}
		}
	}
	else if (position == SENSOR_POSITION_REAR2) {
		if (specific->check_sensor_vendor) {
			if (fimc_is_sec_check_from_ver(core, position)) {
				if (sysfs_finfo_rear2.header_ver[3] != 'L') {
					err("Not supported module. Module ver = %s", sysfs_finfo_rear2.header_ver);
					return  -EIO;
				}
			}
		}
	}

	else if (position == SENSOR_POSITION_REAR3) {
		if (specific->check_sensor_vendor) {
			if (fimc_is_sec_check_from_ver(core, position)) {
				if (sysfs_finfo_rear3.header_ver[3] != 'L') {
					err("Not supported module. Module ver = %s", sysfs_finfo_rear3.header_ver);
					return  -EIO;
				}
			}
		}
	}
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_FRONT) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_FRONT)
	else {
		if (specific->check_sensor_vendor) {
			if (fimc_is_sec_check_from_ver(core, position)) {
				if (sysfs_finfo_front.header_ver[3] != 'L') {
					err("Not supported front module. Module ver = %s", sysfs_finfo_front.header_ver);
					return  -EIO;
				}
			}
		}
	}
#endif

	return ret;
}

#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR) || defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR2) || defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR3)\
	|| defined(CONFIG_CAMERA_EEPROM_SUPPORT_FRONT)\
	|| defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_FRONT)
int fimc_is_sec_fw_sel_eeprom(struct device *dev, int id, bool headerOnly)
{
	int ret = 0;
	char fw_path[100];
	char phone_fw_version[FIMC_IS_HEADER_VER_SIZE + 1] = {0, };

	struct file *fp = NULL;
	mm_segment_t old_fs;
	long fsize, nread;
	u8 *read_buf = NULL;
	u8 *temp_buf = NULL;
	bool is_ldo_enabled;
	struct fimc_is_core *core = (struct fimc_is_core *)dev_get_drvdata(fimc_is_dev);
	struct fimc_is_vender_specific *specific = core->vender.private_data;

	is_ldo_enabled = false;

	/* Use mutex for i2c read */
	mutex_lock(&specific->spi_lock);

#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_FRONT) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_FRONT)
	if (id == SENSOR_POSITION_FRONT) {
		if (!sysfs_finfo_front.is_caldata_read || force_caldata_dump) {
			if (force_caldata_dump) {
				info("Front forced caldata dump!!\n");
				crc32_check_front = false;
				crc32_header_check_front = false;
			}

			fimc_is_sec_rom_power_on(core, SENSOR_POSITION_FRONT);
			is_ldo_enabled = true;

#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_FRONT)
			info("Camera: read cal data from Front EEPROM\n");
			if (!fimc_is_sec_readcal_eeprom(dev, SENSOR_POSITION_FRONT)) {
				sysfs_finfo_front.is_caldata_read = true;
			}
#else
			info("Camera: read cal data from Front OTPROM\n");
			if (!fimc_is_sec_readcal_otprom(dev, SENSOR_POSITION_FRONT)) {
				sysfs_finfo_front.is_caldata_read = true;
			}
#endif
		}
		goto exit;
	} else
#endif

#if defined (CONFIG_CAMERA_EEPROM_SUPPORT_REAR2)
	 if (id == SENSOR_POSITION_REAR2) {
		if (!sysfs_finfo_rear2.is_caldata_read || force_caldata_dump) {
			is_dumped_fw_loading_needed = false;
			if (force_caldata_dump) {
				info("Rear2 [%d]forced caldata dump!!\n", id);
				crc32_check = false;
				crc32_header_check = false;
			}
			fimc_is_sec_rom_power_on(core, id);
			is_ldo_enabled = true;
			info("Camera Rear2 EEPROM\n");
			if (headerOnly) {
				fimc_is_sec_read_eeprom_header(dev, id);
			} else {
				if (!fimc_is_sec_readcal_eeprom(dev, id)) {
					sysfs_finfo_rear2.is_caldata_read = true;
				}
			}
		}
	 } else
#endif /* CONFIG_CAMERA_EEPROM_SUPPORT_REAR2 */

#if defined (CONFIG_CAMERA_EEPROM_SUPPORT_REAR3)
	 if (id == SENSOR_POSITION_REAR3) {
		if (!sysfs_finfo_rear3.is_caldata_read || force_caldata_dump) {
			is_dumped_fw_loading_needed = false;
			if (force_caldata_dump) {
				info("Rear3 [%d]forced caldata dump!!\n", id);
				crc32_check = false;
				crc32_header_check = false;
			}
			fimc_is_sec_rom_power_on(core, id);
			is_ldo_enabled = true;
			info("Camera Rear3 EEPROM\n");
			if (headerOnly) {
				fimc_is_sec_read_eeprom_header(dev, id);
			} else {
				if (!fimc_is_sec_readcal_eeprom(dev, id)) {
					sysfs_finfo_rear3.is_caldata_read = true;
				}
			}
		}
	 } else
#endif /* CONFIG_CAMERA_EEPROM_SUPPORT_REAR3 */
	{
		if (!sysfs_finfo.is_caldata_read || force_caldata_dump) {
			is_dumped_fw_loading_needed = false;
			if (force_caldata_dump) {
				info("Rear [%d]forced caldata dump!!\n", id);
				crc32_check = false;
				crc32_header_check = false;
			}
#if defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR)
			else
#endif
			{
				fimc_is_sec_rom_power_on(core, id);
				is_ldo_enabled = true;
			}
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR)
			info("Camera: read cal data from Rear EEPROM\n");
#elif defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR)
			info("Camera: read cal data from Rear OTPROM\n");
#endif
			if (headerOnly) {
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR)
				fimc_is_sec_read_eeprom_header(dev, id);
#elif defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR)
				fimc_is_sec_read_otprom_header(dev, SENSOR_POSITION_REAR);
#endif
			} else {
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR)
				if (!fimc_is_sec_readcal_eeprom(dev, id)) {
					sysfs_finfo.is_caldata_read = true;
				}
#elif defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR)
				if (!fimc_is_sec_readcal_otprom(dev, SENSOR_POSITION_REAR)) {
					sysfs_finfo.is_caldata_read = true;
				}
#endif
			}
		}
	}

	fimc_is_sec_fw_find(core, id);
	if (headerOnly) {
		goto exit;
	}

	snprintf(fw_path, sizeof(fw_path), "%s%s", FIMC_IS_FW_PATH, sysfs_finfo.load_fw_name);

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	fp = filp_open(fw_path, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		err("Camera: Failed open phone firmware [%s]", fw_path);
//#ifndef CAMERA_REAR2_M10_TEMP	// TODO : need to check why this only fails for rear2 camera in m10
//		ret = -EIO;
//#endif
#ifndef CAMERA_REAR3_M10_TEMP	// TODO : need to check why this only fails for rear3 camera in m10
		ret = -EIO;
#endif
		fp = NULL;
		goto read_phone_fw_exit;
	}

	fsize = fp->f_path.dentry->d_inode->i_size;
	info("start, file path %s, size %ld Bytes\n",
		fw_path, fsize);

#ifdef CAMERA_MODULE_DUALIZE
	if (FIMC_IS_MAX_FW_SIZE >= fsize) {
		memset(fw_buf, 0x0, FIMC_IS_MAX_FW_SIZE);
		temp_buf = fw_buf;
	} else
#endif
	{
		info("Phone FW size is larger than FW buffer. Use vmalloc.\n");
		read_buf = vmalloc(fsize);
		if (!read_buf) {
			err("failed to allocate memory");
			ret = -ENOMEM;
			goto read_phone_fw_exit;
		}
		temp_buf = read_buf;
	}
	nread = vfs_read(fp, (char __user *)temp_buf, fsize, &fp->f_pos);
	if (nread != fsize) {
		err("failed to read firmware file, %ld Bytes", nread);
		ret = -EIO;
		goto read_phone_fw_exit;
	}

	strncpy(phone_fw_version, temp_buf + nread - 11, FIMC_IS_HEADER_VER_SIZE);
	strncpy(sysfs_pinfo.header_ver, temp_buf + nread - 11, FIMC_IS_HEADER_VER_SIZE);
	info("Camera: phone fw version: %s\n", phone_fw_version);

read_phone_fw_exit:
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

exit:
#if defined(CAMERA_SHARED_IO_POWER)
	if (is_ldo_enabled && (!specific->running_front_camera && !specific->running_rear_camera)) {
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_FRONT) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_FRONT)
		if (id == SENSOR_POSITION_FRONT) {
			fimc_is_sec_rom_power_off(core, SENSOR_POSITION_FRONT);
		} else
#endif
		{
			fimc_is_sec_rom_power_off(core, SENSOR_POSITION_REAR);
		}
	}
#else /* defined(CAMERA_SHARED_IO_POWER) */
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_FRONT) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_FRONT)
	if (id == SENSOR_POSITION_FRONT) {
		if (is_ldo_enabled && !specific->running_front_camera)
			fimc_is_sec_rom_power_off(core, SENSOR_POSITION_FRONT);
	} else
#endif /* defined(CONFIG_CAMERA_EEPROM_SUPPORT_FRONT) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_FRONT) */
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR2)
	if (id == SENSOR_POSITION_REAR2) {
		if (is_ldo_enabled && !specific->running_rear2_camera)
			fimc_is_sec_rom_power_off(core, SENSOR_POSITION_REAR2);
	} else
#endif /* CONFIG_CAMERA_EEPROM_SUPPORT_REAR2 */

#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR3)
#if defined(CAMERA_SHARED_IO_POWER_UW)
	if (id == SENSOR_POSITION_REAR3) {
		if (is_ldo_enabled && !specific->running_rear3_camera && !specific->running_front_camera) {
			fimc_is_sec_rom_power_off(core, SENSOR_POSITION_REAR3);
		} else {
			info("front is running so don't power off\n");
		}
	} else
#else /* defined(CAMERA_SHARED_IO_POWER_UW) */
	if (id == SENSOR_POSITION_REAR3) {
		if (is_ldo_enabled && !specific->running_rear3_camera)
			fimc_is_sec_rom_power_off(core, SENSOR_POSITION_REAR3);
	} else
#endif /* defined(CAMERA_SHARED_IO_POWER_UW) */
#endif /* CONFIG_CAMERA_EEPROM_SUPPORT_REAR3 */

	if (id == SENSOR_POSITION_REAR) {
		if (is_ldo_enabled && !specific->running_rear_camera)
			fimc_is_sec_rom_power_off(core, SENSOR_POSITION_REAR);
	}
#endif /* defined(CAMERA_SHARED_IO_POWER) */

	mutex_unlock(&specific->spi_lock);

	return ret;
}
#endif

#if !defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR) && !defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR)
int fimc_is_sec_fw_sel(struct fimc_is_core *core, struct device *dev, bool headerOnly)
{
	int ret = 0;
	char fw_path[100];
	char dump_fw_path[100];
	char dump_fw_version[FIMC_IS_HEADER_VER_SIZE + 1] = {0, };
	char phone_fw_version[FIMC_IS_HEADER_VER_SIZE + 1] = {0, };
#ifdef CAMERA_MODULE_DUALIZE
	int from_fw_revision = 0;
	int dump_fw_revision = 0;
	int phone_fw_revision = 0;
	bool dump_flag = false;
	struct file *setfile_fp = NULL;
	char setfile_path[100];
#endif
	struct file *fp = NULL;
	mm_segment_t old_fs;
	long fsize, nread;
	u8 *read_buf = NULL;
	u8 *temp_buf = NULL;
	bool is_dump_existed = false;
	bool is_dump_needed = false;
	struct fimc_is_vender_specific *specific = core->vender.private_data;
#ifdef CONFIG_COMPANION_USE
	struct fimc_is_spi *spi = &core->spi0;
#endif
	bool is_ldo_enabled = false;
	struct exynos_platform_fimc_is *core_pdata = NULL;

	core_pdata = dev_get_platdata(fimc_is_dev);
	if (!core_pdata) {
		err("core->pdata is null\n");
		return -EINVAL;
	}

	/* Use mutex for spi read */
	mutex_lock(&specific->spi_lock);
	if (!sysfs_finfo.is_caldata_read || force_caldata_dump) {
		is_dumped_fw_loading_needed = false;
		if (force_caldata_dump)
			info("forced caldata dump!!\n");

		if (fimc_is_sec_ldo_enabled(dev, "VDDIO_1.8V_CAM") <= 0) {
			fimc_is_sec_rom_power_on(core, SENSOR_POSITION_REAR);
			is_ldo_enabled = true;
		}
		info("read cal data from FROM\n");
#ifdef CONFIG_COMPANION_USE
		fimc_is_spi_s_pin(spi, false);
#endif

		if (headerOnly) {
			fimc_is_sec_read_from_header(dev);
		} else {
			if (!fimc_is_sec_readcal(core)) {
				sysfs_finfo.is_caldata_read = true;
			}
		}
#ifdef CONFIG_COMPANION_USE
		fimc_is_spi_s_pin(spi, true);
#endif
		/*select AF actuator*/
		if (!crc32_header_check) {
			info("Camera : CRC32 error for all section.\n");
		}

		fimc_is_sec_fw_find(core, SENSOR_POSITION_REAR);
		if (headerOnly) {
			goto exit;
		}

		snprintf(fw_path, sizeof(fw_path), "%s%s", FIMC_IS_FW_PATH, sysfs_finfo.load_fw_name);

		snprintf(dump_fw_path, sizeof(dump_fw_path), "%s%s",
			FIMC_IS_FW_DUMP_PATH, sysfs_finfo.load_fw_name);
		info("Camera: f-rom fw version: %s\n", sysfs_finfo.header_ver);

		old_fs = get_fs();
		set_fs(KERNEL_DS);
		fp = filp_open(dump_fw_path, O_RDONLY, 0);
		if (IS_ERR(fp)) {
			info("Camera: There is no dumped firmware(%s)\n", dump_fw_path);
			is_dump_existed = false;
			goto read_phone_fw;
		} else {
			is_dump_existed = true;
		}

		fsize = fp->f_path.dentry->d_inode->i_size;
		info("start, file path %s, size %ld Bytes\n",
			dump_fw_path, fsize);

#ifdef CAMERA_MODULE_DUALIZE
		if (FIMC_IS_MAX_FW_SIZE >= fsize) {
			memset(fw_buf, 0x0, FIMC_IS_MAX_FW_SIZE);
			temp_buf = fw_buf;
		} else
#endif
		{
			info("Dumped FW size is larger than FW buffer. Use vmalloc.\n");
			read_buf = vmalloc(fsize);
			if (!read_buf) {
				err("failed to allocate memory");
				ret = -ENOMEM;
				goto read_phone_fw;
			}
			temp_buf = read_buf;
		}
		nread = vfs_read(fp, (char __user *)temp_buf, fsize, &fp->f_pos);
		if (nread != fsize) {
			err("failed to read firmware file, %ld Bytes", nread);
			ret = -EIO;
			goto read_phone_fw;
		}

		strncpy(dump_fw_version, temp_buf + nread-11, FIMC_IS_HEADER_VER_SIZE);
		info("Camera: dumped fw version: %s\n", dump_fw_version);

read_phone_fw:
		if (read_buf) {
			vfree(read_buf);
			read_buf = NULL;
			temp_buf = NULL;
		}

		if (fp && is_dump_existed) {
			filp_close(fp, current->files);
			fp = NULL;
		}

		set_fs(old_fs);

		if (ret < 0)
			goto exit;

		old_fs = get_fs();
		set_fs(KERNEL_DS);

		fp = filp_open(fw_path, O_RDONLY, 0);
		if (IS_ERR(fp)) {
			err("Camera: Failed open phone firmware(%s)", fw_path);
			fp = NULL;
			goto read_phone_fw_exit;
		}

		fsize = fp->f_path.dentry->d_inode->i_size;
		info("start, file path %s, size %ld Bytes\n", fw_path, fsize);

#ifdef CAMERA_MODULE_DUALIZE
		if (FIMC_IS_MAX_FW_SIZE >= fsize) {
			memset(fw_buf, 0x0, FIMC_IS_MAX_FW_SIZE);
			temp_buf = fw_buf;
		} else
#endif
		{
			info("Phone FW size is larger than FW buffer. Use vmalloc.\n");
			read_buf = vmalloc(fsize);
			if (!read_buf) {
				err("failed to allocate memory");
				ret = -ENOMEM;
				goto read_phone_fw_exit;
			}
			temp_buf = read_buf;
		}
		nread = vfs_read(fp, (char __user *)temp_buf, fsize, &fp->f_pos);
		if (nread != fsize) {
			err("failed to read firmware file, %ld Bytes", nread);
			ret = -EIO;
			goto read_phone_fw_exit;
		}

		strncpy(phone_fw_version, temp_buf + nread - 11, FIMC_IS_HEADER_VER_SIZE);
		strncpy(sysfs_pinfo.header_ver, temp_buf + nread - 11, FIMC_IS_HEADER_VER_SIZE);
		info("Camera: phone fw version: %s\n", phone_fw_version);

read_phone_fw_exit:
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

		if (ret < 0)
			goto exit;

#if defined(CAMERA_MODULE_DUALIZE) && defined(CAMERA_MODULE_AVAILABLE_DUMP_VERSION)
		if (!strncmp(CAMERA_MODULE_AVAILABLE_DUMP_VERSION, sysfs_finfo.header_ver, 3)) {
			from_fw_revision = fimc_is_sec_fw_revision(sysfs_finfo.header_ver);
			phone_fw_revision = fimc_is_sec_fw_revision(phone_fw_version);
			if (is_dump_existed) {
				dump_fw_revision = fimc_is_sec_fw_revision(dump_fw_version);
			}

			info("from_fw_revision = %d, phone_fw_revision = %d, dump_fw_revision = %d\n",
				from_fw_revision, phone_fw_revision, dump_fw_revision);

			if (fimc_is_sec_compare_ver(SENSOR_POSITION_REAR) /* Check if a module is connected or not */
				&& (!fimc_is_sec_fw_module_compare(sysfs_finfo.header_ver, phone_fw_version) ||
				   (from_fw_revision > phone_fw_revision))) {
				is_dumped_fw_loading_needed = true;
				if (is_dump_existed) {
					if (!fimc_is_sec_fw_module_compare(sysfs_finfo.header_ver,
								dump_fw_version)) {
						is_dump_needed = true;
					} else if (from_fw_revision > dump_fw_revision) {
						is_dump_needed = true;
					} else {
						is_dump_needed = false;
					}
				} else {
					is_dump_needed = true;
				}
			} else {
				is_dump_needed = false;
				if (is_dump_existed) {
					if (!fimc_is_sec_fw_module_compare(phone_fw_version,
						dump_fw_version)) {
						is_dumped_fw_loading_needed = false;
					} else if (phone_fw_revision > dump_fw_revision) {
						is_dumped_fw_loading_needed = false;
					} else {
						is_dumped_fw_loading_needed = true;
					}
				} else {
					is_dumped_fw_loading_needed = false;
				}
			}

			if (force_caldata_dump) {
				if ((!fimc_is_sec_fw_module_compare(sysfs_finfo.header_ver, phone_fw_version))
					|| (from_fw_revision > phone_fw_revision))
					dump_flag = true;
			} else {
				if (is_dump_needed) {
					dump_flag = true;
					crc32_fw_check = false;
					crc32_setfile_check = false;
				}
			}

			if (dump_flag) {
				info("Dump ISP Firmware.\n");
#ifdef CONFIG_COMPANION_USE
				fimc_is_spi_s_pin(spi, false);
#endif
				ret = fimc_is_sec_readfw(core);
				msleep(20);
				ret |= fimc_is_sec_read_setfile(core);
#ifdef CONFIG_COMPANION_USE
				fimc_is_spi_s_pin(spi, true);
#endif
				if (ret < 0) {
					if (!crc32_fw_check || !crc32_setfile_check) {
						is_dumped_fw_loading_needed = false;
						err("Firmware CRC is not valid. Does not use dumped firmware.\n");
					}
				}
			}

			if (phone_fw_version[0] == 0) {
				strcpy(sysfs_pinfo.header_ver, "NULL");
			}

			if (is_dumped_fw_loading_needed) {
				old_fs = get_fs();
				set_fs(KERNEL_DS);
				snprintf(setfile_path, sizeof(setfile_path), "%s%s",
					FIMC_IS_FW_DUMP_PATH, sysfs_finfo.load_setfile_name);
				setfile_fp = filp_open(setfile_path, O_RDONLY, 0);
				if (IS_ERR_OR_NULL(setfile_fp)) {
					set_fs(old_fs);
					crc32_setfile_check = false;
					info("setfile does not exist. Retry setfile dump.\n");
#ifdef CONFIG_COMPANION_USE
					fimc_is_spi_s_pin(spi, false);
#endif
					fimc_is_sec_read_setfile(core);
#ifdef CONFIG_COMPANION_USE
					fimc_is_spi_s_pin(spi, true);
#endif
					setfile_fp = NULL;
				} else {
					if (setfile_fp)
						filp_close(setfile_fp, current->files);
					set_fs(old_fs);
				}
			}
		}
#endif

		if (is_dump_needed && is_dumped_fw_loading_needed) {
			strncpy(loaded_fw, sysfs_finfo.header_ver, FIMC_IS_HEADER_VER_SIZE);
		} else if (!is_dump_needed && is_dumped_fw_loading_needed) {
			strncpy(loaded_fw, dump_fw_version, FIMC_IS_HEADER_VER_SIZE);
		} else {
			strncpy(loaded_fw, phone_fw_version, FIMC_IS_HEADER_VER_SIZE);
		}

	} else {
		info("already loaded the firmware, Phone version=%s, F-ROM version=%s\n",
			sysfs_pinfo.header_ver, sysfs_finfo.header_ver);
	}

exit:
#ifdef CONFIG_COMPANION_USE
	fimc_is_spi_s_pin(spi, true);
#endif
	if (is_ldo_enabled && !specific->running_rear_camera)
		fimc_is_sec_rom_power_off(core, SENSOR_POSITION_REAR);

	mutex_unlock(&specific->spi_lock);

	return ret;
}
#endif

#ifdef CONFIG_COMPANION_USE
int fimc_is_sec_concord_fw_sel(struct fimc_is_core *core, struct device *dev)
{
	int ret = 0;
	char c1_fw_path[100];
	char dump_c1_fw_path[100];
	char dump_c1_fw_version[FIMC_IS_HEADER_VER_SIZE + 1] = {0, };
	char phone_c1_fw_version[FIMC_IS_HEADER_VER_SIZE + 1] = {0, };
#ifdef CAMERA_MODULE_DUALIZE
	int from_c1_fw_revision = 0;
	int dump_c1_fw_revision = 0;
	int phone_c1_fw_revision = 0;
#endif
	struct file *fp = NULL;
	mm_segment_t old_fs;
	long fsize, nread;
	u8 *read_buf = NULL;
	u8 *temp_buf = NULL;
	bool is_dump_existed = false;
	bool is_dump_needed = false;
	int sensor_id = 0;
	bool is_ldo_enabled = false;
	struct fimc_is_spi *spi = &core->spi0;
	struct fimc_is_vender_specific *specific = core->vender.private_data;

	mutex_lock(&specific->spi_lock);
	if ((!sysfs_finfo.is_c1_caldata_read &&
	    (cam_id == CAMERA_SINGLE_REAR /* || cam_id == CAMERA_DUAL_FRONT*/)) ||
	    force_caldata_dump) {
		is_dumped_c1_fw_loading_needed = false;
		if (force_caldata_dump)
			info("forced caldata dump!!\n");

		sysfs_finfo.is_c1_caldata_read = true;

		if (fimc_is_sec_fw_module_compare(sysfs_finfo.concord_header_ver, FW_2P2_F) ||
		    fimc_is_sec_fw_module_compare(sysfs_finfo.concord_header_ver, FW_2P2_I) ||
		    fimc_is_sec_fw_module_compare(sysfs_finfo.concord_header_ver, FW_2P2_A) ||
		    fimc_is_sec_fw_module_compare(sysfs_finfo.concord_header_ver, FW_2P2_B) ||
		    fimc_is_sec_fw_module_compare(sysfs_finfo.concord_header_ver, FW_2P2_C)) {
			snprintf(c1_fw_path, sizeof(c1_fw_path), "%s%s",
				FIMC_IS_FW_PATH, FIMC_IS_FW_COMPANION_2P2_EVT1);
			snprintf(sysfs_finfo.load_c1_fw_name, sizeof(FIMC_IS_FW_COMPANION_2P2_EVT1), "%s", FIMC_IS_FW_COMPANION_2P2_EVT1);
			snprintf(sysfs_finfo.load_c1_mastersetf_name, sizeof(FIMC_IS_COMPANION_2P2_MASTER_SETF), "%s", FIMC_IS_COMPANION_2P2_MASTER_SETF);
			snprintf(sysfs_finfo.load_c1_modesetf_name, sizeof(FIMC_IS_COMPANION_2P2_MODE_SETF), "%s", FIMC_IS_COMPANION_2P2_MODE_SETF);
		} else if (fimc_is_sec_fw_module_compare(sysfs_finfo.concord_header_ver, FW_IMX228)) {
			snprintf(c1_fw_path, sizeof(c1_fw_path), "%s%s",
				FIMC_IS_FW_PATH, FIMC_IS_FW_COMPANION_IMX228_EVT1);
			snprintf(sysfs_finfo.load_c1_fw_name, sizeof(FIMC_IS_FW_COMPANION_IMX228_EVT1), "%s", FIMC_IS_FW_COMPANION_IMX228_EVT1);
			snprintf(sysfs_finfo.load_c1_mastersetf_name, sizeof(FIMC_IS_COMPANION_IMX228_MASTER_SETF), "%s", FIMC_IS_COMPANION_IMX228_MASTER_SETF);
			snprintf(sysfs_finfo.load_c1_modesetf_name, sizeof(FIMC_IS_COMPANION_IMX228_MODE_SETF), "%s", FIMC_IS_COMPANION_IMX228_MODE_SETF);
		} else if (fimc_is_sec_fw_module_compare(sysfs_finfo.concord_header_ver, FW_IMX240_A) ||
		           fimc_is_sec_fw_module_compare(sysfs_finfo.concord_header_ver, FW_IMX240_B) ||
		           fimc_is_sec_fw_module_compare(sysfs_finfo.concord_header_ver, FW_IMX240_C)) {
			snprintf(c1_fw_path, sizeof(c1_fw_path), "%s%s",
				FIMC_IS_FW_PATH, FIMC_IS_FW_COMPANION_IMX240_EVT1);
			snprintf(sysfs_finfo.load_c1_fw_name, sizeof(FIMC_IS_FW_COMPANION_IMX240_EVT1), "%s", FIMC_IS_FW_COMPANION_IMX240_EVT1);
			snprintf(sysfs_finfo.load_c1_mastersetf_name, sizeof(FIMC_IS_COMPANION_IMX240_MASTER_SETF), "%s", FIMC_IS_COMPANION_IMX240_MASTER_SETF);
			snprintf(sysfs_finfo.load_c1_modesetf_name, sizeof(FIMC_IS_COMPANION_IMX240_MODE_SETF), "%s", FIMC_IS_COMPANION_IMX240_MODE_SETF);
		} else if (fimc_is_sec_fw_module_compare(sysfs_finfo.concord_header_ver, FW_IMX260)) {
			snprintf(c1_fw_path, sizeof(c1_fw_path), "%s%s",
				FIMC_IS_FW_PATH, FIMC_IS_FW_COMPANION_IMX260);
			snprintf(sysfs_finfo.load_c1_fw_name, sizeof(FIMC_IS_FW_COMPANION_IMX260), "%s", FIMC_IS_FW_COMPANION_IMX260);
			snprintf(sysfs_finfo.load_c1_mastersetf_name, sizeof(FIMC_IS_COMPANION_IMX260_MASTER_SETF), "%s", FIMC_IS_COMPANION_IMX260_MASTER_SETF);
			snprintf(sysfs_finfo.load_c1_modesetf_name, sizeof(FIMC_IS_COMPANION_IMX260_MODE_SETF), "%s", FIMC_IS_COMPANION_IMX260_MODE_SETF);
		} else if (fimc_is_sec_fw_module_compare(sysfs_finfo.concord_header_ver, FW_2P2_12M)) {
			snprintf(c1_fw_path, sizeof(c1_fw_path), "%s%s",
				FIMC_IS_FW_PATH, FIMC_IS_FW_COMPANION_2P2_12M_EVT1);
			snprintf(sysfs_finfo.load_c1_fw_name, sizeof(FIMC_IS_FW_COMPANION_2P2_12M_EVT1), "%s", FIMC_IS_FW_COMPANION_2P2_12M_EVT1);
			snprintf(sysfs_finfo.load_c1_mastersetf_name, sizeof(FIMC_IS_COMPANION_2P2_12M_MASTER_SETF), "%s", FIMC_IS_COMPANION_2P2_12M_MASTER_SETF);
			snprintf(sysfs_finfo.load_c1_modesetf_name, sizeof(FIMC_IS_COMPANION_2P2_12M_MODE_SETF), "%s", FIMC_IS_COMPANION_2P2_12M_MODE_SETF);
		} else if (fimc_is_sec_fw_module_compare(sysfs_finfo.concord_header_ver, FW_2T2)) {
			snprintf(c1_fw_path, sizeof(c1_fw_path), "%s%s",
				FIMC_IS_FW_PATH, FIMC_IS_FW_COMPANION_2T2_EVT1);
			snprintf(sysfs_finfo.load_c1_fw_name, sizeof(FIMC_IS_FW_COMPANION_2T2_EVT1), "%s", FIMC_IS_FW_COMPANION_2T2_EVT1);
			snprintf(sysfs_finfo.load_c1_mastersetf_name, sizeof(FIMC_IS_COMPANION_2T2_MASTER_SETF), "%s", FIMC_IS_COMPANION_2T2_MASTER_SETF);
			snprintf(sysfs_finfo.load_c1_modesetf_name, sizeof(FIMC_IS_COMPANION_2T2_MODE_SETF), "%s", FIMC_IS_COMPANION_2T2_MODE_SETF);
		} else {
			err("Companion FW selection failed! Default FW will be used");
			sensor_id = specific->rear_sensor_id;
			if (sensor_id == SENSOR_NAME_IMX240) {
				snprintf(c1_fw_path, sizeof(c1_fw_path), "%s%s",
					FIMC_IS_FW_PATH, FIMC_IS_FW_COMPANION_IMX240_EVT1);
				snprintf(sysfs_finfo.load_c1_fw_name, sizeof(FIMC_IS_FW_COMPANION_IMX240_EVT1), "%s", FIMC_IS_FW_COMPANION_IMX240_EVT1);
				snprintf(sysfs_finfo.load_c1_mastersetf_name, sizeof(FIMC_IS_COMPANION_IMX240_MASTER_SETF), "%s", FIMC_IS_COMPANION_IMX240_MASTER_SETF);
				snprintf(sysfs_finfo.load_c1_modesetf_name, sizeof(FIMC_IS_COMPANION_IMX240_MODE_SETF), "%s", FIMC_IS_COMPANION_IMX240_MODE_SETF);
			} else if (sensor_id == SENSOR_NAME_IMX228) {
				snprintf(c1_fw_path, sizeof(c1_fw_path), "%s%s",
					FIMC_IS_FW_PATH, FIMC_IS_FW_COMPANION_IMX228_EVT1);
				snprintf(sysfs_finfo.load_c1_fw_name, sizeof(FIMC_IS_FW_COMPANION_IMX228_EVT1), "%s", FIMC_IS_FW_COMPANION_IMX228_EVT1);
				snprintf(sysfs_finfo.load_c1_mastersetf_name, sizeof(FIMC_IS_COMPANION_IMX228_MASTER_SETF), "%s", FIMC_IS_COMPANION_IMX228_MASTER_SETF);
				snprintf(sysfs_finfo.load_c1_modesetf_name, sizeof(FIMC_IS_COMPANION_IMX228_MODE_SETF), "%s", FIMC_IS_COMPANION_IMX228_MODE_SETF);
			} else if (sensor_id == SENSOR_NAME_S5K2P2_12M) {
				snprintf(c1_fw_path, sizeof(c1_fw_path), "%s%s",
					FIMC_IS_FW_PATH, FIMC_IS_FW_COMPANION_2P2_12M_EVT1);
				snprintf(sysfs_finfo.load_c1_fw_name, sizeof(FIMC_IS_FW_COMPANION_2P2_12M_EVT1), "%s", FIMC_IS_FW_COMPANION_2P2_12M_EVT1);
				snprintf(sysfs_finfo.load_c1_mastersetf_name, sizeof(FIMC_IS_COMPANION_2P2_12M_MASTER_SETF), "%s", FIMC_IS_COMPANION_2P2_12M_MASTER_SETF);
				snprintf(sysfs_finfo.load_c1_modesetf_name, sizeof(FIMC_IS_COMPANION_2P2_12M_MODE_SETF), "%s", FIMC_IS_COMPANION_2P2_12M_MODE_SETF);
			} else if (sensor_id == SENSOR_NAME_S5K2P2) {
				snprintf(c1_fw_path, sizeof(c1_fw_path), "%s%s",
					FIMC_IS_FW_PATH, FIMC_IS_FW_COMPANION_2P2_EVT1);
				snprintf(sysfs_finfo.load_c1_fw_name, sizeof(FIMC_IS_FW_COMPANION_2P2_EVT1), "%s", FIMC_IS_FW_COMPANION_2P2_EVT1);
				snprintf(sysfs_finfo.load_c1_mastersetf_name, sizeof(FIMC_IS_COMPANION_2P2_MASTER_SETF), "%s", FIMC_IS_COMPANION_2P2_MASTER_SETF);
				snprintf(sysfs_finfo.load_c1_modesetf_name, sizeof(FIMC_IS_COMPANION_2P2_MODE_SETF), "%s", FIMC_IS_COMPANION_2P2_MODE_SETF);
			} else if (sensor_id == SENSOR_NAME_S5K2T2) {
				snprintf(c1_fw_path, sizeof(c1_fw_path), "%s%s",
					FIMC_IS_FW_PATH, FIMC_IS_FW_COMPANION_2T2_EVT1);
				snprintf(sysfs_finfo.load_c1_fw_name, sizeof(FIMC_IS_FW_COMPANION_2T2_EVT1), "%s", FIMC_IS_FW_COMPANION_2T2_EVT1);
				snprintf(sysfs_finfo.load_c1_mastersetf_name, sizeof(FIMC_IS_COMPANION_2T2_MASTER_SETF), "%s", FIMC_IS_COMPANION_2T2_MASTER_SETF);
				snprintf(sysfs_finfo.load_c1_modesetf_name, sizeof(FIMC_IS_COMPANION_2T2_MODE_SETF), "%s", FIMC_IS_COMPANION_2T2_MODE_SETF);
			} else if (sensor_id == SENSOR_NAME_IMX260) {
				snprintf(c1_fw_path, sizeof(c1_fw_path), "%s%s",
					FIMC_IS_FW_PATH, FIMC_IS_FW_COMPANION_IMX260);
				snprintf(sysfs_finfo.load_c1_fw_name, sizeof(FIMC_IS_FW_COMPANION_IMX260), "%s", FIMC_IS_FW_COMPANION_IMX260);
				snprintf(sysfs_finfo.load_c1_mastersetf_name, sizeof(FIMC_IS_COMPANION_IMX260_MASTER_SETF), "%s", FIMC_IS_COMPANION_IMX260_MASTER_SETF);
				snprintf(sysfs_finfo.load_c1_modesetf_name, sizeof(FIMC_IS_COMPANION_IMX260_MODE_SETF), "%s", FIMC_IS_COMPANION_IMX260_MODE_SETF);
			} else {
				snprintf(c1_fw_path, sizeof(c1_fw_path), "%s%s",
					FIMC_IS_FW_PATH, FIMC_IS_FW_COMPANION_IMX240_EVT1);
				snprintf(sysfs_finfo.load_c1_fw_name, sizeof(FIMC_IS_FW_COMPANION_IMX240_EVT1), "%s", FIMC_IS_FW_COMPANION_IMX240_EVT1);
				snprintf(sysfs_finfo.load_c1_mastersetf_name, sizeof(FIMC_IS_COMPANION_IMX240_MASTER_SETF), "%s", FIMC_IS_COMPANION_IMX240_MASTER_SETF);
				snprintf(sysfs_finfo.load_c1_modesetf_name, sizeof(FIMC_IS_COMPANION_IMX240_MODE_SETF), "%s", FIMC_IS_COMPANION_IMX240_MODE_SETF);
			}
		}

		snprintf(dump_c1_fw_path, sizeof(dump_c1_fw_path), "%s%s",
			FIMC_IS_FW_DUMP_PATH, sysfs_finfo.load_c1_fw_name);
		info("Camera: f-rom fw version: %s\n", sysfs_finfo.concord_header_ver);

		old_fs = get_fs();
		set_fs(KERNEL_DS);
		ret = 0;
		fp = filp_open(dump_c1_fw_path, O_RDONLY, 0);
		if (IS_ERR(fp)) {
			info("Camera: There is no dumped Companion firmware(%s)\n", dump_c1_fw_path);
			is_dump_existed = false;
			goto read_phone_fw;
		} else {
			is_dump_existed = true;
		}

		fsize = fp->f_path.dentry->d_inode->i_size;
		info("start, file path %s, size %ld Bytes\n",
			dump_c1_fw_path, fsize);

#ifdef CAMERA_MODULE_DUALIZE
		if (FIMC_IS_MAX_FW_SIZE >= fsize) {
			memset(fw_buf, 0x0, FIMC_IS_MAX_FW_SIZE);
			temp_buf = fw_buf;
		} else
#endif
		{
			info("Dumped Companion FW size is larger than FW buffer. Use vmalloc.\n");
			read_buf = vmalloc(fsize);
			if (!read_buf) {
				err("failed to allocate memory");
				ret = -ENOMEM;
				goto read_phone_fw;
			}
			temp_buf = read_buf;
		}
		nread = vfs_read(fp, (char __user *)temp_buf, fsize, &fp->f_pos);
		if (nread != fsize) {
			err("failed to read firmware file, %ld Bytes", nread);
			ret = -EIO;
			goto read_phone_fw;
		}

		strncpy(dump_c1_fw_version, temp_buf + nread - 16, FIMC_IS_HEADER_VER_SIZE);
		info("Camera: dumped companion fw version: %s\n", dump_c1_fw_version);

read_phone_fw:
		if (read_buf) {
			vfree(read_buf);
			read_buf = NULL;
			temp_buf = NULL;
		}

		if (fp && is_dump_existed) {
			filp_close(fp, current->files);
			fp = NULL;
		}

		set_fs(old_fs);
		if (ret < 0)
			goto exit;

		old_fs = get_fs();
		set_fs(KERNEL_DS);

		fp = filp_open(c1_fw_path, O_RDONLY, 0);
		if (IS_ERR(fp)) {
			err("Camera: Failed open phone companion firmware(%s)", c1_fw_path);
			fp = NULL;
			goto read_phone_fw_exit;
		}

		fsize = fp->f_path.dentry->d_inode->i_size;
		info("start, file path %s, size %ld Bytes\n",
			c1_fw_path, fsize);

#ifdef CAMERA_MODULE_DUALIZE
		if (FIMC_IS_MAX_FW_SIZE >= fsize) {
			memset(fw_buf, 0x0, FIMC_IS_MAX_FW_SIZE);
			temp_buf = fw_buf;
		} else
#endif
		{
			info("Phone Companion FW size is larger than FW buffer. Use vmalloc.\n");
			read_buf = vmalloc(fsize);
			if (!read_buf) {
				err("failed to allocate memory");
				ret = -ENOMEM;
				goto read_phone_fw_exit;
			}
			temp_buf = read_buf;
		}
		nread = vfs_read(fp, (char __user *)temp_buf, fsize, &fp->f_pos);
		if (nread != fsize) {
			err("failed to read companion firmware file, %ld Bytes", nread);
			ret = -EIO;
			goto read_phone_fw_exit;
		}

		strncpy(phone_c1_fw_version, temp_buf + nread - 16, FIMC_IS_HEADER_VER_SIZE);
		strncpy(sysfs_pinfo.concord_header_ver, temp_buf + nread - 16, FIMC_IS_HEADER_VER_SIZE);
		info("Camera: phone companion fw version: %s\n", phone_c1_fw_version);

read_phone_fw_exit:
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

		if (ret < 0)
			goto exit;

#if defined(CAMERA_MODULE_DUALIZE) && defined(CAMERA_MODULE_AVAILABLE_DUMP_VERSION)
		if (!strncmp(CAMERA_MODULE_AVAILABLE_DUMP_VERSION, sysfs_finfo.header_ver, 3)) {
			from_c1_fw_revision = fimc_is_sec_fw_revision(sysfs_finfo.concord_header_ver);
			phone_c1_fw_revision = fimc_is_sec_fw_revision(phone_c1_fw_version);
			if (is_dump_existed) {
				dump_c1_fw_revision = fimc_is_sec_fw_revision(dump_c1_fw_version);
			}

			info("from_c1_fw_revision = %d, phone_c1_fw_revision = %d, dump_c1_fw_revision = %d\n",
				from_c1_fw_revision, phone_c1_fw_revision, dump_c1_fw_revision);

			if ((!fimc_is_sec_fw_module_compare(sysfs_finfo.concord_header_ver, phone_c1_fw_version)) ||
					(from_c1_fw_revision > phone_c1_fw_revision)) {
				is_dumped_c1_fw_loading_needed = true;
				if (is_dump_existed) {
					if (!fimc_is_sec_fw_module_compare(sysfs_finfo.concord_header_ver,
								dump_c1_fw_version)) {
						is_dump_needed = true;
					} else if (from_c1_fw_revision > dump_c1_fw_revision) {
						is_dump_needed = true;
					} else {
						is_dump_needed = false;
					}
				} else {
					is_dump_needed = true;
				}
			} else {
				is_dump_needed = false;
				if (is_dump_existed) {
					if (!fimc_is_sec_fw_module_compare(phone_c1_fw_version,
								dump_c1_fw_version)) {
						is_dumped_c1_fw_loading_needed = false;
					} else if (phone_c1_fw_revision > dump_c1_fw_revision) {
						is_dumped_c1_fw_loading_needed = false;
					} else {
						is_dumped_c1_fw_loading_needed = true;
					}
				} else {
					is_dumped_c1_fw_loading_needed = false;
				}
			}

			if (is_dump_needed) {
				info("Dump companion Firmware.\n");
				crc32_c1_fw_check = false;
				if (fimc_is_sec_ldo_enabled(dev, "VDDIO_1.8V_CAM") <= 0) {
					fimc_is_sec_rom_power_on(core, SENSOR_POSITION_REAR);
					is_ldo_enabled = true;
				}

				fimc_is_spi_s_pin(spi, false);
				ret = fimc_is_sec_read_companion_fw(core);
				fimc_is_spi_s_pin(spi, true);
				if (ret < 0) {
					if (!crc32_c1_fw_check) {
						is_dumped_c1_fw_loading_needed = false;
						err("Companion Firmware CRC is not valid. Does not use dumped firmware.\n");
					}
				}

				if (is_ldo_enabled && !specific->running_rear_camera)
					fimc_is_sec_rom_power_off(core, SENSOR_POSITION_REAR);
			}

			if (phone_c1_fw_version[0] == 0) {
				strcpy(sysfs_pinfo.concord_header_ver, "NULL");
			}
		}
#endif

		if (is_dump_needed && is_dumped_c1_fw_loading_needed) {
			strncpy(loaded_companion_fw, sysfs_finfo.concord_header_ver, FIMC_IS_HEADER_VER_SIZE);
		} else if (!is_dump_needed && is_dumped_c1_fw_loading_needed) {
			strncpy(loaded_companion_fw, dump_c1_fw_version, FIMC_IS_HEADER_VER_SIZE);
		} else {
			strncpy(loaded_companion_fw, phone_c1_fw_version, FIMC_IS_HEADER_VER_SIZE);
		}
	} else {
		info("already loaded the firmware, Phone_Comp version=%s, F-ROM_Comp version=%s\n",
			sysfs_pinfo.concord_header_ver, sysfs_finfo.concord_header_ver);
	}

exit:
	mutex_unlock(&specific->spi_lock);

	return ret;
}
#endif
