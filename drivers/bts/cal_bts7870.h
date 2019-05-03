/* arch/arm/mach-exynos/cal_bts.h
 *
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 *              http://www.samsung.com/
 *
 * EXYNOS - BTS CAL code.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __BTSCAL_H__
#define __BTSCAL_H__

#if defined(CONFIG_EXYNOS7870_BTS)

#include <linux/io.h>

#define Outp32(addr, data)      (__raw_writel(data, addr))
#define Inp32(addr)             (__raw_readl(addr))
typedef void __iomem *addr_u32;

#else

typedef unsigned long u32;
typedef unsigned long addr_u32;
#define Outp32(addr, data) (*(unsigned int *)(addr) = (data))
#define Inp32(addr) (*(unsigned int *)(addr))

#endif

/* for ISP RT/NRT Path Setting Through SYSTEM Register */
typedef enum {
	BTS_SYSREG_ISP_VRA_SEL = 0x0,
	BTS_SYSREG_ISP_ISP_SCL_SEL
} BWL_SYSREG_RT_NRT_SEL;

/* for BTS Through SYSTEM Register */
typedef enum {
	BTS_SYSREG_DISPAUD,		/* AUD,DECON_INT2,DECON_INT1, DECON_INT0 */
	BTS_SYSREG_FSYS0,		/* FSYS_PDMA1, PDMA0, FSYS_USB20DRD */
	BTS_SYSREG_FSYS1,		/* FSYS_MMC2, FSYS_MMC1, FSYS_MMC0 */
	BTS_SYSREG_FSYS2,		/* UFS, RTIC, SSS_MDMA, SSS */
	BTS_SYSREG_ISP0,		/* SCALER, VRA */
	BTS_SYSREG_ISP1,		/* CSIS0, CSIS1, BNS, TAA, ISP */
	BTS_SYSREG_MIF_MODAPIF_CP,
	BTS_SYSREG_MIF_MODAPIF_GNSS,
	BTS_SYSREG_MIF_CPU,
	BTS_SYSREG_MIF_APL,
	BTS_SYSREG_MFCMSCL,
	BTS_SYSREG_TOTAL_NUM
} BWL_QOS_SYSREG_IP;

typedef enum {
	BTS_DISP_M0,
	BTS_DISP_S2,
	BTS_DISP_S1,
	BTS_DISP_S0,
	BTS_ISP1_M0,
	BTS_ISP1_S2,
	BTS_ISP1_S1,
	BTS_ISP1_S0,
	BTS_MSCL_M0,
	BTS_JPEG_S0,
	BTS_MSCALER0_S1,
	BTS_MSCALER1_S2,
	BTS_MO_SYSREG_TOTAL_NUM
} BWL_MO_SYSREG_IP;

/* SYSREG offsets */
#define DISPAUD_QOS_CON				0x600
#define DISP_QOS_SEL				0x604
#define DISP_XIU_DISP1_AR_AC_TARGET_CON		0x1014

#define ISP_QOS_CON0				0x500
#define ISP_QOS_CON1				0x600
#define ISP_USER_CON				0x1000
#define ISP_XIU_ISP1_AR_AC_TARGET_CON		0x1034
#define ISP_XIU_ISP1_AW_AC_TARGET_CON		0x1038

#define MFCMSCL_QOS_CON				0x600
#define MFCMSCL_XIU_MSCL0DX_M0			0x1000
#define MFCMSCL_XIU_MSCL0DX_S0			0x1004
#define MFCMSCL_XIU_MSCL0DX_S1			0x1008
#define MFCMSCL_XIU_MSCL0DX_S2			0x100C

#define AUD_QOS_CON				0x600

#define FSYS_QOS_CON0				0x600
#define FSYS_QOS_CON1				0x604
#define FSYS_QOS_CON2				0x608

#define PMUALIVE_MODAPIF_CP_QOS_CON		0x078 /* csj */
#define PMUALIVE_MODAPIF_GNSS_QOS_CON		0x0B8 /* csj */
#define MIF_CPU_QOS_CON				0x608 /* csj */
#define MIF_APL_QOS_CON				0x60C /* csj */

#define CP_QOS_OVERRIDE				0x1
#define GNSS_QOS_OVERRIDE			0x1

/* for BTS V2.1 Register */
#define BTS_RCON			0x100
#define BTS_RMODE			0x104
#define BTS_WCON			0x180
#define BTS_WMODE			0x184
#define BTS_PRIORITY			0x200
#define BTS_TOKENMAX			0x204
#define BTS_BWUPBOUND			0x20C
#define BTS_BWLOBOUND			0x210
#define BTS_INITTKN			0x214
#define BTS_RSTCLK			0x218
#define BTS_RSTTKN			0x21C
#define BTS_DEMWIN			0x220
#define BTS_DEMTKN			0x224
#define BTS_DEFWIN			0x228
#define BTS_DEFTKN			0x22C
#define BTS_PRMWIN			0x230
#define BTS_PRMTKN			0x234
#define BTS_MOUPBOUND			0x240
#define BTS_MOLOBOUND			0x244
#define BTS_FLEXIBLE			0x280
#define BTS_POLARITY			0x284
#define BTS_FBMGRP0ADRS			0x290
#define BTS_FBMGRP0ADRE			0x294
#define BTS_FBMGRP1ADRS			0x298
#define BTS_FBMGRP1ADRE			0x29C
#define BTS_FBMGRP2ADRS			0x2A0
#define BTS_FBMGRP2ADRE			0x2A4
#define BTS_FBMGRP3ADRS			0x2A8
#define BTS_FBMGRP3ADRE			0x2AC
#define BTS_FBMGRP4ADRS			0x2B0
#define BTS_FBMGRP4ADRE			0x2B4
#define BTS_EMERGENTID			0x2C0
#define BTS_RISINGTH			0x2C4
#define BTS_FALLINGTH			0x2C8
#define BTS_FALLINGMO			0x2CC
#define BTS_MOCOUNTER			0x2F0
#define BTS_STATUS			0x2F4
#define BTS_BWMONLOW			0x2F8
#define BTS_BWMONUP			0x2FC

#define WOFFSET				0x100

void bts_setotf_sysreg(BWL_SYSREG_RT_NRT_SEL path_sel, addr_u32 base,
			bool enable);
void bts_setmo_sysreg(BWL_MO_SYSREG_IP mo_id, addr_u32 base,
			unsigned int ar, unsigned int aw);
void bts_setqos_sysreg(BWL_QOS_SYSREG_IP qos_id, addr_u32 base,
			unsigned int *priority);
void bts_setqos(addr_u32 base, unsigned int priority);
void bts_setqos_bw(addr_u32 base, unsigned int priority,
			unsigned int window, unsigned int token);
void bts_setqos_mo(addr_u32 base, unsigned int priority, unsigned int mo);
void bts_disable(addr_u32 base);

#endif
