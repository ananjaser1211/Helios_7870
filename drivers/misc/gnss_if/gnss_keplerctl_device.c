/*
 * Copyright (C) 2010 Samsung Electronics.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/mcu_ipc.h>

#include <asm/cacheflush.h>

#include "gnss_prj.h"
#include "gnss_link_device_shmem.h"
#include "pmu-gnss.h"

static irqreturn_t kepler_active_isr(int irq, void *arg)
{
	struct gnss_ctl *gc = (struct gnss_ctl *)arg;
	struct io_device *iod = gc->iod;

	gif_err("ACTIVE Interrupt occurred!\n");

	if (!wake_lock_active(&gc->gc_fault_wake_lock))
		wake_lock_timeout(&gc->gc_fault_wake_lock, HZ);

	gc->iod->gnss_state_changed(gc->iod, STATE_FAULT);
	wake_up(&iod->wq);

	gc->pmu_ops.clear_int(gc, GNSS_INT_ACTIVE_CLEAR);

	return IRQ_HANDLED;
}

static irqreturn_t kepler_wdt_isr(int irq, void *arg)
{
	struct gnss_ctl *gc = (struct gnss_ctl *)arg;
	struct io_device *iod = gc->iod;

	gif_err("WDT Interrupt occurred!\n");

	if (!wake_lock_active(&gc->gc_fault_wake_lock))
		wake_lock_timeout(&gc->gc_fault_wake_lock, HZ);

	gc->iod->gnss_state_changed(gc->iod, STATE_FAULT);
	wake_up(&iod->wq);

	gc->pmu_ops.clear_int(gc, GNSS_INT_WDT_RESET_CLEAR);
	return IRQ_HANDLED;
}

static irqreturn_t kepler_wakelock_isr(int irq, void *arg)
{
	struct gnss_ctl *gc = (struct gnss_ctl *)arg;
	struct gnss_mbox *mbx = gc->gnss_data->mbx;
	struct link_device *ld = gc->iod->ld;
	struct shmem_link_device *shmd = to_shmem_link_device(ld);
	/*
	u32 rx_tail, rx_head, tx_tail, tx_head, gnss_ipc_msg, ap_ipc_msg;
	*/

#ifdef USE_SIMPLE_WAKE_LOCK
	gif_err("Unexpected interrupt occurred(%s)!!!!\n", __func__);
	return IRQ_HANDLED;
#endif

	/* This is for debugging
	tx_head = get_txq_head(shmd);
	tx_tail = get_txq_tail(shmd);
	rx_head = get_rxq_head(shmd);
	rx_tail = get_rxq_tail(shmd);
	gnss_ipc_msg =  mbox_get_value(MCU_GNSS, shmd->irq_gnss2ap_ipc_msg);
	ap_ipc_msg = read_int2gnss(shmd);

	gif_err("RX_H[0x%x], RX_T[0x%x], TX_H[0x%x], TX_T[0x%x],\
			AP_IPC[0x%x], GNSS_IPC[0x%x]\n",
			rx_head, rx_tail, tx_head, tx_tail, ap_ipc_msg, gnss_ipc_msg);
	*/

	/* Clear wake_lock */
	if (wake_lock_active(&shmd->wlock))
		wake_unlock(&shmd->wlock);

	gif_debug("Wake Lock ISR!!!!\n");
	gif_err(">>>>DBUS_SW_WAKE_INT\n");

	/* 1. Set wake-lock-timeout(). */
	if (!wake_lock_active(&gc->gc_wake_lock))
		wake_lock_timeout(&gc->gc_wake_lock, HZ); /* 1 sec */

	/* 2. Disable DBUS_SW_WAKE_INT interrupts. */
	disable_irq_nosync(gc->wake_lock_irq);

	/* 3. Write 0x1 to MBOX_reg[6]. */
	/* MBOX_req[6] is WAKE_LOCK */
	if (mbox_get_value(MCU_GNSS, mbx->reg_wake_lock) == 0X1) {
		gif_err("@@ reg_wake_lock is already 0x1!!!!!!\n");
		return IRQ_HANDLED;
	} else {
		mbox_set_value(MCU_GNSS, mbx->reg_wake_lock, 0x1);
	}

	/* 4. Send interrupt MBOX1[3]. */
	/* Interrupt MBOX1[3] is RSP_WAKE_LOCK_SET */
	mbox_set_interrupt(MCU_GNSS, mbx->int_ap2gnss_ack_wake_set);

	return IRQ_HANDLED;
}
#ifdef USE_SIMPLE_WAKE_LOCK
static void mbox_kepler_simple_lock(void *arg)
{
	struct gnss_ctl *gc = (struct gnss_ctl *)arg;
	struct gnss_mbox *mbx = gc->gnss_data->mbx;

	gif_debug("[GNSS] WAKE interrupt(Mbox15) occurred\n");
	mbox_set_interrupt(MCU_GNSS, mbx->int_ap2gnss_ack_wake_set);
	gc->pmu_ops.clear_int(gc, GNSS_INT_WAKEUP_CLEAR);
}
#endif

static void mbox_kepler_wake_clr(void *arg)
{
	struct gnss_ctl *gc = (struct gnss_ctl *)arg;
	struct gnss_mbox *mbx = gc->gnss_data->mbx;

	/*
	struct link_device *ld = gc->iod->ld;
	struct shmem_link_device *shmd = to_shmem_link_device(ld);
	u32 rx_tail, rx_head, tx_tail, tx_head, gnss_ipc_msg, ap_ipc_msg;
	*/
#ifdef USE_SIMPLE_WAKE_LOCK
	gif_err("Unexpected interrupt occurred(%s)!!!!\n", __func__);
	return ;
#endif
	/*
	tx_head = get_txq_head(shmd);
	tx_tail = get_txq_tail(shmd);
	rx_head = get_rxq_head(shmd);
	rx_tail = get_rxq_tail(shmd);
	gnss_ipc_msg = mbox_get_value(MCU_GNSS, shmd->irq_gnss2ap_ipc_msg);
	ap_ipc_msg = read_int2gnss(shmd);

	gif_eff("RX_H[0x%x], RX_T[0x%x], TX_H[0x%x], TX_T[0x%x], AP_IPC[0x%x], GNSS_IPC[0x%x]\n",
			rx_head, rx_tail, tx_head, tx_tail, ap_ipc_msg, gnss_ipc_msg);
	*/
	gc->pmu_ops.clear_int(gc, GNSS_INT_WAKEUP_CLEAR);

	gif_debug("Wake Lock Clear!!!!\n");
	gif_err(">>>>DBUS_SW_WAKE_INT CLEAR\n");

	wake_unlock(&gc->gc_wake_lock);
	enable_irq(gc->wake_lock_irq);
	if (mbox_get_value(MCU_GNSS, mbx->reg_wake_lock) == 0X0) {
		gif_err("@@ reg_wake_lock is already 0x0!!!!!!\n");
		return ;
	}
	mbox_set_value(MCU_GNSS, mbx->reg_wake_lock, 0x0);
	mbox_set_interrupt(MCU_GNSS, mbx->int_ap2gnss_ack_wake_clr);

}

static void mbox_kepler_rsp_fault_info(void *arg)
{
	struct gnss_ctl *gc = (struct gnss_ctl *)arg;

	complete(&gc->fault_cmpl);
}

static int kepler_hold_reset(struct gnss_ctl *gc)
{
	gif_err("%s\n", __func__);

	if (gc->gnss_state == STATE_OFFLINE) {
		gif_err("Current Kerpler status is OFFLINE, so it will be ignored\n");
		return 0;
	}

	gc->iod->gnss_state_changed(gc->iod, STATE_HOLD_RESET);
	gc->pmu_ops.hold_reset(gc);
	mbox_sw_reset(MCU_GNSS);

	return 0;
}

static int kepler_release_reset(struct gnss_ctl *gc)
{
	gif_err("%s\n", __func__);

	gc->iod->gnss_state_changed(gc->iod, STATE_ONLINE);
	gc->pmu_ops.release_reset(gc);

	return 0;
}

static int kepler_power_on(struct gnss_ctl *gc)
{
	gif_err("%s\n", __func__);

	gc->iod->gnss_state_changed(gc->iod, STATE_ONLINE);
	gc->pmu_ops.power_on(gc, GNSS_POWER_ON);

	return 0;
}

static int kepler_req_fault_info(struct gnss_ctl *gc, u32 *fault_info_regs)
{
	int i, ret;
	struct gnss_mbox *mbx = gc->gnss_data->mbx;
	unsigned long timeout = msecs_to_jiffies(1000);


	mbox_set_interrupt(MCU_GNSS, mbx->int_ap2gnss_req_fault_info);

	ret = wait_for_completion_timeout(&gc->fault_cmpl,
						timeout);
	if (ret == 0) {
		gif_err("Req Fault Info TIMEOUT!\n");
		return -EIO;
	}

	/* Issue interrupt. */
	for (i = 0; i < FAULT_INFO_COUNT; i++) {
		fault_info_regs[i] = mbox_get_value(MCU_GNSS,
				mbx->reg_fault_info[i]);
	}
	wake_unlock(&gc->gc_fault_wake_lock);

	return 0;
}


static int kepler_suspend(struct gnss_ctl *gc)
{
	return 0;
}

static int kepler_resume(struct gnss_ctl *gc)
{
#ifdef USE_SIMPLE_WAKE_LOCK
	gc->pmu_ops.clear_int(gc, GNSS_INT_WAKEUP_CLEAR);
#endif

	return 0;
}

static int kepler_change_gpio(struct gnss_ctl *gc)
{
	int status = 0;

	gif_err("Change GPIO for sensor\n");
	if (!IS_ERR(gc->gnss_sensor_gpio)) {
		status = pinctrl_select_state(gc->gnss_gpio, gc->gnss_sensor_gpio);
		if (status) {
			gif_err("Can't change sensor GPIO(%d)\n", status);
		}
	} else {
		gif_err("gnss_sensor_gpio is not valid(0x%p)\n", gc->gnss_sensor_gpio);
		status = -EIO;
	}

	return status;
}

static int kepler_set_sensor_power(struct gnss_ctl *gc, unsigned long arg)
{
	int ret;
	int reg_en = *((enum sensor_power*)arg);

	if (reg_en == 0) {
		ret = regulator_disable(gc->vdd_sensor_reg);
		if (ret != 0)
			gif_err("Failed : Disable sensor power.\n");
		else
			gif_err("Success : Disable sensor power.\n");
	} else {
		ret = regulator_enable(gc->vdd_sensor_reg);
		if (ret != 0)
			gif_err("Failed : Enable sensor power.\n");
		else
			gif_err("Success : Enable sensor power.\n");
	}
	return ret;
}

static void gnss_get_ops(struct gnss_ctl *gc)
{
	gc->ops.gnss_hold_reset = kepler_hold_reset;
	gc->ops.gnss_release_reset = kepler_release_reset;
	gc->ops.gnss_power_on = kepler_power_on;
	gc->ops.gnss_req_fault_info = kepler_req_fault_info;
	gc->ops.suspend_gnss_ctrl = kepler_suspend;
	gc->ops.resume_gnss_ctrl = kepler_resume;
	gc->ops.change_sensor_gpio = kepler_change_gpio;
	gc->ops.set_sensor_power = kepler_set_sensor_power;
}

static void gnss_get_pmu_ops(struct gnss_ctl *gc)
{
	gc->pmu_ops.hold_reset = gnss_pmu_hold_reset;
	gc->pmu_ops.release_reset = gnss_pmu_release_reset;
	gc->pmu_ops.power_on = gnss_pmu_power_on;
	gc->pmu_ops.clear_int = gnss_pmu_clear_interrupt;
	gc->pmu_ops.init_conf = gnss_pmu_init_conf;
	gc->pmu_ops.change_tcxo_mode = gnss_change_tcxo_mode;

}

int init_gnssctl_device(struct gnss_ctl *gc, struct gnss_data *pdata)
{
	int ret = 0, irq = 0;
	struct platform_device *pdev = NULL;
	struct gnss_mbox *mbox = gc->gnss_data->mbx;
	gif_err("[GNSS IF] Initializing GNSS Control\n");

	gnss_get_ops(gc);
	gnss_get_pmu_ops(gc);
	dev_set_drvdata(gc->dev, gc);

	wake_lock_init(&gc->gc_fault_wake_lock,
				WAKE_LOCK_SUSPEND, "gnss_fault_wake_lock");
	wake_lock_init(&gc->gc_wake_lock,
				WAKE_LOCK_SUSPEND, "gnss_wake_lock");

	init_completion(&gc->fault_cmpl);

	pdev = to_platform_device(gc->dev);

	/* GNSS_ACTIVE */
	irq = platform_get_irq(pdev, 0);
	ret = devm_request_irq(&pdev->dev, irq, kepler_active_isr, 0,
			  "kepler_active_handler", gc);
	if (ret) {
		gif_err("Request irq fail - kepler_active_isr(%d)\n", ret);
		return ret;
	}
	enable_irq_wake(irq);

	/* GNSS_WATCHDOG */
	irq = platform_get_irq(pdev, 1);
	ret = devm_request_irq(&pdev->dev, irq, kepler_wdt_isr, 0,
			  "kepler_wdt_handler", gc);
	if (ret) {
		gif_err("Request irq fail - kepler_wdt_isr(%d)\n", ret);
		return ret;
	}
	enable_irq_wake(irq);

	/* GNSS_WAKEUP */
	gc->wake_lock_irq = platform_get_irq(pdev, 2);
	ret = devm_request_irq(&pdev->dev, gc->wake_lock_irq, kepler_wakelock_isr,
			0, "kepler_wakelock_handler", gc);

	if (ret) {
		gif_err("Request irq fail - kepler_wakelock_isr(%d)\n", ret);
		return ret;
	}
	enable_irq_wake(irq);
#ifdef USE_SIMPLE_WAKE_LOCK
	disable_irq(gc->wake_lock_irq);

	gif_err("Using simple lock sequence!!!\n");
	mbox_request_irq(MCU_GNSS, 15, mbox_kepler_simple_lock, (void *)gc);

#endif

	/* Initializing Shared Memory for GNSS */
	gif_err("Initializing shared memory for GNSS.\n");
	gc->pmu_ops.init_conf(gc);
	gc->gnss_state = STATE_OFFLINE;

	pr_info("[GNSS IF] Register mailbox for GNSS2AP fault handling\n");
	mbox_request_irq(MCU_GNSS, mbox->irq_gnss2ap_req_wake_clr,
			 mbox_kepler_wake_clr, (void *)gc);

	mbox_request_irq(MCU_GNSS, mbox->irq_gnss2ap_rsp_fault_info,
			 mbox_kepler_rsp_fault_info, (void *)gc);

	gc->gnss_gpio = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(gc->gnss_gpio)) {
		gif_err("Can't get gpio for GNSS sensor.\n");
	} else {
		gc->gnss_sensor_gpio = pinctrl_lookup_state(gc->gnss_gpio,
				"gnss_sensor");
	}

	gc->vdd_sensor_reg = devm_regulator_get(gc->dev, "vdd_sensor_2p85");
	if (IS_ERR(gc->vdd_sensor_reg)) {
		gif_err("Cannot get the regulator \"vdd_sensor_2p85\"\n");
	}

	gif_err("---\n");

	return ret;
}

