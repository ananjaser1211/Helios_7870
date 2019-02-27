/*
 * s2mpu05.c
 *
 * Copyright (c) 2015 Samsung Electronics Co., Ltd
 *              http://www.samsung.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/bug.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/mfd/samsung/core.h>
#include <linux/mfd/samsung/s2mpu05.h>
#include <linux/io.h>
#ifdef CONFIG_SEC_DEBUG
#include <linux/sec_debug.h>
#endif
#ifdef CONFIG_SEC_PM
#include <linux/sec_sysfs.h>

#define STATUS1_ACOK	BIT(2)

static struct device *ap_pmic_dev;
#endif /* CONFIG_SEC_PM */

static struct s2mpu05_info *static_info;

struct s2mpu05_info {
	struct regulator_dev *rdev[S2MPU05_REGULATOR_MAX];
	unsigned int opmode[S2MPU05_REGULATOR_MAX];
	int num_regulators;
	struct sec_pmic_dev *iodev;
	bool g3d_en;
	const char *g3d_en_addr;
	const char *g3d_en_pin;
#ifdef CONFIG_SEC_DEBUG_PMIC
	struct class *pmic_test_class;
	struct notifier_block pm_notifier;
#endif
};

#ifdef CONFIG_SEC_DEBUG_PMIC
static struct device *pmic_test_dev;

#define PMIC_NAME	"s2mpu05"
#define PMIC_NUM_OF_BUCKS	5
#define PMIC_NUM_OF_LDOS	35

static int buck_addrs[PMIC_NUM_OF_BUCKS] = { 0x1a, 0x1c, 0x20, 0x23, 0x25 };
static int ldo_addrs[PMIC_NUM_OF_LDOS] = { 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x35, /* LDO1~10 */
	0x36, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3f, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, /* LDO11~24(CP) */
	0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51 }; /* LDO25~35 */

static int s2m_debug_reg(struct regulator_dev *rdev, char *str)
{
	int ret;
	unsigned int val;
	int i;

	if (!static_info)
		return 0;

	for (i = 0; i < PMIC_NUM_OF_BUCKS; ++i) {
		if (buck_addrs[i] == rdev->desc->enable_reg) {
			ret = sec_reg_read(static_info->iodev, rdev->desc->enable_reg, &val);
			pr_info("%s %s: BUCK%d 0x%x = 0x%x (EN=%d)\n", \
					PMIC_NAME, str, i+1, rdev->desc->enable_reg, val, (val >> 6));
			return ret;
		}
	}

	for (i = 0; i < PMIC_NUM_OF_LDOS; ++i) {
		if (ldo_addrs[i] == rdev->desc->enable_reg) {
			ret = sec_reg_read(static_info->iodev, rdev->desc->enable_reg, &val);
			pr_info("%s %s: LDO%d 0x%x = 0x%x (EN=%d)\n", \
					PMIC_NAME, str, i+1, rdev->desc->enable_reg, val, (val >> 6));
			return ret;
		}
	}

	return 0;
}
#endif

/* Some LDOs supports [LPM/Normal]ON mode during suspend state */
static int s2m_set_mode(struct regulator_dev *rdev,
				     unsigned int mode)
{
	struct s2mpu05_info *s2mpu05 = rdev_get_drvdata(rdev);
	unsigned int val;
	int ret, id = rdev_get_id(rdev);

	switch (mode) {
	case SEC_OPMODE_SUSPEND:		/* ON in Standby Mode */
		val = 0x1 << S2MPU05_ENABLE_SHIFT;
		break;
	case SEC_OPMODE_MIF:			/* ON in MIF Mode */
		val = 0x2 << S2MPU05_ENABLE_SHIFT;
		break;
	case SEC_OPMODE_ON:			/* ON in Normal Mode */
		val = 0x3 << S2MPU05_ENABLE_SHIFT;
		break;
	default:
		pr_warn("%s: regulator_suspend_mode : 0x%x not supported\n",
			rdev->desc->name, mode);
		return -EINVAL;
	}

	ret = sec_reg_update(s2mpu05->iodev, rdev->desc->enable_reg,
				  val, rdev->desc->enable_mask);
	if (ret)
		return ret;

#ifdef CONFIG_SEC_DEBUG_PMIC
	s2m_debug_reg(rdev, "set_mode");
#endif

	s2mpu05->opmode[id] = val;
	return 0;
}

static int s2m_enable(struct regulator_dev *rdev)
{
	struct s2mpu05_info *s2mpu05 = rdev_get_drvdata(rdev);
	int ret;

	ret = sec_reg_update(s2mpu05->iodev, rdev->desc->enable_reg,
				  s2mpu05->opmode[rdev_get_id(rdev)],
				  rdev->desc->enable_mask);
#ifdef CONFIG_SEC_DEBUG_PMIC
	s2m_debug_reg(rdev, "regulator_enable");
#endif
	return ret;
}

static int s2m_disable_regmap(struct regulator_dev *rdev)
{
	struct s2mpu05_info *s2mpu05 = rdev_get_drvdata(rdev);
	unsigned int val;
	int ret;

	if (rdev->desc->enable_is_inverted)
		val = rdev->desc->enable_mask;
	else
		val = 0;

	ret = sec_reg_update(s2mpu05->iodev, rdev->desc->enable_reg,
				  val, rdev->desc->enable_mask);
#ifdef CONFIG_SEC_DEBUG_PMIC
	s2m_debug_reg(rdev, "regulator_disable");
#endif
	return ret;
}

static int s2m_is_enabled_regmap(struct regulator_dev *rdev)
{
	struct s2mpu05_info *s2mpu05 = rdev_get_drvdata(rdev);
	int ret;
	unsigned int val;
	ret = sec_reg_read(s2mpu05->iodev, rdev->desc->enable_reg, &val);
	if (ret)
		return ret;

	if (rdev->desc->enable_is_inverted)
		return (val & rdev->desc->enable_mask) == 0;
	else
		return (val & rdev->desc->enable_mask) != 0;
}

static int get_ramp_delay(int ramp_delay)
{
	unsigned char cnt = 0;

	ramp_delay /= 6;

	while (true) {
		ramp_delay = ramp_delay >> 1;
		if (ramp_delay == 0)
			break;
		cnt++;
	}
	return cnt;
}

static int s2m_set_ramp_delay(struct regulator_dev *rdev, int ramp_delay)
{
	struct s2mpu05_info *s2mpu05 = rdev_get_drvdata(rdev);
	int ramp_shift, reg_id = rdev_get_id(rdev);
	int ramp_mask = 0x03;
	unsigned int ramp_value = 0;

	ramp_value = get_ramp_delay(ramp_delay/1000);
	if (ramp_value > 4) {
		pr_warn("%s: ramp_delay: %d not supported\n",
			rdev->desc->name, ramp_delay);
	}

	switch (reg_id) {
	case S2MPU05_BUCK1:
	case S2MPU05_BUCK2:
		ramp_shift = 6;
		break;
	case S2MPU05_BUCK3:
	case S2MPU05_BUCK4:
		ramp_shift = 4;
		break;
	case S2MPU05_BUCK5:
		ramp_shift = 2;
		break;
	default:
		return -EINVAL;
	}

	return sec_reg_update(s2mpu05->iodev, S2MPU05_REG_BUCK_RAMP,
			  ramp_value << ramp_shift, ramp_mask << ramp_shift);
}

static int s2m_get_voltage_sel_regmap(struct regulator_dev *rdev)
{
	struct s2mpu05_info *s2mpu05 = rdev_get_drvdata(rdev);
	int ret;
	unsigned int val;
	ret = sec_reg_read(s2mpu05->iodev, rdev->desc->vsel_reg, &val);
	if (ret)
		return ret;

	val &= rdev->desc->vsel_mask;

	return val;
}

static int s2m_set_voltage_sel_regmap(struct regulator_dev *rdev, unsigned sel)
{
	struct s2mpu05_info *s2mpu05 = rdev_get_drvdata(rdev);
	int reg_id = rdev_get_id(rdev);
	int ret;
	char name[16];
	unsigned int voltage;

	/* voltage information logging to snapshot feature */
	snprintf(name, sizeof(name), "LDO%d", (reg_id - S2MPU05_LDO1) + 1);
	voltage = ((sel & rdev->desc->vsel_mask) * S2MPU05_LDO_STEP2) + S2MPU05_LDO_MIN1;
	exynos_ss_regulator(name, rdev->desc->vsel_reg, voltage, ESS_FLAG_IN);

	ret = sec_reg_update(s2mpu05->iodev, rdev->desc->vsel_reg,
				  sel, rdev->desc->vsel_mask);
	if (ret < 0)
		goto out;

	if (rdev->desc->apply_bit)
		ret = sec_reg_update(s2mpu05->iodev, rdev->desc->apply_reg,
					 rdev->desc->apply_bit,
					 rdev->desc->apply_bit);

	exynos_ss_regulator(name, rdev->desc->vsel_reg, voltage, ESS_FLAG_OUT);

	return ret;
out:
	pr_warn("%s: failed to set voltage_sel_regmap\n", rdev->desc->name);
	exynos_ss_regulator(name, rdev->desc->vsel_reg, voltage, ESS_FLAG_ON);
	return ret;
}

static int s2m_set_voltage_sel_regmap_buck(struct regulator_dev *rdev,
					unsigned sel)
{
	int ret;
	struct s2mpu05_info *s2mpu05 = rdev_get_drvdata(rdev);
	int reg_id = rdev_get_id(rdev);
	unsigned int voltage;
	char name[16];

	/* voltage information logging to snapshot feature */
	snprintf(name, sizeof(name), "BUCK%d", (reg_id - S2MPU05_BUCK1) + 1);
	voltage = (sel * S2MPU05_BUCK_STEP1) + S2MPU05_BUCK_MIN1;
	exynos_ss_regulator(name, rdev->desc->vsel_reg, voltage, ESS_FLAG_IN);

	ret = sec_reg_write(s2mpu05->iodev, rdev->desc->vsel_reg, sel);
	if (ret < 0)
		goto out;

	if (rdev->desc->apply_bit)
		ret = sec_reg_update(s2mpu05->iodev, rdev->desc->apply_reg,
					 rdev->desc->apply_bit,
					 rdev->desc->apply_bit);

	exynos_ss_regulator(name, rdev->desc->vsel_reg, voltage, ESS_FLAG_OUT);

	return ret;
out:
	pr_warn("%s: failed to set voltage_sel_regmap\n", rdev->desc->name);
	exynos_ss_regulator(name, rdev->desc->vsel_reg, voltage, ESS_FLAG_ON);
	return ret;
}

static int s2m_set_voltage_time_sel(struct regulator_dev *rdev,
				   unsigned int old_selector,
				   unsigned int new_selector)
{
	unsigned int ramp_delay = 0;
	int old_volt, new_volt;

	if (rdev->constraints->ramp_delay)
		ramp_delay = rdev->constraints->ramp_delay;
	else if (rdev->desc->ramp_delay)
		ramp_delay = rdev->desc->ramp_delay;

	if (ramp_delay == 0) {
		pr_warn("%s: ramp_delay not set\n", rdev->desc->name);
		return -EINVAL;
	}

	/* sanity check */
	if (!rdev->desc->ops->list_voltage)
		return -EINVAL;

	old_volt = rdev->desc->ops->list_voltage(rdev, old_selector);
	new_volt = rdev->desc->ops->list_voltage(rdev, new_selector);

	if (old_selector < new_selector)
		return DIV_ROUND_UP(new_volt - old_volt, ramp_delay);

	return 0;
}

u32 pmic_rev_get(void)
{
	return SEC_PMIC_REV(static_info->iodev);
}

#ifdef CONFIG_SEC_DEBUG_PMIC
static int _pmic_debug_print_all_regulators(void)
{
	unsigned int val;
	int i;

	if (!static_info)
		return 0;

	for (i = 0; i < PMIC_NUM_OF_BUCKS; ++i) {
		sec_reg_read(static_info->iodev, buck_addrs[i], &val);
		pr_info("%s Buck%d 0x%0x = 0x%x (EN=0x%x)\n", \
			PMIC_NAME, i+1, buck_addrs[i], val, (val >> 6));
	}

	for (i = 0; i < PMIC_NUM_OF_LDOS; ++i) {
		sec_reg_read(static_info->iodev, ldo_addrs[i], &val);
		pr_info("%s LDO%02d 0x%0x = 0x%x (EN=0x%x)\n", \
			PMIC_NAME, i+1, ldo_addrs[i], val, (val >> 6));
	}

	return 0;
}

static int buf_to_i2c_read(const char *buf)
{
	unsigned int addr, val;
	int ret;

	if (!static_info)
		return 0;

	sscanf(buf, "0x%02x", &addr);

	if (addr == 0xff) {
		pr_info("print all regulators\n");
		_pmic_debug_print_all_regulators();
		return 0;
	}

	ret = sec_reg_read(static_info->iodev, addr, &val);
	if (ret < 0)
		return ret;

	pr_info("%s %s: 0x%02x 0x%02x\n", PMIC_NAME, __func__, addr, val);
	return ret;
}

static int buf_to_i2c_write(const char *buf)
{
	unsigned int addr, val;
	int ret;

	if (!static_info)
		return 0;

	sscanf(buf, "0x%02x 0x%02x", &addr, &val);

	ret = sec_reg_write(static_info->iodev, addr, val);
	if (ret < 0)
		return ret;

	pr_info("%s %s: 0x%02x 0x%02x\n", PMIC_NAME, __func__, addr, val);
	return ret;
}

static ssize_t pmic_i2c_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE,
			"ex)\n read\n echo \"0x1a\" > pmic_i2c\n write\n echo \"0x1a 0x00\" > pmic_i2c\n");
}

static ssize_t pmic_i2c_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	if (strlen(buf) <= 3)
		pr_info("%s : wrong input\n", __func__);
	else if (strlen(buf) > 3 && strlen(buf) <= 5)	// read
		buf_to_i2c_read(buf);
	else if (strlen(buf) > 5 && strlen(buf) <= 10)
		buf_to_i2c_write(buf);
	return count;
}

static DEVICE_ATTR(pmic_i2c, 0644, pmic_i2c_show, pmic_i2c_store);

static int pmic_debug_print_all_regulators(struct notifier_block *this,
		unsigned long event, void *ptr)
{
	switch (event) {
	case PM_SUSPEND_PREPARE:
		_pmic_debug_print_all_regulators();
		break;
	}
	return NOTIFY_DONE;
}
#endif

static struct regulator_ops s2mpu05_ldo_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= s2m_is_enabled_regmap,
	.enable			= s2m_enable,
	.disable		= s2m_disable_regmap,
	.get_voltage_sel	= s2m_get_voltage_sel_regmap,
	.set_voltage_sel	= s2m_set_voltage_sel_regmap,
	.set_voltage_time_sel	= s2m_set_voltage_time_sel,
	.set_mode		= s2m_set_mode,
};

static struct regulator_ops s2mpu05_buck_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.is_enabled		= s2m_is_enabled_regmap,
	.enable			= s2m_enable,
	.disable		= s2m_disable_regmap,
	.get_voltage_sel	= s2m_get_voltage_sel_regmap,
	.set_voltage_sel	= s2m_set_voltage_sel_regmap_buck,
	.set_voltage_time_sel	= s2m_set_voltage_time_sel,
	.set_mode		= s2m_set_mode,
	.set_ramp_delay		= s2m_set_ramp_delay,
};

#define _BUCK(macro)	S2MPU05_BUCK##macro
#define _buck_ops(num)	s2mpu05_buck_ops##num

#define _LDO(macro)	S2MPU05_LDO##macro
#define _REG(ctrl)	S2MPU05_REG##ctrl
#define _ldo_ops(num)	s2mpu05_ldo_ops##num
#define _TIME(macro)	S2MPU05_ENABLE_TIME##macro

#define BUCK_DESC(_name, _id, _ops, m, s, v, e, t)	{	\
	.name		= _name,				\
	.id		= _id,					\
	.ops		= _ops,					\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= m,					\
	.uV_step	= s,					\
	.n_voltages	= S2MPU05_BUCK_N_VOLTAGES,		\
	.vsel_reg	= v,					\
	.vsel_mask	= S2MPU05_BUCK_VSEL_MASK,		\
	.enable_reg	= e,					\
	.enable_mask	= S2MPU05_ENABLE_MASK,			\
	.enable_time	= t					\
}

#define LDO_DESC(_name, _id, _ops, m, s, v, e, t)	{	\
	.name		= _name,				\
	.id		= _id,					\
	.ops		= _ops,					\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= m,					\
	.uV_step	= s,					\
	.n_voltages	= S2MPU05_LDO_N_VOLTAGES,		\
	.vsel_reg	= v,					\
	.vsel_mask	= S2MPU05_LDO_VSEL_MASK,		\
	.enable_reg	= e,					\
	.enable_mask	= S2MPU05_ENABLE_MASK,			\
	.enable_time	= t					\
}

static struct regulator_desc regulators[S2MPU05_REGULATOR_MAX] = {
	/* name, id, ops, min_uv, uV_step, vsel_reg, enable_reg */
	LDO_DESC("LDO1", _LDO(1), &_ldo_ops(), _LDO(_MIN3), _LDO(_STEP2),
			_REG(_L1CTRL), _REG(_L1CTRL), _TIME(_LDO)),
	LDO_DESC("LDO2", _LDO(2), &_ldo_ops(), _LDO(_MIN2), _LDO(_STEP2),
			_REG(_L2CTRL), _REG(_L2CTRL), _TIME(_LDO)),
	LDO_DESC("LDO3", _LDO(3), &_ldo_ops(), _LDO(_MIN1), _LDO(_STEP2),
			_REG(_L3CTRL), _REG(_L3CTRL), _TIME(_LDO)),
	LDO_DESC("LDO4", _LDO(4), &_ldo_ops(), _LDO(_MIN1), _LDO(_STEP1),
			_REG(_L4CTRL), _REG(_L4CTRL), _TIME(_LDO)),
	LDO_DESC("LDO5", _LDO(5), &_ldo_ops(), _LDO(_MIN1), _LDO(_STEP1),
			_REG(_L5CTRL), _REG(_L5CTRL), _TIME(_LDO)),
	LDO_DESC("LDO6", _LDO(6), &_ldo_ops(), _LDO(_MIN1), _LDO(_STEP1),
			_REG(_L6CTRL), _REG(_L6CTRL), _TIME(_LDO)),
	LDO_DESC("LDO7", _LDO(7), &_ldo_ops(), _LDO(_MIN1), _LDO(_STEP2),
			_REG(_L7CTRL), _REG(_L7CTRL), _TIME(_LDO)),
	LDO_DESC("LDO8", _LDO(8), &_ldo_ops(), _LDO(_MIN2), _LDO(_STEP2),
			_REG(_L8CTRL), _REG(_L8CTRL), _TIME(_LDO)),
	LDO_DESC("LDO9", _LDO(9), &_ldo_ops(), _LDO(_MIN3), _LDO(_STEP2),
			_REG(_L9CTRL1), _REG(_L9CTRL1), _TIME(_LDO)),
	LDO_DESC("LDO10", _LDO(10), &_ldo_ops(), _LDO(_MIN3), _LDO(_STEP2),
			_REG(_L10CTRL), _REG(_L10CTRL), _TIME(_LDO)),
	LDO_DESC("LDO25", _LDO(25), &_ldo_ops(), _LDO(_MIN1), _LDO(_STEP2),
			_REG(_L25CTRL), _REG(_L25CTRL), _TIME(_LDO)),
	LDO_DESC("LDO26", _LDO(26), &_ldo_ops(), _LDO(_MIN2), _LDO(_STEP2),
			_REG(_L26CTRL), _REG(_L26CTRL), _TIME(_LDO)),
	LDO_DESC("LDO27", _LDO(27), &_ldo_ops(), _LDO(_MIN1), _LDO(_STEP2),
			_REG(_L27CTRL), _REG(_L27CTRL), _TIME(_LDO)),
	LDO_DESC("LDO28", _LDO(28), &_ldo_ops(), _LDO(_MIN2), _LDO(_STEP2),
			_REG(_L28CTRL), _REG(_L28CTRL), _TIME(_LDO)),
	LDO_DESC("LDO29", _LDO(29), &_ldo_ops(), _LDO(_MIN2), _LDO(_STEP2),
			_REG(_L29CTRL), _REG(_L29CTRL), _TIME(_LDO)),
	LDO_DESC("LDO30", _LDO(30), &_ldo_ops(), _LDO(_MIN1), _LDO(_STEP2),
			_REG(_L30CTRL), _REG(_L30CTRL), _TIME(_LDO)),
	LDO_DESC("LDO31", _LDO(31), &_ldo_ops(), _LDO(_MIN2), _LDO(_STEP2),
			_REG(_L31CTRL), _REG(_L31CTRL), _TIME(_LDO)),
	LDO_DESC("LDO32", _LDO(32), &_ldo_ops(), _LDO(_MIN2), _LDO(_STEP2),
			_REG(_L32CTRL), _REG(_L32CTRL), _TIME(_LDO)),
	LDO_DESC("LDO33", _LDO(33), &_ldo_ops(), _LDO(_MIN2), _LDO(_STEP2),
			_REG(_L33CTRL), _REG(_L33CTRL), _TIME(_LDO)),
	LDO_DESC("LDO34", _LDO(34), &_ldo_ops(), _LDO(_MIN2), _LDO(_STEP2),
			_REG(_L34CTRL), _REG(_L34CTRL), _TIME(_LDO)),
	LDO_DESC("LDO35", _LDO(35), &_ldo_ops(), _LDO(_MIN2), _LDO(_STEP2),
			_REG(_L35CTRL), _REG(_L35CTRL), _TIME(_LDO)),
	BUCK_DESC("BUCK1", _BUCK(1), &_buck_ops(), _BUCK(_MIN1), _BUCK(_STEP1),
			_REG(_B1CTRL2), _REG(_B1CTRL1), _TIME(_BUCK1)),
	BUCK_DESC("BUCK2", _BUCK(2), &_buck_ops(), _BUCK(_MIN1), _BUCK(_STEP1),
			_REG(_B2CTRL2), _REG(_B2CTRL1), _TIME(_BUCK2)),
	BUCK_DESC("BUCK3", _BUCK(3), &_buck_ops(), _BUCK(_MIN1), _BUCK(_STEP1),
			_REG(_B3CTRL2), _REG(_B3CTRL1), _TIME(_BUCK3)),
	BUCK_DESC("BUCK4", _BUCK(4), &_buck_ops(), _BUCK(_MIN2), _BUCK(_STEP2),
			_REG(_B4CTRL2), _REG(_B4CTRL1), _TIME(_BUCK4)),
	BUCK_DESC("BUCK5", _BUCK(5), &_buck_ops(), _BUCK(_MIN2), _BUCK(_STEP2),
			_REG(_B5CTRL2), _REG(_B5CTRL1), _TIME(_BUCK5)),
};

#ifdef CONFIG_OF
static int s2mpu05_pmic_dt_parse_pdata(struct sec_pmic_dev *iodev,
					struct sec_platform_data *pdata)
{
	struct device_node *pmic_np, *regulators_np, *reg_np;
	struct sec_regulator_data *rdata;
	unsigned long i;
	int ret;
	u32 val;

	pmic_np = iodev->dev->of_node;
	if (!pmic_np) {
		dev_err(iodev->dev, "could not find pmic sub-node\n");
		return -ENODEV;
	}

	ret = of_property_read_u32(pmic_np, "g3d_en", &val);
	if (ret)
		return -EINVAL;
	pdata->g3d_en = !!val;

	regulators_np = of_find_node_by_name(pmic_np, "regulators");
	if (!regulators_np) {
		dev_err(iodev->dev, "could not find regulators sub-node\n");
		return -EINVAL;
	}

	/* count the number of regulators to be supported in pmic */
	pdata->num_regulators = 0;
	for_each_child_of_node(regulators_np, reg_np) {
		pdata->num_regulators++;
	}

	rdata = devm_kzalloc(iodev->dev, sizeof(*rdata) *
				pdata->num_regulators, GFP_KERNEL);
	if (!rdata) {
		dev_err(iodev->dev,
			"could not allocate memory for regulator data\n");
		return -ENOMEM;
	}

	pdata->regulators = rdata;
	for_each_child_of_node(regulators_np, reg_np) {
		for (i = 0; i < ARRAY_SIZE(regulators); i++)
			if (!of_node_cmp(reg_np->name,
					regulators[i].name))
				break;

		if (i == ARRAY_SIZE(regulators)) {
			dev_warn(iodev->dev,
			"don't know how to configure regulator %s\n",
			reg_np->name);
			continue;
		}

		rdata->id = i;
		rdata->initdata = of_get_regulator_init_data(
						iodev->dev, reg_np);
		rdata->reg_node = reg_np;
		rdata++;
	}

	return 0;
}
#else
static int s2mpu05_pmic_dt_parse_pdata(struct sec_pmic_dev *iodev,
					struct sec_platform_data *pdata)
{
	return 0;
}
#endif /* CONFIG_OF */

#ifdef CONFIG_SEC_DEBUG
int pmic_reset_enabled(int reset_enabled)
{
	struct s2mpu05_info *s2mpu05 = static_info;
	int ret = 0;
	unsigned int tmp;

	if (reset_enabled) {
		/* 1 key hard reset */
		pr_info("PM: Set device 1-key hard reset.\n");

		/* Disable warm reset */
		sec_reg_read(s2mpu05->iodev, S2MPU05_REG_CTRL3, &tmp);
		tmp &= ~(0x70);
		tmp |= (0 << 6) | (1 << 5) | (0 << 4);	/* set MRSEL bit to 1 */
		sec_reg_write(s2mpu05->iodev, S2MPU05_REG_CTRL3, tmp);

		/* Enable manual reset */
		sec_reg_read(s2mpu05->iodev, S2MPU05_REG_CTRL1, &tmp);
		tmp &= ~(0x10);
		tmp |= (1 << 4);	/* MRSTB_EN */
		sec_reg_write(s2mpu05->iodev, S2MPU05_REG_CTRL1, tmp);
	} else {
		int debug_level = sec_debug_get_debug_level();
		/* 2 key hard reset */
		pr_info("PM: Set device 2-key reset. (debug_level = %d)\n", debug_level);

		if (debug_level >= 1) {
			/* MID, HIGH: warm reset */
			/* Disable manual reset */
			sec_reg_read(s2mpu05->iodev, S2MPU05_REG_CTRL1, &tmp);
			tmp &= ~(0x10);
			tmp |= (0 << 4);	/* MRSTB_EN */
			sec_reg_write(s2mpu05->iodev, S2MPU05_REG_CTRL1, tmp);

			/* Enable warm reset */
			sec_reg_read(s2mpu05->iodev, S2MPU05_REG_CTRL3, &tmp);
			tmp &= ~(0x70);
			tmp |= (1 << 6) | (0 << 5) | (1 << 4);	/* set MRSEL bit to 0 */
			sec_reg_write(s2mpu05->iodev, S2MPU05_REG_CTRL3, tmp);
		} else {
			/* LOW: manual reset */
			/* Disable warm reset */
			sec_reg_read(s2mpu05->iodev, S2MPU05_REG_CTRL3, &tmp);
			tmp &= ~(0x70);
			tmp |= (0 << 6) | (0 << 5) | (0 << 4);	/* set MRSEL bit to 0 */
			sec_reg_write(s2mpu05->iodev, S2MPU05_REG_CTRL3, tmp);

			/* Enable manual reset */
			sec_reg_read(s2mpu05->iodev, S2MPU05_REG_CTRL1, &tmp);
			tmp &= ~(0x10);
			tmp |= (1 << 4);	/* MRSTB_EN */
			sec_reg_write(s2mpu05->iodev, S2MPU05_REG_CTRL1, tmp);
		}
	}

	return ret;
}
#endif

#ifdef CONFIG_SEC_PM
static ssize_t chg_det_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	int ret, chg_det;
	u8 val;

	ret = sec_reg_read(static_info->iodev, S2MPU05_REG_ST1, &val);

	if(ret)
		chg_det = -1;
	else
		chg_det = !!(val & STATUS1_ACOK); // ACOK active high

	pr_info("%s: ap pmic chg det: %d\n", __func__, chg_det);

	return sprintf(buf, "%d\n", chg_det);
}

static DEVICE_ATTR_RO(chg_det);

static struct attribute *ap_pmic_attributes[] = {
	&dev_attr_chg_det.attr,
	NULL
};

static const struct attribute_group ap_pmic_attr_group = {
	.attrs = ap_pmic_attributes,
};
#endif /* CONFIG_SEC_PM */

static int s2mpu05_pmic_probe(struct platform_device *pdev)
{
	struct sec_pmic_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	struct sec_platform_data *pdata = iodev->pdata;
	struct regulator_config config = { };
	struct s2mpu05_info *s2mpu05;
	int i, ret;

	ret = sec_reg_read(iodev, S2MPU05_REG_ID, &SEC_PMIC_REV(iodev));
	if (ret < 0)
		return ret;

	if (iodev->dev->of_node) {
		ret = s2mpu05_pmic_dt_parse_pdata(iodev, pdata);
		if (ret)
			return ret;
	}

	if (!pdata) {
		dev_err(pdev->dev.parent, "Platform data not supplied\n");
		return -ENODEV;
	}

	s2mpu05 = devm_kzalloc(&pdev->dev, sizeof(struct s2mpu05_info),
				GFP_KERNEL);
	if (!s2mpu05)
		return -ENOMEM;

	s2mpu05->iodev = iodev;

	s2mpu05->g3d_en_pin =
		(const char *)of_get_property(iodev->dev->of_node,
						"buck2en_pin", NULL);
	s2mpu05->g3d_en_addr =
		(const char *)of_get_property(iodev->dev->of_node,
						"buck2en_addr", NULL);

	static_info = s2mpu05;

	s2mpu05->g3d_en = pdata->g3d_en;

	platform_set_drvdata(pdev, s2mpu05);

	for (i = 0; i < pdata->num_regulators; i++) {
		int id = pdata->regulators[i].id;
		config.dev = &pdev->dev;
		config.regmap = iodev->regmap;
		config.init_data = pdata->regulators[i].initdata;
		config.driver_data = s2mpu05;
		config.of_node = pdata->regulators[i].reg_node;
		s2mpu05->opmode[id] = regulators[id].enable_mask;

		s2mpu05->rdev[i] = regulator_register(&regulators[id], &config);
		if (IS_ERR(s2mpu05->rdev[i])) {
			ret = PTR_ERR(s2mpu05->rdev[i]);
			dev_err(&pdev->dev, "regulator init failed for %d\n",
				i);
			s2mpu05->rdev[i] = NULL;
			goto err;
		}
	}

	s2mpu05->num_regulators = pdata->num_regulators;

	/* SELMIF : Buck3,LDO4,6,7,8 controlled by PWREN_MIF */
	ret = sec_reg_update(iodev, S2MPU05_REG_SELMIF, 0x1F, 0x1F);
	if (ret) {
		dev_err(&pdev->dev, "set Buck2,LDO2,6,7,8,10 controlled by PWREN_MIF : error\n");
		return ret;
	}

	/* On sequence Config for MIF_G3D, CP_PWR_UP */
	sec_reg_write(iodev, 0x62, 0x4E);	/* seq. LDO9, LDO8 */
	sec_reg_write(iodev, 0x63, 0x33);	/* seq. LDO11, LDO10 */
	sec_reg_write(iodev, 0x64, 0x54);	/* seq. LDO13, LDO12 */
	sec_reg_write(iodev, 0x65, 0x76);	/* seq. LDO15, LDO14 */
	sec_reg_write(iodev, 0x71, 0x80);	/* seq. L11~L4 */
	sec_reg_write(iodev, 0x72, 0x0F);	/* seq. L19~L12 */

#ifdef CONFIG_SEC_PM
	ap_pmic_dev = sec_device_create(NULL, "ap_pmic");

	ret = sysfs_create_group(&ap_pmic_dev->kobj, &ap_pmic_attr_group);
	if (ret)
		dev_err(&pdev->dev, "failed to create ap_pmic sysfs group\n");
#endif /* CONFIG_SEC_PM */

#ifdef CONFIG_SEC_DEBUG_PMIC
	s2mpu05->pmic_test_class = class_create(THIS_MODULE, "pmic_test");
	pmic_test_dev = device_create(s2mpu05->pmic_test_class, NULL, 0, NULL, "s2mpu05");
	ret = device_create_file(pmic_test_dev, &dev_attr_pmic_i2c);
	if (ret) {
		dev_err(&pdev->dev, "create file to control i2c\n");
		return ret;
	}
	dev_set_drvdata(pmic_test_dev, s2mpu05);
	s2mpu05->pm_notifier.notifier_call = pmic_debug_print_all_regulators;
	ret = register_pm_notifier(&s2mpu05->pm_notifier);
	if (ret) {
		dev_err(&pdev->dev, "Failed to setup pm notifier\n");
		goto err;
	}
#endif

	return 0;

err:
	for (i = 0; i < S2MPU05_REGULATOR_MAX; i++)
		regulator_unregister(s2mpu05->rdev[i]);

	return ret;
}

static int s2mpu05_pmic_remove(struct platform_device *pdev)
{
	struct s2mpu05_info *s2mpu05 = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < S2MPU05_REGULATOR_MAX; i++)
		regulator_unregister(s2mpu05->rdev[i]);

	return 0;
}

static const struct platform_device_id s2mpu05_pmic_id[] = {
	{ "s2mpu05-pmic", 0},
	{ },
};
MODULE_DEVICE_TABLE(platform, s2mpu05_pmic_id);

static struct platform_driver s2mpu05_pmic_driver = {
	.driver = {
		.name = "s2mpu05-pmic",
		.owner = THIS_MODULE,
	},
	.probe = s2mpu05_pmic_probe,
	.remove = s2mpu05_pmic_remove,
	.id_table = s2mpu05_pmic_id,
};

static int __init s2mpu05_pmic_init(void)
{
	return platform_driver_register(&s2mpu05_pmic_driver);
}
subsys_initcall(s2mpu05_pmic_init);

static void __exit s2mpu05_pmic_exit(void)
{
	platform_driver_unregister(&s2mpu05_pmic_driver);
}
module_exit(s2mpu05_pmic_exit);

/* Module information */
MODULE_AUTHOR("Samsung LSI");
MODULE_DESCRIPTION("SAMSUNG S2MPU05 Regulator Driver");
MODULE_LICENSE("GPL");
