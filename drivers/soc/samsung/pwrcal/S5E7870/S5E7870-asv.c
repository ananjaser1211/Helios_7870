#include "../pwrcal.h"
#include "../pwrcal-env.h"
#include "../pwrcal-rae.h"
#include "../pwrcal-vclk.h"
#include "../pwrcal-asv.h"
#include "S5E7870-cmusfr.h"
#include "S5E7870-sysreg.h"
#include "S5E7870-vclk.h"
#include "S5E7870-vclk-internal.h"



#ifdef PWRCAL_TARGET_LINUX
#include <soc/samsung/ect_parser.h>
//#include <linux/io.h>
//#include <mach/map.h>
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
	cal_asv_dvfs_cpucl0,
	cal_asv_dvfs_cpucl1,
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

struct asv_tbl_info {
	unsigned cpucl0_asv_group:4;
	int cpucl0_modified_group:4;
	unsigned cpucl0_ssa10:2;
	unsigned cpucl0_ssa11:2;
	unsigned cpucl0_ssa0:4;
	unsigned cpucl1_asv_group:4;
	int cpucl1_modified_group:4;
	unsigned cpucl1_ssa10:2;
	unsigned cpucl1_ssa11:2;
	unsigned cpucl1_ssa0:4;

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
	unsigned asb_pgm_version:8;

	unsigned cp_asv_group:4;
	unsigned cp_modified_group:4;
	unsigned cp_ssa10:2;
	unsigned cp_ssa11:2;
	unsigned cp_ssa0:4;

	unsigned cpucl0_vthr:2;
	unsigned cpucl0_delta:2;
	unsigned cpucl1_vthr:2;
	unsigned cpucl1_delta:2;
	unsigned g3d_vthr:2;
	unsigned g3d_delta:2;
	unsigned int_vthr:2;
	unsigned int_delta:2;
	unsigned mif_vthr:2;
	unsigned mif_delta:2;

	unsigned cpu_cl_ema:3;
	unsigned cpu_cl_emas:1;
	unsigned cpu_cl_emaw:2;
	unsigned reserve_21:2;
	unsigned g3d_ema:3;
	unsigned g3d_emas:1;
	unsigned g3d_emaw:2;
	unsigned reserved_22:2;
	unsigned cp_rwa:6;
	unsigned cp_ema:4;
	unsigned int_ema:4;
	unsigned gnss_ema:4;
	unsigned product_group_sel:1;
	unsigned product_group_0:4;
	unsigned product_group_1:4;
	unsigned reserved_23:1;
};
#define ASV_INFO_ADDR_BASE	(0x100D9000)
#define ASV_INFO_ADDR_CNT	(sizeof(struct asv_tbl_info) / 4)

static struct asv_tbl_info asv_tbl_info;

static struct asv_table_list *pwrcal_cpucl0_asv_table;
static struct asv_table_list *pwrcal_cpucl1_asv_table;
static struct asv_table_list *pwrcal_g3d_asv_table;
static struct asv_table_list *pwrcal_mif_asv_table;
static struct asv_table_list *pwrcal_int_asv_table;
static struct asv_table_list *pwrcal_disp_asv_table;
static struct asv_table_list *pwrcal_cam_asv_table;

static struct asv_table_list *pwrcal_cpucl0_rcc_table;
static struct asv_table_list *pwrcal_cpucl1_rcc_table;
static struct asv_table_list *pwrcal_g3d_rcc_table;
static struct asv_table_list *pwrcal_mif_rcc_table;

static struct pwrcal_vclk_dfs *asv_dvfs_cpucl0;
static struct pwrcal_vclk_dfs *asv_dvfs_cpucl1;
static struct pwrcal_vclk_dfs *asv_dvfs_g3d;
static struct pwrcal_vclk_dfs *asv_dvfs_mif;
static struct pwrcal_vclk_dfs *asv_dvfs_int;
static struct pwrcal_vclk_dfs *asv_dvfs_disp;
static struct pwrcal_vclk_dfs *asv_dvfs_cam;

static unsigned int cpucl0_subgrp_index = 256;
static unsigned int cpucl1_subgrp_index = 256;
static unsigned int g3d_subgrp_index = 256;
static unsigned int mif_subgrp_index = 256;
static unsigned int int_subgrp_index = 256;
static unsigned int cam_subgrp_index = 256;
static unsigned int disp_subgrp_index = 256;

static unsigned int cpucl0_ssa1_table[8];
static unsigned int cpucl1_ssa1_table[8];
static unsigned int g3d_ssa1_table[8];
static unsigned int mif_ssa1_table[8];
static unsigned int int_ssa1_table[8];
static unsigned int cam_ssa1_table[8];
static unsigned int disp_ssa1_table[8];

static unsigned int cpucl0_ssa0_base;
static unsigned int cpucl1_ssa0_base;
static unsigned int g3d_ssa0_base;
static unsigned int mif_ssa0_base;
static unsigned int int_ssa0_base;
static unsigned int cam_ssa0_base;
static unsigned int disp_ssa0_base;

static unsigned int cpucl0_ssa0_offset;
static unsigned int cpucl1_ssa0_offset;
static unsigned int g3d_ssa0_offset;
static unsigned int mif_ssa0_offset;
static unsigned int int_ssa0_offset;
static unsigned int cam_ssa0_offset;
static unsigned int disp_ssa0_offset;

static void asv_set_grp(unsigned int id, unsigned int asvgrp)
{
	force_asv_group[id & 0x0000FFFF] = FORCE_ASV_MAGIC | asvgrp;
}

static void asv_set_tablever(unsigned int version)
{
	asv_tbl_info.asv_table_ver = version;

	return;
}

static void asv_set_ssa0(unsigned int id, unsigned int ssa0)
{
	switch (id & 0x0000FFFF) {
	case cal_asv_dvfs_cpucl0:
		asv_tbl_info.cpucl0_ssa0 = ssa0;
		break;
	case cal_asv_dvfs_cpucl1:
		asv_tbl_info.cpucl1_ssa0 = ssa0;
		break;
	case cal_asv_dvfs_g3d:
		asv_tbl_info.g3d_ssa0 = ssa0;
		break;
	case cal_asv_dvfs_mif:
		asv_tbl_info.mif_ssa0 = ssa0;
		break;
	case cal_asv_dvfs_int:
		asv_tbl_info.int_ssa0 = ssa0;
		break;
	case cal_asv_dvfs_cam:
		asv_tbl_info.disp_ssa0 = ssa0;
		break;
	case cal_asv_dvfs_disp:
		asv_tbl_info.disp_ssa0 = ssa0;
		break;
	default:
		break;
	}

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

	domain = ect_asv_get_domain(asv_block, "dvfs_cpucl0");
	if (!domain)
		BUG();

	if (asv_tbl_info.asv_table_ver >= domain->num_of_table) {
		pr_err("!! CHECK !!\n");
		pr_err("ASV Table version higher than ECT(Fused ver:%d)\n",
				asv_tbl_info.asv_table_ver);
		pr_err("!! CHECK !! \n");
		asv_tbl_info.asv_table_ver = domain->num_of_table - 1;
	}

	lv = get_max_freq_lv(domain, asv_tbl_info.asv_table_ver);
	if (lv >= 0)
		asv_dvfs_cpucl0->table->max_freq = domain->level_list[lv] * 1000;
	lv = get_min_freq_lv(domain, asv_tbl_info.asv_table_ver);
	if (lv >= 0)
		asv_dvfs_cpucl0->table->min_freq = domain->level_list[lv] * 1000;

	domain = ect_asv_get_domain(asv_block, "dvfs_cpucl1");
	if (!domain)
		BUG();
	lv = get_max_freq_lv(domain, asv_tbl_info.asv_table_ver);
	if (lv >= 0)
		asv_dvfs_cpucl1->table->max_freq = domain->level_list[lv] * 1000;
	lv = get_min_freq_lv(domain, asv_tbl_info.asv_table_ver);
	if (lv >= 0)
		asv_dvfs_cpucl1->table->min_freq = domain->level_list[lv] * 1000;

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
	asv_dvfs_cpucl0->table->max_freq = 1586000;
	asv_dvfs_cpucl1->table->max_freq = 1586000;
	asv_dvfs_g3d->table->max_freq = 1246000;
	asv_dvfs_mif->table->max_freq = 900000;
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
		asv_tbl_info.cpucl0_asv_group = 0;
		asv_tbl_info.cpucl0_modified_group = 0;
		asv_tbl_info.cpucl0_ssa10 = 0;
		asv_tbl_info.cpucl0_ssa11 = 0;
		asv_tbl_info.cpucl0_ssa0 = 0;
		asv_tbl_info.cpucl1_asv_group = 0;
		asv_tbl_info.cpucl1_modified_group = 0;
		asv_tbl_info.cpucl1_ssa10 = 0;
		asv_tbl_info.cpucl1_ssa11 = 0;
		asv_tbl_info.cpucl1_ssa0 = 0;

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
		asv_tbl_info.asb_pgm_version = 0;

		asv_tbl_info.cp_asv_group = 0;
		asv_tbl_info.cp_modified_group = 0;
		asv_tbl_info.cp_ssa10 = 0;
		asv_tbl_info.cp_ssa11 = 0;
		asv_tbl_info.cp_ssa0 = 0;

		asv_tbl_info.cpucl0_vthr = 0;
		asv_tbl_info.cpucl0_delta = 0;
		asv_tbl_info.cpucl1_vthr = 0;
		asv_tbl_info.cpucl1_delta = 0;
		asv_tbl_info.g3d_vthr = 0;
		asv_tbl_info.g3d_delta = 0;
		asv_tbl_info.int_vthr = 0;
		asv_tbl_info.int_delta = 0;
		asv_tbl_info.mif_vthr = 0;
		asv_tbl_info.mif_delta = 0;

		asv_tbl_info.cpu_cl_ema = 0;
		asv_tbl_info.cpu_cl_emas = 0;
		asv_tbl_info.cpu_cl_emaw = 0;
		asv_tbl_info.reserve_21 = 0;
		asv_tbl_info.g3d_ema = 0;
		asv_tbl_info.g3d_emas = 0;
		asv_tbl_info.g3d_emaw = 0;
		asv_tbl_info.reserved_22 = 0;
		asv_tbl_info.cp_rwa = 0;
		asv_tbl_info.cp_ema = 0;
		asv_tbl_info.int_ema = 0;
		asv_tbl_info.gnss_ema = 0;
		asv_tbl_info.product_group_sel = 0;
		asv_tbl_info.product_group_0 = 0;
		asv_tbl_info.product_group_1 = 0;
		asv_tbl_info.reserved_23 = 0;
	}
	asv_set_freq_limit();
}


static int get_asv_group(enum dvfs_id domain, unsigned int lv)
{
	int asv = 0;
	int mod = 0;

	switch (domain) {
	case cal_asv_dvfs_cpucl0:
		asv = asv_tbl_info.cpucl0_asv_group;
		mod = asv_tbl_info.cpucl0_modified_group;
		break;
	case cal_asv_dvfs_cpucl1:
		asv = asv_tbl_info.cpucl1_asv_group;
		mod = asv_tbl_info.cpucl1_modified_group;
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
	unsigned int *ssa1_table = NULL;
	unsigned int ssa0_base = 0, ssa0_offset = 0;

	switch (domain) {
	case cal_asv_dvfs_cpucl0:
		asv = get_asv_group(cal_asv_dvfs_cpucl0, lv);
		ssa10 = asv_tbl_info.cpucl0_ssa10;
		ssa11 = asv_tbl_info.cpucl0_ssa11;
		ssa0 = asv_tbl_info.cpucl0_ssa0;
		subgrp_index = cpucl0_subgrp_index;
		ssa0_base = cpucl0_ssa0_base;
		ssa0_offset = cpucl0_ssa0_offset;
		ssa1_table = cpucl0_ssa1_table;
		table = pwrcal_cpucl0_asv_table[asv_tbl_info.asv_table_ver].table[lv].voltage;
		break;
	case cal_asv_dvfs_cpucl1:
		asv = get_asv_group(cal_asv_dvfs_cpucl1, lv);
		ssa10 = asv_tbl_info.cpucl1_ssa10;
		ssa11 = asv_tbl_info.cpucl1_ssa11;
		ssa0 = asv_tbl_info.cpucl1_ssa0;
		subgrp_index = cpucl1_subgrp_index;
		ssa0_base = cpucl1_ssa0_base;
		ssa0_offset = cpucl1_ssa0_offset;
		ssa1_table = cpucl1_ssa1_table;
		table = pwrcal_cpucl1_asv_table[asv_tbl_info.asv_table_ver].table[lv].voltage;
		break;
	case cal_asv_dvfs_g3d:
		asv = get_asv_group(cal_asv_dvfs_g3d, lv);
		ssa10 = asv_tbl_info.g3d_ssa10;
		ssa11 = asv_tbl_info.g3d_ssa11;
		ssa0 = asv_tbl_info.g3d_ssa0;
		subgrp_index = g3d_subgrp_index;
		ssa0_base = g3d_ssa0_base;
		ssa0_offset = g3d_ssa0_offset;
		ssa1_table = g3d_ssa1_table;
		table = pwrcal_g3d_asv_table[asv_tbl_info.asv_table_ver].table[lv].voltage;
		break;
	case cal_asv_dvfs_mif:
		asv = get_asv_group(cal_asv_dvfs_mif, lv);
		ssa10 = asv_tbl_info.mif_ssa10;
		ssa11 = asv_tbl_info.mif_ssa11;
		ssa0 = asv_tbl_info.mif_ssa0;
		subgrp_index = mif_subgrp_index;
		ssa0_base = mif_ssa0_base;
		ssa0_offset = mif_ssa0_offset;
		ssa1_table = mif_ssa1_table;
		table = pwrcal_mif_asv_table[asv_tbl_info.asv_table_ver].table[lv].voltage;
		break;
	case cal_asv_dvfs_int:
		asv = get_asv_group(cal_asv_dvfs_int, lv);
		ssa10 = asv_tbl_info.int_ssa10;
		ssa11 = asv_tbl_info.int_ssa11;
		ssa0 = asv_tbl_info.int_ssa0;
		subgrp_index = int_subgrp_index;
		ssa0_base = int_ssa0_base;
		ssa0_offset = int_ssa0_offset;
		ssa1_table = int_ssa1_table;
		table = pwrcal_int_asv_table[asv_tbl_info.asv_table_ver].table[lv].voltage;
		break;
	case cal_asv_dvfs_cam:
		asv = get_asv_group(cal_asv_dvfs_cam, lv);
		ssa10 = asv_tbl_info.disp_ssa10;
		ssa11 = asv_tbl_info.disp_ssa11;
		ssa0 = asv_tbl_info.disp_ssa0;
		subgrp_index = cam_subgrp_index;
		ssa0_base = cam_ssa0_base;
		ssa0_offset = cam_ssa0_offset;
		ssa1_table = cam_ssa1_table;
		table = pwrcal_cam_asv_table[asv_tbl_info.asv_table_ver].table[lv].voltage;
		break;
	case cal_asv_dvfs_disp:
		asv = get_asv_group(cal_asv_dvfs_disp, lv);
		ssa10 = asv_tbl_info.disp_ssa10;
		ssa11 = asv_tbl_info.disp_ssa11;
		ssa0 = asv_tbl_info.disp_ssa0;
		subgrp_index = disp_subgrp_index;
		ssa0_base = disp_ssa0_base;
		ssa0_offset = disp_ssa0_offset;
		ssa1_table = disp_ssa1_table;
		table = pwrcal_disp_asv_table[asv_tbl_info.asv_table_ver].table[lv].voltage;
		break;
	default:
		BUG();	/* Never reach */
		break;
	}

	volt = table[asv];

	if (ssa1_table) {
		if (lv < subgrp_index)
			volt += ssa1_table[ssa10];
		else
			volt += ssa1_table[ssa11 + 4];
	}
	if (ssa0_base)
		if (volt < ssa0_base + ssa0 * ssa0_offset)
			volt = ssa0_base + ssa0 * ssa0_offset;

	return volt;

}


static int dvfscpucl0_get_asv_table(unsigned int *table)
{
	int lv, max_lv;

	max_lv = asv_dvfs_cpucl0->table->num_of_lv;

	for (lv = 0; lv < max_lv; lv++)
	{
	/* CL0 Voltage Override
	Define Needed Voltages manually */
	table[0] = 1200000;
	table[1] = 1375000;
	table[2] = 1150000;
	table[3] = 1000000;
	table[4] = 950000;
	table[5] = 900000;
	table[6] = 850000;
	table[7] = 806250;
	table[8] = 768750;
	table[9] = 725000;
	table[10] = 687500;
	table[11] = 675000;
	table[12] = 662500;
	table[13] = 631250;
	table[14] = 587500;
	table[15] = 550000;
	table[16] = 531250;	
	}

	return max_lv;
}

static int dvfscpucl1_get_asv_table(unsigned int *table)
{
	int lv, max_lv;

	max_lv = asv_dvfs_cpucl1->table->num_of_lv;

	for (lv = 0; lv < max_lv; lv++)
	{
	/* CL1 Voltage Override
	Define Needed Voltages manually */
	table[0] = 1200000;
	table[1] = 1325000;
	table[2] = 1150000;
	table[3] = 1000000;
	table[4] = 950000;
	table[5] = 900000;
	table[6] = 850000;
	table[7] = 806250;
	table[8] = 768750;
	table[9] = 725000;
	table[10] = 687500;
	table[11] = 675000;
	table[12] = 662500;
	table[13] = 631250;
	table[14] = 587500;
	table[15] = 550000;
	table[16] = 531250;	
	}

	return max_lv;
}

static int dfscpu_set_ema(unsigned int volt)
{
	unsigned int cpu_ema, ema_temp;

	if (volt >= 950000) {
		ema_temp = (asv_tbl_info.cpu_cl_ema << 3) | (asv_tbl_info.cpu_cl_emaw << 1) | asv_tbl_info.cpu_cl_emas;
		cpu_ema = (ema_temp << 12) | (ema_temp << 6) | ema_temp;
		if (cpu_ema == 0)
			cpu_ema = 0x1B6D2;
		pwrcal_writel(CPUCL0_EMA_CON, cpu_ema);
		pwrcal_writel(CPUCL1_EMA_CON, cpu_ema);
		}
	else {
		pwrcal_writel(CPUCL0_EMA_CON, 0x12492);
		pwrcal_writel(CPUCL1_EMA_CON, 0x12492);
		}

	return 0;
}

static int dvfsg3d_get_asv_table(unsigned int *table)
{
	int lv, max_lv;

	max_lv = asv_dvfs_g3d->table->num_of_lv;

	for (lv = 0; lv < max_lv; lv++)
		table[lv] = get_asv_voltage(cal_asv_dvfs_g3d, lv);

	return max_lv;
}

static int dfsg3d_set_ema(unsigned int volt)
{
	unsigned int g3d_ema;

	if (asv_dvfs_g3d->vclk.ref_count == 0)
		return 0;

	if (volt >= 750000) {
		g3d_ema = (asv_tbl_info.g3d_emas << 8) | (asv_tbl_info.g3d_emaw << 4) | asv_tbl_info.g3d_ema;
		if (g3d_ema != 0) {
			pwrcal_writel(G3D_EMA_RA1_HS_CON, g3d_ema);
			pwrcal_writel(G3D_EMA_RF1_HS_CON, g3d_ema);
			pwrcal_writel(G3D_EMA_RF2_HS_CON, g3d_ema);
			}
		else {
			pwrcal_writel(G3D_EMA_RA1_HS_CON, 0x12);
			pwrcal_writel(G3D_EMA_RF1_HS_CON, 0x12);
			pwrcal_writel(G3D_EMA_RF2_HS_CON, 0x22);
			}
		}
	else {
		pwrcal_writel(G3D_EMA_RA1_HS_CON, 0x12);
		pwrcal_writel(G3D_EMA_RF1_HS_CON, 0x12);
		pwrcal_writel(G3D_EMA_RF2_HS_CON, 0x24);
		}

	return 0;
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

static int dvfsdisp_get_asv_table(unsigned int *table)
{
	int lv, max_lv;

	max_lv = asv_dvfs_disp->table->num_of_lv;

	for (lv = 0; lv < max_lv; lv++)
		table[lv] = get_asv_voltage(cal_asv_dvfs_disp, lv);

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

static int asv_rcc_set_table(void)
{
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
	asv_voltage_init_table(&pwrcal_cpucl0_asv_table, asv_dvfs_cpucl0);
	asv_voltage_init_table(&pwrcal_cpucl1_asv_table, asv_dvfs_cpucl1);
	asv_voltage_init_table(&pwrcal_g3d_asv_table, asv_dvfs_g3d);
	asv_voltage_init_table(&pwrcal_mif_asv_table, asv_dvfs_mif);
	asv_voltage_init_table(&pwrcal_int_asv_table, asv_dvfs_int);
	asv_voltage_init_table(&pwrcal_cam_asv_table, asv_dvfs_cam);
	asv_voltage_init_table(&pwrcal_disp_asv_table, asv_dvfs_disp);
}

static void asv_rcc_table_init(void)
{
	asv_rcc_init_table(&pwrcal_cpucl0_rcc_table, asv_dvfs_cpucl0);
	asv_rcc_init_table(&pwrcal_cpucl1_rcc_table, asv_dvfs_cpucl1);
	asv_rcc_init_table(&pwrcal_g3d_rcc_table, asv_dvfs_g3d);
	asv_rcc_init_table(&pwrcal_mif_rcc_table, asv_dvfs_mif);
}

static void asv_ssa_init(void)
{
	int i;
	void *gen_block;
	struct ect_gen_param_table *param;
	unsigned int asv_table_version = asv_tbl_info.asv_table_ver;

	gen_block = ect_get_block("GEN");
	if (gen_block == NULL)
		return;

	param = ect_gen_param_get_table(gen_block, "SSA_CPUCL0");
	if (param) {
		cpucl0_subgrp_index = param->parameter[asv_table_version * param->num_of_col + 1];
		cpucl0_ssa0_base = param->parameter[asv_table_version * param->num_of_col + 2];
		cpucl0_ssa0_offset = param->parameter[asv_table_version * param->num_of_col + 3];
		for (i = 0; i < 8; i++)
			cpucl0_ssa1_table[i] = param->parameter[asv_table_version * param->num_of_col + 4 + i];
	}
	param = ect_gen_param_get_table(gen_block, "SSA_CPUCL1");
	if (param) {
		cpucl1_subgrp_index = param->parameter[asv_table_version * param->num_of_col + 1];
		cpucl1_ssa0_base = param->parameter[asv_table_version * param->num_of_col + 2];
		cpucl1_ssa0_offset = param->parameter[asv_table_version * param->num_of_col + 3];
		for (i = 0; i < 8; i++)
			cpucl1_ssa1_table[i] = param->parameter[asv_table_version * param->num_of_col + 4 + i];
	}
	param = ect_gen_param_get_table(gen_block, "SSA_G3D");
	if (param) {
		g3d_subgrp_index = param->parameter[asv_table_version * param->num_of_col + 1];
		g3d_ssa0_base = param->parameter[asv_table_version * param->num_of_col + 2];
		g3d_ssa0_offset = param->parameter[asv_table_version * param->num_of_col + 3];
		for (i = 0; i < 8; i++)
			g3d_ssa1_table[i] = param->parameter[asv_table_version * param->num_of_col + 4 + i];
	}
	param = ect_gen_param_get_table(gen_block, "SSA_MIF");
	if (param) {
		mif_subgrp_index = param->parameter[asv_table_version * param->num_of_col + 1];
		mif_ssa0_base = param->parameter[asv_table_version * param->num_of_col + 2];
		mif_ssa0_offset = param->parameter[asv_table_version * param->num_of_col + 3];
		for (i = 0; i < 8; i++)
			mif_ssa1_table[i] = param->parameter[asv_table_version * param->num_of_col + 4 + i];
	}
	param = ect_gen_param_get_table(gen_block, "SSA_INT");
	if (param) {
		int_subgrp_index = param->parameter[asv_table_version * param->num_of_col + 1];
		int_ssa0_base = param->parameter[asv_table_version * param->num_of_col + 2];
		int_ssa0_offset = param->parameter[asv_table_version * param->num_of_col + 3];
		for (i = 0; i < 8; i++)
			int_ssa1_table[i] = param->parameter[asv_table_version * param->num_of_col + 4 + i];
	}
	param = ect_gen_param_get_table(gen_block, "SSA_CAM");
	if (param) {
		cam_subgrp_index = param->parameter[asv_table_version * param->num_of_col + 1];
		cam_ssa0_base = param->parameter[asv_table_version * param->num_of_col + 2];
		cam_ssa0_offset = param->parameter[asv_table_version * param->num_of_col + 3];
		for (i = 0; i < 8; i++)
			cam_ssa1_table[i] = param->parameter[asv_table_version * param->num_of_col + 4 + i];
	}
	param = ect_gen_param_get_table(gen_block, "SSA_DISP");
	if (param) {
		disp_subgrp_index = param->parameter[asv_table_version * param->num_of_col + 1];
		disp_ssa0_base = param->parameter[asv_table_version * param->num_of_col + 2];
		disp_ssa0_offset = param->parameter[asv_table_version * param->num_of_col + 3];
		for (i = 0; i < 8; i++)
			disp_ssa1_table[i] = param->parameter[asv_table_version * param->num_of_col + 4 + i];
	}
}

int cal_asv_init(void)
{
	struct vclk *vclk;

	vclk = cal_get_vclk(dvfs_cpucl0);
	asv_dvfs_cpucl0 = to_dfs(vclk);
	asv_dvfs_cpucl0->dfsops->get_asv_table = dvfscpucl0_get_asv_table;
	asv_dvfs_cpucl0->dfsops->set_ema = dfscpu_set_ema;

	vclk = cal_get_vclk(dvfs_cpucl1);
	asv_dvfs_cpucl1 = to_dfs(vclk);
	asv_dvfs_cpucl1->dfsops->get_asv_table = dvfscpucl1_get_asv_table;
	asv_dvfs_cpucl1->dfsops->set_ema = dfscpu_set_ema;

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

	vclk = cal_get_vclk(dvfs_disp);
	asv_dvfs_disp = to_dfs(vclk);
	asv_dvfs_disp->dfsops->get_asv_table = dvfsdisp_get_asv_table;

	vclk = cal_get_vclk(dvfs_cam);
	asv_dvfs_cam = to_dfs(vclk);
	asv_dvfs_cam->dfsops->get_asv_table = dvfscam_get_asv_table;

	pwrcal_cpucl0_asv_table = NULL;
	pwrcal_cpucl1_asv_table = NULL;
	pwrcal_g3d_asv_table = NULL;
	pwrcal_mif_asv_table = NULL;
	pwrcal_int_asv_table = NULL;
	pwrcal_disp_asv_table = NULL;
	pwrcal_cam_asv_table = NULL;

	pwrcal_cpucl0_rcc_table = NULL;
	pwrcal_cpucl1_rcc_table = NULL;
	pwrcal_g3d_rcc_table = NULL;
	pwrcal_mif_rcc_table = NULL;

	asv_get_asvinfo();
	asv_voltage_table_init();
	asv_rcc_table_init();
	asv_ssa_init();

	return 0;
}
void asv_print_info(void)
{
	pr_info("asv_table_ver : %d\n", asv_tbl_info.asv_table_ver);
	pr_info("fused_grp : %d\n", asv_tbl_info.fused_grp);

	pr_info("cpucl0_asv_group : %d\n", asv_tbl_info.cpucl0_asv_group);
	pr_info("cpucl1_asv_group : %d\n", asv_tbl_info.cpucl1_asv_group);

	pr_info("g3d_asv_group : %d\n", asv_tbl_info.g3d_asv_group);
	pr_info("mif_asv_group : %d\n", asv_tbl_info.mif_asv_group);

	pr_info("int_asv_group : %d\n", asv_tbl_info.int_asv_group);
	pr_info("disp_asv_group : %d\n", asv_tbl_info.disp_asv_group);
}

static void rcc_print_info(void)
{

}

static int asv_get_grp(unsigned int id, unsigned int lv)
{
	return get_asv_group((enum dvfs_id)id, lv);
}

static int asv_get_tablever(void)
{
	return (int)(asv_tbl_info.asv_table_ver);
}

#if defined(CONFIG_SEC_DEBUG)
enum ids_info
{
	table_ver,
	cpu_asv,
	g3d_asv,
	mif_asv
};

int asv_ids_information(enum ids_info id)
{
	int res = 0;

	switch (id) {
		case table_ver:
			res = asv_tbl_info.asv_table_ver;
			break;
		case cpu_asv:
			res = asv_tbl_info.cpucl0_asv_group;
			break;
		case g3d_asv:
			res = asv_tbl_info.g3d_asv_group;
			break;
		case mif_asv:
			res = asv_tbl_info.mif_asv_group;
			break;
		default:
			break;
	};

	return res;
}
#endif /* CONFIG_SEC_DEBUG */

struct cal_asv_ops cal_asv_ops = {
	.print_asv_info = asv_print_info,
	.print_rcc_info = rcc_print_info,
	.asv_init = cal_asv_init,
	.set_grp = asv_set_grp,
	.get_grp = asv_get_grp,
	.set_tablever = asv_set_tablever,
	.get_tablever = asv_get_tablever,
	.set_rcc_table = asv_rcc_set_table,
	.set_ssa0 = asv_set_ssa0,
};
