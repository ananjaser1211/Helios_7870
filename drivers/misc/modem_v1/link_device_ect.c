/*
 * Copyright (C) 2010 Samsung Electronics.
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

#if defined(CONFIG_ECT)
#include <soc/samsung/ect_parser.h>
#endif

#include "modem_prj.h"
#include "link_device_ect.h"

static unsigned long found_table_mask;

static struct {
	char *table_name;
	char *tag;
} cp_dvfs_search_table[MAX_TABLE_COUNT] = {
	/* [index]  = {	table_name,	tag,	}, */
	[CL0_TABLE] = { "dvfs_cpucl0",	"CL0", },
	[CL1_TABLE] = { "dvfs_cpucl1",	"CL1", },
	[MIF_TABLE] = { "dvfs_mif",	"MIF", },
	[INT_TABLE] = { "dvfs_int",	"INT", },
};

#if defined(CONFIG_ECT)
inline int get_total_table_count(struct ect_table_data *table_data)
{
	return hweight_long(found_table_mask);
}

static inline void mark_table_found(int i)
{
	set_bit(i, &found_table_mask);
}

struct freq_table *get_ordered_table(struct ect_table_data *table_data,
		int n)
{
	int index;

	if ((n + 1) > get_total_table_count(table_data))
		return NULL;

	for_each_set_bit(index, &found_table_mask, MAX_TABLE_COUNT) {
		if (n == 0)
			break;
		n--;
	}

	return &table_data->ect_table[index];
}

int exynos_devfreq_parse_ect(struct ect_table_data *table_data)
{
	struct freq_table *ect_table;
	struct ect_dvfs_header *header;
	struct ect_dvfs_domain *domain;
	unsigned long missing_table;
	int i, j, counter;
	int ret = 0;

	header = (struct ect_dvfs_header *)ect_get_block(BLOCK_DVFS);
	if (header == NULL)
		return -ENODEV;

	strlcpy(table_data->parser_version, "CT0",
			sizeof(table_data->parser_version));

	for (i = 0; i < MAX_TABLE_COUNT; i++) {
		domain = ect_dvfs_get_domain(header,
				cp_dvfs_search_table[i].table_name);
		if (!domain)
			continue;

		mark_table_found(i);
		ect_table = &table_data->ect_table[i];
		strlcpy(ect_table->tag, cp_dvfs_search_table[i].tag,
				sizeof(ect_table->tag));

		mif_info("parsing %.4s table...\n", ect_table->tag);

		BUG_ON(domain->num_of_level > sizeof(ect_table->freq));

		ect_table->num_of_table = domain->num_of_level;
		for (j = domain->num_of_level - 1; j >= 0; j--) {
			counter = (domain->num_of_level - 1) - j;
			ect_table->freq[j] = domain->list_level[counter].level;
			mif_info("%.4s_LEV[%d] : %u\n", ect_table->tag, j + 1,
					ect_table->freq[j]);
		}
	}

	for_each_clear_bit(missing_table, &found_table_mask,
			MAX_TABLE_COUNT) {
		switch (missing_table) {
		case CL0_TABLE:
		case CL1_TABLE:
			if (!test_bit(CL0_TABLE, &found_table_mask) &&
			    !test_bit(CL1_TABLE, &found_table_mask)) {
				mif_err("Missing CPU ECT data\n");
				ret = -EINVAL;
			}
			break;
		case MIF_TABLE:
			mif_err("Missing MIF ECT data\n");
			ret = -EINVAL;
			break;
		case INT_TABLE:
			mif_err("Missing INT ECT data\n");
			ret = -EINVAL;
			break;
		default:
			mif_err("Table %lu is missing\n", missing_table);
		}
	}

	return ret;
}
#else /* !CONFIG_ECT */
int get_total_table_count(struct ect_table_data *table_data)
{
	return 0;
}

struct freq_table *get_ordered_table(struct ect_table_data *table_data, int n)
{
	return NULL;
}

int exynos_devfreq_parse_ect(struct ect_table_data *table_data)

	struct freq_table *ect_table = table_data->ect_table;
	mif_err("ECT is not defined\n");

	INIT_LIST_HEAD(&table_data->table_list);
	ect_table[CL0_TABLE].num_of_table = 0;
	ect_table[CL1_TABLE].num_of_table = 0;
	ect_table[MIF_TABLE].num_of_table = 0;
	ect_table[INT_TABLE].num_of_table = 0;

	return 0;
}
#endif /* CONFIG_ECT */
