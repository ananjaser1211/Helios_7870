/*
 * Samsung Exynos5 SoC series Sensor driver
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
#include <linux/videodev2_exynos_camera_ext.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/vmalloc.h>
#include <linux/platform_device.h>
#ifdef CONFIG_OF
#include <linux/of_gpio.h>
#endif
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>

#include <exynos-fimc-is-sensor.h>
#include "fimc-is-core.h"
#include "fimc-is-device-sensor.h"
#include "fimc-is-resourcemgr.h"
#include "fimc-is-hw.h"
#include "fimc-is-dt.h"
#include "fimc-is-module-sr130pc20-soc.h"
#include "fimc-is-module-sr130pc20-soc-reg.h"

#define SENSOR_NAME "SR130PC20"
#define SENSOR_VER "SR130PC20M"

static const struct sensor_regset_table sr130pc20_regset_table = {
	.init			= SENSOR_REGISTER_REGSET(sr130pc20_Init_Reg),
	.init_vt			= SENSOR_REGISTER_REGSET(sr130pc20_Init_VT_Reg),

	.effect_none		= SENSOR_REGISTER_REGSET(sr130pc20_Effect_Normal),
	.effect_negative	= SENSOR_REGISTER_REGSET(sr130pc20_Effect_Negative),
	.effect_gray		= SENSOR_REGISTER_REGSET(sr130pc20_Effect_Gray),
	.effect_sepia		= SENSOR_REGISTER_REGSET(sr130pc20_Effect_Sepia),
	.effect_aqua		= SENSOR_REGISTER_REGSET(sr130pc20_Effect_Aqua),

	.fps_auto		= SENSOR_REGISTER_REGSET(sr130pc20_fps_auto),
	.fps_7			= SENSOR_REGISTER_REGSET(sr130pc20_fps_7_camcorder),
	.fps_15			= SENSOR_REGISTER_REGSET(sr130pc20_fps_15_camcorder),
	.fps_25_camcorder	= SENSOR_REGISTER_REGSET(sr130pc20_fps_25_camcorder),
	.fps_30_camcorder	= SENSOR_REGISTER_REGSET(sr130pc20_fps_30_camcorder),

	.wb_auto		= SENSOR_REGISTER_REGSET(sr130pc20_wb_auto),
	.wb_daylight		= SENSOR_REGISTER_REGSET(sr130pc20_wb_daylight),
	.wb_cloudy		= SENSOR_REGISTER_REGSET(sr130pc20_wb_cloudy),
	.wb_fluorescent		= SENSOR_REGISTER_REGSET(sr130pc20_wb_fluorescent),
	.wb_incandescent	= SENSOR_REGISTER_REGSET(sr130pc20_wb_incandescent),

	.brightness_m4		= SENSOR_REGISTER_REGSET(sr130pc20_brightness_m4),
	.brightness_m3		= SENSOR_REGISTER_REGSET(sr130pc20_brightness_m3),
	.brightness_m2		= SENSOR_REGISTER_REGSET(sr130pc20_brightness_m2),
	.brightness_m1		= SENSOR_REGISTER_REGSET(sr130pc20_brightness_m1),
	.brightness_default	= SENSOR_REGISTER_REGSET(sr130pc20_brightness_default),
	.brightness_p1		= SENSOR_REGISTER_REGSET(sr130pc20_brightness_p1),
	.brightness_p2		= SENSOR_REGISTER_REGSET(sr130pc20_brightness_p2),
	.brightness_p3		= SENSOR_REGISTER_REGSET(sr130pc20_brightness_p3),
	.brightness_p4		= SENSOR_REGISTER_REGSET(sr130pc20_brightness_p4),

	.contrast_p4		= SENSOR_REGISTER_REGSET(sr130pc20_contrast_p4),
	.contrast_p3		= SENSOR_REGISTER_REGSET(sr130pc20_contrast_p3),
	.contrast_p2		= SENSOR_REGISTER_REGSET(sr130pc20_contrast_p2),
	.contrast_p1		= SENSOR_REGISTER_REGSET(sr130pc20_contrast_p1),
	.contrast_default	= SENSOR_REGISTER_REGSET(sr130pc20_contrast_default),
	.contrast_m1		= SENSOR_REGISTER_REGSET(sr130pc20_contrast_m1),
	.contrast_m2		= SENSOR_REGISTER_REGSET(sr130pc20_contrast_m2),
	.contrast_m3		= SENSOR_REGISTER_REGSET(sr130pc20_contrast_m3),
	.contrast_m4		= SENSOR_REGISTER_REGSET(sr130pc20_contrast_m4),

	/*.preview	= SENSOR_REGISTER_REGSET(sr130pc20_preview),*/
	.stop_stream		= SENSOR_REGISTER_REGSET(sr130pc20_stop_stream),
	.start_stream		= SENSOR_REGISTER_REGSET(sr130pc20_start_stream),

	.i2c_check		= SENSOR_REGISTER_REGSET(sr130pc20_i2c_check),

	.resol_176_144		= SENSOR_REGISTER_REGSET(sr130pc20_resol_176_144),
	.resol_320_240		= SENSOR_REGISTER_REGSET(sr130pc20_resol_320_240),
	.resol_352_288		= SENSOR_REGISTER_REGSET(sr130pc20_resol_352_288),
	.resol_528_432		= SENSOR_REGISTER_REGSET(sr130pc20_resol_528_432),
	.resol_640_480		= SENSOR_REGISTER_REGSET(sr130pc20_resol_640_480),
	//.resol_704_576		= SENSOR_REGISTER_REGSET(sr130pc20_resol_704_576),
	//.resol_800_600		= SENSOR_REGISTER_REGSET(sr130pc20_resol_800_600),
	//.resol_1280_960		= SENSOR_REGISTER_REGSET(sr130pc20_resol_1280_960),
	//.resol_1600_1200	= SENSOR_REGISTER_REGSET(sr130pc20_resol_1600_1200),
	/*.capture_mode = SENSOR_REGISTER_REGSET(sr130pc20_resol_1600_1200)*/
	.resol_176_144_capture		= SENSOR_REGISTER_REGSET(sr130pc20_resol_176_144_capture),
	.resol_320_240_capture		= SENSOR_REGISTER_REGSET(sr130pc20_resol_320_240_capture),
	.resol_640_480_capture		= SENSOR_REGISTER_REGSET(sr130pc20_resol_640_480_capture),
	.resol_1280_960_capture 	= SENSOR_REGISTER_REGSET(sr130pc20_resol_1280_960_capture),

	.hflip			= SENSOR_REGISTER_REGSET(sr130pc20_hflip),
	.vflip			= SENSOR_REGISTER_REGSET(sr130pc20_vflip),
	.flip_off		= SENSOR_REGISTER_REGSET(sr130pc20_flip_off),
	.camcorder_flip		= SENSOR_REGISTER_REGSET(sr130pc20_camcorder_flip),

	.anti_banding_flicker_50hz	= SENSOR_REGISTER_REGSET(sr130pc20_anti_banding_flicker_50hz),
	.anti_banding_flicker_60hz	= SENSOR_REGISTER_REGSET(sr130pc20_anti_banding_flicker_60hz),

	.video_mode_preview	= SENSOR_REGISTER_REGSET(sr130pc20_video_mode_preview),
	.video_mode_encode	= SENSOR_REGISTER_REGSET(sr130pc20_video_mode_encode),
	.video_mode_upcc	= SENSOR_REGISTER_REGSET(sr130pc20_video_mode_upcc),

	.work_mode_auto		= SENSOR_REGISTER_REGSET(sr130pc20_work_mode_auto),

	.night_normal		= SENSOR_REGISTER_REGSET(sr130pc20_night_normal),
	.night_dark		= SENSOR_REGISTER_REGSET(sr130pc20_night_dark),

	.metering_matrix	= SENSOR_REGISTER_REGSET(sr130pc20_metering_matrix),
	.metering_spot		= SENSOR_REGISTER_REGSET(sr130pc20_metering_spot),
	.metering_center	= SENSOR_REGISTER_REGSET(sr130pc20_metering_center),

	.ae_on_50hz		= SENSOR_REGISTER_REGSET(sr130pc20_AE_on_50Hz),
	.ae_off_50hz		= SENSOR_REGISTER_REGSET(sr130pc20_AE_off_50Hz),
	.ae_on_60hz		= SENSOR_REGISTER_REGSET(sr130pc20_AE_on_60Hz),
	.ae_off_60hz		= SENSOR_REGISTER_REGSET(sr130pc20_AE_off_60Hz),
};

static struct fimc_is_sensor_cfg settle_sr130pc20[] = {
	FIMC_IS_SENSOR_CFG_SOC(1280, 960, 30, 4, 0),
	FIMC_IS_SENSOR_CFG_SOC(1280, 960, 15, 4, 1),
	FIMC_IS_SENSOR_CFG_SOC(800, 600, 30, 4, 2),
	FIMC_IS_SENSOR_CFG_SOC(640, 480, 30, 4, 3),
	FIMC_IS_SENSOR_CFG_SOC(640, 480, 25, 4, 4),
	FIMC_IS_SENSOR_CFG_SOC(320, 240, 25, 4, 5),
	FIMC_IS_SENSOR_CFG_SOC(176, 144, 25, 4, 6),
};

static const struct sr130pc20_fps sr130pc20_framerates[] = {
	{ I_FPS_0,	FRAME_RATE_AUTO },
	{ I_FPS_7,	FRAME_RATE_7},
	{ I_FPS_15,	FRAME_RATE_15 },
	{ I_FPS_20,	FRAME_RATE_20 },
	{ I_FPS_25,	FRAME_RATE_25 },
	{ I_FPS_30, FRAME_RATE_30 },
};

static const struct sr130pc20_framesize sr130pc20_preview_frmsizes[] = {
	{ PREVIEW_SZ_320x240,	320,  240 },
	{ PREVIEW_SZ_CIF,	352,  288 },
	{ PREVIEW_SZ_528x432, 528, 432 },
	{ PREVIEW_SZ_VGA,	640,  480 },
};

static const struct sr130pc20_framesize sr130pc20_capture_frmsizes[] = {
	{ CAPTURE_SZ_VGA,	640,  480 },
	{ CAPTURE_SZ_1MP,	1280, 960 },
};

static struct fimc_is_vci vci_sr130pc20[] = {
	{
		.pixelformat = V4L2_PIX_FMT_YUYV,
		.config = {{0, HW_FORMAT_YUV422_8BIT}, {1, HW_FORMAT_UNKNOWN}, {2, HW_FORMAT_USER}, {3, 0}}
	}
};

static inline struct fimc_is_module_enum *to_module(struct v4l2_subdev *subdev)
{
	struct fimc_is_module_enum *module;
	module = (struct fimc_is_module_enum *)v4l2_get_subdevdata(subdev);
	return module;
}

static inline struct sr130pc20_state *to_state(struct v4l2_subdev *subdev)
{
	struct fimc_is_module_enum *module;
	module = (struct fimc_is_module_enum *)v4l2_get_subdevdata(subdev);
	return (struct sr130pc20_state *)module->private_data;
}

static inline struct i2c_client *to_client(struct v4l2_subdev *subdev)
{
	struct fimc_is_module_enum *module;
	module = (struct fimc_is_module_enum *)v4l2_get_subdevdata(subdev);
	return (struct i2c_client *)module->client;
}

static int fimc_is_sr130pc20_read8(struct i2c_client *client, u8 addr, u8 *val)
{
	int ret = 0;
	struct i2c_msg msg[2];
	u8 wbuf[2];

	if (!client->adapter) {
		cam_err("Could not find adapter!\n");
		ret = -ENODEV;
		goto p_err;
	}

	/* 1. I2C operation for writing. */
	msg[0].addr = client->addr;
	msg[0].flags = 0; /* write : 0, read : 1 */
	msg[0].len = 1;
	msg[0].buf = wbuf;
	/* TODO : consider other size of buffer */
	wbuf[0] = addr;

	/* 2. I2C operation for reading data. */
	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 1;
	msg[1].buf = val;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret < 0) {
		cam_err("i2c transfer fail(%d)", ret);
		cam_info("I2CW08(0x%04X) [0x%04X] : 0x%04X\n", client->addr, addr, *val);
		goto p_err;
	}

#ifdef PRINT_I2CCMD
	cam_info("I2CR08(0x%04X) [0x%04X] : 0x%04X\n", client->addr, addr, *val);
#endif

p_err:
	return ret;
}

static int fimc_is_sr130pc20_write8(struct i2c_client *client, u8 addr, u8 val)
{
	int ret = 0;
	struct i2c_msg msg[1];
	u8 wbuf[3];

	if (!client->adapter) {
		cam_err("Could not find adapter!\n");
		ret = -ENODEV;
		goto p_err;
	}

	msg->addr = client->addr;
	msg->flags = 0;
	msg->len = 2;
	msg->buf = wbuf;
	wbuf[0] = addr;
	wbuf[1] = val;

	if (addr == 0xff) {
		if (val != 0xff) {
			cam_info("%s : use delay (%d ms) in I2C Write \n", __func__, val * 10);
			msleep(val * 10);
		}
	} else {
		ret = i2c_transfer(client->adapter, msg, 1);
		if (ret < 0) {
			cam_err("i2c transfer fail(%d)", ret);
			cam_info("I2CW08(0x%04X) [0x%04X] : 0x%04X\n", client->addr, addr, val);
		}
	}

#ifdef PRINT_I2CCMD
	cam_info("I2CW08(0x%04X) [0x%04X] : 0x%04X\n", client->addr, addr, val);
#endif

p_err:
	return ret;
}

#ifndef CONFIG_LOAD_FILE
static int sr130pc20_write_regs(struct v4l2_subdev *sd, const struct sensor_reg regs[], int size)
{
	int err = 0, i;
	struct i2c_client *client = to_client(sd);

	for (i = 0; i < size; i++) {
		err = fimc_is_sr130pc20_write8(client, regs[i].addr, regs[i].data);
		CHECK_ERR_MSG(err, "register set failed\n");
	}

	return 0;
}
#else
#define TABLE_MAX_NUM 500
static char *sr130pc20_regs_table;
static int sr130pc20_regs_table_size;

int sr130pc20_regs_table_init(void)
{
	struct file *filp;
	char *dp;
	long size;
	loff_t pos;
	int ret;
	mm_segment_t fs = get_fs();

	cam_info("%s : E\n", __func__);

	set_fs(get_ds());

	filp = filp_open(LOAD_FILE_PATH, O_RDONLY, 0);

	if (IS_ERR_OR_NULL(filp)) {
		cam_err("file open error\n");
		return PTR_ERR(filp);
	}

	size = filp->f_path.dentry->d_inode->i_size;
	cam_dbg("size = %ld\n", size);
	//dp = kmalloc(size, GFP_KERNEL);
	dp = vmalloc(size);
	if (unlikely(!dp)) {
		cam_err("Out of Memory\n");
		filp_close(filp, current->files);
		return -ENOMEM;
	}

	pos = 0;
	memset(dp, 0, size);
	ret = vfs_read(filp, (char __user *)dp, size, &pos);

	if (unlikely(ret != size)) {
		cam_err("Failed to read file ret = %d\n", ret);
		/*kfree(dp);*/
		vfree(dp);
		filp_close(filp, current->files);
		return -EINVAL;
	}

	filp_close(filp, current->files);

	set_fs(fs);

	sr130pc20_regs_table = dp;

	sr130pc20_regs_table_size = size;

	*((sr130pc20_regs_table + sr130pc20_regs_table_size) - 1) = '\0';

	cam_info("%s : X\n", __func__);
	return 0;
}

void sr130pc20_regs_table_exit(void)
{
	printk(KERN_DEBUG "%s %d\n", __func__, __LINE__);

	if (sr130pc20_regs_table) {
		vfree(sr130pc20_regs_table);
		sr130pc20_regs_table = NULL;
	}
}

#if !defined(CONFIG_LOAD_FILE)
static bool sr130pc20_is_hexnum(char *num)
{
	int i = 0;

	for (i = 2; num[i] != '\0'; i++) {
		if (!(((num[i] >= '0') && (num[5] <= '9'))
		    || ((num[5] >= 'a') && (num[5] <= 'f'))
		    || ((num[5] >= 'A') && (num[5] <= 'F'))))
			return false;
	}

	return true;
}
#endif

static int sr130pc20_write_regs_from_sd(struct v4l2_subdev *sd, const char *name)
{
	char *start = NULL, *end = NULL, *reg = NULL, *temp_start = NULL;
	u8 addr = 0, value = 0;
	u8 data = 0;
	char data_buf[5] = {0, };
	u32 len = 0;
	int err = 0;
	struct i2c_client *client = to_client(sd);

	cam_info("%s : E, sr130pc20_regs_table_size - %d\n", __func__, sr130pc20_regs_table_size);

	addr = value = 0;

	*(data_buf + 4) = '\0';

	start = strnstr(sr130pc20_regs_table, name, sr130pc20_regs_table_size);
	CHECK_ERR_COND_MSG(start == NULL, -ENODATA, "start is NULL\n");

	end = strnstr(start, "};", sr130pc20_regs_table_size);
	CHECK_ERR_COND_MSG(start == NULL, -ENODATA, "end is NULL\n");

	while (1) {
		len = end -start;
		temp_start = strnstr(start, "{", len);
		if (!temp_start || (temp_start > end)) {
			cam_info("write end of %s\n", name);
			break;
		}
		start = temp_start;

		len = end -start;
		/* Find Address */
		reg = strnstr(start, "0x", len);
		if (!reg || (reg > end)) {
			cam_info("write end of %s\n", name);
			break;
		}

		start = (reg + 4);

		/* Write Value to Address */
		memcpy(data_buf, reg, 4);

		err = kstrtou8(data_buf, 16, &data);
		CHECK_ERR_MSG(err, "kstrtou16 failed\n");

		addr = data;
		len = end -start;
		/* Find Data */
		reg = strnstr(start, "0x", len);
		if (!reg || (reg > end)) {
			cam_info("write end of %s\n", name);
			break;
		}

		/* Write Value to Address */
		memcpy(data_buf, reg, 4);

		err = kstrtou8(data_buf, 16, &data);
		CHECK_ERR_MSG(err, "kstrtou16 failed\n");

		value = data;

		err = fimc_is_sr130pc20_write8(client, addr, value);
		CHECK_ERR_MSG(err, "register set failed\n");
	}

	cam_info("%s : X\n", __func__);

	return (err > 0) ? 0 : err;
}
#endif


static int sensor_sr130pc20_apply_set(struct v4l2_subdev *sd,
	const char * setting_name, const struct sensor_regset *table)
{
	int ret = 0;

	cam_info("%s : E, setting_name : %s\n", __func__, setting_name);

#ifdef CONFIG_LOAD_FILE
	cam_info("COFIG_LOAD_FILE feature is enabled\n");
	ret = sr130pc20_write_regs_from_sd(sd, setting_name);
	CHECK_ERR_MSG(ret, "[COFIG_LOAD_FILE]regs set apply is fail\n");
#else
	ret = sr130pc20_write_regs(sd, table->reg, table->size);
	CHECK_ERR_MSG(ret, "regs set apply is fail\n");
#endif
	cam_info("%s : X\n", __func__);

	return ret;
}

static int __find_resolution(struct v4l2_subdev *subdev,
			     struct v4l2_mbus_framefmt *mf,
			     enum sr130pc20_oprmode *type,
			     u32 *resolution)
{
	struct sr130pc20_state *state = to_state(subdev);
	const struct sr130pc20_resolution *fsize = &sr130pc20_resolutions[0];
	const struct sr130pc20_resolution *match = NULL;

	enum sr130pc20_oprmode stype = state->oprmode;
	int i = ARRAY_SIZE(sr130pc20_resolutions);
	unsigned int min_err = ~0;
	int err;

	while (i--) {
		if (stype == fsize->type) {
			err = abs(fsize->width - mf->width)
				+ abs(fsize->height - mf->height);

			if (err < min_err) {
				min_err = err;
				match = fsize;
				stype = fsize->type;
			}
		}
		fsize++;
	}
	cam_info("LINE(%d): mf width: %d, mf height: %d, mf code: %d\n", __LINE__,
		mf->width, mf->height, stype);
	if (match) {
		mf->width  = match->width;
		mf->height = match->height;
		*resolution = match->value;
		*type = stype;
		cam_info("LINE(%d): match width: %d, match height: %d, match code: %d\n", __LINE__,
			match->width, match->height, stype);
		return 0;
	}
	cam_info("LINE(%d): mf width: %d, mf height: %d, mf code: %d\n", __LINE__,
		mf->width, mf->height, stype);

	return -EINVAL;
}

static struct v4l2_mbus_framefmt *__find_format(struct sr130pc20_state *state,
				struct v4l2_subdev_fh *fh,
				enum v4l2_subdev_format_whence which,
				enum sr130pc20_oprmode type)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return fh ? v4l2_subdev_get_try_format(fh, 0) : NULL;

	return &state->ffmt[type];
}

static const struct sr130pc20_framesize *sr130pc20_get_framesize
	(const struct sr130pc20_framesize *frmsizes,
	u32 frmsize_count, u32 index)
{
	int i = 0;

	for (i = 0; i < frmsize_count; i++) {
		if (frmsizes[i].index == index)
			return &frmsizes[i];
	}

	return NULL;
}

static void sr130pc20_set_framesize(struct v4l2_subdev *subdev,
				const struct sr130pc20_framesize *frmsizes,
				u32 num_frmsize, bool preview)
{
	struct sr130pc20_state *state = to_state(subdev);
	const struct sr130pc20_framesize **found_frmsize = NULL;
	u32 width = state->req_fmt.width;
	u32 height = state->req_fmt.height;
	int i = 0;

	cam_info("%s: Requested Res %dx%d\n", __func__, width, height);

	found_frmsize = preview ?
		&state->preview.frmsize : &state->capture.frmsize;

	for (i = 0; i < num_frmsize; i++) {
		if ((frmsizes[i].width == width) &&
			(frmsizes[i].height == height)) {
			*found_frmsize = &frmsizes[i];
			break;
		}
	}

	if (*found_frmsize == NULL) {
		cam_err("%s: error, invalid frame size %dx%d\n",
			__func__, width, height);
		*found_frmsize = preview ?
			sr130pc20_get_framesize(frmsizes, num_frmsize,
					PREVIEW_SZ_SVGA) :
			sr130pc20_get_framesize(frmsizes, num_frmsize,
					CAPTURE_SZ_1MP);
		//BUG_ON(!(*found_frmsize));
	}

	if (preview)
		cam_info("Preview Res Set: %dx%d, index %d\n",
			(*found_frmsize)->width, (*found_frmsize)->height,
			(*found_frmsize)->index);
	else
		cam_info("Capture Res Set: %dx%d, index %d\n",
			(*found_frmsize)->width, (*found_frmsize)->height,
			(*found_frmsize)->index);
}

static int sr130pc20_set_preview_size(struct v4l2_subdev *subdev)
{
	struct sr130pc20_state *state = to_state(subdev);
	int err = 0;

	u32 width, height;

	if (!state->preview.update_frmsize)
		return 0;

	if (unlikely(!state->preview.frmsize)) {
		cam_warn("warning, preview resolution not set\n");
		state->preview.frmsize = sr130pc20_get_framesize(
					sr130pc20_preview_frmsizes,
					ARRAY_SIZE(sr130pc20_preview_frmsizes),
					PREVIEW_SZ_SVGA);
	}

	width = state->preview.frmsize->width;
	height = state->preview.frmsize->height;

	cam_info("set preview size(%dx%d)\n", width, height);

	if (state->sensor_mode == SENSOR_MOVIE) {
		if (width == 640 && height == 480) {
			err = sensor_sr130pc20_apply_set(subdev, "sr130pc20_resol_640_480",
				&sr130pc20_regset_table.resol_640_480);
		} else if (width == 320 && height == 240) {
			err = sensor_sr130pc20_apply_set(subdev, "sr130pc20_resol_320_240",
				&sr130pc20_regset_table.resol_320_240);
		} else { // 176 x 144
			err = sensor_sr130pc20_apply_set(subdev, "sr130pc20_resol_176_144",
				&sr130pc20_regset_table.resol_176_144);
		}
		cam_info("sensor_mode is movie\n");
	} else {
		if (width == 176 && height == 144) {
			err = sensor_sr130pc20_apply_set(subdev, "sr130pc20_resol_176_144",
				&sr130pc20_regset_table.resol_176_144);
		} else if(width == 352 && height == 288) { /* VT Preview size */
			err = sensor_sr130pc20_apply_set(subdev, "sr130pc20_resol_352_288",
				&sr130pc20_regset_table.resol_352_288);
		} else if(width == 320 && height == 240) {
			err = sensor_sr130pc20_apply_set(subdev, "sr130pc20_resol_320_240",
				&sr130pc20_regset_table.resol_320_240);
		} else if (width == 640 && height == 480) {
			err = sensor_sr130pc20_apply_set(subdev, "sr130pc20_resol_640_480",
				&sr130pc20_regset_table.resol_640_480);
		} else {
			err = sensor_sr130pc20_apply_set(subdev, "sr130pc20_resol_800_600",
				&sr130pc20_regset_table.resol_800_600);
		}
	}
	CHECK_ERR_MSG(err, "fail to set preview size\n");

	state->preview.update_frmsize = 0;

	return 0;
}
static int sr130pc20_set_frame_rate(struct v4l2_subdev *subdev, s32 fps)
{
	struct sr130pc20_state *state = to_state(subdev);
	int min = FRAME_RATE_AUTO;
	int max = FRAME_RATE_30;
	int ret = 0;
	int i = 0, fps_index = -1;

	cam_info("set frame rate %d\n", fps);

	if ((fps < min) || (fps > max)) {
		cam_warn("set_frame_rate: error, invalid frame rate %d\n", fps);
		fps = (fps < min) ? min : max;
	}

	if (unlikely(!state->initialized)) {
		cam_info("pending fps %d\n", fps);
		state->req_fps = fps;
		return 0;
	}

	if (state->sensor_mode == SENSOR_MOVIE) {
		fps = FRAME_RATE_25;
		cam_info("change frame rate to %d fps for recording\n", fps);
	}

	for (i = 0; i < ARRAY_SIZE(sr130pc20_framerates); i++) {
		if (fps == sr130pc20_framerates[i].fps) {
			fps_index = sr130pc20_framerates[i].index;
			state->req_fps = -1;
			break;
		}
	}

	if (unlikely(fps_index < 0)) {
		cam_warn("set_fps: warning, not supported fps %d\n", fps);
		return 0;
	}

	if (fps == state->fps) {
		cam_info("set_fps: fps:%d, state->fps:%d\n", fps, state->fps);
		return 0;
	}

	switch (fps) {
	case FRAME_RATE_AUTO :
		ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_fps_auto",
			&sr130pc20_regset_table.fps_auto);
		break;
	case FRAME_RATE_7 :
		ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_fps_7",
			&sr130pc20_regset_table.fps_7);
		break;
	case FRAME_RATE_15 :
		ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_fps_15",
			&sr130pc20_regset_table.fps_15);
		break;
	case FRAME_RATE_25:
		ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_fps_25_camcorder",
			&sr130pc20_regset_table.fps_25_camcorder);
		break;
	case FRAME_RATE_30:
		ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_fps_30_camcorder",
			&sr130pc20_regset_table.fps_30_camcorder);
		break;
	default:
		ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_fps_auto",
			&sr130pc20_regset_table.fps_auto);
		break;
	}

	state->fps = fps;
	CHECK_ERR_MSG(ret, "fail to set framerate\n");

	return 0;
}

static int sr130pc20_set_effect(struct v4l2_subdev *subdev, int effect)
{
	struct sr130pc20_state *state = to_state(subdev);
	int ret = 0;

	cam_info("%s( %d ) : E\n",__func__, effect);

	switch (effect) {
	case V4L2_IMAGE_EFFECT_BASE :
	case V4L2_IMAGE_EFFECT_NONE :
		ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_Effect_Normal",
			&sr130pc20_regset_table.effect_none);
		break;
	case V4L2_IMAGE_EFFECT_BNW :
		ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_Effect_Gray",
			&sr130pc20_regset_table.effect_gray);
		break;
	case V4L2_IMAGE_EFFECT_SEPIA :
		ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_Effect_Sepia",
			&sr130pc20_regset_table.effect_sepia);
		break;
	case V4L2_IMAGE_EFFECT_NEGATIVE :
		ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_Effect_Negative",
			&sr130pc20_regset_table.effect_negative);
		break;
	case V4L2_IMAGE_EFFECT_AQUA :
		ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_Effect_Aqua",
			&sr130pc20_regset_table.effect_aqua);
		break;
	default:
		cam_dbg("%s: Not support.(%d)\n", __func__, effect);
		break;
	}

	state->effect = effect;
	return ret;
}

static int sr130pc20_set_anti_banding(struct v4l2_subdev *subdev, int anti_banding)
{
	struct sr130pc20_state *state = to_state(subdev);
	int ret = 0;

	cam_info("%s( %d ) : E\n",__func__, anti_banding);

	switch (anti_banding) {
	case ANTI_BANDING_AUTO :
	case ANTI_BANDING_OFF :
	case ANTI_BANDING_50HZ :
		ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_anti_banding_flicker_50hz",
			&sr130pc20_regset_table.anti_banding_flicker_50hz);
		break;
	case ANTI_BANDING_60HZ :
		ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_anti_banding_flicker_60hz",
			&sr130pc20_regset_table.anti_banding_flicker_60hz);
		break;
	default:
		cam_dbg("%s: Not support.(%d)\n", __func__, anti_banding);
		break;
	}

	state->anti_banding = anti_banding;
	return ret;
}

static int sr130pc20_set_metering(struct v4l2_subdev *subdev, int metering)
{
	struct sr130pc20_state *state = to_state(subdev);
	int ret = 0;

	cam_info("%s( %d ) : E\n",__func__, metering);

	switch (metering) {
	case METERING_MATRIX:
		ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_metering_matrix",
			&sr130pc20_regset_table.metering_matrix);
		break;
	case METERING_CENTER:
		ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_metering_center",
			&sr130pc20_regset_table.metering_center);
		break;
	case METERING_SPOT:
		ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_metering_spot",
			&sr130pc20_regset_table.metering_spot);
		break;
	default:
		cam_dbg("%s: Not support.(%d)\n", __func__, metering);
		break;
	}

	state->metering = metering;
	return ret;
}

static int sr130pc20_set_brightness(struct v4l2_subdev *subdev, int brightness)
{
	struct sr130pc20_state *state = to_state(subdev);
	int ret = 0;

	cam_info("%s( %d ) : E\n",__func__, brightness);

	switch (brightness) {
	case EV_MINUS_4:
		ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_brightness_m4",
			&sr130pc20_regset_table.brightness_m4);
		break;
	case EV_MINUS_3:
		ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_brightness_m3",
			&sr130pc20_regset_table.brightness_m3);
		break;
	case EV_MINUS_2:
		ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_brightness_m2",
			&sr130pc20_regset_table.brightness_m2);
		break;
	case EV_MINUS_1:
		ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_brightness_m1",
			&sr130pc20_regset_table.brightness_m1);
		break;
	case EV_DEFAULT:
		ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_brightness_default",
			&sr130pc20_regset_table.brightness_default);
		break;
	case EV_PLUS_1:
		ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_brightness_p1",
			&sr130pc20_regset_table.brightness_p1);
		break;
	case EV_PLUS_2:
		ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_brightness_p2",
			&sr130pc20_regset_table.brightness_p2);
		break;
	case EV_PLUS_3:
		ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_brightness_p3",
			&sr130pc20_regset_table.brightness_p3);
		break;
	case EV_PLUS_4:
		ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_brightness_p4",
			&sr130pc20_regset_table.brightness_p4);
		break;
	default:
		cam_dbg("%s: Not support.(%d)\n", __func__, brightness);
		break;
	}

	state->brightness = brightness;
	return ret;
}

static int sr130pc20_set_contrast(struct v4l2_subdev *subdev, int contrast)
{
	struct sr130pc20_state *state = to_state(subdev);
	int ret = 0;

	cam_info("%s( %d ) : E\n",__func__, contrast);

	switch (contrast) {
	case CONTRAST_MINUS_2:
		ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_contrast_m2",
			&sr130pc20_regset_table.contrast_m2);
		break;
	case CONTRAST_MINUS_1:
		ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_contrast_m1",
			&sr130pc20_regset_table.contrast_m1);
		break;
	case CONTRAST_DEFAULT:
		ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_contrast_default",
			&sr130pc20_regset_table.contrast_default);
		break;
	case CONTRAST_PLUS_1:
		ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_contrast_p1",
			&sr130pc20_regset_table.contrast_p1);
		break;
	case CONTRAST_PLUS_2:
		ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_contrast_p2",
			&sr130pc20_regset_table.contrast_p2);
		break;
	default:
		cam_dbg("%s: Not support.(%d)\n", __func__, contrast);
		break;
	}

	state->contrast = contrast;
	return ret;
}

static int sr130pc20_set_wb(struct v4l2_subdev *subdev, int wb)
{
	struct sr130pc20_state *state = to_state(subdev);
	int ret = 0;

	cam_info("%s( %d ) : E\n",__func__, wb);

	switch (wb) {
	case WHITE_BALANCE_BASE:
	case WHITE_BALANCE_AUTO:
		ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_wb_auto",
			&sr130pc20_regset_table.wb_auto);
		break;
	case WHITE_BALANCE_SUNNY:
		ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_wb_daylight",
			&sr130pc20_regset_table.wb_daylight);
		break;
	case WHITE_BALANCE_CLOUDY:
		ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_wb_cloudy",
			&sr130pc20_regset_table.wb_cloudy);
		break;
	case WHITE_BALANCE_TUNGSTEN:
		ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_wb_incandescen",
			&sr130pc20_regset_table.wb_incandescent);
		break;
	case WHITE_BALANCE_FLUORESCENT:
		ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_wb_fluorescent",
			&sr130pc20_regset_table.wb_fluorescent);
		break;
	default:
		cam_dbg("%s: Not support.(%d)\n", __func__, wb);
		break;
	}

	state->white_balance = wb;
	return ret;
}

static int sr130pc20_set_hflip(struct v4l2_subdev *subdev)
{
	int ret = 0;

	cam_info("[%s:%d] E \n",__func__, __LINE__);

	ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_HFlip",
		&sr130pc20_regset_table.hflip);

	return ret;
}

static int sr130pc20_set_vflip(struct v4l2_subdev *subdev)
{
	int ret = 0;

	cam_info("[%s:%d] E \n",__func__, __LINE__);

	ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_VFlip",
		&sr130pc20_regset_table.vflip);

	return ret;
}

static int sr130pc20_set_flip_off(struct v4l2_subdev *subdev)
{
	int ret = 0;

	cam_info("[%s:%d] E \n",__func__, __LINE__);

	ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_Flip_Off",
		&sr130pc20_regset_table.flip_off);

	return ret;
}

#if 0
static int sr130pc20_set_camcorder_flip(struct v4l2_subdev *subdev)
{
	int ret = 0;

	cam_info("[%s:%d] E \n",__func__, __LINE__);

	ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_Camcorder_Flip",
		&sr130pc20_regset_table.camcorder_flip);

	return ret;
}
#endif

static int sr130pc20_set_vt_mode(struct v4l2_subdev *subdev, int vt_mode)
{
	struct sr130pc20_state *state = to_state(subdev);
	int ret = 0;

	cam_info("%s( %d ) : E\n",__func__, vt_mode);

	switch (vt_mode) {
	case VT_MODE_OFF:
		ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_Init_Reg",
			&sr130pc20_regset_table.init);
		break;
	case VT_MODE_1:
		ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_Init_VT_Reg_",
			&sr130pc20_regset_table.init_vt);
		break;
	default:
		cam_dbg("%s: Not support.(%d)\n", __func__, vt_mode);
		break;
	}

	state->vt_mode = vt_mode;
	return ret;
}

int sensor_sr130pc20_stream_on(struct v4l2_subdev *subdev)
{
	int ret = 0;
	struct sr130pc20_state *state = to_state(subdev);

	cam_info("stream on - Mode check = %d\n", state->runmode);

	ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_start_stream",
		&sr130pc20_regset_table.start_stream);

	if(state->runmode == RUNMODE_RECORDING_STOP) {
		cam_info("RECORDING STOP MINIMUM DELAY 450\n");
		msleep(450);
	}

	return ret;
}

int sensor_sr130pc20_stream_off(struct v4l2_subdev *subdev)
{
	int ret = 0;
	struct sr130pc20_state *state = to_state(subdev);

	cam_info("stream off\n");
	ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_stop_stream",
		&sr130pc20_regset_table.stop_stream);
	state->preview.update_frmsize = 1;
	state->skip_set_vga_size = false;

	switch (state->runmode) {
	case RUNMODE_CAPTURING:
		cam_dbg("Capture Stop!\n");
		state->runmode = RUNMODE_CAPTURING_STOP;
		ret  = sr130pc20_set_flip_off(subdev);
		break;

	case RUNMODE_RUNNING:
		cam_dbg("Preview Stop!\n");
		state->runmode = RUNMODE_RUNNING_STOP;
		break;

	case RUNMODE_RECORDING:
		cam_dbg("Recording Stop!\n");
		state->runmode = RUNMODE_RECORDING_STOP;
		break;

	default:
		break;
	}

	return ret;
}

static int sr130pc20_set_sensor_mode(struct v4l2_subdev *subdev, s32 val)
{
	struct sr130pc20_state *state = to_state(subdev);

	cam_info("mode=%d\n", val);

	switch (val) {
	case SENSOR_MOVIE:
		/* We does not support movie mode when in VT. */
		if (state->vt_mode) {
			state->sensor_mode = SENSOR_CAMERA;
			cam_warn("%s: error, Not support movie\n", __func__);
		} else {
			state->sensor_mode = SENSOR_MOVIE;
		}
		break;
	case SENSOR_CAMERA:
		state->sensor_mode = SENSOR_CAMERA;
		break;

	default:
		cam_err("%s: error, Not support.(%d)\n", __func__, val);
		state->sensor_mode = SENSOR_CAMERA;
		break;
	}

	return 0;
}

static inline int sr130pc20_get_exif_exptime(struct v4l2_subdev *subdev, u32 *exp_time)
{
	struct i2c_client *client = to_client(subdev);

	int err = 0;
	u8 read_value1 = 0;
	u8 read_value2 = 0;
	u8 read_value3 = 0;
	int OPCLK = 26000000;

	err = fimc_is_sr130pc20_write8(client, 0x03, 0x20);
	CHECK_ERR_COND(err < 0, -ENODEV);

	fimc_is_sr130pc20_read8(client, 0x80, &read_value1);
	fimc_is_sr130pc20_read8(client, 0x81, &read_value2);
	fimc_is_sr130pc20_read8(client, 0x82, &read_value3);

	cam_dbg("exposure time read_value %d, %d, %d\n",
		read_value1, read_value2, read_value3);

	if(read_value1 == 0 && read_value2 == 0 && read_value3 == 0)
	{
		*exp_time = 0;
		err = -EFAULT;
	} else {
		*exp_time = OPCLK / ((read_value1 << 19) + (read_value2 << 11) + (read_value3 << 3));
		cam_dbg("exposure time %u\n", *exp_time);
	}

	return err;
}

static inline int sr130pc20_get_exif_iso(struct v4l2_subdev *subdev, u16 *iso)
{
	struct i2c_client *client = to_client(subdev);
	int err = 0;
	u8 read_value = 0;

	err = fimc_is_sr130pc20_write8(client, 0x03, 0x20);
	CHECK_ERR_COND(err < 0, -ENODEV);
	fimc_is_sr130pc20_read8(client, 0xb0, &read_value);

	cam_dbg("iso : read_value=%d\n", read_value);

	if (read_value < 0x15)
		*iso = 50;
	else if (read_value < 0x35)
		*iso = 100;
	else if (read_value < 0x45)
		*iso = 200;
	else
		*iso = 400;

	cam_dbg("ISO=%d\n", *iso);
	return 0;
}

static inline void sr130pc20_get_exif_flash(struct v4l2_subdev *subdev,
					u16 *flash)
{
	*flash = 0;
}

static int sr130pc20_get_exif(struct v4l2_subdev *subdev)
{
	struct sr130pc20_state *state = to_state(subdev);

	/* exposure time */
	state->exif.exp_time_den = 0;
	sr130pc20_get_exif_exptime(subdev, &state->exif.exp_time_den);
	/* iso */
	state->exif.iso = 0;
	sr130pc20_get_exif_iso(subdev, &state->exif.iso);
	/* flash */
	sr130pc20_get_exif_flash(subdev, &state->exif.flash);

	return 0;
}

static int sensor_sr130pc20_init(struct v4l2_subdev *subdev, u32 val)
{
	int ret = 0;
	u8 id;

	struct sr130pc20_state *state = to_state(subdev);
	struct i2c_client *client = to_client(subdev);

	cam_info("%s : E \n", __func__);

	state->system_clock = 146 * 1000 * 1000;
	state->line_length_pck = 146 * 1000 * 1000;

#ifdef CONFIG_LOAD_FILE
	ret = sr130pc20_regs_table_init();
	CHECK_ERR_MSG(ret, "[CONFIG_LOAD_FILE] init fail \n");
#endif
	ret = fimc_is_sr130pc20_read8(client, 0x4, &id);
	if (ret < 0) {
		cam_err("%s: read id failed\n", __func__);
		goto p_err;
	}

	ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_Init_Reg",
		&sr130pc20_regset_table.init);
	if (ret) {
		cam_err("%s: write cmd failed\n", __func__);
		goto p_err;
	}

	ret = sensor_sr130pc20_apply_set(subdev, "sr130pc20_stop_stream",
		&sr130pc20_regset_table.stop_stream);
	if (ret) {
		cam_err("%s: write cmd failed\n", __func__);
		goto p_err;
	}

	state->initialized = 1;
	state->power_on =  SR130PC20_HW_POWER_ON;
	state->runmode = RUNMODE_INIT;
	state->preview.update_frmsize = 1;
	state->sensor_mode = SENSOR_CAMERA;
	state->contrast = CONTRAST_DEFAULT;
	state->effect = V4L2_IMAGE_EFFECT_NONE;
	state->metering = METERING_MATRIX;
	state->white_balance = WHITE_BALANCE_AUTO;
	state->fps = FRAME_RATE_AUTO;
	state->req_fps = FRAME_RATE_AUTO;
	state->recording = 0;
	state->vt_mode = VT_MODE_OFF;
	state->anti_banding = ANTI_BANDING_AUTO;
	state->brightness = EV_DEFAULT;
	state->hflip = false;
	state->vflip = false;
	state->skip_set_vga_size = false;

	cam_info("%s(id : %X) : X \n", __func__, id);

p_err:
	return ret;
}


static int sr130pc20_s_power(struct v4l2_subdev *subdev, int on)
{
	struct sr130pc20_state *state = to_state(subdev);
	int tries = 3;
	int err = 0;

	cam_info("sr130pc20_s_power=%d\n", on);

	if (on) {

		state->runmode = RUNMODE_NOTREADY;

		if (!state->initialized) {
retry:
			err = sensor_sr130pc20_init(subdev, 0);
			if (err && --tries) {
				cam_err("s_stream: retry to init...\n");
				// err = sr130pc20_reset(subdev, 1);
				cam_err("s_stream: power-on failed\n");
				goto retry;
			}
		}
	} else {
		state->initialized = 0;
		state->power_on = SR130PC20_HW_POWER_OFF;
	}

	return err;
}

static int sensor_sr130pc20_s_capture_mode(struct v4l2_subdev *subdev, int value)
{
	struct sr130pc20_state *state = to_state(subdev);
	int ret = 0;

	cam_info("%s : value(%d) - E\n",__func__, value);

	if ((SENSOR_CAMERA == state->sensor_mode) && value) {
		cam_info("s_ctrl : Capture Mode!\n");
		state->format_mode = V4L2_PIX_FMT_MODE_CAPTURE;
	} else {
		cam_info("s_ctrl : Preview Mode!\n");
		state->format_mode = V4L2_PIX_FMT_MODE_PREVIEW;
	}

	return ret;
}

static int sr130pc20_set_capture_size(struct v4l2_subdev *subdev)
{
	struct sr130pc20_state *state = to_state(subdev);

	u32 width, height;
	int err = 0;

	cam_info("%s - E\n",__func__);

	if (unlikely(!state->capture.frmsize)) {
		cam_warn("warning, capture resolution not set\n");
		state->capture.frmsize = sr130pc20_get_framesize(
					sr130pc20_capture_frmsizes,
					ARRAY_SIZE(sr130pc20_capture_frmsizes),
					CAPTURE_SZ_1MP);
	}

	width = state->capture.frmsize->width;
	height = state->capture.frmsize->height;
	state->preview.update_frmsize = 1;

	if (state->fps != FRAME_RATE_AUTO)
		err = sensor_sr130pc20_apply_set(subdev, "sr130pc20_fps_auto",
			&sr130pc20_regset_table.fps_auto);

	/* Transit to capture mode */
	if (state->capture.lowlux_night) {
		cam_info("capture_mode: night lowlux\n");
	/*	err = sensor_sr130pc20_apply_set(client, &sr130pc20_regset_table.lowcapture);*/
	}

	cam_info("%s - width * height = %d * %d \n",__func__, width, height);
	if(width == 1280 && height == 960)
		err = sensor_sr130pc20_apply_set(subdev, "sr130pc20_resol_1280_960_capture",
			&sr130pc20_regset_table.resol_1280_960_capture);
	else if(width == 640 && height == 480)
		err = sensor_sr130pc20_apply_set(subdev, "sr130pc20_resol_640_480_capture",
			&sr130pc20_regset_table.resol_640_480_capture);
	else if(width == 320 && height == 240)
		err = sensor_sr130pc20_apply_set(subdev, "sr130pc20_resol_320_240_capture",
			&sr130pc20_regset_table.resol_320_240_capture);
	else // 176x144
		err = sensor_sr130pc20_apply_set(subdev, "sr130pc20_resol_176_144_capture",
			&sr130pc20_regset_table.resol_176_144_capture);

	CHECK_ERR_MSG(err, "fail to capture_mode (%d)\n", err);

	cam_info("%s - X\n",__func__);

	return err;
}

static int sr130pc20_start_capture(struct v4l2_subdev *subdev)
{
	struct sr130pc20_state *state = to_state(subdev);
	int err = 0;
	u32 capture_delay;

	cam_info("%s: E\n", __func__);


	err = sr130pc20_set_capture_size(subdev);
	CHECK_ERR(err);

	if (state->hflip == 1)
		err  = sr130pc20_set_hflip(subdev);
	else if (state->vflip == 1)
		err  = sr130pc20_set_vflip(subdev);

	err = sensor_sr130pc20_stream_on(subdev);
	CHECK_ERR(err);

	state->runmode = RUNMODE_CAPTURING;
	capture_delay = 10;
	msleep(capture_delay);

	/* Get EXIF */
	sr130pc20_get_exif(subdev);

	cam_info("%s: X\n", __func__);

	return 0;
}


static int sr130pc20_start_preview(struct v4l2_subdev *subdev)
{
	struct sr130pc20_state *state = to_state(subdev);
	int err = 0;

	cam_info("Camera Preview start : E - runmode = %d\n", state->runmode);

	if ((state->runmode == RUNMODE_NOTREADY) ||
	    (state->runmode == RUNMODE_CAPTURING)) {
		cam_err("%s: error - Invalid runmode\n", __func__);
		return -EPERM;
	}

	/* Check pending fps */
	if (state->req_fps >= 0) {
		err = sr130pc20_set_frame_rate(subdev, state->req_fps);
		CHECK_ERR(err);
	}

	/* Set preview size */
	err = sr130pc20_set_preview_size(subdev);
	CHECK_ERR_MSG(err, "failed to set preview size(%d)\n", err);

	err = sr130pc20_set_effect(subdev, state->effect);
	CHECK_ERR(err);

	err = sensor_sr130pc20_stream_on(subdev);
	CHECK_ERR(err);

	if (RUNMODE_INIT == state->runmode)
		msleep(100);

	state->runmode = (state->sensor_mode == SENSOR_CAMERA) ?
			RUNMODE_RUNNING : RUNMODE_RECORDING;

	cam_info("Camera Preview start : X - runmode = %d\n", state->runmode);
	return 0;
}

static int sensor_sr130pc20_s_ctrl(struct v4l2_subdev *subdev, struct v4l2_control *ctrl)
{
	struct sr130pc20_state *state = to_state(subdev);
	int ret = 0;

	if (!state->initialized && ctrl->id != V4L2_CID_CAMERA_SENSOR_MODE) {
		cam_warn("s_ctrl: warning, camera not initialized. ID %d(0x%08X)\n",
			ctrl->id & 0xFF, ctrl->id);
		return 0;
	}

	switch (ctrl->id) {
	case V4L2_CID_CAMERA_SENSOR_MODE:
		cam_info("V4L2_CID_CAMERA_SENSOR_MODE : %d\n", ctrl->value);
		 sr130pc20_set_sensor_mode(subdev, ctrl->value);
		break;
	case V4L2_CID_CAMERA_INIT:
		cam_info("V4L2_CID_CAMERA_INIT\n");
/*  // Move to set_power function
		ret = sensor_sr130pc20_init(subdev, 0);
		if (ret) {
			cam_err("sensor_sr130pc20_init is fail(%d)", ret);
			goto p_err;
		}
*/
		break;
	case V4L2_CID_CAPTURE:
		cam_info("V4L2_CID_CAPTURE : %d \n", ctrl->value);
		ret = sensor_sr130pc20_s_capture_mode(subdev, ctrl->value);
		if (ret) {
			cam_err("sensor_sr130pc20_s_capture_mode is fail(%d)", ret);
			goto p_err;
		}
		break;
	case V4L2_CID_CAMERA_SET_SNAPSHOT_SIZE:
/* capture size of SoC sensor is to be set up in start_capture.
		cam_info("V4L2_CID_CAMERA_SET_SNAPSHOT_SIZE : %d\n", ctrl->value);
		ret = sr130pc20_set_capture_size(subdev, ctrl->value);
		if (ret) {
			cam_err("sr130pc20_set_snapshot_size is fail(%d)", ret);
			goto p_err;
		}
*/
		break;
	case V4L2_CID_IMAGE_EFFECT:
	case V4L2_CID_CAMERA_EFFECT:
		cam_info("V4L2_CID_IMAGE_EFFECT :%d\n", ctrl->value);
		ret = sr130pc20_set_effect(subdev, ctrl->value);
		break;
	case V4L2_CID_HFLIP:
		cam_info("V4L2_CID_HFLIP : %d\n", ctrl->value);
		state->hflip = ctrl->value;
		break;
	case V4L2_CID_VFLIP:
		cam_info("V4L2_CID_VFLIP : %d\n", ctrl->value);
		state->vflip = ctrl->value;
		break;
	case V4L2_CID_CAMERA_ANTI_BANDING:
		cam_info("V4L2_CID_CAMERA_ANTI_BANDING :%d\n", ctrl->value);
		ret = sr130pc20_set_anti_banding(subdev, ctrl->value);
		break;
	case V4L2_CID_IS_CAMERA_METERING:
	case V4L2_CID_CAMERA_METERING:
		cam_info("V4L2_CID_CAMERA_METERING :%d\n", ctrl->value);
		ret = sr130pc20_set_metering(subdev, ctrl->value);
		break;
	case V4L2_CID_CAMERA_BRIGHTNESS:
	case V4L2_CID_CAM_BRIGHTNESS:
		cam_info("V4L2_CID_CAMEAR_BRIGHTNESS :%d\n", ctrl->value);
		ret = sr130pc20_set_brightness(subdev, ctrl->value);
		break;
	case V4L2_CID_CAMERA_CONTRAST:
	case V4L2_CID_CAM_CONTRAST:
		cam_info("V4L2_CID_CAMEAR_CONTRAST :%d\n", ctrl->value);
		ret = sr130pc20_set_contrast(subdev, ctrl->value);
		break;
	case V4L2_CID_WHITE_BALANCE_PRESET:
		cam_info("V4L2_CID_WHITE_BALANCE_PRESET :%d\n", ctrl->value);
		ret = sr130pc20_set_wb(subdev, ctrl->value);
		break;
	case V4L2_CID_CAM_FRAME_RATE:
		cam_info("V4L2_CID_CAM_FRAME_RATE :%d\n", ctrl->value);
		ret = sr130pc20_set_frame_rate(subdev, ctrl->value);
		break;
	case V4L2_CID_CAMERA_VT_MODE:
		cam_info("V4L2_CID_CAMERA_VT_MODE :%d\n", ctrl->value);
		ret = sr130pc20_set_vt_mode(subdev, ctrl->value);
		break;
	case V4L2_CID_CAMERA_ISO:
	case V4L2_CID_CAMERA_AEAWB_LOCK_UNLOCK:
	case V4L2_CID_CAMERA_SET_POSTVIEW_SIZE:
	case V4L2_CID_JPEG_QUALITY:
	case V4L2_CID_CAMERA_FOCUS_MODE:
	case V4L2_CID_CAMERA_POWER_OFF:
		cam_dbg("s_ctrl : unsupported id is requested. ID (0x%08X)\n", ctrl->id);
		break;
	default:
		cam_warn("s_ctrl : invalid id is requested. ID (0x%08X)\n", ctrl->id);
		break;
	}

p_err:
	return ret;
}

static int sensor_sr130pc20_g_ctrl(struct v4l2_subdev *subdev, struct v4l2_control *ctrl)
{
	struct sr130pc20_state *state = to_state(subdev);
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_EXIF_EXPOSURE_TIME_DEN:
		if (state->sensor_mode == SENSOR_CAMERA) {
			ctrl->value = state->exif.exp_time_den;
		} else {
			ctrl->value = 26;
		}
		cam_dbg("V4L2_CID_CAMERA_EXIF_EXPTIME :%d\n", ctrl->value);
		break;
	case V4L2_CID_EXIF_EXPOSURE_TIME_NUM:
		ctrl->value = 1;
		cam_dbg("V4L2_CID_EXIF_EXPOSURE_TIME_NUM :%d\n", ctrl->value);
		break;
	case V4L2_CID_CAMERA_EXIF_FLASH:
		ctrl->value = state->exif.flash;
		cam_dbg("V4L2_CID_CAMERA_EXIF_FLASH :%d\n", ctrl->value);
		break;
	case V4L2_CID_CAMERA_EXIF_ISO:
		if (state->sensor_mode == SENSOR_CAMERA) {
			ctrl->value = state->exif.iso;
		} else {
			ctrl->value = 100;
		}
		cam_dbg("V4L2_CID_CAMERA_EXIF_ISO :%d\n", ctrl->value);
		break;
	case V4L2_CID_CAMERA_EXIF_BV:
		cam_dbg("g_ctrl : unsupported id is requested. ID (0x%08X)\n", ctrl->id);
		break;
	default:
		cam_warn("g_ctrl : invalid ioctl(0x%08X) is requested", ctrl->id);
		break;
	}

	return ret;
}

static int sensor_sr130pc20_g_ext_ctrls(struct v4l2_subdev *subdev, struct v4l2_ext_controls *ctrls)
{
	struct v4l2_ext_control *ctrl = ctrls->controls;
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_CAM_SENSOR_FW_VER:
		strcpy(ctrl->string, SENSOR_VER);
		cam_dbg("V4L2_CID_CAM_SENSOR_FW_VER :%s\n", ctrl->string);
		break;
	default:
		cam_warn("g_ext_ctrl : invalid ioctl(0x%08X) is requested", ctrl->id);
		break;
	}

	return ret;
}

static int sr130pc20_s_mbus_fmt(struct v4l2_subdev *subdev, struct v4l2_mbus_framefmt *fmt)
{
	struct sr130pc20_state *state = to_state(subdev);
	s32 previous_index = 0;

	BUG_ON(!subdev);
	BUG_ON(!fmt);

	cam_info("%s: E\n", __func__);

	cam_info("%s: pixelformat = 0x%x, colorspace = 0x%x, width = %d, height = %d\n",
		__func__, fmt->code, fmt->colorspace, fmt->width, fmt->height);

	v4l2_fill_pix_format(&state->req_fmt, fmt);

	if (state->format_mode != V4L2_PIX_FMT_MODE_CAPTURE) {
		previous_index = state->preview.frmsize ?
				state->preview.frmsize->index : -1;
		sr130pc20_set_framesize(subdev, sr130pc20_preview_frmsizes,
			ARRAY_SIZE(sr130pc20_preview_frmsizes), true);

		if (state->preview.frmsize) {
			if (previous_index != state->preview.frmsize->index)
				state->preview.update_frmsize = 1;
		}
	} else {
		sr130pc20_set_framesize(subdev, sr130pc20_capture_frmsizes,
			ARRAY_SIZE(sr130pc20_capture_frmsizes), false);
	}

	cam_info("output size : %d x %d\n", fmt->width, fmt->height);

	return 0;
}

static int sr130pc20_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct sr130pc20_state *state = to_state(subdev);
	int err = 0;

	cam_info("stream mode = %d\n", enable);

	switch (enable) {
	case STREAM_MODE_CAM_OFF:
		err = sensor_sr130pc20_stream_off(subdev);
		break;

	case STREAM_MODE_CAM_ON:
		if (state->format_mode == V4L2_PIX_FMT_MODE_CAPTURE)
			err = sr130pc20_start_capture(subdev);
		else
			err = sr130pc20_start_preview(subdev);
		break;

	case STREAM_MODE_MOVIE_OFF:
		cam_info("movie off");
		state->recording = 0;
		break;

	case STREAM_MODE_MOVIE_ON:
		cam_info("movie on");
		state->recording = 1;
		break;

	default:
		cam_err("%s: error - Invalid stream mode\n", __func__);
		break;
	}

	return err;
}

static int sr130pc20_s_param(struct v4l2_subdev *subdev, struct v4l2_streamparm *param)
{
	struct sr130pc20_state *state = to_state(subdev);
	s32 req_fps;

	req_fps = param->parm.capture.timeperframe.denominator /
			param->parm.capture.timeperframe.numerator;

	cam_info("s_parm state->fps=%d, state->req_fps=%d\n",
		state->fps, req_fps);

	return sr130pc20_set_frame_rate(subdev, req_fps);
}

static const struct v4l2_subdev_core_ops core_ops = {
	.s_power	= sr130pc20_s_power,
	.init		= sensor_sr130pc20_init,
	.s_ctrl		= sensor_sr130pc20_s_ctrl,
	.g_ctrl		= sensor_sr130pc20_g_ctrl,
	.g_ext_ctrls	= sensor_sr130pc20_g_ext_ctrls,
//	.s_ext_ctrls	= sensor_sr130pc20_g_ctrl,
};

static const struct v4l2_subdev_video_ops video_ops = {
	.s_stream = sr130pc20_s_stream,
	.s_parm = sr130pc20_s_param,
	.s_mbus_fmt = sr130pc20_s_mbus_fmt
};

/* enum code by flite video device command */
static int sr130pc20_enum_mbus_code(struct v4l2_subdev *subdev,
				 struct v4l2_subdev_fh *fh,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	cam_err("Need to check\n");

	if (!code || code->index >= SIZE_DEFAULT_FFMT)
		return -EINVAL;

	code->code = default_fmt[code->index].code;

	return 0;
}

/* get format by flite video device command */
static int sr130pc20_get_fmt(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh,
			  struct v4l2_subdev_format *fmt)
{
	struct sr130pc20_state *state = to_state(subdev);
	struct v4l2_mbus_framefmt *format;

	cam_err("Need to check\n");

	if (fmt->pad != 0)
		return -EINVAL;

	format = __find_format(state, fh, fmt->which, state->res_type);
	if (!format)
		return -EINVAL;

	fmt->format = *format;

	return 0;
}

/* set format by flite video device command */
static int sr130pc20_set_fmt(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh,
			  struct v4l2_subdev_format *fmt)
{
	struct sr130pc20_state *state = to_state(subdev);
	struct v4l2_mbus_framefmt *format = &fmt->format;
	struct v4l2_mbus_framefmt *sfmt;
	enum sr130pc20_oprmode type;
	u32 resolution = 0;
	int ret;

	cam_info("%s: E\n", __func__);
	if (fmt->pad != 0)
		return -EINVAL;

	cam_info("%s: fmt->format.width = %d, fmt->format.height = %d",
			__func__, fmt->format.width, fmt->format.height);

	ret = __find_resolution(subdev, format, &type, &resolution);
	if (ret < 0)
		return ret;

	sfmt = __find_format(state, fh, fmt->which, type);
	if (!sfmt)
		return 0;

	sfmt		= &default_fmt[type];
	sfmt->width	= format->width;
	sfmt->height	= format->height;

	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		/* for enum size of entity by flite */
		state->ffmt[type].width		= format->width;
		state->ffmt[type].height	= format->height;
		state->ffmt[type].code		= V4L2_MBUS_FMT_YUYV8_2X8;

		/* find adaptable resolution */
		state->resolution		= resolution;
		state->code				= V4L2_MBUS_FMT_YUYV8_2X8;
		state->res_type			= type;

		/* for set foramat */
		state->req_fmt.width	= format->width;
		state->req_fmt.height	= format->height;

		if ((state->power_on == SR130PC20_HW_POWER_ON)
		    && (state->runmode != RUNMODE_CAPTURING))
			sr130pc20_s_mbus_fmt(subdev, sfmt);  /* set format */
	}

	return 0;
}

static struct v4l2_subdev_pad_ops pad_ops = {
	.enum_mbus_code	= sr130pc20_enum_mbus_code,
	.get_fmt	= sr130pc20_get_fmt,
	.set_fmt	= sr130pc20_set_fmt,
};

static const struct v4l2_subdev_ops subdev_ops = {
	.core = &core_ops,
	.pad	= &pad_ops,
	.video = &video_ops
};

/*
 * @ brief
 * frame duration time
 * @ unit
 * nano second
 * @ remarks
 */
int sensor_sr130pc20_s_duration(struct v4l2_subdev *subdev, u64 duration)
{
	int ret = 0;
	u32 framerate;
	struct fimc_is_module_enum *module;
	struct sr130pc20_state *module_sr130pc20;
	struct i2c_client *client;

	BUG_ON(!subdev);

	module = (struct fimc_is_module_enum *)v4l2_get_subdevdata(subdev);
	if (unlikely(!module)) {
		cam_err("module is NULL");
		ret = -EINVAL;
		goto p_err;
	}

	module_sr130pc20 = module->private_data;
	if (unlikely(!module_sr130pc20)) {
		cam_err("module_sr130pc20 is NULL");
		ret = -EINVAL;
		goto p_err;
	}

	client = module->client;
	if (unlikely(!client)) {
		cam_err("client is NULL");
		ret = -EINVAL;
		goto p_err;
	}

	framerate = duration;
	if (framerate > FRAME_RATE_MAX) {
		cam_err("framerate is invalid(%d)", framerate);
		ret = -EINVAL;
		goto p_err;
	}

p_err:
	return ret;
}

int sensor_sr130pc20_g_min_duration(struct v4l2_subdev *subdev)
{
	int ret = 0;
	return ret;
}

int sensor_sr130pc20_g_max_duration(struct v4l2_subdev *subdev)
{
	int ret = 0;
	return ret;
}

int sensor_sr130pc20_s_exposure(struct v4l2_subdev *subdev, u64 exposure)
{
	int ret = 0;
	u8 value;
	struct fimc_is_module_enum *sensor;
	struct i2c_client *client;

	BUG_ON(!subdev);

	sensor = (struct fimc_is_module_enum *)v4l2_get_subdevdata(subdev);
	if (unlikely(!sensor)) {
		cam_err("sensor is NULL");
		ret = -EINVAL;
		goto p_err;
	}

	client = sensor->client;
	if (unlikely(!client)) {
		cam_err("client is NULL");
		ret = -EINVAL;
		goto p_err;
	}

	value = exposure & 0xFF;


p_err:
	return ret;
}

int sensor_sr130pc20_g_min_exposure(struct v4l2_subdev *subdev)
{
	int ret = 0;
	return ret;
}

int sensor_sr130pc20_g_max_exposure(struct v4l2_subdev *subdev)
{
	int ret = 0;
	return ret;
}

int sensor_sr130pc20_s_again(struct v4l2_subdev *subdev, u64 sensitivity)
{
	int ret = 0;

	cam_info("%s\n", __func__);

	return ret;
}

int sensor_sr130pc20_g_min_again(struct v4l2_subdev *subdev)
{
	int ret = 0;
	return ret;
}

int sensor_sr130pc20_g_max_again(struct v4l2_subdev *subdev)
{
	int ret = 0;
	return ret;
}

int sensor_sr130pc20_s_dgain(struct v4l2_subdev *subdev)
{
	int ret = 0;
	return ret;
}

int sensor_sr130pc20_g_min_dgain(struct v4l2_subdev *subdev)
{
	int ret = 0;
	return ret;
}

int sensor_sr130pc20_g_max_dgain(struct v4l2_subdev *subdev)
{
	int ret = 0;
	return ret;
}

struct fimc_is_sensor_ops module_sr130pc20_ops = {
	.stream_on	= sensor_sr130pc20_stream_on,
	.stream_off	= sensor_sr130pc20_stream_off,
	.s_duration	= sensor_sr130pc20_s_duration,
	.g_min_duration	= sensor_sr130pc20_g_min_duration,
	.g_max_duration	= sensor_sr130pc20_g_max_duration,
	.s_exposure	= sensor_sr130pc20_s_exposure,
	.g_min_exposure	= sensor_sr130pc20_g_min_exposure,
	.g_max_exposure	= sensor_sr130pc20_g_max_exposure,
	.s_again	= sensor_sr130pc20_s_again,
	.g_min_again	= sensor_sr130pc20_g_min_again,
	.g_max_again	= sensor_sr130pc20_g_max_again,
	.s_dgain	= sensor_sr130pc20_s_dgain,
	.g_min_dgain	= sensor_sr130pc20_g_min_dgain,
	.g_max_dgain	= sensor_sr130pc20_g_max_dgain
};

#ifdef CONFIG_OF
static int sensor_sr130pc20_power_setpin(struct i2c_client *client,
		struct exynos_platform_fimc_is_module *pdata)
{
	struct device_node *dnode;
	int gpio_reset = 0;
	int gpio_standby = 0;
	int gpio_io_en = 0;
	int gpio_none = 0;

	BUG_ON(!client);
	BUG_ON(!client->dev.of_node);

	dnode = client->dev.of_node;

	cam_info("%s E\n", __func__);

	gpio_reset = of_get_named_gpio(dnode, "gpio_reset", 0);
	if (!gpio_is_valid(gpio_reset)) {
		cam_err("%s failed to get gpio_reset\n",__func__);
		return -EINVAL;
	} else {
		gpio_request_one(gpio_reset, GPIOF_OUT_INIT_LOW, "CAM_GPIO_OUTPUT_LOW");
		gpio_free(gpio_reset);
	}

	gpio_standby = of_get_named_gpio(dnode, "gpio_standby", 0);
	if (!gpio_is_valid(gpio_standby)) {
		cam_err("%s failed to get gpio_standby\n",__func__);
		return -EINVAL;
	} else {
		gpio_request_one(gpio_standby, GPIOF_OUT_INIT_LOW, "CAM_GPIO_OUTPUT_LOW");
		gpio_free(gpio_standby);
	}

	gpio_io_en = of_get_named_gpio(dnode, "gpio_io_en", 0);
	if (!gpio_is_valid(gpio_io_en)) {
		err("%s failed to get gpio_io_en\n",__func__);
		return -EINVAL;
	} else {
		gpio_request_one(gpio_io_en, GPIOF_OUT_INIT_LOW, "CAM_GPIO_OUTPUT_LOW");
		gpio_free(gpio_io_en);
	}

	SET_PIN_INIT(pdata, SENSOR_SCENARIO_EXTERNAL, GPIO_SCENARIO_ON);
	SET_PIN_INIT(pdata, SENSOR_SCENARIO_EXTERNAL, GPIO_SCENARIO_OFF);

	/* FRONT CAMERA - POWER ON */
	SET_PIN(pdata, SENSOR_SCENARIO_EXTERNAL, GPIO_SCENARIO_ON, gpio_io_en, "VT_CAM_1P8", PIN_OUTPUT, 1, 1);
	SET_PIN(pdata, SENSOR_SCENARIO_EXTERNAL, GPIO_SCENARIO_ON, gpio_none, "VT_CAM_2P8", PIN_REGULATOR, 1, 2000);
	SET_PIN(pdata, SENSOR_SCENARIO_EXTERNAL, GPIO_SCENARIO_ON, gpio_standby, NULL, PIN_OUTPUT, 1, 2000);
	SET_PIN(pdata, SENSOR_SCENARIO_EXTERNAL, GPIO_SCENARIO_ON, gpio_none, "pin", PIN_FUNCTION, 0, 31000);
	SET_PIN(pdata, SENSOR_SCENARIO_EXTERNAL, GPIO_SCENARIO_ON, gpio_reset, NULL, PIN_OUTPUT, 1, 1000);

	/* FRONT CAMERA - POWER OFF */
	SET_PIN(pdata, SENSOR_SCENARIO_EXTERNAL, GPIO_SCENARIO_OFF, gpio_reset, NULL, PIN_OUTPUT, 0, 10);
	SET_PIN(pdata, SENSOR_SCENARIO_EXTERNAL, GPIO_SCENARIO_OFF, gpio_none, "pin", PIN_FUNCTION, 1, 0);
	SET_PIN(pdata, SENSOR_SCENARIO_EXTERNAL, GPIO_SCENARIO_OFF, gpio_standby, NULL, PIN_OUTPUT, 0, 1);
	SET_PIN(pdata, SENSOR_SCENARIO_EXTERNAL, GPIO_SCENARIO_OFF, gpio_none, "VT_CAM_2P8", PIN_REGULATOR, 0, 0);
	SET_PIN(pdata, SENSOR_SCENARIO_EXTERNAL, GPIO_SCENARIO_OFF, gpio_io_en, "VT_CAM_1P8", PIN_OUTPUT, 0, 1);

/*
	sensor_sr130pc20_apply_set(client, &sr130pc20_regset_table.init);
	sensor_sr130pc20_apply_set(client, &sr130pc20_regset_table.stop_stream);
	fimc_is_sr130pc20_read8(client, 0x4, &id);
	cam_info(" %s(id%X)\n", __func__, id);
*/
	cam_info("%s X\n", __func__);
	return 0;
}
#endif /* CONFIG_OF */

int sensor_sr130pc20_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int ret = 0;
	struct fimc_is_core *core;
	struct v4l2_subdev *subdev_module;
	struct fimc_is_module_enum *module;
	struct fimc_is_device_sensor *device;
	struct exynos_platform_fimc_is_module *pdata;

	BUG_ON(!fimc_is_dev);

	core = (struct fimc_is_core *)dev_get_drvdata(fimc_is_dev);
	if (!core) {
		probe_err("core device is not yet probed");
		return -EPROBE_DEFER;
	}
	device = &core->sensor[SENSOR_SR130PC20_INSTANCE];

	pdata = kzalloc(sizeof(struct exynos_platform_fimc_is_module), GFP_KERNEL);
	if (!pdata) {
		pr_err("%s: no memory for platform data\n", __func__);
		return -ENOMEM;
	}

#ifdef CONFIG_OF
	fimc_is_sensor_module_soc_parse_dt(client, pdata, sensor_sr130pc20_power_setpin);
#endif
	subdev_module = kzalloc(sizeof(struct v4l2_subdev), GFP_KERNEL);
	if (!subdev_module) {
		probe_err("subdev_module is NULL");
		ret = -ENOMEM;
		goto p_err;
	}

	/* SR130PC20 */
	module = &device->module_enum[atomic_read(&core->resourcemgr.rsccount_module)];
	atomic_inc(&core->resourcemgr.rsccount_module);
	module->pdata = pdata;
	module->sensor_id = SENSOR_SR130PC20_NAME;
	module->subdev = subdev_module;
	module->device = SENSOR_SR130PC20_INSTANCE;
	module->ops = &module_sr130pc20_ops;
	module->client = client;

	module->active_width = 1280;
	module->active_height = 960;
	module->pixel_width = module->active_width;
	module->pixel_height = module->active_height;
	module->max_framerate = 30;
	module->position = SENSOR_POSITION_FRONT;
	module->mode = CSI_MODE_CH0_ONLY;
	module->lanes = CSI_DATA_LANES_1;
	module->vcis = ARRAY_SIZE(vci_sr130pc20);
	module->vci = vci_sr130pc20;
	module->sensor_maker = "SF";
	module->sensor_name = "SR130PC20M";
	module->cfgs = ARRAY_SIZE(settle_sr130pc20);
	module->cfg = settle_sr130pc20;
	module->private_data = kzalloc(sizeof(struct sr130pc20_state), GFP_KERNEL);
	if (!module->private_data) {
		probe_err("private_data is NULL");
		ret = -ENOMEM;
		kfree(subdev_module);
		goto p_err;
	}

	v4l2_i2c_subdev_init(subdev_module, client, &subdev_ops);
	v4l2_set_subdevdata(subdev_module, module);
	v4l2_set_subdev_hostdata(subdev_module, device);
	snprintf(subdev_module->name, V4L2_SUBDEV_NAME_SIZE, "sensor-subdev.%d", module->sensor_id);

p_err:
	probe_info("%s(%d)\n", __func__, ret);
	return ret;
}

static int sensor_sr130pc20_remove(struct i2c_client *client)
{
	int ret = 0;
	return ret;
}

#ifdef CONFIG_OF
static const struct of_device_id exynos_fimc_is_sensor_sr130pc20_match[] = {
	{
		.compatible = "samsung,exynos5-fimc-is-sensor-sr130pc20",
	},
	{},
};
#endif

static const struct i2c_device_id sensor_sr130pc20_idt[] = {
	{ SENSOR_NAME, 0 },
	{},
};

static struct i2c_driver sensor_sr130pc20_driver = {
	.driver = {
		.name	= SENSOR_NAME,
		.owner	= THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = exynos_fimc_is_sensor_sr130pc20_match
#endif
	},
	.probe	= sensor_sr130pc20_probe,
	.remove	= sensor_sr130pc20_remove,
	.id_table = sensor_sr130pc20_idt
};

module_i2c_driver(sensor_sr130pc20_driver);

MODULE_AUTHOR("Gilyeon lim");
MODULE_DESCRIPTION("Sensor sr130pc20 driver");
MODULE_LICENSE("GPL v2");
