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

int decon_simple_notifier_call_chain(unsigned long val, int blank)
{
	struct fb_info *fbinfo = registered_fb[0];
	struct fb_event v = {0, };
	int fb_blank = blank;

	v.info = fbinfo;
	v.data = &fb_blank;

	return blocking_notifier_call_chain(&decon_notifier_list, val, &v);
}
EXPORT_SYMBOL(decon_simple_notifier_call_chain);

#define __XX(a)	#a,
const char *EVENT_NAME[] = { EVENT_LIST };
const char *STATE_NAME[] = { STATE_LIST };
#undef __XX

u32 EVENT_NAME_LEN;
u32 STATE_NAME_LEN;
u64 EVENT_TIME[STATE_MAX][EVENT_MAX];

static int decon_notifier_timestamp_early(struct notifier_block *this,
	unsigned long val, void *v)
{
	struct fb_event *evdata = NULL;
	int state = 0;

	switch (val) {
	case FB_EARLY_EVENT_BLANK:
	case DECON_EARLY_EVENT_DOZE:
		break;
	default:
		return NOTIFY_DONE;
	}

	evdata = v;

	if (!evdata || !evdata->info || evdata->info->node || !evdata->data)
		return NOTIFY_DONE;

	state = *(int *)evdata->data;

	EVENT_TIME[state][val] = local_clock();

	pr_info("decon: decon_notifier: blank_mode: %d, %02lx, + %-*s, %-*s\n",
		state, val, STATE_NAME_LEN, STATE_NAME[state], EVENT_NAME_LEN, EVENT_NAME[val]);

	return NOTIFY_DONE;
}

struct notifier_block decon_nb_priority_max = {
	.notifier_call = decon_notifier_timestamp_early,
	.priority = INT_MAX,
};

static int decon_notifier_timestamp_after(struct notifier_block *this,
	unsigned long val, void *v)
{
	struct fb_event *evdata = NULL;
	int state = 0;
	u64 early_delta = 0, after_delta = 0, frame_delta = 0;

	switch (val) {
	case FB_EVENT_BLANK:
	case DECON_EVENT_DOZE:
	case DECON_EVENT_FRAME:
	case DECON_EVENT_FRAME_SEND:
		break;
	default:
		return NOTIFY_DONE;
	}

	evdata = v;

	if (!evdata || !evdata->info || evdata->info->node || !evdata->data)
		return NOTIFY_DONE;

	state = *(int *)evdata->data;

	EVENT_TIME[state][val] = local_clock();

	early_delta = max(EVENT_TIME[state][FB_EARLY_EVENT_BLANK], EVENT_TIME[state][DECON_EARLY_EVENT_DOZE]);
	early_delta = div_u64(EVENT_TIME[state][val] - early_delta, NSEC_PER_MSEC);

	after_delta = max(EVENT_TIME[state][FB_EVENT_BLANK], EVENT_TIME[state][DECON_EVENT_DOZE]);
	after_delta = div_u64(EVENT_TIME[state][val] - after_delta, NSEC_PER_MSEC);

	frame_delta = max3(EVENT_TIME[state][FB_EVENT_BLANK], EVENT_TIME[state][DECON_EVENT_DOZE], EVENT_TIME[state][DECON_EVENT_FRAME]);
	frame_delta = div_u64(EVENT_TIME[state][val] - frame_delta, NSEC_PER_MSEC);

	if (val == DECON_EVENT_FRAME)
		pr_info("decon: decon_notifier: blank_mode: %d, %02lx, * %-*s, %-*s, %lld(%lld)\n",
			state, val, STATE_NAME_LEN, STATE_NAME[state], EVENT_NAME_LEN, EVENT_NAME[val], after_delta, early_delta);
	else if (val == DECON_EVENT_FRAME_SEND)
		pr_info("decon: decon_notifier: blank_mode: %d, %02lx, * %-*s, %-*s, %lld(%lld)\n",
			state, val, STATE_NAME_LEN, STATE_NAME[state], EVENT_NAME_LEN, EVENT_NAME[val], frame_delta, early_delta);
	else
		pr_info("decon: decon_notifier: blank_mode: %d, %02lx, - %-*s, %-*s, %lld\n",
			state, val, STATE_NAME_LEN, STATE_NAME[state], EVENT_NAME_LEN, EVENT_NAME[val], early_delta);

	return NOTIFY_DONE;
}

struct notifier_block decon_nb_priority_min = {
	.notifier_call = decon_notifier_timestamp_after,
	.priority = INT_MIN,
};

/* this is for fb_notifier_call_chain from outside */
static int decon_fb_notifier_event(struct notifier_block *this,
	unsigned long val, void *v)
{
	struct fb_event *evdata = NULL;
	int state = 0;

	switch (val) {
	case FB_EARLY_EVENT_BLANK:
	case FB_EVENT_BLANK:
		break;
	default:
		return NOTIFY_DONE;
	}

	evdata = v;

	if (!evdata || !evdata->info || evdata->info->node || !evdata->data)
		return NOTIFY_DONE;

	state = *(int *)evdata->data;

	if (val >= EVENT_MAX || state >= STATE_MAX) {
		pr_info("decon: invalid notifier info: %d, %02lx\n", state, val);
		return NOTIFY_DONE;
	}

	decon_notifier_call_chain(val, v);

	return NOTIFY_DONE;
}

static struct notifier_block decon_fb_notifier = {
	.notifier_call = decon_fb_notifier_event,
};

static void __exit decon_notifier_exit(void)
{
	decon_unregister_notifier(&decon_nb_priority_min);
	decon_unregister_notifier(&decon_nb_priority_max);

	fb_unregister_client(&decon_fb_notifier);
}

static int __init decon_notifier_init(void)
{
	EVENT_NAME_LEN = EVENT_NAME[FB_EARLY_EVENT_BLANK] ? min_t(size_t, MAX_INPUT, strlen(EVENT_NAME[FB_EARLY_EVENT_BLANK])) : EVENT_NAME_LEN;
	STATE_NAME_LEN = STATE_NAME[FB_BLANK_POWERDOWN] ? min_t(size_t, MAX_INPUT, strlen(STATE_NAME[FB_BLANK_POWERDOWN])) : STATE_NAME_LEN;

	fb_register_client(&decon_fb_notifier);

	decon_register_notifier(&decon_nb_priority_max);
	decon_register_notifier(&decon_nb_priority_min);

	return 0;
}

late_initcall(decon_notifier_init);
module_exit(decon_notifier_exit);

