/*
 * Samsung Exynos5 SoC series Sensor driver
 *
 *
 * Copyright (c) 2018 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/videodev2.h>
#include <linux/videodev2_exynos_camera.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>

#include <exynos-fimc-is-sensor.h>
#include "fimc-is-hw.h"
#include "fimc-is-core.h"
#include "fimc-is-device-sensor.h"
#include "fimc-is-device-sensor-peri.h"
#include "fimc-is-resourcemgr.h"
#include "fimc-is-dt.h"

#include "fimc-is-device-module-base.h"


#define SENSOR_MODULE_NAME           "FIMC-IS-MODULE-5E9"
#define REAR_SENSOR_MODULE_NAME      "FIMC-IS-MODULE-REAR-5E9"
#define FRONT_SENSOR_MODULE_NAME     "FIMC-IS-MODULE-FRONT-5E9"

#define SENSOR_MODULE_5E9_REAR    0
#define SENSOR_MODULE_5E9_FRONT   1

static struct fimc_is_sensor_cfg config_module_5e9[] = {
	/* 2592x1944@30fps */
	FIMC_IS_SENSOR_CFG(2592, 1944,  30, 19, 0, CSI_DATA_LANES_2),
	/* 2592x1460@30fps */
	FIMC_IS_SENSOR_CFG(2592, 1460,  30, 19, 1, CSI_DATA_LANES_2),
	/* 2592x1458@30fps */
	FIMC_IS_SENSOR_CFG(2592, 1458,  30, 19, 2, CSI_DATA_LANES_2),
	/* 2592x1260@30fps */
	FIMC_IS_SENSOR_CFG(2592, 1260,  30, 19, 3, CSI_DATA_LANES_2),
	/* 1936x1936@30fps */
	FIMC_IS_SENSOR_CFG(1936, 1936,  30, 19, 4, CSI_DATA_LANES_2),
	/* 2592x1460@24fps */
	FIMC_IS_SENSOR_CFG(2592, 1460,  24, 19, 5, CSI_DATA_LANES_2),
	/* 2592x1458@24fps */
	FIMC_IS_SENSOR_CFG(2592, 1458,  24, 19, 6, CSI_DATA_LANES_2),
	/* 2592x1260@24fps */
	FIMC_IS_SENSOR_CFG(2592, 1260,  24, 19, 7, CSI_DATA_LANES_2),
	/* 1936x1936@24fps */
	FIMC_IS_SENSOR_CFG(1936, 1936,  24, 19, 8, CSI_DATA_LANES_2),
	/* 1296x972@58fps */
	FIMC_IS_SENSOR_CFG(1296, 972 ,  58, 19, 9, CSI_DATA_LANES_2),
	/* 2576x1932@30fps */
	FIMC_IS_SENSOR_CFG(2576, 1932,  30, 19, 10, CSI_DATA_LANES_2),
	/* 2560x1440@30fps */
	FIMC_IS_SENSOR_CFG(2560, 1440,  30, 19, 11, CSI_DATA_LANES_2),
	/* 2224x1080@30fps */
	FIMC_IS_SENSOR_CFG(2224, 1080,  30, 19, 12, CSI_DATA_LANES_2),
	/* 1920x1920@30fps */
	FIMC_IS_SENSOR_CFG(1920, 1920,  30, 19, 13, CSI_DATA_LANES_2),
	/* 1280x960@30fps */
	FIMC_IS_SENSOR_CFG(1280, 960,  30, 19, 14, CSI_DATA_LANES_2),
	/* 1280x720@30fps */
	FIMC_IS_SENSOR_CFG(1280, 720,  30, 19, 15, CSI_DATA_LANES_2),
	/* 640x480@120fps */
	FIMC_IS_SENSOR_CFG(640 , 480 , 120, 19, 16, CSI_DATA_LANES_2),
};

static struct fimc_is_vci vci_module_5e9[] = {
	{
		.pixelformat = V4L2_PIX_FMT_SBGGR10,
		.config = {{0, HW_FORMAT_RAW10}, {1, HW_FORMAT_UNKNOWN}, {2, HW_FORMAT_USER}, {3, 0}}
	}, {
		.pixelformat = V4L2_PIX_FMT_SBGGR12,
		.config = {{0, HW_FORMAT_RAW10}, {1, HW_FORMAT_UNKNOWN}, {2, HW_FORMAT_USER}, {3, 0}}
	}, {
		.pixelformat = V4L2_PIX_FMT_SBGGR16,
		.config = {{0, HW_FORMAT_RAW10}, {1, HW_FORMAT_UNKNOWN}, {2, HW_FORMAT_USER}, {3, 0}}
	}
};

static const struct v4l2_subdev_core_ops core_ops = {
	.init = sensor_module_init,
	.g_ctrl = sensor_module_g_ctrl,
	.s_ctrl = sensor_module_s_ctrl,
	.g_ext_ctrls = sensor_module_g_ext_ctrls,
	.s_ext_ctrls = sensor_module_s_ext_ctrls,
	.ioctl = sensor_module_ioctl,
	.log_status = sensor_module_log_status,
};

static const struct v4l2_subdev_video_ops video_ops = {
	.s_stream = sensor_module_s_stream,
	.s_mbus_fmt = sensor_module_s_format,
};

static const struct v4l2_subdev_ops subdev_ops = {
	.core = &core_ops,
	.video = &video_ops,
};

#if defined(CONFIG_CAMERA_A2CORELTE)
static int sensor_module_5e9_power_setpin_with_rear_eeprom(struct platform_device *pdev,
		struct exynos_platform_fimc_is_module *pdata);
#else
static int sensor_module_5e9_power_setpin_with_eeprom(struct platform_device *pdev,
		struct exynos_platform_fimc_is_module *pdata);
static int sensor_module_5e9_power_setpin_with_otp(struct platform_device *pdev,
		struct exynos_platform_fimc_is_module *pdata);
#endif
enum sensor_module_5e9_cal_memory {
	MODULE_5E9_CAL_MEMORY_N = 0,
	MODULE_5E9_CAL_MEMORY_FROM = 1,
	MODULE_5E9_CAL_MEMORY_EEPROM = 2,
	MODULE_5E9_CAL_MEMORY_OTP = 3,
	MODULE_5E9_CAL_MEMORY_MAX
};

static int sensor_module_5e9_power_setpin(struct platform_device *pdev,
		struct exynos_platform_fimc_is_module *pdata)
{
	struct device *dev;
	struct device_node *dnode;
	int ret = 0;
	int cal_type = MODULE_5E9_CAL_MEMORY_OTP;

	BUG_ON(!pdev);

	dev = &pdev->dev;
	dnode = dev->of_node;

	if (of_property_read_bool(dnode, "cal_memory")) {
		ret = of_property_read_u32(dnode, "cal_memory", &cal_type);
		if (ret) {
			warn("cal_memory read is fail(%d)",ret);
		}
		else {
			dev_info(dev, "%s cal_memory type is [%d]\n", __func__, cal_type);
		}
	} else {
		dev_warn(dev, "%s no cal_memory feild setting to opt\n", __func__);
	}
#if defined(CONFIG_CAMERA_A2CORELTE)
	return sensor_module_5e9_power_setpin_with_rear_eeprom(pdev, pdata);
#else
	if (cal_type == MODULE_5E9_CAL_MEMORY_EEPROM) {
		return sensor_module_5e9_power_setpin_with_eeprom(pdev, pdata);
	}
	else {
		return sensor_module_5e9_power_setpin_with_otp(pdev, pdata);
	}
#endif
}

#if defined(CONFIG_CAMERA_A2CORELTE)
static int sensor_module_5e9_power_setpin_with_rear_eeprom(struct platform_device *pdev,
		struct exynos_platform_fimc_is_module *pdata)
{
	struct device *dev;
	struct device_node *dnode;
	int gpio_reset = 0;
	//int gpio_mclk = 0;
	int gpio_none = 0;
	int gpio_cam_1p2_en = 0;
	int gpio_cam_2p8_en = 0;
    int gpio_camaf_2p8_en = 0;
	struct fimc_is_core *core;
	
	BUG_ON(!pdev);

	dev = &pdev->dev;
	dnode = dev->of_node;

	core = (struct fimc_is_core *)dev_get_drvdata(fimc_is_dev);
	if (!core) {
		err("core is NULL");
		return -EINVAL;
	}

	dev_info(dev, "%s E v4\n", __func__);

	gpio_reset = of_get_named_gpio(dnode, "gpio_reset", 0);
	if (!gpio_is_valid(gpio_reset)) {
		dev_err(dev, "failed to get gpio_reset\n");
	} else {
		gpio_request_one(gpio_reset, GPIOF_OUT_INIT_LOW, "CAM_GPIO_OUTPUT_LOW");
		gpio_free(gpio_reset);
	}

	gpio_cam_1p2_en = of_get_named_gpio(dnode, "gpio_cam_1p2_en", 0);
	if (!gpio_is_valid(gpio_cam_1p2_en)) {
		dev_err(dev, "failed to get PIN_POWER_EN\n");
		return -EINVAL;
	} else {
		gpio_request_one(gpio_cam_1p2_en, GPIOF_OUT_INIT_LOW, "CAM_VDDIO_EN");
		gpio_free(gpio_cam_1p2_en);
	}

	gpio_cam_2p8_en = of_get_named_gpio(dnode, "gpio_cam_2p8_en", 0);
	if (!gpio_is_valid(gpio_cam_2p8_en)) {
		dev_err(dev, "failed to get PIN_POWER_EN\n");
		return -EINVAL;
	} else {
		gpio_request_one(gpio_cam_2p8_en, GPIOF_OUT_INIT_LOW, "CAM_CORE_AVDD_EN");
		gpio_free(gpio_cam_2p8_en);
	}
	
	gpio_camaf_2p8_en = of_get_named_gpio(dnode, "gpio_camaf_2p8_en", 0);
	if (!gpio_is_valid(gpio_camaf_2p8_en)) {
		dev_err(dev, "failed to get gpio_camaf_2p8_en\n");
		return -EINVAL;
	} else {
		gpio_request_one(gpio_camaf_2p8_en, GPIOF_OUT_INIT_LOW, "CAM_GPIO_OUTPUT_LOW");
		gpio_free(gpio_camaf_2p8_en);
	}
	SET_PIN_INIT(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON);
	SET_PIN_INIT(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF);
	SET_PIN_INIT(pdata, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_ON);
	SET_PIN_INIT(pdata, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_OFF);

	/***************** Normal On *********************/
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_reset, "sen_rst low", PIN_OUTPUT, 0, 100);
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_cam_2p8_en, "gpio_cam_2p8_en", PIN_OUTPUT, 1, 100);
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_cam_1p2_en, "gpio_cam_1p2_en", PIN_OUTPUT, 1, 100);
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_none, "VDD_CAM_IO_1P8", PIN_REGULATOR, 1, 100);
//	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_none, "pin", PIN_FUNCTION, 1, 3000);
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_none, "pin", PIN_FUNCTION, 2, 0);
    if (gpio_is_valid(gpio_camaf_2p8_en)) {
		SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_camaf_2p8_en, "camaf_2p8_en", PIN_OUTPUT, 1, 9000);
	} else {
		SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_none, "VDD_CAM_AF_2P8", PIN_REGULATOR, 1, 9000);
	}
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_reset, "sen_rst high", PIN_OUTPUT, 1, 500);

	/***************** Normal Off *********************/
//	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_none, "pin", PIN_FUNCTION, 0, 10);
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_none, "pin", PIN_FUNCTION, 0, 0);
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_none, "pin", PIN_FUNCTION, 1, 0);
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_none, "pin", PIN_FUNCTION, 0, 0);	
    if (gpio_is_valid(gpio_camaf_2p8_en)) {
		SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_camaf_2p8_en, "camaf_2p8_en", PIN_OUTPUT, 0, 1000);
	} else {
		SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_none, "VDD_CAM_AF_2P8", PIN_REGULATOR, 0, 1000);
	}
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_reset, "sen_rst low", PIN_OUTPUT, 0, 500);
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_cam_2p8_en, "gpio_cam_2p8_en", PIN_OUTPUT, 0, 0);
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_cam_1p2_en, "gpio_cam_1p2_en", PIN_OUTPUT, 0, 0);
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_none, "VDD_CAM_IO_1P8", PIN_REGULATOR, 0, 100);

	/****************** ROM powerOn ******************/
    if (gpio_is_valid(gpio_camaf_2p8_en)) {
		SET_PIN(pdata, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_ON, gpio_camaf_2p8_en, "camaf_2p8_en", PIN_OUTPUT, 1, 2000);
	} else {
		SET_PIN(pdata, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_ON, gpio_none, "VDD_CAM_AF_2P8", PIN_REGULATOR, 1, 2000);
	}
	SET_PIN(pdata, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_ON, gpio_none, "VDD_CAM_IO_1P8", PIN_REGULATOR, 1, 100);

	/****************** ROM power Off ******************/
    if (gpio_is_valid(gpio_camaf_2p8_en)) {
		SET_PIN(pdata, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_OFF, gpio_camaf_2p8_en, "camaf_2p8_en", PIN_OUTPUT, 0, 10);
	} else {
		SET_PIN(pdata, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_OFF, gpio_none, "VDD_CAM_AF_2P8", PIN_REGULATOR, 0, 10);
	}
	SET_PIN(pdata, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_OFF, gpio_none, "VDD_CAM_IO_1P8", PIN_REGULATOR, 0, 100);

	dev_info(dev, "%s X v4\n", __func__);

	return 0;
}

#else
static int sensor_module_5e9_power_setpin_with_eeprom(struct platform_device *pdev,
		struct exynos_platform_fimc_is_module *pdata)
{
	struct device *dev;
	struct device_node *dnode;
	int gpio_reset = 0;
	int gpio_mclk = 0;
	int gpio_none = 0;
	int gpio_vtcam_core_en = 0;
	int gpio_vtcam_a2p8_en = 0;
	int gpio_rcam1_ldo_en = 0;
	struct fimc_is_core *core;
	bool shared_mclk = false;
	bool shared_camio_1p8 = false;

	BUG_ON(!pdev);

	dev = &pdev->dev;
	dnode = dev->of_node;

	core = (struct fimc_is_core *)dev_get_drvdata(fimc_is_dev);
	if (!core) {
		err("core is NULL");
		return -EINVAL;
	}

	dev_info(dev, "%s E v4\n", __func__);

	gpio_reset = of_get_named_gpio(dnode, "gpio_reset", 0);
	if (!gpio_is_valid(gpio_reset)) {
		dev_err(dev, "failed to get gpio_reset\n");
	} else {
		gpio_request_one(gpio_reset, GPIOF_OUT_INIT_LOW, "CAM_GPIO_OUTPUT_LOW");
		gpio_free(gpio_reset);
	}

	gpio_mclk = of_get_named_gpio(dnode, "gpio_mclk", 0);
	if (!gpio_is_valid(gpio_mclk)) {
		dev_err(dev, "failed to get gpio_mclk\n");
	} else {
		gpio_request_one(gpio_mclk, GPIOF_OUT_INIT_LOW, "CAM_MCLK_OUTPUT_LOW");
		gpio_free(gpio_mclk);
	}

	gpio_vtcam_core_en = of_get_named_gpio(dnode, "gpio_vtcam_core_en", 0);
	if (!gpio_is_valid(gpio_vtcam_core_en)) {
		dev_err(dev, "failed to get PIN_POWER_EN\n");
		return -EINVAL;
	} else {
		gpio_request_one(gpio_vtcam_core_en, GPIOF_OUT_INIT_LOW, "CAM_VDDIO_EN");
		gpio_free(gpio_vtcam_core_en);
	}

	gpio_vtcam_a2p8_en = of_get_named_gpio(dnode, "gpio_vtcam_a2p8_en", 0);
	if (!gpio_is_valid(gpio_vtcam_a2p8_en)) {
		dev_err(dev, "failed to get PIN_POWER_EN\n");
		return -EINVAL;
	} else {
		gpio_request_one(gpio_vtcam_a2p8_en, GPIOF_OUT_INIT_LOW, "CAM_CORE_AVDD_EN");
		gpio_free(gpio_vtcam_a2p8_en);
	}

	gpio_rcam1_ldo_en = of_get_named_gpio(dnode, "gpio_rcam1_ldo_en", 0);
	if (!gpio_is_valid(gpio_rcam1_ldo_en)) {
		dev_err(dev, "failed to get PIN_POWER_EN\n");
		return -EINVAL;
	} else {
		gpio_request_one(gpio_rcam1_ldo_en, GPIOF_OUT_INIT_LOW, "CAM_CORE_AVDD_EN");
		gpio_free(gpio_rcam1_ldo_en);
	}

	shared_mclk = of_property_read_bool(dnode, "shared_mclk");
	shared_camio_1p8 = of_property_read_bool(dnode, "shared_camio_1p8");

	SET_PIN_INIT(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON);
	SET_PIN_INIT(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF);
	SET_PIN_INIT(pdata, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_ON);
	SET_PIN_INIT(pdata, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_OFF);

	/***************** Normal On *********************/
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_reset, "sen_rst low", PIN_OUTPUT, 0, 100);
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_vtcam_a2p8_en, "sensor_vtcam_a2p8_en", PIN_OUTPUT, 1, 500);
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_vtcam_core_en, "sensor_vtcam_core_en", PIN_OUTPUT, 1, 500);
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_rcam1_ldo_en, "sensor_rcam1_ldo_en", PIN_OUTPUT, 1, 500);
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_none, "pin", PIN_FUNCTION, 1, 3000);
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_reset, "sen_rst high", PIN_OUTPUT, 1, 0);

	/***************** Normal Off *********************/
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_none, "pin", PIN_FUNCTION, 0, 10);
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_reset, "sen_rst", PIN_OUTPUT, 0, 500);
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_vtcam_a2p8_en, "sensor_vtcam_a2p8_en", PIN_OUTPUT, 0, 0);
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_vtcam_core_en, "sensor_vtcam_core_en", PIN_OUTPUT, 0, 0);
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_rcam1_ldo_en, "sensor_rcam1_ldo_en", PIN_OUTPUT, 0, 0);

	/****************** ROM powerOn ******************/
	SET_PIN(pdata, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_ON, gpio_rcam1_ldo_en, "sensor_rcam1_ldo_en", PIN_OUTPUT, 1, 500);

	/****************** ROM power Off ******************/
	SET_PIN(pdata, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_OFF, gpio_rcam1_ldo_en, "sensor_rcam1_ldo_en", PIN_OUTPUT, 0, 0);

	dev_info(dev, "%s X v4\n", __func__);

	return 0;
}

static int sensor_module_5e9_power_setpin_with_otp(struct platform_device *pdev,
		struct exynos_platform_fimc_is_module *pdata)
{
	struct device *dev;
	struct device_node *dnode;
	int gpio_reset = 0;
	int gpio_mclk = 0;
	int gpio_none = 0;
	int gpio_vtcam_core_en = 0;
	int gpio_vtcam_a2p8_en = 0;
	int gpio_rcam1_ldo_en = 0;
	struct fimc_is_core *core;
	bool shared_mclk = false;
	bool shared_camio_1p8 = false;

	BUG_ON(!pdev);

	dev = &pdev->dev;
	dnode = dev->of_node;

	core = (struct fimc_is_core *)dev_get_drvdata(fimc_is_dev);
	if (!core) {
		err("core is NULL");
		return -EINVAL;
	}

	dev_info(dev, "%s E v4\n", __func__);

	gpio_reset = of_get_named_gpio(dnode, "gpio_reset", 0);
	if (!gpio_is_valid(gpio_reset)) {
		dev_err(dev, "failed to get gpio_reset\n");
	} else {
		gpio_request_one(gpio_reset, GPIOF_OUT_INIT_LOW, "CAM_GPIO_OUTPUT_LOW");
		gpio_free(gpio_reset);
	}

	gpio_mclk = of_get_named_gpio(dnode, "gpio_mclk", 0);
	if (!gpio_is_valid(gpio_mclk)) {
		dev_err(dev, "failed to get gpio_mclk\n");
	} else {
		gpio_request_one(gpio_mclk, GPIOF_OUT_INIT_LOW, "CAM_MCLK_OUTPUT_LOW");
		gpio_free(gpio_mclk);
	}

	gpio_vtcam_core_en = of_get_named_gpio(dnode, "gpio_vtcam_core_en", 0);
	if (!gpio_is_valid(gpio_vtcam_core_en)) {
		dev_err(dev, "failed to get PIN_POWER_EN\n");
		return -EINVAL;
	} else {
		gpio_request_one(gpio_vtcam_core_en, GPIOF_OUT_INIT_LOW, "CAM_VDDIO_EN");
		gpio_free(gpio_vtcam_core_en);
	}

	gpio_vtcam_a2p8_en = of_get_named_gpio(dnode, "gpio_vtcam_a2p8_en", 0);
	if (!gpio_is_valid(gpio_vtcam_a2p8_en)) {
		dev_err(dev, "failed to get PIN_POWER_EN\n");
		return -EINVAL;
	} else {
		gpio_request_one(gpio_vtcam_a2p8_en, GPIOF_OUT_INIT_LOW, "CAM_CORE_AVDD_EN");
		gpio_free(gpio_vtcam_a2p8_en);
	}

	gpio_rcam1_ldo_en = of_get_named_gpio(dnode, "gpio_rcam1_ldo_en", 0);
	if (!gpio_is_valid(gpio_rcam1_ldo_en)) {
		dev_err(dev, "failed to get PIN_POWER_EN\n");
		return -EINVAL;
	} else {
		gpio_request_one(gpio_rcam1_ldo_en, GPIOF_OUT_INIT_LOW, "CAM_CORE_AVDD_EN");
		gpio_free(gpio_rcam1_ldo_en);
	}

	shared_mclk = of_property_read_bool(dnode, "shared_mclk");
	shared_camio_1p8 = of_property_read_bool(dnode, "shared_camio_1p8");

	SET_PIN_INIT(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON);
	SET_PIN_INIT(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF);
	SET_PIN_INIT(pdata, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_ON);
	SET_PIN_INIT(pdata, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_OFF);

	/* Normal On */
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_reset, "sen_rst low", PIN_OUTPUT, 0, 100);

	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_vtcam_a2p8_en, "sensor_vtcam_a2p8_en", PIN_OUTPUT, 1, 500);
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_vtcam_core_en, "sensor_vtcam_core_en", PIN_OUTPUT, 1, 500);
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_rcam1_ldo_en, "sensor_rcam1_ldo_en", PIN_OUTPUT, 1, 500);

	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_none, "pin", PIN_FUNCTION, 1, 3000);
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_reset, "sen_rst high", PIN_OUTPUT, 1, 0);

	/* Normal Off */
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_none, "pin", PIN_FUNCTION, 0, 10);
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_reset, "sen_rst", PIN_OUTPUT, 0, 500);


	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_vtcam_a2p8_en, "sensor_vtcam_a2p8_en", PIN_OUTPUT, 0, 0);
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_vtcam_core_en, "sensor_vtcam_core_en", PIN_OUTPUT, 0, 0);
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_rcam1_ldo_en, "sensor_rcam1_ldo_en", PIN_OUTPUT, 0, 0);

	/* ROM powerOn */
	SET_PIN(pdata, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_ON, gpio_reset, "sen_rst low", PIN_OUTPUT, 0, 100);

	SET_PIN(pdata, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_ON, gpio_vtcam_a2p8_en, "sensor_vtcam_a2p8_en", PIN_OUTPUT, 1, 500);
	SET_PIN(pdata, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_ON, gpio_vtcam_core_en, "sensor_vtcam_core_en", PIN_OUTPUT, 1, 500);
	SET_PIN(pdata, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_ON, gpio_rcam1_ldo_en, "sensor_rcam1_ldo_en", PIN_OUTPUT, 1, 500);

	SET_PIN(pdata, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_ON, gpio_none, "pin", PIN_FUNCTION, 1, 3000);
	SET_PIN(pdata, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_ON, gpio_reset, "sen_rst high", PIN_OUTPUT, 1, 500);

	/* ROM power Off */
	SET_PIN(pdata, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_OFF, gpio_none, "pin", PIN_FUNCTION, 0, 10);
	SET_PIN(pdata, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_OFF, gpio_reset, "sen_rst", PIN_OUTPUT, 0, 100);
	SET_PIN(pdata, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_OFF, gpio_vtcam_a2p8_en, "sensor_vtcam_a2p8_en", PIN_OUTPUT, 0, 0);
	SET_PIN(pdata, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_OFF, gpio_vtcam_core_en, "sensor_vtcam_core_en", PIN_OUTPUT, 0, 0);
	SET_PIN(pdata, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_OFF, gpio_rcam1_ldo_en, "sensor_rcam1_ldo_en", PIN_OUTPUT, 0, 0);

	dev_info(dev, "%s X v4\n", __func__);

	return 0;
}
#endif
static int __init sensor_module_5e9_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct fimc_is_core *core;
	struct v4l2_subdev *subdev_module;
	struct fimc_is_module_enum *module;
	struct fimc_is_device_sensor *device;
	struct sensor_open_extended *ext;
	struct exynos_platform_fimc_is_module *pdata;
	struct device *dev;
#if 0
	struct pinctrl_state *s;
#endif

	BUG_ON(!fimc_is_dev);

	core = (struct fimc_is_core *)dev_get_drvdata(fimc_is_dev);
	if (!core) {
		probe_info("core device is not yet probed");
		return -EPROBE_DEFER;
	}

	dev = &pdev->dev;

	fimc_is_sensor_module_parse_dt(pdev, sensor_module_5e9_power_setpin);

	pdata = dev_get_platdata(dev);
	device = &core->sensor[pdata->id];

	subdev_module = kzalloc(sizeof(struct v4l2_subdev), GFP_KERNEL);
	if (!subdev_module) {
		probe_err("subdev_module is NULL");
		ret = -ENOMEM;
		goto p_err;
	}

	module = &device->module_enum[atomic_read(&core->resourcemgr.rsccount_module)];
	atomic_inc(&core->resourcemgr.rsccount_module);
	clear_bit(FIMC_IS_MODULE_GPIO_ON, &module->state);
	module->pdata = pdata;
	module->dev = dev;
	module->sensor_id = SENSOR_NAME_S5K5E9;
	module->subdev = subdev_module;
	module->device = pdata->id;
	module->client = NULL;
	module->active_width = 2592;
	module->active_height = 1944;
	module->margin_left = 0;
	module->margin_right = 0;
	module->margin_top = 0;
	module->margin_bottom = 0;
	module->pixel_width = module->active_width;
	module->pixel_height = module->active_height;
	module->max_framerate = 120;
	module->position = pdata->position;
	module->mode = CSI_MODE_DT_ONLY;
	module->lanes = CSI_DATA_LANES_2;
	module->bitwidth = 10;
	module->vcis = ARRAY_SIZE(vci_module_5e9);
	module->vci = vci_module_5e9;
	module->sensor_maker = "SLSI";
	module->sensor_name = "S5K5E9";
	if(pdata->id == 1)
		module->setfile_name = "setfile_5e9_front.bin";
	else
	module->setfile_name = "setfile_5e9.bin";
	module->cfgs = ARRAY_SIZE(config_module_5e9);
	module->cfg = config_module_5e9;
	module->ops = NULL;
	/* Sensor peri */
	module->private_data = kzalloc(sizeof(struct fimc_is_device_sensor_peri), GFP_KERNEL);
	if (!module->private_data) {
		probe_err("fimc_is_device_sensor_peri is NULL");
		ret = -ENOMEM;
		goto p_err;
	}
	fimc_is_sensor_peri_probe((struct fimc_is_device_sensor_peri*)module->private_data);
	PERI_SET_MODULE(module);

	ext = &module->ext;
	ext->mipi_lane_num = module->lanes;
	ext->I2CSclk = 0;

	ext->sensor_con.product_name = module->sensor_id;
	ext->sensor_con.peri_type = SE_I2C;
	ext->sensor_con.peri_setting.i2c.channel = pdata->sensor_i2c_ch;
	ext->sensor_con.peri_setting.i2c.slave_address = pdata->sensor_i2c_addr;
	ext->sensor_con.peri_setting.i2c.speed = 400000;

	if (pdata->af_product_name !=  ACTUATOR_NAME_NOTHING) {
		ext->actuator_con.product_name = pdata->af_product_name;
		ext->actuator_con.peri_type = SE_I2C;
		ext->actuator_con.peri_setting.i2c.channel = pdata->af_i2c_ch;
		ext->actuator_con.peri_setting.i2c.slave_address = pdata->af_i2c_addr;
		ext->actuator_con.peri_setting.i2c.speed = 400000;
	}

	if (pdata->flash_product_name != FLADRV_NAME_NOTHING) {
		ext->flash_con.product_name = pdata->flash_product_name;
		ext->flash_con.peri_type = SE_GPIO;
		ext->flash_con.peri_setting.gpio.first_gpio_port_no = pdata->flash_first_gpio;
		ext->flash_con.peri_setting.gpio.second_gpio_port_no = pdata->flash_second_gpio;
	}

	ext->from_con.product_name = FROMDRV_NAME_NOTHING;

	if (pdata->preprocessor_product_name != PREPROCESSOR_NAME_NOTHING) {
		ext->preprocessor_con.product_name = pdata->preprocessor_product_name;
		ext->preprocessor_con.peri_info0.valid = true;
		ext->preprocessor_con.peri_info0.peri_type = SE_SPI;
		ext->preprocessor_con.peri_info0.peri_setting.spi.channel = pdata->preprocessor_spi_channel;
		ext->preprocessor_con.peri_info1.valid = true;
		ext->preprocessor_con.peri_info1.peri_type = SE_I2C;
		ext->preprocessor_con.peri_info1.peri_setting.i2c.channel = pdata->preprocessor_i2c_ch;
		ext->preprocessor_con.peri_info1.peri_setting.i2c.slave_address = pdata->preprocessor_i2c_addr;
		ext->preprocessor_con.peri_info1.peri_setting.i2c.speed = 400000;
		ext->preprocessor_con.peri_info2.valid = true;
		ext->preprocessor_con.peri_info2.peri_type = SE_DMA;
		ext->preprocessor_con.peri_info2.peri_setting.dma.channel = FLITE_ID_D;
	} else {
		ext->preprocessor_con.product_name = pdata->preprocessor_product_name;
	}

	if (pdata->ois_product_name != OIS_NAME_NOTHING) {
		ext->ois_con.product_name = pdata->ois_product_name;
		ext->ois_con.peri_type = SE_I2C;
		ext->ois_con.peri_setting.i2c.channel = pdata->ois_i2c_ch;
		ext->ois_con.peri_setting.i2c.slave_address = pdata->ois_i2c_addr;
		ext->ois_con.peri_setting.i2c.speed = 400000;
	} else {
		ext->ois_con.product_name = pdata->ois_product_name;
		ext->ois_con.peri_type = SE_NULL;
	}

	v4l2_subdev_init(subdev_module, &subdev_ops);

	v4l2_set_subdevdata(subdev_module, module);
	v4l2_set_subdev_hostdata(subdev_module, device);
	snprintf(subdev_module->name, V4L2_SUBDEV_NAME_SIZE, "sensor-subdev.%d", module->sensor_id);


#if 0
	s = pinctrl_lookup_state(pdata->pinctrl, "release");

	if (pinctrl_select_state(pdata->pinctrl, s) < 0) {
		probe_err("pinctrl_select_state is fail\n");
		goto p_err;
	}
#endif
p_err:
	probe_info("%s done(%d)\n", __func__, ret);
	return ret;
}

static int sensor_module_5e9_remove(struct platform_device *pdev)
{
	int ret = 0;

	info("%s\n", __func__);

	return ret;
}

static const struct of_device_id exynos_fimc_is_sensor_module_5e9_match[] = {
	{
		.compatible = "samsung,sensor-rear-module-5e9",
	},
	{
		.compatible = "samsung,sensor-front-module-5e9",
	},
	{},
};
MODULE_DEVICE_TABLE(of, exynos_fimc_is_sensor_module_5e9_match);

static struct platform_driver rear_sensor_module_5e9_driver = {
	.probe  = sensor_module_5e9_probe,
	.remove = sensor_module_5e9_remove,
	.driver = {
		.name   = "REAR_SENSOR_MODULE_NAME",
		.owner  = THIS_MODULE,
		.of_match_table = exynos_fimc_is_sensor_module_5e9_match,
	}
};

module_platform_driver(rear_sensor_module_5e9_driver);

static struct platform_driver front_sensor_module_5e9_driver = {
	.probe  = sensor_module_5e9_probe,
	.remove = sensor_module_5e9_remove,
	.driver = {
		.name   = "FRONT_SENSOR_MODULE_NAME",
		.owner  = THIS_MODULE,
		.of_match_table = exynos_fimc_is_sensor_module_5e9_match,
	}
};

module_platform_driver(front_sensor_module_5e9_driver);
