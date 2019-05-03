/*
  * Samsung Mobile VE Group.
  *
  * drivers/battery/sec_batt_dischg_no_ic_by_policy.h
  *
  * Common header for for samsung batter self discharging.
  *
  * Copyright (C) 2015, Samsung Electronics.
  *
  * This program is free software. You can redistribute it and/or modify
  * it under the terms of the GNU General Public License version 2 as
  * published by the Free Software Foundation
  */

#ifndef __SDCHG_COMMON_H__
#define __SDCHG_COMMON_H__

/******************************************/
// Original Header
#include <linux/pm.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/power_supply.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/cpu.h>
#include <linux/sched.h>
#include <linux/fb.h>
#include <linux/pm_qos.h>
#include <linux/reboot.h>
#include <linux/alarmtimer.h>
#include <linux/suspend.h>

/******************************************/
/* Code Control Feature (for no discharging IC) */
/******************************************/
//#define SDCHG_SELF_TEST
//#define SDCHG_CHECK_TYPE_SOC  // for using soc(not voltage)
/******************************************/

/******************************************/
/* Condition Feature (for no discharging IC) */
/******************************************/
/* default value (applied in case of no having dt data) */
#define SDCHG_TEMP_START	600
#define SDCHG_TEMP_END		550

#ifdef SDCHG_SELF_TEST
#define SDCHG_SOC_START			96	//99	,93
#define SDCHG_SOC_END			92	//98	,92
#define SDCHG_VOLTAGE_START	4250	// 4320
#define SDCHG_VOLTAGE_END		4200	// 4300
#else
#define SDCHG_SOC_START			96
#define SDCHG_SOC_END			92
#define SDCHG_VOLTAGE_START	4250	// Soc 96
#define SDCHG_VOLTAGE_END		4200	// Soc 92
#endif
/******************************************/
#ifdef SDCHG_CHECK_TYPE_SOC
#define SDCHG_BATTCOND_START	sdchg_info->soc_start
#define SDCHG_BATTCOND_END		sdchg_info->soc_end
#else
#define SDCHG_BATTCOND_START	sdchg_info->voltage_start
#define SDCHG_BATTCOND_END		sdchg_info->voltage_end
#endif
/******************************************/
/* for no discharging IC */
/******************************************/
enum __sdchg_state__ {
	SDCHG_STATE_NONE = 0,
	SDCHG_STATE_SET,
	SDCHG_STATE_SET_DISPLAY_ON,
#ifdef SDCHG_SUB_POLICY_SET
	SDCHG_STATE_SET_LOW,
	SDCHG_STATE_SET_LOW_DISPLAY_ON,
#endif
	SDCHG_STATE_MAX
};

enum __sdchg_charger_type__ {
	SDCHG_CHARGER_NONE = 0,
	SDCHG_CHARGER_MUIC,
	SDCHG_CHARGER_WIRELESS,
	SDCHG_CHARGER_MAX
};
/******************************************/
struct sdchg_info_chip_t
{
	int factory_discharging;
	u32 adc_max;
	u32 adc_min;
	u32 ntc_limit;
	bool sdchg_en;
	struct sdchg_info_t *pinfo;
};

struct sdchg_info_nochip_t
{
	int need_state;
	int set_state;

	struct wake_lock wake_lock;
	struct wake_lock end_wake_lock;
	bool wake_lock_set;

#ifdef CONFIG_FB
	bool display_on;
	struct notifier_block fb_nb;
#endif
	bool state_machine_run;

	void (*sdchg_monitor)(void *, __kernel_time_t, bool);

	struct sdchg_info_t *pinfo;	// struct sec_battery_info

	void *pData;		// personal data
};

struct sdchg_info_t
{
	char *type;

	struct list_head info_list;

	void *battery;

	/*********************/
	/* dt data */
	u32 temp_start;
	u32 temp_end;
	u32 soc_start;
	u32 soc_end;
	u32 voltage_start;
	u32 voltage_end;	// use battery,swelling_drop_float_voltage
	/*********************/

	// func
	int (*sdchg_probe)(void *);
	int (*sdchg_remove)(void);
	void (*sdchg_parse_dt)(struct device *);

	// factory func
	void (*sdchg_adc_check)(void *);
	void (*sdchg_ntc_check)(void *);
	void (*sdchg_force_control)(void *, bool);
	void (*sdchg_discharging_check)(void *);

	int (*sdchg_force_check)(void *);

	// nochip data
	struct sdchg_info_chip_t *chip;
	struct sdchg_info_nochip_t *nochip;
};

extern struct list_head sdchg_info_head;
extern bool sdchg_nochip_support;

extern int wireless_charger_notifier_register(struct notifier_block *nb, notifier_fn_t notifier);
extern int wireless_charger_notifier_unregister(struct notifier_block *nb);

extern bool sdchg_check_polling_time(__kernel_time_t curr);
extern void sdchg_set_polling_time(int polling_time);
extern unsigned int sdchg_get_polling_time(unsigned int cur_polling_time);

#endif	// #ifndef __SDCHG_COMMON_H__


