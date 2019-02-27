/*
 * Calibration support for Cirrus Logic CS35L40 codec
 *
 * Copyright 2017 Cirrus Logic
 *
 * Author:	David Rhodes	<david.rhodes@cirrus.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/file.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <asm/io.h>
#include <linux/firmware.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <linux/power_supply.h>
#include <linux/fs.h>

#include <linux/mfd/cs35l40/core.h>
#include <linux/mfd/cs35l40/registers.h>
#include <linux/mfd/cs35l40/calibration.h>
#include <linux/mfd/cs35l40/wmfw.h>

#define CS35L40_CAL_VERSION "5.00.6"

#define CS35L40_CAL_CLASS_NAME		"cs35l40"
#define CS35L40_CAL_DIR_NAME		"cirrus_cal"
#define CS35L40_CAL_CONFIG_FILENAME	"cs35l40-dsp1-spk-prot-calib.bin"
#define CS35L40_CAL_PLAYBACK_FILENAME	"cs35l40-dsp1-spk-prot.bin"
#define CS35L40_CAL_RDC_SAVE_LOCATION	"/efs/cirrus/rdc_cal"
#define CS35L40_CAL_TEMP_SAVE_LOCATION	"/efs/cirrus/temp_cal"

#define CS35L40_CAL_COMPLETE_DELAY_MS	1100
#define CS34L40_CAL_AMBIENT_DEFAULT	23
#define CS34L40_CAL_RDC_DEFAULT		8580

struct cs35l40_cal_t {
	struct class *cal_class;
	struct device *dev;
	struct regmap *regmap;
	bool cal_running;
	bool cal_second;
	struct mutex lock;
	struct delayed_work cal_complete_work;
	int efs_cache_read;
	unsigned int efs_cache_rdc;
	unsigned int efs_cache_temp;
};

static struct cs35l40_cal_t *cs35l40_cal;

struct cs35l40_dsp_buf {
	struct list_head list;
	void *buf;
};

static void cs35l40_cal_restart(void);

static struct cs35l40_dsp_buf *cs35l40_dsp_buf_alloc(const void *src, size_t len,
					     struct list_head *list)
{
	struct cs35l40_dsp_buf *buf = kzalloc(sizeof(*buf), GFP_KERNEL);

	if (buf == NULL)
		return NULL;

	buf->buf = vmalloc(len);
	if (!buf->buf) {
		kfree(buf);
		return NULL;
	}
	memcpy(buf->buf, src, len);

	if (list)
		list_add_tail(&buf->list, list);

	return buf;
}

static void cs35l40_dsp_buf_free(struct list_head *list)
{
	while (!list_empty(list)) {
		struct cs35l40_dsp_buf *buf = list_first_entry(list,
							   struct cs35l40_dsp_buf,
							   list);
		list_del(&buf->list);
		vfree(buf->buf);
		kfree(buf);
	}
}

static unsigned long long int cs35l40_rdc_to_ohms(unsigned long int rdc)
{
	return ((rdc * CS35L40_CAL_AMP_CONSTANT_NUM) /
		CS35L40_CAL_AMP_CONSTANT_DENOM);
}

static int cs35l40_load_config(const char *file)
{
	LIST_HEAD(buf_list);
	struct regmap *regmap = cs35l40_cal->regmap;
	struct wmfw_coeff_hdr *hdr;
	struct wmfw_coeff_item *blk;
	const struct firmware *firmware;
	const char *region_name;
	int ret, pos, blocks, type, offset, reg;
	struct cs35l40_dsp_buf *buf;

	ret = request_firmware(&firmware, file, cs35l40_cal->dev);

	if (ret != 0) {
		dev_err(cs35l40_cal->dev, "Failed to request '%s'\n", file);
		ret = 0;
		goto out;
	}
	ret = -EINVAL;

	if (sizeof(*hdr) >= firmware->size) {
		dev_err(cs35l40_cal->dev, "%s: file too short, %zu bytes\n",
			file, firmware->size);
		goto out_fw;
	}

	hdr = (void *)&firmware->data[0];
	if (memcmp(hdr->magic, "WMDR", 4) != 0) {
		dev_err(cs35l40_cal->dev, "%s: invalid magic\n", file);
		goto out_fw;
	}

	switch (be32_to_cpu(hdr->rev) & 0xff) {
	case 1:
		break;
	default:
		dev_err(cs35l40_cal->dev, "%s: Unsupported coefficient file format %d\n",
			 file, be32_to_cpu(hdr->rev) & 0xff);
		ret = -EINVAL;
		goto out_fw;
	}

	dev_dbg(cs35l40_cal->dev, "%s: v%d.%d.%d\n", file,
		(le32_to_cpu(hdr->ver) >> 16) & 0xff,
		(le32_to_cpu(hdr->ver) >>  8) & 0xff,
		le32_to_cpu(hdr->ver) & 0xff);

	pos = le32_to_cpu(hdr->len);

	blocks = 0;
	while (pos < firmware->size &&
	       pos - firmware->size > sizeof(*blk)) {
		blk = (void *)(&firmware->data[pos]);

		type = le16_to_cpu(blk->type);
		offset = le16_to_cpu(blk->offset);

		dev_dbg(cs35l40_cal->dev, "%s.%d: %x v%d.%d.%d\n",
			 file, blocks, le32_to_cpu(blk->id),
			 (le32_to_cpu(blk->ver) >> 16) & 0xff,
			 (le32_to_cpu(blk->ver) >>  8) & 0xff,
			 le32_to_cpu(blk->ver) & 0xff);
		dev_dbg(cs35l40_cal->dev, "%s.%d: %d bytes at 0x%x in %x\n",
			 file, blocks, le32_to_cpu(blk->len), offset, type);

		reg = 0;
		region_name = "Unknown";
		switch (type) {
		case WMFW_ADSP2_YM:
			dev_dbg(cs35l40_cal->dev, "%s.%d: %d bytes in %x for %x\n",
				 file, blocks, le32_to_cpu(blk->len),
				 type, le32_to_cpu(blk->id));

			if (le32_to_cpu(blk->id) == 0xcd) {
				reg = CS35L40_YM_CONFIG_ADDR;
				reg += offset - 0x8;
			}
			break;

		case WMFW_HALO_YM_PACKED:
			dev_dbg(cs35l40_cal->dev, "%s.%d: %d bytes in %x for %x\n",
				 file, blocks, le32_to_cpu(blk->len),
				 type, le32_to_cpu(blk->id));

			if (le32_to_cpu(blk->id) == 0xcd) {
				/*     config addr packed + 1        */
				/* config size (config[0]) is not at 24bit packed boundary */
				/* so that fist word gets written by itself to unpacked mem */
				/* then the rest of it starts here */
				/* offset = 3 (groups of 4 24bit words) * 3 (packed words) * 4 bytes */
				reg = CS35L40_DSP1_YMEM_PACK_0 + 3 * 4 * 3;
			}
			break;

		default:
			dev_dbg(cs35l40_cal->dev, "%s.%d: region type %x at %d\n",
				 file, blocks, type, pos);
			break;
		}

		if (reg) {
			if ((pos + le32_to_cpu(blk->len) + sizeof(*blk)) >
			    firmware->size) {
				dev_err(cs35l40_cal->dev,
					 "%s.%d: %s region len %d bytes exceeds file length %zu\n",
					 file, blocks, region_name,
					 le32_to_cpu(blk->len),
					 firmware->size);
				ret = -EINVAL;
				goto out_fw;
			}

			buf = cs35l40_dsp_buf_alloc(blk->data,
						le32_to_cpu(blk->len),
						&buf_list);
			if (!buf) {
				dev_err(cs35l40_cal->dev, "Out of memory\n");
				ret = -ENOMEM;
				goto out_fw;
			}

			dev_dbg(cs35l40_cal->dev, "%s.%d: Writing %d bytes at %x\n",
				 file, blocks, le32_to_cpu(blk->len),
				 reg);
			ret = regmap_raw_write_async(regmap, reg, buf->buf,
						     le32_to_cpu(blk->len));
			if (ret != 0) {
				dev_err(cs35l40_cal->dev,
					"%s.%d: Failed to write to %x in %s: %d\n",
					file, blocks, reg, region_name, ret);
			}
		}

		pos += (le32_to_cpu(blk->len) + sizeof(*blk) + 3) & ~0x03;
		blocks++;
	}

	ret = regmap_async_complete(regmap);
	if (ret != 0)
		dev_err(cs35l40_cal->dev, "Failed to complete async write: %d\n", ret);

	if (pos > firmware->size)
		dev_err(cs35l40_cal->dev, "%s.%d: %zu bytes at end of file\n",
			  file, blocks, pos - firmware->size);

	dev_info(cs35l40_cal->dev, "%s load complete\n", file);

out_fw:
	regmap_async_complete(regmap);
	release_firmware(firmware);
	cs35l40_dsp_buf_free(&buf_list);
out:
	return ret;
}

static void cs35l40_cal_complete_work(struct work_struct *work)
{
	int rdc, status, checksum, temp;
	unsigned long long int ohms;
	unsigned int cal_state;

	mutex_lock(&cs35l40_cal->lock);

	regmap_read(cs35l40_cal->regmap, CS35L40_CAL_STATUS, &status);
	regmap_read(cs35l40_cal->regmap, CS35L40_CAL_RDC, &rdc);
	regmap_read(cs35l40_cal->regmap, CS35L40_CAL_AMBIENT, &temp);
	regmap_read(cs35l40_cal->regmap, CS35L40_CAL_CHECKSUM, &checksum);

	regmap_read(cs35l40_cal->regmap,
			CS35L40_CSPL_STATE, &cal_state);
	if (cal_state == CS35L40_CSPL_STATE_ERROR) {
		dev_err(cs35l40_cal->dev,
			"Error during calibration, invalidating results\n");
		rdc = status = checksum = temp = 0;
	}

	ohms = cs35l40_rdc_to_ohms((unsigned long int)rdc);

	if (status == CS35L40_CSPL_STATUS_OUT_OF_RANGE) {
		dev_err(cs35l40_cal->dev, "Calibration out of range\n");
		if (cs35l40_cal->cal_second == false) {
			cs35l40_cal_restart();
			mutex_unlock(&cs35l40_cal->lock);
			return;
		}
	}

	dev_info(cs35l40_cal->dev, "Calibration complete\n");
	dev_info(cs35l40_cal->dev, "Duration:\t%d ms\n",
				    CS35L40_CAL_COMPLETE_DELAY_MS);
	dev_info(cs35l40_cal->dev, "Status:\t%d\n", status);
	if (status == CS35L40_CSPL_STATUS_INCOMPLETE)
		dev_err(cs35l40_cal->dev, "Calibration incomplete\n");
	dev_info(cs35l40_cal->dev, "R:\t\t%d (%llu.%llu Ohms)\n",
			rdc, ohms >> CS35L40_CAL_RDC_RADIX,
			    (ohms & (((1 << CS35L40_CAL_RDC_RADIX) - 1))) *
			    10000 / (1 << CS35L40_CAL_RDC_RADIX));
	dev_info(cs35l40_cal->dev, "Checksum:\t%d\n", checksum);
	dev_info(cs35l40_cal->dev, "Ambient:\t%d\n", temp);


	usleep_range(5000, 5500);

	regmap_write(cs35l40_cal->regmap, CS35L40_CSPL_COMMAND,
			CS35L40_CSPL_CMD_MUTE);

	usleep_range(5000, 5500);

	cs35l40_load_config(CS35L40_CAL_PLAYBACK_FILENAME);

	regmap_update_bits(cs35l40_cal->regmap,
		CS35L40_MIXER_NGATE_CH1_CFG,
		CS35L40_NG_ENABLE_MASK,
		CS35L40_NG_ENABLE_MASK);
	regmap_update_bits(cs35l40_cal->regmap,
		CS35L40_MIXER_NGATE_CH2_CFG,
		CS35L40_NG_ENABLE_MASK,
		CS35L40_NG_ENABLE_MASK);
	dev_dbg(cs35l40_cal->dev, "NOISE GATE ENABLE\n");

	regmap_write(cs35l40_cal->regmap, CS35L40_CAL_STATUS, status);
	regmap_write(cs35l40_cal->regmap, CS35L40_CAL_RDC, rdc);
	regmap_write(cs35l40_cal->regmap, CS35L40_CAL_AMBIENT, temp);
	regmap_write(cs35l40_cal->regmap, CS35L40_CAL_CHECKSUM, checksum);

	regmap_write(cs35l40_cal->regmap, CS35L40_CSPL_COMMAND,
			CS35L40_CSPL_CMD_REINIT);
	usleep_range(35000, 40000);

	regmap_read(cs35l40_cal->regmap,
			CS35L40_CSPL_STATE, &cal_state);
	if (cal_state == CS35L40_CSPL_STATE_ERROR)
		dev_err(cs35l40_cal->dev,
			"Playback config load error\n");

	usleep_range(1000, 1500);
	regmap_write(cs35l40_cal->regmap, CS35L40_CSPL_COMMAND,
			CS35L40_CSPL_CMD_UNMUTE);

	dev_dbg(cs35l40_cal->dev, "Calibration complete\n");
	cs35l40_cal->cal_running = 0;
	cs35l40_cal->efs_cache_read = 0;
	mutex_unlock(&cs35l40_cal->lock);
}

static int cs35l40_get_power_temp(void)
{
	union power_supply_propval value = {0};
	struct power_supply *psy;

	psy = power_supply_get_by_name("battery");
	if (!psy) {
		dev_warn(cs35l40_cal->dev, "failed to get battery, assuming %d\n",
				CS34L40_CAL_AMBIENT_DEFAULT);
		return CS34L40_CAL_AMBIENT_DEFAULT;
	}

	psy->get_property(psy, POWER_SUPPLY_PROP_TEMP, &value);

	return DIV_ROUND_CLOSEST(value.intval, 10);
}

static void cs35l40_cal_start(void)
{
	int ambient;
	unsigned int global_en;

	dev_dbg(cs35l40_cal->dev, "Calibration prepare start\n");

	regmap_read(cs35l40_cal->regmap,
			CS35L40_PWR_CTRL1, &global_en);
	while ((global_en & 1) == 0) {
		usleep_range(1000, 1500);
		regmap_read(cs35l40_cal->regmap,
			CS35L40_PWR_CTRL1, &global_en);
	}

	/* extra time for HALO startup */
	usleep_range(10000, 15500);

	regmap_write(cs35l40_cal->regmap, CS35L40_CSPL_COMMAND,
			CS35L40_CSPL_CMD_MUTE);

	usleep_range(1000, 5500);

	cs35l40_load_config(CS35L40_CAL_CONFIG_FILENAME);

	regmap_update_bits(cs35l40_cal->regmap,
		CS35L40_MIXER_NGATE_CH1_CFG,
		CS35L40_NG_ENABLE_MASK, 0);
	regmap_update_bits(cs35l40_cal->regmap,
		CS35L40_MIXER_NGATE_CH2_CFG,
		CS35L40_NG_ENABLE_MASK, 0);
	dev_dbg(cs35l40_cal->dev, "NOISE GATE DISABLE\n");

	ambient = cs35l40_get_power_temp();
	regmap_write(cs35l40_cal->regmap, CS35L40_CAL_AMBIENT, ambient);

	regmap_write(cs35l40_cal->regmap, CS35L40_CSPL_COMMAND,
			CS35L40_CSPL_CMD_REINIT);
	usleep_range(1000, 1500);
	regmap_write(cs35l40_cal->regmap, CS35L40_CSPL_COMMAND,
			CS35L40_CSPL_CMD_UNMUTE);
}

static void cs35l40_cal_restart(void)
{
	unsigned int cal_state;
	int delay = msecs_to_jiffies(CS35L40_CAL_COMPLETE_DELAY_MS);
	int retries = 10;

	cs35l40_cal->cal_second = true;
	dev_info(cs35l40_cal->dev, "Restarting calibration\n");

	regmap_write(cs35l40_cal->regmap, CS35L40_CAL_RDC, 0);
	regmap_write(cs35l40_cal->regmap, CS35L40_CAL_AMBIENT, 0);
	regmap_write(cs35l40_cal->regmap, CS35L40_CAL_STATUS, 0);
	regmap_write(cs35l40_cal->regmap, CS35L40_CAL_CHECKSUM, 0);

	cs35l40_cal_start();
	usleep_range(35000, 40000);

	regmap_read(cs35l40_cal->regmap,
			CS35L40_CSPL_STATE, &cal_state);
	while (cal_state == CS35L40_CSPL_STATE_ERROR && retries > 0) {
		dev_err(cs35l40_cal->dev,
			"Calibration load error, reloading\n");
		cs35l40_cal_start();
		usleep_range(35000, 40000);
		regmap_read(cs35l40_cal->regmap,
				CS35L40_CSPL_STATE, &cal_state);
		retries--;
	}

	if (retries == 0) {
		dev_err(cs35l40_cal->dev, "Calibration failed\n");
	}

	cs35l40_cal->cal_running = 1;

	dev_dbg(cs35l40_cal->dev, "Calibration prepare complete\n");

	queue_delayed_work(system_unbound_wq,
				&cs35l40_cal->cal_complete_work,
				delay);
}

static int cs35l40_cal_read_file(char *filename, int *value)
{
	struct file *cal_filp;
	mm_segment_t old_fs = get_fs();
	char str[12] = {0};
	int ret;

	set_fs(get_ds());

	cal_filp = filp_open(filename, O_RDONLY, 0660);
	if (IS_ERR(cal_filp)) {
		ret = PTR_ERR(cal_filp);
		dev_err(cs35l40_cal->dev, "Failed to open calibration file %s: %d\n",
			filename, ret);
		goto err_open;
	}

	ret = vfs_read(cal_filp, (char __user *)str, sizeof(str),
							&cal_filp->f_pos);
	if (ret != sizeof(str)) {
		dev_err(cs35l40_cal->dev, "Failed to read calibration file %s\n",
			filename);
		ret = -EIO;
		goto err_read;
	}

	ret = 0;

	if (kstrtoint(str, 0, value)) {
		dev_err(cs35l40_cal->dev, "Failed to parse calibration.\n");
		ret = -EINVAL;
	}

err_read:
	filp_close(cal_filp, current->files);
err_open:
	set_fs(old_fs);
	return ret;
}

int cs35l40_cal_apply(void)
{
	int ret1 = 0, ret2 = 0;
	unsigned int temp, rdc, status, checksum;

	if (cs35l40_cal->efs_cache_read == 1) {
		rdc = cs35l40_cal->efs_cache_rdc;
		temp = cs35l40_cal->efs_cache_temp;
	} else {
		ret1 = cs35l40_cal_read_file(CS35L40_CAL_RDC_SAVE_LOCATION,
						&rdc);

		ret2 = cs35l40_cal_read_file(CS35L40_CAL_TEMP_SAVE_LOCATION,
						&temp);

		if (ret1 < 0 || ret2 < 0) {
			dev_err(cs35l40_cal->dev,
				"No saved calibration, writing defaults\n");
			rdc = CS34L40_CAL_RDC_DEFAULT;
			temp = CS34L40_CAL_AMBIENT_DEFAULT;
		}

		cs35l40_cal->efs_cache_rdc = rdc;
		cs35l40_cal->efs_cache_temp = temp;
		cs35l40_cal->efs_cache_read = 1;
	}

	status = 1;
	checksum = status + rdc;

	dev_info(cs35l40_cal->dev,
		"Writing calibration: RDC = %d,\tTemp = %d\t \
		Status = %d\tChecksum = %d\n",
		rdc, temp, status, checksum);

	regmap_write(cs35l40_cal->regmap, CS35L40_CAL_RDC, rdc);
	regmap_write(cs35l40_cal->regmap, CS35L40_CAL_AMBIENT, temp);
	regmap_write(cs35l40_cal->regmap, CS35L40_CAL_STATUS, status);
	regmap_write(cs35l40_cal->regmap, CS35L40_CAL_CHECKSUM, checksum);

	return (ret1 < 0) ? ret1 : ret2;
}
EXPORT_SYMBOL_GPL(cs35l40_cal_apply);

/***** SYSFS Interfaces *****/

static ssize_t cs35l40_cal_version_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	return sprintf(buf, CS35L40_CAL_VERSION "\n");
}

static ssize_t cs35l40_cal_version_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	return 0;
}

static ssize_t cs35l40_cal_status_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	return sprintf(buf, "%s\n",
			cs35l40_cal->cal_running ? "Enabled" : "Disabled");
}

static ssize_t cs35l40_cal_status_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	int prepare;
	int ret = kstrtos32(buf, 10, &prepare);
	unsigned int cal_state;
	int delay = msecs_to_jiffies(CS35L40_CAL_COMPLETE_DELAY_MS);
	int retries = 10;

	mutex_lock(&cs35l40_cal->lock);

	if (ret == 0) {
		if (prepare == 1) {

			regmap_write(cs35l40_cal->regmap, CS35L40_CAL_RDC, 0);
			regmap_write(cs35l40_cal->regmap, CS35L40_CAL_AMBIENT, 0);
			regmap_write(cs35l40_cal->regmap, CS35L40_CAL_STATUS, 0);
			regmap_write(cs35l40_cal->regmap, CS35L40_CAL_CHECKSUM, 0);

			cs35l40_cal->cal_second = false;
			cs35l40_cal_start();
			usleep_range(35000, 40000);

			regmap_read(cs35l40_cal->regmap,
					CS35L40_CSPL_STATE, &cal_state);
			while (cal_state == CS35L40_CSPL_STATE_ERROR && retries > 0) {
				dev_err(cs35l40_cal->dev,
					"Calibration load error, reloading\n");
				cs35l40_cal_start();
				usleep_range(35000, 40000);
				regmap_read(cs35l40_cal->regmap,
						CS35L40_CSPL_STATE, &cal_state);
				retries--;
			}

			if (retries == 0) {
				dev_err(cs35l40_cal->dev, "Calibration failed\n");
				mutex_unlock(&cs35l40_cal->lock);
				return size;
			}

			cs35l40_cal->cal_running = 1;

			dev_dbg(cs35l40_cal->dev, "Calibration prepare complete\n");

			queue_delayed_work(system_unbound_wq,
						&cs35l40_cal->cal_complete_work,
						delay);
		}
	}

	mutex_unlock(&cs35l40_cal->lock);
	return size;
}

static ssize_t cs35l40_cal_rdc_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	unsigned int rdc;

	regmap_read(cs35l40_cal->regmap, CS35L40_CAL_RDC, &rdc);

	return sprintf(buf, "%d", rdc);
}

static ssize_t cs35l40_cal_rdc_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	int rdc, ret;

	ret = kstrtos32(buf, 10, &rdc);
	if (ret == 0)
		regmap_write(cs35l40_cal->regmap, CS35L40_CAL_RDC, rdc);
	return size;
}

static ssize_t cs35l40_cal_temp_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	unsigned int temp;

	regmap_read(cs35l40_cal->regmap, CS35L40_CAL_AMBIENT, &temp);
	return sprintf(buf, "%d", temp);
}

static ssize_t cs35l40_cal_temp_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	int temp, ret;

	ret = kstrtos32(buf, 10, &temp);
	if (ret == 0)
		regmap_write(cs35l40_cal->regmap, CS35L40_CAL_AMBIENT, temp);
	return size;
}

static ssize_t cs35l40_cal_checksum_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	unsigned int checksum;

	regmap_read(cs35l40_cal->regmap, CS35L40_CAL_CHECKSUM, &checksum);
	return sprintf(buf, "%d", checksum);
}

static ssize_t cs35l40_cal_checksum_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	int checksum, ret;

	ret = kstrtos32(buf, 10, &checksum);
	if (ret == 0)
		regmap_write(cs35l40_cal->regmap, CS35L40_CAL_CHECKSUM, checksum);
	return size;
}

static ssize_t cs35l40_cal_set_status_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	unsigned int set_status;

	regmap_read(cs35l40_cal->regmap, CS35L40_CAL_SET_STATUS, &set_status);
	return sprintf(buf, "%d", set_status);
}

static ssize_t cs35l40_cal_set_status_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	return 0;
}

static ssize_t cs35l40_cal_rdc_stored_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	unsigned int rdc;

	cs35l40_cal_read_file(CS35L40_CAL_RDC_SAVE_LOCATION, &rdc);
	return sprintf(buf, "%d", rdc);
}

static ssize_t cs35l40_cal_rdc_stored_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	return 0;
}

static ssize_t cs35l40_cal_temp_stored_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	unsigned int temp_stored;

	cs35l40_cal_read_file(CS35L40_CAL_TEMP_SAVE_LOCATION, &temp_stored);
	return sprintf(buf, "%d", temp_stored);
}

static ssize_t cs35l40_cal_temp_stored_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	return 0;
}

static DEVICE_ATTR(version, 0444, cs35l40_cal_version_show,
				cs35l40_cal_version_store);
static DEVICE_ATTR(status, 0664, cs35l40_cal_status_show,
				cs35l40_cal_status_store);
static DEVICE_ATTR(rdc, 0664, cs35l40_cal_rdc_show,
				cs35l40_cal_rdc_store);
static DEVICE_ATTR(temp, 0664, cs35l40_cal_temp_show,
				cs35l40_cal_temp_store);
static DEVICE_ATTR(checksum, 0664, cs35l40_cal_checksum_show,
				cs35l40_cal_checksum_store);
static DEVICE_ATTR(set_status, 0444, cs35l40_cal_set_status_show,
				cs35l40_cal_set_status_store);
static DEVICE_ATTR(rdc_stored, 0444, cs35l40_cal_rdc_stored_show,
				cs35l40_cal_rdc_stored_store);
static DEVICE_ATTR(temp_stored, 0444, cs35l40_cal_temp_stored_show,
				cs35l40_cal_temp_stored_store);

static struct attribute *cs35l40_cal_attr[] = {
	&dev_attr_version.attr,
	&dev_attr_status.attr,
	&dev_attr_rdc.attr,
	&dev_attr_temp.attr,
	&dev_attr_checksum.attr,
	&dev_attr_set_status.attr,
	&dev_attr_rdc_stored.attr,
	&dev_attr_temp_stored.attr,
	NULL,
};

static struct attribute_group cs35l40_cal_attr_grp = {
	.attrs = cs35l40_cal_attr,
};

static int cs35l40_cal_probe(struct platform_device *pdev)
{
	struct cs35l40_data *cs35l40 = dev_get_drvdata(pdev->dev.parent);
	int ret;
	unsigned int temp;

	cs35l40_cal = kzalloc(sizeof(struct cs35l40_cal_t), GFP_KERNEL);
	if (cs35l40_cal == NULL)
		return -ENOMEM;

	cs35l40_cal->dev = device_create(cs35l40->mfd_class, NULL, 1, NULL,
						CS35L40_CAL_DIR_NAME);
	if (IS_ERR(cs35l40_cal->dev)) {
		ret = PTR_ERR(cs35l40_cal->dev);
		goto err_dev;
	}

	cs35l40_cal->regmap = cs35l40->regmap;
	regmap_read(cs35l40_cal->regmap, 0x00000000, &temp);
	dev_info(&pdev->dev, "Prince Calibration Driver probe, Dev ID = %x\n", temp);

	ret = sysfs_create_group(&cs35l40_cal->dev->kobj, &cs35l40_cal_attr_grp);
	if (ret) {
		dev_err(cs35l40_cal->dev, "Failed to create sysfs group\n");
		goto err_dev;
	}

	cs35l40_cal->efs_cache_read = 0;
	cs35l40_cal->cal_second = false;

	mutex_init(&cs35l40_cal->lock);
	INIT_DELAYED_WORK(&cs35l40_cal->cal_complete_work, cs35l40_cal_complete_work);

	return 0;

err_dev:
	kfree(cs35l40_cal);
	return ret;
}

static int cs35l40_cal_remove(struct platform_device *pdev)
{
	kfree(cs35l40_cal);
	return 0;
}

static struct platform_driver cs35l40_cal_driver = {
	.driver = {
		.name = "cs35l40-cal",
		.owner = THIS_MODULE,
	},
	.probe = cs35l40_cal_probe,
	.remove = cs35l40_cal_remove,
};
module_platform_driver(cs35l40_cal_driver);
