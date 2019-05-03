/*
 * s2mps16.c
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
#include <../drivers/pinctrl/samsung/pinctrl-samsung.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/mfd/samsung/core.h>
#include <linux/mfd/samsung/s2mps16.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/exynos-ss.h>

#include <soc/samsung/exynos-pmu.h>
#if defined(CONFIG_PWRCAL)
#include <../drivers/soc/samsung/pwrcal/pwrcal.h>
#if defined(CONFIG_SOC_EXYNOS8890)
#include <../drivers/soc/samsung/pwrcal/S5E8890/S5E8890-vclk.h>
#endif
#endif

#define EXYNOS_PMU_G3D_STATUS          0x4064
#define EXYNOS_PMU_GPU_DVS_CTRL                0x6100
#define EXYNOS_PMU_GPU_DVS_STATUS      0x6104
#define G3D_DVS_CTRL_ON                        (0x1 << 1)
#define G3D_DVS_CTRL_OFF               (0x0 << 1)
#define G3D_DVS_CTRL                   (0x1 << 1)
#define G3D_DVS_STATUS                 (0x1)
#define LOCAL_PWR_CFG                  (0xF << 0)

static struct s2mps16_info *static_info;
static struct regulator_desc regulators[][S2MPS16_REGULATOR_MAX];

struct s2mps16_info {
	struct regulator_dev *rdev[S2MPS16_REGULATOR_MAX];
	unsigned int opmode[S2MPS16_REGULATOR_MAX];
	unsigned int vsel_value[S2MPS16_REGULATOR_MAX];
	int num_regulators;
	bool dvs_en;
	struct sec_pmic_dev *iodev;
	bool g3d_en;
	bool cache_data;
	int dvs_pin;
	struct mutex lock;
	bool g3d_ocp;
	bool buck11_en;
	unsigned int desc_type;

};

/* Some LDOs supports [LPM/Normal]ON mode during suspend state */
static int s2m_set_mode(struct regulator_dev *rdev,
				     unsigned int mode)
{
	struct s2mps16_info *s2mps16 = rdev_get_drvdata(rdev);
	unsigned int val;
	int ret, id = rdev_get_id(rdev);

	switch (mode) {
	case SEC_OPMODE_SUSPEND:			/* ON in Standby Mode */
		val = 0x1 << S2MPS16_ENABLE_SHIFT;
		break;
	case SEC_OPMODE_MIF:			/* ON in PWREN_MIF mode */
		val = 0x2 << S2MPS16_ENABLE_SHIFT;
		break;
	case SEC_OPMODE_ON:			/* ON in Normal Mode */
		val = 0x3 << S2MPS16_ENABLE_SHIFT;
		break;
	default:
		pr_warn("%s: regulator_suspend_mode : 0x%x not supported\n",
			rdev->desc->name, mode);
		return -EINVAL;
	}

	ret = sec_reg_update(s2mps16->iodev, rdev->desc->enable_reg,
				  val, rdev->desc->enable_mask);
	if (ret)
		return ret;

	s2mps16->opmode[id] = val;
	return 0;
}

static int s2m_enable(struct regulator_dev *rdev)
{
	struct s2mps16_info *s2mps16 = rdev_get_drvdata(rdev);

	int reg_id = rdev_get_id(rdev);

	/* disregard BUCK6 enable */
	if (reg_id == S2MPS16_BUCK6 && s2mps16->g3d_en)
		return 0;

	return sec_reg_update(s2mps16->iodev, rdev->desc->enable_reg,
				  s2mps16->opmode[rdev_get_id(rdev)],
				  rdev->desc->enable_mask);
}

static int s2m_disable_regmap(struct regulator_dev *rdev)
{
	struct s2mps16_info *s2mps16 = rdev_get_drvdata(rdev);
	int reg_id = rdev_get_id(rdev);
	unsigned int val;

	/* disregard BUCK6 disable */
	if (reg_id == S2MPS16_BUCK6 && s2mps16->g3d_en)
		return 0;

	if (rdev->desc->enable_is_inverted)
		val = rdev->desc->enable_mask;
	else
		val = 0;

	return sec_reg_update(s2mps16->iodev, rdev->desc->enable_reg,
				  val, rdev->desc->enable_mask);
}

static int s2m_is_enabled_regmap(struct regulator_dev *rdev)
{
	struct s2mps16_info *s2mps16 = rdev_get_drvdata(rdev);
	int ret, reg_id = rdev_get_id(rdev);
	unsigned int val;
	/* DVS regulators (MIF, INT, APOLLO, ATLANTIC, DISP_CAM0) */
	if (reg_id == S2MPS16_BUCK1 || reg_id == S2MPS16_BUCK2 ||
		reg_id == S2MPS16_BUCK3 || reg_id == S2MPS16_BUCK4 ||
		reg_id == S2MPS16_BUCK5)
		return 1;

	/* BUCK6 is controlled by g3d gpio */
	if (reg_id == S2MPS16_BUCK6 && s2mps16->g3d_en) {
		exynos_pmu_read(EXYNOS_PMU_G3D_STATUS, &val);
		if ((val & LOCAL_PWR_CFG) == LOCAL_PWR_CFG)
			return 1;
		else
			return 0;
	} else {
		ret = sec_reg_read(s2mps16->iodev,
				rdev->desc->enable_reg, &val);
		if (ret)
			return ret;
	}

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
	struct s2mps16_info *s2mps16 = rdev_get_drvdata(rdev);
	int ramp_shift, reg_id = rdev_get_id(rdev);
	int ramp_mask = 0x03;
	unsigned int ramp_value = 0;

	ramp_value = get_ramp_delay(ramp_delay/1000);
	if (ramp_value > 4) {
		pr_warn("%s: ramp_delay: %d not supported\n",
			rdev->desc->name, ramp_delay);
	}

	switch (reg_id) {
	case S2MPS16_BUCK2:
	case S2MPS16_BUCK4:
	case S2MPS16_BUCK5:
		ramp_shift = 6;
		break;
	case S2MPS16_BUCK1:
	case S2MPS16_BUCK3:
	case S2MPS16_BUCK6:
		ramp_shift = 4;
		break;
	case S2MPS16_BUCK7:
	case S2MPS16_BUCK11:
		ramp_shift = 2;
		break;
	case S2MPS16_BUCK8:
	case S2MPS16_BUCK9:
	case S2MPS16_BB1:
		ramp_shift = 0;
		break;
	default:
		return -EINVAL;
	}

	return sec_reg_update(s2mps16->iodev, S2MPS16_REG_BUCK_RAMP,
		ramp_value << ramp_shift, ramp_mask << ramp_shift);
}

static int s2m_get_voltage_sel_regmap(struct regulator_dev *rdev)
{
	struct s2mps16_info *s2mps16 = rdev_get_drvdata(rdev);
	int ret, reg_id = rdev_get_id(rdev);
	unsigned int val;

	/* if dvs pin set high, get the voltage on the diffrent register. */
	if (reg_id == S2MPS16_BUCK6
			&& s2mps16->dvs_en && s2m_get_dvs_is_on()) {
		ret = sec_reg_read(s2mps16->iodev, S2MPS16_REG_B6CTRL2, &val);
		if (ret)
			return ret;
	} else if ((reg_id >= S2MPS16_BUCK1 && reg_id <= S2MPS16_BUCK5)
			&& s2mps16->vsel_value[reg_id] && s2mps16->cache_data) {
		return s2mps16->vsel_value[reg_id];
	} else {
		ret = sec_reg_read(s2mps16->iodev, rdev->desc->vsel_reg, &val);
		if (ret)
			return ret;
	}

	val &= rdev->desc->vsel_mask;

	return val;
}

static int s2m_set_voltage_sel_regmap(struct regulator_dev *rdev, unsigned sel)
{
	struct s2mps16_info *s2mps16 = rdev_get_drvdata(rdev);
	int reg_id = rdev_get_id(rdev);
	int ret;
	char name[16];
	unsigned int voltage;

	/* voltage information logging to snapshot feature */
	snprintf(name, sizeof(name), "LDO%d", (reg_id - S2MPS16_LDO1) + 1);
	voltage = ((sel & rdev->desc->vsel_mask) * S2MPS16_LDO_STEP2) + S2MPS16_LDO_MIN1;
	exynos_ss_regulator(name, rdev->desc->vsel_reg, voltage, ESS_FLAG_IN);

	ret = sec_reg_update(s2mps16->iodev, rdev->desc->vsel_reg,
				  sel, rdev->desc->vsel_mask);
	if (ret < 0)
		goto out;

	if (rdev->desc->apply_bit)
		ret = sec_reg_update(s2mps16->iodev, rdev->desc->apply_reg,
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
	struct s2mps16_info *s2mps16 = rdev_get_drvdata(rdev);
	int reg_id = rdev_get_id(rdev);
	unsigned int voltage;
	char name[16];

	/* If dvs_en = 0, dvs_pin = 1, occur BUG_ON */
	if (reg_id == S2MPS16_BUCK6
		&& !s2mps16->dvs_en && gpio_is_valid(s2mps16->dvs_pin)) {
		BUG_ON(s2m_get_dvs_is_on());
	}
	/* voltage information logging to snapshot feature */
	snprintf(name, sizeof(name), "BUCK%d", (reg_id - S2MPS16_BUCK1) + 1);
	voltage = (sel * S2MPS16_BUCK_STEP1) + S2MPS16_BUCK_MIN1;
	exynos_ss_regulator(name, rdev->desc->vsel_reg, voltage, ESS_FLAG_IN);

	if (reg_id == S2MPS16_BUCK1 || reg_id == S2MPS16_BUCK2 ||
		reg_id == S2MPS16_BUCK3 || reg_id == S2MPS16_BUCK4 ||
		reg_id == S2MPS16_BUCK5)
		s2mps16->vsel_value[reg_id] = sel;

	ret = sec_reg_write(s2mps16->iodev, rdev->desc->vsel_reg, sel);
	if (ret < 0)
		goto i2c_out;

	if (rdev->desc->apply_bit)
		ret = sec_reg_update(s2mps16->iodev, rdev->desc->apply_reg,
					 rdev->desc->apply_bit,
					 rdev->desc->apply_bit);
	exynos_ss_regulator(name, rdev->desc->vsel_reg, voltage, ESS_FLAG_OUT);

	return ret;

i2c_out:
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

void s2m_init_dvs()
{
	if (cal_dfs_ext_ctrl(dvfs_g3d, cal_dfs_dvs, 2) != 0) {
		pr_info("%s: failed to init dvs\n", __func__);
	}
}

int s2m_get_dvs_is_enable()
{
	unsigned int val;

	exynos_pmu_read(EXYNOS_PMU_GPU_DVS_CTRL, &val);
	return (val & G3D_DVS_CTRL);
}
EXPORT_SYMBOL_GPL(s2m_get_dvs_is_enable);

int s2m_get_dvs_is_on()
{
	unsigned int val;
	exynos_pmu_read(EXYNOS_PMU_GPU_DVS_STATUS, &val);
	return !(val & G3D_DVS_STATUS);
}
EXPORT_SYMBOL_GPL(s2m_get_dvs_is_on);

int s2m_set_dvs_pin(bool gpio_val)
{
	unsigned int temp, count = 0;
	if (!static_info->dvs_en || !gpio_is_valid(static_info->dvs_pin)) {
		pr_warn("%s: dvs pin ctrl failed\n", __func__);
		return -EINVAL;
	}

	/* wait for 90us, 100us when dvs pin control */
	if (gpio_val) {
		exynos_pmu_read(EXYNOS_PMU_GPU_DVS_CTRL, &temp);
		exynos_pmu_write(EXYNOS_PMU_GPU_DVS_CTRL, temp | G3D_DVS_CTRL_ON);
		udelay(90);
	} else {
		exynos_pmu_read(EXYNOS_PMU_GPU_DVS_CTRL, &temp);
		exynos_pmu_write(EXYNOS_PMU_GPU_DVS_CTRL,
				(temp & ~G3D_DVS_CTRL) | G3D_DVS_CTRL_OFF);
		do {
			udelay(100);
			if (count > 3) {
				pr_warn("%s: dvs pin ctrl off failed\n",
								__func__);
				return -EINVAL;
			}
			exynos_pmu_read(EXYNOS_PMU_GPU_DVS_STATUS, &temp);
			count++;
		} while (!(temp & G3D_DVS_STATUS));
	}

	return 0;
}
EXPORT_SYMBOL_GPL(s2m_set_dvs_pin);

static struct regulator_ops s2mps16_ldo_ops = {
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

static struct regulator_ops s2mps16_buck_ops = {
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

#define _BUCK(macro)	S2MPS16_BUCK##macro
#define _buck_ops(num)	s2mps16_buck_ops##num

#define _LDO(macro)	S2MPS16_LDO##macro
#define _REG(ctrl)	S2MPS16_REG##ctrl
#define _ldo_ops(num)	s2mps16_ldo_ops##num
#define _TIME(macro)	S2MPS16_ENABLE_TIME##macro

#define BUCK_DESC(_name, _id, _ops, m, s, v, e, t)	{	\
	.name		= _name,				\
	.id		= _id,					\
	.ops		= _ops,					\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.min_uV		= m,					\
	.uV_step	= s,					\
	.n_voltages	= S2MPS16_BUCK_N_VOLTAGES,		\
	.vsel_reg	= v,					\
	.vsel_mask	= S2MPS16_BUCK_VSEL_MASK,		\
	.enable_reg	= e,					\
	.enable_mask	= S2MPS16_ENABLE_MASK,			\
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
	.n_voltages	= S2MPS16_LDO_N_VOLTAGES,		\
	.vsel_reg	= v,					\
	.vsel_mask	= S2MPS16_LDO_VSEL_MASK,		\
	.enable_reg	= e,					\
	.enable_mask	= S2MPS16_ENABLE_MASK,			\
	.enable_time	= t					\
}

enum regulator_desc_type {
	S2MPS16_DESC_TYPE0 = 0,
};

static struct regulator_desc regulators[][S2MPS16_REGULATOR_MAX] = {
	[S2MPS16_DESC_TYPE0] = {
		/* name, id, ops, min_uv, uV_step, vsel_reg, enable_reg */
		LDO_DESC("LDO1", _LDO(1), &_ldo_ops(), _LDO(_MIN2),
		_LDO(_STEP1), _REG(_L1CTRL), _REG(_L1CTRL), _TIME(_LDO)),
		LDO_DESC("LDO2", _LDO(2), &_ldo_ops(), _LDO(_MIN4),
		_LDO(_STEP2), _REG(_L2CTRL), _REG(_L2CTRL), _TIME(_LDO)),
		LDO_DESC("LDO3", _LDO(3), &_ldo_ops(), _LDO(_MIN3),
		_LDO(_STEP2), _REG(_L3CTRL), _REG(_L3CTRL), _TIME(_LDO)),
		LDO_DESC("LDO4", _LDO(4), &_ldo_ops(), _LDO(_MIN3),
		_LDO(_STEP1), _REG(_L4CTRL), _REG(_L4CTRL), _TIME(_LDO)),
		LDO_DESC("LDO5", _LDO(5), &_ldo_ops(), _LDO(_MIN3),
		_LDO(_STEP2), _REG(_L5CTRL), _REG(_L5CTRL), _TIME(_LDO)),
		LDO_DESC("LDO6", _LDO(6), &_ldo_ops(), _LDO(_MIN4),
		_LDO(_STEP2), _REG(_L6CTRL), _REG(_L6CTRL), _TIME(_LDO)),
		LDO_DESC("LDO7", _LDO(7), &_ldo_ops(), _LDO(_MIN1),
		_LDO(_STEP2), _REG(_L7CTRL), _REG(_L7CTRL), _TIME(_LDO)),
		LDO_DESC("LDO8", _LDO(8), &_ldo_ops(), _LDO(_MIN1),
		_LDO(_STEP2), _REG(_L8CTRL), _REG(_L8CTRL), _TIME(_LDO)),
		LDO_DESC("LDO9", _LDO(9), &_ldo_ops(), _LDO(_MIN1),
		_LDO(_STEP2), _REG(_L9CTRL), _REG(_L9CTRL), _TIME(_LDO)),
		LDO_DESC("LDO10", _LDO(10), &_ldo_ops(), _LDO(_MIN1),
		_LDO(_STEP2), _REG(_L10CTRL), _REG(_L10CTRL), _TIME(_LDO)),
		LDO_DESC("LDO11", _LDO(11), &_ldo_ops(), _LDO(_MIN1),
		_LDO(_STEP2), _REG(_L11CTRL), _REG(_L11CTRL), _TIME(_LDO)),
		LDO_DESC("LDO12", _LDO(12), &_ldo_ops(), _LDO(_MIN3),
		_LDO(_STEP1), _REG(_L12CTRL), _REG(_L12CTRL), _TIME(_LDO)),
		LDO_DESC("LDO13", _LDO(13), &_ldo_ops(), _LDO(_MIN3),
		_LDO(_STEP1), _REG(_L13CTRL), _REG(_L13CTRL), _TIME(_LDO)),
		LDO_DESC("LDO25", _LDO(25), &_ldo_ops(), _LDO(_MIN3),
		_LDO(_STEP1), _REG(_L25CTRL), _REG(_L25CTRL), _TIME(_LDO)),
		LDO_DESC("LDO26", _LDO(26), &_ldo_ops(), _LDO(_MIN3),
		_LDO(_STEP1), _REG(_L26CTRL), _REG(_L26CTRL), _TIME(_LDO)),
		LDO_DESC("LDO27", _LDO(27), &_ldo_ops(), _LDO(_MIN3),
		_LDO(_STEP1), _REG(_L27CTRL), _REG(_L27CTRL), _TIME(_LDO)),
		LDO_DESC("LDO28", _LDO(28), &_ldo_ops(), _LDO(_MIN4),
		_LDO(_STEP2), _REG(_L28CTRL), _REG(_L28CTRL), _TIME(_LDO)),
		LDO_DESC("LDO29", _LDO(29), &_ldo_ops(), _LDO(_MIN4),
		_LDO(_STEP2), _REG(_L29CTRL), _REG(_L29CTRL), _TIME(_LDO)),
		LDO_DESC("LDO30", _LDO(30), &_ldo_ops(), _LDO(_MIN3),
		_LDO(_STEP2), _REG(_L30CTRL), _REG(_L30CTRL), _TIME(_LDO)),
		LDO_DESC("LDO31", _LDO(31), &_ldo_ops(), _LDO(_MIN3),
		_LDO(_STEP1), _REG(_L31CTRL), _REG(_L31CTRL), _TIME(_LDO)),
		LDO_DESC("LDO32", _LDO(32), &_ldo_ops(), _LDO(_MIN3),
		_LDO(_STEP2), _REG(_L32CTRL), _REG(_L32CTRL), _TIME(_LDO)),
		LDO_DESC("LDO33", _LDO(33), &_ldo_ops(), _LDO(_MIN4),
		_LDO(_STEP2), _REG(_L33CTRL), _REG(_L33CTRL), _TIME(_LDO)),
		LDO_DESC("LDO34", _LDO(34), &_ldo_ops(), _LDO(_MIN4),
		_LDO(_STEP2), _REG(_L34CTRL), _REG(_L34CTRL), _TIME(_LDO)),
		LDO_DESC("LDO35", _LDO(35), &_ldo_ops(), _LDO(_MIN4),
		_LDO(_STEP2), _REG(_L35CTRL), _REG(_L35CTRL), _TIME(_LDO)),
		LDO_DESC("LDO36", _LDO(36), &_ldo_ops(), _LDO(_MIN4),
		_LDO(_STEP2), _REG(_L36CTRL), _REG(_L36CTRL), _TIME(_LDO)),
		LDO_DESC("LDO37", _LDO(37), &_ldo_ops(), _LDO(_MIN3),
		_LDO(_STEP2), _REG(_L37CTRL), _REG(_L37CTRL), _TIME(_LDO)),
		LDO_DESC("LDO38", _LDO(38), &_ldo_ops(), _LDO(_MIN3),
		_LDO(_STEP1), _REG(_L38CTRL), _REG(_L38CTRL), _TIME(_LDO)),
		BUCK_DESC("BUCK1", _BUCK(1), &_buck_ops(), _BUCK(_MIN1),
		_BUCK(_STEP1), _REG(_B1CTRL2), _REG(_B1CTRL1), _TIME(_BUCK1)),
		BUCK_DESC("BUCK2", _BUCK(2), &_buck_ops(), _BUCK(_MIN1),
		_BUCK(_STEP1), _REG(_B2CTRL2), _REG(_B2CTRL1), _TIME(_BUCK2)),
		BUCK_DESC("BUCK3", _BUCK(3), &_buck_ops(), _BUCK(_MIN1),
		_BUCK(_STEP1), _REG(_B3CTRL2), _REG(_B3CTRL1), _TIME(_BUCK3)),
		BUCK_DESC("BUCK4", _BUCK(4), &_buck_ops(), _BUCK(_MIN1),
		_BUCK(_STEP1), _REG(_B4CTRL2), _REG(_B4CTRL1), _TIME(_BUCK4)),
		BUCK_DESC("BUCK5", _BUCK(5), &_buck_ops(), _BUCK(_MIN1),
		_BUCK(_STEP1), _REG(_B5CTRL2), _REG(_B5CTRL1), _TIME(_BUCK5)),
		BUCK_DESC("BUCK6", _BUCK(6), &_buck_ops(), _BUCK(_MIN1),
		_BUCK(_STEP1), _REG(_B6CTRL3), _REG(_B6CTRL1), _TIME(_BUCK6)),
		BUCK_DESC("BUCK7", _BUCK(7), &_buck_ops(), _BUCK(_MIN1),
		_BUCK(_STEP1), _REG(_B7CTRL2), _REG(_B7CTRL1), _TIME(_BUCK7)),
		BUCK_DESC("BUCK8", _BUCK(8), &_buck_ops(), _BUCK(_MIN2),
		_BUCK(_STEP2), _REG(_B8CTRL2), _REG(_B8CTRL1), _TIME(_BUCK8)),
		BUCK_DESC("BUCK9", _BUCK(9), &_buck_ops(), _BUCK(_MIN2),
		_BUCK(_STEP2), _REG(_B9CTRL2), _REG(_B9CTRL1), _TIME(_BUCK9)),
		BUCK_DESC("BUCK11", _BUCK(11), &_buck_ops(), _BUCK(_MIN1),
		_BUCK(_STEP1), _REG(_B11CTRL2), _REG(_B11CTRL1),
		_TIME(_BUCK11)),
		BUCK_DESC("BB", S2MPS16_BB1, &_buck_ops(), _BUCK(_MIN3),
		_BUCK(_STEP2), _REG(_BB1CTRL2), _REG(_BB1CTRL1),
		_TIME(_BB)),
	},

};
#ifdef CONFIG_OF
static int s2mps16_pmic_dt_parse_pdata(struct sec_pmic_dev *iodev,
					struct sec_platform_data *pdata)
{
	struct device_node *pmic_np, *regulators_np, *reg_np;
	struct sec_regulator_data *rdata;
	unsigned int i, s2mps16_desc_type;
	int ret;
	u32 val;
	pdata->smpl_warn_vth = 0;
	pdata->smpl_warn_hys = 0;

	pmic_np = iodev->dev->of_node;
	if (!pmic_np) {
		dev_err(iodev->dev, "could not find pmic sub-node\n");
		return -ENODEV;
	}
	/* get 2 gpio values */
	if (of_gpio_count(pmic_np) < 2) {
		dev_err(iodev->dev, "could not find pmic gpios\n");
		return -EINVAL;
	}
	pdata->smpl_warn = of_get_gpio(pmic_np, 0);
	pdata->dvs_pin = of_get_gpio(pmic_np, 1);

	ret = of_property_read_u32(pmic_np, "cache_data", &val);
	if (ret)
		return -EINVAL;
	pdata->cache_data = !!val;

	ret = of_property_read_u32(pmic_np, "g3d_en", &val);
	if (ret)
		return -EINVAL;
	pdata->g3d_en = !!val;

	ret = of_property_read_u32(pmic_np, "dvs_en", &val);
	if (ret)
		return -EINVAL;
	pdata->dvs_en = !!val;

	ret = of_property_read_u32(pmic_np, "smpl_warn_en", &val);
	if (ret)
		return -EINVAL;
	pdata->smpl_warn_en = !!val;

	ret = of_property_read_u32(pmic_np, "smpl_warn_vth", &val);
	if (ret)
		return -EINVAL;
	pdata->smpl_warn_vth = val;

	ret = of_property_read_u32(pmic_np, "smpl_warn_hys", &val);
	if (ret)
		return -EINVAL;
	pdata->smpl_warn_hys = val;

	ret = of_property_read_u32(pmic_np, "ldo8_7_seq", &val);
	if (ret)
		pdata->ldo8_7_seq = 0x05;
	else
		pdata->ldo8_7_seq = val;

	ret = of_property_read_u32(pmic_np, "ldo10_9_seq", &val);
	if (ret)
		pdata->ldo10_9_seq = 0x61;
	else
		pdata->ldo10_9_seq = val;

	pdata->adc_en = false;
	if (of_find_property(pmic_np, "adc-on", NULL))
		pdata->adc_en = true;
	iodev->adc_en = pdata->adc_en;

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
	s2mps16_desc_type = S2MPS16_DESC_TYPE0;
	for_each_child_of_node(regulators_np, reg_np) {
		for (i = 0; i < ARRAY_SIZE(regulators[s2mps16_desc_type]); i++)
			if (!of_node_cmp(reg_np->name,
					regulators[s2mps16_desc_type][i].name))
				break;

		if (i == ARRAY_SIZE(regulators[s2mps16_desc_type])) {
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
static int s2mps16_pmic_dt_parse_pdata(struct sec_pmic_dev *iodev,
					struct sec_platform_data *pdata)
{
	return 0;
}
#endif /* CONFIG_OF */

static int s2mps16_pmic_probe(struct platform_device *pdev)
{
	struct sec_pmic_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	struct sec_platform_data *pdata = iodev->pdata;
	struct regulator_config config = { };
	struct s2mps16_info *s2mps16;
	int i, ret;
	unsigned int s2mps16_desc_type;

	ret = sec_reg_read(iodev, S2MPS16_REG_ID, &SEC_PMIC_REV(iodev));
	if (ret < 0)
		return ret;

	s2mps16_desc_type = S2MPS16_DESC_TYPE0;
	if (iodev->dev->of_node) {
		ret = s2mps16_pmic_dt_parse_pdata(iodev, pdata);
		if (ret)
			return ret;
	}

	if (!pdata) {
		dev_err(pdev->dev.parent, "Platform data not supplied\n");
		return -ENODEV;
	}

	s2mps16 = devm_kzalloc(&pdev->dev, sizeof(struct s2mps16_info),
				GFP_KERNEL);
	if (!s2mps16)
		return -ENOMEM;

	s2mps16->desc_type = s2mps16_desc_type;
	s2mps16->iodev = iodev;
	s2mps16->buck11_en = (const char *)of_find_property(iodev->dev->of_node,
							"buck11_en", NULL);
	mutex_init(&s2mps16->lock);

	static_info = s2mps16;

	s2mps16->dvs_en = pdata->dvs_en;
	s2mps16->g3d_en = pdata->g3d_en;
	s2mps16->cache_data = pdata->cache_data;

	if (gpio_is_valid(pdata->dvs_pin)) {
		ret = devm_gpio_request(&pdev->dev, pdata->dvs_pin,
					"S2MPS16 DVS_PIN");
		if (ret < 0)
			return ret;
		if (pdata->dvs_en) {
			/* Set DVS Regulator Voltage 0x30 - 0.6 voltage */
			ret = sec_reg_write(iodev, S2MPS16_REG_B6CTRL2, 0x30);
			if (ret < 0)
				return ret;
			s2m_init_dvs();
			s2mps16->dvs_pin = pdata->dvs_pin;
		} else
			dev_err(&pdev->dev, "g3d dvs is not enabled.\n");
	}

	platform_set_drvdata(pdev, s2mps16);

	for (i = 0; i < pdata->num_regulators; i++) {
		int id = pdata->regulators[i].id;
		config.dev = &pdev->dev;
		config.regmap = iodev->regmap;
		config.init_data = pdata->regulators[i].initdata;
		config.driver_data = s2mps16;
		config.of_node = pdata->regulators[i].reg_node;
		s2mps16->opmode[id] =
			regulators[s2mps16_desc_type][id].enable_mask;

		s2mps16->rdev[i] = regulator_register(
				&regulators[s2mps16_desc_type][id], &config);
		if (IS_ERR(s2mps16->rdev[i])) {
			ret = PTR_ERR(s2mps16->rdev[i]);
			dev_err(&pdev->dev, "regulator init failed for %d\n",
				i);
			s2mps16->rdev[i] = NULL;
			goto err;
		}
	}

	s2mps16->num_regulators = pdata->num_regulators;


	if (pdata->g3d_en) {
		/* for buck6 gpio control, disable i2c control */
		ret = sec_reg_update(iodev, S2MPS16_REG_B6CTRL1,
				0x80, 0xC0);
		if (ret) {
			dev_err(&pdev->dev, "buck6 gpio control error\n");
			goto err;
		}
		if (!s2mps16->buck11_en) {
			ret = sec_reg_update(iodev, S2MPS16_REG_L11CTRL,
					0x00, 0xC0);
			if (ret) {
				dev_err(&pdev->dev, "regulator sync enable error\n");
					goto err;
			}
		} else {
			/* for buck11 gpio control, disable i2c control */
			ret = sec_reg_update(iodev, S2MPS16_REG_B11CTRL1,
					0x80, 0xC0);
			if (ret) {
				dev_err(&pdev->dev, "buck11 gpio control error\n");
				goto err;
			}
		}
	}

	if (pdata->smpl_warn_en) {
		ret = sec_reg_update(iodev, S2MPS16_REG_CTRL2,
						pdata->smpl_warn_vth, 0xe0);
		if (ret) {
			dev_err(&pdev->dev, "set smpl_warn configuration i2c write error\n");
			goto err;
		}
		pr_info("%s: smpl_warn vth is 0x%x\n", __func__,
							pdata->smpl_warn_vth);

		ret = sec_reg_update(iodev, S2MPS16_REG_CTRL2,
						pdata->smpl_warn_hys, 0x18);
		if (ret) {
			dev_err(&pdev->dev, "set smpl_warn configuration i2c write error\n");
			goto err;
		}
		pr_info("%s: smpl_warn hysteresis is 0x%x\n", __func__,
							pdata->smpl_warn_hys);
	}

	/* RTC Low jitter mode */
	ret = sec_reg_update(iodev, S2MPS16_REG_RTC_BUF, 0x10, 0x10);
	if (ret) {
		dev_err(&pdev->dev, "set low jitter mode i2c write error\n");
		goto err;
	}

	/* SELMIF set LDO2,4,5,6,8,9,12,13 controlled by PWREN_MIF */
	ret = sec_reg_write(iodev, S2MPS16_REG_SELMIF, 0xFF);
	if (ret) {
		dev_err(&pdev->dev, "set selmif sel error\n");
		goto err;
	}

	sec_reg_update(iodev, S2MPS16_REG_B4CTRL1, 0x00, 0x10);

	/* On sequence Config for PWREN_MIF */
	sec_reg_write(iodev, 0x70, 0xB4);	/* seq. Buck2, Buck1 */
	sec_reg_write(iodev, 0x71, 0x2C);	/* seq. Buck4, Buck3 */
	sec_reg_write(iodev, 0x72, 0xD4);	/* seq. Buck6, Buck5 */
	sec_reg_write(iodev, 0x73, 0x11);	/* seq. Buck8, Buck7 */
	sec_reg_write(iodev, 0x74, 0xE0);	/* seq. Buck10, Buck9 */
	if (s2mps16->buck11_en)
		sec_reg_write(iodev, 0x75, 0x27);	/* seq. BB, Buck11 */
	else
		sec_reg_write(iodev, 0x75, 0x20);
	sec_reg_write(iodev, 0x76, 0x93);	/* seq. LDO2, LDO1 */
	sec_reg_write(iodev, 0x77, 0x60);	/* Seq. LDO4, LDO3 */
	sec_reg_write(iodev, 0x78, 0x87);	/* seq. LDO6, LDO5 */
	sec_reg_write(iodev, 0x79, pdata->ldo8_7_seq);	/* Seq. LDO8, LDO7 */
	sec_reg_write(iodev, 0x7A, pdata->ldo10_9_seq);	/* Seq. LDO10, LDO9 */
	if (s2mps16->buck11_en)
		sec_reg_write(iodev, 0x7B, 0x50);	/* Seq. LDO12, LDO11 */
	else
		sec_reg_write(iodev, 0x7B, 0x57);
	sec_reg_write(iodev, 0x7C, 0x75);	/* Seq. LDO14, LDO13 */
	sec_reg_write(iodev, 0x7D, 0x98);	/* Seq. LDO16, LDO15 */
	sec_reg_write(iodev, 0x7E, 0x00);	/* Seq. LDO18, LDO17 */
	sec_reg_write(iodev, 0x7F, 0x00);	/* Seq. LDO20, LDO19 */
	sec_reg_write(iodev, 0x80, 0x21);	/* Seq. LDO22, LDO21 */
	sec_reg_write(iodev, 0x81, 0x3F);	/* Seq. LDO24, LDO23 */
	sec_reg_write(iodev, 0x82, 0x00);	/* Seq. LDO26, LDO25 */
	sec_reg_write(iodev, 0x83, 0x40);	/* Seq. B1~B8*/
	sec_reg_write(iodev, 0x84, 0x40);	/* Seq. B9~B11, BB, LDO1~4 */
	sec_reg_write(iodev, 0x85, 0x00);	/* Seq. LDO5~12 */
	sec_reg_write(iodev, 0x86, 0x0E);	/* Seq. LDO13~20 */
	sec_reg_write(iodev, 0x87, 0x00);	/* Seq. LDO26~21 */

	return 0;
err:
	for (i = 0; i < S2MPS16_REGULATOR_MAX; i++)
		regulator_unregister(s2mps16->rdev[i]);

	return ret;
}

static int s2mps16_pmic_remove(struct platform_device *pdev)
{
	struct s2mps16_info *s2mps16 = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < S2MPS16_REGULATOR_MAX; i++)
		regulator_unregister(s2mps16->rdev[i]);

	return 0;
}

static const struct platform_device_id s2mps16_pmic_id[] = {
	{ "s2mps16-pmic", 0},
	{ },
};
MODULE_DEVICE_TABLE(platform, s2mps16_pmic_id);

static struct platform_driver s2mps16_pmic_driver = {
	.driver = {
		.name = "s2mps16-pmic",
		.owner = THIS_MODULE,
	},
	.probe = s2mps16_pmic_probe,
	.remove = s2mps16_pmic_remove,
	.id_table = s2mps16_pmic_id,
};

static int __init s2mps16_pmic_init(void)
{
	return platform_driver_register(&s2mps16_pmic_driver);
}
subsys_initcall(s2mps16_pmic_init);

static void __exit s2mps16_pmic_exit(void)
{
	platform_driver_unregister(&s2mps16_pmic_driver);
}
module_exit(s2mps16_pmic_exit);

/* Module information */
MODULE_AUTHOR("Sangbeom Kim <sbkim73@samsung.com>");
MODULE_DESCRIPTION("SAMSUNG S2MPS16 Regulator Driver");
MODULE_LICENSE("GPL");
