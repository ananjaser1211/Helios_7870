/*
 *
 * drivers/media/tdmb/isdbt_port_sms3230.c
 *
 * isdbt driver wrapper for Siano SMS3230
 *
 * Copyright (C) (2015, Samsung Electronics)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/spi/spi.h>

#include "isdbt.h"
#include "isdbt_port_sms3230.h"
//#define ISDBT_POWER_NON_CONTROL
struct isdbt_drv_func sms3230_drv_func_struct;

extern void sms3230_set_port_if(unsigned long interface);


#ifdef ISDBT_POWER_NON_CONTROL

void sms_chip_poweron(int id)
{
	pr_debug("%s %d id:%d\n", __func__, __LINE__, id);
	isdbt_power_on(0);
	msleep(50);
}

void sms_chip_poweroff(int id)
{
	pr_debug("%s %d id:%d\n", __func__, __LINE__, id);
	isdbt_power_off();
}

#endif


int probe_drv(void)
{
	sms3230_set_port_if((unsigned long)isdbt_get_if_handle());
	return smscore_module_init();
}

int remove_drv(void)
{
/*
	smscore_module_exit();
	isdbt_control_gpio(false);
*/
	return 0;
}

int hw_init_drv(unsigned long arg)
{
	isdbt_control_gpio(true);
//	isdbt_control_irq(true);
	return arg;
}

void hw_deinit_drv(void)
{
	isdbt_control_gpio(false);
//	isdbt_control_irq(false);
}

extern int smsspi_request_connect(void);
extern int smsspi_plug_notify(int plugin);
int open_drv(struct inode *inode, struct file *filp)
{
	int res = 0;

//	res = isdbt_drv_open(inode, filp);

	return res;
}

ssize_t read_drv(struct file *filp
	, char *buf, size_t count, loff_t *f_pos)
{
	ssize_t size = 0;

//	size = isdbt_drv_read(filp, buf, count, f_pos);

	return size;
}

long ioctl_drv(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int res = 0;

	pr_info("%s %d [sms3230] cmd(0x%x)\n", __func__, __LINE__, cmd);	

//	res = isdbt_drv_ioctl(filp, cmd, arg);

	if (res == 0) {
		switch (cmd) {
			case IOCTL_ISDBT_POWER_ON:
#ifndef ISDBT_POWER_NON_CONTROL
				pr_info("%s %d [sms3230] IOCTL_ISDBT_POWER_ON\n", __func__, __LINE__);
				isdbt_power_on(0);
				msleep(100);
				smsspi_plug_notify(1);
#endif
				break;
			case IOCTL_ISDBT_POWER_OFF:
#ifndef ISDBT_POWER_NON_CONTROL
				pr_info("%s %d [sms3230] IOCTL_ISDBT_POWER_OFF\n", __func__, __LINE__);
				smsspi_plug_notify(0);
				msleep(100);
				isdbt_power_off();
#endif
				break;
			case IOCTL_ISDBT_RESET:
#ifndef ISDBT_POWER_NON_CONTROL
				pr_info("%s %d [sms3230] IOCTL_ISDBT_RESET - replace it with OFF for test now..\n", __func__, __LINE__);
				smsspi_plug_notify(0);
				msleep(100);
				isdbt_power_off();
#if 0
				msleep(500);
				isdbt_power_on(0);
				msleep(100);
				smsspi_plug_notify(1);
#endif
#endif
				break;
			default:
				break;
		}
	}

	return res;
}

int mmap_drv(struct file *filp, struct vm_area_struct *vma)
{
//	return isdbt_drv_mmap(filp, vma);
	return 0;
}

irqreturn_t irq_drv(int irq, void *dev_id)
{
/*
#ifdef USE_THREADED_IRQ
	return isdbt_threaded_irq(irq, dev_id);
#else
	return isdbt_irq(irq, dev_id);
#endif
*/
	return 0;
}
int release_drv(struct inode *inode, struct file *filp)
{
#ifndef ISDBT_POWER_NON_CONTROL
//	return isdbt_drv_release(inode, filp);
	smsspi_plug_notify(0);
	msleep(100);
	isdbt_power_off();
#endif
	return 0;
}

struct isdbt_drv_func *sms3230_drv_func(void)
{
	sms3230_drv_func_struct.probe = probe_drv;
	sms3230_drv_func_struct.remove = remove_drv;
	sms3230_drv_func_struct.power_on = hw_init_drv;
	sms3230_drv_func_struct.power_off = hw_deinit_drv;
	sms3230_drv_func_struct.open = open_drv;
	sms3230_drv_func_struct.read = read_drv;
	sms3230_drv_func_struct.ioctl = ioctl_drv;
	sms3230_drv_func_struct.mmap = mmap_drv;
	sms3230_drv_func_struct.irq_handler = irq_drv;
	sms3230_drv_func_struct.release = release_drv;

	return &sms3230_drv_func_struct;
}

