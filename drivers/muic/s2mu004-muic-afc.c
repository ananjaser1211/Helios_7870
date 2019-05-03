/*
 * s2mu004-muic.c - MUIC driver for the Samsung s2mu004
 *
 *  Copyright (C) 2015 Samsung Electronics
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * This driver is based on max77843-muic-afc.c
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/wakelock.h>

#include <linux/mfd/samsung/s2mu004.h>
#include <linux/mfd/samsung/s2mu004-private.h>

/* MUIC header file */
#include <linux/muic/muic.h>
#include <linux/muic/s2mu004-muic-hv-typedef.h>
#include <linux/muic/s2mu004-muic.h>
#include <linux/muic/s2mu004-muic-hv.h>

#if defined(CONFIG_MUIC_NOTIFIER)
#include <linux/muic/muic_notifier.h>
#endif /* CONFIG_MUIC_NOTIFIER */

#include "muic-internal.h"
#if defined(CONFIG_MUIC_SUPPORT_CCIC)
#include "muic_ccic.h"
#endif

//#if !defined(CONFIG_SEC_FACTORY)
//#if defined(CONFIG_MUIC_ADCMODE_SWITCH_WA)
#include <linux/delay.h>
//#endif /* CONFIG_MUIC_ADCMODE_SWITCH_WA */
//#endif /* !CONFIG_SEC_FACTORY */

static bool debug_en_checklist = false;

/* temp function for function pointer (TODO) */
enum act_function_num {
	FUNC_TA_TO_PREPARE		= 0,
	FUNC_PREPARE_TO_PREPARE_DUPLI,
	FUNC_PREPARE_TO_AFC_5V,
	FUNC_PREPARE_TO_QC_PREPARE,
	FUNC_PREPARE_DUPLI_TO_PREPARE_DUPLI,
	FUNC_PREPARE_DUPLI_TO_AFC_5V,
	FUNC_PREPARE_DUPLI_TO_AFC_ERR_V,
	FUNC_PREPARE_DUPLI_TO_AFC_9V,
	FUNC_AFC_5V_TO_AFC_5V_DUPLI,
	FUNC_AFC_5V_TO_AFC_ERR_V,
	FUNC_AFC_5V_TO_AFC_9V,
	FUNC_AFC_5V_DUPLI_TO_AFC_5V_DUPLI,
	FUNC_AFC_5V_DUPLI_TO_AFC_ERR_V,
	FUNC_AFC_5V_DUPLI_TO_AFC_9V,
	FUNC_AFC_ERR_V_TO_AFC_ERR_V_DUPLI,
	FUNC_AFC_ERR_V_TO_AFC_9V,
	FUNC_AFC_ERR_V_DUPLI_TO_AFC_ERR_V_DUPLI,
	FUNC_AFC_ERR_V_DUPLI_TO_AFC_9V,
	FUNC_AFC_9V_TO_AFC_5V,
	FUNC_AFC_9V_TO_AFC_9V,
	FUNC_QC_PREPARE_TO_QC_9V,
#ifdef CONFIG_MUIC_HV_FORCE_LIMIT
	FUNC_QC_9V_TO_QC_5V,
#endif
};

muic_afc_data_t prepare_dupli_to_afc_9v;
/* afc_condition_checklist[ATTACHED_DEV_TA_MUIC] */
muic_afc_data_t ta_to_prepare = {
	.new_dev		= ATTACHED_DEV_AFC_CHARGER_PREPARE_MUIC,
	.afc_name		= "AFC charger Prepare",
	.afc_irq		= MUIC_AFC_IRQ_VDNMON,
	.status_vbadc		= VBADC_DONTCARE,
	.status_vdnmon          = VDNMON_LOW,
	.function_num		= FUNC_TA_TO_PREPARE,
	.next			= &ta_to_prepare,
};

/* afc_condition_checklist[ATTACHED_DEV_AFC_CHARGER_PREPARE_MUIC] */
muic_afc_data_t prepare_to_qc_prepare = {
	.new_dev		= ATTACHED_DEV_QC_CHARGER_PREPARE_MUIC,
	.afc_name		= "QC charger Prepare",
	.afc_irq		= MUIC_AFC_IRQ_MPNACK,
	.status_vbadc		= VBADC_DONTCARE,
	.status_vdnmon          = VDNMON_DONTCARE,
	.function_num		= FUNC_PREPARE_TO_QC_PREPARE,
	.next			= &prepare_dupli_to_afc_9v,
};

muic_afc_data_t prepare_to_afc_5v = {
	.new_dev		= ATTACHED_DEV_AFC_CHARGER_5V_MUIC,
	.afc_name		= "AFC charger 5V",
	.afc_irq		= MUIC_AFC_IRQ_VBADC,
	.status_vbadc		= VBADC_AFC_5V,
	.status_vdnmon          = VDNMON_DONTCARE,
	.function_num		= FUNC_PREPARE_TO_AFC_5V,
	.next			= &prepare_to_afc_5v,
};

muic_afc_data_t prepare_to_prepare_dupli = {
	.new_dev		= ATTACHED_DEV_AFC_CHARGER_PREPARE_DUPLI_MUIC,
	.afc_name		= "AFC charger prepare (mrxrdy)",
	.afc_irq		= MUIC_AFC_IRQ_MRXRDY,
	.status_vbadc		= VBADC_DONTCARE,
	.status_vdnmon          = VDNMON_DONTCARE,
	.function_num		= FUNC_PREPARE_TO_PREPARE_DUPLI,
	.next			= &prepare_to_qc_prepare,
};

muic_afc_data_t prepare_dupli_to_prepare_dupli = {
	.new_dev		= ATTACHED_DEV_AFC_CHARGER_PREPARE_DUPLI_MUIC,
	.afc_name		= "AFC charger prepare (mrxrdy)",
	.afc_irq		= MUIC_AFC_IRQ_MRXRDY,
	.status_vbadc		= VBADC_DONTCARE,
	.status_vdnmon          = VDNMON_DONTCARE,
	.function_num		= FUNC_PREPARE_DUPLI_TO_PREPARE_DUPLI,
	.next			= &prepare_to_qc_prepare,
};

muic_afc_data_t prepare_dupli_to_afc_9v = {
	.new_dev		= ATTACHED_DEV_AFC_CHARGER_9V_MUIC,
	.afc_name		= "AFC charger 9V",
	.afc_irq		= MUIC_AFC_IRQ_VBADC,
	.status_vbadc		= VBADC_AFC_9V,
	.status_vdnmon          = VDNMON_DONTCARE,
	.function_num		= FUNC_PREPARE_DUPLI_TO_AFC_9V,
	.next			= &prepare_dupli_to_prepare_dupli,
};

muic_afc_data_t prepare_dupli_to_afc_err_v = {
	.new_dev		= ATTACHED_DEV_AFC_CHARGER_ERR_V_MUIC,
	.afc_name		= "AFC charger ERR V",
	.afc_irq		= MUIC_AFC_IRQ_VBADC,
	.status_vbadc		= VBADC_AFC_ERR_V,
	.status_vdnmon          = VDNMON_DONTCARE,
	.function_num		= FUNC_PREPARE_DUPLI_TO_AFC_ERR_V,
	.next			= &prepare_dupli_to_afc_9v,
};

muic_afc_data_t prepare_dupli_to_afc_5v = {
	.new_dev		= ATTACHED_DEV_AFC_CHARGER_5V_MUIC,
	.afc_name		= "AFC charger 5V",
	.afc_irq		= MUIC_AFC_IRQ_VBADC,
	.status_vbadc		= VBADC_AFC_5V,
	.status_vdnmon          = VDNMON_DONTCARE,
	.function_num		= FUNC_PREPARE_DUPLI_TO_AFC_5V,
	.next			= &prepare_dupli_to_afc_err_v,
};

muic_afc_data_t afc_5v_to_afc_9v = {
	.new_dev		= ATTACHED_DEV_AFC_CHARGER_5V_MUIC,
	.afc_name		= "AFC charger 9V",
	.afc_irq		= MUIC_AFC_IRQ_VBADC,
	.status_vbadc		= VBADC_AFC_5V,
	.status_vdnmon          = VDNMON_DONTCARE,
	.function_num		= FUNC_AFC_5V_TO_AFC_9V,
	.next			= &afc_5v_to_afc_9v,
};

muic_afc_data_t afc_5v_to_afc_err_v = {
	.new_dev		= ATTACHED_DEV_AFC_CHARGER_ERR_V_MUIC,
	.afc_name		= "AFC charger ERR V",
	.afc_irq		= MUIC_AFC_IRQ_VBADC,
	.status_vbadc		= VBADC_AFC_ERR_V,
	.status_vdnmon          = VDNMON_DONTCARE,
	.function_num		= FUNC_AFC_5V_TO_AFC_ERR_V,
	.next			= &afc_5v_to_afc_9v,
};

muic_afc_data_t afc_5v_to_afc_5v_dupli = {
	.new_dev		= ATTACHED_DEV_AFC_CHARGER_PREPARE_DUPLI_MUIC,
	.afc_name		= "AFC charger 5V (mrxrdy)",
	.afc_irq		= MUIC_AFC_IRQ_MRXRDY,
	.status_vbadc		= VBADC_AFC_5V,
	.status_vdnmon          = VDNMON_DONTCARE,
	.function_num		= FUNC_AFC_5V_TO_AFC_5V_DUPLI,
	.next			= &prepare_dupli_to_prepare_dupli,
};

muic_afc_data_t afc_5v_dupli_to_afc_5v_dupli = {
	.new_dev		= ATTACHED_DEV_AFC_CHARGER_5V_DUPLI_MUIC,
	.afc_name		= "AFC charger 5V (mrxrdy)",
	.afc_irq		= MUIC_AFC_IRQ_MRXRDY,
	.status_vbadc		= VBADC_AFC_5V,
	.status_vdnmon          = VDNMON_DONTCARE,
	.function_num		= FUNC_AFC_5V_DUPLI_TO_AFC_5V_DUPLI,
	.next			= &afc_5v_dupli_to_afc_5v_dupli,
};

muic_afc_data_t afc_5v_dupli_to_afc_9v = {
	.new_dev		= ATTACHED_DEV_AFC_CHARGER_9V_MUIC,
	.afc_name		= "AFC charger 9V",
	.afc_irq		= MUIC_AFC_IRQ_VBADC,
	.status_vbadc		= VBADC_AFC_9V,
	.status_vdnmon          = VDNMON_DONTCARE,
	.function_num		= FUNC_AFC_5V_DUPLI_TO_AFC_9V,
	.next			= &afc_5v_dupli_to_afc_5v_dupli,
};

muic_afc_data_t afc_5v_dupli_to_afc_err_v = {
	.new_dev		= ATTACHED_DEV_AFC_CHARGER_ERR_V_MUIC,
	.afc_name		= "AFC charger ERR V",
	.afc_irq		= MUIC_AFC_IRQ_VBADC,
	.status_vbadc		= VBADC_AFC_ERR_V,
	.status_vdnmon          = VDNMON_DONTCARE,
	.function_num		= FUNC_AFC_5V_DUPLI_TO_AFC_ERR_V,
	.next			= &afc_5v_dupli_to_afc_9v,
};

muic_afc_data_t afc_err_v_to_afc_9v = {
	.new_dev		= ATTACHED_DEV_AFC_CHARGER_9V_MUIC,
	.afc_name		= "AFC charger 9V",
	.afc_irq		= MUIC_AFC_IRQ_VBADC,
	.status_vbadc		= VBADC_AFC_9V,
	.status_vdnmon          = VDNMON_DONTCARE,
	.function_num		= FUNC_AFC_ERR_V_TO_AFC_9V,
	.next			= &afc_err_v_to_afc_9v,
};

muic_afc_data_t afc_err_v_to_afc_err_v_dupli = {
	.new_dev		= ATTACHED_DEV_AFC_CHARGER_ERR_V_DUPLI_MUIC,
	.afc_name		= "AFC charger ERR V (mrxrdy)",
	.afc_irq		= MUIC_AFC_IRQ_MRXRDY,
	.status_vbadc		= VBADC_AFC_ERR_V,
	.status_vdnmon          = VDNMON_DONTCARE,
	.function_num		= FUNC_AFC_ERR_V_TO_AFC_ERR_V_DUPLI,
	.next			= &afc_err_v_to_afc_9v,
};

muic_afc_data_t afc_err_v_dupli_to_afc_9v = {
	.new_dev		= ATTACHED_DEV_AFC_CHARGER_9V_MUIC,
	.afc_name		= "AFC charger 9V",
	.afc_irq		= MUIC_AFC_IRQ_VBADC,
	.status_vbadc		= VBADC_AFC_9V,
	.status_vdnmon          = VDNMON_DONTCARE,
	.function_num		= FUNC_AFC_ERR_V_DUPLI_TO_AFC_9V,
	.next			= &afc_err_v_dupli_to_afc_9v,
};

muic_afc_data_t afc_err_v_dupli_to_afc_err_v_dupli = {
	.new_dev		= ATTACHED_DEV_AFC_CHARGER_ERR_V_DUPLI_MUIC,
	.afc_name		= "AFC charger ERR V (mrxrdy)",
	.afc_irq		= MUIC_AFC_IRQ_MRXRDY,
	.status_vbadc		= VBADC_AFC_ERR_V,
	.status_vdnmon          = VDNMON_DONTCARE,
	.function_num	 	= FUNC_AFC_ERR_V_DUPLI_TO_AFC_ERR_V_DUPLI,
	.next			= &afc_err_v_dupli_to_afc_9v,
};

muic_afc_data_t qc_prepare_to_qc_9v = {
	.new_dev		= ATTACHED_DEV_QC_CHARGER_9V_MUIC,
	.afc_name		= "QC charger 9V",
	.afc_irq		= MUIC_AFC_IRQ_VBADC,
	.status_vbadc		= VBADC_QC_9V,
	.status_vdnmon          = VDNMON_DONTCARE,
	.function_num		= FUNC_QC_PREPARE_TO_QC_9V,
	.next			= &qc_prepare_to_qc_9v,
};

muic_afc_data_t qc_9v_to_qc_9v = {
	.new_dev		= ATTACHED_DEV_QC_CHARGER_9V_MUIC,
	.afc_name		= "QC charger 9V",
	.afc_irq		= MUIC_AFC_IRQ_DONTCARE,
	.status_vbadc		= VBADC_QC_9V,
	.status_vdnmon          = VDNMON_DONTCARE,
	.function_num		= FUNC_QC_PREPARE_TO_QC_9V,
	.next			= &qc_9v_to_qc_9v,
};

/* afc_condition_checklist[ATTACHED_DEV_AFC_CHARGER_9V_MUIC] */
muic_afc_data_t afc_9v_to_afc_5v = {
	.new_dev		= ATTACHED_DEV_AFC_CHARGER_5V_MUIC,
	.afc_name		= "AFC charger 5V",
	.afc_irq		= MUIC_AFC_IRQ_VBADC,//check only mxrx ready
	.status_vbadc		= VBADC_AFC_5V,
	.status_vdnmon          = VDNMON_DONTCARE,
	.function_num		= FUNC_AFC_9V_TO_AFC_5V,
	.next			= &afc_5v_to_afc_5v_dupli,
};

/* afc_condition_checklist[ATTACHED_DEV_AFC_CHARGER_9V_MUIC] */
muic_afc_data_t afc_9v_to_afc_9v = {
	.new_dev		= ATTACHED_DEV_AFC_CHARGER_9V_MUIC,
	.afc_name		= "AFC charger 9V",
	.afc_irq		= MUIC_AFC_IRQ_DONTCARE,
	.status_vbadc		= VBADC_AFC_9V,
	.status_vdnmon          = VDNMON_DONTCARE,
	.function_num		= FUNC_AFC_9V_TO_AFC_9V,
	.next			= &afc_9v_to_afc_5v,
};

muic_afc_data_t		*afc_condition_checklist[ATTACHED_DEV_NUM] = {
	[ATTACHED_DEV_TA_MUIC]			= &ta_to_prepare,
	[ATTACHED_DEV_AFC_CHARGER_PREPARE_MUIC]	= &prepare_to_prepare_dupli,
	[ATTACHED_DEV_AFC_CHARGER_PREPARE_DUPLI_MUIC] = &prepare_dupli_to_afc_5v,
	[ATTACHED_DEV_AFC_CHARGER_5V_MUIC]	= &afc_5v_to_afc_5v_dupli,
	[ATTACHED_DEV_AFC_CHARGER_5V_DUPLI_MUIC]= &afc_5v_dupli_to_afc_err_v,
	[ATTACHED_DEV_AFC_CHARGER_ERR_V_MUIC]	= &afc_err_v_to_afc_err_v_dupli,
	[ATTACHED_DEV_AFC_CHARGER_ERR_V_DUPLI_MUIC] = &afc_err_v_dupli_to_afc_err_v_dupli,
	[ATTACHED_DEV_AFC_CHARGER_9V_MUIC]	= &afc_9v_to_afc_9v,
	[ATTACHED_DEV_QC_CHARGER_PREPARE_MUIC]	= &qc_prepare_to_qc_9v,
	[ATTACHED_DEV_QC_CHARGER_9V_MUIC]	= &qc_9v_to_qc_9v,
};

struct afc_init_data_s {
	struct work_struct muic_afc_init_work;
	struct s2mu004_muic_data *muic_data;
};
struct afc_init_data_s afc_init_data;

bool muic_check_is_hv_dev(struct s2mu004_muic_data *muic_data)
{
	bool ret = false;

	switch (muic_data->attached_dev) {
	case ATTACHED_DEV_AFC_CHARGER_PREPARE_MUIC:
	case ATTACHED_DEV_AFC_CHARGER_PREPARE_DUPLI_MUIC:
	case ATTACHED_DEV_AFC_CHARGER_5V_MUIC:
	case ATTACHED_DEV_AFC_CHARGER_5V_DUPLI_MUIC:
	case ATTACHED_DEV_AFC_CHARGER_9V_MUIC:
	case ATTACHED_DEV_AFC_CHARGER_ERR_V_MUIC:
	case ATTACHED_DEV_AFC_CHARGER_ERR_V_DUPLI_MUIC:
	case ATTACHED_DEV_HV_ID_ERR_UNDEFINED_MUIC:
	case ATTACHED_DEV_HV_ID_ERR_UNSUPPORTED_MUIC:
	case ATTACHED_DEV_HV_ID_ERR_SUPPORTED_MUIC:
		ret = true;
		break;
	default:
		ret = false;
		break;
	}

	if (debug_en_checklist)
		pr_info("%s:%s attached_dev(%d)[%c]\n", MUIC_HV_DEV_NAME,
			__func__, muic_data->attached_dev, (ret ? 'T' : 'F'));

	return ret;
}

muic_attached_dev_t hv_muic_check_id_err
	(struct s2mu004_muic_data *muic_data, muic_attached_dev_t new_dev)
{
	muic_attached_dev_t after_new_dev = new_dev;

	if (!muic_check_is_hv_dev(muic_data))
		goto out;

	switch(new_dev) {
	case ATTACHED_DEV_TA_MUIC:
		pr_info("%s:%s cannot change HV(%d)->TA(%d)!\n", MUIC_DEV_NAME,
			__func__, muic_data->attached_dev, new_dev);
		after_new_dev = muic_data->attached_dev;
		break;
	case ATTACHED_DEV_UNDEFINED_CHARGING_MUIC:
		pr_info("%s:%s HV ID Err - Undefined\n", MUIC_DEV_NAME, __func__);
		after_new_dev = ATTACHED_DEV_HV_ID_ERR_UNDEFINED_MUIC;
		break;
	case ATTACHED_DEV_UNSUPPORTED_ID_VB_MUIC:
		pr_info("%s:%s HV ID Err - Unsupported\n", MUIC_DEV_NAME, __func__);
		after_new_dev = ATTACHED_DEV_HV_ID_ERR_UNSUPPORTED_MUIC;
		break;
	default:
		pr_info("%s:%s HV ID Err - Supported\n", MUIC_DEV_NAME, __func__);
		after_new_dev = ATTACHED_DEV_HV_ID_ERR_SUPPORTED_MUIC;
		break;
	}
out:
	return after_new_dev;
}

static int s2mu004_hv_muic_write_reg(struct i2c_client *i2c, u8 reg, u8 value)
{
	u8 before_val, after_val;
	int ret;

	s2mu004_read_reg(i2c, reg, &before_val);
	ret = s2mu004_write_reg(i2c, reg, value);
	s2mu004_read_reg(i2c, reg, &after_val);

	pr_info("%s:%s reg[0x%02x] = [0x%02x] + [0x%02x] -> [0x%02x]\n",
		MUIC_HV_DEV_NAME, __func__, reg, before_val, value, after_val);
	return ret;
}

int s2mu004_muic_hv_update_reg(struct i2c_client *i2c,
	const u8 reg, const u8 val, const u8 mask, const bool debug_en)
{
	u8 before_val, new_val, after_val;
	int ret = 0;

	ret = s2mu004_read_reg(i2c, reg, &before_val);
	if (ret)
		pr_err("%s:%s err read REG(0x%02x) [%d] \n", MUIC_DEV_NAME,
				__func__, reg, ret);

	new_val = (val & mask) | (before_val & (~mask));

	if (before_val ^ new_val) {
		ret = s2mu004_hv_muic_write_reg(i2c, reg, new_val);
		if (ret)
			pr_err("%s:%s err write REG(0x%02x) [%d]\n",
					MUIC_DEV_NAME, __func__, reg, ret);
	} else if (debug_en) {
		pr_info("%s:%s REG(0x%02x): already [0x%02x], don't write reg\n",
				MUIC_DEV_NAME, __func__, reg, before_val);
		goto out;
	}

	if (debug_en) {
		ret = s2mu004_read_reg(i2c, reg, &after_val);
		if (ret < 0)
			pr_err("%s:%s err read REG(0x%02x) [%d]\n",
					MUIC_DEV_NAME, __func__, reg, ret);

		pr_info("%s:%s REG(0x%02x): [0x%02x]+[0x%02x:0x%02x]=[0x%02x]\n",
				MUIC_DEV_NAME, __func__, reg, before_val,
				val, mask, after_val);
	}

out:
	return ret;
}

void s2mu004_hv_muic_reset_hvcontrol_reg(struct s2mu004_muic_data *muic_data)
{
	struct i2c_client *i2c = muic_data->i2c;

	s2mu004_hv_muic_write_reg(i2c, 0x4b, 0x00);
	s2mu004_hv_muic_write_reg(i2c, 0x49, 0x00);
	s2mu004_hv_muic_write_reg(i2c, 0x4a, 0x00);
	s2mu004_hv_muic_write_reg(i2c, 0x5f, 0x01);

	cancel_delayed_work(&muic_data->afc_send_mpnack);
}

void s2mu004_muic_set_afc_ready(struct s2mu004_muic_data *muic_data, bool value)
{
	bool before, after;
	pr_err("[DEBUG] %s(%d) , value %d \n" , __func__, __LINE__, value);
	before = muic_data->is_afc_muic_ready;
	muic_data->is_afc_muic_ready = value;
	after = muic_data->is_afc_muic_ready;

	pr_info("%s:%s afc_muic_ready[%d->%d]\n", MUIC_DEV_NAME, __func__, before, after);
}

static int s2mu004_hv_muic_state_maintain(struct s2mu004_muic_data *muic_data)
{
	int ret = 0;

	pr_info("%s:%s \n", MUIC_HV_DEV_NAME, __func__);

	if (muic_data->attached_dev == ATTACHED_DEV_NONE_MUIC) {
		pr_info("%s:%s Detached(%d), need to check after\n", MUIC_HV_DEV_NAME,
				__func__, muic_data->attached_dev);
		return ret;
	}

	return ret;
}

static void s2mu004_mpnack_irq_mask(struct s2mu004_muic_data *muic_data, int enable)
{
	pr_info("%s: irq enable: %d\n", __func__, enable);
	if (enable)
		s2mu004_muic_hv_update_reg(muic_data->i2c, 0x05, 0x00, 0x08, 0);
	else
		s2mu004_muic_hv_update_reg(muic_data->i2c, 0x05, 0x08, 0x08, 0);
}

static void s2mu004_hv_muic_set_afc_after_prepare
					(struct s2mu004_muic_data *muic_data)
{
	pr_info("%s:%s HV charger is detected\n", MUIC_HV_DEV_NAME, __func__);

	muic_data->retry_cnt = 0;
	s2mu004_mpnack_irq_mask(muic_data, 0);
	s2mu004_hv_muic_write_reg(muic_data->i2c, 0x5f, 0x05);
	s2mu004_hv_muic_write_reg(muic_data->i2c, 0x4A, 0x0e);
	schedule_delayed_work(&muic_data->afc_send_mpnack, msecs_to_jiffies(2000));
	schedule_delayed_work(&muic_data->afc_control_ping_retry, msecs_to_jiffies(50));
}

void s2mu004_muic_afc_after_prepare(struct work_struct *work)
{
	struct s2mu004_muic_data *muic_data =
		container_of(work, struct s2mu004_muic_data, afc_after_prepare.work);
	u8 vbvolt = 0;

	pr_err("[DEBUG] %s(%d) \n " , __func__, __LINE__);
	
	msleep(100);
	s2mu004_read_reg(muic_data->i2c, S2MU004_REG_MUIC_DEVICE_APPLE, &vbvolt);
	vbvolt = !!(vbvolt & DEV_TYPE_APPLE_VBUS_WAKEUP);

	pr_info("[DEBUG] %s vbvolt=%d\n " , __func__, vbvolt);

	if(vbvolt) {
		mutex_lock(&muic_data->afc_mutex);
		s2mu004_hv_muic_set_afc_after_prepare(muic_data);
		mutex_unlock(&muic_data->afc_mutex);
	}
}

#define RETRY_QC_CNT 3
void s2mu004_muic_afc_qc_retry(struct work_struct *work)
{
	struct s2mu004_muic_data *muic_data =
		container_of(work, struct s2mu004_muic_data, afc_qc_retry.work);
	pr_err("[DEBUG] %s retry_qc_cnt : (%d) \n " , __func__, muic_data->retry_qc_cnt);
	if (muic_data->retry_qc_cnt < RETRY_QC_CNT) {
		muic_data->retry_qc_cnt++;
		s2mu004_hv_muic_write_reg(muic_data->i2c, 0x49, 0x00); //3
		msleep(10);
		s2mu004_hv_muic_write_reg(muic_data->i2c, 0x49, 0xbd); //3
		schedule_delayed_work(&muic_data->afc_qc_retry, msecs_to_jiffies(50));
	} else {
		muic_data->retry_qc_cnt = 0;
		muic_data->qc_prepare = 0;
	}

}

#define RETRY_PING_CNT 20
static void s2mu004_muic_afc_control_ping_retry(struct work_struct *work)
{
	struct s2mu004_muic_data *muic_data =
		container_of(work, struct s2mu004_muic_data, afc_control_ping_retry.work);
	pr_err("[DEBUG] %s(%d) retry_cnt : %d  \n " , __func__, __LINE__, muic_data->retry_cnt);

	if (muic_data->retry_cnt <  RETRY_PING_CNT) {
		muic_data->retry_cnt++;
		s2mu004_hv_muic_write_reg(muic_data->i2c, 0x4A, 0x0e);
		schedule_delayed_work(&muic_data->afc_control_ping_retry, msecs_to_jiffies(50));
	} else {
		muic_data->retry_cnt = 0;
		s2mu004_mpnack_irq_mask(muic_data, 1); /* enable mpnack irq */
		s2mu004_hv_muic_write_reg(muic_data->i2c, 0x4A, 0x0e);
	}
}

static void s2mu004_hv_muic_afc_control_ping
		(struct s2mu004_muic_data *muic_data, bool ping_continue)
{
	int ret;

	pr_err("%s:%s control ping[%d, %c]\n", MUIC_HV_DEV_NAME, __func__,
				muic_data->afc_count, ping_continue ? 'T' : 'F');
	if (ping_continue) {
		msleep(30);
		ret = s2mu004_hv_muic_write_reg(muic_data->i2c, 0x4A, 0x0e); //3
		schedule_delayed_work(&muic_data->afc_send_mpnack, msecs_to_jiffies(2000));
		schedule_delayed_work(&muic_data->afc_control_ping_retry, msecs_to_jiffies(50));
	}
}

static int s2mu004_hv_muic_handle_attach
		(struct s2mu004_muic_data *muic_data, const muic_afc_data_t *new_afc_data)
{
	int ret = 0;
	bool noti = true;
	muic_attached_dev_t	new_dev	= new_afc_data->new_dev;

	pr_err("%s:%s , new_afc_data->function_num %d  \n", MUIC_HV_DEV_NAME, __func__, new_afc_data->function_num );

	if (muic_data->is_charger_ready == false) {
		if (new_afc_data->new_dev == ATTACHED_DEV_AFC_CHARGER_PREPARE_MUIC) {
			muic_data->is_afc_muic_prepare = true;
			pr_info("%s:%s is_charger_ready[%c], is_afc_muic_prepare[%c]\n",
				MUIC_HV_DEV_NAME, __func__,
				(muic_data->is_charger_ready ? 'T' : 'F'),
				(muic_data->is_afc_muic_prepare ? 'T' : 'F'));

			return ret;
		}
		pr_info("%s:%s is_charger_ready[%c], just return\n", MUIC_HV_DEV_NAME,
			__func__, (muic_data->is_charger_ready ? 'T' : 'F'));
		return ret;
	}

	switch (new_afc_data->function_num) {
	case FUNC_TA_TO_PREPARE:
		schedule_delayed_work(&muic_data->afc_after_prepare, msecs_to_jiffies(60));
		muic_data->afc_count = 0;
		break;
	case FUNC_PREPARE_TO_PREPARE_DUPLI:
		pr_err("[DEBUG] %s(%d) \n " , __func__, __LINE__);
		muic_data->afc_count++;
		if (muic_data->afc_count < 3) {
			s2mu004_hv_muic_afc_control_ping(muic_data, true);
			noti = false;
		}
		break;
	case FUNC_PREPARE_TO_AFC_5V:
		if (muic_data->afc_count > AFC_CHARGER_WA_PING) {
			s2mu004_hv_muic_afc_control_ping(muic_data, false);
		} else {
			s2mu004_hv_muic_afc_control_ping(muic_data, true);
			noti = false;
		}
		break;
	case FUNC_PREPARE_TO_QC_PREPARE:
		muic_data->afc_count = 0;
		muic_data->qc_prepare = 1;
		msleep(60);
		s2mu004_hv_muic_write_reg(muic_data->i2c, 0x5f, 0x01); //3
		s2mu004_hv_muic_write_reg(muic_data->i2c, 0x49, 0xbd); //3
		schedule_delayed_work(&muic_data->afc_qc_retry, msecs_to_jiffies(50));
		break;
	case FUNC_PREPARE_DUPLI_TO_PREPARE_DUPLI:
		pr_err("[DEBUG] %s(%d)dupli, dupli \n " , __func__, __LINE__);
		muic_data->afc_count++;
		if (muic_data->afc_count < 3) {
			s2mu004_hv_muic_afc_control_ping(muic_data, true);
			noti = false;
		}
		break;
	case FUNC_PREPARE_DUPLI_TO_AFC_5V:
#if 0
		// should verify vbus 9V -> 0x48[3:0] b0100
#ifdef CONFIG_MUIC_HV_FORCE_LIMIT
		if(muic_data->pdata->hv_sel)
		{
			s2mu004_hv_muic_afc_control_ping(muic_data, false);
//			s2mu004_hv_muic_adcmode_oneshot(muic_data);
		}else {
			if(muic_data->afc_count > AFC_CHARGER_WA_PING) {
				s2mu004_hv_muic_afc_control_ping(muic_data, false);
			} else {
				s2mu004_hv_muic_afc_control_ping(muic_data, true);
				noti = false;
			}
		}
#else
		if(muic_data->afc_count > AFC_CHARGER_WA_PING) {
			s2mu004_hv_muic_afc_control_ping(muic_data, false);
		} else {
			s2mu004_hv_muic_afc_control_ping(muic_data, true);
			noti = false;
		}
#endif
#endif
		break;
	case FUNC_PREPARE_DUPLI_TO_AFC_ERR_V:
		if (muic_data->afc_count > AFC_CHARGER_WA_PING) {
			s2mu004_hv_muic_afc_control_ping(muic_data, false);
		} else {
			s2mu004_hv_muic_afc_control_ping(muic_data, true);
			noti = false;
		}
		break;
	case FUNC_PREPARE_DUPLI_TO_AFC_9V:
		s2mu004_hv_muic_afc_control_ping(muic_data, false);
	//	muic_data->afc_count = 0;
		break;
	case FUNC_AFC_5V_TO_AFC_5V_DUPLI:
		/* attached_dev is changed. MPING Missing did not happened
		 * Cancel delayed work */
		pr_info("%s:%s cancel_delayed_work(dev %d), Mping missing wa\n",
			MUIC_HV_DEV_NAME, __func__, new_dev);
		muic_data->afc_count++;
		if (muic_data->afc_count < 3) {
			s2mu004_hv_muic_afc_control_ping(muic_data, true);
			noti = false;
		}
		break;
	case FUNC_AFC_5V_TO_AFC_ERR_V:
		/* attached_dev is changed. MPING Missing did not happened
		 * Cancel delayed work */
		pr_info("%s:%s cancel_delayed_work(dev %d), Mping missing wa\n",
			MUIC_HV_DEV_NAME, __func__, new_dev);
		if (muic_data->afc_count > AFC_CHARGER_WA_PING) {
			s2mu004_hv_muic_afc_control_ping(muic_data, false);
		} else {
			s2mu004_hv_muic_afc_control_ping(muic_data, true);
			noti = false;
		}
		break;
	case FUNC_AFC_5V_TO_AFC_9V:
		/* attached_dev is changed. MPING Missing did not happened
		 * Cancel delayed work */
		pr_info("%s:%s cancel_delayed_work(dev %d), Mping missing wa\n",
			MUIC_HV_DEV_NAME, __func__, new_dev);
		s2mu004_hv_muic_write_reg(muic_data->i2c, 0x05, 0x00); //3

		break;
	case FUNC_AFC_5V_DUPLI_TO_AFC_5V_DUPLI:
		muic_data->afc_count++;
		if (muic_data->afc_count > AFC_CHARGER_WA_PING) {
			s2mu004_hv_muic_afc_control_ping(muic_data, false);
		} else {
			s2mu004_hv_muic_afc_control_ping(muic_data, true);
			noti = false;
		}
		break;
	case FUNC_AFC_5V_DUPLI_TO_AFC_ERR_V:
		if (muic_data->afc_count > AFC_CHARGER_WA_PING) {
			s2mu004_hv_muic_afc_control_ping(muic_data, false);
		} else {
			s2mu004_hv_muic_afc_control_ping(muic_data, true);
			noti = false;
		}
		break;
	case FUNC_AFC_5V_DUPLI_TO_AFC_9V:
		s2mu004_hv_muic_afc_control_ping(muic_data, false);
#ifdef CONFIG_MUIC_HV_FORCE_LIMIT
			if(muic_data->pdata->silent_chg_change_state == SILENT_CHG_CHANGING)
				noti = false;
#endif
		break;
	case FUNC_AFC_ERR_V_TO_AFC_ERR_V_DUPLI:
		muic_data->afc_count++;
		if (muic_data->afc_count > AFC_CHARGER_WA_PING) {
			s2mu004_hv_muic_afc_control_ping(muic_data, false);
		} else {
			s2mu004_hv_muic_afc_control_ping(muic_data, true);
			noti = false;
		}
		break;
	case FUNC_AFC_ERR_V_TO_AFC_9V:
		s2mu004_hv_muic_afc_control_ping(muic_data, false);
		break;
	case FUNC_AFC_ERR_V_DUPLI_TO_AFC_ERR_V_DUPLI:
		muic_data->afc_count++;
		if (muic_data->afc_count > AFC_CHARGER_WA_PING) {
			s2mu004_hv_muic_afc_control_ping(muic_data, false);
		} else {
			s2mu004_hv_muic_afc_control_ping(muic_data, true);
			noti = false;
		}
		break;
	case FUNC_AFC_ERR_V_DUPLI_TO_AFC_9V:
		s2mu004_hv_muic_afc_control_ping(muic_data, false);
		break;
	case FUNC_AFC_9V_TO_AFC_5V:
	//	muic_data->afc_count++;
	//	s2mu004_hv_muic_afc_control_ping(muic_data, false);
		break;
	case FUNC_AFC_9V_TO_AFC_9V:
#ifdef CONFIG_MUIC_HV_FORCE_LIMIT
		muic_data->afc_count++;
		if (muic_data->afc_count > AFC_CHARGER_WA_PING) {
			s2mu004_hv_muic_afc_control_ping(muic_data, false);
//			s2mu004_hv_muic_adcmode_oneshot(muic_data);
		} else {
			pr_info("dummy int called [%d]\n", muic_data->afc_count);
		}
#else
		muic_data->afc_count++;
		if (muic_data->afc_count < AFC_CHARGER_WA_PING) {
			s2mu004_hv_muic_afc_control_ping(muic_data, true);
		}
#endif
		break;
#if 0 /* remove qc */
	case FUNC_QC_PREPARE_TO_QC_5V:
		if (muic_data->is_qc_vb_settle == true)
//			s2mu004_hv_muic_adcmode_oneshot(muic_data);
		else
			noti = false;
		break;
#endif
	case FUNC_QC_PREPARE_TO_QC_9V:
//		muic_data->is_qc_vb_settle = true;
//		s2mu004_hv_muic_adcmode_oneshot(muic_data);
		break;
#if 0
	case FUNC_QC_5V_TO_QC_5V:
		if (muic_data->is_qc_vb_settle == true)
//			s2mu004_hv_muic_adcmode_oneshot(muic_data);
		else
			noti = false;
		break;
	case FUNC_QC_5V_TO_QC_9V:
		muic_data->is_qc_vb_settle = true;
//		s2mu004_hv_muic_adcmode_oneshot(muic_data);
#ifdef CONFIG_MUIC_HV_FORCE_LIMIT
		if(muic_data->pdata->silent_chg_change_state == SILENT_CHG_CHANGING)
			noti = false;
#endif
		break;
	case FUNC_QC_9V_TO_QC_9V:
#ifdef CONFIG_MUIC_HV_FORCE_LIMIT
		if(!muic_data->pdata->hv_sel)
#endif
//		s2mu004_hv_muic_adcmode_oneshot(muic_data);
		break;
#ifdef CONFIG_MUIC_HV_FORCE_LIMIT
	case FUNC_QC_9V_TO_QC_5V:
//		s2mu004_hv_muic_adcmode_oneshot(muic_data);
		break;
#endif
#endif /* remove qc */
	default:
		pr_warn("%s:%s undefinded hv function num(%d)\n", MUIC_HV_DEV_NAME,
					__func__, new_afc_data->function_num);
		ret = -ESRCH;
		goto out;
	}

#if defined(CONFIG_MUIC_NOTIFIER)
	if (muic_data->attached_dev == new_dev)
		noti = false;
	else if (new_dev == ATTACHED_DEV_AFC_CHARGER_PREPARE_DUPLI_MUIC || \
		new_dev == ATTACHED_DEV_AFC_CHARGER_5V_DUPLI_MUIC || \
		new_dev == ATTACHED_DEV_AFC_CHARGER_ERR_V_DUPLI_MUIC)
		noti = false;

	if (noti){
		if(new_dev == ATTACHED_DEV_AFC_CHARGER_5V_MUIC){
			pr_err("%s: AFC CHARGER 5V MUIC delay cable noti\n " , __func__);
			//queue_delayed_work(muic_data->cable_type_wq,
			//		&muic_data->afc_cable_type_work, msecs_to_jiffies(80));
			schedule_delayed_work(&muic_data->afc_cable_type_work, msecs_to_jiffies(80));
		} else{
			muic_notifier_attach_attached_dev(new_dev);
		}
	}
#endif /* CONFIG_MUIC_NOTIFIER */

	muic_data->attached_dev = new_dev;
out:
	return ret;
}

static bool muic_check_hv_irq
			(struct s2mu004_muic_data *muic_data,
			const muic_afc_data_t *tmp_afc_data, int irq)
{
	int afc_irq = 0;
	bool ret = false;

	pr_err("%s(%d) irq %d , irq_dnres %d, irq_mrxrdy %d, irq_vbadc %d irq_mpnack %d irq_vdnmon %d\n",__func__, __LINE__,
			irq , muic_data->irq_dnres, muic_data->irq_mrxrdy, muic_data->irq_vbadc, muic_data->irq_mpnack, muic_data->irq_vdnmon);
	/* change irq num to muic_afc_irq_t */

	if (irq == muic_data->irq_vbadc)
		afc_irq = MUIC_AFC_IRQ_VBADC;
	else if (irq == muic_data->irq_mrxrdy)
		afc_irq = MUIC_AFC_IRQ_MRXRDY;
	else if(irq == muic_data->irq_vdnmon)
		afc_irq = MUIC_AFC_IRQ_VDNMON;
	else if (irq == muic_data->irq_mpnack)
		afc_irq = MUIC_AFC_IRQ_MPNACK;
	else {
		pr_err("%s:%s cannot find irq #(%d)\n", MUIC_HV_DEV_NAME, __func__, irq);
		ret = false;
		goto out;
	}

	pr_err("%s(%d) tmp_afc_data->afc_irq %d , afc_irq %d \n",__func__, __LINE__,
			tmp_afc_data->afc_irq , afc_irq);

	if (tmp_afc_data->afc_irq == afc_irq) {
		ret = true;
		goto out;
	}

	if (tmp_afc_data->afc_irq == MUIC_AFC_IRQ_DONTCARE) {
		ret = true;
		goto out;
	}

out:
	if (debug_en_checklist) {
		pr_info("%s:%s check_data dev(%d) irq(%d:%d) ret(%c)\n",
				MUIC_HV_DEV_NAME, __func__, tmp_afc_data->new_dev,
				tmp_afc_data->afc_irq, afc_irq, ret ? 'T' : 'F');
	}

	return ret;
}
#if 0
static bool muic_check_hvcontrol1_dpdnvden
			(const muic_afc_data_t *tmp_afc_data, u8 dpdnvden)
{
	bool ret = false;

	if (tmp_afc_data->hvcontrol1_dpdnvden == dpdnvden) {
		ret = true;
		goto out;
	}

	if (tmp_afc_data->hvcontrol1_dpdnvden == DPDNVDEN_DONTCARE) {
		ret = true;
		goto out;
	}

out:
	if (debug_en_checklist) {
		pr_info("%s:%s check_data dev(%d) dpdnvden(0x%x:0x%x) ret(%c)\n",
				MUIC_HV_DEV_NAME, __func__, tmp_afc_data->new_dev,
				tmp_afc_data->hvcontrol1_dpdnvden, dpdnvden,
				ret ? 'T' : 'F');
	}

	return ret;
}
#endif
static bool muic_check_status_vbadc
			(const muic_afc_data_t *tmp_afc_data, u8 vbadc)
{
	bool ret = false;

	if (tmp_afc_data->status_vbadc == vbadc) {
		ret = true;
		goto out;
	}

	if (tmp_afc_data->status_vbadc == VBADC_AFC_5V) {
		switch (vbadc) {
		case VBADC_5_3V:
		case VBADC_5_7V_6_3V:
			ret = true;
			goto out;
		default:
			break;
		}
	}

	if (tmp_afc_data->status_vbadc == VBADC_AFC_9V) {
		switch (vbadc) {
		case VBADC_8_7V_9_3V:
		case VBADC_9_7V_10_3V:
			ret = true;
			goto out;
		default:
			break;
		}
	}

	if (tmp_afc_data->status_vbadc == VBADC_AFC_ERR_V_NOT_0) {
		switch (vbadc) {
		case VBADC_7_7V_8_3V:
		case VBADC_10_7V_11_3V:
		case VBADC_11_7V_12_3V:
		case VBADC_12_7V_13_3V:
		case VBADC_13_7V_14_3V:
		case VBADC_14_7V_15_3V:
		case VBADC_15_7V_16_3V:
		case VBADC_16_7V_17_3V:
		case VBADC_17_7V_18_3V:
		case VBADC_18_7V_19_3V:
			ret = true;
			goto out;
		default:
			break;
		}
	}

	if (tmp_afc_data->status_vbadc == VBADC_AFC_ERR_V) {
		switch (vbadc) {
		case VBADC_7_7V_8_3V:
		case VBADC_10_7V_11_3V:
		case VBADC_11_7V_12_3V:
		case VBADC_12_7V_13_3V:
		case VBADC_13_7V_14_3V:
		case VBADC_14_7V_15_3V:
		case VBADC_15_7V_16_3V:
		case VBADC_16_7V_17_3V:
		case VBADC_17_7V_18_3V:
		case VBADC_18_7V_19_3V:
		case VBADC_19_7V:
			ret = true;
			goto out;
		default:
			break;
		}
	}
#if 0
	if (tmp_afc_data->status_vbadc == VBADC_QC_5V) {
		switch (vbadc) {
		case VBADC_4V_5V:
		case VBADC_5V_6V:
			ret = true;
			goto out;
		default:
			break;
		}
	}

	if (tmp_afc_data->status_vbadc == VBADC_QC_9V) {
		switch (vbadc) {
		case VBADC_6V_7V:
		case VBADC_7V_8V:
		case VBADC_8V_9V:
		case VBADC_9V_10V:
			ret = true;
			goto out;
		default:
			break;
		}
	}
#endif
	if (tmp_afc_data->status_vbadc == VBADC_ANY) {
		switch (vbadc) {
		case VBADC_5_3V:
		case VBADC_5_7V_6_3V:
		case VBADC_6_7V_7_3V:
		case VBADC_7_7V_8_3V:
		case VBADC_8_7V_9_3V:
		case VBADC_9_7V_10_3V:
		case VBADC_10_7V_11_3V:
		case VBADC_11_7V_12_3V:
		case VBADC_12_7V_13_3V:
		case VBADC_13_7V_14_3V:
		case VBADC_14_7V_15_3V:
		case VBADC_15_7V_16_3V:
		case VBADC_16_7V_17_3V:
		case VBADC_17_7V_18_3V:
		case VBADC_18_7V_19_3V:
		case VBADC_19_7V:
			ret = true;
			goto out;
		default:
			break;
		}
	}

	if (tmp_afc_data->status_vbadc == VBADC_DONTCARE) {
		ret = true;
		goto out;
	}

out:
	if (debug_en_checklist) {
		pr_info("%s:%s check_data dev(%d) vbadc(0x%x:0x%x) ret(%c)\n",
				MUIC_HV_DEV_NAME, __func__, tmp_afc_data->new_dev,
				tmp_afc_data->status_vbadc, vbadc, ret ? 'T' : 'F');
	}

	return ret;
}

static bool muic_check_status_vdnmon
			(const muic_afc_data_t *tmp_afc_data, u8 vdnmon)
{
	bool ret = false;

	if (tmp_afc_data->status_vdnmon == vdnmon) {
		ret = true;
		goto out;
	}

	if (tmp_afc_data->status_vdnmon == VDNMON_DONTCARE) {
		ret = true;
		goto out;
	}

out:
	if (debug_en_checklist) {
		pr_info("%s:%s check_data dev(%d) vdnmon(0x%x:0x%x) ret(%c)\n",
				MUIC_HV_DEV_NAME, __func__, tmp_afc_data->new_dev,
				tmp_afc_data->status_vdnmon, vdnmon, ret ? 'T' : 'F');
	}

	return ret;
}

bool muic_check_dev_ta(struct s2mu004_muic_data *muic_data)
{
	u8 status1 = muic_data->status1;
	u8 status2 = muic_data->status2;
	u8 status4 = muic_data->status4;
	u8 adc, vbvolt, chgtyp;

	adc = status1 & ADC_MASK;
	vbvolt = status2 & DEV_TYPE_APPLE_VBUS_WAKEUP;
	//chgdetrun = status2 & STATUS2_CHGDETRUN_MASK;
	chgtyp = status4 & DEV_TYPE_DCPCHG;

	pr_err("[DEBUG] %s(%d) , adc %d \n" , __func__, __LINE__, adc);
	if (adc != ADC_OPEN) {
		s2mu004_muic_set_afc_ready(muic_data, false);
		return false;
	}
//	if (vbvolt == VB_LOW || chgdetrun == CHGDETRUN_TRUE || chgtyp != CHGTYP_DEDICATED_CHARGER) {
	if (vbvolt == 0 || chgtyp == 0) {
		s2mu004_muic_set_afc_ready(muic_data, false);
#if defined(CONFIG_MUIC_NOTIFIER)
		muic_notifier_detach_attached_dev(muic_data->attached_dev);
#endif
		muic_data->attached_dev = ATTACHED_DEV_NONE_MUIC;
		s2mu004_hv_muic_reset_hvcontrol_reg(muic_data);
		return false;
	}

	return true;
}

static void s2mu004_hv_muic_detect_dev(struct s2mu004_muic_data *muic_data, int irq)
{
	struct i2c_client *i2c = muic_data->i2c;
	const muic_afc_data_t *tmp_afc_data = afc_condition_checklist[muic_data->attached_dev];
#ifdef	CONFIG_MUIC_SUPPORT_CCIC
	muic_data_t *pmuic = (muic_data_t *)muic_data->ccic_data;
#endif
	int intr = MUIC_INTR_DETACH;
	int ret;
	int i;
	u8 status[4];
	u8 hvcontrol[2];
//	u8 vdnmon, dpdnvden, mpnack;
	u8	vdnmon, vbadc;
	bool flag_next = true;
	bool muic_dev_ta = false;

	pr_err("%s:%s irq(%d)\n", MUIC_HV_DEV_NAME, __func__, irq);

	if (tmp_afc_data == NULL) {
		pr_info("%s:%s non AFC Charger, just return!\n", MUIC_HV_DEV_NAME, __func__);
		return;
	}

	ret = s2mu004_read_reg(i2c, S2MU004_REG_MUIC_ADC, &status[0]);
	if (ret) {
		pr_err("%s:%s fail to read muic reg(%d)\n", MUIC_DEV_NAME, __func__, ret);
		return;
	}

	ret = s2mu004_read_reg(i2c, S2MU004_REG_MUIC_DEVICE_APPLE, &status[1]);
	if (ret) {
		pr_err("%s:%s fail to read muic reg(%d)\n", MUIC_DEV_NAME, __func__, ret);
		return;
	}

	ret = s2mu004_read_reg(i2c, S2MU004_REG_AFC_STATUS, &status[2]);
	if (ret) {
		pr_err("%s:%s fail to read muic reg(%d)\n", MUIC_DEV_NAME, __func__, ret);
		return;
	}

	ret = s2mu004_read_reg(i2c, S2MU004_REG_MUIC_CHG_TYPE, &status[3]);
	if (ret) {
		pr_err("%s:%s fail to read muic reg(%d)\n", MUIC_DEV_NAME, __func__, ret);
		return;
	}

	pr_err("%s:%s STATUS1:0x%02x, 2:0x%02x, 3:0x%02x\n", MUIC_DEV_NAME, __func__,
					status[0], status[1], status[2]);

	/* attached status */
	muic_data->status1 = status[0];
	muic_data->status2 = status[1];
	muic_data->status3 = status[2];
	muic_data->status4 = status[3];

	/* check TA type */
	muic_dev_ta = muic_check_dev_ta(muic_data);
	if (!muic_dev_ta) {
		pr_err("%s:%s device type is not TA!\n", MUIC_HV_DEV_NAME, __func__);
		return;
	}

	/* attached status */
//	mpnack = status[2] & STATUS_MPNACK_MASK;
	vdnmon = status[2] & STATUS_VDNMON_MASK;
	vbadc = status[2] & STATUS_VBADC_MASK;

	ret = s2mu004_bulk_read(i2c, S2MU004_REG_AFC_CTRL1, 2, hvcontrol);
	if (ret) {
		pr_err("%s:%s fail to read muic reg(%d)\n", MUIC_HV_DEV_NAME,
				__func__, ret);
		return;
	}

	pr_info("%s:%s HVCONTROL1:0x%02x, 2:0x%02x\n", MUIC_HV_DEV_NAME, __func__,
			hvcontrol[0], hvcontrol[1]);

	/* attached - control */
	muic_data->hvcontrol1 = hvcontrol[0];
	muic_data->hvcontrol2 = hvcontrol[1];

//	dpdnvden = hvcontrol[0] & HVCONTROL1_DPDNVDEN_MASK;

//	pr_info("%s:%s vdnmon:0x%x mpnack:0x%x vbadc:0x%x dpdnvden:0x%x\n",
//		MUIC_HV_DEV_NAME, __func__, vdnmon, mpnack, vbadc, dpdnvden);
	pr_info("%s:%s vbadc:0x%x \n", MUIC_HV_DEV_NAME, __func__, vbadc);

	for (i = 0; i < 10; i++, tmp_afc_data = tmp_afc_data->next) {

		if (!flag_next) {
			pr_info("%s:%s not found new_dev in afc_condition_checklist\n",
				MUIC_HV_DEV_NAME, __func__);
			break;
		}

		pr_err("%s:%s tmp_afc_data->name %s \n",
				MUIC_HV_DEV_NAME, __func__, tmp_afc_data->afc_name );
		if (tmp_afc_data->next == tmp_afc_data)
			flag_next = false;

		if (!(muic_check_hv_irq(muic_data, tmp_afc_data, irq)))
			continue;

//		if (!(muic_check_hvcontrol1_dpdnvden(tmp_afc_data, dpdnvden)))
//			continue;

		if (!(muic_check_status_vbadc(tmp_afc_data, vbadc)))
			continue;

		if(!(muic_check_status_vdnmon(tmp_afc_data, vdnmon)))
			continue;

		pr_err("%s:%s checklist match found at i(%d), %s(%d)\n",
			MUIC_HV_DEV_NAME, __func__, i, tmp_afc_data->afc_name,
			tmp_afc_data->new_dev);

		intr = MUIC_INTR_ATTACH;

		break;
	}

	if (intr == MUIC_INTR_ATTACH) {
		pr_err("%s:%s AFC ATTACHED\n", MUIC_HV_DEV_NAME, __func__);
		pr_err("%s:%s %d->%d\n", MUIC_HV_DEV_NAME, __func__,
				muic_data->attached_dev, tmp_afc_data->new_dev);
#ifdef	CONFIG_MUIC_SUPPORT_CCIC
		muic_set_legacy_dev(pmuic, tmp_afc_data->new_dev);
#endif
		ret = s2mu004_hv_muic_handle_attach(muic_data, tmp_afc_data);
		if (ret)
			pr_err("%s:%s cannot handle attach(%d)\n", MUIC_HV_DEV_NAME,
				__func__, ret);
	} else {
		pr_info("%s:%s AFC MAINTAIN (%d)\n", MUIC_HV_DEV_NAME, __func__,
				muic_data->attached_dev);
		ret = s2mu004_hv_muic_state_maintain(muic_data);
		if (ret)
			pr_err("%s:%s cannot maintain state(%d)\n", MUIC_HV_DEV_NAME,
				__func__, ret);
		goto out;
	}

out:
	return;
}

#define MASK(width, shift)	(((0x1 << (width)) - 1) << shift)
#define CHGIN_STATUS_SHIFT	5
#define CHGIN_STATUS_WIDTH	3
#define CHGIN_STATUS_MASK	MASK(CHGIN_STATUS_WIDTH, CHGIN_STATUS_SHIFT)

void s2mu004_muic_afc_check_vbadc(struct work_struct *work)
{
	struct s2mu004_muic_data *muic_data =
		container_of(work, struct s2mu004_muic_data, afc_check_vbadc.work);
	int ret;
	u8 val_vbadc, chg_sts0;

	ret = s2mu004_read_reg(muic_data->i2c, S2MU004_REG_AFC_STATUS, &val_vbadc);
	if (ret) {
		pr_err("%s:%s fail to read muic reg(%d)\n", MUIC_DEV_NAME, __func__, ret);
	}

	ret = s2mu004_read_reg(muic_data->i2c, 0X0A, &chg_sts0); // get chg in status
	if (ret) {
		pr_err("%s:%s fail to read muic chg reg(%d)\n", MUIC_DEV_NAME, __func__, ret);
	}
	chg_sts0 = chg_sts0 & CHGIN_STATUS_MASK;
	val_vbadc = val_vbadc & STATUS_VBADC_MASK;
	pr_err("%s(%d) vbadc : %d , attached_dev : %d, chg in: %02x\n " , __func__, __LINE__, val_vbadc, muic_data->attached_dev, chg_sts0);

	if((muic_data->attached_dev != ATTACHED_DEV_AFC_CHARGER_9V_MUIC)
		&& (!(chg_sts0 == 0x0))
		&& (val_vbadc == VBADC_8_7V_9_3V)) {
		mutex_lock(&muic_data->muic_mutex);
		s2mu004_hv_muic_detect_dev(muic_data, muic_data->irq_vbadc);
		mutex_unlock(&muic_data->muic_mutex);
	}

}

void s2mu004_muic_afc_cable_type_work(struct work_struct *work)
{
	struct s2mu004_muic_data *muic_data =
		container_of(work, struct s2mu004_muic_data, afc_cable_type_work.work);
	int ret;
	u8 val_vbadc, chg_sts0;

	ret = s2mu004_read_reg(muic_data->i2c, S2MU004_REG_AFC_STATUS, &val_vbadc);
	if (ret) {
		pr_err("%s:%s fail to read muic reg(%d)\n", MUIC_DEV_NAME, __func__, ret);
	}

	ret = s2mu004_read_reg(muic_data->i2c, 0X0A, &chg_sts0); // get chg in status
	if (ret) {
		pr_err("%s:%s fail to read muic chg reg(%d)\n", MUIC_DEV_NAME, __func__, ret);
	}

	chg_sts0 = chg_sts0 & CHGIN_STATUS_MASK;
	val_vbadc = val_vbadc & STATUS_VBADC_MASK;
	pr_err("%s(%d) vbadc : %d , attached_dev : %d, chg in: %02x\n " , __func__, __LINE__, val_vbadc, muic_data->attached_dev, chg_sts0);

	if(muic_data->attached_dev != ATTACHED_DEV_NONE_MUIC
		&& (val_vbadc != VBADC_8_7V_9_3V)
		&& (!(chg_sts0 == 0x0))){
		mutex_lock(&muic_data->muic_mutex);
		muic_notifier_attach_attached_dev(ATTACHED_DEV_AFC_CHARGER_5V_MUIC);
		mutex_unlock(&muic_data->muic_mutex);
	}

}

void s2mu004_muic_afc_send_mpnack(struct work_struct *work)
{
	struct s2mu004_muic_data *muic_data =
		container_of(work, struct s2mu004_muic_data, afc_send_mpnack.work);
	pr_err("[DEBUG] %s(%d) \n " , __func__, __LINE__);
	mutex_lock(&muic_data->afc_mutex);
	s2mu004_hv_muic_detect_dev(muic_data, muic_data->irq_mpnack);
	mutex_unlock(&muic_data->afc_mutex);
}

/* TA setting in s2mu004-muic.c */
void s2mu004_muic_prepare_afc_charger(struct s2mu004_muic_data *muic_data)
{
	struct i2c_client *i2c = muic_data->i2c;

	pr_info("%s:%s \n", MUIC_DEV_NAME, __func__);

	msleep(200);
	s2mu004_write_reg(i2c, 0x49, 0xa0);

	/* Set TX DATA */
	muic_data->tx_data = (HVTXBYTE_9V << 4) | HVTXBYTE_1_65A;
	s2mu004_write_reg(i2c, S2MU004_REG_TX_BYTE1, 0x46);
	s2mu004_write_reg(i2c, 0x49, 0xa1);
	s2mu004_write_reg(i2c, 0x4a, 0x06);
	s2mu004_muic_set_afc_ready(muic_data, true);

	return;
}

/* TA setting in s2mu004-muic.c */
bool s2mu004_muic_check_change_dev_afc_charger
		(struct s2mu004_muic_data *muic_data, muic_attached_dev_t new_dev)
{
	bool ret = true;

	if (new_dev == ATTACHED_DEV_TA_MUIC || \
		new_dev == ATTACHED_DEV_AFC_CHARGER_PREPARE_MUIC || \
		new_dev == ATTACHED_DEV_AFC_CHARGER_PREPARE_DUPLI_MUIC || \
		new_dev == ATTACHED_DEV_AFC_CHARGER_5V_MUIC || \
		new_dev == ATTACHED_DEV_AFC_CHARGER_5V_DUPLI_MUIC || \
		new_dev == ATTACHED_DEV_AFC_CHARGER_9V_MUIC || \
		new_dev == ATTACHED_DEV_AFC_CHARGER_ERR_V_MUIC || \
		new_dev == ATTACHED_DEV_AFC_CHARGER_ERR_V_DUPLI_MUIC){
		if(muic_check_dev_ta(muic_data)) {
			ret = false;
		}
	}

	return ret;
}

static void s2mu004_hv_muic_detect_after_charger_init(struct work_struct *work)
{
	struct afc_init_data_s *init_data =
	    container_of(work, struct afc_init_data_s, muic_afc_init_work);
	struct s2mu004_muic_data *muic_data = init_data->muic_data;
	int ret;
	u8 status3;

	pr_info("%s:%s\n", MUIC_DEV_NAME, __func__);

	mutex_lock(&muic_data->muic_mutex);

	/* check vdnmon status value */
	ret = s2mu004_read_reg(muic_data->i2c, S2MU004_REG_AFC_STATUS, &status3);
	if (ret) {
		pr_err("%s:%s fail to read muic reg(%d)\n", MUIC_HV_DEV_NAME,
				__func__, ret);
		return;
	}
	pr_info("%s:%s STATUS3:0x%02x\n", MUIC_HV_DEV_NAME, __func__, status3);

	if (muic_data->is_afc_muic_ready) {
		if (muic_data->is_afc_muic_prepare)
//			s2mu004_hv_muic_detect_dev(muic_data, muic_data->irq_vdnmon);
//		else
			s2mu004_hv_muic_detect_dev(muic_data, -1);
	}

	mutex_unlock(&muic_data->muic_mutex);
}

#ifdef CONFIG_HV_MUIC_VOLTAGE_CTRL
void hv_muic_change_afc_voltage(int tx_data)
{
	struct i2c_client *i2c = afc_init_data.muic_data->i2c;
	struct s2mu004_muic_data *muic_data = afc_init_data.muic_data;
	u8 value;

	pr_info("%s: change afc voltage(%x)\n", __func__, tx_data);

	/* QC */
	if (muic_data->attached_dev == ATTACHED_DEV_QC_CHARGER_9V_MUIC) {
		switch (tx_data) {
			case MUIC_HV_5V:
				s2mu004_hv_muic_write_reg(i2c, 0x49, 0xa1);
				break;
			case MUIC_HV_9V:
				s2mu004_hv_muic_write_reg(i2c, 0x49, 0xbd);
				break;
			default:
				break;
		}
	}
	/* AFC */
	else {
		muic_data->afc_count=0;
		s2mu004_read_reg(i2c, S2MU004_REG_TX_BYTE1, &value);
		if (value == tx_data) {
			pr_info("%s: same to current voltage %x\n", __func__, value);
			return;
		}

		s2mu004_write_reg(i2c, S2MU004_REG_TX_BYTE1, tx_data);
		s2mu004_hv_muic_afc_control_ping(muic_data, true);
	}
}

int muic_afc_set_voltage(int vol)
{
	if (vol == 5) {
		hv_muic_change_afc_voltage(MUIC_HV_5V);
	} else if (vol == 9) {
		hv_muic_change_afc_voltage(MUIC_HV_9V);
	} else {
		pr_warn("%s:%s invalid value\n", MUIC_DEV_NAME, __func__);
		return 0;
	}

	return 1;
}
#endif /* CONFIG_HV_MUIC_VOLTAGE_CTRL */

void s2mu004_hv_muic_charger_init(void)
{
	pr_info("%s:%s\n", MUIC_HV_DEV_NAME, __func__);

	if(afc_init_data.muic_data) {
		afc_init_data.muic_data->is_charger_ready = true;
		schedule_work(&afc_init_data.muic_afc_init_work);
	}
}

#if 0
static void s2mu004_hv_muic_check_qc_vb(struct work_struct *work)
{
	struct s2mu004_muic_data *muic_data =
	    container_of(work, struct s2mu004_muic_data, hv_muic_qc_vb_work.work);
	u8 status3, vbadc;

	if (!muic_data) {
		pr_err("%s:%s cannot read muic_data!\n", MUIC_HV_DEV_NAME, __func__);
		return;
	}

	mutex_lock(&muic_data->muic_mutex);

	if (muic_data->is_qc_vb_settle == true) {
		pr_info("%s:%s already qc vb settled\n", MUIC_HV_DEV_NAME, __func__);
		goto out;
	}

	pr_info("%s:%s\n", MUIC_HV_DEV_NAME, __func__);

	s2mu004_hv_muic_read_reg(muic_data->i2c, S2MU004_REG_AFC_STATUS, &status3);
	vbadc = status3 & status_vbadc_MASK;

	if (vbadc == VBADC_4V_5V || vbadc == VBADC_5V_6V) {
		muic_data->is_qc_vb_settle = true;
		s2mu004_hv_muic_detect_dev(muic_data, muic_data->irq_vbadc);
	}

out:
	mutex_unlock(&muic_data->muic_mutex);
	return;
}

static void s2mu004_hv_muic_check_mping_miss(struct work_struct *work)
{
	struct s2mu004_muic_data *muic_data =
		container_of(work, struct s2mu004_muic_data, hv_muic_mping_miss_wa.work);

	if (!muic_data) {
		pr_err("%s:%s cannot read muic_data!\n", MUIC_HV_DEV_NAME, __func__);
		return;
	}

	mutex_lock(&muic_data->muic_mutex);

	/* Check the current device */
	if (muic_data->attached_dev != ATTACHED_DEV_AFC_CHARGER_PREPARE_MUIC &&
		muic_data->attached_dev != ATTACHED_DEV_AFC_CHARGER_5V_MUIC) {
		pr_info("%s:%s MPing Missing did not happened "
			"but AFC protocol did not success\n",
			MUIC_HV_DEV_NAME, __func__);
		goto out;
	}

	pr_info("%s:%s\n", MUIC_HV_DEV_NAME, __func__);

	/* We make MPING NACK interrupt virtually */
	s2mu004_hv_muic_detect_dev(muic_data, muic_data->irq_mpnack);

out:
	mutex_unlock(&muic_data->muic_mutex);
	return;
}
#endif

void s2mu004_hv_muic_init_detect(struct s2mu004_muic_data *muic_data)
{
//	int ret;
//	u8 status3, vdnmon;

	pr_info("%s:%s\n", MUIC_DEV_NAME, __func__);

	mutex_lock(&muic_data->muic_mutex);
#if 0
	if (muic_data->is_boot_dpdnvden == DPDNVDEN_ENABLE)
		pr_info("%s:%s dpdnvden already ENABLE\n", MUIC_HV_DEV_NAME, __func__);
	else if (muic_data->is_boot_dpdnvden == DPDNVDEN_DISABLE) {
		mdelay(30);
		pr_info("%s:%s dpdnvden == DISABLE, 30ms delay\n", MUIC_HV_DEV_NAME, __func__);
	} else {
		pr_err("%s:%s dpdnvden is not correct(0x%x)!\n", MUIC_HV_DEV_NAME,
			__func__, muic_data->is_boot_dpdnvden);
		goto out;
	}
#endif

//	s2mu004_hv_muic_detect_dev(muic_data, muic_data->irq_vdnmon);

#if 0
	ret = s2mu004_read_reg(muic_data->i2c, S2MU004_REG_AFC_STATUS, &status3);
	if (ret) {
		pr_err("%s:%s fail to read muic reg(%d)\n", MUIC_DEV_NAME, __func__, ret);
		vdnmon = VDNMON_DONTCARE;
	} else
		vdnmon = status3 & status_vdnmon_MASK;

	if (vdnmon == VDNMON_LOW)
		s2mu004_hv_muic_detect_dev(muic_data, muic_data->irq_vdnmon);
	else
		pr_info("%s:%s vdnmon != HIGH(0x%x)\n", MUIC_HV_DEV_NAME, __func__, vdnmon);
#endif

//out:
	mutex_unlock(&muic_data->muic_mutex);
}
#if 0
void s2mu004_hv_muic_init_check_dpdnvden(struct s2mu004_muic_data *muic_data)
{
	u8 hvcontrol1;
	int ret;

	mutex_lock(&muic_data->muic_mutex);

	ret = s2mu004_hv_muic_read_reg(muic_data->i2c, S2MU004_REG_AFC_CTRL1, &hvcontrol1);
	if (ret) {
		pr_err("%s:%s cannot read HVCONTROL1 reg!\n", MUIC_HV_DEV_NAME, __func__);
		muic_data->is_boot_dpdnvden = DPDNVDEN_DONTCARE;
	} else
		muic_data->is_boot_dpdnvden = hvcontrol1 & HVCONTROL1_DPDNVDEN_MASK;

	mutex_unlock(&muic_data->muic_mutex);
}
#endif
static irqreturn_t s2mu004_muic_hv_irq(int irq, void *data)
{
	struct s2mu004_muic_data *muic_data = data;
	int ret;
	u8 val_vbadc;
	pr_err("%s:%s irq!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!:%d\n", MUIC_HV_DEV_NAME, __func__, irq);

	mutex_lock(&muic_data->muic_mutex);
//	if (muic_data->is_muic_ready == false)
//		pr_info("%s:%s MUIC is not ready, just return\n", MUIC_HV_DEV_NAME,
//			__func__);
	if (muic_data->is_afc_muic_ready == false)
		pr_info("%s:%s not ready yet(afc_muic_ready[%c])\n", MUIC_HV_DEV_NAME,
			 __func__, (muic_data->is_afc_muic_ready ? 'T' : 'F'));
	else if (muic_data->is_charger_ready == false && irq != muic_data->irq_vdnmon)
		pr_info("%s:%s not ready yet(charger_ready[%c])\n", MUIC_HV_DEV_NAME,
			__func__, (muic_data->is_charger_ready ? 'T' : 'F'));
	else if (muic_data->pdata->afc_disable)
		pr_info("%s:%s AFC disable by USER (afc_disable[%c]\n", MUIC_HV_DEV_NAME,
			__func__, (muic_data->pdata->afc_disable ? 'T' : 'F'));
	else {
		muic_data->afc_irq = irq;

		/* Re-check vbadc voltage, if vbadc 9v interrupt does not occur when send pings. */
		if (irq == muic_data->irq_vbadc && muic_data->afc_count >= 2)
		{
			pr_err("%s: VbADC interrupt after sending master ping x3\n " , __func__);
			ret = s2mu004_read_reg(muic_data->i2c, S2MU004_REG_AFC_STATUS, &val_vbadc);
			if (ret) {
				pr_err("%s:%s fail to read muic reg(%d)\n", MUIC_DEV_NAME, __func__, ret);
			}
			val_vbadc = val_vbadc & STATUS_VBADC_MASK;
			if (val_vbadc != VBADC_8_7V_9_3V) {
				pr_err("%s: vbadc: %d\n " , __func__, val_vbadc);
				schedule_delayed_work(&muic_data->afc_check_vbadc, msecs_to_jiffies(100));
			}
		}
		/* After ping , if there is response then cancle mpnack work */
		if ((irq == muic_data->irq_mrxrdy) || (irq == muic_data->irq_mpnack)) {
			cancel_delayed_work(&muic_data->afc_send_mpnack);
			cancel_delayed_work(&muic_data->afc_control_ping_retry);
		}

		if ((irq == muic_data->irq_vbadc) && (muic_data->qc_prepare == 1)) {
			muic_data->qc_prepare = 0;
			cancel_delayed_work(&muic_data->afc_qc_retry);
		}

		s2mu004_hv_muic_detect_dev(muic_data, muic_data->afc_irq);

	}

	mutex_unlock(&muic_data->muic_mutex);

	return IRQ_HANDLED;
}

#define REQUEST_HV_IRQ(_irq, _dev_id, _name)				\
do {									\
	ret = request_threaded_irq(_irq, NULL, s2mu004_muic_hv_irq,	\
				IRQF_NO_SUSPEND, _name, _dev_id);	\
	if (ret < 0) {							\
		pr_err("%s:%s Failed to request IRQ #%d: %d\n",		\
				MUIC_HV_DEV_NAME, __func__, _irq, ret);	\
		_irq = 0;						\
	}								\
} while (0)

int s2mu004_afc_muic_irq_init(struct s2mu004_muic_data *muic_data)
{
	int ret = 0;

	pr_info("%s:%s\n", MUIC_HV_DEV_NAME, __func__);

	if (muic_data->mfd_pdata && (muic_data->mfd_pdata->irq_base > 0)) {
		int irq_base = muic_data->mfd_pdata->irq_base;

		/* request AFC MUIC IRQ */
		muic_data->irq_vdnmon = irq_base + S2MU004_AFC_IRQ_VDNMon;
		REQUEST_HV_IRQ(muic_data->irq_vdnmon, muic_data, "muic-vdnmon");
//		muic_data->irq_dnres = irq_base + S2MU004_AFC_IRQ_DNRes;
//		REQUEST_HV_IRQ(muic_data->irq_dnres, muic_data, "muic-dnres");
		muic_data->irq_mrxrdy = irq_base + S2MU004_AFC_IRQ_MRxRdy;
		REQUEST_HV_IRQ(muic_data->irq_mrxrdy, muic_data, "muic-mrxrdy");
//		muic_data->irq_mpnack = irq_base + S2MU004_AFC_IRQ_MPNack;
//		REQUEST_HV_IRQ(muic_data->irq_mpnack, muic_data, "muic-mpnack");
		muic_data->irq_vbadc = irq_base + S2MU004_AFC_IRQ_VbADC;
		REQUEST_HV_IRQ(muic_data->irq_vbadc, muic_data, "muic-vbadc");

		muic_data->irq_mpnack = irq_base + S2MU004_AFC_IRQ_MPNack;
		REQUEST_HV_IRQ(muic_data->irq_mpnack, muic_data, "muic-mpnack");

		pr_info("%s:%s dnres(%d), mrxrdy(%d), mpnack(%d), vbadc(%d), vdnmon(%d)\n",
				MUIC_HV_DEV_NAME, __func__,
				muic_data->irq_dnres, muic_data->irq_mrxrdy,
				muic_data->irq_mpnack, muic_data->irq_vbadc, muic_data->irq_vdnmon);
	}

	return ret;
}

#define FREE_HV_IRQ(_irq, _dev_id, _name)					\
do {									\
	if (_irq) {							\
		free_irq(_irq, _dev_id);				\
		pr_info("%s:%s IRQ(%d):%s free done\n", MUIC_HV_DEV_NAME,	\
				__func__, _irq, _name);			\
	}								\
} while (0)

void s2mu004_hv_muic_free_irqs(struct s2mu004_muic_data *muic_data)
{
	pr_info("%s:%s\n", MUIC_HV_DEV_NAME, __func__);

	/* free MUIC IRQ */
	FREE_HV_IRQ(muic_data->irq_vdnmon, muic_data, "muic-vdnmon");
//	FREE_HV_IRQ(muic_data->irq_dnres, muic_data, "muic-dnres");
	FREE_HV_IRQ(muic_data->irq_mrxrdy, muic_data, "muic-mrxrdy");
	FREE_HV_IRQ(muic_data->irq_mpnack, muic_data, "muic-mpnack");
	FREE_HV_IRQ(muic_data->irq_vbadc, muic_data, "muic-vbadc");
}
#if 0
#if defined(CONFIG_OF)
int of_s2mu004_hv_muic_dt(struct s2mu004_muic_data *muic_data)
{
	struct device_node *np_muic;
	int ret = 0;

	np_muic = of_find_node_by_path("/muic");
	if (np_muic == NULL)
		return -EINVAL;

	ret = of_property_read_u8(np_muic, "muic,qc-hv", &muic_data->qc_hv);
	if (ret) {
		pr_err("%s:%s There is no Property of muic,qc-hv\n",
				MUIC_DEV_NAME, __func__);
		goto err;
	}

	pr_info("%s:%s muic_data->qc-hv:0x%02x\n", MUIC_DEV_NAME, __func__,
				muic_data->qc_hv);

err:
	of_node_put(np_muic);

	return ret;
}
#endif /* CONFIG_OF */
#endif
void s2mu004_hv_muic_initialize(struct s2mu004_muic_data *muic_data)
{
	pr_info("%s:%s\n", MUIC_HV_DEV_NAME, __func__);

	muic_data->is_afc_handshaking = false;
	muic_data->is_afc_muic_prepare = false;
	muic_data->is_charger_ready = true;
	muic_data->pdata->afc_disable = false;
	
	s2mu004_write_reg(muic_data->i2c, 0xd8, 0x84); /* OTP */
	s2mu004_write_reg(muic_data->i2c, 0x2c, 0x55); /* OTP */
	s2mu004_write_reg(muic_data->i2c, 0xc3, 0x88); /* OTP */

	s2mu004_hv_muic_write_reg(muic_data->i2c, 0x4b, 0x00);
	s2mu004_hv_muic_write_reg(muic_data->i2c, 0x49, 0x00);
	s2mu004_hv_muic_write_reg(muic_data->i2c, 0x4a, 0x00);
	s2mu004_hv_muic_write_reg(muic_data->i2c, 0x5f, 0x01);

	afc_init_data.muic_data = muic_data;
	INIT_WORK(&afc_init_data.muic_afc_init_work, s2mu004_hv_muic_detect_after_charger_init);

	INIT_DELAYED_WORK(&muic_data->afc_check_vbadc, s2mu004_muic_afc_check_vbadc);
	INIT_DELAYED_WORK(&muic_data->afc_cable_type_work, s2mu004_muic_afc_cable_type_work);

	INIT_DELAYED_WORK(&muic_data->afc_send_mpnack, s2mu004_muic_afc_send_mpnack);
	INIT_DELAYED_WORK(&muic_data->afc_control_ping_retry, s2mu004_muic_afc_control_ping_retry);
	INIT_DELAYED_WORK(&muic_data->afc_qc_retry, s2mu004_muic_afc_qc_retry);
	INIT_DELAYED_WORK(&muic_data->afc_after_prepare, s2mu004_muic_afc_after_prepare);

	mutex_init(&muic_data->afc_mutex);
}

void s2mu004_hv_muic_remove(struct s2mu004_muic_data *muic_data)
{
	pr_info("%s:%s\n", MUIC_HV_DEV_NAME, __func__);

	/* Set digital IVR when power off under revision EVT3 */
	if (muic_data->ic_rev_id < 3)
		s2mu004_muic_hv_update_reg(muic_data->i2c, 0xB3, 0x00, 0x08, 0);
	/* Afc reset */
	s2mu004_muic_hv_update_reg(muic_data->i2c, 0x5f, 0x80, 0x80, 0);

	cancel_work_sync(&afc_init_data.muic_afc_init_work);
//	cancel_delayed_work_sync(&muic_data->hv_muic_qc_vb_work);
//	cancel_delayed_work(&muic_data->hv_muic_mping_miss_wa);

	s2mu004_hv_muic_free_irqs(muic_data);
}
#if 0
void s2mu004_hv_muic_remove_wo_free_irq(struct s2mu004_muic_data *muic_data)
{
	pr_info("%s:%s\n", MUIC_HV_DEV_NAME, __func__);
	cancel_work_sync(&afc_init_data.muic_afc_init_work);
//	cancel_delayed_work_sync(&muic_data->hv_muic_qc_vb_work);
//	cancel_delayed_work(&muic_data->hv_muic_mping_miss_wa);
}
#endif
