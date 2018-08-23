/*
 *  drivers/cpufreq/cpufreq_intellidemand.c
 *
 *  Copyright (C)  2001 Russell King
 *            (C)  2003 Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>.
 *                      Jun Nakajima <jun.nakajima@intel.com>
 *            (C)  2013 The Linux Foundation. All rights reserved.
 *            (C)  2013 Paul Reioux
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/cpu.h>
#include <linux/jiffies.h>
#include <linux/kernel_stat.h>
#include <linux/mutex.h>
#include <linux/hrtimer.h>
#include <linux/tick.h>
#include <linux/ktime.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/slab.h>

#define INTELLIDEMAND_MAJOR_VERSION    5
#define INTELLIDEMAND_MINOR_VERSION    5

/*
 * dbs is used in this file as a shortform for demandbased switching
 * It helps to keep variable names smaller, simpler
 */

#define DEF_SAMPLING_RATE			(20000)
#define DEF_FREQUENCY_UP_THRESHOLD		(80)
#define DEF_SAMPLING_DOWN_FACTOR		(1)
#define MAX_SAMPLING_DOWN_FACTOR		(3)
#define MICRO_FREQUENCY_UP_THRESHOLD		(95)
#define MICRO_FREQUENCY_MIN_SAMPLE_RATE		(10000)
#define MIN_FREQUENCY_UP_THRESHOLD		(11)
#define MAX_FREQUENCY_UP_THRESHOLD		(100)
#define MIN_FREQUENCY_DOWN_DIFFERENTIAL		(1)

#define DEF_FREQ_STEP				(25)
#define DEF_STEP_UP_EARLY_HISPEED		(1958400)
#define DEF_STEP_UP_INTERIM_HISPEED		(2265600)
#define DEF_SAMPLING_EARLY_HISPEED_FACTOR	(2)
#define DEF_SAMPLING_INTERIM_HISPEED_FACTOR	(3)

/* PATCH : SMART_UP */
#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))

#define SMART_UP_PLUS (0)
#define SMART_UP_SLOW_UP_AT_HIGH_FREQ (1)
#define SUP_MAX_STEP (3)
#define SUP_CORE_NUM (4)
#define SUP_SLOW_UP_DUR (5)
#define SUP_SLOW_UP_DUR_DEFAULT (2)

#define SUP_HIGH_SLOW_UP_DUR (5)
#define SUP_FREQ_LEVEL (14)

#if defined(SMART_UP_PLUS)
static unsigned int SUP_THRESHOLD_STEPS[SUP_MAX_STEP] = {85, 90, 95};
static unsigned int SUP_FREQ_STEPS[SUP_MAX_STEP] = {4, 3, 2};
typedef struct{
	unsigned int freq_idx;
	unsigned int freq_value;
} freq_table_idx;
static freq_table_idx pre_freq_idx[SUP_CORE_NUM] = {};

#endif


#if defined(SMART_UP_SLOW_UP_AT_HIGH_FREQ)

#define SUP_SLOW_UP_FREQUENCY			(1728000)
#define SUP_HIGH_SLOW_UP_FREQUENCY		(2265600)
#define SUP_SLOW_UP_LOAD			(75)

typedef struct {
	unsigned int hist_max_load[SUP_SLOW_UP_DUR];
	unsigned int hist_load_cnt;
} history_load;
static void reset_hist(history_load *hist_load);
static history_load hist_load[SUP_CORE_NUM] = {};

typedef struct {
	unsigned int hist_max_load[SUP_HIGH_SLOW_UP_DUR];
	unsigned int hist_load_cnt;
} history_load_high;
static void reset_hist_high(history_load_high *hist_load);
static history_load_high hist_load_high[SUP_CORE_NUM] = {};

#endif


/*
 * The polling frequency of this governor depends on the capability of
 * the processor. Default polling frequency is 1000 times the transition
 * latency of the processor. The governor will work on any processor with
 * transition latency <= 10mS, using appropriate sampling
 * rate.
 * For CPUs with transition latency > 10mS (mostly drivers with CPUFREQ_ETERNAL)
 * this governor will not work.
 * All times here are in uS.
 */
#define MIN_SAMPLING_RATE_RATIO			(2)

static unsigned int min_sampling_rate;

#define LATENCY_MULTIPLIER			(1000)
#define MIN_LATENCY_MULTIPLIER			(100)
#define TRANSITION_LATENCY_LIMIT		(10 * 1000 * 1000)

#define POWERSAVE_BIAS_MAXLEVEL			(1000)
#define POWERSAVE_BIAS_MINLEVEL			(-1000)

static void do_dbs_timer(struct work_struct *work);

/* Sampling types */
enum {DBS_NORMAL_SAMPLE, DBS_SUB_SAMPLE};

struct cpu_dbs_info_s {
	u64 prev_cpu_idle;
	u64 prev_cpu_iowait;
	u64 prev_cpu_wall;
	u64 prev_cpu_nice;
	struct cpufreq_policy *cur_policy;
	struct delayed_work work;
	struct cpufreq_frequency_table *freq_table;
	unsigned int freq_lo;
	unsigned int freq_lo_jiffies;
	unsigned int freq_hi_jiffies;
	unsigned int rate_mult;
	unsigned int prev_load;
	unsigned int max_load;
	unsigned int cpu;
	unsigned int sample_type:1;
	unsigned int freq_stay_count;
	/*
	 * percpu mutex that serializes governor limit change with
	 * do_dbs_timer invocation. We do not want do_dbs_timer to run
	 * when user is changing the governor or limits.
	 */
	struct mutex timer_mutex;
};
static DEFINE_PER_CPU(struct cpu_dbs_info_s, id_cpu_dbs_info);

static inline void dbs_timer_init(struct cpu_dbs_info_s *dbs_info);
static inline void dbs_timer_exit(struct cpu_dbs_info_s *dbs_info);

static unsigned int dbs_enable;	/* number of CPUs using this policy */

/*
 * dbs_mutex protects dbs_enable and dbs_info during start/stop.
 */
static DEFINE_MUTEX(dbs_mutex);

static struct workqueue_struct *dbs_wq;

static struct dbs_tuners {
	unsigned int sampling_rate;
	unsigned int up_threshold;
	unsigned int up_threshold_multi_core;
	unsigned int optimal_freq;
	unsigned int up_threshold_any_cpu_load;
	unsigned int sync_freq;
	unsigned int sampling_down_factor;
	/* 20130711 smart_up */
	unsigned int smart_up;
	unsigned int smart_slow_up_load;
	unsigned int smart_slow_up_freq;
	unsigned int smart_slow_up_dur;
	unsigned int smart_high_slow_up_freq;
	unsigned int smart_high_slow_up_dur;
	unsigned int smart_each_off;
	/* end smart_up */
	unsigned int freq_step;
	unsigned int step_up_early_hispeed;
	unsigned int step_up_interim_hispeed;
	unsigned int sampling_early_factor;
	unsigned int sampling_interim_factor;
	unsigned int two_phase_freq;
} dbs_tuners_ins = {
	.up_threshold_multi_core = DEF_FREQUENCY_UP_THRESHOLD,
	.up_threshold = DEF_FREQUENCY_UP_THRESHOLD,
	.sampling_down_factor = DEF_SAMPLING_DOWN_FACTOR,
	.up_threshold_any_cpu_load = DEF_FREQUENCY_UP_THRESHOLD,
	.sync_freq = 1574400,
	.optimal_freq = 1574400,
	/* 20130711 smart_up */
	.smart_up = SMART_UP_PLUS,
	.smart_slow_up_load = SUP_SLOW_UP_LOAD,
	.smart_slow_up_freq = SUP_SLOW_UP_FREQUENCY,
	.smart_slow_up_dur = SUP_SLOW_UP_DUR_DEFAULT,
	.smart_high_slow_up_freq = SUP_HIGH_SLOW_UP_FREQUENCY,
	.smart_high_slow_up_dur = SUP_HIGH_SLOW_UP_DUR,
	.smart_each_off = 0,
	/* end smart_up */
	.freq_step = DEF_FREQ_STEP,
	.step_up_early_hispeed = DEF_STEP_UP_EARLY_HISPEED,
	.step_up_interim_hispeed = DEF_STEP_UP_INTERIM_HISPEED,
	.sampling_early_factor = DEF_SAMPLING_EARLY_HISPEED_FACTOR,
	.sampling_interim_factor = DEF_SAMPLING_INTERIM_HISPEED_FACTOR,
	.two_phase_freq = 0,
	.sampling_rate = DEF_SAMPLING_RATE,
};

/************************** sysfs interface ************************/

static ssize_t show_sampling_rate_min(struct kobject *kobj,
				      struct attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", min_sampling_rate);
}

define_one_global_ro(sampling_rate_min);

/* cpufreq_intellidemand Governor Tunables */
#define show_one(file_name, object)					\
static ssize_t show_##file_name						\
(struct kobject *kobj, struct attribute *attr, char *buf)	      \
{									\
	return sprintf(buf, "%u\n", dbs_tuners_ins.object);		\
}
show_one(sampling_rate, sampling_rate);
show_one(up_threshold, up_threshold);
show_one(up_threshold_multi_core, up_threshold_multi_core);
show_one(sampling_down_factor, sampling_down_factor);
show_one(optimal_freq, optimal_freq);
show_one(up_threshold_any_cpu_load, up_threshold_any_cpu_load);
show_one(sync_freq, sync_freq);
/* 20130711 smart_up */
show_one(smart_up, smart_up);
show_one(smart_slow_up_load, smart_slow_up_load);
show_one(smart_slow_up_freq, smart_slow_up_freq);
show_one(smart_slow_up_dur, smart_slow_up_dur);
show_one(smart_high_slow_up_freq, smart_high_slow_up_freq);
show_one(smart_high_slow_up_dur, smart_high_slow_up_dur);
show_one(smart_each_off, smart_each_off);
/* end smart_up */
show_one(freq_step, freq_step);
show_one(step_up_early_hispeed, step_up_early_hispeed);
show_one(step_up_interim_hispeed, step_up_interim_hispeed);
show_one(sampling_early_factor, sampling_early_factor);
show_one(sampling_interim_factor, sampling_interim_factor);

static int two_phase_freq_array[NR_CPUS] = {[0 ... NR_CPUS-1] = 1958400} ;

static ssize_t show_two_phase_freq
(struct kobject *kobj, struct attribute *attr, char *buf)
{
	int i = 0 ;
	int shift = 0 ;
	char *buf_pos = buf;
	for ( i = 0 ; i < NR_CPUS; i++) {
		shift = sprintf(buf_pos,"%d,",two_phase_freq_array[i]);
		buf_pos += shift;
	}
	*(buf_pos-1) = '\0';
	return strlen(buf);
}

static ssize_t store_two_phase_freq(struct kobject *a, struct attribute *b,
		const char *buf, size_t count)
{

	int ret = 0;
	if (NR_CPUS == 1)
		ret = sscanf(buf,"%u",&two_phase_freq_array[0]);
	else if (NR_CPUS == 2)
		ret = sscanf(buf,"%u,%u",&two_phase_freq_array[0],
				&two_phase_freq_array[1]);
	else if (NR_CPUS == 4)
		ret = sscanf(buf, "%u,%u,%u,%u", &two_phase_freq_array[0],
				&two_phase_freq_array[1],
				&two_phase_freq_array[2],
				&two_phase_freq_array[3]);
	if (ret < NR_CPUS)
		return -EINVAL;

	return count;
}

static ssize_t store_sampling_rate(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret = 0;
	int mpd = strcmp(current->comm, "mpdecision");

	if (mpd == 0)
		return ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	dbs_tuners_ins.sampling_rate = max(input, min_sampling_rate);

	return count;
}

static ssize_t store_sync_freq(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret = 0;
	int mpd = strcmp(current->comm, "mpdecision");

	if (mpd == 0)
		return ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	dbs_tuners_ins.sync_freq = input;

	return count;
}

static ssize_t store_optimal_freq(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret = 0;
	int mpd = strcmp(current->comm, "mpdecision");

	if (mpd == 0)
		return ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	dbs_tuners_ins.optimal_freq = input;

	return count;
}

static ssize_t store_up_threshold(struct kobject *a, struct attribute *b,
				  const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_FREQUENCY_UP_THRESHOLD ||
			input < MIN_FREQUENCY_UP_THRESHOLD) {
		return -EINVAL;
	}
	dbs_tuners_ins.up_threshold = input;

	return count;
}

static ssize_t store_up_threshold_multi_core(struct kobject *a,
			struct attribute *b, const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_FREQUENCY_UP_THRESHOLD ||
			input < MIN_FREQUENCY_UP_THRESHOLD) {
		return -EINVAL;
	}
	dbs_tuners_ins.up_threshold_multi_core = input;

	return count;
}

static ssize_t store_up_threshold_any_cpu_load(struct kobject *a,
			struct attribute *b, const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_FREQUENCY_UP_THRESHOLD ||
			input < MIN_FREQUENCY_UP_THRESHOLD) {
		return -EINVAL;
	}
	dbs_tuners_ins.up_threshold_any_cpu_load = input;

	return count;
}

static ssize_t store_sampling_down_factor(struct kobject *a,
			struct attribute *b, const char *buf, size_t count)
{
	unsigned int input, j;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_SAMPLING_DOWN_FACTOR || input < 1)
		return -EINVAL;
	dbs_tuners_ins.sampling_down_factor = input;

	/* Reset down sampling multiplier in case it was active */
	for_each_online_cpu(j) {
		struct cpu_dbs_info_s *dbs_info;
		dbs_info = &per_cpu(id_cpu_dbs_info, j);
		dbs_info->rate_mult = 1;
	}
	return count;
}

/* PATCH : SMART_UP */
#if defined(SMART_UP_SLOW_UP_AT_HIGH_FREQ)
static void reset_hist(history_load *hist_load)
{
	int i;

	for (i = 0; i < SUP_SLOW_UP_DUR ; i++)
		hist_load->hist_max_load[i] = 0;

	hist_load->hist_load_cnt = 0;
}


static void reset_hist_high(history_load_high *hist_load)
{	int i;

	for (i = 0; i < SUP_HIGH_SLOW_UP_DUR ; i++)
		hist_load->hist_max_load[i] = 0;

	hist_load->hist_load_cnt = 0;
}

#endif

/* 20130711 smart_up */
static ssize_t store_smart_up(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int i, input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;
	if (input > 1) {
		input = 1;
	} else if (input < 0) {
		input = 0;
	}

	/* buffer reset */
	for_each_online_cpu(i) {
		reset_hist(&hist_load[i]);
		reset_hist_high(&hist_load_high[i]);
	}
	dbs_tuners_ins.smart_up = input;

	return count;
}

static ssize_t store_smart_slow_up_load(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int i, input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;
	if (input > 100) {
		input = 100;
	} else if (input < 0) {
		input = 0;
	}

        /* buffer reset */
	for_each_online_cpu(i) {
		reset_hist(&hist_load[i]);
		reset_hist_high(&hist_load_high[i]);
	}
	dbs_tuners_ins.smart_slow_up_load = input;

	return count;
}

static ssize_t store_smart_slow_up_freq(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int i, input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;
	if (input < 0)
		input = 0;

	/* buffer reset */
	for_each_online_cpu(i) {
		reset_hist(&hist_load[i]);
		reset_hist_high(&hist_load_high[i]);
	}
	dbs_tuners_ins.smart_slow_up_freq = input;

	return count;
}

static ssize_t store_smart_slow_up_dur(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int i, input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;
	if (input > SUP_SLOW_UP_DUR) {
		input = SUP_SLOW_UP_DUR;
	} else if (input < 1) {
		input = 1;
	}

	/* buffer reset */
	for_each_online_cpu(i) {
		reset_hist(&hist_load[i]);
		reset_hist_high(&hist_load_high[i]);
	}
	dbs_tuners_ins.smart_slow_up_dur = input;

	return count;
}
static ssize_t store_smart_high_slow_up_freq(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	unsigned int i;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;
	if (input < 0)
		input = 0;
	/* buffer reset */
	for_each_online_cpu(i) {
		reset_hist(&hist_load[i]);
		reset_hist_high(&hist_load_high[i]);
	}
	dbs_tuners_ins.smart_high_slow_up_freq = input;

	return count;
}
static ssize_t store_smart_high_slow_up_dur(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	unsigned int i;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;
	if (input > SUP_HIGH_SLOW_UP_DUR ) {
		input = SUP_HIGH_SLOW_UP_DUR;
	}else if (input < 1 ) {
		input = 1;
	}
	/* buffer reset */
	for_each_online_cpu(i) {
		reset_hist(&hist_load[i]);
		reset_hist_high(&hist_load_high[i]);
	}
	dbs_tuners_ins.smart_high_slow_up_dur = input;

	return count;
}
static ssize_t store_smart_each_off(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int i, input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;
	if (input > SUP_CORE_NUM) {
		input = SUP_CORE_NUM;
	} else if (input < 0) {
		input = 0;
	}

	/* buffer reset */
	for_each_online_cpu(i) {
		reset_hist(&hist_load[i]);
		reset_hist_high(&hist_load_high[i]);
	}
	dbs_tuners_ins.smart_each_off = input;

	return count;
}
/* end smart_up */

static ssize_t store_freq_step(struct kobject *a,
			struct attribute *b, const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > 100 ||
			input < 0) {
		return -EINVAL;
	}
	dbs_tuners_ins.freq_step = input;

	return count;
}

static ssize_t store_step_up_early_hispeed(struct kobject *a,
			struct attribute *b, const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > 2265600 ||
			input < 0) {
		return -EINVAL;
	}
	dbs_tuners_ins.step_up_early_hispeed = input;

	return count;
}

static ssize_t store_step_up_interim_hispeed(struct kobject *a,
			struct attribute *b, const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > DEF_STEP_UP_INTERIM_HISPEED ||
			input < 0) {
		return -EINVAL;
	}
	dbs_tuners_ins.step_up_interim_hispeed = input;

	return count;
}

static ssize_t store_sampling_early_factor(struct kobject *a,
			struct attribute *b, const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input < 1)
		return -EINVAL;
	dbs_tuners_ins.sampling_early_factor = input;

	return count;
}

static ssize_t store_sampling_interim_factor(struct kobject *a,
			struct attribute *b, const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input < 1)
		return -EINVAL;
	dbs_tuners_ins.sampling_interim_factor = input;

	return count;
}

define_one_global_rw(sampling_rate);
define_one_global_rw(up_threshold);
define_one_global_rw(sampling_down_factor);
define_one_global_rw(up_threshold_multi_core);
define_one_global_rw(optimal_freq);
define_one_global_rw(up_threshold_any_cpu_load);
define_one_global_rw(sync_freq);
/* 20130711 smart_up */
define_one_global_rw(smart_up);
define_one_global_rw(smart_slow_up_load);
define_one_global_rw(smart_slow_up_freq);
define_one_global_rw(smart_slow_up_dur);
define_one_global_rw(smart_high_slow_up_freq);
define_one_global_rw(smart_high_slow_up_dur);
define_one_global_rw(smart_each_off);
/* end smart_up */
define_one_global_rw(freq_step);
define_one_global_rw(step_up_early_hispeed);
define_one_global_rw(step_up_interim_hispeed);
define_one_global_rw(sampling_early_factor);
define_one_global_rw(sampling_interim_factor);
define_one_global_rw(two_phase_freq);

static struct attribute *dbs_attributes[] = {
	&sampling_rate_min.attr,
	&sampling_rate.attr,
	&up_threshold.attr,
	&sampling_down_factor.attr,
	&up_threshold_multi_core.attr,
	&optimal_freq.attr,
	&up_threshold_any_cpu_load.attr,
	&sync_freq.attr,
	/* 20130711 smart_up */
	&smart_up.attr,
	&smart_slow_up_load.attr,
	&smart_slow_up_freq.attr,
	&smart_slow_up_dur.attr,
	&smart_high_slow_up_freq.attr,
	&smart_high_slow_up_dur.attr,
	&smart_each_off.attr,
	/* end smart_up */
	&freq_step.attr,
	&step_up_early_hispeed.attr,
	&step_up_interim_hispeed.attr,
	&sampling_early_factor.attr,
	&sampling_interim_factor.attr,
	&two_phase_freq.attr,
	NULL
};

static struct attribute_group dbs_attr_group = {
	.attrs = dbs_attributes,
	.name = "intellidemand",
};

/************************** sysfs end ************************/

static void dbs_freq_increase(struct cpufreq_policy *p, unsigned int freq)
{
	if (p->cur == p->max)
		return;

	__cpufreq_driver_target(p, freq, CPUFREQ_RELATION_L);
}

static void dbs_check_cpu(struct cpu_dbs_info_s *this_dbs_info)
{

#if defined(SMART_UP_PLUS)
	unsigned int core_j = 0;
#endif

	/* Extrapolated load of this CPU */
	unsigned int load_at_max_freq = 0;
	unsigned int max_load_freq;
	/* Current load across this CPU */
	unsigned int cur_load = 0;
	unsigned int max_load = 0;
	unsigned int max_load_other_cpu = 0;
	struct cpufreq_policy *policy;
	unsigned int j;
	static unsigned int phase = 0;
	static unsigned int counter = 0;
	unsigned int nr_cpus;
	unsigned int sampling_rate;

	sampling_rate = dbs_tuners_ins.sampling_rate * this_dbs_info->rate_mult;
	this_dbs_info->freq_lo = 0;
	policy = this_dbs_info->cur_policy;
	if (policy == NULL)
		return;

	/*
	 * Every sampling_rate, we check, if current idle time is less
	 * than 20% (default), then we try to increase frequency
	 * Every sampling_rate, we look for a the lowest
	 * frequency which can sustain the load while keeping idle time over
	 * 30%. If such a frequency exist, we try to decrease to this frequency.
	 *
	 * Any frequency increase takes it to the maximum frequency.
	 * Frequency reduction happens at minimum steps of
	 * 5% (default) of current frequency
	 */

	/* Get Absolute Load - in terms of freq */
	max_load_freq = 0;

	for_each_cpu(j, policy->cpus) {
		struct cpu_dbs_info_s *j_dbs_info;
		u64 cur_wall_time, cur_idle_time;
		unsigned int idle_time, wall_time;
		unsigned int load_freq;
		int freq_avg;

		j_dbs_info = &per_cpu(id_cpu_dbs_info, j);

		cur_idle_time = get_cpu_idle_time(j, &cur_wall_time, 0);
		wall_time = (unsigned int)
			(cur_wall_time - j_dbs_info->prev_cpu_wall);
		j_dbs_info->prev_cpu_wall = cur_wall_time;

		idle_time = (unsigned int)
			(cur_idle_time - j_dbs_info->prev_cpu_idle);
		j_dbs_info->prev_cpu_idle = cur_idle_time;

		/*
		 * For the purpose of intellidemand, waiting for disk IO is an
		 * indication that you're performance critical, and not that
		 * the system is actually idle. So subtract the iowait time
		 * from the cpu idle time.
		 */

		if (unlikely(!wall_time || wall_time < idle_time))
			continue;

		/*
		 * If the CPU had gone completely idle, and a task just woke up
		 * on this CPU now, it would be unfair to calculate 'load' the
		 * usual way for this elapsed time-window, because it will show
		 * near-zero load, irrespective of how CPU intensive that task
		 * actually is. This is undesirable for latency-sensitive bursty
		 * workloads.
		 *
		 * To avoid this, we reuse the 'load' from the previous
		 * time-window and give this task a chance to start with a
		 * reasonably high CPU frequency. (However, we shouldn't over-do
		 * this copy, lest we get stuck at a high load (high frequency)
		 * for too long, even when the current system load has actually
		 * dropped down. So we perform the copy only once, upon the
		 * first wake-up from idle.)
		 *
		 * Detecting this situation is easy: the governor's deferrable
		 * timer would not have fired during CPU-idle periods. Hence
		 * an unusually large 'wall_time' (as compared to the sampling
		 * rate) indicates this scenario.
		 *
		 * prev_load can be zero in two cases and we must recalculate it
		 * for both cases:
		 * - during long idle intervals
		 * - explicitly set to zero
		 */
		if (unlikely(wall_time > (2 * sampling_rate) &&
			     j_dbs_info->prev_load)) {
			cur_load = j_dbs_info->prev_load;
			j_dbs_info->max_load = cur_load;

			/*
			 * Perform a destructive copy, to ensure that we copy
			 * the previous load only once, upon the first wake-up
			 * from idle.
			 */
			j_dbs_info->prev_load = 0;
		} else {
			cur_load = 100 * (wall_time - idle_time) / wall_time;
			j_dbs_info->max_load = max(cur_load, j_dbs_info->prev_load);
			j_dbs_info->prev_load = cur_load;
		}

		if (cur_load > max_load)
			max_load = cur_load;

//		freq_avg = __cpufreq_driver_getavg(policy, j);
		if (policy == NULL)
			return;
//		if (freq_avg <= 0)
//			freq_avg = policy->cur;

		load_freq = cur_load * freq_avg;
		if (load_freq > max_load_freq)
			max_load_freq = load_freq;

#if defined(SMART_UP_PLUS)
		max_load = cur_load;
		core_j = j;
#endif

	}

	for_each_online_cpu(j) {
		struct cpu_dbs_info_s *j_dbs_info;
		j_dbs_info = &per_cpu(id_cpu_dbs_info, j);

		if (j == policy->cpu)
			continue;

		if (max_load_other_cpu < j_dbs_info->max_load)
			max_load_other_cpu = j_dbs_info->max_load;
	}

	/* calculate the scaled load across CPU */
	load_at_max_freq = (cur_load * policy->cur)/policy->max;

	cpufreq_notify_utilization(policy, load_at_max_freq);

/* PATCH : SMART_UP */
	if (dbs_tuners_ins.smart_up && (core_j + 1) >
				dbs_tuners_ins.smart_each_off) {
		if (max_load_freq > SUP_THRESHOLD_STEPS[0] * policy->cur) {
			int smart_up_inc =
				(policy->max - policy->cur) / SUP_FREQ_STEPS[0];
			int freq_next = 0;
			int i = 0;

			/* 20130429 UPDATE */
			int check_idx =  0;
			int check_freq = 0;
			int temp_up_inc =0;

			if (counter < 5) {
				counter++;
				if (counter > 2) {
					phase = 1;
				}
			}

			nr_cpus = num_online_cpus();
			dbs_tuners_ins.two_phase_freq = two_phase_freq_array[nr_cpus-1];
			if (dbs_tuners_ins.two_phase_freq < policy->cur)
				phase = 1;
			if (dbs_tuners_ins.two_phase_freq != 0 && phase == 0) {
				dbs_freq_increase(policy, dbs_tuners_ins.two_phase_freq);
			} else {
				if (policy->cur < policy->max)
					this_dbs_info->rate_mult =
						dbs_tuners_ins.sampling_down_factor;
				dbs_freq_increase(policy, policy->max);
			}

			for (i = (SUP_MAX_STEP - 1); i > 0; i--) {
				if (max_load_freq > SUP_THRESHOLD_STEPS[i]
							* policy->cur) {
					smart_up_inc = (policy->max - policy->cur)
							/ SUP_FREQ_STEPS[i];
					break;
				}
			}

			/* 20130429 UPDATE */
			check_idx =  pre_freq_idx[core_j].freq_idx;
			check_freq = pre_freq_idx[core_j].freq_value;
			if (( check_idx == 0)
					|| (this_dbs_info->freq_table[check_idx].frequency
					!=  policy->cur)) {
				int i = 0;
				for (i =0; i < SUP_FREQ_LEVEL; i ++) {
					if (this_dbs_info->freq_table[i].frequency == policy->cur) {
						pre_freq_idx[core_j].freq_idx = i;
						pre_freq_idx[core_j].freq_value = policy->cur;
						check_idx =  i;
						check_freq = policy->cur;
						break;
					}
				}
			}

			if (check_idx < SUP_FREQ_LEVEL-1) {
				temp_up_inc =
					this_dbs_info->freq_table[check_idx + 1].frequency
					- check_freq;
			}

			if (smart_up_inc < temp_up_inc )
				smart_up_inc = temp_up_inc;

			freq_next = MIN((policy->cur + smart_up_inc), policy->max);

			if (policy->cur >= dbs_tuners_ins.smart_high_slow_up_freq) {
				int idx = hist_load_high[core_j].hist_load_cnt;
				int avg_hist_load = 0;

				if (idx >= dbs_tuners_ins.smart_high_slow_up_dur)
					idx = 0;

				hist_load_high[core_j].hist_max_load[idx] = max_load;
				hist_load_high[core_j].hist_load_cnt = idx + 1;

				/* note : check history_load and get_sum_hist_load */
				if (hist_load_high[core_j].
						hist_max_load[dbs_tuners_ins.smart_high_slow_up_dur - 1] > 0) {
					int sum_hist_load_freq = 0;
					int i = 0;
					for (i = 0; i < dbs_tuners_ins.smart_high_slow_up_dur; i++)
						sum_hist_load_freq +=
							hist_load_high[core_j].hist_max_load[i];

					avg_hist_load = sum_hist_load_freq
								/ dbs_tuners_ins.smart_high_slow_up_dur;

					if (avg_hist_load > dbs_tuners_ins.smart_slow_up_load) {
						reset_hist_high(&hist_load_high[core_j]);
						freq_next = MIN((policy->cur + temp_up_inc), policy->max);
					} else
						freq_next = policy->cur;
				} else {
					freq_next = policy->cur;
				}

			} else if (policy->cur >= dbs_tuners_ins.smart_slow_up_freq ) {
				int idx = hist_load[core_j].hist_load_cnt;
				int avg_hist_load = 0;

				if (idx >= dbs_tuners_ins.smart_slow_up_dur)
					idx = 0;

				hist_load[core_j].hist_max_load[idx] = max_load;
				hist_load[core_j].hist_load_cnt = idx + 1;

				/* note : check history_load and get_sum_hist_load */
				if (hist_load[core_j].
					hist_max_load[dbs_tuners_ins.smart_slow_up_dur - 1] > 0) {
					int sum_hist_load_freq = 0;
					int i = 0;
					for (i = 0; i < dbs_tuners_ins.smart_slow_up_dur; i++)
						sum_hist_load_freq +=
							hist_load[core_j].hist_max_load[i];

					avg_hist_load = sum_hist_load_freq
							/ dbs_tuners_ins.smart_slow_up_dur;

					if (avg_hist_load > dbs_tuners_ins.smart_slow_up_load) {
						reset_hist(&hist_load[core_j]);
						freq_next = MIN((policy->cur + temp_up_inc), policy->max);
					} else
						freq_next = policy->cur;
				} else {
					freq_next = policy->cur;
				}
			} else {
				reset_hist(&hist_load[core_j]);
			}
			if (freq_next == policy->max)
				this_dbs_info->rate_mult =
					dbs_tuners_ins.sampling_down_factor;

			dbs_freq_increase(policy, freq_next);
			return;
		}
	} else {
		/* Check for frequency increase */
		if (max_load_freq > dbs_tuners_ins.up_threshold * policy->cur) {
			int target;
			int inc;

			if (policy->cur < dbs_tuners_ins.step_up_early_hispeed) {
				target = dbs_tuners_ins.step_up_early_hispeed;
			} else if (policy->cur < dbs_tuners_ins.step_up_interim_hispeed) {
				if (policy->cur == dbs_tuners_ins.step_up_early_hispeed) {
					if (this_dbs_info->freq_stay_count <
						dbs_tuners_ins.sampling_early_factor) {
						this_dbs_info->freq_stay_count++;
						return;
					}
				}
				this_dbs_info->freq_stay_count = 1;
				inc = (policy->max * dbs_tuners_ins.freq_step) / 100;
				target = min(dbs_tuners_ins.step_up_interim_hispeed,
					policy->cur + inc);
			} else {
				if (policy->cur == dbs_tuners_ins.step_up_interim_hispeed) {
					if (this_dbs_info->freq_stay_count <
						dbs_tuners_ins.sampling_interim_factor) {
						this_dbs_info->freq_stay_count++;
						return;
					}
				}
				this_dbs_info->freq_stay_count = 1;
				target = policy->max;
			}

			pr_debug("%s: cpu=%d, cur=%d, target=%d\n",
				__func__, policy->cpu, policy->cur, target);

			/* If switching to max speed, apply sampling_down_factor */
			if (target == policy->max)
				this_dbs_info->rate_mult =
					dbs_tuners_ins.sampling_down_factor;

			dbs_freq_increase(policy, target);
			return;
		}
	}
	if (counter > 0) {
		counter--;
		if (counter == 0) {
			phase = 0;
		}
	}

	if (num_online_cpus() > 1) {
		if (max_load_other_cpu >
				dbs_tuners_ins.up_threshold_any_cpu_load) {
			if (policy->cur < dbs_tuners_ins.sync_freq)
				dbs_freq_increase(policy,
						dbs_tuners_ins.sync_freq);
			return;
		}

		if (max_load_freq > (dbs_tuners_ins.up_threshold_multi_core *
								policy->cur)) {
			if (policy->cur < dbs_tuners_ins.optimal_freq)
				dbs_freq_increase(policy,
						dbs_tuners_ins.optimal_freq);
			return;
		}
	}

	/* Check for frequency decrease */
	/* if we cannot reduce the frequency anymore, break out early */
	if (policy->cur == policy->min)
		return;

	/*
	 * The optimal frequency is the frequency that is the lowest that
	 * can support the current CPU usage without triggering the up
	 * policy. To be safe, we focus 10 points under the threshold.
	 */
	if (max_load_freq <
	    (dbs_tuners_ins.up_threshold * policy->cur)) {
		unsigned int freq_next;
		freq_next = max_load_freq /
				(dbs_tuners_ins.up_threshold);

		/* PATCH : SMART_UP */
		if (dbs_tuners_ins.smart_up && (core_j + 1) >
					dbs_tuners_ins.smart_each_off) {
			if (freq_next >= dbs_tuners_ins.smart_high_slow_up_freq) {
				int idx = hist_load_high[core_j].hist_load_cnt;

				if (idx >= dbs_tuners_ins.smart_high_slow_up_dur)
					idx = 0;

				hist_load_high[core_j].hist_max_load[idx] = max_load;
				hist_load_high[core_j].hist_load_cnt = idx + 1;
			} else if (freq_next >= dbs_tuners_ins.smart_slow_up_freq) {
				int idx = hist_load[core_j].hist_load_cnt;

				if (idx >= dbs_tuners_ins.smart_slow_up_dur)
					idx = 0;

				hist_load[core_j].hist_max_load[idx] = max_load;
				hist_load[core_j].hist_load_cnt = idx + 1;

				reset_hist_high(&hist_load_high[core_j]);
			} else if (policy->cur >= dbs_tuners_ins.smart_slow_up_freq) {
				reset_hist(&hist_load[core_j]);
				reset_hist_high(&hist_load_high[core_j]);
			}
		}

		/* No longer fully busy, reset rate_mult */
		this_dbs_info->rate_mult = 1;
		this_dbs_info->freq_stay_count = 1;

		if (num_online_cpus() > 1) {
			if (max_load_other_cpu >
				dbs_tuners_ins.up_threshold_multi_core &&
					freq_next < dbs_tuners_ins.sync_freq)
				freq_next = dbs_tuners_ins.sync_freq;

			if (max_load_freq >
					(dbs_tuners_ins.up_threshold_multi_core *
					policy->cur) &&
					freq_next < dbs_tuners_ins.optimal_freq)
				freq_next = dbs_tuners_ins.optimal_freq;

		}
		__cpufreq_driver_target(policy, freq_next,
				CPUFREQ_RELATION_L);
	}
}

static void do_dbs_timer(struct work_struct *work)
{
	struct cpu_dbs_info_s *dbs_info =
		container_of(work, struct cpu_dbs_info_s, work.work);
	int sample_type = dbs_info->sample_type;
	int delay;

	if (unlikely(!cpu_online(dbs_info->cpu) || !dbs_info->cur_policy))
		return;

	mutex_lock(&dbs_info->timer_mutex);

	/* Common NORMAL_SAMPLE setup */
	dbs_info->sample_type = DBS_NORMAL_SAMPLE;
	if (sample_type == DBS_NORMAL_SAMPLE) {
		dbs_check_cpu(dbs_info);
		if (dbs_info->freq_lo) {
			/* Setup timer for SUB_SAMPLE */
			dbs_info->sample_type = DBS_SUB_SAMPLE;
			delay = dbs_info->freq_hi_jiffies;
		} else {
			delay = usecs_to_jiffies(dbs_tuners_ins.sampling_rate
				* dbs_info->rate_mult);
		}
	} else {
		__cpufreq_driver_target(dbs_info->cur_policy,
			dbs_info->freq_lo, CPUFREQ_RELATION_H);
		delay = dbs_info->freq_lo_jiffies;
	}
	queue_delayed_work_on(dbs_info->cpu, dbs_wq, &dbs_info->work, delay);
	mutex_unlock(&dbs_info->timer_mutex);
}

static inline void dbs_timer_init(struct cpu_dbs_info_s *dbs_info)
{
	/* We want all CPUs to do sampling nearly on same jiffy */
	int delay = usecs_to_jiffies(dbs_tuners_ins.sampling_rate);

	if (num_online_cpus() > 1)
		delay -= jiffies % delay;

	dbs_info->sample_type = DBS_NORMAL_SAMPLE;
	INIT_DEFERRABLE_WORK(&dbs_info->work, do_dbs_timer);
	queue_delayed_work_on(dbs_info->cpu, dbs_wq, &dbs_info->work, delay);
}

static inline void dbs_timer_exit(struct cpu_dbs_info_s *dbs_info)
{
	cancel_delayed_work_sync(&dbs_info->work);
}

static int cpufreq_governor_dbs(struct cpufreq_policy *policy,
				   unsigned int event)
{
	unsigned int cpu = policy->cpu;
	struct cpu_dbs_info_s *this_dbs_info;
	unsigned int j;
	int rc;

	this_dbs_info = &per_cpu(id_cpu_dbs_info, cpu);

	switch (event) {
	case CPUFREQ_GOV_START:
		if ((!cpu_online(cpu)) || (!policy))
			return -EINVAL;

		mutex_lock(&dbs_mutex);

		dbs_enable++;
		for_each_cpu(j, policy->cpus) {
			struct cpu_dbs_info_s *j_dbs_info;
			unsigned int prev_load;
			j_dbs_info = &per_cpu(id_cpu_dbs_info, j);
			j_dbs_info->cur_policy = policy;

			j_dbs_info->prev_cpu_idle = get_cpu_idle_time(j,
						&j_dbs_info->prev_cpu_wall,
						0);

			prev_load = (unsigned int)
				(j_dbs_info->prev_cpu_wall - j_dbs_info->prev_cpu_idle);
			j_dbs_info->prev_load = 100 * prev_load /
				(unsigned int) j_dbs_info->prev_cpu_wall;
		}
		cpu = policy->cpu;
		this_dbs_info->cpu = cpu;
		this_dbs_info->rate_mult = 1;
		this_dbs_info->freq_stay_count = 1;
		/*
		 * Start the timerschedule work, when this governor
		 * is used for first time
		 */
		if (dbs_enable == 1) {
			unsigned int latency;

			rc = sysfs_create_group(cpufreq_global_kobject,
						&dbs_attr_group);
			if (rc) {
				dbs_enable--;
				mutex_unlock(&dbs_mutex);
				return rc;
			}

			/* policy latency is in nS. Convert it to uS first */
			latency = policy->cpuinfo.transition_latency / 1000;
			if (latency == 0)
				latency = 1;
			/* Bring kernel and HW constraints together */
			min_sampling_rate = max(min_sampling_rate,
					MIN_LATENCY_MULTIPLIER * latency);
			if (latency != 1)
				dbs_tuners_ins.sampling_rate =
					max(dbs_tuners_ins.sampling_rate,
						latency * LATENCY_MULTIPLIER);

			if (dbs_tuners_ins.optimal_freq == 0)
				dbs_tuners_ins.optimal_freq = policy->min;

			if (dbs_tuners_ins.sync_freq == 0)
				dbs_tuners_ins.sync_freq = policy->min;
		}
		mutex_unlock(&dbs_mutex);

		dbs_timer_init(this_dbs_info);
		break;

	case CPUFREQ_GOV_STOP:
		dbs_timer_exit(this_dbs_info);

		mutex_lock(&dbs_mutex);

		dbs_enable--;

		/* If device is being removed, policy is no longer
		 * valid. */
		this_dbs_info->cur_policy = NULL;
		if (!dbs_enable)
			sysfs_remove_group(cpufreq_global_kobject,
					   &dbs_attr_group);

		mutex_unlock(&dbs_mutex);

		break;

	case CPUFREQ_GOV_LIMITS:
		/* If device is being removed, skip set limits */
		if (!this_dbs_info->cur_policy)
			break;
		mutex_lock(&this_dbs_info->timer_mutex);
		__cpufreq_driver_target(this_dbs_info->cur_policy,
				policy->cur, CPUFREQ_RELATION_L);
		dbs_check_cpu(this_dbs_info);
		mutex_unlock(&this_dbs_info->timer_mutex);
		break;
	}
	return 0;
}

#ifndef CONFIG_CPU_FREQ_DEFAULT_GOV_INTELLIDEMAND
static
#endif
struct cpufreq_governor cpufreq_gov_intellidemand = {
	.name			= "intellidemand",
	.governor		= cpufreq_governor_dbs,
	.max_transition_latency	= TRANSITION_LATENCY_LIMIT,
	.owner			= THIS_MODULE,
};

static int __init cpufreq_gov_dbs_init(void)
{
	u64 idle_time;
	unsigned int i;
	int cpu = get_cpu();

	idle_time = get_cpu_idle_time_us(cpu, NULL);
	put_cpu();
	if (idle_time != -1ULL) {
		/* Idle micro accounting is supported. Use finer thresholds */
		dbs_tuners_ins.up_threshold = MICRO_FREQUENCY_UP_THRESHOLD;
		/*
		 * In nohz/micro accounting case we set the minimum frequency
		 * not depending on HZ, but fixed (very low). The deferred
		 * timer might skip some samples if idle/sleeping as needed.
		*/
		min_sampling_rate = MICRO_FREQUENCY_MIN_SAMPLE_RATE;
	} else {
		/* For correct statistics, we need 10 ticks for each measure */
		min_sampling_rate =
			MIN_SAMPLING_RATE_RATIO * jiffies_to_usecs(10);
	}

	dbs_wq = alloc_workqueue("intellidemand_dbs_wq", WQ_HIGHPRI, 0);
	if (!dbs_wq) {
		printk(KERN_ERR "Failed to create intellidemand_dbs_wq workqueue\n");
		return -EFAULT;
	}
	for_each_possible_cpu(i) {
		struct cpu_dbs_info_s *this_dbs_info =
			&per_cpu(id_cpu_dbs_info, i);

		mutex_init(&this_dbs_info->timer_mutex);
	}

	return cpufreq_register_governor(&cpufreq_gov_intellidemand);
}

static void __exit cpufreq_gov_dbs_exit(void)
{
	unsigned int i;

	cpufreq_unregister_governor(&cpufreq_gov_intellidemand);
	for_each_possible_cpu(i) {
		struct cpu_dbs_info_s *this_dbs_info =
			&per_cpu(id_cpu_dbs_info, i);
		mutex_destroy(&this_dbs_info->timer_mutex);
	}
	destroy_workqueue(dbs_wq);
}

MODULE_AUTHOR("Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>");
MODULE_AUTHOR("Alexey Starikovskiy <alexey.y.starikovskiy@intel.com>");
MODULE_AUTHOR("Paul Reioux <reioux@gmail.com>");
MODULE_DESCRIPTION("'cpufreq_intellidemand' - A dynamic cpufreq governor for "
	"Low Latency Frequency Transition capable processors");
MODULE_LICENSE("GPL");

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_INTELLIDEMAND
fs_initcall(cpufreq_gov_dbs_init);
#else
module_init(cpufreq_gov_dbs_init);
#endif
module_exit(cpufreq_gov_dbs_exit);
