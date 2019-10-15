/*
 * muic_ccic.c
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
#include <linux/string.h>
#if defined (CONFIG_OF)
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#endif

#include <linux/muic/muic.h>
#if defined(CONFIG_MUIC_NOTIFIER)
#include <linux/muic/muic_notifier.h>
#endif

#if defined(CONFIG_USB_EXTERNAL_NOTIFY)
#include <linux/usb_notify.h>
#endif

#include "muic-internal.h"
#include "muic_coagent.h"

#if defined(CONFIG_USB_EXTERNAL_NOTIFY)
extern void muic_send_dock_intent(int type);

static int muic_handle_usb_notification(struct notifier_block *nb,
				unsigned long action, void *data)
{
#ifdef CONFIG_MUIC_POGO
	muic_data_t *pmuic =
		container_of(nb, muic_data_t, usb_nb);
#endif

	switch (action) {
	/* Abnormal device */
	case EXTERNAL_NOTIFY_3S_NODEVICE:
		pr_info("%s: 3S_NODEVICE(USB HOST Connection timeout)\n", __func__);
#ifdef CONFIG_MUIC_POGO
		if (pmuic->attached_dev == ATTACHED_DEV_POGO_MUIC)
			muic_set_pogo_status(pmuic, 1);
#endif
		break;

	default:
		break;
	}

	return NOTIFY_DONE;
}


void muic_register_usb_notifier(muic_data_t *pmuic)
{
	int ret = 0;

	pr_info("%s: Registering EXTERNAL_NOTIFY_DEV_MUIC.\n", __func__);


	ret = usb_external_notify_register(&pmuic->usb_nb,
		muic_handle_usb_notification, EXTERNAL_NOTIFY_DEV_MUIC);
	if (ret < 0) {
		pr_info("%s: USB Noti. is not ready.\n", __func__);
		return;
	}

	pr_info("%s: done.\n", __func__);
}

void muic_unregister_usb_notifier(muic_data_t *pmuic)
{
	int ret = 0;

	pr_info("%s\n", __func__);

	ret = usb_external_notify_unregister(&pmuic->usb_nb);
	if (ret < 0) {
		pr_info("%s: USB Noti. unregister error.\n", __func__);
		return;
	}

	pr_info("%s: done.\n", __func__);
}
#else
void muic_register_usb_notifier(muic_data_t *pmuic){}
void muic_unregister_usb_notifier(muic_data_t *pmuic){}
#endif
