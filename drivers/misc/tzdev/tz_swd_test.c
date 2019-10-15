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

#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>

#include <tzdev/tzdev.h>

#include "sysdep.h"
#include "tzdev.h"
#include "tz_cdev.h"
#include "tz_common.h"
#include "tz_mem.h"
#include "tz_swd_test.h"

MODULE_AUTHOR("Alex Matveev <alex.matveev@samsung.com>");
MODULE_DESCRIPTION("Trustzone test driver for low-level SWd interface");
MODULE_LICENSE("GPL");

#define TZ_SWD_TEST_DEVICE_NAME "tz_swd_test"

struct tz_swd_test_mem {
	struct list_head list;
	unsigned int id;
	struct page *page;
};

static LIST_HEAD(tz_swd_test_mem_list);
static DEFINE_MUTEX(tz_swd_test_mem_mutex);

static struct tz_swd_test_mem *__tz_swd_test_mem_find(unsigned int id)
{
	struct tz_swd_test_mem *mem;

	list_for_each_entry(mem, &tz_swd_test_mem_list, list)
		if (mem->id == id)
			return mem;

	return NULL;
}

static void __tz_swd_test_mem_release(struct tz_swd_test_mem *mem)
{
	BUG_ON(tzdev_mem_release(mem->id));
	__free_page(mem->page);
	list_del(&mem->list);
	kfree(mem);
}

static int tz_swd_test_mem_register(unsigned int write)
{
	int ret, id;
	struct page *page;
	struct tz_swd_test_mem *mem;

	/* XXX: only a single page for now */
	page = alloc_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	memset(page_address(page), 0, PAGE_SIZE);

	mem = kmalloc(sizeof(struct tz_swd_test_mem), GFP_KERNEL);
	if (!mem) {
		ret = -ENOMEM;
		goto free_page;
	}

	mutex_lock(&tz_swd_test_mem_mutex);
	id = tzdev_mem_register(page_address(page), PAGE_SIZE, write);
	if (id < 0) {
		ret = id;
		goto free_mem;
	}

	mem->id = id;
	mem->page = page;

	list_add_tail(&mem->list, &tz_swd_test_mem_list);
	mutex_unlock(&tz_swd_test_mem_mutex);

	return id;

free_mem:
	mutex_unlock(&tz_swd_test_mem_mutex);
	kfree(mem);
free_page:
	__free_page(page);
	return ret;
}

static int tz_swd_test_mem_release(unsigned int id)
{
	int ret;
	struct tz_swd_test_mem *mem;

	mutex_lock(&tz_swd_test_mem_mutex);
	mem = __tz_swd_test_mem_find(id);
	if (!mem) {
		ret = -ENOENT;
		goto out;
	}

	__tz_swd_test_mem_release(mem);

	ret = 0;

out:
	mutex_unlock(&tz_swd_test_mem_mutex);
	return ret;
}

static int tz_swd_test_mem_read(unsigned int id, unsigned long ptr,
		unsigned long size)
{
	int ret;
	struct tz_swd_test_mem *mem;

	if (size > PAGE_SIZE)
		return -EINVAL;

	mutex_lock(&tz_swd_test_mem_mutex);
	mem = __tz_swd_test_mem_find(id);
	if (!mem) {
		ret = -ENOENT;
		goto out;
	}

	if (copy_to_user((void __user *)ptr, page_address(mem->page), size)) {
		ret = -EFAULT;
		goto out;
	}

	ret = 0;

out:
	mutex_unlock(&tz_swd_test_mem_mutex);
	return ret;
}

static int tz_swd_test_mem_write(unsigned int id, unsigned long ptr,
		unsigned long size)
{
	int ret;
	struct tz_swd_test_mem *mem;

	if (size > PAGE_SIZE)
		return -EINVAL;

	mutex_lock(&tz_swd_test_mem_mutex);
	mem = __tz_swd_test_mem_find(id);
	if (!mem) {
		ret = -ENOENT;
		goto out;
	}

	if (copy_from_user(page_address(mem->page), (void __user *)ptr, size)) {
		ret = -EFAULT;
		goto out;
	}

	ret = 0;

out:
	mutex_unlock(&tz_swd_test_mem_mutex);
	return ret;
}

static long tz_swd_test_ioctl(struct file *file, unsigned int cmd,
		unsigned long arg)
{
	switch (cmd) {
	case TZ_SWD_TEST_GET_EVENT: {
		struct tzio_smc_data __user *argp = (struct tzio_smc_data __user *)arg;
		struct tzio_smc_data s;

		s = tzdev_get_event();

		if (copy_to_user(argp, &s, sizeof(struct tzio_smc_data)))
			return -EFAULT;

		return 0;
	}
	case TZ_SWD_TEST_SEND_COMMAND: {
		struct tzio_smc_data __user *argp = (struct tzio_smc_data __user *)arg;
		struct tzio_smc_data s;

		if (copy_from_user(&s, argp, sizeof(struct tzio_smc_data)))
			return -EFAULT;

		s = tzdev_send_command(s.args[0], s.args[1]);

		if (copy_to_user(argp, &s, sizeof(struct tzio_smc_data)))
			return -EFAULT;

		return 0;
	}
	case TZ_SWD_TEST_MEM_REGISTER: {
		struct tzio_mem_register __user *argp = (struct tzio_mem_register __user *)arg;
		struct tzio_mem_register s;
		int ret;

		if (copy_from_user(&s, argp, sizeof(struct tzio_mem_register)))
			return -EFAULT;

		ret = tz_swd_test_mem_register(s.write);
		if (ret)
			return ret;

		if (copy_to_user(argp, &s, sizeof(struct tzio_mem_register)))
			return -EFAULT;

		return 0;
	}
	case TZ_SWD_TEST_MEM_RELEASE: {
		return tz_swd_test_mem_release(arg);
	}
	case TZ_SWD_TEST_MEM_READ: {
		struct tz_swd_test_mem_rw __user *argp = (struct tz_swd_test_mem_rw __user *)arg;
		struct tz_swd_test_mem_rw s;

		if (copy_from_user(&s, argp, sizeof(struct tz_swd_test_mem_rw)))
			return -EFAULT;

		return tz_swd_test_mem_read(s.id, s.ptr, s.size);
	}
	case TZ_SWD_TEST_MEM_WRITE: {
		struct tz_swd_test_mem_rw __user *argp = (struct tz_swd_test_mem_rw __user *)arg;
		struct tz_swd_test_mem_rw s;

		if (copy_from_user(&s, argp, sizeof(struct tz_swd_test_mem_rw)))
			return -EFAULT;

		return tz_swd_test_mem_write(s.id, s.ptr, s.size);
	}
	default:
		return -ENOTTY;
	}
}

static int tz_swd_test_open(struct inode *inode, struct file *filp)
{
	if (!tzdev_is_up())
		return -EPERM;

	return 0;
}

static const struct file_operations tz_swd_test_fops = {
	.owner = THIS_MODULE,
	.open = tz_swd_test_open,
	.unlocked_ioctl = tz_swd_test_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = tz_swd_test_ioctl,
#endif /* CONFIG_COMPAT */
};

static struct tz_cdev tz_swd_test_cdev = {
	.name = TZ_SWD_TEST_DEVICE_NAME,
	.fops = &tz_swd_test_fops,
	.owner = THIS_MODULE,
};

static int __init tz_swd_test_init(void)
{
	int rc;

	rc = tz_cdev_register(&tz_swd_test_cdev);

	return rc;
}

static void __exit tz_swd_test_exit(void)
{
	struct tz_swd_test_mem *mem, *tmp;

	list_for_each_entry_safe(mem, tmp, &tz_swd_test_mem_list, list)
		__tz_swd_test_mem_release(mem);

	tz_cdev_unregister(&tz_swd_test_cdev);
}

module_init(tz_swd_test_init);
module_exit(tz_swd_test_exit);
