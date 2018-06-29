/*
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/spinlock.h>
#include <linux/suspend.h>
#include <linux/wakeup_reason.h>
#include <linux/gpio.h>
#include <linux/syscore_ops.h>
#ifdef CONFIG_SEC_DEBUG
#include <linux/sec_debug.h>
#endif
#include <asm/psci.h>
#include <asm/suspend.h>
#include <asm/smp_plat.h>

#include <soc/samsung/exynos-pm.h>
#include <soc/samsung/exynos-pmu.h>
#include <soc/samsung/exynos-powermode.h>

#include <sound/exynos-audmixer.h>

#if defined(CONFIG_SOC_EXYNOS8890)
#define GPIO_ALIVE_PA_ADDR             0x10580000
#elif defined(CONFIG_SOC_EXYNOS7870)
#define GPIO_ALIVE_PA_ADDR             0x139F0000
#endif
#define WAKEUP_STAT_EINT                (1 << 0)
#define WAKEUP_STAT_RTC_ALARM           (1 << 1)
#define WAKEUP_STAT_RTC_TICK		(1 << 2)
#define WAKEUP_STAT_TRTC_ALARM		(1 << 3)
#define WAKEUP_STAT_TRTC_TICK		(1 << 4)
#define WAKEUP_STAT_MMC0		(1 << 9)
#define WAKEUP_STAT_MMC1		(1 << 10)
#define WAKEUP_STAT_MMC2		(1 << 11)
#define WAKEUP_STAT_I2S			(1 << 13)
#define WAKEUP_STAT_TIMER		(1 << 14)
#define WAKEUP_STAT_CP_RESET_REQ	(1 << 20)
#define WAKEUP_STAT_GNSS_WAKEUP_REQ	(1 << 21)
#define WAKEUP_STAT_GNSS_RESET_REQ	(1 << 22)
#define WAKEUP_STAT_GNSS_ACTIVE		(1 << 23)
#define WAKEUP_STAT_INT_MBOX_CP		(1 << 24)
#define WAKEUP_STAT_CP_ACTIVE		(1 << 25)
#define WAKEUP_STAT_INT_MBOX_GNSS	(1 << 26)
#define WAKEUP_STAT_SFR		(1 << 28)
/*
 * PMU register offset
 */
#define EXYNOS_PMU_WAKEUP_STAT		0x0600
#define EXYNOS_PMU_EINT_WAKEUP_MASK	0x060C
#define BOOT_CPU			0
#define NR_CPUS_PER_CLUSTER		4

static void __iomem *exynos_eint_base;
extern u32 exynos_eint_to_pin_num(int eint);
#define EXYNOS_EINT_PEND(b, x)      ((b) + 0xA00 + (((x) >> 3) * 4))
#ifdef CONFIG_SOC_EXYNOS7870
#define EXYNOS_MAX_EINT_IRQS	24
#else
#define EXYNOS_MAX_EINT_IRQS	32
#endif

static void exynos_show_wakeup_reason_eint(void)
{
	int bit;
	int i, size;
	long unsigned int ext_int_pend;
	u64 eint_wakeup_mask;
	bool found = 0;
	unsigned int val;

#ifdef CONFIG_SOC_EXYNOS7870
	/* gpa0, gpa1, gpa2 */
	pr_info("EINT_PEND: 0x%02x, 0x%02x 0x%02x\n",
			__raw_readl(EXYNOS_EINT_PEND(exynos_eint_base, 0)),
			__raw_readl(EXYNOS_EINT_PEND(exynos_eint_base, 8)),
			__raw_readl(EXYNOS_EINT_PEND(exynos_eint_base, 16)));
#else
	/* gpa0, gpa1, gpa2, gpa3 */
	pr_info("EINT_PEND: 0x%02x, 0x%02x 0x%02x, 0x%02x\n",
			__raw_readl(EXYNOS_EINT_PEND(exynos_eint_base, 0)),
			__raw_readl(EXYNOS_EINT_PEND(exynos_eint_base, 8)),
			__raw_readl(EXYNOS_EINT_PEND(exynos_eint_base, 16)),
			__raw_readl(EXYNOS_EINT_PEND(exynos_eint_base, 24)));
#endif

	exynos_pmu_read(EXYNOS_PMU_EINT_WAKEUP_MASK, &val);
	eint_wakeup_mask = val;

	for (i = 0, size = 8; i < EXYNOS_MAX_EINT_IRQS; i += size) {
		ext_int_pend =
			__raw_readl(EXYNOS_EINT_PEND(exynos_eint_base, i));

		for_each_set_bit(bit, &ext_int_pend, size) {
			u32 gpio;
			int irq;

			if (eint_wakeup_mask & (1 << (i + bit)))
				continue;

			gpio = exynos_eint_to_pin_num(i + bit);
			irq = gpio_to_irq(gpio);

#ifdef CONFIG_SUSPEND
			log_wakeup_reason(irq);
			update_wakeup_reason_stats(irq, i + bit);
#endif
			found = 1;
		}
	}

	if (!found)
		pr_info("Resume caused by unknown EINT\n");
}

static void exynos_show_wakeup_reason(bool sleep_abort)
{
	unsigned int wakeup_stat;

	if (sleep_abort)
		pr_info("PM: early wakeup!\n");

	exynos_pmu_read(EXYNOS_PMU_WAKEUP_STAT, &wakeup_stat);

	pr_info("WAKEUP_STAT: 0x%08x\n", wakeup_stat);

	if (wakeup_stat & WAKEUP_STAT_EINT)
		exynos_show_wakeup_reason_eint();
	else if (wakeup_stat & WAKEUP_STAT_RTC_ALARM)
		pr_info("Resume caused by RTC alarm\n");
	else if (wakeup_stat & WAKEUP_STAT_RTC_TICK)
		pr_info("Resume caused by RTC tick\n");
	else if (wakeup_stat & WAKEUP_STAT_TRTC_ALARM)
		pr_info("Resume caused by TRTC alarm\n");
	else if (wakeup_stat & WAKEUP_STAT_TRTC_TICK)
		pr_info("Resume caused by TRTC tick\n");
	else if (wakeup_stat & WAKEUP_STAT_MMC0)
		pr_info("Resume caused by MMC0\n");
	else if (wakeup_stat & WAKEUP_STAT_MMC1)
		pr_info("Resume caused by MMC1\n");
	else if (wakeup_stat & WAKEUP_STAT_MMC2)
		pr_info("Resume caused by MMC2\n");
	else if (wakeup_stat & WAKEUP_STAT_I2S)
		pr_info("Resume caused by I2S\n");
	else if (wakeup_stat & WAKEUP_STAT_TIMER)
		pr_info("Resume caused by TIMER\n");
	else if (wakeup_stat & WAKEUP_STAT_CP_RESET_REQ)
		pr_info("Resume caused by CP_RESET_REQ\n");
	else if (wakeup_stat & WAKEUP_STAT_GNSS_WAKEUP_REQ)
		pr_info("Resume caused by GNSS_WAKEUP_REQ\n");
	else if (wakeup_stat & WAKEUP_STAT_GNSS_RESET_REQ)
		pr_info("Resume caused by GNSS_RESET_REQ\n");
	else if (wakeup_stat & WAKEUP_STAT_GNSS_ACTIVE)
		pr_info("Resume caused by GNSS active\n");
	else if (wakeup_stat & WAKEUP_STAT_INT_MBOX_CP)
		pr_info("Resume caused by INT_MBOX_CP\n");
	else if (wakeup_stat & WAKEUP_STAT_CP_ACTIVE)
		pr_info("Resume caused by CP active\n");
	else if (wakeup_stat & WAKEUP_STAT_INT_MBOX_GNSS)
		pr_info("Resume caused by INT_MBOX_GNSS\n");
	else if (wakeup_stat & WAKEUP_STAT_SFR)
		pr_info("Resume caused by SFR\n");
	else
		pr_info("Resume caused by wakeup_stat 0x%08x\n",
			wakeup_stat);
}

#ifdef CONFIG_CPU_IDLE
static DEFINE_RWLOCK(exynos_pm_notifier_lock);
static RAW_NOTIFIER_HEAD(exynos_pm_notifier_chain);

static int exynos_pm_notify(enum exynos_pm_event event, int nr_to_call, int *nr_calls)
{
	int ret;

	ret = __raw_notifier_call_chain(&exynos_pm_notifier_chain, event, NULL,
		nr_to_call, nr_calls);

	return notifier_to_errno(ret);
}

int exynos_pm_register_notifier(struct notifier_block *nb)
{
	unsigned long flags;
	int ret;

	write_lock_irqsave(&exynos_pm_notifier_lock, flags);
	ret = raw_notifier_chain_register(&exynos_pm_notifier_chain, nb);
	write_unlock_irqrestore(&exynos_pm_notifier_lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(exynos_pm_register_notifier);

int exynos_pm_unregister_notifier(struct notifier_block *nb)
{
	unsigned long flags;
	int ret;

	write_lock_irqsave(&exynos_pm_notifier_lock, flags);
	ret = raw_notifier_chain_unregister(&exynos_pm_notifier_chain, nb);
	write_unlock_irqrestore(&exynos_pm_notifier_lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(exynos_pm_unregister_notifier);

int exynos_pm_lpa_enter(void)
{
	int nr_calls;
	int ret = 0;

	read_lock(&exynos_pm_notifier_lock);
	ret = exynos_pm_notify(LPA_ENTER, -1, &nr_calls);
	if (ret)
		/*
		 * Inform listeners (nr_calls - 1) about failure of LPA
		 * entry who are notified earlier to prepare for it.
		 */
		exynos_pm_notify(LPA_ENTER_FAIL, nr_calls - 1, NULL);
	read_unlock(&exynos_pm_notifier_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(exynos_pm_lpa_enter);

int exynos_pm_lpa_exit(void)
{
	int ret;

	read_lock(&exynos_pm_notifier_lock);
	ret = exynos_pm_notify(LPA_EXIT, -1, NULL);
	read_unlock(&exynos_pm_notifier_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(exynos_pm_lpa_exit);

int exynos_pm_sicd_enter(void)
{
	int nr_calls;
	int ret = 0;

	read_lock(&exynos_pm_notifier_lock);
	ret = exynos_pm_notify(SICD_ENTER, -1, &nr_calls);
	read_unlock(&exynos_pm_notifier_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(exynos_pm_sicd_enter);

int exynos_pm_sicd_exit(void)
{
	int ret;

	read_lock(&exynos_pm_notifier_lock);
	ret = exynos_pm_notify(SICD_EXIT, -1, NULL);
	read_unlock(&exynos_pm_notifier_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(exynos_pm_sicd_exit);
#endif /* CONFIG_CPU_IDLE */

int early_wakeup;
int psci_index;
int is_cp_call;

static int exynos_pm_syscore_suspend(void)
{
#ifdef CONFIG_SEC_DEBUG
	unsigned int val;
	unsigned int addr;
	int debug_level;
#endif

	if (!exynos_check_cp_status()) {
		pr_info("%s: sleep canceled by CP reset \n",__func__);
		return -EINVAL;
	}
	
#ifdef CONFIG_SEC_DEBUG
	debug_level = sec_debug_get_debug_level();

	if (debug_level >= 1) {
		/* debug level MID or HIGH */
		addr = 0x0038;
		exynos_pmu_read(addr, &val);
		pr_info("CP_STAT (0x%x) = 0x%x\n", addr, val);
		addr = 0x0048;
		exynos_pmu_read(addr, &val);
		pr_info("GNSS_STAT (0x%x) = 0x%x\n", addr, val);
		addr = 0x00c0;
		exynos_pmu_read(addr, &val);
		pr_info("CONNECT_SLEEP_STATUS (0x%x) = 0x%x\n", addr, val);
	}
#endif

	is_cp_call = is_cp_aud_enabled();
	if (is_cp_call) {
		psci_index = PSCI_SYSTEM_CP_CALL;
		exynos_prepare_cp_call();
		pr_info("%s: Enter CP Call mode for voice call\n",__func__);
	} else {
		psci_index = PSCI_SYSTEM_SLEEP;
		exynos_prepare_sys_powerdown(SYS_SLEEP);
		pr_info("%s: Enter sleep mode\n",__func__);
	}

	return 0;
}

static void exynos_pm_syscore_resume(void)
{
	pr_info("========== wake up ==========\n");
	if (is_cp_call)
		exynos_wakeup_cp_call(early_wakeup);
	else
		exynos_wakeup_sys_powerdown(SYS_SLEEP,
					(bool)early_wakeup);

	exynos_show_wakeup_reason((bool)early_wakeup);

	pr_debug("%s: post sleep, preparing to return\n", __func__);
}

static struct syscore_ops exynos_pm_syscore_ops = {
	.suspend	= exynos_pm_syscore_suspend,
	.resume		= exynos_pm_syscore_resume,
};

#ifdef CONFIG_SEC_GPIO_DVS
extern void gpio_dvs_check_sleepgpio(void);
#endif

static int exynos_pm_enter(suspend_state_t state)
{
#ifdef CONFIG_SEC_GPIO_DVS
	/************************ Caution !!! ****************************/
	/* This function must be located in appropriate SLEEP position
	 * in accordance with the specification of each BB vendor.
	 */
	/************************ Caution !!! ****************************/
	gpio_dvs_check_sleepgpio();
#endif
	/* This will also act as our return point when
	 * we resume as it saves its own register state and restores it
	 * during the resume. */
	early_wakeup = cpu_suspend(psci_index);
	if (early_wakeup)
		pr_info("%s: return to originator\n", __func__);

	return early_wakeup;
}

static const struct platform_suspend_ops exynos_pm_ops = {
	.enter		= exynos_pm_enter,
	.valid		= suspend_valid_only_mem,
};

static struct bus_type exynos_info_subsys = {
	.name = "exynos_info",
	.dev_name = "exynos_info",
};

static ssize_t core_status_show(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf)
{
	ssize_t n = 0;
	int cpu, cluster = 0;
	unsigned int mpidr;

	for_each_possible_cpu(cpu) {
		/*
		 * Each cluster has four cores.
		 * "cpu % NR_CPUS_PER_CLUSTER == 0" means that
		 * the cpu is a first one of each cluster.
		 */
		if (!(cpu % NR_CPUS_PER_CLUSTER)) {
			mpidr = cpu_logical_map(cpu);
			cluster =  MPIDR_AFFINITY_LEVEL(mpidr, 1);
			n += scnprintf(buf + n, 24, "%s L2 : %d\n",
				(!cpu) ? "boot" : "nonboot",
				exynos_cpu.l2_state(cluster));

			n += scnprintf(buf + n, 24, "%s Noncpu : %d\n",
				(!cpu) ? "boot" : "nonboot",
				exynos_cpu.noncpu_state(cluster));
		}
		n += scnprintf(buf + n, 24, "CPU%d : %d\n",
				cpu, exynos_cpu.power_state(cpu));
	}

	return n;
}

static struct kobj_attribute exynos_info_attr =
	__ATTR(core_status, 0644, core_status_show, NULL);

static struct attribute *exynos_info_sysfs_attrs[] = {
	&exynos_info_attr.attr,
	NULL,
};

static struct attribute_group exynos_info_sysfs_group = {
	.attrs = exynos_info_sysfs_attrs,
};

static const struct attribute_group *exynos_info_sysfs_groups[] = {
	&exynos_info_sysfs_group,
	NULL,
};

#if defined(CONFIG_SEC_DEBUG)
enum ids_info
{
	table_ver,
	cpu_asv,
	g3d_asv
};

extern int asv_ids_information(enum ids_info id);

static ssize_t show_asv_info(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	int count = 0;

	/* Set asv group info to buf */
	count += sprintf(&buf[count], "%d ", asv_ids_information(table_ver));
	count += sprintf(&buf[count], "%03x ", asv_ids_information(cpu_asv));
	count += sprintf(&buf[count], "%03x ", asv_ids_information(g3d_asv));
	count += sprintf(&buf[count], "\n");

	return count;
}

static DEVICE_ATTR(asv_info, 0664, show_asv_info, NULL);

#endif /* CONFIG_SEC_FACTORY */

static __init int exynos_pm_drvinit(void)
{
#if defined(CONFIG_SEC_DEBUG)
	int ret;
#endif
	if (subsys_system_register(&exynos_info_subsys,
					exynos_info_sysfs_groups))
		pr_err("fail to register exynos_info subsys\n");

	suspend_set_ops(&exynos_pm_ops);
	register_syscore_ops(&exynos_pm_syscore_ops);

	exynos_eint_base = ioremap(GPIO_ALIVE_PA_ADDR, SZ_8K);

	if (exynos_eint_base == NULL) {
		pr_err("%s: unable to ioremap for EINT base address\n",
				__func__);
		BUG();
	}

#if defined(CONFIG_SEC_DEBUG)
	/* create sysfs group */
	ret = sysfs_create_file(power_kobj, &dev_attr_asv_info.attr);
	if (ret) {
		pr_err("%s: failed to create exynos8890 asv attribute file\n",
				__func__);
	}
#endif

	return 0;
}
arch_initcall(exynos_pm_drvinit);
