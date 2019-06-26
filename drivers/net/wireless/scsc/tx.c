/******************************************************************************
 *
 * Copyright (c) 2012 - 2017 Samsung Electronics Co., Ltd. All rights reserved
 *
 *****************************************************************************/

#include "dev.h"
#include "debug.h"
#include "mgt.h"
#include "mlme.h"
#include "netif.h"
#include "log_clients.h"
#include "hip4_sampler.h"

#include "scsc_wifilogger_rings.h"

/**
 * Needed to get HIP4_DAT)SLOTS...should be part
 * of initialization and callbacks registering
 */
#include "hip4.h"

#include <linux/spinlock.h>

static int slsi_tx_eapol(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb)
{
	struct netdev_vif	*ndev_vif = netdev_priv(dev);
	struct slsi_peer	*peer;
	u8			*eapol = NULL;
	u16			msg_type = 0;
	u16			proto = ntohs(skb->protocol);
	int			ret = 0;
	u32              dwell_time = sdev->fw_dwell_time;
	u64			tx_bytes_tmp = 0;

	slsi_spinlock_lock(&ndev_vif->peer_lock);
	peer = slsi_get_peer_from_mac(sdev, dev, eth_hdr(skb)->h_dest);
	if (!peer) {
		slsi_spinlock_unlock(&ndev_vif->peer_lock);
		SLSI_WARN(sdev, "no peer record for %pM, dropping EAP frame\n", eth_hdr(skb)->h_dest);
		return -EINVAL;
	}

	switch (proto) {
	case ETH_P_PAE:
		 /*  Detect if this is an EAPOL key frame. If so detect if
		 *  it is an EAPOL-Key M4 packet
		 *  In M4 packet,
		 *   - MIC bit set in key info
		 *   - Key type bit set in key info (pairwise=1, Group=0)
		 *   - Key Data Length would be 0
		 */
		if ((skb->len + sizeof(struct ethhdr)) >= 99)
			eapol = skb->data + sizeof(struct ethhdr);
		if (eapol && eapol[SLSI_EAPOL_IEEE8021X_TYPE_POS] == SLSI_IEEE8021X_TYPE_EAPOL_KEY) {
			msg_type = FAPI_MESSAGETYPE_EAPOL_KEY_M123;

			if ((eapol[SLSI_EAPOL_TYPE_POS] == SLSI_EAPOL_TYPE_RSN_KEY || eapol[SLSI_EAPOL_TYPE_POS] == SLSI_EAPOL_TYPE_WPA_KEY) &&
			    (eapol[SLSI_EAPOL_KEY_INFO_LOWER_BYTE_POS] & SLSI_EAPOL_KEY_INFO_KEY_TYPE_BIT_IN_LOWER_BYTE) &&
			    (eapol[SLSI_EAPOL_KEY_INFO_HIGHER_BYTE_POS] & SLSI_EAPOL_KEY_INFO_MIC_BIT_IN_HIGHER_BYTE) &&
			    (eapol[SLSI_EAPOL_KEY_DATA_LENGTH_HIGHER_BYTE_POS] == 0) &&
			    (eapol[SLSI_EAPOL_KEY_DATA_LENGTH_LOWER_BYTE_POS] == 0)) {
				SLSI_INFO(sdev, "Send 4way-H/S, M4\n");
				msg_type = FAPI_MESSAGETYPE_EAPOL_KEY_M4;
				dwell_time = 0;
			} else if (msg_type == FAPI_MESSAGETYPE_EAPOL_KEY_M123) {
				if (!(eapol[SLSI_EAPOL_KEY_INFO_HIGHER_BYTE_POS] &
				      SLSI_EAPOL_KEY_INFO_MIC_BIT_IN_HIGHER_BYTE))
					SLSI_INFO(sdev, "Send 4way-H/S, M1\n");
				else if (eapol[SLSI_EAPOL_KEY_INFO_HIGHER_BYTE_POS] &
					 SLSI_EAPOL_KEY_INFO_SECURE_BIT_IN_HIGHER_BYTE)
					SLSI_INFO(sdev, "Send 4way-H/S, M3\n");
				else
					SLSI_INFO(sdev, "Send 4way-H/S, M2\n");
			}
		} else {
			msg_type = FAPI_MESSAGETYPE_EAP_MESSAGE;
			dwell_time = 0;
		}
	break;
	case ETH_P_WAI:
		SLSI_NET_DBG1(dev, SLSI_MLME, "WAI protocol frame\n");
		msg_type = FAPI_MESSAGETYPE_WAI_MESSAGE;
		if ((skb->data[17]) != 9) /*subtype 9 refers to unicast negotiation response*/
			dwell_time = 0;
	break;
	default:
		SLSI_NET_DBG1(dev, SLSI_MLME, "unsupported protocol\n");
		slsi_spinlock_unlock(&ndev_vif->peer_lock);
		return -EOPNOTSUPP;
	}

	/* EAPOL/WAI frames are send via the MLME */
	tx_bytes_tmp = skb->len; /*len copy to avoid null pointer of skb*/
	ret = slsi_mlme_send_frame_data(sdev, dev, skb, msg_type, 0, dwell_time, 0);
	if (!ret) {
		peer->sinfo.tx_packets++;
		peer->sinfo.tx_bytes += tx_bytes_tmp;
	}
	slsi_spinlock_unlock(&ndev_vif->peer_lock);
	return ret;
}

uint slsi_sg_host_align_mask; /* TODO -- this needs to be resolved! */

/**
 * This function deals with TX of data frames.
 * On success, skbs are properly FREED; on error skb is NO MORE freed.
 *
 * NOTE THAT currently ONLY the following set of err-codes will trigger
 * a REQUEUE and RETRY by upper layers in Kernel NetStack:
 *
 *  -ENOSPC
 */
int slsi_tx_data(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb)
{
	struct slsi_skb_cb  *cb;
	struct netdev_vif   *ndev_vif = netdev_priv(dev);
	struct slsi_peer    *peer;
	struct sk_buff      *original_skb = NULL;
	u16                 len = skb->len;
	int                 ret = 0;
	enum slsi_traffic_q tq;
	u32 dwell_time = 0;
	u8 *frame;
	u32 arp_opcode;
	u32 dhcp_message_type = SLSI_DHCP_MESSAGE_TYPE_INVALID;

	if (slsi_is_test_mode_enabled()) {
		/* This signals is in XML file because parts of the Firmware need the symbols defined by them
		 * but this is actually not handled in wlanlite firmware.
		 */
		SLSI_NET_INFO(dev, "Skip sending signal, WlanLite FW does not support MA_UNITDATA.request\n");
		return -EOPNOTSUPP;
	}

	SLSI_NET_DBG3(dev, SLSI_TX, "queue_mapping:%d\n", skb->queue_mapping);
	SLSI_NET_DBG_HEX(dev, SLSI_TX, skb->data, skb->len < 64 ? skb->len : 64, "\n");

	if (!ndev_vif->activated)
		return -EINVAL;

	if ((ndev_vif->vif_type == FAPI_VIFTYPE_AP) && !ndev_vif->peer_sta_records) {
		SLSI_NET_DBG3(dev, SLSI_TX, "AP with no STAs associated, ignore TX frame\n");
		return -EINVAL;
	}

	/* check if it is an high important frame? At the moment EAPOL, DHCP
	 * and ARP are treated as high important frame and are sent over
	 * MLME for applying special rules in transmission.
	 */
	if (skb->queue_mapping == SLSI_NETIF_Q_PRIORITY) {
		int proto = be16_to_cpu(eth_hdr(skb)->h_proto);

		switch (proto) {
		default:
			/* Only EAP packets and IP frames with DHCP are stored in SLSI_NETIF_Q_PRIORITY */
			SLSI_NET_ERR(dev, "Bad h_proto=0x%x in SLSI_NETIF_Q_PRIORITY\n", proto);
			return -EINVAL;
		case ETH_P_PAE:
		case ETH_P_WAI:
			SLSI_NET_DBG2(dev, SLSI_MLME, "transmit EAP packet from SLSI_NETIF_Q_PRIORITY\n");
			return slsi_tx_eapol(sdev, dev, skb);
		case ETH_P_ARP:
			SLSI_NET_DBG2(dev, SLSI_MLME, "transmit ARP frame from SLSI_NETIF_Q_PRIORITY\n");
			frame = skb->data + sizeof(struct ethhdr);
			arp_opcode = frame[6] << 8 | frame[7];
			if ((arp_opcode == 1) &&
			    memcmp(&frame[14], &frame[24], 4)) { /*opcode 1: ARP request(except gratuitous ARP)*/
				dwell_time = sdev->fw_dwell_time;
			}
			return slsi_mlme_send_frame_data(sdev, dev, skb, FAPI_MESSAGETYPE_ARP, 0, dwell_time, 0);
		case ETH_P_IP:
			if (skb->len >= 285 && slsi_is_dhcp_packet(skb->data) != SLSI_TX_IS_NOT_DHCP) {
				if (skb->data[42] == 1)  /*opcode 1 refers to DHCP discover/request*/
					dwell_time = sdev->fw_dwell_time;
				dhcp_message_type = skb->data[284];
				if (dhcp_message_type == SLSI_DHCP_MESSAGE_TYPE_DISCOVER)
					SLSI_INFO(sdev, "Send DHCP [DISCOVER]\n");
				else if (dhcp_message_type == SLSI_DHCP_MESSAGE_TYPE_OFFER)
					SLSI_INFO(sdev, "Send DHCP [OFFER]\n");
				else if (dhcp_message_type == SLSI_DHCP_MESSAGE_TYPE_REQUEST)
					SLSI_INFO(sdev, "Send DHCP [REQUEST]\n");
				else if (dhcp_message_type == SLSI_DHCP_MESSAGE_TYPE_DECLINE)
					SLSI_INFO(sdev, "Send DHCP [DECLINE]\n");
				else if (dhcp_message_type == SLSI_DHCP_MESSAGE_TYPE_ACK)
					SLSI_INFO(sdev, "Send DHCP [ACK]\n");
				else if (dhcp_message_type == SLSI_DHCP_MESSAGE_TYPE_NAK)
					SLSI_INFO(sdev, "Send DHCP [NAK]\n");
				else if (dhcp_message_type == SLSI_DHCP_MESSAGE_TYPE_RELEASE)
					SLSI_INFO(sdev, "Send DHCP [RELEASE]\n");
				else if (dhcp_message_type == SLSI_DHCP_MESSAGE_TYPE_INFORM)
					SLSI_INFO(sdev, "Send DHCP [INFORM]\n");
				else if (dhcp_message_type == SLSI_DHCP_MESSAGE_TYPE_FORCERENEW)
					SLSI_INFO(sdev, "Send DHCP [FORCERENEW]\n");
				else
					SLSI_INFO(sdev, "Send DHCP [INVALID]\n");
				return slsi_mlme_send_frame_data(sdev, dev, skb, FAPI_MESSAGETYPE_DHCP, 0, dwell_time,
								 0);
			}
			/* IP frame can have only DHCP packet in SLSI_NETIF_Q_PRIORITY */
			SLSI_NET_ERR(dev, "Bad IP frame in SLSI_NETIF_Q_PRIORITY\n");
			return -EINVAL;
		}
	}

	if (skb_headroom(skb) < (fapi_sig_size(ma_unitdata_req) + 160)) {
		struct sk_buff *skb2 = NULL;

		skb2 = slsi_skb_realloc_headroom(skb, fapi_sig_size(ma_unitdata_req) + 160);
		if (!skb2)
			return -EINVAL;
		/* Keep track of this copy...*/
		original_skb = skb;
		skb = skb2;
	}

	/* Align mac_header with skb->data */
	if (skb_headroom(skb) != skb->mac_header)
		skb_pull(skb, skb->mac_header - skb_headroom(skb));

	len = skb->len;

	(void)skb_push(skb, fapi_sig_size(ma_unitdata_req));
	tq = slsi_frame_priority_to_ac_queue(skb->priority);
	fapi_set_u16(skb, id,           MA_UNITDATA_REQ);
	fapi_set_u16(skb, receiver_pid, 0);
	fapi_set_u16(skb, sender_pid,   SLSI_TX_PROCESS_ID_MIN);
	fapi_set_u32(skb, fw_reference, 0);
	fapi_set_u16(skb, u.ma_unitdata_req.vif, ndev_vif->ifnum);
	fapi_set_u16(skb, u.ma_unitdata_req.host_tag, slsi_tx_host_tag(sdev, tq));
	fapi_set_u16(skb, u.ma_unitdata_req.peer_index, MAP_QS_TO_AID(slsi_netif_get_qs_from_queue
			(skb->queue_mapping, tq)));

	SCSC_HIP4_SAMPLER_PKT_TX(sdev->minor_prof, fapi_get_u16(skb, u.ma_unitdata_req.host_tag));

	/* by default the priority is set to contention. It is overridden and set appropriate
	 * priority if peer supports QoS. The broadcast/multicast frames are sent in non-QoS .
	 */
	fapi_set_u16(skb, u.ma_unitdata_req.priority,                FAPI_PRIORITY_CONTENTION);

	/* If TCP Ack suppression and robustness is enabled, TCP Acks are marked in SKB control block.
	 * If it is a TCP Ack, mark them so in FAPI signal
	 */
	if (slsi_skb_cb_get(skb)->frame_format == SLSI_NETIF_FRAME_TCP_ACK)
		fapi_set_u16(skb, u.ma_unitdata_req.data_unit_descriptor,    FAPI_DATAUNITDESCRIPTOR_TCP_ACK);
	else
		fapi_set_u16(skb, u.ma_unitdata_req.data_unit_descriptor,    FAPI_DATAUNITDESCRIPTOR_IEEE802_3_FRAME);

	SLSI_NET_DBG_HEX(dev, SLSI_TX, skb->data, skb->len < 128 ? skb->len : 128, "\n");

	cb = slsi_skb_cb_init(skb);
	cb->sig_length = fapi_sig_size(ma_unitdata_req);
	cb->data_length = skb->len;
	/* colour is defined as: */
	/* u16 register bits:
	 * 0      - do not use
	 * [2:1]  - vif
	 * [7:3]  - peer_index
	 * [10:8] - ac queue
	 */
	cb->colour = (slsi_frame_priority_to_ac_queue(skb->priority) << 8) |
		(fapi_get_u16(skb, u.ma_unitdata_req.peer_index) << 3) | ndev_vif->ifnum << 1;

	/* Log only the linear skb chunk ... unidata anywya will be truncated to 100.*/
	SCSC_WLOG_PKTFATE_LOG_TX_DATA_FRAME(fapi_get_u16(skb, u.ma_unitdata_req.host_tag),
					    skb->data, skb_headlen(skb));

	/* ACCESS POINT MODE */
	if (ndev_vif->vif_type == FAPI_VIFTYPE_AP) {
		struct ethhdr *ehdr = eth_hdr(skb);

		if (is_multicast_ether_addr(ehdr->h_dest)) {
			ret = scsc_wifi_fcq_transmit_data(dev,
							  &ndev_vif->ap.group_data_qs,
							  slsi_frame_priority_to_ac_queue(skb->priority),
							  sdev,
							  (cb->colour & 0x6) >> 1,
							  (cb->colour & 0xf8) >> 3);
			if (ret < 0) {
				SLSI_NET_DBG3(dev, SLSI_TX, "no fcq for groupcast, dropping TX frame\n");
				/* Free the local copy here ..if any */
				if (original_skb)
					slsi_kfree_skb(skb);
				return ret;
			}
			ret = scsc_wifi_transmit_frame(&sdev->hip4_inst, false, skb);
			if (ret == NETDEV_TX_OK) {
				/**
				 * Frees the original since the copy has already
				 * been freed downstream
				 */
				if (original_skb)
					slsi_kfree_skb(original_skb);
				return ret;
			} else if (ret < 0) {
				/* scsc_wifi_transmit_frame failed, decrement BoT counters */
				scsc_wifi_fcq_receive_data(dev,
							   &ndev_vif->ap.group_data_qs,
							   slsi_frame_priority_to_ac_queue(skb->priority),
							   sdev,
							   (cb->colour & 0x6) >> 1,
							   (cb->colour & 0xf8) >> 3);
				if (original_skb)
					slsi_kfree_skb(skb);
				return ret;
			}
			if (original_skb)
				slsi_kfree_skb(skb);
			return -EIO;
		}
	}
	slsi_spinlock_lock(&ndev_vif->peer_lock);

	peer = slsi_get_peer_from_mac(sdev, dev, eth_hdr(skb)->h_dest);
	if (!peer) {
		slsi_spinlock_unlock(&ndev_vif->peer_lock);
		SLSI_NET_DBG3(dev, SLSI_TX, "no peer record for %02x:%02x:%02x:%02x:%02x:%02x, dropping TX frame\n",
			      eth_hdr(skb)->h_dest[0], eth_hdr(skb)->h_dest[1], eth_hdr(skb)->h_dest[2], eth_hdr(skb)->h_dest[3], eth_hdr(skb)->h_dest[4], eth_hdr(skb)->h_dest[5]);
		if (original_skb)
			slsi_kfree_skb(skb);
		return -EINVAL;
	}
	/**
	 * skb->priority will contain the priority obtained from the IP Diff/Serv field.
	 * The skb->priority field is defined in terms of the FAPI_PRIORITY_* definitions.
	 * For QoS enabled associations, this is the tid and is the value required in
	 * the ma_unitdata_req.priority field. For non-QoS assocations, the ma_unitdata_req.
	 * priority field requires FAPI_PRIORITY_CONTENTION.
	 */
	if (peer->qos_enabled)
		fapi_set_u16(skb, u.ma_unitdata_req.priority, skb->priority);

	slsi_debug_frame(sdev, dev, skb, "TX");

	ret = scsc_wifi_fcq_transmit_data(dev, &peer->data_qs,
					  slsi_frame_priority_to_ac_queue(skb->priority),
					  sdev,
					  (cb->colour & 0x6) >> 1,
					  (cb->colour & 0xf8) >> 3);
	if (ret < 0) {
		SLSI_NET_DBG3(dev, SLSI_TX, "no fcq for %02x:%02x:%02x:%02x:%02x:%02x, dropping TX frame\n",
			      eth_hdr(skb)->h_dest[0], eth_hdr(skb)->h_dest[1], eth_hdr(skb)->h_dest[2], eth_hdr(skb)->h_dest[3], eth_hdr(skb)->h_dest[4], eth_hdr(skb)->h_dest[5]);
		slsi_spinlock_unlock(&ndev_vif->peer_lock);
		if (original_skb)
			slsi_kfree_skb(skb);
		return ret;
	}

	/* SKB is owned by scsc_wifi_transmit_frame() unless the transmission is
	 * unsuccesful.
	 */
	ret = scsc_wifi_transmit_frame(&sdev->hip4_inst, false, skb);
	if (ret != NETDEV_TX_OK) {
		/* scsc_wifi_transmit_frame failed, decrement BoT counters */
		scsc_wifi_fcq_receive_data(dev, &peer->data_qs, slsi_frame_priority_to_ac_queue(skb->priority),
					   sdev,
					   (cb->colour & 0x6) >> 1,
					   (cb->colour & 0xf8) >> 3);

		if (ret == -ENOSPC) {
			slsi_spinlock_unlock(&ndev_vif->peer_lock);
			if (original_skb)
				slsi_kfree_skb(skb);
			return ret;
		}
		slsi_spinlock_unlock(&ndev_vif->peer_lock);
		if (original_skb)
			slsi_kfree_skb(skb);
		return -EIO;
	}
	/* Frame has been successfully sent, and freed by lower layers */
	slsi_spinlock_unlock(&ndev_vif->peer_lock);
	/* What about the original if we passed in a copy ? */
	if (original_skb)
		slsi_kfree_skb(original_skb);
	peer->sinfo.tx_packets++;
	peer->sinfo.tx_bytes += len;
	slsi_offline_dbg_printf(sdev, 4, false, "%llu : %-8s : ucast_tx", ktime_to_ns(ktime_get()), "HIP TX");
	return ret;
}

int slsi_tx_data_lower(struct slsi_dev *sdev, struct sk_buff *skb)
{
	struct net_device *dev;
	struct netdev_vif *ndev_vif;
	struct slsi_peer  *peer;
	u16               vif;
	u8                *dest;
	int               ret;
	struct slsi_skb_cb *cb = slsi_skb_cb_get(skb);

	vif = fapi_get_vif(skb);

	switch (fapi_get_u16(skb, u.ma_unitdata_req.data_unit_descriptor)) {
	case FAPI_DATAUNITDESCRIPTOR_IEEE802_3_FRAME:
		if (ntohs(eth_hdr(skb)->h_proto) == ETH_P_PAE || ntohs(eth_hdr(skb)->h_proto) == ETH_P_WAI)
			return slsi_tx_control(sdev, NULL, skb);
		dest = eth_hdr(skb)->h_dest;
		break;

	case FAPI_DATAUNITDESCRIPTOR_AMSDU:
		/* The AMSDU frame type is an AMSDU payload ready to be prepended by
		 * an 802.11 frame header by the firmware. The AMSDU subframe header
		 * is identical to an Ethernet header in terms of addressing, so it
		 * is safe to access the destination address through the ethernet
		 * structure.
		 */
		dest = eth_hdr(skb)->h_dest;
		break;
	case FAPI_DATAUNITDESCRIPTOR_IEEE802_11_FRAME:
		dest = ieee80211_get_DA((struct ieee80211_hdr *)fapi_get_data(skb));
		break;
	default:
		SLSI_ERR(sdev, "data_unit_descriptor incorrectly set (0x%02x), dropping TX frame\n",
			 fapi_get_u16(skb, u.ma_unitdata_req.data_unit_descriptor));
		return -EINVAL;
	}

	rcu_read_lock();
	dev = slsi_get_netdev_rcu(sdev, vif);
	if (!dev) {
		SLSI_ERR(sdev, "netdev(%d) No longer exists\n", vif);
		rcu_read_unlock();
		return -EINVAL;
	}

	ndev_vif = netdev_priv(dev);
	rcu_read_unlock();

	if (is_multicast_ether_addr(dest) && ((ndev_vif->vif_type == FAPI_VIFTYPE_AP))) {
		if (scsc_wifi_fcq_transmit_data(dev, &ndev_vif->ap.group_data_qs,
						slsi_frame_priority_to_ac_queue(skb->priority),
						sdev,
						(cb->colour & 0x6) >> 1,
						(cb->colour & 0xf8) >> 3) < 0) {
			SLSI_NET_DBG3(dev, SLSI_TX, "no fcq for groupcast, dropping TX frame\n");
			return -EINVAL;
		}
		ret = scsc_wifi_transmit_frame(&sdev->hip4_inst, false, skb);
		if (ret == NETDEV_TX_OK)
			return ret;
		/**
		 * This should be NEVER RETRIED/REQUEUED and its' handled
		 * by the caller in UDI cdev_write
		 */
		if (ret == -ENOSPC)
			SLSI_NET_DBG1(dev, SLSI_TX, "TX_LOWER...Queue Full... BUT Dropping packet\n");
		else
			SLSI_NET_DBG1(dev, SLSI_TX, "TX_LOWER...Generic Error...Dropping packet\n");
		/* scsc_wifi_transmit_frame failed, decrement BoT counters */
		scsc_wifi_fcq_receive_data(dev, &ndev_vif->ap.group_data_qs,
					   slsi_frame_priority_to_ac_queue(skb->priority),
					   sdev,
					   (cb->colour & 0x6) >> 1,
					   (cb->colour & 0xf8) >> 3);
		return ret;
	}

	slsi_spinlock_lock(&ndev_vif->peer_lock);
	peer = slsi_get_peer_from_mac(sdev, dev, dest);
	if (!peer) {
		SLSI_ERR(sdev, "no peer record for %02x:%02x:%02x:%02x:%02x:%02x, dropping TX frame\n",
			 dest[0], dest[1], dest[2], dest[3], dest[4], dest[5]);
		slsi_spinlock_unlock(&ndev_vif->peer_lock);
		return -EINVAL;
	}
	slsi_debug_frame(sdev, dev, skb, "TX");

	if (fapi_get_u16(skb, u.ma_unitdata_req.priority) == FAPI_PRIORITY_CONTENTION)
		skb->priority = FAPI_PRIORITY_QOS_UP0;
	else
		skb->priority = fapi_get_u16(skb, u.ma_unitdata_req.priority);

	if (scsc_wifi_fcq_transmit_data(dev, &peer->data_qs,
					slsi_frame_priority_to_ac_queue(skb->priority),
					sdev,
					(cb->colour & 0x6) >> 1,
					(cb->colour & 0xf8) >> 3) < 0) {
		SLSI_NET_DBG3(dev, SLSI_TX, "no fcq for %02x:%02x:%02x:%02x:%02x:%02x, dropping TX frame\n",
			      eth_hdr(skb)->h_dest[0], eth_hdr(skb)->h_dest[1], eth_hdr(skb)->h_dest[2], eth_hdr(skb)->h_dest[3], eth_hdr(skb)->h_dest[4], eth_hdr(skb)->h_dest[5]);
		slsi_spinlock_unlock(&ndev_vif->peer_lock);
		return -EINVAL;
	}
	/* SKB is owned by scsc_wifi_transmit_frame() unless the transmission is
	 * unsuccesful.
	 */
	ret = scsc_wifi_transmit_frame(&sdev->hip4_inst, false, skb);
	if (ret < 0) {
		SLSI_NET_DBG1(dev, SLSI_TX, "%s (signal: %d)\n", ret == -ENOSPC ? "Queue is full. Flow control" : "Failed to transmit", fapi_get_sigid(skb));
		/* scsc_wifi_transmit_frame failed, decrement BoT counters */
		scsc_wifi_fcq_receive_data(dev, &ndev_vif->ap.group_data_qs,
					   slsi_frame_priority_to_ac_queue(skb->priority),
					   sdev,
					   (cb->colour & 0x6) >> 1,
					   (cb->colour & 0xf8) >> 3);
		if (ret == -ENOSPC)
			SLSI_NET_DBG1(dev, SLSI_TX,
				      "TX_LOWER...Queue Full...BUT Dropping packet\n");
		else
			SLSI_NET_DBG1(dev, SLSI_TX,
				      "TX_LOWER...Generic Error...Dropping packet\n");
		slsi_spinlock_unlock(&ndev_vif->peer_lock);
		return ret;
	}

	slsi_offline_dbg_printf(sdev, 4, false, "%llu : %-8s : ucast_tx", ktime_to_ns(ktime_get()), "HIP TX");
	slsi_spinlock_unlock(&ndev_vif->peer_lock);
	return 0;
}

/**
 * NOTE:
 * 1. dev can be NULL
 * 2. On error the SKB is NOT freed, NOR retried (ENOSPC dropped).
 * Callers should take care to free the SKB eventually.
 */
int slsi_tx_control(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb)
{
	struct slsi_skb_cb *cb;
	int res = 0;
	struct fapi_signal_header *hdr;

	if (WARN_ON(!skb)) {
		res = -EINVAL;
		goto exit;
	}

	slsi_debug_frame(sdev, dev, skb, "TX");

	/**
	 * Sanity check of the skb - if it's not an MLME, MA, debug or test
	 * signal it will be discarded.
	 * Skip if test mode (wlanlite) is enabled.
	 */
	if (!slsi_is_test_mode_enabled())
		if (!fapi_is_mlme(skb) && !fapi_is_ma(skb) && !fapi_is_debug(skb) && !fapi_is_test(skb)) {
			SLSI_NET_WARN(dev, "Discarding skb because it has type: 0x%04X\n", fapi_get_sigid(skb));
			return -EINVAL;
		}

	cb = slsi_skb_cb_init(skb);
	cb->sig_length = fapi_get_expected_size(skb);
	cb->data_length = skb->len;
	/* F/w will panic if fw_reference is not zero. */
	hdr = (struct fapi_signal_header *)skb->data;
	hdr->fw_reference = 0;

	/* Log only the linear skb  chunk */
	SCSC_WLOG_PKTFATE_LOG_TX_CTRL_FRAME(fapi_get_u16(skb, u.mlme_frame_transmission_ind.host_tag),
					    skb->data, skb_headlen(skb));

	res = scsc_wifi_transmit_frame(&sdev->hip4_inst, true, skb);
	if (res != NETDEV_TX_OK) {
		char reason[80];

		SLSI_NET_ERR(dev, "%s (signal %d)\n", res == -ENOSPC ? "Queue is full. Flow control" : "Failed to transmit", fapi_get_sigid(skb));

		snprintf(reason, sizeof(reason), "Failed to transmit signal 0x%04X (err:%d)", fapi_get_sigid(skb), res);
		slsi_sm_service_failed(sdev, reason);

		res = -EIO;
	} else {
		slsi_offline_dbg_printf(sdev, 4, false, "%llu : %-8s : ctrl", ktime_to_ns(ktime_get()), "HIP TX");
	}
exit:
	return res;
}

void slsi_tx_pause_queues(struct slsi_dev *sdev)
{
	if (!sdev)
		return;

	scsc_wifi_fcq_pause_queues(sdev);
}

void slsi_tx_unpause_queues(struct slsi_dev *sdev)
{
	if (!sdev)
		return;

	scsc_wifi_fcq_unpause_queues(sdev);
}
