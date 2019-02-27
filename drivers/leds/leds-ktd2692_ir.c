/*
 * LED driver - leds-ktd2692.c
 *
 * Copyright (C) 2013 Sunggeun Yim <sunggeun.yim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/pwm.h>
#include <linux/vmalloc.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/leds/leds-ktd2692.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_gpio.h>
#endif

extern struct class *camera_class; /*sys/class/camera*/
struct device *irled_dev;

static struct ktd2692_platform_data *global_ktd2692data;
struct device *ktd2692_dev;

int ktd2692_set_current(unsigned int value);

static void ktd2692_setGpio(int onoff)
{
	if (onoff) {
		__gpio_set_value(global_ktd2692data->flash_control, 1); /* enable IC */
		__gpio_set_value(global_ktd2692data->iris_led_tz, 1);
	} else {
		__gpio_set_value(global_ktd2692data->flash_control, 0); /* disable IC */
		__gpio_set_value(global_ktd2692data->iris_led_tz, 0);
	}
}

static void ktd2692_set_low_bit(void)
{
	__gpio_set_value(global_ktd2692data->flash_control, 0);
	udelay(T_L_LB);	/* 12ms */
	__gpio_set_value(global_ktd2692data->flash_control, 1);
	udelay(T_H_LB);	/* 4ms */
}

static void ktd2692_set_high_bit(void)
{
	__gpio_set_value(global_ktd2692data->flash_control, 0);
	udelay(T_L_HB);	/* 4ms */
	__gpio_set_value(global_ktd2692data->flash_control, 1);
	udelay(T_H_HB);	/* 12ms */
}

static int ktd2692_set_bit(unsigned int bit)
{
	if (bit) {
		ktd2692_set_high_bit();
	} else {
		ktd2692_set_low_bit();
	}
	return 0;
}

static int ktd2692_write_data(unsigned data)
{
	int err = 0;
	unsigned int bit = 0;

	/* Data Start Condition */
	__gpio_set_value(global_ktd2692data->flash_control, 1);
	ndelay(T_SOD*1000); //15us

	/* BIT 7*/
	bit = ((data>> 7) & 0x01);
	ktd2692_set_bit(bit);

	/* BIT 6 */
	bit = ((data>> 6) & 0x01);
	ktd2692_set_bit(bit);

	/* BIT 5*/
	bit = ((data>> 5) & 0x01);
	ktd2692_set_bit(bit);

	/* BIT 4 */
	bit = ((data>> 4) & 0x01);
	ktd2692_set_bit(bit);

	/* BIT 3*/
	bit = ((data>> 3) & 0x01);
	ktd2692_set_bit(bit);

	/* BIT 2 */
	bit = ((data>> 2) & 0x01);
	ktd2692_set_bit(bit);

	/* BIT 1*/
	bit = ((data>> 1) & 0x01);
	ktd2692_set_bit(bit);

	/* BIT 0 */
	bit = ((data>> 0) & 0x01);
	ktd2692_set_bit(bit);

	 __gpio_set_value(global_ktd2692data->flash_control, 0);
	ndelay(T_EOD_L*1000); //4us

	/* Data End Condition */
	__gpio_set_value(global_ktd2692data->flash_control, 1);
	udelay(T_EOD_H);

	return err;
}

int ktd2692_set_current(unsigned int value)
{
	int ret = 0;
	unsigned long flags = 0;

	LED_INFO("KTD2692-IR LED : E (set %d of 15 step)\n", value);

	if (value > 0) {
		/* It will be controlled by Aux GPIO Pin */
		global_ktd2692data->mode_status = KTD2692_DISABLES_MOVIE_FLASH_MODE;

		spin_lock_irqsave(&global_ktd2692data->int_lock, flags);
		ktd2692_write_data(global_ktd2692data->LVP_Voltage|
							KTD2692_ADDR_LVP_SETTING);
/*
		ktd2692_write_data(global_ktd2692data->movie_current_value|
							KTD2692_ADDR_MOVIE_CURRENT_SETTING);
		ktd2692_write_data(global_ktd2692data->flash_current_value|
							KTD2692_ADDR_FLASH_CURRENT_SETTING);
*/
		ktd2692_write_data(KTD2692_ADDR_MOVIE_CURRENT_SETTING | (value & 0x0F));
		ktd2692_write_data(KTD2692_ADDR_FLASH_CURRENT_SETTING | (value & 0x0F));

		ktd2692_write_data(global_ktd2692data->mode_status|
							KTD2692_ADDR_MOVIE_FLASHMODE_CONTROL);
		spin_unlock_irqrestore(&global_ktd2692data->int_lock, flags);

		ktd2692_setGpio(1);
	} else {
		ktd2692_setGpio(0);
	}
	LED_INFO("KTD2692-IR LED : X\n");

	return ret;
}
EXPORT_SYMBOL(ktd2692_set_current);

ssize_t ktd2692_store(struct device *dev,
			struct device_attribute *attr, const char *buf,
			size_t count)
{
	int value = 0;
	int ret = 0;
	unsigned long flags = 0;

	if ((buf == NULL) || kstrtouint(buf, 10, &value)) {
		return -1;
	}

	global_ktd2692data->sysfs_input_data = value;

	ret = gpio_request(global_ktd2692data->flash_control, "ktd2692_led_control");
	if (ret) {
		LED_ERROR("Failed to requeset ktd2692_led_control\n");
	}

	ret = gpio_request(global_ktd2692data->iris_led_tz, "ktd2692_iris_led_tz");
	if (ret) {
		LED_ERROR("Failed to requeset rt8547_led_en\n");
		return ret;
	}

	if (value <= 0) {
		LED_INFO("KTD2692-IR LED OFF. : E(%d)\n", value);

		global_ktd2692data->mode_status = KTD2692_DISABLES_MOVIE_FLASH_MODE;
		spin_lock_irqsave(&global_ktd2692data->int_lock, flags);
		ktd2692_write_data(global_ktd2692data->mode_status|
							KTD2692_ADDR_MOVIE_FLASHMODE_CONTROL);
		spin_unlock_irqrestore(&global_ktd2692data->int_lock, flags);

		ktd2692_setGpio(0);
		LED_INFO("KTD2692-IR LED OFF. : X(%d)\n", value);
	} else {
		LED_INFO("KTD2692-IR LED ON. : E(%d)\n", value);

		global_ktd2692data->mode_status = KTD2692_ENABLE_MOVIE_MODE;
		spin_lock_irqsave(&global_ktd2692data->int_lock, flags);
		ktd2692_write_data(global_ktd2692data->LVP_Voltage|
							KTD2692_ADDR_LVP_SETTING);

		ktd2692_write_data(global_ktd2692data->movie_current_value|
							KTD2692_ADDR_MOVIE_CURRENT_SETTING);
		ktd2692_write_data(global_ktd2692data->mode_status|
							KTD2692_ADDR_MOVIE_FLASHMODE_CONTROL);
		spin_unlock_irqrestore(&global_ktd2692data->int_lock, flags);

		ktd2692_setGpio(1);
		LED_INFO("KTD2692-IR LED ON. : X(%d)\n", value);
	}

	gpio_free(global_ktd2692data->flash_control);
	gpio_free(global_ktd2692data->iris_led_tz);

	return count;
}

ssize_t ktd2692_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", global_ktd2692data->sysfs_input_data);
}

static DEVICE_ATTR(ir_led, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH,
	ktd2692_show, ktd2692_store);

static int ktd2692_parse_dt(struct device *dev,
                                struct ktd2692_platform_data *pdata)
{
	struct device_node *dnode = dev->of_node;
	int ret = 0;

	/* Defulat Value */
	pdata->LVP_Voltage = KTD2692_DISABLE_LVP;
	pdata->flash_timeout = KTD2692_TIMER_1049ms;	/* default */
	pdata->min_current_value = KTD2692_MIN_CURRENT_240mA;
	pdata->movie_current_value = KTD2692_MOVIE_CURRENT3; /* 3/16 x (Iflash_max /3) */
	pdata->flash_current_value = KTD2692_FLASH_CURRENT16; /* 3/16 x Iflash_max */
	pdata->mode_status = KTD2692_DISABLES_MOVIE_FLASH_MODE;

	/* get gpio */
	pdata->flash_control = of_get_named_gpio(dnode, "flash_control", 0);
	if (!gpio_is_valid(pdata->flash_control)) {
		dev_err(dev, "failed to get flash_control\n");
		return -1;
	}

	pdata->iris_led_tz = of_get_named_gpio(dnode, "iris_led_tz", 0);
	if (!gpio_is_valid(pdata->iris_led_tz)) {
		dev_err(dev, "failed to get iris_led_tz\n");
		return -1;
	}

	return ret;
}

static int ktd2692_probe(struct platform_device *pdev)
{
	struct ktd2692_platform_data *pdata;
	int ret = 0;

	if (pdev->dev.of_node) {
		pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata) {
			dev_err(&pdev->dev, "Failed to allocate memory\n");
			return -ENOMEM;
		}
		ret = ktd2692_parse_dt(&pdev->dev, pdata);
		if (ret < 0) {
			return -EFAULT;
		}
	} else {
	pdata = pdev->dev.platform_data;
		if (pdata == NULL) {
			return -EFAULT;
		}
	}

	global_ktd2692data = pdata;
	ktd2692_dev = &pdev->dev;

	LED_INFO("KTD2692_LED Probe\n");

	irled_dev = device_create(camera_class, NULL, 0, NULL, "irled");
	if (IS_ERR(irled_dev)) {
		LED_ERROR("Failed to create device(irled)!\n");
	}

	if (device_create_file(irled_dev, &dev_attr_ir_led) < 0) {
		LED_ERROR("failed to create device file, %s\n",
				dev_attr_ir_led.attr.name);
	}

	spin_lock_init(&pdata->int_lock);

	return 0;
}
static int __devexit ktd2692_remove(struct platform_device *pdev)
{
	device_remove_file(irled_dev, &dev_attr_ir_led);

	device_destroy(camera_class, 0);
	class_destroy(camera_class);

	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id ktd2692_dt_ids[] = {
	{ .compatible = "ktd2692-ir",},
	{},
};
/*MODULE_DEVICE_TABLE(of, ktd2692_dt_ids);*/
#endif

static struct platform_driver ktd2692_driver = {
	.driver = {
		   .name = ktd2692_NAME,
		   .owner = THIS_MODULE,
#ifdef CONFIG_OF
		   .of_match_table = ktd2692_dt_ids,
#endif
		   },
	.probe = ktd2692_probe,
	.remove = ktd2692_remove,
};

static int __init ktd2692_init(void)
{
	return platform_driver_register(&ktd2692_driver);
}

static void __exit ktd2692_exit(void)
{
	platform_driver_unregister(&ktd2692_driver);
}

module_init(ktd2692_init);
module_exit(ktd2692_exit);

MODULE_AUTHOR("sunggeun yim <sunggeun.yim@samsung.com.com>");
MODULE_DESCRIPTION("KTD2692 driver");
MODULE_LICENSE("GPL");


