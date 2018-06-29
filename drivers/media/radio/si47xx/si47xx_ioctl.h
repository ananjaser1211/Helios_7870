/*
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#ifndef _SI47XX_IOCTL_H
#define _SI47XX_IOCTL_H

#include <linux/types.h>
#include <linux/ioctl.h>

#include "si47xx_dev.h"

/* MAGIC NUMBER */
#define SI47XX_IOC_MAGIC		0xFA

/* MAX SEQ NUMBER */
#define SI47XX_IOC_NR_MAX		41

/* COMMANDS */
#define SI47XX_IOC_POWERUP		_IO(SI47XX_IOC_MAGIC, 0)

#define SI47XX_IOC_POWERDOWN		_IO(SI47XX_IOC_MAGIC, 1)

#define SI47XX_IOC_BAND_SET		_IOW(SI47XX_IOC_MAGIC, 2, int)

#define SI47XX_IOC_CHAN_SPACING_SET	_IOW(SI47XX_IOC_MAGIC, 3, int)

#define SI47XX_IOC_CHAN_SELECT		_IOW(SI47XX_IOC_MAGIC, 4, u32)

#define SI47XX_IOC_CHAN_GET		_IOR(SI47XX_IOC_MAGIC, 5, u32)

#define SI47XX_IOC_SEEK_UP		_IOR(SI47XX_IOC_MAGIC, 6, u32)

#define SI47XX_IOC_SEEK_DOWN		_IOR(SI47XX_IOC_MAGIC, 7, u32)

/* VNVS:28OCT'09---- SI47XX_IOC_SEEK_AUTO is disabled as of now */
/* #define SI47XX_IOC_SEEK_AUTO		_IOR(SI47XX_IOC_MAGIC, 8, u32) */

#define SI47XX_IOC_RSSI_SEEK_TH_SET	_IOW(SI47XX_IOC_MAGIC, 9, u8)

#define SI47XX_IOC_SEEK_SNR_SET		_IOW(SI47XX_IOC_MAGIC, 10, u8)

#define SI47XX_IOC_SEEK_CNT_SET		_IOW(SI47XX_IOC_MAGIC, 11, u8)

#define SI47XX_IOC_CUR_RSSI_GET		\
	_IOR(SI47XX_IOC_MAGIC, 12, struct rssi_snr_t)

#define SI47XX_IOC_VOLEXT_ENB		_IO(SI47XX_IOC_MAGIC, 13)

#define SI47XX_IOC_VOLEXT_DISB		_IO(SI47XX_IOC_MAGIC, 14)

#define SI47XX_IOC_VOLUME_SET		_IOW(SI47XX_IOC_MAGIC, 15, u8)

#define SI47XX_IOC_VOLUME_GET		_IOR(SI47XX_IOC_MAGIC, 16, u8)

#define SI47XX_IOC_MUTE_ON		_IO(SI47XX_IOC_MAGIC, 17)

#define SI47XX_IOC_MUTE_OFF		_IO(SI47XX_IOC_MAGIC, 18)

#define SI47XX_IOC_MONO_SET		_IO(SI47XX_IOC_MAGIC, 19)

#define SI47XX_IOC_STEREO_SET		_IO(SI47XX_IOC_MAGIC, 20)

#define SI47XX_IOC_RSTATE_GET		\
	_IOR(SI47XX_IOC_MAGIC, 21, struct dev_state_t)

#define SI47XX_IOC_RDS_DATA_GET		\
	_IOR(SI47XX_IOC_MAGIC, 22, struct radio_data_t)

#define SI47XX_IOC_RDS_ENABLE		_IO(SI47XX_IOC_MAGIC, 23)

#define SI47XX_IOC_RDS_DISABLE		_IO(SI47XX_IOC_MAGIC, 24)

#define SI47XX_IOC_RDS_TIMEOUT_SET	_IOW(SI47XX_IOC_MAGIC, 25, u32)

#define SI47XX_IOC_SEEK_CANCEL		_IO(SI47XX_IOC_MAGIC, 26)

/* VNVS:START 13-OCT'09 :
 * Added IOCTLs for reading the device-id,chip-id,power configuration,
 * system configuration2 registers*/
#define SI47XX_IOC_DEVICE_ID_GET	\
	_IOR(SI47XX_IOC_MAGIC, 27, struct device_id)

#define SI47XX_IOC_CHIP_ID_GET		\
	_IOR(SI47XX_IOC_MAGIC, 28, struct chip_id)

#define SI47XX_IOC_SYS_CONFIG2_GET	\
	_IOR(SI47XX_IOC_MAGIC, 29, struct sys_config2)

#define SI47XX_IOC_POWER_CONFIG_GET	_IO(SI47XX_IOC_MAGIC, 30)

/* For reading AFCRL bit, to check for a valid channel */
#define SI47XX_IOC_AFCRL_GET		_IOR(SI47XX_IOC_MAGIC, 31, u8)

/* Setting DE-emphasis Time Constant.
 * For DE=0,TC=50us(Europe,Japan,Australia) and DE=1,TC=75us(USA)
 */
#define SI47XX_IOC_DE_SET		_IOW(SI47XX_IOC_MAGIC, 32, u8)

#define SI47XX_IOC_SYS_CONFIG3_GET	\
	_IOR(SI47XX_IOC_MAGIC, 33, struct sys_config3)

#define SI47XX_IOC_STATUS_RSSI_GET	\
	_IOR(SI47XX_IOC_MAGIC, 34, struct status_rssi)

#define SI47XX_IOC_SYS_CONFIG2_SET	\
	_IOW(SI47XX_IOC_MAGIC, 35, struct sys_config2)

#define SI47XX_IOC_SYS_CONFIG3_SET	\
	_IOW(SI47XX_IOC_MAGIC, 36, struct sys_config3)

#define SI47XX_IOC_DSMUTE_ON		_IO(SI47XX_IOC_MAGIC, 37)

#define SI47XX_IOC_DSMUTE_OFF		_IO(SI47XX_IOC_MAGIC, 38)

#define SI47XX_IOC_RESET_RDS_DATA	_IO(SI47XX_IOC_MAGIC, 39)

#define SI47XX_IOC_SEEK_FULL		_IOR(SI47XX_IOC_MAGIC, 40, u32)

#define SI47XX_IOC_CHAN_CHECK_VALID		_IOR(SI47XX_IOC_MAGIC, 41, bool)

#endif