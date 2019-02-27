/*
 * Copyright (C) 2012-2016 Samsung Electronics, Inc.
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

#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/printk.h>
#include <linux/uaccess.h>

#include "tz_cdev.h"

#define TZ_PRINTK_TEST_PREFIX		KERN_DEFAULT "SW> "

static DEFINE_MUTEX(tz_printk_test_mutex);
static char tz_printk_test_buf[PAGE_SIZE];
static unsigned int tz_printk_test_iterations = 100;

static ssize_t tz_printk_test_iterations_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", tz_printk_test_iterations);
}

static ssize_t tz_printk_test_iterations_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int new;
	int ret;

	ret = kstrtouint(buf, 0, &new);
	if (ret)
		return ret;

	if (!new)
		return -EINVAL;

	mutex_lock(&tz_printk_test_mutex);
	tz_printk_test_iterations = new;
	mutex_unlock(&tz_printk_test_mutex);

	return count;
}

static DEVICE_ATTR(iterations, S_IWUSR | S_IRUSR | S_IRGRP,
		tz_printk_test_iterations_show,
		tz_printk_test_iterations_store);

static ssize_t tz_printk_test_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *pos)
{
	unsigned int i;
	ssize_t nbytes;

	(void) filp;
	(void) pos;

	if (count >= sizeof(tz_printk_test_buf))
		return -ENOSPC;

	mutex_lock(&tz_printk_test_mutex);

	nbytes = strncpy_from_user(tz_printk_test_buf, buf, count);
	if (nbytes <= 0)
		goto out;

	tz_printk_test_buf[nbytes] = '\0';
	for (i = 0; i < tz_printk_test_iterations; i++)
		printk(TZ_PRINTK_TEST_PREFIX "%s\n", tz_printk_test_buf);

out:
	mutex_unlock(&tz_printk_test_mutex);
	return nbytes;
}

static const struct file_operations tz_printk_test_fops = {
	.owner = THIS_MODULE,
	.write = tz_printk_test_write
};

static struct tz_cdev tz_printk_test_cdev = {
	.owner = THIS_MODULE,
	.name = "tz_printk_test",
	.fops = &tz_printk_test_fops,
};

static int __init tz_printk_test_init(void)
{
	int rc;

	rc = tz_cdev_register(&tz_printk_test_cdev);
	if (rc) {
		pr_err("failed to register tz_printk_test char device\n");
		return rc;
	}

	rc = device_create_file(tz_printk_test_cdev.device, &dev_attr_iterations);
	if (rc) {
		pr_err("iterations sysfs file creation failed\n");
		goto fail;
	}

	return 0;

fail:
	tz_cdev_unregister(&tz_printk_test_cdev);
	return rc;
}

static void __exit tz_printk_test_exit(void)
{
	tz_cdev_unregister(&tz_printk_test_cdev);
}

module_init(tz_printk_test_init);
module_exit(tz_printk_test_exit);

MODULE_AUTHOR("Oleg Latin <o.latin@partner.samsung.com>");
MODULE_LICENSE("GPL");
