/*
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Helper file for Samsung EXYNOS DECON driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/pm_runtime.h>

#include "decon.h"
#include "dsim.h"
#include "decon_helper.h"
#include <video/mipi_display.h>

inline int decon_is_no_bootloader_fb(struct decon_device *decon)
{
#ifdef CONFIG_DECON_USE_BOOTLOADER_FB
	return !decon->bl_fb_info.phy_addr;
#else
	return 1;
#endif
}

inline int decon_clk_set_parent(struct device *dev, struct clk *p, struct clk *c)
{
	clk_set_parent(c, p);
	return 0;
}

int decon_clk_set_rate(struct device *dev, struct clk *clk,
		const char *conid, unsigned long rate)
{
	if (IS_ERR_OR_NULL(clk)) {
		if (IS_ERR_OR_NULL(conid)) {
			decon_err("%s: couldn't set clock(%ld)\n", __func__, rate);
			return -ENODEV;
		}
		clk = clk_get(dev, conid);
		clk_set_rate(clk, rate);
		clk_put(clk);
	} else {
		clk_set_rate(clk, rate);
	}

	return 0;
}

void decon_to_psr_info(struct decon_device *decon, struct decon_psr_info *psr)
{
	psr->psr_mode = decon->pdata->psr_mode;
	psr->trig_mode = decon->pdata->trig_mode;
	psr->out_type = decon->out_type;
}

void decon_to_init_param(struct decon_device *decon, struct decon_init_param *p)
{
	struct decon_lcd *lcd_info = decon->lcd_info;
	struct v4l2_mbus_framefmt mbus_fmt;

	mbus_fmt.width = 0;
	mbus_fmt.height = 0;
	mbus_fmt.code = 0;
	mbus_fmt.field = 0;
	mbus_fmt.colorspace = 0;

	p->lcd_info = lcd_info;
	p->psr.psr_mode = decon->pdata->psr_mode;
	p->psr.trig_mode = decon->pdata->trig_mode;
	p->psr.out_type = decon->out_type;
	p->nr_windows = decon->pdata->max_win;
}

/**
* ----- APIs for DISPLAY_SUBSYSTEM_EVENT_LOG -----
*/
/* ===== STATIC APIs ===== */

#ifdef CONFIG_DECON_EVENT_LOG
/* logging a event related with DECON */
void disp_log_win_config(char *ts, struct seq_file *s,
				struct decon_win_config *config)
{
	int win;
	struct decon_win_config *cfg;
	char *state[] = { "D", "C", "B", "U" };
	char *blending[] = { "N", "P", "C" };
	char *idma[] = { "G0", "G1", "V0", "V1", "VR0", "VR1", "G2" };

	for (win = 0; win < MAX_DECON_WIN; win++) {
		cfg = &config[win];

		if (cfg->state == DECON_WIN_STATE_DISABLED)
			continue;

		if (cfg->state == DECON_WIN_STATE_COLOR) {
			seq_printf(s, "%s\t[%d] %s, C(%d), D(%d,%d,%d,%d) "
				"P(%d)\n",
				ts, win, state[cfg->state], cfg->color,
				cfg->dst.x, cfg->dst.y,
				cfg->dst.x + cfg->dst.w,
				cfg->dst.y + cfg->dst.h,
				cfg->protection);
		} else {
			seq_printf(s, "%s\t[%d] %s,(%d,%d,%d), F(%d) P(%d)"
				" A(%d), %s, %s, f(%d) (%d,%d,%d,%d,%d,%d) ->"
				" (%d,%d,%d,%d,%d,%d)\n",
				ts, win, state[cfg->state], cfg->fd_idma[0],
				cfg->fd_idma[1], cfg->fd_idma[2],
				cfg->fence_fd, cfg->protection,
				cfg->plane_alpha, blending[cfg->blending],
				idma[cfg->idma_type], cfg->format,
				cfg->src.x, cfg->src.y,
				cfg->src.x + cfg->src.w,
				cfg->src.y + cfg->src.h,
				cfg->src.f_w, cfg->src.f_h,
				cfg->dst.x, cfg->dst.y,
				cfg->dst.x + cfg->dst.w,
				cfg->dst.y + cfg->dst.h,
				cfg->dst.f_w, cfg->dst.f_h);
		}
	}
}

void disp_log_update_info(char *ts, struct seq_file *s,
				struct decon_update_reg_data *reg)
{
	int win;

	for (win = 0; win < MAX_DECON_WIN; win++) {
		if (!(reg->wincon[win] & WINCON_ENWIN))
			continue;

		seq_printf(s, "%s\t[%d] U(%d): (%d,%d,%d,%d) -> "
			"(%d,%d,%d,%d)\n",
			ts, win, reg->need_update,
			reg->offset_x[win], reg->offset_y[win],
			reg->whole_w[win], reg->whole_h[win],
			(reg->vidosd_a[win] >> 13) & 0x1fff,
			(reg->vidosd_a[win]) & 0x1fff,
			(reg->vidosd_b[win] >> 13) & 0x1fff,
			(reg->vidosd_b[win]) & 0x1fff);
	}
}

void disp_ss_event_log_win_update(char *ts, struct seq_file *s,
					struct decon_update_reg_data *reg)
{
	disp_log_win_config(ts, s, reg->win_config);
	disp_log_update_info(ts, s, reg);
}

void disp_ss_event_log_win_config(char *ts, struct seq_file *s,
					struct decon_win_config_data *win_cfg)
{
	disp_log_win_config(ts, s, win_cfg->config);
}

static inline void disp_ss_event_log_decon
	(disp_ss_event_t type, struct v4l2_subdev *sd, ktime_t time)
{
	struct decon_device *decon = container_of(sd, struct decon_device, sd);
	int idx = atomic_inc_return(&decon->disp_ss_log_idx) % DISP_EVENT_LOG_MAX;
	struct disp_ss_log *log = &decon->disp_ss_log[idx];

	if (time.tv64)
		log->time = time;
	else
		log->time = ktime_get();
	log->type = type;

	switch (type) {
	case DISP_EVT_DECON_SUSPEND:
	case DISP_EVT_DECON_RESUME:
	case DISP_EVT_ENTER_LPD:
	case DISP_EVT_EXIT_LPD:
		log->data.pm.pm_status = pm_runtime_active(decon->dev);
		log->data.pm.elapsed = ktime_sub(ktime_get(), log->time);
		break;
	case DISP_EVT_TE_INTERRUPT:
	case DISP_EVT_UNDERRUN:
	case DISP_EVT_LINECNT_ZERO:
		break;
	default:
		/* Any remaining types will be log just time and type */
		break;
	}
}

/* logging a event related with DSIM */
static inline void disp_ss_event_log_dsim
	(disp_ss_event_t type, struct v4l2_subdev *sd, ktime_t time)
{
	struct dsim_device *dsim = container_of(sd, struct dsim_device, sd);
	struct decon_device *decon = get_decon_drvdata(dsim->id);
	int idx = atomic_inc_return(&decon->disp_ss_log_idx) % DISP_EVENT_LOG_MAX;
	struct disp_ss_log *log = &decon->disp_ss_log[idx];

	if (time.tv64)
		log->time = time;
	else
		log->time = ktime_get();
	log->type = type;

	switch (type) {
	case DISP_EVT_DSIM_SUSPEND:
	case DISP_EVT_DSIM_RESUME:
	case DISP_EVT_ENTER_ULPS:
	case DISP_EVT_EXIT_ULPS:
		log->data.pm.pm_status = pm_runtime_active(dsim->dev);
		log->data.pm.elapsed = ktime_sub(ktime_get(), log->time);
		break;
	default:
		/* Any remaining types will be log just time and type */
		break;
	}
}

/* If event are happend continuously, then ignore */
static bool disp_ss_event_ignore
	(disp_ss_event_t type, struct decon_device *decon)
{
	int latest = atomic_read(&decon->disp_ss_log_idx) % DISP_EVENT_LOG_MAX;
	struct disp_ss_log *log;
	int idx;

	/* Seek a oldest from current index */
	idx = (latest + DISP_EVENT_LOG_MAX - DECON_ENTER_LPD_CNT) % DISP_EVENT_LOG_MAX;
	do {
		if (++idx >= DISP_EVENT_LOG_MAX)
			idx = 0;

		log = &decon->disp_ss_log[idx];
		if (log->type != type)
			return false;
	} while (latest != idx);

	return true;
}

/* ===== EXTERN APIs ===== */
/* Common API to log a event related with DECON/DSIM */
void DISP_SS_EVENT_LOG(disp_ss_event_t type, struct v4l2_subdev *sd, ktime_t time)
{
	struct decon_device *decon = get_decon_drvdata(0);

	if (!decon || IS_ERR_OR_NULL(decon->debug_event))
		return;

	if (!(decon->disp_ss_log_unmask & type))
		return;

	/* log a eventy softly */
	switch (type) {
	case DISP_EVT_TE_INTERRUPT:
	case DISP_EVT_UNDERRUN:
		/* If occurs continuously, skipped. It is a burden */
		if (disp_ss_event_ignore(type, decon))
			break;
	case DISP_EVT_BLANK:
	case DISP_EVT_UNBLANK:
	case DISP_EVT_ENTER_LPD:
	case DISP_EVT_EXIT_LPD:
	case DISP_EVT_DECON_SUSPEND:
	case DISP_EVT_DECON_RESUME:
	case DISP_EVT_LINECNT_ZERO:
	case DISP_EVT_TRIG_MASK:
	case DISP_EVT_TRIG_UNMASK:
	case DISP_EVT_DECON_FRAMEDONE:
	case DISP_EVT_DECON_FRAMEDONE_WAIT:
	case DISP_EVT_ACT_VSYNC:
	case DISP_EVT_DEACT_VSYNC:
	case DISP_EVT_WIN_CONFIG:
	case DISP_EVT_ACT_PROT:
	case DISP_EVT_DEACT_PROT:
	case DISP_EVT_UPDATE_TIMEOUT:
	case DISP_EVT_VSYNC_TIMEOUT:
	case DISP_EVT_VSTATUS_TIMEOUT:
		disp_ss_event_log_decon(type, sd, time);
		break;
	case DISP_EVT_ENTER_ULPS:
	case DISP_EVT_EXIT_ULPS:
	case DISP_EVT_DSIM_SUSPEND:
	case DISP_EVT_DSIM_RESUME:
	case DISP_EVT_DSIM_FRAMEDONE:
	case DISP_EVT_DSIM_INTR_ENABLE:
	case DISP_EVT_DSIM_INTR_DISABLE:
		disp_ss_event_log_dsim(type, sd, time);
		break;
	default:
		return;
	}

	wake_up_interruptible_all(&decon->event_wait);
}

void DISP_SS_EVENT_LOG_WINCON(struct v4l2_subdev *sd, struct decon_reg_data *regs)
{
	struct decon_device *decon = container_of(sd, struct decon_device, sd);
	int idx = atomic_inc_return(&decon->disp_ss_log_idx) % DISP_EVENT_LOG_MAX;
	struct disp_ss_log *log = &decon->disp_ss_log[idx];
	int win = 0;
	bool window_updated = false;

	log->time = ktime_get();
	log->type = DISP_EVT_UPDATE_HANDLER;

	for (win = 0; win < 3; win++) {
		if (regs->wincon[win] & WINCON_ENWIN) {
			log->data.reg.wincon[win] = regs->wincon[win];
			log->data.reg.offset_x[win] = regs->offset_x[win];
			log->data.reg.offset_y[win] = regs->offset_y[win];
			log->data.reg.whole_w[win] = regs->whole_w[win];
			log->data.reg.whole_h[win] = regs->whole_h[win];
			log->data.reg.vidosd_a[win] = regs->vidosd_a[win];
			log->data.reg.vidosd_b[win] = regs->vidosd_b[win];
			memcpy(&log->data.reg.win_config[win], &regs->win_config[win],
					sizeof(struct decon_win_config));
		} else {
			log->data.reg.win_config[win].state = DECON_WIN_STATE_DISABLED;
		}
	}

#ifdef CONFIG_FB_WINDOW_UPDATE
	if ((regs->need_update) ||
		(decon->need_update && regs->update_win.w)) {
		window_updated = true;
		memcpy(&log->data.reg.win, &regs->update_win,
				sizeof(struct decon_rect));
	}
#endif
	if (!window_updated) {
		log->data.reg.win.x = 0;
		log->data.reg.win.y = 0;
		log->data.reg.win.w = decon->lcd_info->xres;
		log->data.reg.win.h = decon->lcd_info->yres;
	}
}

void DISP_SS_EVENT_LOG_UPDATE_PARAMS(struct v4l2_subdev *sd,
				struct decon_reg_data *regs)
{
	struct decon_device *decon = container_of(sd, struct decon_device, sd);
	int idx = atomic_inc_return(&decon->disp_ss_log_idx) % DISP_EVENT_LOG_MAX;
	struct disp_ss_log *log = &decon->disp_ss_log[idx];
	int win = 0;

	if (!(decon->disp_ss_log_unmask & DISP_EVT_UPDATE_PARAMS))
		return;

	log->time = ktime_get();
	log->type = DISP_EVT_UPDATE_PARAMS;
	log->data.reg.overlap_cnt = regs->win_overlap_cnt;
	log->data.reg.bandwidth = regs->cur_bw;

	for (win = 0; win < MAX_DECON_WIN; win++) {
		log->data.reg.wincon[win] = 0;
		if (regs->wincon[win] & WINCON_ENWIN) {
			log->data.reg.wincon[win] = regs->wincon[win];
			log->data.reg.offset_x[win] = regs->offset_x[win];
			log->data.reg.offset_y[win] = regs->offset_y[win];
			log->data.reg.whole_w[win] = regs->whole_w[win];
			log->data.reg.whole_h[win] = regs->whole_h[win];
			log->data.reg.vidosd_a[win] = regs->vidosd_a[win];
			log->data.reg.vidosd_b[win] = regs->vidosd_b[win];
			memcpy(&log->data.reg.win_config[win], &regs->win_config[win],
				sizeof(struct decon_win_config));
		}
	}

#ifdef CONFIG_FB_WINDOW_UPDATE
	log->data.reg.need_update = regs->need_update;
	if ((regs->need_update) ||
		(decon->need_update && regs->update_win.w)) {
		memcpy(&log->data.reg.win, &regs->update_win,
				sizeof(struct decon_rect));
	} else {
		log->data.reg.win.x = 0;
		log->data.reg.win.y = 0;
		log->data.reg.win.w = decon->lcd_info->xres;
		log->data.reg.win.h = decon->lcd_info->yres;
	}
#else
	memset(&log->data.reg.win, 0, sizeof(struct decon_rect));
#endif
	wake_up_interruptible_all(&decon->event_wait);
}

void DISP_SS_EVENT_LOG_WIN_CONFIG(struct v4l2_subdev *sd, struct decon_win_config_data *win_data)
{
	struct decon_device *decon = container_of(sd, struct decon_device, sd);
	int idx = atomic_inc_return(&decon->disp_ss_log_idx) % DISP_EVENT_LOG_MAX;
	struct disp_ss_log *log = &decon->disp_ss_log[idx];

	if (!(decon->disp_ss_log_unmask & DISP_EVT_WIN_CONFIG_PARAM))
		return;

	log->time = ktime_get();
	log->type = DISP_EVT_WIN_CONFIG_PARAM;

	memcpy(&log->data.win_data, win_data, sizeof(struct decon_win_config_data));

	wake_up_interruptible_all(&decon->event_wait);
}

/* Common API to log a event related with DSIM COMMAND */
void DISP_SS_EVENT_LOG_CMD(struct v4l2_subdev *sd, u32 cmd_id, unsigned long data)
{
	struct dsim_device *dsim = container_of(sd, struct dsim_device, sd);
	struct decon_device *decon = get_decon_drvdata(dsim->id);
	int idx;
	struct disp_ss_log *log;

	if (!decon || IS_ERR_OR_NULL(decon->debug_event))
		return;

	if (!(decon->disp_ss_log_unmask & DISP_EVT_DSIM_COMMAND))
		return;

	idx = atomic_inc_return(&decon->disp_ss_log_idx) % DISP_EVENT_LOG_MAX;
	log = &decon->disp_ss_log[idx];

	log->time = ktime_get();
	log->type = DISP_EVT_DSIM_COMMAND;
	log->data.cmd_buf.id = cmd_id;
	if (cmd_id == MIPI_DSI_DCS_LONG_WRITE)
		log->data.cmd_buf.buf = *(u8 *)(data);
	else
		log->data.cmd_buf.buf = (u8)data;

	wake_up_interruptible_all(&decon->event_wait);
}

void DISP_SS_EVENT_SIZE_ERR_LOG(struct v4l2_subdev *sd, struct disp_ss_size_info *info)
{
	struct dsim_device *dsim = container_of(sd, struct dsim_device, sd);
	struct decon_device *decon = get_decon_drvdata(dsim->id);
	int idx;
	struct disp_ss_log *log;

	if (!decon || IS_ERR_OR_NULL(decon->debug_event))
		return;

	idx = atomic_inc_return(&decon->disp_ss_log_idx) % DISP_EVENT_LOG_MAX;
	log = &decon->disp_ss_log[idx];

	log->time = ktime_get();
	log->type = DISP_EVT_SIZE_ERR;
	memcpy(&log->data.size_mismatch, info, sizeof(struct disp_ss_size_info));

	wake_up_interruptible_all(&decon->event_wait);
}

/* display logged events related with DECON */
void DISP_SS_EVENT_SHOW(struct seq_file *s, struct decon_device *decon,
			int base_idx, bool sync)
{
	int idx = atomic_read(&decon->disp_ss_log_idx) % DISP_EVENT_LOG_MAX;
	struct disp_ss_log *log;
	int latest = idx;
	struct timeval tv;
	char ts[20];

	if (!sync) {
		/* TITLE */
		seq_printf(s, "-------------------DECON EVENT LOGGER ----------------------\n");
		seq_printf(s, "-- STATUS: LPD(%s) ", IS_ENABLED(CONFIG_DECON_LPD_DISPLAY) ? "on" : "off");
		seq_printf(s, "PKTGO(%s) ", IS_ENABLED(CONFIG_DECON_MIPI_DSI_PKTGO) ? "on" : "off");
		seq_printf(s, "BlockMode(%s) ", IS_ENABLED(CONFIG_DECON_BLOCKING_MODE) ? "on" : "off");
		seq_printf(s, "Window_Update(%s)\n", IS_ENABLED(CONFIG_FB_WINDOW_UPDATE) ? "on" : "off");
		seq_printf(s, "-------------------------------------------------------------\n");
		seq_printf(s, "%14s  %20s  %20s\n",
			"Time", "Event ID", "Remarks");
		seq_printf(s, "-------------------------------------------------------------\n");

		if (idx < 0) {
			seq_printf(s, "No Events available. Done.\n");
			seq_printf(s, "-------------------------------------------------------------\n");
			return;
		}
	}

	if (sync) {
		if (base_idx != DEFAULT_BASE_IDX)
			idx = base_idx % DISP_EVENT_LOG_MAX;
	} else {
		/* Seek a oldest from current index */
		idx = (idx + DISP_EVENT_LOG_MAX - DISP_EVENT_PRINT_MAX) % DISP_EVENT_LOG_MAX;
	}

	do {
		if (++idx >= DISP_EVENT_LOG_MAX)
			idx = 0;

		/* Seek a index */
		log = &decon->disp_ss_log[idx];

		/* TIME */
		tv = ktime_to_timeval(log->time);
		sprintf(ts, "[%6ld.%06ld] ", tv.tv_sec, tv.tv_usec);
		seq_printf(s, "%s", ts);

		/* EVETN ID + Information */
		switch (log->type) {
		case DISP_EVT_BLANK:
			seq_printf(s, "%20s  %20s", "FB_BLANK", "-\n");
			break;
		case DISP_EVT_UNBLANK:
			seq_printf(s, "%20s  %20s", "FB_UNBLANK", "-\n");
			break;
		case DISP_EVT_ACT_VSYNC:
			seq_printf(s, "%20s  %20s", "ACT_VSYNC", "-\n");
			break;
		case DISP_EVT_DEACT_VSYNC:
			seq_printf(s, "%20s  %20s", "DEACT_VSYNC", "-\n");
			break;
		case DISP_EVT_WIN_CONFIG:
			seq_printf(s, "%20s  %20s", "WIN_CONFIG", "-\n");
			break;
		case DISP_EVT_WIN_CONFIG_PARAM:
			seq_printf(s, "%20s %d %20s", "WIN_CONFIG_PARAM", log->data.win_data.fd_odma, "-\n");
			disp_ss_event_log_win_config(ts, s, &log->data.win_data);
			break;
		case DISP_EVT_TE_INTERRUPT:
			seq_printf(s, "%20s  %20s", "TE_INTERRUPT", "-\n");
			break;
		case DISP_EVT_UNDERRUN:
			seq_printf(s, "%20s  %20s", "UNDER_RUN", "-\n");
			break;
		case DISP_EVT_DSIM_FRAMEDONE:
			seq_printf(s, "%20s  %20s", "DSIM_FRAME_DONE", "-\n");
			break;
		case DISP_EVT_DECON_FRAMEDONE:
			seq_printf(s, "%20s  %20s", "DECON_FRAME_DONE", "-\n");
			break;
		case DISP_EVT_TRIG_MASK:
			seq_printf(s, "%20s  %20s", "TRIG_MASK", "-\n");
			break;
		case DISP_EVT_TRIG_UNMASK:
			seq_printf(s, "%20s  %20s", "TRIG_UNMASK", "-\n");
			break;
		case DISP_EVT_DECON_FRAMEDONE_WAIT:
			seq_printf(s, "%20s  %20s", "FRAMEDONE_WAIT", "-\n");
			break;
		case DISP_EVT_ACT_PROT:
			seq_printf(s, "%20s  %20s", "PROTECTION_ENABLE", "-\n");
			break;
		case DISP_EVT_DEACT_PROT:
			seq_printf(s, "%20s  %20s", "PROTECTION_DISABLE", "-\n");
			break;
		case DISP_EVT_UPDATE_TIMEOUT:
			seq_printf(s, "%20s  %20s", "UPDATE_TIMEOUT", "-\n");
			break;
		case DISP_EVT_LINECNT_ZERO:
			seq_printf(s, "%20s  %20s", "LINECNT_ZERO", "-\n");
			break;
		case DISP_EVT_UPDATE_HANDLER:
			seq_printf(s, "%20s  ", "UPDATE_HANDLER");
			seq_printf(s, "overlap=%d, bw=0x%x, (%d,%d,%d,%d) U=%d\n",
					log->data.reg.overlap_cnt,
					log->data.reg.bandwidth,
					log->data.reg.win.x,
					log->data.reg.win.y,
					log->data.reg.win.x + log->data.reg.win.w,
					log->data.reg.win.y + log->data.reg.win.h,
					log->data.reg.need_update);
			break;
		case DISP_EVT_UPDATE_PARAMS:
			seq_printf(s, "%20s  ", "UPDATE_PARAMS");
			seq_printf(s, "overlap=%d, bw=0x%x, (%d,%d,%d,%d) U=%d\n",
					log->data.reg.overlap_cnt,
					log->data.reg.bandwidth,
					log->data.reg.win.x,
					log->data.reg.win.y,
					log->data.reg.win.w,
					log->data.reg.win.h,
					log->data.reg.need_update);
			disp_ss_event_log_win_update(ts, s,
						&log->data.reg);
			break;
		case DISP_EVT_DSIM_COMMAND:
			seq_printf(s, "%20s  ", "DSIM_COMMAND");
			seq_printf(s, "id=0x%x, command=0x%x\n",
					log->data.cmd_buf.id,
					log->data.cmd_buf.buf);
			break;
		case DISP_EVT_DECON_SUSPEND:
			seq_printf(s, "%20s  %20s", "DECON_SUSPEND", "-\n");
			break;
		case DISP_EVT_DECON_RESUME:
			seq_printf(s, "%20s  %20s", "DECON_RESUME", "-\n");
			break;
		case DISP_EVT_ENTER_LPD:
			seq_printf(s, "%20s  ", "ENTER_LPD");
			tv = ktime_to_timeval(log->data.pm.elapsed);
			seq_printf(s, "pm=%s, elapsed=[%ld.%03lds]\n",
					log->data.pm.pm_status ? "active ":"suspend",
					tv.tv_sec, tv.tv_usec/1000);
			break;
		case DISP_EVT_EXIT_LPD:
			seq_printf(s, "%20s  ", "EXIT_LPD");
			tv = ktime_to_timeval(log->data.pm.elapsed);
			seq_printf(s, "pm=%s, elapsed=[%ld.%03lds]\n",
					log->data.pm.pm_status ? "active ":"suspend",
					tv.tv_sec, tv.tv_usec/1000);
			break;
		case DISP_EVT_DSIM_SUSPEND:
			seq_printf(s, "%20s  %20s", "DSIM_SUSPEND", "-\n");
			break;
		case DISP_EVT_DSIM_RESUME:
			seq_printf(s, "%20s  %20s", "DSIM_RESUME", "-\n");
			break;
		case DISP_EVT_ENTER_ULPS:
			seq_printf(s, "%20s  ", "ENTER_ULPS");
			tv = ktime_to_timeval(log->data.pm.elapsed);
			seq_printf(s, "pm=%s, elapsed=[%ld.%03lds]\n",
					log->data.pm.pm_status ? "active ":"suspend",
					tv.tv_sec, tv.tv_usec/1000);
			break;
		case DISP_EVT_EXIT_ULPS:
			seq_printf(s, "%20s  ", "EXIT_ULPS");
			tv = ktime_to_timeval(log->data.pm.elapsed);
			seq_printf(s, "pm=%s, elapsed=[%ld.%03lds]\n",
					log->data.pm.pm_status ? "active ":"suspend",
					tv.tv_sec, tv.tv_usec/1000);
			break;
		case DISP_EVT_DSIM_INTR_ENABLE:
			seq_printf(s, "%20s  %20s", "DSIM_INTR_ENABLE", "-\n");
			break;
		case DISP_EVT_DSIM_INTR_DISABLE:
			seq_printf(s, "%20s  %20s", "DSIM_INTR_DISABLE", "-\n");
			break;
		default:
			break;
		}
	} while (latest != idx);

	if (!sync)
		seq_printf(s, "-------------------------------------------------------------\n");

	return;
}

#if defined(CONFIG_DEBUG_LIST)
void DISP_SS_DUMP(u32 type)
{
	struct decon_device *decon = get_decon_drvdata(0);

	if (!decon || decon->disp_dump & BIT(type))
		return;

	switch (type) {
	case DISP_DUMP_DECON_UNDERRUN:
	case DISP_DUMP_LINECNT_ZERO:
	case DISP_DUMP_VSYNC_TIMEOUT:
	case DISP_DUMP_VSTATUS_TIMEOUT:
	case DISP_DUMP_COMMAND_WR_TIMEOUT:
	case DISP_DUMP_COMMAND_RD_ERROR:
		decon_dump(decon);
		BUG();
		break;
	}
}
#endif
#endif
