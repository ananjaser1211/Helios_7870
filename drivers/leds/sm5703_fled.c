/* drivers/leds/sm5703_fled.c
 * SM5703 Flash LED Driver
 *
 * Copyright (C) 2013 Siliconmitus Technology Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/leds/smfled.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/mfd/sm5703.h>
#include <linux/mfd/sm5703_irq.h>
#include <linux/leds/sm5703_fled.h>
#include <linux/delay.h>
#include <linux/power_supply.h>
#include <linux/version.h>
#include <linux/of.h>
#include <linux/battery/charger/sm5703_charger.h>
#include <linux/sec_batt.h>
#ifdef CONFIG_SM5703_MUIC
#include <linux/i2c/sm5703-muic.h>
#endif

#include <linux/of_gpio.h>

#define SM5703_FLED_DEBUG

#define ALIAS_NAME "sm5703-fled"

#define SM5703_FLED_INFO(format, args...) \
	printk(KERN_INFO "%s:%s() line-%d: " format, \
			ALIAS_NAME, __FUNCTION__, __LINE__, ## args)
#define SM5703_FLED_WARN(format, args...) \
	printk(KERN_WARNING "%s:%s() line-%d: " format, \
			ALIAS_NAME, __FUNCTION__, __LINE__, ## args)
#define SM5703_FLED_ERR(format, args...) \
	printk(KERN_ERR "%s:%s() line-%d: " format, \
			ALIAS_NAME, __FUNCTION__, __LINE__, ## args)

#define SM5703_FLED_PIN_CTRL (1<<4)

#define EN_FLED_IRQ 0

extern struct class *camera_class;
struct device *flash_dev;
bool assistive_light = false;
bool recording_mode = false;
struct device *sm5703_dev;
bool flash_config_camera = false;

static struct i2c_client * sm5703_fled_client = NULL;

typedef struct sm5703_fled_info {
	sm_fled_info_t base;
	const sm5703_fled_platform_data_t *pdata;
	sm5703_mfd_chip_t *chip;
	struct mutex led_lock;
	struct i2c_client *i2c_client;
	int movie_current;
	int flash_current;
	int boost;
	int flash_status;
	int ta_exist;
	int chgon_call;
} sm5703_fled_info_t;


static struct platform_device sm_fled_pdev = {
	.name = "sm-flash-led",
	.id = -1,
};

int led_irq_gpio1 = -1;
int led_irq_gpio2 = -1;
bool flash_control_ready = false;

static int sm5703_fled_set_movie_current_sel(struct sm_fled_info *fled_info, int selector);
static int sm5703_fled_set_flash_current_sel(struct sm_fled_info *fled_info, int selector);
static int sm5703_fled_flash(struct sm_fled_info *fled_info, int turn_way);

static int sm5703_fled_set_mode(struct sm_fled_info *fled_info,
		flashlight_mode_t mode);

int32_t sm5703_led_mode_ctrl(int state)
{
	sm_fled_info_t *fled_info = sm_fled_get_info_by_name(NULL);
	sm5703_fled_info_t *info = (sm5703_fled_info_t *)fled_info;

	pr_info("%s : state = %d\n", __func__, state);

	if (assistive_light == true) {
		pr_info("%s : assistive_light is enabled \n", __func__);
		return 0;
	}

	switch(state) {
		case 0:
			if (recording_mode == true) {
				sm5703_fled_set_movie_current_sel(fled_info, info->pdata->fled_torch_current);
				recording_mode = false;
			}
			sm5703_fled_set_mode(fled_info, FLASHLIGHT_MODE_OFF);
			sm5703_fled_notification(fled_info);
			break;
		case 1:
			sm5703_fled_set_mode(fled_info, FLASHLIGHT_MODE_TORCH);
			sm5703_fled_notification(fled_info);
			break;
		case 2:
			sm5703_fled_set_mode(fled_info, FLASHLIGHT_MODE_FLASH);
			sm5703_fled_notification(fled_info);
			break;
		case 3:  /* enable external gpio control */
			sm5703_assign_bits(info->i2c_client, SM5703_FLEDCNTL1,
							SM5703_FLEDEN_MASK, SM5703_FLEDEN_EXTERNAL);
			break;
		case 4: /* disable external gpio control for protecting ESD */
			sm5703_assign_bits(info->i2c_client, SM5703_FLEDCNTL1,
							SM5703_FLEDEN_MASK, SM5703_FLEDEN_DISABLE);
			break;
		case 5:
			recording_mode = true;
			sm5703_fled_set_movie_current_sel(fled_info, info->pdata->fled_movie_current);
			sm5703_fled_set_mode(fled_info, FLASHLIGHT_MODE_TORCH);
			sm5703_fled_notification(fled_info);
			break;
		default :
			sm5703_fled_set_mode(fled_info, FLASHLIGHT_MODE_OFF);
			sm5703_fled_notification(fled_info);
			break;
	}

	return 0;
}
EXPORT_SYMBOL(flash_control_ready);
EXPORT_SYMBOL(sm5703_led_mode_ctrl);

static ssize_t flash_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int sel = 0;
	sm_fled_info_t *fled_info = sm_fled_get_info_by_name(NULL);
	sm5703_fled_info_t *info = (sm5703_fled_info_t *)fled_info;
	int i, nValue=0;
	int torch_current = 0;

	BUG_ON(fled_info == NULL);

	for(i=0; i<count; i++) {
		if(buf[i]<'0' || buf[i]>'9')
			break;
		nValue = nValue*10 + (buf[i]-'0');
	}

	switch(nValue) {
		case 0:
			pr_info("LED Off\n");
			assistive_light = false;
			sm5703_fled_set_mode(fled_info, FLASHLIGHT_MODE_OFF);
			sm5703_fled_flash(fled_info, TURN_WAY_GPIO);
			sm5703_fled_notification(fled_info);

			/* Set torch current */
			sm5703_fled_set_movie_current_sel(fled_info, info->pdata->fled_torch_current);
			break;

		case 99:
			pr_info("Flash On : TEST \n");
			if(sel < 0){
				pr_err("SM5703 fled current set fail \n");
			}

			sm5703_fled_set_mode(fled_info, FLASHLIGHT_MODE_FLASH);
			sm5703_fled_notification(fled_info);
			sm5703_fled_flash(fled_info, TURN_WAY_GPIO);
			break;

		case 100:
			pr_info("Torch On : Factory\n");
			sel = sm5703_fled_set_movie_current_sel(fled_info, SM5703_MOVIE_CURRENT(200)); //200mA
			if(sel < 0){
				pr_err("SM5703 fled current set fail \n");
			}

			sm5703_fled_set_mode(fled_info, FLASHLIGHT_MODE_TORCH);
			sm5703_fled_notification(fled_info);
			sm5703_fled_flash(fled_info, TURN_WAY_GPIO);
			break;

		case 200:
			pr_info("Flash On : Factory\n");
			if(sel < 0){
				pr_err("SM5703 fled current set fail \n");
			}

			/* Flash Current set 300mA for continous flash on mode */
			sm5703_assign_bits(info->i2c_client, SM5703_FLEDCNTL3, SM5703_IFLED_MASK, SM5703_FLASH_CURRENT(300));
			/* ENABSTMR:Disable | ABSTMR:Don't care | FLEDEN:Enable */
			sm5703_reg_write(info->i2c_client, SM5703_FLEDCNTL1,0x03);
			/* nENSAFET:Disable | SAFET:Don't care | nONESHOT:Disable | ONETIMER:Don't care */
			sm5703_reg_write(info->i2c_client, SM5703_FLEDCNTL2,0x90);
			flash_config_camera = false;

			sm5703_fled_set_mode(fled_info, FLASHLIGHT_MODE_FLASH);
			sm5703_fled_notification(fled_info);
			sm5703_fled_flash(fled_info, TURN_WAY_GPIO);
			break;

		case 1:
		case 1001:
		case 1002:
		case 1004:
		case 1006:
		case 1009:
			pr_info("Torch ON\n");
			assistive_light = true;
			if (1001 <= nValue && nValue <= 1010) {
				/* (value) 1001, 1002, 1004, 1006, 1009 */
				if (nValue <= 1001)
					torch_current = 20;
				else if (nValue <= 1002)
					torch_current = 40;
				else if (nValue <= 1004)
					torch_current = 60;
				else if (nValue <= 1006)
					torch_current = 90;
				else if (nValue <= 1009)
					torch_current = 120;
				else
					torch_current = 60;
			} else {
				torch_current = 60;
			}
			sel = sm5703_fled_set_movie_current_sel(fled_info, SM5703_MOVIE_CURRENT(torch_current));
			if(sel < 0){
				pr_err("SM5703 fled current set fail \n");
			}

			sm5703_fled_set_mode(fled_info, FLASHLIGHT_MODE_TORCH);
			sm5703_fled_notification(fled_info);
			sm5703_fled_flash(fled_info, TURN_WAY_GPIO);
			break;

		default:
			pr_err("Torch NC:%d\n", nValue);
			break;
	}

	return count;
}

static DEVICE_ATTR(rear_flash, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH, NULL, flash_store);
static DEVICE_ATTR(rear_torch_flash, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH, NULL, flash_store);

int create_flash_sysfs(void)
{
	int err = -ENODEV;

	if (IS_ERR_OR_NULL(camera_class)) {
		pr_err("flash_sysfs: error, camera class not exist");
		return -ENODEV;
	}

	flash_dev = device_create(camera_class, NULL, 0, NULL, "flash");
	if (IS_ERR(flash_dev)) {
		pr_err("flash_sysfs: failed to create device(flash)\n");
		return -ENODEV;
	}

	err = device_create_file(flash_dev, &dev_attr_rear_flash);
	if (unlikely(err < 0)) {
		pr_err("flash_sysfs: failed to create device file, %s\n",
				dev_attr_rear_flash.attr.name);
	}

	err = device_create_file(flash_dev, &dev_attr_rear_torch_flash);
	if (unlikely(err < 0)) {
		pr_err("flash_sysfs: failed to create device file, %s\n",
				dev_attr_rear_torch_flash.attr.name);
	}
	return 0;
}

#ifdef CONFIG_CHARGER_SM5703
extern int sm5703_chg_fled_init(struct i2c_client *client);
#endif

static int sm5703_fled_init(struct sm_fled_info *fled_info)
{
	sm5703_fled_info_t *info = (sm5703_fled_info_t *)fled_info;
	sm5703_mfd_platform_data_t *mfd_pdata;
	BUG_ON(info == NULL);
	mfd_pdata = info->chip->pdata;
	mutex_lock(&info->led_lock);

	info->base.flashlight_dev->props.strobe_brightness =
		info->pdata->fled_flash_current;
	sm5703_assign_bits(info->i2c_client, SM5703_FLEDCNTL3,
			SM5703_IFLED_MASK, info->pdata->fled_flash_current);

	info->base.flashlight_dev->props.torch_brightness =
		info->pdata->fled_torch_current;
	sm5703_assign_bits(info->i2c_client, SM5703_FLEDCNTL4,
			SM5703_IMLED_MASK, info->pdata->fled_torch_current);

	/* ENABSTMR:Enable | ABSTMR:1.6sec | FLEDEN:Disable */
	sm5703_reg_write(info->i2c_client, SM5703_FLEDCNTL1,0x1C);
	/* nENSAFET:Disable | SAFET:400us | nONESHOT:Disable | ONETIMER:500ms */
	sm5703_reg_write(info->i2c_client, SM5703_FLEDCNTL2,0x94);
	flash_config_camera = true;
	/*
	sm5703_assign_bits(info->i2c_client,SM5703_FLEDCNTL1,
			SM5703_FLEDEN_MASK,SM5703_FLEDEN_EXTERNAL);
	*/
	mutex_unlock(&info->led_lock);
	return 0;
}

static int sm5703_fled_suspend(struct sm_fled_info *info, pm_message_t state)
{
	SM5703_FLED_INFO("Suspend\n");
	return 0;
}

static int sm5703_fled_resume(struct sm_fled_info *info)
{
	SM5703_FLED_INFO("Resume\n");
	return 0;
}

#ifdef CONFIG_FLED_SM5703_EXT_GPIO
/* For GPIO operation, camera driver must use lock / unlock funtion */
void sm5703_fled_flash_critial_section_lock(struct sm_fled_info *fled_info)
{
	sm5703_fled_info_t *info = (sm5703_fled_info_t *)fled_info;
	BUG_ON(info == NULL);
	sm5703_fled_lock(fled_info);
	//sm5703_fled_set_ta_status(info->i2c_client, 0);
}
EXPORT_SYMBOL(sm5703_fled_flash_critial_section_lock);

void sm5703_fled_flash_critial_section_unlock(struct sm_fled_info *fled_info)
{
	sm5703_fled_info_t *info = (sm5703_fled_info_t *)fled_info;
	BUG_ON(info == NULL);
	//sm5703_fled_set_ta_status(info->i2c_client, info->ta_exist);
	sm5703_fled_unlock(fled_info);
}
EXPORT_SYMBOL(sm5703_fled_flash_critial_section_unlock);
#endif /* CONFIG_FLED_SM5703_EXT_GPIO */

int32_t sm5703_charger_notification(struct sm_fled_info *fled_info,
		int32_t on)
{
	union power_supply_propval ac_val, usb_val;
	sm5703_fled_info_t *info = (sm5703_fled_info_t *)fled_info;
	int mode = fled_info->hal->fled_get_mode(fled_info);
	int vbus_valid = sm5703_reg_read(info->i2c_client, SM5703_STATUS5);
	int cntl_val = 0;
	BUG_ON(info == NULL);
	sm5703_fled_lock(fled_info);

	psy_do_property("ac", get,
					POWER_SUPPLY_PROP_ONLINE, ac_val);
	psy_do_property("usb", get,
					POWER_SUPPLY_PROP_ONLINE, usb_val);
	info->ta_exist = (ac_val.intval || usb_val.intval);
	info->chgon_call = on;

	SM5703_FLED_INFO("%s, info->boost = %d, mode = %d, vbus_valid = %d\n",
				__FUNCTION__, info->boost, mode, vbus_valid);
	SM5703_FLED_INFO("%s, info->ta_exist = %d, info->chgon_call = %d, info->flash_status = %d\n",
				__FUNCTION__, info->ta_exist, info->chgon_call, info->flash_status);

	if (info->ta_exist) {
		if (mode == FLASHLIGHT_MODE_TORCH ) {
			sm5703_assign_bits(info->i2c_client, SM5703_FLEDCNTL6,
							SM5703_BSTOUT_MASK, SM5703_BSTOUT_4P5);
			sm5703_assign_bits(info->i2c_client, SM5703_CNTL, SM5703_OPERATION_MODE_MASK,
							SM5703_OPERATION_MODE_CHARGING_ON);
		} else if (mode == FLASHLIGHT_MODE_FLASH ) {
			sm5703_assign_bits(info->i2c_client, SM5703_FLEDCNTL6,
							SM5703_BSTOUT_MASK, SM5703_BSTOUT_4P5);
			sm5703_assign_bits(info->i2c_client, SM5703_CNTL, SM5703_OPERATION_MODE_MASK,
							SM5703_OPERATION_MODE_FLASH_BOOST_MODE);
		} else {
			sm5703_assign_bits(info->i2c_client, SM5703_FLEDCNTL6,
							SM5703_BSTOUT_MASK, SM5703_BSTOUT_4P5);
			sm5703_assign_bits(info->i2c_client, SM5703_CNTL, SM5703_OPERATION_MODE_MASK,
							SM5703_OPERATION_MODE_CHARGING_ON);
		}
	} else {
		if (mode == FLASHLIGHT_MODE_TORCH ) {
			sm5703_assign_bits(info->i2c_client, SM5703_FLEDCNTL6,
							SM5703_BSTOUT_MASK, SM5703_BSTOUT_4P5);
			sm5703_assign_bits(info->i2c_client, SM5703_CNTL, SM5703_OPERATION_MODE_MASK,
							SM5703_OPERATION_MODE_FLASH_BOOST_MODE);
		} else if (mode == FLASHLIGHT_MODE_FLASH ) {
			sm5703_assign_bits(info->i2c_client, SM5703_FLEDCNTL6,
							SM5703_BSTOUT_MASK, SM5703_BSTOUT_4P5);
			sm5703_assign_bits(info->i2c_client, SM5703_CNTL, SM5703_OPERATION_MODE_MASK,
							SM5703_OPERATION_MODE_FLASH_BOOST_MODE);
		} else {
			sm5703_assign_bits(info->i2c_client, SM5703_FLEDCNTL6,
							SM5703_BSTOUT_MASK,SM5703_BSTOUT_4P5);
			sm5703_assign_bits(info->i2c_client, SM5703_CNTL, SM5703_OPERATION_MODE_MASK,
							SM5703_OPERATION_MODE_CHARGING_ON);
		}
	}

	cntl_val = sm5703_reg_read(info->i2c_client, SM5703_CNTL);
	cntl_val &= SM5703_OPERATION_MODE_MASK;
	SM5703_FLED_INFO("%s, OperationMode = %d\n", __FUNCTION__, cntl_val);

	sm5703_fled_unlock(fled_info);

	return 0;
}
EXPORT_SYMBOL(sm5703_charger_notification);

int32_t sm5703_boost_notification(struct sm_fled_info *fled_info, int32_t on)
{
	sm5703_fled_info_t *info = (sm5703_fled_info_t *)fled_info;
	int mode = fled_info->hal->fled_get_mode(fled_info);
	int cntl_val = 0;
	BUG_ON(info == NULL);
	sm5703_fled_lock(fled_info);
	info->boost = on;
#if 0//def CONFIG_FLED_SM5703_EXT_GPIO
	info->ta_exist = on;
	//sm5703_fled_set_ta_status(info->i2c_client, on);
#endif
	SM5703_FLED_INFO("%s, on = %d, info->boost = %d, mode = %d\n",
				__FUNCTION__, on, info->boost, mode);
	SM5703_FLED_INFO("%s, info->ta_exist = %d, info->chgon_call = %d, info->flash_status = %d\n",
				__FUNCTION__, info->ta_exist, info->chgon_call, info->flash_status);

	if (on == 1) {
		if (mode == FLASHLIGHT_MODE_TORCH )
		{
			sm5703_assign_bits(info->i2c_client, SM5703_FLEDCNTL6,
							SM5703_BSTOUT_MASK, SM5703_BSTOUT_5P1);
			sm5703_assign_bits(info->i2c_client, SM5703_CNTL, SM5703_OPERATION_MODE_MASK,
							SM5703_OPERATION_MODE_USB_OTG_MODE);
		}
		else if (mode == FLASHLIGHT_MODE_FLASH )
		{
			sm5703_assign_bits(info->i2c_client, SM5703_FLEDCNTL6,
							SM5703_BSTOUT_MASK, SM5703_BSTOUT_5P1);
			sm5703_assign_bits(info->i2c_client, SM5703_CNTL, SM5703_OPERATION_MODE_MASK,
							SM5703_OPERATION_MODE_FLASH_BOOST_MODE);
		}
		else
		{
			sm5703_assign_bits(info->i2c_client, SM5703_FLEDCNTL6,
							SM5703_BSTOUT_MASK, SM5703_BSTOUT_5P0);
			sm5703_assign_bits(info->i2c_client, SM5703_CNTL, SM5703_OPERATION_MODE_MASK,
							SM5703_OPERATION_MODE_USB_OTG_MODE);
		}
	} else if(on == 0) {
		if (mode == FLASHLIGHT_MODE_TORCH )
		{
			sm5703_assign_bits(info->i2c_client, SM5703_FLEDCNTL6,
							SM5703_BSTOUT_MASK, SM5703_BSTOUT_4P5);
			sm5703_assign_bits(info->i2c_client, SM5703_CNTL, SM5703_OPERATION_MODE_MASK,
							SM5703_OPERATION_MODE_FLASH_BOOST_MODE);
		}
		else if (mode == FLASHLIGHT_MODE_FLASH )
		{
			sm5703_assign_bits(info->i2c_client, SM5703_FLEDCNTL6,
							SM5703_BSTOUT_MASK, SM5703_BSTOUT_4P5);
			sm5703_assign_bits(info->i2c_client, SM5703_CNTL, SM5703_OPERATION_MODE_MASK,
							SM5703_OPERATION_MODE_FLASH_BOOST_MODE);
		}
		else
		{
			sm5703_assign_bits(info->i2c_client, SM5703_FLEDCNTL6,
							SM5703_BSTOUT_MASK, SM5703_BSTOUT_4P5);
			sm5703_assign_bits(info->i2c_client, SM5703_CNTL, SM5703_OPERATION_MODE_MASK,
							SM5703_OPERATION_MODE_CHARGING_ON);
		}
	}

	cntl_val = sm5703_reg_read(info->i2c_client, SM5703_CNTL);
	cntl_val &= SM5703_OPERATION_MODE_MASK;
	SM5703_FLED_INFO("%s, OperationMode = %d\n", __FUNCTION__, cntl_val);

	sm5703_fled_unlock(fled_info);
	return 0;
}
EXPORT_SYMBOL(sm5703_boost_notification);

int32_t sm5703_fled_notification(struct sm_fled_info *fled_info)
{
	sm5703_fled_info_t *info = (sm5703_fled_info_t *)fled_info;
	int mode = fled_info->hal->fled_get_mode(fled_info);
#ifdef SM5703_FLED_DEBUG
	int vbus_valid = sm5703_reg_read(info->i2c_client, SM5703_STATUS5);
	int cntl_val = 0;
#endif

	BUG_ON(info == NULL);
	sm5703_fled_lock(fled_info);
#ifdef SM5703_FLED_DEBUG
	SM5703_FLED_INFO("%s, info->boost = %d, mode = %d, vbus_valid = %d\n",
				__FUNCTION__, info->boost, mode, vbus_valid);
	SM5703_FLED_INFO("%s, info->ta_exist = %d, info->chgon_call = %d, info->flash_status = %d\n",
				__FUNCTION__, info->ta_exist, info->chgon_call, info->flash_status);
	SM5703_FLED_INFO("%s, set start\n",__FUNCTION__);
#endif
	if (info->ta_exist == 1) {
		if (mode == FLASHLIGHT_MODE_TORCH ) {
			sm5703_assign_bits(info->i2c_client, SM5703_CNTL, SM5703_OPERATION_MODE_MASK,
							SM5703_OPERATION_MODE_CHARGING_ON);
		} else if (mode == FLASHLIGHT_MODE_FLASH ) {
			sm5703_assign_bits(info->i2c_client, SM5703_CNTL, SM5703_OPERATION_MODE_MASK,
							SM5703_OPERATION_MODE_FLASH_BOOST_MODE);
		} else {
			sm5703_assign_bits(info->i2c_client, SM5703_CNTL, SM5703_OPERATION_MODE_MASK,
							SM5703_OPERATION_MODE_CHARGING_ON);
		}
	} else {
		if (info->boost){
			if (mode == FLASHLIGHT_MODE_TORCH ) {
				sm5703_assign_bits(info->i2c_client, SM5703_CNTL, SM5703_OPERATION_MODE_MASK,
								SM5703_OPERATION_MODE_USB_OTG_MODE);
			} else if (mode == FLASHLIGHT_MODE_FLASH ) {
				sm5703_assign_bits(info->i2c_client, SM5703_CNTL, SM5703_OPERATION_MODE_MASK,
								SM5703_OPERATION_MODE_FLASH_BOOST_MODE);
			} else {
				sm5703_assign_bits(info->i2c_client, SM5703_CNTL, SM5703_OPERATION_MODE_MASK,
								SM5703_OPERATION_MODE_USB_OTG_MODE);
			}
		} else {
			if (mode == FLASHLIGHT_MODE_TORCH ) {
				sm5703_assign_bits(info->i2c_client, SM5703_CNTL, SM5703_OPERATION_MODE_MASK,
								SM5703_OPERATION_MODE_FLASH_BOOST_MODE);
			} else if (mode == FLASHLIGHT_MODE_FLASH ) {
				sm5703_assign_bits(info->i2c_client, SM5703_CNTL, SM5703_OPERATION_MODE_MASK,
								SM5703_OPERATION_MODE_FLASH_BOOST_MODE);
			} else {
				sm5703_assign_bits(info->i2c_client, SM5703_CNTL, SM5703_OPERATION_MODE_MASK,
								SM5703_OPERATION_MODE_CHARGING_ON);
			}
		}
	}
	SM5703_FLED_INFO("%s, set done\n",__FUNCTION__);
#ifdef SM5703_FLED_DEBUG
	cntl_val = sm5703_reg_read(info->i2c_client, SM5703_CNTL);
	cntl_val &= SM5703_OPERATION_MODE_MASK;
	SM5703_FLED_INFO("%s, OperationMode = %d\n",__FUNCTION__,cntl_val);
#endif
	sm5703_fled_unlock(fled_info);
	return 0;
}
EXPORT_SYMBOL(sm5703_fled_notification);


static int sm5703_fled_set_mode(struct sm_fled_info *fled_info,
		flashlight_mode_t mode)
{
	sm5703_fled_info_t *info = (sm5703_fled_info_t *)fled_info;
#ifdef SM5703_FLED_DEBUG
	SM5703_FLED_INFO("Start : %s, mode = %d, info->flash_status = %d\n",
				__FUNCTION__, mode, info->flash_status);
	SM5703_FLED_INFO("%s, info->ta_exist = %d\n", __FUNCTION__, info->ta_exist);
#endif

	switch (mode) {
		case FLASHLIGHT_MODE_OFF:
			SM5703_FLED_INFO("FLASHLIGHT_MODE_OFF\n");
			/*sm5703_assign_bits(info->i2c_client, SM5703_FLEDCNTL1,
							SM5703_FLEDEN_MASK, SM5703_FLEDEN_DISABLE); */

			if (flash_config_camera == false) {
				/* Set flash configuration */
				sm5703_fled_set_flash_current_sel(fled_info, info->pdata->fled_flash_current);
				/* ENABSTMR:Enable | ABSTMR:1.6sec | FLEDEN:Disable */
				sm5703_reg_write(info->i2c_client, SM5703_FLEDCNTL1,0x1C);
				/* nENSAFET:Disable | SAFET:400us | nONESHOT:Disable | ONETIMER:500ms */
				sm5703_reg_write(info->i2c_client, SM5703_FLEDCNTL2,0x94);
				flash_config_camera = true;
			}
			break;
		case FLASHLIGHT_MODE_TORCH:
			SM5703_FLED_INFO("FLASHLIGHT_MODE_TORCH\n");
			/*sm5703_assign_bits(info->i2c_client, SM5703_FLEDCNTL1,
							SM5703_FLEDEN_MASK, SM5703_FLEDEN_MOVIE_MODE);*/
			break;
		case FLASHLIGHT_MODE_FLASH:
			SM5703_FLED_INFO("FLASHLIGHT_MODE_FLASH\n");
			/*sm5703_assign_bits(info->i2c_client, SM5703_FLEDCNTL1,
							SM5703_FLEDEN_MASK, SM5703_FLEDEN_FLASH_MODE);*/
			break;
		default:
			SM5703_FLED_ERR("Not FLASH MODE ERROR\n");
			return -EINVAL;
	}

	info->base.flashlight_dev->props.mode = mode;
	return 0;
}

static int sm5703_fled_get_mode(struct sm_fled_info *info)
{
	sm5703_fled_info_t *sm5703_fled_info = (sm5703_fled_info_t *)info;
	return sm5703_fled_info->base.flashlight_dev->props.mode;
}

void sm5703_fled_lock(struct sm_fled_info *fled_info)
{
	sm5703_fled_info_t *info = (sm5703_fled_info_t *)fled_info;
	mutex_lock(&info->led_lock);
}
EXPORT_SYMBOL(sm5703_fled_lock);

void sm5703_fled_unlock(struct sm_fled_info *fled_info)
{
	sm5703_fled_info_t *info = (sm5703_fled_info_t *)fled_info;
	mutex_unlock(&info->led_lock);
}
EXPORT_SYMBOL(sm5703_fled_unlock);

static int sm5703_fled_flash(struct sm_fled_info *fled_info, int turn_way)
{
	int ret = 0;
	sm5703_fled_info_t *info = (sm5703_fled_info_t *)fled_info;

	SM5703_FLED_INFO("Start : E\n");

	SM5703_FLED_INFO("%s, info->boost = %d\n", __FUNCTION__, info->boost);
	SM5703_FLED_INFO("%s, info->ta_exist = %d, info->flash_status = %d\n",
				__FUNCTION__, info->ta_exist, info->flash_status);
	/*
	 if (info->flash_status == 0) {
	// Lock LED until setting to OFF MODE
		sm5703_fled_lock(fled_info);
		info->flash_status = 1;
	}
	 */
	SM5703_FLED_INFO("%s, turn_way = %d, info->base.flashlight_dev->props.mode = %d\n",
				__FUNCTION__, turn_way, info->base.flashlight_dev->props.mode);

	if (turn_way == TURN_WAY_I2C)
	{
		switch (info->base.flashlight_dev->props.mode)
		{
			case FLASHLIGHT_MODE_FLASH:
				sm5703_assign_bits(info->i2c_client, SM5703_FLEDCNTL1,
								SM5703_FLEDEN_MASK, SM5703_FLEDEN_FLASH_MODE);
				break;
			case FLASHLIGHT_MODE_TORCH:
				sm5703_assign_bits(info->i2c_client, SM5703_FLEDCNTL1,
								SM5703_FLEDEN_MASK, SM5703_FLEDEN_MOVIE_MODE);
				break;
			case FLASHLIGHT_MODE_OFF:
				sm5703_assign_bits(info->i2c_client, SM5703_FLEDCNTL1,
								SM5703_FLEDEN_MASK, SM5703_FLEDEN_DISABLE);
				break;
			default:
				sm5703_assign_bits(info->i2c_client, SM5703_FLEDCNTL1,
								SM5703_FLEDEN_MASK, SM5703_FLEDEN_DISABLE);
				SM5703_FLED_ERR("Error : not flash / mode\n");
				ret = -EINVAL;
		}
	}
	else if (turn_way == TURN_WAY_GPIO)
	{
		switch (info->base.flashlight_dev->props.mode) {
			case FLASHLIGHT_MODE_FLASH:
				sm5703_assign_bits(info->i2c_client, SM5703_FLEDCNTL1,
								SM5703_FLEDEN_MASK, SM5703_FLEDEN_EXTERNAL);
				flash_control_ready = true;
				gpio_request(led_irq_gpio1, NULL);
				gpio_request(led_irq_gpio2, NULL);
				gpio_direction_output(led_irq_gpio1, 1);
				gpio_direction_output(led_irq_gpio2, 0);
				gpio_free(led_irq_gpio1);
				gpio_free(led_irq_gpio2);
				break;
			case FLASHLIGHT_MODE_TORCH:
				sm5703_assign_bits(info->i2c_client, SM5703_FLEDCNTL1,
								SM5703_FLEDEN_MASK, SM5703_FLEDEN_EXTERNAL);
				flash_control_ready = true;
				gpio_request(led_irq_gpio1, NULL);
				gpio_request(led_irq_gpio2, NULL);
				gpio_direction_output(led_irq_gpio1, 0);
				gpio_direction_output(led_irq_gpio2, 1);
				gpio_free(led_irq_gpio1);
				gpio_free(led_irq_gpio2);
				break;
			case FLASHLIGHT_MODE_OFF:
				gpio_request(led_irq_gpio1, NULL);
				gpio_request(led_irq_gpio2, NULL);
				gpio_direction_output(led_irq_gpio1, 0);
				gpio_direction_output(led_irq_gpio2, 0);
				gpio_free(led_irq_gpio1);
				gpio_free(led_irq_gpio2);
				sm5703_assign_bits(info->i2c_client, SM5703_FLEDCNTL1,
								SM5703_FLEDEN_MASK, SM5703_FLEDEN_DISABLE);
				flash_control_ready = false;
				break;
			default:
				gpio_request(led_irq_gpio1, NULL);
				gpio_request(led_irq_gpio2, NULL);
				gpio_direction_output(led_irq_gpio1, 0);
				gpio_direction_output(led_irq_gpio2, 0);
				gpio_free(led_irq_gpio1);
				gpio_free(led_irq_gpio2);
				sm5703_assign_bits(info->i2c_client, SM5703_FLEDCNTL1,
								SM5703_FLEDEN_MASK, SM5703_FLEDEN_DISABLE);
				flash_control_ready = false;
				SM5703_FLED_ERR("Error : not flash / mode\n");
				ret = -EINVAL;
		}
	}
	else
	{
		SM5703_FLED_ERR("Error : not flash / mode\n");
		ret = -EINVAL;
	}

	return ret;
}

static int flash_current[] = {
	300000, 325000, 350000, 375000, 400000, 425000, 450000, 475000,
	500000, 525000, 550000, 575000, 600000, 625000, 650000, 700000,
	750000, 800000, 850000, 900000, 950000, 1000000, 1050000, 1100000,
	1150000, 1200000, 1250000, 1300000, 1350000, 1400000, 1450000, 1500000
};


/* Return value : -EINVAL => selector parameter is out of range, otherwise current in mA*/
static int sm5703_fled_movie_current_list(struct sm_fled_info *info,
		int selector)
{
	if (selector < 0 || selector > 0x1f )
		return -EINVAL;
	return ((10 + selector * 10) * 1000);
}


static int sm5703_fled_flash_current_list(struct sm_fled_info *info,
		int selector)
{
	if (selector < 0 || selector > 0x1f )
		return -EINVAL;
	return flash_current[selector];
}

static struct flashlight_properties sm5703_fled_props = {
	.type = FLASHLIGHT_TYPE_LED,
	.torch_brightness = 0,
	.torch_max_brightness = 0x1f,
	.strobe_brightness = 0,
	.strobe_max_brightness = 0x1f,
	.strobe_delay = 2,
	//.flash_timeout = 64,
	.alias_name = "sm5703-fled",
};


static int sm5703_fled_set_movie_current_sel(struct sm_fled_info *fled_info,
		int selector)
{
	int rc;
	sm5703_fled_info_t *info = (sm5703_fled_info_t *)fled_info;
	SM5703_FLED_INFO("Set movie current to %d\n", selector);
	if (selector < 0 || selector >  info->
			base.flashlight_dev->props.torch_max_brightness)
		return -EINVAL;
	rc = sm5703_assign_bits(info->i2c_client, SM5703_FLEDCNTL4,
			SM5703_IMLED_MASK, selector);
	if (rc == 0)
		info->base.flashlight_dev->props.torch_brightness = selector;
	return rc;
}
static int sm5703_fled_set_flash_current_sel(struct sm_fled_info *fled_info,
		int selector)
{
	int rc;
	sm5703_fled_info_t *info = (sm5703_fled_info_t *)fled_info;
	SM5703_FLED_INFO("Set flash current to %d\n", selector);
	if (selector < 0 || selector >  info->
			base.flashlight_dev->props.strobe_max_brightness)
		return -EINVAL;
	rc = sm5703_assign_bits(info->i2c_client, SM5703_FLEDCNTL3,
			SM5703_IFLED_MASK, selector);
	if (rc == 0)
		info->base.flashlight_dev->props.strobe_brightness = selector;
	return 0;
}

static int sm5703_fled_get_movie_current_sel(struct sm_fled_info *fled_info)
{
	int rc;
	sm5703_fled_info_t *info = (sm5703_fled_info_t *)fled_info;
	rc = sm5703_reg_read(info->i2c_client, SM5703_FLEDCNTL4);
	if (rc < 0)
		return rc;
	return (rc & SM5703_IMLED_MASK);
}

static int sm5703_fled_get_flash_current_sel(struct sm_fled_info *fled_info)
{
	int rc;
	sm5703_fled_info_t *info = (sm5703_fled_info_t *)fled_info;
	rc = sm5703_reg_read(info->i2c_client, SM5703_FLEDCNTL3);
	if (rc < 0)
		return rc;
	return (rc & SM5703_IFLED_MASK);
}

static void sm5703_fled_shutdown(struct sm_fled_info *info)
{
	flashlight_set_mode(info->flashlight_dev, FLASHLIGHT_MODE_OFF);
	return;
}

static struct sm_fled_hal sm5703_fled_hal = {
	.fled_init = sm5703_fled_init,
	.fled_suspend = sm5703_fled_suspend,
	.fled_resume = sm5703_fled_resume,
	.fled_set_mode = sm5703_fled_set_mode,
	.fled_get_mode = sm5703_fled_get_mode,
	.fled_strobe = sm5703_fled_flash,
	.fled_movie_current_list = sm5703_fled_movie_current_list,
	.fled_flash_current_list = sm5703_fled_flash_current_list,
	/* method to set */
	.fled_set_movie_current_sel = sm5703_fled_set_movie_current_sel,
	.fled_set_flash_current_sel = sm5703_fled_set_flash_current_sel,

	/* method to get */
	.fled_get_movie_current_sel = sm5703_fled_get_movie_current_sel,
	.fled_get_flash_current_sel = sm5703_fled_get_flash_current_sel,
	/* PM shutdown, optional */
	.fled_shutdown = sm5703_fled_shutdown,

};

//Flash current
static sm5703_fled_platform_data_t sm5703_default_fled_pdata = {
	.fled_flash_current = SM5703_FLASH_CURRENT(1000),
	.fled_movie_current = SM5703_MOVIE_CURRENT(150),
	.fled_torch_current = SM5703_MOVIE_CURRENT(100),
};

#define FLAG_HIGH			(0x01)
#define FLAG_LOW			(0x02)
#define FLAG_LOW_TO_HIGH	(0x04)
#define FLAG_HIGH_TO_LOW	(0x08)
#define FLAG_CHANGED		(FLAG_LOW_TO_HIGH|FLAG_HIGH_TO_LOW)

#ifdef CONFIG_OF
static int sm5703_fled_parse_dt(struct device *dev,
		struct sm5703_fled_platform_data *pdata)
{
	struct device_node *np = dev->of_node;
	u32 buffer[2];

	/* copy default value */
	*pdata = sm5703_default_fled_pdata;

	if (of_property_read_u32_array(np, "flash_current", buffer, 1) == 0) {
		dev_info(dev, "flash_current = <%d>\n", buffer[0]);
		pdata->fled_flash_current = SM5703_FLASH_CURRENT(buffer[0]);
	}

	if (of_property_read_u32_array(np, "movie_current", buffer, 1) == 0) {
		dev_info(dev, "movie_current = <%d>\n", buffer[0]);
		pdata->fled_movie_current = SM5703_MOVIE_CURRENT(buffer[0]);
	}

	if (of_property_read_u32_array(np, "torch_current", buffer, 1) == 0) {
		dev_info(dev, "torch_current = <%d>\n", buffer[0]);
		pdata->fled_torch_current = SM5703_MOVIE_CURRENT(buffer[0]);
	}

	return 0;
}

static struct of_device_id sm5703_fled_match_table[] = {
	{ .compatible = "siliconmitus,sm5703-fled",},
	{},
};
#else
static int sm5703_fled_parse_dt(struct device *dev,
		struct sm5703_fled_platform_data *pdata)
{
	return 0;
}
#define sm5703_fled_match_table NULL
#endif

static int sm5703_fled_probe(struct platform_device *pdev)
{
	int ret;
	struct sm5703_mfd_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct sm5703_mfd_platform_data *mfd_pdata = chip->dev->platform_data;
	struct sm5703_fled_platform_data *pdata;
	sm5703_fled_info_t *fled_info;
	SM5703_FLED_INFO("Siliconmitus SM5703 FlashLED driver probing...\n");
#ifdef CONFIG_OF
	if (pdev->dev.parent->of_node) {
		pdev->dev.of_node = of_find_compatible_node(
				of_node_get(pdev->dev.parent->of_node), NULL,
				sm5703_fled_match_table[0].compatible);
	}
#endif
	if (pdev->dev.of_node) {
		pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata) {
			dev_err(&pdev->dev, "Failed to allocate memory\n");
			ret = -ENOMEM;
			goto err_parse_dt_nomem;
		}
		ret = sm5703_fled_parse_dt(&pdev->dev, pdata);
		if (ret < 0)
			goto err_parse_dt;
	} else {
		BUG_ON(mfd_pdata == NULL);
		if (mfd_pdata->fled_platform_data)
			pdata = mfd_pdata->fled_platform_data;
		else
			pdata = &sm5703_default_fled_pdata;
	}
	fled_info = kzalloc(sizeof(*fled_info), GFP_KERNEL);
	if (!fled_info) {
		ret = -ENOMEM;
		goto err_fled_nomem;
	}
	mutex_init(&fled_info->led_lock);
	fled_info->i2c_client = chip->i2c_client;
	fled_info->base.init_props = &sm5703_fled_props;
	fled_info->base.hal = &sm5703_fled_hal;
	fled_info->pdata = pdata;
	fled_info->chip = chip;
	chip->fled_info = fled_info;
	platform_set_drvdata(pdev, fled_info);

	sm5703_dev = &pdev->dev;
	sm5703_fled_client = chip->i2c_client;

	sm_fled_pdev.dev.parent = &(pdev->dev);
	ret = platform_device_register(&sm_fled_pdev);
	if (ret < 0)
		goto err_register_pdev;
#if 0
	ret = register_irq(pdev, fled_info);
	if (ret < 0) {
		SM5703_FLED_ERR("Error : can't register irq\n");
		goto err_register_irq;

	}
#endif

	if (pdev->dev.of_node) {
		led_irq_gpio1 = of_get_named_gpio(pdev->dev.of_node, "sm5703,led1-gpio", 0);
		SM5703_FLED_INFO("led1-gpio:%d\n", led_irq_gpio1);
		if (led_irq_gpio1 < 0) {
			pr_err("Fail get led1-gpio\n");
			return -EINVAL;
		}

		led_irq_gpio2 = of_get_named_gpio(pdev->dev.of_node, "sm5703,led2-gpio", 0);
		SM5703_FLED_INFO("led2-gpio:%d\n", led_irq_gpio2);
		if (led_irq_gpio2 < 0) {
			pr_err("Fail get led2-gpio\n");
			return -EINVAL;
		}

		/* DVS : GPIO initialize to output low*/
		gpio_request(led_irq_gpio1, NULL);
		gpio_request(led_irq_gpio2, NULL);
		gpio_direction_output(led_irq_gpio1, 0);
		gpio_direction_output(led_irq_gpio2, 0);
		gpio_free(led_irq_gpio1);
		gpio_free(led_irq_gpio2);
	}
	/* Create Samsung Flash Sysfs */
	create_flash_sysfs();

	pdata->fled_pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR_OR_NULL(pdata->fled_pinctrl)) {
		pr_err("%s:%d Getting pinctrl handle failed\n",
				__func__, __LINE__);
		return -EINVAL;
	}

	SM5703_FLED_INFO("End : X\n");

	return 0;
err_register_pdev:
	kfree(fled_info);
err_fled_nomem:
err_parse_dt:
err_parse_dt_nomem:
	return ret;
}

static int sm5703_fled_remove(struct platform_device *pdev)
{
	struct sm5703_fled_info *fled_info;
	SM5703_FLED_INFO("Siliconmitus SM5703 FlashLED driver removing...\n");
	fled_info = platform_get_drvdata(pdev);
	platform_device_unregister(&sm_fled_pdev);
	mutex_destroy(&fled_info->led_lock);
	kfree(fled_info);

	if(flash_dev) {
		device_remove_file(flash_dev, &dev_attr_rear_flash);
		device_remove_file(flash_dev, &dev_attr_rear_torch_flash);
	}

	if (camera_class && flash_dev) {
		device_destroy(camera_class, flash_dev->devt);
	}

	return 0;
}

static struct platform_driver sm5703_fled_driver = {
	.probe	= sm5703_fled_probe,
	.remove	= sm5703_fled_remove,
	.driver	= {
		.name	= "sm5703-fled",
		.owner	= THIS_MODULE,
		.of_match_table = sm5703_fled_match_table,
	},
};


static int __init sm5703_fled_module_init(void)
{
	return platform_driver_register(&sm5703_fled_driver);
}

static void __exit sm5703_fled_module_exit(void)
{
	platform_driver_unregister(&sm5703_fled_driver);
}

device_initcall(sm5703_fled_module_init);
module_exit(sm5703_fled_module_exit);

MODULE_DESCRIPTION("Siliconmitus SM5703 FlashLED Driver");
MODULE_VERSION(SM5703_DRV_VER);
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:sm5703-flashLED");

