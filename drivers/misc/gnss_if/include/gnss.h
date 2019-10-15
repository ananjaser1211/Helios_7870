/*
 * Copyright (C) 2014 Samsung Electronics.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __GNSS_IF_H__
#define __GNSS_IF_H__

#include <linux/platform_device.h>
#include <linux/miscdevice.h>

/**
 * struct gnss_io_t - declaration for io_device
 * @name:	device name
 * @id:		for SIPC4, contains format & channel information
 *		(id & 11100000b)>>5 = format  (eg, 0=FMT, 1=RAW, 2=RFS)
 *		(id & 00011111b)    = channel (valid only if format is RAW)
 *		for SIPC5, contains only 8-bit channel ID
 * @format:	device format
 * @io_type:	type of this io_device
 * @links:	list of link_devices to use this io_device
 *		for example, if you want to use DPRAM and USB in an io_device.
 *		.links = LINKTYPE(LINKDEV_DPRAM) | LINKTYPE(LINKDEV_USB)
 * @tx_link:	when you use 2+ link_devices, set the link for TX.
 *		If define multiple link_devices in @links,
 *		you can receive data from them. But, cannot send data to all.
 *		TX is only one link_device.
 * @app:	the name of the application that will use this IO device
 *
 */
struct gnss_io_t {
	char *name;
	int   id;
	char *app;
};

#define STR_SHMEM_BASE		"shmem_base"

#define SHMEM_SIZE_1MB		(1 << 20)	/* 1 MB */
#define SHMEM_SIZE_2MB		(2 << 20)	/* 2 MB */
#define SHMEM_SIZE_4MB		(4 << 20)	/* 4 MB */

enum gnss_fault_info {
	INFO0,
	INFO1,
	INFO2,
	INFO3,
	INFO4,
	INFO5,
	INFO6,
	INFO7,
	INFO8,
	INFO9,
	FAULT_INFO_COUNT,
};

enum gnss_bcmd_ctrl {
	CTRL0,
	CTRL1,
	CTRL2,
	CTRL3,
	BCMD_CTRL_COUNT,
};

struct gnss_mbox {
	int int_ap2gnss_bcmd;
	int int_ap2gnss_req_fault_info;
	int int_ap2gnss_ipc_msg;
	int int_ap2gnss_ack_wake_set;
	int int_ap2gnss_ack_wake_clr;

	int irq_gnss2ap_bcmd;
	int irq_gnss2ap_rsp_fault_info;
	int irq_gnss2ap_ipc_msg;
	int irq_gnss2ap_req_wake_clr;

	unsigned reg_bcmd_ctrl[BCMD_CTRL_COUNT];
	unsigned reg_rx_ipc_msg;
	unsigned reg_tx_ipc_msg;
	unsigned reg_wake_lock;

	unsigned reg_rx_head;
	unsigned reg_rx_tail;
	unsigned reg_tx_head;
	unsigned reg_tx_tail;

	unsigned reg_fault_info[FAULT_INFO_COUNT];
};

struct gnss_pmu {
	int (*power)(int);
	int (*init)(void);
	int (*get_pwr_status)(void);
	int (*stop)(void);
	int (*start)(void);
	int (*clear_cp_fail)(void);
	int (*clear_cp_wdt)(void);
};

/* platform data */
struct gnss_data {
	char *name;
	char *device_node_name;

	int irq_gnss_active;
	int irq_gnss_wdt;
	int irq_gnss_wakeup;

	struct gnss_mbox *mbx;

	/* Information of IO devices */
	struct gnss_io_t *iodev;

	/* SHDMEM ADDR */
	u32 shmem_base;
	u32 shmem_size;
	u32 ipcmem_offset;
	u32 ipc_size;

	u8 __iomem *gnss_base;
	u8 __iomem *ipc_base;
};

struct shmem_conf {
	u32 shmem_base;
	u32 shmem_size;
};


#ifdef CONFIG_OF
#define gif_dt_read_enum(np, prop, dest) \
	do { \
		u32 val; \
		if (of_property_read_u32(np, prop, &val)) \
			return -EINVAL; \
		dest = (__typeof__(dest))(val); \
	} while (0)

#define gif_dt_read_bool(np, prop, dest) \
	do { \
		u32 val; \
		if (of_property_read_u32(np, prop, &val)) \
			return -EINVAL; \
		dest = val ? true : false; \
	} while (0)

#define gif_dt_read_string(np, prop, dest) \
	do { \
		if (of_property_read_string(np, prop, \
				(const char **)&dest)) \
		return -EINVAL; \
	} while (0)

#define gif_dt_read_u32(np, prop, dest) \
	do { \
		u32 val; \
		if (of_property_read_u32(np, prop, &val)) \
			return -EINVAL; \
				dest = val; \
	} while (0)
#define gif_dt_read_u32_array(np, prop, dest, sz) \
	do { \
		if (of_property_read_u32_array(np, prop, dest, (sz))) \
			return -EINVAL; \
	} while (0)
#endif

#define LOG_TAG	"gif: "
#define CALLEE	(__func__)
#define CALLER	(__builtin_return_address(0))

#define gif_err_limited(fmt, ...) \
	 printk_ratelimited(KERN_ERR "%s: " pr_fmt(fmt), __func__, ##__VA_ARGS__)
#define gif_err(fmt, ...) \
	pr_err(LOG_TAG "%s: " pr_fmt(fmt), __func__, ##__VA_ARGS__)
#define gif_debug(fmt, ...) \
	pr_debug(LOG_TAG "%s: " pr_fmt(fmt), __func__, ##__VA_ARGS__)
#define gif_info(fmt, ...) \
	pr_info(LOG_TAG "%s: " pr_fmt(fmt), __func__, ##__VA_ARGS__)
#define gif_trace(fmt, ...) \
	printk(KERN_DEBUG "gif: %s: %d: called(%pF): " fmt, \
		__func__, __LINE__, __builtin_return_address(0), ##__VA_ARGS__)

#endif
