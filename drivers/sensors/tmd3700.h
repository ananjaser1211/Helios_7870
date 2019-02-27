/*
 * Copyright (C) 2010 Samsung Electronics. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#ifndef __LINUX_TAOS_H
#define __LINUX_TAOS_H

#include <linux/types.h>

#ifdef __KERNEL__
#define TAOS_OPT "taos-opt"
#define MIN 1
#define FEATURE_TMD3700 1


#define ENABLE                  0x80
#define ALS_TIME                0x81
#define PRX_RATE                0x82
#define WAIT_TIME               0x83
#define ALS_MINTHRESHLO         0x84
#define ALS_MINTHRESHHI         0x85
#define ALS_MAXTHRESHLO         0x86
#define ALS_MAXTHRESHHI         0x87
#define PRX_MINTHRESH           0x88
#define PRX_MAXTHRESH           0x8A
#define PPERS                   0x8C
#define PGCFG0                  0x8E
#define PGCFG1                  0x8F
#define CFG1                    0x90

#define REVID                   0x91
#define CHIPID                  0x92
#define STATUS                  0x93
#define CLR_CHAN0LO             0x94
#define CLR_CHAN0HI             0x95
#define RED_CHAN1LO             0x96
#define RED_CHAN1HI             0x97
#define GRN_CHAN1LO             0x98
#define GRN_CHAN1HI             0x99
#define BLU_CHAN1LO             0x9A
#define BLU_CHAN1HI             0x9B
#define PRX_DATA_HIGH           0x9C
#define PRX_DATA_LOW            0x9D
/*#define TEST_STATUS             0x1F*/

#define CFG2                    0x9F
#define CFG3                    0xAB
#define CFG4                    0xAC
#define CFG5                    0xAD
#define POFFSET_L               0xC0
#define POFFSET_H               0xC1
#define AZ_CONFIG               0xD6
#define CALIB                   0xD7
#define CALIBCFG                0xD9
#define CALIBSTAT               0xDC
#define INTENAB                 0xDD

#define FACTORYTRIM             0xE6
#define FACTORYTRIMSIGN         0xE7

enum intenab_reg {
	asien = (0x1 << 7),
	psien = (0x1 << 6),
	pien = (0x1 << 5),
	aien = (0x1 << 4),
	cien = (0x1 << 3),
};

enum status_reg {
	asat = (0x1 << 7),
	psat = (0x1 << 6),
	pint = (0x1 << 5),
	aint = (0x1 << 4),
	cint = (0x1 << 3),
	psat_reflective = (0x1 << 1),
	psat_ambient = (0x1 << 0),
};

enum enable_reg {
	wen = (0x1 << 3),
	pen = (0x1 << 2),
	aen = (0x1 << 1),
	pon = (0x1),
};

enum cfg3_reg {
	int_read_clear = (0x1 << 7),
};

enum cmd_reg {
	CMD_REG = (0x1 << 7),
	CMD_INCR = (0x1 << 5),
	CMD_SPL_FN = (0x3 << 5),
	CMD_PROX_INT_CLR = (0x5 << 0),
	CMD_ALS_INT_CLR = (0x6 << 0),
};

/*TMD3782 cmd reg masks*/
#define CMD_BYTE_RW             0x00
#define CMD_WORD_BLK_RW         0x20
#define CMD_PROX_INTCLR         0x05
#define CMD_ALS_INTCLR          0x06
#define CMD_PROXALS_INTCLR      0x80

#define P_TIME_US(p)   ((((p) / 88) - 1.0) + 0.5)
#define PRX_PERSIST(p) (((p) & 0xf) << 4)
#define ALS_PERSIST(p) (((p) & 0xf) << 0)


#define CMD_TST_REG             0x08
#define CMD_USER_REG            0x09

/* TMD3782 cntrl reg masks */
#define CNTL_REG_CLEAR          0x00
#define CNTL_PROX_INT_ENBL      0x20
#define CNTL_ALS_INT_ENBL       0x10
#define CNTL_WAIT_TMR_ENBL      0x08
#define CNTL_PROX_DET_ENBL      0x04
#define CNTL_ADC_ENBL           0x02
#define CNTL_PWRON              0x01
#define CNTL_ALSPON_ENBL        0x03
#define CNTL_INTALSPON_ENBL     0x13
#define CNTL_PROXPON_ENBL       0x0F
#define CNTL_INTPROXPON_ENBL    0x2F

/* TMD3782 status reg masks */
#define STA_ADCVALID            0x01
#define STA_PRXVALID            0x02
#define STA_ADC_PRX_VALID       0x03
#define STA_ADCINTR             0x10
#define STA_PRXINTR             0x20

#ifdef CONFIG_PROX_WINDOW_TYPE
#define WINTYPE_OTHERS          '0'
#define WINTYPE_WHITE           '2'
#define WHITEWINDOW_HI_THRESHOLD   720
#define WHITEWINDOW_LOW_THRESHOLD  590
#define BLACKWINDOW_HI_THRESHOLD   650
#define BLACKWINDOW_LOW_THRESHOLD  520
#endif

enum taos_op_modes {
	mode_off = 0x00,
	mode_als = 0x01, /* ALS */
	mode_prox = 0x02, /* Proximity */
	mode_als_prox = 0x03, /* ALS + Proximity */
};

struct taos_platform_data {
	int als_int;
	int enable;
	void (*power)(bool);
	int (*light_adc_value)(void);
	void (*led_on)(bool);
	int prox_thresh_hi;
	int prox_thresh_low;
	int prox_th_hi_cal;
	int prox_th_low_cal;
	int als_time;
	int intr_filter;
	int prox_pulsecnt;
	int als_gain;
	int dgf;
	int cct_coef;
	int cct_offset;
	int coef_r;
	int coef_g;
	int coef_b;
	int min_max;
	int prox_rawdata_trim;
	int crosstalk_max_offset;
	int thresholed_max_offset;
	int lux_multiple;
	int vled_ldo;

};
#endif /*__KERNEL__*/
#endif
