/*****************************************************************************
	Copyright(c) 2013 FCI Inc. All Rights Reserved

	File name : fc8300.c

	Description : Driver source file

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

	History :
	----------------------------------------------------------------------
*******************************************************************************/
#include <linux/miscdevice.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/poll.h>
#include <linux/module.h>
#include <linux/slab.h>

#if defined(CONFIG_ISDBT_ANT_DET)
#include <linux/wakelock.h>
#include <linux/input.h>
#endif

#include "fc8300.h"
#include "bbm.h"
#include "fci_oal.h"
#include "fci_tun.h"
#include "fc8300_regs.h"
#include "fc8300_isr.h"
#include "fci_hal.h"

struct ISDBT_INIT_INFO_T *hInit;
#define FEATURE_GLOBAL_MEM

#ifdef FEATURE_GLOBAL_MEM
struct ISDBT_OPEN_INFO_T hOpen_Val;
#define RING_BUFFER_SIZE	(1024*1024)
u8 ringbuffer[RING_BUFFER_SIZE];
#else
#define RING_BUFFER_SIZE	(188 * 320 * 50)
#endif

u8 static_ringbuffer[RING_BUFFER_SIZE];
//#define FEATURE_TS_CHECK

#ifdef FEATURE_TS_CHECK
u32 check_cnt_size;

#define MAX_DEMUX           2

/*
 * Sync Byte 0xb8
 */
#define SYNC_BYTE_INVERSION

struct pid_info {
	unsigned long count;
	unsigned long discontinuity;
	unsigned long continuity;
};

struct demux_info {
	struct pid_info  pids[8192];

	unsigned long    ts_packet_c;
	unsigned long    malformed_packet_c;
	unsigned long    tot_scraped_sz;
	unsigned long    packet_no;
	unsigned long    sync_err;
	unsigned long    sync_err_set;
};

static int is_sync(unsigned char *p)
{
	int syncword = p[0];
#ifdef SYNC_BYTE_INVERSION
	if (0x47 == syncword || 0xb8 == syncword)
		return 1;
#else
	if (0x47 == syncword)
		return 1;
#endif
	return 0;
}
static struct demux_info demux[MAX_DEMUX];

int print_pkt_log(void)
{
	unsigned long i = 0;

	print_log(NULL, "\nPKT_TOT : %d, SYNC_ERR : %d, SYNC_ERR_BIT : %d, ERR_PKT : %d\n"
		, demux[0].ts_packet_c, demux[0].sync_err
		, demux[0].sync_err_set, demux[0].malformed_packet_c);

	for (i = 0; i < 8192; i++) {
		if (demux[0].pids[i].count > 0)
			print_log(NULL, "PID : %d, TOT_PKT : %d, DISCONTINUITY : %d\n"
			, i, demux[0].pids[i].count
			, demux[0].pids[i].discontinuity);
	}

}

int put_ts_packet(int no, unsigned char *packet, int sz)
{
	unsigned char *p;
	int transport_error_indicator, pid, payload_unit_start_indicator;
	int continuity_counter, last_continuity_counter;
	int i;
	if ((sz % 188)) {
		print_log(NULL, "L : %d", sz);
	} else {
		for (i = 0; i < sz; i += 188) {
			p = packet + i;

			pid = ((p[1] & 0x1f) << 8) + p[2];

			demux[no].ts_packet_c++;
			if (!is_sync(packet + i)) {
				print_log(NULL, "S     ");
				demux[no].sync_err++;
				if (0x80 == (p[1] & 0x80))
					demux[no].sync_err_set++;
				print_log(NULL, "0x%x, 0x%x, 0x%x, 0x%x\n"
					, *p, *(p+1),  *(p+2), *(p+3));
				continue;
			}

			transport_error_indicator = (p[1] & 0x80) >> 7;
			if (1 == transport_error_indicator) {
				demux[no].malformed_packet_c++;
				continue;
			}

			payload_unit_start_indicator = (p[1] & 0x40) >> 6;

			demux[no].pids[pid].count++;

			continuity_counter = p[3] & 0x0f;

			if (demux[no].pids[pid].continuity == -1) {
				demux[no].pids[pid].continuity
					= continuity_counter;
			} else {
				last_continuity_counter
					= demux[no].pids[pid].continuity;

				demux[no].pids[pid].continuity
					= continuity_counter;

				if (((last_continuity_counter + 1) & 0x0f)
					!= continuity_counter) {
					demux[no].pids[pid].discontinuity++;
				}
			}
		}
	}
	return 0;
}

void create_tspacket_anal()
{
	int n, i;

	for (n = 0; n < MAX_DEMUX; n++) {
		memset((void *)&demux[n], 0, sizeof(demux[n]));

		for (i = 0; i < 8192; i++)
			demux[n].pids[i].continuity = -1;
	}
}
#endif

enum ISDBT_MODE driver_mode = ISDBT_POWEROFF;
static DEFINE_MUTEX(ringbuffer_lock);
static DEFINE_MUTEX(power_onoff_lock);

static DECLARE_WAIT_QUEUE_HEAD(isdbt_isr_wait);
static u8 isdbt_isr_sig;
#ifndef USE_THREADED_IRQ
static struct task_struct *isdbt_kthread;
#endif

#ifdef USE_THREADED_IRQ
irqreturn_t isdbt_threaded_irq(int irq, void *dev_id)
{
	struct ISDBT_INIT_INFO_T *hInit = (struct ISDBT_INIT_INFO_T *)dev_id;

	mutex_lock(&power_onoff_lock);
	if (driver_mode == ISDBT_POWERON) {
		driver_mode = ISDBT_DATAREAD;
		bbm_com_isr(hInit);
		if (driver_mode == ISDBT_DATAREAD)
			driver_mode = ISDBT_POWERON;
	}
	mutex_unlock(&power_onoff_lock);

	return IRQ_HANDLED;
}
#else
irqreturn_t isdbt_irq(int irq, void *dev_id)
{
	isdbt_isr_sig = 1;
	wake_up_interruptible(&isdbt_isr_wait);
	return IRQ_HANDLED;
}
#endif

int data_callback(ulong hDevice, u8 bufid, u8 *data, int len)
{
	struct ISDBT_INIT_INFO_T *hInit;
	struct list_head *temp;
	hInit = (struct ISDBT_INIT_INFO_T *)hDevice;

	list_for_each(temp, &(hInit->hHead))
	{
		struct ISDBT_OPEN_INFO_T *hOpen;

		hOpen = list_entry(temp, struct ISDBT_OPEN_INFO_T, hList);

		if (hOpen->isdbttype == TS_TYPE) {
			mutex_lock(&ringbuffer_lock);
			if (fci_ringbuffer_free(&hOpen->RingBuffer) < len) {
				/*print_log(hDevice, "f"); */
				/* return 0 */;
				FCI_RINGBUFFER_SKIP(&hOpen->RingBuffer, len);
			}

			fci_ringbuffer_write(&hOpen->RingBuffer, data, len);

			wake_up_interruptible(&(hOpen->RingBuffer.queue));

			mutex_unlock(&ringbuffer_lock);

#ifdef FEATURE_TS_CHECK
			if (!(len%188)) {
				put_ts_packet(0, data, len);
				check_cnt_size += len;

				if (check_cnt_size > 188*320*40) {
					print_pkt_log();
					check_cnt_size = 0;
				}
			}
#endif
		}
	}

	return 0;
}

#ifndef USE_THREADED_IRQ
static int isdbt_thread(void *hDevice)
{
	struct ISDBT_INIT_INFO_T *hInit = (struct ISDBT_INIT_INFO_T *)hDevice;

	set_user_nice(current, -20);

	print_log(hInit, "isdbt_kthread enter\n");

	bbm_com_ts_callback_register((ulong)hInit, data_callback);

	while (1) {
		wait_event_interruptible(isdbt_isr_wait,
			isdbt_isr_sig || kthread_should_stop());
		isdbt_isr_sig = 0;
		mutex_lock(&power_onoff_lock);
		if (driver_mode == ISDBT_POWERON)
			bbm_com_isr(hInit);
		mutex_unlock(&power_onoff_lock);

		if (kthread_should_stop())
			break;
	}

	bbm_com_ts_callback_deregister();

	print_log(hInit, "isdbt_kthread exit\n");

	return 0;
}
#endif

void isdbt_set_drv_mode(u32 mode)
{
	mutex_lock(&power_onoff_lock);
	driver_mode = mode;
	mutex_unlock(&power_onoff_lock);
}

int isdbt_drv_open(struct inode *inode, struct file *filp)
{
	struct ISDBT_OPEN_INFO_T *hOpen;

	print_log(hInit, "isdbt open\n");

#ifdef FEATURE_GLOBAL_MEM
	hOpen = &hOpen_Val;
	hOpen->buf = &ringbuffer[0];
#else
	hOpen = kmalloc(sizeof(struct ISDBT_OPEN_INFO_T), GFP_KERNEL);
	hOpen->buf = kmalloc(RING_BUFFER_SIZE, GFP_KERNEL);
#endif
	/*kmalloc(RING_BUFFER_SIZE, GFP_KERNEL);*/
	hOpen->isdbttype = 0;
	if (list_empty(&(hInit->hHead)))
		list_add(&(hOpen->hList), &(hInit->hHead));

	hOpen->hInit = (HANDLE *)hInit;

	if (hOpen->buf == NULL) {
		print_log(hInit, "ring buffer malloc error\n");
		return -ENOMEM;
	}

	fci_ringbuffer_init(&hOpen->RingBuffer, hOpen->buf, RING_BUFFER_SIZE);

	filp->private_data = hOpen;

	return 0;
}

ssize_t isdbt_drv_read(struct file *filp
	, char *buf, size_t count, loff_t *f_pos)
{
	s32 avail;
	s32 non_blocking = filp->f_flags & O_NONBLOCK;
	struct ISDBT_OPEN_INFO_T *hOpen
		= (struct ISDBT_OPEN_INFO_T *)filp->private_data;
	struct fci_ringbuffer *cibuf = &hOpen->RingBuffer;
	ssize_t len, read_len = 0;

	if (!cibuf->data || !count)	{
		/*print_log(hInit, " return 0\n"); */
		return 0;
	}

	if (non_blocking && (fci_ringbuffer_empty(cibuf)))	{
		/*print_log(hInit, "return EWOULDBLOCK\n"); */
		return -EWOULDBLOCK;
	}

	if (wait_event_interruptible(cibuf->queue,
		!fci_ringbuffer_empty(cibuf))) {
		print_log(hInit, "%s return ERESTARTSYS\n", __func__);
		return -ERESTARTSYS;
	}

	mutex_lock(&ringbuffer_lock);

	avail = fci_ringbuffer_avail(cibuf);

	if (count >= avail)
		len = avail;
	else
		len = count - (count % 188);

	read_len = fci_ringbuffer_read_user(cibuf, buf, len);

	mutex_unlock(&ringbuffer_lock);

	return read_len;
}

int isdbt_drv_release(struct inode *inode, struct file *filp)
{
	struct ISDBT_OPEN_INFO_T *hOpen;

	print_log(hInit, "isdbt_release\n");

	hOpen = filp->private_data;

	hOpen->isdbttype = 0;
	if (!list_empty(&(hInit->hHead)))
		list_del(&(hOpen->hList));

#ifndef FEATURE_GLOBAL_MEM
	kfree(hOpen->buf);
	kfree(hOpen);
#endif

	return 0;
}

void isdbt_isr_check(HANDLE hDevice)
{
	u8 isr_time = 0;

	bbm_com_write(hDevice, DIV_BROADCAST, BBM_BUF_INT_ENABLE, 0x00);

	while (isr_time < 10) {
		if (!isdbt_isr_sig)
			break;

		msWait(10);
		isr_time++;
	}

}

long isdbt_drv_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	s32 res = BBM_NOK;
	s32 err = 0;
	s32 size = 0;
	struct ISDBT_OPEN_INFO_T *hOpen;

	struct ioctl_info info;

	if (_IOC_TYPE(cmd) != IOCTL_MAGIC)
		return -EINVAL;
	if (_IOC_NR(cmd) >= IOCTL_MAXNR)
		return -EINVAL;

	hOpen = filp->private_data;

	size = _IOC_SIZE(cmd);

	switch (cmd) {
	case IOCTL_ISDBT_RESET:
		res = bbm_com_reset(hInit, DIV_BROADCAST);
		print_log(hInit, "[FC8300] IOCTL_ISDBT_RESET \n");
		break;
	case IOCTL_ISDBT_INIT:
		pr_err("[FC8300] IOCTL_ISDBT_INIT\n");
		res = bbm_com_i2c_init(hInit, FCI_HPI_TYPE);
		if (res) {
			print_log(hInit
				, "FC8300 bbm_com_i2c_init Initialize Fail \n");
			break;
		}
		res |= bbm_com_probe(hInit, DIV_BROADCAST);
		if (res) {
			print_log(hInit
				, "FC8300  bbm_com_probe Initialize Fail \n");
			break;
		}
		pr_err("[FC8300] IOCTL_ISDBT_INIT bbm_com_probe success\n");
		res |= bbm_com_init(hInit, DIV_BROADCAST);
		res |= bbm_com_tuner_select(hInit
			, DIV_BROADCAST, FC8300_TUNER, ISDBT_13SEG);
		break;
	case IOCTL_ISDBT_BYTE_READ:
		pr_err("[FC8300] IOCTL_ISDBT_BYTE_READ\n");
		err = copy_from_user((void *)&info, (void *)arg, size);
		res = bbm_com_byte_read(hInit, DIV_BROADCAST, (u16)info.buff[0]
			, (u8 *)(&info.buff[1]));
		err |= copy_to_user((void *)arg, (void *)&info, size);
		break;
	case IOCTL_ISDBT_WORD_READ:
		err = copy_from_user((void *)&info, (void *)arg, size);
		res = bbm_com_word_read(hInit, DIV_BROADCAST, (u16)info.buff[0]
			, (u16 *)(&info.buff[1]));
		err |= copy_to_user((void *)arg, (void *)&info, size);
		break;
	case IOCTL_ISDBT_LONG_READ:
		err = copy_from_user((void *)&info, (void *)arg, size);
		res = bbm_com_long_read(hInit, DIV_BROADCAST, (u16)info.buff[0]
			, (u32 *)(&info.buff[1]));
		err |= copy_to_user((void *)arg, (void *)&info, size);
		break;
	case IOCTL_ISDBT_BULK_READ:
		err = copy_from_user((void *)&info, (void *)arg, size);
		res = bbm_com_bulk_read(hInit, DIV_BROADCAST, (u16)info.buff[0]
			, (u8 *)(&info.buff[2]), info.buff[1]);
		err |= copy_to_user((void *)arg, (void *)&info, size);
		break;
	case IOCTL_ISDBT_BYTE_WRITE:
		err = copy_from_user((void *)&info, (void *)arg, size);
		res = bbm_com_byte_write(hInit, DIV_BROADCAST, (u16)info.buff[0]
			, (u8)info.buff[1]);
		break;
	case IOCTL_ISDBT_WORD_WRITE:
		err = copy_from_user((void *)&info, (void *)arg, size);
		res = bbm_com_word_write(hInit, DIV_BROADCAST, (u16)info.buff[0]
			, (u16)info.buff[1]);
		break;
	case IOCTL_ISDBT_LONG_WRITE:
		err = copy_from_user((void *)&info, (void *)arg, size);
		res = bbm_com_long_write(hInit, DIV_BROADCAST, (u16)info.buff[0]
			, (u32)info.buff[1]);
		break;
	case IOCTL_ISDBT_BULK_WRITE:
		err = copy_from_user((void *)&info, (void *)arg, size);
		res = bbm_com_bulk_write(hInit, DIV_BROADCAST, (u16)info.buff[0]
			, (u8 *)(&info.buff[2]), info.buff[1]);
		break;
	case IOCTL_ISDBT_TUNER_READ:
		err = copy_from_user((void *)&info, (void *)arg, size);
		res = bbm_com_tuner_read(hInit, DIV_BROADCAST, (u8)info.buff[0]
			, (u8)info.buff[1],  (u8 *)(&info.buff[3])
			, (u8)info.buff[2]);
		err |= copy_to_user((void *)arg, (void *)&info, size);
		break;
	case IOCTL_ISDBT_TUNER_WRITE:
		err = copy_from_user((void *)&info, (void *)arg, size);
		res = bbm_com_tuner_write(hInit, DIV_BROADCAST, (u8)info.buff[0]
			, (u8)info.buff[1], (u8 *)(&info.buff[3])
			, (u8)info.buff[2]);
		break;
	case IOCTL_ISDBT_TUNER_SET_FREQ:
		{
			u32 f_rf;
			err = copy_from_user((void *)&info, (void *)arg, size);

			f_rf = (u32)info.buff[0] ;
			isdbt_isr_check(hInit);
			res = bbm_com_tuner_set_freq(hInit
				, DIV_MASTER, f_rf, 0x15);
			mutex_lock(&ringbuffer_lock);
			fci_ringbuffer_flush(&hOpen->RingBuffer);
			mutex_unlock(&ringbuffer_lock);
			bbm_com_write(hInit
				, DIV_BROADCAST, BBM_BUF_INT_ENABLE, 0x01);
		}
		break;
	case IOCTL_ISDBT_TUNER_SELECT:
		pr_err("[FC8300] IOCTL_ISDBT_TUNER_SELECT\n");
		err = copy_from_user((void *)&info, (void *)arg, size);
		res = bbm_com_tuner_select(hInit
			, DIV_BROADCAST, (u32)info.buff[0], (u32)info.buff[1]);
		print_log(hInit, "[FC8300] IOCTL_ISDBT_TUNER_SELECT \n");
		break;
	case IOCTL_ISDBT_TS_START:
#ifdef FEATURE_TS_CHECK
		create_tspacket_anal();
		check_cnt_size = 0;
#endif
		hOpen->isdbttype = TS_TYPE;
		res = BBM_OK;
		print_log(hInit, "[FC8300] IOCTL_ISDBT_TS_START \n");
		break;
	case IOCTL_ISDBT_TS_STOP:
		hOpen->isdbttype = 0;
		res = BBM_OK;
		print_log(hInit, "[FC8300] IOCTL_ISDBT_TS_STOP \n");
		break;
	case IOCTL_ISDBT_POWER_ON:
		res = BBM_OK;
		print_log(hInit, "[FC8300] IOCTL_ISDBT_POWER_ON \n");
		break;
	case IOCTL_ISDBT_POWER_OFF:
		mutex_lock(&power_onoff_lock);
		driver_mode = ISDBT_POWEROFF;
		mutex_unlock(&power_onoff_lock);
		res = BBM_OK;
		print_log(hInit, "[FC8300] IOCTL_ISDBT_POWER_OFF \n");
		break;
	case IOCTL_ISDBT_SCAN_STATUS:
		res = bbm_com_scan_status(hInit, DIV_BROADCAST);
		print_log(hInit
			, "[FC8300] IOCTL_ISDBT_SCAN_STATUS : %d\n", res);
		break;
	case IOCTL_ISDBT_TUNER_GET_RSSI:
		err = copy_from_user((void *)&info, (void *)arg, size);
		res = bbm_com_tuner_get_rssi(hInit
			, DIV_BROADCAST, (s32 *)&info.buff[0]);
		err |= copy_to_user((void *)arg, (void *)&info, size);
		break;
	default:
		print_log(hInit, "isdbt ioctl error!\n");
		res = BBM_NOK;
		break;
	}

	if (err < 0) {
		print_log(hInit, "copy to/from user fail : %d", err);
		res = BBM_NOK;
	}
	return res;
}

int isdbt_drv_probe(void)
{
	int res;

	print_log(hInit, "isdbt_drv_probe\n");

	hInit = kmalloc(sizeof(struct ISDBT_INIT_INFO_T), GFP_KERNEL);
	if (hInit == NULL) {
		print_log(hInit, "isdbt hInit malloc fail!\n");
		res = BBM_NOK;
		return res;
	}

#ifdef USE_THREADED_IRQ
	bbm_com_ts_callback_register((ulong)hInit, data_callback);
#endif

	res = bbm_com_hostif_select(hInit, BBM_SPI);
	if (res)
		print_log(hInit, "isdbt host interface select fail!\n");

#ifndef USE_THREADED_IRQ
	if (!isdbt_kthread)	{
		print_log(hInit, "kthread run\n");
		isdbt_kthread = kthread_run(isdbt_thread, (void*)hInit, "isdbt_thread");
	}
#endif

	INIT_LIST_HEAD(&(hInit->hHead));
#if defined(CONFIG_ISDBT_ANT_DET)
	wake_lock_init(&isdbt_ant_wlock, WAKE_LOCK_SUSPEND, "isdbt_ant_wlock");

	if (!isdbt_ant_det_reg_input(pdev))
		goto err_reg_input;
	if (!isdbt_ant_det_create_wq())
		goto free_reg_input;
	if (!isdbt_ant_det_irq_set(true))
		goto free_ant_det_wq;

	return 0;
free_ant_det_wq:
	isdbt_ant_det_destroy_wq();
free_reg_input:
	isdbt_ant_det_unreg_input();
err_reg_input:
	return -EFAULT;
#else
	return res;
#endif
}

int isdbt_drv_remove(void)
{
	print_log(hInit, "ISDBT remove\n");

	bbm_com_ts_callback_deregister();
	bbm_com_hostif_deselect(hInit);

	kfree(hInit);
	return 0;
}

int isdbt_drv_mmap(struct file *filp, struct vm_area_struct *vma)
{
	return 0;
}
