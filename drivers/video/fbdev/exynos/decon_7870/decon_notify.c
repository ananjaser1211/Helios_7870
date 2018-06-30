/* decon_notify.c
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
#include <linux/fb.h>
#include <linux/export.h>

#include "decon_notify.h"

static BLOCKING_NOTIFIER_HEAD(decon_notifier_list);

int decon_register_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&decon_notifier_list, nb);
}
EXPORT_SYMBOL(decon_register_notifier);

int decon_unregister_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&decon_notifier_list, nb);
}
EXPORT_SYMBOL(decon_unregister_notifier);

int decon_notifier_call_chain(unsigned long val, void *v)
{
	return blocking_notifier_call_chain(&decon_notifier_list, val, v);
}
EXPORT_SYMBOL(decon_notifier_call_chain);

static int decon_notifier_event(struct notifier_block *this,
	unsigned long val, void *v)
{
	if (decon_notifier_list.head == NULL)
		return NOTIFY_DONE;

	switch (val) {
	case FB_EVENT_BLANK:
	case FB_EARLY_EVENT_BLANK:
		break;
	default:
		return NOTIFY_DONE;
	}

	pr_info("%s: %02lx\n", __func__, val);

	decon_notifier_call_chain(val, v);

	return NOTIFY_DONE;
}

static struct notifier_block decon_fb_notifier = {
	.notifier_call = decon_notifier_event,
};

static void __exit decon_notifier_exit(void)
{
	fb_unregister_client(&decon_fb_notifier);

	return;
}

static int __init decon_notifier_init(void)
{
	fb_register_client(&decon_fb_notifier);

	return 0;
}

late_initcall(decon_notifier_init);
module_exit(decon_notifier_exit);

