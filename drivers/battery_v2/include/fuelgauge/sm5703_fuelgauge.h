/*
 * drivers/battery/sm5703_fuelgauge.h
 *
 * Header of SiliconMitus SM5703 Fuelgauge Driver
 *
 * Copyright (C) 2015 SiliconMitus
 * Author: SW Jung
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifndef SM5703_FUELGAUGE_H
#define SM5703_FUELGAUGE_H

#include <linux/i2c.h>
#include <linux/mfd/sm5703.h>
#include "../sec_charging_common.h"

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#endif /* #ifdef CONFIG_DEBUG_FS */

#define FG_DRIVER_VER "0.0.0.1"
#define USE_SUSPEND_LATE

#if defined(CONFIG_BATTERY_AGE_FORECAST)
#define ENABLE_BATT_LONG_LIFE 1
#endif

enum sm5703_valrt_mode {
	SM5703_NORMAL_MODE = 0,
	SM5703_RECOVERY_MODE,
	SM5703_COLD_MODE,
};

enum sm5703_variants {
	NOVEL = 0,
	J2LTE,
	O5LTE,
	J3XATT,
	J7LTE,
};

struct battery_data_t {
	const int battery_type; /* 4200 or 4350 or 4400*/
	const int battery_table[3][16];
	const int rce_value[3];
	const int dtcd_value;
	const int rs_value[4];
	const int vit_period;
	const int mix_value[2];
	const int topoff_soc[2];
	const int volt_cal;
	const int curr_cal;
	const int temp_std;
	const int temp_offset;
	const int temp_offset_cal;
	const int charge_offset_cal;
};

struct sm5703_fg_info {
	bool is_low_batt_alarm;
	/* State Of Connect */
	int online;
	/* battery SOC (capacity) */
	int batt_soc;
	/* battery voltage */
	int batt_voltage;
	/* battery AvgVoltage */
	int batt_avgvoltage;
	/* battery OCV */
	int batt_ocv;
	/* Current */
	int batt_current;
	/* battery Avg Current */
	int batt_avgcurrent;

	struct battery_data_t *comp_pdata;

	struct mutex param_lock;
	/* copy from platform data /
	 * DTS or update by shell script */

	struct mutex io_lock;
	struct device *dev;
	int32_t temperature; /* 0.1 deg C*/
	int32_t temp_fg; /* 0.1 deg C*/
	/* register programming */
	int reg_addr;
	u8 reg_data[2];

	int battery_table[3][16];
#ifdef ENABLE_BATT_LONG_LIFE
#ifdef CONFIG_BATTERY_AGE_FORECAST_DETACHABLE
	int v_max_table[3];
	int q_max_table[3];
#else
	int v_max_table[5];
	int q_max_table[5];
#endif
	int v_max_now;
	int q_max_now;
#endif
	int rce_value[3];
	int dtcd_value;
	int rs_value[4]; /*rs mix_factor max min*/
	int vit_period;
	int mix_value[2]; /*mix_rate init_blank*/

	int enable_topoff_soc;
	int topoff_soc;

	int volt_cal;
	int curr_cal;

	int temp_std;
	int temp_offset;
	int temp_offset_cal;
	int charge_offset_cal;

	int battery_type; /* 4200 or 4350 or 4400*/
	uint32_t soc_alert_flag : 1;  /* 0 : nu-occur, 1: occur */
	uint32_t volt_alert_flag : 1; /* 0 : nu-occur, 1: occur */
	uint32_t flag_full_charge : 1; /* 0 : no , 1 : yes*/
	uint32_t flag_chg_status : 1; /* 0 : discharging, 1: charging*/

	int32_t irq_ctrl;

	uint32_t is_FG_initialised;
	int iocv_error_count;

	int n_tem_poff;
	int n_tem_poff_offset;
	int l_tem_poff;
	int l_tem_poff_offset;

	/* previous battery voltage current*/
	int p_batt_voltage;
	int p_batt_current;
};

struct sm5703_platform_data {
	int capacity_max;
	int capacity_max_margin;
	int capacity_min;
	int capacity_calculation_type;
	int fuel_alert_soc;
	int fullsocthr;
	int fg_irq;
	int model_type;
	unsigned long fg_irq_attr;

	char *fuelgauge_name;
#if defined(CONFIG_BATTERY_AGE_FORECAST)
	int num_age_step;
	int age_step;
	int age_data_length;
	sec_age_data_t* age_data;
#endif
};

struct sm5703_fuelgauge_data {
	struct device           *dev;
	struct i2c_client       *i2c;
	struct mutex            fuelgauge_mutex;
	struct sm5703_platform_data *pdata;
	struct power_supply             psy_fg;
	struct delayed_work isr_work;

	int cable_type;
	bool is_charging;

	/* HW-dedicated fuel guage info structure
	 * used in individual fuel gauge file only
	 * (ex. dummy_fuelgauge.c)
	 */
	struct sm5703_fg_info	info;
	struct battery_data_t	*battery_data;

	bool is_fuel_alerted;
	struct wake_lock fuel_alert_wake_lock;

	unsigned int capacity_old;      /* only for atomic calculation */
	unsigned int capacity_max;      /* only for dynamic calculation */
	unsigned int standard_capacity;
	
#if defined(CONFIG_BATTERY_AGE_FORECAST)
	unsigned int chg_float_voltage; /* BATTERY_AGE_FORECAST */
#endif

	bool initial_update_of_soc;
	struct mutex fg_lock;

	/* register programming */
	int reg_addr;
	u8 reg_data[2];

	unsigned int pre_soc;
	int fg_irq;
	int force_dec_mode;
#ifdef USE_SUSPEND_LATE
	bool	is_sleep_state;
#endif
};

#endif // SM5703_FUELGAUGE_H
