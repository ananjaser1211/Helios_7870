/*
 * Copyright (C) 2014 Samsung Electronics Co.Ltd
 * http://www.samsung.com
 *
 * UART Switch driver
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
*/

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/delay.h>
#include <soc/samsung/pmu-cp.h>
#include <soc/samsung/exynos-pmu.h>
#ifdef CONFIG_SEC_SYSFS
#include <linux/sec_sysfs.h>
#endif

#ifdef CONFIG_MUIC_NOTIFIER
#include <linux/muic/muic.h>
#include <linux/muic/muic_notifier.h>
#endif

#include <linux/mcu_ipc.h>
#include "uart_sel.h"

static void uart_dir_work(void)
{
	u32 info_value = 0;

	info_value = (switch_data->uart_connect && switch_data->uart_switch_sel);

	if (info_value != mbox_get_value(MCU_CP, switch_data->mbx_uart_noti)) {
		pr_err("%s: change uart state to %s\n", __func__,
			info_value ? "CP" : "AP");

		mbox_set_value(MCU_CP, switch_data->mbx_uart_noti, info_value);
		if (mbox_get_value(MCU_CP, switch_data->mbx_uart_noti))
			mbox_set_interrupt(MCU_CP, switch_data->int_uart_noti);
	}
	else {
		pr_info("%s: Fail change uart state. C[0x%02x] S[0x%02x] M[0x%02x]\n", __func__,
			switch_data->uart_connect, switch_data->uart_switch_sel, mbox_get_value(MCU_CP, switch_data->mbx_uart_noti));
	}
}

void cp_recheck_uart_dir(void)
{
	u32 mbx_uart_noti;

	mbx_uart_noti = mbox_get_value(MCU_CP, switch_data->mbx_uart_noti);
	if (switch_data->uart_switch_sel != mbx_uart_noti)
		pr_err("Uart notifier data is not matched with mbox!\n");

	uart_dir_work();
}
EXPORT_SYMBOL_GPL(cp_recheck_uart_dir);

#ifdef CONFIG_MUIC_NOTIFIER
static int switch_handle_notification(struct notifier_block *nb,
				unsigned long action, void *data)
{
#if defined(CONFIG_CCIC_NOTIFIER)
	CC_NOTI_ATTACH_TYPEDEF *p_noti = (CC_NOTI_ATTACH_TYPEDEF *)data;
	muic_attached_dev_t attached_dev = p_noti->cable_type;
#else
	muic_attached_dev_t attached_dev = *(muic_attached_dev_t *)data;
#endif
	pr_err("%s: action=%lu attached_dev=%d\n", __func__, action, (int)attached_dev);

	if ((attached_dev == ATTACHED_DEV_JIG_UART_OFF_MUIC) ||
		(attached_dev == ATTACHED_DEV_JIG_UART_ON_MUIC) ||
		(attached_dev == ATTACHED_DEV_JIG_UART_ON_VB_MUIC) ||
		(attached_dev == ATTACHED_DEV_JIG_UART_OFF_VB_MUIC)) {
		switch (action) {
		case MUIC_NOTIFY_CMD_DETACH:
		case MUIC_NOTIFY_CMD_LOGICALLY_DETACH:
			switch_data->uart_connect = false;
			uart_dir_work();
			break;
		case MUIC_NOTIFY_CMD_ATTACH:
		case MUIC_NOTIFY_CMD_LOGICALLY_ATTACH:
			switch_data->uart_connect = true;
			uart_dir_work();
			break;
			}
	}

	return 0;
}
#endif

static int set_uart_sel(void)
{
	int ret;

	if (switch_data->uart_switch_sel == CP) {
		pr_err("Change Uart to CP\n");
		ret = exynos_pmu_update(EXYNOS_PMU_UART_IO_SHARE_CTRL,
			SEL_CP_UART_DBG, SEL_CP_UART_DBG);
		if (ret < 0)
			pr_err("%s: ERR! write Fail: %d\n", __func__, ret);
	} else {
		pr_err("Change UART to AP\n");
		ret = exynos_pmu_update(EXYNOS_PMU_UART_IO_SHARE_CTRL,
			SEL_CP_UART_DBG, 0);
		if (ret < 0)
			pr_err("%s: ERR! write Fail: %d\n", __func__, ret);
	}
	uart_dir_work();

	return 0;
}

static ssize_t usb_sel_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	return sprintf(buf, "PDA\n");
}

static ssize_t usb_sel_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	return count;
}

static ssize_t
uart_sel_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	u32 uart_ctrl;
	ssize_t ret = 0;

	if (ret < PAGE_SIZE - 1) {
		exynos_pmu_read(EXYNOS_PMU_UART_IO_SHARE_CTRL, &uart_ctrl);
		ret += snprintf(buf + ret, PAGE_SIZE - ret, (uart_ctrl & SEL_CP_UART_DBG) ?
			"CP\n":"AP\n");
	} else {
		buf[PAGE_SIZE-2] = '\n';
		buf[PAGE_SIZE-1] = '\0';
		ret = PAGE_SIZE-1;
	}

	return ret;
}

static ssize_t
uart_sel_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	pr_err("Change UART port path\n");

	if (!strncasecmp(buf, "AP", 2)) {
		switch_data->uart_switch_sel = AP;
		set_uart_sel();
	} else if (!strncasecmp(buf, "CP", 2)) {
		switch_data->uart_switch_sel = CP;
		set_uart_sel();
	} else {
		pr_err("%s invalid value\n", __func__);
	}

	return count;
}

static DEVICE_ATTR(usb_sel, 0664, usb_sel_show, usb_sel_store);
static DEVICE_ATTR(uart_sel, 0664, uart_sel_show, uart_sel_store);

static struct attribute *modemif_sysfs_attributes[] = {
	&dev_attr_uart_sel.attr,
	&dev_attr_usb_sel.attr,
	NULL
};

static const struct attribute_group uart_sel_sysfs_group = {
	.attrs = modemif_sysfs_attributes,
};

static int uart_sel_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int err = 0;

	pr_err("%s: uart_sel probe start.\n", __func__);

	switch_data = devm_kzalloc(dev, sizeof(struct uart_sel_data), GFP_KERNEL);

	err = of_property_read_u32(dev->of_node, "int_ap2cp_uart_noti",
			&switch_data->int_uart_noti);
	if (err) {
		pr_err("SWITCH_SEL parse error! [id]\n");
		return err;
	}
	err = of_property_read_u32(dev->of_node, "mbx_ap2cp_uart_noti",
			&switch_data->mbx_uart_noti);
	if (err) {
		pr_err("SWITCH_SEL parse error! [id]\n");
		return err;
	}

	err = device_create_file(dev, &dev_attr_uart_sel);
	if (err)
			pr_err("can't create modem_ctrl!!!\n");

	if ((get_switch_sel() & 0xff) != 0) {
		if ((get_switch_sel() & SWITCH_SEL_UART_MASK))
			switch_data->uart_switch_sel = AP;
		else
			switch_data->uart_switch_sel = CP;
	} else {
		switch_data->uart_switch_sel = AP;
	}

	switch_data->uart_connect = false;
	set_uart_sel();

#if defined(CONFIG_MUIC_NOTIFIER)
	switch_data->uart_notifier.notifier_call = switch_handle_notification;
	muic_notifier_register(&switch_data->uart_notifier, switch_handle_notification,
					MUIC_NOTIFY_DEV_USB);
#endif

#ifdef CONFIG_SEC_SYSFS
	if (IS_ERR(switch_device)) {
		pr_err("%s Failed to create device(switch)!\n", __func__);
		return -ENODEV;
	}

	/* create sysfs group */
	err = sysfs_create_group(&switch_device->kobj, &uart_sel_sysfs_group);
	if (err) {
		pr_err("%s: failed to create modemif sysfs attribute group\n",
			__func__);
		return -ENODEV;
	}
#endif

	return 0;
}

static int __exit uart_sel_remove(struct platform_device *pdev)
{
	/* TODO */
	return 0;
}

#ifdef CONFIG_PM
static int uart_sel_suspend(struct device *dev)
{
	/* TODO */
	return 0;
}

static int uart_sel_resume(struct device *dev)
{
	/* TODO */
	return 0;
}
#else
#define uart_sel_suspend NULL
#define uart_sel_resume NULL
#endif

static const struct dev_pm_ops uart_sel_pm_ops = {
	.suspend = uart_sel_suspend,
	.resume = uart_sel_resume,
};

static const struct of_device_id exynos_uart_sel_dt_match[] = {
		{ .compatible = "samsung,exynos-uart-sel", },
		{},
};
MODULE_DEVICE_TABLE(of, exynos_uart_sel_dt_match);

static struct platform_driver uart_sel_driver = {
	.probe		= uart_sel_probe,
	.remove		= uart_sel_remove,
	.driver		= {
		.name = "uart_sel",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(exynos_uart_sel_dt_match),
		.pm = &uart_sel_pm_ops,
	},
};
module_platform_driver(uart_sel_driver);

MODULE_DESCRIPTION("UART SEL driver");
MODULE_AUTHOR("<hy50.seo@samsung.com>");
MODULE_LICENSE("GPL");
