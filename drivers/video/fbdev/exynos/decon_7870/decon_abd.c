/*
 * Copyright (c) 2016 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Interface file between DECON and DSIM for Samsung EXYNOS DPU driver
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

#include "decon.h"
#include "dsim.h"
#include "../../../../../kernel/irq/internals.h"

void decon_abd_save_log_fto(struct abd_protect *abd, struct sync_fence *fence)
{
	struct abd_trace *first = &abd->f_first;
	struct abd_trace *lcdon = &abd->f_lcdon;
	struct abd_trace *event = &abd->f_event;

	struct abd_log *first_log = &first->log[(first->count % ABD_LOG_MAX)];
	struct abd_log *lcdon_log = &lcdon->log[(lcdon->count % ABD_LOG_MAX)];
	struct abd_log *event_log = &event->log[(event->count % ABD_LOG_MAX)];

	memset(event_log, 0, sizeof(struct abd_log));
	event_log->stamp = ktime_get();
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

static void decon_abd_clear_pending_bit(int irq)
{
	struct irq_desc *desc;

	desc = irq_to_desc(irq);
	if (desc->irq_data.chip->irq_ack) {
		desc->irq_data.chip->irq_ack(&desc->irq_data);
		desc->istate &= ~IRQS_PENDING;
	}
}

static void decon_abd_save_log_pin(struct decon_device *decon, struct abd_pin *pin, struct abd_trace *trace, bool on)
{
	struct abd_trace *first = &pin->p_first;

	struct abd_log *first_log = &first->log[(first->count) % ABD_LOG_MAX];
	struct abd_log *trace_log = &trace->log[(trace->count) % ABD_LOG_MAX];

	trace_log->stamp = ktime_get();
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

static void decon_abd_enable_interrupt(struct decon_device *decon, struct abd_pin *pin, bool on)
{
	struct abd_trace *trace = &pin->p_lcdon;

	if (!pin || !pin->irq)
		return;

	pin->level = gpio_get_value(pin->gpio);

	decon_info("%s: on: %d, %s(%d) level: %d, count: %d, state: %d\n", __func__, on, pin->name, pin->irq, pin->level, trace->count, decon->state);

	if (pin->level == pin->active_level) {
		decon_abd_save_log_pin(decon, pin, trace, on);
		if (pin->name && !strcmp(pin->name, "pcd")) {
			decon->ignore_vsync = 1;
			decon_info("%s: ignore_vsync: %d\n", __func__, decon->ignore_vsync);
		}
	}

	if (on) {
		decon_abd_clear_pending_bit(pin->irq);
		enable_irq(pin->irq);
	} else
		disable_irq_nosync(pin->irq);
}

void decon_abd_enable(struct decon_device *decon, int enable)
{
	struct abd_protect *abd = &decon->abd;

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

	decon_abd_enable_interrupt(decon, &abd->pcd, enable);
	decon_abd_enable_interrupt(decon, &abd->det, enable);
	decon_abd_enable_interrupt(decon, &abd->err, enable);
}

irqreturn_t decon_abd_pcd_handler(int irq, void *dev_id)
{
	struct decon_device *decon = (struct decon_device *)dev_id;
	struct abd_pin *pin = &decon->abd.pcd;
	struct abd_trace *trace = &pin->p_event;

	pin->level = gpio_get_value(pin->gpio);

	decon_info("%s: %s(%d) level: %d, count: %d, state: %d\n", __func__, pin->name, pin->irq, pin->level, trace->count, decon->state);

	decon_abd_save_log_pin(decon, pin, trace, 1);

	if (pin->active_level != pin->level)
		goto exit;

	decon->ignore_vsync = 1;

exit:
	return IRQ_HANDLED;
}

irqreturn_t decon_abd_det_handler(int irq, void *dev_id)
{
	struct decon_device *decon = (struct decon_device *)dev_id;
	struct abd_pin *pin = &decon->abd.det;
	struct abd_trace *trace = &pin->p_event;

	pin->level = gpio_get_value(pin->gpio);

	decon_info("%s: %s(%d) level: %d, count: %d, state: %d\n", __func__, pin->name, pin->irq, pin->level, trace->count, decon->state);

	decon_abd_save_log_pin(decon, pin, trace, 1);

	if (pin->active_level != pin->level)
		goto exit;

exit:
	return IRQ_HANDLED;
}

irqreturn_t decon_abd_err_handler(int irq, void *dev_id)
{
	struct decon_device *decon = (struct decon_device *)dev_id;
	struct abd_pin *pin = &decon->abd.err;
	struct abd_trace *trace = &pin->p_event;

	pin->level = gpio_get_value(pin->gpio);

	decon_info("%s: %s(%d) level: %d, count: %d, state: %d\n", __func__, pin->name, pin->irq, pin->level, trace->count, decon->state);

	decon_abd_save_log_pin(decon, pin, trace, 1);

	if (pin->active_level != pin->level)
		goto exit;

exit:
	return IRQ_HANDLED;
}

static int decon_abd_reboot_notifier(struct notifier_block *this,
		unsigned long code, void *unused)
{
	struct abd_protect *abd = container_of(this, struct abd_protect, reboot_notifier);
	struct decon_device *decon = container_of(abd, struct decon_device, abd);

	decon_info("%s: %lu\n",  __func__, code);

	decon_abd_enable(decon, 0);

	return NOTIFY_DONE;
}

static int decon_debug_pin_log_print(struct seq_file *m, struct abd_trace *trace)
{
	int i = 0;
	struct timeval tv;
	struct abd_log *log;

	seq_printf(m, "%s total count: %d\n", trace->name, trace->count);
	for (i = 0; i < ABD_LOG_MAX; i++) {
		log = &trace->log[i];
		if (!log->stamp.tv64)
			continue;
		tv = ns_to_timeval(log->stamp.tv64);
		seq_printf(m, "\ttime: %lu.%06lu level: %d onoff: %d state: %d\n",
			(unsigned long)tv.tv_sec, tv.tv_usec, log->level, log->onoff, log->state);
	}

	return 0;
}

static int decon_debug_pin_print(struct seq_file *m, struct abd_pin *pin)
{
	if (!pin->irq)
		return 0;

	seq_printf(m, "[%s interrupts]\n", pin->name);

	decon_debug_pin_log_print(m, &pin->p_first);
	decon_debug_pin_log_print(m, &pin->p_lcdon);
	decon_debug_pin_log_print(m, &pin->p_event);

	return 0;
}

static const char *sync_status_str(int status)
{
	if (status == 0)
		return "signaled";

	if (status > 0)
		return "active";

	return "error";
}

static int decon_debug_fto_print(struct seq_file *m, struct abd_trace *trace)
{
	int i = 0;
	struct timeval tv;
	struct abd_log *log;

	seq_printf(m, "%s total count: %d\n", trace->name, trace->count);
	for (i = 0; i < ABD_LOG_MAX; i++) {
		log = &trace->log[i];
		if (!log->stamp.tv64)
			continue;
		tv = ns_to_timeval(log->stamp.tv64);
		seq_printf(m, "\ttime: %lu.%06lu, %d, %s: %s\n",
			(unsigned long)tv.tv_sec, tv.tv_usec, log->winid, log->fence.name, sync_status_str(atomic_read(&log->fence.status)));
	}

	return 0;
}

static int decon_debug_ss_log_print(struct seq_file *m)
{
	unsigned int ss_log_max = 200, i, idx;
	struct timeval tv;
	struct decon_device *decon = m->private;
	int start = atomic_read(&decon->disp_ss_log_idx);
	struct disp_ss_log *log;

	start = (start > ss_log_max) ? start - ss_log_max + 1: 0;

	for (i = 0; i < ss_log_max; i++) {
		idx = (start + i) % DISP_EVENT_LOG_MAX;
		log = &decon->disp_ss_log[idx];

		if (!log->time.tv64)
			continue;
		tv = ns_to_timeval(log->time.tv64);
		if (i && !(i % 10))
			seq_puts(m, "\n");
		seq_printf(m, "%lu.%06lu %2u, ", (unsigned long)tv.tv_sec, tv.tv_usec, log->type);
	}

	seq_puts(m, "\n");

	return 0;
}

static int decon_debug_show(struct seq_file *m, void *unused)
{
	struct decon_device *decon = m->private;
	struct abd_protect *abd = &decon->abd;

	seq_printf(m, "========== LCD DEBUG ==========\n");
	seq_printf(m, "isync: %d\n", decon->ignore_vsync);
	decon_debug_pin_print(m, &abd->pcd);
	decon_debug_pin_print(m, &abd->det);
	decon_debug_pin_print(m, &abd->err);

	seq_printf(m, "========== FTO DEBUG ==========\n");
	decon_debug_fto_print(m, &abd->f_first);
	decon_debug_fto_print(m, &abd->f_lcdon);
	decon_debug_fto_print(m, &abd->f_event);

	decon_debug_ss_log_print(m);

	return 0;
}

static int decon_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, decon_debug_show, inode->i_private);
}

static const struct file_operations decon_debug_fops = {
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
	.open = decon_debug_open,
};

static int decon_abd_register_function(struct decon_device *decon, struct abd_pin *pin, char *keyword,
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

	gpio = of_get_named_gpio_flags(np, dts_name, 0, &flags);
	if (gpio_is_valid(gpio)) {
		decon_info("%s: found %s(%d) success\n", __func__, dts_name, gpio);
		pin->gpio = gpio;
		pin->irq = gpio_to_irq(pin->gpio);
		pin->active_level = !(flags & OF_GPIO_ACTIVE_LOW);
		irqf_type = (flags & OF_GPIO_ACTIVE_LOW) ? IRQF_TRIGGER_FALLING : IRQF_TRIGGER_RISING;
		decon_info("%s: %s is active %s, %s\n", __func__, keyword, pin->active_level ? "high" : "low",
			(irqf_type == IRQF_TRIGGER_RISING) ? "rising" : "falling");
		ret++;

		if (pin->irq) {
			pin->name = keyword;
			pin->p_first.name = "first";
			pin->p_lcdon.name = "lcdon";
			pin->p_event.name = "event";

			irq_set_irq_type(pin->irq, irqf_type);
			irq_set_status_flags(pin->irq, _IRQ_NOAUTOEN);
			decon_abd_clear_pending_bit(pin->irq);

			if (devm_request_irq(dev, pin->irq, func, irqf_type, keyword, decon)) {
				decon_err("%s: failed to request irq for %s\n", __func__, keyword);
				pin->irq = 0;
				ret--;
			}

			pin->level = gpio_get_value(pin->gpio);
			if (pin->level == pin->active_level) {
				decon_info("%s: %s(%d) is already %s(%d)\n", __func__, keyword, pin->gpio,
					(pin->active_level) ? "high" : "low", pin->level);

				decon_abd_save_log_pin(decon, pin, trace, 1);

				if (pin->name && !strcmp(pin->name, "pcd")) {
					decon->ignore_vsync = 1;
					decon_info("%s: ignore_vsync: %d\n", __func__, decon->ignore_vsync);
				}
			}
		}
	}

exit:
	return ret;
}

int decon_abd_register(struct decon_device *decon)
{
	int ret = 0;
	struct abd_protect *abd = &decon->abd;

	decon_info("%s: ++\n", __func__);

	ret += decon_abd_register_function(decon, &abd->pcd, "pcd", decon_abd_pcd_handler);
	ret += decon_abd_register_function(decon, &abd->det, "det", decon_abd_det_handler);
	ret += decon_abd_register_function(decon, &abd->err, "err", decon_abd_err_handler);

	abd->u_first.name = abd->f_first.name = "first";
	abd->u_lcdon.name = abd->f_lcdon.name = "lcdon";
	abd->u_event.name = abd->f_event.name = "event";

	if (ret < 0)
		goto exit;

	debugfs_create_file("debug", 0444, decon->debug_root, decon, &decon_debug_fops);

	abd->reboot_notifier.notifier_call = decon_abd_reboot_notifier;
	register_reboot_notifier(&abd->reboot_notifier);
exit:
	decon_info("%s: -- %d entity was registered\n", __func__, ret);

	return ret;
}

