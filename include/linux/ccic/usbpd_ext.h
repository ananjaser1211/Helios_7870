#if defined(CONFIG_IFPMIC_SUPPORT)
#include <linux/ifpmic/ccic/usbpd_ext.h>
#else
#if defined(CONFIG_CCIC_NOTIFIER)
#include <linux/ccic/ccic_notifier.h>
#endif
#if defined(CONFIG_DUAL_ROLE_USB_INTF)
#include <linux/usb/class-dual-role.h>
#endif
#ifdef CONFIG_USB_TYPEC_MANAGER_NOTIFIER
#include <linux/battery/battery_notifier.h>
#endif
#endif /* CONFIG_IFPMIC_SUPPORT */

#ifndef __USBPD_EXT_H__
#define __USBPD_EXT_H__

#ifdef CONFIG_USB_TYPEC_MANAGER_NOTIFIER
extern struct pdic_notifier_struct pd_noti;
#endif

#if defined(CONFIG_CCIC_NOTIFIER)
void ccic_event_work(void *data, int dest, int id, int attach, int event);
extern void select_pdo(int num);
#endif
#if defined(CONFIG_DUAL_ROLE_USB_INTF)
extern void role_swap_check(struct work_struct *wk);
extern int dual_role_is_writeable(struct dual_role_phy_instance *drp,
				  enum dual_role_property prop);
extern int dual_role_get_local_prop(struct dual_role_phy_instance *dual_role,
				    enum dual_role_property prop,
				    unsigned int *val);
extern int dual_role_set_prop(struct dual_role_phy_instance *dual_role,
			      enum dual_role_property prop,
			      const unsigned int *val);
extern int dual_role_init(void *_data);
#endif
#endif
