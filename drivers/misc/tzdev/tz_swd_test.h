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

#ifndef __TZ_SWD_TEST_H__
#define __TZ_SWD_TEST_H__

#include <linux/types.h>
#include "tz_common.h"

#define TZ_SWD_TEST_IOC_MAGIC		'c'
#define TZ_SWD_TEST_GET_BUF_SIZE	_IOW(TZ_SWD_TEST_IOC_MAGIC, 0, __u32)

#define TZ_SWD_TEST_SEND_COMMAND	_IOWR(TZ_IOC_MAGIC, 0, struct tzio_smc_data)
#define TZ_SWD_TEST_GET_EVENT		_IOR(TZ_IOC_MAGIC, 1, int)
#define TZ_SWD_TEST_MEM_REGISTER	_IOWR(TZ_IOC_MAGIC, 2, struct tzio_mem_register)
#define TZ_SWD_TEST_MEM_RELEASE		_IOW(TZ_IOC_MAGIC, 3, int)
#define TZ_SWD_TEST_MEM_READ		_IOR(TZ_IOC_MAGIC, 4, struct tz_swd_test_mem_rw)
#define TZ_SWD_TEST_MEM_WRITE		_IOW(TZ_IOC_MAGIC, 5, struct tz_swd_test_mem_rw)

struct tz_swd_test_mem_rw {
	__s32 id;			/* Memory region ID (in) */
	__u64 ptr;			/* Memory region start (in) */
	__u64 size;			/* Memory region size (in) */
};

#endif /*!__TZ_SWD_TEST_H__*/
