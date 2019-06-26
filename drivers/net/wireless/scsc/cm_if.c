/****************************************************************************
 *
 * Copyright (c) 2012 - 2017 Samsung Electronics Co., Ltd. All rights reserved
 *
 *       Chip Manager interface
 *
 ****************************************************************************/

#include "mgt.h"
#include "dev.h"
#include "debug.h"
#include "scsc_wifi_cm_if.h"
#ifdef CONFIG_SCSC_WLAN_HIP4_PROFILING
#include "hip4_sampler.h"
#endif

#include "../scsc/scsc_mx_impl.h" /* TODO */
#include <scsc/scsc_mx.h>
#ifdef CONFIG_SCSC_LOG_COLLECTION
#include <scsc/scsc_log_collector.h>
#endif

static bool EnableTestMode;
module_param(EnableTestMode, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(EnableTestMode, "Enable WlanLite test mode driver.");

static BLOCKING_NOTIFIER_HEAD(slsi_wlan_notifier);

static bool EnableRfTestMode;
module_param(EnableRfTestMode, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(EnableRfTestMode, "Enable RF test mode driver.");

static struct mutex slsi_start_mutex;
static int recovery_in_progress;
static u16 latest_scsc_panic_code;

#define SLSI_SM_WLAN_SERVICE_STOP_RECOVERY_TIMEOUT 20000

/* TODO: Would be good to get this removed - use module_client? */
struct slsi_cm_ctx {
	struct slsi_dev *sdev;
};

/* Only one wlan service instance is assumed for now. */
static struct slsi_cm_ctx cm_ctx;

static void slsi_hip_block_bh(struct slsi_dev *sdev);

int slsi_wlan_service_notifier_register(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&slsi_wlan_notifier, nb);
}

int slsi_wlan_service_notifier_unregister(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&slsi_wlan_notifier, nb);
}

static int wlan_suspend(struct scsc_service_client *client)
{
	struct slsi_dev *sdev = container_of(client, struct slsi_dev, mx_wlan_client);

	SLSI_INFO_NODEV("Nofity registered functions\n");
	blocking_notifier_call_chain(&slsi_wlan_notifier, SCSC_WIFI_SUSPEND, sdev);

	return 0;
}

static int wlan_resume(struct scsc_service_client *client)
{
	struct slsi_dev *sdev = container_of(client, struct slsi_dev, mx_wlan_client);

	SLSI_INFO_NODEV("Nofity registered functions\n");
	blocking_notifier_call_chain(&slsi_wlan_notifier, SCSC_WIFI_RESUME, sdev);

	return 0;
}

static void wlan_stop_on_failure(struct scsc_service_client *client)
{
	int state;
	struct slsi_dev *sdev = container_of(client, struct slsi_dev, mx_wlan_client);

	SLSI_INFO_NODEV("\n");

	mutex_lock(&slsi_start_mutex);
	recovery_in_progress = 1;
	sdev->recovery_status = 1;
	state = atomic_read(&sdev->cm_if.cm_if_state);
	if (state != SCSC_WIFI_CM_IF_STATE_STOPPED) {
		atomic_set(&sdev->cm_if.cm_if_state, SCSC_WIFI_CM_IF_STATE_BLOCKED);
		sdev->fail_reported = true;

		/* If next state is stopped, then don't signal recovery since
		 * the Android framework won't/shouldn't restart (supplicant
		 * stop and start).
		 */
		if (sdev->recovery_next_state != SCSC_WIFI_CM_IF_STATE_STOPPING) {
			slsi_hip_block_bh(sdev);

			/* Stop wlan operations. Send event to registered parties */
			mutex_unlock(&slsi_start_mutex);
			SLSI_INFO_NODEV("Nofity registered functions\n");
			blocking_notifier_call_chain(&slsi_wlan_notifier, SCSC_WIFI_STOP, sdev);
			mutex_lock(&slsi_start_mutex);
		}
	} else {
		SLSI_INFO_NODEV("Wi-Fi service driver not started\n");
	}

	mutex_unlock(&slsi_start_mutex);
}

static void wlan_failure_reset(struct scsc_service_client *client, u16 scsc_panic_code)
{
	SLSI_INFO_NODEV("Enter\n");
	latest_scsc_panic_code = scsc_panic_code;
}

int slsi_check_rf_test_mode(void)
{
	struct file *fp = NULL;
#if defined(ANDROID_VERSION) && ANDROID_VERSION >= 90000
 	char *filepath = "/data/vendor/conn/.psm.info";
#else
	char *filepath = "/data/misc/conn/.psm.info";
#endif
	char power_val = 0;

	fp = filp_open(filepath, O_RDONLY, 0);
	if (IS_ERR(fp) || (!fp)) {
		pr_err("%s is not exist.\n", filepath);
		return -ENOENT; /* -2 */
	}

	kernel_read(fp, fp->f_pos, &power_val, 1);

	/* if power_val is 0, it means rf_test mode by rf. */
	if (power_val == '0') {
		pr_err("*#rf# is enabled.\n");
		EnableRfTestMode = 1;
	} else {
		pr_err("*#rf# is disabled.\n");
		EnableRfTestMode = 0;
	}

	if (fp)
		filp_close(fp, NULL);

	return 0;
}

/* WLAN service driver registration
 * ================================
 */
void slsi_wlan_service_probe(struct scsc_mx_module_client *module_client, struct scsc_mx *mx, enum scsc_module_client_reason reason)
{
	struct slsi_dev            *sdev;
	struct device              *dev;
	struct scsc_service_client mx_wlan_client;
#ifdef CONFIG_SCSC_LOG_COLLECTION
	char buf[SCSC_LOG_FAPI_VERSION_SIZE];
#endif

	SLSI_UNUSED_PARAMETER(module_client);

	SLSI_INFO_NODEV("WLAN service probe\n");

	mutex_lock(&slsi_start_mutex);

	if (reason == SCSC_MODULE_CLIENT_REASON_RECOVERY && !recovery_in_progress)
		goto done;

	if (reason == SCSC_MODULE_CLIENT_REASON_RECOVERY) {
		SLSI_INFO_NODEV("Probe recovery\n");
		sdev = cm_ctx.sdev;
		recovery_in_progress = 0;
		sdev->fail_reported = false;
		sdev->recovery_status = 0;
		sdev->mlme_blocked = false;
		complete_all(&sdev->recovery_completed);
	} else {
		/* Register callbacks */
		mx_wlan_client.stop_on_failure   = wlan_stop_on_failure;
		mx_wlan_client.failure_reset     = wlan_failure_reset;
		mx_wlan_client.suspend           = wlan_suspend;
		mx_wlan_client.resume            = wlan_resume;

		dev = scsc_mx_get_device(mx);

		/* The mutex must be released at this point since the attach
		 * process may call various functions including
		 * slsi_sm_wlan_service_start and slsi_sm_wlan_service_open, which will
		 * claim the same mutex.
		 */
		mutex_unlock(&slsi_start_mutex);
		sdev = slsi_dev_attach(dev, mx, &mx_wlan_client);
		mutex_lock(&slsi_start_mutex);
		if (!sdev) {
			SLSI_ERR_NODEV("WLAN attach failed - slsi_dev_attach\n");
			goto done;
		}

		cm_ctx.sdev = sdev; /* TODO: For now. */

		atomic_set(&sdev->cm_if.cm_if_state, SCSC_WIFI_CM_IF_STATE_PROBING);
		get_device(dev);

#ifdef CONFIG_SCSC_WLAN_HIP4_PROFILING
		hip4_sampler_create(mx);
#endif
#ifdef CONFIG_SCSC_LOG_COLLECTION
		memset(buf, 0, SCSC_LOG_FAPI_VERSION_SIZE);
		/* Write FAPI VERSION to collector header */
		/* IMPORTANT - Do not change the formatting as User space tooling is parsing the string
		 * to read SAP fapi versions.
		 */
		snprintf(buf, SCSC_LOG_FAPI_VERSION_SIZE, "ma:%u.%u, mlme:%u.%u, debug:%u.%u, test:%u.%u",
			 FAPI_MAJOR_VERSION(FAPI_DATA_SAP_VERSION), FAPI_MINOR_VERSION(FAPI_DATA_SAP_VERSION),
			 FAPI_MAJOR_VERSION(FAPI_CONTROL_SAP_VERSION), FAPI_MINOR_VERSION(FAPI_CONTROL_SAP_VERSION),
			 FAPI_MAJOR_VERSION(FAPI_DEBUG_SAP_VERSION), FAPI_MINOR_VERSION(FAPI_DEBUG_SAP_VERSION),
			 FAPI_MAJOR_VERSION(FAPI_TEST_SAP_VERSION), FAPI_MINOR_VERSION(FAPI_TEST_SAP_VERSION));

		scsc_log_collector_write_fapi(buf, SCSC_LOG_FAPI_VERSION_SIZE);
#endif
	}

	if (reason != SCSC_MODULE_CLIENT_REASON_RECOVERY)
		atomic_set(&sdev->cm_if.cm_if_state, SCSC_WIFI_CM_IF_STATE_PROBED);

done:
	mutex_unlock(&slsi_start_mutex);
}

/* service_clean_up_locked expects the slsi_start_mutex mutex to be claimed when
 * service_clean_up_locked is called.
 */
static void service_clean_up_locked(struct slsi_dev *sdev)
{
	atomic_set(&sdev->cm_if.cm_if_state, SCSC_WIFI_CM_IF_STATE_REMOVING);
	put_device(sdev->dev);

	atomic_set(&sdev->cm_if.cm_if_state, SCSC_WIFI_CM_IF_STATE_REMOVED);

	sdev->maxwell_core = NULL;

	/* The mutex must be released at this point since the tear down
	 * process will call various functions including
	 * slsi_sm_wlan_service_stop and slsi_sm_wlan_service_close, which will
	 * claim the same mutex.
	 */
	mutex_unlock(&slsi_start_mutex);
	slsi_dev_detach(sdev);
	mutex_lock(&slsi_start_mutex);
}

static void slsi_wlan_service_remove(struct scsc_mx_module_client *module_client, struct scsc_mx *mx, enum scsc_module_client_reason reason)
{
	struct slsi_dev *sdev;
	int             state;

	SLSI_UNUSED_PARAMETER(mx);
	SLSI_UNUSED_PARAMETER(module_client);

	sdev = cm_ctx.sdev;
	if (!sdev) {
		SLSI_INFO_NODEV("no sdev\n");
		return;
	}

	if (reason == SCSC_MODULE_CLIENT_REASON_RECOVERY && !recovery_in_progress) {
		SLSI_INFO_NODEV("WLAN service remove - recovery. Service not active.\n");
	} else if (reason == SCSC_MODULE_CLIENT_REASON_RECOVERY && recovery_in_progress) {
		int r;

		SLSI_INFO_NODEV("WLAN service remove - recovery\n");

		/* Only indicate if the next state is not stopping. The recovery
		 * handling won't have any affect if the framework is closing
		 * anyway.
		 */
		if (sdev->recovery_next_state != SCSC_WIFI_CM_IF_STATE_STOPPING) {
			SLSI_INFO_NODEV("Nofity registered functions\n");
			blocking_notifier_call_chain(&slsi_wlan_notifier, SCSC_WIFI_FAILURE_RESET, sdev);
		}

		mutex_lock(&slsi_start_mutex);
		/**
		 * If there was a request to stop during the recovery, then do
		 * not sent a hang - just stop here. The Wi-Fi service driver is
		 * ready to be turned on again. Let the service_stop complete.
		 */
		complete_all(&sdev->recovery_remove_completion);
		if (sdev->recovery_next_state == SCSC_WIFI_CM_IF_STATE_STOPPING) {
			SLSI_INFO_NODEV("Recovery - next state stopping\n");
		} else {
			SLSI_INFO_NODEV("Calling slsi_send_hanged_vendor_event with latest_scsc_panic_code=0x%x\n",
					latest_scsc_panic_code);
			if (slsi_send_hanged_vendor_event(sdev, latest_scsc_panic_code) < 0)
				SLSI_ERR(sdev, "Failed to send hang event\n");

			/* Complete any pending ctrl signals, which will prevent
			 * the hang event from being processed.
			 */
			complete_all(&sdev->sig_wait.completion);
		}

		mutex_unlock(&slsi_start_mutex);
		r = wait_for_completion_timeout(&sdev->recovery_stop_completion,
						msecs_to_jiffies(SLSI_SM_WLAN_SERVICE_STOP_RECOVERY_TIMEOUT));
		if (r == 0)
			SLSI_INFO(sdev, "recovery_stop_completion timeout\n");

		mutex_lock(&slsi_start_mutex);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
		reinit_completion(&sdev->recovery_stop_completion);
#else
		/*This is how the macro is used in the older verion.*/
		INIT_COMPLETION(sdev->recovery_stop_completion);
#endif
		mutex_unlock(&slsi_start_mutex);

	} else {
		SLSI_INFO_NODEV("WLAN service remove\n");

#ifdef CONFIG_SCSC_WLAN_DEBUG
		hip4_sampler_destroy(mx);
#endif

		mutex_lock(&slsi_start_mutex);
		state = atomic_read(&sdev->cm_if.cm_if_state);
		if (state != SCSC_WIFI_CM_IF_STATE_STARTED &&
		    state != SCSC_WIFI_CM_IF_STATE_PROBED &&
		    state != SCSC_WIFI_CM_IF_STATE_STOPPED &&
		    state != SCSC_WIFI_CM_IF_STATE_BLOCKED) {
			mutex_unlock(&slsi_start_mutex);
			SLSI_INFO_NODEV("state-event error %d\n", state);
			return;
		}

		service_clean_up_locked(sdev);
		mutex_unlock(&slsi_start_mutex);
	}
}

/* Block future HIP runs through the hip_switch */
static void slsi_hip_block_bh(struct slsi_dev *sdev)
{
	SLSI_WARN(sdev, "HIP state set to #SLSI_HIP_STATE_BLOCKED#\n");
	atomic_set(&sdev->hip.hip_state, SLSI_HIP_STATE_BLOCKED);
}

struct scsc_mx_module_client wlan_driver = {
	.name = "WLAN driver",
	.probe = slsi_wlan_service_probe,
	.remove = slsi_wlan_service_remove,
};

int slsi_sm_service_driver_register(void)
{
	struct slsi_cm_ctx *ctx = &cm_ctx;

	memset(ctx, 0, sizeof(*ctx));
	mutex_init(&slsi_start_mutex);
	scsc_mx_module_register_client_module(&wlan_driver);

	return 0;
}

void slsi_sm_service_driver_unregister(void)
{
	scsc_mx_module_unregister_client_module(&wlan_driver);
}

/* start/stop wlan service
 * =======================
 */
void slsi_sm_service_failed(struct slsi_dev *sdev, const char *reason)
{
	int state;

	mutex_lock(&slsi_start_mutex);

	state = atomic_read(&sdev->cm_if.cm_if_state);
	if (state != SCSC_WIFI_CM_IF_STATE_STARTED &&
	    state != SCSC_WIFI_CM_IF_STATE_STOPPING) {
		mutex_unlock(&slsi_start_mutex);
		SLSI_INFO(sdev, "State %d - ignoring event\n", state);
		return;
	}

	/* Limit the volume of error reports to the core */
	if (!sdev->fail_reported) {
		/* This log may be scraped by test systems */
		SLSI_ERR(sdev, "scsc_wifibt: FATAL ERROR: %s\n", reason);

		atomic_set(&sdev->cm_if.cm_if_state, SCSC_WIFI_CM_IF_STATE_BLOCKED);
		slsi_hip_block_bh(sdev);
		scsc_mx_service_service_failed(sdev->service, reason);
		scsc_mx_service_mif_dump_registers(sdev->service);
		sdev->fail_reported = true;
	}

	mutex_unlock(&slsi_start_mutex);
}

/* Is production test mode enabled? */
bool slsi_is_test_mode_enabled(void)
{
	return EnableTestMode;
}

/* Is production rf test mode enabled? */
bool slsi_is_rf_test_mode_enabled(void)
{
	return EnableRfTestMode;
}

int slsi_sm_wlan_service_open(struct slsi_dev *sdev)
{
	int err = 0;
	int state;

	mutex_lock(&slsi_start_mutex);
	state = atomic_read(&sdev->cm_if.cm_if_state);
	if (state != SCSC_WIFI_CM_IF_STATE_PROBED &&
	    state != SCSC_WIFI_CM_IF_STATE_STOPPED) {
		SLSI_INFO(sdev, "State-event error %d\n", state);
		err = -EINVAL;
		goto exit;
	}

	/* Open service - will download FW - will set MBOX0 with Starting address */
	SLSI_INFO(sdev, "Open WLAN service\n");
	sdev->service = scsc_mx_service_open(sdev->maxwell_core, SCSC_SERVICE_ID_WLAN, &sdev->mx_wlan_client, &err);
	if (!sdev->service) {
		atomic_set(&sdev->cm_if.cm_if_state, SCSC_WIFI_CM_IF_STATE_PROBED);
		SLSI_WARN(sdev, "Service open failed\n");
		err = -EINVAL;
		goto exit;
	}

exit:
	mutex_unlock(&slsi_start_mutex);
	return err;
}

#if defined(CONFIG_SLUB_DEBUG_ON) || defined(CONFIG_DEBUG_PREEMPT) || defined(CONFIG_DEBUG_RT_MUTEXES) || \
	defined(CONFIG_DEBUG_SPINLOCK) && defined(CONFIG_DEBUG_MUTEXES) && defined(CONFIG_DEBUG_LOCK_ALLOC) || \
	defined(CONFIG_DEBUG_LOCK_ALLOC) && defined(CONFIG_DEBUG_ATOMIC_SLEEP) && defined(CONFIG_DEBUG_LIST)
#define KERNEL_DEBUG_OPTIONS_ENABLED
#endif

int slsi_sm_wlan_service_start(struct slsi_dev *sdev)
{
	struct slsi_hip4 *hip = &sdev->hip4_inst;
	scsc_mifram_ref  ref;
	int              err = 0;
	int              err2 = 0;
	int              state;

	mutex_lock(&slsi_start_mutex);
	state = atomic_read(&sdev->cm_if.cm_if_state);
	SLSI_INFO(sdev,
		  "Recovery -- Status:%d  In_Progress:%d  -- cm_if_state:%d\n",
		  sdev->recovery_status, recovery_in_progress, state);
	if (state != SCSC_WIFI_CM_IF_STATE_PROBED &&
	    state != SCSC_WIFI_CM_IF_STATE_STOPPED) {
		SLSI_INFO(sdev, "State-event error %d\n", state);
		mutex_unlock(&slsi_start_mutex);
		return -EINVAL;
	}

	atomic_set(&sdev->cm_if.cm_if_state, SCSC_WIFI_CM_IF_STATE_STARTING);

#ifdef KERNEL_DEBUG_OPTIONS_ENABLED
	SLSI_WARN(sdev, "Kernel config debug options are enabled. This might impact the throughput performance.\n");
#endif

	/* Get RAM from the MIF */
	SLSI_INFO(sdev, "Allocate mifram\n");
	err = scsc_mx_service_mifram_alloc(sdev->service, 2 * 1024 * 1024, &sdev->hip4_inst.hip_ref, 4096);
	if (err) {
		SLSI_WARN(sdev, "scsc_mx_service_mifram_alloc failed err: %d\n", err);
		atomic_set(&sdev->cm_if.cm_if_state, SCSC_WIFI_CM_IF_STATE_STOPPED);
		mutex_unlock(&slsi_start_mutex);
		return err;
	}

	SLSI_INFO(sdev, "Start HIP\n");
	err = slsi_hip_start(sdev);
	if (err) {
		SLSI_WARN(sdev, "slsi_hip_start failed err: %d\n", err);
		atomic_set(&sdev->cm_if.cm_if_state, SCSC_WIFI_CM_IF_STATE_STOPPED);
		slsi_hip_stop(sdev);
		mutex_unlock(&slsi_start_mutex);
		return err;
	}

	err = scsc_mx_service_mif_ptr_to_addr(sdev->service, hip->hip_control, &ref);
	if (err) {
		SLSI_WARN(sdev, "scsc_mx_service_mif_ptr_to_addr failed err: %d\n", err);
		atomic_set(&sdev->cm_if.cm_if_state, SCSC_WIFI_CM_IF_STATE_STOPPED);
		slsi_hip_stop(sdev);
		mutex_unlock(&slsi_start_mutex);
		return err;
	}

	SLSI_INFO(sdev, "Starting WLAN service\n");
	err = scsc_mx_service_start(sdev->service, ref);
	if (err) {
		SLSI_WARN(sdev, "scsc_mx_service_start failed err: %d\n", err);
		atomic_set(&sdev->cm_if.cm_if_state, SCSC_WIFI_CM_IF_STATE_STOPPED);
		slsi_hip_stop(sdev);
		mutex_unlock(&slsi_start_mutex);
		return err;
	}
	err = slsi_hip_setup(sdev);
	if (err) {
		SLSI_WARN(sdev, "slsi_hip_setup failed err: %d\n", err);
		atomic_set(&sdev->cm_if.cm_if_state, SCSC_WIFI_CM_IF_STATE_STOPPED);
		SLSI_INFO_NODEV("Stopping WLAN service\n");
		err2 = scsc_mx_service_stop(sdev->service);
		if (err2)
			SLSI_INFO(sdev, "scsc_mx_service_stop failed err2: %d\n", err2);
		slsi_hip_stop(sdev);
		mutex_unlock(&slsi_start_mutex);
		return err;
	}
	/* Service has started, inform SAP versions to the registered SAPs */
	err = slsi_hip_sap_setup(sdev);
	if (err) {
		SLSI_WARN(sdev, "slsi_hip_sap_setup failed err: %d\n", err);
		atomic_set(&sdev->cm_if.cm_if_state, SCSC_WIFI_CM_IF_STATE_STOPPED);
		SLSI_INFO_NODEV("Stopping WLAN service\n");
		err2 = scsc_mx_service_stop(sdev->service);
		if (err2)
			SLSI_INFO(sdev, "scsc_mx_service_stop failed err2: %d\n", err2);
		slsi_hip_stop(sdev);
		mutex_unlock(&slsi_start_mutex);
		return err;
	}
	atomic_set(&sdev->cm_if.cm_if_state, SCSC_WIFI_CM_IF_STATE_STARTED);
	mutex_unlock(&slsi_start_mutex);
	return 0;
}

static void __slsi_sm_wlan_service_stop_wait_locked(struct slsi_dev *sdev)
{
	int r;

	mutex_unlock(&slsi_start_mutex);
	r = wait_for_completion_timeout(&sdev->recovery_remove_completion,
					msecs_to_jiffies(SLSI_SM_WLAN_SERVICE_STOP_RECOVERY_TIMEOUT));
	if (r == 0)
		SLSI_INFO(sdev, "recovery_remove_completion timeout\n");

	mutex_lock(&slsi_start_mutex);
	sdev->recovery_next_state = SCSC_WIFI_CM_IF_STATE_STOPPED;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
	reinit_completion(&sdev->recovery_remove_completion);
#else
	/*This is how the macro is used in the older verion.*/
	INIT_COMPLETION(sdev->recovery_remove_completion);
#endif
}

void slsi_sm_wlan_service_stop(struct slsi_dev *sdev)
{
	int cm_if_state;
	int err = 0;

	mutex_lock(&slsi_start_mutex);
	cm_if_state = atomic_read(&sdev->cm_if.cm_if_state);
	SLSI_INFO(sdev,
		  "Recovery -- Status:%d  In_Progress:%d  -- cm_if_state:%d\n",
		  sdev->recovery_status, recovery_in_progress, cm_if_state);

	if (cm_if_state == SCSC_WIFI_CM_IF_STATE_BLOCKED) {
		__slsi_sm_wlan_service_stop_wait_locked(sdev);

		/* If the wait hasn't timed out, the recovery remove completion
		 * will have completed properly and the cm_if_state will be
		 * set to stopped here. If the probe hasn't fired for some reason
		 * try and do a service_stop regardless, since that's all we can
		 * do in this situation; hence skip the state check.
		 */
		 goto skip_state_check;
	}

	if (cm_if_state != SCSC_WIFI_CM_IF_STATE_STARTED &&
	    cm_if_state != SCSC_WIFI_CM_IF_STATE_REMOVED &&
	    cm_if_state != SCSC_WIFI_CM_IF_STATE_PROBED) {
		SLSI_INFO(sdev, "Service not started or incorrect state %d\n",
			  cm_if_state);
		goto exit;
	}

	/**
	 * Note that the SCSC_WIFI_CM_IF_STATE_STOPPING state will inhibit
	 * auto-recovery mechanism, so be careful not to abuse it: as an
	 * example if panic happens on start or stop we don't want to
	 * un-necessarily pass by STOPPING in order to have a successful
	 * recovery in such a situation.
	 */
	atomic_set(&sdev->cm_if.cm_if_state, SCSC_WIFI_CM_IF_STATE_STOPPING);
skip_state_check:
	SLSI_INFO_NODEV("Stopping WLAN service\n");
	err = scsc_mx_service_stop(sdev->service);
	if (err == -EILSEQ) {
		/* scsc_mx_service_stop failed since there's a recovery in
		 * progress, so just wait for it to complete and try again.
		 */
		SLSI_INFO(sdev, "scsc_mx_service_stop failed err: %d\n", err);
		__slsi_sm_wlan_service_stop_wait_locked(sdev);
		goto skip_state_check;
	} else if (err == -EIO) {
		char reason[80];

		SLSI_INFO(sdev, "scsc_mx_service_stop failed err: %d\n", err);

		/* scsc_mx_service_stop since there was no respons from firmware
		 * to the stop request. Generate a host initiated panic to reset
		 * the chip and wait for it to complete.
		 */
		sdev->recovery_next_state = SCSC_WIFI_CM_IF_STATE_STOPPING;
		snprintf(reason, sizeof(reason), "WLAN scsc_mx_service_stop failed");

		mutex_unlock(&slsi_start_mutex);
		slsi_sm_service_failed(sdev, reason);
		mutex_lock(&slsi_start_mutex);

		__slsi_sm_wlan_service_stop_wait_locked(sdev);
	} else if (err == -EPERM) {
		/* Special case when recovery is disabled, otherwise the driver
		 * will wait forever for recovery that never comes
		 */
		SLSI_INFO(sdev, "refused due to previous failure, recovery is disabled: %d\n", err);
	} else if (err != 0) {
		SLSI_INFO(sdev, "scsc_mx_service_stop failed, unknown err: %d\n", err);
	}

	atomic_set(&sdev->cm_if.cm_if_state, SCSC_WIFI_CM_IF_STATE_STOPPED);
exit:
	mutex_unlock(&slsi_start_mutex);
}

#define SLSI_SM_WLAN_SERVICE_CLOSE_RETRY 60
void slsi_sm_wlan_service_close(struct slsi_dev *sdev)
{
	int cm_if_state, r;

	mutex_lock(&slsi_start_mutex);
	cm_if_state = atomic_read(&sdev->cm_if.cm_if_state);
	if (cm_if_state != SCSC_WIFI_CM_IF_STATE_STOPPED) {
		SLSI_INFO(sdev, "Service not stopped\n");
		goto exit;
	}

	SLSI_INFO_NODEV("Closing WLAN service\n");
	scsc_mx_service_mifram_free(sdev->service, sdev->hip4_inst.hip_ref);
	r = scsc_mx_service_close(sdev->service);
	if (r == -EIO) {
		int retry_counter;

		/**
		 * Error handling in progress - try and close again later.
		 * The service close call shall remain blocked until close
		 * service is successful. Try up to 30 seconds.
		 */
		for (retry_counter = 0;
		     SLSI_SM_WLAN_SERVICE_CLOSE_RETRY > retry_counter;
		     retry_counter++) {
			msleep(500);
			r = scsc_mx_service_close(sdev->service);
			if (r == 0) {
				SLSI_INFO(sdev, "scsc_mx_service_close closed after %d attempts\n",
					  retry_counter + 1);
				break;
			}
		}

		if (retry_counter + 1 == SLSI_SM_WLAN_SERVICE_CLOSE_RETRY)
			SLSI_ERR(sdev, "scsc_mx_service_close failed %d times\n",
				 SLSI_SM_WLAN_SERVICE_CLOSE_RETRY);
	} else if (r == -EPERM) {
		SLSI_ERR(sdev, "scsc_mx_service_close - recovery is disabled (%d)\n", r);
	}

	if (recovery_in_progress)
		complete_all(&sdev->recovery_stop_completion);
exit:
	mutex_unlock(&slsi_start_mutex);
}
