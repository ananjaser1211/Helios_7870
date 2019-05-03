/*
 * Copyright (C) 2012-2016 Samsung Electronics, Inc.
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

#ifndef __TZ_KERNEL_API_TEST_H__
#define __TZ_KERNEL_API_TEST_H__

#include <linux/types.h>

#define TZ_KAPI_TEST_IOC_MAGIC		'c'
#define TZ_KAPI_TEST_RUN		_IOW(TZ_KAPI_TEST_IOC_MAGIC, 0, int)
#define TZ_KAPI_REGISTER_MEM		_IO(TZ_KAPI_TEST_IOC_MAGIC, 1)

#endif /*!__TZ_KERNEL_API_TEST_H__*/
