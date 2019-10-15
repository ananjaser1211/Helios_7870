/*
 *  smssdio.c - Siano 1xxx SDIO interface driver
 *
 *  Copyright 2008 Pierre Ossman
 *
 * Copyright (C) 2006-2011, Siano Mobile Silicon (Doron Cohen)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 *
 * This hardware is a bit odd in that all transfers should be done
 * to/from the SMSSDIO_DATA register, yet the "increase address" bit
 * always needs to be set.
 *
 * Also, buffers from the card are always aligned to 128 byte
 * boundaries.
 */

/*
 * General cleanup notes:
 *
 * - only typedefs should be name *_t
 *
 * - use ERR_PTR and friends for smscore_register_device()
 *
 * - smscore_getbuffer should zero fields
 *
 * Fix stop command
 */

#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>

#include "smscoreapi.h"
#include "sms-cards.h"

/* Registers */

#define SMSSDIO_DATA		0x00
#define SMSSDIO_INT		0x04
#define SMSSDIO_AHB_CNT		0x1C
#define SMSSDIO_BLOCK_SIZE	128
#define MAX_SDIO_BUF_SIZE	0x8000
#define SMSSDIO_CCCR		6

static const struct sdio_device_id smssdio_ids[] = {
	{SDIO_DEVICE(SDIO_VENDOR_ID_SIANO, SDIO_DEVICE_ID_SIANO_STELLAR),
	 .driver_data = SMS1XXX_BOARD_SIANO_STELLAR},
	{SDIO_DEVICE(SDIO_VENDOR_ID_SIANO, SDIO_DEVICE_ID_SIANO_NOVA_A0),
	 .driver_data = SMS1XXX_BOARD_SIANO_NOVA_A},
	{SDIO_DEVICE(SDIO_VENDOR_ID_SIANO, SDIO_DEVICE_ID_SIANO_NOVA_B0),
	 .driver_data = SMS1XXX_BOARD_SIANO_NOVA_B},
	{SDIO_DEVICE(SDIO_VENDOR_ID_SIANO, SDIO_DEVICE_ID_SIANO_VEGA_A0),
	 .driver_data = SMS1XXX_BOARD_SIANO_VEGA},
	{SDIO_DEVICE(SDIO_VENDOR_ID_SIANO, SDIO_DEVICE_ID_SIANO_VENICE),
	 .driver_data = SMS1XXX_BOARD_SIANO_VEGA},
	{SDIO_DEVICE(SDIO_VENDOR_ID_SIANO, 0x302),
	 .driver_data = SMS1XXX_BOARD_SIANO_MING},
	{SDIO_DEVICE(SDIO_VENDOR_ID_SIANO, 0x320),
	 .driver_data = SMS1XXX_BOARD_SIANO_QING},
	{SDIO_DEVICE(SDIO_VENDOR_ID_SIANO, 0x400),
	 .driver_data = SMS1XXX_BOARD_SIANO_MING},
	{SDIO_DEVICE(SDIO_VENDOR_ID_SIANO, 0x500),
	 .driver_data = SMS1XXX_BOARD_SIANO_PELE},
	{SDIO_DEVICE(SDIO_VENDOR_ID_SIANO, 0x510),
	 .driver_data = SMS1XXX_BOARD_SIANO_ZICO},
	{SDIO_DEVICE(SDIO_VENDOR_ID_SIANO, 0x600),
	 .driver_data = SMS1XXX_BOARD_SIANO_RIO},
	{SDIO_DEVICE(SDIO_VENDOR_ID_SIANO, 0x610),
	 .driver_data = SMS1XXX_BOARD_SIANO_SANTOS},
    {SDIO_DEVICE(SDIO_VENDOR_ID_SIANO, 0x700),
	 .driver_data = SMS1XXX_BOARD_SIANO_SIENA},
    {SDIO_DEVICE(SDIO_VENDOR_ID_SIANO, 0x720),
	 .driver_data = SMS1XXX_BOARD_SIANO_MANILA},
	{SDIO_DEVICE(SDIO_VENDOR_ID_SIANO, 0x800),
	 .driver_data = SMS1XXX_BOARD_SIANO_DENVER_1530},
    {SDIO_DEVICE(SDIO_VENDOR_ID_SIANO, 0x810),
	 .driver_data = SMS1XXX_BOARD_SIANO_DENVER_2160},
	{ /* end: all zeroes */ },
};

MODULE_DEVICE_TABLE(sdio, smssdio_ids);

struct smssdio_device {
	struct sdio_func *func;
	struct work_struct work_thread;
	void *coredev;
	bool wait_for_version_resp;
	int chip_metal;
	struct completion version_ex_done;
	struct smscore_buffer_t *split_cb;
};

static u32 sdio_use_workthread = 0;

module_param(sdio_use_workthread, int, S_IRUGO);
MODULE_PARM_DESC(sdio_use_workthread, "Use workthread for sdio interupt handling. Required for specific host drivers (defaule 0)");

static u32 sdio_plugin_delay = 0;

module_param(sdio_plugin_delay, int, S_IRUGO);
MODULE_PARM_DESC(sdio_plugin_delay, "Specify the sleep time in mSec need to wait before communicating with plugged in device. 0 means no wait. (defaule 0)");
static struct sdio_func sdio_func_0;


/*******************************************************************/
/* Siano core callbacks                                            */
/*******************************************************************/

static int smssdio_sendrequest(void *context, void *buffer, size_t size)
{
	int ret = 0;
	struct smssdio_device *smsdev;
	void* auxbuf = NULL;

	smsdev = context;
	
	if (size & 3)
	{
		/* Make sure size is aligned to 32 bits, round up if required*/			
		auxbuf = kmalloc((size + 3) & 0xfffffffc, GFP_KERNEL);
		if (!auxbuf)
		{
			sms_err("Failed to allocate memory");
			return -ENOMEM;
		}
			
		memcpy (auxbuf, buffer, size);
		buffer = auxbuf;
		size = (size + 3) & 0xfffffffc;
	}

	sdio_claim_host(smsdev->func);
	while (size >= smsdev->func->cur_blksize) {
		ret = sdio_memcpy_toio(smsdev->func, SMSSDIO_DATA,
					buffer, smsdev->func->cur_blksize);
		if (ret)
			goto out;

		buffer += smsdev->func->cur_blksize;
		size -= smsdev->func->cur_blksize;
	}

	if (size) {
		ret = sdio_memcpy_toio(smsdev->func, SMSSDIO_DATA,
					buffer, size);
	}

out:
	if (auxbuf)
		kfree(auxbuf);
	sdio_release_host(smsdev->func);

	return ret;
}

/*******************************************************************/
/* SDIO callbacks                                                  */
/*******************************************************************/


static int verify_valid_hdr(struct SmsMsgHdr_S *hdr, int max_size)
{
	if ((hdr->msgType < MSG_TYPE_BASE_VAL) || (hdr->msgType > MSG_LAST_MSG_TYPE))
	{
		sms_debug("Msg has bad type");
		return -EINVAL;
	}
	if (hdr->msgLength > MAX_SDIO_BUF_SIZE)
	{
		sms_debug("Msg has bad length");
		return -EINVAL;
	}
	if (!(hdr->msgFlags & MSG_HDR_FLAG_SPLIT_MSG) && 
		(hdr->msgLength > max_size))
	{
		sms_debug("Msg is not split with length more than transaction.");
		return -EINVAL;
	}
	if ((hdr->msgFlags & MSG_HDR_FLAG_SPLIT_MSG) &&
	    !((hdr->msgType == MSG_SMS_DAB_CHANNEL) || (hdr->msgType == MSG_SMS_DVBT_BDA_DATA)))
	{
		sms_debug("Control Msg has split");
		return -EINVAL;
	}
	return 0;


}

static void smssdio_work_thread(struct work_struct *arg)
{
	int ret, isr, trnsfr_cnt, cur_trnsfr;
	int bytes_transfered;
	int abort;

	struct smscore_buffer_t local_buf;
        struct smscore_buffer_t *cb = NULL;
	struct SmsMsgHdr_S *hdr;
	size_t size, msg_size;
	int split;
	char* tmp_buf;
    	struct smssdio_device *smsdev = container_of(arg, struct smssdio_device, work_thread);
	 
	BUG_ON(smsdev->func->cur_blksize != SMSSDIO_BLOCK_SIZE);
	/*
	 * The interrupt register has no defined meaning. It is just
	 * a way of turning of the level triggered interrupt.
	 */
	sdio_claim_host(smsdev->func);

	bytes_transfered = 0;
	trnsfr_cnt = 	sdio_readb(smsdev->func, SMSSDIO_AHB_CNT+2, &ret) << 16 | 
			sdio_readb(smsdev->func, SMSSDIO_AHB_CNT+1, &ret) << 8 | 
			sdio_readb(smsdev->func, SMSSDIO_AHB_CNT, &ret);
	isr = sdio_readb(smsdev->func, SMSSDIO_INT, &ret);
	if (!(isr & 1))
	{
		int i;
		for (i = 0; ((!isr) && (i < 10)) ; i++)
		{
			sms_err("Received false interrupt!!!! waiting (%d times) (isr=0x%x) trnsfr_cnt=%d", i, isr, trnsfr_cnt);
			msleep(1);
			isr = sdio_readb(smsdev->func, SMSSDIO_INT, &ret);
			sms_err("after sleep 0x%x", isr);
		}
		/*
		*no true interrupt received
		*/
		if (!isr)
		{
			sdio_release_host(smsdev->func);
			return;
		}
	}
		
	if ((trnsfr_cnt > MAX_SDIO_BUF_SIZE) || (trnsfr_cnt < 0))
	{
		printk("Got bad transaction length. Assume 0 transaction length and get out (probably cannot recover from this)\n");
		trnsfr_cnt = 0;
		goto exit_with_error;
	
	}

	if (ret) {
		sms_err("Got error reading interrupt status=%d, isr=0x%x\n", ret, isr);
		isr = sdio_readb(smsdev->func, SMSSDIO_INT, &ret);
		if (ret)
		{
			sms_err("Second read also failed, try to recover\n");
			goto exit_with_error;
		}
		sms_err("Second read succeed status=%d, isr=0x%x (continue)\n", ret, isr);
	}
	

	if (smsdev->split_cb == NULL) {		
		if (smsdev->coredev)
		{
			cb = smscore_getbuffer(smsdev->coredev);
			if (!cb) {
				sms_err("Unable to allocate data buffer!\n");
				goto exit_with_error;
			}
		}
		else
		{
			cb = &local_buf;
			cb->p = kmalloc_node(SMSSDIO_BLOCK_SIZE, GFP_ATOMIC, NUMA_NO_NODE);
		}
		split = 0;
		ret = sdio_memcpy_fromio(smsdev->func,
					 cb->p,
					 SMSSDIO_DATA,
					 SMSSDIO_BLOCK_SIZE);
		bytes_transfered += SMSSDIO_BLOCK_SIZE;
		if (ret) {
			sms_err("Error %d reading initial block\n", ret);
			goto exit_with_error;
		}

		hdr = cb->p;
		if (verify_valid_hdr(hdr,trnsfr_cnt))
		{
			sms_err("recieved bad header. abort the io operation. transaction size=%d, isr=0x%x\n", trnsfr_cnt, isr);
			sms_err("hdr start: 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x\n", 
				((u8*)hdr)[0], ((u8*)hdr)[1], ((u8*)hdr)[2], ((u8*)hdr)[3], 
				((u8*)hdr)[4], ((u8*)hdr)[5], ((u8*)hdr)[6], ((u8*)hdr)[7]);
			goto exit_with_error;
		}
		if (hdr->msgFlags & MSG_HDR_FLAG_SPLIT_MSG) {
			if (cb == &local_buf)
			{
				sms_err ("Recieved split message before the device was initialized. Ignoring message\n");
				kfree(cb->p);
				sdio_release_host(smsdev->func);
				return;
			}
			smsdev->split_cb = cb;
			sdio_release_host(smsdev->func);
			return;
		}

		if (hdr->msgLength > smsdev->func->cur_blksize)
		{
			if (cb == &local_buf)
			{
				sms_err ("Recieved long message before the device was initialized. Ignoring message\n");
				kfree(cb->p);
				sdio_release_host(smsdev->func);
				return;
			}
			size = hdr->msgLength - smsdev->func->cur_blksize;
		}
		else
			size = 0;
		msg_size=size;
	} else {
		cb = smsdev->split_cb;
		hdr = cb->p;
		split = 1;
		size = hdr->msgLength - sizeof(struct SmsMsgHdr_S);
		msg_size = size;
		smsdev->split_cb = NULL;
	}
	if (size) {
		void *buffer;

		buffer = cb->p + (hdr->msgLength - size);
		size = ALIGN(size, SMSSDIO_BLOCK_SIZE);

		/*
		 * Read one block at a time in order to know how many transfered...
		 */
		ret = sdio_memcpy_fromio(smsdev->func,
					  buffer, SMSSDIO_DATA,
					  size);
		if (ret) {
				/* Not sure how many bytes were transferred, but at least one block caused the error	*/
				/* The bytes_transfered increased by one block since it is the minimum and we don't  	*/
				/* want to read these bytes again during error recovery									*/ 
				bytes_transfered += SMSSDIO_BLOCK_SIZE;
				smscore_putbuffer(smsdev->coredev, cb);
				sms_err("Error %d reading "
					"data from card!\n", ret);
				cb = NULL;
				abort = 0x1;
				sdio_writeb(&sdio_func_0, abort, SMSSDIO_CCCR, &ret);
				goto exit_with_error;
			}
	}

	sdio_release_host(smsdev->func);
	cb->size = hdr->msgLength;
	cb->offset = 0;	
	if (smsdev->wait_for_version_resp)
	{
		if (hdr->msgType == MSG_SMS_GET_VERSION_EX_RES)
		{
			smsdev->chip_metal = ((u8*)(hdr+1))[3];
			sms_info("chip metal=0x%x\n", smsdev->chip_metal);
			smsdev->wait_for_version_resp = 0;
			sms_info("complete get version\n");
			complete(&smsdev->version_ex_done);
			if (cb != &local_buf)
			{
				sms_info("free the buffer (0x%p)", cb);
				smscore_putbuffer(smsdev->coredev, cb);
			}
			else
			{
				kfree(cb->p);
			}
			return;		
		}

	}

	smscore_onresponse(smsdev->coredev, cb);
	return;

exit_with_error:
	tmp_buf = kmalloc_node(SMSSDIO_BLOCK_SIZE, GFP_ATOMIC, NUMA_NO_NODE);
	 
	cur_trnsfr = sdio_readb(smsdev->func, SMSSDIO_AHB_CNT+2, &ret) << 16 | 
			sdio_readb(smsdev->func, SMSSDIO_AHB_CNT+1, &ret) << 8 | 
			sdio_readb(smsdev->func, SMSSDIO_AHB_CNT, &ret);
	
	while ((cur_trnsfr == trnsfr_cnt) && (bytes_transfered < trnsfr_cnt))
	{
		sdio_memcpy_fromio(smsdev->func,
				  tmp_buf, SMSSDIO_DATA,
				  SMSSDIO_BLOCK_SIZE);

		cur_trnsfr = 	sdio_readb(smsdev->func, SMSSDIO_AHB_CNT+2, &ret) << 16 | 
				sdio_readb(smsdev->func, SMSSDIO_AHB_CNT+1, &ret) << 8 | 
				sdio_readb(smsdev->func, SMSSDIO_AHB_CNT, &ret);
		sms_err("read another block. transfered %d of %d trnsfr_cnt=%d\n", bytes_transfered, trnsfr_cnt, trnsfr_cnt);
		bytes_transfered += SMSSDIO_BLOCK_SIZE;
	}
	kfree(tmp_buf);
	sms_err("bytes_transfered=%d, trnsfr_cnt=%d, cur_trnsfr=%d\n", bytes_transfered, trnsfr_cnt, cur_trnsfr);
	if (cb && cb != smsdev->split_cb && cb != &local_buf)
		smscore_putbuffer(smsdev->coredev, cb);
	else if (cb == &local_buf)
		kfree(cb->p);
	if (smsdev->split_cb)
	{ /*If header kept before, the bad message should drop the kept header*/
		smscore_putbuffer(smsdev->coredev, smsdev->split_cb);
		smsdev->split_cb = NULL;
	}
	sdio_release_host(smsdev->func);
	return;

}


static void smssdio_interrupt(struct sdio_func *func)
{
	struct smssdio_device *smsdev = sdio_get_drvdata(func);
	if (sdio_use_workthread == 0) /*When not required - handle everything from interrupt content*/
	{
		smssdio_work_thread(&smsdev->work_thread);
	}
	else
	{
		schedule_work(&smsdev->work_thread);
	}
}

static int smssdio_check_version(struct smssdio_device *smsdev)
{
	int ret = 1;
	struct SmsMsgHdr_S smsmsg;

	sms_debug("Checking the device version.\n");
	smsdev->wait_for_version_resp = 1;
	init_completion(&smsdev->version_ex_done);
	SMS_INIT_MSG(&smsmsg, MSG_SMS_GET_VERSION_EX_REQ,
		     sizeof(struct SmsMsgHdr_S));
	sms_debug("Sending get version message.\n");
	smssdio_sendrequest(smsdev, &smsmsg, sizeof(struct SmsMsgHdr_S));
	sms_debug("Waiting for version response.\n");
	/*Wait for response*/
	ret = wait_for_completion_timeout(&smsdev->version_ex_done, msecs_to_jiffies(1000));
	if (ret <= 0)
	{ /* No response recieved*/
		sms_info("No response to get version command");
		return -ENODEV;
	}
	return 0;
}



static int smssdio_probe(struct sdio_func *func,
			 const struct sdio_device_id *id)
{
	int ret;

	int board_id;
	struct smssdio_device *smsdev;
	struct smsdevice_params_t params;

	board_id = id->driver_data;

	smsdev = kzalloc(sizeof(struct smssdio_device), GFP_KERNEL);
	if (!smsdev)
		return -ENOMEM;

	if (func->num == 1)
	{
		sdio_func_0.num=0;
		sdio_func_0.card = func->card;
	}

	smsdev->func = func;
        INIT_WORK(&smsdev->work_thread, smssdio_work_thread);
	sms_debug("Claiming host\n");
	sdio_claim_host(func);
	sms_debug("Enable function\n");
	ret = sdio_enable_func(func);
	if (ret)
		goto claimed;
	sms_debug("Setting block size\n");
	ret = sdio_set_block_size(func, SMSSDIO_BLOCK_SIZE);
	if (ret)
		goto enabled;
	sms_debug("Registering interrupt.\n");
	ret = sdio_claim_irq(func, smssdio_interrupt);
	if (ret)
		goto enabled;

	sdio_set_drvdata(func, smsdev);
	sms_debug("Releasing the host.\n");
	sdio_release_host(func);

	memset(&params, 0, sizeof(struct smsdevice_params_t));

	params.device = &func->dev;
	params.buffer_size = MAX_SDIO_BUF_SIZE;	
	params.num_buffers = 14;
	params.context = smsdev;

	snprintf(params.devpath, sizeof(params.devpath),
		 "sdio\\%s", sdio_func_id(func));

	params.sendrequest_handler = smssdio_sendrequest;

	params.device_type = sms_get_board(board_id)->type;

	/*get version for Siena only*/
	/*This is due to bug - Siena A0/1 and Siena A2 has same PID but require different FW So in case of */
	/*seina check the step according to version*/
	if (params.device_type  == SMS_SIENA)
	{
		ret = smssdio_check_version(smsdev);
		if (ret ==0 && smsdev->chip_metal >= 2)
		{
			sms_debug("Changing the device type to Siena A2 following get version response\n");
			params.device_type = SMS_SIENA_A2;
		}
	}

	params.require_node_buffer = 1;

	if (params.device_type != SMS_STELLAR)
		params.flags |= SMS_DEVICE_FAMILY2;
	else {
		/*
		 * FIXME: Stellar needs special handling...
		 */
		ret = -ENODEV;
		goto reclaim;
	}
	if (sdio_plugin_delay)
		msleep (min((u32)1000, sdio_plugin_delay));
	ret = smscore_register_device(&params, &smsdev->coredev);
	if (ret < 0)
		goto reclaim;

	smscore_set_board_id(smsdev->coredev, board_id);

	ret = smscore_start_device(smsdev->coredev);
	if (ret < 0)
		goto registered;

	return 0;

registered:
	smscore_unregister_device(smsdev->coredev);
reclaim:
	sdio_claim_host(func);
	sdio_release_irq(func);
enabled:
	sdio_disable_func(func);
claimed:
	sdio_release_host(func);
	kfree(smsdev);

	return ret;
}

static void smssdio_remove(struct sdio_func *func)
{
	struct smssdio_device *smsdev;

	smsdev = sdio_get_drvdata(func);
	sdio_claim_host(func);

	sdio_release_irq(func);
	sdio_disable_func(func);
	sdio_release_host(func);

	if (smsdev->split_cb)
		smscore_putbuffer(smsdev->coredev, smsdev->split_cb);

	smscore_unregister_device(smsdev->coredev);

	kfree(smsdev);
}

static struct sdio_driver smssdio_driver = {
	.name = "smssdio",
	.id_table = smssdio_ids,
	.probe = smssdio_probe,
	.remove = smssdio_remove,
};

/*******************************************************************/
/* Module functions                                                */
/*******************************************************************/

int smssdio_register(void)
{
	int ret = 0;

	ret = sdio_register_driver(&smssdio_driver);

	return ret;
}

void smssdio_unregister(void)
{
	sdio_unregister_driver(&smssdio_driver);
}


