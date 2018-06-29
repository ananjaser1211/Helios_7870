/*
 * muic_afc.c
 *
 * Copyright (C) 2014 Samsung Electronics
 * Jeongrae Kim <jryu.kim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/module.h>

#include <linux/muic/muic.h>
#include <linux/muic/muic_afc.h>
#include "muic-internal.h"
#include "muic_regmap.h"
#include "muic_i2c.h"
#if defined(CONFIG_MUIC_NOTIFIER)
#include <linux/muic/muic_notifier.h>
#endif /* CONFIG_MUIC_NOTIFIER */

/* Bit 0 : VBUS_VAILD, Bit 1~7 : Reserved */
#define	REG_RSVDID1	0x15

#define	REG_AFCTXD	0x19
#define	REG_AFCSTAT	0x1a
#define	REG_VBUSSTAT	0x1b

enum afc_return {
	AFC_WORK_FAIL = 0,
	AFC_WORK_SUCCESS = 1,
	AFC_WORK_NO_CHANGE = 1,
};

muic_data_t *gpmuic;

static int muic_is_afc_voltage(void);
static int muic_dpreset_afc(void);
static int muic_restart_afc(void);

/* To make AFC work properly on boot */
static int is_charger_ready;
static struct work_struct muic_afc_init_work;

static int muic_drop_afc_voltage(int state)
{
	struct afc_ops *afcops = gpmuic->regmapdesc->afcops;
	int ret = AFC_WORK_FAIL;
	int retry = 0, val = 0;

	pr_info("%s start. state = %d\n", __func__, state);

	if (gpmuic->is_afc_5v == state) {
		pr_info("%s AFC state is already %d, skip\n", __func__, state);
		ret = AFC_WORK_NO_CHANGE;
		return ret;
	}

	if (state) {
		/* Flash on state */
		if (muic_is_afc_voltage() && gpmuic->is_afc_device) {
			val = muic_dpreset_afc();
			if (val < 0) {
				pr_err("%s:failed to AFC reset(%d)\n",
						__func__, val);
			}
			msleep(60); // 60ms delay

			afcops->afc_ctrl_reg(gpmuic->regmapdesc, AFCCTRL_VBUS_READ, 1);
			afcops->afc_ctrl_reg(gpmuic->regmapdesc, AFCCTRL_VBUS_READ, 0);
			for (retry = 0; retry <20; retry++) {
				mdelay(20);
				val = muic_is_afc_voltage();
				if (!val) {
					pr_info("%s:AFC Reset Success(%d)\n",
							__func__, val);
					gpmuic->is_afc_5v = 1;
					ret = AFC_WORK_SUCCESS;
					return ret;
				} else {
					pr_info("%s:AFC Reset Failed(%d)\n",
							__func__, val);
					gpmuic->is_afc_5v = -1;
				}
			}
		} else {
			pr_info("%s:Not connected AFC\n",__func__);
			gpmuic->is_afc_5v = 1;
			ret = AFC_WORK_NO_CHANGE;
		}
	} else {
		/* Flash off state */
		if ((gpmuic->attached_dev == ATTACHED_DEV_AFC_CHARGER_5V_MUIC) ||
				((gpmuic->is_afc_device) && (gpmuic->attached_dev != ATTACHED_DEV_AFC_CHARGER_9V_MUIC)))
			muic_restart_afc();
		gpmuic->is_afc_5v = 0;
		ret = AFC_WORK_SUCCESS;
	}

	pr_info("%s end.\n", __func__);

	return ret;
}

/*
 * muic_check_afc_state - check afc state for camera and charger
 * @state
 *   1: afc off for camera
 *   0: afc on for camera
 *   5: afc off for charger
 *   9: afc on for charger
 */
int muic_check_afc_state(int state)
{
	int ret = AFC_WORK_FAIL;

	mutex_lock(&gpmuic->lock);
	pr_info("%s: state(%d) lcd(%d) camera(%d)\n", __func__, state, 
			gpmuic->check_charger_lcd_on, gpmuic->is_camera_on);

	switch (state) {
		/* for camera */
		case 0:
		case 1:
			if (gpmuic->check_charger_lcd_on)
				ret = AFC_WORK_NO_CHANGE;
			else
				ret = muic_drop_afc_voltage(state);
			
			gpmuic->is_camera_on = state;
			break;
		/* for charger */
		case 5:
			gpmuic->check_charger_lcd_on = true;
			ret = muic_drop_afc_voltage(1);
			break;
		case 9:
			gpmuic->check_charger_lcd_on = false;
			ret = muic_drop_afc_voltage(0);
			break;
		default:
			break;
	}
	mutex_unlock(&gpmuic->lock);

	return ret;
}
EXPORT_SYMBOL(muic_check_afc_state);

int muic_torch_prepare(int state)
{
	return AFC_WORK_NO_CHANGE;
}
EXPORT_SYMBOL(muic_torch_prepare);

static int muic_is_afc_voltage(void)
{
	struct i2c_client *i2c = gpmuic->i2c;
	int vbus_status;
	
	if (gpmuic->attached_dev == ATTACHED_DEV_NONE_MUIC) {
		pr_info("%s attached_dev None \n", __func__);
		return 0;
	}

	vbus_status = muic_i2c_read_byte(i2c, REG_VBUSSTAT);
	vbus_status = (vbus_status & 0x0F);
	pr_info("%s vbus_status (%d)\n", __func__, vbus_status);
	if (vbus_status == 0x00)
		return 0;
	else
		return 1;
}

int muic_dpreset_afc(void)
{
	struct afc_ops *afcops = gpmuic->regmapdesc->afcops;

	pr_info("%s: gpmuic->attached_dev = %d\n", __func__, gpmuic->attached_dev);
	if ( (gpmuic->attached_dev == ATTACHED_DEV_AFC_CHARGER_9V_MUIC) ||
		(gpmuic->attached_dev == ATTACHED_DEV_AFC_CHARGER_PREPARE_MUIC) ||
		(muic_is_afc_voltage()) ) {
		// ENAFC set '0'
		afcops->afc_ctrl_reg(gpmuic->regmapdesc, AFCCTRL_ENAFC, 0);
		msleep(50); // 50ms delay

		// DP_RESET
		pr_info("%s:AFC Disable \n", __func__);
		afcops->afc_ctrl_reg(gpmuic->regmapdesc, AFCCTRL_DIS_AFC, 1);
		msleep(20);
		afcops->afc_ctrl_reg(gpmuic->regmapdesc, AFCCTRL_DIS_AFC, 0);

		gpmuic->attached_dev = ATTACHED_DEV_AFC_CHARGER_5V_MUIC;
		muic_notifier_attach_attached_dev(ATTACHED_DEV_AFC_CHARGER_5V_MUIC);
	}

	return 0;
}

static int muic_restart_afc(void)
{
	struct i2c_client *i2c = gpmuic->i2c;
	int ret, value;
	struct afc_ops *afcops = gpmuic->regmapdesc->afcops;

	pr_info("%s:AFC Restart attached_dev = 0x%x\n", __func__, gpmuic->attached_dev);
	msleep(120); // 120ms delay
	if (gpmuic->attached_dev == ATTACHED_DEV_NONE_MUIC) {
		pr_info("%s:%s Device type is None\n",MUIC_DEV_NAME, __func__);
		return 0;
	}
	gpmuic->attached_dev = ATTACHED_DEV_AFC_CHARGER_PREPARE_MUIC;
	muic_notifier_attach_attached_dev(ATTACHED_DEV_AFC_CHARGER_PREPARE_MUIC);
	cancel_delayed_work(&gpmuic->afc_retry_work);
	schedule_delayed_work(&gpmuic->afc_retry_work, msecs_to_jiffies(5000)); // 5sec

	// voltage(9.0V) + current(1.65A) setting : 0x
	value = 0x46;
	ret = muic_i2c_write_byte(i2c, REG_AFCTXD, value);
	if (ret < 0)
		printk(KERN_ERR "[muic] %s: err write AFC_TXD(%d)\n", __func__, ret);
	pr_info("%s:AFC_TXD [0x%02x]\n", __func__, value);

	// ENAFC set '1'
	afcops->afc_ctrl_reg(gpmuic->regmapdesc, AFCCTRL_ENAFC, 1);

	return 0;
}

static void muic_afc_retry_work(struct work_struct *work)
{
	struct i2c_client *i2c = gpmuic->i2c;
	struct afc_ops *afcops = gpmuic->regmapdesc->afcops;
	int ret, vbus;

	//Reason of AFC fail
	ret = muic_i2c_read_byte(i2c, 0x3C);
	pr_info("%s : Read 0x3C = [0x%02x]\n", __func__, ret);

	// read AFC_STATUS
	ret = muic_i2c_read_byte(i2c, REG_AFCSTAT);
	pr_info("%s: AFC_STATUS [0x%02x]\n", __func__, ret);
	
	pr_info("%s:AFC retry work\n", __func__);
	if (gpmuic->attached_dev == ATTACHED_DEV_AFC_CHARGER_PREPARE_MUIC) {
		vbus = muic_i2c_read_byte(i2c, REG_RSVDID1);
		if (!(vbus & 0x01)) {
			pr_info("%s:%s VBUS is nothing\n",MUIC_DEV_NAME, __func__);
			gpmuic->attached_dev = ATTACHED_DEV_NONE_MUIC;
			muic_notifier_attach_attached_dev(ATTACHED_DEV_NONE_MUIC);
			return;
		}		
		
		pr_info("%s: [MUIC] devtype is afc prepare - Disable AFC\n", __func__);
		afcops->afc_ctrl_reg(gpmuic->regmapdesc, AFCCTRL_DIS_AFC, 1);
		msleep(20);
		afcops->afc_ctrl_reg(gpmuic->regmapdesc, AFCCTRL_DIS_AFC, 0);
	}
}

static void muic_focrced_detection_by_charger(struct work_struct *work)
{
	struct afc_ops *afcops = gpmuic->regmapdesc->afcops;

	pr_info("%s\n", __func__);

	mutex_lock(&gpmuic->muic_mutex);

	afcops->afc_init_check(gpmuic->regmapdesc);

	mutex_unlock(&gpmuic->muic_mutex);
}

void muic_charger_init(void)
{
	pr_info("%s\n", __func__);

	if (!gpmuic) {
		pr_info("%s: MUIC AFC is not ready.\n", __func__);
		return;
	}

	if (is_charger_ready) {
		pr_info("%s: charger is already ready.\n", __func__);
		return;
	}

	is_charger_ready = true;

	if (gpmuic->attached_dev == ATTACHED_DEV_TA_MUIC)
		schedule_work(&muic_afc_init_work);
}

static ssize_t afc_off_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	muic_data_t *pmuic = dev_get_drvdata(dev);
	return snprintf(buf, 4, "%d\n", pmuic->is_afc_5v);
}

static ssize_t afc_off_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t size)
{
	if (!strncmp(buf, "1", 1)) {
		pr_info("%s, Disable AFC\n", __func__);
		muic_check_afc_state(1);
	} else {
		pr_info("%s, Enable AFC\n", __func__);
		muic_check_afc_state(0);
	}

	return size;
}
static DEVICE_ATTR(afc_off, S_IRUGO | S_IWUSR,
		afc_off_show, afc_off_store);
void muic_init_afc_state(muic_data_t *pmuic)
{
	int ret;
	gpmuic = pmuic;
	gpmuic->is_afc_5v = 0;
	gpmuic->check_charger_lcd_on = false;
	gpmuic->is_camera_on = false;
	gpmuic->is_afc_device = 0;

	mutex_init(&gpmuic->lock);
	
	/* To make AFC work properly on boot */
	INIT_WORK(&muic_afc_init_work, muic_focrced_detection_by_charger);
	INIT_DELAYED_WORK(&gpmuic->afc_retry_work, muic_afc_retry_work);

	ret = device_create_file(switch_device, &dev_attr_afc_off);
	if (ret < 0) {
		pr_err("[MUIC] Failed to create file (disable AFC)!\n");
	}

	pr_info("%s:attached_dev = %d\n", __func__, gpmuic->attached_dev);
}

MODULE_DESCRIPTION("MUIC driver");
MODULE_AUTHOR("<jryu.kim@samsung.com>");
MODULE_LICENSE("GPL");
