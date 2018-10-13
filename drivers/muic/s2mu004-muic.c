/*
 * driver/muic/s2mu004.c - S2MU004 micro USB switch device driver
 *
 * Copyright (C) 2015 Samsung Electronics
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
 * This driver is based on max77843-muic-afc.c
 *
 */

#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/wakelock.h>

#include <linux/muic/muic.h>
#include <linux/mfd/samsung/s2mu004.h>
#include <linux/mfd/samsung/s2mu004-private.h>
#include <linux/muic/s2mu004-muic.h>
#if defined(CONFIG_BATTERY_SAMSUNG_V2)
#include "../battery_v2/include/sec_charging_common.h"
#else
#include <linux/battery/sec_charging_common.h>
#endif
#if defined(CONFIG_CCIC_S2MU004)
#include <linux/ccic/usbpd-s2mu004.h>
#endif

#include <linux/muic/s2mu004-muic-hv.h>
#if defined(CONFIG_HV_MUIC_S2MU004_AFC)
#include <linux/muic/s2mu004-muic-hv-typedef.h>
#endif /* CONFIG_HV_MUIC_S2MU004_AFC */

#if defined(CONFIG_MUIC_NOTIFIER)
#include <linux/muic/muic_notifier.h>
#endif /* CONFIG_MUIC_NOTIFIER */

#if defined(CONFIG_CCIC_NOTIFIER)
#include <linux/ccic/ccic_notifier.h>
#include <linux/usb_notify.h>
#endif /* CONFIG_CCIC_NOTIFIER */

#include "muic-internal.h"
#if defined(CONFIG_MUIC_SUPPORT_CCIC)
#include "muic_ccic.h"
#endif

#if defined(CONFIG_VBUS_NOTIFIER)
#include <linux/vbus_notifier.h>
#endif /* CONFIG_VBUS_NOTIFIER */

#if defined(CONFIG_MUIC_UART_SWITCH)
#include <mach/pinctrl-samsung.h>
#endif

#define GPIO_LEVEL_HIGH		1
#define GPIO_LEVEL_LOW		0

static void s2mu004_dcd_rescan(struct s2mu004_muic_data *muic_data);
static struct s2mu004_muic_data *static_data;
static void s2mu004_muic_handle_attach(struct s2mu004_muic_data *muic_data,
			muic_attached_dev_t new_dev, int adc, u8 vbvolt);
static void s2mu004_muic_handle_detach(struct s2mu004_muic_data *muic_data);
static void s2mu004_muic_detect_dev(struct s2mu004_muic_data *muic_data);
#if defined(CONFIG_CCIC_S2MU004) || !defined(CONFIG_SEC_FACTORY)
static int s2mu004_muic_get_vbus_state(struct s2mu004_muic_data *muic_data);
static void s2mu004_muic_set_water_adc_ldo_wa(struct s2mu004_muic_data *muic_data, bool en);
#endif
#if defined(CONFIG_CCIC_S2MU004)
static int s2mu004_muic_refresh_adc(struct s2mu004_muic_data *muic_data);
static int s2mu004_muic_water_judge(struct s2mu004_muic_data *muic_data);
static int s2mu004_muic_set_rid_adc_en(struct s2mu004_muic_data *muic_data, bool en);
static void s2mu004_muic_set_rid_int_mask_en(struct s2mu004_muic_data *muic_data, bool en);
#endif
#if 0
static int s2mu004_muic_get_adc_by_mode(struct s2mu004_muic_data *muic_data);
#endif
static int s2mu004_i2c_update_bit(struct i2c_client *client,
			u8 reg, u8 mask, u8 shift, u8 value);
static int s2mu004_i2c_read_byte(struct i2c_client *client, u8 command);
static int s2mu004_i2c_write_byte(struct i2c_client *client,
			u8 command, u8 value);


#if defined(DEBUG_MUIC)
#define MAX_LOG 25
#define READ 0
#define WRITE 1

static u8 s2mu004_log_cnt;
static u8 s2mu004_log[MAX_LOG][3];

static void s2mu004_reg_log(u8 reg, u8 value, u8 rw)
{
	s2mu004_log[s2mu004_log_cnt][0] = reg;
	s2mu004_log[s2mu004_log_cnt][1] = value;
	s2mu004_log[s2mu004_log_cnt][2] = rw;
	s2mu004_log_cnt++;
	if (s2mu004_log_cnt >= MAX_LOG)
		s2mu004_log_cnt = 0;
}
static void s2mu004_print_reg_log(void)
{
	int i;
	u8 reg, value, rw;
	char mesg[256] = "";

	for (i = 0; i < MAX_LOG; i++) {
		reg = s2mu004_log[s2mu004_log_cnt][0];
		value = s2mu004_log[s2mu004_log_cnt][1];
		rw = s2mu004_log[s2mu004_log_cnt][2];
		s2mu004_log_cnt++;

		if (s2mu004_log_cnt >= MAX_LOG)
			s2mu004_log_cnt = 0;
		sprintf(mesg+strlen(mesg), "%x(%x)%x ", reg, value, rw);
	}
	pr_info("%s:%s\n", __func__, mesg);
}
void s2mu004_read_reg_dump(struct s2mu004_muic_data *muic, char *mesg)
{
	u8 val;

	s2mu004_read_reg(muic->i2c, S2MU004_REG_MUIC_CTRL1, &val);
	sprintf(mesg+strlen(mesg), "CTRL1:%x ", val);
	s2mu004_read_reg(muic->i2c, S2MU004_REG_MUIC_SW_CTRL, &val);
	sprintf(mesg+strlen(mesg), "SW_CTRL:%x ", val);
	s2mu004_read_reg(muic->i2c, S2MU004_REG_MUIC_INT1_MASK, &val);
	sprintf(mesg+strlen(mesg), "IM1:%x ", val);
	s2mu004_read_reg(muic->i2c, S2MU004_REG_MUIC_INT2_MASK, &val);
	sprintf(mesg+strlen(mesg), "IM2:%x ", val);
	s2mu004_read_reg(muic->i2c, S2MU004_REG_MUIC_CHG_TYPE, &val);
	sprintf(mesg+strlen(mesg), "CHG_T:%x ", val);
	s2mu004_read_reg(muic->i2c, S2MU004_REG_MUIC_DEVICE_APPLE, &val);
	sprintf(mesg+strlen(mesg), "APPLE_DT:%x ", val);
	s2mu004_read_reg(muic->i2c, S2MU004_REG_MUIC_ADC, &val);
	sprintf(mesg+strlen(mesg), "ADC:%x ", val);
	s2mu004_read_reg(muic->i2c, S2MU004_REG_MUIC_DEVICE_TYPE1, &val);
	sprintf(mesg+strlen(mesg), "DT1:%x ", val);
	s2mu004_read_reg(muic->i2c, S2MU004_REG_MUIC_DEVICE_TYPE2, &val);
	sprintf(mesg+strlen(mesg), "DT2:%x ", val);
	s2mu004_read_reg(muic->i2c, S2MU004_REG_MUIC_DEVICE_TYPE3, &val);
	sprintf(mesg+strlen(mesg), "DT3:%x ", val);
}
void s2mu004_print_reg_dump(struct s2mu004_muic_data *muic_data)
{
	char mesg[256] = "";

	s2mu004_read_reg_dump(muic_data, mesg);

	pr_info("%s:%s\n", __func__, mesg);
}
#endif

static int s2mu004_i2c_read_byte(struct i2c_client *client, u8 command)
{
	u8 ret;
	int retry = 0;

	s2mu004_read_reg(client, command, &ret);

	while (ret < 0) {
		pr_info("[muic] %s: reg(0x%x), retrying...\n",
			__func__, command);
		if (retry > 10) {
			pr_err("[muic] %s: retry failed!!\n", __func__);
			break;
		}
		msleep(100);
		s2mu004_read_reg(client, command, &ret);
		retry++;
	}

#ifdef DEBUG_MUIC
	s2mu004_reg_log(command, ret, retry << 1 | READ);
#endif
	return ret;
}
static int s2mu004_i2c_write_byte(struct i2c_client *client,
			u8 command, u8 value)
{
	int ret;
	int retry = 0;
	u8 written = 0;

	ret = s2mu004_write_reg(client, command, value);

	while (ret < 0) {
		pr_info("[muic] %s: reg(0x%x), retrying...\n",
			__func__, command);
		s2mu004_read_reg(client, command, &written);
		if (written < 0)
			pr_err("[muic] %s: reg(0x%x)\n",
				__func__, command);
		msleep(100);
		ret = s2mu004_write_reg(client, command, value);
		retry++;
	}
#ifdef DEBUG_MUIC
	s2mu004_reg_log(command, value, retry << 1 | WRITE);
#endif
	return ret;
}
static int s2mu004_i2c_guaranteed_wbyte(struct i2c_client *client,
			u8 command, u8 value)
{
	int ret;
	int retry = 0;
	int written;

	ret = s2mu004_i2c_write_byte(client, command, value);
	written = s2mu004_i2c_read_byte(client, command);

	while (written != value) {
		pr_info("[muic] reg(0x%x): written(0x%x) != value(0x%x)\n",
			command, written, value);
		if (retry > 10) {
			pr_err("[muic] %s: retry failed!!\n", __func__);
			break;
		}
		msleep(100);
		retry++;
		ret = s2mu004_i2c_write_byte(client, command, value);
		written = s2mu004_i2c_read_byte(client, command);
	}
	return ret;
}

static int s2mu004_i2c_update_bit(struct i2c_client *i2c,
			u8 reg, u8 mask, u8 shift, u8 value)
{
	int ret;
	u8 reg_val = 0;

	reg_val = s2mu004_i2c_read_byte(i2c, reg);
	reg_val &= ~mask;
	reg_val |= value << shift;
	ret = s2mu004_i2c_write_byte(i2c, reg, reg_val);
	pr_info("[update_bit:%s] reg(0x%x):  value(0x%x)\n", __func__, reg, reg_val);
	if (ret < 0) {
		pr_err("%s: Reg = 0x%X, mask = 0x%X, val = 0x%X write err : %d\n",
				__func__, reg, mask, value, ret);
	}

	return ret;
}

#if defined(CONFIG_CCIC_S2MU004)
void s2mu004_usbpd_set_muic_type(int type)
{
	int data;
	struct i2c_client *i2c = static_data->i2c;
	int vbvolt = 0;
	int adc = 0;

	mutex_lock(&static_data->muic_mutex);

	vbvolt = s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_DEVICE_APPLE);
	vbvolt = !!(vbvolt & DEV_TYPE_APPLE_VBUS_WAKEUP);
	adc = s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_ADC);

	if ((static_data->attach_mode != S2MU004_NONE_CABLE)
			|| vbvolt) {
		pr_info("%s type : (%d)\n", __func__, type);

		data = s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_RID_CTRL);
		data |= (0x01 << 1);
		s2mu004_i2c_write_byte(i2c, S2MU004_REG_MUIC_RID_CTRL, data);

		/* start secondary dp dm detect */
		data = s2mu004_i2c_read_byte(i2c, 0x6a);
		data |= 0x01;
		s2mu004_i2c_write_byte(i2c, 0x6a, data);

		static_data->attach_mode = S2MU004_SECOND_ATTACH;
#if 0
		/* wait for muic device detect */
		msleep(150);

		/* if there is no reason for irq occur
		force to call detect_dev func */
		if (((adc & 0x1f) == 0x1f) && vbvolt)
			s2mu004_muic_detect_dev(static_data);
#endif
	}

	mutex_unlock(&static_data->muic_mutex);
}

int s2mu004_usbpd_get_adc(void)
{
	int adc;
	int vbus = 0;
	adc = s2mu004_i2c_read_byte(static_data->i2c, S2MU004_REG_MUIC_ADC);
	adc &= 0x1f;

	if (adc == 0)
		return adc;
	else {
		vbus = s2mu004_i2c_read_byte(static_data->i2c, S2MU004_REG_MUIC_DEVICE_APPLE);
		vbus = !!(vbus & DEV_TYPE_APPLE_VBUS_WAKEUP);
		return !vbus;
	}
}
#endif

#if defined(GPIO_USB_SEL)
static int s2mu004_set_gpio_usb_sel(int uart_sel)
{
	return 0;
}
#endif /* GPIO_USB_SEL */

static int s2mu004_set_gpio_uart_sel(int uart_sel)
{
	const char *mode;
#if !defined(CONFIG_MUIC_UART_SWITCH)
	int uart_sel_gpio = muic_pdata.gpio_uart_sel;
	int uart_sel_val;
	int ret;

	ret = gpio_request(uart_sel_gpio, "GPIO_UART_SEL");
	if (ret) {
		pr_err("[muic] failed to gpio_request GPIO_UART_SEL\n");
		return ret;
	}

	uart_sel_val = gpio_get_value(uart_sel_gpio);

	switch (uart_sel) {
	case MUIC_PATH_UART_AP:
		mode = "AP_UART";
		if (gpio_is_valid(uart_sel_gpio))
			gpio_direction_output(uart_sel_gpio, 1);
		break;
	case MUIC_PATH_UART_CP:
		mode = "CP_UART";
		if (gpio_is_valid(uart_sel_gpio))
			gpio_direction_output(uart_sel_gpio, 0);
		break;
	default:
		mode = "Error";
		break;
	}

	uart_sel_val = gpio_get_value(uart_sel_gpio);

	gpio_free(uart_sel_gpio);

	pr_info("[muic] %s, GPIO_UART_SEL(%d)=%c\n",
		mode, uart_sel_gpio, (uart_sel_val == 0 ? 'L' : 'H'));
#else
	switch (uart_sel) {
	case MUIC_PATH_UART_AP:
		mode = "AP_UART";
		pin_config_set(muic_pdata.uart_addr, muic_pdata.uart_rxd,
					PINCFG_PACK(PINCFG_TYPE_FUNC, 0x2));
		pin_config_set(muic_pdata.uart_addr, muic_pdata.uart_txd,
					PINCFG_PACK(PINCFG_TYPE_FUNC, 0x2));
		break;
	case MUIC_PATH_UART_CP:
		mode = "CP_UART";
		pin_config_set(muic_pdata.uart_addr, muic_pdata.uart_rxd,
					PINCFG_PACK(PINCFG_TYPE_FUNC, 0x3));
		pin_config_set(muic_pdata.uart_addr, muic_pdata.uart_txd,
					PINCFG_PACK(PINCFG_TYPE_FUNC, 0x3));
		break;
	default:
		mode = "Error";
		break;
	}

	printk(KERN_DEBUG "[muic] %s %s\n", __func__, mode);
#endif
	return 0;
}
#if defined(GPIO_DOC_SWITCH)
static int s2mu004_set_gpio_doc_switch(int val)
{
	int doc_switch_gpio = muic_pdata.gpio_doc_switch;
	int doc_switch_val;
	int ret;

	ret = gpio_request(doc_switch_gpio, "GPIO_DOC_SWITCH");
	if (ret) {
		pr_err("[muic] failed to gpio_request GPIO_DOC_SWITCH\n");
		return ret;
	}

	doc_switch_val = gpio_get_value(doc_switch_gpio);

	if (gpio_is_valid(doc_switch_gpio))
			gpio_set_value(doc_switch_gpio, val);

	doc_switch_val = gpio_get_value(doc_switch_gpio);

	gpio_free(doc_switch_gpio);

	pr_info("[muic] GPIO_DOC_SWITCH(%d)=%c\n",
		doc_switch_gpio, (doc_switch_val == 0 ? 'L' : 'H'));

	return 0;
}
#endif /* GPIO_DOC_SWITCH */

#ifdef CONFIG_SEC_FACTORY
static int set_otg_reg(struct s2mu004_muic_data *muic_data, bool on)
{
	struct i2c_client *i2c = muic_data->i2c;
	u8 reg_val;
	int ret = 0;

	if (on) {
		muic_data->attach_mode = S2MU004_MUIC_OTG;
		/* enable vbus det for interrupt */
		reg_val = s2mu004_i2c_read_byte(muic_data->i2c, 0xDA);
		reg_val |= (0x01 << 4);
		s2mu004_i2c_write_byte(muic_data->i2c, 0xDA , reg_val);
	} else {
		/* disable vbus det for interrupt */
		if (muic_data->attach_mode == S2MU004_MUIC_OTG) {
			reg_val = s2mu004_i2c_read_byte(muic_data->i2c, 0xDA);
			reg_val &= 0xef;
			s2mu004_i2c_write_byte(muic_data->i2c, 0xDA , reg_val);
		}
	}

	/* 0x1e : hidden register */
	ret = s2mu004_i2c_read_byte(i2c, 0x1e);
	if (ret < 0)
		pr_err("[muic] %s err read 0x1e reg(%d)\n", __func__, ret);

	/* Set 0x1e[5:4] bit to 0x11 or 0x01 */
	if (on)
		reg_val = ret | (0x1 << 5);
	else
		reg_val = ret & ~(0x1 << 5);

	if (reg_val ^ ret) {
		pr_info("[muic] %s 0x%x != 0x%x, update\n", __func__, reg_val, ret);

		ret = s2mu004_i2c_guaranteed_wbyte(i2c, 0x1e, reg_val);
		if (ret < 0)
			pr_err("[muic] %s err write(%d)\n", __func__, ret);
	} else {
		pr_info("[muic] %s 0x%x == 0x%x, just return\n", __func__, reg_val, ret);
		return 0;
	}

	ret = s2mu004_i2c_read_byte(i2c, 0x1e);
	if (ret < 0)
		pr_err("[muic] %s err read reg 0x1e(%d)\n", __func__, ret);
	else
		pr_info("[muic] %s after change(0x%x)\n", __func__, ret);

	return ret;
}

static int init_otg_reg(struct s2mu004_muic_data *muic_data)
{
	struct i2c_client *i2c = muic_data->i2c;
	u8 reg_val;
	int ret = 0;

	/* 0x73 : check EVT0 or EVT1 */
	ret = s2mu004_i2c_read_byte(i2c, 0x73);
	if (ret < 0)
		pr_err("[muic] %s err read 'reg 0x73'(%d)\n", __func__, ret);

	if ((ret&0xF) > 0)
		return 0;

	/* 0x89 : hidden register */
	ret = s2mu004_i2c_read_byte(i2c, 0x89);
	if (ret < 0)
		pr_err("[muic] %s err read 'reg 0x89'(%d)\n", __func__, ret);

	/* Set 0x89[1] bit : T_DET_VAL */
	reg_val = ret | (0x1 << 1);

	if (reg_val ^ ret) {
		pr_info("[muic] %s 0x%x != 0x%x, update\n", __func__, reg_val, ret);

		ret = s2mu004_i2c_guaranteed_wbyte(i2c, 0x89, reg_val);
		if (ret < 0)
			pr_err("[muic] %s err write(%d)\n", __func__, ret);
	} else {
		pr_info("[muic] %s 0x%x == 0x%x, just return\n", __func__, reg_val, ret);
		return 0;
	}

	ret = s2mu004_i2c_read_byte(i2c, 0x89);
	if (ret < 0)
		pr_err("[muic] %s err read 'reg 0x89'(%d)\n", __func__, ret);
	else
		pr_info("[muic] %s after change(0x%x)\n", __func__, ret);

	/* 0x92 : hidden register */
	ret = s2mu004_i2c_read_byte(i2c, 0x92);
	if (ret < 0)
		pr_err("[muic] %s err read 'reg 0x92'(%d)\n", __func__, ret);

	/* Set 0x92[7] bit : EN_JIG_AP */
	reg_val = ret | (0x1 << 7);

	if (reg_val ^ ret) {
		pr_info("[muic] %s 0x%x != 0x%x, update\n", __func__, reg_val, ret);

		ret = s2mu004_i2c_guaranteed_wbyte(i2c, 0x92, reg_val);
		if (ret < 0)
			pr_err("[muic] %s err write(%d)\n", __func__, ret);
	} else {
		pr_info("[muic] %s 0x%x == 0x%x, just return\n",	__func__, reg_val, ret);
		return 0;
	}

	ret = s2mu004_i2c_read_byte(i2c, 0x92);
	if (ret < 0)
		pr_err("[muic] %s err read 'reg 0x92'(%d)\n", __func__, ret);
	else
		pr_info("[muic] %s after change(0x%x)\n", __func__, ret);

	return ret;
}
#endif

/* TODO: There is no needs to use JIGB pin by MUIC if CCIC is supported */
#if !defined(CONFIG_CCIC_S2MU004)
static int s2mu004_muic_jig_on(struct s2mu004_muic_data *muic_data)
{
	bool en = muic_data->is_jig_on;
	int reg = 0, ret = 0;

	pr_err("[muic] %s: %s\n", __func__, en ? "on" : "off");

	reg = s2mu004_i2c_read_byte(muic_data->i2c,
		S2MU004_REG_MUIC_SW_CTRL);

	if (en)
		reg |= MANUAL_SW_JIG_EN;
	else
		reg &= ~(MANUAL_SW_JIG_EN);

	ret = s2mu004_i2c_write_byte(muic_data->i2c,
			S2MU004_REG_MUIC_SW_CTRL, (u8)reg);
	if (en) {
		reg = s2mu004_i2c_read_byte(muic_data->i2c, S2MU004_REG_MUIC_CTRL1);
		if (muic_data->is_rustproof) {
			pr_info("[muic] %s rustproof mode! set manual switching!\n", __func__);
			reg &= ~(CTRL_MANUAL_SW_MASK);
		}
		else {
			pr_info("[muic] %s NOT rustproof mode! set auto switching!\n", __func__);
			reg |= CTRL_MANUAL_SW_MASK;
		}

		return s2mu004_i2c_guaranteed_wbyte(muic_data->i2c,
				S2MU004_REG_MUIC_CTRL1, (u8)reg);
	} else
		return ret;

}
#endif

static ssize_t s2mu004_muic_show_uart_en(struct device *dev,
						struct device_attribute *attr, char *buf)
{
	struct s2mu004_muic_data *muic_data = dev_get_drvdata(dev);
	int ret = 0;

	if (!muic_data->is_rustproof) {
		pr_info("[muic] %s UART ENABLE\n",  __func__);
		ret = sprintf(buf, "1\n");
	} else {
		pr_info("[muic] %s UART DISABLE\n",  __func__);
		ret = sprintf(buf, "0\n");
	}

	return ret;
}

static ssize_t s2mu004_muic_set_uart_en(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	struct s2mu004_muic_data *muic_data = dev_get_drvdata(dev);

	if (!strncmp(buf, "1", 1))
		muic_data->is_rustproof = false;
	else if (!strncmp(buf, "0", 1))
		muic_data->is_rustproof = true;
	else
		pr_info("[muic] %s invalid value\n",  __func__);

	pr_info("[muic] %s uart_en(%d)\n",
		__func__, !muic_data->is_rustproof);

	return count;
}

static ssize_t s2mu004_muic_show_usb_en(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct s2mu004_muic_data *muic_data = dev_get_drvdata(dev);

	return sprintf(buf, "%s:%s attached_dev = %d\n",
		MUIC_DEV_NAME, __func__, muic_data->attached_dev);
}

static ssize_t s2mu004_muic_set_usb_en(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct s2mu004_muic_data *muic_data = dev_get_drvdata(dev);
	muic_attached_dev_t new_dev = ATTACHED_DEV_USB_MUIC;

	if (!strncasecmp(buf, "1", 1))
		s2mu004_muic_handle_attach(muic_data, new_dev, 0, 0);
	else if (!strncasecmp(buf, "0", 1))
		s2mu004_muic_handle_detach(muic_data);
	else
		pr_info("[muic] %s invalid value\n", __func__);

	pr_info("[muic] %s attached_dev(%d)\n",
		__func__, muic_data->attached_dev);

	return count;
}

static ssize_t s2mu004_muic_show_adc(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct s2mu004_muic_data *muic_data = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&muic_data->muic_mutex);

#if defined(CONFIG_CCIC_S2MU004)	
	ret = s2mu004_muic_refresh_adc(muic_data);
#else
	ret = s2mu004_i2c_read_byte(muic_data->i2c, S2MU004_REG_MUIC_ADC);
#endif
	mutex_unlock(&muic_data->muic_mutex);
	if (ret < 0) {
		pr_err("[muic] %s err read adc reg(%d)\n",
			__func__, ret);
		return sprintf(buf, "UNKNOWN\n");
	}

	return sprintf(buf, "%x\n", (ret & ADC_MASK));
}

static ssize_t s2mu004_muic_show_usb_state(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct s2mu004_muic_data *muic_data = dev_get_drvdata(dev);
	static unsigned long swtich_slot_time;

	if (printk_timed_ratelimit(&swtich_slot_time, 5000))
		pr_info("[muic] %s muic_data->attached_dev(%d)\n",
			__func__, muic_data->attached_dev);

	switch (muic_data->attached_dev) {
	case ATTACHED_DEV_USB_MUIC:
	case ATTACHED_DEV_CDP_MUIC:
	case ATTACHED_DEV_JIG_USB_OFF_MUIC:
	case ATTACHED_DEV_JIG_USB_ON_MUIC:
		return sprintf(buf, "USB_STATE_CONFIGURED\n");
	default:
		break;
	}

	return 0;
}

#ifdef DEBUG_MUIC
static ssize_t s2mu004_muic_show_mansw(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct s2mu004_muic_data *muic_data = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&muic_data->muic_mutex);
	ret = s2mu004_i2c_read_byte(muic_data->i2c, S2MU004_REG_MUIC_SW_CTRL);
	mutex_unlock(&muic_data->muic_mutex);

	pr_info("func:%s ret:%d buf%s\n", __func__, ret, buf);

	if (ret < 0) {
		pr_err("%s: fail to read muic reg\n", __func__);
		return sprintf(buf, "UNKNOWN\n");
	}
	return sprintf(buf, "0x%x\n", ret);
}
static ssize_t s2mu004_muic_show_interrupt_status(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct s2mu004_muic_data *muic_data = dev_get_drvdata(dev);
	int st1, st2;

	mutex_lock(&muic_data->muic_mutex);
	st1 = s2mu004_i2c_read_byte(muic_data->i2c, S2MU004_REG_MUIC_INT1);
	st2 = s2mu004_i2c_read_byte(muic_data->i2c, S2MU004_REG_MUIC_INT2);
	mutex_unlock(&muic_data->muic_mutex);

	pr_info("func:%s st1:0x%x st2:0x%x buf%s\n", __func__, st1, st2, buf);

	if (st1 < 0 || st2 < 0) {
		pr_err("%s: fail to read muic reg\n", __func__);
		return sprintf(buf, "UNKNOWN\n");
	}
	return sprintf(buf, "st1:0x%x st2:0x%x\n", st1, st2);
}
static ssize_t s2mu004_muic_show_registers(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct s2mu004_muic_data *muic_data = dev_get_drvdata(dev);
	char mesg[256] = "";

	mutex_lock(&muic_data->muic_mutex);
	s2mu004_read_reg_dump(muic_data, mesg);
	mutex_unlock(&muic_data->muic_mutex);
	pr_info("%s:%s\n", __func__, mesg);

	return sprintf(buf, "%s\n", mesg);
}

#endif

#if defined(CONFIG_USB_HOST_NOTIFY)
static ssize_t s2mu004_muic_show_otg_test(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct s2mu004_muic_data *muic_data = dev_get_drvdata(dev);
	int ret;
	u8 val = 0;

	mutex_lock(&muic_data->muic_mutex);
	ret = s2mu004_i2c_read_byte(muic_data->i2c,
		S2MU004_REG_MUIC_INT2_MASK);
	mutex_unlock(&muic_data->muic_mutex);

	if (ret < 0) {
		pr_err("[muic] %s: fail to read muic reg\n", __func__);
		return sprintf(buf, "UNKNOWN\n");
	}

	pr_info("[muic] func:%s ret:%d val:%x buf%s\n",
		__func__, ret, val, buf);

	val &= INT_VBUS_ON_MASK;
	return sprintf(buf, "%x\n", val);
}

static ssize_t s2mu004_muic_set_otg_test(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct s2mu004_muic_data *muic_data = dev_get_drvdata(dev);

	pr_info("[muic] %s buf:%s\n", __func__, buf);

	/*
	*	The otg_test is set 0 durring the otg test. Not 1 !!!
	*/

	if (!strncmp(buf, "0", 1)) {
		muic_data->is_otg_test = 1;
#ifdef CONFIG_SEC_FACTORY
		set_otg_reg(muic_data, 1);
#endif
	} else if (!strncmp(buf, "1", 1)) {
		muic_data->is_otg_test = 0;
#ifdef CONFIG_SEC_FACTORY
		set_otg_reg(muic_data, 0);
#endif
	} else {
		pr_info("[muic] %s Wrong command\n", __func__);
		return count;
	}

	return count;
}
#endif

#ifdef	CONFIG_MUIC_SUPPORT_CCIC
static ssize_t s2mu004_muic_show_attached_dev(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct s2mu004_muic_data *muic_data = dev_get_drvdata(dev);
#if 0
	muic_data_t *pmuic = (muic_data_t *)muic_data->ccic_data;
	int mdev = muic_get_current_legacy_dev(pmuic);
#else 
	int mdev = muic_data->attached_dev;
#endif
	pr_info("%s:%s attached_dev:%d\n", MUIC_DEV_NAME, __func__,
			mdev);

	switch(mdev) {
	case ATTACHED_DEV_NONE_MUIC:
		return sprintf(buf, "No VPS\n");
	case ATTACHED_DEV_USB_MUIC:
		return sprintf(buf, "USB\n");
	case ATTACHED_DEV_CDP_MUIC:
		return sprintf(buf, "CDP\n");
	case ATTACHED_DEV_OTG_MUIC:
		return sprintf(buf, "OTG\n");
	case ATTACHED_DEV_TA_MUIC:
		return sprintf(buf, "TA\n");
	case ATTACHED_DEV_JIG_UART_OFF_MUIC:
		return sprintf(buf, "JIG UART OFF\n");
	case ATTACHED_DEV_JIG_UART_OFF_VB_MUIC:
		return sprintf(buf, "JIG UART OFF/VB\n");
	case ATTACHED_DEV_JIG_UART_ON_MUIC:
		return sprintf(buf, "JIG UART ON\n");
	case ATTACHED_DEV_JIG_UART_ON_VB_MUIC:
		return sprintf(buf, "JIG UART ON/VB\n");
	case ATTACHED_DEV_JIG_USB_OFF_MUIC:
		return sprintf(buf, "JIG USB OFF\n");
	case ATTACHED_DEV_JIG_USB_ON_MUIC:
		return sprintf(buf, "JIG USB ON\n");
	case ATTACHED_DEV_DESKDOCK_MUIC:
		return sprintf(buf, "DESKDOCK\n");
	case ATTACHED_DEV_AUDIODOCK_MUIC:
		return sprintf(buf, "AUDIODOCK\n");
	case ATTACHED_DEV_CHARGING_CABLE_MUIC:
		return sprintf(buf, "PS CABLE\n");
	case ATTACHED_DEV_AFC_CHARGER_5V_MUIC:
	case ATTACHED_DEV_AFC_CHARGER_9V_MUIC:
	case ATTACHED_DEV_QC_CHARGER_5V_MUIC:
	case ATTACHED_DEV_QC_CHARGER_9V_MUIC:
		return sprintf(buf, "AFC Charger\n");
	default:
		break;
	}

	return sprintf(buf, "UNKNOWN\n");
}
#else
static ssize_t s2mu004_muic_show_attached_dev(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct s2mu004_muic_data *muic_data = dev_get_drvdata(dev);

	pr_info("[muic] %s :%d\n",
		__func__, muic_data->attached_dev);

	switch (muic_data->attached_dev) {
	case ATTACHED_DEV_NONE_MUIC:
		return sprintf(buf, "No VPS\n");
	case ATTACHED_DEV_USB_MUIC:
		return sprintf(buf, "USB\n");
	case ATTACHED_DEV_CDP_MUIC:
		return sprintf(buf, "CDP\n");
	case ATTACHED_DEV_OTG_MUIC:
		return sprintf(buf, "OTG\n");
	case ATTACHED_DEV_TA_MUIC:
		return sprintf(buf, "TA\n");
	case ATTACHED_DEV_JIG_UART_OFF_MUIC:
		return sprintf(buf, "JIG UART OFF\n");
	case ATTACHED_DEV_JIG_UART_OFF_VB_MUIC:
		return sprintf(buf, "JIG UART OFF/VB\n");
	case ATTACHED_DEV_JIG_UART_ON_MUIC:
		return sprintf(buf, "JIG UART ON\n");
	case ATTACHED_DEV_JIG_UART_ON_VB_MUIC:
		return sprintf(buf, "JIG UART ON/VB\n");
	case ATTACHED_DEV_JIG_USB_OFF_MUIC:
		return sprintf(buf, "JIG USB OFF\n");
	case ATTACHED_DEV_JIG_USB_ON_MUIC:
		return sprintf(buf, "JIG USB ON\n");
	case ATTACHED_DEV_DESKDOCK_MUIC:
		return sprintf(buf, "DESKDOCK\n");
	case ATTACHED_DEV_AUDIODOCK_MUIC:
		return sprintf(buf, "AUDIODOCK\n");
	default:
		break;
	}

	return sprintf(buf, "UNKNOWN\n");
}
#endif

static ssize_t s2mu004_muic_show_audio_path(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	return 0;
}

static ssize_t s2mu004_muic_set_audio_path(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t count)
{
	return 0;
}

static ssize_t s2mu004_muic_show_apo_factory(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	struct s2mu004_muic_data *muic_data = dev_get_drvdata(dev);
	const char *mode;

	/* true: Factory mode, false: not Factory mode */
	if (muic_data->is_factory_start)
		mode = "FACTORY_MODE";
	else
		mode = "NOT_FACTORY_MODE";

	pr_info("[muic] %s : %s\n",
		__func__, mode);

	return sprintf(buf, "%s\n", mode);
}

static ssize_t s2mu004_muic_set_apo_factory(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct s2mu004_muic_data *muic_data = dev_get_drvdata(dev);
	const char *mode;

	pr_info("[muic] %s buf:%s\n",
		__func__, buf);

	/* "FACTORY_START": factory mode */
	if (!strncmp(buf, "FACTORY_START", 13)) {
		muic_data->is_factory_start = true;
		mode = "FACTORY_MODE";
	} else {
		pr_info("[muic] %s Wrong command\n",  __func__);
		return count;
	}

	return count;
}

static ssize_t muic_show_vbus_value(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct s2mu004_muic_data *muic_data = dev_get_drvdata(dev);
	struct i2c_client *i2c = muic_data->i2c;
	int val = 0; 
	u8 ret, vbadc, afc_ctrl = 0;


	if ( (muic_data->attached_dev != ATTACHED_DEV_AFC_CHARGER_9V_MUIC)
		& (muic_data->attached_dev != ATTACHED_DEV_QC_CHARGER_9V_MUIC) ) {
		/* Set AFC_EN, VB_ADC_EN to true in case of did not prepared afc
		   for PD charger or Normal TA with flag afc_disable  */
		pr_info("%s: Set AFC_EN, VB_ADC_ON \n", __func__);
		afc_ctrl = s2mu004_i2c_read_byte(i2c, S2MU004_REG_AFC_CTRL1);
		afc_ctrl |= (0x01 << 7);
		afc_ctrl |= (0x01 << 5);
		s2mu004_i2c_write_byte(i2c, S2MU004_REG_AFC_CTRL1, afc_ctrl);
		mdelay(10);
		afc_ctrl = s2mu004_i2c_read_byte(i2c, S2MU004_REG_AFC_CTRL1);
	}

	/* 2. Read VBADC */
	ret = s2mu004_i2c_read_byte(i2c, S2MU004_REG_AFC_STATUS);
	if (ret < 0)
		pr_err("%s:%s err read AFC STATUS(0x%2x)\n", MUIC_DEV_NAME, __func__, ret);

	vbadc = ret & STATUS_VBADC_MASK;
	pr_info("%s:%s vbadc:0x%x, afc_ctrl:0x%x cable:(%d)\n", MUIC_DEV_NAME,
			__func__, vbadc, afc_ctrl, muic_data->attached_dev);

	switch (vbadc) {
	case VBADC_5_3V:
	case VBADC_5_7V_6_3V:
		val = 5; 
		break;
	case VBADC_8_7V_9_3V:
	case VBADC_9_7V_10_3V:
		val = 9; 
		break;
	default:
		break;
	}

	/* NOTE: If enter this statement with 9V, voltage would be decreased to 5V */
	if ( (muic_data->attached_dev != ATTACHED_DEV_AFC_CHARGER_9V_MUIC)
		& (muic_data->attached_dev != ATTACHED_DEV_QC_CHARGER_9V_MUIC) ) {
		/* Clear AFC_EN, VB_ADC_EN  */
		pr_info("%s: Clear AFC_EN, VB_ADC_ON \n", __func__);
		afc_ctrl = s2mu004_i2c_read_byte(i2c, S2MU004_REG_AFC_CTRL1);
		afc_ctrl &= ~(0x01 << 7);
		afc_ctrl &= ~(0x01 << 5);
		s2mu004_i2c_write_byte(i2c, S2MU004_REG_AFC_CTRL1, afc_ctrl);
		afc_ctrl = s2mu004_i2c_read_byte(i2c, S2MU004_REG_AFC_CTRL1);
	}

	pr_info("%s:%s VBUS:%d, afc_ctrl:0x%x\n", MUIC_DEV_NAME, __func__, val, afc_ctrl);

	if (val > 0) 
		return sprintf(buf, "%dV\n", val);

	return sprintf(buf, "UNKNOWN\n");
}

#if defined(CONFIG_HV_MUIC_S2MU004_AFC)
static ssize_t s2mu004_muic_show_afc_disable(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct s2mu004_muic_data *muic_data = dev_get_drvdata(dev);
	struct muic_platform_data *pdata = muic_data->pdata;

	if (pdata->afc_disable) {
		pr_info("%s:%s AFC DISABLE\n", MUIC_DEV_NAME, __func__);
		return sprintf(buf, "1\n");
	}

	pr_info("%s:%s AFC ENABLE", MUIC_DEV_NAME, __func__);
	return sprintf(buf, "0\n");
}

static ssize_t s2mu004_muic_set_afc_disable(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct s2mu004_muic_data *muic_data = dev_get_drvdata(dev);
	struct muic_platform_data *pdata = muic_data->pdata;
#if defined(CONFIG_CCIC_S2MU004)
	struct i2c_client *i2c = muic_data->i2c;
	int data = 0;
#endif
#if defined(CONFIG_HV_MUIC_VOLTAGE_CTRL) || defined(CONFIG_SUPPORT_QC30)
	union power_supply_propval psy_val;
#endif

	mutex_lock(&muic_data->muic_mutex);
	if (!strncasecmp(buf, "1", 1)) {
		pdata->afc_disable = true;
	} else if (!strncasecmp(buf, "0", 1)) {
		pdata->afc_disable = false;
	} else {
		pr_warn("%s:%s invalid value\n", MUIC_DEV_NAME, __func__);
	}

	pr_info("%s:%s afc_disable(%d)\n", MUIC_DEV_NAME, __func__, pdata->afc_disable);

#if defined(CONFIG_HV_MUIC_VOLTAGE_CTRL) || defined(CONFIG_SUPPORT_QC30)
	psy_val.intval = pdata->afc_disable ? '1' : '0';
	psy_do_property("battery", set,
		POWER_SUPPLY_EXT_PROP_HV_DISABLE, psy_val);
#endif
	/* FIXME: for factory self charging test (AFC-> NORMAL TA) */
	if (muic_data->is_factory_start) {
		pr_info("%s :[muic] re-detect chg \n", __func__);

#if defined(CONFIG_CCIC_S2MU004)
		/* 2 AFC RESET */
		data = s2mu004_i2c_read_byte(i2c, 0x5F);
		data |= (0x01 << 7);
		s2mu004_i2c_write_byte(i2c, 0x5F, data);

		/* 3 ADC OFF */
		data = s2mu004_i2c_read_byte(i2c, 0xCC);
		data &= ~(0x01 << 1);
		s2mu004_i2c_write_byte(i2c, 0xCC, data);
		mdelay(50);

		/* 4. disable rid detection for muic dp dm detect */
		data = s2mu004_i2c_read_byte(i2c, 0xCC);
		data |= (0x01 << 1);
		s2mu004_i2c_write_byte(i2c, 0xCC, data);
		mdelay(50);

		/* 5 Charger Detection Restart */
		data = s2mu004_i2c_read_byte(i2c, 0x6A);
		data |= (0x01 << 0);
		s2mu004_i2c_write_byte(i2c, 0x6A, data);
		pr_info("%s :[muic] re-detect chg done \n", __func__);

		/* TODO: When closing charge test, there would be afc_disable 0
		 * It needs to set is_afc_muic_ready to false in order to
		 * detect afc 9V again */
		if (!pdata->afc_disable)
			s2mu004_muic_handle_detach(muic_data);
		/* Needs to 150ms after rescan */
		mdelay(150);
#else
		s2mu004_muic_handle_detach(muic_data);
		s2mu004_muic_detect_dev(muic_data);
#endif
	}

	mutex_unlock(&muic_data->muic_mutex);
	return count;
}

#ifdef CONFIG_HV_MUIC_VOLTAGE_CTRL
extern void hv_muic_change_afc_voltage(int tx_data);

static ssize_t muic_store_afc_set_voltage(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	if (!strncasecmp(buf, "5V", 2)) {
		hv_muic_change_afc_voltage(MUIC_HV_5V);
	} else if (!strncasecmp(buf, "9V", 2)) {
		hv_muic_change_afc_voltage(MUIC_HV_9V);
	} else {
		pr_warn("%s:%s invalid value : %s\n", MUIC_DEV_NAME, __func__, buf);
		return count;
	}

	return count;
}
#endif /* CONFIG_HV_MUIC_VOLTAGE_CTRL */
#endif /* CONFIG_HV_MUIC_S2MU004_AFC */

static DEVICE_ATTR(uart_en, 0664, s2mu004_muic_show_uart_en,
					s2mu004_muic_set_uart_en);
static DEVICE_ATTR(adc, 0664, s2mu004_muic_show_adc, NULL);
#ifdef DEBUG_MUIC
static DEVICE_ATTR(mansw, 0664, s2mu004_muic_show_mansw, NULL);
static DEVICE_ATTR(dump_registers, 0664, s2mu004_muic_show_registers, NULL);
static DEVICE_ATTR(int_status, 0664, s2mu004_muic_show_interrupt_status, NULL);
#endif
static DEVICE_ATTR(usb_state, 0664, s2mu004_muic_show_usb_state, NULL);
#if defined(CONFIG_USB_HOST_NOTIFY)
static DEVICE_ATTR(otg_test, 0664,
		s2mu004_muic_show_otg_test, s2mu004_muic_set_otg_test);
#endif
static DEVICE_ATTR(attached_dev, 0664, s2mu004_muic_show_attached_dev, NULL);
static DEVICE_ATTR(audio_path, 0664,
		s2mu004_muic_show_audio_path, s2mu004_muic_set_audio_path);
static DEVICE_ATTR(apo_factory, 0664,
		s2mu004_muic_show_apo_factory,
		s2mu004_muic_set_apo_factory);
static DEVICE_ATTR(usb_en, 0664,
		s2mu004_muic_show_usb_en,
		s2mu004_muic_set_usb_en);
static DEVICE_ATTR(vbus_value, 0444, muic_show_vbus_value, NULL);
#if defined(CONFIG_HV_MUIC_S2MU004_AFC)
static DEVICE_ATTR(afc_disable, 0664,
		s2mu004_muic_show_afc_disable, s2mu004_muic_set_afc_disable);
#ifdef CONFIG_HV_MUIC_VOLTAGE_CTRL
static DEVICE_ATTR(afc_set_voltage, 0220,
		NULL, muic_store_afc_set_voltage);
#endif /* CONFIG_HV_MUIC_VOLTAGE_CTRL */
#endif /* CONFIG_HV_MUIC_S2MU004_AFC */


static struct attribute *s2mu004_muic_attributes[] = {
	&dev_attr_uart_en.attr,
	&dev_attr_adc.attr,
#ifdef DEBUG_MUIC
	&dev_attr_mansw.attr,
	&dev_attr_dump_registers.attr,
	&dev_attr_int_status.attr,
#endif
	&dev_attr_usb_state.attr,
#if defined(CONFIG_USB_HOST_NOTIFY)
	&dev_attr_otg_test.attr,
#endif
	&dev_attr_attached_dev.attr,
	&dev_attr_audio_path.attr,
	&dev_attr_apo_factory.attr,
	&dev_attr_usb_en.attr,
	&dev_attr_vbus_value.attr,
#if defined(CONFIG_HV_MUIC_S2MU004_AFC)
	&dev_attr_afc_disable.attr,
#ifdef CONFIG_HV_MUIC_VOLTAGE_CTRL
	&dev_attr_afc_set_voltage.attr,
#endif /* CONFIG_HV_MUIC_VOLTAGE_CTRL */
#endif /* CONFIG_HV_MUIC_S2MU004_AFC */
	NULL
};

static const struct attribute_group s2mu004_muic_group = {
	.attrs = s2mu004_muic_attributes,
};

static int set_ctrl_reg(struct s2mu004_muic_data *muic_data, int shift, bool on)
{
	struct i2c_client *i2c = muic_data->i2c;
	u8 reg_val;
	int ret = 0;

	ret = s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_CTRL1);
	if (ret < 0)
		pr_err("[muic] %s err read CTRL(%d)\n",
			__func__, ret);

	if (on)
		reg_val = ret | (0x1 << shift);
	else
		reg_val = ret & ~(0x1 << shift);

	if (reg_val ^ ret) {
		pr_info("[muic] %s 0x%x != 0x%x, update\n",
			__func__, reg_val, ret);

		ret = s2mu004_i2c_guaranteed_wbyte(i2c, S2MU004_REG_MUIC_CTRL1,
				reg_val);
		if (ret < 0)
			pr_err("[muic] %s err write(%d)\n",
				__func__, ret);
	} else {
		pr_info("[muic] %s 0x%x == 0x%x, just return\n",
			__func__, reg_val, ret);
		return 0;
	}

	ret = s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_CTRL1);
	if (ret < 0)
		pr_err("[muic] %s err read CTRL(%d)\n", __func__, ret);
	else
		pr_info("[muic] %s after change(0x%x)\n",
			__func__, ret);

	return ret;
}

static int set_int_mask(struct s2mu004_muic_data *muic_data, bool on)
{
	int shift = CTRL_INT_MASK_SHIFT;
	int ret = 0;

	ret = set_ctrl_reg(muic_data, shift, on);

	return ret;
}

static int set_manual_sw(struct s2mu004_muic_data *muic_data, bool on)
{
	int shift = CTRL_MANUAL_SW_SHIFT;
	int ret = 0;

	ret = set_ctrl_reg(muic_data, shift, on);

	return ret;
}

static int set_com_sw(struct s2mu004_muic_data *muic_data,
			enum s2mu004_reg_manual_sw_value reg_val)
{
	struct i2c_client *i2c = muic_data->i2c;
	int ret = 0;
	int temp = 0;

	/*  --- MANSW [7:5][4:2][1][0] : DM DP RSVD JIG  --- */
	temp = s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_SW_CTRL);
	if (temp < 0)
		pr_err("[muic] %s err read MANSW(0x%x)\n",
			__func__, temp);

	if ((reg_val & MANUAL_SW_DM_DP_MASK) != (temp & MANUAL_SW_DM_DP_MASK)) {
		pr_info("[muic] %s 0x%x != 0x%x, update\n",
			__func__, (reg_val & MANUAL_SW_DM_DP_MASK), (temp & MANUAL_SW_DM_DP_MASK));

		ret = s2mu004_i2c_guaranteed_wbyte(i2c,
			S2MU004_REG_MUIC_SW_CTRL, ((reg_val & MANUAL_SW_DM_DP_MASK)|(temp & 0x03)));
		if (ret < 0)
			pr_err("[muic] %s err write MANSW(0x%x)\n",
				__func__, ((reg_val & MANUAL_SW_DM_DP_MASK)|(temp & 0x03)));
	} else {
		pr_info("[muic] %s MANSW reg(0x%x), just pass\n",
			__func__, reg_val);
	}

	return ret;
}

static int com_to_open_with_vbus(struct s2mu004_muic_data *muic_data)
{
	enum s2mu004_reg_manual_sw_value reg_val;
	int ret = 0;

	reg_val = MANSW_OPEN_WITH_VBUS;
	ret = set_com_sw(muic_data, reg_val);
	if (ret)
		pr_err("[muic] %s set_com_sw err\n", __func__);

	return ret;
}

#ifndef com_to_open
static int com_to_open(struct s2mu004_muic_data *muic_data)
{
	enum s2mu004_reg_manual_sw_value reg_val;
	int ret = 0;
	u8 vbvolt;

	vbvolt = s2mu004_i2c_read_byte(muic_data->i2c, S2MU004_REG_MUIC_DEVICE_APPLE);
	vbvolt &= DEV_TYPE_APPLE_VBUS_WAKEUP;
	if (vbvolt) {
		ret = com_to_open_with_vbus(muic_data);
		return ret;
	}

	reg_val = MANSW_OPEN;
	ret = set_com_sw(muic_data, reg_val);
	if (ret)
		pr_err("[muic] %s set_com_sw err\n", __func__);

	return ret;
}
#endif

static int com_to_usb(struct s2mu004_muic_data *muic_data)
{
	enum s2mu004_reg_manual_sw_value reg_val;
	int ret = 0;

	reg_val = MANSW_USB;
	ret = set_com_sw(muic_data, reg_val);
	if (ret)
		pr_err("[muic] %s set_com_usb err\n", __func__);

	return ret;
}

static int com_to_otg(struct s2mu004_muic_data *muic_data)
{
	enum s2mu004_reg_manual_sw_value reg_val;
	int ret = 0;

	reg_val = MANSW_OTG;
	ret = set_com_sw(muic_data, reg_val);
	if (ret)
		pr_err("[muic] %s set_com_otg err\n", __func__);

	return ret;
}

static int com_to_uart(struct s2mu004_muic_data *muic_data)
{
	enum s2mu004_reg_manual_sw_value reg_val;
	int ret = 0;

	if (muic_data->is_rustproof) {
		pr_info("[muic] %s rustproof mode\n", __func__);
		return ret;
	}
	reg_val = MANSW_UART;
	ret = set_com_sw(muic_data, reg_val);
	if (ret)
		pr_err("[muic] %s set_com_uart err\n", __func__);

	return ret;
}

static int com_to_audio(struct s2mu004_muic_data *muic_data)
{
	enum s2mu004_reg_manual_sw_value reg_val;
	int ret = 0;

	reg_val = MANSW_AUDIO;
	ret = set_com_sw(muic_data, reg_val);
	if (ret)
		pr_err("[muic] %s set_com_audio err\n", __func__);

	return ret;
}

static int switch_to_dock_audio(struct s2mu004_muic_data *muic_data)
{
	int ret = 0;

	pr_info("[muic] %s\n", __func__);

	ret = com_to_audio(muic_data);
	if (ret) {
		pr_err("[muic] %s com->audio set err\n", __func__);
		return ret;
	}

	return ret;
}

static int switch_to_system_audio(struct s2mu004_muic_data *muic_data)
{
	int ret = 0;

	pr_info("[muic] %s\n", __func__);

	return ret;
}


static int switch_to_ap_usb(struct s2mu004_muic_data *muic_data)
{
	int ret = 0;

	pr_info("[muic] %s\n", __func__);

	ret = com_to_usb(muic_data);
	if (ret) {
		pr_err("[muic] %s com->usb set err\n", __func__);
		return ret;
	}

	return ret;
}

static int switch_to_cp_usb(struct s2mu004_muic_data *muic_data)
{
	int ret = 0;

	pr_info("[muic] %s\n", __func__);

	ret = com_to_usb(muic_data);
	if (ret) {
		pr_err("[muic] %s com->usb set err\n", __func__);
		return ret;
	}

	return ret;
}

static int switch_to_ap_uart(struct s2mu004_muic_data *muic_data)
{
	int ret = 0;

	pr_info("[muic] %s\n", __func__);
#if !defined(CONFIG_MUIC_UART_SWITCH)
	if (muic_data->pdata->gpio_uart_sel)
#endif
		muic_data->pdata->set_gpio_uart_sel(MUIC_PATH_UART_AP);

	ret = com_to_uart(muic_data);
	if (ret) {
		pr_err("[muic] %s com->uart set err\n", __func__);
		return ret;
	}

	return ret;
}

static int switch_to_cp_uart(struct s2mu004_muic_data *muic_data)
{
	int ret = 0;

	pr_info("[muic] %s\n", __func__);
#if !defined(CONFIG_MUIC_UART_SWITCH)
	if (muic_data->pdata->gpio_uart_sel)
#endif
		muic_data->pdata->set_gpio_uart_sel(MUIC_PATH_UART_CP);

	ret = com_to_uart(muic_data);
	if (ret) {
		pr_err("[muic] %s com->uart set err\n", __func__);
		return ret;
	}

	return ret;
}

static int attach_charger(struct s2mu004_muic_data *muic_data,
			muic_attached_dev_t new_dev)
{
	int ret = 0;

	pr_info("[muic] %s : %d\n", __func__, new_dev);

	muic_data->attached_dev = new_dev;

	return ret;
}

static int detach_charger(struct s2mu004_muic_data *muic_data)
{
	int ret = 0;

	pr_info("[muic] %s\n", __func__);

	muic_data->attached_dev = ATTACHED_DEV_NONE_MUIC;

	return ret;
}

static int attach_usb_util(struct s2mu004_muic_data *muic_data,
			muic_attached_dev_t new_dev)
{
	int ret = 0;

	ret = attach_charger(muic_data, new_dev);
	if (ret)
		return ret;

	if (muic_data->pdata->usb_path == MUIC_PATH_USB_CP) {
		ret = switch_to_cp_usb(muic_data);
		return ret;
	}

	ret = switch_to_ap_usb(muic_data);
	return ret;
}

static int attach_usb(struct s2mu004_muic_data *muic_data,
			muic_attached_dev_t new_dev)
{
	int ret = 0;

	if (muic_data->attached_dev == new_dev) {
		pr_info("[muic] %s duplicated\n", __func__);
		return ret;
	}

	pr_info("%s:%s\n", MFD_DEV_NAME, __func__);

	pr_info("[muic] %s\n", __func__);

	ret = attach_usb_util(muic_data, new_dev);
	if (ret)
		return ret;

	return ret;
}
#if 0
static int set_vbus_interrupt(struct s2mu004_muic_data *muic_data, int enable)
{
	struct i2c_client *i2c = muic_data->i2c;
	int ret = 0;

	if (enable) {
		ret = s2mu004_i2c_write_byte(i2c, S2MU004_REG_MUIC_INT2_MASK,
			REG_INTMASK2_VBUS);
		if (ret < 0)
			pr_err("[muic] %s(%d)\n", __func__, ret);
	} else {
		ret = s2mu004_i2c_write_byte(i2c, S2MU004_REG_MUIC_INT2_MASK,
			REG_INTMASK2_VALUE);
		if (ret < 0)
			pr_err("[muic] %s(%d)\n", __func__, ret);
	}
	return ret;
}
#endif
static int attach_otg_usb(struct s2mu004_muic_data *muic_data,
			muic_attached_dev_t new_dev)
{
	int ret = 0;

	if (muic_data->attached_dev == new_dev) {
		pr_info("[muic] %s duplicated\n", __func__);
		return ret;
	}

	pr_info("[muic] %s\n", __func__);

#ifdef CONFIG_MUIC_S2MU004_SUPPORT_LANHUB
	/* LANHUB doesn't work under AUTO switch mode, so turn it off */
	/* set MANUAL SW mode */
	set_manual_sw(muic_data, 0);

	/* enable RAW DATA mode, only for OTG LANHUB */
	set_ctrl_reg(muic_data, CTRL_RAW_DATA_SHIFT, 0);
#endif

	ret = com_to_otg(muic_data);

	muic_data->attached_dev = new_dev;

	return ret;
}

static int detach_otg_usb(struct s2mu004_muic_data *muic_data)
{
	struct muic_platform_data *pdata = muic_data->pdata;
	int ret = 0;

	pr_info("[muic] %s : %d\n",
		__func__, muic_data->attached_dev);

	ret = com_to_open(muic_data);
	if (ret)
		return ret;

#ifdef CONFIG_MUIC_S2MU004_SUPPORT_LANHUB
	/* disable RAW DATA mode */
	set_ctrl_reg(muic_data, CTRL_RAW_DATA_SHIFT, 1);

#ifdef CONFIG_MACH_DEGAS
	/* System rev less than 0.1 cannot use Auto switch mode in DEGAS */
	if (system_rev >= 0x1)
#endif
		/* set AUTO SW mode */
		set_manual_sw(muic_data, 1);
#endif

	muic_data->attached_dev = ATTACHED_DEV_NONE_MUIC;

	if (pdata->usb_path == MUIC_PATH_USB_CP)
		return ret;

	return ret;
}

static int detach_usb(struct s2mu004_muic_data *muic_data)
{
	struct muic_platform_data *pdata = muic_data->pdata;
	int ret = 0;

	pr_info("[muic] %s : %d\n",
		__func__, muic_data->attached_dev);

	ret = detach_charger(muic_data);
	if (ret)
		return ret;

	ret = com_to_open(muic_data);
	if (ret)
		return ret;

	muic_data->attached_dev = ATTACHED_DEV_NONE_MUIC;

	if (pdata->usb_path == MUIC_PATH_USB_CP)
		return ret;

	return ret;
}

static int attach_deskdock(struct s2mu004_muic_data *muic_data,
			muic_attached_dev_t new_dev, u8 vbvolt)
{
	int ret = 0;

	pr_info("[muic] %s vbus(%x)\n", __func__, vbvolt);

#ifdef CONFIG_MACH_DEGAS
	/* Audio-out doesn't work under AUTO switch mode, so turn it off */
	/* set MANUAL SW mode */
	set_manual_sw(muic_data, 0);
#endif

	ret = switch_to_dock_audio(muic_data);
	if (ret)
		return ret;

	if (vbvolt)
		ret = attach_charger(muic_data, new_dev);
	else
		ret = detach_charger(muic_data);
	if (ret)
		return ret;

	muic_data->attached_dev = new_dev;

	return ret;
}

static int detach_deskdock(struct s2mu004_muic_data *muic_data)
{
	int ret = 0;

	pr_info("[muic] %s\n", __func__);

#ifdef CONFIG_MACH_DEGAS
	/* System rev less than 0.1 cannot use Auto switch mode in DEGAS */
	if (system_rev >= 0x1)
		set_manual_sw(muic_data, 1); /* set AUTO SW mode */
#endif

	ret = switch_to_system_audio(muic_data);
	if (ret)
		pr_err("[muic] %s err changing audio path(%d)\n",
			__func__, ret);

	ret = detach_charger(muic_data);
	if (ret)
		pr_err("[muic] %s err detach_charger(%d)\n",
			__func__, ret);

	muic_data->attached_dev = ATTACHED_DEV_NONE_MUIC;

	return ret;
}

static int attach_audiodock(struct s2mu004_muic_data *muic_data,
			muic_attached_dev_t new_dev, u8 vbus)
{
	int ret = 0;

	pr_info("[muic] %s\n", __func__);

	if (!vbus) {
		ret = detach_charger(muic_data);
		if (ret)
			pr_err("[muic] %s err detach_charger(%d)\n",
				__func__, ret);

		ret = com_to_open(muic_data);
		if (ret)
			return ret;

		muic_data->attached_dev = new_dev;

		return ret;
	}

	ret = attach_usb_util(muic_data, new_dev);
	if (ret)
		pr_err("[muic] %s attach_usb_util(%d)\n",
			__func__, ret);

	muic_data->attached_dev = new_dev;

	return ret;
}

static int detach_audiodock(struct s2mu004_muic_data *muic_data)
{
	int ret = 0;

	pr_info("[muic] %s\n", __func__);

	ret = detach_charger(muic_data);
	if (ret)
		pr_err("[muic] %s err detach_charger(%d)\n",
			__func__, ret);

	ret = com_to_open(muic_data);
	if (ret)
		return ret;

	muic_data->attached_dev = ATTACHED_DEV_NONE_MUIC;

	return ret;
}

static int attach_jig_uart_boot_off(struct s2mu004_muic_data *muic_data,
				muic_attached_dev_t new_dev)
{
	struct muic_platform_data *pdata = muic_data->pdata;
	int ret = 0;

	pr_info("[muic] %s(%d)\n",
		__func__, new_dev);

	if (pdata->uart_path == MUIC_PATH_UART_AP)
		ret = switch_to_ap_uart(muic_data);
	else
		ret = switch_to_cp_uart(muic_data);

	ret = attach_charger(muic_data, new_dev);

	return ret;
}

static int detach_jig_uart_boot_off(struct s2mu004_muic_data *muic_data)
{
	int ret = 0;

	pr_info("[muic] %s\n", __func__);

	ret = detach_charger(muic_data);
	if (ret)
		pr_err("[muic] %s err detach_charger(%d)\n", __func__, ret);

	muic_data->attached_dev = ATTACHED_DEV_NONE_MUIC;

	return ret;
}

#if 0
static int attach_jig_uart_boot_on(struct s2mu004_muic_data *muic_data,
				muic_attached_dev_t new_dev)
{
	int ret = 0;

	pr_err("[muic] %s(%d)\n",
		__func__, new_dev);

	ret = set_com_sw(muic_data, MANSW_OPEN);
	if (ret)
		pr_err("[muic] %s set_com_sw err\n", __func__);

	muic_data->attached_dev = ATTACHED_DEV_JIG_UART_ON_MUIC;

	return ret;
}
#endif

static int dettach_jig_uart_boot_on(struct s2mu004_muic_data *muic_data)
{
	pr_info("[muic] %s\n", __func__);

	muic_data->attached_dev = ATTACHED_DEV_NONE_MUIC;

	return 0;
}

static int attach_jig_usb_boot_off(struct s2mu004_muic_data *muic_data,
				u8 vbvolt)
{
	int ret = 0;

	if (muic_data->attached_dev == ATTACHED_DEV_JIG_USB_OFF_MUIC) {
		pr_info("[muic] %s duplicated\n", __func__);
		return ret;
	}

	pr_info("[muic] %s\n", __func__);

	ret = attach_usb_util(muic_data, ATTACHED_DEV_JIG_USB_OFF_MUIC);
	if (ret)
		return ret;

	return ret;
}

static int attach_jig_usb_boot_on(struct s2mu004_muic_data *muic_data,
				u8 vbvolt)
{
	int ret = 0;

	if (muic_data->attached_dev == ATTACHED_DEV_JIG_USB_ON_MUIC) {
		pr_info("[muic] %s duplicated\n", __func__);
		return ret;
	}

	pr_info("[muic] %s\n", __func__);

	ret = attach_usb_util(muic_data, ATTACHED_DEV_JIG_USB_ON_MUIC);
	if (ret)
		return ret;

	return ret;
}

#if defined(CONFIG_SUPPORT_RID_PERIODIC)
static int s2mu004_muic_set_adc_mode(struct s2mu004_muic_data *muic_data, int adc_mode)
{
	struct i2c_client *i2c = muic_data->i2c;
	u8 reg_ctrl1, reg_ctrl3 = 0;
	int ret = 0;
#ifdef	CONFIG_MUIC_SUPPORT_CCIC
	muic_data_t *pmuic = (muic_data_t *)muic_data->ccic_data;

	if (!(pmuic->opmode & OPMODE_CCIC)) {
		pr_info("%s, array : Don't set the periodic mode\n", __func__);
		goto EOS;
	}
#endif

	reg_ctrl1 = s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_CTRL1);
	reg_ctrl3 = s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_CTRL3);

	switch (adc_mode) {
		case S2MU004_ADC_ONESHOT:
			pr_info("%s, Set adc to oneshot.\n", __func__);
			reg_ctrl1 |= (0x01 << 3);
			reg_ctrl3 |= (0x01 << 2);
			break;
		case S2MU004_ADC_PERIODIC:
			pr_info("%s, Set adc to periodic.\n", __func__);
			reg_ctrl1 &= ~(0x01 << 3);
			reg_ctrl3 &= ~(0x01 << 2);
			break;
		default:
			pr_info("%s, Invalid Ctrl.\n", __func__);
			ret = -1;
			goto EOS;
			break;
	}

	s2mu004_i2c_write_byte(i2c, S2MU004_REG_MUIC_CTRL1, reg_ctrl1);
	s2mu004_i2c_write_byte(i2c, S2MU004_REG_MUIC_CTRL3, reg_ctrl3);

EOS:
	return ret;
}
#endif

#if defined(CONFIG_CCIC_S2MU004) || !defined(CONFIG_SEC_FACTORY)
static int s2mu004_muic_get_vbus_state(struct s2mu004_muic_data *muic_data)
{
	struct i2c_client *i2c = muic_data->i2c;
	u8 reg_val = 0;
	int vbus = 0;

	reg_val = s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_DEVICE_APPLE);
	vbus = !!(reg_val & DEV_TYPE_APPLE_VBUS_WAKEUP);
	pr_info("[muic] %s vbus : (%d)\n", __func__, vbus);
	return vbus;
}
#endif

#if defined(CONFIG_CCIC_S2MU004)
static int s2mu004_muic_set_rid_adc_en(struct s2mu004_muic_data *muic_data, bool en)
{
	struct i2c_client *i2c = muic_data->i2c;
	int ret = 0;
	pr_info("[muic] %s rid en : (%d)\n", __func__, en);
	if (en) {
		/* enable rid detection for muic dp dm detect */
		ret = s2mu004_i2c_update_bit(i2c,
				S2MU004_REG_MUIC_RID_CTRL, RID_CTRL_ADC_OFF_MASK, RID_CTRL_ADC_OFF_SHIFT, 0x0);
	} else {
		/* disable rid detection for muic dp dm detect */
		ret = s2mu004_i2c_update_bit(i2c,
				S2MU004_REG_MUIC_RID_CTRL, RID_CTRL_ADC_OFF_MASK, RID_CTRL_ADC_OFF_SHIFT, 0x1);
	}

	return ret;
}

static void s2mu004_muic_set_rid_int_mask_en(struct s2mu004_muic_data *muic_data, bool en)
{
	struct i2c_client *i2c = muic_data->i2c;
	u8 irq_reg[S2MU004_IRQ_GROUP_NR] = {0};

	pr_info("%s en : %d\n", __func__, (int)en);

	if (en) {
		s2mu004_i2c_update_bit(i2c,
			S2MU004_REG_MUIC_INT2_MASK,
			INT_ADC_CHANGE_MASK | INT_RSRV_ATTACH_MASK,
			0, INT_ADC_CHANGE_MASK | INT_RSRV_ATTACH_MASK);
		s2mu004_i2c_update_bit(i2c,
			S2MU004_REG_MUIC_INT1_MASK,
			INT_DETACH_MASK | INT_ATTACH_MASK,
			0, INT_DETACH_MASK | INT_ATTACH_MASK);
	} else {
		s2mu004_bulk_read(i2c, S2MU004_REG_MUIC_INT1,
				S2MU004_NUM_IRQ_MUIC_REGS, &irq_reg[MUIC_INT1]);

		s2mu004_i2c_update_bit(i2c,
				S2MU004_REG_MUIC_INT2_MASK,
				INT_ADC_CHANGE_MASK | INT_RSRV_ATTACH_MASK,
				0, 0);

		s2mu004_i2c_update_bit(i2c,
				S2MU004_REG_MUIC_INT1_MASK,
				INT_DETACH_MASK | INT_ATTACH_MASK,
				0, 0);
	}
}

static int s2mu004_muic_recheck_adc(struct s2mu004_muic_data *muic_data)
{
	int i = 0;
	struct i2c_client *i2c = muic_data->i2c;
	int adc = ADC_OPEN;
	u8 chk_int;

	s2mu004_muic_set_rid_adc_en(muic_data, false);
	usleep_range(10000, 12000);
	s2mu004_read_reg(i2c, 0x6, &chk_int);
	s2mu004_muic_set_rid_adc_en(muic_data, true);
	usleep_range(20000, 21000);

	for (i = 0; i < 50; i++) {
		usleep_range(1000, 1050);
		adc = s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_ADC) & ADC_MASK;
		if (adc != ADC_OPEN) {
			pr_info("%s, %d th try, adc : 0x%x\n", __func__, i, adc);
			return adc;
		}
	}

	usleep_range(10000, 10500);
	pr_info("%s, after delay\n", __func__);
	return s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_ADC) & ADC_MASK;
}

static int s2mu004_muic_refresh_adc(struct s2mu004_muic_data *muic_data)
{
	struct i2c_client *i2c = muic_data->i2c;
	int adc = 0;
	u8 reg_data, b_Rid_en = 0;

	reg_data = s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_RID_CTRL);
	if (!(reg_data & 0x2)) {
		b_Rid_en = 1;
	} else {
		pr_info("%s, enable the RID\n", __func__);
		reg_data &= ~(0x01 << 1);
		s2mu004_i2c_write_byte(i2c, S2MU004_REG_MUIC_RID_CTRL, reg_data);
		msleep(35);
	}

	adc = s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_ADC);
	pr_info("%s, adc : 0x%X\n", __func__, adc);

	if(!b_Rid_en) {
		pr_info("%s, disable the RID\n", __func__);
		reg_data |= (0x01 << 1);
		s2mu004_i2c_write_byte(i2c, S2MU004_REG_MUIC_RID_CTRL, reg_data);
	}
	return adc;
}

#if 0
static int s2mu004_muic_get_adc_mode(struct s2mu004_muic_data *muic_data)
{
	struct i2c_client *i2c = muic_data->i2c;
	u8 reg_ctrl1, reg_ctrl3 = 0;

	reg_ctrl1 = s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_CTRL1);
	reg_ctrl3 = s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_CTRL3);

	if ((reg_ctrl1 & (0x01 << 3)) && (reg_ctrl3 & (0x01 << 2))) {
		pr_info("%s, It's Oneshot mode.\n", __func__);
		return S2MU004_ADC_ONESHOT;
	} else if (!(reg_ctrl1 & (0x01 << 3)) && !(reg_ctrl3 & (0x01 << 2))) {
		pr_info("%s, It's Periodic mode.\n", __func__);
		return S2MU004_ADC_PERIODIC;
	} else {
		return -1;
	}
}

static int s2mu004_muic_get_adc_by_mode(struct s2mu004_muic_data *muic_data)
{
	int adc = 0;
	bool b_periodic = false;

	if (s2mu004_muic_get_adc_mode(muic_data) == S2MU004_ADC_PERIODIC) {
		s2mu004_muic_set_adc_mode(muic_data, S2MU004_ADC_ONESHOT);
		b_periodic = true;
		usleep_range(1000, 1500);
	}
	adc = s2mu004_muic_refresh_adc(muic_data);
	if (b_periodic) {
		s2mu004_muic_set_adc_mode(muic_data, S2MU004_ADC_PERIODIC);
	}

	pr_info("%s, adc : %d, b_periodic? : %d\n", __func__, adc, (int)b_periodic);

	return adc;
}
#endif
#endif
static void s2mu004_muic_handle_attach(struct s2mu004_muic_data *muic_data,
			muic_attached_dev_t new_dev, int adc, u8 vbvolt)
{
	int ret = 0;
	bool noti = (new_dev != muic_data->attached_dev) ? true : false;
#if defined(CONFIG_HV_MUIC_S2MU004_AFC) && !defined(CONFIG_MUIC_S2MU004_NON_USB_C_TYPE)
	muic_data_t *pmuic = (muic_data_t *)muic_data->ccic_data;
#endif	

#if defined(CONFIG_HV_MUIC_S2MU004_AFC)
		if (muic_data->attached_dev == ATTACHED_DEV_HV_ID_ERR_UNDEFINED_MUIC ||
			muic_data->attached_dev == ATTACHED_DEV_HV_ID_ERR_UNSUPPORTED_MUIC ||
			muic_data->attached_dev == ATTACHED_DEV_HV_ID_ERR_SUPPORTED_MUIC)
			return;
#endif /* CONFIG_HV_MUIC_S2MU004_AFC */

#if defined(CONFIG_CCIC_S2MU004)
	if(muic_data->water_status == S2MU004_WATER_MUIC_CCIC_DET) {
		pr_info("[muic] %s : skipped by water detected condition\n", __func__);
		return;
	}
#endif

	muic_data->is_jig_on = false;
	pr_info("[muic] %s : muic_data->attached_dev: %d, new_dev: %d, muic_data->suspended: %d\n",
		__func__, muic_data->attached_dev, new_dev, muic_data->suspended);

	switch (muic_data->attached_dev) {
	case ATTACHED_DEV_USB_MUIC:
	case ATTACHED_DEV_CDP_MUIC:
	case ATTACHED_DEV_JIG_USB_OFF_MUIC:
	case ATTACHED_DEV_JIG_USB_ON_MUIC:
	case ATTACHED_DEV_TIMEOUT_OPEN_MUIC:
#if defined(CONFIG_SEC_FACTORY)
	case ATTACHED_DEV_CARKIT_MUIC:
#endif
		if (new_dev != muic_data->attached_dev) {
			pr_info("[muic] %s new(%d)!=attached(%d)\n",
				__func__, new_dev, muic_data->attached_dev);
			ret = detach_usb(muic_data);
		}
		break;
	case ATTACHED_DEV_OTG_MUIC:
	/* OTG -> LANHUB, meaning TA is attached to LANHUB(OTG) */
		if (new_dev != muic_data->attached_dev) {
			pr_info("[muic] %s new(%d)!=attached(%d)",
				__func__, new_dev, muic_data->attached_dev);
			ret = detach_otg_usb(muic_data);
		}
		break;

	case ATTACHED_DEV_AUDIODOCK_MUIC:
		if (new_dev != muic_data->attached_dev) {
			pr_info("[muic] %s new(%d)!=attached(%d)\n",
				__func__, new_dev, muic_data->attached_dev);
			ret = detach_audiodock(muic_data);
		}
		break;

	case ATTACHED_DEV_TA_MUIC:
	case ATTACHED_DEV_UNDEFINED_CHARGING_MUIC:
#if defined(CONFIG_HV_MUIC_S2MU004_AFC)
		noti = s2mu004_muic_check_change_dev_afc_charger(muic_data, new_dev);
		if (noti) {
			s2mu004_muic_set_afc_ready(muic_data, false);
			muic_data->is_afc_muic_prepare = false;
			s2mu004_hv_muic_reset_hvcontrol_reg(muic_data);
		}
#endif
		if (new_dev != muic_data->attached_dev) {
			pr_info("[muic] %s new(%d)!=attached(%d)\n",
				__func__, new_dev, muic_data->attached_dev);
			ret = detach_charger(muic_data);
		}
		break;

	case ATTACHED_DEV_JIG_UART_OFF_VB_OTG_MUIC:
	case ATTACHED_DEV_JIG_UART_OFF_VB_FG_MUIC:
	case ATTACHED_DEV_JIG_UART_OFF_VB_MUIC:
	case ATTACHED_DEV_JIG_UART_OFF_MUIC:
		if (new_dev != ATTACHED_DEV_JIG_UART_OFF_MUIC) {
			pr_info("[muic] %s new(%d)!=attached(%d)\n",
				__func__, new_dev, muic_data->attached_dev);
			ret = detach_jig_uart_boot_off(muic_data);
		}
		break;

	case ATTACHED_DEV_JIG_UART_ON_MUIC:
	case ATTACHED_DEV_JIG_UART_ON_VB_MUIC:
		if (new_dev != muic_data->attached_dev) {
			pr_info("[muic] %s new(%d)!=attached(%d)\n",
				__func__, new_dev, muic_data->attached_dev);
			ret = detach_jig_uart_boot_off(muic_data);
		}
		break;
	case ATTACHED_DEV_DESKDOCK_MUIC:
		if (new_dev != muic_data->attached_dev) {
			pr_info("[muic] %s new(%d)!=attached(%d)\n",
				__func__, new_dev, muic_data->attached_dev);

			if (muic_data->is_factory_start)
				ret = detach_deskdock(muic_data);
			else {
				noti = false;
				ret = dettach_jig_uart_boot_on(muic_data);
			}
		}
		break;
#if defined(CONFIG_HV_MUIC_S2MU004_AFC)
	case ATTACHED_DEV_AFC_CHARGER_PREPARE_MUIC:
	case ATTACHED_DEV_AFC_CHARGER_PREPARE_DUPLI_MUIC:
	case ATTACHED_DEV_AFC_CHARGER_5V_MUIC:
	case ATTACHED_DEV_AFC_CHARGER_5V_DUPLI_MUIC:
	case ATTACHED_DEV_AFC_CHARGER_9V_MUIC:
	case ATTACHED_DEV_AFC_CHARGER_ERR_V_MUIC:
	case ATTACHED_DEV_AFC_CHARGER_ERR_V_DUPLI_MUIC:
	case ATTACHED_DEV_QC_CHARGER_PREPARE_MUIC:
	case ATTACHED_DEV_QC_CHARGER_5V_MUIC:
	case ATTACHED_DEV_QC_CHARGER_9V_MUIC:
		noti = s2mu004_muic_check_change_dev_afc_charger(muic_data, new_dev);
		if (noti) {
			s2mu004_muic_set_afc_ready(muic_data, false);
			muic_data->is_afc_muic_prepare = false;
			s2mu004_hv_muic_reset_hvcontrol_reg(muic_data);
		}
#endif
	default:
		break;
	}

#if defined(CONFIG_MUIC_NOTIFIER)
	if (noti) {
		if (!muic_data->suspended)
			muic_notifier_detach_attached_dev(muic_data->attached_dev);
		else
			muic_data->need_to_noti = true;
	}
#endif /* CONFIG_MUIC_NOTIFIER */

	switch (new_dev) {
	case ATTACHED_DEV_USB_MUIC:
	case ATTACHED_DEV_CDP_MUIC:
	case ATTACHED_DEV_TIMEOUT_OPEN_MUIC:
#if defined(CONFIG_SEC_FACTORY)
	case ATTACHED_DEV_CARKIT_MUIC:
		if (new_dev == ATTACHED_DEV_CARKIT_MUIC)
			muic_data->is_jig_on = true;
#endif
		ret = attach_usb(muic_data, new_dev);
		break;
	case ATTACHED_DEV_OTG_MUIC:
		ret = attach_otg_usb(muic_data, new_dev);
		break;
	case ATTACHED_DEV_AUDIODOCK_MUIC:
		ret = attach_audiodock(muic_data, new_dev, vbvolt);
		break;
	case ATTACHED_DEV_TA_MUIC:
	case ATTACHED_DEV_UNDEFINED_CHARGING_MUIC:
	case ATTACHED_DEV_UNSUPPORTED_ID_VB_MUIC:
#if defined(CONFIG_HV_MUIC_S2MU004_AFC)
		if (muic_data->pdata->afc_disable)
			pr_info("%s:%s AFC Disable(%d) by USER!\n", MUIC_DEV_NAME,
				__func__, muic_data->pdata->afc_disable);
		else {
#if defined(CONFIG_MUIC_S2MU004_NON_USB_C_TYPE)
			if (muic_data->is_afc_muic_ready == false && muic_data->afc_check)
#else
			if (muic_data->is_afc_muic_ready == false && muic_data->afc_check
					&& pmuic->is_afc_pdic_ready == true)

#endif
				s2mu004_muic_prepare_afc_charger(muic_data);
		}
#endif
		com_to_open_with_vbus(muic_data);
		ret = attach_charger(muic_data, new_dev);
		break;
	case ATTACHED_DEV_JIG_UART_OFF_VB_OTG_MUIC:
	case ATTACHED_DEV_JIG_UART_OFF_VB_FG_MUIC:
	case ATTACHED_DEV_JIG_UART_OFF_VB_MUIC:
	case ATTACHED_DEV_JIG_UART_OFF_MUIC:
	case ATTACHED_DEV_JIG_UART_ON_MUIC:
	case ATTACHED_DEV_JIG_UART_ON_VB_MUIC:
		muic_data->is_jig_on = true;
		ret = attach_jig_uart_boot_off(muic_data, new_dev);
		break;
	case ATTACHED_DEV_JIG_USB_OFF_MUIC:
		muic_data->is_jig_on = true;
		ret = attach_jig_usb_boot_off(muic_data, vbvolt);
		break;
	case ATTACHED_DEV_JIG_USB_ON_MUIC:
		muic_data->is_jig_on = true;
		ret = attach_jig_usb_boot_on(muic_data, vbvolt);
		break;
	case ATTACHED_DEV_DESKDOCK_MUIC:
		ret = attach_deskdock(muic_data, new_dev, vbvolt);
		break;
	case ATTACHED_DEV_UNKNOWN_MUIC:
		com_to_open_with_vbus(muic_data);
		ret = attach_charger(muic_data, new_dev);
		break;
	case ATTACHED_DEV_TYPE3_MUIC:
		muic_data->attached_dev = new_dev;
		break;
	case ATTACHED_DEV_UNDEFINED_RANGE_MUIC:
		muic_data->attached_dev = new_dev;
		break;
	default:
		noti = false;
		pr_info("[muic] %s unsupported dev=%d, adc=0x%x, vbus=%c\n",
			__func__, new_dev, adc, (vbvolt ? 'O' : 'X'));
		break;
	}

/* TODO: There is no needs to use JIGB pin by MUIC if CCIC is supported */
#if !defined(CONFIG_CCIC_S2MU004)
#if !defined(CONFIG_MUIC_S2MU004_ENABLE_AUTOSW)
	ret = s2mu004_muic_jig_on(muic_data);
#endif
#endif

	if (ret)
		pr_err("[muic] %s something wrong %d (ERR=%d)\n",
			__func__, new_dev, ret);

	pr_info("%s:%s done\n", MFD_DEV_NAME, __func__);

#if defined(CONFIG_HV_MUIC_S2MU004_AFC)
	new_dev = hv_muic_check_id_err(muic_data, new_dev);
#endif

#if defined(CONFIG_MUIC_NOTIFIER)
	if (noti) {
		if (!muic_data->suspended)
			muic_notifier_attach_attached_dev(new_dev);
		else
			muic_data->need_to_noti = true;
	}
#endif /* CONFIG_MUIC_NOTIFIER */

}

static void s2mu004_muic_handle_detach(struct s2mu004_muic_data *muic_data)
{
	int ret = 0;
	bool noti = true;

#if defined(CONFIG_HV_MUIC_S2MU004_AFC)
		s2mu004_muic_set_afc_ready(muic_data, false);
		muic_data->is_afc_muic_prepare = false;
#endif

	ret = com_to_open(muic_data);

	switch (muic_data->attached_dev) {
	case ATTACHED_DEV_JIG_USB_OFF_MUIC:
	case ATTACHED_DEV_JIG_USB_ON_MUIC:
	case ATTACHED_DEV_USB_MUIC:
	case ATTACHED_DEV_CDP_MUIC:
	case ATTACHED_DEV_TIMEOUT_OPEN_MUIC:
#if defined(CONFIG_SEC_FACTORY)
	case ATTACHED_DEV_CARKIT_MUIC:
#endif
		ret = detach_usb(muic_data);
		break;
	case ATTACHED_DEV_OTG_MUIC:
		ret = detach_otg_usb(muic_data);
		break;
	case ATTACHED_DEV_TA_MUIC:
	case ATTACHED_DEV_UNDEFINED_CHARGING_MUIC:
		ret = detach_charger(muic_data);
#if defined(CONFIG_HV_MUIC_S2MU004_AFC)
		s2mu004_hv_muic_reset_hvcontrol_reg(muic_data);
#endif
		break;
	case ATTACHED_DEV_JIG_UART_OFF_VB_OTG_MUIC:
	case ATTACHED_DEV_JIG_UART_OFF_VB_FG_MUIC:
	case ATTACHED_DEV_JIG_UART_OFF_VB_MUIC:
	case ATTACHED_DEV_JIG_UART_OFF_MUIC:
	case ATTACHED_DEV_JIG_UART_ON_MUIC:
	case ATTACHED_DEV_JIG_UART_ON_VB_MUIC:
		ret = detach_jig_uart_boot_off(muic_data);
		break;
	case ATTACHED_DEV_DESKDOCK_MUIC:
		if (muic_data->is_factory_start)
			ret = detach_deskdock(muic_data);
		else {
			noti = false;
			ret = dettach_jig_uart_boot_on(muic_data);
		}
		break;
	case ATTACHED_DEV_AUDIODOCK_MUIC:
		ret = detach_audiodock(muic_data);
		break;
	case ATTACHED_DEV_NONE_MUIC:
		pr_info("[muic] %s duplicated(NONE)\n", __func__);
		break;
	case ATTACHED_DEV_UNKNOWN_MUIC:
	case ATTACHED_DEV_UNDEFINED_RANGE_MUIC:
	case ATTACHED_DEV_UNSUPPORTED_ID_VB_MUIC:
		pr_info("[muic] %s UNKNOWN\n", __func__);
		ret = detach_charger(muic_data);
		muic_data->attached_dev = ATTACHED_DEV_NONE_MUIC;
		break;
#if defined(CONFIG_HV_MUIC_S2MU004_AFC)
	case ATTACHED_DEV_AFC_CHARGER_PREPARE_MUIC:
	case ATTACHED_DEV_AFC_CHARGER_PREPARE_DUPLI_MUIC:
	case ATTACHED_DEV_AFC_CHARGER_5V_MUIC:
	case ATTACHED_DEV_AFC_CHARGER_5V_DUPLI_MUIC:
	case ATTACHED_DEV_AFC_CHARGER_9V_MUIC:
	case ATTACHED_DEV_QC_CHARGER_5V_MUIC:
	case ATTACHED_DEV_QC_CHARGER_9V_MUIC:
	case ATTACHED_DEV_QC_CHARGER_PREPARE_MUIC:
	case ATTACHED_DEV_AFC_CHARGER_ERR_V_MUIC:
	case ATTACHED_DEV_AFC_CHARGER_ERR_V_DUPLI_MUIC:
		ret = detach_charger(muic_data);
		s2mu004_hv_muic_reset_hvcontrol_reg(muic_data);
		break;
#endif
	default:
		noti = false;
		pr_info("[muic] %s invalid type(%d)\n",
			__func__, muic_data->attached_dev);
		muic_data->attached_dev = ATTACHED_DEV_NONE_MUIC;
		break;
	}
	if (ret)
		pr_err("[muic] %s something wrong %d (ERR=%d)\n",
			__func__, muic_data->attached_dev, ret);

#ifndef CONFIG_CCIC_S2MU004
	muic_data->bcd_rescan_cnt = 0;
#endif

#if defined(CONFIG_MUIC_NOTIFIER)
	if (noti) {
		if (!muic_data->suspended)
			muic_notifier_detach_attached_dev(muic_data->attached_dev);
		else
			muic_data->need_to_noti = true;
	}
#endif /* CONFIG_MUIC_NOTIFIER */
}

static void s2mu004_muic_detect_dev(struct s2mu004_muic_data *muic_data)
{
	struct i2c_client *i2c = muic_data->i2c;
	muic_attached_dev_t new_dev = ATTACHED_DEV_UNKNOWN_MUIC;
#ifdef	CONFIG_MUIC_SUPPORT_CCIC
	muic_data_t *pmuic = (muic_data_t *)muic_data->ccic_data;
#endif
#if defined(CONFIG_CCIC_S2MU004) && !defined(CONFIG_SEC_FACTORY)
	struct otg_notify *o_notify = get_otg_notify();
#endif
	int intr = MUIC_INTR_DETACH;
	int vbvolt = 0, vmid = 0;
	int val1 = 0, val2 = 0, val3 = 0, val4 = 0, adc = 0;
	int val5 = 0, val6 = 0, val7 = 0;
#if defined(CONFIG_CCIC_S2MU004)
	int data = 0, check_adc = 0;
#endif

	val1 = s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_DEVICE_TYPE1);
	val2 = s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_DEVICE_TYPE2);
	val3 = s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_DEVICE_TYPE3);
	val4 = s2mu004_i2c_read_byte(i2c, S2MU004_REG_REV_ID);
	adc = s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_ADC);
	val5 = s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_DEVICE_APPLE);
	val6 = s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_CHG_TYPE);
	val7 = s2mu004_i2c_read_byte(i2c, S2MU004_REG_SC_STATUS2);

	vbvolt = !!(val5 & DEV_TYPE_APPLE_VBUS_WAKEUP);
	vmid = !!(val7 & 0x7);

	pr_info("[muic] dev[1:0x%x, 2:0x%x, 3:0x%x]\n"
		", adc:0x%x, vbvolt:0x%x, apple:0x%x, chg_type:0x%x, vmid:0x%x, dev_id:0x%x\n",
		val1, val2, val3, adc, vbvolt, val5, val6, vmid, val4);
	pr_info("%s:%s\n", MFD_DEV_NAME, __func__);

	if (ADC_CONVERSION_ERR_MASK & adc) {
		pr_info("[muic] ADC conversion error!\n");
		return ;
	}
#if defined(CONFIG_CCIC_S2MU004)
	if (muic_data->attach_mode == S2MU004_MUIC_DETACH) {
		/* FIXME: for VB on-off case */
		if ((adc == 0) && muic_data->jig_state) {
#if defined(CONFIG_VBUS_NOTIFIER)
			vbus_notifier_handle((!!vbvolt) ? STATUS_VBUS_HIGH : STATUS_VBUS_LOW);
#endif /* CONFIG_VBUS_NOTIFIER */
			muic_data->attach_mode = S2MU004_FIRST_ATTACH;

			if (vbvolt) {
				if (muic_data->attached_dev == ATTACHED_DEV_JIG_UART_OFF_VB_MUIC ||
					muic_data->attached_dev == ATTACHED_DEV_JIG_UART_OFF_MUIC) {
					intr = MUIC_INTR_ATTACH;
					new_dev = ATTACHED_DEV_JIG_UART_OFF_VB_MUIC;
					pr_info("[muic] MUIC JIG UART OFF VB OFF\n");
					goto jig;
				} else if (muic_data->attached_dev == ATTACHED_DEV_JIG_UART_ON_VB_MUIC ||
					muic_data->attached_dev == ATTACHED_DEV_JIG_UART_ON_MUIC) {
					intr = MUIC_INTR_ATTACH;
					new_dev = ATTACHED_DEV_JIG_UART_ON_VB_MUIC;
					pr_info("[muic] MUIC JIG UART ON VB OFF\n");
					goto jig;
				} else
					return;
			} else {
				if (muic_data->attached_dev == ATTACHED_DEV_JIG_UART_OFF_VB_MUIC ||
					muic_data->attached_dev == ATTACHED_DEV_JIG_UART_OFF_MUIC) {
					intr = MUIC_INTR_ATTACH;
					new_dev = ATTACHED_DEV_JIG_UART_OFF_MUIC;
					pr_info("[muic] MUIC JIG UART OFF VB OFF\n");
					goto jig;
				} else if (muic_data->attached_dev == ATTACHED_DEV_JIG_UART_ON_VB_MUIC ||
					muic_data->attached_dev == ATTACHED_DEV_JIG_UART_ON_MUIC) {
					intr = MUIC_INTR_ATTACH;
					new_dev = ATTACHED_DEV_JIG_UART_ON_MUIC;
					pr_info("[muic] MUIC JIG UART ON VB OFF\n");
					goto jig;
				} else
					return;
			}
		} else {
			muic_data->jig_state = false;
			muic_data->re_detect = 0;
			muic_data->afc_check = false;
#if defined(CONFIG_SUPPORT_RID_PERIODIC)
			s2mu004_muic_set_adc_mode(muic_data, S2MU004_ADC_ONESHOT);

			msleep(10);
#endif
			data = s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_RID_CTRL);
			data &= ~(0x01 << 1);
			s2mu004_i2c_write_byte(i2c, S2MU004_REG_MUIC_RID_CTRL, data);

			msleep(100);

			check_adc = s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_ADC);
#if defined(CONFIG_SUPPORT_RID_PERIODIC)
			s2mu004_muic_set_adc_mode(muic_data, S2MU004_ADC_PERIODIC);
#endif
			pr_info("%s : check adc at detach (%x)\n", __func__, check_adc);

			if (check_adc == 0) {
				muic_data->attach_mode = S2MU004_FIRST_ATTACH;
				muic_pdic_notifier_attach_attached_dev(ATTACHED_DEV_TYPE3_MUIC);
				if (vbvolt) {
					pr_info("%s :[muic] change mode to second attach!\n", __func__);
					pmuic->is_dcdtmr_intr = true;
					schedule_delayed_work(&muic_data->dcd_recheck, 0);

					static_data->attach_mode = S2MU004_SECOND_ATTACH;
				}
			} else {
				muic_data->attach_mode = S2MU004_NONE_CABLE;
				muic_pdic_notifier_detach_attached_dev(muic_data->attached_dev);
			}

			if (muic_data->otg_state) {
				muic_data->otg_state = false;
#if !defined(CONFIG_SEC_FACTORY)
				if (check_adc == 0 && is_blocked(o_notify, NOTIFY_BLOCK_TYPE_HOST))
					return;
#endif
			}
		}
	} else if (muic_data->attach_mode == S2MU004_NONE_CABLE) {
		if (adc == ADC_JIG_UART_OFF) {
			pr_info("ADC_JIG_UART_OFF\n");
			intr = MUIC_INTR_ATTACH;
			if (vbvolt)
				new_dev = ATTACHED_DEV_JIG_UART_OFF_VB_MUIC;
			else
				new_dev = ATTACHED_DEV_JIG_UART_OFF_MUIC;
			muic_data->jig_state = true;
		} else if (adc == ADC_JIG_USB_ON || adc == ADC_JIG_USB_OFF ||
			adc == ADC_DESKDOCK) {
			intr = MUIC_INTR_ATTACH;
			new_dev = ATTACHED_DEV_JIG_USB_ON_MUIC;
			pr_info("[muic] ADC JIG_USB_ON DETECTED\n");
			muic_data->jig_state = true;
#if defined(CONFIG_SEC_FACTORY)
		} else if (adc == ADC_CEA936ATYPE1_CHG) {
			if (!vbvolt)
				return;
			else {
				intr = MUIC_INTR_ATTACH;
				new_dev = ATTACHED_DEV_CARKIT_MUIC;
				pr_info("[muic] SMD DL 255k 200k charger disable\n");
			}
			muic_data->jig_state = true;
#endif

#if defined(CONFIG_NEW_SEC_FACTORY)
		} else if ((pmuic->opmode & OPMODE_CCIC) && (((adc & 0x1f) == 0) || vbvolt)) {
#else
		} else if (((adc & 0x1f) == 0) || vbvolt) {
#endif		
			pr_info("%s :[muic] change mode to first attach!\n", __func__);
			intr = MUIC_INTR_ATTACH;
			new_dev = ATTACHED_DEV_TYPE3_MUIC;
			muic_data->attach_mode = S2MU004_FIRST_ATTACH;
			muic_pdic_notifier_attach_attached_dev(ATTACHED_DEV_TYPE3_MUIC);
			if (vbvolt) {

				/* FIXME: for VB on-off case */
				if (muic_data->jig_state == true) {
					pr_info("%s : not enter second attach for jig\n", __func__);
					if (muic_data->attached_dev == ATTACHED_DEV_JIG_UART_OFF_MUIC) {
						intr = MUIC_INTR_ATTACH;
						new_dev = ATTACHED_DEV_JIG_UART_OFF_VB_MUIC;
						pr_info("[muic] MUIC JIG UART OFF VB ON\n");
						goto jig;
					} else if (muic_data->attached_dev == ATTACHED_DEV_JIG_UART_ON_MUIC) {
						intr = MUIC_INTR_ATTACH;
						new_dev = ATTACHED_DEV_JIG_UART_ON_VB_MUIC;
						pr_info("[muic] MUIC JIG UART ON VB ON\n");
						goto jig;
					} else
						return;
				}

				pr_info("%s :[muic] change mode to second attach!\n", __func__);
				/* disable rid detection for muic dp dm detect */
				data = s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_RID_CTRL);
				data |= (0x01 << 1);
				s2mu004_i2c_write_byte(i2c, S2MU004_REG_MUIC_RID_CTRL, data);

				msleep(100);

				/* start secondary dp dm detect */
				data = s2mu004_i2c_read_byte(i2c, 0x6a);
				data |= 0x01;
				s2mu004_i2c_write_byte(i2c, 0x6a, data);

				static_data->attach_mode = S2MU004_SECOND_ATTACH;
			}
			return;
		}
	} else if (muic_data->attach_mode == S2MU004_FIRST_ATTACH) {
		if (vbvolt) {
			/* FIXME: for VB on-off case */
			if (muic_data->jig_state == true) {
				pr_info("%s : not enter second attach for jig\n", __func__);
				if (muic_data->attached_dev == ATTACHED_DEV_JIG_UART_OFF_MUIC) {
					intr = MUIC_INTR_ATTACH;
					new_dev = ATTACHED_DEV_JIG_UART_OFF_VB_MUIC;
					pr_info("[muic] MUIC JIG UART OFF VB ON\n");
					goto jig;
				} else if (muic_data->attached_dev == ATTACHED_DEV_JIG_UART_ON_MUIC) {
					intr = MUIC_INTR_ATTACH;
					new_dev = ATTACHED_DEV_JIG_UART_ON_VB_MUIC;
					pr_info("[muic] MUIC JIG UART ON VB ON\n");
					goto jig;
				} else
					return;
			}

			pr_info("%s :[muic] change mode to second attach!\n", __func__);

			muic_pdic_notifier_attach_attached_dev(ATTACHED_DEV_TYPE3_MUIC);

			/* disable rid detection for muic dp dm detect */
			data = s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_RID_CTRL);
			data |= (0x01 << 1);
			s2mu004_i2c_write_byte(i2c, S2MU004_REG_MUIC_RID_CTRL, data);

			msleep(100);

			/* start secondary dp dm detect */
			data = s2mu004_i2c_read_byte(i2c, 0x6a);
			data |= 0x01;
			s2mu004_i2c_write_byte(i2c, 0x6a, data);

			static_data->attach_mode = S2MU004_SECOND_ATTACH;
		}
		return;
	} else if (!vbvolt)
		return;

	if (muic_data->attach_mode == S2MU004_MUIC_OTG && vbvolt) {
#if defined(CONFIG_VBUS_NOTIFIER)
		vbus_notifier_handle((!!vbvolt) ? STATUS_VBUS_HIGH : STATUS_VBUS_LOW);
#endif /* CONFIG_VBUS_NOTIFIER */
		return;
	}

#endif
	/* Attached */
	switch (val1) {
	case DEV_TYPE1_CDP:
		if (vbvolt) {
			intr = MUIC_INTR_ATTACH;
			new_dev = ATTACHED_DEV_CDP_MUIC;
			pr_info("[muic] USB_CDP DETECTED\n");
		}
		break;
	case DEV_TYPE1_USB:
		if (vbvolt) {
			intr = MUIC_INTR_ATTACH;
			new_dev = ATTACHED_DEV_USB_MUIC;
#ifdef CONFIG_MUIC_SUPPORT_CCIC
			pr_info("[muic] USB DETECTED\n");
			pmuic->is_dcdtmr_intr = true;
			schedule_delayed_work(&muic_data->dcd_recheck, 0);
#else
			if(muic_data->bcd_rescan_cnt++ < MAX_BCD_RESCAN_CNT) {
				schedule_delayed_work(&muic_data->dcd_recheck, msecs_to_jiffies(10));
				return;
			}
			else
				intr = MUIC_INTR_ATTACH;
			pr_info("[muic] USB DETECTED\n");
#endif
		}
		break;
	case DEV_TYPE1_DEDICATED_CHG:
	case 0x44:
	case 0x60:
		if (vbvolt) {
			intr = MUIC_INTR_ATTACH;
			new_dev = ATTACHED_DEV_TA_MUIC;
			muic_data->afc_check = true;
			pr_info("[muic] DEDICATED CHARGER DETECTED\n");
		}
		break;
#ifdef CONFIG_MUIC_S2MU004_NON_USB_C_TYPE
	case DEV_TYPE1_USB_OTG:
		intr = MUIC_INTR_ATTACH;
		new_dev = ATTACHED_DEV_OTG_MUIC;
		pr_info("[muic] USB_OTG DETECTED\n");
		break;
#endif /* CONFIG_MUIC_S2MU004_NON_USB_C_TYPE */
	case DEV_TYPE1_T1_T2_CHG:
		if (vbvolt) {
			intr = MUIC_INTR_ATTACH;
			/* 200K, 442K should be checkef */
			if (ADC_CEA936ATYPE2_CHG == adc) {
				new_dev = ATTACHED_DEV_UNDEFINED_RANGE_MUIC;
				pr_info("[muic] CEA936ATYPE2_CHG DETECTED\n");
				muic_data->afc_check = false;
			} else {
				new_dev = ATTACHED_DEV_USB_MUIC;
				pr_info("[muic] T1_T2_CHG DETECTED\n");
			}
		}
		break;
	default:
		break;
	}

	switch (val2) {
	case DEV_TYPE2_SDP_1P8S:
		if (vbvolt) {
			intr = MUIC_INTR_ATTACH;
			new_dev = ATTACHED_DEV_TIMEOUT_OPEN_MUIC;
#ifdef CONFIG_MUIC_SUPPORT_CCIC
			pr_info("%s:%s: SDP_1P8S DETECTED\n", MUIC_DEV_NAME, __func__);
			pmuic->is_dcdtmr_intr = true;
			schedule_delayed_work(&muic_data->dcd_recheck, 0);
#else
			if(muic_data->bcd_rescan_cnt++ < MAX_BCD_RESCAN_CNT) {			
				schedule_delayed_work(&muic_data->dcd_recheck, msecs_to_jiffies(10));
				return;
			}
			else
				intr = MUIC_INTR_ATTACH;
			pr_info("%s:%s: SDP_1P8S DETECTED\n", MUIC_DEV_NAME, __func__);
#endif
		}
		break;
	default:
		break;
	}

#ifdef CONFIG_MUIC_S2MU004_NON_USB_C_TYPE
	pr_info("%s:%s s2mu004 non usb-c type\n", MFD_DEV_NAME, __func__);
	switch (val2) {
	case DEV_TYPE2_JIG_UART_OFF:
		intr = MUIC_INTR_ATTACH;
		if (muic_data->is_otg_test) {
			mdelay(100);
			val7 = s2mu004_i2c_read_byte(i2c, S2MU004_REG_SC_STATUS2);
			vmid = val7 & 0x7;
			if (vmid == 0x4) {
				pr_info("[muic] OTG_TEST DETECTED, vmid = %d\n", vmid);
				vbvolt = 1;
				new_dev = ATTACHED_DEV_JIG_UART_OFF_VB_OTG_MUIC;
			} else
				new_dev = ATTACHED_DEV_JIG_UART_OFF_MUIC;
		} else if (vbvolt)
			new_dev = ATTACHED_DEV_JIG_UART_OFF_VB_MUIC;
		else
			new_dev = ATTACHED_DEV_JIG_UART_OFF_MUIC;
		pr_info("[muic] JIG_UART_OFF DETECTED\n");
		break;
	case DEV_TYPE2_JIG_USB_OFF:
		if (!vbvolt)
			break;
		intr = MUIC_INTR_ATTACH;
		new_dev = ATTACHED_DEV_JIG_USB_OFF_MUIC;
		pr_info("[muic] JIG_USB_OFF DETECTED\n");
		break;
	case DEV_TYPE2_JIG_USB_ON:
		if (!vbvolt)
			break;
		intr = MUIC_INTR_ATTACH;
		new_dev = ATTACHED_DEV_JIG_USB_ON_MUIC;
		pr_info("[muic] JIG_USB_ON DETECTED\n");
		break;

	case DEV_TYPE2_JIG_UART_ON:
		if (new_dev != ATTACHED_DEV_JIG_UART_ON_MUIC) {
			intr = MUIC_INTR_ATTACH;
			if (!vbvolt) {
				new_dev = ATTACHED_DEV_JIG_UART_ON_MUIC;
				pr_info("[muic] ADC JIG_UART_ON DETECTED\n");
			}
			else {
				new_dev = ATTACHED_DEV_JIG_UART_ON_VB_MUIC;
				pr_info("[muic] ADC JIG_UART_ON_VB DETECTED\n");
			}
		}
		break;
	default:
		break;
	}
#endif /* CONFIG_MUIC_S2MU004_NON_C_TYPE */

	/* This is for Apple cables */
	if (vbvolt && ((val5 & DEV_TYPE_APPLE_APPLE2P4A_CHG) || (val5 & DEV_TYPE_APPLE_APPLE2A_CHG) ||
		(val5 & DEV_TYPE_APPLE_APPLE1A_CHG) || (val5 & DEV_TYPE_APPLE_APPLE0P5A_CHG))) {
		intr = MUIC_INTR_ATTACH;
		new_dev = ATTACHED_DEV_TA_MUIC;
		muic_data->afc_check = false;
		pr_info("[muic] APPLE_CHG DETECTED\n");
#ifdef CONFIG_MUIC_SUPPORT_CCIC
		pmuic->is_dcdtmr_intr = true;
		schedule_delayed_work(&muic_data->dcd_recheck, 0);
#endif
	}

	if ((val6 & DEV_TYPE_CHG_TYPE) &&
		(new_dev == ATTACHED_DEV_UNKNOWN_MUIC)) {
		intr = MUIC_INTR_ATTACH;
		new_dev = ATTACHED_DEV_TA_MUIC;
		muic_data->afc_check = false;
		pr_info("[muic] CHG_TYPE DETECTED\n");
#ifdef CONFIG_MUIC_SUPPORT_CCIC
		pmuic->is_dcdtmr_intr = true;
		schedule_delayed_work(&muic_data->dcd_recheck, 0);
#endif
	}

#ifdef CONFIG_MUIC_S2MU004_NON_USB_C_TYPE
	if (new_dev == ATTACHED_DEV_UNKNOWN_MUIC) {
		switch (adc) {
		case ADC_CEA936ATYPE1_CHG: /*200k ohm */
			if (vbvolt) {
				intr = MUIC_INTR_ATTACH;
				/* This is workaournd for LG USB cable
						which has 219k ohm ID */
				new_dev = ATTACHED_DEV_USB_MUIC;
				pr_info("[muic] TYPE1 CHARGER DETECTED(USB)\n");
			}
			break;
		case ADC_CEA936ATYPE2_CHG:
			if (vbvolt) {
				intr = MUIC_INTR_ATTACH;
				new_dev = ATTACHED_DEV_UNDEFINED_RANGE_MUIC;
				muic_data->afc_check = false;
				pr_info("[muic] %s unsupported ADC(0x%02x)\n",
				__func__, adc);
			}
			break;
		case ADC_JIG_USB_OFF: /* 255k */
			if (!vbvolt) {
				intr = MUIC_INTR_DETACH;
				new_dev = ATTACHED_DEV_UNKNOWN_MUIC;
				pr_info("[muic] Unsupported->Discarded.\n");
				break;
			}
			if (new_dev != ATTACHED_DEV_JIG_USB_OFF_MUIC) {
				intr = MUIC_INTR_ATTACH;
				new_dev = ATTACHED_DEV_JIG_USB_OFF_MUIC;
				pr_info("[muic] ADC JIG_USB_OFF DETECTED\n");
			}
			break;
		case ADC_JIG_USB_ON:
			if (!vbvolt)
				break;
			if (new_dev != ATTACHED_DEV_JIG_USB_ON_MUIC) {
				intr = MUIC_INTR_ATTACH;
				new_dev = ATTACHED_DEV_JIG_USB_ON_MUIC;
				pr_info("[muic] ADC JIG_USB_ON DETECTED\n");
			}
			break;
		case ADC_JIG_UART_OFF:
			intr = MUIC_INTR_ATTACH;
			if (muic_data->is_otg_test) {
				mdelay(100);
				val7 = s2mu004_i2c_read_byte(i2c, S2MU004_REG_SC_STATUS2);
				vmid = val7 & 0x7;
				if (vmid == 0x4) {
					pr_info("[muic] OTG_TEST DETECTED, vmid = %d\n", vmid);
					vbvolt = 1;
					new_dev = ATTACHED_DEV_JIG_UART_OFF_VB_OTG_MUIC;
				} else
					new_dev = ATTACHED_DEV_JIG_UART_OFF_MUIC;
			} else if (vbvolt)
				new_dev = ATTACHED_DEV_JIG_UART_OFF_VB_MUIC;
			else
				new_dev = ATTACHED_DEV_JIG_UART_OFF_MUIC;

			pr_info("[muic] ADC JIG_UART_OFF DETECTED\n");
			break;
		case ADC_JIG_UART_ON:
			if (new_dev != ATTACHED_DEV_JIG_UART_ON_MUIC) {
				intr = MUIC_INTR_ATTACH;
				if (!vbvolt) {
					new_dev = ATTACHED_DEV_JIG_UART_ON_MUIC;
					pr_info("[muic] ADC JIG_UART_ON DETECTED\n");
				}
				else {
					new_dev = ATTACHED_DEV_JIG_UART_ON_VB_MUIC;
					pr_info("[muic] ADC JIG_UART_ON_VB DETECTED\n");
				}
			}
			break;
		case ADC_SMARTDOCK: /* 0x10000 40.2K ohm */
			/* SMARTDOCK is not supported */
			/* force not to charge the device with SMARTDOCK */
			if(vbvolt) {
				intr = MUIC_INTR_ATTACH;
				new_dev = ATTACHED_DEV_UNSUPPORTED_ID_VB_MUIC;
				pr_info("[muic] %s unsupported ADC(0x%02x) but charging\n",
					__func__, adc);
			}
			else {
				intr = MUIC_INTR_DETACH;
				new_dev = ATTACHED_DEV_UNKNOWN_MUIC;
				pr_info("[muic] Unsupported->Discarded.\n");
			}
			break;
		case ADC_HMT: /* 0x10001 49.9K ohm */
			if(vbvolt) {
				intr = MUIC_INTR_ATTACH;
				new_dev = ATTACHED_DEV_UNDEFINED_RANGE_MUIC;
				pr_info("[muic] %s unsupported ADC(0x%02x)\n",	__func__, adc);
			}
			else {
				intr = MUIC_INTR_DETACH;
				new_dev = ATTACHED_DEV_UNKNOWN_MUIC;
				pr_info("[muic] Unsupported->Discarded.\n");
			}
			break;
		case ADC_AUDIODOCK:
#ifdef CONFIG_MUIC_S2MU004_SUPPORT_AUDIODOCK
			intr = MUIC_INTR_ATTACH;
			new_dev = ATTACHED_DEV_AUDIODOCK_MUIC;
			pr_info("[muic] ADC AUDIODOCK DETECTED\n");
#else
			if(vbvolt) {
				intr = MUIC_INTR_ATTACH;
				new_dev = ATTACHED_DEV_UNDEFINED_RANGE_MUIC;
				pr_info("[muic] ADC AUDIODOCK DETECTED but not supported.\n");
			}
			else {
				intr = MUIC_INTR_DETACH;
				new_dev = ATTACHED_DEV_UNKNOWN_MUIC;
				pr_info("[muic] Unsupported->Discarded.\n");
			}
#endif
			break;
		case ADC_UNIVERSAL_MMDOCK :
			if(vbvolt) {
				intr = MUIC_INTR_ATTACH;
				new_dev = ATTACHED_DEV_UNDEFINED_RANGE_MUIC;
				pr_info("[muic] %s unsupported ADC(0x%02x)\n",	__func__, adc);
			}
			else {
				intr = MUIC_INTR_DETACH;
				new_dev = ATTACHED_DEV_UNKNOWN_MUIC;
				pr_info("[muic] Unsupported->Discarded.\n");
			}
			break;
		case ADC_OPEN:
			/* sometimes muic fails to
				catch JIG_UART_OFF detaching */
			/* double check with ADC */
			if (new_dev == ATTACHED_DEV_JIG_UART_OFF_MUIC) {
				new_dev = ATTACHED_DEV_UNKNOWN_MUIC;
				intr = MUIC_INTR_DETACH;
				pr_info("[muic] ADC OPEN DETECTED\n");
			}
			break;
		default:
			if(vbvolt) {
				intr = MUIC_INTR_ATTACH;
				new_dev = ATTACHED_DEV_UNDEFINED_RANGE_MUIC;
				pr_info("[muic] %s unsupported ADC(0x%02x)\n",	__func__, adc);
			}
			else {
				intr = MUIC_INTR_DETACH;
				new_dev = ATTACHED_DEV_UNKNOWN_MUIC;
				pr_info("[muic] Unsupported->Discarded.\n");
			}
			break;
		}
	}
#endif /* CONFIG_MUIC_S2MU004_NON_USB_C_TYPE */

#if 0
	if (ATTACHED_DEV_UNKNOWN_MUIC == new_dev) {
		if (vbvolt) {
			intr = MUIC_INTR_ATTACH;
			new_dev = ATTACHED_DEV_UNDEFINED_CHARGING_MUIC;
			pr_info("[muic] UNDEFINED VB DETECTED\n");
		} else {
			intr = MUIC_INTR_DETACH;
		}
	}
#endif

#if defined(CONFIG_CCIC_S2MU004)
/* FIXME: for VB on-off case */
jig:
#endif

	if (intr == MUIC_INTR_ATTACH) {
#ifdef	CONFIG_MUIC_SUPPORT_CCIC
		muic_set_legacy_dev(pmuic, new_dev);
#endif
		s2mu004_muic_handle_attach(muic_data, new_dev, adc, vbvolt);
	} else {
		if (muic_data->attach_mode == S2MU004_SECOND_ATTACH)
			return;
#ifdef	CONFIG_MUIC_SUPPORT_CCIC
		if (pmuic->opmode & OPMODE_CCIC) {
			if (!mdev_continue_for_TA_USB(pmuic, muic_data->attached_dev))
				return;
		}
#endif
		s2mu004_muic_handle_detach(muic_data);
	}

#if defined(CONFIG_VBUS_NOTIFIER)
	vbus_notifier_handle((!!vbvolt) ? STATUS_VBUS_HIGH : STATUS_VBUS_LOW);
#endif /* CONFIG_VBUS_NOTIFIER */

}

static int s2mu004_muic_reg_init(struct s2mu004_muic_data *muic_data)
{
	struct i2c_client *i2c = muic_data->i2c;
	int ret = 0;
	int val1, val2, val3, adc;
	u8 reg_data = 0;
#ifndef CONFIG_MUIC_S2MU004_NON_USB_C_TYPE
#if defined(CONFIG_CCIC_S2MU004)
#else
	int data = 0;
#endif /* CONFIG_CCIC_S2MU004 */
#endif /* CONFIG_MUIC_S2MU004_NON_USB_C_TYPE */

	pr_info("[muic] %s\n", __func__);

	val1 = s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_DEVICE_TYPE1);
	val2 = s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_DEVICE_TYPE2);
	val3 = s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_DEVICE_TYPE3);
	adc = s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_ADC);
	pr_info("[muic] dev[1:0x%x, 2:0x%x, 3:0x%x], adc:0x%x\n", val1, val2, val3, adc);

#ifndef CONFIG_MUIC_S2MU004_NON_USB_C_TYPE
	pr_info("%s:%s s2mu004 usb-c type \n", MFD_DEV_NAME, __func__);
#if defined(CONFIG_CCIC_S2MU004)
	s2mu004_i2c_write_byte(i2c, S2MU004_REG_MUIC_INT1_MASK, INT_PDIC_MASK1);
	s2mu004_i2c_write_byte(i2c, S2MU004_REG_MUIC_INT2_MASK, INT_PDIC_MASK2);

	/* set dcd timer out to 0.6s */
	reg_data = s2mu004_i2c_read_byte(i2c, 0xCB);
	reg_data &= ~0x07;
	reg_data |= 0x04;
	s2mu004_i2c_write_byte(i2c, 0xCB, reg_data);

	/* enable rid detect for waterproof */
	reg_data = s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_RID_CTRL);
	reg_data &= ~(0x01 << 1);
	s2mu004_i2c_write_byte(i2c, S2MU004_REG_MUIC_RID_CTRL, reg_data);
#else
	data = s2mu004_i2c_read_byte(i2c, 0xCC);
	data |= (0x01 << 1);
	s2mu004_i2c_write_byte(i2c, 0xCC, data);

	/* adc, RID int masking */
	data = s2mu004_i2c_read_byte(i2c, 0x08);
	data |= (0x01 << 5);
	s2mu004_i2c_write_byte(i2c, 0x08, data);

	data = s2mu004_i2c_read_byte(i2c, 0x09);
	data |= (0x01 << 2);
	s2mu004_i2c_write_byte(i2c, 0x09, data);
#endif /* CONFIG_CCIC_S2MU004 */
#endif /* CONFIG_MUIC_S2MU004_NON_USB_C_TYPE */

	reg_data = s2mu004_i2c_read_byte(i2c, 0xDA);
	reg_data &= 0xef;
	s2mu004_i2c_write_byte(i2c, 0xDA, reg_data);
#if defined(CONFIG_SUPPORT_RID_PERIODIC)
	s2mu004_muic_set_adc_mode(muic_data, S2MU004_ADC_PERIODIC);
#endif
	ret = s2mu004_i2c_guaranteed_wbyte(i2c,
			S2MU004_REG_MUIC_CTRL1, CTRL_MASK);
	if (ret < 0)
		pr_err("[muic] failed to write ctrl(%d)\n", ret);

	s2mu004_i2c_update_bit(i2c,
			S2MU004_REG_LDOADC_VSETL, LDOADC_VSETH_MASK, 0, LDOADC_VSET_3V);
#if defined(CONFIG_CCIC_S2MU004)
	s2mu004_i2c_update_bit(i2c,
			S2MU004_REG_LDOADC_VSETH, LDOADC_VSETH_MASK, 0, LDOADC_VSET_2_7V);
#else
	s2mu004_i2c_update_bit(i2c,
			S2MU004_REG_LDOADC_VSETH, LDOADC_VSETH_MASK, 0, LDOADC_VSET_3V);
#endif
	s2mu004_i2c_update_bit(i2c,
			S2MU004_REG_LDOADC_VSETH,
			LDOADC_VSETH_WAKE_HYS_MASK,
			LDOADC_VSETH_WAKE_HYS_SHIFT, 0x1);

	return ret;
}

#if defined(CONFIG_CCIC_S2MU004)
#ifndef CONFIG_SEC_FACTORY
static int s2mu004_muic_get_otg_state(void)
{
	struct power_supply *psy_otg;
	union power_supply_propval val;
	int ret = 0;
	psy_otg = get_power_supply_by_name("otg");
	if (psy_otg) {
		ret = psy_otg->get_property(psy_otg, POWER_SUPPLY_PROP_CHARGE_POWERED_OTG_CONTROL, &val);
	} else {
		pr_info("%s:%d get psy otg failed\n", __func__, __LINE__);
	}
	if (ret) {
		pr_info("%s:%d get prop fail\n", __func__, __LINE__);
	} else {
		pr_info("%s:%d is ocp ?: %d\n", __func__, __LINE__, val.intval);
		return val.intval;
	}
	return 0;
}
#endif
#endif

static irqreturn_t s2mu004_muic_irq_thread(int irq, void *data)
{
	struct s2mu004_muic_data *muic_data = data;
#ifndef CONFIG_SEC_FACTORY
	int irq_num = irq - muic_data->s2mu004_dev->irq_base;
	int vbvolt, adc = -1;
	struct i2c_client *i2c = muic_data->i2c;
#if defined(CONFIG_CCIC_S2MU004)
	u8 reg_data = 0;
#endif
#else
#if defined(CONFIG_CCIC_S2MU004)
	struct i2c_client *i2c = muic_data->i2c;
	u8 reg_data = 0;
	int irq_num = irq - muic_data->s2mu004_dev->irq_base;
	int reg_val, vbvolt, adc;
#endif
#endif

#ifdef CONFIG_MUIC_SUPPORT_CCIC
	muic_data_t *pmuic = muic_data->ccic_data;
#endif
	mutex_lock(&muic_data->muic_mutex);
	wake_lock(&muic_data->wake_lock);

#if defined(CONFIG_CCIC_S2MU004)
	pr_info("%s:%s muic mode (%d) irq_num (%d)\n", MFD_DEV_NAME,
			__func__, muic_data->attach_mode, irq_num);

	/* NONE_CABLE    : default muic mode that cable is empty
	   DETACH        : MUIC real DETACH occur
	   SECOND ATTACH : there is vbus, so re-detect dp, dm for TA & USB
	   FRIST ATTACH  : there is cable but no vbus
	   OTG		 : there is otg cable */

	/* divide timing that call the detect_dev() */
	/* when Vbus off, force rid to enable */
#ifndef CONFIG_SEC_FACTORY
	vbvolt = s2mu004_muic_get_vbus_state(muic_data);
#else
	reg_val = s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_DEVICE_APPLE);
	vbvolt = !!(reg_val & DEV_TYPE_APPLE_VBUS_WAKEUP);
#endif
	adc = s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_ADC);

#ifndef CONFIG_SEC_FACTORY
	if ((irq_num == S2MU004_MUIC_IRQ2_VBUS_OFF)
		&& (pmuic->opmode & OPMODE_CCIC)
		&& !muic_data->jig_state
		&& s2mu004_muic_get_otg_state()) {
		muic_pdic_notifier_attach_attached_dev(ATTACHED_DEV_CHECK_OCP);
	}
#endif

	if ((IS_WATER_ADC(adc) || (adc & ADC_CONVERSION_ERR_MASK))
		&& !muic_data->re_detect && !vbvolt
		&& (pmuic->opmode & OPMODE_CCIC)
		&& ((irq_num == S2MU004_MUIC_IRQ2_ADC_CHANGE)
			|| (irq_num == S2MU004_MUIC_IRQ1_ATTACH))) {
		adc = s2mu004_muic_water_judge(muic_data);
	}

	adc &= ADC_MASK;
	pr_info("%s:%d  vbvolt : %d, irq_num : %d\n", __func__, __LINE__, vbvolt, irq_num);
	if (((irq_num == S2MU004_MUIC_IRQ2_ADC_CHANGE) || (irq_num == S2MU004_MUIC_IRQ1_ATTACH))
		&& !vbvolt && adc != ADC_GND && (pmuic->opmode & OPMODE_CCIC)) {
		pr_info("%s:%d adc : 0x%X, water_status : %d, vbvolt : %d\n",
					__func__, __LINE__, adc, muic_data->water_status, vbvolt);
		if (adc < ADC_OPEN && muic_data->water_status == S2MU004_WATER_MUIC_IDLE) {
			cancel_delayed_work(&muic_data->water_detect_handler);
			schedule_delayed_work(&muic_data->water_detect_handler, msecs_to_jiffies(0));
		} else if (adc == ADC_OPEN && IS_WATER_STATUS(muic_data->water_status)) {
				cancel_delayed_work(&muic_data->water_dry_handler);
			schedule_delayed_work(&muic_data->water_dry_handler,
				msecs_to_jiffies(WATER_DET_STABLE_DURATION_MS + 1000));
			msleep(100);
		} else if ((muic_data->water_status == S2MU004_WATER_MUIC_CCIC_DET
					|| muic_data->water_status == S2MU004_WATER_MUIC_CCIC_STABLE
					|| muic_data->water_status == S2MU004_WATER_MUIC_DET)
				&& IS_WATER_ADC(adc)) {
			s2mu004_muic_set_rid_adc_en(muic_data, false);
			pr_info("%s WATER Toggling(audio),, adc : 0x%X\n", __func__, adc);
			}
		goto EOH;
	} else if (muic_data->water_status == S2MU004_WATER_MUIC_CCIC_STABLE) {
		if (irq_num == S2MU004_MUIC_IRQ2_VBUS_OFF
			|| irq_num == S2MU004_MUIC_IRQ2_VBUS_ON) {
			muic_notifier_detach_attached_dev(ATTACHED_DEV_WATER_MUIC);
			muic_notifier_attach_attached_dev(ATTACHED_DEV_WATER_MUIC);
#if defined(CONFIG_VBUS_NOTIFIER)
			vbus_notifier_handle(vbvolt ? STATUS_VBUS_HIGH : STATUS_VBUS_LOW);
#endif /* CONFIG_VBUS_NOTIFIER */
		}
		ccic_uevent_work(CCIC_NOTIFY_ID_WATER, 1);
		pr_info("[muic] %s : skipped by water detected condition\n", __func__);
		goto EOH;
	} else if (irq_num == S2MU004_MUIC_IRQ2_VBUS_OFF) {
		if (muic_data->attach_mode == S2MU004_MUIC_OTG) {
			reg_data = s2mu004_i2c_read_byte(muic_data->i2c, 0xDA);
			reg_data &= 0xef;
			s2mu004_i2c_write_byte(muic_data->i2c, 0xDA , reg_data);
			muic_data->otg_state = true;
		}
		if ((muic_data->attach_mode == S2MU004_MUIC_OTG) &&
			(s2mu004_muic_refresh_adc(muic_data) == ADC_JIG_UART_OFF)) {
			pr_info("%s:%d, OTG - VBUS off case\n", __func__, __LINE__);
			muic_data->attach_mode = S2MU004_NONE_CABLE;
		} else
			muic_data->attach_mode = S2MU004_MUIC_DETACH;

		/* FIXME: for VB on-off case */
		/* muic_data->jig_state = false; */

	} else if (irq_num == S2MU004_MUIC_IRQ1_DETACH &&
		(muic_data->attach_mode == S2MU004_FIRST_ATTACH ||
		muic_data->attach_mode == S2MU004_MUIC_OTG)) {

		/* disable vbus det for interrupt */
		if (muic_data->attach_mode == S2MU004_MUIC_OTG) {
			reg_data = s2mu004_i2c_read_byte(muic_data->i2c, 0xDA);
			reg_data &= 0xef;
			s2mu004_i2c_write_byte(muic_data->i2c, 0xDA , reg_data);
		}

		muic_data->attach_mode = S2MU004_MUIC_DETACH;
	}

	if ((muic_data->attach_mode == S2MU004_NONE_CABLE) ||
		(muic_data->attach_mode == S2MU004_MUIC_DETACH) ||
		((muic_data->attach_mode == S2MU004_SECOND_ATTACH) &&
				(irq_num == S2MU004_MUIC_IRQ1_ATTACH) &&
						!muic_data->jig_state) ||
		((muic_data->attach_mode == S2MU004_FIRST_ATTACH) &&
				(irq_num == S2MU004_MUIC_IRQ2_VBUS_ON)) ||
		((muic_data->attach_mode == S2MU004_MUIC_OTG) &&
				(irq_num == S2MU004_MUIC_IRQ2_VBUS_ON)))
		s2mu004_muic_detect_dev(muic_data);
#else
#ifndef CONFIG_SEC_FACTORY
	vbvolt = s2mu004_muic_get_vbus_state(muic_data);
	adc = s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_ADC) & ADC_MASK;
	if (!vbvolt) {
		pr_info("%s:%s adc: 0x%X\n", MFD_DEV_NAME, __func__, adc);

		if (IS_AUDIO_ADC(adc) && !muic_data->is_water_wa) {
			if (irq_num == S2MU004_MUIC_IRQ2_ADC_CHANGE) {
				s2mu004_muic_set_water_adc_ldo_wa(muic_data, true);
			}
		} else if (IS_WATER_ADC(adc) && !muic_data->is_water_wa) {
			if (irq_num == S2MU004_MUIC_IRQ1_ATTACH) {
				s2mu004_muic_set_water_adc_ldo_wa(muic_data, true);
			}
		}
	}

	if (adc == ADC_OPEN
		&& irq_num == S2MU004_MUIC_IRQ2_ADC_CHANGE
		&& muic_data->is_water_wa) {
		usleep_range(20000,21000);
		s2mu004_muic_set_water_adc_ldo_wa(muic_data, false);
	}
#endif
	s2mu004_muic_detect_dev(muic_data);
#endif

#if defined(CONFIG_CCIC_S2MU004)
EOH:
#endif
	wake_unlock(&muic_data->wake_lock);
	mutex_unlock(&muic_data->muic_mutex);

	pr_info("%s:%s done\n", MFD_DEV_NAME, __func__);
	return IRQ_HANDLED;
}

#if defined(CONFIG_CCIC_S2MU004)
static void s2mu004_muic_put_dry_chk_time(struct s2mu004_muic_data *muic_data)
{
	struct timeval time;

	do_gettimeofday(&time);
	pr_info("%s Dry check time : %ld\n", __func__, (long)time.tv_sec);
	muic_data->dry_chk_time = (long)time.tv_sec;
}
#endif

#if defined(CONFIG_CCIC_S2MU004) || !defined(CONFIG_SEC_FACTORY)
static void s2mu004_muic_set_water_adc_ldo_wa(struct s2mu004_muic_data *muic_data, bool en)
{
	struct i2c_client *i2c = muic_data->i2c;

#if !defined(CONFIG_CCIC_S2MU004) && !defined(CONFIG_SEC_FACTORY)
	muic_data->is_water_wa = en;
#endif
	pr_info("%s: en : (%d)\n", __func__, (int)en);
	if (en) {
		/* W/A apply */
		s2mu004_i2c_update_bit(i2c,
				S2MU004_REG_LDOADC_VSETH, LDOADC_VSETH_MASK, 0, LDOADC_VSET_1_2V);
		usleep_range(WATER_TOGGLE_WA_DURATION_US, WATER_TOGGLE_WA_DURATION_US + 1000);
		s2mu004_i2c_update_bit(i2c,
				S2MU004_REG_LDOADC_VSETH, LDOADC_VSETH_MASK, 0, LDOADC_VSET_1_4V);
	} else {
		/* W/A unapply */
#if defined(CONFIG_CCIC_S2MU004)
		s2mu004_i2c_update_bit(i2c,
				S2MU004_REG_LDOADC_VSETH, LDOADC_VSETH_MASK, 0, LDOADC_VSET_2_7V);
#else
		s2mu004_i2c_update_bit(i2c,
				S2MU004_REG_LDOADC_VSETH, LDOADC_VSETH_MASK, 0, LDOADC_VSET_3V);
#endif
	}
	return;
}
#endif /* CONFIG_CCIC_S2MU004 || !CONFIG_SEC_FACTORY */

#ifdef CONFIG_CCIC_S2MU004
static int s2mu004_muic_water_judge(struct s2mu004_muic_data *muic_data)
{
	int i, adc_recheck = 0;//, adc_open_cnt = 0;
	pr_info("%s : start rid rescan\n", __func__);

#if defined(CONFIG_SUPPORT_RID_PERIODIC)
	/* Prepare for the adc recheck : oneshot mode. */
	s2mu004_muic_set_adc_mode(muic_data, S2MU004_ADC_ONESHOT);
#endif
	for (i = 0; i < WATER_DET_RETRY_CNT; i++) {
		adc_recheck = s2mu004_muic_recheck_adc(muic_data);
		if (adc_recheck == ADC_GND) {
#if defined(CONFIG_SUPPORT_RID_PERIODIC)
			s2mu004_muic_set_adc_mode(muic_data, S2MU004_ADC_PERIODIC);
#endif
			pr_info("%s : NOT WATER From MUIC RID\n", __func__);
			return adc_recheck;
		}
		if (s2mu004_muic_get_vbus_state(muic_data)) {
			pr_info("%s : vbus while detecting\n", __func__);
			return ADC_GND;
		}
		pr_info("%s : %d st try : adc(0x%x)\n", __func__, i, adc_recheck);
	}

	if (adc_recheck != ADC_OPEN) {
		muic_data->re_detect = 1;
	} else {
		muic_data->re_detect = 0;
	}

#if defined(CONFIG_SUPPORT_RID_PERIODIC)
	s2mu004_muic_set_adc_mode(muic_data, S2MU004_ADC_PERIODIC);
#endif
	return adc_recheck;
}

static void s2mu004_muic_water_detect_handler(struct work_struct *work)
{
	struct s2mu004_muic_data *muic_data =
		container_of(work, struct s2mu004_muic_data, water_detect_handler.work);
	int wait_ret = 0, adc = 0;
#if defined(CONFIG_VBUS_NOTIFIER)
	int vbvolt = 0;
#endif

	mutex_lock(&muic_data->water_det_mutex);
	wake_lock(&muic_data->water_wake_lock);
	if (muic_data->water_status != S2MU004_WATER_MUIC_IDLE) {
		pr_info("%s %d exit detect, due to status mismatch\n", __func__, __LINE__);
		goto EXIT_DETECT;
	}

	pr_info("%s\n", __func__);

	s2mu004_muic_set_rid_adc_en(muic_data, false);
	muic_data->water_status = S2MU004_WATER_MUIC_DET;
	muic_pdic_notifier_attach_attached_dev(ATTACHED_DEV_CHK_WATER_REQ);

	wait_ret = wait_event_interruptible_timeout(muic_data->wait,
		muic_data->water_status >= S2MU004_WATER_MUIC_CCIC_DET,
		msecs_to_jiffies(WATER_CCIC_WAIT_DURATION_MS));

	if ((wait_ret < 0) || (!wait_ret)) {
		pr_err("%s wait_q abnormal, status : %d\n", __func__, muic_data->water_status);
		muic_data->water_status = S2MU004_WATER_MUIC_IDLE;
		muic_data->re_detect = 0;
		s2mu004_muic_set_water_adc_ldo_wa(muic_data, false);
		s2mu004_muic_set_rid_adc_en(muic_data, true);
	} else {
		if (muic_data->water_status == S2MU004_WATER_MUIC_CCIC_DET) {
			pr_info("%s: WATER DETECT!!!\n", __func__);
			muic_data->dry_cnt = 0;
			muic_data->dry_duration_sec = WATER_DRY_RETRY_INTERVAL_SEC;
			muic_notifier_attach_attached_dev(ATTACHED_DEV_WATER_MUIC);
			s2mu004_i2c_update_bit(muic_data->i2c,
					0xD5,
					LDOADC_VSETH_WAKE_HYS_MASK,
					LDOADC_VSETH_WAKE_HYS_SHIFT, 0x1);
			s2mu004_muic_set_water_adc_ldo_wa(muic_data, true);
			msleep(100);
			adc = s2mu004_muic_recheck_adc(muic_data);
			msleep(2000);
			muic_data->water_status = S2MU004_WATER_MUIC_CCIC_STABLE;
			muic_data->water_dry_status = S2MU004_WATER_DRY_MUIC_IDLE;
#if defined(CONFIG_VBUS_NOTIFIER)
			vbvolt = s2mu004_muic_get_vbus_state(muic_data);
			vbus_notifier_handle(vbvolt ? STATUS_VBUS_HIGH : STATUS_VBUS_LOW);
#endif /* CONFIG_VBUS_NOTIFIER */
			s2mu004_muic_put_dry_chk_time(muic_data);
			cancel_delayed_work(&muic_data->water_dry_handler);
			schedule_delayed_work(&muic_data->water_dry_handler,
				msecs_to_jiffies(1800000));
			pr_info("%s %d WATER DETECT stabled adc : 0x%X\n", __func__, __LINE__, adc);
		} else if (muic_data->water_status == S2MU004_WATER_MUIC_CCIC_INVALID) {
			pr_info("%s Not Water From CCIC.\n", __func__);
			muic_data->water_status = S2MU004_WATER_MUIC_IDLE;
			muic_data->re_detect = 0;
			s2mu004_muic_set_water_adc_ldo_wa(muic_data, false);
			s2mu004_muic_set_rid_adc_en(muic_data, true);
		}
	}
EXIT_DETECT:
	wake_unlock(&muic_data->water_wake_lock);
	mutex_unlock(&muic_data->water_det_mutex);
}

static void s2mu004_muic_water_dry_handler(struct work_struct *work)
{
	struct s2mu004_muic_data *muic_data =
		container_of(work, struct s2mu004_muic_data, water_dry_handler.work);
	int adc, i, wait_ret = 0;

	mutex_lock(&muic_data->water_dry_mutex);
	wake_lock(&muic_data->water_dry_wake_lock);

	if (muic_data->water_status != S2MU004_WATER_MUIC_CCIC_STABLE) {
		pr_info("%s Invalid status for Dry check\n", __func__);
		goto EXIT_DRY_STATE;
	}

	pr_info("%s Dry check start\n", __func__);
	s2mu004_muic_put_dry_chk_time(muic_data);
	s2mu004_muic_set_rid_int_mask_en(muic_data, true);
	s2mu004_muic_set_water_adc_ldo_wa(muic_data, false);

	if (muic_data->dry_cnt++ > 5) {
		muic_data->dry_duration_sec = 1800;
		pr_info("%s Dry check cnt : %d\n", __func__, muic_data->dry_cnt);
	}

	for (i = 0; i < WATER_DET_RETRY_CNT; i++) {
		adc = s2mu004_muic_recheck_adc(muic_data);
		pr_info("%s, %d th try, adc : 0x%X\n", __func__, i, (char)adc);
		if (adc < 0x1F) {
			pr_info("%s WATER IS NOT DRIED YET!!!\n", __func__);
			s2mu004_muic_set_rid_adc_en(muic_data, false);
			cancel_delayed_work(&muic_data->water_dry_handler);
			schedule_delayed_work(&muic_data->water_dry_handler,
				msecs_to_jiffies(1800000));
			msleep(1000);
			goto EXIT_DRY;
		}
	}
	muic_data->water_dry_status = S2MU004_WATER_DRY_MUIC_DET;
	muic_pdic_notifier_attach_attached_dev(ATTACHED_DEV_CHK_WATER_DRY_REQ);
	wait_ret = wait_event_interruptible_timeout(muic_data->wait,
		muic_data->water_dry_status >= S2MU004_WATER_DRY_MUIC_CCIC_DET,
		msecs_to_jiffies(WATER_CCIC_WAIT_DURATION_MS));

	if ((wait_ret < 0) || (!wait_ret)
			|| muic_data->water_dry_status == S2MU004_WATER_DRY_MUIC_CCIC_INVALID) {
		pr_err("%s wait_q abnormal, status : %d\n",
			__func__, muic_data->water_dry_status);
		s2mu004_muic_set_rid_adc_en(muic_data, false);
		muic_data->water_dry_status = S2MU004_WATER_DRY_MUIC_IDLE;
		s2mu004_muic_set_water_adc_ldo_wa(muic_data, true);
		msleep(1000);
		muic_data->water_status = S2MU004_WATER_MUIC_CCIC_STABLE;
		cancel_delayed_work(&muic_data->water_dry_handler);
		schedule_delayed_work(&muic_data->water_dry_handler,
			msecs_to_jiffies(WATER_DRY_RETRY_INTERVAL_MS));
	} else if (muic_data->water_dry_status == S2MU004_WATER_DRY_MUIC_CCIC_DET) {
		pr_info("%s WATER DRIED!!!\n", __func__);
			s2mu004_muic_set_water_adc_ldo_wa(muic_data, false);
		msleep(500);
	muic_notifier_detach_attached_dev(ATTACHED_DEV_WATER_MUIC);
	muic_pdic_notifier_detach_attached_dev(ATTACHED_DEV_WATER_MUIC);
	muic_data->attach_mode = S2MU004_NONE_CABLE;
			muic_data->water_status = S2MU004_WATER_MUIC_IDLE;
			muic_data->water_dry_status = S2MU004_WATER_DRY_MUIC_IDLE;
	muic_data->re_detect = 0;
		muic_data->dry_duration_sec = WATER_DRY_RETRY_INTERVAL_SEC;
		muic_data->dry_cnt = 0;
	}
EXIT_DRY:
	pr_info("%s %d Exit DRY handler!!!\n", __func__, __LINE__);
	s2mu004_muic_set_rid_int_mask_en(muic_data, false);
EXIT_DRY_STATE:
	wake_unlock(&muic_data->water_dry_wake_lock);
	mutex_unlock(&muic_data->water_dry_mutex);
}

static int ccic_com_to_open_with_vbus(void *mdata)
{
	struct s2mu004_muic_data *muic_data =
		(struct s2mu004_muic_data *)mdata;
	int ret = 0;

	mutex_lock(&muic_data->switch_mutex);
	pr_info("[muic] %s\n", __func__);
	ret = com_to_open_with_vbus(muic_data);
	mutex_unlock(&muic_data->switch_mutex);

	return ret;
}

static int ccic_switch_to_ap_usb(void *mdata)
{
	struct s2mu004_muic_data *muic_data =
		(struct s2mu004_muic_data *)mdata;
	int ret = 0;

	mutex_lock(&muic_data->switch_mutex);
	pr_info("[muic] %s\n", __func__);
	ret = switch_to_ap_usb(muic_data);
	mutex_unlock(&muic_data->switch_mutex);

	return ret;
}

static int ccic_switch_to_ap_uart(void *mdata)
{
	struct s2mu004_muic_data *muic_data =
		(struct s2mu004_muic_data *)mdata;
	int ret = 0;

	mutex_lock(&muic_data->switch_mutex);
	pr_info("[muic] %s\n", __func__);
	ret = switch_to_ap_uart(muic_data);
	mutex_unlock(&muic_data->switch_mutex);

	return ret;
}

static int ccic_switch_to_cp_uart(void *mdata)
{
	struct s2mu004_muic_data *muic_data =
		(struct s2mu004_muic_data *)mdata;
	int ret = 0;

	mutex_lock(&muic_data->switch_mutex);
	pr_info("[muic] %s\n", __func__);
	ret = switch_to_cp_uart(muic_data);
	mutex_unlock(&muic_data->switch_mutex);

	return ret;
}

static int muic_get_vbus(void *mdata)
{
	struct s2mu004_muic_data *muic_data =
		(struct s2mu004_muic_data *)mdata;

	return s2mu004_muic_get_vbus_state(muic_data);
}

static void ccic_set_jig_state(void *mdata, bool val)
{
	struct s2mu004_muic_data *muic_data =
		(struct s2mu004_muic_data *)mdata;

	mutex_lock(&muic_data->muic_mutex);
	muic_data->jig_state = val;
	pr_info("[muic] %s jig_state : (%d)\n", __func__, muic_data->jig_state);
	mutex_unlock(&muic_data->muic_mutex);
}

static void s2mu004_muic_ccic_set_water_det(void *mdata, bool val)
{
	struct s2mu004_muic_data *muic_data =
		(struct s2mu004_muic_data *)mdata;

	mutex_lock(&muic_data->muic_mutex);
	if (muic_data->water_dry_status == S2MU004_WATER_DRY_MUIC_DET) {
		if (val) {
			muic_data->water_dry_status = S2MU004_WATER_DRY_MUIC_CCIC_INVALID;
		} else {
			muic_data->water_dry_status = S2MU004_WATER_DRY_MUIC_CCIC_DET;
		}
	} else {
		if (muic_data->water_status >= S2MU004_WATER_MUIC_DET) {
			if (val) {
				muic_data->water_status = S2MU004_WATER_MUIC_CCIC_DET;
			} else {
				muic_data->water_status = S2MU004_WATER_MUIC_CCIC_INVALID;
			}
			pr_info("[muic] %s muic_data->water_status : (%d)\n", __func__,
												muic_data->water_status);
		} else {
			pr_err("[muic] %s wrong status\n", __func__);
		}
	}
	mutex_unlock(&muic_data->muic_mutex);
	return;
}

void muic_disable_otg_detect(void)
{
	struct i2c_client *i2c = static_data->i2c;
	u8 reg_data = 0;

	pr_info("[muic] DISABLE USB_OTG DETECTED\n");

	reg_data = s2mu004_i2c_read_byte(i2c, 0xDA);
	reg_data &= ~(0x01 << 4);
	s2mu004_i2c_write_byte(i2c, 0xDA , reg_data);
}

static void s2mu004_set_cable_state(void *mdata, muic_attached_dev_t new_dev)
{
	struct s2mu004_muic_data *muic_data =
		(struct s2mu004_muic_data *)mdata;

	u8 reg_data = 0;

	mutex_lock(&muic_data->muic_mutex);
	wake_lock(&muic_data->wake_lock);

	switch (new_dev) {
	case ATTACHED_DEV_JIG_USB_OFF_MUIC:
		muic_data->attached_dev = ATTACHED_DEV_JIG_USB_OFF_MUIC;
		muic_data->jig_state = true;
		break;
	case ATTACHED_DEV_JIG_USB_ON_MUIC:
		muic_data->attached_dev = ATTACHED_DEV_JIG_USB_ON_MUIC;
		muic_data->jig_state = true;
		break;
	case ATTACHED_DEV_JIG_UART_OFF_MUIC:
		muic_data->attached_dev = ATTACHED_DEV_JIG_UART_OFF_MUIC;
		muic_data->jig_state = true;
		break;
	case ATTACHED_DEV_JIG_UART_OFF_VB_MUIC:
		muic_data->attached_dev = ATTACHED_DEV_JIG_UART_OFF_VB_MUIC;
		muic_data->jig_state = true;
		break;
	case ATTACHED_DEV_JIG_UART_ON_MUIC:
		muic_data->attached_dev = ATTACHED_DEV_JIG_UART_ON_MUIC;
		muic_data->jig_state = true;
		break;
	case ATTACHED_DEV_JIG_UART_ON_VB_MUIC:
		muic_data->attached_dev = ATTACHED_DEV_JIG_UART_ON_VB_MUIC;
		muic_data->jig_state = true;
		break;
	case ATTACHED_DEV_OTG_MUIC:
		muic_data->attached_dev = ATTACHED_DEV_OTG_MUIC;

		muic_data->attach_mode = S2MU004_MUIC_OTG;
		/* enable vbus det for interrupt */
		reg_data = s2mu004_i2c_read_byte(muic_data->i2c, 0xDA);
		reg_data |= (0x01 << 4);
		s2mu004_i2c_write_byte(muic_data->i2c, 0xDA , reg_data);

		pr_info("[muic] USB_OTG DETECTED\n");
		break;
	default:
		break;
	}

	if (muic_data->jig_state == true) {
		/* enable rid detect for waterproof */
		reg_data = s2mu004_i2c_read_byte(muic_data->i2c, S2MU004_REG_MUIC_RID_CTRL);
		reg_data &= ~(0x01 << 1);
		s2mu004_i2c_write_byte(muic_data->i2c, S2MU004_REG_MUIC_RID_CTRL, reg_data);
	}

	wake_unlock(&muic_data->wake_lock);
	mutex_unlock(&muic_data->muic_mutex);
}

static void s2mu004_mdev_dcd_rescan(void *mdata)
{
	struct s2mu004_muic_data *muic_data =
		(struct s2mu004_muic_data *)mdata;

	mutex_lock(&muic_data->muic_mutex);
	wake_lock(&muic_data->wake_lock);

	s2mu004_dcd_rescan(muic_data);

	wake_unlock(&muic_data->wake_lock);
	mutex_unlock(&muic_data->muic_mutex);

	return;
}
#endif /* CONFIG_CCIC_S2MU004 */

static void s2mu004_muic_dcd_recheck(struct work_struct *work)
{
	struct s2mu004_muic_data *muic_data =
		container_of(work, struct s2mu004_muic_data, dcd_recheck.work);

#ifdef CONFIG_MUIC_SUPPORT_CCIC	
	muic_data_t *pmuic = muic_data->ccic_data;

	pr_info("%s!\n", __func__);

	muic_set_dcd_rescan(pmuic);
#else
	mutex_lock(&muic_data->muic_mutex);
	wake_lock(&muic_data->wake_lock);

	pr_info("%s\n", __func__);
	
	s2mu004_dcd_rescan(muic_data);

	s2mu004_muic_detect_dev(muic_data);

	wake_unlock(&muic_data->wake_lock);
	mutex_unlock(&muic_data->muic_mutex);	
#endif /* CONFIG_MUIC_SUPPORT_CCIC */
	
	return;
}

static void s2mu004_dcd_rescan(struct s2mu004_muic_data *muic_data)
{
#ifdef CONFIG_MUIC_SUPPORT_CCIC
	int data = 0;
	int ret = 0;
	int reg_val = 0;
	struct i2c_client *i2c = muic_data->i2c;

	mutex_lock(&muic_data->switch_mutex);
	pr_info("%s call!\n", __func__);

	reg_val = s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_SW_CTRL);

	ret = com_to_open(muic_data);
	if (ret < 0)
		pr_err("%s, fail to open mansw\n", __func__);

	/* enable rid detect for waterproof */
	data = s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_RID_CTRL);
	data &= ~(0x01 << 1);
	s2mu004_i2c_write_byte(i2c, S2MU004_REG_MUIC_RID_CTRL, data);

	msleep(50);

	/* disable rid detection for muic dp dm detect */
	data = s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_RID_CTRL);
	data |= (0x01 << 1);
	s2mu004_i2c_write_byte(i2c, S2MU004_REG_MUIC_RID_CTRL, data);

	msleep(100);

	/* start secondary dp dm detect */
	data = s2mu004_i2c_read_byte(i2c, 0x6a);
	data |= 0x01;
	s2mu004_i2c_write_byte(i2c, 0x6a, data);

#ifndef CONFIG_SEC_FACTORY
	msleep(650);
#else
	msleep(50);
#endif

	ret = s2mu004_i2c_guaranteed_wbyte(i2c,	S2MU004_REG_MUIC_SW_CTRL, reg_val);
	if (ret < 0)
		pr_err("[muic] %s err write MANSW(0x%x)\n",	__func__, reg_val);

	mutex_unlock(&muic_data->switch_mutex);

	return;

#else
	int data = 0;
	int ret = 0;
	int reg_val = 0;
	struct i2c_client *i2c = muic_data->i2c;

	mutex_lock(&muic_data->switch_mutex);
	
	pr_info("%s call\n", __func__);

	reg_val = s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_SW_CTRL);

	/* muic mux switch open */
	ret = com_to_open(muic_data);
	if (ret < 0)
		pr_err("%s, fail to open mansw\n", __func__);	

	/* bc rescan */
	data = s2mu004_i2c_read_byte(i2c, S2MU004_REG_MUIC_BCD_RESCAN);
	data |= BCD_RESCAN_MASK;
	s2mu004_i2c_write_byte(i2c, S2MU004_REG_MUIC_BCD_RESCAN, data);

	msleep(190);

	/* restore muic mux switch */
	ret = s2mu004_i2c_guaranteed_wbyte(i2c, S2MU004_REG_MUIC_SW_CTRL, reg_val);
	if (ret < 0)
		pr_err("[muic] %s err write MANSW(0x%x)\n", __func__, reg_val);

	mutex_unlock(&muic_data->switch_mutex);

	return;
#endif /* CONFIG_MUIC_SUPPORT_CCIC */
}

static int s2mu004_init_rev_info(struct s2mu004_muic_data *muic_data)
{
	u8 dev_id;
	int ret = 0;

	pr_info("[muic] %s\n", __func__);

	dev_id = s2mu004_i2c_read_byte(muic_data->i2c, S2MU004_REG_REV_ID);
	if (dev_id < 0) {
		pr_err("[muic] %s(%d)\n", __func__, dev_id);
		ret = -ENODEV;
	} else {
		muic_data->muic_vendor = 0x05;
		muic_data->muic_version = (dev_id & 0x0F);
		pr_info("[muic] %s : vendor=0x%x, ver=0x%x, dev_id=0x%x\n",
			__func__, muic_data->muic_vendor,
			muic_data->muic_version, dev_id);
		muic_data->ic_rev_id = (dev_id & 0xF0) >>4;
		pr_info("[muic] %s : rev id =0x%x \n",__func__,  muic_data->ic_rev_id);
	}
	return ret;
}

static int s2mu004_muic_irq_init(struct s2mu004_muic_data *muic_data)
{
	int ret = 0;

	pr_info("%s:%s\n", MUIC_DEV_NAME, __func__);

	if (muic_data->mfd_pdata && (muic_data->mfd_pdata->irq_base > 0)) {
		int irq_base = muic_data->mfd_pdata->irq_base;

		/* request MUIC IRQ */
		muic_data->irq_attach = irq_base + S2MU004_MUIC_IRQ1_ATTACH;
		REQUEST_IRQ(muic_data->irq_attach, muic_data, "muic-attach");

		muic_data->irq_detach = irq_base + S2MU004_MUIC_IRQ1_DETACH;
		REQUEST_IRQ(muic_data->irq_detach, muic_data, "muic-detach");

		muic_data->irq_rid_chg = irq_base + S2MU004_MUIC_IRQ1_RID_CHG;
		REQUEST_IRQ(muic_data->irq_rid_chg, muic_data, "muic-rid_chg");

		muic_data->irq_vbus_on = irq_base + S2MU004_MUIC_IRQ2_VBUS_ON;
		REQUEST_IRQ(muic_data->irq_vbus_on, muic_data, "muic-vbus_on");

		muic_data->irq_rsvd_attach = irq_base + S2MU004_MUIC_IRQ2_RSVD_ATTACH;
		REQUEST_IRQ(muic_data->irq_rsvd_attach, muic_data, "muic-rsvd_attach");

		muic_data->irq_adc_change = irq_base + S2MU004_MUIC_IRQ2_ADC_CHANGE;
		REQUEST_IRQ(muic_data->irq_adc_change, muic_data, "muic-adc_change");

		muic_data->irq_av_charge = irq_base + S2MU004_MUIC_IRQ2_AV_CHARGE;
		REQUEST_IRQ(muic_data->irq_av_charge, muic_data, "muic-av_charge");

		muic_data->irq_vbus_off = irq_base + S2MU004_MUIC_IRQ2_VBUS_OFF;
		REQUEST_IRQ(muic_data->irq_vbus_off, muic_data, "muic-vbus_off");

	}

	pr_info("%s:%s\n", MFD_DEV_NAME, __func__);
	pr_info("%s:%s muic-attach(%d), muic-detach(%d), muic-rid_chg(%d), muic-vbus_on(%d)",
		MUIC_DEV_NAME, __func__, muic_data->irq_attach,	muic_data->irq_detach, muic_data->irq_rid_chg,	\
			muic_data->irq_vbus_on);
	pr_info("muic-rsvd_attach(%d), muic-adc_change(%d), muic-av_charge(%d), muic-vbus_off(%d)\n",
		muic_data->irq_rsvd_attach, muic_data->irq_adc_change, muic_data->irq_av_charge, muic_data->irq_vbus_off);

	return ret;
}

static void s2mu004_muic_free_irqs(struct s2mu004_muic_data *muic_data)
{
	pr_info("%s:%s\n", MUIC_DEV_NAME, __func__);

	/* free MUIC IRQ */
	FREE_IRQ(muic_data->irq_attach, muic_data, "muic-attach");
	FREE_IRQ(muic_data->irq_detach, muic_data, "muic-detach");
	FREE_IRQ(muic_data->irq_rid_chg, muic_data, "muic-rid_chg");
	FREE_IRQ(muic_data->irq_vbus_on, muic_data, "muic-vbus_on");
	FREE_IRQ(muic_data->irq_rsvd_attach, muic_data, "muic-rsvd_attach");
	FREE_IRQ(muic_data->irq_adc_change, muic_data, "muic-adc_change");
	FREE_IRQ(muic_data->irq_av_charge, muic_data, "muic-av_charge");
	FREE_IRQ(muic_data->irq_vbus_off, muic_data, "muic-vbus_off");
}

#if defined(CONFIG_OF)
static int of_s2mu004_muic_dt(struct device *dev, struct s2mu004_muic_data *muic_data)
{
	struct device_node *np, *np_muic;
	int ret = 0;
	pr_info("%s:%s\n", MUIC_DEV_NAME, __func__);

	np = dev->parent->of_node;
	if (!np) {
		pr_err("[muic] %s : could not find np\n", __func__);
		return -ENODEV;
	}

	np_muic = of_find_node_by_name(np, "muic");
	if (!np_muic) {
		pr_err("[muic] %s : could not find muic sub-node np_muic\n", __func__);
		return -EINVAL;
	}

/* FIXME */
#if !defined(CONFIG_MUIC_UART_SWITCH)
	if (of_gpio_count(np_muic) < 1) {
		pr_err("[muic] %s : could not find muic gpio\n", __func__);
		muic_data->pdata->gpio_uart_sel = 0;
	} else
		muic_data->pdata->gpio_uart_sel = of_get_gpio(np_muic, 0);
#else
	muic_data->pdata->uart_addr =
		(const char *)of_get_property(np_muic, "muic,uart_addr", NULL);
	muic_data->pdata->uart_txd =
		(const char *)of_get_property(np_muic, "muic,uart_txd", NULL);
	muic_data->pdata->uart_rxd =
		(const char *)of_get_property(np_muic, "muic,uart_rxd", NULL);
#endif

	return ret;
}
#endif /* CONFIG_OF */

/* if need to set s2mu004 pdata */
static struct of_device_id s2mu004_muic_match_table[] = {
	{ .compatible = "samsung,s2mu004-muic",},
	{},
};

static int s2mu004_muic_probe(struct platform_device *pdev)
{
	struct s2mu004_dev *s2mu004 = dev_get_drvdata(pdev->dev.parent);
	struct s2mu004_platform_data *mfd_pdata = dev_get_platdata(s2mu004->dev);
	struct s2mu004_muic_data *muic_data;
#ifdef CONFIG_MUIC_SUPPORT_CCIC
	muic_data_t *pmuic;
#endif
	int ret = 0;

	pr_info("[muic] %s:%s\n", MFD_DEV_NAME, __func__);

	muic_data = kzalloc(sizeof(struct s2mu004_muic_data), GFP_KERNEL);
	if (!muic_data) {
		pr_err("[muic] %s: failed to allocate driver data\n", __func__);
		ret = -ENOMEM;
		goto err_return;
	}

#ifdef CONFIG_MUIC_SUPPORT_CCIC
	pmuic = kzalloc(sizeof(muic_data_t), GFP_KERNEL);
	if (!pmuic) {
		pr_err("%s: failed to allocate driver data\n", __func__);
		ret = -ENOMEM;
		goto err_kfree2;
	}
#endif

	if (!mfd_pdata) {
		pr_err("%s: failed to get s2mu004 mfd platform data\n", __func__);
		ret = -ENOMEM;
		goto err_kfree1;
	}

	/* save platfom data for gpio control functions */

	static_data = muic_data;
#ifdef CONFIG_MUIC_SUPPORT_CCIC
	pmuic->pdata = &muic_pdata;
	pmuic->muic_data = (struct s2mu004_muic_data *)muic_data;
	pmuic->com_to_open_with_vbus = ccic_com_to_open_with_vbus;
	pmuic->switch_to_ap_usb = ccic_switch_to_ap_usb;
	pmuic->switch_to_ap_uart = ccic_switch_to_ap_uart;
	pmuic->switch_to_cp_uart = ccic_switch_to_cp_uart;
	pmuic->get_vbus = muic_get_vbus;
	pmuic->set_jig_state = ccic_set_jig_state;
	pmuic->set_cable_state = s2mu004_set_cable_state;
	pmuic->dcd_rescan = s2mu004_mdev_dcd_rescan;
	pmuic->is_dcdtmr_intr = false;
	pmuic->set_water_detect = s2mu004_muic_ccic_set_water_det;
	pmuic->is_afc_pdic_ready = false;	

	muic_data->ccic_data = (muic_data_t *)pmuic;
	pmuic->opmode = get_ccic_info() & 0xF;
#endif

	muic_data->s2mu004_dev = s2mu004;
	muic_data->dev = &pdev->dev;
	muic_data->i2c = s2mu004->i2c;
	muic_data->mfd_pdata = mfd_pdata;
	muic_data->pdata = &muic_pdata;
	muic_data->temp = 0;
	muic_data->attach_mode = S2MU004_NONE_CABLE;
	muic_data->check_adc = false;
	muic_data->jig_state = false;
	muic_data->re_detect = 0;
	muic_data->afc_check = false;
#if defined(CONFIG_CCIC_S2MU004)
	muic_data->water_status = S2MU004_WATER_MUIC_IDLE;
	muic_data->water_dry_status = S2MU004_WATER_DRY_MUIC_IDLE;
	muic_data->dry_chk_time = 0;
	muic_data->dry_cnt = 0;
	muic_data->dry_duration_sec = WATER_DRY_RETRY_INTERVAL_SEC;
	muic_data->otg_state = false;
#else
#ifndef CONFIG_SEC_FACTORY
	muic_data->is_water_wa = false;
#endif
	muic_data->bcd_rescan_cnt = 0;
#endif
#if defined(CONFIG_OF)
	ret = of_s2mu004_muic_dt(&pdev->dev, muic_data);
	if (ret < 0)
		pr_err("[muic] no muic dt! ret[%d]\n", ret);
#if 0
#if defined(CONFIG_HV_MUIC_S2MU004_AFC)
	ret = of_s2mu004_hv_muic_dt(muic_data);
	if (ret < 0) {
		pr_err("%s:%s not found muic dt! ret[%d]\n", MUIC_DEV_NAME, __func__, ret);
	}
#endif /* CONFIG_HV_MUIC_S2MU004_AFC */
#endif
#endif /* CONFIG_OF */

	mutex_init(&muic_data->switch_mutex);
	mutex_init(&muic_data->muic_mutex);
#if defined(CONFIG_CCIC_S2MU004)
	init_waitqueue_head(&muic_data->wait);
	mutex_init(&muic_data->water_det_mutex);
	mutex_init(&muic_data->water_dry_mutex);
#endif
	muic_data->is_factory_start = false;
	muic_data->attached_dev = ATTACHED_DEV_UNKNOWN_MUIC;
	muic_data->is_usb_ready = false;
	platform_set_drvdata(pdev, muic_data);

#if !defined(CONFIG_MUIC_UART_SWITCH)
	if (muic_pdata.gpio_uart_sel)
#endif
		muic_pdata.set_gpio_uart_sel = s2mu004_set_gpio_uart_sel;

	if (muic_data->pdata->init_gpio_cb)
		ret = muic_data->pdata->init_gpio_cb(get_switch_sel());
	if (ret) {
		pr_err("[muic] %s: failed to init gpio(%d)\n", __func__, ret);
		goto fail_init_gpio;
	}

#ifdef CONFIG_SEC_SYSFS
	/* create sysfs group */
	ret = sysfs_create_group(&switch_device->kobj, &s2mu004_muic_group);
	if (ret) {
		pr_err("[muic] failed to create sysfs\n");
		goto fail;
	}
	dev_set_drvdata(switch_device, muic_data);
#endif

	ret = s2mu004_init_rev_info(muic_data);
	if (ret) {
		pr_err("[muic] failed to init muic(%d)\n", ret);
		goto fail;
	}

	ret = s2mu004_muic_reg_init(muic_data);
	if (ret) {
		pr_err("[muic] failed to init muic(%d)\n", ret);
		goto fail;
	}

#if defined(CONFIG_HV_MUIC_S2MU004_AFC)
	/* initial hv cable detection */
	if (muic_data->is_afc_muic_ready)
		s2mu004_hv_muic_init_detect(muic_data);

	s2mu004_hv_muic_initialize(muic_data);

	/* initial check dpdnvden before cable detection */
	/* s2mu004_hv_muic_init_check_dpdnvden(muic_data); */
#endif /* CONFIG_HV_MUIC_S2MU004_AFC */

#if 0
	ret = s2mu004_i2c_read_byte(muic_data->i2c, S2MU004_REG_MUIC_SW_CTRL);
	if (ret < 0)
		pr_err("[muic] %s: err MANSW (%d)\n", __func__, ret);
	else {
		/* RUSTPROOF : disable UART connection if MANSW
			from BL is OPEN_RUSTPROOF */
		if (ret == MANSW_OPEN_RUSTPROOF) {
			muic_data->is_rustproof = true;
			com_to_open_with_vbus(muic_data);
		} else {
			muic_data->is_rustproof = false;
		}
	}
#else
	muic_data->is_rustproof = muic_data->pdata->rustproof_on;
	if (muic_data->is_rustproof) {
		pr_err("[muic] %s rustproof is enabled\n", __func__);
		com_to_open_with_vbus(muic_data);
	}
#endif

	if (muic_data->pdata->init_switch_dev_cb)
		muic_data->pdata->init_switch_dev_cb();

	ret = s2mu004_muic_irq_init(muic_data);
	if (ret) {
		pr_err("[muic] %s: failed to init irq(%d)\n", __func__, ret);
		goto fail_init_irq;
	}

#if defined(CONFIG_HV_MUIC_S2MU004_AFC)
	ret = s2mu004_afc_muic_irq_init(muic_data);
	if (ret < 0) {
		pr_err("%s:%s Failed to initialize HV MUIC irq:%d\n", MUIC_DEV_NAME,
				__func__, ret);
		s2mu004_hv_muic_free_irqs(muic_data);
	}
#endif /* CONFIG_HV_MUIC_S2MU004_AFC */

	wake_lock_init(&muic_data->wake_lock, WAKE_LOCK_SUSPEND, "muic_wake");
#if defined(CONFIG_CCIC_S2MU004)
	wake_lock_init(&muic_data->water_wake_lock, WAKE_LOCK_SUSPEND, "muic_water_wake");
	wake_lock_init(&muic_data->water_dry_wake_lock, WAKE_LOCK_SUSPEND, "muic_water_dry_wake");
#endif
	/* initial cable detection */
	set_int_mask(muic_data, false);
#ifdef CONFIG_SEC_FACTORY
	init_otg_reg(muic_data);
#endif

#if defined(CONFIG_CCIC_S2MU004)
	INIT_DELAYED_WORK(&muic_data->water_dry_handler, s2mu004_muic_water_dry_handler);
	INIT_DELAYED_WORK(&muic_data->water_detect_handler, s2mu004_muic_water_detect_handler);
#endif
	INIT_DELAYED_WORK(&muic_data->dcd_recheck, s2mu004_muic_dcd_recheck);
	s2mu004_muic_irq_thread(-1, muic_data);

#ifdef CONFIG_MUIC_SUPPORT_CCIC
	if (pmuic->opmode & OPMODE_CCIC)
		muic_register_ccic_notifier(pmuic);
	else
		pr_info("%s: OPMODE_MUIC, CCIC NOTIFIER is not used.\n", __func__);
#endif
#if defined(CONFIG_CCIC_S2MU004)
	if (!s2mu004_muic_get_vbus_state(muic_data)) {
		pr_info("%s : init adc : 0x%X\n", __func__,
			s2mu004_muic_recheck_adc(muic_data));
	}
#endif
	return 0;

fail_init_irq:
	s2mu004_muic_free_irqs(muic_data);
fail:
#ifdef CONFIG_SEC_SYSFS
	sysfs_remove_group(&switch_device->kobj, &s2mu004_muic_group);
#endif
	mutex_destroy(&muic_data->muic_mutex);
fail_init_gpio:
err_kfree1:
#ifdef CONFIG_MUIC_SUPPORT_CCIC
	kfree(pmuic);
err_kfree2:
#endif
	kfree(muic_data);
err_return:
	return ret;
}

static int s2mu004_muic_remove(struct platform_device *pdev)
{
	struct s2mu004_muic_data *muic_data = platform_get_drvdata(pdev);
#ifdef CONFIG_SEC_SYSFS
	sysfs_remove_group(&switch_device->kobj, &s2mu004_muic_group);
#endif

	if (muic_data) {
		pr_info("[muic] %s\n", __func__);

#if defined(CONFIG_HV_MUIC_S2MU004_AFC)
		s2mu004_hv_muic_remove(muic_data);
#endif /* CONFIG_HV_MUIC_S2MU004_AFC */

		disable_irq_wake(muic_data->i2c->irq);
		s2mu004_muic_free_irqs(muic_data);
		mutex_destroy(&muic_data->muic_mutex);
#if defined(CONFIG_CCIC_S2MU004)
		mutex_destroy(&muic_data->water_det_mutex);
		mutex_destroy(&muic_data->water_dry_mutex);
#endif
		i2c_set_clientdata(muic_data->i2c, NULL);
		kfree(muic_data);
	}

	return 0;
}

static void s2mu004_muic_shutdown(struct device *dev)
{
	struct s2mu004_muic_data *muic_data = dev_get_drvdata(dev);
	int ret;

	pr_info("[muic] %s\n", __func__);
	if (!muic_data->i2c) {
		pr_err("[muic] %s no muic i2c client\n", __func__);
		return;
	}

	pr_info("[muic] open D+,D-,V_bus line\n");
	ret = com_to_open(muic_data);
	if (ret < 0)
		pr_err("[muic] fail to open mansw\n");

	/* set auto sw mode before shutdown to make sure device goes into */
	/* LPM charging when TA or USB is connected during off state */
	pr_info("[muic] muic auto detection enable\n");
	ret = set_manual_sw(muic_data, true);
	if (ret < 0) {
		pr_err("[muic] %s fail to update reg\n", __func__);
		return;
	}

#if defined(CONFIG_HV_MUIC_S2MU004_AFC)
	s2mu004_hv_muic_remove(muic_data);
#endif /* CONFIG_HV_MUIC_S2MU004_AFC */
}

#if defined CONFIG_PM
static int s2mu004_muic_suspend(struct device *dev)
{
	struct s2mu004_muic_data *muic_data = dev_get_drvdata(dev);

	muic_data->suspended = true;

	return 0;
}

static int s2mu004_muic_resume(struct device *dev)
{
	struct s2mu004_muic_data *muic_data = dev_get_drvdata(dev);
#if defined(CONFIG_CCIC_S2MU004)
	struct timeval time;
	long duration;
#endif

	muic_data->suspended = false;

	if (muic_data->need_to_noti) {
		if (muic_data->attached_dev)
			muic_notifier_attach_attached_dev(muic_data->attached_dev);
		else
			muic_notifier_detach_attached_dev(muic_data->attached_dev);
		muic_data->need_to_noti = false;
	}

#if defined(CONFIG_CCIC_S2MU004)
	if (muic_data->water_status == S2MU004_WATER_MUIC_CCIC_STABLE) {
		if (!s2mu004_muic_get_vbus_state(muic_data)) {
			do_gettimeofday(&time);
			duration = (long)time.tv_sec - muic_data->dry_chk_time;
			pr_info("%s dry check duration : (%ld)\n", __func__, duration);
			if (duration > muic_data->dry_duration_sec || duration < 0) {
				cancel_delayed_work(&muic_data->water_dry_handler);
				schedule_delayed_work(&muic_data->water_dry_handler, 0);
			}
		}
	}
#endif
	return 0;
}
#else
#define s2mu004_muic_suspend NULL
#define s2mu004_muic_resume NULL
#endif

static SIMPLE_DEV_PM_OPS(s2mu004_muic_pm_ops, s2mu004_muic_suspend,
			 s2mu004_muic_resume);

static struct platform_driver s2mu004_muic_driver = {
	.driver = {
		.name = "s2mu004-muic",
		.owner	= THIS_MODULE,
		.of_match_table = s2mu004_muic_match_table,
#ifdef CONFIG_PM
		.pm = &s2mu004_muic_pm_ops,
#endif
		.shutdown = s2mu004_muic_shutdown,
	},
	.probe = s2mu004_muic_probe,
/* FIXME: It makes build error of defined but not used. */
	/* .remove = __devexit_p(s2mu004_muic_remove), */
	.remove = s2mu004_muic_remove,
};

static int __init s2mu004_muic_init(void)
{
	return platform_driver_register(&s2mu004_muic_driver);
}
module_init(s2mu004_muic_init);

static void __exit s2mu004_muic_exit(void)
{
	platform_driver_unregister(&s2mu004_muic_driver);
}
module_exit(s2mu004_muic_exit);

MODULE_DESCRIPTION("S2MU004 USB Switch driver");
MODULE_LICENSE("GPL");
