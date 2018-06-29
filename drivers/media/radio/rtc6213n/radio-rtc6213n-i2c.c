/*
 * drivers/media/radio/rtc6213n/radio-rtc6213n-i2c.c
 *
 * I2C driver for Richwave RTC6213N FM Tuner
 *
 * Copyright (c) 2013 Richwave Technology Co.Ltd
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/* kernel includes */
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include "radio-rtc6213n.h"

static struct of_device_id rtc6213n_i2c_dt_ids[] = {
	{.compatible = "rtc6213n"},
	{}
};

/* I2C Device ID List */
static const struct i2c_device_id rtc6213n_i2c_id[] = {
    /* Generic Entry */
	{ "rtc6213n", 0 },
	/* Terminating entry */
	{ }
};
MODULE_DEVICE_TABLE(i2c, rtc6213n_i2c_id);


/**************************************************************************
 * Module Parameters
 **************************************************************************/

/* Radio Nr */
static int radio_nr = -1;
module_param(radio_nr, int, 0444);
MODULE_PARM_DESC(radio_nr, "Radio Nr");

/* RDS buffer blocks */
static unsigned int rds_buf = 100;
module_param(rds_buf, uint, 0444);
MODULE_PARM_DESC(rds_buf, "RDS buffer entries: *100*");

/* RDS maximum block errors */
static unsigned short max_rds_errors = 1;
/* 0 means   0  errors requiring correction */
/* 1 means 1-2  errors requiring correction (used by original USBRadio.exe) */
/* 2 means 3-5  errors requiring correction */
/* 3 means   6+ errors or errors in checkword, correction not possible */
module_param(max_rds_errors, ushort, 0644);
MODULE_PARM_DESC(max_rds_errors, "RDS maximum block errors: *1*");

enum rtc6213n_ctrl_id {
	RTC6213N_ID_CSR0_ENABLE,
	RTC6213N_ID_CSR0_DISABLE,
	RTC6213N_ID_DEVICEID,
	RTC6213N_ID_CSR0_DIS_SMUTE,
	RTC6213N_ID_CSR0_DIS_MUTE,
	RTC6213N_ID_CSR0_DEEM,
	RTC6213N_ID_CSR0_BLNDADJUST,
	RTC6213N_ID_CSR0_VOLUME,
	RTC6213N_ID_CSR0_BAND,
	RTC6213N_ID_CSR0_CHSPACE,
	RTC6213N_ID_CSR0_DIS_AGC,
	RTC6213N_ID_CSR0_RDS_EN,
	RTC6213N_ID_SEEK_CANCEL,
	RTC6213N_ID_CSR0_SEEKRSSITH,
	RTC6213N_ID_CSR0_OFSTH,
	RTC6213N_ID_CSR0_QLTTH,
	RTC6213N_ID_RSSI,
	RTC6213N_ID_RDS_RDY,
	RTC6213N_ID_STD,
	RTC6213N_ID_SF,
	RTC6213N_ID_RDS_SYNC,
	RTC6213N_ID_SI,
};

static struct v4l2_ctrl_config rtc6213n_ctrls[] = {
	[RTC6213N_ID_CSR0_ENABLE] = {
		.ops	= &rtc6213n_ctrl_ops,
		.id		= V4L2_CID_PRIVATE_CSR0_ENABLE,
		.type	= V4L2_CTRL_TYPE_BOOLEAN,
		.name	= "CSR0_ENABLE",
		.min	= 0,
		.max	= 1,
		.step	= 1,
	},
	[RTC6213N_ID_CSR0_DISABLE] = {
		.ops	= &rtc6213n_ctrl_ops,
		.id		= V4L2_CID_PRIVATE_CSR0_DISABLE,
		.type	= V4L2_CTRL_TYPE_BOOLEAN,
		.name	= "CSR0_DISABLE",
		.min	= 0,
		.max	= 1,
		.step	= 1,
	},
	[RTC6213N_ID_DEVICEID] = {
		.ops	= &rtc6213n_ctrl_ops,
		.id		= V4L2_CID_PRIVATE_DEVICEID,
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.name	= "DEVICEID",
		.min	= 0,
		.max	= 255,
		.step	= 1,
	},
	[RTC6213N_ID_CSR0_DIS_SMUTE] = {
		.ops	= &rtc6213n_ctrl_ops,
		.id		= V4L2_CID_PRIVATE_CSR0_DIS_SMUTE,
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.name	= "CSR0_DIS_SMUTE",
		.min    = 0,
		.max    = 1,
		.step   = 1,
	},
	[RTC6213N_ID_CSR0_DIS_MUTE] = {
		.ops	= &rtc6213n_ctrl_ops,
		.id		= V4L2_CID_PRIVATE_CSR0_DIS_MUTE,
		.type	= V4L2_CTRL_TYPE_BOOLEAN,
		.name	= "CSR0_DIS_MUTE",
		.min    = 0,
		.max    = 1,
		.step   = 1,
	},
	[RTC6213N_ID_CSR0_DEEM] = {
		.ops	= &rtc6213n_ctrl_ops,
		.id		= V4L2_CID_PRIVATE_CSR0_DEEM,
		.type	= V4L2_CTRL_TYPE_BOOLEAN,
		.name	= "CSR0_DEEM",
		.min	= 0,
		.max	= 1,
		.step	= 1,
	},
	[RTC6213N_ID_CSR0_BLNDADJUST] = {
		.ops	= &rtc6213n_ctrl_ops,
		.id		= V4L2_CID_PRIVATE_CSR0_BLNDADJUST,
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.name	= "CSR0_BLNDADJUST",
		.min	= 0,
		.max	= 15,
		.step	= 1,
	},
	[RTC6213N_ID_CSR0_VOLUME] = {
		.ops	= &rtc6213n_ctrl_ops,
		.id		= V4L2_CID_PRIVATE_CSR0_VOLUME,
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.name	= "CSR0_VOLUME",
		.min    = 0,
		.max    = 15,
		.step   = 1,
	},
	[RTC6213N_ID_CSR0_BAND] = {
		.ops	= &rtc6213n_ctrl_ops,
		.id		= V4L2_CID_PRIVATE_CSR0_BAND,
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.name	= "CSR0_BAND",
		.min	= 0,
		.max	= 3,
		.step	= 1,
		},
	[RTC6213N_ID_CSR0_CHSPACE] = {
		.ops	= &rtc6213n_ctrl_ops,
		.id		= V4L2_CID_PRIVATE_CSR0_CHSPACE,
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.name	= "CSR0_CHSPACE",
		.min	= 0,
		.max	= 3,
		.step	= 1,
		},
	[RTC6213N_ID_CSR0_DIS_AGC] = {
		.ops	= &rtc6213n_ctrl_ops,
		.id		= V4L2_CID_PRIVATE_CSR0_DIS_AGC,
		.type	= V4L2_CTRL_TYPE_BOOLEAN,
		.name	= "CSR0_DIS_AGC",
		.min	= 0,
		.max	= 1,
		.step	= 1,
	},
	[RTC6213N_ID_CSR0_RDS_EN] = {
		.ops	= &rtc6213n_ctrl_ops,
		.id		= V4L2_CID_PRIVATE_CSR0_RDS_EN,
		.type	= V4L2_CTRL_TYPE_BOOLEAN,
		.name	= "CSR0_RDS_EN",
		.min	= 0,
		.max	= 1,
		.step	= 1,
	},
	[RTC6213N_ID_SEEK_CANCEL] = {
		.ops	= &rtc6213n_ctrl_ops,
		.id		= V4L2_CID_PRIVATE_SEEK_CANCEL,
		.type	= V4L2_CTRL_TYPE_BOOLEAN,
		.name	= "SEEK_CANCEL",
		.min	= 0,
		.max	= 1,
		.step	= 1,
	},
	[RTC6213N_ID_CSR0_SEEKRSSITH] = {
		.ops	= &rtc6213n_ctrl_ops,
		.id		= V4L2_CID_PRIVATE_CSR0_SEEKRSSITH,
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.name	= "CSR0_SEEKRSSITH",
		.min	= 0,
		.max	= 255,
		.step	= 1,
	},
	[RTC6213N_ID_CSR0_OFSTH] = {
		.ops	= &rtc6213n_ctrl_ops,
		.id		= V4L2_CID_PRIVATE_CSR0_OFSTH,
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.name	= "CSR0_OFSTH",
		.def    = 64,
		.min	= 0,
		.max	= 255,
		.step	= 1,
	},
	[RTC6213N_ID_CSR0_QLTTH] = {
		.ops	= &rtc6213n_ctrl_ops,
		.id		= V4L2_CID_PRIVATE_CSR0_QLTTH,
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.name	= "CSR0_QLTTH",
		.def    = 80,
		.min	= 0,
		.max	= 255,
		.step	= 1,
	},
	[RTC6213N_ID_RSSI] = {
		.ops	= &rtc6213n_ctrl_ops,
		.id		= V4L2_CID_PRIVATE_RSSI,
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.name	= "CSR0_SEEKRSSITH",
		.flags  = V4L2_CTRL_FLAG_VOLATILE,
		.min	= 0,
		.max	= 255,
		.step	= 1,
	},
};

/**************************************************************************
 * I2C Definitions
 **************************************************************************/
/* Write starts with the upper byte of register 0x02 */
#define WRITE_REG_NUM       RADIO_REGISTER_NUM
#define WRITE_INDEX(i)      ((i + 0x02)%16)

/* Read starts with the upper byte of register 0x0a */
#define READ_REG_NUM        RADIO_REGISTER_NUM
#define READ_INDEX(i)       ((i + RADIO_REGISTER_NUM - 0x0a) % READ_REG_NUM)
static int rtc6213n_radio_add_new_custom(struct rtc6213n_device *radio,
	enum rtc6213n_ctrl_id id);

/*static*/
struct tasklet_struct my_tasklet;
/**************************************************************************
 * General Driver Functions - REGISTERs
 **************************************************************************/

/*
 * rtc6213n_get_register - read register
 */
int rtc6213n_get_register(struct rtc6213n_device *radio, int regnr)
{
	u16 buf[READ_REG_NUM];
	struct i2c_msg msgs[1] = {
		{ radio->client->addr, I2C_M_RD, sizeof(u16) * READ_REG_NUM,
			(void *)buf },
	};

	if (i2c_transfer(radio->client->adapter, msgs, 1) != 1)
		return -EIO;

	radio->registers[regnr] = __be16_to_cpu(buf[READ_INDEX(regnr)]);

	return 0;
}


/*
 * rtc6213n_set_register - write register
 */
int rtc6213n_set_register(struct rtc6213n_device *radio, int regnr)
{
	int i;
	u16 buf[WRITE_REG_NUM];
	struct i2c_msg msgs[1] = {
		{ radio->client->addr, 0, sizeof(u16) * WRITE_REG_NUM,
			(void *)buf },
	};

	for (i = 0; i < WRITE_REG_NUM; i++)
		buf[i] = __cpu_to_be16(radio->registers[WRITE_INDEX(i)]);

	if (i2c_transfer(radio->client->adapter, msgs, 1) != 1)
		return -EIO;

	return 0;
}

/*
 * rtc6213n_set_register - write register
 */
int rtc6213n_set_serial_registers(struct rtc6213n_device *radio,
	u16 *data, int bytes)
{
	int i;
	u16 buf[46];
	struct i2c_msg msgs[1] = {
		{ radio->client->addr, 0, sizeof(u16) * bytes,
			(void *)buf },
	};

	for (i = 0; i < bytes; i++)
		buf[i] = __cpu_to_be16(data[i]);

	if (i2c_transfer(radio->client->adapter, msgs, 1) != 1)
		return -EIO;

	return 0;
}

/**************************************************************************
 * General Driver Functions - ENTIRE REGISTERS
 **************************************************************************/
/*
 * rtc6213n_get_all_registers - read entire registers
 */
/* changed from static */
int rtc6213n_get_all_registers(struct rtc6213n_device *radio)
{
	int i;
	u16 buf[READ_REG_NUM];
	struct i2c_msg msgs[1] = {
		{ radio->client->addr, I2C_M_RD, sizeof(u16) * READ_REG_NUM,
			(void *)buf },
	};

	if (i2c_transfer(radio->client->adapter, msgs, 1) != 1)
		return -EIO;

	for (i = 0; i < READ_REG_NUM; i++)
		radio->registers[i] = __be16_to_cpu(buf[READ_INDEX(i)]);

	return 0;
}

/*
 * rtc6213n_get_allbanks_registers - read entire registers of each bank
 * in case of need, we keep here
 */
#if 0
int rtc6213n_get_allbanks_registers(struct rtc6213n_device *radio)
{
	int i;
	u16 buf[READ_REG_NUM];

	radio->registers[BANKCFG] = 0x4000;
	retval = rtc6213n_set_register(radio, BANKCFG);
	if (retval < 0)
		goto done;

	struct i2c_msg msgs[1] = {
		{ radio->client->addr, I2C_M_RD, sizeof(u16) * READ_REG_NUM,
			(void *)buf },
	};

	if (i2c_transfer(radio->client->adapter, msgs, 1) != 1)
		return -EIO;

	for (i = 0; i < READ_REG_NUM; i++)
		radio->registers[i] = __be16_to_cpu(buf[READ_INDEX(i)]);

	return 0;
}
#endif

int rtc6213n_disconnect_check(struct rtc6213n_device *radio)
{
	return 0;
}

/**************************************************************************
 * File Operations Interface
 **************************************************************************/

/*
 * rtc6213n_fops_open - file open
 */
int rtc6213n_fops_open(struct file *file)
{
	struct rtc6213n_device *radio = video_drvdata(file);
	int retval = v4l2_fh_open(file);

	dev_info(&radio->videodev.dev, "rtc6213n_fops_open radio->handler=%p\n", (void *)&radio->ctrl_handler);


	return retval;
}

/*
 * rtc6213n_fops_release - file release
 */
int rtc6213n_fops_release(struct file *file)
{
	struct rtc6213n_device *radio = video_drvdata(file);
	int retval = v4l2_fh_release(file);

	dev_info(&radio->videodev.dev, "rtc6213n_fops_release : Exit\n");
	return retval;
}



/**************************************************************************
 * Video4Linux Interface
 **************************************************************************/

/*
 * rtc6213n_vidioc_querycap - query device capabilities
 */
int rtc6213n_vidioc_querycap(struct file *file, void *priv,
	struct v4l2_capability *capability)
{
	strlcpy(capability->driver, DRIVER_NAME, sizeof(capability->driver));
	strlcpy(capability->card, DRIVER_CARD, sizeof(capability->card));
	capability->version = DRIVER_KERNEL_VERSION;
	capability->capabilities = V4L2_CAP_HW_FREQ_SEEK |
		V4L2_CAP_TUNER | V4L2_CAP_RADIO;

	return 0;
}



/**************************************************************************
 * I2C Interface
 **************************************************************************/

/*
 * rtc6213n_i2c_interrupt - interrupt handler
 */
static irqreturn_t rtc6213n_i2c_interrupt(int irq, void *dev_id)
{
	struct rtc6213n_device *radio = dev_id;
	unsigned char regnr;
	unsigned char blocknum;
	unsigned short bler; /* rds block errors */
	unsigned short rds;
	unsigned char tmpbuf[3];
	int retval = 0;

	/* check Seek/Tune Complete */
	retval = rtc6213n_get_register(radio, STATUS);
	if (retval < 0)
		goto end;

	retval = rtc6213n_get_register(radio, RSSI);
	if (retval < 0)
		goto end;

	if ((rtc6213n_wq_flag == SEEK_WAITING) ||
		(rtc6213n_wq_flag == TUNE_WAITING)) {
		if (radio->registers[STATUS] & STATUS_STD) {
			rtc6213n_wq_flag = WAIT_OVER;
			wake_up_interruptible(&rtc6213n_wq);
			/* ori: complete(&radio->completion); */
			dev_info(&radio->videodev.dev, "rtc6213n_i2c_interrupt Seek/Tune Done\n");
			dev_info(&radio->videodev.dev, "STATUS=0x%4.4hx, STD = %d, SF = %d, RSSI = %d\n",
			radio->registers[STATUS],
			(radio->registers[STATUS] & STATUS_STD) >> 14,
			(radio->registers[STATUS] & STATUS_SF) >> 13,
			(radio->registers[RSSI] & RSSI_RSSI));
		}
		goto end;
	}

	/* Update RDS registers */
	for (regnr = 1; regnr < RDS_REGISTER_NUM; regnr++) {
		retval = rtc6213n_get_register(radio, STATUS + regnr);
		if (retval < 0)
			goto end;
	}

	/* get rds blocks */
	if ((radio->registers[STATUS] & STATUS_RDS_RDY) == 0)
		/* No RDS group ready, better luck next time */
		goto end;
	#ifdef _RDSDEBUG
	dev_info(&radio->videodev.dev, "interrupt : STATUS=0x%4.4hx, RSSI=%d\n",
		radio->registers[STATUS], radio->registers[RSSI] & RSSI_RSSI);
	dev_info(&radio->videodev.dev, "BAErr %d, BBErr %d, BCErr %d, BDErr %d\n",
		(radio->registers[RSSI] & RSSI_RDS_BA_ERRS) >> 14,
		(radio->registers[RSSI] & RSSI_RDS_BB_ERRS) >> 12,
		(radio->registers[RSSI] & RSSI_RDS_BC_ERRS) >> 10,
		(radio->registers[RSSI] & RSSI_RDS_BD_ERRS) >> 8);
	dev_info(&radio->videodev.dev, "RDS_RDY=%d, RDS_SYNC=%d\n",
		(radio->registers[STATUS] & STATUS_RDS_RDY) >> 15,
		(radio->registers[STATUS] & STATUS_RDS_SYNC) >> 11);
	#endif

	for (blocknum = 0; blocknum < 5; blocknum++) {
		switch (blocknum) {
		default:
			bler = (radio->registers[RSSI] &
					RSSI_RDS_BA_ERRS) >> 14;
			rds = radio->registers[BA_DATA];
			break;
		case 1:
			bler = (radio->registers[RSSI] &
					RSSI_RDS_BB_ERRS) >> 12;
			rds = radio->registers[BB_DATA];
			break;
		case 2:
			bler = (radio->registers[RSSI] &
					RSSI_RDS_BC_ERRS) >> 10;
			rds = radio->registers[BC_DATA];
			break;
		case 3:
			bler = (radio->registers[RSSI] &
					RSSI_RDS_BD_ERRS) >> 8;
			rds = radio->registers[BD_DATA];
			break;
		case 4:		/* block index 4 for RSSI */
			bler = 0;
			rds = radio->registers[RSSI] & RSSI;
			break;
		};

		/* Fill the V4L2 RDS buffer */
		put_unaligned_le16(rds, &tmpbuf);
		tmpbuf[2] = blocknum;       /* offset name */

		tmpbuf[2] |= blocknum << 3; /* received offset */
		tmpbuf[2] |= bler << 6;

		/* copy RDS block to internal buffer */
		memcpy(&radio->buffer[radio->wr_index], &tmpbuf, 3);
		radio->wr_index += 3;

		/* wrap write pointer */
		if (radio->wr_index >= radio->buf_size)
			radio->wr_index = 0;

		/* check for overflow */
		if (radio->wr_index == radio->rd_index) {
			/* increment and wrap read pointer */
			radio->rd_index += 3;
			if (radio->rd_index >= radio->buf_size)
				radio->rd_index = 0;
		}
	}

	if (radio->wr_index != radio->rd_index)
		wake_up_interruptible(&radio->read_queue);

end:

	return IRQ_HANDLED;
}

static int rtc6213n_radio_add_new_custom(struct rtc6213n_device *radio,
	enum rtc6213n_ctrl_id id)
{
	int retval;
	struct v4l2_ctrl *ctrl;

	ctrl = v4l2_ctrl_new_custom(&radio->ctrl_handler,
		&rtc6213n_ctrls[id], NULL);
	retval = radio->ctrl_handler.error;
	if (ctrl == NULL && retval)
		dev_err(radio->v4l2_dev.dev, "Could not initialize '%s' control %d\n",
			rtc6213n_ctrls[id].name, retval);
	return retval;
}

/*
 * rtc6213n_i2c_probe - probe for the device
 */
static int rtc6213n_i2c_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	struct rtc6213n_device *radio;
	struct v4l2_device *v4l2_dev;
	struct v4l2_ctrl_handler *hdl = NULL;
	struct device_node *np = client->dev.of_node;
	int retval = 0;

	/* struct v4l2_ctrl *ctrl; */
#ifdef _CHECKIRQGPIO
	/* need to add description "irq-fm" in dts */
	enum of_gpio_flags irq_flags;
	int gpio;
	int irq;
#endif

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		retval = -ENODEV;
		goto err_initial;
	}

	if (!np) {
		dev_err(&client->dev, "no device tree\n");
		return -EINVAL;
	}

	/* private data allocation and initialization */
	radio = kzalloc(sizeof(struct rtc6213n_device), GFP_KERNEL);
	if (!radio) {
		retval = -ENOMEM;
		goto err_initial;
	}

	v4l2_dev = &radio->v4l2_dev;
	retval = v4l2_device_register(&client->dev, v4l2_dev);
	if (retval < 0) {
		dev_err(&client->dev, "couldn't register v4l2_device\n");
		goto err_video;
	}

	hdl = &radio->ctrl_handler;
	v4l2_ctrl_handler_init(hdl, ARRAY_SIZE(rtc6213n_ctrls));

	/* private CID addressed on V4L2_CID_PRIVATE_BASE would not
	   get the proper type from v4l2_ctrl_fill */
	retval = rtc6213n_radio_add_new_custom(radio,
		RTC6213N_ID_CSR0_ENABLE);
	if (retval < 0)
		goto errunreg;

	retval = rtc6213n_radio_add_new_custom(radio,
		RTC6213N_ID_CSR0_DISABLE);
	if (retval < 0)
		goto errunreg;

	retval = rtc6213n_radio_add_new_custom(radio,
		RTC6213N_ID_DEVICEID);
	if (retval < 0)
		goto errunreg;

	retval = rtc6213n_radio_add_new_custom(radio,
		RTC6213N_ID_CSR0_DIS_SMUTE);
	if (retval < 0)
		goto errunreg;

	retval = rtc6213n_radio_add_new_custom(radio,
		RTC6213N_ID_CSR0_DIS_MUTE);
	if (retval < 0)
		goto errunreg;

	retval = rtc6213n_radio_add_new_custom(radio,
		RTC6213N_ID_CSR0_DEEM);
	if (retval < 0)
		goto errunreg;

	retval = rtc6213n_radio_add_new_custom(radio,
		RTC6213N_ID_CSR0_BLNDADJUST);
	if (retval < 0)
		goto errunreg;

	retval = rtc6213n_radio_add_new_custom(radio,
		RTC6213N_ID_CSR0_VOLUME);
	if (retval < 0)
		goto errunreg;

	retval = rtc6213n_radio_add_new_custom(radio,
		RTC6213N_ID_CSR0_BAND);
	if (retval < 0)
		goto errunreg;

	retval = rtc6213n_radio_add_new_custom(radio,
		RTC6213N_ID_CSR0_CHSPACE);
	if (retval < 0)
		goto errunreg;

	retval = rtc6213n_radio_add_new_custom(radio,
		RTC6213N_ID_CSR0_DIS_AGC);
	if (retval < 0)
		goto errunreg;

	retval = rtc6213n_radio_add_new_custom(radio,
		RTC6213N_ID_CSR0_RDS_EN);
	if (retval < 0)
		goto errunreg;

	retval = rtc6213n_radio_add_new_custom(radio,
		RTC6213N_ID_SEEK_CANCEL);
	if (retval < 0)
		goto errunreg;

	retval = rtc6213n_radio_add_new_custom(radio,
		RTC6213N_ID_CSR0_SEEKRSSITH);
	if (retval < 0)
		goto errunreg;

	retval = rtc6213n_radio_add_new_custom(radio,
		RTC6213N_ID_CSR0_OFSTH);
	if (retval < 0)
		goto errunreg;

	retval = rtc6213n_radio_add_new_custom(radio,
		RTC6213N_ID_CSR0_QLTTH);
	if (retval < 0)
		goto errunreg;

	retval = rtc6213n_radio_add_new_custom(radio,
		RTC6213N_ID_RSSI);
	if (retval < 0)
		goto errunreg;

	radio->users = 0;
	radio->client = client;
	mutex_init(&radio->lock);

	memcpy(&radio->videodev, &rtc6213n_viddev_template,
		sizeof(struct video_device));

	i2c_set_clientdata(client, radio);		/* move from below */
	radio->videodev.lock = &radio->lock;
	radio->videodev.v4l2_dev = v4l2_dev;
	radio->videodev.ioctl_ops = &rtc6213n_ioctl_ops;
	video_set_drvdata(&radio->videodev, radio);

	/* get device and chip versions */
	if (rtc6213n_get_all_registers(radio) < 0) {
		retval = -EIO;
		goto err_video;
	}
	
/*
	dev_info(&client->dev,
		"rtc6213n_i2c_probe DeviceID=0x%4.4hx ChipID=0x%4.4hx ctrls_size=%d handler=%p\n",
		radio->registers[DEVICEID], radio->registers[CHIPID],
		1 + ARRAY_SIZE(rtc6213n_ctrls), (void *)hdl);
*/
	v4l2_dev->ctrl_handler = hdl;
	v4l2_ctrl_handler_setup(hdl);

#ifdef _CHECKIRQGPIO
	gpio = of_get_named_gpio_flags(np, "irq_gpio", 0, &irq_flags);
	if (!gpio_is_valid(gpio)) {
		dev_info(&client->dev, "invalid gpio: %d\n", gpio);
		return -1;
	}

	if (gpio_request(gpio, "int_overcurrent")) {
		dev_info(&client->dev, "can't request gpio %d\n", gpio);
		return -1;
	}

	if (gpio_direction_input(gpio))	{
		dev_info(&client->dev, "can't configure gpio %d\n", gpio);
		gpio_free(gpio);
		return -1;
	}

	irq = gpio_to_irq(gpio);
#endif

	/* rds buffer allocation */
	radio->buf_size = rds_buf * 3;
	radio->buffer = kmalloc(radio->buf_size, GFP_KERNEL);
	if (!radio->buffer) {
		retval = -EIO;
		goto err_video;
	}

	/* rds buffer configuration */
	radio->wr_index = 0;
	radio->rd_index = 0;
	init_waitqueue_head(&radio->read_queue);
	init_waitqueue_head(&rtc6213n_wq);

	/* mark Seek/Tune Complete Interrupt enabled */
	radio->stci_enabled = true;
	init_completion(&radio->completion);

#ifdef _CHECKIRQGPIO
	retval = devm_request_threaded_irq(&client->dev, irq, NULL,
		rtc6213n_i2c_interrupt,	IRQF_TRIGGER_FALLING|IRQF_ONESHOT,
		DRIVER_NAME, radio);
#else
	dev_info(&client->dev, "rtc6213n_i2c_probe DeviceID=0x%4.4hx ChipID=0x%4.4hx\n",
		radio->registers[DEVICEID], radio->registers[CHIPID]);
	retval = request_threaded_irq(client->irq, NULL, rtc6213n_i2c_interrupt,
		IRQF_TRIGGER_FALLING|IRQF_ONESHOT,
		DRIVER_NAME, radio); /*irq_test */
#endif

	if (retval) {
		dev_err(&client->dev, "Failed to register interrupt\n");
		goto err_rds;
	}

	/* register video device */
	retval = video_register_device(&radio->videodev, VFL_TYPE_RADIO,
		radio_nr);
	if (retval) {
		dev_info(&client->dev, "Could not register video device\n");
		goto err_all;
	}
	/* i2c_set_clientdata(client, radio); */
	dev_info(&client->dev, "rtc6213n_i2c_probe exit\n");

	return 0;
err_all:
	free_irq(client->irq, radio);
err_rds:
	kfree(radio->buffer);
err_video:
	video_device_release(&radio->videodev);
errunreg:
	v4l2_ctrl_handler_free(hdl);
	v4l2_device_unregister(v4l2_dev);
/* err_radio: */
	kfree(radio);
err_initial:
	return retval;
}

/*
 * rtc6213n_i2c_remove - remove the device
 */
static int rtc6213n_i2c_remove(struct i2c_client *client)
{
	struct rtc6213n_device *radio = i2c_get_clientdata(client);

	free_irq(client->irq, radio);
	video_device_release(&radio->videodev);
	v4l2_ctrl_handler_free(&radio->ctrl_handler);
	video_unregister_device(&radio->videodev);
	v4l2_device_unregister(&radio->v4l2_dev);
	kfree(radio);
	dev_info(&client->dev, "rtc6213n_i2c_remove exit\n");

	return 0;
}

#ifdef CONFIG_PM
/*
 * rtc6213n_i2c_suspend - suspend the device
 */
static int rtc6213n_i2c_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct rtc6213n_device *radio = i2c_get_clientdata(client);

	dev_info(&radio->videodev.dev, "rtc6213n_i2c_suspend\n");
	return 0;
}


/*
 * rtc6213n_i2c_resume - resume the device
 */
static int rtc6213n_i2c_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct rtc6213n_device *radio = i2c_get_clientdata(client);

	dev_info(&radio->videodev.dev, "rtc6213n_i2c_resume\n");

	return 0;
}

static SIMPLE_DEV_PM_OPS(rtc6213n_i2c_pm, rtc6213n_i2c_suspend,
						rtc6213n_i2c_resume);
#endif


/*
 * rtc6213n_i2c_driver - i2c driver interface
 */
struct i2c_driver rtc6213n_i2c_driver = {
	.driver = {
		.name			= "rtc6213n",
		.owner			= THIS_MODULE,
		.of_match_table = of_match_ptr(rtc6213n_i2c_dt_ids),
#ifdef CONFIG_PM
		.pm				= &rtc6213n_i2c_pm,
#endif
	},
	.probe				= rtc6213n_i2c_probe,
	.remove				= rtc6213n_i2c_remove,
	.id_table			= rtc6213n_i2c_id,
};

/*
 * rtc6213n_i2c_init
 */
int rtc6213n_i2c_init(void)
{
	pr_info(KERN_INFO DRIVER_DESC ", Version " DRIVER_VERSION "\n");
	return i2c_add_driver(&rtc6213n_i2c_driver);
}

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_VERSION(DRIVER_VERSION);
