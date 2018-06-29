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

#define pr_fmt(fmt) "tzpc_test: " fmt
#define DEBUG

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/mm.h>
#include <linux/clk.h>
#include <asm/io.h>

#include "tz_cdev.h"

#if defined(CONFIG_ARCH_EXYNOS)
#define MAP_ADDRESS	0x10830000
#define MAP_SIZE	0x1000
#define CLK_NAME	"secss"
#elif defined(CONFIG_ARCH_WHALE)
#define MAP_ADDRESS	0x10830000
#define MAP_SIZE	0x1000
#elif defined(CONFIG_ARCH_MT6755) || defined(CONFIG_ARCH_MT6735) || defined(CONFIG_MACH_MT6757)
#define MAP_ADDRESS	0x1100A000
#define MAP_SIZE	0x1000
#endif

#if defined(CONFIG_ARCH_EXYNOS)
static struct clk *clk;
#endif

static int tzpc_test_open(struct inode *inodp, struct file *filp)
{
#if defined(CONFIG_ARCH_EXYNOS)
	clk_enable(clk);
#endif
	return 0;
}

static int tzpc_test_release(struct inode *inodp, struct file *filp)
{
#if defined(CONFIG_ARCH_EXYNOS)
	clk_disable(clk);
#endif
	return 0;
}

static int tzpc_test_mmap(struct file *filp, struct vm_area_struct *vma)
{
	unsigned long off = vma->vm_pgoff << PAGE_SHIFT;
	unsigned long phys = MAP_ADDRESS + off;
	unsigned long vsize = vma->vm_end - vma->vm_start;
	unsigned long psize = MAP_SIZE - off;
	int ret;

	if (vsize > psize) {
		return -EINVAL;
	}

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	ret = remap_pfn_range(vma, vma->vm_start, __phys_to_pfn(phys), vsize, vma->vm_page_prot);
	if (ret) {
		goto out;
	}

	return 0;
out:
	return ret;
}

static struct file_operations tzpc_test_fops = {
	.open = tzpc_test_open,
	.release = tzpc_test_release,
	.mmap = tzpc_test_mmap,
};

static struct tz_cdev tzpc_test_cdev = {
	.name = "tzpc_test",
	.fops = &tzpc_test_fops,
	.owner = THIS_MODULE,
};

static int __init tzpc_test_init(void)
{
	int rc;

	pr_devel("module init\n");

#if defined(CONFIG_ARCH_EXYNOS)
	clk = clk_get(NULL, CLK_NAME);
	if (IS_ERR(clk)) {
		pr_err("Clock get failed\n");
		return -ENOENT;
	}
#endif
	rc = tz_cdev_register(&tzpc_test_cdev);
	if (rc)
		pr_err("char device registration failed\n");

	return rc;
}

static void __exit tzpc_test_exit(void)
{
	pr_devel("module exit\n");
	tz_cdev_unregister(&tzpc_test_cdev);
}

module_init(tzpc_test_init);
module_exit(tzpc_test_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sergey Fedorov <s.fedorov@samsung.com>");
