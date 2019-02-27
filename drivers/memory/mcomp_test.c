/*
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS - Memory Compressor Unit Test
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/kthread.h>
#include <linux/interrupt.h>
#include <linux/syscalls.h>
#include "exynos-mcomp.h"

static int core;
static char file[255];
static char str[SZ_4K];
static char buf[SZ_4K];

static struct task_struct *task;

static int thread_func(void *data)
{
	size_t size = PAGE_SIZE;
	unsigned char *disk, *comp, *decomp;
	int comp_len;
	int i;
	int fd = -1;

	/* set string to disk buffer */
	if (str[0] == 0 && file[0] == 0) {
		pr_info("set string or file_name first\n");
		return 0;
	}

	if (file[0] != 0) {
		fd = sys_open(file, O_RDONLY, 0);
		if (fd < 0) {
			pr_info("file open failed\n");
			return 0;
		}
		if (sys_read(fd, buf, size) < 0) {
			pr_info("file open failed\n");
			sys_close(fd);
			return 0;
		}
		pr_info("file opened: %s\n", file);
	} else if (str[0] != 0) {
		memcpy(buf, str, size);
	}

	/* allocate buffers */
	disk = (unsigned char *)get_zeroed_page(GFP_KERNEL);
	comp = (unsigned char *)get_zeroed_page(GFP_KERNEL);
	decomp = (unsigned char *)get_zeroed_page(GFP_KERNEL);

	if (!disk || !comp || !decomp) {
		pr_info("page alloc fail\n");
		return 0;
	}

	/* original data */
	memcpy(disk, buf, size);

	/* compress */
	comp_len = mcomp_compress_page(0, disk, comp);

	if (comp_len == 0) {
		pr_info("comp_len failed\n");
		goto error_free;
	}

	if (comp_len == size) {
		pr_info("not compressed\n");
		pr_info("disk=%s\n", disk);
		pr_info("comp=%s\n", comp);
	} else {
		/* decompress */
		mcomp_decompress_page(comp, comp_len, decomp);

		/* verify original data */
		for (i = 0; i < size; i++) {
			if (disk[i] != decomp[i]) {
				pr_info("verification failed: disk[%d]=%d decomp[%d]=%d\n",
						i, disk[i], i, decomp[i]);
				goto error_free;
			}
		}
		pr_info("verification ok\n");
	}

error_free:
	if (fd >= 0)
		sys_close(fd);

	free_page((unsigned long)decomp);
	free_page((unsigned long)comp);
	free_page((unsigned long)disk);

	return 0;
}

/* file */
static ssize_t file_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	sscanf(buf, "%s", file);
	return count;
}

static ssize_t file_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	return sprintf(buf, "file = %s\n", file);
}

static struct kobj_attribute file_attribute = __ATTR(file, S_IWUSR|S_IRUGO, file_show, file_store);

/* str */
static ssize_t str_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	sscanf(buf, "%s", str);
	return count;
}

static ssize_t str_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	return sprintf(buf, "str = %s\n", str);
}

static struct kobj_attribute str_attribute = __ATTR(str, S_IWUSR|S_IRUGO, str_show, str_store);

/* core */
static ssize_t core_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	sscanf(buf, "%d", &core);

	task = kthread_create(thread_func, NULL, "thread%u", 0);
	kthread_bind(task, core);
	wake_up_process(task);

	return count;
}

static ssize_t core_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	return sprintf(buf, "core = %d\n", core);
}

static struct kobj_attribute core_attribute = __ATTR(core, S_IWUSR|S_IRUGO, core_show, core_store);

static struct attribute *attrs[] = {
	&core_attribute.attr,
	&str_attribute.attr,
	&file_attribute.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = attrs,
};

static struct kobject *core_kobj;

static int __init core_init(void)
{
	int ret = 0;

	core_kobj = kobject_create_and_add("mcomp_test", kernel_kobj);
	if (!core_kobj)
		return -ENOMEM;

	ret = sysfs_create_group(core_kobj, &attr_group);
	if (ret)
		kobject_put(core_kobj);

	return ret;
}

static void __exit core_exit(void)
{
	kobject_put(core_kobj);
}

module_init(core_init);
module_exit(core_exit);

MODULE_AUTHOR("Jungwook Kim");
MODULE_LICENSE("GPL");
