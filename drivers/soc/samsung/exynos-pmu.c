/*
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * EXYNOS - CPU PMU(Power Management Unit) support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/smp.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/platform_device.h>

#include <asm/smp_plat.h>

#include <soc/samsung/exynos-pmu.h>

/**
 * register offset value from base address
 */
#define PMU_CP_STAT				0x0038

#define PMU_CPU_CONFIG_BASE			0x2000
#define PMU_CPU_STATUS_BASE			0x2004
#define PMU_CPU_ADDR_OFFSET			0x80
#define CPU_LOCAL_PWR_CFG			0xF

#define PMU_NONCPU_STATUS_BASE			0x2404
#define PMU_CPUSEQ_OPTION_BASE			0x2488
#define PMU_L2_STATUS_BASE			0x2604
#define PMU_CLUSTER_ADDR_OFFSET			0x20
#define NONCPU_LOCAL_PWR_CFG			0xF
#define L2_LOCAL_PWR_CFG			0x7

/**
 * "pmureg" has the mapped base address of PMU(Power Management Unit)
 */
static struct regmap *pmureg;

/**
 * No driver refers the "pmureg" directly, through the only exported API.
 */
int exynos_pmu_read(unsigned int offset, unsigned int *val)
{
	return regmap_read(pmureg, offset, val);
}

int exynos_pmu_write(unsigned int offset, unsigned int val)
{
	return regmap_write(pmureg, offset, val);
}

int exynos_pmu_update(unsigned int offset, unsigned int mask, unsigned int val)
{
	return regmap_update_bits(pmureg, offset, mask, val);
}

EXPORT_SYMBOL(exynos_pmu_read);
EXPORT_SYMBOL(exynos_pmu_write);
EXPORT_SYMBOL(exynos_pmu_update);

/**
 * CPU power control registers in PMU are arranged at regular intervals
 * (interval = 0x80). pmu_cpu_offset calculates how far cpu is from address
 * of first cpu. This expression is based on cpu and cluster id in MPIDR,
 * refer below.

 * cpu address offset : ((cluster id << 2) | (cpu id)) * 0x80
 */
#define pmu_cpu_offset(mpidr)			\
	(( MPIDR_AFFINITY_LEVEL(mpidr, 1) << 2	\
	 | MPIDR_AFFINITY_LEVEL(mpidr, 0))	\
	 * PMU_CPU_ADDR_OFFSET)

static int exynos_l2_state(unsigned int cluster)
{
	unsigned int l2_stat = 0;
	unsigned int offset;

	offset = cluster * PMU_CLUSTER_ADDR_OFFSET;

	regmap_read(pmureg, PMU_L2_STATUS_BASE + offset, &l2_stat);

	return ((l2_stat & L2_LOCAL_PWR_CFG) == L2_LOCAL_PWR_CFG);
}

static int exynos_noncpu_state(unsigned int cluster)
{
	unsigned int noncpu_stat = 0;
	unsigned int offset;

	offset = cluster * PMU_CLUSTER_ADDR_OFFSET;

	regmap_read(pmureg, PMU_NONCPU_STATUS_BASE + offset, &noncpu_stat);

	return ((noncpu_stat & NONCPU_LOCAL_PWR_CFG) == NONCPU_LOCAL_PWR_CFG);
}

static void exynos_cpu_up(unsigned int cpu)
{
	unsigned int mpidr = cpu_logical_map(cpu);
	unsigned int offset;

	offset = pmu_cpu_offset(mpidr);
	regmap_update_bits(pmureg, PMU_CPU_CONFIG_BASE + offset,
			CPU_LOCAL_PWR_CFG, CPU_LOCAL_PWR_CFG);
}

static void exynos_cpu_down(unsigned int cpu)
{
	unsigned int mpidr = cpu_logical_map(cpu);
	unsigned int offset;

	offset = pmu_cpu_offset(mpidr);
	regmap_update_bits(pmureg, PMU_CPU_CONFIG_BASE + offset,
			CPU_LOCAL_PWR_CFG, 0);
}

static int exynos_cpu_state(unsigned int cpu)
{
	unsigned int mpidr = cpu_logical_map(cpu);
	unsigned int offset, val = 0;

	offset = pmu_cpu_offset(mpidr);
	regmap_read(pmureg, PMU_CPU_STATUS_BASE + offset, &val);

	return ((val & CPU_LOCAL_PWR_CFG) == CPU_LOCAL_PWR_CFG);
}

static int exynos_cluster_state(unsigned int cluster)
{
	unsigned int noncpu_stat, l2_stat = 0;

	noncpu_stat = exynos_noncpu_state(cluster);
	l2_stat = exynos_l2_state(cluster);

	return l2_stat && noncpu_stat;
}

/**
 * While Exynos with multi cluster supports to shutdown down both cluster,
 * there is no benefit in boot cluster. So Exynos-PMU driver supports
 * only non-boot cluster down.
 */
void exynos_cpu_sequencer_ctrl(unsigned int cluster, int enable)
{
	unsigned int offset;

	offset = cluster * PMU_CLUSTER_ADDR_OFFSET;
	regmap_update_bits(pmureg, PMU_CPUSEQ_OPTION_BASE + offset, 1, enable);
}

static void exynos_cluster_up(unsigned int cluster)
{
	exynos_cpu_sequencer_ctrl(cluster, false);
}

static void exynos_cluster_down(unsigned int cluster)
{
	exynos_cpu_sequencer_ctrl(cluster, true);
}

struct exynos_cpu_power_ops exynos_cpu = {
	.power_up = exynos_cpu_up,
	.power_down = exynos_cpu_down,
	.power_state = exynos_cpu_state,
	.cluster_up = exynos_cluster_up,
	.cluster_down = exynos_cluster_down,
	.cluster_state = exynos_cluster_state,
	.l2_state = exynos_l2_state,
	.noncpu_state = exynos_noncpu_state,
};


int exynos_check_cp_status(void)
{
	unsigned int val;

	exynos_pmu_read(PMU_CP_STAT, &val);

	return val;
}

static int exynos_pmu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	pmureg = syscon_regmap_lookup_by_phandle(dev->of_node,
						"samsung,syscon-phandle");
	if (IS_ERR(pmureg)) {
		pr_err("Fail to get regmap of PMU\n");
		return PTR_ERR(pmureg);
	}

	return 0;
}

static const struct of_device_id of_exynos_pmu_match[] = {
	{ .compatible = "samsung,exynos-pmu", },
	{ },
};

static const struct platform_device_id exynos_pmu_ids[] = {
	{ "exynos-pmu", },
	{ }
};

static struct platform_driver exynos_pmu_driver = {
	.driver = {
		.name = "exynos-pmu",
		.owner = THIS_MODULE,
		.of_match_table = of_exynos_pmu_match,
	},
	.probe		= exynos_pmu_probe,
	.id_table	= exynos_pmu_ids,
};

int __init exynos_pmu_init(void)
{
	return platform_driver_register(&exynos_pmu_driver);
}
subsys_initcall(exynos_pmu_init);
