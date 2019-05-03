/* drivers/gpu/arm/.../platform/gpu_integration_callbacks.c
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
 * @file gpu_integration_callbacks.c
 * DDK porting layer.
 */

#include <mali_kbase.h>
#include <mali_midg_regmap.h>

#include <linux/pm_qos.h>
#include <linux/sched.h>

#include <mali_kbase_gpu_memory_debugfs.h>

/*
* peak_flops: 100/85
* sobel: 100/50
*/
#define COMPUTE_JOB_WEIGHT (10000/50)

#ifdef CONFIG_SENSORS_SEC_THERMISTOR
extern int sec_therm_get_ap_temperature(void);
#endif

#ifdef CONFIG_SCHED_HMP
extern int set_hmp_boost(int enable);
#endif

#ifdef CONFIG_USE_VSYNC_SKIP
void decon_extra_vsync_wait_set(int);
void decon_extra_vsync_wait_add(int);
#endif

#ifdef MALI_SEC_SEPERATED_UTILIZATION
/** Notify the Power Management Metrics System that the GPU active state might
 * have changed.
 *
 * If it has indeed changed since the last time the Metrics System was
 * notified, then it calculates the active/idle time. Otherwise, it does
 * nothing. For example, the caller can signal going idle when the last non-hw
 * counter context deschedules, and then signals going idle again when the
 * hwcounter context itself also deschedules.
 *
 * If there is only one context left running and that is HW counters
 * collection, then the caller should set @p is_active to MALI_FALSE. This has
 * a side effect that counter collecting contexts that also run jobs will be
 * invisible to utilization metrics. Note that gator cannot run jobs itself, so
 * is unaffected by this.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid
 *                  pointer)
 * @param is_active Indicator that GPU must be recorded active (MALI_TRUE), or
 *                  idle (MALI_FALSE)
 */
void gpu_pm_record_state(void *dev, mali_bool is_active);
#endif


void gpu_create_context(void *ctx)
{
	struct kbase_context *kctx;
	char current_name[sizeof(current->comm)];

	kctx = (struct kbase_context *)ctx;
	KBASE_DEBUG_ASSERT(kctx != NULL);

	kctx->ctx_status = CTX_UNINITIALIZED;
	kctx->ctx_need_qos = false;

	get_task_comm(current_name, current);
	strncpy((char *)(&kctx->name), current_name, CTX_NAME_SIZE);

	atomic_set(&kctx->used_pmem_pages, 0);
	atomic_set(&kctx->used_tmem_pages, 0);

	kctx->ctx_status = CTX_INITIALIZED;

	/* MALI_SEC_SECURE_RENDERING */
	kctx->enabled_TZASC = MALI_FALSE;

	kctx->destroying_context = MALI_FALSE;
}

void gpu_destroy_context(void *ctx)
{
	struct kbase_context *kctx;
	struct kbase_device *kbdev;

	kctx = (struct kbase_context *)ctx;
	KBASE_DEBUG_ASSERT(kctx != NULL);

	kbdev = kctx->kbdev;
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	kctx->destroying_context = MALI_TRUE;

	/* MALI_SEC_SECURE_RENDERING */
	if (kctx->enabled_TZASC == MALI_TRUE &&
		kbdev->js_data.secure_ops != NULL &&
		kbdev->js_data.secure_ops->secure_mem_disable != NULL) {
		int res = 0;

		/* Switch GPU to non-secure world mode */
		res = kbdev->js_data.secure_ops->secure_mem_disable();
		if (res == SMC_CALL_ERROR)
			dev_err(kbdev->dev, "G3D - context : cannot disable the protection mode.\n");
		else
			printk("[G3D] - context : disable the protection mode, kctx : %p\n", kctx);
		BUG_ON(res == SMC_CALL_ERROR);
		kctx->enabled_TZASC = MALI_FALSE;
	}

#ifdef MALI_SEC_HWCNT
	if ((kbdev->hwcnt.kctx != kctx) && (kbdev->hwcnt.kctx_gpr == kctx)) {
		if (kbdev->hwcnt.is_init) {
			kbdev->hwcnt.triggered = 1;
			kbdev->hwcnt.trig_exception = 1;
			wake_up(&kbdev->hwcnt.wait);

			mutex_lock(&kbdev->hwcnt.mlock);

			if (kbdev->hwcnt.kctx) {
				kbdev->hwcnt.state = KBASE_INSTR_STATE_IDLE;
				hwcnt_stop(kbdev);
			}

			kbdev->hwcnt.enable_for_gpr = FALSE;
			kbdev->hwcnt.enable_for_utilization = kbdev->hwcnt.s_enable_for_utilization;
			kbdev->hwcnt.kctx_gpr = NULL;

			mutex_unlock(&kbdev->hwcnt.mlock);
		}
	}
#endif
	kctx->ctx_status = CTX_DESTROYED;

	if (kctx->ctx_need_qos)
	{
#ifdef CONFIG_MALI_DVFS
		gpu_dvfs_boost_lock(GPU_DVFS_BOOST_UNSET);
#endif
#ifdef CONFIG_SCHED_HMP
		set_hmp_boost(0);
		set_hmp_aggressive_up_migration(false);
		set_hmp_aggressive_yield(false);
#endif
	}
}

mali_error gpu_vendor_dispatch(struct kbase_context *kctx, void * const args, u32 args_size)
{
	struct kbase_device *kbdev;
	union uk_header *ukh = args;
	u32 id;

	KBASE_DEBUG_ASSERT(ukh != NULL);

	kbdev = kctx->kbdev;
	id = ukh->id;
	ukh->ret = MALI_ERROR_NONE;	/* Be optimistic */

	switch(id)
	{
#ifdef MALI_SEC_HWCNT
	case KBASE_FUNC_HWCNT_UTIL_SETUP:
	{
		struct kbase_uk_hwcnt_setup *setup = args;

		if (sizeof(*setup) != args_size)
			goto bad_size;

		if (MALI_ERROR_NONE != hwcnt_setup(kctx, setup))
			ukh->ret = MALI_ERROR_FUNCTION_FAILED;
		break;
	}

	case KBASE_FUNC_HWCNT_GPR_DUMP:
		{
			struct kbase_uk_hwcnt_gpr_dump *dump = args;

			if (sizeof(*dump) != args_size)
				goto bad_size;

			mutex_lock(&kbdev->hwcnt.mlock);
			if (kbdev->js_data.runpool_irq.secure_mode == MALI_TRUE) {
				mutex_unlock(&kbdev->hwcnt.mlock);
				dev_err(kbdev->dev, "cannot support ioctl %u in secure mode", id);
				break;
			}

			if (MALI_ERROR_NONE != hwcnt_dump(kctx)) {
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
				mutex_unlock(&kbdev->hwcnt.mlock);
				break;
			}
			hwcnt_get_gpr_resource(kbdev, dump);
			mutex_unlock(&kbdev->hwcnt.mlock);
			break;
		}

	case KBASE_FUNC_TMU_SKIP:
		{
/* MALI_SEC_INTEGRATION */
#ifdef CONFIG_SENSORS_SEC_THERMISTOR
#ifdef CONFIG_USE_VSYNC_SKIP
			struct kbase_uk_tmu_skip *tskip = args;
			int thermistor = sec_therm_get_ap_temperature();
			u32 i, t_index = tskip->num_ratiometer;

			for (i = 0; i < tskip->num_ratiometer; i++)
				if (thermistor >= tskip->temperature[i])
					t_index = i;

			if (t_index < tskip->num_ratiometer) {
				decon_extra_vsync_wait_add(tskip->skip_count[t_index]);
				ukh->ret = MALI_ERROR_NONE;
			} else {
				decon_extra_vsync_wait_set(0);
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			}

#endif /* CONFIG_USE_VSYNC_SKIP */
#endif /* CONFIG_SENSORS_SEC_THERMISTOR */
			break;
		}

	case KBASE_FUNC_VSYNC_SKIP:
		{
			struct kbase_uk_vsync_skip *vskip = args;

			if (sizeof(*vskip) != args_size)
				goto bad_size;
/* MALI_SEC_INTEGRATION */
#ifdef CONFIG_USE_VSYNC_SKIP
			/* increment vsync skip variable that is used in fimd driver */
			KBASE_TRACE_ADD_EXYNOS(kbdev, LSI_HWCNT_VSYNC_SKIP, NULL, NULL, 0u, vskip->skip_count);

			if (vskip->skip_count == 0) {
				decon_extra_vsync_wait_set(0);
			} else {
				decon_extra_vsync_wait_add(vskip->skip_count);
			}
#endif /* CONFIG_USE_VSYNC_SKIP */
			break;
		}
#endif

	case KBASE_FUNC_CREATE_SURFACE:
		{
			kbase_mem_set_max_size(kctx);
			break;
		}

	case KBASE_FUNC_DESTROY_SURFACE:
		{
			kbase_mem_free_list_cleanup(kctx);
			break;
		}

	case KBASE_FUNC_SET_MIN_LOCK :
		{
#ifdef CONFIG_MALI_DVFS
			struct kbase_uk_keep_gpu_powered *kgp = (struct kbase_uk_keep_gpu_powered *)args;
#endif /* CONFIG_MALI_DVFS */
			if (!kctx->ctx_need_qos) {
				kctx->ctx_need_qos = true;
#ifdef CONFIG_SCHED_HMP
				set_hmp_boost(1);
				set_hmp_aggressive_up_migration(true);
				set_hmp_aggressive_yield(true);
#endif
#ifdef CONFIG_MALI_DVFS
				if (kgp->padding) {
					struct exynos_context *platform;
					platform = (struct exynos_context *) kbdev->platform_context;
					platform->boost_egl_min_lock = kgp->padding;
					gpu_pm_qos_command(platform, GPU_CONTROL_PM_QOS_EGL_SET);
				} else {
					gpu_dvfs_boost_lock(GPU_DVFS_BOOST_SET);
				}
#endif /* CONFIG_MALI_DVFS */
			}
			break;
		}

	case KBASE_FUNC_UNSET_MIN_LOCK :
		{
#ifdef CONFIG_MALI_DVFS
			struct kbase_uk_keep_gpu_powered *kgp = (struct kbase_uk_keep_gpu_powered *)args;
#endif /* CONFIG_MALI_DVFS */
			if (kctx->ctx_need_qos) {
				kctx->ctx_need_qos = false;
#ifdef CONFIG_SCHED_HMP
				set_hmp_boost(0);
				set_hmp_aggressive_up_migration(false);
				set_hmp_aggressive_yield(false);
#endif
#ifdef CONFIG_MALI_DVFS
				if (kgp->padding) {
					struct exynos_context *platform;
					platform = (struct exynos_context *) kbdev->platform_context;
					platform->boost_egl_min_lock = 0;
					gpu_pm_qos_command(platform, GPU_CONTROL_PM_QOS_EGL_RESET);
				} else {
					gpu_dvfs_boost_lock(GPU_DVFS_BOOST_UNSET);
				}
#endif /* CONFIG_MALI_DVFS */
			}
			break;
		}

	/* MALI_SEC_SECURE_RENDERING */
	case KBASE_FUNC_SECURE_WORLD_RENDERING :
	{
		if (kctx->enabled_TZASC == MALI_FALSE &&
			kbdev->js_data.secure_ops != NULL &&
			kbdev->js_data.secure_ops->secure_mem_enable != NULL) {
			int res = 0;

			/* Switch GPU to secure world mode */
			res = kbdev->js_data.secure_ops->secure_mem_enable();
			if (res == SMC_CALL_ERROR)
				dev_err(kbdev->dev, "[G3D] - IOCTL : cannot enable the protection mode.\n");
			else
				printk("[G3D] - IOCTL : enable the protection mode, kctx : %p\n", kctx);
			BUG_ON(res == SMC_CALL_ERROR);
			kctx->enabled_TZASC = MALI_TRUE;
		}
		break;
	}

	/* MALI_SEC_SECURE_RENDERING */
	case KBASE_FUNC_NON_SECURE_WORLD_RENDERING :
	{
		if (kctx->enabled_TZASC == MALI_TRUE &&
			kbdev->js_data.secure_ops != NULL &&
			kbdev->js_data.secure_ops->secure_mem_disable != NULL) {
			int res = 0;

			/* Switch GPU to non-secure world mode */
			res = kbdev->js_data.secure_ops->secure_mem_disable();
			if (res == SMC_CALL_ERROR)
				dev_err(kbdev->dev, "[G3D] - IOCTL : cannot disable the protection mode.\n");
			else
				printk("[G3D] - IOCTL : disable the protection mode, kctx : %p\n", kctx);
			BUG_ON(res == SMC_CALL_ERROR);
			kctx->enabled_TZASC = MALI_FALSE;
		}
		break;
	}
	default:
		break;
	}

	return MALI_ERROR_NONE;

bad_size:
	dev_err(kbdev->dev, "Wrong syscall size (%d) for %08x\n", args_size, id);
	return MALI_ERROR_FUNCTION_FAILED;
}

#include <mali_kbase_gpu_memory_debugfs.h>
int gpu_memory_seq_show(struct seq_file *sfile, void *data)
{
	ssize_t ret = 0;
	struct list_head *entry;
	const struct list_head *kbdev_list;
	size_t free_size = 0;

	kbdev_list = kbase_dev_list_get();
	list_for_each(entry, kbdev_list) {
		struct kbase_device *kbdev = NULL;
		struct kbasep_kctx_list_element *element;

		kbdev = list_entry(entry, struct kbase_device, entry);
		/* output the total memory usage and cap for this device */
		mutex_lock(&kbdev->kctx_list_lock);
		list_for_each_entry(element, &kbdev->kctx_list, link) {
			free_size += atomic_read(&(element->kctx->osalloc.free_list_size));
		}
		mutex_unlock(&kbdev->kctx_list_lock);
		ret = seq_printf(sfile, "===========================================================\n");
		ret = seq_printf(sfile, " %16s  %18s  %20s\n", \
				"dev name", \
				"total used pages", \
				"total shrink pages");
		ret = seq_printf(sfile, "-----------------------------------------------------------\n");
		ret = seq_printf(sfile, " %16s  %18u  %20zu\n", \
				kbdev->devname, \
				atomic_read(&(kbdev->memdev.used_pages)), \
				free_size);
		ret = seq_printf(sfile, "===========================================================\n\n");
		ret = seq_printf(sfile, "%28s     %10s  %12s  %10s  %10s  %10s  %10s\n", \
				"context name", \
				"context addr", \
				"used pages", \
				"shrink pages", \
				"pmem pages", \
				"tmem pages", \
				"others");
		ret = seq_printf(sfile, "====================================================");
		ret = seq_printf(sfile, "=========================================================\n");
		mutex_lock(&kbdev->kctx_list_lock);
		list_for_each_entry(element, &kbdev->kctx_list, link) {
			/* output the memory usage and cap for each kctx
			* opened on this device */

			ret = seq_printf(sfile, "  (%24s), %s-0x%pK    %12u  %10u  %10u  %10u  %10u\n", \
				element->kctx->name, \
				"kctx", \
				element->kctx, \
				atomic_read(&(element->kctx->used_pages)),
				atomic_read(&(element->kctx->osalloc.free_list_size)),
				atomic_read(&(element->kctx->used_pmem_pages)),
				atomic_read(&(element->kctx->used_tmem_pages)),
				atomic_read(&(element->kctx->used_pages)) - atomic_read(&(element->kctx->used_pmem_pages)) - atomic_read(&(element->kctx->used_tmem_pages)));
		}
		mutex_unlock(&kbdev->kctx_list_lock);
	}
	kbase_dev_list_put(kbdev_list);
	return ret;
}

void gpu_update_status(void *dev, char *str, u32 val)
{
	struct kbase_device *kbdev;

	kbdev = (struct kbase_device *)dev;
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	if(strcmp(str, "completion_code") == 0)
	{
		if(val == 0x58) // DATA_INVALID_FAULT
			((struct exynos_context *)kbdev->platform_context)->data_invalid_fault_count ++;
		else if((val & 0xf0) == 0xc0) // MMU_FAULT
			((struct exynos_context *)kbdev->platform_context)->mmu_fault_count ++;

	}
	else if(strcmp(str, "reset_count") == 0)
		((struct exynos_context *)kbdev->platform_context)->reset_count++;
}

void gpu_jd_done_worker(void *dev)
{
	struct kbase_device *kbdev;
	mali_error err = MALI_ERROR_FUNCTION_FAILED;
	int now;
	int diff_ms;
	struct exynos_context *platform;

	kbdev = (struct kbase_device *)dev;
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	if ((kbdev->hwcnt.kctx) && (kbdev->hwcnt.is_init) && (kbdev->hwcnt.enable_for_utilization == TRUE)) {
		now = ktime_to_ms(ktime_get());
		diff_ms = now - kbdev->hwcnt.hwcnt_prev_time;
		platform = (struct exynos_context *) kbdev->platform_context;

		if (diff_ms > platform->hwcnt_dump_period) {
			mutex_lock(&kbdev->hwcnt.mlock);
			if (kbdev->js_data.runpool_irq.secure_mode == MALI_FALSE) {
				kbdev->hwcnt.hwcnt_prev_time = now;
				err = hwcnt_dump(kbdev->hwcnt.kctx);
				if (err != MALI_ERROR_NONE) {
					dev_err(kbdev->dev, "hwcnt dump error in %s %d \n", __FUNCTION__, err);
				}
			}
			mutex_unlock(&kbdev->hwcnt.mlock);
		}
	}
}

/* MALI_SEC_SECURE_RENDERING */
void kbasep_js_cacheclean(struct kbase_device *kbdev)
{
    /* Limit the number of loops to avoid a hang if the interrupt is missed */
    u32 max_loops = KBASE_CLEAN_CACHE_MAX_LOOPS;

    /* use GPU_COMMAND completion solution */
    /* clean the caches */
    kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND), GPU_COMMAND_CLEAN_CACHES, NULL);

    /* wait for cache flush to complete before continuing */
    while (--max_loops && (kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_IRQ_RAWSTAT), NULL) & CLEAN_CACHES_COMPLETED) == 0)
        ;

    /* clear the CLEAN_CACHES_COMPLETED irq */
    kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_CLEAR), CLEAN_CACHES_COMPLETED, NULL);
    KBASE_DEBUG_ASSERT_MSG(kbdev->hwcnt.state != KBASE_INSTR_STATE_CLEANING,
        "Instrumentation code was cleaning caches, but Job Management code cleared their IRQ - Instrumentation code will now hang.");
}

/* MALI_SEC_INTEGRATION */
int kbase_alloc_phy_pages_helper_gpu(struct kbase_va_region * reg, size_t nr_pages_to_free)
{
	if (0 == nr_pages_to_free)
		return 0;

	if (reg->flags & KBASE_REG_CUSTOM_PMEM)
	{
		kbase_atomic_add_pages(nr_pages_to_free, &reg->alloc->imported.kctx->used_pmem_pages);
		kbase_atomic_add_pages(nr_pages_to_free, &reg->alloc->imported.kctx->kbdev->memdev.used_pmem_pages);
	}
	else if (reg->flags & KBASE_REG_CUSTOM_TMEM)
	{
		kbase_atomic_add_pages(nr_pages_to_free, &reg->alloc->imported.kctx->used_tmem_pages);
		kbase_atomic_add_pages(nr_pages_to_free, &reg->alloc->imported.kctx->kbdev->memdev.used_tmem_pages);
	}

	return 0;
}

/* MALI_SEC_INTEGRATION */
int kbase_free_phy_pages_helper_gpu(struct kbase_va_region * reg, size_t nr_pages_to_free)
{
	if (0 == nr_pages_to_free)
		return 0;

	if (reg->flags & KBASE_REG_CUSTOM_PMEM)
	{
		kbase_atomic_sub_pages(nr_pages_to_free, &reg->alloc->imported.kctx->used_pmem_pages);
		kbase_atomic_sub_pages(nr_pages_to_free, &reg->alloc->imported.kctx->kbdev->memdev.used_pmem_pages);
	}
	else if (reg->flags & KBASE_REG_CUSTOM_TMEM)
	{
		kbase_atomic_sub_pages(nr_pages_to_free, &reg->alloc->imported.kctx->used_tmem_pages);
		kbase_atomic_sub_pages(nr_pages_to_free, &reg->alloc->imported.kctx->kbdev->memdev.used_tmem_pages);
	}

	return 0;
}

void gpu_mem_init(void *dev)
{
	struct kbase_device *kbdev;
	struct kbasep_mem_device *memdev;

	kbdev = (struct kbase_device *)dev;
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	memdev = &kbdev->memdev;
	KBASE_DEBUG_ASSERT(memdev != NULL);

	/* MALI_SEC_INTEGRATION */
	atomic_set(&memdev->used_pmem_pages, 0);
	atomic_set(&memdev->used_tmem_pages, 0);
}

void kbase_mem_set_max_size(struct kbase_context *kctx)
{
	struct kbase_mem_allocator *allocator = &kctx->osalloc;
	mutex_lock(&allocator->free_list_lock);
	allocator->free_list_max_size = MEM_FREE_DEFAULT;
	mutex_unlock(&allocator->free_list_lock);
}

void kbase_mem_free_list_cleanup(struct kbase_context *kctx)
{
	int tofree,i=0;
	struct kbase_mem_allocator *allocator = &kctx->osalloc;
	tofree = MAX(MEM_FREE_LIMITS, atomic_read(&allocator->free_list_size)) - MEM_FREE_LIMITS;
	if (tofree > 0)
	{
		struct page *p;
		mutex_lock(&allocator->free_list_lock);
	        allocator->free_list_max_size = MEM_FREE_LIMITS;
		for(i=0; i < tofree; i++)
		{
			p = list_first_entry(&allocator->free_list_head, struct page, lru);
			list_del(&p->lru);
			if (likely(0 != p))
			{
			    dma_unmap_page(allocator->kbdev->dev, page_private(p),
				    PAGE_SIZE,
				    DMA_BIDIRECTIONAL);
			    ClearPagePrivate(p);
			    __free_page(p);
			}
		}
		atomic_set(&allocator->free_list_size, MEM_FREE_LIMITS);
		mutex_unlock(&allocator->free_list_lock);
	}
}

#define KBASE_MMU_PAGE_ENTRIES	512

static phys_addr_t mmu_pte_to_phy_addr(u64 entry)
{
	if (!(entry & 1))
		return 0;

	return entry & ~0xFFF;
}

/* MALI_SEC_INTEGRATION */
static void gpu_page_table_info_dp_level(struct kbase_context *kctx, mali_addr64 vaddr, phys_addr_t pgd, int level)
{
	u64 *pgd_page;
	int i;
	int index = (vaddr >> (12 + ((3 - level) * 9))) & 0x1FF;
	int min_index = index - 3;
	int max_index = index + 3;

	if (min_index < 0)
		min_index = 0;
	if (max_index >= KBASE_MMU_PAGE_ENTRIES)
		max_index = KBASE_MMU_PAGE_ENTRIES - 1;

	/* Map and dump entire page */

	pgd_page = kmap(pfn_to_page(PFN_DOWN(pgd)));

	dev_err(kctx->kbdev->dev, "Dumping level %d @ physical address 0x%016llX (matching index %d):\n", level, pgd, index);

	if (!pgd_page) {
		dev_err(kctx->kbdev->dev, "kmap failure\n");
		return;
	}

	for (i = min_index; i <= max_index; i++) {
		if (i == index) {
			dev_err(kctx->kbdev->dev, "[%03d]: 0x%016llX *\n", i, pgd_page[i]);
		} else {
			dev_err(kctx->kbdev->dev, "[%03d]: 0x%016llX\n", i, pgd_page[i]);
		}
	}

	/* parse next level (if any) */

	if ((pgd_page[index] & 3) == ENTRY_IS_PTE) {
		phys_addr_t target_pgd = mmu_pte_to_phy_addr(pgd_page[index]);
		gpu_page_table_info_dp_level(kctx, vaddr, target_pgd, level + 1);
	} else if ((pgd_page[index] & 3) == ENTRY_IS_ATE) {
		dev_err(kctx->kbdev->dev, "Final physical address: 0x%016llX\n", pgd_page[index] & ~(0xFFF | ENTRY_FLAGS_MASK));
	} else {
		dev_err(kctx->kbdev->dev, "Final physical address: INVALID!\n");
	}

	kunmap(pfn_to_page(PFN_DOWN(pgd)));
}

void gpu_debug_pagetable_info(void *ctx, mali_addr64 vaddr)
{
	struct kbase_context *kctx;

	kctx = (struct kbase_context *)ctx;
	KBASE_DEBUG_ASSERT(kctx != NULL);

	dev_err(kctx->kbdev->dev, "Looking up virtual GPU address: 0x%016llX\n", vaddr);
	gpu_page_table_info_dp_level(kctx, vaddr, kctx->pgd, 0);
}

#ifdef MALI_SEC_CL_BOOST
void gpu_cl_boost_init(void *dev)
{
	struct kbase_device *kbdev;

	kbdev = (struct kbase_device *)dev;
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	atomic_set(&kbdev->pm.metrics.time_compute_jobs, 0);
	atomic_set(&kbdev->pm.metrics.time_vertex_jobs, 0);
	atomic_set(&kbdev->pm.metrics.time_fragment_jobs, 0);
}

void gpu_cl_boost_update_utilization(void *dev, void *atom, u64 microseconds_spent)
{
	struct kbase_jd_atom *katom;
	struct kbase_device *kbdev;

	kbdev = (struct kbase_device *)dev;
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	katom = (struct kbase_jd_atom *)atom;
	KBASE_DEBUG_ASSERT(katom != NULL);

	if (katom->core_req & BASE_JD_REQ_ONLY_COMPUTE)
		atomic_add((microseconds_spent >> KBASE_PM_TIME_SHIFT), &kbdev->pm.metrics.time_compute_jobs);
	else if (katom->core_req & BASE_JD_REQ_FS)
		atomic_add((microseconds_spent >> KBASE_PM_TIME_SHIFT), &kbdev->pm.metrics.time_fragment_jobs);
	else if (katom->core_req & BASE_JD_REQ_CS)
		atomic_add((microseconds_spent >> KBASE_PM_TIME_SHIFT), &kbdev->pm.metrics.time_vertex_jobs);
}
#endif

#ifdef MALI_SEC_FENCE_INTEGRATION
#define KBASE_FENCE_TIMEOUT 1000
#define DUMP_CHUNK 256

#ifdef KBASE_FENCE_DUMP
static const char *kbase_sync_status_str(int status)
{
	if (status > 0)
		return "signaled";
	else if (status == 0)
		return "active";
	else
		return "error";
}

static void kbase_sync_print_pt(struct seq_file *s, struct sync_pt *pt, bool fence)
{
	int status;

	if (pt == NULL)
		return;
	status = pt->status;

	seq_printf(s, "  %s%spt %s",
		   fence ? pt->parent->name : "",
		   fence ? "_" : "",
		   kbase_sync_status_str(status));
	if (pt->status) {
		struct timeval tv = ktime_to_timeval(pt->timestamp);
		seq_printf(s, "@%ld.%06ld", tv.tv_sec, tv.tv_usec);
	}

	if (pt->parent->ops->timeline_value_str &&
	    pt->parent->ops->pt_value_str) {
		char value[64];
		pt->parent->ops->pt_value_str(pt, value, sizeof(value));
		seq_printf(s, ": %s", value);
		if (fence) {
			pt->parent->ops->timeline_value_str(pt->parent, value,
						    sizeof(value));
			seq_printf(s, " / %s", value);
		}
	} else if (pt->parent->ops->print_pt) {
		seq_printf(s, ": ");
		pt->parent->ops->print_pt(s, pt);
	}

	seq_printf(s, "\n");
}

static void kbase_fence_print(struct seq_file *s, struct sync_fence *fence)
{
	struct list_head *pos;
	unsigned long flags;

	seq_printf(s, "[%p] %s: %s\n", fence, fence->name,
		   kbase_sync_status_str(fence->status));

	list_for_each(pos, &fence->pt_list_head) {
		struct sync_pt *pt =
			container_of(pos, struct sync_pt, pt_list);
		kbase_sync_print_pt(s, pt, true);
	}

	spin_lock_irqsave(&fence->waiter_list_lock, flags);
	list_for_each(pos, &fence->waiter_list_head) {
		struct sync_fence_waiter *waiter =
			container_of(pos, struct sync_fence_waiter,
				     waiter_list);

		if (waiter)
			seq_printf(s, "waiter %pF\n", waiter->callback);
	}
	spin_unlock_irqrestore(&fence->waiter_list_lock, flags);
}

static char kbase_sync_dump_buf[64 * 1024];
static void kbase_fence_dump(struct sync_fence *fence)
{
	int i;
	struct seq_file s = {
		.buf = kbase_sync_dump_buf,
		.size = sizeof(kbase_sync_dump_buf) - 1,
	};

	kbase_fence_print(&s, fence);
	for (i = 0; i < s.count; i += DUMP_CHUNK) {
		if ((s.count - i) > DUMP_CHUNK) {
			char c = s.buf[i + DUMP_CHUNK];
			s.buf[i + DUMP_CHUNK] = 0;
			pr_cont("%s", s.buf + i);
			s.buf[i + DUMP_CHUNK] = c;
		} else {
			s.buf[s.count] = 0;
			pr_cont("%s", s.buf + i);
		}
	}
}
#endif

/* MALI_SEC_INTEGRATION */
static void kbase_fence_timeout(unsigned long data)
{
	struct kbase_jd_atom *katom;

	katom = (struct kbase_jd_atom *)data;
	KBASE_DEBUG_ASSERT(NULL != katom);

	if (katom == NULL || katom->fence == NULL)
		return;

	if (atomic_read(&(katom->fence->status)) != 0) {
		kbase_fence_del_timer(katom);
		return;
	}
	pr_info("Release fence is not signaled on [%p] for %d ms\n", katom->fence, KBASE_FENCE_TIMEOUT);

#ifdef KBASE_FENCE_DUMP
	kbase_fence_dump(katom->fence);
#endif
#ifdef KBASE_FENCE_TIMEOUT_FAKE_SIGNAL
	{
		struct sync_pt *pt;
		struct sync_timeline *timeline;
		pt = container_of(katom->fence->cbs[0].sync_pt, struct sync_pt, base);
		if (pt == NULL)
			return;

		timeline = sync_pt_parent(pt);

		sync_timeline_signal(timeline);
	}
#endif
	return;
}

void kbase_fence_timer_init(void *atom)
{
	const u32 timeout = msecs_to_jiffies(KBASE_FENCE_TIMEOUT);
	struct kbase_jd_atom *katom;

	katom = (struct kbase_jd_atom *)atom;
	KBASE_DEBUG_ASSERT(NULL != katom);

	if (katom == NULL)
		return;

	init_timer(&katom->fence_timer);
	katom->fence_timer.function = kbase_fence_timeout;
	katom->fence_timer.data = (unsigned long)katom;
	katom->fence_timer.expires = jiffies + timeout;

	add_timer(&katom->fence_timer);
	return;
}

void kbase_fence_del_timer(void *atom)
{
	struct kbase_jd_atom *katom;

	katom = (struct kbase_jd_atom *)atom;
	KBASE_DEBUG_ASSERT(NULL != katom);

	if (katom == NULL)
		return;

	if (katom->fence_timer.function == kbase_fence_timeout)
		del_timer(&katom->fence_timer);
	katom->fence_timer.function = NULL;
	return;
}
#endif

static void dvfs_callback(struct work_struct *data)
{
#ifdef CONFIG_MALI_DVFS
	unsigned long flags;
	struct kbasep_pm_metrics_data *metrics;
	struct kbase_device *kbdev;
	struct exynos_context *platform;

	KBASE_DEBUG_ASSERT(data != NULL);

	metrics = container_of(data, struct kbasep_pm_metrics_data, work.work);

	kbdev = metrics->kbdev;
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	platform = (struct exynos_context *)kbdev->platform_context;
	KBASE_DEBUG_ASSERT(platform != NULL);

	kbase_platform_dvfs_event(metrics->kbdev, 0);

	spin_lock_irqsave(&metrics->lock, flags);

	if (metrics->timer_active)
		queue_delayed_work_on(0, platform->dvfs_wq,
				platform->delayed_work, msecs_to_jiffies(platform->polling_speed));

	spin_unlock_irqrestore(&metrics->lock, flags);
#endif
}

void gpu_pm_metrics_init(void *dev)
{
	struct kbase_device *kbdev;
	struct exynos_context *platform;

	kbdev = (struct kbase_device *)dev;
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	platform = (struct exynos_context *)kbdev->platform_context;
	KBASE_DEBUG_ASSERT(platform != NULL);

#ifdef CONFIG_MALI_DVFS
	INIT_DELAYED_WORK(&kbdev->pm.metrics.work, dvfs_callback);
	platform->dvfs_wq = create_workqueue("g3d_dvfs");
	platform->delayed_work = &kbdev->pm.metrics.work;

	queue_delayed_work_on(0, platform->dvfs_wq,
		platform->delayed_work, msecs_to_jiffies(platform->polling_speed));
#endif
}

void gpu_pm_metrics_term(void *dev)
{
	struct kbase_device *kbdev;
	struct exynos_context *platform;

	kbdev = (struct kbase_device *)dev;
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	platform = (struct exynos_context *)kbdev->platform_context;
	KBASE_DEBUG_ASSERT(platform != NULL);

#ifdef CONFIG_MALI_DVFS
	cancel_delayed_work(platform->delayed_work);
	flush_workqueue(platform->dvfs_wq);
	destroy_workqueue(platform->dvfs_wq);
#endif
}

#ifdef MALI_SEC_SEPERATED_UTILIZATION
void gpu_pm_record_state(void *dev, mali_bool is_active)
{
    unsigned long flags;
    ktime_t now;
    ktime_t diff;
	struct kbase_device *kbdev;

	kbdev = (struct kbase_device *)dev;
    KBASE_DEBUG_ASSERT(kbdev != NULL);

    mutex_lock(&kbdev->pm.lock);

    spin_lock_irqsave(&kbdev->pm.metrics.lock, flags);

    now = ktime_get();
    diff = ktime_sub(now, kbdev->pm.metrics.time_period_start);

    /* Note: We cannot use kbdev->pm.gpu_powered for debug checks that
     * we're in the right state because:
     * 1) we may be doing a delayed poweroff, in which case gpu_powered
     *    might (or might not, depending on timing) still be true soon after
     *    the call to kbase_pm_context_idle()
     * 2) hwcnt collection keeps the GPU powered
     */

    if (!kbdev->pm.metrics.gpu_active && is_active) {
        /* Going from idle to active, and not already recorded.
         * Log current time spent idle so far */

        kbdev->pm.metrics.time_idle += (u32) (ktime_to_ns(diff) >> KBASE_PM_TIME_SHIFT);
    } else if (kbdev->pm.metrics.gpu_active && !is_active) {
        /* Going from active to idle, and not already recorded.
         * Log current time spent active so far */

        kbdev->pm.metrics.time_busy += (u32) (ktime_to_ns(diff) >> KBASE_PM_TIME_SHIFT);
    }
    kbdev->pm.metrics.time_period_start = now;
    kbdev->pm.metrics.gpu_active = is_active;

    spin_unlock_irqrestore(&kbdev->pm.metrics.lock, flags);

    mutex_unlock(&kbdev->pm.lock);
}
#endif

/* caller needs to hold kbdev->pm.metrics.lock before calling this function */
int gpu_pm_get_dvfs_utilisation(struct kbase_device *kbdev, int *util_gl_share, int util_cl_share[2])
{
	int utilisation = 0;
#if !defined(MALI_SEC_CL_BOOST)
	int busy;
#else
	int compute_time = 0, vertex_time = 0, fragment_time = 0, total_time = 0, compute_time_rate = 0;
#endif

	ktime_t now = ktime_get();
	ktime_t diff;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	diff = ktime_sub(now, kbdev->pm.metrics.time_period_start);

	if (kbdev->pm.metrics.gpu_active) {
		u32 ns_time = (u32) (ktime_to_ns(diff) >> KBASE_PM_TIME_SHIFT);
		kbdev->pm.metrics.time_busy += ns_time;
		kbdev->pm.metrics.busy_cl[0] += ns_time * kbdev->pm.metrics.active_cl_ctx[0];
		kbdev->pm.metrics.busy_cl[1] += ns_time * kbdev->pm.metrics.active_cl_ctx[1];
		kbdev->pm.metrics.busy_gl += ns_time * kbdev->pm.metrics.active_gl_ctx;
		kbdev->pm.metrics.time_period_start = now;
	} else {
		kbdev->pm.metrics.time_idle += (u32) (ktime_to_ns(diff) >> KBASE_PM_TIME_SHIFT);
		kbdev->pm.metrics.time_period_start = now;
	}

	if (kbdev->pm.metrics.time_idle + kbdev->pm.metrics.time_busy == 0) {
		/* No data - so we return NOP */
		utilisation = -1;
#if !defined(MALI_SEC_CL_BOOST)
		if (util_gl_share)
			*util_gl_share = -1;
		if (util_cl_share) {
			util_cl_share[0] = -1;
			util_cl_share[1] = -1;
		}
#endif
		goto out;
	}

	utilisation = (100 * kbdev->pm.metrics.time_busy) /
			(kbdev->pm.metrics.time_idle +
			 kbdev->pm.metrics.time_busy);

#if !defined(MALI_SEC_CL_BOOST)
	busy = kbdev->pm.metrics.busy_gl +
		kbdev->pm.metrics.busy_cl[0] +
		kbdev->pm.metrics.busy_cl[1];

	if (busy != 0) {
		if (util_gl_share)
			*util_gl_share =
				(100 * kbdev->pm.metrics.busy_gl) / busy;
		if (util_cl_share) {
			util_cl_share[0] =
				(100 * kbdev->pm.metrics.busy_cl[0]) / busy;
			util_cl_share[1] =
				(100 * kbdev->pm.metrics.busy_cl[1]) / busy;
		}
	} else {
		if (util_gl_share)
			*util_gl_share = -1;
		if (util_cl_share) {
			util_cl_share[0] = -1;
			util_cl_share[1] = -1;
		}
	}
#endif

#ifdef MALI_SEC_CL_BOOST
	compute_time = atomic_read(&kbdev->pm.metrics.time_compute_jobs);
	vertex_time = atomic_read(&kbdev->pm.metrics.time_vertex_jobs);
	fragment_time = atomic_read(&kbdev->pm.metrics.time_fragment_jobs);
	total_time = compute_time + vertex_time + fragment_time;

	if (compute_time > 0 && total_time > 0)
	{
		compute_time_rate = (100 * compute_time) / total_time;
		utilisation = utilisation * (COMPUTE_JOB_WEIGHT * compute_time_rate + 100 * (100 - compute_time_rate));
		utilisation /= 10000;

		if (utilisation >= 100) utilisation = 100;
	}
#endif
 out:

	kbdev->pm.metrics.time_idle = 0;
	kbdev->pm.metrics.time_busy = 0;
#if !defined(MALI_SEC_CL_BOOST)
	kbdev->pm.metrics.busy_cl[0] = 0;
	kbdev->pm.metrics.busy_cl[1] = 0;
	kbdev->pm.metrics.busy_gl = 0;
#else
	atomic_set(&kbdev->pm.metrics.time_compute_jobs, 0);
	atomic_set(&kbdev->pm.metrics.time_vertex_jobs, 0);
	atomic_set(&kbdev->pm.metrics.time_fragment_jobs, 0);
#endif

	return utilisation;
}

static mali_bool dbg_enable = FALSE;
static void gpu_set_poweron_dbg(mali_bool enable_dbg)
{
	dbg_enable = enable_dbg;
}

static mali_bool gpu_get_poweron_dbg(void)
{
	return dbg_enable;
}

/* S.LSI INTERGRATION */
extern struct kbase_device *pkbdev;

static mali_bool gpu_mem_profile_check_kctx(void *ctx)
{
	struct kbase_context *kctx;
	struct kbase_device *kbdev;
	struct kbasep_kctx_list_element *element, *tmp;
	mali_bool found_element = MALI_FALSE;

	kctx = (struct kbase_context *)ctx;
	KBASE_DEBUG_ASSERT(kctx != NULL);

	kbdev = pkbdev;
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	mutex_lock(&kbdev->kctx_list_lock);
	list_for_each_entry_safe(element, tmp, &kbdev->kctx_list, link) {
		if (element->kctx == kctx) {
			found_element = MALI_TRUE;
			break;
		}
	}
	mutex_unlock(&kbdev->kctx_list_lock);

	return found_element;
}

struct kbase_vendor_callbacks exynos_callbacks = {
	.create_context = gpu_create_context,
	.destroy_context = gpu_destroy_context,
#ifdef MALI_SEC_CL_BOOST
	.cl_boost_init = gpu_cl_boost_init,
	.cl_boost_update_utilization = gpu_cl_boost_update_utilization,
#else
	.cl_boost_init = NULL,
	.cl_boost_update_utilization = NULL,
#endif
#ifdef MALI_SEC_FENCE_INTEGRATION
	.fence_timer_init = kbase_fence_timer_init,
	.fence_del_timer = kbase_fence_del_timer,
#else
	.fence_timer_init = NULL,
	.fence_del_timer = NULL,
#endif
#ifdef CONFIG_SOC_EXYNOS7890
	.init_hw = exynos_gpu_init_hw,
#else
	.init_hw = NULL,
#endif
#ifdef MALI_SEC_HWCNT
	.hwcnt_init = exynos_hwcnt_init,
	.hwcnt_remove = exynos_hwcnt_remove,
	.hwcnt_prepare_suspend = hwcnt_prepare_suspend,
	.hwcnt_prepare_resume = hwcnt_prepare_resume,
	.hwcnt_update = exynos_gpu_hwcnt_update,
	.hwcnt_power_up = hwcnt_power_up,
	.hwcnt_power_down = hwcnt_power_down,
#else
	.hwcnt_init = NULL,
	.hwcnt_remove = NULL,
	.hwcnt_prepare_suspend = NULL,
	.hwcnt_prepare_resume = NULL,
	.hwcnt_update = NULL,
	.hwcnt_power_up = NULL,
	.hwcnt_power_down = NULL,
#endif
	.jd_done_worker = gpu_jd_done_worker,
#ifdef CONFIG_MALI_MIDGARD_DVFS
	.pm_metrics_init = NULL,
	.pm_metrics_term = NULL,
#else
	.pm_metrics_init = gpu_pm_metrics_init,
	.pm_metrics_term = gpu_pm_metrics_term,
#endif
	.set_poweron_dbg = gpu_set_poweron_dbg,
	.get_poweron_dbg = gpu_get_poweron_dbg,
	.debug_pagetable_info = gpu_debug_pagetable_info,
	.mem_init = gpu_mem_init,
	.mem_profile_check_kctx = gpu_mem_profile_check_kctx,
#ifdef MALI_SEC_SEPERATED_UTILIZATION
	.pm_record_state = gpu_pm_record_state,
#else
	.pm_record_state = NULL,
#endif
};

