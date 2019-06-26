/****************************************************************************
 *
 * Copyright (c) 2012 - 2018 Samsung Electronics Co., Ltd. All rights reserved
 *
 ****************************************************************************/

#include "ioctl.h"
#include "debug.h"
#include "mlme.h"
#include "mgt.h"
#include "cac.h"
#include "hip.h"
#include "netif.h"
#include <net/netlink.h>
#include <linux/netdevice.h>
#include <linux/ieee80211.h>
#include "mib.h"
#include <scsc/scsc_mx.h>
#include <scsc/scsc_log_collector.h>
#include "dev.h"

#define CMD_RXFILTERADD         "RXFILTER-ADD"
#define CMD_RXFILTERREMOVE              "RXFILTER-REMOVE"
#define CMD_RXFILTERSTART               "RXFILTER-START"
#define CMD_RXFILTERSTOP                "RXFILTER-STOP"
#define CMD_SETCOUNTRYREV               "SETCOUNTRYREV"
#define CMD_GETCOUNTRYREV               "GETCOUNTRYREV"
#define CMD_SETROAMTRIGGER              "SETROAMTRIGGER"
#define CMD_GETROAMTRIGGER              "GETROAMTRIGGER"
#define CMD_SETSUSPENDMODE              "SETSUSPENDMODE"
#define CMD_SETROAMDELTA                "SETROAMDELTA"
#define CMD_GETROAMDELTA                "GETROAMDELTA"
#define CMD_SETROAMSCANPERIOD           "SETROAMSCANPERIOD"
#define CMD_GETROAMSCANPERIOD           "GETROAMSCANPERIOD"
#define CMD_SETFULLROAMSCANPERIOD               "SETFULLROAMSCANPERIOD"
#define CMD_GETFULLROAMSCANPERIOD               "GETFULLROAMSCANPERIOD"
#define CMD_SETSCANCHANNELTIME          "SETSCANCHANNELTIME"
#define CMD_GETSCANCHANNELTIME          "GETSCANCHANNELTIME"
#define CMD_SETSCANNPROBES              "SETSCANNPROBES"
#define CMD_GETSCANNPROBES              "GETSCANNPROBES"
#define CMD_SETROAMMODE         "SETROAMMODE"
#define CMD_GETROAMMODE         "GETROAMMODE"
#define CMD_SETROAMINTRABAND            "SETROAMINTRABAND"
#define CMD_GETROAMINTRABAND            "GETROAMINTRABAND"
#define CMD_SETROAMBAND         "SETROAMBAND"
#define CMD_GETROAMBAND         "GETROAMBAND"
#define CMD_SETROAMSCANCONTROL          "SETROAMSCANCONTROL"
#define CMD_GETROAMSCANCONTROL          "GETROAMSCANCONTROL"
#define CMD_SETSCANHOMETIME             "SETSCANHOMETIME"
#define CMD_GETSCANHOMETIME             "GETSCANHOMETIME"
#define CMD_SETSCANHOMEAWAYTIME         "SETSCANHOMEAWAYTIME"
#define CMD_GETSCANHOMEAWAYTIME         "GETSCANHOMEAWAYTIME"
#define CMD_SETOKCMODE          "SETOKCMODE"
#define CMD_GETOKCMODE          "GETOKCMODE"
#define CMD_SETWESMODE          "SETWESMODE"
#define CMD_GETWESMODE          "GETWESMODE"
#define CMD_SET_PMK             "SET_PMK"
#define CMD_HAPD_GET_CHANNEL			"HAPD_GET_CHANNEL"
#define CMD_SET_SAP_CHANNEL_LIST                "SET_SAP_CHANNEL_LIST"
#define CMD_REASSOC             "REASSOC"
#define CMD_SETROAMSCANCHANNELS         "SETROAMSCANCHANNELS"
#define CMD_GETROAMSCANCHANNELS         "GETROAMSCANCHANNELS"
#define CMD_SENDACTIONFRAME             "SENDACTIONFRAME"
#define CMD_HAPD_MAX_NUM_STA            "HAPD_MAX_NUM_STA"
#define CMD_COUNTRY            "COUNTRY"
#define CMD_SEND_GK                               "SEND_GK"
#define CMD_SETAPP2PWPSIE "SET_AP_P2P_WPS_IE"
#define CMD_P2PSETPS "P2P_SET_PS"
#define CMD_P2PSETNOA "P2P_SET_NOA"
#define CMD_P2PECSA "P2P_ECSA"
#define CMD_P2PLOSTART "P2P_LO_START"
#define CMD_P2PLOSTOP "P2P_LO_STOP"
#define CMD_TDLSCHANNELSWITCH  "TDLS_CHANNEL_SWITCH"
#define CMD_SETROAMOFFLOAD     "SETROAMOFFLOAD"
#define CMD_SETROAMOFFLAPLIST  "SETROAMOFFLAPLIST"

#define CMD_SETBAND "SETBAND"
#define CMD_GETBAND "GETBAND"
#define CMD_SET_FCC_CHANNEL "SET_FCC_CHANNEL"

#define CMD_FAKEMAC "FAKEMAC"

#define CMD_GETBSSRSSI "GET_BSS_RSSI"
#define CMD_GETBSSINFO "GETBSSINFO"
#define CMD_GETSTAINFO "GETSTAINFO"
#define CMD_GETASSOCREJECTINFO "GETASSOCREJECTINFO"

/* Known commands from framework for which no handlers */
#define CMD_AMPDU_MPDU "AMPDU_MPDU"
#define CMD_BTCOEXMODE "BTCOEXMODE"
#define CMD_BTCOEXSCAN_START "BTCOEXSCAN-START"
#define CMD_BTCOEXSCAN_STOP "BTCOEXSCAN-STOP"
#define CMD_CHANGE_RL "CHANGE_RL"
#define CMD_INTERFACE_CREATE "INTERFACE_CREATE"
#define CMD_INTERFACE_DELETE "INTERFACE_DELETE"
#define CMD_SET_INDOOR_CHANNELS "SET_INDOOR_CHANNELS"
#define CMD_GET_INDOOR_CHANNELS "GET_INDOOR_CHANNELS"
#define CMD_LTECOEX "LTECOEX"
#define CMD_MIRACAST "MIRACAST"
#define CMD_RESTORE_RL "RESTORE_RL"
#define CMD_RPSMODE "RPSMODE"
#define CMD_SETCCXMODE "SETCCXMODE"
#define CMD_SETDFSSCANMODE "SETDFSSCANMODE"
#define CMD_SETJOINPREFER "SETJOINPREFER"
#define CMD_SETSINGLEANT "SETSINGLEANT"
#define CMD_SET_TX_POWER_CALLING "SET_TX_POWER_CALLING"

#define CMD_DRIVERDEBUGDUMP "DEBUG_DUMP"
#define CMD_TESTFORCEHANG "SLSI_TEST_FORCE_HANG"
#define CMD_GETREGULATORY "GETREGULATORY"

#define CMD_SET_TX_POWER_SAR "SET_TX_POWER_SAR"
#define CMD_GET_TX_POWER_SAR "GET_TX_POWER_SAR"

#define ROAMOFFLAPLIST_MIN 1
#define ROAMOFFLAPLIST_MAX 100

static int slsi_parse_hex(unsigned char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return 0;
}

static void slsi_machexstring_to_macarray(char *mac_str, u8 *mac_arr)
{
	mac_arr[0] = slsi_parse_hex(mac_str[0]) << 4 | slsi_parse_hex(mac_str[1]);
	mac_arr[1] = slsi_parse_hex(mac_str[3]) << 4 | slsi_parse_hex(mac_str[4]);
	mac_arr[2] = slsi_parse_hex(mac_str[6]) << 4 | slsi_parse_hex(mac_str[7]);
	mac_arr[3] = slsi_parse_hex(mac_str[9]) << 4 | slsi_parse_hex(mac_str[10]);
	mac_arr[4] = slsi_parse_hex(mac_str[12]) << 4 | slsi_parse_hex(mac_str[13]);
	mac_arr[5] = slsi_parse_hex(mac_str[15]) << 4 | slsi_parse_hex(mac_str[16]);
}

static ssize_t slsi_set_suspend_mode(struct net_device *dev, char *command)
{
	int vif;
	struct netdev_vif *netdev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = netdev_vif->sdev;
	int               user_suspend_mode;
	int               previous_suspend_mode;
	u8                host_state;
	int               ret = 0;

	user_suspend_mode = *(command + strlen(CMD_SETSUSPENDMODE) + 1) - '0';

	SLSI_MUTEX_LOCK(sdev->device_config_mutex);
	previous_suspend_mode = sdev->device_config.user_suspend_mode;
	SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);

	if (user_suspend_mode != previous_suspend_mode) {
		SLSI_MUTEX_LOCK(sdev->netdev_add_remove_mutex);
		for (vif = 1; vif <= CONFIG_SCSC_WLAN_MAX_INTERFACES; vif++) {
			struct net_device *dev = slsi_get_netdev_locked(sdev, vif);
			struct netdev_vif *ndev_vif;

			if (!dev)
				continue;

			ndev_vif = netdev_priv(dev);
			SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
			if ((ndev_vif->activated) &&
			    (ndev_vif->vif_type == FAPI_VIFTYPE_STATION) &&
			    (ndev_vif->sta.vif_status == SLSI_VIF_STATUS_CONNECTED)) {
				if (user_suspend_mode)
					ret = slsi_update_packet_filters(sdev, dev);
				else
					ret = slsi_clear_packet_filters(sdev, dev);
				if (ret != 0)
					SLSI_NET_ERR(dev, "Error in updating /clearing the packet filters,ret=%d", ret);
			}

			SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
		}
		SLSI_MUTEX_UNLOCK(sdev->netdev_add_remove_mutex);
	}

	SLSI_MUTEX_LOCK(sdev->device_config_mutex);
	sdev->device_config.user_suspend_mode = user_suspend_mode;
	host_state = sdev->device_config.host_state;

	if (!sdev->device_config.user_suspend_mode)
		host_state = host_state | FAPI_HOSTSTATE_LCD_ACTIVE;
	else
		host_state = host_state & ~FAPI_HOSTSTATE_LCD_ACTIVE;
	sdev->device_config.host_state = host_state;

	ret = slsi_mlme_set_host_state(sdev, dev, host_state);
	if (ret != 0)
		SLSI_NET_ERR(dev, "Error in setting the Host State, ret=%d", ret);

	SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);
	return ret;
}

static ssize_t slsi_set_p2p_oppps(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif *ndev_vif;
	struct slsi_dev   *sdev;
	u8                *p2p_oppps_param = NULL;
	int               offset = 0;
	unsigned int      ct_param;
	unsigned int      legacy_ps;
	unsigned int      opp_ps;
	int               readbyte = 0;
	int               result = 0;

	p2p_oppps_param = command + strlen(CMD_P2PSETPS) + 1;
	ndev_vif = netdev_priv(dev);
	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	/* The NOA param shall be added only after P2P-VIF is active */
	if ((!ndev_vif->activated) || (ndev_vif->iftype != NL80211_IFTYPE_P2P_GO)) {
		SLSI_ERR_NODEV("P2P GO vif not activated\n");
		result = -EINVAL;
		goto exit;
	}

	sdev = ndev_vif->sdev;
	readbyte = slsi_str_to_int(&p2p_oppps_param[offset], &legacy_ps);
	if (!readbyte) {
		SLSI_ERR(sdev, "ct_param: failed to read legacy_ps\n");
		result = -EINVAL;
		goto exit;
	}
	offset = offset + readbyte + 1;

	readbyte = slsi_str_to_int(&p2p_oppps_param[offset], &opp_ps);
	if (!readbyte) {
		SLSI_ERR(sdev, "ct_param: failed to read ct_param\n");
		result = -EINVAL;
		goto exit;
	}
	offset = offset + readbyte + 1;

	readbyte = slsi_str_to_int(&p2p_oppps_param[offset], &ct_param);
	if (!readbyte) {
		SLSI_ERR(sdev, "ct_param: failed to read ct_param\n");
		result = -EINVAL;
		goto exit;
	}

	if (opp_ps == 0)
		result = slsi_mlme_set_ctwindow(sdev, dev, opp_ps);
	else if (ct_param < (unsigned int)ndev_vif->ap.beacon_interval)
		result = slsi_mlme_set_ctwindow(sdev, dev, ct_param);
	else
		SLSI_DBG1(sdev, SLSI_CFG80211, "p2p ct window = %d is out of range for beacon interval(%d)\n", ct_param, ndev_vif->ap.beacon_interval);
exit:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);

	return result;
}

static ssize_t slsi_p2p_set_noa_params(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif    *ndev_vif;
	struct slsi_dev      *sdev;
	int                  result = 0;
	u8                   *noa_params = NULL;
	int                  offset = 0;
	int                  readbyte = 0;
	unsigned int         noa_count;
	unsigned int         duration;
	unsigned int         interval;

	noa_params = command + strlen(CMD_P2PSETNOA) + 1;
	ndev_vif = netdev_priv(dev);
	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	/* The NOA param shall be added only after P2P-VIF is active */
	if ((!ndev_vif->activated) || (ndev_vif->iftype != NL80211_IFTYPE_P2P_GO)) {
		SLSI_ERR_NODEV("P2P GO vif not activated\n");
		result = -EINVAL;
		goto exit;
	}

	sdev = ndev_vif->sdev;
	readbyte = slsi_str_to_int(&noa_params[offset], &noa_count);
	if (!readbyte) {
		SLSI_ERR(sdev, "noa_count: failed to read a numeric value\n");
		result = -EINVAL;
		goto exit;
	}
	offset = offset + readbyte + 1;

	readbyte = slsi_str_to_int(&noa_params[offset], &interval);
	if (!readbyte) {
		SLSI_ERR(sdev, "interval: failed to read a numeric value\n");
		result = -EINVAL;
		goto exit;
	}
	offset = offset + readbyte + 1;

	readbyte = slsi_str_to_int(&noa_params[offset], &duration);
	if (!readbyte) {
		SLSI_ERR(sdev, "duration: failed to read a numeric value, at offset(%d)\n", offset);
		result = -EINVAL;
		goto exit;
	}

	/* Skip start time */
	result = slsi_mlme_set_p2p_noa(sdev, dev, noa_count, interval, duration);

exit:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	return result;
}

static ssize_t slsi_p2p_ecsa(struct net_device *dev, char *command)
{
	struct netdev_vif *ndev_vif;
	struct netdev_vif *group_dev_vif;
	struct slsi_dev   *sdev;
	struct net_device *group_dev = NULL;
	int                  result = 0;
	u8                   *ecsa_params = NULL;
	int                  offset = 0;
	int                  readbyte = 0;
	unsigned int         channel;
	unsigned int         bandwidth;
	u16 center_freq = 0;
	u16 chan_info = 0;
	struct cfg80211_chan_def chandef;
	enum ieee80211_band band;
	enum nl80211_channel_type chan_type = NL80211_CHAN_NO_HT;

	ecsa_params = command + strlen(CMD_P2PECSA) + 1;
	ndev_vif = netdev_priv(dev);
	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	sdev = ndev_vif->sdev;
	group_dev = slsi_get_netdev(sdev, SLSI_NET_INDEX_P2PX_SWLAN);
	if (!group_dev) {
		SLSI_INFO(sdev, "No Group net_dev found\n");
		return -EINVAL;
	}
	readbyte = slsi_str_to_int(&ecsa_params[offset], &channel);
	if (!readbyte) {
		SLSI_ERR(sdev, "channel: failed to read a numeric value\n");
		result = -EINVAL;
		goto exit;
	}
	offset = offset + readbyte + 1;
	readbyte = slsi_str_to_int(&ecsa_params[offset], &bandwidth);
	if (!readbyte) {
		SLSI_ERR(sdev, "bandwidth: failed to read a numeric value\n");
		result = -EINVAL;
		goto exit;
	}
	offset = offset + readbyte + 1;
	band = (channel <= 14) ? IEEE80211_BAND_2GHZ : IEEE80211_BAND_5GHZ;
	center_freq = ieee80211_channel_to_frequency(channel, band);
	SLSI_DBG1(sdev, SLSI_CFG80211, "p2p ecsa_params (center_freq)= (%d)\n", center_freq);
	chandef.chan = ieee80211_get_channel(sdev->wiphy, center_freq);
	chandef.width = (band  == IEEE80211_BAND_2GHZ) ? NL80211_CHAN_WIDTH_20_NOHT : NL80211_CHAN_WIDTH_80;

#ifndef SSB_4963_FIXED
	/* Default HT40 configuration */
	if (sdev->band_5g_supported) {
		if (bandwidth == 80) {
			chandef.width = NL80211_CHAN_WIDTH_40;
			bandwidth = 40;
			if (channel == 36 || channel == 44 || channel == 149 || channel == 157)
				chan_type = NL80211_CHAN_HT40PLUS;
			else
				chan_type = NL80211_CHAN_HT40MINUS;
		}
	}
#endif
	if (channel == 165 && bandwidth != 20) {
		bandwidth = 20;
		chan_type = NL80211_CHAN_WIDTH_20;
	}
	cfg80211_chandef_create(&chandef, chandef.chan, chan_type);
	chan_info = slsi_get_chann_info(sdev, &chandef);
	if (bandwidth != 20)
		center_freq = slsi_get_center_freq1(sdev, chan_info, center_freq);
	group_dev_vif = netdev_priv(group_dev);
	SLSI_MUTEX_LOCK(group_dev_vif->vif_mutex);
	result = slsi_mlme_channel_switch(sdev, group_dev, center_freq, chan_info);
	SLSI_MUTEX_UNLOCK(group_dev_vif->vif_mutex);

exit:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	return result;
}

static ssize_t slsi_ap_vendor_ies_write(struct slsi_dev *sdev, struct net_device *dev, u8 *ie,
					size_t ie_len, u16 purpose)
{
	u8                *vendor_ie = NULL;
	int               result = 0;
	struct netdev_vif *ndev_vif;

	ndev_vif = netdev_priv(dev);
	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	/* During AP start before mlme_start_req, supplicant calls set_ap_wps_ie() to send the vendor IEs for each
	 * beacon, probe response and association response. As we get all of them in mlme_start_req, ignoring the
	 * same which comes before adding GO VIF
	 */
	if (!ndev_vif->activated) {
		SLSI_DBG1(sdev, SLSI_CFG80211, "vif not activated\n");
		result = 0;
		goto exit;
	}
	if (!(ndev_vif->iftype == NL80211_IFTYPE_P2P_GO || ndev_vif->iftype == NL80211_IFTYPE_AP)) {
		SLSI_ERR(sdev, "Not AP or P2P interface. interfaceType:%d\n", ndev_vif->iftype);
		result = -EINVAL;
		goto exit;
	}

	vendor_ie = kmalloc(ie_len, GFP_KERNEL);
	if (!vendor_ie) {
		SLSI_ERR(sdev, "kmalloc failed\n");
		result = -ENOMEM;
		goto exit;
	}
	memcpy(vendor_ie, ie, ie_len);

	slsi_clear_cached_ies(&ndev_vif->ap.add_info_ies, &ndev_vif->ap.add_info_ies_len);
	result = slsi_ap_prepare_add_info_ies(ndev_vif, vendor_ie, ie_len);

	if (result == 0)
		result = slsi_mlme_add_info_elements(sdev, dev, purpose, ndev_vif->ap.add_info_ies, ndev_vif->ap.add_info_ies_len);

	slsi_clear_cached_ies(&ndev_vif->ap.add_info_ies, &ndev_vif->ap.add_info_ies_len);
	kfree(vendor_ie);

exit:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	return result;
}

static ssize_t slsi_set_ap_p2p_wps_ie(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	int                  readbyte = 0;
	int                  offset = 0;
	int                  result = 0;
	enum if_type {
		IF_TYPE_NONE,
		IF_TYPE_P2P_DEVICE,
		IF_TYPE_AP_P2P
	} iftype = IF_TYPE_NONE;
	enum frame_type {
		FRAME_TYPE_NONE,
		FRAME_TYPE_BEACON,
		FRAME_TYPE_PROBE_RESPONSE,
		FRAME_TYPE_ASSOC_RESPONSE
	} frametype = FRAME_TYPE_NONE;
	u8 *params = command + strlen(CMD_SETAPP2PWPSIE) + 1;
	int params_len = buf_len - strlen(CMD_SETAPP2PWPSIE) - 1;

	readbyte = slsi_str_to_int(&params[offset], (int *)&frametype);
	if (!readbyte) {
		SLSI_ERR(sdev, "frametype: failed to read a numeric value\n");
		result = -EINVAL;
		goto exit;
	}
	offset = offset + readbyte + 1;
	readbyte = slsi_str_to_int(&params[offset], (int *)&iftype);
	if (!readbyte) {
		SLSI_ERR(sdev, "iftype: failed to read a numeric value\n");
		result = -EINVAL;
		goto exit;
	}
	offset = offset + readbyte + 1;
	params_len = params_len - offset;

	SLSI_NET_DBG2(dev, SLSI_NETDEV,
		      "command=%s, frametype=%d, iftype=%d, total buf_len=%d, params_len=%d\n",
		      command, frametype, iftype, buf_len, params_len);

	/* check the net device interface type */
	if (iftype == IF_TYPE_P2P_DEVICE) {
		u8                *probe_resp_ie = NULL; /* params+offset; */

		if (frametype != FRAME_TYPE_PROBE_RESPONSE) {
			SLSI_NET_ERR(dev, "Wrong frame type received\n");
			goto exit;
		}
		probe_resp_ie = kmalloc(params_len, GFP_KERNEL);
		if (probe_resp_ie == NULL) {
			SLSI_ERR(sdev, "Malloc for IEs failed\n");
			return -ENOMEM;
		}

		memcpy(probe_resp_ie, params+offset, params_len);

		return slsi_p2p_dev_probe_rsp_ie(sdev, dev, probe_resp_ie, params_len);
	} else if (iftype == IF_TYPE_AP_P2P) {
		if (frametype == FRAME_TYPE_BEACON)
			return slsi_ap_vendor_ies_write(sdev, dev, params + offset, params_len, FAPI_PURPOSE_BEACON);
		else if (frametype == FRAME_TYPE_PROBE_RESPONSE)
			return slsi_ap_vendor_ies_write(sdev, dev, params + offset, params_len,
							FAPI_PURPOSE_PROBE_RESPONSE);
		else if (frametype == FRAME_TYPE_ASSOC_RESPONSE)
			return slsi_ap_vendor_ies_write(sdev, dev, params + offset, params_len,
							FAPI_PURPOSE_ASSOCIATION_RESPONSE);
	}
exit:
	return result;
}

/**
 * P2P_LO_START handling.
 * Add unsync vif, register for action frames and set the listen channel.
 * The probe response IEs would be configured later.
 */
static int slsi_p2p_lo_start(struct net_device *dev, char *command)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	struct ieee80211_channel *chan = NULL;
	char  *lo_params = NULL;
	unsigned int channel, duration, interval, count;
	int  ret = 0;
	int  freq;
	int  readbyte = 0;
	enum ieee80211_band band;
	int  offset = 0;

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	/* Reject LO if other operations are in progress. Back to back LO can be received.
	 * In such a case, if state is Listening then the listen offload flag should be true else
	 * reject the request as the Listening state would then be due to ROC.
	 */
	if ((sdev->p2p_state == P2P_SCANNING) || (sdev->p2p_state > P2P_LISTENING) ||
	    ((sdev->p2p_state == P2P_LISTENING) && (!ndev_vif->unsync.listen_offload))) {
		SLSI_NET_ERR(dev, "Reject LO due to ongoing P2P operation (state: %s)\n", slsi_p2p_state_text(sdev->p2p_state));
		ret = -EINVAL;
		goto exit;
	}

	lo_params = command + strlen(CMD_P2PLOSTART) + 1;
	readbyte = slsi_str_to_int(&lo_params[offset], &channel);
	if (!readbyte) {
		SLSI_ERR(sdev, "channel: failed to read a numeric value\n");
		ret = -EINVAL;
		goto exit;
	}
	offset = offset + readbyte + 1;
	readbyte = slsi_str_to_int(&lo_params[offset], &duration);
	if (!readbyte) {
		SLSI_ERR(sdev, "duration: failed to read a numeric value\n");
		ret = -EINVAL;
		goto exit;
	}
	offset = offset + readbyte + 1;
	readbyte = slsi_str_to_int(&lo_params[offset], &interval);
	if (!readbyte) {
		SLSI_ERR(sdev, "interval: failed to read a numeric value\n");
		ret = -EINVAL;
		goto exit;
	}
	offset = offset + readbyte + 1;
	readbyte = slsi_str_to_int(&lo_params[offset], &count);
	if (!readbyte) {
		SLSI_ERR(sdev, "count: failed to read a numeric value\n");
		ret = -EINVAL;
		goto exit;
	}

	if (!ndev_vif->activated) {
		ret = slsi_mlme_add_vif(sdev, dev, dev->dev_addr, dev->dev_addr);
		if (ret != 0) {
			SLSI_NET_ERR(dev, "Unsync vif addition failed\n");
			goto exit;
		}

		ndev_vif->activated = true;
		ndev_vif->mgmt_tx_data.exp_frame = SLSI_P2P_PA_INVALID;
		SLSI_P2P_STATE_CHANGE(sdev, P2P_IDLE_VIF_ACTIVE);

		ret = slsi_mlme_register_action_frame(sdev, dev, SLSI_ACTION_FRAME_PUBLIC, SLSI_ACTION_FRAME_PUBLIC);
		if (ret != 0) {
			SLSI_NET_ERR(dev, "Action frame registration for unsync vif failed\n");
			goto exit_with_vif_deactivate;
		}
	}

	/* Send set_channel irrespective of the values of LO parameters as they are not cached
	 * in driver to check whether they have changed.
	 */
	band = (channel <= 14) ? IEEE80211_BAND_2GHZ : IEEE80211_BAND_5GHZ;
	freq = ieee80211_channel_to_frequency(channel, band);
	chan = ieee80211_get_channel(sdev->wiphy, freq);
	if (!chan) {
		SLSI_NET_ERR(dev, "Incorrect channel: %u - Listen Offload failed\n", channel);
		ret = -EINVAL;
		goto exit_with_vif_deactivate;
	}

	ret = slsi_mlme_set_channel(sdev, dev, chan, duration, interval, count);
	if (ret != 0) {
		SLSI_NET_ERR(dev, "Set channel for unsync vif failed\n");
		goto exit_with_vif_deactivate;
	} else {
		ndev_vif->chan = chan;
	}
	/* If framework sends the values for listen offload as 1,500,5000 and 6,
	 * where 5000ms (5 seconds) is the listen interval which needs to be repeated
	 * 6 times(i.e. count). Hence listen_end_ind comes after 30 seconds
	 * (6 * 5000 = 30000ms) Hence host should wait 31 seconds to delete the
	 * unsync VIF for one such P2P listen offload request.
	 */
	slsi_p2p_queue_unsync_vif_del_work(ndev_vif, interval * count + 1000);
	ndev_vif->unsync.listen_offload = true;
	SLSI_P2P_STATE_CHANGE(ndev_vif->sdev, P2P_LISTENING);
	goto exit;

exit_with_vif_deactivate:
	slsi_p2p_vif_deactivate(sdev, dev, true);
exit:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	return ret;
}

/**
 * P2P_LO_STOP handling.
 * Clear listen offload flag.
 * Delete the P2P unsynchronized vif.
 */
static int slsi_p2p_lo_stop(struct net_device *dev)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	WARN_ON((!ndev_vif->unsync.listen_offload) || (ndev_vif->sdev->p2p_state != P2P_LISTENING));

	ndev_vif->unsync.listen_offload = false;

	/* Deactivating the p2p unsynchronized vif */
	if (ndev_vif->sdev->p2p_state == P2P_LISTENING)
		slsi_p2p_vif_deactivate(ndev_vif->sdev, ndev_vif->wdev.netdev, true);

	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);

	return 0;
}

static ssize_t slsi_rx_filter_num_write(struct net_device *dev, int add_remove, int filter_num)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	int               ret = 0;

	if (add_remove)
		sdev->device_config.rx_filter_num = filter_num;
	else
		sdev->device_config.rx_filter_num = 0;
	return ret;
}

#ifdef CONFIG_SCSC_WLAN_WIFI_SHARING
static ssize_t slsi_create_interface(struct net_device *dev, char *intf_name)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	struct net_device   *swlan_dev;

	swlan_dev = slsi_get_netdev(sdev, SLSI_NET_INDEX_P2PX_SWLAN);
	if (swlan_dev && (strcmp(swlan_dev->name, intf_name) == 0))
		return 0;

	SLSI_NET_ERR(dev, "Failed to create interface %s\n", intf_name);
	return -EINVAL;
}

static ssize_t slsi_delete_interface(struct net_device *dev, char *intf_name)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	struct net_device   *swlan_dev;

	swlan_dev = slsi_get_netdev(sdev, SLSI_NET_INDEX_P2PX_SWLAN);
	if (swlan_dev && (strcmp(swlan_dev->name, intf_name) == 0)) {
		ndev_vif = netdev_priv(swlan_dev);
		if (ndev_vif->activated)
			slsi_stop_net_dev(sdev, swlan_dev);
		return 0;
	}

	SLSI_NET_ERR(dev, "Failed to delete interface %s\n", intf_name);
	return -EINVAL;
}

static ssize_t slsi_set_indoor_channels(struct net_device *dev, char *arg)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	int readbyte = 0;
	int offset = 0;
	int res;
	int ret;

	readbyte = slsi_str_to_int(&arg[offset], &res);

	ret = slsi_set_mib_wifi_sharing_5ghz_channel(sdev, SLSI_PSID_UNIFI_WI_FI_SHARING5_GHZ_CHANNEL,
						     res, offset, readbyte, arg);

	return ret;
}

static ssize_t slsi_get_indoor_channels(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	char op[150] = "";
	char int_string[30] = "";
	int i;
	int len = 0;

	SLSI_DBG1_NODEV(SLSI_MLME, "GET_INDOOR_CHANNELS : %d ", sdev->num_5g_restricted_channels);

	for (i = 0; i < sdev->num_5g_restricted_channels; i++) {
		sprintf(int_string, "%d", sdev->wifi_sharing_5g_restricted_channels[i]);
		strcat(op, int_string);
		strcat(op, " ");
	}

	len = snprintf(command, buf_len, "%d %s", sdev->num_5g_restricted_channels, op);

	return len;
}
#endif
static ssize_t slsi_set_country_rev(struct net_device *dev, char *country_code)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	char              alpha2_rev[4];
	int               status = 0;

	if (!country_code)
		return -EINVAL;

	memcpy(alpha2_rev, country_code, 4);

	status = slsi_set_country_update_regd(sdev, alpha2_rev, 4);

	return status;
}

static ssize_t slsi_get_country_rev(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	u8                buf[5];
	int               len = 0;

	memset(buf, 0, sizeof(buf));

	len = snprintf(command, buf_len, "%s %c%c %d", CMD_GETCOUNTRYREV,
		       sdev->device_config.domain_info.regdomain->alpha2[0],
		       sdev->device_config.domain_info.regdomain->alpha2[1],
		       sdev->device_config.domain_info.regdomain->dfs_region);

	return len;
}

#ifdef CONFIG_SCSC_WLAN_WES_NCHO
static ssize_t slsi_roam_scan_trigger_write(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	int               mib_value = 0;

	slsi_str_to_int(command, &mib_value);

	return slsi_set_mib_roam(sdev, NULL, SLSI_PSID_UNIFI_RSSI_ROAM_SCAN_TRIGGER, mib_value);
}

static ssize_t slsi_roam_scan_trigger_read(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	int               mib_value = 0;
	int               res;

	res = slsi_get_mib_roam(sdev, SLSI_PSID_UNIFI_RSSI_ROAM_SCAN_TRIGGER, &mib_value);
	if (res)
		return res;
	res = snprintf(command, buf_len, "%s %d", CMD_GETROAMTRIGGER, mib_value);
	return res;
}

static ssize_t slsi_roam_delta_trigger_write(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	int               mib_value = 0;

	slsi_str_to_int(command, &mib_value);

	return slsi_set_mib_roam(sdev, NULL, SLSI_PSID_UNIFI_ROAM_DELTA_TRIGGER, mib_value);
}

static ssize_t slsi_roam_delta_trigger_read(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	int               mib_value = 0;
	int               res;

	res = slsi_get_mib_roam(sdev, SLSI_PSID_UNIFI_ROAM_DELTA_TRIGGER, &mib_value);
	if (res)
		return res;

	res = snprintf(command, buf_len, "%s %d", CMD_GETROAMDELTA, mib_value);
	return res;
}

static ssize_t slsi_cached_channel_scan_period_write(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	int               mib_value = 0;

	slsi_str_to_int(command, &mib_value);
	return slsi_set_mib_roam(sdev, NULL, SLSI_PSID_UNIFI_ROAM_CACHED_CHANNEL_SCAN_PERIOD, mib_value * 1000000);
}

static ssize_t slsi_cached_channel_scan_period_read(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	int               mib_value = 0;
	int               res;

	res = slsi_get_mib_roam(sdev, SLSI_PSID_UNIFI_ROAM_CACHED_CHANNEL_SCAN_PERIOD, &mib_value);
	if (res)
		return res;

	res = snprintf(command, buf_len, "%s %d", CMD_GETROAMSCANPERIOD, mib_value / 1000000);

	return res;
}

static ssize_t slsi_full_roam_scan_period_write(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	int               mib_value = 0;

	slsi_str_to_int(command, &mib_value);

	return slsi_set_mib_roam(sdev, NULL, SLSI_PSID_UNIFI_FULL_ROAM_SCAN_PERIOD, mib_value * 1000000);
}

static ssize_t slsi_full_roam_scan_period_read(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	int               mib_value = 0;
	int               res;

	res = slsi_get_mib_roam(sdev, SLSI_PSID_UNIFI_FULL_ROAM_SCAN_PERIOD, &mib_value);
	if (res)
		return res;

	res = snprintf(command, buf_len, "%s %d", CMD_GETFULLROAMSCANPERIOD, mib_value / 1000000);

	return res;
}

static ssize_t slsi_roam_scan_max_active_channel_time_write(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	int               mib_value = 0;

	slsi_str_to_int(command, &mib_value);

	return slsi_set_mib_roam(sdev, NULL, SLSI_PSID_UNIFI_ROAM_SCAN_MAX_ACTIVE_CHANNEL_TIME, mib_value);
}

static ssize_t slsi_roam_scan_max_active_channel_time_read(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	int               mib_value = 0;
	int               res;

	res = slsi_get_mib_roam(sdev, SLSI_PSID_UNIFI_ROAM_SCAN_MAX_ACTIVE_CHANNEL_TIME, &mib_value);
	if (res)
		return res;

	res = snprintf(command, buf_len, "%s %d", CMD_GETSCANCHANNELTIME, mib_value);

	return res;
}

static ssize_t slsi_roam_scan_probe_interval_write(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	int               mib_value = 0;

	slsi_str_to_int(command, &mib_value);
	return slsi_set_mib_roam(sdev, NULL, SLSI_PSID_UNIFI_ROAM_SCAN_NPROBE, mib_value);
}

static ssize_t slsi_roam_scan_probe_interval_read(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	int               mib_value = 0;
	int               res;

	res = slsi_get_mib_roam(sdev, SLSI_PSID_UNIFI_ROAM_SCAN_NPROBE, &mib_value);
	if (res)
		return res;

	res = snprintf(command, buf_len, "%s %d", CMD_GETSCANNPROBES, mib_value);

	return res;
}

static ssize_t slsi_roam_mode_write(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	int               mib_value = 0;

	if (slsi_is_rf_test_mode_enabled()) {
		SLSI_DBG1_NODEV(SLSI_MLME, "SLSI_PSID_UNIFI_ROAM_MODE is not supported because of rf test mode.\n");
		return -ENOTSUPP;
	}

	slsi_str_to_int(command, &mib_value);

	return slsi_set_mib_roam(sdev, NULL, SLSI_PSID_UNIFI_ROAM_MODE, mib_value);
}

static ssize_t slsi_roam_mode_read(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	int               mib_value = 0;
	int               res;

	res = slsi_get_mib_roam(sdev, SLSI_PSID_UNIFI_ROAM_MODE, &mib_value);
	if (res)
		return res;

	res = snprintf(command, buf_len, "%s %d", CMD_GETROAMMODE, mib_value);

	return res;
}

static int slsi_roam_offload_ap_list(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif          *ndev_vif = netdev_priv(dev);
	struct slsi_dev            *sdev = ndev_vif->sdev;
	struct cfg80211_acl_data  *mac_acl;
	int                        ap_count = 0;
	int                        buf_pos = 0;
	int                        i, r;
	int                        malloc_len;

	/* command format:
	 *     x,aa:bb:cc:dd:ee:ff,xx:yy:zz:qq:ww:ee...
	 *     x = 1 to 100
	 *     each mac address id 17 bytes and every mac address is separated by ','
	 */
	buf_pos = slsi_str_to_int(command, &ap_count);
	if (ap_count < ROAMOFFLAPLIST_MIN || ap_count > ROAMOFFLAPLIST_MAX) {
		SLSI_ERR(sdev, "ap_count: %d\n", ap_count);
		return -EINVAL;
	}
	buf_pos++;
	/* each mac address takes 18 bytes(17 for mac address and 1 for ',') except the last one.
	 * the last mac address is just 17 bytes(without a coma)
	 */
	if ((buf_len - buf_pos) < (ap_count*18 - 1)) {
		SLSI_ERR(sdev, "Invalid buff len:%d for %d APs\n", (buf_len - buf_pos), ap_count);
		return -EINVAL;
	}
	malloc_len = sizeof(struct cfg80211_acl_data) + sizeof(struct mac_address) * ap_count;
	mac_acl = kmalloc(malloc_len, GFP_KERNEL);
	if (!mac_acl) {
		SLSI_ERR(sdev, "MEM fail for size:%ld\n", sizeof(struct cfg80211_acl_data) + sizeof(struct mac_address) * ap_count);
		return -ENOMEM;
	}

	for (i = 0; i < ap_count; i++) {
		slsi_machexstring_to_macarray(&command[buf_pos], mac_acl->mac_addrs[i].addr);
		buf_pos += 18;
		SLSI_DBG3_NODEV(SLSI_MLME, "[%pM]", mac_acl->mac_addrs[i].addr);
	}
	mac_acl->acl_policy = NL80211_ACL_POLICY_DENY_UNLESS_LISTED;
	mac_acl->n_acl_entries = ap_count;

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	r = slsi_mlme_set_acl(sdev, dev, ndev_vif->ifnum, mac_acl);
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	kfree(mac_acl);
	return r;
}

static ssize_t slsi_roam_scan_band_write(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	int               mib_value = 0;

	slsi_str_to_int(command, &mib_value);
	return slsi_set_mib_roam(sdev, NULL, SLSI_PSID_UNIFI_ROAM_SCAN_BAND, mib_value);
}

static ssize_t slsi_roam_scan_band_read(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	int               mib_value = 0;
	int               res;

	res = slsi_get_mib_roam(sdev, SLSI_PSID_UNIFI_ROAM_SCAN_BAND, &mib_value);
	if (res)
		return res;

	res = snprintf(command, buf_len, "%s %d", CMD_GETROAMINTRABAND, mib_value);

	return res;
}

static ssize_t slsi_freq_band_write(struct net_device *dev, uint band)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;

	slsi_band_update(sdev, band);
	/* Convert to correct Mib value (intra_band:1, all_band:2) */
	return slsi_set_mib_roam(sdev, NULL, SLSI_PSID_UNIFI_ROAM_SCAN_BAND, (band == SLSI_FREQ_BAND_AUTO) ? 2 : 1);
}

static ssize_t slsi_freq_band_read(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	char              buf[128];
	int               pos = 0;
	const size_t      bufsz = sizeof(buf);

	memset(buf, '\0', 128);
	pos += scnprintf(buf + pos, bufsz - pos, "Band %d", sdev->device_config.supported_band);

	buf[pos] = '\0';
	memcpy(command, buf, pos + 1);

	return pos;
}

static ssize_t slsi_roam_scan_control_write(struct net_device *dev, int mode)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;

	SLSI_MUTEX_LOCK(sdev->device_config_mutex);

	if (mode == 0 || mode == 1) {
		sdev->device_config.roam_scan_mode = mode;
	} else {
		SLSI_ERR(sdev, "Invalid roam Mode: Must be 0 or, 1 Not '%c'\n", mode);
		SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);
		return -EINVAL;
	}

	SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);
	return slsi_set_mib_roam(sdev, NULL, SLSI_PSID_UNIFI_ROAM_SCAN_CONTROL, sdev->device_config.roam_scan_mode);
}

static ssize_t slsi_roam_scan_control_read(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	int               mib_value = 0;
	int               res;

	res = slsi_get_mib_roam(sdev, SLSI_PSID_UNIFI_ROAM_SCAN_CONTROL, &mib_value);
	if (res)
		return res;

	res = snprintf(command, buf_len, "%s %d", CMD_GETROAMSCANCONTROL, mib_value);

	return res;
}

static ssize_t slsi_roam_scan_home_time_write(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	int               mib_value = 0;

	slsi_str_to_int(command, &mib_value);

	return slsi_set_mib_roam(sdev, NULL, SLSI_PSID_UNIFI_ROAM_SCAN_HOME_TIME, mib_value);
}

static ssize_t slsi_roam_scan_home_time_read(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	int               mib_value = 0;
	int               res;

	res = slsi_get_mib_roam(sdev, SLSI_PSID_UNIFI_ROAM_SCAN_HOME_TIME, &mib_value);
	if (res)
		return res;

	res = snprintf(command, buf_len, "%s %d", CMD_GETSCANHOMETIME, mib_value);

	return res;
}

static ssize_t slsi_roam_scan_home_away_time_write(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	int               mib_value = 0;

	slsi_str_to_int(command, &mib_value);
	return slsi_set_mib_roam(sdev, NULL, SLSI_PSID_UNIFI_ROAM_SCAN_HOME_AWAY_TIME, mib_value);
}

static ssize_t slsi_roam_scan_home_away_time_read(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	int               mib_value = 0;
	int               res;

	res = slsi_get_mib_roam(sdev, SLSI_PSID_UNIFI_ROAM_SCAN_HOME_AWAY_TIME, &mib_value);
	if (res)
		return res;

	res = snprintf(command, buf_len, "%s %d", CMD_GETSCANHOMEAWAYTIME, mib_value);

	return res;
}

static ssize_t slsi_roam_scan_channels_write(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	int               result = 0;
	int               i, channel_count;
	int               offset = 0;
	int               readbyte = 0;
	int               channels[MAX_CHANNEL_LIST];

	readbyte = slsi_str_to_int(command, &channel_count);

	if (!readbyte) {
		SLSI_ERR(sdev, "channel count: failed to read a numeric value");
		return -EINVAL;
	}
	SLSI_MUTEX_LOCK(sdev->device_config_mutex);

	if (channel_count > MAX_CHANNEL_LIST)
		channel_count = MAX_CHANNEL_LIST;
	sdev->device_config.wes_roam_scan_list.n = channel_count;

	for (i = 0; i < channel_count; i++) {
		offset = offset + readbyte + 1;
		readbyte = slsi_str_to_int(&command[offset], &channels[i]);
		if (!readbyte) {
			SLSI_ERR(sdev, "failed to read a numeric value\n");
			SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);
			return -EINVAL;
		}

		sdev->device_config.wes_roam_scan_list.channels[i] = channels[i];
	}
	SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	result = slsi_mlme_set_cached_channels(sdev, dev, channel_count, sdev->device_config.wes_roam_scan_list.channels);
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);

	return result;
}

static ssize_t slsi_roam_scan_channels_read(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	char              channel_buf[128] = { 0 };
	int               pos = 0;
	int               i;
	int               channel_count = 0;

	SLSI_MUTEX_LOCK(sdev->device_config_mutex);
	channel_count = sdev->device_config.wes_roam_scan_list.n;
	pos = scnprintf(channel_buf, sizeof(channel_buf), "%s %d", CMD_GETROAMSCANCHANNELS, channel_count);
	for (i = 0; i < channel_count; i++)
		pos += scnprintf(channel_buf + pos, sizeof(channel_buf) - pos, " %d", sdev->device_config.wes_roam_scan_list.channels[i]);
	channel_buf[pos] = '\0';

	SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);

	memcpy(command, channel_buf, pos + 1);

	return pos;
}

static ssize_t slsi_okc_mode_write(struct net_device *dev, int mode)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;

	SLSI_MUTEX_LOCK(sdev->device_config_mutex);

	if (mode == 0 || mode == 1) {
		sdev->device_config.okc_mode = mode;
	} else {
		SLSI_ERR(sdev, "Invalid OKC Mode: Must be 0 or, 1 Not '%c'\n", mode);
		SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);
		return -EINVAL;
	}

	SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);
	return 0;
}

static ssize_t slsi_okc_mode_read(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	int               okc_mode;
	int               res;

	SLSI_MUTEX_LOCK(sdev->device_config_mutex);
	okc_mode = sdev->device_config.okc_mode;
	SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);

	res = snprintf(command, buf_len, "%s %d", CMD_GETOKCMODE, okc_mode);

	return res;
}

static ssize_t slsi_wes_mode_write(struct net_device *dev, int mode)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	int               result = 0;
	u32               action_frame_bmap = SLSI_STA_ACTION_FRAME_BITMAP;

	SLSI_MUTEX_LOCK(sdev->device_config_mutex);

	if (mode == 0 || mode == 1) {
		sdev->device_config.wes_mode = mode;
	} else {
		SLSI_ERR(sdev, "Invalid WES Mode: Must be 0 or 1 Not '%c'\n", mode);
		SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);
		return -EINVAL;
	}
	SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);
	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	if ((ndev_vif->activated) && (ndev_vif->vif_type == FAPI_VIFTYPE_STATION) &&
	    (ndev_vif->sta.vif_status == SLSI_VIF_STATUS_CONNECTED)) {
		if (sdev->device_config.wes_mode)
			action_frame_bmap |= SLSI_ACTION_FRAME_VENDOR_SPEC;

		result = slsi_mlme_register_action_frame(sdev, dev, action_frame_bmap, action_frame_bmap);
	}

	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);

	return result;
}

static ssize_t slsi_wes_mode_read(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	int               wes_mode;
	int               res;

	SLSI_MUTEX_LOCK(sdev->device_config_mutex);
	wes_mode = sdev->device_config.wes_mode;
	SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);

	res = snprintf(command, buf_len, "%s %d", CMD_GETWESMODE, wes_mode);

	return res;
}
#endif

static ssize_t slsi_set_pmk(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	u8                pmk[33];
	int               result = 0;

	memcpy((u8 *)pmk, command + strlen("SET_PMK "), 32);
	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	result = slsi_mlme_set_pmk(sdev, dev, pmk, 32);

	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	return result;
}

static ssize_t slsi_auto_chan_read(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = ndev_vif->sdev;
	int             ap_auto_chan;
	int result = 0;

	SLSI_MUTEX_LOCK(sdev->device_config_mutex);
	ap_auto_chan = sdev->device_config.ap_auto_chan;
	SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);

	result = snprintf(command, buf_len, "%d\n", ap_auto_chan);
	return result;
}

static ssize_t slsi_auto_chan_write(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif        *ndev_vif = netdev_priv(dev);
	struct slsi_dev          *sdev = ndev_vif->sdev;
	int                      n_channels;
	struct ieee80211_channel *channels[SLSI_NO_OF_SCAN_CHANLS_FOR_AUTO_CHAN_MAX] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
	int                      count_channels;
	int                      offset;
	int                      chan;
#ifdef CONFIG_SCSC_WLAN_WIFI_SHARING
	struct net_device *sta_dev = slsi_get_netdev(sdev, SLSI_NET_INDEX_WLAN);
	struct netdev_vif *ndev_sta_vif  = netdev_priv(sta_dev);
	int sta_frequency;
#endif

	offset = slsi_str_to_int(&command[21], &n_channels);
	if (!offset) {
		SLSI_ERR(sdev, "channel count: failed to read a numeric value");
		return -EINVAL;
	}

	if (n_channels > SLSI_NO_OF_SCAN_CHANLS_FOR_AUTO_CHAN_MAX) {
		SLSI_ERR(sdev, "channel count:%d > SLSI_NO_OF_SCAN_CHANLS_FOR_AUTO_CHAN_MAX:%d\n", n_channels, SLSI_NO_OF_SCAN_CHANLS_FOR_AUTO_CHAN_MAX);
		return -EINVAL;
	}

	/* If "1 6 11" are passed, scan all "1 - 14" channels. If "1 6" are passed, scan "1 - 9" channels */
	if (n_channels == 3)
		n_channels = 14;
	else if (n_channels == 2)
		n_channels = 9;
	count_channels = 0;
	for (chan = 1; chan <= n_channels; chan++) {
		int center_freq;

		center_freq = ieee80211_channel_to_frequency(chan, NL80211_BAND_2GHZ);
		channels[count_channels] = ieee80211_get_channel(sdev->wiphy, center_freq);
		if (!channels[count_channels])
			SLSI_WARN(sdev, "channel number:%d invalid\n", chan);
		else
			count_channels++;

	}

	SLSI_DBG3(sdev, SLSI_INIT_DEINIT, "Number of channels for autochannel selection= %d", count_channels);

	SLSI_MUTEX_LOCK(sdev->device_config_mutex);
	sdev->device_config.ap_auto_chan = 0;
	SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);

#ifdef CONFIG_SCSC_WLAN_WIFI_SHARING
if ((ndev_sta_vif->activated) && (ndev_sta_vif->vif_type == FAPI_VIFTYPE_STATION) &&
				 (ndev_sta_vif->sta.vif_status == SLSI_VIF_STATUS_CONNECTING ||
				  ndev_sta_vif->sta.vif_status == SLSI_VIF_STATUS_CONNECTED)) {
	sta_frequency = ndev_sta_vif->chan->center_freq;
	SLSI_MUTEX_LOCK(sdev->device_config_mutex);
	if ((sta_frequency / 1000) == 2)
		sdev->device_config.ap_auto_chan = ieee80211_frequency_to_channel(sta_frequency);
	else
		sdev->device_config.ap_auto_chan = 1;
	SLSI_INFO(sdev, "Channel selected = %d", sdev->device_config.ap_auto_chan);
	SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);
	return 0;
}
#endif /*wifi sharing*/
	return slsi_auto_chan_select_scan(sdev, count_channels, channels);
}

static ssize_t slsi_reassoc_write(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif   *ndev_vif = netdev_priv(dev);
	struct slsi_dev     *sdev = ndev_vif->sdev;
	u8                  bssid[6] = { 0 };
	int                 channel;
	int                 freq;
	enum ieee80211_band band = IEEE80211_BAND_2GHZ;
	int                 r = 0;

	if (command[17] != ' ') {
		SLSI_ERR(sdev, "Invalid Format '%s' '%c'\n", command, command[17]);
		return -EINVAL;
	}

	command[17] = '\0';

	slsi_machexstring_to_macarray(command, bssid);

	if (!slsi_str_to_int(&command[18], &channel)) {
		SLSI_ERR(sdev, "Invalid channel string: '%s'\n", &command[18]);
		return -EINVAL;
	}

	if (channel > 14)
		band = IEEE80211_BAND_5GHZ;
	freq = (u16)ieee80211_channel_to_frequency(channel, band);

	ndev_vif = netdev_priv(dev);
	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	r = slsi_mlme_roam(sdev, dev, bssid, freq);

	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	return r;
}

static ssize_t slsi_send_action_frame(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif    *ndev_vif = netdev_priv(dev);
	struct slsi_dev      *sdev = ndev_vif->sdev;
	char                 *temp;
	u8                   bssid[6] = { 0 };
	int                  channel = 0;
	int                  freq = 0;
	enum ieee80211_band  band = IEEE80211_BAND_2GHZ;
	int                  r = 0;
	u16                  host_tag = slsi_tx_mgmt_host_tag(sdev);
	u32                  dwell_time;
	struct ieee80211_hdr *hdr;
	u8                   *buf = NULL;
	u8                   *final_buf = NULL;
	u8                   temp_byte;
	int                  len = 0;
	int                  final_length = 0;
	int                  i = 0, j = 0;
	char                 *pos;

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	if ((!ndev_vif->activated) || (ndev_vif->vif_type != FAPI_VIFTYPE_STATION) ||
	    (ndev_vif->sta.vif_status != SLSI_VIF_STATUS_CONNECTED)) {
		SLSI_ERR(sdev, "Not a STA vif or status is not CONNECTED\n");
		SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
		return -EINVAL;
	}
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);

	command[17] = '\0';
	slsi_machexstring_to_macarray(command, bssid);

	command[17] = ' ';
	pos = strchr(command, ' ');
	if (pos == NULL)
		return -EINVAL;
	*pos++ = '\0';

	if (!slsi_str_to_int(pos, &channel)) {
		SLSI_ERR(sdev, "Invalid channel string: '%s'\n", pos);
		return -EINVAL;
	}
	pos++;

	if (channel > 14)
		band = IEEE80211_BAND_5GHZ;
	freq = (u16)ieee80211_channel_to_frequency(channel, band);

	pos = strchr(pos, ' ');
	if (pos == NULL)
		return -EINVAL;
	*pos++ = '\0';

	if (!slsi_str_to_int(pos, &dwell_time)) {
		SLSI_ERR(sdev, "Invalid dwell time string: '%s'\n", pos);
		return -EINVAL;
	}

	pos = strchr(pos, ' ');
	if (pos == NULL)
		return -EINVAL;
	pos++;

	/*Length of data*/
	temp = pos;
	while (*temp != '\0')
		temp++;
	len = temp - pos;

	if (len <= 0)
		return -EINVAL;
	buf = kmalloc((len + 1) / 2, GFP_KERNEL);

	if (buf == NULL) {
		SLSI_ERR(sdev, "Malloc  failed\n");
		return -ENOMEM;
	}
	/*We receive a char buffer, convert to hex*/
	temp = pos;
	for (i = 0, j = 0; j < len; j += 2) {
		if (j + 1 == len)
			temp_byte = slsi_parse_hex(temp[j]);
		else
			temp_byte = slsi_parse_hex(temp[j]) << 4 | slsi_parse_hex(temp[j + 1]);
		buf[i++] = temp_byte;
	}
	len = i;

	final_length = len + IEEE80211_HEADER_SIZE;
	final_buf = kmalloc(final_length, GFP_KERNEL);
	if (final_buf == NULL) {
		SLSI_ERR(sdev, "Malloc  failed\n");
		kfree(buf);
		return -ENOMEM;
	}

	hdr = (struct ieee80211_hdr *)final_buf;
	hdr->frame_control = IEEE80211_FC(IEEE80211_FTYPE_MGMT, IEEE80211_STYPE_ACTION);
	SLSI_ETHER_COPY(hdr->addr1, bssid);
	SLSI_ETHER_COPY(hdr->addr2, sdev->hw_addr);
	SLSI_ETHER_COPY(hdr->addr3, bssid);
	memcpy(final_buf + IEEE80211_HEADER_SIZE, buf, len);

	kfree(buf);

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	r = slsi_mlme_send_frame_mgmt(sdev, dev, final_buf, final_length, FAPI_DATAUNITDESCRIPTOR_IEEE802_11_FRAME, FAPI_MESSAGETYPE_IEEE80211_ACTION, host_tag, SLSI_FREQ_HOST_TO_FW(freq), dwell_time * 1000, 0);

	kfree(final_buf);
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	return r;
}

static ssize_t slsi_setting_max_sta_write(struct net_device *dev, int sta_number)
{
	struct netdev_vif    *ndev_vif = netdev_priv(dev);
	struct slsi_dev      *sdev = ndev_vif->sdev;
	struct slsi_mib_data mib_data = { 0, NULL };
	int                  result = 0;

	if (sta_number > 10 || sta_number < 1)
		return -EINVAL;
	result = slsi_mib_encode_uint(&mib_data, SLSI_PSID_UNIFI_MAX_CLIENT, sta_number, 0);
	if ((result != SLSI_MIB_STATUS_SUCCESS) || (mib_data.dataLength == 0))
		return -ENOMEM;
	result = slsi_mlme_set(sdev, dev, mib_data.data, mib_data.dataLength);
	if (result != 0)
		SLSI_ERR(sdev, "max_sta: mlme_set_req failed: Result code: %d\n", result);
	kfree(mib_data.data);

	return result;
}

static ssize_t slsi_country_write(struct net_device *dev, char *country_code)
{
	struct netdev_vif *netdev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = netdev_vif->sdev;
	char              alpha2_code[SLSI_COUNTRY_CODE_LEN];
	int               status;

	if (strlen(country_code) < 2)
		return -EINVAL;

	memcpy(alpha2_code, country_code, 2);
	alpha2_code[2] = ' '; /* set 3rd byte of countrycode to ASCII space */

	status = slsi_set_country_update_regd(sdev, alpha2_code, SLSI_COUNTRY_CODE_LEN);

	return status;
}

static ssize_t slsi_update_rssi_boost(struct net_device *dev, char *rssi_boost_string)
{
	struct netdev_vif *netdev_vif = netdev_priv(dev);
	struct slsi_dev   *sdev = netdev_vif->sdev;
	int digit1, digit2, band, lendigit1, lendigit2;
	int boost = 0, length = 0, i = 0;

	if (strlen(rssi_boost_string) < 8)
		return -EINVAL;
	for (i = 0; i < (strlen(rssi_boost_string) - 4);) {
		if (rssi_boost_string[i] == '0' &&
		    rssi_boost_string[i + 1] == '4') {
			if (rssi_boost_string[i + 2] == '0' &&
			    rssi_boost_string[i + 3] == '2' &&
			    ((i + 7) < strlen(rssi_boost_string)))
				i = i + 4;
			else
				return -EINVAL;
			digit1 = slsi_parse_hex(rssi_boost_string[i]);
			digit2 = slsi_parse_hex(rssi_boost_string[i + 1]);
			boost = (digit1 * 16) + digit2;
			band = rssi_boost_string[i + 3] - '0';
			SLSI_MUTEX_LOCK(sdev->device_config_mutex);
			if (band == 0) {
				sdev->device_config.rssi_boost_2g = 0;
				sdev->device_config.rssi_boost_5g = 0;
			} else if (band == 1) {
				sdev->device_config.rssi_boost_2g = 0;
				sdev->device_config.rssi_boost_5g = boost;
			} else {
				sdev->device_config.rssi_boost_2g = boost;
				sdev->device_config.rssi_boost_5g = 0;
			}
			SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);
			if ((netdev_vif->activated) &&
			    (netdev_vif->vif_type == FAPI_VIFTYPE_STATION)) {
				return slsi_set_boost(sdev, dev);
			} else {
				return 0;
			}
		} else {
			i = i + 2;
			lendigit1 = slsi_parse_hex(rssi_boost_string[i]);
			lendigit2 = slsi_parse_hex(rssi_boost_string[i + 1]);
			length = (lendigit1 * 16) + lendigit2;
			i = i + (length * 2) + 2;
		}
	}
	return -EINVAL;
}

static int slsi_tdls_channel_switch(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif  *ndev_vif = netdev_priv(dev);
	struct slsi_dev    *sdev = ndev_vif->sdev;
	int                r, len_processed;
	u8                 peer_mac[6];
	u32                center_freq = 0;
	u32                chan_info = 0;
	int                is_ch_switch;
	struct slsi_peer   *peer;

/*  Command format:
 *      [0/1] [mac address] [center frequency] [channel_info]
 *  channel switch: "1 00:01:02:03:04:05 2412 20"
 *  cancel channel switch: "0 00:01:02:03:04:05"
 */
/* switch/cancel(1 byte) space(1byte) macaddress 17 char */
#define SLSI_TDLS_IOCTL_CMD_DATA_MIN_LEN 19

	if (buf_len < SLSI_TDLS_IOCTL_CMD_DATA_MIN_LEN) {
		SLSI_NET_ERR(dev, "buf len should be atleast %d. but is:%d\n", SLSI_TDLS_IOCTL_CMD_DATA_MIN_LEN, buf_len);
		return -EINVAL;
	}

	if (ndev_vif->sta.sta_bss == NULL) {
		SLSI_NET_ERR(dev, "sta_bss is not available\n");
		return -EINVAL;
	}

	is_ch_switch = command[0] - '0';
	buf_len -= 2;
	command += 2;

	slsi_machexstring_to_macarray(command, peer_mac);

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	if (is_ch_switch) {
		/* mac address(17 char) + space(1 char) = 18 */
		command += 18;
		buf_len -= 18;

		len_processed = slsi_str_to_int(command, &center_freq);
		/* +1 for space */
		buf_len -= len_processed + 1;
		command += len_processed + 1;
		if (buf_len <= 0) {
			SLSI_NET_ERR(dev, "No buf for chan_info\n");
			r = -EINVAL;
			goto exit;
		}
		buf_len -= slsi_str_to_int(command, &chan_info);

		if (((chan_info & 0xFF) != 20)  && ((chan_info & 0xFF) != 40)) {
			SLSI_NET_ERR(dev, "Invalid chan_info(%d)\n", chan_info);
			r = -EINVAL;
			goto exit;
		}
		/* In 2.4 Ghz channel 1(2412MHz) to channel 14(2484MHz) */
		/* for 40MHz channels are from 1 to 13, its 2422MHz to 2462MHz. */
		if ((((chan_info & 0xFF) == 20) && (center_freq < 2412 || center_freq > 2484)) ||
		    (((chan_info & 0xFF) == 40) && (center_freq < 2422 || center_freq > 2462))) {
			SLSI_NET_ERR(dev, "Invalid center_freq(%d) for chan_info(%d)\n", center_freq, chan_info);
			r = -EINVAL;
			goto exit;
		}
	} else {
		/* Incase of cancel channel switch fallback to bss channel */
		center_freq = ndev_vif->sta.sta_bss->channel->center_freq;
		chan_info = 20; /* Hardcoded to 20MHz as cert tests use BSS with 20MHz */
	}

	peer = slsi_get_peer_from_mac(sdev, dev, peer_mac);

	if (!peer || !slsi_is_tdls_peer(dev, peer)) {
		SLSI_NET_ERR(dev, "%s peer aid:%d\n", peer ? "Invalid" : "No", peer ? peer->aid : 0);
		r = -EINVAL;
		goto exit;
	}

	r = slsi_mlme_tdls_action(sdev, dev, peer_mac, FAPI_TDLSACTION_CHANNEL_SWITCH, center_freq, chan_info);

exit:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	return r;
}

int slsi_set_tx_power_calling(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif    *ndev_vif = netdev_priv(dev);
	struct slsi_dev      *sdev = ndev_vif->sdev;
	int                  mode;
	int                  error = 0;
	u8                   host_state;

	(void)slsi_str_to_int(command, &mode);
	SLSI_MUTEX_LOCK(sdev->device_config_mutex);
	host_state = sdev->device_config.host_state;

	if (!mode)
		host_state = host_state | FAPI_HOSTSTATE_SAR_ACTIVE;
	else
		host_state = host_state & ~FAPI_HOSTSTATE_SAR_ACTIVE;

	error = slsi_mlme_set_host_state(sdev, dev, host_state);
	if (!error)
		sdev->device_config.host_state = host_state;

	SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);

	return error;
}

int slsi_set_tx_power_sar(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif    *ndev_vif = netdev_priv(dev);
	struct slsi_dev      *sdev = ndev_vif->sdev;
	int                  mode;
	int                  error = 0;
	u8                   host_state;

	(void)slsi_str_to_int(command, &mode);
	SLSI_MUTEX_LOCK(sdev->device_config_mutex);
	host_state = sdev->device_config.host_state;
	host_state &= ~(FAPI_HOSTSTATE_SAR_ACTIVE | BIT(3) | BIT(4));

	if (mode)
		host_state |= ((mode - 1) << 3) | FAPI_HOSTSTATE_SAR_ACTIVE;

	error = slsi_mlme_set_host_state(sdev, dev, host_state);
	if (!error)
		sdev->device_config.host_state = host_state;

	SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);

	return error;
}

int slsi_get_tx_power_sar(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif        *ndev_vif = netdev_priv(dev);
	struct slsi_dev      *sdev = ndev_vif->sdev;
	int len = 0;
	u8                   host_state, index;

	SLSI_MUTEX_LOCK(sdev->device_config_mutex);
	host_state = sdev->device_config.host_state;

	if (host_state & FAPI_HOSTSTATE_SAR_ACTIVE)
		index = ((host_state >> 3) & 3) + 1;
	else
		index = 0;

	len = snprintf(command, buf_len, "%u", index);
	SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);

	return len;
}

static int slsi_print_regulatory(struct slsi_802_11d_reg_domain *domain_info, char *buf, int buf_len, struct slsi_supported_channels *supported_channels, int supp_chan_length)
{
	int  cur_pos = 0;
	int  i, j, k;
	char *dfs_region_str[] = {"unknown", "ETSI", "FCC", "JAPAN", "GLOBAL", "CHINA"};
	u8   dfs_region_index;
	struct ieee80211_reg_rule *reg_rule;
	int  channel_start_freq = 0;
	int  channel_end_freq = 0;
	int  channel_start_num = 0;
	int  channel_end_num = 0;
	int  channel_count = 0;
	int  channel_increment = 0;
	int  channel_band = 0;
	bool display_pattern = false;

	cur_pos = snprintf(buf, buf_len, "country %c%c:", domain_info->regdomain->alpha2[0],
			   domain_info->regdomain->alpha2[1]);
	dfs_region_index = domain_info->regdomain->dfs_region <= 5 ? domain_info->regdomain->dfs_region : 0;
	cur_pos += snprintf(buf + cur_pos, buf_len - cur_pos, "DFS-%s\n", dfs_region_str[dfs_region_index]);
	for (i = 0; i < domain_info->regdomain->n_reg_rules; i++) {
		reg_rule = &domain_info->regdomain->reg_rules[i];
		cur_pos += snprintf(buf + cur_pos, buf_len - cur_pos, "\t(%d-%d @ %d), (N/A, %d)",
					reg_rule->freq_range.start_freq_khz/1000,
					reg_rule->freq_range.end_freq_khz/1000,
					reg_rule->freq_range.max_bandwidth_khz/1000,
					MBM_TO_DBM(reg_rule->power_rule.max_eirp));
		if (reg_rule->flags) {
			if (reg_rule->flags & NL80211_RRF_DFS)
				cur_pos += snprintf(buf + cur_pos, buf_len - cur_pos, ", DFS");
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 9))
			if (reg_rule->flags & NL80211_RRF_NO_OFDM)
				cur_pos += snprintf(buf + cur_pos, buf_len - cur_pos, ", NO_OFDM");
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0))
			if (reg_rule->flags & (NL80211_RRF_PASSIVE_SCAN|NL80211_RRF_NO_IBSS))
				cur_pos += snprintf(buf + cur_pos, buf_len - cur_pos, ", NO_IR");
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
			if (reg_rule->flags & (NL80211_RRF_NO_IR))
				cur_pos += snprintf(buf + cur_pos, buf_len - cur_pos, ", NO_IR");
#endif
			if (reg_rule->flags & NL80211_RRF_NO_INDOOR)
				cur_pos += snprintf(buf + cur_pos, buf_len - cur_pos, ", NO_INDOOR");
			if (reg_rule->flags & NL80211_RRF_NO_OUTDOOR)
				cur_pos += snprintf(buf + cur_pos, buf_len - cur_pos, ", NO_OUTDOOR");
		}
		cur_pos += snprintf(buf + cur_pos, buf_len - cur_pos, "\n");
	}

	/* Display of Supported Channels for 2.4GHz and 5GHz */
	cur_pos += snprintf(buf + cur_pos, buf_len - cur_pos, "Channels:");

	for (i = 0; i < supp_chan_length; i++) {
		channel_start_num = supported_channels[i].start_chan_num;
		channel_count = supported_channels[i].channel_count;
		channel_increment = supported_channels[i].increment;
		channel_band = supported_channels[i].band;
		channel_end_num = channel_start_num + ((channel_count - 1) * channel_increment);
		for (j = channel_start_num; j <= channel_end_num; j += channel_increment) {
			channel_start_freq = (ieee80211_channel_to_frequency(j, channel_band)*1000) - 10000;
			channel_end_freq = (ieee80211_channel_to_frequency(j, channel_band)*1000) + 10000;
			for (k = 0; k < domain_info->regdomain->n_reg_rules; k++) {
				reg_rule = &domain_info->regdomain->reg_rules[k];
				if ((reg_rule->freq_range.start_freq_khz <= channel_start_freq) &&
				    (reg_rule->freq_range.end_freq_khz >= channel_end_freq)) {
					if (display_pattern)
						cur_pos += snprintf(buf + cur_pos, buf_len - cur_pos, ", %d", j);
					else
						cur_pos += snprintf(buf + cur_pos, buf_len - cur_pos, " %d", j);
					display_pattern = true;
					break;
				}
			}
		}
	}
	cur_pos += snprintf(buf + cur_pos, buf_len - cur_pos, "\n");
	return cur_pos;
}

static int slsi_get_supported_channels(struct slsi_dev *sdev, struct net_device *dev, struct slsi_supported_channels *supported_channels)
{
	struct slsi_mib_data      mibrsp = { 0, NULL };
	struct slsi_mib_data      supported_chan_mib = { 0, NULL };
	struct slsi_mib_value     *values = NULL;
	struct slsi_mib_get_entry get_values[] = {{SLSI_PSID_UNIFI_SUPPORTED_CHANNELS, { 0, 0 } } };
	int                       i, chan_count, chan_start;
	int			  supp_chan_length = 0;

	/* Expect each mib length in response is <= 16. So assume 16 bytes for each MIB */
	mibrsp.dataLength = 16;
	mibrsp.data = kmalloc(mibrsp.dataLength, GFP_KERNEL);
	if (mibrsp.data == NULL) {
		SLSI_ERR(sdev, "Cannot kmalloc %d bytes\n", mibrsp.dataLength);
		return 0;
	}
	values = slsi_read_mibs(sdev, dev, get_values, 1, &mibrsp);
	if (!values)
		goto exit_with_mibrsp;

	if (values[0].type != SLSI_MIB_TYPE_OCTET) {
		SLSI_ERR(sdev, "Supported_Chan invalid type.");
		goto exit_with_values;
	}

	supported_chan_mib = values[0].u.octetValue;
	for (i = 0; i < supported_chan_mib.dataLength / 2; i++) {
		chan_start = supported_chan_mib.data[i*2];
		chan_count = supported_chan_mib.data[i*2 + 1];
		if (chan_start == 1) { /* for 2.4GHz */
			supported_channels[supp_chan_length].start_chan_num = 1;
			supported_channels[supp_chan_length].channel_count = chan_count;
			supported_channels[supp_chan_length].increment = 1;
			supported_channels[supp_chan_length].band = NL80211_BAND_2GHZ;
			supp_chan_length = supp_chan_length + 1;
		} else { /* for 5GHz */
			supported_channels[supp_chan_length].start_chan_num = chan_start;
			supported_channels[supp_chan_length].channel_count = chan_count;
			supported_channels[supp_chan_length].increment = 4;
			supported_channels[supp_chan_length].band = NL80211_BAND_5GHZ;
			supp_chan_length = supp_chan_length + 1;
		}
	}
exit_with_values:
	kfree(values);
exit_with_mibrsp:
	kfree(mibrsp.data);
	return supp_chan_length;
}

static int slsi_get_regulatory(struct net_device *dev, char *buf, int buf_len)
{
	struct netdev_vif              *ndev_vif = netdev_priv(dev);
	struct slsi_dev                *sdev = ndev_vif->sdev;
	int                            mode;
	int                            cur_pos = 0;
	int                            status;
	u8                             alpha2[3];
	struct slsi_supported_channels supported_channels[5];
	int			       supp_chan_length;

	mode = buf[strlen(CMD_GETREGULATORY) + 1] - '0';
	if (mode == 1) {
		struct slsi_802_11d_reg_domain domain_info;

		memset(&domain_info, 0, sizeof(struct slsi_802_11d_reg_domain));
		SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
		if (!ndev_vif->activated || ndev_vif->vif_type != FAPI_VIFTYPE_STATION || !ndev_vif->sta.sta_bss) {
			cur_pos += snprintf(buf, buf_len - cur_pos, "Station not connected");
			SLSI_ERR(sdev, "station not connected. vif.activated:%d, vif.type:%d, vif.bss:%s\n",
				 ndev_vif->activated, ndev_vif->vif_type, ndev_vif->sta.sta_bss ? "yes" : "no");
			SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
			return -EINVAL;
		}
		/* read vif specific country code, index = vifid+1 */
		status = slsi_read_default_country(sdev, alpha2, ndev_vif->ifnum + 1);
		SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
		if (status)
			return status;

		/* max 20 rules */
		domain_info.regdomain = kmalloc(sizeof(*domain_info.regdomain) + sizeof(struct ieee80211_reg_rule) * 20, GFP_KERNEL);
		if (!domain_info.regdomain) {
			SLSI_ERR(sdev, "no memory size:%lu\n",
				 sizeof(struct ieee80211_regdomain) + sizeof(struct ieee80211_reg_rule) * 20);
			return -ENOMEM;
		}

		/* get regulatory rules based on country code */
		domain_info.countrylist = sdev->device_config.domain_info.countrylist;
		domain_info.country_len = sdev->device_config.domain_info.country_len;
		status = slsi_read_regulatory_rules(sdev, &domain_info, alpha2);
		if (status) {
			kfree(domain_info.regdomain);
			return status;
		}
		/* get supported channels based on country code */
		supp_chan_length = slsi_get_supported_channels(sdev, dev, &supported_channels[0]);
		cur_pos += slsi_print_regulatory(&domain_info, buf + cur_pos, buf_len - cur_pos, &supported_channels[0], supp_chan_length);
		kfree(domain_info.regdomain);
	} else if (mode == 0) {
		SLSI_MUTEX_LOCK(sdev->device_config_mutex);
		supp_chan_length = slsi_get_supported_channels(sdev, dev, &supported_channels[0]);
		cur_pos += slsi_print_regulatory(&sdev->device_config.domain_info, buf + cur_pos, buf_len - cur_pos, &supported_channels[0], supp_chan_length);
		SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);
	} else {
		cur_pos += snprintf(buf, buf_len - cur_pos, "invalid option %d", mode);
		SLSI_ERR(sdev, "invalid option:%d\n", mode);
		return -EINVAL;
	}
	/* Buf is somewhere close to 4Kbytes. so expect some spare space. If there is no spare
	 * space we might have missed printing some text in buf.
	 */
	if (buf_len - cur_pos)
		return cur_pos;
	else
		return -ENOMEM;
}

int slsi_set_fcc_channel(struct net_device *dev, char *cmd, int cmd_len)
{
	struct netdev_vif    *ndev_vif = netdev_priv(dev);
	struct slsi_dev      *sdev = ndev_vif->sdev;
	int                  status;
	bool                 flight_mode_ena;
	u8                   host_state;
	int                  err;
	char                 alpha2[3];

	/* SET_FCC_CHANNEL 0 when device is in flightmode */
	flight_mode_ena = (cmd[0]  == '0');
	SLSI_MUTEX_LOCK(sdev->device_config_mutex);
	host_state = sdev->device_config.host_state;

	if (flight_mode_ena)
		host_state = host_state & ~FAPI_HOSTSTATE_CELLULAR_ACTIVE;
	else
		host_state = host_state | FAPI_HOSTSTATE_CELLULAR_ACTIVE;
	sdev->device_config.host_state = host_state;

	status = slsi_mlme_set_host_state(sdev, dev, host_state);
	if (status) {
		SLSI_ERR(sdev, "Err setting MMaxPowerEna. error = %d\n", status);
	} else {
		err = slsi_read_default_country(sdev, alpha2, 1);
		if (err) {
			SLSI_WARN(sdev, "Err updating reg_rules = %d\n", err);
		} else {
			memcpy(sdev->device_config.domain_info.regdomain->alpha2, alpha2, 2);
			/* Read the regulatory params for the country.*/
			if (slsi_read_regulatory_rules(sdev, &sdev->device_config.domain_info, alpha2) == 0) {
				slsi_reset_channel_flags(sdev);
				wiphy_apply_custom_regulatory(sdev->wiphy, sdev->device_config.domain_info.regdomain);
				slsi_update_supported_channels_regd_flags(sdev);
			}
		}
	}
	SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);

	return status;
}

int slsi_fake_mac_write(struct net_device *dev, char *cmd)
{
	struct netdev_vif    *ndev_vif = netdev_priv(dev);
	struct slsi_dev      *sdev = ndev_vif->sdev;
	struct slsi_mib_data mib_data = { 0, NULL };
	int                  status;
	bool                enable;

	if (strncmp(cmd, "ON", strlen("ON")) == 0)
		enable = 1;
	else
		enable = 0;

	status = slsi_mib_encode_bool(&mib_data, SLSI_PSID_UNIFI_MAC_ADDRESS_RANDOMISATION_ACTIVATED, enable, 0);
	if (status != SLSI_MIB_STATUS_SUCCESS) {
		SLSI_ERR(sdev, "FAKE MAC FAIL: no mem for MIB\n");
		return -ENOMEM;
	}

	status = slsi_mlme_set(sdev, NULL, mib_data.data, mib_data.dataLength);

	kfree(mib_data.data);

	if (status)
		SLSI_ERR(sdev, "Err setting unifiMacAddrRandomistaion MIB. error = %d\n", status);

	return status;
}

static char *slsi_get_assoc_status(u16 fw_result_code)
{
	char *assoc_status_label = "unspecified_failure";

	switch (fw_result_code) {
	case FAPI_RESULTCODE_SUCCESS:
		assoc_status_label = "success";
		break;
	case FAPI_RESULTCODE_TRANSMISSION_FAILURE:
		assoc_status_label = "transmission_failure";
		break;
	case FAPI_RESULTCODE_HOST_REQUEST_SUCCESS:
		assoc_status_label = "host_request_success";
		break;
	case FAPI_RESULTCODE_HOST_REQUEST_FAILED:
		assoc_status_label = "host_request_failed";
		break;
	case FAPI_RESULTCODE_PROBE_TIMEOUT:
		assoc_status_label = "probe_timeout";
		break;
	case FAPI_RESULTCODE_AUTH_TIMEOUT:
		assoc_status_label = "auth_timeout";
		break;
	case FAPI_RESULTCODE_ASSOC_TIMEOUT:
		assoc_status_label = "assoc_timeout";
		break;
	case FAPI_RESULTCODE_ASSOC_ABORT:
		assoc_status_label = "assoc_abort";
		break;
	}
	return assoc_status_label;
}

int slsi_get_sta_info(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif   *ndev_vif = netdev_priv(dev);
	struct slsi_dev      *sdev = ndev_vif->sdev;
	int                       len;
#ifdef CONFIG_SCSC_WLAN_WIFI_SHARING
	struct net_device *ap_dev;
	struct netdev_vif *ndev_ap_vif;

	ap_dev = slsi_get_netdev(sdev, SLSI_NET_INDEX_P2PX_SWLAN);

	if (ap_dev) {
		ndev_ap_vif = netdev_priv(ap_dev);
		SLSI_MUTEX_LOCK(ndev_ap_vif->vif_mutex);
		if (SLSI_IS_VIF_INDEX_MHS(sdev, ndev_ap_vif))
			ndev_vif = ndev_ap_vif;
		SLSI_MUTEX_UNLOCK(ndev_ap_vif->vif_mutex);
	}
#endif

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	if ((!ndev_vif->activated) || (ndev_vif->vif_type != FAPI_VIFTYPE_AP)) {
		SLSI_ERR(sdev, "slsi_get_sta_info: AP is not up.Command not allowed vif.activated:%d, vif.type:%d\n",
			 ndev_vif->activated, ndev_vif->vif_type);
		SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
		return -EINVAL;
	}

	len = snprintf(command, buf_len, "wl_get_sta_info : %02x%02x%02x %u %d %d %d %d %d %d %u ",
		       ndev_vif->ap.last_disconnected_sta.address[0], ndev_vif->ap.last_disconnected_sta.address[1],
		       ndev_vif->ap.last_disconnected_sta.address[2], ndev_vif->ap.channel_freq,
		       ndev_vif->ap.last_disconnected_sta.bandwidth, ndev_vif->ap.last_disconnected_sta.rssi,
		       ndev_vif->ap.last_disconnected_sta.tx_data_rate, ndev_vif->ap.last_disconnected_sta.mode,
		       ndev_vif->ap.last_disconnected_sta.antenna_mode,
		       ndev_vif->ap.last_disconnected_sta.mimo_used, ndev_vif->ap.last_disconnected_sta.reason);

	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);

	return len;
}

static int slsi_get_bss_rssi(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif        *ndev_vif = netdev_priv(dev);
	int len = 0;

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	len = snprintf(command, buf_len, "%d", ndev_vif->sta.last_connected_bss.rssi);
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);

	return len;
}

static int slsi_get_bss_info(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif        *ndev_vif = netdev_priv(dev);
	int len = 0;

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	len = snprintf(command, buf_len, "%02x:%02x:%02x %u %u %d %u %u %u %u %u %d %d %u %u %u %u",
		       ndev_vif->sta.last_connected_bss.address[0],  ndev_vif->sta.last_connected_bss.address[1],
		       ndev_vif->sta.last_connected_bss.address[2],
		       ndev_vif->sta.last_connected_bss.channel_freq, ndev_vif->sta.last_connected_bss.bandwidth,
		       ndev_vif->sta.last_connected_bss.rssi, ndev_vif->sta.last_connected_bss.tx_data_rate,
		       ndev_vif->sta.last_connected_bss.mode, ndev_vif->sta.last_connected_bss.antenna_mode,
		       ndev_vif->sta.last_connected_bss.mimo_used,
		       ndev_vif->sta.last_connected_bss.passpoint_version, ndev_vif->sta.last_connected_bss.snr,
		       ndev_vif->sta.last_connected_bss.noise_level, ndev_vif->sta.last_connected_bss.roaming_akm,
		       ndev_vif->sta.last_connected_bss.roaming_count, ndev_vif->sta.last_connected_bss.kv,
		       ndev_vif->sta.last_connected_bss.kvie);

	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);

	return len;
}

static int slsi_get_assoc_reject_info(struct net_device *dev, char *command, int buf_len)
{
	struct netdev_vif    *ndev_vif = netdev_priv(dev);
	struct slsi_dev      *sdev = ndev_vif->sdev;
	int len = 0;

	len = snprintf(command, buf_len, "assoc_reject.status : %d %s", sdev->assoc_result_code,
		       slsi_get_assoc_status(sdev->assoc_result_code));

	return len;
}

int slsi_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
#define MAX_LEN_PRIV_COMMAND    4096 /*This value is the max reply size set in supplicant*/
	struct android_wifi_priv_cmd priv_cmd;
	int                          ret = 0;
	u8                           *command = NULL;

	if (!dev) {
		ret = -ENODEV;
		goto exit;
	}

	if (!rq->ifr_data) {
		ret = -EINVAL;
		goto exit;
	}
	if (copy_from_user((void *)&priv_cmd, (void *)rq->ifr_data, sizeof(struct android_wifi_priv_cmd))) {
		ret = -EFAULT;
		SLSI_NET_ERR(dev, "ifr data failed\n");
		goto exit;
	}

	if ((priv_cmd.total_len > MAX_LEN_PRIV_COMMAND) || (priv_cmd.total_len < 0)) {
		ret = -EINVAL;
		SLSI_NET_ERR(dev, "Length mismatch total_len = %d\n", priv_cmd.total_len);
		goto exit;
	}
	command = kmalloc((priv_cmd.total_len + 1), GFP_KERNEL);
	if (!command) {
		ret = -ENOMEM;
		SLSI_NET_ERR(dev, "No memory\n");
		goto exit;
	}
	if (copy_from_user(command, priv_cmd.buf, priv_cmd.total_len)) {
		ret = -EFAULT;
		SLSI_NET_ERR(dev, "Buffer copy fail\n");
		goto exit;
	}
	command[priv_cmd.total_len] = '\0';

	SLSI_INFO_NODEV("command: %.*s\n", priv_cmd.total_len, command);

	if (strncasecmp(command, CMD_SETSUSPENDMODE, strlen(CMD_SETSUSPENDMODE)) == 0) {
		ret = slsi_set_suspend_mode(dev, command);
	} else if (strncasecmp(command, CMD_SETJOINPREFER, strlen(CMD_SETJOINPREFER)) == 0) {
		char *rssi_boost_string = command + strlen(CMD_SETJOINPREFER) + 1;

		ret = slsi_update_rssi_boost(dev, rssi_boost_string);
	} else if (strncasecmp(command, CMD_RXFILTERADD, strlen(CMD_RXFILTERADD)) == 0) {
		int filter_num = *(command + strlen(CMD_RXFILTERADD) + 1) - '0';

		ret = slsi_rx_filter_num_write(dev, 1, filter_num);
	} else if (strncasecmp(command, CMD_RXFILTERREMOVE, strlen(CMD_RXFILTERREMOVE)) == 0) {
		int filter_num = *(command + strlen(CMD_RXFILTERREMOVE) + 1) - '0';

		ret = slsi_rx_filter_num_write(dev, 0, filter_num);
#ifdef CONFIG_SCSC_WLAN_WIFI_SHARING
	} else if (strncasecmp(command, CMD_INTERFACE_CREATE, strlen(CMD_INTERFACE_CREATE)) == 0) {
		char *intf_name = command + strlen(CMD_INTERFACE_CREATE) + 1;

		ret = slsi_create_interface(dev, intf_name);
	} else if (strncasecmp(command, CMD_INTERFACE_DELETE, strlen(CMD_INTERFACE_DELETE)) == 0) {
		char *intf_name = command + strlen(CMD_INTERFACE_DELETE) + 1;

		ret = slsi_delete_interface(dev, intf_name);
	} else if (strncasecmp(command, CMD_SET_INDOOR_CHANNELS, strlen(CMD_SET_INDOOR_CHANNELS)) == 0) {
		char *arg = command + strlen(CMD_SET_INDOOR_CHANNELS) + 1;

		ret = slsi_set_indoor_channels(dev, arg);
	} else if (strncasecmp(command, CMD_GET_INDOOR_CHANNELS, strlen(CMD_GET_INDOOR_CHANNELS)) == 0) {
		ret = slsi_get_indoor_channels(dev, command, priv_cmd.total_len);
#endif
	} else if (strncasecmp(command, CMD_SETCOUNTRYREV, strlen(CMD_SETCOUNTRYREV)) == 0) {
		char *country_code = command + strlen(CMD_SETCOUNTRYREV) + 1;

		ret = slsi_set_country_rev(dev, country_code);
	} else if (strncasecmp(command, CMD_GETCOUNTRYREV, strlen(CMD_GETCOUNTRYREV)) == 0) {
		ret = slsi_get_country_rev(dev, command, priv_cmd.total_len);
#ifdef CONFIG_SCSC_WLAN_WES_NCHO
	} else if (strncasecmp(command, CMD_SETROAMTRIGGER, strlen(CMD_SETROAMTRIGGER)) == 0) {
		int skip = strlen(CMD_SETROAMTRIGGER) + 1;

		ret = slsi_roam_scan_trigger_write(dev, command + skip,
						   priv_cmd.total_len - skip);
	} else if (strncasecmp(command, CMD_GETROAMTRIGGER, strlen(CMD_GETROAMTRIGGER)) == 0) {
		ret = slsi_roam_scan_trigger_read(dev, command, priv_cmd.total_len);
	} else if (strncasecmp(command, CMD_SETROAMDELTA, strlen(CMD_SETROAMDELTA)) == 0) {
		int skip = strlen(CMD_SETROAMDELTA) + 1;

		ret = slsi_roam_delta_trigger_write(dev, command + skip,
						    priv_cmd.total_len - skip);
	} else if (strncasecmp(command, CMD_GETROAMDELTA, strlen(CMD_GETROAMDELTA)) == 0) {
		ret = slsi_roam_delta_trigger_read(dev, command, priv_cmd.total_len);
	} else if (strncasecmp(command, CMD_SETROAMSCANPERIOD, strlen(CMD_SETROAMSCANPERIOD)) == 0) {
		int skip = strlen(CMD_SETROAMSCANPERIOD) + 1;

		ret = slsi_cached_channel_scan_period_write(dev, command + skip,
							    priv_cmd.total_len - skip);
	} else if (strncasecmp(command, CMD_GETROAMSCANPERIOD, strlen(CMD_GETROAMSCANPERIOD)) == 0) {
		ret = slsi_cached_channel_scan_period_read(dev, command, priv_cmd.total_len);
	} else if (strncasecmp(command, CMD_SETFULLROAMSCANPERIOD, strlen(CMD_SETFULLROAMSCANPERIOD)) == 0) {
		int skip = strlen(CMD_SETFULLROAMSCANPERIOD) + 1;

		ret = slsi_full_roam_scan_period_write(dev, command + skip,
						       priv_cmd.total_len - skip);
	} else if (strncasecmp(command, CMD_GETFULLROAMSCANPERIOD, strlen(CMD_GETFULLROAMSCANPERIOD)) == 0) {
		ret = slsi_full_roam_scan_period_read(dev, command, priv_cmd.total_len);
	} else if (strncasecmp(command, CMD_SETSCANCHANNELTIME, strlen(CMD_SETSCANCHANNELTIME)) == 0) {
		int skip = strlen(CMD_SETSCANCHANNELTIME) + 1;

		ret = slsi_roam_scan_max_active_channel_time_write(dev, command + skip,
								   priv_cmd.total_len - skip);
	} else if (strncasecmp(command, CMD_GETSCANCHANNELTIME, strlen(CMD_GETSCANCHANNELTIME)) == 0) {
		ret = slsi_roam_scan_max_active_channel_time_read(dev, command, priv_cmd.total_len);
	} else if (strncasecmp(command, CMD_SETSCANNPROBES, strlen(CMD_SETSCANNPROBES)) == 0) {
		int skip = strlen(CMD_SETSCANNPROBES) + 1;

		ret = slsi_roam_scan_probe_interval_write(dev, command + skip,
							  priv_cmd.total_len - skip);
	} else if (strncasecmp(command, CMD_GETSCANNPROBES, strlen(CMD_GETSCANNPROBES)) == 0) {
		ret = slsi_roam_scan_probe_interval_read(dev, command, priv_cmd.total_len);
	} else if (strncasecmp(command, CMD_SETROAMMODE, strlen(CMD_SETROAMMODE)) == 0) {
		int skip = strlen(CMD_SETROAMMODE) + 1;

		ret = slsi_roam_mode_write(dev, command + skip,
					   priv_cmd.total_len - skip);
	} else if (strncasecmp(command, CMD_GETROAMMODE, strlen(CMD_GETROAMMODE)) == 0) {
		ret = slsi_roam_mode_read(dev, command, priv_cmd.total_len);
	} else if (strncasecmp(command, CMD_SETROAMINTRABAND, strlen(CMD_SETROAMINTRABAND)) == 0) {
		int skip = strlen(CMD_SETROAMINTRABAND) + 1;

		ret = slsi_roam_scan_band_write(dev, command + skip,
						priv_cmd.total_len - skip);
	} else if (strncasecmp(command, CMD_GETROAMINTRABAND, strlen(CMD_GETROAMINTRABAND)) == 0) {
		ret = slsi_roam_scan_band_read(dev, command, priv_cmd.total_len);
	} else if (strncasecmp(command, CMD_SETROAMBAND, strlen(CMD_SETROAMBAND)) == 0) {
		uint band = *(command + strlen(CMD_SETROAMBAND) + 1) - '0';

		ret = slsi_freq_band_write(dev, band);
	} else if (strncasecmp(command, CMD_SETBAND, strlen(CMD_SETBAND)) == 0) {
		uint band = *(command + strlen(CMD_SETBAND) + 1) - '0';

		ret = slsi_freq_band_write(dev, band);
	} else if ((strncasecmp(command, CMD_GETROAMBAND, strlen(CMD_GETROAMBAND)) == 0) || (strncasecmp(command, CMD_GETBAND, strlen(CMD_GETBAND)) == 0)) {
		ret = slsi_freq_band_read(dev, command, priv_cmd.total_len);
	} else if (strncasecmp(command, CMD_SETROAMSCANCONTROL, strlen(CMD_SETROAMSCANCONTROL)) == 0) {
		int mode = *(command + strlen(CMD_SETROAMSCANCONTROL) + 1) - '0';

		ret = slsi_roam_scan_control_write(dev, mode);
	} else if (strncasecmp(command, CMD_GETROAMSCANCONTROL, strlen(CMD_GETROAMSCANCONTROL)) == 0) {
		ret = slsi_roam_scan_control_read(dev, command, priv_cmd.total_len);
	} else if (strncasecmp(command, CMD_SETSCANHOMETIME, strlen(CMD_SETSCANHOMETIME)) == 0) {
		int skip = strlen(CMD_SETSCANHOMETIME) + 1;

		ret = slsi_roam_scan_home_time_write(dev, command + skip,
						     priv_cmd.total_len - skip);
	} else if (strncasecmp(command, CMD_GETSCANHOMETIME, strlen(CMD_GETSCANHOMETIME)) == 0) {
		ret = slsi_roam_scan_home_time_read(dev, command, priv_cmd.total_len);
	} else if (strncasecmp(command, CMD_SETSCANHOMEAWAYTIME, strlen(CMD_SETSCANHOMEAWAYTIME)) == 0) {
		int skip = strlen(CMD_SETSCANHOMEAWAYTIME) + 1;

		ret = slsi_roam_scan_home_away_time_write(dev, command + skip,
							  priv_cmd.total_len - skip);
	} else if (strncasecmp(command, CMD_GETSCANHOMEAWAYTIME, strlen(CMD_GETSCANHOMEAWAYTIME)) == 0) {
		ret = slsi_roam_scan_home_away_time_read(dev, command, priv_cmd.total_len);
	} else if (strncasecmp(command, CMD_SETOKCMODE, strlen(CMD_SETOKCMODE)) == 0) {
		int mode = *(command + strlen(CMD_SETOKCMODE) + 1) - '0';

		ret = slsi_okc_mode_write(dev, mode);
	} else if (strncasecmp(command, CMD_GETOKCMODE, strlen(CMD_GETOKCMODE)) == 0) {
		ret = slsi_okc_mode_read(dev, command, priv_cmd.total_len);
	} else if (strncasecmp(command, CMD_SETWESMODE, strlen(CMD_SETWESMODE)) == 0) {
		int mode = *(command + strlen(CMD_SETWESMODE) + 1) - '0';

		ret = slsi_wes_mode_write(dev, mode);
	} else if (strncasecmp(command, CMD_GETWESMODE, strlen(CMD_GETWESMODE)) == 0) {
		ret = slsi_wes_mode_read(dev, command, priv_cmd.total_len);
	} else if (strncasecmp(command, CMD_SETROAMSCANCHANNELS, strlen(CMD_SETROAMSCANCHANNELS)) == 0) {
		int skip = strlen(CMD_SETROAMSCANCHANNELS) + 1;

		ret = slsi_roam_scan_channels_write(dev, command + skip,
						    priv_cmd.total_len - skip);
	} else if (strncasecmp(command, CMD_GETROAMSCANCHANNELS, strlen(CMD_GETROAMSCANCHANNELS)) == 0) {
		ret = slsi_roam_scan_channels_read(dev, command, priv_cmd.total_len);
#endif
	} else if (strncasecmp(command, CMD_SET_PMK, strlen(CMD_SET_PMK)) == 0) {
		ret = slsi_set_pmk(dev, command, priv_cmd.total_len);
	} else if (strncasecmp(command, CMD_HAPD_GET_CHANNEL, strlen(CMD_HAPD_GET_CHANNEL)) == 0) {
		ret = slsi_auto_chan_read(dev, command, priv_cmd.total_len);
	} else if (strncasecmp(command, CMD_SET_SAP_CHANNEL_LIST, strlen(CMD_SET_SAP_CHANNEL_LIST)) == 0) {
		ret = slsi_auto_chan_write(dev, command, priv_cmd.total_len);
	} else if (strncasecmp(command, CMD_REASSOC, strlen(CMD_REASSOC)) == 0) {
		int skip = strlen(CMD_REASSOC) + 1;

		ret = slsi_reassoc_write(dev, command + skip,
					 priv_cmd.total_len - skip);
	} else if (strncasecmp(command, CMD_SENDACTIONFRAME, strlen(CMD_SENDACTIONFRAME)) == 0) {
		int skip = strlen(CMD_SENDACTIONFRAME) + 1;

		ret = slsi_send_action_frame(dev, command + skip,
					     priv_cmd.total_len - skip);
	} else if (strncasecmp(command, CMD_HAPD_MAX_NUM_STA, strlen(CMD_HAPD_MAX_NUM_STA)) == 0) {
		int sta_num;
		u8 *max_sta = command + strlen(CMD_HAPD_MAX_NUM_STA) + 1;

		slsi_str_to_int(max_sta, &sta_num);
		ret = slsi_setting_max_sta_write(dev, sta_num);
	} else if (strncasecmp(command, CMD_COUNTRY, strlen(CMD_COUNTRY)) == 0) {
		char *country_code = command + strlen(CMD_COUNTRY) + 1;

		ret = slsi_country_write(dev, country_code);
	} else if (strncasecmp(command, CMD_SETAPP2PWPSIE, strlen(CMD_SETAPP2PWPSIE)) == 0) {
		ret = slsi_set_ap_p2p_wps_ie(dev, command, priv_cmd.total_len);
	} else if (strncasecmp(command, CMD_P2PSETPS, strlen(CMD_P2PSETPS)) == 0) {
		ret = slsi_set_p2p_oppps(dev, command, priv_cmd.total_len);
	} else if (strncasecmp(command, CMD_P2PSETNOA, strlen(CMD_P2PSETNOA)) == 0) {
		ret = slsi_p2p_set_noa_params(dev, command, priv_cmd.total_len);
	} else if (strncasecmp(command, CMD_P2PECSA, strlen(CMD_P2PECSA)) == 0) {
		ret = slsi_p2p_ecsa(dev, command);
	} else if (strncasecmp(command, CMD_P2PLOSTART, strlen(CMD_P2PLOSTART)) == 0) {
		ret = slsi_p2p_lo_start(dev, command);
	} else if (strncasecmp(command, CMD_P2PLOSTOP, strlen(CMD_P2PLOSTOP)) == 0) {
		ret = slsi_p2p_lo_stop(dev);
	} else if (strncasecmp(command, CMD_TDLSCHANNELSWITCH, strlen(CMD_TDLSCHANNELSWITCH)) == 0) {
		ret = slsi_tdls_channel_switch(dev, command + strlen(CMD_TDLSCHANNELSWITCH) + 1,
					       priv_cmd.total_len - (strlen(CMD_TDLSCHANNELSWITCH) + 1));
	} else if (strncasecmp(command, CMD_SETROAMOFFLOAD, strlen(CMD_SETROAMOFFLOAD)) == 0) {
		ret = slsi_roam_mode_write(dev, command + strlen(CMD_SETROAMOFFLOAD) + 1,
					   priv_cmd.total_len - (strlen(CMD_SETROAMOFFLOAD) + 1));
	} else if (strncasecmp(command, CMD_SETROAMOFFLAPLIST, strlen(CMD_SETROAMOFFLAPLIST)) == 0) {
		ret = slsi_roam_offload_ap_list(dev, command + strlen(CMD_SETROAMOFFLAPLIST) + 1,
						priv_cmd.total_len - (strlen(CMD_SETROAMOFFLAPLIST) + 1));
	} else if (strncasecmp(command, CMD_SET_TX_POWER_CALLING, strlen(CMD_SET_TX_POWER_CALLING)) == 0) {
		ret = slsi_set_tx_power_calling(dev, command + strlen(CMD_SET_TX_POWER_CALLING) + 1,
						priv_cmd.total_len - (strlen(CMD_SET_TX_POWER_CALLING) + 1));
	} else if (strncasecmp(command, CMD_SET_TX_POWER_SAR, strlen(CMD_SET_TX_POWER_SAR)) == 0) {
		ret = slsi_set_tx_power_sar(dev, command + strlen(CMD_SET_TX_POWER_SAR) + 1,
					    priv_cmd.total_len - (strlen(CMD_SET_TX_POWER_SAR) + 1));
	} else if (strncasecmp(command, CMD_GET_TX_POWER_SAR, strlen(CMD_GET_TX_POWER_SAR)) == 0) {
		ret = slsi_get_tx_power_sar(dev, command, priv_cmd.total_len);
	} else if (strncasecmp(command, CMD_GETREGULATORY, strlen(CMD_GETREGULATORY)) == 0) {
		ret = slsi_get_regulatory(dev, command, priv_cmd.total_len);
#ifdef CONFIG_SCSC_WLAN_HANG_TEST
	} else if (strncasecmp(command, CMD_TESTFORCEHANG, strlen(CMD_TESTFORCEHANG)) == 0) {
		ret = slsi_test_send_hanged_vendor_event(dev);
#endif
	} else if (strncasecmp(command, CMD_SET_FCC_CHANNEL, strlen(CMD_SET_FCC_CHANNEL)) == 0) {
		ret = slsi_set_fcc_channel(dev, command + strlen(CMD_SET_FCC_CHANNEL) + 1,
					   priv_cmd.total_len - (strlen(CMD_SET_FCC_CHANNEL) + 1));
	} else if (strncasecmp(command, CMD_FAKEMAC, strlen(CMD_FAKEMAC)) == 0) {
		ret = slsi_fake_mac_write(dev, command + strlen(CMD_FAKEMAC) + 1);
	} else if (strncasecmp(command, CMD_GETBSSRSSI, strlen(CMD_GETBSSRSSI)) == 0) {
		ret = slsi_get_bss_rssi(dev, command, priv_cmd.total_len);
	} else if (strncasecmp(command, CMD_GETBSSINFO, strlen(CMD_GETBSSINFO)) == 0) {
		ret = slsi_get_bss_info(dev, command, priv_cmd.total_len);
	} else if (strncasecmp(command, CMD_GETSTAINFO, strlen(CMD_GETSTAINFO)) == 0) {
		ret = slsi_get_sta_info(dev, command, priv_cmd.total_len);
	} else if (strncasecmp(command, CMD_GETASSOCREJECTINFO, strlen(CMD_GETASSOCREJECTINFO)) == 0) {
		ret = slsi_get_assoc_reject_info(dev, command, priv_cmd.total_len);
	} else if ((strncasecmp(command, CMD_RXFILTERSTART, strlen(CMD_RXFILTERSTART)) == 0) ||
			(strncasecmp(command, CMD_RXFILTERSTOP, strlen(CMD_RXFILTERSTOP)) == 0) ||
			(strncasecmp(command, CMD_BTCOEXMODE, strlen(CMD_BTCOEXMODE)) == 0) ||
			(strncasecmp(command, CMD_BTCOEXSCAN_START, strlen(CMD_BTCOEXSCAN_START)) == 0) ||
			(strncasecmp(command, CMD_BTCOEXSCAN_STOP, strlen(CMD_BTCOEXSCAN_STOP)) == 0) ||
			(strncasecmp(command, CMD_MIRACAST, strlen(CMD_MIRACAST)) == 0)) {
		ret = 0;
	} else if ((strncasecmp(command, CMD_AMPDU_MPDU, strlen(CMD_AMPDU_MPDU)) == 0) ||
			(strncasecmp(command, CMD_CHANGE_RL, strlen(CMD_CHANGE_RL)) == 0) ||
			(strncasecmp(command, CMD_INTERFACE_CREATE, strlen(CMD_INTERFACE_CREATE)) == 0) ||
			(strncasecmp(command, CMD_INTERFACE_DELETE, strlen(CMD_INTERFACE_DELETE)) == 0) ||
			(strncasecmp(command, CMD_LTECOEX, strlen(CMD_LTECOEX)) == 0) ||
			(strncasecmp(command, CMD_RESTORE_RL, strlen(CMD_RESTORE_RL)) == 0) ||
			(strncasecmp(command, CMD_RPSMODE, strlen(CMD_RPSMODE)) == 0) ||
			(strncasecmp(command, CMD_SETCCXMODE, strlen(CMD_SETCCXMODE)) == 0) ||
			(strncasecmp(command, CMD_SETDFSSCANMODE, strlen(CMD_SETDFSSCANMODE)) == 0) ||
			(strncasecmp(command, CMD_SETSINGLEANT, strlen(CMD_SETSINGLEANT)) == 0)) {
		ret  = -ENOTSUPP;
#ifndef SLSI_TEST_DEV
	} else if (strncasecmp(command, CMD_DRIVERDEBUGDUMP, strlen(CMD_DRIVERDEBUGDUMP)) == 0) {
		slsi_dump_stats(dev);
#ifdef CONFIG_SCSC_LOG_COLLECTION
		scsc_log_collector_schedule_collection(SCSC_LOG_HOST_WLAN, SCSC_LOG_HOST_WLAN_REASON_DRIVERDEBUGDUMP);
#else
		ret = mx140_log_dump();
#endif
#endif
	} else {
		ret  = -ENOTSUPP;
	}
	if (strncasecmp(command, CMD_SETROAMBAND, strlen(CMD_SETROAMBAND)) != 0 && strncasecmp(command, CMD_SETBAND, strlen(CMD_SETBAND)) != 0 && copy_to_user(priv_cmd.buf, command, priv_cmd.total_len)) {
		ret = -EFAULT;
		SLSI_NET_ERR(dev, "Buffer copy fail\n");
	}

exit:
	kfree(command);
	return ret;
}
