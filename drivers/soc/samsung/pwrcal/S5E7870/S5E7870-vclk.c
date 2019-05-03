#include "../pwrcal-pmu.h"
#include "../pwrcal-clk.h"
#include "../pwrcal-rae.h"
#include "S5E7870-cmu.h"
#include "S5E7870-cmusfr.h"
#include "S5E7870-vclk.h"
#include "S5E7870-vclk-internal.h"

struct pwrcal_vclk_grpgate *vclk_grpgate_list[num_of_grpgate];
struct pwrcal_vclk_m1d1g1 *vclk_m1d1g1_list[num_of_m1d1g1];
struct pwrcal_vclk_p1 *vclk_p1_list[num_of_p1];
struct pwrcal_vclk_m1 *vclk_m1_list[num_of_m1];
struct pwrcal_vclk_d1 *vclk_d1_list[num_of_d1];
struct pwrcal_vclk_pxmxdx *vclk_pxmxdx_list[num_of_pxmxdx];
struct pwrcal_vclk_umux *vclk_umux_list[num_of_umux];
struct pwrcal_vclk_dfs *vclk_dfs_list[num_of_dfs];
unsigned int vclk_grpgate_list_size = num_of_grpgate;
unsigned int vclk_m1d1g1_list_size = num_of_m1d1g1;
unsigned int vclk_p1_list_size = num_of_p1;
unsigned int vclk_m1_list_size = num_of_m1;
unsigned int vclk_d1_list_size = num_of_d1;
unsigned int vclk_pxmxdx_list_size = num_of_pxmxdx;
unsigned int vclk_umux_list_size = num_of_umux;
unsigned int vclk_dfs_list_size = num_of_dfs;


#define ADD_LIST(to, x)		to[x & 0xFFFF] = &(vclk_##x)

static struct pwrcal_clk_set pxmxdx_top_grp[] = {
	{CLK_NONE,				0,	0},
};
static struct pwrcal_clk_set pxmxdx_disp_grp[] = {
	{CLK(DISPAUD_MUX_CLKCMU_DISPAUD_BUS_USER), 1, -1},
	{CLK(DISPAUD_DIV_CLK_DISPAUD_APB), 1, -1},
	{CLK(MIF_MUXGATE_CLKCMU_DISPAUD_BUS),	1,	-1},
	{CLK_NONE,				0,	0},
};
static struct pwrcal_clk_set pxmxdx_mfcmscl_grp[] = {
	{CLK(MFCMSCL_MUX_CLKCMU_MFCMSCL_MSCL_USER), 1, 0},
	{CLK(MFCMSCL_MUX_CLKCMU_MFCMSCL_MFC_USER), 1, 0},
	{CLK(MFCMSCL_DIV_CLK_MFCMSCL_APB), 2, -1},
	{CLK(MIF_MUXGATE_CLKCMU_MFCMSCL_MSCL),	1,	0},
	{CLK(MIF_MUXGATE_CLKCMU_MFCMSCL_MFC),	1,	0},
	{CLK_NONE,				0,	0},
};
static struct pwrcal_clk_set pxmxdx_isp_vra_grp[] = {
	{CLK(ISP_MUX_CLKCMU_ISP_VRA_USER), 1, 0},
	{CLK(ISP_DIV_CLK_ISP_APB), 3, -1},
	{CLK(MIF_MUXGATE_CLKCMU_ISP_VRA),	1,	0},
	{CLK_NONE,				0,	0},
};
static struct pwrcal_clk_set pxmxdx_isp_cam_grp[] = {
	{CLK(ISP_MUX_CLKCMU_ISP_CAM_USER), 1, 0},
	{CLK(ISP_DIV_CLK_ISP_CAM_HALF), 1, -1},
	{CLK(MIF_MUXGATE_CLKCMU_ISP_CAM),	1,	0},
	{CLK_NONE,				0,	0},
};
static struct pwrcal_clk_set pxmxdx_isp_isp_grp[] = {
	{CLK(ISP_MUX_CLKCMU_ISP_ISP_USER), 1, 0},
	{CLK(MIF_MUXGATE_CLKCMU_ISP_ISP),	1,	0},
	{CLK_NONE,				0,	0},
};

static struct pwrcal_clk_set pxmxdx_oscclk_aud_grp[] = {
	{CLK(PMU_DEBUG_CLKOUT_SEL08),	1,	-1},
	{CLK(PMU_DEBUG_CLKOUT_SEL09),	1,	-1},
	{CLK(PMU_DEBUG_CLKOUT_SEL10),	1,	-1},
	{CLK(PMU_DEBUG_CLKOUT_SEL11),	1,	-1},
	{CLK(PMU_DEBUG_CLKOUT_SEL12),	1,	-1},
	{CLK(PMU_DEBUG_CLKOUT_DISABLE),	0,	1},
	{CLK_NONE,			0,	0},
};

PXMXDX(pxmxdx_top,	0,	pxmxdx_top_grp);
PXMXDX(pxmxdx_disp,	dvfs_disp,	pxmxdx_disp_grp);
PXMXDX(pxmxdx_mfcmscl,	0,	pxmxdx_mfcmscl_grp);
PXMXDX(pxmxdx_isp_vra,	0,	pxmxdx_isp_vra_grp);
PXMXDX(pxmxdx_isp_cam,	0,	pxmxdx_isp_cam_grp);
PXMXDX(pxmxdx_isp_isp,	0,	pxmxdx_isp_isp_grp);
PXMXDX(pxmxdx_oscclk_aud,	0,	pxmxdx_oscclk_aud_grp);

P1(p1_disp_pll, 0, DISP_PLL);
P1(p1_aud_pll, 0, AUD_PLL);
P1(p1_usb_pll, 0, USB_PLL);
P1(p1_isp_pll, 0, ISP_PLL);

M1(m1_dummy, 0, 0);

D1(d1_dispaud_mi2s, 0, DISPAUD_DIV_CLK_DISPAUD_MI2S);
D1(d1_dispaud_mixer, 0, DISPAUD_DIV_CLK_DISPAUD_MIXER);


M1D1G1(sclk_decon_vclk, 0, MIF_MUX_CLKCMU_DISPAUD_DECON_INT_VCLK, MIF_DIV_CLKCMU_DISPAUD_DECON_INT_VCLK, MIF_GATE_CLKCMU_DISPAUD_DECON_INT_VCLK, DISPAUD_MUX_CLKCMU_DISPAUD_DECON_INT_VCLK_USER);
M1D1G1(sclk_decon_vclk_local, 0, DISPAUD_MUX_CLK_DISPAUD_DECON_INT_VCLK, DISPAUD_DIV_CLK_DISPAUD_DECON_INT_VCLK, DISPAUD_GATE_CLK_DISPAUD_UID_DECON_IPCLKPORT_I_VCLK, 0);
M1D1G1(sclk_decon_eclk, 0, MIF_MUX_CLKCMU_DISPAUD_DECON_INT_ECLK, MIF_DIV_CLKCMU_DISPAUD_DECON_INT_ECLK, MIF_GATE_CLKCMU_DISPAUD_DECON_INT_ECLK, DISPAUD_MUX_CLKCMU_DISPAUD_DECON_INT_ECLK_USER);
M1D1G1(sclk_decon_eclk_local, 0, DISPAUD_MUX_CLK_DISPAUD_DECON_INT_ECLK, DISPAUD_DIV_CLK_DISPAUD_DECON_INT_ECLK, DISPAUD_GATE_CLK_DISPAUD_UID_DECON_IPCLKPORT_I_ECLK, 0);
M1D1G1(sclk_mmc0, 0, MIF_MUX_CLKCMU_FSYS_MMC0, MIF_DIV_CLKCMU_FSYS_MMC0, MIF_GATE_CLKCMU_FSYS_MMC0, 0);
M1D1G1(sclk_mmc1, 0, MIF_MUX_CLKCMU_FSYS_MMC1, MIF_DIV_CLKCMU_FSYS_MMC1, MIF_GATE_CLKCMU_FSYS_MMC1, 0);
M1D1G1(sclk_mmc2, 0, MIF_MUX_CLKCMU_FSYS_MMC2, MIF_DIV_CLKCMU_FSYS_MMC2, MIF_GATE_CLKCMU_FSYS_MMC2, 0);
M1D1G1(sclk_ufsunipro, 0, MIF_MUX_CLKCMU_FSYS_UFSUNIPRO, MIF_DIV_CLKCMU_FSYS_UFSUNIPRO, MIF_GATE_CLKCMU_FSYS_UFSUNIPRO, 0);
M1D1G1(sclk_ufsunipro_cfg, 0, MIF_MUX_CLKCMU_FSYS_UFSUNIPRO_CFG, MIF_DIV_CLKCMU_FSYS_UFSUNIPRO_CFG, MIF_GATE_CLKCMU_FSYS_UFSUNIPRO_CFG, 0);
M1D1G1(sclk_usb20drd, 0, MIF_MUX_CLKCMU_FSYS_USB20DRD_REFCLK, MIF_DIV_CLKCMU_FSYS_USB20DRD_REFCLK, MIF_GATE_CLKCMU_FSYS_USB20DRD_REFCLK, 0);
M1D1G1(sclk_uart_sensor, gate_peri_peric0, MIF_MUX_CLKCMU_PERI_UART_SENSOR, MIF_DIV_CLKCMU_PERI_UART_SENSOR, MIF_GATE_CLKCMU_PERI_UART_SENSOR, 0);
M1D1G1(sclk_uart_btwififm, gate_peri_peric0, MIF_MUX_CLKCMU_PERI_UART_BTWIFIFM, MIF_DIV_CLKCMU_PERI_UART_BTWIFIFM, MIF_GATE_CLKCMU_PERI_UART_BTWIFIFM, 0);
M1D1G1(sclk_uart_debug, gate_peri_peric0, MIF_MUX_CLKCMU_PERI_UART_DEBUG, MIF_DIV_CLKCMU_PERI_UART_DEBUG, MIF_GATE_CLKCMU_PERI_UART_DEBUG, 0);
M1D1G1(sclk_spi_frontfrom, gate_peri_peric1, MIF_MUX_CLKCMU_PERI_SPI_FRONTFROM, MIF_DIV_CLKCMU_PERI_SPI_FRONTFROM, MIF_GATE_CLKCMU_PERI_SPI_FRONTFROM, 0);
M1D1G1(sclk_spi_rearfrom, gate_peri_peric1, MIF_MUX_CLKCMU_PERI_SPI_REARFROM, MIF_DIV_CLKCMU_PERI_SPI_REARFROM, MIF_GATE_CLKCMU_PERI_SPI_REARFROM, 0);
M1D1G1(sclk_spi_ese, 0, MIF_MUX_CLKCMU_PERI_SPI_ESE, MIF_DIV_CLKCMU_PERI_SPI_ESE, MIF_GATE_CLKCMU_PERI_SPI_ESE, 0);
M1D1G1(sclk_spi_voiceprocessor, gate_peri_peric1, MIF_MUX_CLKCMU_PERI_SPI_VOICEPROCESSOR, MIF_DIV_CLKCMU_PERI_SPI_VOICEPROCESSOR, MIF_GATE_CLKCMU_PERI_SPI_VOICEPROCESSOR, 0);
M1D1G1(sclk_spi_sensorhub, gate_peri_peric1, MIF_MUX_CLKCMU_PERI_SPI_SENSORHUB, MIF_DIV_CLKCMU_PERI_SPI_SENSORHUB, MIF_GATE_CLKCMU_PERI_SPI_SENSORHUB, 0);
M1D1G1(sclk_isp_sensor0, 0, MIF_MUX_CLKCMU_ISP_SENSOR0, MIF_DIV_CLKCMU_ISP_SENSOR0, MIF_GATE_CLKCMU_ISP_SENSOR0, 0);
M1D1G1(sclk_isp_sensor1, 0, MIF_MUX_CLKCMU_ISP_SENSOR1, MIF_DIV_CLKCMU_ISP_SENSOR1, MIF_GATE_CLKCMU_ISP_SENSOR1, 0);
M1D1G1(sclk_isp_sensor2, 0, MIF_MUX_CLKCMU_ISP_SENSOR2, MIF_DIV_CLKCMU_ISP_SENSOR2, MIF_GATE_CLKCMU_ISP_SENSOR2, 0);




static struct pwrcal_clk *gategrp_cpucl0_ppmu[] = {
	CLK(CPUCL0_DIV_CLK_CPUCL0_ACLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_cpucl0_bts[] = {
	CLK(CPUCL0_DIV_CLK_CPUCL0_ACLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_cpucl1_ppmu[] = {
	CLK(CPUCL1_DIV_CLK_CPUCL1_ACLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_cpucl1_bts[] = {
	CLK(CPUCL1_DIV_CLK_CPUCL1_ACLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_mfcmscl_sysmmu_mscl[] = {
	CLK(MFCMSCL_MUX_CLKCMU_MFCMSCL_MSCL_USER),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_mfcmscl_sysmmu_mfc[] = {
	CLK(MFCMSCL_MUX_CLKCMU_MFCMSCL_MSCL_USER),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_mfcmscl_ppmu[] = {
	CLK(MFCMSCL_MUX_CLKCMU_MFCMSCL_MSCL_USER),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_mfcmscl_bts[] = {
	CLK(MFCMSCL_MUX_CLKCMU_MFCMSCL_MSCL_USER),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_mfcmscl_common[] = {
	CLK(MFCMSCL_GATE_CLK_MFCMSCL_UID_CLKCMU_MFCMSCL_MSCL_PPMU),
	CLK(MFCMSCL_GATE_CLK_MFCMSCL_UID_CLKCMU_MFCMSCL_MSCL),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_mfcmscl_common_mscl[] = {
	CLK(MFCMSCL_GATE_CLK_MFCMSCL_UID_CLKCMU_MFCMSCL_MSCL_D),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_mfcmscl_mscl_bi[] = {
	CLK(MFCMSCL_GATE_CLK_MFCMSCL_UID_CLKCMU_MFCMSCL_MSCL_BI),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_mfcmscl_mscl_poly[] = {
	CLK(MFCMSCL_GATE_CLK_MFCMSCL_UID_CLKCMU_MFCMSCL_MSCL_POLY),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_mfcmscl_jpeg[] = {
	CLK(MFCMSCL_GATE_CLK_MFCMSCL_UID_CLKCMU_MFCMSCL_MSCL_JPEG),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_mfcmscl_mfc[] = {
	CLK(MFCMSCL_GATE_CLK_MFCMSCL_UID_CLKCMU_MFCMSCL_MFC),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_g3d_common[] = {
	CLK(G3D_GATE_CLK_G3D_UID_PPMU_G3D_IPCLKPORT_ACLK),
	CLK(G3D_GATE_CLK_G3D_UID_PPMU_G3D_IPCLKPORT_PCLK),
	CLK(G3D_GATE_CLK_G3D_UID_QE_G3D_IPCLKPORT_ACLK),
	CLK(G3D_GATE_CLK_G3D_UID_QE_G3D_IPCLKPORT_PCLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_g3d_sysmmu[] = {
	CLK(G3D_DIV_CLK_G3D_BUS),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_g3d_ppmu[] = {
	CLK(G3D_DIV_CLK_G3D_BUS),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_g3d_bts[] = {
	CLK(G3D_DIV_CLK_G3D_BUS),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_g3d_g3d[] = {
	CLK(G3D_GATE_CLK_G3D_UID_G3D_IPCLKPORT_CLK),
	CLK(G3D_GATE_CLK_G3D_UID_ASYNCS_D0_G3D_IPCLKPORT_I_CLK),
	CLK(G3D_GATE_CLK_G3D_UID_ASYNC_G3D_P_IPCLKPORT_PCLKM),
	CLK(G3D_GATE_CLK_G3D_UID_SYSREG_G3D_IPCLKPORT_PCLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_peric0[] = {
	CLK(PERI_GATE_CLK_PERI_UID_BUSP1_PERIC0_IPCLKPORT_HCLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_peric1[] = {
/*	CLK(PERI_GATE_CLK_PERI_UID_BUSP1_PERIC1_IPCLKPORT_HCLK),*/
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_pwm_motor[] = {
	CLK(PERI_GATE_CLK_PERI_UID_PWM_MOTOR_IPCLKPORT_i_PCLK_S0),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_sclk_pwm_motor[] = {
	CLK(PERI_GATE_CLK_PERI_UID_PWM_MOTOR_IPCLKPORT_i_OSCCLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_mct[] = {
	CLK(PERI_GATE_CLK_PERI_UID_MCT_IPCLKPORT_PCLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_i2c_sensor2[] = {
	CLK(PERI_GATE_CLK_PERI_UID_I2C_SENSOR2_IPCLKPORT_PCLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_i2c_sensor1[] = {
	CLK(PERI_GATE_CLK_PERI_UID_I2C_SENSOR1_IPCLKPORT_PCLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_i2c_tsp[] = {
	CLK(PERI_GATE_CLK_PERI_UID_I2C_TSP_IPCLKPORT_PCLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_i2c_touchkey[] = {
	CLK(PERI_GATE_CLK_PERI_UID_I2C_TOUCHKEY_IPCLKPORT_PCLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_i2c_fuelgauge[] = {
	CLK(PERI_GATE_CLK_PERI_UID_I2C_FUELGAUGE_IPCLKPORT_PCLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_i2c_spkamp[] = {
	CLK(PERI_GATE_CLK_PERI_UID_I2C_SPKAMP_IPCLKPORT_PCLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_i2c_nfc[] = {
	CLK(PERI_GATE_CLK_PERI_UID_I2C_NFC_IPCLKPORT_PCLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_i2c_muic[] = {
	CLK(PERI_GATE_CLK_PERI_UID_I2C_MUIC_IPCLKPORT_PCLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_i2c_ifpmic[] = {
	CLK(PERI_GATE_CLK_PERI_UID_I2C_IFPMIC_IPCLKPORT_PCLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_hsi2c_frontcam[] = {
	CLK(PERI_GATE_CLK_PERI_UID_HSI2C_FRONTCAM_IPCLKPORT_iPCLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_hsi2c_maincam[] = {
	CLK(PERI_GATE_CLK_PERI_UID_HSI2C_MAINCAM_IPCLKPORT_iPCLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_hsi2c_depthcam[] = {
	CLK(PERI_GATE_CLK_PERI_UID_HSI2C_DEPTHCAM_IPCLKPORT_iPCLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_hsi2c_frontsensor[] = {
	CLK(PERI_GATE_CLK_PERI_UID_HSI2C_FRONTSENSOR_IPCLKPORT_iPCLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_hsi2c_rearaf[] = {
	CLK(PERI_GATE_CLK_PERI_UID_HSI2C_REARAF_IPCLKPORT_iPCLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_hsi2c_rearsensor[] = {
	CLK(PERI_GATE_CLK_PERI_UID_HSI2C_REARSENSOR_IPCLKPORT_iPCLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_gpio_touch[] = {
	CLK(PERI_GATE_CLK_PERI_UID_GPIO_TOUCH_IPCLKPORT_PCLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_gpio_top[] = {
	CLK(PERI_GATE_CLK_PERI_UID_GPIO_TOP_IPCLKPORT_PCLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_gpio_nfc[] = {
	CLK(PERI_GATE_CLK_PERI_UID_GPIO_NFC_IPCLKPORT_PCLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_gpio_ese[] = {
	CLK(PERI_GATE_CLK_PERI_UID_GPIO_ESE_IPCLKPORT_PCLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_wdt_cpucl1[] = {
	CLK(PERI_GATE_CLK_PERI_UID_WDT_CPUCL1_IPCLKPORT_PCLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_wdt_cpucl0[] = {
	CLK(PERI_GATE_CLK_PERI_UID_WDT_CPUCL0_IPCLKPORT_PCLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_uart_debug[] = {
	CLK(PERI_GATE_CLK_PERI_UID_UART_DEBUG_IPCLKPORT_PCLK),
	CLK(PERI_GATE_CLK_PERI_UID_UART_DEBUG_IPCLKPORT_EXT_UCLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_uart_btwififm[] = {
	CLK(PERI_GATE_CLK_PERI_UID_UART_BTWIFIFM_IPCLKPORT_PCLK),
	CLK(PERI_GATE_CLK_PERI_UID_UART_BTWIFIFM_IPCLKPORT_EXT_UCLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_uart_sensor[] = {
	CLK(PERI_GATE_CLK_PERI_UID_UART_SENSOR_IPCLKPORT_PCLK),
	CLK(PERI_GATE_CLK_PERI_UID_UART_SENSOR_IPCLKPORT_EXT_UCLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_tmu_g3d[] = {
	CLK(PERI_GATE_CLK_PERI_UID_SFRIF_TMU_G3D_IPCLKPORT_PCLK),
	CLK(PERI_GATE_CLK_PERI_UID_TMU_G3D_IPCLKPORT_I_CLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_tmu_cpucl1[] = {
	CLK(PERI_GATE_CLK_PERI_UID_SFRIF_TMU_CPUCL1_IPCLKPORT_PCLK),
	CLK(PERI_GATE_CLK_PERI_UID_TMU_CPUCL1_IPCLKPORT_I_CLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_tmu_cpucl0[] = {
	CLK(PERI_GATE_CLK_PERI_UID_SFRIF_TMU_CPUCL0_IPCLKPORT_PCLK),
	CLK(PERI_GATE_CLK_PERI_UID_TMU_CPUCL0_IPCLKPORT_I_CLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_spi_sensorhub[] = {
	CLK(PERI_GATE_CLK_PERI_UID_SPI_SENSORHUB_IPCLKPORT_PCLK),
	CLK(PERI_GATE_CLK_PERI_UID_SPI_SENSORHUB_IPCLKPORT_SPI_EXT_CLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_spi_voiceprocessor[] = {
	CLK(PERI_GATE_CLK_PERI_UID_SPI_VOICEPROCESSOR_IPCLKPORT_PCLK),
	CLK(PERI_GATE_CLK_PERI_UID_SPI_VOICEPROCESSOR_IPCLKPORT_SPI_EXT_CLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_spi_ese[] = {
	CLK(PERI_GATE_CLK_PERI_UID_SPI_ESE_IPCLKPORT_PCLK),
	CLK(PERI_GATE_CLK_PERI_UID_SPI_ESE_IPCLKPORT_SPI_EXT_CLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_spi_rearfrom[] = {
	CLK(PERI_GATE_CLK_PERI_UID_SPI_REARFROM_IPCLKPORT_PCLK),
	CLK(PERI_GATE_CLK_PERI_UID_SPI_REARFROM_IPCLKPORT_SPI_EXT_CLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_spi_frontfrom[] = {
	CLK(PERI_GATE_CLK_PERI_UID_SPI_FRONTFROM_IPCLKPORT_PCLK),
	CLK(PERI_GATE_CLK_PERI_UID_SPI_FRONTFROM_IPCLKPORT_SPI_EXT_CLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_gpio_alive[] = {
	CLK(MIF_DIV_CLKCMU_PERI_BUS),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_chipid[] = {
	CLK(MIF_DIV_CLKCMU_PERI_BUS),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_otp_con_top[] = {
	CLK(MIF_DIV_CLKCMU_PERI_BUS),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_rtc_alive[] = {
	CLK(MIF_DIV_CLKCMU_PERI_BUS),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_peri_rtc_top[] = {
	CLK(MIF_DIV_CLKCMU_PERI_BUS),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_fsys_common[] = {
	CLK(FSYS_GATE_CLK_FSYS_UID_PPMU_FSYS_IPCLKPORT_PCLK),
	CLK(FSYS_GATE_CLK_FSYS_UID_PPMU_FSYS_IPCLKPORT_ACLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_fsys_common_sub1[] = {
	CLK(FSYS_GATE_CLK_FSYS_UID_UPSIZER_BUS1_FSYS_IPCLKPORT_aclk),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_fsys_common_sub2[] = {
	CLK(FSYS_GATE_CLK_FSYS_UID_BUSP3_FSYS_IPCLKPORT_HCLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_fsys_sysmmu[] = {
	CLK(MIF_DIV_CLKCMU_FSYS_BUS),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_fsys_ppmu[] = {
	CLK(MIF_DIV_CLKCMU_FSYS_BUS),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_fsys_bts[] = {
	CLK(MIF_DIV_CLKCMU_FSYS_BUS),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_fsys_usb20drd[] = {
	CLK(FSYS_GATE_CLK_FSYS_UID_USB20DRD_IPCLKPORT_HCLK_USB20_CTRL),
	CLK(FSYS_GATE_CLK_FSYS_UID_USB20DRD_IPCLKPORT_ACLK_HSDRD),
	CLK(FSYS_GATE_CLK_FSYS_UID_USB20DRD_IPCLKPORT_HSDRD_ref_clk),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_fsys_mmc0[] = {
	CLK(FSYS_GATE_CLK_FSYS_UID_MMC0_IPCLKPORT_I_ACLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_fsys_sclk_mmc0[] = {
	CLK(MIF_DIV_CLKCMU_FSYS_MMC0),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_fsys_mmc1[] = {
	CLK(FSYS_GATE_CLK_FSYS_UID_MMC1_IPCLKPORT_I_ACLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_fsys_sclk_mmc1[] = {
	CLK(MIF_DIV_CLKCMU_FSYS_MMC1),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_fsys_mmc2[] = {
	CLK(FSYS_GATE_CLK_FSYS_UID_MMC2_IPCLKPORT_I_ACLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_fsys_sclk_mmc2[] = {
	CLK(MIF_DIV_CLKCMU_FSYS_MMC2),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_fsys_sss[] = {
	CLK(MIF_DIV_CLKCMU_FSYS_BUS),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_fsys_rtic[] = {
	CLK(MIF_DIV_CLKCMU_FSYS_BUS),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_fsys_pdma0[] = {
	CLK(FSYS_GATE_CLK_FSYS_UID_PDMA0_IPCLKPORT_ACLK_PDMA0),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_fsys_pdma1[] = {
	CLK(MIF_DIV_CLKCMU_FSYS_BUS),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_fsys_sromc[] = {
	CLK(FSYS_GATE_CLK_FSYS_UID_SROMC_IPCLKPORT_HCLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_dispaud_disp[] = {
	CLK(DISPAUD_GATE_CLK_DISPAUD_UID_CLKCMU_DISPAUD_BUS_PPMU),
	CLK(DISPAUD_GATE_CLK_DISPAUD_UID_CLKCMU_DISPAUD_BUS_DISP),
	CLK(DISPAUD_GATE_CLK_DISPAUD_UID_CLKCMU_DISPAUD_BUS),
	CLK(DISPAUD_GATE_CLK_DISPAUD_UID_CLK_DISPAUD_APB_DISP),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_dispaud_aud[] = {
	CLK(DISPAUD_GATE_CLK_DISPAUD_UID_CON_DISPAUD_IPCLKPORT_I_EXT2AUD_BCK_gpio_I2S),
	CLK(DISPAUD_GATE_CLK_DISPAUD_UID_CON_DISPAUD_IPCLKPORT_I_AUD_I2S_BCLK_BT_IN),
	CLK(DISPAUD_GATE_CLK_DISPAUD_UID_CON_DISPAUD_IPCLKPORT_I_CP2AUD_BCK),
	CLK(DISPAUD_GATE_CLK_DISPAUD_UID_CON_DISPAUD_IPCLKPORT_I_AUD_I2S_BCLK_FM_IN),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_dispaud_sysmmu[] = {
	CLK(DISPAUD_MUX_CLKCMU_DISPAUD_BUS_USER),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_dispaud_ppmu[] = {
	CLK(DISPAUD_MUX_CLKCMU_DISPAUD_BUS_USER),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_dispaud_bts[] = {
	CLK(DISPAUD_MUX_CLKCMU_DISPAUD_BUS_USER),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_dispaud_decon[] = {
	CLK(DISPAUD_MUX_CLKCMU_DISPAUD_BUS_USER),
	CLK(DISPAUD_GATE_CLK_DISPAUD_UID_DECON_IPCLKPORT_I_VCLK),
	CLK(DISPAUD_GATE_CLK_DISPAUD_UID_DECON_IPCLKPORT_I_ECLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_dispaud_dsim0[] = {
	CLK(DISPAUD_DIV_CLK_DISPAUD_APB),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_dispaud_mixer[] = {
	CLK(DISPAUD_GATE_CLK_DISPAUD_UID_MIXER_AUD_IPCLKPORT_SYSCLK),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_dispaud_mi2s_aud[] = {
	CLK(DISPAUD_GATE_CLK_DISPAUD_UID_CLK_DISPAUD_APB_AUD),
	CLK(DISPAUD_GATE_CLK_DISPAUD_UID_MI2S_AUD_IPCLKPORT_I2SCODCLKI),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_dispaud_mi2s_amp[] = {
	CLK(DISPAUD_GATE_CLK_DISPAUD_UID_CLK_DISPAUD_APB_AUD_AMP),
	CLK(DISPAUD_GATE_CLK_DISPAUD_UID_MI2S_AMP_IPCLKPORT_I2SCODCLKI),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_isp_common[] = {
	CLK(ISP_GATE_CLK_ISP_UID_CLK_ISP_ISPD_PPMU),
	CLK(ISP_GATE_CLK_ISP_UID_CLK_ISP_ISPD),
	CLK(ISP_GATE_CLK_ISP_UID_CLK_ISP_CAM),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_isp_sysmmu[] = {
	CLK(ISP_MUX_CLK_ISP_ISPD),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_isp_ppmu[] = {
	CLK(ISP_MUX_CLK_ISP_ISPD),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_isp_bts[] = {
	CLK(ISP_MUX_CLK_ISP_ISPD),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_isp_cam[] = {
	CLK(ISP_MUX_CLK_ISP_CAM),
	CLK(ISP_GATE_CLK_ISP_UID_CLK_ISP_CAM_HALF),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_isp_isp[] = {
	CLK(ISP_MUX_CLK_ISP_ISP),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_isp_vra[] = {
	CLK(ISP_GATE_CLK_ISP_UID_CLK_ISP_VRA),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_mif_adcif[] = {
	CLK(MIF_GATE_CLK_MIF_UID_WRAP_ADC_IF_IPCLKPORT_I_OSC_SYS),
	CLK(MIF_GATE_CLK_MIF_UID_WRAP_ADC_IF_IPCLKPORT_PCLK_S1),
	CLK(MIF_GATE_CLK_MIF_UID_WRAP_ADC_IF_IPCLKPORT_PCLK_S0),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_mif_hsi2c_mif[] = {
	CLK(MIF_GATE_CLK_MIF_UID_ASYNCAPB_HSI2C_CP_IPCLKPORT_PCLKS),
	CLK(MIF_GATE_CLK_MIF_UID_ASYNCAPB_HSI2C_AP_IPCLKPORT_PCLKS),
	CLK(MIF_GATE_CLK_MIF_UID_MODAPIF_V1P5_MIF_CP_IPCLKPORT_PCLK_HSI2C),
	CLK(MIF_GATE_CLK_MIF_UID_MODAPIF_V1P5_MIF_CP_IPCLKPORT_PCLK_HSI2C_BAT_1),
	CLK(MIF_GATE_CLK_MIF_UID_MODAPIF_V1P5_MIF_CP_IPCLKPORT_PCLK_HSI2C_BAT_0),
	CLK(MIF_GATE_CLK_MIF_UID_HSI2C_MIF_IPCLKPORT_iTCLK),
	CLK(MIF_GATE_CLK_MIF_UID_HSI2C_MIF_IPCLKPORT_iPCLK),
	CLK(MIF_GATE_CLK_MIF_UID_ASYNCAPB_HSI2C_CP_IPCLKPORT_PCLKM),
	CLK(MIF_GATE_CLK_MIF_UID_ASYNCAPB_HSI2C_AP_IPCLKPORT_PCLKM),
	CLK_NONE,
};
static struct pwrcal_clk *gategrp_sclk_uart0[] = {
	CLK(FIN_TEMP),
	CLK_NONE,
};




GRPGATE(gate_cpucl0_ppmu, 0, gategrp_cpucl0_ppmu);
GRPGATE(gate_cpucl0_bts, 0, gategrp_cpucl0_bts);
GRPGATE(gate_cpucll_ppmu, 0, gategrp_cpucl1_ppmu);
GRPGATE(gate_cpucll_bts, 0, gategrp_cpucl1_bts);

GRPGATE(gate_mfcmscl_common, pxmxdx_mfcmscl, gategrp_mfcmscl_common);
GRPGATE(gate_mfcmscl_common_mscl, gate_mfcmscl_common, gategrp_mfcmscl_common_mscl);
GRPGATE(gate_mfcmscl_sysmmu_mfc, gate_mfcmscl_mfc, gategrp_mfcmscl_sysmmu_mfc);
GRPGATE(gate_mfcmscl_sysmmu_mscl, gate_mfcmscl_common_mscl, gategrp_mfcmscl_sysmmu_mscl);
GRPGATE(gate_mfcmscl_ppmu, gate_mfcmscl_common, gategrp_mfcmscl_ppmu);
GRPGATE(gate_mfcmscl_bts, gate_mfcmscl_common, gategrp_mfcmscl_bts);
GRPGATE(gate_mfcmscl_mscl_bi, gate_mfcmscl_common_mscl, gategrp_mfcmscl_mscl_bi);
GRPGATE(gate_mfcmscl_mscl_poly, gate_mfcmscl_common_mscl, gategrp_mfcmscl_mscl_poly);
GRPGATE(gate_mfcmscl_jpeg, gate_mfcmscl_common_mscl, gategrp_mfcmscl_jpeg);
GRPGATE(gate_mfcmscl_mfc, gate_mfcmscl_common, gategrp_mfcmscl_mfc);

GRPGATE(gate_g3d_common, dvfs_g3d, gategrp_g3d_common);
GRPGATE(gate_g3d_sysmmu, gate_g3d_common, gategrp_g3d_sysmmu);
GRPGATE(gate_g3d_ppmu, gate_g3d_common, gategrp_g3d_ppmu);
GRPGATE(gate_g3d_bts, gate_g3d_common, gategrp_g3d_bts);
GRPGATE(gate_g3d_g3d, gate_g3d_common, gategrp_g3d_g3d);

GRPGATE(gate_peri_peric0, 0, gategrp_peri_peric0);
GRPGATE(gate_peri_peric1, 0, gategrp_peri_peric1);
GRPGATE(gate_peri_pwm_motor, gate_peri_peric1, gategrp_peri_pwm_motor);
GRPGATE(gate_peri_sclk_pwm_motor, 0, gategrp_peri_sclk_pwm_motor);
GRPGATE(gate_peri_mct, 0, gategrp_peri_mct);
GRPGATE(gate_peri_i2c_sensor2, gate_peri_peric0, gategrp_peri_i2c_sensor2);
GRPGATE(gate_peri_i2c_sensor1, gate_peri_peric0, gategrp_peri_i2c_sensor1);
GRPGATE(gate_peri_i2c_tsp, gate_peri_peric0, gategrp_peri_i2c_tsp);
GRPGATE(gate_peri_i2c_touchkey, gate_peri_peric0, gategrp_peri_i2c_touchkey);
GRPGATE(gate_peri_i2c_fuelgauge, gate_peri_peric0, gategrp_peri_i2c_fuelgauge);
GRPGATE(gate_peri_i2c_spkamp, gate_peri_peric0, gategrp_peri_i2c_spkamp);
GRPGATE(gate_peri_i2c_nfc, gate_peri_peric0, gategrp_peri_i2c_nfc);
GRPGATE(gate_peri_i2c_muic, gate_peri_peric0, gategrp_peri_i2c_muic);
GRPGATE(gate_peri_i2c_ifpmic, gate_peri_peric0, gategrp_peri_i2c_ifpmic);
GRPGATE(gate_peri_hsi2c_frontcam, gate_peri_peric1, gategrp_peri_hsi2c_frontcam);
GRPGATE(gate_peri_hsi2c_maincam, gate_peri_peric1, gategrp_peri_hsi2c_maincam);
GRPGATE(gate_peri_hsi2c_depthcam, gate_peri_peric0, gategrp_peri_hsi2c_depthcam);
GRPGATE(gate_peri_hsi2c_frontsensor, gate_peri_peric0, gategrp_peri_hsi2c_frontsensor);
GRPGATE(gate_peri_hsi2c_rearaf, gate_peri_peric0, gategrp_peri_hsi2c_rearaf);
GRPGATE(gate_peri_hsi2c_rearsensor, gate_peri_peric0, gategrp_peri_hsi2c_rearsensor);
GRPGATE(gate_peri_gpio_touch, gate_peri_peric1, gategrp_peri_gpio_touch);
GRPGATE(gate_peri_gpio_top, gate_peri_peric1, gategrp_peri_gpio_top);
GRPGATE(gate_peri_gpio_nfc, gate_peri_peric1, gategrp_peri_gpio_nfc);
GRPGATE(gate_peri_gpio_ese, gate_peri_peric1, gategrp_peri_gpio_ese);
GRPGATE(gate_peri_wdt_cpucl1, 0, gategrp_peri_wdt_cpucl1);
GRPGATE(gate_peri_wdt_cpucl0, 0, gategrp_peri_wdt_cpucl0);
GRPGATE(gate_peri_uart_debug, sclk_uart_debug, gategrp_peri_uart_debug);
GRPGATE(gate_peri_uart_btwififm, sclk_uart_btwififm, gategrp_peri_uart_btwififm);
GRPGATE(gate_peri_uart_sensor, sclk_uart_sensor, gategrp_peri_uart_sensor);
GRPGATE(gate_peri_tmu_g3d, 0, gategrp_peri_tmu_g3d);
GRPGATE(gate_peri_tmu_cpucl1, 0, gategrp_peri_tmu_cpucl1);
GRPGATE(gate_peri_tmu_cpucl0, 0, gategrp_peri_tmu_cpucl0);
GRPGATE(gate_peri_spi_sensorhub, sclk_spi_sensorhub, gategrp_peri_spi_sensorhub);
GRPGATE(gate_peri_spi_voiceprocessor, sclk_spi_voiceprocessor, gategrp_peri_spi_voiceprocessor);
GRPGATE(gate_peri_spi_ese, sclk_spi_ese, gategrp_peri_spi_ese);
GRPGATE(gate_peri_spi_rearfrom, sclk_spi_rearfrom, gategrp_peri_spi_rearfrom);
GRPGATE(gate_peri_spi_frontfrom, sclk_spi_frontfrom, gategrp_peri_spi_frontfrom);
GRPGATE(gate_peri_gpio_alive, gate_peri_peric1, gategrp_peri_gpio_alive);
GRPGATE(gate_peri_chipid, 0, gategrp_peri_chipid);
GRPGATE(gate_peri_otp_con_top, 0, gategrp_peri_otp_con_top);
GRPGATE(gate_peri_rtc_alive, 0, gategrp_peri_rtc_alive);
GRPGATE(gate_peri_rtc_top, 0, gategrp_peri_rtc_top);

GRPGATE(gate_fsys_common, 0, gategrp_fsys_common);
GRPGATE(gate_fsys_common_sub1, gate_fsys_common, gategrp_fsys_common_sub1);
GRPGATE(gate_fsys_common_sub2, gate_fsys_common, gategrp_fsys_common_sub2);
GRPGATE(gate_fsys_sysmmu, gate_fsys_common, gategrp_fsys_sysmmu);
GRPGATE(gate_fsys_ppmu, gate_fsys_common, gategrp_fsys_ppmu);
GRPGATE(gate_fsys_bts, gate_fsys_common, gategrp_fsys_bts);
GRPGATE(gate_fsys_usb20drd, gate_fsys_common_sub2, gategrp_fsys_usb20drd);
GRPGATE(gate_fsys_mmc0, gate_fsys_common_sub2, gategrp_fsys_mmc0);
GRPGATE(gate_fsys_sclk_mmc0, sclk_mmc0, gategrp_fsys_sclk_mmc0);
GRPGATE(gate_fsys_mmc1, gate_fsys_common_sub2, gategrp_fsys_mmc1);
GRPGATE(gate_fsys_sclk_mmc1, sclk_mmc1, gategrp_fsys_sclk_mmc1);
GRPGATE(gate_fsys_mmc2, gate_fsys_common_sub2, gategrp_fsys_mmc2);
GRPGATE(gate_fsys_sclk_mmc2, sclk_mmc2, gategrp_fsys_sclk_mmc2);
GRPGATE(gate_fsys_sss, gate_fsys_common_sub1, gategrp_fsys_sss);
GRPGATE(gate_fsys_rtic, gate_fsys_common_sub1, gategrp_fsys_rtic);
GRPGATE(gate_fsys_pdma0, gate_fsys_common_sub1, gategrp_fsys_pdma0);
GRPGATE(gate_fsys_pdma1, gate_fsys_common_sub1, gategrp_fsys_pdma1);
GRPGATE(gate_fsys_sromc, gate_fsys_common_sub2, gategrp_fsys_sromc);

GRPGATE(gate_dispaud_disp, pxmxdx_disp, gategrp_dispaud_disp);
GRPGATE(gate_dispaud_aud, pxmxdx_disp, gategrp_dispaud_aud);
GRPGATE(gate_dispaud_sysmmu, gate_dispaud_disp, gategrp_dispaud_sysmmu);
GRPGATE(gate_dispaud_ppmu, gate_dispaud_disp, gategrp_dispaud_ppmu);
GRPGATE(gate_dispaud_bts, gate_dispaud_disp, gategrp_dispaud_bts);
GRPGATE(gate_dispaud_decon, gate_dispaud_disp, gategrp_dispaud_decon);
GRPGATE(gate_dispaud_dsim0, gate_dispaud_disp, gategrp_dispaud_dsim0);
GRPGATE(gate_dispaud_mixer, gate_dispaud_aud, gategrp_dispaud_mixer);
GRPGATE(gate_dispaud_mi2s_aud, gate_dispaud_aud, gategrp_dispaud_mi2s_aud);
GRPGATE(gate_dispaud_mi2s_amp, gate_dispaud_aud, gategrp_dispaud_mi2s_amp);

GRPGATE(gate_isp_common, dvfs_cam, gategrp_isp_common);
GRPGATE(gate_isp_sysmmu, gate_isp_common, gategrp_isp_sysmmu);
GRPGATE(gate_isp_ppmu, gate_isp_common, gategrp_isp_ppmu);
GRPGATE(gate_isp_bts, gate_isp_common, gategrp_isp_bts);
GRPGATE(gate_isp_cam, gate_isp_common, gategrp_isp_cam);
GRPGATE(gate_isp_isp, gate_isp_common, gategrp_isp_isp);
GRPGATE(gate_isp_vra, gate_isp_common, gategrp_isp_vra);

GRPGATE(gate_mif_adcif, 0, gategrp_mif_adcif);
GRPGATE(gate_mif_hsi2c_mif, 0, gategrp_mif_hsi2c_mif);

GRPGATE(sclk_uart0, 0, gategrp_sclk_uart0);


UMUX(umux_dispaud_clkphy_dispaud_mipiphy_txbyteclkhs_user, 0, DISPAUD_MUX_CLKPHY_DISPAUD_MIPIPHY_TXBYTECLKHS_USER);
UMUX(umux_dispaud_clkphy_dispaud_mipiphy_rxclkesc0_user, 0, DISPAUD_MUX_CLKPHY_DISPAUD_MIPIPHY_RXCLKESC0_USER);
UMUX(umux_fsys_clkphy_fsys_usb20drd_phyclock_user, 0, FSYS_MUX_CLKPHY_FSYS_USB20DRD_PHYCLOCK_USER);
UMUX(umux_fsys_clkphy_fsys_ufs_tx0_symbol_user, 0, FSYS_MUX_CLKPHY_FSYS_UFS_TX0_SYMBOL_USER);
UMUX(umux_fsys_clkphy_fsys_ufs_rx0_symbol_user, 0, FSYS_MUX_CLKPHY_FSYS_UFS_RX0_SYMBOL_USER);
UMUX(umux_isp_clkphy_isp_s_rxbyteclkhs0_s4_user, 0, ISP_MUX_CLKPHY_ISP_S_RXBYTECLKHS0_S4_USER);
UMUX(umux_isp_clkphy_isp_s_rxbyteclkhs0_s4s_user, 0, ISP_MUX_CLKPHY_ISP_S_RXBYTECLKHS0_S4S_USER);


void vclk_unused_disable(void)
{
	vclk_disable(VCLK(gate_cpucl0_ppmu));
	vclk_disable(VCLK(gate_cpucl0_bts));
	vclk_disable(VCLK(gate_cpucll_ppmu));
	vclk_disable(VCLK(gate_cpucll_bts));
	vclk_disable(VCLK(gate_mfcmscl_sysmmu_mscl));
	vclk_disable(VCLK(gate_mfcmscl_sysmmu_mfc));
	vclk_disable(VCLK(gate_mfcmscl_ppmu));
	vclk_disable(VCLK(gate_mfcmscl_bts));
	vclk_disable(VCLK(gate_mfcmscl_common));
	vclk_disable(VCLK(gate_mfcmscl_mscl_bi));
	vclk_disable(VCLK(gate_mfcmscl_mscl_poly));
	vclk_disable(VCLK(gate_mfcmscl_jpeg));
	vclk_disable(VCLK(gate_mfcmscl_mfc));
	vclk_disable(VCLK(gate_g3d_common));
	vclk_disable(VCLK(gate_g3d_sysmmu));
	vclk_disable(VCLK(gate_g3d_ppmu));
	vclk_disable(VCLK(gate_g3d_bts));
	vclk_disable(VCLK(gate_g3d_g3d));
//	vclk_disable(VCLK(gate_peri_pwm_motor));
//	vclk_disable(VCLK(gate_peri_sclk_pwm_motor));
	vclk_disable(VCLK(gate_peri_i2c_sensor2));
	vclk_disable(VCLK(gate_peri_i2c_sensor1));
	vclk_disable(VCLK(gate_peri_i2c_tsp));
	vclk_disable(VCLK(gate_peri_i2c_touchkey));
	vclk_disable(VCLK(gate_peri_i2c_fuelgauge));
	vclk_disable(VCLK(gate_peri_i2c_spkamp));
	vclk_disable(VCLK(gate_peri_i2c_nfc));
	vclk_disable(VCLK(gate_peri_i2c_muic));
	vclk_disable(VCLK(gate_peri_i2c_ifpmic));
	vclk_disable(VCLK(gate_peri_hsi2c_frontcam));
	vclk_disable(VCLK(gate_peri_hsi2c_maincam));
	vclk_disable(VCLK(gate_peri_hsi2c_depthcam));
	vclk_disable(VCLK(gate_peri_hsi2c_frontsensor));
	vclk_disable(VCLK(gate_peri_hsi2c_rearaf));
	vclk_disable(VCLK(gate_peri_hsi2c_rearsensor));
//	vclk_disable(VCLK(gate_peri_uart_debug));
//	vclk_disable(VCLK(gate_peri_uart_btwififm));
//	vclk_disable(VCLK(gate_peri_uart_sensor));
//	vclk_disable(VCLK(gate_peri_tmu_g3d));
//	vclk_disable(VCLK(gate_peri_tmu_cpucl1));
//	vclk_disable(VCLK(gate_peri_tmu_cpucl0));
	vclk_disable(VCLK(gate_peri_spi_sensorhub));
	vclk_disable(VCLK(gate_peri_spi_voiceprocessor));
	vclk_disable(VCLK(gate_peri_spi_ese));
	vclk_disable(VCLK(gate_peri_spi_rearfrom));
	vclk_disable(VCLK(gate_peri_spi_frontfrom));
	vclk_disable(VCLK(gate_peri_chipid));
	vclk_disable(VCLK(gate_peri_otp_con_top));
	vclk_disable(VCLK(gate_peri_rtc_alive));
	vclk_disable(VCLK(gate_peri_rtc_top));

	vclk_disable(VCLK(gate_fsys_sysmmu));
	vclk_disable(VCLK(gate_fsys_ppmu));
	vclk_disable(VCLK(gate_fsys_bts));
	vclk_disable(VCLK(gate_fsys_usb20drd));
	vclk_disable(VCLK(gate_fsys_mmc0));
	vclk_disable(VCLK(gate_fsys_mmc1));
	vclk_disable(VCLK(gate_fsys_mmc2));
	vclk_disable(VCLK(gate_fsys_sss));
	vclk_disable(VCLK(gate_fsys_rtic));
	vclk_disable(VCLK(gate_fsys_pdma0));
	vclk_disable(VCLK(gate_fsys_pdma1));
	vclk_disable(VCLK(gate_fsys_sromc));
//	vclk_disable(VCLK(gate_dispaud_sysmmu));
//	vclk_disable(VCLK(gate_dispaud_ppmu));
//	vclk_disable(VCLK(gate_dispaud_bts));
//	vclk_disable(VCLK(gate_dispaud_decon));
//	vclk_disable(VCLK(gate_dispaud_dsim0));
//	vclk_disable(VCLK(gate_dispaud_mixer));
//	vclk_disable(VCLK(gate_dispaud_mi2s_aud));
//	vclk_disable(VCLK(gate_dispaud_mi2s_amp));
//	vclk_disable(VCLK(gate_dispaud_disp));
//	vclk_disable(VCLK(gate_dispaud_aud));
	vclk_disable(VCLK(gate_isp_sysmmu));
	vclk_disable(VCLK(gate_isp_ppmu));
	vclk_disable(VCLK(gate_isp_bts));
	vclk_disable(VCLK(gate_isp_cam));
	vclk_disable(VCLK(gate_isp_isp));
	vclk_disable(VCLK(gate_isp_vra));
	vclk_disable(VCLK(gate_mif_adcif));
	vclk_disable(VCLK(gate_mif_hsi2c_mif));


//	vclk_disable(VCLK(sclk_decon_vclk));
//	vclk_disable(VCLK(sclk_decon_vclk_local));
//	vclk_disable(VCLK(sclk_decon_eclk));
//	vclk_disable(VCLK(sclk_decon_eclk_local));
	vclk_disable(VCLK(sclk_mmc0));
	vclk_disable(VCLK(sclk_mmc1));
	vclk_disable(VCLK(sclk_mmc2));
	vclk_disable(VCLK(sclk_ufsunipro));
	vclk_disable(VCLK(sclk_ufsunipro_cfg));
	vclk_disable(VCLK(sclk_usb20drd));
//	vclk_disable(VCLK(sclk_uart_sensor));
//	vclk_disable(VCLK(sclk_uart_btwififm));
//	vclk_disable(VCLK(sclk_uart_debug));
	vclk_disable(VCLK(sclk_spi_frontfrom));
	vclk_disable(VCLK(sclk_spi_rearfrom));
	vclk_disable(VCLK(sclk_spi_ese));
	vclk_disable(VCLK(sclk_spi_voiceprocessor));
	vclk_disable(VCLK(sclk_spi_sensorhub));
	vclk_disable(VCLK(sclk_isp_sensor0));
	vclk_disable(VCLK(sclk_isp_sensor1));
	vclk_disable(VCLK(sclk_isp_sensor2));

//	vclk_disable(VCLK(p1_disp_pll));
	vclk_disable(VCLK(p1_aud_pll));
	vclk_disable(VCLK(p1_usb_pll));
	vclk_disable(VCLK(p1_isp_pll));

	vclk_disable(VCLK(pxmxdx_top));
//	vclk_disable(VCLK(pxmxdx_disp));
	vclk_disable(VCLK(pxmxdx_mfcmscl));
	vclk_disable(VCLK(pxmxdx_isp_vra));
	vclk_disable(VCLK(pxmxdx_isp_cam));
	vclk_disable(VCLK(pxmxdx_isp_isp));

//	vclk_disable(VCLK(umux_dispaud_clkphy_dispaud_mipiphy_txbyteclkhs_user));
//	vclk_disable(VCLK(umux_dispaud_clkphy_dispaud_mipiphy_rxclkesc0_user));
	vclk_disable(VCLK(umux_fsys_clkphy_fsys_usb20drd_phyclock_user));
	vclk_disable(VCLK(umux_fsys_clkphy_fsys_ufs_tx0_symbol_user));
	vclk_disable(VCLK(umux_fsys_clkphy_fsys_ufs_rx0_symbol_user));
	vclk_disable(VCLK(umux_isp_clkphy_isp_s_rxbyteclkhs0_s4_user));
	vclk_disable(VCLK(umux_isp_clkphy_isp_s_rxbyteclkhs0_s4s_user));

}

void vclk_init(void)
{
	ADD_LIST(vclk_pxmxdx_list, pxmxdx_top);
	ADD_LIST(vclk_pxmxdx_list, pxmxdx_disp);
	ADD_LIST(vclk_pxmxdx_list, pxmxdx_mfcmscl);
	ADD_LIST(vclk_pxmxdx_list, pxmxdx_isp_vra);
	ADD_LIST(vclk_pxmxdx_list, pxmxdx_isp_cam);
	ADD_LIST(vclk_pxmxdx_list, pxmxdx_isp_isp);
	ADD_LIST(vclk_pxmxdx_list, pxmxdx_oscclk_aud);

	ADD_LIST(vclk_p1_list, p1_disp_pll);
	ADD_LIST(vclk_p1_list, p1_aud_pll);
	ADD_LIST(vclk_p1_list, p1_usb_pll);
	ADD_LIST(vclk_p1_list, p1_isp_pll);

	ADD_LIST(vclk_m1_list, m1_dummy);

	ADD_LIST(vclk_d1_list, d1_dispaud_mi2s);
	ADD_LIST(vclk_d1_list, d1_dispaud_mixer);


	ADD_LIST(vclk_m1d1g1_list, sclk_decon_vclk);
	ADD_LIST(vclk_m1d1g1_list, sclk_decon_vclk_local);
	ADD_LIST(vclk_m1d1g1_list, sclk_decon_eclk);
	ADD_LIST(vclk_m1d1g1_list, sclk_decon_eclk_local);
	ADD_LIST(vclk_m1d1g1_list, sclk_mmc0);
	ADD_LIST(vclk_m1d1g1_list, sclk_mmc1);
	ADD_LIST(vclk_m1d1g1_list, sclk_mmc2);
	ADD_LIST(vclk_m1d1g1_list, sclk_ufsunipro);
	ADD_LIST(vclk_m1d1g1_list, sclk_ufsunipro_cfg);
	ADD_LIST(vclk_m1d1g1_list, sclk_usb20drd);
	ADD_LIST(vclk_m1d1g1_list, sclk_uart_sensor);
	ADD_LIST(vclk_m1d1g1_list, sclk_uart_btwififm);
	ADD_LIST(vclk_m1d1g1_list, sclk_uart_debug);
	ADD_LIST(vclk_m1d1g1_list, sclk_spi_frontfrom);
	ADD_LIST(vclk_m1d1g1_list, sclk_spi_rearfrom);
	ADD_LIST(vclk_m1d1g1_list, sclk_spi_ese);
	ADD_LIST(vclk_m1d1g1_list, sclk_spi_voiceprocessor);
	ADD_LIST(vclk_m1d1g1_list, sclk_spi_sensorhub);
	ADD_LIST(vclk_m1d1g1_list, sclk_isp_sensor0);
	ADD_LIST(vclk_m1d1g1_list, sclk_isp_sensor1);
	ADD_LIST(vclk_m1d1g1_list, sclk_isp_sensor2);


	ADD_LIST(vclk_grpgate_list, gate_cpucl0_ppmu);
	ADD_LIST(vclk_grpgate_list, gate_cpucl0_bts);
	ADD_LIST(vclk_grpgate_list, gate_cpucll_ppmu);
	ADD_LIST(vclk_grpgate_list, gate_cpucll_bts);

	ADD_LIST(vclk_grpgate_list, gate_mfcmscl_common);
	ADD_LIST(vclk_grpgate_list, gate_mfcmscl_common_mscl);
	ADD_LIST(vclk_grpgate_list, gate_mfcmscl_sysmmu_mscl);
	ADD_LIST(vclk_grpgate_list, gate_mfcmscl_sysmmu_mfc);
	ADD_LIST(vclk_grpgate_list, gate_mfcmscl_ppmu);
	ADD_LIST(vclk_grpgate_list, gate_mfcmscl_bts);
	ADD_LIST(vclk_grpgate_list, gate_mfcmscl_mscl_bi);
	ADD_LIST(vclk_grpgate_list, gate_mfcmscl_mscl_poly);
	ADD_LIST(vclk_grpgate_list, gate_mfcmscl_jpeg);
	ADD_LIST(vclk_grpgate_list, gate_mfcmscl_mfc);

	ADD_LIST(vclk_grpgate_list, gate_g3d_common);
	ADD_LIST(vclk_grpgate_list, gate_g3d_sysmmu);
	ADD_LIST(vclk_grpgate_list, gate_g3d_ppmu);
	ADD_LIST(vclk_grpgate_list, gate_g3d_bts);
	ADD_LIST(vclk_grpgate_list, gate_g3d_g3d);

	ADD_LIST(vclk_grpgate_list, gate_peri_peric0);
	ADD_LIST(vclk_grpgate_list, gate_peri_peric1);
	ADD_LIST(vclk_grpgate_list, gate_peri_pwm_motor);
	ADD_LIST(vclk_grpgate_list, gate_peri_sclk_pwm_motor);
	ADD_LIST(vclk_grpgate_list, gate_peri_mct);
	ADD_LIST(vclk_grpgate_list, gate_peri_i2c_sensor2);
	ADD_LIST(vclk_grpgate_list, gate_peri_i2c_sensor1);
	ADD_LIST(vclk_grpgate_list, gate_peri_i2c_tsp);
	ADD_LIST(vclk_grpgate_list, gate_peri_i2c_touchkey);
	ADD_LIST(vclk_grpgate_list, gate_peri_i2c_fuelgauge);
	ADD_LIST(vclk_grpgate_list, gate_peri_i2c_spkamp);
	ADD_LIST(vclk_grpgate_list, gate_peri_i2c_nfc);
	ADD_LIST(vclk_grpgate_list, gate_peri_i2c_muic);
	ADD_LIST(vclk_grpgate_list, gate_peri_i2c_ifpmic);
	ADD_LIST(vclk_grpgate_list, gate_peri_hsi2c_frontcam);
	ADD_LIST(vclk_grpgate_list, gate_peri_hsi2c_maincam);
	ADD_LIST(vclk_grpgate_list, gate_peri_hsi2c_depthcam);
	ADD_LIST(vclk_grpgate_list, gate_peri_hsi2c_frontsensor);
	ADD_LIST(vclk_grpgate_list, gate_peri_hsi2c_rearaf);
	ADD_LIST(vclk_grpgate_list, gate_peri_hsi2c_rearsensor);
	ADD_LIST(vclk_grpgate_list, gate_peri_gpio_touch);
	ADD_LIST(vclk_grpgate_list, gate_peri_gpio_top);
	ADD_LIST(vclk_grpgate_list, gate_peri_gpio_nfc);
	ADD_LIST(vclk_grpgate_list, gate_peri_gpio_ese);
	ADD_LIST(vclk_grpgate_list, gate_peri_wdt_cpucl1);
	ADD_LIST(vclk_grpgate_list, gate_peri_wdt_cpucl0);
	ADD_LIST(vclk_grpgate_list, gate_peri_uart_debug);
	ADD_LIST(vclk_grpgate_list, gate_peri_uart_btwififm);
	ADD_LIST(vclk_grpgate_list, gate_peri_uart_sensor);
	ADD_LIST(vclk_grpgate_list, gate_peri_tmu_g3d);
	ADD_LIST(vclk_grpgate_list, gate_peri_tmu_cpucl1);
	ADD_LIST(vclk_grpgate_list, gate_peri_tmu_cpucl0);
	ADD_LIST(vclk_grpgate_list, gate_peri_spi_sensorhub);
	ADD_LIST(vclk_grpgate_list, gate_peri_spi_voiceprocessor);
	ADD_LIST(vclk_grpgate_list, gate_peri_spi_ese);
	ADD_LIST(vclk_grpgate_list, gate_peri_spi_rearfrom);
	ADD_LIST(vclk_grpgate_list, gate_peri_spi_frontfrom);
	ADD_LIST(vclk_grpgate_list, gate_peri_gpio_alive);
	ADD_LIST(vclk_grpgate_list, gate_peri_chipid);
	ADD_LIST(vclk_grpgate_list, gate_peri_otp_con_top);
	ADD_LIST(vclk_grpgate_list, gate_peri_rtc_alive);
	ADD_LIST(vclk_grpgate_list, gate_peri_rtc_top);

	ADD_LIST(vclk_grpgate_list, gate_fsys_common);
	ADD_LIST(vclk_grpgate_list, gate_fsys_common_sub1);
	ADD_LIST(vclk_grpgate_list, gate_fsys_common_sub2);
	ADD_LIST(vclk_grpgate_list, gate_fsys_sysmmu);
	ADD_LIST(vclk_grpgate_list, gate_fsys_ppmu);
	ADD_LIST(vclk_grpgate_list, gate_fsys_bts);
	ADD_LIST(vclk_grpgate_list, gate_fsys_usb20drd);
	ADD_LIST(vclk_grpgate_list, gate_fsys_mmc0);
	ADD_LIST(vclk_grpgate_list, gate_fsys_sclk_mmc0);
	ADD_LIST(vclk_grpgate_list, gate_fsys_mmc1);
	ADD_LIST(vclk_grpgate_list, gate_fsys_sclk_mmc1);
	ADD_LIST(vclk_grpgate_list, gate_fsys_mmc2);
	ADD_LIST(vclk_grpgate_list, gate_fsys_sclk_mmc2);
	ADD_LIST(vclk_grpgate_list, gate_fsys_sss);
	ADD_LIST(vclk_grpgate_list, gate_fsys_rtic);
	ADD_LIST(vclk_grpgate_list, gate_fsys_pdma0);
	ADD_LIST(vclk_grpgate_list, gate_fsys_pdma1);
	ADD_LIST(vclk_grpgate_list, gate_fsys_sromc);

	ADD_LIST(vclk_grpgate_list, gate_dispaud_disp);
	ADD_LIST(vclk_grpgate_list, gate_dispaud_aud);
	ADD_LIST(vclk_grpgate_list, gate_dispaud_sysmmu);
	ADD_LIST(vclk_grpgate_list, gate_dispaud_ppmu);
	ADD_LIST(vclk_grpgate_list, gate_dispaud_bts);
	ADD_LIST(vclk_grpgate_list, gate_dispaud_decon);
	ADD_LIST(vclk_grpgate_list, gate_dispaud_dsim0);
	ADD_LIST(vclk_grpgate_list, gate_dispaud_mixer);
	ADD_LIST(vclk_grpgate_list, gate_dispaud_mi2s_aud);
	ADD_LIST(vclk_grpgate_list, gate_dispaud_mi2s_amp);

	ADD_LIST(vclk_grpgate_list, gate_isp_common);
	ADD_LIST(vclk_grpgate_list, gate_isp_sysmmu);
	ADD_LIST(vclk_grpgate_list, gate_isp_ppmu);
	ADD_LIST(vclk_grpgate_list, gate_isp_bts);
	ADD_LIST(vclk_grpgate_list, gate_isp_cam);
	ADD_LIST(vclk_grpgate_list, gate_isp_isp);
	ADD_LIST(vclk_grpgate_list, gate_isp_vra);
	ADD_LIST(vclk_grpgate_list, gate_mif_adcif);
	ADD_LIST(vclk_grpgate_list, gate_mif_hsi2c_mif);
	ADD_LIST(vclk_grpgate_list, sclk_uart0);

	ADD_LIST(vclk_umux_list, umux_dispaud_clkphy_dispaud_mipiphy_txbyteclkhs_user);
	ADD_LIST(vclk_umux_list, umux_dispaud_clkphy_dispaud_mipiphy_rxclkesc0_user);
	ADD_LIST(vclk_umux_list, umux_fsys_clkphy_fsys_usb20drd_phyclock_user);
	ADD_LIST(vclk_umux_list, umux_fsys_clkphy_fsys_ufs_tx0_symbol_user);
	ADD_LIST(vclk_umux_list, umux_fsys_clkphy_fsys_ufs_rx0_symbol_user);
	ADD_LIST(vclk_umux_list, umux_isp_clkphy_isp_s_rxbyteclkhs0_s4_user);
	ADD_LIST(vclk_umux_list, umux_isp_clkphy_isp_s_rxbyteclkhs0_s4s_user);

	ADD_LIST(vclk_dfs_list, dvfs_cpucl0);
	ADD_LIST(vclk_dfs_list, dvfs_cpucl1);
	ADD_LIST(vclk_dfs_list, dvfs_g3d);
	ADD_LIST(vclk_dfs_list, dvfs_mif);
	ADD_LIST(vclk_dfs_list, dvfs_int);
	ADD_LIST(vclk_dfs_list, dvfs_disp);
	ADD_LIST(vclk_dfs_list, dvfs_cam);

	return;
}
