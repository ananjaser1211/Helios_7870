/******************************************************************************
 *
 * Copyright (c) 2012 - 2018 Samsung Electronics Co., Ltd. All rights reserved
 *
 *****************************************************************************/

#include <linux/delay.h>
#include <linux/firmware.h>
#include <scsc/kic/slsi_kic_lib.h>

#ifdef CONFIG_ARCH_EXYNOS
#include <linux/soc/samsung/exynos-soc.h>
#endif

#include <scsc/scsc_mx.h>
#include <scsc/scsc_release.h>
#include "mgt.h"
#include "debug.h"
#include "mlme.h"
#include "netif.h"
#include "utils.h"
#include "udi.h"
#include "log_clients.h"
#ifdef SLSI_TEST_DEV
#include "unittest.h"
#endif
#include "hip.h"
#ifdef CONFIG_SCSC_LOG_COLLECTION
#include <scsc/scsc_log_collector.h>
#endif

#include "procfs.h"
#include "mib.h"
#include "unifiio.h"
#include "ba.h"
#include "scsc_wifi_fcq.h"
#include "cac.h"
#include "cfg80211_ops.h"
#include "nl80211_vendor.h"
#ifdef CONFIG_SCSC_WLBTD
#include "scsc_wlbtd.h"
#endif

#define CSR_WIFI_SME_MIB2_HOST_PSID_MASK    0x8000
#define SLSI_DEFAULT_HW_MAC_ADDR    "\x00\x00\x0F\x11\x22\x33"
#define MX_WLAN_FILE_PATH_LEN_MAX (128)
#define SLSI_MIB_REG_RULES_MAX (50)
#define SLSI_MIB_MAX_CLIENT (10)
#define SLSI_REG_PARAM_START_INDEX (1)

static char *mib_file_t = "wlan_t.hcf";
module_param(mib_file_t, charp, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(mib_file_t, "mib data filename");

static char *mib_file2_t = "wlan_t_sw.hcf";
module_param(mib_file2_t, charp, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(mib_file2_t, "mib data filename");

/* MAC address override. If set to FF's, then
 * the address is taken from config files or
 * default derived from HW ID.
 */
static char *mac_addr_override = "ff:ff:ff:ff:ff:ff";
module_param_named(mac_addr, mac_addr_override, charp, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(mac_addr_override, "WLAN MAC address override");

static int slsi_mib_open_file(struct slsi_dev *sdev, struct slsi_dev_mib_info *mib_info, const struct firmware **fw);
static int slsi_mib_close_file(struct slsi_dev *sdev, const struct firmware *e);
static int slsi_mib_download_file(struct slsi_dev *sdev, struct slsi_dev_mib_info *mib_info);
static int slsi_country_to_index(struct slsi_802_11d_reg_domain *domain_info, const char *alpha2);
static int slsi_mib_initial_get(struct slsi_dev *sdev);
static int slsi_hanged_event_count;
#ifdef CONFIG_SCSC_WLAN_WIFI_SHARING
#define SLSI_MAX_CHAN_5G_BAND 25
#define SLSI_2G_CHANNEL_ONE 2412
static int slsi_5ghz_all_channels[] = {5180, 5200, 5220, 5240, 5260, 5280, 5300, 5320, 5500, 5520,
				       5540, 5560, 5580, 5600, 5620, 5640, 5660, 5680, 5700, 5720,
				       5745, 5765, 5785, 5805, 5825 };
#endif

/* MAC address override stored in /sys/wifi/mac_addr */
static ssize_t sysfs_show_macaddr(struct kobject *kobj, struct kobj_attribute *attr,
				  char *buf);
static ssize_t sysfs_store_macaddr(struct kobject *kobj, struct kobj_attribute *attr,
				   const char *buf, size_t count);

static char sysfs_mac_override[] = "ff:ff:ff:ff:ff:ff";
static struct kobject *macaddr_kobj_ref;
static struct kobj_attribute mac_attr = __ATTR(mac_addr, 0660, sysfs_show_macaddr, sysfs_store_macaddr);

/* Retrieve mac address in sysfs global */
static ssize_t sysfs_show_macaddr(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  char *buf)
{
	return snprintf(buf, sizeof(sysfs_mac_override), "%s", sysfs_mac_override);
}

/* Update mac address in sysfs global */
static ssize_t sysfs_store_macaddr(struct kobject *kobj,
				   struct kobj_attribute *attr,
				   const char *buf,
				   size_t count)
{
	int r;

	SLSI_INFO_NODEV("Override WLAN MAC address %s\n", buf);

	/* size of macaddr string */
	r = sscanf(buf, "%18s", (char *)&sysfs_mac_override);

	return (r > 0) ? count : 0;
}

/* Register sysfs mac address override */
void slsi_create_sysfs_macaddr(void)
{
	int r;

	/* Create sysfs directory /sys/wifi */
	macaddr_kobj_ref = kobject_create_and_add("wifi", NULL);

	/* Create sysfs file /sys/wifi/mac_addr */
	r = sysfs_create_file(macaddr_kobj_ref, &mac_attr.attr);
	if (r) {
		/* Failed, so clean up dir */
		pr_err("Can't create /sys/wifi/mac_addr\n");

		kobject_put(macaddr_kobj_ref);
		macaddr_kobj_ref = NULL;
		return;
	}
}

/* Unregister sysfs mac address override */
void slsi_destroy_sysfs_macaddr(void)
{
	if (!macaddr_kobj_ref)
		return;

	/* Destroy /sys/wifi/mac_addr file */
	sysfs_remove_file(kernel_kobj, &mac_attr.attr);

	/* Destroy /sys/wifi virtual dir */
	kobject_put(macaddr_kobj_ref);
}

void slsi_purge_scan_results_locked(struct netdev_vif *ndev_vif, u16 scan_id)
{
	struct slsi_scan_result *scan_result;
	struct slsi_scan_result *prev = NULL;

	scan_result = ndev_vif->scan[scan_id].scan_results;
	while (scan_result) {
		slsi_kfree_skb(scan_result->beacon);
		slsi_kfree_skb(scan_result->probe_resp);
		prev = scan_result;
		scan_result = scan_result->next;
		kfree(prev);
	}
	ndev_vif->scan[scan_id].scan_results = NULL;
}

void slsi_purge_scan_results(struct netdev_vif *ndev_vif, u16 scan_id)
{
	SLSI_MUTEX_LOCK(ndev_vif->scan_result_mutex);
	slsi_purge_scan_results_locked(ndev_vif, scan_id);
	SLSI_MUTEX_UNLOCK(ndev_vif->scan_result_mutex);
}

struct sk_buff *slsi_dequeue_cached_scan_result(struct slsi_scan *scan, int *count)
{
	struct sk_buff *skb = NULL;
	struct slsi_scan_result *scan_result = scan->scan_results;

	if (scan_result) {
		if (scan_result->beacon) {
			skb = scan_result->beacon;
			scan_result->beacon = NULL;
		} else if (scan_result->probe_resp) {
			skb = scan_result->probe_resp;
			scan_result->probe_resp = NULL;
		} else {
			SLSI_ERR_NODEV("Scan entry with no beacon /probe resp!!\n");
		}

		/*If beacon and probe response indicated above , remove the entry*/
		if (!scan_result->beacon  && !scan_result->probe_resp) {
			scan->scan_results = scan_result->next;
			kfree(scan_result);
			if (count)
				(*count)++;
		}
	}
	return skb;
}

void slsi_get_hw_mac_address(struct slsi_dev *sdev, u8 *addr)
{
#ifndef SLSI_TEST_DEV
	const struct firmware *e = NULL;
	int                   i;
	u32                   u[ETH_ALEN];
	char                  path_name[MX_WLAN_FILE_PATH_LEN_MAX];
	int                   r;
	bool		      valid = false;

	/* Module parameter override */
	r = sscanf(mac_addr_override, "%02X:%02X:%02X:%02X:%02X:%02X", &u[0], &u[1], &u[2], &u[3], &u[4], &u[5]);
	if (r != ETH_ALEN) {
		SLSI_ERR(sdev, "mac_addr modparam set, but format is incorrect (should be e.g. xx:xx:xx:xx:xx:xx)\n");
		goto mac_sysfs;
	}
	for (i = 0; i < ETH_ALEN; i++) {
		if (u[i] != 0xff)
			valid = true;
		addr[i] = u[i] & 0xff;
	}

	/* If the override is valid, use it */
	if (valid) {
		SLSI_INFO(sdev, "MAC address from modparam: %02X:%02X:%02X:%02X:%02X:%02X\n",
			  u[0], u[1], u[2], u[3], u[4], u[5]);
		return;
	}

	/* Sysfs parameter override */
mac_sysfs:
	r = sscanf(sysfs_mac_override, "%02X:%02X:%02X:%02X:%02X:%02X", &u[0], &u[1], &u[2], &u[3], &u[4], &u[5]);
	if (r != ETH_ALEN) {
		SLSI_ERR(sdev, "mac_addr in sysfs set, but format is incorrect (should be e.g. xx:xx:xx:xx:xx:xx)\n");
		goto mac_file;
	}
	for (i = 0; i < ETH_ALEN; i++) {
		if (u[i] != 0xff)
			valid = true;
		addr[i] = u[i] & 0xff;
	}

	/* If the override is valid, use it */
	if (valid) {
		SLSI_INFO(sdev, "MAC address from sysfs: %02X:%02X:%02X:%02X:%02X:%02X\n",
			  u[0], u[1], u[2], u[3], u[4], u[5]);
		return;
	}

	/* read mac.txt */
mac_file:
	if (sdev->maddr_file_name) {
		scnprintf(path_name, MX_WLAN_FILE_PATH_LEN_MAX, "wlan/%s", sdev->maddr_file_name);
		SLSI_DBG1(sdev, SLSI_INIT_DEINIT, "MAC address file : %s\n", path_name);

		r = mx140_file_request_device_conf(sdev->maxwell_core, &e, path_name);
		if (r != 0)
			goto mac_efs;

		if (!e) {
			SLSI_ERR(sdev, "mx140_file_request_device_conf() returned succes, but firmware was null\n");
			goto mac_efs;
		}
		r = sscanf(e->data, "%02X:%02X:%02X:%02X:%02X:%02X", &u[0], &u[1], &u[2], &u[3], &u[4], &u[5]);
		mx140_file_release_conf(sdev->maxwell_core, e);
		if (r != ETH_ALEN) {
			SLSI_ERR(sdev, "%s exists, but format is incorrect (should be e.g. xx:xx:xx:xx:xx:xx)\n", path_name);
			goto mac_efs;
		}
		for (i = 0; i < ETH_ALEN; i++)
			addr[i] = u[i] & 0xff;
		SLSI_INFO(sdev, "MAC address loaded from %s: %02X:%02X:%02X:%02X:%02X:%02X\n", path_name, u[0], u[1], u[2], u[3], u[4], u[5]);
		return;
	}
mac_efs:
	r = mx140_request_file(sdev->maxwell_core, CONFIG_SCSC_WLAN_MAC_ADDRESS_FILENAME, &e);
	if (r != 0)
		goto mac_default;
	if (!e) {
		SLSI_ERR(sdev, "mx140_request_file() returned succes, but firmware was null\n");
		goto mac_default;
	}
	r = sscanf(e->data, "%02X:%02X:%02X:%02X:%02X:%02X", &u[0], &u[1], &u[2], &u[3], &u[4], &u[5]);
	if (r != ETH_ALEN) {
		SLSI_ERR(sdev, "%s exists, but format is incorrect (should be e.g. xx:xx:xx:xx:xx:xx)\n", path_name);
		goto mac_default;
	}
	for (i = 0; i < ETH_ALEN; i++)
		addr[i] = u[i] & 0xff;
	SLSI_INFO(sdev, "MAC address loaded from %s: %02X:%02X:%02X:%02X:%02X:%02X\n", CONFIG_SCSC_WLAN_MAC_ADDRESS_FILENAME, u[0], u[1], u[2], u[3], u[4], u[5]);
	mx140_release_file(sdev->maxwell_core, e);
	return;

mac_default:
	/* This is safe to call, even if the struct firmware handle is NULL */
	mx140_file_release_conf(sdev->maxwell_core, e);

	SLSI_ETHER_COPY(addr, SLSI_DEFAULT_HW_MAC_ADDR);
#ifdef CONFIG_ARCH_EXYNOS
	/* Randomise MAC address from the soc uid */
	addr[3] = (exynos_soc_info.unique_id & 0xFF0000000000) >> 40;
	addr[4] = (exynos_soc_info.unique_id & 0x00FF00000000) >> 32;
	addr[5] = (exynos_soc_info.unique_id & 0x0000FF000000) >> 24;
#endif
	SLSI_DBG1(sdev, SLSI_INIT_DEINIT,
		  "MAC addr file NOT found, using default MAC ADDR: %pM\n", addr);

#else
	/* We use FIXED Mac addresses with the unittest driver */
	struct slsi_test_dev *uftestdev = (struct slsi_test_dev *)sdev->maxwell_core;

	SLSI_ETHER_COPY(addr, uftestdev->hw_addr);
	SLSI_DBG1(sdev, SLSI_INIT_DEINIT, "Test Device Address: %pM\n", addr);
#endif
}

static void write_wifi_version_info_file(struct slsi_dev *sdev)
{
	struct file *fp = NULL;

#if defined(ANDROID_VERSION) && (ANDROID_VERSION >= 90000)
	char *filepath = "/data/vendor/conn/.wifiver.info";
#else
	char *filepath = "/data/misc/conn/.wifiver.info";
#endif
	char buf[256];
	char build_id_fw[128];
	char build_id_drv[64];

	fp = filp_open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);

	if (IS_ERR(fp)) {
		SLSI_WARN(sdev, "version file wasn't found\n");
		return;
	} else if (!fp) {
		SLSI_WARN(sdev, "%s doesn't exist.\n", filepath);
		return;
	}

	mxman_get_fw_version(build_id_fw, 128);
	mxman_get_driver_version(build_id_drv, 64);

	/* WARNING:
	 * Please do not change the format of the following string
	 * as it can have fatal consequences.
	 * The framework parser for the version may depend on this
	 * exact formatting.
	 *
	 * Also beware that ANDROID_VERSION will not be defined in AOSP.
	 */
#if defined(ANDROID_VERSION) && (ANDROID_VERSION >= 90000)
	/* P-OS */
	snprintf(buf, sizeof(buf),
		 "%s\n"	/* drv_ver: already appended by mxman_get_driver_version() */
		 "f/w_ver: %s\n"
		 "hcf_ver_hw: %s\n"
		 "hcf_ver_sw: %s\n"
		 "regDom_ver: %d.%d\n",
		 build_id_drv,
		 build_id_fw,
		 sdev->mib[0].platform,
		 sdev->mib[1].platform,
		 ((sdev->reg_dom_version >> 8) & 0xFF), (sdev->reg_dom_version & 0xFF));
#else
	/* O-OS, or unknown */
	snprintf(buf, sizeof(buf),
		 "%s (f/w_ver: %s)\nregDom_ver: %d.%d\n",
		 build_id_drv,
		 build_id_fw,
		 ((sdev->reg_dom_version >> 8) & 0xFF), (sdev->reg_dom_version & 0xFF));
#endif

/* If ANDROID_VERSION is not known, avoid writing the file, as it could go to the wrong
 * location.
 */
#ifdef ANDROID_VERSION
#ifdef CONFIG_SCSC_WLBTD
	wlbtd_write_file(filepath, buf);
#else
	kernel_write(fp, buf, strlen(buf), 0);
#endif

	if (fp)
		filp_close(fp, NULL);

	SLSI_INFO(sdev, "Succeed to write firmware/host information to .wifiver.info\n");
#else
	SLSI_UNUSED_PARAMETER(filepath);
#endif
}

static void write_m_test_chip_version_file(struct slsi_dev *sdev)
{
#ifdef CONFIG_SCSC_WLBTD
	char *filepath = "/data/vendor/conn/.cid.info";
	char buf[256];

	snprintf(buf, sizeof(buf), "%s\n", SCSC_RELEASE_SOLUTION);

	wlbtd_write_file(filepath, buf);

	SLSI_WARN(sdev, "Wrote chip information to .cid.info\n");
#endif
}

#ifdef CONFIG_SCSC_LOG_COLLECTION
static int slsi_hcf_collect(struct scsc_log_collector_client *collect_client, size_t size)
{
	struct slsi_dev *sdev = (struct slsi_dev *)collect_client->prv;
	int ret = 0;
	u8 index = sdev->collect_mib.num_files;
	u8 i;
	u8 *data;

	SLSI_INFO_NODEV("Collecting WLAN HCF\n");

	if (!sdev->collect_mib.enabled)
		SLSI_INFO_NODEV("Collection not enabled\n");

	spin_lock(&sdev->collect_mib.in_collection);
	ret = scsc_log_collector_write(&index, sizeof(char), 1);
	if (ret) {
		spin_unlock(&sdev->collect_mib.in_collection);
		return ret;
	}

	for (i = 0; i < index; i++) {
		SLSI_INFO_NODEV("Collecting WLAN HCF. File %s\n", sdev->collect_mib.file[i].file_name);
		/* Write file name */
		ret = scsc_log_collector_write((char *)&sdev->collect_mib.file[i].file_name, 32, 1);
		if (ret) {
			spin_unlock(&sdev->collect_mib.in_collection);
			return ret;
		}
		/* Write file len */
		ret = scsc_log_collector_write((char *)&sdev->collect_mib.file[i].len, sizeof(u16), 1);
		if (ret) {
			spin_unlock(&sdev->collect_mib.in_collection);
			return ret;
		}
		/* Write data */
		data = sdev->collect_mib.file[i].data;
		if (!data)
			continue;
		ret = scsc_log_collector_write((char *)data, sdev->collect_mib.file[i].len, 1);
		if (ret) {
			spin_unlock(&sdev->collect_mib.in_collection);
			return ret;
		}
	}
	spin_unlock(&sdev->collect_mib.in_collection);

	return ret;
}

/* Collect client registration for HCF file*/
struct scsc_log_collector_client slsi_hcf_client = {
	.name = "wlan_hcf",
	.type = SCSC_LOG_CHUNK_WLAN_HCF,
	.collect_init = NULL,
	.collect = slsi_hcf_collect,
	.collect_end = NULL,
	.prv = NULL,
};
#endif

#define SLSI_SM_WLAN_SERVICE_RECOVERY_COMPLETED_TIMEOUT 20000
int slsi_start(struct slsi_dev *sdev)
{
#ifndef CONFIG_SCSC_DOWNLOAD_FILE
	const struct firmware *fw = NULL;
#endif
	int  err = 0, r;
	int i;
	char alpha2[3];
#ifdef CONFIG_SCSC_WLAN_AP_INFO_FILE
	u32 offset = 0;
	struct file *fp = NULL;
#if defined(ANDROID_VERSION) && ANDROID_VERSION >= 90000
 	char *filepath = "/data/vendor/conn/.softap.info";
#else
	char *filepath = "/data/misc/conn/.softap.info";
#endif
	char buf[512];
#endif

	if (WARN_ON(!sdev))
		return -EINVAL;

	SLSI_MUTEX_LOCK(sdev->start_stop_mutex);

	slsi_wakelock(&sdev->wlan_wl);

	if (sdev->device_state != SLSI_DEVICE_STATE_STOPPED) {
		SLSI_DBG1(sdev, SLSI_INIT_DEINIT, "Device already started: device_state:%d\n", sdev->device_state);
		goto done;
	}

	if (sdev->recovery_status) {
		r = wait_for_completion_timeout(&sdev->recovery_completed,
						msecs_to_jiffies(SLSI_SM_WLAN_SERVICE_RECOVERY_COMPLETED_TIMEOUT));
		if (r == 0)
			SLSI_INFO(sdev, "recovery_completed timeout\n");

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
		reinit_completion(&sdev->recovery_completed);
#else
		/*This is how the macro is used in the older version.*/
		INIT_COMPLETION(sdev->recovery_completed);
#endif
	}

	sdev->device_state = SLSI_DEVICE_STATE_STARTING;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
	reinit_completion(&sdev->sig_wait.completion);
#else
	INIT_COMPLETION(sdev->sig_wait.completion);
#endif

	SLSI_DBG2(sdev, SLSI_INIT_DEINIT, "Step [1/2]: Start WLAN service\n");
	SLSI_EC_GOTO(slsi_sm_wlan_service_open(sdev), err, err_done);
	/**
	 * Download MIB data, if any.
	 */
	SLSI_DBG2(sdev, SLSI_INIT_DEINIT, "Step [2/3]: Send MIB configuration\n");

	sdev->local_mib.mib_hash = 0; /* Reset localmib hash value */
#ifndef SLSI_TEST_DEV
#ifdef CONFIG_SCSC_LOG_COLLECTION
	spin_lock_init(&sdev->collect_mib.in_collection);
	sdev->collect_mib.num_files = 0;
	sdev->collect_mib.enabled = false;
#endif
#ifndef CONFIG_SCSC_DOWNLOAD_FILE
	if (slsi_is_rf_test_mode_enabled()) {
		sdev->mib[0].mib_file_name = mib_file_t;
		sdev->mib[1].mib_file_name = mib_file2_t;
	}

	/* Place MIB files in shared memory */
	for (i  = 0; i < SLSI_WLAN_MAX_MIB_FILE; i++) {
		err = slsi_mib_open_file(sdev, &sdev->mib[i], &fw);

		/* Only the first file is mandatory */
		if (i == 0 && err) {
			SLSI_ERR(sdev, "mib: Mandatory wlan hcf missing. WLAN will not start (err=%d)\n", err);
			slsi_sm_wlan_service_close(sdev);
			goto err_done;
		}
	}

	err = slsi_sm_wlan_service_start(sdev);
	if (err) {
		SLSI_ERR(sdev, "slsi_sm_wlan_service_start failed: err=%d\n", err);
		slsi_mib_close_file(sdev, fw);
		slsi_sm_wlan_service_close(sdev);
		goto err_done;
	}
	slsi_mib_close_file(sdev, fw);
#else
	/* Download main MIB file via mlme_set */
	err = slsi_sm_wlan_service_start(sdev);
	if (err) {
		SLSI_ERR(sdev, "slsi_sm_wlan_service_start failed: err=%d\n", err);
		slsi_sm_wlan_service_close(sdev);
		goto err_done;
	}
	SLSI_EC_GOTO(slsi_mib_download_file(sdev, &sdev->mib), err, err_hip_started);
#endif
	/* Always try to download optional localmib file via mlme_set, ignore error */
	(void)slsi_mib_download_file(sdev, &sdev->local_mib);
#endif
	/**
	 * Download MIB data, if any.
	 * Get f/w capabilities and default configuration
	 * configure firmware
	 */
	SLSI_MUTEX_LOCK(sdev->device_config_mutex);
	sdev->device_config.rssi_boost_2g = 0;
	sdev->device_config.rssi_boost_5g = 0;
	SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);
	SLSI_DBG2(sdev, SLSI_INIT_DEINIT, "Step [3/3]: Get MIB configuration\n");
	SLSI_EC_GOTO(slsi_mib_initial_get(sdev), err, err_hip_started);
	SLSI_INFO(sdev, "=== Version info from the [MIB] ===\n");
	SLSI_INFO(sdev, "HW Version : 0x%.4X (%u)\n", sdev->chip_info_mib.chip_version, sdev->chip_info_mib.chip_version);
	SLSI_INFO(sdev, "Platform : 0x%.4X (%u)\n", sdev->plat_info_mib.plat_build, sdev->plat_info_mib.plat_build);
	slsi_cfg80211_update_wiphy(sdev);

	/* Get UnifiCountryList */
	SLSI_MUTEX_LOCK(sdev->device_config_mutex);
	sdev->device_config.host_state = FAPI_HOSTSTATE_CELLULAR_ACTIVE;
	err = slsi_read_unifi_countrylist(sdev, SLSI_PSID_UNIFI_COUNTRY_LIST);
	SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);
	if (err)
		goto err_hip_started;

	/* Get unifiDefaultCountry  */
	err = slsi_read_default_country(sdev, alpha2, 1);
	alpha2[2] = '\0';
	if (err < 0)
		goto err_hip_started;

	/* unifiDefaultCountry != world_domain */
	if (!(alpha2[0] == '0' && alpha2[1] == '0'))
		if (memcmp(sdev->device_config.domain_info.regdomain->alpha2, alpha2, 2) != 0) {
			memcpy(sdev->device_config.domain_info.regdomain->alpha2, alpha2, 2);

			/* Read the regulatory params for the country*/
			if (slsi_read_regulatory_rules(sdev, &sdev->device_config.domain_info, alpha2) == 0) {
				slsi_reset_channel_flags(sdev);
				wiphy_apply_custom_regulatory(sdev->wiphy, sdev->device_config.domain_info.regdomain);
			}
		}
	/* Do nothing for unifiDefaultCountry == world_domain */

	/* write .wifiver.info */
	write_wifi_version_info_file(sdev);

	/* write .cid.info */
	write_m_test_chip_version_file(sdev);

#ifdef CONFIG_SCSC_WLAN_AP_INFO_FILE
	/* writing .softap.info in /data/misc/conn */
	fp = filp_open(filepath, O_WRONLY | O_CREAT, 0666); /* 0666 required by framework */

	if (!fp)  {
		WARN(1, "%s doesn't exist\n", filepath);
	} else if (IS_ERR(fp)) {
		WARN(1, "%s open returned error %d\n", filepath, IS_ERR(fp));
	} else {
		offset = snprintf(buf + offset, sizeof(buf), "#softap.info\n");
		offset += snprintf(buf + offset, sizeof(buf), "DualBandConcurrency=%s\n", sdev->dualband_concurrency ? "yes" : "no");
#ifdef CONFIG_SCSC_WLAN_WIFI_SHARING
		offset += snprintf(buf + offset, sizeof(buf), "DualInterface=%s\n", "yes");
#else
		offset += snprintf(buf + offset, sizeof(buf), "DualInterface=%s\n", "no");
#endif
		offset += snprintf(buf + offset, sizeof(buf), "5G=%s\n", sdev->band_5g_supported ? "yes" : "no");
		offset += snprintf(buf + offset, sizeof(buf), "maxClient=%d\n", !sdev->softap_max_client ? SLSI_MIB_MAX_CLIENT : sdev->softap_max_client);

		/* following are always supported */
		offset += snprintf(buf + offset, sizeof(buf), "HalFn_setCountryCodeHal=yes\n");
		offset += snprintf(buf + offset, sizeof(buf), "HalFn_getValidChannels=yes\n");
#ifdef CONFIG_SCSC_WLBTD
		wlbtd_write_file(filepath, buf);
#else
		kernel_write(fp, buf, strlen(buf), 0);
#endif

		if (fp)
			filp_close(fp, NULL);

		SLSI_DBG2(sdev, SLSI_INIT_DEINIT, "Succeed to write softap information to .softap.info\n");
	}
#endif
#ifdef CONFIG_SCSC_LOG_COLLECTION
	/* Register with log collector to collect wlan hcf file */
	slsi_hcf_client.prv = sdev;
	scsc_log_collector_register_client(&slsi_hcf_client);
	sdev->collect_mib.enabled = true;
#endif
	slsi_update_supported_channels_regd_flags(sdev);
	SLSI_DBG2(sdev, SLSI_INIT_DEINIT, "---Driver started successfully---\n");
	sdev->device_state = SLSI_DEVICE_STATE_STARTED;
	memset(sdev->rtt_vif, -1, sizeof(sdev->rtt_vif));
	SLSI_MUTEX_UNLOCK(sdev->start_stop_mutex);

	slsi_kic_system_event(slsi_kic_system_event_category_initialisation,
			      slsi_kic_system_events_wifi_service_driver_started, GFP_KERNEL);

	slsi_wakeunlock(&sdev->wlan_wl);
	return err;

err_hip_started:
#ifndef SLSI_TEST_DEV
	slsi_sm_wlan_service_stop(sdev);
	slsi_hip_stop(sdev);
	slsi_sm_wlan_service_close(sdev);
#endif

err_done:
	sdev->device_state = SLSI_DEVICE_STATE_STOPPED;

done:
	slsi_wakeunlock(&sdev->wlan_wl);

	slsi_kic_system_event(slsi_kic_system_event_category_initialisation,
			      slsi_kic_system_events_wifi_on, GFP_KERNEL);

	SLSI_MUTEX_UNLOCK(sdev->start_stop_mutex);
	return err;
}

struct net_device *slsi_dynamic_interface_create(struct wiphy        *wiphy,
					     const char          *name,
					     enum nl80211_iftype type,
					     struct vif_params   *params)
{
	struct slsi_dev   *sdev = SDEV_FROM_WIPHY(wiphy);
	struct net_device *dev = NULL;
	struct netdev_vif *ndev_vif = NULL;
	int               err = -EINVAL;
	int               iface;

	SLSI_DBG1(sdev, SLSI_CFG80211, "name:%s\n", name);

	iface = slsi_netif_dynamic_iface_add(sdev, name);
	if (iface < 0)
		return NULL;

	dev = slsi_get_netdev(sdev, iface);
	if (!dev)
		return NULL;

	ndev_vif = netdev_priv(dev);

	err = slsi_netif_register_rtlnl_locked(sdev, dev);
	if (err)
		return NULL;

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	ndev_vif->iftype = type;
	dev->ieee80211_ptr->iftype = type;
	if (params)
		dev->ieee80211_ptr->use_4addr = params->use_4addr;
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);

	return dev;
}

static void slsi_stop_chip(struct slsi_dev *sdev)
{
#ifdef CONFIG_SCSC_LOG_COLLECTION
	u8 index = sdev->collect_mib.num_files;
	u8 i;
#endif
	WARN_ON(!SLSI_MUTEX_IS_LOCKED(sdev->start_stop_mutex));

	SLSI_DBG1(sdev, SLSI_INIT_DEINIT, "netdev_up_count:%d device_state:%d\n", sdev->netdev_up_count, sdev->device_state);

	if (sdev->device_state != SLSI_DEVICE_STATE_STARTED)
		return;

	/* Only shutdown on the last device going down. */
	if (sdev->netdev_up_count)
		return;

#ifdef CONFIG_SCSC_LOG_COLLECTION
	sdev->collect_mib.enabled = false;
	scsc_log_collector_unregister_client(&slsi_hcf_client);
	for (i = 0; i < index; i++)
		kfree(sdev->collect_mib.file[i].data);
#endif

	slsi_reset_channel_flags(sdev);
	slsi_regd_init(sdev);
	sdev->device_state = SLSI_DEVICE_STATE_STOPPING;

	slsi_sm_wlan_service_stop(sdev);
	sdev->device_state = SLSI_DEVICE_STATE_STOPPED;

	slsi_hip_stop(sdev);
#ifndef SLSI_TEST_DEV
	slsi_sm_wlan_service_close(sdev);
#endif
	slsi_kic_system_event(slsi_kic_system_event_category_deinitialisation,
			      slsi_kic_system_events_wifi_service_driver_stopped, GFP_KERNEL);

	/* Cleanup the Channel Config */
	SLSI_MUTEX_LOCK(sdev->device_config_mutex);
	slsi_kfree_skb(sdev->device_config.channel_config);
	sdev->device_config.channel_config = NULL;

	sdev->mlme_blocked = false;

	slsi_kic_system_event(slsi_kic_system_event_category_deinitialisation,
			      slsi_kic_system_events_wifi_off, GFP_KERNEL);

	SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);
}

void slsi_vif_cleanup(struct slsi_dev *sdev, struct net_device *dev, bool hw_available)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	int               i;

	SLSI_NET_DBG3(dev, SLSI_INIT_DEINIT, "clean VIF\n");

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	if (ndev_vif->activated) {
		netif_carrier_off(dev);
		for (i = 0; i < SLSI_ADHOC_PEER_CONNECTIONS_MAX; i++) {
			struct slsi_peer *peer = ndev_vif->peer_sta_record[i];

			if (peer && peer->valid)
				slsi_ps_port_control(sdev, dev, peer, SLSI_STA_CONN_STATE_DISCONNECTED);
		}

		if (ndev_vif->vif_type == FAPI_VIFTYPE_STATION) {
			bool already_disconnected = false;

			SLSI_DBG2(sdev, SLSI_INIT_DEINIT, "Station active: hw_available=%d\n", hw_available);
			if (hw_available) {
				if (ndev_vif->sta.sta_bss) {
					slsi_mlme_disconnect(sdev, dev, ndev_vif->sta.sta_bss->bssid, FAPI_REASONCODE_UNSPECIFIED_REASON, true);
					slsi_handle_disconnect(sdev, dev, ndev_vif->sta.sta_bss->bssid, 0);
					already_disconnected = true;
				} else {
					slsi_mlme_del_vif(sdev, dev);
				}
			}
			if (!already_disconnected) {
				SLSI_DBG2(sdev, SLSI_INIT_DEINIT, "Calling slsi_vif_deactivated\n");
				slsi_vif_deactivated(sdev, dev);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 2, 0))
				cfg80211_disconnected(dev, FAPI_REASONCODE_UNSPECIFIED_REASON, NULL, 0, false, GFP_ATOMIC);
#else
				cfg80211_disconnected(dev, FAPI_REASONCODE_UNSPECIFIED_REASON, NULL, 0, GFP_ATOMIC);
#endif
			}
		} else if (ndev_vif->vif_type == FAPI_VIFTYPE_AP) {
			SLSI_DBG2(sdev, SLSI_INIT_DEINIT, "AP active\n");
			if (hw_available) {
				struct slsi_peer *peer;
				int              j = 0;
				int              r = 0;

				while (j < SLSI_PEER_INDEX_MAX) {
					peer = ndev_vif->peer_sta_record[j];
					if (peer && peer->valid)
						slsi_ps_port_control(sdev, dev, peer, SLSI_STA_CONN_STATE_DISCONNECTED);
					++j;
				}
				r = slsi_mlme_disconnect(sdev, dev, NULL, WLAN_REASON_DEAUTH_LEAVING, true);
				if (r != 0)
					SLSI_NET_ERR(dev, "Disconnection returned with CFM failure\n");
				slsi_mlme_del_vif(sdev, dev);
			}
			SLSI_DBG2(sdev, SLSI_INIT_DEINIT, "Calling slsi_vif_deactivated\n");
			slsi_vif_deactivated(sdev, dev);

			if (ndev_vif->iftype == NL80211_IFTYPE_P2P_GO)
				SLSI_P2P_STATE_CHANGE(sdev, P2P_IDLE_NO_VIF);
		} else if (ndev_vif->vif_type == FAPI_VIFTYPE_UNSYNCHRONISED) {
			if (SLSI_IS_VIF_INDEX_WLAN(ndev_vif)) {
				slsi_wlan_unsync_vif_deactivate(sdev, dev, hw_available);
			} else {
				SLSI_DBG2(sdev, SLSI_INIT_DEINIT, "P2P active - Deactivate\n");
				slsi_p2p_vif_deactivate(sdev, dev, hw_available);
			}
		}
	}
}

void slsi_scan_cleanup(struct slsi_dev *sdev, struct net_device *dev)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	int               i;

	SLSI_NET_DBG3(dev, SLSI_INIT_DEINIT, "clean scan_data\n");

	SLSI_MUTEX_LOCK(ndev_vif->scan_mutex);
	for (i = 0; i < SLSI_SCAN_MAX; i++) {
		if (ndev_vif->scan[i].scan_req && !sdev->mlme_blocked &&
		    SLSI_IS_VIF_INDEX_P2P_GROUP(sdev, ndev_vif))
			slsi_mlme_del_scan(sdev, dev, (ndev_vif->ifnum << 8 | i), false);
		slsi_purge_scan_results(ndev_vif, i);
		if (ndev_vif->scan[i].scan_req && i == SLSI_SCAN_HW_ID)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0))
			cfg80211_scan_done(ndev_vif->scan[i].scan_req, &info);
#else
			cfg80211_scan_done(ndev_vif->scan[i].scan_req, false);
#endif

		if (ndev_vif->scan[i].sched_req && i == SLSI_SCAN_SCHED_ID)
			cfg80211_sched_scan_stopped(sdev->wiphy);

		ndev_vif->scan[i].scan_req = NULL;
		ndev_vif->scan[i].sched_req = NULL;
	}
	SLSI_MUTEX_UNLOCK(ndev_vif->scan_mutex);
}

static void slsi_stop_net_dev_locked(struct slsi_dev *sdev, struct net_device *dev, bool hw_available)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);

	SLSI_NET_DBG1(dev, SLSI_INIT_DEINIT, "Stopping netdev_up_count=%d, hw_available = %d\n", sdev->netdev_up_count, hw_available);

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(sdev->start_stop_mutex));

	if (!ndev_vif->is_available) {
		SLSI_NET_DBG1(dev, SLSI_INIT_DEINIT, "Not Available\n");
		return;
	}

	if (WARN_ON(!sdev->netdev_up_count)) {
		SLSI_NET_DBG1(dev, SLSI_INIT_DEINIT, "sdev->netdev_up_count=%d\n", sdev->netdev_up_count);
		return;
	}

	if (!hw_available)
		complete_all(&ndev_vif->sig_wait.completion);

	slsi_scan_cleanup(sdev, dev);

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	slsi_vif_cleanup(sdev, dev, hw_available);
	ndev_vif->is_available = false;
	sdev->netdev_up_count--;
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);

	slsi_stop_chip(sdev);
}

/* Called when a net device wants to go DOWN */
void slsi_stop_net_dev(struct slsi_dev *sdev, struct net_device *dev)
{
	SLSI_MUTEX_LOCK(sdev->start_stop_mutex);
	slsi_stop_net_dev_locked(sdev, dev, sdev->recovery_status ? false : true);
	SLSI_MUTEX_UNLOCK(sdev->start_stop_mutex);
}

/* Called when we get sdio_removed */
void slsi_stop(struct slsi_dev *sdev)
{
	struct net_device *dev;
	int               i;

	SLSI_MUTEX_LOCK(sdev->start_stop_mutex);
	SLSI_DBG1(sdev, SLSI_INIT_DEINIT, "netdev_up_count:%d\n", sdev->netdev_up_count);

	complete_all(&sdev->sig_wait.completion);

	SLSI_MUTEX_LOCK(sdev->netdev_add_remove_mutex);
	for (i = 1; i <= CONFIG_SCSC_WLAN_MAX_INTERFACES; i++) {
		dev = slsi_get_netdev_locked(sdev, i);
		if (dev)
			slsi_stop_net_dev_locked(sdev, sdev->netdev[i], false);
	}
	SLSI_MUTEX_UNLOCK(sdev->netdev_add_remove_mutex);

	SLSI_MUTEX_UNLOCK(sdev->start_stop_mutex);
}

/* MIB download handling */
static u8 *slsi_mib_slice(struct slsi_dev *sdev, const u8 *data, u32 length, u32 *p_parsed_len,
			  u32 *p_mib_slice_len)
{
	const u8 *p = data;
	u8       *mib_slice;
	u32      mib_slice_len = 0;

	SLSI_UNUSED_PARAMETER_NOT_DEBUG(sdev);

	if (!length)
		return NULL;

	mib_slice = kmalloc(length + 4, GFP_KERNEL);
	if (!mib_slice)
		return NULL;

	while (length >= 4) {
		u16 psid = SLSI_BUFF_LE_TO_U16(p);
		u16 pslen = (u16)(4 + SLSI_BUFF_LE_TO_U16(&p[2]));

		if (pslen & 0x1)
			pslen++;

		if (psid & CSR_WIFI_SME_MIB2_HOST_PSID_MASK) {
			/* do nothing */
		} else {
			/* SLSI_ERR (sdev, "PSID=0x%04X : FW\n", psid); */
#define CSR_WIFI_HOSTIO_MIB_SET_MAX     (1800)
			if ((mib_slice_len + pslen) > CSR_WIFI_HOSTIO_MIB_SET_MAX)
				break;
			if (pslen > length + 4) {
				SLSI_ERR(sdev, "length %u read from MIB file > space %u - corrupt file?\n", pslen, length + 4);
				mib_slice_len = 0;
				break;
			}
			memcpy(&mib_slice[mib_slice_len], p, pslen);
			mib_slice_len += pslen;
		}
		p += pslen;
		length -= pslen;
	}

	*p_mib_slice_len = mib_slice_len;
	*p_parsed_len = (p - data);

	return mib_slice;
}

/* Extract the platform name string from the HCF file */
static int slsi_mib_get_platform(struct slsi_dev_mib_info *mib_info)
{
	size_t plat_name_len;
	int pos = 0;

	/* The mib_data passed to this function should already
	 * have had its HCF header skipped.
	 *
	 * This is shoehorned into specific PSIDs to allow backward
	 * compatibility, so we must look into the HCF payload
	 * instead of the header :(
	 *
	 * The updated configcmd util guarantees that these keys
	 * will appear first:
	 *
	 * PSIDs:
	 * 0xfffe - 16 bit version ID, value 1.
	 * 0xffff - If version ID=1, holds platform name string.
	 */

	mib_info->platform[0] = '\0';

	/* Sanity - payload long enough for info? */
	if (mib_info->mib_len < 12) {
		SLSI_INFO_NODEV("HCF file too short\n");
		return -EINVAL;				/* file too short */
	}

	if (mib_info->mib_data[pos++] != 0xFE ||	/* Version ID FFFE */
	    mib_info->mib_data[pos++] != 0xFF) {
		SLSI_INFO_NODEV("No HCF version ID\n");
		return -EINVAL;				/* No version ID */
	}
	if (mib_info->mib_data[pos++] != 0x01 ||	/* Len 1, LE */
	    mib_info->mib_data[pos++] != 0x00) {
		SLSI_INFO_NODEV("Bad length\n");
		return -EINVAL;				/* Unknown length */
	}
	if (mib_info->mib_data[pos++] != 0x01 ||	/* Header ID 1, LE */
	    mib_info->mib_data[pos++] != 0x00) {
		SLSI_INFO_NODEV("Bad version ID\n");
		return -EINVAL;				/* Unknown version ID */
	}
	if (mib_info->mib_data[pos++] != 0xFF ||	/* Platform Name FFFF */
	    mib_info->mib_data[pos++] != 0xFF) {
		SLSI_INFO_NODEV("No HCF platform name\n");
		return -EINVAL;				/* No platform name */
	}

	/* Length of platform name */
	plat_name_len = mib_info->mib_data[pos++];
	plat_name_len |= (mib_info->mib_data[pos++] << 16);

	/* Sanity check */
	if (plat_name_len + pos > mib_info->mib_len || plat_name_len < 2) {
		SLSI_ERR_NODEV("Bad HCF FFFF key length %zu\n",
			       plat_name_len);
		return -EINVAL;				/* Implausible length */
	}

	/* Skip vldata header SC-506179-SP. This conveys the
	 * length of the platform string and is 2 or 3 octets long
	 * depending on the length of the string.
	 */
	{
#define SLSI_VLDATA_STRING	0xA0
#define SLSI_VLDATA_LEN		0x17

		u8 vlen_hdr = mib_info->mib_data[pos++];
		u8 vlen_len = vlen_hdr & SLSI_VLDATA_LEN; /* size of length field */

		/* Skip vlen header octet */
		plat_name_len--;

		SLSI_DBG1_NODEV(SLSI_INIT_DEINIT, "vlhdr 0x%x, len %u\n", vlen_hdr, vlen_len);

		/* Is it an octet string type? */
		if (!(vlen_hdr & SLSI_VLDATA_STRING)) {
			SLSI_ERR_NODEV("No string vlen header 0x%x\n", vlen_hdr);
			return -EINVAL;
		}

		/* Handle 1 or 2 octet length field only */
		if (vlen_len > 2) {
			SLSI_ERR_NODEV("Too long octet string header %u\n", vlen_len);
			return -EINVAL;
		}

		/* Skip over the string length field.
		 * Note we just use datalength anyway.
		 */
		pos += vlen_len;
		plat_name_len -= vlen_len;
	}

	/* Limit the platform name to space in driver and read */
	{
		size_t trunc_len = plat_name_len;

		if (trunc_len >= sizeof(mib_info->platform))
			trunc_len = sizeof(mib_info->platform) - 1;

		/* Extract platform name */
		memcpy(mib_info->platform, &mib_info->mib_data[pos], trunc_len);
		mib_info->platform[trunc_len] = '\0';

		/* Print non-truncated string in log now */
		SLSI_INFO_NODEV("MIB platform: %.*s\n", (int)plat_name_len, &mib_info->mib_data[pos]);

		SLSI_DBG1_NODEV(SLSI_INIT_DEINIT, "plat_name_len: %zu + %u\n",
				plat_name_len, (plat_name_len & 1));
	}

	/* Pad string to 16-bit boundary */
	plat_name_len += (plat_name_len & 1);
	pos += plat_name_len;

	/* Advance over the keys we read, FW doesn't need them */
	mib_info->mib_data += pos;
	mib_info->mib_len -= pos;

	SLSI_DBG1_NODEV(SLSI_INIT_DEINIT, "Skip %d octets HCF payload\n", pos);

	return 0;
}

#define MGT_HASH_SIZE_BYTES	2 /* Hash will be contained in a uint32 */
#define MGT_HASH_OFFSET		4
static int slsi_mib_open_file(struct slsi_dev *sdev, struct slsi_dev_mib_info *mib_info, const struct firmware **fw)
{
	int r = -1;
	const struct firmware *e = NULL;
	const char *mib_file_ext;
	char path_name[MX_WLAN_FILE_PATH_LEN_MAX];
	char *mib_file_name = mib_info->mib_file_name;
#ifdef CONFIG_SCSC_LOG_COLLECTION
	u8 index = sdev->collect_mib.num_files;
	u8 *data;
#endif

	if (!mib_file_name || !fw)
		return -EINVAL;

	mib_info->mib_data = NULL;
	mib_info->mib_len = 0;
	mib_info->mib_hash = 0; /* Reset mib hash value */

	SLSI_DBG2(sdev, SLSI_INIT_DEINIT, "MIB file - Name : %s\n", mib_file_name);

	/* Use MIB file compatibility mode? */
	mib_file_ext = strrchr(mib_file_name, '.');
	if (!mib_file_ext) {
		SLSI_ERR(sdev, "configuration file name '%s' invalid\n", mib_file_name);
		return -EINVAL;
	}

	/* Build MIB file path from override */
	scnprintf(path_name, MX_WLAN_FILE_PATH_LEN_MAX, "wlan/%s", mib_file_name);
	SLSI_INFO(sdev, "Path to the MIB file : %s\n", path_name);

	r = mx140_file_request_conf(sdev->maxwell_core, &e, "wlan", mib_file_name);
	if (r || (!e)) {
		SLSI_DBG2(sdev, SLSI_INIT_DEINIT, "Skip MIB download as file %s is NOT found\n", mib_file_name);
		*fw = e;
		return r;
	}

	mib_info->mib_data = (u8 *)e->data;
	mib_info->mib_len = e->size;

#ifdef CONFIG_SCSC_LOG_COLLECTION
	spin_lock(&sdev->collect_mib.in_collection);
	memset(&sdev->collect_mib.file[index].file_name, 0, 32);
	memcpy(&sdev->collect_mib.file[index].file_name, mib_file_name, 32);
	sdev->collect_mib.file[index].len = mib_info->mib_len;
	data = kmalloc(mib_info->mib_len, GFP_ATOMIC);
	if (!data) {
		spin_unlock(&sdev->collect_mib.in_collection);
		goto cont;
	}
	memcpy(data, mib_info->mib_data, mib_info->mib_len);
	sdev->collect_mib.file[index].data = data;
	sdev->collect_mib.num_files += 1;
	spin_unlock(&sdev->collect_mib.in_collection);
cont:
#endif
	/* Check MIB file header */
	if (mib_info->mib_len >= 8 &&              /* Room for header */
		/*(sdev->mib_data[6] & 0xF0) == 0x20 && */ /* WLAN subsystem */
		mib_info->mib_data[7] == 1) {      /* First file format */
		int i;

		mib_info->mib_hash = 0;
		for (i = 0; i < MGT_HASH_SIZE_BYTES; i++)
			mib_info->mib_hash = (mib_info->mib_hash << 8) | mib_info->mib_data[i + MGT_HASH_OFFSET];

		SLSI_INFO(sdev, "MIB hash: 0x%.04x\n", mib_info->mib_hash);
		/* All good - skip header and continue */
		mib_info->mib_data += 8;
		mib_info->mib_len -= 8;

		/* Extract platform name if available */
		slsi_mib_get_platform(mib_info);
	} else {
		/* Bad header */
		SLSI_ERR(sdev, "configuration file '%s' has bad header\n", mib_info->mib_file_name);
		mx140_file_release_conf(sdev->maxwell_core, e);
		return -EINVAL;
	}

	*fw = e;
	return 0;
}

static int slsi_mib_close_file(struct slsi_dev *sdev, const struct firmware *e)
{
	SLSI_DBG2(sdev, SLSI_INIT_DEINIT, "MIB close %p\n", e);

	if (!e || !sdev)
		return -EIO;

	mx140_file_release_conf(sdev->maxwell_core, e);

	return 0;
}

static int slsi_mib_download_file(struct slsi_dev *sdev, struct slsi_dev_mib_info *mib_info)
{
	int r = -1;
	const struct firmware *e = NULL;
	u8 *mib_slice;
	u32 mib_slice_len, parsed_len;

	r = slsi_mib_open_file(sdev, mib_info, &e);
	if (r)
		return r;
	/**
	 * MIB data should not be larger than CSR_WIFI_HOSTIO_MIB_SET_MAX.
	 * Slice it into smaller ones and download one by one
	 */
	while (mib_info->mib_len > 0) {
		mib_slice = slsi_mib_slice(sdev, mib_info->mib_data, mib_info->mib_len, &parsed_len, &mib_slice_len);
		if (!mib_slice)
			break;
		if (mib_slice_len == 0 || mib_slice_len > mib_info->mib_len) {
			/* Sanity check MIB parsing */
			SLSI_ERR(sdev, "slsi_mib_slice returned implausible %d\n", mib_slice_len);
			r = -EINVAL;
			kfree(mib_slice);
			break;
		}
		r = slsi_mlme_set(sdev, NULL, mib_slice, mib_slice_len);
		kfree(mib_slice);
		if (r != 0)     /* some mib can fail to be set, but continue */
			SLSI_ERR(sdev, "mlme set failed r=0x%x during downloading:'%s'\n",
				 r, mib_info->mib_file_name);

		mib_info->mib_data += parsed_len;
		mib_info->mib_len -= parsed_len;
	}

	slsi_mib_close_file(sdev, e);

	return r;
}

static int slsi_mib_initial_get(struct slsi_dev *sdev)
{
	struct slsi_mib_data mibreq = { 0, NULL };
	struct slsi_mib_data mibrsp = { 0, NULL };
	int *band = sdev->supported_5g_channels;
	int rx_len = 0;
	int r;
	int i = 0;
	int j = 0;
	int chan_start = 0;
	int chan_count = 0;
	int index = 0;
	int mib_index = 0;
	static const struct slsi_mib_get_entry get_values[] = {{ SLSI_PSID_UNIFI_CHIP_VERSION,            { 0, 0 } },
							       { SLSI_PSID_UNIFI_SUPPORTED_CHANNELS,      { 0, 0 } },
							       { SLSI_PSID_UNIFI_HT_ACTIVATED, {0, 0} },
							       { SLSI_PSID_UNIFI_VHT_ACTIVATED, {0, 0} },
							       { SLSI_PSID_UNIFI_HT_CAPABILITIES, {0, 0} },
							       { SLSI_PSID_UNIFI_VHT_CAPABILITIES, {0, 0} },
							       { SLSI_PSID_UNIFI24_G40_MHZ_CHANNELS, {0, 0} },
							       { SLSI_PSID_UNIFI_HARDWARE_PLATFORM, {0, 0} },
							       { SLSI_PSID_UNIFI_REG_DOM_VERSION, {0, 0} },
							       { SLSI_PSID_UNIFI_NAN_ENABLED, {0, 0} },
							       { SLSI_PSID_UNIFI_DEFAULT_DWELL_TIME, {0, 0} },
#ifdef CONFIG_SCSC_WLAN_WIFI_SHARING
							       { SLSI_PSID_UNIFI_WI_FI_SHARING5_GHZ_CHANNEL, {0, 0} },
#endif
#ifdef CONFIG_SCSC_WLAN_AP_INFO_FILE
							       { SLSI_PSID_UNIFI_DUAL_BAND_CONCURRENCY, {0, 0} },
							       { SLSI_PSID_UNIFI_MAX_CLIENT, {0, 0} }
#endif
							      };/*Check the mibrsp.dataLength when a new mib is added*/

	r = slsi_mib_encode_get_list(&mibreq, sizeof(get_values) / sizeof(struct slsi_mib_get_entry), get_values);
	if (r != SLSI_MIB_STATUS_SUCCESS)
		return -ENOMEM;

	mibrsp.dataLength = 154;
	mibrsp.data = kmalloc(mibrsp.dataLength, GFP_KERNEL);
	if (!mibrsp.data) {
		kfree(mibreq.data);
		return -ENOMEM;
	}

	r = slsi_mlme_get(sdev, NULL, mibreq.data, mibreq.dataLength, mibrsp.data, mibrsp.dataLength, &rx_len);
	kfree(mibreq.data);
	if (r == 0) {
		struct slsi_mib_value *values;

		mibrsp.dataLength = (u32)rx_len;

		values = slsi_mib_decode_get_list(&mibrsp, sizeof(get_values) / sizeof(struct slsi_mib_get_entry), get_values);

		if (!values) {
			kfree(mibrsp.data);
			return -EINVAL;
		}

		if (values[mib_index].type != SLSI_MIB_TYPE_NONE) {    /* CHIP_VERSION */
			SLSI_CHECK_TYPE(sdev, values[mib_index].type, SLSI_MIB_TYPE_UINT);
			sdev->chip_info_mib.chip_version = values[mib_index].u.uintValue;
		}

		if (values[++mib_index].type != SLSI_MIB_TYPE_NONE) {    /* SUPPORTED_CHANNELS */
			SLSI_CHECK_TYPE(sdev, values[mib_index].type, SLSI_MIB_TYPE_OCTET);
			if (values[mib_index].type == SLSI_MIB_TYPE_OCTET) {
				sdev->band_5g_supported = 0;
				memset(sdev->supported_2g_channels, 0, sizeof(sdev->supported_2g_channels));
				memset(sdev->supported_5g_channels, 0, sizeof(sdev->supported_5g_channels));
				for (i = 0; i < values[mib_index].u.octetValue.dataLength / 2; i++) {
					/* If any 5GHz channel is supported, update band_5g_supported */
					if ((values[mib_index].u.octetValue.data[i * 2] > 14) &&
					    (values[mib_index].u.octetValue.data[i * 2 + 1] > 0)) {
						sdev->band_5g_supported = 1;
						break;
					}
				}
				for (i = 0; i < values[mib_index].u.octetValue.dataLength; i += 2) {
					chan_start = values[mib_index].u.octetValue.data[i];
					chan_count = values[mib_index].u.octetValue.data[i + 1];
					band = sdev->supported_5g_channels;
					if (chan_start < 15) {
						index = chan_start - 1;
						band = sdev->supported_2g_channels;
					} else if (chan_start >= 36 && chan_start <= 48) {
						index = (chan_start - 36) / 4;
					} else if (chan_start >= 52 && chan_start <= 64) {
						index = ((chan_start - 52) / 4) + 4;
					} else if (chan_start >= 100 && chan_start <= 140) {
						index = ((chan_start - 100) / 4) + 8;
					} else if (chan_start >= 149 && chan_start <= 165) {
						index = ((chan_start - 149) / 4) + 20;
					} else {
						continue;
					}

					for (j = 0; j < chan_count; j++)
						band[index + j] = 1;
					sdev->enabled_channel_count += chan_count;
				}
			}
		}

		if (values[++mib_index].type != SLSI_MIB_TYPE_NONE) /* HT enabled? */
			sdev->fw_ht_enabled = values[mib_index].u.boolValue;
		else
			SLSI_WARN(sdev, "Error reading HT enabled mib\n");
		if (values[++mib_index].type != SLSI_MIB_TYPE_NONE)  /* VHT enabled? */
			sdev->fw_vht_enabled = values[mib_index].u.boolValue;
		else
			SLSI_WARN(sdev, "Error reading VHT enabled mib\n");
		if (values[++mib_index].type == SLSI_MIB_TYPE_OCTET) { /* HT capabilities */
			if (values[mib_index].u.octetValue.dataLength >= 4)
				memcpy(&sdev->fw_ht_cap, values[mib_index].u.octetValue.data, 4);
			else
				SLSI_WARN(sdev, "Error reading HT capabilities\n");
		} else {
			SLSI_WARN(sdev, "Error reading HT capabilities\n");
		}
		if (values[++mib_index].type == SLSI_MIB_TYPE_OCTET) { /* VHT capabilities */
			if (values[mib_index].u.octetValue.dataLength >= 4)
				memcpy(&sdev->fw_vht_cap, values[mib_index].u.octetValue.data, 4);
			else
				SLSI_WARN(sdev, "Error reading VHT capabilities\n");
		} else {
			SLSI_WARN(sdev, "Error reading VHT capabilities\n");
		}
		if (values[++mib_index].type != SLSI_MIB_TYPE_NONE)  /* 40Mz wide channels in the 2.4G band enabled */
			sdev->fw_2g_40mhz_enabled = values[mib_index].u.boolValue;
		else
			SLSI_WARN(sdev, "Error reading 2g 40mhz enabled mib\n");

		if (values[++mib_index].type != SLSI_MIB_TYPE_NONE) {    /* HARDWARE_PLATFORM */
			SLSI_CHECK_TYPE(sdev, values[mib_index].type, SLSI_MIB_TYPE_UINT);
			sdev->plat_info_mib.plat_build = values[mib_index].u.uintValue;
		} else {
			SLSI_WARN(sdev, "Error reading Hardware platform\n");
		}
		if (values[++mib_index].type != SLSI_MIB_TYPE_NONE) {    /* REG_DOM_VERSION */
			SLSI_CHECK_TYPE(sdev, values[mib_index].type, SLSI_MIB_TYPE_UINT);
			sdev->reg_dom_version = values[mib_index].u.uintValue;
		} else {
			SLSI_WARN(sdev, "Error reading Reg domain version\n");
		}

		/* NAN enabled? */
		if (values[++mib_index].type != SLSI_MIB_TYPE_NONE) {
			sdev->nan_enabled = values[mib_index].u.boolValue;
		} else {
			sdev->nan_enabled = false;
			SLSI_WARN(sdev, "Error reading NAN enabled mib\n");
                }

		if (values[++mib_index].type != SLSI_MIB_TYPE_NONE) { /* UnifiForcedScheduleDuration */
			SLSI_CHECK_TYPE(sdev, values[mib_index].type, SLSI_MIB_TYPE_UINT);
			sdev->fw_dwell_time = values[mib_index].u.uintValue;
		} else {
			SLSI_WARN(sdev, "Error reading UnifiForcedScheduleDuration\n");
		}

#ifdef CONFIG_SCSC_WLAN_WIFI_SHARING
		if (values[++mib_index].type == SLSI_MIB_TYPE_OCTET) {  /* 5Ghz Allowed Channels */
			if (values[mib_index].u.octetValue.dataLength >= 8) {
				memcpy(&sdev->wifi_sharing_5ghz_channel, values[mib_index].u.octetValue.data, 8);
				slsi_extract_valid_wifi_sharing_channels(sdev);
			} else {
				SLSI_WARN(sdev, "Error reading 5Ghz Allowed Channels\n");
			}
		} else {
			SLSI_WARN(sdev, "Error reading 5Ghz Allowed Channels\n");
		}
#endif

#ifdef CONFIG_SCSC_WLAN_AP_INFO_FILE
		if (values[++mib_index].type != SLSI_MIB_TYPE_NONE) /* Dual band concurrency */
			sdev->dualband_concurrency = values[mib_index].u.boolValue;
		else
			SLSI_WARN(sdev, "Error reading dual band concurrency\n");
		if (values[++mib_index].type == SLSI_MIB_TYPE_UINT) /* max client for soft AP */
			sdev->softap_max_client = values[mib_index].u.uintValue;
		else
			SLSI_WARN(sdev, "Error reading SoftAP max client\n");
#endif

		kfree(values);
	}
	kfree(mibrsp.data);
	slsi_check_num_radios(sdev);
	return r;
}

int slsi_set_mib_roam(struct slsi_dev *dev, struct net_device *ndev, u16 psid, int value)
{
	struct slsi_mib_data mib_data = { 0, NULL };
	int error = SLSI_MIB_STATUS_FAILURE;

	if (slsi_mib_encode_int(&mib_data, psid, value, 0) == SLSI_MIB_STATUS_SUCCESS)
		if (mib_data.dataLength) {
			error = slsi_mlme_set(dev, ndev, mib_data.data, mib_data.dataLength);
			if (error)
				SLSI_ERR(dev, "Err Setting MIB failed. error = %d\n", error);
			kfree(mib_data.data);
		}

	return error;
}

int slsi_get_mib_roam(struct slsi_dev *sdev, u16 psid, int *mib_value)
{
	struct slsi_mib_data mibreq = { 0, NULL };
	struct slsi_mib_data mibrsp = { 0, NULL };
	int rx_len = 0;
	int r;
	struct slsi_mib_get_entry get_values[] = { { psid, { 0, 0 } } };

	r = slsi_mib_encode_get_list(&mibreq, sizeof(get_values) / sizeof(struct slsi_mib_get_entry), get_values);
	if (r != SLSI_MIB_STATUS_SUCCESS)
		return -ENOMEM;

	mibrsp.dataLength = 64;
	mibrsp.data = kmalloc(mibrsp.dataLength, GFP_KERNEL);
	if (!mibrsp.data) {
		kfree(mibreq.data);
		return -ENOMEM;
	}

	r = slsi_mlme_get(sdev, NULL, mibreq.data, mibreq.dataLength, mibrsp.data, mibrsp.dataLength, &rx_len);
	kfree(mibreq.data);

	if (r == 0) {
		struct slsi_mib_value *values;

		mibrsp.dataLength = (u32)rx_len;

		values = slsi_mib_decode_get_list(&mibrsp, sizeof(get_values) / sizeof(struct slsi_mib_get_entry), get_values);

		if (!values) {
			kfree(mibrsp.data);
			return -EINVAL;
		}

		WARN_ON(values[0].type == SLSI_MIB_TYPE_OCTET ||
			values[0].type == SLSI_MIB_TYPE_NONE);

		if (values[0].type == SLSI_MIB_TYPE_INT)
			*mib_value = (int)(values->u.intValue);
		else if (values[0].type == SLSI_MIB_TYPE_UINT)
			*mib_value = (int)(values->u.uintValue);
		else if (values[0].type == SLSI_MIB_TYPE_BOOL)
			*mib_value = (int)(values->u.boolValue);

		SLSI_DBG2(sdev, SLSI_MLME, "MIB value = %d\n", *mib_value);
		kfree(values);
	} else {
		SLSI_ERR(sdev, "Mib read failed (error: %d)\n", r);
	}

	kfree(mibrsp.data);
	return r;
}

#ifdef CONFIG_SCSC_WLAN_GSCAN_ENABLE
int slsi_mib_get_gscan_cap(struct slsi_dev *sdev, struct slsi_nl_gscan_capabilities *cap)
{
	struct slsi_mib_data mibreq = { 0, NULL };
	struct slsi_mib_data mibrsp = { 0, NULL };
	int rx_len = 0;
	int r;
	static const struct slsi_mib_get_entry get_values[] = { { SLSI_PSID_UNIFI_GOOGLE_MAX_NUMBER_OF_PERIODIC_SCANS, { 0, 0 } },
							       { SLSI_PSID_UNIFI_GOOGLE_MAX_RSSI_SAMPLE_SIZE,            { 0, 0 } },
							       { SLSI_PSID_UNIFI_GOOGLE_MAX_HOTLIST_APS,       { 0, 0 } },
							       { SLSI_PSID_UNIFI_GOOGLE_MAX_SIGNIFICANT_WIFI_CHANGE_APS, { 0, 0 } },
							       { SLSI_PSID_UNIFI_GOOGLE_MAX_BSSID_HISTORY_ENTRIES, { 0, 0 } },};

	r = slsi_mib_encode_get_list(&mibreq, sizeof(get_values) / sizeof(struct slsi_mib_get_entry), get_values);
	if (r != SLSI_MIB_STATUS_SUCCESS)
		return -ENOMEM;

	mibrsp.dataLength = 64;
	mibrsp.data = kmalloc(mibrsp.dataLength, GFP_KERNEL);
	if (!mibrsp.data) {
		kfree(mibreq.data);
		return -ENOMEM;
	}

	r = slsi_mlme_get(sdev, NULL, mibreq.data, mibreq.dataLength, mibrsp.data, mibrsp.dataLength, &rx_len);
	kfree(mibreq.data);

	if (r == 0) {
		struct slsi_mib_value *values;

		mibrsp.dataLength = (u32)rx_len;

		values = slsi_mib_decode_get_list(&mibrsp, sizeof(get_values) / sizeof(struct slsi_mib_get_entry), get_values);

		if (!values) {
			kfree(mibrsp.data);
			return -EINVAL;
		}

		if (values[0].type != SLSI_MIB_TYPE_NONE) {
			SLSI_CHECK_TYPE(sdev, values[0].type, SLSI_MIB_TYPE_UINT);
			cap->max_scan_buckets = values[0].u.uintValue;
		}

		if (values[1].type != SLSI_MIB_TYPE_NONE) {
			SLSI_CHECK_TYPE(sdev, values[1].type, SLSI_MIB_TYPE_UINT);
			cap->max_rssi_sample_size = values[1].u.uintValue;
		}

		if (values[2].type != SLSI_MIB_TYPE_NONE) {
			SLSI_CHECK_TYPE(sdev, values[2].type, SLSI_MIB_TYPE_UINT);
			cap->max_hotlist_aps = values[2].u.uintValue;
		}

		if (values[3].type != SLSI_MIB_TYPE_NONE) {
			SLSI_CHECK_TYPE(sdev, values[3].type, SLSI_MIB_TYPE_UINT);
			cap->max_significant_wifi_change_aps = values[3].u.uintValue;
		}

		if (values[4].type != SLSI_MIB_TYPE_NONE) {
			SLSI_CHECK_TYPE(sdev, values[4].type, SLSI_MIB_TYPE_UINT);
			cap->max_bssid_history_entries = values[4].u.uintValue;
		}

		kfree(values);
	}
	kfree(mibrsp.data);
	return r;
}
#endif

int slsi_mib_get_rtt_cap(struct slsi_dev *sdev, struct net_device *dev, struct slsi_rtt_capabilities *cap)
{
	struct slsi_mib_data supported_rtt_capab = { 0, NULL };
	struct slsi_mib_data mibrsp = { 0, NULL };
	struct slsi_mib_value     *values = NULL;

	struct slsi_mib_get_entry get_values[] = { { SLSI_PSID_UNIFI_RTT_CAPABILITIES, { 0, 0 } } };

	mibrsp.dataLength = 64;
	mibrsp.data = kmalloc(mibrsp.dataLength, GFP_KERNEL);
	if (!mibrsp.data) {
		SLSI_ERR(sdev, "Cannot kmalloc %d bytes\n", mibrsp.dataLength);
		kfree(mibrsp.data);
		return -ENOMEM;
	}

	values = slsi_read_mibs(sdev, dev, get_values, 1, &mibrsp);
	if (!values) {
		kfree(mibrsp.data);
		return -EINVAL;
	}

	if (values[0].type != SLSI_MIB_TYPE_OCTET) {
		SLSI_ERR(sdev, "Invalid type (%d) for SLSI_PSID_UNIFI_RTT_CAPABILITIES", values[0].type);
		kfree(mibrsp.data);
		kfree(values);
		return -EINVAL;
	}
	supported_rtt_capab = values[0].u.octetValue;
	cap->rtt_one_sided_supported = supported_rtt_capab.data[0];
	cap->rtt_ftm_supported = supported_rtt_capab.data[1];
	cap->lci_support = supported_rtt_capab.data[2];
	cap->lcr_support = supported_rtt_capab.data[3];
	cap->responder_supported = supported_rtt_capab.data[4];
	cap->preamble_support = supported_rtt_capab.data[5];
	cap->bw_support = supported_rtt_capab.data[6];
	cap->mc_version = supported_rtt_capab.data[7];

	kfree(values);
	kfree(mibrsp.data);
	return 0;
}

struct slsi_peer *slsi_peer_add(struct slsi_dev *sdev, struct net_device *dev, u8 *peer_address, u16 aid)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_peer *peer = NULL;
	u16 queueset = 0;

	if (WARN_ON(!aid)) {
		SLSI_NET_ERR(dev, "Invalid aid(0) received\n");
		return NULL;
	}
	queueset = MAP_AID_TO_QS(aid);

	/* MUST only be called from the control path that has acquired the lock */
	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));
	if (WARN_ON(!ndev_vif->activated))
		return NULL;

	if (!peer_address) {
		SLSI_NET_WARN(dev, "Peer without address\n");
		return NULL;
	}

	peer = slsi_get_peer_from_mac(sdev, dev, peer_address);
	if (peer) {
		if (ndev_vif->sta.tdls_enabled && (peer->queueset == 0)) {
			SLSI_NET_DBG3(dev, SLSI_CFG80211, "TDLS enabled and its station queueset\n");
		} else {
			SLSI_NET_WARN(dev, "Peer (MAC:%pM) already exists\n", peer_address);
			return NULL;
		}
	}

	if (slsi_get_peer_from_qs(sdev, dev, queueset)) {
		SLSI_NET_WARN(dev, "Peer (queueset:%d) already exists\n", queueset);
		return NULL;
	}

	SLSI_NET_DBG2(dev, SLSI_CFG80211, "%pM, aid:%d\n", peer_address, aid);

	peer = ndev_vif->peer_sta_record[queueset];
	if (!peer) {
		/* If it reaches here, something has gone wrong */
		SLSI_NET_ERR(dev, "Peer (queueset:%d) is NULL\n", queueset);
		return NULL;
	}

	peer->aid = aid;
	peer->queueset = queueset;
	SLSI_ETHER_COPY(peer->address, peer_address);
	peer->assoc_ie = NULL;
	peer->assoc_resp_ie = NULL;
	peer->is_wps = false;
	peer->connected_state = SLSI_STA_CONN_STATE_DISCONNECTED;
	/* Initialise the Station info */
	slsi_peer_reset_stats(sdev, dev, peer);
	ratelimit_state_init(&peer->sinfo_mib_get_rs, SLSI_SINFO_MIB_ACCESS_TIMEOUT, 0);

	if (scsc_wifi_fcq_ctrl_q_init(&peer->ctrl_q) < 0) {
		SLSI_NET_ERR(dev, "scsc_wifi_fcq_ctrl_q_init failed\n");
		return NULL;
	}

#ifdef CONFIG_SCSC_WLAN_DEBUG
	if (scsc_wifi_fcq_unicast_qset_init(dev, &peer->data_qs, peer->queueset, sdev, ndev_vif->ifnum, peer) < 0) {
#else
	if (scsc_wifi_fcq_unicast_qset_init(dev, &peer->data_qs, peer->queueset, sdev, ndev_vif->ifnum, peer) < 0) {
#endif
		SLSI_NET_ERR(dev, "scsc_wifi_fcq_unicast_qset_init failed\n");
		scsc_wifi_fcq_ctrl_q_deinit(&peer->ctrl_q);
		return NULL;
	}

	/* A peer is only valid once all the data is initialised
	 * otherwise a process could check the flag and start to read
	 * uninitialised data.
	 */

	if (ndev_vif->sta.tdls_enabled)
		ndev_vif->sta.tdls_peer_sta_records++;
	else
		ndev_vif->peer_sta_records++;

	ndev_vif->cfg80211_sinfo_generation++;
	skb_queue_head_init(&peer->buffered_frames);

	/* For TDLS this flag will be set while moving the packets from STAQ to TDLSQ */
	/* TODO: changes for moving packets is removed for now. Enable this when these data path changes go in*/
/*	if (!ndev_vif->sta.tdls_enabled)
 *		peer->valid = true;
 */
	peer->valid = true;

	SLSI_NET_DBG2(dev, SLSI_CFG80211, "created station peer %pM AID:%d\n", peer->address, aid);
	return peer;
}

void slsi_peer_reset_stats(struct slsi_dev *sdev, struct net_device *dev, struct slsi_peer *peer)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);

	SLSI_UNUSED_PARAMETER(sdev);

	SLSI_NET_DBG3(dev, SLSI_CFG80211, "Peer:%pM\n", peer->address);

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	memset(&peer->sinfo, 0x00, sizeof(peer->sinfo));
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0))
	peer->sinfo.filled = BIT(NL80211_STA_INFO_RX_BYTES) |
			     BIT(NL80211_STA_INFO_TX_BYTES) |
			     BIT(NL80211_STA_INFO_RX_PACKETS) |
			     BIT(NL80211_STA_INFO_TX_PACKETS) |
			     BIT(NL80211_STA_INFO_RX_DROP_MISC) |
			     BIT(NL80211_STA_INFO_TX_FAILED) |
			     BIT(NL80211_STA_INFO_SIGNAL) |
			     BIT(NL80211_STA_INFO_BSS_PARAM);
#else
	peer->sinfo.filled = STATION_INFO_RX_BYTES |
			     STATION_INFO_TX_BYTES |
			     STATION_INFO_RX_PACKETS |
			     STATION_INFO_TX_PACKETS |
			     STATION_INFO_RX_DROP_MISC |
			     STATION_INFO_TX_FAILED |
			     STATION_INFO_SIGNAL |
			     STATION_INFO_BSS_PARAM;
#endif
}

void slsi_dump_stats(struct net_device *dev)
{
	SLSI_UNUSED_PARAMETER(dev);

	SLSI_INFO_NODEV("slsi_hanged_event_count: %d\n", slsi_hanged_event_count);
}

enum slsi_wlan_vendor_attr_hanged_event {
	SLSI_WLAN_VENDOR_ATTR_HANGED_EVENT_PANIC_CODE = 1,
	SLSI_WLAN_VENDOR_ATTR_HANGED_EVENT_MAX
};

int slsi_send_hanged_vendor_event(struct slsi_dev *sdev, u16 scsc_panic_code)
{
	struct sk_buff *skb;
	int length = sizeof(scsc_panic_code);

	slsi_hanged_event_count++;
	SLSI_INFO(sdev, "Sending SLSI_NL80211_VENDOR_HANGED_EVENT , count: %d, reason =0x%2x\n", slsi_hanged_event_count, scsc_panic_code);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
	skb = cfg80211_vendor_event_alloc(sdev->wiphy, NULL, length, SLSI_NL80211_VENDOR_HANGED_EVENT, GFP_KERNEL);
#else
	skb = cfg80211_vendor_event_alloc(sdev->wiphy, length, SLSI_NL80211_VENDOR_HANGED_EVENT, GFP_KERNEL);
#endif
	if (!skb) {
		SLSI_ERR_NODEV("Failed to allocate SKB for vendor hanged event");
		return -ENOMEM;
	}

	if (nla_put(skb, SLSI_WLAN_VENDOR_ATTR_HANGED_EVENT_PANIC_CODE, length, &scsc_panic_code)) {
		SLSI_ERR_NODEV("Failed nla_put for panic code\n");
		slsi_kfree_skb(skb);
		return -EINVAL;
	}
	cfg80211_vendor_event(skb, GFP_KERNEL);

	return 0;
}

#ifdef CONFIG_SCSC_WLAN_HANG_TEST
int slsi_test_send_hanged_vendor_event(struct net_device *dev)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);

	SLSI_INFO(ndev_vif->sdev, "Test FORCE HANG\n");
	return slsi_send_hanged_vendor_event(ndev_vif->sdev, SCSC_PANIC_CODE_HOST << 15);
}
#endif

static bool slsi_search_ies_for_qos_indicators(struct slsi_dev *sdev, u8 *ies, int ies_len)
{
	SLSI_UNUSED_PARAMETER_NOT_DEBUG(sdev);

	if (cfg80211_find_ie(WLAN_EID_HT_CAPABILITY, ies, ies_len)) {
		SLSI_DBG1(sdev, SLSI_CFG80211, "QOS enabled due to WLAN_EID_HT_CAPABILITY\n");
		return true;
	}
	if (cfg80211_find_ie(WLAN_EID_VHT_CAPABILITY, ies, ies_len)) {
		SLSI_DBG1(sdev, SLSI_CFG80211, "QOS enabled due to WLAN_EID_VHT_CAPABILITY\n");
		return true;
	}
	if (cfg80211_find_vendor_ie(WLAN_OUI_MICROSOFT, WLAN_OUI_TYPE_MICROSOFT_WMM, ies, ies_len)) {
		SLSI_DBG1(sdev, SLSI_CFG80211, "QOS enabled due to WLAN_OUI_TYPE_MICROSOFT_WMM\n");
		return true;
	}
	return false;
}

void slsi_peer_update_assoc_req(struct slsi_dev *sdev, struct net_device *dev, struct slsi_peer *peer, struct sk_buff *skb)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	u16 id = fapi_get_u16(skb, id);

	/* MUST only be called from the control path that has acquired the lock */
	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	switch (id) {
	case MLME_CONNECTED_IND:
	case MLME_PROCEDURE_STARTED_IND:
		if (WARN_ON(ndev_vif->vif_type != FAPI_VIFTYPE_AP &&
			    ndev_vif->vif_type != FAPI_VIFTYPE_STATION)) {
			slsi_kfree_skb(skb);
			return;
		}
		break;
	default:
		slsi_kfree_skb(skb);
		WARN_ON(1);
		return;
	}

	slsi_kfree_skb(peer->assoc_ie);
	peer->assoc_ie = NULL;
	peer->capabilities = 0;

	if (fapi_get_datalen(skb)) {
		int mgmt_hdr_len;
		struct ieee80211_mgmt *mgmt = fapi_get_mgmt(skb);
		struct netdev_vif *ndev_vif = netdev_priv(dev);

		/* Update the skb to just point to the frame */
		skb_pull(skb, fapi_get_siglen(skb));

		if (ieee80211_is_assoc_req(mgmt->frame_control)) {
			mgmt_hdr_len = (mgmt->u.assoc_req.variable - (u8 *)mgmt);
			if (ndev_vif->vif_type == FAPI_VIFTYPE_AP)
				peer->capabilities = le16_to_cpu(mgmt->u.assoc_req.capab_info);
		} else if (ieee80211_is_reassoc_req(mgmt->frame_control)) {
			mgmt_hdr_len = (mgmt->u.reassoc_req.variable - (u8 *)mgmt);
			if (ndev_vif->vif_type == FAPI_VIFTYPE_AP)
				peer->capabilities = le16_to_cpu(mgmt->u.reassoc_req.capab_info);
		} else {
			WARN_ON(1);
			slsi_kfree_skb(skb);
			return;
		}

		skb_pull(skb, mgmt_hdr_len);

		peer->assoc_ie = skb;
		peer->sinfo.assoc_req_ies = skb->data;
		peer->sinfo.assoc_req_ies_len = skb->len;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
		peer->sinfo.filled |= STATION_INFO_ASSOC_REQ_IES;
#endif
		peer->qos_enabled = slsi_search_ies_for_qos_indicators(sdev, skb->data, skb->len);
	}
}

void slsi_peer_update_assoc_rsp(struct slsi_dev *sdev, struct net_device *dev, struct slsi_peer *peer, struct sk_buff *skb)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	u16 id = fapi_get_u16(skb, id);

	SLSI_UNUSED_PARAMETER(sdev);

	/* MUST only be called from the control path that has acquired the lock */
	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	if (ndev_vif->vif_type != FAPI_VIFTYPE_STATION)
		goto exit_with_warnon;

	if (id != MLME_CONNECT_IND && id != MLME_ROAMED_IND && id != MLME_REASSOCIATE_IND) {
		SLSI_NET_ERR(dev, "Unexpected id =0x%4x\n", id);
		goto exit_with_warnon;
	}

	slsi_kfree_skb(peer->assoc_resp_ie);
	peer->assoc_resp_ie = NULL;
	peer->capabilities = 0;
	if (fapi_get_datalen(skb)) {
		int mgmt_hdr_len;
		struct ieee80211_mgmt *mgmt = fapi_get_mgmt(skb);

		/* Update the skb to just point to the frame */
		skb_pull(skb, fapi_get_siglen(skb));

		if (ieee80211_is_assoc_resp(mgmt->frame_control)) {
			mgmt_hdr_len = (mgmt->u.assoc_resp.variable - (u8 *)mgmt);
			peer->capabilities = le16_to_cpu(mgmt->u.assoc_resp.capab_info);
		} else if (ieee80211_is_reassoc_resp(mgmt->frame_control)) {
			mgmt_hdr_len = (mgmt->u.reassoc_resp.variable - (u8 *)mgmt);
			peer->capabilities = le16_to_cpu(mgmt->u.reassoc_resp.capab_info);
		} else {
			goto exit_with_warnon;
		}

		skb_pull(skb, mgmt_hdr_len);
		peer->assoc_resp_ie = skb;
	}
	return;

exit_with_warnon:
	WARN_ON(1);
	slsi_kfree_skb(skb);
}

int slsi_peer_remove(struct slsi_dev *sdev, struct net_device *dev, struct slsi_peer *peer)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff *buff_frame;

	SLSI_UNUSED_PARAMETER(sdev);

	/* MUST only be called from the control path that has acquired the lock */
	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	if (!peer) {
		SLSI_NET_WARN(dev, "peer=NULL");
		return -EINVAL;
	}

	SLSI_NET_DBG2(dev, SLSI_CFG80211, "%pM\n", peer->address);

	buff_frame = slsi_skb_dequeue(&peer->buffered_frames);
	while (buff_frame) {
		SLSI_NET_DBG3(dev, SLSI_MLME, "FLUSHING BUFFERED FRAMES\n");
		slsi_kfree_skb(buff_frame);
		buff_frame = slsi_skb_dequeue(&peer->buffered_frames);
	}

	slsi_rx_ba_stop_all(dev, peer);

	/* Take the peer lock to protect the transmit data path
	 * when accessing peer records.
	 */
	slsi_spinlock_lock(&ndev_vif->peer_lock);

	/* The information is no longer valid so first update the flag to ensure that
	 * another process doesn't try to use it any more.
	 */
	peer->valid = false;
	peer->is_wps = false;
	peer->connected_state = SLSI_STA_CONN_STATE_DISCONNECTED;

	if (slsi_is_tdls_peer(dev, peer))
		ndev_vif->sta.tdls_peer_sta_records--;
	else
		ndev_vif->peer_sta_records--;

	slsi_spinlock_unlock(&ndev_vif->peer_lock);

	ndev_vif->cfg80211_sinfo_generation++;

	scsc_wifi_fcq_qset_deinit(dev, &peer->data_qs, sdev, ndev_vif->ifnum, peer);
	scsc_wifi_fcq_ctrl_q_deinit(&peer->ctrl_q);

	slsi_kfree_skb(peer->assoc_ie);
	slsi_kfree_skb(peer->assoc_resp_ie);
	memset(peer, 0x00, sizeof(*peer));

	return 0;
}

int slsi_vif_activated(struct slsi_dev *sdev, struct net_device *dev)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);

	SLSI_UNUSED_PARAMETER(sdev);

	/* MUST only be called from the control path that has acquired the lock */
	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	/* MUST have cleared any peer records previously */
	WARN_ON(ndev_vif->peer_sta_records);

	if (WARN_ON(ndev_vif->activated))
		return -EALREADY;

	if (ndev_vif->vif_type == FAPI_VIFTYPE_AP)
		/* Enable the Multicast queue set for AP mode */
		if (scsc_wifi_fcq_multicast_qset_init(dev, &ndev_vif->ap.group_data_qs, sdev, ndev_vif->ifnum) < 0)
			return -EFAULT;

	if (ndev_vif->vif_type == FAPI_VIFTYPE_STATION) {
		/* MUST have cleared any tdls peer records previously */
		WARN_ON(ndev_vif->sta.tdls_peer_sta_records);

		ndev_vif->sta.tdls_peer_sta_records = 0;
		ndev_vif->sta.tdls_enabled = false;
		ndev_vif->sta.roam_in_progress = false;
		ndev_vif->sta.nd_offload_enabled = true;

		memset(ndev_vif->sta.keepalive_host_tag, 0, sizeof(ndev_vif->sta.keepalive_host_tag));
	}

	ndev_vif->cfg80211_sinfo_generation = 0;
	ndev_vif->peer_sta_records = 0;
	ndev_vif->activated = true;
	ndev_vif->mgmt_tx_data.exp_frame = SLSI_P2P_PA_INVALID;
	return 0;
}

void slsi_vif_deactivated(struct slsi_dev *sdev, struct net_device *dev)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	int i;

	/* MUST only be called from the control path that has acquired the lock */
	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	/* The station type VIF is deactivated when the AP connection is lost */
	if (ndev_vif->vif_type == FAPI_VIFTYPE_STATION) {
		ndev_vif->sta.group_key_set = false;
		ndev_vif->sta.vif_status = SLSI_VIF_STATUS_UNSPECIFIED;
		memset(ndev_vif->sta.keepalive_host_tag, 0, sizeof(ndev_vif->sta.keepalive_host_tag));

		/* delete the TSPEC entries (if any) if it is a STA vif */
		if (ndev_vif->iftype == NL80211_IFTYPE_STATION)
			cac_delete_tspec_list(sdev);

		if (ndev_vif->sta.tdls_enabled)
			WARN(ndev_vif->sta.tdls_peer_sta_records, "vif:%d, tdls_peer_sta_records:%d", ndev_vif->ifnum, ndev_vif->sta.tdls_peer_sta_records);

		if (ndev_vif->sta.sta_bss) {
			slsi_cfg80211_put_bss(sdev->wiphy, ndev_vif->sta.sta_bss);
			ndev_vif->sta.sta_bss = NULL;
		}
		ndev_vif->sta.tdls_enabled = false;
	}

	/* MUST be done first to ensure that other code doesn't treat the VIF as still active */
	ndev_vif->activated = false;
	slsi_skb_queue_purge(&ndev_vif->rx_data.queue);

	for (i = 0; i < (SLSI_ADHOC_PEER_CONNECTIONS_MAX); i++) {
		struct slsi_peer *peer = ndev_vif->peer_sta_record[i];

		if (peer && peer->valid) {
			if (ndev_vif->vif_type == FAPI_VIFTYPE_AP && peer->assoc_ie)
				cfg80211_del_sta(dev, peer->address, GFP_KERNEL);
			slsi_peer_remove(sdev, dev, peer);
		}
	}

	if (ndev_vif->vif_type == FAPI_VIFTYPE_AP) {
		memset(&ndev_vif->ap.last_disconnected_sta, 0, sizeof(ndev_vif->ap.last_disconnected_sta));
		scsc_wifi_fcq_qset_deinit(dev, &ndev_vif->ap.group_data_qs, sdev, ndev_vif->ifnum, NULL);
	}

	if ((ndev_vif->iftype == NL80211_IFTYPE_P2P_CLIENT) || (ndev_vif->iftype == NL80211_IFTYPE_P2P_GO)) {
		SLSI_P2P_STATE_CHANGE(sdev, P2P_IDLE_NO_VIF);
		sdev->p2p_group_exp_frame = SLSI_P2P_PA_INVALID;
	}

	/* MUST be done last as lots of code is dependent on checking the vif_type */
	ndev_vif->vif_type = SLSI_VIFTYPE_UNSPECIFIED;
	ndev_vif->set_power_mode = FAPI_POWERMANAGEMENTMODE_POWER_SAVE;
	if (slsi_is_rf_test_mode_enabled()) {
		SLSI_NET_ERR(dev, "*#rf# rf test mode set is enabled.\n");
		ndev_vif->set_power_mode = FAPI_POWERMANAGEMENTMODE_ACTIVE_MODE;
	} else {
		ndev_vif->set_power_mode = FAPI_POWERMANAGEMENTMODE_POWER_SAVE;
	}
	ndev_vif->mgmt_tx_data.exp_frame = SLSI_P2P_PA_INVALID;

	/* SHOULD have cleared any peer records */
	WARN(ndev_vif->peer_sta_records, "vif:%d, peer_sta_records:%d", ndev_vif->ifnum, ndev_vif->peer_sta_records);

	sdev->device_config.qos_info = -1;
}

static int slsi_sta_ieee80211_mode(struct net_device *dev, u16 current_bss_channel_frequency)
{
	struct netdev_vif    *ndev_vif = netdev_priv(dev);
	const u8             *ie;

	ie = cfg80211_find_ie(WLAN_EID_VHT_OPERATION, ndev_vif->sta.sta_bss->ies->data,
			      ndev_vif->sta.sta_bss->ies->len);
	if (ie)
		return SLSI_80211_MODE_11AC;

	ie = cfg80211_find_ie(WLAN_EID_HT_OPERATION, ndev_vif->sta.sta_bss->ies->data, ndev_vif->sta.sta_bss->ies->len);
	if (ie)
		return SLSI_80211_MODE_11N;

	if (current_bss_channel_frequency > 5000)
		return  SLSI_80211_MODE_11A;

	ie = cfg80211_find_ie(WLAN_EID_SUPP_RATES, ndev_vif->sta.sta_bss->ies->data, ndev_vif->sta.sta_bss->ies->len);
	if (ie)
		return slsi_get_supported_mode(ie);
	return -EINVAL;
}

static int slsi_get_sta_mode(struct net_device *dev, const u8 *last_peer_mac)
{
	struct netdev_vif    *ndev_vif = netdev_priv(dev);
	struct slsi_dev *sdev = ndev_vif->sdev;
	struct slsi_peer    *last_peer;
	const u8             *peer_ie;

	last_peer = slsi_get_peer_from_mac(sdev, dev, last_peer_mac);

	if (!last_peer) {
		SLSI_NET_ERR(dev, "Peer not found\n");
		return -EINVAL;
	}

	if (ndev_vif->ap.mode == SLSI_80211_MODE_11AC) { /*AP supports VHT*/
		peer_ie = cfg80211_find_ie(WLAN_EID_VHT_CAPABILITY, last_peer->assoc_ie->data,
					   last_peer->assoc_ie->len);
		if (peer_ie)
			return SLSI_80211_MODE_11AC;

		peer_ie = cfg80211_find_ie(WLAN_EID_HT_CAPABILITY, last_peer->assoc_ie->data,
					   last_peer->assoc_ie->len);
		if (peer_ie)
			return SLSI_80211_MODE_11N;
		return  SLSI_80211_MODE_11A;
	}
	if (ndev_vif->ap.mode == SLSI_80211_MODE_11N) { /*AP supports HT*/
		peer_ie = cfg80211_find_ie(WLAN_EID_HT_CAPABILITY, last_peer->assoc_ie->data,
					   last_peer->assoc_ie->len);
		if (peer_ie)
			return SLSI_80211_MODE_11N;
		if (ndev_vif->ap.channel_freq > 5000)
			return SLSI_80211_MODE_11A;
		peer_ie = cfg80211_find_ie(WLAN_EID_SUPP_RATES, last_peer->assoc_ie->data,
					   last_peer->assoc_ie->len);
		if (peer_ie)
			return slsi_get_supported_mode(peer_ie);
	}

	if (ndev_vif->ap.channel_freq > 5000)
		return SLSI_80211_MODE_11A;

	if (ndev_vif->ap.mode == SLSI_80211_MODE_11G) {	/*AP supports 11g mode */
		peer_ie = cfg80211_find_ie(WLAN_EID_SUPP_RATES, last_peer->assoc_ie->data,
					   last_peer->assoc_ie->len);
		if (peer_ie)
			return slsi_get_supported_mode(peer_ie);
	}

	return SLSI_80211_MODE_11B;
}

int slsi_populate_bss_record(struct net_device *dev)
{
	struct netdev_vif        *ndev_vif = netdev_priv(dev);
	struct slsi_dev          *sdev = ndev_vif->sdev;
	struct slsi_mib_data     mibrsp = { 0, NULL };
	struct slsi_mib_value    *values = NULL;
	const u8                 *ie, *ext_capab, *rm_capab, *ext_data, *rm_data, *bss_load;
	u8                       ext_capab_ie_len, rm_capab_ie_len;
	bool                     neighbor_report_bit = 0, btm = 0;
	u16                                     fw_tx_rate;
	struct slsi_mib_get_entry get_values[] = { { SLSI_PSID_UNIFI_CURRENT_BSS_CHANNEL_FREQUENCY, { 0, 0 } },
							       { SLSI_PSID_UNIFI_CURRENT_BSS_BANDWIDTH, { 0, 0 } },
							       { SLSI_PSID_UNIFI_CURRENT_BSS_NSS, {0, 0} },
							       { SLSI_PSID_UNIFI_AP_MIMO_USED, {0, 0} },
							       { SLSI_PSID_UNIFI_LAST_BSS_SNR, {0, 0} },
							       { SLSI_PSID_UNIFI_LAST_BSS_RSSI, { 0, 0 } },
							       { SLSI_PSID_UNIFI_ROAMING_COUNT, {0, 0} },
							       { SLSI_PSID_UNIFI_LAST_BSS_TX_DATA_RATE, { 0, 0 } },
							       { SLSI_PSID_UNIFI_ROAMING_AKM, {0, 0} } };

	mibrsp.dataLength = 10 * ARRAY_SIZE(get_values);
	mibrsp.data = kmalloc(mibrsp.dataLength, GFP_KERNEL);
	if (!mibrsp.data) {
		SLSI_ERR(sdev, "Cannot kmalloc %d bytes for interface MIBs\n", mibrsp.dataLength);
		return -ENOMEM;
	}

	values = slsi_read_mibs(sdev, dev, get_values, ARRAY_SIZE(get_values), &mibrsp);

	memset(&ndev_vif->sta.last_connected_bss, 0, sizeof(ndev_vif->sta.last_connected_bss));

	if (!values) {
		SLSI_NET_DBG1(dev, SLSI_MLME, "mib decode list failed\n");
		kfree(values);
		kfree(mibrsp.data);
		return -EINVAL;
	}

	/* The Below sequence of reading the BSS Info related Mibs is very important */
	if (values[0].type != SLSI_MIB_TYPE_NONE) {    /* CURRENT_BSS_CHANNEL_FREQUENCY */
		SLSI_CHECK_TYPE(sdev, values[0].type, SLSI_MIB_TYPE_UINT);
		ndev_vif->sta.last_connected_bss.channel_freq = ((values[0].u.uintValue) / 2);
	}

	if (values[1].type != SLSI_MIB_TYPE_NONE) {    /* CURRENT_BSS_BANDWIDTH */
		SLSI_CHECK_TYPE(sdev, values[1].type, SLSI_MIB_TYPE_UINT);
		ndev_vif->sta.last_connected_bss.bandwidth = values[1].u.uintValue;
	}

	if (values[2].type != SLSI_MIB_TYPE_NONE) {    /* CURRENT_BSS_NSS */
		SLSI_CHECK_TYPE(sdev, values[2].type, SLSI_MIB_TYPE_UINT);
		ndev_vif->sta.last_connected_bss.antenna_mode = values[2].u.uintValue;
	}

	if (values[3].type != SLSI_MIB_TYPE_NONE) {    /* AP_MIMO_USED */
		SLSI_CHECK_TYPE(sdev, values[3].type, SLSI_MIB_TYPE_UINT);
		ndev_vif->sta.last_connected_bss.mimo_used = values[3].u.uintValue;
	}

	if (values[4].type != SLSI_MIB_TYPE_NONE) {    /* SNR */
		SLSI_CHECK_TYPE(sdev, values[4].type, SLSI_MIB_TYPE_UINT);
		ndev_vif->sta.last_connected_bss.snr = values[4].u.uintValue;
	}

	if (values[5].type != SLSI_MIB_TYPE_NONE) {    /* RSSI */
		SLSI_CHECK_TYPE(sdev, values[5].type, SLSI_MIB_TYPE_INT);
		ndev_vif->sta.last_connected_bss.rssi = values[5].u.intValue;
	}

	if (values[6].type != SLSI_MIB_TYPE_NONE) {    /* ROAMING_COUNT */
		SLSI_CHECK_TYPE(sdev, values[6].type, SLSI_MIB_TYPE_UINT);
		ndev_vif->sta.last_connected_bss.roaming_count = values[6].u.uintValue;
	}

	if (values[7].type != SLSI_MIB_TYPE_NONE) {    /* TX_DATA_RATE */
		SLSI_CHECK_TYPE(sdev, values[7].type, SLSI_MIB_TYPE_UINT);
		fw_tx_rate = values[7].u.uintValue;
		slsi_fw_tx_rate_calc(fw_tx_rate, NULL,
				     (unsigned long *)(&ndev_vif->sta.last_connected_bss.tx_data_rate));
	}

	if (values[8].type != SLSI_MIB_TYPE_NONE) {    /* ROAMING_AKM */
		SLSI_CHECK_TYPE(sdev, values[8].type, SLSI_MIB_TYPE_UINT);
		ndev_vif->sta.last_connected_bss.roaming_akm = values[8].u.uintValue;
	}

	kfree(values);
	kfree(mibrsp.data);

	if (!ndev_vif->sta.sta_bss) {
		SLSI_WARN(sdev, "Bss missing due to out of order msg from firmware!! Cannot collect Big Data\n");
		return -EINVAL;
	}

	SLSI_ETHER_COPY(ndev_vif->sta.last_connected_bss.address, ndev_vif->sta.sta_bss->bssid);

	ndev_vif->sta.last_connected_bss.mode = slsi_sta_ieee80211_mode(dev,
									   ndev_vif->sta.last_connected_bss.channel_freq);
	if (ndev_vif->sta.last_connected_bss.mode == -EINVAL) {
		SLSI_ERR(sdev, "slsi_get_bss_info : Supported Rates IE is null");
		return -EINVAL;
	}

	ie = cfg80211_find_vendor_ie(WLAN_OUI_WFA, SLSI_WLAN_OUI_TYPE_WFA_HS20_IND,
				     ndev_vif->sta.sta_bss->ies->data, ndev_vif->sta.sta_bss->ies->len);
	if (ie) {
		if ((ie[6] >> 4) == 0)
			ndev_vif->sta.last_connected_bss.passpoint_version = 1;
		else
			ndev_vif->sta.last_connected_bss.passpoint_version = 2;
	}

	ndev_vif->sta.last_connected_bss.noise_level = (ndev_vif->sta.last_connected_bss.rssi -
							   ndev_vif->sta.last_connected_bss.snr);

	ext_capab = cfg80211_find_ie(WLAN_EID_EXT_CAPABILITY, ndev_vif->sta.sta_bss->ies->data,
				     ndev_vif->sta.sta_bss->ies->len);
	rm_capab = cfg80211_find_ie(WLAN_EID_RRM_ENABLED_CAPABILITIES, ndev_vif->sta.sta_bss->ies->data,
				    ndev_vif->sta.sta_bss->ies->len);
	bss_load =  cfg80211_find_ie(WLAN_EID_QBSS_LOAD, ndev_vif->sta.sta_bss->ies->data,
				     ndev_vif->sta.sta_bss->ies->len);

	if (ext_capab) {
		ext_capab_ie_len = ext_capab[1];
		ext_data = &ext_capab[2];
		if ((ext_capab_ie_len >= 2) && (ext_data[1] &
		     SLSI_WLAN_EXT_CAPA1_PROXY_ARP_ENABLED))	/*check bit12 is set or not */
			ndev_vif->sta.last_connected_bss.kvie |= 1 << 1;
		if (ext_capab_ie_len >= 3) {
			if (ext_data[2] & SLSI_WLAN_EXT_CAPA2_TFS_ENABLED) /*check bit16 is set or not */
				ndev_vif->sta.last_connected_bss.kvie |= 1 << 2;
			if (ext_data[2] & SLSI_WLAN_EXT_CAPA2_WNM_SLEEP_ENABLED)	/*check bit17 is set or not */
				ndev_vif->sta.last_connected_bss.kvie |= 1 << 3;
			if (ext_data[2] & SLSI_WLAN_EXT_CAPA2_TIM_ENABLED)  /*check bit18 is set or not */
				ndev_vif->sta.last_connected_bss.kvie |= 1 << 4;
			/*check bit19 is set or not */
			if (ext_data[2] & SLSI_WLAN_EXT_CAPA2_BSS_TRANSISITION_ENABLED) {
				ndev_vif->sta.last_connected_bss.kvie |= 1 << 5;
				btm = 1;
				}
			if (ext_data[2] & SLSI_WLAN_EXT_CAPA2_DMS_ENABLED)  /*check bit20 is set or not */
				ndev_vif->sta.last_connected_bss.kvie |= 1 << 6;
		}
	}
	if (bss_load)
		ndev_vif->sta.last_connected_bss.kvie |= 1;
	if (rm_capab) {
		rm_capab_ie_len = rm_capab[1];
		rm_data = &rm_capab[2];
		if (rm_capab_ie_len >= 1) {
			neighbor_report_bit = SLSI_WLAN_RM_CAPA0_NEIGHBOR_REPORT_ENABLED & rm_data[0];
			if (SLSI_WLAN_RM_CAPA0_LINK_MEASUREMENT_ENABLED & rm_data[0])
				ndev_vif->sta.last_connected_bss.kvie |= 1 << 7;
			if (neighbor_report_bit)
				ndev_vif->sta.last_connected_bss.kvie |= 1 << 8;
			if (SLSI_WLAN_RM_CAPA0_PASSIVE_MODE_ENABLED & rm_data[0])
				ndev_vif->sta.last_connected_bss.kvie |= 1 << 9;
			if (SLSI_WLAN_RM_CAPA0_ACTIVE_MODE_ENABLED & rm_data[0])
				ndev_vif->sta.last_connected_bss.kvie |= 1 << 10;
			if (SLSI_WLAN_RM_CAPA0_TABLE_MODE_ENABLED & rm_data[0])
				ndev_vif->sta.last_connected_bss.kvie |= 1 << 11;
		}
	}
	if (!neighbor_report_bit && !btm && !bss_load)
		ndev_vif->sta.last_connected_bss.kv = 0;
	else if (neighbor_report_bit != 0 && (!btm && !bss_load))
		ndev_vif->sta.last_connected_bss.kv = 1;		/*11k support */
	else if (!neighbor_report_bit && (btm || bss_load))
		ndev_vif->sta.last_connected_bss.kv = 2;		/*11v support */
	else
		ndev_vif->sta.last_connected_bss.kv = 3;		/*11kv support */

	return 0;
}

static int slsi_fill_last_disconnected_sta_info(struct slsi_dev *sdev, struct net_device *dev,
						const u8 *last_peer_mac, const u16 reason_code)
{
	int i;
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_peer *last_peer;
	struct slsi_mib_data     mibrsp = { 0, NULL };
	struct slsi_mib_value    *values = NULL;
	u16                             fw_tx_rate;
	struct slsi_mib_get_entry get_values[] = { { SLSI_PSID_UNIFI_PEER_BANDWIDTH, { 0, 0 } },
							       { SLSI_PSID_UNIFI_CURRENT_PEER_NSS, {0, 0} },
							       { SLSI_PSID_UNIFI_PEER_RSSI, { 0, 0 } },
							       { SLSI_PSID_UNIFI_PEER_TX_DATA_RATE, { 0, 0 } } };

	SLSI_ETHER_COPY(ndev_vif->ap.last_disconnected_sta.address,
			last_peer_mac);
	ndev_vif->ap.last_disconnected_sta.reason = reason_code;
	ndev_vif->ap.last_disconnected_sta.mode = slsi_get_sta_mode(dev, last_peer_mac);
	last_peer = slsi_get_peer_from_mac(sdev, dev, last_peer_mac);
	if (!last_peer) {
		SLSI_NET_ERR(dev, "Peer not found\n");
		return -EINVAL;
	}
	for (i = 0; i < ARRAY_SIZE(get_values); i++)
		get_values[i].index[0] = last_peer->aid;

	ndev_vif->ap.last_disconnected_sta.bandwidth = SLSI_DEFAULT_UNIFI_PEER_BANDWIDTH;
	ndev_vif->ap.last_disconnected_sta.antenna_mode = SLSI_DEFAULT_UNIFI_PEER_NSS;
	ndev_vif->ap.last_disconnected_sta.rssi = SLSI_DEFAULT_UNIFI_PEER_RSSI;
	ndev_vif->ap.last_disconnected_sta.tx_data_rate = SLSI_DEFAULT_UNIFI_PEER_TX_DATA_RATE;

	mibrsp.dataLength = 15 * ARRAY_SIZE(get_values);
	mibrsp.data = kmalloc(mibrsp.dataLength, GFP_KERNEL);

	if (!mibrsp.data) {
		SLSI_ERR(sdev, "Cannot kmalloc %d bytes for interface MIBs\n", mibrsp.dataLength);
		return -ENOMEM;
	}

	values = slsi_read_mibs(sdev, dev, get_values, ARRAY_SIZE(get_values), &mibrsp);

	if (!values) {
		SLSI_NET_DBG1(dev, SLSI_MLME, "mib decode list failed\n");
		kfree(values);
		kfree(mibrsp.data);
		return -EINVAL;
	}
	if (values[0].type != SLSI_MIB_TYPE_NONE) {   /* LAST_PEER_BANDWIDTH */
		SLSI_CHECK_TYPE(sdev, values[0].type, SLSI_MIB_TYPE_INT);
		ndev_vif->ap.last_disconnected_sta.bandwidth = values[0].u.intValue;
	}

	if (values[1].type != SLSI_MIB_TYPE_NONE) {     /*LAST_PEER_NSS*/
		SLSI_CHECK_TYPE(sdev, values[1].type, SLSI_MIB_TYPE_INT);
		ndev_vif->ap.last_disconnected_sta.antenna_mode = values[1].u.intValue;
	}

	if (values[2].type != SLSI_MIB_TYPE_NONE) {    /* LAST_PEER_RSSI*/
		SLSI_CHECK_TYPE(sdev, values[2].type, SLSI_MIB_TYPE_INT);
		ndev_vif->ap.last_disconnected_sta.rssi = values[2].u.intValue;
	}

	if (values[3].type != SLSI_MIB_TYPE_NONE) {    /* LAST_PEER_TX_DATA_RATE */
		SLSI_CHECK_TYPE(sdev, values[3].type, SLSI_MIB_TYPE_UINT);
		fw_tx_rate = values[3].u.uintValue;
		slsi_fw_tx_rate_calc(fw_tx_rate, NULL,
				     (unsigned long *)&ndev_vif->ap.last_disconnected_sta.tx_data_rate);
	}

	kfree(values);
	kfree(mibrsp.data);

	return 0;
}

int slsi_handle_disconnect(struct slsi_dev *sdev, struct net_device *dev, u8 *peer_address, u16 reason)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);

	if (WARN_ON(!dev))
		goto exit;

	SLSI_NET_DBG3(dev, SLSI_MLME, "slsi_handle_disconnect(vif:%d)\n", ndev_vif->ifnum);

	/* MUST only be called from somewhere that has acquired the lock */
	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	if (!ndev_vif->activated) {
		SLSI_NET_DBG1(dev, SLSI_MLME, "VIF not activated\n");
		goto exit;
	}

	switch (ndev_vif->vif_type) {
	case FAPI_VIFTYPE_STATION:
	{
		netif_carrier_off(dev);

		/* MLME-DISCONNECT-IND could indicate the completion of a MLME-DISCONNECT-REQ or
		 * the connection with the AP has been lost
		 */
		if (ndev_vif->sta.vif_status == SLSI_VIF_STATUS_CONNECTING) {
			if (peer_address)
				SLSI_NET_WARN(dev, "Unexpected mlme_disconnect_ind - whilst connecting\n");
			else
				SLSI_NET_WARN(dev, "Connection failure\n");
		} else if (ndev_vif->sta.vif_status == SLSI_VIF_STATUS_CONNECTED) {
			if (reason == FAPI_REASONCODE_SYNCHRONISATION_LOSS)
				reason = 0; /*reason code to recognise beacon loss */
			else if (reason == FAPI_REASONCODE_KEEP_ALIVE_FAILURE)
				reason = WLAN_REASON_DEAUTH_LEAVING;/* Change to a standard reason code */

			if (ndev_vif->sta.is_wps) /* Ignore sending deauth or disassoc event to cfg80211 during WPS session */
				SLSI_NET_INFO(dev, "Ignoring Deauth notification to cfg80211 from the peer during WPS procedure\n");
			else {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 2, 0))
					cfg80211_disconnected(dev, reason, NULL, 0, false, GFP_KERNEL);
#else
					cfg80211_disconnected(dev, reason, NULL, 0, GFP_KERNEL);
#endif
					SLSI_NET_DBG3(dev, SLSI_MLME, "Received disconnect from AP, reason = %d\n", reason);
			}
		} else if (ndev_vif->sta.vif_status == SLSI_VIF_STATUS_DISCONNECTING) {
			/* Change keep alive and sync_loss reason code while sending to supplicant to a standard reason code */
			if (reason == FAPI_REASONCODE_KEEP_ALIVE_FAILURE ||
			    reason == FAPI_REASONCODE_SYNCHRONISATION_LOSS)
				reason = WLAN_REASON_DEAUTH_LEAVING;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 2, 0))
			cfg80211_disconnected(dev, reason, NULL, 0, true, GFP_KERNEL);
#else
			cfg80211_disconnected(dev, reason, NULL, 0, GFP_KERNEL);
#endif
			SLSI_NET_DBG3(dev, SLSI_MLME, "Completion of disconnect from AP\n");
		} else {
			/* Vif status is in erronus state.*/
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 2, 0))
			cfg80211_disconnected(dev, reason, NULL, 0, false, GFP_KERNEL);
#else
			cfg80211_disconnected(dev, reason, NULL, 0, GFP_KERNEL);
#endif
			SLSI_NET_WARN(dev, "disconnect in wrong state vif_status(%d)\n", ndev_vif->sta.vif_status);
		}

		ndev_vif->sta.is_wps = false;

		/* Populate bss records on incase of disconnection.
		 * For connection failure its not required.
		 */
		if (!(ndev_vif->sta.vif_status == SLSI_VIF_STATUS_CONNECTING ||
		      ndev_vif->sta.vif_status == SLSI_VIF_STATUS_UNSPECIFIED))
			slsi_populate_bss_record(dev);

		kfree(ndev_vif->sta.assoc_req_add_info_elem);
		if (ndev_vif->sta.assoc_req_add_info_elem) {
			ndev_vif->sta.assoc_req_add_info_elem = NULL;
			ndev_vif->sta.assoc_req_add_info_elem_len = 0;
		}
		slsi_mlme_del_vif(sdev, dev);
		slsi_vif_deactivated(sdev, dev);
		break;
	}
	case FAPI_VIFTYPE_AP:
	{
		struct slsi_peer *peer = NULL;

		peer = slsi_get_peer_from_mac(sdev, dev, peer_address);
		if (!peer) {
			SLSI_NET_DBG1(dev, SLSI_MLME, "peer NOT found by MAC address\n");
			goto exit;
		}

		SLSI_NET_DBG3(dev, SLSI_MLME, "MAC:%pM\n", peer_address);
		slsi_fill_last_disconnected_sta_info(sdev, dev, peer_address, reason);
		slsi_ps_port_control(sdev, dev, peer, SLSI_STA_CONN_STATE_DISCONNECTED);
		if ((peer->connected_state == SLSI_STA_CONN_STATE_CONNECTED) || (peer->connected_state == SLSI_STA_CONN_STATE_DOING_KEY_CONFIG))
			cfg80211_del_sta(dev, peer->address, GFP_KERNEL);

		slsi_peer_remove(sdev, dev, peer);

		/* If last client disconnects (after WPA2 handshake) then take wakelock till group is removed
		 * to avoid possibility of delay in group removal if platform suspends at this point.
		 */
		if (ndev_vif->ap.p2p_gc_keys_set && (ndev_vif->peer_sta_records == 0)) {
			SLSI_NET_DBG2(dev, SLSI_MLME, "P2PGO - Acquire wakelock after last client disconnection\n");
			slsi_wakelock(&sdev->wlan_wl);
		}
		break;
	}
	default:
		SLSI_NET_WARN(dev, "mlme_disconnect_ind(vif:%d, unexpected vif type:%d)\n", ndev_vif->ifnum, ndev_vif->vif_type);
		break;
	}
exit:
	return 0;
}

int slsi_ps_port_control(struct slsi_dev *sdev, struct net_device *dev, struct slsi_peer *peer, enum slsi_sta_conn_state s)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);

	SLSI_UNUSED_PARAMETER(sdev);

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	switch (s) {
	case SLSI_STA_CONN_STATE_DISCONNECTED:
		SLSI_NET_DBG1(dev, SLSI_TX, "STA disconnected, SET : FCQ - Disabled\n");
		peer->authorized = false;
		if (ndev_vif->vif_type == FAPI_VIFTYPE_AP && !ndev_vif->peer_sta_records)
			(void)scsc_wifi_fcq_8021x_port_state(dev, &ndev_vif->ap.group_data_qs, SCSC_WIFI_FCQ_8021x_STATE_BLOCKED);
		return scsc_wifi_fcq_8021x_port_state(dev, &peer->data_qs, SCSC_WIFI_FCQ_8021x_STATE_BLOCKED);

	case SLSI_STA_CONN_STATE_DOING_KEY_CONFIG:
		SLSI_NET_DBG1(dev, SLSI_TX, "STA doing KEY config, SET : FCQ - Disabled\n");
		peer->authorized = false;
		if (ndev_vif->vif_type == FAPI_VIFTYPE_AP && !ndev_vif->peer_sta_records)
			(void)scsc_wifi_fcq_8021x_port_state(dev, &ndev_vif->ap.group_data_qs, SCSC_WIFI_FCQ_8021x_STATE_BLOCKED);
		return scsc_wifi_fcq_8021x_port_state(dev, &peer->data_qs, SCSC_WIFI_FCQ_8021x_STATE_BLOCKED);

	case SLSI_STA_CONN_STATE_CONNECTED:
		SLSI_NET_DBG1(dev, SLSI_TX, "STA connected, SET : FCQ - Enabled\n");
		peer->authorized = true;
		if (ndev_vif->vif_type == FAPI_VIFTYPE_AP)
			(void)scsc_wifi_fcq_8021x_port_state(dev, &ndev_vif->ap.group_data_qs, SCSC_WIFI_FCQ_8021x_STATE_OPEN);
		return scsc_wifi_fcq_8021x_port_state(dev, &peer->data_qs, SCSC_WIFI_FCQ_8021x_STATE_OPEN);

	default:
		SLSI_NET_DBG1(dev, SLSI_TX, "SET : FCQ - Disabled\n");
		peer->authorized = false;
		if (ndev_vif->vif_type == FAPI_VIFTYPE_AP && !ndev_vif->peer_sta_records)
			(void)scsc_wifi_fcq_8021x_port_state(dev, &ndev_vif->ap.group_data_qs, SCSC_WIFI_FCQ_8021x_STATE_BLOCKED);
		return scsc_wifi_fcq_8021x_port_state(dev, &peer->data_qs, SCSC_WIFI_FCQ_8021x_STATE_BLOCKED);
	}

	return 0;
}

int slsi_set_uint_mib(struct slsi_dev *sdev, struct net_device *dev, u16 psid, int value)
{
	struct slsi_mib_data mib_data = { 0, NULL };
	int r = 0;

	SLSI_DBG2(sdev, SLSI_MLME, "UINT MIB Set Request (PSID = 0x%04X, Value = %d)\n", psid, value);

	r = slsi_mib_encode_uint(&mib_data, psid, value, 0);
	if (r == SLSI_MIB_STATUS_SUCCESS) {
		if (mib_data.dataLength) {
			r = slsi_mlme_set(sdev, dev, mib_data.data, mib_data.dataLength);
			if (r != 0)
				SLSI_ERR(sdev, "MIB (PSID = 0x%04X) set error = %d\n", psid, r);
			kfree(mib_data.data);
		}
	}
	return r;
}

int slsi_send_max_transmit_msdu_lifetime(struct slsi_dev *dev, struct net_device *ndev, u32 msdu_lifetime)
{
#ifdef CCX_MSDU_LIFETIME_MIB_NA
	struct slsi_mib_data mib_data = { 0, NULL };
	int error = 0;

	if (slsi_mib_encode_uint(&mib_data, SLSI_PSID_DOT11_MAX_TRANSMIT_MSDU_LIFETIME, msdu_lifetime, 0) == SLSI_MIB_STATUS_SUCCESS)
		if (mib_data.dataLength) {
			error = slsi_mlme_set(dev, ndev, mib_data.data, mib_data.dataLength);
			if (error)
				SLSI_ERR(dev, "Err Sending max msdu lifetime failed. error = %d\n", error);
			kfree(mib_data.data);
		}
	return error;
#endif
	/* TODO: current firmware do not have this MIB yet */
	return 0;
}

int slsi_read_max_transmit_msdu_lifetime(struct slsi_dev *dev, struct net_device *ndev, u32 *msdu_lifetime)
{
#ifdef CCX_MSDU_LIFETIME_MIB_NA
	struct slsi_mib_data mib_data = { 0, NULL };
	struct slsi_mib_data mib_res = { 0, NULL };
	struct slsi_mib_entry mib_val;
	int error = 0;
	int mib_rx_len = 0;
	size_t len;

	SLSI_UNUSED_PARAMETER(ndev);

	mib_res.dataLength = 10; /* PSID header(5) + dot11MaxReceiveLifetime 4 bytes + status(1) */
	mib_res.data = kmalloc(mib_res.dataLength, GFP_KERNEL);

	if (!mib_res.data)
		return -ENOMEM;

	slsi_mib_encode_get(&mib_data, SLSI_PSID_DOT11_MAX_TRANSMIT_MSDU_LIFETIME, 0);
	error = slsi_mlme_get(dev, NULL, mib_data.data, mib_data.dataLength,
			      mib_res.data, mib_res.dataLength, &mib_rx_len);
	kfree(mib_data.data);

	if (error) {
		SLSI_ERR(dev, "Err Reading max msdu lifetime failed. error = %d\n", error);
		kfree(mib_res.data);
		return error;
	}

	len = slsi_mib_decode(&mib_res, &mib_val);

	if (len != 8) {
		kfree(mib_res.data);
		return -EINVAL;
	}
	*msdu_lifetime = mib_val.value.u.uintValue;

	kfree(mib_res.data);

	return error;
#endif
	/* TODO: current firmware do not have this MIB yet */
	return 0;
}

void slsi_band_cfg_update(struct slsi_dev *sdev, int band)
{
	/* TODO: lock scan_mutex*/
	switch (band) {
	case SLSI_FREQ_BAND_AUTO:
		sdev->wiphy->bands[0] = sdev->device_config.band_2G;
		sdev->wiphy->bands[1] = sdev->device_config.band_5G;
		break;
	case SLSI_FREQ_BAND_5GHZ:
		sdev->wiphy->bands[0] = NULL;
		sdev->wiphy->bands[1] = sdev->device_config.band_5G;
		break;
	case SLSI_FREQ_BAND_2GHZ:
		sdev->wiphy->bands[0] = sdev->device_config.band_2G;
		sdev->wiphy->bands[1] = NULL;
		break;
	default:
		break;
	}
	wiphy_apply_custom_regulatory(sdev->wiphy, sdev->device_config.domain_info.regdomain);
	slsi_update_supported_channels_regd_flags(sdev);
}

int slsi_band_update(struct slsi_dev *sdev, int band)
{
	int i;
	struct net_device *dev;
	struct netdev_vif *ndev_vif;

	SLSI_MUTEX_LOCK(sdev->device_config_mutex);

	SLSI_DBG3(sdev, SLSI_CFG80211, "supported_band:%d\n", band);

	if (band == sdev->device_config.supported_band) {
		SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);
		return 0;
	}

	sdev->device_config.supported_band = band;

	slsi_band_cfg_update(sdev, band);

	SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);

	/* If new band is auto(2.4GHz + 5GHz, no need to check for station connection.*/
	if (band == 0)
		return 0;

	/* If station is connected on any rejected band, disconnect the station. */
	SLSI_MUTEX_LOCK(sdev->netdev_add_remove_mutex);
	for (i = 1; i < (CONFIG_SCSC_WLAN_MAX_INTERFACES + 1); i++) {
		dev = slsi_get_netdev_locked(sdev, i);
		if (!dev)
			break;
		ndev_vif = netdev_priv(dev);
		SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
		/**
		 * 1. vif should be activated and vif type should be station.
		 * 2. Station should be either in connecting or connected state.
		 * 3. if (new band is 5G and connection is on 2.4) or (new band is 2.4 and connection is 5)
		 * when all the above conditions are true drop the connection
		 * Do not wait for disconnect ind.
		 */
		if ((ndev_vif->activated) && (ndev_vif->vif_type == FAPI_VIFTYPE_STATION) &&
		    (ndev_vif->sta.vif_status == SLSI_VIF_STATUS_CONNECTING || ndev_vif->sta.vif_status == SLSI_VIF_STATUS_CONNECTED) &&
		    (ndev_vif->chan->hw_value <= 14 ? band == SLSI_FREQ_BAND_5GHZ : band == SLSI_FREQ_BAND_2GHZ)) {
			int r;

			if (!ndev_vif->sta.sta_bss) {
				SLSI_ERR(sdev, "slsi_mlme_disconnect failed, sta_bss is not available\n");
				SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
				SLSI_MUTEX_UNLOCK(sdev->netdev_add_remove_mutex);
				return -EINVAL;
			}

			r = slsi_mlme_disconnect(sdev, dev, ndev_vif->sta.sta_bss->bssid, WLAN_REASON_DEAUTH_LEAVING, true);
			LOG_CONDITIONALLY(r != 0, SLSI_ERR(sdev, "slsi_mlme_disconnect(%pM) failed with %d\n", ndev_vif->sta.sta_bss->bssid, r));

			r = slsi_handle_disconnect(sdev, dev, ndev_vif->sta.sta_bss->bssid, 0);
			LOG_CONDITIONALLY(r != 0, SLSI_ERR(sdev, "slsi_handle_disconnect(%pM) failed with %d\n", ndev_vif->sta.sta_bss->bssid, r));
		}
		SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	}
	SLSI_MUTEX_UNLOCK(sdev->netdev_add_remove_mutex);

	return 0;
}

/* This takes care to free the SKB on failure */
int slsi_send_gratuitous_arp(struct slsi_dev *sdev, struct net_device *dev)
{
	int ret = 0;
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff *arp;
	struct ethhdr *ehdr;
	static const u8 arp_hdr[] = { 0x00, 0x01, 0x08, 0x00, 0x06, 0x04, 0x00, 0x01 };
	int arp_size = sizeof(arp_hdr) + ETH_ALEN + sizeof(ndev_vif->ipaddress) + ETH_ALEN + sizeof(ndev_vif->ipaddress);

	SLSI_NET_DBG2(dev, SLSI_CFG80211, "\n");

	if (!ndev_vif->ipaddress)
		return 0;

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	if (WARN_ON(!ndev_vif->activated))
		return -EINVAL;
	if (WARN_ON(ndev_vif->vif_type != FAPI_VIFTYPE_STATION))
		return -EINVAL;
	if (WARN_ON(ndev_vif->sta.vif_status != SLSI_VIF_STATUS_CONNECTED))
		return -EINVAL;

	SLSI_NET_DBG2(dev, SLSI_CFG80211, "IP:%pI4\n", &ndev_vif->ipaddress);

	arp = slsi_alloc_skb(sizeof(struct ethhdr) + arp_size, GFP_KERNEL);
	if (WARN_ON(!arp))
		return -ENOMEM;

	/* The Ethernet header is accessed in the stack. */
	skb_reset_mac_header(arp);

	/* Ethernet Header */
	ehdr = (struct ethhdr *)skb_put(arp, sizeof(struct ethhdr));
	memset(ehdr->h_dest, 0xFF, ETH_ALEN);
	SLSI_ETHER_COPY(ehdr->h_source, dev->dev_addr);
	ehdr->h_proto = cpu_to_be16(ETH_P_ARP);

	/* Arp Data */
	memcpy(skb_put(arp, sizeof(arp_hdr)), arp_hdr, sizeof(arp_hdr));
	SLSI_ETHER_COPY(skb_put(arp, ETH_ALEN), dev->dev_addr);
	memcpy(skb_put(arp, sizeof(ndev_vif->ipaddress)), &ndev_vif->ipaddress, sizeof(ndev_vif->ipaddress));
	memset(skb_put(arp, ETH_ALEN), 0xFF, ETH_ALEN);
	memcpy(skb_put(arp, sizeof(ndev_vif->ipaddress)), &ndev_vif->ipaddress, sizeof(ndev_vif->ipaddress));

	arp->dev = dev;
	arp->protocol = ETH_P_ARP;
	arp->ip_summed = CHECKSUM_UNNECESSARY;
	arp->queue_mapping = slsi_netif_get_peer_queue(0, 0); /* Queueset 0 AC 0 */

	ret = slsi_tx_data(sdev, dev, arp);
	if (ret)
		slsi_kfree_skb(arp);

	return ret;
}

const u8 addr_mask[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
const u8 solicited_node_addr_mask[6] = { 0x33, 0x33, 0xff, 0x00, 0x00, 0x01 };

static void slsi_create_packet_filter_element(u8                               filterid,
					      u8                               pkt_filter_mode,
					      u8                               num_pattern_desc,
					      struct slsi_mlme_pattern_desc    *pattern_desc,
					      struct slsi_mlme_pkt_filter_elem *pkt_filter_elem,
					      u8                               *pkt_filters_len)
{
	u8 pkt_filter_hdr[SLSI_PKT_FILTER_ELEM_HDR_LEN] = { 0xdd,             /* vendor ie*/
							    0x00,             /*Length to be filled*/
							    0x00, 0x16, 0x32, /*oui*/
							    0x02,
							    filterid,         /*filter id to be filled*/
							    pkt_filter_mode   /* pkt filter mode to be filled */
	};
	u8 i, pattern_desc_len = 0;

	WARN_ON(num_pattern_desc > SLSI_MAX_PATTERN_DESC);

	memcpy(pkt_filter_elem->header, pkt_filter_hdr, SLSI_PKT_FILTER_ELEM_HDR_LEN);
	pkt_filter_elem->num_pattern_desc = num_pattern_desc;

	for (i = 0; i < num_pattern_desc; i++) {
		memcpy(&pkt_filter_elem->pattern_desc[i], &pattern_desc[i], sizeof(struct slsi_mlme_pattern_desc));
		pattern_desc_len += SLSI_PKT_DESC_FIXED_LEN + (2 * pattern_desc[i].mask_length);
	}

	/*Update the length in the header*/
	pkt_filter_elem->header[1] =  SLSI_PKT_FILTER_ELEM_FIXED_LEN + pattern_desc_len;
	*pkt_filters_len += (SLSI_PKT_FILTER_ELEM_HDR_LEN + pattern_desc_len);

	SLSI_DBG3_NODEV(SLSI_MLME, "filterid=0x%x,pkt_filter_mode=0x%x,num_pattern_desc=0x%x\n",
			filterid, pkt_filter_mode, num_pattern_desc);
}

#define SLSI_SCREEN_OFF_FILTERS_COUNT 1

static int slsi_set_common_packet_filters(struct slsi_dev *sdev, struct net_device *dev)
{
	struct slsi_mlme_pattern_desc pattern_desc;
	struct slsi_mlme_pkt_filter_elem pkt_filter_elem[1];
	u8 pkt_filters_len = 0, num_filters = 0;

	/*Opt out all broadcast and multicast packets (filter on I/G bit)*/
	pattern_desc.offset = 0;
	pattern_desc.mask_length = 1;
	pattern_desc.mask[0] = 0x01;
	pattern_desc.pattern[0] = 0x01;

	slsi_create_packet_filter_element(SLSI_ALL_BC_MC_FILTER_ID,
					  FAPI_PACKETFILTERMODE_OPT_OUT_SLEEP | FAPI_PACKETFILTERMODE_OPT_OUT,
					  1, &pattern_desc, &pkt_filter_elem[num_filters], &pkt_filters_len);
	num_filters++;
	return slsi_mlme_set_packet_filter(sdev, dev, pkt_filters_len, num_filters, pkt_filter_elem);
}

int  slsi_set_arp_packet_filter(struct slsi_dev *sdev, struct net_device *dev)
{
	struct slsi_mlme_pattern_desc pattern_desc[SLSI_MAX_PATTERN_DESC];
	int num_pattern_desc = 0;
	u8 pkt_filters_len = 0, num_filters = 0;
	struct slsi_mlme_pkt_filter_elem pkt_filter_elem[2];
	int ret;
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_peer *peer = slsi_get_peer_from_qs(sdev, dev, SLSI_STA_PEER_QUEUESET);

	if (WARN_ON(ndev_vif->vif_type != FAPI_VIFTYPE_STATION))
		return -EINVAL;

	if (WARN_ON(!peer))
		return -EINVAL;

	if (slsi_is_proxy_arp_supported_on_ap(peer->assoc_resp_ie))
		return 0;

	/*Set the IP address while suspending as this will be used by firmware for ARP/NDP offloading*/
	slsi_mlme_set_ip_address(sdev, dev);
#ifndef CONFIG_SCSC_WLAN_BLOCK_IPV6
	slsi_mlme_set_ipv6_address(sdev, dev);
#endif

	SLSI_NET_DBG2(dev, SLSI_MLME, "Set ARP filter\n");

	/*Opt in the broadcast ARP packets for Local IP address*/
	num_pattern_desc = 0;
	pattern_desc[num_pattern_desc].offset = 0; /*filtering on MAC destination Address*/
	pattern_desc[num_pattern_desc].mask_length = ETH_ALEN;
	SLSI_ETHER_COPY(pattern_desc[num_pattern_desc].mask, addr_mask);
	SLSI_ETHER_COPY(pattern_desc[num_pattern_desc].pattern, addr_mask);
	num_pattern_desc++;

	/*filter on ethertype ARP*/
	SET_ETHERTYPE_PATTERN_DESC(pattern_desc[num_pattern_desc], ETH_P_ARP);
	num_pattern_desc++;

	pattern_desc[num_pattern_desc].offset = 0x26; /*filtering on Target IP Address*/
	pattern_desc[num_pattern_desc].mask_length = 4;
	memcpy(pattern_desc[num_pattern_desc].mask, addr_mask, pattern_desc[num_pattern_desc].mask_length);
	memcpy(pattern_desc[num_pattern_desc].pattern, &ndev_vif->ipaddress, pattern_desc[num_pattern_desc].mask_length);
	num_pattern_desc++;

	slsi_create_packet_filter_element(SLSI_LOCAL_ARP_FILTER_ID, FAPI_PACKETFILTERMODE_OPT_IN,
					  num_pattern_desc, pattern_desc, &pkt_filter_elem[num_filters], &pkt_filters_len);
	num_filters++;

	ret = slsi_mlme_set_packet_filter(sdev, dev, pkt_filters_len, num_filters, pkt_filter_elem);
	if (ret)
		return ret;

#ifndef CONFIG_SCSC_WLAN_BLOCK_IPV6
	pkt_filters_len = 0;
	num_filters = 0;

	/*Opt in the multicast NS packets for Local IP address in active mode*/
	num_pattern_desc = 0;
	pattern_desc[num_pattern_desc].offset = 0; /*filtering on MAC destination Address*/
	pattern_desc[num_pattern_desc].mask_length = ETH_ALEN;
	SLSI_ETHER_COPY(pattern_desc[num_pattern_desc].mask, addr_mask);
	memcpy(pattern_desc[num_pattern_desc].pattern, solicited_node_addr_mask, 3);
	memcpy(&pattern_desc[num_pattern_desc].pattern[3], &ndev_vif->ipv6address.s6_addr[13], 3); /* last 3 bytes of IPv6 address*/
	num_pattern_desc++;

	/*filter on ethertype ARP*/
	SET_ETHERTYPE_PATTERN_DESC(pattern_desc[num_pattern_desc], 0x86DD);
	num_pattern_desc++;

	pattern_desc[num_pattern_desc].offset = 0x14; /*filtering on next header*/
	pattern_desc[num_pattern_desc].mask_length = 1;
	pattern_desc[num_pattern_desc].mask[0] = 0xff;
	pattern_desc[num_pattern_desc].pattern[0] = 0x3a;
	num_pattern_desc++;

	pattern_desc[num_pattern_desc].offset = 0x36; /*filtering on ICMP6 packet type*/
	pattern_desc[num_pattern_desc].mask_length = 1;
	pattern_desc[num_pattern_desc].mask[0] = 0xff;
	pattern_desc[num_pattern_desc].pattern[0] = 0x87; /* Neighbor Solicitation type in ICMPv6 */
	num_pattern_desc++;

	slsi_create_packet_filter_element(SLSI_LOCAL_NS_FILTER_ID, FAPI_PACKETFILTERMODE_OPT_IN,
					  num_pattern_desc, pattern_desc, &pkt_filter_elem[num_filters], &pkt_filters_len);
	num_filters++;

	ret = slsi_mlme_set_packet_filter(sdev, dev, pkt_filters_len, num_filters, pkt_filter_elem);
	if (ret)
		return ret;
#endif

	return ret;
}

static int  slsi_set_multicast_packet_filters(struct slsi_dev *sdev, struct net_device *dev)
{
	struct slsi_mlme_pattern_desc pattern_desc;
	u8 pkt_filters_len = 0, i, num_filters = 0;
	int ret = 0;
	struct slsi_mlme_pkt_filter_elem *pkt_filter_elem = NULL;
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	u8 mc_filter_id, mc_filter_count;

	/* Multicast packets for registered multicast addresses to be opted in on screen off*/
	SLSI_NET_DBG2(dev, SLSI_MLME, "Set mc filters ,count =%d\n", ndev_vif->sta.regd_mc_addr_count);

	mc_filter_count = ndev_vif->sta.regd_mc_addr_count;
	if (!mc_filter_count)
		return 0;

	pkt_filter_elem = kmalloc((mc_filter_count * sizeof(struct slsi_mlme_pkt_filter_elem)), GFP_KERNEL);
	if (!pkt_filter_elem) {
		SLSI_NET_ERR(dev, "ERROR Memory allocation failure\n");
		return -ENOMEM;
	}

	pattern_desc.offset = 0;
	pattern_desc.mask_length = ETH_ALEN;
	SLSI_ETHER_COPY(pattern_desc.mask, addr_mask);

	for (i = 0; i < mc_filter_count; i++) {
		SLSI_ETHER_COPY(pattern_desc.pattern, ndev_vif->sta.regd_mc_addr[i]);
		mc_filter_id = SLSI_REGD_MC_FILTER_ID + i;

		slsi_create_packet_filter_element(mc_filter_id,
						  FAPI_PACKETFILTERMODE_OPT_IN | FAPI_PACKETFILTERMODE_OPT_IN_SLEEP,
						  1, &pattern_desc, &pkt_filter_elem[num_filters], &pkt_filters_len);
		num_filters++;
	}

	ret = slsi_mlme_set_packet_filter(sdev, dev, pkt_filters_len, num_filters, pkt_filter_elem);
	kfree(pkt_filter_elem);

	return ret;
}

int  slsi_clear_packet_filters(struct slsi_dev *sdev, struct net_device *dev)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_peer *peer = slsi_get_peer_from_qs(sdev, dev, SLSI_STA_PEER_QUEUESET);

	u8 i, pkt_filters_len = 0;
	int num_filters = 0;
	int ret = 0;
	struct slsi_mlme_pkt_filter_elem *pkt_filter_elem;
	u8 mc_filter_id;

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	if (WARN_ON(ndev_vif->vif_type != FAPI_VIFTYPE_STATION))
		return -EINVAL;

	if (WARN_ON(!peer))
		return -EINVAL;

	SLSI_NET_DBG2(dev, SLSI_MLME, "Clear filters on Screen on");

	/*calculate number of filters*/
	num_filters = ndev_vif->sta.regd_mc_addr_count + SLSI_SCREEN_OFF_FILTERS_COUNT;
	if ((slsi_is_proxy_arp_supported_on_ap(peer->assoc_resp_ie)) == false) {
		num_filters++;
#ifndef CONFIG_SCSC_WLAN_BLOCK_IPV6
		num_filters++;
#endif
	}

	pkt_filter_elem = kmalloc((num_filters * sizeof(struct slsi_mlme_pkt_filter_elem)), GFP_KERNEL);
	if (!pkt_filter_elem) {
		SLSI_NET_ERR(dev, "ERROR Memory allocation failure");
		return -ENOMEM;
	}

	num_filters = 0;
	for (i = 0; i < ndev_vif->sta.regd_mc_addr_count; i++) {
		mc_filter_id = SLSI_REGD_MC_FILTER_ID + i;
		slsi_create_packet_filter_element(mc_filter_id, 0, 0, NULL, &pkt_filter_elem[num_filters], &pkt_filters_len);
		num_filters++;
	}
	if ((slsi_is_proxy_arp_supported_on_ap(peer->assoc_resp_ie)) == false) {
		slsi_create_packet_filter_element(SLSI_LOCAL_ARP_FILTER_ID, 0, 0, NULL, &pkt_filter_elem[num_filters], &pkt_filters_len);
		num_filters++;
#ifndef CONFIG_SCSC_WLAN_BLOCK_IPV6
		slsi_create_packet_filter_element(SLSI_LOCAL_NS_FILTER_ID, 0, 0, NULL, &pkt_filter_elem[num_filters], &pkt_filters_len);
		num_filters++;
#endif
	}

	slsi_create_packet_filter_element(SLSI_ALL_BC_MC_FILTER_ID, 0, 0, NULL, &pkt_filter_elem[num_filters], &pkt_filters_len);
	num_filters++;

	ret = slsi_mlme_set_packet_filter(sdev, dev, pkt_filters_len, num_filters, pkt_filter_elem);
	kfree(pkt_filter_elem);
	return ret;
}

int  slsi_update_packet_filters(struct slsi_dev *sdev, struct net_device *dev)
{
	int ret = 0;

	struct netdev_vif *ndev_vif = netdev_priv(dev);

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));
	WARN_ON(ndev_vif->vif_type != FAPI_VIFTYPE_STATION);

	ret = slsi_set_multicast_packet_filters(sdev, dev);
	if (ret)
		return ret;

	ret = slsi_set_arp_packet_filter(sdev, dev);
	if (ret)
		return ret;

	return slsi_set_common_packet_filters(sdev, dev);
}

#define IPV6_PF_PATTERN_MASK 0xf0
#define IPV6_PF_PATTERN 0x60

#ifdef CONFIG_SCSC_WLAN_DISABLE_NAT_KA
#define SLSI_ON_CONNECT_FILTERS_COUNT 2
#else
#define SLSI_ON_CONNECT_FILTERS_COUNT 3
#endif

void slsi_set_packet_filters(struct slsi_dev *sdev, struct net_device *dev)
{
	struct slsi_mlme_pattern_desc pattern_desc[SLSI_MAX_PATTERN_DESC];
	int num_pattern_desc = 0;
	u8 pkt_filters_len = 0;
	int num_filters = 0;

	struct slsi_mlme_pkt_filter_elem pkt_filter_elem[SLSI_ON_CONNECT_FILTERS_COUNT];
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_peer *peer = slsi_get_peer_from_qs(sdev, dev, SLSI_STA_PEER_QUEUESET);

	if (WARN_ON(!ndev_vif->activated))
		return;

	if (WARN_ON(ndev_vif->vif_type != FAPI_VIFTYPE_STATION))
		return;

	if (WARN_ON(!peer))
		return;

	if (WARN_ON(!peer->assoc_resp_ie))
		return;

#ifdef CONFIG_SCSC_WLAN_BLOCK_IPV6

	/*Opt out all IPv6 packets in active and suspended mode (ipv6 filtering)*/
	num_pattern_desc = 0;
	pattern_desc[num_pattern_desc].offset = 0x0E; /*filtering on IP Protocol version*/
	pattern_desc[num_pattern_desc].mask_length = 1;
	pattern_desc[num_pattern_desc].mask[0] = IPV6_PF_PATTERN_MASK;
	pattern_desc[num_pattern_desc].pattern[0] = IPV6_PF_PATTERN;
	num_pattern_desc++;

	slsi_create_packet_filter_element(SLSI_ALL_IPV6_PKTS_FILTER_ID,
					  FAPI_PACKETFILTERMODE_OPT_OUT | FAPI_PACKETFILTERMODE_OPT_OUT_SLEEP,
					  num_pattern_desc, pattern_desc, &pkt_filter_elem[num_filters], &pkt_filters_len);
	num_filters++;

#endif

	if (slsi_is_proxy_arp_supported_on_ap(peer->assoc_resp_ie)) {
		SLSI_NET_DBG1(dev, SLSI_CFG80211, "Proxy ARP service supported on AP");

		/* Opt out Gratuitous ARP packets (ARP Announcement) in active and suspended mode.
		 * For suspended mode, gratituous ARP is dropped by "opt out all broadcast" that will be
		 * set  in slsi_set_common_packet_filters on screen off
		 */
		num_pattern_desc = 0;
		pattern_desc[num_pattern_desc].offset = 0; /*filtering on MAC destination Address*/
		pattern_desc[num_pattern_desc].mask_length = ETH_ALEN;
		SLSI_ETHER_COPY(pattern_desc[num_pattern_desc].mask, addr_mask);
		SLSI_ETHER_COPY(pattern_desc[num_pattern_desc].pattern, addr_mask);
		num_pattern_desc++;

		SET_ETHERTYPE_PATTERN_DESC(pattern_desc[num_pattern_desc], ETH_P_ARP);
		num_pattern_desc++;

		slsi_create_packet_filter_element(SLSI_PROXY_ARP_FILTER_ID, FAPI_PACKETFILTERMODE_OPT_OUT,
						  num_pattern_desc, pattern_desc, &pkt_filter_elem[num_filters], &pkt_filters_len);
		num_filters++;

#ifndef CONFIG_SCSC_WLAN_BLOCK_IPV6
		/* Opt out unsolicited Neighbor Advertisement packets .For suspended mode, NA is dropped by
		 * "opt out all IPv6 multicast" already set in slsi_create_common_packet_filters
		 */

		num_pattern_desc = 0;

		pattern_desc[num_pattern_desc].offset = 0; /*filtering on MAC destination Address*/
		pattern_desc[num_pattern_desc].mask_length = ETH_ALEN;
		SLSI_ETHER_COPY(pattern_desc[num_pattern_desc].mask, addr_mask);
		SLSI_ETHER_COPY(pattern_desc[num_pattern_desc].pattern, solicited_node_addr_mask);
		num_pattern_desc++;

		SET_ETHERTYPE_PATTERN_DESC(pattern_desc[num_pattern_desc], 0x86DD);
		num_pattern_desc++;

		pattern_desc[num_pattern_desc].offset = 0x14; /*filtering on next header*/
		pattern_desc[num_pattern_desc].mask_length = 1;
		pattern_desc[num_pattern_desc].mask[0] = 0xff;
		pattern_desc[num_pattern_desc].pattern[0] = 0x3a;
		num_pattern_desc++;

		pattern_desc[num_pattern_desc].offset = 0x36; /*filtering on ICMP6 packet type*/
		pattern_desc[num_pattern_desc].mask_length = 1;
		pattern_desc[num_pattern_desc].mask[0] = 0xff;
		pattern_desc[num_pattern_desc].pattern[0] = 0x88; /* Neighbor Advertisement type in ICMPv6 */
		num_pattern_desc++;

		slsi_create_packet_filter_element(SLSI_PROXY_ARP_NA_FILTER_ID, FAPI_PACKETFILTERMODE_OPT_OUT,
						  num_pattern_desc, pattern_desc, &pkt_filter_elem[num_filters], &pkt_filters_len);
		num_filters++;

#endif
	}

#ifndef CONFIG_SCSC_WLAN_DISABLE_NAT_KA
	{
		const u8 nat_ka_pattern[4] = { 0x11, 0x94, 0x00, 0x09 };
		/*Opt out the NAT T for IPsec*/
		num_pattern_desc = 0;
		pattern_desc[num_pattern_desc].offset = 0x24; /*filtering on destination port number*/
		pattern_desc[num_pattern_desc].mask_length = 4;
		memcpy(pattern_desc[num_pattern_desc].mask, addr_mask, 4);
		memcpy(pattern_desc[num_pattern_desc].pattern, nat_ka_pattern, 4);
		num_pattern_desc++;

		slsi_create_packet_filter_element(SLSI_NAT_IPSEC_FILTER_ID, FAPI_PACKETFILTERMODE_OPT_OUT_SLEEP,
						  num_pattern_desc, pattern_desc, &pkt_filter_elem[num_filters], &pkt_filters_len);
		num_filters++;
	}
#endif

	if (num_filters)
		slsi_mlme_set_packet_filter(sdev, dev, pkt_filters_len, num_filters, pkt_filter_elem);
}

int slsi_ip_address_changed(struct slsi_dev *sdev, struct net_device *dev, __be32 ipaddress)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	int ret = 0;

	/* Store the IP address outside the check for vif being active
	 * as we get the same notification in case of static IP
	 */
	if (ndev_vif->ipaddress != ipaddress)
		ndev_vif->ipaddress = ipaddress;

	if (ndev_vif->activated && (ndev_vif->vif_type == FAPI_VIFTYPE_AP)) {
		struct slsi_mlme_pattern_desc pattern_desc[1];
		u8 num_patterns = 0;
		struct slsi_mlme_pkt_filter_elem pkt_filter_elem[1];
		u8 pkt_filters_len = 0;
		u8 num_filters = 0;

		ndev_vif->ipaddress = ipaddress;
		ret = slsi_mlme_set_ip_address(sdev, dev);
		if (ret != 0)
			SLSI_NET_ERR(dev, "slsi_mlme_set_ip_address ERROR. ret=%d", ret);

		/* Opt out IPv6 packets in platform suspended mode */
		pattern_desc[num_patterns].offset = 0x0E;
		pattern_desc[num_patterns].mask_length = 0x01;
		pattern_desc[num_patterns].mask[0] = IPV6_PF_PATTERN_MASK;
		pattern_desc[num_patterns++].pattern[0] = IPV6_PF_PATTERN;

		slsi_create_packet_filter_element(SLSI_AP_ALL_IPV6_PKTS_FILTER_ID, FAPI_PACKETFILTERMODE_OPT_OUT_SLEEP,
						  num_patterns, pattern_desc, &pkt_filter_elem[num_filters], &pkt_filters_len);
		num_filters++;
		ret = slsi_mlme_set_packet_filter(sdev, dev, pkt_filters_len, num_filters, pkt_filter_elem);
		if (ret != 0)
			SLSI_NET_ERR(dev, "slsi_mlme_set_packet_filter (return :%d) ERROR\n", ret);
	} else if ((ndev_vif->activated) &&
		   (ndev_vif->vif_type == FAPI_VIFTYPE_STATION) &&
		   (ndev_vif->sta.vif_status == SLSI_VIF_STATUS_CONNECTED)) {
		struct slsi_peer *peer = slsi_get_peer_from_qs(sdev, dev, SLSI_STA_PEER_QUEUESET);

		if (WARN_ON(!peer))
			return -EINVAL;

		if (!(peer->capabilities & WLAN_CAPABILITY_PRIVACY) ||
		    (ndev_vif->sta.group_key_set && peer->pairwise_key_set))
			slsi_send_gratuitous_arp(sdev, dev);
		else
			ndev_vif->sta.gratuitous_arp_needed = true;

		slsi_mlme_powermgt(sdev, dev, ndev_vif->set_power_mode);
	}

	return ret;
}

#define SLSI_AP_AUTO_CHANLS_LIST_FROM_HOSTAPD_MAX 3

int slsi_auto_chan_select_scan(struct slsi_dev *sdev, int n_channels, struct ieee80211_channel *channels[])
{
	struct net_device *dev;
	struct netdev_vif *ndev_vif;
	struct sk_buff_head unique_scan_results;
	int scan_result_count[SLSI_AP_AUTO_CHANLS_LIST_FROM_HOSTAPD_MAX] = { 0, 0, 0 };
	int i, j;
	int r = 0;
	int selected_index = 0;
	int min_index = 0;
	u32 freqdiff = 0;

	if (slsi_is_test_mode_enabled()) {
		SLSI_WARN(sdev, "not supported in WlanLite mode\n");
		return -EOPNOTSUPP;
	}

	skb_queue_head_init(&unique_scan_results);

	dev = slsi_get_netdev(sdev, SLSI_NET_INDEX_WLAN); /* use the main VIF */
	if (!dev) {
		r = -EINVAL;
		return r;
	}

	ndev_vif = netdev_priv(dev);
	SLSI_MUTEX_LOCK(ndev_vif->scan_mutex);

	if (ndev_vif->scan[SLSI_SCAN_HW_ID].scan_req) {
		r = -EBUSY;
		goto exit_with_vif;
	}
	ndev_vif->scan[SLSI_SCAN_HW_ID].is_blocking_scan = true;
	r = slsi_mlme_add_scan(sdev,
			       dev,
			       FAPI_SCANTYPE_AP_AUTO_CHANNEL_SELECTION,
			       FAPI_REPORTMODE_REAL_TIME,
			       0,    /* n_ssids */
			       NULL, /* ssids */
			       n_channels,
			       channels,
			       NULL,
			       NULL,                   /* ie */
			       0,                      /* ie_len */
			       ndev_vif->scan[SLSI_SCAN_HW_ID].is_blocking_scan);

	if (r == 0) {
		struct sk_buff *unique_scan;
		struct sk_buff *scan;

		SLSI_MUTEX_LOCK(ndev_vif->scan_result_mutex);
		scan = slsi_dequeue_cached_scan_result(&ndev_vif->scan[SLSI_SCAN_HW_ID], NULL);
		while (scan) {
			struct ieee80211_mgmt *mgmt = fapi_get_mgmt(scan);
			struct ieee80211_channel *channel;

			/* make sure this BSSID has not already been used */
			skb_queue_walk(&unique_scan_results, unique_scan) {
				struct ieee80211_mgmt *unique_mgmt = fapi_get_mgmt(unique_scan);

				if (compare_ether_addr(mgmt->bssid, unique_mgmt->bssid) == 0) {
					slsi_kfree_skb(scan);
					goto next_scan;
				}
			}

			slsi_skb_queue_head(&unique_scan_results, scan);

			channel = slsi_find_scan_channel(sdev, mgmt, fapi_get_mgmtlen(scan), fapi_get_u16(scan, u.mlme_scan_ind.channel_frequency) / 2);
			if (!channel)
				goto next_scan;

			/* check for interfering channels for 1, 6 and 11 */
			for (i = 0, j = 0; i < SLSI_AP_AUTO_CHANLS_LIST_FROM_HOSTAPD_MAX && channels[j]; i++, j = j + 5) {
				if (channel->center_freq == channels[j]->center_freq) {
					SLSI_NET_DBG3(dev, SLSI_CFG80211, "exact match:%d\n", i);
					scan_result_count[i] += 5;
					goto next_scan;
				}
				freqdiff = abs((int)channel->center_freq - (channels[j]->center_freq));
				if (freqdiff <= 20) {
					SLSI_NET_DBG3(dev, SLSI_CFG80211, "overlapping:%d, freqdiff:%d\n", i, freqdiff);
					scan_result_count[i] += (5 - (freqdiff / 5));
				}
			}

next_scan:
			scan = slsi_dequeue_cached_scan_result(&ndev_vif->scan[SLSI_SCAN_HW_ID], NULL);
		}
		SLSI_MUTEX_UNLOCK(ndev_vif->scan_result_mutex);

		/* Select the channel to use */
		for (i = 0, j = 0; i < SLSI_AP_AUTO_CHANLS_LIST_FROM_HOSTAPD_MAX; i++, j = j + 5) {
			SLSI_NET_DBG3(dev, SLSI_CFG80211, "score[%d]:%d\n", i, scan_result_count[i]);
			if (scan_result_count[i] <= scan_result_count[min_index]) {
				min_index = i;
				selected_index = j;
			}
		}
		SLSI_NET_DBG3(dev, SLSI_CFG80211, "selected:%d with score:%d\n", selected_index, scan_result_count[min_index]);

		SLSI_MUTEX_LOCK(sdev->device_config_mutex);
		sdev->device_config.ap_auto_chan = channels[selected_index]->hw_value & 0xFF;
		SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);

		SLSI_INFO(sdev, "Channel selected = %d", sdev->device_config.ap_auto_chan);
	}
	slsi_skb_queue_purge(&unique_scan_results);
	ndev_vif->scan[SLSI_SCAN_HW_ID].is_blocking_scan = false;

exit_with_vif:
	SLSI_MUTEX_UNLOCK(ndev_vif->scan_mutex);
	return r;
}

int slsi_set_boost(struct slsi_dev *sdev, struct net_device *dev)
{
	int error = 0;

	SLSI_MUTEX_LOCK(sdev->device_config_mutex);
	error = slsi_set_mib_rssi_boost(sdev, dev, SLSI_PSID_UNIFI_ROAM_RSSI_BOOST, 1,
					sdev->device_config.rssi_boost_2g);
	if (error)
		SLSI_ERR(sdev, "Err setting boost value For 2g after adding vif. error = %d\n", error);
	error = slsi_set_mib_rssi_boost(sdev, dev, SLSI_PSID_UNIFI_ROAM_RSSI_BOOST, 2,
					sdev->device_config.rssi_boost_5g);
	if (error)
		SLSI_ERR(sdev, "Err setting boost value for 5g after adding vif . error = %d\n", error);
	SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);
	return error;
}

/**
 * Work to be done when ROC retention duration expires:
 * Send ROC expired event to cfg80211 and queue work to delete unsync vif after retention timeout.
 */
static void slsi_p2p_roc_duration_expiry_work(struct work_struct *work)
{
	struct netdev_vif *ndev_vif = container_of((struct delayed_work *)work, struct netdev_vif, unsync.roc_expiry_work);

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	/* There can be a race condition of this work function waiting for ndev_vif->vif_mutex and meanwhile the vif is deleted (due to net_stop).
	 * In such cases ndev_vif->chan would have been cleared.
	 */
	if (ndev_vif->sdev->p2p_state == P2P_IDLE_NO_VIF) {
		SLSI_NET_DBG1(ndev_vif->wdev.netdev, SLSI_CFG80211, "P2P unsync vif is not present\n");
		goto exit;
	}

	SLSI_NET_DBG3(ndev_vif->wdev.netdev, SLSI_CFG80211, "Send ROC expired event\n");

	/* If action frame tx is in progress don't schedule work to delete vif */
	if (ndev_vif->sdev->p2p_state != P2P_ACTION_FRAME_TX_RX) {
		slsi_p2p_queue_unsync_vif_del_work(ndev_vif, SLSI_P2P_UNSYNC_VIF_EXTRA_MSEC);
		SLSI_P2P_STATE_CHANGE(ndev_vif->sdev, P2P_IDLE_VIF_ACTIVE);
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 9))
	cfg80211_remain_on_channel_expired(&ndev_vif->wdev, ndev_vif->unsync.roc_cookie, ndev_vif->chan, GFP_KERNEL);
#else
	cfg80211_remain_on_channel_expired(ndev_vif->wdev.netdev, ndev_vif->unsync.roc_cookie,
					   ndev_vif->chan, ndev_vif->channel_type, GFP_KERNEL);
#endif

exit:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
}

/**
 * Work to be done when unsync vif retention duration expires:
 * Delete the unsync vif.
 */
static void slsi_p2p_unsync_vif_delete_work(struct work_struct *work)
{
	struct netdev_vif *ndev_vif = container_of((struct delayed_work *)work, struct netdev_vif, unsync.del_vif_work);

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	SLSI_NET_DBG1(ndev_vif->wdev.netdev, SLSI_CFG80211, "Delete vif duration expired - Deactivate unsync vif\n");
	slsi_p2p_vif_deactivate(ndev_vif->sdev, ndev_vif->wdev.netdev, true);
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
}

/* Initializations for P2P - Change vif type to unsync, create workqueue and init work */
int slsi_p2p_init(struct slsi_dev *sdev, struct netdev_vif *ndev_vif)
{
	SLSI_DBG1(sdev, SLSI_INIT_DEINIT, "Initialize P2P - Init P2P state to P2P_IDLE_NO_VIF\n");
	sdev->p2p_state = P2P_IDLE_NO_VIF;
	sdev->p2p_group_exp_frame = SLSI_P2P_PA_INVALID;

	ndev_vif->vif_type = FAPI_VIFTYPE_UNSYNCHRONISED;

	INIT_DELAYED_WORK(&ndev_vif->unsync.roc_expiry_work, slsi_p2p_roc_duration_expiry_work);
	INIT_DELAYED_WORK(&ndev_vif->unsync.del_vif_work, slsi_p2p_unsync_vif_delete_work);

	return 0;
}

/* De-initializations for P2P - Reset vif type, cancel work and destroy workqueue */
void slsi_p2p_deinit(struct slsi_dev *sdev, struct netdev_vif *ndev_vif)
{
	SLSI_DBG1(sdev, SLSI_INIT_DEINIT, "De-initialize P2P\n");

	ndev_vif->vif_type = SLSI_VIFTYPE_UNSPECIFIED;

	/* Work should have been cleaned up by now */
	if (WARN_ON(delayed_work_pending(&ndev_vif->unsync.del_vif_work)))
		cancel_delayed_work(&ndev_vif->unsync.del_vif_work);

	if (WARN_ON(delayed_work_pending(&ndev_vif->unsync.roc_expiry_work)))
		cancel_delayed_work(&ndev_vif->unsync.roc_expiry_work);
}

/**
 * P2P vif activation:
 * Add unsync vif, register for action frames, configure Probe Rsp IEs if required and set channel
 */
int slsi_p2p_vif_activate(struct slsi_dev *sdev, struct net_device *dev, struct ieee80211_channel *chan, u16 duration, bool set_probe_rsp_ies)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	u32 af_bmap_active = SLSI_ACTION_FRAME_PUBLIC;
	u32 af_bmap_suspended = SLSI_ACTION_FRAME_PUBLIC;
	int r = 0;

	SLSI_DBG1(sdev, SLSI_INIT_DEINIT, "Activate P2P unsync vif\n");

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	/* Interface address and device address are same for P2P unsync vif */
	if (slsi_mlme_add_vif(sdev, dev, dev->dev_addr, dev->dev_addr) != 0) {
		SLSI_NET_ERR(dev, "slsi_mlme_add_vif failed for unsync vif\n");
		goto exit_with_error;
	}

	ndev_vif->activated = true;
	SLSI_P2P_STATE_CHANGE(sdev, P2P_IDLE_VIF_ACTIVE);

	if (slsi_mlme_register_action_frame(sdev, dev, af_bmap_active, af_bmap_suspended) != 0) {
		SLSI_NET_ERR(dev, "Action frame registration failed for unsync vif\n");
		goto exit_with_vif;
	}

	if (set_probe_rsp_ies) {
		u16 purpose = FAPI_PURPOSE_PROBE_RESPONSE;

		if (!ndev_vif->unsync.probe_rsp_ies) {
			SLSI_NET_ERR(dev, "Probe Response IEs not available for ROC\n");
			goto exit_with_vif;
		}

		if (slsi_mlme_add_info_elements(sdev, dev, purpose, ndev_vif->unsync.probe_rsp_ies, ndev_vif->unsync.probe_rsp_ies_len) != 0) {
			SLSI_NET_ERR(dev, "Setting Probe Response IEs for unsync vif failed\n");
			goto exit_with_vif;
		}
		ndev_vif->unsync.ies_changed = false;
	}

	if (slsi_mlme_set_channel(sdev, dev, chan, SLSI_FW_CHANNEL_DURATION_UNSPECIFIED, 0, 0) != 0) {
		SLSI_NET_ERR(dev, "Set channel failed for unsync vif\n");
		goto exit_with_vif;
	} else {
		ndev_vif->chan = chan;
	}

	ndev_vif->mgmt_tx_data.exp_frame = SLSI_P2P_PA_INVALID;
	goto exit;

exit_with_vif:
	slsi_p2p_vif_deactivate(sdev, dev, true);
exit_with_error:
	r = -EINVAL;
exit:
	return r;
}

/* Delete unsync vif - DON'T update the vif type */
void slsi_p2p_vif_deactivate(struct slsi_dev *sdev, struct net_device *dev, bool hw_available)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);

	SLSI_NET_DBG1(dev, SLSI_INIT_DEINIT, "De-activate P2P unsync vif\n");

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	if (sdev->p2p_state == P2P_IDLE_NO_VIF) {
		SLSI_NET_DBG1(dev, SLSI_INIT_DEINIT, "P2P unsync vif already deactivated\n");
		return;
	}

	/* Indicate failure using cfg80211_mgmt_tx_status() if frame TX is not completed during VIF delete */
	if (ndev_vif->mgmt_tx_data.exp_frame != SLSI_P2P_PA_INVALID) {
		ndev_vif->mgmt_tx_data.exp_frame = SLSI_P2P_PA_INVALID;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
		cfg80211_mgmt_tx_status(&ndev_vif->wdev, ndev_vif->mgmt_tx_data.cookie, ndev_vif->mgmt_tx_data.buf, ndev_vif->mgmt_tx_data.buf_len, false, GFP_KERNEL);
#else
		cfg80211_mgmt_tx_status(dev, ndev_vif->mgmt_tx_data.cookie, ndev_vif->mgmt_tx_data.buf, ndev_vif->mgmt_tx_data.buf_len, false, GFP_KERNEL);
#endif
	}

	cancel_delayed_work(&ndev_vif->unsync.del_vif_work);
	cancel_delayed_work(&ndev_vif->unsync.roc_expiry_work);

	if (hw_available)
		slsi_mlme_del_vif(sdev, dev);

	SLSI_P2P_STATE_CHANGE(sdev, P2P_IDLE_NO_VIF);

	/* slsi_vif_deactivated is not used here after del_vif as it modifies vif type as well */

	ndev_vif->activated = false;
	ndev_vif->chan = NULL;

	if (WARN_ON(ndev_vif->unsync.listen_offload))
		ndev_vif->unsync.listen_offload = false;

	slsi_unsync_vif_set_probe_rsp_ie(ndev_vif, NULL, 0);
	(void)slsi_set_mgmt_tx_data(ndev_vif, 0, 0, NULL, 0);

	SLSI_NET_DBG2(dev, SLSI_INIT_DEINIT, "P2P unsync vif deactivated\n");
}

/**
 * Delete unsync vif when group role is being started.
 * For such cases the net_device during the call would be of the group interface (called from ap_start/connect).
 * Hence get the net_device using P2P Index. Take the mutex lock and call slsi_p2p_vif_deactivate.
 */
void slsi_p2p_group_start_remove_unsync_vif(struct slsi_dev *sdev)
{
	struct net_device *dev = NULL;
	struct netdev_vif *ndev_vif = NULL;

	SLSI_DBG1(sdev, SLSI_INIT_DEINIT, "Starting P2P Group - Remove unsync vif\n");

	dev = slsi_get_netdev(sdev, SLSI_NET_INDEX_P2P);
	if (!dev) {
		SLSI_ERR(sdev, "Failed to deactivate p2p vif as dev is not found\n");
		return;
	}

	ndev_vif = netdev_priv(dev);

	if (WARN_ON(!(SLSI_IS_P2P_UNSYNC_VIF(ndev_vif))))
		return;

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	slsi_p2p_vif_deactivate(sdev, dev, true);
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
}

/**
 * Called only for P2P Device mode (p2p0 interface) to store the Probe Response IEs
 * which would be used in Listen (ROC) state.
 * If the IEs are received in Listen Offload mode, then configure the IEs in firmware.
 */
int slsi_p2p_dev_probe_rsp_ie(struct slsi_dev *sdev, struct net_device *dev, u8 *probe_rsp_ie, size_t probe_rsp_ie_len)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	int ret = 0;

	SLSI_UNUSED_PARAMETER(sdev);

	if (!SLSI_IS_P2P_UNSYNC_VIF(ndev_vif)) {
		SLSI_NET_ERR(dev, "Incorrect vif type - Not unsync vif\n");
		kfree(probe_rsp_ie);
		return -EINVAL;
	}

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	SLSI_NET_DBG2(dev, SLSI_CFG80211, "Received Probe Rsp IE len = %zu, Current IE len = %zu\n", probe_rsp_ie_len, ndev_vif->unsync.probe_rsp_ies_len);

	if (!ndev_vif->unsync.listen_offload) { /* ROC */
		/* Store the IEs. Upon receiving it on subsequent occassions, store only if IEs have changed */
		if (ndev_vif->unsync.probe_rsp_ies_len != probe_rsp_ie_len)                           /* Check if IE length changed */
			ndev_vif->unsync.ies_changed = true;
		else if (memcmp(ndev_vif->unsync.probe_rsp_ies, probe_rsp_ie, probe_rsp_ie_len) != 0) /* Check if IEs changed */
			ndev_vif->unsync.ies_changed = true;
		else {                                                                                    /* No change in IEs */
			kfree(probe_rsp_ie);
			goto exit;
		}

		slsi_unsync_vif_set_probe_rsp_ie(ndev_vif, probe_rsp_ie, probe_rsp_ie_len);
	} else {	/* P2P Listen Offloading */
		if (sdev->p2p_state == P2P_LISTENING) {
			ret = slsi_mlme_add_info_elements(sdev, dev, FAPI_PURPOSE_PROBE_RESPONSE, probe_rsp_ie, probe_rsp_ie_len);
			if (ret != 0) {
				SLSI_NET_ERR(dev, "Listen Offloading: Setting Probe Response IEs for unsync vif failed\n");
				ndev_vif->unsync.listen_offload = false;
				slsi_p2p_vif_deactivate(sdev, dev, true);
			}
		}
	}

exit:
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	return ret;
}

/**
 * This should be called only for P2P Device mode (p2p0 interface). NULL IEs to clear Probe Response IEs are not updated
 * in driver to avoid configuring the Probe Response IEs to firmware on every ROC.
 * Use this call as a cue to stop any ongoing P2P scan as there is no API from user space for cancelling scan.
 * If ROC was in progress as part of P2P_FIND then Cancel ROC will be received.
 */
int slsi_p2p_dev_null_ies(struct slsi_dev *sdev, struct net_device *dev)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);

	if (!SLSI_IS_P2P_UNSYNC_VIF(ndev_vif)) {
		SLSI_NET_ERR(dev, "Incorrect vif type - Not unsync vif\n");
		return -EINVAL;
	}

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	SLSI_NET_DBG3(dev, SLSI_CFG80211, "Probe Rsp NULL IEs\n");

	if (sdev->p2p_state == P2P_SCANNING) {
		struct sk_buff *scan_result;

		SLSI_MUTEX_LOCK(ndev_vif->scan_mutex);

		SLSI_NET_DBG1(dev, SLSI_CFG80211, "Stop Find - Abort ongoing P2P scan\n");

		(void)slsi_mlme_del_scan(sdev, dev, ((ndev_vif->ifnum << 8) | SLSI_SCAN_HW_ID), false);

		SLSI_MUTEX_LOCK(ndev_vif->scan_result_mutex);
		scan_result = slsi_dequeue_cached_scan_result(&ndev_vif->scan[SLSI_SCAN_HW_ID], NULL);
		while (scan_result) {
			slsi_rx_scan_pass_to_cfg80211(sdev, dev, scan_result);
			scan_result = slsi_dequeue_cached_scan_result(&ndev_vif->scan[SLSI_SCAN_HW_ID], NULL);
		}
		SLSI_MUTEX_UNLOCK(ndev_vif->scan_result_mutex);

		WARN_ON(!ndev_vif->scan[SLSI_SCAN_HW_ID].scan_req);

		if (ndev_vif->scan[SLSI_SCAN_HW_ID].scan_req)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0))
			cfg80211_scan_done(ndev_vif->scan[SLSI_SCAN_HW_ID].scan_req, &info);
#else
			cfg80211_scan_done(ndev_vif->scan[SLSI_SCAN_HW_ID].scan_req, true);
#endif

		ndev_vif->scan[SLSI_SCAN_HW_ID].scan_req = NULL;

		SLSI_MUTEX_UNLOCK(ndev_vif->scan_mutex);

		if (ndev_vif->activated) {
			/* Supplicant has stopped FIND. Also clear Probe Response IEs in firmware and driver
			 * as Cancel ROC will not be sent as driver was not in Listen
			 */
			SLSI_NET_DBG1(dev, SLSI_CFG80211, "Stop Find - Clear Probe Response IEs in firmware\n");
			if (slsi_mlme_add_info_elements(sdev, dev, FAPI_PURPOSE_PROBE_RESPONSE, NULL, 0) != 0)
				SLSI_NET_ERR(dev, "Clearing Probe Response IEs failed for unsync vif\n");
			slsi_unsync_vif_set_probe_rsp_ie(ndev_vif, NULL, 0);

			SLSI_P2P_STATE_CHANGE(sdev, P2P_IDLE_VIF_ACTIVE);
		} else {
			SLSI_P2P_STATE_CHANGE(sdev, P2P_IDLE_NO_VIF);
		}
	}

	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
	return 0;
}

/**
 * Returns the P2P public action frame subtype.
 * Returns SLSI_P2P_PA_INVALID if it is not a P2P public action frame.
 */
int slsi_p2p_get_public_action_subtype(const struct ieee80211_mgmt *mgmt)
{
	int subtype = SLSI_P2P_PA_INVALID;
	/* Vendor specific Public Action (0x09), P2P OUI (0x50, 0x6f, 0x9a), P2P Subtype (0x09) */
	u8 p2p_pa_frame[5] = { 0x09, 0x50, 0x6f, 0x9a, 0x09 };
	u8 *action = (u8 *)&mgmt->u.action.u;

	if (memcmp(&action[0], p2p_pa_frame, 5) == 0) {
		subtype = action[5];
	} else {
		/* For service discovery action frames dummy subtype is used */
		switch (action[0]) {
		case SLSI_PA_GAS_INITIAL_REQ:
		case SLSI_PA_GAS_INITIAL_RSP:
		case SLSI_PA_GAS_COMEBACK_REQ:
		case SLSI_PA_GAS_COMEBACK_RSP:
			subtype = (action[0] | SLSI_PA_GAS_DUMMY_SUBTYPE_MASK);
			break;
		}
	}

	return subtype;
}

/**
 * Returns the P2P status code of Status attribute of the GO Neg Rsp frame.
 * Returns -1 if status attribute is NOT found.
 */
int slsi_p2p_get_go_neg_rsp_status(struct net_device *dev, const struct ieee80211_mgmt *mgmt)
{
	int status = -1;
	u8 p2p_oui_type[4] = { 0x50, 0x6f, 0x9a, 0x09 };
	u8 *action = (u8 *)&mgmt->u.action.u;
	u8 *vendor_ie = &action[7];             /* 1 (0x09), 4 (0x50, 0x6f, 0x9a, 0x09), 1 (0x01), 1 (Dialog Token) */
	u8 ie_length, elem_idx;
	u16 attr_length;

	while (vendor_ie && (*vendor_ie == SLSI_WLAN_EID_VENDOR_SPECIFIC)) {
		ie_length = vendor_ie[1];

		if (memcmp(&vendor_ie[2], p2p_oui_type, 4) == 0) {
			elem_idx = 6; /* 1 (Id - 0xdd) + 1 (Length) + 4 (OUI and Type) */

			while (ie_length > elem_idx) {
				attr_length = ((vendor_ie[elem_idx + 1]) | (vendor_ie[elem_idx + 2] << 8));

				if (vendor_ie[elem_idx] == SLSI_P2P_STATUS_ATTR_ID) {
					SLSI_NET_DBG3(dev, SLSI_CFG80211, "Status Attribute Found, attr_length = %d, value (%u %u %u %u)\n",
						      attr_length, vendor_ie[elem_idx], vendor_ie[elem_idx + 1], vendor_ie[elem_idx + 2], vendor_ie[elem_idx + 3]);
					status = vendor_ie[elem_idx + 3];
					break;
				}
				elem_idx += 3 + attr_length;
			}

			break;
		}
		vendor_ie += 2 + ie_length;
	}

	SLSI_UNUSED_PARAMETER(dev);

	return status;
}

/**
 * Returns the next expected P2P public action frame subtype for input subtype.
 * Returns SLSI_P2P_PA_INVALID if no frame is expected.
 */
u8 slsi_p2p_get_exp_peer_frame_subtype(u8 subtype)
{
	switch (subtype) {
	/* Peer response is expected for following frames */
	case SLSI_P2P_PA_GO_NEG_REQ:
	case SLSI_P2P_PA_GO_NEG_RSP:
	case SLSI_P2P_PA_INV_REQ:
	case SLSI_P2P_PA_DEV_DISC_REQ:
	case SLSI_P2P_PA_PROV_DISC_REQ:
	case SLSI_PA_GAS_INITIAL_REQ_SUBTYPE:
	case SLSI_PA_GAS_COMEBACK_REQ_SUBTYPE:
		return subtype + 1;
	default:
		return SLSI_P2P_PA_INVALID;
	}
}

void slsi_wlan_dump_public_action_subtype(struct slsi_dev *sdev, struct ieee80211_mgmt *mgmt, bool tx)
{
	u8 action_code = ((u8 *)&mgmt->u.action.u)[0];
	u8 action_category = mgmt->u.action.category;
	char *tx_rx_string = "Received";
	char wnm_action_fields[28][35] = { "Event Request", "Event Report", "Diagnostic Request",
					   "Diagnostic Report", "Location Configuration Request",
					   "Location Configuration Response", "BSS Transition Management Query",
					   "BSS Transition Management Request",
					   "BSS Transition Management Response", "FMS Request", "FMS Response",
					   "Collocated Interference Request", "Collocated Interference Report",
					   "TFS Request", "TFS Response", "TFS Notify", "WNM Sleep Mode Request",
					   "WNM Sleep Mode Response", "TIM Broadcast Request",
					   "TIM Broadcast Response", "QoS Traffic Capability Update",
					   "Channel Usage Request", "Channel Usage Response", "DMS Request",
					   "DMS Response", "Timing Measurement Request",
					   "WNM Notification Request", "WNM Notification Response" };

	if (tx)
		tx_rx_string = "Send";

	switch (action_category) {
	case WLAN_CATEGORY_RADIO_MEASUREMENT:
		switch (action_code) {
		case SLSI_RM_RADIO_MEASUREMENT_REQ:
			SLSI_INFO(sdev, "%s Radio Measurement Frame (Radio Measurement Req)\n", tx_rx_string);
			break;
		case SLSI_RM_RADIO_MEASUREMENT_REP:
			SLSI_INFO(sdev, "%s Radio Measurement Frame (Radio Measurement Rep)\n", tx_rx_string);
			break;
		case SLSI_RM_LINK_MEASUREMENT_REQ:
			SLSI_INFO(sdev, "%s Radio Measurement Frame (Link Measurement Req)\n", tx_rx_string);
			break;
		case SLSI_RM_LINK_MEASUREMENT_REP:
			SLSI_INFO(sdev, "%s Radio Measurement Frame (Link Measurement Rep)\n", tx_rx_string);
			break;
		case SLSI_RM_NEIGH_REP_REQ:
			SLSI_INFO(sdev, "%s Radio Measurement Frame (Neighbor Report Req)\n", tx_rx_string);
			break;
		case SLSI_RM_NEIGH_REP_RSP:
			SLSI_INFO(sdev, "%s Radio Measurement Frame (Neighbor Report Resp)\n", tx_rx_string);
			break;
		default:
			SLSI_INFO(sdev, "%s Radio Measurement Frame (Reserved)\n", tx_rx_string);
		}
		break;
	case WLAN_CATEGORY_PUBLIC:
		switch (action_code) {
		case SLSI_PA_GAS_INITIAL_REQ:
			SLSI_DBG1_NODEV(SLSI_CFG80211, "%s: GAS Initial Request\n", tx ? "TX" : "RX");
			break;
		case SLSI_PA_GAS_INITIAL_RSP:
			SLSI_DBG1_NODEV(SLSI_CFG80211, "%s: GAS Initial Response\n", tx ? "TX" : "RX");
			break;
		case SLSI_PA_GAS_COMEBACK_REQ:
			SLSI_DBG1_NODEV(SLSI_CFG80211, "%s: GAS Comeback Request\n", tx ? "TX" : "RX");
			break;
		case SLSI_PA_GAS_COMEBACK_RSP:
			SLSI_DBG1_NODEV(SLSI_CFG80211, "%s: GAS Comeback Response\n", tx ? "TX" : "RX");
			break;
		default:
			SLSI_DBG1_NODEV(SLSI_CFG80211, "Unknown GAS Frame : %d\n", action_code);
		}
		break;
	case WLAN_CATEGORY_WNM:
		if (action_code >= SLSI_WNM_ACTION_FIELD_MIN && action_code <= SLSI_WNM_ACTION_FIELD_MAX)
			SLSI_INFO(sdev, "%s WNM Frame (%s)\n", tx_rx_string, wnm_action_fields[action_code]);
		else
			SLSI_INFO(sdev, "%s WNM Frame (Reserved)\n", tx_rx_string);
		break;
	}
}

void slsi_abort_sta_scan(struct slsi_dev *sdev)
{
	struct net_device *wlan_net_dev = NULL;
	struct netdev_vif *ndev_vif;

	wlan_net_dev = slsi_get_netdev(sdev, SLSI_NET_INDEX_WLAN);

	if (!wlan_net_dev) {
		SLSI_ERR(sdev, "Dev not found\n");
		return;
	}

	ndev_vif = netdev_priv(wlan_net_dev);
	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	SLSI_MUTEX_LOCK(ndev_vif->scan_mutex);

	if (ndev_vif->scan[SLSI_SCAN_HW_ID].scan_req) {
		struct sk_buff *scan_result;

		SLSI_DBG2(sdev, SLSI_CFG80211, "Abort ongoing WLAN scan\n");
		(void)slsi_mlme_del_scan(sdev, wlan_net_dev, ((ndev_vif->ifnum << 8) | SLSI_SCAN_HW_ID), false);
		SLSI_MUTEX_LOCK(ndev_vif->scan_result_mutex);
		scan_result = slsi_dequeue_cached_scan_result(&ndev_vif->scan[SLSI_SCAN_HW_ID], NULL);
		while (scan_result) {
			slsi_rx_scan_pass_to_cfg80211(sdev, wlan_net_dev, scan_result);
			scan_result = slsi_dequeue_cached_scan_result(&ndev_vif->scan[SLSI_SCAN_HW_ID], NULL);
		}
		SLSI_MUTEX_UNLOCK(ndev_vif->scan_result_mutex);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0))
		cfg80211_scan_done(ndev_vif->scan[SLSI_SCAN_HW_ID].scan_req, &info);
#else
		cfg80211_scan_done(ndev_vif->scan[SLSI_SCAN_HW_ID].scan_req, true);
#endif

		ndev_vif->scan[SLSI_SCAN_HW_ID].scan_req = NULL;
		ndev_vif->scan[SLSI_SCAN_HW_ID].requeue_timeout_work = false;
	}
	SLSI_MUTEX_UNLOCK(ndev_vif->scan_mutex);
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
}

/**
 * Returns a slsi_dhcp_tx enum value after verifying whether the 802.11 packet in skb
 * is a DHCP packet (identified by UDP port numbers)
 */
int slsi_is_dhcp_packet(u8 *data)
{
	u8 *p;
	int ret = SLSI_TX_IS_NOT_DHCP;

	p = data + SLSI_IP_TYPE_OFFSET;

	if (*p == SLSI_IP_TYPE_UDP) {
		u16 source_port, dest_port;

		p = data + SLSI_IP_SOURCE_PORT_OFFSET;
		source_port = p[0] << 8 | p[1];
		p = data + SLSI_IP_DEST_PORT_OFFSET;
		dest_port = p[0] << 8 | p[1];
		if ((source_port == SLSI_DHCP_CLIENT_PORT) && (dest_port == SLSI_DHCP_SERVER_PORT))
			ret = SLSI_TX_IS_DHCP_CLIENT;
		else if ((source_port == SLSI_DHCP_SERVER_PORT) && (dest_port == SLSI_DHCP_CLIENT_PORT))
			ret = SLSI_TX_IS_DHCP_SERVER;
	}

	return ret;
}

int slsi_ap_prepare_add_info_ies(struct netdev_vif *ndev_vif, const u8 *ies, size_t ies_len)
{
	const u8 *wps_p2p_ies = NULL;
	size_t wps_p2p_ie_len = 0;

	/* The ies may contain Extended Capability followed by WPS IE. The Extended capability IE needs to be excluded. */
	wps_p2p_ies = cfg80211_find_ie(SLSI_WLAN_EID_VENDOR_SPECIFIC, ies, ies_len);
	if (wps_p2p_ies) {
		size_t temp_len = wps_p2p_ies - ies;

		wps_p2p_ie_len = ies_len - temp_len;
	}

	SLSI_NET_DBG2(ndev_vif->wdev.netdev, SLSI_MLME, "WPA IE len = %zu, WMM IE len = %zu, IEs len = %zu, WPS_P2P IEs len = %zu\n",
		      ndev_vif->ap.wpa_ie_len, ndev_vif->ap.wmm_ie_len, ies_len, wps_p2p_ie_len);

	ndev_vif->ap.add_info_ies_len = ndev_vif->ap.wpa_ie_len + ndev_vif->ap.wmm_ie_len + wps_p2p_ie_len;
	ndev_vif->ap.add_info_ies = kmalloc(ndev_vif->ap.add_info_ies_len, GFP_KERNEL); /* Caller needs to free this */

	if (!ndev_vif->ap.add_info_ies) {
		SLSI_NET_DBG1(ndev_vif->wdev.netdev, SLSI_MLME, "Failed to allocate memory for IEs\n");
		ndev_vif->ap.add_info_ies_len = 0;
		return -ENOMEM;
	}

	if (ndev_vif->ap.cache_wpa_ie) {
		memcpy(ndev_vif->ap.add_info_ies, ndev_vif->ap.cache_wpa_ie, ndev_vif->ap.wpa_ie_len);
		ndev_vif->ap.add_info_ies += ndev_vif->ap.wpa_ie_len;
	}

	if (ndev_vif->ap.cache_wmm_ie) {
		memcpy(ndev_vif->ap.add_info_ies, ndev_vif->ap.cache_wmm_ie, ndev_vif->ap.wmm_ie_len);
		ndev_vif->ap.add_info_ies += ndev_vif->ap.wmm_ie_len;
	}

	if (wps_p2p_ies) {
		memcpy(ndev_vif->ap.add_info_ies, wps_p2p_ies, wps_p2p_ie_len);
		ndev_vif->ap.add_info_ies += wps_p2p_ie_len;
	}

	ndev_vif->ap.add_info_ies -= ndev_vif->ap.add_info_ies_len;

	return 0;
}

/* Set the correct bit in the channel sets */
static void slsi_roam_channel_cache_add_channel(struct slsi_roaming_network_map_entry *network_map, u8 channel)
{
	if (channel <= 14)
		network_map->channels_24_ghz |= (1 << channel);
	else if (channel >= 36 && channel <= 64)   /* Uni1 */
		network_map->channels_5_ghz |= (1 << ((channel - 36) / 4));
	else if (channel >= 100 && channel <= 140) /* Uni2 */
		network_map->channels_5_ghz |= (1 << (8 + ((channel - 100) / 4)));
	else if (channel >= 149 && channel <= 165) /* Uni3 */
		network_map->channels_5_ghz |= (1 << (24 + ((channel - 149) / 4)));
}

void slsi_roam_channel_cache_add_entry(struct slsi_dev *sdev, struct net_device *dev, const u8 *ssid, const u8 *bssid, u8 channel)
{
	struct list_head *pos;
	int found = 0;
	struct netdev_vif *ndev_vif = netdev_priv(dev);

	list_for_each(pos, &ndev_vif->sta.network_map) {
		struct slsi_roaming_network_map_entry *network_map = list_entry(pos, struct slsi_roaming_network_map_entry, list);

		if (network_map->ssid.ssid_len == ssid[1] &&
		    memcmp(network_map->ssid.ssid, &ssid[2], ssid[1]) == 0) {
			found = 1;
			network_map->last_seen_jiffies = jiffies;
			if (network_map->only_one_ap_seen && memcmp(network_map->initial_bssid, bssid, ETH_ALEN) != 0)
				network_map->only_one_ap_seen = false;
			slsi_roam_channel_cache_add_channel(network_map, channel);
			break;
		}
	}
	if (!found) {
		struct slsi_roaming_network_map_entry *network_map;

		SLSI_NET_DBG3(dev, SLSI_MLME, "New Entry : Channel: %d : %.*s\n", channel, ssid[1], &ssid[2]);
		network_map = kmalloc(sizeof(*network_map), GFP_ATOMIC);
		if (network_map) {
			network_map->ssid.ssid_len = ssid[1];
			memcpy(network_map->ssid.ssid, &ssid[2], ssid[1]);
			network_map->channels_24_ghz = 0;
			network_map->channels_5_ghz = 0;
			network_map->last_seen_jiffies = jiffies;
			SLSI_ETHER_COPY(network_map->initial_bssid, bssid);
			network_map->only_one_ap_seen = true;
			slsi_roam_channel_cache_add_channel(network_map, channel);
			list_add(&network_map->list, &ndev_vif->sta.network_map);
		} else {
			SLSI_ERR(sdev, "New Entry : %.*s kmalloc() failed\n", ssid[1], &ssid[2]);
		}
	}
}

void slsi_roam_channel_cache_add(struct slsi_dev *sdev, struct net_device *dev, struct sk_buff *skb)
{
	struct ieee80211_mgmt *mgmt = fapi_get_mgmt(skb);
	size_t mgmt_len = fapi_get_mgmtlen(skb);
	int ielen = mgmt_len - (mgmt->u.beacon.variable - (u8 *)mgmt);
	u32 freq = fapi_get_u16(skb, u.mlme_scan_ind.channel_frequency) / 2;
	const u8 *scan_ds = cfg80211_find_ie(WLAN_EID_DS_PARAMS, mgmt->u.beacon.variable, ielen);
	const u8 *scan_ht = cfg80211_find_ie(WLAN_EID_HT_OPERATION, mgmt->u.beacon.variable, ielen);
	const u8 *scan_ssid = cfg80211_find_ie(WLAN_EID_SSID, mgmt->u.beacon.variable, ielen);
	u8 chan = 0;

	/* Use the DS or HT channel as the Offchannel results mean the RX freq is not reliable */
	if (scan_ds)
		chan = scan_ds[2];
	else if (scan_ht)
		chan = scan_ht[2];
	else
		chan = ieee80211_frequency_to_channel(freq);

	if (chan) {
		enum ieee80211_band band = IEEE80211_BAND_2GHZ;

		if (chan > 14)
			band = IEEE80211_BAND_5GHZ;

#ifdef CONFIG_SCSC_WLAN_DEBUG
		if (freq != (u32)ieee80211_channel_to_frequency(chan, band)) {
			if (band == IEEE80211_BAND_5GHZ && freq < 3000)
				SLSI_NET_DBG2(dev, SLSI_MLME, "Off Band Result : mlme_scan_ind(freq:%d) != DS(freq:%d)\n", freq, ieee80211_channel_to_frequency(chan, band));

			if (band == IEEE80211_BAND_2GHZ && freq > 3000)
				SLSI_NET_DBG2(dev, SLSI_MLME, "Off Band Result : mlme_scan_ind(freq:%d) != DS(freq:%d)\n", freq, ieee80211_channel_to_frequency(chan, band));
		}
#endif
	}

	if (!scan_ssid || !scan_ssid[1] || scan_ssid[1] > 32) {
		SLSI_NET_DBG3(dev, SLSI_MLME, "SSID not defined : Could not find SSID ie or Hidden\n");
		return;
	}

	slsi_roam_channel_cache_add_entry(sdev, dev, scan_ssid, mgmt->bssid, chan);
}

void slsi_roam_channel_cache_prune(struct net_device *dev, int seconds)
{
	struct slsi_roaming_network_map_entry *network_map;
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct list_head *pos, *q;
	unsigned long now = jiffies;
	unsigned long age;

	list_for_each_safe(pos, q, &ndev_vif->sta.network_map) {
		network_map = list_entry(pos, struct slsi_roaming_network_map_entry, list);
		age = (now - network_map->last_seen_jiffies) / HZ;

		if (time_after_eq(now, network_map->last_seen_jiffies + (seconds * HZ))) {
			list_del(pos);
			kfree(network_map);
		}
	}
}

int slsi_roam_channel_cache_get_channels_int(struct net_device *dev, struct slsi_roaming_network_map_entry *network_map, u8 *channels)
{
	int index = 0;
	int i;

	SLSI_UNUSED_PARAMETER(dev);

	/* 2.4 Ghz Channels */
	for (i = 1; i <= 14; i++)
		if (network_map->channels_24_ghz & (1 << i)) {
			channels[index] = i;
			index++;
		}

	/* 5 Ghz Uni1 Channels */
	for (i = 36; i <= 64; i += 4)
		if (network_map->channels_5_ghz & (1 << ((i - 36) / 4))) {
			channels[index] = i;
			index++;
		}

	/* 5 Ghz Uni2 Channels */
	for (i = 100; i <= 140; i += 4)
		if (network_map->channels_5_ghz & (1 << (8 + ((i - 100) / 4)))) {
			channels[index] = i;
			index++;
		}

	/* 5 Ghz Uni3 Channels */
	for (i = 149; i <= 165; i += 4)
		if (network_map->channels_5_ghz & (1 << (24 + ((i - 149) / 4)))) {
			channels[index] = i;
			index++;
		}
	return index;
}

static struct slsi_roaming_network_map_entry *slsi_roam_channel_cache_get(struct net_device *dev, const u8 *ssid)
{
	struct slsi_roaming_network_map_entry *network_map = NULL;
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct list_head *pos;

	if (WARN_ON(!ssid))
		return NULL;

	list_for_each(pos, &ndev_vif->sta.network_map) {
		network_map = list_entry(pos, struct slsi_roaming_network_map_entry, list);
		if (network_map->ssid.ssid_len == ssid[1] &&
		    memcmp(network_map->ssid.ssid, &ssid[2], ssid[1]) == 0)
			break;
	}
	return network_map;
}

u32 slsi_roam_channel_cache_get_channels(struct net_device *dev, const u8 *ssid, u8 *channels)
{
	u32 channels_count = 0;
	struct slsi_roaming_network_map_entry *network_map;

	network_map = slsi_roam_channel_cache_get(dev, ssid);
	if (network_map)
		channels_count = slsi_roam_channel_cache_get_channels_int(dev, network_map, channels);

	return channels_count;
}

static bool slsi_roam_channel_cache_single_ap(struct net_device *dev, const u8 *ssid)
{
	bool only_one_ap_seen = true;
	struct slsi_roaming_network_map_entry *network_map;

	network_map = slsi_roam_channel_cache_get(dev, ssid);
	if (network_map)
		only_one_ap_seen = network_map->only_one_ap_seen;

	return only_one_ap_seen;
}

int slsi_roaming_scan_configure_channels(struct slsi_dev *sdev, struct net_device *dev, const u8 *ssid, u8 *channels)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	u32 cached_channels_count;

	SLSI_UNUSED_PARAMETER(sdev);

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));
	WARN_ON(!ndev_vif->activated);
	WARN_ON(ndev_vif->vif_type != FAPI_VIFTYPE_STATION);

	cached_channels_count = slsi_roam_channel_cache_get_channels(dev, ssid, channels);
	if (slsi_roam_channel_cache_single_ap(dev,  ssid)) {
		SLSI_NET_DBG3(dev, SLSI_MLME, "Skip Roaming Scan for Single AP %.*s\n", ssid[1], &ssid[2]);
		return 0;
	}

	SLSI_NET_DBG3(dev, SLSI_MLME, "Roaming Scan Channels. %d cached\n", cached_channels_count);

	return cached_channels_count;
}

#ifdef CONFIG_SCSC_WLAN_WES_NCHO
int slsi_is_wes_action_frame(const struct ieee80211_mgmt *mgmt)
{
	int r = 0;
	/* Vendor specific Action (0x7f), SAMSUNG OUI (0x00, 0x00, 0xf0) */
	u8 wes_vs_action_frame[4] = { 0x7f, 0x00, 0x00, 0xf0 };
	u8 *action = (u8 *)&mgmt->u.action;

	if (memcmp(action, wes_vs_action_frame, 4) == 0)
		r = 1;

	return r;
}
#endif

static u32 slsi_remap_reg_rule_flags(u8 flags)
{
	u32 remapped_flags = 0;

	if (flags & SLSI_REGULATORY_DFS)
		remapped_flags |= NL80211_RRF_DFS;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 9))
	if (flags & SLSI_REGULATORY_NO_OFDM)
		remapped_flags |= NL80211_RRF_NO_OFDM;
#endif
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(3, 13, 0))
	if (flags & SLSI_REGULATORY_NO_IR)
		remapped_flags |= NL80211_RRF_PASSIVE_SCAN | NL80211_RRF_NO_IBSS;
#endif
	if (flags & SLSI_REGULATORY_NO_INDOOR)
		remapped_flags |= NL80211_RRF_NO_INDOOR;
	if (flags & SLSI_REGULATORY_NO_OUTDOOR)
		remapped_flags |= NL80211_RRF_NO_OUTDOOR;

	return remapped_flags;
}

static void slsi_reg_mib_to_regd(struct slsi_mib_data *mib, struct slsi_802_11d_reg_domain *domain_info)
{
	int i = 0;
	int num_rules = 0;
	u16 freq;
	u8 byte_val;
	struct ieee80211_reg_rule *reg_rule;

	domain_info->regdomain->alpha2[0] = *(u8 *)(&mib->data[i]);
	i++;

	domain_info->regdomain->alpha2[1] = *(u8 *)(&mib->data[i]);
	i++;

	domain_info->regdomain->dfs_region = *(u8 *)(&mib->data[i]);
	i++;

	while (i < mib->dataLength) {
		reg_rule = &domain_info->regdomain->reg_rules[num_rules];

		/* start freq 2 bytes */
		freq = __le16_to_cpu(*(u16 *)(&mib->data[i]));
		reg_rule->freq_range.start_freq_khz = MHZ_TO_KHZ(freq);

		/* end freq 2 bytes */
		freq = __le16_to_cpu(*(u16 *)(&mib->data[i + 2]));
		reg_rule->freq_range.end_freq_khz = MHZ_TO_KHZ(freq);

		/* Max Bandwidth 1 byte */
		byte_val = *(u8 *)(&mib->data[i + 4]);
		reg_rule->freq_range.max_bandwidth_khz = MHZ_TO_KHZ(byte_val);

		/* max_antenna_gain is obsolute now.*/
		reg_rule->power_rule.max_antenna_gain = 0;

		/* Max Power 1 byte */
		byte_val = *(u8 *)(&mib->data[i + 5]);
		reg_rule->power_rule.max_eirp = DBM_TO_MBM(byte_val);

		/* Flags 1 byte */
		reg_rule->flags = slsi_remap_reg_rule_flags(*(u8 *)(&mib->data[i + 6]));

		i += 7;

		num_rules++; /* Num of reg rules */
	}

	domain_info->regdomain->n_reg_rules = num_rules;
}

void slsi_reset_channel_flags(struct slsi_dev *sdev)
{
	enum ieee80211_band band;
	struct ieee80211_channel *chan;
	int i;
	struct wiphy *wiphy = sdev->wiphy;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0))
	for (band = 0; band < NUM_NL80211_BANDS; band++) {
#else
	for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
#endif
		if (!wiphy->bands[band])
			continue;
		for (i = 0; i < wiphy->bands[band]->n_channels; i++) {
			chan = &wiphy->bands[band]->channels[i];
			chan->flags = 0;
		}
	}
}

int slsi_read_regulatory_rules(struct slsi_dev *sdev, struct slsi_802_11d_reg_domain *domain_info, const char *alpha2)
{
	struct slsi_mib_data mibreq = { 0, NULL };
	struct slsi_mib_data mibrsp = { 0, NULL };
	struct slsi_mib_entry mib_val;
	int r                       = 0;
	int rx_len                = 0;
	int len                     = 0;
	int index;

	index = slsi_country_to_index(domain_info, alpha2);

	if (index == -1) {
		SLSI_ERR(sdev, "Unsupported index\n");
		return -EINVAL;
	}

	slsi_mib_encode_get(&mibreq, SLSI_PSID_UNIFI_REGULATORY_PARAMETERS, index);

	/* Max of 6 regulatory constraints.
	 * each constraint start_freq(2 byte), end_freq(2 byte), Band width(1 byte), Max power(1 byte),
	 * rules flag (1 byte)
	 * firmware can have a max of 6 rules for a country.
	 */
	/* PSID header (5 bytes) + ((3 bytes) alpha2 code + dfs) + (max of 50 regulatory rules * 7 bytes each row) + MIB status(1) */
	mibrsp.dataLength = 5 + 3 + (SLSI_MIB_REG_RULES_MAX * 7) + 1;
	mibrsp.data = kmalloc(mibrsp.dataLength, GFP_KERNEL);

	if (!mibrsp.data) {
		SLSI_ERR(sdev, "Failed to alloc for Mib response\n");
		kfree(mibreq.data);
		return -ENOMEM;
	}

	r = slsi_mlme_get(sdev, NULL, mibreq.data, mibreq.dataLength,
			  mibrsp.data, mibrsp.dataLength, &rx_len);
	kfree(mibreq.data);

	if (r == 0) {
		mibrsp.dataLength = rx_len;

		len = slsi_mib_decode(&mibrsp, &mib_val);

		if (len == 0) {
			kfree(mibrsp.data);
			SLSI_ERR(sdev, "Mib decode error\n");
			return -EINVAL;
		}
		slsi_reg_mib_to_regd(&mib_val.value.u.octetValue, domain_info);
	} else {
		SLSI_ERR(sdev, "Mib read failed (error: %d)\n", r);
	}

	kfree(mibrsp.data);
	return r;
}

static int slsi_country_to_index(struct slsi_802_11d_reg_domain *domain_info, const char *alpha2)
{
	int index = 0;
	bool index_found = false;

	SLSI_DBG3_NODEV(SLSI_MLME, "\n");
	if (domain_info->countrylist) {
		for (index = 0; index < domain_info->country_len; index += 2) {
			if (memcmp(&domain_info->countrylist[index], alpha2, 2) == 0) {
				index_found = true;
				break;
			}
		}

		/* If the set country is not present in the country list, fall back to
		 * world domain i.e. regulatory rules index = 1
		 */
		if (index_found)
			return (index / 2) + 1;
		else
			return 1;
	}

	return -1;
}

/* Set the rssi boost value of a particular band as set in the SETJOINPREFER command*/
int slsi_set_mib_rssi_boost(struct slsi_dev *sdev, struct net_device *dev, u16 psid, int index, int boost)
{
	struct slsi_mib_data mib_data = { 0, NULL };
	int  error = SLSI_MIB_STATUS_FAILURE;

	SLSI_DBG2(sdev, SLSI_MLME, "Set rssi boost: %d\n", boost);
	WARN_ON(!SLSI_MUTEX_IS_LOCKED(sdev->device_config_mutex));
	if (slsi_mib_encode_int(&mib_data, psid, boost, index) == SLSI_MIB_STATUS_SUCCESS)
		if (mib_data.dataLength) {
			error = slsi_mlme_set(sdev, NULL, mib_data.data, mib_data.dataLength);
			if (error)
				SLSI_ERR(sdev, "Err Setting MIB failed. error = %d\n", error);
			kfree(mib_data.data);
		}

	return error;
}

void slsi_modify_ies_on_channel_switch(struct net_device *dev, struct cfg80211_ap_settings *settings,
				       u8 *ds_params_ie, u8 *ht_operation_ie, struct ieee80211_mgmt  *mgmt,
				       u16 beacon_ie_head_len)
{
	slsi_modify_ies(dev, WLAN_EID_DS_PARAMS, mgmt->u.beacon.variable,
			beacon_ie_head_len, 2, ieee80211_frequency_to_channel(settings->chandef.chan->center_freq));

	slsi_modify_ies(dev, WLAN_EID_HT_OPERATION, (u8 *)settings->beacon.tail,
			settings->beacon.tail_len, 2,
			ieee80211_frequency_to_channel(settings->chandef.chan->center_freq));
}

#ifdef CONFIG_SCSC_WLAN_WIFI_SHARING
void slsi_extract_valid_wifi_sharing_channels(struct slsi_dev *sdev)
{
	int i, j;
	int p = 0;
	int k = (SLSI_MAX_CHAN_5G_BAND - 1);
	int flag = 0;

	for (i = 4; i >= 0 ; i--) {
		for (j = 0; j <= 7 ; j++) {
			if ((i == 4) && (j == 0))
				j = 1;
			if (sdev->wifi_sharing_5ghz_channel[i] & (u8)(1 << (7 - j)))
				sdev->valid_5g_freq[p] = slsi_5ghz_all_channels[k];
			else
				sdev->valid_5g_freq[p] = 0;
			p++;
			k--;
			if (p == SLSI_MAX_CHAN_5G_BAND) {
				flag = 1;
				break;
			}
		}
		if (flag == 1)
			break;
	}
}

bool slsi_if_valid_wifi_sharing_channel(struct slsi_dev *sdev, int freq)
{
	int i;

	for (i = 0; i <= (SLSI_MAX_CHAN_5G_BAND - 1) ; i++) {
		if (sdev->valid_5g_freq[i] == freq)
			return 1;
	}
	return 0;
}

void slsi_select_wifi_sharing_ap_channel(struct wiphy *wiphy, struct net_device *dev,
					 struct cfg80211_ap_settings *settings,
					 struct slsi_dev *sdev, int *wifi_sharing_channel_switched)
{
	struct net_device *sta_dev = slsi_get_netdev(sdev, SLSI_NET_INDEX_WLAN);
	struct netdev_vif *ndev_sta_vif  = netdev_priv(sta_dev);
	int sta_frequency = ndev_sta_vif->chan->center_freq;

	//slsi_extract_valid_wifi_sharing_channels(sdev);
	SLSI_DBG1(sdev, SLSI_CFG80211, "Station connected on frequency: %d\n", sta_frequency);

	if (((sta_frequency) / 1000) == 2) { /*For 2.4GHz */
		/*if single antenna*/
#ifdef CONFIG_SCSC_WLAN_SINGLE_ANTENNA
		if ((settings->chandef.chan->center_freq) != (sta_frequency)) {
			*wifi_sharing_channel_switched = 1;
			settings->chandef.chan = __ieee80211_get_channel(wiphy, sta_frequency);
			settings->chandef.center_freq1 = sta_frequency;
		}
#else
		/* if dual antenna */
		if ((((settings->chandef.chan->center_freq) / 1000) == 5) &&
		    !(slsi_check_if_channel_restricted_already(sdev,
		    ieee80211_frequency_to_channel(settings->chandef.chan->center_freq))) &&
		    slsi_if_valid_wifi_sharing_channel(sdev, settings->chandef.chan->center_freq)) {
			settings->chandef.chan = __ieee80211_get_channel(wiphy, settings->chandef.chan->center_freq);
			settings->chandef.center_freq1 = settings->chandef.chan->center_freq;
		} else {
			if ((settings->chandef.chan->center_freq) != (sta_frequency)) {
				*wifi_sharing_channel_switched = 1;
				settings->chandef.chan = __ieee80211_get_channel(wiphy, sta_frequency);
				settings->chandef.center_freq1 = sta_frequency;
			}
		}
#endif
	}

	else { /* For 5GHz */
		/* For single antenna */
#ifdef CONFIG_SCSC_WLAN_SINGLE_ANTENNA
		if ((settings->chandef.chan->center_freq) != (sta_frequency)) {
			*wifi_sharing_channel_switched = 1;
			settings->chandef.chan = __ieee80211_get_channel(wiphy, sta_frequency);
			settings->chandef.center_freq1 = sta_frequency;
		}
		/* Single antenna end */
#else
		/* For Dual Antenna */
		if (((settings->chandef.chan->center_freq) / 1000) == 5) {
			if (!(slsi_check_if_channel_restricted_already(sdev,
			      ieee80211_frequency_to_channel(sta_frequency))) &&
			    slsi_if_valid_wifi_sharing_channel(sdev, sta_frequency)) {
				if ((settings->chandef.chan->center_freq) != (sta_frequency)) {
					*wifi_sharing_channel_switched = 1;
					settings->chandef.chan = __ieee80211_get_channel(wiphy, sta_frequency);
				}
			} else {
				*wifi_sharing_channel_switched = 1;
				settings->chandef.chan = __ieee80211_get_channel(wiphy, SLSI_2G_CHANNEL_ONE);
				settings->chandef.center_freq1 = SLSI_2G_CHANNEL_ONE;
			}
		}
#endif
	}

	SLSI_DBG1(sdev, SLSI_CFG80211, "AP frequency chosen: %d\n", settings->chandef.chan->center_freq);
}

int slsi_get_byte_position(int bit)
{
	int byte_pos = 0;

	/* bit will find which bit, pos will tell which pos in the array */
	if (bit >= 8 && bit <= 15)
		byte_pos = 1;
	else if (bit >= 16 && bit <= 23)
		byte_pos = 2;
	else if (bit >= 24 && bit <= 31)
		byte_pos = 3;
	else if (bit >= 32 && bit <= 38)
		byte_pos = 4;

	return byte_pos;
}

int slsi_check_if_channel_restricted_already(struct slsi_dev *sdev, int channel)
{
	int i;

	for (i = 0; i < sdev->num_5g_restricted_channels; i++)
		if (sdev->wifi_sharing_5g_restricted_channels[i] == channel)
			return 1;

	return 0;
}

int slsi_set_mib_wifi_sharing_5ghz_channel(struct slsi_dev *sdev, u16 psid, int res,
					   int offset, int readbyte, char *arg)
{
	struct slsi_mib_entry mib_entry;
	struct slsi_mib_data buffer = { 0, NULL };
	int error = SLSI_MIB_STATUS_FAILURE;
	int i;
	int bit = 0; /* find which bit to set */
	int byte_pos = 0; /* which index to set bit among 8 larger set*/
	int freq;
	int j;
	int bit_mask;
	int num_channels;
	int p = 0;
	int new_channels = 0;
	int freq_to_be_checked = 0;

	mib_entry.value.type = SLSI_MIB_TYPE_OCTET;
	mib_entry.value.u.octetValue.dataLength = 8;
	mib_entry.value.u.octetValue.data = kmalloc(64, GFP_KERNEL);

	for (i = 0; i < 8; i++)
		mib_entry.value.u.octetValue.data[i] = sdev->wifi_sharing_5ghz_channel[i];

	if (res == 0) {
		for (i = 0; i < 25 ; i++)
			sdev->wifi_sharing_5g_restricted_channels[i] = 0;
		sdev->num_5g_restricted_channels = 0;
		new_channels = 1;
	} else if (res == -1) {
		for (i = 0; i < 8; i++)
			mib_entry.value.u.octetValue.data[i] = 0x00;

		for (i = 0; i < 25 ; i++)
			sdev->wifi_sharing_5g_restricted_channels[i] = 0;

		for (i = 24; i >= 0 ; i--) {
			if (sdev->valid_5g_freq[i] != 0)
				sdev->wifi_sharing_5g_restricted_channels[p++] =
				ieee80211_frequency_to_channel(sdev->valid_5g_freq[i]);
		}
		sdev->num_5g_restricted_channels = p;
		new_channels = 1;
	} else {
		num_channels = res;

		for (i = 0; i < num_channels; i++) {
			offset = offset + readbyte + 1;
			readbyte = slsi_str_to_int(&arg[offset], &res);
			/*if channel is not already present , then only add it*/
			freq_to_be_checked = ieee80211_channel_to_frequency(res, NL80211_BAND_5GHZ);
			if (slsi_if_valid_wifi_sharing_channel(sdev, freq_to_be_checked) &&
			    (!slsi_check_if_channel_restricted_already(sdev, res))) {
				if ((sdev->num_5g_restricted_channels) > 24)
					break;
				new_channels = 1;
				sdev->wifi_sharing_5g_restricted_channels[(sdev->num_5g_restricted_channels)++] = res;
			}
		}

		if (new_channels) {
			for (i = 0; i < (sdev->num_5g_restricted_channels); i++) {
				freq = ieee80211_channel_to_frequency(sdev->wifi_sharing_5g_restricted_channels[i],
								      NL80211_BAND_5GHZ);
				for (j = 0; j < 25; j++) {
					if (slsi_5ghz_all_channels[j] == freq) {
						bit = j + 14;
						break;
					}
				}
				byte_pos = slsi_get_byte_position(bit);
				bit_mask  = (bit % 8);
				mib_entry.value.u.octetValue.data[byte_pos] &= (u8)(~(1 << (bit_mask)));
			}
		}
	}

	if (new_channels) {
		error = slsi_mib_encode_octet(&buffer, psid, mib_entry.value.u.octetValue.dataLength,
					      mib_entry.value.u.octetValue.data, 0);
		if (error != SLSI_MIB_STATUS_SUCCESS) {
			error = -ENOMEM;
			goto exit;
		}

		if (WARN_ON(buffer.dataLength == 0)) {
			error = -EINVAL;
			goto exit;
		}

		error = slsi_mlme_set(sdev, NULL, buffer.data, buffer.dataLength);
		kfree(buffer.data);

		if (!error)
			return 0;

exit:
		SLSI_ERR(sdev, "Error in setting wifi sharing 5ghz channel. error = %d\n", error);
		return error;
	}

	return 0;
}
#endif
#ifdef CONFIG_SCSC_WLAN_ENABLE_MAC_RANDOMISATION
int slsi_set_mac_randomisation_mask(struct slsi_dev *sdev, u8 *mac_address_mask)
{
	int r = 0;
	struct slsi_mib_data mib_data = { 0, NULL };

	SLSI_DBG1(sdev, SLSI_CFG80211, "Mask is :%pM\n", mac_address_mask);
	r = slsi_mib_encode_octet(&mib_data, SLSI_PSID_UNIFI_MAC_ADDRESS_RANDOMISATION_MASK, ETH_ALEN,
				  mac_address_mask, 0);
	if (r != SLSI_MIB_STATUS_SUCCESS) {
		SLSI_ERR(sdev, "Err setting unifiMacAddrRandomistaionMask MIB. error = %d\n", r);
		if (sdev->scan_addr_set) {
			struct slsi_mib_data mib_data_randomization_activated = { 0, NULL };

			r = slsi_mib_encode_bool(&mib_data_randomization_activated,
						 SLSI_PSID_UNIFI_MAC_ADDRESS_RANDOMISATION_ACTIVATED, 1, 0);
			if (r != SLSI_MIB_STATUS_SUCCESS) {
				SLSI_ERR(sdev, "UNIFI_MAC_ADDRESS_RANDOMISATION_ACTIVATED: no mem for MIB\n");
				return  -ENOMEM;
			}

			r = slsi_mlme_set(sdev, NULL, mib_data_randomization_activated.data,
					  mib_data_randomization_activated.dataLength);

			kfree(mib_data_randomization_activated.data);

			if (r)
				SLSI_ERR(sdev, "Err setting unifiMacAddrRandomistaionActivated MIB. error = %d\n", r);
				return r;
			}
			return -ENOMEM;
		}
	if (mib_data.dataLength == 0) {
		SLSI_WARN(sdev, "Mib Data length is Zero\n");
		return -EINVAL;
	}
	r = slsi_mlme_set(sdev, NULL, mib_data.data, mib_data.dataLength);
	if (r)
		SLSI_ERR(sdev, "Err setting Randomized mac mask= %d\n", r);
	kfree(mib_data.data);
	return r;
}
#endif
/* Set the new country code and read the regulatory parameters of updated country. */
int slsi_set_country_update_regd(struct slsi_dev *sdev, const char *alpha2_code, int size)
{
	struct slsi_mib_data mib_data = { 0, NULL };
	char alpha2[4];
	int  error = 0;

	SLSI_DBG2(sdev, SLSI_MLME, "Set country code: %c%c\n", alpha2_code[0], alpha2_code[1]);

	if (size == 4) {
		memcpy(alpha2, alpha2_code, 4);
	} else {
		memcpy(alpha2, alpha2_code, 3);
		alpha2[3] = '\0';
	}

	if (memcmp(alpha2, sdev->device_config.domain_info.regdomain->alpha2, 2) == 0) {
		SLSI_DBG3(sdev, SLSI_MLME, "Country is already set to the requested country code\n");
		return 0;
	}

	SLSI_MUTEX_LOCK(sdev->device_config_mutex);

	error = slsi_mib_encode_octet(&mib_data, SLSI_PSID_UNIFI_DEFAULT_COUNTRY, 3, alpha2, 0);
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

	if (error) {
		SLSI_ERR(sdev, "Err setting country error = %d\n", error);
		SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);
		return -1;
	}

	/* Read the regulatory params for the country */
	if (slsi_read_regulatory_rules(sdev, &sdev->device_config.domain_info, alpha2) == 0) {
		slsi_reset_channel_flags(sdev);
		wiphy_apply_custom_regulatory(sdev->wiphy, sdev->device_config.domain_info.regdomain);
		slsi_update_supported_channels_regd_flags(sdev);
	}

exit:
	SLSI_MUTEX_UNLOCK(sdev->device_config_mutex);
	return error;
}

/* Read unifiDisconnectTimeOut MIB */
int slsi_read_disconnect_ind_timeout(struct slsi_dev *sdev, u16 psid)
{
	struct slsi_mib_data mibreq = { 0, NULL };
	struct slsi_mib_data mibrsp = { 0, NULL };
	struct slsi_mib_entry mib_val;
	int r                       = 0;
	int rx_len                = 0;
	int len                     = 0;

	SLSI_DBG3(sdev, SLSI_MLME, "\n");

	slsi_mib_encode_get(&mibreq, psid, 0);

	mibrsp.dataLength = 10; /* PSID header(5) + uint 4 bytes + status(1) */
	mibrsp.data = kmalloc(mibrsp.dataLength, GFP_KERNEL);

	if (!mibrsp.data) {
		SLSI_ERR(sdev, "Failed to alloc for Mib response\n");
		kfree(mibreq.data);
		return -ENOMEM;
	}

	r = slsi_mlme_get(sdev, NULL, mibreq.data, mibreq.dataLength, mibrsp.data,
			  mibrsp.dataLength, &rx_len);
	kfree(mibreq.data);

	if (r == 0) {
		mibrsp.dataLength = rx_len;
		len = slsi_mib_decode(&mibrsp, &mib_val);

		if (len == 0) {
			kfree(mibrsp.data);
			SLSI_ERR(sdev, "Mib decode error\n");
			return -EINVAL;
		}
		/* Add additional 1 sec delay */
		sdev->device_config.ap_disconnect_ind_timeout = ((mib_val.value.u.uintValue + 1) * 1000);
	} else {
		SLSI_ERR(sdev, "Mib read failed (error: %d)\n", r);
	}

	kfree(mibrsp.data);
	return r;
}

/* Read unifiDefaultCountry MIB */
int slsi_read_default_country(struct slsi_dev *sdev, u8 *alpha2, u16 index)
{
	struct slsi_mib_data mibreq = { 0, NULL };
	struct slsi_mib_data mibrsp = { 0, NULL };
	struct slsi_mib_entry mib_val;
	int r                       = 0;
	int rx_len                = 0;
	int len                     = 0;

	slsi_mib_encode_get(&mibreq, SLSI_PSID_UNIFI_DEFAULT_COUNTRY, index);

	mibrsp.dataLength = 11; /* PSID header(5) + index(1) + country code alpha2 3 bytes + status(1) */
	mibrsp.data = kmalloc(mibrsp.dataLength, GFP_KERNEL);

	if (!mibrsp.data) {
		SLSI_ERR(sdev, "Failed to alloc for Mib response\n");
		kfree(mibreq.data);
		return -ENOMEM;
	}

	r = slsi_mlme_get(sdev, NULL, mibreq.data, mibreq.dataLength, mibrsp.data,
			  mibrsp.dataLength, &rx_len);

	kfree(mibreq.data);

	if (r == 0) {
		mibrsp.dataLength = rx_len;
		len = slsi_mib_decode(&mibrsp, &mib_val);

		if (len == 0) {
			kfree(mibrsp.data);
			SLSI_ERR(sdev, "Mib decode error\n");
			return -EINVAL;
		}
		memcpy(alpha2, mib_val.value.u.octetValue.data, 2);
	} else {
		SLSI_ERR(sdev, "Mib read failed (error: %d)\n", r);
	}

	kfree(mibrsp.data);
	return r;
}

int slsi_copy_country_table(struct slsi_dev *sdev, struct slsi_mib_data *mib, int len)
{
	sdev->device_config.domain_info.countrylist = kmalloc(len, GFP_KERNEL);
	sdev->device_config.domain_info.country_len = len;

	if (!sdev->device_config.domain_info.countrylist) {
		SLSI_ERR(sdev, "kmalloc failed\n");
		return -EINVAL;
	}

	if (!mib || !mib->data) {
		SLSI_ERR(sdev, "Invalid MIB country table\n");
		return -EINVAL;
	}

	memcpy(sdev->device_config.domain_info.countrylist, mib->data, len);

	return 0;
}

/* Read unifi country list */
int slsi_read_unifi_countrylist(struct slsi_dev *sdev, u16 psid)
{
	struct slsi_mib_data mibreq = { 0, NULL };
	struct slsi_mib_data mibrsp = { 0, NULL };
	struct slsi_mib_entry mib_val;
	int r                       = 0;
	int rx_len                = 0;
	int len                  = 0;
	int ret;

	slsi_mib_encode_get(&mibreq, psid, 0);

	/* Fixed fields len (5) : 2 bytes(PSID) + 2 bytes (Len) + 1 byte (status)
	 * Data : 148 countries??? for SLSI_PSID_UNIFI_COUNTRY_LIST
	 */
	mibrsp.dataLength = 5 + (NUM_COUNTRY * 2);
	mibrsp.data = kmalloc(mibrsp.dataLength, GFP_KERNEL);

	if (!mibrsp.data) {
		SLSI_ERR(sdev, "Failed to alloc for Mib response\n");
		kfree(mibreq.data);
		return -ENOMEM;
	}

	r = slsi_mlme_get(sdev, NULL, mibreq.data, mibreq.dataLength, mibrsp.data,
			  mibrsp.dataLength, &rx_len);

	kfree(mibreq.data);

	if (r == 0) {
		mibrsp.dataLength = rx_len;
		len = slsi_mib_decode(&mibrsp, &mib_val);

		if (len == 0) {
			kfree(mibrsp.data);
			return -EINVAL;
		}
		ret = slsi_copy_country_table(sdev, &mib_val.value.u.octetValue, len);
		if (ret < 0) {
			kfree(mibrsp.data);
			return ret;
		}
	} else {
		SLSI_ERR(sdev, "Mib read failed (error: %d)\n", r);
	}

	kfree(mibrsp.data);
	return r;
}

void  slsi_regd_deinit(struct slsi_dev *sdev)
{
	SLSI_DBG1(sdev, SLSI_INIT_DEINIT, "slsi_regd_deinit\n");

	kfree(sdev->device_config.domain_info.countrylist);
}

void slsi_clear_offchannel_data(struct slsi_dev *sdev, bool acquire_lock)
{
	struct net_device *dev = NULL;
	struct netdev_vif *ndev_vif = NULL;

	dev = slsi_get_netdev(sdev, SLSI_NET_INDEX_P2PX_SWLAN);
	if (WARN_ON(!dev)) {
		SLSI_ERR(sdev, "No Group net dev found\n");
		return;
	}
	ndev_vif = netdev_priv(dev);

	if (acquire_lock)
		SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	/* Reset dwell time should be sent on group vif */
	(void)slsi_mlme_reset_dwell_time(sdev, dev);

	if (acquire_lock)
		SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);

	sdev->p2p_group_exp_frame = SLSI_P2P_PA_INVALID;
}

static void slsi_hs2_unsync_vif_delete_work(struct work_struct *work)
{
	struct netdev_vif *ndev_vif = container_of((struct delayed_work *)work, struct netdev_vif, unsync.hs2_del_vif_work);

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);
	SLSI_NET_DBG1(ndev_vif->wdev.netdev, SLSI_CFG80211, "Delete HS vif duration expired  - Deactivate unsync vif\n");
	slsi_wlan_unsync_vif_deactivate(ndev_vif->sdev, ndev_vif->wdev.netdev, true);
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);
}

int slsi_wlan_unsync_vif_activate(struct slsi_dev *sdev, struct net_device *dev,
				  struct ieee80211_channel *chan, u16 wait)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	int r = 0;
	u8 device_address[ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	u32                 action_frame_bmap;
	SLSI_DBG1(sdev, SLSI_INIT_DEINIT, "Activate wlan unsync vif\n");

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	ndev_vif->vif_type = FAPI_VIFTYPE_UNSYNCHRONISED;

	/* Avoid suspend when wlan unsync VIF is active */
	slsi_wakelock(&sdev->wlan_wl);

	/* Interface address and device address are same for unsync vif */
	if (slsi_mlme_add_vif(sdev, dev, dev->dev_addr, device_address) != 0) {
		SLSI_NET_ERR(dev, "add vif failed for wlan unsync vif\n");
		goto exit_with_error;
	}

	if (slsi_vif_activated(sdev, dev) != 0) {
		SLSI_NET_ERR(dev, "vif activate failed for wlan unsync vif\n");
		slsi_mlme_del_vif(sdev, dev);
		goto exit_with_error;
	}
	sdev->wlan_unsync_vif_state = WLAN_UNSYNC_VIF_ACTIVE;
	INIT_DELAYED_WORK(&ndev_vif->unsync.hs2_del_vif_work, slsi_hs2_unsync_vif_delete_work);
	action_frame_bmap = SLSI_ACTION_FRAME_PUBLIC | SLSI_ACTION_FRAME_RADIO_MEASUREMENT;

	r = slsi_mlme_register_action_frame(sdev, dev, action_frame_bmap, action_frame_bmap);
	if (r != 0) {
		SLSI_NET_ERR(dev, "slsi_mlme_register_action_frame failed: resultcode = %d, action_frame_bmap:%d\n",
			     r, action_frame_bmap);
		goto exit_with_vif;
	}

	if (slsi_mlme_set_channel(sdev, dev, chan, SLSI_FW_CHANNEL_DURATION_UNSPECIFIED, 0, 0) != 0) {
		SLSI_NET_ERR(dev, "Set channel failed for wlan unsync vif\n");
		goto exit_with_vif;
	}
	ndev_vif->chan = chan;
	return r;

exit_with_vif:
	slsi_wlan_unsync_vif_deactivate(sdev, dev, true);
exit_with_error:
	slsi_wakeunlock(&sdev->wlan_wl);
	return -EINVAL;
}

/* Delete unsync vif - DON'T update the vif type */
void slsi_wlan_unsync_vif_deactivate(struct slsi_dev *sdev, struct net_device *dev, bool hw_available)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);

	SLSI_NET_DBG1(dev, SLSI_INIT_DEINIT, "De-activate wlan unsync vif\n");

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	if (sdev->wlan_unsync_vif_state == WLAN_UNSYNC_NO_VIF) {
		SLSI_NET_DBG1(dev, SLSI_INIT_DEINIT, "wlan unsync vif already deactivated\n");
		return;
	}

	cancel_delayed_work(&ndev_vif->unsync.hs2_del_vif_work);

	/* slsi_vif_deactivated is not used here after slsi_mlme_del_vif
	 *  as it modifies vif type as well
	 */
	if (hw_available)
		slsi_mlme_del_vif(sdev, dev);

	slsi_wakeunlock(&sdev->wlan_wl);

	sdev->wlan_unsync_vif_state = WLAN_UNSYNC_NO_VIF;
	ndev_vif->activated = false;
	ndev_vif->chan = NULL;

	(void)slsi_set_mgmt_tx_data(ndev_vif, 0, 0, NULL, 0);
}

void slsi_scan_ind_timeout_handle(struct work_struct *work)
{
	struct netdev_vif *ndev_vif = container_of((struct delayed_work *)work, struct netdev_vif, scan_timeout_work);
	struct net_device *dev = slsi_get_netdev(ndev_vif->sdev, ndev_vif->ifnum);

	SLSI_MUTEX_LOCK(ndev_vif->scan_mutex);
	if (ndev_vif->scan[SLSI_SCAN_HW_ID].scan_req) {
		if (ndev_vif->scan[SLSI_SCAN_HW_ID].requeue_timeout_work) {
			queue_delayed_work(ndev_vif->sdev->device_wq, &ndev_vif->scan_timeout_work,
					   msecs_to_jiffies(SLSI_FW_SCAN_DONE_TIMEOUT_MSEC));
			ndev_vif->scan[SLSI_SCAN_HW_ID].requeue_timeout_work = false;
		} else {
			SLSI_WARN(ndev_vif->sdev, "Mlme_scan_done_ind not received\n");
			(void)slsi_mlme_del_scan(ndev_vif->sdev, dev, ndev_vif->ifnum << 8 | SLSI_SCAN_HW_ID, true);
			slsi_scan_complete(ndev_vif->sdev, dev, SLSI_SCAN_HW_ID, false);
		}
	}
	SLSI_MUTEX_UNLOCK(ndev_vif->scan_mutex);
}

void slsi_update_supported_channels_regd_flags(struct slsi_dev *sdev)
{
	int i = 0;
	struct wiphy *wiphy = sdev->wiphy;
	struct ieee80211_channel *chan;

	/* If all channels are supported by chip no need disable any channel
	 * So return
	 */
	if (sdev->enabled_channel_count == 39)
		return;
	if (wiphy->bands[0]) {
		for (i = 0; i < ARRAY_SIZE(sdev->supported_2g_channels); i++) {
			if (sdev->supported_2g_channels[i] == 0) {
				chan = &wiphy->bands[0]->channels[i];
				chan->flags |= IEEE80211_CHAN_DISABLED;
			}
		}
	}
	if (sdev->band_5g_supported && wiphy->bands[1]) {
		for (i = 0; i <  ARRAY_SIZE(sdev->supported_5g_channels); i++) {
			if (sdev->supported_5g_channels[i] == 0) {
				chan = &wiphy->bands[1]->channels[i];
				chan->flags |= IEEE80211_CHAN_DISABLED;
			}
		}
	}
}
int slsi_find_chan_idx(u16 chan, u8 hw_mode)
{
	int idx = 0, i = 0;
	u16 slsi_5ghz_channels_list[25] = {36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132,
				      136, 140, 144, 149, 153, 157, 161, 165};

	if (hw_mode == SLSI_ACS_MODE_IEEE80211B || hw_mode == SLSI_ACS_MODE_IEEE80211G) {
		idx = chan - 1;
		return idx;
	}
	for (i = 0; i < 25; i++) {
		if (chan == slsi_5ghz_channels_list[i]) {
			idx = i;
			break;
		}
	}
	return idx;
}

