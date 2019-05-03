/*
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *      http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/io.h>
#include <linux/gpio.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/input.h>
#endif
#include <linux/sec_ext.h>
#if defined(CONFIG_BATTERY_SAMSUNG_V2)
#include "../../battery_v2/include/sec_battery.h"
#else
#include <linux/battery/sec_battery.h>
#endif
#include <linux/sec_batt.h>

#include <asm/cacheflush.h>
#include <asm/system_misc.h>

#include <soc/samsung/exynos-pmu.h>

/* function ptr for original arm_pm_restart */
void (*mach_restart)(enum reboot_mode mode, const char *cmd);
EXPORT_SYMBOL(mach_restart);

/* INFORM2 */
enum sec_power_flags {
	SEC_POWER_OFF = 0x0,
	SEC_POWER_RESET = 0x12345678,
};
/* INFORM3 */
#define SEC_RESET_REASON_PREFIX 0x12345670
#define SEC_RESET_SET_PREFIX    0xabc00000
enum sec_reset_reason {
	SEC_RESET_REASON_UNKNOWN   = (SEC_RESET_REASON_PREFIX | 0x0),
	SEC_RESET_REASON_DOWNLOAD  = (SEC_RESET_REASON_PREFIX | 0x1),
	SEC_RESET_REASON_UPLOAD    = (SEC_RESET_REASON_PREFIX | 0x2),
	SEC_RESET_REASON_CHARGING  = (SEC_RESET_REASON_PREFIX | 0x3),
	SEC_RESET_REASON_RECOVERY  = (SEC_RESET_REASON_PREFIX | 0x4),
	SEC_RESET_REASON_FOTA      = (SEC_RESET_REASON_PREFIX | 0x5),
	SEC_RESET_REASON_FOTA_BL   = (SEC_RESET_REASON_PREFIX | 0x6), /* update bootloader */
	SEC_RESET_REASON_SECURE    = (SEC_RESET_REASON_PREFIX | 0x7), /* image secure check fail */
	SEC_RESET_REASON_FWUP      = (SEC_RESET_REASON_PREFIX | 0x9), /* emergency firmware update */
	SEC_RESET_REASON_BOOTLOADER      = (SEC_RESET_REASON_PREFIX | 0xd),
	SEC_RESET_REASON_EMERGENCY = 0x0,
	
	#ifdef CONFIG_SEC_DEBUG_MDM_SEPERATE_CRASH
	SEC_RESET_CP_DEBUG         = (SEC_RESET_SET_PREFIX | 0xc0000)
	#endif
	SEC_RESET_SET_DEBUG        = (SEC_RESET_SET_PREFIX | 0xd0000),
	SEC_RESET_SET_SWSEL        = (SEC_RESET_SET_PREFIX | 0xe0000),
	SEC_RESET_SET_SUD          = (SEC_RESET_SET_PREFIX | 0xf0000),

	#ifdef CONFIG_DIAG_MODE
	SEC_RESET_SET_DIAG         = (SEC_RESET_SET_PREFIX | 0xe)	/* Diag enable for CP */
	#endif
};

static void sec_power_off(void)
{
	int poweroff_try = 0;
	struct power_supply *ac_psy = power_supply_get_by_name("ac");
	struct power_supply *usb_psy = power_supply_get_by_name("usb");
	struct power_supply *wc_psy = power_supply_get_by_name("wireless");
	union power_supply_propval ac_val;
	union power_supply_propval water_val;
	union power_supply_propval usb_val;
	union power_supply_propval wc_val;

#ifdef CONFIG_OF
	int powerkey_gpio = -1;
	struct device_node *np, *pp;

	np = of_find_node_by_path("/gpio_keys");
	if (!np)
		return;
	for_each_child_of_node(np, pp) {
		uint keycode = 0;
		if (!of_find_property(pp, "gpios", NULL))
			continue;
		of_property_read_u32(pp, "linux,code", &keycode);
		if (keycode == KEY_POWER) {
			pr_info("%s: <%u>\n", __func__,  keycode);
			powerkey_gpio = of_get_gpio(pp, 0);
			break;
		}
	}
	of_node_put(np);

	if (!gpio_is_valid(powerkey_gpio)) {
		pr_err("Couldn't find power key node\n");
		return;
	}
#else
	int powerkey_gpio = GPIO_nPOWER;
#endif

	local_irq_disable();

	ac_psy->get_property(ac_psy, POWER_SUPPLY_PROP_ONLINE, &ac_val);
	usb_psy->get_property(usb_psy, POWER_SUPPLY_PROP_ONLINE, &usb_val);
	wc_psy->get_property(wc_psy, POWER_SUPPLY_PROP_ONLINE, &wc_val);
#if defined(CONFIG_BATTERY_SAMSUNG_V2)
	ac_psy->get_property(ac_psy, POWER_SUPPLY_EXT_PROP_WATER_DETECT, &water_val);
#else
	water_val.intval = 0;
#endif

	pr_info("[%s] AC[%d] : USB[%d] : WC[%d] : WATER[%d]\n", __func__,
		ac_val.intval, usb_val.intval, wc_val.intval, water_val.intval);

	while (1) {
		/* Check reboot charging */
#ifdef CONFIG_SAMSUNG_BATTERY
		if ((ac_val.intval || water_val.intval || usb_val.intval || wc_val.intval ||
		     (poweroff_try >= 5)) && !lpcharge) {
#else
		if ((ac_val.intval || water_val.intval || usb_val.intval || wc_val.intval ||
		     (poweroff_try >= 5))) {
#endif
			pr_emerg("%s: charger connected or power off "
					"failed(%d), reboot!\n",
					__func__, poweroff_try);
			/* To enter LP charging */
			exynos_pmu_write(EXYNOS_PMU_INFORM2, SEC_POWER_OFF);

#ifdef CONFIG_SEC_DEBUG
			sec_debug_reboot_handler();
#endif
			flush_cache_all();
			mach_restart(REBOOT_SOFT, "sw reset");

			pr_emerg("%s: waiting for reboot\n", __func__);
			while (1)
				;
		}

		/* wait for power button release */
		if (gpio_get_value(powerkey_gpio)) {
			pr_emerg("%s: set PS_HOLD low\n", __func__);

#ifdef CONFIG_SEC_DEBUG
			sec_debug_reboot_handler();
			flush_cache_all();
#endif

			/* power off code
			 * PS_HOLD Out/High -->
			 * Low PS_HOLD_CONTROL, R/W, 0x1002_330C
			 */
			exynos_pmu_update(EXYNOS_PMU_PS_HOLD_CONTROL, 0x1<<8, 0x0);

			++poweroff_try;
			pr_emerg
			    ("%s: Should not reach here! (poweroff_try:%d)\n",
			     __func__, poweroff_try);
		} else {
		/* if power button is not released, wait and check TA again */
			pr_info("%s: PowerButton is not released.\n", __func__);
		}
		mdelay(1000);
	}
}

static void sec_reboot(enum reboot_mode reboot_mode, const char *cmd)
{
	local_irq_disable();

	pr_emerg("%s (%d, %s)\n", __func__, reboot_mode, cmd ? cmd : "(null)");

	/* LPM mode prevention */
	exynos_pmu_write(EXYNOS_PMU_INFORM2, SEC_POWER_RESET);

	if (cmd) {
		unsigned long value;
		if (!strcmp(cmd, "fota"))
			exynos_pmu_write(EXYNOS_PMU_INFORM3, SEC_RESET_REASON_FOTA);
		else if (!strcmp(cmd, "fota_bl"))
			exynos_pmu_write(EXYNOS_PMU_INFORM3, SEC_RESET_REASON_FOTA_BL);
		else if (!strcmp(cmd, "recovery"))
			exynos_pmu_write(EXYNOS_PMU_INFORM3, SEC_RESET_REASON_RECOVERY);
		else if (!strcmp(cmd, "download"))
			exynos_pmu_write(EXYNOS_PMU_INFORM3, SEC_RESET_REASON_DOWNLOAD);
		else if (!strcmp(cmd, "bootloader"))
			exynos_pmu_write(EXYNOS_PMU_INFORM3, SEC_RESET_REASON_BOOTLOADER);
		else if (!strcmp(cmd, "upload"))
			exynos_pmu_write(EXYNOS_PMU_INFORM3, SEC_RESET_REASON_UPLOAD);
		else if (!strcmp(cmd, "secure"))
			exynos_pmu_write(EXYNOS_PMU_INFORM3, SEC_RESET_REASON_SECURE);
		else if (!strcmp(cmd, "fwup"))
			exynos_pmu_write(EXYNOS_PMU_INFORM3, SEC_RESET_REASON_FWUP);
		else if (!strncmp(cmd, "emergency", 9))
			exynos_pmu_write(EXYNOS_PMU_INFORM3, SEC_RESET_REASON_EMERGENCY);
		else if (!strncmp(cmd, "debug", 5)
			 && !kstrtoul(cmd + 5, 0, &value))
			exynos_pmu_write(EXYNOS_PMU_INFORM3, SEC_RESET_SET_DEBUG | value);
#ifdef CONFIG_SEC_DEBUG_MDM_SEPERATE_CRASH
		else if (!strncmp(cmd, "cpdebug", 7)
			 && !kstrtoul(cmd + 7, 0, &value))
			exynos_pmu_write(EXYNOS_PMU_INFORM3, SEC_RESET_SET_CP_DEBUG | value);
#endif
		else if (!strncmp(cmd, "swsel", 5)
			 && !kstrtoul(cmd + 5, 0, &value))
			exynos_pmu_write(EXYNOS_PMU_INFORM3, SEC_RESET_SET_SWSEL | value);
		else if (!strncmp(cmd, "sud", 3)
			 && !kstrtoul(cmd + 3, 0, &value))
			exynos_pmu_write(EXYNOS_PMU_INFORM3, SEC_RESET_SET_SUD | value);
#if defined(CONFIG_DIAG_MODE)
		else if (!strncmp(cmd, "diag", 4)
				&& !kstrtoul(cmd + 4, 0, &value)) {
			exynos_pmu_write(EXYNOS_PMU_INFORM3, SEC_RESET_SET_DIAG | (value & 0x1));
		}
#endif
		else if (!strncmp(cmd, "panic", 5)) {
			/*
			 * This line is intentionally blanked because the INFORM3 is used for upload cause
			 * in sec_debug_set_upload_cause() only in case of  panic() .
			 */
		} else {
			exynos_pmu_write(EXYNOS_PMU_INFORM3, SEC_RESET_REASON_UNKNOWN);
		}
	} else {
		exynos_pmu_write(EXYNOS_PMU_INFORM3, SEC_RESET_REASON_UNKNOWN);
	}

	flush_cache_all();
	mach_restart(REBOOT_SOFT, "sw reset");

	pr_emerg("%s: waiting for reboot\n", __func__);
	while (1)
		;
}

static int __init sec_reboot_init(void)
{
	mach_restart = arm_pm_restart;
	pm_power_off = sec_power_off;
	arm_pm_restart = sec_reboot;
	return 0;
}

subsys_initcall(sec_reboot_init);
