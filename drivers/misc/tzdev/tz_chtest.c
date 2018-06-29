/*
 * Copyright (C) 2016 Samsung Electronics, Inc.
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

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/printk.h>
#include <linux/uaccess.h>

#include "tz_cdev.h"
#include "tzdev.h"
#include "tz_chtest.h"

MODULE_LICENSE("GPL");

#ifdef CUSTOM_HANDLER_DEBUG
#define LOG_DBG(...)		pr_info( "[tz_chtest] DBG : " __VA_ARGS__)
#else
#define LOG_DBG(...)
#endif /* CUSTOM_HANDLER_DEBUG */
#define LOG_ERR(...)		pr_alert("[tz_chtest] ERR : " __VA_ARGS__)

static int tz_chtest_fd_open = 0;
static DEFINE_MUTEX(tz_chtest_fd_lock);

static int tz_chtest_drv_open(struct inode *n, struct file *f)
{
	int ret = 0;

	mutex_lock(&tz_chtest_fd_lock);
	if (tz_chtest_fd_open) {
		ret = -EBUSY;
		goto out;
	}
	tz_chtest_fd_open++;
	LOG_DBG("opened\n");

out:
	mutex_unlock(&tz_chtest_fd_lock);
	return ret;
}

static inline int tz_chtest_drv_release(struct inode *inode, struct file *file)
{
	mutex_lock(&tz_chtest_fd_lock);
	if (tz_chtest_fd_open)
		tz_chtest_fd_open--;
	mutex_unlock(&tz_chtest_fd_lock);
	LOG_DBG("released\n");
	return 0;
}

static long tz_chtest_drv_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	struct tzio_smc_data tzdev_smc_cmd;
	void __user *ioargp = (void __user *) arg;
	struct tz_chtest_data chtest_data;

	switch (cmd) {
	case IOCTL_CUSTOM_HANDLER_CMD:
		LOG_DBG("IOCTL_CUSTOM_HANDLER_CMD\n");
		ret = copy_from_user(&chtest_data, ioargp, sizeof(chtest_data));
		if (ret != 0) {
			LOG_ERR("copy_from_user failed, ret = 0x%08x\n", ret);
			return -EFAULT;
		}

		if (chtest_data.r0 < CUSTOM_HANDLER_CMD_BASE_RAW ||
				chtest_data.r0 >= (CUSTOM_HANDLER_EXEC_BASE_RAW	+ CUSTOM_HANDLER_COUNT)) {
			LOG_ERR("FID number is out of range\n");
			return -EFAULT;
		}

		/* Add FID base to produce the final SCM FID number */
		chtest_data.r0 |= CUSTOM_HANDLER_FID_BASE;

		tzdev_smc_cmd.args[0] = chtest_data.r0;
		tzdev_smc_cmd.args[1] = chtest_data.r1;
		tzdev_smc_cmd.args[2] = chtest_data.r2;
		tzdev_smc_cmd.args[3] = chtest_data.r3;

		LOG_DBG("Regs before : r0 = 0x%08x, r1, = 0x%08x, r2 = 0x%08x, r3 = 0x%08x\n",
			chtest_data.r0, chtest_data.r1, chtest_data.r2, chtest_data.r3);

		ret = __tzdev_smc_cmd(&tzdev_smc_cmd, 0);
		if (!ret) {
			chtest_data.r0 = tzdev_smc_cmd.args[0];
			chtest_data.r1 = tzdev_smc_cmd.args[1];
			chtest_data.r2 = tzdev_smc_cmd.args[2];
			chtest_data.r3 = tzdev_smc_cmd.args[3];
		} else {
			LOG_ERR("__tzdev_smc_cmd() failed, ret = 0x%08x\n", ret);
			return -EFAULT;
		}

		LOG_DBG("Regs after  : r0 = 0x%08x, r1, = 0x%08x, r2 = 0x%08x, r3 = 0x%08x\n",
			chtest_data.r0, chtest_data.r1, chtest_data.r2, chtest_data.r3);

		ret = copy_to_user(ioargp, &chtest_data, sizeof(chtest_data));
		if (ret != 0) {
			LOG_ERR("copy_to_user failed, ret = 0x%08x\n", ret);
			return -EFAULT;
		}
		break;

	default:
		LOG_ERR("UNKNOWN CMD, cmd = 0x%08x\n", cmd);
		return -ENOTTY;
	}

	return 0;
}

static const struct file_operations misc_fops = {
	.owner = THIS_MODULE,
	.open = tz_chtest_drv_open,
	.unlocked_ioctl = tz_chtest_drv_unlocked_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = tz_chtest_drv_unlocked_ioctl,
#endif /* CONFIG_COMPAT */
	.release = tz_chtest_drv_release,
};

static struct tz_cdev tz_chtest_cdev = {
	.name = CUSTOM_HANDLER_TEST_NAME,
	.fops = &misc_fops,
	.owner = THIS_MODULE,
};

static int __init tz_chtest_drv_init(void)
{
	int ret;

	ret = tz_cdev_register(&tz_chtest_cdev);
	if (ret) {
		LOG_ERR("Unable to register tz_chtest driver, ret = 0x%08x\n", ret);
		return ret;
	}

	LOG_DBG("INSTALLED\n");
	return 0;
}

static void __exit tz_chtest_drv_exit(void)
{
	tz_cdev_unregister(&tz_chtest_cdev);
	LOG_DBG("REMOVED\n");
}

module_init(tz_chtest_drv_init);
module_exit(tz_chtest_drv_exit);
