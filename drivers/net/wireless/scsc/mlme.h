/****************************************************************************
 *
 * Copyright (c) 2012 - 2017 Samsung Electronics Co., Ltd. All rights reserved
 *
 ****************************************************************************/

#ifndef __SLSI_MLME_H__
#define __SLSI_MLME_H__

#include "dev.h"
#include "mib.h"

enum slsi_ac_index_wmm_pe {
	AC_BE,
	AC_BK,
	AC_VI,
	AC_VO
};

#define SLSI_FREQ_FW_TO_HOST(f) ((f) / 2)
#define SLSI_FREQ_HOST_TO_FW(f) ((f) * 2)

#define SLSI_SINFO_MIB_ACCESS_TIMEOUT (1000) /* 1 sec timeout */

#define SLSI_WLAN_EID_VENDOR_SPECIFIC 0xdd
#define SLSI_WLAN_EID_INTERWORKING 107

#define SLSI_WLAN_OUI_TYPE_WFA_HS20_IND 0x10
#define SLSI_WLAN_OUI_TYPE_WFA_OSEN 0x12

/*Extended capabilities bytes*/
#define SLSI_WLAN_EXT_CAPA2_BSS_TRANSISITION_ENABLED  (1 << 3)
#define SLSI_WLAN_EXT_CAPA3_INTERWORKING_ENABLED        (1 << 7)
#define SLSI_WLAN_EXT_CAPA4_QOS_MAP_ENABLED                  (1 << 0)
#define SLSI_WLAN_EXT_CAPA5_WNM_NOTIF_ENABLED              (1 << 6)
#define SLSI_WLAN_EXT_CAPA2_QBSS_LOAD_ENABLED                  BIT(7)
#define SLSI_WLAN_EXT_CAPA1_PROXY_ARP_ENABLED                  BIT(4)
#define SLSI_WLAN_EXT_CAPA2_TFS_ENABLED                              BIT(0)
#define SLSI_WLAN_EXT_CAPA2_WNM_SLEEP_ENABLED                 BIT(1)
#define SLSI_WLAN_EXT_CAPA2_TIM_ENABLED                              BIT(2)
#define SLSI_WLAN_EXT_CAPA2_DMS_ENABLED                             BIT(4)

/*RM Enabled Capabilities Bytes*/
#define SLSI_WLAN_RM_CAPA0_LINK_MEASUREMENT_ENABLED       BIT(0)
#define SLSI_WLAN_RM_CAPA0_NEIGHBOR_REPORT_ENABLED         BIT(1)
#define SLSI_WLAN_RM_CAPA0_PASSIVE_MODE_ENABLED              BIT(4)
#define SLSI_WLAN_RM_CAPA0_ACTIVE_MODE_ENABLED	               BIT(5)
#define SLSI_WLAN_RM_CAPA0_TABLE_MODE_ENABLED                 BIT(6)


#define SLSI_AP_EXT_CAPAB_IE_LEN_MAX 12           /* EID (1) + Len (1) + Ext Capab (8) */

#define SLSI_SCAN_DONE_IND_WAIT_TIMEOUT 40000 /* 40 seconds */

/* WLAN_EID_COUNTRY available from kernel version 3.7 */
#ifndef WLAN_EID_COUNTRY
#define WLAN_EID_COUNTRY 7
#endif

/* P2P (Wi-Fi Direct) */
#define SLSI_P2P_WILDCARD_SSID "DIRECT-"
#define SLSI_P2P_WILDCARD_SSID_LENGTH 7
#define SLSI_P2P_SOCIAL_CHAN_COUNT      3

/* A join scan with P2P GO SSID can come and hence the SSID length comparision should include >= */
#define SLSI_IS_P2P_SSID(ssid, ssid_len) ((ssid_len >= SLSI_P2P_WILDCARD_SSID_LENGTH) && \
					  (memcmp(ssid, SLSI_P2P_WILDCARD_SSID, SLSI_P2P_WILDCARD_SSID_LENGTH) == 0))

/* Action frame categories for registering with firmware */
#define SLSI_ACTION_FRAME_PUBLIC        (1 << 4)
#define SLSI_ACTION_FRAME_VENDOR_SPEC_PROTECTED (1 << 30)
#define SLSI_ACTION_FRAME_VENDOR_SPEC   (1 << 31)
#define SLSI_ACTION_FRAME_WMM     (1 << 17)
#define SLSI_ACTION_FRAME_WNM     (1 << 10)
#define SLSI_ACTION_FRAME_QOS     (1 << 1)
#define SLSI_ACTION_FRAME_PROTECTED_DUAL    BIT(9)
#define SLSI_ACTION_FRAME_RADIO_MEASUREMENT    BIT(5)

/* Firmware transmit rates */
#define SLSI_TX_RATE_NON_HT_1MBPS 0x4001
#define SLSI_TX_RATE_NON_HT_6MBPS 0x4004
#define SLSI_ROAMING_CHANNELS_MAX 38

#define SLSI_WLAN_EID_WAPI 68

/**
 * If availability_duration is set to SLSI_FW_CHANNEL_DURATION_UNSPECIFIED
 * then the firmware autonomously decides how long to remain listening on
 * the configured channel.
 */
#define SLSI_FW_CHANNEL_DURATION_UNSPECIFIED             (0x0000)
extern struct ieee80211_supported_band    slsi_band_2ghz;
extern struct ieee80211_supported_band    slsi_band_5ghz;
extern struct ieee80211_sta_vht_cap       slsi_vht_cap;

/* Packet Filtering */
#define SLSI_MAX_PATTERN_DESC    4                                         /* We are not using more than 4 pattern descriptors in a pkt filter*/
#define SLSI_PKT_DESC_FIXED_LEN 2                                          /* offset (1) + mask length (1)*/
#define SLSI_PKT_FILTER_ELEM_FIXED_LEN  6                                  /* oui(3) + oui type(1) + filter id (1) + pkt filter mode(1)*/
#define SLSI_PKT_FILTER_ELEM_HDR_LEN  (2 + SLSI_PKT_FILTER_ELEM_FIXED_LEN) /* element id + len + SLSI_PKT_FILTER_ELEM_FIXED_LEN*/
#define SLSI_MAX_PATTERN_LENGTH 6

/*Default values of MIBS params for GET_STA_INFO driver private command */
#define SLSI_DEFAULT_UNIFI_PEER_BANDWIDTH             -1
#define SLSI_DEFAULT_UNIFI_PEER_NSS                          0
#define SLSI_DEFAULT_UNIFI_PEER_RSSI                         1
#define SLSI_DEFAULT_UNIFI_PEER_TX_DATA_RATE          0

#define SLSI_CHECK_TYPE(sdev, recv_type, exp_type) \
	do { \
		int var1 = recv_type; \
		int var2 = exp_type; \
		if (var1 != var2) { \
			SLSI_WARN(sdev, "Type mismatched, expected type: %d and received type %d ", var2, var1); \
		} \
	} while (0)


struct slsi_mlme_pattern_desc {
	u8 offset;
	u8 mask_length;
	u8 mask[SLSI_MAX_PATTERN_LENGTH];
	u8 pattern[SLSI_MAX_PATTERN_LENGTH];
};

struct slsi_mlme_pkt_filter_elem {
	u8                            header[SLSI_PKT_FILTER_ELEM_HDR_LEN];
	u8                            num_pattern_desc;
	struct slsi_mlme_pattern_desc pattern_desc[SLSI_MAX_PATTERN_DESC];
};

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 9))
u16 slsi_get_chann_info(struct slsi_dev *sdev, struct cfg80211_chan_def *chandef);
int slsi_check_channelization(struct slsi_dev *sdev, struct cfg80211_chan_def *chandef,
			      int wifi_sharing_channel_switched);
#else
u16 slsi_get_chann_info(struct slsi_dev *sdev, enum nl80211_channel_type channel_type);
int slsi_check_channelization(struct slsi_dev *sdev, enum nl80211_channel_type channel_type);
#endif

int slsi_mlme_set_ip_address(struct slsi_dev *sdev, struct net_device *dev);
#ifndef CONFIG_SCSC_WLAN_BLOCK_IPV6
int slsi_mlme_set_ipv6_address(struct slsi_dev *sdev, struct net_device *dev);
#endif
int slsi_mlme_set(struct slsi_dev *sdev, struct net_device *dev, u8 *req, int req_len);
int slsi_mlme_get(struct slsi_dev *sdev, struct net_device *dev, u8 *req, int req_len,
		  u8 *resp, int resp_buf_len, int *resp_len);

int slsi_mlme_add_vif(struct slsi_dev *sdev, struct net_device *dev, u8 *interface_address, u8 *device_address);
void slsi_mlme_del_vif(struct slsi_dev *sdev, struct net_device *dev);
int slsi_mlme_set_channel(struct slsi_dev *sdev, struct net_device *dev, struct ieee80211_channel *chan, u16 duration, u16 interval, u16 count);
void slsi_ap_obss_scan_done_ind(struct net_device *dev, struct netdev_vif *ndev_vif);

u16 slsi_compute_chann_info(struct slsi_dev *sdev, u16 width, u16 center_freq0, u16 channel_freq);
/**
 * slsi_mlme_add_autonomous_scan() Returns:
 *  0 : Scan installed
 * >0 : Scan NOT installed. Not an Error
 * <0 : Scan NOT installed. Error
 */
int slsi_mlme_add_scan(struct slsi_dev                    *sdev,
		       struct net_device                  *dev,
		       u16								  scan_type,
		       u16                                report_mode,
		       u32                                n_ssids,
		       struct cfg80211_ssid               *ssids,
		       u32                                n_channels,
		       struct ieee80211_channel           *channels[],
		       void                               *gscan_param,
		       const u8                           *ies,
		       u16                                ies_len,
		       bool                               wait_for_ind);

int slsi_mlme_add_sched_scan(struct slsi_dev                    *sdev,
			     struct net_device                  *dev,
			     struct cfg80211_sched_scan_request *request,
			     const u8                           *ies,
			     u16                                ies_len);

int slsi_mlme_del_scan(struct slsi_dev *sdev, struct net_device *dev, u16 scan_id, bool scan_timed_out);
int slsi_mlme_start(struct slsi_dev *sdev, struct net_device *dev, u8 *bssid, struct cfg80211_ap_settings *settings, const u8 *wpa_ie_pos, const u8 *wmm_ie_pos, bool append_vht_ies);
int slsi_mlme_connect(struct slsi_dev *sdev, struct net_device *dev, struct cfg80211_connect_params *sme, struct ieee80211_channel *channel, const u8 *bssid);
int slsi_mlme_set_key(struct slsi_dev *sdev, struct net_device *dev, u16 key_id, u16 key_type, const u8 *address, struct key_params *key);
int slsi_mlme_get_key(struct slsi_dev *sdev, struct net_device *dev, u16 key_id, u16 key_type, u8 *seq, int *seq_len);

/**
 * Sends MLME-DISCONNECT-REQ and waits for the MLME-DISCONNECT-CFM
 * MLME-DISCONNECT-CFM only indicates if the firmware has accepted the request (or not)
 * the actual end of the disconnection is indicated by the firmware sending
 * MLME-DISCONNECT-IND (following a successful MLME-DISCONNECT-CFM).
 * The host has to wait for the full exchange to complete with the firmware before
 * returning to cfg80211 if it made the disconnect request. Due to this, this function
 * waits for both the MLME-DISCONNECT-CFM and the MLME-DISCONNECT-IND (if the
 * MLME-DISCONNECT-CFM was successful)
 */
int slsi_mlme_disconnect(struct slsi_dev *sdev, struct net_device *dev, u8 *bssid, u16 reason_code, bool wait_ind);

int slsi_mlme_req(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb);
struct sk_buff *slsi_mlme_req_no_cfm(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb);

struct sk_buff *slsi_mlme_req_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb, u16 ind_id);
/* Reads multiple MIB data related to station info. returns 0 if success else errno*/
int slsi_mlme_get_sinfo_mib(struct slsi_dev *sdev, struct net_device *dev,
			    struct slsi_peer *peer);

int slsi_mlme_connect_scan(struct slsi_dev *sdev, struct net_device *dev,
			   u32 n_ssids, struct cfg80211_ssid *ssids, struct ieee80211_channel *channel);
int slsi_mlme_powermgt(struct slsi_dev *sdev, struct net_device *dev, u16 ps_mode);
int slsi_mlme_powermgt_unlocked(struct slsi_dev *sdev, struct net_device *dev, u16 ps_mode);
int slsi_mlme_register_action_frame(struct slsi_dev *sdev, struct net_device *dev,  u32 af_bitmap_active, u32 af_bitmap_suspended);
int slsi_mlme_channel_switch(struct slsi_dev *sdev, struct net_device *dev,  u16 center_freq, u16 chan_info);
int slsi_mlme_add_info_elements(struct slsi_dev *sdev, struct net_device *dev,  u16 purpose, const u8 *ies, const u16 ies_len);
int slsi_mlme_send_frame_mgmt(struct slsi_dev *sdev, struct net_device *dev, const u8 *frame, int frame_len, u16 data_desc, u16 msg_type, u16 host_tag, u16 freq, u32 dwell_time, u32 period);
int slsi_mlme_send_frame_data(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb, u16 msg_type,
			      u16 host_tag, u32 dwell_time, u32 period);
int slsi_mlme_reset_dwell_time(struct slsi_dev *sdev, struct net_device *dev);
int slsi_mlme_set_packet_filter(struct slsi_dev *sdev, struct net_device *dev, int pkt_filter_len, u8 num_filters, struct slsi_mlme_pkt_filter_elem *pkt_filter_elems);
void slsi_mlme_connect_resp(struct slsi_dev *sdev, struct net_device *dev);
void slsi_mlme_connected_resp(struct slsi_dev *sdev, struct net_device *dev, u16 peer_index);
void slsi_mlme_roamed_resp(struct slsi_dev *sdev, struct net_device *dev);
int slsi_mlme_set_pmk(struct slsi_dev *sdev, struct net_device *dev, const u8 *pmk, u16 pmklen);
int slsi_mlme_roam(struct slsi_dev *sdev, struct net_device *dev, const u8 *bssid, u16 freq);
int slsi_mlme_set_cached_channels(struct slsi_dev *sdev, struct net_device *dev, u32 channels_count, u8 *channels);
int slsi_mlme_tdls_peer_resp(struct slsi_dev *sdev, struct net_device *dev, u16 pid, u16 tdls_event);
int slsi_mlme_tdls_action(struct slsi_dev *sdev, struct net_device *dev, const u8 *peer, int action, u16 center_freq, u16 chan_info);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 9))
int slsi_mlme_set_acl(struct slsi_dev *sdev, struct net_device *dev, u16 ifnum, const struct cfg80211_acl_data *params);
#endif
int slsi_mlme_set_traffic_parameters(struct slsi_dev *sdev, struct net_device *dev, u16 user_priority, u16 medium_time, u16 minimun_data_rate, u8 *mac);
int slsi_mlme_del_traffic_parameters(struct slsi_dev *sdev, struct net_device *dev, u16 user_priority);

#ifdef CONFIG_SCSC_WLAN_GSCAN_ENABLE
int slsi_mlme_significant_change_set(struct slsi_dev *sdev, struct net_device *dev, struct slsi_nl_significant_change_params *significant_param_ptr);
int slsi_mlme_set_pno_list(struct slsi_dev *sdev, int count,
			   struct slsi_epno_param *epno_param, struct slsi_epno_hs2_param *epno_hs2_param);
int slsi_mlme_start_link_stats_req(struct slsi_dev *sdev, u16 mpdu_size_threshold, bool aggressive_statis_enabled);
int slsi_mlme_stop_link_stats_req(struct slsi_dev *sdev, u16 stats_stop_mask);
int slsi_mlme_nan_enable(struct slsi_dev *sdev, struct net_device *dev, struct slsi_hal_nan_enable_req *hal_req);
int slsi_mlme_nan_publish(struct slsi_dev *sdev, struct net_device *dev, struct slsi_hal_nan_publish_req *hal_req,
			  u16 publish_id);
int slsi_mlme_nan_subscribe(struct slsi_dev *sdev, struct net_device *dev, struct slsi_hal_nan_subscribe_req *hal_req,
			    u16 subscribe_id);
int slsi_mlme_nan_tx_followup(struct slsi_dev *sdev, struct net_device *dev,
			      struct slsi_hal_nan_transmit_followup_req *hal_req);
int slsi_mlme_nan_set_config(struct slsi_dev *sdev, struct net_device *dev, struct slsi_hal_nan_config_req *hal_req);
#endif

int slsi_mlme_set_ext_capab(struct slsi_dev *sdev, struct net_device *dev, struct slsi_mib_value *mib_val);
int slsi_mlme_set_hs2_ext_cap(struct slsi_dev *sdev, struct net_device *dev, const u8 *ies, int ie_len);
int slsi_mlme_reassociate(struct slsi_dev *sdev, struct net_device *dev);
void slsi_mlme_reassociate_resp(struct slsi_dev *sdev, struct net_device *dev);
int slsi_modify_ies(struct net_device *dev, u8 eid, u8 *ies, int ies_len, u8 ie_index, u8 ie_value);
int slsi_mlme_set_rssi_monitor(struct slsi_dev *sdev, struct net_device *dev, u8 enable, s8 low_rssi_threshold, s8 high_rssi_threshold);
struct slsi_mib_value *slsi_read_mibs(struct slsi_dev *sdev, struct net_device *dev, struct slsi_mib_get_entry *mib_entries, int mib_count, struct slsi_mib_data *mibrsp);
int slsi_mlme_set_host_state(struct slsi_dev *sdev, struct net_device *dev, u8 host_state);
int slsi_mlme_set_ctwindow(struct slsi_dev *sdev, struct net_device *dev, unsigned int ct_param);
int slsi_mlme_set_p2p_noa(struct slsi_dev *sdev, struct net_device *dev, unsigned int noa_count,
			  unsigned int interval, unsigned int duration);
void slsi_fw_tx_rate_calc(u16 fw_rate, struct rate_info *tx_rate, unsigned long *data_rate_mbps);
#endif /*__SLSI_MLME_H__*/
