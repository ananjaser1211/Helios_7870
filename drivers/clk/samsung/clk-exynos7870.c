/*
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This file contains clocks of Exynos7870.
 */

#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <dt-bindings/clock/exynos7870.h>
#include "../../soc/samsung/pwrcal/S5E7870/S5E7870-vclk.h"
#include "composite.h"

#if defined(CONFIG_ECT)
#include <soc/samsung/ect_parser.h>
#endif

enum exynos7870_clks {
	none,

	oscclk = 1,

	/* The group of clocks in mfcmscl */
	mscl_sysmmu = 10, mfc_sysmmu, mfcmscl_ppmu, mfcmscl_bts, gate_mscl_bi, gate_mscl_poly, gate_jpeg, gate_mfc,

	/* The group of clocks in g3d */
	g3d_sysmmu = 50, g3d_ppmu, g3d_bts, gate_g3d,

	/* The group of clocks related with pwm and mct in peri */
	peri_pwm_motor = 100, peri_sclk_pwm_motor, peri_mct,

	/* The group of clocks related with i2c in peri */
	i2c_sensor1 = 110, i2c_sensor2, i2c_tsp, i2c_touchkey, i2c_fuelgauge, i2c_spkamp, i2c_nfc, i2c_muic, i2c_ifpmic,

	/* The group of clocks related with hsi2c in peri */
	hsi2c_frontcam = 130, hsi2c_maincam, hsi2c_depthcam, hsi2c_frontsensor, hsi2c_rearaf, hsi2c_rearsensor,

	/* The group of clocks related with gpio in peri */
	gpio_touch = 150, gpio_top, gpio_nfc, gpio_ese, gpio_alive,

	/* The group of clocks related with wdt in peri */
	wdt_cpucl0 = 160, wdt_cpucl1,

	/* The group of clocks related with uart in peri */
	uart_debug = 170, uart_btwififm, uart_sensor,

	/* The group of clocks related with tmu in peri */
	peri_tmu_g3d = 180, peri_tmu_cpucl1, peri_tmu_cpucl0,

	 /* The group of clocks related with spi in peri */
	peri_spi_sensorhub = 190, peri_spi_voiceprocessor, peri_spi_ese, peri_spi_rearfrom, peri_spi_frontfrom,

	/* The group of clocks related with rtc in peri */
	peri_rtc_alive = 210, peri_rtc_top,

	/* The group of etc clocks in peri */
	peri_chipid = 220, peri_otp_con_top,


	/* The group of clocks in fsys */
	fsys_sysmmu = 300, fsys_ppmu, fsys_bts,
	fsys_mmc0 = 310, fsys_mmc1, fsys_mmc2, fsys_sclk_mmc0, fsys_sclk_mmc1, fsys_sclk_mmc2,
	fsys_sss = 330,  fsys_rtic, fsys_pdma0, fsys_pdma1, fsys_sromc, fsys_usb20drd, fsys_usb20drd_phyclock,
	usb_pll = 350,

	/* The group of clocks in dispaud */
	dispaud_sysmmu = 400, dispaud_ppmu, dispaud_bts,
	dispaud_decon = 410, dispaud_dsim0, dispaud_mixer, dispaud_mi2s_aud, dispaud_mi2s_amp,
	dispaud_bus = 430, dispaud_decon_int_vclk, dispaud_decon_int_eclk, dispaud_mipiphy_txbyteclkhs, dispaud_mipiphy_rxclkesc0,
	decon_vclk = 450, decon_vclk_local, decon_eclk, decon_eclk_local,
	disp_pll = 460, aud_pll, d1_i2s, d1_mixer,
	oscclk_aud = 470,

	/* The group of clocks in isp */
	isp_sysmmu = 500, isp_ppmu, isp_bts,
	isp_cam = 510, isp_isp, isp_vra, pxmxdx_vra, pxmxdx_cam, pxmxdx_isp,
	isp_s_rxbyteclkhs0_s4 = 520, isp_s_rxbyteclkhs0_s4s,
	isp_pll = 530,

	/* The group of clocks in mif */
	mif_adcif = 600, mif_hsi2c_mif, mmc0_sclk, mmc1_sclk, mmc2_sclk,
	ufsunipro_sclk = 610, ufsunipro_cfg_sclk, usb20drd_sclk,
	uart_sensor_sclk = 620, uart_btwififm_sclk, uart_debug_sclk,
	spi_frontfrom_sclk = 630, spi_rearfrom_sclk, spi_ese_sclk, spi_voiceprocessor_sclk, spi_sensorhub_sclk,
	isp_sensor0_sclk = 640, isp_sensor1_sclk, isp_sensor2_sclk,

	/* number of dfs driver starts from 2000 */
	dfs_mif = 2000, dfs_mif_sw, dfs_int, dfs_cam, dfs_disp,

	nr_clks,
};

/* fixed rate clocks generated outside the soc */
static struct samsung_fixed_rate exynos7870_fixed_rate_ext_clks[] __initdata = {
	FRATE(oscclk, "fin_pll", NULL, CLK_IS_ROOT, 26000000),
};

static struct of_device_id ext_clk_match[] __initdata = {
	{ .compatible = "samsung,exynos7870-oscclk", .data = (void *)0, },
};

static struct init_vclk exynos7870_mfcmscl_vclks[] __initdata = {
	/* MFC & MSCL ACLK */
	VCLK(mscl_sysmmu, gate_mfcmscl_sysmmu_mscl, "gate_mfcmscl_sysmmu_mscl", 0, 0, NULL),
	VCLK(mfc_sysmmu, gate_mfcmscl_sysmmu_mfc, "gate_mfcmscl_sysmmu_mfc", 0, 0, NULL),
	VCLK(mfcmscl_ppmu, gate_mfcmscl_ppmu, "gate_mfcmscl_ppmu", 0, 0, NULL),
	VCLK(mfcmscl_bts, gate_mfcmscl_bts, "gate_mfcmscl_bts", 0, 0, NULL),
	VCLK(gate_mscl_bi, gate_mfcmscl_mscl_bi, "gate_mfcmscl_mscl_bi", 0, 0, NULL),
	VCLK(gate_mscl_poly, gate_mfcmscl_mscl_poly, "gate_mfcmscl_mscl_poly", 0, 0, NULL),
	VCLK(gate_jpeg, gate_mfcmscl_jpeg, "gate_mfcmscl_jpeg", 0, 0, NULL),
	VCLK(gate_mfc, gate_mfcmscl_mfc, "gate_mfcmscl_mfc", 0, 0, NULL),
};

static struct init_vclk exynos7870_g3d_vclks[] __initdata = {
	/* G3D ACLK */
	VCLK(g3d_sysmmu, gate_g3d_sysmmu, "gate_g3d_sysmmu", 0, 0, NULL),
	VCLK(g3d_ppmu, gate_g3d_ppmu, "gate_g3d_ppmu", 0, 0, NULL),
	VCLK(g3d_bts, gate_g3d_bts, "gate_g3d_bts", 0, 0, "gate_g3d_bts_alias"),
	VCLK(gate_g3d, gate_g3d_g3d, "gate_g3d_g3d", 0, 0, "vclk_g3d"),
};

static struct init_vclk exynos7870_peri_vclks[] __initdata = {
	/* PERI PWM ACLK & SCLK */
	VCLK(peri_pwm_motor, gate_peri_pwm_motor, "gate_peri_pwm_motor", 0, 0, NULL),
	VCLK(peri_sclk_pwm_motor, gate_peri_sclk_pwm_motor, "gate_peri_sclk_pwm_motor", 0, 0, NULL),
	/* PERI MCT ACLK */
	VCLK(peri_mct, gate_peri_mct, "gate_peri_mct", 0, 0, NULL),
	/* PERI I2C ACLK */
	VCLK(i2c_sensor1, gate_peri_i2c_sensor1, "gate_peri_i2c_sensor1", 0, 0, NULL),
	VCLK(i2c_sensor2, gate_peri_i2c_sensor2, "gate_peri_i2c_sensor2", 0, 0, NULL),
	VCLK(i2c_tsp, gate_peri_i2c_tsp, "gate_peri_i2c_tsp", 0, 0, NULL),
	VCLK(i2c_touchkey, gate_peri_i2c_touchkey, "gate_peri_i2c_touchkey", 0, 0, NULL),
	VCLK(i2c_fuelgauge, gate_peri_i2c_fuelgauge, "gate_peri_i2c_fuelgauge", 0, 0, NULL),
	VCLK(i2c_spkamp, gate_peri_i2c_spkamp, "gate_peri_i2c_spkamp", 0, 0, NULL),
	VCLK(i2c_nfc, gate_peri_i2c_nfc, "gate_peri_i2c_nfc", 0, 0, "i2c2_pclk"),
	VCLK(i2c_muic, gate_peri_i2c_muic, "gate_peri_i2c_muic", 0, 0, NULL),
	VCLK(i2c_ifpmic, gate_peri_i2c_ifpmic, "gate_peri_i2c_ifpmic", 0, 0, NULL),
	/* PERI HSI2C ACLK */
	VCLK(hsi2c_frontcam, gate_peri_hsi2c_frontcam, "gate_peri_hsi2c_frontcam", 0, 0, NULL),
	VCLK(hsi2c_maincam, gate_peri_hsi2c_maincam, "gate_peri_hsi2c_maincam", 0, 0, NULL),
	VCLK(hsi2c_depthcam, gate_peri_hsi2c_depthcam, "gate_peri_hsi2c_depthcam", 0, 0, NULL),
	VCLK(hsi2c_frontsensor, gate_peri_hsi2c_frontsensor, "gate_peri_hsi2c_frontsensor", 0, 0, NULL),
	VCLK(hsi2c_rearaf,gate_peri_hsi2c_rearaf, "gate_peri_hsi2c_rearaf", 0, 0, NULL),
	VCLK(hsi2c_rearsensor, gate_peri_hsi2c_rearsensor, "gate_peri_hsi2c_rearsensor", 0, 0, NULL),
	/* PERI GPIO ACLK */
	VCLK(gpio_touch, gate_peri_gpio_touch, "gate_peri_gpio_touch", 0, 0, NULL),
	VCLK(gpio_top, gate_peri_gpio_top, "gate_peri_gpio_top", 0, 0, NULL),
	VCLK(gpio_nfc, gate_peri_gpio_nfc, "gate_peri_gpio_nfc", 0, 0, NULL),
	VCLK(gpio_ese, gate_peri_gpio_ese, "gate_peri_gpio_ese", 0, 0, NULL),
	VCLK(gpio_alive, gate_peri_gpio_alive, "gate_peri_gpio_alive", 0, 0, NULL),
	/* PERI WDT ACLK */
	VCLK(wdt_cpucl0, gate_peri_wdt_cpucl0, "gate_peri_wdt_cpucl0", 0, 0, NULL),
	VCLK(wdt_cpucl1, gate_peri_wdt_cpucl1, "gate_peri_wdt_cpucl1", 0, 0, NULL),
	/* PERI UART ACLK */
	VCLK(uart_debug, gate_peri_uart_debug, "gate_peri_uart_debug", 0, 0, "console-pclk2"),
	VCLK(uart_btwififm, gate_peri_uart_btwififm, "gate_peri_uart_btwififm", 0, 0, NULL),
	VCLK(uart_sensor, gate_peri_uart_sensor, "gate_peri_uart_sensor", 0, 0, NULL),
	/* PERI TMU ACLK */
	VCLK(peri_tmu_g3d, gate_peri_tmu_g3d, "gate_peri_tmu_g3d", 0, 0, NULL),
	VCLK(peri_tmu_cpucl1, gate_peri_tmu_cpucl1, "gate_peri_tmu_cpucl1", 0, 0, NULL),
	VCLK(peri_tmu_cpucl0, gate_peri_tmu_cpucl0, "gate_peri_tmu_cpucl0", 0, 0, NULL),
	/* PERI SPI ACLK */
	VCLK(peri_spi_sensorhub, gate_peri_spi_sensorhub, "gate_peri_spi_sensorhub", 0, 0, NULL),
	VCLK(peri_spi_voiceprocessor, gate_peri_spi_voiceprocessor, "gate_peri_spi_voiceprocessor", 0, 0, NULL),
#ifdef CONFIG_SENSORS_FINGERPRINT
	VCLK(peri_spi_ese, gate_peri_spi_ese, "gate_peri_spi_ese", 0, 0, "fp-spi-pclk"),
#else
	VCLK(peri_spi_ese, gate_peri_spi_ese, "gate_peri_spi_ese", 0, 0, NULL),
#endif
	VCLK(peri_spi_rearfrom, gate_peri_spi_rearfrom, "gate_peri_spi_rearfrom", 0, 0, NULL),
	VCLK(peri_spi_frontfrom, gate_peri_spi_frontfrom, "gate_peri_spi_frontfrom", 0, 0, NULL),
	/* PERI RTC ACLK */
	VCLK(peri_rtc_alive, gate_peri_rtc_alive, "gate_peri_rtc_alive", 0, 0, NULL),
	VCLK(peri_rtc_top, gate_peri_rtc_top, "gate_peri_rtc_top", 0, 0, NULL),
	/* PERI ETC ACLK */
	VCLK(peri_chipid, gate_peri_chipid, "gate_peri_chipid", 0, 0, NULL),
	VCLK(peri_otp_con_top, gate_peri_otp_con_top, "gate_peri_otp_con_top", 0, 0, NULL),
};

static struct init_vclk exynos7870_fsys_vclks[] __initdata = {
	/* FSYS COMMON*/
	VCLK(fsys_sysmmu, gate_fsys_sysmmu, "gate_fsys_sysmmu", 0, 0, NULL),
	VCLK(fsys_ppmu, gate_fsys_ppmu, "gate_fsys_ppmu", 0, 0, NULL),
	VCLK(fsys_bts, gate_fsys_bts, "gate_fsys_bts", 0, 0, NULL),
	VCLK(fsys_usb20drd, gate_fsys_usb20drd, "gate_fsys_usb20drd", 0, 0, NULL),
	VCLK(fsys_mmc0, gate_fsys_mmc0, "gate_fsys_mmc0", 0, 0, NULL),
	VCLK(fsys_mmc1, gate_fsys_mmc1, "gate_fsys_mmc1", 0, 0, NULL),
	VCLK(fsys_mmc2, gate_fsys_mmc2, "gate_fsys_mmc2", 0, 0, NULL),
	VCLK(fsys_sclk_mmc0, gate_fsys_sclk_mmc0, "gate_fsys_sclk_mmc0", 0, 0, NULL),
	VCLK(fsys_sclk_mmc1, gate_fsys_sclk_mmc1, "gate_fsys_sclk_mmc1", 0, 0, NULL),
	VCLK(fsys_sclk_mmc2, gate_fsys_sclk_mmc2, "gate_fsys_sclk_mmc2", 0, 0, NULL),
	VCLK(fsys_sss, gate_fsys_sss, "gate_fsys_sss", 0, 0, NULL),
	VCLK(fsys_rtic, gate_fsys_rtic, "gate_fsys_rtic", 0, 0, NULL),
	VCLK(fsys_pdma0, gate_fsys_pdma0, "gate_fsys_pdma0", 0, 0, NULL),
#ifdef CONFIG_SENSORS_FINGERPRINT
	VCLK(fsys_pdma1, gate_fsys_pdma1, "gate_fsys_pdma1", 0, 0, "apb_pclk"),
#else
	VCLK(fsys_pdma1, gate_fsys_pdma1, "gate_fsys_pdma1", 0, 0, NULL),
#endif
	VCLK(fsys_sromc, gate_fsys_sromc, "gate_fsys_sromc", 0, 0, NULL),
	VCLK(fsys_usb20drd_phyclock, umux_fsys_clkphy_fsys_usb20drd_phyclock_user, "umux_fsys_clkphy_fsys_usb20drd_phyclock_user", 0, 0, NULL),

	VCLK(usb_pll, p1_usb_pll, "p1_usb_pll", 0, 0, NULL),
};

static struct init_vclk exynos7870_dispaud_vclks[] __initdata = {
	/* DISPAUD ACLK */
	VCLK(dispaud_sysmmu, gate_dispaud_sysmmu, "gate_dispaud_sysmmu", 0, 0, NULL),
	VCLK(dispaud_ppmu, gate_dispaud_ppmu, "gate_dispaud_ppmu", 0, 0, NULL),
	VCLK(dispaud_bts, gate_dispaud_bts, "gate_dispaud_bts", 0, 0, NULL),
	VCLK(dispaud_decon, gate_dispaud_decon, "gate_dispaud_decon", 0, 0, NULL),
	VCLK(dispaud_dsim0, gate_dispaud_dsim0, "gate_dispaud_dsim0", 0, 0, NULL),
	VCLK(dispaud_mixer, gate_dispaud_mixer, "gate_dispaud_mixer", 0, 0, NULL),
	VCLK(dispaud_mi2s_aud, gate_dispaud_mi2s_aud, "gate_dispaud_mi2s_aud", 0, 0, NULL),
	VCLK(dispaud_mi2s_amp, gate_dispaud_mi2s_amp, "gate_dispaud_mi2s_amp", 0, 0, NULL),
	/*
	VCLK(dispaud_bus, umux_dispaud_clkcmu_dispaud_bus_user, "umux_dispaud_clkcmu_dispaud_bus_user", 0, 0, NULL),
	VCLK(dispaud_decon_int_vclk, umux_dispaud_clkcmu_dispaud_decon_int_vclk_user, "umux_dispaud_clkcmu_dispaud_decon_int_vclk_user", 0, 0, NULL),
	VCLK(dispaud_decon_int_eclk, umux_dispaud_clkcmu_dispaud_decon_int_eclk_user, "umux_dispaud_clkcmu_dispaud_decon_int_eclk_user", 0, 0, NULL),
	*/
	VCLK(dispaud_mipiphy_txbyteclkhs, umux_dispaud_clkphy_dispaud_mipiphy_txbyteclkhs_user, "umux_dispaud_clkphy_dispaud_mipiphy_txbyteclkhs_user", 0, 0, NULL),
	VCLK(dispaud_mipiphy_rxclkesc0, umux_dispaud_clkphy_dispaud_mipiphy_rxclkesc0_user, "umux_dispaud_clkphy_dispaud_mipiphy_rxclkesc0_user", 0, 0, NULL),

	/* DISPAUD SCLK */
	VCLK(decon_vclk, sclk_decon_vclk, "sclk_decon_vclk", 0, 0, NULL),
	VCLK(decon_vclk_local, sclk_decon_vclk_local, "sclk_decon_vclk_local", 0, 0, NULL),
	VCLK(decon_eclk, sclk_decon_eclk, "sclk_decon_eclk", 0, 0, NULL),
	VCLK(decon_eclk_local, sclk_decon_eclk_local, "sclk_decon_eclk_local", 0, 0, NULL),
	/* DISPAUD PLL */
	VCLK(disp_pll, p1_disp_pll, "p1_disp_pll", 0, 0, NULL),
	VCLK(aud_pll, p1_aud_pll, "p1_aud_pll", 0, 0, NULL),

	VCLK(d1_i2s, d1_dispaud_mi2s, "d1_dispaud_mi2s", 0, 0, NULL),
	VCLK(d1_mixer, d1_dispaud_mixer, "d1_dispaud_mixer", 0, 0, NULL),
	VCLK(oscclk_aud, pxmxdx_oscclk_aud, "pxmxdx_oscclk_aud", 0, 0, NULL),
};
static struct init_vclk exynos7870_isp_vclks[] __initdata = {
	VCLK(isp_sysmmu, gate_isp_sysmmu, "gate_isp_sysmmu", 0, 0, NULL),
	VCLK(isp_ppmu, gate_isp_ppmu, "gate_isp_ppmu", 0, 0, NULL),
	VCLK(isp_bts, gate_isp_bts, "gate_isp_bts", 0, 0, NULL),
	VCLK(isp_cam, gate_isp_cam, "gate_isp_cam", 0, 0, NULL),
	VCLK(isp_isp, gate_isp_isp, "gate_isp_isp", 0, 0, NULL),
	VCLK(isp_vra, gate_isp_vra, "gate_isp_vra", 0, 0, NULL),

	VCLK(pxmxdx_vra, pxmxdx_isp_vra, "pxmxdx_isp_vra", 0, 0, NULL),
	VCLK(pxmxdx_cam, pxmxdx_isp_cam, "pxmxdx_isp_cam", 0, 0, NULL),
	VCLK(pxmxdx_isp, pxmxdx_isp_isp, "pxmxdx_isp_isp", 0, 0, NULL),

	VCLK(isp_s_rxbyteclkhs0_s4, umux_isp_clkphy_isp_s_rxbyteclkhs0_s4_user, "umux_isp_clkphy_isp_s_rxbyteclkhs0_s4_user", 0, 0, NULL),
	VCLK(isp_s_rxbyteclkhs0_s4s, umux_isp_clkphy_isp_s_rxbyteclkhs0_s4s_user, "umux_isp_clkphy_isp_s_rxbyteclkhs0_s4s_user", 0, 0, NULL),

	VCLK(isp_pll, p1_isp_pll, "p1_isp_pll", 0, 0, NULL),
};

static struct init_vclk exynos7870_mif_vclks[] __initdata = {
	VCLK(mif_adcif, gate_mif_adcif, "gate_mif_adcif", 0, 0, NULL),
	VCLK(mif_hsi2c_mif, gate_mif_hsi2c_mif, "gate_mif_hsi2c_mif", 0, 0, NULL),

	VCLK(mmc0_sclk, sclk_mmc0, "sclk_mmc0", 0, 0, NULL),
	VCLK(mmc1_sclk, sclk_mmc1, "sclk_mmc1", 0, 0, NULL),
	VCLK(mmc2_sclk, sclk_mmc2, "sclk_mmc2", 0, 0, NULL),
	/*
	VCLK(ufsunipro_sclk, sclk_ufsunipro, "sclk_ufsunipro", 0, 0, NULL),
	VCLK(ufsunipro_cfg_sclk, sclk_ufsunipro_cfg, "sclk_ufsunipro_cfg", 0, 0, NULL),
	*/
	VCLK(usb20drd_sclk, sclk_usb20drd, "sclk_usb20drd" , 0, 0, NULL),
	VCLK(uart_sensor_sclk, sclk_uart_sensor, "sclk_uart_sensor", 0, 0, "console-sclk0"),
	VCLK(uart_btwififm_sclk, sclk_uart_btwififm, "sclk_uart_btwififm", 0, 0, NULL),
	VCLK(uart_debug_sclk, sclk_uart_debug, "sclk_uart_debug", 0, 0, "console-sclk2"),
	VCLK(spi_frontfrom_sclk, sclk_spi_frontfrom, "sclk_spi_frontfrom", 0, 0, NULL),
	VCLK(spi_rearfrom_sclk, sclk_spi_rearfrom, "sclk_spi_rearfrom", 0, 0, NULL),
#ifdef CONFIG_SENSORS_FINGERPRINT
	VCLK(spi_ese_sclk, sclk_spi_ese, "sclk_spi_ese", 0, 0, "fp-spi-sclk"),
#else
	VCLK(spi_ese_sclk, sclk_spi_ese, "sclk_spi_ese", 0, 0, NULL),
#endif
	VCLK(spi_voiceprocessor_sclk, sclk_spi_voiceprocessor, "sclk_spi_voiceprocessor", 0, 0, NULL),
	VCLK(spi_sensorhub_sclk, sclk_spi_sensorhub, "sclk_spi_sensorhub", 0, 0, NULL),
	VCLK(isp_sensor0_sclk, sclk_isp_sensor0, "sclk_isp_sensor0", 0, 0, NULL),
	VCLK(isp_sensor1_sclk, sclk_isp_sensor1, "sclk_isp_sensor1", 0, 0, NULL),
	VCLK(isp_sensor2_sclk, sclk_isp_sensor2, "sclk_isp_sensor2", 0, 0, NULL),
};

static struct init_vclk exynos7870_dfs_vclks[] __initdata = {
	/* DFS */
	VCLK(dfs_mif, dvfs_mif, "dvfs_mif", 0, VCLK_DFS, NULL),
	VCLK(dfs_mif_sw, dvfs_mif, "dvfs_mif_sw", 0, VCLK_DFS_SWITCH, NULL),
	VCLK(dfs_int, dvfs_int, "dvfs_int", 0, VCLK_DFS, NULL),
	VCLK(dfs_cam, dvfs_cam, "dvfs_cam", 0, VCLK_DFS, NULL),
	VCLK(dfs_disp, dvfs_disp, "dvfs_disp", 0, VCLK_DFS, NULL),
};

/* register exynos7870 clocks */
void __init exynos7870_clk_init(struct device_node *np)
{
	struct samsung_clk_provider *ctx;
	void __iomem *reg_base;
	int ret;

	if (np) {
		reg_base = of_iomap(np, 0);
		if (!reg_base)
			panic("%s: failed to map registers\n", __func__);
	} else {
		panic("%s: unable to determine soc\n", __func__);
	}

#if defined(CONFIG_ECT)
	ect_parse_binary_header();
#endif

	ret = cal_init();
	if (ret)
		pr_err("%s: unable to initialize power cal\n", __func__);

	ctx = samsung_clk_init(np, reg_base, nr_clks);
	if (!ctx)
		panic("%s: unable to allocate context.\n", __func__);

	samsung_register_of_fixed_ext(ctx, exynos7870_fixed_rate_ext_clks,
			ARRAY_SIZE(exynos7870_fixed_rate_ext_clks), ext_clk_match);

	/* Regist clock local IP */
	samsung_register_vclk(ctx, exynos7870_mfcmscl_vclks, ARRAY_SIZE(exynos7870_mfcmscl_vclks));
	samsung_register_vclk(ctx, exynos7870_g3d_vclks, ARRAY_SIZE(exynos7870_g3d_vclks));
	samsung_register_vclk(ctx, exynos7870_peri_vclks, ARRAY_SIZE(exynos7870_peri_vclks));
	samsung_register_vclk(ctx, exynos7870_fsys_vclks, ARRAY_SIZE(exynos7870_fsys_vclks));
	samsung_register_vclk(ctx, exynos7870_dispaud_vclks, ARRAY_SIZE(exynos7870_dispaud_vclks));
	samsung_register_vclk(ctx, exynos7870_isp_vclks, ARRAY_SIZE(exynos7870_isp_vclks));
	samsung_register_vclk(ctx, exynos7870_mif_vclks, ARRAY_SIZE(exynos7870_mif_vclks));
	samsung_register_vclk(ctx, exynos7870_dfs_vclks, ARRAY_SIZE(exynos7870_dfs_vclks));

	samsung_clk_of_add_provider(np, ctx);

	clk_register_fixed_factor(NULL, "pwm-clock", "gate_peri_sclk_pwm_motor", CLK_SET_RATE_PARENT, 1, 1);

	pr_info("EXYNOS7870: Clock setup completed\n");
}
CLK_OF_DECLARE(exynos7870_clks, "samsung,exynos7870-clock", exynos7870_clk_init);
