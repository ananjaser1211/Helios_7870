/*
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * BUS Monitor Debugging Driver for Samsung EXYNOS8890 SoC
 * By Hosung Kim (hosung0.kim@samsung.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/exynos-busmon.h>

/* S-NODE register */
#define REG_DBG_INT_MASK		(0x0)
#define REG_DBG_INT_CLEAR		(0x4)
#define REG_DBG_INT_SOURCE		(0x8)
#define REG_DBG_CONTROL			(0x10)
#define REG_DBG_TIMEOUT_INTERVAL	(0x14)
#define REG_DBG_READ_TIMEOUT_MO		(0x20)
#define REG_DBG_READ_TIMEOUT_USER	(0x24)
#define REG_DBG_READ_TIMEOUT_ID		(0x28)
#define REG_DBG_WRITE_TIMEOUT_MO	(0x30)
#define REG_DBG_WRITE_TIMEOUT_USER	(0x34)
#define REG_DBG_WRITE_TIMEOUT_ID	(0x38)
#define REG_DBG_ERR_RPT_OFFSET		(0x2000)

/* M-NODE register */
#define REG_ERR_RPT_INT_MASK		(0x0)
#define REG_ERR_RPT_INT_CLEAR		(0x4)
#define REG_ERR_RPT_INT_INFO		(0x8)
#define REG_ERR_RPT_RID			(0x28)
#define REG_ERR_RPT_BID			(0x38)

#define BIT_TIMEOUT_AR			(1 << 16)
#define BIT_TIMEOUT_AW			(1 << 0)
#define BIT_ERR_RPT_AR			(1 << 17)
#define BIT_ERR_RPT_AW			(1 << 1)

#define BUSMON_TYPE_SNODE		(0)
#define BUSMON_TYPE_MNODE		(1)

#define VAL_TIMEOUT_DEFAULT		(0xFFFFF)
#define VAL_TIMEOUT_TEST		(0x1)

#define DISABLED			(0)
#define ENABLED				(1)
#define NEED_TO_CHECK			(0xCAFE)

struct busmon_rpathinfo {
	unsigned int id;
	char *port_name;
	char *dest_name;
	unsigned int bits;
};

struct busmon_masterinfo {
	char *port_name;
	unsigned int user;
	char *master_name;
	unsigned int bits;
};

struct busmon_platdata {
	unsigned int type;
	char *name;
	unsigned int phy_regs;
	void __iomem *regs;
	unsigned int irq;
	unsigned int time_val;
	bool timeout_enabled;
	bool err_rpt_enabled;
	char *need_rpath;
};

static struct busmon_rpathinfo rpathinfo_array[] = {
	{0,	"G3D1",		"MEMS_0",		0x1F},
	{5,	"CAM0",		"MEMS_0",		0x1F},
	{6,	"CAM1",		"MEMS_0",		0x1F},
	{1,	"DISP0_0",	"MEMS_0",		0x1F},
	{2,	"DISP0_1",	"MEMS_0",		0x1F},
	{3,	"DISP1_0",	"MEMS_0",		0x1F},
	{4,	"DISP1_1",	"MEMS_0",		0x1F},
	{7,	"ISP0",		"MEMS_0",		0x1F},
	{12,	"CAM0",		"MEMS_0",		0x1F},
	{13,	"CAM1",		"MEMS_0",		0x1F},
	{8,	"DISP0_0",	"MEMS_0",		0x1F},
	{9,	"DISP0_1",	"MEMS_0",		0x1F},
	{10,	"DISP1_0",	"MEMS_0",		0x1F},
	{11,	"DISP1_1",	"MEMS_0",		0x1F},
	{14,	"ISP0",		"MEMS_0",		0x1F},
	{15,	"IMEM",		"MEMS_0",		0x1F},
	{16,	"AUD",		"MEMS_0",		0x1F},
	{17,	"CORESIGHT",	"MEMS_0",		0x1F},
	{18,	"CAM1",		"MEMS_0",		0x1F},
	{20,	"FSYS1",	"MEMS_0",		0x1F},
	{19,	"ISP0",		"MEMS_0",		0x1F},
	{21,	"CP",		"MEMS_0",		0x1F},
	{22,	"FSYS0",	"MEMS_0",		0x1F},
	{25,	"MFC0",		"MEMS_0",		0x1F},
	{26,	"MFC1",		"MEMS_0",		0x1F},
	{23,	"MSCL0",	"MEMS_0",		0x1F},
	{24,	"MSCL1",	"MEMS_0",		0x1F},
	{0,	"G3D1",		"MEMS_1",		0x1F},
	{5,	"CAM0",		"MEMS_1",		0x1F},
	{6,	"CAM1",		"MEMS_1",		0x1F},
	{1,	"DISP0_0",	"MEMS_1",		0x1F},
	{2,	"DISP0_1",	"MEMS_1",		0x1F},
	{3,	"DISP1_0",	"MEMS_1",		0x1F},
	{4,	"DISP1_1",	"MEMS_1",		0x1F},
	{7,	"ISP0",		"MEMS_1",		0x1F},
	{12,	"CAM0",		"MEMS_1",		0x1F},
	{13,	"CAM1",		"MEMS_1",		0x1F},
	{8,	"DISP0_0",	"MEMS_1",		0x1F},
	{9,	"DISP0_1",	"MEMS_1",		0x1F},
	{10,	"DISP1_0",	"MEMS_1",		0x1F},
	{11,	"DISP1_1",	"MEMS_1",		0x1F},
	{14,	"ISP0",		"MEMS_1",		0x1F},
	{15,	"IMEM",		"MEMS_1",		0x1F},
	{16,	"AUD",		"MEMS_1",		0x1F},
	{17,	"CORESIGHT",	"MEMS_1",		0x1F},
	{18,	"CAM1",		"MEMS_1",		0x1F},
	{20,	"FSYS1",	"MEMS_1",		0x1F},
	{19,	"ISP0",		"MEMS_1",		0x1F},
	{21,	"CP",		"MEMS_1",		0x1F},
	{22,	"FSYS0",	"MEMS_1",		0x1F},
	{25,	"MFC0",		"MEMS_1",		0x1F},
	{26,	"MFC1",		"MEMS_1",		0x1F},
	{23,	"MSCL0",	"MEMS_1",		0x1F},
	{24,	"MSCL1",	"MEMS_1",		0x1F},
	{0,	"IMEM",		"PERI",			0xF},
	{1,	"AUD",		"PERI",			0xF},
	{2,	"CORESIGHT",	"PERI",			0xF},
	{3,	"FSYS0",	"PERI",			0xF},
	{6,	"MFC0",		"PERI",			0xF},
	{7,	"MFC1",		"PERI",			0xF},
	{4,	"MSCL0",	"PERI",			0xF},
	{5,	"MSCL1",	"PERI",			0xF},
	{8,	"CAM1",		"PERI",			0xF},
	{10,	"FSYS1",	"PERI",			0xF},
	{9,	"ISP0",		"PERI",			0xF},
};

static struct busmon_masterinfo masterinfo_array[] = {
	/* DISP0_0 */
	{"DISP0_0", 1 << 0, "sysmmu",	0x1},
	{"DISP0_0", 1 << 0, "S-IDMA0",	0x3},
	{"DISP0_0", 1 << 1, "IDMA3",	0x3},

	/* DISP0_1 */
	{"DISP0_1", 1 << 0, "sysmmu",	0x1},
	{"DISP0_1", 0 << 0, "IDMA0",	0x3},
	{"DISP0_1", 1 << 1, "IDMA4",	0x3},

	/* DISP1_0 */
	{"DISP1_0", 1 << 0, "sysmmu",	0x1},
	{"DISP1_0", 0 << 0, "IDMA1",	0x3},
	{"DISP1_0", 1 << 1, "VGR0",	0x3},

	/* DISP1_1 */
	{"DISP1_1", 1 << 0, "sysmmu",	0x1},
	{"DISP1_1", 0 << 0, "IDMA2",	0x7},
	{"DISP1_1", 1 << 1, "VGR1",	0x7},
	{"DISP1_1", 1 << 2, "WDMA",	0x7},

	/* MFC0 */
	{"MFC0", 1 << 0, "sysmmu",	0x1},
	{"MFC0", 0 << 0, "MFC M0",	0x1},

	/* MFC1 */
	{"MFC1", 1 << 0, "sysmmu",	0x1},
	{"MFC1", 0 << 0, "MFC M1",	0x1},

	/* IMEM */
	{"IMEM", 0 << 0, "SSS M0",	0xF},
	{"IMEM", 1 << 2, "RTIC",	0xF},
	{"IMEM", 1 << 3, "SSS M1",	0xF},
	{"IMEM", 1 << 0, "MCOMP",	0x3},
	{"IMEM", 1 << 1, "APM",		0x3},

	/* G3D */
	{"G3D0", 0 << 0, "G3D0",	0x1},
	{"G3D1", 0 << 1, "G3D1",	0x1},

	/* AUD */
	{"AUD", 1 << 0, "sysmmu",	0x1},
	{"AUD", 1 << 1, "DMAC",		0x7},
	{"AUD", 1 << 2, "AUD CA5",	0x7},

	/* MSCL0 */
	{"MSCL0", 1 << 0, "sysmmu",	0x1},
	{"MSCL0", 0 << 0, "JPEG",	0x3},
	{"MSCL0", 1 << 1, "MSCL0",	0x3},

	/* MSCL1 */
	{"MSCL1", 1 << 0, "sysmmu",	0x1},
	{"MSCL1", 0 << 0, "G2D",	0x3},
	{"MSCL1", 1 << 1, "MSCL1",	0x3},

	/* FSYS1 */
	{"FSYS1", 0 << 0, "MMC51",	0x7},
	{"FSYS1", 1 << 2, "UFS",	0x7},
	{"FSYS1", 1 << 0, "PCIE_WIFI0",	0x3},
	{"FSYS1", 1 << 1, "PCIE_WIFI1",	0x3},

	/* FSYS0 */
	{"FSYS0", 0 << 0, "ETR USB",			0x7},
	{"FSYS0", 1 << 2, "USB30",			0x7},
	{"FSYS0", 1 << 0, "UFS",			0x7},
	{"FSYS0", 1 << 0 | 1 << 2, "MMC51",		0x7},
	{"FSYS0", 1 << 1, "PDMA0",			0x7},
	{"FSYS0", 1 << 1 | 1 << 2, "PDMA(secure)",	0x7},
	{"FSYS0", 1 << 0 | 1 << 1, "USB20",		0x3},

	/* CAM0 */
	{"CAM0", 1 << 0, "sysmmu",			0x1},
	{"CAM0", 0 << 0, "MIPI_CSIS0",			0x7},
	{"CAM0", 1 << 1, "MIPI_CSIS1",			0x7},
	{"CAM0", 1 << 1, "FIMC_3AA0",			0x7},
	{"CAM0", 1 << 2, "FIMC_3AA1",			0x7},

	/* CAM1 */
	{"CAM1", 1 << 2, "sysmmu_IS_B",			0x7},
	{"CAM1", 0 << 0, "MIPI_CSI2 or ISP2",		0xF},
	{"CAM1", 1 << 3, "ISP1",			0xF},
	{"CAM1", 1 << 0 | 1 << 2, "sysmmu_SCL",		0x3},
	{"CAM1", 1 << 0, "MC_SCALER",			0x3},
	{"CAM1", 1 << 0 | 1 << 1 | 1 << 2, "sysmmu_VRA", 0x7},
	{"CAM1", 1 << 0 | 1 << 1, "FIMC_VRA",		0x7},
	{"CAM1", 1 << 1 | 1 << 2, "sysmmu_CA7",		0x7},
	{"CAM1", 1 << 1, "CA7",				0xF},
	{"CAM1", 1 << 1 | 1 << 3, "PDMA_IS",		0xF},

	/* ISP0 */
	{"ISP0", 1 << 0, "sysmmu",			0x1},
	{"ISP0", 0 << 0, "FIMC_ISP",			0x3},
	{"ISP0", 1 << 1, "FIMC_TPU",			0x3},

	/* CP */
	{"CP", 0 << 0, "CR7M",				0x0},
	{"CP", 1 << 3, "TL3MtoL2",			0x8},
	{"CP", 1 << 4, "DMAC",				0x10},
	{"CP", 1 << 2, "MEMtoL2",			0x14},
	{"CP", 1 << 3 | 1 << 4, "CSXAP",		0x18},
	{"CP", 1 << 0 | 1 << 3 | 1 << 4, "LMAC",	0x19},
	{"CP", 1 << 1 | 1 << 3 | 1 << 4, "HMtoL2",	0x1A},
};

static struct busmon_platdata pdata_array[] = {
	/* S-node, BLK_CCORE - Data Path */
	{BUSMON_TYPE_SNODE, "CCORE_MEMS_0_S_NODE",		0x10703000, NULL, 320, VAL_TIMEOUT_DEFAULT, ENABLED, ENABLED, "MEMS_0"},
	{BUSMON_TYPE_SNODE, "CCORE_MEMS_1_S_NODE",		0x10713000, NULL, 321, VAL_TIMEOUT_DEFAULT, ENABLED, ENABLED, "MEMS_1"},
	{BUSMON_TYPE_SNODE, "CCORE_PERI_S_NODE",		0x10723000, NULL, 322, VAL_TIMEOUT_DEFAULT, ENABLED, ENABLED, "PERI"},

	/* S-node, BLK_BUS0 */
	{BUSMON_TYPE_SNODE, "P_BUS0_BUS0_SFR_S_NODE",		0X11E73000, NULL, 352, VAL_TIMEOUT_DEFAULT, ENABLED, ENABLED, NULL},
	{BUSMON_TYPE_SNODE, "P_BUS0_CAM0_S_NODE",		0X11E63000, NULL, 353, VAL_TIMEOUT_DEFAULT, ENABLED, ENABLED, NULL},
	{BUSMON_TYPE_SNODE, "P_BUS0_CAM1_S_NODE",		0X11E53000, NULL, 354, VAL_TIMEOUT_DEFAULT, ENABLED, ENABLED, NULL},
	{BUSMON_TYPE_SNODE, "P_BUS0_DISP0_S_NODE",		0X11E33000, NULL, 355, VAL_TIMEOUT_DEFAULT, ENABLED, ENABLED, NULL},
	{BUSMON_TYPE_SNODE, "P_BUS0_FSYS1_S_NODE",		0X11E13000, NULL, 356, VAL_TIMEOUT_DEFAULT, ENABLED, ENABLED, NULL},
	{BUSMON_TYPE_SNODE, "P_BUS0_PERIC0_S_NODE",		0X11E23000, NULL, 357, VAL_TIMEOUT_DEFAULT, ENABLED, ENABLED, NULL},
	{BUSMON_TYPE_SNODE, "P_BUS0_PERIC1_S_NODE",		0X11EA3000, NULL, 358, VAL_TIMEOUT_DEFAULT, ENABLED, ENABLED, NULL},
	{BUSMON_TYPE_SNODE, "P_BUS0_PERIS_S_NODE",		0X11E03000, NULL, 359, VAL_TIMEOUT_DEFAULT, ENABLED, ENABLED, NULL},
	{BUSMON_TYPE_SNODE, "P_BUS0_TREX_BUS0_S_NODE",		0X11E93000, NULL, 360, VAL_TIMEOUT_DEFAULT, ENABLED, ENABLED, NULL},
	{BUSMON_TYPE_SNODE, "P_BUS0_TREX_BUS0_PERI_S_NODE",	0X11E83000, NULL, 361, VAL_TIMEOUT_DEFAULT, ENABLED, ENABLED, NULL},
	{BUSMON_TYPE_SNODE, "P_BUS0_VPP_S_NODE",		0X11E43000, NULL, 362, VAL_TIMEOUT_DEFAULT, ENABLED, ENABLED, NULL},

	/* S-node, BLK_BUS1 */
	{BUSMON_TYPE_SNODE, "P_BUS1_BUS1_SFR_S_NODE",		0X11C43000, NULL, 373, VAL_TIMEOUT_DEFAULT, ENABLED, ENABLED, NULL},
	{BUSMON_TYPE_SNODE, "P_BUS1_FSYS0_S_NODE",		0X11C03000, NULL, 374, VAL_TIMEOUT_DEFAULT, ENABLED, ENABLED, NULL},
        {BUSMON_TYPE_SNODE, "P_BUS1_MFC_S_NODE",		0X11C23000, NULL, 375, VAL_TIMEOUT_DEFAULT, ENABLED, ENABLED, NULL},
	{BUSMON_TYPE_SNODE, "P_BUS1_MSCL_S_NODE",		0X11C13000, NULL, 376, VAL_TIMEOUT_DEFAULT, ENABLED, ENABLED, NULL},
	{BUSMON_TYPE_SNODE, "P_BUS1_TREX_BUS1_S_NODE",		0X11C63000, NULL, 377, VAL_TIMEOUT_DEFAULT, ENABLED, ENABLED, NULL},
	{BUSMON_TYPE_SNODE, "P_BUS1_TREX_BUS1_PERI_S_NODE",	0X11C53000, NULL, 378, VAL_TIMEOUT_DEFAULT, ENABLED, ENABLED, NULL},

	/* S-node, BLK_CORE */
	{BUSMON_TYPE_SNODE, "P_CORE_APL_S_NODE",		0X10443000, NULL, 326, VAL_TIMEOUT_DEFAULT, ENABLED, ENABLED, NULL},
	{BUSMON_TYPE_SNODE, "P_CORE_AUD_S_NODE",		0X10493000, NULL, 327, VAL_TIMEOUT_DEFAULT, ENABLED, ENABLED, NULL},
	{BUSMON_TYPE_SNODE, "P_CORE_CCORE_SFR_S_NODE",		0X104B3000, NULL, 335, VAL_TIMEOUT_DEFAULT, ENABLED, ENABLED, NULL},
	{BUSMON_TYPE_SNODE, "P_CORE_CORESIGHT_S_NODE",		0X10423000, NULL, 329, VAL_TIMEOUT_DEFAULT, ENABLED, ENABLED, NULL},
	{BUSMON_TYPE_SNODE, "P_CORE_G3D_S_NODE",		0X104A3000, NULL, 330, VAL_TIMEOUT_DEFAULT, ENABLED, ENABLED, NULL},
	{BUSMON_TYPE_SNODE, "P_CORE_MIF0_S_NODE",		0X10453000, NULL, 331, VAL_TIMEOUT_DEFAULT, ENABLED, ENABLED, NULL},
	{BUSMON_TYPE_SNODE, "P_CORE_MIF1_S_NODE",		0X10463000, NULL, 332, VAL_TIMEOUT_DEFAULT, ENABLED, ENABLED, NULL},
	{BUSMON_TYPE_SNODE, "P_CORE_MIF2_S_NODE",		0X10473000, NULL, 333, VAL_TIMEOUT_DEFAULT, ENABLED, ENABLED, NULL},
	{BUSMON_TYPE_SNODE, "P_CORE_MIF3_S_NODE",		0X10483000, NULL, 334, VAL_TIMEOUT_DEFAULT, ENABLED, ENABLED, NULL},
	{BUSMON_TYPE_SNODE, "P_CORE_MNGS_S_NODE",		0X10433000, NULL, 328, VAL_TIMEOUT_DEFAULT, ENABLED, ENABLED, NULL},
	{BUSMON_TYPE_SNODE, "P_CORE_TREX_MIF_S_NODE",		0X104D3000, NULL, 336, VAL_TIMEOUT_DEFAULT, ENABLED, ENABLED, NULL},
	{BUSMON_TYPE_SNODE, "P_CORE_TREX_MIF_PERI_S_NODE",	0X104C3000, NULL, 337, VAL_TIMEOUT_DEFAULT, ENABLED, ENABLED, NULL},

	/* M-node, BLK_BUS0 */
	{BUSMON_TYPE_MNODE, "BUS1_CAM0_M_NODE",			0X11F73000, NULL, 344,  0, DISABLED, ENABLED, NULL},
	{BUSMON_TYPE_MNODE, "BUS1_CAM1_M_NODE",			0X11F13000, NULL, 345,  0, DISABLED, ENABLED, NULL},
	{BUSMON_TYPE_MNODE, "BUS1_DISP0_0_M_NODE",		0x11F33000, NULL, 346,  0, DISABLED, ENABLED, NULL},
	{BUSMON_TYPE_MNODE, "BUS1_DISP0_1_M_NODE",		0X11F43000, NULL, 347,  0, DISABLED, ENABLED, NULL},
	{BUSMON_TYPE_MNODE, "BUS1_DISP1_0_M_NODE",		0x11F53000, NULL, 350,  0, DISABLED, ENABLED, NULL},
	{BUSMON_TYPE_MNODE, "BUS1_DISP1_1_M_NODE",		0x11F63000, NULL, 351,  0, DISABLED, ENABLED, NULL},
	{BUSMON_TYPE_MNODE, "BUS1_FSYS1_M_NODE",		0x11F03000, NULL, 348,  0, DISABLED, ENABLED, NULL},
	{BUSMON_TYPE_MNODE, "BUS1_ISP0_M_NODE",			0x11F23000, NULL, 349,  0, DISABLED, ENABLED, NULL},

	/* M-node, BLK_BUS1 */
	{BUSMON_TYPE_MNODE, "CCORE_AUD_M_NODE",			0x106C3000, NULL, 315,  0, DISABLED, ENABLED, NULL},
	{BUSMON_TYPE_MNODE, "CCORE_CORESIGHT_M_NODE",		0x106D3000, NULL, 316,  0, DISABLED, ENABLED, NULL},
	{BUSMON_TYPE_MNODE, "CCORE_CP_M_NODE",			0x10733000, NULL, 323,  0, DISABLED, ENABLED, NULL},
	{BUSMON_TYPE_MNODE, "CCORE_G3D0_M_NODE",		0x10683000, NULL, 318,  0, DISABLED, ENABLED, NULL},
	{BUSMON_TYPE_MNODE, "CCORE_G3D1_M_NODE",		0x10693000, NULL, 319,  0, DISABLED, ENABLED, NULL},
	{BUSMON_TYPE_MNODE, "CCORE_IMEM_M_NODE",		0x106B3000, NULL, 317,  0, DISABLED, ENABLED, NULL},

	/* M-node, BLK_CCORE */
	{BUSMON_TYPE_MNODE, "P_CORE_BUS_M_NODE",		0x104F3000, NULL, 325,  0, DISABLED, ENABLED, NULL},

#if DISABLED
	/* M-node, BLK_CAM0 */
	{BUSMON_TYPE_MNODE, "TREX_A_AA0_M_NODE",		0x14433000, NULL, 115,  0, DISABLED, ENABLED, NULL},
	{BUSMON_TYPE_MNODE, "TREX_A_AA1_M_NODE",		0x14443000, NULL, 116,  0, DISABLED, ENABLED, NULL},
	{BUSMON_TYPE_MNODE, "TREX_A_BNS_A_M_NODE",		0x14423000, NULL, 117,  0, DISABLED, ENABLED, NULL},
	{BUSMON_TYPE_MNODE, "TREX_A_CSIS_0_M_NODE",		0x14403000, NULL, 118,  0, DISABLED, ENABLED, NULL},
	{BUSMON_TYPE_MNODE, "TREX_A_CSIS_1_M_NODE",		0x14413000, NULL, 119,  0, DISABLED, ENABLED, NULL},

	/* M-node, BLK_CAM1 */
	{BUSMON_TYPE_MNODE, "TREX_B_CSIS_2_M_NODE",		0x14503000, NULL, 165,  0, DISABLED, ENABLED, NULL},
	{BUSMON_TYPE_MNODE, "TREX_B_CSIS_3_M_NODE",		0x14513000, NULL, 166,  0, DISABLED, ENABLED, NULL},
	{BUSMON_TYPE_MNODE, "TREX_B_ISP_1_M_NODE",		0x14523000, NULL, 167,  0, DISABLED, ENABLED, NULL},
	{BUSMON_TYPE_MNODE, "TREX_CAM1_ISP_1_1_M_NODE",		0x145A3000, NULL, 168,  0, DISABLED, ENABLED, NULL},
	{BUSMON_TYPE_MNODE, "TREX_CAM1_MC_SCALER_M_NODE",	0x14593000, NULL, 169,  0, DISABLED, ENABLED, NULL},
	{BUSMON_TYPE_MNODE, "TREX_CAM1_BLOCKB_M_NODE",		0x14583000, NULL, 170,  0, DISABLED, ENABLED, NULL},
	{BUSMON_TYPE_MNODE, "TREX_CAM1_VRA_M_NODE",		0x145B3000, NULL, 171,  0, DISABLED, ENABLED, NULL},

	/* M-node, BLK_ISP0 */
	{BUSMON_TYPE_MNODE, "TREX_C_ISP_0_M_NODE",		0x14603000, NULL, 272,  0, DISABLED, ENABLED, NULL},
	{BUSMON_TYPE_MNODE, "TREX_C_TPU_M_NODE",		0x14613000, NULL, 273,  0, DISABLED, ENABLED, NULL},
#endif
};

struct busmon_dev {
	struct device			*dev;
	struct busmon_platdata		*pdata;
	struct busmon_rpathinfo		*rpathinfo;
	struct busmon_masterinfo	*masterinfo;
	struct of_device_id		*match;
	int				irq;
	int				id;
	void __iomem			*regs;
	spinlock_t			ctrl_lock;
	struct busmon_notifier		notifier_info;
};

struct busmon_panic_block {
	struct notifier_block nb_panic_block;
	struct busmon_dev *pdev;
};

/* declare notifier_list */
static ATOMIC_NOTIFIER_HEAD(busmon_notifier_list);

static const struct of_device_id busmon_dt_match[] = {
	{ .compatible = "samsung,exynos-busmonitor",
	  .data = NULL, },
	{},
};
MODULE_DEVICE_TABLE(of, busmon_dt_match);

static struct busmon_rpathinfo*
	busmon_get_rpathinfo(struct busmon_dev *busmon, unsigned int id, char *dest_name)
{
	struct busmon_rpathinfo *rpath = NULL;
	int i;

	for (i = 0; i < ARRAY_SIZE(rpathinfo_array); i++) {
		if (busmon->rpathinfo[i].id == (id & busmon->rpathinfo[i].bits)) {
			if (dest_name && !strncmp(busmon->rpathinfo[i].dest_name,
					dest_name, strlen(busmon->rpathinfo[i].dest_name))) {
				rpath = &busmon->rpathinfo[i];
				break;
			}
		}
	}
	return rpath;
}

static struct busmon_masterinfo*
	busmon_get_masterinfo(struct busmon_dev *busmon, char *port_name, unsigned int user)
{
	struct busmon_masterinfo *master = NULL;
	unsigned int val;
	int i;

	for (i = 0; i < ARRAY_SIZE(masterinfo_array); i++) {
		if (!strncmp(busmon->masterinfo[i].port_name, port_name, strlen(port_name))) {
			val = user & busmon->masterinfo[i].bits;
			if (val == busmon->masterinfo[i].user) {
				master = &busmon->masterinfo[i];
				break;
			}
		}
	}
	return master;
}


static void busmon_err_rpt_dump(struct busmon_dev *busmon, int num, bool clear)
{
	struct busmon_platdata *pdata = busmon->pdata;
	struct busmon_rpathinfo *rpath = NULL;
	unsigned int id = 0, val = 0, offset = 0;

	if (!pdata[num].err_rpt_enabled)
		return;

	if (pdata[num].type == BUSMON_TYPE_SNODE)
		offset = REG_DBG_ERR_RPT_OFFSET;

	val = __raw_readl(pdata[num].regs + offset + REG_ERR_RPT_INT_INFO);

	if (!val)
		return;

	if (pdata[num].type == BUSMON_TYPE_SNODE && pdata[num].need_rpath) {
		if (val & BIT_ERR_RPT_AR) {
			id = 0xfff & (__raw_readl(pdata[num].regs + offset + REG_ERR_RPT_RID));
			if (clear)
				__raw_writel(BIT_ERR_RPT_AR,
						pdata[num].regs + offset + REG_ERR_RPT_INT_CLEAR);
		} else if (val & BIT_ERR_RPT_AW) {
			id = 0xfff & (__raw_readl(pdata[num].regs + offset + REG_ERR_RPT_BID));
			if (clear)
				__raw_writel(BIT_ERR_RPT_AW,
						pdata[num].regs + offset + REG_ERR_RPT_INT_CLEAR);
		}
		rpath = busmon_get_rpathinfo(busmon, id, pdata[num].need_rpath);
		if (!rpath)
			pr_info("error: can't find rpathinfo with id:%x, dest_name:%s\n",
					id, pdata[num].need_rpath);
	}

	pr_info("\n=================================================================\n"
		"Debugging Information - error response detected in %s\n"
		"Node           : %s [address: 0x%08X]\n"
		"Path           : %s -> %s\n"
		"ID             : %x\n"
		"INT_SOURCE     : %x\n",
		val & BIT_ERR_RPT_AR ? "reading" : "writing",
		pdata[num].name,
		pdata[num].phy_regs,
		rpath ? rpath->port_name : (pdata[num].type ? pdata[num].name : "no information"),
		rpath ? rpath->dest_name : (pdata[num].type ? "no information" : pdata[num].name),
		id,val);
}

static void busmon_timeout_dump(struct busmon_dev *busmon, int num, bool clear)
{
	struct busmon_platdata *pdata = busmon->pdata;
	struct busmon_masterinfo *master = NULL;
	struct busmon_rpathinfo *rpath = NULL;
	unsigned int val = 0, user = 0, id = 0, mo = 0;

	if (!pdata[num].timeout_enabled)
		return;

	val = __raw_readl(pdata[num].regs + REG_DBG_INT_SOURCE);

	if (!val)
		return;

	if (!pdata[num].need_rpath) {
		pr_info("info: it doesn't need rpath, Other M-node may will more "
			"debugging information\n");
	} else {
		if (val & BIT_TIMEOUT_AR) {
			user = 0xf & (__raw_readl(pdata[num].regs + REG_DBG_READ_TIMEOUT_USER));
			id = 0xfff & (__raw_readl(pdata[num].regs + REG_DBG_READ_TIMEOUT_ID));
			mo = 0x3f & (__raw_readl(pdata[num].regs + REG_DBG_READ_TIMEOUT_MO));
			if (clear)
				__raw_writel(BIT_TIMEOUT_AR,
						pdata[num].regs + REG_DBG_INT_CLEAR);
		} else if (val & BIT_TIMEOUT_AW) {
			user = 0xf & (__raw_readl(pdata[num].regs + REG_DBG_WRITE_TIMEOUT_USER));
			id = 0xfff & (__raw_readl(pdata[num].regs + REG_DBG_WRITE_TIMEOUT_ID));
			mo = 0x1f & (__raw_readl(pdata[num].regs + REG_DBG_WRITE_TIMEOUT_MO));
			if (clear)
				__raw_writel(BIT_TIMEOUT_AW,
						pdata[num].regs + REG_DBG_INT_CLEAR);
		}

		/* Find userinfo / master info */
		rpath = busmon_get_rpathinfo(busmon, id, pdata[num].need_rpath);
		if (!rpath) {
			pr_info("error: can't find rpathinfo with user:"
				"%x, id:%x, mo:%x dest_name:%s\n",
				user, id, mo, pdata[num].need_rpath);
		} else {
			master = busmon_get_masterinfo(busmon, rpath->port_name, user);
			if (!master) {
				pr_info("error: can't find masterinfo with port:"
					"%s, user:%x, id:%x, mo:%x\n",
					rpath->port_name, user, id, mo);
			}
		}
	}

	pr_info("\n=================================================================\n"
		"Debugging Information - Timeout occurs in %s\n"
		"Node           : %s [address: 0x%08X]\n"
		"Path           : %s (master: %s) -> %s\n"
		"User           : %x\n"
		"ID             : %x\n"
		"MO             : %x\n"
		"INT_SOURCE     : %x\n",
		val & BIT_TIMEOUT_AR ? "reading" : "writing",
		pdata[num].name,
		pdata[num].phy_regs,
		master ? master->port_name : "no information",
		master ? master->master_name : "no information",
		pdata[num].name,
		user, id, mo, val);
}

static void busmon_dump(struct busmon_dev *busmon, bool clear)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(pdata_array); i++) {
		/* Check Timeout */
		busmon_timeout_dump(busmon, i, clear);

		/* Check Error Report */
		busmon_err_rpt_dump(busmon, i, clear);
	}
}

static irqreturn_t busmon_irq_handler(int irq, void *data)
{
	struct busmon_dev *busmon = (struct busmon_dev *)data;

	/* Check error has been logged */
	dev_info(busmon->dev, "BUS monitor information: %d interrupt occurs.\n", (irq - 32));

	busmon_dump(busmon, true);

	/* Disable to call notifier_call_chain of busmon in Exynos8890 EVT0 */
#if DISABLED
	atomic_notifier_call_chain(&busmon_notifier_list, 0, &busmon->notifier_info);
#endif
	pr_info("\n=================================================================\n");
	panic("Error detected by BUS monitor.");

	return IRQ_HANDLED;
}

void busmon_notifier_chain_register(struct notifier_block *block)
{
	atomic_notifier_chain_register(&busmon_notifier_list, block);
}

static int busmon_logging_panic_handler(struct notifier_block *nb,
				   unsigned long l, void *buf)
{
	struct busmon_panic_block *busmon_panic = (struct busmon_panic_block *)nb;
	struct busmon_dev *busmon = busmon_panic->pdev;

	if (!IS_ERR_OR_NULL(busmon)) {
		/* Check error has been logged */
		busmon_dump(busmon, false);
	}
	return 0;
}

static void busmon_init(struct busmon_dev *busmon)
{
	struct busmon_platdata *pdata = busmon->pdata;
	int i;

	for (i = 0; i < ARRAY_SIZE(pdata_array); i++) {
		if (pdata[i].type == BUSMON_TYPE_SNODE && pdata[i].timeout_enabled) {
			/* first of all, error clear at occurs previous */
			__raw_writel(BIT_TIMEOUT_AR | BIT_TIMEOUT_AW,
						pdata[i].regs + REG_DBG_INT_CLEAR);
			/* set timeout interval value */
			__raw_writel(pdata[i].time_val, pdata[i].regs + REG_DBG_TIMEOUT_INTERVAL);
			/* unmask timeout function */
			__raw_writel(BIT_TIMEOUT_AR | BIT_TIMEOUT_AW, pdata[i].regs + REG_DBG_INT_MASK);
			/* enable timeout function */
			__raw_writel(ENABLED, pdata[i].regs + REG_DBG_CONTROL);
			pr_debug("Exynos BUS Monitor irq:%u - %s timeout enabled\n",
						pdata[i].irq - 32, pdata[i].name);
		}
		if (pdata[i].err_rpt_enabled) {
			/* enable err_rpt of s-node */
			if (pdata[i].type == BUSMON_TYPE_SNODE) {
				/* first of all, error clear at occurs previous */
				__raw_writel(BIT_ERR_RPT_AR | BIT_ERR_RPT_AW,
						pdata[i].regs + REG_DBG_ERR_RPT_OFFSET +
						REG_ERR_RPT_INT_CLEAR);
				/* unmask timeout function */
				__raw_writel(BIT_ERR_RPT_AR | BIT_ERR_RPT_AW, pdata[i].regs +
						REG_DBG_ERR_RPT_OFFSET + REG_ERR_RPT_INT_MASK);
			} else {
				/* first of all, error clear at occurs previous */
				__raw_writel(BIT_ERR_RPT_AR | BIT_ERR_RPT_AW,
						pdata[i].regs + REG_ERR_RPT_INT_CLEAR);
				/* unmask timeout function */
				__raw_writel(BIT_ERR_RPT_AR | BIT_ERR_RPT_AW, pdata[i].regs +
						REG_ERR_RPT_INT_MASK);
			}
			pr_debug("Exynos BUS Monitor irq:%u - %s error reporting enabled\n",
						pdata[i].irq - 32, pdata[i].name);
		}
	}
}

static int busmon_probe(struct platform_device *pdev)
{
	struct busmon_dev *busmon;
	struct busmon_panic_block *busmon_panic = NULL;
	int ret, i;
	u32 size;

	busmon = devm_kzalloc(&pdev->dev, sizeof(struct busmon_dev), GFP_KERNEL);
	if (!busmon) {
		dev_err(&pdev->dev, "failed to allocate memory for driver's "
				"private data\n");
		return -ENOMEM;
	}
	busmon->dev = &pdev->dev;

	spin_lock_init(&busmon->ctrl_lock);

	busmon->pdata = pdata_array;
	busmon->masterinfo = masterinfo_array;
	busmon->rpathinfo = rpathinfo_array;

	for (i = 0; i < ARRAY_SIZE(pdata_array); i++) {
		if (busmon->pdata[i].type == BUSMON_TYPE_SNODE)
			size = SZ_16K;
		else
			size = SZ_256;

		busmon->pdata[i].regs =	devm_ioremap_nocache(&pdev->dev,
						busmon->pdata[i].phy_regs, size);
		if (busmon->pdata[i].regs == NULL) {
			dev_err(&pdev->dev, "failed to claim register region\n");
			return -ENOENT;
		}

		ret = devm_request_irq(&pdev->dev, busmon->pdata[i].irq + 32,
					busmon_irq_handler, IRQF_GIC_MULTI_TARGET,
					busmon->pdata[i].name, busmon);
		if (ret) {
			dev_err(&pdev->dev, "irq request failed\n");
			return -ENXIO;
		}
	}

	busmon_panic = devm_kzalloc(&pdev->dev,
			sizeof(struct busmon_panic_block), GFP_KERNEL);
	if (!busmon_panic) {
		dev_err(&pdev->dev, "failed to allocate memory for driver's "
				"panic handler data\n");
	} else {
		busmon_panic->nb_panic_block.notifier_call =
					busmon_logging_panic_handler;
		busmon_panic->pdev = busmon;
		atomic_notifier_chain_register(&panic_notifier_list,
					&busmon_panic->nb_panic_block);
	}

	platform_set_drvdata(pdev, busmon);

	busmon_init(busmon);
	dev_info(&pdev->dev, "success to probe bus monitor driver\n");

	return 0;
}

static int busmon_remove(struct platform_device *pdev)
{
	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int busmon_suspend(struct device *dev)
{
	return 0;
}

static int busmon_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct busmon_dev *busmon = platform_get_drvdata(pdev);

	busmon_init(busmon);

	return 0;
}

static SIMPLE_DEV_PM_OPS(busmon_pm_ops,
			 busmon_suspend,
			 busmon_resume);
#define BUSMON_PM	(busmon_pm_ops)
#else
#define BUSMON_PM	NULL
#endif

static struct platform_driver exynos_busmon_driver = {
	.probe		= busmon_probe,
	.remove		= busmon_remove,
	.driver		= {
		.name		= "exynos-busmon",
		.of_match_table	= busmon_dt_match,
		.pm		= &busmon_pm_ops,
	},
};

module_platform_driver(exynos_busmon_driver);

MODULE_DESCRIPTION("Samsung Exynos8890 EVT0 BUS MONITOR DRIVER");
MODULE_AUTHOR("Hosung Kim <hosung0.kim@samsung.com");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:exynos-busmon");
