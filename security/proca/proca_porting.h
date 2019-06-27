/*
 * This is needed backporting of source code from Kernel version 4.x
 *
 * Copyright (C) 2018 Samsung Electronics, Inc.
 *
 * Hryhorii Tur, <hryhorii.tur@partner.samsung.com>
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

#ifndef __LINUX_PROCA_PORTING_H
#define __LINUX_PROCA_PORTING_H

#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0)

static inline struct inode *locks_inode(const struct file *f)
{
	return f->f_path.dentry->d_inode;
}

static inline ssize_t
__vfs_getxattr(struct dentry *dentry, struct inode *inode, const char *name,
	       void *value, size_t size)
{
	if (inode->i_op->getxattr)
		return inode->i_op->getxattr(dentry, name, value, size);
	else
		return -EOPNOTSUPP;
}

#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 21)
/* d_backing_inode is absent on some Linux Kernel 3.x. but it back porting for
 * few Samsung kernels:
 * Exynos7570 (3.18.14): CL 13680422
 * Exynos7870 (3.18.14): CL 14632149
 * SDM450 (3.18.71): initially
 */
#if !defined(CONFIG_SOC_EXYNOS7570) && !defined(CONFIG_ARCH_SDM450) && \
	!defined(CONFIG_SOC_EXYNOS7870)
#define d_backing_inode(dentry)	((dentry)->d_inode)
#endif
#define inode_lock(inode)	mutex_lock(&(inode)->i_mutex)
#define inode_unlock(inode)	mutex_unlock(&(inode)->i_mutex)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 2, 0)
#define security_add_hooks(hooks, count, name)
#else
#define LINUX_LSM_SUPPORTED
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0)
#define security_add_hooks(hooks, count, name) security_add_hooks(hooks, count)
#endif
#include <linux/lsm_hooks.h>
#endif

#endif /* __LINUX_PROCA_PORTING_H */
