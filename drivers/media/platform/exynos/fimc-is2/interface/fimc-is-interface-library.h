/*
 * Samsung EXYNOS FIMC-IS (Imaging Subsystem) driver
 *
 * Copyright (C) 2014 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef FIMC_IS_API_COMMON_H
#define FIMC_IS_API_COMMON_H

#include <linux/vmalloc.h>

#include "fimc-is-core.h"
#include "fimc-is-debug.h"
#include "fimc-is-regs.h"
#include "fimc-is-binary.h"


#define SET_CPU_AFFINITY	/* enable @ Exynos3475 */

#if !defined(DISABLE_LIB)
#define LIB_MEM_TRACK
#endif

#define LIB_ISP_OFFSET		(0x00000040)
#define LIB_ISP_CODE_SIZE	(0x00240000)

#define LIB_VRA_OFFSET		(0x00000400)
#define LIB_VRA_CODE_SIZE	(0x00040000)

#define FIMC_IS_MAX_TASK_WORKER	(5)
#define FIMC_IS_MAX_TASK	(20)

/* depends on FIMC-IS version */
#define TASK_PRIORITY_BASE      (10)
#define TASK_PRIORITY_1ST       (TASK_PRIORITY_BASE + 1)       /* highest */
#define TASK_PRIORITY_2ND       (TASK_PRIORITY_BASE + 2)
#define TASK_PRIORITY_3RD       (TASK_PRIORITY_BASE + 3)
#define TASK_PRIORITY_4TH       (TASK_PRIORITY_BASE + 4)
#define TASK_PRIORITY_5TH       (TASK_PRIORITY_BASE + 5)       /* lowest */

/* Task index */
#define TASK_OTF       		(0)
#define TASK_AF       		(1)
#define TASK_ISP_DMA		(2)
#define TASK_3AA_DMA       	(3)
#define TASK_AA       		(4)

/* Task priority */
#define TASK_OTF_PRIORITY		(MAX_RT_PRIO - 2)
#define TASK_AF_PRIORITY		(MAX_RT_PRIO - 3)
#define TASK_ISP_DMA_PRIORITY		(MAX_RT_PRIO - 4)
#define TASK_3AA_DMA_PRIORITY		(MAX_RT_PRIO - 5)
#define TASK_AA_PRIORITY		(MAX_RT_PRIO - 6)
#define TASK_VRA_PRIORITY		(MAX_RT_PRIO - 4)

/* Task affinity */
#define TASK_OTF_AFFINITY		(3)
#define TASK_AF_AFFINITY		(1)
#define TASK_ISP_DMA_AFFINITY		(2)
#define TASK_3AA_DMA_AFFINITY		(2)
#define TASK_AA_AFFINITY		(1)
#define TASK_VRA_AFFINITY		(2)

typedef u32 (*task_func)(void *params);

typedef u32 (*start_up_func_t)(void **func);
typedef void(*os_system_func_t)(void);

struct fimc_is_task_work {
	struct kthread_work		work;
	task_func			func;
	void				*params;
};

struct fimc_is_lib_task {
	struct kthread_worker		worker;
	struct task_struct		*task;
	spinlock_t			work_lock;
	u32				work_index;
	struct fimc_is_task_work	work[FIMC_IS_MAX_TASK];
};

#ifdef LIB_MEM_TRACK
#define MT_TYPE_HEAP		1
#define MT_TYPE_SEMA		2
#define MT_TYPE_MUTEX		3
#define MT_TYPE_TIMER		4
#define MT_TYPE_SPINLOCK	5
#define MT_TYPE_RESERVED	6
#define MT_TYPE_TAAISP_DMA	7
#define MT_TYPE_VRA_DMA		8


#define MT_STATUS_ALLOC	0x1
#define MT_STATUS_FREE 	0x2

#define MEM_TRACK_COUNT	64

struct _lib_mem_track {
	ulong			lr;
	int			cpu;
	int			pid;
	unsigned long long	when;
};

struct lib_mem_track {
	int			type;
	ulong			addr;
	size_t			size;
	int			status;
	struct _lib_mem_track	alloc;
	struct _lib_mem_track	free;
};

struct lib_mem_tracks {
	struct list_head	list;
	int			num_of_track;
	struct lib_mem_track	track[MEM_TRACK_COUNT];
};
#endif

struct fimc_is_lib_dma_buffer {
	u32			size;
	ulong			kvaddr;
	dma_addr_t		dvaddr;

	struct list_head	list;
};

struct fimc_is_lib_support {
	struct fimc_is_minfo			*minfo;

	struct fimc_is_interface_ischain	*itfc;
	struct hwip_intr_handler		*intr_handler_taaisp[ID_3AAISP_MAX][INTR_HWIP_MAX];
	struct fimc_is_lib_task			task_taaisp[FIMC_IS_MAX_TASK_WORKER];

	/* memory management */
	struct list_head			lib_mem_list;
	struct list_head			taaisp_mem_list;
	struct list_head			vra_mem_list;
	bool					binary_load_flg;

	u32					reserved_lib_size;
	u32					reserved_taaisp_size;
	u32					reserved_vra_size;

	/* for log */
	ulong					kvaddr_debug;
	ulong					kvaddr_debug_cnt;
	ulong					log_ptr;
	char					log_buf[256];
	char					string[256];

	/* for library load */
	struct platform_device			*pdev;
#ifdef LIB_MEM_TRACK
	struct list_head			list_of_tracks;
	struct lib_mem_tracks			*cur_tracks;
#endif
};

struct fimc_is_memory_attribute {
	pgprot_t state;
	int numpages;
	ulong vaddr;
};

struct fimc_is_memory_change_data {
	pgprot_t set_mask;
	pgprot_t clear_mask;
};

int fimc_is_load_bin(void);
void fimc_is_load_clear(void);
void fimc_is_flush_ddk_thread(void);
void check_lib_memory_leak(void);

int fimc_is_log_write(const char *str, ...);
int fimc_is_log_write_console(char *str);
int fimc_is_lib_logdump(void);

int fimc_is_spin_lock_init(void **slock);
int fimc_is_spin_lock_finish(void *slock_lib);
int fimc_is_spin_lock(void *slock_lib);
int fimc_is_spin_unlock(void *slock_lib);
int fimc_is_spin_lock_irq(void *slock_lib);
int fimc_is_spin_unlock_irq(void *slock_lib);
ulong fimc_is_spin_lock_irqsave(void *slock_lib);
int fimc_is_spin_unlock_irqrestore(void *slock_lib, ulong flag);

void *fimc_is_alloc_reserved_buffer(u32 size);
void fimc_is_free_reserved_buffer(void *buf);
void *fimc_is_alloc_reserved_taaisp_dma_buffer(u32 size);
void fimc_is_free_reserved_taaisp_dma_buffer(void *buf);
void *fimc_is_alloc_reserved_vra_dma_buffer(u32 size);
void fimc_is_free_reserved_vra_dma_buffer(void *buf);
int fimc_is_translate_taaisp_kva_to_dva(ulong src_addr, u32 *target_addr);
int fimc_is_translate_vra_kva_to_dva(ulong src_addr, u32 *target_addr);

void fimc_is_taaisp_cache_invalid(ulong kvaddr, u32 size);
void fimc_is_taaisp_cache_flush(ulong kvaddr, u32 size);
void fimc_is_tpu_cache_invalid(ulong kvaddr, u32 size);
void fimc_is_tpu_cache_flush(ulong kvaddr, u32 size);
void fimc_is_vra_cache_invalid(ulong kvaddr, u32 size);
void fimc_is_vra_cache_flush(ulong kvaddr, u32 size);

int fimc_is_memory_attribute_nxrw(struct fimc_is_memory_attribute *attribute);
int fimc_is_memory_attribute_rox(struct fimc_is_memory_attribute *attribute);

#endif
