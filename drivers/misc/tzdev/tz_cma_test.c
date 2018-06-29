/*
 * Copyright (C) 2015 Samsung Electronics, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/dma-contiguous.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/sched.h>
#include <linux/spinlock.h>

#include "tz_cdev.h"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CMA migration test helper module");

struct mem_info {
	__u64 addr;
	__u32 size;
	__u32 nr_cma_pages;
};

#define TZDEV_CMA_CALC		_IOWR('c', 1, struct mem_info)

static int calc_cma_pages(__u64 vaddr, __u32 size, __u32 *res)
{
	unsigned long start, end;
	__u32 nr_pages, i;
	__u32 nr_cma_pages = 0, nr_pinned = 0;
	struct page **pages, **pages_temp;
	int ret;
	unsigned long migrate_type;
	struct mm_struct *mm = current->mm;
	struct zone *zone;
	unsigned long flags;

	start = vaddr >> PAGE_SHIFT;
	end = (vaddr + size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	nr_pages = end - start;

	pages = kcalloc(nr_pages, sizeof(struct page *), GFP_KERNEL);
	if (!pages) {
		printk(KERN_ERR "tzdev_cma_test: kcalloc() failed\n");
		return -ENOMEM;
	}

	/* Get user pages */
	down_read(&mm->mmap_sem);
	pages_temp = pages;
	while (nr_pinned < nr_pages) {
		ret = get_user_pages(current, mm, vaddr, nr_pages - nr_pinned,
				1, 0, pages_temp, NULL);
		if (ret < 0) {
			up_write(&mm->mmap_sem);
			printk(KERN_ERR "tzdev_cma_test: get_user_pages() failed\n");
			goto err_pin;
		}
		nr_pinned += ret;
		pages_temp += ret;
		vaddr += ret * PAGE_SIZE;

	}
	up_read(&mm->mmap_sem);

	/* Calc number of CMA pages */
	for (i = 0; i < nr_pinned; i++) {
		/* Zone lock must be held to avoid race with
		 * set_pageblock_migratetype() */
		zone = page_zone(pages[i]);
		spin_lock_irqsave(&zone->lock, flags);
		migrate_type = get_pageblock_migratetype(pages[i]);
		spin_unlock_irqrestore(&zone->lock, flags);
		if (migrate_type == MIGRATE_CMA)
			nr_cma_pages++;
	}

	*res = nr_cma_pages;
	ret = 0;

err_pin:
	for (i = 0; i < nr_pinned; i++)
		put_page(pages[i]);

	kfree(pages);

	return ret;
}

long cma_test_ioctl(struct file *file, unsigned int cmd, unsigned long param)
{
	long ret = 0;

	switch (cmd) {
	case TZDEV_CMA_CALC:
	{
		struct mem_info __user *p = (struct mem_info __user *)param;
		struct mem_info mem;
		if (copy_from_user(&mem, p, sizeof(mem))) {
			ret = -EFAULT;
			printk(KERN_ERR "tzdev_cma_test: copy_from_user() failed\n");
			break;
		}

		if (calc_cma_pages(mem.addr, mem.size, &mem.nr_cma_pages)) {
			ret = -EFAULT;
			break;
		}

		if (copy_to_user(p, &mem, sizeof(mem))) {
			ret = -EFAULT;
			printk(KERN_ERR "tzdev_cma_test: copy_to_user() failed\n");
			break;
		}
		break;
	}
	default:
		printk(KERN_ERR "tzdev_cma_test: unknown ioctl command (0x%x)\n", cmd);
		ret = -EFAULT;
		break;
	}

	return ret;
}

static const struct file_operations cma_test_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= cma_test_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= cma_test_ioctl,
#endif /* CONFIG_COMPAT */
};

static struct tz_cdev tzdev_cma_test_cdev = {
	.name = "tzdev_cma_test",
	.fops = &cma_test_fops,
	.owner = THIS_MODULE,
};

static int __init init_cma_test(void)
{
	int ret;

	printk(KERN_INFO "tzdev_cma_test: init_cma_test\n");

	ret = tz_cdev_register(&tzdev_cma_test_cdev);
	if (ret) {
		printk(KERN_ERR "tzdev_cma_test: failed to register cma test misc device!\n");
		return ret;
	}

	return 0;
}

static void __exit cleanup_cma_test(void)
{
	tz_cdev_unregister(&tzdev_cma_test_cdev);
	printk(KERN_INFO "tzdev_cma_test: cleanup_cma_test\n");
}

module_init(init_cma_test);
module_exit(cleanup_cma_test);
