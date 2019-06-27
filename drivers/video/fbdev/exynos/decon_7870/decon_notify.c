/*
 * Copyright (c) Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/fb.h>
#include <linux/export.h>
#include <linux/module.h>

#include "decon_notify.h"

static BLOCKING_NOTIFIER_HEAD(decon_notifier_list);

int decon_register_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&decon_notifier_list, nb);
}
EXPORT_SYMBOL(decon_register_notifier);

int decon_unregister_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&decon_notifier_list, nb);
}
EXPORT_SYMBOL(decon_unregister_notifier);

int decon_notifier_call_chain(unsigned long val, void *v)
{
	return blocking_notifier_call_chain(&decon_notifier_list, val, v);
}
EXPORT_SYMBOL(decon_notifier_call_chain);

#define __XX(a)	#a,
const char *EVENT_NAME[] = { EVENT_LIST };
const char *STATE_NAME[] = { STATE_LIST };
#undef __XX

static ktime_t decon_ktime;

static int decon_notifier_event_time(struct notifier_block *this,
	unsigned long val, void *v)
{
	struct fb_event *evdata = NULL;
	int blank = 0;

	switch (val) {
	case FB_EVENT_BLANK:
		break;
	default:
		return NOTIFY_DONE;
	}

	evdata = v;

	blank = *(int *)evdata->data;

	pr_info("decon: decon_notifier_event: blank_mode: %d, %02lx, - %s, %s, %lld\n", blank, val, STATE_NAME[blank], EVENT_NAME[val], ktime_to_ms(ktime_sub(ktime_get(), decon_ktime)));

	return NOTIFY_DONE;
}

static struct notifier_block decon_time_notifier = {
	.notifier_call = decon_notifier_event_time,
	.priority = -1,
};

static int decon_notifier_event(struct notifier_block *this,
	unsigned long val, void *v)
{
	struct fb_event *evdata = NULL;
	int blank = 0;

	switch (val) {
	case FB_EVENT_BLANK:
	case FB_EARLY_EVENT_BLANK:
	case DECON_EVENT_DOZE:
		break;
	default:
		return NOTIFY_DONE;
	}

	evdata = v;

	if (evdata && evdata->info && evdata->info->node)
		return NOTIFY_DONE;

	if (val == FB_EARLY_EVENT_BLANK)
		decon_ktime = ktime_get();

	blank = *(int *)evdata->data;

	if (blank >= STATE_MAX || val >= EVENT_MAX) {
		pr_info("decon: invalid notifier info: %d, %02lx\n", blank, val);
		return NOTIFY_DONE;
	}

	if (val == FB_EARLY_EVENT_BLANK)
		pr_info("decon: decon_notifier_event: blank_mode: %d, %02lx, + %s, %s\n", blank, val, STATE_NAME[blank], EVENT_NAME[val]);
	else if (val == FB_EVENT_BLANK)
		pr_info("decon: decon_notifier_event: blank_mode: %d, %02lx, ~ %s, %s\n", blank, val, STATE_NAME[blank], EVENT_NAME[val]);

	decon_notifier_call_chain(val, v);

	return NOTIFY_DONE;
}

static struct notifier_block decon_fb_notifier = {
	.notifier_call = decon_notifier_event,
};

static void __exit decon_notifier_exit(void)
{
	decon_unregister_notifier(&decon_time_notifier);

	fb_unregister_client(&decon_fb_notifier);
}

static int __init decon_notifier_init(void)
{
	fb_register_client(&decon_fb_notifier);

	decon_register_notifier(&decon_time_notifier);

	return 0;
}

late_initcall(decon_notifier_init);
module_exit(decon_notifier_exit);

