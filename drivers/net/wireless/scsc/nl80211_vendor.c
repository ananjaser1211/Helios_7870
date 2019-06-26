/*****************************************************************************
 *
 * Copyright (c) 2014 - 2018 Samsung Electronics Co., Ltd. All rights reserved
 *
 ****************************************************************************/
#include <linux/version.h>
#include <net/cfg80211.h>
#include <net/ip.h>
#include <linux/etherdevice.h>
#include "dev.h"
#include "cfg80211_ops.h"
#include "debug.h"
#include "mgt.h"
#include "mlme.h"
#include "netif.h"
#include "unifiio.h"
#include "mib.h"
#include "nl80211_vendor.h"

#ifdef CONFIG_SCSC_WLAN_ENHANCED_LOGGING
#include "scsc_wifilogger.h"
#include "scsc_wifilogger_rings.h"
#include "scsc_wifilogger_types.h"
#endif

#define SLSI_GSCAN_INVALID_RSSI        0x7FFF
#define SLSI_EPNO_AUTH_FIELD_WEP_OPEN  1
#define SLSI_EPNO_AUTH_FIELD_WPA_PSK   2
#define SLSI_EPNO_AUTH_FIELD_WPA_EAP   4
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

#ifdef CONFIG_SCSC_WLAN_ENHANCED_LOGGING
static int mem_dump_buffer_size;
static char *mem_dump_buffer;
#endif
#ifdef CONFIG_SCSC_WLAN_DEBUG
char *slsi_print_event_name(int event_id)
{
	switch (event_id) {
	case SLSI_NL80211_SIGNIFICANT_CHANGE_EVENT:
		return "SIGNIFICANT_CHANGE_EVENT";
	case SLSI_NL80211_HOTLIST_AP_FOUND_EVENT:
		return "HOTLIST_AP_FOUND_EVENT";
	case SLSI_NL80211_SCAN_RESULTS_AVAILABLE_EVENT:
		return "SCAN_RESULTS_AVAILABLE_EVENT";
	case SLSI_NL80211_FULL_SCAN_RESULT_EVENT:
		return "FULL_SCAN_RESULT_EVENT";
	case SLSI_NL80211_SCAN_EVENT:
		return "BUCKET_SCAN_DONE_EVENT";
	case SLSI_NL80211_HOTLIST_AP_LOST_EVENT:
		return "HOTLIST_AP_LOST_EVENT";
#ifdef CONFIG_SCSC_WLAN_KEY_MGMT_OFFLOAD
	case SLSI_NL80211_VENDOR_SUBCMD_KEY_MGMT_ROAM_AUTH:
		return "KEY_MGMT_ROAM_AUTH";
#endif
	case SLSI_NL80211_VENDOR_HANGED_EVENT:
		return "SLSI_NL80211_VENDOR_HANGED_EVENT";
	case SLSI_NL80211_EPNO_EVENT:
		return "SLSI_NL80211_EPNO_EVENT";
	case SLSI_NL80211_HOTSPOT_MATCH:
		return "SLSI_NL80211_HOTSPOT_MATCH";
	case SLSI_NL80211_RSSI_REPORT_EVENT:
		return "SLSI_NL80211_RSSI_REPORT_EVENT";
#ifdef CONFIG_SCSC_WLAN_ENHANCED_LOGGING
	case SLSI_NL80211_LOGGER_RING_EVENT:
		return "SLSI_NL80211_LOGGER_RING_EVENT";
	case SLSI_NL80211_LOGGER_FW_DUMP_EVENT:
		return "SLSI_NL80211_LOGGER_FW_DUMP_EVENT";
#endif
	case SLSI_NL80211_NAN_RESPONSE_EVENT:
		return "SLSI_NL80211_NAN_RESPONSE_EVENT";
	case SLSI_NL80211_NAN_PUBLISH_TERMINATED_EVENT:
		return "SLSI_NL80211_NAN_PUBLISH_TERMINATED_EVENT";
	case SLSI_NL80211_NAN_MATCH_EVENT:
		return "SLSI_NL80211_NAN_MATCH_EVENT";
	case SLSI_NL80211_NAN_MATCH_EXPIRED_EVENT:
		return "SLSI_NL80211_NAN_MATCH_EXPIRED_EVENT";
	case SLSI_NL80211_NAN_SUBSCRIBE_TERMINATED_EVENT:
		return "SLSI_NL80211_NAN_SUBSCRIBE_TERMINATED_EVENT";
	case SLSI_NL80211_NAN_FOLLOWUP_EVENT:
		return "SLSI_NL80211_NAN_FOLLOWUP_EVENT";
	case SLSI_NL80211_NAN_DISCOVERY_ENGINE_EVENT:
		return "SLSI_NL80211_NAN_DISCOVERY_ENGINE_EVENT";
	case SLSI_NL80211_RTT_RESULT_EVENT:
		return "SLSI_NL80211_RTT_RESULT_EVENT";
	case SLSI_NL80211_RTT_COMPLETE_EVENT:
		return "SLSI_NL80211_RTT_COMPLETE_EVENT";
	case SLSI_NL80211_VENDOR_ACS_EVENT:
		return "SLSI_NL80211_VENDOR_ACS_EVENT";
	default:
		return "UNKNOWN_EVENT";
	}
}
#endif

int slsi_vendor_event(struct slsi_dev *sdev, int event_id, const void *data, int len)
{
	struct sk_buff *skb;

#ifdef CONFIG_SCSC_WLAN_DEBUG
	SLSI_DBG1_NODEV(SLSI_MLME, "Event: %s(%d), data = %p, len = %d\n",
			slsi_print_event_name(event_id), event_id, data, len);
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
	skb = cfg80211_vendor_event_alloc(sdev->wiphy, NULL, len, event_id, GFP_KERNEL);
#else
	skb = cfg80211_vendor_event_alloc(sdev->wiphy, len, event_id, GFP_KERNEL);
#endif
	if (!skb) {
		SLSI_ERR_NODEV("Failed to allocate skb for vendor event: %d\n", event_id);
		return -ENOMEM;
	}

	nla_put_nohdr(skb, len, data);

	cfg80211_vendor_event(skb, GFP_KERNEL);

	return 0;
}

static int slsi_vendor_cmd_reply(struct wiphy *wiphy, const void *data, int len)
{
	struct sk_buff *skb;

	skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, len);
	if (!skb) {
		SLSI_ERR_NODEV("Failed to allocate skb\n");
		return -ENOMEM;
	}

	nla_put_nohdr(skb, len, data);

	return cfg80211_vendor_cmd_reply(skb);
}

static struct net_device *slsi_gscan_get_netdev(struct slsi_dev *sdev)
{
	return slsi_get_netdev(sdev, SLSI_NET_INDEX_WLAN);
}

static struct net_device *slsi_nan_get_netdev(struct slsi_dev *sdev)
{
#if CONFIG_SCSC_WLAN_MAX_INTERFACES >= 4
	return slsi_get_netdev(sdev, SLSI_NET_INDEX_NAN);
#else
	return NULL;
#endif
}

static struct netdev_vif *slsi_gscan_get_vif(struct slsi_dev *sdev)
{
	struct net_device *dev;

	dev = slsi_gscan_get_netdev(sdev);
	if (!dev) {
		SLSI_WARN_NODEV("Dev is NULL\n");
		return NULL;
	}

	return netdev_priv(dev);
}

#ifdef CONFIG_SCSC_WLAN_DEBUG
static void slsi_gscan_add_dump_params(struct slsi_nl_gscan_param *nl_gscan_param)
{
	int i;
	int j;

	SLSI_DBG2_NODEV(SLSI_GSCAN, "Parameters for SLSI_NL80211_VENDOR_SUBCMD_ADD_GSCAN sub-command:\n");
	SLSI_DBG2_NODEV(SLSI_GSCAN, "base_period: %d max_ap_per_scan: %d report_threshold_percent: %d report_threshold_num_scans = %d num_buckets: %d\n",
			nl_gscan_param->base_period, nl_gscan_param->max_ap_per_scan,
			nl_gscan_param->report_threshold_percent, nl_gscan_param->report_threshold_num_scans,
			nl_gscan_param->num_buckets);

	for (i = 0; i < nl_gscan_param->num_buckets; i++) {
		SLSI_DBG2_NODEV(SLSI_GSCAN, "Bucket: %d\n", i);
		SLSI_DBG2_NODEV(SLSI_GSCAN, "\tbucket_index: %d band: %d period: %d report_events: %d num_channels: %d\n",
				nl_gscan_param->nl_bucket[i].bucket_index, nl_gscan_param->nl_bucket[i].band,
				nl_gscan_param->nl_bucket[i].period, nl_gscan_param->nl_bucket[i].report_events,
				nl_gscan_param->nl_bucket[i].num_channels);

		for (j = 0; j < nl_gscan_param->nl_bucket[i].num_channels; j++)
			SLSI_DBG2_NODEV(SLSI_GSCAN, "\tchannel_list[%d]: %d\n",
					j, nl_gscan_param->nl_bucket[i].channels[j].channel);
	}
}

static void slsi_gscan_set_hotlist_dump_params(struct slsi_nl_hotlist_param *nl_hotlist_param)
{
	int i;

	SLSI_DBG2_NODEV(SLSI_GSCAN, "Parameters for SUBCMD_SET_BSSID_HOTLIST sub-command:\n");
	SLSI_DBG2_NODEV(SLSI_GSCAN, "lost_ap_sample_size: %d, num_bssid: %d\n",
			nl_hotlist_param->lost_ap_sample_size, nl_hotlist_param->num_bssid);

	for (i = 0; i < nl_hotlist_param->num_bssid; i++) {
		SLSI_DBG2_NODEV(SLSI_GSCAN, "AP[%d]\n", i);
		SLSI_DBG2_NODEV(SLSI_GSCAN, "\tBSSID:%pM rssi_low:%d rssi_high:%d\n",
				nl_hotlist_param->ap[i].bssid, nl_hotlist_param->ap[i].low, nl_hotlist_param->ap[i].high);
	}
}

void slsi_gscan_scan_res_dump(struct slsi_gscan_result *scan_data)
{
	struct slsi_nl_scan_result_param *nl_scan_res = &scan_data->nl_scan_res;

	SLSI_DBG3_NODEV(SLSI_GSCAN, "TS:%llu SSID:%s BSSID:%pM Chan:%d RSSI:%d Bcn_Int:%d Capab:%#x IE_Len:%d\n",
			nl_scan_res->ts, nl_scan_res->ssid, nl_scan_res->bssid, nl_scan_res->channel,
			nl_scan_res->rssi, nl_scan_res->beacon_period, nl_scan_res->capability, nl_scan_res->ie_length);

	SLSI_DBG_HEX_NODEV(SLSI_GSCAN, &nl_scan_res->ie_data[0], nl_scan_res->ie_length, "IE_Data:\n");
	if (scan_data->anqp_length) {
		SLSI_DBG3_NODEV(SLSI_GSCAN, "ANQP_LENGTH:%d\n", scan_data->anqp_length);
		SLSI_DBG_HEX_NODEV(SLSI_GSCAN, nl_scan_res->ie_data + nl_scan_res->ie_length, scan_data->anqp_length, "ANQP_info:\n");
	}
}
#endif

static int slsi_gscan_get_capabilities(struct wiphy *wiphy,
				       struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_nl_gscan_capabilities nl_cap;
	int                               ret = 0;
	struct slsi_dev                   *sdev = SDEV_FROM_WIPHY(wiphy);

	SLSI_DBG1_NODEV(SLSI_GSCAN, "SUBCMD_GET_GSCAN_CAPABILITIES\n");

	memset(&nl_cap, 0, sizeof(struct slsi_nl_gscan_capabilities));

	ret = slsi_mib_get_gscan_cap(sdev, &nl_cap);
	if (ret != 0) {
		SLSI_ERR(sdev, "Failed to read mib\n");
		return ret;
	}

	nl_cap.max_scan_cache_size = SLSI_GSCAN_MAX_SCAN_CACHE_SIZE;
	nl_cap.max_ap_cache_per_scan = SLSI_GSCAN_MAX_AP_CACHE_PER_SCAN;
	nl_cap.max_scan_reporting_threshold = SLSI_GSCAN_MAX_SCAN_REPORTING_THRESHOLD;

	ret = slsi_vendor_cmd_reply(wiphy, &nl_cap, sizeof(struct slsi_nl_gscan_capabilities));
	if (ret)
		SLSI_ERR_NODEV("gscan_get_capabilities vendor cmd reply failed (err = %d)\n", ret);

	return ret;
}

static u32 slsi_gscan_put_channels(struct ieee80211_supported_band *chan_data, bool no_dfs, bool only_dfs, u32 *buf)
{
	u32 chan_count = 0;
	u32 chan_flags;
	int i;

	if (chan_data == NULL) {
		SLSI_DBG3_NODEV(SLSI_GSCAN, "Band not supported\n");
		return 0;
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0)
	chan_flags = (IEEE80211_CHAN_PASSIVE_SCAN | IEEE80211_CHAN_NO_OFDM | IEEE80211_CHAN_RADAR);
#else
	chan_flags = (IEEE80211_CHAN_NO_IR | IEEE80211_CHAN_NO_OFDM | IEEE80211_CHAN_RADAR);
#endif

	for (i = 0; i < chan_data->n_channels; i++) {
		if (chan_data->channels[i].flags & IEEE80211_CHAN_DISABLED)
			continue;
		if (only_dfs) {
			if (chan_data->channels[i].flags & chan_flags)
				buf[chan_count++] = chan_data->channels[i].center_freq;
			continue;
		}
		if (no_dfs && (chan_data->channels[i].flags & chan_flags))
			continue;
		buf[chan_count++] = chan_data->channels[i].center_freq;
	}
	return chan_count;
}

static int slsi_gscan_get_valid_channel(struct wiphy *wiphy,
					struct wireless_dev *wdev, const void *data, int len)
{
	int             ret = 0, type, band;
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	u32             *chan_list;
	u32             chan_count = 0, mem_len = 0;
	struct sk_buff  *reply;

	type = nla_type(data);

	if (type == GSCAN_ATTRIBUTE_BAND)
		band = nla_get_u32(data);
	else
		return -EINVAL;

	if (band == 0) {
		SLSI_WARN(sdev, "NO Bands. return 0 channel\n");
		return ret;
	}

	SLSI_MUTEX_LOCK(sdev->netdev_add_remove_mutex);
	if (wiphy->bands[IEEE80211_BAND_2GHZ]) {
		mem_len += wiphy->bands[IEEE80211_BAND_2GHZ]->n_channels * sizeof(u32);
	}
	if (wiphy->bands[IEEE80211_BAND_5GHZ]) {
		mem_len += wiphy->bands[IEEE80211_BAND_5GHZ]->n_channels * sizeof(u32);
	}
	if (mem_len == 0) {
		ret = -ENOTSUPP;
		goto exit;
	}

	chan_list = kmalloc(mem_len, GFP_KERNEL);
	if (chan_list == NULL) {
		ret = -ENOMEM;
		goto exit;
	}
	mem_len += SLSI_NL_VENDOR_REPLY_OVERHEAD + (SLSI_NL_ATTRIBUTE_U32_LEN * 2);
	reply = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, mem_len);
	if (reply == NULL) {
		ret = -ENOMEM;
		goto exit_with_chan_list;
	}
	switch (band) {
	case WIFI_BAND_BG:
		chan_count = slsi_gscan_put_channels(wiphy->bands[IEEE80211_BAND_2GHZ], false, false, chan_list);
		break;
	case WIFI_BAND_A:
		chan_count = slsi_gscan_put_channels(wiphy->bands[IEEE80211_BAND_5GHZ], true, false, chan_list);
		break;
	case WIFI_BAND_A_DFS:
		chan_count = slsi_gscan_put_channels(wiphy->bands[IEEE80211_BAND_5GHZ], false, true, chan_list);
		break;
	case WIFI_BAND_A_WITH_DFS:
		chan_count = slsi_gscan_put_channels(wiphy->bands[IEEE80211_BAND_5GHZ], false, false, chan_list);
		break;
	case WIFI_BAND_ABG:
		chan_count = slsi_gscan_put_channels(wiphy->bands[IEEE80211_BAND_2GHZ], true, false, chan_list);
		chan_count += slsi_gscan_put_channels(wiphy->bands[IEEE80211_BAND_5GHZ], true, false, chan_list + chan_count);
		break;
	case WIFI_BAND_ABG_WITH_DFS:
		chan_count = slsi_gscan_put_channels(wiphy->bands[IEEE80211_BAND_2GHZ], false, false, chan_list);
		chan_count += slsi_gscan_put_channels(wiphy->bands[IEEE80211_BAND_5GHZ], false, false, chan_list + chan_count);
		break;
	default:
		chan_count = 0;
		SLSI_WARN(sdev, "Invalid Band %d\n", band);
	}
	nla_put_u32(reply, GSCAN_ATTRIBUTE_NUM_CHANNELS, chan_count);
	nla_put(reply, GSCAN_ATTRIBUTE_CHANNEL_LIST, chan_count * sizeof(u32), chan_list);

	ret =  cfg80211_vendor_cmd_reply(reply);

	if (ret)
		SLSI_ERR(sdev, "FAILED to reply GET_VALID_CHANNELS\n");

exit_with_chan_list:
	kfree(chan_list);
exit:
	SLSI_MUTEX_UNLOCK(sdev->netdev_add_remove_mutex);
	return ret;
}

struct slsi_gscan_result *slsi_prepare_scan_result(struct sk_buff *skb, u16 anqp_length, int hs2_id)
{
	struct ieee80211_mgmt    *mgmt = fapi_get_mgmt(skb);
	struct slsi_gscan_result *scan_res;
	struct timespec          ts;
	const u8                 *ssid_ie;
	int                      mem_reqd;
	int                      ie_len;
	u8                       *ie;

	ie = &mgmt->u.beacon.variable[0];
	ie_len = fapi_get_datalen(skb) - (ie - (u8 *)mgmt) - anqp_length;

	/* Exclude 1 byte for ie_data[1]. sizeof(u16) to include anqp_length, sizeof(int) for hs_id */
	mem_reqd = (sizeof(struct slsi_gscan_result) - 1) + ie_len + anqp_length + sizeof(int) + sizeof(u16);

	/* Allocate memory for scan result */
	scan_res = kmalloc(mem_reqd, GFP_KERNEL);
	if (scan_res == NULL) {
		SLSI_ERR_NODEV("Failed to allocate memory for scan result\n");
		return NULL;
	}

	/* Exclude 1 byte for ie_data[1] */
	scan_res->scan_res_len = (sizeof(struct slsi_nl_scan_result_param) - 1) + ie_len;
	scan_res->anqp_length = 0;

	get_monotonic_boottime(&ts);
	scan_res->nl_scan_res.ts = (u64)TIMESPEC_TO_US(ts);

	ssid_ie = cfg80211_find_ie(WLAN_EID_SSID, &mgmt->u.beacon.variable[0], ie_len);
	if ((ssid_ie != NULL) && (ssid_ie[1] > 0) && (ssid_ie[1] < IEEE80211_MAX_SSID_LEN)) {
		memcpy(scan_res->nl_scan_res.ssid, &ssid_ie[2], ssid_ie[1]);
		scan_res->nl_scan_res.ssid[ssid_ie[1]] = '\0';
	} else {
		scan_res->nl_scan_res.ssid[0] = '\0';
	}

	SLSI_ETHER_COPY(scan_res->nl_scan_res.bssid, mgmt->bssid);
	scan_res->nl_scan_res.channel = fapi_get_u16(skb, u.mlme_scan_ind.channel_frequency) / 2;
	scan_res->nl_scan_res.rssi = fapi_get_s16(skb, u.mlme_scan_ind.rssi);
	scan_res->nl_scan_res.rtt = SLSI_GSCAN_RTT_UNSPECIFIED;
	scan_res->nl_scan_res.rtt_sd = SLSI_GSCAN_RTT_UNSPECIFIED;
	scan_res->nl_scan_res.beacon_period = mgmt->u.beacon.beacon_int;
	scan_res->nl_scan_res.capability = mgmt->u.beacon.capab_info;
	scan_res->nl_scan_res.ie_length = ie_len;
	memcpy(scan_res->nl_scan_res.ie_data, ie, ie_len);
	memcpy(scan_res->nl_scan_res.ie_data + ie_len, &hs2_id, sizeof(int));
	memcpy(scan_res->nl_scan_res.ie_data + ie_len + sizeof(int), &anqp_length, sizeof(u16));
	if (anqp_length) {
		memcpy(scan_res->nl_scan_res.ie_data + ie_len + sizeof(u16) + sizeof(int), ie + ie_len, anqp_length);
		scan_res->anqp_length = anqp_length + sizeof(u16) + sizeof(int);
	}

#ifdef CONFIG_SCSC_WLAN_DEBUG
	slsi_gscan_scan_res_dump(scan_res);
#endif

	return scan_res;
}

void slsi_hotlist_ap_lost_indication(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb)
{
	struct slsi_nl_scan_result_param *nl_scan_res;
	struct slsi_hotlist_result       *hotlist, *temp;
	struct netdev_vif                *ndev_vif = netdev_priv(dev);
	u8                               *mac_addr;
	u16                              num_entries;
	int                              mem_reqd;
	bool                             found = false;
	int                              offset = 0;
	int                              i;

	if (!ndev_vif) {
		SLSI_WARN_NODEV("ndev_vif is NULL\n");
		slsi_kfree_skb(skb);
		return;
	}

	SLSI_MUTEX_LOCK(ndev_vif->scan_mutex);

	num_entries = fapi_get_s16(skb, u.mlme_ap_loss_ind.entries);
	mac_addr = fapi_get_data(skb);

	SLSI_NET_DBG1(dev, SLSI_GSCAN, "Hotlist AP Lost Indication: num_entries %d\n", num_entries);

	mem_reqd = num_entries * sizeof(struct slsi_nl_scan_result_param);
	nl_scan_res = kmalloc(mem_reqd, GFP_KERNEL);
	if (nl_scan_res == NULL) {
		SLSI_NET_ERR(dev, "Failed to allocate memory for hotlist lost\n");
		goto out;
	}

	for (i = 0; i < num_entries; i++) {
		SLSI_NET_DBG3(dev, SLSI_GSCAN,
			      "Remove the GSCAN results for the lost AP: %pM\n", &mac_addr[ETH_ALEN * i]);
		slsi_gscan_hash_remove(sdev, &mac_addr[ETH_ALEN * i]);

		list_for_each_entry_safe(hotlist, temp, &sdev->hotlist_results, list) {
			if (memcmp(hotlist->nl_scan_res.bssid, &mac_addr[ETH_ALEN * i], ETH_ALEN) == 0) {
				SLSI_NET_DBG2(dev, SLSI_GSCAN, "Lost AP [%d]: %pM\n", i, &mac_addr[ETH_ALEN * i]);
				list_del(&hotlist->list);

				hotlist->nl_scan_res.ie_length = 0; /* Not sending the IE for hotlist lost event */
				memcpy(&nl_scan_res[offset], &hotlist->nl_scan_res, sizeof(struct slsi_nl_scan_result_param));
				offset++;

				kfree(hotlist);
				found = true;
				break;
			}
		}

		if (!found)
			SLSI_NET_ERR(dev, "Hostlist record is not available in scan result\n");

		found = false;
	}

	slsi_vendor_event(sdev, SLSI_NL80211_HOTLIST_AP_LOST_EVENT,
			  nl_scan_res, (offset * sizeof(struct slsi_nl_scan_result_param)));

	kfree(nl_scan_res);
out:
	slsi_kfree_skb(skb);
	SLSI_MUTEX_UNLOCK(ndev_vif->scan_mutex);
}

void slsi_hotlist_ap_found_indication(struct slsi_dev *sdev, struct net_device *dev, struct slsi_gscan_result *scan_res)
{
	struct slsi_hotlist_result *hotlist, *temp;
	int                        num_hotlist_results = 0;
	struct netdev_vif          *ndev_vif = netdev_priv(dev);
	int                        mem_reqd;
	u8                         *event_buffer;
	int                        offset;

	if (!ndev_vif) {
		SLSI_WARN_NODEV("ndev_vif is NULL\n");
		return;
	}

	if (!SLSI_MUTEX_IS_LOCKED(ndev_vif->scan_mutex))
		SLSI_WARN_NODEV("ndev_vif->scan_mutex is not locked\n");

	SLSI_NET_DBG1(dev, SLSI_GSCAN, "Hotlist AP Found Indication: %pM\n", scan_res->nl_scan_res.bssid);

	/* Check if the hotlist result is already available */
	list_for_each_entry_safe(hotlist, temp, &sdev->hotlist_results, list) {
		if (memcmp(hotlist->nl_scan_res.bssid, scan_res->nl_scan_res.bssid, ETH_ALEN) == 0) {
			SLSI_DBG3(sdev, SLSI_GSCAN, "Hotlist result already available for: %pM\n", scan_res->nl_scan_res.bssid);
			/* Delete the old result - store the new result */
			list_del(&hotlist->list);
			kfree(hotlist);
			break;
		}
	}

	/* Allocating memory for storing the hostlist result */
	mem_reqd = scan_res->scan_res_len + (sizeof(struct slsi_hotlist_result) - sizeof(struct slsi_nl_scan_result_param));
	SLSI_DBG3(sdev, SLSI_GSCAN, "hotlist result alloc size: %d\n", mem_reqd);
	hotlist = kmalloc(mem_reqd, GFP_KERNEL);
	if (hotlist == NULL) {
		SLSI_ERR(sdev, "Failed to allocate memory for hotlist\n");
		return;
	}

	hotlist->scan_res_len = scan_res->scan_res_len;
	memcpy(&hotlist->nl_scan_res, &scan_res->nl_scan_res, scan_res->scan_res_len);

	INIT_LIST_HEAD(&hotlist->list);
	list_add(&hotlist->list, &sdev->hotlist_results);

	/* Calculate the number of hostlist results and mem required */
	mem_reqd = 0;
	offset = 0;
	list_for_each_entry_safe(hotlist, temp, &sdev->hotlist_results, list) {
		mem_reqd += sizeof(struct slsi_nl_scan_result_param); /* If IE required use: hotlist->scan_res_len */
		num_hotlist_results++;
	}
	SLSI_DBG3(sdev, SLSI_GSCAN, "num_hotlist_results = %d, mem_reqd = %d\n", num_hotlist_results, mem_reqd);

	/* Allocate event buffer */
	event_buffer = kmalloc(mem_reqd, GFP_KERNEL);
	if (event_buffer == NULL) {
		SLSI_ERR_NODEV("Failed to allocate memory for event_buffer\n");
		return;
	}

	/* Prepare the event buffer */
	list_for_each_entry_safe(hotlist, temp, &sdev->hotlist_results, list) {
		memcpy(&event_buffer[offset], &hotlist->nl_scan_res, sizeof(struct slsi_nl_scan_result_param));
		offset += sizeof(struct slsi_nl_scan_result_param); /* If IE required use: hotlist->scan_res_len */
	}

	slsi_vendor_event(sdev, SLSI_NL80211_HOTLIST_AP_FOUND_EVENT, event_buffer, offset);

	kfree(event_buffer);
}

static void slsi_gscan_hash_add(struct slsi_dev *sdev, struct slsi_gscan_result *scan_res)
{
	u8                key = SLSI_GSCAN_GET_HASH_KEY(scan_res->nl_scan_res.bssid[5]);
	struct netdev_vif *ndev_vif;

	ndev_vif = slsi_gscan_get_vif(sdev);
	if (!SLSI_MUTEX_IS_LOCKED(ndev_vif->scan_mutex))
		SLSI_WARN_NODEV("ndev_vif->scan_mutex is not locked\n");

	scan_res->hnext = sdev->gscan_hash_table[key];
	sdev->gscan_hash_table[key] = scan_res;

	/* Update the total buffer consumed and number of scan results */
	sdev->buffer_consumed += scan_res->scan_res_len;
	sdev->num_gscan_results++;
}

static struct slsi_gscan_result *slsi_gscan_hash_get(struct slsi_dev *sdev, u8 *mac)
{
	struct slsi_gscan_result *temp;
	struct netdev_vif        *ndev_vif;
	u8                       key = SLSI_GSCAN_GET_HASH_KEY(mac[5]);

	ndev_vif = slsi_gscan_get_vif(sdev);

	if (!SLSI_MUTEX_IS_LOCKED(ndev_vif->scan_mutex))
		SLSI_WARN_NODEV("ndev_vif->scan_mutex is not locked\n");

	temp = sdev->gscan_hash_table[key];
	while (temp != NULL) {
		if (memcmp(temp->nl_scan_res.bssid, mac, ETH_ALEN) == 0)
			return temp;
		temp = temp->hnext;
	}

	return NULL;
}

void slsi_gscan_hash_remove(struct slsi_dev *sdev, u8 *mac)
{
	u8                       key = SLSI_GSCAN_GET_HASH_KEY(mac[5]);
	struct slsi_gscan_result *curr;
	struct slsi_gscan_result *prev;
	struct netdev_vif        *ndev_vif;
	struct slsi_gscan_result *scan_res = NULL;

	ndev_vif = slsi_gscan_get_vif(sdev);
	if (!SLSI_MUTEX_IS_LOCKED(ndev_vif->scan_mutex))
		SLSI_WARN_NODEV("ndev_vif->scan_mutex is not locked\n");

	if (sdev->gscan_hash_table[key] == NULL)
		return;

	if (memcmp(sdev->gscan_hash_table[key]->nl_scan_res.bssid, mac, ETH_ALEN) == 0) {
		scan_res = sdev->gscan_hash_table[key];
		sdev->gscan_hash_table[key] = sdev->gscan_hash_table[key]->hnext;
	} else {
		prev = sdev->gscan_hash_table[key];
		curr = prev->hnext;

		while (curr != NULL) {
			if (memcmp(curr->nl_scan_res.bssid, mac, ETH_ALEN) == 0) {
				scan_res = curr;
				prev->hnext = curr->hnext;
				break;
			}
			prev = curr;
			curr = curr->hnext;
		}
	}

	if (scan_res) {
		/* Update the total buffer consumed and number of scan results */
		sdev->buffer_consumed -= scan_res->scan_res_len;
		sdev->num_gscan_results--;
		kfree(scan_res);
	}

	if (sdev->num_gscan_results < 0)
		SLSI_ERR(sdev, "Wrong num_gscan_results: %d\n", sdev->num_gscan_results);
}

int slsi_check_scan_result(struct slsi_dev *sdev, struct slsi_bucket *bucket, struct slsi_gscan_result *new_scan_res)
{
	struct slsi_gscan_result *scan_res;

	/* Check if the scan result for the same BSS already exists in driver buffer */
	scan_res = slsi_gscan_hash_get(sdev, new_scan_res->nl_scan_res.bssid);
	if (scan_res == NULL) { /* New scan result */
		if ((sdev->buffer_consumed + new_scan_res->scan_res_len) >= SLSI_GSCAN_MAX_SCAN_CACHE_SIZE) {
			SLSI_DBG2(sdev, SLSI_GSCAN,
				  "Scan buffer full, discarding scan result, buffer_consumed = %d, buffer_threshold = %d\n",
				  sdev->buffer_consumed, sdev->buffer_threshold);

			/* Scan buffer is full can't store anymore new results */
			return SLSI_DISCARD_SCAN_RESULT;
		}

		return SLSI_KEEP_SCAN_RESULT;
	}

	/* Even if scan buffer is full existing results can be replaced with the latest one */
	if (scan_res->scan_cycle == bucket->scan_cycle)
		/* For the same scan cycle the result will be replaced only if the RSSI is better */
		if (new_scan_res->nl_scan_res.rssi < scan_res->nl_scan_res.rssi)
			return SLSI_DISCARD_SCAN_RESULT;

	/* Remove the existing scan result */
	slsi_gscan_hash_remove(sdev, scan_res->nl_scan_res.bssid);

	return SLSI_KEEP_SCAN_RESULT;
}

void slsi_gscan_handle_scan_result(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb, u16 scan_id, bool scan_done)
{
	struct slsi_gscan_result *scan_res = NULL;
	struct netdev_vif        *ndev_vif = netdev_priv(dev);
	struct slsi_bucket       *bucket;
	u16                      bucket_index;
	int                      event_type = WIFI_SCAN_FAILED;
	u16                      anqp_length;
	int                      hs2_network_id;

	if (!SLSI_MUTEX_IS_LOCKED(ndev_vif->scan_mutex))
		SLSI_WARN_NODEV("ndev_vif->scan_mutex is not locked\n");

	SLSI_NET_DBG_HEX(dev, SLSI_GSCAN, skb->data, skb->len, "mlme_scan_ind skb->len: %d\n", skb->len);

	bucket_index = scan_id - SLSI_GSCAN_SCAN_ID_START;
	if (bucket_index >= SLSI_GSCAN_MAX_BUCKETS) {
		SLSI_NET_ERR(dev, "Invalid bucket index: %d (scan_id = %#x)\n", bucket_index, scan_id);
		goto out;
	}

	bucket = &sdev->bucket[bucket_index];
	if (!bucket->used) {
		SLSI_NET_DBG1(dev, SLSI_GSCAN, "Bucket is not active, index: %d (scan_id = %#x)\n", bucket_index, scan_id);
		goto out;
	}

	/* For scan_done indication - no need to store the results */
	if (scan_done) {
		bucket->scan_cycle++;
		bucket->gscan->num_scans++;

		SLSI_NET_DBG3(dev, SLSI_GSCAN, "scan done, scan_cycle = %d, num_scans = %d\n",
			      bucket->scan_cycle, bucket->gscan->num_scans);

		if (bucket->report_events & SLSI_REPORT_EVENTS_EACH_SCAN)
			event_type = WIFI_SCAN_RESULTS_AVAILABLE;
		if (bucket->gscan->num_scans % bucket->gscan->report_threshold_num_scans == 0)
			event_type = WIFI_SCAN_THRESHOLD_NUM_SCANS;
		if (sdev->buffer_consumed >= sdev->buffer_threshold)
			event_type = WIFI_SCAN_THRESHOLD_PERCENT;

		if (event_type != WIFI_SCAN_FAILED)
			slsi_vendor_event(sdev, SLSI_NL80211_SCAN_EVENT, &event_type, sizeof(event_type));

		goto out;
	}

	anqp_length = fapi_get_u16(skb, u.mlme_scan_ind.anqp_elements_length);
	/* TODO new FAPI 3.c has mlme_scan_ind.network_block_id, use that when fapi is updated. */
	hs2_network_id = 1;

	scan_res = slsi_prepare_scan_result(skb, anqp_length, hs2_network_id);
	if (scan_res == NULL) {
		SLSI_NET_ERR(dev, "Failed to prepare scan result\n");
		goto out;
	}

	/* Check for hotlist AP or ePNO networks */
	if (fapi_get_u16(skb, u.mlme_scan_ind.hotlisted_ap)) {
		slsi_hotlist_ap_found_indication(sdev, dev, scan_res);
	} else if (fapi_get_u16(skb, u.mlme_scan_ind.preferrednetwork_ap)) {
		if (anqp_length == 0)
			slsi_vendor_event(sdev, SLSI_NL80211_EPNO_EVENT,
					  &scan_res->nl_scan_res, scan_res->scan_res_len);
		else
			slsi_vendor_event(sdev, SLSI_NL80211_HOTSPOT_MATCH,
					  &scan_res->nl_scan_res, scan_res->scan_res_len + scan_res->anqp_length);
	}

	if (bucket->report_events & SLSI_REPORT_EVENTS_FULL_RESULTS) {
		struct sk_buff *nlevent;

		SLSI_NET_DBG3(dev, SLSI_GSCAN, "report_events: SLSI_REPORT_EVENTS_FULL_RESULTS\n");
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
	nlevent = cfg80211_vendor_event_alloc(sdev->wiphy, NULL, scan_res->scan_res_len + 4, SLSI_NL80211_FULL_SCAN_RESULT_EVENT, GFP_KERNEL);
#else
	nlevent = cfg80211_vendor_event_alloc(sdev->wiphy, scan_res->scan_res_len + 4, SLSI_NL80211_FULL_SCAN_RESULT_EVENT, GFP_KERNEL);
#endif
		if (!nlevent) {
			SLSI_ERR(sdev, "failed to allocate sbk of size:%d\n", scan_res->scan_res_len + 4);
			kfree(scan_res);
			goto out;
		}
		if (nla_put_u32(nlevent, GSCAN_ATTRIBUTE_SCAN_BUCKET_BIT, (1 << bucket_index)) ||
		    nla_put(nlevent, GSCAN_ATTRIBUTE_SCAN_RESULTS, scan_res->scan_res_len, &scan_res->nl_scan_res)) {
			SLSI_ERR(sdev, "failed to put data\n");
			kfree_skb(nlevent);
			kfree(scan_res);
			goto out;
		}
		cfg80211_vendor_event(nlevent, GFP_KERNEL);
	}

	if (slsi_check_scan_result(sdev, bucket, scan_res) == SLSI_DISCARD_SCAN_RESULT) {
		kfree(scan_res);
		goto out;
	 }
	slsi_gscan_hash_add(sdev, scan_res);

out:
	slsi_kfree_skb(skb);
}

u8 slsi_gscan_get_scan_policy(enum wifi_band band)
{
	u8 scan_policy;

	switch (band) {
	case WIFI_BAND_UNSPECIFIED:
		scan_policy = FAPI_SCANPOLICY_ANY_RA;
		break;
	case WIFI_BAND_BG:
		scan_policy = FAPI_SCANPOLICY_2_4GHZ;
		break;
	case WIFI_BAND_A:
		scan_policy = (FAPI_SCANPOLICY_5GHZ |
			       FAPI_SCANPOLICY_NON_DFS);
		break;
	case WIFI_BAND_A_DFS:
		scan_policy = (FAPI_SCANPOLICY_5GHZ |
			       FAPI_SCANPOLICY_DFS);
		break;
	case WIFI_BAND_A_WITH_DFS:
		scan_policy = (FAPI_SCANPOLICY_5GHZ |
			       FAPI_SCANPOLICY_NON_DFS |
			       FAPI_SCANPOLICY_DFS);
		break;
	case WIFI_BAND_ABG:
		scan_policy = (FAPI_SCANPOLICY_5GHZ |
			       FAPI_SCANPOLICY_NON_DFS |
			       FAPI_SCANPOLICY_2_4GHZ);
		break;
	case WIFI_BAND_ABG_WITH_DFS:
		scan_policy = (FAPI_SCANPOLICY_5GHZ |
			       FAPI_SCANPOLICY_NON_DFS |
			       FAPI_SCANPOLICY_DFS |
			       FAPI_SCANPOLICY_2_4GHZ);
		break;
	default:
		scan_policy = FAPI_SCANPOLICY_ANY_RA;
		break;
	}

	SLSI_DBG2_NODEV(SLSI_GSCAN, "Scan Policy: %#x\n", scan_policy);

	return scan_policy;
}

static int slsi_gscan_add_read_params(struct slsi_nl_gscan_param *nl_gscan_param, const void *data, int len)
{
	int                         j = 0;
	int                         type, tmp, tmp1, tmp2, k = 0;
	const struct nlattr         *iter, *iter1, *iter2;
	struct slsi_nl_bucket_param *nl_bucket;

	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);

		if (j >= SLSI_GSCAN_MAX_BUCKETS)
			break;

		switch (type) {
		case GSCAN_ATTRIBUTE_BASE_PERIOD:
			nl_gscan_param->base_period = nla_get_u32(iter);
			break;
		case GSCAN_ATTRIBUTE_NUM_AP_PER_SCAN:
			nl_gscan_param->max_ap_per_scan = nla_get_u32(iter);
			break;
		case GSCAN_ATTRIBUTE_REPORT_THRESHOLD:
			nl_gscan_param->report_threshold_percent = nla_get_u32(iter);
			break;
		case GSCAN_ATTRIBUTE_REPORT_THRESHOLD_NUM_SCANS:
			nl_gscan_param->report_threshold_num_scans = nla_get_u32(iter);
			break;
		case GSCAN_ATTRIBUTE_NUM_BUCKETS:
			nl_gscan_param->num_buckets = nla_get_u32(iter);
			break;
		case GSCAN_ATTRIBUTE_CH_BUCKET_1:
		case GSCAN_ATTRIBUTE_CH_BUCKET_2:
		case GSCAN_ATTRIBUTE_CH_BUCKET_3:
		case GSCAN_ATTRIBUTE_CH_BUCKET_4:
		case GSCAN_ATTRIBUTE_CH_BUCKET_5:
		case GSCAN_ATTRIBUTE_CH_BUCKET_6:
		case GSCAN_ATTRIBUTE_CH_BUCKET_7:
		case GSCAN_ATTRIBUTE_CH_BUCKET_8:
			nla_for_each_nested(iter1, iter, tmp1) {
				type = nla_type(iter1);
				nl_bucket = nl_gscan_param->nl_bucket;

				switch (type) {
				case GSCAN_ATTRIBUTE_BUCKET_ID:
					nl_bucket[j].bucket_index = nla_get_u32(iter1);
					break;
				case GSCAN_ATTRIBUTE_BUCKET_PERIOD:
					nl_bucket[j].period = nla_get_u32(iter1);
					break;
				case GSCAN_ATTRIBUTE_BUCKET_NUM_CHANNELS:
					nl_bucket[j].num_channels = nla_get_u32(iter1);
					break;
				case GSCAN_ATTRIBUTE_BUCKET_CHANNELS:
					nla_for_each_nested(iter2, iter1, tmp2) {
						nl_bucket[j].channels[k].channel = nla_get_u32(iter2);
						k++;
					}
					k = 0;
					break;
				case GSCAN_ATTRIBUTE_BUCKETS_BAND:
					nl_bucket[j].band = nla_get_u32(iter1);
					break;
				case GSCAN_ATTRIBUTE_REPORT_EVENTS:
					nl_bucket[j].report_events = nla_get_u32(iter1);
					break;
				case GSCAN_ATTRIBUTE_BUCKET_EXPONENT:
					nl_bucket[j].exponent = nla_get_u32(iter1);
					break;
				case GSCAN_ATTRIBUTE_BUCKET_STEP_COUNT:
					nl_bucket[j].step_count = nla_get_u32(iter1);
					break;
				case GSCAN_ATTRIBUTE_BUCKET_MAX_PERIOD:
					nl_bucket[j].max_period = nla_get_u32(iter1);
					break;
				default:
					SLSI_ERR_NODEV("No ATTR_BUKTS_type - %x\n", type);
					break;
				}
			}
			j++;
			break;
		default:
			SLSI_ERR_NODEV("No GSCAN_ATTR_CH_BUKT_type - %x\n", type);
			break;
		}
	}

	return 0;
}

int slsi_gscan_add_verify_params(struct slsi_nl_gscan_param *nl_gscan_param)
{
	int i;

	if ((nl_gscan_param->max_ap_per_scan < 0) || (nl_gscan_param->max_ap_per_scan > SLSI_GSCAN_MAX_AP_CACHE_PER_SCAN)) {
		SLSI_ERR_NODEV("Invalid max_ap_per_scan: %d\n", nl_gscan_param->max_ap_per_scan);
		return -EINVAL;
	}

	if ((nl_gscan_param->report_threshold_percent < 0) || (nl_gscan_param->report_threshold_percent > SLSI_GSCAN_MAX_SCAN_REPORTING_THRESHOLD)) {
		SLSI_ERR_NODEV("Invalid report_threshold_percent: %d\n", nl_gscan_param->report_threshold_percent);
		return -EINVAL;
	}

	if ((nl_gscan_param->num_buckets <= 0) || (nl_gscan_param->num_buckets > SLSI_GSCAN_MAX_BUCKETS)) {
		SLSI_ERR_NODEV("Invalid num_buckets: %d\n", nl_gscan_param->num_buckets);
		return -EINVAL;
	}

	for (i = 0; i < nl_gscan_param->num_buckets; i++) {
		if ((nl_gscan_param->nl_bucket[i].band == WIFI_BAND_UNSPECIFIED) && (nl_gscan_param->nl_bucket[i].num_channels == 0)) {
			SLSI_ERR_NODEV("No band/channels provided for gscan: band = %d, num_channel = %d\n",
				       nl_gscan_param->nl_bucket[i].band, nl_gscan_param->nl_bucket[i].num_channels);
			return -EINVAL;
		}

		if (nl_gscan_param->nl_bucket[i].report_events > 4) {
			SLSI_ERR_NODEV("Unsupported report event: report_event = %d\n", nl_gscan_param->nl_bucket[i].report_events);
			return -EINVAL;
		}
	}

	return 0;
}

void slsi_gscan_add_to_list(struct slsi_gscan **sdev_gscan, struct slsi_gscan *gscan)
{
	gscan->next = *sdev_gscan;
	*sdev_gscan = gscan;
}

int slsi_gscan_alloc_buckets(struct slsi_dev *sdev, struct slsi_gscan *gscan, int num_buckets)
{
	int i;
	int bucket_index = 0;
	int free_buckets = 0;

	for (i = 0; i < SLSI_GSCAN_MAX_BUCKETS; i++)
		if (!sdev->bucket[i].used)
			free_buckets++;

	if (num_buckets > free_buckets) {
		SLSI_ERR_NODEV("Not enough free buckets, num_buckets = %d, free_buckets = %d\n",
			       num_buckets, free_buckets);
		return -EINVAL;
	}

	/* Allocate free buckets for the current gscan */
	for (i = 0; i < SLSI_GSCAN_MAX_BUCKETS; i++)
		if (!sdev->bucket[i].used) {
			sdev->bucket[i].used = true;
			sdev->bucket[i].gscan = gscan;
			gscan->bucket[bucket_index] = &sdev->bucket[i];
			bucket_index++;
			if (bucket_index == num_buckets)
				break;
		}

	return 0;
}

static void slsi_gscan_free_buckets(struct slsi_gscan *gscan)
{
	struct slsi_bucket *bucket;
	int                i;

	SLSI_DBG1_NODEV(SLSI_GSCAN, "gscan = %p, num_buckets = %d\n", gscan, gscan->num_buckets);

	for (i = 0; i < gscan->num_buckets; i++) {
		bucket = gscan->bucket[i];

		SLSI_DBG2_NODEV(SLSI_GSCAN, "bucket = %p, used = %d, report_events = %d, scan_id = %#x, gscan = %p\n",
				bucket, bucket->used, bucket->report_events, bucket->scan_id, bucket->gscan);
		if (bucket->used) {
			bucket->used = false;
			bucket->report_events = 0;
			bucket->gscan = NULL;
		}
	}
}

void slsi_gscan_flush_scan_results(struct slsi_dev *sdev)
{
	struct netdev_vif        *ndev_vif;
	struct slsi_gscan_result *temp;
	int                      i;

	ndev_vif = slsi_gscan_get_vif(sdev);
	if (!ndev_vif) {
		SLSI_WARN_NODEV("ndev_vif is NULL");
		return;
	}

	SLSI_MUTEX_LOCK(ndev_vif->scan_mutex);
	for (i = 0; i < SLSI_GSCAN_HASH_TABLE_SIZE; i++)
		while (sdev->gscan_hash_table[i]) {
			temp = sdev->gscan_hash_table[i];
			sdev->gscan_hash_table[i] = sdev->gscan_hash_table[i]->hnext;
			sdev->num_gscan_results--;
			sdev->buffer_consumed -= temp->scan_res_len;
			kfree(temp);
		}

	SLSI_DBG2(sdev, SLSI_GSCAN, "num_gscan_results: %d, buffer_consumed = %d\n",
		  sdev->num_gscan_results, sdev->buffer_consumed);

	if (sdev->num_gscan_results != 0)
		SLSI_WARN_NODEV("sdev->num_gscan_results is not zero\n");

	if (sdev->buffer_consumed != 0)
		SLSI_WARN_NODEV("sdev->buffer_consumedis not zero\n");

	SLSI_MUTEX_UNLOCK(ndev_vif->scan_mutex);
}

void slsi_gscan_flush_hotlist_results(struct slsi_dev *sdev)
{
	struct slsi_hotlist_result *hotlist, *temp;
	struct netdev_vif          *ndev_vif;

	ndev_vif = slsi_gscan_get_vif(sdev);
	if (!ndev_vif) {
		SLSI_WARN_NODEV("ndev_vif is NULL\n");
		return;
	}

	SLSI_MUTEX_LOCK(ndev_vif->scan_mutex);

	list_for_each_entry_safe(hotlist, temp, &sdev->hotlist_results, list) {
		list_del(&hotlist->list);
		kfree(hotlist);
	}

	INIT_LIST_HEAD(&sdev->hotlist_results);

	SLSI_MUTEX_UNLOCK(ndev_vif->scan_mutex);
}

static int slsi_gscan_add_mlme(struct slsi_dev *sdev, struct slsi_nl_gscan_param *nl_gscan_param, struct slsi_gscan *gscan)
{
	struct slsi_gscan_param      gscan_param;
	struct net_device            *dev;
	int                          ret = 0;
	int                          i;

	dev = slsi_gscan_get_netdev(sdev);

	if (!dev) {
		SLSI_WARN_NODEV("dev is NULL\n");
		return -EINVAL;
	}

	for (i = 0; i < nl_gscan_param->num_buckets; i++) {
		u16 report_mode = 0;

		gscan_param.nl_bucket = &nl_gscan_param->nl_bucket[i]; /* current bucket */
		gscan_param.bucket = gscan->bucket[i];

		if (gscan_param.bucket->report_events) {
			if (gscan_param.bucket->report_events & SLSI_REPORT_EVENTS_EACH_SCAN)
				report_mode |= FAPI_REPORTMODE_END_OF_SCAN_CYCLE;
			if (gscan_param.bucket->report_events & SLSI_REPORT_EVENTS_FULL_RESULTS)
				report_mode |= FAPI_REPORTMODE_REAL_TIME;
			if (gscan_param.bucket->report_events & SLSI_REPORT_EVENTS_NO_BATCH)
				report_mode |= FAPI_REPORTMODE_NO_BATCH;
		} else {
			report_mode = FAPI_REPORTMODE_BUFFER_FULL;
		}

		if (report_mode == 0) {
			SLSI_NET_ERR(dev, "Invalid report event value: %d\n", gscan_param.bucket->report_events);
			return -EINVAL;
		}

		/* In case of epno no_batch mode should be set. */
		if (sdev->epno_active)
			report_mode |= FAPI_REPORTMODE_NO_BATCH;

		ret = slsi_mlme_add_scan(sdev,
					 dev,
					 FAPI_SCANTYPE_GSCAN,
					 report_mode,
					 0,     /* n_ssids */
					 NULL,  /* ssids */
					 nl_gscan_param->nl_bucket[i].num_channels,
					 NULL,  /* ieee80211_channel */
					 &gscan_param,
					 NULL,  /* ies */
					 0,     /* ies_len */
					 false /* wait_for_ind */);

		if (ret != 0) {
			SLSI_NET_ERR(dev, "Failed to add bucket: %d\n", i);

			/* Delete the scan those are already added */
			for (i = (i - 1); i >= 0; i--)
				slsi_mlme_del_scan(sdev, dev, gscan->bucket[i]->scan_id, false);
			break;
		}
	}

	return ret;
}

static int slsi_gscan_add(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	int                        ret = 0;
	struct slsi_dev            *sdev = SDEV_FROM_WIPHY(wiphy);
	struct slsi_nl_gscan_param *nl_gscan_param = NULL;
	struct slsi_gscan          *gscan;
	struct netdev_vif          *ndev_vif;
	int                        buffer_threshold;
	int                        i;

	SLSI_DBG1_NODEV(SLSI_GSCAN, "SUBCMD_ADD_GSCAN\n");

	if (!sdev) {
		SLSI_WARN_NODEV("sdev is NULL\n");
		return -EINVAL;
	}

	if (!slsi_dev_gscan_supported())
		return -ENOTSUPP;

	ndev_vif = slsi_gscan_get_vif(sdev);

	SLSI_MUTEX_LOCK(ndev_vif->scan_mutex);
	/* Allocate memory for the received scan params */
	nl_gscan_param = kzalloc(sizeof(*nl_gscan_param), GFP_KERNEL);
	if (nl_gscan_param == NULL) {
		SLSI_ERR_NODEV("Failed for allocate memory for gscan params\n");
		ret = -ENOMEM;
		goto exit;
	}

	slsi_gscan_add_read_params(nl_gscan_param, data, len);

#ifdef CONFIG_SCSC_WLAN_DEBUG
	slsi_gscan_add_dump_params(nl_gscan_param);
#endif

	ret = slsi_gscan_add_verify_params(nl_gscan_param);
	if (ret) {
		/* After adding a hotlist a new gscan is added with 0 buckets - return success */
		if (nl_gscan_param->num_buckets == 0) {
			kfree(nl_gscan_param);
			SLSI_MUTEX_UNLOCK(ndev_vif->scan_mutex);
			return 0;
		}

		goto exit;
	}

	/* Allocate Memory for the new gscan */
	gscan = kzalloc(sizeof(*gscan), GFP_KERNEL);
	if (gscan == NULL) {
		SLSI_ERR_NODEV("Failed to allocate memory for gscan\n");
		ret = -ENOMEM;
		goto exit;
	}

	gscan->num_buckets = nl_gscan_param->num_buckets;
	gscan->report_threshold_percent = nl_gscan_param->report_threshold_percent;
	gscan->report_threshold_num_scans = nl_gscan_param->report_threshold_num_scans;
	gscan->nl_bucket = nl_gscan_param->nl_bucket[0];

	/* If multiple gscan is added; consider the lowest report_threshold_percent */
	buffer_threshold = (SLSI_GSCAN_MAX_SCAN_CACHE_SIZE * nl_gscan_param->report_threshold_percent) / 100;
	if ((sdev->buffer_threshold == 0) || (buffer_threshold < sdev->buffer_threshold))
		sdev->buffer_threshold = buffer_threshold;

	ret = slsi_gscan_alloc_buckets(sdev, gscan, nl_gscan_param->num_buckets);
	if (ret)
		goto exit_with_gscan_free;

	for (i = 0; i < nl_gscan_param->num_buckets; i++)
		gscan->bucket[i]->report_events = nl_gscan_param->nl_bucket[i].report_events;

	ret = slsi_gscan_add_mlme(sdev, nl_gscan_param, gscan);
	if (ret) {
		/* Free the buckets */
		slsi_gscan_free_buckets(gscan);

		goto exit_with_gscan_free;
	}

	slsi_gscan_add_to_list(&sdev->gscan, gscan);

	goto exit;

exit_with_gscan_free:
	kfree(gscan);
exit:
	kfree(nl_gscan_param);

	SLSI_MUTEX_UNLOCK(ndev_vif->scan_mutex);
	return ret;
}

static int slsi_gscan_del(struct wiphy *wiphy,
			  struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev   *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev;
	struct netdev_vif *ndev_vif;
	struct slsi_gscan *gscan;
	int               ret = 0;
	int               i;

	SLSI_DBG1_NODEV(SLSI_GSCAN, "SUBCMD_DEL_GSCAN\n");

	dev = slsi_gscan_get_netdev(sdev);
	if (!dev) {
		SLSI_WARN_NODEV("dev is NULL\n");
		return -EINVAL;
	}

	ndev_vif = netdev_priv(dev);

	SLSI_MUTEX_LOCK(ndev_vif->scan_mutex);
	while (sdev->gscan != NULL) {
		gscan = sdev->gscan;

		SLSI_DBG3(sdev, SLSI_GSCAN, "gscan = %p, num_buckets = %d\n", gscan, gscan->num_buckets);

		for (i = 0; i < gscan->num_buckets; i++)
			if (gscan->bucket[i]->used)
				slsi_mlme_del_scan(sdev, dev, gscan->bucket[i]->scan_id, false);
		slsi_gscan_free_buckets(gscan);
		sdev->gscan = gscan->next;
		kfree(gscan);
	}
	SLSI_MUTEX_UNLOCK(ndev_vif->scan_mutex);

	slsi_gscan_flush_scan_results(sdev);

	sdev->buffer_threshold = 0;

	return ret;
}

static int slsi_gscan_get_scan_results(struct wiphy *wiphy,
				       struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev          *sdev = SDEV_FROM_WIPHY(wiphy);
	struct sk_buff           *skb;
	struct slsi_gscan_result *scan_res;
	struct nlattr            *scan_hdr;
	struct netdev_vif        *ndev_vif;
	int                      num_results = 0;
	int                      mem_needed;
	const struct nlattr      *attr;
	int                      nl_num_results = 0;
	int                      ret = 0;
	int                      temp;
	int                      type;
	int                      i;

	SLSI_DBG1_NODEV(SLSI_GSCAN, "SUBCMD_GET_SCAN_RESULTS\n");

	/* Read the number of scan results need to be given */
	nla_for_each_attr(attr, data, len, temp) {
		type = nla_type(attr);

		switch (type) {
		case GSCAN_ATTRIBUTE_NUM_OF_RESULTS:
			nl_num_results = nla_get_u32(attr);
			break;
		default:
			SLSI_ERR_NODEV("Unknown attribute: %d\n", type);
			break;
		}
	}

	ndev_vif = slsi_gscan_get_vif(sdev);
	if (!ndev_vif) {
		SLSI_WARN_NODEV("ndev_vif is NULL\n");
		return -EINVAL;
	}

	SLSI_MUTEX_LOCK(ndev_vif->scan_mutex);

	num_results = sdev->num_gscan_results;

	SLSI_DBG3(sdev, SLSI_GSCAN, "nl_num_results: %d, num_results = %d\n", nl_num_results, sdev->num_gscan_results);

	if (num_results == 0) {
		SLSI_DBG1(sdev, SLSI_GSCAN, "No scan results available\n");
		/* Return value should be 0 for this scenario */
		goto exit;
	}

	/* Find the number of results to return */
	if (num_results > nl_num_results)
		num_results = nl_num_results;

	/* 12 bytes additional for scan_id, flags and num_resuls */
	mem_needed = num_results * sizeof(struct slsi_nl_scan_result_param) + 12;

	skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, mem_needed);
	if (skb == NULL) {
		SLSI_ERR_NODEV("skb alloc failed");
		ret = -ENOMEM;
		goto exit;
	}

	scan_hdr = nla_nest_start(skb, GSCAN_ATTRIBUTE_SCAN_RESULTS);
	if (scan_hdr == NULL) {
		kfree_skb(skb);
		SLSI_ERR_NODEV("scan_hdr is NULL.\n");
		ret = -ENOMEM;
		goto exit;
	}

	nla_put_u32(skb, GSCAN_ATTRIBUTE_SCAN_ID, 0);
	nla_put_u8(skb, GSCAN_ATTRIBUTE_SCAN_FLAGS, 0);
	nla_put_u32(skb, GSCAN_ATTRIBUTE_NUM_OF_RESULTS, num_results);

	for (i = 0; i < SLSI_GSCAN_HASH_TABLE_SIZE; i++)
		while (sdev->gscan_hash_table[i]) {
			scan_res = sdev->gscan_hash_table[i];
			sdev->gscan_hash_table[i] = sdev->gscan_hash_table[i]->hnext;
			sdev->num_gscan_results--;
			sdev->buffer_consumed -= scan_res->scan_res_len;
			/* TODO: If IE is included then HAL is not able to parse the results */
			nla_put(skb, GSCAN_ATTRIBUTE_SCAN_RESULTS, sizeof(struct slsi_nl_scan_result_param), &scan_res->nl_scan_res);
			kfree(scan_res);
			num_results--;
			if (num_results == 0)
				goto out;
		}
out:
	nla_nest_end(skb, scan_hdr);

	ret = cfg80211_vendor_cmd_reply(skb);
exit:
	SLSI_MUTEX_UNLOCK(ndev_vif->scan_mutex);
	return ret;
}

static int slsi_gscan_set_hotlist_read_params(struct slsi_nl_hotlist_param *nl_hotlist_param, const void *data, int len)
{
	int                               tmp, tmp1, tmp2, type, j = 0;
	const struct nlattr               *outer, *inner, *iter;
	struct slsi_nl_ap_threshold_param *nl_ap;
	int                               flush;

	nla_for_each_attr(iter, data, len, tmp2) {
		type = nla_type(iter);
		switch (type) {
		case GSCAN_ATTRIBUTE_HOTLIST_BSSIDS:
			nla_for_each_nested(outer, iter, tmp) {
				nl_ap = &nl_hotlist_param->ap[j];
				nla_for_each_nested(inner, outer, tmp1) {
					type = nla_type(inner);
					switch (type) {
					case GSCAN_ATTRIBUTE_BSSID:
						SLSI_ETHER_COPY(&nl_ap->bssid[0], nla_data(inner));
						break;
					case GSCAN_ATTRIBUTE_RSSI_LOW:
						nl_ap->low = nla_get_s8(inner);
						break;
					case GSCAN_ATTRIBUTE_RSSI_HIGH:
						nl_ap->high = nla_get_s8(inner);
						break;
					default:
						SLSI_ERR_NODEV("Unknown type %d\n", type);
						break;
					}
				}
				j++;
			}
			nl_hotlist_param->num_bssid = j;
			break;
		case GSCAN_ATTRIBUTE_HOTLIST_FLUSH:
			flush = nla_get_u8(iter);
			break;
		case GSCAN_ATTRIBUTE_LOST_AP_SAMPLE_SIZE:
			nl_hotlist_param->lost_ap_sample_size = nla_get_u32(iter);
			break;
		default:
			SLSI_ERR_NODEV("No ATTRIBUTE_HOTLIST - %d\n", type);
			break;
		}
	}

	return 0;
}

static int slsi_gscan_set_hotlist(struct wiphy *wiphy,
				  struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev              *sdev = SDEV_FROM_WIPHY(wiphy);
	struct slsi_nl_hotlist_param *nl_hotlist_param;
	struct net_device            *dev;
	struct netdev_vif            *ndev_vif;
	int                          ret = 0;

	SLSI_DBG1_NODEV(SLSI_GSCAN, "SUBCMD_SET_BSSID_HOTLIST\n");

	dev = slsi_gscan_get_netdev(sdev);
	if (!dev) {
		SLSI_WARN_NODEV("dev is NULL\n");
		return -EINVAL;
	}

	ndev_vif = netdev_priv(dev);

	SLSI_MUTEX_LOCK(ndev_vif->scan_mutex);
	/* Allocate memory for the received scan params */
	nl_hotlist_param = kzalloc(sizeof(*nl_hotlist_param), GFP_KERNEL);
	if (nl_hotlist_param == NULL) {
		SLSI_ERR_NODEV("Failed for allocate memory for gscan hotlist_param\n");
		ret = -ENOMEM;
		goto exit;
	}

	slsi_gscan_set_hotlist_read_params(nl_hotlist_param, data, len);

#ifdef CONFIG_SCSC_WLAN_DEBUG
	slsi_gscan_set_hotlist_dump_params(nl_hotlist_param);
#endif
	ret = slsi_mlme_set_bssid_hotlist_req(sdev, dev, nl_hotlist_param);
	if (ret)
		SLSI_ERR_NODEV("Failed to set hostlist\n");

	kfree(nl_hotlist_param);
	SLSI_MUTEX_UNLOCK(ndev_vif->scan_mutex);
exit:
	return ret;
}

static int slsi_gscan_reset_hotlist(struct wiphy *wiphy,
				    struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev   *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev;
	int               ret = 0;
	struct netdev_vif *ndev_vif;

	SLSI_DBG1_NODEV(SLSI_GSCAN, "SUBCMD_RESET_BSSID_HOTLIST\n");

	dev = slsi_gscan_get_netdev(sdev);
	if (!dev) {
		SLSI_WARN_NODEV("dev is NULL\n");
		return -EINVAL;
	}
	ndev_vif = netdev_priv(dev);

	SLSI_MUTEX_LOCK(ndev_vif->scan_mutex);
	ret = slsi_mlme_set_bssid_hotlist_req(sdev, dev, NULL);
	if (ret)
		SLSI_ERR_NODEV("Failed to reset hostlist\n");

	SLSI_MUTEX_UNLOCK(ndev_vif->scan_mutex);

	slsi_gscan_flush_hotlist_results(sdev);

	return ret;
}

static struct slsi_gscan *slsi_mlme_get_tracking_scan_id(struct slsi_dev                          *sdev,
							 struct slsi_nl_significant_change_params *significant_param_ptr)
{
	/* If channel hint is not in significant change req, link to previous gscan, else new gscan */
	struct slsi_gscan *ret_gscan;

	if (sdev->gscan != NULL) {
		ret_gscan = sdev->gscan;
		SLSI_DBG3(sdev, SLSI_GSCAN, "Existing Scan for tracking\n");
	} else {
		struct slsi_gscan *gscan;
		/* Allocate Memory for the new gscan */
		gscan = kzalloc(sizeof(*gscan), GFP_KERNEL);
		if (gscan == NULL) {
			SLSI_ERR(sdev, "Failed to allocate memory for gscan\n");
			return NULL;
		}
		gscan->num_buckets = 1;
		if (slsi_gscan_alloc_buckets(sdev, gscan, gscan->num_buckets) != 0) {
			SLSI_ERR(sdev, "NO free buckets. Abort tracking\n");
			kfree(gscan);
			return NULL;
		}
		/*Build nl_bucket based on channels in significant_param_ptr->ap array*/
		gscan->nl_bucket.band = WIFI_BAND_UNSPECIFIED;
		gscan->nl_bucket.num_channels = 0;
		gscan->nl_bucket.period = 5 * 1000; /* Default */
		slsi_gscan_add_to_list(&sdev->gscan, gscan);
		ret_gscan = gscan;
		SLSI_DBG3(sdev, SLSI_GSCAN, "New Scan for tracking\n");
	}
	SLSI_DBG3(sdev, SLSI_GSCAN, "tracking channel num:%d\n", ret_gscan->nl_bucket.num_channels);
	return ret_gscan;
}

static int slsi_gscan_set_significant_change(struct wiphy *wiphy,
					     struct wireless_dev *wdev, const void *data, int len)
{
	int                                      ret = 0;
	struct slsi_dev                          *sdev = SDEV_FROM_WIPHY(wiphy);
	struct slsi_nl_significant_change_params *significant_change_param;
	u8                                       bss_count = 0;
	struct slsi_nl_ap_threshold_param        *bss_param_ptr;
	int                                      tmp, tmp1, tmp2, type;
	const struct nlattr                      *outer, *inner, *iter;
	struct net_device                        *net_dev;
	struct slsi_gscan                        *gscan;
	struct netdev_vif                        *ndev_vif;

	SLSI_DBG3(sdev, SLSI_GSCAN, "SUBCMD_SET_SIGNIFICANT_CHANGE Received\n");

	significant_change_param = kmalloc(sizeof(*significant_change_param), GFP_KERNEL);
	if (!significant_change_param) {
		SLSI_ERR(sdev, "NO mem for significant_change_param\n");
		return -ENOMEM;
	}
	memset(significant_change_param, 0, sizeof(struct slsi_nl_significant_change_params));
	nla_for_each_attr(iter, data, len, tmp2) {
		type = nla_type(iter);
		switch (type) {
		case GSCAN_ATTRIBUTE_RSSI_SAMPLE_SIZE:
			significant_change_param->rssi_sample_size = nla_get_u16(iter);
			SLSI_DBG3(sdev, SLSI_GSCAN, "rssi_sample_size %d\n", significant_change_param->rssi_sample_size);
			break;
		case GSCAN_ATTRIBUTE_LOST_AP_SAMPLE_SIZE:
			significant_change_param->lost_ap_sample_size = nla_get_u16(iter);
			SLSI_DBG3(sdev, SLSI_GSCAN, "lost_ap_sample_size %d\n", significant_change_param->lost_ap_sample_size);
			break;
		case GSCAN_ATTRIBUTE_MIN_BREACHING:
			significant_change_param->min_breaching = nla_get_u16(iter);
			SLSI_DBG3(sdev, SLSI_GSCAN, "min_breaching %d\n", significant_change_param->min_breaching);
			break;
		case GSCAN_ATTRIBUTE_SIGNIFICANT_CHANGE_BSSIDS:
			nla_for_each_nested(outer, iter, tmp) {
				bss_param_ptr = &significant_change_param->ap[bss_count];
				bss_count++;
				SLSI_DBG3(sdev, SLSI_GSCAN, "bssids[%d]:\n", bss_count);
				if (bss_count == SLSI_GSCAN_MAX_SIGNIFICANT_CHANGE_APS) {
					SLSI_ERR(sdev, "Can support max:%d aps. Skipping excess\n", SLSI_GSCAN_MAX_SIGNIFICANT_CHANGE_APS);
					break;
				}
				nla_for_each_nested(inner, outer, tmp1) {
					switch (nla_type(inner)) {
					case GSCAN_ATTRIBUTE_BSSID:
						SLSI_ETHER_COPY(&bss_param_ptr->bssid[0], nla_data(inner));
						SLSI_DBG3(sdev, SLSI_GSCAN, "\tbssid %pM\n", bss_param_ptr->bssid);
						break;
					case GSCAN_ATTRIBUTE_RSSI_HIGH:
						bss_param_ptr->high = nla_get_s8(inner);
						SLSI_DBG3(sdev, SLSI_GSCAN, "\thigh %d\n", bss_param_ptr->high);
						break;
					case GSCAN_ATTRIBUTE_RSSI_LOW:
						bss_param_ptr->low = nla_get_s8(inner);
						SLSI_DBG3(sdev, SLSI_GSCAN, "\tlow %d\n", bss_param_ptr->low);
						break;
					default:
						SLSI_ERR(sdev, "unknown attribute:%d\n", type);
						break;
					}
				}
			}
			break;
		default:
			SLSI_ERR(sdev, "Unknown type:%d\n", type);
			break;
		}
	}
	significant_change_param->num_bssid = bss_count;
	net_dev = slsi_get_netdev(sdev, SLSI_NET_INDEX_WLAN);
	ndev_vif = netdev_priv(net_dev);

	SLSI_MUTEX_LOCK(ndev_vif->scan_mutex);
	gscan = slsi_mlme_get_tracking_scan_id(sdev, significant_change_param);
	if (gscan) {
		if (slsi_mlme_significant_change_set(sdev, net_dev, significant_change_param)) {
			SLSI_ERR(sdev, "Could not set GSCAN significant cfg\n");
			ret = -EINVAL;
		}
	} else {
		ret = -ENOMEM;
	}
	SLSI_MUTEX_UNLOCK(ndev_vif->scan_mutex);
	kfree(significant_change_param);
	return ret;
}

void slsi_rx_significant_change_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	u32               eventdata_len;
	u16               bssid_count = fapi_get_buff(skb, u.mlme_significant_change_ind.number_of_results);
	u16               rssi_entry_count = fapi_get_buff(skb, u.mlme_significant_change_ind.number_of_rssi_entries);
	u32               i, j;
	u8                *eventdata  = NULL;
	u8                *op_ptr, *ip_ptr;
	u16               *le16_ptr;

	/* convert fapi buffer to wifi-hal structure
	 *   fapi buffer: [mac address 6 bytes] [chan_freq 2 bytes]
	 *                [riis history N*2 bytes]
	 *   wifi-hal structure
	 *      typedef struct {
	 *              uint16_t channel;
	 *              mac_addr bssid;
	 *              short rssi_history[8];
	 *      } ChangeInfo;
	 */

	SLSI_DBG3(sdev, SLSI_GSCAN, "No BSSIDs:%d\n", bssid_count);

	eventdata_len = (8 + (8 * 2)) * bssid_count; /* see wifi-hal structure in above comments */
	eventdata = kmalloc(eventdata_len, GFP_KERNEL);
	if (!eventdata) {
		SLSI_ERR(sdev, "no mem for event data\n");
		slsi_kfree_skb(skb);
		return;
	}

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	op_ptr = eventdata;
	ip_ptr = fapi_get_data(skb);
	for (i = 0; i < bssid_count; i++) {
		le16_ptr = (u16 *)&ip_ptr[6]; /* channel: required unit MHz. received unit 512KHz */
		*(u16 *)op_ptr = le16_to_cpu(*le16_ptr) / 2;
		SLSI_DBG3(sdev, SLSI_GSCAN, "[%d] channel:%d\n", i, *(u16 *)op_ptr);
		op_ptr += 2;

		SLSI_ETHER_COPY(op_ptr, ip_ptr); /* mac_addr */
		SLSI_DBG3(sdev, SLSI_GSCAN, "[%d] mac:%pM\n", i, op_ptr);
		op_ptr += ETH_ALEN;

		for (j = 0; j < 8; j++) {
			if (j < rssi_entry_count) {
				*op_ptr = ip_ptr[8 + j * 2];
				*(op_ptr + 1) = ip_ptr[9 + j * 2];
			} else {
				s16 invalid_rssi = SLSI_GSCAN_INVALID_RSSI;
				*(u16 *)op_ptr = invalid_rssi;
			}
			op_ptr += 2;
		}
		ip_ptr += 8 + (rssi_entry_count * 2);
	}
	SLSI_DBG_HEX(sdev, SLSI_GSCAN, eventdata, eventdata_len, "significant change event buffer:\n");
	SLSI_DBG_HEX(sdev, SLSI_GSCAN, fapi_get_data(skb), fapi_get_datalen(skb), "significant change skb buffer:\n");
	slsi_vendor_event(sdev, SLSI_NL80211_SIGNIFICANT_CHANGE_EVENT, eventdata, eventdata_len);
	kfree(eventdata);
	slsi_kfree_skb(skb);
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
}

void slsi_rx_rssi_report_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_rssi_monitor_evt event_data;

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	SLSI_ETHER_COPY(event_data.bssid, fapi_get_buff(skb, u.mlme_rssi_report_ind.bssid));
	event_data.rssi = fapi_get_s16(skb, u.mlme_rssi_report_ind.rssi);
	SLSI_DBG3(sdev, SLSI_GSCAN, "RSSI threshold breached, Current RSSI for %pM= %d\n", event_data.bssid, event_data.rssi);
	slsi_vendor_event(sdev, SLSI_NL80211_RSSI_REPORT_EVENT, &event_data, sizeof(event_data));
	slsi_kfree_skb(skb);
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
}

static int slsi_gscan_reset_significant_change(struct wiphy *wiphy,
					       struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev    *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device  *net_dev;
	struct netdev_vif  *ndev_vif;
	struct slsi_bucket *bucket = NULL;
	u32                i;

	SLSI_DBG3(sdev, SLSI_GSCAN, "SUBCMD_RESET_SIGNIFICANT_CHANGE Received\n");

	net_dev = slsi_get_netdev(sdev, SLSI_NET_INDEX_WLAN);
	ndev_vif = netdev_priv(net_dev);

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	for (i = 0; i < SLSI_GSCAN_MAX_BUCKETS; i++) {
		bucket = &sdev->bucket[i];
		if (!bucket->used || !bucket->for_change_tracking)
			continue;

		(void)slsi_mlme_del_scan(sdev, net_dev, bucket->scan_id, false);
		bucket->for_change_tracking = false;
		bucket->used = false;
		SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
		return 0;
	}
	SLSI_DBG3(sdev, SLSI_GSCAN, "Significant change scan not found\n");
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);

	return 0;
}

#ifdef CONFIG_SCSC_WLAN_KEY_MGMT_OFFLOAD
static int slsi_key_mgmt_set_pmk(struct wiphy *wiphy,
				 struct wireless_dev *wdev, const void *pmk, int pmklen)
{
	struct slsi_dev    *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device  *net_dev;
	struct netdev_vif  *ndev_vif;
	int r = 0;

	if (wdev->iftype == NL80211_IFTYPE_P2P_CLIENT) {
		SLSI_DBG3(sdev, SLSI_GSCAN, "Not required to set PMK for P2P client\n");
		return r;
	}
	SLSI_DBG3(sdev, SLSI_GSCAN, "SUBCMD_SET_PMK Received\n");

	net_dev = slsi_get_netdev(sdev, SLSI_NET_INDEX_WLAN);
	ndev_vif = netdev_priv(net_dev);

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	r = slsi_mlme_set_pmk(sdev, net_dev, pmk, (u16)pmklen);

	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	return r;
}
#endif

static int slsi_set_bssid_blacklist(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev    *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device  *net_dev;
	struct netdev_vif *ndev_vif;
	int                      temp1;
	int                      type;
	const struct nlattr      *attr;
	u32 num_bssids = 0;
	u8 i = 0;
	int ret;
	u8 *bssid = NULL;
	struct cfg80211_acl_data *acl_data = NULL;

	SLSI_DBG1_NODEV(SLSI_GSCAN, "SUBCMD_SET_BSSID_BLACK_LIST\n");

	net_dev = slsi_get_netdev(sdev, SLSI_NET_INDEX_WLAN);
	if (!net_dev) {
		SLSI_WARN_NODEV("net_dev is NULL\n");
		return -EINVAL;
	}

	ndev_vif = netdev_priv(net_dev);
	/*This subcmd can be issued in either connected or disconnected state.
	  * Hence using scan_mutex and not vif_mutex
	  */
	SLSI_MUTEX_LOCK(ndev_vif->scan_mutex);
	nla_for_each_attr(attr, data, len, temp1) {
		type = nla_type(attr);

		switch (type) {
		case GSCAN_ATTRIBUTE_NUM_BSSID:
			if (acl_data)
				break;

			num_bssids = nla_get_u32(attr);
			acl_data = kmalloc(sizeof(*acl_data) + (sizeof(struct mac_address) * num_bssids), GFP_KERNEL);
			if (!acl_data) {
				ret = -ENOMEM;
				goto exit;
			}
			acl_data->n_acl_entries = num_bssids;
			break;

		case GSCAN_ATTRIBUTE_BLACKLIST_BSSID:
			if (!acl_data) {
				ret = -EINVAL;
				goto exit;
			}
			bssid = (u8 *)nla_data(attr);
			SLSI_ETHER_COPY(acl_data->mac_addrs[i].addr, bssid);
			SLSI_DBG3_NODEV(SLSI_GSCAN, "mac_addrs[%d]:%pM)\n", i, acl_data->mac_addrs[i].addr);
			i++;
			break;
		default:
			SLSI_ERR_NODEV("Unknown attribute: %d\n", type);
			ret = -EINVAL;
			goto exit;
		}
	}

	if (acl_data) {
		acl_data->acl_policy = FAPI_ACLPOLICY_BLACKLIST;
		ret = slsi_mlme_set_acl(sdev, net_dev, 0, acl_data);
		if (ret)
			SLSI_ERR_NODEV("Failed to set bssid blacklist\n");
	} else {
		ret =  -EINVAL;
	}
exit:
	SLSI_MUTEX_UNLOCK(ndev_vif->scan_mutex);
	kfree(acl_data);
	return ret;
}

static int slsi_start_keepalive_offload(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
#ifndef CONFIG_SCSC_WLAN_NAT_KEEPALIVE_DISABLE
	struct slsi_dev    *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device  *net_dev;
	struct netdev_vif *ndev_vif;

	int                      temp;
	int                      type;
	const struct nlattr      *attr;
	u16 ip_pkt_len = 0;
	u8 *ip_pkt = NULL, *src_mac_addr = NULL, *dst_mac_addr = NULL;
	u32 period = 0;
	struct slsi_peer *peer;
	struct sk_buff *skb;
	struct ethhdr *ehdr;
	int r = 0;
	u16 host_tag = 0;
	u8 index = 0;

	SLSI_DBG3(sdev, SLSI_MLME, "SUBCMD_START_KEEPALIVE_OFFLOAD received\n");
	net_dev = slsi_get_netdev(sdev, SLSI_NET_INDEX_WLAN);
	ndev_vif = netdev_priv(net_dev);

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	if (!ndev_vif->activated) {
		SLSI_WARN_NODEV("ndev_vif is not activated\n");
		r = -EINVAL;
		goto exit;
	}
	if (ndev_vif->vif_type != FAPI_VIFTYPE_STATION) {
		SLSI_WARN_NODEV("ndev_vif->vif_type is not FAPI_VIFTYPE_STATION\n");
		r = -EINVAL;
		goto exit;
	}
	if (ndev_vif->sta.vif_status != SLSI_VIF_STATUS_CONNECTED) {
		SLSI_WARN_NODEV("ndev_vif->sta.vif_status is not SLSI_VIF_STATUS_CONNECTED\n");
		r = -EINVAL;
		goto exit;
	}

	peer = slsi_get_peer_from_qs(sdev, net_dev, SLSI_STA_PEER_QUEUESET);
	if (!peer) {
		SLSI_WARN_NODEV("peer is NULL\n");
		r = -EINVAL;
		goto exit;
	}

	nla_for_each_attr(attr, data, len, temp) {
		type = nla_type(attr);

		switch (type) {
		case MKEEP_ALIVE_ATTRIBUTE_IP_PKT_LEN:
			ip_pkt_len = nla_get_u16(attr);
			break;

		case MKEEP_ALIVE_ATTRIBUTE_IP_PKT:
			ip_pkt = (u8 *)nla_data(attr);
			break;

		case MKEEP_ALIVE_ATTRIBUTE_PERIOD_MSEC:
			period = nla_get_u32(attr);
			break;

		case MKEEP_ALIVE_ATTRIBUTE_DST_MAC_ADDR:
			dst_mac_addr = (u8 *)nla_data(attr);
			break;

		case MKEEP_ALIVE_ATTRIBUTE_SRC_MAC_ADDR:
			src_mac_addr = (u8 *)nla_data(attr);
			break;

		case MKEEP_ALIVE_ATTRIBUTE_ID:
			index = nla_get_u8(attr);
			break;

		default:
			SLSI_ERR_NODEV("Unknown attribute: %d\n", type);
			r = -EINVAL;
			goto exit;
		}
	}

	/* Stop any existing request. This may fail if no request exists
	  * so ignore the return value
	  */
	slsi_mlme_send_frame_mgmt(sdev, net_dev, NULL, 0,
				  FAPI_DATAUNITDESCRIPTOR_IEEE802_3_FRAME,
				  FAPI_MESSAGETYPE_PERIODIC_OFFLOAD,
				  ndev_vif->sta.keepalive_host_tag[index - 1], 0, 0, 0);

	skb = slsi_alloc_skb(sizeof(struct ethhdr) + ip_pkt_len, GFP_KERNEL);
	if (!skb) {
		SLSI_WARN_NODEV("Memory allocation failed for skb\n");
		r = -ENOMEM;
		goto exit;
	}

	skb_reset_mac_header(skb);
	skb_set_network_header(skb, sizeof(struct ethhdr));

	/* Ethernet Header */
	ehdr = (struct ethhdr *)skb_put(skb, sizeof(struct ethhdr));

	if (dst_mac_addr)
		SLSI_ETHER_COPY(ehdr->h_dest, dst_mac_addr);
	if (src_mac_addr)
		SLSI_ETHER_COPY(ehdr->h_source, src_mac_addr);
	ehdr->h_proto = cpu_to_be16(ETH_P_IP);
	if (ip_pkt)
		memcpy(skb_put(skb, ip_pkt_len), ip_pkt, ip_pkt_len);

	skb->dev = net_dev;
	skb->protocol = ETH_P_IP;
	skb->ip_summed = CHECKSUM_UNNECESSARY;

	/* Queueset 0 AC 0 */
	skb->queue_mapping = slsi_netif_get_peer_queue(0, 0);

	/* Enabling the "Don't Fragment" Flag in the IP Header */
	ip_hdr(skb)->frag_off |= htons(IP_DF);

	/* Calculation of IP header checksum */
	ip_hdr(skb)->check = 0;
	ip_send_check(ip_hdr(skb));

	host_tag = slsi_tx_mgmt_host_tag(sdev);
	r = slsi_mlme_send_frame_data(sdev, net_dev, skb, FAPI_MESSAGETYPE_PERIODIC_OFFLOAD, host_tag,
				      0, (period * 1000));
	if (r == 0)
		ndev_vif->sta.keepalive_host_tag[index - 1] = host_tag;
	else
		slsi_kfree_skb(skb);

exit:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	return r;
#else
	SLSI_DBG3_NODEV(SLSI_MLME, "SUBCMD_START_KEEPALIVE_OFFLOAD received\n");
	SLSI_DBG3_NODEV(SLSI_MLME, "NAT Keep Alive Feature is disabled\n");
	return -EOPNOTSUPP;

#endif
}

static int slsi_stop_keepalive_offload(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
#ifndef CONFIG_SCSC_WLAN_NAT_KEEPALIVE_DISABLE
	struct slsi_dev    *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device  *net_dev;
	struct netdev_vif *ndev_vif;
	int r = 0;
	int                      temp;
	int                      type;
	const struct nlattr      *attr;
	u8 index = 0;

	SLSI_DBG3(sdev, SLSI_MLME, "SUBCMD_STOP_KEEPALIVE_OFFLOAD received\n");
	net_dev = slsi_get_netdev(sdev, SLSI_NET_INDEX_WLAN);
	ndev_vif = netdev_priv(net_dev);

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	if (!ndev_vif->activated) {
		SLSI_WARN(sdev, "VIF is not activated\n");
		r = -EINVAL;
		goto exit;
	}
	if (ndev_vif->vif_type != FAPI_VIFTYPE_STATION) {
		SLSI_WARN(sdev, "Not a STA VIF\n");
		r = -EINVAL;
		goto exit;
	}
	if (ndev_vif->sta.vif_status != SLSI_VIF_STATUS_CONNECTED) {
		SLSI_WARN(sdev, "VIF is not connected\n");
		r = -EINVAL;
		goto exit;
	}

	nla_for_each_attr(attr, data, len, temp) {
		type = nla_type(attr);

		switch (type) {
		case MKEEP_ALIVE_ATTRIBUTE_ID:
			index = nla_get_u8(attr);
			break;

		default:
			SLSI_ERR_NODEV("Unknown attribute: %d\n", type);
			r = -EINVAL;
			goto exit;
		}
	}

	r = slsi_mlme_send_frame_mgmt(sdev, net_dev, NULL, 0, FAPI_DATAUNITDESCRIPTOR_IEEE802_3_FRAME,
				      FAPI_MESSAGETYPE_PERIODIC_OFFLOAD, ndev_vif->sta.keepalive_host_tag[index - 1], 0, 0, 0);
	ndev_vif->sta.keepalive_host_tag[index - 1] = 0;

exit:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	return r;
#else
	SLSI_DBG3_NODEV(SLSI_MLME, "SUBCMD_STOP_KEEPALIVE_OFFLOAD received\n");
	SLSI_DBG3_NODEV(SLSI_MLME, "NAT Keep Alive Feature is disabled\n");
	return -EOPNOTSUPP;

#endif
}

static inline int slsi_epno_ssid_list_get(struct slsi_dev *sdev,
					  struct slsi_epno_ssid_param *epno_ssid_params, const struct nlattr *outer)
{
	int type, tmp;
	u8  epno_auth;
	u8  len = 0;
	const struct nlattr *inner;

	nla_for_each_nested(inner, outer, tmp) {
		type = nla_type(inner);
		switch (type) {
		case SLSI_ATTRIBUTE_EPNO_FLAGS:
			epno_ssid_params->flags |= nla_get_u16(inner);
			break;
		case SLSI_ATTRIBUTE_EPNO_AUTH:
			epno_auth = nla_get_u8(inner);
			if (epno_auth & SLSI_EPNO_AUTH_FIELD_WEP_OPEN)
				epno_ssid_params->flags |= FAPI_EPNOPOLICY_AUTH_OPEN;
			else if (epno_auth & SLSI_EPNO_AUTH_FIELD_WPA_PSK)
				epno_ssid_params->flags |= FAPI_EPNOPOLICY_AUTH_PSK;
			else if (epno_auth & SLSI_EPNO_AUTH_FIELD_WPA_EAP)
				epno_ssid_params->flags |= FAPI_EPNOPOLICY_AUTH_EAPOL;
			break;
		case SLSI_ATTRIBUTE_EPNO_SSID_LEN:
			len = nla_get_u8(inner);
			if (len <= 32) {
				epno_ssid_params->ssid_len = len;
			} else {
				SLSI_ERR(sdev, "SSID too long %d\n", len);
				return -EINVAL;
			}
			break;
		case SLSI_ATTRIBUTE_EPNO_SSID:
			memcpy(epno_ssid_params->ssid, nla_data(inner), len);
			break;
		default:
			SLSI_WARN(sdev, "Ignoring unknown type:%d\n", type);
		}
	}
	return 0;
}

static int slsi_set_epno_ssid(struct wiphy *wiphy,
			      struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev             *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device           *net_dev;
	struct netdev_vif           *ndev_vif;
	int                         r = 0;
	int                         tmp, tmp1, type, num = 0;
	const struct nlattr         *outer, *iter;
	u8                          i = 0;
	struct slsi_epno_ssid_param *epno_ssid_params;
	struct slsi_epno_param *epno_params;

	SLSI_DBG3(sdev, SLSI_GSCAN, "SUBCMD_SET_EPNO_LIST Received\n");

	if (!slsi_dev_epno_supported())
		return -EOPNOTSUPP;

	epno_params = kmalloc((sizeof(*epno_params) + (sizeof(*epno_ssid_params) * SLSI_GSCAN_MAX_EPNO_SSIDS)),
			      GFP_KERNEL);
	if (!epno_params) {
		SLSI_ERR(sdev, "Mem alloc fail\n");
		return -ENOMEM;
	}
	net_dev = slsi_get_netdev(sdev, SLSI_NET_INDEX_WLAN);
	ndev_vif = netdev_priv(net_dev);
	nla_for_each_attr(iter, data, len, tmp1) {
		type = nla_type(iter);
		switch (type) {
		case SLSI_ATTRIBUTE_EPNO_MINIMUM_5G_RSSI:
			epno_params->min_5g_rssi = nla_get_u16(iter);
			break;
		case SLSI_ATTRIBUTE_EPNO_MINIMUM_2G_RSSI:
			epno_params->min_2g_rssi = nla_get_u16(iter);
			break;
		case SLSI_ATTRIBUTE_EPNO_INITIAL_SCORE_MAX:
			epno_params->initial_score_max = nla_get_u16(iter);
			break;
		case SLSI_ATTRIBUTE_EPNO_CUR_CONN_BONUS:
			epno_params->current_connection_bonus = nla_get_u8(iter);
			break;
		case SLSI_ATTRIBUTE_EPNO_SAME_NETWORK_BONUS:
			epno_params->same_network_bonus = nla_get_u8(iter);
			break;
		case SLSI_ATTRIBUTE_EPNO_SECURE_BONUS:
			epno_params->secure_bonus = nla_get_u8(iter);
			break;
		case SLSI_ATTRIBUTE_EPNO_5G_BONUS:
			epno_params->band_5g_bonus = nla_get_u8(iter);
			break;
		case SLSI_ATTRIBUTE_EPNO_SSID_LIST:
			nla_for_each_nested(outer, iter, tmp) {
				epno_ssid_params = &epno_params->epno_ssid[i];
				epno_ssid_params->flags = 0;
				r = slsi_epno_ssid_list_get(sdev, epno_ssid_params, outer);
				if (r)
					goto exit;
				i++;
			}
			break;
		case SLSI_ATTRIBUTE_EPNO_SSID_NUM:
			num = nla_get_u8(iter);
			if (num > SLSI_GSCAN_MAX_EPNO_SSIDS) {
				SLSI_ERR(sdev, "Cannot support %d SSIDs. max %d\n", num, SLSI_GSCAN_MAX_EPNO_SSIDS);
				r = -EINVAL;
				goto exit;
			}
			epno_params->num_networks = num;
			break;
		default:
			SLSI_ERR(sdev, "Invalid attribute %d\n", type);
			r = -EINVAL;
			goto exit;
		}
	}

	if (i != num) {
		SLSI_ERR(sdev, "num_ssid %d does not match ssids sent %d\n", num, i);
		r = -EINVAL;
		goto exit;
	}
	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	r = slsi_mlme_set_pno_list(sdev, num, epno_params, NULL);
	if (r == 0)
		sdev->epno_active = (num != 0);
	else
		sdev->epno_active = false;
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
exit:
	kfree(epno_params);
	return r;
}

static int slsi_set_hs_params(struct wiphy *wiphy,
			      struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev            *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device          *net_dev;
	struct netdev_vif          *ndev_vif;
	int                        r = 0;
	int                        tmp, tmp1, tmp2, type, num = 0;
	const struct nlattr        *outer, *inner, *iter;
	u8                         i = 0;
	struct slsi_epno_hs2_param *epno_hs2_params_array;
	struct slsi_epno_hs2_param *epno_hs2_params;

	SLSI_DBG3(sdev, SLSI_GSCAN, "SUBCMD_SET_HS_LIST Received\n");

	if (!slsi_dev_epno_supported())
		return -EOPNOTSUPP;

	epno_hs2_params_array = kmalloc(sizeof(*epno_hs2_params_array) * SLSI_GSCAN_MAX_EPNO_HS2_PARAM, GFP_KERNEL);
	if (!epno_hs2_params_array) {
		SLSI_ERR(sdev, "Mem alloc fail\n");
		return -ENOMEM;
	}

	net_dev = slsi_get_netdev(sdev, SLSI_NET_INDEX_WLAN);
	ndev_vif = netdev_priv(net_dev);

	nla_for_each_attr(iter, data, len, tmp2) {
		type = nla_type(iter);
		switch (type) {
		case SLSI_ATTRIBUTE_EPNO_HS_PARAM_LIST:
			nla_for_each_nested(outer, iter, tmp) {
				epno_hs2_params = &epno_hs2_params_array[i];
				i++;
				nla_for_each_nested(inner, outer, tmp1) {
					type = nla_type(inner);

					switch (type) {
					case SLSI_ATTRIBUTE_EPNO_HS_ID:
						epno_hs2_params->id = (u32)nla_get_u32(inner);
						break;
					case SLSI_ATTRIBUTE_EPNO_HS_REALM:
						memcpy(epno_hs2_params->realm, nla_data(inner), 256);
						break;
					case SLSI_ATTRIBUTE_EPNO_HS_CONSORTIUM_IDS:
						memcpy(epno_hs2_params->roaming_consortium_ids, nla_data(inner), 16 * 8);
						break;
					case SLSI_ATTRIBUTE_EPNO_HS_PLMN:
						memcpy(epno_hs2_params->plmn, nla_data(inner), 3);
						break;
					default:
						SLSI_WARN(sdev, "Ignoring unknown type:%d\n", type);
					}
				}
			}
			break;
		case SLSI_ATTRIBUTE_EPNO_HS_NUM:
			num = nla_get_u8(iter);
			if (num > SLSI_GSCAN_MAX_EPNO_HS2_PARAM) {
				SLSI_ERR(sdev, "Cannot support %d SSIDs. max %d\n", num, SLSI_GSCAN_MAX_EPNO_SSIDS);
				r = -EINVAL;
				goto exit;
			}
			break;
		default:
			SLSI_ERR(sdev, "Invalid attribute %d\n", type);
			r = -EINVAL;
			goto exit;
		}
	}
	if (i != num) {
		SLSI_ERR(sdev, "num_ssid %d does not match ssids sent %d\n", num, i);
		r = -EINVAL;
		goto exit;
	}

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	r = slsi_mlme_set_pno_list(sdev, num, NULL, epno_hs2_params_array);
	if (r == 0)
		sdev->epno_active = true;
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
exit:
	kfree(epno_hs2_params_array);
	return r;
}

static int slsi_reset_hs_params(struct wiphy *wiphy,
				struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev    *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device  *net_dev;
	struct netdev_vif  *ndev_vif;
	int                r;

	SLSI_DBG3(sdev, SLSI_GSCAN, "SUBCMD_RESET_HS_LIST Received\n");

	if (!slsi_dev_epno_supported())
		return -EOPNOTSUPP;

	net_dev = slsi_get_netdev(sdev, SLSI_NET_INDEX_WLAN);
	ndev_vif = netdev_priv(net_dev);

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	r = slsi_mlme_set_pno_list(sdev, 0, NULL, NULL);
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	sdev->epno_active = false;
	return r;
}

static int slsi_set_rssi_monitor(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev            *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device          *net_dev;
	struct netdev_vif          *ndev_vif;
	int                        r = 0;
	int                      temp;
	int                      type;
	const struct nlattr      *attr;
	s8 min_rssi = 0, max_rssi = 0;
	u16 enable = 0;

	SLSI_DBG3(sdev, SLSI_GSCAN, "Recd RSSI monitor command\n");

	net_dev = slsi_get_netdev(sdev, SLSI_NET_INDEX_WLAN);
	if (net_dev == NULL) {
		SLSI_ERR(sdev, "netdev is NULL!!\n");
		return -ENODEV;
	}

	ndev_vif = netdev_priv(net_dev);
	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	if (!ndev_vif->activated) {
		SLSI_ERR(sdev, "Vif not activated\n");
		r = -EINVAL;
		goto exit;
	}
	if (ndev_vif->vif_type != FAPI_VIFTYPE_STATION) {
		SLSI_ERR(sdev, "Not a STA vif\n");
		r = -EINVAL;
		goto exit;
	}
	if (ndev_vif->sta.vif_status != SLSI_VIF_STATUS_CONNECTED) {
		SLSI_ERR(sdev, "STA vif not connected\n");
		r = -EINVAL;
		goto exit;
	}

	nla_for_each_attr(attr, data, len, temp) {
		type = nla_type(attr);
		switch (type) {
		case SLSI_RSSI_MONITOR_ATTRIBUTE_START:
			enable = (u16)nla_get_u8(attr);
			break;
		case SLSI_RSSI_MONITOR_ATTRIBUTE_MIN_RSSI:
			min_rssi = nla_get_s8(attr);
			break;
		case SLSI_RSSI_MONITOR_ATTRIBUTE_MAX_RSSI:
			max_rssi = nla_get_s8(attr);
			break;
		default:
			r = -EINVAL;
			goto exit;
		}
	}
	if (min_rssi > max_rssi) {
		SLSI_ERR(sdev, "Invalid params, min_rssi= %d ,max_rssi = %d\n", min_rssi, max_rssi);
		r = -EINVAL;
		goto exit;
	}
	r = slsi_mlme_set_rssi_monitor(sdev, net_dev, enable, min_rssi, max_rssi);
exit:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	return r;
}

#ifdef CONFIG_SCSC_WLAN_DEBUG
void slsi_lls_debug_dump_stats(struct slsi_dev *sdev, struct slsi_lls_radio_stat *radio_stat,
			       struct slsi_lls_iface_stat *iface_stat, u8 *buf, int buf_len, int num_of_radios)
{
	int i, j;

	for (j = 0; j < num_of_radios; j++) {
		SLSI_DBG3(sdev, SLSI_GSCAN, "radio_stat====\n");
		SLSI_DBG3(sdev, SLSI_GSCAN, "\tradio_id : %d, on_time : %d, tx_time : %d, rx_time : %d,"
			  "on_time_scan : %d, num_channels : %d\n", radio_stat->radio, radio_stat->on_time,
			  radio_stat->tx_time, radio_stat->rx_time, radio_stat->on_time_scan,
			  radio_stat->num_channels);

		radio_stat = (struct slsi_lls_radio_stat *)((u8 *)radio_stat + sizeof(struct slsi_lls_radio_stat) +
			     (sizeof(struct slsi_lls_channel_stat) * radio_stat->num_channels));
	}
	SLSI_DBG3(sdev, SLSI_GSCAN, "iface_stat====\n");
	SLSI_DBG3(sdev, SLSI_GSCAN, "\tiface %p info : (mode : %d, mac_addr : %pM, state : %d, roaming : %d,"
		  " capabilities : %d, ssid : %s, bssid : %pM, ap_country_str : [%d%d%d])\trssi_data : %d\n",
		  iface_stat->iface, iface_stat->info.mode, iface_stat->info.mac_addr, iface_stat->info.state,
		  iface_stat->info.roaming, iface_stat->info.capabilities, iface_stat->info.ssid,
		  iface_stat->info.bssid, iface_stat->info.ap_country_str[0], iface_stat->info.ap_country_str[1],
		  iface_stat->info.ap_country_str[2], iface_stat->rssi_data);

	SLSI_DBG3(sdev, SLSI_GSCAN, "\tnum_peers %d\n", iface_stat->num_peers);
	for (i = 0; i < iface_stat->num_peers; i++) {
		SLSI_DBG3(sdev, SLSI_GSCAN, "\t\tpeer_mac_address %pM\n", iface_stat->peer_info[i].peer_mac_address);
	}

	SLSI_DBG_HEX(sdev, SLSI_GSCAN, buf, buf_len, "return buffer\n");
}
#endif

static int slsi_lls_set_stats(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev     *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device   *net_dev = NULL;
	struct netdev_vif   *ndev_vif = NULL;
	int                 temp;
	int                 type;
	const struct nlattr *attr;
	u32                 mpdu_size_threshold = 0;
	u32                 aggr_stat_gathering = 0;
	int                 r = 0, i;

	if (!slsi_dev_lls_supported())
		return -EOPNOTSUPP;

	if (slsi_is_test_mode_enabled()) {
		SLSI_WARN(sdev, "not supported in WlanLite mode\n");
		return -EOPNOTSUPP;
	}

	nla_for_each_attr(attr, data, len, temp) {
		type = nla_type(attr);

		switch (type) {
		case LLS_ATTRIBUTE_SET_MPDU_SIZE_THRESHOLD:
			mpdu_size_threshold = nla_get_u32(attr);
			break;

		case LLS_ATTRIBUTE_SET_AGGR_STATISTICS_GATHERING:
			aggr_stat_gathering = nla_get_u32(attr);
			break;

		default:
			SLSI_ERR_NODEV("Unknown attribute: %d\n", type);
			r = -EINVAL;
		}
	}

	SLSI_MUTEX_LOCK(sdev->device_config_mutex);
	/* start Statistics measurements in Firmware */
	(void)slsi_mlme_start_link_stats_req(sdev, mpdu_size_threshold, aggr_stat_gathering);

	net_dev = slsi_get_netdev(sdev, SLSI_NET_INDEX_WLAN);
	if (net_dev) {
		ndev_vif = netdev_priv(net_dev);
		for (i = 0; i < SLSI_LLS_AC_MAX; i++) {
			ndev_vif->rx_packets[i] = 0;
			ndev_vif->tx_packets[i] = 0;
			ndev_vif->tx_no_ack[i] = 0;
		}
	}
	SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);
	return 0;
}

static int slsi_lls_clear_stats(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev    *sdev = SDEV_FROM_WIPHY(wiphy);
	int                      temp;
	int                      type;
	const struct nlattr      *attr;
	u32 stats_clear_req_mask = 0;
	u32 stop_req             = 0;
	int r = 0, i;
	struct net_device *net_dev = NULL;
	struct netdev_vif *ndev_vif = NULL;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");

	nla_for_each_attr(attr, data, len, temp) {
		type = nla_type(attr);

		switch (type) {
		case LLS_ATTRIBUTE_CLEAR_STOP_REQUEST_MASK:
			stats_clear_req_mask = nla_get_u32(attr);
			SLSI_DBG3(sdev, SLSI_GSCAN, "stats_clear_req_mask:%u\n", stats_clear_req_mask);
			break;

		case LLS_ATTRIBUTE_CLEAR_STOP_REQUEST:
			stop_req = nla_get_u32(attr);
			SLSI_DBG3(sdev, SLSI_GSCAN, "stop_req:%u\n", stop_req);
			break;

		default:
			SLSI_ERR(sdev, "Unknown attribute:%d\n", type);
			r = -EINVAL;
		}
	}

	/* stop_req = 0 : clear the stats which are flaged 0
	 * stop_req = 1 : clear the stats which are flaged 1
	 */
	if (!stop_req)
		stats_clear_req_mask = ~stats_clear_req_mask;

	SLSI_MUTEX_LOCK(sdev->device_config_mutex);
	(void)slsi_mlme_stop_link_stats_req(sdev, stats_clear_req_mask);
	net_dev = slsi_get_netdev(sdev, SLSI_NET_INDEX_WLAN);
	if (net_dev) {
		ndev_vif = netdev_priv(net_dev);
		for (i = 0; i < SLSI_LLS_AC_MAX; i++) {
			ndev_vif->rx_packets[i] = 0;
			ndev_vif->tx_packets[i] = 0;
			ndev_vif->tx_no_ack[i] = 0;
		}
	}
	SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);
	return 0;
}

static u32 slsi_lls_ie_to_cap(const u8 *ies, int ies_len)
{
	u32 capabilities = 0;
	const u8 *ie_data;
	const u8 *ie;
	int ie_len;

	if (!ies || ies_len == 0) {
		SLSI_ERR_NODEV("no ie[&%p %d]\n", ies, ies_len);
		return 0;
	}
	ie = cfg80211_find_ie(WLAN_EID_EXT_CAPABILITY, ies, ies_len);
	if (ie) {
		ie_len = ie[1];
		ie_data = &ie[2];
		if ((ie_len >= 4) && (ie_data[3] & SLSI_WLAN_EXT_CAPA3_INTERWORKING_ENABLED))
			capabilities |= SLSI_LLS_CAPABILITY_INTERWORKING;
		if ((ie_len >= 7) && (ie_data[6] & 0x01)) /* Bit48: UTF-8 ssid */
			capabilities |= SLSI_LLS_CAPABILITY_SSID_UTF8;
	}

	ie = cfg80211_find_vendor_ie(WLAN_OUI_WFA, SLSI_WLAN_OUI_TYPE_WFA_HS20_IND, ies, ies_len);
	if (ie)
		capabilities |= SLSI_LLS_CAPABILITY_HS20;
	return capabilities;
}

static void slsi_lls_iface_sta_stats(struct slsi_dev *sdev, struct netdev_vif *ndev_vif,
				     struct slsi_lls_iface_stat *iface_stat)
{
	int                       i;
	struct slsi_lls_interface_link_layer_info *lls_info = &iface_stat->info;
	enum slsi_lls_peer_type   peer_type;
	struct slsi_peer          *peer;
	const u8                  *ie_data, *ie;
	u8                        ie_len;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");

	if (ndev_vif->ifnum == SLSI_NET_INDEX_WLAN) {
		lls_info->mode = SLSI_LLS_INTERFACE_STA;
		peer_type = SLSI_LLS_PEER_AP;
	} else {
		lls_info->mode = SLSI_LLS_INTERFACE_P2P_CLIENT;
		peer_type = SLSI_LLS_PEER_P2P_GO;
	}

	switch (ndev_vif->sta.vif_status) {
	case SLSI_VIF_STATUS_CONNECTING:
		lls_info->state = SLSI_LLS_AUTHENTICATING;
		break;
	case SLSI_VIF_STATUS_CONNECTED:
		lls_info->state = SLSI_LLS_ASSOCIATED;
		break;
	default:
		lls_info->state = SLSI_LLS_DISCONNECTED;
	}
	lls_info->roaming = ndev_vif->sta.roam_in_progress ?
				SLSI_LLS_ROAMING_ACTIVE : SLSI_LLS_ROAMING_IDLE;

	iface_stat->info.capabilities = 0;
	lls_info->ssid[0] = 0;
	if (ndev_vif->sta.sta_bss) {
		ie = cfg80211_find_ie(WLAN_EID_SSID, ndev_vif->sta.sta_bss->ies->data,
				      ndev_vif->sta.sta_bss->ies->len);
		if (ie) {
			ie_len = ie[1];
			ie_data = &ie[2];
			memcpy(lls_info->ssid, ie_data, ie_len);
			lls_info->ssid[ie_len] = 0;
		}
		SLSI_ETHER_COPY(lls_info->bssid, ndev_vif->sta.sta_bss->bssid);
		ie = cfg80211_find_ie(WLAN_EID_COUNTRY, ndev_vif->sta.sta_bss->ies->data,
				      ndev_vif->sta.sta_bss->ies->len);
		if (ie) {
			ie_data = &ie[2];
			memcpy(lls_info->ap_country_str, ie_data, 3);
			iface_stat->peer_info[0].capabilities |= SLSI_LLS_CAPABILITY_COUNTRY;
		}
	}

	peer = ndev_vif->peer_sta_record[SLSI_STA_PEER_QUEUESET]; /* connected AP */
	if (peer && peer->valid && peer->assoc_ie && peer->assoc_resp_ie) {
		iface_stat->info.capabilities |= slsi_lls_ie_to_cap(peer->assoc_ie->data, peer->assoc_ie->len);
		if (peer->capabilities & WLAN_CAPABILITY_PRIVACY) {
			iface_stat->peer_info[0].capabilities |= SLSI_LLS_CAPABILITY_PROTECTED;
			iface_stat->info.capabilities |= SLSI_LLS_CAPABILITY_PROTECTED;
		}
		if (peer->qos_enabled) {
			iface_stat->peer_info[0].capabilities |= SLSI_LLS_CAPABILITY_QOS;
			iface_stat->info.capabilities |= SLSI_LLS_CAPABILITY_QOS;
		}
		iface_stat->peer_info[0].capabilities |= slsi_lls_ie_to_cap(peer->assoc_resp_ie->data, peer->assoc_resp_ie->len);

		SLSI_ETHER_COPY(iface_stat->peer_info[0].peer_mac_address, peer->address);
		iface_stat->peer_info[0].type = peer_type;
		iface_stat->num_peers = 1;
	}

	for (i = MAP_AID_TO_QS(SLSI_TDLS_PEER_INDEX_MIN); i <= MAP_AID_TO_QS(SLSI_TDLS_PEER_INDEX_MAX); i++) {
		peer = ndev_vif->peer_sta_record[i];
		if (peer && peer->valid) {
			SLSI_ETHER_COPY(iface_stat->peer_info[iface_stat->num_peers].peer_mac_address, peer->address);
			iface_stat->peer_info[iface_stat->num_peers].type = SLSI_LLS_PEER_TDLS;
			if (peer->qos_enabled)
				iface_stat->peer_info[iface_stat->num_peers].capabilities |= SLSI_LLS_CAPABILITY_QOS;
			iface_stat->peer_info[iface_stat->num_peers].num_rate = 0;
			iface_stat->num_peers++;
		}
	}
}

static void slsi_lls_iface_ap_stats(struct slsi_dev *sdev, struct netdev_vif *ndev_vif, struct slsi_lls_iface_stat *iface_stat)
{
	enum slsi_lls_peer_type peer_type = SLSI_LLS_PEER_INVALID;
	struct slsi_peer        *peer;
	int                     i;
	struct net_device *dev;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");

	/* We are AP/GO, so we advertize our own country. */
	memcpy(iface_stat->info.ap_country_str, iface_stat->info.country_str, 3);

	if (ndev_vif->ifnum == SLSI_NET_INDEX_WLAN) {
		iface_stat->info.mode = SLSI_LLS_INTERFACE_SOFTAP;
		peer_type = SLSI_LLS_PEER_STA;
	} else if (ndev_vif->ifnum == SLSI_NET_INDEX_P2PX_SWLAN) {
		dev = sdev->netdev[SLSI_NET_INDEX_P2PX_SWLAN];
		if (SLSI_IS_VIF_INDEX_P2P_GROUP(sdev, ndev_vif)) {
			iface_stat->info.mode = SLSI_LLS_INTERFACE_P2P_GO;
			peer_type = SLSI_LLS_PEER_P2P_CLIENT;
		}
	}

	for (i = MAP_AID_TO_QS(SLSI_PEER_INDEX_MIN); i <= MAP_AID_TO_QS(SLSI_PEER_INDEX_MAX); i++) {
		peer = ndev_vif->peer_sta_record[i];
		if (peer && peer->valid) {
			SLSI_ETHER_COPY(iface_stat->peer_info[iface_stat->num_peers].peer_mac_address, peer->address);
			iface_stat->peer_info[iface_stat->num_peers].type = peer_type;
			iface_stat->peer_info[iface_stat->num_peers].num_rate = 0;
			if (peer->qos_enabled)
				iface_stat->peer_info[iface_stat->num_peers].capabilities = SLSI_LLS_CAPABILITY_QOS;
			iface_stat->num_peers++;
		}
	}

	memcpy(iface_stat->info.ssid, ndev_vif->ap.ssid, ndev_vif->ap.ssid_len);
	iface_stat->info.ssid[ndev_vif->ap.ssid_len] = 0;
	if (ndev_vif->ap.privacy)
		iface_stat->info.capabilities |= SLSI_LLS_CAPABILITY_PROTECTED;
	if (ndev_vif->ap.qos_enabled)
		iface_stat->info.capabilities |= SLSI_LLS_CAPABILITY_QOS;
}

static void slsi_lls_iface_stat_fill(struct slsi_dev *sdev,
				     struct net_device *net_dev,
				     struct slsi_lls_iface_stat *iface_stat)
{
	int                       i;
	struct netdev_vif         *ndev_vif;
	struct slsi_mib_data      mibrsp = { 0, NULL };
	struct slsi_mib_value     *values = NULL;
	struct slsi_mib_get_entry get_values[] = {{ SLSI_PSID_UNIFI_AC_RETRIES, { SLSI_TRAFFIC_Q_BE + 1, 0 } },
						 { SLSI_PSID_UNIFI_AC_RETRIES, { SLSI_TRAFFIC_Q_BK + 1, 0 } },
						 { SLSI_PSID_UNIFI_AC_RETRIES, { SLSI_TRAFFIC_Q_VI + 1, 0 } },
						 { SLSI_PSID_UNIFI_AC_RETRIES, { SLSI_TRAFFIC_Q_VO + 1, 0 } },
						 { SLSI_PSID_UNIFI_BEACON_RECEIVED, {0, 0} },
						 { SLSI_PSID_UNIFI_PS_LEAKY_AP, {0, 0} },
						 { SLSI_PSID_UNIFI_RSSI, {0, 0} } };

	iface_stat->iface = NULL;
	iface_stat->info.mode = SLSI_LLS_INTERFACE_UNKNOWN;
	iface_stat->info.country_str[0] = sdev->device_config.domain_info.regdomain->alpha2[0];
	iface_stat->info.country_str[1] = sdev->device_config.domain_info.regdomain->alpha2[1];
	iface_stat->info.country_str[2] = ' '; /* 3rd char of our country code is ASCII<space> */

	for (i = 0; i < SLSI_LLS_AC_MAX; i++)
		iface_stat->ac[i].ac = SLSI_LLS_AC_MAX;

	if (!net_dev)
		return;

	ndev_vif = netdev_priv(net_dev);
	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	if (!ndev_vif->activated)
		goto exit;

	if (ndev_vif->vif_type == FAPI_VIFTYPE_STATION) {
		slsi_lls_iface_sta_stats(sdev, ndev_vif, iface_stat);
	} else if (ndev_vif->vif_type == FAPI_VIFTYPE_AP) {
		slsi_lls_iface_ap_stats(sdev, ndev_vif, iface_stat);
		SLSI_ETHER_COPY(iface_stat->info.bssid, net_dev->dev_addr);
	}
	SLSI_ETHER_COPY(iface_stat->info.mac_addr, net_dev->dev_addr);

	mibrsp.dataLength = 10 * sizeof(get_values) / sizeof(get_values[0]);
	mibrsp.data = kmalloc(mibrsp.dataLength, GFP_KERNEL);
	if (!mibrsp.data) {
		SLSI_ERR(sdev, "Cannot kmalloc %d bytes for interface MIBs\n", mibrsp.dataLength);
		goto exit;
	}

	values = slsi_read_mibs(sdev, net_dev, get_values, sizeof(get_values) / sizeof(get_values[0]), &mibrsp);
	if (!values)
		goto exit;

	for (i = 0; i < SLSI_LLS_AC_MAX; i++) {
		if (values[i].type == SLSI_MIB_TYPE_UINT) {
			iface_stat->ac[i].ac = slsi_fapi_to_android_traffic_q(i);
			iface_stat->ac[i].retries = values[i].u.uintValue;
			iface_stat->ac[i].rx_mpdu = ndev_vif->rx_packets[i];
			iface_stat->ac[i].tx_mpdu = ndev_vif->tx_packets[i];
			iface_stat->ac[i].mpdu_lost = ndev_vif->tx_no_ack[i];
		} else {
			SLSI_WARN(sdev, "LLS: expected datatype 1 but received %d\n", values[i].type);
		}
	}

	if (values[4].type == SLSI_MIB_TYPE_UINT)
		iface_stat->beacon_rx = values[4].u.uintValue;

	if (values[5].type == SLSI_MIB_TYPE_UINT) {
		iface_stat->leaky_ap_detected = values[5].u.uintValue;
		iface_stat->leaky_ap_guard_time = 5; /* 5 milli sec. As mentioned in lls document */
	}

	if (values[6].type == SLSI_MIB_TYPE_INT)
		iface_stat->rssi_data = values[6].u.intValue;

exit:
	kfree(values);
	kfree(mibrsp.data);
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
}

void slsi_check_num_radios(struct slsi_dev *sdev)
{
	struct slsi_mib_data      mibrsp = { 0, NULL };
	struct slsi_mib_value     *values = NULL;
	struct slsi_mib_get_entry get_values[] = {{ SLSI_PSID_UNIFI_RADIO_SCAN_TIME, { 1, 0 } } };

	if (slsi_is_test_mode_enabled()) {
		SLSI_WARN(sdev, "not supported in WlanLite mode\n");
		return;
	}

	/* Expect each mib length in response is <= 15 So assume 15 bytes for each MIB */
	mibrsp.dataLength = 15 * ARRAY_SIZE(get_values);
	mibrsp.data = kmalloc(mibrsp.dataLength, GFP_KERNEL);
	if (!mibrsp.data) {
		SLSI_ERR(sdev, "Cannot kmalloc %d bytes\n", mibrsp.dataLength);
		sdev->lls_num_radio = 0;
		return;
	}

	values = slsi_read_mibs(sdev, NULL, get_values, ARRAY_SIZE(get_values), &mibrsp);
	if (!values) {
		sdev->lls_num_radio = 0;
	} else {
		sdev->lls_num_radio = values[0].type == SLSI_MIB_TYPE_NONE ? 1 : 2;
		kfree(values);
	}

	kfree(mibrsp.data);
}

static void slsi_lls_radio_stat_fill(struct slsi_dev *sdev, struct net_device *dev,
				     struct slsi_lls_radio_stat *radio_stat,
				     int max_chan_count, int radio_index, int twoorfive)
{
	struct slsi_mib_data      mibrsp = { 0, NULL };
	struct slsi_mib_data      supported_chan_mib = { 0, NULL };
	struct slsi_mib_value     *values = NULL;
	struct slsi_mib_get_entry get_values[] = {{ SLSI_PSID_UNIFI_RADIO_SCAN_TIME, { radio_index, 0 } },
						 { SLSI_PSID_UNIFI_RADIO_RX_TIME, { radio_index, 0 } },
						 { SLSI_PSID_UNIFI_RADIO_TX_TIME, { radio_index, 0 } },
						 { SLSI_PSID_UNIFI_RADIO_ON_TIME, { radio_index, 0 } },
						 { SLSI_PSID_UNIFI_SUPPORTED_CHANNELS, { 0, 0 } } };
	u32                       *radio_data[] = {&radio_stat->on_time_scan, &radio_stat->rx_time,
						   &radio_stat->tx_time, &radio_stat->on_time};
	int                       i, j, chan_count, chan_start, k;

	radio_stat->radio = radio_index;

	/* Expect each mib length in response is <= 15 So assume 15 bytes for each MIB */
	mibrsp.dataLength = 15 * sizeof(get_values) / sizeof(get_values[0]);
	mibrsp.data = kmalloc(mibrsp.dataLength, GFP_KERNEL);
	if (mibrsp.data == NULL) {
		SLSI_ERR(sdev, "Cannot kmalloc %d bytes\n", mibrsp.dataLength);
		return;
	}
	values = slsi_read_mibs(sdev, NULL, get_values, sizeof(get_values) / sizeof(get_values[0]), &mibrsp);
	if (!values)
		goto exit_with_mibrsp;

	for (i = 0; i < (sizeof(get_values) / sizeof(get_values[0])) - 1; i++) {
		if (values[i].type == SLSI_MIB_TYPE_UINT) {
			*radio_data[i] = values[i].u.uintValue;
		} else {
			SLSI_ERR(sdev, "invalid type. iter:%d", i);
		}
	}
	if (values[4].type != SLSI_MIB_TYPE_OCTET) {
		SLSI_ERR(sdev, "Supported_Chan invalid type.");
		goto exit_with_values;
	}

	supported_chan_mib = values[4].u.octetValue;
	for (j = 0; j < supported_chan_mib.dataLength / 2; j++) {
		struct slsi_lls_channel_info *radio_chan;

		chan_start = supported_chan_mib.data[j * 2];
		chan_count = supported_chan_mib.data[j * 2 + 1];
		if (radio_stat->num_channels + chan_count > max_chan_count)
			chan_count = max_chan_count - radio_stat->num_channels;
		if (chan_start == 1 && (twoorfive & BIT(0))) { /* for 2.4GHz */
			for (k = 0; k < chan_count; k++) {
				radio_chan = &radio_stat->channels[radio_stat->num_channels + k].channel;
				if (k + chan_start == 14)
					radio_chan->center_freq = 2484;
				else
					radio_chan->center_freq = 2407 + (chan_start + k) * 5;
				radio_chan->width = SLSI_LLS_CHAN_WIDTH_20;
			}
			radio_stat->num_channels += chan_count;
		} else if (chan_start != 1 && (twoorfive & BIT(1))) {
			/* for 5GHz */
			for (k = 0; k < chan_count; k++) {
				radio_chan = &radio_stat->channels[radio_stat->num_channels + k].channel;
				radio_chan->center_freq = 5000 + (chan_start + (k * 4)) * 5;
				radio_chan->width = SLSI_LLS_CHAN_WIDTH_20;
			}
			radio_stat->num_channels += chan_count;
		}
	}
exit_with_values:
	kfree(values);
exit_with_mibrsp:
	kfree(mibrsp.data);
}

static int slsi_lls_fill(struct slsi_dev *sdev, u8 **src_buf)
{
	struct net_device          *net_dev = NULL;
	struct slsi_lls_radio_stat *radio_stat;
	struct slsi_lls_radio_stat *radio_stat_temp;
	struct slsi_lls_iface_stat *iface_stat;
	int                        buf_len = 0;
	int                        max_chan_count = 0;
	u8                         *buf;
	int                        num_of_radios_supported;
	int i = 0;
	int radio_type[2] = {BIT(0), BIT(1)};

	if (sdev->lls_num_radio == 0) {
		slsi_check_num_radios(sdev);
		if (sdev->lls_num_radio == 0)
			return -EIO;
	}

	num_of_radios_supported = sdev->lls_num_radio;
	net_dev = slsi_get_netdev(sdev, SLSI_NET_INDEX_WLAN);

	if (sdev->wiphy->bands[NL80211_BAND_2GHZ])
		max_chan_count = sdev->wiphy->bands[NL80211_BAND_2GHZ]->n_channels;
	if (sdev->wiphy->bands[NL80211_BAND_5GHZ])
		max_chan_count += sdev->wiphy->bands[NL80211_BAND_5GHZ]->n_channels;
	buf_len = (int)((num_of_radios_supported * sizeof(struct slsi_lls_radio_stat))
			+ sizeof(struct slsi_lls_iface_stat)
			+ sizeof(u8)

			+ (sizeof(struct slsi_lls_peer_info) * SLSI_ADHOC_PEER_CONNECTIONS_MAX)
			+ (sizeof(struct slsi_lls_channel_stat) * max_chan_count));
	buf = kzalloc(buf_len, GFP_KERNEL);
	if (!buf) {
		SLSI_ERR(sdev, "No mem. Size:%d\n", buf_len);
		return -ENOMEM;
	}
	buf[0] = num_of_radios_supported;
	*src_buf = buf;
	iface_stat = (struct slsi_lls_iface_stat *)(buf + sizeof(u8));
	slsi_lls_iface_stat_fill(sdev, net_dev, iface_stat);

	radio_stat = (struct slsi_lls_radio_stat *)(buf + sizeof(u8) + sizeof(struct slsi_lls_iface_stat) +
		     (sizeof(struct slsi_lls_peer_info) * iface_stat->num_peers));
	radio_stat_temp = radio_stat;
	if (num_of_radios_supported == 1) {
		radio_type[0] = BIT(0) | BIT(1);
		slsi_lls_radio_stat_fill(sdev, net_dev, radio_stat, max_chan_count, 0, radio_type[0]);
		radio_stat = (struct slsi_lls_radio_stat *)((u8 *)radio_stat + sizeof(struct slsi_lls_radio_stat) +
				     (sizeof(struct slsi_lls_channel_stat) * radio_stat->num_channels));
	} else {
		for (i = 1; i <= num_of_radios_supported ; i++) {
			slsi_lls_radio_stat_fill(sdev, net_dev, radio_stat, max_chan_count, i, radio_type[i - 1]);
			radio_stat = (struct slsi_lls_radio_stat *)((u8 *)radio_stat +
					     sizeof(struct slsi_lls_radio_stat) + (sizeof(struct slsi_lls_channel_stat)
					     * radio_stat->num_channels));
		}
	}
#ifdef CONFIG_SCSC_WLAN_DEBUG
	if (slsi_dev_llslogs_supported())
		slsi_lls_debug_dump_stats(sdev, radio_stat_temp, iface_stat, buf, buf_len, num_of_radios_supported);
#endif
	return buf_len;
}

static int slsi_lls_get_stats(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev        *sdev = SDEV_FROM_WIPHY(wiphy);
	int                    ret;
	u8                     *buf = NULL;
	int                    buf_len;

	if (!slsi_dev_lls_supported())
		return -EOPNOTSUPP;

	if (slsi_is_test_mode_enabled()) {
		SLSI_WARN(sdev, "not supported in WlanLite mode\n");
		return -EOPNOTSUPP;
	}

	if(!sdev) {
		SLSI_ERR(sdev, "sdev is Null\n");
		return -EINVAL;
	}

	if (!sdev) {
		SLSI_ERR(sdev, "sdev is Null\n");
		return -EINVAL;
	}

	SLSI_MUTEX_LOCK(sdev->device_config_mutex);
	/* In case of lower layer failure do not read LLS MIBs */
	if (sdev->mlme_blocked)
		buf_len = -EIO;
	else
		buf_len = slsi_lls_fill(sdev, &buf);
	SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);

	if (buf_len > 0) {
		ret = slsi_vendor_cmd_reply(wiphy, buf, buf_len);
		if (ret)
			SLSI_ERR_NODEV("vendor cmd reply failed (err:%d)\n", ret);
	} else {
		ret = buf_len;
	}
	kfree(buf);
	return ret;
}

static int slsi_gscan_set_oui(struct wiphy *wiphy,
			      struct wireless_dev *wdev, const void *data, int len)
{
	int ret = 0;

#ifdef CONFIG_SCSC_WLAN_ENABLE_MAC_RANDOMISATION

	struct slsi_dev          *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = wdev->netdev;
	struct netdev_vif *ndev_vif;
	int                      temp;
	int                      type;
	const struct nlattr      *attr;
	u8 scan_oui[6];

	memset(&scan_oui, 0, 6);

	if (!dev) {
		SLSI_ERR(sdev, "dev is NULL!!\n");
		return -EINVAL;
	}

	ndev_vif = netdev_priv(dev);
	SLSI_MUTEX_LOCK(ndev_vif->scan_mutex);
	sdev->scan_addr_set = 0;

	nla_for_each_attr(attr, data, len, temp) {
		type = nla_type(attr);
		switch (type) {
		case SLSI_NL_ATTRIBUTE_PNO_RANDOM_MAC_OUI:
		{
			memcpy(&scan_oui, nla_data(attr), 3);
			break;
		}
		default:
			ret = -EINVAL;
			SLSI_ERR(sdev, "Invalid type : %d\n", type);
			break;
		}
	}

	memcpy(sdev->scan_mac_addr, scan_oui, 6);
	sdev->scan_addr_set = 1;

	SLSI_MUTEX_UNLOCK(ndev_vif->scan_mutex);
#endif
	return ret;
}

static int slsi_get_feature_set(struct wiphy *wiphy,
				struct wireless_dev *wdev, const void *data, int len)
{
	u32 feature_set = 0;
	int ret = 0;

	SLSI_DBG3_NODEV(SLSI_GSCAN, "\n");

	feature_set |= SLSI_WIFI_HAL_FEATURE_RSSI_MONITOR;
	feature_set |= SLSI_WIFI_HAL_FEATURE_CONTROL_ROAMING;
#ifndef CONFIG_SCSC_WLAN_NAT_KEEPALIVE_DISABLE
	feature_set |= SLSI_WIFI_HAL_FEATURE_MKEEP_ALIVE;
#endif
#ifdef CONFIG_SCSC_WLAN_ENHANCED_LOGGING
		feature_set |= SLSI_WIFI_HAL_FEATURE_LOGGER;
#endif
	if (slsi_dev_gscan_supported())
		feature_set |= SLSI_WIFI_HAL_FEATURE_GSCAN;
	if (slsi_dev_lls_supported())
		feature_set |= SLSI_WIFI_HAL_FEATURE_LINK_LAYER_STATS;
	if (slsi_dev_epno_supported())
		feature_set |= SLSI_WIFI_HAL_FEATURE_HAL_EPNO;
	if (slsi_dev_nan_supported(SDEV_FROM_WIPHY(wiphy)))
		feature_set |= SLSI_WIFI_HAL_FEATURE_NAN;
	if (slsi_dev_rtt_supported()) {
		feature_set |= SLSI_WIFI_HAL_FEATURE_D2D_RTT;
		feature_set |= SLSI_WIFI_HAL_FEATURE_D2AP_RTT;
	}

	ret = slsi_vendor_cmd_reply(wiphy, &feature_set, sizeof(feature_set));

	return ret;
}

static int slsi_set_country_code(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev          *sdev = SDEV_FROM_WIPHY(wiphy);
	int                      ret = 0;
	int                      temp;
	int                      type;
	const struct nlattr      *attr;
	char country_code[SLSI_COUNTRY_CODE_LEN];

	SLSI_DBG3(sdev, SLSI_GSCAN, "Received country code command\n");

	nla_for_each_attr(attr, data, len, temp) {
		type = nla_type(attr);
		switch (type) {
		case SLSI_NL_ATTRIBUTE_COUNTRY_CODE:
		{
			if (nla_len(attr) < (SLSI_COUNTRY_CODE_LEN - 1)) {
				ret = -EINVAL;
				SLSI_ERR(sdev, "Insufficient Country Code Length : %d\n", nla_len(attr));
				return ret;
			}
			memcpy(country_code, nla_data(attr), (SLSI_COUNTRY_CODE_LEN - 1));
			break;
		}
		default:
			ret = -EINVAL;
			SLSI_ERR(sdev, "Invalid type : %d\n", type);
			return ret;
		}
	}
	ret = slsi_set_country_update_regd(sdev, country_code, SLSI_COUNTRY_CODE_LEN);
	if (ret < 0)
		SLSI_ERR(sdev, "Set country failed ret:%d\n", ret);
	return ret;
}

static int slsi_rtt_get_capabilities(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_rtt_capabilities rtt_cap;
	int                               ret = 0;
	struct slsi_dev                   *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = wdev->netdev;

	SLSI_DBG1_NODEV(SLSI_GSCAN, "SUBCMD_GET_RTT_CAPABILITIES\n");
	if (!slsi_dev_rtt_supported()) {
		SLSI_WARN(sdev, "RTT not supported.\n");
		return -ENOTSUPP;
	}
	memset(&rtt_cap, 0, sizeof(struct slsi_rtt_capabilities));

	ret = slsi_mib_get_rtt_cap(sdev, dev, &rtt_cap);
	if (ret != 0) {
		SLSI_ERR(sdev, "Failed to read mib\n");
		return ret;
	}
	ret = slsi_vendor_cmd_reply(wiphy, &rtt_cap, sizeof(struct slsi_rtt_capabilities));
	if (ret)
		SLSI_ERR_NODEV("rtt_get_capabilities vendor cmd reply failed (err = %d)\n", ret);
	return ret;
}

static int slsi_rtt_set_config(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	int r, type, j = 0;
	struct slsi_dev            *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif;
	struct slsi_rtt_config *nl_rtt_params;
	const struct nlattr *iter, *outer, *inner;
	int tmp, tmp1, tmp2;
	u16 rtt_id = 0;
	u8 num_devices = 0;
	u16 rtt_peer = SLSI_RTT_PEER_AP;
	u16 vif_idx = 0;
	u16 center_freq0 = 0, center_freq1 = 0, channel_freq = 0, width = 0;

	SLSI_DBG1_NODEV(SLSI_GSCAN, "SUBCMD_RTT_RANGE_START\n");
	if (!slsi_dev_rtt_supported()) {
		SLSI_ERR(sdev, "RTT not supported.\n");
		return WIFI_HAL_ERROR_NOT_SUPPORTED;
	}
	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		switch (type) {
		case SLSI_RTT_ATTRIBUTE_TARGET_CNT:
			num_devices = nla_get_u8(iter);
			SLSI_DBG1_NODEV(SLSI_GSCAN, "Target cnt %d\n", num_devices);
			break;
		case SLSI_RTT_ATTRIBUTE_TARGET_ID:
			rtt_id = nla_get_u16(iter);
			SLSI_DBG1_NODEV(SLSI_GSCAN, "Target id %d\n", rtt_id);
			break;
		default:
			SLSI_ERR_NODEV("Unexpected RTT attribute:type - %d\n", type);
			break;
		}
	}
	if (!num_devices) {
		SLSI_ERR_NODEV("No device found for rtt configuration!\n");
		return -EINVAL;
	}
	/* Allocate memory for the received config params */
	nl_rtt_params = kcalloc(num_devices, sizeof(*nl_rtt_params), GFP_KERNEL);
	if (!nl_rtt_params) {
		SLSI_ERR_NODEV("Failed for allocate memory for config rtt_param\n");
		return -ENOMEM;
	}
	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		switch (type) {
		case SLSI_RTT_ATTRIBUTE_TARGET_INFO:
			nla_for_each_nested(outer, iter, tmp1) {
				nla_for_each_nested(inner, outer, tmp2) {
					switch (nla_type(inner)) {
					case SLSI_RTT_ATTRIBUTE_TARGET_MAC:
						memcpy(nl_rtt_params[j].peer_addr, nla_data(inner), ETH_ALEN);
						break;
					case SLSI_RTT_ATTRIBUTE_TARGET_TYPE:
						nl_rtt_params[j].type = nla_get_u16(inner);
						break;
					case SLSI_RTT_ATTRIBUTE_TARGET_PEER:
						rtt_peer = nla_get_u16(inner);
						break;
					case SLSI_RTT_ATTRIBUTE_TARGET_CHAN_FREQ:
						channel_freq = nla_get_u16(inner);
						nl_rtt_params[j].channel_freq = channel_freq * 2;
						break;
					case SLSI_RTT_ATTRIBUTE_TARGET_CHAN_WIDTH:
						width = nla_get_u16(inner);
						break;
					case SLSI_RTT_ATTRIBUTE_TARGET_CHAN_FREQ0:
						center_freq0 = nla_get_u16(inner);
						break;
					case SLSI_RTT_ATTRIBUTE_TARGET_CHAN_FREQ1:
						center_freq1 = nla_get_u16(inner);
						break;
					case SLSI_RTT_ATTRIBUTE_TARGET_PERIOD:
						nl_rtt_params[j].burst_period = nla_get_u8(inner);
						break;
					case SLSI_RTT_ATTRIBUTE_TARGET_NUM_BURST:
						nl_rtt_params[j].num_burst = nla_get_u8(inner);
						break;
					case SLSI_RTT_ATTRIBUTE_TARGET_NUM_FTM_BURST:
						nl_rtt_params[j].num_frames_per_burst = nla_get_u8(inner);
						break;
					case SLSI_RTT_ATTRIBUTE_TARGET_NUM_RETRY_FTMR:
						nl_rtt_params[j].num_retries_per_ftmr = nla_get_u8(inner);
						break;
					case SLSI_RTT_ATTRIBUTE_TARGET_BURST_DURATION:
						nl_rtt_params[j].burst_duration = nla_get_u8(inner);
						break;
					case SLSI_RTT_ATTRIBUTE_TARGET_PREAMBLE:
						nl_rtt_params[j].preamble = nla_get_u16(inner);
						break;
					case SLSI_RTT_ATTRIBUTE_TARGET_BW:
						nl_rtt_params[j].bw = nla_get_u16(inner);
						break;
					case SLSI_RTT_ATTRIBUTE_TARGET_LCI:
						nl_rtt_params[j].LCI_request = nla_get_u16(inner);
						break;
					case SLSI_RTT_ATTRIBUTE_TARGET_LCR:
						nl_rtt_params[j].LCR_request = nla_get_u16(inner);
						break;
					default:
						SLSI_ERR_NODEV("Unknown RTT INFO ATTRIBUTE type: %d\n", type);
						break;
					}
					if (rtt_peer == SLSI_RTT_PEER_NAN) {
#if CONFIG_SCSC_WLAN_MAX_INTERFACES >= 4
						SLSI_ETHER_COPY(nl_rtt_params[j].source_addr,
								sdev->netdev_addresses[SLSI_NET_INDEX_NAN]);
#else
						SLSI_ERR(sdev, "NAN not supported(mib:%d)\n", sdev->nan_enabled);
#endif
					} else {
						SLSI_ETHER_COPY(nl_rtt_params[j].source_addr,
								sdev->netdev_addresses[SLSI_NET_INDEX_WLAN]);
					}
				}
				/* width+1:to match RTT width enum value with NL enums */
				nl_rtt_params[j].channel_info = slsi_compute_chann_info(sdev, width + 1, center_freq0,
											channel_freq);
				j++;
			}
			break;
		default:
			SLSI_ERR_NODEV("No ATTRIBUTE_Target cnt - %d\n", type);
			break;
		}
	}
	if (rtt_peer == SLSI_RTT_PEER_AP) {
		vif_idx = 0;
	} else if (rtt_peer == SLSI_RTT_PEER_NAN) {
		if (!slsi_dev_nan_supported(sdev)) {
			SLSI_ERR(sdev, "NAN not supported(mib:%d)\n", sdev->nan_enabled);
			kfree(nl_rtt_params);
			return WIFI_HAL_ERROR_NOT_SUPPORTED;
		}
		ndev_vif = netdev_priv(dev);
		if (ndev_vif->activated) {
			vif_idx = ndev_vif->vif_type;
		} else {
			SLSI_ERR(sdev, "NAN vif not activated\n");
			kfree(nl_rtt_params);
			return -EINVAL;
		}
	}
	r = slsi_mlme_add_range_req(sdev, num_devices, nl_rtt_params, rtt_id, vif_idx);
	if (r) {
		r = -EINVAL;
		SLSI_ERR_NODEV("Failed to set rtt config\n");
	} else {
		sdev->rtt_vif[rtt_id] = vif_idx;
		SLSI_DBG1_NODEV(SLSI_GSCAN, "Successfully set rtt config\n");
	}
	kfree(nl_rtt_params);
	return r;
}

int slsi_tx_rate_calc(struct sk_buff *nl_skb, u16 fw_rate, int res, bool tx_rate)
{
	u8 preamble;
	const u32 fw_rate_idx_to_80211_rate[] = { 0, 10, 20, 55, 60, 90, 110, 120, 180, 240, 360, 480, 540 };
	u32 data_rate = 0;
	u32 mcs = 0, nss = 0;
	u32 chan_bw_idx = 0;
	int gi_idx;

	preamble = (fw_rate & SLSI_FW_API_RATE_HT_SELECTOR_FIELD) >> 14;
	if ((fw_rate & SLSI_FW_API_RATE_HT_SELECTOR_FIELD) == SLSI_FW_API_RATE_NON_HT_SELECTED) {
		u16 fw_rate_idx = fw_rate & SLSI_FW_API_RATE_INDEX_FIELD;

		if (fw_rate > 0 && fw_rate_idx < ARRAY_SIZE(fw_rate_idx_to_80211_rate))
			data_rate = fw_rate_idx_to_80211_rate[fw_rate_idx];
	} else if ((fw_rate & SLSI_FW_API_RATE_HT_SELECTOR_FIELD) == SLSI_FW_API_RATE_HT_SELECTED) {
		nss = (SLSI_FW_API_RATE_HT_NSS_FIELD & fw_rate) >> 6;
		chan_bw_idx = (fw_rate & SLSI_FW_API_RATE_BW_FIELD) >> 9;
		gi_idx = ((fw_rate & SLSI_FW_API_RATE_SGI) == SLSI_FW_API_RATE_SGI) ? 1 : 0;
		mcs = SLSI_FW_API_RATE_HT_MCS_FIELD & fw_rate;
		if ((chan_bw_idx < 2) && (mcs <= 7)) {
			data_rate = (nss + 1) * slsi_rates_table[chan_bw_idx][gi_idx][mcs];
		} else if (mcs == 32 && chan_bw_idx == 1) {
			if (gi_idx == 1)
				data_rate = (nss + 1) * 67;
			else
				data_rate = (nss + 1) * 60;
		} else {
			SLSI_WARN_NODEV("FW DATA RATE decode error fw_rate:%x, bw:%x, mcs_idx:%x, nss : %d\n",
					fw_rate, chan_bw_idx, mcs, nss);
		}
	} else if ((fw_rate & SLSI_FW_API_RATE_HT_SELECTOR_FIELD) == SLSI_FW_API_RATE_VHT_SELECTED) {
		/* report vht rate in legacy units and not as mcs index. reason: upper layers may still be not
		 * updated with vht msc table.
		 */
		chan_bw_idx = (fw_rate & SLSI_FW_API_RATE_BW_FIELD) >> 9;
		gi_idx = ((fw_rate & SLSI_FW_API_RATE_SGI) == SLSI_FW_API_RATE_SGI) ? 1 : 0;
		/* Calculate  NSS --> bits 6 to 4*/
		nss = (SLSI_FW_API_RATE_VHT_NSS_FIELD & fw_rate) >> 4;
		mcs = SLSI_FW_API_RATE_VHT_MCS_FIELD & fw_rate;
		/* Bandwidth (BW): 0x0= 20 MHz, 0x1= 40 MHz, 0x2= 80 MHz, 0x3= 160/ 80+80 MHz. 0x3 is not supported */
		if ((chan_bw_idx <= 2) && (mcs <= 9))
			data_rate = (nss + 1) * slsi_rates_table[chan_bw_idx][gi_idx][mcs];
		else
			SLSI_WARN_NODEV("FW DATA RATE decode error fw_rate:%x, bw:%x, mcs_idx:%x,nss : %d\n",
					fw_rate, chan_bw_idx, mcs, nss);
		if (nss > 1)
			nss += 1;
	}

	if (tx_rate) {
		res |= nla_put_u32(nl_skb, SLSI_RTT_EVENT_ATTR_TX_PREAMBLE, preamble);
		res |= nla_put_u32(nl_skb, SLSI_RTT_EVENT_ATTR_TX_NSS, nss);
		res |= nla_put_u32(nl_skb, SLSI_RTT_EVENT_ATTR_TX_BW, chan_bw_idx);
		res |= nla_put_u32(nl_skb, SLSI_RTT_EVENT_ATTR_TX_MCS, mcs);
		res |= nla_put_u32(nl_skb, SLSI_RTT_EVENT_ATTR_TX_RATE, data_rate);
	} else {
		res |= nla_put_u32(nl_skb, SLSI_RTT_EVENT_ATTR_RX_PREAMBLE, preamble);
		res |= nla_put_u32(nl_skb, SLSI_RTT_EVENT_ATTR_RX_NSS, nss);
		res |= nla_put_u32(nl_skb, SLSI_RTT_EVENT_ATTR_RX_BW, chan_bw_idx);
		res |= nla_put_u32(nl_skb, SLSI_RTT_EVENT_ATTR_RX_MCS, mcs);
		res |= nla_put_u32(nl_skb, SLSI_RTT_EVENT_ATTR_RX_RATE, data_rate);
	}
	return res;
}

void slsi_rx_range_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	u32 i, tm;
	u16 rtt_entry_count = fapi_get_u16(skb, u.mlme_range_ind.entries);
	u16 rtt_id = fapi_get_u16(skb, u.mlme_range_ind.rtt_id);
	u32 tmac = fapi_get_u32(skb, u.mlme_range_ind.spare_3);
	int data_len = fapi_get_datalen(skb);
	u8                *ip_ptr, *start_ptr;
	u16 tx_data, rx_data;
	struct sk_buff *nl_skb;
	int res = 0;
	struct nlattr *nlattr_nested;
	struct timespec          ts;
	u64 tkernel;
	u8 rep_cnt = 0;

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NULL, NLMSG_DEFAULT_SIZE,
					     SLSI_NL80211_RTT_RESULT_EVENT, GFP_KERNEL);
#else
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NLMSG_DEFAULT_SIZE, SLSI_NL80211_RTT_RESULT_EVENT,
					     GFP_KERNEL);
#endif
#ifdef CONFIG_SCSC_WLAN_DEBUG
	SLSI_DBG1_NODEV(SLSI_GSCAN, "Event: %s(%d)\n",
			slsi_print_event_name(SLSI_NL80211_RTT_RESULT_EVENT), SLSI_NL80211_RTT_RESULT_EVENT);
#endif

	if (!nl_skb) {
		SLSI_ERR(sdev, "NO MEM for nl_skb!!!\n");
		goto exit;
		}

	ip_ptr = fapi_get_data(skb);
	start_ptr = fapi_get_data(skb);
	res |= nla_put_u16(nl_skb, SLSI_RTT_ATTRIBUTE_RESULT_CNT, rtt_entry_count);
	res |= nla_put_u16(nl_skb, SLSI_RTT_ATTRIBUTE_TARGET_ID, rtt_id);
	res |= nla_put_u8(nl_skb, SLSI_RTT_ATTRIBUTE_RESULTS_PER_TARGET, 1);
	for (i = 0; i < rtt_entry_count; i++) {
		nlattr_nested = nla_nest_start(nl_skb, SLSI_RTT_ATTRIBUTE_RESULT);
		if (!nlattr_nested) {
			SLSI_ERR(sdev, "Error in nla_nest_start\n");
			/* Dont use slsi skb wrapper for this free */
			kfree_skb(nl_skb);
			goto exit;
		}
		ip_ptr += 7;             /*skip first 7 bytes for fapi_ie_generic */
		res |= nla_put(nl_skb, SLSI_RTT_EVENT_ATTR_ADDR, ETH_ALEN, ip_ptr);
		ip_ptr += 6;
		res |= nla_put_u8(nl_skb, SLSI_RTT_EVENT_ATTR_BURST_NUM, *ip_ptr++);
		res |= nla_put_u8(nl_skb, SLSI_RTT_EVENT_ATTR_MEASUREMENT_NUM, *ip_ptr++);
		res |= nla_put_u8(nl_skb, SLSI_RTT_EVENT_ATTR_SUCCESS_NUM, *ip_ptr++);
		res |= nla_put_u8(nl_skb, SLSI_RTT_EVENT_ATTR_NUM_PER_BURST_PEER, *ip_ptr++);
		res |= nla_put_u16(nl_skb, SLSI_RTT_EVENT_ATTR_STATUS, *ip_ptr);
		ip_ptr += 2;
		res |= nla_put_u8(nl_skb, SLSI_RTT_EVENT_ATTR_RETRY_AFTER_DURATION, *ip_ptr++);
		res |= nla_put_u16(nl_skb, SLSI_RTT_EVENT_ATTR_TYPE, *ip_ptr);
		ip_ptr += 2;
		res |= nla_put_u16(nl_skb, SLSI_RTT_EVENT_ATTR_RSSI, *ip_ptr);
		ip_ptr += 2;
		res |= nla_put_u16(nl_skb, SLSI_RTT_EVENT_ATTR_RSSI_SPREAD, *ip_ptr);
		ip_ptr += 2;
		memcpy(&tx_data, ip_ptr, 2);
		res = slsi_tx_rate_calc(nl_skb, tx_data, res, 1);
		ip_ptr += 2;
		memcpy(&rx_data, ip_ptr, 2);
		res = slsi_tx_rate_calc(nl_skb, rx_data, res, 0);
		ip_ptr += 2;
		res |= nla_put_u32(nl_skb, SLSI_RTT_EVENT_ATTR_RTT, *ip_ptr);
		ip_ptr += 4;
		res |= nla_put_u16(nl_skb, SLSI_RTT_EVENT_ATTR_RTT_SD, *ip_ptr);
		ip_ptr += 2;
		res |= nla_put_u16(nl_skb, SLSI_RTT_EVENT_ATTR_RTT_SPREAD, *ip_ptr);
		ip_ptr += 2;
		get_monotonic_boottime(&ts);
		tkernel = (u64)TIMESPEC_TO_US(ts);
		tm = *ip_ptr;
		res |= nla_put_u32(nl_skb, SLSI_RTT_EVENT_ATTR_TIMESTAMP_US, tkernel - (tmac - tm));
		ip_ptr += 4;
		res |= nla_put_u16(nl_skb, SLSI_RTT_EVENT_ATTR_BURST_DURATION_MSN, *ip_ptr);
		ip_ptr += 2;
		res |= nla_put_u8(nl_skb, SLSI_RTT_EVENT_ATTR_NEGOTIATED_BURST_NUM, *ip_ptr++);
		for (rep_cnt = 0; rep_cnt < 2; rep_cnt++) {
			if (ip_ptr - start_ptr < data_len && ip_ptr[0] == WLAN_EID_MEASURE_REPORT) {
				if (ip_ptr[4] == 8)  /*LCI Element*/
					res |= nla_put(nl_skb, SLSI_RTT_EVENT_ATTR_LCI,
						       ip_ptr[1] + 2, ip_ptr);
				else if (ip_ptr[4] == 11)   /*LCR element */
					res |= nla_put(nl_skb, SLSI_RTT_EVENT_ATTR_LCR,
						       ip_ptr[1] + 2, ip_ptr);
				ip_ptr += ip_ptr[1] + 2;
			}
		}
		nla_nest_end(nl_skb, nlattr_nested);
	}
	SLSI_DBG_HEX(sdev, SLSI_GSCAN, fapi_get_data(skb), fapi_get_datalen(skb), "range indication skb buffer:\n");
	if (res) {
		SLSI_ERR(sdev, "Error in nla_put*:%x\n", res);
		kfree_skb(nl_skb);
		goto exit;
	}
	cfg80211_vendor_event(nl_skb, GFP_KERNEL);
exit:
	slsi_kfree_skb(skb);
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
}

void slsi_rx_range_done_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	u16 rtt_id = fapi_get_u16(skb, u.mlme_range_ind.rtt_id);
	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
#ifdef CONFIG_SCSC_WLAN_DEBUG
	SLSI_DBG1_NODEV(SLSI_GSCAN, "Event: %s(%d)\n",
			slsi_print_event_name(SLSI_NL80211_RTT_COMPLETE_EVENT), SLSI_NL80211_RTT_COMPLETE_EVENT);
#endif
	slsi_vendor_event(sdev, SLSI_NL80211_RTT_COMPLETE_EVENT, &rtt_id, sizeof(rtt_id));
	slsi_kfree_skb(skb);
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
}

static int slsi_rtt_cancel_config(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	int temp, ret, r = 1, j = 0, type;
	struct slsi_dev            *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = wdev->netdev;
	u8 *addr;
	const struct nlattr *iter;
	u16  num_devices = 0, rtt_id = 0;

	SLSI_DBG1_NODEV(SLSI_GSCAN, "RTT_SUBCMD_CANCEL_CONFIG\n");
	if (!slsi_dev_rtt_supported()) {
		SLSI_WARN(sdev, "RTT not supported.\n");
		return -ENOTSUPP;
	}
	nla_for_each_attr(iter, data, len, temp) {
		type = nla_type(iter);
		switch (type) {
		case SLSI_RTT_ATTRIBUTE_TARGET_CNT:
			num_devices = nla_get_u16(iter);
			SLSI_DBG1_NODEV(SLSI_GSCAN, "Target cnt %d\n", num_devices);
			break;
		case SLSI_RTT_ATTRIBUTE_TARGET_ID:
			rtt_id = nla_get_u16(iter);
			SLSI_DBG1_NODEV(SLSI_GSCAN, "Target id %d\n", rtt_id);
			break;
		default:
			SLSI_ERR_NODEV("No ATTRIBUTE_Target cnt - %d\n", type);
			break;
		}
	}
	/* Allocate memory for the received mac addresses */
	if (num_devices) {
		addr = kzalloc(ETH_ALEN * num_devices, GFP_KERNEL);
		if (!addr) {
			SLSI_ERR_NODEV("Failed for allocate memory for mac addresses\n");
			ret = -ENOMEM;
			return ret;
		}
		nla_for_each_attr(iter, data, len, temp) {
			type = nla_type(iter);
			if (type == SLSI_RTT_ATTRIBUTE_TARGET_MAC) {
				memcpy(&addr[j], nla_data(iter), ETH_ALEN);
				j++;
			} else {
				SLSI_ERR_NODEV("No ATTRIBUTE_MAC - %d\n", type);
			}
		}

		r = slsi_mlme_del_range_req(sdev, dev, num_devices, addr, rtt_id);
		kfree(addr);
	}
	if (r)
		SLSI_ERR_NODEV("Failed to cancel rtt config\n");
	return r;
}

static int slsi_nan_get_new_id(u32 id_map, int max_ids)
{
	int i;

	for (i = 1; i <= max_ids; i++) {
		if (!(id_map & BIT(i)))
			return i;
	}
	return 0;
}

static int slsi_nan_get_new_publish_id(struct netdev_vif *ndev_vif)
{
	return slsi_nan_get_new_id(ndev_vif->nan.publish_id_map, SLSI_NAN_MAX_PUBLISH_ID);
}

static int slsi_nan_get_new_subscribe_id(struct netdev_vif *ndev_vif)
{
	return slsi_nan_get_new_id(ndev_vif->nan.subscribe_id_map, SLSI_NAN_MAX_SUBSCRIBE_ID);
}

static bool slsi_nan_is_publish_id_active(struct netdev_vif *ndev_vif, u32 id)
{
	return ndev_vif->nan.publish_id_map & BIT(id);
}

static bool slsi_nan_is_subscribe_id_active(struct netdev_vif *ndev_vif, u32 id)
{
	return ndev_vif->nan.subscribe_id_map & BIT(id);
}

static void slsi_vendor_nan_command_reply(struct wiphy *wiphy, u32 status, u32 error, u32 response_type,
					  u16 publish_subscribe_id, struct slsi_hal_nan_capabilities *capabilities)
{
	int reply_len;
	struct sk_buff  *reply;

	reply_len = SLSI_NL_VENDOR_REPLY_OVERHEAD + SLSI_NL_ATTRIBUTE_U32_LEN *
		    (3 + sizeof(struct slsi_hal_nan_capabilities));
	reply = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, reply_len);
	if (!reply) {
		SLSI_WARN_NODEV("SKB alloc failed for vendor_cmd reply\n");
		return;
	}

	nla_put_u32(reply, NAN_REPLY_ATTR_STATUS_TYPE, status);
	nla_put_u32(reply, NAN_REPLY_ATTR_VALUE, error);
	nla_put_u32(reply, NAN_REPLY_ATTR_RESPONSE_TYPE, response_type);

	if (capabilities) {
		nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_CONCURRENT_CLUSTER,
			    capabilities->max_concurrent_nan_clusters);
		nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_PUBLISHES, capabilities->max_publishes);
		nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_SUBSCRIBES, capabilities->max_subscribes);
		nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_SERVICE_NAME_LEN, capabilities->max_service_name_len);
		nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_MATCH_FILTER_LEN, capabilities->max_match_filter_len);
		nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_TOTAL_MATCH_FILTER_LEN,
			    capabilities->max_total_match_filter_len);
		nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_SERVICE_SPECIFIC_INFO_LEN,
			    capabilities->max_service_specific_info_len);
		nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_VSA_DATA_LEN, capabilities->max_vsa_data_len);
		nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_MESH_DATA_LEN, capabilities->max_mesh_data_len);
		nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_NDI_INTERFACES, capabilities->max_ndi_interfaces);
		nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_NDP_SESSIONS, capabilities->max_ndp_sessions);
		nla_put_u32(reply, NAN_REPLY_ATTR_CAP_MAX_APP_INFO_LEN, capabilities->max_app_info_len);
	} else if (publish_subscribe_id) {
		nla_put_u16(reply, NAN_REPLY_ATTR_PUBLISH_SUBSCRIBE_TYPE, publish_subscribe_id);
	}

	if (cfg80211_vendor_cmd_reply(reply))
		SLSI_ERR_NODEV("FAILED to reply nan coammnd. response_type:%d\n", response_type);
}

static int slsi_nan_enable_get_nl_params(struct slsi_dev *sdev, struct slsi_hal_nan_enable_req *hal_req,
					 const void *data, int len)
{
	int type, tmp;
	const struct nlattr *iter;

	memset(hal_req, 0, sizeof(*hal_req));
	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		switch (type) {
		case NAN_REQ_ATTR_MASTER_PREF:
			hal_req->master_pref = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_CLUSTER_LOW:
			hal_req->cluster_low = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_CLUSTER_HIGH:
			hal_req->cluster_high = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_SUPPORT_5G_VAL:
			hal_req->support_5g_val = nla_get_u8(iter);
			hal_req->config_support_5g = 1;
			break;

		case NAN_REQ_ATTR_SID_BEACON_VAL:
			hal_req->sid_beacon_val = nla_get_u8(iter);
			hal_req->config_sid_beacon = 1;
			break;

		case NAN_REQ_ATTR_RSSI_CLOSE_2G4_VAL:
			hal_req->rssi_close_2dot4g_val = nla_get_u8(iter);
			hal_req->config_2dot4g_rssi_close = 1;
			break;

		case NAN_REQ_ATTR_RSSI_MIDDLE_2G4_VAL:
			hal_req->rssi_middle_2dot4g_val =  nla_get_u8(iter);
			hal_req->config_2dot4g_rssi_middle = 1;
			break;

		case NAN_REQ_ATTR_RSSI_PROXIMITY_2G4_VAL:
			hal_req->rssi_proximity_2dot4g_val = nla_get_u8(iter);
			hal_req->rssi_proximity_2dot4g_val = 1;
			break;

		case NAN_REQ_ATTR_HOP_COUNT_LIMIT_VAL:
			hal_req->hop_count_limit_val = nla_get_u8(iter);
			hal_req->config_hop_count_limit = 1;
			break;

		case NAN_REQ_ATTR_SUPPORT_2G4_VAL:
			hal_req->support_2dot4g_val = nla_get_u8(iter);
			hal_req->config_2dot4g_support = 1;
			break;

		case NAN_REQ_ATTR_BEACONS_2G4_VAL:
			hal_req->beacon_2dot4g_val = nla_get_u8(iter);
			hal_req->config_2dot4g_beacons = 1;
			break;

		case NAN_REQ_ATTR_SDF_2G4_VAL:
			hal_req->sdf_2dot4g_val = nla_get_u8(iter);
			hal_req->config_2dot4g_sdf = 1;
			break;

		case NAN_REQ_ATTR_BEACON_5G_VAL:
			hal_req->beacon_5g_val = nla_get_u8(iter);
			hal_req->config_5g_beacons = 1;
			break;

		case NAN_REQ_ATTR_SDF_5G_VAL:
			hal_req->sdf_5g_val = nla_get_u8(iter);
			hal_req->config_5g_sdf = 1;
			break;

		case NAN_REQ_ATTR_RSSI_CLOSE_5G_VAL:
			hal_req->rssi_close_5g_val = nla_get_u8(iter);
			hal_req->config_5g_rssi_close = 1;
			break;

		case NAN_REQ_ATTR_RSSI_MIDDLE_5G_VAL:
			hal_req->rssi_middle_5g_val = nla_get_u8(iter);
			hal_req->config_5g_rssi_middle = 1;
			break;

		case NAN_REQ_ATTR_RSSI_CLOSE_PROXIMITY_5G_VAL:
			hal_req->rssi_close_proximity_5g_val = nla_get_u8(iter);
			hal_req->config_5g_rssi_close_proximity = 1;
			break;

		case NAN_REQ_ATTR_RSSI_WINDOW_SIZE_VAL:
			hal_req->rssi_window_size_val = nla_get_u8(iter);
			hal_req->config_rssi_window_size = 1;
			break;

		case NAN_REQ_ATTR_OUI_VAL:
			hal_req->oui_val = nla_get_u32(iter);
			hal_req->config_oui = 1;
			break;

		case NAN_REQ_ATTR_MAC_ADDR_VAL:
			memcpy(hal_req->intf_addr_val, nla_data(iter), ETH_ALEN);
			hal_req->config_intf_addr = 1;
			break;

		case NAN_REQ_ATTR_CLUSTER_VAL:
			hal_req->config_cluster_attribute_val = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_SOCIAL_CH_SCAN_DWELL_TIME:
			memcpy(hal_req->scan_params_val.dwell_time, nla_data(iter),
			       sizeof(hal_req->scan_params_val.dwell_time));
			hal_req->config_scan_params = 1;
			break;

		case NAN_REQ_ATTR_SOCIAL_CH_SCAN_PERIOD:
			memcpy(hal_req->scan_params_val.scan_period, nla_data(iter),
			       sizeof(hal_req->scan_params_val.scan_period));
			hal_req->config_scan_params = 1;
			break;

		case NAN_REQ_ATTR_RANDOM_FACTOR_FORCE_VAL:
			hal_req->random_factor_force_val = nla_get_u8(iter);
			hal_req->config_random_factor_force = 1;
			break;

		case NAN_REQ_ATTR_HOP_COUNT_FORCE_VAL:
			hal_req->hop_count_force_val = nla_get_u8(iter);
			hal_req->config_hop_count_force = 1;
			break;

		case NAN_REQ_ATTR_CHANNEL_2G4_MHZ_VAL:
			hal_req->channel_24g_val = nla_get_u32(iter);
			hal_req->config_24g_channel = 1;
			break;

		case NAN_REQ_ATTR_CHANNEL_5G_MHZ_VAL:
			hal_req->channel_5g_val = nla_get_u8(iter);
			hal_req->config_5g_channel = 1;
			break;

		default:
			SLSI_ERR(sdev, "Unexpected NAN enable attribute TYPE:%d\n", type);
			return NAN_STATUS_INVALID_MSG_ID;
		}
	}
	return NAN_STATUS_SUCCESS;
}

static int slsi_nan_enable(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct slsi_hal_nan_enable_req hal_req;
	int ret;
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif;
	u8 *nan_vif_mac_address;
	u8 broadcast_mac[ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
	u32 reply_status = NAN_STATUS_SUCCESS;

	if (!dev) {
		SLSI_ERR(sdev, "No NAN interface\n");
		ret = -ENOTSUPP;
		reply_status = NAN_STATUS_NAN_NOT_ALLOWED;
		goto exit;
	}

	if (!slsi_dev_nan_supported(sdev)) {
		SLSI_ERR(sdev, "NAN not allowed(mib:%d)\n", sdev->nan_enabled);
		ret = WIFI_HAL_ERROR_NOT_SUPPORTED;
		reply_status = NAN_STATUS_NAN_NOT_ALLOWED;
		goto exit;
	}

	ndev_vif = netdev_priv(dev);

	reply_status = slsi_nan_enable_get_nl_params(sdev, &hal_req, data, len);
	if (reply_status != NAN_STATUS_SUCCESS) {
		ret = -EINVAL;
		goto exit;
	}

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	ndev_vif->vif_type = FAPI_VIFTYPE_NAN;
	nan_vif_mac_address = hal_req.config_intf_addr ? hal_req.intf_addr_val : dev->dev_addr;
	ret = slsi_mlme_add_vif(sdev, dev, nan_vif_mac_address, broadcast_mac);
	if (ret) {
		reply_status = NAN_TERMINATED_REASON_FAILURE;
		SLSI_ERR(sdev, "failed to set unsync vif. Cannot start NAN\n");
	} else {
		ret = slsi_mlme_nan_enable(sdev, dev, &hal_req);
		if (ret) {
			SLSI_ERR(sdev, "failed to enable NAN.\n");
			reply_status = NAN_TERMINATED_REASON_FAILURE;
			slsi_mlme_del_vif(sdev, dev);
			ndev_vif->activated = false;
			ndev_vif->nan.subscribe_id_map = 0;
			ndev_vif->nan.publish_id_map = 0;
		} else {
			slsi_vif_activated(sdev, dev);
		}
	}

	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
exit:
	slsi_vendor_nan_command_reply(wiphy, reply_status, ret, NAN_RESPONSE_ENABLED, 0, NULL);
	return ret;
}

static int slsi_nan_disable(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif;

	if (dev) {
		ndev_vif = netdev_priv(dev);
		SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
		if (ndev_vif->activated) {
			slsi_mlme_del_vif(sdev, dev);
			ndev_vif->activated = false;
			ndev_vif->nan.subscribe_id_map = 0;
			ndev_vif->nan.publish_id_map = 0;
		} else {
			SLSI_WARN(sdev, "NAN FWif not active!!");
		}
		SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	} else {
		SLSI_WARN(sdev, "No NAN interface!!");
	}

	slsi_vendor_nan_command_reply(wiphy, NAN_STATUS_SUCCESS, 0, NAN_RESPONSE_DISABLED, 0, NULL);

	return 0;
}

static int slsi_nan_publish_get_nl_params(struct slsi_dev *sdev, struct slsi_hal_nan_publish_req *hal_req,
					  const void *data, int len)
{
	int type, tmp;
	const struct nlattr *iter;

	memset(hal_req, 0, sizeof(*hal_req));
	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		switch (type) {
		case NAN_REQ_ATTR_PUBLISH_ID:
			hal_req->publish_id = nla_get_u16(iter);
			break;
		case NAN_REQ_ATTR_PUBLISH_TTL:
			hal_req->ttl = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_PUBLISH_PERIOD:
			hal_req->period = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_PUBLISH_TYPE:
			hal_req->publish_type = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_PUBLISH_TX_TYPE:
			hal_req->tx_type = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_PUBLISH_COUNT:
			hal_req->publish_count = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_PUBLISH_SERVICE_NAME_LEN:
			hal_req->service_name_len = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_PUBLISH_SERVICE_NAME:
			memcpy(hal_req->service_name, nla_data(iter), hal_req->service_name_len);
			break;

		case NAN_REQ_ATTR_PUBLISH_MATCH_ALGO:
			hal_req->publish_match_indicator = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_PUBLISH_SERVICE_INFO_LEN:
			hal_req->service_specific_info_len = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_PUBLISH_SERVICE_INFO:
			memcpy(hal_req->service_specific_info, nla_data(iter), hal_req->service_specific_info_len);
			break;

		case NAN_REQ_ATTR_PUBLISH_RX_MATCH_FILTER_LEN:
			hal_req->rx_match_filter_len = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_PUBLISH_RX_MATCH_FILTER:
			memcpy(hal_req->rx_match_filter, nla_data(iter), hal_req->rx_match_filter_len);
			break;

		case NAN_REQ_ATTR_PUBLISH_TX_MATCH_FILTER_LEN:
			hal_req->tx_match_filter_len = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_PUBLISH_TX_MATCH_FILTER:
			memcpy(hal_req->tx_match_filter, nla_data(iter), hal_req->tx_match_filter_len);
			break;

		case NAN_REQ_ATTR_PUBLISH_RSSI_THRESHOLD_FLAG:
			hal_req->rssi_threshold_flag = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_PUBLISH_CONN_MAP:
			hal_req->connmap = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_PUBLISH_RECV_IND_CFG:
			hal_req->recv_indication_cfg = nla_get_u8(iter);
			break;

		default:
			SLSI_ERR(sdev, "Unexpected NAN publish attribute TYPE:%d\n", type);
			return NAN_STATUS_INVALID_MSG_ID;
		}
	}
	return NAN_STATUS_SUCCESS;
}

static int slsi_nan_publish(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct slsi_hal_nan_publish_req hal_req;
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif;
	int ret;
	u32 reply_status;
	u32 publish_id = 0;

	if (!dev) {
		SLSI_ERR(sdev, "NAN netif not active!!");
		ret = -EINVAL;
		reply_status = NAN_STATUS_NAN_NOT_ALLOWED;
		goto exit;
	}

	ndev_vif = netdev_priv(dev);
	reply_status = slsi_nan_publish_get_nl_params(sdev, &hal_req, data, len);
	if (reply_status != NAN_STATUS_SUCCESS) {
		ret = -EINVAL;
		goto exit;
	}

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	if (!ndev_vif->activated) {
		SLSI_WARN(sdev, "NAN vif not activated\n");
		reply_status = NAN_STATUS_NAN_NOT_ALLOWED;
		ret = WIFI_HAL_ERROR_NOT_AVAILABLE;
		goto exit_with_lock;
	}

	if (!hal_req.publish_id) {
		hal_req.publish_id = slsi_nan_get_new_publish_id(ndev_vif);
	} else if (!slsi_nan_is_publish_id_active(ndev_vif, hal_req.publish_id)) {
		SLSI_WARN(sdev, "Publish id %d not found. map:%x\n", hal_req.publish_id,
			  ndev_vif->nan.publish_id_map);
		reply_status = NAN_STATUS_INVALID_HANDLE;
		ret = -EINVAL;
		goto exit_with_lock;
	}

	if (hal_req.publish_id) {
		ret = slsi_mlme_nan_publish(sdev, dev, &hal_req, hal_req.publish_id);
		if (ret)
			reply_status = NAN_STATUS_DE_FAILURE;
		else
			publish_id = hal_req.publish_id;
	} else {
		reply_status = NAN_STATUS_INVALID_HANDLE;
		SLSI_WARN(sdev, "Too Many concurrent PUBLISH REQ(map:%x)\n",
			  ndev_vif->nan.publish_id_map);
		ret = -ENOTSUPP;
	}
exit_with_lock:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
exit:
	slsi_vendor_nan_command_reply(wiphy, reply_status, ret, NAN_RESPONSE_PUBLISH, publish_id, NULL);
	return ret;
}

static int slsi_nan_publish_cancel(struct wiphy *wiphy, struct wireless_dev *wdev,
				   const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif;
	int type, tmp, ret = 0;
	u16 publish_id = 0;
	const struct nlattr *iter;
	u32 reply_status = NAN_STATUS_SUCCESS;

	if (!dev) {
		SLSI_ERR(sdev, "NAN netif not active!!");
		reply_status = NAN_STATUS_NAN_NOT_ALLOWED;
		ret = -EINVAL;
		goto exit;
	}

	ndev_vif = netdev_priv(dev);
	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		switch (type) {
		case NAN_REQ_ATTR_PUBLISH_ID:
			publish_id = nla_get_u16(iter);
			break;
		default:
			SLSI_ERR(sdev, "Unexpected NAN publishcancel attribute TYPE:%d\n", type);
		}
	}

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	if (!ndev_vif->activated) {
		reply_status = NAN_STATUS_NAN_NOT_ALLOWED;
		ret = WIFI_HAL_ERROR_NOT_AVAILABLE;
		goto exit_with_lock;
	}
	if (!publish_id || !slsi_nan_is_publish_id_active(ndev_vif, publish_id)) {
		reply_status = NAN_STATUS_INVALID_HANDLE;
		SLSI_WARN(sdev, "Publish_id(%d) not active. map:%x\n",
			  publish_id, ndev_vif->nan.publish_id_map);
	} else {
		ret = slsi_mlme_nan_publish(sdev, dev, NULL, publish_id);
		if (ret)
			reply_status = NAN_STATUS_DE_FAILURE;
	}
exit_with_lock:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
exit:
	slsi_vendor_nan_command_reply(wiphy, reply_status, ret, NAN_RESPONSE_PUBLISH_CANCEL, publish_id, NULL);
	return ret;
}

static int slsi_nan_subscribe_get_nl_params(struct slsi_dev *sdev, struct slsi_hal_nan_subscribe_req *hal_req,
					    const void *data, int len)
{
	int type, tmp;
	const struct nlattr *iter;

	memset(hal_req, 0, sizeof(*hal_req));
	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		switch (type) {
		case NAN_REQ_ATTR_SUBSCRIBE_ID:
			hal_req->subscribe_id = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_TTL:
			hal_req->ttl = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_PERIOD:
			hal_req->period = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_TYPE:
			hal_req->subscribe_type = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_RESP_FILTER_TYPE:
			hal_req->service_response_filter = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_RESP_INCLUDE:
			hal_req->service_response_include = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_USE_RESP_FILTER:
			hal_req->use_service_response_filter = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_SSI_REQUIRED:
			hal_req->ssi_required_for_match_indication = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_MATCH_INDICATOR:
			hal_req->subscribe_match_indicator = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_COUNT:
			hal_req->subscribe_count = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_SERVICE_NAME_LEN:
			hal_req->service_name_len = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_SERVICE_NAME:
			memcpy(hal_req->service_name, nla_data(iter), hal_req->service_name_len);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_SERVICE_INFO_LEN:
			hal_req->service_specific_info_len = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_SERVICE_INFO:
			memcpy(hal_req->service_specific_info, nla_data(iter), hal_req->service_specific_info_len);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_RX_MATCH_FILTER_LEN:
			hal_req->rx_match_filter_len = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_RX_MATCH_FILTER:
			memcpy(hal_req->rx_match_filter, nla_data(iter), hal_req->rx_match_filter_len);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_TX_MATCH_FILTER_LEN:
			hal_req->tx_match_filter_len = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_TX_MATCH_FILTER:
			memcpy(hal_req->tx_match_filter, nla_data(iter), hal_req->tx_match_filter_len);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_RSSI_THRESHOLD_FLAG:
			hal_req->rssi_threshold_flag = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_CONN_MAP:
			hal_req->connmap = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_NUM_INTF_ADDR_PRESENT:
			hal_req->num_intf_addr_present = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_INTF_ADDR:
			memcpy(hal_req->intf_addr, nla_data(iter), hal_req->num_intf_addr_present * ETH_ALEN);
			break;

		case NAN_REQ_ATTR_SUBSCRIBE_RECV_IND_CFG:
			hal_req->recv_indication_cfg = nla_get_u8(iter);
			break;

		default:
			SLSI_ERR(sdev, "Unexpected NAN subscribe attribute TYPE:%d\n", type);
			return NAN_STATUS_INVALID_MSG_ID;
		}
	}
	return NAN_STATUS_SUCCESS;
}

static int slsi_nan_subscribe(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif;
	struct slsi_hal_nan_subscribe_req *hal_req;
	int ret;
	u32 reply_status;
	u32 subscribe_id = 0;

	if (!dev) {
		SLSI_ERR(sdev, "NAN netif not active!!\n");
		reply_status = NAN_STATUS_NAN_NOT_ALLOWED;
		ret = -EINVAL;
		goto exit;
	}

	hal_req = kmalloc(sizeof(*hal_req), GFP_KERNEL);
	if (!hal_req) {
		SLSI_ERR(sdev, "Failed to alloc hal_req structure!!!\n");
		reply_status = NAN_STATUS_NO_SPACE_AVAILABLE;
		ret = -ENOMEM;
		goto exit;
	}

	ndev_vif = netdev_priv(dev);
	reply_status = slsi_nan_subscribe_get_nl_params(sdev, hal_req, data, len);
	if (reply_status != NAN_STATUS_SUCCESS) {
		kfree(hal_req);
		ret = -EINVAL;
		goto exit;
	}

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	if (!ndev_vif->activated) {
		SLSI_WARN(sdev, "NAN vif not activated\n");
		reply_status = NAN_STATUS_NAN_NOT_ALLOWED;
		ret = WIFI_HAL_ERROR_NOT_AVAILABLE;
		goto exit_with_lock;
	}

	if (!hal_req->subscribe_id) {
		hal_req->subscribe_id = slsi_nan_get_new_subscribe_id(ndev_vif);
	} else if (!slsi_nan_is_subscribe_id_active(ndev_vif, hal_req->subscribe_id)) {
		SLSI_WARN(sdev, "Subscribe id %d not found. map:%x\n", hal_req->subscribe_id,
			  ndev_vif->nan.subscribe_id_map);
		reply_status = NAN_STATUS_INVALID_HANDLE;
		ret = -EINVAL;
		goto exit_with_lock;
	}

	ret = slsi_mlme_nan_subscribe(sdev, dev, hal_req, hal_req->subscribe_id);
	if (ret)
		reply_status = NAN_STATUS_DE_FAILURE;
	else
		subscribe_id = hal_req->subscribe_id;

exit_with_lock:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	kfree(hal_req);
exit:
	slsi_vendor_nan_command_reply(wiphy, reply_status, ret, NAN_RESPONSE_SUBSCRIBE, subscribe_id, NULL);
	return ret;
}

static int slsi_nan_subscribe_cancel(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif;
	int type, tmp, ret = WIFI_HAL_ERROR_UNKNOWN;
	u16 subscribe_id = 0;
	const struct nlattr *iter;
	u32 reply_status = NAN_STATUS_SUCCESS;

	if (!dev) {
		SLSI_ERR(sdev, "NAN netif not active!!");
		reply_status = NAN_STATUS_NAN_NOT_ALLOWED;
		ret = WIFI_HAL_ERROR_NOT_AVAILABLE;
		goto exit;
	}

	ndev_vif = netdev_priv(dev);

	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		switch (type) {
		case NAN_REQ_ATTR_SUBSCRIBE_ID:
			subscribe_id = nla_get_u16(iter);
			break;
		default:
			SLSI_ERR(sdev, "Unexpected NAN subscribecancel attribute TYPE:%d\n", type);
			reply_status = NAN_STATUS_INVALID_MSG_ID;
			goto exit;
		}
	}

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	if (ndev_vif->activated) {
		if (!subscribe_id || !slsi_nan_is_subscribe_id_active(ndev_vif, subscribe_id)) {
			SLSI_WARN(sdev, "subscribe_id(%d) not active. map:%x\n",
				  subscribe_id, ndev_vif->nan.subscribe_id_map);
			reply_status = NAN_STATUS_INVALID_HANDLE;
		} else {
			ret = slsi_mlme_nan_subscribe(sdev, dev, NULL, subscribe_id);
			if (ret)
				reply_status = NAN_STATUS_DE_FAILURE;
		}
	} else {
		SLSI_ERR(sdev, "vif not activated\n");
		reply_status = NAN_STATUS_NAN_NOT_ALLOWED;
		ret = WIFI_HAL_ERROR_NOT_AVAILABLE;
	}
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
exit:
	slsi_vendor_nan_command_reply(wiphy, reply_status, ret, NAN_RESPONSE_SUBSCRIBE_CANCEL, subscribe_id, NULL);
	return ret;
}

static int slsi_nan_followup_get_nl_params(struct slsi_dev *sdev, struct slsi_hal_nan_transmit_followup_req *hal_req,
					   const void *data, int len)
{
	int type, tmp;
	const struct nlattr *iter;

	memset(hal_req, 0, sizeof(*hal_req));
	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		switch (type) {
		case NAN_REQ_ATTR_FOLLOWUP_ID:
			hal_req->publish_subscribe_id = nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_FOLLOWUP_REQUESTOR_ID:
			hal_req->requestor_instance_id = nla_get_u32(iter);
			break;

		case NAN_REQ_ATTR_FOLLOWUP_ADDR:
			memcpy(hal_req->addr, nla_data(iter), ETH_ALEN);
			break;

		case NAN_REQ_ATTR_FOLLOWUP_PRIORITY:
			hal_req->priority = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_FOLLOWUP_TX_WINDOW:
			hal_req->dw_or_faw = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_FOLLOWUP_SERVICE_NAME_LEN:
			hal_req->service_specific_info_len =  nla_get_u16(iter);
			break;

		case NAN_REQ_ATTR_FOLLOWUP_SERVICE_NAME:
			memcpy(hal_req->service_specific_info, nla_data(iter), hal_req->service_specific_info_len);
			break;

		case NAN_REQ_ATTR_FOLLOWUP_RECV_IND_CFG:
			hal_req->recv_indication_cfg = nla_get_u8(iter);
			break;

		default:
			SLSI_ERR(sdev, "Unexpected NAN followup attribute TYPE:%d\n", type);
			return NAN_STATUS_INVALID_MSG_ID;
		}
	}
	return NAN_STATUS_SUCCESS;
}

static int slsi_nan_transmit_followup(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif;
	struct slsi_hal_nan_transmit_followup_req hal_req;
	int ret;
	u32 reply_status = NAN_STATUS_SUCCESS;

	if (!dev) {
		SLSI_ERR(sdev, "NAN netif not active!!");
		ret = -EINVAL;
		reply_status = NAN_STATUS_NAN_NOT_ALLOWED;
		goto exit;
	}

	ndev_vif = netdev_priv(dev);
	reply_status = slsi_nan_followup_get_nl_params(sdev, &hal_req, data, len);
	if (reply_status) {
		ret = -EINVAL;
		goto exit;
	}

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	if (!ndev_vif->activated) {
		SLSI_WARN(sdev, "NAN vif not activated\n");
		reply_status = NAN_STATUS_NAN_NOT_ALLOWED;
		ret = WIFI_HAL_ERROR_NOT_AVAILABLE;
		goto exit_with_lock;
	}

	if (!hal_req.publish_subscribe_id ||
	    !(slsi_nan_is_subscribe_id_active(ndev_vif, hal_req.publish_subscribe_id) ||
	    slsi_nan_is_publish_id_active(ndev_vif, hal_req.publish_subscribe_id))) {
		SLSI_WARN(sdev, "publish/Subscribe id %d not found. map:%x\n", hal_req.publish_subscribe_id,
			  ndev_vif->nan.subscribe_id_map);
		reply_status = NAN_STATUS_INVALID_HANDLE;
		ret = -EINVAL;
		goto exit_with_lock;
	}

	ret = slsi_mlme_nan_tx_followup(sdev, dev, &hal_req);
	if (ret)
		reply_status = NAN_STATUS_DE_FAILURE;

exit_with_lock:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
exit:
	slsi_vendor_nan_command_reply(wiphy, reply_status, ret, NAN_RESPONSE_TRANSMIT_FOLLOWUP, 0, NULL);
	return ret;
}

static int slsi_nan_config_get_nl_params(struct slsi_dev *sdev, struct slsi_hal_nan_config_req *hal_req,
					 const void *data, int len)
{
	int type, type1, tmp, tmp1, disc_attr_idx = 0, famchan_idx = 0;
	const struct nlattr *iter, *iter1;
	struct slsi_hal_nan_post_discovery_param *disc_attr;
	struct slsi_hal_nan_further_availability_channel *famchan;

	memset(hal_req, 0, sizeof(*hal_req));
	nla_for_each_attr(iter, data, len, tmp) {
		type = nla_type(iter);
		switch (type) {
		case NAN_REQ_ATTR_SID_BEACON_VAL:
			hal_req->sid_beacon = nla_get_u8(iter);
			hal_req->config_sid_beacon = 1;
			break;

		case NAN_REQ_ATTR_RSSI_PROXIMITY_2G4_VAL:
			hal_req->rssi_proximity = nla_get_u8(iter);
			hal_req->config_rssi_proximity = 1;
			break;

		case NAN_REQ_ATTR_MASTER_PREF:
			hal_req->master_pref = nla_get_u8(iter);
			hal_req->config_master_pref = 1;
			break;

		case NAN_REQ_ATTR_RSSI_CLOSE_PROXIMITY_5G_VAL:
			hal_req->rssi_close_proximity_5g_val = nla_get_u8(iter);
			hal_req->config_5g_rssi_close_proximity = 1;
			break;

		case NAN_REQ_ATTR_RSSI_WINDOW_SIZE_VAL:
			hal_req->rssi_window_size_val = nla_get_u8(iter);
			hal_req->config_rssi_window_size = 1;
			break;

		case NAN_REQ_ATTR_CLUSTER_VAL:
			hal_req->config_cluster_attribute_val = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_SOCIAL_CH_SCAN_DWELL_TIME:
			memcpy(hal_req->scan_params_val.dwell_time, nla_data(iter),
			       sizeof(hal_req->scan_params_val.dwell_time));
			hal_req->config_scan_params = 1;
			break;

		case NAN_REQ_ATTR_SOCIAL_CH_SCAN_PERIOD:
			memcpy(hal_req->scan_params_val.scan_period, nla_data(iter),
			       sizeof(hal_req->scan_params_val.scan_period));
			hal_req->config_scan_params = 1;
			break;

		case NAN_REQ_ATTR_RANDOM_FACTOR_FORCE_VAL:
			hal_req->random_factor_force_val = nla_get_u8(iter);
			hal_req->config_random_factor_force = 1;
			break;

		case NAN_REQ_ATTR_HOP_COUNT_FORCE_VAL:
			hal_req->hop_count_force_val = nla_get_u8(iter);
			hal_req->config_hop_count_force = 1;
			break;

		case NAN_REQ_ATTR_CONN_CAPABILITY_PAYLOAD_TX:
			hal_req->conn_capability_val.payload_transmit_flag = nla_get_u8(iter);
			hal_req->config_conn_capability = 1;
			break;

		case NAN_REQ_ATTR_CONN_CAPABILITY_WFD:
			hal_req->conn_capability_val.is_wfd_supported = nla_get_u8(iter);
			hal_req->config_conn_capability = 1;
			break;

		case NAN_REQ_ATTR_CONN_CAPABILITY_WFDS:
			hal_req->conn_capability_val.is_wfds_supported = nla_get_u8(iter);
			hal_req->config_conn_capability = 1;
			break;

		case NAN_REQ_ATTR_CONN_CAPABILITY_TDLS:
			hal_req->conn_capability_val.is_tdls_supported = nla_get_u8(iter);
			hal_req->config_conn_capability = 1;
			break;

		case NAN_REQ_ATTR_CONN_CAPABILITY_MESH:
			hal_req->conn_capability_val.is_mesh_supported = nla_get_u8(iter);
			hal_req->config_conn_capability = 1;
			break;

		case NAN_REQ_ATTR_CONN_CAPABILITY_IBSS:
			hal_req->conn_capability_val.is_ibss_supported = nla_get_u8(iter);
			hal_req->config_conn_capability = 1;
			break;

		case NAN_REQ_ATTR_CONN_CAPABILITY_WLAN_INFRA:
			hal_req->conn_capability_val.wlan_infra_field = nla_get_u8(iter);
			hal_req->config_conn_capability = 1;
			break;

		case NAN_REQ_ATTR_DISCOVERY_ATTR_NUM_ENTRIES:
			hal_req->num_config_discovery_attr = nla_get_u8(iter);
			break;

		case NAN_REQ_ATTR_DISCOVERY_ATTR_VAL:
			if (disc_attr_idx >= hal_req->num_config_discovery_attr) {
				SLSI_ERR(sdev,
					 "disc attr(%d) > num disc attr(%d)\n",
					 disc_attr_idx + 1, hal_req->num_config_discovery_attr);
				return -EINVAL;
			}
			disc_attr = &hal_req->discovery_attr_val[disc_attr_idx];
			disc_attr_idx++;
			nla_for_each_nested(iter1, iter, tmp1) {
				type1 = nla_type(iter1);
				switch (type1) {
				case NAN_REQ_ATTR_CONN_TYPE:
					disc_attr->type = nla_get_u8(iter1);
					break;

				case NAN_REQ_ATTR_NAN_ROLE:
					disc_attr->role = nla_get_u8(iter1);
					break;

				case NAN_REQ_ATTR_TRANSMIT_FREQ:
					disc_attr->transmit_freq = nla_get_u8(iter1);
					break;

				case NAN_REQ_ATTR_AVAILABILITY_DURATION:
					disc_attr->duration = nla_get_u8(iter1);
					break;

				case NAN_REQ_ATTR_AVAILABILITY_INTERVAL:
					disc_attr->avail_interval_bitmap = nla_get_u32(iter1);
					break;

				case NAN_REQ_ATTR_MAC_ADDR_VAL:
					memcpy(disc_attr->addr, nla_data(iter1), ETH_ALEN);
					break;

				case NAN_REQ_ATTR_MESH_ID_LEN:
					disc_attr->mesh_id_len = nla_get_u16(iter1);
					break;

				case NAN_REQ_ATTR_MESH_ID:
					memcpy(disc_attr->mesh_id, nla_data(iter1), disc_attr->mesh_id_len);
					break;

				case NAN_REQ_ATTR_INFRASTRUCTURE_SSID_LEN:
					disc_attr->infrastructure_ssid_len = nla_get_u16(iter1);
					break;

				case NAN_REQ_ATTR_INFRASTRUCTURE_SSID:
					memcpy(disc_attr->infrastructure_ssid_val, nla_data(iter1),
					       disc_attr->infrastructure_ssid_len);
					break;
				}
			}
			break;

		case NAN_REQ_ATTR_FURTHER_AVAIL_NUM_ENTRIES:
			hal_req->fam_val.numchans = nla_get_u8(iter);
			hal_req->config_fam = 1;
			break;

		case NAN_REQ_ATTR_FURTHER_AVAIL_VAL:
			hal_req->config_fam = 1;
			if (famchan_idx >= hal_req->fam_val.numchans) {
				SLSI_ERR(sdev,
					 "famchan attr(%d) > numchans(%d)\n",
					 famchan_idx + 1, hal_req->fam_val.numchans);
				return -EINVAL;
			}
			famchan = &hal_req->fam_val.famchan[famchan_idx];
			famchan_idx++;
			nla_for_each_nested(iter1, iter, tmp1) {
				type1 = nla_type(iter1);
				switch (type1) {
				case NAN_REQ_ATTR_FURTHER_AVAIL_ENTRY_CTRL:
					famchan->entry_control = nla_get_u8(iter1);
					break;

				case NAN_REQ_ATTR_FURTHER_AVAIL_CHAN_CLASS:
					famchan->class_val = nla_get_u8(iter1);
					break;

				case NAN_REQ_ATTR_FURTHER_AVAIL_CHAN:
					famchan->channel = nla_get_u8(iter1);
					break;

				case NAN_REQ_ATTR_FURTHER_AVAIL_CHAN_MAPID:
					famchan->mapid = nla_get_u8(iter1);
					break;

				case NAN_REQ_ATTR_FURTHER_AVAIL_INTERVAL_BITMAP:
					famchan->avail_interval_bitmap = nla_get_u32(iter1);
					break;
				}
			}
			break;
		default:
			SLSI_ERR(sdev, "Unexpected NAN config attribute TYPE:%d\n", type);
			return NAN_STATUS_INVALID_MSG_ID;
		}
	}
	return NAN_STATUS_SUCCESS;
}

static int slsi_nan_set_config(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif;
	struct slsi_hal_nan_config_req hal_req;
	int ret;
	u32 reply_status = NAN_STATUS_SUCCESS;

	if (!dev) {
		SLSI_ERR(sdev, "NAN netif not active!!");
		ret = -EINVAL;
		reply_status = NAN_STATUS_NAN_NOT_ALLOWED;
		goto exit;
	}

	ndev_vif = netdev_priv(dev);
	reply_status = slsi_nan_config_get_nl_params(sdev, &hal_req, data, len);
	if (reply_status) {
		ret = -EINVAL;
		goto exit;
	}

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	if (!ndev_vif->activated) {
		SLSI_WARN(sdev, "NAN vif not activated\n");
		reply_status = NAN_STATUS_NAN_NOT_ALLOWED;
		ret = WIFI_HAL_ERROR_NOT_AVAILABLE;
	} else {
		ret = slsi_mlme_nan_set_config(sdev, dev, &hal_req);
		if (ret)
			reply_status = NAN_STATUS_DE_FAILURE;
	}
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
exit:
	slsi_vendor_nan_command_reply(wiphy, reply_status, ret, NAN_RESPONSE_CONFIG, 0, NULL);
	return ret;
}

static int slsi_nan_get_capabilities(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data,
				     int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = slsi_nan_get_netdev(sdev);
	struct netdev_vif *ndev_vif;
	u32 reply_status = NAN_STATUS_SUCCESS;
	struct slsi_hal_nan_capabilities nan_capabilities;
	int ret = 0, i;
	struct slsi_mib_value *values = NULL;
	struct slsi_mib_data mibrsp = { 0, NULL };

	/*********************************************************************
	 * TODO: FW is not yet ready with MIBS. update the below MIBs after
	 * FW change.
	 */
#define SLSI_PSID_UNIFI_NANMAX_CONCURRENT_CLUSTERS 6082
#define SLSI_PSID_UNIFI_NANMAX_CONCURRENT_PUBLISHES 6083
#define SLSI_PSID_UNIFI_NANMAX_CONCURRENT_SUBSCRIBES 6084
#define SLSI_PSID_UNIFI_NANMAX_SERVICE_NAME_LENGTH 6085
#define SLSI_PSID_UNIFI_NANMAX_MATCH_FILTER_LENGTH 6086
#define SLSI_PSID_UNIFI_NANMAX_TOTAL_MATCH_FILTER_LENGTH 6087
#define SLSI_PSID_UNIFI_NANMAX_SERVICE_SPECIFIC_INFO_LENGTH 6088
#define SLSI_PSID_UNIFI_NANMAX_VSA_DATA_LENGTH 6089
#define SLSI_PSID_UNIFI_NANMAX_MESH_DATA_LENGTH 6090
#define SLSI_PSID_UNIFI_NANMAX_NDI_INTERFACE 6091
#define SLSI_PSID_UNIFI_NANMAX_NDP_SESSIONS 6092
#define SLSI_PSID_UNIFI_NANMAX_APP_INFO_LENGTH 6093
	struct slsi_mib_get_entry get_values[] = {{ SLSI_PSID_UNIFI_NANMAX_CONCURRENT_CLUSTERS, { 0, 0 } },
						  { SLSI_PSID_UNIFI_NANMAX_CONCURRENT_PUBLISHES, { 0, 0 } },
						  { SLSI_PSID_UNIFI_NANMAX_CONCURRENT_SUBSCRIBES, { 0, 0 } },
						  { SLSI_PSID_UNIFI_NANMAX_SERVICE_NAME_LENGTH, { 0, 0 } },
						  { SLSI_PSID_UNIFI_NANMAX_MATCH_FILTER_LENGTH, { 0, 0 } },
						  { SLSI_PSID_UNIFI_NANMAX_TOTAL_MATCH_FILTER_LENGTH, { 0, 0 } },
						  { SLSI_PSID_UNIFI_NANMAX_SERVICE_SPECIFIC_INFO_LENGTH, { 0, 0 } },
						  { SLSI_PSID_UNIFI_NANMAX_VSA_DATA_LENGTH, { 0, 0 } },
						  { SLSI_PSID_UNIFI_NANMAX_MESH_DATA_LENGTH, { 0, 0 } },
						  { SLSI_PSID_UNIFI_NANMAX_NDI_INTERFACE, { 0, 0 } },
						  { SLSI_PSID_UNIFI_NANMAX_NDP_SESSIONS, { 0, 0 } },
						  { SLSI_PSID_UNIFI_NANMAX_APP_INFO_LENGTH, { 0, 0 } } };
	/*********************************************************************/

	u32 *capabilities_mib_val[] = { &nan_capabilities.max_concurrent_nan_clusters,
									&nan_capabilities.max_publishes,
									&nan_capabilities.max_subscribes,
									&nan_capabilities.max_service_name_len,
									&nan_capabilities.max_match_filter_len,
									&nan_capabilities.max_total_match_filter_len,
									&nan_capabilities.max_service_specific_info_len,
									&nan_capabilities.max_vsa_data_len,
									&nan_capabilities.max_mesh_data_len,
									&nan_capabilities.max_ndi_interfaces,
									&nan_capabilities.max_ndp_sessions,
									&nan_capabilities.max_app_info_len };

	if (!dev) {
		SLSI_ERR(sdev, "NAN netif not active!!");
		reply_status = NAN_STATUS_NAN_NOT_ALLOWED;
		ret = -EINVAL;
		goto exit;
	}

	ndev_vif = netdev_priv(dev);

	/* Expect each mib length in response is 11 */
	mibrsp.dataLength = 11 * ARRAY_SIZE(get_values);
	mibrsp.data = kmalloc(mibrsp.dataLength, GFP_KERNEL);
	if (!mibrsp.data) {
		SLSI_ERR(sdev, "Cannot kmalloc %d bytes\n", mibrsp.dataLength);
		reply_status = NAN_STATUS_NO_SPACE_AVAILABLE;
		ret = -ENOMEM;
		goto exit;
	}

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	values = slsi_read_mibs(sdev, NULL, get_values, ARRAY_SIZE(get_values), &mibrsp);
	if (!values) {
		ret = 0xFFFFFFFF;
		reply_status = 0xFFFFFFFF;
		goto exit_with_mibrsp;
	}

	for (i = 0; i < ARRAY_SIZE(get_values); i++) {
		if (values[i].type == SLSI_MIB_TYPE_UINT) {
			*capabilities_mib_val[i] = values[i].u.uintValue;
			SLSI_DBG2(sdev, SLSI_GSCAN, "MIB value = %ud\n", *capabilities_mib_val[i]);
		} else {
			SLSI_ERR(sdev, "invalid type(%d). iter:%d\n", values[i].type, i);
			ret = 0xFFFFFFFF;
			reply_status = 0xFFFFFFFF;
			*capabilities_mib_val[i] = 0;
		}
	}

	kfree(values);
exit_with_mibrsp:
	kfree(mibrsp.data);
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
exit:
	slsi_vendor_nan_command_reply(wiphy, reply_status, ret, NAN_RESPONSE_GET_CAPABILITIES, 0, &nan_capabilities);
	return ret;
}

void slsi_nan_event(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb)
{
	struct sk_buff *nl_skb = NULL;
	int res = 0;
	u16 event, identifier, evt_reason;
	u8 *mac_addr;
	u16 hal_event;
	struct nlattr *nlattr_start;
	struct netdev_vif *ndev_vif;
	enum slsi_nan_disc_event_type disc_event_type = 0;

	ndev_vif = netdev_priv(dev);
	event = fapi_get_u16(skb, u.mlme_nan_event_ind.event);
	identifier = fapi_get_u16(skb, u.mlme_nan_event_ind.identifier);
	mac_addr = fapi_get_buff(skb, u.mlme_nan_event_ind.address_or_identifier);
	evt_reason = fapi_get_u16(skb, u.mlme_nan_event_ind.reason_code);

	switch (event) {
	case FAPI_EVENT_WIFI_EVENT_NAN_PUBLISH_TERMINATED:
		hal_event = SLSI_NL80211_NAN_PUBLISH_TERMINATED_EVENT;
		break;
	case FAPI_EVENT_WIFI_EVENT_NAN_MATCH_EXPIRED:
		hal_event = SLSI_NL80211_NAN_MATCH_EXPIRED_EVENT;
		break;
	case FAPI_EVENT_WIFI_EVENT_NAN_SUBSCRIBE_TERMINATED:
		hal_event = SLSI_NL80211_NAN_SUBSCRIBE_TERMINATED_EVENT;
		break;
	case FAPI_EVENT_WIFI_EVENT_NAN_ADDRESS_CHANGED:
		disc_event_type = NAN_EVENT_ID_DISC_MAC_ADDR;
		hal_event = SLSI_NL80211_NAN_DISCOVERY_ENGINE_EVENT;
		break;
	case FAPI_EVENT_WIFI_EVENT_NAN_CLUSTER_STARTED:
		disc_event_type = NAN_EVENT_ID_STARTED_CLUSTER;
		hal_event = SLSI_NL80211_NAN_DISCOVERY_ENGINE_EVENT;
		break;
	case FAPI_EVENT_WIFI_EVENT_NAN_CLUSTER_JOINED:
		disc_event_type = NAN_EVENT_ID_JOINED_CLUSTER;
		hal_event = SLSI_NL80211_NAN_DISCOVERY_ENGINE_EVENT;
		break;
	default:
		return;
	}

#ifdef CONFIG_SCSC_WLAN_DEBUG
	SLSI_DBG1_NODEV(SLSI_GSCAN, "Event: %s(%d)\n",
			slsi_print_event_name(hal_event), hal_event);
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NULL, NLMSG_DEFAULT_SIZE, hal_event, GFP_KERNEL);
#else
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NLMSG_DEFAULT_SIZE, hal_event, GFP_KERNEL);
#endif
	if (!nl_skb) {
		SLSI_ERR(sdev, "NO MEM for nl_skb!!!\n");
		return;
	}

	nlattr_start = nla_nest_start(nl_skb, NL80211_ATTR_VENDOR_DATA);
	if (!nlattr_start) {
		SLSI_ERR(sdev, "failed to put NL80211_ATTR_VENDOR_DATA\n");
		/* Dont use slsi skb wrapper for this free */
		kfree_skb(nl_skb);
		return;
	}

	switch (hal_event) {
	case SLSI_NL80211_NAN_PUBLISH_TERMINATED_EVENT:
		res |= nla_put_be16(nl_skb, NAN_EVT_ATTR_PUBLISH_ID, identifier);
		res |= nla_put_be16(nl_skb, NAN_EVT_ATTR_PUBLISH_ID, evt_reason);
		ndev_vif->nan.publish_id_map &= ~BIT(identifier);
		break;
	case SLSI_NL80211_NAN_MATCH_EXPIRED_EVENT:
		res |= nla_put_be16(nl_skb, NAN_EVT_ATTR_MATCH_PUBLISH_SUBSCRIBE_ID, identifier);
		res |= nla_put_be16(nl_skb, NAN_EVT_ATTR_MATCH_REQUESTOR_INSTANCE_ID, evt_reason);
		break;
	case SLSI_NL80211_NAN_SUBSCRIBE_TERMINATED_EVENT:
		res |= nla_put_be16(nl_skb, NAN_EVT_ATTR_SUBSCRIBE_ID, identifier);
		res |= nla_put_be16(nl_skb, NAN_EVT_ATTR_SUBSCRIBE_REASON, evt_reason);
		ndev_vif->nan.subscribe_id_map &= ~BIT(identifier);
		break;
	case SLSI_NL80211_NAN_DISCOVERY_ENGINE_EVENT:
		res |= nla_put_be16(nl_skb, NAN_EVT_ATTR_DISCOVERY_ENGINE_EVT_TYPE, disc_event_type);
		res |= nla_put(nl_skb, NAN_EVT_ATTR_DISCOVERY_ENGINE_MAC_ADDR, ETH_ALEN, mac_addr);
		break;
	}

	if (res) {
		SLSI_ERR(sdev, "Error in nla_put*:%x\n", res);
		/* Dont use slsi skb wrapper for this free */
		kfree_skb(nl_skb);
		return;
	}

	nla_nest_end(nl_skb, nlattr_start);

	cfg80211_vendor_event(nl_skb, GFP_KERNEL);
}

void slsi_nan_followup_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb)
{
	u16 tag_id, tag_len;
	u8  *fapi_data_p, *ptr;
	u8  followup_ie_header[] = {0xdd, 0, 0, 0x16, 0x32, 0x0b, 0x05};
	int fapi_data_len;
	struct slsi_hal_nan_followup_ind *hal_evt;
	struct sk_buff *nl_skb;
	int res;
	struct nlattr *nlattr_start;

	hal_evt = kmalloc(sizeof(*hal_evt), GFP_KERNEL);
	if (!hal_evt) {
		SLSI_ERR(sdev, "No memory for service_ind\n");
		return;
	}
	hal_evt->publish_subscribe_id = fapi_get_u16(skb, u.mlme_nan_followup_ind.publish_subscribe_id);
	hal_evt->requestor_instance_id = fapi_get_u16(skb, u.mlme_nan_followup_ind.requestor_instance_id);
	fapi_data_p = fapi_get_data(skb);
	fapi_data_len = fapi_get_datalen(skb);
	if (!fapi_data_len) {
		SLSI_ERR(sdev, "mlme_nan_followup_ind no mbulk data\n");
		kfree(hal_evt);
		return;
	}

	memset(&hal_evt, 0, sizeof(hal_evt));

	while (fapi_data_len) {
		ptr = fapi_data_p;
		if (fapi_data_len < ptr[1] + 2) {
			SLSI_ERR(sdev, "len err[avail:%d,ie:%d]\n", fapi_data_len, fapi_data_p[1] + 2);
			kfree(hal_evt);
			return;
		}
		if (ptr[1] < sizeof(followup_ie_header) - 2 + 6 + 1 + 1) {
			SLSI_ERR(sdev, "len err[min:%d,ie:%d]\n", (u32)sizeof(followup_ie_header) - 2 + 6 + 1 + 1,
				 fapi_data_p[1] + 2);
			kfree(hal_evt);
			return;
		}
		if (followup_ie_header[0] != ptr[0] ||  followup_ie_header[2] != ptr[2] ||
		    followup_ie_header[3] != ptr[3] ||  followup_ie_header[4] != ptr[4] ||
		    followup_ie_header[5] != ptr[5] || followup_ie_header[6] != ptr[6]) {
			SLSI_ERR(sdev, "unknown IE:%x-%d\n", fapi_data_p[0], fapi_data_p[1] + 2);
			kfree(hal_evt);
			return;
		}

		ptr += sizeof(followup_ie_header);

		ether_addr_copy(hal_evt->addr, ptr);
		ptr += ETH_ALEN;
		ptr += 1; /* skip priority */
		hal_evt->dw_or_faw = *ptr;
		ptr += 1;
		while (fapi_data_p[1] + 2 > (ptr - fapi_data_p) + 4) {
			tag_id = *(u16 *)ptr;
			ptr += 2;
			tag_len = *(u16 *)ptr;
			ptr += 2;
			if (fapi_data_p[1] + 2 < (ptr - fapi_data_p) + tag_len) {
				SLSI_ERR(sdev, "TLV error\n");
				kfree(hal_evt);
				return;
			}
			if (tag_id == SLSI_FAPI_NAN_SERVICE_SPECIFIC_INFO) {
				hal_evt->service_specific_info_len = tag_len;
				memcpy(hal_evt->service_specific_info, ptr, tag_len);
			}
			ptr += tag_len;
		}

		fapi_data_p += fapi_data_p[1] + 2;
		fapi_data_len -= fapi_data_p[1] + 2;
	}

#ifdef CONFIG_SCSC_WLAN_DEBUG
	SLSI_DBG1_NODEV(SLSI_GSCAN, "Event: %s(%d)\n",
			slsi_print_event_name(SLSI_NL80211_NAN_FOLLOWUP_EVENT), SLSI_NL80211_NAN_FOLLOWUP_EVENT);
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NULL, NLMSG_DEFAULT_SIZE, SLSI_NL80211_NAN_FOLLOWUP_EVENT,
					     GFP_KERNEL);
#else
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NLMSG_DEFAULT_SIZE, SLSI_NL80211_NAN_FOLLOWUP_EVENT,
					     GFP_KERNEL);
#endif

	if (!nl_skb) {
		SLSI_ERR(sdev, "NO MEM for nl_skb!!!\n");
		kfree(hal_evt);
		return;
	}

	nlattr_start = nla_nest_start(nl_skb, NL80211_ATTR_VENDOR_DATA);
	if (!nlattr_start) {
		SLSI_ERR(sdev, "failed to put NL80211_ATTR_VENDOR_DATA\n");
		kfree(hal_evt);
		/* Dont use slsi skb wrapper for this free */
		kfree_skb(nl_skb);
		return;
	}

	res = nla_put_be16(nl_skb, NAN_EVT_ATTR_FOLLOWUP_PUBLISH_SUBSCRIBE_ID,
			   cpu_to_le16(hal_evt->publish_subscribe_id));
	res |= nla_put_be16(nl_skb, NAN_EVT_ATTR_FOLLOWUP_REQUESTOR_INSTANCE_ID,
			    cpu_to_le16(hal_evt->requestor_instance_id));
	res |= nla_put(nl_skb, NAN_EVT_ATTR_FOLLOWUP_ADDR, ETH_ALEN, hal_evt->addr);
	res |= nla_put_u8(nl_skb, NAN_EVT_ATTR_FOLLOWUP_DW_OR_FAW, hal_evt->dw_or_faw);
	res |= nla_put_u16(nl_skb, NAN_EVT_ATTR_FOLLOWUP_SERVICE_SPECIFIC_INFO_LEN, hal_evt->service_specific_info_len);
	if (hal_evt->service_specific_info_len)
		res |= nla_put(nl_skb, NAN_EVT_ATTR_FOLLOWUP_SERVICE_SPECIFIC_INFO, hal_evt->service_specific_info_len,
			       hal_evt->service_specific_info);

	if (res) {
		SLSI_ERR(sdev, "Error in nla_put*:%x\n", res);
		kfree(hal_evt);
		/* Dont use slsi skb wrapper for this free */
		kfree_skb(nl_skb);
		return;
	}

	nla_nest_end(nl_skb, nlattr_start);

	cfg80211_vendor_event(nl_skb, GFP_KERNEL);
	kfree(hal_evt);
}

void slsi_nan_service_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb)
{
	u16 tag_id, tag_len;
	u8  *fapi_data_p, *ptr;
	u8  match_ie_header[] = {0xdd, 0, 0, 0x16, 0x32, 0x0b, 0x04};
	int fapi_data_len;
	struct slsi_hal_nan_match_ind *hal_evt;
	struct sk_buff *nl_skb;
	int res, i;
	struct slsi_hal_nan_receive_post_discovery *discovery_attr;
	struct slsi_hal_nan_further_availability_channel *famchan;
	struct nlattr *nlattr_start, *nlattr_nested;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");

	hal_evt = kmalloc(sizeof(*hal_evt), GFP_KERNEL);
	if (!hal_evt) {
		SLSI_ERR(sdev, "No memory for service_ind\n");
		return;
	}

	hal_evt->publish_subscribe_id = fapi_get_u16(skb, u.mlme_nan_service_ind.publish_subscribe_id);
	hal_evt->requestor_instance_id = fapi_get_u32(skb, u.mlme_nan_service_ind.requestor_instance_id);
	fapi_data_p = fapi_get_data(skb);
	fapi_data_len = fapi_get_datalen(skb);
	if (!fapi_data_len) {
		SLSI_ERR(sdev, "mlme_nan_followup_ind no mbulk data\n");
		kfree(hal_evt);
		return;
	}

	memset(hal_evt, 0, sizeof(*hal_evt));

	while (fapi_data_len) {
		ptr = fapi_data_p;
		if (fapi_data_len < ptr[1] + 2) {
			SLSI_ERR(sdev, "len err[avail:%d,ie:%d]\n", fapi_data_len, fapi_data_p[1] + 2);
			kfree(hal_evt);
			return;
		}
		if (ptr[1] < sizeof(match_ie_header) - 2 + 6 + 1 + 1 + 1) {
			SLSI_ERR(sdev, "len err[min:%d,ie:%d]\n", (u32)sizeof(match_ie_header) - 2 + 6 + 1 + 1,
				 fapi_data_p[1] + 2);
			kfree(hal_evt);
			return;
		}
		if (match_ie_header[0] != ptr[0] ||  match_ie_header[2] != ptr[2] ||
		    match_ie_header[3] != ptr[3] ||  match_ie_header[4] != ptr[4] ||
		    match_ie_header[5] != ptr[5] || match_ie_header[6] != ptr[6]) {
			SLSI_ERR(sdev, "unknown IE:%x-%d\n", fapi_data_p[0], fapi_data_p[1] + 2);
			kfree(hal_evt);
			return;
		}

		ptr += sizeof(match_ie_header);

		ether_addr_copy(hal_evt->addr, ptr);
		ptr += ETH_ALEN;
		hal_evt->match_occurred_flag = *ptr;
		ptr += 1;
		hal_evt->out_of_resource_flag = *ptr;
		ptr += 1;
		hal_evt->rssi_value = *ptr;
		ptr += 1;
		while (fapi_data_p[1] + 2 > (ptr - fapi_data_p) + 4) {
			tag_id = *(u16 *)ptr;
			ptr += 2;
			tag_len = *(u16 *)ptr;
			ptr += 2;
			if (fapi_data_p[1] + 2 < (ptr - fapi_data_p) + tag_len) {
				SLSI_ERR(sdev, "TLV error\n");
				kfree(hal_evt);
				return;
			}
			switch (tag_id) {
			case SLSI_FAPI_NAN_SERVICE_SPECIFIC_INFO:
				hal_evt->service_specific_info_len = tag_len;
				memcpy(hal_evt->service_specific_info, ptr, tag_len);
				break;
			case SLSI_FAPI_NAN_CONFIG_PARAM_CONNECTION_CAPAB:
				hal_evt->is_conn_capability_valid = 1;
				if (*ptr & BIT(0))
					hal_evt->conn_capability.is_wfd_supported = 1;
				if (*ptr & BIT(1))
					hal_evt->conn_capability.is_wfds_supported = 1;
				if (*ptr & BIT(2))
					hal_evt->conn_capability.is_tdls_supported = 1;
				if (*ptr & BIT(3))
					hal_evt->conn_capability.wlan_infra_field = 1;
				break;
			case SLSI_FAPI_NAN_CONFIG_PARAM_POST_DISCOVER_PARAM:
				discovery_attr = &hal_evt->discovery_attr[hal_evt->num_rx_discovery_attr];
				discovery_attr->type = ptr[0];
				discovery_attr->role = ptr[1];
				discovery_attr->duration = ptr[2];
				discovery_attr->avail_interval_bitmap = le32_to_cpu(*(__le32 *)&ptr[3]);
				ether_addr_copy(discovery_attr->addr, &ptr[7]);
				discovery_attr->infrastructure_ssid_len = ptr[13];
				if (discovery_attr->infrastructure_ssid_len)
					memcpy(discovery_attr->infrastructure_ssid_val, &ptr[14],
					       discovery_attr->infrastructure_ssid_len);
				hal_evt->num_rx_discovery_attr++;
				break;
			case SLSI_FAPI_NAN_CONFIG_PARAM_FURTHER_AVAIL_CHANNEL_MAP:
				famchan = &hal_evt->famchan[hal_evt->num_chans];
				famchan->entry_control = ptr[0];
				famchan->class_val =  ptr[1];
				famchan->channel = ptr[2];
				famchan->mapid = ptr[3];
				famchan->avail_interval_bitmap = le32_to_cpu(*(__le32 *)&ptr[4]);
				hal_evt->num_chans++;
				break;
			case SLSI_FAPI_NAN_CLUSTER_ATTRIBUTE:
				break;
			}
			ptr += tag_len;
		}

		fapi_data_p += fapi_data_p[1] + 2;
		fapi_data_len -= fapi_data_p[1] + 2;
	}

#ifdef CONFIG_SCSC_WLAN_DEBUG
	SLSI_DBG1_NODEV(SLSI_GSCAN, "Event: %s(%d)\n",
			slsi_print_event_name(SLSI_NL80211_NAN_MATCH_EVENT), SLSI_NL80211_NAN_MATCH_EVENT);
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NULL, NLMSG_DEFAULT_SIZE, SLSI_NL80211_NAN_MATCH_EVENT,
					     GFP_KERNEL);
#else
	nl_skb = cfg80211_vendor_event_alloc(sdev->wiphy, NLMSG_DEFAULT_SIZE, SLSI_NL80211_NAN_MATCH_EVENT, GFP_KERNEL);
#endif
	if (!nl_skb) {
		SLSI_ERR(sdev, "NO MEM for nl_skb!!!\n");
		kfree(hal_evt);
		return;
	}

	nlattr_start = nla_nest_start(nl_skb, NL80211_ATTR_VENDOR_DATA);
	if (!nlattr_start) {
		SLSI_ERR(sdev, "failed to put NL80211_ATTR_VENDOR_DATA\n");
		kfree(hal_evt);
		/* Dont use slsi skb wrapper for this free */
		kfree_skb(nl_skb);
		return;
	}

	res = nla_put_u16(nl_skb, NAN_EVT_ATTR_MATCH_PUBLISH_SUBSCRIBE_ID, hal_evt->publish_subscribe_id);
	res |= nla_put_u32(nl_skb, NAN_EVT_ATTR_MATCH_REQUESTOR_INSTANCE_ID, hal_evt->requestor_instance_id);
	res |= nla_put(nl_skb, NAN_EVT_ATTR_MATCH_ADDR, ETH_ALEN, hal_evt->addr);
	res |= nla_put_u16(nl_skb, NAN_EVT_ATTR_MATCH_SERVICE_SPECIFIC_INFO_LEN, hal_evt->service_specific_info_len);
	if (hal_evt->service_specific_info_len)
		res |= nla_put(nl_skb, NAN_EVT_ATTR_MATCH_SERVICE_SPECIFIC_INFO, hal_evt->service_specific_info_len,
			hal_evt->service_specific_info);
	res |= nla_put_u16(nl_skb, NAN_EVT_ATTR_MATCH_SDF_MATCH_FILTER_LEN, hal_evt->sdf_match_filter_len);
	if (hal_evt->sdf_match_filter_len)
		res |= nla_put(nl_skb, NAN_EVT_ATTR_MATCH_SDF_MATCH_FILTER, hal_evt->sdf_match_filter_len,
			hal_evt->sdf_match_filter);

	res |= nla_put_u8(nl_skb, NAN_EVT_ATTR_MATCH_MATCH_OCCURRED_FLAG, hal_evt->match_occurred_flag);
	res |= nla_put_u8(nl_skb, NAN_EVT_ATTR_MATCH_OUT_OF_RESOURCE_FLAG, hal_evt->out_of_resource_flag);
	res |= nla_put_u8(nl_skb, NAN_EVT_ATTR_MATCH_RSSI_VALUE, hal_evt->rssi_value);
	res |= nla_put_u8(nl_skb, NAN_EVT_ATTR_MATCH_CONN_CAPABILITY_IS_IBSS_SUPPORTED,
		hal_evt->is_conn_capability_valid);
	if (hal_evt->is_conn_capability_valid) {
		res |= nla_put_u8(nl_skb, NAN_EVT_ATTR_MATCH_CONN_CAPABILITY_IS_IBSS_SUPPORTED,
			hal_evt->conn_capability.is_ibss_supported);
		res |= nla_put_u8(nl_skb, NAN_EVT_ATTR_MATCH_CONN_CAPABILITY_IS_WFD_SUPPORTED,
			hal_evt->conn_capability.is_wfd_supported);
		res |= nla_put_u8(nl_skb, NAN_EVT_ATTR_MATCH_CONN_CAPABILITY_IS_WFDS_SUPPORTED,
			hal_evt->conn_capability.is_wfds_supported);
		res |= nla_put_u8(nl_skb, NAN_EVT_ATTR_MATCH_CONN_CAPABILITY_IS_TDLS_SUPPORTED,
			hal_evt->conn_capability.is_tdls_supported);
		res |= nla_put_u8(nl_skb, NAN_EVT_ATTR_MATCH_CONN_CAPABILITY_IS_MESH_SUPPORTED,
			hal_evt->conn_capability.is_mesh_supported);
		res |= nla_put_u8(nl_skb, NAN_EVT_ATTR_MATCH_CONN_CAPABILITY_WLAN_INFRA_FIELD,
			hal_evt->conn_capability.wlan_infra_field);
	}

	res |= nla_put_u8(nl_skb, NAN_EVT_ATTR_MATCH_NUM_RX_DISCOVERY_ATTR, hal_evt->num_rx_discovery_attr);
	for (i = 0; i < hal_evt->num_rx_discovery_attr; i++) {
		nlattr_nested = nla_nest_start(nl_skb, NAN_EVT_ATTR_MATCH_RX_DISCOVERY_ATTR);
		if (!nlattr_nested) {
			SLSI_ERR(sdev, "Error in nla_nest_start\n");
			/* Dont use slsi skb wrapper for this free */
			kfree_skb(nl_skb);
			kfree(hal_evt);
			return;
		}
		discovery_attr = &hal_evt->discovery_attr[i];
		res |= nla_put_u8(nl_skb, NAN_EVT_ATTR_MATCH_DISC_ATTR_TYPE, discovery_attr->type);
		res |= nla_put_u8(nl_skb, NAN_EVT_ATTR_MATCH_DISC_ATTR_ROLE, discovery_attr->role);
		res |= nla_put_u8(nl_skb, NAN_EVT_ATTR_MATCH_DISC_ATTR_DURATION, discovery_attr->duration);
		res |= nla_put_u32(nl_skb, NAN_EVT_ATTR_MATCH_DISC_ATTR_AVAIL_INTERVAL_BITMAP,
		       discovery_attr->avail_interval_bitmap);
		res |= nla_put_u8(nl_skb, NAN_EVT_ATTR_MATCH_DISC_ATTR_MAPID, discovery_attr->mapid);
		res |= nla_put(nl_skb, NAN_EVT_ATTR_MATCH_DISC_ATTR_ADDR, ETH_ALEN, discovery_attr->addr);
		res |= nla_put_u8(nl_skb, NAN_EVT_ATTR_MATCH_DISC_ATTR_MESH_ID_LEN, discovery_attr->mesh_id_len);
		if (discovery_attr->mesh_id_len)
			res |= nla_put(nl_skb, NAN_EVT_ATTR_MATCH_DISC_ATTR_MESH_ID, discovery_attr->mesh_id_len,
			       discovery_attr->mesh_id);
		res |= nla_put_u8(nl_skb, NAN_EVT_ATTR_MATCH_DISC_ATTR_INFRASTRUCTURE_SSID_LEN,
		       discovery_attr->infrastructure_ssid_len);
		if (discovery_attr->infrastructure_ssid_len)
			res |= nla_put(nl_skb, NAN_EVT_ATTR_MATCH_DISC_ATTR_INFRASTRUCTURE_SSID_VAL,
			       discovery_attr->infrastructure_ssid_len, discovery_attr->infrastructure_ssid_val);
		nla_nest_end(nl_skb, nlattr_nested);
	}

	res |= nla_put_u8(nl_skb, NAN_EVT_ATTR_MATCH_NUM_CHANS, hal_evt->num_chans);
	for (i = 0; i < hal_evt->num_chans; i++) {
		nlattr_nested = nla_nest_start(nl_skb, NAN_EVT_ATTR_MATCH_FAMCHAN);
		if (!nlattr_nested) {
			SLSI_ERR(sdev, "Error in nla_nest_start\n");
			/* Dont use slsi skb wrapper for this free */
			kfree_skb(nl_skb);
			kfree(hal_evt);
			return;
		}
		famchan = &hal_evt->famchan[i];
		res |= nla_put_u8(nl_skb, NAN_EVT_ATTR_MATCH_FAM_ENTRY_CONTROL, famchan->entry_control);
		res |= nla_put_u8(nl_skb, NAN_EVT_ATTR_MATCH_FAM_CLASS_VAL, famchan->class_val);
		res |= nla_put_u8(nl_skb, NAN_EVT_ATTR_MATCH_FAM_CHANNEL, famchan->channel);
		res |= nla_put_u8(nl_skb, NAN_EVT_ATTR_MATCH_FAM_MAPID, famchan->mapid);
		res |= nla_put_u32(nl_skb, NAN_EVT_ATTR_MATCH_FAM_AVAIL_INTERVAL_BITMAP,
				   famchan->avail_interval_bitmap);
	}

	res |= nla_put_u8(nl_skb, NAN_EVT_ATTR_MATCH_CLUSTER_ATTRIBUTE_LEN, hal_evt->cluster_attribute_len);
	if (hal_evt->cluster_attribute_len)
		res |= nla_put(nl_skb, NAN_EVT_ATTR_MATCH_CLUSTER_ATTRIBUTE, hal_evt->cluster_attribute_len,
			       hal_evt->cluster_attribute);

	if (res) {
		SLSI_ERR(sdev, "Error in nla_put*:%x\n", res);
		/* Dont use slsi skb wrapper for this free */
		kfree_skb(nl_skb);
		kfree(hal_evt);
		return;
	}

	nla_nest_end(nl_skb, nlattr_start);

	cfg80211_vendor_event(nl_skb, GFP_KERNEL);
	kfree(hal_evt);
}

static int slsi_configure_nd_offload(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev          *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = wdev->netdev;
	struct netdev_vif *ndev_vif;
	int                      ret = 0;
	int                      temp;
	int                      type;
	const struct nlattr      *attr;
	u8 nd_offload_enabled = 0;

	SLSI_DBG3(sdev, SLSI_GSCAN, "Received nd_offload command\n");

	if (!dev) {
		SLSI_ERR(sdev, "dev is NULL!!\n");
		return -EINVAL;
	}

	ndev_vif = netdev_priv(dev);
	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	if (!ndev_vif->activated || (ndev_vif->vif_type != FAPI_VIFTYPE_STATION) ||
	    (ndev_vif->sta.vif_status != SLSI_VIF_STATUS_CONNECTED)) {
		SLSI_DBG3(sdev, SLSI_GSCAN, "vif error\n");
		ret = -EPERM;
		goto exit;
	}

	nla_for_each_attr(attr, data, len, temp) {
		type = nla_type(attr);
		switch (type) {
		case SLSI_NL_ATTRIBUTE_ND_OFFLOAD_VALUE:
		{
			nd_offload_enabled = nla_get_u8(attr);
			break;
		}
		default:
			SLSI_ERR(sdev, "Invalid type : %d\n", type);
			ret = -EINVAL;
			goto exit;
		}
	}

	ndev_vif->sta.nd_offload_enabled = nd_offload_enabled;
	ret = slsi_mlme_set_ipv6_address(sdev, dev);
	if (ret < 0) {
		SLSI_ERR(sdev, "Configure nd_offload failed ret:%d nd_offload_enabled: %d\n", ret, nd_offload_enabled);
		ret = -EINVAL;
		goto exit;
	}
exit:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	return ret;
}

static int slsi_get_roaming_capabilities(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev          *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = wdev->netdev;
	struct netdev_vif *ndev_vif;
	int                      ret = 0;
	struct slsi_mib_value *values = NULL;
	struct slsi_mib_data mibrsp = { 0, NULL };
	struct slsi_mib_get_entry get_values[] = {{ SLSI_PSID_UNIFI_ROAM_BLACKLIST_SIZE, { 0, 0 } } };
	u32    max_blacklist_size = 0;
	u32    max_whitelist_size = 0;
	struct sk_buff *nl_skb;
	struct nlattr *nlattr_start;

	if (!dev) {
		SLSI_ERR(sdev, "dev is NULL!!\n");
		return -EINVAL;
	}

	ndev_vif = netdev_priv(dev);

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	mibrsp.dataLength = 10;
	mibrsp.data = kmalloc(mibrsp.dataLength, GFP_KERNEL);
	if (!mibrsp.data) {
		SLSI_ERR(sdev, "Cannot kmalloc %d bytes\n", mibrsp.dataLength);
		ret = -ENOMEM;
		goto exit;
	}
	values = slsi_read_mibs(sdev, NULL, get_values, ARRAY_SIZE(get_values), &mibrsp);
	if (values && (values[0].type == SLSI_MIB_TYPE_UINT ||  values[0].type == SLSI_MIB_TYPE_INT))
		max_blacklist_size = values[0].u.uintValue;
	nl_skb = cfg80211_vendor_cmd_alloc_reply_skb(sdev->wiphy, NLMSG_DEFAULT_SIZE);
	if (!nl_skb) {
		SLSI_ERR(sdev, "NO MEM for nl_skb!!!\n");
		ret = -ENOMEM;
		goto exit_with_mib_resp;
	}

	nlattr_start = nla_nest_start(nl_skb, NL80211_ATTR_VENDOR_DATA);
	if (!nlattr_start) {
		SLSI_ERR(sdev, "failed to put NL80211_ATTR_VENDOR_DATA\n");
		/* Dont use slsi skb wrapper for this free */
		kfree_skb(nl_skb);
		ret = -EINVAL;
		goto exit_with_mib_resp;
	}

	ret = nla_put_u32(nl_skb, SLSI_NL_ATTR_MAX_BLACKLIST_SIZE, max_blacklist_size);
	ret |= nla_put_u32(nl_skb, SLSI_NL_ATTR_MAX_WHITELIST_SIZE, max_whitelist_size);
	if (ret) {
		SLSI_ERR(sdev, "Error in nla_put*:%x\n", ret);
		/* Dont use slsi skb wrapper for this free */
		kfree_skb(nl_skb);
		goto exit_with_mib_resp;
	}

	ret = cfg80211_vendor_cmd_reply(nl_skb);
	if (ret)
		SLSI_ERR(sdev, "cfg80211_vendor_cmd_reply failed :%d\n", ret);
exit_with_mib_resp:
	kfree(mibrsp.data);
	kfree(values);
exit:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	return ret;
}

static int slsi_set_roaming_state(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev     *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device   *dev = wdev->netdev;
	int                 temp = 0;
	int                 type = 0;
	const struct nlattr *attr;
	int                 ret = 0;
	int                 roam_state = 0;

	if (!dev) {
		SLSI_WARN_NODEV("net_dev is NULL\n");
		return -EINVAL;
	}

	nla_for_each_attr(attr, data, len, temp) {
		type = nla_type(attr);
		switch (type) {
		case SLSI_NL_ATTR_ROAM_STATE:
			roam_state = nla_get_u8(attr);
			break;
		default:
			SLSI_ERR_NODEV("Unknown attribute: %d\n", type);
			ret = -EINVAL;
			goto exit;
		}
	}

	SLSI_DBG1_NODEV(SLSI_GSCAN, "SUBCMD_SET_ROAMING_STATE roam_state = %d\n", roam_state);
	ret = slsi_set_mib_roam(sdev, NULL, SLSI_PSID_UNIFI_ROAMING_ENABLED, roam_state);
	if (ret < 0)
		SLSI_ERR_NODEV("Failed to set roaming state\n");

exit:
	return ret;
}

#ifdef CONFIG_SCSC_WLAN_ENHANCED_LOGGING
static void slsi_on_ring_buffer_data(char *ring_name, char *buffer, int buffer_size,
				     struct scsc_wifi_ring_buffer_status *buffer_status, void *ctx)
{
	struct sk_buff *skb;
	int event_id = SLSI_NL80211_LOGGER_RING_EVENT;
	struct slsi_dev *sdev = ctx;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
	skb = cfg80211_vendor_event_alloc(sdev->wiphy, NULL, buffer_size, event_id, GFP_KERNEL);
#else
	skb = cfg80211_vendor_event_alloc(sdev->wiphy, buffer_size, event_id, GFP_KERNEL);
#endif
	if (!skb) {
		SLSI_ERR_NODEV("Failed to allocate skb for vendor event: %d\n", event_id);
		return;
	}

	if (nla_put(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_RING_STATUS, sizeof(*buffer_status), buffer_status) ||
	    nla_put(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_RING_DATA, buffer_size, buffer)) {
		SLSI_ERR_NODEV("Failed nla_put\n");
		slsi_kfree_skb(skb);
		return;
	}
	cfg80211_vendor_event(skb, GFP_KERNEL);
}

static void slsi_on_alert(char *buffer, int buffer_size, int err_code, void *ctx)
{
	struct sk_buff *skb;
	int event_id = SLSI_NL80211_LOGGER_FW_DUMP_EVENT;
	struct slsi_dev *sdev = ctx;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");
	SLSI_MUTEX_LOCK(sdev->logger_mutex);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
	skb = cfg80211_vendor_event_alloc(sdev->wiphy, NULL, buffer_size, event_id, GFP_KERNEL);
#else
	skb = cfg80211_vendor_event_alloc(sdev->wiphy, buffer_size, event_id, GFP_KERNEL);
#endif
	if (!skb) {
		SLSI_ERR_NODEV("Failed to allocate skb for vendor event: %d\n", event_id);
		goto exit;
	}

	if (nla_put_u32(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_FW_DUMP_LEN, buffer_size) ||
	    nla_put(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_RING_DATA, buffer_size, buffer)) {
		SLSI_ERR_NODEV("Failed nla_put\n");
		slsi_kfree_skb(skb);
		goto exit;
	}
	cfg80211_vendor_event(skb, GFP_KERNEL);
exit:
	SLSI_MUTEX_UNLOCK(sdev->logger_mutex);
}

static void slsi_on_firmware_memory_dump(char *buffer, int buffer_size, void *ctx)
{
	SLSI_ERR_NODEV("slsi_on_firmware_memory_dump\n");
	kfree(mem_dump_buffer);
	mem_dump_buffer = NULL;
	mem_dump_buffer = kmalloc(buffer_size, GFP_KERNEL);
	if (!mem_dump_buffer) {
		SLSI_ERR_NODEV("Failed to allocate memory for mem_dump_buffer\n");
		return;
	}
	mem_dump_buffer_size = buffer_size;
	memcpy(mem_dump_buffer, buffer, mem_dump_buffer_size);
}

static void slsi_on_driver_memory_dump(char *buffer, int buffer_size, void *ctx)
{
	SLSI_ERR_NODEV("slsi_on_driver_memory_dump\n");
	kfree(mem_dump_buffer);
	mem_dump_buffer = NULL;
	mem_dump_buffer_size = buffer_size;
	mem_dump_buffer = kmalloc(mem_dump_buffer_size, GFP_KERNEL);
	if (!mem_dump_buffer) {
		SLSI_ERR_NODEV("Failed to allocate memory for mem_dump_buffer\n");
		return;
	}
	memcpy(mem_dump_buffer, buffer, mem_dump_buffer_size);
}

static int slsi_enable_logging(struct slsi_dev *sdev, bool enable)
{
	int                  status = 0;
#ifdef ENABLE_WIFI_LOGGER_MIB_WRITE
	struct slsi_mib_data mib_data = { 0, NULL };

	SLSI_DBG3(sdev, SLSI_GSCAN, "Value of enable is : %d\n", enable);
	status = slsi_mib_encode_bool(&mib_data, SLSI_PSID_UNIFI_LOGGER_ENABLED, enable, 0);
	if (status != SLSI_MIB_STATUS_SUCCESS) {
		SLSI_ERR(sdev, "slsi_enable_logging failed: no mem for MIB\n");
		status = -ENOMEM;
		goto exit;
	}
	status = slsi_mlme_set(sdev, NULL, mib_data.data, mib_data.dataLength);
	kfree(mib_data.data);
	if (status)
		SLSI_ERR(sdev, "Err setting unifiLoggerEnabled MIB. error = %d\n", status);

exit:
	return status;
#else
	SLSI_DBG3(sdev, SLSI_GSCAN, "UnifiLoggerEnabled MIB write disabled\n");
	return status;
#endif
}

static int slsi_start_logging(struct wiphy *wiphy, struct wireless_dev *wdev, const void  *data, int len)
{
	struct slsi_dev     *sdev = SDEV_FROM_WIPHY(wiphy);
	int                 ret = 0;
	int                 temp = 0;
	int                 type = 0;
	char                ring_name[32] = {0};
	int                 verbose_level = 0;
	int                 ring_flags = 0;
	int                 max_interval_sec = 0;
	int                 min_data_size = 0;
	const struct nlattr *attr;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");
	SLSI_MUTEX_LOCK(sdev->logger_mutex);
	nla_for_each_attr(attr, data, len, temp) {
		type = nla_type(attr);
		switch (type) {
		case SLSI_ENHANCED_LOGGING_ATTRIBUTE_RING_NAME:
			strncpy(ring_name, nla_data(attr), MIN(sizeof(ring_name) - 1, nla_len(attr)));
			break;
		case SLSI_ENHANCED_LOGGING_ATTRIBUTE_VERBOSE_LEVEL:
			verbose_level = nla_get_u32(attr);
			break;
		case SLSI_ENHANCED_LOGGING_ATTRIBUTE_RING_FLAGS:
			ring_flags = nla_get_u32(attr);
			break;
		case SLSI_ENHANCED_LOGGING_ATTRIBUTE_LOG_MAX_INTERVAL:
			max_interval_sec = nla_get_u32(attr);
			break;
		case SLSI_ENHANCED_LOGGING_ATTRIBUTE_LOG_MIN_DATA_SIZE:
			min_data_size = nla_get_u32(attr);
			break;
		default:
			SLSI_ERR(sdev, "Unknown type: %d\n", type);
			ret = -EINVAL;
			goto exit;
		}
	}
	ret = scsc_wifi_set_log_handler(slsi_on_ring_buffer_data, sdev);
	if (ret < 0) {
		SLSI_ERR(sdev, "scsc_wifi_set_log_handler failed ret: %d\n", ret);
		goto exit;
	}
	ret = scsc_wifi_set_alert_handler(slsi_on_alert, sdev);
	if (ret < 0) {
		SLSI_ERR(sdev, "Warning : scsc_wifi_set_alert_handler failed ret: %d\n", ret);
	}
	ret = slsi_enable_logging(sdev, 1);
	if (ret < 0) {
		SLSI_ERR(sdev, "slsi_enable_logging for enable = 1, failed ret: %d\n", ret);
		goto exit_with_reset_alert_handler;
	}
	ret = scsc_wifi_start_logging(verbose_level, ring_flags, max_interval_sec, min_data_size, ring_name);
	if (ret < 0) {
		SLSI_ERR(sdev, "scsc_wifi_start_logging failed ret: %d\n", ret);
		goto exit_with_disable_logging;
	} else {
		goto exit;
	}
exit_with_disable_logging:
	ret = slsi_enable_logging(sdev, 0);
	if (ret < 0)
		SLSI_ERR(sdev, "slsi_enable_logging for enable = 0, failed ret: %d\n", ret);
exit_with_reset_alert_handler:
	ret = scsc_wifi_reset_alert_handler();
	if (ret < 0)
		SLSI_ERR(sdev, "Warning : scsc_wifi_reset_alert_handler failed ret: %d\n", ret);
	ret = scsc_wifi_reset_log_handler();
	if (ret < 0)
		SLSI_ERR(sdev, "scsc_wifi_reset_log_handler failed ret: %d\n", ret);
exit:
	SLSI_MUTEX_UNLOCK(sdev->logger_mutex);
	return ret;
}

static int slsi_reset_logging(struct wiphy *wiphy, struct wireless_dev *wdev, const void  *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	int             ret = 0;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");
	SLSI_MUTEX_LOCK(sdev->logger_mutex);
	ret = slsi_enable_logging(sdev, 0);
	if (ret < 0)
		SLSI_ERR(sdev, "slsi_enable_logging for enable = 0, failed ret: %d\n", ret);
	ret = scsc_wifi_reset_log_handler();
	if (ret < 0)
		SLSI_ERR(sdev, "scsc_wifi_reset_log_handler failed ret: %d\n", ret);
	ret = scsc_wifi_reset_alert_handler();
	if (ret < 0)
		SLSI_ERR(sdev, "Warning : scsc_wifi_reset_alert_handler failed ret: %d\n", ret);
	SLSI_MUTEX_UNLOCK(sdev->logger_mutex);
	return ret;
}

static int slsi_trigger_fw_mem_dump(struct wiphy *wiphy, struct wireless_dev *wdev, const void  *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	int             ret = 0;
	struct sk_buff  *skb = NULL;
	int             length = 100;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");
	SLSI_MUTEX_LOCK(sdev->logger_mutex);

	ret = scsc_wifi_get_firmware_memory_dump(slsi_on_firmware_memory_dump, sdev);
	if (ret) {
		SLSI_ERR(sdev, "scsc_wifi_get_firmware_memory_dump failed : %d\n", ret);
		goto exit;
	}

	/* Alloc the SKB for vendor_event */
	skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, length);
	if (!skb) {
		SLSI_ERR_NODEV("Failed to allocate skb for Vendor event\n");
		ret = -ENOMEM;
		goto exit;
	}

	if (nla_put_u32(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_FW_DUMP_LEN, mem_dump_buffer_size)) {
		SLSI_ERR_NODEV("Failed nla_put\n");
		slsi_kfree_skb(skb);
		ret = -EINVAL;
		goto exit;
	}

	ret = cfg80211_vendor_cmd_reply(skb);

	if (ret)
		SLSI_ERR(sdev, "Vendor Command reply failed ret:%d\n", ret);

exit:
	SLSI_MUTEX_UNLOCK(sdev->logger_mutex);
	return ret;
}

static int slsi_get_fw_mem_dump(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev     *sdev = SDEV_FROM_WIPHY(wiphy);
	int                 ret = 0;
	int                 temp = 0;
	int                 type = 0;
	int                 buf_len = 0;
	void __user         *user_buf = NULL;
	const struct nlattr *attr;
	struct sk_buff      *skb;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");
	SLSI_MUTEX_LOCK(sdev->logger_mutex);
	nla_for_each_attr(attr, data, len, temp) {
		type = nla_type(attr);
		switch (type) {
		case SLSI_ENHANCED_LOGGING_ATTRIBUTE_FW_DUMP_LEN:
			buf_len = nla_get_u32(attr);
			break;
		case SLSI_ENHANCED_LOGGING_ATTRIBUTE_FW_DUMP_DATA:
			user_buf = (void __user *)(unsigned long)nla_get_u64(attr);
			break;
		default:
			SLSI_ERR(sdev, "Unknown type: %d\n", type);
			SLSI_MUTEX_UNLOCK(sdev->logger_mutex);
			return -EINVAL;
		}
	}
	if (buf_len > 0 && user_buf) {
		ret = copy_to_user(user_buf, mem_dump_buffer, buf_len);
		if (ret) {
			SLSI_ERR(sdev, "failed to copy memdump into user buffer : %d\n", ret);
			goto exit;
		}

		/* Alloc the SKB for vendor_event */
		skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, 100);
		if (!skb) {
			SLSI_ERR_NODEV("Failed to allocate skb for Vendor event\n");
			ret = -ENOMEM;
			goto exit;
		}

		/* Indicate the memdump is successfully copied */
		if (nla_put(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_FW_DUMP_DATA, sizeof(ret), &ret)) {
			SLSI_ERR_NODEV("Failed nla_put\n");
			slsi_kfree_skb(skb);
			ret = -EINVAL;
			goto exit;
		}

		ret = cfg80211_vendor_cmd_reply(skb);

		if (ret)
			SLSI_ERR(sdev, "Vendor Command reply failed ret:%d\n", ret);
	}

exit:
	kfree(mem_dump_buffer);
	mem_dump_buffer = NULL;
	SLSI_MUTEX_UNLOCK(sdev->logger_mutex);
	return ret;
}

static int slsi_trigger_driver_mem_dump(struct wiphy *wiphy, struct wireless_dev *wdev, const void  *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	int             ret = 0;
	struct sk_buff  *skb = NULL;
	int             length = 100;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");
	SLSI_MUTEX_LOCK(sdev->logger_mutex);

	ret = scsc_wifi_get_driver_memory_dump(slsi_on_driver_memory_dump, sdev);
	if (ret) {
		SLSI_ERR(sdev, "scsc_wifi_get_driver_memory_dump failed : %d\n", ret);
		goto exit;
	}

	/* Alloc the SKB for vendor_event */
	skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, length);
	if (!skb) {
		SLSI_ERR_NODEV("Failed to allocate skb for Vendor event\n");
		ret = -ENOMEM;
		goto exit;
	}

	if (nla_put_u32(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_DRIVER_DUMP_LEN, mem_dump_buffer_size)) {
		SLSI_ERR_NODEV("Failed nla_put\n");
		slsi_kfree_skb(skb);
		ret = -EINVAL;
		goto exit;
	}

	ret = cfg80211_vendor_cmd_reply(skb);

	if (ret)
		SLSI_ERR(sdev, "Vendor Command reply failed ret:%d\n", ret);

exit:
	SLSI_MUTEX_UNLOCK(sdev->logger_mutex);
	return ret;
}

static int slsi_get_driver_mem_dump(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev     *sdev = SDEV_FROM_WIPHY(wiphy);
	int                 ret = 0;
	int                 temp = 0;
	int                 type = 0;
	int                 buf_len = 0;
	void __user         *user_buf = NULL;
	const struct nlattr *attr;
	struct sk_buff      *skb;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");
	SLSI_MUTEX_LOCK(sdev->logger_mutex);
	nla_for_each_attr(attr, data, len, temp) {
		type = nla_type(attr);
		switch (type) {
		case SLSI_ENHANCED_LOGGING_ATTRIBUTE_DRIVER_DUMP_LEN:
			buf_len = nla_get_u32(attr);
			break;
		case SLSI_ENHANCED_LOGGING_ATTRIBUTE_DRIVER_DUMP_DATA:
			user_buf = (void __user *)(unsigned long)nla_get_u64(attr);
			break;
		default:
			SLSI_ERR(sdev, "Unknown type: %d\n", type);
			SLSI_MUTEX_UNLOCK(sdev->logger_mutex);
			return -EINVAL;
		}
	}
	if (buf_len > 0 && user_buf) {
		ret = copy_to_user(user_buf, mem_dump_buffer, buf_len);
		if (ret) {
			SLSI_ERR(sdev, "failed to copy memdump into user buffer : %d\n", ret);
			goto exit;
		}

		/* Alloc the SKB for vendor_event */
		skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, 100);
		if (!skb) {
			SLSI_ERR_NODEV("Failed to allocate skb for Vendor event\n");
			ret = -ENOMEM;
			goto exit;
		}

		/* Indicate the memdump is successfully copied */
		if (nla_put(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_DRIVER_DUMP_DATA, sizeof(ret), &ret)) {
			SLSI_ERR_NODEV("Failed nla_put\n");
			slsi_kfree_skb(skb);
			ret = -EINVAL;
			goto exit;
		}

		ret = cfg80211_vendor_cmd_reply(skb);

		if (ret)
			SLSI_ERR(sdev, "Vendor Command reply failed ret:%d\n", ret);
	}

exit:
	kfree(mem_dump_buffer);
	mem_dump_buffer = NULL;
	SLSI_MUTEX_UNLOCK(sdev->logger_mutex);
	return ret;
}

static int slsi_get_version(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev     *sdev = SDEV_FROM_WIPHY(wiphy);
	int                 ret = 0;
	int                 temp = 0;
	int                 type = 0;
	int                 buffer_size = 1024;
	bool                log_version = false;
	char                *buffer;
	const struct nlattr *attr;

	buffer = kzalloc(buffer_size, GFP_KERNEL);
	if (!buffer) {
		SLSI_ERR(sdev, "No mem. Size:%d\n", buffer_size);
		return -ENOMEM;
	}
	SLSI_MUTEX_LOCK(sdev->logger_mutex);
	nla_for_each_attr(attr, data, len, temp) {
		type = nla_type(attr);
		switch (type) {
		case SLSI_ENHANCED_LOGGING_ATTRIBUTE_DRIVER_VERSION:
			log_version = true;
			break;
		case SLSI_ENHANCED_LOGGING_ATTRIBUTE_FW_VERSION:
			log_version = false;
			break;
		default:
			SLSI_ERR(sdev, "Unknown type: %d\n", type);
			ret = -EINVAL;
			goto exit;
		}
	}

	if (log_version)
		ret = scsc_wifi_get_driver_version(buffer, buffer_size);
	else
		ret = scsc_wifi_get_firmware_version(buffer, buffer_size);

	if (ret < 0) {
		SLSI_ERR(sdev, "failed to get the version %d\n", ret);
		goto exit;
	}

	ret = slsi_vendor_cmd_reply(wiphy, buffer, strlen(buffer));
exit:
	kfree(buffer);
	SLSI_MUTEX_UNLOCK(sdev->logger_mutex);
	return ret;
}

static int slsi_get_ring_buffers_status(struct wiphy *wiphy, struct wireless_dev *wdev, const void  *data, int len)
{
	struct slsi_dev                     *sdev = SDEV_FROM_WIPHY(wiphy);
	int                                 ret = 0;
	int                                 num_rings = 10;
	struct sk_buff                      *skb;
	struct scsc_wifi_ring_buffer_status status[num_rings];

	SLSI_DBG1(sdev, SLSI_GSCAN, "\n");
	SLSI_MUTEX_LOCK(sdev->logger_mutex);
	memset(status, 0, sizeof(struct scsc_wifi_ring_buffer_status) * num_rings);
	ret = scsc_wifi_get_ring_buffers_status(&num_rings, status);
	if (ret < 0) {
		SLSI_ERR(sdev, "scsc_wifi_get_ring_buffers_status failed ret:%d\n", ret);
		goto exit;
	}

	/* Alloc the SKB for vendor_event */
	skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, 700);
	if (!skb) {
		SLSI_ERR_NODEV("Failed to allocate skb for Vendor event\n");
		ret = -ENOMEM;
		goto exit;
	}

	/* Indicate that the ring count and ring buffers status is successfully copied */
	if (nla_put_u8(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_RING_NUM, num_rings) ||
	    nla_put(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_RING_STATUS, sizeof(status[0]) * num_rings, status)) {
		SLSI_ERR_NODEV("Failed nla_put\n");
		slsi_kfree_skb(skb);
		ret = -EINVAL;
		goto exit;
	}

	ret = cfg80211_vendor_cmd_reply(skb);

	if (ret)
		SLSI_ERR(sdev, "Vendor Command reply failed ret:%d\n", ret);
exit:
	SLSI_MUTEX_UNLOCK(sdev->logger_mutex);
	return ret;
}

static int slsi_get_ring_data(struct wiphy *wiphy, struct wireless_dev *wdev, const void  *data, int len)
{
	struct slsi_dev     *sdev = SDEV_FROM_WIPHY(wiphy);
	int                 ret = 0;
	int                 temp = 0;
	int                 type = 0;
	char                ring_name[32] = {0};
	const struct nlattr *attr;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");
	SLSI_MUTEX_LOCK(sdev->logger_mutex);
	nla_for_each_attr(attr, data, len, temp) {
		type = nla_type(attr);
		switch (type) {
		case SLSI_ENHANCED_LOGGING_ATTRIBUTE_RING_NAME:
			strncpy(ring_name, nla_data(attr), MIN(sizeof(ring_name) - 1, nla_len(attr)));
			break;
		default:
			SLSI_ERR(sdev, "Unknown type: %d\n", type);
			goto exit;
		}
	}

	ret = scsc_wifi_get_ring_data(ring_name);
	if (ret < 0)
		SLSI_ERR(sdev, "trigger_get_data failed ret:%d\n", ret);
exit:
	SLSI_MUTEX_UNLOCK(sdev->logger_mutex);
	return ret;
}

static int slsi_get_logger_supported_feature_set(struct wiphy *wiphy, struct wireless_dev *wdev,
						 const void  *data, int len)
{
	struct slsi_dev *sdev = SDEV_FROM_WIPHY(wiphy);
	int             ret = 0;
	u32             supported_features = 0;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");
	SLSI_MUTEX_LOCK(sdev->logger_mutex);
	ret = scsc_wifi_get_logger_supported_feature_set(&supported_features);
	if (ret < 0) {
		SLSI_ERR(sdev, "scsc_wifi_get_logger_supported_feature_set failed ret:%d\n", ret);
		goto exit;
	}
	ret = slsi_vendor_cmd_reply(wiphy, &supported_features, sizeof(supported_features));
exit:
	SLSI_MUTEX_UNLOCK(sdev->logger_mutex);
	return ret;
}

void slsi_rx_event_log_indication(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb)
{
	u16 event_id = 0;
	u64 timestamp = 0;

	SLSI_MUTEX_LOCK(sdev->logger_mutex);
	event_id = fapi_get_s16(skb, u.mlme_event_log_ind.event);
	timestamp = fapi_get_u64(skb, u.mlme_event_log_ind.timestamp);
	SLSI_DBG3(sdev, SLSI_GSCAN,
		  "slsi_rx_event_log_indication, event id = %d, timestamp = %d\n", event_id, timestamp);
	SCSC_WLOG_FW_EVENT(WLOG_NORMAL, event_id, timestamp, fapi_get_data(skb), fapi_get_datalen(skb));
	slsi_kfree_skb(skb);
	SLSI_MUTEX_UNLOCK(sdev->logger_mutex);
}

static int slsi_start_pkt_fate_monitoring(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	int                  ret = 0;
#ifdef ENABLE_WIFI_LOGGER_MIB_WRITE
	struct slsi_dev      *sdev = SDEV_FROM_WIPHY(wiphy);
	struct slsi_mib_data mib_data = { 0, NULL };

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");
	SLSI_MUTEX_LOCK(sdev->logger_mutex);
	ret = slsi_mib_encode_bool(&mib_data, SLSI_PSID_UNIFI_TX_DATA_CONFIRM, 1, 0);
	if (ret != SLSI_MIB_STATUS_SUCCESS) {
		SLSI_ERR(sdev, "Failed to set UnifiTxDataConfirm MIB : no mem for MIB\n");
		ret = -ENOMEM;
		goto exit;
	}

	ret = slsi_mlme_set(sdev, NULL, mib_data.data, mib_data.dataLength);

	if (ret) {
		SLSI_ERR(sdev, "Err setting UnifiTxDataConfirm MIB. error = %d\n", ret);
		goto exit;
	}

	ret = scsc_wifi_start_pkt_fate_monitoring();
	if (ret < 0) {
		SLSI_ERR(sdev, "scsc_wifi_start_pkt_fate_monitoring failed, ret=%d\n", ret);

		// Resetting the SLSI_PSID_UNIFI_TX_DATA_CONFIRM mib back to 0.
		mib_data.dataLength = 0;
		mib_data.data = NULL;
		ret = slsi_mib_encode_bool(&mib_data, SLSI_PSID_UNIFI_TX_DATA_CONFIRM, 1, 0);
		if (ret != SLSI_MIB_STATUS_SUCCESS) {
			SLSI_ERR(sdev, "Failed to set UnifiTxDataConfirm MIB : no mem for MIB\n");
			ret = -ENOMEM;
			goto exit;
		}

		ret = slsi_mlme_set(sdev, NULL, mib_data.data, mib_data.dataLength);

		if (ret) {
			SLSI_ERR(sdev, "Err setting UnifiTxDataConfirm MIB. error = %d\n", ret);
			goto exit;
		}
	}
exit:
	kfree(mib_data.data);
	SLSI_MUTEX_UNLOCK(sdev->logger_mutex);
	return ret;
#else
	SLSI_ERR_NODEV("slsi_start_pkt_fate_monitoring : UnifiTxDataConfirm MIB write disabled\n");
	return ret;
#endif
}

static int slsi_get_tx_pkt_fates(struct wiphy *wiphy, struct wireless_dev *wdev, const void  *data, int len)
{
	struct slsi_dev     *sdev = SDEV_FROM_WIPHY(wiphy);
	int                 ret = 0;
	int                 temp = 0;
	int                 type = 0;
	void __user         *user_buf = NULL;
	u32                 req_count = 0;
	size_t              provided_count = 0;
	struct sk_buff      *skb;
	const struct nlattr *attr;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");
	SLSI_MUTEX_LOCK(sdev->logger_mutex);
	nla_for_each_attr(attr, data, len, temp) {
		type = nla_type(attr);
		switch (type) {
		case SLSI_ENHANCED_LOGGING_ATTRIBUTE_PKT_FATE_NUM:
			req_count = nla_get_u32(attr);
			break;
		case SLSI_ENHANCED_LOGGING_ATTRIBUTE_PKT_FATE_DATA:
			user_buf = (void __user *)(unsigned long)nla_get_u64(attr);
			break;
		default:
			SLSI_ERR(sdev, "Unknown type: %d\n", type);
			ret = -EINVAL;
			goto exit;
		}
	}

	ret = scsc_wifi_get_tx_pkt_fates(user_buf, req_count, &provided_count);
	if (ret < 0) {
		SLSI_ERR(sdev, "scsc_wifi_get_tx_pkt_fates failed ret: %d\n", ret);
		goto exit;
	}

	/* Alloc the SKB for vendor_event */
	skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, 200);
	if (!skb) {
		SLSI_ERR_NODEV("Failed to allocate skb for Vendor event\n");
		ret = -ENOMEM;
		goto exit;
	}

	if (nla_put(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_PKT_FATE_NUM, sizeof(provided_count), &provided_count)) {
		SLSI_ERR_NODEV("Failed nla_put\n");
		slsi_kfree_skb(skb);
		ret = -EINVAL;
		goto exit;
	}

	ret = cfg80211_vendor_cmd_reply(skb);

	if (ret)
		SLSI_ERR(sdev, "Vendor Command reply failed ret:%d\n", ret);
exit:
	SLSI_MUTEX_UNLOCK(sdev->logger_mutex);
	return ret;
}

static int slsi_get_rx_pkt_fates(struct wiphy *wiphy, struct wireless_dev *wdev, const void  *data, int len)
{
	struct slsi_dev     *sdev = SDEV_FROM_WIPHY(wiphy);
	int                 ret = 0;
	int                 temp = 0;
	int                 type = 0;
	void __user         *user_buf = NULL;
	u32                 req_count = 0;
	size_t              provided_count = 0;
	struct sk_buff      *skb;
	const struct nlattr *attr;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");
	SLSI_MUTEX_LOCK(sdev->logger_mutex);
	nla_for_each_attr(attr, data, len, temp) {
		type = nla_type(attr);
		switch (type) {
		case SLSI_ENHANCED_LOGGING_ATTRIBUTE_PKT_FATE_NUM:
			req_count = nla_get_u32(attr);
			break;
		case SLSI_ENHANCED_LOGGING_ATTRIBUTE_PKT_FATE_DATA:
			user_buf = (void __user *)(unsigned long)nla_get_u64(attr);
			break;
		default:
			SLSI_ERR(sdev, "Unknown type: %d\n", type);
			ret = -EINVAL;
			goto exit;
		}
	}

	ret = scsc_wifi_get_rx_pkt_fates(user_buf, req_count, &provided_count);
	if (ret < 0) {
		SLSI_ERR(sdev, "scsc_wifi_get_rx_pkt_fates failed ret: %d\n", ret);
		goto exit;
	}

	/* Alloc the SKB for vendor_event */
	skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, 200);
	if (!skb) {
		SLSI_ERR_NODEV("Failed to allocate skb for Vendor event\n");
		ret = -ENOMEM;
		goto exit;
	}

	if (nla_put(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_PKT_FATE_NUM, sizeof(provided_count), &provided_count)) {
		SLSI_ERR_NODEV("Failed nla_put\n");
		slsi_kfree_skb(skb);
		ret  = -EINVAL;
		goto exit;
	}

	ret = cfg80211_vendor_cmd_reply(skb);

	if (ret)
		SLSI_ERR(sdev, "Vendor Command reply failed ret:%d\n", ret);
exit:
	SLSI_MUTEX_UNLOCK(sdev->logger_mutex);
	return ret;
}

static int slsi_get_wake_reason_stats(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev     *sdev = SDEV_FROM_WIPHY(wiphy);
	struct slsi_wlan_driver_wake_reason_cnt wake_reason_count;
	int                 ret = 0;
	int                 temp = 0;
	int                 type = 0;
	const struct nlattr *attr;
	struct sk_buff      *skb;

	SLSI_DBG3(sdev, SLSI_GSCAN, "\n");
	// Initialising the wake_reason_count structure values to 0.
	memset(&wake_reason_count, 0, sizeof(struct slsi_wlan_driver_wake_reason_cnt));

	SLSI_MUTEX_LOCK(sdev->logger_mutex);
	nla_for_each_attr(attr, data, len, temp) {
		type = nla_type(attr);
		switch (type) {
		case SLSI_ENHANCED_LOGGING_ATTRIBUTE_WAKE_STATS_CMD_EVENT_WAKE_CNT_SZ:
			wake_reason_count.cmd_event_wake_cnt_sz = nla_get_u32(attr);
			break;
		case SLSI_ENHANCED_LOGGING_ATTRIBUTE_WAKE_STATS_DRIVER_FW_LOCAL_WAKE_CNT_SZ:
			wake_reason_count.driver_fw_local_wake_cnt_sz = nla_get_u32(attr);
			break;
		default:
			SLSI_ERR(sdev, "Unknown type: %d\n", type);
			ret = -EINVAL;
			goto exit;
		}
	}

	if (ret < 0) {
		SLSI_ERR(sdev, "Failed to get wake reason stats :  %d\n", ret);
		goto exit;
	}

	/* Alloc the SKB for vendor_event */
	skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, 700);
	if (!skb) {
		SLSI_ERR_NODEV("Failed to allocate skb for Vendor event\n");
		ret = -ENOMEM;
		goto exit;
	}

	if (nla_put_u32(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_WAKE_STATS_TOTAL_CMD_EVENT_WAKE,
			wake_reason_count.total_cmd_event_wake)) {
		SLSI_ERR_NODEV("Failed nla_put\n");
		slsi_kfree_skb(skb);
		ret  = -EINVAL;
		goto exit;
	}
	if (nla_put(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_WAKE_STATS_CMD_EVENT_WAKE_CNT_PTR, 0,
		    wake_reason_count.cmd_event_wake_cnt)) {
		SLSI_ERR_NODEV("Failed nla_put\n");
		slsi_kfree_skb(skb);
		ret  = -EINVAL;
		goto exit;
	}
	if (nla_put_u32(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_WAKE_STATS_TOTAL_DRIVER_FW_LOCAL_WAKE,
			wake_reason_count.total_driver_fw_local_wake)) {
		SLSI_ERR_NODEV("Failed nla_put\n");
		slsi_kfree_skb(skb);
		ret  = -EINVAL;
		goto exit;
	}
	if (nla_put(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_WAKE_STATS_DRIVER_FW_LOCAL_WAKE_CNT_PTR, 0,
		    wake_reason_count.driver_fw_local_wake_cnt)) {
		SLSI_ERR_NODEV("Failed nla_put\n");
		slsi_kfree_skb(skb);
		ret  = -EINVAL;
		goto exit;
	}
	if (nla_put_u32(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_WAKE_STATS_TOTAL_RX_DATA_WAKE,
			wake_reason_count.total_rx_data_wake) ||
	    nla_put_u32(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_WAKE_STATS_RX_UNICAST_CNT,
			wake_reason_count.rx_wake_details.rx_unicast_cnt) ||
	    nla_put_u32(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_WAKE_STATS_RX_MULTICAST_CNT,
			wake_reason_count.rx_wake_details.rx_multicast_cnt) ||
	    nla_put_u32(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_WAKE_STATS_RX_BROADCAST_CNT,
			wake_reason_count.rx_wake_details.rx_broadcast_cnt) ||
	    nla_put_u32(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_WAKE_STATS_ICMP_PKT,
			wake_reason_count.rx_wake_pkt_classification_info.icmp_pkt) ||
	    nla_put_u32(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_WAKE_STATS_ICMP6_PKT,
			wake_reason_count.rx_wake_pkt_classification_info.icmp6_pkt) ||
	    nla_put_u32(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_WAKE_STATS_ICMP6_RA,
			wake_reason_count.rx_wake_pkt_classification_info.icmp6_ra) ||
	    nla_put_u32(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_WAKE_STATS_ICMP6_NA,
			wake_reason_count.rx_wake_pkt_classification_info.icmp6_na) ||
	    nla_put_u32(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_WAKE_STATS_ICMP6_NS,
			wake_reason_count.rx_wake_pkt_classification_info.icmp6_ns) ||
	    nla_put_u32(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_WAKE_STATS_ICMP4_RX_MULTICAST_CNT,
			wake_reason_count.rx_multicast_wake_pkt_info.ipv4_rx_multicast_addr_cnt) ||
	    nla_put_u32(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_WAKE_STATS_ICMP6_RX_MULTICAST_CNT,
			wake_reason_count.rx_multicast_wake_pkt_info.ipv6_rx_multicast_addr_cnt) ||
	    nla_put_u32(skb, SLSI_ENHANCED_LOGGING_ATTRIBUTE_WAKE_STATS_OTHER_RX_MULTICAST_CNT,
			wake_reason_count.rx_multicast_wake_pkt_info.other_rx_multicast_addr_cnt)) {
		SLSI_ERR_NODEV("Failed nla_put\n");
		slsi_kfree_skb(skb);
		ret  = -EINVAL;
		goto exit;
	}
	ret = cfg80211_vendor_cmd_reply(skb);

	if (ret)
		SLSI_ERR(sdev, "Vendor Command reply failed ret:%d\n", ret);
exit:
	SLSI_MUTEX_UNLOCK(sdev->logger_mutex);
	return ret;
}

#endif /* CONFIG_SCSC_WLAN_ENHANCED_LOGGING */

static int slsi_acs_validate_width_hw_mode(struct slsi_acs_request *request)
{
	if (request->hw_mode != SLSI_ACS_MODE_IEEE80211A && request->hw_mode != SLSI_ACS_MODE_IEEE80211B &&
	    request->hw_mode != SLSI_ACS_MODE_IEEE80211G)
		return -EINVAL;
	if (request->ch_width != 20 && request->ch_width != 40 && request->ch_width != 80)
		return -EINVAL;
	return 0;
}

static int slsi_acs_init(struct wiphy *wiphy,
			 struct wireless_dev *wdev, const void *data, int len)
{
	struct slsi_dev    *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = wdev->netdev;
	struct netdev_vif  *ndev_vif;
	struct slsi_acs_request *request;
	int                      temp;
	int                      type;
	const struct nlattr      *attr;
	int r = 0;
	u32 *freq_list;
	int freq_list_len = 0;

	SLSI_INFO(sdev, "SUBCMD_ACS_INIT Received\n");
	if (slsi_is_test_mode_enabled()) {
		SLSI_ERR(sdev, "Not supported in WlanLite mode\n");
		return -EOPNOTSUPP;
	}
	if (wdev->iftype != NL80211_IFTYPE_AP) {
		SLSI_ERR(sdev, "Invalid iftype: %d\n", wdev->iftype);
		return -EINVAL;
	}
	if (!dev) {
		SLSI_ERR(sdev, "Dev not found!\n");
		return -ENODEV;
	}
	request = kcalloc(1, sizeof(*request), GFP_KERNEL);
	if (!request) {
		SLSI_ERR(sdev, "No memory for request!");
		return -ENOMEM;
	}
	ndev_vif = netdev_priv(dev);

	SLSI_MUTEX_LOCK(ndev_vif->scan_mutex);
	nla_for_each_attr(attr, data, len, temp) {
		type = nla_type(attr);
		switch (type) {
		case SLSI_ACS_ATTR_HW_MODE:
		{
			request->hw_mode = nla_get_u8(attr);
			SLSI_INFO(sdev, "ACS hw mode: %d\n", request->hw_mode);
			break;
		}
		case SLSI_ACS_ATTR_CHWIDTH:
		{
			request->ch_width = nla_get_u16(attr);
			SLSI_INFO(sdev, "ACS ch_width: %d\n", request->ch_width);
			break;
		}
		case SLSI_ACS_ATTR_FREQ_LIST:
		{
			freq_list =  kmalloc(nla_len(attr), GFP_KERNEL);
			if (!freq_list) {
				SLSI_ERR(sdev, "No memory for frequency list!");
				kfree(request);
				SLSI_MUTEX_UNLOCK(ndev_vif->scan_mutex);
				return -ENOMEM;
			}
			memcpy(freq_list, nla_data(attr), nla_len(attr));
			freq_list_len = nla_len(attr) / sizeof(u32);
			SLSI_INFO(sdev, "ACS freq_list_len: %d\n", freq_list_len);
			break;
		}
		default:
			if (type > SLSI_ACS_ATTR_MAX)
				SLSI_ERR(sdev, "Invalid type : %d\n", type);
			break;
		}
	}

	r = slsi_acs_validate_width_hw_mode(request);
	if (r == 0 && freq_list_len) {
		struct ieee80211_channel *channels[freq_list_len];
		struct slsi_acs_chan_info ch_info[MAX_CHAN_VALUE_ACS];
		int i = 0, num_channels = 0;
		int idx;
		u32 chan_flags = (IEEE80211_CHAN_INDOOR_ONLY | IEEE80211_CHAN_RADAR |
					      IEEE80211_CHAN_DISABLED |
#if LINUX_VERSION_CODE <= KERNEL_VERSION(3, 10, 13)
					      IEEE80211_CHAN_PASSIVE_SCAN
#else
					      IEEE80211_CHAN_NO_IR
#endif
					     );

		memset(channels, 0, sizeof(channels));
		memset(&ch_info, 0, sizeof(ch_info));
		for (i = 0; i < freq_list_len; i++) {
			channels[num_channels] = ieee80211_get_channel(wiphy, freq_list[i]);
			if (!channels[num_channels]) {
				SLSI_INFO(sdev, "Ignore invalid freq:%d in freq list\n", freq_list[i]);
			} else if (channels[num_channels]->flags & chan_flags) {
				SLSI_INFO(sdev, "Skip invalid channel:%d for ACS\n", channels[num_channels]->hw_value);
			} else {
				idx = slsi_find_chan_idx(channels[num_channels]->hw_value, request->hw_mode);
				ch_info[idx].chan = channels[num_channels]->hw_value;
				num_channels++;
			}
		}
		for (i = 0; i < 25; i++)
			SLSI_INFO(sdev, "Channel value:%d\n", ch_info[i].chan);      /*will remove after testing */
		if (request->hw_mode == SLSI_ACS_MODE_IEEE80211A)
			request->ch_list_len = 25;
		else
			request->ch_list_len = 14;
		memcpy(&request->acs_chan_info[0], &ch_info[0], sizeof(ch_info));
		ndev_vif->scan[SLSI_SCAN_HW_ID].acs_request = request;
		ndev_vif->scan[SLSI_SCAN_HW_ID].is_blocking_scan = false;
		r = slsi_mlme_add_scan(sdev,
				       dev,
				       FAPI_SCANTYPE_AP_AUTO_CHANNEL_SELECTION,
				       FAPI_REPORTMODE_REAL_TIME,
				       0,    /* n_ssids */
				       NULL, /* ssids */
				       num_channels,
				       channels,
				       NULL,
				       NULL,                   /* ie */
				       0,                      /* ie_len */
				       ndev_vif->scan[SLSI_SCAN_HW_ID].is_blocking_scan);
	} else {
		SLSI_ERR(sdev, "Invalid freq_list len:%d or ch_width:%d or hw_mode:%d\n", freq_list_len,
			 request->ch_width, request->hw_mode);
		r = -EINVAL;
		kfree(request);
	}
	SLSI_INFO(sdev, "SUBCMD_ACS_INIT Received 7 return value:%d\n", r);   /*will remove after testing */
	kfree(freq_list);
	SLSI_MUTEX_UNLOCK(ndev_vif->scan_mutex);
	return r;
}

static const struct  nl80211_vendor_cmd_info slsi_vendor_events[] = {
	{ OUI_GOOGLE, SLSI_NL80211_SIGNIFICANT_CHANGE_EVENT },
	{ OUI_GOOGLE, SLSI_NL80211_HOTLIST_AP_FOUND_EVENT },
	{ OUI_GOOGLE, SLSI_NL80211_SCAN_RESULTS_AVAILABLE_EVENT },
	{ OUI_GOOGLE, SLSI_NL80211_FULL_SCAN_RESULT_EVENT },
	{ OUI_GOOGLE, SLSI_NL80211_SCAN_EVENT },
	{ OUI_GOOGLE, SLSI_NL80211_HOTLIST_AP_LOST_EVENT },
#ifdef CONFIG_SCSC_WLAN_KEY_MGMT_OFFLOAD
	{ OUI_SAMSUNG, SLSI_NL80211_VENDOR_SUBCMD_KEY_MGMT_ROAM_AUTH },
#endif
	{ OUI_SAMSUNG, SLSI_NL80211_VENDOR_HANGED_EVENT },
	{ OUI_GOOGLE,  SLSI_NL80211_EPNO_EVENT },
	{ OUI_GOOGLE,  SLSI_NL80211_HOTSPOT_MATCH },
	{ OUI_GOOGLE,  SLSI_NL80211_RSSI_REPORT_EVENT},
#ifdef CONFIG_SCSC_WLAN_ENHANCED_LOGGING
	{ OUI_GOOGLE,  SLSI_NL80211_LOGGER_RING_EVENT},
	{ OUI_GOOGLE,  SLSI_NL80211_LOGGER_FW_DUMP_EVENT},
#endif
	{ OUI_GOOGLE,  SLSI_NL80211_NAN_RESPONSE_EVENT},
	{ OUI_GOOGLE,  SLSI_NL80211_NAN_PUBLISH_TERMINATED_EVENT},
	{ OUI_GOOGLE,  SLSI_NL80211_NAN_MATCH_EVENT},
	{ OUI_GOOGLE,  SLSI_NL80211_NAN_MATCH_EXPIRED_EVENT},
	{ OUI_GOOGLE,  SLSI_NL80211_NAN_SUBSCRIBE_TERMINATED_EVENT},
	{ OUI_GOOGLE,  SLSI_NL80211_NAN_FOLLOWUP_EVENT},
	{ OUI_GOOGLE,  SLSI_NL80211_NAN_DISCOVERY_ENGINE_EVENT},
	{ OUI_GOOGLE,  SLSI_NL80211_NAN_DISABLED_EVENT},
	{ OUI_GOOGLE,  SLSI_NL80211_RTT_RESULT_EVENT},
	{ OUI_GOOGLE,  SLSI_NL80211_RTT_COMPLETE_EVENT},
	{ OUI_SAMSUNG, SLSI_NL80211_VENDOR_ACS_EVENT}
};

static const struct wiphy_vendor_command     slsi_vendor_cmd[] = {
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_GET_CAPABILITIES
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_gscan_get_capabilities
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_GET_VALID_CHANNELS
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_gscan_get_valid_channel
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_ADD_GSCAN
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_gscan_add
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_DEL_GSCAN
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_gscan_del
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_GET_SCAN_RESULTS
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_gscan_get_scan_results
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_SET_BSSID_HOTLIST
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_gscan_set_hotlist
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_RESET_BSSID_HOTLIST
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_gscan_reset_hotlist
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_SET_SIGNIFICANT_CHANGE
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_gscan_set_significant_change
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_RESET_SIGNIFICANT_CHANGE
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_gscan_reset_significant_change
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_SET_GSCAN_OUI
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_gscan_set_oui
	},
#ifdef CONFIG_SCSC_WLAN_KEY_MGMT_OFFLOAD
	{
		{
			.vendor_id = OUI_SAMSUNG,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_KEY_MGMT_SET_KEY
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_key_mgmt_set_pmk
	},
#endif
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_SET_BSSID_BLACKLIST
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_set_bssid_blacklist
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_START_KEEP_ALIVE_OFFLOAD
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_start_keepalive_offload
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_STOP_KEEP_ALIVE_OFFLOAD
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_stop_keepalive_offload
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_SET_EPNO_LIST
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_set_epno_ssid
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_SET_HS_LIST
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_set_hs_params
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_RESET_HS_LIST
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_reset_hs_params
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_SET_RSSI_MONITOR
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_set_rssi_monitor
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_LSTATS_SUBCMD_SET_STATS
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_lls_set_stats
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_LSTATS_SUBCMD_GET_STATS
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_lls_get_stats
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_LSTATS_SUBCMD_CLEAR_STATS
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_lls_clear_stats
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_GET_FEATURE_SET
		},
		.flags = 0,
		.doit = slsi_get_feature_set
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_SET_COUNTRY_CODE
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_set_country_code
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_CONFIGURE_ND_OFFLOAD
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_configure_nd_offload
	},
#ifdef CONFIG_SCSC_WLAN_ENHANCED_LOGGING
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_START_LOGGING
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_start_logging
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_RESET_LOGGING
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_reset_logging
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_TRIGGER_FW_MEM_DUMP
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_trigger_fw_mem_dump
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_GET_FW_MEM_DUMP
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_get_fw_mem_dump
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_TRIGGER_DRIVER_MEM_DUMP
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_trigger_driver_mem_dump
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_GET_DRIVER_MEM_DUMP
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_get_driver_mem_dump
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_GET_VERSION
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_get_version
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_GET_RING_STATUS
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_get_ring_buffers_status
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_GET_RING_DATA
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_get_ring_data
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_GET_FEATURE
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_get_logger_supported_feature_set
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_START_PKT_FATE_MONITORING
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_start_pkt_fate_monitoring
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_GET_TX_PKT_FATES
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_get_tx_pkt_fates
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_GET_RX_PKT_FATES
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_get_rx_pkt_fates
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_GET_WAKE_REASON_STATS
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_get_wake_reason_stats
	},
#endif /* CONFIG_SCSC_WLAN_ENHANCED_LOGGING */
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_NAN_ENABLE
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_nan_enable
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_NAN_DISABLE
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_nan_disable
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_NAN_PUBLISH
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_nan_publish
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_NAN_PUBLISHCANCEL
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_nan_publish_cancel
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_NAN_SUBSCRIBE
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_nan_subscribe
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_NAN_SUBSCRIBECANCEL
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_nan_subscribe_cancel
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_NAN_TXFOLLOWUP
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_nan_transmit_followup
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_NAN_CONFIG
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_nan_set_config
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_NAN_CAPABILITIES
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_nan_get_capabilities
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_GET_ROAMING_CAPABILITIES
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_get_roaming_capabilities
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_SET_ROAMING_STATE
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_set_roaming_state
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_RTT_GET_CAPABILITIES
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_rtt_get_capabilities
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_RTT_RANGE_START
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_rtt_set_config
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_RTT_RANGE_CANCEL
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_rtt_cancel_config
	},
	{
		{
			.vendor_id = OUI_SAMSUNG,
			.subcmd = SLSI_NL80211_VENDOR_SUBCMD_ACS_INIT
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = slsi_acs_init
	},
};

void slsi_nl80211_vendor_deinit(struct slsi_dev *sdev)
{
	SLSI_DBG2(sdev, SLSI_GSCAN, "De-initialise vendor command and events\n");
	sdev->wiphy->vendor_commands = NULL;
	sdev->wiphy->n_vendor_commands = 0;
	sdev->wiphy->vendor_events = NULL;
	sdev->wiphy->n_vendor_events = 0;

	SLSI_DBG2(sdev, SLSI_GSCAN, "Gscan cleanup\n");
	slsi_gscan_flush_scan_results(sdev);

	SLSI_DBG2(sdev, SLSI_GSCAN, "Hotlist cleanup\n");
	slsi_gscan_flush_hotlist_results(sdev);
}

void slsi_nl80211_vendor_init(struct slsi_dev *sdev)
{
	int i;

	SLSI_DBG2(sdev, SLSI_GSCAN, "Init vendor command and events\n");

	sdev->wiphy->vendor_commands = slsi_vendor_cmd;
	sdev->wiphy->n_vendor_commands = ARRAY_SIZE(slsi_vendor_cmd);
	sdev->wiphy->vendor_events = slsi_vendor_events;
	sdev->wiphy->n_vendor_events = ARRAY_SIZE(slsi_vendor_events);

	for (i = 0; i < SLSI_GSCAN_MAX_BUCKETS; i++)
		sdev->bucket[i].scan_id = (SLSI_GSCAN_SCAN_ID_START + i);

	for (i = 0; i < SLSI_GSCAN_HASH_TABLE_SIZE; i++)
		sdev->gscan_hash_table[i] = NULL;

	INIT_LIST_HEAD(&sdev->hotlist_results);
}
