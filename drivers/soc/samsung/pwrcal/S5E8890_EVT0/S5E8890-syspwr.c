#include "../pwrcal-env.h"
#include "../pwrcal.h"
#include "../pwrcal-pmu.h"
#include "../pwrcal-rae.h"
#include "S5E8890-cmusfr.h"
#include "S5E8890-pmusfr.h"
#include "S5E8890-cmu.h"

struct cmu_backup {
	void *sfr;
	void *power;
	unsigned int backup;
	int backup_valid;
};

enum sys_powerdown {
	syspwr_sicd,
	syspwr_sicd_cpd,
	syspwr_aftr,
	syspwr_stop,
	syspwr_dstop,
	syspwr_lpd,
	syspwr_alpa,
	syspwr_sleep,
	num_of_syspwr,
};

/* set_pmu_lpi_mask */
#define ASATB_MSTIF_MNGS_CAM1		(0x1 << 16)
#define ASATB_MSTIF_MNGS_AUD			(0x1 << 15)

static void set_pmu_lpi_mask(void)
{
	unsigned int tmp;

	tmp = pwrcal_readl(LPI_MASK_MNGS_ASB);
	tmp |= (ASATB_MSTIF_MNGS_AUD | ASATB_MSTIF_MNGS_CAM1);
	pwrcal_writel(LPI_MASK_MNGS_ASB, tmp);

}

/* init_pmu_l2_option*/
#define USE_AUTOMATIC_L2FLUSHREQ	(0x1 << 17)
#define USE_STANDBYWFIL2		(0x1 << 16)
#define USE_RETENTION			(0x1 << 4)

static void init_pmu_l2_option(void)
{
	unsigned int tmp;

	/* disable automatic L2 flush */
	/* disable L2 retention */
	/* eanble STANDBYWFIL2 MNGS only */

	tmp = pwrcal_readl(MNGS_L2_OPTION);
	tmp &= ~(USE_AUTOMATIC_L2FLUSHREQ | USE_RETENTION);
	tmp |= USE_STANDBYWFIL2;
	pwrcal_writel(MNGS_L2_OPTION, tmp);

	tmp = pwrcal_readl(APOLLO_L2_OPTION);
	tmp &= ~(USE_AUTOMATIC_L2FLUSHREQ | USE_RETENTION);
	tmp |= USE_STANDBYWFIL2;
	pwrcal_writel(APOLLO_L2_OPTION, tmp);
}

static unsigned int *pmu_cpuoption_sfrlist[] = {
	MNGS_CPU0_OPTION,
	MNGS_CPU1_OPTION,
	MNGS_CPU2_OPTION,
	MNGS_CPU3_OPTION,
	APOLLO_CPU0_OPTION,
	APOLLO_CPU1_OPTION,
	APOLLO_CPU2_OPTION,
	APOLLO_CPU3_OPTION,
};
static unsigned int *pmu_cpuduration_sfrlist[] = {
	MNGS_CPU0_DURATION0,
	MNGS_CPU1_DURATION0,
	MNGS_CPU2_DURATION0,
	MNGS_CPU3_DURATION0,
	APOLLO_CPU0_DURATION0,
	APOLLO_CPU1_DURATION0,
	APOLLO_CPU2_DURATION0,
	APOLLO_CPU3_DURATION0,
};

#define EMULATION	(0x1 << 31)
#define ENABLE_DBGNOPWRDWN	(0x1 << 30)
#define USE_SMPEN	(0x1 << 28)
#define DBGPWRDWNREQ_EMULATION	(0x1 << 27)
#define RESET_EMULATION	(0x1 << 26)
#define CLAMP_EMULATION	(0x1 << 25)
#define USE_STANDBYWFE	(0x1 << 24)
#define IGNORE_OUTPUT_UPDATE_DONE	(0x1 << 20)
#define REQ_LAST_CPU	(0x1 << 17)
#define USE_STANDBYWFI	(0x1 << 16)
#define SKIP_DBGPWRDWN	(0x1 << 15)
#define USE_DELAYED_RESET_ASSERTION	(0x1 << 12)
#define ENABLE_GPR_CPUPWRUP	(0x1 << 9)
#define ENABLE_AUTO_WAKEUP_INT_REQ	(0x1 << 8)
#define USE_IRQCPU_FOR_PWRUP	(0x1 << 5)
#define USE_IRQCPU_FOR_PWRDOWN	(0x1 << 4)
#define USE_MEMPWR_FEEDBACK	(0x1 << 3)
#define USE_MEMPWR_COUNTER	(0x1 << 2)
#define USE_SC_FEEDBACK	(0x1 << 1)
#define USE_SC_COUNTER	(0x1 << 0)

#define DUR_WAIT_RESET	(0xF << 20)
#define DUR_CHG_RESET	(0xF << 16)
#define DUR_MEMPWR	(0xF << 12)
#define DUR_SCPRE	(0xF << 8)
#define DUR_SCALL	(0xF << 4)
#define DUR_SCPRE_WAIT	(0xF << 0)
#define DUR_SCALL_VALUE (1 << 4)

static void init_pmu_cpu_option(void)
{
	int cpu;
	unsigned int tmp;

	/* 8890 uses Point of no return version PMU */

	/* use both sc_counter and sc_feedback (for veloce counter only) */
	/* enable to wait for low SMP-bit at sys power down */
	for (cpu = 0; cpu < sizeof(pmu_cpuoption_sfrlist) / sizeof(pmu_cpuoption_sfrlist[0]); cpu++) {
		tmp = pwrcal_readl(pmu_cpuoption_sfrlist[cpu]);
		tmp &= ~EMULATION;
		tmp &= ~ENABLE_DBGNOPWRDWN;
		tmp |= USE_SMPEN;
		tmp &= ~DBGPWRDWNREQ_EMULATION;

		tmp &= ~RESET_EMULATION;
		tmp &= ~CLAMP_EMULATION;
		tmp &= ~USE_STANDBYWFE;
		tmp &= ~IGNORE_OUTPUT_UPDATE_DONE;
		tmp &= ~REQ_LAST_CPU;

		tmp |= USE_STANDBYWFI;

		tmp &= ~SKIP_DBGPWRDWN;
		tmp &= ~USE_DELAYED_RESET_ASSERTION;
		tmp &= ~ENABLE_GPR_CPUPWRUP;

		tmp |= ENABLE_AUTO_WAKEUP_INT_REQ;

		tmp &= ~USE_IRQCPU_FOR_PWRUP;
		tmp &= ~USE_IRQCPU_FOR_PWRDOWN;

		tmp |= USE_MEMPWR_FEEDBACK;
		tmp &= ~USE_MEMPWR_COUNTER;

		tmp |= USE_SC_FEEDBACK;
		tmp &= ~USE_SC_COUNTER;

		pwrcal_writel(pmu_cpuoption_sfrlist[cpu], tmp);
	}

	for (cpu = 0; cpu < sizeof(pmu_cpuduration_sfrlist) / sizeof(pmu_cpuduration_sfrlist[0]); cpu++) {
		tmp = pwrcal_readl(pmu_cpuduration_sfrlist[cpu]);
		tmp |= DUR_WAIT_RESET;
		tmp &= ~DUR_SCALL;
		tmp |= DUR_SCALL_VALUE;
		pwrcal_writel(pmu_cpuduration_sfrlist[cpu], tmp);
	}
}

static void init_pmu_cpuseq_option(void)
{

}

#define ENABLE_MNGS_CPU		(0x1 << 0)
#define ENABLE_APOLLO_CPU	(0x1 << 1)

static void init_pmu_up_scheduler(void)
{
	unsigned int tmp;

	/* limit in-rush current for MNGS local power up */
	tmp = pwrcal_readl(UP_SCHEDULER);
	tmp |= ENABLE_MNGS_CPU;
	pwrcal_writel(UP_SCHEDULER, tmp);
	/* limit in-rush current for APOLLO local power up */
	tmp = pwrcal_readl(UP_SCHEDULER);
	tmp |= ENABLE_APOLLO_CPU;
	pwrcal_writel(UP_SCHEDULER, tmp);
}

static unsigned int *pmu_feedback_sfrlist[] = {
	MNGS_NONCPU_OPTION,
	APOLLO_NONCPU_OPTION,
	TOP_PWR_OPTION,
	TOP_PWR_MIF_OPTION,
	PWR_DDRPHY_OPTION,
	CAM0_OPTION,
	MSCL_OPTION,
	G3D_OPTION,
	DISP0_OPTION,
	CAM1_OPTION,
	AUD_OPTION,
	FSYS0_OPTION,
	BUS0_OPTION,
	ISP0_OPTION,
	ISP1_OPTION,
	MFC_OPTION,
	DISP1_OPTION,
	FSYS1_OPTION,
};


static void init_pmu_feedback(void)
{
	int i;
	unsigned int tmp;

	for (i = 0; i < sizeof(pmu_feedback_sfrlist) / sizeof(pmu_feedback_sfrlist[0]); i++) {
		tmp = pwrcal_readl(pmu_feedback_sfrlist[i]);
		tmp &= ~USE_SC_COUNTER;
		tmp |= USE_SC_FEEDBACK;
		pwrcal_writel(pmu_feedback_sfrlist[i], tmp);
	}
}

#define XXTI_DUR_STABLE				0xCB1 /* 2ms @ 26MHz */
#define TCXO_DUR_STABLE				0xCB1 /* 2ms @ 26MHz */
#define EXT_REGULATOR_DUR_STABLE	0x130A /* 3ms @ 26MHz */
#define EXT_REGULATOR_MIF_DUR_STABLE	0x130A /* 3ms @ 26MHz */


static void init_pmu_stable_counter(void)
{
	pwrcal_writel(XXTI_DURATION3, XXTI_DUR_STABLE);
	pwrcal_writel(TCXO_DURATION3, TCXO_DUR_STABLE);
	pwrcal_writel(EXT_REGULATOR_DURATION3, EXT_REGULATOR_DUR_STABLE);
	pwrcal_writel(EXT_REGULATOR_MIF_DURATION3, EXT_REGULATOR_MIF_DUR_STABLE);
}

#define ENABLE_HW_TRIP                 (0x1 << 31)
#define PS_HOLD_OUTPUT_HIGH            (0x3 << 8)
static void init_ps_hold_setting(void)
{
	unsigned int tmp;

	tmp = pwrcal_readl(PS_HOLD_CONTROL);
	tmp |= (ENABLE_HW_TRIP | PS_HOLD_OUTPUT_HIGH);
	pwrcal_writel(PS_HOLD_CONTROL, tmp);
}

static void enable_armidleclockdown(void)
{
/* Kernel control idle clock down feature
   So, this part is comment out in CAL code*/
	pwrcal_setbit(PWR_CTRL3_MNGS, 0, 1); //PWR_CTRL3[0] USE_L2QACTIVE = 1
	pwrcal_setbit(PWR_CTRL3_MNGS, 1, 1); // PWR_CTRL3[1] IGNORE_L2QREQUEST = 1
	pwrcal_setf(PWR_CTRL3_MNGS, 16, 0x7, 0x7); // PWR_CTRL3[18:16] L2QDELAY = 3'b111 (512 timer ticks)
	pwrcal_setbit(PWR_CTRL3_APOLLO, 0, 1); // PWR_CTRL3[0] USE_L2QACTIVE = 1
	pwrcal_setbit(PWR_CTRL3_APOLLO, 1, 1); // PWR_CTRL3[0] IGNORE_L2QREQUEST = 1
}

static void disable_armidleclockdown(void)
{
	pwrcal_setbit(PWR_CTRL3_MNGS, 0, 0); //PWR_CTRL3[0] USE_L2QACTIVE = 0
	pwrcal_setbit(PWR_CTRL3_APOLLO, 0, 0); // PWR_CTRL3[0] USE_L2QACTIVE = 0
}

static void pwrcal_syspwr_init(void)
{
	init_pmu_feedback();
	init_pmu_l2_option();
	init_pmu_cpu_option();
	init_pmu_cpuseq_option();
	init_pmu_up_scheduler();
	set_pmu_lpi_mask();
	init_pmu_stable_counter();
	init_ps_hold_setting();
	enable_armidleclockdown();
}


struct exynos_pmu_conf {
	void *reg;
	unsigned int val[num_of_syspwr];
};

static struct exynos_pmu_conf exynos8890_pmu_config[] = {
	/* { .addr = address, .val =					{       SICD, SICD_CPD,	AFTR,   STOP,  DSTOP,    LPD,   ALPA, SLEEP } } */
	{	MNGS_CPU0_SYS_PWR_REG,					{	0x0 ,	0x0 ,	0x0 ,	0x8 ,	0x0 ,	0x0 ,	0x0 ,	0x8	} },
	{	DIS_IRQ_MNGS_CPU0_LOCAL_SYS_PWR_REG,			{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	DIS_IRQ_MNGS_CPU0_CENTRAL_SYS_PWR_REG,			{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	DIS_IRQ_MNGS_CPU0_CPUSEQUENCER_SYS_PWR_REG,		{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	MNGS_CPU1_SYS_PWR_REG,					{	0x0 ,	0x0 ,	0x0 ,	0x8 ,	0x0 ,	0x0 ,	0x0 ,	0x8	} },
	{	DIS_IRQ_MNGS_CPU1_LOCAL_SYS_PWR_REG,			{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	DIS_IRQ_MNGS_CPU1_CENTRAL_SYS_PWR_REG,			{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	DIS_IRQ_MNGS_CPU1_CPUSEQUENCER_SYS_PWR_REG,		{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	MNGS_CPU2_SYS_PWR_REG,					{	0x0 ,	0x0 ,	0x0 ,	0x8 ,	0x0 ,	0x0 ,	0x0 ,	0x8	} },
	{	DIS_IRQ_MNGS_CPU2_LOCAL_SYS_PWR_REG,			{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	DIS_IRQ_MNGS_CPU2_CENTRAL_SYS_PWR_REG,			{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	DIS_IRQ_MNGS_CPU2_CPUSEQUENCER_SYS_PWR_REG,		{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	MNGS_CPU3_SYS_PWR_REG,					{	0x0 ,	0x0 ,	0x0 ,	0x8 ,	0x0 ,	0x0 ,	0x0 ,	0x8	} },
	{	DIS_IRQ_MNGS_CPU3_LOCAL_SYS_PWR_REG,			{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	DIS_IRQ_MNGS_CPU3_CENTRAL_SYS_PWR_REG,			{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	DIS_IRQ_MNGS_CPU3_CPUSEQUENCER_SYS_PWR_REG,		{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	APOLLO_CPU0_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x8 ,	0x0 ,	0x0 ,	0x0 ,	0x8	} },
	{	DIS_IRQ_APOLLO_CPU0_LOCAL_SYS_PWR_REG,			{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	DIS_IRQ_APOLLO_CPU0_CENTRAL_SYS_PWR_REG,		{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	DIS_IRQ_APOLLO_CPU0_CPUSEQUENCER_SYS_PWR_REG,		{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	APOLLO_CPU1_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x8 ,	0x0 ,	0x0 ,	0x0 ,	0x8	} },
	{	DIS_IRQ_APOLLO_CPU1_LOCAL_SYS_PWR_REG,			{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	DIS_IRQ_APOLLO_CPU1_CENTRAL_SYS_PWR_REG,		{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	DIS_IRQ_APOLLO_CPU1_CPUSEQUENCER_SYS_PWR_REG,		{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	APOLLO_CPU2_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x8 ,	0x0 ,	0x0 ,	0x0 ,	0x8	} },
	{	DIS_IRQ_APOLLO_CPU2_LOCAL_SYS_PWR_REG,			{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	DIS_IRQ_APOLLO_CPU2_CENTRAL_SYS_PWR_REG,		{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	DIS_IRQ_APOLLO_CPU2_CPUSEQUENCER_SYS_PWR_REG,		{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	APOLLO_CPU3_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x8 ,	0x0 ,	0x0 ,	0x0 ,	0x8	} },
	{	DIS_IRQ_APOLLO_CPU3_LOCAL_SYS_PWR_REG,			{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	DIS_IRQ_APOLLO_CPU3_CENTRAL_SYS_PWR_REG,		{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	DIS_IRQ_APOLLO_CPU3_CPUSEQUENCER_SYS_PWR_REG,		{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	MNGS_NONCPU_SYS_PWR_REG,				{	0xF ,	0x0 ,	0x0 ,	0xF ,	0x0 ,	0x0 ,	0x0 ,	0x8	} },
	{	APOLLO_NONCPU_SYS_PWR_REG,				{	0xF ,	0xF ,	0x0 ,	0xF ,	0x0 ,	0x0 ,	0x0 ,	0x8	} },
	{	MNGS_DBG_SYS_PWR_REG,					{	0x3 ,	0x3 ,	0x3 ,	0x3 ,	0x0 ,	0x0 ,	0x0 ,	0x2	} },
	{	CORTEXM3_APM_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	A7IS_SYS_PWR_REG,					{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	DIS_IRQ_A7IS_LOCAL_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	DIS_IRQ_A7IS_CENTRAL_SYS_PWR_REG,			{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	MNGS_L2_SYS_PWR_REG,					{	0x0 ,	0x0 ,	0x0 ,	0x7 ,	0x0 ,	0x0 ,	0x0 ,	0x7	} },
	{	APOLLO_L2_SYS_PWR_REG,					{	0x7 ,	0x7 ,	0x0 ,	0x7 ,	0x0 ,	0x0 ,	0x0 ,	0x7	} },
	{	MNGS_L2_PWR_SYS_PWR_REG,				{	0x3 ,	0x0 ,	0x0 ,	0x3 ,	0x0 ,	0x0 ,	0x0 ,	0x3	} },
	{	CLKSTOP_CMU_TOP_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	CLKRUN_CMU_TOP_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	RETENTION_CMU_TOP_SYS_PWR_REG,				{	0x3 ,	0x3 ,	0x3 ,	0x3 ,	0x0 ,	0x0 ,	0x0 ,	0x3	} },
	{	RESET_CMU_TOP_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0	} },
	{	RESET_CPUCLKSTOP_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0	} },
	{	CLKSTOP_CMU_MIF_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x0 ,	0x0 ,	0x1 ,	0x0 ,	0x0	} },
	{	CLKRUN_CMU_MIF_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x0 ,	0x0 ,	0x1 ,	0x0 ,	0x0	} },
	{	RETENTION_CMU_MIF_SYS_PWR_REG,				{	0x3 ,	0x3 ,	0x3 ,	0x3 ,	0x0 ,	0x3 ,	0x0 ,	0x3	} },
	{	RESET_CMU_MIF_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0	} },
	{	DDRPHY_CLKSTOP_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x0 ,	0x0 ,	0x1 ,	0x0 ,	0x0	} },
	{	DDRPHY_ISO_SYS_PWR_REG,					{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0 ,	0x1 ,	0x0 ,	0x1	} },
	{	DDRPHY_SOC2_ISO_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1	} },
	{	DDRPHY_DLL_CLK_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x1 ,	0x0 ,	0x0 ,	0x1 ,	0x0 ,	0x0	} },
	{	DISABLE_PLL_CMU_TOP_SYS_PWR_REG,			{	0x0 ,	0x0 ,	0x1 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	DISABLE_PLL_AUD_PLL_SYS_PWR_REG,			{	0x0 ,	0x0 ,	0x1 ,	0x0 ,	0x0 ,	0x0 ,	0x1 ,	0x0	} },
	{	DISABLE_PLL_CMU_MIF_SYS_PWR_REG,			{	0x0 ,	0x0 ,	0x1 ,	0x0 ,	0x0 ,	0x1 ,	0x0 ,	0x0	} },
	{	RESET_AHEAD_CP_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1	} },
	{	TOP_BUS_SYS_PWR_REG,					{	0x7 ,	0x7 ,	0x7 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	TOP_RETENTION_SYS_PWR_REG,				{	0x3 ,	0x3 ,	0x3 ,	0x3 ,	0x0 ,	0x0 ,	0x0 ,	0x3	} },
	{	TOP_PWR_SYS_PWR_REG,					{	0x3 ,	0x3 ,	0x3 ,	0x3 ,	0x0 ,	0x0 ,	0x0 ,	0x3	} },
	{	TOP_BUS_MIF_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x7 ,	0x0 ,	0x0 ,	0x7 ,	0x0 ,	0x0	} },
	{	TOP_RETENTION_MIF_SYS_PWR_REG,				{	0x3 ,	0x3 ,	0x3 ,	0x3 ,	0x0 ,	0x3 ,	0x0 ,	0x3	} },
	{	TOP_PWR_MIF_SYS_PWR_REG,				{	0x3 ,	0x3 ,	0x3 ,	0x3 ,	0x0 ,	0x3 ,	0x0 ,	0x3	} },
	{	PSCDC_MIF_SYS_PWR_REG,					{	0x0 ,	0x0 ,	0x3 ,	0x3 ,	0x0 ,	0x3 ,	0x0 ,	0x0	} },
	{	LOGIC_RESET_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	OSCCLK_GATE_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x1	} },
	{	SLEEP_RESET_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0	} },
	{	LOGIC_RESET_MIF_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0 ,	0x1 ,	0x0 ,	0x0	} },
	{	OSCCLK_GATE_MIF_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x0 ,	0x0 ,	0x1 ,	0x0 ,	0x1	} },
	{	SLEEP_RESET_MIF_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0	} },
	{	MEMORY_TOP_SYS_PWR_REG,					{	0x3 ,	0x3 ,	0x3 ,	0x3 ,	0x0 ,	0x0 ,	0x0 ,	0x3	} },
	{	CLEANY_BUS_SYS_PWR_REG,					{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1	} },
	{	LOGIC_RESET_CP_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1	} },
	{	TCXO_GATE_SYS_PWR_REG,					{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1	} },
	{	RESET_ASB_CP_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1	} },
	{	RESET_ASB_MIF_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0 ,	0x1 ,	0x0 ,	0x0	} },
	{	MEMORY_IMEM_ALIVEIRAM_SYS_PWR_REG,			{	0x3 ,	0x3 ,	0x3 ,	0x3 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	MEMORY_MIF_TOP_SYS_PWR_REG,				{	0x3 ,	0x3 ,	0x3 ,	0x3 ,	0x0 ,	0x3 ,	0x0 ,	0x3	} },
	{	LOGIC_RESET_DDRPHY_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0 ,	0x1 ,	0x0 ,	0x0	} },
	{	RETENTION_DDRPHY_SYS_PWR_REG,				{	0x3 ,	0x3 ,	0x3 ,	0x3 ,	0x0 ,	0x3 ,	0x0 ,	0x3	} },
	{	PWR_DDRPHY_SYS_PWR_REG,					{	0x3 ,	0x3 ,	0x3 ,	0x3 ,	0x0 ,	0x3 ,	0x0 ,	0x3	} },
	{	PAD_RETENTION_LPDDR4_SYS_PWR_REG,			{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0 ,	0x1 ,	0x0 ,	0x0	} },
	{	PAD_RETENTION_AUD_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	PAD_RETENTION_JTAG_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0	} },
	{	PAD_RETENTION_PCIE_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	PAD_RETENTION_UFS_CARD_SYS_PWR_REG,			{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	PAD_RETENTION_MMC2_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	PAD_RETENTION_TOP_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	PAD_RETENTION_UART_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	PAD_RETENTION_MMC0_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	PAD_RETENTION_EBIA_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	PAD_RETENTION_EBIB_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	PAD_RETENTION_SPI_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	PAD_RETENTION_MIF_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0 ,	0x1 ,	0x0 ,	0x0	} },
	{	PAD_ISOLATION_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0 ,	0x0 ,	0x0 ,	0x1	} },
	{	PAD_RETENTION_BOOTLDO_0_SYS_PWR_REG,			{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	PAD_RETENTION_UFS_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	PAD_ISOLATION_MIF_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0 ,	0x1 ,	0x0 ,	0x1	} },
	{	PAD_RETENTION_USB_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	PAD_RETENTION_BOOTLDO_1_SYS_PWR_REG,			{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	PAD_ALV_SEL_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	XXTI_SYS_PWR_REG,					{	0x1 ,	0x1 ,	0x1 ,	0x0 ,	0x1 ,	0x1 ,	0x1 ,	0x0	} },
	{	TCXO_SYS_PWR_REG,					{	0x1 ,	0x1 ,	0x1 ,	0x0 ,	0x1 ,	0x1 ,	0x1 ,	0x0	} },
	{	EXT_REGULATOR_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0	} },
	{	EXT_REGULATOR_MIF_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0	} },
	{	GPIO_MODE_SYS_PWR_REG,					{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	GPIO_MODE_FSYS0_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	GPIO_MODE_FSYS1_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	GPIO_MODE_MIF_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0 ,	0x1 ,	0x0 ,	0x0	} },
	{	GPIO_MODE_AUD_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	CAM0_SYS_PWR_REG,					{	0xF ,	0xF ,	0xF ,	0xF ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	MSCL_SYS_PWR_REG,					{	0xF ,	0xF ,	0xF ,	0xF ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	G3D_SYS_PWR_REG,					{	0xF ,	0xF ,	0xF ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	DISP0_SYS_PWR_REG,					{	0xF ,	0xF ,	0xF ,	0xF ,	0x0 ,	0xF ,	0x0 ,	0x0	} },
	{	CAM1_SYS_PWR_REG,					{	0xF ,	0xF ,	0xF ,	0xF ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	AUD_SYS_PWR_REG,					{	0xF ,	0xF ,	0xF ,	0xF ,	0x0 ,	0x0 ,	0xF ,	0x0	} },
	{	FSYS0_SYS_PWR_REG,					{	0xF ,	0xF ,	0xF ,	0xF ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	BUS0_SYS_PWR_REG,					{	0xF ,	0xF ,	0xF ,	0xF ,	0x0 ,	0xF ,	0x0 ,	0x0	} },
	{	ISP0_SYS_PWR_REG,					{	0xF ,	0xF ,	0xF ,	0xF ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	ISP1_SYS_PWR_REG,					{	0xF ,	0xF ,	0xF ,	0xF ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	MFC_SYS_PWR_REG,					{	0xF ,	0xF ,	0xF ,	0xF ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	DISP1_SYS_PWR_REG,					{	0xF ,	0xF ,	0xF ,	0xF ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	FSYS1_SYS_PWR_REG,					{	0xF ,	0xF ,	0xF ,	0xF ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	CLKRUN_CMU_CAM0_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	CLKRUN_CMU_MSCL_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	CLKRUN_CMU_G3D_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	CLKRUN_CMU_DISP0_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	CLKRUN_CMU_CAM1_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	CLKRUN_CMU_AUD_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	CLKRUN_CMU_FSYS0_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	CLKRUN_CMU_BUS0_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	CLKRUN_CMU_ISP0_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	CLKRUN_CMU_ISP1_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	CLKRUN_CMU_MFC_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	CLKRUN_CMU_DISP1_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	CLKRUN_CMU_FSYS1_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	CLKSTOP_CMU_CAM0_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	CLKSTOP_CMU_MSCL_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	CLKSTOP_CMU_G3D_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	CLKSTOP_CMU_DISP0_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	CLKSTOP_CMU_CAM1_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	CLKSTOP_CMU_AUD_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	CLKSTOP_CMU_FSYS0_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	CLKSTOP_CMU_BUS0_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	CLKSTOP_CMU_ISP0_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	CLKSTOP_CMU_ISP1_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	CLKSTOP_CMU_MFC_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	CLKSTOP_CMU_DISP1_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	CLKSTOP_CMU_FSYS1_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	DISABLE_PLL_CMU_CAM0_SYS_PWR_REG,			{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	DISABLE_PLL_CMU_MSCL_SYS_PWR_REG,			{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	DISABLE_PLL_CMU_G3D_SYS_PWR_REG,			{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	DISABLE_PLL_CMU_DISP0_SYS_PWR_REG,			{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	DISABLE_PLL_CMU_CAM1_SYS_PWR_REG,			{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	DISABLE_PLL_CMU_AUD_SYS_PWR_REG,			{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	DISABLE_PLL_CMU_FSYS0_SYS_PWR_REG,			{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	DISABLE_PLL_CMU_BUS0_SYS_PWR_REG,			{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	DISABLE_PLL_CMU_ISP0_SYS_PWR_REG,			{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	DISABLE_PLL_CMU_ISP1_SYS_PWR_REG,			{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	DISABLE_PLL_CMU_MFC_SYS_PWR_REG,			{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	DISABLE_PLL_CMU_DISP1_SYS_PWR_REG,			{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	DISABLE_PLL_CMU_FSYS1_SYS_PWR_REG,			{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	RESET_LOGIC_CAM0_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	RESET_LOGIC_MSCL_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	RESET_LOGIC_G3D_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	RESET_LOGIC_DISP0_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	RESET_LOGIC_CAM1_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	RESET_LOGIC_AUD_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	RESET_LOGIC_FSYS0_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	RESET_LOGIC_BUS0_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	RESET_LOGIC_ISP0_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	RESET_LOGIC_ISP1_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	RESET_LOGIC_MFC_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	RESET_LOGIC_DISP1_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	RESET_LOGIC_FSYS1_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0	} },
	{	MEMORY_CAM0_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	MEMORY_MSCL_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	MEMORY_G3D_SYS_PWR_REG,					{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	MEMORY_DISP0_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	MEMORY_CAM1_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	MEMORY_AUD_SYS_PWR_REG,					{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	MEMORY_FSYS0_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	MEMORY_BUS0_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	MEMORY_ISP0_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	MEMORY_ISP1_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	MEMORY_MFC_SYS_PWR_REG,					{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	MEMORY_DISP1_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	MEMORY_FSYS1_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	RESET_CMU_CAM0_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	RESET_CMU_MSCL_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	RESET_CMU_G3D_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	RESET_CMU_DISP0_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	RESET_CMU_CAM1_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	RESET_CMU_AUD_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	RESET_CMU_FSYS0_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0	} },
	{	RESET_CMU_BUS0_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0	} },
	{	RESET_CMU_ISP0_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	RESET_CMU_ISP1_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	RESET_CMU_MFC_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	RESET_CMU_DISP1_SYS_PWR_REG,				{	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0 ,	0x0	} },
	{	RESET_CMU_FSYS1_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0	} },
	{	RESET_SLEEP_FSYS0_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0	} },
	{	RESET_SLEEP_BUS0_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0	} },
	{	RESET_SLEEP_FSYS1_SYS_PWR_REG,				{	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x1 ,	0x0	} },
	{ NULL, },
};
							/*	{   SICD,		SICD_CPD,	AFTR,		STOP,		DSTOP,		LPD,		ALPA,		SLEEP } }*/
static struct exynos_pmu_conf exynos8890_pmu_option[] = {
	{PMU_SYNC_CTRL,						{	0x1,		0x1,		0x1,		0x1,		0x1,		0x1,		0x1,		0x1} },
	{CENTRAL_SEQ_OPTION,					{	0xFF0000,	0xFF0000,	0xFF0000,	0xFF0002,	0xFF0002,	0xFF0002,	0xFF0002,	0xFF0002} },
	{CENTRAL_SEQ_OPTION1,					{	0x10000000,	0x10000000,	0x10000000,	0x0,		0x0,		0x0,		0x10000000,	0x0} },
	{CENTRAL_SEQ_MIF_OPTION,				{	0x10,		0x10,		0x10,		0x10,		0x10,		0x10,		0x30,		0x10} },
	{WAKEUP_MASK_MIF,					{	0x13,		0x13,		0x13,		0x13,		0x13,		0x13,		0x3,		0x13} },
	{NULL,},
};


void set_pmu_sys_pwr_reg(enum sys_powerdown mode)
{
	int i;

	for (i = 0; exynos8890_pmu_config[i].reg != NULL; i++)
		pwrcal_writel(exynos8890_pmu_config[i].reg,
				exynos8890_pmu_config[i].val[mode]);

	for (i = 0; exynos8890_pmu_option[i].reg != NULL; i++)
		pwrcal_writel(exynos8890_pmu_option[i].reg,
				exynos8890_pmu_option[i].val[mode]);
}

#define	CENTRALSEQ_PWR_CFG	0x10000
#define	CENTRALSEQ__MIF_PWR_CFG	0x10000

void set_pmu_central_seq(bool enable)
{
	unsigned int tmp;

	/* central sequencer */
	tmp = pwrcal_readl(CENTRAL_SEQ_CONFIGURATION);
	if (enable)
		tmp &= ~CENTRALSEQ_PWR_CFG;
	else
		tmp |= CENTRALSEQ_PWR_CFG;
	pwrcal_writel(CENTRAL_SEQ_CONFIGURATION, tmp);

}

void set_pmu_central_seq_mif(bool enable)
{
	unsigned int tmp;

	/* central sequencer MIF */
	tmp = pwrcal_readl(CENTRAL_SEQ_MIF_CONFIGURATION);
	if (enable)
		tmp &= ~CENTRALSEQ_PWR_CFG;
	else
		tmp |= CENTRALSEQ_PWR_CFG;
	pwrcal_writel(CENTRAL_SEQ_MIF_CONFIGURATION, tmp);
}


static int syspwr_hwacg_control(int mode)
{
	/* all QCH_CTRL in CMU_IMEM */
	pwrcal_setbit(QCH_CTRL_AXI_LH_ASYNC_MI_IMEM, 0, 1);
	pwrcal_setbit(QCH_CTRL_SSS, 0, 1);
	pwrcal_setbit(QCH_CTRL_RTIC, 0, 1);
	pwrcal_setbit(QCH_CTRL_INT_MEM, 0, 1);
	pwrcal_setbit(QCH_CTRL_INT_MEM_ALV, 0, 1);
	pwrcal_setbit(QCH_CTRL_MCOMP, 0, 1);
	pwrcal_setbit(QCH_CTRL_CMU_IMEM, 0, 1);
	pwrcal_setbit(QCH_CTRL_PMU_IMEM, 0, 1);
	pwrcal_setbit(QCH_CTRL_SYSREG_IMEM, 0, 1);
	pwrcal_setbit(QCH_CTRL_PPMU_SSSX, 0, 1);
	/* all QCH_CTRL in CMU_PERIS */
	pwrcal_setbit(QCH_CTRL_AXILHASYNCM_PERIS, 0, 1);
	pwrcal_setbit(QCH_CTRL_CMU_PERIS, 0, 1);
	pwrcal_setbit(QCH_CTRL_PMU_PERIS, 0, 1);
	pwrcal_setbit(QCH_CTRL_SYSREG_PERIS, 0, 1);
	pwrcal_setbit(QCH_CTRL_MONOCNT_APBIF, 0, 1);
	/* all QCH_CTRL in CMU_PERIC0 */
	pwrcal_setbit(QCH_CTRL_AXILHASYNCM_PERIC0, 0, 1);
	pwrcal_setbit(QCH_CTRL_CMU_PERIC0, 0, 1);
	pwrcal_setbit(QCH_CTRL_PMU_PERIC0, 0, 1);
	pwrcal_setbit(QCH_CTRL_SYSREG_PERIC0, 0, 1);
	/* all QCH_CTRL in CMU_PERIC1 */
	pwrcal_setbit(QCH_CTRL_AXILHASYNCM_PERIC1, 0, 1);
	pwrcal_setbit(QCH_CTRL_CMU_PERIC1, 0, 1);
	pwrcal_setbit(QCH_CTRL_PMU_PERIC1, 0, 1);
	pwrcal_setbit(QCH_CTRL_SYSREG_PERIC1, 0, 1);
	/* all QCH_CTRL in CMU_FSYS0 */
	if (pwrcal_getf(FSYS0_STATUS, 0, 0xF) == 0xF) {
		pwrcal_setbit(QCH_CTRL_AXI_LH_ASYNC_MI_TOP_FSYS0, 0, 1);
		pwrcal_setbit(QCH_CTRL_AXI_LH_ASYNC_MI_ETR_USB_FSYS0, 0, 1);
		pwrcal_setbit(QCH_CTRL_ETR_USB_FSYS0_ACLK, 0, 1);
		pwrcal_setbit(QCH_CTRL_ETR_USB_FSYS0_PCLK, 0, 1);
		pwrcal_setbit(QCH_CTRL_CMU_FSYS0, 0, 1);
		pwrcal_setbit(QCH_CTRL_PMU_FSYS0, 0, 1);
		pwrcal_setbit(QCH_CTRL_SYSREG_FSYS0, 0, 1);
		pwrcal_setbit(QCH_CTRL_USBDRD30, 0, 0);
		pwrcal_setbit(QCH_CTRL_MMC0, 0, 1);
		pwrcal_setbit(QCH_CTRL_UFS_LINK_EMBEDDED, 0, 1);
		pwrcal_setbit(QCH_CTRL_USBHOST20, 0, 1);
		pwrcal_setbit(QCH_CTRL_PDMA0, 0, 1);
		pwrcal_setbit(QCH_CTRL_PDMAS, 0, 1);
		pwrcal_setbit(QCH_CTRL_PPMU_FSYS0, 0, 0);
		pwrcal_setbit(QCH_CTRL_ACEL_LH_ASYNC_SI_TOP_FSYS0, 0, 0);
	}
	/* all QCH_CTRL in CMU_FSYS1 */
	if (mode != syspwr_sicd && mode != syspwr_sicd_cpd && mode != syspwr_aftr) {
		if (pwrcal_getf(FSYS1_STATUS, 0, 0xF) == 0xF) {
			pwrcal_setbit(CG_CTRL_MAN_SCLK_PCIE_LINK_WIFI0, 0, 1);
			pwrcal_setbit(CG_CTRL_MAN_SCLK_PCIE_LINK_WIFI1, 0, 1);
			pwrcal_setbit(CG_CTRL_VAL_SCLK_PCIE_LINK_WIFI0, 0, 1);
			pwrcal_setbit(CG_CTRL_VAL_SCLK_PCIE_LINK_WIFI1, 0, 1);
			pwrcal_setf(QSTATE_CTRL_PCIE_RC_LINK_WIFI0, 0, 0x3, 0x1);
			pwrcal_setf(QSTATE_CTRL_PCIE_RC_LINK_WIFI1, 0, 0x3, 0x1);
		}
	}
	if (pwrcal_getf(FSYS1_STATUS, 0, 0xF) == 0xF) {
		pwrcal_setbit(QCH_CTRL_AXI_LH_ASYNC_MI_TOP_FSYS1, 0, 1);
		pwrcal_setbit(QCH_CTRL_CMU_FSYS1, 0, 1);
		pwrcal_setbit(QCH_CTRL_PMU_FSYS1, 0, 1);
		pwrcal_setbit(QCH_CTRL_SYSREG_FSYS1, 0, 1);
		pwrcal_setbit(QCH_CTRL_MMC2, 0, 1);
		pwrcal_setbit(QCH_CTRL_UFS_LINK_SDCARD, 0, 1);
		pwrcal_setbit(QCH_CTRL_PPMU_FSYS1, 0, 1);
		pwrcal_setbit(QCH_CTRL_ACEL_LH_ASYNC_SI_TOP_FSYS1, 0, 1);
	}

	pwrcal_setf(QSTATE_CTRL_APM, 0, 0x3, 0x1);
	pwrcal_setf(QSTATE_CTRL_ASYNCAHBM_SSS_ATLAS, 0, 0x3, 0x1);
	pwrcal_setbit(QSTATE_CTRL_OTP_CON_TOP, 0, 1);

	/*BJ: below codes are added because DISP PPMU QCH has bug*/
	if (pwrcal_getf(DISP0_STATUS, 0, 0xF) == 0xF) {
		pwrcal_setbit(QCH_CTRL_PPMU_DISP0_0, 0, 0);
		pwrcal_setbit(QCH_CTRL_PPMU_DISP0_1, 0, 0);
	}
	if (pwrcal_getf(DISP1_STATUS, 0, 0xF) == 0xF) {
		pwrcal_setbit(QCH_CTRL_PPMU_DISP1_0, 0, 0);
		pwrcal_setbit(QCH_CTRL_PPMU_DISP1_1, 0, 0);
	}
	if (pwrcal_getf(DISP0_STATUS, 0, 0xF) == 0xF)
		pwrcal_setbit(QCH_CTRL_DECON0, 0, 0);
	if (pwrcal_getf(DISP1_STATUS, 0, 0xF) == 0xF) {
		pwrcal_setbit(QCH_CTRL_DECON1_PCLK_0, 0, 0);
		pwrcal_setbit(QCH_CTRL_DECON1_PCLK_1, 0, 0);
	}

	return 0;
}
static int syspwr_hwacg_control_post(int mode)
{
	if (pwrcal_getf(FSYS0_STATUS, 0, 0xF) == 0xF) {
		pwrcal_setbit(QCH_CTRL_USBDRD30, 0, 1);
		pwrcal_setbit(QCH_CTRL_USBHOST20, 0, 1);
		pwrcal_setbit(QCH_CTRL_PPMU_FSYS0, 0, 1);
		pwrcal_setbit(QCH_CTRL_ACEL_LH_ASYNC_SI_TOP_FSYS0, 0, 1);
	}
	if (mode != syspwr_sicd && mode != syspwr_sicd_cpd && mode != syspwr_aftr) {
		if (pwrcal_getf(FSYS1_STATUS, 0, 0xF) == 0xF) {
			pwrcal_setf(QSTATE_CTRL_PCIE_RC_LINK_WIFI0, 0, 0x3, 0x3);
			pwrcal_setf(QSTATE_CTRL_PCIE_RC_LINK_WIFI1, 0, 0x3, 0x3);
			pwrcal_setbit(CG_CTRL_MAN_SCLK_PCIE_LINK_WIFI0, 0, 0);
			pwrcal_setbit(CG_CTRL_MAN_SCLK_PCIE_LINK_WIFI1, 0, 0);
			pwrcal_setbit(CG_CTRL_VAL_SCLK_PCIE_LINK_WIFI0, 0, 1);
			pwrcal_setbit(CG_CTRL_VAL_SCLK_PCIE_LINK_WIFI1, 0, 1);
		}
	}
	return 0;
}


static struct cmu_backup cmu_backup_list[] = {
	{BUS0_PLL_LOCK,	NULL,	0,	0},
	{BUS1_PLL_LOCK,	NULL,	0,	0},
	{BUS2_PLL_LOCK,	NULL,	0,	0},
	{BUS3_PLL_LOCK,	NULL,	0,	0},
	{MFC_PLL_LOCK,	NULL,	0,	0},
	{ISP_PLL_LOCK,	NULL,	0,	0},
	{AUD_PLL_LOCK,	NULL,	0,	0},
	{G3D_PLL_LOCK,	NULL,	0,	0},
	{BUS0_PLL_CON0,	NULL,	0,	0},
	{BUS0_PLL_CON1,	NULL,	0,	0},
	{BUS0_PLL_FREQ_DET,	NULL,	0,	0},
	{BUS1_PLL_CON0,	NULL,	0,	0},
	{BUS1_PLL_CON1,	NULL,	0,	0},
	{BUS1_PLL_FREQ_DET,	NULL,	0,	0},
	{BUS2_PLL_CON0,	NULL,	0,	0},
	{BUS2_PLL_CON1,	NULL,	0,	0},
	{BUS2_PLL_FREQ_DET,	NULL,	0,	0},
	{BUS3_PLL_CON0,	NULL,	0,	0},
	{BUS3_PLL_CON1,	NULL,	0,	0},
	{BUS3_PLL_FREQ_DET,	NULL,	0,	0},
	{MFC_PLL_CON0,	NULL,	0,	0},
	{MFC_PLL_CON1,	NULL,	0,	0},
	{MFC_PLL_FREQ_DET,	NULL,	0,	0},
	{ISP_PLL_CON0,	NULL,	0,	0},
	{ISP_PLL_CON1,	NULL,	0,	0},
	{ISP_PLL_FREQ_DET,	NULL,	0,	0},
	{AUD_PLL_CON0,	NULL,	0,	0},
	{AUD_PLL_CON1,	NULL,	0,	0},
	{AUD_PLL_CON2,	NULL,	0,	0},
	{AUD_PLL_FREQ_DET,	NULL,	0,	0},
	{G3D_PLL_CON0,	NULL,	0,	0},
	{G3D_PLL_CON1,	NULL,	0,	0},
	{G3D_PLL_FREQ_DET,	NULL,	0,	0},
	{CLK_CON_MUX_BUS0_PLL,	NULL,	0,	0},
	{CLK_CON_MUX_BUS1_PLL,	NULL,	0,	0},
	{CLK_CON_MUX_BUS2_PLL,	NULL,	0,	0},
	{CLK_CON_MUX_BUS3_PLL,	NULL,	0,	0},
	{CLK_CON_MUX_MFC_PLL,	NULL,	0,	0},
	{CLK_CON_MUX_ISP_PLL,	NULL,	0,	0},
	{CLK_CON_MUX_AUD_PLL,	NULL,	0,	0},
	{CLK_CON_MUX_G3D_PLL,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_BUS0_PLL,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_BUS1_PLL,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_BUS2_PLL,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_BUS3_PLL,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_MFC_PLL,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_ISP_PLL,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_CCORE_800,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_CCORE_264,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_CCORE_G3D_800,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_CCORE_528,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_CCORE_132,	NULL,	0,	0},
	{CLK_CON_MUX_PCLK_CCORE_66,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_BUS0_528,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_BUS0_200,	NULL,	0,	0},
	{CLK_CON_MUX_PCLK_BUS0_132,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_BUS1_528,	NULL,	0,	0},
	{CLK_CON_MUX_PCLK_BUS1_132,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_DISP0_0_400,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_DISP0_1_400_TOP,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_DISP1_0_400,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_DISP1_1_400_TOP,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_MFC_600,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_MSCL0_528,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_MSCL1_528_TOP,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_IMEM_266,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_IMEM_200,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_IMEM_100,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_FSYS0_200,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_FSYS1_200,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_PERIS_66,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_PERIC0_66,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_PERIC1_66,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_ISP0_ISP0_528,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_ISP0_TPU_400,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_ISP0_TREX_528,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_ISP1_ISP1_468,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_CAM0_CSIS0_414,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_CAM0_CSIS1_168,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_CAM0_CSIS2_234,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_CAM0_3AA0_414,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_CAM0_3AA1_414,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_CAM0_CSIS3_132,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_CAM0_TREX_528,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_CAM1_ARM_672,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_CAM1_TREX_VRA_528,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_CAM1_TREX_B_528,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_CAM1_BUS_264,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_CAM1_PERI_84,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_CAM1_CSIS2_414,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_CAM1_CSIS3_132,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_CAM1_SCL_566,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_DISP0_DECON0_ECLK0_TOP,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_DISP0_DECON0_VCLK0_TOP,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_DISP0_DECON0_VCLK1_TOP,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_DISP0_HDMI_AUDIO_TOP,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_DISP1_DECON1_ECLK0_TOP,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_DISP1_DECON1_ECLK1_TOP,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_FSYS0_USBDRD30,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_FSYS0_MMC0,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_FSYS0_UFSUNIPRO20,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_FSYS0_PHY_24M,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_FSYS0_UFSUNIPRO_CFG,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_FSYS1_MMC2,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_FSYS1_UFSUNIPRO20,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_FSYS1_PCIE_PHY,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_FSYS1_UFSUNIPRO_CFG,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_PERIC0_UART0,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_PERIC1_SPI0,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_PERIC1_SPI1,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_PERIC1_SPI2,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_PERIC1_SPI3,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_PERIC1_SPI4,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_PERIC1_SPI5,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_PERIC1_SPI6,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_PERIC1_SPI7,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_PERIC1_UART1,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_PERIC1_UART2,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_PERIC1_UART3,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_PERIC1_UART4,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_PERIC1_UART5,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_CAM1_ISP_SPI0,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_CAM1_ISP_SPI1,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_CAM1_ISP_UART,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_AP2CP_MIF_PLL_OUT,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_PSCDC_400,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_BUS_PLL_MNGS,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_BUS_PLL_APOLLO,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_BUS_PLL_MIF,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_BUS_PLL_G3D,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_CCORE_800,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_CCORE_264,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_CCORE_G3D_800,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_CCORE_528,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_CCORE_132,	NULL,	0,	0},
	{CLK_CON_DIV_PCLK_CCORE_66,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_BUS0_528,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_BUS0_200,	NULL,	0,	0},
	{CLK_CON_DIV_PCLK_BUS0_132,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_BUS1_528,	NULL,	0,	0},
	{CLK_CON_DIV_PCLK_BUS1_132,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_DISP0_0_400,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_DISP0_1_400,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_DISP1_0_400,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_DISP1_1_400,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_MFC_600,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_MSCL0_528,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_MSCL1_528,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_IMEM_266,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_IMEM_200,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_IMEM_100,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_FSYS0_200,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_FSYS1_200,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_PERIS_66,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_PERIC0_66,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_PERIC1_66,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_ISP0_ISP0_528,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_ISP0_TPU_400,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_ISP0_TREX_528,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_ISP1_ISP1_468,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_CAM0_CSIS0_414,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_CAM0_CSIS1_168,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_CAM0_CSIS2_234,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_CAM0_3AA0_414,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_CAM0_3AA1_414,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_CAM0_CSIS3_132,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_CAM0_TREX_528,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_CAM1_ARM_672,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_CAM1_TREX_VRA_528,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_CAM1_TREX_B_528,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_CAM1_BUS_264,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_CAM1_PERI_84,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_CAM1_CSIS2_414,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_CAM1_CSIS3_132,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_CAM1_SCL_566,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_DISP0_DECON0_ECLK0,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_DISP0_DECON0_VCLK0,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_DISP0_DECON0_VCLK1,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_DISP0_HDMI_AUDIO,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_DISP1_DECON1_ECLK0,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_DISP1_DECON1_ECLK1,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_FSYS0_USBDRD30,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_FSYS0_MMC0,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_FSYS0_UFSUNIPRO20,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_FSYS0_PHY_24M,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_FSYS0_UFSUNIPRO_CFG,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_FSYS1_MMC2,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_FSYS1_UFSUNIPRO20,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_FSYS1_PCIE_PHY,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_FSYS1_UFSUNIPRO_CFG,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_PERIC0_UART0,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_PERIC1_SPI0,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_PERIC1_SPI1,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_PERIC1_SPI2,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_PERIC1_SPI3,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_PERIC1_SPI4,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_PERIC1_SPI5,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_PERIC1_SPI6,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_PERIC1_SPI7,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_PERIC1_UART1,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_PERIC1_UART2,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_PERIC1_UART3,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_PERIC1_UART4,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_PERIC1_UART5,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_CAM1_ISP_SPI0,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_CAM1_ISP_SPI1,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_CAM1_ISP_UART,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_AP2CP_MIF_PLL_OUT,	NULL,	0,	0},
	{CLK_CON_DIV_ACLK_PSCDC_400,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_BUS_PLL_MNGS,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_BUS_PLL_APOLLO,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_BUS_PLL_MIF,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_BUS_PLL_G3D,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_ISP_SENSOR0,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_ISP_SENSOR0,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_ISP_SENSOR0,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_ISP_SENSOR1,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_ISP_SENSOR1,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_ISP_SENSOR1,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_ISP_SENSOR2,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_ISP_SENSOR2,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_ISP_SENSOR2,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_ISP_SENSOR3,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_ISP_SENSOR3,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_ISP_SENSOR3,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_PROMISE_INT,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_PROMISE_INT,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_PROMISE_INT,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_PROMISE_DISP,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_PROMISE_DISP,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_PROMISE_DISP,	NULL,	0,	0},
	{CLKOUT_CMU_TOP0,	NULL,	0,	0},
	{CLKOUT_CMU_TOP0_DIV_STAT,	NULL,	0,	0},
	{CLKOUT_CMU_TOP1,	NULL,	0,	0},
	{CLKOUT_CMU_TOP1_DIV_STAT,	NULL,	0,	0},
	{CLKOUT_CMU_TOP2,	NULL,	0,	0},
	{CLKOUT_CMU_TOP2_DIV_STAT,	NULL,	0,	0},
	{CMU_TOP__CLKOUT0,	NULL,	0,	0},
	{CMU_TOP__CLKOUT1,	NULL,	0,	0},
	{CMU_TOP__CLKOUT2,	NULL,	0,	0},
	{CMU_TOP__CLKOUT3,	NULL,	0,	0},
	{CLK_CON_MUX_CP2AP_MIF_CLK_USER,	NULL,	0,	0},
	{AP2CP_CLK_CTRL,	NULL,	0,	0},
	{CLK_ENABLE_PDN_TOP,	NULL,	0,	0},
	{TOP_ROOTCLKEN,	NULL,	0,	0},
	{TOP0_ROOTCLKEN_ON_GATE,	NULL,	0,	0},
	{TOP1_ROOTCLKEN_ON_GATE,	NULL,	0,	0},
	{TOP2_ROOTCLKEN_ON_GATE,	NULL,	0,	0},
	{TOP3_ROOTCLKEN_ON_GATE,	NULL,	0,	0},
	{CLK_CON_MUX_AUD_PLL_USER,	AUD_STATUS,	0,	0},
	{CLK_CON_MUX_SCLK_I2S,	AUD_STATUS,	0,	0},
	{CLK_CON_MUX_SCLK_PCM,	AUD_STATUS,	0,	0},
	{CLK_CON_MUX_CP2AP_AUD_CLK_USER,	AUD_STATUS,	0,	0},
	{CLK_CON_MUX_ACLK_CA5,	AUD_STATUS,	0,	0},
	{CLK_CON_MUX_CDCLK_AUD,	AUD_STATUS,	0,	0},
	{CLK_CON_DIV_AUD_CA5,	AUD_STATUS,	0,	0},
	{CLK_CON_DIV_ACLK_AUD,	AUD_STATUS,	0,	0},
	{CLK_CON_DIV_PCLK_DBG,	AUD_STATUS,	0,	0},
	{CLK_CON_DIV_ATCLK_AUD,	AUD_STATUS,	0,	0},
	{CLK_CON_DIV_AUD_CDCLK,	AUD_STATUS,	0,	0},
	{CLK_CON_DIV_SCLK_I2S,	AUD_STATUS,	0,	0},
	{CLK_CON_DIV_SCLK_PCM,	AUD_STATUS,	0,	0},
	{CLK_CON_DIV_SCLK_SLIMBUS,	AUD_STATUS,	0,	0},
	{CLK_CON_DIV_SCLK_CP_I2S,	AUD_STATUS,	0,	0},
	{CLK_CON_DIV_SCLK_ASRC,	AUD_STATUS,	0,	0},
	{CLK_CON_DIV_CP_CA5,	AUD_STATUS,	0,	0},
	{CLK_CON_DIV_CP_CDCLK,	AUD_STATUS,	0,	0},
	{CLK_ENABLE_SCLK_CA5,	AUD_STATUS,	0,	0},
	{CLK_ENABLE_ACLK_AUD,	AUD_STATUS,	0,	0},
	{CLK_ENABLE_PCLK_AUD,	AUD_STATUS,	0,	0},
	{CLK_ENABLE_ACLK_ATCLK_AUD,	AUD_STATUS,	0,	0},
	{CLK_ENABLE_SCLK_I2S,	AUD_STATUS,	0,	0},
	{CLK_ENABLE_SCLK_PCM,	AUD_STATUS,	0,	0},
	{CLK_ENABLE_SCLK_SLIMBUS,	AUD_STATUS,	0,	0},
	{CLK_ENABLE_SCLK_CP_I2S,	AUD_STATUS,	0,	0},
	{CLK_ENABLE_SCLK_ASRC,	AUD_STATUS,	0,	0},
	{CLK_ENABLE_SCLK_SLIMBUS_CLKIN,	AUD_STATUS,	0,	0},
	{CLK_ENABLE_SCLK_I2S_BCLK,	AUD_STATUS,	0,	0},
	{CLKOUT_CMU_AUD,	AUD_STATUS,	0,	0},
	{CLKOUT_CMU_AUD_DIV_STAT,	AUD_STATUS,	0,	0},
	{CMU_AUD_SPARE0,	AUD_STATUS,	0,	0},
	{CMU_AUD_SPARE1,	AUD_STATUS,	0,	0},
	{CLK_ENABLE_PDN_AUD,	AUD_STATUS,	0,	0},
	{AUD_SFR_IGNORE_REQ_SYSCLK,	AUD_STATUS,	0,	0},
	{CLK_CON_MUX_ACLK_BUS0_528_USER,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_BUS0_200_USER,	NULL,	0,	0},
	{CLK_CON_MUX_PCLK_BUS0_132_USER,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_BUS0_528_BUS0,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_BUS0_200_BUS0,	NULL,	0,	0},
	{CLK_ENABLE_PCLK_BUS0_132_BUS0,	NULL,	0,	0},
	{CLKOUT_CMU_BUS0,	NULL,	0,	0},
	{CLKOUT_CMU_BUS0_DIV_STAT,	NULL,	0,	0},
	{CMU_BUS0_SPARE0,	NULL,	0,	0},
	{CMU_BUS0_SPARE1,	NULL,	0,	0},
	{CLK_ENABLE_PDN_BUS0,	NULL,	0,	0},
	{BUS0_SFR_IGNORE_REQ_SYSCLK,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_BUS1_528_USER,	NULL,	0,	0},
	{CLK_CON_MUX_PCLK_BUS1_132_USER,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_BUS1_528_BUS1,	NULL,	0,	0},
	{CLK_ENABLE_PCLK_BUS1_132_BUS1,	NULL,	0,	0},
	{CLKOUT_CMU_BUS1,	NULL,	0,	0},
	{CLKOUT_CMU_BUS1_DIV_STAT,	NULL,	0,	0},
	{CMU_BUS1_SPARE0,	NULL,	0,	0},
	{CMU_BUS1_SPARE1,	NULL,	0,	0},
	{CLK_ENABLE_PDN_BUS1,	NULL,	0,	0},
	{BUS1_SFR_IGNORE_REQ_SYSCLK,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_CAM0_CSIS0_414_USER,	CAM0_STATUS,	0,	0},
	{CLK_CON_MUX_ACLK_CAM0_CSIS1_168_USER,	CAM0_STATUS,	0,	0},
	{CLK_CON_MUX_ACLK_CAM0_CSIS2_234_USER,	CAM0_STATUS,	0,	0},
	{CLK_CON_MUX_ACLK_CAM0_CSIS3_132_USER,	CAM0_STATUS,	0,	0},
	{CLK_CON_MUX_ACLK_CAM0_3AA0_414_USER,	CAM0_STATUS,	0,	0},
	{CLK_CON_MUX_ACLK_CAM0_3AA1_414_USER,	CAM0_STATUS,	0,	0},
	{CLK_CON_MUX_ACLK_CAM0_TREX_528_USER,	CAM0_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_RXBYTECLKHS0_CSIS0_USER,	CAM0_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_RXBYTECLKHS1_CSIS0_USER,	CAM0_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_RXBYTECLKHS2_CSIS0_USER,	CAM0_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_RXBYTECLKHS3_CSIS0_USER,	CAM0_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_RXBYTECLKHS0_CSIS1_USER,	CAM0_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_RXBYTECLKHS1_CSIS1_USER,	CAM0_STATUS,	0,	0},
	{CLK_CON_DIV_PCLK_CAM0_CSIS0_207,	CAM0_STATUS,	0,	0},
	{CLK_CON_DIV_PCLK_CAM0_3AA0_207,	CAM0_STATUS,	0,	0},
	{CLK_CON_DIV_PCLK_CAM0_3AA1_207,	CAM0_STATUS,	0,	0},
	{CLK_CON_DIV_PCLK_CAM0_TREX_264,	CAM0_STATUS,	0,	0},
	{CLK_CON_DIV_PCLK_CAM0_TREX_132,	CAM0_STATUS,	0,	0},
	{CLK_ENABLE_ACLK_CAM0_CSIS0_414,	CAM0_STATUS,	0,	0},
	{CLK_ENABLE_PCLK_CAM0_CSIS0_207,	CAM0_STATUS,	0,	0},
	{CLK_ENABLE_ACLK_CAM0_CSIS1_168_CAM0,	CAM0_STATUS,	0,	0},
	{CLK_ENABLE_ACLK_CAM0_CSIS2_234_CAM0,	CAM0_STATUS,	0,	0},
	{CLK_ENABLE_ACLK_CAM0_CSIS3_132_CAM0,	CAM0_STATUS,	0,	0},
	{CLK_ENABLE_ACLK_CAM0_3AA0_414_CAM0,	CAM0_STATUS,	0,	0},
	{CLK_ENABLE_PCLK_CAM0_3AA0_207,	CAM0_STATUS,	0,	0},
	{CLK_ENABLE_ACLK_CAM0_3AA1_414_CAM0,	CAM0_STATUS,	0,	0},
	{CLK_ENABLE_PCLK_CAM0_3AA1_207,	CAM0_STATUS,	0,	0},
	{CLK_ENABLE_ACLK_CAM0_TREX_528_CAM0,	CAM0_STATUS,	0,	0},
	{CLK_ENABLE_PCLK_CAM0_TREX_264,	CAM0_STATUS,	0,	0},
	{CLK_ENABLE_PCLK_CAM0_TREX_132,	CAM0_STATUS,	0,	0},
	{CLK_ENABLE_SCLK_PROMISE_CAM0,	CAM0_STATUS,	0,	0},
	{CLK_ENABLE_PHYCLK_HS0_CSIS0_RX_BYTE,	CAM0_STATUS,	0,	0},
	{CLK_ENABLE_PHYCLK_HS1_CSIS0_RX_BYTE,	CAM0_STATUS,	0,	0},
	{CLK_ENABLE_PHYCLK_HS2_CSIS0_RX_BYTE,	CAM0_STATUS,	0,	0},
	{CLK_ENABLE_PHYCLK_HS3_CSIS0_RX_BYTE,	CAM0_STATUS,	0,	0},
	{CLK_ENABLE_PHYCLK_HS0_CSIS1_RX_BYTE,	CAM0_STATUS,	0,	0},
	{CLK_ENABLE_PHYCLK_HS1_CSIS1_RX_BYTE,	CAM0_STATUS,	0,	0},
	{CLK_ENABLE_PCLK_HPM_APBIF_CAM0,	CAM0_STATUS,	0,	0},
	{CLKOUT_CMU_CAM0,	CAM0_STATUS,	0,	0},
	{CLKOUT_CMU_CAM0_DIV_STAT,	CAM0_STATUS,	0,	0},
	{CMU_CAM0_SPARE0,	CAM0_STATUS,	0,	0},
	{CMU_CAM0_SPARE1,	CAM0_STATUS,	0,	0},
	{CLK_ENABLE_PDN_CAM0,	CAM0_STATUS,	0,	0},
	{CAM0_SFR_IGNORE_REQ_SYSCLK,	CAM0_STATUS,	0,	0},
	{CLK_ENABLE_ACLK_CAM0_CSIS0_414_LOCAL,	CAM0_STATUS,	0,	0},
	{CLK_ENABLE_PCLK_CAM0_CSIS0_207_LOCAL,	CAM0_STATUS,	0,	0},
	{CLK_ENABLE_ACLK_CAM0_CSIS1_168_LOCAL,	CAM0_STATUS,	0,	0},
	{CLK_ENABLE_ACLK_CAM0_CSIS2_234_LOCAL,	CAM0_STATUS,	0,	0},
	{CLK_ENABLE_ACLK_CAM0_CSIS3_132_LOCAL,	CAM0_STATUS,	0,	0},
	{CLK_ENABLE_ACLK_CAM0_3AA0_414_LOCAL,	CAM0_STATUS,	0,	0},
	{CLK_ENABLE_PCLK_CAM0_3AA0_207_LOCAL,	CAM0_STATUS,	0,	0},
	{CLK_ENABLE_ACLK_CAM0_3AA1_414_LOCAL,	CAM0_STATUS,	0,	0},
	{CLK_ENABLE_PCLK_CAM0_3AA1_207_LOCAL,	CAM0_STATUS,	0,	0},
	{CLK_ENABLE_PCLK_CAM0_TREX_264_LOCAL,	CAM0_STATUS,	0,	0},
	{CLK_CON_MUX_ACLK_CAM1_ARM_672_USER,	CAM1_STATUS,	0,	0},
	{CLK_CON_MUX_ACLK_CAM1_TREX_VRA_528_USER,	CAM1_STATUS,	0,	0},
	{CLK_CON_MUX_ACLK_CAM1_TREX_B_528_USER,	CAM1_STATUS,	0,	0},
	{CLK_CON_MUX_ACLK_CAM1_BUS_264_USER,	CAM1_STATUS,	0,	0},
	{CLK_CON_MUX_ACLK_CAM1_PERI_84_USER,	CAM1_STATUS,	0,	0},
	{CLK_CON_MUX_ACLK_CAM1_CSIS2_414_USER,	CAM1_STATUS,	0,	0},
	{CLK_CON_MUX_ACLK_CAM1_CSIS3_132_USER,	CAM1_STATUS,	0,	0},
	{CLK_CON_MUX_ACLK_CAM1_SCL_566_USER,	CAM1_STATUS,	0,	0},
	{CLK_CON_MUX_SCLK_CAM1_ISP_SPI0_USER,	CAM1_STATUS,	0,	0},
	{CLK_CON_MUX_SCLK_CAM1_ISP_SPI1_USER,	CAM1_STATUS,	0,	0},
	{CLK_CON_MUX_SCLK_CAM1_ISP_UART_USER,	CAM1_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_RXBYTECLKHS0_CSIS2_USER,	CAM1_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_RXBYTECLKHS1_CSIS2_USER,	CAM1_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_RXBYTECLKHS2_CSIS2_USER,	CAM1_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_RXBYTECLKHS3_CSIS2_USER,	CAM1_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_RXBYTECLKHS0_CSIS3_USER,	CAM1_STATUS,	0,	0},
	{CLK_CON_DIV_PCLK_CAM1_ARM_168,	CAM1_STATUS,	0,	0},
	{CLK_CON_DIV_PCLK_CAM1_TREX_VRA_264,	CAM1_STATUS,	0,	0},
	{CLK_CON_DIV_PCLK_CAM1_BUS_132,	CAM1_STATUS,	0,	0},
	{CLK_CON_DIV_PCLK_CAM1_SCL_283,	CAM1_STATUS,	0,	0},
	{CLK_ENABLE_ACLK_CAM1_ARM_672_CAM1,	CAM1_STATUS,	0,	0},
	{CLK_ENABLE_PCLK_CAM1_ARM_168,	CAM1_STATUS,	0,	0},
	{CLK_ENABLE_ACLK_CAM1_TREX_VRA_528_CAM1,	CAM1_STATUS,	0,	0},
	{CLK_ENABLE_PCLK_CAM1_TREX_VRA_264,	CAM1_STATUS,	0,	0},
	{CLK_ENABLE_ACLK_CAM1_TREX_B_528_CAM1,	CAM1_STATUS,	0,	0},
	{CLK_ENABLE_ACLK_CAM1_BUS_264_CAM1,	CAM1_STATUS,	0,	0},
	{CLK_ENABLE_PCLK_CAM1_BUS_132,	CAM1_STATUS,	0,	0},
	{CLK_ENABLE_PCLK_CAM1_PERI_84,	CAM1_STATUS,	0,	0},
	{CLK_ENABLE_ACLK_CAM1_CSIS2_414_CAM1,	CAM1_STATUS,	0,	0},
	{CLK_ENABLE_ACLK_CAM1_CSIS3_132_CAM1,	CAM1_STATUS,	0,	0},
	{CLK_ENABLE_ACLK_CAM1_SCL_566_CAM1,	CAM1_STATUS,	0,	0},
	{CLK_ENABLE_PCLK_CAM1_SCL_283,	CAM1_STATUS,	0,	0},
	{CLK_ENABLE_SCLK_CAM1_ISP_SPI0_CAM1,	CAM1_STATUS,	0,	0},
	{CLK_ENABLE_SCLK_CAM1_ISP_SPI1_CAM1,	CAM1_STATUS,	0,	0},
	{CLK_ENABLE_SCLK_CAM1_ISP_UART_CAM1,	CAM1_STATUS,	0,	0},
	{CLK_ENABLE_SCLK_ISP_PERI_IS_B,	CAM1_STATUS,	0,	0},
	{CLK_ENABLE_PHYCLK_HS0_CSIS2_RX_BYTE,	CAM1_STATUS,	0,	0},
	{CLK_ENABLE_PHYCLK_HS1_CSIS2_RX_BYTE,	CAM1_STATUS,	0,	0},
	{CLK_ENABLE_PHYCLK_HS2_CSIS2_RX_BYTE,	CAM1_STATUS,	0,	0},
	{CLK_ENABLE_PHYCLK_HS3_CSIS2_RX_BYTE,	CAM1_STATUS,	0,	0},
	{CLK_ENABLE_PHYCLK_HS0_CSIS3_RX_BYTE,	CAM1_STATUS,	0,	0},
	{CLKOUT_CMU_CAM1,	CAM1_STATUS,	0,	0},
	{CLKOUT_CMU_CAM1_DIV_STAT,	CAM1_STATUS,	0,	0},
	{CMU_CAM1_SPARE0,	CAM1_STATUS,	0,	0},
	{CMU_CAM1_SPARE1,	CAM1_STATUS,	0,	0},
	{CLK_ENABLE_PDN_CAM1,	CAM1_STATUS,	0,	0},
	{CAM1_SFR_IGNORE_REQ_SYSCLK,	CAM1_STATUS,	0,	0},
	{CLK_ENABLE_ACLK_CAM1_TREX_VRA_528_LOCAL,	CAM1_STATUS,	0,	0},
	{CLK_ENABLE_PCLK_CAM1_TREX_VRA_264_LOCAL,	CAM1_STATUS,	0,	0},
	{CLK_ENABLE_ACLK_CAM1_BUS_264_LOCAL,	CAM1_STATUS,	0,	0},
	{CLK_ENABLE_PCLK_CAM1_PERI_84_LOCAL,	CAM1_STATUS,	0,	0},
	{CLK_ENABLE_ACLK_CAM1_CSIS2_414_LOCAL,	CAM1_STATUS,	0,	0},
	{CLK_ENABLE_ACLK_CAM1_CSIS3_132_LOCAL,	CAM1_STATUS,	0,	0},
	{CLK_ENABLE_ACLK_CAM1_SCL_566_LOCAL,	CAM1_STATUS,	0,	0},
	{CLK_CON_MUX_ACLK_CCORE_800_USER,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_CCORE_264_USER,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_CCORE_G3D_800_USER,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_CCORE_528_USER,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_CCORE_132_USER,	NULL,	0,	0},
	{CLK_CON_MUX_PCLK_CCORE_66_USER,	NULL,	0,	0},
	{CLK_CON_DIV_SCLK_HPM_CCORE,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_CCORE0,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_CCORE1,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_CCORE2,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_CCORE3,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_CCORE4,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_CCORE_AP,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_CCORE_CP,	NULL,	0,	0},
	{CLK_ENABLE_PCLK_CCORE_AP,	NULL,	0,	0},
	{CLK_ENABLE_PCLK_CCORE_CP,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_HPM_CCORE,	NULL,	0,	0},
	{CLKOUT_CMU_CCORE,	NULL,	0,	0},
	{CLKOUT_CMU_CCORE_DIV_STAT,	NULL,	0,	0},
	{CLK_ENABLE_PDN_CCORE,	NULL,	0,	0},
	{CCORE_SFR_IGNORE_REQ_SYSCLK,	NULL,	0,	0},
	{PSCDC_CTRL_CCORE,	NULL,	0,	0},
	{CLK_STOPCTRL_CCORE,	NULL,	0,	0},
	{CMU_CCORE_SPARE0,	NULL,	0,	0},
	{CMU_CCORE_SPARE1,	NULL,	0,	0},
	{DISP_PLL_LOCK,	DISP0_STATUS,	0,	0},
	{DISP_PLL_CON0,	DISP0_STATUS,	0,	0},
	{DISP_PLL_CON1,	DISP0_STATUS,	0,	0},
	{DISP_PLL_FREQ_DET,	DISP0_STATUS,	0,	0},
	{CLK_CON_MUX_DISP_PLL,	DISP0_STATUS,	0,	0},
	{CLK_CON_MUX_ACLK_DISP0_0_400_USER,	DISP0_STATUS,	0,	0},
	{CLK_CON_MUX_ACLK_DISP0_1_400_USER,	DISP0_STATUS,	0,	0},
	{CLK_CON_MUX_SCLK_DISP0_DECON0_ECLK0_USER,	DISP0_STATUS,	0,	0},
	{CLK_CON_MUX_SCLK_DISP0_DECON0_VCLK0_USER,	DISP0_STATUS,	0,	0},
	{CLK_CON_MUX_SCLK_DISP0_DECON0_VCLK1_USER,	DISP0_STATUS,	0,	0},
	{CLK_CON_MUX_SCLK_DISP0_HDMI_AUDIO_USER,	DISP0_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_HDMIPHY_PIXEL_CLKO_USER,	DISP0_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_HDMIPHY_TMDS_CLKO_USER,	DISP0_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_MIPIDPHY0_RXCLKESC0_USER,	DISP0_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_MIPIDPHY0_BITCLKDIV2_USER_DISP0,	DISP0_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_MIPIDPHY0_BITCLKDIV8_USER,	DISP0_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_MIPIDPHY1_RXCLKESC0_USER,	DISP0_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_MIPIDPHY1_BITCLKDIV2_USER_DISP0,	DISP0_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_MIPIDPHY1_BITCLKDIV8_USER,	DISP0_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_MIPIDPHY2_RXCLKESC0_USER,	DISP0_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_MIPIDPHY2_BITCLKDIV2_USER_DISP0,	DISP0_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_MIPIDPHY2_BITCLKDIV8_USER,	DISP0_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_DPPHY_CH0_TXD_CLK_USER,	DISP0_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_DPPHY_CH1_TXD_CLK_USER,	DISP0_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_DPPHY_CH2_TXD_CLK_USER,	DISP0_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_DPPHY_CH3_TXD_CLK_USER,	DISP0_STATUS,	0,	0},
	{CLK_CON_MUX_ACLK_DISP0_1_400_DISP0,	DISP0_STATUS,	0,	0},
	{CLK_CON_MUX_SCLK_DISP0_DECON0_ECLK0_DISP0,	DISP0_STATUS,	0,	0},
	{CLK_CON_MUX_SCLK_DISP0_DECON0_VCLK0_DISP0,	DISP0_STATUS,	0,	0},
	{CLK_CON_MUX_SCLK_DISP0_DECON0_VCLK1_DISP0,	DISP0_STATUS,	0,	0},
	{CLK_CON_MUX_SCLK_DISP0_HDMI_AUDIO_DISP0,	DISP0_STATUS,	0,	0},
	{CLK_CON_DIV_PCLK_DISP0_0_133,	DISP0_STATUS,	0,	0},
	{CLK_CON_DIV_SCLK_DECON0_ECLK0,	DISP0_STATUS,	0,	0},
	{CLK_CON_DIV_SCLK_DECON0_VCLK0,	DISP0_STATUS,	0,	0},
	{CLK_CON_DIV_SCLK_DECON0_VCLK1,	DISP0_STATUS,	0,	0},
	{CLK_CON_DIV_PHYCLK_HDMIPHY_PIXEL_CLKO,	DISP0_STATUS,	0,	0},
	{CLK_CON_DIV_PHYCLK_HDMIPHY_TMDS_20B_CLKO,	DISP0_STATUS,	0,	0},
	{CLK_CON_DSM_DIV_M_SCLK_HDMI_AUDIO,	DISP0_STATUS,	0,	0},
	{CLK_CON_DSM_DIV_N_SCLK_HDMI_AUDIO,	DISP0_STATUS,	0,	0},
	{CG_CTRL_VAL_ACLK_DISP0_0_400,	DISP0_STATUS,	0,	0},
	{CG_CTRL_VAL_ACLK_DISP0_1_400,	DISP0_STATUS,	0,	0},
	{CG_CTRL_VAL_ACLK_DISP0_0_400_SECURE_SFW_DISP0_0,	DISP0_STATUS,	0,	0},
	{CG_CTRL_VAL_ACLK_DISP0_1_400_SECURE_SFW_DISP0_1,	DISP0_STATUS,	0,	0},
	{CG_CTRL_VAL_PCLK_DISP0_0_133,	DISP0_STATUS,	0,	0},
	{CG_CTRL_VAL_PCLK_DISP0_0_133_HPM_APBIF_DISP0,	DISP0_STATUS,	0,	0},
	{CG_CTRL_VAL_PCLK_DISP0_0_133_SECURE_DECON0,	DISP0_STATUS,	0,	0},
	{CG_CTRL_VAL_PCLK_DISP0_0_133_SECURE_VPP0,	DISP0_STATUS,	0,	0},
	{CG_CTRL_VAL_PCLK_DISP0_0_133_SECURE_SFW_DISP0_0,	DISP0_STATUS,	0,	0},
	{CG_CTRL_VAL_PCLK_DISP0_0_133_SECURE_SFW_DISP0_1,	DISP0_STATUS,	0,	0},
	{CG_CTRL_VAL_SCLK_DISP1_400,	DISP0_STATUS,	0,	0},
	{CG_CTRL_VAL_SCLK_DECON0_ECLK0,	DISP0_STATUS,	0,	0},
	{CG_CTRL_VAL_SCLK_DECON0_VCLK0,	DISP0_STATUS,	0,	0},
	{CG_CTRL_VAL_SCLK_DECON0_VCLK1,	DISP0_STATUS,	0,	0},
	{CG_CTRL_VAL_SCLK_HDMI_AUDIO,	DISP0_STATUS,	0,	0},
	{CG_CTRL_VAL_SCLK_DISP0_PROMISE,	DISP0_STATUS,	0,	0},
	{CG_CTRL_VAL_PHYCLK_HDMIPHY,	DISP0_STATUS,	0,	0},
	{CG_CTRL_VAL_PHYCLK_MIPIDPHY0,	DISP0_STATUS,	0,	0},
	{CG_CTRL_VAL_PHYCLK_MIPIDPHY1,	DISP0_STATUS,	0,	0},
	{CG_CTRL_VAL_PHYCLK_MIPIDPHY2,	DISP0_STATUS,	0,	0},
	{CG_CTRL_VAL_PHYCLK_DPPHY,	DISP0_STATUS,	0,	0},
	{CG_CTRL_VAL_OSCCLK,	DISP0_STATUS,	0,	0},
	{CLKOUT_CMU_DISP0,	DISP0_STATUS,	0,	0},
	{CLKOUT_CMU_DISP0_DIV_STAT,	DISP0_STATUS,	0,	0},
	{DISP0_SFR_IGNORE_REQ_SYSCLK,	DISP0_STATUS,	0,	0},
	{CMU_DISP0_SPARE0,	DISP0_STATUS,	0,	0},
	{CMU_DISP0_SPARE1,	DISP0_STATUS,	0,	0},
	{CG_CTRL_MAN_ACLK_DISP0_0_400,	DISP0_STATUS,	0,	0},
	{CG_CTRL_MAN_ACLK_DISP0_1_400,	DISP0_STATUS,	0,	0},
	{CG_CTRL_MAN_ACLK_DISP0_0_400_SECURE_SFW_DISP0_0,	DISP0_STATUS,	0,	0},
	{CG_CTRL_MAN_ACLK_DISP0_1_400_SECURE_SFW_DISP0_1,	DISP0_STATUS,	0,	0},
	{CG_CTRL_MAN_PCLK_DISP0_0_133,	DISP0_STATUS,	0,	0},
	{CG_CTRL_MAN_PCLK_DISP0_0_133_HPM_APBIF_DISP0,	DISP0_STATUS,	0,	0},
	{CG_CTRL_MAN_PCLK_DISP0_0_133_SECURE_DECON0,	DISP0_STATUS,	0,	0},
	{CG_CTRL_MAN_PCLK_DISP0_0_133_SECURE_VPP0,	DISP0_STATUS,	0,	0},
	{CG_CTRL_MAN_PCLK_DISP0_0_133_SECURE_SFW_DISP0_0,	DISP0_STATUS,	0,	0},
	{CG_CTRL_MAN_PCLK_DISP0_0_133_SECURE_SFW_DISP0_1,	DISP0_STATUS,	0,	0},
	{CG_CTRL_MAN_SCLK_DISP1_400,	DISP0_STATUS,	0,	0},
	{CG_CTRL_MAN_SCLK_DECON0_ECLK0,	DISP0_STATUS,	0,	0},
	{CG_CTRL_MAN_SCLK_DECON0_VCLK0,	DISP0_STATUS,	0,	0},
	{CG_CTRL_MAN_SCLK_DECON0_VCLK1,	DISP0_STATUS,	0,	0},
	{CG_CTRL_MAN_SCLK_HDMI_AUDIO,	DISP0_STATUS,	0,	0},
	{CG_CTRL_MAN_SCLK_DISP0_PROMISE,	DISP0_STATUS,	0,	0},
	{CG_CTRL_MAN_PHYCLK_HDMIPHY,	DISP0_STATUS,	0,	0},
	{CG_CTRL_MAN_PHYCLK_MIPIDPHY0,	DISP0_STATUS,	0,	0},
	{CG_CTRL_MAN_PHYCLK_MIPIDPHY1,	DISP0_STATUS,	0,	0},
	{CG_CTRL_MAN_PHYCLK_MIPIDPHY2,	DISP0_STATUS,	0,	0},
	{CG_CTRL_MAN_PHYCLK_DPPHY,	DISP0_STATUS,	0,	0},
	{CG_CTRL_MAN_OSCCLK,	DISP0_STATUS,	0,	0},
	{CG_CTRL_STAT_ACLK_DISP0_0_400,	DISP0_STATUS,	0,	0},
	{CG_CTRL_STAT_ACLK_DISP0_1_400,	DISP0_STATUS,	0,	0},
	{CG_CTRL_STAT_ACLK_DISP0_0_400_SECURE_SFW_DISP0_0,	DISP0_STATUS,	0,	0},
	{CG_CTRL_STAT_ACLK_DISP0_1_400_SECURE_SFW_DISP0_1,	DISP0_STATUS,	0,	0},
	{CG_CTRL_STAT_PCLK_DISP0_0_133_0,	DISP0_STATUS,	0,	0},
	{CG_CTRL_STAT_PCLK_DISP0_0_133_1,	DISP0_STATUS,	0,	0},
	{CG_CTRL_STAT_PCLK_DISP0_0_133_2,	DISP0_STATUS,	0,	0},
	{CG_CTRL_STAT_PCLK_DISP0_0_133_HPM_APBIF_DISP0,	DISP0_STATUS,	0,	0},
	{CG_CTRL_STAT_PCLK_DISP0_0_133_SECURE_DECON0,	DISP0_STATUS,	0,	0},
	{CG_CTRL_STAT_PCLK_DISP0_0_133_SECURE_VPP0,	DISP0_STATUS,	0,	0},
	{CG_CTRL_STAT_PCLK_DISP0_0_133_SECURE_SFW_DISP0_0,	DISP0_STATUS,	0,	0},
	{CG_CTRL_STAT_PCLK_DISP0_0_133_SECURE_SFW_DISP0_1,	DISP0_STATUS,	0,	0},
	{CG_CTRL_STAT_SCLK_DISP1_400,	DISP0_STATUS,	0,	0},
	{CG_CTRL_STAT_SCLK_DECON0_ECLK0,	DISP0_STATUS,	0,	0},
	{CG_CTRL_STAT_SCLK_DECON0_VCLK0,	DISP0_STATUS,	0,	0},
	{CG_CTRL_STAT_SCLK_DECON0_VCLK1,	DISP0_STATUS,	0,	0},
	{CG_CTRL_STAT_SCLK_HDMI_AUDIO,	DISP0_STATUS,	0,	0},
	{CG_CTRL_STAT_SCLK_DISP0_PROMISE,	DISP0_STATUS,	0,	0},
	{CG_CTRL_STAT_PHYCLK_HDMIPHY,	DISP0_STATUS,	0,	0},
	{CG_CTRL_STAT_PHYCLK_MIPIDPHY0,	DISP0_STATUS,	0,	0},
	{CG_CTRL_STAT_PHYCLK_MIPIDPHY1,	DISP0_STATUS,	0,	0},
	{CG_CTRL_STAT_PHYCLK_MIPIDPHY2,	DISP0_STATUS,	0,	0},
	{CG_CTRL_STAT_PHYCLK_DPPHY,	DISP0_STATUS,	0,	0},
	{CG_CTRL_STAT_OSCCLK,	DISP0_STATUS,	0,	0},
	{QCH_CTRL_AXI_LH_ASYNC_MI_DISP0SFR,	DISP0_STATUS,	0,	0},
	{QCH_CTRL_CMU_DISP0,	DISP0_STATUS,	0,	0},
	{QCH_CTRL_PMU_DISP0,	DISP0_STATUS,	0,	0},
	{QCH_CTRL_SYSREG_DISP0,	DISP0_STATUS,	0,	0},
	{QCH_CTRL_DECON0,	DISP0_STATUS,	0,	0},
	{QCH_CTRL_VPP0,	DISP0_STATUS,	0,	0},
	{QCH_CTRL_DSIM0,	DISP0_STATUS,	0,	0},
	{QCH_CTRL_DSIM1,	DISP0_STATUS,	0,	0},
	{QCH_CTRL_DSIM2,	DISP0_STATUS,	0,	0},
	{QCH_CTRL_HDMI,	DISP0_STATUS,	0,	0},
	{QCH_CTRL_DP,	DISP0_STATUS,	0,	0},
	{QCH_CTRL_PPMU_DISP0_0,	DISP0_STATUS,	0,	0},
	{QCH_CTRL_PPMU_DISP0_1,	DISP0_STATUS,	0,	0},
	{QCH_CTRL_SMMU_DISP0_0,	DISP0_STATUS,	0,	0},
	{QCH_CTRL_SMMU_DISP0_1,	DISP0_STATUS,	0,	0},
	{QCH_CTRL_SFW_DISP0_0,	DISP0_STATUS,	0,	0},
	{QCH_CTRL_SFW_DISP0_1,	DISP0_STATUS,	0,	0},
	{QCH_CTRL_LH_ASYNC_SI_R_TOP_DISP,	DISP0_STATUS,	0,	0},
	{QCH_CTRL_LH_ASYNC_SI_TOP_DISP,	DISP0_STATUS,	0,	0},
	{QSTATE_CTRL_DSIM0,	DISP0_STATUS,	0,	0},
	{QSTATE_CTRL_DSIM1,	DISP0_STATUS,	0,	0},
	{QSTATE_CTRL_DSIM2,	DISP0_STATUS,	0,	0},
	{QSTATE_CTRL_HDMI,	DISP0_STATUS,	0,	0},
	{QSTATE_CTRL_HDMI_AUDIO,	DISP0_STATUS,	0,	0},
	{QSTATE_CTRL_DP,	DISP0_STATUS,	0,	0},
	{QSTATE_CTRL_DISP0_MUX,	DISP0_STATUS,	0,	0},
	{QSTATE_CTRL_HDMI_PHY,	DISP0_STATUS,	0,	0},
	{QSTATE_CTRL_DISP1_400,	DISP0_STATUS,	0,	0},
	{QSTATE_CTRL_DECON0,	DISP0_STATUS,	0,	0},
	{QSTATE_CTRL_HPM_APBIF_DISP0,	DISP0_STATUS,	0,	0},
	{QSTATE_CTRL_PROMISE_DISP0,	DISP0_STATUS,	0,	0},
	{QSTATE_CTRL_DPTX_PHY,	DISP0_STATUS,	0,	0},
	{QSTATE_CTRL_MIPI_DPHY_M1S0,	DISP0_STATUS,	0,	0},
	{QSTATE_CTRL_MIPI_DPHY_M4S0,	DISP0_STATUS,	0,	0},
	{QSTATE_CTRL_MIPI_DPHY_M4S4,	DISP0_STATUS,	0,	0},
	{CLK_CON_MUX_ACLK_DISP1_0_400_USER,	DISP1_STATUS,	0,	0},
	{CLK_CON_MUX_ACLK_DISP1_1_400_USER,	DISP1_STATUS,	0,	0},
	{CLK_CON_MUX_SCLK_DISP1_DECON1_ECLK0_USER,	DISP1_STATUS,	0,	0},
	{CLK_CON_MUX_SCLK_DISP1_DECON1_ECLK1_USER,	DISP1_STATUS,	0,	0},
	{CLK_CON_MUX_SCLK_DISP1_600_USER,	DISP1_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_MIPIDPHY0_BITCLKDIV2_USER_DISP1,	DISP1_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_MIPIDPHY1_BITCLKDIV2_USER_DISP1,	DISP1_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_MIPIDPHY2_BITCLKDIV2_USER_DISP1,	DISP1_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_DISP1_HDMIPHY_PIXEL_CLKO_USER,	DISP1_STATUS,	0,	0},
	{CLK_CON_MUX_ACLK_DISP1_1_400_DISP1,	DISP1_STATUS,	0,	0},
	{CLK_CON_MUX_SCLK_DISP1_DECON1_ECLK0_DISP1,	DISP1_STATUS,	0,	0},
	{CLK_CON_MUX_SCLK_DISP1_DECON1_ECLK1_DISP1,	DISP1_STATUS,	0,	0},
	{CLK_CON_MUX_SCLK_DECON1_ECLK1,	DISP1_STATUS,	0,	0},
	{CLK_CON_DIV_PCLK_DISP1_0_133,	DISP1_STATUS,	0,	0},
	{CLK_CON_DIV_SCLK_DECON1_ECLK0,	DISP1_STATUS,	0,	0},
	{CLK_CON_DIV_SCLK_DECON1_ECLK1,	DISP1_STATUS,	0,	0},
	{CG_CTRL_VAL_ACLK_DISP1_0_400,	DISP1_STATUS,	0,	0},
	{CG_CTRL_VAL_ACLK_DISP1_1_400,	DISP1_STATUS,	0,	0},
	{CG_CTRL_VAL_ACLK_DISP1_0_400_SECURE_SFW_DISP1_0,	DISP1_STATUS,	0,	0},
	{CG_CTRL_VAL_ACLK_DISP1_1_400_SECURE_SFW_DISP1_1,	DISP1_STATUS,	0,	0},
	{CG_CTRL_VAL_PCLK_DISP1_0_133,	DISP1_STATUS,	0,	0},
	{CG_CTRL_VAL_PCLK_DISP1_0_133_HPM_APBIF_DISP1,	DISP1_STATUS,	0,	0},
	{CG_CTRL_VAL_PCLK_DISP1_0_133_SECURE_SFW_DISP1_0,	DISP1_STATUS,	0,	0},
	{CG_CTRL_VAL_PCLK_DISP1_0_133_SECURE_SFW_DISP1_1,	DISP1_STATUS,	0,	0},
	{CG_CTRL_VAL_SCLK_DECON1_ECLK_0,	DISP1_STATUS,	0,	0},
	{CG_CTRL_VAL_SCLK_DECON1_ECLK_1,	DISP1_STATUS,	0,	0},
	{CG_CTRL_VAL_SCLK_DISP1_PROMISE,	DISP1_STATUS,	0,	0},
	{CLKOUT_CMU_DISP1,	DISP1_STATUS,	0,	0},
	{CLKOUT_CMU_DISP1_DIV_STAT,	DISP1_STATUS,	0,	0},
	{DISP1_SFR_IGNORE_REQ_SYSCLK,	DISP1_STATUS,	0,	0},
	{CMU_DISP1_SPARE0,	DISP1_STATUS,	0,	0},
	{CMU_DISP1_SPARE1,	DISP1_STATUS,	0,	0},
	{CG_CTRL_MAN_ACLK_DISP1_0_400,	DISP1_STATUS,	0,	0},
	{CG_CTRL_MAN_ACLK_DISP1_1_400,	DISP1_STATUS,	0,	0},
	{CG_CTRL_MAN_ACLK_DISP1_0_400_SECURE_SFW_DISP1_0,	DISP1_STATUS,	0,	0},
	{CG_CTRL_MAN_ACLK_DISP1_1_400_SECURE_SFW_DISP1_1,	DISP1_STATUS,	0,	0},
	{CG_CTRL_MAN_PCLK_DISP1_0_133,	DISP1_STATUS,	0,	0},
	{CG_CTRL_MAN_PCLK_DISP1_0_133_HPM_APBIF_DISP1,	DISP1_STATUS,	0,	0},
	{CG_CTRL_MAN_PCLK_DISP1_0_133_SECURE_SFW_DISP1_0,	DISP1_STATUS,	0,	0},
	{CG_CTRL_MAN_PCLK_DISP1_0_133_SECURE_SFW_DISP1_1,	DISP1_STATUS,	0,	0},
	{CG_CTRL_MAN_SCLK_DECON1_ECLK_0,	DISP1_STATUS,	0,	0},
	{CG_CTRL_MAN_SCLK_DECON1_ECLK_1,	DISP1_STATUS,	0,	0},
	{CG_CTRL_MAN_SCLK_DISP1_PROMISE,	DISP1_STATUS,	0,	0},
	{CG_CTRL_STAT_ACLK_DISP1_0_400,	DISP1_STATUS,	0,	0},
	{CG_CTRL_STAT_ACLK_DISP1_1_400,	DISP1_STATUS,	0,	0},
	{CG_CTRL_STAT_ACLK_DISP1_0_400_SECURE_SFW_DISP1_0,	DISP1_STATUS,	0,	0},
	{CG_CTRL_STAT_ACLK_DISP1_1_400_SECURE_SFW_DISP1_1,	DISP1_STATUS,	0,	0},
	{CG_CTRL_STAT_PCLK_DISP1_0_133_0,	DISP1_STATUS,	0,	0},
	{CG_CTRL_STAT_PCLK_DISP1_0_133_1,	DISP1_STATUS,	0,	0},
	{CG_CTRL_STAT_PCLK_DISP1_0_133_2,	DISP1_STATUS,	0,	0},
	{CG_CTRL_STAT_PCLK_DISP1_0_133_HPM_APBIF_DISP1,	DISP1_STATUS,	0,	0},
	{CG_CTRL_STAT_PCLK_DISP1_0_133_SECURE_SFW_DISP1_0,	DISP1_STATUS,	0,	0},
	{CG_CTRL_STAT_PCLK_DISP1_0_133_SECURE_SFW_DISP1_1,	DISP1_STATUS,	0,	0},
	{CG_CTRL_STAT_SCLK_DECON1_ECLK_0,	DISP1_STATUS,	0,	0},
	{CG_CTRL_STAT_SCLK_DECON1_ECLK_1,	DISP1_STATUS,	0,	0},
	{CG_CTRL_STAT_SCLK_DISP1_PROMISE,	DISP1_STATUS,	0,	0},
	{QCH_CTRL_AXI_LH_ASYNC_MI_DISP1SFR,	DISP1_STATUS,	0,	0},
	{QCH_CTRL_CMU_DISP1,	DISP1_STATUS,	0,	0},
	{QCH_CTRL_PMU_DISP1,	DISP1_STATUS,	0,	0},
	{QCH_CTRL_SYSREG_DISP1,	DISP1_STATUS,	0,	0},
	{QCH_CTRL_VPP1,	DISP1_STATUS,	0,	0},
	{QCH_CTRL_DECON1_PCLK_0,	DISP1_STATUS,	0,	0},
	{QCH_CTRL_DECON1_PCLK_1,	DISP1_STATUS,	0,	0},
	{QCH_CTRL_PPMU_DISP1_0,	DISP1_STATUS,	0,	0},
	{QCH_CTRL_PPMU_DISP1_1,	DISP1_STATUS,	0,	0},
	{QCH_CTRL_SMMU_DISP1_0,	DISP1_STATUS,	0,	0},
	{QCH_CTRL_SMMU_DISP1_1,	DISP1_STATUS,	0,	0},
	{QCH_CTRL_SFW_DISP1_0,	DISP1_STATUS,	0,	0},
	{QCH_CTRL_SFW_DISP1_1,	DISP1_STATUS,	0,	0},
	{QCH_CTRL_AXI_LH_ASYNC_SI_DISP1_0,	DISP1_STATUS,	0,	0},
	{QCH_CTRL_AXI_LH_ASYNC_SI_DISP1_1,	DISP1_STATUS,	0,	0},
	{QSTATE_CTRL_DECON1_ECLK_0,	DISP1_STATUS,	0,	0},
	{QSTATE_CTRL_DECON1_ECLK_1,	DISP1_STATUS,	0,	0},
	{QSTATE_CTRL_HPM_APBIF_DISP1,	DISP1_STATUS,	0,	0},
	{QSTATE_CTRL_PROMISE_DISP1,	DISP1_STATUS,	0,	0},
	{CLK_CON_MUX_ACLK_FSYS0_200_USER,	FSYS0_STATUS,	0,	0},
	{CLK_CON_MUX_SCLK_FSYS0_USBDRD30_USER,	FSYS0_STATUS,	0,	0},
	{CLK_CON_MUX_SCLK_FSYS0_MMC0_USER,	FSYS0_STATUS,	0,	0},
	{CLK_CON_MUX_SCLK_FSYS0_UFSUNIPRO_EMBEDDED_USER,	FSYS0_STATUS,	0,	0},
	{CLK_CON_MUX_SCLK_FSYS0_24M_USER,	FSYS0_STATUS,	0,	0},
	{CLK_CON_MUX_SCLK_FSYS0_UFSUNIPRO_EMBEDDED_CFG_USER,	FSYS0_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_USBDRD30_UDRD30_PHYCLOCK_USER,	FSYS0_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_USBDRD30_UDRD30_PIPE_PCLK_USER,	FSYS0_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_UFS_TX0_SYMBOL_USER,	FSYS0_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_UFS_RX0_SYMBOL_USER,	FSYS0_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_USBHOST20_PHYCLOCK_USER,	FSYS0_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_USBHOST20_FREECLK_USER,	FSYS0_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_USBHOST20_CLK48MOHCI_USER,	FSYS0_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_USBHOST20PHY_REF_CLK,	FSYS0_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_UFS_RX_PWM_CLK_USER,	FSYS0_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_UFS_TX_PWM_CLK_USER,	FSYS0_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_UFS_REFCLK_OUT_SOC_USER,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_VAL_ACLK_FSYS0_200,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_VAL_PCLK_HPM_APBIF_FSYS0,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_VAL_SCLK_USBDRD30_SUSPEND_CLK,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_VAL_SCLK_MMC0,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_VAL_SCLK_UFSUNIPRO_EMBEDDED,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_VAL_SCLK_USBDRD30_REF_CLK,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_VAL_PHYCLK_USBDRD30_UDRD30_PHYCLOCK,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_VAL_PHYCLK_USBDRD30_UDRD30_PIPE_PCLK,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_VAL_PHYCLK_UFS_TX0_SYMBOL,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_VAL_PHYCLK_UFS_RX0_SYMBOL,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_VAL_PHYCLK_USBHOST20_PHYCLOCK,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_VAL_PHYCLK_USBHOST20_FREECLK,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_VAL_PHYCLK_USBHOST20_CLK48MOHCI,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_VAL_PHYCLK_UFS_RX_PWM_CLK,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_VAL_PHYCLK_UFS_TX_PWM_CLK,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_VAL_PHYCLK_UFS_REFCLK_OUT_SOC,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_VAL_SCLK_PROMISE_FSYS0,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_VAL_SCLK_USBHOST20PHY_REF_CLK,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_VAL_SCLK_UFSUNIPRO_EMBEDDED_CFG,	FSYS0_STATUS,	0,	0},
	{CLKOUT_CMU_FSYS0,	FSYS0_STATUS,	0,	0},
	{CLKOUT_CMU_FSYS0_DIV_STAT,	FSYS0_STATUS,	0,	0},
	{FSYS0_SFR_IGNORE_REQ_SYSCLK,	FSYS0_STATUS,	0,	0},
	{CMU_FSYS0_SPARE0,	FSYS0_STATUS,	0,	0},
	{CMU_FSYS0_SPARE1,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_MAN_ACLK_FSYS0_200,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_MAN_PCLK_HPM_APBIF_FSYS0,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_MAN_SCLK_USBDRD30_SUSPEND_CLK,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_MAN_SCLK_MMC0,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_MAN_SCLK_UFSUNIPRO_EMBEDDED,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_MAN_SCLK_USBDRD30_REF_CLK,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_MAN_PHYCLK_USBDRD30_UDRD30_PHYCLOCK,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_MAN_PHYCLK_USBDRD30_UDRD30_PIPE_PCLK,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_MAN_PHYCLK_UFS_TX0_SYMBOL,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_MAN_PHYCLK_UFS_RX0_SYMBOL,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_MAN_PHYCLK_USBHOST20_PHYCLOCK,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_MAN_PHYCLK_USBHOST20_FREECLK,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_MAN_PHYCLK_USBHOST20_CLK48MOHCI,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_MAN_PHYCLK_UFS_RX_PWM_CLK,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_MAN_PHYCLK_UFS_TX_PWM_CLK,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_MAN_PHYCLK_UFS_REFCLK_OUT_SOC,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_MAN_SCLK_PROMISE_FSYS0,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_MAN_SCLK_USBHOST20PHY_REF_CLK,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_MAN_SCLK_UFSUNIPRO_EMBEDDED_CFG,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_STAT_ACLK_FSYS0_200_0,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_STAT_ACLK_FSYS0_200_1,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_STAT_ACLK_FSYS0_200_2,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_STAT_ACLK_FSYS0_200_3,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_STAT_ACLK_FSYS0_200_4,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_STAT_PCLK_HPM_APBIF_FSYS0,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_STAT_SCLK_USBDRD30_SUSPEND_CLK,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_STAT_SCLK_MMC0,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_STAT_SCLK_UFSUNIPRO_EMBEDDED,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_STAT_SCLK_USBDRD30_REF_CLK,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_STAT_PHYCLK_USBDRD30_UDRD30_PHYCLOCK,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_STAT_PHYCLK_USBDRD30_UDRD30_PIPE_PCLK,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_STAT_PHYCLK_UFS_TX0_SYMBOL,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_STAT_PHYCLK_UFS_RX0_SYMBOL,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_STAT_PHYCLK_USBHOST20_PHYCLOCK,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_STAT_PHYCLK_USBHOST20_FREECLK,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_STAT_PHYCLK_USBHOST20_CLK48MOHCI,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_STAT_PHYCLK_UFS_RX_PWM_CLK,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_STAT_PHYCLK_UFS_TX_PWM_CLK,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_STAT_PHYCLK_UFS_REFCLK_OUT_SOC,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_STAT_SCLK_PROMISE_FSYS0,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_STAT_SCLK_USBHOST20PHY_REF_CLK,	FSYS0_STATUS,	0,	0},
	{CG_CTRL_STAT_SCLK_UFSUNIPRO_EMBEDDED_CFG,	FSYS0_STATUS,	0,	0},
	{QCH_CTRL_AXI_LH_ASYNC_MI_TOP_FSYS0,	FSYS0_STATUS,	0,	0},
	{QCH_CTRL_AXI_LH_ASYNC_MI_ETR_USB_FSYS0,	FSYS0_STATUS,	0,	0},
	{QCH_CTRL_ETR_USB_FSYS0_ACLK,	FSYS0_STATUS,	0,	0},
	{QCH_CTRL_ETR_USB_FSYS0_PCLK,	FSYS0_STATUS,	0,	0},
	{QCH_CTRL_CMU_FSYS0,	FSYS0_STATUS,	0,	0},
	{QCH_CTRL_PMU_FSYS0,	FSYS0_STATUS,	0,	0},
	{QCH_CTRL_SYSREG_FSYS0,	FSYS0_STATUS,	0,	0},
	{QCH_CTRL_USBDRD30,	FSYS0_STATUS,	0,	0},
	{QCH_CTRL_MMC0,	FSYS0_STATUS,	0,	0},
	{QCH_CTRL_UFS_LINK_EMBEDDED,	FSYS0_STATUS,	0,	0},
	{QCH_CTRL_USBHOST20,	FSYS0_STATUS,	0,	0},
	{QCH_CTRL_PDMA0,	FSYS0_STATUS,	0,	0},
	{QCH_CTRL_PDMAS,	FSYS0_STATUS,	0,	0},
	{QCH_CTRL_PPMU_FSYS0,	FSYS0_STATUS,	0,	0},
	{QCH_CTRL_ACEL_LH_ASYNC_SI_TOP_FSYS0,	FSYS0_STATUS,	0,	0},
	{QSTATE_CTRL_USBDRD30,	FSYS0_STATUS,	0,	0},
	{QSTATE_CTRL_UFS_LINK_EMBEDDED,	FSYS0_STATUS,	0,	0},
	{QSTATE_CTRL_USBHOST20,	FSYS0_STATUS,	0,	0},
	{QSTATE_CTRL_USBHOST20_PHY,	FSYS0_STATUS,	0,	0},
	{QSTATE_CTRL_GPIO_FSYS0,	FSYS0_STATUS,	0,	0},
	{QSTATE_CTRL_HPM_APBIF_FSYS0,	FSYS0_STATUS,	0,	0},
	{QSTATE_CTRL_PROMISE_FSYS0,	FSYS0_STATUS,	0,	0},
	{CLK_CON_MUX_ACLK_FSYS1_200_USER,	FSYS1_STATUS,	0,	0},
	{CLK_CON_MUX_SCLK_FSYS1_MMC2_USER,	FSYS1_STATUS,	0,	0},
	{CLK_CON_MUX_SCLK_FSYS1_UFSUNIPRO_SDCARD_USER,	FSYS1_STATUS,	0,	0},
	{CLK_CON_MUX_SCLK_FSYS1_UFSUNIPRO_SDCARD_CFG_USER,	FSYS1_STATUS,	0,	0},
	{CLK_CON_MUX_SCLK_FSYS1_PCIE_PHY_USER,	FSYS1_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_UFS_LINK_SDCARD_TX0_SYMBOL_USER,	FSYS1_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_UFS_LINK_SDCARD_RX0_SYMBOL_USER,	FSYS1_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_PCIE_WIFI0_TX0_USER,	FSYS1_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_PCIE_WIFI0_RX0_USER,	FSYS1_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_PCIE_WIFI1_TX0_USER,	FSYS1_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_PCIE_WIFI1_RX0_USER,	FSYS1_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_PCIE_WIFI0_DIG_REFCLK_USER,	FSYS1_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_PCIE_WIFI1_DIG_REFCLK_USER,	FSYS1_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_UFS_LINK_SDCARD_RX_PWM_CLK_USER,	FSYS1_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_UFS_LINK_SDCARD_TX_PWM_CLK_USER,	FSYS1_STATUS,	0,	0},
	{CLK_CON_MUX_PHYCLK_UFS_LINK_SDCARD_REFCLK_OUT_SOC_USER,	FSYS1_STATUS,	0,	0},
	{CLK_CON_DIV_PCLK_COMBO_PHY_WIFI,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_VAL_ACLK_FSYS1_200,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_VAL_PCLK_HPM_APBIF_FSYS1,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_VAL_PCLK_COMBO_PHY_WIFI,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_VAL_SCLK_MMC2,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_VAL_SCLK_UFSUNIPRO_SDCARD,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_VAL_SCLK_UFSUNIPRO_SDCARD_CFG,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_VAL_SCLK_FSYS1_PCIE0_PHY,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_VAL_SCLK_FSYS1_PCIE1_PHY,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_VAL_SCLK_PCIE_LINK_WIFI0,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_VAL_SCLK_PCIE_LINK_WIFI1,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_VAL_PHYCLK_UFS_LINK_SDCARD_TX0_SYMBOL,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_VAL_PHYCLK_UFS_LINK_SDCARD_RX0_SYMBOL,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_VAL_PHYCLK_PCIE_WIFI0_TX0,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_VAL_PHYCLK_PCIE_WIFI0_RX0,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_VAL_PHYCLK_PCIE_WIFI1_TX0,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_VAL_PHYCLK_PCIE_WIFI1_RX0,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_VAL_PHYCLK_PCIE_WIFI0_DIG_REFCLK,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_VAL_PHYCLK_PCIE_WIFI1_DIG_REFCLK,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_VAL_PHYCLK_UFS_LINK_SDCARD_RX_PWM_CLK,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_VAL_PHYCLK_UFS_LINK_SDCARD_TX_PWM_CLK,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_VAL_PHYCLK_UFS_LINK_SDCARD_REFCLK_OUT_SOC,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_VAL_SCLK_PROMISE_FSYS1,	FSYS1_STATUS,	0,	0},
	{CLKOUT_CMU_FSYS1,	FSYS1_STATUS,	0,	0},
	{CLKOUT_CMU_FSYS1_DIV_STAT,	FSYS1_STATUS,	0,	0},
	{FSYS1_SFR_IGNORE_REQ_SYSCLK,	FSYS1_STATUS,	0,	0},
	{CMU_FSYS1_SPARE0,	FSYS1_STATUS,	0,	0},
	{CMU_FSYS1_SPARE1,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_MAN_ACLK_FSYS1_200,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_MAN_PCLK_HPM_APBIF_FSYS1,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_MAN_PCLK_COMBO_PHY_WIFI,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_MAN_SCLK_MMC2,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_MAN_SCLK_UFSUNIPRO_SDCARD,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_MAN_SCLK_UFSUNIPRO_SDCARD_CFG,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_MAN_SCLK_FSYS1_PCIE0_PHY,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_MAN_SCLK_FSYS1_PCIE1_PHY,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_MAN_SCLK_PCIE_LINK_WIFI0,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_MAN_SCLK_PCIE_LINK_WIFI1,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_MAN_PHYCLK_UFS_LINK_SDCARD_TX0_SYMBOL,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_MAN_PHYCLK_UFS_LINK_SDCARD_RX0_SYMBOL,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_MAN_PHYCLK_PCIE_WIFI0_TX0,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_MAN_PHYCLK_PCIE_WIFI0_RX0,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_MAN_PHYCLK_PCIE_WIFI1_TX0,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_MAN_PHYCLK_PCIE_WIFI1_RX0,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_MAN_PHYCLK_PCIE_WIFI0_DIG_REFCLK,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_MAN_PHYCLK_PCIE_WIFI1_DIG_REFCLK,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_MAN_PHYCLK_UFS_LINK_SDCARD_RX_PWM_CLK,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_MAN_PHYCLK_UFS_LINK_SDCARD_TX_PWM_CLK,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_MAN_PHYCLK_UFS_LINK_SDCARD_REFCLK_OUT_SOC,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_MAN_SCLK_PROMISE_FSYS1,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_STAT_ACLK_FSYS1_200_0,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_STAT_ACLK_FSYS1_200_1,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_STAT_ACLK_FSYS1_200_2,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_STAT_ACLK_FSYS1_200_3,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_STAT_ACLK_FSYS1_200_4,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_STAT_PCLK_HPM_APBIF_FSYS1,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_STAT_PCLK_COMBO_PHY_WIFI,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_STAT_SCLK_MMC2,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_STAT_SCLK_UFSUNIPRO_SDCARD,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_STAT_SCLK_UFSUNIPRO_SDCARD_CFG,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_STAT_SCLK_FSYS1_PCIE0_PHY,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_STAT_SCLK_FSYS1_PCIE1_PHY,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_STAT_SCLK_PCIE_LINK_WIFI0,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_STAT_SCLK_PCIE_LINK_WIFI1,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_STAT_PHYCLK_UFS_LINK_SDCARD_TX0_SYMBOL,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_STAT_PHYCLK_UFS_LINK_SDCARD_RX0_SYMBOL,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_STAT_PHYCLK_PCIE_WIFI0_TX0,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_STAT_PHYCLK_PCIE_WIFI0_RX0,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_STAT_PHYCLK_PCIE_WIFI1_TX0,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_STAT_PHYCLK_PCIE_WIFI1_RX0,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_STAT_PHYCLK_PCIE_WIFI0_DIG_REFCLK,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_STAT_PHYCLK_PCIE_WIFI1_DIG_REFCLK,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_STAT_PHYCLK_UFS_LINK_SDCARD_RX_PWM_CLK,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_STAT_PHYCLK_UFS_LINK_SDCARD_TX_PWM_CLK,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_STAT_PHYCLK_UFS_LINK_SDCARD_REFCLK_OUT_SOC,	FSYS1_STATUS,	0,	0},
	{CG_CTRL_STAT_SCLK_PROMISE_FSYS1,	FSYS1_STATUS,	0,	0},
	{QCH_CTRL_AXI_LH_ASYNC_MI_TOP_FSYS1,	FSYS1_STATUS,	0,	0},
	{QCH_CTRL_CMU_FSYS1,	FSYS1_STATUS,	0,	0},
	{QCH_CTRL_PMU_FSYS1,	FSYS1_STATUS,	0,	0},
	{QCH_CTRL_SYSREG_FSYS1,	FSYS1_STATUS,	0,	0},
	{QCH_CTRL_MMC2,	FSYS1_STATUS,	0,	0},
	{QCH_CTRL_UFS_LINK_SDCARD,	FSYS1_STATUS,	0,	0},
	{QCH_CTRL_PPMU_FSYS1,	FSYS1_STATUS,	0,	0},
	{QCH_CTRL_ACEL_LH_ASYNC_SI_TOP_FSYS1,	FSYS1_STATUS,	0,	0},
	{QSTATE_CTRL_SROMC_FSYS1,	FSYS1_STATUS,	0,	0},
	{QSTATE_CTRL_GPIO_FSYS1,	FSYS1_STATUS,	0,	0},
	{QSTATE_CTRL_HPM_APBIF_FSYS1,	FSYS1_STATUS,	0,	0},
	{QSTATE_CTRL_PROMISE_FSYS1,	FSYS1_STATUS,	0,	0},
	{QSTATE_CTRL_PCIE_RC_LINK_WIFI0,	FSYS1_STATUS,	0,	0},
	{QSTATE_CTRL_PCIE_RC_LINK_WIFI1,	FSYS1_STATUS,	0,	0},
	{QSTATE_CTRL_PCIE_PCS_WIFI0,	FSYS1_STATUS,	0,	0},
	{QSTATE_CTRL_PCIE_PCS_WIFI1,	FSYS1_STATUS,	0,	0},
	{QSTATE_CTRL_PCIE_PHY_FSYS1_WIFI0,	FSYS1_STATUS,	0,	0},
	{QSTATE_CTRL_PCIE_PHY_FSYS1_WIFI1,	FSYS1_STATUS,	0,	0},
	{QSTATE_CTRL_UFS_LINK_SDCARD,	FSYS1_STATUS,	0,	0},
	{CLK_CON_MUX_G3D_PLL_USER,	G3D_STATUS,	0,	0},
	{CLK_CON_MUX_BUS_PLL_USER_G3D,	G3D_STATUS,	0,	0},
	{CLK_CON_MUX_G3D,	G3D_STATUS,	0,	0},
	{CLK_CON_MUX_ACLK_G3D,	G3D_STATUS,	0,	0},
	{CLK_CON_DIV_ACLK_G3D,	G3D_STATUS,	0,	0},
	{CLK_CON_DIV_PCLK_G3D,	G3D_STATUS,	0,	0},
	{CLK_CON_DIV_SCLK_HPM_G3D,	G3D_STATUS,	0,	0},
	{CLK_CON_DIV_SCLK_ATE_G3D,	G3D_STATUS,	0,	0},
	{CLK_ENABLE_ACLK_G3D,	G3D_STATUS,	0,	0},
	{CLK_ENABLE_ACLK_G3D_BUS,	G3D_STATUS,	0,	0},
	{CLK_ENABLE_PCLK_G3D,	G3D_STATUS,	0,	0},
	{CLK_ENABLE_SCLK_HPM_G3D,	G3D_STATUS,	0,	0},
	{CLK_ENABLE_SCLK_ATE_G3D,	G3D_STATUS,	0,	0},
	{CLKOUT_CMU_G3D,	G3D_STATUS,	0,	0},
	{CLKOUT_CMU_G3D_DIV_STAT,	G3D_STATUS,	0,	0},
	{CLK_ENABLE_PDN_G3D,	G3D_STATUS,	0,	0},
	{G3D_SFR_IGNORE_REQ_SYSCLK,	G3D_STATUS,	0,	0},
	{CLK_STOPCTRL_G3D,	G3D_STATUS,	0,	0},
	{CMU_G3D_SPARE0,	G3D_STATUS,	0,	0},
	{CMU_G3D_SPARE1,	G3D_STATUS,	0,	0},
	{CLK_CON_MUX_ACLK_IMEM_266_USER,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_IMEM_200_USER,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_IMEM_100_USER,	NULL,	0,	0},
	{CG_CTRL_VAL_ACLK_IMEM_266,	NULL,	0,	0},
	{CG_CTRL_VAL_ACLK_IMEM_266_SECURE_SSS,	NULL,	0,	0},
	{CG_CTRL_VAL_ACLK_IMEM_266_SECURE_RTIC,	NULL,	0,	0},
	{CG_CTRL_VAL_ACLK_IMEM_200,	NULL,	0,	0},
	{CG_CTRL_VAL_PCLK_IMEM_200_SECURE_SSS,	NULL,	0,	0},
	{CG_CTRL_VAL_PCLK_IMEM_200_SECURE_RTIC,	NULL,	0,	0},
	{CG_CTRL_VAL_ACLK_IMEM_100,	NULL,	0,	0},
	{CG_CTRL_VAL_ACLK_IMEM_100_SECURE_CM3_APM,	NULL,	0,	0},
	{CG_CTRL_VAL_ACLK_IMEM_100_SECURE_AHB_BUSMATRIX_APM,	NULL,	0,	0},
	{CG_CTRL_VAL_ACLK_IMEM_100_SECURE_ISRAMC_APM,	NULL,	0,	0},
	{CG_CTRL_VAL_ACLK_IMEM_100_SECURE_AHB2APB_BRIDGE_APM,	NULL,	0,	0},
	{CG_CTRL_VAL_ACLK_IMEM_100_SECURE_MAILBOX_APM,	NULL,	0,	0},
	{CG_CTRL_VAL_ACLK_IMEM_100_SECURE_TIMER_APM,	NULL,	0,	0},
	{CG_CTRL_VAL_ACLK_IMEM_100_SECURE_WDT_APM,	NULL,	0,	0},
	{CG_CTRL_VAL_ACLK_IMEM_100_SECURE_ISRAMC_SFR_APM,	NULL,	0,	0},
	{CG_CTRL_VAL_ACLK_IMEM_100_SECURE_SFR_APM,	NULL,	0,	0},
	{CLKOUT_CMU_IMEM,	NULL,	0,	0},
	{CLKOUT_CMU_IMEM_DIV_STAT,	NULL,	0,	0},
	{CMU_IMEM_SPARE0,	NULL,	0,	0},
	{CMU_IMEM_SPARE1,	NULL,	0,	0},
	{IMEM_SFR_IGNORE_REQ_SYSCLK,	NULL,	0,	0},
	{CG_CTRL_MAN_ACLK_IMEM_266,	NULL,	0,	0},
	{CG_CTRL_MAN_ACLK_IMEM_266_SECURE_SSS,	NULL,	0,	0},
	{CG_CTRL_MAN_ACLK_IMEM_266_SECURE_RTIC,	NULL,	0,	0},
	{CG_CTRL_MAN_ACLK_IMEM_200,	NULL,	0,	0},
	{CG_CTRL_MAN_PCLK_IMEM_200_SECURE_SSS,	NULL,	0,	0},
	{CG_CTRL_MAN_PCLK_IMEM_200_SECURE_RTIC,	NULL,	0,	0},
	{CG_CTRL_MAN_ACLK_IMEM_100,	NULL,	0,	0},
	{CG_CTRL_MAN_ACLK_IMEM_100_SECURE_CM3_APM,	NULL,	0,	0},
	{CG_CTRL_MAN_ACLK_IMEM_100_SECURE_AHB_BUSMATRIX_APM,	NULL,	0,	0},
	{CG_CTRL_MAN_ACLK_IMEM_100_SECURE_ISRAMC_APM,	NULL,	0,	0},
	{CG_CTRL_MAN_ACLK_IMEM_100_SECURE_AHB2APB_BRIDGE_APM,	NULL,	0,	0},
	{CG_CTRL_MAN_ACLK_IMEM_100_SECURE_MAILBOX_APM,	NULL,	0,	0},
	{CG_CTRL_MAN_ACLK_IMEM_100_SECURE_TIMER_APM,	NULL,	0,	0},
	{CG_CTRL_MAN_ACLK_IMEM_100_SECURE_WDT_APM,	NULL,	0,	0},
	{CG_CTRL_MAN_ACLK_IMEM_100_SECURE_ISRAMC_SFR_APM,	NULL,	0,	0},
	{CG_CTRL_MAN_ACLK_IMEM_100_SECURE_SFR_APM,	NULL,	0,	0},
	{CG_CTRL_STAT_ACLK_IMEM_266_0,	NULL,	0,	0},
	{CG_CTRL_STAT_ACLK_IMEM_266_1,	NULL,	0,	0},
	{CG_CTRL_STAT_ACLK_IMEM_266_SECURE_SSS,	NULL,	0,	0},
	{CG_CTRL_STAT_ACLK_IMEM_266_SECURE_RTIC,	NULL,	0,	0},
	{CG_CTRL_STAT_ACLK_IMEM_200_0,	NULL,	0,	0},
	{CG_CTRL_STAT_ACLK_IMEM_200_1,	NULL,	0,	0},
	{CG_CTRL_STAT_ACLK_IMEM_200_2,	NULL,	0,	0},
	{CG_CTRL_STAT_PCLK_IMEM_200_SECURE_SSS,	NULL,	0,	0},
	{CG_CTRL_STAT_PCLK_IMEM_200_SECURE_RTIC,	NULL,	0,	0},
	{CG_CTRL_STAT_ACLK_IMEM_100,	NULL,	0,	0},
	{CG_CTRL_STAT_ACLK_IMEM_100_SECURE_CM3_APM,	NULL,	0,	0},
	{CG_CTRL_STAT_ACLK_IMEM_100_SECURE_AHB_BUSMATRIX_APM,	NULL,	0,	0},
	{CG_CTRL_STAT_ACLK_IMEM_100_SECURE_ISRAMC_APM,	NULL,	0,	0},
	{CG_CTRL_STAT_ACLK_IMEM_100_SECURE_AHB2APB_BRIDGE_APM,	NULL,	0,	0},
	{CG_CTRL_STAT_ACLK_IMEM_100_SECURE_MAILBOX_APM,	NULL,	0,	0},
	{CG_CTRL_STAT_ACLK_IMEM_100_SECURE_TIMER_APM,	NULL,	0,	0},
	{CG_CTRL_STAT_ACLK_IMEM_100_SECURE_WDT_APM,	NULL,	0,	0},
	{CG_CTRL_STAT_ACLK_IMEM_100_SECURE_ISRAMC_SFR_APM,	NULL,	0,	0},
	{CG_CTRL_STAT_ACLK_IMEM_100_SECURE_SFR_APM,	NULL,	0,	0},
	{QCH_CTRL_AXI_LH_ASYNC_MI_IMEM,	NULL,	0,	0},
	{QCH_CTRL_SSS,	NULL,	0,	0},
	{QCH_CTRL_RTIC,	NULL,	0,	0},
	{QCH_CTRL_INT_MEM,	NULL,	0,	0},
	{QCH_CTRL_INT_MEM_ALV,	NULL,	0,	0},
	{QCH_CTRL_MCOMP,	NULL,	0,	0},
	{QCH_CTRL_CMU_IMEM,	NULL,	0,	0},
	{QCH_CTRL_PMU_IMEM,	NULL,	0,	0},
	{QCH_CTRL_SYSREG_IMEM,	NULL,	0,	0},
	{QCH_CTRL_PPMU_SSSX,	NULL,	0,	0},
	{QCH_CTRL_LH_ASYNC_SI_IMEM,	NULL,	0,	0},
	{QSTATE_CTRL_GIC,	NULL,	0,	0},
	{QSTATE_CTRL_APM,	NULL,	0,	0},
	{QSTATE_CTRL_ASYNCAHBM_SSS_ATLAS,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_ISP0_528_USER,	ISP0_STATUS,	0,	0},
	{CLK_CON_MUX_ACLK_ISP0_TPU_400_USER,	ISP0_STATUS,	0,	0},
	{CLK_CON_MUX_ACLK_ISP0_TREX_528_USER,	ISP0_STATUS,	0,	0},
	{CLK_CON_DIV_PCLK_ISP0,	ISP0_STATUS,	0,	0},
	{CLK_CON_DIV_PCLK_ISP0_TPU,	ISP0_STATUS,	0,	0},
	{CLK_CON_DIV_PCLK_ISP0_TREX_264,	ISP0_STATUS,	0,	0},
	{CLK_CON_DIV_PCLK_ISP0_TREX_132,	ISP0_STATUS,	0,	0},
	{CLK_ENABLE_ACLK_ISP0,	ISP0_STATUS,	0,	0},
	{CLK_ENABLE_PCLK_ISP0,	ISP0_STATUS,	0,	0},
	{CLK_ENABLE_ACLK_ISP0_TPU,	ISP0_STATUS,	0,	0},
	{CLK_ENABLE_PCLK_ISP0_TPU,	ISP0_STATUS,	0,	0},
	{CLK_ENABLE_ACLK_ISP0_TREX,	ISP0_STATUS,	0,	0},
	{CLK_ENABLE_PCLK_TREX_264,	ISP0_STATUS,	0,	0},
	{CLK_ENABLE_PCLK_HPM_APBIF_ISP0,	ISP0_STATUS,	0,	0},
	{CLK_ENABLE_PCLK_TREX_132,	ISP0_STATUS,	0,	0},
	{CLK_ENABLE_SCLK_PROMISE_ISP0,	ISP0_STATUS,	0,	0},
	{CLKOUT_CMU_ISP0,	ISP0_STATUS,	0,	0},
	{CLKOUT_CMU_ISP0_DIV_STAT,	ISP0_STATUS,	0,	0},
	{CMU_ISP0_SPARE0,	ISP0_STATUS,	0,	0},
	{CMU_ISP0_SPARE1,	ISP0_STATUS,	0,	0},
	{CLK_ENABLE_PDN_ISP0,	ISP0_STATUS,	0,	0},
	{ISP0_SFR_IGNORE_REQ_SYSCLK,	ISP0_STATUS,	0,	0},
	{CLK_ENABLE_ACLK_ISP0_LOCAL,	ISP0_STATUS,	0,	0},
	{CLK_ENABLE_PCLK_ISP0_LOCAL,	ISP0_STATUS,	0,	0},
	{CLK_ENABLE_ACLK_ISP0_TPU_LOCAL,	ISP0_STATUS,	0,	0},
	{CLK_ENABLE_PCLK_ISP0_TPU_LOCAL,	ISP0_STATUS,	0,	0},
	{CLK_ENABLE_ACLK_ISP0_TREX_LOCAL,	ISP0_STATUS,	0,	0},
	{CLK_ENABLE_PCLK_TREX_132_LOCAL,	ISP0_STATUS,	0,	0},
	{CLK_CON_MUX_ACLK_ISP1_468_USER,	ISP1_STATUS,	0,	0},
	{CLK_CON_DIV_PCLK_ISP1_234,	ISP1_STATUS,	0,	0},
	{CLK_ENABLE_ACLK_ISP1,	ISP1_STATUS,	0,	0},
	{CLK_ENABLE_PCLK_ISP1_234,	ISP1_STATUS,	0,	0},
	{CLK_ENABLE_PCLK_HPM_APBIF_ISP1,	ISP1_STATUS,	0,	0},
	{CLK_ENABLE_SCLK_PROMISE_ISP1,	ISP1_STATUS,	0,	0},
	{CLKOUT_CMU_ISP1,	ISP1_STATUS,	0,	0},
	{CLKOUT_CMU_ISP1_DIV_STAT,	ISP1_STATUS,	0,	0},
	{CMU_ISP1_SPARE0,	ISP1_STATUS,	0,	0},
	{CMU_ISP1_SPARE1,	ISP1_STATUS,	0,	0},
	{CLK_ENABLE_PDN_ISP1,	ISP1_STATUS,	0,	0},
	{ISP1_SFR_IGNORE_REQ_SYSCLK,	ISP1_STATUS,	0,	0},
	{CLK_ENABLE_ACLK_ISP1_LOCAL,	ISP1_STATUS,	0,	0},
	{CLK_ENABLE_PCLK_ISP1_234_LOCAL,	ISP1_STATUS,	0,	0},
	{CLK_CON_MUX_ACLK_MFC_600_USER,	MFC_STATUS,	0,	0},
	{CLK_CON_DIV_PCLK_MFC_150,	MFC_STATUS,	0,	0},
	{CG_CTRL_VAL_ACLK_MFC_600,	MFC_STATUS,	0,	0},
	{CG_CTRL_VAL_ACLK_MFC_600_SECURE_SFW_MFC_0,	MFC_STATUS,	0,	0},
	{CG_CTRL_VAL_ACLK_MFC_600_SECURE_SFW_MFC_1,	MFC_STATUS,	0,	0},
	{CG_CTRL_VAL_PCLK_MFC_150,	MFC_STATUS,	0,	0},
	{CG_CTRL_VAL_PCLK_MFC_150_HPM_APBIF_MFC,	MFC_STATUS,	0,	0},
	{CG_CTRL_VAL_PCLK_MFC_150_SECURE_SFW_MFC_0,	MFC_STATUS,	0,	0},
	{CG_CTRL_VAL_PCLK_MFC_150_SECURE_SFW_MFC_1,	MFC_STATUS,	0,	0},
	{CG_CTRL_VAL_SCLK_MFC_PROMISE,	MFC_STATUS,	0,	0},
	{CLKOUT_CMU_MFC,	MFC_STATUS,	0,	0},
	{CLKOUT_CMU_MFC_DIV_STAT,	MFC_STATUS,	0,	0},
	{MFC_SFR_IGNORE_REQ_SYSCLK,	MFC_STATUS,	0,	0},
	{CMU_MFC_SPARE0,	MFC_STATUS,	0,	0},
	{CMU_MFC_SPARE1,	MFC_STATUS,	0,	0},
	{CG_CTRL_MAN_ACLK_MFC_600,	MFC_STATUS,	0,	0},
	{CG_CTRL_MAN_ACLK_MFC_600_SECURE_SFW_MFC_0,	MFC_STATUS,	0,	0},
	{CG_CTRL_MAN_ACLK_MFC_600_SECURE_SFW_MFC_1,	MFC_STATUS,	0,	0},
	{CG_CTRL_MAN_PCLK_MFC_150,	MFC_STATUS,	0,	0},
	{CG_CTRL_MAN_PCLK_MFC_150_HPM_APBIF_MFC,	MFC_STATUS,	0,	0},
	{CG_CTRL_MAN_PCLK_MFC_150_SECURE_SFW_MFC_0,	MFC_STATUS,	0,	0},
	{CG_CTRL_MAN_PCLK_MFC_150_SECURE_SFW_MFC_1,	MFC_STATUS,	0,	0},
	{CG_CTRL_MAN_SCLK_MFC_PROMISE,	MFC_STATUS,	0,	0},
	{CG_CTRL_STAT_ACLK_MFC_600,	MFC_STATUS,	0,	0},
	{CG_CTRL_STAT_ACLK_MFC_600_SECURE_SFW_MFC_0,	MFC_STATUS,	0,	0},
	{CG_CTRL_STAT_ACLK_MFC_600_SECURE_SFW_MFC_1,	MFC_STATUS,	0,	0},
	{CG_CTRL_STAT_PCLK_MFC_150_0,	MFC_STATUS,	0,	0},
	{CG_CTRL_STAT_PCLK_MFC_150_1,	MFC_STATUS,	0,	0},
	{CG_CTRL_STAT_PCLK_MFC_150_HPM_APBIF_MFC,	MFC_STATUS,	0,	0},
	{CG_CTRL_STAT_PCLK_MFC_150_SECURE_SFW_MFC_0,	MFC_STATUS,	0,	0},
	{CG_CTRL_STAT_PCLK_MFC_150_SECURE_SFW_MFC_1,	MFC_STATUS,	0,	0},
	{CG_CTRL_STAT_SCLK_MFC_PROMISE,	MFC_STATUS,	0,	0},
	{QCH_CTRL_MFC,	MFC_STATUS,	0,	0},
	{QCH_CTRL_LH_M_MFC,	MFC_STATUS,	0,	0},
	{QCH_CTRL_CMU_MFC,	MFC_STATUS,	0,	0},
	{QCH_CTRL_PMU_MFC,	MFC_STATUS,	0,	0},
	{QCH_CTRL_SYSREG_MFC,	MFC_STATUS,	0,	0},
	{QCH_CTRL_PPMU_MFC_0,	MFC_STATUS,	0,	0},
	{QCH_CTRL_PPMU_MFC_1,	MFC_STATUS,	0,	0},
	{QCH_CTRL_SFW_MFC_0,	MFC_STATUS,	0,	0},
	{QCH_CTRL_SFW_MFC_1,	MFC_STATUS,	0,	0},
	{QCH_CTRL_SMMU_MFC_0,	MFC_STATUS,	0,	0},
	{QCH_CTRL_SMMU_MFC_1,	MFC_STATUS,	0,	0},
	{QCH_CTRL_LH_S_MFC_0,	MFC_STATUS,	0,	0},
	{QCH_CTRL_LH_S_MFC_1,	MFC_STATUS,	0,	0},
	{QSTATE_CTRL_HPM_APBIF_MFC,	MFC_STATUS,	0,	0},
	{QSTATE_CTRL_PROMISE_MFC,	MFC_STATUS,	0,	0},
	{CLK_CON_MUX_ACLK_MSCL0_528_USER,	MSCL_STATUS,	0,	0},
	{CLK_CON_MUX_ACLK_MSCL1_528_USER,	MSCL_STATUS,	0,	0},
	{CLK_CON_MUX_ACLK_MSCL1_528_MSCL,	MSCL_STATUS,	0,	0},
	{CLK_CON_DIV_PCLK_MSCL,	MSCL_STATUS,	0,	0},
	{CG_CTRL_VAL_ACLK_MSCL0_528,	MSCL_STATUS,	0,	0},
	{CG_CTRL_VAL_ACLK_MSCL0_528_SECURE_SFW_MSCL_0,	MSCL_STATUS,	0,	0},
	{CG_CTRL_VAL_ACLK_MSCL1_528,	MSCL_STATUS,	0,	0},
	{CG_CTRL_VAL_ACLK_MSCL1_528_SECURE_SFW_MSCL_1,	MSCL_STATUS,	0,	0},
	{CG_CTRL_VAL_PCLK_MSCL,	MSCL_STATUS,	0,	0},
	{CG_CTRL_VAL_PCLK_MSCL_SECURE_SFW_MSCL_0,	MSCL_STATUS,	0,	0},
	{CG_CTRL_VAL_PCLK_MSCL_SECURE_SFW_MSCL_1,	MSCL_STATUS,	0,	0},
	{CLKOUT_CMU_MSCL,	MSCL_STATUS,	0,	0},
	{CLKOUT_CMU_MSCL_DIV_STAT,	MSCL_STATUS,	0,	0},
	{MSCL_SFR_IGNORE_REQ_SYSCLK,	MSCL_STATUS,	0,	0},
	{CMU_MSCL_SPARE0,	MSCL_STATUS,	0,	0},
	{CMU_MSCL_SPARE1,	MSCL_STATUS,	0,	0},
	{CG_CTRL_MAN_ACLK_MSCL0_528,	MSCL_STATUS,	0,	0},
	{CG_CTRL_MAN_ACLK_MSCL0_528_SECURE_SFW_MSCL_0,	MSCL_STATUS,	0,	0},
	{CG_CTRL_MAN_ACLK_MSCL1_528,	MSCL_STATUS,	0,	0},
	{CG_CTRL_MAN_ACLK_MSCL1_528_SECURE_SFW_MSCL_1,	MSCL_STATUS,	0,	0},
	{CG_CTRL_MAN_PCLK_MSCL,	MSCL_STATUS,	0,	0},
	{CG_CTRL_MAN_PCLK_MSCL_SECURE_SFW_MSCL_0,	MSCL_STATUS,	0,	0},
	{CG_CTRL_MAN_PCLK_MSCL_SECURE_SFW_MSCL_1,	MSCL_STATUS,	0,	0},
	{CG_CTRL_STAT_ACLK_MSCL0_528_0,	MSCL_STATUS,	0,	0},
	{CG_CTRL_STAT_ACLK_MSCL0_528_1,	MSCL_STATUS,	0,	0},
	{CG_CTRL_STAT_ACLK_MSCL0_528_SECURE_SFW_MSCL_0,	MSCL_STATUS,	0,	0},
	{CG_CTRL_STAT_ACLK_MSCL1_528_0,	MSCL_STATUS,	0,	0},
	{CG_CTRL_STAT_ACLK_MSCL1_528_1,	MSCL_STATUS,	0,	0},
	{CG_CTRL_STAT_ACLK_MSCL1_528_SECURE_SFW_MSCL_1,	MSCL_STATUS,	0,	0},
	{CG_CTRL_STAT_PCLK_MSCL_0,	MSCL_STATUS,	0,	0},
	{CG_CTRL_STAT_PCLK_MSCL_1,	MSCL_STATUS,	0,	0},
	{CG_CTRL_STAT_PCLK_MSCL_2,	MSCL_STATUS,	0,	0},
	{CG_CTRL_STAT_PCLK_MSCL_SECURE_SFW_MSCL_0,	MSCL_STATUS,	0,	0},
	{CG_CTRL_STAT_PCLK_MSCL_SECURE_SFW_MSCL_1,	MSCL_STATUS,	0,	0},
	{QCH_CTRL_LH_ASYNC_MI_MSCLSFR,	MSCL_STATUS,	0,	0},
	{QCH_CTRL_CMU_MSCL,	MSCL_STATUS,	0,	0},
	{QCH_CTRL_PMU_MSCL,	MSCL_STATUS,	0,	0},
	{QCH_CTRL_SYSREG_MSCL,	MSCL_STATUS,	0,	0},
	{QCH_CTRL_MSCL_0,	MSCL_STATUS,	0,	0},
	{QCH_CTRL_MSCL_1,	MSCL_STATUS,	0,	0},
	{QCH_CTRL_JPEG,	MSCL_STATUS,	0,	0},
	{QCH_CTRL_G2D,	MSCL_STATUS,	0,	0},
	{QCH_CTRL_SMMU_MSCL_0,	MSCL_STATUS,	0,	0},
	{QCH_CTRL_SMMU_MSCL_1,	MSCL_STATUS,	0,	0},
	{QCH_CTRL_SMMU_JPEG,	MSCL_STATUS,	0,	0},
	{QCH_CTRL_SMMU_G2D,	MSCL_STATUS,	0,	0},
	{QCH_CTRL_PPMU_MSCL_0,	MSCL_STATUS,	0,	0},
	{QCH_CTRL_PPMU_MSCL_1,	MSCL_STATUS,	0,	0},
	{QCH_CTRL_SFW_MSCL_0,	MSCL_STATUS,	0,	0},
	{QCH_CTRL_SFW_MSCL_1,	MSCL_STATUS,	0,	0},
	{QCH_CTRL_LH_ASYNC_SI_MSCL_0,	MSCL_STATUS,	0,	0},
	{QCH_CTRL_LH_ASYNC_SI_MSCL_1,	MSCL_STATUS,	0,	0},
	{CLK_CON_MUX_ACLK_PERIC0_66_USER,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_UART0_USER,	NULL,	0,	0},
	{CG_CTRL_VAL_ACLK_PERIC0_66,	NULL,	0,	0},
	{CG_CTRL_VAL_SCLK_UART0,	NULL,	0,	0},
	{CG_CTRL_VAL_SCLK_PWM,	NULL,	0,	0},
	{CLKOUT_CMU_PERIC0,	NULL,	0,	0},
	{CLKOUT_CMU_PERIC0_DIV_STAT,	NULL,	0,	0},
	{PERIC0_SFR_IGNORE_REQ_SYSCLK,	NULL,	0,	0},
	{CMU_PERIC0_SPARE0,	NULL,	0,	0},
	{CMU_PERIC0_SPARE1,	NULL,	0,	0},
	{CG_CTRL_MAN_ACLK_PERIC0_66,	NULL,	0,	0},
	{CG_CTRL_MAN_SCLK_UART0,	NULL,	0,	0},
	{CG_CTRL_MAN_SCLK_PWM,	NULL,	0,	0},
	{CG_CTRL_STAT_ACLK_PERIC0_66_0,	NULL,	0,	0},
	{CG_CTRL_STAT_ACLK_PERIC0_66_1,	NULL,	0,	0},
	{CG_CTRL_STAT_SCLK_UART0,	NULL,	0,	0},
	{CG_CTRL_STAT_SCLK_PWM,	NULL,	0,	0},
	{QCH_CTRL_AXILHASYNCM_PERIC0,	NULL,	0,	0},
	{QCH_CTRL_CMU_PERIC0,	NULL,	0,	0},
	{QCH_CTRL_PMU_PERIC0,	NULL,	0,	0},
	{QCH_CTRL_SYSREG_PERIC0,	NULL,	0,	0},
	{QSTATE_CTRL_GPIO_BUS0,	NULL,	0,	0},
	{QSTATE_CTRL_UART0,	NULL,	0,	0},
	{QSTATE_CTRL_ADCIF,	NULL,	0,	0},
	{QSTATE_CTRL_PWM,	NULL,	0,	0},
	{QSTATE_CTRL_HSI2C0,	NULL,	0,	0},
	{QSTATE_CTRL_HSI2C1,	NULL,	0,	0},
	{QSTATE_CTRL_HSI2C4,	NULL,	0,	0},
	{QSTATE_CTRL_HSI2C5,	NULL,	0,	0},
	{QSTATE_CTRL_HSI2C9,	NULL,	0,	0},
	{QSTATE_CTRL_HSI2C10,	NULL,	0,	0},
	{QSTATE_CTRL_HSI2C11,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_PERIC1_66_USER,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_SPI0_USER,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_SPI1_USER,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_SPI2_USER,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_SPI3_USER,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_SPI4_USER,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_SPI5_USER,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_SPI6_USER,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_SPI7_USER,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_UART1_USER,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_UART2_USER,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_UART3_USER,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_UART4_USER,	NULL,	0,	0},
	{CLK_CON_MUX_SCLK_UART5_USER,	NULL,	0,	0},
	{CG_CTRL_VAL_ACLK_PERIC1_66,	NULL,	0,	0},
	{CG_CTRL_VAL_ACLK_PERIC1_66_HSI2C,	NULL,	0,	0},
	{CG_CTRL_VAL_SCLK_SPI0,	NULL,	0,	0},
	{CG_CTRL_VAL_SCLK_SPI1,	NULL,	0,	0},
	{CG_CTRL_VAL_SCLK_SPI2,	NULL,	0,	0},
	{CG_CTRL_VAL_SCLK_SPI3,	NULL,	0,	0},
	{CG_CTRL_VAL_SCLK_SPI4,	NULL,	0,	0},
	{CG_CTRL_VAL_SCLK_SPI5,	NULL,	0,	0},
	{CG_CTRL_VAL_SCLK_SPI6,	NULL,	0,	0},
	{CG_CTRL_VAL_SCLK_SPI7,	NULL,	0,	0},
	{CG_CTRL_VAL_SCLK_UART1,	NULL,	0,	0},
	{CG_CTRL_VAL_SCLK_UART2,	NULL,	0,	0},
	{CG_CTRL_VAL_SCLK_UART3,	NULL,	0,	0},
	{CG_CTRL_VAL_SCLK_UART4,	NULL,	0,	0},
	{CG_CTRL_VAL_SCLK_UART5,	NULL,	0,	0},
	{CLKOUT_CMU_PERIC1,	NULL,	0,	0},
	{CLKOUT_CMU_PERIC1_DIV_STAT,	NULL,	0,	0},
	{PERIC1_SFR_IGNORE_REQ_SYSCLK,	NULL,	0,	0},
	{CMU_PERIC1_SPARE0,	NULL,	0,	0},
	{CMU_PERIC1_SPARE1,	NULL,	0,	0},
	{CG_CTRL_MAN_ACLK_PERIC1_66,	NULL,	0,	0},
	{CG_CTRL_MAN_ACLK_PERIC1_66_HSI2C,	NULL,	0,	0},
	{CG_CTRL_MAN_SCLK_SPI0,	NULL,	0,	0},
	{CG_CTRL_MAN_SCLK_SPI1,	NULL,	0,	0},
	{CG_CTRL_MAN_SCLK_SPI2,	NULL,	0,	0},
	{CG_CTRL_MAN_SCLK_SPI3,	NULL,	0,	0},
	{CG_CTRL_MAN_SCLK_SPI4,	NULL,	0,	0},
	{CG_CTRL_MAN_SCLK_SPI5,	NULL,	0,	0},
	{CG_CTRL_MAN_SCLK_SPI6,	NULL,	0,	0},
	{CG_CTRL_MAN_SCLK_SPI7,	NULL,	0,	0},
	{CG_CTRL_MAN_SCLK_UART1,	NULL,	0,	0},
	{CG_CTRL_MAN_SCLK_UART2,	NULL,	0,	0},
	{CG_CTRL_MAN_SCLK_UART3,	NULL,	0,	0},
	{CG_CTRL_MAN_SCLK_UART4,	NULL,	0,	0},
	{CG_CTRL_MAN_SCLK_UART5,	NULL,	0,	0},
	{CG_CTRL_STAT_ACLK_PERIC1_66_0,	NULL,	0,	0},
	{CG_CTRL_STAT_ACLK_PERIC1_66_1,	NULL,	0,	0},
	{CG_CTRL_STAT_ACLK_PERIC1_66_2,	NULL,	0,	0},
	{CG_CTRL_STAT_ACLK_PERIC1_66_3,	NULL,	0,	0},
	{CG_CTRL_STAT_ACLK_PERIC1_66_HSI2C,	NULL,	0,	0},
	{CG_CTRL_STAT_SCLK_SPI0,	NULL,	0,	0},
	{CG_CTRL_STAT_SCLK_SPI1,	NULL,	0,	0},
	{CG_CTRL_STAT_SCLK_SPI2,	NULL,	0,	0},
	{CG_CTRL_STAT_SCLK_SPI3,	NULL,	0,	0},
	{CG_CTRL_STAT_SCLK_SPI4,	NULL,	0,	0},
	{CG_CTRL_STAT_SCLK_SPI5,	NULL,	0,	0},
	{CG_CTRL_STAT_SCLK_SPI6,	NULL,	0,	0},
	{CG_CTRL_STAT_SCLK_SPI7,	NULL,	0,	0},
	{CG_CTRL_STAT_SCLK_UART1,	NULL,	0,	0},
	{CG_CTRL_STAT_SCLK_UART2,	NULL,	0,	0},
	{CG_CTRL_STAT_SCLK_UART3,	NULL,	0,	0},
	{CG_CTRL_STAT_SCLK_UART4,	NULL,	0,	0},
	{CG_CTRL_STAT_SCLK_UART5,	NULL,	0,	0},
	{QCH_CTRL_AXILHASYNCM_PERIC1,	NULL,	0,	0},
	{QCH_CTRL_CMU_PERIC1,	NULL,	0,	0},
	{QCH_CTRL_PMU_PERIC1,	NULL,	0,	0},
	{QCH_CTRL_SYSREG_PERIC1,	NULL,	0,	0},
	{QSTATE_CTRL_GPIO_PERIC1,	NULL,	0,	0},
	{QSTATE_CTRL_GPIO_NFC,	NULL,	0,	0},
	{QSTATE_CTRL_GPIO_TOUCH,	NULL,	0,	0},
	{QSTATE_CTRL_GPIO_FF,	NULL,	0,	0},
	{QSTATE_CTRL_GPIO_ESE,	NULL,	0,	0},
	{QSTATE_CTRL_UART1,	NULL,	0,	0},
	{QSTATE_CTRL_UART2,	NULL,	0,	0},
	{QSTATE_CTRL_UART3,	NULL,	0,	0},
	{QSTATE_CTRL_UART4,	NULL,	0,	0},
	{QSTATE_CTRL_UART5,	NULL,	0,	0},
	{QSTATE_CTRL_SPI0,	NULL,	0,	0},
	{QSTATE_CTRL_SPI1,	NULL,	0,	0},
	{QSTATE_CTRL_SPI2,	NULL,	0,	0},
	{QSTATE_CTRL_SPI3,	NULL,	0,	0},
	{QSTATE_CTRL_SPI4,	NULL,	0,	0},
	{QSTATE_CTRL_SPI5,	NULL,	0,	0},
	{QSTATE_CTRL_SPI6,	NULL,	0,	0},
	{QSTATE_CTRL_SPI7,	NULL,	0,	0},
	{QSTATE_CTRL_HSI2C2,	NULL,	0,	0},
	{QSTATE_CTRL_HSI2C3,	NULL,	0,	0},
	{QSTATE_CTRL_HSI2C6,	NULL,	0,	0},
	{QSTATE_CTRL_HSI2C7,	NULL,	0,	0},
	{QSTATE_CTRL_HSI2C8,	NULL,	0,	0},
	{QSTATE_CTRL_HSI2C12,	NULL,	0,	0},
	{QSTATE_CTRL_HSI2C13,	NULL,	0,	0},
	{QSTATE_CTRL_HSI2C14,	NULL,	0,	0},
	{CLK_CON_MUX_ACLK_PERIS_66_USER,	NULL,	0,	0},
	{CG_CTRL_VAL_ACLK_PERIS,	NULL,	0,	0},
	{CG_CTRL_VAL_ACLK_PERIS_HPM_APBIF_PERIS,	NULL,	0,	0},
	{CG_CTRL_VAL_ACLK_PERIS_SECURE_TZPC,	NULL,	0,	0},
	{CG_CTRL_VAL_ACLK_PERIS_SECURE_RTC,	NULL,	0,	0},
	{CG_CTRL_VAL_ACLK_PERIS_SECURE_OTP,	NULL,	0,	0},
	{CG_CTRL_VAL_ACLK_PERIS_SECURE_CHIPID,	NULL,	0,	0},
	{CG_CTRL_VAL_SCLK_PERIS_SECURE_OTP,	NULL,	0,	0},
	{CG_CTRL_VAL_SCLK_PERIS_SECURE_CHIPID,	NULL,	0,	0},
	{CG_CTRL_VAL_SCLK_PERIS,	NULL,	0,	0},
	{CG_CTRL_VAL_SCLK_PERIS_PROMISE,	NULL,	0,	0},
	{CLKOUT_CMU_PERIS,	NULL,	0,	0},
	{CLKOUT_CMU_PERIS_DIV_STAT,	NULL,	0,	0},
	{PERIS_SFR_IGNORE_REQ_SYSCLK,	NULL,	0,	0},
	{CMU_PERIS_SPARE0,	NULL,	0,	0},
	{CMU_PERIS_SPARE1,	NULL,	0,	0},
	{CG_CTRL_MAN_ACLK_PERIS,	NULL,	0,	0},
	{CG_CTRL_MAN_ACLK_PERIS_HPM_APBIF_PERIS,	NULL,	0,	0},
	{CG_CTRL_MAN_ACLK_PERIS_SECURE_TZPC,	NULL,	0,	0},
	{CG_CTRL_MAN_ACLK_PERIS_SECURE_RTC,	NULL,	0,	0},
	{CG_CTRL_MAN_ACLK_PERIS_SECURE_OTP,	NULL,	0,	0},
	{CG_CTRL_MAN_ACLK_PERIS_SECURE_CHIPID,	NULL,	0,	0},
	{CG_CTRL_MAN_SCLK_PERIS_SECURE_OTP,	NULL,	0,	0},
	{CG_CTRL_MAN_SCLK_PERIS_SECURE_CHIPID,	NULL,	0,	0},
	{CG_CTRL_MAN_SCLK_PERIS,	NULL,	0,	0},
	{CG_CTRL_MAN_SCLK_PERIS_PROMISE,	NULL,	0,	0},
	{CG_CTRL_STAT_ACLK_PERIS_0,	NULL,	0,	0},
	{CG_CTRL_STAT_ACLK_PERIS_1,	NULL,	0,	0},
	{CG_CTRL_STAT_ACLK_PERIS_HPM_APBIF_PERIS,	NULL,	0,	0},
	{CG_CTRL_STAT_ACLK_PERIS_SECURE_TZPC_0,	NULL,	0,	0},
	{CG_CTRL_STAT_ACLK_PERIS_SECURE_TZPC_1,	NULL,	0,	0},
	{CG_CTRL_STAT_ACLK_PERIS_SECURE_RTC,	NULL,	0,	0},
	{CG_CTRL_STAT_ACLK_PERIS_SECURE_OTP,	NULL,	0,	0},
	{CG_CTRL_STAT_ACLK_PERIS_SECURE_CHIPID,	NULL,	0,	0},
	{CG_CTRL_STAT_SCLK_PERIS_SECURE_OTP,	NULL,	0,	0},
	{CG_CTRL_STAT_SCLK_PERIS_SECURE_CHIPID,	NULL,	0,	0},
	{CG_CTRL_STAT_SCLK_PERIS,	NULL,	0,	0},
	{CG_CTRL_STAT_SCLK_PERIS_PROMISE,	NULL,	0,	0},
	{QCH_CTRL_AXILHASYNCM_PERIS,	NULL,	0,	0},
	{QCH_CTRL_CMU_PERIS,	NULL,	0,	0},
	{QCH_CTRL_PMU_PERIS,	NULL,	0,	0},
	{QCH_CTRL_SYSREG_PERIS,	NULL,	0,	0},
	{QCH_CTRL_MONOCNT_APBIF,	NULL,	0,	0},
	{QSTATE_CTRL_MCT,	NULL,	0,	0},
	{QSTATE_CTRL_WDT_MNGS,	NULL,	0,	0},
	{QSTATE_CTRL_WDT_APOLLO,	NULL,	0,	0},
	{QSTATE_CTRL_RTC_APBIF,	NULL,	0,	0},
	{QSTATE_CTRL_SFR_APBIF_TMU,	NULL,	0,	0},
	{QSTATE_CTRL_SFR_APBIF_HDMI_CEC,	NULL,	0,	0},
	{QSTATE_CTRL_HPM_APBIF_PERIS,	NULL,	0,	0},
	{QSTATE_CTRL_TZPC_0,	NULL,	0,	0},
	{QSTATE_CTRL_TZPC_1,	NULL,	0,	0},
	{QSTATE_CTRL_TZPC_2,	NULL,	0,	0},
	{QSTATE_CTRL_TZPC_3,	NULL,	0,	0},
	{QSTATE_CTRL_TZPC_4,	NULL,	0,	0},
	{QSTATE_CTRL_TZPC_5,	NULL,	0,	0},
	{QSTATE_CTRL_TZPC_6,	NULL,	0,	0},
	{QSTATE_CTRL_TZPC_7,	NULL,	0,	0},
	{QSTATE_CTRL_TZPC_8,	NULL,	0,	0},
	{QSTATE_CTRL_TZPC_9,	NULL,	0,	0},
	{QSTATE_CTRL_TZPC_10,	NULL,	0,	0},
	{QSTATE_CTRL_TZPC_11,	NULL,	0,	0},
	{QSTATE_CTRL_TZPC_12,	NULL,	0,	0},
	{QSTATE_CTRL_TZPC_13,	NULL,	0,	0},
	{QSTATE_CTRL_TZPC_14,	NULL,	0,	0},
	{QSTATE_CTRL_TZPC_15,	NULL,	0,	0},
	{QSTATE_CTRL_TOP_RTC,	NULL,	0,	0},
	{QSTATE_CTRL_OTP_CON_TOP,	NULL,	0,	0},
	{QSTATE_CTRL_SFR_APBIF_CHIPID,	NULL,	0,	0},
	{QSTATE_CTRL_TMU,	NULL,	0,	0},
	{QSTATE_CTRL_CHIPID,	NULL,	0,	0},
	{QSTATE_CTRL_PROMISE_PERIS,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_CCORE_800,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_CCORE_264,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_CCORE_G3D_800,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_CCORE_528,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_CCORE_132,	NULL,	0,	0},
	{CLK_ENABLE_PCLK_CCORE_66,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_BUS0_528_TOP,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_BUS0_200_TOP,	NULL,	0,	0},
	{CLK_ENABLE_PCLK_BUS0_132_TOP,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_BUS1_528_TOP,	NULL,	0,	0},
	{CLK_ENABLE_PCLK_BUS1_132_TOP,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_DISP0_0_400,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_DISP0_1_400,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_DISP1_0_400,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_DISP1_1_400,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_MFC_600,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_MSCL0_528,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_MSCL1_528,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_IMEM_266,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_IMEM_200,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_IMEM_100,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_FSYS0_200,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_FSYS1_200,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_PERIS_66,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_PERIC0_66,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_PERIC1_66,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_ISP0_ISP0_528,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_ISP0_TPU_400,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_ISP0_TREX_528,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_ISP1_ISP1_468,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_CAM0_CSIS1_414,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_CAM0_CSIS1_168_TOP,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_CAM0_CSIS2_234_TOP,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_CAM0_3AA0_414_TOP,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_CAM0_3AA1_414_TOP,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_CAM0_CSIS3_132_TOP,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_CAM0_TREX_528_TOP,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_CAM1_ARM_672_TOP,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_CAM1_TREX_VRA_528_TOP,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_CAM1_TREX_B_528_TOP,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_CAM1_BUS_264_TOP,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_CAM1_PERI_84,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_CAM1_CSIS2_414_TOP,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_CAM1_CSIS3_132_TOP,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_CAM1_SCL_566_TOP,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_DISP0_DECON0_ECLK0,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_DISP0_DECON0_VCLK0,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_DISP0_DECON0_VCLK1,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_DISP0_HDMI_ADUIO,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_DISP1_DECON1_ECLK0,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_DISP1_DECON1_ECLK1,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_FSYS0_USBDRD30,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_FSYS0_MMC0,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_FSYS0_UFSUNIPRO20,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_FSYS0_PHY_24M,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_FSYS0_UFSUNIPRO_CFG,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_FSYS1_MMC2,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_FSYS1_UFSUNIPRO20,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_FSYS1_PCIE_PHY,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_FSYS1_UFSUNIPRO_CFG,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_PERIC0_UART0,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_PERIC1_SPI0,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_PERIC1_SPI1,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_PERIC1_SPI2,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_PERIC1_SPI3,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_PERIC1_SPI4,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_PERIC1_SPI5,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_PERIC1_SPI6,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_PERIC1_SPI7,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_PERIC1_UART1,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_PERIC1_UART2,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_PERIC1_UART3,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_PERIC1_UART4,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_PERIC1_UART5,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_CAM1_ISP_SPI0_TOP,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_CAM1_ISP_SPI1_TOP,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_CAM1_ISP_UART_TOP,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_AP2CP_MIF_PLL_OUT,	NULL,	0,	0},
	{CLK_ENABLE_ACLK_PSCDC_400,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_BUS_PLL_MNGS,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_BUS_PLL_APOLLO,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_BUS_PLL_MIF,	NULL,	0,	0},
	{CLK_ENABLE_SCLK_BUS_PLL_G3D,	NULL,	0,	0},
};

static void save_cmusfr(int mode)
{
	int i;

	if (mode == syspwr_sicd || mode == syspwr_sicd_cpd || mode == syspwr_aftr)
		return;

	for (i = 0; i < ARRAY_SIZE(cmu_backup_list); i++) {
		cmu_backup_list[i].backup_valid = 0;
		if (mode == syspwr_sleep) {
			if (cmu_backup_list[i].power) {
				if (pwrcal_getf(cmu_backup_list[i].power, 0, 0xF) != 0xF)
					continue;
			}
		} else {
			if (cmu_backup_list[i].power) {
				if (pwrcal_getf(cmu_backup_list[i].power, 0, 0xF) != 0xF)
					continue;
			} else
				continue;
		}

		cmu_backup_list[i].backup = pwrcal_readl(cmu_backup_list[i].sfr);
		cmu_backup_list[i].backup_valid = 1;
	}
}

static void restore_cmusfr(int mode)
{
	int i;
	if (mode == syspwr_sicd || mode == syspwr_sicd_cpd || mode == syspwr_aftr)
		return;
	for (i = 0; i < ARRAY_SIZE(cmu_backup_list); i++) {
		if (cmu_backup_list[i].backup_valid == 0)
			continue;

		if (cmu_backup_list[i].power)
			if (pwrcal_getf(cmu_backup_list[i].power, 0, 0xF) != 0xF)
				continue;

		pwrcal_writel(cmu_backup_list[i].sfr, cmu_backup_list[i].backup);
	}
}


/* to avoid timing violation when release reset at clock divider in DDRPHY */
static void mif_work_around(void)
{
	pwrcal_writel(SEQ_MIF_TRANSITION0, 0x80870089);
	pwrcal_writel(SEQ_MIF_TRANSITION1, 0x80910088);
	pwrcal_writel(SEQ_MIF_TRANSITION2, 0x80880092);
	pwrcal_writel(SEQ_MIF_TRANSITION3, 0x80920094);
	pwrcal_writel(SEQ_MIF_TRANSITION4, 0x80950093);
	pwrcal_writel(SEQ_MIF_TRANSITION5, 0x80930096);
}

static void pwrcal_syspwr_prepare(int mode)
{
	save_cmusfr(mode);
	syspwr_hwacg_control(mode);
	disable_armidleclockdown();

	set_pmu_sys_pwr_reg(mode);
	set_pmu_central_seq(true);

	mif_work_around();
	switch (mode) {
	case syspwr_stop:
		pwrcal_setbit(TOP_BUS_MIF_OPTION, 2, 0);
		pwrcal_setbit(TOP_BUS_MIF_OPTION, 1, 0);
		pwrcal_setbit(TOP_BUS_MIF_OPTION, 0, 0);
		pwrcal_setbit(FSYS0_OPTION, 31, 0);
		pwrcal_setbit(FSYS0_OPTION, 30, 0);
		pwrcal_setbit(FSYS0_OPTION, 29, 0);
		pwrcal_setbit(FSYS1_OPTION, 31, 0);
		pwrcal_setbit(FSYS1_OPTION, 30, 0);
		pwrcal_setbit(FSYS1_OPTION, 29, 0);
		pwrcal_setbit(G3D_OPTION, 31, 0);
		pwrcal_setbit(G3D_OPTION, 30, 0);
		pwrcal_setbit(WAKEUP_MASK, 30, 1);
		set_pmu_central_seq_mif(true);
		break;
	case syspwr_aftr:
		pwrcal_setbit(TOP_BUS_MIF_OPTION, 2, 0);
		pwrcal_setbit(TOP_BUS_MIF_OPTION, 1, 0);
		pwrcal_setbit(TOP_BUS_MIF_OPTION, 0, 0);
		pwrcal_setbit(FSYS0_OPTION, 31, 0);
		pwrcal_setbit(FSYS0_OPTION, 30, 0);
		pwrcal_setbit(FSYS0_OPTION, 29, 0);
		pwrcal_setbit(FSYS1_OPTION, 31, 0);
		pwrcal_setbit(FSYS1_OPTION, 30, 0);
		pwrcal_setbit(FSYS1_OPTION, 29, 0);
		pwrcal_setbit(G3D_OPTION, 31, 0);
		pwrcal_setbit(G3D_OPTION, 30, 0);
		pwrcal_setbit(WAKEUP_MASK, 30, 1);
		break;
	case syspwr_sicd:
	case syspwr_sicd_cpd:
		pwrcal_setbit(TOP_BUS_MIF_OPTION, 2, 0);
		pwrcal_setbit(TOP_BUS_MIF_OPTION, 1, 1);
		pwrcal_setbit(TOP_BUS_MIF_OPTION, 0, 1);
		pwrcal_setbit(FSYS0_OPTION, 31, 0);
		pwrcal_setbit(FSYS0_OPTION, 30, 0);
		pwrcal_setbit(FSYS0_OPTION, 29, 0);
		pwrcal_setbit(FSYS1_OPTION, 31, 0);
		pwrcal_setbit(FSYS1_OPTION, 30, 0);
		pwrcal_setbit(FSYS1_OPTION, 29, 0);
		pwrcal_setbit(G3D_OPTION, 31, 1);
		pwrcal_setbit(G3D_OPTION, 30, 1);
		pwrcal_setbit(WAKEUP_MASK, 30, 1);
		set_pmu_central_seq_mif(true);
		break;
	case syspwr_alpa:
	case syspwr_dstop:
		pwrcal_setbit(TOP_BUS_MIF_OPTION, 2, 0);
		pwrcal_setbit(TOP_BUS_MIF_OPTION, 1, 0);
		pwrcal_setbit(TOP_BUS_MIF_OPTION, 0, 0);
		pwrcal_setbit(FSYS0_OPTION, 31, 0);
		pwrcal_setbit(FSYS0_OPTION, 30, 0);
		pwrcal_setbit(FSYS0_OPTION, 29, 0);
		pwrcal_setbit(FSYS1_OPTION, 31, 1);
		pwrcal_setbit(FSYS1_OPTION, 30, 1);
		pwrcal_setbit(FSYS1_OPTION, 29, 0);
		pwrcal_setbit(G3D_OPTION, 31, 0);
		pwrcal_setbit(G3D_OPTION, 30, 0);
		pwrcal_setbit(WAKEUP_MASK, 30, 1);
		set_pmu_central_seq_mif(true);
		break;
	case syspwr_lpd:
		pwrcal_setbit(TOP_BUS_MIF_OPTION, 2, 0);
		pwrcal_setbit(TOP_BUS_MIF_OPTION, 1, 0);
		pwrcal_setbit(TOP_BUS_MIF_OPTION, 0, 0);
		pwrcal_setbit(FSYS0_OPTION, 31, 0);
		pwrcal_setbit(FSYS0_OPTION, 30, 0);
		pwrcal_setbit(FSYS0_OPTION, 29, 0);
		pwrcal_setbit(FSYS1_OPTION, 31, 1);
		pwrcal_setbit(FSYS1_OPTION, 30, 1);
		pwrcal_setbit(FSYS1_OPTION, 29, 0);
		pwrcal_setbit(G3D_OPTION, 31, 0);
		pwrcal_setbit(G3D_OPTION, 30, 0);
		pwrcal_setbit(WAKEUP_MASK, 30, 1);
		break;
	case syspwr_sleep:
		pwrcal_setbit(TOP_BUS_MIF_OPTION, 2, 0);
		pwrcal_setbit(TOP_BUS_MIF_OPTION, 1, 0);
		pwrcal_setbit(TOP_BUS_MIF_OPTION, 0, 0);
		pwrcal_setbit(FSYS0_OPTION, 31, 1);
		pwrcal_setbit(FSYS0_OPTION, 30, 1);
		pwrcal_setbit(FSYS0_OPTION, 29, 1);
		pwrcal_setbit(FSYS1_OPTION, 31, 1);
		pwrcal_setbit(FSYS1_OPTION, 30, 1);
		pwrcal_setbit(FSYS1_OPTION, 29, 1);
		pwrcal_setbit(G3D_OPTION, 31, 0);
		pwrcal_setbit(G3D_OPTION, 30, 0);
		pwrcal_setbit(WAKEUP_MASK, 30, 1);
		set_pmu_central_seq_mif(true);
		break;
	default:
		break;
	}
}

#define PAD_INITIATE_WAKEUP	(0x1 << 28)

void set_pmu_pad_retention_release(void)
{
	/*pwrcal_writel(PAD_RETENTION_AUD_OPTION, PAD_INITIATE_WAKEUP);*/
	pwrcal_writel(PAD_RETENTION_PCIE_OPTION, PAD_INITIATE_WAKEUP);
	pwrcal_writel(PAD_RETENTION_UFS_CARD_OPTION, PAD_INITIATE_WAKEUP);
	pwrcal_writel(PAD_RETENTION_MMC2_OPTION, PAD_INITIATE_WAKEUP);
	pwrcal_writel(PAD_RETENTION_TOP_OPTION, PAD_INITIATE_WAKEUP);
	pwrcal_writel(PAD_RETENTION_UART_OPTION, PAD_INITIATE_WAKEUP);
	pwrcal_writel(PAD_RETENTION_MMC0_OPTION, PAD_INITIATE_WAKEUP);
	pwrcal_writel(PAD_RETENTION_EBIA_OPTION, PAD_INITIATE_WAKEUP);
	pwrcal_writel(PAD_RETENTION_EBIB_OPTION, PAD_INITIATE_WAKEUP);
	pwrcal_writel(PAD_RETENTION_SPI_OPTION, PAD_INITIATE_WAKEUP);
	pwrcal_writel(PAD_RETENTION_MIF_OPTION, PAD_INITIATE_WAKEUP);
	pwrcal_writel(PAD_RETENTION_BOOTLDO_0_OPTION, PAD_INITIATE_WAKEUP);
	pwrcal_writel(PAD_RETENTION_UFS_OPTION, PAD_INITIATE_WAKEUP);
	pwrcal_writel(PAD_RETENTION_USB_OPTION, PAD_INITIATE_WAKEUP);
	pwrcal_writel(PAD_RETENTION_BOOTLDO_1_OPTION, PAD_INITIATE_WAKEUP);
}

static void pwrcal_syspwr_post(int mode)
{
	syspwr_hwacg_control_post(mode);
	enable_armidleclockdown();

	if (pwrcal_getbit(CENTRAL_SEQ_CONFIGURATION , 16) == 0x1) {
		pwrcal_setf(QSTATE_CTRL_APM, 0, 0x3, 0x3);
		pwrcal_setf(QSTATE_CTRL_ASYNCAHBM_SSS_ATLAS, 0, 0x3, 0x3);
		pwrcal_setf(QSTATE_CTRL_ASYNCAHBM_SSS_ATLAS, 7, 0x3, 0x0);

		set_pmu_pad_retention_release();
	} else {
		set_pmu_central_seq(false);
	}

	switch (mode) {
	case syspwr_sleep:
		set_pmu_lpi_mask();
	case syspwr_stop:
	case syspwr_sicd:
	case syspwr_sicd_cpd:
	case syspwr_aftr:
	case syspwr_alpa:
	case syspwr_lpd:
	case syspwr_dstop:
		pwrcal_setbit(TOP_BUS_MIF_OPTION, 2, 0);
		pwrcal_setbit(TOP_BUS_MIF_OPTION, 1, 0);
		pwrcal_setbit(TOP_BUS_MIF_OPTION, 0, 0);
		pwrcal_setbit(FSYS0_OPTION, 31, 1);
		pwrcal_setbit(FSYS0_OPTION, 30, 1);
		pwrcal_setbit(FSYS0_OPTION, 29, 1);
		pwrcal_setbit(FSYS1_OPTION, 31, 1);
		pwrcal_setbit(FSYS1_OPTION, 30, 1);
		pwrcal_setbit(FSYS1_OPTION, 29, 1);
		pwrcal_setbit(G3D_OPTION, 31, 0);
		pwrcal_setbit(G3D_OPTION, 30, 0);
		pwrcal_setbit(WAKEUP_MASK, 30, 1);
		set_pmu_central_seq_mif(false);
		break;
	default:
		break;
	}
	restore_cmusfr(mode);
}
static void pwrcal_syspwr_earlywakeup(int mode)
{
	set_pmu_central_seq(false);

	syspwr_hwacg_control_post(mode);
	enable_armidleclockdown();

	switch (mode) {
	case syspwr_stop:
	case syspwr_sicd:
	case syspwr_sicd_cpd:
	case syspwr_aftr:
	case syspwr_alpa:
	case syspwr_lpd:
	case syspwr_dstop:
	case syspwr_sleep:
		pwrcal_setbit(TOP_BUS_MIF_OPTION, 2, 0);
		pwrcal_setbit(TOP_BUS_MIF_OPTION, 1, 0);
		pwrcal_setbit(TOP_BUS_MIF_OPTION, 0, 0);
		pwrcal_setbit(FSYS0_OPTION, 31, 1);
		pwrcal_setbit(FSYS0_OPTION, 30, 1);
		pwrcal_setbit(FSYS0_OPTION, 29, 1);
		pwrcal_setbit(FSYS1_OPTION, 31, 1);
		pwrcal_setbit(FSYS1_OPTION, 30, 1);
		pwrcal_setbit(FSYS1_OPTION, 29, 1);
		pwrcal_setbit(G3D_OPTION, 31, 0);
		pwrcal_setbit(G3D_OPTION, 30, 0);
		pwrcal_setbit(WAKEUP_MASK, 30, 1);
		set_pmu_central_seq_mif(false);
		break;
	default:
		break;
	}

	restore_cmusfr(mode);
}


struct cal_pm_ops cal_pm_ops = {
	.pm_enter = pwrcal_syspwr_prepare,
	.pm_exit = pwrcal_syspwr_post,
	.pm_earlywakeup = pwrcal_syspwr_earlywakeup,
	.pm_init = pwrcal_syspwr_init,
};
