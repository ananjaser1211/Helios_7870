/*
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * EXYNOS - PMU(Power Management Unit) support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __EXYNOS_PMU_GNSS_H
#define __EXYNOS_PMU_GNSS_H __FILE__

#include "gnss_prj.h"

#ifdef CONFIG_SOC_EXYNOS7870
/* BLK_ALIVE: GNSS related SFRs */
#define EXYNOS_PMU_GNSS_CTRL_NS			0x0040
#define EXYNOS_PMU_GNSS_CTRL_S			0x0044
#define EXYNOS_PMU_GNSS_STAT			0x0048
#define EXYNOS_PMU_GNSS_DEBUG			0x004C
#define EXYNOS_PMU_GNSS2AP_MEM_CONFIG		0x0090
#define EXYNOS_PMU_GNSS2AP_MIF0_PERI_ACCESS_CON	0x0094
#define EXYNOS_PMU_GNSS2AP_MIF1_PERI_ACCESS_CON	0x0098
#define EXYNOS_PMU_GNSS_BOOT_TEST_RST_CONFIG	0x00A8
#define EXYNOS_PMU_GNSS2AP_PERI_ACCESS_WIN	0x00AC
#define EXYNOS_PMU_GNSS_MODAPIF_CONFIG		0x00B0
#define EXYNOS_PMU_GNSS_QOS			0x00B8
#define EXYNOS_PMU_CENTRAL_SEQ_GNSS_CONFIGURATION	0x02C0
#define EXYNOS_PMU_RESET_AHEAD_GNSS_SYS_PWR_REG	0x1174
#define EXYNOS_PMU_CLEANY_BUS_SYS_PWR_REG	0x11E8
#define EXYNOS_PMU_LOGIC_RESET_GNSS_SYS_PWR_REG	0x11EC
#define EXYNOS_PMU_TCXO_GATE_GNSS_SYS_PWR_REG	0x11C4
#define EXYNOS_PMU_RESET_ASB_GNSS_SYS_PWR_REG	0x11C8

/* GNSS PMU */
/* For EXYNOS_PMU_GNSS_CTRL Register */
#define GNSS_PWRON                BIT(1)
#define GNSS_RESET_SET            BIT(2)
#define GNSS_START                BIT(3)
#define GNSS_ACTIVE_REQ_EN        BIT(5)
#define GNSS_ACTIVE_REQ_CLR       BIT(6)
#define GNSS_RESET_REQ_EN         BIT(7)
#define GNSS_RESET_REQ_CLR        BIT(8)
#define MASK_GNSS_PWRDN_DONE	BIT(9)
#define RTC_OUT_EN              BIT(10)
#define MASK_SLEEP_START_REQ    BIT(12)
#define SET_SW_SLEEP_START_REQ  BIT(13)
#define GNSS_WAKEUP_REQ_EN		BIT(14)
#define GNSS_WAKEUP_REQ_CLR		BIT(15)
#define CLEANY_BYPASS_END       BIT(16)
#define TCXO_26M_40M_SEL		BIT(17)

#define MEMSIZE_OFFSET	16
#define MEMBASE_ADDR_OFFSET	0
#endif

#define SMC_ID		0x82000700
#define READ_CTRL	0x3
#define WRITE_CTRL	0x4

enum gnss_mode {
	GNSS_POWER_OFF,
	GNSS_POWER_ON,
	GNSS_RESET,
	NUM_GNSS_MODE,
};

enum gnss_int_clear {
	GNSS_INT_WDT_RESET_CLEAR,
	GNSS_INT_ACTIVE_CLEAR,
	GNSS_INT_WAKEUP_CLEAR,
};

enum gnss_tcxo_mode {
	TCXO_SHARED_MODE = 0,
	TCXO_NON_SHARED_MODE = 1,
};

#if defined(CONFIG_SOC_EXYNOS7870)
struct gnss_ctl;
extern int gnss_pmu_init_conf(struct gnss_ctl *);
extern int gnss_pmu_hold_reset(struct gnss_ctl *);
extern int gnss_pmu_release_reset(struct gnss_ctl *);
extern int gnss_pmu_power_on(struct gnss_ctl *, enum gnss_mode mode);
extern int gnss_pmu_clear_interrupt(struct gnss_ctl *,
				enum gnss_int_clear);
extern int gnss_change_tcxo_mode(struct gnss_ctl *, enum gnss_tcxo_mode);
#endif

#endif /* __EXYNOS_PMU_GNSS_H */
