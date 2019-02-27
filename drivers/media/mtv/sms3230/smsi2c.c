/*
 *  smsi2c.c - Siano I2C interface driver
 *
 *  Copyright 2011 Siano Mobile Silicon, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 *
 */

#include <linux/moduleparam.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/slab.h>

#include "smscoreapi.h"
#include "sms-cards.h"

/************************************************************/
/*Platform specific defaults - can be changes by parameters */
/*or in compilation at this section                         */
/************************************************************/
/*Host GPIO pin used for SMS interrupt*/
#define HOST_INTERRUPT_PIN 	135

/*Host GPIO pin used to reset SMS device*/
#define HOST_SMS_RESET_PIN	0xFFFFFFFF

/*Host SPI bus number used for SMS*/
#define HOST_I2C_CTRL_NUM	0

/*SMS device I2C address*/
#define SMSI2C_CLIENT_ADDR		0x68

/*SMS device I2C Interrupt*/
#define SMSI2C_DEVICE_INT_GPIO		0xFFFFFFFF

/*Default SMS device type connected to SPI bus.*/
#define DEFAULT_SMS_DEVICE_TYPE		 SMS_RIO	

/*************************************/
/*End of platform specific parameters*/ 
/*************************************/

static int host_i2c_intr_pin = HOST_INTERRUPT_PIN;
static int host_i2c_ctrl = HOST_I2C_CTRL_NUM;
static int sms_i2c_addr = SMSI2C_CLIENT_ADDR;
static int device_int_line = SMSI2C_DEVICE_INT_GPIO;

module_param(host_i2c_intr_pin, int, S_IRUGO);
MODULE_PARM_DESC(host_i2c_intr_pin, "interrupt pin number used by Host to be interrupted by SMS.");

module_param(host_i2c_ctrl, int, S_IRUGO);
MODULE_PARM_DESC(host_i2c_ctrl, "Number of I2C Controllers used for SMS.");

module_param(sms_i2c_addr, int, S_IRUGO);
MODULE_PARM_DESC(sms_i2c_addr, "I2C Address of SMS device.");

module_param(device_int_line, int, S_IRUGO);
MODULE_PARM_DESC(device_int_line, "interrupt pin number used by Device to interrupt the host. (0xff = polling).");


/* Registers */

struct smsi2c_device {

	struct i2c_adapter *adap;
	struct i2c_client *client;
	int wait_for_version_resp;
	int chip_model;
	void *coredev;	
	struct completion version_ex_done;
};

struct smsi2c_device *g_smsi2c_device;

static void smsi2c_worker_thread(void *arg);
static DECLARE_WORK(smsi2c_work_queue, (void *)smsi2c_worker_thread);

/*******************************************************************/
/* Siano core callbacks                                            */
/*******************************************************************/

static int smsi2c_sendrequest(void *context, void *buffer, size_t size)
{
	int ret;
	struct smsi2c_device *smsdev = (struct smsi2c_device *)context;
	
	if (!smsdev)
	{
		sms_err("smsi2c_sendrequest smsdev NULL!!\n");
		return -ENODEV;
	}
		
	sms_info("Writing message to I2C, size = %d bytes.\n", size);
	sms_info("msg hdr: 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x.\n",
		((u8 *) buffer)[0], ((u8 *) buffer)[1], ((u8 *) buffer)[2],
		((u8 *) buffer)[3], ((u8 *) buffer)[4], ((u8 *) buffer)[5],
		((u8 *) buffer)[6], ((u8 *) buffer)[7]);

	ret = i2c_master_send(smsdev->client, buffer, (int)size);
	sms_debug("i2c_master_send returned %d", ret);

	return ret;
}

#define MAX_CHUNK_SIZE  (10*1024)

static int smsi2c_loadfirmware_handler(void *context, void* p_data, u32 fw_size)
{
	int ret;
	u8* fw_data = (u8*)p_data + 12;
	u32* fw_hdr = p_data;
	u32 actual_crc, dummy_crc,i;
	u32 chunk_size;
	u32 dummy_hdr[3];
	u32 fw_addr, fw_len, dnl_offset;

	
	struct SmsMsgHdr_S BackdoorMsg = {
		MSG_SMS_SWDOWNLOAD_BACKDOOR_REQ, 0, HIF_TASK,
			sizeof(struct SmsMsgHdr_S), 0};
	
	fw_addr = fw_hdr[2];
	fw_len = fw_hdr[1];

	/*The full CRC is for debug and printing, the function doesnt use this*/
	sms_debug("crc=0x%x, len=0x%x, addr=0x%x", fw_hdr[0], fw_len, fw_addr);
	actual_crc=0;
	for (i = 0; i < fw_len+8 ; i++)
	{
		actual_crc ^= ((u8*)p_data)[4+i];
	}
	sms_debug("actual calculated crc=0x%x", actual_crc);
	

	sms_debug("Sending the firmware content in chunks of no more than %dKB.\n", MAX_CHUNK_SIZE/1024);
	dnl_offset = fw_addr + fw_len;
	while (fw_len)
	{
		sms_debug("Sending backdoor command.\n");
		ret = smsi2c_sendrequest(context, &BackdoorMsg, sizeof(BackdoorMsg));
		if (ret)
		{
			sms_err ("failed sending backdoor command");
			return ret;
		}

		msleep(30);
		chunk_size = min((int)fw_len, MAX_CHUNK_SIZE);

		dnl_offset -= chunk_size;
		fw_len -= chunk_size;
		dummy_hdr[1] = chunk_size;
		dummy_hdr[2] = dnl_offset;

		dummy_crc=0;
		for (i = 0; i < 8 ; i++)
		{
			dummy_crc ^= ((u8*)dummy_hdr)[4+i];
		}
		for (i = 0; i < chunk_size ; i++)
		{
			dummy_crc ^= ((u8*)(fw_data+fw_len))[i];
		}
		sms_debug("download chunk size %d at offset 0x%x, act crc is 0x%x.\n", chunk_size, dnl_offset, dummy_crc);
		if (dnl_offset == fw_addr)
		{ /* Only for the last chunk send the correct CRC*/
			dummy_hdr[0] = dummy_crc;
		}
		else
		{/* for all but last chunk, make sure crc is wrong*/
			dummy_hdr[0] = dummy_crc^0x55;
		}
		/*send header of current chunk*/
		ret = smsi2c_sendrequest(context, (u8*)dummy_hdr, 12);
		if (ret)
		{
			sms_err ("failed sending fw header");
			return ret;
		}
		msleep(20);
		/*send the data of current chunk*/
		ret = smsi2c_sendrequest(context, 
						(u8*)(fw_data+fw_len), 
						chunk_size);
		if (ret)
		{
			sms_err ("failed sending fw data");
			return ret;
		}
		msleep(30);
	}
	sms_debug("FW download complete.\n");
	msleep(400);
			
		
	return 0;
}


/*******************************************************************/
/* i2c callbacks                                                  */
/*******************************************************************/

static void smsi2c_interrupt(void *context)
{
	
	struct smsi2c_device *smsdev = (struct smsi2c_device *)context;
	sms_debug("Recieved interrupt from SMS.\n");
	if (!smsdev)
		return;
	
	schedule_work(&smsi2c_work_queue);
}

static void smsi2c_worker_thread(void *args) 
{
	struct smscore_buffer_t *cb;
	struct SmsMsgHdr_S *phdr;
	u16 len;
	int ret;
	u8 local_buf[100];
	sms_debug("Worker thread is running.\n");
	if (!g_smsi2c_device->coredev)
	{
		sms_debug("Using local buffer\n");
		cb = NULL;
		phdr = (struct SmsMsgHdr_S *)local_buf;
	}
	else
	{
		sms_debug("Using core buffer\n");
		cb = smscore_getbuffer(g_smsi2c_device->coredev);
		if (!cb) {
			sms_err("Unable to allocate data buffer!\n");
			goto exit;
		}
		phdr = (struct SmsMsgHdr_S *)cb->p;
	}	
	sms_debug("Recieve the message header.....\n");
	memset(phdr, 0, (int)sizeof(struct SmsMsgHdr_S));
	sms_debug("buf before: 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x",
		((u8*)phdr)[0], ((u8*)phdr)[1], ((u8*)phdr)[2], ((u8*)phdr)[3], 
		((u8*)phdr)[4], ((u8*)phdr)[5], ((u8*)phdr)[6], ((u8*)phdr)[7]);
	ret = i2c_master_recv(g_smsi2c_device->client, 
							(void *)phdr, 
							(int)sizeof(struct SmsMsgHdr_S));
	if (ret < 0) {
		if ((void*)phdr != (void*)local_buf)
			smscore_putbuffer(g_smsi2c_device->coredev, cb);
		sms_err("Unable to read sms header! ret=%d\n", ret);
		goto exit;
	}
	sms_debug("hdr: type=%d, src=%d, dst=%d, len=%d, flag=0x%x\n", 
		phdr->msgType, phdr->msgSrcId, phdr->msgDstId, phdr->msgLength, phdr->msgFlags);
	sms_debug("buf: 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x",
		((u8*)phdr)[0], ((u8*)phdr)[1], ((u8*)phdr)[2], ((u8*)phdr)[3], 
		((u8*)phdr)[4], ((u8*)phdr)[5], ((u8*)phdr)[6], ((u8*)phdr)[7]);
	sms_debug("Recieve the rest of the message.....\n");
	len = phdr->msgLength;
	
	if (len > sizeof(struct SmsMsgHdr_S))
	{
		ret = i2c_master_recv(g_smsi2c_device->client, 
								(u8*)(phdr+1), 
								len - (int)sizeof(struct SmsMsgHdr_S));
		sms_debug("recv of data returned %d", ret);
		if (ret < 0) {
			if ((void*)phdr != (void*)local_buf)
				smscore_putbuffer(g_smsi2c_device->coredev, cb);
			sms_err("Unable to read sms payload!\n");
			goto exit;
		}
		sms_debug("data: 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x",
			((u8*)(phdr+1))[0], ((u8*)(phdr+1))[1], ((u8*)(phdr+1))[2], ((u8*)(phdr+1))[3], 
			((u8*)(phdr+1))[4], ((u8*)(phdr+1))[5], ((u8*)(phdr+1))[6], ((u8*)(phdr+1))[7]);
	}
	if ((phdr->msgType == MSG_SMS_GET_VERSION_EX_RES) && ((void*)phdr == (void*)local_buf))
	{ /*This was an internal command, so we won't send it out*/
		g_smsi2c_device->chip_model = *((u16*)(phdr+1));
		sms_info("chip model=0x%x\n", g_smsi2c_device->chip_model);
		g_smsi2c_device->wait_for_version_resp = 0;
		sms_info("complete get version\n");
		complete(&g_smsi2c_device->version_ex_done);
		sms_info("done.");
		return;
	}
	
	sms_debug("Message recieved. Sending to callback.....\n");	
	if (((void*)phdr != (void*)local_buf))
	{
		sms_debug("Ext buf.....\n");	
		cb->offset = 0;
		cb->size = len;
		if	(g_smsi2c_device->coredev)
		{
			smscore_onresponse(g_smsi2c_device->coredev, cb);
		}
	}
exit:
	return;
}


// allocate and init i2c dev descriptor
// update i2c client params
// 
static int smsi2c_probe(void)
{
	int ret;

	struct smsi2c_device *smsdev;
	struct smsdevice_params_t params;
	struct SmsMsgHdr_S smsmsg;
	struct SmsMsgData2Args_S setIntMsg = {{MSG_SMS_SPI_INT_LINE_SET_REQ, 
				0, 
				11,
				sizeof(struct SmsMsgData2Args_S),
				0},
				{0xff,
				20}};

	    struct i2c_board_info smsi2c_info = {
		I2C_BOARD_INFO("smsi2c", sms_i2c_addr),
	    };
	
	smsdev = kzalloc(sizeof(struct smsi2c_device), GFP_KERNEL);
	if (!smsdev)
	{
		sms_err("Cannot allocate memory for I2C device driver.\n");
		return -ENOMEM;
	}
		
	g_smsi2c_device = smsdev;
	sms_debug ("Memory allocated");
	smsdev->adap = i2c_get_adapter(host_i2c_ctrl);
	if (!smsdev->adap) {
		sms_err("Cannot get adapter #%d.\n", host_i2c_ctrl);
		ret = -ENODEV;
		goto failed_allocate_adapter;
	}
	sms_debug ("Got the adapter");

	smsi2c_info.platform_data = smsdev;

	smsdev->client = i2c_new_device(smsdev->adap, &smsi2c_info);

	if (!smsdev->client) {
		sms_err("Cannot register I2C device with addr 0x%x.\n", sms_i2c_addr);
		 ret = -ENODEV;
		 goto failed_allocate_device;
	}
	sms_debug ("Got the device");


	ret = gpio_request(host_i2c_intr_pin, "sms_gpio");
	if (ret) {
		sms_err("failed to get sms_gpio\n");
		 goto failed_allocate_gpio;
	}	
	gpio_direction_input(host_i2c_intr_pin);
	gpio_export(host_i2c_intr_pin, 0);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,39)
	irq_set_irq_type(gpio_to_irq(host_i2c_intr_pin), IRQ_TYPE_EDGE_FALLING);	
#else
	set_irq_type(gpio_to_irq(host_i2c_intr_pin), IRQ_TYPE_EDGE_FALLING);	
#endif
	/*register irq*/
	ret = request_irq( gpio_to_irq(host_i2c_intr_pin), (irq_handler_t)smsi2c_interrupt,
		 IRQF_TRIGGER_RISING, "SMSI2C", smsdev);
	if (ret < 0) {
		sms_err("failed to allocate interrupt for SMS\n");
		ret = -ENODEV;
		goto failed_allocate_interrupt;
	}	

	if (device_int_line != 0xFFFFFFFF)
	{ /* Device is not using the default interrupt pin*/

		sms_debug("Device is not using the default int pin, need to set the interrupt pin to %d", device_int_line);
		setIntMsg.msgData[1] = device_int_line;
		ret = smsi2c_sendrequest(smsdev, &setIntMsg, sizeof(setIntMsg));
		msleep(50);
	}

	init_completion(&smsdev->version_ex_done);
	smsdev->wait_for_version_resp = 1;
	SMS_INIT_MSG(&smsmsg, MSG_SMS_GET_VERSION_EX_REQ,
		     sizeof(struct SmsMsgHdr_S));
	smsi2c_sendrequest(smsdev, &smsmsg, sizeof(smsmsg));
	/*Wait for response*/
	ret = wait_for_completion_timeout(&smsdev->version_ex_done, msecs_to_jiffies(500));
	if (ret > 0)
	{ /*Got version. device is in*/
		sms_debug("Found and identified the I2C device");
	}
	else
	{ /* No response recieved*/
		sms_err("No response to get version command");
		ret = -ETIME;
		goto failed_registering_coredev;
	}


	memset(&params, 0, sizeof(struct smsdevice_params_t));

	params.device = (struct device *)&smsdev->client->adapter->dev;

	params.buffer_size = 0x400;	
	params.num_buffers = 20;	
	params.context = smsdev;

	snprintf(params.devpath, sizeof(params.devpath),
		 "i2c\\%s", "smsi2c");

	params.sendrequest_handler  = smsi2c_sendrequest;
	params.loadfirmware_handler = smsi2c_loadfirmware_handler;
	switch(smsdev->chip_model)
	{
		case 0: params.device_type = 0; break;
		case 0x1002:
		case 0x1102:
		case 0x1004: params.device_type = SMS_NOVA_B0; break;
		case 0x1182: params.device_type = SMS_VENICE; break;
		case 0x1530: params.device_type = SMS_DENVER_1530; break;
		case 0x2130: params.device_type = SMS_PELE; break;
		case 0x2160: params.device_type = SMS_DENVER_2160; break;
		case 0x2180: params.device_type = SMS_MING; break;
		case 0x2230: params.device_type = SMS_RIO; break;
		case 0x3130: params.device_type = SMS_ZICO; break;
		case 0x3180: params.device_type = SMS_QING; break;
		case 0x3230: params.device_type = SMS_SANTOS; break;
		case 0x4470: params.device_type = SMS_SIENA; break;
		default: params.device_type = 0; break;
	}

	/* Use SMS_DEVICE_FAMILY2 for firmware download over SMS MSGs
	   SMS_DEVICE_FAMILY1 for backdoor I2C firmware download */
	//params.flags |= SMS_DEVICE_FAMILY2;
	
	/* Device protocol completion events */

	ret = smscore_register_device(&params, &smsdev->coredev);
	if (ret < 0)
        {
	        printk(KERN_INFO "smscore_register_device error\n");
		goto failed_registering_coredev;
        }

	ret = smscore_start_device(smsdev->coredev);
	if (ret < 0)
        {
		printk(KERN_INFO "smscore_start_device error\n");
		goto failed_device_start;
        }

	return 0;
failed_device_start:
	smscore_unregister_device(smsdev->coredev);
failed_registering_coredev:
	free_irq(gpio_to_irq(host_i2c_intr_pin), smsdev);
failed_allocate_interrupt:
	gpio_free(host_i2c_intr_pin);
failed_allocate_gpio:
	i2c_unregister_device(smsdev->client);
failed_allocate_device:
	i2c_put_adapter(smsdev->adap);
failed_allocate_adapter:
	g_smsi2c_device = NULL;
	kfree(smsdev);

	return ret;
}



/*******************************************************************/
/* Module functions                                                */
/*******************************************************************/

int smsi2c_register(void)
{
	int ret = 0;
	
	printk(KERN_INFO "smsi2c: Siano SMS1xxx I2c driver\n");
	
	ret = smsi2c_probe();
	
	return ret;
}

void smsi2c_unregister(void)
{
	if	(g_smsi2c_device->coredev)
	{
		//need to save smsdev and check for null
		smscore_unregister_device(g_smsi2c_device->coredev);
		g_smsi2c_device->coredev = NULL;
	}
	free_irq(gpio_to_irq(host_i2c_intr_pin), g_smsi2c_device);
	gpio_free(host_i2c_intr_pin);
	i2c_unregister_device(g_smsi2c_device->client);
	i2c_put_adapter(g_smsi2c_device->adap);
	kfree(g_smsi2c_device);
	g_smsi2c_device = NULL;	
}


