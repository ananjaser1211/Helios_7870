/*
* Samsung Exynos5 SoC series FIMC-IS driver
 *
 * exynos5 fimc-is vender functions
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <exynos-fimc-is-module.h>
#include "fimc-is-vender.h"
#include "fimc-is-vender-specific.h"
#include "fimc-is-core.h"
#include "fimc-is-sec-define.h"
#include "fimc-is-dt.h"
#include "fimc-is-sysfs.h"

#if defined(CONFIG_OIS_USE)
#include "fimc-is-device-ois.h"
#endif
#include "fimc-is-device-preprocessor.h"

#if defined(CONFIG_LEDS_SM5705)
#include <linux/leds/leds-sm5705.h>
#endif
#if defined(CONFIG_LEDS_RT8547)
#include <linux/leds/leds-rt8547.h>
#endif
#if defined(CONFIG_LEDS_S2MU005_FLASH)
#include <linux/leds-s2mu005.h>
#endif

extern int fimc_is_create_sysfs(struct fimc_is_core *core);
extern bool crc32_check;
extern bool crc32_header_check;
extern bool crc32_check_front;
extern bool crc32_check_rear2;
extern bool crc32_check_rear3;
extern bool crc32_header_check_front;
extern bool crc32_header_check_rear2;
extern bool crc32_header_check_rear3;
extern bool is_dumped_fw_loading_needed;
extern bool force_caldata_dump;

static u32  rear_sensor_id;
static u32  front_sensor_id;
#ifdef CAMERA_REAR2
static u32  rear_second_sensor_id;
#endif
#ifdef CAMERA_REAR3
static u32  rear_third_sensor_id;
#endif
static bool check_sensor_vendor;
static bool skip_cal_loading;
static bool use_ois_hsi2c;
static bool use_ois;
static bool use_module_check;

#ifdef CAMERA_PARALLEL_RETENTION_SEQUENCE
struct workqueue_struct *sensor_pwr_ctrl_wq = 0;
#define CAMERA_WORKQUEUE_MAX_WAITING	1000
#endif

#ifdef CONFIG_SECURE_CAMERA_USE
static u32 secure_sensor_id;
#endif

#ifdef USE_CAMERA_HW_BIG_DATA
static struct cam_hw_param_collector cam_hwparam_collector;
static bool mipi_err_check;
static bool need_update_to_file;

bool fimc_is_sec_need_update_to_file(void)
{
	return need_update_to_file;
}

void fimc_is_sec_init_err_cnt_file(struct cam_hw_param *hw_param)
{
	if (hw_param) {
		memset(hw_param, 0, sizeof(struct cam_hw_param));
		fimc_is_sec_copy_err_cnt_to_file();
	}
}

void fimc_is_sec_copy_err_cnt_to_file(void)
{
	struct file *fp = NULL;
	mm_segment_t old_fs;
	long nwrite = 0;
	bool ret = false;
	int old_mask = 0;

	if (current && current->fs) {
		old_fs = get_fs();
		set_fs(KERNEL_DS);

		ret = sys_access(CAM_HW_ERR_CNT_FILE_PATH, 0);

		if (ret != 0) {
			old_mask = sys_umask(7);
			fp = filp_open(CAM_HW_ERR_CNT_FILE_PATH, O_WRONLY | O_CREAT | O_TRUNC | O_SYNC, 0660);
			if (IS_ERR_OR_NULL(fp)) {
				warn("%s open failed", CAM_HW_ERR_CNT_FILE_PATH);
				sys_umask(old_mask);
				set_fs(old_fs);
				return;
			}

			filp_close(fp, current->files);
			sys_umask(old_mask);
		}

		fp = filp_open(CAM_HW_ERR_CNT_FILE_PATH, O_WRONLY | O_TRUNC | O_SYNC, 0660);
		if (IS_ERR_OR_NULL(fp)) {
			warn("%s open failed", CAM_HW_ERR_CNT_FILE_PATH);
			set_fs(old_fs);
			return;
		}

		nwrite = vfs_write(fp, (char *)&cam_hwparam_collector, sizeof(struct cam_hw_param_collector), &fp->f_pos);

		filp_close(fp, current->files);
		set_fs(old_fs);
		need_update_to_file = false;
	}
}

void fimc_is_sec_copy_err_cnt_from_file(void)
{
	struct file *fp = NULL;
	mm_segment_t old_fs;
	long nread = 0;
	bool ret = false;

	ret = fimc_is_sec_file_exist(CAM_HW_ERR_CNT_FILE_PATH);

	if (ret) {
		old_fs = get_fs();
		set_fs(KERNEL_DS);

		fp = filp_open(CAM_HW_ERR_CNT_FILE_PATH, O_RDONLY, 0660);
		if (IS_ERR_OR_NULL(fp)) {
			warn("%s open failed", CAM_HW_ERR_CNT_FILE_PATH);
			set_fs(old_fs);
			return;
		}

		nread = vfs_read(fp, (char *)&cam_hwparam_collector, sizeof(struct cam_hw_param_collector), &fp->f_pos);

		filp_close(fp, current->files);
		set_fs(old_fs);
	}
}

void fimc_is_sec_get_hw_param(struct cam_hw_param **hw_param, u32 position)
{
	switch (position) {
		case SENSOR_POSITION_REAR:
			*hw_param = &cam_hwparam_collector.rear_hwparam;
			break;
		case SENSOR_POSITION_REAR2:
			*hw_param = &cam_hwparam_collector.rear2_hwparam;
			break;
		case SENSOR_POSITION_REAR3:
			*hw_param = &cam_hwparam_collector.rear3_hwparam;
			break;
		case SENSOR_POSITION_FRONT:
			*hw_param = &cam_hwparam_collector.front_hwparam;
			break;
		case SENSOR_POSITION_SECURE:
			*hw_param = &cam_hwparam_collector.iris_hwparam;
			break;
		default:
			need_update_to_file = false;
			return;
	}
	need_update_to_file = true;
}

int fimc_is_sec_get_rear_hw_param(struct cam_hw_param **hw_param)
{
	*hw_param = &cam_hwparam_collector.rear_hwparam;
	need_update_to_file = true;
	return 0;
}

int fimc_is_sec_get_front_hw_param(struct cam_hw_param **hw_param)
{
	*hw_param = &cam_hwparam_collector.front_hwparam;
	need_update_to_file = true;
	return 0;
}

int fimc_is_sec_get_iris_hw_param(struct cam_hw_param **hw_param)
{
	*hw_param = &cam_hwparam_collector.iris_hwparam;
	need_update_to_file = true;
	return 0;
}

int fimc_is_sec_get_rear2_hw_param(struct cam_hw_param **hw_param)
{
	*hw_param = &cam_hwparam_collector.rear2_hwparam;
	need_update_to_file = true;
	return 0;
}

int fimc_is_sec_get_rear3_hw_param(struct cam_hw_param **hw_param)
{
	*hw_param = &cam_hwparam_collector.rear3_hwparam;
	need_update_to_file = true;
	return 0;
}

bool fimc_is_sec_is_valid_moduleid(char* moduleid)
{
	int i = 0;

	if (moduleid == NULL || strlen(moduleid) < 5) {
		err("%s:Camera : moduleid is null or invalid.\n", __func__);
		return false;
	}

	for (i = 0; i < 5; i++)
	{
		if (!((moduleid[i] > 47 && moduleid[i] < 58) || // 0 to 9
			(moduleid[i] > 64 && moduleid[i] < 91))) {  // A to Z
			goto err;
		}
	}

	return true;

err:
	warn("[fimc_is_sec_is_valid_moduleid] invalid moduleid %c%c%c%c%cXX%02X%02X%02X\n",
		moduleid[0], moduleid[1], moduleid[2],
		moduleid[3], moduleid[4], moduleid[7],
		moduleid[8], moduleid[9]);
	return false;
}
#endif

void fimc_is_vendor_csi_stream_on(struct fimc_is_device_csi *csi)
{
#ifdef USE_CAMERA_HW_BIG_DATA
	mipi_err_check = false;
#endif
}

void fimc_is_vender_csi_err_handler(struct fimc_is_device_csi *csi)
{
#ifdef USE_CAMERA_HW_BIG_DATA
	struct fimc_is_device_sensor *device = NULL;
	struct cam_hw_param *hw_param = NULL;

	device = container_of(csi->subdev, struct fimc_is_device_sensor, subdev_csi);

	if (device && device->pdev && !mipi_err_check) {
		switch (device->pdev->id) {
			case CSI_SCENARIO_SEN_REAR:
				fimc_is_sec_get_rear_hw_param(&hw_param);

				if (hw_param)
					hw_param->mipi_sensor_err_cnt++;
#if 0	// TODO: TEMP_M10
			case CSI_SCENARIO_SEN_REAR2:
				fimc_is_sec_get_rear2_hw_param(&hw_param);

				if (hw_param)
					hw_param->mipi_sensor_err_cnt++;
#endif
#if 0	// TODO: TEMP_M10
			case CSI_SCENARIO_SEN_REAR3:
				fimc_is_sec_get_rear3_hw_param(&hw_param);

				if (hw_param)
					hw_param->mipi_sensor_err_cnt++;
#endif
			case CSI_SCENARIO_SEN_FRONT:
				fimc_is_sec_get_front_hw_param(&hw_param);

				if (hw_param)
					hw_param->mipi_sensor_err_cnt++;
				break;
			default:
				break;
		}
		mipi_err_check = true;
	}
#endif
}

int fimc_is_vender_probe(struct fimc_is_vender *vender)
{
	int ret = 0;
	int i = 0;
	struct fimc_is_core *core;
	struct fimc_is_vender_specific *specific;

	BUG_ON(!vender);

	core = container_of(vender, struct fimc_is_core, vender);
	snprintf(vender->fw_path, sizeof(vender->fw_path), "%s", FIMC_IS_FW_SDCARD);
	snprintf(vender->request_fw_path, sizeof(vender->request_fw_path), "%s", FIMC_IS_FW);

	specific = (struct fimc_is_vender_specific *)kmalloc(sizeof(struct fimc_is_vender_specific), GFP_KERNEL);

	/* init mutex for spi read */
	mutex_init(&specific->spi_lock);
	specific->running_front_camera = false;
	specific->running_rear_camera = false;
	specific->running_rear2_camera = false;
	specific->running_rear3_camera = false;

	specific->retention_data.firmware_size = 0;
	memset(&specific->retention_data.firmware_crc32, 0, FIMC_IS_COMPANION_CRC_SIZE);

#ifdef CONFIG_PREPROCESSOR_STANDBY_USE
	SET_SENSOR_STATE(specific->standby_state, SENSOR_STATE_COMPANION, SENSOR_STATE_OFF);
	info("%s: COMPANION STATE %u\n", __func__, GET_SENSOR_STATE(specific->standby_state, SENSOR_STATE_COMPANION));
#endif

#ifdef CONFIG_COMPANION_DCDC_USE
	/* Init companion dcdc */
	comp_pmic_init(&specific->companion_dcdc, NULL);
#endif

	if (fimc_is_create_sysfs(core)) {
		probe_err("fimc_is_create_sysfs is failed");
		ret = -EINVAL;
		goto p_err;
	}

	specific->rear_sensor_id = rear_sensor_id;
	specific->front_sensor_id = front_sensor_id;
#ifdef CAMERA_REAR2
	specific->rear_second_sensor_id = rear_second_sensor_id;
#endif /* CAMERA_REAR2 */
#ifdef CAMERA_REAR3
	specific->rear_third_sensor_id = rear_third_sensor_id;
#endif /* CAMERA_REAR3 */
	specific->check_sensor_vendor = check_sensor_vendor;
	specific->use_ois = use_ois;
	specific->use_ois_hsi2c = use_ois_hsi2c;
	specific->use_module_check = use_module_check;
	specific->skip_cal_loading = skip_cal_loading;

	for (i = 0; i < SENSOR_POSITION_END; i++) {
		specific->eeprom_client[i] = NULL;
	}

	specific->suspend_resume_disable = false;
	specific->need_cold_reset = false;
#ifdef CONFIG_SECURE_CAMERA_USE
	specific->secure_sensor_id = secure_sensor_id;
#endif

	vender->private_data = specific;

#ifdef CAMERA_PARALLEL_RETENTION_SEQUENCE
	if (!sensor_pwr_ctrl_wq) {
		sensor_pwr_ctrl_wq = create_singlethread_workqueue("sensor_pwr_ctrl");
	}
#endif

p_err:
	return ret;
}

#ifdef CAMERA_SYSFS_V2
static int parse_sysfs_caminfo(struct device_node *np,
				struct fimc_is_cam_info *cam_infos, int camera_num)
{
	u32 temp;
	char *pprop;

	DT_READ_U32(np, "isp", cam_infos[camera_num].isp);
	DT_READ_U32(np, "cal_memory", cam_infos[camera_num].cal_memory);
	DT_READ_U32(np, "read_version", cam_infos[camera_num].read_version);
	DT_READ_U32(np, "core_voltage", cam_infos[camera_num].core_voltage);
	DT_READ_U32(np, "upgrade", cam_infos[camera_num].upgrade);
	DT_READ_U32(np, "fw_write", cam_infos[camera_num].fw_write);
	DT_READ_U32(np, "fw_dump", cam_infos[camera_num].fw_dump);
	DT_READ_U32(np, "companion", cam_infos[camera_num].companion);
	DT_READ_U32(np, "ois", cam_infos[camera_num].ois);
	DT_READ_U32(np, "valid", cam_infos[camera_num].valid);
	DT_READ_U32(np, "dual_open", cam_infos[camera_num].dual_open);
	DT_READ_U32(np, "dual_cam", cam_infos[camera_num].dual_cam);

	return 0;
}
#endif

int fimc_is_vender_dt(struct device_node *np)
{
	int ret = 0;
#ifdef CAMERA_SYSFS_V2
	struct device_node *camInfo_np;
	struct fimc_is_cam_info *camera_infos;
	struct fimc_is_common_cam_info *common_camera_infos = NULL;
	char camInfo_string[15];
	int camera_num;
	int total_camera_num;
#endif

	ret = of_property_read_u32(np, "rear_sensor_id", &rear_sensor_id);
	if (ret) {
		probe_err("rear_sensor_id read is fail(%d)", ret);
	}

	ret = of_property_read_u32(np, "front_sensor_id", &front_sensor_id);
	if (ret) {
		probe_err("front_sensor_id read is fail(%d)", ret);
	}

#ifdef CAMERA_REAR2
	ret = of_property_read_u32(np, "rear_second_sensor_id", &rear_second_sensor_id);
	if (ret) {
		probe_err("rear_second_sensor_id read is fail(%d)", ret);
	}
#endif /* CAMERA_REAR2 */
#ifdef CAMERA_REAR3
	ret = of_property_read_u32(np, "rear_third_sensor_id", &rear_third_sensor_id);
	if (ret) {
		probe_err("rear_third_sensor_id read is fail(%d)", ret);
	}
#endif /* CAMERA_REAR3 */

#ifdef CONFIG_SECURE_CAMERA_USE
	ret = of_property_read_u32(np, "secure_sensor_id", &secure_sensor_id);
	if (ret) {
		probe_err("secure_sensor_id read is fail(%d)", ret);
		secure_sensor_id = 0;
	}
#endif

	check_sensor_vendor = of_property_read_bool(np, "check_sensor_vendor");
	if (!check_sensor_vendor) {
		probe_info("check_sensor_vendor not use(%d)\n", check_sensor_vendor);
	}

	use_ois = of_property_read_bool(np, "use_ois");
	if (!use_ois) {
		probe_err("use_ois not use(%d)", use_ois);
	}

	use_ois_hsi2c = of_property_read_bool(np, "use_ois_hsi2c");
	if (!use_ois_hsi2c) {
		probe_err("use_ois_hsi2c not use(%d)", use_ois_hsi2c);
	}

	use_module_check = of_property_read_bool(np, "use_module_check");
	if (!use_module_check) {
		probe_err("use_module_check not use(%d)", use_module_check);
	}

	skip_cal_loading = of_property_read_bool(np, "skip_cal_loading");
	if (!skip_cal_loading) {
		probe_info("skip_cal_loading not use(%d)\n", skip_cal_loading);
	}

#ifdef CAMERA_SYSFS_V2
	ret = of_property_read_u32(np, "total_camera_num", &total_camera_num);
	if (ret) {
		err("total_camera_num read is fail(%d)", ret);
		total_camera_num = 0;
	}
	fimc_is_get_cam_info(&camera_infos);

	for (camera_num = 0; camera_num < total_camera_num; camera_num++) {
		sprintf(camInfo_string, "%s%d", "camera_info", camera_num);

		camInfo_np = of_find_node_by_name(np, camInfo_string);
		if (!camInfo_np) {
			printk(KERN_ERR "%s: can't find camInfo_string node\n", __func__);
			return -ENOENT;
		}
		parse_sysfs_caminfo(camInfo_np, camera_infos, camera_num);
	}

	fimc_is_get_common_cam_info(&common_camera_infos);

	ret = of_property_read_u32(np, "max_supported_camera", &common_camera_infos->max_supported_camera);
	if (ret) {
		probe_err("supported_cameraId read is fail(%d)", ret);
	}

	ret = of_property_read_u32_array(np, "supported_cameraId",
		common_camera_infos->supported_camera_ids, common_camera_infos->max_supported_camera);
	if (ret) {
		probe_err("supported_cameraId read is fail(%d)", ret);
	}
#endif

	return ret;
}

int fimc_is_vender_fw_prepare(struct fimc_is_vender *vender)
{
	int ret = 0;
	struct fimc_is_core *core;
	struct fimc_is_device_preproc *device;
	struct fimc_is_vender_specific *specific;

	BUG_ON(!vender);

#ifdef USE_CAMERA_HW_BIG_DATA
	need_update_to_file = false;
	fimc_is_sec_copy_err_cnt_from_file();
#endif

	specific = vender->private_data;
	core = container_of(vender, struct fimc_is_core, vender);
	device = &core->preproc;

	ret = fimc_is_sec_run_fw_sel(&device->pdev->dev, SENSOR_POSITION_REAR);
	if (core->current_position == SENSOR_POSITION_REAR) {
		if (ret < 0) {
			err("fimc_is_sec_run_fw_sel is fail1(%d)", ret);
			goto p_err;
		}
	}
#if defined (CAMERA_REAR2)
	ret = fimc_is_sec_run_fw_sel(&device->pdev->dev, SENSOR_POSITION_REAR2);
	if (core->current_position == SENSOR_POSITION_REAR2) {
		if (ret < 0) {
			err("fimc_is_sec_run_fw_sel is fail1(%d)", ret);
			goto p_err;
		}
	}
#endif
#if defined (CAMERA_REAR3)
	ret = fimc_is_sec_run_fw_sel(&device->pdev->dev, SENSOR_POSITION_REAR3);
	if (core->current_position == SENSOR_POSITION_REAR3) {
		if (ret < 0) {
			err("fimc_is_sec_run_fw_sel is fail1(%d)", ret);
			goto p_err;
		}
	}
#endif

	ret = fimc_is_sec_run_fw_sel(&device->pdev->dev, SENSOR_POSITION_FRONT);
	if (core->current_position == SENSOR_POSITION_FRONT) {
		if (ret < 0) {
			err("fimc_is_sec_run_fw_sel is fail2(%d)", ret);
			goto p_err;
		}
	}

#ifdef CONFIG_COMPANION_USE
	ret = fimc_is_sec_concord_fw_sel(core, &device->pdev->dev);
	if (ret) {
		err("fimc_is_sec_concord_fw_sel is fail(%d)", ret);
		goto p_err;
	}

	/* TODO: loading firmware */
	fimc_is_s_int_comb_isp(core, false, INTMR2_INTMCIS22);
#endif

p_err:
	return ret;
}

int fimc_is_vender_fw_filp_open(struct fimc_is_vender *vender, struct file **fp, int bin_type)
{
	int ret = FW_SKIP;
	struct fimc_is_from_info *sysfs_finfo;
	char fw_path[FIMC_IS_PATH_LEN];

	fimc_is_sec_get_sysfs_finfo(&sysfs_finfo);
	memset(fw_path, 0x00, sizeof(fw_path));

	if (bin_type == FIMC_IS_BIN_FW) {
		if (is_dumped_fw_loading_needed) {
			snprintf(fw_path, sizeof(fw_path),
					"%s%s", FIMC_IS_FW_DUMP_PATH, sysfs_finfo->load_fw_name);
			*fp = filp_open(fw_path, O_RDONLY, 0);
			if (IS_ERR_OR_NULL(*fp)) {
				*fp = NULL;
				ret = FW_FAIL;
			} else {
				ret = FW_SUCCESS;
			}
		} else {
			ret = FW_SKIP;
		}
	} else if (bin_type == FIMC_IS_BIN_SETFILE) {
		if (is_dumped_fw_loading_needed) {
			snprintf(fw_path, sizeof(fw_path),
					"%s%s", FIMC_IS_FW_DUMP_PATH, sysfs_finfo->load_setfile_name);
			*fp = filp_open(fw_path, O_RDONLY, 0);
			if (IS_ERR_OR_NULL(*fp)) {
				*fp = NULL;
				ret = FW_FAIL;
			} else {
				ret = FW_SUCCESS;
			}
		} else {
			ret = FW_SKIP;
		}
	}

	return ret;
}

int fimc_is_vender_preproc_fw_load(struct fimc_is_vender *vender)
{
	int ret = 0;
	struct fimc_is_core *core;
	struct fimc_is_device_preproc *device;
	struct fimc_is_vender_specific *specific;
#if defined(CONFIG_OIS_USE)
	struct fimc_is_device_ois *ois_device;
#endif
#if defined(CONFIG_OIS_USE) || defined(CONFIG_COMPANION_USE)
	struct device *i2c_dev = NULL;
	struct pinctrl *pinctrl_i2c = NULL;
#endif

	BUG_ON(!vender);

	specific = vender->private_data;
	core = container_of(vender, struct fimc_is_core, vender);
	device = &core->preproc;

#ifdef CONFIG_COMPANION_USE
	/* Set pin function to ISP I2C for Host to use I2C0 */
	i2c_dev = core->client0->dev.parent->parent;
	pinctrl_i2c = devm_pinctrl_get_select(i2c_dev, "i2c_host");
	if (IS_ERR_OR_NULL(pinctrl_i2c)) {
		printk(KERN_ERR "%s: Failed to configure i2c pin\n", __func__);
	} else {
		devm_pinctrl_put(pinctrl_i2c);
	}

	fimc_is_spi_s_pin(&core->spi0, false);
	fimc_is_spi_s_pin(&core->spi1, false);

	if (fimc_is_comp_is_valid(core) == 0) {
#if defined(CONFIG_PREPROCESSOR_STANDBY_USE) && !defined(CONFIG_RELOAD_CAL_DATA)
		if (GET_SENSOR_STATE(specific->standby_state, SENSOR_STATE_COMPANION) == SENSOR_STATE_OFF) {
			ret = fimc_is_comp_loadfirm(core);
		} else {
			ret = fimc_is_comp_retention(core);
			if (ret == -EINVAL) {
				info("companion restart..\n");
				ret = fimc_is_comp_loadfirm(core);
			}
		}
		SET_SENSOR_STATE(specific->standby_state, SENSOR_STATE_COMPANION, SENSOR_STATE_ON);
		info("%s: COMPANION STATE %u\n", __func__, GET_SENSOR_STATE(specific->standby_state, SENSOR_STATE_COMPANION));
#else
		ret = fimc_is_comp_loadfirm(core);
#endif
		if (ret) {
			err("fimc_is_comp_loadfirm() fail");
			goto p_err;
		}

#ifdef CONFIG_COMPANION_DCDC_USE
#if defined(CONFIG_COMPANION_C2_USE) || defined(CONFIG_COMPANION_C3_USE)
		if (fimc_is_comp_get_ver() == FIMC_IS_COMPANION_VERSION_EVT1)
#endif
		{
			if (specific->companion_dcdc.type == DCDC_VENDOR_PMIC) {
				if (!fimc_is_sec_file_exist(FIMC_IS_ISP_CV))
					fimc_is_power_binning(core);
			} else {
				fimc_is_power_binning(core);
			}
		}
#endif /* CONFIG_COMPANION_DCDC_USE*/

		ret = fimc_is_comp_loadsetf(core);
		if (ret) {
			err("fimc_is_comp_loadsetf() fail");
			goto p_err;
		}
	}

	fimc_is_spi_s_pin(&core->spi0, true);
	fimc_is_spi_s_pin(&core->spi1, true);

	pinctrl_i2c = devm_pinctrl_get_select(i2c_dev, "i2c_fw");
	if (IS_ERR_OR_NULL(pinctrl_i2c)) {
		printk(KERN_ERR "%s: Failed to configure i2c pin\n", __func__);
	} else {
		devm_pinctrl_put(pinctrl_i2c);
	}

p_err:
#endif

#if defined(CONFIG_OIS_USE)
	ois_device  = (struct fimc_is_device_ois *)i2c_get_clientdata(core->client1);

	if(specific->use_ois && core->current_position == SENSOR_POSITION_REAR) {
		if (!specific->use_ois_hsi2c) {
			i2c_dev = ois_device->client->dev.parent->parent;
			pinctrl_i2c = devm_pinctrl_get_select(i2c_dev, "i2c_host");
			if (IS_ERR_OR_NULL(pinctrl_i2c)) {
				printk(KERN_ERR "%s: Failed to configure i2c pin\n", __func__);
			} else {
				devm_pinctrl_put(pinctrl_i2c);
			}
		}

		if (!specific->ois_ver_read) {
			fimc_is_ois_check_fw(core);
		}

		fimc_is_ois_exif_data(core);

		if (!specific->use_ois_hsi2c) {
			i2c_dev = ois_device->client->dev.parent->parent;
			pinctrl_i2c = devm_pinctrl_get_select(i2c_dev, "i2c_fw");
			if (IS_ERR_OR_NULL(pinctrl_i2c)) {
				printk(KERN_ERR "%s: Failed to configure i2c pin\n", __func__);
			} else {
				devm_pinctrl_put(pinctrl_i2c);
			}
		}
	}
#endif

	return ret;
}

#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR) || defined(CONFIG_CAMERA_EEPROM_SUPPORT_FRONT) || defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR2) || defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR3)\
	|| defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_FRONT)|| defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR2) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR3)
static int fimc_is_ischain_loadcalb_eeprom(struct fimc_is_core *core,
struct fimc_is_module_enum *active_sensor, int position)
{
	int ret = 0;
	char *cal_ptr;
	char *cal_buf = NULL;
	u32 start_addr = 0;
	int cal_size = 0;
	struct fimc_is_from_info *finfo;
	struct fimc_is_from_info *pinfo;
	char *loaded_fw_ver;

	info("%s\n", __func__);

	if (!force_caldata_dump && !fimc_is_sec_check_from_ver(core, position)) {
		err("Camera : Did not load cal data.");
		return 0;
	}

#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_FRONT) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_FRONT)
	if (position == SENSOR_POSITION_FRONT) {
		start_addr = CAL_OFFSET1;
		cal_size = FIMC_IS_MAX_CAL_SIZE_FRONT;
		fimc_is_sec_get_sysfs_finfo_front(&finfo);
		fimc_is_sec_get_front_cal_buf(&cal_buf);
	}
#endif

#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR)
	if (position == SENSOR_POSITION_REAR){
		start_addr = CAL_OFFSET0;
		cal_size = FIMC_IS_MAX_CAL_SIZE;
		fimc_is_sec_get_sysfs_finfo(&finfo);
		fimc_is_sec_get_cal_buf(&cal_buf);
	}
#endif

#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR2) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR2)
	if (position == SENSOR_POSITION_REAR2) {
		start_addr = CAL_OFFSET0;
		cal_size = FIMC_IS_MAX_CAL_SIZE_REAR2;
		fimc_is_sec_get_sysfs_finfo_rear2(&finfo);
		fimc_is_sec_get_rear2_cal_buf(&cal_buf);
	}
#endif
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR3) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR3)
	if (position == SENSOR_POSITION_REAR3) {
		start_addr = CAL_OFFSET0;
		cal_size = FIMC_IS_MAX_CAL_SIZE_REAR3;
		fimc_is_sec_get_sysfs_finfo_rear3(&finfo);
		fimc_is_sec_get_rear3_cal_buf(&cal_buf);
	}
#endif

	fimc_is_sec_get_sysfs_pinfo(&pinfo);
	fimc_is_sec_get_loaded_fw(&loaded_fw_ver);

#ifdef ENABLE_IS_CORE
	cal_ptr = (char *)(core->resourcemgr.minfo.kvaddr + start_addr);
#else
	if (position == SENSOR_POSITION_FRONT) {
		cal_ptr = (char *)(core->resourcemgr.minfo.kvaddr_front_cal + start_addr);
	} else if (position == SENSOR_POSITION_REAR2) {
		cal_ptr = (char *)(core->resourcemgr.minfo.kvaddr_rear2_cal + start_addr);
	} else if (position == SENSOR_POSITION_REAR3) {
		cal_ptr = (char *)(core->resourcemgr.minfo.kvaddr_rear3_cal + start_addr);
	} else {
		cal_ptr = (char *)(core->resourcemgr.minfo.kvaddr_rear_cal + start_addr);
	}
#endif

	if (position == SENSOR_POSITION_REAR || position == SENSOR_POSITION_FRONT)
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR) || defined(CONFIG_CAMERA_EEPROM_SUPPORT_FRONT)
	info("CAL DATA : MAP ver : %c%c%c%c\n",
			cal_buf[EEP_HEADER_CAL_MAP_VER_START_ADDR],
			cal_buf[EEP_HEADER_CAL_MAP_VER_START_ADDR+1],
			cal_buf[EEP_HEADER_CAL_MAP_VER_START_ADDR+2],
			cal_buf[EEP_HEADER_CAL_MAP_VER_START_ADDR+3]);
#else
	info("CAL DATA : MAP ver : %c%c%c%c\n",
			cal_buf[OTP_HEADER_CAL_MAP_VER_START_ADDR],
			cal_buf[OTP_HEADER_CAL_MAP_VER_START_ADDR+1],
			cal_buf[OTP_HEADER_CAL_MAP_VER_START_ADDR+2],
			cal_buf[OTP_HEADER_CAL_MAP_VER_START_ADDR+3]);
#endif
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR2)
	if (position == SENSOR_POSITION_REAR2)
	info("CAL DATA : MAP ver : %c%c%c%c\n",
			cal_buf[EEP_HEADER_CAL_MAP_VER_START_ADDR_REAR2],
			cal_buf[EEP_HEADER_CAL_MAP_VER_START_ADDR_REAR2 +1],
			cal_buf[EEP_HEADER_CAL_MAP_VER_START_ADDR_REAR2 +2],
			cal_buf[EEP_HEADER_CAL_MAP_VER_START_ADDR_REAR2 +3]);
#endif
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR3)
	if (position == SENSOR_POSITION_REAR3)
	info("CAL DATA : MAP ver : %c%c%c%c\n",
			cal_buf[EEP_HEADER_CAL_MAP_VER_START_ADDR_REAR3],
			cal_buf[EEP_HEADER_CAL_MAP_VER_START_ADDR_REAR3 +1],
			cal_buf[EEP_HEADER_CAL_MAP_VER_START_ADDR_REAR3 +2],
			cal_buf[EEP_HEADER_CAL_MAP_VER_START_ADDR_REAR3 +3]);
#endif

	if (!cal_buf) {
		err("%s:Camera : cal_buf is null.\n", __func__);
		return 0;
	}

	info("Camera : Sensor Version : 0x%x\n", cal_buf[0x5C]);

	info("eeprom_fw_version = %s, phone_fw_version = %s, loaded_fw_version = %s\n",
		finfo->header_ver, pinfo->header_ver, loaded_fw_ver);

	/* CRC check */
	if (position == SENSOR_POSITION_FRONT) {
		if (crc32_check_front == true) {
			memcpy((void *)(cal_ptr) ,(void *)cal_buf, cal_size);
			info("Front Camera : the dumped Cal. data was applied successfully.\n");
		} else {
			if (crc32_header_check_front == true) {
				err("Front Camera : CRC32 error but only header section is no problem.");
				memset((void *)(cal_ptr + 0x1000), 0xFF, cal_size - 0x1000);
				ret = -EIO;
			} else {
				err("Front Camera : CRC32 error for all section.");
				memset((void *)(cal_ptr), 0xFF, cal_size);
				ret = -EIO;
			}
		}
	} else if (position == SENSOR_POSITION_REAR2) {
		if (crc32_check_rear2 == true) {
			memcpy((void *)(cal_ptr) ,(void *)cal_buf, cal_size);
			info("Rear2 Camera : the dumped Cal. data was applied successfully.\n");
		} else {
			if (crc32_header_check_rear2 == true) {
				err("Rear2 Camera : CRC32 error but only header section is no problem.");
				memset((void *)(cal_ptr + 0x1000), 0xFF, cal_size - 0x1000);
				ret = -EIO;
			} else {
				err("Rear2 Camera : CRC32 error for all section.");
				memset((void *)(cal_ptr), 0xFF, cal_size);
				ret = -EIO;
			}
		}
	} else if (position == SENSOR_POSITION_REAR3) {
		if (crc32_check_rear3 == true) {
			memcpy((void *)(cal_ptr) ,(void *)cal_buf, cal_size);
			info("Rear3 Camera : the dumped Cal. data was applied successfully.\n");
		} else {
			if (crc32_header_check_rear3 == true) {
				err("Rear3 Camera : CRC32 error but only header section is no problem.");
				memset((void *)(cal_ptr + 0x1000), 0xFF, cal_size - 0x1000);
				ret = -EIO;
			} else {
				err("Rear3 Camera : CRC32 error for all section.");
				memset((void *)(cal_ptr), 0xFF, cal_size);
				ret = -EIO;
			}
		}
	} else {
		if (crc32_check == true) {
			memcpy((void *)(cal_ptr) ,(void *)cal_buf, cal_size);
			info("Rear Camera : the dumped Cal. data was applied successfully.\n");
		} else {
			if (crc32_header_check == true) {
				err("Rear Camera : CRC32 error but only header section is no problem.");
				memset((void *)(cal_ptr + 0x1000), 0xFF, cal_size - 0x1000);
				ret = -EIO;
			} else {
				err("Rear Camera : CRC32 error for all section.");
				memset((void *)(cal_ptr), 0xFF, cal_size);
				ret = -EIO;
			}
		}
	}

#ifdef ENABLE_IS_CORE
	CALL_BUFOP(core->resourcemgr.minfo.pb_fw, sync_for_device,
		core->resourcemgr.minfo.pb_fw,
		start_addr, cal_size, DMA_TO_DEVICE);
#endif

	if (ret)
		warn("calibration loading is fail");
	else
		info("calibration loading is success\n");

	return ret;
}
#endif

#if !defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR) && !defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR)
static int fimc_is_ischain_loadcalb(struct fimc_is_core *core,
	struct fimc_is_module_enum *active_sensor, int position)
{
	int ret = 0;
	char *cal_ptr;
	u32 start_addr = 0;
	int cal_size = 0;
	struct fimc_is_from_info *sysfs_finfo;
	struct fimc_is_from_info *sysfs_pinfo;
	char *loaded_fw_ver;
	char *cal_buf;

	if (!fimc_is_sec_check_from_ver(core, position)) {
		err("Camera : Did not load cal data.");
		return 0;
	}

	if (position == SENSOR_POSITION_FRONT) {
		start_addr = CAL_OFFSET1;
		cal_size = FIMC_IS_MAX_CAL_SIZE_FRONT;
		fimc_is_sec_get_sysfs_finfo_front(&sysfs_finfo);
		fimc_is_sec_get_front_cal_buf(&cal_buf);
	} else if (position == SENSOR_POSITION_REAR2) {
		start_addr = CAL_OFFSET0;
		cal_size = FIMC_IS_MAX_CAL_SIZE_REAR2;
		fimc_is_sec_get_sysfs_finfo_rear2(&finfo);
		fimc_is_sec_get_rear2_cal_buf(&cal_buf);
	} else if (position == SENSOR_POSITION_REAR3) {
		start_addr = CAL_OFFSET0;
		cal_size = FIMC_IS_MAX_CAL_SIZE_REAR3;
		fimc_is_sec_get_sysfs_finfo_rear3(&finfo);
		fimc_is_sec_get_rear3_cal_buf(&cal_buf);
	}else {
		start_addr = CAL_OFFSET0;
		cal_size = FIMC_IS_MAX_CAL_SIZE;
		fimc_is_sec_get_sysfs_finfo(&sysfs_finfo);
		fimc_is_sec_get_cal_buf(&cal_buf);
	}

	fimc_is_sec_get_sysfs_pinfo(&sysfs_pinfo);
	fimc_is_sec_get_loaded_fw(&loaded_fw_ver);

	cal_ptr = (char *)(core->resourcemgr.minfo.kvaddr + start_addr);

	info("CAL DATA : MAP ver : %c%c%c%c\n", cal_buf[0x60], cal_buf[0x61],
		cal_buf[0x62], cal_buf[0x63]);

	info("from_fw_version = %s, phone_fw_version = %s, loaded_fw_version = %s\n",
		sysfs_finfo->header_ver, sysfs_pinfo->header_ver, loaded_fw_ver);

	/* CRC check */
	if (position == SENSOR_POSITION_FRONT) {
		if (crc32_check_front  == true) {
#ifdef CONFIG_COMPANION_USE
			if (fimc_is_sec_check_from_ver(core, position)) {
				memcpy((void *)(cal_ptr) ,(void *)cal_buf, cal_size);
				info("Camera : the dumped Cal. data was applied successfully.\n");
			} else {
				info("Camera : Did not load dumped Cal. Sensor version is lower than V004.\n");
			}
#else
			memcpy((void *)(cal_ptr) ,(void *)cal_buf, cal_size);
			info("Camera : the dumped Cal. data was applied successfully.\n");
#endif
		} else {
			if (crc32_header_check_front  == true) {
				err("Camera : CRC32 error but only header section is no problem.");
				memcpy((void *)(cal_ptr),
					(void *)cal_buf,
					EEP_HEADER_CAL_DATA_START_ADDR);
				memset((void *)(cal_ptr + EEP_HEADER_CAL_DATA_START_ADDR),
					0xFF,
					cal_size - EEP_HEADER_CAL_DATA_START_ADDR);
			} else {
				err("Camera : CRC32 error for all section.");
				memset((void *)(cal_ptr), 0xFF, cal_size);
				ret = -EIO;
			}
		}
	} else if (position == SENSOR_POSITION_REAR2) {
		if (crc32_check_rear2  == true) {
#ifdef CONFIG_COMPANION_USE
			if (fimc_is_sec_check_from_ver(core, position)) {
				memcpy((void *)(cal_ptr) ,(void *)cal_buf, cal_size);
				info("Camera : the dumped Cal. data was applied successfully.\n");
			} else {
				info("Camera : Did not load dumped Cal. Sensor version is lower than V004.\n");
			}
#else
			memcpy((void *)(cal_ptr) ,(void *)cal_buf, cal_size);
			info("Camera : the dumped Cal. data was applied successfully.\n");
#endif
		} else {
			if (crc32_header_check_rear2  == true) {
				err("Camera : CRC32 error but only header section is no problem.");
				memcpy((void *)(cal_ptr),
					(void *)cal_buf,
					EEP_HEADER_CAL_DATA_START_ADDR);
				memset((void *)(cal_ptr + EEP_HEADER_CAL_DATA_START_ADDR),
					0xFF,
					cal_size - EEP_HEADER_CAL_DATA_START_ADDR);
			} else {
				err("Camera : CRC32 error for all section.");
				memset((void *)(cal_ptr), 0xFF, cal_size);
				ret = -EIO;
			}
		}
	} else if (position == SENSOR_POSITION_REAR3) {
		if (crc32_check_rear3  == true) {
#ifdef CONFIG_COMPANION_USE
			if (fimc_is_sec_check_from_ver(core, position)) {
				memcpy((void *)(cal_ptr) ,(void *)cal_buf, cal_size);
				info("Camera : the dumped Cal. data was applied successfully.\n");
			} else {
				info("Camera : Did not load dumped Cal. Sensor version is lower than V004.\n");
			}
#else
			memcpy((void *)(cal_ptr) ,(void *)cal_buf, cal_size);
			info("Camera : the dumped Cal. data was applied successfully.\n");
#endif
		} else {
			if (crc32_header_check_rear3  == true) {
				err("Camera : CRC32 error but only header section is no problem.");
				memcpy((void *)(cal_ptr),
					(void *)cal_buf,
					EEP_HEADER_CAL_DATA_START_ADDR);
				memset((void *)(cal_ptr + EEP_HEADER_CAL_DATA_START_ADDR),
					0xFF,
					cal_size - EEP_HEADER_CAL_DATA_START_ADDR);
			} else {
				err("Camera : CRC32 error for all section.");
				memset((void *)(cal_ptr), 0xFF, cal_size);
				ret = -EIO;
			}
		}
	} else {
		if (crc32_check == true) {
#ifdef CONFIG_COMPANION_USE
			if (fimc_is_sec_check_from_ver(core, position)) {
				memcpy((void *)(cal_ptr) ,(void *)cal_buf, cal_size);
				info("Camera : the dumped Cal. data was applied successfully.\n");
			} else {
				info("Camera : Did not load dumped Cal. Sensor version is lower than V004.\n");
			}
#else
			memcpy((void *)(cal_ptr) ,(void *)cal_buf, cal_size);
			info("Camera : the dumped Cal. data was applied successfully.\n");
#endif
		} else {
			if (crc32_header_check == true) {
				err("Camera : CRC32 error but only header section is no problem.");
				memcpy((void *)(cal_ptr),
					(void *)cal_buf,
					FROM_HEADER_CAL_DATA_START_ADDR);
				memset((void *)(cal_ptr + FROM_HEADER_CAL_DATA_START_ADDR),
					0xFF,
					cal_size - FROM_HEADER_CAL_DATA_START_ADDR);
			} else {
				err("Camera : CRC32 error for all section.");
				memset((void *)(cal_ptr), 0xFF, cal_size);
				ret = -EIO;
			}
		}
	}

	CALL_BUFOP(core->resourcemgr.minfo.pb_fw, sync_for_device,
		core->resourcemgr.minfo.pb_fw,
		CAL_OFFSET0, FIMC_IS_MAX_CAL_SIZE, DMA_TO_DEVICE);
	if (ret)
		warn("calibration loading is fail");
	else
		info("calibration loading is success\n");

	return ret;
}
#endif

int fimc_is_vender_cal_load(struct fimc_is_vender *vender,
	void *module_data)
{
	int ret = 0;
	struct fimc_is_core *core;
	struct fimc_is_module_enum *module = module_data;

	core = container_of(vender, struct fimc_is_core, vender);

	if(module->position == SENSOR_POSITION_REAR) {
		/* Load calibration data from sensor */
		module->ext.sensor_con.cal_address = CAL_OFFSET0;
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR)
		ret = fimc_is_ischain_loadcalb_eeprom(core, NULL, SENSOR_POSITION_REAR);
#else
		ret = fimc_is_ischain_loadcalb(core, NULL, SENSOR_POSITION_REAR);
#endif
		if (ret) {
			err("loadcalb fail, load default caldata\n");
		}
	} else if(module->position == SENSOR_POSITION_REAR2) {
		module->ext.sensor_con.cal_address = CAL_OFFSET0;
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR2) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR2)
		ret = fimc_is_ischain_loadcalb_eeprom(core, NULL, SENSOR_POSITION_REAR2);
		if (ret) {
			err("loadcalb fail, load default caldata\n");
		}
#else
		module->ext.sensor_con.cal_address = 0;
#endif
	} else if(module->position == SENSOR_POSITION_REAR3) {
		module->ext.sensor_con.cal_address = CAL_OFFSET0;
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_REAR3) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_REAR3)
		ret = fimc_is_ischain_loadcalb_eeprom(core, NULL, SENSOR_POSITION_REAR3);
		if (ret) {
			err("loadcalb fail, load default caldata\n");
		}
#else
		module->ext.sensor_con.cal_address = 0;
#endif
	} else {
#if defined(CONFIG_CAMERA_EEPROM_SUPPORT_FRONT) || defined(CONFIG_CAMERA_OTPROM_SUPPORT_FRONT)
		module->ext.sensor_con.cal_address = CAL_OFFSET1;
		ret = fimc_is_ischain_loadcalb_eeprom(core, NULL, SENSOR_POSITION_FRONT);
		if (ret) {
			err("loadcalb fail, load default caldata\n");
		}
#else
		module->ext.sensor_con.cal_address = 0;
#endif
	}

	return 0;
}

int fimc_is_vender_module_sel(struct fimc_is_vender *vender, void *module_data)
{
	int ret = 0;
	struct fimc_is_module_enum *module = module_data;
	struct fimc_is_vender_specific *specific;

	BUG_ON(!module);

	specific = vender->private_data;

	if (module->position == SENSOR_POSITION_FRONT)
		specific->running_front_camera = true;
	else if (module->position == SENSOR_POSITION_REAR2)
		specific->running_rear2_camera = true;
	else if (module->position == SENSOR_POSITION_REAR3)
		specific->running_rear3_camera = true;
	else
		specific->running_rear_camera = true;

	return ret;
}

int fimc_is_vender_module_del(struct fimc_is_vender *vender, void *module_data)
{
	int ret = 0;
	struct fimc_is_module_enum *module = module_data;
	struct fimc_is_vender_specific *specific;

	BUG_ON(!module);

	specific = vender->private_data;

	if (module->position == SENSOR_POSITION_FRONT)
		specific->running_front_camera = false;
	else if (module->position == SENSOR_POSITION_REAR2)
		specific->running_rear2_camera = false;
	else if (module->position == SENSOR_POSITION_REAR3)
		specific->running_rear3_camera = false;
	else
		specific->running_rear_camera = false;

	return ret;
}

int fimc_is_vender_fw_sel(struct fimc_is_vender *vender)
{
	int ret = 0;
	struct fimc_is_core *core;
	struct device *dev;
	struct fimc_is_from_info *sysfs_finfo;

	BUG_ON(!vender);

	core = container_of(vender, struct fimc_is_core, vender);
	dev = &core->pdev->dev;

	if (!test_bit(FIMC_IS_PREPROC_S_INPUT, &core->preproc.state)) {
		ret = fimc_is_sec_run_fw_sel(dev, core->current_position);
		if (ret) {
			err("fimc_is_sec_run_fw_sel is fail(%d)", ret);
			goto p_err;
		}
	}

	fimc_is_sec_get_sysfs_finfo(&sysfs_finfo);
	snprintf(vender->request_fw_path, sizeof(vender->request_fw_path), "%s",
		sysfs_finfo->load_fw_name);

#if defined(CONFIG_OIS_USE)
	if (core->current_position == SENSOR_POSITION_REAR) {
		if (fimc_is_ois_check_reload_fw(core)) {
			/* Download OIS FW to OIS Device Everytime */
			fimc_is_ois_fw_update(core);
		}
	}

#ifdef CAMERA_REAR2
	if (core->current_position == SENSOR_POSITION_REAR2) {
		if (fimc_is_ois_check_reload_fw(core)) {
			/* Download OIS FW to OIS Device Everytime */
			fimc_is_ois_fw_update(core);
		}
	}
#endif /* CAMERA_REAR2 */
#ifdef CAMERA_REAR3
	if (core->current_position == SENSOR_POSITION_REAR3) {
		if (fimc_is_ois_check_reload_fw(core)) {
			/* Download OIS FW to OIS Device Everytime */
			fimc_is_ois_fw_update(core);
		}
	}
#endif /* CAMERA_REAR3 */
#endif

p_err:
	return ret;
}

int fimc_is_vender_setfile_sel(struct fimc_is_vender *vender, char *setfile_name)
{
	int ret = 0;
#ifdef CONFIG_COMPANION_USE
	struct fimc_is_core *core;
#endif

	BUG_ON(!vender);
	BUG_ON(!setfile_name);

#ifdef CONFIG_COMPANION_USE
	core = container_of(vender, struct fimc_is_core, vender);
	fimc_is_s_int_comb_isp(core, false, INTMR2_INTMCIS22);
#endif

	snprintf(vender->setfile_path, sizeof(vender->setfile_path), "%s%s",
		FIMC_IS_SETFILE_SDCARD_PATH, setfile_name);
	snprintf(vender->request_setfile_path, sizeof(vender->request_setfile_path), "%s",
		setfile_name);

	return ret;
}

#ifdef CAMERA_PARALLEL_RETENTION_SEQUENCE
void sensor_pwr_ctrl(struct work_struct *work)
{
	int ret = 0;
	struct exynos_platform_fimc_is_module *pdata;
	struct fimc_is_module_enum *g_module = NULL;
	struct fimc_is_core *core;

	core = (struct fimc_is_core *)dev_get_drvdata(fimc_is_dev);
	if (!core) {
		err("core is NULL");
		return;
	}

	ret = fimc_is_preproc_g_module(&core->preproc, &g_module);
	if (ret) {
		err("fimc_is_sensor_g_module is fail(%d)", ret);
		return;
	}

	pdata = g_module->pdata;
	ret = pdata->gpio_cfg(g_module->dev, SENSOR_SCENARIO_NORMAL,
		GPIO_SCENARIO_STANDBY_OFF_SENSOR);
	if (ret) {
		err("gpio_cfg(sensor) is fail(%d)", ret);
	}
}

static DECLARE_DELAYED_WORK(sensor_pwr_ctrl_work, sensor_pwr_ctrl);
#endif

int fimc_is_vender_preprocessor_gpio_on_sel(struct fimc_is_vender *vender, u32 scenario, u32 *gpio_scenario)
{
	int ret = 0;
	struct fimc_is_core *core;
	struct fimc_is_vender_specific *specific;

#ifdef CONFIG_COMPANION_DCDC_USE
	struct dcdc_power *dcdc;
	const char *vout_str = NULL;
	int vout = 0;
#endif
#ifdef CAMERA_PARALLEL_RETENTION_SEQUENCE
	int waitWorkqueue;
	struct exynos_platform_fimc_is_module *pdata;
	struct fimc_is_module_enum *module;
#endif

	specific = vender->private_data;
	core = container_of(vender, struct fimc_is_core, vender);

#ifdef CONFIG_PREPROCESSOR_STANDBY_USE
	if (scenario == SENSOR_SCENARIO_NORMAL) {
		if (GET_SENSOR_STATE(specific->standby_state, SENSOR_STATE_COMPANION) == SENSOR_STATE_STANDBY) {
			*gpio_scenario = GPIO_SCENARIO_STANDBY_OFF;
#ifdef CAMERA_PARALLEL_RETENTION_SEQUENCE
			queue_delayed_work(sensor_pwr_ctrl_wq, &sensor_pwr_ctrl_work, 0);
#endif
		}
	}
#endif

#ifdef CONFIG_COMPANION_USE
#ifdef CONFIG_COMPANION_DCDC_USE
	dcdc = &specific->companion_dcdc;

	if (dcdc->type == DCDC_VENDOR_PMIC) {
		/* Set default voltage without power binning if FIMC_IS_ISP_CV not exist. */
		if (!fimc_is_sec_file_exist(FIMC_IS_ISP_CV)) {
			info("Companion file not exist (%s), version : %X\n", FIMC_IS_ISP_CV, fimc_is_comp_get_ver());

			/* Get default vout in power binning table if EVT1 */
			if (fimc_is_comp_get_ver() == FIMC_IS_COMPANION_VERSION_EVT1) {
				vout = dcdc->get_vout_val(0);
				vout_str = dcdc->get_vout_str(0);
			/* If not, get default vout for both EVT0 and EVT1 */
			} else {
				if (dcdc->get_default_vout_val(&vout, &vout_str))
					err("fail to get companion default vout");
			}

			info("Companion: Set default voltage %sV\n", vout_str ? vout_str : "0");
			dcdc->set_vout(dcdc->client, vout);
		/* Do power binning if FIMC_IS_ISP_CV exist with PMIC */
		} else {
			fimc_is_power_binning(core);
		}
	}

#else /* !CONFIG_COMPANION_DCDC_USE*/
	/* Temporary Fixes. Set voltage to 0.85V for EVT0, 0.8V for EVT1 */
	if (fimc_is_comp_get_ver() == FIMC_IS_COMPANION_VERSION_EVT1) {
		info("%s: Companion EVT1. Set voltage 0.8V\n", __func__);
		ret = fimc_is_comp_set_voltage("VDDD_CORE_0.8V_COMP", 800000);
	} else if (fimc_is_comp_get_ver() == FIMC_IS_COMPANION_VERSION_EVT0) {
		info("%s: Companion EVT0. Set voltage 0.85V\n", __func__);
		ret = fimc_is_comp_set_voltage("VDDD_CORE_0.8V_COMP", 850000);
	} else {
		info("%s: Companion unknown rev. Set default voltage 0.85V\n", __func__);
		ret = fimc_is_comp_set_voltage("VDDD_CORE_0.8V_COMP", 850000);
	}
	if (ret < 0) {
		err("Companion core_0.8v setting fail!");
	}
#endif /* CONFIG_COMPANION_DCDC_USE */

#ifdef CAMERA_PARALLEL_RETENTION_SEQUENCE
	if (*gpio_scenario == GPIO_SCENARIO_STANDBY_OFF) {
		ret = fimc_is_preproc_g_module(&core->preproc, &module);
		if (ret) {
			err("fimc_is_sensor_g_module is fail(%d)", ret);
			goto p_err;
		}

		pdata = module->pdata;
		ret = pdata->gpio_cfg(module->dev, scenario, GPIO_SCENARIO_STANDBY_OFF_PREPROCESSOR);
		if (ret) {
			clear_bit(FIMC_IS_MODULE_GPIO_ON, &module->state);
			err("gpio_cfg(companion) is fail(%d)", ret);
			goto p_err;
		}

		waitWorkqueue = 0;
		/* Waiting previous workqueue */
		while (work_busy(&sensor_pwr_ctrl_work.work) &&
			waitWorkqueue < CAMERA_WORKQUEUE_MAX_WAITING) {
			if (!(waitWorkqueue % 100))
				info("Waiting Sensor power sequence...\n");
			usleep_range(100, 100);
			waitWorkqueue++;
		}
		info("workQueue is waited %d times\n", waitWorkqueue);
	}

p_err:
#endif
#endif

	return ret;
}

int fimc_is_vender_preprocessor_gpio_on(struct fimc_is_vender *vender, u32 scenario, u32 gpio_scenario)
{
	int ret = 0;
	return ret;
}

int fimc_is_vender_sensor_gpio_on_sel(struct fimc_is_vender *vender, u32 scenario, u32 *gpio_scenario)
{
	int ret = 0;
	return ret;
}

int fimc_is_vender_sensor_gpio_on(struct fimc_is_vender *vender, u32 scenario, u32 gpio_scenario)
{
	int ret = 0;
	return ret;
}

int fimc_is_vender_preprocessor_gpio_off_sel(struct fimc_is_vender *vender, u32 scenario, u32 *gpio_scenario)
{
	int ret = 0;
	struct fimc_is_vender_specific *specific;

	specific = vender->private_data;

#ifdef CONFIG_PREPROCESSOR_STANDBY_USE
	if (scenario == SENSOR_SCENARIO_NORMAL) {
#if defined(CONFIG_COMPANION_C2_USE)
		if (fimc_is_comp_get_ver() == FIMC_IS_COMPANION_VERSION_EVT1)
#endif
		{
			if (GET_SENSOR_STATE(specific->standby_state, SENSOR_STATE_COMPANION) == SENSOR_STATE_ON) {
				*gpio_scenario = GPIO_SCENARIO_STANDBY_ON;
			}
		}
	}
#endif

	return ret;
}

int fimc_is_vender_preprocessor_gpio_off(struct fimc_is_vender *vender, u32 scenario, u32 gpio_scenario)
{
	int ret = 0;
	struct fimc_is_vender_specific *specific;

	specific = vender->private_data;

#ifdef CONFIG_PREPROCESSOR_STANDBY_USE
	if (scenario == SENSOR_SCENARIO_NORMAL) {
		if (gpio_scenario == GPIO_SCENARIO_STANDBY_ON) {
			SET_SENSOR_STATE(specific->standby_state, SENSOR_STATE_COMPANION, SENSOR_STATE_STANDBY);
		} else if (gpio_scenario == GPIO_SCENARIO_OFF) {
			SET_SENSOR_STATE(specific->standby_state, SENSOR_STATE_COMPANION, SENSOR_STATE_OFF);
		}
		info("%s: COMPANION STATE %u\n", __func__, GET_SENSOR_STATE(specific->standby_state, SENSOR_STATE_COMPANION));
	}
#endif

	return ret;
}

int fimc_is_vender_sensor_gpio_off_sel(struct fimc_is_vender *vender, u32 scenario, u32 *gpio_scenario)
{
	int ret = 0;

	return ret;
}

int fimc_is_vender_sensor_gpio_off(struct fimc_is_vender *vender, u32 scenario, u32 gpio_scenario)
{
	int ret = 0;

	return ret;
}

/* Flash Mode Control */
#ifdef CONFIG_LEDS_LM3560
extern int lm3560_reg_update_export(u8 reg, u8 mask, u8 data);
#endif
#ifdef CONFIG_LEDS_SKY81296
extern int sky81296_torch_ctrl(int state);
#endif
#if defined(CONFIG_TORCH_CURRENT_CHANGE_SUPPORT) && defined(CONFIG_LEDS_S2MPB02)
extern int s2mpb02_set_torch_current(bool movie);
#endif
#ifdef CONFIG_FLED_SM5703
extern bool flash_control_ready;
extern int sm5703_led_mode_ctrl(int state);
#endif
#ifdef CONFIG_LEDS_KTD2692
extern int	ktd2692_led_mode_ctrl(int);
#endif


#ifdef CONFIG_LEDS_SUPPORT_FRONT_FLASH_AUTO
int fimc_is_vender_set_torch(u32 aeflashMode, u32 frontFlashMode)
{
	info("%s : aeflashMode(%d), frontFlashMode(%d)", __func__, aeflashMode, frontFlashMode);
	switch (aeflashMode) {
	case AA_FLASHMODE_ON_ALWAYS: /*TORCH(MOVIE) mode*/
#if defined(CONFIG_LEDS_S2MU005_FLASH)
		s2mu005_led_mode_ctrl(S2MU005_FLED_MODE_MOVIE);
#endif
		break;
	case AA_FLASHMODE_START: /*Pre flash mode*/
	case AA_FLASHMODE_ON: /* Main flash Mode */
#if defined(CONFIG_LEDS_S2MU005_FLASH) && defined(CONFIG_LEDS_SUPPORT_FRONT_FLASH)
		if(frontFlashMode == CAM2_FLASH_MODE_LCD)
			s2mu005_led_mode_ctrl(S2MU005_FLED_MODE_FLASH);
#endif
		break;
	case AA_FLASHMODE_CAPTURE: /*Main flash mode*/
		break;
	case AA_FLASHMODE_OFF: /*OFF mode*/
#if defined(CONFIG_LEDS_S2MU005_FLASH)
		s2mu005_led_mode_ctrl(S2MU005_FLED_MODE_OFF);
#endif
		break;
	default:
		break;
	}

	return 0;
}
#else
int fimc_is_vender_set_torch(u32 aeflashMode)
{
	switch (aeflashMode) {
	case AA_FLASHMODE_ON_ALWAYS: /*TORCH(MOVIE) mode*/
#ifdef CONFIG_LEDS_LM3560
		lm3560_reg_update_export(0xE0, 0xFF, 0xEF);
#elif defined(CONFIG_LEDS_SKY81296)
		sky81296_torch_ctrl(1);
#elif defined(CONFIG_TORCH_CURRENT_CHANGE_SUPPORT) && defined(CONFIG_LEDS_S2MPB02)
		s2mpb02_set_torch_current(true);
#elif defined(CONFIG_LEDS_SM5705)
		sm5705_fled_torch_on(SM5705_FLED_0, SM5705_FLED_MOVIE);
#elif defined(CONFIG_LEDS_S2MU005_FLASH)
		s2mu005_led_mode_ctrl(S2MU005_FLED_MODE_MOVIE);
#elif defined(CONFIG_FLED_SM5703)
		sm5703_led_mode_ctrl(5);
		if (flash_control_ready == false) {
			sm5703_led_mode_ctrl(3);
			flash_control_ready = true;
		}
#elif defined(CONFIG_LEDS_RT8547)
		rt8547_led_mode_ctrl(RT8547_ENABLE_MOVIE_MODE);
#elif defined(CONFIG_LEDS_KTD2692)
		ktd2692_led_mode_ctrl(3);
#endif
		break;
	case AA_FLASHMODE_START: /*Pre flash mode*/
#ifdef CONFIG_LEDS_LM3560
		lm3560_reg_update_export(0xE0, 0xFF, 0xEF);
#elif defined(CONFIG_LEDS_SKY81296)
		sky81296_torch_ctrl(1);
#elif defined(CONFIG_TORCH_CURRENT_CHANGE_SUPPORT) && defined(CONFIG_LEDS_S2MPB02)
		s2mpb02_set_torch_current(false);
#elif defined(CONFIG_LEDS_SM5705)
		sm5705_fled_torch_on(SM5705_FLED_0, SM5705_FLED_PREFLASH);
#elif defined(CONFIG_FLED_SM5703)
		sm5703_led_mode_ctrl(1);
		if (flash_control_ready == false) {
			sm5703_led_mode_ctrl(3);
			flash_control_ready = true;
		}
#elif defined(CONFIG_LEDS_RT8547)
		rt8547_led_mode_ctrl(RT8547_ENABLE_PRE_FLASH_MODE);
#elif defined(CONFIG_LEDS_KTD2692)
		ktd2692_led_mode_ctrl(4);
#endif
		break;
	case AA_FLASHMODE_CAPTURE: /*Main flash mode*/
#if defined(CONFIG_LEDS_SM5705)
		sm5705_fled_flash_on(SM5705_FLED_0);
#elif defined(CONFIG_FLED_SM5703)
		sm5703_led_mode_ctrl(2);
#elif defined(CONFIG_LEDS_RT8547)
		rt8547_led_mode_ctrl(RT8547_ENABLE_FLASH_MODE);
#elif defined(CONFIG_LEDS_KTD2692)
		ktd2692_led_mode_ctrl(2);
#endif
		break;
	case AA_FLASHMODE_OFF: /*OFF mode*/
#ifdef CONFIG_LEDS_SKY81296
		sky81296_torch_ctrl(0);
#elif defined(CONFIG_LEDS_SM5705)
		sm5705_fled_led_off(SM5705_FLED_0);
#elif defined(CONFIG_LEDS_S2MU005_FLASH)
		s2mu005_led_mode_ctrl(S2MU005_FLED_MODE_OFF);
#elif defined(CONFIG_FLED_SM5703)
		sm5703_led_mode_ctrl(0);
#elif defined(CONFIG_LEDS_RT8547)
		rt8547_led_mode_ctrl(RT8547_DISABLES_MOVIE_FLASH_MODE);
#elif defined(CONFIG_LEDS_KTD2692)
		ktd2692_led_mode_ctrl(1);
#endif
		break;
	default:
		break;
	}

	return 0;
}
#endif

int fimc_is_vender_video_s_ctrl(struct v4l2_control *ctrl,
	void *device_data)
{
	int ret = 0;
	struct fimc_is_device_ischain *device = (struct fimc_is_device_ischain *)device_data;
	struct fimc_is_core *core;
	struct fimc_is_vender_specific *specific;
	unsigned int value = 0;
	unsigned int captureIntent = 0;
	unsigned int captureCount = 0;

	BUG_ON(!device);
	BUG_ON(!ctrl);

	core = (struct fimc_is_core *)platform_get_drvdata(device->pdev);
	specific = core->vender.private_data;

	switch (ctrl->id) {
	case V4L2_CID_IS_INTENT:
		ctrl->id = VENDER_S_CTRL;
		value = (unsigned int)ctrl->value;
		captureIntent = (value >> 16) & 0x0000FFFF;
		 if (captureIntent == AA_CAPTRUE_INTENT_STILL_CAPTURE_DEBLUR_DYNAMIC_SHOT
				 || captureIntent == AA_CAPTRUE_INTENT_STILL_CAPTURE_OIS_DYNAMIC_SHOT) {
			 captureCount = value & 0x0000FFFF;
		} else {
			captureIntent = ctrl->value;
			captureCount = 0;
		}
		device->group_3aa.intent_ctl.captureIntent = captureIntent;
		device->group_3aa.intent_ctl.vendor_captureCount = captureCount;
		minfo("[VENDER] s_ctrl intent(%d) count(%d)\n", device, captureIntent, captureCount);
		break;
	case V4L2_CID_IS_CAMERA_TYPE:
		ctrl->id = VENDER_S_CTRL;
		switch (ctrl->value) {
		case IS_COLD_BOOT:
			/* change value to X when !TWIZ | front */
			fimc_is_itf_fwboot_init(device->interface);
			break;
		case IS_WARM_BOOT:
			if (specific ->need_cold_reset) {
				minfo("[VENDER] FW first launching mode for reset\n", device);
				device->interface->fw_boot_mode = FIRST_LAUNCHING;
			} else {
				/* change value to X when TWIZ & back | frist time back camera */
				if (!test_bit(IS_IF_LAUNCH_FIRST, &device->interface->launch_state))
					device->interface->fw_boot_mode = FIRST_LAUNCHING;
				else
					device->interface->fw_boot_mode = WARM_BOOT;
			}
			break;
		case IS_COLD_RESET:
			specific ->need_cold_reset = true;
			minfo("[VENDER] need cold reset!!!\n", device);
			break;
		default:
			err("[VENDER]unsupported ioctl(0x%X)", ctrl->id);
			ret = -EINVAL;
			break;
		}
		break;
	}

	return ret;
}
