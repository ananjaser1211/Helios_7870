/*
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/list.h>
#include <linux/dma-mapping.h>
#include <linux/pm_runtime.h>
#include <linux/pm_qos.h>
#include <linux/suspend.h>
#include <linux/debugfs.h>
#include <linux/clk-provider.h>

#include <soc/samsung/bts.h>
#include "cal_bts7870.h"
#include "regs-bts.h"

#define MAX_MIF_IDX		7

#define FHD_BW			(1920 * 1080 * 4 * 60)

#ifdef BTS_DBGGEN
#define BTS_DBG(x...)		pr_err(x)
#else
#define BTS_DBG(x...)		do {} while (0)
#endif

#define BTS_SYSREG_DOMAIN	(BTS_DISPAUD | \
					BTS_ISP0 | \
					BTS_ISP1 | \
					BTS_MFCMSCL | \
					BTS_JPEG | \
					BTS_MSCL0 | \
					BTS_MSCL1 | \
					BTS_AUD | \
					BTS_FSYS0 | \
					BTS_FSYS1 | \
					BTS_FSYS2 | \
					BTS_MODAPIF_CP | \
					BTS_MODAPIF_GNSS | \
					BTS_CPU | \
					BTS_APL)

enum bts_index {
	BTS_IDX_DISPAUD,
	BTS_IDX_ISP0,
	BTS_IDX_ISP1,
	BTS_IDX_MFCMSCL,
	BTS_IDX_JPEG,
	BTS_IDX_MSCL0,
	BTS_IDX_MSCL1,
	BTS_IDX_AUD,
	BTS_IDX_FSYS0,
	BTS_IDX_FSYS1,
	BTS_IDX_FSYS2,
	BTS_IDX_MODAPIF_CP,
	BTS_IDX_MODAPIF_GNSS,
	BTS_IDX_CPU,
	BTS_IDX_APL,
	BTS_IDX_G3D,
	BTS_MAX,
};

enum bts_id {
	BTS_DISPAUD = (1 << BTS_IDX_DISPAUD),
	BTS_ISP0 = (1 << BTS_IDX_ISP0),
	BTS_ISP1 = (1 << BTS_IDX_ISP1),
	BTS_MFCMSCL = (1 << BTS_IDX_MFCMSCL),
	BTS_JPEG = (1 << BTS_IDX_JPEG),
	BTS_MSCL0 = (1 << BTS_IDX_MSCL0),
	BTS_MSCL1 = (1 << BTS_IDX_MSCL1),
	BTS_AUD = (1 << BTS_IDX_AUD),
	BTS_FSYS0 = (1 << BTS_IDX_FSYS0),
	BTS_FSYS1 = (1 << BTS_IDX_FSYS1),
	BTS_FSYS2 = (1 << BTS_IDX_FSYS2),
	BTS_MODAPIF_CP = (1 << BTS_IDX_MODAPIF_CP),
	BTS_MODAPIF_GNSS = (1 << BTS_IDX_MODAPIF_GNSS),
	BTS_CPU = (1 << BTS_IDX_CPU),
	BTS_APL = (1 << BTS_IDX_APL),
	BTS_G3D = (1 << BTS_IDX_G3D),
};

enum exynos_bts_scenario {
	BS_DISABLE,
	BS_DEFAULT,
	BS_CAM_BNS,
	BS_DEBUG,
	BS_MAX,
};

enum exynos_bts_function {
	BF_SETQOS,
	BF_SETQOS_BW,
	BF_SETQOS_MO,
	BF_DISABLE,
	BF_NOP,
};

struct bts_table {
	enum exynos_bts_function fn;
	unsigned int priority[2];
	unsigned int window;
	unsigned int token;
	unsigned int mo;
	struct bts_info *next_bts;
	int prev_scen;
	int next_scen;
};

struct bts_info {
	enum bts_id id;
	const char *name;
	unsigned int pa_base;
	void __iomem *va_base;
	struct bts_table table[BS_MAX];
	const char *pd_name;
	bool on;
	struct list_head list;
	bool enable;
	struct clk_info *ct_ptr;
	enum exynos_bts_scenario cur_scen;
	enum exynos_bts_scenario top_scen;
};

struct bts_scenario {
	const char *name;
	unsigned int ip;
	enum exynos_bts_scenario id;
	struct bts_info *head;
};

struct clk_info {
	const char *clk_name;
	struct clk *clk;
	enum bts_index index;
};

static struct pm_qos_request exynos7_mif_bts_qos;
static struct pm_qos_request exynos7_int_bts_qos;
static struct srcu_notifier_head exynos_media_notifier;
static struct clk_info clk_table[] = {
	{"gate_g3d_bts_alias", NULL, BTS_IDX_G3D},
};

static DEFINE_MUTEX(media_mutex);
static unsigned int decon_bw, cam_bw, total_bw;
static void __iomem *base_drex;
static void __iomem *base_bts_pmu_alive;
static unsigned int mif_freq;
static unsigned int num_active_disp_win;

static struct bts_info exynos7_bts[] = {
	[BTS_IDX_DISPAUD] = {
		.id = BTS_DISPAUD,
		.name = "disp",
		.pa_base = EXYNOS7870_PA_SYSREG_DISPAUD,
		.pd_name = "pd-dispaud",
		.table[BS_DEFAULT].fn = BF_SETQOS,
		.table[BS_DEFAULT].priority[0] = 0xA,
		.table[BS_DISABLE].fn = BF_SETQOS,
		.table[BS_DISABLE].priority[0] = 0x4,
		.cur_scen = BS_DISABLE,
		.on = false,
		.enable = true,
	},
	[BTS_IDX_ISP0] = {
		.id = BTS_ISP0,
		.name = "isp0",
		.pa_base = EXYNOS7870_PA_SYSREG_ISP,
		.pd_name = "pd-cam0",
		.table[BS_DEFAULT].fn = BF_SETQOS,
		.table[BS_DEFAULT].priority[0] = 0xC,
		.table[BS_DEFAULT].priority[1] = 0xC,
		.table[BS_CAM_BNS].fn = BF_SETQOS,
		.table[BS_CAM_BNS].priority[0] = 0xC,
		.table[BS_CAM_BNS].priority[1] = 0xC,
		.table[BS_DISABLE].fn = BF_SETQOS,
		.table[BS_DISABLE].priority[0] = 0x4,
		.table[BS_DISABLE].priority[1] = 0x4,
		.cur_scen = BS_DISABLE,
		.on = false,
		.enable = true,
	},
	[BTS_IDX_ISP1] = {
		.id = BTS_ISP1,
		.name = "isp1",
		.pa_base = EXYNOS7870_PA_SYSREG_ISP,
		.pd_name = "pd-cam0",
		.table[BS_DEFAULT].fn = BF_SETQOS,
		.table[BS_DEFAULT].priority[0] = 0xC,
		.table[BS_DEFAULT].priority[1] = 0xC,
		.table[BS_CAM_BNS].fn = BF_SETQOS,
		.table[BS_CAM_BNS].priority[0] = 0xC,
		.table[BS_CAM_BNS].priority[1] = 0xC,
		.table[BS_DISABLE].fn = BF_SETQOS,
		.table[BS_DISABLE].priority[0] = 0x4,
		.table[BS_DISABLE].priority[1] = 0x4,
		.cur_scen = BS_DISABLE,
		.on = false,
		.enable = true,
	},
	[BTS_IDX_MFCMSCL] = {
		.id = BTS_MFCMSCL,
		.name = "mfc",
		.pa_base = EXYNOS7870_PA_SYSREG_MFCMSCL,
		.pd_name = "pd-mfcmscl",
		.table[BS_DEFAULT].fn = BF_SETQOS,
		.table[BS_DEFAULT].priority[0] = 0x8,
		.table[BS_DISABLE].fn = BF_SETQOS,
		.table[BS_DISABLE].priority[0] = 0x4,
		.cur_scen = BS_DISABLE,
		.on = false,
		.enable = true,
	},
	[BTS_IDX_FSYS0] = {
		.id = BTS_FSYS0,
		.name = "fsys0",
		.pa_base = EXYNOS7870_PA_SYSREG_FSYS,
		.pd_name = "pd-fsys",
		.table[BS_DEFAULT].fn = BF_NOP,
		.table[BS_DISABLE].fn = BF_SETQOS,
		.table[BS_DISABLE].priority[0] = 0x4,
		.cur_scen = BS_DISABLE,
		.on = false,
		.enable = true,
	},
	[BTS_IDX_FSYS1] = {
		.id = BTS_FSYS1,
		.name = "fsys1",
		.pa_base = EXYNOS7870_PA_SYSREG_FSYS,
		.pd_name = "pd-fsys",
		.table[BS_DEFAULT].fn = BF_NOP,
		.table[BS_DISABLE].fn = BF_SETQOS,
		.table[BS_DISABLE].priority[0] = 0x4,
		.cur_scen = BS_DISABLE,
		.on = false,
		.enable = true,
	},
	[BTS_IDX_FSYS2] = {
		.id = BTS_FSYS2,
		.name = "fsys2",
		.pa_base = EXYNOS7870_PA_SYSREG_FSYS,
		.pd_name = "pd-fsys",
		.table[BS_DEFAULT].fn = BF_NOP,
		.table[BS_DISABLE].fn = BF_SETQOS,
		.table[BS_DISABLE].priority[0] = 0x4,
		.cur_scen = BS_DISABLE,
		.on = false,
		.enable = true,
	},
	[BTS_IDX_MODAPIF_CP] = {
		.id = BTS_MODAPIF_CP,
		.name = "modapif_cp",
		.pa_base = EXYNOS7870_PA_PMU_ALIVE,
		.pd_name = "pd-modapif",
		.table[BS_DEFAULT].fn = BF_SETQOS,
		.table[BS_DEFAULT].priority[0] = 0xD,
		.table[BS_DISABLE].fn = BF_SETQOS,
		.table[BS_DISABLE].priority[0] = 0x4,
		.cur_scen = BS_DISABLE,
		.on = false,
		.enable = true,
	},
	[BTS_IDX_MODAPIF_GNSS] = {
		.id = BTS_MODAPIF_GNSS,
		.name = "modapif_gnss",
		.pa_base = EXYNOS7870_PA_PMU_ALIVE,
		.pd_name = "pd-modapif",
		.table[BS_DEFAULT].fn = BF_SETQOS,
		.table[BS_DEFAULT].priority[0] = 0x4,
		.table[BS_DISABLE].fn = BF_SETQOS,
		.table[BS_DISABLE].priority[0] = 0x4,
		.cur_scen = BS_DISABLE,
		.on = false,
		.enable = true,
	},
	[BTS_IDX_CPU] = {
		.id = BTS_CPU,
		.name = "cpu",
		.pa_base = EXYNOS7870_PA_SYSREG_MIF,
		.pd_name = "pd-cpu",
		.table[BS_DEFAULT].fn = BF_SETQOS,
		.table[BS_DEFAULT].priority[0] = 0x4,
		.table[BS_DISABLE].fn = BF_SETQOS,
		.table[BS_DISABLE].priority[0] = 0x4,
		.cur_scen = BS_DISABLE,
		.on = false,
		.enable = true,
	},
	[BTS_IDX_APL] = {
		.id = BTS_APL,
		.name = "apl",
		.pa_base = EXYNOS7870_PA_SYSREG_MIF,
		.pd_name = "pd-apl",
		.table[BS_DEFAULT].fn = BF_SETQOS,
		.table[BS_DEFAULT].priority[0] = 0x4,
		.table[BS_DISABLE].fn = BF_SETQOS,
		.table[BS_DISABLE].priority[0] = 0x4,
		.cur_scen = BS_DISABLE,
		.on = false,
		.enable = true,
	},
	[BTS_IDX_G3D] = {
		.id = BTS_G3D,
		.name = "g3d",
		.pa_base = EXYNOS7870_PA_BTS_G3D,
		.pd_name = "pd-g3d",
		.table[BS_DEFAULT].fn = BF_SETQOS_MO,
		.table[BS_DEFAULT].priority[0] = 0x44444444,
		.table[BS_DEFAULT].mo = 0x3f,
		.table[BS_DISABLE].fn = BF_DISABLE,
		.cur_scen = BS_DISABLE,
		.on = false,
		.enable = false,
	},
};

static struct bts_scenario bts_scen[] = {
	[BS_DISABLE] = {
		.name = "bts_disable",
		.id = BS_DISABLE,
	},
	[BS_DEFAULT] = {
		.name = "bts_default",
		.id = BS_DEFAULT,
	},
	[BS_CAM_BNS] = {
		.name = "bts_cam_bns",
		.id = BS_CAM_BNS,
	},
	[BS_DEBUG] = {
		.name = "bts_debugging_ip",
		.id = BS_DEBUG,
	},
	[BS_MAX] = {
		.name = "undefined"
	}
};

static DEFINE_SPINLOCK(bts_lock);
static LIST_HEAD(bts_list);

static void bts_clk_on(struct bts_info *bts)
{
	struct clk_info *ptr;
	enum bts_index btstable_index;

	ptr = bts->ct_ptr;
	if (ptr) {
		btstable_index = ptr->index;
		do {
			clk_enable(ptr->clk);
		} while (++ptr < clk_table + ARRAY_SIZE(clk_table)
				&& ptr->index == btstable_index);
	}
}

static void bts_clk_off(struct bts_info *bts)
{
	struct clk_info *ptr;
	enum bts_index btstable_index;

	ptr = bts->ct_ptr;
	if (ptr) {
		btstable_index = ptr->index;
		do {
			clk_disable(ptr->clk);
		} while (++ptr < clk_table + ARRAY_SIZE(clk_table)
				&& ptr->index == btstable_index);
	}
}

static void bts_setqos_ip_sysreg(enum bts_id id, void __iomem *va_base,
					unsigned int *priority)
{
	switch (id) {
	case BTS_DISPAUD:
		bts_setqos_sysreg(BTS_SYSREG_DISPAUD, va_base, priority);
		break;
	case BTS_ISP0:
		bts_setqos_sysreg(BTS_SYSREG_ISP0, va_base, priority);
		break;
	case BTS_ISP1:
		bts_setqos_sysreg(BTS_SYSREG_ISP1, va_base, priority);
		break;
	case BTS_MFCMSCL:
		bts_setqos_sysreg(BTS_SYSREG_MFCMSCL, va_base, priority);
		break;
	case BTS_FSYS0:
		bts_setqos_sysreg(BTS_SYSREG_FSYS0, va_base, priority);
		break;
	case BTS_FSYS1:
		bts_setqos_sysreg(BTS_SYSREG_FSYS1, va_base, priority);
		break;
	case BTS_FSYS2:
		bts_setqos_sysreg(BTS_SYSREG_FSYS2, va_base, priority);
		break;
	case BTS_MODAPIF_CP:
		bts_setqos_sysreg(BTS_SYSREG_MIF_MODAPIF_CP, va_base, priority);
		break;
	case BTS_MODAPIF_GNSS:
		bts_setqos_sysreg(BTS_SYSREG_MIF_MODAPIF_GNSS, va_base, priority);
		break;
	case BTS_CPU:
		bts_setqos_sysreg(BTS_SYSREG_MIF_CPU, va_base, priority);
		break;
	case BTS_APL:
		bts_setqos_sysreg(BTS_SYSREG_MIF_APL, va_base, priority);
		break;
	default:
		break;
	}
}

static void bts_setmo_ip_sysreg(enum bts_id id, void __iomem *va_base,
					unsigned int ar, unsigned int aw)
{
	return;
}

static void bts_set_ip_table(enum exynos_bts_scenario scen,
		struct bts_info *bts)
{
	enum exynos_bts_function fn = bts->table[scen].fn;
	bool on;

	BTS_DBG("[BTS] %s on:%d bts scen: [%s]->[%s]\n", bts->name, bts->on,
			bts_scen[bts->cur_scen].name, bts_scen[scen].name);

	switch (fn) {
	case BF_SETQOS:
		bts_setqos_ip_sysreg(bts->id, bts->va_base, \
					bts->table[scen].priority);

		if (bts->id == BTS_ISP0) {
			on = (scen == BS_DEFAULT || scen == BS_CAM_BNS) ? true : false;

			bts_setotf_sysreg(BTS_SYSREG_ISP_VRA_SEL, \
					bts->va_base, on);
			bts_setotf_sysreg(BTS_SYSREG_ISP_ISP_SCL_SEL, \
					bts->va_base, on);
		}
		break;
	case BF_SETQOS_BW:
		break;
	case BF_SETQOS_MO:
		if (bts->id & BTS_SYSREG_DOMAIN)
			bts_setmo_ip_sysreg(bts->id, bts->va_base, \
						bts->table[scen].mo, \
						bts->table[scen].mo);
		else if (bts->id == BTS_G3D)
			bts_setqos_mo(bts->va_base, bts->table[scen].priority[0], \
					bts->table[scen].mo);
		break;
	case BF_DISABLE:
		break;
	case BF_NOP:
		break;
	}

	bts->cur_scen = scen;
}

static enum exynos_bts_scenario bts_get_scen(struct bts_info *bts)
{
	enum exynos_bts_scenario scen;

	scen = BS_DEFAULT;

	return scen;
}


static void bts_add_scen(enum exynos_bts_scenario scen, struct bts_info *bts)
{
	struct bts_info *first = bts;
	int next = 0;
	int prev = 0;

	if (!bts)
		return;

	BTS_DBG("[bts %s] scen %s off\n",
			bts->name, bts_scen[scen].name);

	do {
		if (bts->enable) {
			if (bts->table[scen].next_scen == 0) {
				if (scen >= bts->top_scen) {
					bts->table[scen].prev_scen = bts->top_scen;
					bts->table[bts->top_scen].next_scen = scen;
					bts->top_scen = scen;
					bts->table[scen].next_scen = -1;

					if (bts->on)
						bts_set_ip_table(bts->top_scen, bts);

				} else {
					for (prev = bts->top_scen; prev > scen; prev = bts->table[prev].prev_scen)
						next = prev;

					bts->table[scen].prev_scen = bts->table[next].prev_scen;
					bts->table[scen].next_scen = bts->table[prev].next_scen;
					bts->table[next].prev_scen = scen;
					bts->table[prev].next_scen = scen;
				}
			}
		}

		bts = bts->table[scen].next_bts;

	} while (bts && bts != first);
}

static void bts_del_scen(enum exynos_bts_scenario scen, struct bts_info *bts)
{
	struct bts_info *first = bts;
	int next = 0;
	int prev = 0;

	if (!bts)
		return;

	BTS_DBG("[bts %s] scen %s off\n",
			bts->name, bts_scen[scen].name);

	do {
		if (bts->enable) {
			if (bts->table[scen].next_scen != 0) {
				if (scen == bts->top_scen) {
					prev = bts->table[scen].prev_scen;
					bts->top_scen = prev;
					bts->table[prev].next_scen = -1;
					bts->table[scen].next_scen = 0;
					bts->table[scen].prev_scen = 0;

					if (bts->on)
						bts_set_ip_table(prev, bts);
				} else if (scen < bts->top_scen) {
					prev = bts->table[scen].prev_scen;
					next = bts->table[scen].next_scen;

					bts->table[next].prev_scen = bts->table[scen].prev_scen;
					bts->table[prev].next_scen = bts->table[scen].next_scen;

					bts->table[scen].prev_scen = 0;
					bts->table[scen].next_scen = 0;

				} else {
					BTS_DBG("%s scenario couldn't exist above top_scen\n", bts_scen[scen].name);
				}
			}

		}

		bts = bts->table[scen].next_bts;

	} while (bts && bts != first);
}

void bts_scen_update(enum bts_scen_type type, unsigned int val)
{
	enum exynos_bts_scenario scen = BS_DEFAULT;
	struct bts_info *bts = NULL;
	bool on;

	spin_lock(&bts_lock);

	switch (type) {
	case TYPE_CAM_BNS:
		on = val ? true : false;
		scen = BS_CAM_BNS;
		bts = &exynos7_bts[BTS_IDX_ISP0];
		BTS_DBG("[BTS] CAM_BNS: %s\n", bts_scen[scen].name);
		break;
	default:
		spin_unlock(&bts_lock);
		return;
	}

	if (on)
		bts_add_scen(scen, bts);
	else
		bts_del_scen(scen, bts);

	spin_unlock(&bts_lock);
}

void bts_initialize(const char *pd_name, bool on)
{
	struct bts_info *bts;
	enum exynos_bts_scenario scen = BS_DISABLE;

	spin_lock(&bts_lock);

	list_for_each_entry(bts, &bts_list, list)
		if (pd_name && bts->pd_name && !strncmp(bts->pd_name, pd_name, strlen(pd_name))) {
			BTS_DBG("[BTS] %s on/off:%d->%d\n", bts->name, bts->on, on);

			if (!bts->on && on)
				bts_clk_on(bts);
			else if (bts->on && !on)
				bts_clk_off(bts);

			if (!bts->enable) {
				bts->on = on;
				continue;
			}

			scen = bts_get_scen(bts);
			if (on) {
				bts_add_scen(scen, bts);
				if (!bts->on) {
					bts->on = true;
					bts_set_ip_table(bts->top_scen, bts);
				}
			} else {
				if (bts->on)
					bts->on = false;

				bts_del_scen(scen, bts);
			}
		}

	spin_unlock(&bts_lock);
}

static void scen_chaining(enum exynos_bts_scenario scen)
{
	struct bts_info *prev = NULL;
	struct bts_info *first = NULL;
	struct bts_info *bts;

	if (bts_scen[scen].ip) {
		list_for_each_entry(bts, &bts_list, list) {
			if (bts_scen[scen].ip & bts->id) {
				if (!first)
					first = bts;
				if (prev)
					prev->table[scen].next_bts = bts;

				prev = bts;
			}
		}

		if (prev)
			prev->table[scen].next_bts = first;

		bts_scen[scen].head = first;
	}
}

static int exynos7_qos_status_open_show(struct seq_file *buf, void *d)
{
	unsigned int i;
	unsigned int val_r;

	spin_lock(&bts_lock);

	for (i = 0; i < ARRAY_SIZE(exynos7_bts); i++) {
		seq_printf(buf, "bts[%d] %s : ", i, exynos7_bts[i].name);
		if (exynos7_bts[i].on) {
			if (exynos7_bts[i].ct_ptr)
				bts_clk_on(exynos7_bts + i);

			/* axi qoscontrol */
			switch (exynos7_bts[i].id) {
			case BTS_DISPAUD:
				val_r = __raw_readl(exynos7_bts[i].va_base + DISPAUD_QOS_CON);
				break;
			case BTS_ISP0:
				val_r = __raw_readl(exynos7_bts[i].va_base + ISP_QOS_CON0);
				break;
			case BTS_ISP1:
				val_r = __raw_readl(exynos7_bts[i].va_base + ISP_QOS_CON1);
				break;
			case BTS_MFCMSCL:
				val_r = __raw_readl(exynos7_bts[i].va_base + MFCMSCL_QOS_CON);
				break;
			case BTS_FSYS0:
				val_r = __raw_readl(exynos7_bts[i].va_base + FSYS_QOS_CON0);
				break;
			case BTS_FSYS1:
				val_r = __raw_readl(exynos7_bts[i].va_base + FSYS_QOS_CON1);
				break;
			case BTS_FSYS2:
				val_r = __raw_readl(exynos7_bts[i].va_base + FSYS_QOS_CON2);
				break;
			case BTS_MODAPIF_CP:
				val_r = __raw_readl(exynos7_bts[i].va_base + PMUALIVE_MODAPIF_CP_QOS_CON);
				break;
			case BTS_MODAPIF_GNSS:
				val_r = __raw_readl(exynos7_bts[i].va_base + PMUALIVE_MODAPIF_GNSS_QOS_CON);
				break;
			case BTS_CPU:
				val_r = __raw_readl(exynos7_bts[i].va_base + MIF_CPU_QOS_CON);
				break;
			case BTS_APL:
				val_r = __raw_readl(exynos7_bts[i].va_base + MIF_APL_QOS_CON);
				break;
			default:
				break;
			}

			if (exynos7_bts[i].ct_ptr)
				bts_clk_off(exynos7_bts + i);
		} else {
			seq_puts(buf, "off\n");
		}
	}

	spin_unlock(&bts_lock);

	return 0;
}

static int exynos7_qos_open(struct inode *inode, struct file *file)
{
	return single_open(file, exynos7_qos_status_open_show, inode->i_private);
}

static const struct file_operations debug_qos_status_fops = {
	.open		= exynos7_qos_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int exynos7_bw_status_open_show(struct seq_file *buf, void *d)
{
	mutex_lock(&media_mutex);

	seq_printf(buf, "bts bandwidth (total %u) : decon %u, cam %u, mif_freq %u\n",
			total_bw, decon_bw, cam_bw, mif_freq);

	mutex_unlock(&media_mutex);

	return 0;
}

static int exynos7_bw_open(struct inode *inode, struct file *file)
{
	return single_open(file, exynos7_bw_status_open_show, inode->i_private);
}

static const struct file_operations debug_bw_status_fops = {
	.open		= exynos7_bw_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int debug_enable_get(void *data, unsigned long long *val)
{
	struct bts_info *first = bts_scen[BS_DEBUG].head;
	struct bts_info *bts = bts_scen[BS_DEBUG].head;
	int cnt = 0;

	if (first) {
		do {
			pr_info("%s, ", bts->name);
			cnt++;
			bts = bts->table[BS_DEBUG].next_bts;
		} while (bts && bts != first);
	}
	if (first && first->top_scen == BS_DEBUG)
		pr_info("is on\n");
	else
		pr_info("is off\n");
	*val = cnt;

	return 0;
}

static int debug_enable_set(void *data, unsigned long long val)
{
	struct bts_info *first = bts_scen[BS_DEBUG].head;
	struct bts_info *bts = bts_scen[BS_DEBUG].head;

	if (first) {
		do {
			pr_info("%s, ", bts->name);

			bts = bts->table[BS_DEBUG].next_bts;
		} while (bts && bts != first);
	}

	spin_lock(&bts_lock);

	if (val) {
		bts_add_scen(BS_DEBUG, bts_scen[BS_DEBUG].head);
		pr_info("is on\n");
	} else {
		bts_del_scen(BS_DEBUG, bts_scen[BS_DEBUG].head);
		pr_info("is off\n");
	}

	spin_unlock(&bts_lock);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(debug_enable_fops, debug_enable_get, debug_enable_set, "%llx\n");

static void bts_status_print(void)
{
	pr_info("0 : disable debug ip\n");
	pr_info("1 : BF_SETQOS\n");
	pr_info("2 : BF_SETQOS_BW\n");
	pr_info("3 : BF_SETQOS_MO\n");
}

static int debug_ip_enable_get(void *data, unsigned long long *val)
{
	struct bts_info *bts = data;

	if (bts->table[BS_DEBUG].next_scen) {
		switch (bts->table[BS_DEBUG].fn) {
		case BF_SETQOS:
			*val = 1;
			break;
		case BF_SETQOS_BW:
			*val = 2;
			break;
		case BF_SETQOS_MO:
			*val = 3;
			break;
		default:
			*val = 4;
			break;
		}
	} else {
		*val = 0;
	}

	bts_status_print();

	return 0;
}

static int debug_ip_enable_set(void *data, unsigned long long val)
{
	struct bts_info *bts = data;

	spin_lock(&bts_lock);

	if (val) {
		bts_scen[BS_DEBUG].ip |= bts->id;

		scen_chaining(BS_DEBUG);

		switch (val) {
		case 1:
			bts->table[BS_DEBUG].fn = BF_SETQOS;
			break;
		case 2:
			bts->table[BS_DEBUG].fn = BF_SETQOS_BW;
			break;
		case 3:
			bts->table[BS_DEBUG].fn = BF_SETQOS_MO;
			break;
		default:
			break;
		}

		bts_add_scen(BS_DEBUG, bts);

		pr_info("%s on 0x%x\n", bts->name, bts_scen[BS_DEBUG].ip);
	} else {
		bts->table[BS_DEBUG].next_bts = NULL;
		bts_del_scen(BS_DEBUG, bts);

		bts_scen[BS_DEBUG].ip &= ~bts->id;
		scen_chaining(BS_DEBUG);

		pr_info("%s off 0x%x\n", bts->name, bts_scen[BS_DEBUG].ip);
	}

	spin_unlock(&bts_lock);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(debug_ip_enable_fops, debug_ip_enable_get, debug_ip_enable_set, "%llx\n");

static int debug_ip_mo_get(void *data, unsigned long long *val)
{
	struct bts_info *bts = data;

	spin_lock(&bts_lock);

	*val = bts->table[BS_DEBUG].mo;

	spin_unlock(&bts_lock);

	return 0;
}

static int debug_ip_mo_set(void *data, unsigned long long val)
{
	struct bts_info *bts = data;

	spin_lock(&bts_lock);

	bts->table[BS_DEBUG].mo = val;
	if (bts->top_scen == BS_DEBUG) {
		if (bts->on)
			bts_set_ip_table(BS_DEBUG, bts);
	}
	pr_info("Debug mo set %s : mo 0x%x\n", bts->name, bts->table[BS_DEBUG].mo);

	spin_unlock(&bts_lock);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(debug_ip_mo_fops, debug_ip_mo_get, debug_ip_mo_set, "%llx\n");

static int debug_ip_token_get(void *data, unsigned long long *val)
{
	return 0;
}

static int debug_ip_token_set(void *data, unsigned long long val)
{
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(debug_ip_token_fops, debug_ip_token_get, debug_ip_token_set, "%llx\n");

static int debug_ip_window_get(void *data, unsigned long long *val)
{
	return 0;
}

static int debug_ip_window_set(void *data, unsigned long long val)
{
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(debug_ip_window_fops, debug_ip_window_get, debug_ip_window_set, "%llx\n");

static int debug_ip_qos_get(void *data, unsigned long long *val)
{
	struct bts_info *bts = data;

	spin_lock(&bts_lock);

	*val = bts->table[BS_DEBUG].priority[0];

	spin_unlock(&bts_lock);

	return 0;
}

static int debug_ip_qos_set(void *data, unsigned long long val)
{
	struct bts_info *bts = data;

	spin_lock(&bts_lock);

	bts->table[BS_DEBUG].priority[0] = val;
	if (bts->top_scen == BS_DEBUG) {
		if (bts->on)
			bts_setqos(bts->va_base, bts->table[BS_DEBUG].priority[0]);
	}
	pr_info("Debug qos set %s : 0x%x\n", bts->name, bts->table[BS_DEBUG].priority[0]);

	spin_unlock(&bts_lock);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(debug_ip_qos_fops, debug_ip_qos_get, debug_ip_qos_set, "%llx\n");

void bts_debugfs(void)
{
	struct bts_info *bts;
	struct dentry *den;
	struct dentry *subden;

	den = debugfs_create_dir("bts_dbg", NULL);
	debugfs_create_file("qos_status", 0440,	den, NULL, &debug_qos_status_fops);
	debugfs_create_file("bw_status", 0440,	den, NULL, &debug_bw_status_fops);
	debugfs_create_file("enable", 0440,	den, NULL, &debug_enable_fops);

	den = debugfs_create_dir("bts", den);
	list_for_each_entry(bts, &bts_list, list) {
		subden = debugfs_create_dir(bts->name, den);
		debugfs_create_file("qos", 0644, subden, bts, &debug_ip_qos_fops);
		debugfs_create_file("token", 0644, subden, bts, &debug_ip_token_fops);
		debugfs_create_file("window", 0644, subden, bts, &debug_ip_window_fops);
		debugfs_create_file("mo", 0644, subden, bts, &debug_ip_mo_fops);
		debugfs_create_file("enable", 0644, subden, bts, &debug_ip_enable_fops);
	}
}

static void bts_drex_init(void __iomem *base)
{

	BTS_DBG("[BTS][%s] bts drex init\n", __func__);

	__raw_writel(0x00000000, base + QOS_TIMEOUT_0xF);
	__raw_writel(0x00000004, base + QOS_TIMEOUT_0xE);
	__raw_writel(0x00000010, base + QOS_TIMEOUT_0xD);
	__raw_writel(0x00000010, base + QOS_TIMEOUT_0xC);
	__raw_writel(0x00000020, base + QOS_TIMEOUT_0xB);
	__raw_writel(0x00000040, base + QOS_TIMEOUT_0xA);
	__raw_writel(0x00000100, base + QOS_TIMEOUT_0x9);
	__raw_writel(0x00000100, base + QOS_TIMEOUT_0x8);
	__raw_writel(0x00000100, base + QOS_TIMEOUT_0x7);
	__raw_writel(0x00000100, base + QOS_TIMEOUT_0x6);
	__raw_writel(0x00000100, base + QOS_TIMEOUT_0x5);
	__raw_writel(0x00000100, base + QOS_TIMEOUT_0x4);
	__raw_writel(0x00000100, base + QOS_TIMEOUT_0x3);
	__raw_writel(0x00000100, base + QOS_TIMEOUT_0x2);
	__raw_writel(0x00000100, base + QOS_TIMEOUT_0x1);
	__raw_writel(0x00000100, base + QOS_TIMEOUT_0x0);
}

static void bts_initialize_domains(void)
{
		bts_initialize("fsys", true);
		bts_initialize("pd-modapif", true);
		bts_initialize("pd-cpu", true);
		bts_initialize("pd-apl", true);
}

static int exynos_bts_notifier_event(struct notifier_block *this,
		unsigned long event,
		void *ptr)
{
	switch (event) {
	case PM_POST_SUSPEND:
		bts_drex_init(base_drex);
		bts_initialize_domains();

		return NOTIFY_OK;
	}

	return NOTIFY_DONE;
}

static struct notifier_block exynos_bts_notifier = {
	.notifier_call = exynos_bts_notifier_event,
};

int exynos7_update_bts_param(int target_idx, int work)
{
	return 0;
}

static int exynos7_bts_notify(unsigned long freq)
{
	BUG_ON(irqs_disabled());

	return srcu_notifier_call_chain(&exynos_media_notifier, freq, NULL);
}

int exynos7_bts_register_notifier(struct notifier_block *nb)
{
	return srcu_notifier_chain_register(&exynos_media_notifier, nb);
}

int exynos7_bts_unregister_notifier(struct notifier_block *nb)
{
	return srcu_notifier_chain_unregister(&exynos_media_notifier, nb);
}

void exynos7_init_bts_ioremap(void)
{
	base_drex = ioremap(EXYNOS7870_PA_DREX0, SZ_4K);
	base_bts_pmu_alive = ioremap(EXYNOS7870_PA_PMU_ALIVE, SZ_4K);
}

int wincnt;
int exynos_update_overlay_wincnt(int cnt)
{
	BTS_DBG("[BTS CNT] overlay window count: %d\n", cnt);

	wincnt = cnt;

	return 0;
}

void exynos_update_media_scenario(enum bts_media_type media_type,
		unsigned int bw, int bw_type)
{
	mutex_lock(&media_mutex);

	switch (media_type) {
	case TYPE_DECON_INT:
		decon_bw = bw;
		num_active_disp_win = decon_bw / FHD_BW;
		break;
	case TYPE_CAM:
		cam_bw = bw;
		break;
	default:
		pr_err("DEVFREQ(MIF) : unsupportd media_type - %u", media_type);
		break;
	}

	total_bw = decon_bw + cam_bw;

	/* MIF minimum frequency calculation as per BTS guide */
	if (cam_bw && decon_bw) {
		if (decon_bw <= (2 * FHD_BW))
			mif_freq = 676000;
		else
			mif_freq = 728000;
	} else {
		if (decon_bw <= (2 * FHD_BW))
			mif_freq = 451000;
		else
			mif_freq = 676000;
		if (wincnt == 3 && decon_bw <= (2 *FHD_BW))
			mif_freq = 676000;
	}

	exynos7_bts_notify(mif_freq);

	BTS_DBG("[BTS BW] total: %u, decon %u, cam %u\n",
			total_bw, decon_bw, cam_bw);
	BTS_DBG("[BTS FREQ] mif_freq: %u\n", mif_freq);

	pm_qos_update_request(&exynos7_mif_bts_qos, mif_freq);

	mutex_unlock(&media_mutex);
}

static int __init exynos7_bts_init(void)
{
	int i;
	int ret;
	enum bts_index btstable_index = BTS_MAX;

	BTS_DBG("[BTS][%s] bts init\n", __func__);

	for (i = 0; i < ARRAY_SIZE(clk_table); i++) {

		if (btstable_index != clk_table[i].index) {
			btstable_index = clk_table[i].index;
			exynos7_bts[btstable_index].ct_ptr = clk_table + i;
		}
		clk_table[i].clk = clk_get(NULL, clk_table[i].clk_name);

		if (IS_ERR(clk_table[i].clk)){
			BTS_DBG("failed to get bts clk %s\n",
					clk_table[i].clk_name);
			if (btstable_index != BTS_MAX)
				exynos7_bts[btstable_index].ct_ptr = NULL;
		}
		else {
			ret = clk_prepare(clk_table[i].clk);
			if (ret) {
				pr_err("[BTS] failed to prepare bts clk %s\n",
						clk_table[i].clk_name);
				for (; i >= 0; i--)
					clk_put(clk_table[i].clk);
				return ret;
			}
		}
	}

	for (i = 0; i < ARRAY_SIZE(exynos7_bts); i++) {
		exynos7_bts[i].va_base = ioremap(exynos7_bts[i].pa_base, SZ_8K);

		list_add(&exynos7_bts[i].list, &bts_list);
	}

	for (i = BS_DEFAULT + 1; i < BS_MAX; i++) {
		scen_chaining(i);
		BTS_DBG("[BTS][%s] scene(%d) is chanined\n", __func__, i);
	}

	exynos7_init_bts_ioremap();
	bts_drex_init(base_drex);
	bts_initialize_domains();

	pm_qos_add_request(&exynos7_mif_bts_qos, PM_QOS_BUS_THROUGHPUT, 0);
	pm_qos_add_request(&exynos7_int_bts_qos, PM_QOS_DEVICE_THROUGHPUT, 0);

	register_pm_notifier(&exynos_bts_notifier);

	srcu_init_notifier_head(&exynos_media_notifier);

	return 0;
}
arch_initcall(exynos7_bts_init);
