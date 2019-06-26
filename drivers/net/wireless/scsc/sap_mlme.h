
/****************************************************************************
 *
 * Copyright (c) 2014 - 2016 Samsung Electronics Co., Ltd. All rights reserved
 *
 ****************************************************************************/

#ifndef __SAP_MLME_H__
#define __SAP_MLME_H__

int sap_mlme_init(void);
int sap_mlme_deinit(void);

/* MLME signal handlers in rx.c */
void slsi_rx_scan_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb);
void slsi_rx_scan_done_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb);
void slsi_rx_channel_switched_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb);
void slsi_rx_connect_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb);
void slsi_rx_connected_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb);
void slsi_rx_received_frame_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb);
void slsi_rx_disconnect_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb);
void slsi_rx_disconnected_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb);
void slsi_rx_procedure_started_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb);
void slsi_rx_frame_transmission_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb);
void slsi_rx_roamed_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb);
void slsi_rx_roam_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb);
void slsi_rx_mic_failure_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb);
void slsi_rx_reassoc_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb);
void slsi_tdls_peer_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb);
void slsi_rx_listen_end_ind(struct net_device *dev, struct sk_buff *skb);
void slsi_rx_blockack_ind(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb);

#endif
