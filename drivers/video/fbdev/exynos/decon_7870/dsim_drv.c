/* linux/drivers/video/fbdev/exynos/decon_7870/dsim_drv.c
 *
 * Samsung SoC MIPI-DSIM driver.
 *
 * Copyright (c) 2015 Samsung Electronics
 *
 * Jiun Yu <jiun.yu@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/ctype.h>
#include <linux/platform_device.h>
#include <linux/phy/phy.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/memory.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/regulator/consumer.h>
#include <linux/notifier.h>
#include <linux/pm_runtime.h>
#include <linux/lcd.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/suspend.h>
#include <linux/wakeup_reason.h>

#include <video/mipi_display.h>

#include "regs-dsim.h"
#include "dsim.h"
#include "decon.h"
#include "panels/dsim_panel.h"
#include "decon_board.h"

#define MHZ (1000 * 1000)

static DEFINE_MUTEX(dsim_rd_wr_mutex);
static DECLARE_COMPLETION(dsim_ph_wr_comp);
static DECLARE_COMPLETION(dsim_wr_comp);
static DECLARE_COMPLETION(dsim_rd_comp);

static int dsim_runtime_suspend(struct device *dev);
static int dsim_runtime_resume(struct device *dev);

#define MIPI_WR_TIMEOUT msecs_to_jiffies(100)
#define MIPI_RD_TIMEOUT msecs_to_jiffies(100)

#ifdef CONFIG_OF
static const struct of_device_id exynos5_dsim[] = {
	{ .compatible = "samsung, exynos8-mipi-dsi" },
	{},
};
MODULE_DEVICE_TABLE(of, exynos5_dsim);
#endif

struct dsim_device *dsim0_for_decon;
EXPORT_SYMBOL(dsim0_for_decon);

struct dsim_device *dsim1_for_decon;
EXPORT_SYMBOL(dsim1_for_decon);

static void dsim_dump(struct dsim_device *dsim)
{
	dsim_info("=== DSIM SFR DUMP ===\n");

	print_hex_dump(KERN_ERR, "", DUMP_PREFIX_ADDRESS, 32, 4,
			dsim->reg_base, 0xC0, false);
}

static void dsim_long_data_wr(struct dsim_device *dsim, unsigned long data0, unsigned int data1)
{
	unsigned int data_cnt = 0, payload = 0;

	/* in case that data count is more then 4 */
	for (data_cnt = 0; data_cnt < data1; data_cnt += 4) {
		/*
		 * after sending 4bytes per one time,
		 * send remainder data less then 4.
		 */
		if ((data1 - data_cnt) < 4) {
			if ((data1 - data_cnt) == 3) {
				payload = *(u8 *)(data0 + data_cnt) |
				    (*(u8 *)(data0 + (data_cnt + 1))) << 8 |
					(*(u8 *)(data0 + (data_cnt + 2))) << 16;
			dev_dbg(dsim->dev, "count = 3 payload = %x, %x %x %x\n",
				payload, *(u8 *)(data0 + data_cnt),
				*(u8 *)(data0 + (data_cnt + 1)),
				*(u8 *)(data0 + (data_cnt + 2)));
			} else if ((data1 - data_cnt) == 2) {
				payload = *(u8 *)(data0 + data_cnt) |
					(*(u8 *)(data0 + (data_cnt + 1))) << 8;
			dev_dbg(dsim->dev,
				"count = 2 payload = %x, %x %x\n", payload,
				*(u8 *)(data0 + data_cnt),
				*(u8 *)(data0 + (data_cnt + 1)));
			} else if ((data1 - data_cnt) == 1) {
				payload = *(u8 *)(data0 + data_cnt);
			}

			dsim_reg_wr_tx_payload(dsim->id, payload);
		/* send 4bytes per one time. */
		} else {
			payload = *(u8 *)(data0 + data_cnt) |
				(*(u8 *)(data0 + (data_cnt + 1))) << 8 |
				(*(u8 *)(data0 + (data_cnt + 2))) << 16 |
				(*(u8 *)(data0 + (data_cnt + 3))) << 24;

			dev_dbg(dsim->dev,
				"count = 4 payload = %x, %x %x %x %x\n",
				payload, *(u8 *)(data0 + data_cnt),
				*(u8 *)(data0 + (data_cnt + 1)),
				*(u8 *)(data0 + (data_cnt + 2)),
				*(u8 *)(data0 + (data_cnt + 3)));

			dsim_reg_wr_tx_payload(dsim->id, payload);
		}
	}
}

static int dsim_wait_for_cmd_fifo_empty(struct dsim_device *dsim, int id)
{
	switch (id) {
	case 0: /* SFR_PH_FIFO */
		if (!wait_for_completion_timeout(&dsim_ph_wr_comp, MIPI_WR_TIMEOUT)) {
			if (dsim_reg_header_fifo_is_empty(dsim->id)) {
				reinit_completion(&dsim_ph_wr_comp);
				dsim_reg_clear_int(dsim->id, DSIM_INTSRC_SFR_PH_FIFO_EMPTY);
				return 0;
			}
			return -ETIMEDOUT;
		}
		break;
	case 1: /* SFR_PL_FIFO */
		if (!wait_for_completion_timeout(&dsim_wr_comp,
			MIPI_WR_TIMEOUT)) {
			if (dsim_read_mask(dsim->id, DSIM_INTSRC, DSIM_INTSRC_SFR_PL_FIFO_EMPTY)) {
				reinit_completion(&dsim_wr_comp);
				dsim_reg_clear_int(dsim->id, DSIM_INTSRC_SFR_PL_FIFO_EMPTY);
				return 0;
			}
			return -ETIMEDOUT;
		}
		break;
	default:
		break;
	}
	return 0;
}

int dsim_write_data(struct dsim_device *dsim, unsigned int data_id,
	unsigned long data0, unsigned int data1)
{
	int ret = 0;
	struct decon_device *decon = (struct decon_device *)dsim->decon;

	/* LPD related: Decon must be enabled for PACKET_GO mode */
	decon_lpd_block_exit(decon);

	mutex_lock(&dsim_rd_wr_mutex);
	if (dsim->state != DSIM_STATE_HSCLKEN) {
		dev_err(dsim->dev, "DSIM is not ready. state(%d)\n", dsim->state);
		ret = -EINVAL;
		goto err_exit;
	}
	DISP_SS_EVENT_LOG_CMD(&dsim->sd, data_id, data0);

	switch (data_id) {
	/* short packet types of packet types for command. */
	case MIPI_DSI_GENERIC_SHORT_WRITE_0_PARAM:
	case MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM:
	case MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM:
	case MIPI_DSI_DCS_SHORT_WRITE:
	case MIPI_DSI_DCS_SHORT_WRITE_PARAM:
	case MIPI_DSI_SET_MAXIMUM_RETURN_PACKET_SIZE:
		reinit_completion(&dsim_ph_wr_comp);
		dsim_reg_clear_int(dsim->id, DSIM_INTSRC_SFR_PH_FIFO_EMPTY);
		dsim_reg_wr_tx_header(dsim->id, data_id, data0, data1);
		if (dsim_wait_for_cmd_fifo_empty(dsim, 0)) {
			dev_err(dsim->dev, "%4d: ID: %2X: MIPI DSIM short packet write Timeout! 0x%02lx\n",
						__LINE__, data_id, data0);
			ret = -ETIMEDOUT;
			goto exit;
		}
		break;

	/* general command */
	case MIPI_DSI_COLOR_MODE_OFF:
	case MIPI_DSI_COLOR_MODE_ON:
	case MIPI_DSI_SHUTDOWN_PERIPHERAL:
	case MIPI_DSI_TURN_ON_PERIPHERAL:
		reinit_completion(&dsim_ph_wr_comp);
		dsim_reg_clear_int(dsim->id, DSIM_INTSRC_SFR_PH_FIFO_EMPTY);
		dsim_reg_wr_tx_header(dsim->id, data_id, data0, data1);
		if (dsim_wait_for_cmd_fifo_empty(dsim, 0)) {
			dev_err(dsim->dev, "%4d: ID: %2X: MIPI DSIM short packet write Timeout! 0x%02lx\n",
						__LINE__, data_id, data0);
			ret = -ETIMEDOUT;
			goto exit;
		}
		break;

	/* packet types for video data */
	case MIPI_DSI_V_SYNC_START:
	case MIPI_DSI_V_SYNC_END:
	case MIPI_DSI_H_SYNC_START:
	case MIPI_DSI_H_SYNC_END:
	case MIPI_DSI_END_OF_TRANSMISSION:
		break;

	/* short and response packet types for command */
	case MIPI_DSI_GENERIC_READ_REQUEST_0_PARAM:
	case MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM:
	case MIPI_DSI_GENERIC_READ_REQUEST_2_PARAM:
	case MIPI_DSI_DCS_READ:
		reinit_completion(&dsim_ph_wr_comp);
		dsim_reg_clear_int(dsim->id, DSIM_INTSRC_SFR_PH_FIFO_EMPTY);
		dsim_reg_wr_tx_header(dsim->id, data_id, data0, data1);
		if (dsim_wait_for_cmd_fifo_empty(dsim, 0)) {
			dev_err(dsim->dev, "%4d: ID: %2X: MIPI DSIM short packet write Timeout! 0x%02lx\n",
						__LINE__, data_id, data0);
			ret = -ETIMEDOUT;
			goto exit;
		}
		break;

	/* long packet type and null packet */
	case MIPI_DSI_NULL_PACKET:
	case MIPI_DSI_BLANKING_PACKET:
		break;

	case MIPI_DSI_GENERIC_LONG_WRITE:
	case MIPI_DSI_DCS_LONG_WRITE:
	{
		unsigned int size;

		size = data1 * 4;
		reinit_completion(&dsim_wr_comp);
		reinit_completion(&dsim_ph_wr_comp);
		dsim_reg_clear_int(dsim->id, DSIM_INTSRC_SFR_PL_FIFO_EMPTY |
						DSIM_INTSRC_SFR_PH_FIFO_EMPTY);
		/* if data count is less then 4, then send 3bytes data.  */
		if (data1 < 4) {
			unsigned int payload = 0;
			payload = *(u8 *)(data0) |
				*(u8 *)(data0 + 1) << 8 |
				*(u8 *)(data0 + 2) << 16;

			dsim_reg_wr_tx_payload(dsim->id, payload);

			dev_dbg(dsim->dev, "count = %d payload = %x,%x %x %x\n",
				data1, payload,
				*(u8 *)(data0),
				*(u8 *)(data0 + 1),
				*(u8 *)(data0 + 2));
		/* in case that data count is more then 4 */
		} else
			dsim_long_data_wr(dsim, data0, data1);

		/* put data into header fifo */
		dsim_reg_wr_tx_header(dsim->id, data_id, data1 & 0xff, (data1 & 0xff00) >> 8);
		if (dsim_wait_for_cmd_fifo_empty(dsim, 1)) {
			dev_err(dsim->dev, "%4d: ID: %2X: MIPI DSIM write Timeout! 0x%02x\n",
						__LINE__, data_id, *(u8 *)data0);
			ret = -ETIMEDOUT;
			goto exit;
		}
		if (dsim_wait_for_cmd_fifo_empty(dsim, 0)) {
			dev_err(dsim->dev, "%4d: ID: %2X: MIPI DSIM short packet write Timeout! 0x%02x\n",
						__LINE__, data_id, *(u8 *)data0);
			ret = -ETIMEDOUT;
			goto exit;
		}
		break;
	}

	/* packet typo for video data */
	case MIPI_DSI_PACKED_PIXEL_STREAM_16:
	case MIPI_DSI_PACKED_PIXEL_STREAM_18:
	case MIPI_DSI_PIXEL_STREAM_3BYTE_18:
	case MIPI_DSI_PACKED_PIXEL_STREAM_24:
		break;
	default:
		dev_warn(dsim->dev, "data id %x is not supported current DSI spec.\n", data_id);
		ret = -EINVAL;
		goto exit;
	}

exit:
	if ((dsim->state == DSIM_STATE_HSCLKEN) && (ret == -ETIMEDOUT)) {
		dev_err(dsim->dev, "0x%08X, 0x%08X, 0x%08X, 0x%08X\n",
				readl(dsim->reg_base + DSIM_DPHY_STATUS),
				readl(dsim->reg_base + DSIM_INTSRC),
				readl(dsim->reg_base + DSIM_FIFOCTRL),
				readl(dsim->reg_base + DSIM_CMD_CONFIG));
		dsim_reg_set_fifo_ctrl(dsim->id, DSIM_FIFOCTRL_INIT_SFR);
		DISP_SS_DUMP(DISP_DUMP_COMMAND_WR_TIMEOUT);
	}

err_exit:
	mutex_unlock(&dsim_rd_wr_mutex);
	decon_lpd_unblock(decon);
	return ret;
}


#ifdef CONFIG_FB_WINDOW_UPDATE
static int dsim_partial_area_command(struct dsim_device *dsim, void *arg)
{
	struct panel_private *priv = &dsim->priv;
	struct decon_win_rect *win_rect = (struct decon_win_rect *)arg;
	char data_2a[5];
	char data_2b[5];
	int retry;

	if (priv->lcdConnected == PANEL_DISCONNEDTED)
		return 0;

	/* w is right & h is bottom */
	data_2a[0] = MIPI_DCS_SET_COLUMN_ADDRESS;
	data_2a[1] = (win_rect->x >> 8) & 0xff;
	data_2a[2] = win_rect->x & 0xff;
	data_2a[3] = (win_rect->w >> 8) & 0xff;
	data_2a[4] = win_rect->w & 0xff;

	data_2b[0] = MIPI_DCS_SET_PAGE_ADDRESS;
	data_2b[1] = (win_rect->y >> 8) & 0xff;
	data_2b[2] = win_rect->y & 0xff;
	data_2b[3] = (win_rect->h >> 8) & 0xff;
	data_2b[4] = win_rect->h & 0xff;
	retry = 2;
	while (dsim_write_data(dsim, MIPI_DSI_DCS_LONG_WRITE,
				(unsigned long)data_2a, ARRAY_SIZE(data_2a)) != 0) {
		dsim_info("%s: fail to write window update size a.\n", __func__);
		if (--retry <= 0) {
			dsim_err("%s: size-a:failed: exceed retry count\n", __func__);
			return -1;
		}
	}

	retry = 2;
	while (dsim_write_data(dsim, MIPI_DSI_DCS_LONG_WRITE,
				(unsigned long)data_2b, ARRAY_SIZE(data_2b)) != 0) {
		dsim_info("%s: fail to write window update size b.\n", __func__);
		if (--retry <= 0) {
			dsim_err("%s: size-b:failed: exceed retry count\n", __func__);
			return -1;
		}
	}

	return 0;
}

static void dsim_set_lcd_full_screen(struct dsim_device *dsim)
{
	struct decon_win_rect win_rect;

	win_rect.x = 0;
	win_rect.y = 0;
	win_rect.w = dsim->lcd_info.xres - 1;
	win_rect.h = dsim->lcd_info.yres - 1;
	dsim_partial_area_command(dsim, (void *)(&win_rect));
}
#else
static void dsim_set_lcd_full_screen(struct dsim_device *dsim)
{
}
#endif

int dsim_read_data(struct dsim_device *dsim, u32 data_id,
	 u32 addr, u32 count, u8 *buf)
{
	u32 rx_fifo, rx_size = 0;
	int i, j, ret = 0;
	struct decon_device *decon = (struct decon_device *)dsim->decon;
	u32 rx_fifo_depth = DSIM_RX_FIFO_MAX_DEPTH;

	/* LPD related: Decon must be enabled for PACKET_GO mode */
	decon_lpd_block_exit(decon);

	if (dsim->state != DSIM_STATE_HSCLKEN) {
		dev_err(dsim->dev, "DSIM is not ready. state(%d)\n", dsim->state);
		decon_lpd_unblock(decon);
		return -EINVAL;
	}

	reinit_completion(&dsim_rd_comp);

	/* Init RX FIFO before read and clear DSIM_INTSRC */
	dsim_reg_set_fifo_ctrl(dsim->id, DSIM_FIFOCTRL_INIT_RX);
	dsim_reg_clear_int(dsim->id, DSIM_INTSRC_RX_DATA_DONE);

	/* Set the maximum packet size returned */
	dsim_write_data(dsim,
		MIPI_DSI_SET_MAXIMUM_RETURN_PACKET_SIZE, count, 0);

	/* Read request */
	dsim_write_data(dsim, data_id, addr, 0);
	if (!wait_for_completion_timeout(&dsim_rd_comp,
		MIPI_RD_TIMEOUT)) {
		dev_err(dsim->dev, "MIPI DSIM read Timeout! %2X, %2X, %d\n", data_id, addr, count);
		DISP_SS_DUMP(DISP_DUMP_COMMAND_RD_ERROR);
		return -ETIMEDOUT;
	}

	mutex_lock(&dsim_rd_wr_mutex);
	DISP_SS_EVENT_LOG_CMD(&dsim->sd, data_id, (char)addr);

	do {
		rx_fifo = dsim_reg_get_rx_fifo(dsim->id);

		/* Parse the RX packet data types */
		switch (rx_fifo & 0xff) {
		case MIPI_DSI_RX_ACKNOWLEDGE_AND_ERROR_REPORT:
			ret = dsim_reg_rx_err_handler(dsim->id, rx_fifo);
			if (ret < 0) {
				goto exit;
			}
			break;
		case MIPI_DSI_RX_END_OF_TRANSMISSION:
			dev_dbg(dsim->dev, "EoTp was received from LCD module.\n");
			break;
		case MIPI_DSI_RX_DCS_SHORT_READ_RESPONSE_1BYTE:
		case MIPI_DSI_RX_DCS_SHORT_READ_RESPONSE_2BYTE:
		case MIPI_DSI_RX_GENERIC_SHORT_READ_RESPONSE_1BYTE:
		case MIPI_DSI_RX_GENERIC_SHORT_READ_RESPONSE_2BYTE:
			dev_dbg(dsim->dev, "Short Packet was received from LCD module.\n");
			for (i = 0; i < count; i++)
				buf[i] = (rx_fifo >> (8 + i * 8)) & 0xff;
			rx_size = count;
			break;
		case MIPI_DSI_RX_DCS_LONG_READ_RESPONSE:
		case MIPI_DSI_RX_GENERIC_LONG_READ_RESPONSE:
			dev_dbg(dsim->dev, "Long Packet was received from LCD module.\n");
			rx_size = (rx_fifo & 0x00ffff00) >> 8;
			dev_dbg(dsim->dev, "rx fifo : %8x, response : %x, rx_size : %d\n",
					rx_fifo, rx_fifo & 0xff, rx_size);
			if (rx_size > count) {
				dev_err(dsim->dev, "rx size is invalid, rx_size: %d, count: %d\n", rx_size, count);
				rx_size = count;
			}
			/* Read data from RX packet payload */
			for (i = 0; i < rx_size >> 2; i++) {
				rx_fifo = dsim_reg_get_rx_fifo(dsim->id);
				for (j = 0; j < 4; j++)
					buf[(i*4)+j] = (u8)(rx_fifo >> (j * 8)) & 0xff;
			}
			if (rx_size % 4) {
				rx_fifo = dsim_reg_get_rx_fifo(dsim->id);
				for (j = 0; j < rx_size % 4; j++)
					buf[4 * i + j] =
						(u8)(rx_fifo >> (j * 8)) & 0xff;
			}
			break;
		default:
			dev_err(dsim->dev, "Packet format is invaild. %x\n", rx_fifo);
			goto rx_error;
		}
	} while (!dsim_reg_rx_fifo_is_empty(dsim->id) && --rx_fifo_depth);

	rx_fifo = readl(dsim->reg_base + DSIM_RXFIFO);
	ret = rx_size;
	goto exit;

rx_error:
	dsim_reg_force_dphy_stop_state(dsim->id, 1);
	usleep_range(3000, 4000);
	dsim_reg_force_dphy_stop_state(dsim->id, 0);
	ret = -EPERM;
	goto exit;

exit:
	mutex_unlock(&dsim_rd_wr_mutex);
	decon_lpd_unblock(decon);

	return ret;
}

#ifdef CONFIG_DECON_MIPI_DSI_PKTGO
void dsim_pkt_go_ready(struct dsim_device *dsim)
{
	if (dsim->pktgo != DSIM_PKTGO_ENABLED)
		return;

	dsim_reg_set_pkt_go_ready(dsim->id);
}

void dsim_pkt_go_enable(struct dsim_device *dsim, bool enable)
{
	struct decon_device *decon = (struct decon_device *)dsim->decon;

	decon_lpd_block(decon);

	if (dsim->state != DSIM_STATE_HSCLKEN)
		goto end;

	if (enable) {
		if (likely(dsim->pktgo == DSIM_PKTGO_ENABLED))
			goto end;

		dsim_reg_set_pkt_go_cnt(dsim->id, 0xff);
		dsim_reg_set_pkt_go_enable(dsim->id, true);
		dsim->pktgo = DSIM_PKTGO_ENABLED;
		dev_dbg(dsim->dev, "%s: DSIM_PKTGO_ENABLED", __func__);
	} else {
		if (unlikely(dsim->pktgo != DSIM_PKTGO_ENABLED))
			goto end;

		dsim_reg_set_pkt_go_cnt(dsim->id, 0x1); /* Do not use 0x0 */
		dsim_reg_set_pkt_go_enable(dsim->id, false);
		dsim->pktgo = DSIM_PKTGO_DISABLED;

		dev_dbg(dsim->dev, "%s: DSIM_PKTGO_DISABLED", __func__);
	}
end:
	decon_lpd_unblock(decon);
}

#endif

static void dsim_write_test(struct dsim_device *dsim)
{
	int ret = 0, re_try = 4, i = 0;
	u32 test_data[2] = {0xaa, 0xaa};

	for (i = 0; i < re_try; i++) {
		ret = dsim_write_data(dsim, MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM,
				test_data[0], test_data[1]);
		if (ret < 0)
			dsim_dbg("%s: Failed to write test data!\n",
				(dsim->pktgo == DSIM_PKTGO_ENABLED) ? "PKT-GO mode" : "Non PKT-GO mode");
		else
			dsim_dbg("%s: Succeeded to write test data!\n",
				(dsim->pktgo == DSIM_PKTGO_ENABLED) ? "PKT-GO mode" : "Non PKT-GO mode");
	}
}

static void dsim_read_test(struct dsim_device *dsim)
{
	int ret = 0;
	int count = 3;
	u8 buf[4];
	u32 rd_addr = 0x04;

	ret = dsim_read_data(dsim, MIPI_DSI_DCS_READ, rd_addr, count, buf);

	if (ret < 0)
		dsim_dbg("%s: Failed to read test data!\n",
				(dsim->pktgo == DSIM_PKTGO_ENABLED) ? "PKT-GO mode" : "Non PKT-GO mode");
	else
		dsim_dbg("%s: Succeeded to read test data! test data[0] = %#x, test data[1] = %#x, test data[2] = %#x\n",
				((dsim->pktgo == DSIM_PKTGO_ENABLED) ? "PKT-GO mode" : "Non PKT-GO mode"), buf[0], buf[1], buf[3]);
}

static irqreturn_t dsim_interrupt_handler(int irq, void *dev_id)
{
	unsigned int int_src;
	struct dsim_device *dsim = dev_id;
	int active;

	spin_lock(&dsim->slock);

	active = pm_runtime_active(dsim->dev);
	if (!active) {
		dev_warn(dsim->dev, "dsim power is off(%d), state(%d)\n", active, dsim->state);
		spin_unlock(&dsim->slock);
		return IRQ_HANDLED;
	}

	int_src = readl(dsim->reg_base + DSIM_INTSRC);

	/* Test bit */
	if (int_src & DSIM_INTSRC_SFR_PL_FIFO_EMPTY)
		complete(&dsim_wr_comp);
	if (int_src & DSIM_INTSRC_SFR_PH_FIFO_EMPTY)
		complete(&dsim_ph_wr_comp);
	if (int_src & DSIM_INTSRC_RX_DATA_DONE)
		complete(&dsim_rd_comp);
	if (int_src & DSIM_INTSRC_FRAME_DONE)
		DISP_SS_EVENT_LOG(DISP_EVT_DSIM_FRAMEDONE, &dsim->sd, ktime_set(0, 0));
	if (int_src & DSIM_INTSRC_ERR_RX_ECC)
		dev_err(dsim->dev, "RX ECC Multibit error was detected!\n");
	dsim_reg_clear_int(dsim->id, int_src);

	spin_unlock(&dsim->slock);

	return IRQ_HANDLED;
}

static void dsim_clocks_info(struct dsim_device *dsim)
{
	dsim_info("%s: %ld Mhz\n", __clk_get_name(dsim->res.pclk),
				clk_get_rate(dsim->res.pclk) / MHZ);
	dsim_info("%s: %ld Mhz\n", __clk_get_name(dsim->res.dphy_esc),
				clk_get_rate(dsim->res.dphy_esc) / MHZ);
	dsim_info("%s: %ld Mhz\n", __clk_get_name(dsim->res.dphy_byte),
				clk_get_rate(dsim->res.dphy_byte) / MHZ);
}

static int dsim_get_clocks(struct dsim_device *dsim)
{
	struct device *dev = dsim->dev;

	dsim->res.pclk = clk_get(dev, "dsim_pclk");
	if (IS_ERR_OR_NULL(dsim->res.pclk)) {
		dsim_err("failed to get dsim_pclk\n");
		return -ENODEV;
	}

	dsim->res.dphy_esc = clk_get(dev, "dphy_escclk");
	if (IS_ERR_OR_NULL(dsim->res.dphy_esc)) {
		dsim_err("failed to get dphy_escclk\n");
		return -ENODEV;
	}

	dsim->res.dphy_byte = clk_get(dev, "dphy_byteclk");
	if (IS_ERR_OR_NULL(dsim->res.dphy_byte)) {
		dsim_err("failed to get dphy_byteclk\n");
		return -ENODEV;
	}

	return 0;
}

static void dsim_put_clocks(struct dsim_device *dsim)
{
	clk_put(dsim->res.pclk);
	clk_put(dsim->res.dphy_esc);
	clk_put(dsim->res.dphy_byte);
}

static int dsim_get_gpios(struct dsim_device *dsim)
{
	struct device *dev = dsim->dev;
	struct dsim_resources *res = &dsim->res;

	dsim_dbg("%s +\n", __func__);

	if (of_get_property(dev->of_node, "gpios", NULL) != NULL)  {
		/* panel reset */
		res->lcd_reset = of_get_gpio(dev->of_node, 0);
		if (res->lcd_reset < 0) {
			dsim_err("failed to get lcd reset GPIO");
			return -ENODEV;
		}
		res->lcd_power[0] = of_get_gpio(dev->of_node, 1);
		if (res->lcd_power[0] < 0) {
			res->lcd_power[0] = -1;
			dsim_info("This board doesn't support LCD power GPIO");
		}
		res->lcd_power[1] = of_get_gpio(dev->of_node, 2);
		if (res->lcd_power[1] < 0) {
			res->lcd_power[1] = -1;
			dsim_dbg("This board doesn't support 2nd LCD power GPIO");
		}
	}

	dsim_dbg("%s -\n", __func__);
	return 0;
}

static int dsim_reset_panel(struct dsim_device *dsim)
{
	dsim_dbg("%s +\n", __func__);

	run_list(dsim->dev, __func__);

	dsim_dbg("%s -\n", __func__);
	return 0;
}

int dsim_set_panel_pre_power(struct dsim_device *dsim)
{
	dsim_dbg("%s +\n", __func__);

	run_list(dsim->dev, __func__);

	dsim_dbg("%s -\n", __func__);

	return 0;
}

#ifdef CONFIG_EXYNOS_MIPI_DSI_ENABLE_EARLY
static int dsim_reset_panel_wait(struct dsim_device *dsim)
{
	run_list(dsim->dev, __func__);

	return 0;
}
#endif

int dsim_set_panel_power(struct dsim_device *dsim, bool on)
{
	dsim_dbg("%s(%d) +\n", __func__, on);

	if (on)
		run_list(dsim->dev, "dsim_set_panel_power_enable");
	else
		run_list(dsim->dev, "dsim_set_panel_power_disable");

	dsim_dbg("%s(%d) -\n", __func__, on);

	return 0;
}

static int dsim_enable(struct dsim_device *dsim)
{
	if (dsim->state == DSIM_STATE_HSCLKEN) {
#ifdef CONFIG_LCD_DOZE_MODE
		if (IS_DOZE(dsim->doze_state)) {
			call_panel_ops(dsim, exitalpm, dsim);
		}
#endif
		goto exit;
	}

	/* Panel power on early*/
	dsim_set_panel_pre_power(dsim);

	call_panel_ops(dsim, prepare, dsim);

#ifdef CONFIG_EXYNOS_MIPI_DSI_ENABLE_EARLY
	if (dsim->enable_early == DSIM_ENABLE_EARLY_DONE) {
		dsim_info("%s: LP11 reset is already done. Jump to enable_hs_clk\n", __func__);
		goto enable_hs_clk;
	}
#endif

	dsim_info("%s: ++\n", __func__);

#if defined(CONFIG_PM_RUNTIME)
	pm_runtime_get_sync(dsim->dev);
#else
	dsim_runtime_resume(dsim->dev);
#endif

	/* Panel power on */
#ifdef CONFIG_LCD_DOZE_MODE
	if (IS_DOZE(dsim->doze_state)) {
		dsim_info("%s: exit doze\n", __func__);
	} else {
		dsim_set_panel_power(dsim, 1);
	}
#else
	dsim_set_panel_power(dsim, 1);
#endif

	call_panel_ops(dsim, resume, dsim);

	/* DPHY power on */
	phy_power_on(dsim->phy);

	dsim_reg_set_clocks(dsim->id, &dsim->clks_param.clks, &dsim->lcd_info.dphy_pms, 1);
	dsim_reg_set_lanes(dsim->id, DSIM_LANE_CLOCK | dsim->data_lane, 1);
	dsim_reg_init(dsim->id, &dsim->lcd_info, dsim->data_lane_cnt,
			&dsim->clks_param.clks);

#ifdef CONFIG_LCD_DOZE_MODE
	if (IS_DOZE(dsim->doze_state)) {
		dsim_info("%s: exit doze\n", __func__);
	} else {
		dsim_reset_panel(dsim);
	}
#else
	dsim_reset_panel(dsim);
#endif

#ifdef CONFIG_EXYNOS_MIPI_DSI_ENABLE_EARLY
	if (dsim->enable_early == DSIM_ENABLE_EARLY_REQUEST) {
		dsim->enable_early = DSIM_ENABLE_EARLY_DONE;
		dsim_info("%s: LP11 reset is done\n", __func__);
		goto exit;
	}

enable_hs_clk:
	dsim_reset_panel_wait(dsim);
#endif
	dsim_reg_start(dsim->id, &dsim->clks_param.clks, DSIM_LANE_CLOCK | dsim->data_lane);

	dsim->state = DSIM_STATE_HSCLKEN;
#ifdef CONFIG_EXYNOS_MIPI_DSI_ENABLE_EARLY
	dsim->enable_early = DSIM_ENABLE_EARLY_NORMAL;
#endif

	enable_irq(dsim->irq);

#ifdef CONFIG_LCD_DOZE_MODE
	if (IS_DOZE(dsim->doze_state)) {
		call_panel_ops(dsim, exitalpm, dsim);
	} else {
		call_panel_ops(dsim, displayon, dsim);
	}
#else
	call_panel_ops(dsim, displayon, dsim);
#endif

exit:
#ifdef CONFIG_LCD_DOZE_MODE
	dsim->doze_state = DOZE_STATE_NORMAL;
#endif
	dsim_info("%s: --\n", __func__);
	return 0;
}

static int dsim_disable(struct dsim_device *dsim)
{
#ifdef CONFIG_EXYNOS_MIPI_DSI_ENABLE_EARLY
	if (dsim->enable_early == DSIM_ENABLE_EARLY_DONE) {
		dsim_info("%s: Jump to disable_early\n", __func__);
		dsim->enable_early = DSIM_ENABLE_EARLY_NORMAL;
		goto disable_early;
	}
#endif

	if (dsim->state == DSIM_STATE_SUSPEND)
		goto exit;

	dsim_info("%s: ++\n", __func__);

#ifdef CONFIG_DECON_MIPI_DSI_PKTGO
	dsim_pkt_go_enable(dsim, false);
#endif
	dsim_set_lcd_full_screen(dsim);
	call_panel_ops(dsim, suspend, dsim);

#ifdef CONFIG_LCD_DOZE_MODE
	dsim->doze_state = DOZE_STATE_SUSPEND;
#endif

	/* Wait for current read & write CMDs. */
	mutex_lock(&dsim_rd_wr_mutex);
	dsim->state = DSIM_STATE_SUSPEND;
	mutex_unlock(&dsim_rd_wr_mutex);

	disable_irq(dsim->irq);

#ifdef CONFIG_EXYNOS_MIPI_DSI_ENABLE_EARLY
disable_early:
#endif
	dsim_reg_stop(dsim->id, DSIM_LANE_CLOCK | dsim->data_lane);

	phy_power_off(dsim->phy);
	dsim_set_panel_power(dsim, 0);

#if defined(CONFIG_PM_RUNTIME)
	pm_runtime_put_sync(dsim->dev);
#else
	dsim_runtime_suspend(dsim->dev);
#endif

exit:
	dsim_info("%s: --\n", __func__);

	return 0;
}

#ifdef CONFIG_LCD_DOZE_MODE
static int dsim_doze_enable(struct dsim_device *dsim)
{
	if (dsim->state == DSIM_STATE_HSCLKEN) {
		if (dsim->doze_state != DOZE_STATE_DOZE) {
			call_panel_ops(dsim, enteralpm, dsim);
		}
		goto exit;
	}

	dsim_info("%s: ++ %d, %d\n", __func__, dsim->state, dsim->doze_state);

#if defined(CONFIG_PM_RUNTIME)
	pm_runtime_get_sync(dsim->dev);
#else
	dsim_runtime_resume(dsim->dev);
#endif

	if (dsim->doze_state == DOZE_STATE_SUSPEND)
		dsim_set_panel_power(dsim, 1);

	/* DPHY power on */
	phy_power_on(dsim->phy);

	dsim_reg_set_clocks(dsim->id, &dsim->clks_param.clks, &dsim->lcd_info.dphy_pms, 1);
	dsim_reg_set_lanes(dsim->id, DSIM_LANE_CLOCK | dsim->data_lane, 1);
	dsim_reg_init(dsim->id, &dsim->lcd_info, dsim->data_lane_cnt,
			&dsim->clks_param.clks);

	if (dsim->doze_state == DOZE_STATE_SUSPEND)
		dsim_reset_panel(dsim);

	dsim_reg_start(dsim->id, &dsim->clks_param.clks, DSIM_LANE_CLOCK | dsim->data_lane);

	dsim->state = DSIM_STATE_HSCLKEN;

	enable_irq(dsim->irq);

	if (dsim->doze_state == DOZE_STATE_SUSPEND || dsim->doze_state == DOZE_STATE_DOZE_SUSPEND)
		call_panel_ops(dsim, enteralpm, dsim);

exit:
	dsim->doze_state = DOZE_STATE_DOZE;

	dsim_info("%s: --\n", __func__);

	return 0;
}

static int dsim_doze_suspend(struct dsim_device *dsim)
{
	if (dsim->state == DSIM_STATE_SUSPEND)
		goto exit;

	dsim_info("%s: ++ %d, %d\n", __func__, dsim->state, dsim->doze_state);

#ifdef CONFIG_DECON_MIPI_DSI_PKTGO
	dsim_pkt_go_enable(dsim, false);
#endif
	dsim_set_lcd_full_screen(dsim);

	if (dsim->doze_state == DOZE_STATE_NORMAL)
		call_panel_ops(dsim, enteralpm, dsim);

	dsim->doze_state = DOZE_STATE_DOZE_SUSPEND;

	/* Wait for current read & write CMDs. */
	mutex_lock(&dsim_rd_wr_mutex);
	dsim->state = DSIM_STATE_SUSPEND;
	mutex_unlock(&dsim_rd_wr_mutex);

	disable_irq(dsim->irq);
	dsim_reg_stop(dsim->id, DSIM_LANE_CLOCK | dsim->data_lane);
	phy_power_off(dsim->phy);

#if defined(CONFIG_PM_RUNTIME)
	pm_runtime_put_sync(dsim->dev);
#else
	dsim_runtime_suspend(dsim->dev);
#endif

exit:
	dsim_info("%s: --\n", __func__);
	return 0;
}
#endif

static int dsim_enter_ulps(struct dsim_device *dsim)
{
	int ret = 0;

	DISP_SS_EVENT_START();
	dsim_dbg("%s +\n", __func__);
	exynos_ss_printk("%s:state %d: active %d:+\n", __func__,
				dsim->state, pm_runtime_active(dsim->dev));

	if (dsim->state != DSIM_STATE_HSCLKEN) {
		ret = -EBUSY;
		goto err;
	}

#ifdef CONFIG_DECON_MIPI_DSI_PKTGO
	dsim_pkt_go_enable(dsim, false);
#endif
	/* Wait for current read & write CMDs. */
	mutex_lock(&dsim_rd_wr_mutex);
	dsim->state = DSIM_STATE_ULPS;
	mutex_unlock(&dsim_rd_wr_mutex);

	/* disable interrupts */
	dsim_reg_set_int(dsim->id, 0);

	disable_irq(dsim->irq);

	ret = dsim_reg_stop_and_enter_ulps(dsim->id, dsim->lcd_info.ddi_type,
			DSIM_LANE_CLOCK | dsim->data_lane);
	if (ret < 0)
		dsim_dump(dsim);

	phy_power_off(dsim->phy);

#if defined(CONFIG_PM_RUNTIME)
	pm_runtime_put_sync(dsim->dev);
#else
	dsim_runtime_suspend(dsim->dev);
#endif

	DISP_SS_EVENT_LOG(DISP_EVT_ENTER_ULPS, &dsim->sd, start);
err:
	dsim_dbg("%s -\n", __func__);
	exynos_ss_printk("%s:state %d: active %d:-\n", __func__,
				dsim->state, pm_runtime_active(dsim->dev));

	return ret;
}

static int dsim_exit_ulps(struct dsim_device *dsim)
{
	int ret = 0;

	DISP_SS_EVENT_START();
	dsim_dbg("%s +\n", __func__);
	exynos_ss_printk("%s:state %d: active %d:+\n", __func__,
				dsim->state, pm_runtime_active(dsim->dev));

	if (dsim->state != DSIM_STATE_ULPS) {
		ret = -EBUSY;
		goto err;
	}

#if defined(CONFIG_PM_RUNTIME)
	pm_runtime_get_sync(dsim->dev);
#else
	dsim_runtime_resume(dsim->dev);
#endif

	enable_irq(dsim->irq);

	/* DPHY power on */
	phy_power_on(dsim->phy);

	dsim_reg_set_clocks(dsim->id, &dsim->clks_param.clks, &dsim->lcd_info.dphy_pms, 1);
	dsim_reg_set_lanes(dsim->id, DSIM_LANE_CLOCK | dsim->data_lane, 1);
	dsim_reg_init(dsim->id, &dsim->lcd_info, dsim->data_lane_cnt,
			&dsim->clks_param.clks);

	ret = dsim_reg_exit_ulps_and_start(dsim->id, dsim->lcd_info.ddi_type,
			DSIM_LANE_CLOCK | dsim->data_lane);
	if (ret < 0)
		dsim_dump(dsim);

	/* enable interrupts */
	dsim_reg_set_int(dsim->id, 1);

	dsim->state = DSIM_STATE_HSCLKEN;

	DISP_SS_EVENT_LOG(DISP_EVT_EXIT_ULPS, &dsim->sd, start);
err:
	dsim_dbg("%s -\n", __func__);
	exynos_ss_printk("%s:state %d: active %d:-\n", __func__,
				dsim->state, pm_runtime_active(dsim->dev));

	return 0;
}

static struct dsim_device *sd_to_dsim(struct v4l2_subdev *sd)
{
	return container_of(sd, struct dsim_device, sd);
}

static int dsim_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct dsim_device *dsim = sd_to_dsim(sd);
	int ret = 0;

	switch (enable) {
	case DSIM_REQ_POWER_OFF:
		ret = dsim_disable(dsim);
		break;
	case DSIM_REQ_POWER_ON:
		ret = dsim_enable(dsim);
		break;
#ifdef CONFIG_LCD_DOZE_MODE
	case DSIM_REQ_DOZE_MODE:
		dsim_info("decon: dsim_doze_enable\n");
		ret = dsim_doze_enable(dsim);
		break;
	case DSIM_REQ_DOZE_SUSPEND:
		dsim_info("decon: dsim_doze_suspend\n");
		ret = dsim_doze_suspend(dsim);
		break;
#endif
	}

	return ret;
}

static long dsim_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct dsim_device *dsim = sd_to_dsim(sd);
	int ret = 0;

	switch (cmd) {
	case DSIM_IOC_GET_LCD_INFO:
		v4l2_set_subdev_hostdata(sd, &dsim->lcd_info);
		break;
	case DSIM_IOC_ENTER_ULPS:
		if ((unsigned long)arg)
			ret = dsim_enter_ulps(dsim);
		else
			ret = dsim_exit_ulps(dsim);
		break;
	case DSIM_IOC_LCD_OFF:
		ret = dsim_set_panel_power(dsim, 0);
		break;

#ifdef CONFIG_DECON_MIPI_DSI_PKTGO
	case DSIM_IOC_PKT_GO_ENABLE:
		dsim_pkt_go_enable(dsim, true);
		break;

	case DSIM_IOC_PKT_GO_DISABLE:
		dsim_pkt_go_enable(dsim, false);
		break;

	case DSIM_IOC_PKT_GO_READY:
		dsim_pkt_go_ready(dsim);
		break;
#endif
#ifdef CONFIG_FB_WINDOW_UPDATE
	case DSIM_IOC_PARTIAL_CMD:
		ret = dsim_partial_area_command(dsim, arg);
		break;
	case DSIM_IOC_SET_PORCH:
		dsim_reg_set_partial_update(dsim->id, (struct decon_lcd *)v4l2_get_subdev_hostdata(sd));
		break;
#endif
	case DSIM_IOC_DUMP:
		dsim_dump(dsim);
		/* Show panel status */
		call_panel_ops(dsim, dump, dsim);
		break;
	case DSIM_IOC_VSYNC:
		break;
	case DSIM_IOC_PANEL_DUMP:
		call_panel_ops(dsim, dump, dsim);
		break;
	default:
		dev_err(dsim->dev, "unsupported ioctl");
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct v4l2_subdev_core_ops dsim_sd_core_ops = {
	.ioctl = dsim_ioctl,
};

static const struct v4l2_subdev_video_ops dsim_sd_video_ops = {
	.s_stream = dsim_s_stream,
};

static const struct v4l2_subdev_ops dsim_subdev_ops = {
	.core = &dsim_sd_core_ops,
	.video = &dsim_sd_video_ops,
};

static int dsim_link_setup(struct media_entity *entity,
			      const struct media_pad *local,
			      const struct media_pad *remote, u32 flags)
{
	if (flags & MEDIA_LNK_FL_ENABLED)
		dev_info(NULL, "Link is enabled\n");
	else
		dev_info(NULL, "Link is disabled\n");

	return 0;
}

static const struct media_entity_operations dsim_entity_ops = {
	.link_setup = dsim_link_setup,
};

static int dsim_register_entity(struct dsim_device *dsim)
{
	struct v4l2_subdev *sd = &dsim->sd;
	struct v4l2_device *v4l2_dev;
	struct device *dev = dsim->dev;
	struct media_pad *pads = &dsim->pad;
	struct media_entity *me = &sd->entity;
	struct exynos_md *md;
	int ret;

	v4l2_subdev_init(sd, &dsim_subdev_ops);
	sd->owner = THIS_MODULE;
	snprintf(sd->name, sizeof(sd->name), "exynos-mipi-dsi%d-subdev", dsim->id);

	dev_set_drvdata(dev, sd);
	pads[DSIM_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	me->ops = &dsim_entity_ops;
	ret = media_entity_init(me, DSIM_PADS_NUM, pads, 0);
	if (ret) {
		dev_err(dev, "failed to initialize media entity\n");
		return ret;
	}

	md = (struct exynos_md *)module_name_to_driver_data(MDEV_MODULE_NAME);
	if (!md) {
		dev_err(dev, "failed to get output media device\n");
		return -ENODEV;
	}

	v4l2_dev = &md->v4l2_dev;

	ret = v4l2_device_register_subdev(v4l2_dev, sd);
	if (ret) {
		dev_err(dev, "failed to register HDMI subdev\n");
		return ret;
	}

	/* 0: DSIM_0, 1: DSIM_1 */
	md->dsim_sd[dsim->id] = &dsim->sd;

	return 0;
}

static ssize_t dsim_rw_test_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return 0;
}

static ssize_t decon_rw_test_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned long cmd;
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct dsim_device *dsim = sd_to_dsim(sd);

	ret = kstrtoul(buf, 0, &cmd);
	if (ret)
		return ret;

	ret = -ENXIO;

	if (cmd == 0x1) {
		dsim_info("Dsim is trying to do write test\n");
		dsim_write_test(dsim);
	} else {
		dsim_info("Dsim is trying to do read test\n");
		dsim_read_test(dsim);
	}

	return ret;
}
static DEVICE_ATTR(rw_test, 0644, dsim_rw_test_show, decon_rw_test_store);

int dsim_create_rw_test_sysfs(struct dsim_device *dsim)
{
	int ret = 0;

	ret = device_create_file(dsim->dev, &dev_attr_rw_test);
	if (ret)
		dsim_err("failed to create read & write test sysfs\n");

	return ret;
}

static int dsim_parse_lcd_info(struct dsim_device *dsim)
{
	u32 res[3];
	struct device_node *node;
	int octa_id_gpio = 0;

	node = of_parse_phandle(dsim->dev->of_node, "lcd_info", 0);

	of_property_read_u32(node, "mode", &dsim->lcd_info.mode);
	dsim_dbg("%s mode\n", dsim->lcd_info.mode ? "command" : "video");

	of_property_read_u32_array(node, "resolution", res, 2);
	dsim->lcd_info.xres = res[0];
	dsim->lcd_info.yres = res[1];
	dsim_info("LCD(%s) resolution: xres(%d), yres(%d)\n",
			of_node_full_name(node), res[0], res[1]);

	of_property_read_u32_array(node, "size", res, 2);
	dsim->lcd_info.width = res[0];
	dsim->lcd_info.height = res[1];
	dsim_dbg("LCD size: width(%d), height(%d)\n", res[0], res[1]);

	of_property_read_u32(node, "timing,refresh", &dsim->lcd_info.fps);
	dsim_dbg("LCD refresh rate(%d)\n", dsim->lcd_info.fps);

	of_property_read_u32_array(node, "timing,h-porch", res, 3);
	dsim->lcd_info.hbp = res[0];
	dsim->lcd_info.hfp = res[1];
	dsim->lcd_info.hsa = res[2];
	dsim_dbg("hbp(%d), hfp(%d), hsa(%d)\n", res[0], res[1], res[2]);

	of_property_read_u32_array(node, "timing,v-porch", res, 3);
	dsim->lcd_info.vbp = res[0];
	dsim->lcd_info.vfp = res[1];
	dsim->lcd_info.vsa = res[2];
	dsim_dbg("vbp(%d), vfp(%d), vsa(%d)\n", res[0], res[1], res[2]);

	of_property_read_u32(node, "timing,dsi-hs-clk", &dsim->lcd_info.hs_clk);
	dsim->clks_param.clks.hs_clk = dsim->lcd_info.hs_clk;
	dsim_dbg("requested hs clock(%d)\n", dsim->lcd_info.hs_clk);

	of_property_read_u32_array(node, "timing,pms", res, 3);
	dsim->lcd_info.dphy_pms.p = res[0];
	dsim->lcd_info.dphy_pms.m = res[1];
	dsim->lcd_info.dphy_pms.s = res[2];
	dsim_dbg("p(%d), m(%d), s(%d)\n", res[0], res[1], res[2]);

	of_property_read_u32(node, "timing,dsi-escape-clk", &dsim->lcd_info.esc_clk);
	dsim->clks_param.clks.esc_clk = dsim->lcd_info.esc_clk;
	dsim_dbg("requested escape clock(%d)\n", dsim->lcd_info.esc_clk);

	of_property_read_u32(node, "mic_en", &dsim->lcd_info.mic_enabled);
	dsim_info("mic enabled (%d)\n", dsim->lcd_info.mic_enabled);

	of_property_read_u32(node, "mic_ratio", &dsim->lcd_info.mic_ratio);
	switch (dsim->lcd_info.mic_ratio) {
	case MIC_COMP_RATIO_1_2:
		dsim_info("mic ratio (%s)\n", "MIC_COMP_RATIO_1_2");
		break;
	case MIC_COMP_RATIO_1_3:
		dsim_info("mic ratio (%s)\n", "MIC_COMP_RATIO_1_3");
		break;
	default: /*MIC_COMP_BYPASS*/
		dsim_info("mic ratio (%s)\n", "MIC_COMP_BYPASS");
		break;
	}

	of_property_read_u32(node, "mic_ver", &dsim->lcd_info.mic_ver);
	dsim_dbg("mic version(%d)\n", dsim->lcd_info.mic_ver);

	of_property_read_u32(node, "type_of_ddi", &dsim->lcd_info.ddi_type);
	dsim_info("ddi type(%d)\n", dsim->lcd_info.ddi_type);

	of_property_read_u32(node, "clklane_onoff", &dsim->lcd_info.clklane_onoff);
	dsim_info("clklane onoff(%d)\n", dsim->lcd_info.clklane_onoff);

	octa_id_gpio = of_get_gpio(node, 0);
	if (octa_id_gpio > 0) {
		gpio_request_one(octa_id_gpio, GPIOF_IN, "octa_id");
		dsim->octa_id = gpio_get_value(octa_id_gpio);
		dsim_info("dsim board OCTA pin (%s)\n", dsim->octa_id ? "high" : "low");
		gpio_free(octa_id_gpio);
	} else {
		dsim_info("dsim board has no OCTA pin (%d)\n", octa_id_gpio);
	}

	return 0;
}

#ifdef CONFIG_EXYNOS_MIPI_DSI_ENABLE_EARLY
static int dsim_enable_early_resume(struct dsim_device *dsim)
{
	struct irq_desc *desc;
	int i;

	if (!dsim->enable_early_irq)
		goto exit;

	dsim_info("%s: +\n", __func__);

	for (i = 0; dsim->enable_early_irq[i]; i++) {
		if (check_wakeup_reason(dsim->enable_early_irq[i])) {
			desc = irq_to_desc(dsim->enable_early_irq[i]);
			dsim_info("%s: irq: %d, %s\n", __func__, dsim->enable_early_irq[i],
				(desc && desc->action && desc->action->name) ? desc->action->name : "");
			dsim->enable_early = DSIM_ENABLE_EARLY_REQUEST;
			dsim_enable(dsim);
			goto exit;
		}
	}

exit:
	dsim_info("%s: -\n", __func__);

	return 0;
}

static int dsim_enable_early_suspend(struct dsim_device *dsim)
{
	dsim_info("%s: +\n", __func__);

	dsim_disable(dsim);

	dsim_info("%s: -\n", __func__);

	return 0;
}

static int dsim_pm_notifier(struct notifier_block *nb,
		unsigned long event, void *v)
{
	struct dsim_device *dsim = container_of(nb, struct dsim_device, pm_notifier);

	dev_info(dsim->dev, "%s: %ld\n", __func__, event);

	switch (event) {
	case PM_SUSPEND_PREPARE:
		dsim_enable_early_suspend(dsim);
		return NOTIFY_OK;
	case PM_POST_SUSPEND:
		dsim_enable_early_resume(dsim);
		return NOTIFY_OK;
	}
	return NOTIFY_DONE;
}

static int dsim_setup_enable_early(struct dsim_device *dsim)
{
	struct irq_desc *desc;
	int ret = 0, count, gpio, i;
	struct device *dev = dsim->dev;

	count = of_gpio_named_count(dev->of_node, "enable_early_gpio");
	if (count < 0) {
		dsim_info("%s: there is no enable_early_gpio\n", __func__);
		goto exit;
	}

	dsim->enable_early_irq = kcalloc(count + 1, sizeof(int), GFP_KERNEL);

	for (i = 0; i < count; i++) {
		gpio = of_get_named_gpio(dev->of_node, "enable_early_gpio", i);
		if (!gpio_is_valid(gpio)) {
			dsim_info("%s: %dth gpio(%d) is invalid\n", __func__, i, gpio);
			kfree(dsim->enable_early_irq);
			dsim->enable_early_irq = NULL;
			goto exit;
		}
		dsim->enable_early_irq[i] = gpio_to_irq(gpio);
	}

	for (i = 0; dsim->enable_early_irq[i]; i++) {
		desc = irq_to_desc(dsim->enable_early_irq[i]);
		dsim_info("%s: irq: %d, %s\n", __func__, dsim->enable_early_irq[i],
			(desc && desc->action && desc->action->name) ? desc->action->name : "");
	}

	dsim->pm_notifier.notifier_call = dsim_pm_notifier;
	dsim->pm_notifier.priority = INT_MAX;
	register_pm_notifier(&dsim->pm_notifier);

exit:
	return ret;
}
#endif

static int dsim_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct resource *res;
	struct device *dev = &pdev->dev;
	struct dsim_device *dsim = NULL;

	dsim = devm_kzalloc(dev, sizeof(struct dsim_device), GFP_KERNEL);
	if (!dsim) {
		dev_err(dev, "failed to allocate dsim device.\n");
		ret = -ENOMEM;
		goto err;
	}

	dsim->id = of_alias_get_id(dev->of_node, "dsim");
	dsim_info("dsim(%d) probe start..\n", dsim->id);

	dsim->phy = devm_phy_get(&pdev->dev, "dsim_dphy");
	if (IS_ERR(dsim->phy))
		return PTR_ERR(dsim->phy);

	if (!dsim->id)
		dsim0_for_decon = dsim;
	else
		dsim1_for_decon = dsim;

	dsim->dev = &pdev->dev;

	dsim_parse_lcd_info(dsim);
	ret = dsim_panel_ops_init(dsim);
	if (ret) {
		dsim_err("%s: failed to set panel ops\n", __func__);
		goto err;
	}

	call_panel_ops(dsim, early_probe, dsim);

	dsim_get_gpios(dsim);

	dsim_get_clocks(dsim);
	spin_lock_init(&dsim->slock);

	of_property_read_u32(dev->of_node, "data_lane_cnt", &dsim->data_lane_cnt);
	dev_info(dev, "using data lane count(%d)\n", dsim->data_lane_cnt);

	/* added */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "failed to get resource");
		ret = -ENOENT;
		goto err_mem_region;
	}

	dsim_info("res: start(0x%x), end(0x%x)\n", (u32)res->start, (u32)res->end);
	dsim->reg_base = devm_ioremap_resource(dev, res);
	if (!dsim->reg_base) {
		dev_err(&pdev->dev, "mipi-dsi: failed to remap io region\n");
		ret = -EINVAL;
		goto err_mem_region;
	}

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(dev, "failed to get resource");
		ret = -EINVAL;
		goto err_mem_region;
	}

	dsim->irq = res->start;
	ret = devm_request_irq(dev, res->start,
			dsim_interrupt_handler, 0, pdev->name, dsim);
	if (ret) {
		dev_err(dev, "failed to install irq\n");
		goto err_irq;
	}

	ret = dsim_register_entity(dsim);
	if (ret)
		goto err_irq;

	dsim->timing.bps = 0;

	mutex_init(&dsim_rd_wr_mutex);

	pm_runtime_enable(dev);

	/* set using lanes */
	switch (dsim->data_lane_cnt) {
	case 1:
		dsim->data_lane = DSIM_LANE_DATA0;
		break;
	case 2:
		dsim->data_lane = DSIM_LANE_DATA0 | DSIM_LANE_DATA1;
		break;
	case 3:
		dsim->data_lane = DSIM_LANE_DATA0 | DSIM_LANE_DATA1 |
			DSIM_LANE_DATA2;
		break;
	case 4:
		dsim->data_lane = DSIM_LANE_DATA0 | DSIM_LANE_DATA1 |
			DSIM_LANE_DATA2 | DSIM_LANE_DATA3;
		break;
	default:
		dev_info(dsim->dev, "data lane is invalid.\n");
		return -EINVAL;
	};

	dsim->state = DSIM_STATE_SUSPEND;

#if defined(CONFIG_PM_RUNTIME)
	pm_runtime_get_sync(dsim->dev);
#else
	dsim_runtime_resume(dsim->dev);
#endif

	/* DPHY init and power on */
	phy_init(dsim->phy);
	phy_power_on(dsim->phy);

	/* With video mode, Do not control clk and lane, dsim standby again */
	if (dsim->lcd_info.mode == DECON_MIPI_COMMAND_MODE) {
		dsim_reg_set_clocks(dsim->id, &dsim->clks_param.clks, &dsim->lcd_info.dphy_pms, 1);
		dsim_reg_set_lanes(dsim->id, DSIM_LANE_CLOCK | dsim->data_lane, 1);
	}

	/* Panel power on */
	ret = dsim_set_panel_pre_power(dsim);
	if (ret) {
		dsim_err("%s : failed to panel power early\n", __func__);
		goto err;
	}

	ret = dsim_set_panel_power(dsim, 1);
	if (ret) {
		dsim_err("%s: failed to panel power\n", __func__);
		goto err;
	}

	/* Goto dsim_init_done when LCD_ON UBOOT is on */
	if (dsim_reg_init(dsim->id, &dsim->lcd_info, dsim->data_lane_cnt,
			&dsim->clks_param.clks) < 0)
			goto dsim_init_done;

	dsim_reset_panel(dsim);

dsim_init_done:
	if (dsim->lcd_info.mode == DECON_MIPI_COMMAND_MODE)
		dsim_reg_start(dsim->id, &dsim->clks_param.clks, DSIM_LANE_CLOCK | dsim->data_lane);

	dsim->state = DSIM_STATE_HSCLKEN;

	call_panel_ops(dsim, probe, dsim);
	/* TODO: displayon is moved to decon probe only in case of lcd on probe */
	/* dsim->panel_ops->displayon(dsim); */

	dsim_clocks_info(dsim);

	dsim_create_rw_test_sysfs(dsim);

#ifdef CONFIG_EXYNOS_MIPI_DSI_ENABLE_EARLY
	dsim_setup_enable_early(dsim);
#endif

	dev_info(dev, "mipi-dsi driver(%s mode) has been probed.\n",
		dsim->lcd_info.mode == DECON_MIPI_COMMAND_MODE ? "CMD" : "VIDEO");

	return 0;

err_irq:
	release_resource(res);

err_mem_region:
	dsim_put_clocks(dsim);
	kfree(dsim);

err:
	return ret;
}

static int dsim_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct dsim_device *dsim = sd_to_dsim(sd);

	pm_runtime_disable(dev);
	dsim_put_clocks(dsim);
	mutex_destroy(&dsim_rd_wr_mutex);
	dev_info(dev, "mipi-dsi driver removed\n");

	return 0;
}

static void dsim_shutdown(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct dsim_device *dsim = sd_to_dsim(sd);

	DISP_SS_EVENT_LOG(DISP_EVT_DSIM_SHUTDOWN, sd, ktime_set(0, 0));
	dev_info(dev, "%s + state:%d\n", __func__, dsim->state);

	dsim_disable(dsim);

	dev_info(dev, "%s -\n", __func__);
}

static const struct of_device_id dsim_match[] = {
	{ .compatible = "samsung,exynos8-mipi-dsi" },
	{},
};
MODULE_DEVICE_TABLE(of, dsim_match);

static struct platform_device_id dsim_ids[] = {
	{
		.name		= "exynos-mipi-dsi"
	},
	{},
};
MODULE_DEVICE_TABLE(platform, dsim_ids);

static int dsim_runtime_suspend(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct dsim_device *dsim = sd_to_dsim(sd);

	DISP_SS_EVENT_LOG(DISP_EVT_DSIM_SUSPEND, sd, ktime_set(0, 0));
	dsim_dbg("%s +\n", __func__);

	clk_disable_unprepare(dsim->res.pclk);
	clk_disable_unprepare(dsim->res.dphy_esc);
	clk_disable_unprepare(dsim->res.dphy_byte);
	dsim_dbg("%s -\n", __func__);
	return 0;
}

static int dsim_runtime_resume(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct dsim_device *dsim = sd_to_dsim(sd);

	DISP_SS_EVENT_LOG(DISP_EVT_DSIM_RESUME, sd, ktime_set(0, 0));
	dsim_dbg("%s: +\n", __func__);

	clk_prepare_enable(dsim->res.pclk);
	clk_prepare_enable(dsim->res.dphy_esc);
	clk_prepare_enable(dsim->res.dphy_byte);
	dsim_dbg("%s -\n", __func__);
	return 0;
}

static const struct dev_pm_ops dsim_pm_ops = {
	.runtime_suspend	= dsim_runtime_suspend,
	.runtime_resume		= dsim_runtime_resume,
};

static struct platform_driver dsim_driver __refdata = {
	.probe			= dsim_probe,
	.remove			= dsim_remove,
	.shutdown		= dsim_shutdown,
	.id_table		= dsim_ids,
	.driver = {
		.name		= "exynos-mipi-dsi",
		.owner		= THIS_MODULE,
		.pm		= &dsim_pm_ops,
		.of_match_table	= of_match_ptr(dsim_match),
		.suppress_bind_attrs = true,
	}
};

static int __init dsim_init(void)
{
	int ret = platform_driver_register(&dsim_driver);
	if (ret)
		dsim_err("mipi_dsi driver register failed\n");

	return ret;
}
late_initcall(dsim_init);

static void __exit dsim_exit(void)
{
	platform_driver_unregister(&dsim_driver);
}

module_exit(dsim_exit);
MODULE_AUTHOR("Jiun Yu <jiun.yu@samsung.com>");
MODULE_DESCRIPTION("Samusung MIPI-DSI driver");
