/*
 *  wacom_i2c_func.c - Wacom G5 Digitizer Controller (I2C bus)
 *
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
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "wacom.h"
#include "wacom_i2c_func.h"
#include "wacom_i2c_firm.h"

#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
#define CONFIG_SAMSUNG_KERNEL_DEBUG_USER
#endif

void forced_release(struct wacom_i2c *wac_i2c)
{
#if defined(CONFIG_SAMSUNG_KERNEL_DEBUG_USER)
	printk(KERN_DEBUG "epen:%s\n", __func__);
#endif
	input_report_abs(wac_i2c->input_dev, ABS_X, 0);
	input_report_abs(wac_i2c->input_dev, ABS_Y, 0);
	input_report_abs(wac_i2c->input_dev, ABS_PRESSURE, 0);
	input_report_abs(wac_i2c->input_dev, ABS_DISTANCE, 0);
	input_report_key(wac_i2c->input_dev, BTN_STYLUS, 0);
	input_report_key(wac_i2c->input_dev, BTN_TOUCH, 0);
	input_report_key(wac_i2c->input_dev, BTN_TOOL_RUBBER, 0);
	input_report_key(wac_i2c->input_dev, BTN_TOOL_PEN, 0);
	input_sync(wac_i2c->input_dev);
#ifdef CONFIG_INPUT_BOOSTER
	input_booster_send_event(BOOSTER_DEVICE_PEN, BOOSTER_MODE_FORCE_OFF);
#endif

	wac_i2c->pen_prox = 0;
	wac_i2c->pen_pressed = 0;
	wac_i2c->side_pressed = 0;
	/*wac_i2c->pen_pdct = PDCT_NOSIGNAL;*/
}

int wacom_i2c_send(struct wacom_i2c *wac_i2c,
			  const char *buf, int count, bool mode)
{
	struct i2c_client *client = mode ?
		wac_i2c->client_boot : wac_i2c->client;

	if (wac_i2c->boot_mode && !mode) {
		printk(KERN_ERR
			"epen:failed to send\n");
		return 0;
	}
	
	return i2c_master_send(client, buf, count);
}

int wacom_i2c_recv(struct wacom_i2c *wac_i2c,
			char *buf, int count, bool mode)
{
	struct i2c_client *client = mode ?
		wac_i2c->client_boot : wac_i2c->client;

	if (wac_i2c->boot_mode && !mode) {
		printk(KERN_ERR
			"epen:failed to send\n");
		return 0;
	}

	return i2c_master_recv(client, buf, count);
}

int wacom_i2c_test(struct wacom_i2c *wac_i2c)
{
	int ret, i;
	char buf, test[10];
	buf = COM_QUERY;

	ret = wacom_i2c_send(wac_i2c, &buf, sizeof(buf), false);
	if (ret > 0)
		printk(KERN_INFO "epen:buf:%d, sent:%d\n", buf, ret);
	else {
		printk(KERN_ERR "epen:Digitizer is not active\n");
		return -1;
	}

	ret = wacom_i2c_recv(wac_i2c, test, sizeof(test), false);
	if (ret >= 0) {
		for (i = 0; i < 8; i++)
			printk(KERN_INFO "epen:%d\n", test[i]);
	} else {
		printk(KERN_ERR "epen:Digitizer does not reply\n");
		return -1;
	}

	return 0;
}

int wacom_checksum(struct wacom_i2c *wac_i2c)
{
	int ret = 0, retry = 10;
	int i = 0;
	u8 buf[5] = {0, };

	buf[0] = COM_CHECKSUM;

	while (retry--) {
		ret = wacom_i2c_send(wac_i2c, &buf[0], 1, false);
		if (ret < 0) {
			printk(KERN_DEBUG
			       "epen:i2c fail, retry, %d\n",
			       __LINE__);
			continue;
		}

		msleep(200);
		ret = wacom_i2c_recv(wac_i2c, buf, 5, false);
		if (ret < 0) {
			printk(KERN_DEBUG
			       "epen:i2c fail, retry, %d\n",
			       __LINE__);
			continue;
		} else if (buf[0] == 0x1f)
			break;
		printk(KERN_DEBUG "epen:checksum retry\n");
	}

	if (ret >= 0) {
		printk(KERN_DEBUG
		       "epen:received checksum %x, %x, %x, %x, %x\n",
		       buf[0], buf[1], buf[2], buf[3], buf[4]);
	}

	for (i = 0; i < 5; ++i) {
		if (buf[i] != fw_chksum[i]) {
			printk(KERN_DEBUG
			       "epen:checksum fail %dth %x %x\n", i,
			       buf[i], fw_chksum[i]);
			break;
		}
	}

	wac_i2c->checksum_result = (5 == i);

	return wac_i2c->checksum_result;
}

int wacom_i2c_query(struct wacom_i2c *wac_i2c)
{
	struct wacom_g5_platform_data *pdata = wac_i2c->pdata;
	struct wacom_features *wac_feature = wac_i2c->wac_feature;
	u8 data[COM_QUERY_BUFFER] = {0, };
	u8 *query = data + COM_QUERY_POS;
	int read_size = COM_QUERY_BUFFER;
	u8 buf = COM_QUERY;
	int ret;
	int i;
	int max_x, max_y, pressure;

	for (i = 0; i < COM_QUERY_RETRY; i++) {
		if (unlikely(pdata->use_query_cmd)) {
			ret = wacom_i2c_send(wac_i2c, &buf, 1, false);
			if (ret < 0) {
				printk(KERN_ERR"epen:I2C send failed(%d)\n", ret);
				msleep(50);
				continue;
			}
			read_size = COM_QUERY_NUM;
			query = data;
			msleep(50);
		}

		ret = wacom_i2c_recv(wac_i2c, data, read_size, false);
		if (ret < 0) {
			printk(KERN_ERR"epen:I2C recv failed(%d)\n", ret);
			continue;
		}

		printk(KERN_INFO "epen:%s: %dth ret of wacom query=%d\n",
		       __func__, i, ret);

		if (read_size != ret) {
			printk(KERN_ERR"epen:read size error %d of %d\n", ret, read_size);
			continue;
		}

		if (0x0f == query[EPEN_REG_HEADER]) {
			wac_feature->fw_version =
				((u16) query[EPEN_REG_FWVER1] << 8) + (u16) query[EPEN_REG_FWVER2];
			break;
		}

		printk(KERN_ERR
				"epen:%X, %X, %X, %X, %X, %X, %X, fw=0x%x\n",
				query[0], query[1], query[2], query[3],
				query[4], query[5], query[6],
				wac_feature->fw_version);
	}

	printk(KERN_NOTICE
		"epen:%X, %X, %X, %X, %X, %X, %X, %X, %X, %X, %X, %X, %X, %X\n", query[0],
		query[1], query[2], query[3], query[4], query[5], query[6],
		query[7], query[8], query[9], query[10], query[11], query[12], query[13]);

	if (i == COM_QUERY_RETRY || ret < 0) {
		printk(KERN_ERR"epen:%s:failed to read query\n", __func__);
		wac_feature->fw_version = 0;
		wac_i2c->query_status = false;
		return ret;
	}
	wac_i2c->query_status = true;

	max_x = ((u16) query[EPEN_REG_X1] << 8) + (u16) query[EPEN_REG_X2];
	max_y = ((u16) query[EPEN_REG_Y1] << 8) + (u16) query[EPEN_REG_Y2];
	pressure = ((u16) query[EPEN_REG_PRESSURE1] << 8) + (u16) query[EPEN_REG_PRESSURE2];

	printk(KERN_NOTICE"epen:q max_x=%d max_y=%d, max_pressure=%d\n",
		max_x, max_y, pressure);
	printk(KERN_NOTICE"epen:p max_x=%d max_y=%d, max_pressure=%d\n",
		pdata->max_x, pdata->max_y, pdata->max_pressure);
	printk(KERN_NOTICE "epen:fw_version=0x%X (d7:0x%X,d8:0x%X)\n",
	       wac_feature->fw_version, query[EPEN_REG_FWVER1], query[EPEN_REG_FWVER2]);
	printk(KERN_NOTICE "epen:mpu %#x, bl %#x, tx %d, ty %d, h %d\n",
		query[EPEN_REG_MPUVER], query[EPEN_REG_BLVER],
		query[EPEN_REG_TILT_X], query[EPEN_REG_TILT_Y],
		query[EPEN_REG_HEIGHT]);

	return ret;
}

int wacom_i2c_modecheck(struct wacom_i2c *wac_i2c)
{
	u8 buf = COM_QUERY;
	int ret;
	int mode = WACOM_I2C_MODE_NORMAL;
	u8 data[COM_QUERY_BUFFER] = {0, };
	int read_size = COM_QUERY_NUM;

	ret = wacom_i2c_send(wac_i2c, &buf, 1, false);
	if (ret < 0) {
		mode = WACOM_I2C_MODE_BOOT;
	}
	else{
		mode = WACOM_I2C_MODE_NORMAL;
	}

	ret = wacom_i2c_recv(wac_i2c, data, read_size, false);
	ret = wacom_i2c_recv(wac_i2c, data, read_size, false);
	printk(KERN_NOTICE "epen:I2C send at usermode(%d) querys(%d)\n",mode, ret);

	return mode;
}

int wacom_i2c_set_sense_mode(struct wacom_i2c *wac_i2c)
{
	int retval;
	char data[4] = {0, 0, 0, 0};

	if (wac_i2c->wcharging_mode == 0)
		data[0] = COM_NORMAL_SENSE_MODE;
	else
		data[0] = COM_LOW_SENSE_MODE;

	retval = wacom_i2c_send(wac_i2c, &data[0], 1, WACOM_NORMAL_MODE);
	if (retval != 1) {
		dev_err(&wac_i2c->client->dev, "%s: failed to send wacom i2c mode, %d\n", __func__, retval);
		return retval;
	}

	msleep(60);

	data[0] = COM_SAMPLERATE_STOP;
	retval = wacom_i2c_send(wac_i2c, &data[0], 1, WACOM_NORMAL_MODE);
	if (retval != 1) {
		dev_err(&wac_i2c->client->dev, "%s: failed to read wacom i2c send1, %d\n", __func__, retval);
		return retval;
	}

	msleep(60);

	data[1] = COM_REQUEST_SENSE_MODE;
	retval = wacom_i2c_send(wac_i2c, &data[1], 1, WACOM_NORMAL_MODE);
	if (retval != 1) {
		dev_err(&wac_i2c->client->dev, "%s: failed to read wacom i2c send2, %d\n", __func__, retval);
		return retval;
	}

	msleep(60);

	retval = wacom_i2c_recv(wac_i2c, &data[2], 2, WACOM_NORMAL_MODE);
	if (retval != 2) {
		dev_err(&wac_i2c->client->dev, "%s: failed to read wacom i2c recv, %d\n", __func__, retval);
		return retval;
	}

	dev_info(&wac_i2c->client->dev, "%s: mode:%X, %X\n", __func__, data[2], data[3]);

	data[0] = COM_SAMPLERATE_STOP;
	retval = wacom_i2c_send(wac_i2c, &data[0], 1, WACOM_NORMAL_MODE);
	if (retval != 1) {
		dev_err(&wac_i2c->client->dev, "%s: failed to read wacom i2c send3, %d\n", __func__, retval);
		return retval;
	}

	msleep(60);

	data[0] = COM_SAMPLERATE_START;
	retval = wacom_i2c_send(wac_i2c, &data[0], 1, WACOM_NORMAL_MODE);
	if (retval != 1) {
		dev_err(&wac_i2c->client->dev, "%s: failed to read wacom i2c send4, %d\n", __func__, retval);
		return retval;
	}

	msleep(60);

	return data[3];
}

#ifdef WACOM_IMPORT_FW_ALGO
#ifdef WACOM_USE_OFFSET_TABLE
void wacom_i2c_coord_offset(u16 *coordX, u16 *coordY)
{
	u16 ix, iy;
	u16 dXx_0, dXy_0, dXx_1, dXy_1;
	int D0, D1, D2, D3, D;

	ix = (u16) (((*coordX)) / CAL_PITCH);
	iy = (u16) (((*coordY)) / CAL_PITCH);

	dXx_0 = *coordX - (ix * CAL_PITCH);
	dXx_1 = CAL_PITCH - dXx_0;

	dXy_0 = *coordY - (iy * CAL_PITCH);
	dXy_1 = CAL_PITCH - dXy_0;

	if (*coordX <= WACOM_MAX_COORD_X) {
		D0 = tableX[user_hand][screen_rotate][ix +
						      (iy * LATTICE_SIZE_X)] *
		    (dXx_1 + dXy_1);
		D1 = tableX[user_hand][screen_rotate][ix + 1 +
						      iy * LATTICE_SIZE_X] *
		    (dXx_0 + dXy_1);
		D2 = tableX[user_hand][screen_rotate][ix +
						      (iy +
						       1) * LATTICE_SIZE_X] *
		    (dXx_1 + dXy_0);
		D3 = tableX[user_hand][screen_rotate][ix + 1 +
						      (iy +
						       1) * LATTICE_SIZE_X] *
		    (dXx_0 + dXy_0);
		D = (D0 + D1 + D2 + D3) / (4 * CAL_PITCH);

		if (((int)*coordX + D) > 0)
			*coordX += D;
		else
			*coordX = 0;
	}

	if (*coordY <= WACOM_MAX_COORD_Y) {
		D0 = tableY[user_hand][screen_rotate][ix +
						      (iy * LATTICE_SIZE_X)] *
		    (dXy_1 + dXx_1);
		D1 = tableY[user_hand][screen_rotate][ix + 1 +
						      iy * LATTICE_SIZE_X] *
		    (dXy_1 + dXx_0);
		D2 = tableY[user_hand][screen_rotate][ix +
						      (iy +
						       1) * LATTICE_SIZE_X] *
		    (dXy_0 + dXx_1);
		D3 = tableY[user_hand][screen_rotate][ix + 1 +
						      (iy +
						       1) * LATTICE_SIZE_X] *
		    (dXy_0 + dXx_0);
		D = (D0 + D1 + D2 + D3) / (4 * CAL_PITCH);

		if (((int)*coordY + D) > 0)
			*coordY += D;
		else
			*coordY = 0;
	}
}
#endif

#ifdef WACOM_USE_AVERAGING
#define STEP 32
void wacom_i2c_coord_average(short *CoordX, short *CoordY,
			     int bFirstLscan, int aveStrength)
{
	unsigned char i;
	unsigned int work;
	unsigned char ave_step = 4, ave_shift = 2;
	static int Sum_X, Sum_Y;
	static int AveBuffX[STEP], AveBuffY[STEP];
	static unsigned char AvePtr;
	static unsigned char bResetted;
#ifdef WACOM_USE_AVE_TRANSITION
	static int tmpBuffX[STEP], tmpBuffY[STEP];
	static unsigned char last_step, last_shift;
	static bool transition;
	static int tras_counter;
#endif
	if (bFirstLscan == 0) {
		bResetted = 0;
#ifdef WACOM_USE_AVE_TRANSITION
		transition = false;
		tras_counter = 0;
		last_step = 4;
		last_shift = 2;
#endif
		return ;
	}
#ifdef WACOM_USE_AVE_TRANSITION
	if (bResetted) {
		if (transition) {
			ave_step = last_step;
			ave_shift = last_shift;
		} else {
			ave_step = 2 << (aveStrength-1);
			ave_shift = aveStrength;
		}

		if (!transition && ave_step != 0 && last_step != 0) {
			if (ave_step > last_step) {
				transition = true;
				tras_counter = ave_step;
				/*printk(KERN_DEBUG
					"epen:Trans %d to %d\n",
					last_step, ave_step);*/

				memcpy(tmpBuffX, AveBuffX,
					sizeof(unsigned int) * last_step);
				memcpy(tmpBuffY, AveBuffY,
					sizeof(unsigned int) * last_step);
				for (i = 0 ; i < last_step; ++i) {
					AveBuffX[i] = tmpBuffX[AvePtr];
					AveBuffY[i] = tmpBuffY[AvePtr];
					if (++AvePtr >= last_step)
						AvePtr = 0;
				}
				for ( ; i < ave_step; ++i) {
					AveBuffX[i] = *CoordX;
					AveBuffY[i] = *CoordY;
					Sum_X += *CoordX;
					Sum_Y += *CoordY;
				}
				AvePtr = 0;

				*CoordX = Sum_X >> ave_shift;
				*CoordY = Sum_Y >> ave_shift;

				bResetted = 1;

				last_step = ave_step;
				last_shift = ave_shift;
				return ;
			} else if (ave_step < last_step) {
				transition = true;
				tras_counter = ave_step;
				/*printk(KERN_DEBUG
					"epen:Trans %d to %d\n",
					last_step, ave_step);*/

				memcpy(tmpBuffX, AveBuffX,
					sizeof(unsigned int) * last_step);
				memcpy(tmpBuffY, AveBuffY,
					sizeof(unsigned int) * last_step);
				Sum_X = 0;
				Sum_Y = 0;
				for (i = 1 ; i <= ave_step; ++i) {
					if (AvePtr == 0)
						AvePtr = last_step - 1;
					else
						--AvePtr;
					AveBuffX[ave_step-i] = tmpBuffX[AvePtr];
					Sum_X = Sum_X + tmpBuffX[AvePtr];

					AveBuffY[ave_step-i] = tmpBuffY[AvePtr];
					Sum_Y = Sum_Y + tmpBuffY[AvePtr];
				}
				AvePtr = 0;
				bResetted = 1;
				*CoordX = Sum_X >> ave_shift;
				*CoordY = Sum_Y >> ave_shift;

				bResetted = 1;

				last_step = ave_step;
				last_shift = ave_shift;
				return ;
			}
		}

		if (!transition && (last_step != ave_step)) {
			last_step = ave_step;
			last_shift = ave_shift;
		}
	}
#endif
	if (bFirstLscan && (bResetted == 0)) {
		AvePtr = 0;
		ave_step = 4;
		ave_shift = 2;
#if defined(WACOM_USE_AVE_TRANSITION)
		tras_counter = ave_step;
#endif
		for (i = 0; i < ave_step; i++) {
			AveBuffX[i] = *CoordX;
			AveBuffY[i] = *CoordY;
		}
		Sum_X = (unsigned int)*CoordX << ave_shift;
		Sum_Y = (unsigned int)*CoordY << ave_shift;
		bResetted = 1;
	} else if (bFirstLscan) {
		Sum_X = Sum_X - AveBuffX[AvePtr] + (*CoordX);
		AveBuffX[AvePtr] = *CoordX;
		work = Sum_X >> ave_shift;
		*CoordX = (unsigned int)work;

		Sum_Y = Sum_Y - AveBuffY[AvePtr] + (*CoordY);
		AveBuffY[AvePtr] = (*CoordY);
		work = Sum_Y >> ave_shift;
		*CoordY = (unsigned int)work;

		if (++AvePtr >= ave_step)
			AvePtr = 0;
	}
#ifdef WACOM_USE_AVE_TRANSITION
	if (transition) {
		--tras_counter;
		if (tras_counter < 0)
			transition = false;
	}
#endif
}
#endif

u8 wacom_i2c_coord_level(u16 gain)
{
	if (gain >= 0 && gain <= 14)
		return 0;
	else if (gain > 14 && gain <= 24)
		return 1;
	else
		return 2;
}

#ifdef WACOM_USE_BOX_FILTER
void boxfilt(short *CoordX, short *CoordY,
			int height, int bFirstLscan)
{
	bool isMoved = false;
	static bool bFirst = true;
	static short lastX_loc, lastY_loc;
	static unsigned char bResetted;
	int threshold = 0;
	int distance = 0;
	static short bounce;

	/*Reset filter*/
	if (bFirstLscan == 0) {
		bResetted = 0;
		return ;
	}

	if (bFirstLscan && (bResetted == 0)) {
		lastX_loc = *CoordX;
		lastY_loc = *CoordY;
		bResetted = 1;
	}

	if (bFirst) {
		lastX_loc = *CoordX;
		lastY_loc = *CoordY;
		bFirst = false;
	}

	/*Start Filtering*/
	threshold = 30;

	/*X*/
	distance = abs(*CoordX - lastX_loc);

	if (distance >= threshold)
		isMoved = true;

	if (isMoved == false) {
		distance = abs(*CoordY - lastY_loc);
		if (distance >= threshold)
			isMoved = true;
	}

	/*Update position*/
	if (isMoved) {
		lastX_loc = *CoordX;
		lastY_loc = *CoordY;
	} else {
		*CoordX = lastX_loc + bounce;
		*CoordY = lastY_loc;
		if (bounce)
			bounce = 0;
		else
			bounce += 5;
	}
}
#endif

#if defined(WACOM_USE_AVE_TRANSITION)
int g_aveLevel_C[] = {2, 2, 4, };
int g_aveLevel_X[] = {3, 3, 4, };
int g_aveLevel_Y[] = {3, 3, 4, };
int g_aveLevel_Trs[] = {3, 4, 4, };
int g_aveLevel_Cor[] = {4, 4, 4, };

void ave_level(short CoordX, short CoordY,
			int height, int *aveStrength)
{
	bool transition = false;
	bool edgeY = false, edgeX = false;
	bool cY = false, cX = false;

	if (CoordY > (WACOM_MAX_COORD_Y - 800))
		cY = true;
	else if (CoordY < 800)
		cY = true;

	if (CoordX > (WACOM_MAX_COORD_X - 800))
		cX = true;
	else if (CoordX < 800)
		cX = true;

	if (cX && cY) {
		*aveStrength = g_aveLevel_Cor[height];
		return ;
	}

	/*Start Filtering*/
	if (CoordX > X_INC_E1)
		edgeX = true;
	else if (CoordX < X_INC_S1)
		edgeX = true;

	/*Right*/
	if (CoordY > Y_INC_E1) {
		/*Transition*/
		if (CoordY < Y_INC_E3)
			transition = true;
		else
			edgeY = true;
	}
	/*Left*/
	else if (CoordY < Y_INC_S1) {
		/*Transition*/
		if (CoordY > Y_INC_S3)
			transition = true;
		else
			edgeY = true;
	}

	if (transition)
		*aveStrength = g_aveLevel_Trs[height];
	else if (edgeX)
		*aveStrength = g_aveLevel_X[height];
	else if (edgeY)
		*aveStrength = g_aveLevel_Y[height];
	else
		*aveStrength = g_aveLevel_C[height];
}
#endif
#endif /*WACOM_IMPORT_FW_ALGO*/

static int keycode[] = {
	KEY_RECENT, KEY_BACK,
};
void wacom_i2c_softkey(struct wacom_i2c *wac_i2c, s16 key, s16 pressed)
{
#ifdef WACOM_USE_SOFTKEY_BLOCK
	if (wac_i2c->block_softkey && pressed) {
		cancel_delayed_work_sync(&wac_i2c->softkey_block_work);
		printk(KERN_DEBUG"epen:block p\n");
		return ;
	} else if (wac_i2c->block_softkey && !pressed) {
		printk(KERN_DEBUG"epen:block r\n");
		wac_i2c->block_softkey = false;
		return ;
	}
#endif
	input_report_key(wac_i2c->input_dev,
		keycode[key], pressed);
	input_sync(wac_i2c->input_dev);

#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
	printk(KERN_DEBUG "epen:keycode:%d pressed:%d\n",
		keycode[key], pressed);
#else
	printk(KERN_DEBUG "epen:pressed:%d\n",
		pressed);
#endif
}

#ifdef LCD_FREQ_SYNC
void wacom_i2c_lcd_freq_check(struct wacom_i2c *wac_i2c, u8 *data)
{
	u32 lcd_freq = 0;

	if (wac_i2c->lcd_freq_wait == false) {
		lcd_freq = ((u16) data[10] << 8) + (u16) data[11];
		wac_i2c->lcd_freq = 2000000000 / (lcd_freq + 1);
		wac_i2c->lcd_freq_wait = true;
		schedule_work(&wac_i2c->lcd_freq_work);
	}
}
#endif

int wacom_i2c_coord(struct wacom_i2c *wac_i2c)
{
	struct wacom_g5_platform_data *pdata = wac_i2c->pdata;
	u8 data[COM_COORD_NUM] = {0, };
	bool prox = false;
	bool rdy = false;
	int ret = 0;
	int stylus;
	s16 x, y, pressure;
	s16 tmp;
	u8 gain = 0;
	s16 softkey, pressed, keycode;
	s8 tilt_x = 0;
	s8 tilt_y = 0;
	s8 retry = 3;

	while (retry--) {
		ret = wacom_i2c_recv(wac_i2c, data, COM_COORD_NUM, false);
		if (ret >= 0)
			break;

		printk(KERN_ERR "epen:%s failed to read i2c.retry %d.L%d\n",
			__func__, retry, __LINE__);
	}
	if (ret < 0) {
		printk(KERN_ERR"epen:i2c err, exit %s\n", __func__);
		return -1;
	}

#if defined(CONFIG_SAMSUNG_KERNEL_DEBUG_USER)
	/*printk(KERN_DEBUG"epen:%x, %x, %x, %x, %x, %x, %x %x %x %x %x %x\n",
		data[0], data[1], data[2], data[3], data[4], data[5], data[6],
		data[7], data[8], data[9], data[10], data[11]);*/
#endif

	rdy = data[0] & 0x80;

#ifdef LCD_FREQ_SYNC
	if (!rdy && !data[1] && !data[2] && !data[3] && !data[4]) {
		if (likely(wac_i2c->use_lcd_freq_sync)) {
			if (unlikely(!wac_i2c->pen_prox)) {
				wacom_i2c_lcd_freq_check(wac_i2c, data);
			}
		}
	}
#endif

	if (rdy) {
		/* checking softkey */
		softkey = !!(data[5] & 0x80);
		if (unlikely(softkey)) {
			if (unlikely(wac_i2c->pen_prox))
				forced_release(wac_i2c);

			pressed = !!(data[5] & 0x40);
			keycode = (data[5] & 0x30) >> 4;

			wacom_i2c_softkey(wac_i2c, keycode, pressed);
			return 0;
		}

		/* prox check */
		if (!wac_i2c->pen_prox) {
			/* check pdct */
			if (unlikely(wac_i2c->pen_pdct == PDCT_NOSIGNAL)) {
				printk(KERN_DEBUG"epen:pdct is not active\n");
				return 0;
			}
#ifdef CONFIG_INPUT_BOOSTER
			input_booster_send_event(BOOSTER_DEVICE_PEN, BOOSTER_MODE_ON);
#endif
			wac_i2c->pen_prox = 1;

			if (data[0] & 0x40)
				wac_i2c->tool = BTN_TOOL_RUBBER;
			else
				wac_i2c->tool = BTN_TOOL_PEN;
#if defined(CONFIG_SAMSUNG_KERNEL_DEBUG_USER)
			printk(KERN_DEBUG"epen:is in(%d)\n", wac_i2c->tool);
#endif
		}

		prox = !!(data[0] & 0x10);
		stylus = !!(data[0] & 0x20);
		x = ((u16) data[1] << 8) + (u16) data[2];
		y = ((u16) data[3] << 8) + (u16) data[4];
		pressure = ((u16) data[5] << 8) + (u16) data[6];
		gain = data[7];
		tilt_x = (s8)data[9];
		tilt_y = -(s8)data[8];

		/* origin */
		x = x - pdata->origin[0];
		y = y - pdata->origin[1];

		/* change axis from wacom to lcd */
		if (pdata->x_invert)
			x = pdata->max_x - x;
		if (pdata->y_invert)
			y = pdata->max_y - y;

		if (pdata->xy_switch) {
			tmp = x;
			x = y;
			y = tmp;
		}

		/* validation check */
		if (unlikely(x < 0 || y < 0 || x > pdata->max_y || y > pdata->max_x)) {
#if defined(CONFIG_SAMSUNG_KERNEL_DEBUG_USER)
			printk(KERN_DEBUG "epen:raw data x=%d, y=%d\n",
				x, y);
#endif
			return 0;
		}

		/* report info */
		input_report_abs(wac_i2c->input_dev, ABS_X, x);
		input_report_abs(wac_i2c->input_dev, ABS_Y, y);
		input_report_abs(wac_i2c->input_dev,
			ABS_PRESSURE, pressure);
		input_report_abs(wac_i2c->input_dev,
			ABS_DISTANCE, gain);
		input_report_abs(wac_i2c->input_dev,
			ABS_TILT_X, tilt_x);
		input_report_abs(wac_i2c->input_dev,
			ABS_TILT_Y, tilt_y);
		input_report_key(wac_i2c->input_dev,
			BTN_STYLUS, stylus);
		input_report_key(wac_i2c->input_dev, BTN_TOUCH, prox);
		input_report_key(wac_i2c->input_dev, wac_i2c->tool, 1);
		input_sync(wac_i2c->input_dev);

		/* log */
		if (prox && !wac_i2c->pen_pressed) {
#if defined(CONFIG_SAMSUNG_KERNEL_DEBUG_USER)
			printk(KERN_DEBUG
				"epen:is pressed(%d,%d,%d)(%d)\n",
				x, y, pressure, wac_i2c->tool);
#else
			printk(KERN_DEBUG "epen:pressed\n");
#endif
		} else if (!prox && wac_i2c->pen_pressed) {
#if defined(CONFIG_SAMSUNG_KERNEL_DEBUG_USER)
			printk(KERN_DEBUG
				"epen:is released(%d,%d,%d)(%d)\n",
				x, y, pressure, wac_i2c->tool);
#else
			printk(KERN_DEBUG "epen:released\n");
#endif
		}
		wac_i2c->pen_pressed = prox;

		/* check side */
		if (stylus && !wac_i2c->side_pressed)
			printk(KERN_DEBUG "epen:side on\n");
		else if (!stylus && wac_i2c->side_pressed)
			printk(KERN_DEBUG "epen:side off\n");

		wac_i2c->side_pressed = stylus;
	} else {
		if (wac_i2c->pen_prox) {
			/* input_report_abs(wac->input_dev,
			   ABS_X, x); */
			/* input_report_abs(wac->input_dev,
			   ABS_Y, y); */

			input_report_abs(wac_i2c->input_dev, ABS_PRESSURE, 0);
			input_report_abs(wac_i2c->input_dev,
				ABS_DISTANCE, 0);
			input_report_key(wac_i2c->input_dev, BTN_STYLUS, 0);
			input_report_key(wac_i2c->input_dev, BTN_TOUCH, 0);
			input_report_key(wac_i2c->input_dev,
				BTN_TOOL_RUBBER, 0);
			input_report_key(wac_i2c->input_dev, BTN_TOOL_PEN, 0);
			input_sync(wac_i2c->input_dev);
#ifdef CONFIG_INPUT_BOOSTER
			input_booster_send_event(BOOSTER_DEVICE_PEN, BOOSTER_MODE_OFF);
#endif
#ifdef WACOM_USE_SOFTKEY_BLOCK
			if (wac_i2c->pen_pressed) {
				cancel_delayed_work_sync(&wac_i2c->softkey_block_work);
				wac_i2c->block_softkey = true;
				schedule_delayed_work(&wac_i2c->softkey_block_work,
								SOFTKEY_BLOCK_DURATION);
			}
#endif
			printk(KERN_DEBUG "epen:is out");
		}
		wac_i2c->pen_prox = 0;
		wac_i2c->pen_pressed = 0;
		wac_i2c->side_pressed = 0;
	}

	return 0;
}
