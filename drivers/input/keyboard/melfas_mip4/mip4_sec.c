/*
 * MELFAS MIP4 Touchkey
 *
 * Copyright (C) 2016 MELFAS Inc.
 *
 * mip4_sec.c : SEC command functions
 */

#include "mip4.h"

#if MIP_USE_CMD

/**
* Print chip firmware version
*/
static ssize_t mip4_tk_fw_version_panel(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mip4_tk_info *info = dev_get_drvdata(dev);
	u8 data[255];
	int ret;
	u8 rbuf[16];

	memset(info->print_buf, 0, PAGE_SIZE);

	if (mip4_tk_get_fw_version(info, rbuf)) {
		input_err(true, &info->client->dev, "%s [ERROR] mip4_tk_get_fw_version\n", __func__);
		sprintf(data, "NG\n");
		goto error;
	}

	input_info(true, &info->client->dev, "%s - F/W Version : %02X.%02X/%02X.%02X/%02X.%02X/%02X.%02X\n", __func__, rbuf[0], rbuf[1], rbuf[2], rbuf[3], rbuf[4], rbuf[5], rbuf[6], rbuf[7]);
	sprintf(data, "0x%02X", rbuf[7]);

error:
	strcat(info->print_buf, data);

	ret = snprintf(buf, PAGE_SIZE, "%s", info->print_buf);
	return ret;
}

/**
* Print chip firmware version
*/
static ssize_t mip4_tk_cmd_fw_version_ic(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mip4_tk_info *info = dev_get_drvdata(dev);
	u8 data[255];
	int ret;
	u8 rbuf[16];

	memset(info->print_buf, 0, PAGE_SIZE);

	if (mip4_tk_get_fw_version(info, rbuf)) {
		input_err(true, &info->client->dev, "%s [ERROR] mip4_tk_get_fw_version\n", __func__);
		sprintf(data, "NG\n");
		goto error;
	}

	input_info(true, &info->client->dev, "%s - F/W Version : %02X.%02X/%02X.%02X/%02X.%02X/%02X.%02X\n", __func__, rbuf[0], rbuf[1], rbuf[2], rbuf[3], rbuf[4], rbuf[5], rbuf[6], rbuf[7]);
	sprintf(data, "%02X.%02X/%02X.%02X/%02X.%02X/%02X.%02X\n", rbuf[0], rbuf[1], rbuf[2], rbuf[3], rbuf[4], rbuf[5], rbuf[6], rbuf[7]);

error:
	strcat(info->print_buf, data);

	ret = snprintf(buf, PAGE_SIZE, "%s", info->print_buf);
	return ret;
}

/**
* Print bin(file) firmware version
*/
static ssize_t mip4_tk_fw_version_phone(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mip4_tk_info *info = dev_get_drvdata(dev);
	u8 data[255];
	int ret;
	u8 rbuf[16];

	if (mip4_tk_get_fw_version_from_bin(info, rbuf)) {
		input_err(true, &info->client->dev, "%s [ERROR] mip_get_fw_version_from_bin\n", __func__);
		sprintf(data, "NG\n");
		goto error;
	}

	input_info(true, &info->client->dev, "%s - BIN Version : %02X.%02X/%02X.%02X/%02X.%02X/%02X.%02X\n", __func__, rbuf[0], rbuf[1], rbuf[2], rbuf[3], rbuf[4], rbuf[5], rbuf[6], rbuf[7]);
	sprintf(data, "0x%02X", rbuf[7]);

error:
	ret = snprintf(buf, 255, "%s", data);
	return ret;
}

/**
* Print bin(file) firmware version
*/
static ssize_t mip4_tk_cmd_fw_version_bin(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mip4_tk_info *info = dev_get_drvdata(dev);
	u8 data[255];
	int ret;
	u8 rbuf[16];

	if (mip4_tk_get_fw_version_from_bin(info, rbuf)) {
		input_err(true, &info->client->dev, "%s [ERROR] mip_get_fw_version_from_bin\n", __func__);
		sprintf(data, "NG\n");
		goto error;
	}

	input_info(true, &info->client->dev, "%s - BIN Version : %02X.%02X/%02X.%02X/%02X.%02X/%02X.%02X\n", __func__, rbuf[0], rbuf[1], rbuf[2], rbuf[3], rbuf[4], rbuf[5], rbuf[6], rbuf[7]);
	sprintf(data, "%02X.%02X/%02X.%02X/%02X.%02X/%02X.%02X\n", rbuf[0], rbuf[1], rbuf[2], rbuf[3], rbuf[4], rbuf[5], rbuf[6], rbuf[7]);

error:
	ret = snprintf(buf, 255, "%s", data);
	return ret;
}

/**
* Update firmware
*/
static ssize_t mip4_tk_cmd_fw_update(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mip4_tk_info *info = i2c_get_clientdata(client);
	int result = 0;
	u8 data[255];
	int ret = 0;

	memset(info->print_buf, 0, PAGE_SIZE);

	input_dbg(true, &info->client->dev, "%s [START]\n", __func__);

	ret = mip4_tk_fw_update_from_storage(info, info->fw_path_ext, true);

	switch (ret) {
	case fw_err_none:
		sprintf(data, "F/W update success.\n");
		break;
	case fw_err_uptodate:
		sprintf(data, "F/W is already up-to-date.\n");
		break;
	case fw_err_download:
		sprintf(data, "F/W update failed : Download error\n");
		break;
	case fw_err_file_type:
		sprintf(data, "F/W update failed : File type error\n");
		break;
	case fw_err_file_open:
		sprintf(data, "F/W update failed : File open error [%s]\n", info->fw_path_ext);
		break;
	case fw_err_file_read:
		sprintf(data, "F/W update failed : File read error\n");
		break;
	default:
		sprintf(data, "F/W update failed.\n");
		break;
	}

	input_dbg(true, &info->client->dev, "%s [DONE]\n", __func__);

	strcat(info->print_buf, data);
	result = snprintf(buf, PAGE_SIZE, "%s", info->print_buf);
	return result;
}

/**
* Update firmware
*/
static ssize_t mip4_tk_fw_update(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mip4_tk_info *info = i2c_get_clientdata(client);
	int result = 0;

	memset(info->print_buf, 0, PAGE_SIZE);

	switch(*buf) {
		info->firmware_state = 1;
	case 's':
	case 'S':
		result = mip4_tk_fw_update_from_kernel(info, true);
		if (result)
			info->firmware_state = 2;
		break;
	case 'i':
	case 'I':
		result = mip4_tk_fw_update_from_storage(info, info->fw_path_ext, true);
		if (result)
			info->firmware_state = 2;
		break;
	default:
		info->firmware_state = 2;
		goto exit;
	}

	info->firmware_state = 0;

exit:
	input_info(true, &info->client->dev, "%s [DONE]\n", __func__);

	return count;
}

static ssize_t mip4_tk_cmd_fw_update_status(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mip4_tk_info *info = i2c_get_clientdata(client);
	size_t count;

	input_info(true, &info->client->dev, "%s : %d\n", __func__, info->firmware_state);

	if (info->firmware_state == 0)
		count = snprintf(buf, PAGE_SIZE, "PASS\n");
	else if (info->firmware_state == 1)
		count = snprintf(buf, PAGE_SIZE, "Downloading\n");
	else if (info->firmware_state == 2)
		count = snprintf(buf, PAGE_SIZE, "Fail\n");
	else
		count = snprintf(buf, PAGE_SIZE, "Fail\n");

	return count;
}

/**
* Sysfs print intensity
*/
static ssize_t mip4_tk_cmd_image(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mip4_tk_info *info = dev_get_drvdata(dev);
	int key_idx = -1;
	int type = 0;
	int retry = 10;

	if (!strcmp(attr->attr.name, "touchkey_recent")) {
		key_idx = 0;
		type = MIP_IMG_TYPE_INTENSITY;
	} else if (!strcmp(attr->attr.name, "touchkey_recent_raw")) {
		key_idx = 0;
		type = MIP_IMG_TYPE_RAWDATA;
	} else if (!strcmp(attr->attr.name, "touchkey_back")) {
		key_idx = 1;
		type = MIP_IMG_TYPE_INTENSITY;
	} else if (!strcmp(attr->attr.name, "touchkey_back_raw")) {
		key_idx = 1;
		type = MIP_IMG_TYPE_RAWDATA;
#ifdef CONFIG_TOUCHKEY_GRIP
	} else if (!strcmp(attr->attr.name, "grip")) {
		key_idx = 2;
		type = MIP_IMG_TYPE_INTENSITY;	
	} else if (!strcmp(attr->attr.name, "grip_raw")) {
		key_idx = 2;
		type = MIP_IMG_TYPE_RAWDATA;		
#endif		
	} else {
		input_err(true, &info->client->dev, "%s [ERROR] Invalid attribute\n", __func__);
		goto error;
	}

	while (retry--) {
		if (info->test_busy == false) {
			break;
		}
		msleep(10);
	}
	if (retry <= 0) {
		input_err(true, &info->client->dev, "%s [ERROR] skip\n", __func__);
		goto exit;
	}

	if (mip4_tk_get_image(info, type)) {
		input_err(true, &info->client->dev, "%s [ERROR] mip_get_image\n", __func__);
		goto error;
	}

exit:
	input_dbg(true, &info->client->dev, "%s - %s [%d]\n", __func__, attr->attr.name, info->image_buf[key_idx]);
	return snprintf(buf, PAGE_SIZE, "%d\n", info->image_buf[key_idx]);

error:
	input_err(true, &info->client->dev, "%s [ERROR]\n", __func__);
	return snprintf(buf, PAGE_SIZE, "NG\n");
}

/*
* Sysfs print threshold
*/
static ssize_t mip4_tk_cmd_threshold(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mip4_tk_info *info = dev_get_drvdata(dev);
	u8 wbuf[2];
	u8 rbuf[2];
	int threshold;

	wbuf[0] = MIP_R0_INFO;
	wbuf[1] = MIP_R1_INFO_THD_CONTACT_KEY;

	if (mip4_tk_i2c_read(info, wbuf, 2, rbuf, 1)) {
		goto error;
	}

	threshold = rbuf[0];

	input_dbg(true, &info->client->dev, "%s - threshold [%d]\n", __func__, threshold);
	return snprintf(buf, PAGE_SIZE, "%d\n", threshold);

error:
	input_err(true, &info->client->dev, "%s [ERROR]\n", __func__);
	return snprintf(buf, PAGE_SIZE, "NG\n");
}

static ssize_t mip4_tk_cmd_key_recal(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct mip4_tk_info *info = dev_get_drvdata(dev);
	u8 wbuf[3];
	int buff;
	int ret;

#ifdef CONFIG_TOUCHKEY_GRIP	
	if(info->sar_mode == 1)
	{
		input_err(true,&info->client->dev, "%s [ERROR] sar only mode \n", __func__);
		goto error;
	}
#endif

	ret = sscanf(buf, "%d", &buff);
	if (ret != 1) {
		input_err(true,&info->client->dev,  "%s: cmd read err\n", __func__);
		return count;
	}	

	input_info(true,&info->client->dev,"%s (%d)\n", __func__, buff);	
	
	if (!(buff >= 0 && buff <= 1)) {
		input_err(true,&info->client->dev,  "%s: wrong command(%d)\n",
			__func__, buff);
		return count;
	}	
	
	wbuf[0] = MIP_R0_CTRL;
	wbuf[1] = MIP_R1_CTRL_REBASELINE_KEY;
	wbuf[2] = 1;

	if (mip4_tk_i2c_write(info, wbuf, 3)) {
		input_err(true,&info->client->dev, "%s [ERROR] mip4_tk_i2c_write\n", __func__);
		goto error;
	}

	return count;

error:
	input_err(true,&info->client->dev, "%s [ERROR]\n", __func__);
	return count;
}


#ifdef MIP_USE_LED
/**
* Store LED on/off
*/
static ssize_t mip4_tk_cmd_led_onoff_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct mip4_tk_info *info = dev_get_drvdata(dev);
	u8 wbuf[6];
	const char delimiters[] = ",\n";
	char string[count];
	char *stringp = string;
	char *token;
	int i, value, idx, bit;
	u8 values[4] = {0, };

	input_dbg(true, &info->client->dev, "%s [START]\n", __func__);

	if (info->led_num <= 0) {
		input_dbg(true, &info->client->dev, "%s - N/A\n", __func__);
		goto exit;
	}

	memset(string, 0x00, count);
	memcpy(string, buf, count);

	//Input format: "LED #0,LED #1,LED #2,..."
	//	Number of LEDs should be matched.

	for (i = 0; i < info->led_num; i++) {
		token = strsep(&stringp, delimiters);
		if (token == NULL) {
			input_err(true, &info->client->dev, "%s [ERROR] LED number mismatch\n", __func__);
			goto error;
		} else {
			if (kstrtoint(token, 10, &value)) {
				input_err(true, &info->client->dev, "%s [ERROR] wrong input value [%s]\n", __func__, token);
				goto error;
			}

			idx = i / 8;
			bit = i % 8;
			if (value == 1) {
				values[idx] |= (1 << bit);
			} else {
				values[idx] &= ~(1 << bit);
			}
		}
	}

	wbuf[0] = MIP_R0_LED;
	wbuf[1] = MIP_R1_LED_ON;
	wbuf[2] = values[0];
	wbuf[3] = values[1];
	wbuf[4] = values[2];
	wbuf[5] = values[3];
	if (mip4_tk_i2c_write(info, wbuf, 6)) {
		input_err(true, &info->client->dev, "%s [ERROR] mip4_tk_i2c_write\n", __func__);
		goto error;
	}
	input_dbg(true, &info->client->dev, "%s - wbuf 0x%02X 0x%02X 0x%02X 0x%02X\n", __func__, wbuf[2], wbuf[3], wbuf[4], wbuf[5]);

exit:
	input_dbg(true, &info->client->dev, "%s [DONE]\n", __func__);
	return count;

error:
	input_err(true, &info->client->dev, "%s [ERROR]\n", __func__);
	return count;
}

/**
* Show LED on/off
*/
static ssize_t mip4_tk_cmd_led_onoff_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mip4_tk_info *info = dev_get_drvdata(dev);
	u8 data[255];
	int ret;
	u8 wbuf[2];
	u8 rbuf[4];
	int i, idx, bit;

	memset(info->print_buf, 0, PAGE_SIZE);

	input_dbg(true, &info->client->dev, "%s [START]\n", __func__);

	if (info->led_num <= 0) {
		sprintf(data, "NA\n");
		goto exit;
	}

	wbuf[0] = MIP_R0_LED;
	wbuf[1] = MIP_R1_LED_ON;

	if (mip4_tk_i2c_read(info, wbuf, 2, rbuf, 4)) {
		input_err(true, &info->client->dev, "%s [ERROR] mip4_tk_i2c_read\n", __func__);
		sprintf(data, "NG\n");
	} else {
		for (i = 0; i < info->led_num; i++) {
			idx = i / 8;
			bit = i % 8;
			if (i == 0) {
				sprintf(data, "%d", ((rbuf[idx] >> bit) & 0x01));
			} else {
				sprintf(data, ",%d", ((rbuf[idx] >> bit) & 0x01));
			}
			strcat(info->print_buf, data);
		}
	}
	sprintf(data, "\n");

exit:
	input_dbg(true, &info->client->dev, "%s [DONE]\n", __func__);

	strcat(info->print_buf, data);
	ret = snprintf(buf, PAGE_SIZE, "%s", info->print_buf);
	return ret;
}

/**
* Store LED brightness
*/
static ssize_t mip4_tk_cmd_led_brightness_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct mip4_tk_info *info = dev_get_drvdata(dev);
	u8 wbuf[2 + MAX_LED_NUM];
	const char delimiters[] = ",\n";
	char string[count];
	char *stringp = string;
	char *token;
	int value, i;
	u8 values[MAX_LED_NUM];

	input_dbg(true, &info->client->dev, "%s [START]\n", __func__);

	if (info->led_num <= 0) {
		input_dbg(true, &info->client->dev, "%s - N/A\n", __func__);
		goto exit;
	}

	memset(string, 0x00, count);
	memcpy(string, buf, count);

	//Input format: "LED #0,LED #1,LED #2,..."
	//	Number of LEDs should be matched.

	for (i = 0; i < info->led_num; i++) {
		token = strsep(&stringp, delimiters);
		if (token == NULL) {
			input_err(true, &info->client->dev, "%s [ERROR] LED number mismatch\n", __func__);
			goto error;
		} else {
			if (kstrtoint(token, 10, &value)) {
				input_err(true, &info->client->dev, "%s [ERROR] wrong input value\n", __func__);
				goto error;
			}
			values[i] = (u8)value;
		}
	}

	wbuf[0] = MIP_R0_LED;
	wbuf[1] = MIP_R1_LED_BRIGHTNESS;
	memcpy(&wbuf[2], values, info->led_num);
	if (mip4_tk_i2c_write(info, wbuf, (2 + info->led_num))) {
		input_err(true, &info->client->dev, "%s [ERROR] mip4_tk_i2c_write\n", __func__);
		goto error;
	}

exit:
	input_dbg(true, &info->client->dev, "%s [DONE]\n", __func__);
	return count;

error:
	input_err(true, &info->client->dev, "%s [ERROR]\n", __func__);
	return count;
}

/**
* Show LED brightness
*/
static ssize_t mip4_tk_cmd_led_brightness_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mip4_tk_info *info = dev_get_drvdata(dev);
	u8 data[255];
	int ret, i;
	u8 wbuf[2];
	u8 rbuf[MAX_LED_NUM];

	memset(info->print_buf, 0, PAGE_SIZE);

	input_dbg(true, &info->client->dev, "%s [START]\n", __func__);

	if (info->led_num <= 0) {
		sprintf(data, "NA\n");
		goto exit;
	}

	wbuf[0] = MIP_R0_LED;
	wbuf[1] = MIP_R1_LED_BRIGHTNESS;
	if (mip4_tk_i2c_read(info, wbuf, 2, rbuf, info->led_num)) {
		input_err(true, &info->client->dev, "%s [ERROR] mip4_tk_i2c_read\n", __func__);
		sprintf(data, "NG\n");
	} else {
		for (i = 0; i < info->led_num; i++) {
			if (i == 0) {
				sprintf(data, "%d", rbuf[i]);
			} else {
				sprintf(data, ",%d", rbuf[i]);
			}
			strcat(info->print_buf, data);
		}
		sprintf(data, "\n");
	}

exit:
	input_dbg(true, &info->client->dev, "%s [DONE]\n", __func__);

	strcat(info->print_buf, data);
	ret = snprintf(buf, PAGE_SIZE, "%s", info->print_buf);
	return ret;
}
#endif // MIP_USE_LED


/*
* Sysfs send key/grip enable
*/
static ssize_t mip4_tk_cmd_enable_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct mip4_tk_info *info = dev_get_drvdata(dev);
	u8 wbuf[3];
	int value;

	input_dbg(true,&info->client->dev, "%s [START]\n", __func__);

	wbuf[0] = MIP_R0_CTRL;

	if (!strcmp(attr->attr.name, "touchkey_enable")) {
		wbuf[1] = MIP_R1_CTRL_ENABLE_KEY;
	} 
#ifdef CONFIG_TOUCHKEY_GRIP
	else if (!strcmp(attr->attr.name, "grip_enable")) {
		wbuf[1] = MIP_R1_CTRL_ENABLE_GRIP;
	} 
#endif	
	else {
		input_err(true,&info->client->dev, "%s [ERROR] Unknown mode [%s]\n", __func__, attr->attr.name);
		goto error;
	}

	if (buf[0] == 48) {
		value = 0;
	} else if (buf[0] == 49) {
		value = 1;
	} else {
		input_err(true,&info->client->dev, "%s [ERROR] Unknown value [%c]\n", __func__, buf[0]);
		goto exit;
	}
	wbuf[2] = value;

	if (mip4_tk_i2c_write(info, wbuf, 3)) {
		input_err(true,&info->client->dev, "%s [ERROR] mip4_tk_i2c_write\n", __func__);
		goto error;
	}
	input_dbg(true,&info->client->dev, "%s - addr[0x%02X%02X] value[%d]\n", __func__, wbuf[0], wbuf[1], value);

exit:
	input_dbg(true,&info->client->dev, "%s [DONE]\n", __func__);
	return count;

error:
	input_err(true,&info->client->dev, "%s [ERROR]\n", __func__);
	return count;
}

/*
* Sysfs read key/grip enable
*/
static ssize_t mip4_tk_cmd_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mip4_tk_info *info = dev_get_drvdata(dev);
	u8 data[255];
	int ret;
	u8 wbuf[8];
	u8 rbuf[4];

	memset(info->print_buf, 0, PAGE_SIZE);

	input_dbg(true,&info->client->dev, "%s [START]\n", __func__);

	wbuf[0] = MIP_R0_CTRL;
	
	if (!strcmp(attr->attr.name, "touchkey_enable")) {
		wbuf[1] = MIP_R1_CTRL_ENABLE_KEY;
	}
#ifdef CONFIG_TOUCHKEY_GRIP	
	else if (!strcmp(attr->attr.name, "grip_enable")) {
		wbuf[1] = MIP_R1_CTRL_ENABLE_GRIP;
	} 
#endif	
	else {
		input_err(true,&info->client->dev, "%s [ERROR] Unknown mode [%s]\n", __func__, attr->attr.name);
		sprintf(data, "%s : Unknown Mode\n", attr->attr.name);
		goto exit;
	}

	if (mip4_tk_i2c_read(info, wbuf, 2, rbuf, 1)) {
		input_err(true,&info->client->dev, "%s [ERROR] mip4_tk_i2c_read\n", __func__);
		sprintf(data, "%s : ERROR\n", attr->attr.name);
	} else {
		input_info(true,&info->client->dev, "%s - addr[0x%02X%02X] value[%d]\n", __func__, wbuf[0], wbuf[1], rbuf[0]);
		sprintf(data, "%s : %d\n", attr->attr.name, rbuf[0]);
	}

	input_dbg(true,&info->client->dev, "%s [DONE]\n", __func__);

exit:
	strcat(info->print_buf, data);
	ret = snprintf(buf, PAGE_SIZE, "%s\n", info->print_buf);
	return ret;
}

/*
* Sysfs key/grip recalibration
*/
static ssize_t mip4_tk_cmd_recal_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct mip4_tk_info *info = dev_get_drvdata(dev);
	u8 wbuf[3];
	int value;

	input_dbg(true,&info->client->dev, "%s [START]\n", __func__);

	wbuf[0] = MIP_R0_CTRL;

	if (!strcmp(attr->attr.name, "touchkey_recal")) {
		wbuf[1] = MIP_R1_CTRL_REBASELINE_KEY;
	}
#ifdef CONFIG_TOUCHKEY_GRIP
	else if (!strcmp(attr->attr.name, "grip_recal")) {
		wbuf[1] = MIP_R1_CTRL_REBASELINE_GRIP;
	}
#endif	
	else {
		input_err(true,&info->client->dev, "%s [ERROR] Unknown mode [%s]\n", __func__, attr->attr.name);
		goto error;
	}

	if (buf[0] == 48) {
		value = 0;
	} else if (buf[0] == 49) {
		value = 1;
	} else {
		input_err(true,&info->client->dev, "%s [ERROR] Unknown value [%c]\n", __func__, buf[0]);
		goto exit;
	}
	wbuf[2] = value;

	if (mip4_tk_i2c_write(info, wbuf, 3)) {
		input_err(true,&info->client->dev, "%s [ERROR] mip4_tk_i2c_write\n", __func__);
		goto error;
	}
	input_dbg(true,&info->client->dev, "%s - addr[0x%02X%02X] value[%d]\n", __func__, wbuf[0], wbuf[1], value);

exit:
	input_dbg(true,&info->client->dev, "%s [DONE]\n", __func__);
	return count;

error:
	input_err(true,&info->client->dev, "%s [ERROR]\n", __func__);
	return count;
}


/*
* Show Key recalibration
*/
static ssize_t mip4_tk_cmd_recal_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mip4_tk_info *info = dev_get_drvdata(dev);
	u8 data[255];
	int ret;
	u8 wbuf[8];
	u8 rbuf[4];

	memset(info->print_buf, 0, PAGE_SIZE);

	input_dbg(true,&info->client->dev, "%s [START]\n", __func__);

	wbuf[0] = MIP_R0_CTRL;
	
	if (!strcmp(attr->attr.name, "touchkey_recal")) {
		wbuf[1] = MIP_R1_CTRL_REBASELINE_KEY;
	} 
#ifdef CONFIG_TOUCHKEY_GRIP	
	else if (!strcmp(attr->attr.name, "grip_recal")) {
		wbuf[1] = MIP_R1_CTRL_REBASELINE_GRIP;
	} 
#endif	
	else {
		input_err(true,&info->client->dev, "%s [ERROR] Unknown mode [%s]\n", __func__, attr->attr.name);
		sprintf(data, "%s : Unknown Mode\n", attr->attr.name);
		goto exit;
	}

	if (mip4_tk_i2c_read(info, wbuf, 2, rbuf, 1)) {
		input_err(true,&info->client->dev, "%s [ERROR] mip4_tk_i2c_read\n", __func__);
		sprintf(data, "%s : ERROR\n", attr->attr.name);
	} else {
		input_info(true,&info->client->dev, "%s - addr[0x%02X%02X] value[%d]\n", __func__, wbuf[0], wbuf[1], rbuf[0]);
		sprintf(data, "%s : %d\n", attr->attr.name, rbuf[0]);
	}

	input_dbg(true,&info->client->dev, "%s [DONE]\n", __func__);

exit:
	strcat(info->print_buf, data);
	ret = snprintf(buf, PAGE_SIZE, "%s\n", info->print_buf);
	return ret;
}

static ssize_t mip4_tk_cmd_key_irq_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mip4_tk_info *info = dev_get_drvdata(dev);
	int home_count = 0;

	input_info(true, &info->client->dev, "%s - Recent : %d, Home : %d, Back : %d \n", __func__,info->irq_key_count[0], home_count, info->irq_key_count[1]);

	return snprintf(buf, PAGE_SIZE, "%d,%d,%d\n",info->irq_key_count[0], home_count, info->irq_key_count[1]);
}

static ssize_t mip4_tk_cmd_key_irq_count_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct mip4_tk_info *info = dev_get_drvdata(dev);

	u8 onoff = 0;
	int i;

	if (buf[0] == 48) {
		onoff = 0;
	} else if (buf[0] == 49) {
		onoff = 1;
	} else {
		input_err(true,&info->client->dev, "%s [ERROR] Unknown value [%c]\n", __func__, buf[0]);
		goto exit;
	}
	
	if (onoff == 0) {
		info->irq_checked= 0;
	} else if (onoff == 1) {
		info->irq_checked= 1;
		for(i=0; i<info->key_num;i++)
			info->irq_key_count[i] = 0;
	} else {
		input_err(true, &info->client->dev, "%s - unknown value %d\n", __func__, onoff);
		goto error;
	}

exit:
	input_info(true,&info->client->dev, "%s - %d [DONE]\n", __func__,onoff);
	return count;

error:
	input_err(true,&info->client->dev, "%s - %d [ERROR]\n", __func__,onoff);
	return count;	
}

#ifdef CONFIG_TOUCHKEY_GRIP
static ssize_t mip4_tk_cmd_grip_test(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mip4_tk_info *info = dev_get_drvdata(dev);
	int ret;
	u8 test_type;
	char data[10];
	int i_x;

	input_dbg(true,&info->client->dev, "%s [START] \n", __func__);
	
	if (!strcmp(attr->attr.name, "grip_cp")) {
		test_type = MIP_TEST_TYPE_CP;
	} else {
		input_err(true,&info->client->dev, "%s [ERROR] Unknown test [%s]\n", __func__, attr->attr.name);
		ret = snprintf(buf, PAGE_SIZE, "%s\n", "ERROR : Unknown test type");
		goto error;
	}

	if (mip4_tk_run_test(info, test_type)) {
		input_err(true,&info->client->dev, "%s [ERROR] mip4_tk_run_test\n", __func__);
		ret = snprintf(buf, PAGE_SIZE, "%s\n", "ERROR");
		goto error;
	}

	input_dbg(true,&info->client->dev, "%s [DONE]\n", __func__);

	memset(info->print_buf, 0, PAGE_SIZE);
	sprintf(info->print_buf, "\n=== %s ===\n\n", attr->attr.name);
	
	//print table header
	for (i_x = 0; i_x < info->grip_ch; i_x++) {
		printk("[%5d]", i_x);
		sprintf(data, "[%5d]", i_x);
		strcat(info->print_buf, data);
		memset(data, 0, 10);
	}
	
	printk("\n");
	sprintf(data, "\n");
	strcat(info->print_buf, data);
	memset(data, 0, 10);

	//print table
	for (i_x = 0; i_x < info->grip_ch; i_x++) {
		printk(" %6d", info->image_buf[info->key_num+ i_x]);
		sprintf(data, " %6d", info->image_buf[info->key_num+ i_x]);
			
		strcat(info->print_buf, data);
		memset(data, 0, 10);
	}

	printk("\n");
	sprintf(data, "\n");
	strcat(info->print_buf, data);
	memset(data, 0, 10);

	ret = snprintf(buf, PAGE_SIZE, "%s\n", info->print_buf);
	return ret;

error:
	dev_err(&info->client->dev, "%s [ERROR]\n", __func__);
	return ret;
}

static ssize_t touchkey_sar_enable(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct mip4_tk_info *info = dev_get_drvdata(dev);
	u8 wbuf[3];
	int buff;
	int ret;
	bool on;

	ret = sscanf(buf, "%d", &buff);	
	if (ret != 1) {
		input_err(true,&info->client->dev, "%s: cmd read err\n", __func__);
		return count;
	}	

	input_info(true,&info->client->dev,"%s (%d)\n", __func__, buff);	

	if (!(buff >= 0 && buff <= 3)) {
		input_err(true,&info->client->dev, "%s: wrong command(%d)\n",
				__func__, buff);
		return count;
	}	

	/*	sar enable param
	  *	0	off
	  *	1	on
	  *	2	force off
	  *	3	force off -> on
	  */	

	if (buff == 3) {
		info->sar_enable_off = 0;
		input_info(true,&info->client->dev,
				"%s : Power back off _ force off -> on (%d)\n",
				__func__, info->sar_enable);
		if (info->sar_enable)
			buff = 1;
		else
			return count;
	}

	if (info->sar_enable_off) {
		if (buff == 1)
			info->sar_enable = true;
		else
			info->sar_enable = false;
		input_info(true,&info->client->dev,
				"%s skip, Power back off _ force off mode (%d)\n",
				__func__, info->sar_enable);
		return count;
	}

	wbuf[0] = MIP_R0_CTRL;
	wbuf[1] = MIP_R1_CTRL_ENABLE_GRIP;	

	if (buff == 1) {
		on = true;
	} else if (buff == 2) {
		on = false;
		info->sar_enable_off = 1;
	} else {
		on = false;
	}
	wbuf[2] = on;

	if (mip4_tk_i2c_write(info, wbuf, 3)) {
		input_err(true,&info->client->dev, "%s [ERROR] mip4_tk_i2c_write\n", __func__);
		return count;
	}

	if (buff == 1) {
		info->sar_enable = true;
	} else {
		input_report_key(info->input_dev, KEY_CP_GRIP, 0x00);
		info->grip_event = 0;
		info->sar_enable = false;
	}
	
	input_info(true,&info->client->dev, "%s - addr[0x%02X%02X] value[%02X]\n", __func__, wbuf[0], wbuf[1], wbuf[2]);

	return count;
}

static ssize_t touchkey_grip_sw_reset(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct mip4_tk_info *info = dev_get_drvdata(dev);
	u8 wbuf[3];
	int buff;
	int ret;

	ret = sscanf(buf, "%d", &buff);
	if (ret != 1) {
		input_err(true,&info->client->dev,  "%s: cmd read err\n", __func__);
		return count;
	}	

	input_info(true,&info->client->dev,"%s (%d)\n", __func__, buff);	
	
	if (!(buff == 1)) {
		input_err(true,&info->client->dev,  "%s: wrong command(%d)\n",
			__func__, buff);
		return count;
	}	

	info->grip_event = 0;
	
	wbuf[0] = MIP_R0_CTRL;
	wbuf[1] = MIP_R1_CTRL_REBASELINE_GRIP;
	wbuf[2] = 1;

	if (mip4_tk_i2c_write(info, wbuf, 3)) {
		input_err(true,&info->client->dev, "%s [ERROR] mip4_tk_i2c_write\n", __func__);
		goto error;
	}

	return count;

error:
	input_err(true,&info->client->dev, "%s [ERROR]\n", __func__);
	return count;
}

static ssize_t touchkey_total_cap_show(struct device *dev,struct device_attribute *attr, char *buf)
{
	struct mip4_tk_info *info = dev_get_drvdata(dev);
	int ret=0;
	u8 wbuf[8];
	u8 rbuf[4];	

	wbuf[0] = MIP_R0_INFO;
	wbuf[1] = MIP_R1_INFO_CAPACITANCE_GRIP;	
	if (mip4_tk_i2c_read(info, wbuf, 2, rbuf, 2)) {
		input_err(true,&info->client->dev, "%s [ERROR] mip4_tk_i2c_read\n", __func__);
		return sprintf(buf, "%d\n", 0);		
	} else {
		input_info(true,&info->client->dev, "%s - addr[0x%02X%02X] value[%d][%d]\n", __func__, wbuf[0], wbuf[1], rbuf[0], rbuf[1]);
		ret = rbuf[0] | (rbuf[1] << 8);
	}	

	return sprintf(buf, "%d\n", ret/100);
}

static ssize_t touchkey_ref_cap_show(struct device *dev,struct device_attribute *attr, char *buf)
{
	struct mip4_tk_info *info = dev_get_drvdata(dev);
	int ret=0;
	u8 wbuf[8];
	u8 rbuf[4];	

	wbuf[0] = MIP_R0_INFO;
	wbuf[1] = MIP_R1_INFO_CAPACITANCE_GRIP_REF;	
	if (mip4_tk_i2c_read(info, wbuf, 2, rbuf, 2)) {
		input_err(true,&info->client->dev, "%s [ERROR] mip4_tk_i2c_read\n", __func__);
		return sprintf(buf, "%d\n", 0);		
	} else {
		input_info(true,&info->client->dev, "%s - addr[0x%02X%02X] value[%d][%d]\n", __func__, wbuf[0], wbuf[1], rbuf[0], rbuf[1]);
		ret = rbuf[0] | (rbuf[1] << 8);
	}	

	return sprintf(buf, "%d\n", ret/100);
}


static ssize_t touchkey_grip_show(struct device *dev,struct device_attribute *attr, char *buf)
{
	struct mip4_tk_info *info = dev_get_drvdata(dev);
	int type = 0;
	int retry = 10;

	type = MIP_IMG_TYPE_INTENSITY;	

	while (retry--) {
		if (info->test_busy == false) {
			break;
		}
		msleep(10);
	}
	if (retry <= 0) {
		input_err(true,&info->client->dev, "%s [ERROR] skip\n", __func__);
		goto exit;
	}

	if (mip4_tk_get_image(info, type)) {
		input_err(true,&info->client->dev, "%s [ERROR] mip_get_image\n", __func__);
		goto error;
	}

exit:
	input_info(true,&info->client->dev, "%s  value : %d\n", __func__, info->image_buf[2]);	

	return snprintf(buf, PAGE_SIZE, "%d\n", info->image_buf[2]);

error:
	input_err(true,&info->client->dev, "%s [ERROR]\n", __func__);
	return snprintf(buf, PAGE_SIZE, "NG\n");
}

static ssize_t touchkey_grip_raw_show(struct device *dev,struct device_attribute *attr, char *buf)
{
	struct mip4_tk_info *info = dev_get_drvdata(dev);
	int type = 0;
	int retry = 10;

	type = MIP_IMG_TYPE_RAWDATA;		

	while (retry--) {
		if (info->test_busy == false) {
			break;
		}
		msleep(10);
	}
	if (retry <= 0) {
		input_err(true,&info->client->dev, "%s [ERROR] skip\n", __func__);
		goto exit;
	}

	if (mip4_tk_get_image(info, type)) {
		input_err(true,&info->client->dev, "%s [ERROR] mip_get_image\n", __func__);
		goto error;
	}

exit:
	input_info(true,&info->client->dev, "%s  value : %d\n", __func__, info->image_buf[2]);

	return snprintf(buf, PAGE_SIZE, "%d\n", info->image_buf[2]);

error:
	input_err(true,&info->client->dev, "%s [ERROR]\n", __func__);
	return snprintf(buf, PAGE_SIZE, "NG\n");
}


static ssize_t touchkey_grip_gain_show(struct device *dev,struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d,%d,%d,%d\n", 0, 0, 0, 0);
}


static ssize_t touchkey_grip_check_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mip4_tk_info *info = dev_get_drvdata(dev);

	input_info(true,&info->client->dev, "%s - grip state[%d]\n", __func__, info->grip_event);

	return sprintf(buf, "%d\n", info->grip_event);
}

static ssize_t touchkey_grip_threshold_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mip4_tk_info *info = dev_get_drvdata(dev);
	u8 wbuf[8];
	u8 rbuf[4];	

	wbuf[0] = MIP_R0_INFO;
	
	wbuf[1] = MIP_R1_INFO_THD_CONTACT_GRIP;	
	if (mip4_tk_i2c_read(info, wbuf, 2, rbuf, 1)) {
		input_err(true,&info->client->dev, "%s [ERROR] mip4_tk_i2c_read\n", __func__);
		info->grip_p_thd = 0;
		return sprintf(buf, "%d\n", 0);		
	} else {
		input_info(true,&info->client->dev, "%s - addr[0x%02X%02X] value[%d]\n", __func__, wbuf[0], wbuf[1], rbuf[0]);
		info->grip_p_thd = rbuf[0];
	}	

	wbuf[1] = MIP_R1_INFO_THD_RELEASE_GRIP;	
	if (mip4_tk_i2c_read(info, wbuf, 2, rbuf, 1)) {
		input_err(true,&info->client->dev, "%s [ERROR] mip4_tk_i2c_read\n", __func__);
		info->grip_r_thd = 0;
		return sprintf(buf, "%d\n", 0);		
	} else {
		input_info(true,&info->client->dev, "%s - addr[0x%02X%02X] value[%d]\n", __func__, wbuf[0], wbuf[1], rbuf[0]);
		info->grip_r_thd = rbuf[0];
	}		

	wbuf[1] = MIP_R1_INFO_THD_BASELINE_GRIP;	
	if (mip4_tk_i2c_read(info, wbuf, 2, rbuf, 1)) {
		input_err(true,&info->client->dev, "%s [ERROR] mip4_tk_i2c_read\n", __func__);
		info->grip_n_thd = 0;
		return sprintf(buf, "%d\n", 0);		
	} else {
		input_info(true,&info->client->dev, "%s - addr[0x%02X%02X] value[%d]\n", __func__, wbuf[0], wbuf[1], rbuf[0]);
		info->grip_n_thd = rbuf[0];
	}			

	return sprintf(buf, "%d,%d,%d\n",info->grip_p_thd, info->grip_r_thd, info->grip_n_thd );
}

static ssize_t touchkey_grip_baseline_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mip4_tk_info *info = dev_get_drvdata(dev);
	int type = 0;
	int retry = 10;

	type = MIP_IMG_TYPE_BASELINE;	

	while (retry--) {
		if (info->test_busy == false) {
			break;
		}
		msleep(10);
	}
	if (retry <= 0) {
		input_err(true,&info->client->dev, "%s [ERROR] skip\n", __func__);
		goto exit;
	}

	if (mip4_tk_get_image(info, type)) {
		input_err(true,&info->client->dev, "%s [ERROR] mip_get_image\n", __func__);
		goto error;
	}

exit:
	input_info(true,&info->client->dev, "%s  value : %d\n", __func__, info->image_buf[2]);

	return snprintf(buf, PAGE_SIZE, "%d\n", info->image_buf[2]);

error:
	input_err(true,&info->client->dev, "%s [ERROR]\n", __func__);
	return snprintf(buf, PAGE_SIZE, "NG\n");
}

static ssize_t touchkey_grip_irq_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mip4_tk_info *info = dev_get_drvdata(dev);

	int result = 0;

	if (info->irq_count)
		result = -1;

	input_info(true, &info->client->dev, "%s - called\n", __func__);

	return snprintf(buf, PAGE_SIZE, "%d,%d,%d\n",result, info->irq_count, info->max_diff);
}

static ssize_t touchkey_grip_irq_count_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct mip4_tk_info *info = dev_get_drvdata(dev);

	u8 onoff = 0;

	if (buf[0] == 48) {
		onoff = 0;
	} else if (buf[0] == 49) {
		onoff = 1;
	} else {
		input_err(true,&info->client->dev, "%s [ERROR] Unknown value [%c]\n", __func__, buf[0]);
		goto exit;
	}
	
	if (onoff == 0) {
		info->abnormal_mode = 0;
	} else if (onoff == 1) {
		info->abnormal_mode = 1;
		info->irq_count = 0;
		info->max_diff = 0;
	} else {
		input_err(true, &info->client->dev, "%s - unknown value %d\n", __func__, onoff);
		goto error;
	}

exit:
	input_info(true,&info->client->dev, "%s - %d [DONE]\n", __func__,onoff);
	return count;

error:
	input_err(true,&info->client->dev, "%s - %d [ERROR]\n", __func__,onoff);
	return count;	
}
#endif

static ssize_t touchkey_chip_name(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mip4_tk_info *info = dev_get_drvdata(dev);

	input_info(true,&info->client->dev, "%s\n", __func__);

	return sprintf(buf, CHIP_NAME);
}

static DEVICE_ATTR(touchkey_firm_version_phone, S_IRUGO, mip4_tk_fw_version_phone, NULL);
static DEVICE_ATTR(touchkey_firm_version_panel, S_IRUGO, mip4_tk_fw_version_panel, NULL);
static DEVICE_ATTR(fw_version_ic, S_IRUGO, mip4_tk_cmd_fw_version_ic, NULL);
static DEVICE_ATTR(fw_version_bin, S_IRUGO, mip4_tk_cmd_fw_version_bin, NULL);
static DEVICE_ATTR(fw_update, S_IRUGO, mip4_tk_cmd_fw_update, NULL);
static DEVICE_ATTR(touchkey_firm_update, S_IWUSR | S_IWGRP, NULL, mip4_tk_fw_update);
static DEVICE_ATTR(touchkey_firm_update_status, S_IRUGO, mip4_tk_cmd_fw_update_status, NULL);
static DEVICE_ATTR(touchkey_recent, S_IRUGO, mip4_tk_cmd_image, NULL);
static DEVICE_ATTR(touchkey_recent_raw, S_IRUGO, mip4_tk_cmd_image, NULL);
static DEVICE_ATTR(touchkey_back, S_IRUGO, mip4_tk_cmd_image, NULL);
static DEVICE_ATTR(touchkey_back_raw, S_IRUGO, mip4_tk_cmd_image, NULL);
static DEVICE_ATTR(touchkey_threshold, S_IRUGO, mip4_tk_cmd_threshold, NULL);
static DEVICE_ATTR(touchkey_earjack, S_IWUSR | S_IWGRP, NULL, mip4_tk_cmd_key_recal);
static DEVICE_ATTR(touchkey_recal, S_IRUGO | S_IWUSR | S_IWGRP, mip4_tk_cmd_recal_show, mip4_tk_cmd_recal_store);
static DEVICE_ATTR(touchkey_enable, S_IRUGO | S_IWUSR | S_IWGRP, mip4_tk_cmd_enable_show, mip4_tk_cmd_enable_store);
static DEVICE_ATTR(touchkey_irq_count, S_IRUGO | S_IWUSR | S_IWGRP, mip4_tk_cmd_key_irq_count_show, mip4_tk_cmd_key_irq_count_store);
#ifdef CONFIG_TOUCHKEY_GRIP
static DEVICE_ATTR(grip_cp, S_IRUGO, mip4_tk_cmd_grip_test, NULL);
static DEVICE_ATTR(grip_enable, S_IRUGO | S_IWUSR | S_IWGRP, mip4_tk_cmd_enable_show, mip4_tk_cmd_enable_store);
static DEVICE_ATTR(sar_enable, S_IWUSR | S_IWGRP, NULL, touchkey_sar_enable);
static DEVICE_ATTR(sw_reset, S_IWUSR | S_IWGRP, NULL, touchkey_grip_sw_reset);
static DEVICE_ATTR(touchkey_total_cap, S_IRUGO, touchkey_total_cap_show, NULL);
static DEVICE_ATTR(touchkey_ref_cap, S_IRUGO, touchkey_ref_cap_show, NULL);
static DEVICE_ATTR(touchkey_grip, S_IRUGO, touchkey_grip_show, NULL);
static DEVICE_ATTR(touchkey_grip_raw, S_IRUGO, touchkey_grip_raw_show, NULL);
static DEVICE_ATTR(touchkey_grip_gain, S_IRUGO, touchkey_grip_gain_show, NULL);
static DEVICE_ATTR(touchkey_grip_check, S_IRUGO, touchkey_grip_check_show, NULL);
static DEVICE_ATTR(touchkey_grip_threshold, S_IRUGO, touchkey_grip_threshold_show, NULL);
static DEVICE_ATTR(touchkey_grip_baseline, S_IRUGO, touchkey_grip_baseline_show, NULL);
static DEVICE_ATTR(grip_irq_count, S_IRUGO | S_IWUSR | S_IWGRP, touchkey_grip_irq_count_show, touchkey_grip_irq_count_store);
#endif
#ifdef MIP_USE_LED
static DEVICE_ATTR(led_onoff, S_IRUGO | S_IWUSR | S_IWGRP, mip4_tk_cmd_led_onoff_show, mip4_tk_cmd_led_onoff_store);
static DEVICE_ATTR(led_brightness, S_IRUGO | S_IWUSR | S_IWGRP, mip4_tk_cmd_led_brightness_show, mip4_tk_cmd_led_brightness_store);
#endif //MIP_USE_LED
static DEVICE_ATTR(touchkey_chip_name, S_IRUGO, touchkey_chip_name, NULL);

/**
* Sysfs - touchkey attr info
*/
static struct attribute *mip_cmd_key_attr[] = {
	&dev_attr_fw_version_ic.attr,
	&dev_attr_fw_version_bin.attr,	
	&dev_attr_touchkey_firm_version_phone.attr,
	&dev_attr_touchkey_firm_version_panel.attr,
	&dev_attr_touchkey_firm_update.attr,
	&dev_attr_touchkey_firm_update_status.attr,
	&dev_attr_fw_update.attr,
	&dev_attr_touchkey_recent.attr,
	&dev_attr_touchkey_recent_raw.attr,
	&dev_attr_touchkey_back.attr,
	&dev_attr_touchkey_back_raw.attr,
	&dev_attr_touchkey_threshold.attr,
	&dev_attr_touchkey_earjack.attr,	
	&dev_attr_touchkey_recal.attr,
	&dev_attr_touchkey_enable.attr,
	&dev_attr_touchkey_irq_count.attr,	
#ifdef CONFIG_TOUCHKEY_GRIP	
	&dev_attr_grip_cp.attr,
	&dev_attr_grip_enable.attr,
	&dev_attr_sar_enable.attr,	
	&dev_attr_sw_reset.attr,		
	&dev_attr_touchkey_total_cap.attr,	
	&dev_attr_touchkey_ref_cap.attr,		
	&dev_attr_touchkey_grip.attr,			
	&dev_attr_touchkey_grip_raw.attr,			
	&dev_attr_touchkey_grip_gain.attr,				
	&dev_attr_touchkey_grip_check.attr,		
	&dev_attr_touchkey_grip_threshold.attr,	
	&dev_attr_touchkey_grip_baseline.attr,	
	&dev_attr_grip_irq_count.attr,
#endif	
#ifdef MIP_USE_LED
	&dev_attr_led_onoff.attr,
	&dev_attr_led_brightness.attr,
#endif // MIP_USE_LED		
	&dev_attr_touchkey_chip_name.attr,
	NULL,
};

/**
* Sysfs - touchkey attr group info
*/
static const struct attribute_group mip_cmd_key_attr_group = {
	.attrs = mip_cmd_key_attr,
};

//extern struct class *sec_class;

/**
* Create sysfs command functions
*/
int mip4_tk_sysfs_cmd_create(struct mip4_tk_info *info)
{
	struct i2c_client *client = info->client;

	if (info->print_buf == NULL) {
		info->print_buf = kzalloc(sizeof(u8) * PAGE_SIZE, GFP_KERNEL);
	}
	if (info->image_buf == NULL) {
		info->image_buf = kzalloc(sizeof(int) * PAGE_SIZE, GFP_KERNEL);
	}

	info->key_dev = sec_device_create(info, "sec_touchkey");
	if (sysfs_create_group(&info->key_dev->kobj, &mip_cmd_key_attr_group)) {
		input_err(true, &client->dev, "%s [ERROR] sysfs_create_group\n", __func__);
		return -EAGAIN;
	}

	return 0;
}

/**
* Remove sysfs command functions
*/
void mip4_tk_sysfs_cmd_remove(struct mip4_tk_info *info)
{
//	device_destroy(sec_class,  0);
	sysfs_remove_group(&info->key_dev->kobj, &mip_cmd_key_attr_group);

	return;
}

#endif

