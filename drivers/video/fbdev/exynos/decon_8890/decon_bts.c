 /*
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Core file for Samsung EXYNOS DECON driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "decon_bts.h"
#include "vpp/vpp.h"
#include <soc/samsung/bts.h>

#define MULTI_FACTOR 		(1 << 10)
#define VPP_MAX 		9
#define HALF_MIC 		2
#define PIX_PER_CLK 		2
#define ROTATION 		4
#define NOT_ROT 		1
#define RGB_FACTOR 		4
#define YUV_FACTOR 		4
 /* If use float factor, DATA format factor should be multiflied by 2
 * And then, should be divided using FORMAT_FACTOR at the end of bw_eq
 */
#define FORMAT_FACTOR 		2
#define DISP_FACTOR 		1

#define ENUM_OFFSET 		2

#ifdef CONFIG_PM_DEVFREQ
void bts_int_mif_bw(struct vpp_dev *vpp)
{
	struct decon_win_config *config = vpp->config;
	struct decon_frame *src = &config->src;
	struct decon_frame *dst = &config->dst;
	struct decon_device *decon = get_decon_drvdata(0);
	u32 vclk = (clk_get_rate(decon->res.vclk_leaf) * PIX_PER_CLK / MHZ);
	/* TODO, parse mic factor automatically at dt */
	u32 mic_factor = HALF_MIC; /* 1/2 MIC */
	u64 s_ratio_h, s_ratio_v = 0;
	/* bpl is multiplied 2, it should be divied at end of eq. */
	u8 bpl = 0;
	u8 rot_factor = is_rotation(config) ? ROTATION : NOT_ROT;
	bool is_rotated = is_rotation(config) ? true : false;

	if (is_rgb(config))
		bpl = RGB_FACTOR;
	else if (is_yuv(config))
		bpl = YUV_FACTOR;
	else
		bpl = RGB_FACTOR;

	if (is_rotated) {
		s_ratio_h = MULTI_FACTOR * src->h / dst->w;
		s_ratio_v = MULTI_FACTOR * src->w / dst->h;
	} else {
		s_ratio_h = MULTI_FACTOR * src->w / dst->w;
		s_ratio_v = MULTI_FACTOR * src->h / dst->h;
	}

	/* BW = (VCLK * MIC_factor * Data format * ScaleRatio_H * ScaleRatio_V * RotationFactor) */
	vpp->cur_bw = vclk * mic_factor * bpl * s_ratio_h * s_ratio_v
		* rot_factor * KHZ / (MULTI_FACTOR * MULTI_FACTOR);
}

void bts_disp_bw(struct vpp_dev *vpp)
{
	struct decon_win_config *config = vpp->config;
	struct decon_frame *src = &config->src;
	struct decon_frame *dst = &config->dst;
	struct decon_device *decon = get_decon_drvdata(0);
	u32 vclk = (clk_get_rate(decon->res.vclk_leaf) * PIX_PER_CLK / MHZ);
	/* TODO, parse mic factor automatically at dt */
	u32 mic_factor = HALF_MIC; /* 1/2 MIC */
	/* TODO, parse lcd width automatically at dt */
	u32 lcd_width = 1440;
	u64 s_ratio_h, s_ratio_v = 0;

	u32 src_w = is_rotation(config) ? src->h : src->w;
	u32 src_h = is_rotation(config) ? src->w : src->h;

	/* ACLK_DISP_400 [MHz] = (VCLK * MIC_factor * ScaleRatio_H * ScaleRatio_V * disp_factor) / 2 * (DST_width / LCD_width) */
	s_ratio_h = (src_w <= dst->w) ? MULTI_FACTOR : MULTI_FACTOR * src_w / dst->w;
	s_ratio_v = (src_h <= dst->h) ? MULTI_FACTOR : MULTI_FACTOR * src_h / dst->h;

	vpp->disp_cur_bw = vclk * mic_factor * s_ratio_h * s_ratio_v
		* DISP_FACTOR * KHZ * (MULTI_FACTOR * dst->w / lcd_width)
		/ (MULTI_FACTOR * MULTI_FACTOR * MULTI_FACTOR) / 2;
}

void bts_mif_lock(struct vpp_dev *vpp)
{
	struct decon_win_config *config = vpp->config;

	if (is_rotation(config)) {
		pm_qos_update_request(&vpp->vpp_mif_qos, 1144000);
	} else {
		pm_qos_update_request(&vpp->vpp_mif_qos, 0);
	}
}

void bts_send_bw(struct vpp_dev *vpp, bool enable)
{
	struct decon_win_config *config = vpp->config;
	u8 vpp_type;
	enum vpp_bw_type bw_type;

	if (is_rotation(config)) {
		if (config->src.w * config->src.h >= FULLHD_SRC)
			bw_type = BW_FULLHD_ROT;
		else
			bw_type = BW_ROT;
	} else {
		bw_type = BW_DEFAULT;
	}

	vpp_type = vpp->id + ENUM_OFFSET;

	if (enable) {
		exynos_update_media_scenario(vpp_type, vpp->cur_bw, bw_type);
	} else {
		exynos_update_media_scenario(vpp_type, 0, BW_DEFAULT);
		vpp->prev_bw = vpp->cur_bw = 0;
	}
}

void bts_add_request(struct decon_device *decon)
{
	pm_qos_add_request(&decon->mif_qos, PM_QOS_BUS_THROUGHPUT, 0);
	pm_qos_add_request(&decon->disp_qos, PM_QOS_DISPLAY_THROUGHPUT, 0);
	pm_qos_add_request(&decon->int_qos, PM_QOS_DEVICE_THROUGHPUT, 0);
}

void bts_send_init(struct decon_device *decon)
{
	pm_qos_update_request(&decon->mif_qos, 546000);
}

void bts_send_release(struct decon_device *decon)
{
	pm_qos_update_request(&decon->mif_qos, 0);
	pm_qos_update_request(&decon->disp_qos, 0);
	pm_qos_update_request(&decon->int_qos, 0);
	decon->disp_cur = decon->disp_prev = 0;
}

void bts_remove_request(struct decon_device *decon)
{
	pm_qos_remove_request(&decon->mif_qos);
	pm_qos_remove_request(&decon->disp_qos);
	pm_qos_remove_request(&decon->int_qos);
}

void bts_calc_bw(struct vpp_dev *vpp)
{
	bts_disp_bw(vpp);
	bts_int_mif_bw(vpp);
}

void bts_send_zero_bw(struct vpp_dev *vpp)
{
	bts_send_bw(vpp, false);
}

void bts_send_calc_bw(struct vpp_dev *vpp)
{
	bts_send_bw(vpp, true);
}
#else
void bts_add_request(struct decon_device *decon){ return; }
void bts_send_init(struct decon_device *decon){ return; }
void bts_send_release(struct decon_device *decon){ return; }
void bts_remove_request(struct decon_device *decon){ return; }

void bts_calc_bw(struct vpp_dev *vpp){ return; }
void bts_send_calc_bw(struct vpp_dev *vpp){ return; }
void bts_send_zero_bw(struct vpp_dev *vpp){ return; }
void bts_mif_lock(struct vpp_dev *vpp){ return; }
#endif

struct decon_init_bts decon_init_bts_control = {
	.bts_add		= bts_add_request,
	.bts_set_init		= bts_send_init,
	.bts_release_init	= bts_send_release,
	.bts_remove		= bts_remove_request,
};

struct decon_bts decon_bts_control = {
	.bts_get_bw		= bts_calc_bw,
	.bts_set_calc_bw	= bts_send_calc_bw,
	.bts_set_zero_bw	= bts_send_zero_bw,
	.bts_set_rot_mif	= bts_mif_lock,
};
