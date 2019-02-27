/*
*	USB PD Driver - Device Policy Manager
*/

#include <linux/device.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/ifpmic/ccic/usbpd.h>
#include <linux/ifpmic/ccic/usbpd-s2mu004.h>
#include <linux/of_gpio.h>

#include <linux/ifpmic/muic/muic.h>
#if defined(CONFIG_MUIC_NOTIFIER)
#include <linux/ifpmic/muic/muic_notifier.h>
#endif /* CONFIG_MUIC_NOTIFIER */
#include <linux/ifpmic/ccic/pdic_notifier.h>
#ifdef CONFIG_BATTERY_SAMSUNG
#ifdef CONFIG_USB_TYPEC_MANAGER_NOTIFIER
#include <linux/battery/battery_notifier.h>
#endif
#endif

#if defined(CONFIG_CCIC_NOTIFIER)
#include <linux/ifpmic/ccic/ccic_notifier.h>
#endif

#if (defined CONFIG_CCIC_NOTIFIER || defined CONFIG_DUAL_ROLE_USB_INTF)
#include <linux/ifpmic/ccic/usbpd_ext.h>
#endif

/* switch device header */
#if defined(CONFIG_SWITCH)
#include <linux/switch.h>
#endif /* CONFIG_SWITCH */

#ifdef CONFIG_USB_HOST_NOTIFY
#include <linux/usb_notify.h>
#endif

#if defined(CONFIG_SWITCH)
static struct switch_dev switch_dock = {
	.name = "ccic_dock",
};
#endif

#ifdef CONFIG_BATTERY_SAMSUNG
#ifdef CONFIG_USB_TYPEC_MANAGER_NOTIFIER
void select_pdo(int num);
void s2mu004_select_pdo(int num);
void (*fp_select_pdo)(int num);

void s2mu004_select_pdo(int num)
{
	struct usbpd_data *pd_data = pd_noti.pd_data;
	bool vbus_short;

	pd_data->phy_ops.get_vbus_short_check(pd_data, &vbus_short);

	if (vbus_short) {
		pr_info(" %s : PDO(%d) is ignored becasue of vbus short\n",
				__func__, pd_noti.sink_status.selected_pdo_num);
		return;
	}

	if (pd_noti.sink_status.selected_pdo_num == num)
		return;
	else if (num > pd_noti.sink_status.available_pdo_num)
		pd_noti.sink_status.selected_pdo_num = pd_noti.sink_status.available_pdo_num;
	else if (num < 1)
		pd_noti.sink_status.selected_pdo_num = 1;
	else
		pd_noti.sink_status.selected_pdo_num = num;
	pr_info(" %s : PDO(%d) is selected to change\n", __func__, pd_noti.sink_status.selected_pdo_num);

	usbpd_manager_inform_event(pd_noti.pd_data, MANAGER_NEW_POWER_SRC);
}

void select_pdo(int num)
{
	if (fp_select_pdo)
		fp_select_pdo(num);
}
#endif
#endif

static void init_source_cap_data(struct usbpd_manager_data *_data)
{
/*	struct usbpd_data *pd_data = manager_to_usbpd(_data);
	int val;						*/
	msg_header_type *msg_header = &_data->pd_data->source_msg_header;
	data_obj_type *data_obj = &_data->pd_data->source_data_obj;

	msg_header->msg_type = USBPD_Source_Capabilities;
/*	pd_data->phy_ops.get_power_role(pd_data, &val);		*/
	msg_header->port_data_role = USBPD_DFP;
	msg_header->spec_revision = 1;
	msg_header->port_power_role = USBPD_SOURCE;
	msg_header->num_data_objs = 1;

	data_obj->power_data_obj.max_current = 500 / 10;
	data_obj->power_data_obj.voltage = 5000 / 50;
	data_obj->power_data_obj.supply = POWER_TYPE_FIXED;
	data_obj->power_data_obj.data_role_swap = 1;
	data_obj->power_data_obj.dual_role_power = 1;
	data_obj->power_data_obj.usb_suspend_support = 1;
	data_obj->power_data_obj.usb_comm_capable = 1;

}

static void init_sink_cap_data(struct usbpd_manager_data *_data)
{
/*	struct usbpd_data *pd_data = manager_to_usbpd(_data);
	int val;						*/
	msg_header_type *msg_header = &_data->pd_data->sink_msg_header;
	data_obj_type *data_obj = _data->pd_data->sink_data_obj;

	msg_header->msg_type = USBPD_Sink_Capabilities;
/*	pd_data->phy_ops.get_power_role(pd_data, &val);		*/
	msg_header->port_data_role = USBPD_UFP;
	msg_header->spec_revision = 1;
	msg_header->port_power_role = USBPD_SINK;
	msg_header->num_data_objs = 2;

	data_obj->power_data_obj_sink.supply_type = POWER_TYPE_FIXED;
	data_obj->power_data_obj_sink.dual_role_power = 1;
	data_obj->power_data_obj_sink.higher_capability = 1;
	data_obj->power_data_obj_sink.externally_powered = 0;
	data_obj->power_data_obj_sink.usb_comm_capable = 1;
	data_obj->power_data_obj_sink.data_role_swap = 1;
	data_obj->power_data_obj_sink.voltage = 5000/50;
	data_obj->power_data_obj_sink.op_current = 500/10;

	(data_obj + 1)->power_data_obj_variable.supply_type = POWER_TYPE_VARIABLE;
	(data_obj + 1)->power_data_obj_variable.max_voltage = _data->sink_cap_max_volt / 50;
	(data_obj + 1)->power_data_obj_variable.min_voltage = 5000 / 50;
	(data_obj + 1)->power_data_obj_variable.max_current = 500 / 10;
}

int usbpd_manager_send_samsung_uvdm_message(void *data, const char *buf, size_t size)
{
	struct s2mu004_usbpd_data *pdic_data = data;
	struct usbpd_data *pd_data = dev_get_drvdata(pdic_data->dev);
	struct policy_data *policy = &pd_data->policy;
	int received_data = 0;
	int data_role = 0;
	int power_role = 0;

	if ((buf == NULL)||(size < sizeof(unsigned int))) {
		pr_info("%s given data is not valid !\n", __func__);
		return -EINVAL;
	}

	sscanf(buf, "%d", &received_data);

	data_role = pd_data->phy_ops.get_data_role(pd_data, &data_role);

	if (data_role == USBPD_UFP) {
		pr_info("%s, skip, now data role is ufp\n", __func__);
		return 0;
	}

	data_role = pd_data->phy_ops.get_power_role(pd_data, &power_role);

	policy->tx_msg_header.msg_type = USBPD_UVDM_MSG;
	policy->tx_msg_header.port_data_role = USBPD_DFP;
	policy->tx_msg_header.port_power_role = power_role;
	policy->tx_data_obj[0].unstructured_vdm.vendor_id = SAMSUNG_VENDOR_ID;
	policy->tx_data_obj[0].unstructured_vdm.vendor_defined = SEC_UVDM_UNSTRUCTURED_VDM;
	policy->tx_data_obj[1].object = received_data;

	if (policy->tx_data_obj[1].sec_uvdm_header.data_type == SEC_UVDM_SHORT_DATA) {
		pr_info("%s - process short data!\n", __func__);
		// process short data
		// phase 1. fill message header
		policy->tx_msg_header.num_data_objs = 2; // VDM Header + 6 VDOs = MAX 7
		// phase 2. fill uvdm header (already filled)
		// phase 3. fill sec uvdm header
		policy->tx_data_obj[1].sec_uvdm_header.total_number_of_uvdm_set = 1;
	} else {
		pr_info("%s - process long data!\n", __func__);
		// process long data
		// phase 1. fill message header
		// phase 2. fill uvdm header
		// phase 3. fill sec uvdm header
		// phase 4.5.6.7 fill sec data header , data , sec data tail and so on.
	}

	usbpd_send_msg(pd_data, &policy->tx_msg_header, policy->tx_data_obj);

	return 0;
}

void usbpd_manager_plug_attach(struct device *dev, muic_attached_dev_t new_dev)
{
#ifdef CONFIG_BATTERY_SAMSUNG
#ifdef CONFIG_USB_TYPEC_MANAGER_NOTIFIER
	struct usbpd_data *pd_data = dev_get_drvdata(dev);
	struct policy_data *policy = &pd_data->policy;

	CC_NOTI_ATTACH_TYPEDEF pd_notifier;

	if (new_dev == ATTACHED_DEV_TYPE3_CHARGER_MUIC) {
		if (policy->send_sink_cap) {
			pd_noti.event = PDIC_NOTIFY_EVENT_PD_SINK_CAP;
			policy->send_sink_cap = 0;
		} else
			pd_noti.event = PDIC_NOTIFY_EVENT_PD_SINK;
		pd_notifier.src = CCIC_NOTIFY_DEV_CCIC;
		pd_notifier.dest = CCIC_NOTIFY_DEV_BATTERY;
		pd_notifier.id = CCIC_NOTIFY_ID_POWER_STATUS;
		pd_notifier.attach = 1;
		pd_notifier.pd = &pd_noti;
#if defined(CONFIG_CCIC_NOTIFIER)
		ccic_notifier_notify((CC_NOTI_TYPEDEF *)&pd_notifier, &pd_noti, 1/* pdic_attach */);
#endif
	}

#else
	struct usbpd_data *pd_data = dev_get_drvdata(dev);
	struct usbpd_manager_data *manager = &pd_data->manager;

	pr_info("%s: usbpd plug attached\n", __func__);
	manager->attached_dev = new_dev;
	s2mu004_pdic_notifier_attach_attached_dev(manager->attached_dev);
#endif
#endif
}

void usbpd_manager_plug_detach(struct device *dev, bool notify)
{
	struct usbpd_data *pd_data = dev_get_drvdata(dev);
	struct usbpd_manager_data *manager = &pd_data->manager;

	pr_info("%s: usbpd plug detached\n", __func__);
	usbpd_policy_reset(pd_data, PLUG_DETACHED);
	if (notify)
		s2mu004_pdic_notifier_detach_attached_dev(manager->attached_dev);
	manager->attached_dev = ATTACHED_DEV_NONE_MUIC;
}

void usbpd_manager_acc_detach(struct device *dev)
{	
	struct usbpd_data *pd_data = dev_get_drvdata(dev);
	struct usbpd_manager_data *manager = &pd_data->manager;

	pr_info("%s\n", __func__);
	if ( manager->acc_type != CCIC_DOCK_DETACHED ) {
		pr_info("%s: schedule_delayed_work \n", __func__);
		if ( manager->acc_type == CCIC_DOCK_HMT )
			schedule_delayed_work(&manager->acc_detach_handler, msecs_to_jiffies(1000));
		else
			schedule_delayed_work(&manager->acc_detach_handler, msecs_to_jiffies(0));
	}	
}

int usbpd_manager_command_to_policy(struct device *dev,
		usbpd_manager_command_type command)
{
	struct usbpd_data *pd_data = dev_get_drvdata(dev);
	struct usbpd_manager_data *manager = &pd_data->manager;

	manager->cmd = command;

	usbpd_kick_policy_work(dev);

	/* TODO: check result
	if (manager->event) {
	 ...
	}
	*/
	return 0;
}

void usbpd_manager_inform_event(struct usbpd_data *pd_data,
		usbpd_manager_event_type event)
{
	struct usbpd_manager_data *manager = &pd_data->manager;

	manager->event = event;

	switch (event) {
	case MANAGER_DISCOVER_IDENTITY_ACKED:
		usbpd_manager_get_identity(pd_data);
		usbpd_manager_command_to_policy(pd_data->dev,
				MANAGER_REQ_VDM_DISCOVER_SVID);
		break;
	case MANAGER_DISCOVER_SVID_ACKED:
		usbpd_manager_get_svids(pd_data);
		usbpd_manager_command_to_policy(pd_data->dev,
				MANAGER_REQ_VDM_DISCOVER_MODE);
		break;
	case MANAGER_DISCOVER_MODE_ACKED:
		usbpd_manager_get_modes(pd_data);
		usbpd_manager_command_to_policy(pd_data->dev,
				MANAGER_REQ_VDM_ENTER_MODE);
		break;
	case MANAGER_ENTER_MODE_ACKED:
		usbpd_manager_enter_mode(pd_data);
		usbpd_manager_command_to_policy(pd_data->dev,
				MANAGER_REQ_VDM_STATUS_UPDATE);
		break;
	case MANAGER_STATUS_UPDATE_ACKED:
		usbpd_manager_command_to_policy(pd_data->dev,
			MANAGER_REQ_VDM_DisplayPort_Configure);
		break;
	case MANAGER_DisplayPort_Configure_ACKED:
		break;
	case MANAGER_NEW_POWER_SRC:
		usbpd_manager_command_to_policy(pd_data->dev,
				MANAGER_REQ_NEW_POWER_SRC);
		break;
	default:
		pr_info("%s: not matched event(%d)\n", __func__, event);
	}
}

bool usbpd_manager_vdm_request_enabled(struct usbpd_data *pd_data)
{
	struct usbpd_manager_data *manager = &pd_data->manager;
	/* TODO : checking cable discovering
	   if (pd_data->counter.discover_identity_counter
		   < USBPD_nDiscoverIdentityCount)

	   struct usbpd_manager_data *manager = &pd_data->manager;
	   if (manager->event != MANAGER_DISCOVER_IDENTITY_ACKED
	      || manager->event != MANAGER_DISCOVER_IDENTITY_NAKED)

	   return(1);
	*/
	if (manager->alt_sended)
		return false;
	else {
		manager->alt_sended = 1;
		return true;
	}	
}

bool usbpd_manager_power_role_swap(struct usbpd_data *pd_data)
{
	struct usbpd_manager_data *manager = &pd_data->manager;

	return manager->power_role_swap;
}

bool usbpd_manager_vconn_source_swap(struct usbpd_data *pd_data)
{
	struct usbpd_manager_data *manager = &pd_data->manager;

	return manager->vconn_source_swap;
}

void usbpd_manager_turn_off_vconn(struct usbpd_data *pd_data)
{
	/* TODO : Turn off vconn */
}

void usbpd_manager_turn_on_source(struct usbpd_data *pd_data)
{
	struct usbpd_manager_data *manager = &pd_data->manager;

	pr_info("%s: usbpd plug attached\n", __func__);

	manager->attached_dev = ATTACHED_DEV_TYPE3_ADAPTER_MUIC;
	s2mu004_pdic_notifier_attach_attached_dev(manager->attached_dev);
	/* TODO : Turn on source */
}

void usbpd_manager_turn_off_power_supply(struct usbpd_data *pd_data)
{
	struct usbpd_manager_data *manager = &pd_data->manager;

	pr_info("%s: usbpd plug detached\n", __func__);

	s2mu004_pdic_notifier_detach_attached_dev(manager->attached_dev);
	manager->attached_dev = ATTACHED_DEV_NONE_MUIC;
	/* TODO : Turn off power supply */
}

void usbpd_manager_turn_off_power_sink(struct usbpd_data *pd_data)
{
	struct usbpd_manager_data *manager = &pd_data->manager;

	pr_info("%s: usbpd sink turn off\n", __func__);

	s2mu004_pdic_notifier_detach_attached_dev(manager->attached_dev);
	manager->attached_dev = ATTACHED_DEV_NONE_MUIC;
	/* TODO : Turn off power sink */
}

bool usbpd_manager_data_role_swap(struct usbpd_data *pd_data)
{
	struct usbpd_manager_data *manager = &pd_data->manager;

	return manager->data_role_swap;
}

int usbpd_manager_register_switch_device(int mode)
{
#ifdef CONFIG_SWITCH
	int ret = 0;
	if (mode) {
		ret = switch_dev_register(&switch_dock);
		if (ret < 0) {
			pr_err("%s: Failed to register dock switch(%d)\n",
			       __func__, ret);
			return -ENODEV;
		}
	} else {
		switch_dev_unregister(&switch_dock);
	}
#endif /* CONFIG_SWITCH */
	return 0;
}

static void usbpd_manager_send_dock_intent(int type)
{
	pr_info("%s: CCIC dock type(%d)\n", __func__, type);
#ifdef CONFIG_SWITCH
	switch_set_state(&switch_dock, type);
#endif /* CONFIG_SWITCH */
}

void usbpd_manager_acc_detach_handler(struct work_struct *wk)
{
//	struct delayed_work *delay_work =
//		container_of(wk, struct delayed_work, work);
	struct usbpd_manager_data *manager =
		container_of(wk, struct usbpd_manager_data, acc_detach_handler.work);

	pr_info("%s: attached_dev : %d ccic dock type %d\n", __func__, manager->attached_dev, manager->acc_type);
	if (manager->attached_dev == ATTACHED_DEV_NONE_MUIC) {
		if (manager->acc_type != CCIC_DOCK_DETACHED) {
			usbpd_manager_send_dock_intent(CCIC_DOCK_DETACHED);
			manager->acc_type = CCIC_DOCK_DETACHED;
		}
	}
}

void usbpd_manager_acc_handler_cancel(struct device *dev)
{
	struct usbpd_data *pd_data = dev_get_drvdata(dev);
	struct usbpd_manager_data *manager = &pd_data->manager;

	if (manager->acc_type != CCIC_DOCK_DETACHED) {
		pr_info("%s: cancel_delayed_work_sync \n", __func__);
		cancel_delayed_work_sync(&manager->acc_detach_handler);
	}
}

static int usbpd_manager_check_accessory(struct usbpd_manager_data *manager)
{
#if defined(CONFIG_USB_HW_PARAM)
	struct otg_notify *o_notify = get_otg_notify();
#endif
	uint16_t vid = manager->Vendor_ID;
	uint16_t pid = manager->Product_ID;
	uint16_t dock_type = 0;

	/* detect Gear VR */
	if (manager->acc_type == CCIC_DOCK_DETACHED) {
		if (vid == SAMSUNG_VENDOR_ID) {
			switch (pid) {
			/* GearVR: Reserved GearVR PID+6 */
			case GEARVR_PRODUCT_ID:
			case GEARVR_PRODUCT_ID_1:
			case GEARVR_PRODUCT_ID_2:
			case GEARVR_PRODUCT_ID_3:
			case GEARVR_PRODUCT_ID_4:
			case GEARVR_PRODUCT_ID_5:
				dock_type = CCIC_DOCK_HMT;
				pr_info("%s : Samsung Gear VR connected.\n", __func__);
#if defined(CONFIG_USB_HW_PARAM)
				if (o_notify)
					inc_hw_param(o_notify, USB_CCIC_VR_USE_COUNT);
#endif
				break;
			case DEXDOCK_PRODUCT_ID:
				dock_type = CCIC_DOCK_DEX;
				pr_info("%s : Samsung DEX connected.\n", __func__);
#if defined(CONFIG_USB_HW_PARAM)
				if (o_notify)
					inc_hw_param(o_notify, USB_CCIC_DEX_USE_COUNT);
#endif
				break;
			case HDMI_PRODUCT_ID:
				dock_type = CCIC_DOCK_HDMI;
				pr_info("%s : Samsung HDMI connected.\n", __func__);
				break;
			default:
				break;
			}
		} else if (vid == SAMSUNG_MPA_VENDOR_ID) {
			switch(pid) {
			case MPA_PRODUCT_ID:
				dock_type = CCIC_DOCK_MPA;
				pr_info("%s : Samsung MPA connected.\n", __func__);
				break;
			default:
				break;
			}
		}
	}

	if (dock_type) {
		manager->acc_type = dock_type;
		usbpd_manager_send_dock_intent(dock_type);
		return 1;
	}

	return 0;
}

/* Ok : 0, NAK: -1 */
int usbpd_manager_get_identity(struct usbpd_data *pd_data)
{
	struct policy_data *policy = &pd_data->policy;
	struct usbpd_manager_data *manager = &pd_data->manager;

	manager->Vendor_ID = policy->rx_data_obj[1].id_header_vdo.USB_Vendor_ID;
	manager->Product_ID = policy->rx_data_obj[3].product_vdo.USB_Product_ID;
	manager->Device_Version = policy->rx_data_obj[3].product_vdo.Device_Version;

	pr_info("%s, Vendor_ID : 0x%x, Product_ID : 0x%x, Device Version : 0x%x\n",
			__func__, manager->Vendor_ID, manager->Product_ID, manager->Device_Version);

	if (usbpd_manager_check_accessory(manager))
		pr_info("%s, Samsung Accessory Connected.\n", __func__);

	return 0;
}

/* Ok : 0, NAK: -1 */
int usbpd_manager_get_svids(struct usbpd_data *pd_data)
{
	struct policy_data *policy = &pd_data->policy;
	struct usbpd_manager_data *manager = &pd_data->manager;

	manager->SVID_0 = policy->rx_data_obj[1].vdm_svid.svid_0;
	manager->SVID_1 = policy->rx_data_obj[1].vdm_svid.svid_1;


	pr_info("%s, SVID_0 : 0x%x, SVID_1 : 0x%x\n", __func__,
				manager->SVID_0, manager->SVID_1);

	return 0;
}

/* Ok : 0, NAK: -1 */
int usbpd_manager_get_modes(struct usbpd_data *pd_data)
{
	struct policy_data *policy = &pd_data->policy;
	struct usbpd_manager_data *manager = &pd_data->manager;

	manager->Standard_Vendor_ID = policy->rx_data_obj[0].structured_vdm.svid;

	pr_info("%s, Standard_Vendor_ID = 0x%x\n", __func__,
				manager->Standard_Vendor_ID);

	return 0;
}

int usbpd_manager_enter_mode(struct usbpd_data *pd_data)
{
	return 0;
}

int usbpd_manager_exit_mode(struct usbpd_data *pd_data, unsigned mode)
{
	return 0;
}

data_obj_type usbpd_manager_select_capability(struct usbpd_data *pd_data)
{
	/* TODO: Request from present capabilities
		indicate if other capabilities would be required */
	data_obj_type obj;
#ifdef CONFIG_BATTERY_SAMSUNG
#ifdef CONFIG_USB_TYPEC_MANAGER_NOTIFIER
	int pdo_num = pd_noti.sink_status.selected_pdo_num;
#endif
#endif
	obj.request_data_object.no_usb_suspend = 1;
	obj.request_data_object.usb_comm_capable = 1;
	obj.request_data_object.capability_mismatch = 0;
	obj.request_data_object.give_back = 0;
#ifdef CONFIG_BATTERY_SAMSUNG
#ifdef CONFIG_USB_TYPEC_MANAGER_NOTIFIER
	obj.request_data_object.min_current = pd_noti.sink_status.power_list[pdo_num].max_current / USBPD_CURRENT_UNIT;
	obj.request_data_object.op_current = pd_noti.sink_status.power_list[pdo_num].max_current / USBPD_CURRENT_UNIT;
	obj.request_data_object.object_position = pd_noti.sink_status.selected_pdo_num;
#else
	obj.request_data_object.min_current = 10;
	obj.request_data_object.op_current = 10;
	obj.request_data_object.object_position = 1;
#endif
#endif

	return obj;
}

/*
   usbpd_manager_evaluate_capability
   : Policy engine ask Device Policy Manager to evaluate option
     based on supplied capabilities
	return	>0	: request object number
		0	: no selected option
*/
int usbpd_manager_evaluate_capability(struct usbpd_data *pd_data)
{
	struct policy_data *policy = &pd_data->policy;
	int i = 0;
	int power_type = 0;
	int pd_volt = 0, pd_current;
#ifdef CONFIG_BATTERY_SAMSUNG
#ifdef CONFIG_USB_TYPEC_MANAGER_NOTIFIER
	int available_pdo_num = 0;
	PDIC_SINK_STATUS *pdic_sink_status = &pd_noti.sink_status;
#endif
#endif
	data_obj_type *pd_obj;

	for (i = 0; i < policy->rx_msg_header.num_data_objs; i++) {
		pd_obj = &policy->rx_data_obj[i];
		power_type = pd_obj->power_data_obj_supply_type.supply_type;
		switch (power_type) {
		case POWER_TYPE_FIXED:
			pd_volt = pd_obj->power_data_obj.voltage;
			pd_current = pd_obj->power_data_obj.max_current;
			dev_info(pd_data->dev, "[%d] FIXED volt(%d)mV, max current(%d)\n",
					i+1, pd_volt * USBPD_VOLT_UNIT, pd_current * USBPD_CURRENT_UNIT);
#ifdef CONFIG_BATTERY_SAMSUNG
#ifdef CONFIG_USB_TYPEC_MANAGER_NOTIFIER
			if (pd_volt * USBPD_VOLT_UNIT <= MAX_CHARGING_VOLT)
				available_pdo_num = i + 1;
			pdic_sink_status->power_list[i + 1].max_voltage = pd_volt * USBPD_VOLT_UNIT;
			pdic_sink_status->power_list[i + 1].max_current = pd_current * USBPD_CURRENT_UNIT;
#endif
#endif
			break;
		case POWER_TYPE_BATTERY:
			pd_volt = pd_obj->power_data_obj_battery.max_voltage;
			dev_info(pd_data->dev, "[%d] BATTERY volt(%d)mV\n",
					i+1, pd_volt * USBPD_VOLT_UNIT);
			break;
		case POWER_TYPE_VARIABLE:
			pd_volt = pd_obj->power_data_obj_variable.max_voltage;
			dev_info(pd_data->dev, "[%d] VARIABLE volt(%d)mV\n",
					i+1, pd_volt * USBPD_VOLT_UNIT);
			break;
		default:
			dev_err(pd_data->dev, "[%d] Power Type Error\n", i+1);
			break;
		}
	}
#ifdef CONFIG_BATTERY_SAMSUNG
#ifdef CONFIG_USB_TYPEC_MANAGER_NOTIFIER
	pdic_sink_status->available_pdo_num = available_pdo_num;
	return available_pdo_num;
#endif
#else
	return 1; /* select default first obj */
#endif
}

/* return: 0: cab be met, -1: cannot be met, -2: could be met later */
int usbpd_manager_match_request(struct usbpd_data *pd_data)
{
	/* TODO: Evaluation of sink request */

	unsigned supply_type
	= pd_data->source_request_obj.power_data_obj_supply_type.supply_type;
	unsigned mismatch, max_min, op, pos;

	if (supply_type == POWER_TYPE_FIXED) {
		pr_info("REQUEST: FIXED\n");
		goto log_fixed_variable;
	} else if (supply_type == POWER_TYPE_VARIABLE) {
		pr_info("REQUEST: VARIABLE\n");
		goto log_fixed_variable;
	} else if (supply_type == POWER_TYPE_BATTERY) {
		pr_info("REQUEST: BATTERY\n");
		goto log_battery;
	} else {
		pr_info("REQUEST: UNKNOWN Supply type.\n");
		return -1;
	}

log_fixed_variable:
	mismatch = pd_data->source_request_obj.request_data_object.capability_mismatch;
	max_min = pd_data->source_request_obj.request_data_object.min_current;
	op = pd_data->source_request_obj.request_data_object.op_current;
	pos = pd_data->source_request_obj.request_data_object.object_position;
	pr_info("Obj position: %d\n", pos);
	pr_info("Mismatch: %d\n", mismatch);
	pr_info("Operating Current: %d mA\n", op*10);
	if (pd_data->source_request_obj.request_data_object.give_back)
		pr_info("Min current: %d mA\n", max_min*10);
	else
		pr_info("Max current: %d mA\n", max_min*10);

	return 0;

log_battery:
	mismatch = pd_data->source_request_obj.request_data_object_battery.capability_mismatch;
	return 0;
}

#ifdef CONFIG_OF
static int of_usbpd_manager_dt(struct usbpd_manager_data *_data)
{
	int ret = 0;
	struct device_node *np =
		of_find_node_by_name(NULL, "pdic-manager");

	if (np == NULL) {
		pr_err("%s np NULL\n", __func__);
		return -EINVAL;
	} else {
		ret = of_property_read_u32(np, "pdic,max_power",
				&_data->max_power);
		if (ret < 0)
			pr_err("%s error reading max_power %d\n",
					__func__, _data->max_power);

		ret = of_property_read_u32(np, "pdic,op_power",
				&_data->op_power);
		if (ret < 0)
			pr_err("%s error reading op_power %d\n",
					__func__, _data->max_power);

		ret = of_property_read_u32(np, "pdic,max_current",
				&_data->max_current);
		if (ret < 0)
			pr_err("%s error reading max_current %d\n",
					__func__, _data->max_current);

		ret = of_property_read_u32(np, "pdic,min_current",
				&_data->min_current);
		if (ret < 0)
			pr_err("%s error reading min_current %d\n",
					__func__, _data->min_current);

		_data->giveback = of_property_read_bool(np,
						     "pdic,giveback");
		_data->usb_com_capable = of_property_read_bool(np,
						     "pdic,usb_com_capable");
		_data->no_usb_suspend = of_property_read_bool(np,
						     "pdic,no_usb_suspend");

		/* source capability */
		ret = of_property_read_u32(np, "source,max_voltage",
				&_data->source_max_volt);
		ret = of_property_read_u32(np, "source,min_voltage",
				&_data->source_min_volt);
		ret = of_property_read_u32(np, "source,max_power",
				&_data->source_max_power);

		/* sink capability */
		ret = of_property_read_u32(np, "sink,capable_max_voltage",
				&_data->sink_cap_max_volt);
		if (ret < 0) {
			_data->sink_cap_max_volt = 5000;
			pr_err("%s error reading sink_cap_max_volt %d\n",
					__func__, _data->sink_cap_max_volt);
		}
	}

	return ret;
}
#endif

void usbpd_init_manager_val(struct usbpd_data *pd_data)
{
	struct usbpd_manager_data *manager = &pd_data->manager;

	pr_info("%s\n", __func__);
	manager->alt_sended = 0;
	manager->Vendor_ID = 0;
	manager->Product_ID = 0;
	manager->Device_Version = 0;
	manager->SVID_0 = 0;
	manager->SVID_1 = 0;
	manager->Standard_Vendor_ID = 0;
}

int usbpd_init_manager(struct usbpd_data *pd_data)
{
	int ret = 0;
	struct usbpd_manager_data *manager = &pd_data->manager;

	pr_info("%s\n", __func__);
	if (manager == NULL) {
		pr_err("%s, usbpd manager data is error!!\n", __func__);
		return -ENOMEM;
	} else
		ret = of_usbpd_manager_dt(manager);
#ifdef CONFIG_BATTERY_SAMSUNG
#ifdef CONFIG_USB_TYPEC_MANAGER_NOTIFIER
	fp_select_pdo = s2mu004_select_pdo;
#endif
#endif
	manager->pd_data = pd_data;
	manager->power_role_swap = true;
	manager->data_role_swap = true;
	manager->vconn_source_swap = true;
	manager->alt_sended = 0;
	manager->acc_type = 0;
	manager->Vendor_ID = 0;
	manager->Product_ID = 0;
	manager->Device_Version = 0;
	manager->SVID_0 = 0;
	manager->SVID_1 = 0;
	manager->Standard_Vendor_ID = 0;

	usbpd_manager_register_switch_device(1);
	init_source_cap_data(manager);
	init_sink_cap_data(manager);
	INIT_DELAYED_WORK(&manager->acc_detach_handler, usbpd_manager_acc_detach_handler);
	pr_info("%s done\n", __func__);
	return ret;
}
