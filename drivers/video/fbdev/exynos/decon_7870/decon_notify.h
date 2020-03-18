/*
 * Copyright (c) Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __DECON_NOTIFY_H__
#define __DECON_NOTIFY_H__

#include <linux/notifier.h>

#define EVENT_LIST	\
__XX(EVENT_NONE)	\
__XX(EVENT_MODE_CHANGE)	\
__XX(EVENT_SUSPEND)	\
__XX(EVENT_RESUME)	\
__XX(EVENT_MODE_DELETE)	\
__XX(EVENT_FB_REGISTERED)	\
__XX(EVENT_FB_UNREGISTERED)	\
__XX(EVENT_GET_CONSOLE_MAP)	\
__XX(EVENT_SET_CONSOLE_MAP)	\
__XX(EVENT_BLANK)	\
__XX(EVENT_NEW_MODELIST)	\
__XX(EVENT_MODE_CHANGE_ALL)	\
__XX(EVENT_CONBLANK)	\
__XX(EVENT_GET_REQ)	\
__XX(EVENT_FB_UNBIND)	\
__XX(EVENT_REMAP_ALL_CONSOLE)	\
__XX(EARLY_EVENT_BLANK)	\
__XX(R_EARLY_EVENT_BLANK)	\
__XX(EVENT_FB_MAX)	\
__XX(EVENT_DOZE)	\
__XX(EARLY_EVENT_DOZE)	\
__XX(EVENT_FRAME)	\
__XX(EVENT_FRAME_SEND)	\

#define STATE_LIST	\
__XX(UNBLANK)	\
__XX(NORMAL)	\
__XX(VSYNC_SUSPEND)	\
__XX(HSYNC_SUSPEND)	\
__XX(POWERDOWN)	\

#define __XX(a)	DECON_##a,
enum {	EVENT_LIST	EVENT_MAX	};
enum {	STATE_LIST	STATE_MAX	};
#undef __XX

#define IS_EARLY(event)		(event == FB_EARLY_EVENT_BLANK || event == DECON_EARLY_EVENT_DOZE)
#define IS_AFTER(event)		(event == FB_EVENT_BLANK || event == DECON_EVENT_DOZE)

extern struct notifier_block decon_nb_priority_max;
extern struct notifier_block decon_nb_priority_min;

extern int decon_register_notifier(struct notifier_block *nb);
extern int decon_unregister_notifier(struct notifier_block *nb);
extern int decon_notifier_call_chain(unsigned long val, void *v);
extern int decon_simple_notifier_call_chain(unsigned long val, int blank);

#endif

