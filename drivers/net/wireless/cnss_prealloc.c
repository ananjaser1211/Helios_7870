/* Copyright (c) 2012,2014-2015 The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/stacktrace.h>
#include <linux/spinlock.h>
#include <linux/skbuff.h>
#include <linux/jiffies.h>

#define DEBUG 1

static DEFINE_SPINLOCK(alloc_lock);

#ifdef CONFIG_SLUB_DEBUG_ON
#define WCNSS_MAX_STACK_TRACE			64
#endif

struct wcnss_prealloc {
	int occupied;
	unsigned int size;
	void *ptr;
	unsigned long time;
#ifdef CONFIG_SLUB_DEBUG_ON
	unsigned long stack_trace[WCNSS_MAX_STACK_TRACE];
	struct stack_trace trace;
#endif
};

/* pre-alloced mem for WLAN driver */
static struct wcnss_prealloc wcnss_skb_allocs[] = {
	{0, 60 * 1024, NULL, 0},
	{0, 60 * 1024, NULL, 0},
	{0, 60 * 1024, NULL, 0},
	{0, 60 * 1024, NULL, 0},
	{0, 60 * 1024, NULL, 0},  // 60k*5 for Tx bundle
	{0, 120 * 1024, NULL, 0},
	{0, 120 * 1024, NULL, 0},
	{0, 120 * 1024, NULL, 0},
	{0, 120 * 1024, NULL, 0},
	{0, 120 * 1024, NULL, 0}, // 120k*5 for Rx bundle
};

static struct wcnss_prealloc wcnss_allocs[] = {
	{0, 8  * 1024, NULL, 0},
	{0, 8  * 1024, NULL, 0},
	{0, 8  * 1024, NULL, 0},
	{0, 8  * 1024, NULL, 0},
	{0, 8  * 1024, NULL, 0},
	{0, 8  * 1024, NULL, 0},
	{0, 8  * 1024, NULL, 0},
	{0, 8  * 1024, NULL, 0},
	{0, 8  * 1024, NULL, 0},
	{0, 8  * 1024, NULL, 0},
	{0, 8  * 1024, NULL, 0},
	{0, 8  * 1024, NULL, 0},
	{0, 8  * 1024, NULL, 0},
	{0, 8  * 1024, NULL, 0},
	{0, 8  * 1024, NULL, 0},
	{0, 8  * 1024, NULL, 0},
	{0, 8  * 1024, NULL, 0},
	{0, 8  * 1024, NULL, 0},
	{0, 8  * 1024, NULL, 0},
	{0, 8  * 1024, NULL, 0},
	{0, 8  * 1024, NULL, 0},
	{0, 8  * 1024, NULL, 0},
	{0, 8  * 1024, NULL, 0},
	{0, 8  * 1024, NULL, 0}, // 8 * 24
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0},
	{0, 12 * 1024, NULL, 0}, // 12 * 40
	{0, 24 * 1024, NULL, 0},
	{0, 24 * 1024, NULL, 0}, // 24 * 2
	{0, 32 * 1024, NULL, 0},
	{0, 32 * 1024, NULL, 0}, // 32 * 2
	{0, 42 * 1024, NULL, 0},
	{0, 42 * 1024, NULL, 0},
	{0, 42 * 1024, NULL, 0},
	{0, 42 * 1024, NULL, 0}, // 42 * 4
	{0, 76 * 1024, NULL, 0},
	{0, 76 * 1024, NULL, 0}, // 76 * 2
	{0, 1504 * 1024, NULL}, 
};

int wcnss_prealloc_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(wcnss_allocs); i++) {
		wcnss_allocs[i].occupied = 0;
		wcnss_allocs[i].ptr = kmalloc(wcnss_allocs[i].size, GFP_KERNEL);
		wcnss_allocs[i].time = 0;
		if (wcnss_allocs[i].ptr == NULL)
			return -ENOMEM;
	}

	for (i = 0; i < ARRAY_SIZE(wcnss_skb_allocs); i++) {
		wcnss_skb_allocs[i].occupied = 0;
		wcnss_skb_allocs[i].ptr = dev_alloc_skb(wcnss_skb_allocs[i].size);
		wcnss_skb_allocs[i].time = 0;
		if (wcnss_skb_allocs[i].ptr == NULL)
			return -ENOMEM;
	}
	return 0;
}
EXPORT_SYMBOL(wcnss_prealloc_init);

void wcnss_prealloc_deinit(void)
{
	int i = 0;

	for (i = 0; i < ARRAY_SIZE(wcnss_allocs); i++) {
		kfree(wcnss_allocs[i].ptr);
		wcnss_allocs[i].ptr = NULL;
	}

	for (i = 0; i < ARRAY_SIZE(wcnss_skb_allocs); i++) {
		dev_kfree_skb(wcnss_skb_allocs[i].ptr);
		wcnss_skb_allocs[i].ptr = NULL;
	}
}
EXPORT_SYMBOL(wcnss_prealloc_deinit);

#ifdef CONFIG_SLUB_DEBUG_ON
static void wcnss_prealloc_save_stack_trace(struct wcnss_prealloc *entry)
{
	struct stack_trace *trace = &entry->trace;

	memset(&entry->stack_trace, 0, sizeof(entry->stack_trace));
	trace->nr_entries = 0;
	trace->max_entries = WCNSS_MAX_STACK_TRACE;
	trace->entries = entry->stack_trace;
	trace->skip = 2;

	save_stack_trace(trace);

	return;
}
#else
static inline void wcnss_prealloc_save_stack_trace(struct wcnss_prealloc *entry)
{
	return;
}
#endif

static unsigned long wcnss_get_time(void)
{
	return jiffies_to_msecs(jiffies);
}

void *wcnss_prealloc_get(unsigned int size)
{
	int i = 0;
	unsigned long flags;

	spin_lock_irqsave(&alloc_lock, flags);
	/* jamie */
	if (size > 500 * 1024)
	{
		i = ARRAY_SIZE(wcnss_allocs)-1;
		if (wcnss_allocs[i].occupied)
		{
			spin_unlock_irqrestore(&alloc_lock, flags);
			pr_err("wcnss: %s: prealloc not available for FW RAMDUMP size: %d\n",
					__func__, size);
			return NULL;
		}

		wcnss_allocs[i].occupied = 1;
		wcnss_allocs[i].time = wcnss_get_time();
		spin_unlock_irqrestore(&alloc_lock, flags);
		wcnss_allocs[i].time = wcnss_get_time();
		wcnss_prealloc_save_stack_trace(&wcnss_allocs[i]);
		pr_err("wcnss: Get Memory for FW RAMDUMP from prealloc\n");
		return wcnss_allocs[i].ptr;
	}
	/* Jamie */
	for (i = 0; i < ARRAY_SIZE(wcnss_allocs); i++) {
		if (wcnss_allocs[i].occupied)
			continue;

		if (wcnss_allocs[i].size > size) {
			/* we found the slot */
			wcnss_allocs[i].occupied = 1;
			spin_unlock_irqrestore(&alloc_lock, flags);
			wcnss_prealloc_save_stack_trace(&wcnss_allocs[i]);
			return wcnss_allocs[i].ptr;
		}
	}
	spin_unlock_irqrestore(&alloc_lock, flags);

	pr_err("wcnss: %s: prealloc not available for size: %d\n",
			__func__, size);
#ifdef DEBUG
	printk("time: %16lu\n", wcnss_get_time());
	for (i = 0; i < ARRAY_SIZE(wcnss_allocs); i++) {
		if (wcnss_allocs[i].occupied)
			printk("%02d: size %08d, time: %16lu\n", i, wcnss_allocs[i].size, wcnss_allocs[i].time);
		else
			printk("%02d: size %08d is free", i, wcnss_allocs[i].size);
	}
#endif

	return NULL;
}
EXPORT_SYMBOL(wcnss_prealloc_get);

int wcnss_prealloc_put(void *ptr)
{
	int i = 0;
	unsigned long flags;

	spin_lock_irqsave(&alloc_lock, flags);
	for (i = 0; i < ARRAY_SIZE(wcnss_allocs); i++) {
		if (wcnss_allocs[i].ptr == ptr) {
			wcnss_allocs[i].occupied = 0;
			wcnss_allocs[i].time = 0;
			spin_unlock_irqrestore(&alloc_lock, flags);
			return 1;
		}
	}
	spin_unlock_irqrestore(&alloc_lock, flags);

	return 0;
}
EXPORT_SYMBOL(wcnss_prealloc_put);

struct sk_buff *wcnss_skb_prealloc_get(unsigned int size)
{
	int i = 0;
	unsigned long flags;

	spin_lock_irqsave(&alloc_lock, flags);
	for (i = 0; i < ARRAY_SIZE(wcnss_skb_allocs); i++) {
		if (wcnss_skb_allocs[i].occupied)
			continue;

		if (wcnss_skb_allocs[i].size > size) {
			/* we found the slot */
			wcnss_skb_allocs[i].occupied = 1;
			wcnss_skb_allocs[i].time = wcnss_get_time();
			spin_unlock_irqrestore(&alloc_lock, flags);
			wcnss_prealloc_save_stack_trace(&wcnss_allocs[i]);
			return wcnss_skb_allocs[i].ptr;
		}
	}
	spin_unlock_irqrestore(&alloc_lock, flags);

	pr_err("wcnss: %s: prealloc not available for size: %d\n",
			__func__, size);
#ifdef DEBUG
	printk("current time: %16lu\n", wcnss_get_time());
	for (i = 0; i < ARRAY_SIZE(wcnss_skb_allocs); i++) {
		if (wcnss_skb_allocs[i].occupied)
			printk("%02d: size %08d, time: %16lu\n", i, wcnss_skb_allocs[i].size, wcnss_skb_allocs[i].time);
		else
			printk("%02d: size %08d is free", i, wcnss_skb_allocs[i].size);
	}
#endif

	return NULL;
}
EXPORT_SYMBOL(wcnss_skb_prealloc_get);

int wcnss_skb_prealloc_put(struct sk_buff *skb)
{
	int i = 0;
	unsigned long flags;

	spin_lock_irqsave(&alloc_lock, flags);
	for (i = 0; i < ARRAY_SIZE(wcnss_skb_allocs); i++) {
		if (wcnss_skb_allocs[i].ptr == skb) {
			wcnss_skb_allocs[i].occupied = 0;
			wcnss_skb_allocs[i].time = 0;
			spin_unlock_irqrestore(&alloc_lock, flags);
			return 1;
		}
	}
	spin_unlock_irqrestore(&alloc_lock, flags);

	return 0;
}
EXPORT_SYMBOL(wcnss_skb_prealloc_put);

#ifdef CONFIG_SLUB_DEBUG_ON
void wcnss_prealloc_check_memory_leak(void)
{
	int i, j = 0;

	for (i = 0; i < ARRAY_SIZE(wcnss_allocs); i++) {
		if (!wcnss_allocs[i].occupied)
			continue;

		if (j == 0) {
			pr_err("wcnss_prealloc: Memory leak detected\n");
			j++;
		}

		pr_err("Size: %u, addr: %pK, backtrace:\n",
				wcnss_allocs[i].size, wcnss_allocs[i].ptr);
		print_stack_trace(&wcnss_allocs[i].trace, 1);
	}

}
#endif

int wcnss_pre_alloc_reset(void)
{
	int i, n = 0;

	for (i = 0; i < ARRAY_SIZE(wcnss_allocs); i++) {
		if (!wcnss_allocs[i].occupied)
			continue;

		wcnss_allocs[i].occupied = 0;
		n++;
	}

	return n;
}

