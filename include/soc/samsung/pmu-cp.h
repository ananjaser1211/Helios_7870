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

#ifndef __EXYNOS_PMU_CP_H
#define __EXYNOS_PMU_CP_H __FILE__

/* BLK_ALIVE: CP related SFRs */
#define EXYNOS_PMU_CP_CTRL_NS			0x0030
#define EXYNOS_PMU_CP_CTRL_S			0x0034
#define EXYNOS_PMU_CP_STAT			0x0038
#define EXYNOS_PMU_CP_DEBUG			0x003C
#define EXYNOS_PMU_CP_DURATION			0x0040
#define EXYNOS_PMU_CP2AP_MEM_CONFIG		0x0050

#define EXYNOS_PMU_CP_BOOT_TEST_RST_CONFIG	0x0068
#define EXYNOS_PMU_CP2AP_PERI_ACCESS_WIN	0x006C
#define EXYNOS_PMU_MODAPIF_CONFIG		0x0070
#define EXYNOS_PMU_CP_CLK_CTRL			0x0074
#define EXYNOS_PMU_CP_QOS			0x0078

#define EXYNOS_PMU_CENTRAL_SEQ_CP_CONFIGURATION	0x0280
#define EXYNOS_PMU_RESET_AHEAD_CP_SYS_PWR_REG	0x1170
#define EXYNOS_PMU_CLEANY_BUS_SYS_PWR_REG		0x11CC
#define EXYNOS_PMU_LOGIC_RESET_CP_SYS_PWR_REG	0x11D0
#define EXYNOS_PMU_TCXO_GATE_SYS_PWR_REG		0x11D4
#define EXYNOS_PMU_RESET_ASB_CP_SYS_PWR_REG		0x11D8

#ifdef CONFIG_SOC_EXYNOS8890
#define EXYNOS_PMU_CP2AP_MIF0_PERI_ACCESS_CON	0x0054
#define EXYNOS_PMU_CP2AP_MIF1_PERI_ACCESS_CON	0x0058
#define EXYNOS_PMU_CP2AP_MIF2_PERI_ACCESS_CON	0x005C
#define EXYNOS_PMU_CP2AP_MIF3_PERI_ACCESS_CON	0x0060
#define EXYNOS_PMU_CP2AP_CCORE_PERI_ACCESS_CON	0x0064

#define EXYNOS_PMU_ERROR_CODE_DATA		0x007C
#define EXYNOS_PMU_ERROR_CODE_PERI		0x0080
#endif

#ifdef CONFIG_SOC_EXYNO7870
#define EXYNOS_PMU_CP2AP_MEM_CONFIG     0x0050
#define EXYNOS_PMU_CP2AP_MIF_ACCESS_WIN0    0x0054
#define EXYNOS_PMU_CP2AP_MIF_ACCESS_WIN1    0x0058

#define EXYNOS_PMU_UART_IO_SHARE_CTRL		0x6200
#endif

/* CP PMU */
/* For EXYNOS_PMU_CP_CTRL Register */
#define CP_PWRON                BIT(1)
#define CP_RESET_SET            BIT(2)
#define CP_START                BIT(3)
#define CP_ACTIVE_REQ_EN        BIT(5)
#define CP_ACTIVE_REQ_CLR       BIT(6)
#define CP_RESET_REQ_EN         BIT(7)
#define CP_RESET_REQ_CLR        BIT(8)
#define MASK_CP_PWRDN_DONE      BIT(9)
#define RTC_OUT_EN              BIT(10)
#define MASK_SLEEP_START_REQ    BIT(12)
#define SET_SW_SLEEP_START_REQ  BIT(13)
#define CLEANY_BYPASS_END       BIT(16)

#define SMC_ID		0x82000700
#define READ_CTRL	0x3
#define WRITE_CTRL	0x4

#ifdef CONFIG_SOC_EXYNOS7870
/* UART IO SHARE CTRL */
#define EXYNOS_PMU_UART_IO_SHARE_CTRL	0x6200
#define SEL_CP_UART_DBG			BIT(8)
#define SEL_UART_DBG_GPIO		BIT(4)
#define FUNC_ISO_EN				BIT(0)
#endif

enum cp_mode {
	CP_POWER_ON,
	CP_RESET,
	CP_POWER_OFF,
	NUM_CP_MODE,
};

enum reset_mode {
	CP_HW_RESET,
	CP_SW_RESET,
};

enum cp_control {
	CP_CTRL_S,
	CP_CTRL_NS,
};

extern int exynos_cp_reset(void);
extern int exynos_cp_release(void);
extern int exynos_cp_init(void);
extern int exynos_cp_active_clear(void);
extern int exynos_clear_cp_reset(void);
extern int exynos_get_cp_power_status(void);
extern int exynos_set_cp_power_onoff(enum cp_mode mode);
extern void exynos_sys_powerdown_conf_cp(void);
extern int exynos_pmu_cp_init(void);
#endif /* __EXYNOS_PMU_CP_H */
