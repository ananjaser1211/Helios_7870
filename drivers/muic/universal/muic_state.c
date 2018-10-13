/*
 * muic_state.c
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

#if defined(CONFIG_MUIC_NOTIFIER)
#include <linux/muic/muic_notifier.h>
#endif /* CONFIG_MUIC_NOTIFIER */

#if defined(CONFIG_VBUS_NOTIFIER)
#include <linux/vbus_notifier.h>
#endif /* CONFIG_VBUS_NOTIFIER */

#if defined(CONFIG_OF)
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#endif /* CONFIG_OF */

#include "muic-internal.h"
#include "muic_apis.h"
#include "muic_i2c.h"
#include "muic_vps.h"
#include "muic_regmap.h"
#include "muic_dt.h"
#if defined(CONFIG_MUIC_UNIVERSAL_SM5705)
#include <linux/muic/muic_afc.h>
#endif

#if defined(CONFIG_MUIC_UNIVERSAL_CCIC)
#include "muic_ccic.h"
#endif

extern int muic_wakeup_noti;

void muic_set_wakeup_noti(int flag)
{
	pr_info("%s: %d\n", __func__, flag);
	muic_wakeup_noti = flag;
}

static void update_jig_state(muic_data_t *pmuic)
{
	int jig_state;

	switch (pmuic->attached_dev) {
	case ATTACHED_DEV_JIG_UART_OFF_MUIC:
	case ATTACHED_DEV_JIG_UART_OFF_VB_MUIC:     /* VBUS enabled */
	case ATTACHED_DEV_JIG_UART_OFF_VB_OTG_MUIC:    /* for otg test */
	case ATTACHED_DEV_JIG_UART_OFF_VB_FG_MUIC:    /* for fg test */
	case ATTACHED_DEV_JIG_UART_ON_MUIC:
	case ATTACHED_DEV_JIG_UART_ON_VB_MUIC:       /* VBUS enabled */
	case ATTACHED_DEV_JIG_USB_OFF_MUIC:
	case ATTACHED_DEV_JIG_USB_ON_MUIC:
		jig_state = true;
		break;
	default:
		jig_state = false;
		break;
	}
	pr_err("%s:%s jig_state : %d\n", MUIC_DEV_NAME, __func__, jig_state);

	if (pmuic->pdata->jig_uart_cb)
		pmuic->pdata->jig_uart_cb(jig_state);
}

static void muic_handle_attach(muic_data_t *pmuic,
			muic_attached_dev_t new_dev, int adc, u8 vbvolt)
{
	int ret = 0;
	bool noti_f = true;

	pr_info("%s:%s attached_dev:%d new_dev:%d adc:0x%02x, vbvolt:%02x\n",
		MUIC_DEV_NAME, __func__, pmuic->attached_dev, new_dev, adc, vbvolt);

	if ((new_dev == pmuic->attached_dev) &&
		(new_dev != ATTACHED_DEV_JIG_UART_OFF_MUIC)) {
		pr_info("%s:%s Duplicated device %d just ignore\n",
				MUIC_DEV_NAME, __func__, pmuic->attached_dev);
		return;
	}
	switch (pmuic->attached_dev) {
	/* For GTACTIVE2 POGO */
	case ATTACHED_DEV_POGO_MUIC:
#ifdef CONFIG_MUIC_POGO
		muic_mux_sel_control(pmuic, NORMAL_USB_PATH);
#endif
	case ATTACHED_DEV_USB_MUIC:
	case ATTACHED_DEV_CDP_MUIC:
	case ATTACHED_DEV_JIG_USB_OFF_MUIC:
	case ATTACHED_DEV_JIG_USB_ON_MUIC:
	case ATTACHED_DEV_TIMEOUT_OPEN_MUIC:
		if (new_dev != pmuic->attached_dev) {
			pr_warn("%s:%s new(%d)!=attached(%d), assume detach\n",
					MUIC_DEV_NAME, __func__, new_dev,
					pmuic->attached_dev);
			ret = detach_usb(pmuic);
		}
		break;
	case ATTACHED_DEV_HMT_MUIC:
	case ATTACHED_DEV_OTG_MUIC:
	/* OTG -> LANHUB, meaning TA is attached to LANHUB(OTG) */
		if (new_dev == ATTACHED_DEV_USB_LANHUB_MUIC) {
			pr_info("%s:%s OTG+TA=>LANHUB. Do not detach OTG.\n",
					__func__, MUIC_DEV_NAME);
			noti_f = false;
			break;
		}

		if (new_dev == pmuic->attached_dev) {
			noti_f = false;
			break;
		}
	case ATTACHED_DEV_USB_LANHUB_MUIC:
		if (new_dev != pmuic->attached_dev) {
			pr_warn("%s:%s new(%d)!=attached(%d), assume detach\n",
					MUIC_DEV_NAME, __func__, new_dev,
					pmuic->attached_dev);
			ret = detach_otg_usb(pmuic);
		}
		break;

	case ATTACHED_DEV_AUDIODOCK_MUIC:
		if (new_dev != pmuic->attached_dev) {
			pr_warn("%s:%s new(%d)!=attached(%d), assume detach\n",
					MUIC_DEV_NAME, __func__, new_dev,
					pmuic->attached_dev);
			ret = detach_audiodock(pmuic);
		}
		break;

	case ATTACHED_DEV_TA_MUIC:
		pmuic->attached_dev = ATTACHED_DEV_NONE_MUIC;
		break;
	case ATTACHED_DEV_JIG_UART_OFF_VB_MUIC:
	case ATTACHED_DEV_JIG_UART_OFF_MUIC:
		if (new_dev != ATTACHED_DEV_JIG_UART_OFF_MUIC) {
			pr_warn("%s:%s new(%d)!=attached(%d), assume detach\n",
					MUIC_DEV_NAME, __func__, new_dev,
					pmuic->attached_dev);
			ret = detach_jig_uart_boot_off(pmuic);
		}
		break;

	case ATTACHED_DEV_JIG_UART_ON_MUIC:
		if (new_dev != pmuic->attached_dev) {
			pr_warn("%s:%s new(%d)!=attached(%d), assume detach\n",
				MUIC_DEV_NAME, __func__, new_dev,
				pmuic->attached_dev);

			if (pmuic->is_factory_start)
				ret = detach_deskdock(pmuic);
			else
				ret = detach_jig_uart_boot_on(pmuic);

			muic_set_wakeup_noti(pmuic->is_factory_start ? 1 : 0);
		}
		break;
	case ATTACHED_DEV_DESKDOCK_MUIC:
	case ATTACHED_DEV_DESKDOCK_VB_MUIC:
		if (new_dev == ATTACHED_DEV_DESKDOCK_MUIC ||
				new_dev == ATTACHED_DEV_DESKDOCK_VB_MUIC) {
			pr_warn("%s:%s new(%d)!=attached(%d), assume same device\n",
					MUIC_DEV_NAME, __func__, new_dev,
					pmuic->attached_dev);
			noti_f = false;
		} else if (new_dev != pmuic->attached_dev) {
			pr_warn("%s:%s new(%d)!=attached(%d), assume detach\n",
					MUIC_DEV_NAME, __func__, new_dev,
					pmuic->attached_dev);
			ret = detach_deskdock(pmuic);
		}
		break;
	case ATTACHED_DEV_UNIVERSAL_MMDOCK_MUIC:
		if (new_dev != pmuic->attached_dev) {
			pr_warn("%s:%s new(%d)!=attached(%d), assume detach\n",
					MUIC_DEV_NAME, __func__, new_dev,
					pmuic->attached_dev);
			ret = detach_otg_usb(pmuic);
		}
		break;
	case ATTACHED_DEV_CHARGING_CABLE_MUIC:
		if (new_dev != pmuic->attached_dev) {
			pr_warn("%s:%s new(%d)!=attached(%d), assume detach\n",
					MUIC_DEV_NAME, __func__, new_dev,
					pmuic->attached_dev);
			ret = detach_ps_cable(pmuic);
		}
		break;
	case ATTACHED_DEV_UNDEFINED_CHARGING_MUIC:
	case ATTACHED_DEV_UNDEFINED_RANGE_MUIC:
		break;
	case ATTACHED_DEV_CHARGING_POGO_VB_MUIC:
#ifdef CONFIG_MUIC_POGO
		muic_mux_sel_control(pmuic, NORMAL_USB_PATH);
#endif
		break;

	default:
		noti_f = false;
	}

	if (noti_f)
		muic_notifier_detach_attached_dev(pmuic->attached_dev);

	noti_f = true;
	switch (new_dev) {
	/* For GTACTIVE2 POGO */
	case ATTACHED_DEV_POGO_MUIC:
#ifdef CONFIG_MUIC_POGO
		muic_mux_sel_control(pmuic, POGO_USB_PATH);
		pmuic->attached_dev = new_dev;
#endif
	case ATTACHED_DEV_USB_MUIC:
	case ATTACHED_DEV_CDP_MUIC:
	case ATTACHED_DEV_TIMEOUT_OPEN_MUIC:
		ret = attach_usb(pmuic, new_dev);
		break;
	case ATTACHED_DEV_HMT_MUIC:
	case ATTACHED_DEV_OTG_MUIC:
	case ATTACHED_DEV_USB_LANHUB_MUIC:
		ret = attach_otg_usb(pmuic, new_dev);
		break;
	case ATTACHED_DEV_AUDIODOCK_MUIC:
		ret = attach_audiodock(pmuic, new_dev, vbvolt);
		break;
	case ATTACHED_DEV_TA_MUIC:
		attach_ta(pmuic);
		if (pmuic->is_camera_on)
			pmuic->is_afc_5v = 1;
		pmuic->attached_dev = new_dev;
		break;
	case ATTACHED_DEV_JIG_UART_OFF_MUIC:
		new_dev = attach_jig_uart_boot_off(pmuic, new_dev, vbvolt);
		break;
	case ATTACHED_DEV_JIG_UART_ON_MUIC:
		/* Keep AP UART path and
		 *  call attach_deskdock to wake up the device in the Facory Build Binary.
		 */
		 if (pmuic->is_factory_start)
			ret = attach_deskdock(pmuic, new_dev);
		else
			ret = attach_jig_uart_boot_on(pmuic, new_dev);

		muic_set_wakeup_noti(pmuic->is_factory_start ? 1 : 0);
		break;
	case ATTACHED_DEV_JIG_USB_OFF_MUIC:
		ret = attach_jig_usb_boot_off(pmuic, vbvolt);
		break;
	case ATTACHED_DEV_JIG_USB_ON_MUIC:
		ret = attach_jig_usb_boot_on(pmuic, vbvolt);
		break;
	case ATTACHED_DEV_MHL_MUIC:
		ret = attach_mhl(pmuic);
		break;
	case ATTACHED_DEV_DESKDOCK_MUIC:
	case ATTACHED_DEV_DESKDOCK_VB_MUIC:
		ret = attach_deskdock(pmuic, new_dev);
		break;
	case ATTACHED_DEV_UNIVERSAL_MMDOCK_MUIC:
		ret = attach_otg_usb(pmuic, new_dev);
		break;
	case ATTACHED_DEV_CHARGING_CABLE_MUIC:
		ret = attach_ps_cable(pmuic, new_dev);
		break;
	case ATTACHED_DEV_UNDEFINED_CHARGING_MUIC:
	case ATTACHED_DEV_UNDEFINED_RANGE_MUIC:
		com_to_open_with_vbus(pmuic);
		pmuic->attached_dev = new_dev;
		break;
	case ATTACHED_DEV_VZW_INCOMPATIBLE_MUIC:
		com_to_open_with_vbus(pmuic);
		pmuic->attached_dev = new_dev;
		break;
	case ATTACHED_DEV_CHARGING_POGO_VB_MUIC:
#ifdef CONFIG_MUIC_POGO
		muic_mux_sel_control(pmuic, POGO_USB_PATH);
		pmuic->attached_dev = new_dev;
#endif
		break;
	default:
		pr_warn("%s:%s unsupported dev=%d, adc=0x%x, vbus=%c\n",
				MUIC_DEV_NAME, __func__, new_dev, adc,
				(vbvolt ? 'O' : 'X'));
		break;
	}

	if (noti_f)
		muic_notifier_attach_attached_dev(new_dev);
	else
		pr_info("%s:%s attach Noti. for (%d) discarded.\n",
				MUIC_DEV_NAME, __func__, new_dev);

	if (ret < 0)
		pr_warn("%s:%s something wrong with attaching %d (ERR=%d)\n",
				MUIC_DEV_NAME, __func__, new_dev, ret);
}

static void muic_handle_detach(muic_data_t *pmuic)
{
	int ret = 0;
	bool noti_f = true;

	ret = com_to_open_with_vbus(pmuic);
#if defined(CONFIG_MUIC_UNIVERSAL_SM5705)
	/* ENAFC set '0' */
	pmuic->regmapdesc->afcops->afc_ctrl_reg(pmuic->regmapdesc, AFCCTRL_ENAFC, 0);
#endif
	pmuic->is_afc_5v = 0;
	pmuic->check_charger_lcd_on = false;

	switch (pmuic->attached_dev) {
	/* For GTACTIVE2 POGO */
	case ATTACHED_DEV_POGO_MUIC:
#ifdef CONFIG_MUIC_POGO
		muic_mux_sel_control(pmuic, NORMAL_USB_PATH);
		pmuic->attached_dev = ATTACHED_DEV_NONE_MUIC;
#endif
	/* FIXME */
	case ATTACHED_DEV_TIMEOUT_OPEN_MUIC:
		pmuic->is_dcdtmr_intr = false;
		pmuic->is_rescanned = false;
	case ATTACHED_DEV_JIG_USB_OFF_MUIC:
	case ATTACHED_DEV_JIG_USB_ON_MUIC:
	case ATTACHED_DEV_USB_MUIC:
	case ATTACHED_DEV_CDP_MUIC:
		ret = detach_usb(pmuic);
		break;
	case ATTACHED_DEV_HMT_MUIC:
	case ATTACHED_DEV_OTG_MUIC:
	case ATTACHED_DEV_USB_LANHUB_MUIC:
		ret = detach_otg_usb(pmuic);
		break;
	case ATTACHED_DEV_TA_MUIC:
		pmuic->attached_dev = ATTACHED_DEV_NONE_MUIC;
		detach_ta(pmuic);
		break;
	case ATTACHED_DEV_JIG_UART_OFF_VB_MUIC:
	case ATTACHED_DEV_JIG_UART_OFF_MUIC:
		ret = detach_jig_uart_boot_off(pmuic);
		break;
	case ATTACHED_DEV_JIG_UART_ON_MUIC:
		if (pmuic->is_factory_start)
			ret = detach_deskdock(pmuic);
		else
			ret = detach_jig_uart_boot_on(pmuic);

		muic_set_wakeup_noti(pmuic->is_factory_start ? 1 : 0);
		break;
	case ATTACHED_DEV_DESKDOCK_MUIC:
	case ATTACHED_DEV_DESKDOCK_VB_MUIC:
		ret = detach_deskdock(pmuic);
		break;
	case ATTACHED_DEV_UNIVERSAL_MMDOCK_MUIC:
		ret = detach_otg_usb(pmuic);
		break;
	case ATTACHED_DEV_AUDIODOCK_MUIC:
		ret = detach_audiodock(pmuic);
		break;
	case ATTACHED_DEV_MHL_MUIC:
		ret = detach_mhl(pmuic);
		break;
	case ATTACHED_DEV_CHARGING_CABLE_MUIC:
		ret = detach_ps_cable(pmuic);
		break;
	case ATTACHED_DEV_NONE_MUIC:
		pmuic->is_afc_device = 0;
		pr_info("%s:%s duplicated(NONE)\n", MUIC_DEV_NAME, __func__);
		break;
	case ATTACHED_DEV_UNDEFINED_CHARGING_MUIC:
		pr_info("%s:%s UNKNOWN\n", MUIC_DEV_NAME, __func__);
		pmuic->attached_dev = ATTACHED_DEV_NONE_MUIC;
		break;
	case ATTACHED_DEV_UNDEFINED_RANGE_MUIC:
		pr_info("%s:%s UNDEFINED_RANGE\n", MUIC_DEV_NAME, __func__);
		pmuic->attached_dev = ATTACHED_DEV_NONE_MUIC;
		break;
	case ATTACHED_DEV_CHARGING_POGO_VB_MUIC:
#ifdef CONFIG_MUIC_POGO
		pr_info("%s:%s CHARGING POGO DOCK\n", MUIC_DEV_NAME, __func__);
		muic_mux_sel_control(pmuic, NORMAL_USB_PATH);
		pmuic->attached_dev = ATTACHED_DEV_NONE_MUIC;
#endif
		break;
	default:
		pmuic->is_afc_device = 0;
		pr_info("%s:%s invalid attached_dev type(%d)\n", MUIC_DEV_NAME,
			__func__, pmuic->attached_dev);
		pmuic->attached_dev = ATTACHED_DEV_NONE_MUIC;
		break;
	}

	if (noti_f)
		muic_notifier_detach_attached_dev(pmuic->attached_dev);

	else
		pr_info("%s:%s detach Noti. for (%d) discarded.\n",
				MUIC_DEV_NAME, __func__, pmuic->attached_dev);
	if (ret < 0)
		pr_warn("%s:%s something wrong with detaching %d (ERR=%d)\n",
				MUIC_DEV_NAME, __func__, pmuic->attached_dev, ret);

}

void muic_detect_dev(muic_data_t *pmuic)
{
	muic_attached_dev_t new_dev = ATTACHED_DEV_UNKNOWN_MUIC;
	int intr = MUIC_INTR_DETACH;
	int usb_id_ctr_value = gpio_get_value(pmuic->usb_id_ctr);
	int rescanned_dev;

	get_vps_data(pmuic, &pmuic->vps);


	pr_info("%s:%s dev[1:0x%x, 2:0x%x, 3:0x%x], adc:0x%x, vbvolt:0x%x\n",
		MUIC_DEV_NAME, __func__, pmuic->vps.s.val1, pmuic->vps.s.val2,
		pmuic->vps.s.val3, pmuic->vps.s.adc, pmuic->vps.s.vbvolt);

	pr_info("%s:%s USB_ID_CTR:%d\n", MUIC_DEV_NAME, __func__, usb_id_ctr_value);

#if defined(CONFIG_MUIC_UNIVERSAL_CCIC)
	if (pmuic->rprd && pmuic->vps.s.vbvolt) {
		pr_info("%s:%s OTG Already attached.\n", MUIC_DEV_NAME, __func__);
#if defined(CONFIG_VBUS_NOTIFIER)
		vbus_notifier_handle(!!pmuic->vps.s.vbvolt ? STATUS_VBUS_HIGH : STATUS_VBUS_LOW);
#endif /* CONFIG_VBUS_NOTIFIER */
		return;
	}
#endif
	vps_resolve_dev(pmuic, &new_dev, &intr);

        if (new_dev == ATTACHED_DEV_USB_MUIC && pmuic->is_dcdtmr_intr == true) {
                new_dev = ATTACHED_DEV_TIMEOUT_OPEN_MUIC;
        }

#if defined(CONFIG_MUIC_UNIVERSAL_CCIC)
	if (pmuic->opmode & OPMODE_CCIC) {
		/* FIXME, POGO for GTACTIVE2 */
		if (new_dev != ATTACHED_DEV_POGO_MUIC && pmuic->attached_dev != ATTACHED_DEV_POGO_MUIC &&
			new_dev != ATTACHED_DEV_CHARGING_POGO_VB_MUIC &&
			pmuic->attached_dev != ATTACHED_DEV_CHARGING_POGO_VB_MUIC &&
			new_dev != ATTACHED_DEV_UNDEFINED_RANGE_MUIC &&
			pmuic->attached_dev != ATTACHED_DEV_UNDEFINED_RANGE_MUIC)
			if (!mdev_continue_for_TA_USB(pmuic, new_dev))
				return;
	}
#endif

	pr_info("%s:%s new_dev: %d, dcdtmr_intr: %d, rescanned: %d\n",
		MUIC_DEV_NAME, __func__, new_dev, pmuic->is_dcdtmr_intr,
		pmuic->is_rescanned);

	if (new_dev == ATTACHED_DEV_TIMEOUT_OPEN_MUIC &&
			pmuic->is_dcdtmr_intr ==true &&
			pmuic->is_rescanned == true) {
		rescanned_dev = BCD_rescan_incomplete_insertion(pmuic, pmuic->is_rescanned);
		pmuic->is_dcdtmr_intr = false;
		pmuic->is_rescanned = false;

		if (rescanned_dev > 0) {
			pr_info("%s : Chgdet complete. new_dev(%d)\n", MUIC_DEV_NAME, rescanned_dev);
			new_dev = rescanned_dev;
		}
		else
			pr_info("%s:%s Rescan error!\n", MUIC_DEV_NAME, __func__);
	}

	if (intr == MUIC_INTR_ATTACH) {
		muic_handle_attach(pmuic, new_dev,
			pmuic->vps.s.adc, pmuic->vps.s.vbvolt);
	} else {
		muic_handle_detach(pmuic);
	}

	update_jig_state(pmuic);

#if defined(CONFIG_VBUS_NOTIFIER)
	vbus_notifier_handle(!!pmuic->vps.s.vbvolt ? STATUS_VBUS_HIGH : STATUS_VBUS_LOW);
#endif /* CONFIG_VBUS_NOTIFIER */

}
