/* linux/drivers/video/fbdev/exynos/decon/panels/backlight_tuning.c
 *
 * Copyright (c) 2017 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/debugfs.h>
#include <linux/sort.h>
#include <linux/ctype.h>
#include <linux/slab.h>

#include "backlight_tuning.h"

/*
* bl tuning usage
* echo min def max out ... (, min def max out ... for platform brightness. if you skip it, we reuse binary default value) > /d/bl_curve/bl_tuning
* , (comma) is delemeter to seperate platform brightnes
* ex) echo 1 2 3 > /d/bl_curve/bl_tuning
* ex) echo 1 2 3 4 > /d/bl_curve/bl_tuning
* ex) echo 1 2 3, 4 5 6 > /d/bl_curve/bl_tuning
* ex) echo 1 2 3 4, 5 6 7 8 > /d/bl_curve/bl_tuning = this means 1 2 3 4 for tune_value, and 5 6 7 8 for brightness
*
* ex) echo 1 3 2 = 1 2 3 because we re-order it automatically
* ex) echo 2 1 3, 6 5 4 =  1 2 3, 4 5 6 because we re-order it automatically
*
* ex) echo 1 2 3 4, 5 6 7 > /d/bl_curve/bl_tuning (X) because tune_value count is 4, but brightness count is 3
* ex) echo 1 2 3 4, 5 6 7, 8 9 10 > /d/bl_curve/bl_tuning (X) because we allow only one ,(comma) to seperate platform brightness point
*
*/

/* ic tuning usage
* echo addr (value. if you skip it, we regard this is read mode) > /d/bl_curve/ic_tuning-i2c_client->name

* ex) echo 0x1 > /d/bl_curve/ic_tuning-i2c_client
* = this means you assign read address as 0x1 when you cat this sysfs next time
* ex) echo 0x1 0x2 > /d/bl_curve/ic_tuning-i2c_client
* = this means you write to 0x01(address) with 0x02(value) and you assign read address as 0x1 when you cat this sysfs next time
*/

#if defined(CONFIG_DEBUG_FS) && defined(CONFIG_SEC_GPIO_DVS) && !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
#define BL_POINTS	\
	X(OUT)		\
	X(MAX)		\
	X(DFT)		\
	X(MIN)		\
	X(OFF)		\
	X(END)

#define X(a)	BL_POINT_##a,
enum {	BL_POINTS	};
#undef X

#define X(a)	#a,
static char *BL_POINT_NAME[] = { BL_POINTS };
#undef X

#define INPUT_LIMIT	80

struct bl_info {
	struct backlight_device *bd;
	unsigned int *brightness_table;

	unsigned int *default_level;
	unsigned int *default_value;

	unsigned int tune_value[INPUT_LIMIT];
	unsigned int tune_level[INPUT_LIMIT];
};

struct ic_info {
	struct backlight_device *bd;
	struct i2c_client *client;
	u8 command;
};

static void make_bl_default_point(struct bl_info *bl)
{
	int i;

	if (IS_ERR_OR_NULL(bl->default_level)) {
		bl->default_level = kcalloc(BL_POINT_END, sizeof(unsigned int), GFP_KERNEL);

		bl->default_level[BL_POINT_OFF] = 0;
		bl->default_level[BL_POINT_MIN] = 1;
		bl->default_level[BL_POINT_DFT] = bl->bd->props.brightness;
		bl->default_level[BL_POINT_MAX] = 255;
		bl->default_level[BL_POINT_OUT] = bl->bd->props.max_brightness;

		for (i = 0; i <= bl->default_level[BL_POINT_MAX]; i++) {
			if (bl->brightness_table[i]) {
				bl->default_level[BL_POINT_MIN] = i;
				break;
			}
		}
	}

	if (IS_ERR_OR_NULL(bl->default_value)) {
		bl->default_value = kcalloc(BL_POINT_END, sizeof(unsigned int), GFP_KERNEL);

		for (i = 0; i < BL_POINT_END; i++)
			bl->default_value[i] = bl->brightness_table[bl->default_level[i]];
	}
}

static int cmp_number(const void *a, const void *b)
{
	return -(*(unsigned int *)a - *(unsigned int *)b);
}

static unsigned int parse_curve(char *str, char *delim, unsigned int *out)
{
	unsigned int i = 0;
	char *p = NULL;
	int rc;

	if (!str) {
		pr_info("%s: str is invalid\n", __func__);
		return i;
	}

	/* trim */
	for (i = 0; str[i]; i++) {
		if (isdigit(str[i]))
			break;
	}
	str = str + i;

	/* seperate with delimiter */
	i = 0;
	while ((p = strsep(&str, delim)) != NULL) {
		rc = kstrtouint(p, 0, out + i);
		if (rc < 0)
			break;
		i++;
	}

	/* big -> small */
	sort(out, i, sizeof(unsigned int), cmp_number, NULL);

	return i;
}

static int make_bl_curve(struct bl_info *bl, unsigned int *bl_value, unsigned int *bl_level)
{
	int i, idx;
	unsigned int value;

	for (i = 0; i <= bl_level[0]; i++) {
		for (idx = 0; bl_level[idx]; idx++) {
			if (i >= bl_level[idx])
				break;
		}

		if (i >= 255 || bl_value[idx] == 0)	/* flat */
			value = bl_value[idx];
		else if (i >= bl_level[idx])
			value = (i - bl_level[idx]) * (bl_value[idx - 1] - bl_value[idx]) / (bl_level[idx - 1] - bl_level[idx]) + bl_value[idx];
		else
			value = 0;

		dev_info(&bl->bd->dev, "[%3d] = %3d, %3d\t%s\n", i, bl->brightness_table[i], value, (value != bl->brightness_table[i]) ? "X" : "");
		bl->brightness_table[i] = value;
	}

	memset(bl->tune_value, 0, sizeof(bl->tune_value));
	memset(bl->tune_level, 0, sizeof(bl->tune_level));

	for (i = 0; bl_level[i]; i++) {
		bl->tune_value[i] = bl_value[i];
		bl->tune_level[i] = bl_level[i];
	}

	return 0;
}

static int check_curve(struct bl_info *bl, char *str)
{
	int i, ret = 0;
	unsigned int bl_level[INPUT_LIMIT] = {0, };
	unsigned int bl_value[INPUT_LIMIT] = {0, };
	unsigned int max_bl_level;
	unsigned int max_bl_value;
	unsigned int off_bl_point = BL_POINT_END - 1;
	char *pos = NULL;

	pos = strrchr(str, ',');
	if (pos) {
		dev_info(&bl->bd->dev, "%s: we found comma for brightness level\n", __func__);
		*pos = '\0';
		pos++;
	}

	max_bl_value = parse_curve(str, " ", bl_value);
	max_bl_level = parse_curve(pos, " ", bl_level);

	if (bl->bd->props.max_brightness <= 255)
		off_bl_point -= 1;

	dev_info(&bl->bd->dev, "max_bl_value: %d, max_bl_level: %d, off_bl_point: %d\n", max_bl_value, max_bl_level, off_bl_point);

	if (max_bl_level && (max_bl_value != max_bl_level)) {
		dev_info(&bl->bd->dev, "max_bl_value: %d, max_bl_level: %d, mismatch\n", max_bl_value, max_bl_level);
		ret = -EINVAL;
		goto exit;
	}

	if (!max_bl_level) {
		dev_info(&bl->bd->dev, "max_bl_level: %d, so we make default bl_level\n", max_bl_value);
		for (i = 0; i <= off_bl_point; i++)
			bl_level[i] = bl->default_level[i];

		max_bl_level = off_bl_point;
	}

	if (!max_bl_value || !max_bl_level) {
		dev_info(&bl->bd->dev, "max_bl_value: %d, max_bl_level: %d, should not be zero\n", max_bl_value, max_bl_level);
		ret = -EINVAL;
		goto exit;
	}

	if (!max_bl_level && (max_bl_value == off_bl_point - 1) && bl_value[off_bl_point] != 0) {
		dev_info(&bl->bd->dev, "off_bl_value[%d]: %d, off_bl_point may not exist, so we add it\n", off_bl_point, bl_value[off_bl_point]);
		max_bl_value++;
	}

	if (bl_level[0] > bl->bd->props.max_brightness) {
		dev_info(&bl->bd->dev, "top_bl_level: %d, it should be smaller than %d\n", bl_level[0], bl->bd->props.max_brightness);
		ret = -EINVAL;
		goto exit;
	}

	dev_info(&bl->bd->dev, "max_bl_value: %d, max_bl_level: %d, off_bl_point: %d\n", max_bl_value, max_bl_level, off_bl_point);

	for (i = max_bl_value; i >= 0; i--)
		dev_info(&bl->bd->dev, "bl_value: %3d, bl_level: %3d\n", bl_value[i], bl_level[i]);

	make_bl_curve(bl, bl_value, bl_level);

exit:
	return ret;
}

static unsigned int count_char(const char *str, char c)
{
	unsigned int i, count = 0;

	for (i = 0; str[i]; i++) {
		if (str[i] == c)
			count++;
	}

	return count;
}

static int bl_tuning_show(struct seq_file *m, void *unused)
{
	struct bl_info *bl = m->private;
	int i, off = 0;

	for (i = 0; i <= bl->bd->props.max_brightness; i++)
		seq_printf(m, "[%3d] = %d,\n", i, *(bl->brightness_table + i));

	seq_puts(m, "+TABLE-------------------------------------------\n");
	seq_printf(m, "%d,\n", bl->brightness_table[0]);
	for (i = 1; i <= bl->bd->props.max_brightness; i++)
		seq_printf(m, "%d,%s", *(bl->brightness_table + i), !(i % 10) ? "\n" : " ");
	seq_puts(m, "\n");

	seq_puts(m, "+DEFAULT-----------------------------------------\n");
	for (i = BL_POINT_OFF; i >= BL_POINT_MAX; i--) {
		seq_printf(m, "%4s: %3d %3d\n", BL_POINT_NAME[i], bl->default_value[i], bl->default_level[i]);
	};
	if (bl->bd->props.max_brightness > 255)
		seq_printf(m, "%4s: %3d %3d\n", BL_POINT_NAME[BL_POINT_OUT], bl->default_value[BL_POINT_OUT], bl->default_level[BL_POINT_OUT]);

	for (off = 0; off < INPUT_LIMIT; off++) {
		if (!bl->tune_level[off]) {
			dev_info(&bl->bd->dev, "%s: off: %3d\n", __func__, off);
			break;
		}
	};
	off = (off == INPUT_LIMIT) ? 0 : off;
	if (off) {
		seq_puts(m, "+TUNING------------------------------------------\n");
		for (i = off; i >= 0; i--) {
			seq_printf(m, "      %3d %3d\n", bl->tune_value[i], bl->tune_level[i]);
		}
	}

	seq_puts(m, "-------------------------------------------------\n");

	return 0;
}

static int bl_tuning_open(struct inode *inode, struct file *file)
{
	return single_open(file, bl_tuning_show, inode->i_private);
}

static ssize_t bl_tuning_write(struct file *filp, const char __user *buf,
					size_t len, loff_t *ppos)
{
	struct bl_info *bl = ((struct seq_file *)(filp->private_data))->private;
	char wbuf[INPUT_LIMIT];
	int count;

	if (*ppos != 0)
		return 0;

	if (sysfs_streq(buf, "0")) {
		dev_info(&bl->bd->dev, "%s: input is 0(zero), so we try to make with default\n", __func__);
		make_bl_curve(bl, bl->default_value, bl->default_level);
		goto exit;
	}

	if (len >= sizeof(wbuf)) {
		dev_info(&bl->bd->dev, "%s: input size is too big, %zu\n", __func__, len);
		goto exit;
	}

	if (count_char(buf, ' ') == 0) {
		dev_info(&bl->bd->dev, "%s: input(blank) is invalid\n", __func__);
		goto exit;
	}

	if (count_char(buf, ',') >= 2) {
		dev_info(&bl->bd->dev, "%s: input(comma) is invalid, %d\n", __func__, count_char(buf, ','));
		goto exit;
	}

	count = simple_write_to_buffer(wbuf, sizeof(wbuf) - 1, ppos, buf, len);
	if (count < 0)
		return count;

	wbuf[count] = '\0';

	check_curve(bl, wbuf);

exit:
	return len;
}

static const struct file_operations bl_tuning_fops = {
	.open		= bl_tuning_open,
	.write		= bl_tuning_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int ic_tuning_show(struct seq_file *m, void *unused)
{
	struct ic_info *ic = m->private;
	int status;

	if (!ic->command) {
		dev_info(&ic->bd->dev, "%s: command is invalid\n", __func__);
		return 0;
	}

	if (ic->bd->props.fb_blank != FB_BLANK_UNBLANK) {
		dev_info(&ic->bd->dev, "%s: fb_blank is invalid, %d\n", __func__, ic->bd->props.fb_blank);
		return 0;
	}

	status = i2c_smbus_read_byte_data(ic->client, ic->command);
	if (status < 0)
		seq_printf(m, "%02x, status: %0d\n", ic->command, status);
	else
		seq_printf(m, "%02x, %02x\n", ic->command, status);

	return 0;
}

static int ic_tuning_open(struct inode *inode, struct file *file)
{
	return single_open(file, ic_tuning_show, inode->i_private);
}

static ssize_t ic_tuning_write(struct file *filp, const char __user *buf,
					size_t len, loff_t *ppos)
{
	struct ic_info *ic = ((struct seq_file *)(filp->private_data))->private;
	int ret = 0, command = 0, value = 0;

	if (*ppos != 0)
		return 0;

	ret = sscanf(buf, "%8i %8i", &command, &value);
	if (clamp(ret, 1, 2) != ret) {
		dev_info(&ic->bd->dev, "%s: input is invalid, %d\n", __func__, ret);
		goto exit;
	}

	if (clamp(command, 1, 255) != command) {
		dev_info(&ic->bd->dev, "%s: input is invalid, command: %02x\n", __func__, command);
		goto exit;
	}

	if (clamp(value, 0, 255) != value) {
		dev_info(&ic->bd->dev, "%s: input is invalid, value: %02x\n", __func__, value);
		goto exit;
	}

	dev_info(&ic->bd->dev, "%s: command: %02x, value: %02x%s\n", __func__, command, value, (ret == 2) ? ", write_mode" : "");

	if (ic->bd->props.fb_blank != FB_BLANK_UNBLANK) {
		dev_info(&ic->bd->dev, "%s: fb_blank is invalid, %d\n", __func__, ic->bd->props.fb_blank);
		goto exit;
	}

	if (ret == 2)
		i2c_smbus_write_byte_data(ic->client, command, value);

	ic->command = command;

exit:
	return len;
}

static const struct file_operations ic_tuning_fops = {
	.open		= ic_tuning_open,
	.write		= ic_tuning_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

int init_bl_curve_debugfs(struct backlight_device *bd, unsigned int *table, struct i2c_client **clients)
{
	struct bl_info *bl = NULL;
	struct ic_info *ic = NULL;
	int ret = 0, i2c_count;
	char name_string[] = "ic_tuning-";
	char full_string[I2C_NAME_SIZE + (u16)(ARRAY_SIZE(name_string))];
	struct dentry *debugfs_root;

	if (!bd) {
		pr_err("%s: failed to get backlight_device\n", __func__);
		ret = -ENODEV;
		goto exit;
	}

	debugfs_root = debugfs_create_dir("bl_curve", NULL);
	if (IS_ERR(debugfs_root) || !debugfs_root) {
		pr_err("%s: failed to debugfs_create_dir\n", __func__);
		ret = -ENOMEM;
		goto exit;
	}

	bl = kzalloc(sizeof(struct bl_info), GFP_KERNEL);
	bl->bd = bd;
	bl->brightness_table = table ? table : kcalloc(bd->props.max_brightness, sizeof(unsigned int), GFP_KERNEL);
	make_bl_default_point(bl);

	debugfs_create_file("bl_tuning", S_IRUSR | S_IWUSR, debugfs_root, bl, &bl_tuning_fops);

	for (i2c_count = 0; clients && clients[i2c_count]; i2c_count++) {
		ic = kzalloc(sizeof(struct ic_info), GFP_KERNEL);
		ic->bd = bd;
		ic->client = clients[i2c_count];

		memset(full_string, 0, sizeof(full_string));
		scnprintf(full_string, sizeof(full_string), "%s%s", name_string, clients[i2c_count]->name);
		debugfs_create_file(full_string, S_IRUSR | S_IWUSR, debugfs_root, ic, &ic_tuning_fops);

		dev_info(&bl->bd->dev, "%s: %s %s %s\n", __func__, full_string, dev_name(&clients[i2c_count]->adapter->dev), dev_name(&clients[i2c_count]->dev));
	}

	dev_info(&bl->bd->dev, "%s done\n", __func__);

exit:
	return ret;
}
#else
int init_bl_curve_debugfs(struct backlight_device *bd, unsigned int *table, struct i2c_client **clients)
{
	return 0;
}
#endif


