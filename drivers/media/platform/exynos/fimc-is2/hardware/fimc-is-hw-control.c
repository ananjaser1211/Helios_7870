/*
 * Samsung EXYNOS FIMC-IS (Imaging Subsystem) driver
 *
 * Copyright (C) 2014 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>

#include "fimc-is-err.h"
#include "fimc-is-core.h"
#include "fimc-is-hw-control.h"

#include "fimc-is-hw-3aa.h"
#include "fimc-is-hw-isp.h"
#include "fimc-is-hw-tpu.h"
#if defined(CONFIG_CAMERA_FIMC_SCALER_USE)
#include "fimc-is-hw-scp.h"
#elif defined(CONFIG_CAMERA_MC_SCALER_VER1_USE)
#include "fimc-is-hw-mcscaler-v1.h"
#elif defined(CONFIG_CAMERA_MC_SCALER_VER2_USE)
#include "fimc-is-hw-mcscaler-v2.h"
#endif
#include "fimc-is-hw-vra.h"
#include "fimc-is-hw-dm.h"

#define INTERNAL_SHOT_EXIST	(1)

void framemgr_e_barrier_common(struct fimc_is_framemgr *this, u32 index, ulong flag)
{
	if (in_interrupt()) {
		framemgr_e_barrier(this, index);
	} else {
		framemgr_e_barrier_irqs(this, index, flag);
	}

	return;
}

void framemgr_x_barrier_common(struct fimc_is_framemgr *this, u32 index, ulong flag)
{
	if (in_interrupt()) {
		framemgr_x_barrier(this, index);
	} else {
		framemgr_x_barrier_irqr(this, index, flag);
	}

	return;
}

static int get_free_work_irq(struct fimc_is_work_list *this,
	struct fimc_is_work **work)
{
	int ret = 0;

	if (work) {
		spin_lock(&this->slock_free);

		if (this->work_free_cnt) {
			*work = container_of(this->work_free_head.next,
					struct fimc_is_work, list);
			list_del(&(*work)->list);
			this->work_free_cnt--;
		} else
			*work = NULL;

		spin_unlock(&this->slock_free);
	} else {
		ret = -EFAULT;
		err_hw("item is null ptr");
	}

	return ret;
}

static int set_req_work_irq(struct fimc_is_work_list *this,
	struct fimc_is_work *work)
{
	int ret = 0;

	if (work) {
		spin_lock(&this->slock_request);
		list_add_tail(&work->list, &this->work_request_head);
		this->work_request_cnt++;
#ifdef TRACE_WORK
		print_req_work_list(this);
#endif

		spin_unlock(&this->slock_request);
	} else {
		ret = -EFAULT;
		err_hw("item is null ptr");
	}

	return ret;
}

static inline void wq_func_schedule(struct fimc_is_interface *itf,
	struct work_struct *work_wq)
{
	if (itf->workqueue)
		queue_work(itf->workqueue, work_wq);
	else
		schedule_work(work_wq);
}

static void prepare_sfr_dump(struct fimc_is_hardware *hardware)
{
	int hw_slot = -1;
	int reg_size = 0;
	struct fimc_is_hw_ip *hw_ip = NULL;

	if (!hardware) {
		err_hw("hardware is null\n");
		return;
	}

	for (hw_slot = 0; hw_slot < HW_SLOT_MAX; hw_slot++) {
		hw_ip = &hardware->hw_ip[hw_slot];

		if (hw_ip->id == DEV_HW_END || hw_ip->id == 0)
			continue;

		if (IS_ERR_OR_NULL(hw_ip->regs) ||
			(hw_ip->regs_start == 0) ||
			(hw_ip->regs_end == 0)) {
			warn_hw("[ID:%d] reg iomem is invalid", hw_ip->id);
			continue;
		}

		/* alloc sfr dump memory */
		reg_size = (hw_ip->regs_end - hw_ip->regs_start + 1);
		hw_ip->sfr_dump = (u8 *)kzalloc(reg_size, GFP_KERNEL);
		if (IS_ERR_OR_NULL(hw_ip->sfr_dump))
			err_hw("[ID:%d] sfr dump memory alloc fail", hw_ip->id);
		else
			info_hw("[ID:%d] sfr dump memory (V/P/S):(%p/%p/0x%X)[0x%llX~0x%llX]",
				hw_ip->id, hw_ip->sfr_dump, (void *)virt_to_phys(hw_ip->sfr_dump),
				reg_size, hw_ip->regs_start, hw_ip->regs_end);

		if (IS_ERR_OR_NULL(hw_ip->regs_b) ||
			(hw_ip->regs_b_start == 0) ||
			(hw_ip->regs_b_end == 0))
			continue;

		/* alloc sfr B dump memory */
		reg_size = (hw_ip->regs_b_end - hw_ip->regs_b_start + 1);
		hw_ip->sfr_b_dump = (u8 *)kzalloc(reg_size, GFP_KERNEL);
		if (IS_ERR_OR_NULL(hw_ip->sfr_b_dump))
			err_hw("[ID:%d] sfr B dump memory alloc fail", hw_ip->id);
		else
			info_hw("[ID:%d] sfr B dump memory (V/P/S):(%p/%p/0x%X)[0x%llX~0x%llX]",
				hw_ip->id, hw_ip->sfr_b_dump, (void *)virt_to_phys(hw_ip->sfr_b_dump),
				reg_size, hw_ip->regs_b_start, hw_ip->regs_b_end);
	}
}

void print_hw_frame_count(struct fimc_is_hw_ip *hw_ip)
{
	int hw_slot = -1, f_index, p_index;
	struct fimc_is_hw_ip *_hw_ip = NULL;
	struct fimc_is_hardware *hardware;
	struct hw_debug_info *debug_info;
	ulong usec[DEBUG_FRAME_COUNT][DEBUG_POINT_MAX];

	BUG_ON(!hw_ip);

	hardware = hw_ip->hardware;

	for (hw_slot = 0; hw_slot < HW_SLOT_MAX; hw_slot++) {
		_hw_ip = &hardware->hw_ip[hw_slot];
		info_hw("[ID:%d]fs(%d), cl(%d), fe(%d), dma(%d)\n", _hw_ip->id,
			atomic_read(&_hw_ip->count.fs),
			atomic_read(&_hw_ip->count.cl),
			atomic_read(&_hw_ip->count.fe),
			atomic_read(&_hw_ip->count.dma));

		for (f_index = 0; f_index < DEBUG_FRAME_COUNT; f_index++) {
			debug_info = &_hw_ip->debug_info[f_index];
			for (p_index = 0 ; p_index < DEBUG_POINT_MAX; p_index++)
				usec[f_index][p_index]  = do_div(debug_info->time[p_index], NSEC_PER_SEC);

			info_hw("[%d][F:%d]shot[%5lu.%06lu], fs[c%d][%5lu.%06lu], fe[c%d][%5lu.%06lu], dma[c%d][%5lu.%06lu], \n",
				f_index, debug_info->fcount,
				(ulong)debug_info->time[DEBUG_POINT_HW_SHOT], usec[f_index][DEBUG_POINT_HW_SHOT] / NSEC_PER_USEC,
				debug_info->cpuid[DEBUG_POINT_FRAME_START],
				(ulong)debug_info->time[DEBUG_POINT_FRAME_START], usec[f_index][DEBUG_POINT_FRAME_START] / NSEC_PER_USEC,
				debug_info->cpuid[DEBUG_POINT_FRAME_END],
				(ulong)debug_info->time[DEBUG_POINT_FRAME_END], usec[f_index][DEBUG_POINT_FRAME_END] / NSEC_PER_USEC,
				debug_info->cpuid[DEBUG_POINT_FRAME_DMA_END],
				(ulong)debug_info->time[DEBUG_POINT_FRAME_DMA_END], usec[f_index][DEBUG_POINT_FRAME_DMA_END] / NSEC_PER_USEC);
		}
	}
}

int fimc_is_hardware_probe(struct fimc_is_hardware *hardware,
	struct fimc_is_interface *itf, struct fimc_is_interface_ischain *itfc)
{
	int ret = 0;
	int i, hw_slot = -1;
	enum fimc_is_hardware_id hw_id = DEV_HW_END;

	BUG_ON(!hardware);
	BUG_ON(!itf);
	BUG_ON(!itfc);

	for (i = 0; i < HW_SLOT_MAX; i++) {
		hardware->hw_ip[i].id = DEV_HW_END;
		hardware->hw_ip[i].priv_info = NULL;

	}

#if defined(SOC_3AAISP)
	hw_id = DEV_HW_3AA0;
	hw_slot = fimc_is_hw_slot_id(hw_id);
	if (!valid_hw_slot_id(hw_slot)) {
		err_hw("invalid slot (%d,%d)", hw_id, hw_slot);
		return -EINVAL;
	}
	ret = fimc_is_hw_3aa_probe(&(hardware->hw_ip[hw_slot]), itf, itfc, hw_id);
	if (ret) {
		err_hw("probe fail (%d,%d)", hw_id, hw_slot);
		return ret;
	}
#endif

#if (defined(SOC_30S) && !defined(SOC_3AAISP))
	hw_id = DEV_HW_3AA0;
	hw_slot = fimc_is_hw_slot_id(hw_id);
	if (!valid_hw_slot_id(hw_slot)) {
		err_hw("invalid slot (%d,%d)", hw_id, hw_slot);
		return -EINVAL;
	}
	ret = fimc_is_hw_3aa_probe(&(hardware->hw_ip[hw_slot]), itf, itfc, hw_id);
	if (ret) {
		err_hw("probe fail (%d,%d)", hw_id, hw_slot);
		return ret;
	}
#endif

#if (defined(SOC_31S) && !defined(SOC_3AAISP))
	hw_id = DEV_HW_3AA1;
	hw_slot = fimc_is_hw_slot_id(hw_id);
	if (!valid_hw_slot_id(hw_slot)) {
		err_hw("invalid slot (%d,%d)", hw_id, hw_slot);
		return -EINVAL;
	}
	ret = fimc_is_hw_3aa_probe(&(hardware->hw_ip[hw_slot]), itf, itfc, hw_id);
	if (ret) {
		err_hw("probe fail (%d,%d)", hw_id, hw_slot);
		return ret;
	}
#endif

#if (defined(SOC_I0S) && !defined(SOC_3AAISP))
	hw_id = DEV_HW_ISP0;
	hw_slot = fimc_is_hw_slot_id(hw_id);
	if (!valid_hw_slot_id(hw_slot)) {
		err_hw("invalid slot (%d,%d)", hw_id, hw_slot);
		return -EINVAL;
	}
	ret = fimc_is_hw_isp_probe(&(hardware->hw_ip[hw_slot]), itf, itfc, hw_id);
	if (ret) {
		err_hw("probe fail (%d,%d)", hw_id, hw_slot);
		return ret;
	}
#endif

#if (defined(SOC_I1S) && !defined(SOC_3AAISP))
	hw_id = DEV_HW_ISP1;
	hw_slot = fimc_is_hw_slot_id(hw_id);
	if (!valid_hw_slot_id(hw_slot)) {
		err_hw("invalid slot (%d,%d)", hw_id, hw_slot);
		return -EINVAL;
	}
	ret = fimc_is_hw_isp_probe(&(hardware->hw_ip[hw_slot]), itf, itfc, hw_id);
	if (ret) {
		err_hw("probe fail (%d,%d)", hw_id, hw_slot);
		return ret;
	}
#endif

#if defined(SOC_DIS)
	hw_id = DEV_HW_TPU;
	hw_slot = fimc_is_hw_slot_id(hw_id);
	if (!valid_hw_slot_id(hw_slot)) {
		err_hw("invalid slot (%d,%d)", hw_id, hw_slot);
		return -EINVAL;
	}
	ret = fimc_is_hw_tpu_probe(&(hardware->hw_ip[hw_slot]), itf, itfc, hw_id);
	if (ret) {
		err_hw("probe fail (%d,%d)", hw_id, hw_slot);
		return ret;
	}
#endif

#if (defined(SOC_SCP) && !defined(MCS_USE_SCP_PARAM))
	hw_id = DEV_HW_SCP;
	hw_slot = fimc_is_hw_slot_id(hw_id);
	if (!valid_hw_slot_id(hw_slot)) {
		err_hw("invalid slot (%d,%d)", hw_id, hw_slot);
		return -EINVAL;
	}

	ret = fimc_is_hw_scp_probe(&(hardware->hw_ip[hw_slot]), itf, itfc, hw_id);
	if (ret) {
		err_hw("probe fail (%d,%d)", hw_id, hw_slot);
		return ret;
	}
#endif

#if (defined(SOC_SCP) && defined(MCS_USE_SCP_PARAM))
	hw_id = DEV_HW_MCSC0;
	hw_slot = fimc_is_hw_slot_id(hw_id);
	if (!valid_hw_slot_id(hw_slot)) {
		err_hw("invalid slot (%d,%d)", hw_id, hw_slot);
		return -EINVAL;
	}

	ret = fimc_is_hw_mcsc_probe(&(hardware->hw_ip[hw_slot]), itf, itfc, hw_id);
	if (ret) {
		err_hw("probe fail (%d,%d)", hw_id, hw_slot);
		return ret;
	}
#endif

#if (defined(SOC_MCS) && defined(SOC_MCS0))
	hw_id = DEV_HW_MCSC0;
	hw_slot = fimc_is_hw_slot_id(hw_id);
	if (!valid_hw_slot_id(hw_slot)) {
		err_hw("invalid slot (%d,%d)", hw_id, hw_slot);
		return -EINVAL;
	}
	ret = fimc_is_hw_mcsc_probe(&(hardware->hw_ip[hw_slot]), itf, itfc, hw_id);
	if (ret) {
		err_hw("probe fail (%d,%d)", hw_id, hw_slot);
		return ret;
	}
#endif

#if (defined(SOC_MCS) && defined(SOC_MCS1))
	hw_id = DEV_HW_MCSC1;
	hw_slot = fimc_is_hw_slot_id(hw_id);
	if (!valid_hw_slot_id(hw_slot)) {
		err_hw("invalid slot (%d,%d)", hw_id, hw_slot);
		return -EINVAL;
	}
	ret = fimc_is_hw_mcsc_probe(&(hardware->hw_ip[hw_slot]), itf, itfc, hw_id);
	if (ret) {
		err_hw("probe fail (%d,%d)", hw_id, hw_slot);
		return ret;
	}
#endif

#if defined(SOC_VRA)
	hw_id = DEV_HW_VRA;
	hw_slot = fimc_is_hw_slot_id(hw_id);
	if (!valid_hw_slot_id(hw_slot)) {
		err_hw("invalid slot (%d,%d)", hw_id, hw_slot);
		return -EINVAL;
	}
	ret = fimc_is_hw_vra_probe(&(hardware->hw_ip[hw_slot]), itf, itfc, hw_id);
	if (ret) {
		err_hw("probe fail (%d,%d)", hw_id, hw_slot);
		return ret;
	}
#endif
	hardware->base_addr_mcuctl = itfc->regs_mcuctl;

	for (i = 0; i < FIMC_IS_STREAM_COUNT; i++)
		hardware->hw_map[i] = 0;

	atomic_set(&hardware->stream_on, 0);
	atomic_set(&hardware->rsccount, 0);
	atomic_set(&hardware->bug_count, 0);
	atomic_set(&hardware->log_count, 0);

	prepare_sfr_dump(hardware);

	return ret;
}

int fimc_is_hardware_set_param(struct fimc_is_hardware *hardware, u32 instance,
	struct is_region *region, u32 lindex, u32 hindex, ulong hw_map)
{
	int ret = 0;
	int hw_slot = -1;
	struct fimc_is_hw_ip *hw_ip = NULL;

	BUG_ON(!hardware);
	BUG_ON(!region);

	for (hw_slot = 0; hw_slot < HW_SLOT_MAX; hw_slot++) {
		hw_ip = &hardware->hw_ip[hw_slot];

		CALL_HW_OPS(hw_ip, clk_gate, instance, true, false);
		ret = CALL_HW_OPS(hw_ip, set_param, region, lindex, hindex,
				instance, hw_map);
		CALL_HW_OPS(hw_ip, clk_gate, instance, false, false);
		if (ret) {
			err_hw("[%d]set_param fail (%d,%d)", instance,
				hw_ip->id, hw_slot);
			return -EINVAL;
		}
	}

	dbg_hw("[%d]set_param hw_map[0x%lx]\n", instance, hw_map);

	return ret;
}

int fimc_is_hardware_shot(struct fimc_is_hardware *hardware, u32 instance,
	struct fimc_is_group *group, struct fimc_is_frame *frame,
	struct fimc_is_framemgr *framemgr, ulong hw_map, u32 framenum)
{
	int ret = 0;
	struct fimc_is_hw_ip *hw_ip = NULL;
	enum fimc_is_hardware_id hw_id = DEV_HW_END;
	struct fimc_is_group *child = NULL;
	ulong flags = 0;
	int hw_list[GROUP_HW_MAX], hw_index, hw_slot;
	int hw_maxnum = 0;
	u32 index;

	BUG_ON(!hardware);
	BUG_ON(!frame);

	framemgr_e_barrier_common(framemgr, 0, flags);
	put_frame(framemgr, frame, FS_PROCESS);
	framemgr_x_barrier_common(framemgr, 0, flags);

	child = group;
	while (child->child)
		child = child->child;

	while (child) {
		hw_maxnum = fimc_is_get_hw_list(child->id, hw_list);
		for (hw_index = hw_maxnum - 1; hw_index >= 0; hw_index--) {
			hw_id = hw_list[hw_index];
			hw_slot = fimc_is_hw_slot_id(hw_id);
			if (!valid_hw_slot_id(hw_slot)) {
				err_hw("[%d]invalid slot (%d,%d)", instance,
					hw_id, hw_slot);
				return -EINVAL;
			}

			hw_ip = &hardware->hw_ip[hw_slot];
			/* hw_ip->fcount : frame number of current frame in Vvalid  @ OTF *
			 * hw_ip->fcount is the frame number of next FRAME END interrupt  *
			 * In OTF scenario, hw_ip->fcount is not same as frame->fcount    */
			atomic_set(&hw_ip->fcount, framenum);
			atomic_set(&hw_ip->instance, instance);

			if (hw_ip->id != DEV_HW_VRA)
				CALL_HW_OPS(hw_ip, clk_gate, instance, true, false);

			ret = CALL_HW_OPS(hw_ip, shot, frame, hw_map);
			index = frame->fcount % DEBUG_FRAME_COUNT;
			hw_ip->debug_index[0] = frame->fcount;
			hw_ip->debug_info[index].cpuid[DEBUG_POINT_HW_SHOT] = raw_smp_processor_id();
			hw_ip->debug_info[index].time[DEBUG_POINT_HW_SHOT] = cpu_clock(raw_smp_processor_id());
			if (ret) {
				err_hw("[%d]shot fail (%d,%d)[F:%d]", instance,
					hw_id, hw_slot, frame->fcount);
				return -EINVAL;
			}
		}
		child = child->parent;
	}

	if (!atomic_read(&hardware->stream_on)
		&& (hw_ip && atomic_read(&hw_ip->status.otf_start)))
		info_hw("[%d]shot [F:%d][G:0x%x][B:0x%lx][O:0x%lx][C:0x%lx][HWF:%d]\n",
			instance, frame->fcount, GROUP_ID(group->id),
			frame->bak_flag, frame->out_flag, frame->core_flag, framenum);

	return ret;
}

int fimc_is_hardware_get_meta(struct fimc_is_hw_ip *hw_ip, struct fimc_is_frame *frame,
	u32 instance, ulong hw_map, u32 output_id, enum fimc_is_frame_done_type done_type)
{
	int ret = 0;

	BUG_ON(!hw_ip);

	if ((output_id != FIMC_IS_HW_CORE_END)
		&& (done_type == FRAME_DONE_NORMAL)
		&& (test_bit(hw_ip->id, &frame->core_flag))) {
		/* FIMC-IS v3.x only
		 * There is a chance that the DMA done interrupt occurred before
		 * the core done interrupt. So we skip to call the get_meta function.
		 */
		dbg_hw("%s: skip to get_meta [ID:%d][F:%d][B:0x%lx][C:0x%lx][O:0x%lx]\n",
			__func__, hw_ip->id, frame->fcount,
			frame->bak_flag, frame->core_flag, frame->out_flag);
		return 0;
	}

	switch (hw_ip->id) {
	case DEV_HW_3AA0:
	case DEV_HW_3AA1:
	case DEV_HW_ISP0:
	case DEV_HW_ISP1:
		copy_ctrl_to_dm(frame->shot);

		ret = CALL_HW_OPS(hw_ip, get_meta, frame, hw_map);
		if (ret) {
			err_hw("[%d]get_meta fail (%d)", instance, hw_ip->id);
			return 0;
		}
		break;
	case DEV_HW_TPU:
		/* TODO */
		break;
	case DEV_HW_VRA:
		ret = CALL_HW_OPS(hw_ip, get_meta, frame, hw_map);
		if (ret) {
			err_hw("[%d]get_meta fail (%d)", instance, hw_ip->id);
			return 0;
		}
		break;
	default:
		return 0;
		break;
	}

	dbg_hw("[%d]get_meta [ID:%d][G:0x%x]\n", instance, hw_ip->id,
		GROUP_ID(hw_ip->group[instance]->id));

	return ret;
}

static int frame_fcount(struct fimc_is_frame *frame, void *data)
{
	return frame->fcount - (u32)(ulong)data;
}

int check_shot_exist(struct fimc_is_framemgr *framemgr, u32 fcount)
{
	struct fimc_is_frame *frame;

	if (framemgr->queued_count[FS_COMPLETE]) {
		frame = find_frame(framemgr, FS_COMPLETE, frame_fcount,
					(void *)(ulong)fcount);
		if (frame) {
			info_hw("[F:%d]is in complete_list\n", fcount);
			return INTERNAL_SHOT_EXIST;
		}
	}

	if (framemgr->queued_count[FS_PROCESS]) {
		frame = find_frame(framemgr, FS_PROCESS, frame_fcount,
					(void *)(ulong)fcount);
		if (frame) {
			info_hw("[F:%d]is in process_list\n", fcount);
			return INTERNAL_SHOT_EXIST;
		}
	}

	return 0;
}

int fimc_is_hardware_grp_shot(struct fimc_is_hardware *hardware, u32 instance,
	struct fimc_is_group *group, struct fimc_is_frame *frame, ulong hw_map)
{
	int ret = 0;
	int i, hw_slot = -1;
	struct fimc_is_hw_ip *hw_ip = NULL;
	enum fimc_is_hardware_id hw_id = DEV_HW_END;
	struct fimc_is_frame *hw_frame;
	struct fimc_is_framemgr *framemgr;
	ulong flags;

	BUG_ON(!hardware);
	BUG_ON(!frame);

	switch (group->id) {
	case GROUP_ID_3AA0:
		hw_id = DEV_HW_3AA0;
		break;
	case GROUP_ID_3AA1:
		hw_id = DEV_HW_3AA1;
		break;
	case GROUP_ID_ISP0:
		hw_id = DEV_HW_ISP0;
		break;
	case GROUP_ID_ISP1:
		hw_id = DEV_HW_ISP1;
		break;
	case GROUP_ID_DIS0:
		hw_id = DEV_HW_TPU;
		break;
	case GROUP_ID_MCS0:
		hw_id = DEV_HW_MCSC0;
		break;
	case GROUP_ID_MCS1:
		hw_id = DEV_HW_MCSC1;
		break;
	case GROUP_ID_VRA0:
		hw_id = DEV_HW_VRA;
		break;
	default:
		hw_id = DEV_HW_END;
		err_hw("[%d]invalid group (%d)", instance, group->id);
		return -EINVAL;
		break;
	}

	hw_slot = fimc_is_hw_slot_id(hw_id);
	if (!valid_hw_slot_id(hw_slot)) {
		err_hw("[%d]invalid slot (%d,%d)", instance, hw_id, hw_slot);
		return -EINVAL;
	}

	hw_ip = &hardware->hw_ip[hw_slot];
	if (hw_ip == NULL) {
		err_hw("[%d][G:0x%d]hw_ip(null) (%d,%d)", instance,
			GROUP_ID(group->id), hw_id, hw_slot);
		return -EINVAL;
	}

	if (!atomic_read(&hardware->stream_on))
		info_hw("[%d]grp_shot [F:%d][G:0x%x][B:0x%lx][O:0x%lx][IN:0x%x]\n",
			instance, frame->fcount, GROUP_ID(group->id),
			frame->bak_flag, frame->out_flag, frame->dvaddr_buffer[0]);

	framemgr = hw_ip->framemgr;
	framemgr_e_barrier_irqs(framemgr, 0, flags);
	ret = check_shot_exist(framemgr, frame->fcount);
	framemgr_x_barrier_irqr(framemgr, 0, flags);

	/* check late shot */
	if (hw_ip->internal_fcount >= frame->fcount || ret == INTERNAL_SHOT_EXIST) {
		info_hw("[%d]LATE_SHOT (%d)[F:%d][G:0x%x][B:0x%lx][O:0x%lx][C:0x%lx]\n",
			instance, hw_ip->internal_fcount, frame->fcount, GROUP_ID(group->id),
			frame->bak_flag, frame->out_flag, frame->core_flag);
		frame->type = SHOT_TYPE_LATE;
		framemgr = hw_ip->framemgr_late;
		if (framemgr->queued_count[FS_REQUEST] > 0) {
			warn_hw("[%d]LATE_SHOT REQ(%d) > 0, PRO(%d)",
				instance,
				framemgr->queued_count[FS_REQUEST],
				framemgr->queued_count[FS_PROCESS]);
		}

		if (frame->lindex || frame->hindex)
			set_bit(FIMC_IS_SUBDEV_FORCE_SET, &group->leader.state);

		ret = 0;
	} else {
		frame->type = SHOT_TYPE_EXTERNAL;
		framemgr = hw_ip->framemgr;
	}

	framemgr_e_barrier_irqs(framemgr, 0, flags);
	hw_frame = get_frame(framemgr, FS_FREE);
	framemgr_x_barrier_irqr(framemgr, 0, flags);
	if (hw_frame == NULL) {
		err_hw("[%d][G:0x%x]free_head(NULL)", instance, GROUP_ID(group->id));
		return -EINVAL;
	}

	hw_frame->groupmgr	= frame->groupmgr;
	hw_frame->group		= frame->group;
	hw_frame->shot		= frame->shot;
	hw_frame->shot_ext	= frame->shot_ext;
	hw_frame->kvaddr_shot	= frame->kvaddr_shot;
	hw_frame->dvaddr_shot	= frame->dvaddr_shot;
	hw_frame->shot_size	= frame->shot_size;
	hw_frame->fcount	= frame->fcount;
	hw_frame->rcount	= frame->rcount;
	hw_frame->bak_flag	= frame->bak_flag;
	hw_frame->out_flag	= frame->out_flag;
	hw_frame->core_flag	= 0;
	atomic_set(&hw_frame->shot_done_flag, 1);

	for (i = 0; i < FIMC_IS_MAX_PLANES; i++)
		hw_frame->dvaddr_buffer[i] = frame->dvaddr_buffer[i];

	hw_frame->instance = instance;

	if (frame->type == SHOT_TYPE_LATE)
		hw_frame->type = SHOT_TYPE_LATE;
	else
		hw_frame->type = SHOT_TYPE_EXTERNAL;

	if (test_bit(FIMC_IS_GROUP_OTF_INPUT, &hw_ip->group[instance]->state)) {
		if (!atomic_read(&hw_ip->status.otf_start)) {
			atomic_set(&hw_ip->status.otf_start, 1);
			info_hw("[%d]OTF start [F:%d][G:0x%x][B:0x%lx][O:0x%lx]\n",
				instance, frame->fcount, GROUP_ID(group->id),
				frame->bak_flag, frame->out_flag);

			for (hw_slot = 0; hw_slot < HW_SLOT_MAX; hw_slot++) {
				hw_ip = &hardware->hw_ip[hw_slot];
				if (test_bit(hw_ip->id, &hw_map)) {
					dbg_hw("[%d][ID:%d]count clear\n", instance, hw_ip->id);
					atomic_set(&hw_ip->count.fs, (frame->fcount - 1));
					atomic_set(&hw_ip->count.cl, (frame->fcount - 1));
					atomic_set(&hw_ip->count.fe, (frame->fcount - 1));
					atomic_set(&hw_ip->count.dma, (frame->fcount - 1));
				}
			}
		} else {
			atomic_set(&hw_ip->hardware->log_count, 0);
			framemgr_e_barrier_irqs(framemgr, 0, flags);
			put_frame(framemgr, hw_frame, FS_REQUEST);
			framemgr_x_barrier_irqr(framemgr, 0, flags);

			return ret;
		}
	}

	if (frame->type == SHOT_TYPE_LATE) {
		err_hw("[%d]grp_shot: LATE_SHOT", instance);
		return -EINVAL;
	}

	ret = fimc_is_hardware_shot(hardware, instance, group, hw_frame, framemgr,
			hw_map, frame->fcount);
	if (ret) {
		err_hw("hardware_shot fail [G:0x%x](%d)", GROUP_ID(group->id),
			hw_ip->id);
		return -EINVAL;
	}

	return ret;
}

int make_internal_shot(struct fimc_is_hw_ip *hw_ip, u32 instance, u32 framenum,
	struct fimc_is_frame **in_frame, struct fimc_is_framemgr *framemgr)
{
	int ret = 0;
	int i = 0;
	struct fimc_is_frame *frame;

	BUG_ON(!hw_ip);
	BUG_ON(!framemgr);

	if (framemgr->queued_count[FS_FREE] < 3) {
		warn_hw("[%d][ID:%d] Free frame is less than 3", instance, hw_ip->id);
	}

	ret = check_shot_exist(framemgr, framenum);
	if (ret == INTERNAL_SHOT_EXIST)
		return ret;

	frame = get_frame(framemgr, FS_FREE);
	if (frame == NULL) {
		err_hw("[%d]config_lock: frame(null)", instance);
		frame_manager_print_info_queues(framemgr);
		return -EINVAL;
	}
	frame->groupmgr		= NULL;
	frame->group		= NULL;
	frame->shot		= NULL;
	frame->shot_ext		= NULL;
	frame->kvaddr_shot	= 0;
	frame->dvaddr_shot	= 0;
	frame->shot_size	= 0;
	frame->fcount		= framenum;
	frame->rcount		= 0;
	frame->bak_flag		= 0;
	frame->out_flag		= 0;
	frame->core_flag	= 0;
	atomic_set(&frame->shot_done_flag, 1);

	for (i = 0; i < FIMC_IS_MAX_PLANES; i++)
		frame->dvaddr_buffer[i]	= 0;

	frame->type = SHOT_TYPE_INTERNAL;
	frame->instance = instance;
	*in_frame = frame;

	return ret;
}

int fimc_is_hardware_config_lock(struct fimc_is_hw_ip *hw_ip, u32 instance, u32 framenum)
{
	int ret = 0;
	struct fimc_is_frame *frame;
	struct fimc_is_framemgr *framemgr;
	struct fimc_is_hardware *hardware;
	struct fimc_is_device_sensor *sensor;
	u32 fcount = framenum + 1;
	u32 log_count;

	BUG_ON(!hw_ip);

	hardware = hw_ip->hardware;

	if (!test_bit(FIMC_IS_GROUP_OTF_INPUT, &hw_ip->group[instance]->state))
		return ret;

	dbg_hw("C.L [F:%d]\n", framenum);

	sensor = hw_ip->group[instance]->device->sensor;

	framemgr = hw_ip->framemgr;
	framemgr_e_barrier(framemgr, 0);
	if (framemgr->queued_count[FS_REQUEST]) {
		frame = get_frame(framemgr, FS_REQUEST);
	} else {
		ret = make_internal_shot(hw_ip, instance, fcount, &frame, framemgr);
		if (ret == INTERNAL_SHOT_EXIST) {
			framemgr_x_barrier(framemgr, 0);
			return ret;
		}
		if (ret) {
			framemgr_x_barrier(framemgr, 0);
			print_hw_frame_count(hw_ip);
			set_bit(FIMC_IS_3AA_STOP, &sensor->force_stop);
			info_hw("[F:%d]int1_mask(0x%08X)\n", fcount, readl(hw_ip->regs + 0x3608));
			memcpy(hw_ip->regs_dump, hw_ip->regs, 0xa4b8);
			info("dumped addr(phys): %p", (void *)virt_to_phys(hw_ip->regs_dump));
			return ret;
		}
		log_count = atomic_read(&hardware->log_count);
		if ((log_count <= 20) || ((log_count > 20) && !(log_count % 100)))
			info_hw("config_lock: INTERNAL_SHOT [F:%d](%d) count(%d)\n",
				fcount, frame->index, log_count);
	}
	frame->frame_info[INFO_CONFIG_LOCK].cpu = raw_smp_processor_id();
	frame->frame_info[INFO_CONFIG_LOCK].pid = current->pid;
	frame->frame_info[INFO_CONFIG_LOCK].when = cpu_clock(raw_smp_processor_id());

	framemgr_x_barrier(framemgr, 0);

	ret = fimc_is_hardware_shot(hardware, instance, hw_ip->group[instance],
			frame, framemgr, hardware->hw_map[instance], framenum);
	if (ret) {
		err_hw("hardware_shot fail [G:0x%x](%d)",
			GROUP_ID(hw_ip->group[instance]->id), hw_ip->id);
		return -EINVAL;
	}

	return ret;
}

void check_late_shot(struct fimc_is_hw_ip *hw_ip)
{
	int ret = 0;
	struct fimc_is_frame *frame;
	struct fimc_is_framemgr *framemgr;

	/* check LATE_FRAME */
	framemgr = hw_ip->framemgr_late;
	framemgr_e_barrier(framemgr, 0);
	if (!framemgr->queued_count[FS_REQUEST]) {
		framemgr_x_barrier(framemgr, 0);
		return;
	}
	frame = get_frame(framemgr, FS_REQUEST);
	framemgr_x_barrier(framemgr, 0);

	if (frame == NULL)
		return;

	framemgr_e_barrier(framemgr, 0);
	put_frame(framemgr, frame, FS_COMPLETE);
	framemgr_x_barrier(framemgr, 0);

	ret = fimc_is_hardware_frame_ndone(hw_ip, frame, frame->instance, true);
	if (ret)
		err_hw("[%d]F_NDONE fail (%d)", frame->instance, hw_ip->id);

	return;
}

void fimc_is_hardware_size_dump(struct fimc_is_hw_ip *hw_ip)
{
	int hw_slot = -1;
	struct fimc_is_hardware *hardware;

	BUG_ON(!hw_ip);
	BUG_ON(!hw_ip->hardware);

	hardware = hw_ip->hardware;

	for (hw_slot = 0; hw_slot < HW_SLOT_MAX; hw_slot++) {
		hw_ip = &hardware->hw_ip[hw_slot];
		if (hw_ip->ops->size_dump)
			hw_ip->ops->size_dump(hw_ip);;
	}

	return;
}

static void fimc_is_hardware_clk_gate_dump(struct fimc_is_hw_ip *hw_ip)
{
	if (hw_ip && hw_ip->clk_gate)
		info_hw("[ID:%d] CLOCK_ENABLE(0x%08X)\n", hw_ip->id, __raw_readl(hw_ip->clk_gate->regs));

	return;
}

void fimc_is_hardware_dump(struct fimc_is_hardware *hardware)
{
	int hw_slot = -1;
	struct fimc_is_hw_ip *hw_ip;

	for (hw_slot = 0; hw_slot < HW_SLOT_MAX; hw_slot++) {
		hw_ip = &hardware->hw_ip[hw_slot];
		fimc_is_hardware_clk_gate_dump(hw_ip);
	}

	return;
}

void fimc_is_hardware_frame_start(struct fimc_is_hw_ip *hw_ip, u32 instance)
{
	struct fimc_is_frame *frame;
	struct fimc_is_framemgr *framemgr;
	struct fimc_is_group *parent;

	BUG_ON(!hw_ip);

	parent = hw_ip->group[instance];
	while (parent->parent)
		parent = parent->parent;

	framemgr = hw_ip->framemgr;
	framemgr_e_barrier(framemgr, 0);
	if (framemgr->queued_count[FS_PROCESS]) {
		frame = get_frame(framemgr, FS_PROCESS);
	} else {
		if ((hw_ip->group[instance]->id == parent->id)
			&& (test_bit(FIMC_IS_GROUP_OTF_INPUT, &hw_ip->group[instance]->state))) {
			frame_manager_print_info_queues(framemgr);
			print_hw_frame_count(hw_ip);
			framemgr_x_barrier(framemgr, 0);
			err_hw("FSTART frame null [ID:%d](%d)", hw_ip->id, hw_ip->internal_fcount);
			return;
		} else {
			goto check;
		}
	}

	if (atomic_read(&hw_ip->status.otf_start)
		&& frame->fcount != atomic_read(&hw_ip->count.fs)) {
		/* error handling */
		info_hw("frame_start_isr (%d, %d)\n", frame->fcount,
			atomic_read(&hw_ip->count.fs));
	}
	/* TODO: multi-instance */
	frame->frame_info[INFO_FRAME_START].cpu = raw_smp_processor_id();
	frame->frame_info[INFO_FRAME_START].pid = current->pid;
	frame->frame_info[INFO_FRAME_START].when = cpu_clock(raw_smp_processor_id());
	put_frame(framemgr, frame, FS_COMPLETE);
check:
	clear_bit(HW_CONFIG, &hw_ip->state);
	atomic_set(&hw_ip->status.Vvalid, V_VALID);
	framemgr_x_barrier(framemgr, 0);

	if (test_bit(FIMC_IS_GROUP_OTF_INPUT, &hw_ip->group[instance]->state))
		check_late_shot(hw_ip);

	return;
}

int fimc_is_hardware_sensor_start(struct fimc_is_hardware *hardware, u32 instance,
	ulong hw_map)
{
	int ret = 0;
	int hw_slot = -1;
	struct fimc_is_hw_ip *hw_ip;
	enum fimc_is_hardware_id hw_id = DEV_HW_END;

	BUG_ON(!hardware);

	if (test_bit(DEV_HW_3AA0, &hw_map)) {
		hw_id = DEV_HW_3AA0;
	} else if (test_bit(DEV_HW_3AA1, &hw_map)) {
		hw_id = DEV_HW_3AA1;
	} else {
		warn_hw("[%d]invalid state hw_map[0x%lx]\n", instance, hw_map);
		return 0;
	}

	hw_slot = fimc_is_hw_slot_id(hw_id);
	if (!valid_hw_slot_id(hw_slot)) {
		err_hw("[%d]invalid slot (%d,%d)", instance, hw_id, hw_slot);
		return -EINVAL;
	}

	hw_ip = &hardware->hw_ip[hw_slot];
	if (hw_ip == NULL) {
		err_hw("[%d]hw_ip(null) (%d,%d)", instance, hw_id, hw_slot);
		return -EINVAL;
	}

	ret = fimc_is_hw_3aa_mode_change(hw_ip, instance, hw_map);
	if (ret) {
		err_hw("[%d]mode_change fail (%d,%d)", instance, hw_ip->id, hw_slot);
		return -EINVAL;
	}

	atomic_set(&hardware->stream_on, 1);
	atomic_set(&hardware->bug_count, 0);
	atomic_set(&hardware->log_count, 0);

	info_hw("[%d]hw_sensor_start [P:0x%lx]\n", instance, hw_map);

	return ret;
}

int fimc_is_hardware_sensor_stop(struct fimc_is_hardware *hardware, u32 instance,
	ulong hw_map)
{
	int ret = 0;
	int hw_slot = -1;
	int retry;
	struct fimc_is_frame *frame;
	struct fimc_is_framemgr *framemgr;
	struct fimc_is_group *group;
	struct fimc_is_hw_ip *hw_ip = NULL;
	enum fimc_is_hardware_id hw_id = DEV_HW_END;

	BUG_ON(!hardware);

	atomic_set(&hardware->stream_on, 0);
	atomic_set(&hardware->bug_count, 0);
	atomic_set(&hardware->log_count, 0);

	if (test_bit(DEV_HW_3AA0, &hw_map)) {
		hw_id = DEV_HW_3AA0;
	} else if (test_bit(DEV_HW_3AA1, &hw_map)) {
		hw_id = DEV_HW_3AA1;
	} else {
		warn_hw("[%d]invalid state hw_map[0x%lx]\n", instance, hw_map);
		return 0;
	}

	hw_slot = fimc_is_hw_slot_id(hw_id);
	if (!valid_hw_slot_id(hw_slot)) {
		err_hw("[%d]invalid slot (%d,%d)", instance,
			hw_id, hw_slot);
		return -EINVAL;
	}

	hw_ip = &hardware->hw_ip[hw_slot];
	group = hw_ip->group[instance];
	framemgr = hw_ip->framemgr;
	if (test_bit(FIMC_IS_GROUP_OTF_INPUT, &group->state)) {
		retry = 99;
		while (--retry && framemgr->queued_count[FS_COMPLETE]) {
			frame = peek_frame(framemgr, FS_COMPLETE);
			if (frame == NULL)
				break;

			info_hw("hw_sensor_stop: com_list: [F:%d][%d][O:0x%lx][C:0x%lx][(%d)",
				frame->fcount, frame->type, frame->out_flag, frame->core_flag,
				framemgr->queued_count[FS_COMPLETE]);
			warn_hw(" %d com waiting...", framemgr->queued_count[FS_COMPLETE]);
			usleep_range(1000, 1000);
		}
		print_hw_frame_count(hw_ip);
	}

	info_hw("[%d]hw_sensor_stop: done[P:0x%lx]\n", instance, hw_map);

	return ret;
}

int fimc_is_hardware_process_start(struct fimc_is_hardware *hardware, u32 instance,
	u32 group_id)
{
	int ret = 0;
	int hw_slot = -1;
	ulong hw_map;
	int hw_list[GROUP_HW_MAX];
	int hw_index, hw_maxnum;
	enum fimc_is_hardware_id hw_id = DEV_HW_END;
	struct fimc_is_hw_ip *hw_ip = NULL;

	BUG_ON(!hardware);

	dbg_hw("[%d]process_start [G:0x%x]\n", instance, GROUP_ID(group_id));

	hw_map = hardware->hw_map[instance];
	hw_maxnum = fimc_is_get_hw_list(group_id, hw_list);
	for (hw_index = 0; hw_index < hw_maxnum; hw_index++) {
		hw_id = hw_list[hw_index];
		hw_slot = fimc_is_hw_slot_id(hw_id);
		if (!valid_hw_slot_id(hw_slot)) {
			err_hw("[%d]invalid slot (%d,%d)", instance,
				hw_id, hw_slot);
			return -EINVAL;
		}

		hw_ip = &hardware->hw_ip[hw_slot];

		CALL_HW_OPS(hw_ip, clk_gate, instance, true, false);
		ret = CALL_HW_OPS(hw_ip, enable, instance, hw_map);
		CALL_HW_OPS(hw_ip, clk_gate, instance, false, false);
		if (ret) {
			err_hw("[%d]enable fail (%d,%d)", instance, hw_ip->id, hw_slot);
			return -EINVAL;
		}
		hw_ip->internal_fcount = 0;
	}

	return ret;
}

void fimc_is_hardware_force_stop(struct fimc_is_hardware *hardware,
	struct fimc_is_hw_ip *hw_ip, u32 instance)
{
	int ret = 0;
	int retry, list_index;
	struct fimc_is_frame *frame;
	struct fimc_is_framemgr *framemgr;
	struct fimc_is_framemgr *framemgr_late;

	BUG_ON(!hw_ip);

	framemgr = hw_ip->framemgr;
	framemgr_late = hw_ip->hardware->framemgr_late;

	pr_info("[@][HW][%d]complete_list (%d)(%d)(%d)\n", instance,
		framemgr->queued_count[FS_COMPLETE],
		framemgr->queued_count[FS_PROCESS],
		framemgr->queued_count[FS_REQUEST]);

	retry = 150;
	while (--retry && framemgr->queued_count[FS_COMPLETE]) {
		frame = peek_frame(framemgr, FS_COMPLETE);
		if (frame == NULL)
			break;

		info_hw("complete_list [F:%d][%d][O:0x%lx][C:0x%lx][(%d)",
			frame->fcount, frame->type, frame->out_flag, frame->core_flag,
			framemgr->queued_count[FS_COMPLETE]);
		ret = fimc_is_hardware_frame_ndone(hw_ip, frame, instance, false);
		if (ret) {
			err_hw("[%d]hardware_frame_ndone fail (%d)", instance, hw_ip->id);
			return;
		}
		warn_hw(" %d com waiting...", framemgr->queued_count[FS_COMPLETE]);
		msleep(1);
	}

	info_hw("[%d]process_list (%d)\n", instance, framemgr->queued_count[FS_PROCESS]);
	retry = 150;
	while (--retry && framemgr->queued_count[FS_PROCESS]) {
		frame = peek_frame(framemgr, FS_PROCESS);
		if (frame == NULL)
			break;

		info_hw("process_list [F:%d][%d][O:0x%lx][C:0x%lx][(%d)",
			frame->fcount, frame->type, frame->out_flag, frame->core_flag,
			framemgr->queued_count[FS_PROCESS]);

		set_bit(hw_ip->id, &frame->core_flag);

		ret = fimc_is_hardware_frame_ndone(hw_ip, frame, instance, false);
		if (ret) {
			err_hw("[%d]hardware_frame_ndone fail (%d)", instance, hw_ip->id);
			return;
		}
		warn_hw(" %d pro waiting...", framemgr->queued_count[FS_PROCESS]);
		msleep(1);
	}

	info_hw("[%d]request_list (%d)\n", instance, framemgr->queued_count[FS_REQUEST]);
	retry = 150;
	while (--retry && framemgr->queued_count[FS_REQUEST]) {
		frame = peek_frame(framemgr, FS_REQUEST);
		if (frame == NULL)
			break;

		info_hw("request_list [F:%d](%d)", frame->fcount,
			framemgr->queued_count[FS_REQUEST]);

		set_bit(hw_ip->id, &frame->core_flag);

		ret = fimc_is_hardware_frame_ndone(hw_ip, frame, instance, false);
		if (ret) {
			err_hw("[%d]hardware_frame_ndone fail (%d)", instance, hw_ip->id);
			return;
		}
		warn_hw(" %d req waiting...", framemgr->queued_count[FS_REQUEST]);
		msleep(1);
	}

	pr_info("[@][HW][%d]late_list (%d)(%d)(%d)\n",
		instance, framemgr_late->queued_count[FS_COMPLETE],
		framemgr_late->queued_count[FS_PROCESS],
		framemgr_late->queued_count[FS_REQUEST]);
	for (list_index = FS_REQUEST; list_index < FS_INVALID; list_index++) {
		info_hw("[%d]late_list[%d] (%d)\n",
			instance, list_index, framemgr_late->queued_count[list_index]);
		retry = 150;
		while (--retry && framemgr_late->queued_count[list_index]) {
			frame = peek_frame(framemgr_late, list_index);
			if (frame == NULL)
				break;

			info_hw("late_list[%d] [F:%d][%d][O:0x%lx][C:0x%lx][(%d)",
				list_index, frame->fcount, frame->type,
				frame->out_flag, frame->core_flag,
				framemgr_late->queued_count[list_index]);

			set_bit(hw_ip->id, &frame->core_flag);

			ret = fimc_is_hardware_frame_ndone(hw_ip, frame, instance, true);
			if (ret) {
				err_hw("[%d]hardware_frame_ndone fail (%d)", instance, hw_ip->id);
				return;
			}
			warn_hw(" %d late waiting...", framemgr_late->queued_count[list_index]);
			msleep(1);
		}
	}

	return;
}

void fimc_is_hardware_process_stop(struct fimc_is_hardware *hardware, u32 instance,
	u32 group_id, u32 mode)
{
	int ret;
	int hw_slot = -1;
	int hw_list[GROUP_HW_MAX];
	int hw_index, hw_maxnum;
	ulong hw_map;
	struct fimc_is_hw_ip *hw_ip;
	enum fimc_is_hardware_id hw_id = DEV_HW_END;

	BUG_ON(!hardware);

	dbg_hw("[%d]process_stop [G:0x%x](%d)\n", instance, GROUP_ID(group_id), mode);

	if (mode == 0)
		return;

	hw_map = hardware->hw_map[instance];
	hw_maxnum = fimc_is_get_hw_list(group_id, hw_list);
	for (hw_index = 0; hw_index < hw_maxnum; hw_index++) {
		hw_id = hw_list[hw_index];
		hw_slot = fimc_is_hw_slot_id(hw_id);
		if (!valid_hw_slot_id(hw_slot)) {
			err_hw("[%d]invalid slot (%d,%d)", instance,
				hw_id, hw_slot);
			return;
		}

		hw_ip = &hardware->hw_ip[hw_slot];

		CALL_HW_OPS(hw_ip, clk_gate, instance, true, false);
		ret = CALL_HW_OPS(hw_ip, disable, instance, hw_map);
		CALL_HW_OPS(hw_ip, clk_gate, instance, false, false);
		if (ret) {
			err_hw("[%d]disable fail (%d,%d)", instance, hw_ip->id, hw_slot);
		}
	}

	switch (group_id) {
	case GROUP_ID_3AA0:
		hw_id = DEV_HW_3AA0;
		break;
	case GROUP_ID_3AA1:
		hw_id = DEV_HW_3AA1;
		break;
	case GROUP_ID_ISP0:
		hw_id = DEV_HW_ISP0;
		break;
	case GROUP_ID_ISP1:
		hw_id = DEV_HW_ISP1;
		break;
	case GROUP_ID_DIS0:
		hw_id = DEV_HW_TPU;
		break;
	case GROUP_ID_MCS0:
		hw_id = DEV_HW_MCSC0;
		break;
	case GROUP_ID_MCS1:
		hw_id = DEV_HW_MCSC1;
		break;
	case GROUP_ID_VRA0:
		hw_id = DEV_HW_VRA;
		break;
	default:
		hw_id = DEV_HW_END;
		err_hw("[%d]invalid group (%d)", instance, group_id);
		return;
		break;
	}

	hw_slot = fimc_is_hw_slot_id(hw_id);
	if (!valid_hw_slot_id(hw_slot)) {
		if (test_bit(hw_id, &hw_map))
			err_hw("[%d]invalid slot (%d,%d)", instance, hw_id, hw_slot);
		return;
	}

	hw_ip = &hardware->hw_ip[hw_slot];
	if (hw_ip == NULL) {
		err_hw("[%d][G:0x%d]hw_ip(null) (%d,%d)", instance,
			group_id, hw_id, hw_slot);
		return;
	}

	CALL_HW_OPS(hw_ip, clk_gate, instance, true, false);
	fimc_is_hardware_force_stop(hardware, hw_ip, instance);
	CALL_HW_OPS(hw_ip, clk_gate, instance, false, false);
	atomic_set(&hw_ip->status.otf_start, 0);

	return;
}

int fimc_is_hardware_open(struct fimc_is_hardware *hardware, u32 hw_id,
	struct fimc_is_group *group, u32 instance, bool rep_flag, u32 module_id)
{
	int ret = 0;
	int hw_slot = -1;
	struct fimc_is_hw_ip *hw_ip = NULL;
	struct fimc_is_group *parent;
	u32 size = 0;
	int i, j;

	BUG_ON(!hardware);

	parent = group;
	while (parent->parent)
		parent = parent->parent;

	hw_slot = fimc_is_hw_slot_id(hw_id);
	if (!valid_hw_slot_id(hw_slot))
		return 0;

	/* HACK : VRA open skip at reprocessing instance */
	if (rep_flag && hw_id == DEV_HW_VRA)
		return 0;

	hw_ip = &(hardware->hw_ip[hw_slot]);
	hw_ip->hardware = hardware;
	hw_ip->framemgr = &hardware->framemgr[parent->id];
	hw_ip->framemgr_late = &hardware->framemgr_late[parent->id];

	/* HACK */
	hw_ip->group[instance] = group;

	CALL_HW_OPS(hw_ip, clk_gate, instance, true, false);
	ret = CALL_HW_OPS(hw_ip, open, instance, &size);
	CALL_HW_OPS(hw_ip, clk_gate, instance, false, false);
	if (ret) {
		err_hw("[%d]open fail (%d)", instance, hw_ip->id);
		return ret;
	}
	if (size) {
		hw_ip->priv_info = kzalloc(size, GFP_KERNEL);
		if(!hw_ip->priv_info) {
			err_hw("hw_ip->priv_info(null) (%d)", hw_ip->id);
			return -EINVAL;
		}
	}

	CALL_HW_OPS(hw_ip, clk_gate, instance, true, false);
	ret = CALL_HW_OPS(hw_ip, init, group, rep_flag, module_id);
	CALL_HW_OPS(hw_ip, clk_gate, instance, false, false);
	if (ret) {
		err_hw("[%d]init fail (%d)", instance, hw_ip->id);
		return ret;
	}

	for (i = 0; i < DEBUG_FRAME_COUNT; i++) {
		hw_ip->debug_info[i].fcount = 0;
		for (j = 0; j < DEBUG_POINT_MAX; j++) {
			hw_ip->debug_info[i].cpuid[j] = 0;
			hw_ip->debug_info[i].time[j] = 0;
		}
	}
	set_bit(HW_OPEN, &hw_ip->state);
	set_bit(HW_INIT, &hw_ip->state);
	atomic_inc(&hw_ip->rsccount);

	if (!rep_flag) {
		hw_ip->debug_index[0] = 0;
		hw_ip->debug_index[1] = 0;
		atomic_set(&hw_ip->count.fs, 0);
		atomic_set(&hw_ip->count.cl, 0);
		atomic_set(&hw_ip->count.fe, 0);
		atomic_set(&hw_ip->count.dma, 0);
		atomic_set(&hw_ip->status.Vvalid, V_BLANK);
	}
	set_bit(hw_id, &hardware->hw_map[instance]);

	info_hw("[%d]open (%d)(%d)\n", instance, hw_ip->id, atomic_read(&hw_ip->rsccount));

	return ret;
}

int fimc_is_hardware_close(struct fimc_is_hardware *hardware,u32 hw_id, u32 instance)
{
	int ret = 0;
	int hw_slot = -1;
	struct fimc_is_hw_ip *hw_ip = NULL;
	int i, j;

	BUG_ON(!hardware);

	hw_slot = fimc_is_hw_slot_id(hw_id);
	if (!valid_hw_slot_id(hw_slot))
		return 0;

	if (!test_bit(hw_id, &hardware->hw_map[instance]))
		return 0;

	hw_ip = &(hardware->hw_ip[hw_slot]);

	switch (hw_id) {
	case DEV_HW_3AA0:
	case DEV_HW_3AA1:
		fimc_is_hw_3aa_object_close(hw_ip, instance);
		break;
	case DEV_HW_ISP0:
	case DEV_HW_ISP1:
		fimc_is_hw_isp_object_close(hw_ip, instance);
		break;
	case DEV_HW_TPU:
		/* TODO */
		break;
	case DEV_HW_VRA:
		break;
		/* TODO */
	default:
		break;
	}

	if (!atomic_dec_and_test(&hw_ip->rsccount)) {
		info_hw("[%d][ID:%d] rsccount(%d)\n", instance, hw_ip->id,
			atomic_read(&hw_ip->rsccount));
		clear_bit(hw_id, &hardware->hw_map[instance]);
		return 0;
	}

	CALL_HW_OPS(hw_ip, clk_gate, instance, true, true);
	ret = CALL_HW_OPS(hw_ip, close, instance);
	if (ret) {
		err_hw("[%d]close fail (%d)", instance, hw_ip->id);
		return 0;
	}

	kfree(hw_ip->priv_info);
	clear_bit(hw_id, &hardware->hw_map[instance]);

	for (i = 0; i < DEBUG_FRAME_COUNT; i++) {
		hw_ip->debug_info[i].fcount = 0;
		for (j = 0; j < DEBUG_POINT_MAX; j++) {
			hw_ip->debug_info[i].cpuid[j] = 0;
			hw_ip->debug_info[i].time[j] = 0;
		}
	}
	hw_ip->debug_index[0] = 0;
	hw_ip->debug_index[1] = 0;
	clear_bit(HW_OPEN, &hw_ip->state);
	clear_bit(HW_INIT, &hw_ip->state);
	clear_bit(HW_CONFIG, &hw_ip->state);
	clear_bit(HW_RUN, &hw_ip->state);
	clear_bit(HW_TUNESET, &hw_ip->state);
	atomic_set(&hw_ip->status.otf_start, 0);
	atomic_set(&hw_ip->fcount, 0);
	atomic_set(&hw_ip->instance, 0);
	hw_ip->internal_fcount = 0;

	return ret;
}

int do_frame_done_work_func(struct fimc_is_interface *itf, int wq_id, u32 instance,
	u32 group_id, u32 fcount, u32 rcount, u32 status)
{
	int ret = 0;
	bool retry_flag = false;
	struct work_struct *work_wq;
	struct fimc_is_work_list *work_list;
	struct fimc_is_work *work;

	work_wq   = &itf->work_wq[wq_id];
	work_list = &itf->work_list[wq_id];
retry:
	get_free_work_irq(work_list, &work);
	if (work) {
		work->msg.id		= 0;
		work->msg.command	= IHC_FRAME_DONE;
		work->msg.instance	= instance;
		work->msg.group		= GROUP_ID(group_id);
		work->msg.param1	= fcount;
		work->msg.param2	= rcount;
		work->msg.param3	= status; /* status: 0:FRAME_DONE, 1:FRAME_NDONE */
		work->msg.param4	= 0;

		work->fcount = work->msg.param1;
		set_req_work_irq(work_list, work);

		if (!work_pending(work_wq))
			wq_func_schedule(itf, work_wq);
	} else {
		err_hw("free work item is empty (%d)", (int)retry_flag);
		if (retry_flag == false) {
			retry_flag = true;
			goto retry;
		}
		ret = -EINVAL;
	}

	return ret;
}

int check_core_end(struct fimc_is_hw_ip *hw_ip, u32 hw_fcount,
	struct fimc_is_frame **in_frame, struct fimc_is_framemgr *framemgr,
	u32 output_id, enum fimc_is_frame_done_type done_type)
{
	int ret = 0;
	struct fimc_is_frame *frame = *in_frame;

	BUG_ON(!hw_ip);
	BUG_ON(!frame);
	BUG_ON(!framemgr);

	if (frame->fcount != hw_fcount) {
		if ((hw_ip->is_leader) && (hw_fcount - frame->fcount >= 2)) {
			dbg_hw("[%d]LATE  CORE END [ID:%d][F:%d][0x%x][C:0x%lx]" \
				"[O:0x%lx][END:%d]\n",
				frame->instance, hw_ip->id, frame->fcount,
				output_id, frame->core_flag, frame->out_flag,
				hw_fcount);

			info_hw("%d: force_done for LATE FRAME [ID:%d][F:%d]\n",
				__LINE__, hw_ip->id, frame->fcount);
			ret = fimc_is_hardware_frame_ndone(hw_ip, frame,
					frame->instance, false);
			if (ret) {
				err_hw("[%d]hardware_frame_ndone fail (%d)",
					frame->instance, hw_ip->id);
				return -EINVAL;
			}
		}

		framemgr_e_barrier(framemgr, 0);
		*in_frame = find_frame(framemgr, FS_COMPLETE, frame_fcount,
					(void *)(ulong)hw_fcount);
		framemgr_x_barrier(framemgr, 0);
		frame = *in_frame;

		if (frame == NULL) {
			err_hw("[ID:%d][F:%d]frame(null)!!(%d)", hw_ip->id,
				hw_fcount, done_type);
			framemgr_e_barrier(framemgr, 0);
			frame_manager_print_info_queues(framemgr);
			print_hw_frame_count(hw_ip);
			framemgr_x_barrier(framemgr, 0);
			return -EINVAL;
		}

		if (!test_bit_variables(hw_ip->id, &frame->core_flag)) {
			info_hw("[%d]invalid core_flag [ID:%d][F:%d][0x%x][C:0x%lx]" \
				"[O:0x%lx]",
				frame->instance, hw_ip->id, frame->fcount,
				output_id, frame->core_flag, frame->out_flag);
#if 0 // temporarily blocked for front recording stop kernel panic
			return -EINVAL;
#endif
		}
	} else {
		dbg_hw("[ID:%d][%d,F:%d]FRAME COUNT invalid",
			hw_ip->id, frame->fcount, hw_fcount);
	}

	return ret;
}

int check_frame_end(struct fimc_is_hw_ip *hw_ip, u32 hw_fcount,
	struct fimc_is_frame **in_frame, struct fimc_is_framemgr *framemgr,
	u32 output_id, enum fimc_is_frame_done_type done_type)
{
	int ret = 0;
	struct fimc_is_frame *frame = *in_frame;

	BUG_ON(!hw_ip);
	BUG_ON(!frame);
	BUG_ON(!framemgr);

	if (frame->fcount != hw_fcount) {
		dbg_hw("[%d]LATE FRAME END [ID:%d][F:%d][0x%x][C:0x%lx][O:0x%lx]" \
			"[END:%d]\n",
			frame->instance, hw_ip->id, frame->fcount, output_id,
			frame->core_flag, frame->out_flag, hw_fcount);

		framemgr_e_barrier(framemgr, 0);
		*in_frame = find_frame(framemgr, FS_COMPLETE, frame_fcount,
					(void *)(ulong)hw_fcount);
		framemgr_x_barrier(framemgr, 0);
		frame = *in_frame;
		if (frame == NULL) {
			err_hw("[ID:%d][F:%d]frame(null)!!(%d)", hw_ip->id,
				hw_fcount, done_type);
			framemgr_e_barrier(framemgr, 0);
			frame_manager_print_info_queues(framemgr);
			print_hw_frame_count(hw_ip);
			framemgr_x_barrier(framemgr, 0);
			return -EINVAL;
		}

		if (!test_bit_variables(output_id, &frame->out_flag)) {
			info_hw("[%d]invalid output_id [ID:%d][F:%d][0x%x][C:0x%lx]" \
				"[O:0x%lx]",
				frame->instance, hw_ip->id, frame->fcount,
				output_id, frame->core_flag, frame->out_flag);
			return -EINVAL;
		}
	}

	return ret;
}

int fimc_is_hardware_frame_done(struct fimc_is_hw_ip *hw_ip, struct fimc_is_frame *frame,
	int wq_id, u32 output_id, u32 status, enum fimc_is_frame_done_type done_type)
{
	int ret = 0;
	struct fimc_is_framemgr *framemgr;
	struct fimc_is_group *parent;

	BUG_ON(!hw_ip);

	switch (done_type) {
	case FRAME_DONE_NORMAL:
		framemgr = hw_ip->framemgr;
		if (frame == NULL) {
			framemgr_e_barrier(framemgr, 0);
			frame = peek_frame(framemgr, FS_COMPLETE);
			framemgr_x_barrier(framemgr, 0);
		} else {
			warn_hw("[ID:%d]frame NOT null!!(%d)", hw_ip->id, done_type);
		}
		break;
	case FRAME_DONE_FORCE:
		framemgr = hw_ip->framemgr;
		break;
	case FRAME_DONE_LATE_SHOT:
		framemgr = hw_ip->framemgr_late;
		if (frame == NULL) {
			warn_hw("[ID:%d]frame null!!(%d)", hw_ip->id, done_type);
			framemgr_e_barrier(framemgr, 0);
			frame = peek_frame(framemgr, FS_COMPLETE);
			framemgr_x_barrier(framemgr, 0);
		}

		if (frame!= NULL && frame->type != SHOT_TYPE_LATE) {
			warn_hw("invalid frame type");
			frame->type = SHOT_TYPE_LATE;
		}
		break;
	default:
		framemgr = hw_ip->framemgr;
		err_hw("[ID:%d]invalid done_type(%d)", hw_ip->id, done_type);
		return -EINVAL;
		break;
	}

	if (frame == NULL) {
		err_hw("[ID:%d][F:%d]frame_done: frame(null)!!(%d)(0x%x)",
			hw_ip->id, atomic_read(&hw_ip->fcount), done_type, output_id);
		framemgr_e_barrier(framemgr, 0);
		frame_manager_print_info_queues(framemgr);
		print_hw_frame_count(hw_ip);
		framemgr_x_barrier(framemgr, 0);
		return -EINVAL;
	}

	parent = hw_ip->group[frame->instance];
	while (parent->parent)
		parent = parent->parent;

	dbg_hw("[%d][ID:%d][0x%x]frame_done [F:%d][G:0x%x][B:0x%lx][C:0x%lx][O:0x%lx]\n",
		frame->instance, hw_ip->id, output_id, frame->fcount,
		GROUP_ID(parent->id), frame->bak_flag, frame->core_flag, frame->out_flag);

	/* check core_done */
	if (output_id == FIMC_IS_HW_CORE_END) {
		switch (done_type) {
		case FRAME_DONE_NORMAL:
			if (!test_bit_variables(hw_ip->id, &frame->core_flag)) {
				ret = check_core_end(hw_ip, atomic_read(&hw_ip->fcount), &frame,
					framemgr, output_id, done_type);
			}
			break;
		case FRAME_DONE_FORCE:
			goto shot_done;
			break;
		case FRAME_DONE_LATE_SHOT:
			goto shot_done;
			break;
		default:
			break;
		}

		if (ret)
			return ret;

		if (hw_ip->is_leader) {
			frame->frame_info[INFO_FRAME_END_PROC].cpu = raw_smp_processor_id();
			frame->frame_info[INFO_FRAME_END_PROC].pid = current->pid;
			frame->frame_info[INFO_FRAME_END_PROC].when = cpu_clock(raw_smp_processor_id());
		}

	} else {
		if (frame->type == SHOT_TYPE_INTERNAL)
			goto shot_done;

		switch(done_type) {
		case FRAME_DONE_NORMAL:
			if (!test_bit_variables(output_id, &frame->out_flag)) {
				ret = check_frame_end(hw_ip, atomic_read(&hw_ip->fcount), &frame,
					framemgr, output_id, done_type);
				if (ret)
					return ret;
			}
			break;
		case FRAME_DONE_FORCE:
			if (!test_bit(output_id, &frame->out_flag))
				goto shot_done;
			break;
		case FRAME_DONE_LATE_SHOT:
			break;
		default:
			break;
		}

		ret = do_frame_done_work_func(hw_ip->itf,
				wq_id,
				frame->instance,
				parent->id,
				frame->fcount,
				frame->rcount,
				status);
		if (ret)
			BUG_ON(1);

		clear_bit(output_id, &frame->out_flag);
	}

	if (frame->shot)
	    fimc_is_hardware_get_meta(hw_ip, frame,
			frame->instance, hw_ip->hardware->hw_map[frame->instance],
			output_id, done_type);

shot_done:
	if (output_id == FIMC_IS_HW_CORE_END)
		clear_bit(hw_ip->id, &frame->core_flag);

	if (done_type == FRAME_DONE_FORCE)
		info_hw("FORCE_DONE [ID:%d][0x%x][F:%d][C:0x%lx][O:0x%lx]\n",
			hw_ip->id, output_id, frame->fcount, frame->core_flag,
			frame->out_flag);

	framemgr_e_barrier(framemgr, 0);
	if (!OUT_FLAG(frame->out_flag, parent->leader.id)
		&& !frame->core_flag
		&& atomic_dec_and_test(&frame->shot_done_flag)) {
		framemgr_x_barrier(framemgr, 0);
		ret = fimc_is_hardware_shot_done(hw_ip, frame, framemgr, done_type);
		return ret;
	}
	framemgr_x_barrier(framemgr, 0);

	return ret;
}

int fimc_is_hardware_shot_done(struct fimc_is_hw_ip *hw_ip, struct fimc_is_frame *frame,
	struct fimc_is_framemgr *framemgr, enum fimc_is_frame_done_type done_type)
{
	int ret = 0;
	struct work_struct *work_wq;
	struct fimc_is_work_list *work_list;
	struct fimc_is_work *work;
	struct fimc_is_group *parent;
	u32  req_id;

	u32 cmd = ISR_DONE;
	u32 err_type = IS_SHOT_SUCCESS;

	BUG_ON(!hw_ip);

	if (frame == NULL) {
		err_hw("[ID:%d]frame(null)!!", hw_ip->id);
		framemgr_e_barrier(framemgr, 0);
		frame_manager_print_info_queues(framemgr);
		print_hw_frame_count(hw_ip);
		framemgr_x_barrier(framemgr, 0);
		BUG_ON(!frame);
	}

	parent = hw_ip->group[frame->instance];
	while (parent->parent)
		parent = parent->parent;

	dbg_hw("[%d][ID:%d]shot_done [F:%d][G:0x%x][B:0x%lx][C:0x%lx][O:0x%lx]\n",
		frame->instance, hw_ip->id, frame->fcount, GROUP_ID(parent->id),
		frame->bak_flag, frame->core_flag, frame->out_flag);

	if (frame->type == SHOT_TYPE_INTERNAL)
		goto free_frame;

	switch (parent->id) {
	case GROUP_ID_3AA0:
	case GROUP_ID_3AA1:
	case GROUP_ID_ISP0:
	case GROUP_ID_ISP1:
	case GROUP_ID_DIS0:
	case GROUP_ID_MCS0:
	case GROUP_ID_MCS1:
	case GROUP_ID_VRA0:
		req_id = parent->leader.id;
		break;
	default:
		err_hw("invalid group (%d)", parent->id);
		goto exit;
		break;
	}

	if (!test_bit_variables(req_id, &frame->out_flag)) {
		err_hw("[%d]invalid bak_flag [ID:%d][F:%d][0x%x][B:0x%lx][O:0x%lx]",
			frame->instance, hw_ip->id, frame->fcount, req_id,
			frame->bak_flag, frame->out_flag);
		goto free_frame;
	}

	if (done_type == FRAME_DONE_LATE_SHOT || done_type == FRAME_DONE_FORCE) {
		cmd      = ISR_NDONE;
		err_type = IS_SHOT_UNKNOWN;
	} else {
		cmd      = ISR_DONE;
		err_type = IS_SHOT_SUCCESS;
	}

	work_wq   = &hw_ip->itf->work_wq[INTR_SHOT_DONE];
	work_list = &hw_ip->itf->work_list[INTR_SHOT_DONE];

	get_free_work_irq(work_list, &work);
	if (work) {
		work->msg.id		= 0;
		work->msg.command	= IHC_FRAME_DONE;
		work->msg.instance	= frame->instance;
		work->msg.group		= GROUP_ID(parent->id);
		work->msg.param1	= frame->fcount;
		work->msg.param2	= err_type; /* status */
		work->msg.param3	= 0;
		work->msg.param4	= 0;

		work->fcount = work->msg.param1;
		set_req_work_irq(work_list, work);

		if (!work_pending(work_wq))
			wq_func_schedule(hw_ip->itf, work_wq);
	} else {
		err_hw("free work item is empty\n");
	}
	clear_bit(req_id, &frame->out_flag);

free_frame:
	if (done_type == FRAME_DONE_LATE_SHOT) {
		info_hw("[%d]LATE_SHOT_DONE [ID:%d][F:%d][G:0x%x]\n",
			frame->instance, hw_ip->id, frame->fcount, GROUP_ID(parent->id));
		goto exit;
	}

	if (done_type == FRAME_DONE_FORCE) {
		info_hw("[%d]SHOT_DONE_FORCE [ID:%d][F:%d][G:0x%x]\n",
			frame->instance, hw_ip->id, frame->fcount, GROUP_ID(parent->id));
		goto exit;
	}

	if (cmd == ISR_NDONE) {
		warn_hw("[%d]shot_NDONE [ID:%d][F:%d][G:0x%x]\n",
			frame->instance, hw_ip->id, frame->fcount, GROUP_ID(parent->id));
		goto exit;
	}

	if (frame->type == SHOT_TYPE_INTERNAL) {
		dbg_hw("[%d]INTERNAL_SHOT_DONE [ID:%d][F:%d][G:0x%x]\n",
			frame->instance, hw_ip->id, frame->fcount, GROUP_ID(parent->id));
		atomic_inc(&hw_ip->hardware->log_count);
	} else {
		dbg_hw("[%d]shot_ DONE [ID:%d][F:%d][G:0x%x]\n",
			frame->instance, hw_ip->id, frame->fcount, GROUP_ID(parent->id));
		atomic_set(&hw_ip->hardware->log_count, 0);
	}
exit:
	framemgr_e_barrier(framemgr, 0);
	trans_frame(framemgr, frame, FS_FREE);
	framemgr_x_barrier(framemgr, 0);
	atomic_set(&frame->shot_done_flag, 0);
	if (framemgr->queued_count[FS_FREE] > 10)
		atomic_set(&hw_ip->hardware->bug_count, 0);

	return ret;
}

int fimc_is_hardware_frame_ndone(struct fimc_is_hw_ip *ldr_hw_ip,
	struct fimc_is_frame *frame, u32 instance, bool late_flag)
{
	int ret = 0;
	int hw_slot = -1;
	struct fimc_is_hw_ip *hw_ip = NULL;
	struct fimc_is_group *group = NULL;
	struct fimc_is_hardware *hardware;
	enum fimc_is_hardware_id hw_id = DEV_HW_END;
	int hw_list[GROUP_HW_MAX], hw_index;
	struct fimc_is_group *parent;
	int hw_maxnum = 0;

	group    = ldr_hw_ip->group[instance];
	hardware = ldr_hw_ip->hardware;

	if (late_flag == true && frame != NULL) {
		parent = group;
		while (parent->parent)
			parent = parent->parent;

		info_hw("frame_ndone [F:%d][O:0x%lx][C:0x%lx]\n", frame->fcount,
				frame->out_flag, frame->core_flag);

		/* if there is not any out_flag without leader, forcely set the core flag */
		if (!OUT_FLAG(frame->out_flag, parent->leader.id))
			set_bit(ldr_hw_ip->id, &frame->core_flag);
	}

	while (group) {
		hw_maxnum = fimc_is_get_hw_list(group->id, hw_list);
		for (hw_index = 0; hw_index < hw_maxnum; hw_index++) {
			hw_id = hw_list[hw_index];
			hw_slot = fimc_is_hw_slot_id(hw_id);
			if (!valid_hw_slot_id(hw_slot)) {
				err_hw("[%d]invalid slot (%d,%d)", instance, hw_id, hw_slot);
				return -EINVAL;
			}

			hw_ip = &(hardware->hw_ip[hw_slot]);
			ret = CALL_HW_OPS(hw_ip, frame_ndone, frame, instance, late_flag);
			if (ret) {
				err_hw("[%d]frame_ndone fail (%d,%d)", instance,
					hw_id, hw_slot);
				return -EINVAL;
			}
		}
		group = group->child;
	}

	return ret;
}

int _set_setfile_number(struct fimc_is_hardware *hardware, u32 hw_id, u32 num)
{
	int ret = 0;
	int hw_slot = -1;

	BUG_ON(!hardware);

	switch (hw_id) {
	case DEV_HW_3AA0:
		hw_slot = fimc_is_hw_slot_id(hw_id);
		if (valid_hw_slot_id(hw_slot))
			hardware->hw_ip[hw_slot].setfile_info.num = num;

		hw_slot = fimc_is_hw_slot_id(DEV_HW_3AA1);
		if (valid_hw_slot_id(hw_slot))
			hardware->hw_ip[hw_slot].setfile_info.num = num;

		hw_slot = fimc_is_hw_slot_id(DEV_HW_ISP0);
		if (valid_hw_slot_id(hw_slot))
			hardware->hw_ip[hw_slot].setfile_info.num = num;

		hw_slot = fimc_is_hw_slot_id(DEV_HW_ISP1);
		if (valid_hw_slot_id(hw_slot))
			hardware->hw_ip[hw_slot].setfile_info.num = num;
		break;
	case DEV_HW_DRC:
	case DEV_HW_DIS:
	case DEV_HW_3DNR:
	case DEV_HW_SCP:
	case DEV_HW_FD:
	case DEV_HW_VRA:
		hw_slot = fimc_is_hw_slot_id(hw_id);
		if (valid_hw_slot_id(hw_slot))
			hardware->hw_ip[hw_slot].setfile_info.num = num;
		break;
	case DEV_HW_MCSC0:
		hw_slot = fimc_is_hw_slot_id(hw_id);
		if (valid_hw_slot_id(hw_slot))
			hardware->hw_ip[hw_slot].setfile_info.num = num;

		hw_slot = fimc_is_hw_slot_id(DEV_HW_MCSC1);
		if (valid_hw_slot_id(hw_slot))
			hardware->hw_ip[hw_slot].setfile_info.num = num;
		break;
	default:
		err_hw("invalid hw (%d)", hw_id);
		return -EINVAL;
		break;
	}

	if (valid_hw_slot_id(hw_slot)) {
		dbg_hw("[ID:%d] setfile number(%d)\n", hw_id,
			hardware->hw_ip[hw_slot].setfile_info.num);
	}

	return ret;
}

int _set_setfile_table(struct fimc_is_hardware *hardware, u32 hw_id, ulong addr,
	u32 size, int index)
{
	int ret = 0;
	int hw_slot = -1;

	BUG_ON(!hardware);

	switch (hw_id) {
	case DEV_HW_3AA0:
		hw_slot = fimc_is_hw_slot_id(hw_id);
		if (valid_hw_slot_id(hw_slot)) {
			hardware->hw_ip[hw_slot].setfile_info.table[index].addr = addr;
			hardware->hw_ip[hw_slot].setfile_info.table[index].size = size;
		}

		hw_slot = fimc_is_hw_slot_id(DEV_HW_3AA1);
		if (valid_hw_slot_id(hw_slot)) {
			hardware->hw_ip[hw_slot].setfile_info.table[index].addr = addr;
			hardware->hw_ip[hw_slot].setfile_info.table[index].size = size;
		}

		hw_slot = fimc_is_hw_slot_id(DEV_HW_ISP0);
		if (valid_hw_slot_id(hw_slot)) {
			hardware->hw_ip[hw_slot].setfile_info.table[index].addr = addr;
			hardware->hw_ip[hw_slot].setfile_info.table[index].size = size;
		}

		hw_slot = fimc_is_hw_slot_id(DEV_HW_ISP1);
		if (valid_hw_slot_id(hw_slot)) {
			hardware->hw_ip[hw_slot].setfile_info.table[index].addr = addr;
			hardware->hw_ip[hw_slot].setfile_info.table[index].size = size;
		}
		break;
	case DEV_HW_DRC:
	case DEV_HW_DIS:
	case DEV_HW_3DNR:
	case DEV_HW_SCP:
	case DEV_HW_FD:
	case DEV_HW_VRA:
		hw_slot = fimc_is_hw_slot_id(hw_id);
		if (valid_hw_slot_id(hw_slot)) {
			hardware->hw_ip[hw_slot].setfile_info.table[index].addr = addr;
			hardware->hw_ip[hw_slot].setfile_info.table[index].size = size;
		}
		break;
	case DEV_HW_MCSC0:
		hw_slot = fimc_is_hw_slot_id(hw_id);
		if (valid_hw_slot_id(hw_slot)) {
			hardware->hw_ip[hw_slot].setfile_info.table[index].addr = addr;
			hardware->hw_ip[hw_slot].setfile_info.table[index].size = size;
		}

		hw_slot = fimc_is_hw_slot_id(DEV_HW_MCSC1);
		if (valid_hw_slot_id(hw_slot)) {
			hardware->hw_ip[hw_slot].setfile_info.table[index].addr = addr;
			hardware->hw_ip[hw_slot].setfile_info.table[index].size = size;
		}
		break;
	default:
		err_hw("invalid hw (%d)",hw_id);
		return -EINVAL;
		break;
	}

	if (valid_hw_slot_id(hw_slot)) {
		dbg_hw("[ID:%d][index:%d] setfile[addr:0x%lx][size:%x]\n", hw_id, index,
			hardware->hw_ip[hw_slot].setfile_info.table[index].addr,
			hardware->hw_ip[hw_slot].setfile_info.table[index].size);
	}

	return ret;
}

int _set_senario_setfile_index(struct fimc_is_hardware *hardware, u32 hw_id,
	u32 scenario, u32 index)
{
	int ret = 0;
	int hw_slot = -1;

	BUG_ON(!hardware);

	switch (hw_id) {
	case DEV_HW_3AA0:
		hw_slot = fimc_is_hw_slot_id(hw_id);
		if (valid_hw_slot_id(hw_slot))
			hardware->hw_ip[hw_slot].setfile_info.index[scenario] = index;

		hw_slot = fimc_is_hw_slot_id(DEV_HW_3AA1);
		if (valid_hw_slot_id(hw_slot))
			hardware->hw_ip[hw_slot].setfile_info.index[scenario] = index;

		hw_slot = fimc_is_hw_slot_id(DEV_HW_ISP0);
		if (valid_hw_slot_id(hw_slot))
			hardware->hw_ip[hw_slot].setfile_info.index[scenario] = index;

		hw_slot = fimc_is_hw_slot_id(DEV_HW_ISP1);
		if (valid_hw_slot_id(hw_slot))
			hardware->hw_ip[hw_slot].setfile_info.index[scenario] = index;
		break;
	case DEV_HW_DRC:
	case DEV_HW_DIS:
	case DEV_HW_3DNR:
	case DEV_HW_SCP:
	case DEV_HW_FD:
	case DEV_HW_VRA:
		hw_slot = fimc_is_hw_slot_id(hw_id);
		if (valid_hw_slot_id(hw_slot))
			hardware->hw_ip[hw_slot].setfile_info.index[scenario] = index;
		break;
	case DEV_HW_MCSC0:
		hw_slot = fimc_is_hw_slot_id(hw_id);
		if (valid_hw_slot_id(hw_slot))
			hardware->hw_ip[hw_slot].setfile_info.index[scenario] = index;

		hw_slot = fimc_is_hw_slot_id(DEV_HW_MCSC1);
		if (valid_hw_slot_id(hw_slot))
			hardware->hw_ip[hw_slot].setfile_info.index[scenario] = index;
		break;
	default:
		err_hw("invalid hw (%d)", hw_id);
		ret = -EINVAL;
		break;
	}

	return ret;
}

int _get_hw_id_by_settfile(u32 id, u32 num, u32 *designed_bit)
{
	int ret = -1;
	u32 d_bit = *designed_bit;

	dbg_hw("%s: designed_bit(0x%x)\n", __func__, d_bit);

	if (d_bit & SETFILE_DESIGN_BIT_3AA_ISP) {
		ret = DEV_HW_3AA0;
		d_bit &= ~SETFILE_DESIGN_BIT_3AA_ISP;
		goto exit;
	}

	if (d_bit & SETFILE_DESIGN_BIT_DRC) {
		ret = DEV_HW_DRC;
		d_bit &= ~SETFILE_DESIGN_BIT_DRC;
		goto exit;
	}

	if (d_bit & SETFILE_DESIGN_BIT_SCC) {
		/* TODO */
		d_bit &= ~SETFILE_DESIGN_BIT_SCC;
		goto exit;
	}

	if (d_bit & SETFILE_DESIGN_BIT_ODC) {
		/* TODO */
		d_bit &= ~SETFILE_DESIGN_BIT_ODC;
		goto exit;
	}

	if (d_bit & SETFILE_DESIGN_BIT_VDIS) {
		ret = DEV_HW_DIS;
		d_bit &= ~SETFILE_DESIGN_BIT_VDIS;
		goto exit;
	}

	if (d_bit & SETFILE_DESIGN_BIT_TDNR) {
		ret = DEV_HW_3DNR;
		d_bit &= ~SETFILE_DESIGN_BIT_TDNR;
		goto exit;
	}

	if (d_bit & SETFILE_DESIGN_BIT_SCX_MCSC) {
		if (fimc_is_has_mcsc())
			ret = DEV_HW_MCSC0;
		else
			ret = DEV_HW_SCP;
		d_bit &= ~SETFILE_DESIGN_BIT_SCX_MCSC;
		goto exit;
	}

	if (d_bit & SETFILE_DESIGN_BIT_FD_VRA) {
		ret = DEV_HW_VRA;
		d_bit &= ~SETFILE_DESIGN_BIT_FD_VRA;
		goto exit;
	}

exit:
	dbg_hw("%s: designed_bit(0x%x)\n", __func__, d_bit);
	*designed_bit = d_bit;

	return ret;
}

void _set_setfile_version(struct fimc_is_hardware *hardware, int version)
{
	int i, hw_slot = -1;

	BUG_ON(!hardware);

	for (i = DEV_HW_3AA0; i < DEV_HW_END; i++) {
		hw_slot = fimc_is_hw_slot_id(i);
		if (valid_hw_slot_id(hw_slot))
			hardware->hw_ip[hw_slot].setfile_info.version = version;
	}
}

int _load_setfile(struct fimc_is_hardware *hardware, u32 hw_id, int index,
	u32 instance, ulong hw_map)
{
	int ret = 0;
	int hw_slot = -1;
	struct fimc_is_hw_ip *hw_ip = NULL;

	BUG_ON(!hardware);

	switch (hw_id) {
	case DEV_HW_3AA0:
		hw_slot = fimc_is_hw_slot_id(DEV_HW_3AA0);
		if (valid_hw_slot_id(hw_slot)) {
			hw_ip = &hardware->hw_ip[hw_slot];
			ret = CALL_HW_OPS(hw_ip, load_setfile, index, instance, hw_map);
		}

		hw_slot = fimc_is_hw_slot_id(DEV_HW_ISP0);
		if (valid_hw_slot_id(hw_slot)) {
			hw_ip = &hardware->hw_ip[hw_slot];
			ret = CALL_HW_OPS(hw_ip, load_setfile, index, instance, hw_map);
		}

		hw_slot = fimc_is_hw_slot_id(DEV_HW_3AA1);
		if (valid_hw_slot_id(hw_slot)) {
			hw_ip = &hardware->hw_ip[hw_slot];
			ret = CALL_HW_OPS(hw_ip, load_setfile, index, instance, hw_map);
		}

		hw_slot = fimc_is_hw_slot_id(DEV_HW_ISP1);
		if (valid_hw_slot_id(hw_slot)) {
			hw_ip = &hardware->hw_ip[hw_slot];
			ret = CALL_HW_OPS(hw_ip, load_setfile, index, instance, hw_map);
		}
		break;
	case DEV_HW_DRC:
	case DEV_HW_DIS:
	case DEV_HW_3DNR:
	case DEV_HW_SCP:
	case DEV_HW_FD:
	case DEV_HW_VRA:
		hw_slot = fimc_is_hw_slot_id(hw_id);
		if (valid_hw_slot_id(hw_slot)) {
			hw_ip = &hardware->hw_ip[hw_slot];
			ret = CALL_HW_OPS(hw_ip, load_setfile, index, instance, hw_map);
		}
		break;
	case DEV_HW_MCSC0:
		hw_slot = fimc_is_hw_slot_id(hw_id);
		if (valid_hw_slot_id(hw_slot)) {
			hw_ip = &hardware->hw_ip[hw_slot];
			ret = CALL_HW_OPS(hw_ip, load_setfile, index, instance, hw_map);
		}

		hw_slot = fimc_is_hw_slot_id(DEV_HW_MCSC1);
		if (valid_hw_slot_id(hw_slot)) {
			hw_ip = &hardware->hw_ip[hw_slot];
			ret = CALL_HW_OPS(hw_ip, load_setfile, index, instance, hw_map);
		}
		break;
	default:
		break;
	}

	return ret;
}

int fimc_is_hardware_load_setfile(struct fimc_is_hardware *hardware, ulong addr,
	u32 instance, ulong hw_map)
{
	struct fimc_is_setfile_header_v3 *header_v3 = NULL;
	struct fimc_is_setfile_header_v2 *header_v2 = NULL;
	struct fimc_is_setfile_element_info *setfile_index_table = NULL;
	ulong scenario_table = 0;
	ulong setfile_table = 0;
	ulong setfile_base = 0;
	u32 setfile_num, setfile_index;
	u32 scenario_num, subip_num, setfile_offset;
	int ver = 0, ret = 0;
	int hw_id = 0, i, j;
	u32 designed_bit;

	BUG_ON(!hardware);
	BUG_ON(!addr);

	info_hw("[%d]load_setfile: [hw path0x%lx], [addr:0x%lx]\n", instance,
		hw_map, addr);
	/* 1. decision setfile version */
	/* 2. load header information */
	header_v3 = (struct fimc_is_setfile_header_v3 *)addr;
	if (header_v3->magic_number < SET_FILE_MAGIC_NUMBER) {
		header_v2 = (struct fimc_is_setfile_header_v2 *)addr;
		if (header_v2->magic_number != (SET_FILE_MAGIC_NUMBER - 1)) {
			err_hw("invalid magic number[0x%08x]", header_v2->magic_number);
			return -EINVAL;
		}
		scenario_num = header_v2->scenario_num;
		subip_num = header_v2->subip_num;
		setfile_offset = header_v2->setfile_offset;
		scenario_table = addr + sizeof(struct fimc_is_setfile_header_v2);
		ver = SETFILE_V2;
	} else {
		scenario_num = header_v3->scenario_num;
		subip_num = header_v3->subip_num;
		setfile_offset = header_v3->setfile_offset;
		scenario_table = addr + sizeof(struct fimc_is_setfile_header_v3);

		ver = SETFILE_V3;
		dbg_hw("%s: designed bit: 0x%08x\n", __func__, header_v3->designed_bit);
		dbg_hw("%s: version code: %s\n", __func__, header_v3->version_code);
		dbg_hw("%s: revision code: %s\n", __func__, header_v3->revision_code);
	}
	setfile_base = addr + setfile_offset;
	setfile_table = scenario_table + subip_num * scenario_num * sizeof(u32);
	setfile_index_table = (struct fimc_is_setfile_element_info *)(setfile_table + \
				(ulong)(subip_num * sizeof(u32)));

	dbg_hw("%s: version: %d\n", __func__, ver);
	dbg_hw("%s: scenario number: %d\n", __func__, scenario_num);
	dbg_hw("%s: subip number: %d\n", __func__, subip_num);
	dbg_hw("%s: offset: 0x%08x\n", __func__, setfile_offset);
	dbg_hw("%s: scenario_table: 0x%lx\n", __func__, scenario_table);
	dbg_hw("%s: setfile base: 0x%lx\n", __func__, setfile_base);
	dbg_hw("%s: setfile_table: 0x%lx\n", __func__, setfile_table);
	dbg_hw("%s: setfile_index_table: 0x%p addr(0x%x) size(0x%x)\n",
		__func__, setfile_index_table,
		setfile_index_table->addr, setfile_index_table->size);

	/* 3. set version */
	_set_setfile_version(hardware, ver);

	designed_bit = header_v3->designed_bit;
	/* 4. set scenaio index, setfile address and size */
	for (i = 0; i < subip_num; i++) {
		hw_id = _get_hw_id_by_settfile(i, subip_num, &designed_bit);
		if (hw_id < 0) {
			err_hw("invalid hw (%d)", hw_id);
			return -EINVAL;
		}

		/* set what setfile index is used at each scenario */
		for (j = 0; j < scenario_num; j++) {
			setfile_index = (u32)*(ulong *)scenario_table;
			if (valid_hw_slot_id(fimc_is_hw_slot_id(hw_id))) {
				ret = _set_senario_setfile_index(hardware, hw_id, j, setfile_index);
				if (ret) {
					err_hw("setting scenario index failed: [ID:%d]", hw_id);
					return -EINVAL;
				}
			}
			scenario_table += sizeof(u32);
		}

		/* set the number of setfile at each sub IP */
		setfile_num = (u32)*(ulong *)setfile_table;
		if (valid_hw_slot_id(fimc_is_hw_slot_id(hw_id)))
			_set_setfile_number(hardware, hw_id, setfile_num);
		setfile_table += sizeof(u32);

		/* set each setfile address and size */
		for (j = 0; j < setfile_num; j++) {
			if (valid_hw_slot_id(fimc_is_hw_slot_id(hw_id))) {
				_set_setfile_table(hardware, hw_id,
					(ulong)(setfile_base + setfile_index_table->addr),
					setfile_index_table->size, j);
				/* load setfile */
				_load_setfile(hardware, hw_id, j, instance, hw_map);
			}
			setfile_index_table++;;
		}
	}

	return ret;
}

int fimc_is_hardware_apply_setfile(struct fimc_is_hardware *hardware, u32 instance,
	u32 sub_mode, ulong hw_map)
{
	struct fimc_is_hw_ip *hw_ip = NULL;
	int hw_id = 0;
	int ret = 0;
	int hw_slot = -1;

	BUG_ON(!hardware);

	info_hw("[%d]apply_setfile: hw_map (0x%lx)\n", instance, hw_map);

	for (hw_slot = 0; hw_slot < HW_SLOT_MAX; hw_slot++) {
		hw_ip = &hardware->hw_ip[hw_slot];
		hw_id = hw_ip->id;
		ret = CALL_HW_OPS(hw_ip, apply_setfile, sub_mode, instance, hw_map);
		if (ret) {
			err_hw("[%d][ID:%d] apply_setfile fail (%d)", instance, hw_id, ret);
			return -EINVAL;
		}
	}

	return ret;
}

int fimc_is_hardware_delete_setfile(struct fimc_is_hardware *hardware, u32 instance,
	ulong hw_map)
{
	int ret = 0;
	int hw_slot = -1;
	struct fimc_is_hw_ip *hw_ip = NULL;
	enum fimc_is_hardware_id hw_id = DEV_HW_END;

	BUG_ON(!hardware);

	info_hw("[%d]delete_setfile: hw_map (0x%lx)\n", instance, hw_map);
	for (hw_slot = 0; hw_slot < HW_SLOT_MAX; hw_slot++) {
		hw_ip = &hardware->hw_ip[hw_slot];
		ret = CALL_HW_OPS(hw_ip, delete_setfile, instance, hw_map);
		if (ret) {
			err_hw("[%d]delete_setfile fail (%d)", instance, hw_id);
			return -EINVAL;
		}
	}

	return ret;
}

int fimc_is_hardware_runtime_resume(struct fimc_is_hardware *hardware)
{
	int ret = 0;
#ifdef ENABLE_DIRECT_CLOCK_GATE
	int hw_slot = -1;
	struct fimc_is_hw_ip *hw_ip = NULL;

	BUG_ON(!hardware);

	for (hw_slot = 0; hw_slot < HW_SLOT_MAX; hw_slot++) {
		hw_ip = &hardware->hw_ip[hw_slot];
		/* init clk gating variable */
		if (hw_ip->clk_gate != NULL)
			memset(hw_ip->clk_gate->refcnt, 0x0, sizeof(int) * HW_SLOT_MAX);
	}
#endif
	return ret;
}

int fimc_is_hardware_runtime_suspend(struct fimc_is_hardware *hardware)
{
	return 0;
}

void fimc_is_hardware_clk_gate(struct fimc_is_hw_ip *hw_ip, u32 instance,
	bool on, bool close)
{
#ifdef ENABLE_DIRECT_CLOCK_GATE
	struct fimc_is_group *parent;
	struct fimc_is_clk_gate *clk_gate;
	u32 idx;
	ulong flag;

	BUG_ON(!hw_ip);

	if (!sysfs_debug.en_clk_gate || hw_ip->clk_gate == NULL)
		return;

	clk_gate = hw_ip->clk_gate;
	idx = hw_ip->clk_gate_idx;

	if (close) {
		spin_lock_irqsave(&clk_gate->slock, flag);
		FIMC_IS_CLOCK_ON(clk_gate->regs, clk_gate->bit[idx]);
		spin_unlock_irqrestore(&clk_gate->slock, flag);
		return;
	}

	if (hw_ip->group[instance])
		parent = hw_ip->group[instance]->parent ? hw_ip->group[instance]->parent : hw_ip->group[instance];
	else
		return;

	if (test_bit(FIMC_IS_GROUP_OTF_INPUT, &parent->state))
		return;

	spin_lock_irqsave(&clk_gate->slock, flag);

	if(on) {
		clk_gate->refcnt[idx]++;

		if (clk_gate->refcnt[idx] > 1)
			goto exit;

		FIMC_IS_CLOCK_ON(clk_gate->regs, clk_gate->bit[idx]);
	} else {
		clk_gate->refcnt[idx]--;

		if (clk_gate->refcnt[idx] >= 1)
			goto exit;

		if(clk_gate->refcnt[idx] < 0){
			warn("[%d][ID:%d] clock is already disable(%d)", instance, hw_ip->id, clk_gate->refcnt[idx]);
			clk_gate->refcnt[idx] = 0;
			goto exit;
		}

		FIMC_IS_CLOCK_OFF(clk_gate->regs, clk_gate->bit[idx]);
	}
exit:
	spin_unlock_irqrestore(&clk_gate->slock, flag);

	if (clk_gate->refcnt[idx] > FIMC_IS_STREAM_COUNT)
		warn("[%d][ID:%d] abnormal clk_gate refcnt(%d)", instance, hw_ip->id, clk_gate->refcnt[idx]);

	return;
#endif
}

void fimc_is_hardware_sfr_dump(struct fimc_is_hardware *hardware)
{
	int hw_slot = -1;
	int reg_size = 0;
	struct fimc_is_hw_ip *hw_ip = NULL;

	if (!hardware) {
		err_hw("hardware is null\n");
		return;
	}

	for (hw_slot = 0; hw_slot < HW_SLOT_MAX; hw_slot++) {
		hw_ip = &hardware->hw_ip[hw_slot];

		if (!test_bit(HW_OPEN, &hw_ip->state))
			continue;

		if (IS_ERR_OR_NULL(hw_ip->sfr_dump)) {
			warn_hw("[ID:%d] sfr_dump memory is invalid", hw_ip->id);
			continue;
		}

		/* dump reg */
		reg_size = (hw_ip->regs_end - hw_ip->regs_start + 1);
		memcpy(hw_ip->sfr_dump, hw_ip->regs, reg_size);

		info_hw("[ID:%d] ##### SFR DUMP(V/P/S):(%p/%p/0x%X)[0x%llX~0x%llX]\n",
				hw_ip->id, hw_ip->sfr_dump, (void *)virt_to_phys(hw_ip->sfr_dump),
				reg_size, hw_ip->regs_start, hw_ip->regs_end);
#ifdef ENABLE_PANIC_SFR_PRINT
		print_hex_dump(KERN_INFO, "", DUMP_PREFIX_OFFSET, 32, 4,
				hw_ip->regs, reg_size, false);
#endif
		if (IS_ERR_OR_NULL(hw_ip->sfr_b_dump))
			continue;

		/* dump reg B */
		reg_size = (hw_ip->regs_b_end - hw_ip->regs_b_start + 1);
		memcpy(hw_ip->sfr_b_dump, hw_ip->regs_b, reg_size);

		info_hw("[ID:%d] ##### SFR B DUMP(V/P/S):(%p/%p/0x%X)[0x%llX~0x%llX]\n",
				hw_ip->id, hw_ip->sfr_b_dump, (void *)virt_to_phys(hw_ip->sfr_b_dump),
				reg_size, hw_ip->regs_b_start, hw_ip->regs_b_end);
#ifdef ENABLE_PANIC_SFR_PRINT
		print_hex_dump(KERN_INFO, "", DUMP_PREFIX_OFFSET, 32, 4,
				hw_ip->regs_b, reg_size, false);
#endif
	}
}