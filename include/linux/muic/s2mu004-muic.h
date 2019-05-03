/*
 * Copyright (C) 2015 Samsung Electronics
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

#if defined(CONFIG_IFPMIC_SUPPORT)
#include <linux/ifpmic/muic/s2mu004-muic.h>
#endif

#ifndef __S2MU004_MUIC_H__
#define __S2MU004_MUIC_H__

//#define CONFIG_HV_MUIC_S2MU004_AFC true /* afc functuin enable */

#include <linux/muic/muic.h>
#if defined(CONFIG_HV_MUIC_S2MU004_AFC)
#include <linux/muic/s2mu004-muic-hv-typedef.h>
#endif /* CONFIG_HV_MUIC_S2MU004_AFC */
#if defined(CONFIG_CCIC_S2MU004)
#include <linux/time.h>
#include <linux/ktime.h>
#include <linux/rtc.h>
#endif
#define MUIC_DEV_NAME	"muic-s2mu004"

#define MAX_BCD_RESCAN_CNT 		5

/* s2mu004 muic register read/write related information defines. */

/* S2MU004 Control register */
#define CTRL_SWITCH_OPEN_SHIFT	4
#define CTRL_RAW_DATA_SHIFT		3
#define CTRL_MANUAL_SW_SHIFT	2
#define CTRL_WAIT_SHIFT			1
#define CTRL_INT_MASK_SHIFT		0

#define CTRL_SWITCH_OPEN_MASK	(0x1 << CTRL_SWITCH_OPEN_SHIFT)
#define CTRL_RAW_DATA_MASK		(0x1 << CTRL_RAW_DATA_SHIFT)
#define CTRL_MANUAL_SW_MASK		(0x1 << CTRL_MANUAL_SW_SHIFT)
#define CTRL_WAIT_MASK			(0x1 << CTRL_WAIT_SHIFT)
#define CTRL_INT_MASK_MASK		(0x1 << CTRL_INT_MASK_SHIFT)

#ifdef CONFIG_MUIC_S2MU004_ENABLE_AUTOSW
#define CTRL_MASK			(CTRL_SWITCH_OPEN_MASK | \
						CTRL_MANUAL_SW_MASK | CTRL_WAIT_MASK | \
						CTRL_INT_MASK_MASK)
#else
#define CTRL_MASK			(CTRL_SWITCH_OPEN_MASK | \
						CTRL_WAIT_MASK | CTRL_INT_MASK_MASK)
#endif

/* S2MU004 MUIC Interrupt 1 register */
#define INT_RID_CHG_SHIFT		5
#define INT_LKR_SHIFT			4
#define INT_LKP_SHIFT			3
#define INT_KP_SHIFT			2
#define INT_DETACH_SHIFT		1
#define INT_ATTACH_SHIFT		0

#define INT_RID_CHG_MASK		(0x1 << INT_RID_CHG_SHIFT)
#define INT_LKR_MASK			(0x1 << INT_LKR_SHIFT)
#define INT_LKP_MASK			(0x1 << INT_LKP_SHIFT)
#define INT_KP_MASK				(0x1 << INT_KP_SHIFT)
#define INT_DETACH_MASK			(0x1 << INT_DETACH_SHIFT)
#define INT_ATTACH_MASK			(0x1 << INT_ATTACH_SHIFT)

/* S2MU004 MUIC Interrupt 2 register */
#define INT_ADC_CHANGE_SHIFT	2
#define INT_RSRV_ATTACH_SHIFT	1
#define INT_CHG_DET_SHIFT		0

#define INT_ADC_CHANGE_MASK		(0x1 << INT_ADC_CHANGE_SHIFT)
#define INT_RSRV_ATTACH_MASK	(0x1 << INT_RSRV_ATTACH_SHIFT)
#define INT_VBUS_ON_MASK		(0x1 << INT_CHG_DET_SHIFT)

/* S2MU004 MUIC Interrupt Maksing for pdic */
#define INT_PDIC_MASK1		(0xFC)
#define INT_PDIC_MASK2		(0x7A)

/* S2MU004 ADC register */
#define ADC_MASK				(0x1f)
#define ADC_CONVERSION_ERR_MASK	(0x1 << 7)

/* S2MU004 Timing Set 1 & 2 register Timing table */
#define KEY_PRESS_TIME_100MS		(0x00)
#define KEY_PRESS_TIME_200MS		(0x10)
#define KEY_PRESS_TIME_300MS		(0x20)
#define KEY_PRESS_TIME_700MS		(0x60)

#define LONGKEY_PRESS_TIME_300MS	(0x00)
#define LONGKEY_PRESS_TIME_500MS	(0x02)
#define LONGKEY_PRESS_TIME_1000MS	(0x07)
#define LONGKEY_PRESS_TIME_1500MS	(0x0C)

#define SWITCHING_WAIT_TIME_10MS	(0x00)
#define SWITCHING_WAIT_TIME_210MS	(0xa0)

/* S2MU004 MUIC Device Type 1 register */
#define DEV_TYPE1_USB_OTG		(0x1 << 7)
#define DEV_TYPE1_DEDICATED_CHG	(0x1 << 6)
#define DEV_TYPE1_CDP			(0x1 << 5)
#define DEV_TYPE1_T1_T2_CHG		(0x1 << 4)
#define DEV_TYPE1_UART			(0x1 << 3)
#define DEV_TYPE1_USB			(0x1 << 2)
#define DEV_TYPE1_AUDIO_2		(0x1 << 1)
#define DEV_TYPE1_AUDIO_1		(0x1 << 0)
#define DEV_TYPE1_USB_TYPES		(DEV_TYPE1_USB_OTG | DEV_TYPE1_CDP | DEV_TYPE1_USB)
#define DEV_TYPE1_CHG_TYPES		(DEV_TYPE1_DEDICATED_CHG | DEV_TYPE1_CDP)

/* S2MU004 MUIC Device Type 2 register */
#define DEV_TYPE2_SDP_1P8S		(0x1 << 7)
#define DEV_TYPE2_AV			(0x1 << 6)
#define DEV_TYPE2_TTY			(0x1 << 5)
#define DEV_TYPE2_PPD			(0x1 << 4)
#define DEV_TYPE2_JIG_UART_OFF		(0x1 << 3)
#define DEV_TYPE2_JIG_UART_ON		(0x1 << 2)
#define DEV_TYPE2_JIG_USB_OFF		(0x1 << 1)
#define DEV_TYPE2_JIG_USB_ON		(0x1 << 0)
#define DEV_TYPE2_JIG_USB_TYPES		(DEV_TYPE2_JIG_USB_OFF | DEV_TYPE2_JIG_USB_ON)
#define DEV_TYPE2_JIG_UART_TYPES	(DEV_TYPE2_JIG_UART_OFF)
#define DEV_TYPE2_JIG_TYPES		(DEV_TYPE2_JIG_UART_TYPES | DEV_TYPE2_JIG_USB_TYPES)

/* S2MU004 MUIC Device Type 3 register */
#define DEV_TYPE3_U200_CHG		(0x1 << 7)
#define DEV_TYPE3_AV_WITH_VBUS	(0x1 << 4)
#define DEV_TYPE3_VBUS_R255		(0x1 << 1)
#define DEV_TYPE3_MHL			(0x1 << 0)
#define DEV_TYPE3_CHG_TYPE		(DEV_TYPE3_U200_CHG | DEV_TYPE3_VBUS_R255)

/* S2MU004 MUIC APPLE Device Type register */
#define DEV_TYPE_APPLE_APPLE0P5A_CHG	(0x1 << 7)
#define DEV_TYPE_APPLE_APPLE1A_CHG		(0x1 << 6)
#define DEV_TYPE_APPLE_APPLE2A_CHG		(0x1 << 5)
#define DEV_TYPE_APPLE_APPLE2P4A_CHG	(0x1 << 4)
#define DEV_TYPE_APPLE_SDP_DCD_OUT		(0x1 << 3)
#define DEV_TYPE_APPLE_RID_WAKEUP		(0x1 << 2)
#define DEV_TYPE_APPLE_VBUS_WAKEUP		(0x1 << 1)
#define DEV_TYPE_APPLE_BCV1P2_OR_OPEN	(0x1 << 0)

/* S2MU004 MUIC CHG Type register */
#define CHG_TYPE_VBUS_R255	(0x1 << 7)
#define DEV_TYPE_U200		(0x1 << 4)
#define DEV_TYPE_SDP_1P8S	(0x1 << 3)
#define DEV_TYPE_USB		(0x1 << 2)
#define DEV_TYPE_CDPCHG		(0x1 << 1)
#define DEV_TYPE_DCPCHG		(0x1 << 0)
#define DEV_TYPE_CHG_TYPE		(DEV_TYPE_U200 | DEV_TYPE_SDP_1P8S)

/* S2MU004_REG_MUIC_BCD_RESCAN */
#define BCD_RESCAN_MASK 0x1

/* S2MU004_REG_MUIC_RID_CTRL */
#define RID_CTRL_ADC_OFF_SHIFT	1
#define RID_CTRL_ADC_OFF_MASK	0x1 << RID_CTRL_ADC_OFF_SHIFT
/*
 * Manual Switch
 * D- [7:5] / D+ [4:2] / CHARGER[1] / OTGEN[0]
 * 000: Open all / 001: USB / 010: AUDIO / 011: UART / 100: V_AUDIO
 * 00: Vbus to Open / 01: Vbus to Charger / 10: Vbus to MIC / 11: Vbus to VBout
 */

#define MANUAL_SW_JIG_EN		(0x1 << 0)

#define MANUAL_SW_DM_SHIFT		5
#define MANUAL_SW_DP_SHIFT		2
#define MANUAL_SW_CHG_SHIFT		1
#define MANUAL_SW_DM_DP_MASK	0xFC

#define MANUAL_SW_OPEN			(0x0)
#define MANUAL_SW_USB			(0x1 << MANUAL_SW_DM_SHIFT | 0x1 << MANUAL_SW_DP_SHIFT)
#define MANUAL_SW_UART		(0x2 << MANUAL_SW_DM_SHIFT | 0x2 << MANUAL_SW_DP_SHIFT)
#define MANUAL_SW_UART2			(0x3 << MANUAL_SW_DM_SHIFT | 0x3 << MANUAL_SW_DP_SHIFT)
#define MANUAL_SW_AUDIO		(0x0 << MANUAL_SW_DM_SHIFT | 0x0 << MANUAL_SW_DP_SHIFT) /* Not Used */

#define MANUAL_SW_OTGEN		(0x1)
#define MANUAL_SW_CHARGER	(0x1 << MANUAL_SW_CHG_SHIFT)

#define WATER_DET_RETRY_CNT				10
#define WATER_CCIC_WAIT_DURATION_MS		4000
#define WATER_DRY_RETRY_INTERVAL_SEC	600
#define WATER_DRY_RETRY_30MIN_SEC		1800
#define WATER_DRY_RETRY_60MIN_SEC		6000
#define WATER_DRY_RETRY_INTERVAL_MS		((WATER_DRY_RETRY_30MIN_SEC) * (1000))
#define WATER_DRY_INTERVAL_MS			10000
#define WATER_DET_STABLE_DURATION_MS	2000
#define DRY_DET_RETRY_CNT_MAX			3
#define RID_REFRESH_DURATION_MS			100
#define WATER_TOGGLE_WA_DURATION_US		20000

/* s2mu004-muic macros */
#define REQUEST_IRQ(_irq, _dev_id, _name)				\
do {									\
	ret = request_threaded_irq(_irq, NULL, s2mu004_muic_irq_thread,	\
				0, _name, _dev_id);	\
	if (ret < 0) {							\
		pr_err("%s:%s Failed to request IRQ #%d: %d\n",		\
				MUIC_DEV_NAME, __func__, _irq, ret);	\
		_irq = 0;						\
	}								\
} while (0)

#define FREE_IRQ(_irq, _dev_id, _name)					\
do {									\
	if (_irq) {							\
		free_irq(_irq, _dev_id);				\
		pr_info("%s:%s IRQ(%d):%s free done\n", MUIC_DEV_NAME,	\
				__func__, _irq, _name);			\
	}								\
} while (0)

#define IS_WATER_ADC(adc)\
		( ((adc) > (ADC_GND)) && ((adc) < (ADC_OPEN)) \
		? 1 : 0 )
#define IS_AUDIO_ADC(adc)\
		( ((adc) >= (ADC_SEND_END)) && ((adc) <= (ADC_REMOTE_S12)) \
		? 1 : 0 )
#define IS_ACC_ADC(adc)\
		( ((adc) >= (ADC_RESERVED_VZW)) \
		&& ((adc) <= (ADC_AUDIOMODE_W_REMOTE)) \
		? 1 : 0 )
#define IS_WATER_STATUS(x)\
		( ((x) == (S2MU004_WATER_MUIC_CCIC_DET)) \
		|| ((x) == (S2MU004_WATER_MUIC_CCIC_STABLE)) \
		? 1 : 0 )

/* end of macros */

/* S2MU004_REG_LDOADC_VSETH register */
#define LDOADC_VSETH_MASK	0x1F
#define LDOADC_VSETL_MASK	0x1F
#define LDOADC_VSET_3V		0x1F
#define LDOADC_VSET_2_7V	0x1C
#define LDOADC_VSET_2_6V	0x0E
#define LDOADC_VSET_2_4V	0x0C
#define LDOADC_VSET_2_2V	0x0A
#define LDOADC_VSET_2_0V	0x08
#define LDOADC_VSET_1_8V	0x06
#define LDOADC_VSET_1_7V	0x05
#define LDOADC_VSET_1_6V	0x04
#define LDOADC_VSET_1_5V	0x03
#define LDOADC_VSET_1_4V	0x02
#define LDOADC_VSET_1_2V	0x00
#define LDOADC_VSETH_WAKE_HYS_SHIFT	6
#define LDOADC_VSETH_WAKE_HYS_MASK	0x1 << LDOADC_VSETH_WAKE_HYS_SHIFT

enum s2mu004_reg_manual_sw_value {
	MANSW_OPEN		=	(MANUAL_SW_OPEN),
	MANSW_OPEN_WITH_VBUS	=	(MANUAL_SW_CHARGER),
	MANSW_USB		=	(MANUAL_SW_USB | MANUAL_SW_CHARGER),
	MANSW_AUDIO	=	(MANUAL_SW_AUDIO | MANUAL_SW_CHARGER), /* Not Used */
	MANSW_OTG		=	(MANUAL_SW_USB | MANUAL_SW_OTGEN),
	MANSW_UART		=	(MANUAL_SW_UART | MANUAL_SW_CHARGER),
	MANSW_OPEN_RUSTPROOF	=	(MANUAL_SW_OPEN | MANUAL_SW_CHARGER),
};

enum s2mu004_muic_mode {
	S2MU004_NONE_CABLE,
	S2MU004_FIRST_ATTACH,
	S2MU004_SECOND_ATTACH,
	S2MU004_MUIC_DETACH,
	S2MU004_MUIC_OTG,
	S2MU004_MUIC_JIG,
};

enum s2mu004_muic_adc_mode {
	S2MU004_ADC_ONESHOT,
	S2MU004_ADC_PERIODIC,
};

typedef enum {
	S2MU004_WATER_MUIC_IDLE,
	S2MU004_WATER_MUIC_DET,
	S2MU004_WATER_MUIC_CCIC_DET,
	S2MU004_WATER_MUIC_CCIC_STABLE,
	S2MU004_WATER_MUIC_CCIC_INVALID,
}t_water_status;

typedef enum {
	S2MU004_WATER_DRY_MUIC_IDLE,
	S2MU004_WATER_DRY_MUIC_DET,
	S2MU004_WATER_DRY_MUIC_CCIC_DET,
	S2MU004_WATER_DRY_MUIC_CCIC_INVALID,
}t_water_dry_status;

/* muic chip specific internal data structure
 * that setted at muic-xxxx.c file
 */
struct s2mu004_muic_data {
	struct device *dev;
	struct i2c_client *i2c; /* i2c addr: 0x7A; MUIC */
	struct mutex muic_mutex;
	struct mutex afc_mutex;
	struct mutex switch_mutex;
#if defined(CONFIG_CCIC_S2MU004)
	struct mutex water_det_mutex;
	struct mutex water_dry_mutex;
#endif
	struct s2mu004_dev *s2mu004_dev;
#if defined(CONFIG_CCIC_S2MU004)
	wait_queue_head_t wait;
#endif
	/* model dependant mfd platform data */
	struct s2mu004_platform_data	*mfd_pdata;

	void *ccic_data;

	int irq_attach;
	int irq_detach;
	int irq_rid_chg;
	int irq_vbus_on;
	int irq_rsvd_attach;
	int irq_adc_change;
	int irq_av_charge;
	int irq_vbus_off;
	int temp;
	bool jig_state;
	struct delayed_work muic_pdic_work;
	int re_detect;
	bool afc_check;
#if defined(CONFIG_HV_MUIC_S2MU004_AFC)
	int irq_dnres;
	int irq_mrxrdy;
	int irq_mpnack;
	int irq_vbadc;
	int irq_vdnmon;
#endif
	int retry_cnt;
	int retry_qc_cnt;
	int qc_prepare;
	/* muic common callback driver internal data */
	struct sec_switch_data *switch_data;

	/* model dependant muic platform data */
	struct muic_platform_data *pdata;

	struct wake_lock wake_lock;
#if defined(CONFIG_CCIC_S2MU004)
	struct wake_lock water_wake_lock;
	struct wake_lock water_dry_wake_lock;
#endif
	/* muic support vps list */
	bool muic_support_list[ATTACHED_DEV_NUM];

	/* muic current attached device */
	muic_attached_dev_t	attached_dev;

	/* muic Device ID */
	u8 muic_vendor;			/* Vendor ID */
	u8 muic_version;		/* Version ID */
	u8 ic_rev_id;			/* Rev ID */

	bool	is_usb_ready;
	bool	is_factory_start;
	bool	is_rustproof;
	bool	is_otg_test;
	bool	check_adc;

#if !defined(CONFIG_MUIC_S2MU004_ENABLE_AUTOSW)
	bool	is_jig_on;
#endif

	/* W/A waiting for the charger ic */
	bool suspended;
	bool need_to_noti;

	struct workqueue_struct *muic_wqueue;
	struct delayed_work afc_check_vbadc;
	struct delayed_work afc_cable_type_work;
	struct workqueue_struct *cable_type_wq;
	struct delayed_work afc_irq_detect;
	struct delayed_work afc_send_mpnack;
	struct delayed_work afc_control_ping_retry;
	struct delayed_work afc_qc_retry;
	struct delayed_work afc_after_prepare;
	struct delayed_work afc_check_interrupt;
	struct delayed_work dcd_recheck;
	struct delayed_work incomplete_check;
#if defined(CONFIG_CCIC_S2MU004)
	struct delayed_work water_detect_handler;
	struct delayed_work water_dry_handler;
#endif

	struct delayed_work afc_mrxrdy;
	int rev_id;
	int afc_irq;
	int attach_mode;

	struct notifier_block pdic_nb;
#if defined(CONFIG_CCIC_S2MU004)
	bool is_water_detect;
	bool is_otg_vboost;
	bool is_otg_reboost;

	bool otg_state;
	t_water_status water_status;
	t_water_dry_status water_dry_status;
	long dry_chk_time;
	int dry_cnt;
	int dry_duration_sec;
#else
#ifndef CONFIG_SEC_FACTORY
	bool is_water_wa;
#endif
	int bcd_rescan_cnt;
#endif

#if defined(CONFIG_HV_MUIC_S2MU004_AFC)
	bool				is_afc_muic_ready;
	bool				is_afc_handshaking;
	bool				is_afc_muic_prepare;
	bool				is_charger_ready;
//	bool				is_qc_vb_settle;

//	u8				is_boot_dpdnvden;
	u8				tx_data;
	bool				is_mrxrdy;
	int				afc_count;
	muic_afc_data_t			afc_data;
	u8				qc_hv;
//	struct delayed_work		hv_muic_qc_vb_work;
//	struct delayed_work		hv_muic_mping_miss_wa;

	/* muic status value */
	u8				status1;
	u8				status2;
	u8				status3;
	u8				status4;

	/* muic hvcontrol value */
	u8				hvcontrol1;
	u8				hvcontrol2;
#endif
};


extern struct device *switch_device;
extern unsigned int system_rev;
extern struct muic_platform_data muic_pdata;

#endif /* __S2MU004_MUIC_H__ */
