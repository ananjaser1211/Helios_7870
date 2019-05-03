/*
 * linux/drivers/exynos/soc/samsung/exynos-hotplug_governor.c
 *
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/cpumask.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/kobject.h>
#include <linux/ktime.h>
#include <linux/hrtimer.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/cpu.h>
#include <linux/stringify.h>
#include <linux/sched.h>
#include <linux/platform_device.h>
#include <linux/debugfs.h>
#include <linux/cpu_pm.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/sched.h>
#include <linux/fb.h>
#include <linux/irq_work.h>
#include <linux/workqueue.h>
#include <linux/pm_qos.h>
#include <linux/suspend.h>

#include <soc/samsung/cpufreq.h>
#include <soc/samsung/exynos-cpu_hotplug.h>

#include <asm/atomic.h>
#include <asm/page.h>
#define CREATE_TRACE_POINTS
#include <trace/events/hotplug_governor.h>

#include "../../cpufreq/cpu_load_metric.h"

#define DEFAULT_UP_CHANGE_FREQ		(1300000)	/* MHz */
#define DEFAULT_DOWN_CHANGE_FREQ	(1500000)	/* MHz */
#define DEFAULT_MONITOR_MS		(100)		/* ms */
#define DEFAULT_BOOT_ENABLE_MS (30000)		/* 30 s */
#define RETRY_BOOT_ENABLE_MS (100)		/* 100 ms */

enum hpgov_event {
	HPGOV_DYNAMIC,
	HPGOV_EVENT_END,
};

typedef enum {
	H0,
	H1,
	MAX_HSTATE,
} hstate_t;

typedef enum {
	GO_DOWN,
	GO_UP,
	STAY,
} action_t;

struct hpgov_attrib {
	struct kobj_attribute	enabled;
	struct kobj_attribute	up_freq;
	struct kobj_attribute	down_freq;
	struct kobj_attribute	rate;
	struct kobj_attribute	load;

	struct attribute_group	attrib_group;
};

struct hpgov_data {
	enum hpgov_event event;
	int req_cpu_max;
	int req_cpu_min;
};

struct {
	uint32_t			enabled;
	atomic_t			cur_cpu_max;
	atomic_t			cur_cpu_min;
	uint32_t			down_freq;
	uint32_t			up_freq;
	uint32_t			rate;
	uint32_t			load;

	struct hpgov_attrib		attrib;
	struct mutex			attrib_lock;
	struct task_struct		*task;
	struct task_struct		*hptask;
	struct irq_work			update_irq_work;
	struct hpgov_data		data;
	int				hp_state;
	wait_queue_head_t		wait_q;
	wait_queue_head_t		wait_hpq;
} exynos_hpgov;

struct cpu_hstate {
	hstate_t state;
	int cpu_nr;
} hstate_state[] = {
	{
		.state = H0,
		.cpu_nr = NR_CPUS,
	}, {
		.state = H1,
		.cpu_nr = NR_CPUS / 2,
	},
};

#define UP_MONITOR_DURATION_NUM   1
#define DOWN_MONITOR_DURATION_NUM   3
#define TASKS_THRESHOLD		410
#define DEFAULT_LOAD_THRESHOLD	320
#define MAX_CLUSTERS   2
static atomic_t freq_history[MAX_CLUSTERS] =  {ATOMIC_INIT(0), ATOMIC_INIT(0)};
static struct delayed_work hpgov_dynamic_work;

static struct pm_qos_request hpgov_max_pm_qos;
static struct pm_qos_request hpgov_min_pm_qos;

static DEFINE_SPINLOCK(hpgov_lock);

enum {
	HP_STATE_WAITING = 0,		/* waiting for cpumask update */
	HP_STATE_SCHEDULED = 1,		/* hotplugging is scheduled */
	HP_STATE_IN_PROGRESS = 2,	/* in the process of hotplugging */
};

static void exynos_hpgov_irq_work(struct irq_work *irq_work)
{
	wake_up(&exynos_hpgov.wait_q);
}

static int exynos_hpgov_update_governor(enum hpgov_event event, int req_cpu_max, int req_cpu_min)
{
	int ret = 0;
	int cur_cpu_max = atomic_read(&exynos_hpgov.cur_cpu_max);
	int cur_cpu_min = atomic_read(&exynos_hpgov.cur_cpu_min);

	switch(event) {
	default:
		break;
	}

	if (req_cpu_max == cur_cpu_max)
		req_cpu_max = 0;

	if (req_cpu_min == cur_cpu_min)
		req_cpu_min = 0;

	if (!req_cpu_max && !req_cpu_min)
		return ret;

	trace_exynos_hpgov_governor_update(event, req_cpu_max, req_cpu_min);
	if (req_cpu_max)
		atomic_set(&exynos_hpgov.cur_cpu_max, req_cpu_max);
	if (req_cpu_min)
		atomic_set(&exynos_hpgov.cur_cpu_min, req_cpu_min);

	exynos_hpgov.hp_state = HP_STATE_SCHEDULED;
	wake_up(&exynos_hpgov.wait_hpq);

	return ret;
}

static int exynos_hpgov_do_update_governor(void *data)
{
	struct hpgov_data *pdata = (struct hpgov_data *)data;
	unsigned long flags;
	enum hpgov_event event;
	int req_cpu_max;
	int req_cpu_min;

	while (1) {
		wait_event(exynos_hpgov.wait_q, pdata->event || kthread_should_stop());
		if (kthread_should_stop())
			break;

		spin_lock_irqsave(&hpgov_lock, flags);
		event = pdata->event;
		req_cpu_max = pdata->req_cpu_max;
		req_cpu_min = pdata->req_cpu_min;
		pdata->event = 0;
		pdata->req_cpu_max = 0;
		pdata->req_cpu_min = 0;
		spin_unlock_irqrestore(&hpgov_lock, flags);

		exynos_hpgov_update_governor(event, req_cpu_max, req_cpu_min);
	}
	return 0;
}

static int exynos_hpgov_do_hotplug(void *data)
{
	int *event = (int *)data;
	int cpu_max;
	int cpu_min;
	int last_max = 0;
	int last_min = 0;

	while (1) {
		wait_event(exynos_hpgov.wait_hpq, *event || kthread_should_stop());
		if (kthread_should_stop())
			break;

restart:
		exynos_hpgov.hp_state = HP_STATE_IN_PROGRESS;
		cpu_max = atomic_read(&exynos_hpgov.cur_cpu_max);
		cpu_min = atomic_read(&exynos_hpgov.cur_cpu_min);

		if (cpu_max != last_max) {
			pm_qos_update_request(&hpgov_max_pm_qos, cpu_max);
			last_max = cpu_max;
		}

		if (cpu_min != last_min) {
			pm_qos_update_request(&hpgov_min_pm_qos, cpu_min);
			last_min = cpu_min;
		}

		exynos_hpgov.hp_state = HP_STATE_WAITING;
		if (last_max != atomic_read(&exynos_hpgov.cur_cpu_max) ||
			last_min != atomic_read(&exynos_hpgov.cur_cpu_min))
			goto restart;
	}

	return 0;
}

static int exynos_hpgov_set_enabled(uint32_t enable)
{
	int ret = 0;
	static uint32_t last_enable;

	enable = (enable > 0) ? 1 : 0;
	if (last_enable == enable)
		return ret;

	last_enable = enable;

	if (enable) {
		exynos_hpgov.task = kthread_create(exynos_hpgov_do_update_governor,
					      &exynos_hpgov.data, "exynos_hpgov");
		if (IS_ERR(exynos_hpgov.task))
			return -EFAULT;

		kthread_bind(exynos_hpgov.task, 0);
		wake_up_process(exynos_hpgov.task);

		exynos_hpgov.hptask = kthread_create(exynos_hpgov_do_hotplug,
						&exynos_hpgov.hp_state, "exynos_hp");
		if (IS_ERR(exynos_hpgov.hptask)) {
			kthread_stop(exynos_hpgov.task);
			return -EFAULT;
		}

		kthread_bind(exynos_hpgov.hptask, 0);
		wake_up_process(exynos_hpgov.hptask);

		exynos_hpgov.enabled = 1;
#ifndef CONFIG_SCHED_HMP
		queue_delayed_work_on(0, system_freezable_wq, &hpgov_dynamic_work, msecs_to_jiffies(exynos_hpgov.rate));
#endif
	} else {
		kthread_stop(exynos_hpgov.hptask);
		kthread_stop(exynos_hpgov.task);
#ifndef CONFIG_SCHED_HMP
		cancel_delayed_work_sync(&hpgov_dynamic_work);
#endif
		exynos_hpgov.enabled = 0;

		smp_wmb();

		pm_qos_update_request(&hpgov_max_pm_qos, PM_QOS_CPU_ONLINE_MAX_DEFAULT_VALUE);
		pm_qos_update_request(&hpgov_min_pm_qos, PM_QOS_CPU_ONLINE_MAX_DEFAULT_VALUE);

		atomic_set(&exynos_hpgov.cur_cpu_max, PM_QOS_CPU_ONLINE_MAX_DEFAULT_VALUE);
		atomic_set(&exynos_hpgov.cur_cpu_min, PM_QOS_CPU_ONLINE_MAX_DEFAULT_VALUE);
	}

	return ret;
}

static int exynos_hpgov_set_load(uint32_t val)
{
	exynos_hpgov.load = val;

	return 0;
}

static int exynos_hpgov_set_rate(uint32_t val)
{
	exynos_hpgov.rate = val;

	return 0;
}

static int exynos_hpgov_set_up_freq(uint32_t val)
{
	exynos_hpgov.up_freq = val;

	return 0;
}

static int exynos_hpgov_set_down_freq(uint32_t val)
{
	exynos_hpgov.down_freq = val;

	return 0;
}

#define HPGOV_PARAM(_name, _param) \
static ssize_t exynos_hpgov_attr_##_name##_show(struct kobject *kobj, \
			struct kobj_attribute *attr, char *buf) \
{ \
	return snprintf(buf, PAGE_SIZE, "%d\n", _param); \
} \
static ssize_t exynos_hpgov_attr_##_name##_store(struct kobject *kobj, \
		struct kobj_attribute *attr, const char *buf, size_t count) \
{ \
	int ret = 0; \
	uint32_t val; \
	uint32_t old_val; \
	mutex_lock(&exynos_hpgov.attrib_lock); \
	ret = kstrtouint(buf, 10, &val); \
	if (ret) { \
		pr_err("Invalid input %s for %s %d\n", \
				buf, __stringify(_name), ret);\
		mutex_unlock(&exynos_hpgov.attrib_lock); \
		return 0; \
	} \
	old_val = _param; \
	ret = exynos_hpgov_set_##_name(val); \
	if (ret) { \
		pr_err("Error %d returned when setting param %s to %d\n",\
				ret, __stringify(_name), val); \
		_param = old_val; \
	} \
	mutex_unlock(&exynos_hpgov.attrib_lock); \
	return count; \
}

#define HPGOV_RW_ATTRIB(i, _name) \
	exynos_hpgov.attrib._name.attr.name = __stringify(_name); \
	exynos_hpgov.attrib._name.attr.mode = S_IRUGO | S_IWUSR; \
	exynos_hpgov.attrib._name.show = exynos_hpgov_attr_##_name##_show; \
	exynos_hpgov.attrib._name.store = exynos_hpgov_attr_##_name##_store; \
	exynos_hpgov.attrib.attrib_group.attrs[i] = &exynos_hpgov.attrib._name.attr;

HPGOV_PARAM(enabled, exynos_hpgov.enabled);
HPGOV_PARAM(up_freq, exynos_hpgov.up_freq);
HPGOV_PARAM(down_freq, exynos_hpgov.down_freq);
HPGOV_PARAM(rate, exynos_hpgov.rate);
HPGOV_PARAM(load, exynos_hpgov.load);

static void hpgov_boot_enable(struct work_struct *work);
static DECLARE_DELAYED_WORK(hpgov_boot_work, hpgov_boot_enable);
static void hpgov_boot_enable(struct work_struct *work)
{
	if (exynos_hpgov_set_enabled(1))
		schedule_delayed_work_on(0, &hpgov_boot_work, msecs_to_jiffies(RETRY_BOOT_ENABLE_MS));
}

static action_t exynos_hpgov_select_up_down(void)
{
	unsigned int down_freq, up_freq;
	unsigned int c0_freq, c1_freq;
	unsigned int c0_util, c1_util;
	unsigned int load;
	struct cluster_stats cl_stat[2];
	int nr;

	nr = avg_nr_running();

	cpumask_copy(cl_stat[0].mask, topology_core_cpumask(0));
	cpumask_copy(cl_stat[1].mask, topology_core_cpumask(4));
	get_cluster_stats(cl_stat);

	c0_freq = cpufreq_quick_get(0);	/* 0 : first cpu number for Cluster 0 */
	c1_freq = cpufreq_quick_get(4); /* 4 : first cpu number for Cluster 1 */

	c0_util = cl_stat[0].util;
	c1_util = cl_stat[1].util;

	down_freq = exynos_hpgov.down_freq;
	up_freq = exynos_hpgov.up_freq;

	load = exynos_hpgov.load;

	/* make c0_freq > c1_Freq */
	if (c1_freq > c0_freq) {
		swap(c0_freq, c1_freq);
		swap(c0_util, c1_util);
	}

	if ((c1_freq > 0 && (c0_freq < down_freq)) &&
		(c1_freq * c1_util + c0_freq * c0_util <= ((up_freq * load) >> 2) * 3)) {
		atomic_inc(&freq_history[GO_DOWN]);
		atomic_set(&freq_history[GO_UP], 0);
	} else if (c0_freq >= up_freq && (c0_util >= load && nr >= TASKS_THRESHOLD)) {
		atomic_inc(&freq_history[GO_UP]);
		atomic_set(&freq_history[GO_DOWN], 0);
	} else {
		atomic_set(&freq_history[GO_UP], 0);
		atomic_set(&freq_history[GO_DOWN], 0);
	}

	if (atomic_read(&freq_history[GO_UP]) > UP_MONITOR_DURATION_NUM)
		return GO_UP;
	else if (atomic_read(&freq_history[GO_DOWN]) > DOWN_MONITOR_DURATION_NUM)
		return GO_DOWN;

	return STAY;
}

static hstate_t exynos_hpgov_hotplug_adjust_state(action_t move, hstate_t old_state)
{
	hstate_t state;

	if (move == GO_DOWN) {
		state = old_state + 1;
		if ((int)state >= (int)MAX_HSTATE)
			state = MAX_HSTATE - 1;
	} else {
		state = old_state - 1;
		if ((int)state < (int)H0)
			state = H0;
	}

	return state;
}

static hstate_t exynos_hpgov_hstate_get_index(int nr)
{
	int i, size;

	size = ARRAY_SIZE(hstate_state);

	for (i = 0; i < size; i++)
		if (hstate_state[i].cpu_nr == nr)
			return hstate_state[i].state;

	return MAX_HSTATE;
}

static void hpgov_dynamic_monitor(struct work_struct *work)
{
	action_t action;
	hstate_t state, old_state;
	int nr;

	action = exynos_hpgov_select_up_down();

	if (action != STAY) {
		nr = num_online_cpus();
		old_state = exynos_hpgov_hstate_get_index(nr);
		state = exynos_hpgov_hotplug_adjust_state(action, old_state);
		if (state < MAX_HSTATE && old_state != state) {
			nr = hstate_state[state].cpu_nr;
			exynos_hpgov_update_governor(HPGOV_DYNAMIC, NR_CPUS, nr);
		}

		atomic_set(&freq_history[GO_UP], 0);
		atomic_set(&freq_history[GO_DOWN], 0);
	}

	queue_delayed_work_on(0, system_freezable_wq, &hpgov_dynamic_work, msecs_to_jiffies(100));
}

static int exynos_cpu_governor_pm_suspend_notifier(struct notifier_block *notifier,
		unsigned long pm_event, void *v)
{
	int nr;

	nr = hstate_state[H1].cpu_nr;

	switch (pm_event) {
		case PM_SUSPEND_PREPARE:
			atomic_set(&freq_history[GO_UP], 0);
			atomic_set(&freq_history[GO_DOWN], 0);

			cancel_delayed_work_sync(&hpgov_dynamic_work);
			exynos_hpgov_update_governor(HPGOV_DYNAMIC, nr, nr);
			break;
	}

	return NOTIFY_OK;
}

static struct notifier_block exynos_cpu_governor_suspend_nb = {
	.notifier_call = exynos_cpu_governor_pm_suspend_notifier,
	.priority = INT_MAX - 1,
};

static int exynos_cpu_governor_pm_resume_notifier(struct notifier_block *notifier,
		unsigned long pm_event, void *v)
{

	int nr;

	nr = hstate_state[H1].cpu_nr;

	switch (pm_event) {
		case PM_POST_SUSPEND:
			exynos_hpgov_update_governor(HPGOV_DYNAMIC, NR_CPUS, nr);
			queue_delayed_work_on(0, system_freezable_wq, &hpgov_dynamic_work, msecs_to_jiffies(100));
			break;
	}

	return NOTIFY_OK;
}

static struct notifier_block exynos_cpu_governor_resume_nb = {
	.notifier_call = exynos_cpu_governor_pm_resume_notifier,
	.priority = INT_MIN,
};

static int __init exynos_hpgov_init(void)
{
	int ret = 0;
	const int attr_count = 5;

	mutex_init(&exynos_hpgov.attrib_lock);
	init_waitqueue_head(&exynos_hpgov.wait_q);
	init_waitqueue_head(&exynos_hpgov.wait_hpq);
	init_irq_work(&exynos_hpgov.update_irq_work, exynos_hpgov_irq_work);
	INIT_DELAYED_WORK(&hpgov_dynamic_work, hpgov_dynamic_monitor);

	exynos_hpgov.attrib.attrib_group.attrs =
		kzalloc(attr_count * sizeof(struct attribute *), GFP_KERNEL);
	if (!exynos_hpgov.attrib.attrib_group.attrs) {
		ret = -ENOMEM;
		goto done;
	}

	HPGOV_RW_ATTRIB(0, enabled);
#ifndef CONFIG_SCHED_HMP
	HPGOV_RW_ATTRIB(1, up_freq);
	HPGOV_RW_ATTRIB(2, down_freq);
	HPGOV_RW_ATTRIB(3, rate);
	HPGOV_RW_ATTRIB(4, load);
#endif

	exynos_hpgov.attrib.attrib_group.name = "governor";
	ret = sysfs_create_group(exynos_cpu_hotplug_kobj(), &exynos_hpgov.attrib.attrib_group);
	if (ret)
		pr_err("Unable to create sysfs objects :%d\n", ret);

	atomic_set(&exynos_hpgov.cur_cpu_max, PM_QOS_CPU_ONLINE_MAX_DEFAULT_VALUE);
#ifndef CONFIG_SCHED_HMP
	atomic_set(&exynos_hpgov.cur_cpu_min, NR_CLUST0_CPUS);
#else
	atomic_set(&exynos_hpgov.cur_cpu_min, PM_QOS_CPU_ONLINE_MIN_DEFAULT_VALUE);
#endif

	pm_qos_add_request(&hpgov_max_pm_qos, PM_QOS_CPU_ONLINE_MAX, PM_QOS_CPU_ONLINE_MAX_DEFAULT_VALUE);
#ifndef CONFIG_SCHED_HMP
	pm_qos_add_request(&hpgov_min_pm_qos, PM_QOS_CPU_ONLINE_MIN, NR_CLUST0_CPUS);
#else
	pm_qos_add_request(&hpgov_min_pm_qos, PM_QOS_CPU_ONLINE_MIN, PM_QOS_CPU_ONLINE_MIN_DEFAULT_VALUE);
#endif

	exynos_hpgov.down_freq = DEFAULT_DOWN_CHANGE_FREQ;
	exynos_hpgov.up_freq = DEFAULT_UP_CHANGE_FREQ;
	exynos_hpgov.rate = DEFAULT_MONITOR_MS;
	exynos_hpgov.load = DEFAULT_LOAD_THRESHOLD;

	/* regsiter pm notifier */
	register_pm_notifier(&exynos_cpu_governor_suspend_nb);
	register_pm_notifier(&exynos_cpu_governor_resume_nb);

	schedule_delayed_work_on(0, &hpgov_boot_work, msecs_to_jiffies(DEFAULT_BOOT_ENABLE_MS));

done:
	return ret;
}
late_initcall(exynos_hpgov_init);

