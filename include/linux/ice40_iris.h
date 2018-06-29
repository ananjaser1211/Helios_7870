/*
 * Copyright (C) 2012 Samsung Electronics
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
 */

#ifndef _ICE40_IRIS_H_
#define _ICE40_IRIS_H_

struct ice40_iris_platform_data {
	int fw_ver;
	int spi_clk;
	int spi_si;
	int cresetb;
	int rst_n;
	int cdone;
#ifdef CONFIG_LEDS_IRIS_IRLED_SUPPORT
	int led_tz;
#endif
	int fpga_clk;
};

#define SEC_FPGA_MAX_FW_PATH    255
#define SEC_FPGA_FW_FILENAME    "ICE40XX_FPGA.bin"

#define SNPRINT_BUF_SIZE	255
#define FIRMWARE_MAX_RETRY	2

#define FPGA_ENABLE			1
#define FPGA_DISABLE		0
#define GPIO_LEVEL_HIGH		1
#define GPIO_LEVEL_LOW		0
#define GPIO_CON_INPUT		0
#define GPIO_CON_OUTPUT		1
#define GPIO_CON_CLKOUT		2
#define FPGA_TD_TP_MAX_TIME	1023 /* 0~102.3ms (0.1ms, 10bit)*/

#endif /* _ICE40_IRIS_H_ */
