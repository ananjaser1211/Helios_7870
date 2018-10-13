/* dd.h
 *
 * Copyright (c) 2018 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

struct mdnie_info;
struct mdnie_table;
#if defined(CONFIG_DEBUG_FS) && !defined(CONFIG_SAMSUNG_PRODUCT_SHIP) && defined(CONFIG_SEC_GPIO_DVS) && defined(CONFIG_EXYNOS_DECON_MDNIE_LITE)
extern void mdnie_renew_table(struct mdnie_info *mdnie, struct mdnie_table *org);
extern int init_debugfs_mdnie(struct mdnie_info *md, unsigned int mdnie_no);
extern void mdnie_update(struct mdnie_info *mdnie);
#else
static inline void mdnie_renew_table(struct mdnie_info *mdnie, struct mdnie_table *org) {};
static inline void init_debugfs_mdnie(struct mdnie_info *md, unsigned int mdnie_no) {};
#endif

struct i2c_client;
struct backlight_device;
#if defined(CONFIG_DEBUG_FS) && !defined(CONFIG_SAMSUNG_PRODUCT_SHIP) && defined(CONFIG_SEC_GPIO_DVS)
extern int init_debugfs_backlight(struct backlight_device *bd, unsigned int *table, struct i2c_client **clients);
#else
static inline void init_debugfs_backlight(struct backlight_device *bd, unsigned int *table, struct i2c_client **clients) {};
#endif

struct dsim_device;
#if defined(CONFIG_DEBUG_FS) && !defined(CONFIG_SAMSUNG_PRODUCT_SHIP) && defined(CONFIG_SEC_GPIO_DVS)
extern void dsim_write_data_dump(struct dsim_device *dsim, u32 id, unsigned long d0, u32 d1);
#else
static inline void dsim_write_data_dump(struct dsim_device *dsim, u32 id, unsigned long d0, u32 d1) {};
#endif

