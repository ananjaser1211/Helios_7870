/* drivers/gpu/arm/.../platform/gpu_integration_defs.h
 *
 * Copyright 2011 by S.LSI. Samsung Electronics Inc.
 * San#24, Nongseo-Dong, Giheung-Gu, Yongin, Korea
 *
 * Samsung SoC Mali-T Series DDK porting layer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software FoundatIon.
 */

/**
 * @file gpu_integration_defs.h
 * DDK porting layer.
 */

#ifndef _SEC_INTEGRATION_H_
#define _SEC_INTEGRATION_H_

#include <mali_kbase.h>
#include <mali_kbase_mem_linux.h>
#include "mali_kbase_platform.h"
#include "gpu_dvfs_handler.h"

#ifdef MALI_SEC_HWCNT
#include "gpu_hwcnt.h"
#endif

/* kctx initialized with zero from vzalloc, so initialized value required only */
#define CTX_UNINITIALIZED 0x0
#define CTX_INITIALIZED 0x1
#define CTX_DESTROYED 0x2
#define CTX_NAME_SIZE 32

/* MALI_SEC_SECURE_RENDERING */
/* if the smc call is successfully done, ret value will be 2 */
#define SMC_CALL_SUCCESS  0x2
#define SMC_CALL_ERROR    0x1

/* MALI_SEC_INTEGRATION */
#ifdef MALI_SEC_CL_BOOST
#define KBASE_PM_TIME_SHIFT			8
#endif

/* MALI_SEC_INTEGRATION */
#define MEM_FREE_LIMITS 16384
#define MEM_FREE_DEFAULT 16384

int kbase_alloc_phy_pages_helper_gpu(struct kbase_va_region * reg, size_t nr_pages_to_free);
int kbase_free_phy_pages_helper_gpu(struct kbase_va_region * reg, size_t nr_pages_to_free);
mali_error gpu_vendor_dispatch(struct kbase_context *kctx, void * const args, u32 args_size);
void kbasep_js_cacheclean(struct kbase_device *kbdev);

#ifdef MALI_SEC_FENCE_INTEGRATION
void kbase_fence_del_timer(void *atom);
#endif

struct kbase_vendor_data {
	void (* create_context)(void *ctx);
	void (* destroy_context)(void *ctx);
	void (* pm_metrics_init)(void *dev);
	void (* pm_metrics_term)(void *dev);
	void (* cl_boost_init)(void *dev);
	void (* cl_boost_update_utilization)(void *dev, void *atom, u64 microseconds_spent);
	void (* fence_timer_init)(void *atom);
	void (* fence_del_timer)(void *atom);
	int (* get_core_mask)(void *dev);
	int (* init_hw)(void *dev);
	void (* hwcnt_init)(void *dev);
	void (* hwcnt_remove)(void *dev);
	void (* hwcnt_prepare_suspend)(void *dev);
	void (* hwcnt_prepare_resume)(void *dev);
	mali_error (* hwcnt_update)(void *dev);
	void (* hwcnt_power_up)(void *dev);
	void (* hwcnt_power_down)(void *dev);
	void (* set_poweron_dbg)(mali_bool enable_dbg);
	mali_bool (* get_poweron_dbg)(void);
	void (* debug_pagetable_info)(void *ctx, mali_addr64 vaddr);
	void (* jd_done_worker)(void *dev);
	void (* update_status)(void *dev, char *str, u32 val);
	void (* mem_init)(void *dev);
	mali_bool (* mem_profile_check_kctx)(void *ctx);
	void (* pm_record_state)(void *kbdev, mali_bool is_active);
};

#endif /* _SEC_INTEGRATION_H_ */
