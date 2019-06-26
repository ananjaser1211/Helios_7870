/****************************************************************************
 *
 * Copyright (c) 2012 - 2016 Samsung Electronics Co., Ltd. All rights reserved
 *
 ****************************************************************************/

#ifndef __SLSI_CFG80211_OPS_H__
#define __SLSI_CFG80211_OPS_H__

#include <net/cfg80211.h>

struct slsi_dev;

#define SDEV_FROM_WIPHY(wiphy) ((struct slsi_dev *)(wiphy)->priv)
#define WLAN_CIPHER_SUITE_PMK 0x00904C00

#define SLSI_WPS_REQUEST_TYPE_POS               15
#define SLSI_WPS_REQUEST_TYPE_ENROLEE_INFO_ONLY 0x00
#define SLSI_WPS_OUI_PATTERN                    0x04F25000
#define SLSI_P2P_OUI_PATTERN                    0x099a6f50
#define SLSI_VENDOR_OUI_AND_TYPE_LEN            4

struct slsi_dev *slsi_cfg80211_new(struct device *dev);
int slsi_cfg80211_register(struct slsi_dev *sdev);
void slsi_cfg80211_unregister(struct slsi_dev *sdev);
void slsi_cfg80211_free(struct slsi_dev *sdev);
void slsi_cfg80211_update_wiphy(struct slsi_dev *sdev);
#endif /*__SLSI_CFG80211_OPS_H__*/
