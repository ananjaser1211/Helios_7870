/*****************************************************************************
 *
 * Copyright (c) 2012 - 2018 Samsung Electronics Co., Ltd. All rights reserved
 *
 ****************************************************************************/

#include <linux/delay.h>
#include <net/cfg80211.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <scsc/scsc_log_collector.h>

#include "dev.h"
#include "debug.h"
#include "mlme.h"
#include "mib.h"
#include "mgt.h"
#include "cac.h"

#define SLSI_SCAN_PRIVATE_IE_CHANNEL_LIST_HEADER_LEN	7
#define SLSI_SCAN_PRIVATE_IE_SSID_FILTER_HEADER_LEN		7
#define SLSI_SCAN_CHANNEL_DESCRIPTOR_SIZE	3
#define SLSI_CHANN_INFO_HT_SCB  0x0100

#ifdef CONFIG_SCSC_WLAN_GSCAN_ENABLE
#define SLSI_FW_MAX_BSSID_PER_TRACKING_IE   (20)
#define SLSI_FW_BSSID_DESCRIPTOR_BSS_SIZE   (10)
#endif

#define SLSI_NOA_CONFIG_REQUEST_ID          (1)
#define SLSI_MLME_ARP_DROP_FREE_SLOTS_COUNT 16

#define SLSI_FAPI_NAN_ATTRIBUTE_PUT_U8(req, attribute, val) \
{ \
	u16 attribute_len = 1; \
	struct sk_buff *req_p = req; \
	fapi_append_data((req_p), (u8 *)&(attribute), 2); \
	fapi_append_data((req_p), (u8 *)&attribute_len, 2); \
	fapi_append_data((req_p), (u8 *)&(val), 1); \
}

#define SLSI_FAPI_NAN_ATTRIBUTE_PUT_U16(req, attribute, val) \
{ \
	u16 attribute_len = 2; \
	__le16 le16val = cpu_to_le16(val); \
	struct sk_buff *req_p = req; \
	fapi_append_data((req_p), (u8 *)&(attribute), 2); \
	fapi_append_data((req_p), (u8 *)&attribute_len, 2); \
	fapi_append_data((req_p), (u8 *)&le16val, 2); \
}

#define SLSI_FAPI_NAN_ATTRIBUTE_PUT_U32(req, attribute, val) \
{ \
	u16 attribute_len = 4; \
	__le32 le32val = cpu_to_le32(val);\
	struct sk_buff *req_p = req; \
	fapi_append_data((req_p), (u8 *)&(attribute), 2); \
	fapi_append_data((req_p), (u8 *)&attribute_len, 2); \
	fapi_append_data((req_p), (u8 *)&le32val, 4); \
}

#define SLSI_FAPI_NAN_ATTRIBUTE_PUT_DATA(req, attribute, val, val_len) \
{ \
	u16 attribute_len = (val_len); \
	struct sk_buff *req_p = req; \
	fapi_append_data((req_p), (u8 *)&(attribute), 2); \
	fapi_append_data((req_p), (u8 *)&attribute_len, 2); \
	fapi_append_data((req_p), (val), (attribute_len)); \
}

static bool missing_cfm_ind_panic = true;
module_param(missing_cfm_ind_panic, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(missing_cfm_ind_panic, "Panic on missing confirm or indication from the chip");

struct slsi_mlme_rsse {
	u8       group_cs_count;
	const u8 *group_cs;
	u8       pairwise_cs_count;
	const u8 *pairwise_cs;
	u8       akm_suite_count;
	const u8 *akm_suite;
	u8       pmkid_count;
	const u8 *pmkid;
	const u8 *group_mgmt_cs;       /* used for PMF*/
};

static struct sk_buff *slsi_mlme_wait_for_cfm(struct slsi_dev *sdev, struct slsi_sig_send *sig_wait)
{
	struct sk_buff *cfm = NULL;
	int            tm;

	tm = wait_for_completion_timeout(&sig_wait->completion, msecs_to_jiffies(*sdev->sig_wait_cfm_timeout));
	spin_lock_bh(&sig_wait->send_signal_lock);

	/* Confirm timed out? */
	if (!sig_wait->cfm) {
		SLSI_ERR(sdev, "No cfm(0x%.4X) for req(0x%04X) senderid=0x%x\n", sig_wait->cfm_id, sig_wait->req_id, sig_wait->process_id);
		if (tm == 0) {
			char reason[80];

			WARN(1, "Timeout - confirm 0x%04x not received from chip\n", sig_wait->cfm_id);
			if (missing_cfm_ind_panic) {
				snprintf(reason, sizeof(reason), "Timed out while waiting for the cfm(0x%.4x) for req(0x%04x)",
					 sig_wait->cfm_id, sig_wait->req_id);

				spin_unlock_bh(&sig_wait->send_signal_lock);
				slsi_sm_service_failed(sdev, reason);
				spin_lock_bh(&sig_wait->send_signal_lock);
			}
		} else {
			WARN(1, "Confirm 0x%04x lost\n", sig_wait->cfm_id);
		}
	} else {
		WARN_ON(fapi_get_u16(sig_wait->cfm, receiver_pid) != sig_wait->process_id);
		WARN_ON(fapi_get_u16(sig_wait->cfm, id) != sig_wait->cfm_id);
	}

	sig_wait->cfm_id = 0;
	cfm = sig_wait->cfm;
	sig_wait->cfm = NULL;
	if (!cfm)
		sig_wait->ind_id = 0;

	spin_unlock_bh(&sig_wait->send_signal_lock);

	return cfm;
}

static int panic_on_lost_ind(u16 ind_id)
{
	if (ind_id == MLME_SCAN_DONE_IND)
		return 0;
	return 1;
}

static struct sk_buff *slsi_mlme_wait_for_ind(struct slsi_dev *sdev, struct net_device *dev, struct slsi_sig_send *sig_wait, u16 ind_id)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *ind = NULL;
	int               tm = 0;

	/* The indication and confirm may have been received in the same HIP read.
	 * The HIP receive buffer processes all received signals in one thread whilst the
	 * waiting process may not be scheduled even if the "complete" call is made.
	 * In this scenario, the complete() call has already been made for this object
	 * and the wait will return immediately.
	 */
	if (ind_id == MLME_SCAN_DONE_IND)
		/* To handle the coex scenario where BTscan has high priority increasing the wait time to 40 secs */
		tm = wait_for_completion_timeout(&sig_wait->completion, msecs_to_jiffies(SLSI_SCAN_DONE_IND_WAIT_TIMEOUT));
	else if ((ind_id == MLME_DISCONNECT_IND) && (ndev_vif->vif_type == FAPI_VIFTYPE_AP))
		tm = wait_for_completion_timeout(&sig_wait->completion, msecs_to_jiffies(sdev->device_config.ap_disconnect_ind_timeout));
	else
		tm = wait_for_completion_timeout(&sig_wait->completion, msecs_to_jiffies(*sdev->sig_wait_cfm_timeout + 2000));

	spin_lock_bh(&sig_wait->send_signal_lock);

	/* Indication timed out? */
	if (!sig_wait->ind) {
		SLSI_ERR(sdev, "No ind(0x%.4X) for req(0x%04X) senderid=0x%x\n", sig_wait->ind_id, sig_wait->req_id, sig_wait->process_id);
		if (tm == 0) {
			char reason[80];

			WARN(1, "Timeout - indication 0x%04x not received from chip\n", sig_wait->ind_id);
			if (missing_cfm_ind_panic && panic_on_lost_ind(ind_id)) {
				snprintf(reason, sizeof(reason), "Timed out while waiting for the ind(0x%.4x) for req(0x%04x)",
					 sig_wait->ind_id, sig_wait->req_id);

				spin_unlock_bh(&sig_wait->send_signal_lock);
				slsi_sm_service_failed(sdev, reason);
				spin_lock_bh(&sig_wait->send_signal_lock);
			}
		} else {
			WARN(1, "Indication 0x%04x lost\n", sig_wait->ind_id);
		}
	} else {
		WARN_ON(fapi_get_u16(sig_wait->ind, receiver_pid) != sig_wait->process_id);
		WARN_ON(fapi_get_u16(sig_wait->ind, id) != sig_wait->ind_id);
	}

	sig_wait->ind_id = 0;
	ind = sig_wait->ind;
	sig_wait->ind = NULL;

	spin_unlock_bh(&sig_wait->send_signal_lock);

	return ind;
}

/* mib_error: NULL when not required
 * ind: 0 when not required, if used validate_cfm_wait_ind MUST be supplied
 * validate_cfm_wait_ind: NULL when not required, if used ind MUS not be 0
 * NOTE: dev can be NULL!
 */
static struct sk_buff *slsi_mlme_tx_rx(struct slsi_dev *sdev,
				       struct net_device *dev,
				       struct sk_buff *skb,
				       u16 cfm_id,
				       struct sk_buff **mib_error,
				       u16 ind_id,
				       bool (*validate_cfm_wait_ind)(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *cfm))
{
	struct sk_buff       *rx = NULL;
	int                  err;
	u16                  req_id = fapi_get_u16(skb, id);
	struct slsi_sig_send *sig_wait = &sdev->sig_wait;

	if (dev) {
		struct netdev_vif *ndev_vif = netdev_priv(dev);

		sig_wait = &ndev_vif->sig_wait;
	}

	if (sdev->mlme_blocked) {
		SLSI_DBG3(sdev, SLSI_TX, "Rejected. mlme_blocked=%d", sdev->mlme_blocked);
		slsi_kfree_skb(skb);
		return NULL;
	}

	slsi_wakelock(&sdev->wlan_wl);
	SLSI_MUTEX_LOCK(sig_wait->mutex);

	spin_lock_bh(&sig_wait->send_signal_lock);
	if (++sig_wait->process_id > SLSI_TX_PROCESS_ID_MAX)
		sig_wait->process_id = SLSI_TX_PROCESS_ID_MIN;

	WARN_ON(sig_wait->cfm);
	WARN_ON(sig_wait->ind);
	slsi_kfree_skb(sig_wait->cfm);
	slsi_kfree_skb(sig_wait->ind);
	slsi_kfree_skb(sig_wait->mib_error);
	sig_wait->cfm        = NULL;
	sig_wait->ind        = NULL;
	sig_wait->mib_error  = NULL;
	sig_wait->req_id     = req_id;
	sig_wait->cfm_id     = cfm_id;
	sig_wait->ind_id     = ind_id;

	fapi_set_u16(skb, sender_pid, sig_wait->process_id);
	spin_unlock_bh(&sig_wait->send_signal_lock);

	err = slsi_tx_control(sdev, dev, skb);
	if (err != 0) {
		SLSI_ERR(sdev, "Failed to send mlme signal:0x%.4X, err=%d\n", req_id, err);
		slsi_kfree_skb(skb);
		goto clean_exit;
	}

	if (cfm_id) {
		rx = slsi_mlme_wait_for_cfm(sdev, sig_wait);
		if (rx && ind_id) {
			/* The cfm skb is owned by the validate_cfm_wait_ind() function and MUST be freed or saved there */
			if (validate_cfm_wait_ind(sdev, dev, rx)) {
				rx = slsi_mlme_wait_for_ind(sdev, dev, sig_wait, ind_id);
			} else {
				sig_wait->ind_id = 0;   /* Reset as there is no wait for indication */
				rx = NULL;
			}
		}
	} else if (ind_id) {
		rx = slsi_mlme_wait_for_ind(sdev, dev, sig_wait, ind_id);
	}

	/* The cfm_id and ind_id should ALWAYS be 0 at this point */
	WARN_ON(sig_wait->cfm_id);
	WARN_ON(sig_wait->ind_id);
	WARN_ON(sig_wait->cfm);
	WARN_ON(sig_wait->ind);
clean_exit:

	spin_lock_bh(&sig_wait->send_signal_lock);

	sig_wait->req_id = 0;
	sig_wait->cfm_id = 0;
	sig_wait->ind_id = 0;
	slsi_kfree_skb(sig_wait->cfm);
	slsi_kfree_skb(sig_wait->ind);
	sig_wait->cfm = NULL;
	sig_wait->ind = NULL;

	if (mib_error)
		*mib_error = sig_wait->mib_error;
	else
		slsi_kfree_skb(sig_wait->mib_error);
	sig_wait->mib_error = NULL;
	spin_unlock_bh(&sig_wait->send_signal_lock);

	SLSI_MUTEX_UNLOCK(sig_wait->mutex);

	slsi_wakeunlock(&sdev->wlan_wl);
	return rx;
}

/**
 * NOTE: dev can be NULL!
 */
int slsi_mlme_req(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb)
{
	int ret = 0;
	struct slsi_sig_send *sig_wait = &sdev->sig_wait;

	if (dev) {
		struct netdev_vif *ndev_vif = netdev_priv(dev);

		sig_wait = &ndev_vif->sig_wait;
	}
	spin_lock_bh(&sig_wait->send_signal_lock);
	if (++sig_wait->process_id > SLSI_TX_PROCESS_ID_MAX)
		sig_wait->process_id = SLSI_TX_PROCESS_ID_MIN;
	fapi_set_u16(skb, sender_pid, sig_wait->process_id);
	spin_unlock_bh(&sig_wait->send_signal_lock);

	ret = slsi_tx_control(sdev, dev, skb);
	if (ret)
		slsi_kfree_skb(skb);
	return ret;
}

struct sk_buff *slsi_mlme_req_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb, u16 ind_id)
{
	if (WARN_ON(!ind_id))
		goto err;
	return slsi_mlme_tx_rx(sdev, dev, skb, 0, NULL, ind_id, NULL);
err:
	slsi_kfree_skb(skb);
	return NULL;
}

struct sk_buff *slsi_mlme_req_no_cfm(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb)
{
	return slsi_mlme_tx_rx(sdev, dev, skb, 0, NULL, 0, NULL);
}

struct sk_buff *slsi_mlme_req_cfm(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb, u16 cfm_id)
{
	if (WARN_ON(!cfm_id))
		goto err;
	return slsi_mlme_tx_rx(sdev, dev, skb, cfm_id, NULL, 0, NULL);
err:
	slsi_kfree_skb(skb);
	return NULL;
}

/* NOTE: dev can be NULL! */
static inline struct sk_buff *slsi_mlme_req_cfm_mib(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb, u16 cfm_id, struct sk_buff **mib_error)
{
	if (WARN_ON(!cfm_id))
		goto err;
	if (WARN_ON(!mib_error))
		goto err;
	return slsi_mlme_tx_rx(sdev, dev, skb, cfm_id, mib_error, 0, NULL);
err:
	slsi_kfree_skb(skb);
	return NULL;
}

/* NOTE: dev can be NULL! */
static inline struct sk_buff *slsi_mlme_req_cfm_ind(struct slsi_dev *sdev,
						    struct net_device *dev,
						    struct sk_buff *skb,
						    u16 cfm_id,
						    u16 ind_id,
						    bool (*validate_cfm_wait_ind)(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *cfm))
{
	if (WARN_ON(!cfm_id))
		goto err;
	if (WARN_ON(!ind_id))
		goto err;
	if (WARN_ON(!validate_cfm_wait_ind))
		goto err;

	return slsi_mlme_tx_rx(sdev, dev, skb, cfm_id, NULL, ind_id, validate_cfm_wait_ind);

err:
	slsi_kfree_skb(skb);
	return NULL;
}

static struct ieee80211_reg_rule *slsi_get_reg_rule(u32 center_freq, struct slsi_802_11d_reg_domain *domain_info)
{
	struct ieee80211_reg_rule *rule;
	int                       i;

	for (i = 0; i < domain_info->regdomain->n_reg_rules; i++) {
		rule = &domain_info->regdomain->reg_rules[i];

		/* Consider 10Mhz on both side from the center frequency */
		if (((center_freq - MHZ_TO_KHZ(10)) >= rule->freq_range.start_freq_khz) &&
		    ((center_freq + MHZ_TO_KHZ(10)) <= rule->freq_range.end_freq_khz))
			return rule;
	}

	return NULL;
}

u16 slsi_compute_chann_info(struct slsi_dev *sdev, u16 width, u16 center_freq0, u16 channel_freq)
{
	u16 chann_info;
	u16 prim_chan_pos = 0;

	SLSI_DBG3(sdev, SLSI_MLME, "compute channel info\n");
	switch (width) {
	case NL80211_CHAN_WIDTH_20:
		chann_info = 20;
		break;
	case NL80211_CHAN_WIDTH_40:
		chann_info = 40;
		/* Check HT Minus */
		if (center_freq0 < channel_freq)
			chann_info |= SLSI_CHANN_INFO_HT_SCB;
		break;
	case NL80211_CHAN_WIDTH_80:
		/* F = { F1-30, ... F1+30 } => { 0x0000, ... 0x0300} */
		prim_chan_pos = ((30 + channel_freq - center_freq0) / 20);
		if (prim_chan_pos > 3) {
			SLSI_ERR(sdev, "Invalid center_freq0 in chandef : %u, primary channel = %u,"
				 "primary chan pos calculated = %d\n", center_freq0, channel_freq, prim_chan_pos);
			prim_chan_pos = 0;
		}
		prim_chan_pos = 0xFFFF & (prim_chan_pos << 8);
		chann_info = 80 | prim_chan_pos;
		break;
	default:
		SLSI_WARN(sdev, "Invalid chandef.width(0x%x)\n", width);
		chann_info = 0;
		break;
	}

	SLSI_DBG3(sdev, SLSI_MLME, "channel_width:%u, chann_info:0x%x\n", width, chann_info);
	return chann_info;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 9))
u16 slsi_get_chann_info(struct slsi_dev *sdev, struct cfg80211_chan_def *chandef)
{
	u16 chann_info = 0;

	SLSI_UNUSED_PARAMETER(sdev);

	if (chandef->width == NL80211_CHAN_WIDTH_20 || chandef->width == NL80211_CHAN_WIDTH_20_NOHT) {
		chann_info = 20;
		SLSI_DBG3(sdev, SLSI_MLME, "channel_width:%u, chann_info:0x%x\n", chandef->width, chann_info);
	} else if (chandef->chan) {
		chann_info = slsi_compute_chann_info(sdev, chandef->width, chandef->center_freq1,
						     chandef->chan->center_freq);
	}
	return chann_info;
}

int slsi_check_channelization(struct slsi_dev *sdev, struct cfg80211_chan_def *chandef,
			      int wifi_sharing_channel_switched)
{
	u8                        width;
	struct ieee80211_reg_rule *rule = NULL;
	struct ieee80211_channel  *channel = NULL;
	u32 ref_flags;

	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_20:
	case NL80211_CHAN_WIDTH_20_NOHT:
		width = 20;
		break;
	case NL80211_CHAN_WIDTH_40:
		width = 40;
		break;
	case NL80211_CHAN_WIDTH_80:
		width = 80;
		break;
	default:
		SLSI_ERR(sdev, "Invalid chandef.width(0x%x)\n", chandef->width);
		return -EINVAL;
	}

	channel =  ieee80211_get_channel(sdev->wiphy, chandef->chan->center_freq);
	if (!channel) {
		SLSI_ERR(sdev, "Invalid channel %d used to start AP. Channel not found\n", chandef->chan->center_freq);
		return -EINVAL;
	}

	if (wifi_sharing_channel_switched == 1) {
		ref_flags = IEEE80211_CHAN_DISABLED
#if LINUX_VERSION_CODE <= KERNEL_VERSION(3, 10, 13)
			    | IEEE80211_CHAN_PASSIVE_SCAN
#endif
			    ;
	} else {
		ref_flags = IEEE80211_CHAN_DISABLED |
			    IEEE80211_CHAN_RADAR
#if LINUX_VERSION_CODE <= KERNEL_VERSION(3, 10, 13)
			    | IEEE80211_CHAN_PASSIVE_SCAN
#endif
			    ;
	}

	if (channel->flags & ref_flags) {
		SLSI_ERR(sdev, "Invalid channel %d used to start AP\n", chandef->chan->center_freq);
		return -EINVAL;
	}
	rule = slsi_get_reg_rule(MHZ_TO_KHZ(chandef->center_freq1), &sdev->device_config.domain_info);
	if (!rule) {
		SLSI_ERR(sdev, "Invalid channel %d used to start AP. No reg rule found for this channel\n", chandef->chan->center_freq);
		return -EINVAL;
	}

	if (MHZ_TO_KHZ(width) <= rule->freq_range.max_bandwidth_khz) {
		u32 width_boundary1, width_boundary2;

		width_boundary1 = MHZ_TO_KHZ(chandef->center_freq1 - width / 2);
		width_boundary2 = MHZ_TO_KHZ(chandef->center_freq1 + width / 2);
		if ((width_boundary1 >= rule->freq_range.start_freq_khz) && (width_boundary2 <= rule->freq_range.end_freq_khz))
			return 0;
		SLSI_ERR(sdev, "Invalid channel %d used to start AP. Channel not within frequency range of the reg rule\n", chandef->chan->center_freq);
		return -EINVAL;
	}
	return -EINVAL;
}

#else
u16 slsi_get_chann_info(struct slsi_dev *sdev, enum nl80211_channel_type channel_type)
{
	u16 chann_info;

	SLSI_UNUSED_PARAMETER_NOT_DEBUG(sdev);

	/* Channel Info
	 * bits 0 ~ 7 : Channel Width (5, 10, 20, 40)
	 * bit 8 : Set to 1 if primary channel is greater than secondary channel (HT Minus)
	 */
	switch (channel_type) {
	case NL80211_CHAN_NO_HT:
	case NL80211_CHAN_HT20:
		chann_info = 20;
		break;
	case NL80211_CHAN_HT40MINUS:
		chann_info = 40 | SLSI_CHANN_INFO_HT_SCB;
		break;
	case NL80211_CHAN_HT40PLUS:
		chann_info = 40;
		break;
	default:
		SLSI_WARN(sdev, "Unknown channel_type: %d\n", channel_type);
		chann_info = 0;
		break;
	}

	SLSI_DBG3(sdev, SLSI_MLME, "channel_type:%d, chann_info:0x%x\n", channel_type, chann_info);
	return chann_info;
}

int slsi_check_channelization(struct slsi_dev *sdev, enum nl80211_channel_type channel_type)
{
	return 0;
}

#endif

/* Called in the case of MIB SET errors.
 * Decode and print a MIB buffer to the log for debug purposes.
 */
static void mib_buffer_dump_to_log(struct slsi_dev *sdev, u8 *mib_buffer, unsigned int mib_buffer_len)
{
	size_t                mib_decode_result;
	size_t                offset = 0;
	struct slsi_mib_entry decoded_mib_value;
	struct slsi_mib_data  mibdata;
	int                   error_out_len = mib_buffer_len * 3;
	int                   error_out_pos = 0;
	char                  *error_out;

	SLSI_UNUSED_PARAMETER(sdev);

	FUNC_ENTER(sdev);
	SLSI_ERR(sdev, "MIB buffer length: %u. MIB Error (decoded):", mib_buffer_len);

	if (!mib_buffer) {
		SLSI_ERR(sdev, "MIB buffer pointer is NULL - can not decode MIB keys\n");
		return;
	}
	error_out  = kmalloc(error_out_len, GFP_KERNEL);

	while (offset < mib_buffer_len) {
		error_out_pos = 0;
		mibdata.data = &mib_buffer[offset];
		mibdata.dataLength = mib_buffer_len - offset;

		mib_decode_result = slsi_mib_decode(&mibdata, &decoded_mib_value);
		if (!mib_decode_result) {
			SLSI_ERR_HEX(sdev, mibdata.data, mibdata.dataLength, "slsi_mib_decode() Failed to Decode:\n");
			break;
		}

		offset += mib_decode_result;
		/* Time for some eye candy - output the decoded MIB key at error level in the log */
		error_out_pos += snprintf(error_out + error_out_pos, error_out_len - error_out_pos, "%d", (int)(decoded_mib_value.psid));
		if (decoded_mib_value.index[0]) {
			error_out_pos += snprintf(error_out + error_out_pos, error_out_len - error_out_pos, ".%d", (int)(decoded_mib_value.index[0]));
			if (decoded_mib_value.index[1])
				error_out_pos += snprintf(error_out + error_out_pos, error_out_len - error_out_pos, ".%d", (int)(decoded_mib_value.index[1]));
		}

		switch (decoded_mib_value.value.type) {
		case SLSI_MIB_TYPE_BOOL:
			error_out_pos += snprintf(error_out + error_out_pos, error_out_len - error_out_pos, "=%s\n", decoded_mib_value.value.u.boolValue ? "TRUE" : "FALSE");
			break;
		case SLSI_MIB_TYPE_UINT:
			error_out_pos += snprintf(error_out + error_out_pos, error_out_len - error_out_pos, "=%d\n", (int)decoded_mib_value.value.u.uintValue);
			break;
		case SLSI_MIB_TYPE_INT:
			error_out_pos += snprintf(error_out + error_out_pos, error_out_len - error_out_pos, "=%d\n", (int)decoded_mib_value.value.u.intValue);
			break;
		case SLSI_MIB_TYPE_OCTET:
		{
			u32 i;

			error_out_pos += snprintf(error_out + error_out_pos, error_out_len - error_out_pos, "=[");
			for (i = 0; i < decoded_mib_value.value.u.octetValue.dataLength; i++)
				error_out_pos += snprintf(error_out + error_out_pos, error_out_len - error_out_pos, "%.2X", (int)decoded_mib_value.value.u.octetValue.data[i]);
			error_out_pos += snprintf(error_out + error_out_pos, error_out_len - error_out_pos, "]\n");
			break;
		}
		default:
			error_out_pos += snprintf(error_out + error_out_pos, error_out_len - error_out_pos, "=Can not decode MIB key type\n");
			break;
		}

		SLSI_INFO_NODEV("%s", error_out);
	}
	kfree(error_out);
	FUNC_EXIT(sdev);
}

int slsi_mlme_set_ip_address(struct slsi_dev *sdev, struct net_device *dev)
{
	struct sk_buff    *req;
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *cfm;
	int               r = 0;
	u32               ipaddr;
	u8                multicast_add[ETH_ALEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

	if (slsi_is_test_mode_enabled()) {
		SLSI_NET_INFO(dev, "Skip sending signal, WlanLite FW does not support MLME_SET_IP_ADDRESS.request\n");
		return -EOPNOTSUPP;
	}

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	req = fapi_alloc(mlme_set_ip_address_req, MLME_SET_IP_ADDRESS_REQ, ndev_vif->ifnum, sizeof(ndev_vif->ipaddress));
	if (!req)
		return -ENOMEM;

	fapi_set_u16(req, u.mlme_set_ip_address_req.ip_version, 4);
	fapi_set_memcpy(req, u.mlme_set_ip_address_req.multicast_address, multicast_add);

	ipaddr = htonl(be32_to_cpu(ndev_vif->ipaddress));
	fapi_append_data(req, (const u8 *)(&ipaddr), sizeof(ipaddr));

	SLSI_DBG2(sdev, SLSI_MLME, "slsi_mlme_set_ip_address(vif: %d, IP: %pI4)\n", ndev_vif->ifnum, &ndev_vif->ipaddress);
	cfm = slsi_mlme_req_cfm(sdev, dev, req, MLME_SET_IP_ADDRESS_CFM);
	if (!cfm)
		return -EIO;

	if (fapi_get_u16(cfm, u.mlme_set_ip_address_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_NET_ERR(dev, "mlme_set_ip_address_cfm(result:0x%04x) ERROR\n", fapi_get_u16(cfm, u.mlme_set_ip_address_cfm.result_code));
		r = -EINVAL;
	}

	slsi_kfree_skb(cfm);
	return r;
}

#ifndef CONFIG_SCSC_WLAN_BLOCK_IPV6

int slsi_mlme_set_ipv6_address(struct slsi_dev *sdev, struct net_device *dev)
{
	struct sk_buff    *req;
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *cfm;
	int               r = 0;
	u8                solicited_node_addr[ETH_ALEN] = { 0x33, 0x33, 0xff, 0x00, 0x00, 0x00 };

	if (slsi_is_test_mode_enabled()) {
		SLSI_NET_INFO(dev, "Skip sending signal, WlanLite FW does not support MLME_SET_IP_ADDRESS.request\n");
		return -EOPNOTSUPP;
	}

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	req = fapi_alloc(mlme_set_ip_address_req, MLME_SET_IP_ADDRESS_REQ, ndev_vif->ifnum, 16);
	if (!req)
		return -ENOMEM;

	fapi_set_u16(req, u.mlme_set_ip_address_req.ip_version, 6);

	if (ndev_vif->sta.nd_offload_enabled == 1) {
		slsi_spinlock_lock(&ndev_vif->ipv6addr_lock);
		memcpy(&solicited_node_addr[3], &ndev_vif->ipv6address.s6_addr[13], 3);
		slsi_spinlock_unlock(&ndev_vif->ipv6addr_lock);

		fapi_set_memcpy(req, u.mlme_set_ip_address_req.multicast_address, solicited_node_addr);
		fapi_append_data(req, ndev_vif->ipv6address.s6_addr, 16);
		SLSI_DBG2(sdev, SLSI_MLME, "mlme_set_ip_address_req(vif: %d, IP: %pI6)\n", ndev_vif->ifnum,
			  &ndev_vif->ipv6address);
	} else {
		u8  node_addr_nd_disable[16];

		memset(&node_addr_nd_disable, 0, sizeof(node_addr_nd_disable));
		fapi_append_data(req, node_addr_nd_disable, 16);
		SLSI_DBG2(sdev, SLSI_MLME, "mlme_set_ip_address_req(vif: %d, IP-setting ip address to all zeros)\n",
			  ndev_vif->ifnum);
	}

	cfm = slsi_mlme_req_cfm(sdev, dev, req, MLME_SET_IP_ADDRESS_CFM);
	if (!cfm)
		return -EIO;

	if (fapi_get_u16(cfm, u.mlme_set_ip_address_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_NET_ERR(dev, "mlme_set_ip_address_cfm(result:%u) ERROR\n", fapi_get_u16(cfm, u.mlme_set_ip_address_cfm.result_code));
		r = -EINVAL;
	}
	slsi_kfree_skb(cfm);
	return r;
}
#endif

int slsi_mlme_set(struct slsi_dev *sdev, struct net_device *dev, u8 *mib, int mib_len)
{
	struct sk_buff *req;
	struct sk_buff *cfm;
	int            r = 0;
	u16            ifnum = 0;

	if (dev) {
		struct netdev_vif *ndev_vif = netdev_priv(dev);

		ifnum = ndev_vif->ifnum;
	}

	req = fapi_alloc(mlme_set_req, MLME_SET_REQ, ifnum, mib_len);
	if (!req)
		return -ENOMEM;

	fapi_append_data(req, mib, mib_len);

	cfm = slsi_mlme_req_cfm(sdev, dev, req, MLME_SET_CFM);
	if (!cfm)
		return -EIO;

	if (fapi_get_datalen(cfm)) {
		mib_buffer_dump_to_log(sdev, fapi_get_data(cfm), fapi_get_datalen(cfm));
		r = -EINVAL;
	}

	slsi_kfree_skb(cfm);

	return r;
}

int slsi_mlme_get(struct slsi_dev *sdev, struct net_device *dev, u8 *mib, int mib_len, u8 *resp,
		  int resp_buf_len, int *resp_len)
{
	struct sk_buff *req;
	struct sk_buff *err = NULL;
	struct sk_buff *cfm;
	int            r = 0;
	u16            ifnum = 0;

	*resp_len = 0;

	if (dev) {
		struct netdev_vif *ndev_vif = netdev_priv(dev);

		ifnum = ndev_vif->ifnum;
	}
	req = fapi_alloc(mlme_get_req, MLME_GET_REQ, ifnum, mib_len);
	if (!req)
		return -ENOMEM;
	fapi_append_data(req, mib, mib_len);

	cfm = slsi_mlme_req_cfm_mib(sdev, dev, req, MLME_GET_CFM, &err);
	if (!cfm)
		return -EIO;

	if (err) {
		SLSI_DBG1(sdev, SLSI_MLME, "ERROR: mlme_get_cfm with mib error\n");
		mib_buffer_dump_to_log(sdev, fapi_get_data(err), fapi_get_datalen(err));
		LOG_CONDITIONALLY(fapi_get_datalen(cfm) > resp_buf_len,
				  SLSI_ERR(sdev, "Insufficient resp_buf_len(%d). mlme_get_cfm(%d)\n",
					   resp_buf_len, fapi_get_datalen(cfm)));
		r = -EINVAL;
	}

	/* if host has requested for multiple PSIDs in same request, we can get a
	 *   combination of error and success
	 */
	if (fapi_get_datalen(cfm) <= resp_buf_len) {
		*resp_len = fapi_get_datalen(cfm);
		memcpy(resp, fapi_get_data(cfm), fapi_get_datalen(cfm));
		r = 0;
	} else {
		SLSI_WARN(sdev, "Insufficient length (%d) to read MIB values, expected =%d\n", resp_buf_len, fapi_get_datalen(cfm));
		r = -EINVAL;
	}

	slsi_kfree_skb(err);
	slsi_kfree_skb(cfm);

	return r;
}

int slsi_mlme_add_vif(struct slsi_dev *sdev, struct net_device *dev, u8 *interface_address, u8 *device_address)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *req;
	struct sk_buff    *cfm;
	int               r = 0, i;

	if (slsi_is_test_mode_enabled()) {
		SLSI_NET_INFO(dev, "Skip sending signal, WlanLite FW does not support MLME_ADD_VIF.request\n");
		return -EOPNOTSUPP;
	}

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	/* reset host stats */
	for (i = 0; i < SLSI_LLS_AC_MAX; i++) {
		ndev_vif->tx_no_ack[i] = 0;
		ndev_vif->tx_packets[i] = 0;
		ndev_vif->rx_packets[i] = 0;
	}
	req = fapi_alloc(mlme_add_vif_req, MLME_ADD_VIF_REQ, ndev_vif->ifnum, 0);
	if (!req)
		return -ENOMEM;
	fapi_set_u16(req, u.mlme_add_vif_req.virtual_interface_type, ndev_vif->vif_type);
	fapi_set_memcpy(req, u.mlme_add_vif_req.interface_address, interface_address);
	fapi_set_memcpy(req, u.mlme_add_vif_req.device_address, device_address);
	SLSI_NET_DBG2(dev, SLSI_MLME, "mlme_add_vif_req(vif:%d)\n", ndev_vif->ifnum);
	cfm = slsi_mlme_req_cfm(sdev, dev, req, MLME_ADD_VIF_CFM);
	if (!cfm)
		return -EIO;

	if (fapi_get_u16(cfm, u.mlme_add_vif_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_NET_ERR(dev, "mlme_add_vif_cfm(result:%u) ERROR\n", fapi_get_u16(cfm, u.mlme_add_vif_cfm.result_code));
		r = -EINVAL;
	}

	/* By default firmware vif will be in active mode */
	ndev_vif->power_mode = FAPI_POWERMANAGEMENTMODE_ACTIVE_MODE;

	slsi_kfree_skb(cfm);
	return r;
}

void slsi_mlme_del_vif(struct slsi_dev *sdev, struct net_device *dev)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *req;
	struct sk_buff    *cfm;

	if (slsi_is_test_mode_enabled()) {
		SLSI_NET_INFO(dev, "Skip sending signal, WlanLite FW does not support MLME_DEL_VIF.request\n");
		return;
	}

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	SLSI_NET_DBG2(dev, SLSI_MLME, "mlme_del_vif_req(vif:%d)\n", ndev_vif->ifnum);
	req = fapi_alloc(mlme_del_vif_req, MLME_DEL_VIF_REQ, ndev_vif->ifnum, 0);
	if (!req)
		return;

	cfm = slsi_mlme_req_cfm(sdev, dev, req, MLME_DEL_VIF_CFM);
	if (!cfm)
		return;

	if (fapi_get_u16(cfm, u.mlme_del_vif_cfm.result_code) != FAPI_RESULTCODE_SUCCESS)
		SLSI_NET_ERR(dev, "mlme_del_vif_cfm(result:%u) ERROR\n", fapi_get_u16(cfm, u.mlme_del_vif_cfm.result_code));

	if (((ndev_vif->iftype == NL80211_IFTYPE_P2P_CLIENT) || (ndev_vif->iftype == NL80211_IFTYPE_STATION)) &&
	    (ndev_vif->delete_probe_req_ies)) {
		kfree(ndev_vif->probe_req_ies);
		ndev_vif->probe_req_ies = NULL;
		ndev_vif->probe_req_ie_len = 0;
		ndev_vif->delete_probe_req_ies = false;
	}

	slsi_kfree_skb(cfm);
}

int slsi_mlme_set_channel(struct slsi_dev *sdev, struct net_device *dev, struct ieee80211_channel *chan, u16 duration, u16 interval, u16 count)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *req;
	struct sk_buff    *cfm;
	int               r = 0;

	if (slsi_is_test_mode_enabled()) {
		SLSI_NET_INFO(dev, "wlanlite does not support MLME_SET_CHANNEL.request\n");
		return -EOPNOTSUPP;
	}

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	SLSI_NET_DBG2(dev, SLSI_MLME, "mlme_set_channel_req(freq:%u, duration:%u, interval:%u, count:%u)\n", chan->center_freq, duration, interval, count);

	req = fapi_alloc(mlme_set_channel_req, MLME_SET_CHANNEL_REQ, ndev_vif->ifnum, 0);
	if (!req)
		return -ENOMEM;

	fapi_set_u16(req, u.mlme_set_channel_req.availability_duration, duration);
	fapi_set_u16(req, u.mlme_set_channel_req.availability_interval, interval);
	fapi_set_u16(req, u.mlme_set_channel_req.count, count);
	fapi_set_u16(req, u.mlme_set_channel_req.channel_frequency, SLSI_FREQ_HOST_TO_FW(chan->center_freq));

	cfm = slsi_mlme_req_cfm(sdev, dev, req, MLME_SET_CHANNEL_CFM);
	if (!cfm)
		return -EIO;

	if (fapi_get_u16(cfm, u.mlme_set_channel_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_NET_ERR(dev, "mlme_set_channel_cfm(result:%u) ERROR\n", fapi_get_u16(cfm, u.mlme_set_channel_cfm.result_code));
		r = -EINVAL;
	}

	slsi_kfree_skb(cfm);
	return r;
}

void slsi_ap_obss_scan_done_ind(struct net_device *dev, struct netdev_vif *ndev_vif)
{
	struct sk_buff *scan_res;
	u16            scan_id = SLSI_SCAN_HW_ID;

	SLSI_UNUSED_PARAMETER(dev);

	SLSI_NET_DBG1(dev, SLSI_MLME, "Scan before AP start completed\n");

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));
	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->scan_mutex));
	SLSI_MUTEX_LOCK(ndev_vif->scan_result_mutex);

	scan_res = slsi_dequeue_cached_scan_result(&ndev_vif->scan[scan_id], NULL);
	while (scan_res) {
		struct ieee80211_mgmt *mgmt = fapi_get_mgmt(scan_res);
		size_t                mgmt_len = fapi_get_mgmtlen(scan_res);
		size_t                ie_len = mgmt_len - offsetof(struct ieee80211_mgmt, u.beacon.variable); /* ieee80211_mgmt structure is similar for Probe Response and Beacons */

		SLSI_NET_DBG4(dev, SLSI_MLME, "OBSS scan result (scan_id:%d, %pM, freq:%d, rssi:%d, ie_len = %zu)\n",
			      fapi_get_u16(scan_res, u.mlme_scan_ind.scan_id),
			      fapi_get_mgmt(scan_res)->bssid,
			      fapi_get_u16(scan_res, u.mlme_scan_ind.channel_frequency) / 2,
			      fapi_get_s16(scan_res, u.mlme_scan_ind.rssi),
			      ie_len);

		if (!cfg80211_find_ie(WLAN_EID_HT_CAPABILITY, mgmt->u.beacon.variable, ie_len)) {
			SLSI_NET_DBG1(dev, SLSI_MLME, "Non HT BSS detected on primary channel\n");
			ndev_vif->ap.non_ht_bss_present = true;
		}

		slsi_kfree_skb(scan_res);
		scan_res = slsi_dequeue_cached_scan_result(&ndev_vif->scan[scan_id], NULL);
	}
	SLSI_MUTEX_UNLOCK(ndev_vif->scan_result_mutex);
}

/* Null check for cfm done in caller function */
static bool slsi_scan_cfm_validate(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *cfm)
{
	bool r = true;

	if (fapi_get_u16(cfm, u.mlme_add_scan_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_ERR_NODEV("mlme_add_scan_cfm(result:%u) ERROR\n", fapi_get_u16(cfm, u.mlme_add_scan_cfm.result_code));
		r = false;
	}

	slsi_kfree_skb(cfm);
	return r;
}

#ifdef CONFIG_SCSC_WLAN_GSCAN_ENABLE
int slsi_mlme_set_bssid_hotlist_req(struct slsi_dev *sdev, struct net_device *dev, struct slsi_nl_hotlist_param *nl_hotlist_param)
{
	struct sk_buff *req;
	struct sk_buff *cfm;
	int            i;
	int            num_desc;
	int            alloc_data_size;
	int            remaining_ap;
	u8             bssid_desc[] = { 0xdd,             /* Element ID */
					0x00,             /* Length */
					0x00, 0x16, 0x32, /* Samsung OUI */
					0x01,             /* OUI Type: Scan Parameters */
					0x05,             /* OUI Subtype: BSSID Descriptor */
					0x00,             /* AP Lost Threshold */
	};

	if (nl_hotlist_param) {
		/* One BSSID descriptor can contain SLSI_GSCAN_MAX_BSSID_PER_IE AP information */
		num_desc =  (nl_hotlist_param->num_bssid / SLSI_GSCAN_MAX_BSSID_PER_IE);
		if (nl_hotlist_param->num_bssid % SLSI_GSCAN_MAX_BSSID_PER_IE)
			num_desc++;

		alloc_data_size = (num_desc * sizeof(bssid_desc)) + (nl_hotlist_param->num_bssid * SLSI_FW_BSSID_DESCRIPTOR_BSS_SIZE);
		req = fapi_alloc(mlme_set_bssid_hotlist_req, MLME_SET_BSSID_HOTLIST_REQ, 0, alloc_data_size);
		if (!req) {
			SLSI_NET_ERR(dev, "Failed to allocate memory\n");
			return -ENOMEM;
		}

		remaining_ap = nl_hotlist_param->num_bssid;
		bssid_desc[7] = nl_hotlist_param->lost_ap_sample_size;
		for (i = 0; i < nl_hotlist_param->num_bssid; i++) {
			if ((i % SLSI_GSCAN_MAX_BSSID_PER_IE) == 0) {
				if (remaining_ap > SLSI_GSCAN_MAX_BSSID_PER_IE) {
					bssid_desc[1] = sizeof(bssid_desc) - 2 + (SLSI_GSCAN_MAX_BSSID_PER_IE * SLSI_FW_BSSID_DESCRIPTOR_BSS_SIZE); /* Length */
					remaining_ap -= SLSI_GSCAN_MAX_BSSID_PER_IE;
				} else {
					bssid_desc[1] = sizeof(bssid_desc) - 2 + (remaining_ap * SLSI_FW_BSSID_DESCRIPTOR_BSS_SIZE); /* Length */
				}

				fapi_append_data(req, bssid_desc, sizeof(bssid_desc));
			}

			fapi_append_data(req, (u8 *)&nl_hotlist_param->ap[i].bssid[0], ETH_ALEN);

			nl_hotlist_param->ap[i].low = cpu_to_le16(nl_hotlist_param->ap[i].low);
			fapi_append_data(req, (u8 *)&nl_hotlist_param->ap[i].low, 2);

			nl_hotlist_param->ap[i].high = cpu_to_le16(nl_hotlist_param->ap[i].high);
			fapi_append_data(req, (u8 *)&nl_hotlist_param->ap[i].high, 2);
		}
	} else {
		req = fapi_alloc(mlme_set_bssid_hotlist_req, MLME_SET_BSSID_HOTLIST_REQ, 0, 0);
		if (!req) {
			SLSI_NET_ERR(dev, "Failed to allocate memory\n");
			return -ENOMEM;
		}
	}

	cfm = slsi_mlme_req_cfm(sdev, NULL, req, MLME_SET_BSSID_HOTLIST_CFM);
	if (!cfm)
		return -EIO;

	SLSI_DBG2(sdev, SLSI_GSCAN, "mlme_set_bssid_hotlist_cfm(datalen:%u)\n", fapi_get_datalen(cfm));
	if (fapi_get_u16(cfm, u.mlme_set_bssid_hotlist_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_NET_ERR(dev, "mlme_set_bssid_hotlist_cfm(result: %#x) ERROR\n",
			     fapi_get_u16(cfm, u.mlme_set_bssid_hotlist_cfm.result_code));
		slsi_kfree_skb(cfm);
		return -EINVAL;
	}

	slsi_kfree_skb(cfm);
	return 0;
}

int slsi_mlme_append_gscan_channel_list(struct slsi_dev             *sdev,
					struct net_device           *dev,
					struct sk_buff              *req,
					struct slsi_nl_bucket_param *nl_bucket)
{
	u16      channel_freq;
	u8       i;
	u8       *p;
	const u8          channels_list_ie_header[] = {
		0xDD,					/* Element ID: Vendor Specific */
		0x05,					/* Length: actual length will be updated later */
		0x00, 0x16, 0x32,       /* OUI: Samsung Electronics Co. */
		0x01,					/* OUI Type:  Scan parameters */
		0x02					/* OUI Subtype: channel list */
	};
	u8       *channels_list_ie = fapi_append_data(req, channels_list_ie_header, sizeof(channels_list_ie_header));

	if (!channels_list_ie) {
		SLSI_WARN(sdev, "channel list IE append failed\n");
		slsi_kfree_skb(req);
		return -EINVAL;
	}

	if (nl_bucket->band == WIFI_BAND_UNSPECIFIED)
		/* channel list is added only if band is UNSPECIFIED */
		for (i = 0; i < nl_bucket->num_channels; i++) {
			p = fapi_append_data(req, NULL, SLSI_SCAN_CHANNEL_DESCRIPTOR_SIZE);
			if (!p) {
				SLSI_ERR(sdev, "chan desc[%d] append failed\n", i);
				slsi_kfree_skb(req);
				return -EINVAL;
			}
			channel_freq = SLSI_FREQ_HOST_TO_FW(nl_bucket->channels[i].channel);
			channel_freq = cpu_to_le16(channel_freq);
			memcpy(p, &channel_freq, sizeof(channel_freq));
			p[2] = FAPI_SCANPOLICY_ANY_RA;
			channels_list_ie[1] += SLSI_SCAN_CHANNEL_DESCRIPTOR_SIZE; /* Length */
		}
	else {
		p = fapi_append_data(req, NULL, SLSI_SCAN_CHANNEL_DESCRIPTOR_SIZE);
		if (!p) {
			SLSI_ERR(sdev, "chan desc(band specific)append failed\n");
			slsi_kfree_skb(req);
			return -EINVAL;
		}
		/* Channel frequency set to 0 for all channels allowed by the corresponding regulatory domain and scan policy */
		channel_freq = 0;
		memcpy(p, &channel_freq, sizeof(channel_freq));
		p[2] = slsi_gscan_get_scan_policy(nl_bucket->band);
		channels_list_ie[1] += SLSI_SCAN_CHANNEL_DESCRIPTOR_SIZE;
	}

	return 0;
}
#endif

static int slsi_mlme_append_channel_list(struct slsi_dev                    *sdev,
					 struct net_device                  *dev,
					 struct sk_buff                     *req,
					 u32                                num_channels,
					 struct ieee80211_channel           *channels[],
					 u16                                scan_type,
					 bool                               passive_scan)
{
	int               chann;
	u16               freq_fw_unit;
	u8                i;
	int               n_valid_channels = 0;
	u8                *p;

	const u8          channels_list_ie_header[] = {
		0xDD,                   /* Element ID: vendor specific */
		0x05,                   /* Length: actual length will be updated later */
		0x00, 0x16, 0x32,       /* OUI: Samsung Electronics Co. */
		0x01,                   /* OUI Type: scan parameters */
		0x02                    /* OUI Subtype: channel list */
	};

	u8 *channels_list_ie = fapi_append_data(req, channels_list_ie_header, sizeof(channels_list_ie_header));

	if (!channels_list_ie) {
		SLSI_WARN(sdev, "channel list IE append failed\n");
		slsi_kfree_skb(req);
		return -EINVAL;
	}

	/* For P2P Full Scan, Setting Channel Frequency = 0x0000, Scan Policy = 2.4GHz, 5GHz and Non-Dfs. */
	if (scan_type == FAPI_SCANTYPE_P2P_SCAN_FULL) {
		p = fapi_append_data(req, NULL, SLSI_SCAN_CHANNEL_DESCRIPTOR_SIZE);
		if (!p) {
			SLSI_WARN(sdev, "scan channel descriptor append failed\n");
			slsi_kfree_skb(req);
			return -EINVAL;
		}
		p[0] = 0;
		p[1] = 0;
		p[2] = FAPI_SCANPOLICY_2_4GHZ | FAPI_SCANPOLICY_5GHZ | FAPI_SCANPOLICY_NON_DFS;
		channels_list_ie[1] += SLSI_SCAN_CHANNEL_DESCRIPTOR_SIZE;
		return 0;
	}

	for (i = 0; i < num_channels; i++) {
		chann = channels[i]->hw_value & 0xFF;

		if (sdev->device_config.supported_band) {
			if (channels[i]->band == IEEE80211_BAND_2GHZ && sdev->device_config.supported_band != SLSI_FREQ_BAND_2GHZ)
				continue;
			if (channels[i]->band == IEEE80211_BAND_5GHZ && sdev->device_config.supported_band != SLSI_FREQ_BAND_5GHZ)
				continue;
		}

		n_valid_channels++;
		p = fapi_append_data(req, NULL, SLSI_SCAN_CHANNEL_DESCRIPTOR_SIZE);
		if (!p) {
			SLSI_WARN(sdev, "scan channel descriptor append failed\n");
			slsi_kfree_skb(req);
			return -EINVAL;
		}
		freq_fw_unit = 2 * ieee80211_channel_to_frequency(chann, (chann <= 14) ?
								  NL80211_BAND_2GHZ : NL80211_BAND_5GHZ);
		freq_fw_unit = cpu_to_le16(freq_fw_unit);
		memcpy(p, &freq_fw_unit, sizeof(freq_fw_unit));

		if (passive_scan && (scan_type != FAPI_SCANTYPE_AP_AUTO_CHANNEL_SELECTION))
			p[2] = FAPI_SCANPOLICY_PASSIVE;
		else
			p[2] = 0;

		channels_list_ie[1] += SLSI_SCAN_CHANNEL_DESCRIPTOR_SIZE;
	}
	if (n_valid_channels == 0) {
		SLSI_NET_ERR(dev, "no valid channels to Scan\n");
		slsi_kfree_skb(req);
		return -EINVAL;
	}
	return 0;
}

static inline int slsi_set_scan_params(
	struct net_device	*dev,
	u16			scan_id,
	u16			scan_type,
	u16			report_mode,
	int			num_ssids,
	struct cfg80211_ssid	*ssids,
	struct sk_buff		*req)
{
	u8			*p = NULL;
	u8			i;
	struct cfg80211_ssid	*pssid = ssids;
#ifdef CONFIG_SCSC_WLAN_ENABLE_MAC_RANDOMISATION
	struct netdev_vif        *netdev_vif = netdev_priv(dev);
	struct slsi_dev            *sdev = netdev_vif->sdev;
#endif

	fapi_set_u16(req, u.mlme_add_scan_req.scan_id, scan_id);
	fapi_set_u16(req, u.mlme_add_scan_req.scan_type, scan_type);
	fapi_set_u16(req, u.mlme_add_scan_req.report_mode_bitmap, report_mode);


#ifdef CONFIG_SCSC_WLAN_ENABLE_MAC_RANDOMISATION
	if (sdev->scan_addr_set)
		fapi_set_memcpy(req, u.mlme_add_scan_req.device_address, sdev->scan_mac_addr);
	else
#endif
	fapi_set_memcpy(req, u.mlme_add_scan_req.device_address, dev->dev_addr);

	for (i = 0; i < num_ssids; i++, pssid++) {
		p = fapi_append_data(req, NULL, 2 + pssid->ssid_len);
		if (!p) {
			slsi_kfree_skb(req);
			SLSI_NET_WARN(dev, "fail to append SSID element to scan request\n");
			return -EINVAL;
		}

		*p++ = WLAN_EID_SSID;
		*p++ = pssid->ssid_len;

		if (pssid->ssid_len)
			memcpy(p, pssid->ssid, pssid->ssid_len);
	}
	return 0;
}

int slsi_mlme_add_sched_scan(struct slsi_dev                    *sdev,
			     struct net_device                  *dev,
			     struct cfg80211_sched_scan_request *request,
			     const u8                           *ies,
			     u16                                ies_len)
{
	struct netdev_vif                  *ndev_vif = netdev_priv(dev);
	struct sk_buff                     *req;
	struct sk_buff                     *rx;
	int                                r = 0;
	size_t                             alloc_data_size = 0;
	u32                                i;

	/* Scan Timing IE: default values */
	u8 scan_timing_ie[] = {
		0xdd,				/* Element ID: Vendor Specific */
		0x11,				/* Length */
		0x00, 0x16, 0x32,		/* OUI: Samsung Electronics Co. */
		0x01,				/* OUI Type: Scan parameters */
		0x01,				/* OUI Subtype: Scan timing */
		0x00, 0x00, 0x00, 0x00,		/* Min_Period:  filled later in the function */
		0x00, 0x00, 0x00, 0x00,		/* Max_Period:  filled later in the function */
		0x00,				/* Exponent */
		0x00,				/* Step count */
		0x00, 0x01			/* Skip first period: true for scheduled scans*/
	};

	u8 ssid_filter_ie_hdr[] = {
		0xdd,			/* Element ID: Vendor Specific */
		0x05,			/* Length */
		0x00, 0x16, 0x32,	/* OUI: Samsung Electronics Co. */
		0x01,			/* OUI Type: Scan parameters */
		0x04			/* OUI Subtype: SSID Filter */
	};

	if (slsi_is_test_mode_enabled()) {
		SLSI_NET_WARN(dev, "not supported in WlanLite mode\n");
		return -EOPNOTSUPP;
	}

	if (WARN_ON(!(dev->dev_addr)))
		return -EINVAL;

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->scan_mutex));

	alloc_data_size += sizeof(scan_timing_ie) +
					ies_len +
					SLSI_SCAN_PRIVATE_IE_SSID_FILTER_HEADER_LEN +
					(SLSI_SCAN_PRIVATE_IE_CHANNEL_LIST_HEADER_LEN + (request->n_channels * SLSI_SCAN_CHANNEL_DESCRIPTOR_SIZE));

	for (i = 0; i < request->n_ssids; i++) {
		/* 2 bytes for SSID EID and length field +  variable length SSID */
		alloc_data_size += (2 + request->ssids[i].ssid_len);
	}

	for (i = 0; i < request->n_match_sets; i++) {
		/* 1 byte for SSID_length field + variable length SSID */
		alloc_data_size += (1 + request->match_sets[i].ssid.ssid_len);
	}

	req = fapi_alloc(mlme_add_scan_req, MLME_ADD_SCAN_REQ, 0, alloc_data_size);
	if (!req)
		return -ENOMEM;

	r =  slsi_set_scan_params(dev, (ndev_vif->ifnum << 8 | SLSI_SCAN_SCHED_ID),
				  FAPI_SCANTYPE_SCHEDULED_SCAN,
				  FAPI_REPORTMODE_REAL_TIME,
				  request->n_ssids,
				  request->ssids,
				  req);
	if (r)
		return r;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
	SLSI_U32_TO_BUFF_LE(request->scan_plans->interval * 1000, &scan_timing_ie[7]);
#else
	SLSI_U32_TO_BUFF_LE(request->interval * 1000, &scan_timing_ie[7]);
#endif
	fapi_append_data(req, scan_timing_ie, sizeof(scan_timing_ie));
	fapi_append_data(req, ies, ies_len);

	if (request->n_match_sets) {
		struct cfg80211_match_set *match_sets = request->match_sets;
		u8 *ssid_filter_ie = fapi_append_data(req, ssid_filter_ie_hdr, sizeof(ssid_filter_ie_hdr));

		if  (!ssid_filter_ie) {
			slsi_kfree_skb(req);
			SLSI_ERR(sdev, "ssid_filter_ie append failed\n");
			return -EIO;
		}
		for (i = 0; i < request->n_match_sets; i++, match_sets++) {
			SLSI_NET_DBG2(dev, SLSI_MLME, "SSID: %.*s",  match_sets->ssid.ssid_len,  match_sets->ssid.ssid);
			ssid_filter_ie[1] += (1 + match_sets->ssid.ssid_len);
			fapi_append_data(req, &match_sets->ssid.ssid_len, 1);
			fapi_append_data(req, match_sets->ssid.ssid, match_sets->ssid.ssid_len);
		}
	}

	if (request->n_channels) {
		r = slsi_mlme_append_channel_list(sdev, dev, req, request->n_channels, request->channels,
						  FAPI_SCANTYPE_SCHEDULED_SCAN, request->n_ssids == 0);
		if (r)
			return r;
	}

	rx = slsi_mlme_req_cfm(sdev, NULL, req, MLME_ADD_SCAN_CFM);
	if (!rx)
		return -EIO;

	if (fapi_get_u16(rx, u.mlme_add_scan_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_NET_ERR(dev, "mlme_add_scan_cfm(result:%u) ERROR\n", fapi_get_u16(rx, u.mlme_add_scan_cfm.result_code));
		r = -EINVAL;
	}

	slsi_kfree_skb(rx);
	return r;
}

int slsi_mlme_add_scan(
	struct slsi_dev			*sdev,
	struct net_device		*dev,
	u16				scan_type,
	u16				report_mode,
	u32				n_ssids,
	struct cfg80211_ssid		*ssids,
	u32				n_channels,
	struct ieee80211_channel	*channels[],
	void				*gscan,
	const u8			*ies,
	u16				ies_len,
	bool				wait_for_ind)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *req;
	struct sk_buff    *rx;
	int               r = 0;
	size_t            alloc_data_size = 0;
	u32               i;

	/* Scan Timing IE: default values */
	u8 scan_timing_ie[] = {
		0xdd,				/* Element ID: Vendor Specific */
		0x11,				/* Length */
		0x00, 0x16, 0x32,		/* OUI: Samsung Electronics Co. */
		0x01,				/* OUI Type: Scan parameters */
		0x01,				/* OUI Subtype: Scan timing */
		0x00, 0x00, 0x00, 0x00,		/* Min_Period:  filled later in the function */
		0x00, 0x00, 0x00, 0x00,		/* Max_Period:  filled later in the function */
		0x00,				/* Exponent */
		0x00,				/* Step count */
		0x00, 0x00			/* Skip first period: false */
	};

	if (slsi_is_test_mode_enabled()) {
		SLSI_NET_WARN(dev, "not supported in WlanLite mode\n");
		return -EOPNOTSUPP;
	}

	if (WARN_ON(!(dev->dev_addr)))
		return -EINVAL;

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->scan_mutex));
	SLSI_INFO(sdev, "scan started for id:0x%x, n_channels:%d, n_ssids:%d, scan_type:%d\n",
		  (ndev_vif->ifnum << 8 | SLSI_SCAN_HW_ID), n_channels, n_ssids, scan_type);

	alloc_data_size += sizeof(scan_timing_ie) +
					ies_len +
					(SLSI_SCAN_PRIVATE_IE_CHANNEL_LIST_HEADER_LEN + (n_channels * SLSI_SCAN_CHANNEL_DESCRIPTOR_SIZE));

	for (i = 0; i < n_ssids; i++)
		alloc_data_size += 2 + ssids[i].ssid_len; /* 2: SSID EID + len */

	req = fapi_alloc(mlme_add_scan_req, MLME_ADD_SCAN_REQ, 0, alloc_data_size);
	if (!req)
		return -ENOMEM;

	if (!gscan) {
		r =  slsi_set_scan_params(
			dev,
			(ndev_vif->ifnum << 8 | SLSI_SCAN_HW_ID),
			scan_type,
			report_mode,
			n_ssids,
			ssids,
			req);
		if (r)
			return r;

		fapi_append_data(req, scan_timing_ie, sizeof(scan_timing_ie));
		fapi_append_data(req, ies, ies_len);

		if (n_channels) {
			r = slsi_mlme_append_channel_list(sdev, dev, req, n_channels, channels, scan_type,
							  n_ssids == 0);
			if (r)
				return r;
		}
	}
#ifdef CONFIG_SCSC_WLAN_GSCAN_ENABLE
	else {
		struct slsi_gscan_param *gscan_param = (struct slsi_gscan_param *)gscan;

		r =  slsi_set_scan_params(
			dev,
			gscan_param->bucket->scan_id,
			scan_type,
			report_mode,
			n_ssids,
			ssids,
			req);
		if (r)
			return r;

		SLSI_U32_TO_BUFF_LE((gscan_param->nl_bucket->period * 1000), &scan_timing_ie[7]);
		if (gscan_param->nl_bucket->exponent) {
			SLSI_U32_TO_BUFF_LE((gscan_param->nl_bucket->max_period * 1000), &scan_timing_ie[11]);
			scan_timing_ie[15] = (u8)gscan_param->nl_bucket->exponent;
			scan_timing_ie[16] = (u8)gscan_param->nl_bucket->step_count;
		}
		fapi_append_data(req, scan_timing_ie, sizeof(scan_timing_ie));

		r = slsi_mlme_append_gscan_channel_list(sdev, dev, req, gscan_param->nl_bucket);
		if (r)
			return r;
	}
#endif
	if (wait_for_ind) {
		/* Use the Global sig_wait not the Interface specific for Scan Req */
		rx = slsi_mlme_req_cfm_ind(sdev, NULL, req, MLME_ADD_SCAN_CFM, MLME_SCAN_DONE_IND, slsi_scan_cfm_validate);
		if (!rx)
			return -EIO;
		SLSI_NET_DBG3(dev, SLSI_MLME, "mlme_scan_done_ind()\n");

		/* slsi_mlme_add_scan is a generic definition for multiple handlers
		 * Any added functionality, if not generic, should not be defined here.
		 * It should be a part of calling function.
		 */
	} else {
		/* Use the Global sig_wait not the Interface specific for Scan Req */
		rx = slsi_mlme_req_cfm(sdev, NULL, req, MLME_ADD_SCAN_CFM);
		if (!rx)
			return -EIO;
		if (fapi_get_u16(rx, u.mlme_add_scan_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
			SLSI_NET_ERR(dev, "mlme_add_scan_cfm(result:%u) ERROR\n", fapi_get_u16(rx, u.mlme_add_scan_cfm.result_code));
			r = -EINVAL;
		}
	}
	slsi_kfree_skb(rx);
	return r;
}

int slsi_mlme_del_scan(struct slsi_dev *sdev, struct net_device *dev, u16 scan_id, bool scan_timed_out)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff *req;
	struct sk_buff *cfm;
	int            r = 0;

	if (slsi_is_test_mode_enabled()) {
		SLSI_NET_WARN(dev, "not supported in WlanLite mode\n");
		return -EOPNOTSUPP;
	}

	SLSI_NET_DBG2(dev, SLSI_MLME, "mlme_del_scan_req(scan_id:%d)\n", scan_id);

	if ((scan_id & 0xFF) == SLSI_SCAN_HW_ID && ndev_vif->scan[SLSI_SCAN_HW_ID].scan_req && !scan_timed_out)
		cancel_delayed_work(&ndev_vif->scan_timeout_work);

	req = fapi_alloc(mlme_del_scan_req, MLME_DEL_SCAN_REQ, 0, 0);
	if (!req)
		return -ENOMEM;
	fapi_set_u16(req, u.mlme_del_scan_req.scan_id, scan_id);

	/* Use the Global sig_wait not the Interface specific for Scan Req */
	cfm = slsi_mlme_req_cfm(sdev, NULL, req, MLME_DEL_SCAN_CFM);
	if (!cfm)
		return -EIO;

	if (fapi_get_u16(cfm, u.mlme_del_scan_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_NET_ERR(dev, "mlme_del_scan_cfm(result:%u) ERROR\n", fapi_get_u16(cfm, u.mlme_del_scan_cfm.result_code));
		r = -EINVAL;
	}

	slsi_kfree_skb(cfm);
	return r;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 9))
static void slsi_ap_add_ext_capab_ie(struct sk_buff *req, struct netdev_vif *ndev_vif, const u8 *prev_ext)
{
	u8  ext_capa_ie[SLSI_AP_EXT_CAPAB_IE_LEN_MAX];
	int ie_len = 8;

	if (prev_ext && prev_ext[1] > ie_len)
		ie_len = prev_ext[1];

	ext_capa_ie[0] = WLAN_EID_EXT_CAPABILITY;
	ext_capa_ie[1] = ie_len;
	memset(&ext_capa_ie[2], 0, ie_len);

	if (prev_ext)
		memcpy(&ext_capa_ie[2], &prev_ext[2], prev_ext[1]);

	SLSI_DBG3(ndev_vif->sdev, SLSI_MLME, "New Ext capab Added\n");
	/* Set the Operating Mode Notification field - Bit 62 (8th Octet)*/
	ext_capa_ie[9] |= 0x40;

	fapi_append_data(req, &ext_capa_ie[0], ie_len + 2);
}
#endif

static int slsi_prepare_country_ie(struct slsi_dev *sdev, u16 center_freq, u8 *country_ie, u8 **new_country_ie)
{
	struct ieee80211_supported_band band;
	struct ieee80211_reg_rule       *rule;
	struct ieee80211_channel        *channels;
	u8                              *ie;
	u8                              offset = 0;
	int                             i;

	/* Select frequency band */
	if (center_freq < 5180)
		band = slsi_band_2ghz;
	else
		band = slsi_band_5ghz;

	/* Allocate memory for the new country IE - EID(1) + Len(1) + CountryString(3) + ChannelInfo (n * 3) */
	ie = kmalloc(5 + (band.n_channels * 3), GFP_KERNEL);
	if (!ie) {
		SLSI_ERR(sdev, "Failed to allocate memory\n");
		return -ENOMEM;
	}

	/* Preapre the new country IE */
	ie[offset++] = country_ie[0];                                                        /* Element IE */
	ie[offset++] = 0;                                                                    /* IE Length - initialized at the end of this function */
	ie[offset++] = sdev->device_config.domain_info.regdomain->alpha2[0];                 /* Country code */
	ie[offset++] = sdev->device_config.domain_info.regdomain->alpha2[1];                 /* Country code */
	ie[offset++] = country_ie[4];                                                        /* CountryString: 3rd octet */

	channels = band.channels;
	for (i = 0; i < band.n_channels; i++, channels++) {
		/* Get the regulatory rule for the channel */
		rule = slsi_get_reg_rule(MHZ_TO_KHZ(channels->center_freq), &sdev->device_config.domain_info);
		if (rule) {
			ie[offset++] = channels->hw_value;                                    /* Channel number */
			ie[offset++] = 1;                                                     /* Number of channels */
			ie[offset++] = MBM_TO_DBM(rule->power_rule.max_eirp);                 /* Max TX power */
		}
	}

	ie[1] = offset - 2;                 /* Length of IE */
	*new_country_ie = ie;

	return 0;
}

int slsi_modify_ies(struct net_device *dev, u8 eid, u8 *ies, int ies_len, u8 ie_index, u8 ie_value)
{
	u8 *ie;

	SLSI_NET_DBG1(dev, SLSI_MLME, "eid: %d, ie_value = 0x%x\n", eid, ie_value);

	ie = (u8 *)cfg80211_find_ie(eid, ies, ies_len);
	if (ie) {
		switch (eid) {
		case WLAN_EID_HT_CAPABILITY:
		case WLAN_EID_VHT_CAPABILITY:
			ie[ie_index] |= ie_value;
			break;
		case WLAN_EID_DS_PARAMS:
		case WLAN_EID_HT_OPERATION:
			if (ie_index == 2)
				ie[ie_index] = ie_value;
			else
				ie[ie_index] |= ie_value;
			break;
		default:
			SLSI_NET_WARN(dev, "slsi_modify_ies: IE type mismatch : %d\n", eid);
			return false;
		}
		return true;
	}
	SLSI_NET_WARN(dev, "slsi_modify_ies: IE not found : %d\n", eid);
	return false;
}

static void slsi_mlme_start_prepare_ies(struct sk_buff *req, struct netdev_vif *ndev_vif, struct cfg80211_ap_settings *settings, const u8 *wpa_ie_pos, const u8 *wmm_ie_pos)
{
	const u8 *wps_ie, *vht_capab_ie, *tail_pos = NULL, *ext_capab_ie;
	size_t   beacon_ie_len = 0, tail_length = 0;
	u8       *country_ie;
	const u8 *beacon_tail = settings->beacon.tail;
	size_t   beacon_tail_len = settings->beacon.tail_len;

	/**
	 * Channel list of Country IE prepared by hostapd is wrong, so driver needs remove the existing country IE and prepare correct one.
	 * Hostapd adds country IE at the beginning of the tail, beacon_tail is moved to the next IE to avoid the default county IE.
	 */
	country_ie = (u8 *)cfg80211_find_ie(WLAN_EID_COUNTRY, beacon_tail, beacon_tail_len);
	if (country_ie) {
		u8 *new_country_ie = NULL;

		SLSI_DBG3(ndev_vif->sdev, SLSI_MLME, "Country IE found, length = %d", country_ie[1]);

		/* Prepare the new country IE */
		if (slsi_prepare_country_ie(ndev_vif->sdev, ndev_vif->chan->center_freq, country_ie, &new_country_ie) != 0)
			SLSI_ERR(ndev_vif->sdev, "Failed to prepare country IE");

		/* Add the new country IE */
		if (new_country_ie) {
			/* new_country_ie[1] ontains the length of IE */
			fapi_append_data(req, new_country_ie, (new_country_ie[1] + 2));

			/* Free the memory allocated for the new country IE */
			kfree(new_country_ie);

			/* Remove the default country IE from the beacon_tail */
			beacon_tail += (country_ie[1] + 2);
			beacon_tail_len -= (country_ie[1] + 2);
		}
	}

	/* Modify HT IE based on OBSS scan data */
	if (ndev_vif->ap.non_ht_bss_present) {
		u8 op_mode = 1;

		SLSI_NET_DBG1(ndev_vif->wdev.netdev, SLSI_MLME, "Modify Operating mode of BSS in HT IE\n");
		slsi_modify_ies(ndev_vif->wdev.netdev, WLAN_EID_HT_OPERATION, (u8 *)settings->beacon.tail, settings->beacon.tail_len, 4, op_mode);
		ndev_vif->ap.non_ht_bss_present = false;
	}

	/* Vendor IEs are excluded from start_req. Currently WPA IE, WMM IE, WPS IE and P2P IE need to be excluded.
	 * From hostapd, order of IEs are - WPA, WMM, WPS and P2P
	 * Of these the WMM, WPS and P2P IE are usually at the end.
	 * Note: There can be "eid_p2p_manage" and "eid_hs20" after WPS and P2P IE. Both of these are currently not supported.
	 */

	/* Exclude WMM or WPS IE */
	if (wmm_ie_pos)                 /* WMM IE is present. Remove from this position onwards, i.e. copy only till this data. WPS and P2P IE will also get removed. */
		beacon_ie_len = wmm_ie_pos - beacon_tail;
	else {
		/* WMM IE is not present. Check for WPS IE (and thereby P2P IE) and exclude it */
		wps_ie = cfg80211_find_vendor_ie(WLAN_OUI_MICROSOFT, WLAN_OUI_TYPE_MICROSOFT_WPS, beacon_tail, beacon_tail_len);
		if (wps_ie)
			beacon_ie_len = wps_ie - beacon_tail;
		else
			beacon_ie_len = beacon_tail_len;
	}

	/* Exclude WPA IE if present */
	if (wpa_ie_pos) {
		size_t len_before, len;

		len_before = wpa_ie_pos - beacon_tail;
		fapi_append_data(req, beacon_tail, len_before);

		len = len_before + ndev_vif->ap.wpa_ie_len;

		if (beacon_ie_len > len) {                 /* More IEs to go */
			tail_length = beacon_ie_len - len;
			tail_pos = (beacon_tail + len);
		} else                 /* No more IEs, don't add Ext Capab IE as no HT/VHT */
			return;
	} else {
		tail_length = beacon_ie_len;
		tail_pos = beacon_tail;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 9))
	/* Add Ext Capab IE only for VHT mode for now */
	if (ndev_vif->chandef->width == NL80211_CHAN_WIDTH_80) {
		/* Ext Capab should be before VHT IEs */
		vht_capab_ie = (cfg80211_find_ie(WLAN_EID_VHT_CAPABILITY, tail_pos, tail_length));
		ext_capab_ie = cfg80211_find_ie(WLAN_EID_EXT_CAPABILITY, tail_pos, tail_length);
		while (tail_length > 2) {
			if (tail_pos[0] == WLAN_EID_VHT_CAPABILITY)
				slsi_ap_add_ext_capab_ie(req, ndev_vif, ext_capab_ie);
			else if (tail_pos[0] != WLAN_EID_EXT_CAPABILITY && tail_pos[0] != WLAN_EID_VHT_OPERATION)
				fapi_append_data(req, tail_pos, tail_pos[1] + 2);

			tail_length -= tail_pos[1] + 2;
			tail_pos += tail_pos[1] + 2;
		}
		if (!vht_capab_ie)
			slsi_ap_add_ext_capab_ie(req, ndev_vif, ext_capab_ie);
		} else {
		fapi_append_data(req, tail_pos, tail_length);
	}
#else
	fapi_append_data(req, tail_pos, tail_length);
#endif
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 9))
/*EID + LEN + CAPABILITIES + MCS */
/* 1+1+4+8 */
#define SLSI_VHT_CAPABILITIES_IE_LEN 14

/* EID + LEN + WIDTH + SEG0 + SEG1 + MCS */
/* 1+1+1+1+1+2 */
#define SLSI_VHT_OPERATION_IE_LEN 7

static int slsi_prepare_vht_ies(struct net_device *dev, u8 **vht_ie_capab, u8 **vht_ie_operation)
{
	u32               capabs;
	u16               mcs;
	u8                *p_cap, *p_oper;
	struct netdev_vif *ndev_vif = netdev_priv(dev);

	*vht_ie_capab = kmalloc(SLSI_VHT_CAPABILITIES_IE_LEN, GFP_KERNEL);
	if (!(*vht_ie_capab))
		return -EINVAL;
	*vht_ie_operation = kmalloc(SLSI_VHT_OPERATION_IE_LEN, GFP_KERNEL);
	if (!(*vht_ie_operation)) {
		kfree(*vht_ie_capab);
		return -EINVAL;
	}

	p_cap = *vht_ie_capab;
	p_oper = *vht_ie_operation;

	*p_cap++ = WLAN_EID_VHT_CAPABILITY;
	*p_cap++ = SLSI_VHT_CAPABILITIES_IE_LEN - 1 - 1;
	capabs = cpu_to_le32(slsi_vht_cap.cap);
	memcpy(p_cap, &capabs, sizeof(capabs));
	p_cap += sizeof(capabs);
	memcpy(p_cap, &slsi_vht_cap.vht_mcs, sizeof(slsi_vht_cap.vht_mcs));

	*p_oper++ = WLAN_EID_VHT_OPERATION;
	*p_oper++ = SLSI_VHT_OPERATION_IE_LEN - 1 - 1;
	*p_oper++ = IEEE80211_VHT_CHANWIDTH_80MHZ;
	*p_oper++ = ieee80211_frequency_to_channel(ndev_vif->chandef->center_freq1);
	*p_oper++ = 0;
	mcs = cpu_to_le16(0xfffc);
	memcpy(p_oper, &mcs, sizeof(mcs));

	return 0;
}
#endif

int slsi_mlme_start(struct slsi_dev *sdev, struct net_device *dev, u8 *bssid, struct cfg80211_ap_settings *settings, const u8 *wpa_ie_pos, const u8 *wmm_ie_pos, bool append_vht_ies)
{
	struct netdev_vif      *ndev_vif = netdev_priv(dev);
	struct sk_buff         *req;
	struct sk_buff         *cfm;
	struct ieee80211_mgmt  *mgmt;
	int                    r = 0;
	u8                     *p;
	enum nl80211_auth_type auth_type = settings->auth_type;
	u16                    beacon_ie_head_len;
	u16                    chan_info;
	u16                    fw_freq;
	u16                    vht_ies_len = 0;
	u8                     ext_capab_len = 0;
	const u8			     *recv_vht_capab_ie, *recv_vht_operation_ie;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 9))
	u8                     *vht_ie_capab, *vht_ie_operation;
#endif
	SLSI_UNUSED_PARAMETER(bssid);

	if (slsi_is_test_mode_enabled()) {
		SLSI_NET_INFO(dev, "Skip sending signal, WlanLite FW does not support MLME_START.request\n");
		return -EOPNOTSUPP;
	}

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	mgmt = (struct ieee80211_mgmt *)settings->beacon.head;
	beacon_ie_head_len = settings->beacon.head_len - ((u8 *)mgmt->u.beacon.variable - (u8 *)mgmt);

	/* For port enabling, save the privacy bit used in assoc response or beacon */
	ndev_vif->ap.privacy = (mgmt->u.beacon.capab_info & WLAN_CAPABILITY_PRIVACY);
	ndev_vif->ap.qos_enabled = (mgmt->u.beacon.capab_info & WLAN_CAPABILITY_QOS);

	switch (auth_type) {
	case NL80211_AUTHTYPE_OPEN_SYSTEM:
	case NL80211_AUTHTYPE_SHARED_KEY:
		break;
	case NL80211_AUTHTYPE_AUTOMATIC:
		auth_type = NL80211_AUTHTYPE_OPEN_SYSTEM;
		if (settings->privacy && settings->crypto.cipher_group == 0)
			auth_type = NL80211_AUTHTYPE_SHARED_KEY;
		break;
	default:
		SLSI_NET_ERR(dev, "Unsupported auth_type: %d\n", auth_type);
		return -EOPNOTSUPP;
	}

	SLSI_NET_DBG1(dev, SLSI_MLME, "mlme_start_req(vif:%u, bssid:%pM, ssid:%.*s, hidden:%d)\n", ndev_vif->ifnum, bssid, (int)settings->ssid_len, settings->ssid, settings->hidden_ssid);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 9))
	if (append_vht_ies)
		vht_ies_len = SLSI_VHT_CAPABILITIES_IE_LEN + SLSI_VHT_OPERATION_IE_LEN;

	recv_vht_capab_ie = cfg80211_find_ie(WLAN_EID_VHT_CAPABILITY, settings->beacon.tail,
					     settings->beacon.tail_len);
	if (recv_vht_capab_ie)
		vht_ies_len -= (recv_vht_capab_ie[1] + 2);

	recv_vht_operation_ie = cfg80211_find_ie(WLAN_EID_VHT_OPERATION, settings->beacon.tail,
						 settings->beacon.tail_len);
	if (recv_vht_operation_ie)
		vht_ies_len -= (recv_vht_operation_ie[1] + 2);

	if (ndev_vif->chandef->width == NL80211_CHAN_WIDTH_80) {
		/* Ext Capab are not advertised by driver and so the IE would not be sent by hostapd.
		 * Frame the IE in driver and set the required bit(s).
		 */
		SLSI_NET_DBG1(dev, SLSI_MLME, "VHT - Ext Capab IE to be included\n");
		ext_capab_len = SLSI_AP_EXT_CAPAB_IE_LEN_MAX;
	}
#endif

	if (settings->hidden_ssid == 1)
		req = fapi_alloc(mlme_start_req, MLME_START_REQ, ndev_vif->ifnum, settings->ssid_len + beacon_ie_head_len + settings->beacon.tail_len + vht_ies_len + ext_capab_len);
	else
		req = fapi_alloc(mlme_start_req, MLME_START_REQ, ndev_vif->ifnum, beacon_ie_head_len + settings->beacon.tail_len + vht_ies_len + ext_capab_len);

	if (!req)
		return -ENOMEM;
	fapi_set_memcpy(req, u.mlme_start_req.bssid, dev->dev_addr);
	fapi_set_u16(req, u.mlme_start_req.beacon_period, settings->beacon_interval);
	fapi_set_u16(req, u.mlme_start_req.dtim_period, settings->dtim_period);
	fapi_set_u16(req, u.mlme_start_req.capability_information, le16_to_cpu(mgmt->u.beacon.capab_info));
	fapi_set_u16(req, u.mlme_start_req.authentication_type, auth_type);
	fapi_set_u16(req, u.mlme_start_req.hidden_ssid, settings->hidden_ssid < 3 ? settings->hidden_ssid : NL80211_HIDDEN_SSID_ZERO_LEN);

	fw_freq = ndev_vif->chan->center_freq;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 9))
	chan_info = slsi_get_chann_info(sdev, ndev_vif->chandef);
#else
	chan_info = slsi_get_chann_info(sdev, ndev_vif->channel_type);
#endif
	if ((chan_info & 20) != 20)
		fw_freq = slsi_get_center_freq1(sdev, chan_info, fw_freq);

	fapi_set_u16(req, u.mlme_start_req.channel_frequency, (2 * fw_freq));
	fapi_set_u16(req, u.mlme_start_req.channel_information, chan_info);
	ndev_vif->ap.channel_freq = fw_freq;

	/* Addition of SSID IE in mlme_start_req for hiddenSSID case */
	if (settings->hidden_ssid != 0) {
		p = fapi_append_data(req, NULL, 2 + settings->ssid_len);
		if (!p) {
			slsi_kfree_skb(req);
			return -EINVAL;
		}
		*p++ = WLAN_EID_SSID;
		*p++ = settings->ssid_len;
		memcpy(p, settings->ssid, settings->ssid_len);
	}

	if (beacon_ie_head_len && settings->hidden_ssid == 0)
		fapi_append_data(req, mgmt->u.beacon.variable, beacon_ie_head_len);
	else if (beacon_ie_head_len && settings->hidden_ssid == 1)
		fapi_append_data(req, mgmt->u.beacon.variable + 2, beacon_ie_head_len - 2);
	else if (beacon_ie_head_len && settings->hidden_ssid == 2)
		fapi_append_data(req, mgmt->u.beacon.variable + 2 + settings->ssid_len, beacon_ie_head_len - (2 + settings->ssid_len));

	if (settings->beacon.tail_len)
		slsi_mlme_start_prepare_ies(req, ndev_vif, settings, wpa_ie_pos, wmm_ie_pos);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 9))
	if ((append_vht_ies) && !slsi_prepare_vht_ies(dev, &vht_ie_capab, &vht_ie_operation)) {
		fapi_append_data(req, vht_ie_capab, SLSI_VHT_CAPABILITIES_IE_LEN);
		fapi_append_data(req, vht_ie_operation, SLSI_VHT_OPERATION_IE_LEN);
		kfree(vht_ie_capab);
		kfree(vht_ie_operation);
	}
#endif

	cfm = slsi_mlme_req_cfm(sdev, dev, req, MLME_START_CFM);
	if (!cfm)
		return -EIO;
	if (fapi_get_u16(cfm, u.mlme_start_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_NET_ERR(dev, "mlme_start_cfm(result:%u) ERROR\n", fapi_get_u16(cfm, u.mlme_start_cfm.result_code));
		r = -EINVAL;
	}

	slsi_kfree_skb(cfm);
	return r;
}

static const u8 *slsi_mlme_connect_get_sec_ie(struct cfg80211_connect_params *sme, int *sec_ie_len)
{
	u16      version;
	const u8 *ptr = NULL;

	if (sme->crypto.wpa_versions == 0) {
		/* WAPI */
		ptr = cfg80211_find_ie(SLSI_WLAN_EID_WAPI,  sme->ie, sme->ie_len);
		if (ptr) {
			version = ptr[3] << 8 | ptr[2];
			if (version != 1) {
				SLSI_ERR_NODEV("Unexpected version (%d) in WAPI ie\n", version);
				return NULL;
			}
		}
	} else if (sme->crypto.wpa_versions == 2) {
		/* RSN */
		ptr = cfg80211_find_ie(WLAN_EID_RSN, sme->ie, sme->ie_len);

		if (ptr) {
			/* version index is 2 for RSN */
			version = ptr[2 + 1] << 8 | ptr[2];
			if (version != 1) {
				SLSI_ERR_NODEV("Unexpected version (%d) in rsn ie\n", version);
				return NULL;
			}
		}
	}
	*sec_ie_len = ptr ? ptr[1] + 2 : 0;
	return ptr;
}

/* If is_copy is true copy the required IEs from connect_ie to ie_dest. else
 * calculate the required ie length
 */
static int slsi_mlme_connect_info_elems_ie_prep(struct slsi_dev *sdev, const u8 *connect_ie,
						const size_t connect_ie_len, bool is_copy, u8 *ie_dest, int ie_dest_len)
{
	const u8 *ie_pos = NULL;
	int      info_elem_length = 0;
	u16      curr_ie_len;

	if (is_copy && (!ie_dest || ie_dest_len == 0))
		return -EINVAL;

	/* find interworking ie id:107 */
	ie_pos = cfg80211_find_ie(SLSI_WLAN_EID_INTERWORKING, connect_ie, connect_ie_len);
	if (ie_pos) {
		curr_ie_len = *(ie_pos + 1) + 2;
		if (is_copy) {
			if (ie_dest_len >= curr_ie_len) {
				memcpy(ie_dest, ie_pos, curr_ie_len);
				ie_dest += curr_ie_len;
				/* free space avail in ie_dest for next ie*/
				ie_dest_len -= curr_ie_len;
			} else {
				SLSI_ERR_NODEV("interwork ie extract error (ie_copy_l:%d, c_ie_l:%d):\n", ie_dest_len, curr_ie_len);
				return -EINVAL;
			}
		} else {
			info_elem_length = curr_ie_len;
		}
	}

	/* vendor specific IEs will be the last elements. */
	ie_pos = cfg80211_find_ie(WLAN_EID_VENDOR_SPECIFIC, connect_ie, connect_ie_len);
	if (ie_pos) {
		/* length of all the vendor specific IEs */
		curr_ie_len = connect_ie_len - (ie_pos - connect_ie);
		if (is_copy) {
			if (ie_dest_len >= curr_ie_len) {
				memcpy(ie_dest, ie_pos, curr_ie_len);
				ie_dest += curr_ie_len;
				ie_dest_len -= curr_ie_len;
			} else {
				SLSI_ERR_NODEV("vendor ie extract error (ie_copy_l:%d, c_ie_l:%d):\n", ie_dest_len, curr_ie_len);
				return -EINVAL;
			}
		} else {
			info_elem_length += curr_ie_len;
		}
	}

	if (sdev->device_config.qos_info != -1) {
		if (is_copy) {
			if (ie_dest_len >= 9) {
				int pos = 0;

				ie_dest[pos++] = SLSI_WLAN_EID_VENDOR_SPECIFIC;
				ie_dest[pos++] = 0x07;
				ie_dest[pos++] = 0x00;
				ie_dest[pos++] = 0x50;
				ie_dest[pos++] = 0xf2;
				ie_dest[pos++] = WLAN_OUI_TYPE_MICROSOFT_WMM;
				ie_dest[pos++] = WMM_OUI_SUBTYPE_INFORMATION_ELEMENT;
				ie_dest[pos++] = WMM_VERSION;
				ie_dest[pos++] = sdev->device_config.qos_info & 0x0F;
				ie_dest += pos;
				ie_dest_len -= pos;
			} else {
				SLSI_ERR_NODEV("Required 9bytes but left:%d\n", ie_dest_len);
				return -EINVAL;
			}
		} else {
			info_elem_length += 9;
		}
	}
	return info_elem_length;
}

static int slsi_mlme_connect_info_elements(struct slsi_dev *sdev, struct net_device *dev, struct cfg80211_connect_params *sme)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	int               info_elem_length = 0;
	struct sk_buff    *req;
	struct sk_buff    *cfm;
	int               r = 0;
	u8                *p;

	info_elem_length = slsi_mlme_connect_info_elems_ie_prep(sdev, sme->ie, sme->ie_len, false, NULL, 0);

	/* NO IE required in MLME-ADD-INFO-ELEMENTS */
	if (info_elem_length <= 0)
		return info_elem_length;

	req = fapi_alloc(mlme_add_info_elements_req, MLME_ADD_INFO_ELEMENTS_REQ,
			 ndev_vif->ifnum, info_elem_length);
	if (!req)
		return -ENOMEM;

	fapi_set_u16(req, u.mlme_add_info_elements_req.purpose, FAPI_PURPOSE_ASSOCIATION_REQUEST);

	p = fapi_append_data(req, NULL, info_elem_length);
	if (!p) {
		slsi_kfree_skb(req);
		return -EINVAL;
	}

	(void)slsi_mlme_connect_info_elems_ie_prep(sdev, sme->ie, sme->ie_len, true, p, info_elem_length);

	/* backup ies */
	if (SLSI_IS_VIF_INDEX_WLAN(ndev_vif)) {
		if (ndev_vif->sta.assoc_req_add_info_elem_len)
			kfree(ndev_vif->sta.assoc_req_add_info_elem);
		ndev_vif->sta.assoc_req_add_info_elem_len = 0;

		ndev_vif->sta.assoc_req_add_info_elem = kmalloc(info_elem_length, GFP_KERNEL);
		if (ndev_vif->sta.assoc_req_add_info_elem) {
			memcpy(ndev_vif->sta.assoc_req_add_info_elem, p, info_elem_length);
			ndev_vif->sta.assoc_req_add_info_elem_len = info_elem_length;
		} else {
			SLSI_WARN(sdev, "No mem for ndev_vif->sta.assoc_req_add_info_elem size %d\n", info_elem_length);
		}
	}

	SLSI_NET_DBG2(dev, SLSI_MLME, "mlme_add_info_elements_req(vif:%u)\n", ndev_vif->ifnum);
	cfm = slsi_mlme_req_cfm(sdev, dev, req, MLME_ADD_INFO_ELEMENTS_CFM);
	if (!cfm)
		return -EIO;

	if (fapi_get_u16(cfm, u.mlme_add_info_elements_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_NET_ERR(dev, "mlme_add_info_elements_cfm(result:%u) ERROR\n", fapi_get_u16(cfm, u.mlme_connect_cfm.result_code));
		r = -EINVAL;
	}

	if (cfg80211_find_vendor_ie(WLAN_OUI_MICROSOFT, WLAN_OUI_TYPE_MICROSOFT_WPS, sme->ie, sme->ie_len))
		ndev_vif->sta.is_wps = true;

	slsi_kfree_skb(cfm);
	return r;
}

int slsi_mlme_connect(struct slsi_dev *sdev, struct net_device *dev, struct cfg80211_connect_params *sme, struct ieee80211_channel *channel, const u8 *bssid)
{
	struct netdev_vif      *ndev_vif = netdev_priv(dev);
	struct sk_buff         *req;
	struct sk_buff         *cfm;
	int                    r = 0;
	u8                     *p;
	enum nl80211_auth_type auth_type = sme->auth_type;
	u8                     mac_addr[ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	struct key_params      slsi_key;
	const u8               *sec_ie = NULL;
	int                    sec_ie_len = 0;

	memset(&slsi_key, 0, sizeof(slsi_key));

	if (slsi_is_test_mode_enabled()) {
		SLSI_NET_INFO(dev, "Skip sending signal, WlanLite FW does not support MLME_CONNECT.request\n");
		return -EOPNOTSUPP;
	}

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	if (WARN(!bssid, "BSSID is Null"))
		return -EINVAL;

	if (WARN(!sme->ssid_len, "SSID is Null"))
		return -EINVAL;

	switch (auth_type) {
	case NL80211_AUTHTYPE_OPEN_SYSTEM:
	case NL80211_AUTHTYPE_SHARED_KEY:
		break;
	case NL80211_AUTHTYPE_AUTOMATIC:
		/* In case of WEP, need to try both open and shared.
		 * FW does this if auth is shared_key. So set it to shared.
		 */
		if (sme->privacy &&
		    (sme->crypto.cipher_group == WLAN_CIPHER_SUITE_WEP40 ||
		     sme->crypto.cipher_group == WLAN_CIPHER_SUITE_WEP104))
			auth_type = NL80211_AUTHTYPE_SHARED_KEY;
		else
			auth_type = NL80211_AUTHTYPE_OPEN_SYSTEM;
		break;
	default:
		SLSI_NET_ERR(dev, "Unsupported auth_type: %d\n", auth_type);
		return -EOPNOTSUPP;
	}

	/* We save the WEP key for shared authentication. */
	if ((auth_type == NL80211_AUTHTYPE_SHARED_KEY) &&
	    ((sme->crypto.cipher_group == WLAN_CIPHER_SUITE_WEP40) ||
	     (sme->crypto.cipher_group == WLAN_CIPHER_SUITE_WEP104)) &&
	    (ndev_vif->vif_type == FAPI_VIFTYPE_STATION)) {
		SLSI_NET_DBG3(dev, SLSI_MLME, "key len (%d)\n", sme->key_len);
		slsi_key.key = (u8 *)sme->key;
		if (!slsi_key.key)
			return -EINVAL;
		slsi_key.key_len = sme->key_len;
		slsi_key.seq_len = 0;
		if (sme->crypto.n_ciphers_pairwise)
			slsi_key.cipher = sme->crypto.ciphers_pairwise[0];

		r = slsi_mlme_set_key(sdev, dev, sme->key_idx, FAPI_KEYTYPE_WEP, mac_addr, &slsi_key);
		if (r != 0) {
			SLSI_NET_ERR(dev, "Error Setting Shared key (%d)", r);
			return r;
		}
	}

	/*Do not check sme->ie as wpa_supplicant sends some invalid value in it even if ie_len is zero .*/
	if (sme->ie_len) {
		r = slsi_mlme_connect_info_elements(sdev, dev, sme);
		if (r)
			return r;

		sec_ie = slsi_mlme_connect_get_sec_ie(sme, &sec_ie_len);
		if (sec_ie_len < 0) {
			SLSI_NET_ERR(dev, "ERROR preparing Security IEs\n");
			return sec_ie_len;
		}
	}

	SLSI_NET_DBG1(dev, SLSI_MLME, "mlme_connect_req(vif:%u, bssid:%pM, ssid:%.*s)\n", ndev_vif->ifnum, bssid, (int)sme->ssid_len, sme->ssid);
	req = fapi_alloc(mlme_connect_req, MLME_CONNECT_REQ, ndev_vif->ifnum,
			 2 + sme->ssid_len +   /*SSID IE*/
			 sec_ie_len);          /*WPA/WPA2/WAPI/OSEN*/
	if (!req)
		return -ENOMEM;

	fapi_set_memcpy(req, u.mlme_connect_req.bssid, bssid);
	fapi_set_u16(req, u.mlme_connect_req.authentication_type, auth_type);
	/* Need to double the freq for the firmware */
	fapi_set_u16(req, u.mlme_connect_req.channel_frequency, (2 * channel->center_freq));

	p = fapi_append_data(req, NULL, 2 + sme->ssid_len + sec_ie_len);
	if (!p) {
		slsi_kfree_skb(req);
		return -EINVAL;
	}
	*p++ = WLAN_EID_SSID;
	*p++ = sme->ssid_len;
	memcpy(p, sme->ssid, sme->ssid_len);
	p += sme->ssid_len;

	if (sec_ie_len)
		memcpy(p, sec_ie, sec_ie_len);
	cfm = slsi_mlme_req_cfm(sdev, dev, req, MLME_CONNECT_CFM);
	if (!cfm)
		return -EIO;

	if (fapi_get_u16(cfm, u.mlme_connect_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_NET_ERR(dev, "mlme_connect_cfm(result:%u) ERROR\n", fapi_get_u16(cfm, u.mlme_connect_cfm.result_code));
		r = -EINVAL;
	}

	slsi_kfree_skb(cfm);
	return r;
}

void slsi_mlme_connect_resp(struct slsi_dev *sdev, struct net_device *dev)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *req;
	struct sk_buff    *cfm;

	if (slsi_is_test_mode_enabled()) {
		SLSI_NET_INFO(dev, "Skip sending signal, WlanLite FW does not support MLME_CONNECT_RESP\n");
		return;
	}

	SLSI_NET_DBG1(dev, SLSI_MLME, "mlme_connect_resp(vif:%u)\n", ndev_vif->ifnum);
	req = fapi_alloc(mlme_connect_res, MLME_CONNECT_RES, ndev_vif->ifnum, 0);
	if (!req)
		return;

	cfm = slsi_mlme_req_no_cfm(sdev, dev, req);
	WARN_ON(cfm);
}

void slsi_mlme_connected_resp(struct slsi_dev *sdev, struct net_device *dev, u16 peer_index)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *req;

	if (slsi_is_test_mode_enabled()) {
		SLSI_NET_INFO(dev, "Skip sending signal, WlanLite FW does not support MLME_CONNECT_RESP\n");
		return;
	}

	SLSI_NET_DBG1(dev, SLSI_MLME, "mlme_connected_resp(vif:%u, peer_index:%d)\n", ndev_vif->ifnum, peer_index);
	req = fapi_alloc(mlme_connected_res, MLME_CONNECTED_RES, ndev_vif->ifnum, 0);
	if (!req) {
		SLSI_NET_ERR(dev, "mlme-connected-response :: memory allocation failed\n");
		return;
	}

	fapi_set_u16(req, u.mlme_connected_res.peer_index, peer_index);
	slsi_mlme_req_no_cfm(sdev, dev, req);
}

void slsi_mlme_roamed_resp(struct slsi_dev *sdev, struct net_device *dev)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *req;
	struct sk_buff    *cfm;

	if (slsi_is_test_mode_enabled()) {
		SLSI_NET_INFO(dev, "Skip sending signal, WlanLite FW does not support MLME_ROAMED_RESP\n");
		return;
	}

	SLSI_NET_DBG1(dev, SLSI_MLME, "mlme_roamed_resp\n");
	req = fapi_alloc(mlme_roamed_res, MLME_ROAMED_RES, ndev_vif->ifnum, 0);
	if (!req)
		return;

	cfm = slsi_mlme_req_no_cfm(sdev, dev, req);
	WARN_ON(cfm);
}

/* Null check for cfm done in caller function */
bool slsi_disconnect_cfm_validate(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *cfm)
{
	int  result = fapi_get_u16(cfm, u.mlme_disconnect_cfm.result_code);
	bool r = false;

	SLSI_UNUSED_PARAMETER(sdev);

	if (WARN_ON(!dev))
		goto exit;

	if (result == FAPI_RESULTCODE_SUCCESS)
		r = true;
	/* Not present code would mean peer is already disconnected and hence no ind (could be race scenario), don't log as error */
	else if (result != FAPI_RESULTCODE_NOT_PRESENT)
		SLSI_NET_ERR(dev, "mlme_disconnect_cfm(result:%u) ERROR\n", fapi_get_u16(cfm, u.mlme_disconnect_cfm.result_code));

exit:
	slsi_kfree_skb(cfm);
	return r;
}

int slsi_mlme_disconnect(struct slsi_dev *sdev, struct net_device *dev, u8 *mac, u16 reason_code, bool wait_ind)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *req;
	struct sk_buff    *rx;
	int               r = 0;

	if (slsi_is_test_mode_enabled()) {
		SLSI_NET_INFO(dev, "Skip sending signal, WlanLite FW does not support MLME_DISCONNECT.request\n");
		return -EOPNOTSUPP;
	}

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	SLSI_NET_DBG1(dev, SLSI_MLME, "mlme_disconnect_req(vif:%u, bssid:%pM, reason:%d)\n", ndev_vif->ifnum, mac, reason_code);

#ifdef CONFIG_SCSC_LOG_COLLECTION
	scsc_log_collector_schedule_collection(SCSC_LOG_HOST_WLAN, SCSC_LOG_HOST_WLAN_REASON_DISCONNECT);
#else
	mx140_log_dump();
#endif
	/* No data reference required */
	req = fapi_alloc(mlme_disconnect_req, MLME_DISCONNECT_REQ, ndev_vif->ifnum, 0);
	if (!req)
		return -ENOMEM;
	SLSI_INFO(sdev, "Send DEAUTH, reason = %d\n", reason_code);
	fapi_set_u16(req, u.mlme_disconnect_req.reason_code, reason_code);
	if (mac)
		fapi_set_memcpy(req, u.mlme_disconnect_req.peer_sta_address, mac);
	else
		fapi_set_memset(req, u.mlme_disconnect_req.peer_sta_address, 0);
	if (wait_ind) {
		rx = slsi_mlme_req_cfm_ind(sdev, dev, req, MLME_DISCONNECT_CFM, MLME_DISCONNECT_IND, slsi_disconnect_cfm_validate);
		if (!rx) {
			SLSI_NET_ERR(dev, "mlme_disconnect_cfm() ERROR\n");
			r = -EINVAL;
		}
	} else {
		rx = slsi_mlme_req_cfm(sdev, dev, req, MLME_DISCONNECT_CFM);
		if (rx) {
			if (fapi_get_u16(rx, u.mlme_disconnect_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
				SLSI_NET_ERR(dev, "mlme_disconnect_cfm(result:%u) ERROR\n", fapi_get_u16(rx, u.mlme_disconnect_cfm.result_code));
				r = -EINVAL;
			}
		} else {
			r = -EIO;
		}
	}

	slsi_kfree_skb(rx);
	return r;
}

int slsi_mlme_set_key(struct slsi_dev *sdev, struct net_device *dev, u16 key_id, u16 key_type, const u8 *address, struct key_params *key)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *req;
	struct sk_buff    *cfm;
	int               r = 0;

	if (slsi_is_test_mode_enabled()) {
		SLSI_NET_INFO(dev, "Skip sending signal, WlanLite FW does not support MLME_SETKEYS.request\n");
		return -EOPNOTSUPP;
	}

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	SLSI_NET_DBG1(dev, SLSI_MLME, "mlme_setkeys_req(key_id:%d, key_type:%d, address:%pM, length:%d, cipher:0x%.8X)\n", key_id, key_type, address, key->key_len, key->cipher);
	req = fapi_alloc(mlme_setkeys_req, MLME_SETKEYS_REQ, ndev_vif->ifnum, key->key_len + 1);         /* + 1 for the wep key index */
	if (!req)
		return -ENOMEM;
	fapi_set_u16(req, u.mlme_setkeys_req.length, key->key_len * 8);
	fapi_set_u16(req, u.mlme_setkeys_req.key_id, key_id);
	fapi_set_u16(req, u.mlme_setkeys_req.key_type, key_type);
	fapi_set_memcpy(req, u.mlme_setkeys_req.address, address);
	fapi_set_memset(req, u.mlme_setkeys_req.sequence_number, 0x00);

	if (key->seq_len && key->seq) {
		int i;
		u16 temp_seq;

		SLSI_NET_DBG3(dev, SLSI_MLME, "mlme_setkeys_req(key->seq_len:%d)\n", key->seq_len);

		/* Sequence would be in little endian format
		 * If sequence is say key->seq is
		 * 04 03 02 01 00 00 00 00, it would be encoded as :
		 * 0x0304 0x0102 0x0000 0x0000 for firmware
		 */
		for (i = 0; i < key->seq_len; i += 2) {
			temp_seq = (u16)(key->seq[i + 1] << 8) | (u16)(key->seq[i]);
			fapi_set_u16(req, u.mlme_setkeys_req.sequence_number[i / 2], temp_seq);
		}
	}

	fapi_set_u32(req, u.mlme_setkeys_req.cipher_suite_selector, key->cipher);

	if (key->cipher == WLAN_CIPHER_SUITE_WEP40 || key->cipher == WLAN_CIPHER_SUITE_WEP104) {
		u8 wep_key_id = (u8)key_id;

		if (key_id > 3)
			SLSI_NET_WARN(dev, "Key ID is greater than 3");
		/* Incase of WEP key index is appended before key.
		 * So increment length by one
		 */
		fapi_set_u16(req, u.mlme_setkeys_req.length, (key->key_len + 1) * 8);
		fapi_append_data(req, &wep_key_id, 1);
	}
	fapi_append_data(req, key->key, key->key_len);

	cfm = slsi_mlme_req_cfm(sdev, dev, req, MLME_SETKEYS_CFM);
	if (!cfm)
		return -EIO;

	if (fapi_get_u16(cfm, u.mlme_setkeys_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_NET_ERR(dev, "mlme_setkeys_cfm(result:%u) ERROR\n", fapi_get_u16(cfm, u.mlme_setkeys_cfm.result_code));
		r = -EINVAL;
	}
	slsi_kfree_skb(cfm);
	return r;
}

int slsi_mlme_get_key(struct slsi_dev *sdev, struct net_device *dev, u16 key_id, u16 key_type, u8 *seq, int *seq_len)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *req;
	struct sk_buff    *cfm;
	int               r = 0;

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	SLSI_NET_DBG2(dev, SLSI_MLME, "mlme_get_key_sequence_req(key_id:%d, key_type:%d)\n", key_id, key_type);
	req = fapi_alloc(mlme_get_key_sequence_req, MLME_GET_KEY_SEQUENCE_REQ, ndev_vif->ifnum, 0);
	if (!req)
		return -ENOMEM;
	fapi_set_u16(req, u.mlme_get_key_sequence_req.key_id, key_id);
	fapi_set_u16(req, u.mlme_get_key_sequence_req.key_type, key_type);

	cfm = slsi_mlme_req_cfm(sdev, dev, req, MLME_GET_KEY_SEQUENCE_CFM);
	if (!cfm)
		return -EIO;

	if (fapi_get_u16(cfm, u.mlme_get_key_sequence_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_NET_ERR(dev, "mlme_get_key_sequence_cfm(result:%u) ERROR\n", fapi_get_u16(cfm, u.mlme_get_key_sequence_cfm.result_code));
		r = -ENOENT;
	} else {
		int i;
		u16 temp_seq;

		/* For WPA2 Key RSC - 8 octets. For WPAI, it would be 16 octets (code would need to be updated)
		 * Length is not available in cfm but even if max length 8 is assigned, it should be ok as other octets
		 * would be padded with 0s
		 */
		*seq_len = 8;

		/* Sequence from firmware is of a[8] type u16 (16 octets) and only 8 octets are required for WPA/WPA2.
		 * If sequence is say 0x01 0x02 0x03 0x04 with 0x01 as MSB and 0x04 as LSB then
		 * it would be encoded as: 0x0304 0x0102 by firmware.
		 * Sequence is expected to be returned in little endian
		 */

		for (i = 0; i < *seq_len / 2; i++) {
			temp_seq = fapi_get_u16(cfm, u.mlme_get_key_sequence_cfm.sequence_number[i]);
			*seq = (u8)(temp_seq & 0xFF);
			*(seq + 1) = (u8)((temp_seq >> 8) & 0xFF);

			seq += 2;
		}
	}

	slsi_kfree_skb(cfm);
	return r;
}

void slsi_fw_tx_rate_calc(u16 fw_rate, struct rate_info *tx_rate, unsigned long *data_rate_mbps)
{
	const int fw_rate_idx_to_80211_rate[] = { 0, 10, 20, 55, 60, 90, 110, 120, 180, 240, 360, 480, 540 };

	if (tx_rate) {
		tx_rate->flags = 0;
		tx_rate->legacy = 0;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0))
		tx_rate->bw = 0;
#endif
	}
	if ((fw_rate & SLSI_FW_API_RATE_HT_SELECTOR_FIELD) == SLSI_FW_API_RATE_NON_HT_SELECTED) {
		u16 fw_rate_idx = fw_rate & SLSI_FW_API_RATE_INDEX_FIELD;

		if (fw_rate > 0 && fw_rate_idx < ARRAY_SIZE(fw_rate_idx_to_80211_rate)) {
			if (tx_rate)
				tx_rate->legacy = fw_rate_idx_to_80211_rate[fw_rate_idx];
			if (data_rate_mbps)
				*data_rate_mbps = fw_rate_idx_to_80211_rate[fw_rate_idx] / 10;
		}

	} else if ((fw_rate & SLSI_FW_API_RATE_HT_SELECTOR_FIELD) == SLSI_FW_API_RATE_HT_SELECTED) {
		u8 mcs = SLSI_FW_API_RATE_HT_MCS_FIELD & fw_rate;
		u8 nss = ((SLSI_FW_API_RATE_HT_NSS_FIELD & fw_rate) >> 6) + 1;

		if (tx_rate) {
			tx_rate->flags |= RATE_INFO_FLAGS_MCS;
			tx_rate->mcs = mcs;

			if ((fw_rate & SLSI_FW_API_RATE_BW_FIELD) == SLSI_FW_API_RATE_BW_40MHZ)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0))
				tx_rate->bw |= RATE_INFO_BW_40;
#else
				tx_rate->flags |= RATE_INFO_FLAGS_40_MHZ_WIDTH;
#endif
			if (fw_rate & SLSI_FW_API_RATE_SGI)
				tx_rate->flags |= RATE_INFO_FLAGS_SHORT_GI;
		}

		if (data_rate_mbps) {
			int chan_bw_idx;
			int gi_idx;
			int mcs_idx;

			chan_bw_idx = (fw_rate & SLSI_FW_API_RATE_BW_FIELD) >> 9;
			gi_idx = ((fw_rate & SLSI_FW_API_RATE_SGI) == SLSI_FW_API_RATE_SGI) ? 1 : 0;
			mcs_idx = SLSI_FW_API_RATE_HT_MCS_FIELD & fw_rate;

			if ((chan_bw_idx < 2) && (mcs_idx <= 7)) {
				*data_rate_mbps = (unsigned long)(nss * slsi_rates_table[chan_bw_idx][gi_idx][mcs_idx]) / 10;
			} else if (mcs == 32 && chan_bw_idx == 1) {
				if (gi_idx == 1)
					*data_rate_mbps = (unsigned long)(nss * 67)/10;
				else
					*data_rate_mbps = nss * 6;
			} else {
				SLSI_WARN_NODEV("FW DATA RATE decode error fw_rate:%x, bw:%x, mcs_idx:%x, nss : %d\n",
						fw_rate, chan_bw_idx, mcs_idx, nss);
			}
		}

	} else if ((fw_rate & SLSI_FW_API_RATE_HT_SELECTOR_FIELD) == SLSI_FW_API_RATE_VHT_SELECTED) {
		int chan_bw_idx;
		int gi_idx;
		int mcs_idx;
		u8 nss;

		/* report vht rate in legacy units and not as mcs index. reason: upper layers may still be not
		 * updated with vht msc table.
		 */
		chan_bw_idx = (fw_rate & SLSI_FW_API_RATE_BW_FIELD) >> 9;
		gi_idx = ((fw_rate & SLSI_FW_API_RATE_SGI) == SLSI_FW_API_RATE_SGI) ? 1 : 0;
		/* Calculate  NSS --> bits 6 to 4*/
		nss = ((SLSI_FW_API_RATE_VHT_NSS_FIELD & fw_rate) >> 4) + 1;
		mcs_idx = SLSI_FW_API_RATE_VHT_MCS_FIELD & fw_rate;
		/* Bandwidth (BW): 0x0= 20 MHz, 0x1= 40 MHz, 0x2= 80 MHz, 0x3= 160/ 80+80 MHz. 0x3 is not supported */
		if ((chan_bw_idx <= 2) && (mcs_idx <= 9)) {
			if (tx_rate)
				tx_rate->legacy = nss * slsi_rates_table[chan_bw_idx][gi_idx][mcs_idx];
			if (data_rate_mbps)
				*data_rate_mbps = (unsigned long)(nss * slsi_rates_table[chan_bw_idx][gi_idx][mcs_idx]) / 10;
		} else {
			SLSI_WARN_NODEV("FW DATA RATE decode error fw_rate:%x, bw:%x, mcs_idx:%x,nss : %d\n", fw_rate, chan_bw_idx, mcs_idx, nss);
		}
	}
}

int slsi_mlme_get_sinfo_mib(struct slsi_dev *sdev, struct net_device *dev,
			    struct slsi_peer *peer)
{
	struct netdev_vif                      *ndev_vif = netdev_priv(dev);
	struct slsi_mib_data                   mibreq = { 0, NULL };
	struct slsi_mib_data                   mibrsp = { 0, NULL };
	struct slsi_mib_value                  *values = NULL;
	int                                    data_length = 0;
	int                                    r = 0;
	static const struct slsi_mib_get_entry get_values[] = {
		{ SLSI_PSID_UNIFI_TX_DATA_RATE, { 0, 0 } },         /* to get STATION_INFO_TX_BITRATE*/
		{ SLSI_PSID_UNIFI_RSSI, { 0, 0 } },                 /* to get STATION_INFO_SIGNAL_AVG*/
		{ SLSI_PSID_UNIFI_THROUGHPUT_DEBUG, { 2, 0 } },
		{ SLSI_PSID_UNIFI_THROUGHPUT_DEBUG, { 3, 0 } },
		{ SLSI_PSID_UNIFI_THROUGHPUT_DEBUG, { 4, 0 } },
		{ SLSI_PSID_UNIFI_THROUGHPUT_DEBUG, { 24, 0 } },
		{ SLSI_PSID_UNIFI_THROUGHPUT_DEBUG, { 29, 0 } },
	};
	int tx_counter = 0;
	int rx_counter = 0;

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	if (!peer) {
		SLSI_WARN(sdev, "Peer Not available\n");
		return -EINVAL;
	}

	/*check if function is called within given period*/
	if (__ratelimit(&peer->sinfo_mib_get_rs))
		return 0;

	r = slsi_mib_encode_get_list(&mibreq, (sizeof(get_values) / sizeof(struct slsi_mib_get_entry)),
				     get_values);
	if (r != SLSI_MIB_STATUS_SUCCESS)
		return -ENOMEM;

	/* Fixed fields len (5) : 2 bytes(PSID) + 2 bytes (Len) + 1 byte (VLDATA header )  [10 for 2 PSIDs]
	 * Data : 3 bytes for SLSI_PSID_UNIFI_TX_DATA_RATE , 1 byte for SLSI_PSID_UNIFI_RSSI
	 * 10*5 bytes for 5 Throughput Mib's
	 */
	mibrsp.dataLength = 64;
	mibrsp.data = kmalloc(mibrsp.dataLength, GFP_KERNEL);

	if (!mibrsp.data) {
		SLSI_NET_DBG1(dev, SLSI_MLME, "failed to allocate memory\n");
		kfree(mibreq.data);
		return -ENOMEM;
	}

	r = slsi_mlme_get(sdev, dev, mibreq.data, mibreq.dataLength, mibrsp.data,
			  mibrsp.dataLength, &data_length);
	kfree(mibreq.data);

	if (r == 0) {
		mibrsp.dataLength = (u32)data_length;
		values = slsi_mib_decode_get_list(&mibrsp,
						  (sizeof(get_values) / sizeof(struct slsi_mib_get_entry)), get_values);
		if (!values) {
			SLSI_NET_DBG1(dev, SLSI_MLME, "mib decode list failed\n");
			kfree(mibrsp.data);
			return -ENOMEM;
		}

		if (values[0].type != SLSI_MIB_TYPE_NONE) {
			SLSI_CHECK_TYPE(sdev, values[0].type, SLSI_MIB_TYPE_UINT);
			slsi_fw_tx_rate_calc((u16)values[0].u.uintValue, &peer->sinfo.txrate, NULL);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0))
			peer->sinfo.filled |= BIT(NL80211_STA_INFO_TX_BITRATE);
#else
			peer->sinfo.filled |= STATION_INFO_TX_BITRATE;
#endif
			SLSI_DBG3(sdev, SLSI_MLME, "SLSI_PSID_UNIFI_TX_DATA_RATE = %d\n",
				  values[0].u.uintValue);
		}

		if (values[1].type != SLSI_MIB_TYPE_NONE) {
			SLSI_CHECK_TYPE(sdev, values[1].type, SLSI_MIB_TYPE_INT);
			if (values[1].u.intValue >= 0)
				peer->sinfo.signal = -1;
			else
				peer->sinfo.signal = (s8)values[1].u.intValue;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0))
			peer->sinfo.filled |= BIT(NL80211_STA_INFO_SIGNAL);
#else
			peer->sinfo.filled |= STATION_INFO_SIGNAL;
#endif
			SLSI_DBG3(sdev, SLSI_MLME, "SLSI_PSID_UNIFI_RSSI = %d\n",
				  values[1].u.intValue);
		}

		if (values[2].type == SLSI_MIB_TYPE_UINT)
			rx_counter += values[2].u.uintValue; /*bad_fcs_count*/
		else
			SLSI_ERR(sdev, "invalid type. iter:%d", 2);
		if (values[3].type == SLSI_MIB_TYPE_UINT)
			tx_counter += values[3].u.uintValue; /*missed_ba_count*/
		else
			SLSI_ERR(sdev, "invalid type. iter:%d", 3);
		if (values[4].type == SLSI_MIB_TYPE_UINT)
			tx_counter += values[4].u.uintValue; /*missed_ack_count*/
		else
			SLSI_ERR(sdev, "invalid type. iter:%d", 4);
		if (values[5].type == SLSI_MIB_TYPE_UINT)
			rx_counter += values[5].u.uintValue; /*mac_bad_sig_count*/
		else
			SLSI_ERR(sdev, "invalid type. iter:%d", 5);
		if (values[6].type == SLSI_MIB_TYPE_UINT)
			rx_counter += values[6].u.uintValue; /*rx_error_count*/
		else
			SLSI_ERR(sdev, "invalid type. iter:%d", 6);

		peer->sinfo.tx_failed = tx_counter;
		peer->sinfo.rx_dropped_misc = rx_counter;
	} else {
		SLSI_NET_DBG1(dev, SLSI_MLME, "mlme_get_req failed(result:%u)\n", r);
	}

	kfree(mibrsp.data);
	kfree(values);
	return r;
}

int slsi_mlme_connect_scan(struct slsi_dev *sdev, struct net_device *dev,
			   u32 n_ssids, struct cfg80211_ssid *ssids, struct ieee80211_channel *channel)
{
	struct netdev_vif                  *ndev_vif = netdev_priv(dev);
	int                                r = 0;
	struct ieee80211_channel           **scan_channels = NULL;
	struct ieee80211_channel           **add_scan_channels;
	int                                n_channels = 0;
	struct sk_buff                     *scan;

	SLSI_MUTEX_LOCK(ndev_vif->scan_mutex);

	if (ndev_vif->scan[SLSI_SCAN_HW_ID].scan_req) {
		SLSI_NET_DBG3(dev, SLSI_MLME, "stop on-going Scan\n");
		(void)slsi_mlme_del_scan(sdev, dev, ndev_vif->ifnum << 8 | SLSI_SCAN_HW_ID, false);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0))
		cfg80211_scan_done(ndev_vif->scan[SLSI_SCAN_HW_ID].scan_req, &info);
#else
		cfg80211_scan_done(ndev_vif->scan[SLSI_SCAN_HW_ID].scan_req, true);
#endif

		ndev_vif->scan[SLSI_SCAN_HW_ID].scan_req = NULL;
	}

	if (!channel) {
		enum ieee80211_band band;
		struct wiphy        *wiphy = sdev->wiphy;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0))
		for (band = 0; band < NUM_NL80211_BANDS; band++) {
#else
		for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
#endif
			if (!wiphy->bands[band])
				continue;
			n_channels += wiphy->bands[band]->n_channels;
		}

		WARN_ON(n_channels == 0);
		scan_channels = kmalloc_array((size_t)n_channels, sizeof(*scan_channels), GFP_KERNEL);
		if (!scan_channels) {
			SLSI_MUTEX_UNLOCK(ndev_vif->scan_mutex);
			return -ENOMEM;
		}
		n_channels = 0;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0))
		for (band = 0; band < NUM_NL80211_BANDS; band++) {
#else
		for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
#endif
			int j;

			if (!wiphy->bands[band])
				continue;
			for (j = 0; j < wiphy->bands[band]->n_channels; j++)
				if (!(wiphy->bands[band]->channels[j].flags & IEEE80211_CHAN_DISABLED)) {
					scan_channels[n_channels] = &wiphy->bands[band]->channels[j];
					n_channels++;
				}
		}
		add_scan_channels = scan_channels;
	} else {
		n_channels = 1;
		add_scan_channels = &channel;
	}
	ndev_vif->scan[SLSI_SCAN_HW_ID].is_blocking_scan = true;
	r = slsi_mlme_add_scan(sdev,
			       dev,
			       FAPI_SCANTYPE_FULL_SCAN,
			       FAPI_REPORTMODE_REAL_TIME,
			       n_ssids,
			       ssids,
			       n_channels,
			       add_scan_channels,
			       NULL,
			       ndev_vif->probe_req_ies,
			       ndev_vif->probe_req_ie_len,
			       ndev_vif->scan[SLSI_SCAN_HW_ID].is_blocking_scan);

	SLSI_MUTEX_LOCK(ndev_vif->scan_result_mutex);
	scan = slsi_dequeue_cached_scan_result(&ndev_vif->scan[SLSI_SCAN_HW_ID], NULL);
	while (scan) {
		slsi_rx_scan_pass_to_cfg80211(sdev, dev, scan);
		scan = slsi_dequeue_cached_scan_result(&ndev_vif->scan[SLSI_SCAN_HW_ID], NULL);
	}
	SLSI_MUTEX_UNLOCK(ndev_vif->scan_result_mutex);

	kfree(scan_channels);
	ndev_vif->scan[SLSI_SCAN_HW_ID].is_blocking_scan = false;

	SLSI_MUTEX_UNLOCK(ndev_vif->scan_mutex);
	return r;
}

/**
 * The powermgt_lock mutex is to ensure atomic update of the power management state.
 */
DEFINE_MUTEX(powermgt_lock);
/**
 * The slsi_mlme_powermgt_unlocked() must be called from a context that is synchronised
 * with ndev_vif. if called without the ndev_vif mutex already taken, other mechanisms
 * must ensure that ndev_vif will exist for the duration of the function.
 */
int slsi_mlme_powermgt_unlocked(struct slsi_dev *sdev, struct net_device *dev, u16 power_mode)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *req;
	struct sk_buff    *rx;
	int               r = 0;

	mutex_lock(&powermgt_lock);

	if (WARN_ON(!ndev_vif->activated)) {
		mutex_unlock(&powermgt_lock);
		return -EINVAL;
	}

	if (ndev_vif->power_mode == power_mode) {
		mutex_unlock(&powermgt_lock);
		SLSI_NET_DBG3(dev, SLSI_MLME, "power management mode is same as requested. No changes done\n");
		return 0;
	}

	SLSI_NET_DBG2(dev, SLSI_MLME, "mlme_powermgt_req(vif:%d, power_management_mode:%d)\n", ndev_vif->ifnum, power_mode);
	req = fapi_alloc(mlme_powermgt_req, MLME_POWERMGT_REQ, ndev_vif->ifnum, 0);
	if (!req) {
		mutex_unlock(&powermgt_lock);
		return -ENOMEM;
	}
	fapi_set_u16(req, u.mlme_powermgt_req.power_management_mode, power_mode);

	rx = slsi_mlme_req_cfm(sdev, dev, req, MLME_POWERMGT_CFM);
	if (!rx) {
		mutex_unlock(&powermgt_lock);
		return -EIO;
	}

	if (fapi_get_u16(rx, u.mlme_powermgt_cfm.result_code) == FAPI_RESULTCODE_SUCCESS) {
		ndev_vif->power_mode = power_mode;
	} else {
		SLSI_NET_ERR(dev, "mlme_powermgt_cfm(result:%u) ERROR\n", fapi_get_u16(rx, u.mlme_powermgt_cfm.result_code));
		r = -EINVAL;
	}

	slsi_kfree_skb(rx);
	mutex_unlock(&powermgt_lock);
	return r;
}

int slsi_mlme_powermgt(struct slsi_dev *sdev, struct net_device *dev, u16 power_mode)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	return slsi_mlme_powermgt_unlocked(sdev, dev, power_mode);
}

int slsi_mlme_register_action_frame(struct slsi_dev *sdev, struct net_device *dev, u32 af_bitmap_active, u32 af_bitmap_suspended)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *req;
	struct sk_buff    *cfm;
	int               r = 0;

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	req = fapi_alloc(mlme_register_action_frame_req, MLME_REGISTER_ACTION_FRAME_REQ, ndev_vif->ifnum, 0);
	if (!req)
		return -ENOMEM;

	fapi_set_u32(req, u.mlme_register_action_frame_req.action_frame_category_bitmap_active, af_bitmap_active);
	fapi_set_u32(req, u.mlme_register_action_frame_req.action_frame_category_bitmap_suspended, af_bitmap_suspended);

	SLSI_NET_DBG2(dev, SLSI_MLME, "mlme_register_action_frame(vif:%d, active:%d, suspended:%d)\n", ndev_vif->ifnum, af_bitmap_active, af_bitmap_suspended);
	cfm = slsi_mlme_req_cfm(sdev, dev, req, MLME_REGISTER_ACTION_FRAME_CFM);
	if (!cfm)
		return -EIO;

	if (fapi_get_u16(cfm, u.mlme_register_action_frame_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_NET_ERR(dev, "mlme_register_action_frame_cfm(result:%u) ERROR\n", fapi_get_u16(cfm, u.mlme_register_action_frame_cfm.result_code));
		r = -EINVAL;
	}

	slsi_kfree_skb(cfm);
	return r;
}

int slsi_mlme_channel_switch(struct slsi_dev *sdev, struct net_device *dev,  u16 center_freq, u16 chan_info)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *req;
	struct sk_buff    *cfm;
	int               r = 0;

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	SLSI_NET_DBG2(dev, SLSI_MLME, "mlme_channel_switch_req(vif:%d, freq: %d, channel info: 0x%x)\n", ndev_vif->ifnum, center_freq, chan_info);
	req = fapi_alloc(mlme_channel_switch_req, MLME_CHANNEL_SWITCH_REQ, ndev_vif->ifnum, 0);
	if (!req)
		return -ENOMEM;

	fapi_set_u16(req, u.mlme_channel_switch_req.channel_frequency, SLSI_FREQ_HOST_TO_FW(center_freq));
	fapi_set_u16(req, u.mlme_channel_switch_req.channel_information, chan_info);

	cfm = slsi_mlme_req_cfm(sdev, dev, req, MLME_CHANNEL_SWITCH_CFM);
	if (!cfm)
		return -EIO;

	if (fapi_get_u16(cfm, u.mlme_channel_switch_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_NET_ERR(dev, "mlme_channel_switch_cfm(result:%u) ERROR\n", fapi_get_u16(cfm, u.mlme_channel_switch_cfm.result_code));
		r = -EINVAL;
	}

	slsi_kfree_skb(cfm);
	return r;
}

int slsi_mlme_add_info_elements(struct slsi_dev *sdev, struct net_device *dev,  u16 purpose, const u8 *ies, const u16 ies_len)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *req;
	struct sk_buff    *cfm;
	int               r = 0;
	u8                *p;

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	req = fapi_alloc(mlme_add_info_elements_req, MLME_ADD_INFO_ELEMENTS_REQ, ndev_vif->ifnum, ies_len);
	if (!req)
		return -ENOMEM;

	fapi_set_u16(req, u.mlme_add_info_elements_req.purpose, purpose);

	if (ies_len != 0) {
		p = fapi_append_data(req, ies, ies_len);
		if (!p) {
			slsi_kfree_skb(req);
			return -EINVAL;
		}
	}

	SLSI_NET_DBG2(dev, SLSI_MLME, "mlme_add_info_elements_req(vif:%d, ies_len:%d)\n", ndev_vif->ifnum, ies_len);
	cfm = slsi_mlme_req_cfm(sdev, dev, req, MLME_ADD_INFO_ELEMENTS_CFM);
	if (!cfm)
		return -EIO;

	if (fapi_get_u16(cfm, u.mlme_add_info_elements_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_NET_ERR(dev, "mlme_add_info_elements_cfm(result:%u) ERROR\n", fapi_get_u16(cfm, u.mlme_add_info_elements_cfm.result_code));
		r = -EINVAL;
	}

	slsi_kfree_skb(cfm);
	return r;
}

int slsi_mlme_send_frame_data(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb, u16 msg_type,
			      u16 host_tag, u32 dwell_time, u32 period)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	u16 len = skb->len;
	struct sk_buff *original_skb = 0;
	int ret;

	/* don't let ARP frames exhaust all the control slots */
	if (msg_type == FAPI_MESSAGETYPE_ARP) {
		int free_slots = 0;

	    free_slots = hip4_free_ctrl_slots_count(&sdev->hip4_inst);

		if (free_slots < 0) {
			SLSI_DBG1(sdev, SLSI_MLME, "drop ARP (error in getting free slot count)\n");
			return free_slots;
		}

		if (free_slots < SLSI_MLME_ARP_DROP_FREE_SLOTS_COUNT) {
			SLSI_DBG1(sdev, SLSI_MLME, "drop ARP (running out of Control slots:%d)\n", free_slots);
			slsi_kfree_skb(skb);
			return NETDEV_TX_OK;
		}
	}

	/* check for headroom to push signal header; if not available, re-alloc headroom */
	if (skb_headroom(skb) < (fapi_sig_size(mlme_send_frame_req))) {
		struct sk_buff *skb2 = NULL;

		skb2 = slsi_skb_realloc_headroom(skb, fapi_sig_size(mlme_send_frame_req));
		if (!skb2)
			return -EINVAL;
		original_skb = skb;
		skb = skb2;
	}
	len = skb->len;
	(void)skb_push(skb, (fapi_sig_size(mlme_send_frame_req)));

	/* fill the signal header */
	fapi_set_u16(skb, id,           MLME_SEND_FRAME_REQ);
	fapi_set_u16(skb, receiver_pid,  0);
	fapi_set_u16(skb, sender_pid,    SLSI_TX_PROCESS_ID_MIN);
	fapi_set_u16(skb, fw_reference,  0);

	/* fill in signal parameters */
	fapi_set_u16(skb, u.mlme_send_frame_req.vif, ndev_vif->ifnum);

	if (host_tag == 0)
		host_tag = slsi_tx_mgmt_host_tag(sdev);

	fapi_set_u16(skb, u.mlme_send_frame_req.host_tag, host_tag);
	fapi_set_u16(skb, u.mlme_send_frame_req.data_unit_descriptor, FAPI_DATAUNITDESCRIPTOR_IEEE802_3_FRAME);
	fapi_set_u16(skb, u.mlme_send_frame_req.message_type, msg_type);
	fapi_set_u16(skb, u.mlme_send_frame_req.channel_frequency, 0);
	fapi_set_u32(skb, u.mlme_send_frame_req.dwell_time, dwell_time);
	fapi_set_u32(skb, u.mlme_send_frame_req.period, period);

	SLSI_DBG2(sdev, SLSI_MLME, "mlme_send_frame_req(vif:%d, message_type:%d, host_tag:%d)\n", ndev_vif->ifnum, msg_type, host_tag);
	ret = slsi_tx_control(sdev, dev, skb);
	if (ret != 0) {
		SLSI_WARN(sdev, "failed to send MLME signal(err=%d)\n", ret);
		return ret;
	}

	if (original_skb)
		slsi_kfree_skb(original_skb);

	/* as the frame is queued to HIP for transmission, store the host tag of the frames
	 * to validate the transmission status in MLME-Frame-Transmission.indication.
	 * Take necessary action based on the type of frame and status of it's transmission
	 */
	if (msg_type == FAPI_MESSAGETYPE_EAPOL_KEY_M4) {
		ndev_vif->sta.m4_host_tag = host_tag;
		SLSI_NET_DBG1(dev, SLSI_MLME, "EAPOL-Key M4 frame (host_tag:%d)\n", ndev_vif->sta.m4_host_tag);
	} else if (msg_type == FAPI_MESSAGETYPE_EAP_MESSAGE) {
		if (!ndev_vif->sta.is_wps && (ndev_vif->iftype == NL80211_IFTYPE_STATION)) {
			/* In case of non-P2P station and Enterprise security store the host_tag.
			 * If transmission of such frame fails, inform supplicant to disconnect.
			 */
			ndev_vif->sta.eap_hosttag = host_tag;
			SLSI_NET_DBG1(dev, SLSI_MLME, "EAP frame (host_tag:%d)\n", ndev_vif->sta.eap_hosttag);
		}
	}
	return ret;
}

int slsi_mlme_send_frame_mgmt(struct slsi_dev *sdev, struct net_device *dev, const u8 *frame, int frame_len,
			      u16 data_desc, u16 msg_type, u16 host_tag, u16 freq, u32 dwell_time, u32 period)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *req;
	struct sk_buff    *cfm;
	int               r = 0;
	u8                *p;

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	req = fapi_alloc(mlme_send_frame_req, MLME_SEND_FRAME_REQ, ndev_vif->ifnum, frame_len);
	if (!req) {
		SLSI_WARN(sdev, "failed to alloc memory\n");
		return -ENOMEM;
	}

	fapi_set_u16(req, u.mlme_send_frame_req.host_tag, host_tag);
	fapi_set_u16(req, u.mlme_send_frame_req.data_unit_descriptor, data_desc);
	fapi_set_u16(req, u.mlme_send_frame_req.message_type, msg_type);
	fapi_set_u16(req, u.mlme_send_frame_req.channel_frequency, freq);
	fapi_set_u32(req, u.mlme_send_frame_req.dwell_time, dwell_time);
	fapi_set_u32(req, u.mlme_send_frame_req.period, period);

	p = fapi_append_data(req, frame, frame_len);
	if (!p) {
		slsi_kfree_skb(req);
		SLSI_WARN(sdev, "failed to append data\n");
		return -EINVAL;
	}

	SLSI_NET_DBG2(dev, SLSI_MLME, "mlme_send_frame_req(vif:%d, message_type:%d,host_tag:%d)\n", ndev_vif->ifnum, msg_type, host_tag);
	cfm = slsi_mlme_req_cfm(sdev, dev, req, MLME_SEND_FRAME_CFM);
	if (!cfm)
		return -EIO;

	if (fapi_get_u16(cfm, u.mlme_send_frame_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_NET_ERR(dev, "mlme_send_frame_cfm(result:%u) ERROR\n", fapi_get_u16(cfm, u.mlme_send_frame_cfm.result_code));
		r = -EINVAL;
	}

	slsi_kfree_skb(cfm);
	return r;
}

int slsi_mlme_reset_dwell_time(struct slsi_dev *sdev, struct net_device *dev)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *req;
	struct sk_buff    *cfm;
	int               r = 0;

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	SLSI_NET_DBG2(dev, SLSI_MLME, "mlme_reset_dwell_time_req (vif:%d)\n", ndev_vif->ifnum);

	req = fapi_alloc(mlme_reset_dwell_time_req, MLME_RESET_DWELL_TIME_REQ, ndev_vif->ifnum, 0);

	if (!req)
		return -ENOMEM;

	cfm = slsi_mlme_req_cfm(sdev, dev, req, MLME_RESET_DWELL_TIME_CFM);
	if (!cfm)
		return -EIO;

	if (fapi_get_u16(cfm, u.mlme_reset_dwell_time_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_NET_ERR(dev, "mlme_reset_dwell_time_cfm(result:%u) ERROR\n", fapi_get_u16(cfm, u.mlme_reset_dwell_time_cfm.result_code));
		r = -EINVAL;
	}

	slsi_kfree_skb(cfm);
	return r;
}

int slsi_mlme_set_packet_filter(struct slsi_dev *sdev, struct net_device *dev,
				int pkt_filter_len,
				u8 num_filters,
				struct slsi_mlme_pkt_filter_elem *pkt_filter_elems)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *req;
	struct sk_buff    *cfm;
	int               r = 0, i = 0, j = 0;
	u8                *p;
	u8                index = 0;

	if (WARN_ON(!ndev_vif->activated))
		return -EINVAL;

	if (WARN_ON(!num_filters))
		return -EINVAL;

	req = fapi_alloc(mlme_set_packet_filter_req, MLME_SET_PACKET_FILTER_REQ, ndev_vif->ifnum, pkt_filter_len);
	if (!req)
		return -ENOMEM;

	p = fapi_append_data(req, NULL, pkt_filter_len);
	if (!p) {
		slsi_kfree_skb(req);
		return -EINVAL;
	}

	for (i = 0; i < num_filters; i++) {
		struct slsi_mlme_pkt_filter_elem pkt_filter_elem = pkt_filter_elems[i];

		memcpy(&p[index], pkt_filter_elem.header, SLSI_PKT_FILTER_ELEM_HDR_LEN);
		index += SLSI_PKT_FILTER_ELEM_HDR_LEN;

		for (j = 0; j < pkt_filter_elem.num_pattern_desc; j++) {
			p[index++] = pkt_filter_elem.pattern_desc[j].offset;
			p[index++] = pkt_filter_elem.pattern_desc[j].mask_length;
			memcpy(&p[index], pkt_filter_elem.pattern_desc[j].mask, pkt_filter_elem.pattern_desc[j].mask_length);
			index += pkt_filter_elem.pattern_desc[j].mask_length;
			memcpy(&p[index], pkt_filter_elem.pattern_desc[j].pattern, pkt_filter_elem.pattern_desc[j].mask_length);
			index += pkt_filter_elem.pattern_desc[j].mask_length;
		}
	}

	SLSI_NET_DBG2(dev, SLSI_MLME, "mlme_set_packet_filter_req(vif:%d, num_filters:%d)\n", ndev_vif->ifnum, num_filters);
	cfm = slsi_mlme_req_cfm(sdev, dev, req, MLME_SET_PACKET_FILTER_CFM);
	if (!cfm)
		return -EIO;

	slsi_kfree_skb(cfm);
	return r;
}

int slsi_mlme_set_pmk(struct slsi_dev *sdev, struct net_device *dev, const u8 *pmk, u16 pmklen)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *req;
	struct sk_buff    *cfm;
	int               r = 0;

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	if (WARN_ON(!ndev_vif->activated))
		return -EINVAL;
	if (pmk)
		req = fapi_alloc(mlme_set_pmk_req, MLME_SET_PMK_REQ, ndev_vif->ifnum, pmklen);
	else
		req = fapi_alloc(mlme_set_pmk_req, MLME_SET_PMK_REQ, ndev_vif->ifnum, 0);

	if (!req)
		return -ENOMEM;
	if (pmk)
		fapi_append_data(req, pmk, pmklen);

	SLSI_NET_DBG2(dev, SLSI_MLME, "mlme_set_pmk_req(vif:%u, pmklen:%d)\n", ndev_vif->ifnum, pmklen);
	cfm = slsi_mlme_req_cfm(sdev, dev, req, MLME_SET_PMK_CFM);
	if (!cfm)
		return -EIO;

	if (fapi_get_u16(cfm, u.mlme_set_pmk_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_NET_ERR(dev, "mlme_set_pmk_cfm(result:%u) ERROR\n", fapi_get_u16(cfm, u.mlme_set_pmk_cfm.result_code));
		r = -EINVAL;
	}
	slsi_kfree_skb(cfm);
	return r;
}

int slsi_mlme_roam(struct slsi_dev *sdev, struct net_device *dev, const u8 *bssid, u16 freq)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *req;
	struct sk_buff    *cfm;
	int               r = 0;

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));
	if (WARN_ON(!ndev_vif->activated))
		return -EINVAL;

	SLSI_NET_DBG1(dev, SLSI_MLME, "mlme_roam_req(vif:%u, bssid:%pM, freq:%d)\n", ndev_vif->ifnum, bssid, freq);
	req = fapi_alloc(mlme_roam_req, MLME_ROAM_REQ, ndev_vif->ifnum, 0);
	if (!req)
		return -ENOMEM;
	fapi_set_memcpy(req, u.mlme_roam_req.bssid, bssid);
	fapi_set_u16(req, u.mlme_roam_req.channel_frequency, SLSI_FREQ_HOST_TO_FW(freq));

	cfm = slsi_mlme_req_cfm(sdev, dev, req, MLME_ROAM_CFM);
	if (!cfm)
		return -EIO;
	if (fapi_get_u16(cfm, u.mlme_roam_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_NET_ERR(dev, "mlme_roam_cfm(result:%u) ERROR\n", fapi_get_u16(cfm, u.mlme_roam_cfm.result_code));
		r = -EINVAL;
	}
	slsi_kfree_skb(cfm);
	ndev_vif->sta.roam_in_progress = true;
	return r;
}

int slsi_mlme_set_cached_channels(struct slsi_dev *sdev, struct net_device *dev, u32 channels_count, u8 *channels)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *req;
	struct sk_buff    *cfm;
	u8                *p;
	int               r = 0;
	size_t            channel_ie = 0;
	int               i;
	const u8          channels_list_ie_header[] = {
		0xDD,					/* Element ID: Vendor Specific */
		0x05,					/* Length: actual length will be updated later */
		0x00, 0x16, 0x32,       /* OUI: Samsung Electronics Co. */
		0x01,					/* OUI Type:  Scan parameters */
		0x02					/* OUI Subtype: channel list */
	};

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));
	if (WARN_ON(!ndev_vif->activated))
		return -EINVAL;

	if (channels_count) {
		channel_ie += 6 + (channels_count * SLSI_SCAN_CHANNEL_DESCRIPTOR_SIZE);
		req = fapi_alloc(mlme_set_cached_channels_req, MLME_SET_CACHED_CHANNELS_REQ, ndev_vif->ifnum, channel_ie);
	} else {
		req = fapi_alloc(mlme_set_cached_channels_req, MLME_SET_CACHED_CHANNELS_REQ, ndev_vif->ifnum, 0);
	}
	if (!req)
		return -ENOMEM;

	if (channels_count) {
		u16 freq_fw_unit;
		u8  *channels_list_ie = fapi_append_data(req, channels_list_ie_header, sizeof(channels_list_ie_header));

		if (!channels_list_ie) {
			SLSI_WARN(sdev, "channel list IE append failed\n");
			slsi_kfree_skb(req);
			return -EINVAL;
		}

		for (i = 0; i < channels_count; i++) {
			SLSI_NET_DBG3(dev, SLSI_MLME, "request for channels %d\n", channels[i]);
			p = fapi_append_data(req, NULL, SLSI_SCAN_CHANNEL_DESCRIPTOR_SIZE);
			if (!p) {
				slsi_kfree_skb(req);
				return -EINVAL;
			}
			freq_fw_unit = 2 * ieee80211_channel_to_frequency(channels[i], (channels[i] <= 14) ? NL80211_BAND_2GHZ : NL80211_BAND_5GHZ);
			freq_fw_unit = cpu_to_le16(freq_fw_unit);
			memcpy(p, &freq_fw_unit, sizeof(freq_fw_unit));

			p[2] = FAPI_SCANPOLICY_2_4GHZ | FAPI_SCANPOLICY_5GHZ;

			channels_list_ie[1] += SLSI_SCAN_CHANNEL_DESCRIPTOR_SIZE;
		}
	}

	SLSI_NET_DBG2(dev, SLSI_MLME, "mlme_set_cached_channels_req(vif:%d)\n", ndev_vif->ifnum);
	cfm = slsi_mlme_req_cfm(sdev, dev, req, MLME_SET_CACHED_CHANNELS_CFM);
	if (!cfm)
		return -EIO;

	if (fapi_get_u16(cfm, u.mlme_set_cached_channels_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_NET_ERR(dev, "mlme_set_cached_channels_cfm(result:%u) ERROR\n", fapi_get_u16(cfm, u.mlme_set_cached_channels_cfm.result_code));
		r = -EINVAL;
	}

	slsi_kfree_skb(cfm);
	return r;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 9))
int slsi_mlme_set_acl(struct slsi_dev *sdev, struct net_device *dev, u16 ifnum, const struct cfg80211_acl_data *params)
{
	struct sk_buff    *req;
	struct sk_buff    *cfm;
	size_t            mac_acl_size = 0;
	int               i, r = 0;

	mac_acl_size = sizeof((params->mac_addrs[0])) * (params->n_acl_entries);
	req = fapi_alloc(mlme_set_acl_req, MLME_SET_ACL_REQ, ifnum, mac_acl_size);
	if (!req) {
		SLSI_NET_ERR(dev, "fapi alloc failure\n");
		return -ENOMEM;
	}
	fapi_set_u16(req, u.mlme_set_acl_req.entries, params->n_acl_entries);
	fapi_set_u16(req, u.mlme_set_acl_req.acl_policy, params->acl_policy);

	for (i = 0; i < params->n_acl_entries; i++)
		fapi_append_data(req, params->mac_addrs[i].addr, sizeof((params->mac_addrs[i])));

	SLSI_NET_DBG2(dev, SLSI_MLME, "mlme_set_acl_req(vif:%u, n_acl_entries:%d)\n", ifnum, params->n_acl_entries);

	if (ifnum)
		cfm = slsi_mlme_req_cfm(sdev, dev, req, MLME_SET_ACL_CFM);
	else
		cfm = slsi_mlme_req_cfm(sdev, NULL, req, MLME_SET_ACL_CFM);
	if (!cfm)
		return -EIO;

	if (fapi_get_u16(cfm, u.mlme_set_acl_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_NET_ERR(dev, "mlme_set_acl_cfm(result:%u) ERROR\n", fapi_get_u16(cfm, u.mlme_set_acl_cfm.result_code));
		r = -EINVAL;
	}

	slsi_kfree_skb(cfm);
	return r;
}
#endif

int slsi_mlme_set_traffic_parameters(struct slsi_dev *sdev, struct net_device *dev, u16 user_priority, u16 medium_time, u16 minimun_data_rate, u8 *mac)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *req;
	struct sk_buff    *rx;
	int               r = 0;

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	if (WARN_ON(!ndev_vif->activated))
		return -EINVAL;

	if (WARN_ON(ndev_vif->vif_type != FAPI_VIFTYPE_STATION && ndev_vif->iftype == NL80211_IFTYPE_STATION))
		return -EINVAL;

	req = fapi_alloc(mlme_set_traffic_parameters_req, MLME_SET_TRAFFIC_PARAMETERS_REQ, ndev_vif->ifnum, 0);
	if (!req)
		return -ENOMEM;

	fapi_set_u16(req, u.mlme_set_traffic_parameters_req.user_priority, user_priority);
	fapi_set_u16(req, u.mlme_set_traffic_parameters_req.medium_time, medium_time);
	fapi_set_u16(req, u.mlme_set_traffic_parameters_req.minimum_data_rate, minimun_data_rate);

	if (mac)
		fapi_set_memcpy(req, u.mlme_set_traffic_parameters_req.peer_address, mac);
	else
		fapi_set_memset(req, u.mlme_set_traffic_parameters_req.peer_address, 0);

	SLSI_NET_DBG2(dev, SLSI_MLME, "mlme_set_traffic_parameters_req(vif:%u, user_priority:%d, medium_time:%d)\n", ndev_vif->ifnum, user_priority, medium_time);
	rx = slsi_mlme_req_cfm(sdev, dev, req, MLME_SET_TRAFFIC_PARAMETERS_CFM);
	if (!rx)
		return -EIO;

	if (fapi_get_u16(rx, u.mlme_set_traffic_parameters_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_NET_ERR(dev, "mlme_set_traffic_parameters_cfm(result:%u) ERROR\n", fapi_get_u16(rx, u.mlme_set_traffic_parameters_cfm.result_code));
		r = -EINVAL;
	}

	return r;
}

int slsi_mlme_del_traffic_parameters(struct slsi_dev *sdev, struct net_device *dev, u16 user_priority)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *req;
	struct sk_buff    *rx;
	int               r = 0;

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	if (WARN_ON(!ndev_vif->activated))
		return -EINVAL;

	if (WARN_ON(ndev_vif->vif_type != FAPI_VIFTYPE_STATION && ndev_vif->iftype == NL80211_IFTYPE_STATION))
		return -EINVAL;

	req = fapi_alloc(mlme_del_traffic_parameters_req, MLME_DEL_TRAFFIC_PARAMETERS_REQ, ndev_vif->ifnum, 0);
	if (!req)
		return -ENOMEM;

	fapi_set_u16(req, u.mlme_del_traffic_parameters_req.user_priority, user_priority);

	SLSI_NET_DBG2(dev, SLSI_MLME, "mlme_del_traffic_parameters_req(vif:%u, user_priority:%d)\n", ndev_vif->ifnum, user_priority);
	rx = slsi_mlme_req_cfm(sdev, dev, req, MLME_DEL_TRAFFIC_PARAMETERS_CFM);
	if (!rx)
		return -EIO;

	if (fapi_get_u16(rx, u.mlme_del_traffic_parameters_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_NET_ERR(dev, "mlme_del_traffic_parameters_cfm(result:%u) ERROR\n", fapi_get_u16(rx, u.mlme_del_traffic_parameters_cfm.result_code));
		r = -EINVAL;
	}

	return r;
}

int slsi_mlme_set_ext_capab(struct slsi_dev *sdev, struct net_device *dev, struct slsi_mib_value *mib_val)
{
	struct slsi_mib_data mib_data = { 0, NULL };
	int                  error = 0;

	error = slsi_mib_encode_octet(&mib_data, SLSI_PSID_UNIFI_EXTENDED_CAPABILITIES, mib_val->u.octetValue.dataLength, mib_val->u.octetValue.data, 0);
	if (error != SLSI_MIB_STATUS_SUCCESS) {
		error = -ENOMEM;
		goto exit;
	}

	if (WARN_ON(mib_data.dataLength == 0)) {
		error = -EINVAL;
		goto exit;
	}

	error = slsi_mlme_set(sdev, NULL, mib_data.data, mib_data.dataLength);
	kfree(mib_data.data);

	if (!error)
		return 0;

exit:
	SLSI_ERR(sdev, "Error in setting ext capab. error = %d\n", error);
	return error;
}

int slsi_mlme_set_hs2_ext_cap(struct slsi_dev *sdev, struct net_device *dev, const u8 *ies, int ie_len)
{
	struct slsi_mib_entry mib_entry;
	struct slsi_mib_data  mibreq = { 0, NULL };
	struct slsi_mib_data  mibrsp = { 0, NULL };
	const u8              *ext_capab_ie;
	int                   r                       = 0;
	int                   rx_length               = 0;
	int                   len                     = 0;

	slsi_mib_encode_get(&mibreq, SLSI_PSID_UNIFI_EXTENDED_CAPABILITIES, 0);

	/*  5 (header) + 9 (data)  + 2 (mlme expects 16 (??))*/
	mibrsp.dataLength = 16;
	mibrsp.data = kmalloc(mibrsp.dataLength, GFP_KERNEL);

	if (!mibrsp.data) {
		SLSI_ERR(sdev, "Failed to alloc for Mib response\n");
		kfree(mibreq.data);
		return -ENOMEM;
	}

	r = slsi_mlme_get(sdev, NULL, mibreq.data, mibreq.dataLength,
			  mibrsp.data, mibrsp.dataLength, &rx_length);
	kfree(mibreq.data);

	if (r == 0) {
		mibrsp.dataLength = rx_length;
		len = slsi_mib_decode(&mibrsp, &mib_entry);
		if (len == 0) {
			SLSI_ERR(sdev, "Mib decode error\n");
			r = -EINVAL;
			goto exit;
		}
	} else {
		SLSI_NET_DBG1(dev, SLSI_MLME, "Mib read failed (error: %d)\n", r);
		goto exit;
	}

	ext_capab_ie = cfg80211_find_ie(WLAN_EID_EXT_CAPABILITY, ies, ie_len);

	if (ext_capab_ie) {
		u8 ext_capab_ie_len = ext_capab_ie[1];

		ext_capab_ie += 2; /* skip the EID and length*/

		/*BSS Transition bit is bit 19 ,ie length must be >= 3 */
		if ((ext_capab_ie_len >= 3) && (ext_capab_ie[2] & SLSI_WLAN_EXT_CAPA2_BSS_TRANSISITION_ENABLED))
			mib_entry.value.u.octetValue.data[2] |= SLSI_WLAN_EXT_CAPA2_BSS_TRANSISITION_ENABLED;
		else
			mib_entry.value.u.octetValue.data[2] &= ~SLSI_WLAN_EXT_CAPA2_BSS_TRANSISITION_ENABLED;

		/*interworking bit is bit 31 ,ie length must be >= 4 */
		if ((ext_capab_ie_len >= 4) && (ext_capab_ie[3] & SLSI_WLAN_EXT_CAPA3_INTERWORKING_ENABLED))
			mib_entry.value.u.octetValue.data[3] |= SLSI_WLAN_EXT_CAPA3_INTERWORKING_ENABLED;
		else
			mib_entry.value.u.octetValue.data[3] &= ~SLSI_WLAN_EXT_CAPA3_INTERWORKING_ENABLED;

		/*QoS MAP is bit 32 ,ie length must be >= 5 */
		if ((ext_capab_ie_len >= 5) && (ext_capab_ie[4] & SLSI_WLAN_EXT_CAPA4_QOS_MAP_ENABLED))
			mib_entry.value.u.octetValue.data[4] |= SLSI_WLAN_EXT_CAPA4_QOS_MAP_ENABLED;
		else
			mib_entry.value.u.octetValue.data[4] &= ~SLSI_WLAN_EXT_CAPA4_QOS_MAP_ENABLED;

		/*WNM- Notification bit is bit 46 ,ie length must be >= 6 */
		if ((ext_capab_ie_len >= 6) && (ext_capab_ie[5] & SLSI_WLAN_EXT_CAPA5_WNM_NOTIF_ENABLED))
			mib_entry.value.u.octetValue.data[5] |= SLSI_WLAN_EXT_CAPA5_WNM_NOTIF_ENABLED;
		else
			mib_entry.value.u.octetValue.data[5] &= ~SLSI_WLAN_EXT_CAPA5_WNM_NOTIF_ENABLED;
	} else {
		mib_entry.value.u.octetValue.data[2] &= ~SLSI_WLAN_EXT_CAPA2_BSS_TRANSISITION_ENABLED;
		mib_entry.value.u.octetValue.data[3] &= ~SLSI_WLAN_EXT_CAPA3_INTERWORKING_ENABLED;
		mib_entry.value.u.octetValue.data[4] &= ~SLSI_WLAN_EXT_CAPA4_QOS_MAP_ENABLED;
		mib_entry.value.u.octetValue.data[5] &= ~SLSI_WLAN_EXT_CAPA5_WNM_NOTIF_ENABLED;
	}

	r = slsi_mlme_set_ext_capab(sdev, dev, &mib_entry.value);
exit:
	kfree(mibrsp.data);
	return r;
}

int slsi_mlme_tdls_peer_resp(struct slsi_dev *sdev, struct net_device *dev, u16 pid, u16 tdls_event)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *req;
	struct sk_buff    *cfm;

	req = fapi_alloc(mlme_tdls_peer_res, MLME_TDLS_PEER_RES, ndev_vif->ifnum, 0);
	if (!req)
		return -ENOMEM;

	fapi_set_u16(req, u.mlme_tdls_peer_res.peer_index, pid);
	fapi_set_u16(req, u.mlme_tdls_peer_res.tdls_event, tdls_event);

	SLSI_NET_DBG2(dev, SLSI_MLME, "mlme_tdls_peer_res(vif:%d)\n", ndev_vif->ifnum);
	cfm = slsi_mlme_req_no_cfm(sdev, dev, req);
	WARN_ON(cfm);

	return 0;
}

int slsi_mlme_tdls_action(struct slsi_dev *sdev, struct net_device *dev, const u8 *peer, int action, u16 center_freq, u16 chan_info)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *req;
	struct sk_buff    *cfm;
	int               r = 0;

	SLSI_NET_DBG2(dev, SLSI_MLME, "mlme_tdls_action_req(action:%u)\n", action);
	req = fapi_alloc(mlme_tdls_action_req, MLME_TDLS_ACTION_REQ, ndev_vif->ifnum, 0);
	if (!req)
		return -ENOMEM;

	center_freq = SLSI_FREQ_HOST_TO_FW(center_freq);

	fapi_set_memcpy(req, u.mlme_tdls_action_req.peer_sta_address, peer);
	fapi_set_u16(req, u.mlme_tdls_action_req.tdls_action, action);
	fapi_set_u16(req, u.mlme_tdls_action_req.channel_frequency, center_freq);
	fapi_set_u16(req, u.mlme_tdls_action_req.channel_information, chan_info);

	cfm = slsi_mlme_req_cfm(sdev, dev, req, MLME_TDLS_ACTION_CFM);
	if (!cfm)
		return -EIO;

	if (fapi_get_u16(cfm, u.mlme_tdls_action_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_NET_ERR(dev, "mlme_tdls_action_cfm(result:%u) ERROR\n", fapi_get_u16(cfm, u.mlme_tdls_action_cfm.result_code));
		r = -EINVAL;
	}

	slsi_kfree_skb(cfm);

	return r;
}

int slsi_mlme_reassociate(struct slsi_dev *sdev, struct net_device *dev)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *req;
	struct sk_buff    *cfm;
	int               r = 0;

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));
	if (WARN_ON(!ndev_vif->activated))
		return -EINVAL;

	SLSI_NET_DBG1(dev, SLSI_MLME, "mlme_reassoc_req(vif:%u)\n", ndev_vif->ifnum);
	req = fapi_alloc(mlme_reassociate_req, MLME_REASSOCIATE_REQ, ndev_vif->ifnum, 0);
	if (!req)
		return -ENOMEM;

	cfm = slsi_mlme_req_cfm(sdev, dev, req, MLME_REASSOCIATE_CFM);
	if (!cfm)
		return -EIO;

	if (fapi_get_u16(cfm, u.mlme_reassociate_cfm.result_code) == FAPI_RESULTCODE_HOST_REQUEST_SUCCESS) {
		SLSI_NET_DBG1(dev, SLSI_MLME, "mlme_reassoc_cfm(result:0x%4x)\n", fapi_get_u16(cfm, u.mlme_reassociate_cfm.result_code));
	} else {
		SLSI_NET_ERR(dev, "mlme_reassoc_cfm(result:0x%4x) ERROR\n", fapi_get_u16(cfm, u.mlme_reassociate_cfm.result_code));
		r = -EINVAL;
	}
	slsi_kfree_skb(cfm);
	return r;
}

void slsi_mlme_reassociate_resp(struct slsi_dev *sdev, struct net_device *dev)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *req;
	struct sk_buff    *cfm;

	if (slsi_is_test_mode_enabled()) {
		SLSI_NET_INFO(dev, "Skip sending signal, WlanLite FW does not support MLME_REASSOCIATE_RESP\n");
		return;
	}

	SLSI_NET_DBG1(dev, SLSI_MLME, "mlme_reassociate_resp(vif:%d)\n", ndev_vif->ifnum);
	req = fapi_alloc(mlme_reassociate_res, MLME_REASSOCIATE_RES, ndev_vif->ifnum, 0);
	if (!req)
		return;

	cfm = slsi_mlme_req_no_cfm(sdev, dev, req);
	WARN_ON(cfm);
}

#ifdef CONFIG_SCSC_WLAN_GSCAN_ENABLE
int slsi_mlme_gscan_append_tracking_channels(struct slsi_dev   *sdev,
					     struct net_device *dev,
					     struct sk_buff    *skb,
					     struct slsi_gscan *gscan)
{
	u32 n_channels = 0;
	int len = 0;

	if (gscan->nl_bucket.band == WIFI_BAND_UNSPECIFIED)
		n_channels = gscan->nl_bucket.num_channels;
	else
		n_channels = 1;

	SLSI_DBG3(sdev, SLSI_GSCAN, "num chan: %d", n_channels);
	if (skb) {
		int r = slsi_mlme_append_gscan_channel_list(sdev, dev, skb, &gscan->nl_bucket);

		if (r) {
			SLSI_ERR(sdev, "Error appending channel list [%d]\n", r);
			return r;
		}
	} else {
		len = SLSI_SCAN_PRIVATE_IE_CHANNEL_LIST_HEADER_LEN + (n_channels * SLSI_SCAN_CHANNEL_DESCRIPTOR_SIZE);
	}
	return len;
}

int slsi_mlme_significant_change_set(struct slsi_dev *sdev, struct net_device *dev,
				     struct slsi_nl_significant_change_params *significant_param_ptr)
{
	struct sk_buff    *req;
	struct sk_buff    *rx;
	int               r = 0;
	size_t            alloc_data_size;
	u32               i, j;
	u32               num_bssid_descriptor_ies;
	struct slsi_gscan *gscan = sdev->gscan;

	u8                scan_timing_ie[] = {
		0xdd,				/* Element ID: Vendor Specific */
		0x11,				/* Length */
		0x00, 0x16, 0x32,		/* OUI: Samsung Electronics Co. */
		0x01,				/* OUI Type: Scan parameters */
		0x01,				/* OUI Subtype: Scan timing */
		0x00, 0x00, 0x00, 0x00,		/* Min_Period:  filled later in the function */
		0x00, 0x00, 0x00, 0x00,		/* Max_Period:  filled later in the function */
		0x00,				/* Exponent */
		0x00,				/* Step count */
		0x00, 0x00			/* Skip first period: false */
	};

	u8                change_tracking_ie[] = {  /* wifi change tracking ie*/
		0xdd, 0x07,                         /* VENDOR_SPECIFIC, LEN */
		0x00, 0x16, 0x32,                   /* OUI */
		0x01, 0x06,                         /* OUI Type, OUI sub-type */
		0x00,                               /* The number of samples needed for averaging RSSI */
		0x00                                /* Min num of APs breaching low_RSSI_Threshold that are needed before informing the host */
	};

	u8                bssid_descriptor_ie[] = {                 /* Bssid descriptor list ie*/
		0xdd, 6 + (SLSI_FW_MAX_BSSID_PER_TRACKING_IE * SLSI_FW_BSSID_DESCRIPTOR_BSS_SIZE), /* VENDOR_SPECIFIC, LEN(update later) */
		0x00, 0x16, 0x32,                                   /* OUI */
		0x01, 0x05,                                         /* OUI Type, OUI sub-type */
		0x00,                                               /* ap lost threshold */
	};

	if (slsi_is_test_mode_enabled()) {
		SLSI_NET_INFO(dev, "Skip sending signal, WlanLite FW does not support MLME_ADD_SCAN.request\n");
		return -EOPNOTSUPP;
	}

	if (WARN_ON(!(dev->dev_addr)))
		return -EINVAL;

	change_tracking_ie[7] = (u8)significant_param_ptr->rssi_sample_size;
	change_tracking_ie[8] = (u8)significant_param_ptr->min_breaching;
	num_bssid_descriptor_ies = significant_param_ptr->num_bssid / SLSI_FW_MAX_BSSID_PER_TRACKING_IE;
	if (significant_param_ptr->num_bssid % SLSI_FW_MAX_BSSID_PER_TRACKING_IE)
		num_bssid_descriptor_ies++;

	alloc_data_size = sizeof(scan_timing_ie) + sizeof(change_tracking_ie)
			  + sizeof(bssid_descriptor_ie) * num_bssid_descriptor_ies
			  + SLSI_FW_BSSID_DESCRIPTOR_BSS_SIZE * significant_param_ptr->num_bssid
			  + slsi_mlme_gscan_append_tracking_channels(sdev, dev, NULL, gscan);

	req = fapi_alloc(mlme_add_scan_req, MLME_ADD_SCAN_REQ, 0, alloc_data_size);
	if (!req)
		return -ENOMEM;

	fapi_set_u16(req, u.mlme_add_scan_req.scan_id, gscan->bucket[0]->scan_id);
	fapi_set_u16(req, u.mlme_add_scan_req.scan_type, FAPI_SCANTYPE_GSCAN);
	fapi_set_memcpy(req, u.mlme_add_scan_req.device_address, dev->dev_addr);
	fapi_set_u16(req, u.mlme_add_scan_req.report_mode_bitmap, FAPI_REPORTMODE_REAL_TIME);

	r = slsi_mlme_gscan_append_tracking_channels(sdev, dev, req, gscan);
	if (r != 0) {
		slsi_kfree_skb(req);
		return r;
	}

	SLSI_U32_TO_BUFF_LE((gscan->nl_bucket.period * 1000), &scan_timing_ie[7]);
	fapi_append_data(req, scan_timing_ie, sizeof(scan_timing_ie));
	fapi_append_data(req, change_tracking_ie, sizeof(change_tracking_ie));

	bssid_descriptor_ie[7] = significant_param_ptr->lost_ap_sample_size;
	for (i = 0; i < num_bssid_descriptor_ies; i++) {
		u8 bss_num = significant_param_ptr->num_bssid - (i * SLSI_FW_MAX_BSSID_PER_TRACKING_IE);

		if (bss_num < SLSI_FW_MAX_BSSID_PER_TRACKING_IE)
			bssid_descriptor_ie[1] = 6 + (SLSI_FW_BSSID_DESCRIPTOR_BSS_SIZE * bss_num);
		else
			bss_num = SLSI_FW_MAX_BSSID_PER_TRACKING_IE;

		fapi_append_data(req, bssid_descriptor_ie, sizeof(bssid_descriptor_ie));
		for (j = i * SLSI_FW_MAX_BSSID_PER_TRACKING_IE; j < (i * SLSI_FW_MAX_BSSID_PER_TRACKING_IE + bss_num); j++) {
			fapi_append_data(req, significant_param_ptr->ap[j].bssid, ETH_ALEN);
			fapi_append_data(req, (u8 *)&significant_param_ptr->ap[j].low, 2);
			fapi_append_data(req, (u8 *)&significant_param_ptr->ap[j].high, 2);
		}
	}

	/* Use the Global sig_wait not the Interface specific for Scan Req */
	rx = slsi_mlme_req_cfm(sdev, NULL, req, MLME_ADD_SCAN_CFM);
	if (!rx) {
		gscan->bucket[0]->for_change_tracking = false;
		return -EIO;
	} else if (fapi_get_u16(rx, u.mlme_add_scan_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		gscan->bucket[0]->for_change_tracking = false;
		SLSI_NET_ERR(dev, "mlme_add_scan_cfm(ERROR:%u)", fapi_get_u16(rx, u.mlme_add_scan_cfm.result_code));
		r = -EINVAL;
	} else {
		gscan->bucket[0]->for_change_tracking = true;
	}
	slsi_kfree_skb(rx);

	return r;
}

int slsi_mlme_add_range_req(struct slsi_dev *sdev, u8 count,
			    struct slsi_rtt_config *nl_rtt_params, u16 rtt_id, u16 vif_idx)
{
	struct sk_buff *req;
	struct sk_buff *rx;
	int            r = 0, i;
	size_t         alloc_data_size = 0;
	u8             fapi_ie_generic[] = { 0xdd, 0x24, 0x00, 0x16, 0x32, 0x0a, 0x01 };
	/* calculate data size */
	alloc_data_size += count * (fapi_ie_generic[1] + 2);

	req = fapi_alloc(mlme_add_range_req, MLME_ADD_RANGE_REQ, 0, alloc_data_size);
	if (!req) {
		SLSI_ERR(sdev, "failed to alloc %zd\n", alloc_data_size);
		return -ENOMEM;
	}
	SLSI_DBG2(sdev, SLSI_MLME, "count-->%d allocated data size: %d\n", count, alloc_data_size);
	/*fill the data */
	fapi_set_u16(req, u.mlme_add_range_req.vif, vif_idx);
	fapi_set_u16(req, u.mlme_add_range_req.rtt_id, rtt_id);
	for (i = 0; i < count; i++) {
		fapi_append_data(req, fapi_ie_generic, sizeof(fapi_ie_generic));
		fapi_append_data(req, nl_rtt_params[i].source_addr, ETH_ALEN);
		fapi_append_data(req, nl_rtt_params[i].peer_addr, ETH_ALEN);
		fapi_append_data(req, (u8 *)&nl_rtt_params[i].type, 2);
		fapi_append_data(req, (u8 *)&nl_rtt_params[i].channel_freq, 2);
		fapi_append_data(req, (u8 *)&nl_rtt_params[i].channel_info, 2);
		fapi_append_data(req, (u8 *)&nl_rtt_params[i].burst_period, 1);
		fapi_append_data(req, (u8 *)&nl_rtt_params[i].num_burst, 1);
		fapi_append_data(req, (u8 *)&nl_rtt_params[i].num_frames_per_burst, 1);
		fapi_append_data(req, (u8 *)&nl_rtt_params[i].num_retries_per_ftmr, 1);
		fapi_append_data(req, (u8 *)&nl_rtt_params[i].burst_duration, 1);
		fapi_append_data(req, (u8 *)&nl_rtt_params[i].preamble, 2);
		fapi_append_data(req, (u8 *)&nl_rtt_params[i].bw, 2);
		fapi_append_data(req, (u8 *)&nl_rtt_params[i].LCI_request, 2);
		fapi_append_data(req, (u8 *)&nl_rtt_params[i].LCR_request, 2);
	}
	rx = slsi_mlme_req_cfm(sdev, NULL, req, MLME_ADD_RANGE_CFM);
	SLSI_DBG2(sdev, SLSI_MLME, "(After mlme req cfm for rtt config)\n");
	if (!rx)
		return -EIO;
	if (fapi_get_u16(rx, u.mlme_add_range_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_ERR(sdev, "mlme_add_range_cfm(ERROR:%x)",
			 fapi_get_u16(rx, u.mlme_add_range_cfm.result_code));
		r = -EINVAL;
	}

	slsi_kfree_skb(rx);
	return r;
}

bool slsi_del_range_cfm_validate(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *cfm)
{
	int  result = fapi_get_u16(cfm, u.mlme_del_range_cfm.result_code);
	bool r = false;

	SLSI_UNUSED_PARAMETER(sdev);

	if (WARN_ON(!dev))
		goto exit;

	if (result == FAPI_RESULTCODE_SUCCESS)
		r = true;
	else
		SLSI_NET_ERR(dev, "mlme_del_range_cfm(result:%u) ERROR\n", result);

exit:
	slsi_kfree_skb(cfm);
	return r;
}

int slsi_mlme_del_range_req(struct slsi_dev *sdev, struct net_device *dev, u16 count,
			    u8 *addr, u16 rtt_id)
{
	struct sk_buff *req;
	struct sk_buff *rx;
	u16           *rtt_vif_idx = sdev->rtt_vif;
	int            r = 0, i;
	size_t         alloc_data_size = 0;

	/* calculate data size-->2 bytes for vif */
	alloc_data_size += count * sizeof(ETH_ALEN);
	/* Alloc data size */
	req = fapi_alloc(mlme_del_range_req, MLME_DEL_RANGE_REQ, 0, alloc_data_size);
	if (!req) {
		SLSI_ERR(sdev, "failed to alloc %zd\n", alloc_data_size);
		return -ENOMEM;
	}
	/*fill the data */
	fapi_set_u16(req, u.mlme_del_range_req.vif, rtt_vif_idx[rtt_id]);
	fapi_set_u16(req, u.mlme_del_range_req.rtt_id, rtt_id);
	fapi_set_u16(req, u.mlme_del_range_req.entries, count);
	for (i = 0; i < count; i++)
		fapi_append_data(req, &addr[i * ETH_ALEN], ETH_ALEN);

	rx = slsi_mlme_req_cfm_ind(sdev, dev, req, MLME_DEL_RANGE_CFM, MLME_RANGE_IND, slsi_del_range_cfm_validate);
	sdev->rtt_vif[rtt_id] = -1;
	if (!rx) {
		SLSI_NET_ERR(dev, "mlme_del_range_cfm() ERROR\n");
		slsi_kfree_skb(rx);
		return -EINVAL;
	}
	slsi_kfree_skb(rx);
	return r;
}

#define SLSI_FAPI_EPNO_NETWORK_MIN_SIZE (3)
int slsi_mlme_set_pno_list(struct slsi_dev *sdev, int count,
			   struct slsi_epno_param *epno_param, struct slsi_epno_hs2_param *epno_hs2_param)
{
	struct sk_buff *req;
	struct sk_buff *rx;
	int            r = 0;
	size_t         alloc_data_size = 0;
	u32            i, j;
	u8             fapi_ie_generic[] = { 0xdd, 0, 0x00, 0x16, 0x32, 0x01, 0x00 };
	u8             *buff_ptr, *ie_start_pos;

	if (count) {
		/* calculate data size */
		if (epno_param) {
			alloc_data_size += sizeof(fapi_ie_generic) + SLSI_FAPI_EPNO_NETWORK_MIN_SIZE * count + 11;
			for (i = 0; i < count; i++)
				alloc_data_size += epno_param->epno_ssid[i].ssid_len;
		} else if (epno_hs2_param) {
			for (i = 0; i < count; i++) {
				/* fapi_ie_generic + Network_block_ID(1) + Realm_length(1) + realm_data(x)
				 * + Roaming_Consortium_Count(1) + Roaming Consortium data(16 * 8) +
				 * PLMN length(1) + PLMN data(6)
				 */
				if (strlen(epno_hs2_param->realm))
					alloc_data_size += sizeof(fapi_ie_generic) + 1 + 1 + (strlen(epno_hs2_param->realm) + 1)
							   + 1 + 16 * 8 + 1 + 6;
				else
					alloc_data_size += sizeof(fapi_ie_generic) + 1 + 1 + 0
							   + 1 + 16 * 8 + 1 + 6;
			}
		}
	}

	/* Alloc data size */
	req = fapi_alloc(mlme_set_pno_list_req, MLME_SET_PNO_LIST_REQ, 0, alloc_data_size);
	if (!req) {
		SLSI_ERR(sdev, "failed to alloc %zd\n", alloc_data_size);
		return -ENOMEM;
	}
	if (count) {
		/* Fill data */
		if (epno_param) {
			fapi_ie_generic[1] = alloc_data_size - 2;
			fapi_ie_generic[6] = 9; /* OUI */
			fapi_append_data(req, fapi_ie_generic, sizeof(fapi_ie_generic));
			fapi_append_data(req, (u8 *)epno_param, (sizeof(*epno_param) - 1));
			for (i = 0; i < count; i++) {
				fapi_append_data(req, (u8 *)&epno_param->epno_ssid[i].flags, 2);
				fapi_append_data(req, (u8 *)&epno_param->epno_ssid[i].ssid_len, 1);
				fapi_append_data(req, (u8 *)epno_param->epno_ssid[i].ssid,
						 epno_param->epno_ssid[i].ssid_len);
			}
		} else if (epno_hs2_param) {
			u8 realm_length;
			u8 roaming_consortium_count = 16;
			u8 plmn_length = 6;
			u8 plmn_digit[6];

			fapi_ie_generic[6] = 0x10; /* OUI subtype = Passpoint Network */
			for (i = 0; i < count; i++) {
				buff_ptr = fapi_append_data(req, fapi_ie_generic, sizeof(fapi_ie_generic));
				if (!buff_ptr) {
					SLSI_ERR(sdev, "failed append data\n");
					slsi_kfree_skb(req);
					return -EINVAL;
				}
				ie_start_pos = buff_ptr;

				fapi_append_data(req, (u8 *)&epno_hs2_param[i].id, 1);

				realm_length = strlen(epno_hs2_param[i].realm);
				if (realm_length) {
					realm_length++;
					fapi_append_data(req, &realm_length, 1);
					fapi_append_data(req, epno_hs2_param[i].realm, realm_length);
				} else {
					fapi_append_data(req, &realm_length, 1);
				}

				fapi_append_data(req, &roaming_consortium_count, 1);
				fapi_append_data(req, (u8 *)&epno_hs2_param[i].roaming_consortium_ids, 16 * 8);

				fapi_append_data(req, &plmn_length, 1);
				for (j = 0; j < 3; j++) {
					plmn_digit[j * 2] =  epno_hs2_param[i].plmn[i] & 0x0F;
					plmn_digit[(j * 2) + 1] =  epno_hs2_param[i].plmn[i] & 0xF0 >> 4;
				}
				buff_ptr = fapi_append_data(req, plmn_digit, sizeof(plmn_digit));
				if (!buff_ptr) {
					SLSI_ERR(sdev, "failed append data\n");
					slsi_kfree_skb(req);
					return -EINVAL;
				}

				buff_ptr += sizeof(plmn_digit);
				ie_start_pos[1] = buff_ptr - ie_start_pos - 2; /* fill ie length field */
			}
		}
	}

	/* Send signal */
	/* Use the Global sig_wait not the Interface specific for mlme-set-pno.list */
	rx = slsi_mlme_req_cfm(sdev, NULL, req, MLME_SET_PNO_LIST_CFM);
	if (!rx)
		return -EIO;
	if (fapi_get_u16(rx, u.mlme_set_pno_list_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_ERR(sdev, "mlme_set_pno_list_cfm(ERROR:%u)",
			 fapi_get_u16(rx, u.mlme_set_pno_list_cfm.result_code));
		r = -EINVAL;
	}
	slsi_kfree_skb(rx);
	return r;
}

int slsi_mlme_start_link_stats_req(struct slsi_dev *sdev, u16 mpdu_size_threshold, bool aggressive_stats_enabled)
{
	struct sk_buff    *req;
	struct sk_buff    *cfm;
	int               r = 0;

	req = fapi_alloc(mlme_start_link_statistics_req, MLME_START_LINK_STATISTICS_REQ, 0, 0);
	if (!req) {
		SLSI_ERR(sdev, "memory allocation failed for signal\n");
		return -ENOMEM;
	}

	fapi_set_u16(req, u.mlme_start_link_statistics_req.mpdu_size_threshold, mpdu_size_threshold);
	fapi_set_u16(req, u.mlme_start_link_statistics_req.aggressive_statistics_gathering_enabled,
		     aggressive_stats_enabled);

	SLSI_DBG2(sdev, SLSI_MLME, "(mpdu_size_threshold:%d, aggressive_stats_enabled:%d)\n",
		  mpdu_size_threshold, aggressive_stats_enabled);
	cfm = slsi_mlme_req_cfm(sdev, NULL, req, MLME_START_LINK_STATISTICS_CFM);
	if (!cfm)
		return -EIO;

	if (fapi_get_u16(cfm, u.mlme_start_link_statistics_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_ERR(sdev, "mlme_start_link_statistics_cfm (result: %u) ERROR\n",
			 fapi_get_u16(cfm, u.mlme_start_link_statistics_cfm.result_code));
		r = -EINVAL;
		}

	slsi_kfree_skb(cfm);
	return r;
}

int slsi_mlme_stop_link_stats_req(struct slsi_dev *sdev, u16 stats_stop_mask)
{
	struct sk_buff    *req;
	struct sk_buff    *cfm;
	int               r = 0;

	req = fapi_alloc(mlme_stop_link_statistics_req, MLME_STOP_LINK_STATISTICS_REQ, 0, 0);
	if (!req) {
		SLSI_ERR(sdev, "memory allocation failed for signal\n");
		return -ENOMEM;
	}

	fapi_set_u16(req, u.mlme_stop_link_statistics_req.statistics_stop_bitmap, stats_stop_mask);

	SLSI_DBG2(sdev, SLSI_MLME, "statistics_stop_bitmap:%d\n", stats_stop_mask);
	cfm = slsi_mlme_req_cfm(sdev, NULL, req, MLME_STOP_LINK_STATISTICS_CFM);
	if (!cfm)
		return -EIO;

	if (fapi_get_u16(cfm, u.mlme_stop_link_statistics_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_ERR(sdev, "mlme_stop_link_statistics_cfm (result: %u) ERROR\n",
			 fapi_get_u16(cfm, u.mlme_stop_link_statistics_cfm.result_code));
		r = -EINVAL;
	}
	slsi_kfree_skb(cfm);
	return r;
}
#endif

int slsi_mlme_set_rssi_monitor(struct slsi_dev *sdev, struct net_device *dev, u8 enable, s8 low_rssi_threshold, s8 high_rssi_threshold)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *req;
	struct sk_buff    *cfm;
	int               r = 0;

	SLSI_NET_DBG1(dev, SLSI_MLME, "mlme_set_rssi_monitor(vif:%u), enable =%d, low_rssi_threshold = %d,high_rssi_threshold =%d\n",
		      ndev_vif->ifnum, enable, low_rssi_threshold, high_rssi_threshold);

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));
	req = fapi_alloc(mlme_monitor_rssi_req, MLME_MONITOR_RSSI_REQ, ndev_vif->ifnum, 0);
	if (!req) {
		SLSI_NET_ERR(dev, "fapi alloc failure\n");
		return -ENOMEM;
	}

	fapi_set_u16(req, u.mlme_monitor_rssi_req.rssi_monitoring_enabled, enable);
	fapi_set_u16(req, u.mlme_monitor_rssi_req.low_rssi_threshold, low_rssi_threshold);
	fapi_set_u16(req, u.mlme_monitor_rssi_req.high_rssi_threshold, high_rssi_threshold);

	cfm = slsi_mlme_req_cfm(sdev, dev, req, MLME_MONITOR_RSSI_CFM);
	if (!cfm)
		return -EIO;

	if (fapi_get_u16(cfm, u.mlme_monitor_rssi_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_NET_ERR(dev, "mlme_monitor_rssi_cfm(result:0x%04x) ERROR\n", fapi_get_u16(cfm, u.mlme_monitor_rssi_cfm.result_code));
		r = -EINVAL;
	}

	slsi_kfree_skb(cfm);
	return r;
}

struct slsi_mib_value *slsi_read_mibs(struct slsi_dev *sdev, struct net_device *dev,
				      struct slsi_mib_get_entry *mib_entries, int mib_count, struct slsi_mib_data *mibrsp)
{
	struct slsi_mib_data  mibreq = { 0, NULL };
	struct slsi_mib_value *values;
	int                   rx_length, r;

	r = slsi_mib_encode_get_list(&mibreq, mib_count, mib_entries);
	if (r != SLSI_MIB_STATUS_SUCCESS) {
		SLSI_WARN(sdev, "slsi_mib_encode_get_list fail %d\n", r);
		return NULL;
	}

	r = slsi_mlme_get(sdev, dev, mibreq.data, mibreq.dataLength, mibrsp->data, mibrsp->dataLength, &rx_length);
	kfree(mibreq.data);

	if (r != 0) {
		SLSI_ERR(sdev, "Mib (err:%d)\n", r);
		return NULL;
	}

	mibrsp->dataLength = (u32)rx_length;
	values = slsi_mib_decode_get_list(mibrsp, mib_count, mib_entries);
	if (!values)
		SLSI_WARN(sdev, "decode error\n");
	return values;
}

int slsi_mlme_set_ctwindow(struct slsi_dev *sdev, struct net_device *dev, unsigned int ct_param)
{
	struct netdev_vif *ndev_vif;
	struct sk_buff    *req;
	struct sk_buff    *cfm;
	int               r = 0;

	SLSI_NET_DBG1(dev, SLSI_MLME, "mlme_set_ctwindow(ct_param = %d)\n", ct_param);

	ndev_vif = netdev_priv(dev);

	req = fapi_alloc(mlme_set_ctwindow_req, MLME_SET_CTWINDOW_REQ, ndev_vif->ifnum, 0);
	if (!req)
		return -ENOMEM;

	fapi_set_u16(req, u.mlme_set_ctwindow_req.ctwindow, ((ct_param * 1000) / 1024));

	cfm = slsi_mlme_req_cfm(sdev, dev, req, MLME_SET_CTWINDOW_CFM);
	if (!cfm)
		return -EIO;

	if (fapi_get_u16(cfm, u.mlme_set_ctwindow_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_NET_ERR(dev, "mlme_set_ctwindow_cfm(result:%u) ERROR\n",
			     fapi_get_u16(cfm, u.mlme_set_ctwindow_cfm.result_code));
		r = -EINVAL;
	}
	slsi_kfree_skb(cfm);
	return r;
}

int slsi_mlme_set_p2p_noa(struct slsi_dev *sdev, struct net_device *dev, unsigned int noa_count,
			  unsigned int interval, unsigned int duration)
{
	struct netdev_vif *ndev_vif;
	struct sk_buff    *req;
	struct sk_buff    *cfm;
	int               r = 0;

	SLSI_NET_DBG1(dev, SLSI_MLME, "mlme_set_noa_req(noa_count = %d, interval = %d, duration = %d)\n",
		      noa_count, interval, duration);

	ndev_vif = netdev_priv(dev);

	req = fapi_alloc(mlme_set_noa_req, MLME_SET_NOA_REQ, ndev_vif->ifnum, 0);
	if (!req)
		return -ENOMEM;

	fapi_set_u16(req, u.mlme_set_noa_req.request_id, SLSI_NOA_CONFIG_REQUEST_ID);
	fapi_set_u16(req, u.mlme_set_noa_req.noa_count, noa_count);
	if (!interval)
		fapi_set_u32(req, u.mlme_set_noa_req.interval, (1 * 1024 * ndev_vif->ap.beacon_interval));
	else
		fapi_set_u32(req, u.mlme_set_noa_req.interval, interval * 1000);
	fapi_set_u32(req, u.mlme_set_noa_req.duration, duration * 1000);

	cfm = slsi_mlme_req_cfm(sdev, dev, req, MLME_SET_NOA_CFM);
	if (!cfm)
		return -EIO;

	if (fapi_get_u16(cfm, u.mlme_set_noa_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_NET_ERR(dev, "mlme_set_noa_cfm(result:%u) ERROR\n",
			     fapi_get_u16(cfm, u.mlme_set_noa_cfm.result_code));
		r = -EINVAL;
	}
	slsi_kfree_skb(cfm);
	return r;
}

int slsi_mlme_set_host_state(struct slsi_dev *sdev, struct net_device *dev, u8 host_state)
{
	struct sk_buff    *req;
	struct sk_buff    *cfm;
	int               r = 0;

	if (slsi_is_test_mode_enabled()) {
		SLSI_NET_INFO(dev, "Skip MLME_HOST_STATE_REQ in wlanlite mode\n");
		return -EOPNOTSUPP;
	}

	SLSI_NET_DBG1(dev, SLSI_MLME, "mlme_set_host_state(state =%d)\n", host_state);

	req = fapi_alloc(mlme_host_state_req, MLME_HOST_STATE_REQ, 0, 0);
	if (!req) {
		SLSI_NET_ERR(dev, "fapi alloc failure\n");
		return -ENOMEM;
	}

	fapi_set_u16(req, u.mlme_host_state_req.host_state, host_state);

	cfm = slsi_mlme_req_cfm(sdev, NULL, req, MLME_HOST_STATE_CFM);
	if (!cfm)
		return -EIO;

	if (fapi_get_u16(cfm, u.mlme_host_state_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_NET_ERR(dev, "mlme_host_state_cfm(result:0x%04x) ERROR\n",
			     fapi_get_u16(cfm, u.mlme_host_state_cfm.result_code));
		r = -EINVAL;
	}

	slsi_kfree_skb(cfm);
	return r;
}

static void slsi_mlme_nan_enable_fapi_data(struct sk_buff *req, struct slsi_hal_nan_enable_req *hal_req)
{
	u8  nan_config_fields_header[] = {0xdd, 0x00, 0x00, 0x16, 0x32, 0x0b, 0x01};
	u8 *header_ptr;
	u16 attribute;
	u8  band_usage = BIT(0) | BIT(1);
	u8  scan_param[] = {0, 0, 0};
	int len = 0;

	header_ptr = fapi_append_data(req, nan_config_fields_header, sizeof(nan_config_fields_header));
	len += sizeof(nan_config_fields_header);

	if (hal_req->config_2dot4g_beacons && !hal_req->beacon_2dot4g_val)
		band_usage &= ~BIT(0);
	if (hal_req->config_2dot4g_sdf && !hal_req->sdf_2dot4g_val)
		band_usage &= ~BIT(1);
	if (hal_req->config_5g_beacons && hal_req->beacon_5g_val)
		band_usage |= BIT(2);
	if (hal_req->config_5g_sdf && hal_req->sdf_5g_val)
		band_usage |= BIT(3);

	attribute = SLSI_FAPI_NAN_CONFIG_PARAM_BAND_USAGE;
	SLSI_FAPI_NAN_ATTRIBUTE_PUT_U8(req, attribute, band_usage);
	attribute = SLSI_FAPI_NAN_CONFIG_PARAM_MASTER_PREFERENCE;
	SLSI_FAPI_NAN_ATTRIBUTE_PUT_U16(req, attribute, hal_req->master_pref);
	len += 11; /* 5 for band_usage, 6 for master preference */

	if (hal_req->config_sid_beacon) {
		attribute = SLSI_FAPI_NAN_CONFIG_PARAM_SID_BEACON;
		SLSI_FAPI_NAN_ATTRIBUTE_PUT_U8(req, attribute, hal_req->sid_beacon_val);
		len += 5;
	}

	if (hal_req->config_2dot4g_rssi_close) {
		attribute = SLSI_FAPI_NAN_CONFIG_PARAM_2_4_RSSI_CLOSE;
		SLSI_FAPI_NAN_ATTRIBUTE_PUT_U8(req, attribute, hal_req->rssi_close_2dot4g_val);
		len += 5;
	}

	if (hal_req->config_2dot4g_rssi_middle) {
		attribute = SLSI_FAPI_NAN_CONFIG_PARAM_2_4_RSSI_MIDDLE;
		SLSI_FAPI_NAN_ATTRIBUTE_PUT_U8(req, attribute, hal_req->rssi_middle_2dot4g_val);
		len += 5;
	}

	if (hal_req->config_2dot4g_rssi_proximity) {
		attribute = SLSI_FAPI_NAN_CONFIG_PARAM_2_4_RSSI_PROXIMITY;
		SLSI_FAPI_NAN_ATTRIBUTE_PUT_U8(req, attribute, hal_req->rssi_proximity_2dot4g_val);
		len += 5;
	}

	if (hal_req->config_5g_rssi_close) {
		attribute = SLSI_FAPI_NAN_CONFIG_PARAM_5_RSSI_CLOSE;
		SLSI_FAPI_NAN_ATTRIBUTE_PUT_U8(req, attribute, hal_req->rssi_close_5g_val);
		len += 5;
	}

	if (hal_req->config_5g_rssi_middle) {
		attribute = SLSI_FAPI_NAN_CONFIG_PARAM_5_RSSI_MIDDLE;
		SLSI_FAPI_NAN_ATTRIBUTE_PUT_U8(req, attribute, hal_req->rssi_middle_5g_val);
		len += 5;
	}

	if (hal_req->config_5g_rssi_close_proximity) {
		attribute = SLSI_FAPI_NAN_CONFIG_PARAM_5_RSSI_PROXIMITY;
		SLSI_FAPI_NAN_ATTRIBUTE_PUT_U8(req, attribute, hal_req->rssi_close_proximity_5g_val);
		len += 5;
	}

	if (hal_req->config_hop_count_limit) {
		attribute = SLSI_FAPI_NAN_CONFIG_PARAM_HOP_COUNT_LIMIT;
		SLSI_FAPI_NAN_ATTRIBUTE_PUT_U8(req, attribute, hal_req->hop_count_limit_val);
		len += 5;
	}

	if (hal_req->config_rssi_window_size) {
		attribute = SLSI_FAPI_NAN_CONFIG_PARAM_RSSI_WINDOW_SIZE;
		SLSI_FAPI_NAN_ATTRIBUTE_PUT_U8(req, attribute, hal_req->rssi_window_size_val);
		len += 5;
	}

	if (hal_req->config_scan_params) {
		attribute = SLSI_FAPI_NAN_CONFIG_PARAM_SCAN_PARAMETER_2_4;
		scan_param[0] = hal_req->scan_params_val.dwell_time[0];
		scan_param[1] = hal_req->scan_params_val.scan_period[0] & 0x00FF;
		scan_param[2] = (hal_req->scan_params_val.scan_period[0] & 0xFF00) >> 8;
		SLSI_FAPI_NAN_ATTRIBUTE_PUT_DATA(req, attribute, scan_param, 3);
		attribute = SLSI_FAPI_NAN_CONFIG_PARAM_SCAN_PARAMETER_5;
		scan_param[0] = hal_req->scan_params_val.dwell_time[1];
		scan_param[1] = hal_req->scan_params_val.scan_period[1] & 0x00FF;
		scan_param[2] = (hal_req->scan_params_val.scan_period[1] & 0xFF00) >> 8;
		SLSI_FAPI_NAN_ATTRIBUTE_PUT_DATA(req, attribute, scan_param, 3);
		len += 7 * 2;
	}

	/* update len */
	header_ptr[1] = len - 2;
}

int slsi_mlme_nan_enable(struct slsi_dev *sdev, struct net_device *dev, struct slsi_hal_nan_enable_req *hal_req)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *req;
	struct sk_buff    *cfm;
	int               r = 0;
	u16               nan_oper_ctrl = 0;

	SLSI_NET_DBG3(dev, SLSI_MLME, "\n");

	/* max mbulk data for mlme-nan-start.req is about 87 bytes but
	 * allocate 100 bytes
	 */
	req = fapi_alloc(mlme_nan_start_req, MLME_NAN_START_REQ, ndev_vif->ifnum, 100);
	if (!req) {
		SLSI_NET_ERR(dev, "fapi alloc failure\n");
		return -ENOMEM;
	}

	if (hal_req->config_cluster_attribute_val)
		nan_oper_ctrl |= FAPI_NANOPERATIONCONTROL_CLUSTER_SDF;
	nan_oper_ctrl |= FAPI_NANOPERATIONCONTROL_MAC_ADDRESS_EVENT | FAPI_NANOPERATIONCONTROL_START_CLUSTER_EVENT |
			FAPI_NANOPERATIONCONTROL_JOINED_CLUSTER_EVENT;

	fapi_set_u16(req, u.mlme_nan_start_req.cluster_low, hal_req->cluster_low);
	fapi_set_u16(req, u.mlme_nan_start_req.cluster_high, hal_req->cluster_high);
	fapi_set_u16(req, u.mlme_nan_start_req.nan_operation_control_flags, nan_oper_ctrl);

	slsi_mlme_nan_enable_fapi_data(req, hal_req);

	cfm = slsi_mlme_req_cfm(sdev, dev, req, MLME_NAN_START_CFM);
	if (!cfm)
		return -EIO;

	if (fapi_get_u16(cfm, u.mlme_nan_start_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_NET_ERR(dev, "MLME_NAN_START_CFM(result:0x%04x) ERROR\n",
			     fapi_get_u16(cfm, u.mlme_host_state_cfm.result_code));
		r = -EINVAL;
	}

	slsi_kfree_skb(cfm);
	return r;
}

static void slsi_mlme_nan_append_tlv(struct sk_buff *req, __le16 tlv_t, __le16 tlv_l, u8 *tlv_v, u8 **header_ptr,
				     u8 header_ie_generic_len, u8 **end_ptr)
{
	u8 tmp_buf[255 + 4];
	u8 *tmp_buf_pos;
	int tmp_buf_len, len1, ip_ie_len;

	memcpy(tmp_buf, &tlv_t, 2);
	memcpy(tmp_buf + 2, &tlv_l, 2);
	memcpy(tmp_buf + 4, tlv_v, tlv_l);
	tmp_buf_len = 4 + tlv_l;
	ip_ie_len = *end_ptr - *header_ptr - 2;
	tmp_buf_pos = tmp_buf;

	while (tmp_buf_len + ip_ie_len > 255) {
		len1 = 255 - ip_ie_len;
		fapi_append_data(req, tmp_buf_pos, len1);
		(*header_ptr)[1] = 255;
		tmp_buf_len -= len1;
		tmp_buf_pos += len1;
		ip_ie_len = 0;
		if (tmp_buf_len) {
			*header_ptr = fapi_append_data(req, *header_ptr, header_ie_generic_len);
			*end_ptr = *header_ptr + header_ie_generic_len;
		} else {
			*end_ptr = *header_ptr + header_ie_generic_len + 255;
		}
	}
	if (tmp_buf_len) {
		fapi_append_data(req, tmp_buf, tmp_buf_len);
		*end_ptr += tmp_buf_len;
	}
}

static void slsi_mlme_nan_publish_fapi_data(struct sk_buff *req, struct slsi_hal_nan_publish_req *hal_req)
{
	u8  nan_publish_fields_header[] = {0xdd, 0x00, 0x00, 0x16, 0x32, 0x0b, 0x02};
	u8 *header_ptr, *end_ptr;
	__le16 le16val;

	header_ptr = fapi_append_data(req, nan_publish_fields_header, sizeof(nan_publish_fields_header));
	le16val = cpu_to_le16(hal_req->ttl);
	fapi_append_data(req, (u8 *)&le16val, 2);
	le16val = cpu_to_le16(hal_req->period);
	fapi_append_data(req, (u8 *)&le16val, 2);
	fapi_append_data(req, &hal_req->publish_type, 1);
	fapi_append_data(req, &hal_req->tx_type, 1);
	fapi_append_data(req, &hal_req->publish_count, 1);
	fapi_append_data(req, &hal_req->publish_match_indicator, 1);
	fapi_append_data(req, &hal_req->rssi_threshold_flag, 1);
	end_ptr = fapi_append_data(req, &hal_req->connmap, 1);
	end_ptr += 1;

	if (hal_req->service_name_len)
		slsi_mlme_nan_append_tlv(req, cpu_to_le16 (SLSI_FAPI_NAN_SERVICE_NAME),
					 cpu_to_le16 (hal_req->service_name_len), hal_req->service_name, &header_ptr,
					 sizeof(nan_publish_fields_header), &end_ptr);

	if (hal_req->service_specific_info_len)
		slsi_mlme_nan_append_tlv(req, cpu_to_le16 (SLSI_FAPI_NAN_SERVICE_SPECIFIC_INFO),
					 cpu_to_le16 (hal_req->service_specific_info_len),
					 hal_req->service_specific_info, &header_ptr,
					 sizeof(nan_publish_fields_header), &end_ptr);

	if (hal_req->rx_match_filter_len)
		slsi_mlme_nan_append_tlv(req, cpu_to_le16 (SLSI_FAPI_NAN_RX_MATCH_FILTER),
					 cpu_to_le16 (hal_req->rx_match_filter_len), hal_req->rx_match_filter,
					 &header_ptr, sizeof(nan_publish_fields_header), &end_ptr);

	if (hal_req->tx_match_filter_len)
		slsi_mlme_nan_append_tlv(req, cpu_to_le16 (SLSI_FAPI_NAN_TX_MATCH_FILTER),
					 cpu_to_le16 (hal_req->tx_match_filter_len), hal_req->tx_match_filter,
					 &header_ptr, sizeof(nan_publish_fields_header), &end_ptr);

	/* update len */
	header_ptr[1] = end_ptr - header_ptr - 2;
}

int slsi_mlme_nan_publish(struct slsi_dev *sdev, struct net_device *dev, struct slsi_hal_nan_publish_req *hal_req,
			  u16 publish_id)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *req;
	struct sk_buff    *cfm;
	int               r = 0;
	u16               nan_sdf_flags = 0;

	SLSI_NET_DBG3(dev, SLSI_MLME, "\n");
	if (hal_req) {
		/* max possible length for publish attributes : 8*255 */
		req = fapi_alloc(mlme_nan_publish_req, MLME_NAN_PUBLISH_REQ, ndev_vif->ifnum, 8 * 255);
		if (!req) {
			SLSI_NET_ERR(dev, "fapi alloc failure\n");
			return -ENOMEM;
		}

		/* Set/Enable corresponding bits to disable any indications
		 * that follow a publish.
		 * BIT0 - Disable publish termination indication.
		 * BIT1 - Disable match expired indication.
		 * BIT2 - Disable followUp indication received (OTA).
		 */
		if (hal_req->recv_indication_cfg & BIT(0))
			nan_sdf_flags |= FAPI_NANSDFCONTROL_PUBLISH_END_EVENT;
		if (hal_req->recv_indication_cfg & BIT(1))
			nan_sdf_flags |= FAPI_NANSDFCONTROL_MATCH_EXPIRED_EVENT;
		if (hal_req->recv_indication_cfg & BIT(2))
			nan_sdf_flags |= FAPI_NANSDFCONTROL_RECEIVED_FOLLOWUP_EVENT;
	} else {
		req = fapi_alloc(mlme_nan_publish_req, MLME_NAN_PUBLISH_REQ, ndev_vif->ifnum, 0);
		if (!req) {
			SLSI_NET_ERR(dev, "fapi alloc failure\n");
			return -ENOMEM;
		}
	}

	fapi_set_u16(req, u.mlme_nan_publish_req.publish_id, publish_id);
	fapi_set_u16(req, u.mlme_nan_publish_req.nan_sdf_flags, nan_sdf_flags);

	if (hal_req)
		slsi_mlme_nan_publish_fapi_data(req, hal_req);

	cfm = slsi_mlme_req_cfm(sdev, dev, req, MLME_NAN_PUBLISH_CFM);
	if (!cfm)
		return -EIO;

	if (fapi_get_u16(cfm, u.mlme_nan_publish_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_NET_ERR(dev, "MLME_NAN_PUBLISH_CFM(result:0x%04x) ERROR\n",
			     fapi_get_u16(cfm, u.mlme_host_state_cfm.result_code));
		r = -EINVAL;
	}

	if (hal_req && !r)
		ndev_vif->nan.publish_id_map |= BIT(publish_id);
	else
		ndev_vif->nan.publish_id_map &= ~BIT(publish_id);
	slsi_kfree_skb(cfm);
	return r;
}

static void slsi_mlme_nan_subscribe_fapi_data(struct sk_buff *req, struct slsi_hal_nan_subscribe_req *hal_req)
{
	u8  nan_subscribe_fields_header[] = {0xdd, 0x00, 0x00, 0x16, 0x32, 0x0b, 0x03};
	u8 *header_ptr, *end_ptr;
	__le16 le16val;

	header_ptr = fapi_append_data(req, nan_subscribe_fields_header, sizeof(nan_subscribe_fields_header));
	le16val = cpu_to_le16(hal_req->ttl);
	fapi_append_data(req, (u8 *)&le16val, 2);
	le16val = cpu_to_le16(hal_req->period);
	fapi_append_data(req, (u8 *)&le16val, 2);
	fapi_append_data(req, &hal_req->subscribe_type, 1);
	fapi_append_data(req, &hal_req->service_response_filter, 1);
	fapi_append_data(req, &hal_req->service_response_include, 1);
	fapi_append_data(req, &hal_req->use_service_response_filter, 1);
	fapi_append_data(req, &hal_req->ssi_required_for_match_indication, 1);
	fapi_append_data(req, &hal_req->subscribe_match_indicator, 1);
	fapi_append_data(req, &hal_req->subscribe_count, 1);
	fapi_append_data(req, &hal_req->rssi_threshold_flag, 1);
	end_ptr = fapi_append_data(req, &hal_req->connmap, 1);
	end_ptr += 1;

	if (hal_req->service_name_len)
		slsi_mlme_nan_append_tlv(req, cpu_to_le16 (SLSI_FAPI_NAN_SERVICE_NAME),
					 cpu_to_le16 (hal_req->service_name_len), hal_req->service_name, &header_ptr,
					 sizeof(nan_subscribe_fields_header), &end_ptr);

	if (hal_req->service_specific_info_len)
		slsi_mlme_nan_append_tlv(req, cpu_to_le16 (SLSI_FAPI_NAN_SERVICE_SPECIFIC_INFO),
					 cpu_to_le16 (hal_req->service_specific_info_len),
					 hal_req->service_specific_info, &header_ptr,
					 sizeof(nan_subscribe_fields_header), &end_ptr);

	if (hal_req->rx_match_filter_len)
		slsi_mlme_nan_append_tlv(req, cpu_to_le16 (SLSI_FAPI_NAN_RX_MATCH_FILTER),
					 cpu_to_le16 (hal_req->rx_match_filter_len), hal_req->rx_match_filter,
					 &header_ptr, sizeof(nan_subscribe_fields_header), &end_ptr);

	if (hal_req->tx_match_filter_len)
		slsi_mlme_nan_append_tlv(req, cpu_to_le16 (SLSI_FAPI_NAN_TX_MATCH_FILTER),
					 cpu_to_le16 (hal_req->tx_match_filter_len), hal_req->tx_match_filter,
					 &header_ptr, sizeof(nan_subscribe_fields_header), &end_ptr);

	/* update len */
	header_ptr[1] = end_ptr - header_ptr - 2;
}

int slsi_mlme_nan_subscribe(struct slsi_dev *sdev, struct net_device *dev, struct slsi_hal_nan_subscribe_req *hal_req,
			    u16 subscribe_id)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *req;
	struct sk_buff    *cfm;
	int               r = 0;
	u16               nan_sdf_flags = 0;

	SLSI_NET_DBG3(dev, SLSI_MLME, "\n");
	if (hal_req) {
		/*max possible length for publish attributes: 8*255 */
		req = fapi_alloc(mlme_nan_subscribe_req, MLME_NAN_SUBSCRIBE_REQ, ndev_vif->ifnum, 8 * 255);
		if (!req) {
			SLSI_NET_ERR(dev, "fapi alloc failure\n");
			return -ENOMEM;
		}
		/* Set/Enable corresponding bits to disable
		 * indications that follow a subscribe.
		 * BIT0 - Disable subscribe termination indication.
		 * BIT1 - Disable match expired indication.
		 * BIT2 - Disable followUp indication received (OTA).
		 */
		if (hal_req->recv_indication_cfg & BIT(0))
			nan_sdf_flags |= FAPI_NANSDFCONTROL_SUBSCRIBE_END_EVENT;
		if (hal_req->recv_indication_cfg & BIT(1))
			nan_sdf_flags |= FAPI_NANSDFCONTROL_MATCH_EXPIRED_EVENT;
		if (hal_req->recv_indication_cfg & BIT(2))
			nan_sdf_flags |= FAPI_NANSDFCONTROL_RECEIVED_FOLLOWUP_EVENT;
	} else {
		req = fapi_alloc(mlme_nan_subscribe_req, MLME_NAN_SUBSCRIBE_REQ, ndev_vif->ifnum, 0);
		if (!req) {
			SLSI_NET_ERR(dev, "fapi alloc failure\n");
			return -ENOMEM;
		}
	}

	fapi_set_u16(req, u.mlme_nan_subscribe_req.subscribe_id, subscribe_id);
	fapi_set_u16(req, u.mlme_nan_subscribe_req.nan_sdf_flags, nan_sdf_flags);

	if (hal_req)
		slsi_mlme_nan_subscribe_fapi_data(req, hal_req);

	cfm = slsi_mlme_req_cfm(sdev, dev, req, MLME_NAN_SUBSCRIBE_CFM);
	if (!cfm)
		return -EIO;

	if (fapi_get_u16(cfm, u.mlme_nan_subscribe_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_NET_ERR(dev, "MLME_NAN_SUBSCRIBE_CFM(res:0x%04x)\n",
			     fapi_get_u16(cfm, u.mlme_host_state_cfm.result_code));
		r = -EINVAL;
	}

	if (hal_req && !r)
		ndev_vif->nan.subscribe_id_map |= BIT(subscribe_id);
	else
		ndev_vif->nan.subscribe_id_map &= ~BIT(subscribe_id);
	slsi_kfree_skb(cfm);
	return r;
}

static void slsi_mlme_nan_followup_fapi_data(struct sk_buff *req, struct slsi_hal_nan_transmit_followup_req *hal_req)
{
	u8  nan_followup_fields_header[] = {0xdd, 0x00, 0x00, 0x16, 0x32, 0x0b, 0x05};
	u8 *header_ptr, *end_ptr;

	header_ptr = fapi_append_data(req, nan_followup_fields_header, sizeof(nan_followup_fields_header));
	fapi_append_data(req, hal_req->addr, ETH_ALEN);
	fapi_append_data(req, &hal_req->priority, 1);
	end_ptr = fapi_append_data(req, &hal_req->dw_or_faw, 1);
	end_ptr += 1;

	if (hal_req->service_specific_info_len)
		slsi_mlme_nan_append_tlv(req, cpu_to_le16 (SLSI_FAPI_NAN_SERVICE_SPECIFIC_INFO),
					 cpu_to_le16 (hal_req->service_specific_info_len),
					 hal_req->service_specific_info, &header_ptr,
					 sizeof(nan_followup_fields_header), &end_ptr);

	/* update len */
	header_ptr[1] = end_ptr - header_ptr - 2;
}

int slsi_mlme_nan_tx_followup(struct slsi_dev *sdev, struct net_device *dev,
			      struct slsi_hal_nan_transmit_followup_req *hal_req)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *req;
	struct sk_buff    *cfm;
	int               r = 0;
	u16               nan_sdf_flags = 0;

	SLSI_NET_DBG3(dev, SLSI_MLME, "\n");

	/* max possible length for publish attributes: 5*255 */
	req = fapi_alloc(mlme_nan_followup_req, MLME_NAN_FOLLOWUP_REQ, ndev_vif->ifnum, 5 * 255);
	if (!req) {
		SLSI_NET_ERR(dev, "fapi alloc failure\n");
		return -ENOMEM;
	}

	/* Set/Enable corresponding bits to disable responses after followUp.
	 * BIT0 - Disable followUp response from FW.
	 */
	if (hal_req->recv_indication_cfg & BIT(0))
		nan_sdf_flags |= FAPI_NANSDFCONTROL_DISABLE_RESPONSES_AFTER_FOLLOWUP;

	fapi_set_u16(req, u.mlme_nan_followup_req.requestor_instance_id, hal_req->publish_subscribe_id);
	fapi_set_u16(req, u.mlme_nan_followup_req.requestor_instance_id, hal_req->requestor_instance_id);
	fapi_set_u16(req, u.mlme_nan_subscribe_req.nan_sdf_flags, nan_sdf_flags);

	slsi_mlme_nan_followup_fapi_data(req, hal_req);

	cfm = slsi_mlme_req_cfm(sdev, dev, req, MLME_NAN_FOLLOWUP_CFM);
	if (!cfm)
		return -EIO;

	if (fapi_get_u16(cfm, u.mlme_nan_followup_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_NET_ERR(dev, "MLME_NAN_FOLLOWUP_CFM(res:0x%04x)\n",
			     fapi_get_u16(cfm, u.mlme_host_state_cfm.result_code));
		r = -EINVAL;
	}

	slsi_kfree_skb(cfm);
	return r;
}

static void slsi_mlme_nan_config_fapi_data(struct sk_buff *req, struct slsi_hal_nan_config_req *hal_req)
{
	u8  nan_config_fields_header[] = {0xdd, 0x00, 0x00, 0x16, 0x32, 0x0b, 0x01};
	u8 *header_ptr;
	u16 attribute;
	u8  scan_param[] = {0, 0, 0};
	int len = 0;

	header_ptr = fapi_append_data(req, nan_config_fields_header, sizeof(nan_config_fields_header));
	attribute = SLSI_FAPI_NAN_CONFIG_PARAM_MASTER_PREFERENCE;
	SLSI_FAPI_NAN_ATTRIBUTE_PUT_U16(req, attribute, hal_req->master_pref);
	len += sizeof(nan_config_fields_header) + 5;

	if (hal_req->config_sid_beacon) {
		attribute = SLSI_FAPI_NAN_CONFIG_PARAM_SID_BEACON;
		SLSI_FAPI_NAN_ATTRIBUTE_PUT_U8(req, attribute, hal_req->sid_beacon);
		len += 5;
	}

	if (hal_req->config_rssi_proximity) {
		attribute = SLSI_FAPI_NAN_CONFIG_PARAM_2_4_RSSI_PROXIMITY;
		SLSI_FAPI_NAN_ATTRIBUTE_PUT_U8(req, attribute, hal_req->rssi_proximity);
		len += 5;
	}

	if (hal_req->config_5g_rssi_close_proximity) {
		attribute = SLSI_FAPI_NAN_CONFIG_PARAM_5_RSSI_PROXIMITY;
		SLSI_FAPI_NAN_ATTRIBUTE_PUT_U8(req, attribute, hal_req->rssi_close_proximity_5g_val);
		len += 5;
	}

	if (hal_req->config_rssi_window_size) {
		attribute = SLSI_FAPI_NAN_CONFIG_PARAM_RSSI_WINDOW_SIZE;
		SLSI_FAPI_NAN_ATTRIBUTE_PUT_U8(req, attribute, hal_req->rssi_window_size_val);
		len += 5;
	}

	if (hal_req->config_scan_params) {
		attribute = SLSI_FAPI_NAN_CONFIG_PARAM_SCAN_PARAMETER_2_4;
		scan_param[0] = hal_req->scan_params_val.dwell_time[0];
		scan_param[1] = hal_req->scan_params_val.scan_period[0] & 0x00FF;
		scan_param[2] = (hal_req->scan_params_val.scan_period[0] & 0xFF00) >> 8;
		SLSI_FAPI_NAN_ATTRIBUTE_PUT_DATA(req, attribute, scan_param, 3);
		attribute = SLSI_FAPI_NAN_CONFIG_PARAM_SCAN_PARAMETER_5;
		scan_param[0] = hal_req->scan_params_val.dwell_time[1];
		scan_param[1] = hal_req->scan_params_val.scan_period[1] & 0x00FF;
		scan_param[2] = (hal_req->scan_params_val.scan_period[1] & 0xFF00) >> 8;
		SLSI_FAPI_NAN_ATTRIBUTE_PUT_DATA(req, attribute, scan_param, 3);
		len += 7 * 2;
	}

	if (hal_req->config_conn_capability) {
		u8 con_cap = 0;

		if (hal_req->conn_capability_val.is_wfd_supported)
			con_cap |= BIT(0);
		if (hal_req->conn_capability_val.is_wfds_supported)
			con_cap |= BIT(1);
		if (hal_req->conn_capability_val.is_tdls_supported)
			con_cap |= BIT(2);
		if (hal_req->conn_capability_val.wlan_infra_field)
			con_cap |= BIT(3);
		attribute = SLSI_FAPI_NAN_CONFIG_PARAM_CONNECTION_CAPAB;
		SLSI_FAPI_NAN_ATTRIBUTE_PUT_U8(req, attribute, hal_req->rssi_window_size_val);
		len += 5;
	}
	/* update len */
	header_ptr[1] = len - 2;
}

int slsi_mlme_nan_set_config(struct slsi_dev *sdev, struct net_device *dev, struct slsi_hal_nan_config_req *hal_req)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *req;
	struct sk_buff    *cfm;
	int               r = 0;
	u16               nan_oper_ctrl = 0;

	SLSI_NET_DBG3(dev, SLSI_MLME, "\n");
	/* max possible length for publish attributes 5*255 */
	req = fapi_alloc(mlme_nan_config_req, MLME_NAN_CONFIG_REQ, ndev_vif->ifnum, 5 * 255);
	if (!req) {
		SLSI_NET_ERR(dev, "fapi alloc failure\n");
		return -ENOMEM;
	}

	if (hal_req->config_cluster_attribute_val)
		nan_oper_ctrl |= FAPI_NANOPERATIONCONTROL_CLUSTER_SDF;
	nan_oper_ctrl |= FAPI_NANOPERATIONCONTROL_MAC_ADDRESS_EVENT | FAPI_NANOPERATIONCONTROL_START_CLUSTER_EVENT |
			FAPI_NANOPERATIONCONTROL_JOINED_CLUSTER_EVENT;
	fapi_set_u16(req, u.mlme_nan_config_req.nan_operation_control_flags, nan_oper_ctrl);

	slsi_mlme_nan_config_fapi_data(req, hal_req);

	cfm = slsi_mlme_req_cfm(sdev, dev, req, MLME_NAN_FOLLOWUP_CFM);
	if (!cfm)
		return -EIO;

	if (fapi_get_u16(cfm, u.mlme_nan_followup_cfm.result_code) != FAPI_RESULTCODE_SUCCESS) {
		SLSI_NET_ERR(dev, "MLME_NAN_FOLLOWUP_CFM(res:0x%04x)\n",
			     fapi_get_u16(cfm, u.mlme_host_state_cfm.result_code));
		r = -EINVAL;
	}

	slsi_kfree_skb(cfm);
	return r;
}
