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

#ifndef FIMC_IS_CIS_SR544_H
#define FIMC_IS_CIS_SR544_H

#include "fimc-is-cis.h"

#define EXT_CLK_Mhz (26)

#define SENSOR_SR544_MAX_WIDTH		(2584 + 16)
#define SENSOR_SR544_MAX_HEIGHT		(1940 + 12)

/* TODO: Check below values are valid */
#define SENSOR_SR544_FINE_INTEGRATION_TIME_MIN                0x0
#define SENSOR_SR544_FINE_INTEGRATION_TIME_MAX                0x6D8
#define SENSOR_SR544_COARSE_INTEGRATION_TIME_MIN              0x4
#define SENSOR_SR544_COARSE_INTEGRATION_TIME_MAX_MARGIN       0x4

#define USE_GROUP_PARAM_HOLD	(1)
#define USE_OTP_AWB_CAL_DATA	(0)

#endif

