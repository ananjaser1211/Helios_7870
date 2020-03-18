/*
 * Copyright (c) Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/clk-provider.h>
#include <linux/pm_runtime.h>
#include <linux/exynos_iovmm.h>
#include <linux/of_address.h>
#include <linux/pinctrl/consumer.h>
#include <linux/irq.h>
#include <media/v4l2-subdev.h>
#include <linux/reboot.h>
#include <linux/debugfs.h>

#include "../../../../../kernel/irq/internals.h"
#include "decon.h"
#include "decon_board.h"
#include "decon_notify.h"
#include "dsim.h"

#define abd_printf(m, ...)	\
{	if (m) seq_printf(m, __VA_ARGS__); else decon_info(__VA_ARGS__);	}	\

void decon_abd_save_str(struct abd_protect *abd, const char *print)
{
	unsigned int idx = atomic_inc_return(&abd->event.log_idx) % ABD_EVENT_LOG_MAX;

	abd->event.log[idx].stamp = local_clock();
	abd->event.log[idx].print = print;
}

static void _decon_abd_print_bit(struct seq_file *m, struct abd_log *log)
{
	struct timeval tv;
	unsigned int bit = 0;
	char print_buf[200] = {0, };
	struct seq_file p = {
		.buf = print_buf,
		.size = sizeof(print_buf) - 1,
	};

	if (!m)
		seq_puts(&p, "decon_abd: ");

	tv = ns_to_timeval(log->stamp);
	seq_printf(&p, "time: %lu.%06lu, 0x%0*X, ", (unsigned long)tv.tv_sec, tv.tv_usec, log->size >> 2, log->value);

	for (bit = 0; bit < log->size; bit++) {
		if (log->print[bit]) {
			if (!bit || !log->print[bit - 1] || strcmp(log->print[bit - 1], log->print[bit]))
				seq_printf(&p, "%s, ", log->print[bit]);
		}
	}

	abd_printf(m, "%s\n", p.count ? p.buf : "");
}

void decon_abd_save_bit(struct abd_protect *abd, unsigned int size, unsigned int value, char **print)
{
	struct abd_trace *first = &abd->b_first;
	struct abd_trace *event = &abd->b_event;

	struct abd_log *first_log = &first->log[(first->count % ABD_LOG_MAX)];
	struct abd_log *event_log = &event->log[(event->count % ABD_LOG_MAX)];

	memset(event_log, 0, sizeof(struct abd_log));
	event_log->stamp = local_clock();
	event_log->value = value;
	event_log->size = size;
	memcpy(&event_log->print, print, sizeof(char *) * size);

	if (!first->count) {
		memset(first_log, 0, sizeof(struct abd_log));
		memcpy(first_log, event_log, sizeof(struct abd_log));
		first->count++;
	}

	_decon_abd_print_bit(NULL, event_log);

	event->count++;
}

void decon_abd_save_fto(struct abd_protect *abd, struct sync_fence *fence)
{
	struct abd_trace *first = &abd->f_first;
	struct abd_trace *lcdon = &abd->f_lcdon;
	struct abd_trace *event = &abd->f_event;

	struct abd_log *first_log = &first->log[(first->count % ABD_LOG_MAX)];
	struct abd_log *lcdon_log = &lcdon->log[(lcdon->count % ABD_LOG_MAX)];
	struct abd_log *event_log = &event->log[(event->count % ABD_LOG_MAX)];

	memset(event_log, 0, sizeof(struct abd_log));
	event_log->stamp = local_clock();
	memcpy(&event_log->fence, fence, sizeof(struct sync_fence));

	if (!first->count) {
		memset(first_log, 0, sizeof(struct abd_log));
		memcpy(first_log, event_log, sizeof(struct abd_log));
		first->count++;
	}

	if (!lcdon->lcdon_flag) {
		memset(lcdon_log, 0, sizeof(struct abd_log));
		memcpy(lcdon_log, event_log, sizeof(struct abd_log));
		lcdon->count++;
		lcdon->lcdon_flag++;
	}

	event->count++;
}

static void decon_abd_save_pin(struct decon_device *decon, struct abd_pin *pin, struct abd_trace *trace, bool on)
{
	struct abd_trace *first = &pin->p_first;

	struct abd_log *first_log = &first->log[(first->count) % ABD_LOG_MAX];
	struct abd_log *trace_log = &trace->log[(trace->count) % ABD_LOG_MAX];

	trace_log->stamp = local_clock();
	trace_log->level = pin->level;
	trace_log->state = decon->state;
	trace_log->onoff = on;

	if (!first->count) {
		memset(first_log, 0, sizeof(struct abd_log));
		memcpy(first_log, trace_log, sizeof(struct abd_log));
		first->count++;
	}

	trace->count++;
}

static void decon_abd_pin_clear_pending_bit(int irq)
{
	struct irq_desc *desc;

	desc = irq_to_desc(irq);
	if (desc->irq_data.chip->irq_ack) {
		desc->irq_data.chip->irq_ack(&desc->irq_data);
		desc->istate &= ~IRQS_PENDING;
	}
}

static void decon_abd_pin_enable_interrupt(struct decon_device *decon, struct abd_pin *pin, bool on)
{
	struct abd_trace *trace = &pin->p_lcdon;

	if (!pin || !pin->irq)
		return;

	pin->level = gpio_get_value(pin->gpio);

	if (pin->level == pin->active_level)
		decon_abd_save_pin(decon, pin, trace, on);

	decon_info("%s: on: %d, %s(%d,%d) level: %d, count: %d, state: %d, %s\n", __func__,
		on, pin->name, pin->irq, pin->desc->depth, pin->level, trace->count, decon->state, (pin->level == pin->active_level) ? "abnormal" : "normal");

	if (pin->name && !strcmp(pin->name, "pcd")) {
		decon->ignore_vsync = (pin->level == pin->active_level) ? 1 : 0;
		decon_info("%s: ignore_vsync: %d\n", __func__, decon->ignore_vsync);
	}

	if (on) {
		decon_abd_pin_clear_pending_bit(pin->irq);
		enable_irq(pin->irq);
	} else
		disable_irq_nosync(pin->irq);
}

static void decon_abd_pin_enable(struct decon_device *decon, int enable)
{
	struct abd_protect *abd = &decon->abd;
	struct dsim_device *dsim = container_of(decon->output_sd, struct dsim_device, sd);
	unsigned int i = 0;

	if (!abd)
		return;

	if (enable) {
		if (abd->irq_enable == 1) {
			decon_info("%s: already enabled irq_enable: %d\n", __func__, abd->irq_enable);
			return;
		}

		abd->f_lcdon.lcdon_flag = 0;
		abd->u_lcdon.lcdon_flag = 0;

		abd->irq_enable = 1;
	} else {
		if (abd->irq_enable == 0) {
			decon_info("%s: already disabled irq_enable: %d\n", __func__, abd->irq_enable);
			return;
		}

		abd->irq_enable = 0;
	}

	if (dsim && !dsim->priv.lcdconnected)
		decon_info("%s: lcdconnected: %d\n", __func__, dsim->priv.lcdconnected);

	for (i = 0; i < ABD_PIN_MAX; i++)
		decon_abd_pin_enable_interrupt(decon, &abd->pin[i], enable);
}

irqreturn_t decon_abd_handler(int irq, void *dev_id)
{
	struct decon_device *decon = (struct decon_device *)dev_id;
	struct abd_protect *abd = &decon->abd;
	struct abd_pin *pin = NULL;
	struct abd_trace *trace = NULL;
	struct adb_pin_handler *pin_handler = NULL;
	unsigned int i = 0;

	spin_lock(&decon->abd.slock);

	for (i = 0; i < ABD_PIN_MAX; i++) {
		pin = &abd->pin[i];
		trace = &pin->p_event;
		if (pin && irq == pin->irq)
			break;
	}

	if (i == ABD_PIN_MAX) {
		decon_info("%s: irq(%d) is not in abd\n", __func__, irq);
		goto exit;
	}

	pin->level = gpio_get_value(pin->gpio);

	decon_abd_save_pin(decon, pin, trace, 1);

	decon_info("%s: %s(%d) level: %d, count: %d, state: %d, %s\n", __func__,
		pin->name, pin->irq, pin->level, trace->count, decon->state, (pin->level == pin->active_level) ? "abnormal" : "normal");

	if (pin->active_level != pin->level)
		goto exit;

	if (i == ABD_PIN_PCD) {
		decon->ignore_vsync = 1;
		decon_info("%s: ignore_vsync: %d\n", __func__, decon->ignore_vsync);
	}

	list_for_each_entry(pin_handler, &pin->handler_list, node) {
		if (pin_handler && pin_handler->handler)
			pin_handler->handler(irq, pin_handler->dev_id);
	}

exit:
	spin_unlock(&decon->abd.slock);

	return IRQ_HANDLED;
}

int decon_abd_pin_register_handler(int irq, irq_handler_t handler, void *dev_id)
{
	struct decon_device *decon = get_decon_drvdata(0);
	struct abd_protect *abd = &decon->abd;
	struct abd_pin *pin = NULL;
	struct adb_pin_handler *pin_handler = NULL;
	unsigned int i = 0;

	if (!irq) {
		decon_info("%s: irq(%d) invalid\n", __func__, irq);
		return -EINVAL;
	}

	if (!handler) {
		decon_info("%s: handler invalid\n", __func__);
		return -EINVAL;
	}

	for (i = 0; i < ABD_PIN_MAX; i++) {
		pin = &abd->pin[i];
		if (pin && irq == pin->irq) {
			decon_info("%s: find irq(%d) for %s pin\n", __func__, irq, pin->name);

			list_for_each_entry(pin_handler, &pin->handler_list, node) {
				WARN(pin_handler->handler == handler && pin_handler->dev_id == dev_id,	\
					"%s: already registered handler\n", __func__);
			}

			pin_handler = kzalloc(sizeof(struct adb_pin_handler), GFP_KERNEL);
			if (!pin_handler) {
				decon_info("%s: handler kzalloc faile\n", __func__);
				break;
			}
			pin_handler->handler = handler;
			pin_handler->dev_id = dev_id;
			list_add_tail(&pin_handler->node, &pin->handler_list);

			decon_info("%s: handler is registered\n", __func__);
			break;
		}
	}

	if (i == ABD_PIN_MAX) {
		decon_info("%s: irq(%d) is not in abd\n", __func__, irq);
		return -EINVAL;
	}

	return 0;
}

static void _decon_abd_print_pin(struct seq_file *m, struct abd_trace *trace)
{
	struct timeval tv;
	struct abd_log *log;
	unsigned int i = 0;

	if (!trace->count)
		return;

	abd_printf(m, "%s total count: %d\n", trace->name, trace->count);
	for (i = 0; i < ABD_LOG_MAX; i++) {
		log = &trace->log[i];
		if (!log->stamp)
			continue;
		tv = ns_to_timeval(log->stamp);
		abd_printf(m, "time: %lu.%06lu level: %d onoff: %d state: %d\n",
			(unsigned long)tv.tv_sec, tv.tv_usec, log->level, log->onoff, log->state);
	}
}

static void decon_abd_print_pin(struct seq_file *m, struct abd_pin *pin)
{
	if (!pin->irq)
		return;

	if (!pin->p_first.count)
		return;

	abd_printf(m, "[%s]\n", pin->name);

	_decon_abd_print_pin(m, &pin->p_first);
	_decon_abd_print_pin(m, &pin->p_lcdon);
	_decon_abd_print_pin(m, &pin->p_event);
}

static const char *sync_status_str(int status)
{
	if (status == 0)
		return "signaled";

	if (status > 0)
		return "active";

	return "error";
}

static void decon_abd_print_fto(struct seq_file *m, struct abd_trace *trace)
{
	struct timeval tv;
	struct abd_log *log;
	unsigned int i = 0;

	if (!trace->count)
		return;

	abd_printf(m, "%s total count: %d\n", trace->name, trace->count);
	for (i = 0; i < ABD_LOG_MAX; i++) {
		log = &trace->log[i];
		if (!log->stamp)
			continue;
		tv = ns_to_timeval(log->stamp);
		abd_printf(m, "time: %lu.%06lu, %d, %s: %s\n",
			(unsigned long)tv.tv_sec, tv.tv_usec, log->winid, log->fence.name, sync_status_str(atomic_read(&log->fence.status)));
	}
}

static void decon_abd_print_ss_log(struct seq_file *m)
{
	unsigned int log_max = 200, i, idx;
	struct timeval tv;
	struct decon_device *decon = m ? m->private : get_decon_drvdata(0);
	int start = atomic_read(&decon->disp_ss_log_idx);
	struct disp_ss_log *log;

	start = (start > log_max) ? start - log_max + 1 : 0;

	for (i = 0; i < log_max; i++) {
		idx = (start + i) % DISP_EVENT_LOG_MAX;
		log = &decon->disp_ss_log[idx];

		if (!ktime_to_ns(log->time))
			continue;
		tv = ktime_to_timeval(log->time);
		if (i && !(i % 10))
			abd_printf(m, "\n");
		abd_printf(m, "%lu.%06lu %11u, ", (unsigned long)tv.tv_sec, tv.tv_usec, log->type);
	}

	abd_printf(m, "\n");
}

static void decon_abd_print_str(struct seq_file *m)
{
	unsigned int log_max = ABD_EVENT_LOG_MAX, i, idx;
	struct timeval tv;
	struct decon_device *decon = m ? m->private : get_decon_drvdata(0);
	int start = atomic_read(&decon->abd.event.log_idx);
	struct abd_event_log *log;
	char print_buf[200] = {0, };
	struct seq_file p = {
		.buf = print_buf,
		.size = sizeof(print_buf) - 1,
	};

	if (start < 0)
		return;

	abd_printf(m, "==========_STR_DEBUG_==========\n");

	start = (start > log_max) ? start - log_max + 1 : 0;

	for (i = 0; i < log_max; i++) {
		idx = (start + i) % ABD_EVENT_LOG_MAX;
		log = &decon->abd.event.log[idx];

		if (!log->stamp)
			continue;
		tv = ns_to_timeval(log->stamp);
		if (i && !(i % 2)) {
			abd_printf(m, "%s\n", p.buf);
			p.count = 0;
			memset(print_buf, 0, sizeof(print_buf));
		}
		seq_printf(&p, "%lu.%06lu %-20s ", (unsigned long)tv.tv_sec, tv.tv_usec, log->print);
	}

	abd_printf(m, "%s\n", p.count ? p.buf : "");
}

static void decon_abd_print_bit(struct seq_file *m, struct abd_trace *trace)
{
	struct abd_log *log;
	unsigned int i = 0;

	if (!trace->count)
		return;

	abd_printf(m, "%s total count: %d\n", trace->name, trace->count);
	for (i = 0; i < ABD_LOG_MAX; i++) {
		log = &trace->log[i];
		if (!log->stamp)
			continue;
		_decon_abd_print_bit(m, log);
	}
}

static int decon_abd_show(struct seq_file *m, void *unused)
{
	struct decon_device *decon = m ? m->private : get_decon_drvdata(0);
	struct abd_protect *abd = &decon->abd;
	struct dsim_device *dsim = container_of(decon->output_sd, struct dsim_device, sd);
	unsigned int i = 0;

	abd_printf(m, "==========_DECON_ABD_==========\n");
	abd_printf(m, "isync: %d, lcdconnected: %d, lcdtype: %6X\n", decon->ignore_vsync, dsim->priv.lcdconnected, lcdtype);

	for (i = 0; i < ABD_PIN_MAX; i++) {
		if (abd->pin[i].p_first.count) {
			abd_printf(m, "==========_PIN_DEBUG_==========\n");
			break;
		}
	}
	for (i = 0; i < ABD_PIN_MAX; i++)
		decon_abd_print_pin(m, &abd->pin[i]);

	if (abd->b_first.count) {
		abd_printf(m, "==========_BIT_DEBUG_==========\n");
		decon_abd_print_bit(m, &abd->b_first);
		decon_abd_print_bit(m, &abd->b_event);
	}

	if (abd->f_first.count) {
		abd_printf(m, "==========_FTO_DEBUG_==========\n");
		decon_abd_print_fto(m, &abd->f_first);
		decon_abd_print_fto(m, &abd->f_lcdon);
		decon_abd_print_fto(m, &abd->f_event);
	}

	decon_abd_print_str(m);

	abd_printf(m, "==========_RAM_DEBUG_==========\n");
	decon_abd_print_ss_log(m);

	return 0;
}

static int decon_abd_open(struct inode *inode, struct file *file)
{
	return single_open(file, decon_abd_show, inode->i_private);
}

static const struct file_operations decon_abd_fops = {
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
	.open = decon_abd_open,
};

static int decon_abd_reboot_notifier(struct notifier_block *this,
		unsigned long code, void *unused)
{
	struct abd_protect *abd = container_of(this, struct abd_protect, reboot_notifier);
	struct decon_device *decon = container_of(abd, struct decon_device, abd);
	struct dsim_device *dsim = container_of(decon->output_sd, struct dsim_device, sd);
	unsigned int i = 0;
	struct seq_file *m = NULL;

	decon_info("++ %s: %lu\n",  __func__, code);

	decon_abd_pin_enable(decon, 0);

	abd_printf(m, "==========_DECON_ABD_==========\n");
	abd_printf(m, "isync: %d, lcdconnected: %d, lcdtype: %6X\n", decon->ignore_vsync, dsim->priv.lcdconnected, lcdtype);

	for (i = 0; i < ABD_PIN_MAX; i++) {
		if (abd->pin[i].p_first.count) {
			abd_printf(m, "==========_PIN_DEBUG_==========\n");
			break;
		}
	}
	for (i = 0; i < ABD_PIN_MAX; i++)
		decon_abd_print_pin(m, &abd->pin[i]);

	if (abd->b_first.count) {
		abd_printf(m, "==========_BIT_DEBUG_==========\n");
		decon_abd_print_bit(m, &abd->b_first);
		decon_abd_print_bit(m, &abd->b_event);
	}

	if (abd->f_first.count) {
		abd_printf(m, "==========_FTO_DEBUG_==========\n");
		decon_abd_print_fto(m, &abd->f_first);
		decon_abd_print_fto(m, &abd->f_lcdon);
		decon_abd_print_fto(m, &abd->f_event);
	}

	decon_abd_print_str(m);

	decon_info("-- %s: %lu\n",  __func__, code);

	return NOTIFY_DONE;
}

static int decon_abd_pin_register_function(struct decon_device *decon, struct abd_pin *pin, char *keyword,
		irqreturn_t func(int irq, void *dev_id))
{
	int ret = 0, gpio = 0;
	enum of_gpio_flags flags;
	struct device_node *np = NULL;
	struct device *dev = decon->dev;
	unsigned int irqf_type = IRQF_TRIGGER_RISING;
	struct abd_trace *trace = &pin->p_lcdon;
	char *prefix_gpio = "gpio_";
	char dts_name[10] = {0, };

	np = dev->of_node;
	if (!np) {
		decon_warn("device node not exist\n");
		goto exit;
	}

	if (strlen(keyword) + strlen(prefix_gpio) >= sizeof(dts_name)) {
		decon_warn("%s: %s is too log(%zu)\n", __func__, keyword, strlen(keyword));
		goto exit;
	}

	scnprintf(dts_name, sizeof(dts_name), "%s%s", prefix_gpio, keyword);

	if (!of_find_property(np, dts_name, NULL))
		goto exit;

	gpio = of_get_named_gpio_flags(np, dts_name, 0, &flags);
	if (!gpio_is_valid(gpio)) {
		decon_info("%s: gpio_is_valid fail, gpio: %s, %d\n", __func__, dts_name, gpio);
		goto exit;
	}

	decon_info("%s: found %s(%d) success\n", __func__, dts_name, gpio);

	if (gpio_to_irq(gpio) > 0) {
		pin->gpio = gpio;
		pin->irq = gpio_to_irq(gpio);
		pin->desc = irq_to_desc(pin->irq);
	} else {
		decon_info("%s: gpio_to_irq fail, gpio: %d, irq: %d\n", __func__, gpio, gpio_to_irq(gpio));
		pin->gpio = 0;
		pin->irq = 0;
		goto exit;
	}

	pin->active_level = !(flags & OF_GPIO_ACTIVE_LOW);
	irqf_type = (flags & OF_GPIO_ACTIVE_LOW) ? IRQF_TRIGGER_FALLING : IRQF_TRIGGER_RISING;
	decon_info("%s: %s is active %s, %s\n", __func__, keyword, pin->active_level ? "high" : "low",
		(irqf_type == IRQF_TRIGGER_RISING) ? "rising" : "falling");

	pin->name = keyword;
	pin->p_first.name = "first";
	pin->p_lcdon.name = "lcdon";
	pin->p_event.name = "event";

	irq_set_irq_type(pin->irq, irqf_type);
	irq_set_status_flags(pin->irq, _IRQ_NOAUTOEN);
	decon_abd_pin_clear_pending_bit(pin->irq);

	if (devm_request_irq(dev, pin->irq, func, irqf_type, keyword, decon)) {
		decon_err("%s: failed to request irq for %s\n", __func__, keyword);
		pin->gpio = 0;
		pin->irq = 0;
		goto exit;
	}

	INIT_LIST_HEAD(&pin->handler_list);

	pin->level = gpio_get_value(pin->gpio);
	if (pin->level == pin->active_level) {
		decon_info("%s: %s(%d) is already %s(%d)\n", __func__, keyword, pin->gpio,
			(pin->active_level) ? "high" : "low", pin->level);

		decon_abd_save_pin(decon, pin, trace, 1);

		if (pin->name && !strcmp(pin->name, "pcd")) {
			decon->ignore_vsync = 1;
			decon_info("%s: ignore_vsync: %d\n", __func__, decon->ignore_vsync);
		}
	}

exit:
	return ret;
}

static int decon_abd_pin_early_notifier_callback(struct notifier_block *this,
			unsigned long event, void *data)
{
	struct abd_protect *abd = container_of(this, struct abd_protect, pin_early_notifier);
	struct decon_device *decon = container_of(abd, struct decon_device, abd);
	struct fb_event *evdata = data;
	int fb_blank;

	switch (event) {
	case FB_EARLY_EVENT_BLANK:
	case DECON_EARLY_EVENT_DOZE:
		break;
	default:
		return NOTIFY_DONE;
	}

	fb_blank = *(int *)evdata->data;

	if (evdata->info->node)
		return NOTIFY_DONE;

	if (IS_EARLY(event) && fb_blank == FB_BLANK_POWERDOWN)
		decon_abd_pin_enable(decon, 0);

	return NOTIFY_DONE;
}

static int decon_abd_pin_after_notifier_callback(struct notifier_block *this,
			unsigned long event, void *data)
{
	struct abd_protect *abd = container_of(this, struct abd_protect, pin_after_notifier);
	struct decon_device *decon = container_of(abd, struct decon_device, abd);
	struct fb_event *evdata = data;
	struct abd_pin *pin = NULL;
	unsigned int i = 0;
	int fb_blank;

	switch (event) {
	case FB_EVENT_BLANK:
	case DECON_EVENT_DOZE:
		break;
	default:
		return NOTIFY_DONE;
	}

	fb_blank = *(int *)evdata->data;

	if (evdata->info->node)
		return NOTIFY_DONE;

	if (IS_AFTER(event) && fb_blank == FB_BLANK_UNBLANK)
		decon_abd_pin_enable(decon, 1);
	else if (IS_AFTER(event) && fb_blank == FB_BLANK_POWERDOWN) {
		for (i = 0; i < ABD_PIN_MAX; i++) {
			pin = &abd->pin[i];
			if (pin && pin->irq)
				decon_abd_pin_clear_pending_bit(pin->irq);
		}
	}

	return NOTIFY_DONE;
}

static void decon_abd_pin_register(struct decon_device *decon)
{
	struct abd_protect *abd = &decon->abd;

	spin_lock_init(&decon->abd.slock);

	decon_abd_pin_register_function(decon, &abd->pin[ABD_PIN_PCD], "pcd", decon_abd_handler);
	decon_abd_pin_register_function(decon, &abd->pin[ABD_PIN_DET], "det", decon_abd_handler);
	decon_abd_pin_register_function(decon, &abd->pin[ABD_PIN_ERR], "err", decon_abd_handler);

	abd->pin_early_notifier.notifier_call = decon_abd_pin_early_notifier_callback;
	abd->pin_early_notifier.priority = decon_nb_priority_max.priority - 1;
	decon_register_notifier(&abd->pin_early_notifier);

	abd->pin_after_notifier.notifier_call = decon_abd_pin_after_notifier_callback;
	abd->pin_after_notifier.priority = decon_nb_priority_min.priority + 1;
	decon_register_notifier(&abd->pin_after_notifier);
}

static void decon_abd_register(struct decon_device *decon)
{
	struct abd_protect *abd = &decon->abd;

	decon_info("%s: ++\n", __func__);

	atomic_set(&decon->abd.event.log_idx, -1);

	abd->u_first.name = abd->f_first.name = abd->b_first.name = "first";
	abd->u_lcdon.name = abd->f_lcdon.name = "lcdon";
	abd->u_event.name = abd->f_event.name = abd->b_event.name = "event";

	debugfs_create_file("debug", 0444, decon->debug_root, decon, &decon_abd_fops);

	abd->reboot_notifier.notifier_call = decon_abd_reboot_notifier;
	register_reboot_notifier(&abd->reboot_notifier);

	decon_info("%s: -- entity was registered\n", __func__);
}

static struct decon_device *find_decon_device(void)
{
	struct platform_device *pdev = NULL;
	struct decon_device *decon = NULL;

	pdev = of_find_decon_platform_device();
	if (!pdev) {
		decon_info("%s: of_find_device_by_node fail\n", __func__);
		return NULL;
	}

	decon = platform_get_drvdata(pdev);
	if (!decon) {
		decon_info("%s: platform_get_drvdata fail\n", __func__);
		return NULL;
	}

	return decon;
}

static int __init decon_abd_init(void)
{
	struct decon_device *decon = find_decon_device();

	if (!decon) {
		decon_info("find_decon_device fail\n");
		return 0;
	}

	if (decon->out_type == DECON_OUT_DSI) {
		decon->ignore_vsync = lcdtype ? 0 : 1;
		decon_info("%s: lcdtype: %6X\n", __func__, lcdtype);

		decon_abd_register(decon);
		if (lcdtype) {
			decon_abd_pin_register(decon);
			decon_abd_pin_enable(decon, 1);
		}
	}

	return 0;
}

late_initcall(decon_abd_init);

