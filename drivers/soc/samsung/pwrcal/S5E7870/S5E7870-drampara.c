#include "../pwrcal-env.h"
#include "../pwrcal-rae.h"
#include "S5E7870-sfrbase.h"
#include "S5E7870-vclk-internal.h"
#include "S5E7870-pmusfr.h"

#ifdef PWRCAL_TARGET_LINUX
#include <soc/samsung/ect_parser.h>
#else
#include <mach/ect_parser.h>
#endif

#ifndef MHZ
#define MHZ		((unsigned long long)1000000)
#endif

#define DMC_WAIT_CNT 10000

#if 1 /// joshua용으로 수정 필요

#define DREX0_MEMORY_CONTROL0			((void *)(DREX0_BASE + 0x0010))
#define DREX0_POWER_DOWN_CONFIG			((void *)(DREX0_BASE + 0x0014))
#define DREX0_CG_CONTROL			((void *)(DREX0_BASE + 0x0018))

#define DREX0_TICK_GRANULARITY_S0		((void *)(DREX0_BASE + 0x0100))
#define DREX0_TEMP_SENSING_S0					((void *)(DREX0_BASE + 0x0108))
#define DREX0_DQS_OSC_CON1_S0					((void *)(DREX0_BASE + 0x0110))
#define DREX0_TERMINATION_CONTROL_S0			((void *)(DREX0_BASE + 0x0114))
#define DREX0_WINCONFIG_WRITE_ODT_S0			((void *)(DREX0_BASE + 0x0118))
#define DREX0_TIMING_ROW0_S0		((void *)(DREX0_BASE + 0x0140))
#define DREX0_TIMING_ROW1_S0		((void *)(DREX0_BASE + 0x0144))
#define DREX0_TIMING_DATA_ACLK_S0		((void *)(DREX0_BASE + 0x0148))
#define DREX0_TIMING_DATA_MCLK_S0			((void *)(DREX0_BASE + 0x014C))
#define DREX0_TIMING_POWER0_S0		((void *)(DREX0_BASE + 0x0150))
#define DREX0_TIMING_POWER1_S0		((void *)(DREX0_BASE + 0x0154))
#define DREX0_TIMING_ETC1_S0	((void *)(DREX0_BASE + 0x015c))

#define DREX0_TICK_GRANULARITY_S1		((void *)(DREX0_BASE + 0x0180))
#define DREX0_TEMP_SENSING_S1					((void *)(DREX0_BASE + 0x0188))
#define DREX0_DQS_OSC_CON1_S1					((void *)(DREX0_BASE + 0x0190))
#define DREX0_TERMINATION_CONTROL_S1			((void *)(DREX0_BASE + 0x0194))
#define DREX0_WINCONFIG_WRITE_ODT_S1			((void *)(DREX0_BASE + 0x0198))
#define DREX0_TIMING_ROW0_S1		((void *)(DREX0_BASE + 0x01c0))
#define DREX0_TIMING_ROW1_S1		((void *)(DREX0_BASE + 0x01c4))
#define DREX0_TIMING_DATA_ACLK_S1		((void *)(DREX0_BASE + 0x01c8))
#define DREX0_TIMING_DATA_MCLK_S1			((void *)(DREX0_BASE + 0x01cC))
#define DREX0_TIMING_POWER0_S1		((void *)(DREX0_BASE + 0x01d0))
#define DREX0_TIMING_POWER1_S1		((void *)(DREX0_BASE + 0x01d4))
#define DREX0_TIMING_ETC1_S1	((void *)(DREX0_BASE + 0x01dc))

#define PHY0_CAL_CON0		((void *)(DREXPHY0_BASE + 0x0004))
#define PHY0_DVFS_CON0		((void *)(DREXPHY0_BASE + 0x00B8))
#define PHY0_DVFS_CON1		((void *)(DREXPHY0_BASE + 0x00E0))
#define PHY0_DVFS_CON2		((void *)(DREXPHY0_BASE + 0x00BC))
#define PHY0_DVFS_CON3		((void *)(DREXPHY0_BASE + 0x00C0))
#define PHY0_DVFS_CON4		((void *)(DREXPHY0_BASE + 0x00C4))
#define PHY0_DVFS_CON5		((void *)(DREXPHY0_BASE + 0x00C8))
#define PHY0_DVFS_CON6		((void *)(DREXPHY0_BASE + 0x00CC))
#define PHY0_ZQ_CON9	((void *)(DREXPHY0_BASE + 0x03EC))

#define DREX0_PAUSE_MRS0	((void *)(DREX0_BASE + 0x0080))
#define DREX0_PAUSE_MRS1	((void *)(DREX0_BASE + 0x0084))
#define DREX0_PAUSE_MRS2	((void *)(DREX0_BASE + 0x0088))
#define DREX0_PAUSE_MRS3	((void *)(DREX0_BASE + 0x008C))
#define DREX0_PAUSE_MRS4	((void *)(DREX0_BASE + 0x0090))

#define DREX0_TIMING_SET_SW	((void *)(DREX0_BASE + 0x0020))

#define DREX0_PORT_TICK_GRANULARITY_S0	((void *)(DREX0_PF_BASE + 0x0010))
#define DREX0_PORT_TICK_GRANULARITY_S1	((void *)(DREX0_PF_BASE + 0x0014))

// PHY DVFS CON SFR BIT DEFINITION /
#define PHY_DVFS_CON0_SET1_MASK	((0x0)|(1<<31)|(1<<29)|(0x0<<24))
#define PHY_DVFS_CON0_SET0_MASK	((0x0)|(1<<30)|(1<<28)|(0x0<<24)|(0x7<<21)|(0x7<<18)|(0x7<<15)|(0x7<<12)|(0x7<<9)|(0x7<<6)|(0x7<<3)|(0x7<<0))
#define PHY_DVFS_CON0_DVFS_MODE_MASK	((0x0)|(0x3<<24))
#define PHY_DVFS_CON0_DVFS_MODE_POSITION	24

#define PHY_DVFS_CON1_SET1_MASK	((0x7<<21)|(0x7<<18)|(0x7<<15)|(0x7<<12)|(0x7<<9)|(0x7<<6)|(0x7<<3)|(0x7<<0))
#define PHY_DVFS_CON1_SET0_MASK	((0x0))
#define PHY_DVFS_CON2_SET1_MASK	((0x0)|(0x1<<31)|(0x1F<<24)|(0xF<<12)|(0xF<<8))
#define PHY_DVFS_CON2_SET0_MASK	((0x0)|(0x1<<30)|(0x1F<<16)|(0xF<<7)|(0xF<<0))
#define PHY_DVFS_CON3_SET1_MASK	((0x0)|(0x1<<30)|(0x1<<29)|(0x7<<23)|(0x1<<17)|(0xf<<12)|(0xf<<8))
#define PHY_DVFS_CON3_SET0_MASK	((0x0)|(0x1<<31)|(0x1<<28)|(0x7<<20)|(0x1<<16)|(0xf<<4)|(0xf<<0))
#define PHY_DVFS_CON4_SET1_MASK	((0x0)|(0x3F<<18)|(0x3F<<12))
#define PHY_DVFS_CON4_SET0_MASK	((0x0)|(0x3F<<6)|(0x3F<<0))
#define PHY_DVFS_CON5_SET1_MASK	((0x0)|(0x7<<24)|(0x7<<16)|(0x7<<8)|(0x7<<0))
#define PHY_DVFS_CON5_SET0_MASK	((0x0))
#define PHY_DVFS_CON6_SET1_MASK	((0x0))
#define PHY_DVFS_CON6_SET0_MASK	((0x0)|(0x7<<24)|(0x7<<16)|(0x7<<8)|(0x7<<0))

#define DREX0_DIRECTCMD		((void *)(DREX0_BASE + 0x001C))


#define DREX0_timing_set_sw_con 0 /* Pause triggered from CMU */

#define DQS_OSC_UPDATE_EN 0
#define PERIODIC_WR_TRAIN 0

#if DQS_OSC_UPDATE_EN
#define dvfs_dqs_osc_en 1
#else
#define dvfs_dqs_osc_en 0
#endif

#if PERIODIC_WR_TRAIN
#define dvfs_offset 16
#else
#define dvfs_offset 0
#endif


enum dmc_dvfs_mif_level_idx {
	DMC_DVFS_MIF_L0,
	DMC_DVFS_MIF_L1,
	DMC_DVFS_MIF_L2,
	DMC_DVFS_MIF_L3,
	DMC_DVFS_MIF_L4,
	DMC_DVFS_MIF_L5,
	DMC_DVFS_MIF_L6,
	DMC_DVFS_MIF_L7,
	DMC_DVFS_MIF_L8,
	COUNT_OF_CMU_DVFS_MIF_LEVEL,
	CMU_DVFS_MIF_INVALID = 0xFF,
};

enum dmc_timing_set_idx {
	DMC_TIMING_SET_0 = 0,
	DMC_TIMING_SET_1
};

enum phy_timing_set_idx {
	PHY_Normal_mode = 0 ,
	PHY_DVFS0_mode,
	PHY_DVFS1_mode
};

enum timing_parameter_column {
	drex_Tick_Granularity,
	drex_mr4_sensing_cyc,
	drex_dqs_osc_start_cyc,
	drex_Termination_Control,
	drex_Winconfig_Write_Odt,
	drex_Timing_Row0,
	drex_Timing_Row1,
	drex_Timing_Data_Aclk,
	drex_Timing_Data_Mclk,
	drex_Timing_Power0,
	drex_Timing_Power1,
	drex_Etcl,
	drex_Puase_MRS0,
	drex_Puase_MRS1,
	drex_Puase_MRS2,
	drex_Puase_MRS3,
	drex_Puase_MRS4,
	phy_Dvfs_Con0_set1,
	phy_Dvfs_Con0_set0,
	phy_Dvfs_Con0_set1_mask,
	phy_Dvfs_Con0_set0_mask,
	phy_Dvfs_Con1_set1,
	phy_Dvfs_Con1_set0,
	phy_Dvfs_Con1_set1_mask,
	phy_Dvfs_Con1_set0_mask,
	phy_Dvfs_Con2_set1,
	phy_Dvfs_Con2_set0,
	phy_Dvfs_Con2_set1_mask,
	phy_Dvfs_Con2_set0_mask,
	phy_Dvfs_Con3_set1,
	phy_Dvfs_Con3_set0,
	phy_Dvfs_Con3_set1_mask,
	phy_Dvfs_Con3_set0_mask,
	num_of_g_dmc_drex_dfs_mif_table_column = drex_Etcl - drex_Tick_Granularity + 1,
	num_of_g_dmc_directcmd_dfs_mif_table_column = drex_Puase_MRS4 - drex_Puase_MRS0 + 1,
	num_of_g_dmc_phy_dfs_mif_table_column = phy_Dvfs_Con3_set0_mask - phy_Dvfs_Con0_set1 + 1,
	num_of_dram_parameter = num_of_g_dmc_drex_dfs_mif_table_column + num_of_g_dmc_directcmd_dfs_mif_table_column + num_of_g_dmc_phy_dfs_mif_table_column,
};

struct dmc_drex_dfs_mif_table {
	unsigned int drex_Tick_Granularity;
	unsigned int drex_mr4_sensing_cyc;
	unsigned int drex_dqs_osc_start_cyc;
	unsigned int drex_Termination_Control;
	unsigned int drex_Winconfig_Write_Odt;
	unsigned int drex_Timing_Row0;
	unsigned int drex_Timing_Row1;
	unsigned int drex_Timing_Data_Aclk;
	unsigned int drex_Timing_Data_Mclk;
	unsigned int drex_Timing_Power0;
	unsigned int drex_Timing_Power1;
	unsigned int drex_Etcl;
};


struct dmc_directcmd_dfs_mif_table {
	unsigned int drex_Puase_MRS0;
	unsigned int drex_Puase_MRS1;
	unsigned int drex_Puase_MRS2;
	unsigned int drex_Puase_MRS3;
	unsigned int drex_Puase_MRS4;
};

struct dmc_phy_dfs_mif_table {
	unsigned int phy_Dvfs_Con0_set1;
	unsigned int phy_Dvfs_Con0_set0;
	unsigned int phy_Dvfs_Con0_set1_mask;
	unsigned int phy_Dvfs_Con0_set0_mask;
	unsigned int phy_Dvfs_Con1_set1;
	unsigned int phy_Dvfs_Con1_set0;
	unsigned int phy_Dvfs_Con1_set1_mask;
	unsigned int phy_Dvfs_Con1_set0_mask;
	unsigned int phy_Dvfs_Con2_set1;
	unsigned int phy_Dvfs_Con2_set0;
	unsigned int phy_Dvfs_Con2_set1_mask;
	unsigned int phy_Dvfs_Con2_set0_mask;
	unsigned int phy_Dvfs_Con3_set1;
	unsigned int phy_Dvfs_Con3_set0;
	unsigned int phy_Dvfs_Con3_set1_mask;
	unsigned int phy_Dvfs_Con3_set0_mask;
	unsigned int phy_Dvfs_Con4_set1;
	unsigned int phy_Dvfs_Con4_set0;
	unsigned int phy_Dvfs_Con4_set1_mask;
	unsigned int phy_Dvfs_Con4_set0_mask;
	unsigned int phy_Dvfs_Con5_set1;
	unsigned int phy_Dvfs_Con5_set0;
	unsigned int phy_Dvfs_Con5_set1_mask;
	unsigned int phy_Dvfs_Con5_set0_mask;
	unsigned int phy_Dvfs_Con6_set1;
	unsigned int phy_Dvfs_Con6_set0;
	unsigned int phy_Dvfs_Con6_set1_mask;
	unsigned int phy_Dvfs_Con6_set0_mask;
};

enum dmc_dvfs_mif_switching_level_idx {
	DMC_DVFS_MIF__switching_L0,
	DMC_DVFS_MIF__switching_L1,
};

#define LP4_RL 12
#define LP4_WL 6
#define LP4_RL_L10 6
#define LP4_WL_L10 4
#define DRAM_RLWL 0x09
#define DRAM_RLWL_L10 0x00

static struct dmc_drex_dfs_mif_table *pwrcal_dfs_drex_mif_table;
static struct dmc_directcmd_dfs_mif_table *pwrcal_pause_directcmd_dfs_mif_table;
static struct dmc_phy_dfs_mif_table *pwrcal_dfs_phy_mif_table;


static unsigned long long *mif_freq_to_level;
static int num_mif_freq_to_level;


static unsigned int convert_to_level(unsigned long long freq)
{
	int idx;
	int tablesize = num_mif_freq_to_level;

	for (idx = tablesize - 1; idx >= 0; idx--)
		if (freq <= mif_freq_to_level[idx])
			return (unsigned int)idx;

	return 0;
}


void pwrcal_dmc_set_dvfs(unsigned long long target_mif_freq, unsigned int timing_set_idx)
{
	unsigned int uReg;
//	unsigned int soc_vref[4];
	unsigned int target_mif_level_idx, target_mif_level_switch_idx;
	unsigned int offset;


	target_mif_level_idx = convert_to_level(target_mif_freq);
	target_mif_level_switch_idx = convert_to_level(target_mif_freq);

	if (timing_set_idx == DMC_TIMING_SET_0) {
		for (offset = 0; offset < 0x100000; offset += 0x100000) {

			/* Phy & DREX Mode setting */
			if (target_mif_level_idx != 0) {
				uReg = pwrcal_readl(offset + PHY0_DVFS_CON0);
				uReg &= ~(PHY_DVFS_CON0_DVFS_MODE_MASK);
				uReg |= (PHY_DVFS0_mode<<PHY_DVFS_CON0_DVFS_MODE_POSITION);
				pwrcal_writel(offset + PHY0_DVFS_CON0, uReg);
			} else {
				uReg = pwrcal_readl(offset + PHY0_DVFS_CON0);
				uReg &= ~(PHY_DVFS_CON0_DVFS_MODE_MASK);
				uReg |= (PHY_Normal_mode<<PHY_DVFS_CON0_DVFS_MODE_POSITION);
				pwrcal_writel(offset + PHY0_DVFS_CON0, uReg);
			}

			pwrcal_writel(offset + DREX0_TIMING_SET_SW, DREX0_timing_set_sw_con);
			pwrcal_writel((void *)CMU_MIF_BASE + 0x1004,  DMC_TIMING_SET_0); /*cmu pause setting */

			#define PHY_DVFS_CON0_DVFS_MODE_MASK	((0x0)|(0x3<<24))
			#define PHY_DVFS_CON0_DVFS_MODE_POSITION	24

			pwrcal_writel(offset + DREX0_TICK_GRANULARITY_S0, pwrcal_dfs_drex_mif_table[target_mif_level_idx].drex_Tick_Granularity);
			pwrcal_writel(offset + DREX0_PORT_TICK_GRANULARITY_S0, pwrcal_dfs_drex_mif_table[target_mif_level_idx].drex_Tick_Granularity);
			pwrcal_writel(offset + DREX0_TEMP_SENSING_S0, pwrcal_dfs_drex_mif_table[target_mif_level_idx].drex_mr4_sensing_cyc);
			pwrcal_writel(offset + DREX0_DQS_OSC_CON1_S0, pwrcal_dfs_drex_mif_table[target_mif_level_idx].drex_dqs_osc_start_cyc);
			pwrcal_writel(offset + DREX0_TERMINATION_CONTROL_S0, pwrcal_dfs_drex_mif_table[target_mif_level_idx].drex_Termination_Control);
			pwrcal_writel(offset + DREX0_WINCONFIG_WRITE_ODT_S0, pwrcal_dfs_drex_mif_table[target_mif_level_idx].drex_Winconfig_Write_Odt);
			pwrcal_writel(offset + DREX0_TIMING_ROW0_S0, pwrcal_dfs_drex_mif_table[target_mif_level_idx].drex_Timing_Row0);
			pwrcal_writel(offset + DREX0_TIMING_ROW1_S0, pwrcal_dfs_drex_mif_table[target_mif_level_idx].drex_Timing_Row1);
			pwrcal_writel(offset + DREX0_TIMING_DATA_ACLK_S0, pwrcal_dfs_drex_mif_table[target_mif_level_idx].drex_Timing_Data_Aclk);
			pwrcal_writel(offset + DREX0_TIMING_DATA_MCLK_S0, pwrcal_dfs_drex_mif_table[target_mif_level_idx].drex_Timing_Data_Mclk);
			pwrcal_writel(offset + DREX0_TIMING_POWER0_S0, pwrcal_dfs_drex_mif_table[target_mif_level_idx].drex_Timing_Power0);
			pwrcal_writel(offset + DREX0_TIMING_POWER1_S0, pwrcal_dfs_drex_mif_table[target_mif_level_idx].drex_Timing_Power1);
			pwrcal_writel(offset + DREX0_TIMING_ETC1_S0, pwrcal_dfs_drex_mif_table[target_mif_level_idx].drex_Etcl);

			uReg = pwrcal_readl(offset + PHY0_DVFS_CON0);
			uReg &= ~(pwrcal_dfs_phy_mif_table[target_mif_level_idx].phy_Dvfs_Con0_set0_mask);
			uReg |= pwrcal_dfs_phy_mif_table[target_mif_level_idx].phy_Dvfs_Con0_set0;
			pwrcal_writel(offset + PHY0_DVFS_CON0, uReg);

			uReg = pwrcal_readl(offset + PHY0_DVFS_CON1);
			uReg &= ~(pwrcal_dfs_phy_mif_table[target_mif_level_idx].phy_Dvfs_Con1_set0_mask);
			uReg |= pwrcal_dfs_phy_mif_table[target_mif_level_idx].phy_Dvfs_Con1_set0;
			pwrcal_writel(offset + PHY0_DVFS_CON1, uReg);

			uReg = pwrcal_readl(offset + PHY0_DVFS_CON2);
			uReg &= ~(pwrcal_dfs_phy_mif_table[target_mif_level_idx].phy_Dvfs_Con2_set0_mask);
			uReg |= pwrcal_dfs_phy_mif_table[target_mif_level_idx].phy_Dvfs_Con2_set0;
			pwrcal_writel(offset + PHY0_DVFS_CON2, uReg);

			uReg = pwrcal_readl(offset + PHY0_DVFS_CON3);
			uReg &= ~(pwrcal_dfs_phy_mif_table[target_mif_level_idx].phy_Dvfs_Con3_set0_mask);
			uReg |= pwrcal_dfs_phy_mif_table[target_mif_level_idx].phy_Dvfs_Con3_set0;
			pwrcal_writel(offset + PHY0_DVFS_CON3, uReg);

			pwrcal_writel(offset + DREX0_PAUSE_MRS0, pwrcal_pause_directcmd_dfs_mif_table[target_mif_level_idx].drex_Puase_MRS0);
			pwrcal_writel(offset + DREX0_PAUSE_MRS1, pwrcal_pause_directcmd_dfs_mif_table[target_mif_level_idx].drex_Puase_MRS1);
			pwrcal_writel(offset + DREX0_PAUSE_MRS2, pwrcal_pause_directcmd_dfs_mif_table[target_mif_level_idx].drex_Puase_MRS2);
			pwrcal_writel(offset + DREX0_PAUSE_MRS3, pwrcal_pause_directcmd_dfs_mif_table[target_mif_level_idx].drex_Puase_MRS3);
			pwrcal_writel(offset + DREX0_PAUSE_MRS4, pwrcal_pause_directcmd_dfs_mif_table[target_mif_level_idx].drex_Puase_MRS4);
		}

	} else if (timing_set_idx == DMC_TIMING_SET_1) {
		for (offset = 0; offset < 0x100000; offset += 0x100000) {

			/* Phy & DREX Mode setting */
			uReg = pwrcal_readl(offset + PHY0_DVFS_CON0);
			uReg &= ~(PHY_DVFS_CON0_DVFS_MODE_MASK);
			uReg |= (PHY_DVFS1_mode<<PHY_DVFS_CON0_DVFS_MODE_POSITION);
			pwrcal_writel(offset + PHY0_DVFS_CON0, uReg);

			pwrcal_writel(offset + DREX0_TIMING_SET_SW,  DREX0_timing_set_sw_con);
			pwrcal_writel((void *)CMU_MIF_BASE + 0x1004,  DMC_TIMING_SET_1); /*cmu pause setting */

			pwrcal_writel(offset + DREX0_TICK_GRANULARITY_S1, pwrcal_dfs_drex_mif_table[target_mif_level_switch_idx].drex_Tick_Granularity);
			pwrcal_writel(offset + DREX0_PORT_TICK_GRANULARITY_S1, pwrcal_dfs_drex_mif_table[target_mif_level_switch_idx].drex_Tick_Granularity);
			pwrcal_writel(offset + DREX0_TEMP_SENSING_S1, pwrcal_dfs_drex_mif_table[target_mif_level_switch_idx].drex_mr4_sensing_cyc);
			pwrcal_writel(offset + DREX0_DQS_OSC_CON1_S1, pwrcal_dfs_drex_mif_table[target_mif_level_switch_idx].drex_dqs_osc_start_cyc);
			pwrcal_writel(offset + DREX0_TERMINATION_CONTROL_S1, pwrcal_dfs_drex_mif_table[target_mif_level_switch_idx].drex_Termination_Control);
			pwrcal_writel(offset + DREX0_WINCONFIG_WRITE_ODT_S1, pwrcal_dfs_drex_mif_table[target_mif_level_switch_idx].drex_Winconfig_Write_Odt);
			pwrcal_writel(offset + DREX0_TIMING_ROW0_S1, pwrcal_dfs_drex_mif_table[target_mif_level_switch_idx].drex_Timing_Row0);
			pwrcal_writel(offset + DREX0_TIMING_ROW1_S1, pwrcal_dfs_drex_mif_table[target_mif_level_switch_idx].drex_Timing_Row1);
			pwrcal_writel(offset + DREX0_TIMING_DATA_ACLK_S1, pwrcal_dfs_drex_mif_table[target_mif_level_switch_idx].drex_Timing_Data_Aclk);
			pwrcal_writel(offset + DREX0_TIMING_DATA_MCLK_S1, pwrcal_dfs_drex_mif_table[target_mif_level_switch_idx].drex_Timing_Data_Mclk);
			pwrcal_writel(offset + DREX0_TIMING_POWER0_S1, pwrcal_dfs_drex_mif_table[target_mif_level_switch_idx].drex_Timing_Power0);
			pwrcal_writel(offset + DREX0_TIMING_POWER1_S1, pwrcal_dfs_drex_mif_table[target_mif_level_switch_idx].drex_Timing_Power1);
			pwrcal_writel(offset + DREX0_TIMING_ETC1_S1, pwrcal_dfs_drex_mif_table[target_mif_level_switch_idx].drex_Etcl);

			uReg = pwrcal_readl(offset + PHY0_DVFS_CON0);
			uReg &= ~(pwrcal_dfs_phy_mif_table[target_mif_level_switch_idx].phy_Dvfs_Con0_set1_mask);
			uReg |= pwrcal_dfs_phy_mif_table[target_mif_level_switch_idx].phy_Dvfs_Con0_set1;
			pwrcal_writel(offset + PHY0_DVFS_CON0, uReg);

			uReg = pwrcal_readl(offset + PHY0_DVFS_CON1);
			uReg &= ~(pwrcal_dfs_phy_mif_table[target_mif_level_switch_idx].phy_Dvfs_Con1_set1_mask);
			uReg |= pwrcal_dfs_phy_mif_table[target_mif_level_switch_idx].phy_Dvfs_Con1_set1;
			pwrcal_writel(offset + PHY0_DVFS_CON1, uReg);

			uReg = pwrcal_readl(offset + PHY0_DVFS_CON2);
			uReg &= ~(pwrcal_dfs_phy_mif_table[target_mif_level_switch_idx].phy_Dvfs_Con2_set1_mask);
			uReg |= pwrcal_dfs_phy_mif_table[target_mif_level_switch_idx].phy_Dvfs_Con2_set1;
			pwrcal_writel(offset + PHY0_DVFS_CON2, uReg);

			uReg = pwrcal_readl(offset + PHY0_DVFS_CON3);
			uReg &= ~(pwrcal_dfs_phy_mif_table[target_mif_level_switch_idx].phy_Dvfs_Con3_set1_mask);
			uReg |= pwrcal_dfs_phy_mif_table[target_mif_level_switch_idx].phy_Dvfs_Con3_set1;
			pwrcal_writel(offset + PHY0_DVFS_CON3, uReg);

			pwrcal_writel(offset + DREX0_PAUSE_MRS0, pwrcal_pause_directcmd_dfs_mif_table[target_mif_level_switch_idx].drex_Puase_MRS0);
			pwrcal_writel(offset + DREX0_PAUSE_MRS1, pwrcal_pause_directcmd_dfs_mif_table[target_mif_level_switch_idx].drex_Puase_MRS1);
			pwrcal_writel(offset + DREX0_PAUSE_MRS2, pwrcal_pause_directcmd_dfs_mif_table[target_mif_level_switch_idx].drex_Puase_MRS2);
			pwrcal_writel(offset + DREX0_PAUSE_MRS3, pwrcal_pause_directcmd_dfs_mif_table[target_mif_level_switch_idx].drex_Puase_MRS3);
			pwrcal_writel(offset + DREX0_PAUSE_MRS4, pwrcal_pause_directcmd_dfs_mif_table[target_mif_level_switch_idx].drex_Puase_MRS4);
		}
	} else {
		pr_err("wrong DMC timing set selection on DVFS\n");
		return;
	}
}

//static unsigned int per_mrs_en;

void pwrcal_dmc_set_pre_dvfs(void)
{

}

void pwrcal_dmc_set_post_dvfs(unsigned long long target_freq)
{

}



void pwrcal_dmc_set_refresh_method_pre_dvfs(unsigned long long current_rate, unsigned long long target_rate)
{
	/* target_rate is MIF clock rate */
	unsigned int uReg;
	uReg = pwrcal_readl(DREX0_CG_CONTROL);
	uReg = ((uReg & ~(1 << 24)) | (0 << 24));
	pwrcal_writel(DREX0_CG_CONTROL, uReg);	//External Clock Gating - PHY Clock Gating disable
}

void pwrcal_dmc_set_refresh_method_post_dvfs(unsigned long long current_rate, unsigned long long target_rate)
{
	/* target_rate is MIF clock rate */
	unsigned int uReg;
	uReg = pwrcal_readl(DREX0_CG_CONTROL);
	uReg = ((uReg & ~(1 << 24)) | (1 << 24));
	pwrcal_writel(DREX0_CG_CONTROL, uReg);	//External Clock Gating - PHY Clock Gating enable
}

void pwrcal_dmc_set_dsref_cycle(unsigned long long target_rate)
{
	unsigned int  uReg, cycle;

	/* target_rate is MIF clock rate */
	if (target_rate > 800 * MHZ)
		cycle = 0x3ff;
	else if (target_rate > 400 * MHZ)
		cycle = 0x1ff;
	else if (target_rate > 200 * MHZ)
		cycle = 0x90;
	else
		cycle = 0x90;


	/* dsref disable */
	uReg = pwrcal_readl(DREX0_MEMORY_CONTROL0);
	uReg &= ~(1 << 4);
	pwrcal_writel(DREX0_MEMORY_CONTROL0, uReg);

	uReg = pwrcal_readl(DREX0_POWER_DOWN_CONFIG);
	uReg = ((uReg & ~(0xffff << 16)) | (cycle << 16));
	pwrcal_writel(DREX0_POWER_DOWN_CONFIG, uReg);

	/* dsref enable */
	uReg = pwrcal_readl(DREX0_MEMORY_CONTROL0);
	uReg |= (1 << 4);
	pwrcal_writel(DREX0_MEMORY_CONTROL0, uReg);

}



void dfs_dram_param_init(void)
{
	int i;
	void *dram_block;
	u32 uMem_type;
//	int memory_size = 2; // means 3GB
	struct ect_timing_param_size *size;

	uMem_type = (pwrcal_readl(PMU_DREX_CALIBRATION2) & 0xFFFF);

	dram_block = ect_get_block(BLOCK_TIMING_PARAM);
	if (dram_block == NULL)
		return;

	size = ect_timing_param_get_size(dram_block, uMem_type);
	if (size == NULL)
		return;

	if (num_of_dram_parameter != size->num_of_timing_param)
		return;

	pwrcal_dfs_drex_mif_table = kzalloc(sizeof(struct dmc_drex_dfs_mif_table) * num_of_g_dmc_drex_dfs_mif_table_column * size->num_of_level, GFP_KERNEL);
	if (pwrcal_dfs_drex_mif_table == NULL)
		return;

	pwrcal_pause_directcmd_dfs_mif_table = kzalloc(sizeof(struct dmc_directcmd_dfs_mif_table) * num_of_g_dmc_directcmd_dfs_mif_table_column * size->num_of_level, GFP_KERNEL);
	if (pwrcal_pause_directcmd_dfs_mif_table == NULL)
		return;

	pwrcal_dfs_phy_mif_table = kzalloc(sizeof(struct dmc_phy_dfs_mif_table) * num_of_g_dmc_phy_dfs_mif_table_column * size->num_of_level, GFP_KERNEL);
	if (pwrcal_dfs_phy_mif_table == NULL)
		return;

	for (i = 0; i < size->num_of_level; ++i) {
		pwrcal_dfs_drex_mif_table[i].drex_Tick_Granularity = size->timing_parameter[i * num_of_dram_parameter + drex_Tick_Granularity];
		pwrcal_dfs_drex_mif_table[i].drex_mr4_sensing_cyc = size->timing_parameter[i * num_of_dram_parameter + drex_mr4_sensing_cyc];
		pwrcal_dfs_drex_mif_table[i].drex_dqs_osc_start_cyc = size->timing_parameter[i * num_of_dram_parameter + drex_dqs_osc_start_cyc];
		pwrcal_dfs_drex_mif_table[i].drex_Termination_Control = size->timing_parameter[i * num_of_dram_parameter + drex_Termination_Control];
		pwrcal_dfs_drex_mif_table[i].drex_Winconfig_Write_Odt = size->timing_parameter[i * num_of_dram_parameter + drex_Winconfig_Write_Odt];
		pwrcal_dfs_drex_mif_table[i].drex_Timing_Row0 = size->timing_parameter[i * num_of_dram_parameter + drex_Timing_Row0];
		pwrcal_dfs_drex_mif_table[i].drex_Timing_Row1 = size->timing_parameter[i * num_of_dram_parameter + drex_Timing_Row1];
		pwrcal_dfs_drex_mif_table[i].drex_Timing_Data_Aclk = size->timing_parameter[i * num_of_dram_parameter + drex_Timing_Data_Aclk];
		pwrcal_dfs_drex_mif_table[i].drex_Timing_Data_Mclk = size->timing_parameter[i * num_of_dram_parameter + drex_Timing_Data_Mclk];
		pwrcal_dfs_drex_mif_table[i].drex_Timing_Power0 = size->timing_parameter[i * num_of_dram_parameter + drex_Timing_Power0];
		pwrcal_dfs_drex_mif_table[i].drex_Timing_Power1 = size->timing_parameter[i * num_of_dram_parameter + drex_Timing_Power1];
		pwrcal_dfs_drex_mif_table[i].drex_Etcl = size->timing_parameter[i * num_of_dram_parameter + drex_Etcl];

		pwrcal_pause_directcmd_dfs_mif_table[i].drex_Puase_MRS0 = size->timing_parameter[i * num_of_dram_parameter + drex_Puase_MRS0];
		pwrcal_pause_directcmd_dfs_mif_table[i].drex_Puase_MRS1 = size->timing_parameter[i * num_of_dram_parameter + drex_Puase_MRS1];
		pwrcal_pause_directcmd_dfs_mif_table[i].drex_Puase_MRS2 = size->timing_parameter[i * num_of_dram_parameter + drex_Puase_MRS2];
		pwrcal_pause_directcmd_dfs_mif_table[i].drex_Puase_MRS3 = size->timing_parameter[i * num_of_dram_parameter + drex_Puase_MRS3];
		pwrcal_pause_directcmd_dfs_mif_table[i].drex_Puase_MRS4 = size->timing_parameter[i * num_of_dram_parameter + drex_Puase_MRS4];

		pwrcal_dfs_phy_mif_table[i].phy_Dvfs_Con0_set1 = size->timing_parameter[i * num_of_dram_parameter + phy_Dvfs_Con0_set1];
		pwrcal_dfs_phy_mif_table[i].phy_Dvfs_Con0_set0 = size->timing_parameter[i * num_of_dram_parameter + phy_Dvfs_Con0_set0];
		pwrcal_dfs_phy_mif_table[i].phy_Dvfs_Con0_set1_mask = size->timing_parameter[i * num_of_dram_parameter + phy_Dvfs_Con0_set1_mask];
		pwrcal_dfs_phy_mif_table[i].phy_Dvfs_Con0_set0_mask = size->timing_parameter[i * num_of_dram_parameter + phy_Dvfs_Con0_set0_mask];
		pwrcal_dfs_phy_mif_table[i].phy_Dvfs_Con1_set1 = size->timing_parameter[i * num_of_dram_parameter + phy_Dvfs_Con1_set1];
		pwrcal_dfs_phy_mif_table[i].phy_Dvfs_Con1_set0 = size->timing_parameter[i * num_of_dram_parameter + phy_Dvfs_Con1_set0];
		pwrcal_dfs_phy_mif_table[i].phy_Dvfs_Con1_set1_mask = size->timing_parameter[i * num_of_dram_parameter + phy_Dvfs_Con1_set1_mask];
		pwrcal_dfs_phy_mif_table[i].phy_Dvfs_Con1_set0_mask = size->timing_parameter[i * num_of_dram_parameter + phy_Dvfs_Con1_set0_mask];
		pwrcal_dfs_phy_mif_table[i].phy_Dvfs_Con2_set1 = size->timing_parameter[i * num_of_dram_parameter + phy_Dvfs_Con2_set1];
		pwrcal_dfs_phy_mif_table[i].phy_Dvfs_Con2_set0 = size->timing_parameter[i * num_of_dram_parameter + phy_Dvfs_Con2_set0];
		pwrcal_dfs_phy_mif_table[i].phy_Dvfs_Con2_set1_mask = size->timing_parameter[i * num_of_dram_parameter + phy_Dvfs_Con2_set1_mask];
		pwrcal_dfs_phy_mif_table[i].phy_Dvfs_Con2_set0_mask = size->timing_parameter[i * num_of_dram_parameter + phy_Dvfs_Con2_set0_mask];
		pwrcal_dfs_phy_mif_table[i].phy_Dvfs_Con3_set1 = size->timing_parameter[i * num_of_dram_parameter + phy_Dvfs_Con3_set1];
		pwrcal_dfs_phy_mif_table[i].phy_Dvfs_Con3_set0 = size->timing_parameter[i * num_of_dram_parameter + phy_Dvfs_Con3_set0];
		pwrcal_dfs_phy_mif_table[i].phy_Dvfs_Con3_set1_mask = size->timing_parameter[i * num_of_dram_parameter + phy_Dvfs_Con3_set1_mask];
		pwrcal_dfs_phy_mif_table[i].phy_Dvfs_Con3_set0_mask = size->timing_parameter[i * num_of_dram_parameter + phy_Dvfs_Con3_set0_mask];
	}
}

void dfs_mif_level_init(void)
{
	int i;
	void *dvfs_block;
	struct ect_dvfs_domain *domain;

	dvfs_block = ect_get_block(BLOCK_DVFS);
	if (dvfs_block == NULL)
		return;

	domain = ect_dvfs_get_domain(dvfs_block, vclk_dvfs_mif.vclk.name);
	if (domain == NULL)
		return;

	mif_freq_to_level = kzalloc(sizeof(unsigned long long) * domain->num_of_level, GFP_KERNEL);
	if (mif_freq_to_level == NULL)
		return;

	num_mif_freq_to_level = domain->num_of_level;

	for (i = 0; i < domain->num_of_level; ++i)
		mif_freq_to_level[i] = (unsigned long long) domain->list_level[i].level * KHZ;
}

void dfs_dram_init(void)
{
	dfs_dram_param_init();
	dfs_mif_level_init();
}


#endif
