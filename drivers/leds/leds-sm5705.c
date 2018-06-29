/*
 * Flash-LED device driver for SM5705
 *
 * Copyright (C) 2015 Silicon Mitus
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/workqueue.h>
#include <linux/leds/leds-sm5705.h>
#include <linux/mfd/sm5705.h>
#include <linux/muic/muic_afc.h>
#include <linux/battery/charger/sm5705_charger_oper.h>

enum {
	SM5705_FLED_OFF_MODE					= 0x0,
	SM5705_FLED_ON_MOVIE_MODE				= 0x1,
	SM5705_FLED_ON_FLASH_MODE				= 0x2,
	SM5705_FLED_ON_EXTERNAL_CONTROL_MODE	= 0x3,
};

struct sm5705_fled_info {
	struct device *dev;
	struct i2c_client *i2c;

	struct sm5705_fled_platform_data *pdata;
	struct device *rear_fled_dev;
	struct mutex led_lock;
};

static struct sm5705_fled_info *g_sm5705_fled;
static bool fimc_is_activated = 0;
static bool assistive_light = false;
#if defined(CONFIG_MUIC_UNIVERSAL_SM5705_AFC)
static bool muic_flash_on_status = false;
#endif

extern struct class *camera_class; /*sys/class/camera*/
extern int sm5705_call_fg_device_id(void);

void sm5705_fled_lock(struct sm5705_fled_info *fled_info)
{
	mutex_lock(&fled_info->led_lock);
}

void sm5705_fled_unlock(struct sm5705_fled_info *fled_info)
{
	mutex_unlock(&fled_info->led_lock);
}

static inline int __get_revision_number(void)
{
	return sm5705_call_fg_device_id();
}

/**
 * SM5705 Flash-LEDs device register control functions
 */
static int sm5705_FLEDx_mode_enable(struct sm5705_fled_info *sm5705_fled,
	int index, unsigned char FLEDxEN)
{
	int ret;

	ret = sm5705_update_reg(sm5705_fled->i2c,
		SM5705_REG_FLED1CNTL1 + (index * 4),
		(FLEDxEN & 0x3), 0x3);
	if (IS_ERR_VALUE(ret)) {
		dev_err(sm5705_fled->dev, "%s: fail to update REG:FLED%dEN (value=%d)\n",
			__func__, index, FLEDxEN);
		return ret;
	}

	dev_info(sm5705_fled->dev, "%s: FLED[%d] set mode = %d\n",
		__func__, index, FLEDxEN);

	return 0;
}

static inline unsigned char _calc_flash_current_offset_to_mA(
	unsigned short current_mA)
{
	return current_mA < 700 ?
		(((current_mA - 300) / 25) & 0x1F) :((((current_mA - 700) / 50) + 0xF) & 0x1F);
}

static int sm5705_FLEDx_set_flash_current(struct sm5705_fled_info *sm5705_fled,
	int index, unsigned short current_mA)
{
	int ret;
	unsigned char reg_val;

	reg_val = _calc_flash_current_offset_to_mA(current_mA);
	ret = sm5705_write_reg(sm5705_fled->i2c, SM5705_REG_FLED1CNTL3 + (index * 4),
		reg_val);
	if (IS_ERR_VALUE(ret)) {
		dev_err(sm5705_fled->dev, "%s: fail to write REG:FLED%dCNTL3 (value=%d)\n",
			__func__, index, reg_val);
		return ret;
	}

	return 0;
}

static inline unsigned char _calc_torch_current_offset_to_mA(
		unsigned short current_mA)
{
	if (current_mA > 320)
		current_mA = 320;

	return (((current_mA - 10) / 10) & 0x1F);
}

static inline unsigned short _calc_torch_current_mA_to_offset(unsigned char offset)
{
	return (((offset & 0x1F) + 1) * 10);
}

static int sm5705_FLEDx_set_torch_current(
	struct sm5705_fled_info *sm5705_fled, int index, unsigned short current_mA)
{
	int ret;
	unsigned char reg_val;

	reg_val = _calc_torch_current_offset_to_mA(current_mA);
	ret = sm5705_write_reg(sm5705_fled->i2c, SM5705_REG_FLED1CNTL4 + (index * 4),
		reg_val);
	if (IS_ERR_VALUE(ret)) {
		dev_err(sm5705_fled->dev, "%s: fail to write REG:FLED%dCNTL4 (value=%d)\n",
			__func__, index, reg_val);
		return ret;
	}

	return 0;
}

#if defined(CONFIG_MUIC_UNIVERSAL_SM5705_AFC)
/**
 * SM5705 Flash-LED to MUIC interface functions
 */
/*#define IGNORE_AFC_STATE*/
static inline bool sm5705_fled_check_valid_vbus_from_MUIC(void)
{
	if (muic_check_afc_state(1) == 1) {
		return true; /* Can use FLED */
	}
	return false; /* Can NOT use FLED*/
}

static inline int sm5705_fled_muic_flash_work_on(
	struct sm5705_fled_info *sm5705_fled)
{
	/* MUIC 9V -> 5V function */
	muic_check_afc_state(1);
	/*muic_dpreset_afc();*/
	return 0;
}

static inline int sm5705_fled_muic_flash_work_off(
	struct sm5705_fled_info *sm5705_fled)
{
	/* MUIC 5V -> 9V function */
	muic_check_afc_state(0);
	/*muic_restart_afc();*/

	return 0;
}

static int sm5705_fled_muic_flash_on_prepare(void)
{
	int ret = 0;

	if (muic_torch_prepare(1) == 1) {
		muic_flash_on_status = true;
		ret = 0;
	} else {
		pr_err("%s: fail to prepare for AFC V_drop\n", __func__);
		ret = -1;
	}

	return ret;
}

static void sm5705_fled_muic_flash_off_prepare(void)
{
	if (muic_flash_on_status == true) {
		muic_torch_prepare(0);
		muic_flash_on_status = false;
	}
}
#endif
/**
 * SM5705 Flash-LED operation control functions
 */
static int sm5705_fled_initialize(struct sm5705_fled_info *sm5705_fled)
{
	struct device *dev = sm5705_fled->dev;
	struct sm5705_fled_platform_data *pdata = sm5705_fled->pdata;
	int i, ret;

	for (i=0; i < SM5705_FLED_MAX; ++i) {
		if (pdata->led[i].used_gpio) {
			ret = gpio_request(pdata->led[i].flash_en_pin, "sm5705_fled");
			if (IS_ERR_VALUE(ret)) {
				dev_err(dev, "%s: fail to request flash gpio pin = %d (ret=%d)\n",
					__func__, pdata->led[i].flash_en_pin, ret);
				return ret;
			}
			gpio_direction_output(pdata->led[i].flash_en_pin, 0);

			ret = gpio_request(pdata->led[i].torch_en_pin, "sm5705_fled");
			if (IS_ERR_VALUE(ret)) {
				dev_err(dev, "%s: fail to request torch gpio pin = %d (ret=%d)\n",
					__func__, pdata->led[i].torch_en_pin, ret);
				return ret;
			}
			gpio_direction_output(pdata->led[i].torch_en_pin, 0);

			dev_info(dev, "SM5705 FLED[%d] used External GPIO control Mode \
				(Flash pin=%d, Torch pin=%d)\n",
				i, pdata->led[i].flash_en_pin, pdata->led[i].torch_en_pin);
		} else {
			dev_info(dev, "SM5705 FLED[%d] used I2C control Mode\n", i);
		}
		ret = sm5705_FLEDx_mode_enable(sm5705_fled, i, SM5705_FLED_OFF_MODE);
		if (IS_ERR_VALUE(ret)) {
			dev_err(dev, "%s: fail to set FLED[%d] external control mode\n", __func__, i);
			return ret;
		}
	}

	return 0;
}

static void sm5705_fled_deinitialize(struct sm5705_fled_info *sm5705_fled)
{
	struct device *dev = sm5705_fled->dev;
	struct sm5705_fled_platform_data *pdata = sm5705_fled->pdata;
	int i;

	for (i=0; i < SM5705_FLED_MAX; ++i) {
		if (pdata->led[i].used_gpio) {
			gpio_free(pdata->led[i].flash_en_pin);
			gpio_free(pdata->led[i].torch_en_pin);
		}
		sm5705_FLEDx_mode_enable(sm5705_fled, i, SM5705_FLED_OFF_MODE);
	}

	dev_info(dev, "%s: FLEDs de-initialize done.\n", __func__);
}

static inline int _fled_turn_on_torch(struct sm5705_fled_info *sm5705_fled, int index)
{
	struct sm5705_fled_platform_data *pdata = sm5705_fled->pdata;
	struct device *dev = sm5705_fled->dev;
	int ret;

	if (pdata->led[index].used_gpio) {
		ret = sm5705_FLEDx_mode_enable(sm5705_fled, index,
			SM5705_FLED_ON_EXTERNAL_CONTROL_MODE);
		if (IS_ERR_VALUE(ret)) {
			dev_err(dev, "%s: fail to set FLED[%d] External control mode\n",
				__func__, index);
			return ret;
		}
		gpio_set_value(pdata->led[index].flash_en_pin, 0);
		gpio_set_value(pdata->led[index].torch_en_pin, 1);
	} else {
		ret = sm5705_FLEDx_mode_enable(sm5705_fled, index,
			SM5705_FLED_ON_MOVIE_MODE);
		if (IS_ERR_VALUE(ret)) {
			dev_err(dev, "%s: fail to set FLED[%d] Movie mode\n", __func__, index);
			return ret;
		}
	}
	sm5705_charger_oper_push_event(SM5705_CHARGER_OP_EVENT_TORCH, 1);

	dev_info(dev, "%s: FLED[%d] Torch turn-on done.\n", __func__, index);

	return 0;
}

static int sm5705_fled_turn_on_torch(struct sm5705_fled_info *sm5705_fled,
	int index, unsigned short current_mA)
{
	struct device *dev = sm5705_fled->dev;
	int ret;
#if defined(CONFIG_MUIC_UNIVERSAL_SM5705_AFC)
	ret = sm5705_fled_muic_flash_on_prepare();

	if (ret < 0) {
		dev_err(dev, "%s: fail to prepare for AFC V_drop\n", __func__);
	}
#endif
	ret = sm5705_FLEDx_set_torch_current(sm5705_fled, index, current_mA);
	if (IS_ERR_VALUE(ret)) {
		dev_err(dev, "%s: fail to set FLED[%d] torch current (current_mA=%d)\n",
			__func__, index, current_mA);
		return ret;
	}

	_fled_turn_on_torch(sm5705_fled, index);

	return 0;
}

#if 0
static int sm5705_fled_turn_on_flash(struct sm5705_fled_info *sm5705_fled, int index,
	unsigned short current_mA)
{
	struct device *dev = sm5705_fled->dev;
	struct sm5705_fled_platform_data *pdata = sm5705_fled->pdata;
	int ret;

	ret = sm5705_FLEDx_set_flash_current(sm5705_fled, index, current_mA);
	if (IS_ERR_VALUE(ret)) {
		dev_err(dev, "%s: fail to set FLED[%d] flash current (current_mA=%d)\n",
			__func__, index, current_mA);
		return ret;
	}

	if (pdata->led[index].used_gpio) {
		ret = sm5705_FLEDx_mode_enable(sm5705_fled, index,
			SM5705_FLED_ON_EXTERNAL_CONTROL_MODE);
		if (IS_ERR_VALUE(ret)) {
			dev_err(dev, "%s: fail to set FLED[%d] External control mode\n",
				__func__, index);
			return ret;
		}
		gpio_set_value(pdata->led[index].torch_en_pin, 0);
		gpio_set_value(pdata->led[index].flash_en_pin, 1);
	} else {
		ret = sm5705_FLEDx_mode_enable(sm5705_fled, index,
			SM5705_FLED_ON_FLASH_MODE);
		if (IS_ERR_VALUE(ret)) {
			dev_err(dev, "%s: fail to set FLED[%d] Flash mode\n",__func__, index);
			return ret;
		}
	}
	sm5705_charger_oper_push_event(SM5705_CHARGER_OP_EVENT_FLASH, 1);

	dev_info(dev, "%s: FLED[%d] Flash turn-on done.\n", __func__, index);

	return 0;
}
#endif

static int sm5705_fled_turn_off(struct sm5705_fled_info *sm5705_fled, int index)
{
	struct device *dev = sm5705_fled->dev;
	struct sm5705_fled_platform_data *pdata = sm5705_fled->pdata;
	int ret;

	if (pdata->led[index].used_gpio) {
		gpio_set_value(pdata->led[index].flash_en_pin, 0);
		gpio_set_value(pdata->led[index].torch_en_pin, 0);
	}

	ret = sm5705_FLEDx_mode_enable(sm5705_fled, index, SM5705_FLED_OFF_MODE);
	if (IS_ERR_VALUE(ret)) {
		dev_err(dev, "%s: fail to set FLED[%d] OFF mode\n", __func__, index);
		return ret;
	}

	ret = sm5705_FLEDx_set_flash_current(sm5705_fled, index, 0);
	if (IS_ERR_VALUE(ret)) {
		dev_err(dev, "%s: fail to set FLED[%d] flash current\n", __func__, index);
		return ret;
	}

	ret = sm5705_FLEDx_set_torch_current(sm5705_fled, index, 0);
	if (IS_ERR_VALUE(ret)) {
		dev_err(dev, "%s: fail to set FLED[%d] torch current\n", __func__, index);
		return ret;
	}

	sm5705_charger_oper_push_event(SM5705_CHARGER_OP_EVENT_FLASH, 0);
	sm5705_charger_oper_push_event(SM5705_CHARGER_OP_EVENT_TORCH, 0);

#if defined(CONFIG_MUIC_UNIVERSAL_SM5705_AFC)
	sm5705_fled_muic_flash_off_prepare();
#endif
	dev_info(dev, "%s: FLED[%d] turn-off done.\n", __func__, index);

	return 0;
}

/**
 *  For Export Flash control functions (external GPIO control)
 */
int sm5705_fled_prepare_flash(unsigned char index)
{
	if (fimc_is_activated == 1) {
		/* skip to overlapping function calls */
		return 0;
	}

	if (assistive_light == true) {
		pr_info("%s : assistive_light is enabled \n", __func__);
		return 0;
	}

	if (g_sm5705_fled == NULL) {
		pr_err("sm5705-fled: %s: invalid g_sm5705_fled, maybe not registed fled \
			device driver\n", __func__);
		return -ENXIO;
	}

	if (g_sm5705_fled->pdata->led[index].used_gpio == 0) {
		pr_err("sm5705-fled: %s: can't used external GPIO control, check device tree\n",
			__func__);
		return -ENOENT;
	}

#if defined(CONFIG_MUIC_UNIVERSAL_SM5705_AFC)
#ifdef IGNORE_AFC_STATE
	sm5705_fled_muic_flash_work_on(g_sm5705_fled);
#else
	if (sm5705_charger_oper_get_current_op_mode() == SM5705_CHARGER_OP_MODE_CHG_ON) {
		/* W/A : for protect form VBUS drop */
		sm5705_charger_oper_push_event(SM5705_CHARGER_OP_EVENT_FLASH, 1);
		if (!sm5705_fled_check_valid_vbus_from_MUIC()) {
			pr_err("%s: Can't used FLED, because of failed AFC V_drop\n", __func__);
			sm5705_charger_oper_push_event(SM5705_CHARGER_OP_EVENT_FLASH, 0);
			return -1;
		}
		sm5705_charger_oper_push_event(SM5705_CHARGER_OP_EVENT_FLASH, 0);
	} else {
		if (!sm5705_fled_check_valid_vbus_from_MUIC()) {
			pr_err("%s: Can't used FLED, because of failed AFC V_drop\n", __func__);
			return -1;
		}
	}
#endif
#endif

	sm5705_FLEDx_set_torch_current(g_sm5705_fled, index,
		g_sm5705_fled->pdata->led[index].preflash_current_mA);
	sm5705_FLEDx_set_flash_current(g_sm5705_fled, index,
		g_sm5705_fled->pdata->led[index].flash_current_mA);

	sm5705_FLEDx_mode_enable(g_sm5705_fled, index,
		SM5705_FLED_ON_EXTERNAL_CONTROL_MODE);

	fimc_is_activated = 1;

	return 0;
}
EXPORT_SYMBOL(sm5705_fled_prepare_flash);

int sm5705_fled_torch_on(unsigned char index, unsigned char mode)
{
	dev_info(g_sm5705_fled->dev, "%s: %s - ON : E\n", __func__,
		mode == SM5705_FLED_PREFLASH ? "Pre-flash" : "Movie");

	if (mode == SM5705_FLED_PREFLASH) {
		dev_info(g_sm5705_fled->dev, "%s: set preflash_current_mA(%d mA)\n",
			__func__, g_sm5705_fled->pdata->led[index].preflash_current_mA);
		sm5705_FLEDx_set_torch_current(g_sm5705_fled, index,
			g_sm5705_fled->pdata->led[index].preflash_current_mA);
	} else {
		dev_info(g_sm5705_fled->dev, "%s: set movie_current_mA(%d mA)\n",
			__func__, g_sm5705_fled->pdata->led[index].movie_current_mA);
		sm5705_FLEDx_set_torch_current(g_sm5705_fled, index,
			g_sm5705_fled->pdata->led[index].movie_current_mA);
	}

	if (fimc_is_activated != 1) {
#if defined(CONFIG_MUIC_UNIVERSAL_SM5705_AFC)
#ifdef IGNORE_AFC_STATE
		sm5705_fled_muic_flash_work_on(g_sm5705_fled);
#else
		if (!sm5705_fled_check_valid_vbus_from_MUIC()) {
			pr_err("%s: Can't used FLED, because of failed AFC V_drop\n", __func__);
			return -1;
		}
#endif
#endif
		sm5705_FLEDx_set_flash_current(g_sm5705_fled, index,
			g_sm5705_fled->pdata->led[index].flash_current_mA);

		sm5705_FLEDx_mode_enable(g_sm5705_fled, index, SM5705_FLED_ON_EXTERNAL_CONTROL_MODE);
		fimc_is_activated = 1;
	}

	sm5705_fled_lock(g_sm5705_fled);
	sm5705_charger_oper_push_event(SM5705_CHARGER_OP_EVENT_FLASH, 0);
	sm5705_charger_oper_push_event(SM5705_CHARGER_OP_EVENT_TORCH, 1);
	sm5705_fled_unlock(g_sm5705_fled);
	dev_info(g_sm5705_fled->dev, "%s: %s - ON : X\n", __func__,
		mode == SM5705_FLED_PREFLASH ? "Pre-flash" : "Movie");
	return 0;
}
EXPORT_SYMBOL(sm5705_fled_torch_on);

int sm5705_fled_flash_on(unsigned char index)
{
	dev_info(g_sm5705_fled->dev, "%s: Flash - ON : E\n", __func__);

	sm5705_fled_lock(g_sm5705_fled);
	sm5705_charger_oper_push_event(SM5705_CHARGER_OP_EVENT_TORCH, 0);
	sm5705_charger_oper_push_event(SM5705_CHARGER_OP_EVENT_FLASH, 1);
	sm5705_fled_unlock(g_sm5705_fled);
	dev_info(g_sm5705_fled->dev, "%s: Flash - ON : X\n", __func__);
	return 0;
}
EXPORT_SYMBOL(sm5705_fled_flash_on);

int sm5705_fled_led_off(unsigned char index)
{
	if (assistive_light == true) {
		pr_info("%s : assistive_light is enabled \n", __func__);
		return 0;
	}

	dev_info(g_sm5705_fled->dev, "%s: LED - OFF : E\n", __func__);
	sm5705_fled_lock(g_sm5705_fled);
	sm5705_charger_oper_push_event(SM5705_CHARGER_OP_EVENT_TORCH, 0);
	sm5705_charger_oper_push_event(SM5705_CHARGER_OP_EVENT_FLASH, 0);
	sm5705_fled_unlock(g_sm5705_fled);
	dev_info(g_sm5705_fled->dev, "%s: LED - OFF : X\n", __func__);
	return 0;
}
EXPORT_SYMBOL(sm5705_fled_led_off);

int sm5705_fled_close_flash(unsigned char index)
{
	if (fimc_is_activated == 0) {
		/* skip to overlapping function calls */
		return 0;
	}

	dev_info(g_sm5705_fled->dev, "%s: Close Process\n", __func__);

	if (g_sm5705_fled == NULL) {
		pr_err("sm5705-fled: %s: invalid g_sm5705_fled, maybe not registed fled \
			device driver\n", __func__);
		return -ENXIO;
	}

	sm5705_fled_led_off(SM5705_FLED_0);
	sm5705_FLEDx_mode_enable(g_sm5705_fled, index, SM5705_FLED_OFF_MODE);

#if defined(CONFIG_MUIC_UNIVERSAL_SM5705_AFC)
	sm5705_fled_muic_flash_work_off(g_sm5705_fled);
#endif
	fimc_is_activated = 0;

	return 0;
}
EXPORT_SYMBOL(sm5705_fled_close_flash);

/**
 * For Camera-class Rear Flash device file support functions
 */
#define REAR_FLASH_INDEX	SM5705_FLED_0

static ssize_t sm5705_rear_flash_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct sm5705_fled_info *sm5705_fled = dev_get_drvdata(dev->parent);
	int ret, value ;
	int torch_current = 0;

	if ((buf == NULL) || kstrtouint(buf, 10, &value)) {
		return -1;
	}

	/* temp error canceling code */
	if (sm5705_fled != g_sm5705_fled) {
		dev_info(dev, "%s: sm5705_fled handler mismatched (g_handle:%p , l_handle:%p)\n",
			__func__, g_sm5705_fled, sm5705_fled);
		sm5705_fled = g_sm5705_fled;
	}

	dev_info(dev, "%s: %s - value(%d)\n", __func__,
		value == 0 ? "Torch OFF" : "Torch ON", value);

	if (value == 0) {
		/* Turn off Torch */
		assistive_light = false;
		ret = sm5705_fled_turn_off(sm5705_fled, REAR_FLASH_INDEX);
	} else if (value == 1) {
		/* Turn on Torch */
		assistive_light = true;
		fimc_is_activated = 0;
		ret = sm5705_fled_turn_on_torch(sm5705_fled, REAR_FLASH_INDEX, 
			g_sm5705_fled->pdata->led[REAR_FLASH_INDEX].torch_current_mA);
	} else if (value == 100) {
		/* Factory mode Turn on Torch */
		assistive_light = true;
		ret = sm5705_fled_turn_on_torch(sm5705_fled, REAR_FLASH_INDEX,
			g_sm5705_fled->pdata->led[REAR_FLASH_INDEX].factory_current_mA);
	} else if (1001 <= value && value <= 1010) {
		/* (value) 1001, 1002, 1004, 1006, 1009 */
		assistive_light = true;
		fimc_is_activated = 0;
		if (value <= 1001)
			torch_current = g_sm5705_fled->pdata->led[REAR_FLASH_INDEX].torch_table[0];
		else if (value <= 1002)
			torch_current = g_sm5705_fled->pdata->led[REAR_FLASH_INDEX].torch_table[1];
		else if (value <= 1004)
			torch_current = g_sm5705_fled->pdata->led[REAR_FLASH_INDEX].torch_table[2];
		else if (value <= 1006)
			torch_current = g_sm5705_fled->pdata->led[REAR_FLASH_INDEX].torch_table[3];
		else if (value <= 1009)
			torch_current = g_sm5705_fled->pdata->led[REAR_FLASH_INDEX].torch_table[4];
		else
			torch_current = g_sm5705_fled->pdata->led[REAR_FLASH_INDEX].torch_table[2];

		dev_info(dev, "%s, torch_current:%d\n", __func__, torch_current);
		ret = sm5705_fled_turn_on_torch(sm5705_fled, REAR_FLASH_INDEX, torch_current);
	} else {
		dev_info(dev, "%s, Invalid value:%d\n", __func__, value);
	}

	if (IS_ERR_VALUE(ret)) {
		dev_err(dev, "%s: fail to rear flash file operation:store (value=%d, ret=%d)\n",
			__func__, value, ret);
	}

	return count;
}

static ssize_t sm5705_rear_flash_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	unsigned char offset = _calc_torch_current_offset_to_mA(320);

	dev_info(dev, "%s: SM5705 Movie mode max current = 320mA(offset:%d)\n",
		__func__, offset);

	return sprintf(buf, "%d\n", offset);
}

static DEVICE_ATTR(rear_flash, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH,
	sm5705_rear_flash_show, sm5705_rear_flash_store);
static DEVICE_ATTR(rear_torch_flash, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH,
	sm5705_rear_flash_show, sm5705_rear_flash_store);

/**
 * SM5705 Flash-LED device driver management functions
 */

#ifdef CONFIG_OF
static int sm5705_fled_parse_dt(struct device *dev,
	struct sm5705_fled_platform_data *pdata)
{
	struct device_node *nproot = dev->parent->of_node;
	struct device_node *np, *c_np;
	unsigned int temp;
	int index;
	int ret = 0;

	np = of_find_node_by_name(nproot, "flash");
	if (unlikely(np == NULL)) {
		dev_err(dev, "%s: fail to find flash node\n", __func__);
		return ret;
	}

	for_each_child_of_node(np, c_np) {
		ret = of_property_read_u32(c_np, "id", &temp);
		if (ret) {
			dev_err(dev, "%s: fail to get a id\n", __func__);
			return ret;
		}
		index = temp;

		ret = of_property_read_u32(c_np, "pre-flash-current-mA", &temp);
		if (ret) {
			temp = 150;
			dev_err(dev, "%s: fail to get dt, ret(%d): set default pre-flash-current-mA(%d mA)\n",
				__func__, ret, temp);

		}
		pdata->led[index].preflash_current_mA = temp;

		ret = of_property_read_u32(c_np, "flash-mode-current-mA", &temp);
		if (ret) {
			temp = 1000;
			dev_err(dev, "%s: fail to get dt, ret(%d): set default flash-mode-current-mA(%d mA)\n",
				__func__, ret, temp);
		}
		pdata->led[index].flash_current_mA = temp;

		ret = of_property_read_u32(c_np, "movie-mode-current-mA", &temp);
		if (ret) {
			temp = 150;
			dev_err(dev, "%s: fail to get dt, ret(%d): set default movie-mode-current-mA(%d mA)\n",
				__func__, ret, temp);
		}
		pdata->led[index].movie_current_mA = temp;

		ret = of_property_read_u32(c_np, "torch-mode-current-mA", &temp);
		if (ret) {
			temp = 60;
			dev_err(dev, "%s: fail to get dt, ret(%d): set default torch-mode-current-mA(%d mA)\n",
				__func__, ret, temp);
		}
		pdata->led[index].torch_current_mA = temp;

		ret = of_property_read_u32(c_np, "factory-mode-current-mA", &temp);
		if (ret) {
			temp = 320;
			dev_err(dev, "%s: fail to get dt, ret(%d): set default factory-mode-current-mA(%d mA)\n",
				__func__, ret, temp);
		}
		pdata->led[index].factory_current_mA = temp;

		ret = of_property_read_u32(c_np, "used-gpio-control", &temp);
		if (ret) {
			dev_err(dev, "%s: fail to get dt:used-gpio-control\n", __func__);
			return ret;
		}
		pdata->led[index].used_gpio = (bool)(temp & 0x1);

		ret = of_property_read_u32_array(c_np, "torch_table", pdata->led[index].torch_table, TORCH_STEP);
		if (ret) {
			pr_info("%s : set a default torch_table\n", __func__);
			pdata->led[index].torch_table[0] = 20;
			pdata->led[index].torch_table[1] = 40;
			pdata->led[index].torch_table[2] = 60;
			pdata->led[index].torch_table[3] = 90;
			pdata->led[index].torch_table[4] = 120;
		}

		if (pdata->led[index].used_gpio) {
			ret = of_get_named_gpio(c_np, "flash-en-gpio", 0);
			if (ret < 0) {
				dev_err(dev, "%s: fail to get dt:flash-en-gpio (ret=%d)\n", __func__, ret);
				return ret;
			}
			pdata->led[index].flash_en_pin = ret;

			ret = of_get_named_gpio(c_np, "torch-en-gpio", 0);
			if (ret < 0) {
				dev_err(dev, "%s: fail to get dt:torch-en-gpio (ret=%d)\n", __func__, ret);
				return ret;
			}
			pdata->led[index].torch_en_pin = ret;
		}
	}

	return 0;
}
#endif

static inline struct sm5705_fled_platform_data *_get_sm5705_fled_platform_data(
	struct device *dev, struct sm5705_dev *sm5705)
{
	struct sm5705_fled_platform_data *pdata;
	int i, ret;

#ifdef CONFIG_OF
	pdata = devm_kzalloc(dev, sizeof(struct sm5705_fled_platform_data), GFP_KERNEL);
	if (unlikely(!pdata)) {
		dev_err(dev, "%s: fail to allocate memory for sm5705_fled_platform_data\n",
			__func__);
		goto out_p;
	}

	ret = sm5705_fled_parse_dt(dev, pdata);
	if (IS_ERR_VALUE(ret)) {
		dev_err(dev, "%s: fail to parse dt for sm5705 flash-led (ret=%d)\n", __func__, ret);
		goto out_kfree_p;
	}
#else
	pdata = sm5705->pdata->fled_platform_data;
	if (unlikely(!pdata)) {
		dev_err(dev, "%s: fail to get sm5705_fled_platform_data\n", __func__);
		goto out_p;
	}
#endif

	dev_info(dev, "sm5705 flash-LED device platform data info, \n");
	for (i=0; i < SM5705_FLED_MAX; ++i) {
		dev_info(dev, "[FLED-%d] PreFlash: %dmA, Flash: %dmA, Movie: %dmA, \
			Torch: %dmA, Factory: %dmA, used_gpio=%d, GPIO_PIN(%d, %d)\n", i,
			pdata->led[i].preflash_current_mA, pdata->led[i].flash_current_mA,
			pdata->led[i].movie_current_mA, pdata->led[i].torch_current_mA,
			pdata->led[i].factory_current_mA, pdata->led[i].used_gpio,
			pdata->led[i].flash_en_pin, pdata->led[i].torch_en_pin);
	}

	return pdata;

out_kfree_p:
	devm_kfree(dev, pdata);
out_p:
	return NULL;
}

static int sm5705_fled_probe(struct platform_device *pdev)
{
	struct sm5705_dev *sm5705 = dev_get_drvdata(pdev->dev.parent);
	struct sm5705_fled_info *sm5705_fled;
	struct sm5705_fled_platform_data *sm5705_fled_pdata;
	struct device *dev = &pdev->dev;
	int i = 0, ret = 0;

	if (IS_ERR_OR_NULL(camera_class)) {
		dev_err(dev, "%s: can't find camera_class sysfs object, didn't used rear_flash attribute\n",
			__func__);
		return -ENOENT;
	}

	sm5705_fled = devm_kzalloc(dev, sizeof(struct sm5705_fled_info), GFP_KERNEL);
	if (unlikely(!sm5705_fled)) {
		dev_err(dev, "%s: fail to allocate memory for sm5705_fled_info\n", __func__);
		return -ENOMEM;
	}

	dev_info(dev, "SM5705(rev.%d) Flash-LED devic driver Probing..\n",
		__get_revision_number());

	sm5705_fled_pdata = _get_sm5705_fled_platform_data(dev, sm5705);
	if (unlikely(!sm5705_fled_pdata)) {
		dev_info(dev, "%s: fail to get platform data\n", __func__);
		goto fled_platfrom_data_err;
	}

	sm5705_fled->dev = dev;
	sm5705_fled->i2c = sm5705->i2c;
	sm5705_fled->pdata = sm5705_fled_pdata;
	platform_set_drvdata(pdev, sm5705_fled);
	g_sm5705_fled = sm5705_fled;

	ret = sm5705_fled_initialize(sm5705_fled);
	if (IS_ERR_VALUE(ret)) {
		dev_err(dev, "%s: fail to initialize SM5705 Flash-LED[%d] (ret=%d)\n", __func__, i, ret);
		goto fled_init_err;
	}

	/* create camera_class rear_flash device */
	sm5705_fled->rear_fled_dev = device_create(camera_class, NULL, 3, NULL, "flash");
	if (IS_ERR(sm5705_fled->rear_fled_dev)) {
		dev_err(dev, "%s fail to create device for rear_flash\n", __func__);
		goto fled_deinit_err;
	}
	sm5705_fled->rear_fled_dev->parent = dev;
	mutex_init(&sm5705_fled->led_lock);

	ret = device_create_file(sm5705_fled->rear_fled_dev, &dev_attr_rear_flash);
	if (IS_ERR_VALUE(ret)) {
		dev_err(dev, "%s fail to create device file for rear_flash\n", __func__);
		goto fled_rear_device_err;
	}

	ret = device_create_file(sm5705_fled->rear_fled_dev, &dev_attr_rear_torch_flash);
	if (IS_ERR_VALUE(ret)) {
		dev_err(dev, "%s fail to create device file for rear_torch_flash\n", __func__);
		goto fled_rear_device_err;
	}

	sm5705_fled_pdata->fled_pinctrl = devm_pinctrl_get(dev->parent);
	if (IS_ERR_OR_NULL(sm5705_fled_pdata->fled_pinctrl)) {
		pr_err("%s:%d Getting pinctrl handle failed\n",
				__func__, __LINE__);
		goto fled_rear_device_err;
	}

	dev_info(dev, "%s: Probe done.\n", __func__);

	return 0;

fled_rear_device_err:
	device_destroy(camera_class, sm5705_fled->rear_fled_dev->devt);

fled_deinit_err:
	sm5705_fled_deinitialize(sm5705_fled);

fled_init_err:
	platform_set_drvdata(pdev, NULL);
#ifdef CONFIG_OF
	devm_kfree(dev, sm5705_fled_pdata);
#endif

fled_platfrom_data_err:
	devm_kfree(dev, sm5705_fled);

	return ret;
}

static int sm5705_fled_remove(struct platform_device *pdev)
{
	struct sm5705_fled_info *sm5705_fled = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	int i;

	device_remove_file(sm5705_fled->rear_fled_dev, &dev_attr_rear_flash);
	device_remove_file(sm5705_fled->rear_fled_dev, &dev_attr_rear_torch_flash);

	device_destroy(camera_class, sm5705_fled->rear_fled_dev->devt);

	for (i = 0; i != SM5705_FLED_MAX; ++i) {
		sm5705_fled_turn_off(sm5705_fled, i);
	}
	sm5705_fled_deinitialize(sm5705_fled);

	platform_set_drvdata(pdev, NULL);
	mutex_destroy(&sm5705_fled->led_lock);
#ifdef CONFIG_OF
	devm_kfree(dev, sm5705_fled->pdata);
#endif
	devm_kfree(dev, sm5705_fled);

	return 0;
}

static void sm5705_fled_shutdown(struct device *dev)
{
	struct sm5705_fled_info *sm5705_fled = dev_get_drvdata(dev);
	int i;

	for (i=0; i < SM5705_FLED_MAX; ++i) {
		sm5705_fled_turn_off(sm5705_fled, i);
	}
}

#ifdef CONFIG_OF
static struct of_device_id sm5705_fled_match_table[] = {
	{ .compatible = "siliconmitus,sm5705-fled",},
	{},
};
#else
#define sm5705_fled_match_table NULL
#endif

static struct platform_driver sm5705_fled_driver = {
	.probe		= sm5705_fled_probe,
	.remove		= sm5705_fled_remove,
	.driver		= {
		.name	= "sm5705-fled",
		.owner	= THIS_MODULE,
		.shutdown = sm5705_fled_shutdown,
		.of_match_table = sm5705_fled_match_table,
	},
};

static int __init sm5705_fled_init(void)
{
	printk("%s\n",__func__);
	return platform_driver_register(&sm5705_fled_driver);
}
module_init(sm5705_fled_init);

static void __exit sm5705_fled_exit(void)
{
	platform_driver_unregister(&sm5705_fled_driver);
}
module_exit(sm5705_fled_exit);

MODULE_DESCRIPTION("SM5705 FLASH-LED driver");
MODULE_ALIAS("platform:sm5705-flashLED");
MODULE_LICENSE("GPL");

