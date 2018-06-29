#ifndef FIMC_IS_VENDOR_CONFIG_A3XPREMIUM_H
#define FIMC_IS_VENDOR_CONFIG_A3XPREMIUM_H

#include "fimc-is-eeprom-rear-3p8_v001.h"
#include "fimc-is-eeprom-front-5e3_v001.h"

#define VENDER_PATH

#define CAMERA_MODULE_ES_VERSION_REAR 'A'
#define CAMERA_MODULE_ES_VERSION_FRONT 'A'
#define CAL_MAP_ES_VERSION_REAR '1'
#define CAL_MAP_ES_VERSION_FRONT '1'

#define CAMERA_SYSFS_V2
#define CAMERA_SHARED_IO_POWER	// if used front and rear shared IO power

#define USE_CAMERA_HW_BIG_DATA

#ifdef USE_CAMERA_HW_BIG_DATA
#define CSI_SCENARIO_SEN_REAR	(0)
#define CSI_SCENARIO_SEN_FRONT	(1)
#endif

#endif /* FIMC_IS_VENDOR_CONFIG_A3XPREMIUM_H */
