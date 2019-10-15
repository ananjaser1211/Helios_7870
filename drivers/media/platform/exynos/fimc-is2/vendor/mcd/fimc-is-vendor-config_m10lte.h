#ifndef FIMC_IS_VENDOR_CONFIG_M10LTE_H
#define FIMC_IS_VENDOR_CONFIG_M10LTE_H

#include "fimc-is-eeprom-rear-3l2_v003.h"
#include "fimc-is-otprom-front-5e9_v001.h"
#include "fimc-is-eeprom-rear-5e9_v001.h"

/* This count is defined by count of fimc_is_sensor in the dtsi file */
#define FIMC_IS_HW_SENSOR_COUNT 3

#define VENDER_PATH

#define CAMERA_MODULE_ES_VERSION_REAR 'A'
//#define CAMERA_MODULE_ES_VERSION_REAR2 'A'
#define CAMERA_MODULE_ES_VERSION_REAR3 'A'
#define CAMERA_MODULE_ES_VERSION_FRONT 'A'
#define CAL_MAP_ES_VERSION_REAR '1'
//#define CAL_MAP_ES_VERSION_REAR2 '1'
#define CAL_MAP_ES_VERSION_REAR3 '1'
#define CAL_MAP_ES_VERSION_FRONT '1'

#define CAMERA_SYSFS_V2

//#define CAMERA_REAR2		// Support Rear2 for Dual Camera
#define CAMERA_REAR3		// Support Rear2 for Dual Camera
//#define CAMERA_SHARED_IO_POWER	// if used front and rear shared IO power

#define CAMERA_SHARED_IO_POWER_UW
#define CAMERA_OTPROM_SUPPORT_FRONT_5E9

#define USE_CAMERA_HW_BIG_DATA

//TEMP:
//#define CAMERA_REAR2_M10_TEMP
#define CAMERA_REAR3_M10_TEMP

#define EEPROM_DEBUG

#ifdef USE_CAMERA_HW_BIG_DATA
#define CSI_SCENARIO_SEN_REAR	(0)
#define CSI_SCENARIO_SEN_FRONT	(1)
//#define CSI_SCENARIO_SEN_REAR2	(2)
#define CSI_SCENARIO_SEN_REAR3	(2)
#endif

#define USE_MFHDR_CAMERA_INTERFACE

#define USE_FACE_UNLOCK_AE_AWB_INIT /* for Face Unlock */

#endif /* FIMC_IS_VENDOR_CONFIG_M10_H */
