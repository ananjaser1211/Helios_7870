/*****************************************************************************
 *
 * Copyright (c) 2014 - 2016 Samsung Electronics Co., Ltd. and its Licensors.
 * All rights reserved.
 *
 ****************************************************************************/

#ifndef __SLSI_WAKELOCK_H__
#define __SLSI_WAKELOCK_H__

#ifdef CONFIG_WAKELOCK
#include <linux/wakelock.h>
#endif
#include <linux/spinlock.h>

struct slsi_wake_lock {
#ifdef CONFIG_WAKELOCK
	struct wake_lock wl;
#endif
	/* Spinlock to synchronize the access of the counter */
	spinlock_t       wl_spinlock;
	int              counter;
};

void slsi_wakelock(struct slsi_wake_lock *lock);
void slsi_wakeunlock(struct slsi_wake_lock *lock);
void slsi_wakelock_timeout(struct slsi_wake_lock *lock, int timeout);
int slsi_is_wakelock_active(struct slsi_wake_lock *lock);
void slsi_wakelock_exit(struct slsi_wake_lock *lock);
void slsi_wakelock_init(struct slsi_wake_lock *lock, char *name);
#endif
