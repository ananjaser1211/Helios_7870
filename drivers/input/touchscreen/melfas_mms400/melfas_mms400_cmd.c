/*
 * MELFAS MMS400 Touchscreen
 *
 * Copyright (C) 2014 MELFAS Inc.
 *
 *
 * Command Functions (Optional)
 *
 */

#include "melfas_mms400.h"
#ifdef CONFIG_TRUSTONIC_TRUSTED_UI
#include <linux/trustedui.h>
#endif

#if MMS_USE_CMD_MODE

#define NAME_OF_UNKNOWN_CMD "not_support_cmd"

enum CMD_STATUS {
	CMD_STATUS_WAITING = 0,
	CMD_STATUS_RUNNING,
	CMD_STATUS_OK,
	CMD_STATUS_FAIL,
	CMD_STATUS_NONE,
};

static ssize_t scrub_position_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	char buff[256] = { 0 };

#ifdef CONFIG_SAMSUNG_PRODUCT_SHIP
	input_info(true, &info->client->dev,
			"%s: scrub_id: %d\n", __func__, info->scrub_id);
#else
	input_info(true, &info->client->dev,
			"%s: scrub_id: %d, X:%d, Y:%d\n", __func__,
			info->scrub_id, info->scrub_x, info->scrub_y);
#endif

	snprintf(buff, sizeof(buff), "%d %d %d", info->scrub_id, info->scrub_x, info->scrub_y);

	info->scrub_id = 0;
	info->scrub_x = 0;
	info->scrub_y = 0;

	return snprintf(buf, PAGE_SIZE, "%s", buff);
}

/**
 * Clear command result
 */
static void cmd_clear_result(struct mms_ts_info *info)
{
	char delim = ':';
	memset(info->cmd_result, 0x00, sizeof(u8) * 4096);
	memcpy(info->cmd_result, info->cmd, strnlen(info->cmd, CMD_LEN));
	strncat(info->cmd_result, &delim, 1);
}

static void cmd_check_sram(void *device_data);

/*
 * cmd_set_result_all
 * added for one command result store
 */
 
void cmd_set_result_all(struct mms_ts_info *info, char *buff, int len, char *item)
{
	char delim1 = ' ';
	char delim2 = ':';
	int cmd_result_len;

	cmd_result_len = (int)strlen(info->cmd_result_all) + len + 2 + (int)strlen(item);

	if (cmd_result_len >= CMD_RESULT_STR_LEN) {
		pr_err("%s %s: cmd length is over (%d)!!", SECLOG, __func__, cmd_result_len);
		return;
	}

	info->item_count++;
	strncat(info->cmd_result_all, &delim1, 1);
	strncat(info->cmd_result_all, item, strlen(item));
	strncat(info->cmd_result_all, &delim2, 1);
	strncat(info->cmd_result_all, buff, len);
}

/**
 * Set command result
 */
static void cmd_set_result(struct mms_ts_info *info, char *buf, int len)
{
	strncat(info->cmd_result, buf, len);
}

/**
 * Command : Update firmware
 */
static void cmd_fw_update(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	char buf[64] = { 0 };

	int fw_location = info->cmd_param[0];

	cmd_clear_result(info);

	/* Factory cmd for firmware update
	 * argument represent what is source of firmware like below.
	 *
	 * 0 : [BUILT_IN] Getting firmware which is for user.
	 * 1 : [UMS] Getting firmware from sd card.
	 * 2 : none
	 * 3 : [FFU] Getting firmware from air.
	 */

	switch (fw_location) {
	case 0:
		if (mms_fw_update_from_kernel(info, true)) {
			goto ERROR;
		}
		break;
	case 1:
		if (mms_fw_update_from_storage(info, true)) {
			goto ERROR;
		}
		break;
	case 3 :
		if (mms_fw_update_from_ffu(info, true)) {
			goto ERROR;
		}
		break;
	default:
		goto ERROR;
		break;
	}

	sprintf(buf, "%s", "OK");
	info->cmd_state = CMD_STATUS_OK;
	goto EXIT;

ERROR:
	sprintf(buf, "%s", "NG");
	info->cmd_state = CMD_STATUS_FAIL;
	goto EXIT;

EXIT:
	cmd_set_result(info, buf, strnlen(buf, sizeof(buf)));
	tsp_debug_dbg(true, &info->client->dev, "%s - cmd[%s] state[%d]\n",
		__func__, buf, info->cmd_state);
}

/**
 * Command : Get firmware version from MFSB file
 */
static void cmd_get_fw_ver_bin(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	char buf[64] = { 0 };

	const char *fw_name = info->dtdata->fw_name;
	const struct firmware *fw;
	struct mms_bin_hdr *fw_hdr;
	struct mms_fw_img **img;
	u8 ver_file[MMS_FW_MAX_SECT_NUM * 2];
	int i = 0;
	int offset = sizeof(struct mms_bin_hdr);

	cmd_clear_result(info);

	request_firmware(&fw, fw_name, &info->client->dev);

	if (!fw) {
		sprintf(buf, "%s", "NG");
		info->cmd_state = CMD_STATUS_FAIL;
		goto EXIT;
	}

	fw_hdr = (struct mms_bin_hdr *)fw->data;
	img = kzalloc(sizeof(*img) * fw_hdr->section_num, GFP_KERNEL);

	for (i = 0; i < fw_hdr->section_num; i++, offset += sizeof(struct mms_fw_img)) {
		img[i] = (struct mms_fw_img *)(fw->data + offset);
		ver_file[i * 2] = ((img[i]->version) >> 8) & 0xFF;
		ver_file[i * 2 + 1] = (img[i]->version) & 0xFF;
	}

	release_firmware(fw);

	sprintf(buf, "ME%02X%02X%02X", info->dtdata->panel, ver_file[3],ver_file[5]);
	info->cmd_state = CMD_STATUS_OK;

	kfree(img);

EXIT:
	cmd_set_result(info, buf, strnlen(buf, sizeof(buf)));
	if (info->cmd_all_factory_state == CMD_STATUS_RUNNING)
		cmd_set_result_all(info, buf, strnlen(buf, sizeof(buf)), "FW_VER_BIN");
	input_info(true, &info->client->dev, "%s: %s(%d)\n", __func__, info->cmd_result,
				(int)strnlen(info->cmd_result, sizeof(info->cmd_result)));
	tsp_debug_dbg(true, &info->client->dev, "%s - cmd[%s] state[%d]\n",
		__func__, buf, info->cmd_state);

}

/**
 * Command : Get firmware version from IC
 */
static void cmd_get_fw_ver_ic(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	char buf[64] = { 0 };
	u8 rbuf[64];

	cmd_clear_result(info);

	if (mms_get_fw_version(info, rbuf)) {
		sprintf(buf, "%s", "NG");
		info->cmd_state = CMD_STATUS_FAIL;
		goto EXIT;
	}

	info->boot_ver_ic = rbuf[1];
	info->core_ver_ic = rbuf[3];
	info->config_ver_ic = rbuf[5];

	sprintf(buf, "ME%02X%02X%02X",
		info->dtdata->panel, info->core_ver_ic, info->config_ver_ic);
	info->cmd_state = CMD_STATUS_OK;

EXIT:
	cmd_set_result(info, buf, strnlen(buf, sizeof(buf)));
	if (info->cmd_all_factory_state == CMD_STATUS_RUNNING)
		cmd_set_result_all(info, buf, strnlen(buf, sizeof(buf)), "FW_VER_IC");
	input_info(true, &info->client->dev, "%s: %s(%d)\n", __func__, info->cmd_result,
				(int)strnlen(info->cmd_result, sizeof(info->cmd_result)));
	tsp_debug_dbg(true, &info->client->dev, "%s - cmd[%s] state[%d]\n",
		__func__, buf, info->cmd_state);
}

/**
 * Command : Get chip vendor
 */
static void cmd_get_chip_vendor(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	char buf[64] = { 0 };
	//u8 rbuf[64];

	cmd_clear_result(info);

	sprintf(buf, "MELFAS");
	cmd_set_result(info, buf, strnlen(buf, sizeof(buf)));

	if (info->cmd_all_factory_state == CMD_STATUS_RUNNING)
		cmd_set_result_all(info, buf, strnlen(buf, sizeof(buf)), "IC_VENDOR");
	
	info->cmd_state = CMD_STATUS_OK;

	tsp_debug_dbg(true, &info->client->dev, "%s - cmd[%s] state[%d]\n",
		__func__, buf, info->cmd_state);
		
	tsp_debug_dbg(true, &info->client->dev, "%s: %s(%d)\n", __func__, info->cmd_result,
				(int)strnlen(info->cmd_result, sizeof(info->cmd_result)));

	return;
}

/**
 * Command : Get chip name
 */
static void cmd_get_chip_name(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	char buf[64] = { 0 };
	//u8 rbuf[64];

	cmd_clear_result(info);

	sprintf(buf, CHIP_NAME);
	cmd_set_result(info, buf, strnlen(buf, sizeof(buf)));

	if (info->cmd_all_factory_state == CMD_STATUS_RUNNING)
		cmd_set_result_all(info, buf, strnlen(buf, sizeof(buf)), "IC_NAME");
	
	info->cmd_state = CMD_STATUS_OK;

	tsp_debug_dbg(true, &info->client->dev, "%s - cmd[%s] state[%d]\n",
		__func__, buf, info->cmd_state);
		
	tsp_debug_dbg(true, &info->client->dev, "%s: %s(%d)\n", __func__, info->cmd_result,
				(int)strnlen(info->cmd_result, sizeof(info->cmd_result)));
				
	return;
}

static void cmd_get_config_ver(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	char buf[64] = { 0 };

	cmd_clear_result(info);
	sprintf(buf, "%s_ME_%02d%02d",
		info->product_name, info->fw_month, info->fw_date);
	cmd_set_result(info, buf, strnlen(buf, sizeof(buf)));

	info->cmd_state = CMD_STATUS_OK;

	tsp_debug_dbg(true, &info->client->dev, "%s - cmd[%s] state[%d]\n",
		__func__, buf, info->cmd_state);
}

static void get_checksum_data(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	char buf[64] = { 0 };
	u8 rbuf[64];
	u8 wbuf[64];
	int val;

	cmd_clear_result(info);

	wbuf[0] = MIP_R0_INFO;
	wbuf[1] = MIP_R1_INFO_CHECKSUM_REALTIME;
	if (mms_i2c_read(info, wbuf, 2, rbuf, 1)) {
		info->cmd_state = CMD_STATUS_FAIL;
		goto EXIT;
	}

	val = rbuf[0];

	sprintf(buf, "%d", val);
	cmd_set_result(info, buf, strnlen(buf, sizeof(buf)));

	info->cmd_state = CMD_STATUS_OK;

EXIT:
	tsp_debug_err(true, &info->client->dev, "%s - cmd[%s] state[%d]\n",
		__func__, buf, info->cmd_state);
}
/**
 * Command : Get X ch num
 */
static void cmd_get_x_num(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	char buf[64] = { 0 };
	u8 rbuf[64];
	u8 wbuf[64];
	int val;

	cmd_clear_result(info);

	wbuf[0] = MIP_R0_INFO;
	wbuf[1] = MIP_R1_INFO_NODE_NUM_X;
	if (mms_i2c_read(info, wbuf, 2, rbuf, 1)) {
		info->cmd_state = CMD_STATUS_FAIL;
		goto EXIT;
	}

	val = rbuf[0];

	sprintf(buf, "%d", val);
	cmd_set_result(info, buf, strnlen(buf, sizeof(buf)));

	info->cmd_state = CMD_STATUS_OK;

EXIT:
	tsp_debug_dbg(true, &info->client->dev, "%s - cmd[%s] state[%d]\n",
		__func__, buf, info->cmd_state);
}

/**
 * Command : Get Y ch num
 */
static void cmd_get_y_num(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	char buf[64] = { 0 };
	u8 rbuf[64];
	u8 wbuf[64];
	int val;

	cmd_clear_result(info);

	wbuf[0] = MIP_R0_INFO;
	wbuf[1] = MIP_R1_INFO_NODE_NUM_Y;
	if (mms_i2c_read(info, wbuf, 2, rbuf, 1)) {
		info->cmd_state = CMD_STATUS_FAIL;
		goto EXIT;
	}

	val = rbuf[0];

	sprintf(buf, "%d", val);
	cmd_set_result(info, buf, strnlen(buf, sizeof(buf)));

	info->cmd_state = CMD_STATUS_OK;

EXIT:
	tsp_debug_dbg(true, &info->client->dev, "%s - cmd[%s] state[%d]\n",
		__func__, buf, info->cmd_state);
}

/**
 * Command : Get X resolution
 */
static void cmd_get_max_x(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	char buf[64] = { 0 };
	u8 rbuf[64];
	u8 wbuf[64];
	int val;

	cmd_clear_result(info);

	wbuf[0] = MIP_R0_INFO;
	wbuf[1] = MIP_R1_INFO_RESOLUTION_X;
	if (mms_i2c_read(info, wbuf, 2, rbuf, 2)) {
		info->cmd_state = CMD_STATUS_FAIL;
		goto EXIT;
	}

	val = (rbuf[0]) | (rbuf[1] << 8);

	sprintf(buf, "%d", val);
	cmd_set_result(info, buf, strnlen(buf, sizeof(buf)));

	info->cmd_state = CMD_STATUS_OK;

EXIT:
	tsp_debug_dbg(true, &info->client->dev, "%s - cmd[%s] state[%d]\n",
		__func__, buf, info->cmd_state);
}

/**
 * Command : Get Y resolution
 */
static void cmd_get_max_y(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	char buf[64] = { 0 };
	u8 rbuf[64];
	u8 wbuf[64];
	int val;

	cmd_clear_result(info);

	wbuf[0] = MIP_R0_INFO;
	wbuf[1] = MIP_R1_INFO_RESOLUTION_Y;
	if (mms_i2c_read(info, wbuf, 2, rbuf, 2)) {
		info->cmd_state = CMD_STATUS_FAIL;
		goto EXIT;
	}

	val = (rbuf[0]) | (rbuf[1] << 8);

	sprintf(buf, "%d", val);
	cmd_set_result(info, buf, strnlen(buf, sizeof(buf)));

	info->cmd_state = CMD_STATUS_OK;

EXIT:
	tsp_debug_dbg(true, &info->client->dev, "%s - cmd[%s] state[%d]\n",
		__func__, buf, info->cmd_state);
}

/**
 * Command : Power off
 */
static void cmd_module_off_master(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	char buf[64] = { 0 };

	cmd_clear_result(info);

	mms_power_control(info, 0);

	sprintf(buf, "%s", "OK");
	info->cmd_state = CMD_STATUS_OK;

	cmd_set_result(info, buf, strnlen(buf, sizeof(buf)));
	tsp_debug_dbg(true, &info->client->dev, "%s - cmd[%s] state[%d]\n",
		__func__, buf, info->cmd_state);
}

/**
 * Command : Power on
 */
static void cmd_module_on_master(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	char buf[64] = { 0 };

	cmd_clear_result(info);

	mms_power_control(info, 1);

	sprintf(buf, "%s", "OK");
	info->cmd_state = CMD_STATUS_OK;

	cmd_set_result(info, buf, strnlen(buf, sizeof(buf)));
	tsp_debug_dbg(true, &info->client->dev, "%s - cmd[%s] state[%d]\n",
		__func__, buf, info->cmd_state);
}

/**
 * Command : Read intensity image
 */
static void cmd_read_intensity(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	char buf[64] = { 0 };

	int min = 999999;
	int max = -999999;
	int i = 0;

	cmd_clear_result(info);

	if (mms_get_image(info, MIP_IMG_TYPE_INTENSITY)) {
		sprintf(buf, "%s", "NG");
		info->cmd_state = CMD_STATUS_FAIL;
		goto EXIT;
	}

	for (i = 0; i < (info->node_x * info->node_y); i++) {
		if (info->image_buf[i] > max) {
			max = info->image_buf[i];
		}
		if (info->image_buf[i] < min) {
			min = info->image_buf[i];
		}
	}

	sprintf(buf, "%d,%d", min, max);
	info->cmd_state = CMD_STATUS_OK;

EXIT:
	cmd_set_result(info, buf, strnlen(buf, sizeof(buf)));
	tsp_debug_dbg(true, &info->client->dev, "%s - cmd[%s] state[%d]\n",
		__func__, buf, info->cmd_state);
}

/**
 * Command : Get intensity data
 */
static void cmd_get_intensity(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	char buf[64] = { 0 };

	int x = info->cmd_param[0];
	int y = info->cmd_param[1];
	int idx = 0;

	cmd_clear_result(info);

	if ((x < 0) || (x >= info->node_x) || (y < 0) || (y >= info->node_y)) {
		sprintf(buf, "%s", "NG");
		info->cmd_state = CMD_STATUS_FAIL;
		goto EXIT;
	}

	idx = y * info->node_x + x;

	sprintf(buf, "%d", info->image_buf[idx]);
	info->cmd_state = CMD_STATUS_OK;

EXIT:
	cmd_set_result(info, buf, strnlen(buf, sizeof(buf)));
	tsp_debug_dbg(true, &info->client->dev, "%s - cmd[%s] state[%d]\n",
		__func__, buf, info->cmd_state);
}

/**
 * Command : Read rawdata image
 */
static void cmd_read_rawdata(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	char buf[64] = { 0 };

	int min = 999999;
	int max = -999999;
	int i = 0;

	cmd_clear_result(info);

	if (mms_get_image(info, MIP_IMG_TYPE_RAWDATA)) {
		sprintf(buf, "%s", "NG");
		info->cmd_state = CMD_STATUS_FAIL;
		goto EXIT;
	}

	for (i = 0; i < (info->node_x * info->node_y); i++) {
		if (info->image_buf[i] > max) {
			max = info->image_buf[i];
		}
		if (info->image_buf[i] < min) {
			min = info->image_buf[i];
		}
	}

	sprintf(buf, "%d,%d", min, max);
	info->cmd_state = CMD_STATUS_OK;

EXIT:
	cmd_set_result(info, buf, strnlen(buf, sizeof(buf)));
	tsp_debug_dbg(true, &info->client->dev, "%s - cmd[%s] state[%d]\n",
		__func__, buf, info->cmd_state);
}

/**
 * Command : Get rawdata
 */
static void cmd_get_rawdata(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	char buf[64] = { 0 };

	int x = info->cmd_param[0];
	int y = info->cmd_param[1];
	int idx = 0;

	cmd_clear_result(info);

	if ((x < 0) || (x >= info->node_x) || (y < 0) || (y >= info->node_y)) {
		sprintf(buf, "%s", "NG");
		info->cmd_state = CMD_STATUS_FAIL;
		goto EXIT;
	}

	idx = y * info->node_x + x;

	sprintf(buf, "%d", info->image_buf[idx]);
	info->cmd_state = CMD_STATUS_OK;

EXIT:
	cmd_set_result(info, buf, strnlen(buf, sizeof(buf)));
	tsp_debug_dbg(true, &info->client->dev, "%s - cmd[%s] state[%d]\n",
		__func__, buf, info->cmd_state);
}

/**
 * Command : Run cm delta test
 */
static void cmd_run_test_cm_delta(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	char buf[64] = { 0 };

	int min = 999999;
	int max = -999999;
	int i = 0;

	cmd_clear_result(info);

	if (mms_run_test(info, MIP_TEST_TYPE_CM_DELTA)) {
		sprintf(buf, "%s", "NG");
		info->cmd_state = CMD_STATUS_FAIL;
		goto EXIT;
	}

	for (i = 0; i < (info->node_x * info->node_y); i++) {
		if (info->image_buf[i] > max) {
			max = info->image_buf[i];
		}
		if (info->image_buf[i] < min) {
			min = info->image_buf[i];
		}
	}

	sprintf(buf, "%d,%d", min, max);
	info->cmd_state = CMD_STATUS_OK;

EXIT:
	cmd_set_result(info, buf, strnlen(buf, sizeof(buf)));
	if (info->cmd_all_factory_state == CMD_STATUS_RUNNING)
		cmd_set_result_all(info, buf, strnlen(buf, sizeof(buf)), "CM_DELTA");
	input_info(true, &info->client->dev, "%s: %s(%d)\n", __func__, info->cmd_result,
				(int)strnlen(info->cmd_result, sizeof(info->cmd_result)));
	tsp_debug_dbg(true, &info->client->dev, "%s - cmd[%s] state[%d]\n",
		__func__, buf, info->cmd_state);
}

/**
 * Command : Get result of cm delta test
 */
static void cmd_get_cm_delta(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	char buf[64] = { 0 };

	int x = info->cmd_param[0];
	int y = info->cmd_param[1];
	int idx = 0;

	cmd_clear_result(info);

	if ((x < 0) || (x >= info->node_x) || (y < 0) || (y >= info->node_y)) {
		sprintf(buf, "%s", "NG");
		info->cmd_state = CMD_STATUS_FAIL;
		goto EXIT;
	}

	idx = y * info->node_x + x;

	sprintf(buf, "%d", info->image_buf[idx]);
	info->cmd_state = CMD_STATUS_OK;

EXIT:
	cmd_set_result(info, buf, strnlen(buf, sizeof(buf)));
	tsp_debug_dbg(true, &info->client->dev, "%s - cmd[%s] state[%d]\n",
		__func__, buf, info->cmd_state);
}

/**
 * Command : Run cm abs test
 */
static void cmd_run_test_cm_abs(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	char buf[64] = { 0 };

	int min = 999999;
	int max = -999999;
	int i = 0;

	cmd_clear_result(info);

	if (mms_run_test(info, MIP_TEST_TYPE_CM_ABS)) {
		sprintf(buf, "%s", "NG");
		info->cmd_state = CMD_STATUS_FAIL;
		goto EXIT;
	}

	for (i = 0; i < (info->node_x * info->node_y); i++) {
		if (info->image_buf[i] > max) {
			max = info->image_buf[i];
		}
		if (info->image_buf[i] < min) {
			min = info->image_buf[i];
		}
	}

	sprintf(buf, "%d,%d", min, max);
	info->cmd_state = CMD_STATUS_OK;

EXIT:
	cmd_set_result(info, buf, strnlen(buf, sizeof(buf)));
	tsp_debug_dbg(true, &info->client->dev, "%s - cmd[%s] state[%d]\n",
		__func__, buf, info->cmd_state);
}

/**
 * Command : Get result of cm abs test
 */
static void cmd_get_cm_abs(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	char buf[64] = { 0 };

	int x = info->cmd_param[0];
	int y = info->cmd_param[1];
	int idx = 0;

	cmd_clear_result(info);

	if ((x < 0) || (x >= info->node_x) || (y < 0) || (y >= info->node_y)) {
		sprintf(buf, "%s", "NG");
		info->cmd_state = CMD_STATUS_FAIL;
		goto EXIT;
	}

	idx = y * info->node_x + x;

	sprintf(buf, "%d", info->image_buf[idx]);
	info->cmd_state = CMD_STATUS_OK;

EXIT:
	cmd_set_result(info, buf, strnlen(buf, sizeof(buf)));
	tsp_debug_dbg(true, &info->client->dev, "%s - cmd[%s] state[%d]\n",
		__func__, buf, info->cmd_state);
}

static void check_connection(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	char buf[64] = { 0 };
	
	cmd_clear_result(info);

	if (mms_run_test(info, MIP_TEST_TYPE_OPEN))
		goto EXIT;

	input_info(true, &info->client->dev, "%s: connection check(%d)\n", __func__, info->image_buf[0]);

	if (!info->image_buf[0])
		goto EXIT;

	sprintf(buf, "%s", "OK");
	cmd_set_result(info, buf, strnlen(buf, sizeof(buf)));

	info->cmd_state = CMD_STATUS_OK;

	input_info(true, &info->client->dev, "%s - cmd[%s] state[%d]\n",
		__func__, buf, info->cmd_state);

	return;
EXIT:
	sprintf(buf, "%s", "NG");
	info->cmd_state = CMD_STATUS_FAIL;
	cmd_set_result(info, buf, strnlen(buf, sizeof(buf)));
	tsp_debug_dbg(true, &info->client->dev, "%s - cmd[%s] state[%d]\n",
		__func__, buf, info->cmd_state);
}

static void cmd_get_threshold(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	char buf[64] = { 0 };

	cmd_clear_result(info);
	sprintf(buf, "55");
	cmd_set_result(info, buf, strnlen(buf, sizeof(buf)));

	info->cmd_state = CMD_STATUS_OK;

	tsp_debug_dbg(true, &info->client->dev, "%s - cmd[%s] state[%d]\n",
		__func__, buf, info->cmd_state);
}

static void get_intensity_all_data(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	int ret;
	int length;

	cmd_clear_result(info);

	ret = mms_get_image(info, MIP_IMG_TYPE_INTENSITY);
	if (ret < 0) {
		tsp_debug_err(true, &info->client->dev, "%s: failed to read intensity, %d\n", __func__, ret);
		sprintf(info->print_buf, "%s", "NG");
		goto out;
	}

	info->cmd_state = CMD_STATUS_OK;

	length = strlen(info->print_buf);
	tsp_debug_err(true, &info->client->dev, "%s: length is %d\n", __func__, length);

out:
	cmd_set_result(info, info->print_buf, length);

	mutex_lock(&info->lock);
	info->cmd_busy = false;
	mutex_unlock(&info->lock);

	info->cmd_state = CMD_STATUS_WAITING;
}

static void get_rawdata_all_data(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	int ret;
	int length;

	cmd_clear_result(info);

	ret = mms_get_image(info, MIP_IMG_TYPE_RAWDATA);
	if (ret < 0) {
		tsp_debug_err(true, &info->client->dev, "%s: failed to read raw data, %d\n", __func__, ret);
		sprintf(info->print_buf, "%s", "NG");
		goto out;
	}

	info->cmd_state = CMD_STATUS_OK;
	length = strlen(info->print_buf);
	tsp_debug_err(true, &info->client->dev, "%s: length is %d\n", __func__, length);

out:
	cmd_set_result(info, info->print_buf, length);

	mutex_lock(&info->lock);
	info->cmd_busy = false;
	mutex_unlock(&info->lock);

	info->cmd_state = CMD_STATUS_WAITING;
}

static void get_cm_delta_all_data(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	int ret;
	int length;

	cmd_clear_result(info);

	ret = mms_run_test(info, MIP_TEST_TYPE_CM_DELTA);
	if (ret < 0) {
		tsp_debug_err(true, &info->client->dev, "%s: failed to read cm delta, %d\n", __func__, ret);
		sprintf(info->print_buf, "%s", "NG");
		goto out;
	}

	info->cmd_state = CMD_STATUS_OK;
	length = strlen(info->print_buf);
	tsp_debug_err(true, &info->client->dev, "%s: length is %d\n", __func__, length);

out:
	cmd_set_result(info, info->print_buf, length);

	mutex_lock(&info->lock);
	info->cmd_busy = false;
	mutex_unlock(&info->lock);

	info->cmd_state = CMD_STATUS_WAITING;
}

static void get_cm_abs_all_data(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	int ret;
	int length;

	cmd_clear_result(info);

	ret = mms_run_test(info, MIP_TEST_TYPE_CM_ABS);
	if (ret < 0) {
		tsp_debug_err(true, &info->client->dev, "%s: failed to read cm abs, %d\n", __func__, ret);
		sprintf(info->print_buf, "%s", "NG");
		goto out;
	}

	info->cmd_state = CMD_STATUS_OK;
	length = strlen(info->print_buf);
	tsp_debug_err(true, &info->client->dev, "%s: length is %d\n", __func__, length);

out:
	cmd_set_result(info, info->print_buf, length);

	mutex_lock(&info->lock);
	info->cmd_busy = false;
	mutex_unlock(&info->lock);

	info->cmd_state = CMD_STATUS_WAITING;
}

static void dead_zone_enable(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	int length = 0;
	int enable = info->cmd_param[0];
	u8 wbuf[4];
	int status;

	cmd_clear_result(info);

	tsp_debug_info(true, &info->client->dev, "%s %d\n", __func__, enable);

	if (enable)
		status = 0;
	else
		status = 2;

	wbuf[0] = MIP_R0_CTRL;
	wbuf[1] = MIP_R1_CTRL_DISABLE_EDGE_EXPAND;
	wbuf[2] = status;

	if ((enable == 0) || (enable == 1)) {
		if (mms_i2c_write(info, wbuf, 3)) {
			tsp_debug_err(true, &info->client->dev, "%s [ERROR] mms_i2c_write\n", __func__);
			goto out;
		} else
			tsp_debug_info(true, &info->client->dev, "%s - value[%d]\n", __func__, wbuf[2]);
	} else {
		tsp_debug_err(true, &info->client->dev, "%s [ERROR] Unknown value[%d]\n", __func__, status);
		goto out;
	}
	tsp_debug_dbg(true, &info->client->dev, "%s [DONE] \n", __func__);

	info->cmd_state = CMD_STATUS_OK;
	length = strlen(info->print_buf);
	tsp_debug_err(true, &info->client->dev, "%s: length is %d\n", __func__, length);

out:
	cmd_set_result(info, info->print_buf, length);

	mutex_lock(&info->lock);
	info->cmd_busy = false;
	mutex_unlock(&info->lock);

	info->cmd_state = CMD_STATUS_WAITING;
}

static void factory_cmd_result_all(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	char buff[16] = { 0 };
	
	cmd_clear_result(info);
	
	info->item_count = 0;
	memset(info->cmd_result_all, 0x00, CMD_RESULT_STR_LEN);
	
	info->cmd_all_factory_state = CMD_STATUS_RUNNING;
	
	snprintf(buff, sizeof(buff), "%d", info->dtdata->item_version);
	cmd_set_result_all(info, buff, strnlen(buff, sizeof(buff)), "ITEM_VERSION");
	
	cmd_get_chip_vendor(info);
	cmd_get_chip_name(info);
	cmd_get_fw_ver_bin(info);
	cmd_get_fw_ver_ic(info);
	
	cmd_run_test_cm_delta(info);
	cmd_check_sram(info);

	info->cmd_all_factory_state = CMD_STATUS_OK;

	input_info(true, &info->client->dev, "%s: %d%s\n", __func__, info->item_count,
				info->cmd_result_all);
}

#ifdef GLOVE_MODE
static void glove_mode(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	int length = 0;
	int enable = info->cmd_param[0];
	u8 wbuf[4];

	cmd_clear_result(info);

	tsp_debug_info(true, &info->client->dev, "%s %d\n", __func__, enable);

	wbuf[0] = MIP_R0_CTRL;
	wbuf[1] = MIP_R1_CTRL_GLOVE_MODE;
	wbuf[2] = enable;

	if ((enable == 0) || (enable == 1)) {
		if (mms_i2c_write(info, wbuf, 3)) {
			tsp_debug_err(true, &info->client->dev, "%s [ERROR] mms_i2c_write\n", __func__);
			goto out;
		} else
			tsp_debug_info(true, &info->client->dev, "%s - value[%d]\n", __func__, wbuf[2]);
	} else {
		tsp_debug_err(true, &info->client->dev, "%s [ERROR] Unknown value[%d]\n", __func__, enable);
		goto out;
	}
	tsp_debug_dbg(true, &info->client->dev, "%s [DONE] \n", __func__);

	info->cmd_state = CMD_STATUS_OK;
	length = strlen(info->print_buf);
	tsp_debug_err(true, &info->client->dev, "%s: length is %d\n", __func__, length);

out:
	cmd_set_result(info, info->print_buf, length);

	mutex_lock(&info->lock);
	info->cmd_busy = false;
	mutex_unlock(&info->lock);

	info->cmd_state = CMD_STATUS_WAITING;
}
#endif

#ifdef COVER_MODE
static void clear_cover_mode(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	int length = 0;
	int enable = info->cmd_param[0];
	u8 wbuf[4];

	cmd_clear_result(info);

	tsp_debug_info(true, &info->client->dev, "%s %d\n", __func__, enable);

	if (!info->enabled) {
		tsp_debug_err(true, &info->client->dev,
			"%s : tsp disabled\n", __func__);
		goto out;
	}

	wbuf[0] = MIP_R0_CTRL;
	wbuf[1] = MIP_R1_CTRL_WINDOW_MODE;
	wbuf[2] = enable;

	if ((enable >= 0) || (enable <= 3)) {
		if (mms_i2c_write(info, wbuf, 3)) {
			tsp_debug_err(true, &info->client->dev, "%s [ERROR] mms_i2c_write\n", __func__);
			goto out;
		} else{
			tsp_debug_info(true, &info->client->dev, "%s - value[%d]\n", __func__, wbuf[2]);
		}
	} else {
		tsp_debug_err(true, &info->client->dev, "%s [ERROR] Unknown value[%d]\n", __func__, enable);
		goto out;
	}
	tsp_debug_dbg(true, &info->client->dev, "%s [DONE] \n", __func__);

	info->cmd_state = CMD_STATUS_OK;
	length = strlen(info->print_buf);
	tsp_debug_err(true, &info->client->dev, "%s: length is %d\n", __func__, length);

out:
	if(enable > 0)
		info->cover_mode = true;
	else
		info->cover_mode = false;
	cmd_set_result(info, info->print_buf, length);

	mutex_lock(&info->lock);
	info->cmd_busy = false;
	mutex_unlock(&info->lock);

	info->cmd_state = CMD_STATUS_WAITING;
}
#endif

#ifdef CONFIG_TRUSTONIC_TRUSTED_UI
static void tui_mode_cmd(struct mms_ts_info *info)
{
	char buf[16] = "TUImode:FAIL";
	cmd_clear_result(info);
	cmd_set_result(info, buf, strnlen(buf, sizeof(buf)));

	mutex_lock(&info->lock);
	info->cmd_busy = false;
	mutex_unlock(&info->lock);

	info->cmd_state = CMD_STATUS_WAITING;
	dev_err(&info->client->dev, "%s: %s(%d)\n", __func__, buf,
		  (int)strnlen(buf, sizeof(buf)));
}
#endif

static void spay_enable(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	char buf[64] = { 0 };

	cmd_clear_result(info);

	if (!info->dtdata->support_lpm) {
		input_err(true, &info->client->dev, "%s not supported\n", __func__);
		snprintf(buf, sizeof(buf), "%s", NAME_OF_UNKNOWN_CMD);
		info->cmd_state = CMD_STATUS_NONE;
		goto out;
	}

	if (info->cmd_param[0]) {
		info->lowpower_mode = true;
		info->lowpower_flag = info->lowpower_flag | MMS_LPM_FLAG_SPAY;
	} else {
		info->lowpower_flag = info->lowpower_flag & ~(MMS_LPM_FLAG_SPAY);
		if (!info->lowpower_flag)
			info->lowpower_mode = false;
	}

	input_info(true, &info->client->dev, "%s: %s mode, %x\n",
			__func__, info->lowpower_mode ? "LPM" : "normal",
			info->lowpower_flag);
	snprintf(buf, sizeof(buf), "%s", "OK");

out:
	cmd_set_result(info, buf, strnlen(buf, sizeof(buf)));

	mutex_lock(&info->lock);
	info->cmd_busy = false;
	mutex_unlock(&info->lock);

	info->cmd_state = CMD_STATUS_WAITING;
	input_info(true, &info->client->dev, "%s: %s\n", __func__, buf);
}

static void aod_enable(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	char buf[64] = { 0 };

	cmd_clear_result(info);

	if (!info->dtdata->support_lpm) {
		input_err(true, &info->client->dev, "%s not supported\n", __func__);
		snprintf(buf, sizeof(buf), "%s", NAME_OF_UNKNOWN_CMD);
		info->cmd_state = CMD_STATUS_NONE;
		goto out;
	}

	if (info->cmd_param[0]) {
		info->lowpower_mode = true;
		info->lowpower_flag = info->lowpower_flag | MMS_LPM_FLAG_AOD;
	} else {
		info->lowpower_flag = info->lowpower_flag & ~(MMS_LPM_FLAG_AOD);
		if (!info->lowpower_flag)
			info->lowpower_mode = false;
	}
	input_info(true, &info->client->dev, "%s: %s mode, %x\n",
			__func__, info->lowpower_mode ? "LPM" : "normal",
			info->lowpower_flag);
	snprintf(buf, sizeof(buf), "%s", "OK");
	info->cmd_state = CMD_STATUS_OK;

out:
	cmd_set_result(info, buf, strnlen(buf, sizeof(buf)));

	mutex_lock(&info->lock);
	info->cmd_busy = false;
	mutex_unlock(&info->lock);

	info->cmd_state = CMD_STATUS_WAITING;
	input_info(true, &info->client->dev, "%s: %s\n", __func__, buf);
}

static void set_aod_rect(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	char buf[64] = { 0 };
	u8 data[11] = {0};
	int i;

	cmd_clear_result(info);

	if (!info->enabled) {
		input_err(true, &info->client->dev, "%s: [ERROR] Touch is stopped\n", __func__);
		snprintf(buf, sizeof(buf), "%s", "TSP turned off");
		info->cmd_state = CMD_STATUS_NONE;
		goto out;
	}

	input_info(true, &info->client->dev, "%s: w:%d, h:%d, x:%d, y:%d\n",
			__func__, info->cmd_param[0], info->cmd_param[1],
			info->cmd_param[2], info->cmd_param[3]);

	data[0] = MIP_R0_AOT;
	data[1] = MIP_R0_AOT_BOX_W;
	for (i = 0; i < 4; i++) {
		data[i * 2 + 2] = info->cmd_param[i] & 0xFF;
		data[i * 2 + 3] = (info->cmd_param[i] >> 8) & 0xFF;
	}

	disable_irq(info->client->irq);

	if (mms_i2c_write(info, data, 10)) {
		input_err(true, &info->client->dev, "%s [ERROR] mms_i2c_write\n", __func__);
		enable_irq(info->client->irq);
		goto out;
	}

	enable_irq(info->client->irq);

	snprintf(buf, sizeof(buf), "%s", "OK");
	info->cmd_state = CMD_STATUS_OK;
out:
	cmd_set_result(info, buf, strnlen(buf, sizeof(buf)));

	mutex_lock(&info->lock);
	info->cmd_busy = false;
	mutex_unlock(&info->lock);

	info->cmd_state = CMD_STATUS_WAITING;
	input_info(true, &info->client->dev, "%s: %s\n", __func__, buf);
}


static void get_aod_rect(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	char buf[64] = { 0 };
	u8 wbuf[16];
	u8 rbuf[16];
	u16 rect_data[4] = {0, };
	int i;

	cmd_clear_result(info);

	disable_irq(info->client->irq);

	wbuf[0] = MIP_R0_AOT;
	wbuf[1] = MIP_R0_AOT_BOX_W;

	if (mms_i2c_read(info, wbuf, 2, rbuf, 8)) {
		input_err(true, &info->client->dev, "%s [ERROR] mms_i2c_write\n", __func__);
		goto out;
	}

	enable_irq(info->client->irq);

	for (i = 0; i < 4; i++)
		rect_data[i] = (rbuf[i * 2 + 1] & 0xFF) << 8 | (rbuf[i * 2] & 0xFF);

	input_info(true, &info->client->dev, "%s: w:%d, h:%d, x:%d, y:%d\n",
			__func__, rect_data[0], rect_data[1], rect_data[2], rect_data[3]);

	snprintf(buf, sizeof(buf), "%s", "OK");
	info->cmd_state = CMD_STATUS_OK;
out:
	cmd_set_result(info, buf, strnlen(buf, sizeof(buf)));

	mutex_lock(&info->lock);
	info->cmd_busy = false;
	mutex_unlock(&info->lock);

	info->cmd_state = CMD_STATUS_WAITING;
	input_info(true, &info->client->dev, "%s: %s\n", __func__, buf);
	enable_irq(info->client->irq);
}

/**
 * Command : Check SRAM failure
 */
static void cmd_check_sram(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	char buf[64] = { 0 };
	int val;

	cmd_clear_result(info);

	val = (int) info->sram_addr[0];

	if(val != 0)
		sprintf(buf, "0x%x", val);
	else
		sprintf(buf, "%s", "0");

	cmd_set_result(info, buf, strnlen(buf, sizeof(buf)));

	if (info->cmd_all_factory_state == CMD_STATUS_RUNNING)
		cmd_set_result_all(info, buf, strnlen(buf, sizeof(buf)), "SRAM");

	info->cmd_state = CMD_STATUS_OK;

	tsp_debug_dbg(true, &info->client->dev, "%s - cmd[%s] state[%d]\n",
		__func__, buf, info->cmd_state);
		
	input_info(true, &info->client->dev, "%s: %s(%d)\n", __func__, info->cmd_result,
				(int)strnlen(info->cmd_result, sizeof(info->cmd_result)));

	return;
}

/**
 * Command : Unknown cmd
 */
static void cmd_unknown_cmd(void *device_data)
{
	struct mms_ts_info *info = (struct mms_ts_info *)device_data;
	char buf[16] = { 0 };

	cmd_clear_result(info);

	snprintf(buf, sizeof(buf), "%s", NAME_OF_UNKNOWN_CMD);
	cmd_set_result(info, buf, strnlen(buf, sizeof(buf)));

	info->cmd_state = CMD_STATUS_NONE;

	mutex_lock(&info->lock);
	info->cmd_busy = false;
	mutex_unlock(&info->lock);

	tsp_debug_dbg(true, &info->client->dev, "%s - cmd[%s] state[%d]\n",
		__func__, buf, info->cmd_state);
}

#ifdef CONFIG_TRUSTONIC_TRUSTED_UI
static void tui_mode_cmd(struct mms_ts_info *info);
#endif

#define MMS_CMD(name, func)	.cmd_name = name, .cmd_func = func

/**
 * Info of command function
 */
struct mms_cmd {
	struct list_head list;
	const char *cmd_name;
	void (*cmd_func) (void *device_data);
};

/**
 * List of command functions
 */
static struct mms_cmd mms_commands[] = {
	{MMS_CMD("fw_update", cmd_fw_update),},
	{MMS_CMD("get_fw_ver_bin", cmd_get_fw_ver_bin),},
	{MMS_CMD("get_fw_ver_ic", cmd_get_fw_ver_ic),},
	{MMS_CMD("get_chip_vendor", cmd_get_chip_vendor),},
	{MMS_CMD("get_chip_name", cmd_get_chip_name),},
	{MMS_CMD("get_checksum_data", get_checksum_data),},
	{MMS_CMD("get_x_num", cmd_get_x_num),},
	{MMS_CMD("get_y_num", cmd_get_y_num),},
	{MMS_CMD("get_max_x", cmd_get_max_x),},
	{MMS_CMD("get_max_y", cmd_get_max_y),},
	{MMS_CMD("module_off_master", cmd_module_off_master),},
	{MMS_CMD("module_on_master", cmd_module_on_master),},
	{MMS_CMD("run_intensity_read", cmd_read_intensity),},
	{MMS_CMD("get_intensity", cmd_get_intensity),},
	{MMS_CMD("run_rawdata_read", cmd_read_rawdata),},
	{MMS_CMD("get_rawdata", cmd_get_rawdata),},
	{MMS_CMD("run_inspection_read", cmd_run_test_cm_delta),},
	{MMS_CMD("get_inspection", cmd_get_cm_delta),},
	{MMS_CMD("run_cm_delta_read", cmd_run_test_cm_delta),},
	{MMS_CMD("get_cm_delta", cmd_get_cm_delta),},
	{MMS_CMD("run_cm_abs_read", cmd_run_test_cm_abs),},
	{MMS_CMD("get_cm_abs", cmd_get_cm_abs),},
	{MMS_CMD("get_config_ver", cmd_get_config_ver),},
	{MMS_CMD("get_threshold", cmd_get_threshold),},
	{MMS_CMD("get_intensity_all_data", get_intensity_all_data),},
	{MMS_CMD("get_rawdata_all_data", get_rawdata_all_data),},
	{MMS_CMD("get_cm_delta_all_data", get_cm_delta_all_data),},
	{MMS_CMD("get_cm_abs_all_data", get_cm_abs_all_data),},
	{MMS_CMD("dead_zone_enable", dead_zone_enable),},
#ifdef GLOVE_MODE
	{MMS_CMD("glove_mode", glove_mode),},
#endif
#ifdef COVER_MODE
	{MMS_CMD("clear_cover_mode", clear_cover_mode),},
#endif
	{MMS_CMD("module_off_slave", cmd_unknown_cmd),},
	{MMS_CMD("module_on_slave", cmd_unknown_cmd),},
	{MMS_CMD("spay_enable", spay_enable),},
	{MMS_CMD("aod_enable", aod_enable),},
	{MMS_CMD("set_aod_rect", set_aod_rect),},
	{MMS_CMD("get_aod_rect", get_aod_rect),},
	{MMS_CMD("check_sram", cmd_check_sram),},
	{MMS_CMD("check_connection", check_connection),},
	{MMS_CMD("factory_cmd_result_all", factory_cmd_result_all),},
	{MMS_CMD(NAME_OF_UNKNOWN_CMD, cmd_unknown_cmd),},
};

/**
 * Sysfs - recv command
 */
static ssize_t mms_sys_cmd(struct device *dev, struct device_attribute *devattr,
					const char *buf, size_t count)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	int ret;
	char *cur, *start, *end;
	char cbuf[CMD_LEN] = { 0 };
	int len, i;
	struct mms_cmd *mms_cmd_ptr = NULL;
	char delim = ',';
	bool cmd_found = false;
	int param_cnt = 0;

	if (!info) {
		pr_err("%s [ERROR] mms_ts_info not found\n", __func__);
		ret = -EINVAL;
		return count;
	}

	if (strlen(buf) >= CMD_LEN) {
		tsp_debug_err(true, &info->client->dev, "%s: cmd length is over (%s,%d)!!\n", __func__, buf, (int)strlen(buf));
		ret = -EINVAL;
		goto ERROR;
	}

	tsp_debug_dbg(true, &info->client->dev, "%s [START]\n", __func__);
	tsp_debug_dbg(true, &info->client->dev, "%s - input [%s]\n", __func__, buf);

	if (!info->input_dev) {
		tsp_debug_err(true, &info->client->dev,
			"%s [ERROR] input_dev not found\n", __func__);
		ret = -EINVAL;
		goto ERROR;
	}

	if (info->cmd_busy == true) {
		tsp_debug_err(true, &info->client->dev,
			"%s [ERROR] previous command is not ended\n", __func__);
		ret = -1;
		goto ERROR;
	}

	mutex_lock(&info->lock);
	info->cmd_busy = true;
	mutex_unlock(&info->lock);

	info->cmd_state = 1;
	for (i = 0; i < ARRAY_SIZE(info->cmd_param); i++) {
		info->cmd_param[i] = 0;
	}

	len = (int)count;
	if (*(buf + len - 1) == '\n') {
		len--;
	}

	memset(info->cmd, 0x00, ARRAY_SIZE(info->cmd));
	memcpy(info->cmd, buf, len);
	cur = strchr(buf, (int)delim);
	if (cur)
		memcpy(cbuf, buf, cur - buf);
	else
		memcpy(cbuf, buf, len);

	tsp_debug_dbg(true, &info->client->dev, "%s - command [%s]\n", __func__, cbuf);

	//command
	list_for_each_entry(mms_cmd_ptr, &info->cmd_list_head, list) {
		if (!strncmp(cbuf, mms_cmd_ptr->cmd_name, CMD_LEN)) {
			cmd_found = true;
			break;
		}
	}
	if (!cmd_found) {
		list_for_each_entry(mms_cmd_ptr, &info->cmd_list_head, list) {
			if (!strncmp(NAME_OF_UNKNOWN_CMD, mms_cmd_ptr->cmd_name, CMD_LEN)) {
				break;
			}
		}
	}

	//parameter
	if (cur && cmd_found) {
		cur++;
		start = cur;
		memset(cbuf, 0x00, ARRAY_SIZE(cbuf));

		do {
			if (*cur == delim || cur - buf == len) {
				end = cur;
				memcpy(cbuf, start, end - start);
				*(cbuf + strnlen(cbuf, ARRAY_SIZE(cbuf))) = '\0';
				if (kstrtoint(cbuf, 10, info->cmd_param + param_cnt) < 0) {
					goto ERROR;
				}
				start = cur + 1;
				memset(cbuf, 0x00, ARRAY_SIZE(cbuf));
				param_cnt++;
			}
			cur++;
		} while ((cur - buf <= len) && (param_cnt < CMD_PARAM_NUM));
	}

	//print
	tsp_debug_dbg(true, &info->client->dev, "%s - cmd [%s]\n", __func__, mms_cmd_ptr->cmd_name);
	for (i = 0; i < param_cnt; i++) {
		tsp_debug_dbg(true, &info->client->dev,
			"%s - param #%d [%d]\n", __func__, i, info->cmd_param[i]);
	}

#ifdef CONFIG_TRUSTONIC_TRUSTED_UI
	if (TRUSTEDUI_MODE_INPUT_SECURED & trustedui_get_current_mode())
		tui_mode_cmd(info);
	else
#endif
	//execute
	mms_cmd_ptr->cmd_func(info);

	tsp_debug_dbg(true, &info->client->dev, "%s [DONE]\n", __func__);
	return count;

ERROR:
	tsp_debug_err(true, &info->client->dev, "%s [ERROR]\n", __func__);
	return count;
}
static DEVICE_ATTR(cmd, S_IWUSR | S_IWGRP, NULL, mms_sys_cmd);

/**
 * Sysfs - print command status
 */
static ssize_t mms_sys_cmd_status(struct device *dev,
				struct device_attribute *devattr,char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	int ret;
	char cbuf[32] = {0};

	tsp_debug_dbg(true, &info->client->dev, "%s [START]\n", __func__);

	tsp_debug_dbg(true, &info->client->dev, "%s - status [%d]\n", __func__, info->cmd_state);

	if (info->cmd_state == CMD_STATUS_WAITING) {
		snprintf(cbuf, sizeof(cbuf), "WAITING");
	} else if (info->cmd_state == CMD_STATUS_RUNNING) {
		snprintf(cbuf, sizeof(cbuf), "RUNNING");
	} else if (info->cmd_state == CMD_STATUS_OK) {
		snprintf(cbuf, sizeof(cbuf), "OK");
	} else if (info->cmd_state == CMD_STATUS_FAIL) {
		snprintf(cbuf, sizeof(cbuf), "FAIL");
	} else if (info->cmd_state == CMD_STATUS_NONE) {
		snprintf(cbuf, sizeof(cbuf), "NOT_APPLICABLE");
	}

	ret = snprintf(buf, PAGE_SIZE, "%s\n", cbuf);
	//memset(info->print_buf, 0, 4096);

	tsp_debug_dbg(true, &info->client->dev, "%s [DONE]\n", __func__);

	return ret;
}
static DEVICE_ATTR(cmd_status, S_IRUGO, mms_sys_cmd_status, NULL);

/**
 * Sysfs - print one command status
 */
static ssize_t mms_sys_cmd_status_all(struct device *dev,
				struct device_attribute *devattr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	int ret;
	char cbuf[32] = {0};

	tsp_debug_dbg(true, &info->client->dev, "%s [START]\n", __func__);

	tsp_debug_dbg(true, &info->client->dev, "%s - status [%d]\n", __func__, info->cmd_all_factory_state);

	if (info->cmd_all_factory_state == CMD_STATUS_WAITING) {
		snprintf(cbuf, sizeof(cbuf), "WAITING");
	} else if (info->cmd_all_factory_state == CMD_STATUS_RUNNING) {
		snprintf(cbuf, sizeof(cbuf), "RUNNING");
	} else if (info->cmd_all_factory_state == CMD_STATUS_OK) {
		snprintf(cbuf, sizeof(cbuf), "OK");
	} else if (info->cmd_all_factory_state == CMD_STATUS_FAIL) {
		snprintf(cbuf, sizeof(cbuf), "FAIL");
	} else if (info->cmd_all_factory_state == CMD_STATUS_NONE) {
		snprintf(cbuf, sizeof(cbuf), "NOT_APPLICABLE");
	}

	ret = snprintf(buf, PAGE_SIZE, "%s\n", cbuf);
	//memset(info->print_buf, 0, 4096);

	tsp_debug_dbg(true, &info->client->dev, "%s [DONE]\n", __func__);

	return ret;
}
static DEVICE_ATTR(cmd_status_all, S_IRUGO, mms_sys_cmd_status_all, NULL);

/**
 * Sysfs - print command result
 */
static ssize_t mms_sys_cmd_result(struct device *dev,
				struct device_attribute *devattr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	int ret;

	tsp_debug_dbg(true, &info->client->dev, "%s [START]\n", __func__);

	tsp_debug_dbg(true, &info->client->dev, "%s - result [%s]\n", __func__, info->cmd_result);

	mutex_lock(&info->lock);
	info->cmd_busy = false;
	mutex_unlock(&info->lock);

	info->cmd_state = CMD_STATUS_WAITING;

	//EXIT:
	ret = snprintf(buf, PAGE_SIZE, "%s\n", info->cmd_result);
	//memset(info->print_buf, 0, 4096);

	tsp_debug_dbg(true, &info->client->dev, "%s [DONE]\n", __func__);

	return ret;
}
static DEVICE_ATTR(cmd_result, S_IRUGO, mms_sys_cmd_result, NULL);

/**
 * Sysfs - print command result all  "one command"
 */
static ssize_t cmd_show_result_all(struct device *dev,
				 struct device_attribute *devattr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	int size;

	tsp_debug_dbg(true, &info->client->dev, "%s [START]\n", __func__);
	
	mutex_lock(&info->lock);
	info->cmd_busy = false;
	mutex_unlock(&info->lock);

	info->cmd_state = CMD_STATUS_WAITING;
	pr_info("%s: %d, %s\n",__func__, info->item_count, info->cmd_result_all);
	size = snprintf(buf, CMD_RESULT_STR_LEN, "%d%s\n", info->item_count, info->cmd_result_all);

	info->item_count = 0;
	memset(info->cmd_result_all, 0x00, CMD_RESULT_STR_LEN);

	return size;
}
static DEVICE_ATTR(cmd_result_all, S_IRUGO, cmd_show_result_all, NULL);

/**
 * Sysfs - print command list
 */
static ssize_t mms_sys_cmd_list(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	int ret;
	int i = 0;
	char buffer[info->cmd_buffer_size];
	char buffer_name[CMD_LEN];

	tsp_debug_dbg(true, &info->client->dev, "%s [START]\n", __func__);

	snprintf(buffer, 30, "== Command list ==\n");
	while (strncmp(mms_commands[i].cmd_name, NAME_OF_UNKNOWN_CMD, CMD_LEN) != 0) {
		snprintf(buffer_name, CMD_LEN, "%s\n", mms_commands[i].cmd_name);
		strcat(buffer, buffer_name);
		i++;
	}

	tsp_debug_dbg(true, &info->client->dev, "%s - cmd[%s] state[%d]\n",
		__func__, buf, info->cmd_state);

	ret = snprintf(buf, PAGE_SIZE, "%s\n", buffer);
	//memset(info->print_buf, 0, 4096);

	tsp_debug_dbg(true, &info->client->dev, "%s [DONE]\n", __func__);

	return ret;
}
static DEVICE_ATTR(cmd_list, S_IRUGO, mms_sys_cmd_list, NULL);
static DEVICE_ATTR(scrub_pos, S_IRUGO, scrub_position_show, NULL);

static ssize_t read_multi_count_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);

	tsp_debug_info(true, &info->client->dev, "%s: %d\n", __func__, info->multi_count);

	return snprintf(buf, PAGE_SIZE, "%d", info->multi_count);
}

static ssize_t clear_multi_count_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);

	info->multi_count = 0;
	tsp_debug_info(true, &info->client->dev, "%s: clear\n", __func__);

	return count;
}

static ssize_t read_comm_err_count_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);

	tsp_debug_info(true, &info->client->dev, "%s: %d\n", __func__, info->comm_err_count);

	return snprintf(buf, PAGE_SIZE, "%d", info->comm_err_count);
}

static ssize_t clear_comm_err_count_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);

	info->comm_err_count = 0;

	tsp_debug_info(true, &info->client->dev, "%s: clear\n", __func__);

	return count;
}

static ssize_t read_module_id_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "ME%02X%02X%02X0000",
		info->dtdata->panel, info->core_ver_ic, info->config_ver_ic);
}

static ssize_t read_vendor_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "MELFAS");
}

static ssize_t sensitivity_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	
	u8 wbuf[64];
	u8 rbuf[64];
	int ret;
	int i;
	u16 sTspSensitivity[5] = {0, };

	wbuf[0] = MIP_R0_CTRL;
	wbuf[1] = MIP_TS_READ_SENSITIVITY_VALUE;			
			
	ret = mms_i2c_read(info, wbuf, 2, rbuf, 10);
	
	if (ret != 0) {
		input_err(true, &info->client->dev, "%s: i2c fail!, %d\n", __func__, ret);
		return ret;
	}
	
	for(i = 0; i < 5; i++)
		sTspSensitivity[i] = (rbuf[i * 2 + 1] & 0xFF) << 8 | (rbuf[i * 2] & 0xFF);
		
	input_info(true, &info->client->dev, "%s: sensitivity mode,%d,%d,%d,%d,%d\n", __func__,
		sTspSensitivity[0], sTspSensitivity[1], sTspSensitivity[2], sTspSensitivity[3], sTspSensitivity[4]);
		
	return snprintf(buf, PAGE_SIZE,"%d,%d,%d,%d,%d",
			sTspSensitivity[0], sTspSensitivity[1], sTspSensitivity[2], sTspSensitivity[3], sTspSensitivity[4]);
		
}

static ssize_t sensitivity_mode_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{

	struct mms_ts_info *info = dev_get_drvdata(dev);

	u8 wbuf[64];
	int ret;
	//u8 temp;
	unsigned long value = 0;

	wbuf[0] = MIP_R0_CTRL;
	wbuf[1] = MIP_R1_CTRL_GLOVE_MODE;

	if (count > 2)
		return -EINVAL;

	ret = kstrtoul(buf, 10, &value);
	if (ret != 0)
		return ret;

	input_err(true, &info->client->dev, "%s: enable:%d\n", __func__, value);
	
	if (value == 1) {
		wbuf[2] = 1; // enable
		//temp = 0x1;
		ret = mms_i2c_write(info, wbuf, 3);
		if (ret < 0) {
			input_err(true, &info->client->dev, "%s: send sensitivity mode on fail!\n", __func__);
			return ret;
		}
		input_info(true, &info->client->dev, "%s: enable end\n", __func__);
	} else {
		wbuf[2] = 0; // disable
		//temp = 0x1;
		ret = mms_i2c_write(info, wbuf, 3);
		if (ret < 0) {
			input_err(true, &info->client->dev, "%s: send sensitivity mode off fail!\n", __func__);
			return ret;
		}
		input_info(true, &info->client->dev, "%s: disable end\n", __func__);
	}

	input_info(true, &info->client->dev, "%s: done\n", __func__);

	return count;
}

static DEVICE_ATTR(multi_count, S_IRUGO | S_IWUSR | S_IWGRP, read_multi_count_show, clear_multi_count_store);
static DEVICE_ATTR(comm_err_count, S_IRUGO | S_IWUSR | S_IWGRP, read_comm_err_count_show, clear_comm_err_count_store);
static DEVICE_ATTR(module_id, S_IRUGO, read_module_id_show, NULL);
static DEVICE_ATTR(vendor, S_IRUGO, read_vendor_show, NULL);
static DEVICE_ATTR(sensitivity_mode, S_IRUGO | S_IWUSR | S_IWGRP, sensitivity_mode_show, sensitivity_mode_store);

/**
 * Sysfs - cmd attr info
 */
static struct attribute *mms_cmd_attr[] = {
	&dev_attr_cmd.attr,
	&dev_attr_cmd_status.attr,
	&dev_attr_cmd_status_all.attr,
	&dev_attr_cmd_result.attr,
	&dev_attr_cmd_result_all.attr,
	&dev_attr_cmd_list.attr,
	&dev_attr_scrub_pos.attr,
	&dev_attr_multi_count.attr,
	&dev_attr_comm_err_count.attr,
	&dev_attr_module_id.attr,
	&dev_attr_vendor.attr,
	&dev_attr_sensitivity_mode.attr,
	NULL,
};

/**
 * Sysfs - cmd attr group info
 */
static const struct attribute_group mms_cmd_attr_group = {
	.attrs = mms_cmd_attr,
};

/**
 * Create sysfs command functions
 */
int mms_sysfs_cmd_create(struct mms_ts_info *info)
{
	struct i2c_client *client = info->client;
	int i = 0;

	info->cmd_result = kzalloc(sizeof(u8) * 4096, GFP_KERNEL);
	//init cmd list
	INIT_LIST_HEAD(&info->cmd_list_head);
	info->cmd_buffer_size = 0;

	for (i = 0; i < ARRAY_SIZE(mms_commands); i++) {
		list_add_tail(&mms_commands[i].list, &info->cmd_list_head);
		if (mms_commands[i].cmd_name) {
			info->cmd_buffer_size += strlen(mms_commands[i].cmd_name) + 1;
		}
	}

	info->cmd_busy = false;
	info->print_buf = kzalloc(sizeof(u8) * 4096, GFP_KERNEL);

	//create sysfs
	if (sysfs_create_group(&client->dev.kobj, &mms_cmd_attr_group)) {
		tsp_debug_err(true, &client->dev, "%s [ERROR] sysfs_create_group\n", __func__);
		return -EAGAIN;
	}

	//create class
	//info->cmd_class = class_create(THIS_MODULE, "melfas");
	//info->cmd_dev = device_create(info->cmd_class, NULL, info->cmd_dev_t, NULL, "touchscreen");
	info->cmd_dev = sec_device_create(info, "tsp");
	if (sysfs_create_group(&info->cmd_dev->kobj, &mms_cmd_attr_group)) {
		tsp_debug_err(true, &client->dev, "%s [ERROR] sysfs_create_group\n", __func__);
		return -EAGAIN;
	}

	if (sysfs_create_link(&info->cmd_dev->kobj, &info->input_dev->dev.kobj, "input")) {
		tsp_debug_err(true, &client->dev, "%s [ERROR] sysfs_create_link\n", __func__);
		return -EAGAIN;
	}

	return 0;
}

/**
 * Remove sysfs command functions
 */
void mms_sysfs_cmd_remove(struct mms_ts_info *info)
{
	sysfs_remove_group(&info->client->dev.kobj, &mms_cmd_attr_group);

	kfree(info->cmd_result);
	kfree(info->print_buf);

	return;
}

#endif
