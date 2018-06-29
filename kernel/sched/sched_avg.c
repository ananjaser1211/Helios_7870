/* Copyright (c) 2014, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*
 * Scheduler hook for average runqueue determination
 */
#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/math64.h>

static DEFINE_PER_CPU(u64, nr_sum);
static DEFINE_PER_CPU(u64, last_time);
static DEFINE_PER_CPU(u32, nr);
static DEFINE_PER_CPU(spinlock_t, nr_lock) = __SPIN_LOCK_UNLOCKED(nr_lock);
static u64 last_get_time;

/**
 * avg_nr_running
 * @return: Average nr_running value since last poll.
 *	    Returns the avg * 100 to return up to two decimal points
 *	    of accuracy.
 */
int avg_nr_running(void)
{
	int cpu;
	u64 curr_time = sched_clock();
	u64 diff = curr_time - last_get_time;
	u64 tmp_avg = 0;
	int avg;

	if (!diff)
		return 0;

	last_get_time = curr_time;

	/* read and reset nr_running counts */
	for_each_online_cpu(cpu) {
		unsigned long flags;

		spin_lock_irqsave(&per_cpu(nr_lock, cpu), flags);
		tmp_avg += per_cpu(nr_sum, cpu);
		tmp_avg += per_cpu(nr, cpu) *
			(curr_time - per_cpu(last_time, cpu));
		per_cpu(last_time, cpu) = curr_time;
		per_cpu(nr_sum, cpu) = 0;
		spin_unlock_irqrestore(&per_cpu(nr_lock, cpu), flags);
	}

	avg = (int) div64_u64(tmp_avg * 100, diff);

	return avg;
}

/**
 * sched_update_avg_nr_running
 * @cpu: cpu where nr_running is updated
 * @nr_running: Updated nr running value for cpu.
 *
 * Update average with latest nr_running value for CPU
 */
void sched_update_avg_nr_running(int cpu, unsigned int nr_running)
{
	u64 diff;
	u64 curr_time;
	unsigned long flags;

	spin_lock_irqsave(&per_cpu(nr_lock, cpu), flags);
	curr_time = sched_clock();
	diff = curr_time - per_cpu(last_time, cpu);
	per_cpu(last_time, cpu) = curr_time;
	per_cpu(nr_sum, cpu) += per_cpu(nr, cpu) * diff;
	per_cpu(nr, cpu) = nr_running;

	spin_unlock_irqrestore(&per_cpu(nr_lock, cpu), flags);
}
