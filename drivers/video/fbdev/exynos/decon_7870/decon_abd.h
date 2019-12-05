/*
 * Copyright (c) Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __DECON_ABD_H__
#define __DECON_ABD_H__

#define ABD_EVENT_LOG_MAX	50
#define ABD_LOG_MAX		10

struct abd_event_log {
	u64 stamp;
	const char *print;
};

struct abd_event {
	struct abd_event_log log[ABD_EVENT_LOG_MAX];
	atomic_t log_idx;
};

struct abd_log {
	u64 stamp;

	/* pin */
	unsigned int level;
	unsigned int state;
	unsigned int onoff;

	/* fence */
	unsigned int winid;
	struct sync_fence fence;

	/* bit error */
	unsigned int size;
	unsigned int value;
	char *print[32];	/* max: 32 bit */
};

struct abd_trace {
	const char *name;
	unsigned int count;
	unsigned int lcdon_flag;
	struct abd_log log[ABD_LOG_MAX];
};

struct adb_pin_handler {
	struct list_head node;
	irq_handler_t handler;
	void *dev_id;
};

struct abd_pin {
	const char *name;
	unsigned int irq;
	struct irq_desc *desc;
	int gpio;
	int level;
	int active_level;

	struct abd_trace p_first;
	struct abd_trace p_lcdon;
	struct abd_trace p_event;

	struct list_head handler_list;
};

enum {
	ABD_PIN_PCD,
	ABD_PIN_DET,
	ABD_PIN_ERR,
	ABD_PIN_MAX
};

struct abd_protect {
	struct abd_pin pin[ABD_PIN_MAX];
	struct abd_event event;

	struct abd_trace f_first;
	struct abd_trace f_lcdon;
	struct abd_trace f_event;

	struct abd_trace u_first;
	struct abd_trace u_lcdon;
	struct abd_trace u_event;

	struct abd_trace b_first;
	struct abd_trace b_event;

	struct notifier_block pin_early_notifier;
	struct notifier_block pin_after_notifier;

	unsigned int irq_enable;
	struct notifier_block reboot_notifier;
	spinlock_t slock;
};

extern unsigned int lcdtype;

struct decon_device;
extern void decon_abd_save_fto(struct abd_protect *abd, struct sync_fence *fence);
extern void decon_abd_save_str(struct abd_protect *abd, const char *print);
extern void decon_abd_save_bit(struct abd_protect *abd, unsigned int size, unsigned int value, char **print);
extern int decon_abd_pin_register_handler(int irq, irq_handler_t handler, void *dev_id);
#endif

