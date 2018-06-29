#ifndef FIMC_IS_VENDOR_CONFIG_ON7X_H
#define FIMC_IS_VENDOR_CONFIG_ON7X_H

#include "fimc-is-eeprom-rear-imx258_v002.h"
#include "fimc-is-eeprom-front-4h5yc_v002.h"

#define VENDER_PATH

#define CAMERA_MODULE_ES_VERSION_REAR 'A'
#define CAMERA_MODULE_ES_VERSION_FRONT 'A'
#define CAL_MAP_ES_VERSION_REAR '2'
#define CAL_MAP_ES_VERSION_FRONT '2'

#define CAMERA_SYSFS_V2

#define CAMERA_SHARED_IO_POWER	// if used front and rear shared IO power

#endif /* FIMC_IS_VENDOR_CONFIG_ON7X_H */
