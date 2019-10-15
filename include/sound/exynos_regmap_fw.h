/*
 * API to parse firmware binary and update codec
 *
 * Copyright (c) 2014 Samsung Electronics Co. Ltd.
 *	Tushar Behera <tushar.b@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef _REGMAP_FW_H__
#define _REGMAP_FW_H__

#include <linux/firmware.h>

#ifdef CONFIG_SND_SOC_REGMAP_FIRMWARE
void exynos_regmap_update_fw(const char *fw_name,
				struct device *dev,
				struct regmap *regmap,
				int i2c_addr,
				void (*post_fn)(void *),
				void *post_fn_arg,
				void (*post_fn_failed)(void *),
				void *post_fn_failed_arg);
#else
static void exynos_regmap_update_fw(const char *fw_name,
				struct device *dev,
				struct regmap *regmap,
				int i2c_addr,
				void (*post_fn)(void *),
				void *post_fn_arg,
				void (*post_fn_failed)(void *),
				void *post_fn_failed_arg)
{
	if (post_fn_failed)
		post_fn_failed(post_fn_failed_arg);
}
#endif

#endif /* _REGMAP_FW_H__ */
