/*
 * exynos_thermal_common.c - Samsung EXYNOS common thermal file
 *
 *  Copyright (C) 2013 Samsung Electronics
 *  Amit Daniel Kachhap <amit.daniel@samsung.com>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/cpu_cooling.h>
#include <linux/gpu_cooling.h>
#include <linux/isp_cooling.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <linux/thermal.h>
#include <linux/pm_qos.h>
#include <linux/cpufreq.h>
#include <soc/samsung/cpufreq.h>

#include "exynos_thermal_common.h"

#if defined(CONFIG_GPU_THERMAL)
extern int gpu_dvfs_get_max_freq(void);
extern int gpu_dvfs_get_min_freq(void);
#endif

unsigned long cpu_max_temp[2];

struct exynos_thermal_zone {
	enum thermal_device_mode mode;
	struct thermal_zone_device *therm_dev;
	struct thermal_cooling_device *cool_dev[MAX_COOLING_DEVICE];
	unsigned int cool_dev_size;
	struct platform_device *exynos4_dev;
	struct thermal_sensor_conf *sensor_conf;
	bool bind;
};

static DEFINE_MUTEX (thermal_suspend_lock);
static bool suspended;
static bool is_cpu_hotplugged_out;

/* Get mode callback functions for thermal zone */
static int exynos_get_mode(struct thermal_zone_device *thermal,
			enum thermal_device_mode *mode)
{
	struct exynos_thermal_zone *th_zone = thermal->devdata;
	if (th_zone)
		*mode = th_zone->mode;
	return 0;
}

/* Set mode callback functions for thermal zone */
static int exynos_set_mode(struct thermal_zone_device *thermal,
			enum thermal_device_mode mode)
{
	struct exynos_thermal_zone *th_zone = thermal->devdata;
	if (!th_zone) {
		dev_err(&thermal->device,
			"thermal zone not registered\n");
		return 0;
	}

	mutex_lock(&thermal->lock);

	if (mode == THERMAL_DEVICE_ENABLED &&
		!th_zone->sensor_conf->trip_data.trigger_falling)
		thermal->polling_delay = IDLE_INTERVAL;
	else
		thermal->polling_delay = 0;

	mutex_unlock(&thermal->lock);

	th_zone->mode = mode;
	thermal_zone_device_update(thermal);
	dev_dbg(th_zone->sensor_conf->dev,
		"thermal polling set for duration=%d msec\n",
		thermal->polling_delay);
	return 0;
}


/* Get trip type callback functions for thermal zone */
static int exynos_get_trip_type(struct thermal_zone_device *thermal, int trip,
				 enum thermal_trip_type *type)
{
	struct exynos_thermal_zone *th_zone = thermal->devdata;
	int max_trip = th_zone->sensor_conf->trip_data.trip_count;
	int trip_type;

	if (trip < 0 || trip >= max_trip)
		return -EINVAL;

	trip_type = th_zone->sensor_conf->trip_data.trip_type[trip];

	if (trip_type == SW_TRIP)
		*type = THERMAL_TRIP_CRITICAL;
	else if (trip_type == THROTTLE_ACTIVE)
		*type = THERMAL_TRIP_ACTIVE;
	else if (trip_type == THROTTLE_PASSIVE)
		*type = THERMAL_TRIP_PASSIVE;
	else
		return -EINVAL;

	return 0;
}

/* Get trip temperature callback functions for thermal zone */
static int exynos_get_trip_temp(struct thermal_zone_device *thermal, int trip,
				unsigned long *temp)
{
	struct exynos_thermal_zone *th_zone = thermal->devdata;
	int max_trip = th_zone->sensor_conf->trip_data.trip_count;

	if (trip < 0 || trip >= max_trip)
		return -EINVAL;

	*temp = th_zone->sensor_conf->trip_data.trip_val[trip];
	/* convert the temperature into millicelsius */
	*temp = *temp * MCELSIUS;

	return 0;
}

/* Get critical temperature callback functions for thermal zone */
static int exynos_get_crit_temp(struct thermal_zone_device *thermal,
				unsigned long *temp)
{
	struct exynos_thermal_zone *th_zone = thermal->devdata;
	int max_trip = th_zone->sensor_conf->trip_data.trip_count;
	/* Get the temp of highest trip*/
	return exynos_get_trip_temp(thermal, max_trip - 1, temp);
}

/* Bind callback functions for thermal zone */
static int exynos_bind(struct thermal_zone_device *thermal,
			struct thermal_cooling_device *cdev)
{
	int ret = 0, i, tab_size, level;
	struct freq_clip_table *tab_ptr, *clip_data;
	struct exynos_thermal_zone *th_zone = thermal->devdata;
	struct thermal_sensor_conf *data = th_zone->sensor_conf;
	enum thermal_trip_type type;
#ifdef CONFIG_ARM_EXYNOS_MP_CPUFREQ
	struct cpufreq_policy policy;
#endif
#ifdef CONFIG_GPU_THERMAL
	int gpu_max_freq, gpu_min_freq;
#endif

	tab_ptr = (struct freq_clip_table *)data->cooling_data.freq_data;
	tab_size = data->cooling_data.freq_clip_count;

	if (tab_ptr == NULL || tab_size == 0)
		return 0;

	/* find the cooling device registered*/
	for (i = 0; i < th_zone->cool_dev_size; i++)
		if (cdev == th_zone->cool_dev[i])
			break;

	/* No matching cooling device */
	if (i == th_zone->cool_dev_size)
		return 0;

	/* Bind the thermal zone to the cpufreq cooling device */
	for (i = 0; i < tab_size; i++) {
		clip_data = (struct freq_clip_table *)&(tab_ptr[i]);

#ifdef CONFIG_ARM_EXYNOS_MP_CPUFREQ
		if(data->d_type == CLUSTER0 || data->d_type == CLUSTER1) {
			ret = cpufreq_get_policy(&policy, data->d_type);
			if (ret)
				return -EINVAL;

			if (clip_data->freq_clip_max > policy.max) {
				pr_warn("%s: Throttling freq(%d) is greater than policy max(%d)\n", __func__, clip_data->freq_clip_max, policy.max);
				clip_data->freq_clip_max = policy.max;
			 } else if (clip_data->freq_clip_max < policy.min){
				pr_warn("%s: Throttling freq(%d) is lower than policy min(%d)\n", __func__, clip_data->freq_clip_max, policy.min);
				clip_data->freq_clip_max = policy.min;
			 }
		}
#endif

#ifdef CONFIG_GPU_THERMAL
		if(data->d_type == GPU) {
			gpu_max_freq = gpu_dvfs_get_max_freq() * 1000;
			gpu_min_freq = gpu_dvfs_get_min_freq() * 1000;

			if(gpu_max_freq > 0 && gpu_min_freq > 0) {
				if (clip_data->freq_clip_max > gpu_max_freq) {
					pr_warn("%s: GPU Throttling freq(%d) is greater than max(%d)\n", __func__, clip_data->freq_clip_max, gpu_max_freq);
					clip_data->freq_clip_max = gpu_max_freq;
				 } else if (clip_data->freq_clip_max < gpu_min_freq){
					pr_warn("%s: GPU Throttling freq(%d) is lower than min(%d)\n", __func__, clip_data->freq_clip_max, gpu_min_freq);
					clip_data->freq_clip_max = gpu_min_freq;
				 }
			}
		}
#endif

		if (data->d_type == CLUSTER0)
			level = cpufreq_cooling_get_level(0, clip_data->freq_clip_max);
		else if (data->d_type == CLUSTER1)
			level = cpufreq_cooling_get_level(4, clip_data->freq_clip_max);
		else if (data->d_type == GPU)
			level = gpufreq_cooling_get_level(0, clip_data->freq_clip_max);
		else if (data->d_type == ISP)
			level = isp_cooling_get_fps(0, clip_data->freq_clip_max);
		else
			level = (int)THERMAL_CSTATE_INVALID;

		if (level == THERMAL_CSTATE_INVALID) {
			pr_warn("Bind fail is occurred because level is invalid.\n");
			return 0;
		}

		exynos_get_trip_type(thermal, i, &type);

		switch (GET_ZONE(type)) {
		case MONITOR_ZONE:
		case WARN_ZONE:
			if (thermal_zone_bind_cooling_device(thermal, i, cdev,
								level, 0)) {
				dev_err(data->dev,
					"error unbinding cdev inst=%d\n", i);
				ret = -EINVAL;
			}
			th_zone->bind = true;
			break;
		default:
			ret = -EINVAL;
		}
	}

	return ret;
}

/* Unbind callback functions for thermal zone */
static int exynos_unbind(struct thermal_zone_device *thermal,
			struct thermal_cooling_device *cdev)
{
	int ret = 0, i, tab_size;
	struct exynos_thermal_zone *th_zone = thermal->devdata;
	struct thermal_sensor_conf *data = th_zone->sensor_conf;
	enum thermal_trip_type type;

	if (th_zone->bind == false)
		return 0;

	tab_size = data->cooling_data.freq_clip_count;

	if (tab_size == 0)
		return 0;

	/* find the cooling device registered*/
	for (i = 0; i < th_zone->cool_dev_size; i++)
		if (cdev == th_zone->cool_dev[i])
			break;

	/* No matching cooling device */
	if (i == th_zone->cool_dev_size)
		return 0;

	/* Bind the thermal zone to the cpufreq cooling device */
	for (i = 0; i < tab_size; i++) {
		exynos_get_trip_type(thermal, i, &type);

		switch (GET_ZONE(type)) {
		case MONITOR_ZONE:
		case WARN_ZONE:
			if (thermal_zone_unbind_cooling_device(thermal, i,
								cdev)) {
				dev_err(data->dev,
					"error unbinding cdev inst=%d\n", i);
				ret = -EINVAL;
			}
			th_zone->bind = false;
			break;
		default:
			ret = -EINVAL;
		}
	}
	return ret;
}

#ifdef CONFIG_CPU_THERMAL
extern int cpufreq_set_cur_temp(bool suspended, unsigned long temp);
#else
static inline int cpufreq_set_cur_temp(bool suspended, unsigned long temp) { return 0; }
#endif
#ifdef CONFIG_GPU_THERMAL
extern int gpufreq_set_cur_temp(bool suspended, unsigned long temp);
#else
static inline int gpufreq_set_cur_temp(bool suspended, unsigned long temp) { return 0; }
#endif
#ifdef CONFIG_ISP_THERMAL
extern int isp_set_cur_temp(bool suspended, unsigned long temp);
#else
static inline int isp_set_cur_temp(bool suspended, unsigned long temp) { return 0; }
#endif

/* Get temperature callback functions for thermal zone */
static int exynos_get_temp(struct thermal_zone_device *thermal,
			unsigned long *temp)
{
	struct exynos_thermal_zone *th_zone = thermal->devdata;
	void *data;
	unsigned long max_temp;

	if (!th_zone->sensor_conf) {
		dev_err(&thermal->device,
			"Temperature sensor not initialised\n");
		return -EINVAL;
	}
	data = th_zone->sensor_conf->driver_data;
	*temp = th_zone->sensor_conf->read_temperature(data);

	/* convert the temperature into millicelsius */
	*temp = *temp * MCELSIUS;

	mutex_lock(&thermal_suspend_lock);
	if (th_zone->sensor_conf->d_type == CLUSTER0 || th_zone->sensor_conf->d_type == CLUSTER1) {
		cpu_max_temp[th_zone->sensor_conf->d_type] = *temp;
		max_temp = max(cpu_max_temp[CLUSTER0], cpu_max_temp[CLUSTER1]);
		cpufreq_set_cur_temp(suspended, max_temp / 1000);
	} else if (th_zone->sensor_conf->d_type == GPU)
		gpufreq_set_cur_temp(suspended, *temp / 1000);
	else if (th_zone->sensor_conf->d_type == ISP)
		isp_set_cur_temp(suspended, *temp / 1000);
	mutex_unlock(&thermal_suspend_lock);

	return 0;
}

/* Get temperature callback functions for thermal zone */
static int exynos_set_emul_temp(struct thermal_zone_device *thermal,
						unsigned long temp)
{
	void *data;
	int ret = -EINVAL;
	struct exynos_thermal_zone *th_zone = thermal->devdata;

	if (!th_zone->sensor_conf) {
		dev_err(&thermal->device,
			"Temperature sensor not initialised\n");
		return -EINVAL;
	}
	data = th_zone->sensor_conf->driver_data;
	if (th_zone->sensor_conf->write_emul_temp)
		ret = th_zone->sensor_conf->write_emul_temp(data, temp);
	return ret;
}

/* Get the temperature trend */
static int exynos_get_trend(struct thermal_zone_device *thermal,
			int trip, enum thermal_trend *trend)
{
	int ret;
	unsigned long trip_temp;

	ret = exynos_get_trip_temp(thermal, trip, &trip_temp);
	if (ret < 0)
		return ret;

	if (thermal->temperature >= trip_temp)
		*trend = THERMAL_TREND_RAISE_FULL;
	else
		*trend = THERMAL_TREND_DROP_FULL;

	return 0;
}

struct pm_qos_request thermal_cpu_hotplug_request;
static int exynos_throttle_cpu_hotplug(struct thermal_zone_device *thermal)
{
	struct exynos_thermal_zone *th_zone = thermal->devdata;
	struct thermal_sensor_conf *data = th_zone->sensor_conf;
	struct cpufreq_cooling_device *cpufreq_device = (struct cpufreq_cooling_device *)th_zone->cool_dev[0]->devdata;
	int ret = 0;
	int cur_temp = 0;

	if (!thermal->temperature)
		return -EINVAL;

	cur_temp = thermal->temperature / MCELSIUS;

	if (is_cpu_hotplugged_out) {
		if (cur_temp < data->hotplug_in_threshold) {
			/*
			 * If current temperature is lower than low threshold,
			 * call cluster1_cores_hotplug(false) for hotplugged out cpus.
			 */
			pm_qos_update_request(&thermal_cpu_hotplug_request, NR_CPUS);
			is_cpu_hotplugged_out = false;
			cpufreq_device->cpufreq_state = 0;
		}
	} else {
		if (cur_temp >= data->hotplug_out_threshold) {
			/*
			 * If current temperature is higher than high threshold,
			 * call cluster1_cores_hotplug(true) to hold temperature down.
			 */
			pm_qos_update_request(&thermal_cpu_hotplug_request, NR_CLUST1_CPUS);
			is_cpu_hotplugged_out = true;
		}
	}

	return ret;
}

/* Operation callback functions for thermal zone */
static struct thermal_zone_device_ops exynos_dev_ops = {
	.bind = exynos_bind,
	.unbind = exynos_unbind,
	.get_temp = exynos_get_temp,
	.set_emul_temp = exynos_set_emul_temp,
	.get_trend = exynos_get_trend,
	.get_mode = exynos_get_mode,
	.set_mode = exynos_set_mode,
	.get_trip_type = exynos_get_trip_type,
	.get_trip_temp = exynos_get_trip_temp,
	.get_crit_temp = exynos_get_crit_temp,
};

/* Operation callback functions for thermal zone */
static struct thermal_zone_device_ops exynos_dev_hotplug_ops = {
	.bind = exynos_bind,
	.unbind = exynos_unbind,
	.get_temp = exynos_get_temp,
	.set_emul_temp = exynos_set_emul_temp,
	.get_trend = exynos_get_trend,
	.get_mode = exynos_get_mode,
	.set_mode = exynos_set_mode,
	.get_trip_type = exynos_get_trip_type,
	.get_trip_temp = exynos_get_trip_temp,
	.get_crit_temp = exynos_get_crit_temp,
	.throttle_cpu_hotplug = exynos_throttle_cpu_hotplug,
};

/*
 * This function may be called from interrupt based temperature sensor
 * when threshold is changed.
 */
void exynos_report_trigger(struct thermal_sensor_conf *conf)
{
	unsigned int i;
	char data[10];
	char *envp[] = { data, NULL };
	struct exynos_thermal_zone *th_zone;

	if (!conf || !conf->pzone_data) {
		pr_err("Invalid temperature sensor configuration data\n");
		return;
	}

	th_zone = conf->pzone_data;

	if (th_zone->bind == false) {
		for (i = 0; i < th_zone->cool_dev_size; i++) {
			if (!th_zone->cool_dev[i])
				continue;
			exynos_bind(th_zone->therm_dev,
					th_zone->cool_dev[i]);
		}
	}

	thermal_zone_device_update(th_zone->therm_dev);

	mutex_lock(&th_zone->therm_dev->lock);
	/* Find the level for which trip happened */
	for (i = 0; i < th_zone->sensor_conf->trip_data.trip_count; i++) {
		if (th_zone->therm_dev->last_temperature <
			th_zone->sensor_conf->trip_data.trip_val[i] * MCELSIUS)
			break;
	}

	if (th_zone->mode == THERMAL_DEVICE_ENABLED &&
		!th_zone->sensor_conf->trip_data.trigger_falling) {
		if (i > 0)
			th_zone->therm_dev->polling_delay = ACTIVE_INTERVAL;
		else
			th_zone->therm_dev->polling_delay = IDLE_INTERVAL;
	}

	snprintf(data, sizeof(data), "%u", i);
	kobject_uevent_env(&th_zone->therm_dev->device.kobj, KOBJ_CHANGE, envp);
	mutex_unlock(&th_zone->therm_dev->lock);
}

static int exynos_pm_notifier(struct notifier_block *notifier,
			unsigned long event, void *v)
{
	switch (event) {
	case PM_SUSPEND_PREPARE:
		mutex_lock(&thermal_suspend_lock);
		suspended = true;
		cpufreq_set_cur_temp(suspended, 0);
		gpufreq_set_cur_temp(suspended, 0);
		isp_set_cur_temp(suspended, 0);
		mutex_unlock(&thermal_suspend_lock);
		break;
	case PM_POST_SUSPEND:
		mutex_lock(&thermal_suspend_lock);
		suspended = false;
		mutex_unlock(&thermal_suspend_lock);
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block exynos_tmu_pm_notifier = {
	.notifier_call = exynos_pm_notifier,
};

#if defined(CONFIG_GPU_THERMAL) && defined(CONFIG_MALI_DEBUG_KERNEL_SYSFS)
struct thermal_sensor_conf *gpu_thermal_conf_ptr = NULL;
#endif

/* Register with the in-kernel thermal management */
int exynos_register_thermal(struct thermal_sensor_conf *sensor_conf)
{
	int ret, cpu;
	struct cpumask mask_val;
	struct exynos_thermal_zone *th_zone;
	struct thermal_zone_device_ops *dev_ops;

	if (!sensor_conf || !sensor_conf->read_temperature) {
		pr_err("Temperature sensor not initialised\n");
		return -EINVAL;
	}

	th_zone = devm_kzalloc(sensor_conf->dev,
				sizeof(struct exynos_thermal_zone), GFP_KERNEL);
	if (!th_zone)
		return -ENOMEM;

	th_zone->sensor_conf = sensor_conf;
	cpumask_clear(&mask_val);

	for_each_possible_cpu(cpu) {
		if (cpu_topology[cpu].cluster_id == sensor_conf->id) {
			cpumask_copy(&mask_val, topology_core_cpumask(cpu));
			break;
		}
	}

	/*
	 * TODO: 1) Handle multiple cooling devices in a thermal zone
	 *	 2) Add a flag/name in cooling info to map to specific
	 *	 sensor
	 */
	if (sensor_conf->cooling_data.freq_clip_count > 0) {
		if (sensor_conf->d_type == CLUSTER0 || sensor_conf->d_type == CLUSTER1) {
			th_zone->cool_dev[th_zone->cool_dev_size] =
					cpufreq_cooling_register(&mask_val);
		} else if (sensor_conf->d_type ==  GPU) {
			th_zone->cool_dev[th_zone->cool_dev_size] =
					gpufreq_cooling_register(&mask_val);
#if defined(CONFIG_GPU_THERMAL) && defined(CONFIG_MALI_DEBUG_KERNEL_SYSFS)
			gpu_thermal_conf_ptr = sensor_conf;
#endif
		} else if (sensor_conf->d_type ==  ISP) {
			th_zone->cool_dev[th_zone->cool_dev_size] =
					isp_cooling_register(&mask_val);
		}
		if (IS_ERR(th_zone->cool_dev[th_zone->cool_dev_size])) {
			dev_err(sensor_conf->dev,
				"Failed to register cpufreq cooling device\n");
			ret = -EINVAL;
			goto err_unregister;
		}
		th_zone->cool_dev_size++;
	}

	/* Add hotplug function ops */
	if (sensor_conf->hotplug_enable) {
		dev_ops = &exynos_dev_hotplug_ops;
		pm_qos_add_request(&thermal_cpu_hotplug_request, PM_QOS_CPU_ONLINE_MAX,
					PM_QOS_CPU_ONLINE_MAX_DEFAULT_VALUE);
	} else
		dev_ops = &exynos_dev_ops;

	th_zone->therm_dev = thermal_zone_device_register(
			sensor_conf->name, sensor_conf->trip_data.trip_count,
			0, th_zone, dev_ops, NULL, 0,
			sensor_conf->trip_data.trigger_falling ? 0 :
			IDLE_INTERVAL);

	if (IS_ERR(th_zone->therm_dev)) {
		dev_err(sensor_conf->dev,
			"Failed to register thermal zone device\n");
		ret = PTR_ERR(th_zone->therm_dev);
		goto err_unregister;
	}
	th_zone->mode = THERMAL_DEVICE_ENABLED;
	sensor_conf->pzone_data = th_zone;

	if (sensor_conf->id == 0)
		register_pm_notifier(&exynos_tmu_pm_notifier);

	dev_info(sensor_conf->dev,
		"Exynos: Thermal zone(%s) registered\n", sensor_conf->name);

	return 0;

err_unregister:
	exynos_unregister_thermal(sensor_conf);
	return ret;
}

/* Un-Register with the in-kernel thermal management */
void exynos_unregister_thermal(struct thermal_sensor_conf *sensor_conf)
{
	int i;
	struct exynos_thermal_zone *th_zone;

	if (!sensor_conf || !sensor_conf->pzone_data) {
		pr_err("Invalid temperature sensor configuration data\n");
		return;
	}

#if defined(CONFIG_GPU_THERMAL) && defined(CONFIG_MALI_DEBUG_KERNEL_SYSFS)
	gpu_thermal_conf_ptr = NULL;
#endif

	th_zone = sensor_conf->pzone_data;

	thermal_zone_device_unregister(th_zone->therm_dev);

	for (i = 0; i < th_zone->cool_dev_size; i++) {
		if (sensor_conf->d_type == CLUSTER0 || sensor_conf->d_type == CLUSTER1)
			cpufreq_cooling_unregister(th_zone->cool_dev[i]);
		else if (sensor_conf->d_type == GPU)
			gpufreq_cooling_unregister(th_zone->cool_dev[i]);
		else if (sensor_conf->d_type == ISP)
			isp_cooling_unregister(th_zone->cool_dev[i]);
	}

	if (sensor_conf->id == 0)
		unregister_pm_notifier(&exynos_tmu_pm_notifier);

	dev_info(sensor_conf->dev,
		"Exynos: Kernel Thermal management unregistered\n");
}
