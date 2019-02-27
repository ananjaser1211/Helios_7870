/*
 * amsung Exynos5 SoC series Sensor driver
 *
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
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
#include "../../sensor/module_framework/fimc-is-device-sensor-peri.h"
#include "fimc-is-resourcemgr.h"
#include "fimc-is-dt.h"

#include "fimc-is-helper-i2c.h"
#include "fimc-is-companion.h"

#define FIMC_IS_DEV_COMP_DEV_NAME "73C2"

int fimc_is_comp_stream_on(struct v4l2_subdev *subdev)
{
	struct fimc_is_preprocessor *preprocessor;
	struct i2c_client *client;
	int ret = 0;
	u16 companion_id = 0;

	BUG_ON(!subdev);

	preprocessor = (struct fimc_is_preprocessor *)v4l2_get_subdevdata(subdev);
	BUG_ON(!preprocessor);

	client = preprocessor->client;
	if (unlikely(!client)) {
		err("client is NULL");
		ret = -EINVAL;
		goto p_err;
	}

	fimc_is_comp_i2c_read(client, 0x0, &companion_id);
	info("Companion validation: 0x%04x\n", companion_id);

	ret = fimc_is_comp_i2c_write(client, 0x6800, 1);
	if (ret) {
		err("i2c write fail");
	}

	fimc_is_comp_i2c_read(client, 0x6800, &companion_id);
	info("indirect Companion stream on status: 0x%04x, %s\n", companion_id, __func__);
p_err:
	return ret;
}

int fimc_is_comp_stream_off(struct v4l2_subdev *subdev)
{
	struct fimc_is_preprocessor *preprocessor;
	struct i2c_client *client;
	int ret = 0;
	u16 companion_id = 0;

	BUG_ON(!subdev);

	preprocessor = (struct fimc_is_preprocessor *)v4l2_get_subdevdata(subdev);
	BUG_ON(!preprocessor);

	client = preprocessor->client;
	if (unlikely(!client)) {
		err("client is NULL");
		ret = -EINVAL;
		goto p_err;
	}

	fimc_is_comp_i2c_read(client, 0x0, &companion_id);
	info("Companion validation: 0x%04x\n", companion_id);

	ret = fimc_is_comp_i2c_write(client, 0x6800, 0);
	if (ret) {
		err("i2c write fail");
	}

	fimc_is_comp_i2c_read(client, 0x6800, &companion_id);
	info("indirect Companion stream off status: 0x%04x, %s\n", companion_id, __func__);
p_err:
	return ret;
}

int fimc_is_comp_mode_change(struct v4l2_subdev *subdev, u32 mode)
{
	struct fimc_is_preprocessor *preprocessor;
	struct i2c_client *client;
	int ret = 0;
	u16 compdata;

	BUG_ON(!subdev);

	preprocessor = (struct fimc_is_preprocessor *)v4l2_get_subdevdata(subdev);
	BUG_ON(!preprocessor);

	client = preprocessor->client;
	if (unlikely(!client)) {
		err("client is NULL");
		ret = -EINVAL;
		goto p_err;
	}

	if(preprocessor->position == 1)
		mode = COMPANION_FRONT_MODE_OFFSET_4E6;

	ret = fimc_is_comp_i2c_write(client, 0x0042, mode);
	if (ret) {
		err("i2c write fail");
	}

	fimc_is_comp_i2c_read(client, 0x42, &compdata);
	info("direct Companion mode: 0x%04x, %s\n", compdata, __func__);

p_err:
	return ret;
}

int sensor_comp_init(struct v4l2_subdev *subdev, u32 val){
	int ret =0;
	return ret;
}

static struct fimc_is_preprocessor_ops preprocessor_ops = {
	.preprocessor_stream_on = fimc_is_comp_stream_on,
	.preprocessor_stream_off = fimc_is_comp_stream_off,
	.preprocessor_mode_change = fimc_is_comp_mode_change,
};

static const struct v4l2_subdev_core_ops core_ops = {
	.init = sensor_comp_init,
};

static const struct v4l2_subdev_ops subdev_ops = {
	.core = &core_ops,
};

static int fimc_is_comp_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct fimc_is_core *core= NULL;
	struct v4l2_subdev *subdev_preprocessor = NULL;
	struct fimc_is_preprocessor *preprocessor = NULL;
	struct fimc_is_device_sensor *device = NULL;
	struct fimc_is_device_sensor_peri *sensor_peri = NULL;
	struct fimc_is_resourcemgr *resourcemgr = NULL;
	struct fimc_is_module_enum *module_enum = NULL;
	struct device_node *dnode;
	struct device *dev;
	u32 mindex = 0, mmax = 0;
	u32 sensor_id = 0;
	u32 position = 0;

	BUG_ON(!pdev);
	BUG_ON(!fimc_is_dev);

	dev = &pdev->dev;
	dnode = dev->of_node;

	if (fimc_is_dev == NULL) {
		warn("fimc_is_dev is not yet probed(preprop)");
		pdev->dev.init_name = FIMC_IS_DEV_COMP_DEV_NAME;
		ret =  -EPROBE_DEFER;
		goto p_err;
	}

	core = (struct fimc_is_core *)dev_get_drvdata(fimc_is_dev);
	if (!core) {
		err("core device is not yet probed");
		ret = -EPROBE_DEFER;
		goto p_err;
	}

	ret = of_property_read_u32(dnode, "id", &sensor_id);
	if (ret) {
		err("id read is fail(%d)", ret);
		goto p_err;
	}

	ret = of_property_read_u32(dnode, "position", &position);
	if (ret) {
		err("id read is fail(%d)", ret);
		goto p_err;
	}

	probe_info("%s position %d\n", __func__, position);

	device = &core->sensor[sensor_id];
	if (!device) {
		err("sensor device is NULL");
		ret = -ENOMEM;
		goto p_err;
	}
	info("device module id = %d \n",device->pdata->id);

	resourcemgr = device->resourcemgr;
	module_enum = device->module_enum;

	mmax = atomic_read(&resourcemgr->rsccount_module);
	for (mindex = 0; mindex < mmax; mindex++) {
		if (mindex == position) {
			sensor_peri = (struct fimc_is_device_sensor_peri *)module_enum[mindex].private_data;
			if (!sensor_peri) {
				return -EPROBE_DEFER;
			}
			info("module id : %d \n", mindex);
			break;
		}
	}

	preprocessor = &sensor_peri->preprocessor;
	if (!preprocessor) {
		err("preprocessor is NULL");
		ret = -ENOMEM;
		goto p_err;
	}

	subdev_preprocessor = kzalloc(sizeof(struct v4l2_subdev), GFP_KERNEL);
	if (!subdev_preprocessor) {
		err("subdev_preprocessor is NULL");
		ret = -ENOMEM;
		goto p_err;
	}

	sensor_peri->subdev_preprocessor = subdev_preprocessor;

	preprocessor->preprocessor_ops = &preprocessor_ops;

	/* This name must is match to sensor_open_extended actuator name */
	preprocessor = &sensor_peri->preprocessor;
	preprocessor->id = PREPROCESSOR_NAME_73C2;
	preprocessor->subdev = subdev_preprocessor;
	preprocessor->device = sensor_id;
	preprocessor->client = core->client0;
	preprocessor->position = position;

	v4l2_subdev_init(subdev_preprocessor, &subdev_ops);
	v4l2_set_subdevdata(subdev_preprocessor, preprocessor);
	v4l2_set_subdev_hostdata(subdev_preprocessor, device);
	snprintf(subdev_preprocessor->name, V4L2_SUBDEV_NAME_SIZE, "preprop-subdev.%d", sensor_id);

	ret = v4l2_device_register_subdev(&device->v4l2_dev, subdev_preprocessor);
	if (ret) {
		merr("v4l2_device_register_subdev is fail(%d)", device, ret);
		goto p_err;
	}

	set_bit(FIMC_IS_SENSOR_COMPANION_AVAILABLE, &sensor_peri->peri_state);

p_err:
	return ret;
}

static int fimc_is_comp_remove(struct platform_device *pdev)
{
	int ret = 0;

	info("%s\n", __func__);

	return ret;
}

#ifdef CONFIG_OF
static const struct of_device_id exynos_fimc_device_companion_match[] = {
	{
		.compatible = "samsung,exynos5-fimc-is-device-companion0",
	},
	{
		.compatible = "samsung,exynos5-fimc-is-device-companion1",
	},
	{},
};
MODULE_DEVICE_TABLE(of, exynos_fimc_device_companion_match);

static struct platform_driver fimc_is_device_companion_driver = {
	.probe		= fimc_is_comp_probe,
	.remove		= fimc_is_comp_remove,
	.driver = {
		.name	= FIMC_IS_DEV_COMP_DEV_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = exynos_fimc_device_companion_match,
	}
};

#else
static struct platform_device_id fimc_is_device_companion_driver_ids[] = {
	{
		.name		= FIMC_IS_DEV_COMP_DEV_NAME,
		.driver_data	= 0,
	},
	{},
};
MODULE_DEVICE_TABLE(platform, fimc_is_device_companion_driver_ids);

static struct platform_driver fimc_is_device_companion_driver = {
	.probe		= fimc_is_comp_probe,
	.remove		= __devexit_p(fimc_is_comp_remove),
	.id_table	= fimc_is_device_companion_driver_ids,
	.driver	  = {
		.name	= FIMC_IS_DEV_COMP_DEV_NAME,
		.owner	= THIS_MODULE,
	}
};
#endif

static int __init fimc_is_comp_init(void)
{
	int ret = platform_driver_register(&fimc_is_device_companion_driver);
	if (ret)
		err("platform_driver_register failed: %d\n", ret);

	return ret;
}
late_initcall(fimc_is_comp_init);

static void __exit fimc_is_comp_exit(void)
{
	platform_driver_unregister(&fimc_is_device_companion_driver);
}
module_exit(fimc_is_comp_exit);

MODULE_AUTHOR("Wonjin LIM<wj.lim@samsung.com>");
MODULE_DESCRIPTION("Exynos FIMC_IS_DEVICE_COMPANION driver");
MODULE_LICENSE("GPL");
