#ifndef __EXYNOS7870_H__
#define __EXYNOS7870_H__

#include "S5E7870-sfrbase.h"

#define CPUCL0_EMA_CON		((void *)(SYSREG_CPUCL0_BASE + 0x0330))
#define CPUCL0_EMA_REG1		((void *)(SYSREG_CPUCL0_BASE + 0x0004))
#define CPUCL1_EMA_CON		((void *)(SYSREG_CPUCL1_BASE + 0x0330))
#define CPUCL1_EMA_REG1		((void *)(SYSREG_CPUCL1_BASE + 0x0004))
#define GPU_EMA_RF2_UHD_CON	((void *)(SYSREG_G3D_BASE + 0x0318))
#define CAM_EMA_RF2_UHD_CON	((void *)(SYSREG_ISP_BASE + 0x0318))
#define CAM_EMA_RF2_HS_CON	((void *)(SYSREG_ISP_BASE + 0x2718))
#endif
