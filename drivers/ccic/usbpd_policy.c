/*
*	USB PD Driver - Policy Engine
*/

#include <linux/device.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/ccic/usbpd.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include <linux/time.h>

#include <linux/muic/muic.h>
#if defined(CONFIG_MUIC_NOTIFIER)
#include <linux/muic/muic_notifier.h>
#endif /* CONFIG_MUIC_NOTIFIER */

#include <linux/usb_notify.h>

#if (defined CONFIG_CCIC_NOTIFIER || defined CONFIG_DUAL_ROLE_USB_INTF)
#include <linux/ccic/usbpd_ext.h>
#endif

#define CHECK_MSG(pd, msg, ret) do {\
	if (pd->phy_ops.get_status(pd, msg))\
		return ret;\
	} while (0);

#define CHECK_CMD(pd, event, ret) do {\
		if (pd->manager.cmd == event) {\
			pd->manager.cmd = 0; \
			return ret;\
		} \
	} while (0);

policy_state usbpd_policy_src_startup(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);
	pd_data->counter.caps_counter = 0;
	usbpd_init_protocol(pd_data);
	return PE_SRC_Send_Capabilities;
}

policy_state usbpd_policy_src_discovery(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);
	msleep(tSendSourceCap);
	if (pd_data->counter.caps_counter <= USBPD_nCapsCount)
		return PE_SRC_Send_Capabilities;
	else
		return PE_SRC_Disabled;
}

policy_state usbpd_policy_src_send_capabilities(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	policy->tx_msg_header.word = pd_data->source_msg_header.word;
	policy->tx_data_obj[0].object = pd_data->source_data_obj.object;

	dev_info(pd_data->dev, "%s\n", __func__);

	pd_data->counter.caps_counter++;
	if (usbpd_send_msg(pd_data, &policy->tx_msg_header,
				policy->tx_data_obj)) {
		pd_data->policy.state = PE_SRC_Send_Capabilities;
		if (usbpd_wait_msg(pd_data, MSG_REQUEST, tSenderResponseSRC)) {
			if (policy->rx_msg_header.msg_type == USBPD_Request &&
				policy->rx_msg_header.num_data_objs > 0) {
				pd_data->counter.hard_reset_counter = 0;
				pd_data->counter.caps_counter = 0;
				pd_data->source_request_obj.object
					= policy->rx_data_obj[0].object;
				dev_info(pd_data->dev, "got Request.\n");
				return PE_SRC_Negotiate_Capability;

			} else {
				dev_err(pd_data->dev,
					"Not get request object\n");
				goto hard_reset;
			}
		} else if (pd_data->phy_ops.get_status(pd_data, MSG_GOODCRC)) {
			if (policy->abnormal_state)
				return PE_SRC_Send_Capabilities;
			pd_data->counter.caps_counter = 0;
			dev_err(pd_data->dev,
				"%s NoResponseTimer\n", __func__);
			goto hard_reset;
		} else {	/* not receive good crc */
			if (policy->abnormal_state)
				return PE_SRC_Send_Capabilities;
			return PE_SRC_Discovery;
		}
	} else
		return PE_SRC_Discovery;

hard_reset:
	if (pd_data->counter.hard_reset_counter > USBPD_nHardResetCount)
		return Error_Recovery;

	return PE_SRC_Hard_Reset;
}
#if 0
policy_state usbpd_policy_src_send_capabilities(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	policy->tx_msg_header.word = pd_data->source_msg_header.word;
	policy->tx_data_obj[0].object = pd_data->source_data_obj.object;

	dev_info(pd_data->dev, "%s 0x%x 0x%x\n", __func__
			, policy->tx_msg_header.word
			, policy->tx_data_obj[0].object);

	pd_data->counter.caps_counter++;
	if (usbpd_send_msg(pd_data, &policy->tx_msg_header,
				policy->tx_data_obj)) {
		pd_data->counter.hard_reset_counter = 0;
		pd_data->counter.caps_counter = 0;
		if (usbpd_wait_msg(pd_data, MSG_REQUEST, tSenderResponse)) {
			if (policy->rx_msg_header.msg_type == USBPD_Request &&
				policy->rx_msg_header.num_data_objs > 0) {
					pd_data->source_request_obj.object
						= policy->rx_data_obj[0].object;
					dev_info(pd_data->dev, "got Request.\n");
					return PE_SRC_Negotiate_Capability;
			} else {
				dev_err(pd_data->dev,
					"Not get request object\n");
				return Error_Recovery;
			}
		} else {
			dev_err(pd_data->dev,
				"%s NoResponseTimer\n", __func__);
			return PE_SRC_Hard_Reset;
		}
	} else
		return PE_SRC_Discovery;

	if (pd_data->counter.hard_reset_counter > USBPD_nHardResetCount)
		return Error_Recovery;

	return PE_SRC_Send_Capabilities;
}
#endif
policy_state usbpd_policy_src_negotiate_capability(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);
	if (usbpd_manager_match_request(pd_data) == 0)
		return PE_SRC_Transition_Supply; /* Accept */
	else
		return PE_SRC_Capability_Response; /* Reject */
}

policy_state usbpd_policy_src_transition_supply(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);
	/* TODO: If GotoMin send GotoMin message */

	if (usbpd_send_ctrl_msg(pd_data, &policy->tx_msg_header, USBPD_Accept,
			USBPD_DFP, USBPD_SOURCE)) {
		msleep(tSrcTransition);

		if (usbpd_send_ctrl_msg(pd_data, &policy->tx_msg_header,
			USBPD_PS_RDY, USBPD_DFP, USBPD_SOURCE))
			return PE_SRC_Ready;
		else
			return PE_SRC_Send_Soft_Reset;
	} else {
		return PE_SRC_Send_Soft_Reset;
	}
	return PE_SRC_Transition_Supply;
}

policy_state usbpd_policy_src_ready(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);
	CHECK_MSG(pd_data, MSG_GET_SRC_CAP, PE_SRC_Give_Source_Cap);
	CHECK_MSG(pd_data, MSG_REQUEST, PE_SRC_Negotiate_Capability);
	CHECK_MSG(pd_data, MSG_PR_SWAP, PE_PRS_SRC_SNK_Evaluate_Swap);
	CHECK_MSG(pd_data, MSG_DR_SWAP, PE_DRS_Evaluate_Port);
	CHECK_MSG(pd_data, MSG_VCONN_SWAP, PE_VCS_Evaluate_Swap);
	CHECK_MSG(pd_data, VDM_DISCOVER_IDENTITY, PE_UFP_VDM_Get_Identity);
	CHECK_MSG(pd_data, VDM_DISCOVER_SVID, PE_UFP_VDM_Get_SVIDs);
	CHECK_MSG(pd_data, VDM_DISCOVER_MODE, PE_UFP_VDM_Get_Modes);
	CHECK_MSG(pd_data, VDM_ENTER_MODE, PE_UFP_VDM_Evaluate_Mode_Entry);
	CHECK_MSG(pd_data, VDM_ATTENTION, PE_DFP_VDM_Attention_Request);
	CHECK_MSG(pd_data, VDM_DP_STATUS_UPDATE, PE_UFP_VDM_Evaluate_Status);
	CHECK_MSG(pd_data, VDM_DP_CONFIGURE, PE_UFP_VDM_Evaluate_Configure);

	CHECK_CMD(pd_data, MANAGER_REQ_GET_SNKCAP, PE_SRC_Get_Sink_Cap);
	CHECK_CMD(pd_data, MANAGER_REQ_GOTOMIN, PE_SRC_Transition_Supply);
	CHECK_CMD(pd_data, MANAGER_REQ_SRCCAP_CHANGE, PE_SRC_Send_Capabilities);
	CHECK_CMD(pd_data, MANAGER_REQ_PR_SWAP, PE_PRS_SRC_SNK_Send_Swap);
	CHECK_CMD(pd_data, MANAGER_REQ_DR_SWAP, PE_DRS_Evaluate_Send_Port);
	CHECK_CMD(pd_data, MANAGER_REQ_VCONN_SWAP, PE_VCS_Send_Swap);
	CHECK_CMD(pd_data, MANAGER_REQ_VDM_DISCOVER_IDENTITY, PE_DFP_VDM_Identity_Request);
	CHECK_CMD(pd_data, MANAGER_REQ_VDM_DISCOVER_SVID, PE_DFP_VDM_SVIDs_Request);
	CHECK_CMD(pd_data, MANAGER_REQ_VDM_DISCOVER_MODE, PE_DFP_VDM_Modes_Request);
	CHECK_CMD(pd_data, MANAGER_REQ_VDM_ENTER_MODE, PE_DFP_VDM_Mode_Entry_Request);
	CHECK_CMD(pd_data, MANAGER_REQ_VDM_STATUS_UPDATE, PE_DFP_VDM_Status_Update);
	CHECK_CMD(pd_data, MANAGER_REQ_VDM_DisplayPort_Configure, PE_DFP_VDM_DisplayPort_Configure);
	CHECK_CMD(pd_data, MANAGER_REQ_VDM_ATTENTION, PE_UFP_VDM_Attention_Request);

/*	for data role swap test
	if (usbpd_manager_vdm_request_enabled(pd_data)) {
		msleep(tDiscoverIdentity);
		return PE_DRS_Evaluate_Send_Port;
	}
*/
	if (usbpd_manager_vdm_request_enabled(pd_data)) {
		msleep(tDiscoverIdentity);
		return PE_DFP_VDM_Identity_Request;
	}

	return PE_SRC_Ready;
}

policy_state usbpd_policy_src_disabled(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);
	return PE_SRC_Disabled;
}

policy_state usbpd_policy_src_capability_response(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	if (usbpd_send_ctrl_msg(pd_data, &policy->tx_msg_header, USBPD_Reject,
				USBPD_DFP, USBPD_SOURCE)) {
		return PE_SRC_Ready;
		/* TODO: if (Contract Invalid)
		   return(PE_SRC_Hard_Reset) */
	}
	/*
	else if (no Explicit Contract && Reject message sent
			|| Wait message sent)
		return(PE_SRC_Wait_New_Capabilities);
	*/
	return PE_SRC_Capability_Response;
}

policy_state usbpd_policy_src_hard_reset(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	msleep(tPSHardReset);

	pd_data->phy_ops.hard_reset(pd_data);
	pd_data->phy_ops.set_cc_control(pd_data, USBPD_CC_OFF);
	pd_data->counter.hard_reset_counter++;

	return PE_SRC_Transition_to_default;
}

policy_state usbpd_policy_src_hard_reset_received(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	msleep(tPSHardReset);

	return PE_SRC_Transition_to_default;
}

policy_state usbpd_policy_src_transition_to_default(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	pd_data->phy_ops.driver_reset(pd_data);
	/*
	Request Device Policy Manager to request power
	supply Hard Resets to vSafe5V via vSafe0V

	If(Type-C request Device Policy Manager to set Port Data Role to DFP)
		turn off VCONN
	*/

	/*
	Request Device Policy Manager to turn on VCONN
	Initialize and start NoResponseTimer
	Inform Protocol Layer Hard Reset complete
	*/
	return PE_SRC_Startup;
}

policy_state usbpd_policy_src_give_source_cap(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	/*
	TODO: Request source capabilities from Device Policy Manager
	Send Capabilities message
	*/

	policy->tx_msg_header.msg_type = USBPD_Source_Capabilities;
	policy->tx_msg_header.port_data_role = USBPD_DFP;
	policy->tx_msg_header.port_power_role = USBPD_SOURCE;
	policy->tx_msg_header.num_data_objs = 1;

	policy->tx_data_obj[0].power_data_obj.max_current = 100;
	policy->tx_data_obj[0].power_data_obj.voltage = 100;
	policy->tx_data_obj[0].power_data_obj.peak_current = 0;
	policy->tx_data_obj[0].power_data_obj.data_role_swap = 1;
	policy->tx_data_obj[0].power_data_obj.usb_comm_capable = 1;
	policy->tx_data_obj[0].power_data_obj.externally_powered = 0;
	policy->tx_data_obj[0].power_data_obj.usb_suspend_support = 1;
	policy->tx_data_obj[0].power_data_obj.dual_role_power = 1;
	policy->tx_data_obj[0].power_data_obj.supply = 0;

	if (usbpd_send_msg(pd_data, &policy->tx_msg_header,
				policy->tx_data_obj))
		return PE_SRC_Ready;
	else
		return PE_SRC_Give_Source_Cap;
}

policy_state usbpd_policy_src_get_sink_cap(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);


	if (usbpd_send_ctrl_msg(pd_data, &policy->tx_msg_header,
				USBPD_Get_Sink_Cap, USBPD_DFP, USBPD_SOURCE))
		pd_data->policy.state = PE_SRC_Get_Sink_Cap;
		if (usbpd_wait_msg(pd_data, MSG_SNK_CAP, tSenderResponse)) {
			/* TODO: pass sink cap to device policy manager */
			dev_info(pd_data->dev, "got SinkCap.\n");
		}

	return PE_SRC_Ready;
}

policy_state usbpd_policy_src_wait_new_capabilities(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	return PE_SRC_Send_Capabilities;
}

policy_state usbpd_policy_src_send_soft_reset(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	usbpd_init_protocol(pd_data);

	if (usbpd_send_ctrl_msg(pd_data, &policy->tx_msg_header,
				USBPD_Soft_Reset, USBPD_DFP, USBPD_SOURCE)) {
		pd_data->policy.state = PE_SRC_Send_Soft_Reset;
		if (usbpd_wait_msg(pd_data, MSG_ACCEPT, tSenderResponse))
			return PE_SRC_Send_Capabilities;
		if (policy->abnormal_state)
			return PE_SRC_Send_Soft_Reset;
	}
	return PE_SRC_Hard_Reset;
}

policy_state usbpd_policy_src_soft_reset(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	if (usbpd_send_ctrl_msg(pd_data, &policy->tx_msg_header,
				USBPD_Accept, USBPD_DFP, USBPD_SOURCE))
		return PE_SRC_Send_Capabilities;
	else
		return PE_SRC_Hard_Reset;
}

policy_state usbpd_policy_snk_startup(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);
	usbpd_init_protocol(pd_data);

	pd_data->phy_ops.set_cc_control(pd_data, USBPD_CC_ON);

	return PE_SNK_Discovery;
}

policy_state usbpd_policy_snk_discovery(struct policy_data *policy)
{
	/* TODO: wait vbus */
	/* if coming from HardReset
	   && NoResponseTimer timeout
	   && HardResetCounter <= nHardResetCount,
	   return(PE_SNK_Hard_Reset) */
	return PE_SNK_Wait_for_Capabilities;
}

policy_state usbpd_policy_snk_wait_for_capabilities(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	pd_data->policy.state = PE_SNK_Wait_for_Capabilities;
	if (usbpd_wait_msg(pd_data, MSG_SRC_CAP, tSinkWaitCap))
		return PE_SNK_Evaluate_Capability;
#if !defined(CONFIG_SEC_FACTORY)
	if (policy->abnormal_state)
		return PE_SNK_Wait_for_Capabilities;
	if (pd_data->counter.hard_reset_counter <= USBPD_nHardResetCount) {
		pd_data->counter.hard_reset_counter++;
		return PE_SNK_Wait_for_Capabilities;
	} else
		return Error_Recovery;
#endif
	return PE_SNK_Wait_for_Capabilities;
}

policy_state usbpd_policy_snk_evaluate_capability(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int sink_request_obj_num = 0;

	dev_info(pd_data->dev, "%s\n", __func__);

	usbpd_protocol_rx(pd_data);

#ifdef CONFIG_USB_TYPEC_MANAGER_NOTIFIER
	if (pd_noti.sink_status.selected_pdo_num == 0) {
		pd_noti.sink_status.selected_pdo_num = 1;
		if (policy->sink_cap_received) {
			policy->send_sink_cap = 1;
			policy->sink_cap_received = 0;
		}
	}
#endif
	sink_request_obj_num = usbpd_manager_evaluate_capability(pd_data);

	if (sink_request_obj_num > 0)
		return PE_SNK_Select_Capability;
	else
		return PE_SNK_Hard_Reset;
}

policy_state usbpd_policy_snk_select_capability(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	policy->tx_msg_header.msg_type = USBPD_Request;
	policy->tx_msg_header.port_data_role = USBPD_UFP;
	policy->tx_msg_header.port_power_role = USBPD_SINK;
	policy->tx_msg_header.num_data_objs = 1;

	policy->tx_data_obj[0] = usbpd_manager_select_capability(pd_data);

	if (usbpd_send_msg(pd_data, &policy->tx_msg_header,
				policy->tx_data_obj)) {
		unsigned msg;
		pd_data->policy.state = PE_SNK_Select_Capability;
		msg = usbpd_wait_msg(pd_data, MSG_ACCEPT | MSG_REJECT
				| MSG_WAIT, tSenderResponse);
		if (policy->abnormal_state)
			return PE_SNK_Select_Capability;
		if (msg & MSG_ACCEPT)
			return PE_SNK_Transition_Sink;
		else if (msg & (MSG_REJECT | MSG_WAIT))
			return PE_SNK_Ready;
		else
			return PE_SNK_Hard_Reset;

		/* If no explicit contract
		   policy->state = PE_SNK_Wait_for_Capabilities;
		 */
	}
	return Error_Recovery;
}

policy_state usbpd_policy_snk_transition_sink(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	policy->state = PE_SNK_Transition_Sink;
	if (usbpd_wait_msg(pd_data, MSG_PSRDY, tPSTransition)) {
		dev_info(pd_data->dev, "got PS_READY.\n");
#ifdef CONFIG_USB_TYPEC_MANAGER_NOTIFIER
		pd_noti.sink_status.current_pdo_num = pd_noti.sink_status.selected_pdo_num;
#endif
		return PE_SNK_Ready;
	}

	if (policy->abnormal_state)
		return PE_SNK_Transition_Sink;

	return PE_SNK_Hard_Reset;
}

policy_state usbpd_policy_snk_ready(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	usbpd_manager_plug_attach(pd_data->dev, ATTACHED_DEV_TYPE3_CHARGER_MUIC);

	CHECK_MSG(pd_data, MSG_GET_SNK_CAP, PE_SNK_Give_Sink_Cap);
	CHECK_MSG(pd_data, MSG_SRC_CAP, PE_SNK_Evaluate_Capability);
	CHECK_MSG(pd_data, MSG_PR_SWAP, PE_PRS_SNK_SRC_Evaluate_Swap);
	CHECK_MSG(pd_data, MSG_DR_SWAP, PE_DRS_Evaluate_Port);
	CHECK_MSG(pd_data, MSG_VCONN_SWAP, PE_VCS_Evaluate_Swap);
	CHECK_MSG(pd_data, VDM_DISCOVER_IDENTITY, PE_UFP_VDM_Get_Identity);
	CHECK_MSG(pd_data, VDM_DISCOVER_SVID, PE_UFP_VDM_Get_SVIDs);
	CHECK_MSG(pd_data, VDM_DISCOVER_MODE, PE_UFP_VDM_Get_Modes);
	CHECK_MSG(pd_data, VDM_ENTER_MODE, PE_UFP_VDM_Evaluate_Mode_Entry);
	CHECK_MSG(pd_data, VDM_ATTENTION, PE_DFP_VDM_Attention_Request);
	CHECK_MSG(pd_data, VDM_DP_STATUS_UPDATE, PE_UFP_VDM_Evaluate_Status);
	CHECK_MSG(pd_data, VDM_DP_CONFIGURE, PE_UFP_VDM_Evaluate_Configure);

	CHECK_CMD(pd_data, MANAGER_REQ_NEW_POWER_SRC, PE_SNK_Select_Capability);
	CHECK_CMD(pd_data, MANAGER_REQ_PR_SWAP, PE_PRS_SNK_SRC_Send_Swap);
	CHECK_CMD(pd_data, MANAGER_REQ_DR_SWAP, PE_DRS_Evaluate_Send_Port);
	CHECK_CMD(pd_data, MANAGER_REQ_VCONN_SWAP, PE_VCS_Send_Swap);
#if 0 /* disable function that support dp control */
	CHECK_CMD(pd_data, MANAGER_REQ_VDM_DISCOVER_IDENTITY, PE_DFP_VDM_Identity_Request);
	CHECK_CMD(pd_data, MANAGER_REQ_VDM_DISCOVER_SVID, PE_DFP_VDM_SVIDs_Request);
	CHECK_CMD(pd_data, MANAGER_REQ_VDM_DISCOVER_MODE, PE_DFP_VDM_Modes_Request);
	CHECK_CMD(pd_data, MANAGER_REQ_VDM_ATTENTION, PE_UFP_VDM_Attention_Request);
	CHECK_CMD(pd_data, MANAGER_REQ_VDM_ENTER_MODE, PE_DFP_VDM_Mode_Entry_Request);
	CHECK_CMD(pd_data, MANAGER_REQ_VDM_STATUS_UPDATE, PE_DFP_VDM_Status_Update);
	CHECK_CMD(pd_data, MANAGER_REQ_VDM_DisplayPort_Configure, PE_DFP_VDM_DisplayPort_Configure);
#endif
	return PE_SNK_Ready;
}

policy_state usbpd_policy_snk_hard_reset(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	pd_data->phy_ops.hard_reset(pd_data);
	pd_data->phy_ops.set_cc_control(pd_data, USBPD_CC_OFF);
	/* increase hard reset counter */
	pd_data->counter.hard_reset_counter++;

	return PE_SNK_Transition_to_default;
}

policy_state usbpd_policy_snk_transition_to_default(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	pd_data->phy_ops.driver_reset(pd_data);

	usbpd_manager_turn_off_vconn(pd_data);

/*	pd_data->phy_ops.set_data_role(pd_data, USBPD_UFP);   */

	return PE_SNK_Startup;
}

policy_state usbpd_policy_snk_give_sink_cap(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

#ifdef CONFIG_USB_TYPEC_MANAGER_NOTIFIER
	pd_noti.sink_status.selected_pdo_num = 0;
#endif
	policy->tx_msg_header.word = pd_data->sink_msg_header.word;
	policy->tx_data_obj[0].object = pd_data->sink_data_obj[0].object;
	policy->tx_data_obj[1].object = pd_data->sink_data_obj[1].object;

	policy->sink_cap_received = 1;

	if (usbpd_send_msg(pd_data, &policy->tx_msg_header,
				policy->tx_data_obj))
		return PE_SNK_Ready;
	else
		return PE_SNK_Give_Sink_Cap;
}

policy_state usbpd_policy_snk_get_source_cap(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	if (usbpd_send_ctrl_msg(pd_data, &policy->tx_msg_header,
				USBPD_Get_Source_Cap, USBPD_UFP, USBPD_SINK))
		return PE_SNK_Ready;
	else
		return PE_SNK_Get_Source_Cap;
}

policy_state usbpd_policy_snk_soft_reset(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	if (usbpd_send_ctrl_msg(pd_data, &policy->tx_msg_header,
				USBPD_Accept, USBPD_UFP, USBPD_SINK))
		return PE_SNK_Wait_for_Capabilities;
	else
		return PE_SNK_Hard_Reset;
}

policy_state usbpd_policy_drs_evaluate_port(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int data_role = 0;
	int power_role = 0;

	dev_info(pd_data->dev, "%s\n", __func__);

	if (policy->modal_operation) {
		pd_data->phy_ops.get_power_role(pd_data, &power_role);

		if (power_role == USBPD_SOURCE)
			return PE_SRC_Hard_Reset;
		else
			return PE_SNK_Hard_Reset;
	}

	pd_data->phy_ops.get_data_role(pd_data, &data_role);

	if (data_role == USBPD_DFP)
		return PE_DRS_DFP_UFP_Evaluate_DR_Swap;
	else
		return PE_DRS_UFP_DFP_Evaluate_DR_Swap;
}

policy_state usbpd_policy_drs_evaluate_send_port(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int data_role = 0;
	int power_role = 0;

	dev_info(pd_data->dev, "%s\n", __func__);

	if (policy->modal_operation) {
		pd_data->phy_ops.get_power_role(pd_data, &power_role);

		if (power_role == USBPD_SOURCE)
			return PE_SRC_Hard_Reset;
		else
			return PE_SNK_Hard_Reset;
	}

	pd_data->phy_ops.get_data_role(pd_data, &data_role);

	if (data_role == USBPD_DFP)
		return PE_DRS_DFP_UFP_Send_DR_Swap;
	else
		return PE_DRS_UFP_DFP_Send_DR_Swap;
}

policy_state usbpd_policy_drs_dfp_ufp_evaluate_dr_swap(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	bool drs_ok;

	dev_info(pd_data->dev, "%s\n", __func__);

	drs_ok = usbpd_manager_data_role_swap(pd_data);

	if (drs_ok)
		return PE_DRS_DFP_UFP_Accept_DR_Swap;
	else
		return PE_DRS_DFP_UFP_Reject_DR_Swap;
}

policy_state usbpd_policy_drs_dfp_ufp_accept_dr_swap(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int power_role = 0;

	dev_info(pd_data->dev, "%s\n", __func__);

	pd_data->phy_ops.get_power_role(pd_data, &power_role);

	if (usbpd_send_ctrl_msg(pd_data, &policy->tx_msg_header,
			USBPD_Accept, USBPD_DFP, power_role)) {
		return PE_DRS_DFP_UFP_Change_to_UFP;
	}

	return PE_DRS_DFP_UFP_Accept_DR_Swap;
}

policy_state usbpd_policy_drs_dfp_ufp_change_to_ufp(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int power_role = 0;

	dev_info(pd_data->dev, "%s\n", __func__);

	pd_data->phy_ops.set_data_role(pd_data, USBPD_UFP);
	pd_data->phy_ops.get_power_role(pd_data, &power_role);

	if (power_role == USBPD_SOURCE)
		return PE_SRC_Ready;
	else
		return PE_SNK_Ready;
}

policy_state usbpd_policy_drs_dfp_ufp_send_dr_swap(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int power_role = 0;

	dev_info(pd_data->dev, "%s\n", __func__);
	pd_data->phy_ops.get_power_role(pd_data, &power_role);

	if (usbpd_send_ctrl_msg(pd_data, &policy->tx_msg_header,
				USBPD_DR_Swap, USBPD_DFP, power_role)) {
		unsigned msg;
		pd_data->policy.state = PE_DRS_DFP_UFP_Send_DR_Swap;
		msg = usbpd_wait_msg(pd_data, MSG_ACCEPT | MSG_REJECT
				| MSG_WAIT, tSenderResponse);
		if (policy->abnormal_state)
			return PE_DRS_DFP_UFP_Send_DR_Swap;

		if (msg & MSG_ACCEPT)
			return PE_DRS_DFP_UFP_Change_to_UFP;
	}

	if (power_role == USBPD_SOURCE)
		return PE_SRC_Ready;
	else
		return PE_SNK_Ready;
}

policy_state usbpd_policy_drs_dfp_ufp_reject_dr_swap(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int power_role = 0;

	dev_info(pd_data->dev, "%s\n", __func__);
	pd_data->phy_ops.get_power_role(pd_data, &power_role);

	if (usbpd_send_ctrl_msg(pd_data, &policy->tx_msg_header,
				USBPD_Reject, USBPD_DFP, power_role)) {
		if (power_role == USBPD_SOURCE)
			return PE_SRC_Ready;
		else
			return PE_SNK_Ready;
	}

	return PE_DRS_DFP_UFP_Reject_DR_Swap;
}

policy_state usbpd_policy_drs_ufp_dfp_evaluate_dr_swap(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	bool drs_ok;

	dev_info(pd_data->dev, "%s\n", __func__);

	drs_ok = usbpd_manager_data_role_swap(pd_data);

	if (drs_ok)
		return PE_DRS_UFP_DFP_Accept_DR_Swap;
	else
		return PE_DRS_UFP_DFP_Reject_DR_Swap;
}

policy_state usbpd_policy_drs_ufp_dfp_accept_dr_swap(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int power_role = 0;

	dev_info(pd_data->dev, "%s\n", __func__);
	pd_data->phy_ops.get_power_role(pd_data, &power_role);

	if (usbpd_send_ctrl_msg(pd_data, &policy->tx_msg_header,
				USBPD_Accept, USBPD_UFP, power_role)) {
		return PE_DRS_UFP_DFP_Change_to_DFP;
	}
	return PE_DRS_UFP_DFP_Accept_DR_Swap;
}

policy_state usbpd_policy_drs_ufp_dfp_change_to_dfp(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int power_role = 0;

	dev_info(pd_data->dev, "%s\n", __func__);

	pd_data->phy_ops.set_data_role(pd_data, USBPD_DFP);
	pd_data->phy_ops.get_power_role(pd_data, &power_role);

	if (power_role == USBPD_SOURCE)
		return PE_SRC_Ready;
	else
		return PE_SNK_Ready;
}

policy_state usbpd_policy_drs_ufp_dfp_send_dr_swap(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int power_role = 0;

	dev_info(pd_data->dev, "%s\n", __func__);
	pd_data->phy_ops.get_power_role(pd_data, &power_role);

	if (usbpd_send_ctrl_msg(pd_data, &policy->tx_msg_header,
				USBPD_DR_Swap, USBPD_UFP, power_role)) {
		unsigned msg;
		pd_data->policy.state = PE_DRS_UFP_DFP_Send_DR_Swap;
		msg = usbpd_wait_msg(pd_data, MSG_ACCEPT | MSG_REJECT
				| MSG_WAIT, tSenderResponse);
		if (policy->abnormal_state)
			return PE_DRS_UFP_DFP_Send_DR_Swap;

		if (msg & MSG_ACCEPT)
			return PE_DRS_UFP_DFP_Change_to_DFP;
	}

	if (power_role == USBPD_SOURCE)
		return PE_SRC_Ready;
	else
		return PE_SNK_Ready;
}

policy_state usbpd_policy_drs_ufp_dfp_reject_dr_swap(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int power_role = 0;

	dev_info(pd_data->dev, "%s\n", __func__);
	pd_data->phy_ops.get_power_role(pd_data, &power_role);

	if (usbpd_send_ctrl_msg(pd_data, &policy->tx_msg_header,
				USBPD_Reject, USBPD_UFP, USBPD_SINK)) {
		if (power_role == USBPD_SOURCE)
			return PE_SRC_Ready;
		else
			return PE_SNK_Ready;
	}

	return PE_DRS_UFP_DFP_Reject_DR_Swap;
}

policy_state usbpd_policy_prs_src_snk_reject_pr_swap(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	if (usbpd_send_ctrl_msg(pd_data, &policy->tx_msg_header,
				USBPD_Reject, USBPD_DFP, USBPD_SOURCE))
		return PE_SRC_Ready;

	return PE_PRS_SRC_SNK_Reject_PR_Swap;
}

policy_state usbpd_policy_prs_src_snk_evaluate_swap(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	bool prs_ok;

	dev_info(pd_data->dev, "%s\n", __func__);

	prs_ok = usbpd_manager_power_role_swap(pd_data);

	if (prs_ok)
		return PE_PRS_SRC_SNK_Accept_Swap;
	else
		return PE_PRS_SRC_SNK_Reject_PR_Swap;
}

policy_state usbpd_policy_prs_src_snk_send_swap(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	if (usbpd_send_ctrl_msg(pd_data, &policy->tx_msg_header,
				USBPD_PR_Swap, USBPD_DFP, USBPD_SOURCE)) {
		unsigned msg;

		pd_data->policy.state = PE_PRS_SRC_SNK_Send_Swap;
		msg = usbpd_wait_msg(pd_data, MSG_ACCEPT | MSG_REJECT
				| MSG_WAIT, tSenderResponse);
		if (policy->abnormal_state)
			return PE_PRS_SRC_SNK_Send_Swap;

		if (msg & MSG_ACCEPT)
			return PE_PRS_SRC_SNK_Transition_off;
	}
	return PE_SRC_Ready;
}

policy_state usbpd_policy_prs_src_snk_accept_swap(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	if (usbpd_send_ctrl_msg(pd_data, &policy->tx_msg_header,
				USBPD_Accept, USBPD_DFP, USBPD_SOURCE))
		return PE_PRS_SRC_SNK_Transition_off;

	return PE_PRS_SRC_SNK_Accept_Swap;
}

policy_state usbpd_policy_prs_src_snk_transition_to_off(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	pd_data->phy_ops.set_otg_control(pd_data, 0);

	msleep(150);

	return PE_PRS_SRC_SNK_Assert_Rd;
}

policy_state usbpd_policy_prs_src_snk_assert_rd(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	pd_data->phy_ops.set_power_role(pd_data, USBPD_SINK);

	return PE_PRS_SRC_SNK_Wait_Source_on;
}

policy_state usbpd_policy_prs_src_snk_wait_source_on(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	if (usbpd_send_ctrl_msg(pd_data, &policy->tx_msg_header,
				USBPD_PS_RDY, USBPD_DFP, USBPD_SINK)) {
		pd_data->policy.state = PE_PRS_SRC_SNK_Wait_Source_on;
		if (usbpd_wait_msg(pd_data, MSG_PSRDY, tPSSourceOn)) {
			pd_data->counter.swap_hard_reset_counter = 0;
			dev_info(pd_data->dev, "got PSRDY.\n");
			return PE_SNK_Startup;
		} else {
			if (policy->abnormal_state)
				return PE_PRS_SRC_SNK_Wait_Source_on;
			goto hard_reset;
		}
	}
	return PE_PRS_SRC_SNK_Wait_Source_on;

hard_reset:
	if (pd_data->counter.swap_hard_reset_counter > USBPD_nHardResetCount)
		return Error_Recovery;

	return PE_SNK_Hard_Reset;
}

policy_state usbpd_policy_prs_snk_src_reject_swap(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	if (usbpd_send_ctrl_msg(pd_data, &policy->tx_msg_header,
				USBPD_Reject, USBPD_UFP, USBPD_SINK))
		return PE_SNK_Ready;

	return PE_PRS_SNK_SRC_Reject_Swap;
}

policy_state usbpd_policy_prs_snk_src_evaluate_swap(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	bool prs_ok;
	dev_info(pd_data->dev, "%s\n", __func__);

	prs_ok = usbpd_manager_power_role_swap(pd_data);

	if (prs_ok)
		return PE_PRS_SNK_SRC_Accept_Swap;
	else
		return PE_PRS_SNK_SRC_Reject_Swap;
}

policy_state usbpd_policy_prs_snk_src_send_swap(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	if (usbpd_send_ctrl_msg(pd_data, &policy->tx_msg_header,
				USBPD_PR_Swap, USBPD_UFP, USBPD_SINK)) {
		unsigned msg;

		pd_data->policy.state = PE_PRS_SNK_SRC_Send_Swap;
		msg = usbpd_wait_msg(pd_data, MSG_ACCEPT | MSG_REJECT
				| MSG_WAIT, tSenderResponse);
		if (policy->abnormal_state)
			return PE_PRS_SNK_SRC_Send_Swap;
		if (msg & MSG_ACCEPT)
			return PE_PRS_SNK_SRC_Transition_off;
	}
	return PE_SNK_Ready;
}

policy_state usbpd_policy_prs_snk_src_accept_swap(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	if (usbpd_send_ctrl_msg(pd_data, &policy->tx_msg_header,
				USBPD_Accept, USBPD_DFP, USBPD_SINK))
		return PE_PRS_SNK_SRC_Transition_off;

	return PE_PRS_SNK_SRC_Accept_Swap;
}

policy_state usbpd_policy_prs_snk_src_transition_to_off(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	usbpd_manager_turn_off_power_sink(pd_data);

	pd_data->policy.state = PE_PRS_SNK_SRC_Transition_off;
	if (usbpd_wait_msg(pd_data, MSG_PSRDY, tPSSourceOff)) {
		pd_data->counter.swap_hard_reset_counter = 0;
		dev_info(pd_data->dev, "got PSRDY.\n");
		return PE_PRS_SNK_SRC_Assert_Rp;
	}
	if (policy->abnormal_state)
		return PE_PRS_SNK_SRC_Transition_off;

	if (pd_data->counter.swap_hard_reset_counter > USBPD_nHardResetCount)
		return Error_Recovery;

	return PE_SRC_Hard_Reset;
}

policy_state usbpd_policy_prs_snk_src_assert_rp(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	pd_data->phy_ops.set_power_role(pd_data, USBPD_SOURCE);

	return PE_PRS_SNK_SRC_Source_on;
}

policy_state usbpd_policy_prs_snk_src_source_on(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	pd_data->phy_ops.set_otg_control(pd_data, 1);

	msleep(150);

	if (usbpd_send_ctrl_msg(pd_data, &policy->tx_msg_header,
				USBPD_PS_RDY, USBPD_DFP, USBPD_SOURCE)) {
		msleep(tSwapSourceStart); /* 20ms */
		return PE_SRC_Startup;
	}
	return PE_PRS_SNK_SRC_Source_on;
}

policy_state usbpd_policy_vcs_evaluate_swap(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	bool vcs_ok;

	dev_info(pd_data->dev, "%s\n", __func__);
	vcs_ok = usbpd_manager_vconn_source_swap(pd_data);

	if (vcs_ok)
		return PE_VCS_Accept_Swap;
	else
		return PE_VCS_Reject_VCONN_Swap;
}

policy_state usbpd_policy_vcs_accept_swap(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int vconn_source = 0;
	int power_role = 0;
	int data_role = 0;

	pd_data->phy_ops.get_vconn_source(pd_data, &vconn_source);
	pd_data->phy_ops.get_power_role(pd_data, &power_role);
	pd_data->phy_ops.get_data_role(pd_data, &data_role);

	if (usbpd_send_ctrl_msg(pd_data, &policy->tx_msg_header,
				USBPD_Accept, data_role, power_role)) {
		if (vconn_source)
			return PE_VCS_Wait_for_VCONN;
		else
			return PE_VCS_Turn_On_VCONN;
	}

	return PE_VCS_Accept_Swap;
}

policy_state usbpd_policy_vcs_send_swap(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int vconn_source = 0;
	int power_role = 0;

	pd_data->phy_ops.get_vconn_source(pd_data, &vconn_source);
	pd_data->phy_ops.get_power_role(pd_data, &power_role);

	if (usbpd_send_ctrl_msg(pd_data, &policy->tx_msg_header,
				USBPD_VCONN_Swap, USBPD_DFP, power_role)) {
		if (vconn_source)
			return PE_VCS_Wait_for_VCONN;
		else
			return PE_VCS_Turn_On_VCONN;
	}

	return PE_VCS_Send_Swap;
}

policy_state usbpd_policy_vcs_wait_for_vconn(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	pd_data->policy.state = PE_VCS_Wait_for_VCONN;
	if (usbpd_wait_msg(pd_data, MSG_PSRDY, tVCONNSourceOn)) {
		pd_data->counter.swap_hard_reset_counter = 0;
		dev_info(pd_data->dev, "got PSRDY.\n");
		return PE_VCS_Turn_Off_VCONN;
	}
	if (policy->abnormal_state)
		return PE_VCS_Wait_for_VCONN;

	if (pd_data->counter.swap_hard_reset_counter > USBPD_nHardResetCount)
		return Error_Recovery;

	return PE_SNK_Hard_Reset;
}

policy_state usbpd_policy_vcs_turn_off_vconn(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int power_role = 0;

	pd_data->phy_ops.get_power_role(pd_data, &power_role);

	dev_info(pd_data->dev, "%s\n", __func__);

	pd_data->phy_ops.set_vconn_source(pd_data, USBPD_VCONN_OFF);

	if (power_role == USBPD_SOURCE)
		return PE_SRC_Ready;
	else
		return PE_SNK_Ready;
}

policy_state usbpd_policy_vcs_turn_on_vconn(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	pd_data->phy_ops.set_vconn_source(pd_data, USBPD_VCONN_ON);

	return PE_VCS_Send_PS_RDY;
}

policy_state usbpd_policy_vcs_send_ps_rdy(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int power_role = 0;
	int data_role = 0;

	dev_info(pd_data->dev, "%s\n", __func__);

	pd_data->phy_ops.get_power_role(pd_data, &power_role);
	pd_data->phy_ops.get_data_role(pd_data, &data_role);

	mdelay(5);

	if (usbpd_send_ctrl_msg(pd_data, &policy->tx_msg_header,
				USBPD_PS_RDY, data_role, data_role)) {
		if (power_role == USBPD_SOURCE)
			return PE_SRC_Ready;
		else
			return PE_SNK_Ready;
	}

	return PE_VCS_Send_PS_RDY;
}

policy_state usbpd_policy_vcs_reject_vconn_swap(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int power_role = 0;

	dev_info(pd_data->dev, "%s\n", __func__);

	pd_data->phy_ops.get_power_role(pd_data, &power_role);

	if (usbpd_send_ctrl_msg(pd_data, &policy->tx_msg_header,
				USBPD_Reject, USBPD_DFP, power_role)) {
		if (power_role == USBPD_SOURCE)
			return PE_SRC_Ready;
		else
			return PE_SNK_Ready;
	}

	return PE_VCS_Reject_VCONN_Swap;
}

policy_state usbpd_policy_ufp_vdm_get_identity(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	if (usbpd_manager_get_identity(pd_data) == 0)
		return PE_UFP_VDM_Send_Identity;
	else
		return PE_UFP_VDM_Get_Identity_NAK;
}

policy_state usbpd_policy_ufp_vdm_send_identity(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int power_role = 0;

	dev_info(pd_data->dev, "%s\n", __func__);

	pd_data->phy_ops.get_power_role(pd_data, &power_role);

	policy->tx_msg_header.msg_type = USBPD_Vendor_Defined;
	policy->tx_msg_header.port_data_role = USBPD_UFP;
	policy->tx_msg_header.port_power_role = power_role;
	policy->tx_msg_header.num_data_objs = 1;

	policy->tx_data_obj[0].structured_vdm.svid = PD_SID;
	policy->tx_data_obj[0].structured_vdm.vdm_type = Structured_VDM;
	policy->tx_data_obj[0].structured_vdm.version = 0;
	policy->tx_data_obj[0].structured_vdm.obj_pos = 1;
	policy->tx_data_obj[0].structured_vdm.command_type = Responder_ACK;
	policy->tx_data_obj[0].structured_vdm.command = Discover_Identity;

	/* TODO: data object should be prepared from device manager */

	if (usbpd_send_msg(pd_data, &policy->tx_msg_header,
				policy->tx_data_obj)) {
		if (power_role == USBPD_SINK)
			return PE_SNK_Ready;
		else
			return PE_SRC_Ready;
	}
	return PE_UFP_VDM_Send_Identity;
}

policy_state usbpd_policy_ufp_vdm_get_identity_nak(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int power_role = 0;

	dev_info(pd_data->dev, "%s\n", __func__);

	pd_data->phy_ops.get_power_role(pd_data, &power_role);

	policy->tx_msg_header.msg_type = USBPD_Vendor_Defined;
	policy->tx_msg_header.port_data_role = USBPD_UFP;
	policy->tx_msg_header.port_power_role = power_role;
	policy->tx_msg_header.num_data_objs = 1;

	policy->tx_data_obj[0].structured_vdm.svid = PD_SID;
	policy->tx_data_obj[0].structured_vdm.vdm_type = Structured_VDM;
	policy->tx_data_obj[0].structured_vdm.version = 0;
	policy->tx_data_obj[0].structured_vdm.obj_pos = 1;
	policy->tx_data_obj[0].structured_vdm.command_type = Responder_NAK;
	policy->tx_data_obj[0].structured_vdm.command = Discover_Identity;

	if (usbpd_send_msg(pd_data, &policy->tx_msg_header,
				policy->tx_data_obj)) {
		if (power_role == USBPD_SINK)
			return PE_SNK_Ready;
		else
			return PE_SRC_Ready;
	}
	return PE_UFP_VDM_Get_Identity_NAK;
}

policy_state usbpd_policy_ufp_vdm_get_svids(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	if (usbpd_manager_get_svids(pd_data) == 0)
		return PE_UFP_VDM_Send_SVIDs;
	else
		return PE_UFP_VDM_Get_SVIDs_NAK;

}

policy_state usbpd_policy_ufp_vdm_send_svids(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int power_role = 0;

	dev_info(pd_data->dev, "%s\n", __func__);

	pd_data->phy_ops.get_power_role(pd_data, &power_role);

	policy->tx_msg_header.msg_type = USBPD_Vendor_Defined;
	policy->tx_msg_header.port_data_role = USBPD_UFP;
	policy->tx_msg_header.port_power_role = power_role;
	policy->tx_msg_header.num_data_objs = 2;

	policy->tx_data_obj[0].structured_vdm.svid = PD_SID;
	policy->tx_data_obj[0].structured_vdm.vdm_type = Structured_VDM;
	policy->tx_data_obj[0].structured_vdm.version = 0;
	policy->tx_data_obj[0].structured_vdm.obj_pos = 1;
	policy->tx_data_obj[0].structured_vdm.command_type = Responder_ACK;
	policy->tx_data_obj[0].structured_vdm.command = Discover_SVIDs;

	policy->tx_data_obj[1].vdm_svid.svid_0 = PD_SID;
	policy->tx_data_obj[1].vdm_svid.svid_1 = 0xFF01;

	/* TODO: data object should be prepared from device manager */

	if (usbpd_send_msg(pd_data, &policy->tx_msg_header,
				policy->tx_data_obj)) {
		if (power_role == USBPD_SINK)
			return PE_SNK_Ready;
		else
			return PE_SRC_Ready;
	}

	return PE_UFP_VDM_Send_SVIDs;
}

policy_state usbpd_policy_ufp_vdm_get_svids_nak(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int power_role = 0;

	dev_info(pd_data->dev, "%s\n", __func__);

	pd_data->phy_ops.get_power_role(pd_data, &power_role);

	policy->tx_msg_header.msg_type = USBPD_Vendor_Defined;
	policy->tx_msg_header.port_data_role = USBPD_UFP;
	policy->tx_msg_header.port_power_role = power_role;
	policy->tx_msg_header.num_data_objs = 1;

	policy->tx_data_obj[0].structured_vdm.svid = PD_SID;
	policy->tx_data_obj[0].structured_vdm.vdm_type = Structured_VDM;
	policy->tx_data_obj[0].structured_vdm.version = 0;
	policy->tx_data_obj[0].structured_vdm.obj_pos = 1;
	policy->tx_data_obj[0].structured_vdm.command_type = Responder_NAK;
	policy->tx_data_obj[0].structured_vdm.command = Discover_SVIDs;

	if (usbpd_send_msg(pd_data, &policy->tx_msg_header,
				policy->tx_data_obj)) {
		if (power_role == USBPD_SINK)
			return PE_SNK_Ready;
		else
			return PE_SRC_Ready;
	}
	return PE_UFP_VDM_Get_SVIDs_NAK;
}

policy_state usbpd_policy_ufp_vdm_get_modes(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	if (usbpd_manager_get_modes(pd_data) == 0)
		return PE_UFP_VDM_Send_Modes;
	else
		return PE_UFP_VDM_Get_Modes_NAK;
}

policy_state usbpd_policy_ufp_vdm_send_modes(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int power_role = 0;

	dev_info(pd_data->dev, "%s\n", __func__);

	pd_data->phy_ops.get_power_role(pd_data, &power_role);

	policy->tx_msg_header.msg_type = USBPD_Vendor_Defined;
	policy->tx_msg_header.port_data_role = USBPD_UFP;
	policy->tx_msg_header.port_power_role = power_role;
	policy->tx_msg_header.num_data_objs = 2;

	policy->tx_data_obj[0].structured_vdm.svid = PD_SID;
	policy->tx_data_obj[0].structured_vdm.vdm_type = Structured_VDM;
	policy->tx_data_obj[0].structured_vdm.version = 0;
	policy->tx_data_obj[0].structured_vdm.obj_pos = 1;
	policy->tx_data_obj[0].structured_vdm.command_type = Responder_ACK;
	policy->tx_data_obj[0].structured_vdm.command = Discover_Modes;

	/* TODO: data object should be prepared from device manager */

	if (usbpd_send_msg(pd_data, &policy->tx_msg_header,
				policy->tx_data_obj)) {
		if (power_role == USBPD_SINK)
			return PE_SNK_Ready;
		else
			return PE_SRC_Ready;
	}
	return PE_UFP_VDM_Send_Modes;
}

policy_state usbpd_policy_ufp_vdm_get_modes_nak(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int power_role = 0;

	dev_info(pd_data->dev, "%s\n", __func__);

	pd_data->phy_ops.get_power_role(pd_data, &power_role);

	policy->tx_msg_header.msg_type = USBPD_Vendor_Defined;
	policy->tx_msg_header.port_data_role = USBPD_UFP;
	policy->tx_msg_header.port_power_role = power_role;
	policy->tx_msg_header.num_data_objs = 1;

	policy->tx_data_obj[0].structured_vdm.svid = PD_SID;
	policy->tx_data_obj[0].structured_vdm.vdm_type = Structured_VDM;
	policy->tx_data_obj[0].structured_vdm.version = 0;
	policy->tx_data_obj[0].structured_vdm.obj_pos = 1;
	policy->tx_data_obj[0].structured_vdm.command_type = Responder_NAK;
	policy->tx_data_obj[0].structured_vdm.command = Discover_Modes;

	/* TODO: data object should be prepared from device manager */

	if (usbpd_send_msg(pd_data, &policy->tx_msg_header,
				policy->tx_data_obj)) {
		if (power_role == USBPD_SINK)
			return PE_SNK_Ready;
		else
			return PE_SRC_Ready;
	}
	return PE_UFP_VDM_Get_Modes_NAK;
}

policy_state usbpd_policy_ufp_vdm_evaluate_mode_entry(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	/* Todo
	check DPM evaluate request to enter a mode
	*/
/*
	if (usbpd_manager_enter_mode(pd_data, mode_pos,
				mode_vdo) == 0)
		return PE_UFP_VDM_Mode_Entry_ACK;
	else
		return PE_UFP_VDM_Mode_Entry_NAK;
*/
	return PE_UFP_VDM_Evaluate_Mode_Entry;
}

policy_state usbpd_policy_ufp_vdm_mode_entry_ack(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int power_role = 0;

	dev_info(pd_data->dev, "%s\n", __func__);

	pd_data->phy_ops.get_power_role(pd_data, &power_role);

	policy->tx_msg_header.msg_type = USBPD_Vendor_Defined;
	policy->tx_msg_header.port_data_role = USBPD_UFP;
	policy->tx_msg_header.port_power_role = power_role;
	policy->tx_msg_header.num_data_objs = 1;

	policy->tx_data_obj[0].structured_vdm.svid = PD_SID;
	policy->tx_data_obj[0].structured_vdm.vdm_type = Structured_VDM;
	policy->tx_data_obj[0].structured_vdm.version = 0;
	policy->tx_data_obj[0].structured_vdm.obj_pos = 1;
	policy->tx_data_obj[0].structured_vdm.command_type = Responder_ACK;
	policy->tx_data_obj[0].structured_vdm.command = Enter_Mode;

	if (usbpd_send_msg(pd_data, &policy->tx_msg_header,
				policy->tx_data_obj)) {
		if (power_role == USBPD_SINK)
			return PE_SNK_Ready;
		else
			return PE_SRC_Ready;
	}
	return PE_UFP_VDM_Mode_Entry_ACK;
}

policy_state usbpd_policy_ufp_vdm_mode_entry_nak(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int power_role = 0;

	dev_info(pd_data->dev, "%s\n", __func__);

	pd_data->phy_ops.get_power_role(pd_data, &power_role);

	policy->tx_msg_header.msg_type = USBPD_Vendor_Defined;
	policy->tx_msg_header.port_data_role = USBPD_UFP;
	policy->tx_msg_header.port_power_role = power_role;
	policy->tx_msg_header.num_data_objs = 1;

	policy->tx_data_obj[0].structured_vdm.svid = PD_SID;
	policy->tx_data_obj[0].structured_vdm.vdm_type = Structured_VDM;
	policy->tx_data_obj[0].structured_vdm.version = 0;
	policy->tx_data_obj[0].structured_vdm.obj_pos = 1;
	policy->tx_data_obj[0].structured_vdm.command_type = Responder_NAK;
	policy->tx_data_obj[0].structured_vdm.command = Enter_Mode;

	if (usbpd_send_msg(pd_data, &policy->tx_msg_header,
				policy->tx_data_obj)) {
		if (power_role == USBPD_SINK)
			return PE_SNK_Ready;
		else
			return PE_SRC_Ready;
	}
	return PE_UFP_VDM_Mode_Entry_NAK;
}

policy_state usbpd_policy_ufp_vdm_mode_exit(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	if (pd_data->phy_ops.get_status(pd_data, VDM_EXIT_MODE)) {
		if (policy->rx_data_obj[0].structured_vdm.command
				== Exit_Mode) {
			unsigned mode_pos;

			/* get mode to exit */
			mode_pos = policy->rx_data_obj[0].structured_vdm.obj_pos;
			if (usbpd_manager_exit_mode(pd_data, mode_pos) == 0)
				return PE_UFP_VDM_Mode_Exit_ACK;
			else
				return PE_UFP_VDM_Mode_Exit_NAK;
		}
	}
	return PE_UFP_VDM_Mode_Exit;

}

policy_state usbpd_policy_ufp_vdm_mode_exit_ack(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int power_role = 0;

	dev_info(pd_data->dev, "%s\n", __func__);

	pd_data->phy_ops.get_power_role(pd_data, &power_role);

	policy->tx_msg_header.msg_type = USBPD_Vendor_Defined;
	policy->tx_msg_header.port_data_role = USBPD_UFP;
	policy->tx_msg_header.port_power_role = power_role;
	policy->tx_msg_header.num_data_objs = 1;

	policy->tx_data_obj[0].structured_vdm.svid = PD_SID;
	policy->tx_data_obj[0].structured_vdm.vdm_type = Structured_VDM;
	policy->tx_data_obj[0].structured_vdm.version = 0;
	policy->tx_data_obj[0].structured_vdm.obj_pos = 1;
	policy->tx_data_obj[0].structured_vdm.command_type = Responder_ACK;
	policy->tx_data_obj[0].structured_vdm.command = Exit_Mode;

	if (usbpd_send_msg(pd_data, &policy->tx_msg_header,
				policy->tx_data_obj)) {
		if (power_role == USBPD_SINK)
			return PE_SNK_Ready;
		else
			return PE_SRC_Ready;
	}
	return PE_UFP_VDM_Mode_Exit_NAK;
}

policy_state usbpd_policy_ufp_vdm_mode_exit_nak(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int power_role = 0;

	dev_info(pd_data->dev, "%s\n", __func__);

	pd_data->phy_ops.get_power_role(pd_data, &power_role);

	policy->tx_msg_header.msg_type = USBPD_Vendor_Defined;
	policy->tx_msg_header.port_data_role = USBPD_UFP;
	policy->tx_msg_header.port_power_role = power_role;
	policy->tx_msg_header.num_data_objs = 1;

	policy->tx_data_obj[0].structured_vdm.svid = PD_SID;
	policy->tx_data_obj[0].structured_vdm.vdm_type = Structured_VDM;
	policy->tx_data_obj[0].structured_vdm.version = 0;
	policy->tx_data_obj[0].structured_vdm.obj_pos = 1;
	policy->tx_data_obj[0].structured_vdm.command_type = Responder_NAK;
	policy->tx_data_obj[0].structured_vdm.command = Exit_Mode;

	if (usbpd_send_msg(pd_data, &policy->tx_msg_header,
				policy->tx_data_obj)) {
		if (power_role == USBPD_SINK)
			return PE_SNK_Ready;
		else
			return PE_SRC_Ready;
	}
	return PE_UFP_VDM_Mode_Exit_NAK;
}

policy_state usbpd_policy_ufp_vdm_attention_request(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int power_role = 0;

	dev_info(pd_data->dev, "%s\n", __func__);

	pd_data->phy_ops.get_power_role(pd_data, &power_role);

	policy->tx_msg_header.msg_type = USBPD_Vendor_Defined;
	policy->tx_msg_header.port_data_role = USBPD_UFP;
	policy->tx_msg_header.port_power_role = power_role;
/*	policy->tx_msg_header.num_data_objs = 1; number of objects*/

	policy->tx_data_obj[0].structured_vdm.svid = PD_SID;
	policy->tx_data_obj[0].structured_vdm.vdm_type = Structured_VDM;
	policy->tx_data_obj[0].structured_vdm.version = 0;
	policy->tx_data_obj[0].structured_vdm.obj_pos = 0;
	policy->tx_data_obj[0].structured_vdm.command_type = Initiator;
	policy->tx_data_obj[0].structured_vdm.command = Attention;

	if (usbpd_send_msg(pd_data, &policy->tx_msg_header,
				policy->tx_data_obj)) {
		if (power_role == USBPD_SINK)
			return PE_SNK_Ready;
		else
			return PE_SRC_Ready;
	}
	return PE_UFP_VDM_Attention_Request;

}

policy_state usbpd_policy_ufp_vdm_evaluate_status(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	/* Todo
	check DPM evaluate request to inform status
	*/
/*
	if (usbpd_manager_enter_mode(pd_data, mode_pos,
				mode_vdo) == 0)
		return PE_UFP_VDM_Mode_Entry_ACK;
	else
		return PE_UFP_VDM_Mode_Entry_NAK;
*/
	return PE_UFP_VDM_Evaluate_Status;
}

policy_state usbpd_policy_ufp_vdm_status_ack(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int power_role = 0;

	dev_info(pd_data->dev, "%s\n", __func__);

	pd_data->phy_ops.get_power_role(pd_data, &power_role);

	policy->tx_msg_header.msg_type = USBPD_Vendor_Defined;
	policy->tx_msg_header.port_data_role = USBPD_UFP;
	policy->tx_msg_header.port_power_role = power_role;
	policy->tx_msg_header.num_data_objs = 1;

	policy->tx_data_obj[0].structured_vdm.svid = PD_SID;
	policy->tx_data_obj[0].structured_vdm.vdm_type = Structured_VDM;
	policy->tx_data_obj[0].structured_vdm.version = 0;
	policy->tx_data_obj[0].structured_vdm.obj_pos = 1;
	policy->tx_data_obj[0].structured_vdm.command_type = Responder_ACK;
	policy->tx_data_obj[0].structured_vdm.command = DisplayPort_Status_Update;

	if (usbpd_send_msg(pd_data, &policy->tx_msg_header,
				policy->tx_data_obj)) {
		if (power_role == USBPD_SINK)
			return PE_SNK_Ready;
		else
			return PE_SRC_Ready;
	}
	return PE_UFP_VDM_Status_ACK;
}

policy_state usbpd_policy_ufp_vdm_status_nak(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int power_role = 0;

	dev_info(pd_data->dev, "%s\n", __func__);

	pd_data->phy_ops.get_power_role(pd_data, &power_role);

	policy->tx_msg_header.msg_type = USBPD_Vendor_Defined;
	policy->tx_msg_header.port_data_role = USBPD_UFP;
	policy->tx_msg_header.port_power_role = power_role;
	policy->tx_msg_header.num_data_objs = 1;

	policy->tx_data_obj[0].structured_vdm.svid = PD_SID;
	policy->tx_data_obj[0].structured_vdm.vdm_type = Structured_VDM;
	policy->tx_data_obj[0].structured_vdm.version = 0;
	policy->tx_data_obj[0].structured_vdm.obj_pos = 1;
	policy->tx_data_obj[0].structured_vdm.command_type = Responder_NAK;
	policy->tx_data_obj[0].structured_vdm.command = DisplayPort_Status_Update;

	if (usbpd_send_msg(pd_data, &policy->tx_msg_header,
				policy->tx_data_obj)) {
		if (power_role == USBPD_SINK)
			return PE_SNK_Ready;
		else
			return PE_SRC_Ready;
	}
	return PE_UFP_VDM_Status_NAK;
}

policy_state usbpd_policy_ufp_vdm_evaluate_configure(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	/* Todo
	check DPM evaluate request to inform status
	*/
/*
	if (usbpd_manager_enter_mode(pd_data, mode_pos,
				mode_vdo) == 0)
		return PE_UFP_VDM_Mode_Entry_ACK;
	else
		return PE_UFP_VDM_Mode_Entry_NAK;
*/
	return PE_UFP_VDM_Evaluate_Configure;
}

policy_state usbpd_policy_ufp_vdm_configure_ack(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int power_role = 0;

	dev_info(pd_data->dev, "%s\n", __func__);

	pd_data->phy_ops.get_power_role(pd_data, &power_role);

	policy->tx_msg_header.msg_type = USBPD_Vendor_Defined;
	policy->tx_msg_header.port_data_role = USBPD_UFP;
	policy->tx_msg_header.port_power_role = power_role;
	policy->tx_msg_header.num_data_objs = 1;

	policy->tx_data_obj[0].structured_vdm.svid = PD_SID;
	policy->tx_data_obj[0].structured_vdm.vdm_type = Structured_VDM;
	policy->tx_data_obj[0].structured_vdm.version = 0;
	policy->tx_data_obj[0].structured_vdm.obj_pos = 1;
	policy->tx_data_obj[0].structured_vdm.command_type = Responder_ACK;
	policy->tx_data_obj[0].structured_vdm.command = DisplayPort_Configure;

	if (usbpd_send_msg(pd_data, &policy->tx_msg_header,
				policy->tx_data_obj)) {
		if (power_role == USBPD_SINK)
			return PE_SNK_Ready;
		else
			return PE_SRC_Ready;
	}
	return PE_UFP_VDM_Configure_ACK;
}

policy_state usbpd_policy_ufp_vdm_configure_nak(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int power_role = 0;

	dev_info(pd_data->dev, "%s\n", __func__);

	pd_data->phy_ops.get_power_role(pd_data, &power_role);

	policy->tx_msg_header.msg_type = USBPD_Vendor_Defined;
	policy->tx_msg_header.port_data_role = USBPD_UFP;
	policy->tx_msg_header.port_power_role = power_role;
	policy->tx_msg_header.num_data_objs = 1;

	policy->tx_data_obj[0].structured_vdm.svid = PD_SID;
	policy->tx_data_obj[0].structured_vdm.vdm_type = Structured_VDM;
	policy->tx_data_obj[0].structured_vdm.version = 0;
	policy->tx_data_obj[0].structured_vdm.obj_pos = 1;
	policy->tx_data_obj[0].structured_vdm.command_type = Responder_NAK;
	policy->tx_data_obj[0].structured_vdm.command = DisplayPort_Configure;

	if (usbpd_send_msg(pd_data, &policy->tx_msg_header,
				policy->tx_data_obj)) {
		if (power_role == USBPD_SINK)
			return PE_SNK_Ready;
		else
			return PE_SRC_Ready;
	}
	return PE_UFP_VDM_Configure_NAK;
}

/* the end ufp */

policy_state usbpd_policy_dfp_vdm_identity_request(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int power_role = 0;

	dev_info(pd_data->dev, "%s\n", __func__);

	pd_data->phy_ops.get_power_role(pd_data, &power_role);

	policy->tx_msg_header.msg_type = USBPD_Vendor_Defined;
	policy->tx_msg_header.port_data_role = USBPD_DFP;
	policy->tx_msg_header.port_power_role = power_role;
	policy->tx_msg_header.num_data_objs = 1;

	policy->tx_data_obj[0].structured_vdm.svid = PD_SID;
	policy->tx_data_obj[0].structured_vdm.vdm_type = Structured_VDM;
	policy->tx_data_obj[0].structured_vdm.version = 0;
	policy->tx_data_obj[0].structured_vdm.obj_pos = 1;
	policy->tx_data_obj[0].structured_vdm.command_type = Initiator;
	policy->tx_data_obj[0].structured_vdm.command = Discover_Identity;

	pd_data->counter.discover_identity_counter++;
	if (usbpd_send_msg(pd_data, &policy->tx_msg_header,
				policy->tx_data_obj)) {
		pd_data->policy.state = PE_DFP_VDM_Identity_Request;
		if (usbpd_wait_msg(pd_data, VDM_DISCOVER_IDENTITY,
					tVDMSenderResponse)) {
			pd_data->counter.discover_identity_counter = 0;

			dev_info(pd_data->dev, "Msg header objs(%d)\n",
				policy->rx_msg_header.num_data_objs);
			dev_info(pd_data->dev, "VDM header type(%d)\n",
				policy->rx_data_obj[0].structured_vdm.command_type);
			dev_info(pd_data->dev, "ID Header VDO 0x%x\n",
				policy->rx_data_obj[1].object);
			dev_info(pd_data->dev, "Cert Stat VDO 0x%x\n",
				policy->rx_data_obj[2].object);
			dev_info(pd_data->dev, "Product VDO 0x%x\n",
				policy->rx_data_obj[3].object);

			if (policy->rx_data_obj[0].structured_vdm.command_type
					== Responder_ACK)
				return PE_DFP_VDM_Identity_ACKed;
		}
	}
	return PE_DFP_VDM_Identity_NAKed;
}

static policy_state usbpd_policy_dfp_vdm_response(struct policy_data *policy,
					usbpd_manager_event_type event)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int power_role = 0;

	usbpd_manager_inform_event(pd_data, event);

	if (&pd_data->phy_ops.get_power_role)
		pd_data->phy_ops.get_power_role(pd_data, &power_role);

	if (power_role == USBPD_SINK)
		return PE_SNK_Ready;
	else
		return PE_SRC_Ready;
}

policy_state usbpd_policy_dfp_vdm_identity_acked(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	return usbpd_policy_dfp_vdm_response(policy,
				MANAGER_DISCOVER_IDENTITY_ACKED);
}

policy_state usbpd_policy_dfp_vdm_identity_naked(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	return usbpd_policy_dfp_vdm_response(policy,
				MANAGER_DISCOVER_IDENTITY_NAKED);
}

policy_state usbpd_policy_dfp_vdm_svids_request(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int power_role = 0;

	dev_info(pd_data->dev, "%s\n", __func__);

	pd_data->phy_ops.get_power_role(pd_data, &power_role);

	policy->tx_msg_header.msg_type = USBPD_Vendor_Defined;
	policy->tx_msg_header.port_data_role = USBPD_DFP;
	policy->tx_msg_header.port_power_role = power_role;
	policy->tx_msg_header.num_data_objs = 1;

	policy->tx_data_obj[0].structured_vdm.svid = PD_SID;
	policy->tx_data_obj[0].structured_vdm.vdm_type = Structured_VDM;
	policy->tx_data_obj[0].structured_vdm.version = 0;
	policy->tx_data_obj[0].structured_vdm.obj_pos = 1;
	policy->tx_data_obj[0].structured_vdm.command_type = Initiator;
	policy->tx_data_obj[0].structured_vdm.command = Discover_SVIDs;

	if (usbpd_send_msg(pd_data, &policy->tx_msg_header,
				policy->tx_data_obj)) {
		pd_data->policy.state = PE_DFP_VDM_SVIDs_Request;
		if (usbpd_wait_msg(pd_data, VDM_DISCOVER_SVID,
					tVDMSenderResponse)) {
			if (policy->rx_data_obj[0].structured_vdm.command_type
					== Responder_ACK)
				return PE_DFP_VDM_SVIDs_ACKed;
		}
	}
	return PE_DFP_VDM_SVIDs_NAKed;
}

policy_state usbpd_policy_dfp_vdm_svids_acked(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	return usbpd_policy_dfp_vdm_response(policy,
				MANAGER_DISCOVER_SVID_ACKED);
}

policy_state usbpd_policy_dfp_vdm_svids_naked(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	return usbpd_policy_dfp_vdm_response(policy,
				MANAGER_DISCOVER_SVID_NAKED);
}

policy_state usbpd_policy_dfp_vdm_modes_request(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int power_role = 0;

	dev_info(pd_data->dev, "%s\n", __func__);

	pd_data->phy_ops.get_power_role(pd_data, &power_role);

	policy->tx_msg_header.msg_type = USBPD_Vendor_Defined;
	policy->tx_msg_header.port_data_role = USBPD_DFP;
	policy->tx_msg_header.port_power_role = power_role;
	policy->tx_msg_header.num_data_objs = 1;

	policy->tx_data_obj[0].structured_vdm.svid = PD_SID_1;
	policy->tx_data_obj[0].structured_vdm.vdm_type = Structured_VDM;
	policy->tx_data_obj[0].structured_vdm.version = 0;
	policy->tx_data_obj[0].structured_vdm.obj_pos = 1;
	policy->tx_data_obj[0].structured_vdm.command_type = Initiator;
	policy->tx_data_obj[0].structured_vdm.command = Discover_Modes;

	if (usbpd_send_msg(pd_data, &policy->tx_msg_header,
				policy->tx_data_obj)) {
		pd_data->policy.state = PE_DFP_VDM_Modes_Request;
		if (usbpd_wait_msg(pd_data, VDM_DISCOVER_MODE,
					tVDMSenderResponse)) {
			if (policy->rx_data_obj[0].structured_vdm.command_type
					== Responder_ACK)
				return PE_DFP_VDM_Modes_ACKed;
		}
	}
	return PE_DFP_VDM_Modes_NAKed;
}

policy_state usbpd_policy_dfp_vdm_modes_acked(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	return usbpd_policy_dfp_vdm_response(policy,
				MANAGER_DISCOVER_MODE_ACKED);
}

policy_state usbpd_policy_dfp_vdm_modes_naked(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	return usbpd_policy_dfp_vdm_response(policy,
				MANAGER_DISCOVER_MODE_NAKED);
}

policy_state usbpd_policy_dfp_vdm_entry_request(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int power_role = 0;

	dev_info(pd_data->dev, "%s\n", __func__);

	pd_data->phy_ops.get_power_role(pd_data, &power_role);

	policy->tx_msg_header.msg_type = USBPD_Vendor_Defined;
	policy->tx_msg_header.port_data_role = USBPD_DFP;
	policy->tx_msg_header.port_power_role = power_role;
	policy->tx_msg_header.num_data_objs = 1;

	policy->tx_data_obj[0].object = 0;
	policy->tx_data_obj[0].structured_vdm.svid = PD_SID_1;
	policy->tx_data_obj[0].structured_vdm.vdm_type = Structured_VDM;
	policy->tx_data_obj[0].structured_vdm.version = 0;
	policy->tx_data_obj[0].structured_vdm.obj_pos = 1;/* Todo select which_mode */
	policy->tx_data_obj[0].structured_vdm.command_type = Initiator;
	policy->tx_data_obj[0].structured_vdm.command = Enter_Mode;

	/* TODO: obj_pos , vdo should be set by device manager */

	if (usbpd_send_msg(pd_data, &policy->tx_msg_header,
				policy->tx_data_obj)) {
		pd_data->policy.state = PE_DFP_VDM_Mode_Entry_Request;
		if (usbpd_wait_msg(pd_data, VDM_ENTER_MODE,
					tVDMWaitModeEntry)) {
			if (policy->rx_data_obj[0].structured_vdm.command_type
					== Responder_ACK)
				return PE_DFP_VDM_Mode_Entry_ACKed;
		}
	}
	return PE_DFP_VDM_Mode_Entry_NAKed;
}

policy_state usbpd_policy_dfp_vdm_entry_acked(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	return usbpd_policy_dfp_vdm_response(policy, MANAGER_ENTER_MODE_ACKED);
}

policy_state usbpd_policy_dfp_vdm_entry_naked(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	return usbpd_policy_dfp_vdm_response(policy, MANAGER_ENTER_MODE_NAKED);
}

policy_state usbpd_policy_dfp_vdm_exit_request(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int power_role = 0;

	dev_info(pd_data->dev, "%s\n", __func__);

	pd_data->phy_ops.get_power_role(pd_data, &power_role);

	policy->tx_msg_header.msg_type = USBPD_Vendor_Defined;
	policy->tx_msg_header.port_data_role = USBPD_DFP;
	policy->tx_msg_header.port_power_role = power_role;
	policy->tx_msg_header.num_data_objs = 1;

	policy->tx_data_obj[0].structured_vdm.svid = PD_SID;
	policy->tx_data_obj[0].structured_vdm.vdm_type = Structured_VDM;
	policy->tx_data_obj[0].structured_vdm.version = 0;
/*	policy->tx_data_obj[0].structured_vdm.obj_pos = which_mode; */
	policy->tx_data_obj[0].structured_vdm.command_type = Initiator;
	policy->tx_data_obj[0].structured_vdm.command = Exit_Mode;

	if (usbpd_send_msg(pd_data, &policy->tx_msg_header,
				policy->tx_data_obj)) {
		pd_data->policy.state = PE_DFP_VDM_Mode_Exit_Request;
		if (usbpd_wait_msg(pd_data, VDM_EXIT_MODE,
					tVDMWaitModeExit)) {
			if (policy->rx_data_obj[0].structured_vdm.command_type
					== Responder_ACK)
				return PE_DFP_VDM_Mode_Exit_ACKed;
		}
	}
	return PE_DFP_VDM_Mode_Exit_NAKed;
}

policy_state usbpd_policy_dfp_vdm_exit_acked(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	return usbpd_policy_dfp_vdm_response(policy, MANAGER_EXIT_MODE_ACKED);
}

policy_state usbpd_policy_dfp_vdm_exit_naked(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	return usbpd_policy_dfp_vdm_response(policy, MANAGER_EXIT_MODE_NAKED);
}

policy_state usbpd_policy_dfp_vdm_attention_request(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	return usbpd_policy_dfp_vdm_response(policy, MANAGER_ATTENTION_REQUEST);
}

policy_state usbpd_policy_dfp_vdm_status_update(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int power_role = 0;

	dev_info(pd_data->dev, "%s\n", __func__);

	pd_data->phy_ops.get_power_role(pd_data, &power_role);
	pd_data->phy_ops.set_check_msg_pass(pd_data, CHECK_MSG_PASS);

	dev_info(pd_data->dev, "%s %d\n", __func__, __LINE__);

	policy->tx_msg_header.msg_type = USBPD_Vendor_Defined;
	policy->tx_msg_header.port_data_role = USBPD_DFP;
	policy->tx_msg_header.port_power_role = power_role;
	policy->tx_msg_header.num_data_objs = 2;

	policy->tx_data_obj[0].object = 0;
	policy->tx_data_obj[0].structured_vdm.svid = PD_SID_1;
	policy->tx_data_obj[0].structured_vdm.vdm_type = Structured_VDM;
	policy->tx_data_obj[0].structured_vdm.version = 0;
	policy->tx_data_obj[0].structured_vdm.obj_pos = 1;/* Todo select which_mode */
	policy->tx_data_obj[0].structured_vdm.command_type = Initiator;
	policy->tx_data_obj[0].structured_vdm.command = DisplayPort_Status_Update;
	dev_info(pd_data->dev, "%s %d\n", __func__, __LINE__);

	/* second object for vdo */
	policy->tx_data_obj[1].object = 0;
	policy->tx_data_obj[1].displayport_status.port_connected = 1;
	dev_info(pd_data->dev, "%s %d\n", __func__, __LINE__);

	/* TODO: obj_pos , vdo should be set by device manager */

	if (usbpd_send_msg(pd_data, &policy->tx_msg_header,
				policy->tx_data_obj)) {
		pd_data->policy.state = PE_DFP_VDM_Status_Update;
		if (usbpd_wait_msg(pd_data, MSG_PASS,
					tVDMWaitModeEntry)) {
			pd_data->phy_ops.set_check_msg_pass(pd_data, NONE_CHECK_MSG_PASS);
			pr_info("%s : command(%d), command_type(%d), obj_pos(%d), version(%d), vdm_type(%d)\n",
			__func__, policy->rx_data_obj[0].structured_vdm.command,
			policy->rx_data_obj[0].structured_vdm.command_type,
			policy->rx_data_obj[0].structured_vdm.obj_pos,
			policy->rx_data_obj[0].structured_vdm.version,
			policy->rx_data_obj[0].structured_vdm.vdm_type);

			if (policy->rx_data_obj[0].structured_vdm.command_type
					== Responder_ACK)
				return PE_DFP_VDM_Status_Update_ACKed;
		}
	}
	pd_data->phy_ops.set_check_msg_pass(pd_data, NONE_CHECK_MSG_PASS);

	return PE_DFP_VDM_Status_Update_NAKed;
}

policy_state usbpd_policy_dfp_vdm_status_update_acked(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	return usbpd_policy_dfp_vdm_response(policy, MANAGER_STATUS_UPDATE_ACKED);
}

policy_state usbpd_policy_dfp_vdm_status_update_naked(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	return usbpd_policy_dfp_vdm_response(policy, MANAGER_STATUS_UPDATE_NAKED);
}

policy_state usbpd_policy_dfp_vdm_displayport_configure(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);
	int power_role = 0;

	dev_info(pd_data->dev, "%s\n", __func__);

	pd_data->phy_ops.get_power_role(pd_data, &power_role);
	pd_data->phy_ops.set_check_msg_pass(pd_data, CHECK_MSG_PASS);

	policy->tx_msg_header.msg_type = USBPD_Vendor_Defined;
	policy->tx_msg_header.port_data_role = USBPD_DFP;
	policy->tx_msg_header.port_power_role = power_role;
	policy->tx_msg_header.num_data_objs = 2;

	policy->tx_data_obj[0].structured_vdm.svid = PD_SID_1;
	policy->tx_data_obj[0].structured_vdm.vdm_type = Structured_VDM;
	policy->tx_data_obj[0].structured_vdm.version = 0;
	policy->tx_data_obj[0].structured_vdm.obj_pos = 1;/* Todo select which_mode */
	policy->tx_data_obj[0].structured_vdm.command_type = Initiator;
	policy->tx_data_obj[0].structured_vdm.command = DisplayPort_Configure;

	/* second object for vdo */
	policy->tx_data_obj[1].object = 0;
	policy->tx_data_obj[1].displayport_configurations.select_configuration = USB_U_AS_UFP_D;
	policy->tx_data_obj[1].displayport_configurations.displayport_protocol = DP_V_1_3;
	policy->tx_data_obj[1].displayport_configurations.ufp_u_pin_assignment = PIN_ASSIGNMENT_D;

	/* TODO: obj_pos , vdo should be set by device manager */
	if (usbpd_send_msg(pd_data, &policy->tx_msg_header,
				policy->tx_data_obj)) {
		pd_data->policy.state = PE_DFP_VDM_DisplayPort_Configure;
		if (usbpd_wait_msg(pd_data, MSG_PASS,
					tVDMWaitModeEntry)) {
			pd_data->phy_ops.set_check_msg_pass(pd_data, NONE_CHECK_MSG_PASS);
			if (policy->rx_data_obj[0].structured_vdm.command_type
					== Responder_ACK)
				return PE_DFP_VDM_DisplayPort_Configure_ACKed;
		}
	}
	pd_data->phy_ops.set_check_msg_pass(pd_data, NONE_CHECK_MSG_PASS);

	return PE_DFP_VDM_DisplayPort_Configure_NAKed;
}

policy_state usbpd_policy_dfp_vdm_displayport_configure_acked(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	return usbpd_policy_dfp_vdm_response(policy, MANAGER_DisplayPort_Configure_ACKED);
}

policy_state usbpd_policy_dfp_vdm_displayport_configure_naked(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_info(pd_data->dev, "%s\n", __func__);

	return usbpd_policy_dfp_vdm_response(policy, MANAGER_DisplayPort_Configure_NACKED);
}

policy_state usbpd_error_recovery(struct policy_data *policy)
{
	struct usbpd_data *pd_data = policy_to_usbpd(policy);

	dev_err(pd_data->dev, "%s\n", __func__);

	return Error_Recovery;
}

void usbpd_policy_work(struct work_struct *work)
{
	struct usbpd_data *pd_data = container_of(work, struct usbpd_data,
			worker);
	struct policy_data *policy = &pd_data->policy;
	int power_role = 0;
	policy_state next_state = policy->state;
	policy_state saved_state;

	do {
		if (!policy->plug_valid) {
			pr_info("%s : usbpd cable is empty\n", __func__);
			break;
		}

		if (policy->rx_hardreset || policy->rx_softreset
				|| policy->plug) {
			saved_state = 0;
			next_state = 0; /* default */
		}
		saved_state = next_state;
		switch (next_state) {
		case PE_SRC_Startup:
			next_state = usbpd_policy_src_startup(policy);
			store_usblog_notify(NOTIFY_FUNCSTATE, (void *)&next_state, NULL);
			break;
		case PE_SRC_Discovery:
			next_state = usbpd_policy_src_discovery(policy);
			break;
		case PE_SRC_Send_Capabilities:
			next_state = usbpd_policy_src_send_capabilities(policy);
			break;
		case PE_SRC_Negotiate_Capability:
			next_state = usbpd_policy_src_negotiate_capability(policy);
			break;
		case PE_SRC_Transition_Supply:
			next_state = usbpd_policy_src_transition_supply(policy);
			break;
		case PE_SRC_Ready:
			next_state = usbpd_policy_src_ready(policy);
			store_usblog_notify(NOTIFY_FUNCSTATE, (void *)&next_state, NULL);
			break;
		case PE_SRC_Disabled:
			next_state = usbpd_policy_src_disabled(policy);
			store_usblog_notify(NOTIFY_FUNCSTATE, (void *)&next_state, NULL);
			break;
		case PE_SRC_Capability_Response:
			next_state = usbpd_policy_src_capability_response(policy);
			break;
		case PE_SRC_Hard_Reset:
			next_state = usbpd_policy_src_hard_reset(policy);
			store_usblog_notify(NOTIFY_FUNCSTATE, (void *)&next_state, NULL);
			break;
		case PE_SRC_Hard_Reset_Received:
			next_state = usbpd_policy_src_hard_reset_received(policy);
			store_usblog_notify(NOTIFY_FUNCSTATE, (void *)&next_state, NULL);
			break;
		case PE_SRC_Transition_to_default:
			next_state = usbpd_policy_src_transition_to_default(policy);
			break;
		case PE_SRC_Give_Source_Cap:
			next_state = usbpd_policy_src_give_source_cap(policy);
			break;
		case PE_SRC_Get_Sink_Cap:
			next_state = usbpd_policy_src_get_sink_cap(policy);
			break;
		case PE_SRC_Wait_New_Capabilities:
			next_state = usbpd_policy_src_wait_new_capabilities(policy);
			break;
		case PE_SRC_Send_Soft_Reset:
			next_state = usbpd_policy_src_send_soft_reset(policy);
			break;
		case PE_SRC_Soft_Reset:
			next_state = usbpd_policy_src_soft_reset(policy);
			store_usblog_notify(NOTIFY_FUNCSTATE, (void *)&next_state, NULL);
			break;

		case PE_SNK_Startup:
			next_state = usbpd_policy_snk_startup(policy);
			store_usblog_notify(NOTIFY_FUNCSTATE, (void *)&next_state, NULL);
			break;
		case PE_SNK_Discovery:
			next_state = usbpd_policy_snk_discovery(policy);
			break;
		case PE_SNK_Wait_for_Capabilities:
			next_state = usbpd_policy_snk_wait_for_capabilities(policy);
			break;
		case PE_SNK_Evaluate_Capability:
			next_state = usbpd_policy_snk_evaluate_capability(policy);
			break;
		case PE_SNK_Select_Capability:
			next_state = usbpd_policy_snk_select_capability(policy);
			break;
		case PE_SNK_Transition_Sink:
			next_state = usbpd_policy_snk_transition_sink(policy);
			break;
		case PE_SNK_Ready:
			next_state = usbpd_policy_snk_ready(policy);
			store_usblog_notify(NOTIFY_FUNCSTATE, (void *)&next_state, NULL);
			break;
		case PE_SNK_Hard_Reset:
			next_state = usbpd_policy_snk_hard_reset(policy);
			store_usblog_notify(NOTIFY_FUNCSTATE, (void *)&next_state, NULL);
			break;
		case PE_SNK_Transition_to_default:
			next_state = usbpd_policy_snk_transition_to_default(policy);
			store_usblog_notify(NOTIFY_FUNCSTATE, (void *)&next_state, NULL);
			break;
		case PE_SNK_Give_Sink_Cap:
			next_state = usbpd_policy_snk_give_sink_cap(policy);
			break;
		case PE_SNK_Get_Source_Cap:
			next_state = usbpd_policy_snk_get_source_cap(policy);
			break;
		case PE_SNK_Soft_Reset:
			next_state = usbpd_policy_snk_soft_reset(policy);
			store_usblog_notify(NOTIFY_FUNCSTATE, (void *)&next_state, NULL);
			break;

		case PE_DRS_Evaluate_Port:
			next_state = usbpd_policy_drs_evaluate_port(policy);
			break;
		case PE_DRS_Evaluate_Send_Port:
			next_state = usbpd_policy_drs_evaluate_send_port(policy);
			break;
		case PE_DRS_DFP_UFP_Evaluate_DR_Swap:
			next_state = usbpd_policy_drs_dfp_ufp_evaluate_dr_swap(policy);
			break;
		case PE_DRS_DFP_UFP_Accept_DR_Swap:
			next_state = usbpd_policy_drs_dfp_ufp_accept_dr_swap(policy);
			store_usblog_notify(NOTIFY_FUNCSTATE, (void *)&next_state, NULL);
			break;
		case PE_DRS_DFP_UFP_Change_to_UFP:
			next_state = usbpd_policy_drs_dfp_ufp_change_to_ufp(policy);
			break;
		case PE_DRS_DFP_UFP_Send_DR_Swap:
			next_state = usbpd_policy_drs_dfp_ufp_send_dr_swap(policy);
			store_usblog_notify(NOTIFY_FUNCSTATE, (void *)&next_state, NULL);
			break;
		case PE_DRS_DFP_UFP_Reject_DR_Swap:
			next_state = usbpd_policy_drs_dfp_ufp_reject_dr_swap(policy);
			break;
		case PE_DRS_UFP_DFP_Evaluate_DR_Swap:
			next_state = usbpd_policy_drs_ufp_dfp_evaluate_dr_swap(policy);
			break;
		case PE_DRS_UFP_DFP_Accept_DR_Swap:
			next_state = usbpd_policy_drs_ufp_dfp_accept_dr_swap(policy);
			store_usblog_notify(NOTIFY_FUNCSTATE, (void *)&next_state, NULL);
			break;
		case PE_DRS_UFP_DFP_Change_to_DFP:
			next_state = usbpd_policy_drs_ufp_dfp_change_to_dfp(policy);
			break;
		case PE_DRS_UFP_DFP_Send_DR_Swap:
			next_state = usbpd_policy_drs_ufp_dfp_send_dr_swap(policy);
			store_usblog_notify(NOTIFY_FUNCSTATE, (void *)&next_state, NULL);
			break;
		case PE_DRS_UFP_DFP_Reject_DR_Swap:
			next_state = usbpd_policy_drs_ufp_dfp_reject_dr_swap(policy);
			break;

		case PE_PRS_SRC_SNK_Reject_PR_Swap:
			next_state = usbpd_policy_prs_src_snk_reject_pr_swap(policy);
			break;
		case PE_PRS_SRC_SNK_Evaluate_Swap:
			next_state = usbpd_policy_prs_src_snk_evaluate_swap(policy);
			break;
		case PE_PRS_SRC_SNK_Send_Swap:
			next_state = usbpd_policy_prs_src_snk_send_swap(policy);
			store_usblog_notify(NOTIFY_FUNCSTATE, (void *)&next_state, NULL);
			break;
		case PE_PRS_SRC_SNK_Accept_Swap:
			next_state = usbpd_policy_prs_src_snk_accept_swap(policy);
			store_usblog_notify(NOTIFY_FUNCSTATE, (void *)&next_state, NULL);
			break;
		case PE_PRS_SRC_SNK_Transition_off:
			next_state = usbpd_policy_prs_src_snk_transition_to_off(policy);
			break;
		case PE_PRS_SRC_SNK_Assert_Rd:
			next_state = usbpd_policy_prs_src_snk_assert_rd(policy);
			break;
		case PE_PRS_SRC_SNK_Wait_Source_on:
			next_state = usbpd_policy_prs_src_snk_wait_source_on(policy);
			break;
		case PE_PRS_SNK_SRC_Reject_Swap:
			next_state = usbpd_policy_prs_snk_src_reject_swap(policy);
			break;
		case PE_PRS_SNK_SRC_Evaluate_Swap:
			next_state = usbpd_policy_prs_snk_src_evaluate_swap(policy);
			break;
		case PE_PRS_SNK_SRC_Send_Swap:
			next_state = usbpd_policy_prs_snk_src_send_swap(policy);
			store_usblog_notify(NOTIFY_FUNCSTATE, (void *)&next_state, NULL);
			break;
		case PE_PRS_SNK_SRC_Accept_Swap:
			next_state = usbpd_policy_prs_snk_src_accept_swap(policy);
			store_usblog_notify(NOTIFY_FUNCSTATE, (void *)&next_state, NULL);
			break;
		case PE_PRS_SNK_SRC_Transition_off:
			next_state = usbpd_policy_prs_snk_src_transition_to_off(policy);
			break;
		case PE_PRS_SNK_SRC_Assert_Rp:
			next_state = usbpd_policy_prs_snk_src_assert_rp(policy);
			break;
		case PE_PRS_SNK_SRC_Source_on:
			next_state = usbpd_policy_prs_snk_src_source_on(policy);
			break;
		case PE_VCS_Evaluate_Swap:
			next_state = usbpd_policy_vcs_evaluate_swap(policy);
			break;
		case PE_VCS_Accept_Swap:
			next_state = usbpd_policy_vcs_accept_swap(policy);
			break;
		case PE_VCS_Wait_for_VCONN:
			next_state = usbpd_policy_vcs_wait_for_vconn(policy);
			break;
		case PE_VCS_Turn_Off_VCONN:
			next_state = usbpd_policy_vcs_turn_off_vconn(policy);
			break;
		case PE_VCS_Turn_On_VCONN:
			next_state = usbpd_policy_vcs_turn_on_vconn(policy);
			break;
		case PE_VCS_Send_PS_RDY:
			next_state = usbpd_policy_vcs_send_ps_rdy(policy);
			break;
		case PE_VCS_Send_Swap:
			next_state = usbpd_policy_vcs_send_swap(policy);
			break;
		case PE_VCS_Reject_VCONN_Swap:
			next_state = usbpd_policy_vcs_reject_vconn_swap(policy);
			break;

		case PE_UFP_VDM_Get_Identity:
			next_state = usbpd_policy_ufp_vdm_get_identity(policy);
			break;
		case PE_UFP_VDM_Send_Identity:
			next_state = usbpd_policy_ufp_vdm_send_identity(policy);
			break;
		case PE_UFP_VDM_Get_Identity_NAK:
			next_state = usbpd_policy_ufp_vdm_get_identity_nak(policy);
			break;
		case PE_UFP_VDM_Get_SVIDs:
			next_state = usbpd_policy_ufp_vdm_get_svids(policy);
			break;
		case PE_UFP_VDM_Send_SVIDs:
			next_state = usbpd_policy_ufp_vdm_send_svids(policy);
			break;
		case PE_UFP_VDM_Get_SVIDs_NAK:
			next_state = usbpd_policy_ufp_vdm_get_svids_nak(policy);
			break;
		case PE_UFP_VDM_Get_Modes:
			next_state = usbpd_policy_ufp_vdm_get_modes(policy);
			break;
		case PE_UFP_VDM_Send_Modes:
			next_state = usbpd_policy_ufp_vdm_send_modes(policy);
			break;
		case PE_UFP_VDM_Get_Modes_NAK:
			next_state = usbpd_policy_ufp_vdm_get_modes_nak(policy);
			break;
		case PE_UFP_VDM_Evaluate_Mode_Entry:
			next_state = usbpd_policy_ufp_vdm_evaluate_mode_entry(policy);
			break;
		case PE_UFP_VDM_Mode_Entry_ACK:
			next_state = usbpd_policy_ufp_vdm_mode_entry_ack(policy);
			break;
		case PE_UFP_VDM_Mode_Entry_NAK:
			next_state = usbpd_policy_ufp_vdm_mode_entry_nak(policy);
			break;
		case PE_UFP_VDM_Mode_Exit:
			next_state = usbpd_policy_ufp_vdm_mode_exit(policy);
			break;
		case PE_UFP_VDM_Mode_Exit_ACK:
			next_state = usbpd_policy_ufp_vdm_mode_exit_ack(policy);
			break;
		case PE_UFP_VDM_Mode_Exit_NAK:
			next_state = usbpd_policy_ufp_vdm_mode_exit_nak(policy);
			break;
		case PE_UFP_VDM_Attention_Request:
			next_state = usbpd_policy_ufp_vdm_attention_request(policy);
			break;
		case PE_UFP_VDM_Evaluate_Status:
			next_state = usbpd_policy_ufp_vdm_evaluate_status(policy);
			break;
		case PE_UFP_VDM_Status_ACK:
			next_state = usbpd_policy_ufp_vdm_status_ack(policy);
			break;
		case PE_UFP_VDM_Status_NAK:
			next_state = usbpd_policy_ufp_vdm_status_nak(policy);
			break;
		case PE_UFP_VDM_Evaluate_Configure:
			next_state = usbpd_policy_ufp_vdm_evaluate_configure(policy);
			break;
		case PE_UFP_VDM_Configure_ACK:
			next_state = usbpd_policy_ufp_vdm_configure_ack(policy);
			break;
		case PE_UFP_VDM_Configure_NAK:
			next_state = usbpd_policy_ufp_vdm_configure_nak(policy);
			break;
		case PE_DFP_VDM_Identity_Request:
			next_state = usbpd_policy_dfp_vdm_identity_request(policy);
			break;
		case PE_DFP_VDM_Identity_ACKed:
			next_state = usbpd_policy_dfp_vdm_identity_acked(policy);
			break;
		case PE_DFP_VDM_Identity_NAKed:
			next_state = usbpd_policy_dfp_vdm_identity_naked(policy);
			break;
		case PE_DFP_VDM_SVIDs_Request:
			next_state = usbpd_policy_dfp_vdm_svids_request(policy);
			break;
		case PE_DFP_VDM_SVIDs_ACKed:
			next_state = usbpd_policy_dfp_vdm_svids_acked(policy);
			break;
		case PE_DFP_VDM_SVIDs_NAKed:
			next_state = usbpd_policy_dfp_vdm_svids_naked(policy);
			break;
		case PE_DFP_VDM_Modes_Request:
			next_state = usbpd_policy_dfp_vdm_modes_request(policy);
			break;
		case PE_DFP_VDM_Modes_ACKed:
			next_state = usbpd_policy_dfp_vdm_modes_acked(policy);
			break;
		case PE_DFP_VDM_Modes_NAKed:
			next_state = usbpd_policy_dfp_vdm_modes_naked(policy);
			break;
		case PE_DFP_VDM_Mode_Entry_Request:
			next_state = usbpd_policy_dfp_vdm_entry_request(policy);
			break;
		case PE_DFP_VDM_Mode_Entry_ACKed:
			next_state = usbpd_policy_dfp_vdm_entry_acked(policy);
			break;
		case PE_DFP_VDM_Mode_Entry_NAKed:
			next_state = usbpd_policy_dfp_vdm_entry_naked(policy);
			break;
		case PE_DFP_VDM_Mode_Exit_Request:
			next_state = usbpd_policy_dfp_vdm_exit_request(policy);
			break;
		case PE_DFP_VDM_Mode_Exit_ACKed:
			next_state = usbpd_policy_dfp_vdm_exit_acked(policy);
			break;
		case PE_DFP_VDM_Mode_Exit_NAKed:
			next_state = usbpd_policy_dfp_vdm_exit_naked(policy);
			break;
		case PE_DFP_VDM_Attention_Request:
			next_state = usbpd_policy_dfp_vdm_attention_request(policy);
			break;
		case PE_DFP_VDM_Status_Update:
			next_state = usbpd_policy_dfp_vdm_status_update(policy);
			break;
		case PE_DFP_VDM_Status_Update_ACKed:
			next_state = usbpd_policy_dfp_vdm_status_update_acked(policy);
			break;
		case PE_DFP_VDM_Status_Update_NAKed:
			next_state = usbpd_policy_dfp_vdm_status_update_naked(policy);
			break;
		case PE_DFP_VDM_DisplayPort_Configure:
			next_state = usbpd_policy_dfp_vdm_displayport_configure(policy);
			break;
		case PE_DFP_VDM_DisplayPort_Configure_ACKed:
			next_state = usbpd_policy_dfp_vdm_displayport_configure_acked(policy);
			break;
		case PE_DFP_VDM_DisplayPort_Configure_NAKed:
			next_state = usbpd_policy_dfp_vdm_displayport_configure_naked(policy);
			break;

		case Error_Recovery:
			next_state = usbpd_error_recovery(policy);
			store_usblog_notify(NOTIFY_FUNCSTATE, (void *)&next_state, NULL);
			break;

		default:
			if (&pd_data->phy_ops.get_power_role)
				pd_data->phy_ops.get_power_role(pd_data,
						&power_role);
			pr_info("%s, %d\n", __func__, power_role);

			if (power_role == USBPD_SINK) {
				pr_info("%s, SINK\n", __func__);
				if (policy->rx_hardreset) {
					policy->rx_hardreset = 0;
					next_state = PE_SNK_Transition_to_default;
				} else if (policy->rx_softreset) {
					policy->rx_softreset = 0;
					next_state = PE_SNK_Soft_Reset;
				} else if (policy->plug) {
					policy->plug = 0;
					next_state = PE_SNK_Startup;
				} else {
					next_state = PE_SNK_Startup;
				}
			} else {
				pr_info("%s, SOURCE\n", __func__);
				if (policy->rx_hardreset) {
					policy->rx_hardreset = 0;
					next_state = PE_SRC_Hard_Reset_Received;
				} else if (policy->rx_softreset) {
					policy->rx_softreset = 0;
					next_state = PE_SRC_Soft_Reset;
				} else if (policy->plug) {
					policy->plug = 0;
					next_state = PE_SRC_Startup;
				} else {
					next_state = PE_SRC_Startup;
				}
			}
			break;
		}
	} while (saved_state != next_state);

	policy->state = next_state;
	dev_info(pd_data->dev, "%s Finished\n", __func__);
}

void usbpd_init_policy(struct usbpd_data *pd_data)
{
	int i;
	struct policy_data *policy = &pd_data->policy;

	policy->state = 0;
	policy->rx_hardreset = 0;
	policy->rx_softreset = 0;
	policy->plug = 0;
	policy->rx_msg_header.word = 0;
	policy->tx_msg_header.word = 0;
	policy->modal_operation = 0;
	policy->sink_cap_received = 0;
	policy->send_sink_cap = 0;
	for (i = 0; i < USBPD_MAX_COUNT_MSG_OBJECT; i++) {
		policy->rx_data_obj[i].object = 0;
		policy->tx_data_obj[i].object = 0;
	}
}

void usbpd_kick_policy_work(struct device *dev)
{
	struct usbpd_data *pd_data = dev_get_drvdata(dev);

	schedule_work(&pd_data->worker);
}

