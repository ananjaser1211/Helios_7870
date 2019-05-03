/*
 * sm5705-muic-afc.c - SM5705 AFC micro USB switch device driver
 *
 * Copyright (C) 2014 Samsung Electronics
 * Thomas Ryu <smilesr.ryu@samsung.com>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/host_notify.h>

#include <linux/muic/muic.h>
#include "muic_sm5705_afc.h"
#include "muic_i2c.h"

#if defined(CONFIG_MUIC_NOTIFIER)
#include <linux/muic/muic_notifier.h>
#endif /* CONFIG_MUIC_NOTIFIER */

#if defined(CONFIG_VBUS_NOTIFIER)
#include <linux/vbus_notifier.h>
#endif /* CONFIG_VBUS_NOTIFIER */

#if defined (CONFIG_OF)
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#endif /* CONFIG_OF */

/* SM5705 AFC CTRL register */
#define AFC_VBUS_READ_SHIFT    3
#define AFC_DM_RESET_SHIFT     2
#define AFC_DP_RESET_SHIFT     1
#define AFC_ENAFC_SHIFT        0

#define AFC_VBUS_READ_MASK    (1 << AFC_VBUS_READ_SHIFT)
#define AFC_DM_RESET_MASK     (1 << AFC_DM_RESET_SHIFT)
#define AFC_DP_RESET_MASK     (1 << AFC_DP_RESET_SHIFT)
#define AFC_ENAFC_MASK        (1 << AFC_ENAFC_SHIFT)
#define REG_AFCCNTL		0x18

int set_afc_ctrl_reg(muic_data_t *pmuic, int shift, bool on)
{
	struct i2c_client *i2c = pmuic->i2c;
	u8 reg_val;
	int ret = 0;

	ret = muic_i2c_read_byte(i2c, REG_AFCCNTL);
	if (ret < 0)
		printk(KERN_ERR "[muic] %s(%d)\n", __func__, ret);
	if (on)
		reg_val = ret | (0x1 << shift);
	else
		reg_val = ret & ~(0x1 << shift);

	if (reg_val ^ ret) {
		printk(KERN_DEBUG "[muic] %s reg_val(0x%x)!=AFC_CTRL reg(0x%x), update reg\n",
			__func__, reg_val, ret);

		ret = muic_i2c_write_byte(i2c, REG_AFCCNTL,
				reg_val);
		if (ret < 0)
			printk(KERN_ERR "[muic] %s err write AFC_CTRL(%d)\n",
					__func__, ret);
	} else {
		printk(KERN_DEBUG "[muic] %s (0x%x), just return\n",
				__func__, ret);
		return 0;
	}

	ret = muic_i2c_read_byte(i2c, REG_AFCCNTL);
	if (ret < 0)
		printk(KERN_ERR "[muic] %s err read AFC_CTRL(%d)\n",
			__func__, ret);
	else
		printk(KERN_DEBUG "[muic] %s AFC_CTRL reg after change(0x%x)\n",
			__func__, ret);

	return ret;
}

int set_afc_ctrl_enafc(muic_data_t *pmuic, bool on)
{
	int shift = AFC_ENAFC_SHIFT;
	int ret = 0;

	ret = set_afc_ctrl_reg(pmuic, shift, on);

	return ret;
}

int set_afc_vbus_read(muic_data_t *pmuic, bool on)
{
	int shift = AFC_VBUS_READ_SHIFT;
	int ret = 0;

	ret = set_afc_ctrl_reg(pmuic, shift, on);

	return ret;
}

int set_afc_dm_reset(muic_data_t *pmuic, bool on)
{
	int shift = AFC_DM_RESET_SHIFT;
	int ret = 0;

	ret = set_afc_ctrl_reg(pmuic, shift, on);

	return ret;
}

int set_afc_dp_reset(muic_data_t *pmuic, bool on)
{
	int shift = AFC_DP_RESET_SHIFT;
	int ret = 0;

	ret = set_afc_ctrl_reg(pmuic, shift, on);

	return ret;
}


