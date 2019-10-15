/* Copyright (c) 2009-2010, 2013-2014 The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
/*
 * Bluetooth Power Switch Module
 * controls power to external Bluetooth device
 * with interface to power management device
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/rfkill.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/bluetooth-power.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
//#include <net/cnss.h> // comment out until WLAN driver is ready to expose API
#include <linux/hrtimer.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/serial_core.h>
#include <linux/wakelock.h>
#include <linux/serial_s3c.h>
#include <soc/samsung/exynos-powermode.h>
#define BT_UPORT 0
#define BT_LPM_ENABLE
//#define ACTIVE_LOW_WAKE_HOST_GPIO
#define STATUS_IDLE	1
#define STATUS_BUSY	0

#ifdef BT_LPM_ENABLE
extern s3c_wake_peer_t s3c2410_serial_wake_peer[CONFIG_SERIAL_SAMSUNG_UARTS];
static int bt_wake_state = -1;
struct _bt_lpm {
	int host_wake;
	int dev_wake;

	struct hrtimer enter_lpm_timer;
	ktime_t enter_lpm_delay;

	struct uart_port *uport;

	struct wake_lock host_wake_lock;
	struct wake_lock bt_wake_lock;
} bt_lpm;
#endif // BT_LPM_ENABLE

#define BT_PWR_DBG(fmt, arg...)  pr_debug("%s: " fmt "\n" , __func__ , ## arg)
#define BT_PWR_INFO(fmt, arg...) pr_info("%s: " fmt "\n" , __func__ , ## arg)
#define BT_PWR_ERR(fmt, arg...)  pr_err("%s: " fmt "\n" , __func__ , ## arg)

int idle_ip_index;

static struct of_device_id bt_power_match_table[] = {
	{	.compatible = "qca,ar3002" },
	{	.compatible = "qca,qca6174" },
	{}
};

static struct bluetooth_power_platform_data *bt_power_pdata;
static struct platform_device *btpdev;
static bool previous;

static int bt_vreg_init(struct bt_power_vreg_data *vreg)
{
	int rc = 0;
	struct device *dev = &btpdev->dev;

	BT_PWR_DBG("vreg_get for : %s", vreg->name);

	/* Get the regulator handle */
	vreg->reg = regulator_get(dev, vreg->name);
	if (IS_ERR(vreg->reg)) {
		rc = PTR_ERR(vreg->reg);
		pr_err("%s: regulator_get(%s) failed. rc=%d\n",
			__func__, vreg->name, rc);
		goto out;
	}

	if ((regulator_count_voltages(vreg->reg) > 0)
			&& (vreg->low_vol_level) && (vreg->high_vol_level))
		vreg->set_voltage_sup = 1;

out:
	return rc;
}

static int bt_vreg_enable(struct bt_power_vreg_data *vreg)
{
	int rc = 0;

	BT_PWR_DBG("vreg_en for : %s", vreg->name);

	if (!vreg->is_enabled) {
		if (vreg->set_voltage_sup) {
			rc = regulator_set_voltage(vreg->reg,
						vreg->low_vol_level,
						vreg->high_vol_level);
			if (rc < 0) {
				BT_PWR_ERR("vreg_set_vol(%s) failed rc=%d\n",
						vreg->name, rc);
				goto out;
			}
		}

		rc = regulator_enable(vreg->reg);
		if (rc < 0) {
			BT_PWR_ERR("regulator_enable(%s) failed. rc=%d\n",
					vreg->name, rc);
			goto out;
		}
		vreg->is_enabled = true;
	}
out:
	return rc;
}

static int bt_vreg_disable(struct bt_power_vreg_data *vreg)
{
	int rc = 0;

	if (!vreg)
		return rc;

	BT_PWR_DBG("vreg_disable for : %s", vreg->name);

	if (vreg->is_enabled) {
		rc = regulator_disable(vreg->reg);
		if (rc < 0) {
			BT_PWR_ERR("regulator_disable(%s) failed. rc=%d\n",
					vreg->name, rc);
			goto out;
		}
		vreg->is_enabled = false;

		if (vreg->set_voltage_sup) {
			/* Set the min voltage to 0 */
			rc = regulator_set_voltage(vreg->reg,
						0,
						vreg->high_vol_level);
			if (rc < 0) {
				BT_PWR_ERR("vreg_set_vol(%s) failed rc=%d\n",
						vreg->name, rc);
				goto out;

			}
		}
	}
out:
	return rc;
}

static int bt_configure_vreg(struct bt_power_vreg_data *vreg)
{
	int rc = 0;

	BT_PWR_DBG("config %s", vreg->name);

	/* Get the regulator handle for vreg */
	if (!(vreg->reg)) {
		rc = bt_vreg_init(vreg);
		if (rc < 0)
			return rc;
	}
	rc = bt_vreg_enable(vreg);

	return rc;
}

static int bt_configure_gpios(int on)
{
	int rc = 0;
	int bt_reset_gpio = bt_power_pdata->bt_gpio_sys_rst;

	BT_PWR_DBG("%s  bt_gpio= %d on: %d", __func__, bt_reset_gpio, on);

	if (on) {
		exynos_update_ip_idle_status(idle_ip_index, STATUS_BUSY);
		gpio_set_value(bt_reset_gpio, 0);
		msleep(50);
		gpio_set_value(bt_reset_gpio, 1);
		msleep(50);
	} else {
		gpio_set_value(bt_reset_gpio, 0);
		exynos_update_ip_idle_status(idle_ip_index, STATUS_IDLE);
		msleep(100);
	}
	return rc;
}

static int bluetooth_power(int on)
{
	int rc = 0;

	BT_PWR_DBG("bluetooth_power on: %d", on);

	if (on) {
		// on is 1 for BT ENABLE

#ifdef BT_LPM_ENABLE
		if ( irq_set_irq_wake(bt_power_pdata->irq, 1)) {
			BT_PWR_ERR("[BT] Set_irq_wake failed.\n");
			goto gpio_fail;
		}
#endif // BT_LPM_ENABLE

#ifdef GPIO_3_3V_DCDC_EN
		if (bt_power_pdata->wlan_dcdc_en) {
			rc = bt_configure_vreg(bt_power_pdata->wlan_dcdc_en);
			if (rc < 0) {
				BT_PWR_ERR("wlan_dcdc_en config failed");
				goto wlan_dcdc_fail;
			}
		}
#endif // GPIO_3_3V_DCDC_EN

		if (bt_power_pdata->bt_gpio_sys_rst) {
			rc = bt_configure_gpios(on);
			if (rc < 0) {
				BT_PWR_ERR("bt_power gpio config failed");
				goto gpio_fail;
			}
		}
	} else {
		// on is 0 for BT DISABLE
#ifdef BT_LPM_ENABLE
		if (gpio_get_value(bt_power_pdata->bt_gpio_sys_rst) && irq_set_irq_wake(bt_power_pdata->irq, 0)) {
			BT_PWR_ERR("[BT] Release_irq_wake failed.\n");
			goto gpio_fail;
		}
#endif //BT_LPM_ENABLE	

		bt_configure_gpios(on);
gpio_fail:
		if (bt_power_pdata->bt_gpio_sys_rst)
			gpio_free(bt_power_pdata->bt_gpio_sys_rst);
		if (bt_power_pdata->bt_gpio_bt_wake)
			gpio_free(bt_power_pdata->bt_gpio_bt_wake);
		if (bt_power_pdata->bt_gpio_host_wake)
			gpio_free(bt_power_pdata->bt_gpio_host_wake);
#ifdef GPIO_3_3V_DCDC_EN
wlan_dcdc_fail:
		bt_vreg_disable(bt_power_pdata->wlan_dcdc_en);
#endif //GPIO_3_3V_DCDC_EN

#ifdef BT_LPM_ENABLE
		BT_PWR_ERR("[BT] bluetooth_power will be off. release host wakelock in 1s\n");
		wake_lock_timeout(&bt_lpm.host_wake_lock, HZ/2);
#endif //BT_LPM_ENABLE
	}
	return rc;
}

static int bluetooth_toggle_radio(void *data, bool blocked)
{
	int ret = 0;
	int (*power_control)(int enable);

	power_control =
		((struct bluetooth_power_platform_data *)data)->bt_power_setup;

	if (previous != blocked)
		ret = (*power_control)(!blocked);
	if (!ret)
		previous = blocked;
	return ret;
}

static const struct rfkill_ops bluetooth_power_rfkill_ops = {
	.set_block = bluetooth_toggle_radio,
};

//#ifdef CONFIG_CNSS
#if 0
static ssize_t enable_extldo(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	int ret;
	bool enable = false;
	struct cnss_platform_cap cap;

	ret = cnss_get_platform_cap(&cap);
	if (ret) {
		BT_PWR_ERR("Platform capability info from CNSS not available!");
		enable = false;
	} else if (!ret && (cap.cap_flag & CNSS_HAS_EXTERNAL_SWREG)) {
		enable = true;
	}
	return snprintf(buf, 6, "%s", (enable ? "true" : "false"));
}
#else
static ssize_t enable_extldo(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	return snprintf(buf, 6, "%s", "false");
}
#endif

static DEVICE_ATTR(extldo, S_IRUGO, enable_extldo, NULL);

static int bluetooth_power_rfkill_probe(struct platform_device *pdev)
{
	struct rfkill *rfkill;
	int ret;

	rfkill = rfkill_alloc("bt_power", &pdev->dev, RFKILL_TYPE_BLUETOOTH,
			      &bluetooth_power_rfkill_ops,
			      pdev->dev.platform_data);

	if (!rfkill) {
		dev_err(&pdev->dev, "rfkill allocate failed\n");
		return -ENOMEM;
	}

	/* add file into rfkill0 to handle LDO27 */
	ret = device_create_file(&pdev->dev, &dev_attr_extldo);
	if (ret < 0)
		BT_PWR_ERR("device create file error!");

	/* force Bluetooth off during init to allow for user control */
	rfkill_init_sw_state(rfkill, 1);
	previous = 1;

	ret = rfkill_register(rfkill);
	if (ret) {
		dev_err(&pdev->dev, "rfkill register failed=%d\n", ret);
		rfkill_destroy(rfkill);
		return ret;
	}

	platform_set_drvdata(pdev, rfkill);

	return 0;
}

static void bluetooth_power_rfkill_remove(struct platform_device *pdev)
{
	struct rfkill *rfkill;

	dev_dbg(&pdev->dev, "%s\n", __func__);

	rfkill = platform_get_drvdata(pdev);
	if (rfkill)
	{
		rfkill_unregister(rfkill);
		rfkill_destroy(rfkill);
	}
	platform_set_drvdata(pdev, NULL);
}

#define MAX_PROP_SIZE 32
static int bt_dt_parse_vreg_info(struct device *dev,
		struct bt_power_vreg_data **vreg_data, const char *vreg_name)
{
	int len, ret = 0;
	const __be32 *prop;
	char prop_name[MAX_PROP_SIZE];
	struct bt_power_vreg_data *vreg;
	struct device_node *np = dev->of_node;

	BT_PWR_DBG("vreg dev tree parse for %s", vreg_name);

	snprintf(prop_name, MAX_PROP_SIZE, "%s-supply", vreg_name);
	if (of_parse_phandle(np, prop_name, 0)) {
		vreg = devm_kzalloc(dev, sizeof(*vreg), GFP_KERNEL);
		if (!vreg) {
			dev_err(dev, "No memory for vreg: %s\n", vreg_name);
			ret = -ENOMEM;
			goto err;
		}

		vreg->name = vreg_name;

		snprintf(prop_name, MAX_PROP_SIZE,
				"%s-voltage-level", vreg_name);
		prop = of_get_property(np, prop_name, &len);
		if (!prop || (len != (2 * sizeof(__be32)))) {
			dev_warn(dev, "%s %s property\n",
				prop ? "invalid format" : "no", prop_name);
		} else {
			vreg->low_vol_level = be32_to_cpup(&prop[0]);
			vreg->high_vol_level = be32_to_cpup(&prop[1]);
		}

		*vreg_data = vreg;
		BT_PWR_DBG("%s: vol=[%d %d]uV\n",
			vreg->name, vreg->low_vol_level,
			vreg->high_vol_level);
	} else
		BT_PWR_INFO("%s: is not provided in device tree", vreg_name);

err:
	return ret;
}

static int bt_power_populate_dt_pinfo(struct platform_device *pdev)
{
	int rc;

	BT_PWR_DBG("bt_power_populate_dt_pinfo. \n");

	if (!bt_power_pdata)
		return -ENOMEM;

	if (pdev->dev.of_node) {
		
		bt_power_pdata->bt_gpio_sys_rst = of_get_gpio(pdev->dev.of_node, 0);
		if (bt_power_pdata->bt_gpio_sys_rst < 0) {
			BT_PWR_ERR("bt-reset-gpio not provided in device tree");
			return bt_power_pdata->bt_gpio_sys_rst;
		}
		if ((rc = gpio_request(bt_power_pdata->bt_gpio_sys_rst, "bten_gpio")) < 0) {
			BT_PWR_ERR("bt-reset-gpio request failed.\n");
			return rc;	
		}

		bt_power_pdata->bt_gpio_bt_wake = of_get_gpio(pdev->dev.of_node, 1);
		if (bt_power_pdata->bt_gpio_bt_wake < 0) {
			BT_PWR_ERR("bt-wake-gpio not provided in device tree");
			return bt_power_pdata->bt_gpio_bt_wake;
		}
		if ((rc = gpio_request(bt_power_pdata->bt_gpio_bt_wake, "btwake_gpio")) < 0) {
			BT_PWR_ERR("bt-wake-gpio request failed.\n");
			return rc;	
		}
		
		bt_power_pdata->bt_gpio_host_wake = of_get_gpio(pdev->dev.of_node, 2);
		if (bt_power_pdata->bt_gpio_host_wake < 0) {
			BT_PWR_ERR("bt-hostwake-gpio not provided in device tree");
			return bt_power_pdata->bt_gpio_host_wake;
		}
		if ((rc = gpio_request(bt_power_pdata->bt_gpio_host_wake, "bthostwake_gpio")) < 0) {
			BT_PWR_ERR("host-wake-gpio request failed.\n");
			return rc;	
		}

		gpio_direction_input(bt_power_pdata->bt_gpio_host_wake);
		gpio_direction_output(bt_power_pdata->bt_gpio_bt_wake, 0);
		gpio_direction_output(bt_power_pdata->bt_gpio_sys_rst, 0);

#ifdef GPIO_3_3V_DCDC_EN
		rc = bt_dt_parse_vreg_info(&pdev->dev,
					&bt_power_pdata->wlan_dcdc_en,
					"vdd-wlan");
		if (rc < 0)
			return rc;		
#endif //GPIO_3_3V_DCDC_EN

	}

	bt_power_pdata->bt_power_setup = bluetooth_power;

	return 0;
}

#ifdef BT_LPM_ENABLE
static void set_wake_locked(int wake)
{
	if (wake)
		wake_lock(&bt_lpm.bt_wake_lock);

	gpio_set_value(bt_power_pdata->bt_gpio_bt_wake, wake);
	bt_lpm.dev_wake = wake;

	if (bt_wake_state != wake)
	{
		BT_PWR_ERR("[BT] set_wake_locked value = %d\n", wake);
		bt_wake_state = wake;
	}
}

static enum hrtimer_restart enter_lpm(struct hrtimer *timer)
{
	if (bt_lpm.uport != NULL)
		set_wake_locked(0);

    if (bt_lpm.host_wake == 0)
	    exynos_update_ip_idle_status(idle_ip_index, STATUS_IDLE);
	wake_lock_timeout(&bt_lpm.bt_wake_lock, HZ/2);

	return HRTIMER_NORESTART;
}

void qcomm_bt_lpm_exit_lpm_locked(struct uart_port *uport)
{
	bt_lpm.uport = uport;

	hrtimer_try_to_cancel(&bt_lpm.enter_lpm_timer);
	exynos_update_ip_idle_status(idle_ip_index, STATUS_BUSY);
	set_wake_locked(1);

	hrtimer_start(&bt_lpm.enter_lpm_timer, bt_lpm.enter_lpm_delay,
		HRTIMER_MODE_REL);
}

static void update_host_wake_locked(int host_wake)
{
	if (host_wake == bt_lpm.host_wake)
		return;

	bt_lpm.host_wake = host_wake;

	if (host_wake) {
		exynos_update_ip_idle_status(idle_ip_index, STATUS_BUSY);
		wake_lock(&bt_lpm.host_wake_lock);
	} else  {
		/* Take a timed wakelock, so that upper layers can take it.
		 * The chipset deasserts the hostwake lock, when there is no
		 * more data to send.
		 */
		BT_PWR_ERR("[BT] update_host_wake_locked host_wake is deasserted. release wakelock in 1s\n");
		wake_lock_timeout(&bt_lpm.host_wake_lock, HZ/2);
		
        if (bt_lpm.dev_wake == 0)
            exynos_update_ip_idle_status(idle_ip_index, STATUS_IDLE);
	}
}

static irqreturn_t host_wake_isr(int irq, void *dev)
{
	int host_wake;

	host_wake = gpio_get_value(bt_power_pdata->bt_gpio_host_wake);
	irq_set_irq_type(irq, host_wake ? IRQF_TRIGGER_FALLING : IRQF_TRIGGER_RISING);

#ifdef ACTIVE_LOW_WAKE_HOST_GPIO
	host_wake = host_wake ? 0 : 1;
#endif

	if (!bt_lpm.uport) {
		bt_lpm.host_wake = host_wake;
		BT_PWR_ERR("[BT] host_wake_isr uport is null\n");
		return IRQ_HANDLED;
	}

	update_host_wake_locked(host_wake);

	return IRQ_HANDLED;
}

static int qcomm_bt_lpm_init(struct platform_device *pdev)
{
	int ret;

	hrtimer_init(&bt_lpm.enter_lpm_timer, CLOCK_MONOTONIC,
			HRTIMER_MODE_REL);
	bt_lpm.enter_lpm_delay = ktime_set(1, 0);  /* 1 sec */ /*1->3*//*3->4*/
	bt_lpm.enter_lpm_timer.function = enter_lpm;

	bt_lpm.host_wake = 0;

	wake_lock_init(&bt_lpm.host_wake_lock, WAKE_LOCK_SUSPEND,
			 "BT_host_wake");
	wake_lock_init(&bt_lpm.bt_wake_lock, WAKE_LOCK_SUSPEND,
			 "BT_bt_wake");

	s3c2410_serial_wake_peer[BT_UPORT] = (s3c_wake_peer_t) qcomm_bt_lpm_exit_lpm_locked;


	bt_power_pdata->irq = gpio_to_irq(bt_power_pdata->bt_gpio_host_wake);
#ifdef ACTIVE_LOW_WAKE_HOST_GPIO
	ret = request_irq(bt_power_pdata->irq, host_wake_isr, IRQF_TRIGGER_FALLING, "bt_host_wake", NULL);
#else
	ret = request_irq(bt_power_pdata->irq, host_wake_isr, IRQF_TRIGGER_RISING, "bt_host_wake", NULL);
#endif

	if (ret) {
		BT_PWR_ERR("[BT] Request_host wake irq failed.\n");
		return ret;
	}

	return 0;
}
#endif

static int bt_power_probe(struct platform_device *pdev)
{
	int ret = 0;

	dev_dbg(&pdev->dev, "%s\n", __func__);

	bt_power_pdata =
		kzalloc(sizeof(struct bluetooth_power_platform_data),
			GFP_KERNEL);

	if (!bt_power_pdata) {
		BT_PWR_ERR("Failed to allocate memory");
		return -ENOMEM;
	}

	if (pdev->dev.of_node) {
		ret = bt_power_populate_dt_pinfo(pdev);
		if (ret < 0) {
			BT_PWR_ERR("Failed to populate device tree info");
			goto free_pdata;
		}
		pdev->dev.platform_data = bt_power_pdata;
	} else if (pdev->dev.platform_data) {
		/* Optional data set to default if not provided */
		if (!((struct bluetooth_power_platform_data *)
			(pdev->dev.platform_data))->bt_power_setup)
			((struct bluetooth_power_platform_data *)
				(pdev->dev.platform_data))->bt_power_setup =
						bluetooth_power;

		memcpy(bt_power_pdata, pdev->dev.platform_data,
			sizeof(struct bluetooth_power_platform_data));
	} else {
		BT_PWR_ERR("Failed to get platform data");
		goto free_pdata;
	}

	if (bluetooth_power_rfkill_probe(pdev) < 0)
		goto free_pdata;

	btpdev = pdev;

#ifdef BT_LPM_ENABLE
	qcomm_bt_lpm_init(pdev);
#endif
	idle_ip_index = exynos_get_idle_ip_index("bluetooth");
	exynos_update_ip_idle_status(idle_ip_index, STATUS_IDLE);

	return 0;

free_pdata:
	kfree(bt_power_pdata);
	return ret;
}

static int bt_power_remove(struct platform_device *pdev)
{
	dev_dbg(&pdev->dev, "%s\n", __func__);

	bluetooth_power_rfkill_remove(pdev);

	if (bt_power_pdata->bt_chip_pwd->reg)
		regulator_put(bt_power_pdata->bt_chip_pwd->reg);

	kfree(bt_power_pdata);
#ifdef BT_LPM_ENABLE		
	wake_lock_destroy(&bt_lpm.host_wake_lock);
	wake_lock_destroy(&bt_lpm.bt_wake_lock);
#endif
	return 0;
}

static struct platform_driver bt_power_driver = {
	.probe = bt_power_probe,
	.remove = bt_power_remove,
	.driver = {
		.name = "bt_power",
		.owner = THIS_MODULE,
		.of_match_table = bt_power_match_table,
	},
};
/*
static int __init bluetooth_power_init(void)
{
	int ret;

	ret = platform_driver_register(&bt_power_driver);
	return ret;
}

static void __exit bluetooth_power_exit(void)
{
	platform_driver_unregister(&bt_power_driver);
}
*/
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MSM Bluetooth power control driver");
MODULE_VERSION("1.40");

module_platform_driver(bt_power_driver);
/*
module_init(bluetooth_power_init);
module_exit(bluetooth_power_exit);
*/