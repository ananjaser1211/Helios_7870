/*
 * sma7p10_charger.c - SMA7P10 Charger Driver
 *
 * Copyright (C) 2016 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "include/charger/sma7p10_charger.h"
#include <linux/of.h>
#include <linux/of_gpio.h>

#define DEBUG

#define ENABLE 1
#define DISABLE 0

static enum power_supply_property sma7p10_charger_props[] = {
	POWER_SUPPLY_PROP_HEALTH,				/* status */
	POWER_SUPPLY_PROP_ONLINE,				/* buck enable/disable */
	POWER_SUPPLY_PROP_CURRENT_MAX,			/* input current */
	POWER_SUPPLY_PROP_CURRENT_NOW,			/* charge current */
};

static void sma7p10_set_charger_state(
	struct sma7p10_charger_data *charger, int enable);

static int sma7p10_read_reg(struct i2c_client *client, u8 reg, u8 *dest)
{
	struct sma7p10_charger_data *charger = i2c_get_clientdata(client);
	int ret = 0;
 
	mutex_lock(&charger->io_lock);
	ret = i2c_smbus_read_byte_data(client, reg);
	mutex_unlock(&charger->io_lock);

	if (ret < 0) {
		pr_err("%s: can't read reg(0x%x), ret(%d)\n", __func__, reg, ret);
		return ret;
	}

	reg &= 0xFF;
	*dest = ret;

	return 0;
}

static int sma7p10_write_reg(struct i2c_client *client, u8 reg, u8 data)
{
	struct sma7p10_charger_data *charger = i2c_get_clientdata(client);
	int ret = 0;

	mutex_lock(&charger->io_lock);
	ret = i2c_smbus_write_byte_data(client, reg, data);
	mutex_unlock(&charger->io_lock);

	if (ret < 0)
		pr_err("%s: can't write reg(0x%x), ret(%d)\n", __func__, reg, ret);

	return ret;
}

static int sma7p10_update_reg(struct i2c_client *client, u8 reg, u8 val, u8 mask)
{
	struct sma7p10_charger_data *charger = i2c_get_clientdata(client);
	int ret = 0;

	mutex_lock(&charger->io_lock);
	ret = i2c_smbus_read_byte_data(client, reg);

	if (ret < 0)
		pr_err("%s: can't update reg(0x%x), ret(%d)\n", __func__, reg, ret);
	else {
		u8 old_val = ret & 0xFF;
		u8 new_val = (val & mask) | (old_val & (~mask));
		ret = i2c_smbus_write_byte_data(client, reg, new_val);
	}
	mutex_unlock(&charger->io_lock);

	return ret;
}

static void sma7p10_charger_test_read(
	struct sma7p10_charger_data *charger)
{
	u8 data = 0;
	u32 addr = 0;
	char str[1024]={0,};
	for (addr = 0x4d; addr <= 0x51; addr++) {
		sma7p10_read_reg(charger->i2c, addr, &data);
		sprintf(str + strlen(str), "[0x%02x]0x%02x, ", addr, data);
	}
	pr_info("%s: SMA7P10 : %s\n", __func__, str);
}

static int sma7p10_get_charger_state(
	struct sma7p10_charger_data *charger)
{
	u8 reg_data;

	sma7p10_read_reg(charger->i2c, SMA7P10_CHG_STATUS1, &reg_data);
	if (reg_data & VIN_OK_STATUS_MASK)
		return POWER_SUPPLY_STATUS_CHARGING;
	return POWER_SUPPLY_STATUS_NOT_CHARGING;
}

static int sma7p10_get_charger_health(struct sma7p10_charger_data *charger)
{
	u8 reg_data;

	sma7p10_read_reg(charger->i2c, SMA7P10_CHG_STATUS1, &reg_data);
	if (reg_data & VINOV_OK_STATUS_MASK) {
			pr_info("%s: VIN overvoltage\n", __func__);
			return POWER_SUPPLY_HEALTH_OVERVOLTAGE;
	} else if (reg_data & VINUV_OK_SHIFT) {
		pr_info("%s: VIN undervoltage\n", __func__);
		return POWER_SUPPLY_HEALTH_UNDERVOLTAGE;
	} else
		return POWER_SUPPLY_HEALTH_GOOD;
}

static void sma7p10_set_charger_state(
	struct sma7p10_charger_data *charger, int enable)
{
	pr_info("%s: BUCK_EN(%s)\n", enable > 0 ? "ENABLE" : "DISABLE", __func__);

	if (enable)
		sma7p10_update_reg(charger->i2c,
			SMA7P10_SC_CTRL1, ENI2C_MASK, ENI2C_MASK);
	else
		sma7p10_update_reg(charger->i2c, SMA7P10_SC_CTRL1, 0, ENI2C_MASK);

	sma7p10_charger_test_read(charger);
}

static int sma7p10_get_charge_current(struct sma7p10_charger_data *charger)
{
	u8 reg_data;
	int charge_current;

	sma7p10_read_reg(charger->i2c, SMA7P10_SC_CTRL0, &reg_data);
	charge_current = reg_data * 10;

	return charge_current;
}

static void sma7p10_set_charge_current(struct sma7p10_charger_data *charger,
	int charge_current)
{
	u8 reg_data;

	if (!charge_current) {
		reg_data = 0x00;
	} else {
		charge_current = (charge_current > 2500) ? 2500 : charge_current;
		reg_data = charge_current  / 10;
	}

	sma7p10_update_reg(charger->i2c,
			SMA7P10_SC_CTRL0, reg_data, CHG_CURRENT_MASK);
	pr_info("%s: charge_current(%d)\n", __func__, charge_current);
}

static void sma7p10_charger_initialize(struct sma7p10_charger_data *charger)
{
	pr_info("%s: \n", __func__);

	/* Battery under-voltage threshold */
	sma7p10_update_reg(charger->i2c, SMA7P10_SC_CTRL5, V3P14, CPQ_MASK);

	/* Battery over_voltage threshold */	
	sma7p10_update_reg(charger->i2c, SMA7P10_SC_CTRL5, 0x01, CV_FLG_MASK);

	/* Soft down time */	
	sma7p10_update_reg(charger->i2c,
		SMA7P10_SC_CTRL6, (0x01) << SOFT_DOWN_SHIFT, SOFT_DOWN_MASK);
}

static irqreturn_t sma7p10_irq_handler(int irq, void *data)
{
//	struct sma7p10_charger_data *charger = data;

	pr_info("%s: \n", __func__);

	return IRQ_HANDLED;
}

static int sma7p10_chg_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val)
{
	struct sma7p10_charger_data *charger =
		container_of(psy, struct sma7p10_charger_data, psy_chg);
	enum power_supply_ext_property ext_psp = psp;

	switch (psp) {
	case POWER_SUPPLY_PROP_HEALTH:
		if (charger->cable_type == POWER_SUPPLY_TYPE_HV_MAINS ||
			charger->cable_type == POWER_SUPPLY_TYPE_HV_MAINS_12V) {
			sma7p10_charger_test_read(charger);
			val->intval = sma7p10_get_charger_health(charger);
		} else
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = sma7p10_get_charger_state(charger);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = sma7p10_get_charge_current(charger);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		return -ENODATA;
	case POWER_SUPPLY_PROP_MAX ... POWER_SUPPLY_EXT_PROP_MAX:
		switch (ext_psp) {
		case POWER_SUPPLY_EXT_PROP_CHECK_SLAVE_I2C:
			{
			u8 reg_data;
			val->intval = (sma7p10_read_reg(charger->i2c, SMA7P10_SC_CTRL0, &reg_data) == 0);
			}
			break;
		case POWER_SUPPLY_EXT_PROP_CHECK_MULTI_CHARGE:
			val->intval = (sma7p10_get_charger_health(charger) == POWER_SUPPLY_HEALTH_GOOD) ?
				sma7p10_get_charger_state(charger) : POWER_SUPPLY_STATUS_NOT_CHARGING;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int sma7p10_chg_set_property(struct power_supply *psy,
	enum power_supply_property psp, const union power_supply_propval *val)
{
	struct sma7p10_charger_data *charger =
		container_of(psy, struct sma7p10_charger_data, psy_chg);

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		charger->is_charging = 
			(val->intval == SEC_BAT_CHG_MODE_CHARGING) ? ENABLE : DISABLE;
		sma7p10_set_charger_state(charger, charger->is_charging);
		if (charger->is_charging == DISABLE)
			sma7p10_set_charge_current(charger, 0);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		charger->charging_current = val->intval;
		sma7p10_set_charge_current(charger, charger->charging_current);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		charger->siop_level = val->intval;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		charger->cable_type = val->intval;
		if (val->intval != POWER_SUPPLY_TYPE_BATTERY) {
			sma7p10_charger_initialize(charger);
		}
		break;
	case POWER_SUPPLY_PROP_STATUS:
	case POWER_SUPPLY_PROP_CURRENT_FULL:
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
	case POWER_SUPPLY_PROP_HEALTH:
		return -ENODATA;
	default:
		return -EINVAL;
	}

	return 0;
}

static int sma7p10_charger_parse_dt(struct device *dev,
	sec_charger_platform_data_t *pdata)
{
	struct device_node *np = of_find_node_by_name(NULL, "sma7p10-charger");
	int ret = 0;

	if (!np) {
		pr_err("%s: np is NULL\n", __func__);
		return -1;
	} else {
		ret = of_get_named_gpio_flags(np, "sma7p10-charger,irq-gpio",
			0, NULL);
		if (ret < 0) {
			pr_err("%s: sma7p10-charger,irq-gpio is empty\n", __func__);
			pdata->irq_gpio = 0;
		} else {
			pdata->irq_gpio = ret;
			pr_info("%s: irq-gpio = %d\n", __func__, pdata->irq_gpio);
		}

		pdata->chg_gpio_en = of_get_named_gpio(np,
			"sma7p10-charger,chg_gpio_en", 0);
		if (pdata->chg_gpio_en < 0) {
			pr_err("%s : cannot get chg_gpio_en : %d\n",
				__func__, pdata->chg_gpio_en);
			return -ENODATA;	
		} else {
			pr_info("%s: chg_gpio_en : %d\n", __func__, pdata->chg_gpio_en);
		}
	}

	np = of_find_node_by_name(NULL, "battery");
	if (!np) {
		pr_err("%s np is NULL\n", __func__);
		return -1;
	}

	return 0;
}

static ssize_t sma7p10_store_addr(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct sma7p10_charger_data *charger = container_of(psy, struct sma7p10_charger_data, psy_chg);
	int x;
	if (sscanf(buf, "0x%x\n", &x) == 1) {
		charger->addr = x;
	}
	return count;
}

static ssize_t sma7p10_show_addr(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct sma7p10_charger_data *charger = container_of(psy, struct sma7p10_charger_data, psy_chg);
	return sprintf(buf, "0x%x\n", charger->addr);
}

static ssize_t sma7p10_store_size(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct sma7p10_charger_data *charger = container_of(psy, struct sma7p10_charger_data, psy_chg);
	int x;
	if (sscanf(buf, "%d\n", &x) == 1) {
		charger->size = x;
	}
	return count;
}

static ssize_t sma7p10_show_size(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct sma7p10_charger_data *charger = container_of(psy, struct sma7p10_charger_data, psy_chg);
	return sprintf(buf, "0x%x\n", charger->size);
}

static ssize_t sma7p10_store_data(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct sma7p10_charger_data *charger = container_of(psy, struct sma7p10_charger_data, psy_chg);
	int x;

	if (sscanf(buf, "0x%x", &x) == 1) {
		u8 data = x;
		if (sma7p10_write_reg(charger->i2c, charger->addr, data) < 0)
		{
			dev_info(charger->dev,
					"%s: addr: 0x%x write fail\n", __func__, charger->addr);
		}
	}
	return count;
}

static ssize_t sma7p10_show_data(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct sma7p10_charger_data *charger = container_of(psy, struct sma7p10_charger_data, psy_chg);
	u8 data;
	int i, count = 0;;
	if (charger->size == 0)
		charger->size = 1;

	for (i = 0; i < charger->size; i++) {
		if (sma7p10_read_reg(charger->i2c, charger->addr+i, &data) < 0) {
			dev_info(charger->dev,
					"%s: read fail\n", __func__);
			count += sprintf(buf+count, "addr: 0x%x read fail\n", charger->addr+i);
			continue;
		}
		count += sprintf(buf+count, "addr: 0x%x, data: 0x%x\n", charger->addr+i,data);
	}
	return count;
}

static DEVICE_ATTR(addr, 0644, sma7p10_show_addr, sma7p10_store_addr);
static DEVICE_ATTR(size, 0644, sma7p10_show_size, sma7p10_store_size);
static DEVICE_ATTR(data, 0644, sma7p10_show_data, sma7p10_store_data);

static struct attribute *sma7p10_attributes[] = {
	&dev_attr_addr.attr,
	&dev_attr_size.attr,
	&dev_attr_data.attr,
	NULL
};

static const struct attribute_group sma7p10_attr_group = {
	.attrs = sma7p10_attributes,
};

static int sma7p10_charger_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	struct device_node *of_node = client->dev.of_node;
	struct sma7p10_charger_data *charger;
	sec_charger_platform_data_t *pdata = client->dev.platform_data;
	int ret = 0;

	pr_info("%s: SMA7P10 Charger Driver Loading\n", __func__);

	if (of_node) {
		pdata = devm_kzalloc(&client->dev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata) {
			pr_err("%s: Failed to allocate memory\n", __func__);
			return -ENOMEM;
		}
		ret = sma7p10_charger_parse_dt(&client->dev, pdata);
		if (ret < 0)
			goto err_parse_dt;
	} else {
		pdata = client->dev.platform_data;
	}

	charger = kzalloc(sizeof(*charger), GFP_KERNEL);
	if (!charger) {
		pr_err("%s: Failed to allocate memory\n", __func__);
		ret = -ENOMEM;
		goto err_nomem;
	}

	mutex_init(&charger->io_lock);
	charger->dev = &client->dev;
	ret = i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA |
		I2C_FUNC_SMBUS_WORD_DATA | I2C_FUNC_SMBUS_I2C_BLOCK);
	if (!ret) {
		ret = i2c_get_functionality(client->adapter);
		dev_err(charger->dev, "I2C functionality is not supported.\n");
		ret = -ENOSYS;
		goto err_i2cfunc_not_support;
	}
	charger->i2c = client;
	charger->pdata = pdata;
	i2c_set_clientdata(client, charger);

	charger->psy_chg.name			= "sma7p10-charger";
	charger->psy_chg.type			= POWER_SUPPLY_TYPE_UNKNOWN;
	charger->psy_chg.get_property	= sma7p10_chg_get_property;
	charger->psy_chg.set_property	= sma7p10_chg_set_property;
	charger->psy_chg.properties		= sma7p10_charger_props;
	charger->psy_chg.num_properties	= ARRAY_SIZE(sma7p10_charger_props);

	/* sma7p10_charger_initialize(charger); */
	charger->cable_type = POWER_SUPPLY_TYPE_BATTERY;

	ret = power_supply_register(charger->dev, &charger->psy_chg);
	if (ret) {
		pr_err("%s: Failed to Register psy_chg\n", __func__);
		ret = -1;
		goto err_power_supply_register;
	}

	charger->wqueue =
		create_singlethread_workqueue(dev_name(charger->dev));
	if (!charger->wqueue) {
		pr_err("%s: Fail to Create Workqueue\n", __func__);
		ret = -1;
		goto err_create_wqueue;
	}

	if (pdata->irq_gpio) {
		charger->chg_irq = gpio_to_irq(pdata->irq_gpio);

		ret = request_threaded_irq(charger->chg_irq, NULL,
			sma7p10_irq_handler,
			IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
			"sma7p10-irq", charger);
		if (ret < 0) {
			pr_err("%s: Failed to Request IRQ(%d)\n", __func__, ret);
			goto err_req_irq;
		}

		ret = enable_irq_wake(charger->chg_irq);
		if (ret < 0)
			pr_err("%s: Failed to Enable Wakeup Source(%d)\n",
				__func__, ret);
	}

	ret = sysfs_create_group(&charger->psy_chg.dev->kobj, &sma7p10_attr_group);
	if (ret) {
		dev_info(&client->dev,
			"%s: sysfs_create_group failed\n", __func__);
	}

	/* sma7p10_charger_initialize(charger); */
	sma7p10_charger_test_read(charger);
	sma7p10_get_charge_current(charger);

	pr_info("%s: SM7P10 Charger Driver Loaded\n", __func__);

	return 0;

err_req_irq:
err_create_wqueue:
	power_supply_unregister(&charger->psy_chg);
err_power_supply_register:
err_i2cfunc_not_support:
	mutex_destroy(&charger->io_lock);
	kfree(charger);
err_nomem:
err_parse_dt:
	kfree(pdata);

	return ret;
}

static int sma7p10_charger_remove(struct i2c_client *client)
{
	struct sma7p10_charger_data *charger = i2c_get_clientdata(client);

	free_irq(charger->chg_irq, NULL);
	destroy_workqueue(charger->wqueue);
	power_supply_unregister(&charger->psy_chg);
	mutex_destroy(&charger->io_lock);
	kfree(charger->pdata);
	kfree(charger);

	return 0;
}

static void sma7p10_charger_shutdown(struct i2c_client *client)
{
}

static const struct i2c_device_id sma7p10_charger_id_table[] = {
	{"sma7p10-charger", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, sma7p10_id_table);

#if defined(CONFIG_PM)
static int sma7p10_charger_suspend(struct device *dev)
{
	struct i2c_client *i2c = container_of(dev, struct i2c_client, dev);
	struct sma7p10_charger_data *charger = i2c_get_clientdata(i2c);

	if (charger->chg_irq) {
		if (device_may_wakeup(dev))
			enable_irq_wake(charger->chg_irq);
		disable_irq(charger->chg_irq);
	}

	return 0;
}

static int sma7p10_charger_resume(struct device *dev)
{
	struct i2c_client *i2c = container_of(dev, struct i2c_client, dev);
	struct sma7p10_charger_data *charger = i2c_get_clientdata(i2c);

	if (charger->chg_irq) {
		if (device_may_wakeup(dev))
			disable_irq_wake(charger->chg_irq);
		enable_irq(charger->chg_irq);
	}

	return 0;
}
#else
#define sma7p10_charger_suspend		NULL
#define sma7p10_charger_resume		NULL
#endif /* CONFIG_PM */

const struct dev_pm_ops sma7p10_pm = {
	.suspend = sma7p10_charger_suspend,
	.resume = sma7p10_charger_resume,
};

#ifdef CONFIG_OF
static struct of_device_id sma7p10_charger_match_table[] = {
	{.compatible = "samsung,sma7p10-charger"},
	{},
};
#else
#define sma7p10_charger_match_table NULL
#endif

static struct i2c_driver sma7p10_charger_driver = {
	.driver = {
		.name	= "sma7p10-charger",
		.owner	= THIS_MODULE,
		.of_match_table = sma7p10_charger_match_table,
#if defined(CONFIG_PM)
		.pm = &sma7p10_pm,
#endif /* CONFIG_PM */
	},
	.probe		= sma7p10_charger_probe,
	.remove		= sma7p10_charger_remove,
	.shutdown	= sma7p10_charger_shutdown,
	.id_table	= sma7p10_charger_id_table,
};

static int __init sma7p10_charger_init(void)
{
	pr_info("%s: \n", __func__);
	return i2c_add_driver(&sma7p10_charger_driver);
}

static void __exit sma7p10_charger_exit(void)
{
	pr_info("%s: \n", __func__);
	i2c_del_driver(&sma7p10_charger_driver);
}

module_init(sma7p10_charger_init);
module_exit(sma7p10_charger_exit);

MODULE_DESCRIPTION("Samsung SMA7P10 Charger Driver");
MODULE_AUTHOR("Samsung Electronics");
MODULE_LICENSE("GPL");
