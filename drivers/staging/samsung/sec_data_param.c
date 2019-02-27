/*
 *sec_hw_param.c
 *
 * Copyright (c) 2016 Samsung Electronics Co., Ltd
 *              http://www.samsung.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/sec_sysfs.h>
#include <linux/soc/samsung/exynos-soc.h>
#include <asm/io.h>

#define MAX_DDR_VENDOR 16
#define LPDDR_BASE 0x104809A4
#define DATA_SIZE 700

enum ids_info
{
	table_ver,
	cpu_asv,
	g3d_asv,
	mif_asv
};

/*
   LPDDR4 (JESD209-4) MR5 Manufacturer ID
   0000 0000B : Reserved
   0000 0001B : Samsung
   0000 0101B : Nanya
   0000 0110B : SK hynix
   0000 1000B : Winbond
   0000 1001B : ESMT
   1111 1111B : Micron
   All others : Reserved
 */

static char* lpddr4_manufacture_name[MAX_DDR_VENDOR] =
	{"NA",
	"SEC"/* Samsung */,
	"NA",
	"NA",
	"NA",
	"NAN" /* Nanya */,
	"HYN" /* SK hynix */,
	"NA",
	"WIN" /* Winbond */,
	"ESM" /* ESMT */,
	"NA",
	"NA",
	"NA",
	"NA",
	"NA",
	"MIC" /* Micron */,};

extern int asv_ids_information(enum ids_info id);
extern unsigned long long pwrcal_get_dram_manufacturer(void);
extern struct exynos_chipid_info exynos_soc_info;
static unsigned int sec_hw_rev;

static int __init sec_hw_param_get_hw_rev(char *arg)
{
    get_option(&arg, &sec_hw_rev);
    return 0;
}
early_param("androidboot.revision", sec_hw_param_get_hw_rev);

static u32 chipid_reverse_value(u32 value, u32 bitcnt)
{
	int tmp, ret = 0;
	int i;

	for (i = 0; i < bitcnt; i++)
	{
		tmp = (value >> i) & 0x1;
		ret += tmp << ((bitcnt - 1) - i);
	}

	return ret;
}

static void chipid_dec_to_36(u32 in, char *p)
{
	int mod;
	int i;

	for (i = 4; i >= 1; i--) {
		mod = in % 36;
		in /= 36;
		p[i] = (mod < 10) ? (mod + '0') : (mod-10 + 'A');
	}

	p[0] = 'N';
	p[5] = 0;
}

static char* get_dram_manufacturer(void)
{
	void* lpddr_reg;
	u32 val;
	int mr5_vendor_id = 0;

	lpddr_reg = ioremap(LPDDR_BASE, SZ_32);

	if (!lpddr_reg) {
		pr_err("failed to get i/o address lpddr_reg\n");
		return lpddr4_manufacture_name[mr5_vendor_id];
	}
	
	val = readl((void __iomem *)lpddr_reg);

	mr5_vendor_id = 0xf & (val >> 24);

	return lpddr4_manufacture_name[mr5_vendor_id];
}

static ssize_t sec_hw_param_ap_info_show(struct kobject *kobj,
			         struct kobj_attribute *attr, char *buf)
{
	ssize_t info_size = 0;
	int reverse_id_0 = 0;
	int tmp = 0;
	char lot_id[6];

	reverse_id_0 = chipid_reverse_value(exynos_soc_info.lot_id, 32);
	tmp = (reverse_id_0 >> 11) & 0x1FFFFF;
	chipid_dec_to_36(tmp, lot_id);

	info_size += snprintf(buf, DATA_SIZE, "\"HW_REV\":\"%d\",", sec_hw_rev);
	info_size += snprintf((char*)(buf+info_size), DATA_SIZE - info_size, "\"LOT_ID\":\"%s\",", lot_id);
	info_size += snprintf((char*)(buf+info_size), DATA_SIZE - info_size, "\"ASV_LIT\":\"%d\",", asv_ids_information(cpu_asv));
	//CPUCL0/1 use same ASV table
	info_size += snprintf((char*)(buf+info_size), DATA_SIZE - info_size, "\"ASV_BIG\":\"%d\",", asv_ids_information(cpu_asv));
	info_size += snprintf((char*)(buf+info_size), DATA_SIZE - info_size, "\"ASV_MIF\":\"%d\",", asv_ids_information(mif_asv));
	info_size += snprintf((char*)(buf+info_size), DATA_SIZE - info_size, "\"IDS_BIG\":\"\",");
	info_size += snprintf((char*)(buf+info_size), DATA_SIZE - info_size, "\"PARAM0\":\"\"");
	
	return info_size;
}

static ssize_t sec_hw_param_ddr_info_show(struct kobject *kobj,
			         struct kobj_attribute *attr, char *buf)
{
	ssize_t info_size = 0;

	info_size += snprintf((char*)(buf), DATA_SIZE, "\"DDRV\":\"%s\",", get_dram_manufacturer());
	info_size += snprintf((char*)(buf+info_size), DATA_SIZE - info_size, "\"C2D\":\"\",");
	info_size += snprintf((char*)(buf+info_size), DATA_SIZE - info_size, "\"D2D\":\"\"");
	
	return info_size;
}


static struct kobj_attribute sec_hw_param_ap_Info_attr =
        __ATTR(ap_info, 0440, sec_hw_param_ap_info_show, NULL);

static struct kobj_attribute sec_hw_param_ddr_Info_attr =
        __ATTR(ddr_info, 0440, sec_hw_param_ddr_info_show, NULL);

static struct attribute *sec_hw_param_attributes[] = {
	&sec_hw_param_ap_Info_attr.attr,	
	&sec_hw_param_ddr_Info_attr.attr,
	NULL,
};

static struct attribute_group sec_hw_param_attr_group = {
	.attrs = sec_hw_param_attributes,
};

static int __init sec_hw_param_init(void)
{
	int ret=0;
	struct device* dev;

	dev = sec_device_create(NULL, "sec_hw_param");
	ret = sysfs_create_group(&dev->kobj, &sec_hw_param_attr_group);

	if (ret)
		printk("%s : could not create sysfs noden", __func__);
	
	return 0;
}
device_initcall(sec_hw_param_init);
