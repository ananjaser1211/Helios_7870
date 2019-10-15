#ifndef FIMC_IS_VENDOR_CONFIG_A2CORELTE_H
#define FIMC_IS_VENDOR_CONFIG_A2CORELTE_H

#include "fimc-is-eeprom-rear-5e9_v001.h"
#include "fimc-is-otprom-front-gc5035_v001.h"

#define VENDER_PATH
#define USE_MFHDR_CAMERA_INTERFACE

#define CAMERA_MODULE_ES_VERSION_REAR 'A'
#define CAMERA_MODULE_ES_VERSION_FRONT 'A'
#define CAL_MAP_ES_VERSION_REAR '1'
#define CAL_MAP_ES_VERSION_FRONT '1'

#define OEM_CRC32_LEN                                   ((0x1CF-0x100)+0x1)
#define AWB_CRC32_LEN                                   ((0x21F-0x200)+0x1)
#define SHADING_CRC32_LEN                               ((0x1CEF-0x300)+0x1)
#define EEP_HEADER_MODULE_ID_ADDR                       0xAE
#define EEP_OEM_VER_START_ADDR                          0x1D0
#define EEP_AP_SHADING_VER_START_ADDR                   0x1FE0
#define EEP_CHECKSUM_AP_SHADING_ADDR                    0x1FFC

#define USE_FACE_UNLOCK_AE_AWB_INIT /* for Face Unlock */
#define CAMERA_SYSFS_V2
#define CAMERA_FRONT_GC5035 
#endif /* FIMC_IS_VENDOR_CONFIG_A2CORELTE_H */
