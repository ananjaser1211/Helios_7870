/* drivers/mfd/sm5703_core.c
 * SM5703 Multifunction Device Driver
 * Charger / Buck / LDOs / FlashLED
 *
 * Copyright (C) 2013 Siliconmitus Technology Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <linux/mfd/core.h>
#include <linux/mfd/sm5703.h>
#include <linux/mfd/sm5703_irq.h>
#include <linux/battery/charger/sm5703_charger.h>
#include <linux/errno.h>
#include <linux/version.h>
#include <linux/device.h>
#include <linux/pm.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_gpio.h>

#if defined(CONFIG_MFD_SM5703_USE_DT) || (LINUX_VERSION_CODE>=KERNEL_VERSION(3,10,0))
#define SM5703_USE_NEW_MFD_DT_API
#endif

#define SM5703_DECLARE_IRQ(irq) { \
	irq, irq, \
	irq##_NAME, IORESOURCE_IRQ }

#ifdef CONFIG_CHARGER_SM5703
const static struct resource sm5703_charger_res[] = {
	SM5703_DECLARE_IRQ(SM5703_AICL_IRQ),
	SM5703_DECLARE_IRQ(SM5703_BATOVP_IRQ),
	SM5703_DECLARE_IRQ(SM5703_LOWBATT_IRQ),
	SM5703_DECLARE_IRQ(SM5703_VBUSLIMIT_IRQ),
	SM5703_DECLARE_IRQ(SM5703_DISLIMIT_IRQ),
	SM5703_DECLARE_IRQ(SM5703_VSYSOLP_IRQ),
	SM5703_DECLARE_IRQ(SM5703_OTGFAIL_IRQ),
	SM5703_DECLARE_IRQ(SM5703_THEMREG_IRQ),
	SM5703_DECLARE_IRQ(SM5703_THEMSHDN_IRQ),
	SM5703_DECLARE_IRQ(SM5703_VSYSNG_IRQ),
	SM5703_DECLARE_IRQ(SM5703_VSYSOK_IRQ),
	SM5703_DECLARE_IRQ(SM5703_NOBAT_IRQ),
	SM5703_DECLARE_IRQ(SM5703_PRETMROFF_IRQ),
	SM5703_DECLARE_IRQ(SM5703_FASTTMROFF_IRQ),
	SM5703_DECLARE_IRQ(SM5703_CHGON_IRQ),
	SM5703_DECLARE_IRQ(SM5703_Q4FULLON_IRQ),
	SM5703_DECLARE_IRQ(SM5703_TOPOFF_IRQ),
	SM5703_DECLARE_IRQ(SM5703_DONE_IRQ),
	SM5703_DECLARE_IRQ(SM5703_CHGRSTF_IRQ),
};

static struct mfd_cell sm5703_charger_devs[] = {
	{
		.name			= "sm5703-charger",
		.num_resources	= ARRAY_SIZE(sm5703_charger_res),
		.id				= -1,
		.resources		= sm5703_charger_res,
#ifdef CONFIG_OF
		.of_compatible = "siliconmitus,sm5703-charger"
#endif /* CONFIG_OF */
	},
};
#endif /*CONFIG_CHARGER_SM5703*/

#ifdef CONFIG_FLED_SM5703
const static struct resource sm5703_fled_res[] = {
	SM5703_DECLARE_IRQ(SM5703_FLEDSHORT_IRQ),
	SM5703_DECLARE_IRQ(SM5703_FLEDOPEN_IRQ),
	SM5703_DECLARE_IRQ(SM5703_BOOSTPOK_NG_IRQ),
	SM5703_DECLARE_IRQ(SM5703_BOOSTPOK_IRQ),
	SM5703_DECLARE_IRQ(SM5703_ABSTMRON_IRQ),
};
static struct mfd_cell sm5703_fled_devs[] = {
	{
		.name			= "sm5703-fled",
		.num_resources	= ARRAY_SIZE(sm5703_fled_res),
		.id				= -1,
		.resources		= sm5703_fled_res,
#ifdef CONFIG_OF
        .of_compatible = "siliconmitus,sm5703-fled"
#endif /* CONFIG_OF */
	},
};
#endif /*CONFIG_FLED_SM5703*/

#ifdef CONFIG_REGULATOR_SM5703
#define SM5703_OF_COMPATIBLE_USBLDO1 "siliconmitus,sm5703-usbldo1"
#define SM5703_OF_COMPATIBLE_USBLDO2 "siliconmitus,sm5703-usbldo2"
#define SM5703_OF_COMPATIBLE_LDO1 "siliconmitus,sm5703-ldo1"
#define SM5703_OF_COMPATIBLE_LDO2 "siliconmitus,sm5703-ldo2"
#define SM5703_OF_COMPATIBLE_LDO3 "siliconmitus,sm5703-ldo3"
#define SM5703_OF_COMPATIBLE_BUCK "siliconmitus,sm5703-buck"

#ifdef CONFIG_OF
#define REG_OF_COMP(_id) .of_compatible = SM5703_OF_COMPATIBLE_##_id,
#else
#define REG_OF_COMP(_id)
#endif /* CONFIG_OF */

#define SM5703_VR_DEVS(_id)             \
{                                       \
	.name		= "sm5703-regulator",	\
	.id		= SM5703_ID_##_id,	\
	REG_OF_COMP(_id)                   \
}

static struct mfd_cell sm5703_regulator_devs[] = {
	SM5703_VR_DEVS(LDO1),
	SM5703_VR_DEVS(LDO2),
	SM5703_VR_DEVS(LDO3),
	SM5703_VR_DEVS(BUCK),
};
/*
static struct mfd_cell sm5703_regulator_devs[] = {
    { .name = "sm5703-safeout", },
};
*/
#endif

inline static int sm5703_read_device(struct i2c_client *i2c,
		int reg, int bytes, void *dest)
{
	int ret;
	if (bytes > 1) {
		ret = i2c_smbus_read_i2c_block_data(i2c, reg, bytes, dest);
	} else {
		ret = i2c_smbus_read_byte_data(i2c, reg);
		if (ret < 0)
			return ret;
		*(unsigned char *)dest = (unsigned char)ret;

		pr_debug("%s : ret = 0x%x, reg = 0x%d, dest = 0x%d\n",
				__func__, ret, reg, *(unsigned char *)dest);
	}

	return ret;
}

inline static int sm5703_write_device(struct i2c_client *i2c,
		int reg, int bytes, const void *src)
{
	int ret;
	const uint8_t *data;
	if (bytes > 1)
		ret = i2c_smbus_write_i2c_block_data(i2c, reg, bytes, src);
	else {
		data = src;
		ret = i2c_smbus_write_byte_data(i2c, reg, *data);

		pr_debug("%s : ret = 0x%x, reg = 0x%x, data = 0x%x\n",
				__func__, ret, reg, *data);
	}

	return ret;
}

int sm5703_block_read_device(struct i2c_client *i2c,
		int reg, int bytes, void *dest)
{
	struct sm5703_mfd_chip *chip = i2c_get_clientdata(i2c);
	int ret;

	mutex_lock(&chip->io_lock);
	ret = sm5703_read_device(i2c, reg, bytes, dest);
	mutex_unlock(&chip->io_lock);

	return ret;
}
EXPORT_SYMBOL(sm5703_block_read_device);

int sm5703_block_write_device(struct i2c_client *i2c,
		int reg, int bytes, const void *src)
{
	struct sm5703_mfd_chip *chip = i2c_get_clientdata(i2c);
	int ret;

	mutex_lock(&chip->io_lock);
	ret = sm5703_write_device(i2c, reg, bytes, src);
	mutex_unlock(&chip->io_lock);

	return ret;
}
EXPORT_SYMBOL(sm5703_block_write_device);

int sm5703_reg_read(struct i2c_client *i2c, int reg)
{
	struct sm5703_mfd_chip *chip = i2c_get_clientdata(i2c);
	unsigned char data = 0;
	int ret, i = 0;

	mutex_lock(&chip->io_lock);
	ret = sm5703_read_device(i2c, reg, 1, &data);
	if (ret < 0) {
		for (i = 0; i < 3; i++) {
			ret = sm5703_read_device(i2c, reg, 1, &data);
			if (ret >= 0)
				break;
		}
		pr_err("%s: i2c re-read error\n", __func__);			
	}
	mutex_unlock(&chip->io_lock);

	pr_debug("%s : ret = 0x%x, reg = 0x%x, data = 0x%x\n", __func__, ret, reg, data);

	if (ret < 0)
		return ret;
	else
		return (int)data;
}
EXPORT_SYMBOL(sm5703_reg_read);

int sm5703_reg_write(struct i2c_client *i2c, int reg,
		unsigned char data)
{
	struct sm5703_mfd_chip *chip = i2c_get_clientdata(i2c);
	int ret, i = 0;

	mutex_lock(&chip->io_lock);
	ret = i2c_smbus_write_byte_data(i2c, reg, data);
	if (ret < 0) {
		for (i = 0; i < 3; i++) {
			ret = i2c_smbus_write_byte_data(i2c, reg, data);
			if (ret >= 0)
				break;
		}
		pr_err("%s: i2c re-write error\n", __func__);
	}
	mutex_unlock(&chip->io_lock);

	pr_debug("%s : ret = 0x%x, reg = 0x%x, data = 0x%x\n",
			__func__, ret, reg, data);

	return ret;
}
EXPORT_SYMBOL(sm5703_reg_write);

int sm5703_read_reg(struct i2c_client *i2c, u8 reg, u8 *dest)
{
	struct sm5703_mfd_chip *chip = i2c_get_clientdata(i2c);
	int ret;

	mutex_lock(&chip->io_lock);
	ret = i2c_smbus_read_byte_data(i2c, reg);
	mutex_unlock(&chip->io_lock);
	if (ret < 0) {
		pr_debug("%s reg(0x%x), ret(%d)\n", __func__, reg, ret);
		return ret;
	}

	ret &= 0xff;
	*dest = ret;
	return 0;
}
EXPORT_SYMBOL(sm5703_read_reg);

int sm5703_write_reg(struct i2c_client *i2c, u8 reg, u8 value)
{
	struct sm5703_mfd_chip *chip = i2c_get_clientdata(i2c);
	int ret;

	mutex_lock(&chip->io_lock);
	ret = i2c_smbus_write_byte_data(i2c, reg, value);
	mutex_unlock(&chip->io_lock);
	if (ret < 0)
		pr_debug("%s reg(0x%x), ret(%d)\n",
				__func__, reg, ret);

	return ret;
}
EXPORT_SYMBOL(sm5703_write_reg);

int sm5703_update_reg(struct i2c_client *i2c, u8 reg, u8 val, u8 mask)
{
	struct sm5703_mfd_chip *chip = i2c_get_clientdata(i2c);
	int ret;

	mutex_lock(&chip->io_lock);
	ret = i2c_smbus_read_byte_data(i2c, reg);
	if (ret >= 0) {
		u8 old_val = ret & 0xff;
		u8 new_val = (val & mask) | (old_val & (~mask));
		ret = i2c_smbus_write_byte_data(i2c, reg, new_val);
	}
	mutex_unlock(&chip->io_lock);
	return ret;
}
EXPORT_SYMBOL(sm5703_update_reg);

int sm5703_assign_bits(struct i2c_client *i2c, int reg,
		unsigned char mask, unsigned char data)
{
	struct sm5703_mfd_chip *chip = i2c_get_clientdata(i2c);
	unsigned char value;
	int ret;

	mutex_lock(&chip->io_lock);
	ret = sm5703_read_device(i2c, reg, 1, &value);

	if (ret < 0)
		goto out;

	value &= ~mask;
	value |= data;
	ret = i2c_smbus_write_byte_data(i2c, reg, value);

	pr_debug("%s : ret = 0x%x, reg = 0x%x, value = 0x%x, data = 0x%x\n",
			__func__, ret, reg, value,data);

out:
	mutex_unlock(&chip->io_lock);
	return ret;
}
EXPORT_SYMBOL(sm5703_assign_bits);

int sm5703_set_bits(struct i2c_client *i2c, int reg,
		unsigned char mask)
{
	return sm5703_assign_bits(i2c,reg,mask,mask);
}
EXPORT_SYMBOL(sm5703_set_bits);

int sm5703_clr_bits(struct i2c_client *i2c, int reg,
		unsigned char mask)
{
	return sm5703_assign_bits(i2c,reg,mask,0);
}
EXPORT_SYMBOL(sm5703_clr_bits);

extern int sm5703_init_irq(sm5703_mfd_chip_t *chip);
extern int sm5703_exit_irq(sm5703_mfd_chip_t *chip);

static int sm5703mfd_parse_dt(struct device *dev,
		sm5703_mfd_platform_data_t *pdata)
{
	int ret;
	struct device_node *np = dev->of_node;
	enum of_gpio_flags irq_gpio_flags;

	ret = pdata->irq_gpio = of_get_named_gpio_flags(np, "sm5703,irq-gpio",
			0, &irq_gpio_flags);
	if (ret < 0) {
		dev_err(dev, "%s : can't get irq-gpio\r\n", __FUNCTION__);
		return ret;
	}

	pdata->irq_base = -1;
	ret = of_property_read_u32(np, "sm5703,irq-base", (u32*)&pdata->irq_base);
	if (ret < 0 || pdata->irq_base == -1) {
		dev_info(dev, "%s : no assignment of irq_base, use irq_alloc_descs()\r\n",
				__FUNCTION__);

		ret = pdata->mrstb_gpio = of_get_named_gpio_flags(np,
				"sm5703,mrstb-gpio", 0, NULL);
		if (ret < 0) {
			dev_err(dev, "%s : can't get mrstb-gpio\r\n", __FUNCTION__);
			pdata->mrstb_gpio = 0;
		}

	}
	return 0;
}

static int sm5703_mfd_probe(struct i2c_client *i2c,
		const struct i2c_device_id *id)
{
	int ret = 0;
	struct device_node *of_node = i2c->dev.of_node;
	sm5703_mfd_chip_t *chip;
	sm5703_mfd_platform_data_t *pdata = i2c->dev.platform_data;

	pr_info("%s : SM5703 MFD Driver %s start probing\n",
			__func__, SM5703_DRV_VER);

	if (of_node) {
		pdata = devm_kzalloc(&i2c->dev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata) {
			dev_err(&i2c->dev, "Failed to allocate memory\n");
			ret = -ENOMEM;
			goto err_dt_nomem;
		}
		ret = sm5703mfd_parse_dt(&i2c->dev, pdata);
		if (ret < 0)
			goto err_parse_dt;
	} else {
		pdata = i2c->dev.platform_data;
	}

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (chip == NULL) {
		dev_err(&i2c->dev, "Memory is not enough.\n");
		ret = -ENOMEM;
		goto err_mfd_nomem;
	}

	chip->dev = &i2c->dev;

	ret = i2c_check_functionality(i2c->adapter, I2C_FUNC_SMBUS_BYTE_DATA |
			I2C_FUNC_SMBUS_WORD_DATA | I2C_FUNC_SMBUS_I2C_BLOCK);
	if (!ret) {
		ret = i2c_get_functionality(i2c->adapter);
		dev_err(chip->dev, "I2C functionality is not supported.\n");
		ret = -ENOSYS;
		goto err_i2cfunc_not_support;
	}

	chip->i2c_client = i2c;
	chip->pdata = pdata;

	pr_info("%s:%s pdata->irq_base = %d\n",
			"sm5703-mfd", __func__, pdata->irq_base);
	/* if board-init had already assigned irq_base (>=0) ,
	   no need to allocate it;
	   assign -1 to let this driver allocate resource by itself*/
	if (pdata->irq_base < 0)
		pdata->irq_base = irq_alloc_descs(-1, 0, SM5703_IRQS_NR, 0);
	if (pdata->irq_base < 0) {
		pr_err("%s:%s irq_alloc_descs Fail! ret(%d)\n",
				"sm5703-mfd", __func__, pdata->irq_base);
		ret = -EINVAL;
		goto irq_base_err;
	} else {
		chip->irq_base = pdata->irq_base;
		pr_info("%s:%s irq_base = %d\n",
				"sm5703-mfd", __func__, chip->irq_base);
	}

	i2c_set_clientdata(i2c, chip);
	mutex_init(&chip->io_lock);
	mutex_init(&chip->suspend_flag_lock);

	/* Set MRSTB GPIO pin to high level to indicate that
	 * system is alive (do NOT do reset) */
	if (pdata->mrstb_gpio) {
		ret = gpio_request(pdata->mrstb_gpio, "sm5703_mrstb");
		if (ret == 0) {
			ret = gpio_direction_output(pdata->mrstb_gpio, 1);
			if (ret < 0)
				pr_err("%s : cannot set GPIO%d output direction(%d)\n",
					__func__, pdata->mrstb_gpio, ret);

		} else
			pr_info("%s : Request GPIO %d failed\n",
				__func__, (int)pdata->mrstb_gpio);
	}

	wake_lock_init(&(chip->irq_wake_lock), WAKE_LOCK_SUSPEND,
			"sm5703mfd_wakelock");

	ret = sm5703_init_irq(chip);

	if (ret < 0) {
		dev_err(chip->dev,
				"Error : can't initialize SM5703 MFD irq\n");
		goto err_init_irq;
	}

#ifdef CONFIG_REGULATOR_SM5703
#if (LINUX_VERSION_CODE>=KERNEL_VERSION(3,6,0))
	ret = mfd_add_devices(chip->dev, 0, &sm5703_regulator_devs[0],
			ARRAY_SIZE(sm5703_regulator_devs),
			NULL, chip->irq_base, NULL);
#else
	ret = mfd_add_devices(chip->dev, 0, &sm5703_regulator_devs[0],
			ARRAY_SIZE(sm5703_regulator_devs),
			NULL, chip->irq_base);
#endif
	if (ret < 0) {
		dev_err(chip->dev,
				"Error : can't add regulator\n");
		goto err_add_regulator_devs;
	}
#endif /*CONFIG_REGULATOR_SM5703*/

#ifdef CONFIG_FLED_SM5703
#if (LINUX_VERSION_CODE>=KERNEL_VERSION(3,6,0))
	ret = mfd_add_devices(chip->dev, 0, &sm5703_fled_devs[0],
			ARRAY_SIZE(sm5703_fled_devs),
			NULL, chip->irq_base, NULL);
#else
	ret = mfd_add_devices(chip->dev, 0, &sm5703_fled_devs[0],
			ARRAY_SIZE(sm5703_fled_devs),
			NULL, chip->irq_base);
#endif
	if (ret < 0) {
		dev_err(chip->dev, "Failed : add FlashLED devices");
		goto err_add_fled_devs;
	}
#endif /*CONFIG_FLED_SM5703*/

#ifdef CONFIG_CHARGER_SM5703
#if (LINUX_VERSION_CODE>=KERNEL_VERSION(3,6,0))
	ret = mfd_add_devices(chip->dev, 0, &sm5703_charger_devs[0],
			ARRAY_SIZE(sm5703_charger_devs),
			NULL, chip->irq_base, NULL);
#else
	ret = mfd_add_devices(chip->dev, 0, &sm5703_charger_devs[0],
			ARRAY_SIZE(sm5703_charger_devs),
			NULL, chip->irq_base);
#endif
	if (ret<0) {
		dev_err(chip->dev, "Failed : add charger devices\n");
		goto err_add_chg_devs;
	}
#endif /*CONFIG_CHARGER_SM5703*/

	pr_info("%s : SM5703 MFD Driver Fin probe\n", __func__);
	return ret;

#ifdef CONFIG_CHARGER_SM5703
err_add_chg_devs:
#endif /*CONFIG_CHARGER_SM5703*/

#ifdef CONFIG_FLED_SM5703
err_add_fled_devs:
#endif /*CONFIG_FLED_SM5703*/
	mfd_remove_devices(chip->dev);
#ifdef CONFIG_REGULATOR_SM5703
err_add_regulator_devs:
#endif /*CONFIG_REGULATOR_SM5703*/
err_init_irq:
	wake_lock_destroy(&(chip->irq_wake_lock));
	mutex_destroy(&chip->io_lock);
irq_base_err:
err_i2cfunc_not_support:
	kfree(chip);
err_mfd_nomem:
err_parse_dt:
	devm_kfree(&i2c->dev, pdata);
err_dt_nomem:
	return ret;
}

static int sm5703_mfd_remove(struct i2c_client *i2c)
{
	sm5703_mfd_chip_t *chip = i2c_get_clientdata(i2c);

	pr_info("%s : SM5703 MFD Driver remove\n", __func__);
	if (chip->pdata->mrstb_gpio)
		gpio_free(chip->pdata->mrstb_gpio);
	mfd_remove_devices(chip->dev);
	wake_lock_destroy(&(chip->irq_wake_lock));
	mutex_destroy(&chip->suspend_flag_lock);
	mutex_destroy(&chip->io_lock);
	kfree(chip);

	return 0;
}

#ifdef CONFIG_PM
extern int sm5703_irq_suspend(sm5703_mfd_chip_t *chip);
extern int sm5703_irq_resume(sm5703_mfd_chip_t *chip);
int sm5703_mfd_suspend(struct device *dev)
{

	struct i2c_client *i2c =
		container_of(dev, struct i2c_client, dev);

	sm5703_mfd_chip_t *chip = i2c_get_clientdata(i2c);
	BUG_ON(chip == NULL);
	return sm5703_irq_suspend(chip);
}

int sm5703_mfd_resume(struct device *dev)
{
	struct i2c_client *i2c =
		container_of(dev, struct i2c_client, dev);
	sm5703_mfd_chip_t *chip = i2c_get_clientdata(i2c);
	BUG_ON(chip == NULL);
	return sm5703_irq_resume(chip);
}
#endif /* CONFIG_PM */

static void sm5703_mfd_shutdown(struct i2c_client *client)
{
	struct i2c_client *i2c = client;

    sm5703_assign_bits(i2c, SM5703_CNTL, SM5703_OPERATION_MODE, SM5703_OPERATION_MODE_CHARGING_ON);
}

static const struct i2c_device_id sm5703_mfd_id_table[] = {
	{ "sm5703-mfd", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, sm5703_id_table);

#ifdef CONFIG_PM
const struct dev_pm_ops sm5703_pm = {
	.suspend = sm5703_mfd_suspend,
	.resume = sm5703_mfd_resume,
};
#endif

#ifdef CONFIG_OF
static struct of_device_id sm5703_match_table[] = {
	{ .compatible = "siliconmitus,sm5703mfd",},
	{},
};
#else
#define sm5703_match_table NULL
#endif

static struct i2c_driver sm5703_mfd_driver = {
	.driver	= {
		.name	= "sm5703-mfd",
		.owner	= THIS_MODULE,
		.of_match_table = sm5703_match_table,
#ifdef CONFIG_PM
		.pm		= &sm5703_pm,
#endif
	},
	.shutdown = sm5703_mfd_shutdown,
	.probe		= sm5703_mfd_probe,
	.remove		= sm5703_mfd_remove,
	.id_table	= sm5703_mfd_id_table,
};

static int __init sm5703_mfd_i2c_init(void)
{
	int ret;

	pr_info("%s : SM5703 init\n", __func__);
	ret = i2c_add_driver(&sm5703_mfd_driver);
	if (ret != 0)
		pr_info("%s : Failed to register SM5703 MFD I2C driver\n",
		__func__);

	return ret;
}
device_initcall(sm5703_mfd_i2c_init);

static void __exit sm5703_mfd_i2c_exit(void)
{
	i2c_del_driver(&sm5703_mfd_driver);
}
module_exit(sm5703_mfd_i2c_exit);

MODULE_DESCRIPTION("Siliconmitus SM5703 MFD I2C Driver");
MODULE_VERSION(SM5703_DRV_VER);
MODULE_LICENSE("GPL");
