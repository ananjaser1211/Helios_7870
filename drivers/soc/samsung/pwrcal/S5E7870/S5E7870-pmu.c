#include "../pwrcal.h"
#include "../pwrcal-env.h"
#include "../pwrcal-rae.h"
#include "../pwrcal-pmu.h"
#include "S5E7870-cmusfr.h"
#include "S5E7870-pmusfr.h"
#include "S5E7870-cmu.h"


static void dispaud_prev(int enable)
{
	if (enable == 0) {
		pwrcal_setf(CLK_ENABLE_CLK_DISPAUD_BUS, 0, 0x7, 0x7);
		pwrcal_setf(CLK_ENABLE_CLK_DISPAUD_APB, 0, 0x1, 0x1);
		pwrcal_setbit(CLK_CON_MUX_CLKPHY_DISPAUD_MIPIPHY_TXBYTECLKHS_USER,	12,	0);
		pwrcal_setbit(CLK_CON_MUX_CLKPHY_DISPAUD_MIPIPHY_RXCLKESC0_USER,	12,	0);
		pwrcal_setbit(CLK_CON_MUX_CLKPHY_DISPAUD_MIPIPHY_TXBYTECLKHS_USER,	27,	1);
		pwrcal_setbit(CLK_CON_MUX_CLKPHY_DISPAUD_MIPIPHY_RXCLKESC0_USER,	27,	1);
	}

	pwrcal_setf(PMU_CLKRUN_CMU_DISPAUD_SYS_PWR_REG, 0, 0x1, 0x0);
	pwrcal_setf(PMU_CLKSTOP_CMU_DISPAUD_SYS_PWR_REG, 0, 0x1, 0x0);
	pwrcal_setf(PMU_DISABLE_PLL_CMU_DISPAUD_SYS_PWR_REG, 0, 0x1, 0x0);
	pwrcal_setf(PMU_RESET_LOGIC_DISPAUD_SYS_PWR_REG, 0, 0x1, 0x0);
	pwrcal_setf(PMU_RESET_CMU_DISPAUD_SYS_PWR_REG, 0, 0x1, 0x0);
}

static void g3d_prev(int enable)
{
	if (enable == 0) {
		pwrcal_setf(CLK_ENABLE_CLK_G3D_BUS, 1, 0x3, 0x3);
		pwrcal_setf(CLK_ENABLE_CLK_G3D_APB, 1, 0x1, 0x1);
	}

	pwrcal_setf(PMU_CLKRUN_CMU_G3D_SYS_PWR_REG, 0, 0x1, 0x0);
	pwrcal_setf(PMU_CLKSTOP_CMU_G3D_SYS_PWR_REG, 0, 0x1, 0x0);
	pwrcal_setf(PMU_DISABLE_PLL_CMU_G3D_SYS_PWR_REG, 0, 0x1, 0x0);
	pwrcal_setf(PMU_RESET_LOGIC_G3D_SYS_PWR_REG, 0, 0x1, 0x0);
	pwrcal_setf(PMU_RESET_CMU_G3D_SYS_PWR_REG, 0, 0x1, 0x0);

}

static void isp_prev(int enable)
{
	if (enable == 0) {
		pwrcal_setf(CLK_ENABLE_CLK_ISP_VRA, 0, 0x1, 0x1);
		pwrcal_setf(CLK_ENABLE_CLK_ISP_APB, 0, 0x1, 0x1);
		pwrcal_setf(CLK_ENABLE_CLK_ISP_ISPD, 0, 0x1, 0x1);
		pwrcal_setf(CLK_ENABLE_CLK_ISP_CAM, 0, 0x1, 0x1);
		pwrcal_setf(CLK_ENABLE_CLK_ISP_ISP, 0, 0x1, 0x1);
		pwrcal_setbit(CLK_CON_MUX_CLKPHY_ISP_S_RXBYTECLKHS0_S4_USER,	12,	0);
		pwrcal_setbit(CLK_CON_MUX_CLKPHY_ISP_S_RXBYTECLKHS0_S4S_USER,	12,	0);
		pwrcal_setbit(CLK_CON_MUX_CLKPHY_ISP_S_RXBYTECLKHS0_S4_USER,	27,	1);
		pwrcal_setbit(CLK_CON_MUX_CLKPHY_ISP_S_RXBYTECLKHS0_S4S_USER,	27,	1);

#if 0 // plz, check the sfr name first
		while (pwrcal_getf((void *)0x14403020, 0, 0x1) != 0x0);
		while (pwrcal_getf((void *)0x14443010, 0, 0x1) != 0x0);
		while (pwrcal_getf((void *)0x1444B010, 0, 0x1) != 0x0);
#endif
	}

	pwrcal_setf(PMU_CLKRUN_CMU_ISP_SYS_PWR_REG, 0, 0x1, 0x0);
	pwrcal_setf(PMU_CLKSTOP_CMU_ISP_SYS_PWR_REG, 0, 0x1, 0x0);
	pwrcal_setf(PMU_DISABLE_PLL_CMU_ISP_SYS_PWR_REG, 0, 0x1, 0x0);
	pwrcal_setf(PMU_RESET_LOGIC_ISP_SYS_PWR_REG, 0, 0x1, 0x0);
	pwrcal_setf(PMU_RESET_CMU_ISP_SYS_PWR_REG, 0, 0x1, 0x0);
}

static void mfcmscl_prev(int enable)
{
	if (enable == 0) {
		pwrcal_setf(CLK_ENABLE_CLK_MFCMSCL_MSCL, 0, 0x1D, 0x1D);
		pwrcal_setf(CLK_ENABLE_CLK_MFCMSCL_APB, 0, 0x1, 0x1);
		pwrcal_setf(CLK_ENABLE_CLK_MFCMSCL_MFC, 0, 0x1, 0x1);
	}

	pwrcal_setf(PMU_CLKRUN_CMU_MFCMSCL_SYS_PWR_REG, 0, 0x1, 0x0);
	pwrcal_setf(PMU_CLKSTOP_CMU_MFCMSCL_SYS_PWR_REG, 0, 0x1, 0x0);
	pwrcal_setf(PMU_DISABLE_PLL_CMU_MFCMSCL_SYS_PWR_REG, 0, 0x1, 0x0);
	pwrcal_setf(PMU_RESET_LOGIC_MFCMSCL_SYS_PWR_REG, 0, 0x1, 0x0);
	pwrcal_setf(PMU_RESET_CMU_MFCMSCL_SYS_PWR_REG, 0, 0x1, 0x0);

}

static void dispaud_post(int enable)
{
	if (enable == 1) {
		pwrcal_gate_disable(CLK(DISPAUD_GATE_CLK_DISPAUD_UID_CLKCMU_DISPAUD_BUS_PPMU));
		pwrcal_gate_disable(CLK(DISPAUD_GATE_CLK_DISPAUD_UID_CLKCMU_DISPAUD_BUS_DISP));
		pwrcal_gate_disable(CLK(DISPAUD_GATE_CLK_DISPAUD_UID_CLKCMU_DISPAUD_BUS));
		pwrcal_gate_disable(CLK(DISPAUD_GATE_CLK_DISPAUD_UID_CLK_DISPAUD_APB_DISP));
		pwrcal_gate_disable(CLK(DISPAUD_GATE_CLK_DISPAUD_UID_CON_DISPAUD_IPCLKPORT_I_EXT2AUD_BCK_gpio_I2S));
		pwrcal_gate_disable(CLK(DISPAUD_GATE_CLK_DISPAUD_UID_CON_DISPAUD_IPCLKPORT_I_AUD_I2S_BCLK_BT_IN));
		pwrcal_gate_disable(CLK(DISPAUD_GATE_CLK_DISPAUD_UID_CON_DISPAUD_IPCLKPORT_I_CP2AUD_BCK));
		pwrcal_gate_disable(CLK(DISPAUD_GATE_CLK_DISPAUD_UID_CON_DISPAUD_IPCLKPORT_I_AUD_I2S_BCLK_FM_IN));
		pwrcal_gate_disable(CLK(DISPAUD_GATE_CLK_DISPAUD_UID_DECON_IPCLKPORT_I_VCLK));
		pwrcal_gate_disable(CLK(DISPAUD_GATE_CLK_DISPAUD_UID_DECON_IPCLKPORT_I_ECLK));
		pwrcal_gate_disable(CLK(DISPAUD_GATE_CLK_DISPAUD_UID_MIXER_AUD_IPCLKPORT_SYSCLK));
		pwrcal_gate_disable(CLK(DISPAUD_GATE_CLK_DISPAUD_UID_CLK_DISPAUD_APB_AUD));
		pwrcal_gate_disable(CLK(DISPAUD_GATE_CLK_DISPAUD_UID_MI2S_AUD_IPCLKPORT_I2SCODCLKI));
		pwrcal_gate_disable(CLK(DISPAUD_GATE_CLK_DISPAUD_UID_CLK_DISPAUD_APB_AUD_AMP));
		pwrcal_gate_disable(CLK(DISPAUD_GATE_CLK_DISPAUD_UID_MI2S_AMP_IPCLKPORT_I2SCODCLKI));

		/*non used disable*/
		pwrcal_gate_disable(CLK(DISPAUD_GATE_CLK_DISPAUD_UID_CLKCMU_DISPAUD_BUS_VPP));
		pwrcal_gate_disable(CLK(DISPAUD_GATE_CLK_DISPAUD_UID_DSIM1_IPCLKPORT_I_TXBYTECLKHS));
		pwrcal_gate_disable(CLK(DISPAUD_GATE_CLK_DISPAUD_UID_DSIM1_IPCLKPORT_I_RXCLKESC0));
	}
}

static void g3d_post(int enable)
{
	if (enable == 1) {
		pwrcal_gate_disable(CLK(G3D_GATE_CLK_G3D_UID_PPMU_G3D_IPCLKPORT_ACLK));
		pwrcal_gate_disable(CLK(G3D_GATE_CLK_G3D_UID_PPMU_G3D_IPCLKPORT_PCLK));
		pwrcal_gate_disable(CLK(G3D_GATE_CLK_G3D_UID_QE_G3D_IPCLKPORT_ACLK));
		pwrcal_gate_disable(CLK(G3D_GATE_CLK_G3D_UID_G3D_IPCLKPORT_CLK));
		pwrcal_gate_disable(CLK(G3D_GATE_CLK_G3D_UID_ASYNCS_D0_G3D_IPCLKPORT_I_CLK));
		pwrcal_gate_disable(CLK(G3D_GATE_CLK_G3D_UID_ASYNC_G3D_P_IPCLKPORT_PCLKM));
		pwrcal_gate_disable(CLK(G3D_GATE_CLK_G3D_UID_SYSREG_G3D_IPCLKPORT_PCLK));
		pwrcal_gate_disable(CLK(G3D_GATE_CLK_G3D_UID_QE_G3D_IPCLKPORT_PCLK));
	}
}

static void isp_post(int enable)
{
	if (enable == 1) {
		pwrcal_gate_disable(CLK(ISP_GATE_CLK_ISP_UID_CLK_ISP_ISPD_PPMU));
		pwrcal_gate_disable(CLK(ISP_GATE_CLK_ISP_UID_CLK_ISP_ISPD));
		pwrcal_gate_disable(CLK(ISP_GATE_CLK_ISP_UID_CLK_ISP_CAM));
		pwrcal_gate_disable(CLK(ISP_GATE_CLK_ISP_UID_CLK_ISP_CAM_HALF));
		pwrcal_gate_disable(CLK(ISP_GATE_CLK_ISP_UID_CLK_ISP_VRA));

		/*non used disable*/
		pwrcal_gate_disable(CLK(ISP_GATE_CLK_ISP_UID_CLK_ISP_ISP));
	}
}

static void mfcmscl_post(int enable)
{
	if (enable == 1) {
		pwrcal_gate_disable(CLK(MFCMSCL_GATE_CLK_MFCMSCL_UID_CLKCMU_MFCMSCL_MSCL_PPMU));
		pwrcal_gate_disable(CLK(MFCMSCL_GATE_CLK_MFCMSCL_UID_CLKCMU_MFCMSCL_MSCL_D));
		pwrcal_gate_disable(CLK(MFCMSCL_GATE_CLK_MFCMSCL_UID_CLKCMU_MFCMSCL_MSCL));
		pwrcal_gate_disable(CLK(MFCMSCL_GATE_CLK_MFCMSCL_UID_CLKCMU_MFCMSCL_MSCL_BI));
		pwrcal_gate_disable(CLK(MFCMSCL_GATE_CLK_MFCMSCL_UID_CLKCMU_MFCMSCL_MSCL_POLY));
		pwrcal_gate_disable(CLK(MFCMSCL_GATE_CLK_MFCMSCL_UID_CLKCMU_MFCMSCL_MSCL_JPEG));
		pwrcal_gate_disable(CLK(MFCMSCL_GATE_CLK_MFCMSCL_UID_CLKCMU_MFCMSCL_MFC));
	}
}

static void dispaud_config(int enable)
{
	pwrcal_setf(PMU_DISPAUD_OPTION, 0, 0x3, 0x2);
	pwrcal_setf(PMU_PAD_RETENTION_AUD_OPTION, 0, 0xFFFFFFFF, 0x10000000);
}

static void g3d_config(int enable)
{
	pwrcal_setf(PMU_G3D_OPTION, 0, 0x1, 0x1);
	pwrcal_setf(PMU_G3D_DURATION0, 4, 0xFF, 0x0);
}

static void isp_config(int enable)
{
	pwrcal_setf(PMU_ISP_OPTION, 0, 0x3, 0x2);
}

static void mfcmscl_config(int enable)
{
	pwrcal_setf(PMU_MFCMSCL_OPTION, 0, 0x3, 0x2);
}

BLKPWR(blkpwr_dispaud, PMU_DISPAUD_CONFIGURATION, 0, 0xF, PMU_DISPAUD_STATUS, 0, 0xF, dispaud_config, dispaud_prev, dispaud_post);
BLKPWR(blkpwr_g3d, PMU_G3D_CONFIGURATION, 0, 0xF, PMU_G3D_STATUS, 0, 0xF, g3d_config, g3d_prev, g3d_post);
BLKPWR(blkpwr_isp, PMU_ISP_CONFIGURATION, 0, 0xF, PMU_ISP_STATUS, 0, 0xF, isp_config, isp_prev, isp_post);
BLKPWR(blkpwr_mfcmscl, PMU_MFCMSCL_CONFIGURATION, 0, 0xF, PMU_MFCMSCL_STATUS, 0, 0xF, mfcmscl_config, mfcmscl_prev, mfcmscl_post);


struct cal_pd *pwrcal_blkpwr_list[4];
unsigned int pwrcal_blkpwr_size = 4;

static int blkpwr_init(void)
{
	pwrcal_blkpwr_list[0] = &blkpwr_blkpwr_dispaud;
	pwrcal_blkpwr_list[1] = &blkpwr_blkpwr_g3d;
	pwrcal_blkpwr_list[2] = &blkpwr_blkpwr_isp;
	pwrcal_blkpwr_list[3] = &blkpwr_blkpwr_mfcmscl;

	return 0;
}

struct cal_pd_ops cal_pd_ops = {
	.pd_control = blkpwr_control,
	.pd_status = blkpwr_status,
	.pd_init = blkpwr_init,
};
