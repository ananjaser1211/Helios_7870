/*
 * Copyright (C) 2012-2017 Samsung Electronics, Inc.
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

#include <linux/clk.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include "sysdep.h"
#include "tz_cdev.h"
#include "tzlog.h"

static DEFINE_MUTEX(tz_clk_test_helper_mutex);
static char tz_clk_test_helper_buf[PAGE_SIZE];

static int tz_clk_test_helper_open(struct inode *inode, struct file *file)
{
	(void)inode;

	file->private_data = NULL;

	return 0;
}

static int tz_clk_test_helper_release(struct inode *inode, struct file *file)
{
	struct clk *clk = file->private_data;

	(void)inode;

	if (clk) {
		clk_disable(clk);
		clk_put(clk);
	}

	return 0;
}

static ssize_t tz_clk_test_helper_write(struct file *file, const char __user *buf,
		size_t count, loff_t *pos)
{
	struct device_node *node;
	struct clk *clk;
	ssize_t nbytes;
	ssize_t ret;
	char *fdt_path;
	char *fdt_clock;
	char *p;

	(void)pos;

	if (count >= sizeof(tz_clk_test_helper_buf))
		return -ENOSPC;

	mutex_lock(&tz_clk_test_helper_mutex);

	if (file->private_data) {
		ret = -EBUSY;
		goto out;
	}

	nbytes = strncpy_from_user(tz_clk_test_helper_buf, buf, count);

	ret = nbytes;
	if (ret <= 0)
		goto out;

	tz_clk_test_helper_buf[ret] = '\0';

	p = memchr(tz_clk_test_helper_buf, ':', ret);
	if (!p) {
		ret = -EINVAL;
		goto out;
	}

	fdt_path = tz_clk_test_helper_buf;
	fdt_clock = p + 1;
	*p = '\0';

	node = of_find_node_by_path(fdt_path);
	if (!node) {
		tzdev_print(0, "of_find_node_by_path(%s) failed\n", fdt_path);
		ret = -ENODEV;
		goto out;
	}

	clk = of_clk_get_by_name(node, fdt_clock);
	if (IS_ERR(clk)) {
		tzdev_print(0, "of_clk_get_by_name(%s) failed, ret=%ld\n",
				fdt_clock, PTR_ERR(clk));
		ret = PTR_ERR(clk);
		goto out_node_put;
	}

	ret = clk_enable(clk);
	if (ret) {
		clk_put(clk);
		goto out_node_put;
	}

	file->private_data = clk;
	ret = nbytes;

out_node_put:
	of_node_put(node);
out:
	mutex_unlock(&tz_clk_test_helper_mutex);

	return ret;
}

static const struct file_operations tz_clk_test_helper_fops = {
	.owner = THIS_MODULE,
	.open = tz_clk_test_helper_open,
	.release = tz_clk_test_helper_release,
	.write = tz_clk_test_helper_write
};

static struct tz_cdev tz_clk_test_helper_cdev = {
	.name = "tz_clk_test_helper",
	.fops = &tz_clk_test_helper_fops,
	.owner = THIS_MODULE,
};

static int __init tz_clk_test_helper_init(void)
{
	int ret;

	ret = tz_cdev_register(&tz_clk_test_helper_cdev);
	if (ret)
		pr_err("failed to register tz_clk_test_helper device, ret=%d\n", ret);

	return ret;
}

static void __exit tz_clk_test_helper_exit(void)
{
	tz_cdev_unregister(&tz_clk_test_helper_cdev);
}

module_init(tz_clk_test_helper_init);
module_exit(tz_clk_test_helper_exit);
