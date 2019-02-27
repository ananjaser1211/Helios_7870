/*
 * Copyright (C) 2012 Samsung Electronics.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __LINK_DEVICE_ECT_H__
#define __LINK_DEVICE_ECT_H__

#include <linux/types.h>

enum ect_table_type {
	CL0_TABLE,
	CL1_TABLE,
	MIF_TABLE,
	INT_TABLE,
	MAX_TABLE_COUNT,
};

#define FREQ_MAX_LV (40)

struct freq_table {
	int num_of_table;
	char tag[4];
	u32 freq[FREQ_MAX_LV];
};

struct ect_table_data {
	char parser_version[4];
	struct freq_table ect_table[MAX_TABLE_COUNT];
};

int get_total_table_count(struct ect_table_data *table_data);
struct freq_table *get_ordered_table(struct ect_table_data *table_data, int n);
int exynos_devfreq_parse_ect(struct ect_table_data *table_data);

#endif /* __LINK_DEVICE_ECT_H__ */