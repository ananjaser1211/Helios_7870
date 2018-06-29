/*
*
* drivers/media/isdbt/isdbt.h
*
* isdbt driver
*
* Copyright (C) (2014, Samsung Electronics)
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

#ifndef __ISDBT_H__
#define __ISDBT_H__

#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/miscdevice.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/wait.h>
#include <linux/ioctl.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/atomic.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/wait.h>
#include <linux/list.h> 
#include <linux/jiffies.h>
#include <linux/wakelock.h>
#include <linux/kthread.h>
#include <linux/platform_device.h>
#if defined(CONFIG_MTV_BROADCOM)
#include <mach/chip_pinmux.h>
#include <mach/pinmux.h>
#endif

/* ================================
           Edit the lines below
================================= */

// If you use pinctrl (CONFIG_SEC_GPIO_SETTINGS),
// you have to define isdbt_gpio_active, isdbt_gpio_suspend in dtsi files !!!

//#ifdef CONFIG_OF_GPIO
#define ISDBT_DEVICE_TREE
//#endif
//#define ISDBT_USE_PMIC

#if defined(CONFIG_MTV_QUALCOMM) // check the following gpiochip base value. (cat /sys/class/gpio/gpiochipN/base)
#define ISDBT_GPIO_BASE        902
#elif defined(CONFIG_MTV_BROADCOM) // check this file (include/mach/chip_pinmux.h)
#define ISDBT_GPIO_BASE        PN_GPIO00
#else
#define ISDBT_GPIO_BASE        0 // If you want to use this value in another project, fix it.
#endif

#define convert_gpio(x)        (ISDBT_GPIO_BASE + x)
#define UNUSED_GPIO            -1 // If there is an unused gpio, use this definition.

#define ISDBT_PWR_EN           214
#define ISDBT_PWR_EN2          UNUSED_GPIO
#define ISDBT_RST              UNUSED_GPIO
#define ISDBT_INT              165
#define ISDBT_ANT_SEL          80
#define ISDBT_SPI_MOSI         68
#define ISDBT_SPI_MISO         69
#define ISDBT_SPI_CS           67
#define ISDBT_SPI_CLK          70

#ifdef ISDBT_USE_PMIC
#define ISDBT_PMIC_NAME "vddwifipa"
//#define ISDBT_PMIC_NAME "8226_l27"
#define ISDBT_PMIC_VOLTAGE	1800000
#endif

// =============================

/* Debug Msg Option */
#define ISDBT_DEBUG

#ifdef ISDBT_DEBUG
#define DPRINTK(x...) printk(KERN_ERR "ISDBT " x)
#else
#define DPRINTK(x...) /* null */
#endif

#define ISDBT_DEV_NAME	"isdbt"
#define ISDBT_DEV_MAJOR		225
#define ISDBT_DEV_MINOR		0

struct isdbt_dt_platform_data {
	int isdbt_irq;
	int isdbt_pwr_en;
	int isdbt_pwr_en2;
	int isdbt_rst;
	int isdbt_ant_sel;
	int isdbt_spi_mosi;
	int isdbt_spi_miso;
	int isdbt_spi_cs;
	int isdbt_spi_clk;
	int isdbt_ant_ctrl;
};

struct isdbt_drv_func {
	int (*probe) (void);
	int (*remove) (void);
	int (*power_on) (unsigned long arg);
	void (*power_off) (void);
	int (*open)(struct inode *inode, struct file *filp);
	int (*release)(struct inode *inode, struct file *filp);
	ssize_t (*read) (struct file *filp, char *buf, size_t count, loff_t *f_pos);
	long (*ioctl)(struct file *filp, unsigned int cmd, unsigned long arg);
	int (*mmap)(struct file *filp, struct vm_area_struct *vma);

	irqreturn_t (*irq_handler)(int irq, void *handle);
};

#if defined(CONFIG_MTV_FC8150)
struct isdbt_drv_func *fc8150_drv_func(void);
#elif defined(CONFIG_MTV_FC8300)
struct isdbt_drv_func *fc8300_drv_func(void);
#elif defined(CONFIG_MTV_MTV222)
struct isdbt_drv_func *mtv222_drv_func(void);
#elif defined(CONFIG_MTV_MTV23x)
struct isdbt_drv_func *mtv23x_drv_func(void);
#elif defined(CONFIG_MTV_SMS3230)
struct isdbt_drv_func *sms3230_drv_func(void);
#else
#error "an unsupported ISDBT driver"
#endif

#if defined(CONFIG_MTV_SPI)
struct spi_device *isdbt_get_spi_handle(void);
#endif

bool isdbt_control_irq(bool set);
void isdbt_control_gpio(bool poweron);

struct spi_device *isdbt_get_if_handle(void);

int isdbt_power_on(unsigned long arg);
bool isdbt_power_off(void);

int isdbt_spi_init(void);
void isdbt_spi_exit(void);
int isdbt_spi_probe(struct spi_device *spi);

#endif /* __ISDBT_H__*/

