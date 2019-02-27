/*
 * Samsung EXYNOS FIMC-IS (Imaging Subsystem) driver
 *
 * Copyright (C) 2014 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "fimc-is-hw-mcscaler-v2.h"
#include "api/fimc-is-hw-api-mcscaler-v2.h"
#include "../interface/fimc-is-interface-ischain.h"
#include "fimc-is-param.h"

static int fimc_is_hw_mcsc_handle_interrupt(u32 id, void *context)
{
	struct fimc_is_hw_ip *hw_ip = NULL;
	u32 status, intr_mask, intr_status;
	bool err_intr_flag = false;
	int ret = 0;
	u32 hl = 0, vl = 0;
	u32 instance;
	u32 hw_fcount, index;

	hw_ip = (struct fimc_is_hw_ip *)context;
	hw_fcount = atomic_read(&hw_ip->fcount);
	instance = atomic_read(&hw_ip->instance);

	fimc_is_scaler_get_input_status(hw_ip->regs, hw_ip->id, &hl, &vl);
	/* read interrupt status register (sc_intr_status) */
	intr_mask = fimc_is_scaler_get_intr_mask(hw_ip->regs, hw_ip->id);
	intr_status = fimc_is_scaler_get_intr_status(hw_ip->regs, hw_ip->id);
	status = (~intr_mask) & intr_status;

	if (status & (1 << INTR_MC_SCALER_OVERFLOW)) {
		err_hw("[MCSC]Overflow!! (0x%x)", status);
		err_intr_flag = true;
	}

	if (status & (1 << INTR_MC_SCALER_OUTSTALL)) {
		err_hw("[MCSC]Output Block BLOCKING!! (0x%x)", status);
		err_intr_flag = true;
	}

	if (status & (1 << INTR_MC_SCALER_INPUT_VERTICAL_UNF)) {
		err_hw("[MCSC]Input OTF Vertical Underflow!! (0x%x)", status);
		err_intr_flag = true;
	}

	if (status & (1 << INTR_MC_SCALER_INPUT_VERTICAL_OVF)) {
		err_hw("[MCSC]Input OTF Vertical Overflow!! (0x%x)", status);
		err_intr_flag = true;
	}

	if (status & (1 << INTR_MC_SCALER_INPUT_HORIZONTAL_UNF)) {
		err_hw("[MCSC]Input OTF Horizontal Underflow!! (0x%x)", status);
		err_intr_flag = true;
	}

	if (status & (1 << INTR_MC_SCALER_INPUT_HORIZONTAL_OVF)) {
		err_hw("[MCSC]Input OTF Horizontal Overflow!! (0x%x)", status);
		err_intr_flag = true;
	}

	if (status & (1 << INTR_MC_SCALER_WDMA_FINISH))
		err_hw("[MCSC]Disabeld interrupt occurred! WDAM FINISH!! (0x%x)", status);

	if (status & (1 << INTR_MC_SCALER_FRAME_START)) {
		atomic_inc(&hw_ip->count.fs);
		hw_ip->debug_index[1] = hw_ip->debug_index[0] % DEBUG_FRAME_COUNT;
		index = hw_ip->debug_index[1];
		hw_ip->debug_info[index].fcount = hw_ip->debug_index[0];
		hw_ip->debug_info[index].cpuid[DEBUG_POINT_FRAME_START] = raw_smp_processor_id();
		hw_ip->debug_info[index].time[DEBUG_POINT_FRAME_START] = cpu_clock(raw_smp_processor_id());
		if (!atomic_read(&hw_ip->hardware->stream_on))
			info_hw("[ID:%d][F:%d]F.S\n", hw_ip->id, hw_fcount);

		fimc_is_hardware_frame_start(hw_ip, instance);
	}

	if (status & (1 << INTR_MC_SCALER_FRAME_END)) {
		if (fimc_is_hw_mcsc_frame_done(hw_ip, NULL, FRAME_DONE_NORMAL)) {
			index = hw_ip->debug_index[1];
			hw_ip->debug_info[index].cpuid[DEBUG_POINT_FRAME_DMA_END] = raw_smp_processor_id();
			hw_ip->debug_info[index].time[DEBUG_POINT_FRAME_DMA_END] = cpu_clock(raw_smp_processor_id());
			if (!atomic_read(&hw_ip->hardware->stream_on))
				info_hw("[ID:%d][F:%d]F.E DMA\n", hw_ip->id, atomic_read(&hw_ip->fcount));

			atomic_inc(&hw_ip->count.dma);
		} else {
			index = hw_ip->debug_index[1];
			hw_ip->debug_info[index].cpuid[DEBUG_POINT_FRAME_END] = raw_smp_processor_id();
			hw_ip->debug_info[index].time[DEBUG_POINT_FRAME_END] = cpu_clock(raw_smp_processor_id());
			if (!atomic_read(&hw_ip->hardware->stream_on))
				info_hw("[ID:%d][F:%d]F.E\n", hw_ip->id, hw_fcount);

			fimc_is_hardware_frame_done(hw_ip, NULL, -1, FIMC_IS_HW_CORE_END,
				0, FRAME_DONE_NORMAL);
		}

		atomic_set(&hw_ip->status.Vvalid, V_BLANK);
		atomic_inc(&hw_ip->count.fe);
		if (atomic_read(&hw_ip->count.fs) < atomic_read(&hw_ip->count.fe)) {
			err_hw("[MCSC] fs(%d), fe(%d), dma(%d)\n",
				atomic_read(&hw_ip->count.fs),
				atomic_read(&hw_ip->count.fe),
				atomic_read(&hw_ip->count.dma));
		}

		wake_up(&hw_ip->status.wait_queue);
	}

	if (err_intr_flag) {
		info_hw("[MCSC][F:%d] Ocurred error interrupt (%d,%d) status(0x%x)\n",
			hw_fcount, hl, vl, status);
		fimc_is_hardware_size_dump(hw_ip);
	}

	fimc_is_scaler_clear_intr_src(hw_ip->regs, hw_ip->id, status);

	if (status & (1 << INTR_MC_SCALER_FRAME_END))
		CALL_HW_OPS(hw_ip, clk_gate, instance, false, false);

	return ret;
}

const struct fimc_is_hw_ip_ops fimc_is_hw_mcsc_ops = {
	.open			= fimc_is_hw_mcsc_open,
	.init			= fimc_is_hw_mcsc_init,
	.close			= fimc_is_hw_mcsc_close,
	.enable			= fimc_is_hw_mcsc_enable,
	.disable		= fimc_is_hw_mcsc_disable,
	.shot			= fimc_is_hw_mcsc_shot,
	.set_param		= fimc_is_hw_mcsc_set_param,
	.frame_ndone		= fimc_is_hw_mcsc_frame_ndone,
	.load_setfile		= fimc_is_hw_mcsc_load_setfile,
	.apply_setfile		= fimc_is_hw_mcsc_apply_setfile,
	.delete_setfile		= fimc_is_hw_mcsc_delete_setfile,
	.size_dump		= fimc_is_hw_mcsc_size_dump,
	.clk_gate		= fimc_is_hardware_clk_gate
};

int fimc_is_hw_mcsc_probe(struct fimc_is_hw_ip *hw_ip, struct fimc_is_interface *itf,
	struct fimc_is_interface_ischain *itfc,	int id)
{
	int ret = 0;
	int hw_slot = -1;

	BUG_ON(!hw_ip);
	BUG_ON(!itf);
	BUG_ON(!itfc);

	/* initialize device hardware */
	hw_ip->id   = id;
	hw_ip->ops  = &fimc_is_hw_mcsc_ops;
	hw_ip->itf  = itf;
	hw_ip->itfc = itfc;
	atomic_set(&hw_ip->fcount, 0);
	hw_ip->internal_fcount = 0;
	hw_ip->is_leader = true;
	atomic_set(&hw_ip->status.Vvalid, V_BLANK);
	atomic_set(&hw_ip->rsccount, 0);
	init_waitqueue_head(&hw_ip->status.wait_queue);

	/* set mcsc sfr base address */
	hw_slot = fimc_is_hw_slot_id(id);
	if (!valid_hw_slot_id(hw_slot)) {
		err_hw("invalid hw_slot (%d, %d)", id, hw_slot);
		return -EINVAL;
	}

	/* set mcsc interrupt handler */
	itfc->itf_ip[hw_slot].handler[INTR_HWIP1].handler = &fimc_is_hw_mcsc_handle_interrupt;

	clear_bit(HW_OPEN, &hw_ip->state);
	clear_bit(HW_INIT, &hw_ip->state);
	clear_bit(HW_CONFIG, &hw_ip->state);
	clear_bit(HW_RUN, &hw_ip->state);
	clear_bit(HW_TUNESET, &hw_ip->state);

	info_hw("[ID:%2d] probe done\n", id);

	return ret;
}

int fimc_is_hw_mcsc_open(struct fimc_is_hw_ip *hw_ip, u32 instance, u32 *size)
{
	int ret = 0;

	BUG_ON(!hw_ip);

	if (test_bit(HW_OPEN, &hw_ip->state))
		return 0;

	*size = sizeof(struct fimc_is_hw_mcsc);

	frame_manager_probe(hw_ip->framemgr, FRAMEMGR_ID_HW | hw_ip->id);
	frame_manager_open(hw_ip->framemgr, FIMC_IS_MAX_HW_FRAME);
	frame_manager_probe(hw_ip->framemgr_late, FRAMEMGR_ID_HW | hw_ip->id | 0xF0);
	frame_manager_open(hw_ip->framemgr_late, FIMC_IS_MAX_HW_FRAME_LATE);

	ret = fimc_is_hw_mcsc_reset(hw_ip);
	if (ret != 0) {
		err_hw("MCSC sw reset fail");
		return -ENODEV;
	}

	atomic_set(&hw_ip->status.Vvalid, V_BLANK);

	return ret;
}

int fimc_is_hw_mcsc_init(struct fimc_is_hw_ip *hw_ip, struct fimc_is_group *group,
	bool flag, u32 module_id)
{
	int ret = 0;
	u32 instance = 0;
	struct fimc_is_hw_mcsc *hw_mcsc;

	BUG_ON(!hw_ip);

	instance = group->instance;
	hw_ip->group[instance] = group;
	hw_mcsc = (struct fimc_is_hw_mcsc *)hw_ip->priv_info;
	hw_mcsc->rep_flag[instance] = flag;
	hw_mcsc->instance = 0;

	/* input source select 0: otf, 1:rdma */
	if (test_bit(FIMC_IS_GROUP_OTF_INPUT, &group->state))
		fimc_is_scaler_set_input_source(hw_ip->regs, hw_ip->id, 0);
	else
		fimc_is_scaler_set_input_source(hw_ip->regs, hw_ip->id, 1);

	return ret;
}

int fimc_is_hw_mcsc_close(struct fimc_is_hw_ip *hw_ip, u32 instance)
{
	int ret = 0;

	BUG_ON(!hw_ip);

	if (!test_bit(HW_OPEN, &hw_ip->state))
		return 0;

	info_hw("[%d]close (%d)(%d)\n", instance, hw_ip->id, atomic_read(&hw_ip->rsccount));

	return ret;
}

int fimc_is_hw_mcsc_enable(struct fimc_is_hw_ip *hw_ip, u32 instance, ulong hw_map)
{
	int ret = 0;

	BUG_ON(!hw_ip);

	if (!test_bit_variables(hw_ip->id, &hw_map))
		return 0;

	if (!test_bit(HW_INIT, &hw_ip->state)) {
		err_hw("[%d]not initialized!! (%d)", instance, hw_ip->id);
		return -EINVAL;
	}

	set_bit(HW_RUN, &hw_ip->state);

	return ret;
}

int fimc_is_hw_mcsc_disable(struct fimc_is_hw_ip *hw_ip, u32 instance, ulong hw_map)
{
	int ret = 0;
	int i;
	u32 timetowait;

	BUG_ON(!hw_ip);

	if (!test_bit_variables(hw_ip->id, &hw_map))
		return 0;

	info_hw("[%d][ID:%d]mcsc_disable: Vvalid(%d)\n", instance, hw_ip->id,
		atomic_read(&hw_ip->status.Vvalid));

	if (test_bit(HW_RUN, &hw_ip->state)) {
		timetowait = wait_event_timeout(hw_ip->status.wait_queue,
			!atomic_read(&hw_ip->status.Vvalid),
			FIMC_IS_HW_STOP_TIMEOUT);

		if (!timetowait) {
			err_hw("[%d][ID:%d] wait FRAME_END timeout (%u)", instance,
				hw_ip->id, timetowait);
			ret = -ETIME;
		}

		/* disable MCSC */
		fimc_is_scaler_clear_rdma_addr(hw_ip->regs);
		for (i = MCSC_OUTPUT0; i < MCSC_OUTPUT_MAX; i++)
			fimc_is_scaler_clear_wdma_addr(hw_ip->regs, i);

		fimc_is_scaler_stop(hw_ip->regs, hw_ip->id);

		ret = fimc_is_hw_mcsc_reset(hw_ip);
		if (ret != 0) {
			err_hw("MCSC sw reset fail");
			return -ENODEV;
		}

		clear_bit(HW_RUN, &hw_ip->state);
		clear_bit(HW_CONFIG, &hw_ip->state);
	} else {
		dbg_hw("[%d]already disabled (%d)\n", instance, hw_ip->id);
	}

	return ret;
}

static int fimc_is_hw_mcsc_rdma_cfg(struct fimc_is_hw_ip *hw_ip, struct fimc_is_frame *frame)
{
	int ret = 0;
	u32 rdma_addr[4] = {0};

	rdma_addr[0] = frame->shot->uctl.scalerUd.sourceAddress[0];
	rdma_addr[1] = frame->shot->uctl.scalerUd.sourceAddress[1];
	rdma_addr[2] = frame->shot->uctl.scalerUd.sourceAddress[2];

	/* DMA in */
	dbg_hw("[%d]mcsc_rdma_cfg [F:%d][addr: %lx]\n",
		frame->instance, frame->fcount, rdma_addr[0]);

	if (rdma_addr[0] == 0) {
		err_hw("Wrong rdma_addr(%x)\n", rdma_addr[0]);
		fimc_is_scaler_clear_rdma_addr(hw_ip->regs);
		ret = -EINVAL;
		return ret;
	}

	/* use only one buffer (per-frame) */
	fimc_is_scaler_set_rdma_frame_seq(hw_ip->regs,
		0x1 << USE_DMA_BUFFER_INDEX);
	fimc_is_scaler_set_rdma_addr(hw_ip->regs,
		rdma_addr[0], rdma_addr[1], rdma_addr[2],
		USE_DMA_BUFFER_INDEX);

	return ret;
}


static void fimc_is_hw_mcsc_wdma_cfg(struct fimc_is_hw_ip *hw_ip, struct fimc_is_frame *frame)
{
	int i;
	struct mcs_param *param;
	u32 wdma_addr[MCSC_OUTPUT_MAX][4] = {{0}};

	param = &hw_ip->region[frame->instance]->parameter.mcs;

	if (frame->type == SHOT_TYPE_INTERNAL)
		goto skip_addr;

	for (i = 0; i < 3; i++) {
		wdma_addr[MCSC_OUTPUT0][i] = frame->shot->uctl.scalerUd.sc0TargetAddress[i];
		wdma_addr[MCSC_OUTPUT1][i] = frame->shot->uctl.scalerUd.sc1TargetAddress[i];
		wdma_addr[MCSC_OUTPUT2][i] = frame->shot->uctl.scalerUd.sc2TargetAddress[i];
		wdma_addr[MCSC_OUTPUT3][i] = frame->shot->uctl.scalerUd.sc3TargetAddress[i];
		wdma_addr[MCSC_OUTPUT4][i] = frame->shot->uctl.scalerUd.sc4TargetAddress[i];
	}
skip_addr:

	/* DMA out */
	for (i = MCSC_OUTPUT0; i < MCSC_OUTPUT_MAX; i++) {
		dbg_hw("[%d]mcsc_wdma_cfg [F:%d][T:%d][addr%d: %lx]\n",
			frame->instance, frame->fcount, frame->type, i, wdma_addr[i][0]);

		if (param->output[i].dma_cmd != DMA_OUTPUT_COMMAND_DISABLE
			&& wdma_addr[i][0] != 0
			&& frame->type != SHOT_TYPE_INTERNAL) {
			fimc_is_scaler_set_dma_out_enable(hw_ip->regs, i, true);

			/* use only one buffer (per-frame) */
			fimc_is_scaler_set_wdma_frame_seq(hw_ip->regs, i,
				0x1 << USE_DMA_BUFFER_INDEX);
			fimc_is_scaler_set_wdma_addr(hw_ip->regs, i,
				wdma_addr[i][0], wdma_addr[i][1], wdma_addr[i][2],
				USE_DMA_BUFFER_INDEX);
		} else {
			fimc_is_scaler_set_dma_out_enable(hw_ip->regs, i, false);
			fimc_is_scaler_clear_wdma_addr(hw_ip->regs, i);
			dbg_hw("[%d][ID:%d] Disable dma out[%d]\n",
					frame->instance, hw_ip->id, i);
		}
	}
}

int fimc_is_hw_mcsc_shot(struct fimc_is_hw_ip *hw_ip, struct fimc_is_frame *frame,
	ulong hw_map)
{
	int ret = 0;
	struct fimc_is_group *parent;
	struct fimc_is_hw_mcsc *hw_mcsc;
	struct mcs_param *param;
	bool start_flag = true;
	u32 lindex, hindex;

	BUG_ON(!hw_ip);
	BUG_ON(!frame);

	dbg_hw("[%d][ID:%d]shot [F:%d]\n", frame->instance, hw_ip->id, frame->fcount);

	if (!test_bit(HW_INIT, &hw_ip->state)) {
		dbg_hw("[%d][ID:%d] hw_mcsc not initialized\n",
			frame->instance, hw_ip->id);
		return -EINVAL;
	}

	if (!test_bit_variables(hw_ip->id, &hw_map))
		return 0;

	if ((!test_bit(ENTRY_M0P, &frame->out_flag))
		&& (!test_bit(ENTRY_M1P, &frame->out_flag))
		&& (!test_bit(ENTRY_M2P, &frame->out_flag))
		&& (!test_bit(ENTRY_M3P, &frame->out_flag))
		&& (!test_bit(ENTRY_M4P, &frame->out_flag)))
		set_bit(hw_ip->id, &frame->core_flag);

	hw_mcsc = (struct fimc_is_hw_mcsc *)hw_ip->priv_info;
	param = &hw_ip->region[frame->instance]->parameter.mcs;

	parent = hw_ip->group[frame->instance];
	while (parent->parent)
		parent = parent->parent;

	if (test_bit(FIMC_IS_GROUP_OTF_INPUT, &parent->state)) {
		if (!test_bit(HW_CONFIG, &hw_ip->state) && !atomic_read(&hw_ip->hardware->stream_on))
			start_flag = true;
		else
			start_flag = false;
	} else {
		start_flag = true;
	}

	if (frame->type == SHOT_TYPE_INTERNAL) {
		dbg_hw("[%d][ID:%d] request not exist\n", frame->instance, hw_ip->id);
		goto config;
	}

	lindex = frame->shot->ctl.vendor_entry.lowIndexParam;
	hindex = frame->shot->ctl.vendor_entry.highIndexParam;

	fimc_is_hw_mcsc_update_param(hw_ip, param,
		lindex, hindex, frame->instance);

	dbg_hw("[%d]mcsc_shot [F:%d][T:%d]\n",
		frame->instance, frame->fcount, frame->type);

config:
	/* RDMA cfg */
	if (param->input.dma_cmd == DMA_INPUT_COMMAND_ENABLE) {
		ret = fimc_is_hw_mcsc_rdma_cfg(hw_ip, frame);
		if (ret) {
			err_hw("[%d][ID:%d][F:%d] scaler rdma_cfg failed\n",
				frame->instance, hw_ip->id, frame->fcount);
			return ret;
		}
	}

	/* RDMA cfg */
	fimc_is_hw_mcsc_wdma_cfg(hw_ip, frame);

	if (start_flag) {
		dbg_hw("mcsc_start[F:%d][I:%d]\n", frame->fcount, frame->instance);
		fimc_is_scaler_start(hw_ip->regs, hw_ip->id);
		if (ret) {
			err_hw("[%d][ID:%d]scaler_start failed!!\n",
				frame->instance, hw_ip->id);
			return -EINVAL;
		}
	}

	set_bit(HW_CONFIG, &hw_ip->state);

	return ret;
}

int fimc_is_hw_mcsc_set_param(struct fimc_is_hw_ip *hw_ip, struct is_region *region,
	u32 lindex, u32 hindex, u32 instance, ulong hw_map)
{
	int ret = 0;
	struct fimc_is_hw_mcsc *hw_mcsc;
	struct mcs_param *param;

	BUG_ON(!hw_ip);
	BUG_ON(!region);

	if (!test_bit_variables(hw_ip->id, &hw_map))
		return 0;

	if (!test_bit(HW_INIT, &hw_ip->state)) {
		err_hw("[%d]not initialized!!", instance);
		return -EINVAL;
	}

	hw_ip->region[instance] = region;
	hw_ip->lindex[instance] = lindex;
	hw_ip->hindex[instance] = hindex;

	param = &region->parameter.mcs;
	hw_mcsc = (struct fimc_is_hw_mcsc *)hw_ip->priv_info;

	if (hw_mcsc->rep_flag[instance]) {
		dbg_hw("[%d] skip mcsc set_param(rep_flag(%d))\n",
			instance, hw_mcsc->rep_flag[instance]);
		return 0;
	}

	fimc_is_hw_mcsc_update_param(hw_ip, param,
		lindex, hindex, instance);

	return ret;
}

int fimc_is_hw_mcsc_update_register(struct fimc_is_hw_ip *hw_ip,
	struct mcs_param *param, u32 output_id, u32 instance)
{
	int ret = 0;

	ret = fimc_is_hw_mcsc_poly_phase(hw_ip, &param->input,
			&param->output[output_id], output_id, instance);
	ret = fimc_is_hw_mcsc_post_chain(hw_ip, &param->input,
			&param->output[output_id], output_id, instance);
	ret = fimc_is_hw_mcsc_flip(hw_ip, &param->output[output_id], output_id, instance);
	ret = fimc_is_hw_mcsc_otf_output(hw_ip, &param->output[output_id], output_id, instance);
	ret = fimc_is_hw_mcsc_dma_output(hw_ip, &param->output[output_id], output_id, instance);
	ret = fimc_is_hw_mcsc_output_yuvrange(hw_ip, &param->output[output_id], output_id, instance);

	return ret;
}

int fimc_is_hw_mcsc_update_param(struct fimc_is_hw_ip *hw_ip,
	struct mcs_param *param, u32 lindex, u32 hindex, u32 instance)
{
	int ret = 0;
	bool control_cmd = false;
	struct fimc_is_hw_mcsc *hw_mcsc;

	BUG_ON(!hw_ip);
	BUG_ON(!param);

	hw_mcsc = (struct fimc_is_hw_mcsc *)hw_ip->priv_info;

	if (hw_mcsc->instance != instance) {
		control_cmd = true;
		info_hw("[%d]hw_mcsc_update_param: hw_ip->instance(%d), control_cmd(%d)\n",
			instance, hw_mcsc->instance, control_cmd);
		hw_mcsc->instance = instance;
	}

	if (control_cmd || (lindex & LOWBIT_OF(PARAM_MCS_INPUT))
		|| (hindex & HIGHBIT_OF(PARAM_MCS_INPUT))) {
		ret = fimc_is_hw_mcsc_otf_input(hw_ip, &param->input, instance);
		ret = fimc_is_hw_mcsc_dma_input(hw_ip, &param->input, instance);
	}

	if (control_cmd || (lindex & LOWBIT_OF(PARAM_MCS_OUTPUT0))
		|| (hindex & HIGHBIT_OF(PARAM_MCS_OUTPUT0))) {
		ret = fimc_is_hw_mcsc_update_register(hw_ip, param, MCSC_OUTPUT0, instance);
	}

	if (control_cmd || (lindex & LOWBIT_OF(PARAM_MCS_OUTPUT1))
		|| (hindex & HIGHBIT_OF(PARAM_MCS_OUTPUT1))) {
		ret = fimc_is_hw_mcsc_update_register(hw_ip, param, MCSC_OUTPUT1, instance);
	}

	if (control_cmd || (lindex & LOWBIT_OF(PARAM_MCS_OUTPUT2))
		|| (hindex & HIGHBIT_OF(PARAM_MCS_OUTPUT2))) {
		ret = fimc_is_hw_mcsc_update_register(hw_ip, param, MCSC_OUTPUT2, instance);
	}

	if (control_cmd || (lindex & LOWBIT_OF(PARAM_MCS_OUTPUT3))
		|| (hindex & HIGHBIT_OF(PARAM_MCS_OUTPUT3))) {
		ret = fimc_is_hw_mcsc_update_register(hw_ip, param, MCSC_OUTPUT3, instance);
	}

	if (control_cmd || (lindex & LOWBIT_OF(PARAM_MCS_OUTPUT4))
		|| (hindex & HIGHBIT_OF(PARAM_MCS_OUTPUT4))) {
		ret = fimc_is_hw_mcsc_update_register(hw_ip, param, MCSC_OUTPUT4, instance);
	}

	if (ret)
		fimc_is_hw_mcsc_size_dump(hw_ip);

	return ret;
}

int fimc_is_hw_mcsc_reset(struct fimc_is_hw_ip *hw_ip)
{
	int ret = 0;

	BUG_ON(!hw_ip);

	ret = fimc_is_scaler_sw_reset(hw_ip->regs, hw_ip->id, 0, 0);
	if (ret != 0) {
		err_hw("MCSC sw reset fail");
		return -ENODEV;
	}

	fimc_is_scaler_clear_intr_all(hw_ip->regs, hw_ip->id);
	fimc_is_scaler_disable_intr(hw_ip->regs, hw_ip->id);
	fimc_is_scaler_mask_intr(hw_ip->regs, hw_ip->id, MCSC_INTR_MASK);

	fimc_is_scaler_set_stop_req_post_en_ctrl(hw_ip->regs, hw_ip->id, 0);

	return ret;
}

int fimc_is_hw_mcsc_load_setfile(struct fimc_is_hw_ip *hw_ip, int index,
	u32 instance, ulong hw_map)
{
	int ret = 0;
	struct fimc_is_hw_mcsc *hw_mcsc;
	struct fimc_is_setfile_info *info;
	u32 setfile_index = 0;

	BUG_ON(!hw_ip);

	if (!test_bit_variables(hw_ip->id, &hw_map)) {
		dbg_hw("[%d]%s: hw_map(0x%lx)\n", instance, __func__, hw_map);
		return 0;
	}

	if (!test_bit(HW_INIT, &hw_ip->state)) {
		err_hw("[%d] not initialized!!", instance);
		return -ESRCH;
	}

	if (!unlikely(hw_ip->priv_info)) {
		err_hw("[%d][ID:%d] priv_info is NULL", instance, hw_ip->id);
		return -EINVAL;
	}
	hw_mcsc = (struct fimc_is_hw_mcsc *)hw_ip->priv_info;
	info = &hw_ip->setfile_info;

	switch (info->version) {
	case SETFILE_V2:
		break;
	case SETFILE_V3:
		break;
	default:
		err_hw("[%d][ID:%d] invalid version (%d)", instance, hw_ip->id,
			info->version);
		return -EINVAL;
	}

	setfile_index = info->index[index];
	hw_mcsc->setfile = (struct hw_api_scaler_setfile *)info->table[setfile_index].addr;
	if (hw_mcsc->setfile->setfile_version != MCSC_SETFILE_VERSION) {
		err_hw("[%d][ID:%d] setfile version(0x%x) is incorrect",
			instance, hw_ip->id, hw_mcsc->setfile->setfile_version);
		return -EINVAL;
	}

	set_bit(HW_TUNESET, &hw_ip->state);

	return ret;
}

int fimc_is_hw_mcsc_apply_setfile(struct fimc_is_hw_ip *hw_ip, int index,
	u32 instance, ulong hw_map)
{
	int ret = 0;
	struct fimc_is_hw_mcsc *hw_mcsc = NULL;
	struct fimc_is_setfile_info *info;
	u32 setfile_index = 0;

	BUG_ON(!hw_ip);

	if (!test_bit_variables(hw_ip->id, &hw_map)) {
		dbg_hw("[%d]%s: hw_map(0x%lx)\n", instance, __func__, hw_map);
		return 0;
	}

	if (!test_bit(HW_INIT, &hw_ip->state)) {
		err_hw("[%d] not initialized!!", instance);
		return -ESRCH;
	}

	if (!unlikely(hw_ip->priv_info)) {
		err_hw("MCSC priv info is NULL");
		return -EINVAL;
	}

	hw_mcsc = (struct fimc_is_hw_mcsc *)hw_ip->priv_info;
	info = &hw_ip->setfile_info;

	if (!hw_mcsc->setfile)
		return 0;

	setfile_index = info->index[index];
	info_hw("[%d][ID:%d] setfile (%d) scenario (%d)\n", instance, hw_ip->id,
		setfile_index, index);

	return ret;
}

int fimc_is_hw_mcsc_delete_setfile(struct fimc_is_hw_ip *hw_ip, u32 instance,
	ulong hw_map)
{
	struct fimc_is_hw_mcsc *hw_mcsc = NULL;

	BUG_ON(!hw_ip);

	if (!test_bit_variables(hw_ip->id, &hw_map)) {
		dbg_hw("[%d]%s: hw_map(0x%lx)\n", instance, __func__, hw_map);
		return 0;
	}

	if (test_bit(HW_TUNESET, &hw_ip->state)) {
		dbg_hw("[%d][ID:%d] setfile already deleted", instance, hw_ip->id);
		return 0;
	}

	hw_mcsc = (struct fimc_is_hw_mcsc *)hw_ip->priv_info;
	hw_mcsc->setfile = NULL;
	clear_bit(HW_TUNESET, &hw_ip->state);

	return 0;
}

bool fimc_is_hw_mcsc_frame_done(struct fimc_is_hw_ip *hw_ip, struct fimc_is_frame *frame,
	int done_type)
{
	int ret = 0;
	int status = 0;
	bool fdone_flag = false;
	struct fimc_is_frame *done_frame;
	struct fimc_is_framemgr *framemgr;

	switch (done_type) {
	case FRAME_DONE_NORMAL:
		status = 0;
		framemgr = hw_ip->framemgr;
		framemgr_e_barrier(framemgr, 0);
		done_frame = peek_frame(framemgr, FS_COMPLETE);
		framemgr_x_barrier(framemgr, 0);
		if (done_frame == NULL) {
			err_hw("[MCSC][F:%d] frame(null)!!", atomic_read(&hw_ip->fcount));
			BUG_ON(1);
		}
		break;
	case FRAME_DONE_LATE_SHOT:
	case FRAME_DONE_FORCE:
		status = 1;
		done_frame = frame;
		break;
	default:
		err_hw("[%d][F:%d] invalid done type(%d)\n", atomic_read(&hw_ip->instance),
			atomic_read(&hw_ip->fcount), done_type);
		return false;
	}

	if (test_bit(ENTRY_M0P, &done_frame->out_flag)) {
		ret = fimc_is_hardware_frame_done(hw_ip, frame,
			WORK_M0P_FDONE, ENTRY_M0P, status, done_type);
		fdone_flag = true;
	}

	if (test_bit(ENTRY_M1P, &done_frame->out_flag)) {
		ret = fimc_is_hardware_frame_done(hw_ip, frame,
			WORK_M1P_FDONE, ENTRY_M1P, status, done_type);
		fdone_flag = true;
	}

	if (test_bit(ENTRY_M2P, &done_frame->out_flag)) {
		ret = fimc_is_hardware_frame_done(hw_ip, frame,
			WORK_M2P_FDONE, ENTRY_M2P, status, done_type);
		fdone_flag = true;
	}

	if (test_bit(ENTRY_M3P, &done_frame->out_flag)) {
		ret = fimc_is_hardware_frame_done(hw_ip, frame,
			WORK_M3P_FDONE, ENTRY_M3P, status, done_type);
		fdone_flag = true;
	}

	if (test_bit(ENTRY_M4P, &done_frame->out_flag)) {
		ret = fimc_is_hardware_frame_done(hw_ip, frame,
			WORK_M4P_FDONE, ENTRY_M4P, status, done_type);
		fdone_flag = true;
	}

	return fdone_flag;
}

int fimc_is_hw_mcsc_frame_ndone(struct fimc_is_hw_ip *hw_ip, struct fimc_is_frame *frame,
	u32 instance, bool late_flag)
{
	int ret = 0;
	bool is_fdone = false;
	enum fimc_is_frame_done_type done_type;

	if (late_flag == true)
		done_type = FRAME_DONE_LATE_SHOT;
	else
		done_type = FRAME_DONE_FORCE;

	is_fdone = fimc_is_hw_mcsc_frame_done(hw_ip, frame, done_type);

	if (test_bit_variables(hw_ip->id, &frame->core_flag))
		ret = fimc_is_hardware_frame_done(hw_ip, frame, -1, FIMC_IS_HW_CORE_END,
				1, done_type);

	return ret;
}

int fimc_is_hw_mcsc_otf_input(struct fimc_is_hw_ip *hw_ip, struct param_mcs_input *input,
	u32 instance)
{
	int ret = 0;
	u32 width, height;
	u32 format, bit_width;
	struct fimc_is_hw_mcsc *hw_mcsc;

	BUG_ON(!hw_ip);
	BUG_ON(!input);

	hw_mcsc = (struct fimc_is_hw_mcsc *)hw_ip->priv_info;

	dbg_hw("[%d]hw_mcsc_otf_input_setting cmd(%d)\n", instance, input->otf_cmd);
	width  = input->width;
	height = input->height;
	format = input->otf_format;
	bit_width = input->otf_bitwidth;

	if (input->otf_cmd == OTF_INPUT_COMMAND_DISABLE)
		return ret;

	ret = fimc_is_hw_mcsc_check_format(HW_MCSC_OTF_INPUT,
		format, bit_width, width, height);
	if (ret) {
		err_hw("[%d]Invalid MCSC OTF Input format: fmt(%d),bit(%d),size(%dx%d)",
			instance, format, bit_width, width, height);
		return ret;
	}

	fimc_is_scaler_set_input_img_size(hw_ip->regs, hw_ip->id, width, height);
	fimc_is_scaler_set_dither(hw_ip->regs, hw_ip->id, input->otf_cmd);

	return ret;
}

int fimc_is_hw_mcsc_dma_input(struct fimc_is_hw_ip *hw_ip, struct param_mcs_input *input,
	u32 instance)
{
	int ret = 0;
	u32 width, height;
	u32 format, bit_width, plane, order;
	u32 y_stride, uv_stride;
	u32 img_format;
	struct fimc_is_hw_mcsc *hw_mcsc;

	BUG_ON(!hw_ip);
	BUG_ON(!input);

	hw_mcsc = (struct fimc_is_hw_mcsc *)hw_ip->priv_info;

	dbg_hw("[%d]hw_mcsc_dma_input_setting cmd(%d)\n", instance, input->dma_cmd);
	width  = input->dma_crop_width;
	height = input->dma_crop_height;
	format = input->dma_format;
	bit_width = input->dma_bitwidth;
	plane = input->plane;
	order = input->dma_order;

	if (input->dma_cmd == DMA_INPUT_COMMAND_DISABLE)
		return ret;

	ret = fimc_is_hw_mcsc_check_format(HW_MCSC_DMA_INPUT,
		format, bit_width, width, height);
	if (ret) {
		err_hw("[%d]Invalid MCSC DMA Input format: fmt(%d),bit(%d),size(%dx%d)",
			instance, format, bit_width, width, height);
		return ret;
	}

	fimc_is_scaler_set_input_img_size(hw_ip->regs, hw_ip->id, width, height);
	fimc_is_scaler_set_dither(hw_ip->regs, hw_ip->id, 0);

	fimc_is_hw_mcsc_adjust_stride(width, plane, false,
		&y_stride, &uv_stride);
	fimc_is_scaler_set_rdma_stride(hw_ip->regs, y_stride, uv_stride);

	ret = fimc_is_hw_mcsc_adjust_input_img_fmt(format, plane, order, &img_format);
	if (ret < 0) {
		warn_hw("[%d][ID:%d] Invalid rdma image format\n", instance, hw_ip->id);
		img_format = hw_mcsc->in_img_format;
	} else {
		hw_mcsc->in_img_format = img_format;
	}

	fimc_is_scaler_set_rdma_size(hw_ip->regs, width, height);
	fimc_is_scaler_set_rdma_format(hw_ip->regs, img_format);

	return ret;
}


int fimc_is_hw_mcsc_poly_phase(struct fimc_is_hw_ip *hw_ip, struct param_mcs_input *input,
	struct param_mcs_output *output, u32 output_id, u32 instance)
{
	int ret = 0;
	u32 src_pos_x, src_pos_y, src_width, src_height;
	u32 poly_dst_width, poly_dst_height;
	u32 out_width, out_height;
	ulong temp_width, temp_height;
	u32 hratio, vratio;
	struct fimc_is_hw_mcsc *hw_mcsc;

	BUG_ON(!hw_ip);
	BUG_ON(!input);
	BUG_ON(!output);

	hw_mcsc = (struct fimc_is_hw_mcsc *)hw_ip->priv_info;

	dbg_hw("[%d][OUT:%d]hw_mcsc_poly_phase_setting cmd(O:%d,D:%d)\n",
		instance, output_id, output->otf_cmd, output->dma_cmd);

	if ((output->otf_cmd == OTF_OUTPUT_COMMAND_DISABLE)
		&& (output->dma_cmd == DMA_OUTPUT_COMMAND_DISABLE)) {
		fimc_is_scaler_set_poly_scaler_enable(hw_ip->regs, output_id, 0);
		return ret;
	}

	fimc_is_scaler_set_poly_scaler_enable(hw_ip->regs, output_id, 1);

	src_pos_x = output->crop_offset_x;
	src_pos_y = output->crop_offset_y;
	src_width = output->crop_width;
	src_height = output->crop_height;

	out_width = output->width;
	out_height = output->height;

	fimc_is_scaler_set_poly_src_size(hw_ip->regs, output_id, src_pos_x, src_pos_y,
		src_width, src_height);

	/* if x1/4 ~ x1/28 scaling, poly scaling ratio to 1/4 */
	if (out_width < (src_width / 4)) {
		poly_dst_width = ALIGN(src_width / 4, 2);
		if (out_height < (src_height / 4))
			poly_dst_height = ALIGN(src_height / 4, 2);
		else
			poly_dst_height = out_height;
	} else {
		poly_dst_width = out_width;
		if (out_height < (src_height / 4)) {
			poly_dst_height = ALIGN(src_height / 4, 2);
		} else {
			poly_dst_height = out_height;
		}
	}

	fimc_is_scaler_set_poly_dst_size(hw_ip->regs, output_id,
		poly_dst_width, poly_dst_height);

	temp_width  = (ulong)src_width;
	temp_height = (ulong)src_height;
	hratio = (u32)((temp_width << 20) / poly_dst_width);
	vratio = (u32)((temp_height << 20) / poly_dst_height);

	fimc_is_scaler_set_poly_scaling_ratio(hw_ip->regs, output_id, hratio, vratio);
	fimc_is_scaler_set_poly_scaler_coef(hw_ip->regs, output_id, hratio, vratio);

	return ret;
}

int fimc_is_hw_mcsc_post_chain(struct fimc_is_hw_ip *hw_ip, struct param_mcs_input *input,
	struct param_mcs_output *output, u32 output_id, u32 instance)
{
	int ret = 0;
	u32 img_width, img_height;
	u32 dst_width, dst_height;
	ulong temp_width, temp_height;
	u32 hratio, vratio;
	struct fimc_is_hw_mcsc *hw_mcsc;

	BUG_ON(!hw_ip);
	BUG_ON(!input);
	BUG_ON(!output);

	hw_mcsc = (struct fimc_is_hw_mcsc *)hw_ip->priv_info;

	dbg_hw("[%d][OUT:%d]hw_mcsc_post_chain_setting cmd(O:%d,D:%d)\n",
		instance, output_id, output->otf_cmd, output->dma_cmd);

	if ((output->otf_cmd == OTF_OUTPUT_COMMAND_DISABLE)
		&& (output->dma_cmd == DMA_OUTPUT_COMMAND_DISABLE)) {
		fimc_is_scaler_set_post_scaler_enable(hw_ip->regs, output_id, 0);
		return ret;
	}

	fimc_is_scaler_get_poly_dst_size(hw_ip->regs, output_id, &img_width, &img_height);

	dst_width = output->width;
	dst_height = output->height;

	/* if x1 ~ x1/4 scaling, post scaler bypassed */
	if ((img_width == dst_width) && (img_height == dst_height)) {
		fimc_is_scaler_set_post_scaler_enable(hw_ip->regs, output_id, 0);
	} else {
		fimc_is_scaler_set_post_scaler_enable(hw_ip->regs, output_id, 1);
	}

	fimc_is_scaler_set_post_img_size(hw_ip->regs, output_id, img_width, img_height);
	fimc_is_scaler_set_post_dst_size(hw_ip->regs, output_id, dst_width, dst_height);

	temp_width  = (ulong)img_width;
	temp_height = (ulong)img_height;
	hratio = (u32)((temp_width << 20) / dst_width);
	vratio = (u32)((temp_height << 20) / dst_height);

	fimc_is_scaler_set_post_scaling_ratio(hw_ip->regs, output_id, hratio, vratio);

	return ret;
}

int fimc_is_hw_mcsc_flip(struct fimc_is_hw_ip *hw_ip, struct param_mcs_output *output,
	u32 output_id, u32 instance)
{
	int ret = 0;
	struct fimc_is_hw_mcsc *hw_mcsc;

	BUG_ON(!hw_ip);
	BUG_ON(!output);

	hw_mcsc = (struct fimc_is_hw_mcsc *)hw_ip->priv_info;

	dbg_hw("[%d][OUT:%d]hw_mcsc_flip_setting flip(%d),cmd(O:%d,D:%d)\n",
		instance, output_id, output->flip, output->otf_cmd, output->dma_cmd);

	if (output->dma_cmd == DMA_OUTPUT_COMMAND_DISABLE)
		return ret;

	fimc_is_scaler_set_flip_mode(hw_ip->regs, output_id, output->flip);

	return ret;
}

int fimc_is_hw_mcsc_otf_output(struct fimc_is_hw_ip *hw_ip, struct param_mcs_output *output,
	u32 output_id, u32 instance)
{
	int ret = 0;
	struct fimc_is_hw_mcsc *hw_mcsc;
	u32 out_width, out_height;
	u32 format, bitwidth;

	BUG_ON(!hw_ip);
	BUG_ON(!output);

	hw_mcsc = (struct fimc_is_hw_mcsc *)hw_ip->priv_info;

	dbg_hw("[%d][OUT:%d]hw_mcsc_otf_output_setting cmd(O:%d,D:%d)\n",
		instance, output_id, output->otf_cmd, output->dma_cmd);

	if (output->otf_cmd == OTF_OUTPUT_COMMAND_DISABLE) {
		fimc_is_scaler_set_otf_out_enable(hw_ip->regs, output_id, false);
		return ret;
	}

	out_width  = output->width;
	out_height = output->height;
	format     = output->otf_format;
	bitwidth  = output->otf_bitwidth;

	ret = fimc_is_hw_mcsc_check_format(HW_MCSC_OTF_OUTPUT,
		format, bitwidth, out_width, out_height);
	if (ret) {
		err_hw("[%d][OUT:%d]Invalid MCSC OTF Output format: fmt(%d),bit(%d),size(%dx%d)",
			instance, output_id, format, bitwidth, out_width, out_height);
		return ret;
	}

	fimc_is_scaler_set_otf_out_enable(hw_ip->regs, output_id, true);
	fimc_is_scaler_set_otf_out_path(hw_ip->regs, output_id);

	return ret;
}

int fimc_is_hw_mcsc_dma_output(struct fimc_is_hw_ip *hw_ip, struct param_mcs_output *output,
	u32 output_id, u32 instance)
{
	int ret = 0;
	u32 out_width, out_height, scaled_width, scaled_height;
	u32 format, plane, order,bitwidth;
	u32 y_stride, uv_stride;
	u32 img_format;
	bool conv420_en = false;
	struct fimc_is_hw_mcsc *hw_mcsc;

	BUG_ON(!hw_ip);
	BUG_ON(!output);

	hw_mcsc = (struct fimc_is_hw_mcsc *)hw_ip->priv_info;

	dbg_hw("[%d][OUT:%d]hw_mcsc_dma_output_setting cmd(O:%d,D:%d)\n",
		instance, output_id, output->otf_cmd, output->dma_cmd);

	if (output->dma_cmd == DMA_OUTPUT_COMMAND_DISABLE) {
		fimc_is_scaler_set_dma_out_enable(hw_ip->regs, output_id, false);
		return ret;
	}

	out_width  = output->width;
	out_height = output->height;
	format     = output->dma_format;
	plane      = output->plane;
	order      = output->dma_order;
	bitwidth   = output->dma_bitwidth;

	ret = fimc_is_hw_mcsc_check_format(HW_MCSC_DMA_OUTPUT,
		format, bitwidth, out_width, out_height);
	if (ret) {
		err_hw("[%d][OUT:%d]Invalid MCSC DMA Output format: fmt(%d),bit(%d),size(%dx%d)",
			instance, output_id, format, bitwidth, out_width, out_height);
		return ret;
	}

	ret = fimc_is_hw_mcsc_adjust_output_img_fmt(format, plane, order,
			&img_format, &conv420_en);
	if (ret < 0) {
		warn_hw("[%d][ID:%d] Invalid dma image format\n", instance, hw_ip->id);
		img_format = hw_mcsc->out_img_format[output_id];
		conv420_en = hw_mcsc->conv420_en[output_id];
	} else {
		hw_mcsc->out_img_format[output_id] = img_format;
		hw_mcsc->conv420_en[output_id] = conv420_en;
	}

	fimc_is_scaler_set_wdma_format(hw_ip->regs, output_id, img_format);
	fimc_is_scaler_set_420_conversion(hw_ip->regs, output_id, 0, conv420_en);

	fimc_is_scaler_get_post_dst_size(hw_ip->regs, output_id, &scaled_width, &scaled_height);
	if ((scaled_width != out_width) || (scaled_height != out_height)) {
		dbg_hw("[%d][ID:%d] Invalid output[%d] scaled size (%d/%d)(%d/%d)\n",
			instance, hw_ip->id, output_id, scaled_width, scaled_height,
			out_width, out_height);
		return -EINVAL;
	}
	fimc_is_scaler_set_wdma_size(hw_ip->regs, output_id, out_width, out_height);

	fimc_is_hw_mcsc_adjust_stride(out_width, plane, conv420_en, &y_stride, &uv_stride);
	fimc_is_scaler_set_wdma_stride(hw_ip->regs, output_id, y_stride, uv_stride);

	return ret;
}

int fimc_is_hw_mcsc_output_yuvrange(struct fimc_is_hw_ip *hw_ip, struct param_mcs_output *output,
	u32 output_id, u32 instance)
{
	int ret = 0;
	int yuv_range = 0;
	struct fimc_is_hw_mcsc *hw_mcsc = NULL;
	scaler_setfile_contents contents;

	BUG_ON(!hw_ip);
	BUG_ON(!output);

	hw_mcsc = (struct fimc_is_hw_mcsc *)hw_ip->priv_info;

	if (output->dma_cmd == DMA_OUTPUT_COMMAND_DISABLE) {
		fimc_is_scaler_set_bchs_enable(hw_ip->regs, output_id, 0);
		return ret;
	}

	yuv_range = output->yuv_range;

	fimc_is_scaler_set_bchs_enable(hw_ip->regs, output_id, 1);
	if (test_bit(HW_TUNESET, &hw_ip->state)) {
		/* set yuv range config value by scaler_param yuv_range mode */
		contents = hw_mcsc->setfile->contents[yuv_range];
		fimc_is_scaler_set_b_c(hw_ip->regs, output_id,
			contents.y_offset, contents.y_gain);
		fimc_is_scaler_set_h_s(hw_ip->regs, output_id,
			contents.c_gain00, contents.c_gain01,
			contents.c_gain10, contents.c_gain11);
		dbg_hw("[%d][ID:%d] set YUV range(%d) by setfile parameter\n",
			instance, hw_ip->id, yuv_range);
	} else {
		if (yuv_range == SCALER_OUTPUT_YUV_RANGE_FULL) {
			/* Y range - [0:255], U/V range - [0:255] */
			fimc_is_scaler_set_b_c(hw_ip->regs, output_id, 0, 256);
			fimc_is_scaler_set_h_s(hw_ip->regs, output_id, 256, 0, 0, 256);
		} else if (yuv_range == SCALER_OUTPUT_YUV_RANGE_NARROW) {
			/* Y range - [16:235], U/V range - [16:239] */
			fimc_is_scaler_set_b_c(hw_ip->regs, output_id, 16, 220);
			fimc_is_scaler_set_h_s(hw_ip->regs, output_id, 224, 0, 0, 224);
		}
		dbg_hw("[%d][ID:%d] YUV range set default settings\n", instance,
			hw_ip->id);
	}

	info_hw("[%d][OUT:%d]hw_mcsc_output_yuv_setting: yuv_range(%d), cmd(O:%d,D:%d)\n"
		"[Y:offset(%d),gain(%d)][C:gain00(%d),01(%d),10(%d),11(%d)\n",
		instance, output_id, yuv_range, output->otf_cmd, output->dma_cmd,
		contents.y_offset, contents.y_gain,
		contents.c_gain00, contents.c_gain01,
		contents.c_gain10, contents.c_gain11);

	return ret;
}

int fimc_is_hw_mcsc_adjust_input_img_fmt(u32 format, u32 plane, u32 order, u32 *img_format)
{
	int ret = 0;

	switch (format) {
	case DMA_INPUT_FORMAT_YUV420:
		switch (plane) {
		case 2:
			switch (order) {
			case DMA_INPUT_ORDER_CbCr:
				*img_format = MCSC_YUV420_2P_UFIRST;
				break;
			case DMA_INPUT_ORDER_CrCb:
				* img_format = MCSC_YUV420_2P_VFIRST;
				break;
			default:
				err_hw("input order error - (%d/%d/%d)", format, order, plane);
				ret = -EINVAL;
				break;
			}
			break;
		case 3:
			*img_format = MCSC_YUV420_3P;
			break;
		default:
			err_hw("input plane error - (%d/%d/%d)", format, order, plane);
			ret = -EINVAL;
			break;
		}
		break;
	case DMA_INPUT_FORMAT_YUV422:
		switch (plane) {
		case 1:
			switch (order) {
			case DMA_INPUT_ORDER_CrYCbY:
				*img_format = MCSC_YUV422_1P_VYUY;
				break;
			case DMA_INPUT_ORDER_CbYCrY:
				*img_format = MCSC_YUV422_1P_UYVY;
				break;
			case DMA_INPUT_ORDER_YCrYCb:
				*img_format = MCSC_YUV422_1P_YVYU;
				break;
			case DMA_INPUT_ORDER_YCbYCr:
				*img_format = MCSC_YUV422_1P_YUYV;
				break;
			default:
				err_hw("img order error - (%d/%d/%d)", format, order, plane);
				ret = -EINVAL;
				break;
			}
			break;
		case 2:
			switch (order) {
			case DMA_INPUT_ORDER_CbCr:
				*img_format = MCSC_YUV422_2P_UFIRST;
				break;
			case DMA_INPUT_ORDER_CrCb:
				*img_format = MCSC_YUV422_2P_VFIRST;
				break;
			default:
				err_hw("img order error - (%d/%d/%d)", format, order, plane);
				ret = -EINVAL;
				break;
			}
			break;
		case 3:
			*img_format = MCSC_YUV422_3P;
			break;
		default:
			err_hw("img plane error - (%d/%d/%d)", format, order, plane);
			ret = -EINVAL;
			break;
		}
		break;
	default:
		err_hw("img format error - (%d/%d/%d)", format, order, plane);
		ret = -EINVAL;
		break;
	}

	return ret;
}


int fimc_is_hw_mcsc_adjust_output_img_fmt(u32 format, u32 plane, u32 order, u32 *img_format,
	bool *conv420_flag)
{
	int ret = 0;

	switch (format) {
	case DMA_OUTPUT_FORMAT_YUV420:
		if (conv420_flag)
			*conv420_flag = true;
		switch (plane) {
		case 2:
			switch (order) {
			case DMA_OUTPUT_ORDER_CbCr:
				*img_format = MCSC_YUV420_2P_UFIRST;
				break;
			case DMA_OUTPUT_ORDER_CrCb:
				* img_format = MCSC_YUV420_2P_VFIRST;
				break;
			default:
				err_hw("output order error - (%d/%d/%d)", format, order, plane);
				ret = -EINVAL;
				break;
			}
			break;
		case 3:
			*img_format = MCSC_YUV420_3P;
			break;
		default:
			err_hw("output plane error - (%d/%d/%d)", format, order, plane);
			ret = -EINVAL;
			break;
		}
		break;
	case DMA_OUTPUT_FORMAT_YUV422:
		if (conv420_flag)
			*conv420_flag = false;
		switch (plane) {
		case 1:
			switch (order) {
			case DMA_OUTPUT_ORDER_CrYCbY:
				*img_format = MCSC_YUV422_1P_VYUY;
				break;
			case DMA_OUTPUT_ORDER_CbYCrY:
				*img_format = MCSC_YUV422_1P_UYVY;
				break;
			case DMA_OUTPUT_ORDER_YCrYCb:
				*img_format = MCSC_YUV422_1P_YVYU;
				break;
			case DMA_OUTPUT_ORDER_YCbYCr:
				*img_format = MCSC_YUV422_1P_YUYV;
				break;
			default:
				err_hw("img order error - (%d/%d/%d)", format, order, plane);
				ret = -EINVAL;
				break;
			}
			break;
		case 2:
			switch (order) {
			case DMA_OUTPUT_ORDER_CbCr:
				*img_format = MCSC_YUV422_2P_UFIRST;
				break;
			case DMA_OUTPUT_ORDER_CrCb:
				*img_format = MCSC_YUV422_2P_VFIRST;
				break;
			default:
				err_hw("img order error - (%d/%d/%d)", format, order, plane);
				ret = -EINVAL;
				break;
			}
			break;
		case 3:
			*img_format = MCSC_YUV422_3P;
			break;
		default:
			err_hw("img plane error - (%d/%d/%d)", format, order, plane);
			ret = -EINVAL;
			break;
		}
		break;
	default:
		err_hw("img format error - (%d/%d/%d)", format, order, plane);
		ret = -EINVAL;
		break;
	}

	return ret;
}

void fimc_is_hw_mcsc_adjust_stride(u32 width, u32 plane, bool conv420_flag,
	u32 *y_stride, u32 *uv_stride)
{
	if ((conv420_flag == false) && (plane == 1)) {
		*y_stride = width * 2;
		*uv_stride = width;
	} else {
		*y_stride = width;
		if (plane == 3)
			*uv_stride = width / 2;
		else
			*uv_stride = width;
	}

	*y_stride = 2 * ((*y_stride / 2) + ((*y_stride % 2) > 0));
	*uv_stride = 2 * ((*uv_stride / 2) + ((*uv_stride % 2) > 0));
}

int fimc_is_hw_mcsc_check_format(enum mcsc_io_type type, u32 format, u32 bit_width,
	u32 width, u32 height)
{
	int ret = 0;

	switch (type) {
	case HW_MCSC_OTF_INPUT:
		/* check otf input */
		if (width < 16 || width > 8192) {
			ret = -EINVAL;
			err_hw("Invalid MCSC OTF Input width(%d)", width);
		}

		if (height < 16 || height > 8192) {
			ret = -EINVAL;
			err_hw("Invalid MCSC OTF Input height(%d)", height);
		}

		if (format != OTF_INPUT_FORMAT_YUV422) {
			ret = -EINVAL;
			err_hw("Invalid MCSC OTF Input format(%d)", format);
		}

		if (bit_width != OTF_INPUT_BIT_WIDTH_8BIT) {
			ret = -EINVAL;
			err_hw("Invalid MCSC OTF Input format(%d)", bit_width);
		}
		break;
	case HW_MCSC_OTF_OUTPUT:
		/* check otf output */
		if (width < 16 || width > 8192) {
			ret = -EINVAL;
			err_hw("Invalid MCSC OTF Output width(%d)", width);
		}

		if (height < 16 || height > 8192) {
			ret = -EINVAL;
			err_hw("Invalid MCSC OTF Output height(%d)", height);
		}

		if (format != OTF_OUTPUT_FORMAT_YUV422) {
			ret = -EINVAL;
			err_hw("Invalid MCSC OTF Output format(%d)", format);
		}

		if (bit_width != OTF_OUTPUT_BIT_WIDTH_8BIT) {
			ret = -EINVAL;
			err_hw("Invalid MCSC OTF Output format(%d)", bit_width);
		}
		break;
	case HW_MCSC_DMA_INPUT:
		/* check dma input */
		if (width < 16 || width > 8192) {
			ret = -EINVAL;
			err_hw("Invalid MCSC DMA Input width(%d)", width);
		}

		if (height < 16 || height > 8192) {
			ret = -EINVAL;
			err_hw("Invalid MCSC DMA Input height(%d)", height);
		}

		if (format != DMA_INPUT_FORMAT_YUV422) {
			ret = -EINVAL;
			err_hw("Invalid MCSC DMA Input format(%d)", format);
		}

		if (bit_width != DMA_INPUT_BIT_WIDTH_8BIT) {
			ret = -EINVAL;
			err_hw("Invalid MCSC DMA Input format(%d)", bit_width);
		}
		break;
	case HW_MCSC_DMA_OUTPUT:
		/* check dma output */
		if (width < 16 || width > 8192) {
			ret = -EINVAL;
			err_hw("Invalid MCSC DMA Output width(%d)", width);
		}

		if (height < 16 || height > 8192) {
			ret = -EINVAL;
			err_hw("Invalid MCSC DMA Output height(%d)", height);
		}

		if (!(format == DMA_OUTPUT_FORMAT_YUV422 ||
			format == DMA_OUTPUT_FORMAT_YUV420)) {
			ret = -EINVAL;
			err_hw("Invalid MCSC DMA Output format(%d)", format);
		}

		if (bit_width != DMA_OUTPUT_BIT_WIDTH_8BIT) {
			ret = -EINVAL;
			err_hw("Invalid MCSC DMA Output format(%d)", bit_width);
		}
		break;
	default:
		err_hw("Invalid MCSC type(%d)", type);
		break;
	}

	return ret;
}

void fimc_is_hw_mcsc_size_dump(struct fimc_is_hw_ip *hw_ip)
{
	int i;
	u32 input_src = 0;
	u32 in_w, in_h = 0;
	u32 rdma_w, rdma_h = 0;
	u32 poly_src_w, poly_src_h = 0;
	u32 poly_dst_w, poly_dst_h = 0;
	u32 post_in_w, post_in_h = 0;
	u32 post_out_w, post_out_h = 0;
	u32 wdma_enable = 0;
	u32 wdma_w, wdma_h = 0;
	u32 rdma_y_stride, rdma_uv_stride = 0;
	u32 wdma_y_stride, wdma_uv_stride = 0;

	input_src = fimc_is_scaler_get_input_source(hw_ip->regs, hw_ip->id);
	fimc_is_scaler_get_input_img_size(hw_ip->regs, hw_ip->id, &in_w, &in_h);
	fimc_is_scaler_get_rdma_size(hw_ip->regs, &rdma_w, &rdma_h);
	fimc_is_scaler_get_rdma_stride(hw_ip->regs, &rdma_y_stride, &rdma_uv_stride);

	dbg_hw("[MCSC]=SIZE=====================================\n"
		"[INPUT] SRC:%d(0:OTF, 1:DMA), SIZE:%dx%d\n"
		"[RDMA] SIZE:%dx%d, STRIDE: Y:%d, UV:%d\n",
		input_src, in_w, in_h,
		rdma_w, rdma_h, rdma_y_stride, rdma_uv_stride);

	for (i = MCSC_OUTPUT0; i < MCSC_OUTPUT_MAX; i++) {
		fimc_is_scaler_get_poly_src_size(hw_ip->regs, i, &poly_src_w, &poly_src_h);
		fimc_is_scaler_get_poly_dst_size(hw_ip->regs, i, &poly_dst_w, &poly_dst_h);
		fimc_is_scaler_get_post_img_size(hw_ip->regs, i, &post_in_w, &post_in_h);
		fimc_is_scaler_get_post_dst_size(hw_ip->regs, i, &post_out_w, &post_out_h);
		fimc_is_scaler_get_wdma_size(hw_ip->regs, i, &wdma_w, &wdma_h);
		fimc_is_scaler_get_wdma_stride(hw_ip->regs, i, &wdma_y_stride, &wdma_uv_stride);
		wdma_enable = fimc_is_scaler_get_dma_out_enable(hw_ip->regs, i);

		dbg_hw("[POLY%d] SRC:%dx%d, DST:%dx%d\n"
			"[POST%d] SRC:%dx%d, DST:%dx%d\n"
			"[WDMA%d] ENABLE:%d, SIZE:%dx%d, STRIDE: Y:%d, UV:%d\n",
			i, poly_src_w, poly_src_h, poly_dst_w, poly_dst_h,
			i, post_in_w, post_in_h, post_out_w, post_out_h,
			i, wdma_enable, wdma_w, wdma_h, wdma_y_stride, wdma_uv_stride);
	}
	dbg_hw("[MCSC]==========================================\n");

	return;
}

