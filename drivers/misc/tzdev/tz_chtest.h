/*
 * Copyright (C) 2016 Samsung Electronics, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _CUSTOM_HANDLER_TEST_H_
#define _CUSTOM_HANDLER_TEST_H_

#include "tzdev.h"

struct tz_chtest_data {
	unsigned int r0;
	unsigned int r1;
	unsigned int r2;
	unsigned int r3;
};

#define CUSTOM_HANDLER_IOC_MAGIC	'g'
#define CUSTOM_HANDLER_TEST_NAME	"tz_chtest"
#define CUSTOM_HANDLER_TEST_DEV		"/dev/"CUSTOM_HANDLER_TEST_NAME
#define IOCTL_CUSTOM_HANDLER_CMD	_IOWR(CUSTOM_HANDLER_IOC_MAGIC, \
					2, struct tz_chtest_data)

/* Starting raw base for Custom Handler commands */
#define CUSTOM_HANDLER_CMD_BASE_RAW	0x100U

/* Starting raw base for Custom Handlers */
#define CUSTOM_HANDLER_EXEC_BASE_RAW	0x200U

/* Number of custom handlers that could be registered */
#define CUSTOM_HANDLER_COUNT		16

/* Calculate FID base according to the platform, there are three variants: */
#ifdef CONFIG_TZDEV_USE_ARM_CALLING_CONVENTION
/* 1. Platform uses ARM calling convention (ATF is present) */
#define CUSTOM_HANDLER_FID_BASE	CREATE_SMC_CMD(SMC_TYPE_FAST, SMC_CURRENT_AARCH, SMC_TOS0_SERVICE_MASK, 0)
#else
#if defined(CONFIG_ARCH_MSM) && defined(CONFIG_MSM_SCM)
/* 2. Any Qualcomm platform */
#define CUSTOM_HANDLER_FID_BASE	0
#else
/* 3. Platform with internal monitor (without ATF) */
#define CUSTOM_HANDLER_FID_BASE	TZDEV_SMC_MAGIC
#endif /* defined(CONFIG_ARCH_MSM) && defined(CONFIG_MSM_SCM) */
#endif /* CONFIG_TZDEV_USE_ARM_CALLING_CONVENTION */

#endif /* _CUSTOM_HANDLER_TEST_H_ */
