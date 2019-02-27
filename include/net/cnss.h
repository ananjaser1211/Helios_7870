/* Copyright (c) 2013-2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef _CNSS_H_
#define _CNSS_H_

/* platform capabilities */
enum cnss_platform_cap_flag {
	CNSS_HAS_EXTERNAL_SWREG = 0x01,
	CNSS_HAS_UART_ACCESS = 0x02,
};

struct cnss_platform_cap {
	u32 cap_flag;
};

/* WLAN driver status */
enum cnss_driver_status {
	CNSS_UNINITIALIZED,
	CNSS_INITIALIZED,
	CNSS_LOAD_UNLOAD
};

#if 1 /* 20160327 AI 4 2 */
extern int cnss_wlan_oob_shutdown(void);
extern int cnss_wlan_check_hang(void);
#endif /* 20160327 AI 4 2 */
extern int cnss_wlan_register_driver(void);
extern void cnss_wlan_unregister_driver(void);
extern void cnss_wlan_force_ldo_reset(void);
extern void cnss_set_driver_status(enum cnss_driver_status driver_status);

typedef int (*oob_irq_handler_t)(void* dev_para);
extern int cnss_wlan_register_oob_irq_handler(oob_irq_handler_t handler, void* pm_oob);
extern int cnss_wlan_unregister_oob_irq_handler(void *pm_oob);
extern int cnss_wlan_query_oob_status(void);
extern int cnss_wlan_get_pending_irq(void);
extern int cnss_update_boarddata(unsigned char *buf, unsigned int len);
extern int cnss_cache_boarddata(const unsigned char *buf, unsigned int len, unsigned int offset);
extern void cnss_get_monotonic_boottime(struct timespec *ts);
extern void cnss_wlan_irq_poll(void);

#endif /* _CNSS_H_ */
