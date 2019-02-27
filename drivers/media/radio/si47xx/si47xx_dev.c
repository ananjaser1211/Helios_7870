 /* drivers/misc/fm_si47xx/si47xx_dev.c
 *
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
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
 */
#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/i2c/si47xx_common.h>

#include "si47xx_dev.h"
#include "commanddefs.h"
#include "propertydefs.h"

/* power_state */
#define RADIO_ON		1
#define RADIO_POWERDOWN		0

/* seek_state */
#define RADIO_SEEK_ON		1
#define RADIO_SEEK_OFF		0

#define FREQ_87500_kHz		8750
#define FREQ_76000_kHz		7600

#define TUNE_RSSI_THRESHOLD	10
#define TUNE_SNR_THRESHOLD	4
#define TUNE_CNT_THRESHOLD	0x00

#ifdef CONFIG_FM_SI47XX_RDS_INT_ON_ALWAYS
#define RDS_BUFFER_LENGTH	50
#endif

enum {
	eTRUE,
	eFALSE,
} dev_struct_status_t;

static struct si47xx_device_t *si47xx_dev;
static struct si47xx_platform_data *si47xx_pdata;

int si47xx_dev_wait_flag = NO_WAIT;

wait_queue_head_t si47xx_waitq;

#ifdef CONFIG_FM_SI47XX_RDS_INT_ON_ALWAYS
static u16 *rds_block_data_buffer;
static u8 *rds_block_error_buffer;
static u8 rds_buffer_index_read;	/* index number for last read data */
static u8 rds_buffer_index_write;	/* index number for last written data */

int rds_data_available;
int rds_data_lost;
int rds_groups_available_till_now;
int si47xx_rds_flag = NO_WAIT;

struct workqueue_struct *si47xx_wq;
struct work_struct si47xx_work;
#endif

static irqreturn_t si47xx_isr(int irq, void *unused)
{
	pr_info("%s(): FM device called IRQ: %d\n",
		__func__, irq);
#ifdef CONFIG_FM_SI47XX_RDS_INT_ON_ALWAYS
	if ((si47xx_dev_wait_flag == SEEK_WAITING) ||
	    (si47xx_dev_wait_flag == TUNE_WAITING)) {
		pr_debug("FM Seek/Tune Interrupt called IRQ %d\n", irq);
		si47xx_dev_wait_flag = WAIT_OVER;
		wake_up_interruptible(&si47xx_waitq);
	} else if (si47xx_rds_flag == RDS_WAITING) {	/* RDS Interrupt */
		pr_debug("rds_groups_available_till_now b/w Power ON/OFF : %d",
			rds_groups_available_till_now);

		if (!work_pending(&si47xx_work))
			queue_work(si47xx_wq, &si47xx_work);
	}
#else
	if ((si47xx_dev_wait_flag == SEEK_WAITING) ||
	    (si47xx_dev_wait_flag == TUNE_WAITING) ||
	    (si47xx_dev_wait_flag == RDS_WAITING)) {
		si47xx_dev_wait_flag = WAIT_OVER;
		wake_up_interruptible(&si47xx_waitq);
	}
#endif
	return IRQ_HANDLED;
}

static int si47xx_power(int on)
{
	int ret = 0;

	pr_err("%s\n", __func__);
	free_irq(si47xx_pdata->si47xx_irq, NULL);

	if (on) {
		gpio_direction_output(si47xx_dev->pdata->rst_gpio, 0);
		gpio_direction_output(si47xx_dev->pdata->int_gpio, 0);

		usleep_range(5, 10);
		gpio_set_value(si47xx_dev->pdata->rst_gpio, 1);
		usleep_range(10, 15);
		gpio_set_value(si47xx_dev->pdata->int_gpio, 1);
		gpio_direction_input(si47xx_dev->pdata->int_gpio);
	} else {
		gpio_set_value(si47xx_dev->pdata->rst_gpio, 0);
		gpio_direction_input(si47xx_dev->pdata->int_gpio);
	}

	si47xx_pdata->si47xx_irq =
		gpio_to_irq(si47xx_pdata->int_gpio);

	ret = request_irq(si47xx_pdata->si47xx_irq, si47xx_isr,
		IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING, "si47xx", NULL);

	return ret;
}

/*-----------------------------------------------------------------------------
 This command returns the status
-----------------------------------------------------------------------------*/
static u8 si47xx_readStatus(void)
{
	u8 status;
	int ret = 0;
	ret = i2c_master_recv((struct i2c_client *)(si47xx_dev->client),
		&status, 1);

	if (ret < 0) {
		pr_err("%s(): failed %d\n", __func__, ret);

		return ret;
	}

	return status;
}


/*-----------------------------------------------------------------------------
 Command that will wait for CTS before returning
-----------------------------------------------------------------------------*/
static void si47xx_waitForCTS(u8 cmd_type)
{
	u16 i;
	u8 rStatus = 0;
	
	for ( i = 0 ; i < 100 ; i++) {
		rStatus = si47xx_readStatus();
		usleep_range(100, 100); 	
		if ((rStatus & CTS)) {
			if (cmd_type == SET_PROPERTY) {
				usleep_range(100*(100-i), 100*(100-i));
			}
			break;
		}
	}
}

/*-----------------------------------------------------------------------------
 Sends a command to the part and returns the reply bytes
-----------------------------------------------------------------------------*/
static int si47xx_command(u8 cmd_size, u8 *cmd, u8 reply_size, u8 *reply)
{
	int ret = 0;
	ret = i2c_master_send((struct i2c_client *)(si47xx_dev->client),
		cmd, cmd_size);
	if (ret < 0) {
		pr_err("%s si47xx_command failed %d\n", __func__, ret);

		return ret;
	}
	si47xx_waitForCTS(cmd[0]);

	if (reply_size) {
		ret = i2c_master_recv((struct i2c_client *)(si47xx_dev->client),
		reply, reply_size);

		if (ret < 0)
			pr_err("%s si47xx_command failed %d\n", __func__, ret);

		return ret;
	}

	return ret;
}

/*-----------------------------------------------------------------------------
 Set the passed property number to the passed value.

 Inputs:
      propNumber:  The number identifying the property to set
      propValue:   The value of the property.
-----------------------------------------------------------------------------*/
static void si47xx_set_property(u16 propNumber, u16 propValue)
{
	u8 cmd[8];
	int ret = 0;

	cmd[0] = SET_PROPERTY;
	cmd[1] = 0;
	cmd[2] = (u8)(propNumber >> 8);
	cmd[3] = (u8)(propNumber & 0x00FF);
	cmd[4] = (u8)(propValue >> 8);
	cmd[5] = (u8)(propValue & 0x00FF);

	ret = si47xx_command(6, cmd, 0, NULL);

	if (ret < 0)
		pr_err("%s(): failed %d\n", __func__, ret);
}

static int powerup(void)
{
	int ret = 0;
	u8 cmd[8];
	u8 rsp[13];

	si47xx_power(1);

	cmd[0] = POWER_UP;
	cmd[1] = POWER_UP_IN_GPO2OEN | POWER_UP_IN_FUNC_FMRX;
	if (si47xx_pdata->mode == 1)
		cmd[2] = POWER_UP_IN_OPMODE_RX_DIGITAL;
	else
		cmd[2] = POWER_UP_IN_OPMODE_RX_ANALOG;

	ret = si47xx_command(3, cmd, 8, rsp);

	if (ret < 0) {
		pr_err("%s(): failed %d\n", __func__, ret);
	} else {
		/* Si4709/09 datasheet: Table 7 */
		msleep(110);
		si47xx_dev->state.power_state = RADIO_ON;
	}

	return ret;
}

static int powerdown(void)
{
	int ret = 0;
	u8 cmd[8];
	u8 rsp[13];

	if (!(RADIO_POWERDOWN == si47xx_dev->state.power_state)) {
		cmd[0] = POWER_DOWN;
		ret = si47xx_command(1, cmd, 1, rsp);

		if (ret < 0)
			pr_err("%s(): failed %d\n", __func__, ret);
		else
			si47xx_dev->state.power_state = RADIO_POWERDOWN;

		msleep(110);
		si47xx_power(0);
	} else
		pr_debug("Device already Powered-OFF\n");

	return ret;
}

/*-----------------------------------------------------------------------------
 Helper function that sends the GET_INT_STATUS command to the part

 Returns:
   The status byte from the part.
-----------------------------------------------------------------------------*/
static s8 getIntStatus(void)
{
	u8 cmd[8];
	u8 rsp[13];
	int ret = 0;
	cmd[0] = GET_INT_STATUS;
	ret = si47xx_command(1, cmd, 1, rsp);

	if (ret < 0) {
		pr_err("%s(): failed %d\n", __func__, ret);
		return ret;
	}

	return rsp[0];
}

/*-----------------------------------------------------------------------------
 Helper function that sends the FM_SEEK_START command to the part

Inputs:
seekUp: If non-zero seek will increment otherwise decrement
wrap:   If non-zero seek will wrap around band limits when hitting the end
of the band limit.
-----------------------------------------------------------------------------*/
static int fmSeekStart(u8 seekUp, u8 wrap)
{
	u8 cmd[8];
	u8 rsp[13];
	int ret;

	cmd[0] = FM_SEEK_START;
	cmd[1] = 0;
	if (seekUp)
		cmd[1] |= FM_SEEK_START_IN_SEEKUP;
	if (wrap)
		cmd[1] |= FM_SEEK_START_IN_WRAP;

	ret = si47xx_command(2, cmd, 1, rsp);

	return ret;
}

static u16 freq_to_channel(u32 frequency)
{
	u16 channel;

	if (frequency < si47xx_dev->settings.bottom_of_band)
		frequency = si47xx_dev->settings.bottom_of_band;

	channel = (frequency - si47xx_dev->settings.bottom_of_band)
		/ si47xx_dev->settings.channel_spacing;

	return channel;
}

/* Only one thread will be able to call this, since this function call is
   protected by a mutex, so no race conditions can arise */
static void wait(void)
{
	wait_event_interruptible(si47xx_waitq,
		(si47xx_dev_wait_flag == WAIT_OVER) ||
		(si47xx_dev_wait_flag == SEEK_CANCEL));
}

#ifndef CONFIG_FM_SI47XX_RDS_INT_ON_ALWAYS
static void wait_RDS(void)
{
	wait_event_interruptible_timeout(si47xx_waitq,
		(si47xx_dev_wait_flag == WAIT_OVER),
		si47xx_dev->settings.timeout_RDS);
}
#endif

/*-----------------------------------------------------------------------------
 Helper function that sends the FM_TUNE_FREQ command to the part

 Inputs:
 frequency in 10kHz steps
-----------------------------------------------------------------------------*/
static int fmTuneFreq(u16 frequency)
{
	u8 cmd[8];
	u8 rsp[13];
	int ret;

	cmd[0] = FM_TUNE_FREQ;
	cmd[1] = 0;
	cmd[2] = (u8)(frequency >> 8);
	cmd[3] = (u8)(frequency & 0x00FF);
	cmd[4] = (u8)0;
	ret  = si47xx_command(5, cmd, 1, rsp);

	return ret;
}

/*-----------------------------------------------------------------------------
 Helper function that sends the FM_TUNE_STATUS command to the part

 Inputs:
 cancel: If non-zero the current seek will be cancelled.
 intack: If non-zero the interrupt for STCINT will be cleared.

Outputs:  These are global variables and are set by this method
STC:    The seek/tune is complete
BLTF:   The seek reached the band limit or original start frequency
AFCRL:  The AFC is railed if this is non-zero
Valid:  The station is valid if this is non-zero
Freq:   The current frequency
RSSI:   The RSSI level read at tune.
ASNR:   The audio SNR level read at tune.
AntCap: The current level of the tuning capacitor.
-----------------------------------------------------------------------------*/

static int fmTuneStatus(u8 cancel, u8 intack, struct tune_data_t *tune_data)
{
	u8 cmd[8];
	u8 rsp[13];
	int ret;

	cmd[0] = FM_TUNE_STATUS;
	cmd[1] = 0;

	if (cancel)
		cmd[1] |= FM_TUNE_STATUS_IN_CANCEL;
	if (intack)
		cmd[1] |= FM_TUNE_STATUS_IN_INTACK;

	ret = si47xx_command(2, cmd, 8, rsp);

	tune_data->stc = !!(rsp[0] & STCINT);
	tune_data->bltf = !!(rsp[1] & FM_TUNE_STATUS_OUT_BTLF);
	tune_data->afcrl = !!(rsp[1] & FM_TUNE_STATUS_OUT_AFCRL);
	tune_data->valid = !!(rsp[1] & FM_TUNE_STATUS_OUT_VALID);
	tune_data->freq = ((u16)rsp[2] << 8) | (u16)rsp[3];
	tune_data->rssi = rsp[4];
	tune_data->asnr = rsp[5];
	tune_data->antcap = rsp[7];

	 pr_info("%s(): tune_data->freq [%d] tune_data->bltf [%d] tune_data->valid [%d]\n", 
		 __func__, tune_data->freq, tune_data->bltf, tune_data->valid);

	return ret;
}

/* -----------------------------------------------------------------------------
 Helper function that sends the FM_RSQ_STATUS command to the part

 Inputs:
  intack: If non-zero the interrupt for STCINT will be cleared.

  Outputs:
  si47xx_status.Status:  Contains bits about the status returned from the part.
  si47xx_status.RsqInts: Contains bits about the interrupts
  that have fired related to RSQ.
  SMUTE:   The soft mute function is currently enabled
  AFCRL:   The AFC is railed if this is non-zero
  Valid:   The station is valid if this is non-zero
  Pilot:   A pilot tone is currently present
  Blend:   Percentage of blend for stereo. (100 = full stereo)
  RSSI:    The RSSI level read at tune.
  ASNR:    The audio SNR level read at tune.
  FreqOff: The frequency offset in kHz of the current station
  from the tuned frequency.
-----------------------------------------------------------------------------*/
static void fmRsqStatus(u8 intack, struct rsq_data_t *rsq_data)
{
	u8 cmd[8];
	u8 rsp[13];

	cmd[0] = FM_RSQ_STATUS;
	cmd[1] = 0;

	if (intack)
		cmd[1] |= FM_RSQ_STATUS_IN_INTACK;

	si47xx_command(2, cmd, 8, rsp);

	rsq_data->rsqints = rsp[1];
	rsq_data->smute = !!(rsp[2] & FM_RSQ_STATUS_OUT_SMUTE);
	rsq_data->afcrl = !!(rsp[2] & FM_RSQ_STATUS_OUT_AFCRL);
	rsq_data->valid = !!(rsp[2] & FM_RSQ_STATUS_OUT_VALID);
	rsq_data->pilot = !!(rsp[3] & FM_RSQ_STATUS_OUT_PILOT);
	rsq_data->blend = rsp[3] & FM_RSQ_STATUS_OUT_STBLEND;
	rsq_data->rssi = rsp[4];
	rsq_data->snr = rsp[5];
	rsq_data->freqoff = rsp[7];
}

/*-----------------------------------------------------------------------------
 Helper function that sends the FM_RDS_STATUS command to the part

 Inputs:
  intack: If non-zero the interrupt for STCINT will be cleared.
  mtfifo: If non-zero the fifo will be cleared.

Outputs:
Status:      Contains bits about the status returned from the part.
RdsInts:     Contains bits about the interrupts that have fired
related to RDS.
RdsSync:     If non-zero the RDS is currently synced.
GrpLost:     If non-zero some RDS groups were lost.
RdsFifoUsed: The amount of groups currently remaining
in the RDS fifo.
BlockA:      Block A group data from the oldest FIFO entry.
BlockB:      Block B group data from the oldest FIFO entry.
BlockC:      Block C group data from the oldest FIFO entry.
BlockD:      Block D group data from the oldest FIFO entry.
BleA:        Block A corrected error information.
BleB:        Block B corrected error information.
BleC:        Block C corrected error information.
BleD:        Block D corrected error information.
-----------------------------------------------------------------------------*/
static void fmRdsStatus(u8 intack, u8 mtfifo, struct radio_data_t *rds_data,
				u8 *RdsFifoUsed)
{
	u8 cmd[8];
	u8 rsp[13];
	int ret = 0;

	cmd[0] = FM_RDS_STATUS;
	cmd[1] = 0;

	if (intack)
		cmd[1] |= FM_RDS_STATUS_IN_INTACK;
	if (mtfifo)
		cmd[1] |= FM_RDS_STATUS_IN_MTFIFO;

	ret = si47xx_command(2, cmd, 13, rsp);
	if (ret < 0) {
		pr_err("%s(): failed %d\n", __func__, ret);
		return;
	}

	*RdsFifoUsed = rsp[3];
	rds_data->rdsa = ((u16)rsp[4] << 8) | (u16)rsp[5];
	rds_data->rdsb = ((u16)rsp[6] << 8) | (u16)rsp[7];
	rds_data->rdsc = ((u16)rsp[8] << 8) | (u16)rsp[9];
	rds_data->rdsd = ((u16)rsp[10] << 8) | (u16)rsp[11];
	rds_data->blera = (rsp[12] & FM_RDS_STATUS_OUT_BLEA) >>
		FM_RDS_STATUS_OUT_BLEA_SHFT;
	rds_data->blerb = (rsp[12] & FM_RDS_STATUS_OUT_BLEB) >>
		FM_RDS_STATUS_OUT_BLEB_SHFT;
	rds_data->blerc = (rsp[12] & FM_RDS_STATUS_OUT_BLEC) >>
		FM_RDS_STATUS_OUT_BLEC_SHFT;
	rds_data->blerd = (rsp[12] & FM_RDS_STATUS_OUT_BLED) >>
		FM_RDS_STATUS_OUT_BLED_SHFT;
}

static int seek(u32 *frequency, int up, int mode)
{
	int ret = 0;
	struct tune_data_t tune_data;

	si47xx_dev_wait_flag = SEEK_WAITING;
	ret = fmSeekStart(up, mode); /* mode 0 is full scan */
	wait();
	
	if (si47xx_dev_wait_flag == SEEK_CANCEL) {
		ret = fmTuneStatus(1, 1, &tune_data);
		*frequency = 0;
		si47xx_dev_wait_flag = NO_WAIT;

		return ret;
	}
	
	si47xx_dev_wait_flag = NO_WAIT;

	if (!(getIntStatus() & STCINT)) {
		pr_err("%s(): failed\n", __func__);
		fmTuneStatus(1, 1, &tune_data);
		ret = -EPERM;
		return ret;
	}

	ret = fmTuneStatus(0, 1, &tune_data);

	if (tune_data.bltf != 1) {
		*frequency = tune_data.freq;
	} else {
		if (tune_data.valid)
			*frequency = tune_data.freq;
		 else {
			 if (mode == 1)
				 *frequency = tune_data.freq;
			 else
				 *frequency = 0;
		 }
	 }
 
	return ret;
}

static int tune_freq(u32 frequency)
{
	int ret;
	u16 channel;
	struct tune_data_t tune_data;

	mutex_lock(&(si47xx_dev->lock));

	channel = freq_to_channel(frequency);

	si47xx_dev_wait_flag = TUNE_WAITING;
	ret = fmTuneFreq(frequency);
	pr_debug("%s(): frequency=%d wait\n", __func__, frequency);
	wait();
	si47xx_dev_wait_flag = NO_WAIT;
	pr_debug("%s(): frequency=%d\n", __func__, frequency);

	if (!(getIntStatus() & STCINT)) {
		pr_err("%s tune is failed!\n", __func__);
		fmTuneStatus(1, 1, &tune_data);
		mutex_unlock(&(si47xx_dev->lock));
		ret = -EPERM;
		return ret;
	}

	ret = fmTuneStatus(0, 1, &tune_data);

	mutex_unlock(&(si47xx_dev->lock));

	return ret;
}


int si47xx_dev_init(struct si47xx_device_t *si47xx)
{
	int ret = 0;

	si47xx_dev = si47xx;
	i2c_set_clientdata(si47xx->client, si47xx_dev);
	dev_set_drvdata(si47xx->dev, si47xx_dev);
	si47xx_pdata = si47xx->pdata;

	mutex_lock(&si47xx_dev->lock);

	si47xx_dev->state.power_state = RADIO_POWERDOWN;
	si47xx_dev->state.seek_state = RADIO_SEEK_OFF;
	si47xx_dev->valid_client_state = eTRUE;
	si47xx_dev->valid = eFALSE;

	si47xx_pdata->si47xx_irq =
		gpio_to_irq(si47xx_pdata->int_gpio);

	ret = request_irq(si47xx_pdata->si47xx_irq, si47xx_isr,
		IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING, "si47xx", NULL);

#ifdef CONFIG_FM_SI47XX_RDS_INT_ON_ALWAYS
	/*Creating Circular Buffer */
	/*Single RDS_Block_Data buffer size is 4x16 bits */
	rds_block_data_buffer = kzalloc(RDS_BUFFER_LENGTH * 8, GFP_KERNEL);
	if (!rds_block_data_buffer) {
		dev_err(si47xx->dev, "Not sufficient memory for creating rds_block_data_buffer");
		ret = -ENOMEM;
		goto exit;
	}

	/*Single RDS_Block_Error buffer size is 4x8 bits */
	rds_block_error_buffer = kzalloc(RDS_BUFFER_LENGTH * 4, GFP_KERNEL);
	if (!rds_block_error_buffer) {
		dev_err(si47xx->dev, "Not sufficient memory for creating rds_block_error_buffer");
		ret = -ENOMEM;
		kfree(rds_block_data_buffer);
		goto exit;
	}

	/*Initialising read and write indices */
	rds_buffer_index_read = 0;
	rds_buffer_index_write = 0;

	/*Creating work-queue */
	si47xx_wq = create_singlethread_workqueue("si47xx_wq");
	if (!si47xx_wq) {
		dev_err(si47xx->dev, "Not sufficient memory for si47xx_wq, work-queue");
		ret = -ENOMEM;
		kfree(rds_block_error_buffer);
		kfree(rds_block_data_buffer);
		goto exit;
	}

	/*Initialising work_queue */
	INIT_WORK(&si47xx_work, si47xx_work_func);

	rds_data_available = 0;
	rds_data_lost = 0;
	rds_groups_available_till_now = 0;
exit:
#endif
	mutex_unlock(&(si47xx_dev->lock));

	return ret;
}

int si47xx_dev_exit(void)
{
	int ret = 0;

	mutex_lock(&(si47xx_dev->lock));
#ifdef CONFIG_FM_SI47XX_RDS_INT_ON_ALWAYS
	if (si47xx_wq)
		destroy_workqueue(si47xx_wq);

	kfree(rds_block_error_buffer);
	kfree(rds_block_data_buffer);
#endif

	mutex_unlock(&(si47xx_dev->lock));

	return ret;
}

void si47xx_dev_digitalmode(bool onoff)
{
	pr_info ("%s(): onoff %d \n", __func__, onoff);
	mutex_lock(&(si47xx_dev->lock));
	if (si47xx_pdata->mode == 1) {
		if(onoff == 1) {
			si47xx_set_property(DIGITAL_OUTPUT_SAMPLE_RATE,
				0xBB80);
			si47xx_set_property(DIGITAL_OUTPUT_FORMAT,
				0x0000);
		} else {
			si47xx_set_property(DIGITAL_OUTPUT_SAMPLE_RATE,
				0x0000);
			si47xx_set_property(DIGITAL_OUTPUT_FORMAT,
				0x0000);
		}
	}
	mutex_unlock(&(si47xx_dev->lock));
}

int si47xx_dev_powerup(void)
{
	int ret = 0;
	u32 value = 100;


	if (!(RADIO_ON == si47xx_dev->state.power_state)) {
		ret = powerup();
		if (ret < 0) {
			pr_err("%s(): failed %d\n", __func__, ret);
		} else if (si47xx_dev->valid_client_state == eFALSE) {
			pr_err("%s(): si47xx is not initialized\n", __func__);
			ret = -EPERM;
		} else {
			si47xx_set_property(FM_RDS_CONFIG, 1);
			
			si47xx_set_property(GPO_IEN, GPO_IEN_STCIEN_MASK |
				GPO_IEN_STCREP_MASK);
			si47xx_set_property(GPO_IEN, GPO_IEN_STCIEN_MASK |
				GPO_IEN_RDSIEN_MASK | GPO_IEN_STCREP_MASK);
			si47xx_set_property(FM_RDS_INTERRUPT_SOURCE,
				FM_RDS_INTERRUPT_SOURCE_RECV_MASK);
			si47xx_set_property(FM_RDS_CONFIG,
				FM_RDS_CONFIG_RDSEN_MASK |
				(3 << FM_RDS_CONFIG_BLETHA_SHFT) |
				(3 << FM_RDS_CONFIG_BLETHB_SHFT) |
				(3 << FM_RDS_CONFIG_BLETHC_SHFT) |
				(3 << FM_RDS_CONFIG_BLETHD_SHFT));
/*VNVS:18-NOV'09 : Setting DE-Time Constant as 50us(Europe,Japan,Australia)*/
			si47xx_set_property(FM_DEEMPHASIS, FM_DEEMPH_50US);
			/* SYSCONFIG2_BITSET_SEEKTH */
			/* &Si47xx_dev->registers[SYSCONFIG2],2); */
/*VNVS:18-NOV'09 : modified for detecting more stations of good quality*/
			si47xx_set_property(FM_SEEK_TUNE_RSSI_THRESHOLD,
				TUNE_RSSI_THRESHOLD);
			si47xx_set_property(FM_SEEK_BAND_BOTTOM, 8750);
			si47xx_set_property(FM_SEEK_BAND_TOP, 10800);
			si47xx_dev->settings.band = BAND_87500_108000_kHz;
			si47xx_dev->settings.bottom_of_band = FREQ_87500_kHz;
			si47xx_set_property(FM_SEEK_FREQ_SPACING,
				CHAN_SPACING_100_kHz);
			si47xx_dev->settings.channel_spacing =
				CHAN_SPACING_100_kHz;

			/* SYSCONFIG3_BITSET_SKSNR( */
			/* &si47xx_dev->registers[SYSCONFIG3],3); */
/*VNVS:18-NOV'09 : modified for detecting more stations of good quality*/
			
			si47xx_set_property(FM_SEEK_TUNE_SNR_THRESHOLD,
			TUNE_SNR_THRESHOLD);
			si47xx_dev->settings.timeout_RDS =
				msecs_to_jiffies(value);
			si47xx_dev->settings.curr_snr = TUNE_SNR_THRESHOLD;
			si47xx_dev->settings.curr_rssi_th = TUNE_RSSI_THRESHOLD;
			si47xx_dev->valid = eTRUE;

			si47xx_dev_STEREO_SET();
#ifdef CONFIG_FM_SI47XX_RDS_INT_ON_ALWAYS
/*Initialising read and write indices */
			rds_buffer_index_read = 0;
			rds_buffer_index_write = 0;

			rds_data_available = 0;
			rds_data_lost = 0;
			rds_groups_available_till_now = 0;
#endif

		}
	} else
		pr_debug("Device already Powered-ON");

	si47xx_set_property(0xff00, 0);

	/* tune initial frequency to remove tunestatus func err
	 * sometimes occur tunestatus func err when execute tunestatus function
	 * before to complete tune_freq.
	 * so run tune_freq just after to complete booting sequence*/
	ret = tune_freq(si47xx_dev->settings.bottom_of_band);

	pr_info("%s(): end\n", __func__);
	return ret;
}

int si47xx_dev_powerdown(void)
{
	int ret = 0;

	pr_info("%s():\n", __func__);

	/* For avoiding turned off pop noise */
	msleep(500);

	mutex_lock(&(si47xx_dev->lock));

	if (si47xx_dev->valid == eFALSE) {
		pr_err("si47xx did not power up\n");
		ret = -EPERM;
	} else {
		ret = powerdown();
		if (ret < 0)
			pr_err("%s(): failed %d\n", __func__, ret);
	}
	mutex_unlock(&(si47xx_dev->lock));

	return ret;
}

int si47xx_dev_band_set(int band)
{
	int ret = 0;
	u16 prev_band;
	u32 prev_bottom_of_band;

	pr_info("%s(): band=%d\n", __func__, band);

	prev_band = si47xx_dev->settings.band;
	prev_bottom_of_band = si47xx_dev->settings.bottom_of_band;
	if (si47xx_dev->valid == eFALSE) {
		pr_err("si47xx did not power up\n");
		ret = -EPERM;
	} else {
		switch (band) {
		case BAND_87500_108000_kHz:
			si47xx_set_property(FM_SEEK_BAND_BOTTOM, 8750);
			si47xx_set_property(FM_SEEK_BAND_TOP, 10800);
			si47xx_dev->settings.band = BAND_87500_108000_kHz;
			si47xx_dev->settings.bottom_of_band = FREQ_87500_kHz;
			break;
		case BAND_76000_108000_kHz:
			si47xx_set_property(FM_SEEK_BAND_BOTTOM, 7600);
			si47xx_set_property(FM_SEEK_BAND_TOP, 10800);
			si47xx_dev->settings.band = BAND_76000_108000_kHz;
			si47xx_dev->settings.bottom_of_band = FREQ_76000_kHz;
			break;
		case BAND_76000_90000_kHz:
			si47xx_set_property(FM_SEEK_BAND_BOTTOM, 7600);
			si47xx_set_property(FM_SEEK_BAND_TOP, 9000);
			si47xx_dev->settings.band = BAND_76000_90000_kHz;
			si47xx_dev->settings.bottom_of_band = FREQ_76000_kHz;
			break;
		default:
			ret = -EINVAL;
		}
	}

	return ret;
}

int si47xx_dev_ch_spacing_set(int ch_spacing)
{
	int ret = 0;
	u16 prev_ch_spacing = 0;

	pr_info("%s(): ch_spacing=%d\n", __func__, ch_spacing);

	mutex_lock(&(si47xx_dev->lock));
	prev_ch_spacing = si47xx_dev->settings.channel_spacing;
	if (si47xx_dev->valid == eFALSE) {
		pr_err("si47xx did not power up\n");
		ret = -EPERM;
	} else {
		switch (ch_spacing) {
		case CHAN_SPACING_200_kHz:
			si47xx_set_property(FM_SEEK_FREQ_SPACING, 20);
			si47xx_dev->settings.channel_spacing =
							CHAN_SPACING_200_kHz;
			break;

		case CHAN_SPACING_100_kHz:
			si47xx_set_property(FM_SEEK_FREQ_SPACING, 10);
			si47xx_dev->settings.channel_spacing =
							CHAN_SPACING_100_kHz;
			break;

		case CHAN_SPACING_50_kHz:
			si47xx_set_property(FM_SEEK_FREQ_SPACING, 5);
			si47xx_dev->settings.channel_spacing =
							CHAN_SPACING_50_kHz;
			break;

		default:
				ret = -EINVAL;
		}
	}

	mutex_unlock(&(si47xx_dev->lock));

	return ret;
}

int si47xx_dev_chan_select(u32 frequency)
{
	int ret = 0;

	pr_info("%s(): frequency=%d\n", __func__, frequency);

	if (si47xx_dev->valid == eFALSE) {
		pr_err("si47xx did not power up\n");
		ret = -EPERM;
	} else {
		si47xx_dev->state.seek_state = RADIO_SEEK_ON;
		ret = tune_freq(frequency);
		si47xx_dev->state.seek_state = RADIO_SEEK_OFF;
	}

	return ret;
}

int si47xx_dev_chan_get(u32 *frequency)
{
	int ret = 0;
	struct tune_data_t tune_data;

	pr_info("%s():\n", __func__);

	mutex_lock(&(si47xx_dev->lock));

	if (si47xx_dev->valid == eFALSE) {
		pr_err("si47xx did not power up\n");
		ret = -EPERM;
	} else {
		ret = fmTuneStatus(0, 1, &tune_data);
		*frequency = tune_data.freq;
	}
	mutex_unlock(&(si47xx_dev->lock));

	return ret;
}

int si47xx_dev_chan_check_valid(bool *valid)
{
	int ret = 0;
	struct rsq_data_t rsq_data;

	pr_info("%s():\n", __func__);

	mutex_lock(&(si47xx_dev->lock));

	if (si47xx_dev->valid == eFALSE) {
		pr_err("si47xx did not power up\n");
		ret = -EPERM;
	} else {
		fmRsqStatus(0, &rsq_data);
		*valid = rsq_data.valid;
	}
	mutex_unlock(&(si47xx_dev->lock));

	return ret;
}


int si47xx_dev_seek_full(u32 *frequency)
{
	int ret = 0;

	pr_info("%s():\n", __func__);

	mutex_lock(&(si47xx_dev->lock));

	if (si47xx_dev->valid == eFALSE) {
		pr_err("si47xx did not power up\n");
		ret = -EPERM;
	} else {
		si47xx_dev->state.seek_state = RADIO_SEEK_ON;
		ret = seek(frequency, 1, 0);
		si47xx_dev->state.seek_state = RADIO_SEEK_OFF;
	}

	mutex_unlock(&(si47xx_dev->lock));

	return ret;
}

int si47xx_dev_seek_up(u32 *frequency)
{
	int ret = 0;

	pr_info("%s():\n", __func__);

	mutex_lock(&(si47xx_dev->lock));

	if (si47xx_dev->valid == eFALSE) {
		pr_err("si47xx did not power up\n");
		ret = -EPERM;
	} else {
		si47xx_dev->state.seek_state = RADIO_SEEK_ON;
		ret = seek(frequency, 1, 1);
		si47xx_dev->state.seek_state = RADIO_SEEK_OFF;
	}

	mutex_unlock(&(si47xx_dev->lock));

	return ret;
}

int si47xx_dev_seek_down(u32 *frequency)
{
	int ret = 0;

	pr_info("%s():\n", __func__);

	mutex_lock(&(si47xx_dev->lock));

	if (si47xx_dev->valid == eFALSE) {
		pr_err("si47xx did not power up\n");
		ret = -EPERM;
	} else {
		si47xx_dev->state.seek_state = RADIO_SEEK_ON;
		ret = seek(frequency, 0, 1);
		si47xx_dev->state.seek_state = RADIO_SEEK_OFF;
	}

	mutex_unlock(&(si47xx_dev->lock));

	return ret;
}

int si47xx_dev_RSSI_seek_th_set(u8 seek_th)
{
	int ret = 0;

	pr_info("%s(): seek_th=%d\n", __func__, seek_th);

	mutex_lock(&(si47xx_dev->lock));

	if (si47xx_dev->valid == eFALSE) {
		pr_err("si47xx did not power up\n");
		ret = -EPERM;
	} else {
		si47xx_set_property(FM_SEEK_TUNE_RSSI_THRESHOLD, seek_th);
		si47xx_dev->settings.curr_rssi_th = seek_th;
	}
	mutex_unlock(&(si47xx_dev->lock));

	return ret;
}

int si47xx_dev_seek_SNR_th_set(u8 seek_SNR)
{
	int ret = 0;

	pr_info("%s(): seek_SNR=%d\n", __func__, seek_SNR);

	mutex_lock(&(si47xx_dev->lock));

	if (si47xx_dev->valid == eFALSE) {
		pr_err("si47xx did not power up\n");
		ret = -EPERM;
	} else {
		si47xx_set_property(FM_SEEK_TUNE_SNR_THRESHOLD, seek_SNR);
		si47xx_dev->settings.curr_snr = seek_SNR;
	}
	mutex_unlock(&(si47xx_dev->lock));

	return ret;
}

int si47xx_dev_cur_RSSI_get(struct rssi_snr_t *cur_RSSI)
{
	int ret = 0;
	struct rsq_data_t rsq_data;

	pr_info("%s():\n", __func__);

	mutex_lock(&(si47xx_dev->lock));

	if (si47xx_dev->valid == eFALSE) {
		pr_err("si47xx did not power up\n");
		ret = -EPERM;
	} else {
		fmRsqStatus(0, &rsq_data);
		cur_RSSI->curr_rssi = rsq_data.rssi;
		cur_RSSI->curr_rssi_th =
			si47xx_dev->settings.curr_rssi_th;
		cur_RSSI->curr_snr = rsq_data.snr;
	}

	mutex_unlock(&(si47xx_dev->lock));

	return ret;
}

int si47xx_dev_seek_FM_ID_th_set(u8 seek_FM_ID_th)
{
	/* reserved */
	return 0;
}

int si47xx_dev_device_id(struct device_id *dev_id)
{
	/* reserved */
	return 0;
}

/*VNVS:START 13-OCT'09----
* Switch Case statements for calling functions which reads device-id,
* chip-id,power configuration, system configuration2 registers
*/
int si47xx_dev_chip_id(struct chip_id *chp_id)
{
	/* reserved */
	return 0;
}

int si47xx_dev_sys_config2(struct sys_config2 *sys_conf2)
{
	int ret = 0;

	pr_info("%s():\n", __func__);

	mutex_lock(&(si47xx_dev->lock));

	if (si47xx_dev->valid == eFALSE) {
		pr_err("si47xx did not power up\n");
		ret = -EPERM;
	} else {
		sys_conf2->rssi_th = si47xx_dev->settings.curr_rssi_th;
		sys_conf2->fm_band = si47xx_dev->settings.band;
		sys_conf2->fm_chan_spac =
			si47xx_dev->settings.channel_spacing;
		sys_conf2->fm_vol = 0;
	}

	mutex_unlock(&(si47xx_dev->lock));

	return ret;
}

int si47xx_dev_sys_config3(struct sys_config3 *sys_conf3)
{
	int ret = 0;

	pr_info("%s():\n", __func__);

	mutex_lock(&(si47xx_dev->lock));

	if (si47xx_dev->valid == eFALSE) {
		pr_err("si47xx did not power up\n");
		ret = -EPERM;
	} else {
		sys_conf3->smmute = 0;
		sys_conf3->smutea = 0;
		sys_conf3->volext = 0;
		sys_conf3->sksnr = si47xx_dev->settings.curr_snr;
		sys_conf3->skcnt = 0;
	}
	mutex_unlock(&(si47xx_dev->lock));

	return ret;
}

int si47xx_dev_status_rssi(struct status_rssi *status)
{
	int ret = 0;
	struct rsq_data_t rsq_data;

	pr_info("%s():\n", __func__);

	mutex_lock(&(si47xx_dev->lock));

	if (si47xx_dev->valid == eFALSE) {
		pr_err("si47xx did not power up\n");
		mutex_unlock(&(si47xx_dev->lock));
		return  -EPERM;
	}
	fmRsqStatus(0, &rsq_data);

	pr_debug("rssi: %d\n", rsq_data.rssi);

	si47xx_dev->settings.curr_rssi = rsq_data.rssi;
	status->rssi = rsq_data.rssi;

	mutex_unlock(&(si47xx_dev->lock));

	return ret;
}

int si47xx_dev_sys_config2_set(struct sys_config2 *sys_conf2)
{
	int ret = 0;

	pr_info("%s(): rssi_th=%d, fm_band=%d, fm_chan_spac=%d\n",
		__func__,
		sys_conf2->rssi_th,
		sys_conf2->fm_band,
		sys_conf2->fm_chan_spac);

	mutex_lock(&(si47xx_dev->lock));

	if (si47xx_dev->valid == eFALSE) {
		pr_err("si47xx did not power up\n");
		ret = -EPERM;
	} else {
		si47xx_set_property(FM_SEEK_TUNE_RSSI_THRESHOLD,
				sys_conf2->rssi_th);
		si47xx_dev_band_set(sys_conf2->fm_band);
		si47xx_set_property(FM_SEEK_FREQ_SPACING,
			sys_conf2->fm_chan_spac);
		si47xx_dev->settings.curr_rssi_th = sys_conf2->rssi_th;
		si47xx_dev->settings.band = sys_conf2->fm_band;
		si47xx_dev->settings.channel_spacing = sys_conf2->fm_chan_spac;
	}

	mutex_unlock(&(si47xx_dev->lock));

	return ret;
}

int si47xx_dev_sys_config3_set(struct sys_config3 *sys_conf3)
{
	int ret = 0;

	pr_info("%s(): sksnr=%d\n", __func__, sys_conf3->sksnr);

	mutex_lock(&(si47xx_dev->lock));

	if (si47xx_dev->valid == eFALSE) {
		pr_err("si47xx did not power up\n");
		ret = -EPERM;
	} else {
		si47xx_set_property(FM_SEEK_TUNE_SNR_THRESHOLD,
			sys_conf3->sksnr);
		si47xx_dev->settings.curr_snr = sys_conf3->sksnr;
	}

	mutex_unlock(&(si47xx_dev->lock));

	return ret;
}

int si47xx_dev_AFCRL_get(u8 *afc)
{
	int ret = 0;
	struct rsq_data_t rsq_data;

	pr_info("%s():\n", __func__);

	mutex_lock(&(si47xx_dev->lock));

	if (si47xx_dev->valid == eFALSE) {
		pr_err("si47xx did not power up\n");
		ret = -EPERM;
	} else {
		fmRsqStatus(0, &rsq_data);
		*afc = rsq_data.afcrl;
	}

	mutex_unlock(&(si47xx_dev->lock));

	return ret;
}

/*Setting DE-emphasis Time Constant.
* For DE=0,TC=50us(Europe,Japan,Australia)
* and DE=1,TC=75us(USA)
*/
int si47xx_dev_DE_set(u8 de_tc)
{
	int ret = 0;

	pr_info("%s():\n", __func__);

	mutex_lock(&(si47xx_dev->lock));

	if (si47xx_dev->valid == eFALSE) {
		pr_err("si47xx did not power up\n");
		ret = -EPERM;
	} else {
		switch (de_tc) {
		case DE_TIME_CONSTANT_50:
			si47xx_set_property(FM_DEEMPHASIS, FM_DEEMPH_50US);
		break;

		case DE_TIME_CONSTANT_75:
			si47xx_set_property(FM_DEEMPHASIS, FM_DEEMPH_75US);
		break;

		default:
			ret = -EINVAL;
		}

		if (ret < 0)
			pr_err("%s(): failed %d\n", __func__, ret);
	}

	mutex_unlock(&(si47xx_dev->lock));

	return ret;
}

/*Resetting the RDS Data Buffer*/
int si47xx_dev_reset_rds_data(void)
{
	int ret = 0;

	pr_info("%s():\n", __func__);

	mutex_lock(&(si47xx_dev->lock));

	if (si47xx_dev->valid == eFALSE) {
		pr_err("si47xx did not power up\n");
		ret = -EPERM;
	} else {
		rds_buffer_index_write = 0;
		rds_buffer_index_read = 0;
		rds_data_lost = 0;
		rds_data_available = 0;
		memset(rds_block_data_buffer, 0, RDS_BUFFER_LENGTH * 8);
		memset(rds_block_error_buffer, 0, RDS_BUFFER_LENGTH * 4);
	}

	mutex_unlock(&(si47xx_dev->lock));

	return ret;
}

int si47xx_dev_volume_set(u8 volume)
{
	int ret = 0;

	if (volume >= SI47XX_VOLUME_NUM)
		return -EINVAL;

	mutex_lock(&(si47xx_dev->lock));

	if (si47xx_dev->valid == eFALSE) {
		pr_err("Si47XX did not power up\n");
		ret = -EPERM;
	} else {
		pr_info("%s(): vol=%d\n", __func__, volume);

		si47xx_set_property(RX_VOLUME, si47xx_pdata->rx_vol[volume] &
			RX_VOLUME_MASK);
		si47xx_dev->vol_idx = volume;
	}

	mutex_unlock(&(si47xx_dev->lock));

	return ret;
}

int si47xx_dev_volume_get(u8 *volume)
{
	int ret = 0;

	pr_info("%s():\n", __func__);

	mutex_lock(&(si47xx_dev->lock));

	if (si47xx_dev->valid == eFALSE) {
		pr_err("si47xx did not power up\n");
		ret = -EPERM;
	} else
		*volume = si47xx_dev->vol_idx;

	mutex_unlock(&(si47xx_dev->lock));

	return ret;
}

/*
VNVS:START 19-AUG'10 : Adding DSMUTE ON/OFF feature.
The Soft Mute feature is available to attenuate the audio
outputs and minimize audible noise in very weak signal conditions.
 */
int si47xx_dev_DSMUTE_ON(void)
{
	int ret = 0;

	pr_info("%s():\n", __func__);

	mutex_lock(&(si47xx_dev->lock));

	if (si47xx_dev->valid == eFALSE) {
		pr_err("si47xx did not power up\n");
		ret = -EPERM;
	} else {
		si47xx_set_property(FM_SOFT_MUTE_RATE, 64);
		si47xx_set_property(FM_SOFT_MUTE_MAX_ATTENUATION, 0);
		si47xx_set_property(FM_SOFT_MUTE_SNR_THRESHOLD, 4);
	}

	mutex_unlock(&(si47xx_dev->lock));

	return ret;
}

int si47xx_dev_DSMUTE_OFF(void)
{
	int ret = 0;

	pr_info("%s():\n", __func__);

	mutex_lock(&(si47xx_dev->lock));

	if (si47xx_dev->valid == eFALSE) {
		pr_err("si47xx did not power up\n");
		ret = -EPERM;
	} else {
		si47xx_set_property(FM_SOFT_MUTE_RATE, 64);
		si47xx_set_property(FM_SOFT_MUTE_MAX_ATTENUATION, 16);
		si47xx_set_property(FM_SOFT_MUTE_SNR_THRESHOLD, 4);
	}

	mutex_unlock(&(si47xx_dev->lock));

	return ret;
}

/*VNVS:END*/

int si47xx_dev_MUTE_ON(void)
{
	int ret = 0;

	pr_info("%s():\n", __func__);

	mutex_lock(&(si47xx_dev->lock));

	if (si47xx_dev->valid == eFALSE) {
		pr_err("si47xx did not power up\n");
		ret = -EPERM;
	} else {
		si47xx_set_property(RX_HARD_MUTE,
			RX_HARD_MUTE_RMUTE_MASK |
			RX_HARD_MUTE_LMUTE_MASK);
	}

	mutex_unlock(&(si47xx_dev->lock));

	return ret;
}

int si47xx_dev_MUTE_OFF(void)
{
	int ret = 0;

	pr_info("%s():\n", __func__);

	mutex_lock(&(si47xx_dev->lock));

	if (si47xx_dev->valid == eFALSE) {
		pr_err("si47xx did not power up\n");
		ret = -EPERM;
	} else {
		si47xx_set_property(RX_HARD_MUTE, 0);
	}

	mutex_unlock(&(si47xx_dev->lock));

	return ret;
}

int si47xx_dev_MONO_SET(void)
{
	int ret = 0;

	pr_info("%s():\n", __func__);

	mutex_lock(&(si47xx_dev->lock));

	if (si47xx_dev->valid == eFALSE) {
		pr_err("si47xx did not power up\n");
		ret = -EPERM;
	} else {
		si47xx_set_property(FM_BLEND_RSSI_STEREO_THRESHOLD, 49);
		si47xx_set_property(FM_BLEND_RSSI_MONO_THRESHOLD, 30);
		si47xx_set_property(FM_BLEND_SNR_STEREO_THRESHOLD, 0);
		si47xx_set_property(FM_BLEND_SNR_MONO_THRESHOLD, 0);
		si47xx_set_property(FM_BLEND_MULTIPATH_STEREO_THRESHOLD, 100);
		si47xx_set_property(FM_BLEND_MULTIPATH_MONO_THRESHOLD, 100);
	}

	mutex_unlock(&(si47xx_dev->lock));

	return ret;
}

int si47xx_dev_STEREO_SET(void)
{
	int ret = 0;

	pr_info("%s():\n", __func__);

	mutex_lock(&(si47xx_dev->lock));

	if (si47xx_dev->valid == eFALSE) {
		pr_err("si47xx did not power up\n");
		ret = -EPERM;
	} else {
		si47xx_set_property(FM_BLEND_RSSI_STEREO_THRESHOLD, 49);
		si47xx_set_property(FM_BLEND_RSSI_MONO_THRESHOLD, 30);
		si47xx_set_property(FM_BLEND_SNR_STEREO_THRESHOLD, 0);
		si47xx_set_property(FM_BLEND_SNR_MONO_THRESHOLD, 0);
		si47xx_set_property(FM_BLEND_MULTIPATH_STEREO_THRESHOLD, 100);
		si47xx_set_property(FM_BLEND_MULTIPATH_MONO_THRESHOLD, 100);
	}

	mutex_unlock(&(si47xx_dev->lock));

	return ret;
}

int si47xx_dev_RDS_ENABLE(void)
{
	int ret = 0;

	pr_info("%s():\n", __func__);

	mutex_lock(&(si47xx_dev->lock));
	if (si47xx_dev->valid == eFALSE) {
		pr_err("si47xx did not power up\n");
		ret = -EPERM;
	} else {
#ifdef CONFIG_FM_SI47XX_RDS_INT_ON_ALWAYS
		si47xx_set_property(GPO_IEN, GPO_IEN_STCIEN_MASK |
		GPO_IEN_STCREP_MASK | GPO_IEN_RDSIEN_MASK |
		GPO_IEN_RDSREP_MASK);
#endif
		si47xx_set_property(FM_RDS_INTERRUPT_SOURCE,
					FM_RDS_INTERRUPT_SOURCE_RECV_MASK);
		si47xx_set_property(FM_RDS_CONFIG, FM_RDS_CONFIG_RDSEN_MASK |
			(3 << FM_RDS_CONFIG_BLETHA_SHFT) |
			(3 << FM_RDS_CONFIG_BLETHB_SHFT) |
			(3 << FM_RDS_CONFIG_BLETHC_SHFT) |
			(3 << FM_RDS_CONFIG_BLETHD_SHFT));
#ifdef CONFIG_FM_SI47XX_RDS_INT_ON_ALWAYS
		si47xx_rds_flag = RDS_WAITING;
#endif
	}

	mutex_unlock(&(si47xx_dev->lock));

	return ret;
}

int si47xx_dev_RDS_DISABLE(void)
{
	int ret = 0;

	pr_info("%s():\n", __func__);

	mutex_lock(&(si47xx_dev->lock));
	if (si47xx_dev->valid == eFALSE) {
		pr_err("si47xx did not power up\n");
		ret = -EPERM;
	} else {
		si47xx_set_property(FM_RDS_CONFIG, 0);
#ifdef CONFIG_FM_SI47XX_RDS_INT_ON_ALWAYS
		si47xx_rds_flag = NO_WAIT;
#endif
	}

	mutex_unlock(&(si47xx_dev->lock));

	return ret;
}

int si47xx_dev_rstate_get(struct dev_state_t *dev_state)
{
	int ret = 0;

	pr_info("%s():\n", __func__);

	mutex_lock(&(si47xx_dev->lock));

	if (si47xx_dev->valid == eFALSE) {
		pr_err("si47xx did not power up\n");
		ret = -EPERM;
	} else {
		dev_state->power_state = si47xx_dev->state.power_state;
		dev_state->seek_state = si47xx_dev->state.seek_state;
	}

	mutex_unlock(&(si47xx_dev->lock));

	return ret;
}

/* VNVS:START 7-JUNE'10 Function call for work-queue "si47xx_wq" */
#ifdef CONFIG_FM_SI47XX_RDS_INT_ON_ALWAYS
void si47xx_work_func(struct work_struct *work)
{
	struct radio_data_t rds_data;
	int i = 0;
	u8 RdsFifoUsed;
#ifdef RDS_DEBUG
	u8 group_type;
#endif

	mutex_lock(&(si47xx_dev->lock));

	if (si47xx_dev->valid == eFALSE) {
		pr_err("si47xx did not power up\n");
		return;
	}

	if (rds_data_lost > 1) {
		pr_debug("No_of_RDS_groups_Lost till now : %d\n",
			rds_data_lost);
	}
	fmRdsStatus(1, 0, &rds_data, &RdsFifoUsed);
	/* RDSR bit and RDS Block data, so reading the RDS registers */
	do {
		/* Writing into rds_block_data_buffer */
		i = 0;
		rds_block_data_buffer[i++ + 4 * rds_buffer_index_write] =
			rds_data.rdsa;
		rds_block_data_buffer[i++ + 4 * rds_buffer_index_write] =
			rds_data.rdsb;
		rds_block_data_buffer[i++ + 4 * rds_buffer_index_write] =
			rds_data.rdsc;
		rds_block_data_buffer[i++ + 4 * rds_buffer_index_write] =
			rds_data.rdsd;

		/*Writing into rds_block_error_buffer */
		i = 0;

		rds_block_error_buffer[i++ + 4 * rds_buffer_index_write] =
			rds_data.blera;
		rds_block_error_buffer[i++ + 4 * rds_buffer_index_write] =
			rds_data.blerb;
		rds_block_error_buffer[i++ + 4 * rds_buffer_index_write] =
			rds_data.blerc;
		rds_block_error_buffer[i++ + 4 * rds_buffer_index_write] =
			rds_data.blerd;
		fmRdsStatus(1, 0, &rds_data, &RdsFifoUsed);
	} while (RdsFifoUsed != 0);

#ifdef RDS_DEBUG
	if (rds_block_error_buffer
			[0 + 4 * rds_buffer_index_write] < 3) {
		pr_debug("PI Code is %d\n",
				rds_block_data_buffer[0 + 4
				* rds_buffer_index_write]);
	}
	if (rds_block_error_buffer
			[1 + 4 * rds_buffer_index_write] < 2) {
		group_type = rds_block_data_buffer[1 + 4
			* rds_buffer_index_write] >> 11;

		if (group_type & 0x01) {
			pr_debug("PI Code is %d\n",
					rds_block_data_buffer[2 + 4
					* rds_buffer_index_write]);
		}
		if (group_type == GROUP_TYPE_2A
				|| group_type == GROUP_TYPE_2B) {
			if (rds_block_error_buffer
					[2 + 4 * rds_buffer_index_write] < 3) {
				pr_debug("Update RT with RDSC\n");
			} else {
				pr_debug("rds_block_error_buffer of Block C is greater than 3\n");
			}
		}
	}
#endif
	rds_buffer_index_write++;

	if (rds_buffer_index_write >= RDS_BUFFER_LENGTH)
		rds_buffer_index_write = 0;

	dev_dbg(si47xx_dev->dev, "rds_buffer_index_write = %d\n",
		rds_buffer_index_write);
	mutex_unlock(&(si47xx_dev->lock));
}
#endif
/*VNVS:END*/

int si47xx_dev_RDS_data_get(struct radio_data_t *data)
{
	int i, ret = 0;
	struct tune_data_t tune_data;
	struct rsq_data_t rsq_data;

	mutex_lock(&(si47xx_dev->lock));

	if (si47xx_dev->valid == eFALSE) {
		pr_err("si47xx did not power up\n");
		mutex_unlock(&(si47xx_dev->lock));
		return  -EPERM;
	}

#ifdef CONFIG_FM_SI47XX_RDS_INT_ON_ALWAYS
	pr_debug("rds_buffer_index_read = %d\n", rds_buffer_index_read);

	/*If No New RDS Data is available return error */
	if (rds_buffer_index_read == rds_buffer_index_write) {
		pr_debug("No_New_RDS_Data_is_available\n");
		ret = fmTuneStatus(0, 1, &tune_data);
		data->curr_channel = tune_data.freq;
		fmRsqStatus(0, &rsq_data);
		data->curr_rssi = rsq_data.rssi;
		pr_debug("curr_channel: %u, curr_rssi:%u\n",
			data->curr_channel, (u32) data->curr_rssi);
		mutex_unlock(&(si47xx_dev->lock));
		return -EBUSY;
	}

	ret = fmTuneStatus(0, 1, &tune_data);
	data->curr_channel = tune_data.freq;
	fmRsqStatus(0, &rsq_data);
	data->curr_rssi = rsq_data.rssi;
	pr_debug("curr_channel: %u, curr_rssi:%u\n",
		data->curr_channel, (u32) data->curr_rssi);

	/* Reading from rds_block_data_buffer */
	i = 0;
	data->rdsa = rds_block_data_buffer[i++ + 4
		* rds_buffer_index_read];
	data->rdsb = rds_block_data_buffer[i++ + 4
		* rds_buffer_index_read];
	data->rdsc = rds_block_data_buffer[i++ + 4
		* rds_buffer_index_read];
	data->rdsd = rds_block_data_buffer[i++ + 4
		* rds_buffer_index_read];

	/* Reading from rds_block_error_buffer */
	i = 0;
	data->blera = rds_block_error_buffer[i++ + 4
		* rds_buffer_index_read];
	data->blerb = rds_block_error_buffer[i++ + 4
		* rds_buffer_index_read];
	data->blerc = rds_block_error_buffer[i++ + 4
		* rds_buffer_index_read];
	data->blerd = rds_block_error_buffer[i++ + 4
		* rds_buffer_index_read];

	/*Flushing the read data */
	memset(&rds_block_data_buffer[0 + 4 * rds_buffer_index_read],
			0, 8);
	memset(&rds_block_error_buffer[0 + 4 * rds_buffer_index_read],
			0, 4);

	rds_buffer_index_read++;

	if (rds_buffer_index_read >= RDS_BUFFER_LENGTH)
		rds_buffer_index_read = 0;

	pr_debug("rds_buffer_index_read=%d\n",
		rds_buffer_index_read);
#endif

	mutex_unlock(&(si47xx_dev->lock));

	return ret;
}

int si47xx_dev_RDS_timeout_set(u32 time_out)
{
	int ret = 0;
	unsigned long jiffy_count = 0;

	/* convert time_out(in milliseconds) into jiffies */
	jiffy_count = msecs_to_jiffies(time_out);

	pr_info("%s(): jiffy_count(%ld)\n", __func__, jiffy_count);

	mutex_lock(&(si47xx_dev->lock));

	if (si47xx_dev->valid == eFALSE) {
		pr_err("si47xx did not power up\n");
		ret = -EPERM;
	} else {
		si47xx_dev->settings.timeout_RDS = jiffy_count;
	}

	mutex_unlock(&(si47xx_dev->lock));

	return ret;
}
