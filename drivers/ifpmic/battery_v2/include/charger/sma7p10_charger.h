/*
 * sma7p10-charger.h - Header of SMA7P10 sub charger IC
 *
 * Copyright (C) 2016 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __SMA7P10_CHARGER_H__
#define __SMA7P10_CHARGER_H__

#include "../sec_charging_common.h"

#define SMA7P10_REGS_BASE	0
#define SMA7P10_REGS_SIZE	1024

#define MASK(width, shift)      (((0x1 << (width)) - 1) << shift) 

/*---------------------------------------- */
/* Registers */
/*---------------------------------------- */
#define SMA7P10_CHG_INT1			0x1
#define SMA7P10_CHG_INT2			0x2
#define SMA7P10_CHG_INT1_MASK		0x3
#define SMA7P10_CHG_INT2_MASK		0x4
#define SMA7P10_CHG_STATUS1			0x5
#define SMA7P10_CHG_STATUS2			0x6
#define SMA7P10_SC_CTRL0			0x4D
#define SMA7P10_SC_CTRL1			0x4E
#define SMA7P10_SC_CTRL5			0x50
#define SMA7P10_SC_CTRL6			0x51
#define SMA7P10_PMIC_ID				0x56

/* SMA7P10_CHG_STATUS1			0x5 */
#define VIN_OK_SHIFT 7
#define VIN_OK_STATUS_MASK		BIT(VIN_OK_SHIFT)
#define VINOV_OK_SHIFT 6
#define VINOV_OK_STATUS_MASK	BIT(VINOV_OK_SHIFT)
#define VINUV_OK_SHIFT 5
#define VINUV_OK_STATUS_MASK	BIT(VINUV_OK_SHIFT)
#define BATOV_OK_SHIFT 4
#define BATOV_OK_STATUS_MASK	BIT(BATOV_OK_SHIFT)
#define BATUV_OK_SHIFT 3
#define BATUV_OK_STATUS_MASK	BIT(BATUV_OK_SHIFT)

/* SMA7P10_CHG_STATUS2			0x6 */
#define MODE_STATUS_SHIFT			6
#define MODE_STATUS_MASK			BIT(MODE_STATUS_SHIFT)

/* SMA7P10_SC_CTRL0			0x4D */
#define CHG_CURRENT_SHIFT			7
#define CHG_CURRENT_MASK			BIT(CHG_CURRENT_SHIFT)

/* SMA7P10_SC_CTRL1			0x4E */
#define ENI2C_SHIFT				3
#define ENI2C_MASK				BIT(ENI2C_SHIFT)

/* SMA7P10_SC_CTRL5			0x50 */
#define CPQ_SHIFT				4
#define CPQ_WIDTH				2
#define CPQ_MASK				MASK(CPQ_WIDTH, CPQ_SHIFT)

#define V3P14					0
#define V3P04					1
#define V2P94					2
#define V2P84					3

#define CV_FLG_SHIFT				0
#define CV_FLG_WIDTH				4
#define CV_FLG_MASK				MASK(CV_FLG_WIDTH, CV_FLG_SHIFT)

/* SMA7P10_SC_CTRL6			0x51 */
#define SOFT_DOWN_SHIFT				1
#define SOFT_DOWN_WIDTH				3
#define	SOFT_DOWN_MASK				MASK(SOFT_DOWN_WIDTH, SOFT_DOWN_SHIFT)

/*---------------------------------------- */
/* Bit Fields */
/*---------------------------------------- */

struct sma7p10_charger_data {
	struct device           *dev;
	struct i2c_client       *i2c;
	struct mutex            io_lock;

	sec_charger_platform_data_t *pdata;

	struct power_supply	psy_chg;
	struct workqueue_struct *wqueue;
	struct delayed_work	isr_work;

	struct pinctrl *i2c_pinctrl;
	struct pinctrl_state *i2c_gpio_state_active;
	struct pinctrl_state *i2c_gpio_state_suspend;

	unsigned int siop_level;
	unsigned int chg_irq;
	unsigned int is_charging;
	unsigned int charging_type;
	unsigned int cable_type;
	unsigned int charging_current_max;
	unsigned int charging_current;

	u8 addr;
	int size;
};
#endif	/* __SMA7P10_CHARGER_H__ */
