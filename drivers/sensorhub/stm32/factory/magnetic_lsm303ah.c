/*
 *  Copyright (C) 2012, Samsung Electronics Co. Ltd. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 */
#include "../ssp.h"

/*************************************************************************/
/* factory Sysfs                                                         */
/*************************************************************************/

#define VENDOR		"STM"
#define CHIP_ID		"LSM303HA"
#define MAG_HW_OFFSET_FILE_PATH	"/efs/FactoryApp/hw_offset"
#define LSM303HA_STATIC_ELLIPSOID_MATRIX 	{10000, 0, 0, 0, 10000, 0, 0, 0, 10000}

int mag_open_hwoffset(struct ssp_data *data)
{
	int iRet = 0;
#if 0
	mm_segment_t old_fs;
	struct file *cal_filp = NULL;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	cal_filp = filp_open(MAG_HW_OFFSET_FILE_PATH, O_RDONLY, 0);
	if (IS_ERR(cal_filp)) {
		pr_err("[SSP] %s: filp_open failed\n", __func__);
		set_fs(old_fs);
		iRet = PTR_ERR(cal_filp);

		data->magoffset.x = 0;
		data->magoffset.y = 0;
		data->magoffset.z = 0;

		return iRet;
	}

	iRet = cal_filp->f_op->read(cal_filp, (char *)&data->magoffset,
		3 * sizeof(char), &cal_filp->f_pos);
	if (iRet != 3 * sizeof(char)) {
		pr_err("[SSP] %s: filp_open failed\n", __func__);
		iRet = -EIO;
	}

	filp_close(cal_filp, current->files);
	set_fs(old_fs);

	ssp_dbg("[SSP]: %s: %d, %d, %d\n", __func__,
		(s8)data->magoffset.x,
		(s8)data->magoffset.y,
		(s8)data->magoffset.z);

	if ((data->magoffset.x == 0) && (data->magoffset.y == 0)
		&& (data->magoffset.z == 0))
		return ERROR;
#endif
	return iRet;
}

int mag_store_hwoffset(struct ssp_data *data)
{
	int iRet = 0;
#if 0
	struct file *cal_filp = NULL;
	mm_segment_t old_fs;

	if (get_hw_offset(data) < 0) {
		pr_err("[SSP]: %s - get_hw_offset failed\n", __func__);
		return ERROR;
	} else {
		old_fs = get_fs();
		set_fs(KERNEL_DS);

		cal_filp = filp_open(MAG_HW_OFFSET_FILE_PATH,
			O_CREAT | O_TRUNC | O_WRONLY, 0660);
		if (IS_ERR(cal_filp)) {
			pr_err("[SSP]: %s - Can't open hw_offset file\n",
				__func__);
			set_fs(old_fs);
			iRet = PTR_ERR(cal_filp);
			return iRet;
		}
		iRet = cal_filp->f_op->write(cal_filp,
			(char *)&data->magoffset,
			3 * sizeof(char), &cal_filp->f_pos);
		if (iRet != 3 * sizeof(char)) {
			pr_err("[SSP]: %s - Can't write the hw_offset"
				" to file\n", __func__);
			iRet = -EIO;
		}
		filp_close(cal_filp, current->files);
		set_fs(old_fs);
		return iRet;
	}
#endif
	return iRet;
}

int set_hw_offset(struct ssp_data *data)
{
	int iRet = 0;
#if 0
	struct ssp_msg *msg;

	if (!(data->uSensorState & 0x04)) {
		pr_info("[SSP]: %s - Skip this function!!!"\
			", magnetic sensor is not connected(0x%llx)\n",
			__func__, data->uSensorState);
		return iRet;
	}

	msg = kzalloc(sizeof(*msg), GFP_KERNEL);
	if (msg == NULL) {
		pr_err("[SSP] %s, failed to alloc memory for ssp_msg\n", __func__);
		return -ENOMEM;
	}
	msg->cmd = MSG2SSP_AP_SET_MAGNETIC_HWOFFSET;
	msg->length = 3;
	msg->options = AP2HUB_WRITE;
	msg->buffer = (char*) kzalloc(3, GFP_KERNEL);
	msg->free_buffer = 1;

	msg->buffer[0] = data->magoffset.x;
	msg->buffer[1] = data->magoffset.y;
	msg->buffer[2] = data->magoffset.z;

	iRet = ssp_spi_async(data, msg);

	if (iRet != SUCCESS) {
		pr_err("[SSP]: %s - i2c fail %d\n", __func__, iRet);
		iRet = ERROR;
	}

	pr_info("[SSP]: %s: x: %d, y: %d, z: %d\n", __func__,
		(s8)data->magoffset.x, (s8)data->magoffset.y, (s8)data->magoffset.z);
#endif
	return iRet;
}

int set_static_matrix(struct ssp_data *data)
{
	int iRet = SUCCESS;
	struct ssp_msg *msg;
	s16 static_matrix[9] = LSM303HA_STATIC_ELLIPSOID_MATRIX;

	if (!(data->uSensorState & 0x04)) {
		pr_info("[SSP]: %s - Skip this function!!!"\
			", magnetic sensor is not connected(0x%llx)\n",
			__func__, data->uSensorState);
		return iRet;
	}

	msg = kzalloc(sizeof(*msg), GFP_KERNEL);
	if (msg == NULL) {
		pr_err("[SSP] %s, failed to alloc memory for ssp_msg\n", __func__);
		return -ENOMEM;
	}
	msg->cmd = MSG2SSP_AP_SET_MAGNETIC_STATIC_MATRIX;
	msg->length = 18;
	msg->options = AP2HUB_WRITE;
	msg->buffer = (char*) kzalloc(18, GFP_KERNEL);

	msg->free_buffer = 1;
	if (data->static_matrix == NULL)
		memcpy(msg->buffer, static_matrix, 18);
	else
		memcpy(msg->buffer, data->static_matrix, 18);

	iRet = ssp_spi_async(data, msg);

	if (iRet != SUCCESS) {
		pr_err("[SSP]: %s - i2c fail %d\n", __func__, iRet);
		iRet = ERROR;
	}
	pr_info("[SSP]: %s: finished \n", __func__);

	return iRet;
}

int get_hw_offset(struct ssp_data *data)
{
	int iRet = 0;
#if 0
	char buffer[3] = { 0, };

	struct ssp_msg *msg = kzalloc(sizeof(*msg), GFP_KERNEL);
	if (msg == NULL) {
		pr_err("[SSP] %s, failed to alloc memory for ssp_msg\n", __func__);
		return -ENOMEM;
	}
	msg->cmd = MSG2SSP_AP_GET_MAGNETIC_HWOFFSET;
	msg->length = 3;
	msg->options = AP2HUB_READ;
	msg->buffer = buffer;
	msg->free_buffer = 0;

	data->magoffset.x = 0;
	data->magoffset.y = 0;
	data->magoffset.z = 0;

	iRet = ssp_spi_sync(data, msg, 1000);

	if (iRet != SUCCESS) {
		pr_err("[SSP]: %s - i2c fail %d\n", __func__, iRet);
		iRet = ERROR;
	}

	data->magoffset.x = buffer[0];
	data->magoffset.y = buffer[1];
	data->magoffset.z = buffer[2];

	pr_info("[SSP]: %s: x: %d, y: %d, z: %d\n", __func__,
		(s8)data->magoffset.x,
		(s8)data->magoffset.y,
		(s8)data->magoffset.z);
#endif
	return iRet;
}

static ssize_t magnetic_vendor_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", VENDOR);
}

static ssize_t magnetic_name_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", CHIP_ID);
}

static int check_rawdata_spec(struct ssp_data *data)
{
	if ((data->buf[SENSOR_TYPE_GEOMAGNETIC_POWER].x == 0) &&
		(data->buf[SENSOR_TYPE_GEOMAGNETIC_POWER].y == 0) &&
		(data->buf[SENSOR_TYPE_GEOMAGNETIC_POWER].z == 0))
		return FAIL;
	else
		return SUCCESS;
}

static ssize_t raw_data_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ssp_data *data = dev_get_drvdata(dev);

	pr_info("[SSP] %s - %d,%d,%d\n", __func__,
		data->buf[SENSOR_TYPE_GEOMAGNETIC_POWER].x,
		data->buf[SENSOR_TYPE_GEOMAGNETIC_POWER].y,
		data->buf[SENSOR_TYPE_GEOMAGNETIC_POWER].z);

	if (data->bGeomagneticRawEnabled == false) {
		data->buf[SENSOR_TYPE_GEOMAGNETIC_POWER].x = -1;
		data->buf[SENSOR_TYPE_GEOMAGNETIC_POWER].y = -1;
		data->buf[SENSOR_TYPE_GEOMAGNETIC_POWER].z = -1;
	}
    
	return snprintf(buf, PAGE_SIZE, "%d,%d,%d\n",
		data->buf[SENSOR_TYPE_GEOMAGNETIC_POWER].x,
		data->buf[SENSOR_TYPE_GEOMAGNETIC_POWER].y,
		data->buf[SENSOR_TYPE_GEOMAGNETIC_POWER].z);
}

static ssize_t raw_data_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	char chTempbuf[4] = { 0 };
	int iRet;
	int64_t dEnable;
	struct ssp_data *data = dev_get_drvdata(dev);
	s32 dMsDelay = 20;
	memcpy(&chTempbuf[0], &dMsDelay, 4);

	iRet = kstrtoll(buf, 10, &dEnable);
	if (iRet < 0)
		return iRet;

	if (dEnable) {
		int iRetries = 50;
		data->buf[SENSOR_TYPE_GEOMAGNETIC_POWER].x = 0;
		data->buf[SENSOR_TYPE_GEOMAGNETIC_POWER].y = 0;
		data->buf[SENSOR_TYPE_GEOMAGNETIC_POWER].z = 0;

		send_instruction(data, ADD_SENSOR, SENSOR_TYPE_GEOMAGNETIC_POWER,
			chTempbuf, 4);

		do {
			msleep(20);
			if (check_rawdata_spec(data) == SUCCESS)
				break;
		} while (--iRetries);

		if (iRetries > 0) {
			pr_info("[SSP] %s - success, %d\n", __func__, iRetries);
			data->bGeomagneticRawEnabled = true;
		} else {
			pr_err("[SSP] %s - wait timeout, %d\n", __func__,
				iRetries);
			data->bGeomagneticRawEnabled = false;
		}
	} else {
		send_instruction(data, REMOVE_SENSOR, SENSOR_TYPE_GEOMAGNETIC_POWER,
			chTempbuf, 4);
		data->bGeomagneticRawEnabled = false;
	}

	return size;
}

static int check_data_spec(struct ssp_data *data)
{
	if ((data->buf[SENSOR_TYPE_GEOMAGNETIC_FIELD].x == 0) &&
		(data->buf[SENSOR_TYPE_GEOMAGNETIC_FIELD].y == 0) &&
		(data->buf[SENSOR_TYPE_GEOMAGNETIC_FIELD].z == 0))
		return FAIL;
	else if ((data->buf[SENSOR_TYPE_GEOMAGNETIC_FIELD].x > 16383) ||
		(data->buf[SENSOR_TYPE_GEOMAGNETIC_FIELD].x < -16383) ||
		(data->buf[SENSOR_TYPE_GEOMAGNETIC_FIELD].y > 16383) ||
		(data->buf[SENSOR_TYPE_GEOMAGNETIC_FIELD].y < -16383) ||
		(data->buf[SENSOR_TYPE_GEOMAGNETIC_FIELD].z > 16383) ||
		(data->buf[SENSOR_TYPE_GEOMAGNETIC_FIELD].z < -16383))
		return FAIL;
	else
		return SUCCESS;
}

static ssize_t adc_data_read(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	bool bSuccess = false;
	u8 chTempbuf[4] = { 0 };
	s16 iSensorBuf[3] = {0, };
	int iRetries = 10;
	struct ssp_data *data = dev_get_drvdata(dev);
	s32 dMsDelay = 20;
	memcpy(&chTempbuf[0], &dMsDelay, 4);

	data->buf[SENSOR_TYPE_GEOMAGNETIC_FIELD].x = 0;
	data->buf[SENSOR_TYPE_GEOMAGNETIC_FIELD].y = 0;
	data->buf[SENSOR_TYPE_GEOMAGNETIC_FIELD].z = 0;

	if (!(atomic64_read(&data->aSensorEnable) & (1 << SENSOR_TYPE_GEOMAGNETIC_FIELD)))
		send_instruction(data, ADD_SENSOR, SENSOR_TYPE_GEOMAGNETIC_FIELD,
			chTempbuf, 4);

	do {
		msleep(60);
		if (check_data_spec(data) == SUCCESS)
			break;
	} while (--iRetries);

	if (iRetries > 0)
		bSuccess = true;

	iSensorBuf[0] = data->buf[SENSOR_TYPE_GEOMAGNETIC_FIELD].x;
	iSensorBuf[1] = data->buf[SENSOR_TYPE_GEOMAGNETIC_FIELD].y;
	iSensorBuf[2] = data->buf[SENSOR_TYPE_GEOMAGNETIC_FIELD].z;

	if (!(atomic64_read(&data->aSensorEnable) & (1 << SENSOR_TYPE_GEOMAGNETIC_FIELD)))
		send_instruction(data, REMOVE_SENSOR, SENSOR_TYPE_GEOMAGNETIC_FIELD,
			chTempbuf, 4);

	pr_info("[SSP]: %s - x = %d, y = %d, z = %d\n", __func__,
		iSensorBuf[0], iSensorBuf[1], iSensorBuf[2]);

	return sprintf(buf, "%s,%d,%d,%d\n", (bSuccess ? "OK" : "NG"),
		iSensorBuf[0], iSensorBuf[1], iSensorBuf[2]);
}

static ssize_t magnetic_get_selftest(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	char chTempBuf[12] = { 0,  };
	int iRet = 0;
	s8 id = 0;
	s16 x_diff = 0, y_diff = 0, z_diff = 0;
	s16 diff_max = 0, diff_min = 0;
	s8 result = 0;
	s8 err[7] = {0, };
	struct ssp_data *data = dev_get_drvdata(dev);

	struct ssp_msg *msg = kzalloc(sizeof(*msg), GFP_KERNEL);
	if (msg == NULL) {
		pr_err("[SSP] %s, failed to alloc memory for ssp_msg\n", __func__);
		goto exit;
	}
	msg->cmd = GEOMAGNETIC_FACTORY;
	msg->length = 12;
	msg->options = AP2HUB_READ;
	msg->buffer = chTempBuf;
	msg->free_buffer = 0;

	iRet = ssp_spi_sync(data, msg, 1000);

	if (iRet != SUCCESS) {
		pr_err("[SSP]: %s - Magnetic Selftest Timeout!! %d\n", __func__, iRet);
		goto exit;
	}

	id = (s8)(chTempBuf[0]);
	x_diff = ((s16)(chTempBuf[2] << 8)) + chTempBuf[1];
	y_diff = ((s16)(chTempBuf[4] << 8)) + chTempBuf[3];
	z_diff = ((s16)(chTempBuf[6] << 8)) + chTempBuf[5];
	diff_max = ((s16)(chTempBuf[8] << 8)) + chTempBuf[7];
	diff_min = ((s16)(chTempBuf[10] << 8)) + chTempBuf[9];
	result = chTempBuf[10];
	
	if(id != 0x1)
		err[0] = -1;
	if(x_diff < diff_min || x_diff > diff_max)
		err[1] = -1;
	if(y_diff < diff_min || y_diff > diff_max)
		err[2] = -1;
	if(z_diff < diff_min || z_diff > diff_max)
		err[3] = -1;
	if(result == 0)
		err[4] = -1;

	pr_info("[SSP] %s\n"
		"[SSP] Test1 - err = %d, id = %d \n"
		"[SSP] Test2 - err = %d, x_diff = %d \n"
		"[SSP] Test3 - err = %d, y_diff = %d \n"
		"[SSP] Test4 - err = %d, z_diff = %d \n"
		"[SSP] Test5 - err = %d, result = %d d\n",
		__func__, err[0], id, err[1], x_diff, err[2], y_diff, err[3], z_diff, err[4], result);

exit:
	return sprintf(buf,
			"%d, %d, %d, %d, %d, %d, %d, %d, %d, %d \n",
			err[0], id, err[1], x_diff, err[2], y_diff, err[3], z_diff, err[4], result);
}

static ssize_t hw_offset_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
#if 0
	struct ssp_data *data = dev_get_drvdata(dev);

	mag_open_hwoffset(data);

	pr_info("[SSP] %s: %d %d %d\n", __func__,
		(s8)data->magoffset.x,
		(s8)data->magoffset.y,
		(s8)data->magoffset.z);

	return sprintf(buf, "%d %d %d\n",
		(s8)data->magoffset.x,
		(s8)data->magoffset.y,
		(s8)data->magoffset.z);
#endif
	return sprintf(buf, "%d", 1);
}

static ssize_t matrix_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ssp_data *data = dev_get_drvdata(dev);	
	return sprintf(buf,
			"%d %d %d %d %d %d %d %d %d\n", data->static_matrix[0], data->static_matrix[1], data->static_matrix[2]
			, data->static_matrix[3], data->static_matrix[4], data->static_matrix[5]
			, data->static_matrix[6], data->static_matrix[7], data->static_matrix[8]);
}

static ssize_t matrix_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct ssp_data *data = dev_get_drvdata(dev);	
 	int iRet;
 	int i;
 	s16 val[9]={0,};
	char* token;
	char* str;

	str = (char*)buf;
 
 	for(i=0;i<9;i++)
 	{
		token = strsep(&str, " \n");
		if(token == NULL)
		{
			pr_err("[SSP] %s : too few arguments (9 needed)",__func__);
 			return -EINVAL;
		}

		iRet = kstrtos16(token, 10, &val[i]);
		if (iRet<0) {
 			pr_err("[SSP] %s : kstros16 error %d",__func__,iRet);
 			return iRet;
 		}
 	}		
	
	for(i=0 ;i<9;i++)
		data->static_matrix[i] = val[i];

	pr_info("[SSP] %s : %d %d %d %d %d %d %d %d %d\n",__func__,data->static_matrix[0], data->static_matrix[1], data->static_matrix[2]
		, data->static_matrix[3], data->static_matrix[4], data->static_matrix[5]
		, data->static_matrix[6], data->static_matrix[7], data->static_matrix[8]);
	
	set_static_matrix(data);

	return size;
}


static DEVICE_ATTR(name, S_IRUGO, magnetic_name_show, NULL);
static DEVICE_ATTR(vendor, S_IRUGO, magnetic_vendor_show, NULL);
static DEVICE_ATTR(raw_data, S_IRUGO | S_IWUSR | S_IWGRP,
	raw_data_show, raw_data_store);
static DEVICE_ATTR(adc, S_IRUGO, adc_data_read, NULL);
static DEVICE_ATTR(selftest, S_IRUGO, magnetic_get_selftest, NULL);
static DEVICE_ATTR(hw_offset, S_IRUGO, hw_offset_show, NULL);
static DEVICE_ATTR(matrix, S_IRUGO | S_IWUSR | S_IWGRP, matrix_show, matrix_store);

static struct device_attribute *mag_attrs[] = {
	&dev_attr_name,
	&dev_attr_vendor,
	&dev_attr_adc,
	&dev_attr_raw_data,
	&dev_attr_selftest,
	&dev_attr_hw_offset,
	&dev_attr_matrix,
	NULL,
};

int initialize_magnetic_sensor(struct ssp_data *data)
{
	int ret;

	ret = set_static_matrix(data);
	if (ret < 0)
		pr_err("[SSP]: %s - set_magnetic_static_matrix failed %d\n",
			__func__, ret);

	return ret;
}

void initialize_magnetic_factorytest(struct ssp_data *data)
{
	sensors_register(data->devices[SENSOR_TYPE_GEOMAGNETIC_FIELD], data, mag_attrs,
		"magnetic_sensor");
}

void remove_magnetic_factorytest(struct ssp_data *data)
{
	sensors_unregister(data->devices[SENSOR_TYPE_GEOMAGNETIC_FIELD], mag_attrs);
}
