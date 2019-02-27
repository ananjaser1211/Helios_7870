#include "../pwrcal.h"
#include "../pwrcal-env.h"
#include "../pwrcal-pmu.h"
#include "../pwrcal-dfs.h"
#include "../pwrcal-rae.h"
#include "../pwrcal-asv.h"
#include "S5E8890-cmusfr.h"
#include "S5E8890-pmusfr.h"
#include "S5E8890-cmu.h"
#include "S5E8890-vclk-internal.h"

#include <soc/samsung/ect_parser.h>


extern int offset_percent;
extern int set_big_volt;
extern int set_lit_volt;
extern int set_int_volt;
extern int set_mif_volt;
extern int set_g3d_volt;
extern int set_disp_volt;
extern int set_cam_volt;

static int common_get_margin_param(unsigned int target_type)
{
	int add_volt = 0;

	switch (target_type) {
	case dvfs_big:
		add_volt = set_big_volt;
		break;
	case dvfs_little:
		add_volt = set_lit_volt;
		break;
	case dvfs_g3d:
		add_volt = set_g3d_volt;
		break;
	case dvfs_mif:
		add_volt = set_mif_volt;
		break;
	case dvfs_int:
		add_volt = set_int_volt;
		break;
	case dvfs_disp:
		add_volt = set_disp_volt;
		break;
	case dvfs_cam:
		add_volt = set_cam_volt;
		break;
	default:
		return add_volt;
	}
	return add_volt;
}

static void pscdc_trans(unsigned int sci_rate,
			unsigned int aclk_mif_rate,
			unsigned int top_mux_bus_pll,
			unsigned int top_div_bus_pll,
			unsigned int mif_mux_mif_pll,
			unsigned int mif_mux_bus_pll,
			unsigned int mif_mux_aclk_mif_pll,
			unsigned int top_mux_ccore,
			unsigned int top_div_ccore,
			unsigned int ccore_mux_user,
			unsigned int mif_pause)
{
	pwrcal_writel(PSCDC_CTRL_MIF0,	0x00000001);
	pwrcal_writel(PSCDC_CTRL_MIF1,	0x00000001);
	pwrcal_writel(PSCDC_CTRL_MIF2,	0x00000001);
	pwrcal_writel(PSCDC_CTRL_MIF3,	0x00000001);
	pwrcal_writel(PSCDC_CTRL_CCORE,	0x00000001);
	pwrcal_writel(PSCDC_CTRL1,	(sci_rate << 16) | aclk_mif_rate);
	pwrcal_writel(PSCDC_SMC_FIFO_CLK_CON0,	0x80000000 | (top_mux_bus_pll << 12));
	pwrcal_writel(PSCDC_SMC_FIFO_CLK_CON1,	0x80010000 | (top_div_bus_pll));
	pwrcal_writel(PSCDC_SMC_FIFO_CLK_CON2,	0x80020000 | (mif_mux_mif_pll << 12));
	pwrcal_writel(PSCDC_SMC_FIFO_CLK_CON3,	0x80030000 | (mif_mux_bus_pll << 12));
	pwrcal_writel(PSCDC_SMC_FIFO_CLK_CON4,	0x80040000 | (mif_mux_aclk_mif_pll << 12));
	pwrcal_writel(PSCDC_SCI_FIFO_CLK_CON0,	0x80000000 | (top_mux_ccore << 12));
	pwrcal_writel(PSCDC_SCI_FIFO_CLK_CON1,	0x80010000 | (top_div_ccore));
	pwrcal_writel(PSCDC_SCI_FIFO_CLK_CON2,	0x80020000 | (ccore_mux_user << 12));
	pwrcal_writel(PSCDC_SCI_FIFO_CLK_CON3,	0x00000000);

	pwrcal_writel(PSCDC_CTRL0, 0x40000000 | (mif_pause << 31));
	while (pwrcal_readl(PSCDC_CTRL0) & 0xC0000000)cpu_relax();
}

/* parent rate table of mux_aclk_ccore_800 */
struct aclk_ccore_800_table {
	unsigned int rate;
	unsigned int mux;
	unsigned int div;
	unsigned int sci_ratio;
	unsigned int smc_ratio;
};
static struct aclk_ccore_800_table *rate_table_aclk_ccore_800;

static int pscdc_switch(unsigned int rate_from, unsigned int rate_switch, struct dfs_table *table)
{
	int lv_from, lv_switch;
	unsigned int rate_sci, rate_smc;	/* unit size : MHZ */
	unsigned int mux_value, div_value;
	unsigned int mux_switch;

	lv_from = dfs_get_lv(rate_from, table);
	if (lv_from < 0)
		goto errorout;
	lv_switch = dfs_get_lv(rate_switch, table) - 1;
	if (lv_switch < 0)
		goto errorout;

	/* HW constraint : Root Clock gate disable (TBD : reset value is disable) */
	pwrcal_setbit(TOP0_ROOTCLKEN_ON_GATE, 0, 0);
	pwrcal_setbit(TOP3_ROOTCLKEN_ON_GATE, 2, 0);

	if (table->trans_pre)
		table->trans_pre(rate_from, rate_switch);

	/*
	STEP 1. MIF_PLL -> SWITCHING PLL
		1-1. set other clock node. (prev)
		1-2. DREX control before switching.
		1-3. PSCDC run.
		1-4. DREX control after switching.
		1-5. set other clock node (post)
	*/

	/* 1-1 */
	if (dfs_trans_div(lv_from, lv_switch, table, TRANS_HIGH))
		goto errorout;
	if (dfs_trans_mux(lv_from, lv_switch, table, TRANS_DIFF))
		goto errorout;

	/* 1-2 */
	if (table->switch_pre)
		table->switch_pre(rate_from, rate_switch);

	/* 1-3 */
	rate_sci = rate_table_aclk_ccore_800[lv_switch].rate;
	rate_smc = rate_switch / 2000;
	mux_value = rate_table_aclk_ccore_800[lv_switch].mux;
	div_value = rate_table_aclk_ccore_800[lv_switch].div;
	mux_switch = (rate_switch >= 936000) ? 3 : 4;
	pscdc_trans(rate_sci, rate_smc, mux_switch, 0, 1, 1, 1, mux_value, div_value, 1, 1);


	/* 1-4 */
	if (table->switch_post)
		table->switch_post(rate_from, rate_switch);

	/* 1-5 */
	if (dfs_trans_div(lv_from, lv_switch, table, TRANS_LOW))
		goto errorout;

	return 0;

errorout:
	return -1;
}

static int pscdc_trasition(unsigned int rate_switch, unsigned int rate_to, struct dfs_table *table)
{
	int lv_to, lv_switch;
	unsigned int rate_sci, rate_smc;	/* unit size : MHZ */
	unsigned int mux_value, div_value;
	unsigned int mux_switch;

	lv_switch = dfs_get_lv(rate_switch, table) - 1;
	if (lv_switch < 0)
		goto errorout;
	lv_to = dfs_get_lv(rate_to, table);
	if (lv_to < 0)
		goto errorout;

	/*
	STEP 2. PLL transition
	*/
	if (dfs_trans_pll(lv_switch, lv_to, table, TRANS_FORCE))
		goto errorout;

	/*
	STEP 3. SWITCHING->MIF_PLL
		3-1. set other clock node.
		3-2. DREX control before switching.
		3-3. PSCDC run.
		3-4. DREX control after switching.
	*/
	/* 3-1 */
	if (dfs_trans_div(lv_switch, lv_to, table, TRANS_HIGH))
		goto errorout;
	if (dfs_trans_mux(lv_switch, lv_to, table, TRANS_DIFF))
		goto errorout;

	/* 3-2 */
	if (table->switch_pre)
		table->switch_pre(rate_switch, rate_to);

	/* 3-3 */
	mux_value = rate_table_aclk_ccore_800[lv_to].mux;
	div_value = rate_table_aclk_ccore_800[lv_to].div;
	mux_switch = (rate_switch >= 936000) ? 3 : 4;
	rate_sci = rate_table_aclk_ccore_800[lv_to].sci_ratio;
	rate_smc = rate_table_aclk_ccore_800[lv_to].smc_ratio;
	pscdc_trans(rate_sci, rate_smc, mux_switch, 0, 1, 1, 0, mux_value, div_value, 1, 1);

	/* 3-4 */
	if (table->switch_post)
		table->switch_post(rate_switch, rate_to);

	/* 3-5 */
	if (dfs_trans_div(lv_switch, lv_to, table, TRANS_LOW))
		goto errorout;

	if (table->trans_post)
		table->trans_post(rate_switch, rate_to);

	return 0;

errorout:
	return -1;
}

struct dfs_switch dfsbig_switches[] = {
	{	936000,	3,	1	},
	{	624000,	3,	2	},
	{	468000,	3,	3	},
	{	312000,	3,	5	},
	{	208000,	3,	8	},
};

static struct dfs_table dfsbig_table = {
	.switches = dfsbig_switches,
	.num_of_switches = ARRAY_SIZE(dfsbig_switches),
	.switch_mux = CLK(MNGS_MUX_MNGS),
	.switch_use = 1,
	.switch_notuse = 0,
	.switch_src_mux = CLK(TOP_MUX_SCLK_BUS_PLL_MNGS),
	.switch_src_div = CLK(TOP_DIV_SCLK_BUS_PLL_MNGS),
	.switch_src_gate = CLK(TOP_GATE_SCLK_BUS_PLL_MNGS),
	.switch_src_usermux = CLK(MNGS_MUX_BUS_PLL_MNGS_USER),
};

struct pwrcal_clk *dfsbig_dfsclkgrp[] = {
	CLK(MNGS_PLL),
	CLK(MNGS_DIV_MNGS),
	CLK(MNGS_DIV_ACLK_MNGS),
	CLK(MNGS_DIV_PCLK_MNGS),
	CLK(MNGS_DIV_ATCLK_MNGS_CORE),
	CLK(MNGS_DIV_PCLK_DBG_MNGS),
	CLK(MNGS_DIV_CNTCLK_MNGS),
	CLK(MNGS_DIV_MNGS_PLL),
	CLK(MNGS_DIV_SCLK_PROMISE_MNGS),
	CLK(MNGS_DIV_ATCLK_MNGS_SOC),
	CLK(MNGS_DIV_ATCLK_MNGS_CSSYS_TRACECLK),
	CLK(MNGS_DIV_ATCLK_MNGS_ASYNCATB_CAM1),
	CLK(MNGS_DIV_ATCLK_MNGS_ASYNCATB_AUD),
	CLK_NONE,
};

struct pwrcal_clk_set dfsbig_en_list[] = {
	{CLK_NONE,	0,	-1},
};

struct dfs_switch dfslittle_switches[] = {
	{	936000,	3,	0	},
	{	468000,	3,	1	},
	{	312000,	3,	2	},
	{	234000,	3,	3	},
	{	117000,	3,	7	},
};

static struct dfs_table dfslittle_table = {
	.switches = dfslittle_switches,
	.num_of_switches = ARRAY_SIZE(dfslittle_switches),
	.switch_mux = CLK(APOLLO_MUX_APOLLO),
	.switch_use = 1,
	.switch_notuse = 0,
	.switch_src_mux = CLK(TOP_MUX_SCLK_BUS_PLL_APOLLO),
	.switch_src_div = CLK(TOP_DIV_SCLK_BUS_PLL_APOLLO),
	.switch_src_gate = CLK(TOP_GATE_SCLK_BUS_PLL_APOLLO),
	.switch_src_usermux = CLK(APOLLO_MUX_BUS_PLL_APOLLO_USER),
};

struct pwrcal_clk *dfslittle_dfsclkgrp[] = {
	CLK(APOLLO_PLL),
	CLK(APOLLO_DIV_ACLK_APOLLO),
	CLK(APOLLO_DIV_PCLK_APOLLO),
	CLK(APOLLO_DIV_ATCLK_APOLLO),
	CLK(APOLLO_DIV_PCLK_DBG_APOLLO),
	CLK(APOLLO_DIV_CNTCLK_APOLLO),
	CLK(APOLLO_DIV_APOLLO_PLL),
	CLK(APOLLO_DIV_SCLK_PROMISE_APOLLO),
	CLK(APOLLO_DIV_APOLLO_RUN_MONITOR),
	CLK_NONE
};

struct pwrcal_clk_set dfslittle_en_list[] = {
	{CLK_NONE,	0,	-1},
};

struct dfs_switch dfsg3d_switches[] = {
	{	936000,	3,	0	},
	{	468000,	3,	1	},
	{	312000,	3,	2	},
	{	234000,	3,	3	},
	{	156000,	3,	5	},
	{	104000,	3,	8	},
};

static struct dfs_table dfsg3d_table = {
	.switches = dfsg3d_switches,
	.num_of_switches = ARRAY_SIZE(dfsg3d_switches),
	.switch_mux = CLK(G3D_MUX_G3D),
	.switch_use = 1,
	.switch_notuse = 0,
	.switch_src_mux = CLK(TOP_MUX_SCLK_BUS_PLL_G3D),
	.switch_src_div = CLK(TOP_DIV_SCLK_BUS_PLL_G3D),
	.switch_src_gate = CLK(TOP_GATE_SCLK_BUS_PLL_G3D),
	.switch_src_usermux = CLK(G3D_MUX_BUS_PLL_USER),
};

struct pwrcal_clk *dfsg3d_dfsclkgrp[] = {
	CLK(G3D_PLL),
	CLK(G3D_DIV_ACLK_G3D),
	CLK(G3D_DIV_PCLK_G3D),
	CLK(G3D_DIV_SCLK_HPM_G3D),
	CLK(G3D_DIV_SCLK_ATE_G3D),
	CLK_NONE,
};

struct pwrcal_clk_set dfsg3d_en_list[] = {
	{CLK_NONE,	0,	-1},
};

struct dfs_switch dfsmif_switches[] = {
	{	468000,	4,	1	},
};


extern void dfsmif_trans_pre(unsigned int rate_from, unsigned int rate_to);
extern void dfsmif_trans_post(unsigned int rate_from, unsigned int rate_to);
extern void dfsmif_switch_pre(unsigned int rate_from, unsigned int rate_to);
extern void dfsmif_switch_post(unsigned int rate_from, unsigned int rate_to);

static struct dfs_table dfsmif_table = {
	.switches = dfsmif_switches,
	.num_of_switches = ARRAY_SIZE(dfsmif_switches),
	.switch_mux = CLK_NONE,
	.switch_use = 0,
	.switch_notuse = 1,
	.private_trans = pscdc_trasition,
	.private_switch = pscdc_switch,
	.trans_pre = dfsmif_trans_pre,
	.trans_post = dfsmif_trans_post,
	.switch_pre = dfsmif_switch_pre,
	.switch_post = dfsmif_switch_post,
};

struct pwrcal_clk *dfsmif_dfsclkgrp[] = {
	CLK(MIF_PLL),
	CLK(MIF0_MUX_PCLK_MIF),
	CLK(MIF1_MUX_PCLK_MIF),
	CLK(MIF2_MUX_PCLK_MIF),
	CLK(MIF3_MUX_PCLK_MIF),
	CLK(MIF0_MUX_SCLK_HPM_MIF),
	CLK(MIF1_MUX_SCLK_HPM_MIF),
	CLK(MIF2_MUX_SCLK_HPM_MIF),
	CLK(MIF3_MUX_SCLK_HPM_MIF),
	CLK(TOP_MUX_ACLK_CCORE_800),
	CLK(TOP_MUX_ACLK_CCORE_528),
	CLK(TOP_MUX_ACLK_CCORE_264),
	CLK(TOP_MUX_ACLK_CCORE_132),
	CLK(TOP_MUX_PCLK_CCORE_66),
	CLK(TOP_MUX_ACLK_CCORE_G3D_800),
	CLK(MIF0_DIV_PCLK_MIF),
	CLK(MIF1_DIV_PCLK_MIF),
	CLK(MIF2_DIV_PCLK_MIF),
	CLK(MIF3_DIV_PCLK_MIF),
	CLK(MIF0_DIV_SCLK_HPM_MIF),
	CLK(MIF1_DIV_SCLK_HPM_MIF),
	CLK(MIF2_DIV_SCLK_HPM_MIF),
	CLK(MIF3_DIV_SCLK_HPM_MIF),
	CLK(TOP_DIV_ACLK_CCORE_800),
	CLK(TOP_DIV_ACLK_CCORE_528),
	CLK(TOP_DIV_ACLK_CCORE_264),
	CLK(TOP_DIV_ACLK_CCORE_132),
	CLK(TOP_DIV_PCLK_CCORE_66),
	CLK(TOP_DIV_ACLK_CCORE_G3D_800),
	CLK_NONE,
};

struct pwrcal_clk_set dfsmif_en_list[] = {
	{CLK(MIF0_MUX_BUS_PLL_USER),	1,	-1},
	{CLK(MIF1_MUX_BUS_PLL_USER),	1,	-1},
	{CLK(MIF2_MUX_BUS_PLL_USER),	1,	-1},
	{CLK(MIF3_MUX_BUS_PLL_USER),	1,	-1},
	{CLK_NONE,	0,	-1},
};

static struct dfs_table dfsint_table = {
};


struct pwrcal_clk *dfsint_dfsclkgrp[] = {
	CLK(TOP_MUX_ACLK_BUS0_528),
	CLK(TOP_MUX_ACLK_BUS1_528),
	CLK(TOP_MUX_ACLK_BUS0_200),
	CLK(TOP_MUX_PCLK_BUS0_132),
	CLK(TOP_MUX_PCLK_BUS1_132),
	CLK(TOP_MUX_ACLK_IMEM_266),
	CLK(TOP_MUX_ACLK_IMEM_200),
	CLK(TOP_MUX_ACLK_IMEM_100),
	CLK(TOP_MUX_ACLK_MFC_600),
	CLK(TOP_MUX_ACLK_MSCL0_528),
	CLK(TOP_MUX_ACLK_MSCL1_528),
	CLK(TOP_MUX_ACLK_PERIS_66),
	CLK(TOP_MUX_ACLK_FSYS0_200),
	CLK(TOP_MUX_ACLK_FSYS1_200),
	CLK(TOP_MUX_ACLK_PERIC0_66),
	CLK(TOP_MUX_ACLK_PERIC1_66),
	CLK(TOP_MUX_ACLK_ISP0_TREX_528),
	CLK(TOP_MUX_ACLK_ISP0_ISP0_528),
	CLK(TOP_MUX_ACLK_ISP0_TPU_400),
	CLK(TOP_MUX_ACLK_ISP1_ISP1_468),
	CLK(TOP_MUX_ACLK_CAM1_ARM_672),
	CLK(TOP_MUX_ACLK_CAM1_TREX_VRA_528),
	CLK(TOP_MUX_ACLK_CAM1_TREX_B_528),
	CLK(TOP_MUX_ACLK_CAM1_BUS_264),
	CLK(TOP_MUX_ACLK_CAM1_PERI_84),
	CLK(TOP_MUX_ACLK_CAM1_CSIS2_414),
	CLK(TOP_MUX_ACLK_CAM1_CSIS3_132),
	CLK(TOP_MUX_ACLK_CAM1_SCL_566),
	CLK(TOP_DIV_ACLK_BUS0_528),
	CLK(TOP_DIV_ACLK_BUS1_528),
	CLK(TOP_DIV_ACLK_BUS0_200),
	CLK(TOP_DIV_PCLK_BUS0_132),
	CLK(TOP_DIV_PCLK_BUS1_132),
	CLK(TOP_DIV_ACLK_IMEM_266),
	CLK(TOP_DIV_ACLK_IMEM_200),
	CLK(TOP_DIV_ACLK_IMEM_100),
	CLK(TOP_DIV_ACLK_MFC_600),
	CLK(TOP_DIV_ACLK_MSCL0_528),
	CLK(TOP_DIV_ACLK_MSCL1_528),
	CLK(TOP_DIV_ACLK_PERIS_66),
	CLK(TOP_DIV_ACLK_FSYS0_200),
	CLK(TOP_DIV_ACLK_FSYS1_200),
	CLK(TOP_DIV_ACLK_PERIC0_66),
	CLK(TOP_DIV_ACLK_PERIC1_66),
	CLK(TOP_DIV_ACLK_ISP0_TREX_528),
	CLK(TOP_DIV_ACLK_ISP0_ISP0_528),
	CLK(TOP_DIV_ACLK_ISP0_TPU_400),
	CLK(TOP_DIV_ACLK_ISP1_ISP1_468),
	CLK(TOP_DIV_ACLK_CAM1_ARM_672),
	CLK(TOP_DIV_ACLK_CAM1_TREX_VRA_528),
	CLK(TOP_DIV_ACLK_CAM1_TREX_B_528),
	CLK(TOP_DIV_ACLK_CAM1_BUS_264),
	CLK(TOP_DIV_ACLK_CAM1_PERI_84),
	CLK(TOP_DIV_ACLK_CAM1_CSIS2_414),
	CLK(TOP_DIV_ACLK_CAM1_CSIS3_132),
	CLK(TOP_DIV_ACLK_CAM1_SCL_566),
	CLK_NONE,
};

struct pwrcal_clk_set dfsint_en_list[] = {
	{CLK_NONE,	0,	-1},
};

static struct dfs_table dfscam_table = {
};

struct pwrcal_clk_set dfscam_en_list[] = {
	{CLK(ISP_PLL),	409500,	0},
	{CLK_NONE,	0,	-1},
};

struct pwrcal_clk *dfscam_dfsclkgrp[] = {
	CLK(TOP_MUX_ACLK_CAM0_TREX_528),
	CLK(TOP_MUX_ACLK_CAM0_CSIS0_414),
	CLK(TOP_MUX_ACLK_CAM0_CSIS1_168),
	CLK(TOP_MUX_ACLK_CAM0_CSIS2_234),
	CLK(TOP_MUX_ACLK_CAM0_3AA0_414),
	CLK(TOP_MUX_ACLK_CAM0_3AA1_414),
	CLK(TOP_MUX_ACLK_CAM0_CSIS3_132),
	CLK(TOP_DIV_ACLK_CAM0_TREX_528),
	CLK(TOP_DIV_ACLK_CAM0_CSIS0_414),
	CLK(TOP_DIV_ACLK_CAM0_CSIS1_168),
	CLK(TOP_DIV_ACLK_CAM0_CSIS2_234),
	CLK(TOP_DIV_ACLK_CAM0_3AA0_414),
	CLK(TOP_DIV_ACLK_CAM0_3AA1_414),
	CLK(TOP_DIV_ACLK_CAM0_CSIS3_132),
	CLK(ISP_PLL),
};

static struct dfs_table dfsdisp_table = {
};

struct pwrcal_clk *dfsdisp_dfsclkgrp[] = {
	CLK(TOP_MUX_ACLK_DISP0_0_400),
	CLK(TOP_MUX_ACLK_DISP0_1_400),
	CLK(TOP_MUX_ACLK_DISP1_0_400),
	CLK(TOP_MUX_ACLK_DISP1_1_400),
	CLK(TOP_DIV_ACLK_DISP0_0_400),
	CLK(TOP_DIV_ACLK_DISP0_1_400),
	CLK(TOP_DIV_ACLK_DISP1_0_400),
	CLK(TOP_DIV_ACLK_DISP1_1_400),
	CLK_NONE,
};


static int dfsbig_set_ema(unsigned int volt)
{
	if (volt > 1000000) {
		pwrcal_writel(MNGS_EMA_CON, 0x000E92B9);
		pwrcal_writel(MNGS_ASSIST_CON, 0x0);
	} else if (volt > 850000) {
		pwrcal_writel(MNGS_EMA_CON, 0x00109AB9);
		pwrcal_writel(MNGS_ASSIST_CON, 0x2);
	} else if (volt > 750000) {
		pwrcal_writel(MNGS_EMA_CON, 0x001099B9);
		pwrcal_writel(MNGS_ASSIST_CON, 0x2);
	} else {
		pwrcal_writel(MNGS_EMA_CON, 0x0010A9B9);
		pwrcal_writel(MNGS_ASSIST_CON, 0x3);
	}

	return 0;
}

static int dfsbig_init_smpl(void)
{
	pwrcal_setf(PWR_CTRL4_MNGS, 4, 0x3F, 7);
	pwrcal_setf(PWR_CTRL4_MNGS, 0, 0x3, 3);
	return 0;
}

static int dfsbig_set_smpl(void)
{
	pwrcal_setf(PWR_CTRL4_MNGS, 0, 0x3, 0);
	pwrcal_setf(PWR_CTRL4_MNGS, 0, 0x3, 3);
	return 0;
}

static int dfsbig_get_smpl(void)
{
	return pwrcal_getbit(PWR_CTRL4_MNGS, 12);
}

static int dfsbig_get_rate_table(unsigned long *table)
{
	return dfs_get_rate_table(&dfsbig_table, table);
}

static int dfsbig_asv_voltage_table(unsigned int *table)
{
	int i;

	for (i = 0; i < dfsbig_table.num_of_lv; i++)
		table[i] = 900000;

	return i;
}

static struct vclk_dfs_ops dfsbig_dfsops = {
	.set_ema = dfsbig_set_ema,
	.init_smpl = dfsbig_init_smpl,
	.set_smpl = dfsbig_set_smpl,
	.get_smpl = dfsbig_get_smpl,
	.get_rate_table = dfsbig_get_rate_table,
	.get_asv_table = dfsbig_asv_voltage_table,
	.get_margin_param = common_get_margin_param,
};


static int dfslittle_set_ema(unsigned int volt)
{
	pwrcal_writel(APOLLO_EMA_CON, 0x00000492);
	return 0;
}

static int dfslittle_get_rate_table(unsigned long *table)
{
	return dfs_get_rate_table(&dfslittle_table, table);
}

static int dfslittle_asv_voltage_table(unsigned int *table)
{
	int i;

	for (i = 0; i < dfslittle_table.num_of_lv; i++)
		table[i] = 900000;

	return i;
}

static int dfslittle_init_smpl(void)
{
	pwrcal_setf(PWR_CTRL4_APOLLO, 4, 0x3F, 7);
	pwrcal_setf(PWR_CTRL4_APOLLO, 0, 0x3, 3);
	return 0;
}

static int dfslittle_set_smpl(void)
{
	pwrcal_setf(PWR_CTRL4_APOLLO, 0, 0x3, 0);
	pwrcal_setf(PWR_CTRL4_APOLLO, 0, 0x3, 3);
	return 0;
}

static int dfslittle_get_smpl(void)
{
	return pwrcal_getbit(PWR_CTRL4_APOLLO, 12);
}

static struct vclk_dfs_ops dfslittle_dfsops = {
	.set_ema = dfslittle_set_ema,
	.init_smpl = dfslittle_init_smpl,
	.set_smpl = dfslittle_set_smpl,
	.get_smpl = dfslittle_get_smpl,
	.get_rate_table = dfslittle_get_rate_table,
	.get_asv_table = dfslittle_asv_voltage_table,
	.get_margin_param = common_get_margin_param,
};

static int dfsg3d_dvs(int command)
{
	unsigned int timeout = 0;

	if (command == 1) {
		pwrcal_setbit(GPU_DVS_CTRL, 1, 1);
		while (pwrcal_getbit(GPU_DVS_STATUS, 0) != 0) {
			timeout++;
			if (timeout > 100000)
				return -1;
			cpu_relax();
		}
	} else if (command == 0) {
		pwrcal_setbit(GPU_DVS_CTRL, 1, 0);
		while (pwrcal_getbit(GPU_DVS_STATUS, 0) != 1) {
			timeout++;
			if (timeout > 100000)
				return -1;
			cpu_relax();
		}
	} else if (command == 2) {
		pwrcal_writel(GPU_DVS_COUNTER, 0x09241964);
		pwrcal_writel(GPU_DVS_CLK_CTRL, 0x00000001);
		pwrcal_writel(GPU_DVS_CTRL, 0x00000005);
	} else {
		return -1;
	}

	return 0;
}


static int dfsg3d_get_rate_table(unsigned long *table)
{
	return dfs_get_rate_table(&dfsg3d_table, table);
}

static int dfsg3d_asv_voltage_table(unsigned int *table)
{
	int i;

	for (i = 0; i < dfsg3d_table.num_of_lv; i++)
		table[i] = 900000;

	return i;
}

static struct vclk_dfs_ops dfsg3d_dfsops = {
	.dvs = dfsg3d_dvs,
	.get_rate_table = dfsg3d_get_rate_table,
	.get_asv_table = dfsg3d_asv_voltage_table,
	.get_margin_param = common_get_margin_param,
};


static int dfsmif_set_ema(unsigned int volt)
{
	return 0;
}

static int dfsmif_get_rate_table(unsigned long *table)
{
	return dfs_get_rate_table(&dfsmif_table, table);
}

static int dfsmif_is_dll_on(void)
{
	return 0;
}

extern void dmc_misc_direct_dmc_enable(int enable);
extern void pwrcal_dmc_set_dvfs(unsigned long long target_mif_freq, unsigned int timing_set_idx);

void dfsmif_trans_pre(unsigned int rate_from, unsigned int rate_to)
{
	dmc_misc_direct_dmc_enable(true);
}

void dfsmif_trans_post(unsigned int rate_from, unsigned int rate_to)
{
	dmc_misc_direct_dmc_enable(false);
}

void dfsmif_switch_pre(unsigned int rate_from, unsigned int rate_to)
{
	static unsigned int paraset;
	unsigned long long rate;

	paraset = (paraset + 1) % 2;
	rate = (unsigned long long)rate_to * 1000;
	pwrcal_dmc_set_dvfs(rate, paraset);
}

void dfsmif_switch_post(unsigned int rate_from, unsigned int rate_to)
{
}

static int dfsmif_asv_voltage_table(unsigned int *table)
{
	int i;

	for (i = 0; i < dfsmif_table.num_of_lv; i++)
		table[i] = 900000;

	return i;
}

static struct vclk_dfs_ops dfsmif_dfsops = {
	.set_ema = dfsmif_set_ema,
	.get_rate_table = dfsmif_get_rate_table,
	.is_dll_on = dfsmif_is_dll_on,
	.get_asv_table = dfsmif_asv_voltage_table,
	.get_margin_param = common_get_margin_param,
};

static int dfsint_set_ema(unsigned int volt)
{
	return 0;
}

int dfsint_get_target_rate(char *member, unsigned long rate)
{
	static struct target_member {
		char *name;
		unsigned int mux;
		unsigned int div;
	} target_list[] = {
		{"ACLK_BUS0_528",	TOP_MUX_ACLK_BUS0_528,	TOP_DIV_ACLK_BUS0_528},
		{"ACLK_BUS1_528",	TOP_MUX_ACLK_BUS1_528,	TOP_DIV_ACLK_BUS1_528},
		{"ACLK_BUS0_200",	TOP_MUX_ACLK_BUS0_200,	TOP_DIV_ACLK_BUS0_200},
		{"PCLK_BUS0_132",	TOP_MUX_PCLK_BUS0_132,	TOP_DIV_PCLK_BUS0_132},
		{"PCLK_BUS1_132",	TOP_MUX_PCLK_BUS1_132,	TOP_DIV_PCLK_BUS1_132},
		{"ACLK_IMEM_266",	TOP_MUX_ACLK_IMEM_266,	TOP_DIV_ACLK_IMEM_266},
		{"ACLK_IMEM_200",	TOP_MUX_ACLK_IMEM_200,	TOP_DIV_ACLK_IMEM_200},
		{"ACLK_IMEM_100",	TOP_MUX_ACLK_IMEM_100,	TOP_DIV_ACLK_IMEM_100},
		{"ACLK_MFC_600",	TOP_MUX_ACLK_MFC_600,	TOP_DIV_ACLK_MFC_600},
		{"ACLK_MSCL0_528",	TOP_MUX_ACLK_MSCL0_528,	TOP_DIV_ACLK_MSCL0_528},
		{"ACLK_MSCL1_528",	TOP_MUX_ACLK_MSCL1_528,	TOP_DIV_ACLK_MSCL1_528},
		{"ACLK_PERIS_66",	TOP_MUX_ACLK_PERIS_66,	TOP_DIV_ACLK_PERIS_66},
		{"ACLK_FSYS0_200",	TOP_MUX_ACLK_FSYS0_200,	TOP_DIV_ACLK_FSYS0_200},
		{"ACLK_FSYS1_200",	TOP_MUX_ACLK_FSYS1_200,	TOP_DIV_ACLK_FSYS1_200},
		{"ACLK_PERIC0_66",	TOP_MUX_ACLK_PERIC0_66,	TOP_DIV_ACLK_PERIC0_66},
		{"ACLK_PERIC1_66",	TOP_MUX_ACLK_PERIC1_66,	TOP_DIV_ACLK_PERIC1_66},
		{"ACLK_ISP0_TREX_528",	TOP_MUX_ACLK_ISP0_TREX_528,	TOP_DIV_ACLK_ISP0_TREX_528},
		{"ACLK_ISP0_ISP0_528",	TOP_MUX_ACLK_ISP0_ISP0_528,	TOP_DIV_ACLK_ISP0_ISP0_528},
		{"ACLK_ISP0_TPU_400",	TOP_MUX_ACLK_ISP0_TPU_400,	TOP_DIV_ACLK_ISP0_TPU_400},
		{"ACLK_ISP1_ISP1_468",	TOP_MUX_ACLK_ISP1_ISP1_468,	TOP_DIV_ACLK_ISP1_ISP1_468},
		{"ACLK_CAM1_ARM_672",	TOP_MUX_ACLK_CAM1_ARM_672,	TOP_DIV_ACLK_CAM1_ARM_672},
		{"ACLK_CAM1_TREX_VRA_528",	TOP_MUX_ACLK_CAM1_TREX_VRA_528,	TOP_DIV_ACLK_CAM1_TREX_VRA_528},
		{"ACLK_CAM1_TREX_B_528",	TOP_MUX_ACLK_CAM1_TREX_B_528,	TOP_DIV_ACLK_CAM1_TREX_B_528},
		{"ACLK_CAM1_BUS_264",	TOP_MUX_ACLK_CAM1_BUS_264,	TOP_DIV_ACLK_CAM1_BUS_264},
		{"ACLK_CAM1_PERI_84",	TOP_MUX_ACLK_CAM1_PERI_84,	TOP_DIV_ACLK_CAM1_PERI_84},
		{"ACLK_CAM1_CSIS2_414",	TOP_MUX_ACLK_CAM1_CSIS2_414,	TOP_DIV_ACLK_CAM1_CSIS2_414},
		{"ACLK_CAM1_CSIS3_132",	TOP_MUX_ACLK_CAM1_CSIS3_132,	TOP_DIV_ACLK_CAM1_CSIS3_132},
		{"ACLK_CAM1_SCL_566",	TOP_MUX_ACLK_CAM1_SCL_566,	TOP_DIV_ACLK_CAM1_SCL_566},
	};
	int target = -1;
	unsigned long represent[32];
	unsigned long rates[32];
	int i, max;

	for (i = 0; i < ARRAY_SIZE(target_list); i++)
		if (strcmp(member, target_list[i].name) == 0) {
			target = i;
			break;
		}

	if (target == -1)
		return 0;

	max = dfs_get_rate_table(&dfsint_table, represent);
	if (max != dfs_get_target_rate_table(&dfsint_table, target_list[target].mux, target_list[target].div, rates))
		return 0;

	for (i = max - 1; i >= 0; i--) {
		if (rates[i] >= rate)
			return represent[i];
	}
	return 0;
}


static int dfsint_get_rate_table(unsigned long *table)
{
	return dfs_get_rate_table(&dfsint_table, table);
}

static int dfsint_asv_voltage_table(unsigned int *table)
{
	int i;

	for (i = 0; i < dfsint_table.num_of_lv; i++)
		table[i] = 900000;

	return i;
}

static struct vclk_dfs_ops dfsint_dfsops = {
	.set_ema = dfsint_set_ema,
	.get_target_rate = dfsint_get_target_rate,
	.get_rate_table = dfsint_get_rate_table,
	.get_asv_table = dfsint_asv_voltage_table,
	.get_margin_param = common_get_margin_param,
};

static int dfscam_set_ema(unsigned int volt)
{
	return 0;
}

static int dfscam_get_rate_table(unsigned long *table)
{
	return dfs_get_rate_table(&dfscam_table, table);
}

static int dfscam_asv_voltage_table(unsigned int *table)
{
	int i;

	for (i = 0; i < dfscam_table.num_of_lv; i++)
		table[i] = 900000;

	return i;
}

static struct vclk_dfs_ops dfscam_dfsops = {
	.set_ema = dfscam_set_ema,
	.get_rate_table = dfscam_get_rate_table,
	.get_asv_table = dfscam_asv_voltage_table,
	.get_margin_param = common_get_margin_param,
};

static int dfsdisp_set_ema(unsigned int volt)
{
	return 0;
}

static int dfsdisp_get_rate_table(unsigned long *table)
{
	return dfs_get_rate_table(&dfsdisp_table, table);
}

static int dfsdisp_asv_voltage_table(unsigned int *table)
{
	int i;

	for (i = 0; i < dfsdisp_table.num_of_lv; i++)
		table[i] = 900000;

	return i;
}

static struct vclk_dfs_ops dfsdisp_dfsops = {
	.set_ema = dfsdisp_set_ema,
	.get_rate_table = dfsdisp_get_rate_table,
	.get_asv_table = dfsdisp_asv_voltage_table,
	.get_margin_param = common_get_margin_param,
};

static DEFINE_SPINLOCK(dvfs_big_lock);
static DEFINE_SPINLOCK(dvfs_little_lock);
static DEFINE_SPINLOCK(dvfs_g3d_lock);
static DEFINE_SPINLOCK(dvfs_mif_lock);
static DEFINE_SPINLOCK(dvfs_int_lock);
static DEFINE_SPINLOCK(dvfs_disp_lock);
static DEFINE_SPINLOCK(dvfs_cam_lock);

DFS(dvfs_big) = {
	.vclk.type	= vclk_group_dfs,
	.vclk.parent	= VCLK(pxmxdx_top),
	.vclk.ref_count	= 1,
	.vclk.vfreq	= 0,
	.vclk.name	= "dvfs_big",
	.vclk.ops	= &dfs_ops,
	.lock		= &dvfs_big_lock,
	.clks		= dfsbig_dfsclkgrp,
	.en_clks		= dfsbig_en_list,
	.table		= &dfsbig_table,
	.dfsops		= &dfsbig_dfsops,
};

DFS(dvfs_little) = {
	.vclk.type	= vclk_group_dfs,
	.vclk.parent	= VCLK(pxmxdx_top),
	.vclk.ref_count	= 1,
	.vclk.vfreq	= 0,
	.vclk.name	= "dvfs_little",
	.vclk.ops	= &dfs_ops,
	.lock		= &dvfs_little_lock,
	.clks		= dfslittle_dfsclkgrp,
	.en_clks		= dfslittle_en_list,
	.table		= &dfslittle_table,
	.dfsops		= &dfslittle_dfsops,
};

DFS(dvfs_g3d) = {
	.vclk.type	= vclk_group_dfs,
	.vclk.parent	= VCLK(pxmxdx_top),
	.vclk.ref_count	= 0,
	.vclk.vfreq	= 0,
	.vclk.name	= "dvfs_g3d",
	.vclk.ops	= &dfs_ops,
	.lock		= &dvfs_g3d_lock,
	.clks		= dfsg3d_dfsclkgrp,
	.en_clks		= dfsg3d_en_list,
	.table		= &dfsg3d_table,
	.dfsops		= &dfsg3d_dfsops,
};

DFS(dvfs_mif) = {
	.vclk.type	= vclk_group_dfs,
	.vclk.parent	= VCLK(pxmxdx_top),
	.vclk.ref_count	= 1,
	.vclk.vfreq	= 0,
	.vclk.name	= "dvfs_mif",
	.vclk.ops	= &dfs_ops,
	.lock		= &dvfs_mif_lock,
	.clks		= dfsmif_dfsclkgrp,
	.en_clks		= dfsmif_en_list,
	.table		= &dfsmif_table,
	.dfsops		= &dfsmif_dfsops,
};

DFS(dvfs_int) = {
	.vclk.type	= vclk_group_dfs,
	.vclk.parent	= VCLK(pxmxdx_top),
	.vclk.ref_count	= 1,
	.vclk.vfreq	= 0,
	.vclk.name	= "dvfs_int",
	.vclk.ops	= &dfs_ops,
	.lock		= &dvfs_int_lock,
	.clks		= dfsint_dfsclkgrp,
	.en_clks		= dfsint_en_list,
	.table		= &dfsint_table,
	.dfsops		= &dfsint_dfsops,
};

DFS(dvfs_cam) = {
	.vclk.type	= vclk_group_dfs,
	.vclk.parent	= VCLK(pxmxdx_top),
	.vclk.ref_count	= 0,
	.vclk.vfreq	= 0,
	.vclk.name	= "dvfs_cam",
	.vclk.ops	= &dfs_ops,
	.lock		= &dvfs_cam_lock,
	.clks		= dfscam_dfsclkgrp,
	.en_clks		= dfscam_en_list,
	.table		= &dfscam_table,
	.dfsops		= &dfscam_dfsops,
};

DFS(dvfs_disp) = {
	.vclk.type	= vclk_group_dfs,
	.vclk.parent	= VCLK(pxmxdx_top),
	.vclk.ref_count	= 1,
	.vclk.vfreq	= 0,
	.vclk.name	= "dvfs_disp",
	.vclk.ops	= &dfs_ops,
	.lock		= &dvfs_disp_lock,
	.clks		= dfsdisp_dfsclkgrp,
	.table		= &dfsdisp_table,
	.dfsops		= &dfsdisp_dfsops,
};


void dfs_set_clk_information(struct pwrcal_vclk_dfs *dfs)
{
	int i, j;
	void *dvfs_block;
	struct ect_dvfs_domain *dvfs_domain;
	struct dfs_table *dvfs_table;

	dvfs_block = ect_get_block("DVFS");
	if (dvfs_block == NULL)
		return;

	dvfs_domain = ect_dvfs_get_domain(dvfs_block, dfs->vclk.name);
	if (dvfs_domain == NULL)
		return;

	dvfs_table = dfs->table;
	dvfs_table->num_of_lv = dvfs_domain->num_of_level;
	dvfs_table->num_of_members = dvfs_domain->num_of_clock + 1;
	dvfs_table->max_freq = dvfs_domain->max_frequency;
	dvfs_table->min_freq = dvfs_domain->min_frequency;

	dvfs_table->members = kzalloc(sizeof(struct pwrcal_clk *) * (dvfs_domain->num_of_clock + 1), GFP_KERNEL);
	if (dvfs_table->members == NULL)
		return;

	dvfs_table->members[0] = REPRESENT_RATE;
	for (i = 0; i < dvfs_domain->num_of_clock; ++i) {
		dvfs_table->members[i + 1] = clk_find(dvfs_domain->list_clock[i]);
		if (dvfs_table->members[i] == NULL)
			return;
	}

	dvfs_table->rate_table = kzalloc(sizeof(unsigned int) * (dvfs_domain->num_of_clock + 1) * dvfs_domain->num_of_level, GFP_KERNEL);
	if (dvfs_table->rate_table == NULL)
		return;

	for (i = 0; i < dvfs_domain->num_of_level; ++i) {

		dvfs_table->rate_table[i * (dvfs_domain->num_of_clock + 1)] = dvfs_domain->list_level[i].level;
		for (j = 0; j <= dvfs_domain->num_of_clock; ++j) {
			dvfs_table->rate_table[i * (dvfs_domain->num_of_clock + 1) + j + 1] =
				dvfs_domain->list_dvfs_value[i * dvfs_domain->num_of_clock + j];
		}
	}

}

void dfs_set_pscdc_information(void)
{
	int i;
	void *gen_block;
	struct ect_gen_param_table *pscdc;

	gen_block = ect_get_block("GEN");
	if (gen_block == NULL)
		return;

	pscdc = ect_gen_param_get_table(gen_block, "PSCDC");
	if (pscdc == NULL)
		return;

	rate_table_aclk_ccore_800 = kzalloc(sizeof(struct aclk_ccore_800_table) * (pscdc->num_of_row), GFP_KERNEL);
	if (rate_table_aclk_ccore_800 == NULL)
		return;

	for (i = 0; i < pscdc->num_of_row; i++) {
		rate_table_aclk_ccore_800[i].rate = pscdc->parameter[i * pscdc->num_of_col + 0];
		rate_table_aclk_ccore_800[i].mux = pscdc->parameter[i * pscdc->num_of_col + 1];
		rate_table_aclk_ccore_800[i].div = pscdc->parameter[i * pscdc->num_of_col + 2];
		rate_table_aclk_ccore_800[i].sci_ratio = pscdc->parameter[i * pscdc->num_of_col + 3];
		rate_table_aclk_ccore_800[i].smc_ratio = pscdc->parameter[i * pscdc->num_of_col + 4];
	}
}

void dfs_init(void)
{
	dfs_set_clk_information(&vclk_dvfs_big);
	dfs_set_clk_information(&vclk_dvfs_little);
	dfs_set_clk_information(&vclk_dvfs_g3d);
	dfs_set_clk_information(&vclk_dvfs_mif);
	dfs_set_clk_information(&vclk_dvfs_int);
	dfs_set_clk_information(&vclk_dvfs_cam);
	dfs_set_clk_information(&vclk_dvfs_disp);

	dfs_dram_init();
	dfs_set_pscdc_information();
}
