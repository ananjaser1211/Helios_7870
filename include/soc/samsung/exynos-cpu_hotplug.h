/*
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * EXYNOS - CPU Hotplug support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __EXYNOS_CPU_HOTPLUG_H
#define __EXYNOS_CPU_HOTPLUG_H __FILE__

struct kobject *exynos_cpu_hotplug_kobj(void);
bool exynos_cpu_hotplug_enabled(void);

#endif /* __EXYNOS_CPU_HOTPLUG_H */

