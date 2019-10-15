/*
 * MELFAS MIP4 Touchkey
 *
 * Copyright (C) 2016 MELFAS Inc.
 *
 * mip4.c : Main functions
 *
 * Version : 2016.03.15
 */

#include "mip4.h"

#if USE_LOW_POWER_MODE
#if USE_WAKELOCK
struct wake_lock lpm_wake_lock;
#endif
#endif

/**
* Reboot chip
*/
void mip4_tk_reboot(struct mip4_tk_info *info)
{
	input_info(true, &info->client->dev, "%s [START]\n", __func__);

	disable_irq_nosync(info->irq);
	info->enabled = false;

	mip4_tk_clear_input(info);

	mip4_tk_power_off(info);
	mip4_tk_power_on(info);
	msleep(100);

#ifdef CONFIG_TOUCHKEY_GRIP
	if (info->sar_enable){
		mip4_tk_set_sar_mode(info, 1);
	}
#endif

	info->enabled = true;
	enable_irq(info->irq);

	input_info(true, &info->client->dev, "%s [DONE]\n", __func__);
}

/*
 * I2C Read
 */
int mip4_tk_i2c_read(struct mip4_tk_info *info, char *write_buf, unsigned int write_len, char *read_buf, unsigned int read_len)
{
	int retry = I2C_RETRY_COUNT;
	int res;

	struct i2c_msg msg[] = {
		{
			.addr = info->client->addr,
			.flags = 0,
			.buf = write_buf,
			.len = write_len,
		}, {
			.addr = info->client->addr,
			.flags = I2C_M_RD,
			.buf = read_buf,
			.len = read_len,
		},
	};

	if (!info->enabled) {
		input_err(true, &info->client->dev,
			"%s [ERROR] device disabled\n", __func__);
		return 1;
	}

	mutex_lock(&info->lock);

	while (retry--) {
		res = i2c_transfer(info->client->adapter, msg, ARRAY_SIZE(msg));

		if (res == ARRAY_SIZE(msg))
			goto DONE;
		else if (res < 0)
			input_err(true, &info->client->dev, "%s [ERROR] i2c_transfer - errno[%d]\n", __func__, res);
		else if (res != ARRAY_SIZE(msg))
			input_err(true, &info->client->dev, "%s [ERROR] i2c_transfer - size[%ld] result[%d]\n", __func__, ARRAY_SIZE(msg), res);
		else
			input_err(true, &info->client->dev, "%s [ERROR] unknown error [%d]\n", __func__, res);
	}

	mutex_unlock(&info->lock);

#if RESET_ON_I2C_ERROR
	mip4_tk_reboot(info);
#endif

	return 1;

DONE:
	mutex_unlock(&info->lock);
	return 0;
}

/*
 * I2C Write
 */
int mip4_tk_i2c_write(struct mip4_tk_info *info, char *write_buf, unsigned int write_len)
{
	int retry = I2C_RETRY_COUNT;
	int res;

	if (!info->enabled) {
		input_err(true, &info->client->dev,
			"%s [ERROR] device disabled\n", __func__);
		return 1;
	}

	mutex_lock(&info->lock);

	while (retry--) {
		res = i2c_master_send(info->client, write_buf, write_len);

		if (res == write_len)
			goto DONE;
		else if (res < 0)
			input_err(true, &info->client->dev, "%s [ERROR] i2c_master_send - errno [%d]\n", __func__, res);
		else if (res != write_len)
			input_err(true, &info->client->dev, "%s [ERROR] length mismatch - write[%d] result[%d]\n", __func__, write_len, res);
		else
			input_err(true, &info->client->dev, "%s [ERROR] unknown error [%d]\n", __func__, res);
	}

	mutex_unlock(&info->lock);

#if RESET_ON_I2C_ERROR
	mip4_tk_reboot(info);
#endif
	return 1;

DONE:
	mutex_unlock(&info->lock);
	return 0;
}

#ifdef CONFIG_TOUCHKEY_GRIP
static void mip4_sar_only_mode(struct mip4_tk_info *info, int on)
{
	u8 cmd;
	u8 wbuf[2];
	u8 rbuf[1];
	int ret;
	int mode_retry = 1;	

	if(info->sar_mode == on){
		input_info(true, &info->client->dev, "%s : skip already %s\n", __func__, (on==1)? "sar only mode":"normal mode");
		return;
	}

	if(on == 1)
		cmd = MIP_CTRL_POWER_LOW;
	else
		cmd = MIP_CTRL_POWER_ACTIVE;

	input_info(true, &info->client->dev, "%s : %s, cmd=%x\n", __func__, (on==1)? "sar only mode":"normal mode", cmd);	

sar_mode:
	ret = mip4_tk_set_power_state(info, cmd);
	if(ret){
		input_err(true, &info->client->dev, "%s fail(%d) \n", __func__, ret);
	}
	msleep(40);

	wbuf[0] = MIP_R0_CTRL;
	wbuf[1] = MIP_R1_CTRL_POWER_STATE;	
	ret = mip4_tk_i2c_read(info, wbuf, 2, rbuf, 1);
	if(ret){
		input_err(true, &info->client->dev, "%s fail(%d)\n", __func__, ret);
	}
	input_info(true, &info->client->dev, "%s read reg = %x\n", __func__,rbuf[0]);

	if( (rbuf[0] != cmd) && (mode_retry == 1) ){
		input_err(true, &info->client->dev, "%s change fail retry\n", __func__);
		mode_retry--;
		goto sar_mode;
	}

	info->sar_mode = rbuf[0];
	
}
#endif

#if defined (CONFIG_MUIC_NOTIFIER) && defined(CONFIG_TOUCHKEY_GRIP)
static int mip4_tk_cpuidle_muic_notification(struct notifier_block *nb,
		unsigned long action, void *data)
{
	struct mip4_tk_info *info = container_of(nb, struct mip4_tk_info, cpuidle_muic_nb);
	muic_attached_dev_t attached_dev = *(muic_attached_dev_t *)data;
	u8 wbuf[3];	
	int ret;

	input_info(true, &info->client->dev, "%s action=%lu, attached_dev=%d\n", __func__, action, attached_dev);

	wbuf[0] = MIP_R0_CTRL;
	wbuf[1] = MIP_R1_CTRL_CHARGER_MODE;	

	switch (attached_dev) {
	case ATTACHED_DEV_OTG_MUIC:
	case ATTACHED_DEV_USB_MUIC:
	case ATTACHED_DEV_TA_MUIC:
	case ATTACHED_DEV_AFC_CHARGER_PREPARE_MUIC:
	case ATTACHED_DEV_AFC_CHARGER_9V_MUIC:
		if (action == MUIC_NOTIFY_CMD_ATTACH) 
		{
			input_info(true, &info->client->dev, "%s : attach\n",__func__);
			wbuf[2] = 1;
			ret = mip4_tk_i2c_write(info, wbuf, 3);
			if (ret < 0)
				input_err(true, &info->client->dev, "%s attach fail(%d)\n", __func__, ret);
		}
		else if (action == MUIC_NOTIFY_CMD_DETACH) {
			input_info(true, &info->client->dev, "%s : detach\n",__func__);
			wbuf[2] = 0;
			ret = mip4_tk_i2c_write(info, wbuf, 3);
			if (ret < 0)
				input_err(true, &info->client->dev, "%s detach fail(%d)\n", __func__, ret);
		}
		break;
	default:
		break;
	}

	wbuf[0] = MIP_R0_CTRL;
	wbuf[1] = MIP_R1_CTRL_REBASELINE_GRIP;
	wbuf[2] = 1;	

	input_info(true, &info->client->dev, "%s rebaseline grip \n", __func__);
	if (mip4_tk_i2c_write(info, wbuf, 3)) {
		input_err(true,&info->client->dev, "%s [ERROR] mip4_tk_i2c_write\n", __func__);
	}	

	return 0;
}

#endif

/*
 * Enable device
 */
int mip4_tk_enable(struct mip4_tk_info *info)
{
	if (info->enabled) {
		input_err(true, &info->client->dev,
			"%s [ERROR] device already enabled\n", __func__);

		goto EXIT;
	}

#if USE_LOW_POWER_MODE
	mip4_tk_set_power_state(info, MIP_CTRL_POWER_ACTIVE);

#if USE_WAKELOCK
	if (wake_lock_active(&lpm_wake_lock)) {
		wake_unlock(&lpm_wake_lock);
		dev_dbg(&info->client->dev, "%s - wake_unlock\n", __func__);
	}
#endif

	info->low_power_mode = false;
	dev_dbg(&info->client->dev, "%s - low power mode : off\n", __func__);
#else
	mip4_tk_power_on(info);
	msleep(100);
#endif

#if 0
	if(info->disable_esd == true){
		/* Disable ESD alert */
		mip4_tk_disable_esd_alert(info);
	}
#endif

	mutex_lock(&info->device);

	if (info->irq_enabled == false) {
		enable_irq(info->irq);
		info->irq_enabled = true;
	}

	info->enabled = true;

	mutex_unlock(&info->device);

EXIT:
	input_info(true, &info->client->dev, MIP_DEV_NAME" - Enabled\n");

	return 0;
}

/*
 * Disable device
 */
int mip4_tk_disable(struct mip4_tk_info *info)
{
	if (!info->enabled) {
		input_err(true, &info->client->dev,
			"%s [ERROR] device already disabled\n", __func__);

		goto EXIT;
	}

	mutex_lock(&info->device);

#if USE_LOW_POWER_MODE
	mip4_tk_set_power_state(info, MIP_CTRL_POWER_LOW);

	info->low_power_mode = true;
	dev_dbg(&info->client->dev, "%s - low power mode : on\n", __func__);

#if USE_WAKELOCK
	if (!wake_lock_active(&lpm_wake_lock)) {
		wake_lock(&lpm_wake_lock);
		dev_dbg(&info->client->dev, "%s - wake_lock\n", __func__);
	}
#endif
#else

	disable_irq(info->irq);
	info->irq_enabled = false;
	info->enabled = false;

	mip4_tk_power_off(info);
#endif /* USE_LOW_POWER_MODE */

	mip4_tk_clear_input(info);
	mutex_unlock(&info->device);

EXIT:
	input_info(true, &info->client->dev, MIP_DEV_NAME" - Disabled\n");

	return 0;
}

#if MIP_USE_INPUT_OPEN_CLOSE
/*
 * Open input device
 */
#ifdef OPEN_CLOSE_WORK
static void mip4_tk_resume_work(struct work_struct *work)
{
	struct mip4_tk_info *info = container_of(work, struct mip4_tk_info,
						resume_work.work);

	input_info(true, &info->client->dev, "%s\n", __func__);

	mip4_tk_enable(info);
}
#endif

static int mip4_tk_input_open(struct input_dev *dev)
{
	struct mip4_tk_info *info = input_get_drvdata(dev);

	if (info->init == false) {
		input_err(true, &info->client->dev, "%s: Touchkey is not probed\n", __func__);
		return 0;
	}

#ifdef CONFIG_TOUCHKEY_GRIP
	input_info(true, &info->client->dev, "%s: sar_enable(%d)\n", __func__, info->sar_enable);
	mip4_sar_only_mode(info, 0);
	
	if (device_may_wakeup(&info->client->dev))
		disable_irq_wake(info->irq );
#else
#ifdef OPEN_CLOSE_WORK
	schedule_delayed_work(&info->resume_work, msecs_to_jiffies(1));
#else
	mip4_tk_enable(info);
#endif
#endif

	return 0;
}

/*
 * Close input device
 */
static void mip4_tk_input_close(struct input_dev *dev)
{
	struct mip4_tk_info *info = input_get_drvdata(dev);

	if (info->init == false) {
		input_err(true, &info->client->dev, "%s: Touchkey is not probed\n", __func__);
		return;
	}

#ifdef CONFIG_TOUCHKEY_GRIP
	input_info(true, &info->client->dev, "%s: sar_enable(%d)\n", __func__, info->sar_enable);
	mip4_sar_only_mode(info, 1);

	if (device_may_wakeup(&info->client->dev))
		enable_irq_wake(info->irq );
	mip4_tk_clear_input(info);
#else
#ifdef OPEN_CLOSE_WORK
	cancel_delayed_work(&info->resume_work);
#endif
	mip4_tk_disable(info);
#endif

	return;
}
#endif

/*
 * Get ready status
 */
int mip4_tk_get_ready_status(struct mip4_tk_info *info)
{
	u8 wbuf[16];
	u8 rbuf[16];
	int ret = 0;

	wbuf[0] = MIP_R0_CTRL;
	wbuf[1] = MIP_R1_CTRL_READY_STATUS;
	if (mip4_tk_i2c_read(info, wbuf, 2, rbuf, 1)) {
		input_err(true, &info->client->dev,
			"%s [ERROR] mip4_tk_i2c_read\n", __func__);
		goto ERROR;
	}
	ret = rbuf[0];

	/* check status */
	if ((ret == MIP_CTRL_STATUS_NONE) || (ret == MIP_CTRL_STATUS_LOG) || (ret == MIP_CTRL_STATUS_READY)) {
		input_info(true, &info->client->dev,
			"%s - status [0x%02X]\n", __func__, ret);
	} else {
		input_err(true, &info->client->dev,
			"%s [ERROR] Unknown status [0x%02X]\n",
			__func__, ret);
		goto ERROR;
	}

	if (ret == MIP_CTRL_STATUS_LOG) {
		/* skip log event */
		wbuf[0] = MIP_R0_LOG;
		wbuf[1] = MIP_R1_LOG_TRIGGER;
		wbuf[2] = 0;
		if (mip4_tk_i2c_write(info, wbuf, 3)) {
			input_err(true, &info->client->dev,
				"%s [ERROR] mip4_tk_i2c_write\n", __func__);
		}
	}

	return ret;

ERROR:
	input_err(true, &info->client->dev, "%s [ERROR]\n", __func__);

	return -1;
}

/*
 * Read chip firmware version
 */
int mip4_tk_get_fw_version(struct mip4_tk_info *info, u8 *ver_buf)
{
	u8 rbuf[8];
	u8 wbuf[2];
	int i;

	wbuf[0] = MIP_R0_INFO;
	wbuf[1] = MIP_R1_INFO_VERSION_BOOT;
	if (mip4_tk_i2c_read(info, wbuf, 2, rbuf, 8))
		goto ERROR;

	for (i = 0; i < MIP_FW_MAX_SECT_NUM; i++) {
		ver_buf[0 + i * 2] = rbuf[1 + i * 2];
		ver_buf[1 + i * 2] = rbuf[0 + i * 2];
	}

	return 0;

ERROR:
	input_err(true, &info->client->dev, "%s [ERROR]\n", __func__);
	return 1;
}

/*
 * Read chip firmware version for u16
 */
int mip4_tk_get_fw_version_u16(struct mip4_tk_info *info, u16 *ver_buf_u16)
{
	u8 rbuf[8];
	int i;

	if (mip4_tk_get_fw_version(info, rbuf))
		goto ERROR;

	for (i = 0; i < MIP_FW_MAX_SECT_NUM; i++)
		ver_buf_u16[i] = (rbuf[0 + i * 2] << 8) | rbuf[1 + i * 2];
	return 0;

ERROR:
	input_err(true, &info->client->dev, "%s [ERROR]\n", __func__);
	return 1;
}

/*
 * Read bin(file) firmware version
 */
int mip4_tk_get_fw_version_from_bin(struct mip4_tk_info *info, u8 *ver_buf)
{
	const struct firmware *fw;
	const char *fw_name = info->pdata->firmware_name;

	request_firmware(&fw, fw_name, &info->client->dev);

	if (!fw) {
		input_err(true, &info->client->dev,"%s [ERROR] request_firmware\n", __func__);
		goto ERROR;
	}

	if (mip4_tk_bin_fw_version(info, fw->data, fw->size, ver_buf)) {
		input_err(true, &info->client->dev,"%s [ERROR] mip4_tk_bin_fw_version\n", __func__);
		release_firmware(fw);
		goto ERROR;
	}

	release_firmware(fw);

	return 0;

ERROR:
	input_err(true, &info->client->dev,"%s [ERROR]\n", __func__);
	return 1;
}

#ifdef CONFIG_TOUCHKEY_GRIP
/*
 * Set sar mode
 */
int mip4_tk_set_sar_mode(struct mip4_tk_info *info, u8 mode)
{
	u8 wbuf[3];

	input_info(true, &info->client->dev, "%s - mode[%02X]\n", __func__, mode);


	wbuf[0] = MIP_R0_CTRL;
	wbuf[1] = MIP_R1_CTRL_ENABLE_GRIP;
	wbuf[2] = mode;
	if (mip4_tk_i2c_write(info, wbuf, 3)) {
		input_err(true, &info->client->dev, "%s [ERROR] mip4_tk_i2c_write\n", __func__);
		goto ERROR;
	}

	return 0;

ERROR:
	input_err(true, &info->client->dev, "%s [ERROR]\n", __func__);
	return 1;
}

/*
 * Get sar mode
 */
int mip4_tk_get_sar_mode(struct mip4_tk_info *info)
{
	u8 wbuf[3];
	u8 rbuf[2];

	wbuf[0] = MIP_R0_CTRL;
	wbuf[1] = MIP_R1_CTRL_ENABLE_GRIP;
	if (mip4_tk_i2c_read(info, wbuf, 2, rbuf, 1)) {
		input_err(true, &info->client->dev, "%s [ERROR] mip4_tk_i2c_read\n", __func__);
		goto ERROR;
	} else {
		input_info(true,&info->client->dev, "%s - addr[0x%02X%02X] value[%d]\n", __func__, wbuf[0], wbuf[1], rbuf[0]);
	}
	return rbuf[0];

ERROR:
	input_err(true, &info->client->dev, "%s [ERROR]\n", __func__);
	return -1;
}
#endif
/*
 * Set power state
 */
int mip4_tk_set_power_state(struct mip4_tk_info *info, u8 mode)
{
	u8 wbuf[3];

	input_dbg(true, &info->client->dev, "%s [START]\n", __func__);

	input_dbg(true, &info->client->dev, "%s - mode[%02X]\n", __func__, mode);

	wbuf[0] = MIP_R0_CTRL;
	wbuf[1] = MIP_R1_CTRL_POWER_STATE;
	wbuf[2] = mode;
	if (mip4_tk_i2c_write(info, wbuf, 3)) {
		input_err(true, &info->client->dev, "%s [ERROR] mip4_tk_i2c_write\n", __func__);
		goto ERROR;
	}

	input_dbg(true, &info->client->dev, "%s [DONE]\n", __func__);
	return 0;

ERROR:
	input_err(true, &info->client->dev, "%s [ERROR]\n", __func__);
	return 1;
}

/*
 * Disable ESD alert
 */
int mip4_tk_disable_esd_alert(struct mip4_tk_info *info)
{
	u8 wbuf[4];
	u8 rbuf[4];

	input_dbg(true, &info->client->dev, "%s [START]\n", __func__);

	wbuf[0] = MIP_R0_CTRL;
	wbuf[1] = MIP_R1_CTRL_DISABLE_ESD_ALERT;
	wbuf[2] = 1;
	if (mip4_tk_i2c_write(info, wbuf, 3)) {
		input_err(true, &info->client->dev, "%s [ERROR] mip4_tk_i2c_write\n", __func__);
		goto ERROR;
	}

	if (mip4_tk_i2c_read(info, wbuf, 2, rbuf, 1)) {
		input_err(true, &info->client->dev, "%s [ERROR] mip4_tk_i2c_read\n", __func__);
		goto ERROR;
	}

	if (rbuf[0] != 1) {
		input_dbg(true, &info->client->dev, "%s [ERROR] failed\n", __func__);
		goto ERROR;
	}

	input_info(true, &info->client->dev, "%s [DONE]\n", __func__);

	return 0;

ERROR:
	input_err(true, &info->client->dev, "%s [ERROR]\n", __func__);
	return 1;
}

/*
 * Alert event handler - ESD
 */
static int mip4_tk_alert_handler_esd(struct mip4_tk_info *info, u8 *rbuf)
{
	u8 frame_cnt = rbuf[1];

	input_info(true, &info->client->dev, "%s - frame_cnt[%d]\n", __func__, frame_cnt);

	if (frame_cnt == 0) {
		/* sensor crack, not ESD */
		info->esd_cnt++;
		input_info(true, &info->client->dev, "%s - esd_cnt[%d]\n", __func__, info->esd_cnt);

		if (info->disable_esd == true) {
			mip4_tk_disable_esd_alert(info);
			info->esd_cnt = 0;
		} else if (info->esd_cnt > ESD_COUNT_FOR_DISABLE) {
			/* Disable ESD alert */
			if (mip4_tk_disable_esd_alert(info)) {
			} else {
				info->disable_esd = true;
				info->esd_cnt = 0;
			}
		} else {
			/* Reset chip */
			mip4_tk_reboot(info);
		}
	} else {
		/* ESD detected */
		/* Reset chip */
		mip4_tk_reboot(info);
		info->esd_cnt = 0;
	}

	input_info(true, &info->client->dev, "%s [DONE]\n", __func__);

	return 0;
}

/*
 * Alert event handler - Input type
 */
static int mip4_tk_alert_handler_inputtype(struct mip4_tk_info *info, u8 *rbuf)
{
	u8 input_type = rbuf[1];

	switch (input_type) {
	case 0:
		input_dbg(true, &info->client->dev, "%s - Input type : Finger\n", __func__);
		break;
	case 1:
		input_dbg(true, &info->client->dev, "%s - Input type : Glove\n", __func__);
		break;
	default:
		input_err(true, &info->client->dev, "%s - Input type : Unknown [%d]\n", __func__, input_type);
		goto ERROR;
		break;
	}

	input_info(true, &info->client->dev, "%s [DONE]\n", __func__);

	return 0;

ERROR:
	input_err(true, &info->client->dev, "%s [ERROR]\n", __func__);
	return 1;
}

/*
 * Interrupt handler
 */
static irqreturn_t mip4_tk_interrupt(int irq, void *dev_id)
{
	struct mip4_tk_info *info = dev_id;
	struct i2c_client *client = info->client;
	u8 wbuf[8];
	u8 rbuf[256];
	unsigned int size = 0;
	u8 category = 0;
	u8 alert_type = 0;

	if (info->init == false) {
		input_err(true, &info->client->dev, "%s: Touchkey is not probed\n", __func__);
		return IRQ_HANDLED;
	}

	/* Read packet info */
	wbuf[0] = MIP_R0_EVENT;
	wbuf[1] = MIP_R1_EVENT_PACKET_INFO;
	if (mip4_tk_i2c_read(info, wbuf, 2, rbuf, 1)) {
		input_err(true, &client->dev,
			"%s [ERROR] Read packet info\n", __func__);
		goto ERROR;
	}

	size = (rbuf[0] & 0x7F);
	category = ((rbuf[0] >> 7) & 0x1);
	input_dbg(true, &client->dev,
			"%s - packet info : size[%d] category[%d]\n",
			__func__, size, category);

	/* Check size */
	if (size <= 0) {
		input_err(true, &client->dev,
			"%s [ERROR] Packet size [%d]\n", __func__, size);
		goto EXIT;
	}

	/* Read packet data */
	wbuf[0] = MIP_R0_EVENT;
	wbuf[1] = MIP_R1_EVENT_PACKET_DATA;
	if (mip4_tk_i2c_read(info, wbuf, 2, rbuf, size)) {
		input_err(true, &client->dev, "%s [ERROR] Read packet data\n", __func__);
		goto ERROR;
	}

	/* Event handler */
	if (category == 0) { /* Touch event*/
		info->esd_cnt = 0;
		mip4_tk_input_event_handler(info, size, rbuf);
	} else {	/* Alert event */
		alert_type = rbuf[0];

		input_info(true, &client->dev,
				"%s - alert type [%d]\n", __func__, alert_type);

		if (alert_type == MIP_ALERT_ESD) {
			/* ESD detection */
			if (mip4_tk_alert_handler_esd(info, rbuf)) {
				goto ERROR;
			}
		} else if (alert_type == MIP_ALERT_INPUT_TYPE) {
			/* Input type changed */
			if (mip4_tk_alert_handler_inputtype(info, rbuf)) {
				goto ERROR;
			}
		} else {
			input_err(true, &client->dev,
				"%s [ERROR] Unknown alert type [%d]\n",
				__func__, alert_type);
			goto ERROR;
		}
	}

EXIT:
	return IRQ_HANDLED;

ERROR:
	if (RESET_ON_EVENT_ERROR) {
		input_info(true, &client->dev, "%s - Reset on error\n", __func__);

		mip4_tk_reboot(info);
	}

	input_err(true, &client->dev, "%s [ERROR]\n", __func__);

	return IRQ_HANDLED;
}

/*
 * Update firmware from kernel built-in binary
 */
int mip4_tk_fw_update_from_kernel(struct mip4_tk_info *info, bool force)
{
	const char *fw_name = info->pdata->firmware_name;
	const struct firmware *fw;
	int retires = 3;
	int ret = fw_err_none;

	input_info(true, &info->client->dev, "%s [START]\n", __func__);

	mutex_lock(&info->device);
	disable_irq(info->irq);

#if defined(CONFIG_KEYBOARD_MELFAS_MHS2041) || defined(CONFIG_KEYBOARD_MELFAS_MHS2041B)
	//Check IC id
	mip4_tk_get_ic_id(info);
#endif

	request_firmware(&fw, fw_name, &info->client->dev);

	if (!fw) {
		input_err(true, &info->client->dev,
				"%s [ERROR] request_firmware\n", __func__);
		goto ERROR;
	}

	/* Update firmware */
	do {
		ret = mip4_tk_flash_fw(info, fw->data, fw->size, force, true);
		if (ret >= fw_err_none) {
			break;
		}
	} while (--retires);

	if (!retires) {
		input_err(true, &info->client->dev,
			"%s [ERROR] mip4_tk_flash_fw failed\n", __func__);
		ret = fw_err_download;
	}

ERROR:
	release_firmware(fw);
	enable_irq(info->irq);
	mutex_unlock(&info->device);

	if (ret < fw_err_none) {
		input_err(true, &info->client->dev, "%s [ERROR]\n", __func__);
	} else {
	/*	mip4_tk_init_config(info); */

		input_info(true, &info->client->dev, "%s [DONE]\n", __func__);
	}
	return ret;
}

/*
 * Update firmware from external storage
 */
int mip4_tk_fw_update_from_storage(struct mip4_tk_info *info, char *path, bool force)
{
	struct file *fp;
	mm_segment_t old_fs;
	size_t fw_size, nread;
	int ret = fw_err_none;

	input_info(true, &info->client->dev, "%s [START]\n", __func__);

	mutex_lock(&info->device);
 	disable_irq(info->irq);

#if defined(CONFIG_KEYBOARD_MELFAS_MHS2041) || defined(CONFIG_KEYBOARD_MELFAS_MHS2041B)
	//Check IC id
	mip4_tk_get_ic_id(info);
#endif

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	fp = filp_open(path, O_RDONLY, S_IRUSR);
	if (IS_ERR(fp)) {
		input_err(true, &info->client->dev,
			"%s [ERROR] file_open - path[%s]\n", __func__, path);
		ret = fw_err_file_open;
		goto ERROR;
	}

 	fw_size = fp->f_path.dentry->d_inode->i_size;
	if (0 < fw_size) {
		/* Read firmware */
		unsigned char *fw_data;
		fw_data = kzalloc(fw_size, GFP_KERNEL);
		nread = vfs_read(fp, (char __user *)fw_data, fw_size, &fp->f_pos);

		if (nread != fw_size) {
			input_err(true, &info->client->dev,
				"%s [ERROR] vfs_read - size[%ld] read[%ld]\n",
				__func__, fw_size, nread);
			ret = fw_err_file_read;
		} else {
			/* Update firmware */
			ret = mip4_tk_flash_fw(info, fw_data, fw_size, force, true);
		}

		kfree(fw_data);
	} else {
		input_err(true, &info->client->dev,
			"%s [ERROR] fw_size [%ld]\n", __func__, fw_size);
		ret = fw_err_file_read;
	}

 	filp_close(fp, current->files);

ERROR:
	set_fs(old_fs);

	enable_irq(info->irq);
	mutex_unlock(&info->device);

	if (ret < fw_err_none) {
		input_err(true, &info->client->dev, "%s [ERROR]\n", __func__);
	} else {
		mip4_tk_init_config(info);

		input_info(true, &info->client->dev, "%s [DONE]\n", __func__);
	}

	return ret;
}

/*
 * Sysfs firmware update
 */
static ssize_t mip4_tk_sys_fw_update(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mip4_tk_info *info = i2c_get_clientdata(client);
	int result = 0;
	u8 data[255];
	int ret = 0;

	memset(info->print_buf, 0, PAGE_SIZE);

	input_info(true, &info->client->dev, "%s [START]\n", __func__);

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
		sprintf(data, "F/W update failed : File open error [%s]\n",
				info->fw_path_ext);
		break;
	case fw_err_file_read:
		sprintf(data, "F/W update failed : File read error\n");
		break;
	default:
		sprintf(data, "F/W update failed.\n");
		break;
	}

	input_info(true, &info->client->dev, "%s [DONE]\n", __func__);

	strcat(info->print_buf, data);
	result = snprintf(buf, PAGE_SIZE, "%s\n", info->print_buf);

	return result;
}
static DEVICE_ATTR(fw_update, S_IRUGO, mip4_tk_sys_fw_update, NULL);

/*
 * Sysfs attr info
 */
static struct attribute *mip4_tk_attrs[] = {
	&dev_attr_fw_update.attr,
	NULL,
};

/*
 * Sysfs attr group info
 */
static const struct attribute_group mip4_tk_attr_group = {
	.attrs = mip4_tk_attrs,
};

/*
 * Initial config
 */
int mip4_tk_init_config(struct mip4_tk_info *info)
{
	u8 wbuf[2];
	u8 rbuf[16];
	int ret = 0;

	/* Product name */
	wbuf[0] = MIP_R0_INFO;
	wbuf[1] = MIP_R1_INFO_PRODUCT_NAME;
	ret = mip4_tk_i2c_read(info, wbuf, 2, rbuf, 16);
	if (ret) {
		input_err(true, &info->client->dev,
			"%s [ERROR] mip4_tk_i2c_read\n", __func__);
		goto ERROR;
	}

	memcpy(info->product_name, rbuf, 16);
	input_info(true, &info->client->dev,
		"%s - product_name[%s]\n", __func__, info->product_name);

	/* Firmware version */
	ret = mip4_tk_get_fw_version(info, rbuf);
	if (ret) {
		input_err(true, &info->client->dev,
			"%s [ERROR] mip4_tk_i2c_read\n", __func__);
		goto ERROR;
	}

	memcpy(info->fw_version, rbuf, 8);
	input_info(true, &info->client->dev,
			"%s - F/W Version : %02X.%02X/%02X.%02X/%02X.%02X/%02X.%02X\n",
			__func__, info->fw_version[0], info->fw_version[1],
			info->fw_version[2], info->fw_version[3],
			info->fw_version[4], info->fw_version[5],
			info->fw_version[6], info->fw_version[7]);

	/* Key */
	wbuf[0] = MIP_R0_INFO;
	wbuf[1] = MIP_R1_INFO_KEY_NUM;
	ret = mip4_tk_i2c_read(info, wbuf, 2, rbuf, 1);
	if (ret) {
		input_err(true, &info->client->dev,
			"%s [ERROR] mip4_tk_i2c_read\n", __func__);
		goto ERROR;
	}

#ifdef CONFIG_TOUCHKEY_GRIP
	info->key_num = rbuf[0] - info->grip_ch;
	if (info->key_num < 0) {
		input_err(true, &info->client->dev,
			"%s [ERROR] key_num[%d] grip_ch_num[%d]\n", __func__, info->key_num, info->grip_ch);
		goto ERROR;
	}
#else
	info->key_num = rbuf[0];
#endif
	info->node_key = rbuf[0];

	if (info->key_num > MAX_KEY_NUM) {
		input_err(true, &info->client->dev,
			"%s [ERROR] MAX_KEY_NUM\n", __func__);
		goto ERROR;
	}

	/* LED */
	wbuf[0] = MIP_R0_LED;
	wbuf[1] = MIP_R1_LED_NUM;
	ret = mip4_tk_i2c_read(info, wbuf, 2, rbuf, 2);
	if (ret) {
		input_err(true, &info->client->dev,
			"%s [ERROR] mip4_tk_i2c_read\n", __func__);
		goto ERROR;
	}

	info->led_num = rbuf[0];
	info->led_max_brightness = rbuf[1];

	if (info->led_num > MAX_LED_NUM) {
		input_err(true, &info->client->dev,
			"%s [ERROR] MAX_LED_NUM\n", __func__);
		goto ERROR;
	}
	
#ifdef CONFIG_TOUCHKEY_GRIP
	input_info(true, &info->client->dev,
			"%s - Key : %d, Grip : %d, LED : %d\n",
			__func__, info->key_num, info->grip_ch, info->led_num);
#else
	input_info(true, &info->client->dev,
			"%s - Key : %d, LED : %d\n",
			__func__, info->key_num, info->led_num);
#endif

	/* Protocol */
	wbuf[0] = MIP_R0_EVENT;
	wbuf[1] = MIP_R1_EVENT_FORMAT;
	ret = mip4_tk_i2c_read(info, wbuf, 2, rbuf, 3);
	if (ret) {
		input_err(true, &info->client->dev,
			"%s [ERROR] mip4_tk_i2c_read\n", __func__);
		goto ERROR;
	}

	info->event_format = (rbuf[0]) | (rbuf[1] << 8);
	info->event_size = rbuf[2];
	if (info->event_size <= 0) {
		input_err(true, &info->client->dev,
			"%s [ERROR] event_size[%d]\n",
			__func__, info->event_size);
		goto ERROR;
	}

	input_info(true, &info->client->dev,
		"%s - event_format[%d] event_size[%d] \n",
		__func__, info->event_format, info->event_size);

	input_info(true, &info->client->dev, "%s [DONE]\n", __func__);

	return 0;

ERROR:
	input_err(true, &info->client->dev, "%s [ERROR]\n", __func__);
	return 1;
}

unsigned int boot_mode;

static int __init get_bootmode(char *arg)
{
	get_option(&arg, &boot_mode);

	return 0;
}
early_param("bootmode", get_bootmode);

/*
 * Initialize driver
 */
static int mip4_tk_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct mip4_tk_info *info;
	struct input_dev *input_dev;
	int ret = 0;

#ifdef CONFIG_BATTERY_SAMSUNG
	if (lpcharge == 1) {
		input_err(true, &client->dev, "%s : Do not load driver due to : lpm %d\n", __func__, lpcharge);
		return -ENODEV;
	}
#endif

	if (boot_mode == 2) {
		pr_err("%s : Do not load driver due to : device entered recovery mode %d\n", __func__, boot_mode);
		return -ENODEV;
	}

	if (!i2c_check_functionality(adapter, I2C_FUNC_I2C)) {
		input_err(true, &client->dev,
				"%s [ERROR] i2c_check_functionality\n",
				__func__);
		return -EIO;
	}

	/* Init info data */
	info = kzalloc(sizeof(struct mip4_tk_info),	GFP_KERNEL);
	if (!info) {
		input_err(true, &client->dev,
				"%s [ERROR] memory allocation for device\n",
				__func__);
		ret = -ENOMEM;
		goto err_alloc;
	}

	input_dev = devm_input_allocate_device(&client->dev);
	if (!input_dev) {
		input_err(true, &client->dev,
				"%s [ERROR] memory allocation for input device\n",
				__func__);
		ret = -ENOMEM;
		goto err_input_alloc;
	}

	info->client = client;
	info->input_dev = input_dev;
	info->irq = -1;
	info->init = false;
	info->power = -1;
	info->irq_enabled = false;
	info->fw_path_ext = kstrdup(FW_PATH_EXTERNAL, GFP_KERNEL); /* need to modify */
	info->key_code_loaded = false;

	/* Get platform data */
#ifdef CONFIG_OF
	if (client->dev.of_node) {
		info->pdata = (struct mip4_tk_platform_data *)devm_kzalloc(
				&client->dev,
				sizeof(struct mip4_tk_platform_data),
				GFP_KERNEL);
		if (!info->pdata) {
			input_err(true, &client->dev,
					"%s [ERROR] memory allocation for pdata\n",
					__func__);
			ret = -ENOMEM;
			goto err_config;
		}

		ret = mip4_tk_parse_devicetree(&client->dev, info);
		if (ret) {
			input_err(true, &client->dev,
					"%s [ERROR] mip4_tk_parse_dt\n",
					__func__);
			ret = -EINVAL;
			goto err_config;
		}
	}
#else
	info->pdata = client->dev.platform_data;
	if (!info->pdata) {
		input_err(true, &client->dev,
			"%s [ERROR] pdata is null\n", __func__);
		ret = -EINVAL;
		goto err_config;
	}
#endif

	/* Init input device */
	info->input_dev->name = "sec_touchkey";
	snprintf(info->phys, sizeof(info->phys), "%s/input0", info->input_dev->name);
	info->input_dev->phys = info->phys;
	info->input_dev->id.bustype = BUS_I2C;
	info->input_dev->dev.parent = &client->dev;

#if MIP_USE_INPUT_OPEN_CLOSE
	info->input_dev->open = mip4_tk_input_open;
	info->input_dev->close = mip4_tk_input_close;
#endif
	set_bit(EV_LED, input_dev->evbit);
	set_bit(LED_MISC, input_dev->ledbit);
	
	/* Set info data */
	input_set_drvdata(input_dev, info);
	i2c_set_clientdata(client, info);

	mutex_init(&info->lock);
	mutex_init(&info->device);

	/* Power on */
	mip4_tk_power_on(info);
	if (!info->boot_on_ldo)
		msleep(100);
	info->enabled = true;

	/* Firmware update */
#if MIP_USE_AUTO_FW_UPDATE
	ret = mip4_tk_fw_update_from_kernel(info, false);
	if (ret < 0) {
		input_err(true, &client->dev,
				"%s [ERROR] mip4_tk_fw_update_from_kernel\n",
				__func__);
		goto err_reg_input_dev;
	}
#endif

	/* Initial config */
	ret = mip4_tk_init_config(info);
	if (ret) {
		input_err(true, &client->dev,
				"%s [ERROR] mip4_tk_init_config\n", __func__);
		goto err_reg_input_dev;
	}

	/* Config input interface */
	mip4_tk_config_input(info);

#ifdef OPEN_CLOSE_WORK
	INIT_DELAYED_WORK(&info->resume_work, mip4_tk_resume_work);
#endif

	/* Register input device */
	ret = input_register_device(input_dev);
	if (ret) {
		input_err(true, &client->dev,
			"%s [ERROR] input_register_device\n", __func__);
		goto err_reg_input_dev;
	}

#if MIP_USE_CALLBACK
	/* Config callback functions */
	mip4_tk_config_callback(info);
#endif

	/* Set interrupt handler */
	info->irq = client->irq;
	ret = request_threaded_irq(client->irq, NULL,
			mip4_tk_interrupt, IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
			MIP_DEV_NAME, info);
	if (ret) {
		input_err(true, &client->dev,
				"%s [ERROR] request_threaded_irq\n",
				__func__);

		goto err_req_irq;
	}

#ifdef CONFIG_TOUCHKEY_GRIP
	wake_lock_init(&info->touchkey_wake_lock, WAKE_LOCK_SUSPEND, "touchkey wake lock");
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
	info->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN +1;
	info->early_suspend.suspend = mip4_tk_early_suspend;
	info->early_suspend.resume = mip4_tk_late_resume;
	register_early_suspend(&info->early_suspend);
#endif

	/* Enable device */
	mip4_tk_enable(info);

#if MIP_USE_DEV
	/* Create dev node (optional) */
	if (mip4_tk_dev_create(info)) {
		input_err(true, &client->dev,
				"%s [ERROR] mip4_tk_dev_create\n", __func__);
		ret = -EAGAIN;

		goto err_dev_create;
	}

	/* Create dev */
	info->class = class_create(THIS_MODULE, MIP_DEV_NAME);
	device_create(info->class, NULL, info->mip4_tk_dev, NULL, MIP_DEV_NAME);
#endif

#if MIP_USE_SYS
	/* Create sysfs for test mode (optional) */
	if (mip4_tk_sysfs_create(info)) {
		input_err(true, &client->dev,
				"%s [ERROR] mip4_tk_sysfs_create\n", __func__);
		ret = -EAGAIN;
		goto err_sysfs_create;
	}
#endif

#if MIP_USE_CMD
	/* Create sysfs for command mode (optional) */
	if (mip4_tk_sysfs_cmd_create(info)) {
		input_err(true, &client->dev,
				"%s [ERROR] mip4_tk_sysfs_cmd_create\n",
				__func__);
		ret = -EAGAIN;
		goto err_sysfs_cmd_create;		
	}
#endif
	/* Create sysfs */
	if (sysfs_create_group(&client->dev.kobj, &mip4_tk_attr_group)) {
		input_err(true, &client->dev,
				"%s [ERROR] sysfs_create_group\n", __func__);
		ret = -EAGAIN;
		goto err_create_attr_group;
	}

	if (sysfs_create_link(&info->key_dev->kobj, &info->input_dev->dev.kobj, "input")) {
		input_err(true, &client->dev,
				"%s [ERROR] sysfs_create_link\n", __func__);
		ret = -EAGAIN;
		goto err_create_link;
	}

#if defined (CONFIG_MUIC_NOTIFIER) && defined(CONFIG_TOUCHKEY_GRIP)
	muic_notifier_register(&info->cpuidle_muic_nb, mip4_tk_cpuidle_muic_notification,
			       MUIC_NOTIFY_DEV_CPUIDLE);
#endif

#ifdef CONFIG_TOUCHKEY_GRIP
	ret = mip4_tk_get_sar_mode(info);
	if( (ret && 0x0F) == 0x01 ){
		mip4_tk_set_sar_mode(info,0);
		info->sar_enable = 0;
	}
	device_init_wakeup(&client->dev, true);
#endif

	info->init = true;

	input_info(true, &client->dev,
		"MELFAS " CHIP_NAME " Touchkey is initialized successfully\n");

	return 0;

//	sysfs_remove_link(NULL, MIP_DEV_NAME);	
err_create_link:	
	sysfs_remove_group(&info->client->dev.kobj, &mip4_tk_attr_group);	
err_create_attr_group:	
#if MIP_USE_CMD
	mip4_tk_sysfs_cmd_remove(info);
err_sysfs_cmd_create:
#endif
#if MIP_USE_SYS
	mip4_tk_sysfs_remove(info);
err_sysfs_create:
#endif	
	device_destroy(info->class, info->mip4_tk_dev);
	class_destroy(info->class);
#if MIP_USE_DEV	
err_dev_create:
#endif
	disable_irq(info->irq);
err_req_irq:
	input_unregister_device(input_dev);
	info->input_dev = NULL;
err_reg_input_dev:
	mutex_destroy(&info->lock);
	mutex_destroy(&info->device);
	mip4_tk_power_off(info);
err_config:
	if (info->input_dev)
		input_free_device(input_dev);
err_input_alloc:
	kfree(info);
err_alloc:
	input_err(true, &client->dev,
		"MELFAS " CHIP_NAME " Touchkey initialization failed\n");
	return ret;
}

/*
 * Remove driver
 */
static int mip4_tk_remove(struct i2c_client *client)
{
	struct mip4_tk_info *info = i2c_get_clientdata(client);

	if (info->irq >= 0) {
		free_irq(info->irq, info);
	}

#if MIP_USE_CMD
	mip4_tk_sysfs_cmd_remove(info);
#endif

#if MIP_USE_SYS
	mip4_tk_sysfs_remove(info);
#endif

	sysfs_remove_group(&info->client->dev.kobj, &mip4_tk_attr_group);
	sysfs_remove_link(NULL, MIP_DEV_NAME);

#if MIP_USE_DEV
	device_destroy(info->class, info->mip4_tk_dev);
	class_destroy(info->class);
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&info->early_suspend);
#endif

	input_unregister_device(info->input_dev);
	info->input_dev = NULL;

	kfree(info);

	return 0;
}

#if defined(CONFIG_PM) || defined(CONFIG_HAS_EARLYSUSPEND)
/*
 * Device suspend event handler
 */
int mip4_tk_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mip4_tk_info *info = i2c_get_clientdata(client);

	input_info(true, &client->dev, "%s [START]\n", __func__);

	mip4_tk_disable(info);

	return 0;
}

/*
 * Device resume event handler
 */
int mip4_tk_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mip4_tk_info *info = i2c_get_clientdata(client);
	int ret = 0;

	input_info(true, &client->dev, "%s [START]\n", __func__);

	mip4_tk_enable(info);

	return ret;
}
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
/*
 * Early suspend handler
 */
void mip4_tk_early_suspend(struct early_suspend *h)
{
	struct mip4_tk_info *info = container_of(h, struct mip4_tk_info, early_suspend);

	input_dbg(true, &info->client->dev, "%s [START]\n", __func__);

	mip4_tk_suspend(&info->client->dev);

	input_dbg(true, &info->client->dev, "%s [DONE]\n", __func__);
}

/*
 * Late resume handler
 */
void mip4_tk_late_resume(struct early_suspend *h)
{
	struct mip4_tk_info *info = container_of(h, struct mip4_tk_info, early_suspend);

	input_dbg(true, &info->client->dev, "%s [START]\n", __func__);

	mip4_tk_resume(&info->client->dev);

	input_dbg(true, &info->client->dev, "%s [DONE]\n", __func__);
}
#endif

void mip4_tk_shutdown(struct i2c_client *client)
{
	struct mip4_tk_info *info = i2c_get_clientdata(client);

	if (info->init == false)
		return;

	input_err(true, &info->client->dev, "%s\n", __func__);
#if defined (CONFIG_MUIC_NOTIFIER) && defined(CONFIG_TOUCHKEY_GRIP)
	muic_notifier_unregister(&info->cpuidle_muic_nb);
#endif
#ifdef OPEN_CLOSE_WORK
	cancel_delayed_work(&info->resume_work);
#endif

	free_irq(info->irq, info);

#if MIP_USE_CMD
	mip4_tk_sysfs_cmd_remove(info);
#endif
	input_unregister_device(info->input_dev);
	info->input_dev = NULL;
	mutex_destroy(&info->lock);
	mutex_destroy(&info->device);
	mip4_tk_power_off(info);
	kfree(info);
}

#if defined(CONFIG_PM)
/*
 * PM info
 */
static const struct dev_pm_ops mip4_tk_pm_ops = {
#if !defined(CONFIG_HAS_EARLYSUSPEND) && !defined(MIP_USE_INPUT_OPEN_CLOSE)
	.suspend = mip4_tk_suspend,
	.resume = mip4_tk_resume,
#endif
};
#endif

#ifdef CONFIG_OF
/*
 * Device tree match table
 */
static const struct of_device_id mip4_tk_match_table[] = {
	{
		.compatible = "melfas," MIP_DEV_NAME,
	},
	{},
};
MODULE_DEVICE_TABLE(of, mip4_tk_match_table);
#else
#define mip4_tk_match_table NULL
#endif

/*
 * I2C Device ID
 */
static const struct i2c_device_id mip4_tk_id[] = {
	{"melfas_"MIP_DEV_NAME, 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, mip4_tk_id);

/*
 * I2C driver info
 */
static struct i2c_driver mip4_tk_driver = {
	.id_table = mip4_tk_id,
	.probe = mip4_tk_probe,
	.remove = mip4_tk_remove,
	.shutdown = &mip4_tk_shutdown,
	.driver = {
		.name = MIP_DEV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = mip4_tk_match_table,
#ifdef CONFIG_PM
		.pm = &mip4_tk_pm_ops,
#endif
	},
};

/*
 * Init driver
 */
static int __init mip4_tk_init(void)
{
	return i2c_add_driver(&mip4_tk_driver);
}

/*
 * Exit driver
 */
static void __exit mip4_tk_exit(void)
{
	i2c_del_driver(&mip4_tk_driver);
}

module_init(mip4_tk_init);
module_exit(mip4_tk_exit);

MODULE_DESCRIPTION("MELFAS MIP4 Touchkey");
MODULE_VERSION("2016.06.30");
MODULE_AUTHOR("Sangwon Jee <jeesw@melfas.com>");
MODULE_LICENSE("GPL");
