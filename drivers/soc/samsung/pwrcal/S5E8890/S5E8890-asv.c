#include "../pwrcal-env.h"
#include "../pwrcal-rae.h"
#include "../pwrcal-asv.h"
#include "S5E8890-cmusfr.h"
#include "S5E8890-cmu.h"
#include "S5E8890-vclk.h"
#include "S5E8890-vclk-internal.h"

#ifdef PWRCAL_TARGET_LINUX
#include <linux/io.h>
#include <asm/map.h>
#include <soc/samsung/ect_parser.h>
#endif
#ifdef PWRCAL_TARGET_FW
#include <mach/ect_parser.h>
#define S5P_VA_APM_SRAM			((void *)0x11200000)
#endif

#define EXYNOS_MAILBOX_RCC_MIN_MAX(x)	(S5P_VA_APM_SRAM + (0x3700) + (x * 0x4))
#define EXYNOS_MAILBOX_ATLAS_RCC(x)	(S5P_VA_APM_SRAM + (0x3730) + (x * 0x4))
#define EXYNOS_MAILBOX_APOLLO_RCC(x)	(S5P_VA_APM_SRAM + (0x3798) + (x * 0x4))
#define EXYNOS_MAILBOX_G3D_RCC(x)	(S5P_VA_APM_SRAM + (0x37EC) + (x * 0x4))
#define EXYNOS_MAILBOX_MIF_RCC(x)	(S5P_VA_APM_SRAM + (0x381C) + (x * 0x4))


enum dvfs_id {
	cal_asv_dvfs_big,
	cal_asv_dvfs_little,
	cal_asv_dvfs_g3d,
	cal_asv_dvfs_mif,
	cal_asv_dvfs_int,
	cal_asv_dvfs_cam,
	cal_asv_dvfs_disp,
	num_of_dvfs,
};

#define MAX_ASV_GROUP	16
#define NUM_OF_ASVTABLE	1
#define PWRCAL_ASV_LIST(table)	{table, sizeof(table) / sizeof(table[0])}

struct asv_table_entry {
	unsigned int index;
	unsigned int *voltage;
};

struct asv_table_list {
	struct asv_table_entry *table;
	unsigned int table_size;
	unsigned int voltage_count;
};

#define FORCE_ASV_MAGIC		0x57E90000
static unsigned int force_asv_group[num_of_dvfs];

#define APM_OTP_ADDRESS		0x101E9018
#define APM_TABLE_BASE		0x11203700
#define APM_FUSE_TABLE_BASE	APM_TABLE_BASE
#define APM_RCC_TABLE_BASE	(APM_TABLE_BASE + 0x0030)
static void *apm_sram_base;

struct asv_tbl_info {
	unsigned mngs_asv_group:4;
	int mngs_modified_group:4;
	unsigned mngs_ssa10:2;
	unsigned mngs_ssa11:2;
	unsigned mngs_ssa0:4;
	unsigned apollo_asv_group:4;
	int apollo_modified_group:4;
	unsigned apollo_ssa10:2;
	unsigned apollo_ssa11:2;
	unsigned apollo_ssa0:4;

	unsigned g3d_asv_group:4;
	int g3d_modified_group:4;
	unsigned g3d_ssa10:2;
	unsigned g3d_ssa11:2;
	unsigned g3d_ssa0:4;
	unsigned mif_asv_group:4;
	int mif_modified_group:4;
	unsigned mif_ssa10:2;
	unsigned mif_ssa11:2;
	unsigned mif_ssa0:4;

	unsigned int_asv_group:4;
	int int_modified_group:4;
	unsigned int_ssa10:2;
	unsigned int_ssa11:2;
	unsigned int_ssa0:4;
	unsigned disp_asv_group:4;
	int disp_modified_group:4;
	unsigned disp_ssa10:2;
	unsigned disp_ssa11:2;
	unsigned disp_ssa0:4;

	unsigned asv_table_ver:7;
	unsigned fused_grp:1;
	unsigned reserved_0:12;
	unsigned g3d_mcs0:4;
	unsigned g3d_mcs1:4;
};
#define ASV_INFO_ADDR_BASE	(0x101E9000)
#define ASV_INFO_ADDR_CNT	(sizeof(struct asv_tbl_info) / 4)

static struct asv_tbl_info asv_tbl_info;

static struct asv_table_list *pwrcal_big_asv_table;
static struct asv_table_list *pwrcal_little_asv_table;
static struct asv_table_list *pwrcal_g3d_asv_table;
static struct asv_table_list *pwrcal_mif_asv_table;
static struct asv_table_list *pwrcal_int_asv_table;
static struct asv_table_list *pwrcal_cam_asv_table;
static struct asv_table_list *pwrcal_disp_asv_table;

static struct asv_table_list *pwrcal_big_rcc_table;
static struct asv_table_list *pwrcal_little_rcc_table;
static struct asv_table_list *pwrcal_g3d_rcc_table;
static struct asv_table_list *pwrcal_mif_rcc_table;

static struct pwrcal_vclk_dfs *asv_dvfs_big;
static struct pwrcal_vclk_dfs *asv_dvfs_little;
static struct pwrcal_vclk_dfs *asv_dvfs_g3d;
static struct pwrcal_vclk_dfs *asv_dvfs_mif;
static struct pwrcal_vclk_dfs *asv_dvfs_int;
static struct pwrcal_vclk_dfs *asv_dvfs_cam;
static struct pwrcal_vclk_dfs *asv_dvfs_disp;


static void asv_set_grp(unsigned int id, unsigned int asvgrp)
{
	force_asv_group[id & 0x0000FFFF] = FORCE_ASV_MAGIC | asvgrp;
}

static void asv_set_tablever(unsigned int version)
{
	asv_tbl_info.asv_table_ver = version;

	return;
}

static int get_max_freq_lv(struct ect_voltage_domain *domain, unsigned int version)
{
	int i;
	int ret = -1;

	for (i = 0; i < domain->num_of_level; i++) {
		if (domain->table_list[version].level_en[i]) {
			ret = i;
			break;
		}
	}

	return ret;
}
static int get_min_freq_lv(struct ect_voltage_domain *domain, unsigned int version)
{
	int i;
	int ret = -1;

	for (i = domain->num_of_level - 1; i >= 0; i--) {
		if (domain->table_list[version].level_en[i]) {
			ret = i;
			break;
		}
	}

	return ret;
}
static void asv_set_freq_limit(void)
{
	void *asv_block;
	struct ect_voltage_domain *domain;
	int lv;

	asv_block = ect_get_block("ASV");
	if (asv_block == NULL)
		BUG();

	if (!asv_tbl_info.fused_grp)
		goto notfused;

	domain = ect_asv_get_domain(asv_block, "dvfs_big");
	if (!domain)
		BUG();
	lv = get_max_freq_lv(domain, asv_tbl_info.asv_table_ver);
	if (lv >= 0)
		asv_dvfs_big->table->max_freq = domain->level_list[lv] * 1000;
	lv = get_min_freq_lv(domain, asv_tbl_info.asv_table_ver);
	if (lv >= 0)
		asv_dvfs_big->table->min_freq = domain->level_list[lv] * 1000;

	domain = ect_asv_get_domain(asv_block, "dvfs_little");
	if (!domain)
		BUG();
	lv = get_max_freq_lv(domain, asv_tbl_info.asv_table_ver);
	if (lv >= 0)
		asv_dvfs_little->table->max_freq = domain->level_list[lv] * 1000;
	lv = get_min_freq_lv(domain, asv_tbl_info.asv_table_ver);
	if (lv >= 0)
		asv_dvfs_little->table->min_freq = domain->level_list[lv] * 1000;

	domain = ect_asv_get_domain(asv_block, "dvfs_g3d");
	if (!domain)
		BUG();
	lv = get_max_freq_lv(domain, asv_tbl_info.asv_table_ver);
	if (lv >= 0)
		asv_dvfs_g3d->table->max_freq = domain->level_list[lv] * 1000;
	lv = get_min_freq_lv(domain, asv_tbl_info.asv_table_ver);
	if (lv >= 0)
		asv_dvfs_g3d->table->min_freq = domain->level_list[lv] * 1000;

	domain = ect_asv_get_domain(asv_block, "dvfs_mif");
	if (!domain)
		BUG();
	lv = get_max_freq_lv(domain, asv_tbl_info.asv_table_ver);
	if (lv >= 0)
		asv_dvfs_mif->table->max_freq = domain->level_list[lv] * 1000;
	lv = get_min_freq_lv(domain, asv_tbl_info.asv_table_ver);
	if (lv >= 0)
		asv_dvfs_mif->table->min_freq = domain->level_list[lv] * 1000;

	domain = ect_asv_get_domain(asv_block, "dvfs_int");
	if (!domain)
		BUG();
	lv = get_max_freq_lv(domain, asv_tbl_info.asv_table_ver);
	if (lv >= 0)
		asv_dvfs_int->table->max_freq = domain->level_list[lv] * 1000;
	lv = get_min_freq_lv(domain, asv_tbl_info.asv_table_ver);
	if (lv >= 0)
		asv_dvfs_int->table->min_freq = domain->level_list[lv] * 1000;

	domain = ect_asv_get_domain(asv_block, "dvfs_disp");
	if (!domain)
		BUG();
	lv = get_max_freq_lv(domain, asv_tbl_info.asv_table_ver);
	if (lv >= 0)
		asv_dvfs_disp->table->max_freq = domain->level_list[lv] * 1000;
	lv = get_min_freq_lv(domain, asv_tbl_info.asv_table_ver);
	if (lv >= 0)
		asv_dvfs_disp->table->min_freq = domain->level_list[lv] * 1000;

	domain = ect_asv_get_domain(asv_block, "dvfs_cam");
	if (!domain)
		BUG();
	lv = get_max_freq_lv(domain, asv_tbl_info.asv_table_ver);
	if (lv >= 0)
		asv_dvfs_cam->table->max_freq = domain->level_list[lv] * 1000;
	lv = get_min_freq_lv(domain, asv_tbl_info.asv_table_ver);
	if (lv >= 0)
		asv_dvfs_cam->table->min_freq = domain->level_list[lv] * 1000;


	return;


notfused:

#ifdef PWRCAL_TARGET_LINUX
	asv_dvfs_big->table->max_freq = 728000;
	asv_dvfs_little->table->max_freq = 1274000;
	asv_dvfs_g3d->table->max_freq = 260000;
	asv_dvfs_mif->table->max_freq = 1539000;
#endif
	return;
}

static void asv_get_asvinfo(void)
{
	int i;
	unsigned int *pasv_table;
	unsigned long tmp;

	pasv_table = (unsigned int *)&asv_tbl_info;
	for (i = 0; i < ASV_INFO_ADDR_CNT; i++) {
#ifdef PWRCAL_TARGET_LINUX
		exynos_smc_readsfr((unsigned long)(ASV_INFO_ADDR_BASE + 0x4 * i), &tmp);
#else
#if (CONFIG_STARTUP_EL_MODE == STARTUP_EL3)
		tmp = *((volatile unsigned int *)(unsigned long)(ASV_INFO_ADDR_BASE + 0x4 * i));
#else
		smc_readsfr((unsigned long)(ASV_INFO_ADDR_BASE + 0x4 * i), &tmp);
#endif
#endif
		*(pasv_table + i) = (unsigned int)tmp;
	}

	if (!asv_tbl_info.fused_grp) {
		asv_tbl_info.mngs_asv_group = 0;
		asv_tbl_info.mngs_modified_group = 0;
		asv_tbl_info.mngs_ssa10 = 0;
		asv_tbl_info.mngs_ssa11 = 0;
		asv_tbl_info.mngs_ssa0 = 0;
		asv_tbl_info.apollo_asv_group = 0;
		asv_tbl_info.apollo_modified_group = 0;
		asv_tbl_info.apollo_ssa10 = 0;
		asv_tbl_info.apollo_ssa11 = 0;
		asv_tbl_info.apollo_ssa0 = 0;

		asv_tbl_info.g3d_asv_group = 0;
		asv_tbl_info.g3d_modified_group = 0;
		asv_tbl_info.g3d_ssa10 = 0;
		asv_tbl_info.g3d_ssa11 = 0;
		asv_tbl_info.g3d_ssa0 = 0;
		asv_tbl_info.mif_asv_group = 0;
		asv_tbl_info.mif_modified_group = 0;
		asv_tbl_info.mif_ssa10 = 0;
		asv_tbl_info.mif_ssa11 = 0;
		asv_tbl_info.mif_ssa0 = 0;

		asv_tbl_info.int_asv_group = 0;
		asv_tbl_info.int_modified_group = 0;
		asv_tbl_info.int_ssa10 = 0;
		asv_tbl_info.int_ssa11 = 0;
		asv_tbl_info.int_ssa0 = 0;
		asv_tbl_info.disp_asv_group = 0;
		asv_tbl_info.disp_modified_group = 0;
		asv_tbl_info.disp_ssa10 = 0;
		asv_tbl_info.disp_ssa11 = 0;
		asv_tbl_info.disp_ssa0 = 0;

		asv_tbl_info.asv_table_ver = 0;
		asv_tbl_info.fused_grp = 0;
		asv_tbl_info.reserved_0 = 0;
		asv_tbl_info.g3d_mcs0 = 0;
		asv_tbl_info.g3d_mcs1 = 0;
	}

	if (asv_tbl_info.g3d_mcs0 == 0)
		asv_tbl_info.g3d_mcs0 = 0x5;
	if (asv_tbl_info.g3d_mcs1 == 0)
		asv_tbl_info.g3d_mcs1 = 0xE;

	asv_set_freq_limit();
}

static int get_asv_group(enum dvfs_id domain, unsigned int lv)
{
	int asv = 0;
	int mod = 0;

	switch (domain) {
	case cal_asv_dvfs_big:
		asv = asv_tbl_info.mngs_asv_group;
		mod = asv_tbl_info.mngs_modified_group;
		break;
	case cal_asv_dvfs_little:
		asv = asv_tbl_info.apollo_asv_group;
		mod = asv_tbl_info.apollo_modified_group;
		break;
	case cal_asv_dvfs_g3d:
		asv = asv_tbl_info.g3d_asv_group;
		mod = asv_tbl_info.g3d_modified_group;
		break;
	case cal_asv_dvfs_mif:
		asv = asv_tbl_info.mif_asv_group;
		mod = asv_tbl_info.mif_modified_group;
		break;
	case cal_asv_dvfs_int:
		asv = asv_tbl_info.int_asv_group;
		mod = asv_tbl_info.int_modified_group;
		break;
	case cal_asv_dvfs_cam:
		asv = asv_tbl_info.disp_asv_group;
		mod = asv_tbl_info.disp_modified_group;
		break;
	case cal_asv_dvfs_disp:
		asv = asv_tbl_info.disp_asv_group;
		mod = asv_tbl_info.disp_modified_group;
		break;
	default:
		BUG();	/* Never reach */
		break;
	}

	if ((force_asv_group[domain & 0x0000FFFF] & 0xFFFF0000) != FORCE_ASV_MAGIC) {
		if (mod)
			asv += mod;
	} else {
		asv = force_asv_group[domain & 0x0000FFFF] & 0x0000FFFF;
	}

	if (asv < 0 || asv >= MAX_ASV_GROUP)
		BUG();	/* Never reach */

	return asv;
}

static unsigned int get_asv_voltage(enum dvfs_id domain, unsigned int lv)
{
	int asv;
	unsigned int ssa10, ssa11;
	unsigned int ssa0;
	unsigned int subgrp_index;
	const unsigned int *table;
	unsigned int volt;

	switch (domain) {
	case cal_asv_dvfs_big:
		asv = get_asv_group(cal_asv_dvfs_big, lv);
		ssa10 = asv_tbl_info.mngs_ssa10;
		ssa11 = asv_tbl_info.mngs_ssa11;
		ssa0 = asv_tbl_info.mngs_ssa0;
		subgrp_index = 16;
		table = pwrcal_big_asv_table[asv_tbl_info.asv_table_ver].table[lv].voltage;
		break;
	case cal_asv_dvfs_little:
		asv = get_asv_group(cal_asv_dvfs_little, lv);
		ssa10 = asv_tbl_info.apollo_ssa10;
		ssa11 = asv_tbl_info.apollo_ssa11;
		ssa0 = asv_tbl_info.apollo_ssa0;
		subgrp_index = 10;
		table = pwrcal_little_asv_table[asv_tbl_info.asv_table_ver].table[lv].voltage;
		break;
	case cal_asv_dvfs_g3d:
		asv = get_asv_group(cal_asv_dvfs_g3d, lv);
		ssa10 = asv_tbl_info.g3d_ssa10;
		ssa11 = asv_tbl_info.g3d_ssa11;
		ssa0 = asv_tbl_info.g3d_ssa0;
		subgrp_index = 7;
		table = pwrcal_g3d_asv_table[asv_tbl_info.asv_table_ver].table[lv].voltage;
		break;
	case cal_asv_dvfs_mif:
		asv = get_asv_group(cal_asv_dvfs_mif, lv);
		ssa10 = asv_tbl_info.mif_ssa10;
		ssa11 = asv_tbl_info.mif_ssa11;
		ssa0 = asv_tbl_info.mif_ssa0;
		subgrp_index = 8;
		table = pwrcal_mif_asv_table[asv_tbl_info.asv_table_ver].table[lv].voltage;
		break;
	case cal_asv_dvfs_int:
		asv = get_asv_group(cal_asv_dvfs_int, lv);
		ssa10 = asv_tbl_info.int_ssa10;
		ssa11 = asv_tbl_info.int_ssa11;
		ssa0 = asv_tbl_info.int_ssa0;
		subgrp_index = 15;
		table = pwrcal_int_asv_table[asv_tbl_info.asv_table_ver].table[lv].voltage;
		break;
	case cal_asv_dvfs_cam:
		asv = get_asv_group(cal_asv_dvfs_cam, lv);
		ssa10 = asv_tbl_info.disp_ssa10;
		ssa11 = asv_tbl_info.disp_ssa11;
		ssa0 = asv_tbl_info.disp_ssa0;
		subgrp_index = 5;
		table = pwrcal_cam_asv_table[asv_tbl_info.asv_table_ver].table[lv].voltage;
		break;
	case cal_asv_dvfs_disp:
		asv = get_asv_group(cal_asv_dvfs_disp, lv);
		ssa10 = asv_tbl_info.disp_ssa10;
		ssa11 = asv_tbl_info.disp_ssa11;
		ssa0 = asv_tbl_info.disp_ssa0;
		subgrp_index = 2;
		table = pwrcal_disp_asv_table[asv_tbl_info.asv_table_ver].table[lv].voltage;
		break;
	default:
		BUG();	/* Never reach */
		break;
	}

	volt = table[asv];
	if (lv < subgrp_index) {
		volt += 12500 * ssa10;
		if (ssa10 == 3)
			volt += 12500;
	} else {
		volt += 12500 * ssa11;
		if (ssa11 == 3)
			volt += 12500;
	}

	if (volt < 575000 + ssa0 * 25000)
		volt = 575000 + ssa0 * 25000;

	return volt;
}

static int dvfsbig_get_asv_table(unsigned int *table)
{
	int lv, max_lv;

	max_lv = asv_dvfs_big->table->num_of_lv;

	for (lv = 0; lv < max_lv; lv++)
		table[lv] = get_asv_voltage(cal_asv_dvfs_big, lv);

	return max_lv;
}

static int dvfslittle_get_asv_table(unsigned int *table)
{
	int lv, max_lv;

	max_lv = asv_dvfs_little->table->num_of_lv;

	for (lv = 0; lv < max_lv; lv++)
		table[lv] = get_asv_voltage(cal_asv_dvfs_little, lv);

	return max_lv;
}

static int dvfsg3d_get_asv_table(unsigned int *table)
{
	int lv, max_lv;

	max_lv = asv_dvfs_g3d->table->num_of_lv;

	for (lv = 0; lv < max_lv; lv++)
		table[lv] = get_asv_voltage(cal_asv_dvfs_g3d, lv);

	return max_lv;
}

static int dvfsmif_get_asv_table(unsigned int *table)
{
	int lv, max_lv;

	max_lv = asv_dvfs_mif->table->num_of_lv;

	for (lv = 0; lv < max_lv; lv++)
		table[lv] = get_asv_voltage(cal_asv_dvfs_mif, lv);

	return max_lv;
}

static int dvfsint_get_asv_table(unsigned int *table)
{
	int lv, max_lv;

	max_lv = asv_dvfs_int->table->num_of_lv;

	for (lv = 0; lv < max_lv; lv++)
		table[lv] = get_asv_voltage(cal_asv_dvfs_int, lv);

	return max_lv;
}

static int dvfscam_get_asv_table(unsigned int *table)
{
	int lv, max_lv;

	max_lv = asv_dvfs_cam->table->num_of_lv;

	for (lv = 0; lv < max_lv; lv++)
		table[lv] = get_asv_voltage(cal_asv_dvfs_cam, lv);

	return max_lv;
}

static int dvfsdisp_get_asv_table(unsigned int *table)
{
	int lv, max_lv;

	max_lv = asv_dvfs_disp->table->num_of_lv;

	for (lv = 0; lv < max_lv; lv++)
		table[lv] = get_asv_voltage(cal_asv_dvfs_disp, lv);

	return max_lv;
}

static int dfsg3d_set_ema(unsigned int volt)
{
	if (volt > 750000)
		pwrcal_writel(EMA_RF2_UHD_CON, asv_tbl_info.g3d_mcs0);
	else
		pwrcal_writel(EMA_RF2_UHD_CON, asv_tbl_info.g3d_mcs1);

	return 0;
}

static int asv_rcc_set_table(void)
{
	int i;
	int lv, max_lv, asv;
	unsigned int *p;
	unsigned long tmp;

	if (!apm_sram_base) {
#ifdef PWRCAL_TARGET_LINUX
		apm_sram_base = (void *)ioremap(APM_TABLE_BASE, SZ_4K);
#else
		apm_sram_base = (void *)APM_TABLE_BASE;
#endif
	}

	p = (unsigned int *)(apm_sram_base);
	for (i = 0; i < 8; i++) {
#ifdef PWRCAL_TARGET_LINUX
		exynos_smc_readsfr((unsigned long)(APM_OTP_ADDRESS + 0x4 * i), &tmp);
#else
#if (CONFIG_STARTUP_EL_MODE == STARTUP_EL3)
		tmp = *((volatile unsigned int *)(unsigned long)(APM_OTP_ADDRESS + 0x4 * i));
#else
		smc_readsfr((unsigned long)(APM_OTP_ADDRESS + 0x4 * i), &tmp);
#endif
#endif
		*(p + i) = (unsigned int)tmp;
	}

	p = (unsigned int *)(apm_sram_base + 0x0030);
	max_lv = asv_dvfs_big->table->num_of_lv;
	if (max_lv > 25)
		max_lv = 25;
	for (lv = 0; lv < max_lv; lv++) {
		asv = get_asv_group(cal_asv_dvfs_big, lv);
		*(p + lv) = pwrcal_big_rcc_table[asv_tbl_info.asv_table_ver].table[lv].voltage[asv];
	}

	p = (unsigned int *)(apm_sram_base + 0x0098);
	max_lv = asv_dvfs_little->table->num_of_lv;
	if (max_lv > 20)
		max_lv = 20;
	for (lv = 0; lv < max_lv; lv++) {
		asv = get_asv_group(cal_asv_dvfs_little, lv);
		*(p + lv) = pwrcal_little_rcc_table[asv_tbl_info.asv_table_ver].table[lv].voltage[asv];
	}

	p = (unsigned int *)(apm_sram_base + 0x00EC);
	max_lv = asv_dvfs_g3d->table->num_of_lv;
	if (max_lv > 11)
		max_lv = 11;
	for (lv = 0; lv < max_lv; lv++) {
		asv = get_asv_group(cal_asv_dvfs_g3d, lv);
		*(p + lv) = pwrcal_g3d_rcc_table[asv_tbl_info.asv_table_ver].table[lv].voltage[asv];
	}

	p = (unsigned int *)(apm_sram_base + 0x011C);
	max_lv = asv_dvfs_mif->table->num_of_lv;
	if (max_lv > 16)
		max_lv = 16;
	for (lv = 0; lv < max_lv; lv++) {
		asv = get_asv_group(cal_asv_dvfs_mif, lv);
		*(p + lv) = pwrcal_mif_rcc_table[asv_tbl_info.asv_table_ver].table[lv].voltage[asv];
	}

	return 0;
}

static void asv_voltage_init_table(struct asv_table_list **asv_table, struct pwrcal_vclk_dfs *dfs)
{
	int i, j, k;
	void *asv_block, *margin_block;
	struct ect_voltage_domain *domain;
	struct ect_voltage_table *table;
	struct asv_table_entry *asv_entry;
	struct ect_margin_domain *margin_domain = NULL;

	asv_block = ect_get_block("ASV");
	if (asv_block == NULL)
		return;

	margin_block = ect_get_block("MARGIN");

	domain = ect_asv_get_domain(asv_block, dfs->vclk.name);
	if (domain == NULL)
		return;

	if (asv_tbl_info.asv_table_ver >= domain->num_of_table)
		BUG();

	if (margin_block)
		margin_domain = ect_margin_get_domain(margin_block, dfs->vclk.name);

	*asv_table = kzalloc(sizeof(struct asv_table_list) * domain->num_of_table, GFP_KERNEL);
	if (*asv_table == NULL)
		return;

	for (i = 0; i < domain->num_of_table; ++i) {
		table = &domain->table_list[i];

		(*asv_table)[i].table_size = domain->num_of_table;
		(*asv_table)[i].table = kzalloc(sizeof(struct asv_table_entry) * domain->num_of_level, GFP_KERNEL);
		if ((*asv_table)[i].table == NULL)
			return;

		for (j = 0; j < domain->num_of_level; ++j) {
			asv_entry = &(*asv_table)[i].table[j];

			asv_entry->index = domain->level_list[j];
			asv_entry->voltage = kzalloc(sizeof(unsigned int) * domain->num_of_group, GFP_KERNEL);

			for (k = 0; k < domain->num_of_group; ++k) {
				if (table->voltages != NULL)
					asv_entry->voltage[k] = table->voltages[j * domain->num_of_group + k];
				else if (table->voltages_step != NULL)
					asv_entry->voltage[k] = table->voltages_step[j * domain->num_of_group + k] * table->volt_step;

				if (margin_domain != NULL) {
					if (margin_domain->offset != NULL)
						asv_entry->voltage[k] += margin_domain->offset[j * margin_domain->num_of_group + k];
					else
						asv_entry->voltage[k] += margin_domain->offset_compact[j * margin_domain->num_of_group + k] * margin_domain->volt_step;
				}
			}
		}
	}
}

static void asv_rcc_init_table(struct asv_table_list **rcc_table, struct pwrcal_vclk_dfs *dfs)

{
	int i, j, k;
	void *rcc_block;
	struct ect_rcc_domain *domain;
	struct ect_rcc_table *table;
	struct asv_table_entry *rcc_entry;

	rcc_block = ect_get_block("RCC");
	if (rcc_block == NULL)
		return;

	domain = ect_rcc_get_domain(rcc_block, dfs->vclk.name);
	if (domain == NULL)
		return;

	*rcc_table = kzalloc(sizeof(struct asv_table_list) * domain->num_of_table, GFP_KERNEL);
	if (*rcc_table == NULL)
		return;

	for (i = 0; i < domain->num_of_table; ++i) {
		table = &domain->table_list[i];

		(*rcc_table)[i].table_size = domain->num_of_table;
		(*rcc_table)[i].table = kzalloc(sizeof(struct asv_table_entry) * domain->num_of_level, GFP_KERNEL);
		if ((*rcc_table)[i].table == NULL)
			return;

		for (j = 0; j < domain->num_of_level; ++j) {
			rcc_entry = &(*rcc_table)[i].table[j];

			rcc_entry->index = domain->level_list[j];
			rcc_entry->voltage = kzalloc(sizeof(unsigned int) * domain->num_of_group, GFP_KERNEL);

			for (k = 0; k < domain->num_of_group; ++k) {
				if (table->rcc != NULL)
					rcc_entry->voltage[k] = table->rcc[j * domain->num_of_group + k];
				else
					rcc_entry->voltage[k] = table->rcc_compact[j * domain->num_of_group + k];
			}
		}
	}
}

static void asv_voltage_table_init(void)
{
	asv_voltage_init_table(&pwrcal_big_asv_table, asv_dvfs_big);
	asv_voltage_init_table(&pwrcal_little_asv_table, asv_dvfs_little);
	asv_voltage_init_table(&pwrcal_g3d_asv_table, asv_dvfs_g3d);
	asv_voltage_init_table(&pwrcal_mif_asv_table, asv_dvfs_mif);
	asv_voltage_init_table(&pwrcal_int_asv_table, asv_dvfs_int);
	asv_voltage_init_table(&pwrcal_cam_asv_table, asv_dvfs_cam);
	asv_voltage_init_table(&pwrcal_disp_asv_table, asv_dvfs_disp);
}

static void asv_rcc_table_init(void)
{
	asv_rcc_init_table(&pwrcal_big_rcc_table, asv_dvfs_big);
	asv_rcc_init_table(&pwrcal_little_rcc_table, asv_dvfs_little);
	asv_rcc_init_table(&pwrcal_g3d_rcc_table, asv_dvfs_g3d);
	asv_rcc_init_table(&pwrcal_mif_rcc_table, asv_dvfs_mif);
}

static int asv_init(void)
{
	struct vclk *vclk;

	vclk = cal_get_vclk(dvfs_big);
	asv_dvfs_big = to_dfs(vclk);
	asv_dvfs_big->dfsops->get_asv_table = dvfsbig_get_asv_table;
/*	asv_dvfs_big->dfsops->set_ema = dvfsbig_set_ema;
*/
	vclk = cal_get_vclk(dvfs_little);
	asv_dvfs_little = to_dfs(vclk);
	asv_dvfs_little->dfsops->get_asv_table = dvfslittle_get_asv_table;
/*	asv_dvfs_little->dfsops->set_ema = dvfslittle_set_ema;
*/
	vclk = cal_get_vclk(dvfs_g3d);
	asv_dvfs_g3d = to_dfs(vclk);
	asv_dvfs_g3d->dfsops->get_asv_table = dvfsg3d_get_asv_table;
	asv_dvfs_g3d->dfsops->set_ema = dfsg3d_set_ema;

	vclk = cal_get_vclk(dvfs_mif);
	asv_dvfs_mif = to_dfs(vclk);
	asv_dvfs_mif->dfsops->get_asv_table = dvfsmif_get_asv_table;

	vclk = cal_get_vclk(dvfs_int);
	asv_dvfs_int = to_dfs(vclk);
	asv_dvfs_int->dfsops->get_asv_table = dvfsint_get_asv_table;
/*	asv_dvfs_int->dfsops->set_ema = dvfsint_set_ema;
*/
	vclk = cal_get_vclk(dvfs_cam);
	asv_dvfs_cam = to_dfs(vclk);
	asv_dvfs_cam->dfsops->get_asv_table = dvfscam_get_asv_table;

	vclk = cal_get_vclk(dvfs_disp);
	asv_dvfs_disp = to_dfs(vclk);
	asv_dvfs_disp->dfsops->get_asv_table = dvfsdisp_get_asv_table;

	pwrcal_big_asv_table = NULL;
	pwrcal_little_asv_table = NULL;
	pwrcal_g3d_asv_table = NULL;
	pwrcal_mif_asv_table = NULL;
	pwrcal_int_asv_table = NULL;
	pwrcal_cam_asv_table = NULL;
	pwrcal_disp_asv_table = NULL;

	pwrcal_big_rcc_table = NULL;
	pwrcal_little_rcc_table = NULL;
	pwrcal_g3d_rcc_table = NULL;
	pwrcal_mif_rcc_table = NULL;

	asv_get_asvinfo();
	asv_voltage_table_init();
	asv_rcc_table_init();

	return 0;
}

static void asv_print_info(void)
{
	pr_info("asv_table_ver : %d\n", asv_tbl_info.asv_table_ver);
	pr_info("fused_grp : %d\n", asv_tbl_info.fused_grp);

	pr_info("mngs_asv_group : %d\n", asv_tbl_info.mngs_asv_group);
	pr_info("mngs_modified_group : %d\n", asv_tbl_info.mngs_modified_group);
	pr_info("apollo_asv_group : %d\n", asv_tbl_info.apollo_asv_group);
	pr_info("apollo_modified_group : %d\n", asv_tbl_info.apollo_modified_group);

	pr_info("g3d_asv_group : %d\n", asv_tbl_info.g3d_asv_group);
	pr_info("g3d_modified_group : %d\n", asv_tbl_info.g3d_modified_group);
	pr_info("mif_asv_group : %d\n", asv_tbl_info.mif_asv_group);
	pr_info("mif_modified_group : %d\n", asv_tbl_info.mif_modified_group);

	pr_info("int_asv_group : %d\n", asv_tbl_info.int_asv_group);
	pr_info("int_modified_group : %d\n", asv_tbl_info.int_modified_group);
	pr_info("disp_asv_group : %d\n", asv_tbl_info.disp_asv_group);
	pr_info("disp_modified_group : %d\n", asv_tbl_info.disp_modified_group);

	pr_info("g3d_mcs0 : %d\n", asv_tbl_info.g3d_mcs0);
	pr_info("g3d_mcs1 : %d\n", asv_tbl_info.g3d_mcs1);
}

static void rcc_print_info(void)
{
	int lv, max_lv, asv;

	pr_info("==== RCC Table ====\n");
	max_lv = asv_dvfs_big->table->num_of_lv;
	if (max_lv > 25)
		max_lv = 25;
	for (lv = 0; lv < max_lv; lv++) {
		asv = get_asv_group(cal_asv_dvfs_big, lv);
		pr_info("big rcc [%d] : %d\n", lv, pwrcal_big_rcc_table[asv_tbl_info.asv_table_ver].table[lv].voltage[asv]);
	}

	max_lv = asv_dvfs_little->table->num_of_lv;
	if (max_lv > 20)
		max_lv = 20;
	for (lv = 0; lv < max_lv; lv++) {
		asv = get_asv_group(cal_asv_dvfs_little, lv);
		pr_info("little rcc [%d] : %d\n", lv, pwrcal_little_rcc_table[asv_tbl_info.asv_table_ver].table[lv].voltage[asv]);
	}

	max_lv = asv_dvfs_g3d->table->num_of_lv;
	if (max_lv > 11)
		max_lv = 11;
	for (lv = 0; lv < max_lv; lv++) {
		asv = get_asv_group(cal_asv_dvfs_g3d, lv);
		pr_info("g3d rcc [%d] : %d\n", lv, pwrcal_g3d_rcc_table[asv_tbl_info.asv_table_ver].table[lv].voltage[asv]);
	}

	max_lv = asv_dvfs_mif->table->num_of_lv;
	if (max_lv > 16)
		max_lv = 16;
	for (lv = 0; lv < max_lv; lv++) {
		asv = get_asv_group(cal_asv_dvfs_mif, lv);
		pr_info("mif rcc [%d] : %d\n", lv, pwrcal_mif_rcc_table[asv_tbl_info.asv_table_ver].table[lv].voltage[asv]);
	}
}

struct cal_asv_ops cal_asv_ops = {
	.print_asv_info = asv_print_info,
	.print_rcc_info = rcc_print_info,
	.set_grp = asv_set_grp,
	.set_tablever = asv_set_tablever,
	.set_rcc_table = asv_rcc_set_table,
	.asv_init = asv_init,
};
