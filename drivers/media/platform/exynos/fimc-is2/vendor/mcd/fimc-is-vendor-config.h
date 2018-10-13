/*
* Samsung Exynos5 SoC series FIMC-IS driver
 *
 * exynos5 fimc-is vender functions
 *
 * Copyright (c) 2015 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef FIMC_IS_VENDOR_CONFIG_H
#define FIMC_IS_VENDOR_CONFIG_H

#if defined(CONFIG_CAMERA_J7X)
#include "fimc-is-vendor-config_j7x.h"
#elif defined(CONFIG_CAMERA_ON7X)
#include "fimc-is-vendor-config_on7x.h"
#elif defined(CONFIG_CAMERA_MATISSE10)
#include "fimc-is-vendor-config_matisse10.h"
#elif defined(CONFIG_CAMERA_GTAXL)
#include "fimc-is-vendor-config_gtaxl.h"
#elif defined(CONFIG_CAMERA_A7MAX)
#include "fimc-is-vendor-config_a7max.h"
#elif defined(CONFIG_CAMERA_ON7E)
#include "fimc-is-vendor-config_on7e.h"
#elif defined(CONFIG_CAMERA_A3XPREMIUM)
#include "fimc-is-vendor-config_a3xpremium.h"
#elif defined(CONFIG_CAMERA_A3Y17)
#include "fimc-is-vendor-config_a3y17.h"
#elif defined(CONFIG_CAMERA_J5Y17) || defined(CONFIG_CAMERA_J7Y17)
#include "fimc-is-vendor-config_j5y17.h"
#elif defined(CONFIG_CAMERA_J7POPKOR)
#include "fimc-is-vendor-config_j7popkor.h"
#elif defined(CONFIG_CAMERA_J7POP)
#include "fimc-is-vendor-config_j7pop.h"
#elif defined(CONFIG_CAMERA_J7VE)
#include "fimc-is-vendor-config_j7ve.h"
#elif defined(CONFIG_CAMERA_GTACTIVE2)
#include "fimc-is-vendor-config_gtactive2.h"
#elif defined(CONFIG_CAMERA_GRANDPPIRIS)
#include "fimc-is-vendor-config_grandppiris.h"
#elif defined(CONFIG_CAMERA_ON7XREF)
#include "fimc-is-vendor-config_on7xref.h"
#elif defined(CONFIG_CAMERA_J7TOP)
#include "fimc-is-vendor-config_j7top.h"
#elif defined(CONFIG_CAMERA_J8)
#include "fimc-is-vendor-config_j8.h"
#elif defined(CONFIG_CAMERA_DEGASY18)
#include "fimc-is-vendor-config_degasy18.h"
#elif defined(CONFIG_CAMERA_GTAXLAD)
#include "fimc-is-vendor-config_gtaxlad.h"
#elif defined(CONFIG_CAMERA_J6)
#include "fimc-is-vendor-config_j6.h"
#elif defined(CONFIG_CAMERA_A6)
#include "fimc-is-vendor-config_a6.h"
#elif defined(CONFIG_CAMERA_J7VEIRIS)
#include "fimc-is-vendor-config_j7veiris.h"
#else
#include "fimc-is-vendor-config_joshua.h"
#endif

#endif
