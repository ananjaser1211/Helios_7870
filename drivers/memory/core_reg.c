/*
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS - Showing system control registers of ARM/MNGS core
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/kthread.h>
#include <asm/core_regs.h>

struct device *dev;
static int core;
static struct task_struct *task;

#define DEF_REG_STRUCT(n) \
	struct register_type reg_##n = {	\
	.name = ""#n,	\
	.read_reg = mrs_##n##_read	\
};

/* armv8 */
DEF_REG_STRUCT(SCTLR)
DEF_REG_STRUCT(MAIR)
DEF_REG_STRUCT(CPUACTLR)
DEF_REG_STRUCT(CPUECTLR)
DEF_REG_STRUCT(L2CTLR)
DEF_REG_STRUCT(L2ACTLR)
DEF_REG_STRUCT(L2ECTLR)
DEF_REG_STRUCT(MPIDR)
DEF_REG_STRUCT(MIDR)
DEF_REG_STRUCT(REVIDR)

/* mongoose */
DEF_REG_STRUCT(FEACTLR)
DEF_REG_STRUCT(FEACTLR2)
DEF_REG_STRUCT(FEACTLR3)
DEF_REG_STRUCT(FPACTLR)
DEF_REG_STRUCT(FPACTLR2)
DEF_REG_STRUCT(LSACTLR)
DEF_REG_STRUCT(LSACTLR2)
DEF_REG_STRUCT(LSACTLR3)
DEF_REG_STRUCT(LSACTLR4)
DEF_REG_STRUCT(CKACTLR)
DEF_REG_STRUCT(MCACTLR2)

#define SET_CORE_REG(r, v) { .reg = &reg_##r, .val = v }

static struct core_register a53_regs[] = {
	SET_CORE_REG(SCTLR, 0),
	SET_CORE_REG(MAIR, 0),
	SET_CORE_REG(MPIDR, 0),
	SET_CORE_REG(MIDR, 0),
	SET_CORE_REG(REVIDR, 0),
	SET_CORE_REG(CPUACTLR, 1),
	SET_CORE_REG(CPUECTLR, 1),
	SET_CORE_REG(L2CTLR, 1),
	SET_CORE_REG(L2ACTLR, 1),
	SET_CORE_REG(L2ECTLR, 1),
	{},
};

static struct core_register a57_regs[] = {
	SET_CORE_REG(SCTLR, 0),
	SET_CORE_REG(MAIR, 0),
	SET_CORE_REG(MPIDR, 0),
	SET_CORE_REG(MIDR, 0),
	SET_CORE_REG(REVIDR, 0),
	SET_CORE_REG(CPUACTLR, 1),
	SET_CORE_REG(CPUECTLR, 1),
	SET_CORE_REG(L2CTLR, 1),
	SET_CORE_REG(L2ACTLR, 1),
	SET_CORE_REG(L2ECTLR, 1),
	{},
};

static struct core_register mngs_regs[] = {
	SET_CORE_REG(SCTLR, 0),
	SET_CORE_REG(MAIR, 0),
	SET_CORE_REG(MPIDR, 0),
	SET_CORE_REG(MIDR, 0),
	SET_CORE_REG(REVIDR, 0),
	SET_CORE_REG(CPUACTLR, 1),
	SET_CORE_REG(CPUECTLR, 1),
	SET_CORE_REG(L2CTLR, 1),
	SET_CORE_REG(L2ACTLR, 1),
	SET_CORE_REG(L2ECTLR, 1),
	SET_CORE_REG(FEACTLR, 1),
	SET_CORE_REG(FEACTLR2, 1),
	SET_CORE_REG(FEACTLR3, 1),
	SET_CORE_REG(FPACTLR, 1),
	SET_CORE_REG(FPACTLR2, 1),
	SET_CORE_REG(LSACTLR, 1),
	SET_CORE_REG(LSACTLR2, 1),
	SET_CORE_REG(LSACTLR3, 1),
	SET_CORE_REG(LSACTLR4, 1),
	SET_CORE_REG(CKACTLR, 1),
	SET_CORE_REG(MCACTLR2, 0),
	{},
};

enum armv8_core_type {
	A57_CORE,
	A53_CORE,
	MNGS_CORE,
};

static char *core_names[] = {"cortex-a57", "cortex-a53", "mongoose"};

static enum armv8_core_type get_core_type(void)
{
	u32 midr = (u32)mrs_MIDR_read();
	if ((midr >> 24) == 0x53) {
		if ((midr & 0xfff0) == 0x10)
			return MNGS_CORE;
	} else if ((midr >> 24) == 0x41) {
		if ((midr & 0xfff0) == 0xD070)
			return A57_CORE;
		else if ((midr & 0xfff0) == 0xD030)
			return A53_CORE;
	}
	return 0;
}

static u64 get_guide_value_from_dt(int core, const char *name)
{
	struct device_node *dn = NULL;
	u64 value;
	int ret;
	enum armv8_core_type ct = get_core_type();
	char *core_name = core_names[ct];

	dn = dev->of_node;
	dn = of_get_child_by_name(dn, core_name);
	if (!dn) {
		pr_info("cannot find node:%s\n", core_name);
		return -1;
	}
	dn = of_get_child_by_name(dn, name);
	if (!dn) {
		pr_info("cannot find node:%s\n", name);
		return -1;
	}
	ret = of_property_read_u64(dn, "guide", &value);
	if (ret) {
		pr_info("cannot read guide value of :%s\n", name);
		return -1;
	}

	return value;
}

int coretest_init(void)
{
	u64 val;
	int diff_cnt = 0;
	enum armv8_core_type core_type = get_core_type();
	struct core_register *regs;
	u32 midr = (u32)mrs_MIDR_read();
	u32 revidr = (u32)mrs_REVIDR_read();

	pr_info("[Core type: %s, Rev: R%xP%x (revidr:0x%08x)]\n",
			core_names[core_type],
			(midr & 0xf00000) >> 20,
			midr & 0xf,
			revidr);

	switch (core_type) {
	case A57_CORE:
		regs = a57_regs;
		break;
	case A53_CORE:
		regs = a53_regs;
		break;
	case MNGS_CORE:
		regs = mngs_regs;
		break;
	}

	while (regs->reg) {
		if (regs->val == 1) {
			regs->val = get_guide_value_from_dt(core, regs->reg->name);
		}
		val = regs->reg->read_reg();
		if (regs->val != 0 && val != regs->val) {
			pr_info("%10s: 0x%016llX (0x%016llX)\n",
					regs->reg->name, val, regs->val);
			diff_cnt++;
		} else {
			pr_info("%10s: 0x%016llX\n", regs->reg->name, val);
		}
		regs++;
	}

	if (diff_cnt == 0)
		pr_info("all values ok\n");
	else
		pr_info("need to be checked: %d\n", diff_cnt);

	return 0;
}

static int thread_func(void *data)
{
	coretest_init();
	return 0;
}

#define NODE_STORE(name) \
	static ssize_t name##_store(struct kobject *kobj, struct kobj_attribute *attr, \
		const char *buf, size_t count)
#define NODE_SHOW(name) \
	static ssize_t name##_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
#define NODE_ATTR(name) \
	static struct kobj_attribute name##_attribute = __ATTR(name, S_IWUSR|S_IRUGO, name##_show, name##_store);

NODE_STORE(core)
{
	sscanf(buf, "%d", &core);

	task = kthread_create(thread_func, NULL, "thread%u", 0);
	kthread_bind(task, core);
	wake_up_process(task);

	return count;
}

NODE_SHOW(core)
{
	return sprintf(buf, "core = %d\n", core);
}

NODE_ATTR(core)

static struct attribute *attrs[] = {
	&core_attribute.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = attrs,
};

static struct kobject *core_kobj;

static int core_reg_probe(struct platform_device *pdev)
{
	dev = &pdev->dev;
	return 0;
}

static int core_reg_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id core_reg_dt_match[] = {
	{ .compatible = "samsung,exynos-core", },
	{},
};

static struct platform_driver core_reg_driver = {
	.probe	= core_reg_probe,
	.remove	= core_reg_remove,
	.driver	= {
		.name = "exynos-core",
		.owner	= THIS_MODULE,
		.of_match_table = core_reg_dt_match,
	}
};

static int __init core_init(void)
{
	int ret = 0;

	core_kobj = kobject_create_and_add("core_reg", kernel_kobj);
	if (!core_kobj)
		return -ENOMEM;

	ret = sysfs_create_group(core_kobj, &attr_group);
	if (ret)
		kobject_put(core_kobj);

	ret = platform_driver_register(&core_reg_driver);
	if (!ret)
		pr_info("%s: init\n", core_reg_driver.driver.name);

	return ret;
}

static void __exit core_exit(void)
{
	kobject_put(core_kobj);
	platform_driver_unregister(&core_reg_driver);
}

module_init(core_init);
module_exit(core_exit);

MODULE_AUTHOR("Jungwook Kim");
MODULE_DESCRIPTION("showing system-control registers of ARM/MNGS core");
MODULE_LICENSE("GPL");
