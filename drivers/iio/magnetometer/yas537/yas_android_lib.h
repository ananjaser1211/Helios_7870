/**
 * Copyright (c) 2014 Yamaha Corporation
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

#ifndef __YAS_ANDROID_LIB_H__
#define __YAS_ANDROID_LIB_H__

#include <stdint.h>
#include "yas.h"

#define YASLIB_DEBUG				(0)
#define YAS_MSM_PLATFORM			(0)
#define YAS_ALGO_CONFIG_FILE_PATH		"/data/system/yas_set.cfg"
#define YAS_ALGO_PARAM_FILE_PATH		"/data/system/yas_lib.cfg"

#define YAS_DEFAULT_SPREAD_1			(100)
#define YAS_DEFAULT_SPREAD_2			(150)
#define YAS_DEFAULT_SPREAD_3			(180)
#define YAS_DEFAULT_VARIATION_1			(50)
#define YAS_DEFAULT_VARIATION_2			(45)
#define YAS_DEFAULT_VARIATION_3			(40)
#define YAS_DEFAULT_EL_SPREAD_1			(200)
#define YAS_DEFAULT_EL_SPREAD_2			(300)
#define YAS_DEFAULT_EL_SPREAD_3			(500)
#define YAS_DEFAULT_EL_VARIATION_1		(30)
#define YAS_DEFAULT_EL_VARIATION_2		(20)
#define YAS_DEFAULT_EL_VARIATION_3		(15)
#define YAS_DEFAULT_TRAD_VARIATION_1		(30000)
#define YAS_DEFAULT_TRAD_VARIATION_2		(30000)
#define YAS_DEFAULT_TRAD_VARIATION_3		(250)
#define YAS_DEFAULT_CWG_THRESHOLD_0		(100)
#define YAS_DEFAULT_CWG_THRESHOLD_1		(6000)
#define YAS_DEFAULT_CWG_THRESHOLD_2		(350)
#define YAS_DEFAULT_CWG_THRESHOLD_3		(8000)
#define YAS_DEFAULT_CWG_THRESHOLD_4		(100)
#define YAS_DEFAULT_CWG_THRESHOLD_5		(3500)
#define YAS_DEFAULT_CWG_THRESHOLD_6		(350)
#define YAS_DEFAULT_CWG_THRESHOLD_7		(4500)
#define YAS_DEFAULT_CWG_THRESHOLD_8		(100)
#define YAS_DEFAULT_CWG_THRESHOLD_9		(2864)
#define YAS_DEFAULT_CWG_THRESHOLD_10		(350)
#define YAS_DEFAULT_CWG_THRESHOLD_11		(3246)
#define YAS_DEFAULT_CALIB_MODE			(0)
#define YAS_DEFAULT_FILTER_ENABLE		(1)

#if YASLIB_DEBUG
#if defined __ANDROID__
#include <cutils/log.h>
#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "yas"
#define YASLOGD(args) (ALOGD args)
#define YASLOGI(args) (ALOGI args)
#define YASLOGE(args) (ALOGE args)
#define YASLOGW(args) (ALOGW args)
#else /* __ANDROID__ */
#include <stdio.h>
#define YASLOGD(args) (printf args)
#define YASLOGI(args) (printf args)
#define YASLOGE(args) (printf args)
#define YASLOGW(args) (printf args)
#endif /* __ANDROID__ */
#else /* DEBUG */
#define YASLOGD(args)
#define YASLOGI(args)
#define YASLOGW(args)
#define YASLOGE(args)
#endif /* DEBUG */

typedef struct _QUATERNIONS {
	struct {
		float q[4];
		float heading_error;
	};
} QUATERNION;

typedef struct _SENSORDATAS {
	union {
		struct {
			int x;
			int y;
			int z;
		};
		struct {
			float vx;
			float vy;
			float vz;
			float ox;
			float oy;
			float oz;
			int accuracy;
		};
	};
} SENSORDATA;

#ifdef __cplusplus
extern "C" {
#endif

int Magnetic_Initialize(void);
int Magnetic_Enable(void);
int Magnetic_Disable(void);
int Magnetic_Set_Delay(uint64_t delay);
int Magnetic_Calibrate(SENSORDATA *raw, SENSORDATA *cal);
int Magnetic_Get_Euler(SENSORDATA *acccal, SENSORDATA *magcal,
		SENSORDATA *orientation);
int Magnetic_Get_Quaternion(SENSORDATA *acccal, SENSORDATA *magcal,
		QUATERNION *quaternion);
#if YAS_SOFTWARE_GYROSCOPE_ENABLE
int Magnetic_Get_SoftwareGyroscope(SENSORDATA *acccal, SENSORDATA *magcal,
		uint32_t timestamp, SENSORDATA *gyro);
#endif
#if YAS_ATTITUDE_FILTER_ENABLE
int Magnetic_Get_Filtered_Quaternion(SENSORDATA *acccal, SENSORDATA *magcal,
		QUATERNION *quaternion, SENSORDATA *gravity,
		SENSORDATA *linear_acceleration);
#endif

#ifdef __cplusplus
}
#endif

#endif
