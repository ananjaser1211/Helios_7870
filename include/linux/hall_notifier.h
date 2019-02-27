/*
 * include/linux/hall_notifier.h
 *
 * header file supporting Hall notifier call chain information
 *
 * Copyright (C) 2010 Samsung Electronics
 * Seung-Jin Hahn <sjin.hahn@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */
#ifndef __HALL_NOTIFIER_H__
#define __HALL_NOTIFIER_H__


#define HALL_CLOSE		0
#define HALL_OPEN		1
#define HALL_UNKNOWN	2

typedef enum {
	HALL_NOTIFY_DEV_GRIPSENSOR = 0,
} hall_notifier_device_t;

struct hall_notifier_struct {
	int hall_state;
	struct blocking_notifier_head notifier_call_chain;
};

#define HALL_NOTIFIER_BLOCK(name)	\
	struct notifier_block (name)

/* hall notifier init/notify function
 * this function is for JUST Hall device driver.
 * DON'T use function anywhere else!!
 */

/* hall notifier register/unregister API */
extern int hall_notifier_register(struct notifier_block *nb,
		notifier_fn_t notifier, hall_notifier_device_t listener);
extern int hall_notifier_unregister(struct notifier_block *nb);
extern void hall_notifier_hall_state(int hall_state);

#endif /* __HALL_NOTIFIER_H__ */
