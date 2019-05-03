/*
 * tfa_log_user_host.c   tfa98xx logging in sysfs
 *
 * Copyright (c) 2015 NXP Semiconductors
 *
 * Author: Michael Kim <michael.kim@nxp.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/sysfs.h>

#define TFA_CLASS_NAME	"nxp"
#define TFA_LOG_DEV_NAME	"tfa_log"
#define TFA_MAX_DEV_COUNT	4
#define TFA_LOG_MAX_COUNT	4
#define DESC_MAXX_LOG	"maxium of X"
#define DESC_MAXT_LOG	"maximum of T"
#define DESC_OVERXMAX_COUNT	"counter of X > Xmax"
#define DESC_OVERTMAX_COUNT	"counter of T > Tmax"

#define TFA_LOGGING_DEFAULT
#define TFA_LOG_IN_SEPARATE_NODES
#if defined(TFA_LOG_IN_SEPARATE_NODES)
#define FILESIZE_LOG	10
#else
#define FILESIZE_LOG	(10 * TFA_LOG_MAX_COUNT)
#endif
#define TFA_LOG_IN_A_SINGLE_NODE
#define FILESIZE_SINGLE_LOG	(10 * TFA_LOG_MAX_COUNT)

/* ---------------------------------------------------------------------- */

#if defined(TFA_LOG_IN_SEPARATE_NODES)
static ssize_t tfa_data_maxx_show(struct device *dev,
	struct device_attribute *attr, char *buf);
static ssize_t tfa_data_maxx_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size);
static DEVICE_ATTR(data_maxx, S_IRUGO | S_IWUSR | S_IWGRP,
	tfa_data_maxx_show, tfa_data_maxx_store);

static ssize_t tfa_data_maxt_show(struct device *dev,
	struct device_attribute *attr, char *buf);
static ssize_t tfa_data_maxt_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size);
static DEVICE_ATTR(data_maxt, S_IRUGO | S_IWUSR | S_IWGRP,
	tfa_data_maxt_show, tfa_data_maxt_store);

static ssize_t tfa_count_overxmax_show(struct device *dev,
	struct device_attribute *attr, char *buf);
static ssize_t tfa_count_overxmax_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size);
static DEVICE_ATTR(count_overxmax, S_IRUGO | S_IWUSR | S_IWGRP,
	tfa_count_overxmax_show, tfa_count_overxmax_store);

static ssize_t tfa_count_overtmax_show(struct device *dev,
	struct device_attribute *attr, char *buf);
static ssize_t tfa_count_overtmax_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size);
static DEVICE_ATTR(count_overtmax, S_IRUGO | S_IWUSR | S_IWGRP,
	tfa_count_overtmax_show, tfa_count_overtmax_store);
#endif /* TFA_LOG_IN_SEPARATE_NODES */

#if defined(TFA_LOG_IN_A_SINGLE_NODE)
static ssize_t tfa_data_show(struct device *dev,
	struct device_attribute *attr, char *buf);
static ssize_t tfa_data_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size);
static DEVICE_ATTR(data, S_IRUGO | S_IWUSR | S_IWGRP,
	tfa_data_show, tfa_data_store);
#endif /* TFA_LOG_IN_A_SINGLE_NODE */

static ssize_t tfa_log_enable_show(struct device *dev,
	struct device_attribute *attr, char *buf);
static ssize_t tfa_log_enable_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size);
static DEVICE_ATTR(enable, S_IRUGO | S_IWUSR | S_IWGRP,
	tfa_log_enable_show, tfa_log_enable_store);

static ssize_t tfa_num_spk_show(struct device *dev,
	struct device_attribute *attr, char *buf);
static ssize_t tfa_num_spk_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size);
static DEVICE_ATTR(num_spk, S_IRUGO | S_IWUSR | S_IWGRP,
	tfa_num_spk_show, tfa_num_spk_store);

/*
 * to check the data in debug log, in the middle by force
 * without hurting scheme to reset after reading
 */
static ssize_t tfa_log_show(struct device *dev,
	struct device_attribute *attr, char *buf);
static ssize_t tfa_log_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size);
static DEVICE_ATTR(log, S_IRUGO | S_IWUSR | S_IWGRP,
	tfa_log_show, tfa_log_store);

static struct attribute *tfa_log_attr[] = {
#if defined(TFA_LOG_IN_SEPARATE_NODES)
	&dev_attr_data_maxx.attr,
	&dev_attr_data_maxt.attr,
	&dev_attr_count_overxmax.attr,
	&dev_attr_count_overtmax.attr,
#endif
#if defined(TFA_LOG_IN_A_SINGLE_NODE)
	&dev_attr_data.attr,
#endif
	&dev_attr_num_spk.attr,
	&dev_attr_enable.attr,
	&dev_attr_log.attr,
	NULL,
};

static struct attribute_group tfa_log_attr_grp = {
	.attrs = tfa_log_attr,
};

struct tfa_log {
	char *desc;
	uint16_t prev_value[TFA_MAX_DEV_COUNT];
	bool is_max;
	bool is_counter;
	bool is_dirty;
};

/* ---------------------------------------------------------------------- */

struct class *g_nxp_class;
struct device *g_tfa_log_dev;
static int cur_status;
static int devcount = 1; /* mono, by default */
static bool blackbox_enabled;
static struct tfa_log blackbox[TFA_LOG_MAX_COUNT];

/* ---------------------------------------------------------------------- */

/* temporarily until API is ready for driver */
static int tfa_read_log(int dev, uint16_t index, uint16_t *value, bool reset);
static int tfa_update_log(int dev, uint16_t index, uint16_t value);

static int tfa_read_log(int dev, uint16_t index, uint16_t *value, bool reset)
{
	pr_info("%s [%d]: %s\n", __func__, index, blackbox[index].desc);

	if (blackbox[index].is_dirty) {
		pr_info("%s: it's read before updated\n", __func__);
		*value = 0; /* no meaningful data to be updated */
		return 0;
	}

	*value = blackbox[index].prev_value[dev];
	if (*value == 0xffff) {
		pr_info("%s: invalid data\n", __func__);
		return -1;
	}

	if (reset) {
		/* reset the last data */
		blackbox[index].prev_value[dev] = 0;
		blackbox[index].is_dirty = 1;
	}

	return 0;
}

static int tfa_update_log(int dev, uint16_t index, uint16_t value)
{
	if (!blackbox_enabled) {
		pr_info("%s: blackbox is inactive\n", __func__);
		return 0;
	}

	pr_info("blackbox[%d]: input[%d] = %d\n",
		dev, index, value);

	blackbox[index].is_dirty = 0;

	if (blackbox[index].is_max)
		blackbox[index].prev_value[dev] =
			(value > blackbox[index].prev_value[dev])
			? value : blackbox[index].prev_value[dev];
	if (blackbox[index].is_counter)
		blackbox[index].prev_value[dev] += value;

	pr_info("blackbox[%d]: data[%d] = %d\n",
		dev, index, blackbox[index].prev_value[dev]);

	return 0;
}

/* ---------------------------------------------------------------------- */

#if defined(TFA_LOG_IN_SEPARATE_NODES)
static ssize_t tfa_data_maxx_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int idx;
	uint16_t value = 0xffff;
	int size;
	int ret = 0;
	char read_string[FILESIZE_LOG] = {0};

	pr_info("%s: begin\n", __func__);

	pr_info("%s: read from sysfs: %s\n",
		__func__, blackbox[0].desc);

	for (idx = 0; idx < devcount; idx++) {
		/* reset the data in sysfs after reading */
		ret = tfa_read_log(idx, 0, &value, true);
		if (ret) {
			pr_info("%s: failed to read data from sysfs\n",
				__func__);
			continue;
		}

		if (idx == 0)
			snprintf(read_string,
				FILESIZE_LOG, "%d", value);
		else
			snprintf(read_string,
				FILESIZE_LOG, "%s %d",
				read_string, value);
	}

	pr_info("%s: %s\n", __func__, read_string);

	if (ret)
		size = snprintf(buf, FILESIZE_LOG, "data error");
	else
		size = snprintf(buf,
			FILESIZE_LOG, "%s", read_string);

	if (size <= 0) {
		pr_err("%s: failed to show in sysfs file", __func__);
		return -EINVAL;
	}

	pr_info("%s: end (%d)\n", __func__, ret);

	return size;
}

static ssize_t tfa_data_maxx_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int idx, offset = 0, next;
	int value[TFA_MAX_DEV_COUNT] = {0xffff};
	char read_string[FILESIZE_LOG] = {0};

	strncpy(read_string, buf, FILESIZE_LOG);

	pr_info("%s: write to sysfs: %s\n",
		__func__, blackbox[0].desc);

	for (idx = 0; idx < devcount; idx++) {
		sscanf(read_string + offset, "%d %n", &value[idx], &next);
		offset += next;

		tfa_update_log(idx, 0, (uint16_t)value[idx]);
	}

	return size;
}

static ssize_t tfa_data_maxt_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int idx;
	uint16_t value = 0xffff;
	int size;
	int ret = 0;
	char read_string[FILESIZE_LOG] = {0};

	pr_info("%s: begin\n", __func__);

	pr_info("%s: read from sysfs: %s\n",
		__func__, blackbox[1].desc);

	for (idx = 0; idx < devcount; idx++) {
		/* reset the data in sysfs after reading */
		ret = tfa_read_log(idx, 1, &value, true);
		if (ret) {
			pr_info("%s: failed to read data from sysfs\n",
				__func__);
			continue;
		}

		if (idx == 0)
			snprintf(read_string,
				FILESIZE_LOG, "%d", value);
		else
			snprintf(read_string,
				FILESIZE_LOG, "%s %d",
				read_string, value);
	}

	pr_info("%s: %s\n", __func__, read_string);

	if (ret)
		size = snprintf(buf, FILESIZE_LOG, "data error");
	else
		size = snprintf(buf,
			FILESIZE_LOG, "%s", read_string);

	if (size <= 0) {
		pr_err("%s: failed to show in sysfs file", __func__);
		return -EINVAL;
	}

	pr_info("%s: end (%d)\n", __func__, ret);

	return size;
}

static ssize_t tfa_data_maxt_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int idx, offset = 0, next;
	int value[TFA_MAX_DEV_COUNT] = {0xffff};
	char read_string[FILESIZE_LOG] = {0};

	strncpy(read_string, buf, FILESIZE_LOG);

	pr_info("%s: write to sysfs: %s\n",
		__func__, blackbox[1].desc);

	for (idx = 0; idx < devcount; idx++) {
		sscanf(read_string + offset, "%d %n", &value[idx], &next);
		offset += next;

		tfa_update_log(idx, 1, (uint16_t)value[idx]);
	}

	return size;
}

static ssize_t tfa_count_overxmax_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int idx;
	uint16_t value = 0xffff;
	int size;
	int ret = 0;
	char read_string[FILESIZE_LOG] = {0};

	pr_info("%s: begin\n", __func__);

	pr_info("%s: read from sysfs: %s\n",
		__func__, blackbox[2].desc);

	for (idx = 0; idx < devcount; idx++) {
		/* reset the data in sysfs after reading */
		ret = tfa_read_log(idx, 2, &value, true);
		if (ret) {
			pr_info("%s: failed to read data from sysfs\n",
				__func__);
			continue;
		}

		if (idx == 0)
			snprintf(read_string,
				FILESIZE_LOG, "%d", value);
		else
			snprintf(read_string,
				FILESIZE_LOG, "%s %d",
				read_string, value);
	}

	pr_info("%s: %s\n", __func__, read_string);

	if (ret)
		size = snprintf(buf, FILESIZE_LOG, "data error");
	else
		size = snprintf(buf,
			FILESIZE_LOG, "%s", read_string);

	if (size <= 0) {
		pr_err("%s: failed to show in sysfs file", __func__);
		return -EINVAL;
	}

	pr_info("%s: end (%d)\n", __func__, ret);

	return size;
}

static ssize_t tfa_count_overxmax_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int idx, offset = 0, next;
	int value[TFA_MAX_DEV_COUNT] = {0xffff};
	char read_string[FILESIZE_LOG] = {0};

	strncpy(read_string, buf, FILESIZE_LOG);

	pr_info("%s: write to sysfs: %s\n",
		__func__, blackbox[2].desc);

	for (idx = 0; idx < devcount; idx++) {
		sscanf(read_string + offset, "%d %n", &value[idx], &next);
		offset += next;

		tfa_update_log(idx, 2, (uint16_t)value[idx]);
	}

	return size;
}

static ssize_t tfa_count_overtmax_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int idx;
	uint16_t value = 0xffff;
	int size;
	int ret = 0;
	char read_string[FILESIZE_LOG] = {0};

	pr_info("%s: begin\n", __func__);

	pr_info("%s: read from sysfs: %s\n",
		__func__, blackbox[3].desc);

	for (idx = 0; idx < devcount; idx++) {
		/* reset the data in sysfs after reading */
		ret = tfa_read_log(idx, 3, &value, true);
		if (ret) {
			pr_info("%s: failed to read data from sysfs\n",
				__func__);
			continue;
		}

		if (idx == 0)
			snprintf(read_string,
				FILESIZE_LOG, "%d", value);
		else
			snprintf(read_string,
				FILESIZE_LOG, "%s %d",
				read_string, value);
	}

	pr_info("%s: %s\n", __func__, read_string);

	if (ret)
		size = snprintf(buf, FILESIZE_LOG, "data error");
	else
		size = snprintf(buf,
			FILESIZE_LOG, "%s", read_string);

	if (size <= 0) {
		pr_err("%s: failed to show in sysfs file", __func__);
		return -EINVAL;
	}

	pr_info("%s: end (%d)\n", __func__, ret);

	return size;
}

static ssize_t tfa_count_overtmax_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int idx, offset = 0, next;
	int value[TFA_MAX_DEV_COUNT] = {0xffff};
	char read_string[FILESIZE_LOG] = {0};

	strncpy(read_string, buf, FILESIZE_LOG);

	pr_info("%s: write to sysfs: %s\n",
		__func__, blackbox[3].desc);

	for (idx = 0; idx < devcount; idx++) {
		sscanf(read_string + offset, "%d %n", &value[idx], &next);
		offset += next;

		tfa_update_log(idx, 3, (uint16_t)value[idx]);
	}

	return size;
}
#endif /* TFA_LOG_IN_SEPARATE_NODES */

#if defined(TFA_LOG_IN_A_SINGLE_NODE)
static ssize_t tfa_data_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int i, idx;
	uint16_t value = 0xffff;
	int size;
	int ret = 0;
	char read_string[FILESIZE_SINGLE_LOG] = {0};

	pr_info("%s: begin\n", __func__);

	for (i = 0; i < TFA_LOG_MAX_COUNT; i++) {
		pr_info("%s: read from sysfs: %s\n",
			__func__, blackbox[i].desc);

		for (idx = 0; idx < devcount; idx++) {
			/* reset the data in sysfs after reading */
			ret = tfa_read_log(idx, i, &value, true);
			if (ret) {
				pr_info("%s: failed to read data from sysfs\n",
					__func__);
				continue;
			}

			if (i == 0 && idx == 0)
				snprintf(read_string,
					FILESIZE_SINGLE_LOG, "%d", value);
			else
				snprintf(read_string,
					FILESIZE_SINGLE_LOG, "%s %d",
					read_string, value);
		}
	}

	pr_info("%s: %s\n", __func__, read_string);

	if (ret)
		size = snprintf(buf, FILESIZE_SINGLE_LOG, "data error");
	else
		size = snprintf(buf,
			FILESIZE_SINGLE_LOG, "%s", read_string);

	if (size <= 0) {
		pr_err("%s: failed to show in sysfs file", __func__);
		return -EINVAL;
	}

	pr_info("%s: end (%d)\n", __func__, ret);

	return size;
}

static ssize_t tfa_data_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int i, idx, offset = 0, next;
	int value[TFA_MAX_DEV_COUNT] = {0xffff};
	char read_string[FILESIZE_SINGLE_LOG] = {0};

	strncpy(read_string, buf, FILESIZE_SINGLE_LOG);

	for (i = 0; i < TFA_LOG_MAX_COUNT; i++) {
		pr_info("%s: write to sysfs: %s\n",
			__func__, blackbox[i].desc);

		for (idx = 0; idx < devcount; idx++) {
			sscanf(read_string + offset, "%d %n", &value[idx], &next);
			offset += next;

			tfa_update_log(idx, i, (uint16_t)value[idx]);
		}
	}

	return size;
}
#endif /* TFA_LOG_IN_A_SINGLE_NODE */

static ssize_t tfa_log_enable_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int size;

	size = snprintf(buf, 25, "%s\n", blackbox_enabled ?
		"blackbox is active" : "blackbox is inactive");

	return size;
}

static ssize_t tfa_log_enable_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int ret = 0;
	int status;

	/* Compare string, excluding the trailing \0 and the potentials eol */
	if (!sysfs_streq(buf, "1") && !sysfs_streq(buf, "0")) {
		pr_debug("%s: invalid value to write\n",
			__func__);
		return -EINVAL;
	}

	ret = kstrtou32(buf, 10, &status);
	blackbox_enabled = (status) ? true : false;

	return size;
}

static ssize_t tfa_num_spk_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int size;

	size = snprintf(buf, 25, "%d\n", devcount);

	return size;
}

static ssize_t tfa_num_spk_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int ret = 0;
	int num_spk;

	/* Compare string, excluding the trailing \0 and the potentials eol */
	if (!sysfs_streq(buf, "1") && !sysfs_streq(buf, "2")
		&& !sysfs_streq(buf, "3") && !sysfs_streq(buf, "4")) {
		pr_debug("%s: invalid value to write\n",
			__func__);
		return -EINVAL;
	}

	ret = kstrtou32(buf, 10, &num_spk);
	devcount = num_spk;

	return size;
}

static ssize_t tfa_log_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int size;

	size = snprintf(buf, 25, "%s\n", cur_status ?
		"sysfs log is active" : "sysfs log is inactive");

	return size;
}

static ssize_t tfa_log_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int i, idx;
	int ret = 0;
	int status;
	char read_string[FILESIZE_LOG] = {0};

	/* Compare string, excluding the trailing \0 and the potentials eol */
	if (!sysfs_streq(buf, "1") && !sysfs_streq(buf, "0")) {
		pr_debug("%s: invalid value to write\n",
			__func__);
		return -EINVAL;
	}

	ret = kstrtou32(buf, 10, &status);
	if (!status) {
		pr_info("%s: do nothing\n", __func__);
		return -EINVAL;
	}
	if (cur_status)
		pr_info("%s: prior writing still runs\n", __func__);

	pr_info("%s: begin\n", __func__);

	cur_status = status; /* run - changed to active */
	for (i = 0; i < TFA_LOG_MAX_COUNT; i++) {
		pr_info("%s: read from sysfs: %s\n",
			__func__, blackbox[i].desc);

		for (idx = 0; idx < devcount; idx++) {
			if (idx == 0)
				snprintf(read_string,
					FILESIZE_LOG, "%d",
					blackbox[i].prev_value[idx]);
			else
				snprintf(read_string,
					FILESIZE_LOG, "%s %d",
					read_string, blackbox[i].prev_value[idx]);
		}

		pr_info("%s: %s\n", __func__, read_string);
	}
	cur_status = 0; /* done - changed to inactive */

	pr_info("%s: end \n", __func__);

	return size;
}

static int __init tfa98xx_log_init(void)
{
	int ret = 0;

	if (!g_nxp_class)
		g_nxp_class = class_create(THIS_MODULE, TFA_CLASS_NAME);
	if (g_nxp_class) {
		g_tfa_log_dev = device_create(g_nxp_class,
			NULL, 2, NULL, TFA_LOG_DEV_NAME);
		if (!IS_ERR(g_tfa_log_dev)) {
			ret = sysfs_create_group(&g_tfa_log_dev->kobj,
				&tfa_log_attr_grp);
			if (ret)
				pr_err("%s: failed to create sysfs group. ret (%d)\n",
					__func__, ret);
		} else {
			class_destroy(g_nxp_class);
		}
	}

#if defined(TFA_LOGGING_DEFAULT)
	blackbox_enabled = true; /* enable by default */
#else
	blackbox_enabled = false; /* control with sysfs node */
#endif

	/* maximum x */
	blackbox[0].desc = DESC_MAXX_LOG;
	blackbox[0].is_max = true;
	blackbox[0].is_counter = false;
	/* maximum t */
	blackbox[1].desc = DESC_MAXT_LOG;
	blackbox[1].is_max = true;
	blackbox[1].is_counter = false;
	/* counter x > x_max */
	blackbox[2].desc = DESC_OVERXMAX_COUNT;
	blackbox[2].is_max = false;
	blackbox[2].is_counter = true;
	/* counter t > t_max */
	blackbox[3].desc = DESC_OVERTMAX_COUNT;
	blackbox[3].is_max = false;
	blackbox[3].is_counter = true;

	pr_info("%s: g_nxp_class=%p\n", __func__, g_nxp_class);
	pr_info("%s: initialized\n", __func__);

	return ret;
}
module_init(tfa98xx_log_init);

static void __exit tfa98xx_log_exit(void)
{
	device_destroy(g_nxp_class, 2);
	class_destroy(g_nxp_class);
	pr_info("exited\n");
}
module_exit(tfa98xx_log_exit);

MODULE_DESCRIPTION("ASoC TFA98XX logging driver");
MODULE_LICENSE("GPL");
