/* arch/arm/mach-exynos/cal_bts.c
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

#include "cal_bts7870.h"

#define QOS_AUD		0x4
#define QOS_JPEG	0x4
#define QOS_MSCL	0x4

void bts_setotf_sysreg(BWL_SYSREG_RT_NRT_SEL path_sel, addr_u32 base, bool enable)
{
	unsigned int tmp_reg;

	tmp_reg = Inp32(base + ISP_USER_CON);

	if (enable)
		Outp32(base + ISP_USER_CON, tmp_reg | (0x1 << path_sel));
	else
		Outp32(base + ISP_USER_CON, tmp_reg & ~(0x1 << path_sel));
}

void bts_setmo_sysreg(BWL_MO_SYSREG_IP mo_id, addr_u32 base, unsigned int ar,
			unsigned int aw)
{
	return;
}

void bts_setqos_sysreg(BWL_QOS_SYSREG_IP qos_id, addr_u32 base, unsigned int *priority)
{
	unsigned int tmp_reg;

	switch (qos_id) {
	case BTS_SYSREG_DISPAUD:
		tmp_reg = Inp32(base + DISPAUD_QOS_CON);
		tmp_reg &= ~((0xf << 20) | (0xf << 16) | (0xf << 8) | (0xf << 4) | 0xf);
		Outp32(base + DISPAUD_QOS_CON, tmp_reg | (QOS_AUD << 20) |
			(QOS_AUD << 16) | (priority[0] << 8) | (priority[0] << 4) |
			(priority[0]));
		break;
	case BTS_SYSREG_ISP0:
		tmp_reg = Inp32(base + ISP_QOS_CON0);
		tmp_reg &= ~((0xf << 8) | (0xf << 4) | 0xf);
		Outp32(base + ISP_QOS_CON0, tmp_reg | (priority[0] << 8) | (priority[0] << 4) | (priority[0]));
		break;
	case BTS_SYSREG_ISP1:
		Outp32(base + ISP_QOS_CON1, (priority[0] << 28) | (priority[0] << 24) |
			(priority[0] << 20) | (priority[0] << 16) | (priority[0] << 12) |
			(priority[0] << 8) | (priority[0] << 4) | (priority[0]));
		break;
	case BTS_SYSREG_MIF_MODAPIF_CP:
		/* CP Qos select : 0x0(SYSREG QOS), 0x1(CP QOS) */
		Outp32(base + PMUALIVE_MODAPIF_CP_QOS_CON, (priority[0] << 16)|(priority[0] << 8) | CP_QOS_OVERRIDE);
		break;
	case BTS_SYSREG_MIF_MODAPIF_GNSS:
		/* GNSS Qos select : 0x0(SYSREG QOS), 0x1(GNSS QOS) */
		Outp32(base + PMUALIVE_MODAPIF_GNSS_QOS_CON, (priority[0] << 16)|(priority[0] << 8) | GNSS_QOS_OVERRIDE);
		break;
	case BTS_SYSREG_MFCMSCL:
		Outp32(base + MFCMSCL_QOS_CON, (priority[0] << 28) | (priority[0] << 24) |
			(QOS_JPEG << 20) | (QOS_JPEG << 16) | (QOS_MSCL << 12) |
			(QOS_MSCL << 8) | (QOS_MSCL << 4) | QOS_MSCL);
		break;
	case BTS_SYSREG_MIF_CPU:
		Outp32(base + MIF_CPU_QOS_CON, (priority[0] << 4) | (priority[0]));
		break;
	case BTS_SYSREG_MIF_APL:
		Outp32(base + MIF_APL_QOS_CON, (priority[0] << 4) | (priority[0]));
		break;
	default:
		break;
	}
}

void bts_setqos(addr_u32 base, unsigned int priority)  /* QOS :  [RRRRWWWW] */
{
	Outp32(base + BTS_RCON, 0x0);
	Outp32(base + BTS_RCON, 0x0);
	Outp32(base + BTS_WCON, 0x0);
	Outp32(base + BTS_WCON, 0x0);

	Outp32(base + BTS_PRIORITY, ((priority >> 16) & 0xFFFF));
	Outp32(base + BTS_TOKENMAX, 0xFFDF);
	Outp32(base + BTS_BWUPBOUND, 0x18);
	Outp32(base + BTS_BWLOBOUND, 0x1);
	Outp32(base + BTS_INITTKN, 0x8);

	Outp32(base + BTS_PRIORITY + WOFFSET, (priority & 0xFFFF));
	Outp32(base + BTS_TOKENMAX + WOFFSET, 0xFFDF);
	Outp32(base + BTS_BWUPBOUND + WOFFSET, 0x18);
	Outp32(base + BTS_BWLOBOUND + WOFFSET, 0x1);
	Outp32(base + BTS_INITTKN + WOFFSET, 0x8);

	Outp32(base + BTS_RCON, 0x1);
	Outp32(base + BTS_WCON, 0x1);
}

void bts_setqos_bw(addr_u32 base, unsigned int priority,
			unsigned int window, unsigned int token) /* QOS :  [RRRRWWWW] */
{
	Outp32(base + BTS_RCON, 0x0);
	Outp32(base + BTS_RCON, 0x0);
	Outp32(base + BTS_WCON, 0x0);
	Outp32(base + BTS_WCON, 0x0);

	Outp32(base + BTS_PRIORITY, ((priority >> 16) & 0xFFFF));
	Outp32(base + BTS_TOKENMAX, 0xFFDF);
	Outp32(base + BTS_BWUPBOUND, 0x18);
	Outp32(base + BTS_BWLOBOUND, 0x1);
	Outp32(base + BTS_INITTKN, 0x8);
	Outp32(base + BTS_DEMWIN, window);
	Outp32(base + BTS_DEMTKN, token);
	Outp32(base + BTS_DEFWIN, window);
	Outp32(base + BTS_DEFTKN, token);
	Outp32(base + BTS_PRMWIN, window);
	Outp32(base + BTS_PRMTKN, token);
	Outp32(base + BTS_FLEXIBLE, 0x0);

	Outp32(base + BTS_PRIORITY + WOFFSET, (priority & 0xFFFF));
	Outp32(base + BTS_TOKENMAX + WOFFSET, 0xFFDF);
	Outp32(base + BTS_BWUPBOUND + WOFFSET, 0x18);
	Outp32(base + BTS_BWLOBOUND + WOFFSET, 0x1);
	Outp32(base + BTS_INITTKN + WOFFSET, 0x8);
	Outp32(base + BTS_DEMWIN + WOFFSET, window);
	Outp32(base + BTS_DEMTKN + WOFFSET, token);
	Outp32(base + BTS_DEFWIN + WOFFSET, window);
	Outp32(base + BTS_DEFTKN + WOFFSET, token);
	Outp32(base + BTS_PRMWIN + WOFFSET, window);
	Outp32(base + BTS_PRMTKN + WOFFSET, token);
	Outp32(base + BTS_FLEXIBLE + WOFFSET, 0x0);

	Outp32(base + BTS_RMODE, 0x1);
	Outp32(base + BTS_WMODE, 0x1);
	Outp32(base + BTS_RCON, 0x3);
	Outp32(base + BTS_WCON, 0x3);
}

void bts_setqos_mo(addr_u32 base, unsigned int priority, unsigned int mo)  /* QOS :  [RRRRWWWW] */
{
	Outp32(base + BTS_RCON, 0x0);
	Outp32(base + BTS_RCON, 0x0);
	Outp32(base + BTS_WCON, 0x0);
	Outp32(base + BTS_WCON, 0x0);

	Outp32(base + BTS_PRIORITY, ((priority >> 16) & 0xFFFF));
	Outp32(base + BTS_MOUPBOUND, 0x7F - mo);
	Outp32(base + BTS_MOLOBOUND, mo);
	Outp32(base + BTS_FLEXIBLE, 0x0);

	Outp32(base + BTS_PRIORITY + WOFFSET, (priority & 0xFFFF));
	Outp32(base + BTS_MOUPBOUND + WOFFSET, 0x7F - mo);
	Outp32(base + BTS_MOLOBOUND + WOFFSET, mo);
	Outp32(base + BTS_FLEXIBLE + WOFFSET, 0x0);

	Outp32(base + BTS_RMODE, 0x2);
	Outp32(base + BTS_WMODE, 0x2);
	Outp32(base + BTS_RCON, 0x3);
	Outp32(base + BTS_WCON, 0x3);
}

void bts_disable(addr_u32 base)
{
	/* reset to default */
	Outp32(base + BTS_RCON, 0x0);
	Outp32(base + BTS_RCON, 0x0);
	Outp32(base + BTS_WCON, 0x0);
	Outp32(base + BTS_WCON, 0x0);

	Outp32(base + BTS_RMODE, 0x1);
	Outp32(base + BTS_WMODE, 0x1);

	Outp32(base + BTS_PRIORITY, 0xA942);
	Outp32(base + BTS_TOKENMAX, 0x0);
	Outp32(base + BTS_BWUPBOUND, 0x3FFF);
	Outp32(base + BTS_BWLOBOUND, 0x3FFF);
	Outp32(base + BTS_INITTKN, 0x7FFF);
	Outp32(base + BTS_DEMWIN, 0x7FFF);
	Outp32(base + BTS_DEMTKN, 0x1FFF);
	Outp32(base + BTS_DEFWIN, 0x7FFF);
	Outp32(base + BTS_DEFTKN, 0x1FFF);
	Outp32(base + BTS_PRMWIN, 0x7FFF);
	Outp32(base + BTS_PRMTKN, 0x1FFF);
	Outp32(base + BTS_MOUPBOUND, 0x1F);
	Outp32(base + BTS_MOLOBOUND, 0x1F);
	Outp32(base + BTS_FLEXIBLE, 0x0);

	Outp32(base + BTS_PRIORITY + WOFFSET, 0xA942);
	Outp32(base + BTS_TOKENMAX + WOFFSET, 0x0);
	Outp32(base + BTS_BWUPBOUND + WOFFSET, 0x3FFF);
	Outp32(base + BTS_BWLOBOUND + WOFFSET, 0x3FFF);
	Outp32(base + BTS_INITTKN + WOFFSET, 0x7FFF);
	Outp32(base + BTS_DEMWIN + WOFFSET, 0x7FFF);
	Outp32(base + BTS_DEMTKN + WOFFSET, 0x1FFF);
	Outp32(base + BTS_DEFWIN + WOFFSET, 0x7FFF);
	Outp32(base + BTS_DEFTKN + WOFFSET, 0x1FFF);
	Outp32(base + BTS_PRMWIN + WOFFSET, 0x7FFF);
	Outp32(base + BTS_PRMTKN + WOFFSET, 0x1FFF);
	Outp32(base + BTS_MOUPBOUND + WOFFSET, 0x1F);
	Outp32(base + BTS_MOLOBOUND + WOFFSET, 0x1F);
	Outp32(base + BTS_FLEXIBLE + WOFFSET, 0x0);
}
