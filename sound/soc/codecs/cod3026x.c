/*
 * Copyright (c) 2014 Samsung Electronics Co. Ltd.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <sound/exynos_regmap_fw.h>
#include <linux/i2c.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/switch.h>
#include <linux/input.h>
#include <linux/completion.h>
#include <linux/wakelock.h>

#include <sound/exynos-audmixer.h>
#include <sound/cod3026x.h>
#include "cod3026x.h"

#define COD3026X_FIRMWARE_NAME	"cod3026x-s2803x-aud-fw.bin"

#define COD3026X_SAMPLE_RATE_48KHZ	48000
#define COD3026X_SAMPLE_RATE_192KHZ	192000

#define COD3026X_RESTORE_OTP_COUNT	5
#define COD3026X_RESTORE_REG_COUNT	16
#define COD3026X_OTP_R_OFFSET		0x0

#define COD3026X_MAX_IRQ_CHK_BITS	5
#define COD3026X_START_IRQ_CHK_BIT	2
#define COD3026X_MJ_DET_INVALID		(-1)

#ifdef CONFIG_SND_SOC_SAMSUNG_VERBOSE_DEBUG
#ifdef dev_dbg
#undef dev_dbg
#endif
#define dev_dbg dev_err
#endif

#define COD3026X_NO_DET_WATER  0
#define COD3026X_DET_WATER 1
#define	COD3026X_DET_WATER_JACK_IN  2
#define COD3026X_WATER_DET_THRESHOLD_MAX 2053
#define COD3026X_WATER_DET_THRESHOLD_MIN_FIRST 200
#define COD3026X_WATER_DET_THRESHOLD_MIN 120
#define COD3026X_WATER_DET_POLLING_TIME    1000
#define COD3026X_JACK_ERR_THRESHOLD_MIN 50
#define COD3026X_JACK_ERR_THRESHOLD_MAX 80

/* Forward Declarations */
static void cod3026x_save_otp_registers(struct snd_soc_codec *codec);
static void cod3026x_restore_otp_registers(struct snd_soc_codec *codec);
static void cod3026x_reset_io_selector_bits(struct snd_soc_codec *codec);
static void cod3026x_configure_mic_bias(struct snd_soc_codec *codec);
static int cod3026x_disable(struct device *dev);
static int cod3026x_enable(struct device *dev);

static inline void cod3026x_usleep(unsigned int u_sec)
{
	usleep_range(u_sec, u_sec + 10);
}

/**
 * Helper functions to read ADC value for button detection
 */

#define COD3026X_ADC_SAMPLE_SIZE	5

static void cod3026x_adc_start(struct cod3026x_priv *cod3026x)
{
	cod3026x->jack_adc = iio_channel_get_all(cod3026x->dev);
}

static void cod3026x_adc_stop(struct cod3026x_priv *cod3026x)
{
	iio_channel_release(cod3026x->jack_adc);
}

static int cod3026x_gdet_adc_get_value(struct cod3026x_priv *cod3026x)
{
	int adc_data = -1;
	int adc_max = 0;
	int adc_min = 0xFFFF;
	int adc_total = 0;
	int adc_retry_cnt = 0;
	int i;
	struct iio_channel *jack_adc = cod3026x->jack_adc;

	for (i = 0; i < COD3026X_ADC_SAMPLE_SIZE; i++) {
		iio_read_channel_raw(&jack_adc[1], &adc_data);
		while (adc_data < 0) {
			adc_retry_cnt++;
			if (adc_retry_cnt > 10)
				return adc_data;
			iio_read_channel_raw(&jack_adc[1], &adc_data);
		}

		if (adc_data > adc_max)
			adc_max = adc_data;
		if (adc_data < adc_min)
			adc_min = adc_data;

		adc_total += adc_data;
	}

	return (adc_total - adc_max - adc_min) / (COD3026X_ADC_SAMPLE_SIZE - 2);
}


static int cod3026x_adc_get_value(struct cod3026x_priv *cod3026x)
{
	int adc_data = -1;
	int adc_max = 0;
	int adc_min = 0xFFFF;
	int adc_total = 0;
	int adc_retry_cnt = 0;
	int i;
	struct iio_channel *jack_adc = cod3026x->jack_adc;

	for (i = 0; i < COD3026X_ADC_SAMPLE_SIZE; i++) {
		iio_read_channel_raw(&jack_adc[0], &adc_data);
		/* if adc_data is negative, ignore */
		while (adc_data < 0) {
			adc_retry_cnt++;
			if (adc_retry_cnt > 10)
				return adc_data;
			iio_read_channel_raw(&jack_adc[0], &adc_data);
		}

		/* Update min/max values */
		if (adc_data > adc_max)
			adc_max = adc_data;
		if (adc_data < adc_min)
			adc_min = adc_data;

		adc_total += adc_data;
	}

	return (adc_total - adc_max - adc_min) / (COD3026X_ADC_SAMPLE_SIZE - 2);
}

/**
 * Return value:
 * true: if the register value cannot be cached, hence we have to read from the
 * hardware directly
 * false: if the register value can be read from cache
 */
static bool cod3026x_volatile_register(struct device *dev, unsigned int reg)
{
	/**
	 * For all the registers for which we want to restore the value during
	 * regcache_sync operation, we need to return true here. For registers
	 * whose value need not be cached and restored should return false here.
	 *
	 * For the time being, let us cache the value of all registers other
	 * than the IRQ pending and IRQ status registers.
	 */
	switch (reg) {
	case COD3026X_01_IRQ1PEND ... COD3026X_05_IRQ5PEND:
	case COD3026X_0B_STATUS1 ... COD3026X_0D_STATUS3:
	case COD3026X_61_RESERVED ... COD3026X_62_IRQ_R:
	case COD3026X_80_DET_PDB ... COD3026X_8B_MICBIAS2:
		return true;
	default:
		return false;
	}
}

/**
 * Return value:
 * true: if the register value can be read
 * flase: if the register cannot be read
 */
static bool cod3026x_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case COD3026X_01_IRQ1PEND ... COD3026X_0D_STATUS3:
	case COD3026X_10_PD_REF ... COD3026X_1C_SV_DA:
	case COD3026X_20_VOL_AD1 ... COD3026X_26_DSM_ADS:
	case COD3026X_30_VOL_HPL ... COD3026X_38_DCT_CLK1:
	case COD3026X_40_DIGITAL_POWER ... COD3026X_44_ADC_R_VOL:
	case COD3026X_50_DAC1 ... COD3026X_5F_SPKLIMIT3:
	case COD3026X_60_OFFSET1 ... COD3026X_62_IRQ_R:
	case COD3026X_70_CLK1_AD ... COD3026X_7A_SL_DA2:
	case COD3026X_80_DET_PDB ... COD3026X_8B_MICBIAS2:
	case COD3026X_D0_CTRL_IREF1 ... COD3026X_DE_CTRL_SPKS2:
		return true;
	default:
		return false;
	}
}

static bool cod3026x_writeable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	/* Reg-0x09 to Reg-0x0B are read-only status registers */
	case COD3026X_01_IRQ1PEND ... COD3026X_0A_IRQ5M:
	case COD3026X_10_PD_REF ... COD3026X_1C_SV_DA:
	case COD3026X_20_VOL_AD1 ... COD3026X_26_DSM_ADS:
	case COD3026X_30_VOL_HPL ... COD3026X_38_DCT_CLK1:
	case COD3026X_40_DIGITAL_POWER ... COD3026X_44_ADC_R_VOL:
	case COD3026X_50_DAC1 ... COD3026X_5F_SPKLIMIT3:
	/* Reg-0x61 is reserved, Reg-0x62 is read-only */
	case COD3026X_60_OFFSET1:
	case COD3026X_70_CLK1_AD ... COD3026X_7A_SL_DA2:
	case COD3026X_80_DET_PDB ... COD3026X_8B_MICBIAS2:
	case COD3026X_D0_CTRL_IREF1 ... COD3026X_DE_CTRL_SPKS2:
		return true;
	default:
		return false;
	}
}

const struct regmap_config cod3026x_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = COD3026X_MAX_REGISTER,
	.readable_reg = cod3026x_readable_register,
	.writeable_reg = cod3026x_writeable_register,
	.volatile_reg = cod3026x_volatile_register,

	.use_single_rw = true,
	.cache_type = REGCACHE_RBTREE,
};

/**
 * TLV_DB_SCALE_ITEM
 *
 * (TLV: Threshold Limit Value)
 *
 * For various properties, the dB values don't change linearly with respect to
 * the digital value of related bit-field. At most, they are quasi-linear,
 * that means they are linear for various ranges of digital values. Following
 * table define such ranges of various properties.
 *
 * TLV_DB_RANGE_HEAD(num)
 * num defines the number of linear ranges of dB values.
 *
 * s0, e0, TLV_DB_SCALE_ITEM(min, step, mute),
 * s0: digital start value of this range (inclusive)
 * e0: digital end valeu of this range (inclusive)
 * min: dB value corresponding to s0
 * step: the delta of dB value in this range
 * mute: ?
 *
 * Example:
 *	TLV_DB_RANGE_HEAD(3),
 *	0, 1, TLV_DB_SCALE_ITEM(-2000, 2000, 0),
 *	2, 4, TLV_DB_SCALE_ITEM(1000, 1000, 0),
 *	5, 6, TLV_DB_SCALE_ITEM(3800, 8000, 0),
 *
 * The above code has 3 linear ranges with following digital-dB mapping.
 * (0...6) -> (-2000dB, 0dB, 1000dB, 2000dB, 3000dB, 3800dB, 4600dB),
 *
 * DECLARE_TLV_DB_SCALE
 *
 * This macro is used in case where there is a linear mapping between
 * the digital value and dB value.
 *
 * DECLARE_TLV_DB_SCALE(name, min, step, mute)
 *
 * name: name of this dB scale
 * min: minimum dB value corresponding to digital 0
 * step: the delta of dB value
 * mute: ?
 *
 * NOTE: The information is mostly for user-space consumption, to be viewed
 * alongwith amixer.
 */

/**
 * cod3026x_ctvol_bst_tlv
 *
 * Map: (0x0, 0dB), (0x1, 12dB), (0x2, 20dB)
 *
 * CTVOL_BST1, reg(0x20), shift(5), width(2)
 * CTVOL_BST2, reg(0x21), shift(5), width(2)
 * CTVOL_BST3, reg(0x22), shift(5), width(2)
 */
static const unsigned int cod3026x_ctvol_bst_tlv[] = {
	TLV_DB_RANGE_HEAD(2),
	0, 1, TLV_DB_SCALE_ITEM(0, 1200, 0),
	2, 2, TLV_DB_SCALE_ITEM(2000, 0, 0),
};

/**
 * cod3026x_ctvol_bst_pga_tlv
 *
 * Range: -16.5dB to +18dB, step 1.5dB
 *
 * CTVOL_BST_PGA1, reg(0x20), shift(0), width(5), invert(1), max(31)
 * CTVOL_BST_PGA2, reg(0x21), shift(0), width(5), invert(1), max(31)
 * CTVOL_BST_PGA3, reg(0x22), shift(0), width(5), invert(1), max(31)
 */
static const DECLARE_TLV_DB_SCALE(cod3026x_ctvol_bst_pga_tlv, -1650, 150, 0);

/**
 * cod3026x_ctvol_hp_tlv
 *
 * Range: -57dB to +6dB, step 1dB
 *
 * CTVOL_HPL, reg(0x30), shift(0), width(6), invert(1), max(63)
 * CTVOL_HPR, reg(0x31), shift(0), width(6), invert(1), max(63)
 */
static const DECLARE_TLV_DB_SCALE(cod3026x_ctvol_hp_tlv, -5700, 100, 0);

/**
 * cod3019_ctvol_ep_tlv
 *
 * Range: 0dB to +12dB, step 1dB
 *
 * CTVOL_EP, reg(0x32), shift(4), width(4), invert(0), max(12)
 */
static const DECLARE_TLV_DB_SCALE(cod3026x_ctvol_ep_tlv, 0, 100, 0);

/**
 * cod3026x_ctvol_spk_pga_tlv
 *
 * Range: -6dB to +3dB, step 1dB
 *
 * CTVOL_SPK_PGA, reg(0x32), shift(0), width(4), invert(0), max(9)
 */
static const DECLARE_TLV_DB_SCALE(cod3026x_ctvol_spk_pga_tlv, -600, 100, 0);

/**
 * cod3026x_dvol_adc_tlv
 *
 * Map as per data-sheet:
 * (0x00 to 0x86) -> (+12dB to -55dB, step 0.5dB)
 * (0x87 to 0x91) -> (-56dB to -66dB, step 1dB)
 * (0x92 to 0x94) -> (-68dB to -72dB, step 2dB)
 * (0x95 to 0x96) -> (-78dB to -84dB, step 6dB)
 *
 * When the map is in descending order, we need to set the invert bit
 * and arrange the map in ascending order. The offsets are calculated as
 * (max - offset).
 *
 * offset_in_table = max - offset_actual;
 *
 * DVOL_ADL, reg(0x43), shift(0), width(8), invert(1), max(0x96)
 * DVOL_ADR, reg(0x44), shift(0), width(8), invert(1), max(0x96)
 * DVOL_DAL, reg(0x51), shift(0), width(8), invert(1), max(0x96)
 * DVOL_DAR, reg(0x52), shift(0), width(8), invert(1), max(0x96)
 */
static const unsigned int cod3026x_dvol_tlv[] = {
	TLV_DB_RANGE_HEAD(4),
	0x00, 0x01, TLV_DB_SCALE_ITEM(-8400, 600, 0),
	0x02, 0x04, TLV_DB_SCALE_ITEM(-7200, 200, 0),
	0x05, 0x09, TLV_DB_SCALE_ITEM(-6600, 100, 0),
	0x10, 0x96, TLV_DB_SCALE_ITEM(-5500, 50, 0),
};

/**
 * cod3026x_dnc_min_gain_tlv
 *
 * Range: -6dB to 0dB, step 1dB
 *
 * DNC_MINGAIN , reg(0x55), shift(5), width(3)
 */
static const unsigned int cod3026x_dnc_min_gain_tlv[] = {
	TLV_DB_RANGE_HEAD(1),
	0x00, 0x06, TLV_DB_SCALE_ITEM(-600, 0, 0),
};

/**
 * cod3026x_dnc_max_gain_tlv
 *
 * Range: 0dB to 24dB, step 1dB
 *
 * DNC_MAXGAIN , reg(0x55), shift(0), width(5)
 */
static const unsigned int cod3026x_dnc_max_gain_tlv[] = {
	TLV_DB_RANGE_HEAD(1),
	0x06, 0x1e, TLV_DB_SCALE_ITEM(0, 2400, 0),
};

/**
 * cod3026x_dnc_lvl_tlv
 *
 * Range: -10.5dB to 0dB, step 1.5dB
 *
 * DNCLVL_R/L, reg(0x55), shift(0/4), width(3), invert(0), max(7)
 */
static const DECLARE_TLV_DB_SCALE(cod3026x_dnc_lvl_tlv, -1050, 0, 0);

/**
 * mono_mix_mode
 *
 * Selecting the Mode of Mono Mixer (inside DAC block)
 */
static const char * const cod3026x_mono_mix_mode_text[] = {
	"Disable", "R", "L", "LR-Invert",
	"(L+R)/2", "L+R"
};

static const struct soc_enum cod3026x_mono_mix_mode_enum =
	SOC_ENUM_SINGLE(COD3026X_50_DAC1, DAC1_MONOMIX_SHIFT,
			ARRAY_SIZE(cod3026x_mono_mix_mode_text),
			cod3026x_mono_mix_mode_text);

/**
 * chargepump_mode
 *
 * Selecting the chargepump mode
 */
static const char * const cod3026x_chargepump_mode_text[] = {
	"VDD", "HALF-VDD", "CLASS-G-D", "CLASS-G-A"
};

static const struct soc_enum cod3026x_chargepump_mode_enum =
	SOC_ENUM_SINGLE(COD3026X_33_CTRL_EP, CTMV_CP_MODE_SHIFT,
			ARRAY_SIZE(cod3026x_chargepump_mode_text),
			cod3026x_chargepump_mode_text);

/**
 * dnc_zcd_enable
 *
 * Enable / Disable dnc zcd
 */
static const char * const cod3026x_dnc_zcd_enable_text[] = {"Off", "On"};

static const struct soc_enum cod3026x_dnc_zcd_enable_enum =
	SOC_ENUM_SINGLE(COD3026X_5C_DNC9, DNC_ZCD_EN_SHIFT,
			ARRAY_SIZE(cod3026x_dnc_zcd_enable_text),
			cod3026x_dnc_zcd_enable_text);

static int dac_soft_mute_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}


static int dac_soft_mute_put(struct snd_kcontrol *kcontrol,
		    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	int value = ucontrol->value.integer.value[0];

	if (!value)
		/* enable soft mute */
		snd_soc_update_bits(codec, COD3026X_50_DAC1,
				DAC1_SOFT_MUTE_MASK, DAC1_SOFT_MUTE_MASK);
	else
		/* diable soft mute */
		snd_soc_update_bits(codec, COD3026X_50_DAC1,
				DAC1_SOFT_MUTE_MASK, 0x0);

	dev_info(codec->dev, "%s: soft mute : %s\n", __func__,
			(!value) ? "on":"off");
	return 0;
}

/**
 * struct snd_kcontrol_new cod3026x_snd_control
 *
 * Every distinct bit-fields within the CODEC SFR range may be considered
 * as a control elements. Such control elements are defined here.
 *
 * Depending on the access mode of these registers, different macros are
 * used to define these control elements.
 *
 * SOC_ENUM: 1-to-1 mapping between bit-field value and provided text
 * SOC_SINGLE: Single register, value is a number
 * SOC_SINGLE_TLV: Single register, value corresponds to a TLV scale
 * SOC_SINGLE_TLV_EXT: Above + custom get/set operation for this value
 * SOC_SINGLE_RANGE_TLV: Register value is an offset from minimum value
 * SOC_DOUBLE: Two bit-fields are updated in a single register
 * SOC_DOUBLE_R: Two bit-fields in 2 different registers are updated
 */

/**
 * All the data goes into cod3026x_snd_controls.
 * All path inter-connections goes into cod3026x_dapm_routes
 */
static const struct snd_kcontrol_new cod3026x_snd_controls[] = {
	SOC_SINGLE_TLV("MIC1 Boost Volume", COD3026X_20_VOL_AD1,
			VOLAD1_CTVOL_BST1_SHIFT,
			(BIT(VOLAD1_CTVOL_BST1_WIDTH) - 1), 0,
			cod3026x_ctvol_bst_tlv),

	SOC_SINGLE_TLV("MIC1 Volume", COD3026X_20_VOL_AD1,
			VOLAD1_CTVOL_BST_PGA1_SHIFT,
			(BIT(VOLAD1_CTVOL_BST_PGA1_WIDTH) - 1), 1,
			cod3026x_ctvol_bst_pga_tlv),

	SOC_SINGLE_TLV("MIC2 Boost Volume", COD3026X_21_VOL_AD2,
			VOLAD2_CTVOL_BST2_SHIFT,
			(BIT(VOLAD2_CTVOL_BST2_WIDTH) - 1), 0,
			cod3026x_ctvol_bst_tlv),

	SOC_SINGLE_TLV("MIC2 Volume", COD3026X_21_VOL_AD2,
			VOLAD2_CTVOL_BST_PGA2_SHIFT,
			(BIT(VOLAD2_CTVOL_BST_PGA2_WIDTH) - 1), 1,
			cod3026x_ctvol_bst_pga_tlv),

	SOC_SINGLE_TLV("MIC3 Boost Volume", COD3026X_22_VOL_AD3,
			VOLAD3_CTVOL_BST3_SHIFT,
			(BIT(VOLAD3_CTVOL_BST3_WIDTH) - 1), 0,
			cod3026x_ctvol_bst_tlv),

	SOC_SINGLE_TLV("MIC3 Volume", COD3026X_22_VOL_AD3,
			VOLAD3_CTVOL_BST_PGA3_SHIFT,
			(BIT(VOLAD3_CTVOL_BST_PGA3_WIDTH) - 1), 1,
			cod3026x_ctvol_bst_pga_tlv),

	SOC_DOUBLE_R_TLV("Headphone Volume", COD3026X_30_VOL_HPL,
			COD3026X_31_VOL_HPR, VOLHP_CTVOL_HP_SHIFT,
			(BIT(VOLHP_CTVOL_HP_WIDTH) - 1), 1,
			cod3026x_ctvol_hp_tlv),

	SOC_SINGLE_TLV("Earphone Volume", COD3026X_32_VOL_EP_SPK,
			CTVOL_EP_SHIFT,
			(BIT(CTVOL_EP_WIDTH) - 1), 0,
			cod3026x_ctvol_ep_tlv),

	SOC_SINGLE_TLV("Speaker Volume", COD3026X_32_VOL_EP_SPK,
			CTVOL_SPK_PGA_SHIFT,
			(BIT(CTVOL_SPK_PGA_WIDTH) - 1), 0,
			cod3026x_ctvol_spk_pga_tlv),

	SOC_SINGLE_TLV("ADC Left Gain", COD3026X_43_ADC_L_VOL,
			AD_DA_DVOL_SHIFT,
			AD_DA_DVOL_MAXNUM, 1, cod3026x_dvol_tlv),

	SOC_SINGLE_TLV("ADC Right Gain", COD3026X_44_ADC_R_VOL,
			AD_DA_DVOL_SHIFT,
			AD_DA_DVOL_MAXNUM, 1, cod3026x_dvol_tlv),

	SOC_DOUBLE_R_TLV("DAC Gain", COD3026X_51_DAC_L_VOL,
			COD3026X_52_DAC_R_VOL, AD_DA_DVOL_SHIFT,
			AD_DA_DVOL_MAXNUM, 1, cod3026x_dvol_tlv),

	SOC_SINGLE_TLV("DNC Min Gain", COD3026X_55_DNC2,
			DNC_MIN_GAIN_SHIFT,
			(BIT(DNC_MIN_GAIN_WIDTH) - 2), 0,
			cod3026x_dnc_min_gain_tlv),

	SOC_SINGLE_TLV("DNC Max Gain", COD3026X_55_DNC2,
			DNC_MAX_GAIN_SHIFT,
			(BIT(DNC_MAX_GAIN_WIDTH) - 2), 0,
			cod3026x_dnc_max_gain_tlv),

	SOC_SINGLE_TLV("DNC Level Left", COD3026X_56_DNC3,
			DNC_LVL_L_SHIFT,
			(BIT(DNC_LVL_L_WIDTH) - 1), 0, cod3026x_dnc_lvl_tlv),

	SOC_SINGLE_TLV("DNC Level Right", COD3026X_56_DNC3,
			DNC_LVL_R_SHIFT,
			(BIT(DNC_LVL_R_WIDTH) - 1), 0, cod3026x_dnc_lvl_tlv),

	SOC_SINGLE("DNC ZCD Timeout", COD3026X_5C_DNC9,
			DNC_ZCD_TIMEOUT_SHIFT, DNC_ZCD_TIMEOUT_MASK, 0),

	SOC_ENUM("DNC ZCD Enable", cod3026x_dnc_zcd_enable_enum),

	SOC_ENUM("MonoMix Mode", cod3026x_mono_mix_mode_enum),

	SOC_ENUM("Chargepump Mode", cod3026x_chargepump_mode_enum),

	SOC_SINGLE_EXT("DAC Soft Mute", SND_SOC_NOPM, 0, 100, 0,
			dac_soft_mute_get, dac_soft_mute_put),
};

static int dac_ev(struct snd_soc_dapm_widget *w, struct snd_kcontrol *kcontrol,
		int event)
{
	dev_dbg(w->codec->dev, "%s called\n", __func__);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* DAC digital power On */
		snd_soc_update_bits(w->codec, COD3026X_40_DIGITAL_POWER,
				PDB_DACDIG_MASK, PDB_DACDIG_MASK);

		/* DAC digital Reset On/Off */
		snd_soc_update_bits(w->codec, COD3026X_40_DIGITAL_POWER,
				RSTB_DAT_DA_MASK, 0x0);

		snd_soc_update_bits(w->codec, COD3026X_40_DIGITAL_POWER,
				RSTB_DAT_DA_MASK, RSTB_DAT_DA_MASK);

		break;

	case SND_SOC_DAPM_PRE_PMD:
		/* DAC digital Reset Off */
		snd_soc_update_bits(w->codec, COD3026X_40_DIGITAL_POWER,
				RSTB_DAT_DA_MASK, 0x0);

		/* DAC digital power Off */
		snd_soc_update_bits(w->codec, COD3026X_40_DIGITAL_POWER,
				PDB_DACDIG_MASK, 0x0);
		break;

	default:
		break;
	}

	return 0;
}

static void cod3026x_adc_digital_mute(struct snd_soc_codec *codec, bool on)
{
	if (on)
		snd_soc_update_bits(codec, COD3026X_42_ADC1,
				ADC1_MUTE_AD_EN_MASK, ADC1_MUTE_AD_EN_MASK);
	else
		snd_soc_update_bits(codec, COD3026X_42_ADC1,
				ADC1_MUTE_AD_EN_MASK, 0);
}

static int cod3026x_capture_init_manual_mode(struct snd_soc_codec *codec)
{
	dev_dbg(codec->dev, "%s called\n", __func__);

	snd_soc_update_bits(codec, COD3026X_10_PD_REF,
			PDB_VMID_MASK, PDB_VMID_MASK);

	snd_soc_update_bits(codec, COD3026X_18_CTRL_REF,
			CTMF_VMID_MASK,
			CTMF_VMID_5K_OM << CTMF_VMID_SHIFT);

	snd_soc_update_bits(codec, COD3026X_11_PD_AD1,
			EN_DSMR_PREQ_MASK | EN_DSML_PREQ_MASK,
			EN_DSMR_PREQ_MASK | EN_DSML_PREQ_MASK);

	msleep(140);

	snd_soc_update_bits(codec, COD3026X_18_CTRL_REF,
			CTMF_VMID_MASK,
			CTMF_VMID_50K_OM << CTMF_VMID_SHIFT);

	snd_soc_update_bits(codec, COD3026X_10_PD_REF,
			PDB_IGEN_MASK, PDB_IGEN_MASK);

	cod3026x_usleep(100);

	return 0;
}

static int cod3026x_capture_init(struct snd_soc_codec *codec)
{
	struct cod3026x_priv *cod3026x = snd_soc_codec_get_drvdata(codec);

	dev_dbg(codec->dev, "%s called\n", __func__);

	mutex_lock(&cod3026x->adc_mute_lock);
	/* enable ADC digital mute before configuring ADC */
	cod3026x_adc_digital_mute(codec, true);
	mutex_unlock(&cod3026x->adc_mute_lock);

	/* Recording Digital  Power on */
	snd_soc_update_bits(codec, COD3026X_40_DIGITAL_POWER,
			PDB_ADCDIG_MASK, PDB_ADCDIG_MASK);

	/* Recording Digital Reset on/off */
	snd_soc_update_bits(codec, COD3026X_40_DIGITAL_POWER,
			RSTB_DAT_AD_MASK, 0x0);
	snd_soc_update_bits(codec, COD3026X_40_DIGITAL_POWER,
			RSTB_DAT_AD_MASK, RSTB_DAT_AD_MASK);

	cod3026x_capture_init_manual_mode(codec);

	return 0;
}

static void cod3026x_capture_deinit_manual_mode(struct snd_soc_codec *codec)
{
	snd_soc_update_bits(codec, COD3026X_10_PD_REF,
			PDB_IGEN_MASK, 0);

	snd_soc_update_bits(codec, COD3026X_10_PD_REF,
			PDB_VMID_MASK, 0);
}

static int cod3026x_capture_deinit(struct snd_soc_codec *codec)
{
	struct cod3026x_priv *cod3026x = snd_soc_codec_get_drvdata(codec);

	cod3026x_capture_deinit_manual_mode(codec);

	/* Recording Digital Reset on */
	snd_soc_update_bits(codec, COD3026X_40_DIGITAL_POWER,
			RSTB_DAT_AD_MASK, 0x0);

	/* Recording Digital  Power off */
	snd_soc_update_bits(codec, COD3026X_40_DIGITAL_POWER,
			PDB_ADCDIG_MASK, 0x0);

	mutex_lock(&cod3026x->adc_mute_lock);
	/* disable ADC digital mute after configuring ADC */
	cod3026x_adc_digital_mute(codec, false);
	mutex_unlock(&cod3026x->adc_mute_lock);

	return 0;
}

static int adc_ev(struct snd_soc_dapm_widget *w, struct snd_kcontrol *kcontrol,
		int event)
{
	struct cod3026x_priv *cod3026x = snd_soc_codec_get_drvdata(w->codec);

	dev_dbg(w->codec->dev, "%s called, event = %d\n", __func__, event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		break;

	case SND_SOC_DAPM_POST_PMU:
		/* disable ADC digital mute after configuring ADC */
		queue_work(cod3026x->adc_mute_wq, &cod3026x->adc_mute_work);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		mutex_lock(&cod3026x->adc_mute_lock);
		/* disable ADC digital mute before configuring ADC */
		cod3026x_adc_digital_mute(w->codec, true);
		mutex_unlock(&cod3026x->adc_mute_lock);
		break;

	default:
		break;
	}

	return 0;
}

int cod3026x_mic_bias_ev(struct snd_soc_codec *codec, int mic_bias, int event)
{
	int is_other_mic_on, mask;

	dev_dbg(codec->dev, "%s Called, Mic bias = %d, Event = %d\n",
				__func__, mic_bias, event);

	is_other_mic_on = snd_soc_read(codec, COD3026X_10_PD_REF);
	if (mic_bias == COD3026X_MICBIAS1) {
		is_other_mic_on &= PDB_MCB2_MASK;
		mask = is_other_mic_on ? PDB_MCB1_MASK :
			PDB_MCB1_MASK | PDB_MCB_LDO_CODEC_MASK;
	} else if (mic_bias == COD3026X_MICBIAS2) {
		is_other_mic_on &= PDB_MCB1_MASK;
		mask = is_other_mic_on ? PDB_MCB2_MASK :
			PDB_MCB2_MASK | PDB_MCB_LDO_CODEC_MASK;
	} else {
		dev_err(codec->dev, "%s Called , Invalid MIC ID\n", __func__);
		return -1;
	}

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_update_bits(codec, COD3026X_10_PD_REF, mask, mask);
		if (mic_bias == COD3026X_MICBIAS2)
				snd_soc_update_bits(codec, COD3026X_18_CTRL_REF,
						CTRM_MCB2_MASK, CTRM_MCB2_MASK);
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_update_bits(codec, COD3026X_10_PD_REF, mask, 0x00);

		if (mic_bias == COD3026X_MICBIAS2)
			snd_soc_update_bits(codec, COD3026X_18_CTRL_REF,
					CTRM_MCB2_MASK, 0);
		break;
	}
	return 0;
}

/**
 * Mute mic if it is active
 *
 * Returns -1 if error, else 0
 */
static int cod3026x_mute_mic(struct snd_soc_codec *codec, bool on)
{
	struct cod3026x_priv *cod3026x = snd_soc_codec_get_drvdata(codec);

	dev_dbg(codec->dev, "%s called, %s\n", __func__,
			on ? "Mute" : "Unmute");

	if (on) {
		mutex_lock(&cod3026x->adc_mute_lock);
		cod3026x_adc_digital_mute(codec, true);
		mutex_unlock(&cod3026x->adc_mute_lock);
	} else {
		mutex_lock(&cod3026x->adc_mute_lock);
		cod3026x_adc_digital_mute(codec, false);
		mutex_unlock(&cod3026x->adc_mute_lock);
	}

	return 0;
}

/* process the button events based on the need */
void cod3026x_process_button_ev(struct snd_soc_codec *codec, int code, int on)
{
	bool key_press = on ? true : false;

	cod3026x_mute_mic(codec, key_press);
}

static int cod3026_power_on_mic1(struct snd_soc_codec *codec)
{
	unsigned int mix_val;
	unsigned int mix_val_bypass;

	dev_dbg(codec->dev, "%s called\n", __func__);

	mix_val = snd_soc_read(codec, COD3026X_23_MIX_AD1);
	mix_val &= EN_MIX_MIC1L_MASK | EN_MIX_MIC1R_MASK;
	mix_val_bypass = snd_soc_read(codec, COD3026X_24_MIX_AD2);
	mix_val_bypass &= EN_MIX_PGA_MIC1L_MASK | EN_MIX_PGA_MIC1R_MASK;

	/* Select default if no paths have been selected */
	if (!mix_val)
		mix_val = EN_MIX_MIC1L_MASK | EN_MIX_MIC1R_MASK;

	/* Reset the mixer-switches before powering on */
	snd_soc_update_bits(codec, COD3026X_23_MIX_AD1,
			EN_MIX_MIC1L_MASK | EN_MIX_MIC1R_MASK, 0x0);

	snd_soc_update_bits(codec, COD3026X_10_PD_REF,
			PDB_IGEN_AD_MASK, PDB_IGEN_AD_MASK);

	if (mix_val_bypass)
		snd_soc_update_bits(codec, COD3026X_12_PD_AD2,
				PDB_MIC_PGA1_MASK | PDB_MIC_BST1_MASK, 0x20);
	else
		snd_soc_update_bits(codec, COD3026X_12_PD_AD2,
				PDB_MIC_PGA1_MASK | PDB_MIC_BST1_MASK,
				PDB_MIC_PGA1_MASK | PDB_MIC_BST1_MASK);

	snd_soc_update_bits(codec, COD3026X_11_PD_AD1,
			PDB_MIXL_MASK | PDB_MIXR_MASK,
			PDB_MIXL_MASK | PDB_MIXR_MASK);

	snd_soc_update_bits(codec, COD3026X_11_PD_AD1,
			PDB_DSML_MASK | PDB_DSMR_MASK,
			PDB_DSML_MASK | PDB_DSMR_MASK);

	if (mix_val_bypass)
		snd_soc_update_bits(codec, COD3026X_24_MIX_AD2,
				EN_MIX_PGA_MIC1L_MASK | EN_MIX_PGA_MIC1R_MASK, mix_val_bypass);
	else
		snd_soc_update_bits(codec, COD3026X_23_MIX_AD1,
				EN_MIX_MIC1L_MASK | EN_MIX_MIC1R_MASK, mix_val);

	snd_soc_update_bits(codec, COD3026X_11_PD_AD1,
			EN_DSML_PREQ_MASK | EN_DSMR_PREQ_MASK, 0x0);

	snd_soc_update_bits(codec, COD3026X_11_PD_AD1,
			RESETB_DSML_MASK | RESETB_DSMR_MASK,
			RESETB_DSML_MASK | RESETB_DSMR_MASK);

	return 0;
}

static int cod3026_power_off_mic1(struct snd_soc_codec *codec)
{
	int other_mic;

	dev_dbg(codec->dev, "%s called\n", __func__);

	other_mic = snd_soc_read(codec, COD3026X_78_MIC_ON);
	other_mic &= (EN_MIC2_MASK | EN_MIC3_MASK | EN_LN_MASK);

	if (!other_mic)
		snd_soc_update_bits(codec, COD3026X_11_PD_AD1,
				RESETB_DSML_MASK | RESETB_DSMR_MASK, 0x0);

	snd_soc_update_bits(codec, COD3026X_24_MIX_AD2,
			EN_MIX_PGA_MIC1L_MASK | EN_MIX_PGA_MIC1R_MASK, 0);

	if (!other_mic) {
		snd_soc_update_bits(codec, COD3026X_11_PD_AD1,
				PDB_DSML_MASK | PDB_DSMR_MASK, 0);

		snd_soc_update_bits(codec, COD3026X_11_PD_AD1,
				PDB_MIXL_MASK | PDB_MIXR_MASK, 0);
	}
	snd_soc_update_bits(codec, COD3026X_12_PD_AD2,
			PDB_MIC_PGA1_MASK | PDB_MIC_BST1_MASK, 0);

	if (!other_mic)
		snd_soc_update_bits(codec, COD3026X_10_PD_REF,
				PDB_IGEN_AD_MASK, 0);

	return 0;
}

static int cod3026_power_on_mic2(struct snd_soc_codec *codec)
{
	unsigned int mix_val;
	unsigned int mix_val_bypass;

	dev_info(codec->dev, "%s called\n", __func__);

	mix_val = snd_soc_read(codec, COD3026X_23_MIX_AD1);
	mix_val &= EN_MIX_MIC2L_MASK | EN_MIX_MIC2R_MASK;
	mix_val_bypass = snd_soc_read(codec, COD3026X_24_MIX_AD2);
	mix_val_bypass &= EN_MIX_PGA_MIC2L_MASK | EN_MIX_PGA_MIC2R_MASK;

	/* Select default if no paths have been selected */
	if (!mix_val)
		mix_val = EN_MIX_MIC2L_MASK | EN_MIX_MIC2R_MASK;

	/* Reset the mixer-switches before powering on */
	snd_soc_update_bits(codec, COD3026X_23_MIX_AD1,
			EN_MIX_MIC2L_MASK | EN_MIX_MIC2R_MASK, 0x0);

	snd_soc_update_bits(codec, COD3026X_10_PD_REF,
			PDB_IGEN_AD_MASK, PDB_IGEN_AD_MASK);

	if (mix_val_bypass)
		snd_soc_update_bits(codec, COD3026X_12_PD_AD2,
				PDB_MIC_PGA2_MASK | PDB_MIC_BST2_MASK, 0x10);
	else
		snd_soc_update_bits(codec, COD3026X_12_PD_AD2,
				PDB_MIC_PGA2_MASK | PDB_MIC_BST2_MASK,
				PDB_MIC_PGA2_MASK | PDB_MIC_BST2_MASK);

	snd_soc_update_bits(codec, COD3026X_11_PD_AD1,
			PDB_MIXL_MASK | PDB_MIXR_MASK,
			PDB_MIXL_MASK | PDB_MIXR_MASK);

	snd_soc_update_bits(codec, COD3026X_11_PD_AD1,
			PDB_DSML_MASK | PDB_DSMR_MASK,
			PDB_DSML_MASK | PDB_DSMR_MASK);

	if (mix_val_bypass)
		snd_soc_update_bits(codec, COD3026X_24_MIX_AD2,
				EN_MIX_PGA_MIC2L_MASK | EN_MIX_PGA_MIC2R_MASK, mix_val_bypass);
	else
		snd_soc_update_bits(codec, COD3026X_23_MIX_AD1,
				EN_MIX_MIC2L_MASK | EN_MIX_MIC2R_MASK, mix_val);

	snd_soc_update_bits(codec, COD3026X_11_PD_AD1,
			EN_DSML_PREQ_MASK | EN_DSMR_PREQ_MASK, 0x0);

	snd_soc_update_bits(codec, COD3026X_11_PD_AD1,
			RESETB_DSML_MASK | RESETB_DSMR_MASK,
			RESETB_DSML_MASK | RESETB_DSMR_MASK);

	return 0;
}



static int cod3026_power_off_mic2(struct snd_soc_codec *codec)
{
	int other_mic;

	dev_dbg(codec->dev, "%s called\n", __func__);

	other_mic = snd_soc_read(codec, COD3026X_78_MIC_ON);
	other_mic &= (EN_MIC1_MASK | EN_MIC3_MASK | EN_LN_MASK);

	if (!other_mic)
		snd_soc_update_bits(codec, COD3026X_11_PD_AD1,
				RESETB_DSML_MASK | RESETB_DSMR_MASK, 0x0);

	snd_soc_update_bits(codec, COD3026X_24_MIX_AD2,
			EN_MIX_PGA_MIC2L_MASK | EN_MIX_PGA_MIC2R_MASK, 0);

	if (!other_mic) {
		snd_soc_update_bits(codec, COD3026X_11_PD_AD1,
				PDB_DSML_MASK | PDB_DSMR_MASK, 0);

		snd_soc_update_bits(codec, COD3026X_11_PD_AD1,
				PDB_MIXL_MASK | PDB_MIXR_MASK, 0);
	}
	snd_soc_update_bits(codec, COD3026X_12_PD_AD2,
			PDB_MIC_PGA2_MASK | PDB_MIC_BST2_MASK, 0);

	if (!other_mic)
		snd_soc_update_bits(codec, COD3026X_10_PD_REF,
				PDB_IGEN_AD_MASK, 0);

	return 0;
}

static int cod3026_power_on_mic3(struct snd_soc_codec *codec)
{
	unsigned int mix_val;
	unsigned int mix_val_bypass;

	dev_info(codec->dev, "%s called\n", __func__);

	mix_val = snd_soc_read(codec, COD3026X_23_MIX_AD1);
	mix_val &= EN_MIX_MIC3L_MASK | EN_MIX_MIC3R_MASK;
	mix_val_bypass = snd_soc_read(codec, COD3026X_24_MIX_AD2);
	mix_val_bypass &= EN_MIX_PGA_MIC3L_MASK | EN_MIX_PGA_MIC3R_MASK;

	/* Select default if no paths have been selected */
	if (!mix_val)
		mix_val = EN_MIX_MIC3L_MASK | EN_MIX_MIC3R_MASK;

	/* Reset the mixer-switches before powering on */
	snd_soc_update_bits(codec, COD3026X_23_MIX_AD1,
			EN_MIX_MIC3L_MASK | EN_MIX_MIC3R_MASK, 0x0);

	snd_soc_update_bits(codec, COD3026X_10_PD_REF,
			PDB_IGEN_AD_MASK, PDB_IGEN_AD_MASK);

	if (mix_val_bypass)
		snd_soc_update_bits(codec, COD3026X_12_PD_AD2,
				PDB_MIC_PGA3_MASK | PDB_MIC_BST3_MASK, 0x08);
	else
		snd_soc_update_bits(codec, COD3026X_12_PD_AD2,
				PDB_MIC_PGA3_MASK | PDB_MIC_BST3_MASK,
				PDB_MIC_PGA3_MASK | PDB_MIC_BST3_MASK);

	snd_soc_update_bits(codec, COD3026X_11_PD_AD1,
			PDB_MIXL_MASK | PDB_MIXR_MASK,
			PDB_MIXL_MASK | PDB_MIXR_MASK);

	snd_soc_update_bits(codec, COD3026X_11_PD_AD1,
			PDB_DSML_MASK | PDB_DSMR_MASK,
			PDB_DSML_MASK | PDB_DSMR_MASK);

	if (mix_val_bypass)
		snd_soc_update_bits(codec, COD3026X_24_MIX_AD2,
				EN_MIX_PGA_MIC3L_MASK | EN_MIX_PGA_MIC3R_MASK, mix_val_bypass);
	else
		snd_soc_update_bits(codec, COD3026X_23_MIX_AD1,
			EN_MIX_MIC3L_MASK | EN_MIX_MIC3R_MASK, mix_val);

	snd_soc_update_bits(codec, COD3026X_11_PD_AD1,
			EN_DSML_PREQ_MASK | EN_DSMR_PREQ_MASK, 0x0);

	snd_soc_update_bits(codec, COD3026X_11_PD_AD1,
			RESETB_DSML_MASK | RESETB_DSMR_MASK,
			RESETB_DSML_MASK | RESETB_DSMR_MASK);

	return 0;
}

static int cod3026_power_off_mic3(struct snd_soc_codec *codec)
{
	int other_mic;

	dev_dbg(codec->dev, "%s called\n", __func__);

	other_mic = snd_soc_read(codec, COD3026X_78_MIC_ON);
	other_mic &= (EN_MIC1_MASK | EN_MIC2_MASK | EN_LN_MASK);

	if (!other_mic)
		snd_soc_update_bits(codec, COD3026X_11_PD_AD1,
				RESETB_DSML_MASK | RESETB_DSMR_MASK, 0);

	snd_soc_update_bits(codec, COD3026X_24_MIX_AD2,
			EN_MIX_PGA_MIC3L_MASK | EN_MIX_PGA_MIC3R_MASK, 0);

	if (!other_mic) {
		snd_soc_update_bits(codec, COD3026X_11_PD_AD1,
				PDB_DSML_MASK | PDB_DSMR_MASK, 0);

		snd_soc_update_bits(codec, COD3026X_11_PD_AD1,
				PDB_MIXL_MASK | PDB_MIXR_MASK, 0);
	}
	snd_soc_update_bits(codec, COD3026X_12_PD_AD2,
			PDB_MIC_PGA3_MASK | PDB_MIC_BST3_MASK, 0);

	if (!other_mic)
		snd_soc_update_bits(codec, COD3026X_10_PD_REF,
				PDB_IGEN_AD_MASK, 0);

	return 0;
}

static int cod3026_power_on_linein(struct snd_soc_codec *codec)
{
	unsigned int mix_val;

	dev_info(codec->dev, "%s called\n", __func__);

	mix_val = snd_soc_read(codec, COD3026X_23_MIX_AD1);
	mix_val &= EN_MIX_LNLL_MASK | EN_MIX_LNLR_MASK;

	/* Select default if no paths have been selected */
	if (!mix_val)
		mix_val = EN_MIX_LNLL_MASK | EN_MIX_LNLR_MASK;

	/* Reset the mixer-switches before powering on */
	snd_soc_update_bits(codec, COD3026X_23_MIX_AD1,
			EN_MIX_LNLL_MASK | EN_MIX_LNLR_MASK, 0x0);

	snd_soc_update_bits(codec, COD3026X_10_PD_REF,
			PDB_IGEN_AD_MASK, PDB_IGEN_AD_MASK);

	snd_soc_update_bits(codec, COD3026X_12_PD_AD2,
			PDB_LNL_MASK | PDB_LNR_MASK,
			PDB_LNL_MASK | PDB_LNR_MASK);

	snd_soc_update_bits(codec, COD3026X_11_PD_AD1,
			PDB_MIXL_MASK | PDB_MIXR_MASK,
			PDB_MIXL_MASK | PDB_MIXR_MASK);

	snd_soc_update_bits(codec, COD3026X_11_PD_AD1,
			PDB_DSML_MASK | PDB_DSMR_MASK,
			PDB_DSML_MASK | PDB_DSMR_MASK);

	snd_soc_update_bits(codec, COD3026X_23_MIX_AD1,
			EN_MIX_LNLL_MASK | EN_MIX_LNLR_MASK, mix_val);

	snd_soc_update_bits(codec, COD3026X_11_PD_AD1,
			EN_DSML_PREQ_MASK | EN_DSMR_PREQ_MASK, 0x0);

	snd_soc_update_bits(codec, COD3026X_11_PD_AD1,
			RESETB_DSML_MASK | RESETB_DSMR_MASK,
			RESETB_DSML_MASK | RESETB_DSMR_MASK);

	return 0;
}

static int cod3026_power_off_linein(struct snd_soc_codec *codec)
{
	int other_mic;

	dev_dbg(codec->dev, "%s called\n", __func__);

	other_mic = snd_soc_read(codec, COD3026X_78_MIC_ON);
	other_mic &= (EN_MIC1_MASK | EN_MIC2_MASK | EN_MIC3_MASK);

	if (!other_mic)
		snd_soc_update_bits(codec, COD3026X_11_PD_AD1,
				RESETB_DSML_MASK | RESETB_DSMR_MASK, 0);

	if (!other_mic) {
		snd_soc_update_bits(codec, COD3026X_11_PD_AD1,
				PDB_DSML_MASK | PDB_DSMR_MASK, 0);

		snd_soc_update_bits(codec, COD3026X_11_PD_AD1,
				PDB_MIXL_MASK | PDB_MIXR_MASK, 0);
	}

	snd_soc_update_bits(codec, COD3026X_12_PD_AD2,
			PDB_LNL_MASK | PDB_LNR_MASK, 0);

	if (!other_mic)
		snd_soc_update_bits(codec, COD3026X_10_PD_REF,
				PDB_IGEN_AD_MASK, 0);

	return 0;
}

static int vmid_ev(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	dev_dbg(w->codec->dev, "%s called\n", __func__);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		cod3026x_capture_init(w->codec);
		break;

	case SND_SOC_DAPM_POST_PMU:
		break;

	case SND_SOC_DAPM_PRE_PMD:
		cod3026x_capture_deinit(w->codec);
		break;

	default:
		break;
	}

	return 0;
}

/**
 * SFR revision 1.14 recommends following settings.
 * If playback is only HP mode, write f/w OTP value at 0xD4 and 0xD5 and enable
 * DNC.
 * If playback is not only HP mode, write all zero data value at 0xD4 and 0xD5
 * with DNC disabled.
 */
static void cod3026x_update_playback_otp(struct snd_soc_codec *codec)
{
	int hp_on, spk_on, ep_on;
	int chop_val;
	int offset;
	int offset_ep;
	struct cod3026x_priv *cod3026x = snd_soc_codec_get_drvdata(codec);

	chop_val = snd_soc_read(codec, COD3026X_76_CHOP_DA);
	hp_on = chop_val & EN_HP_CHOP_MASK;
	spk_on = chop_val & EN_SPK_PGA_CHOP_MASK;
	ep_on = chop_val & EN_EP_CHOP_MASK;

	if (!hp_on && !spk_on && !ep_on) {
		dev_warn(codec->dev, "None of the output paths selected.\n");
		return;
	}

	if (hp_on && !spk_on && !ep_on) {
		/* We are in HP only mode */
		/* Updating OTP register 0xD4 */
		offset = COD3026X_D4_OFFSET_DAL - COD3026X_OTP_REG_WRITE_START;
		snd_soc_write(codec, COD3026X_D4_OFFSET_DAL,
				cod3026x->otp_reg[offset]);

		/* Updating OTP register 0xD5 */
		offset = COD3026X_D5_OFFSET_DAR - COD3026X_OTP_REG_WRITE_START;
		snd_soc_write(codec, COD3026X_D5_OFFSET_DAR,
				cod3026x->otp_reg[offset]);

	} else if (!hp_on && !spk_on && ep_on) {
		/* We are in EP only mode */
		offset_ep = snd_soc_read(codec, COD3026X_D7_CTRL_EP);
		snd_soc_write(codec, COD3026X_D4_OFFSET_DAL,
				offset_ep);

	} else if (hp_on && spk_on && !ep_on) {
		/* We are in HP & SPK  mode */
		/* Updating OTP register 0xD4 */
		offset = COD3026X_D4_OFFSET_DAL - COD3026X_OTP_REG_WRITE_START;
		snd_soc_write(codec, COD3026X_D4_OFFSET_DAL,
				cod3026x->otp_reg[offset]);

		/* Updating OTP register 0xD5 */
		offset = COD3026X_D5_OFFSET_DAR - COD3026X_OTP_REG_WRITE_START;
		snd_soc_write(codec, COD3026X_D5_OFFSET_DAR,
				cod3026x->otp_reg[offset]);
	} else {
		/* This is not-only HP mode */
		snd_soc_write(codec, COD3026X_D4_OFFSET_DAL, 0x0);
		snd_soc_write(codec, COD3026X_D5_OFFSET_DAR, 0x0);
	}

	if (!(hp_on && !spk_on && !ep_on)) {
		/* This is not-only HP mode */
		/* Disable DNC */
		snd_soc_update_bits(codec, COD3026X_54_DNC1,
				EN_DNC_MASK , 0x0);

		cod3026x_usleep(100);
	}
}

static int cod3026x_hp_playback_init(struct snd_soc_codec *codec)
{
	dev_dbg(codec->dev, "%s called\n", __func__);

	/* Enable DNC Start gain*/
	snd_soc_update_bits(codec, COD3026X_54_DNC1,
			DNC_START_GAIN_MASK, DNC_START_GAIN_MASK);

	/* Set DNC Start gain value update*/
	snd_soc_write(codec, COD3026X_5A_DNC7, 0x1A);

	snd_soc_update_bits(codec, COD3026X_54_DNC1, EN_DNC_MASK, EN_DNC_MASK);
	cod3026x_usleep(100);
	snd_soc_update_bits(codec, COD3026X_54_DNC1, EN_DNC_MASK, 0);

	/* set HP volume Level */
	snd_soc_write(codec, COD3026X_30_VOL_HPL, 0x1A);
	snd_soc_write(codec, COD3026X_31_VOL_HPR, 0x1A);

	/* SKIP HP VOL */
	snd_soc_update_bits(codec, COD3026X_19_SV_HP,
			SKIP_HP_SV_MASK, 0x01 << SKIP_HP_SV_SHIFT);

	/* Update OTP configuration */
	cod3026x_update_playback_otp(codec);

	/* DNC Window selection set to 20Hz time window */
	snd_soc_update_bits(codec, COD3026X_57_DNC4, DNC_WINSEL_MASK,
			(DNC_WIN_SIZE_20HZ << DNC_WINSEL_SHIFT));

	cod3026x_usleep(100);

	return 0;
}

static int spkdrv_ev(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	int hp_on, spk_on, ep_on;
	int chop_val;
	unsigned int spk_gain;
	int offset;
	unsigned int mix_val;
	struct cod3026x_priv *cod3026x = snd_soc_codec_get_drvdata(w->codec);

	chop_val = snd_soc_read(w->codec, COD3026X_76_CHOP_DA);
	hp_on = chop_val & EN_HP_CHOP_MASK;
	spk_on = chop_val & EN_SPK_PGA_CHOP_MASK;
	ep_on = chop_val & EN_EP_CHOP_MASK;

	if (!spk_on) {
		dev_dbg(w->codec->dev, "%s called but speaker not enabled\n",
				__func__);
		return 0;
	}
	dev_dbg(w->codec->dev, "%s called event=%d\n", __func__, event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Update OTP configuration */
		cod3026x_update_playback_otp(w->codec);

		/* CP Freq setting scenario rev 0.1*/
		snd_soc_write(w->codec, COD3026X_DD_CTRL_SPKS1, 0x82);

		mix_val = snd_soc_read(w->codec, COD3026X_37_MIX_DA2);
		mix_val &= EN_SPK_MIX_DCTL_MASK | EN_SPK_MIX_DCTR_MASK |
			EN_SPK_MIX_MIXL_MASK | EN_SPK_MIX_MIXR_MASK;

		/* Keep DAC path enabled by default */
		mix_val |= EN_SPK_MIX_DCTL_MASK | EN_SPK_MIX_DCTR_MASK;

		snd_soc_update_bits(w->codec, COD3026X_37_MIX_DA2,
				EN_SPK_MIX_DCTL_MASK | EN_SPK_MIX_DCTR_MASK, 0);

		spk_gain = snd_soc_read(w->codec, COD3026X_32_VOL_EP_SPK);

		snd_soc_update_bits(w->codec, COD3026X_32_VOL_EP_SPK,
				CTVOL_SPK_PGA_MASK, 0);

		snd_soc_update_bits(w->codec, COD3026X_17_PWAUTO_DA,
				PW_AUTO_DA_MASK | APW_SPK_MASK,
				PW_AUTO_DA_MASK | APW_SPK_MASK);

		snd_soc_update_bits(w->codec, COD3026X_37_MIX_DA2,
				EN_SPK_MIX_DCTL_MASK | EN_SPK_MIX_DCTR_MASK |
				EN_SPK_MIX_MIXL_MASK | EN_SPK_MIX_MIXR_MASK,
				mix_val);

		msleep(135);

		snd_soc_update_bits(w->codec, COD3026X_32_VOL_EP_SPK,
				CTVOL_SPK_PGA_MASK, spk_gain);

		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(w->codec, COD3026X_32_VOL_EP_SPK,
				CTVOL_SPK_PGA_MASK, 0x6);

		if (hp_on || ep_on)
			snd_soc_update_bits(w->codec, COD3026X_17_PWAUTO_DA,
					APW_SPK_MASK, 0);
		else
			snd_soc_update_bits(w->codec, COD3026X_17_PWAUTO_DA,
					PW_AUTO_DA_MASK | APW_SPK_MASK, 0);

		cod3026x_usleep(200);

		snd_soc_update_bits(w->codec, COD3026X_37_MIX_DA2,
				EN_SPK_MIX_DCTL_MASK | EN_SPK_MIX_DCTR_MASK, 0);

		cod3026x_usleep(100);
		/* Check HP is ON */
		if (hp_on) {
			/* We are in HP only mode */
			/* Updating OTP register 0xD4 */
			offset = COD3026X_D4_OFFSET_DAL - COD3026X_OTP_REG_WRITE_START;
			snd_soc_write(w->codec, COD3026X_D4_OFFSET_DAL,
					cod3026x->otp_reg[offset]);

			/* Updating OTP register 0xD5 */
			offset = COD3026X_D5_OFFSET_DAR - COD3026X_OTP_REG_WRITE_START;
			snd_soc_write(w->codec, COD3026X_D5_OFFSET_DAR,
					cod3026x->otp_reg[offset]);
			snd_soc_write(w->codec, COD3026X_30_VOL_HPL, 0x1A);
			snd_soc_write(w->codec, COD3026X_31_VOL_HPR, 0x1A);
			msleep(6);

			/* enable DNC */
			snd_soc_update_bits(w->codec,
					COD3026X_54_DNC1, EN_DNC_MASK, EN_DNC_MASK);
		}
		break;
	default:
		break;
	}

	return 0;
}

static int hpdrv_ev(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	int hp_on, spk_on, ep_on;
	int chop_val;
	struct cod3026x_priv *cod3026x = snd_soc_codec_get_drvdata(w->codec);

	chop_val = snd_soc_read(w->codec, COD3026X_76_CHOP_DA);
	hp_on = chop_val & EN_HP_CHOP_MASK;
	spk_on = chop_val & EN_SPK_PGA_CHOP_MASK;
	ep_on = chop_val & EN_EP_CHOP_MASK;

	if (!hp_on) {
		dev_dbg(w->codec->dev, "%s called but headphone not enabled\n",
				__func__);
		return 0;
	}

	dev_dbg(w->codec->dev, "%s called, event = %d\n", __func__, event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		cod3026x->vol_hpl = snd_soc_read(w->codec, COD3026X_30_VOL_HPL);
		cod3026x->vol_hpr = snd_soc_read(w->codec, COD3026X_31_VOL_HPR);

		cod3026x_hp_playback_init(w->codec);
		break;

	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(w->codec, COD3026X_17_PWAUTO_DA,
				PW_AUTO_DA_MASK | APW_HP_MASK,
				PW_AUTO_DA_MASK | APW_HP_MASK);

		snd_soc_update_bits(w->codec, COD3026X_36_MIX_DA1,
			EN_HP_MIXL_DCTL_MASK | EN_HP_MIXR_DCTR_MASK,
			EN_HP_MIXL_DCTL_MASK | EN_HP_MIXR_DCTR_MASK);

		msleep(180);

		/* SKIP HP VOL OFF */
		snd_soc_update_bits(w->codec, COD3026X_19_SV_HP,
				SKIP_HP_SV_MASK, 0x0);

		if (!spk_on && !ep_on) {
			/* Only HP is on, enable DNC and set default analog HP
			 * volume
			 */
			snd_soc_write(w->codec, COD3026X_30_VOL_HPL, 0x1A);
			snd_soc_write(w->codec, COD3026X_31_VOL_HPR, 0x1A);
			msleep(6);

			/* Limiter level selection -0.2dB (defult) */
			snd_soc_update_bits(w->codec, COD3026X_54_DNC1,
					EN_DNC_MASK, EN_DNC_MASK);
		} else {
			/* Either SPK or EP is on, disable DNC and set given
			 * analog HP volume
			 */
			snd_soc_write(w->codec, COD3026X_30_VOL_HPL,
					cod3026x->vol_hpl);
			snd_soc_write(w->codec, COD3026X_31_VOL_HPR,
					cod3026x->vol_hpr);
		}

		break;

	case SND_SOC_DAPM_PRE_PMD:

		snd_soc_update_bits(w->codec, COD3026X_54_DNC1,
				EN_DNC_MASK , 0);

		/* SKIP HP VOL ON */
		snd_soc_update_bits(w->codec, COD3026X_19_SV_HP,
				SKIP_HP_SV_MASK, SKIP_HP_SV_MASK);

		if (spk_on || ep_on)
			snd_soc_update_bits(w->codec, COD3026X_17_PWAUTO_DA,
					APW_HP_MASK, 0);
		else
			snd_soc_update_bits(w->codec, COD3026X_17_PWAUTO_DA,
					PW_AUTO_DA_MASK | APW_HP_MASK, 0);

		snd_soc_update_bits(w->codec, COD3026X_36_MIX_DA1,
				EN_HP_MIXL_DCTL_MASK | EN_HP_MIXR_DCTR_MASK, 0);
		msleep(40);
		/* SKIP HP VOL OFF */
		snd_soc_update_bits(w->codec, COD3026X_19_SV_HP,
				SKIP_HP_SV_MASK, 0x0);

		break;

	default:
		break;
	}

	return 0;
}

static int epdrv_ev(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	int hp_on, spk_on, ep_on;
	int chop_val;

	chop_val = snd_soc_read(w->codec, COD3026X_76_CHOP_DA);
	hp_on = chop_val & EN_HP_CHOP_MASK;
	spk_on = chop_val & EN_SPK_PGA_CHOP_MASK;
	ep_on = chop_val & EN_EP_CHOP_MASK;

	if (!ep_on) {
		dev_dbg(w->codec->dev, "%s called but ear-piece not enabled\n",
				__func__);
		return 0;
	}
	dev_dbg(w->codec->dev, "%s called, event = %d\n", __func__, event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Update OTP configuration */
		cod3026x_update_playback_otp(w->codec);

		snd_soc_update_bits(w->codec, COD3026X_17_PWAUTO_DA,
				APW_EP_MASK | PW_AUTO_DA_MASK,
				APW_EP_MASK | PW_AUTO_DA_MASK);

		snd_soc_update_bits(w->codec, COD3026X_37_MIX_DA2,
				EN_EP_MIX_DCTL_MASK, EN_EP_MIX_DCTL_MASK);

		msleep(136);

		break;

	case SND_SOC_DAPM_PRE_PMD:
		if (spk_on || hp_on)
			snd_soc_update_bits(w->codec, COD3026X_17_PWAUTO_DA,
					APW_EP_MASK, 0x0);
		else
			snd_soc_update_bits(w->codec, COD3026X_17_PWAUTO_DA,
					PW_AUTO_DA_MASK | APW_EP_MASK, 0x0);

		cod3026x_usleep(100);

		snd_soc_update_bits(w->codec, COD3026X_37_MIX_DA2,
				EN_EP_MIX_DCTL_MASK, 0x0);

		cod3026x_usleep(100);

		break;

	default:
		break;
	}

	return 0;
}

static int mic2_pga_ev(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	int mic_on;

	dev_dbg(w->codec->dev, "%s called, event = %d\n", __func__, event);

	mic_on = snd_soc_read(w->codec, COD3026X_78_MIC_ON);
	if (!(mic_on & EN_MIC2_MASK)) {
		dev_dbg(w->codec->dev, "%s: MIC2 is not enabled, returning.\n",
				__func__);
		return 0;
	}

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		cod3026_power_on_mic2(w->codec);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		cod3026_power_off_mic2(w->codec);
		break;
	default:
		break;
	}

	return 0;
}

static int mic1_pga_ev(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	int mic_on;

	dev_dbg(w->codec->dev, "%s called, event = %d\n", __func__, event);

	mic_on = snd_soc_read(w->codec, COD3026X_78_MIC_ON);
	if (!(mic_on & EN_MIC1_MASK)) {
		dev_dbg(w->codec->dev, "%s: MIC1 is not enabled, returning.\n",
				__func__);
		return 0;
	}

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		cod3026_power_on_mic1(w->codec);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		cod3026_power_off_mic1(w->codec);
		break;

	default:
		break;
	}

	return 0;
}

static int mic3_pga_ev(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	int mic_on;

	dev_dbg(w->codec->dev, "%s called, event = %d\n", __func__, event);

	mic_on = snd_soc_read(w->codec, COD3026X_78_MIC_ON);
	if (!(mic_on & EN_MIC3_MASK)) {
		dev_dbg(w->codec->dev, "%s: MIC3 is not enabled, returning.\n",
				__func__);
		return 0;
	}

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		cod3026_power_on_mic3(w->codec);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		cod3026_power_off_mic3(w->codec);
		break;

	default:
		break;
	}

	return 0;
}

static int linein_pga_ev(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	int linein_on;

	dev_dbg(w->codec->dev, "%s called, event = %d\n", __func__, event);

	linein_on = snd_soc_read(w->codec, COD3026X_78_MIC_ON);
	if (!(linein_on & EN_LN_MASK)) {
		dev_dbg(w->codec->dev, "%s: LINE IN is not enabled, returning.\n",
				__func__);
		return 0;
	}

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		cod3026_power_on_linein(w->codec);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		cod3026_power_off_linein(w->codec);
		break;

	default:
		break;
	}

	return 0;
}

static const struct snd_kcontrol_new adcl_mix[] = {
	SOC_DAPM_SINGLE("MIC1L Switch", COD3026X_23_MIX_AD1,
			EN_MIX_MIC1L_SHIFT, 1, 0),
	SOC_DAPM_SINGLE("MIC2L Switch", COD3026X_23_MIX_AD1,
			EN_MIX_MIC2L_SHIFT, 1, 0),
	SOC_DAPM_SINGLE("MIC3L Switch", COD3026X_23_MIX_AD1,
			EN_MIX_MIC3L_SHIFT, 1, 0),
	SOC_DAPM_SINGLE("LINELL Switch", COD3026X_23_MIX_AD1,
			EN_MIX_LNLL_SHIFT, 1, 0),
	SOC_DAPM_SINGLE("LINERL Switch", COD3026X_24_MIX_AD2,
			EN_MIX_LNRL_SHIFT, 1, 0),
	SOC_DAPM_SINGLE("MIC1L Bypass", COD3026X_24_MIX_AD2,
			EN_MIX_PGA_MIC1L_SHIFT, 1, 0),
	SOC_DAPM_SINGLE("MIC2L Bypass", COD3026X_24_MIX_AD2,
			EN_MIX_PGA_MIC2L_SHIFT, 1, 0),
	SOC_DAPM_SINGLE("MIC3L Bypass", COD3026X_24_MIX_AD2,
			EN_MIX_PGA_MIC3L_SHIFT, 1, 0),
};

static const struct snd_kcontrol_new adcr_mix[] = {
	SOC_DAPM_SINGLE("MIC1R Switch", COD3026X_23_MIX_AD1,
			EN_MIX_MIC1R_SHIFT, 1, 0),
	SOC_DAPM_SINGLE("MIC2R Switch", COD3026X_23_MIX_AD1,
			EN_MIX_MIC2R_SHIFT, 1, 0),
	SOC_DAPM_SINGLE("MIC3R Switch", COD3026X_23_MIX_AD1,
			EN_MIX_MIC3R_SHIFT, 1, 0),
	SOC_DAPM_SINGLE("LINELR Switch", COD3026X_24_MIX_AD2,
			EN_MIX_LNLR_SHIFT, 1, 0),
	SOC_DAPM_SINGLE("LINERR Switch", COD3026X_23_MIX_AD1,
			EN_MIX_LNRR_SHIFT, 1, 0),
	SOC_DAPM_SINGLE("MIC1R Bypass", COD3026X_24_MIX_AD2,
			EN_MIX_PGA_MIC1R_SHIFT, 1, 0),
	SOC_DAPM_SINGLE("MIC2R Bypass", COD3026X_24_MIX_AD2,
			EN_MIX_PGA_MIC2R_SHIFT, 1, 0),
	SOC_DAPM_SINGLE("MIC3R Bypass", COD3026X_24_MIX_AD2,
			EN_MIX_PGA_MIC3R_SHIFT, 1, 0),
};

static const struct snd_kcontrol_new hpl_mix[] = {
	SOC_DAPM_SINGLE("DACL Switch", COD3026X_36_MIX_DA1,
			EN_HP_MIXL_DCTL_SHIFT, 1, 0),
	SOC_DAPM_SINGLE("DACR Switch", COD3026X_36_MIX_DA1,
			EN_HP_MIXL_DCTR_SHIFT, 1, 0),
	SOC_DAPM_SINGLE("ADCL Switch", COD3026X_36_MIX_DA1,
			EN_HP_MIXL_MIXL_SHIFT, 1, 0),
	SOC_DAPM_SINGLE("ADCR Switch", COD3026X_36_MIX_DA1,
			EN_HP_MIXL_MIXR_SHIFT, 1, 0),
};

static const struct snd_kcontrol_new hpr_mix[] = {
	SOC_DAPM_SINGLE("DACL Switch", COD3026X_36_MIX_DA1,
			EN_HP_MIXR_DCTL_SHIFT, 1, 0),
	SOC_DAPM_SINGLE("DACR Switch", COD3026X_36_MIX_DA1,
			EN_HP_MIXR_DCTR_SHIFT, 1, 0),
	SOC_DAPM_SINGLE("ADCL Switch", COD3026X_36_MIX_DA1,
			EN_HP_MIXR_MIXL_SHIFT, 1, 0),
	SOC_DAPM_SINGLE("ADCR Switch", COD3026X_36_MIX_DA1,
			EN_HP_MIXR_MIXR_SHIFT, 1, 0),
};

static const struct snd_kcontrol_new ep_mix[] = {
	SOC_DAPM_SINGLE("DACL Switch", COD3026X_37_MIX_DA2,
			EN_EP_MIX_DCTL_SHIFT, 1, 0),
	SOC_DAPM_SINGLE("DACR Switch", COD3026X_37_MIX_DA2,
			EN_EP_MIX_DCTR_SHIFT, 1, 0),
	SOC_DAPM_SINGLE("ADCL Switch", COD3026X_37_MIX_DA2,
			EN_EP_MIX_MIXL_SHIFT, 1, 0),
	SOC_DAPM_SINGLE("ADCR Switch", COD3026X_37_MIX_DA2,
			EN_EP_MIX_MIXR_SHIFT, 1, 0),
};

static const struct snd_kcontrol_new spk_mix[] = {
	SOC_DAPM_SINGLE("DACL Switch", COD3026X_37_MIX_DA2,
			EN_SPK_MIX_DCTL_SHIFT, 1, 0),
	SOC_DAPM_SINGLE("DACR Switch", COD3026X_37_MIX_DA2,
			EN_SPK_MIX_DCTR_SHIFT, 1, 0),
	SOC_DAPM_SINGLE("ADCL Switch", COD3026X_37_MIX_DA2,
			EN_SPK_MIX_MIXL_SHIFT, 1, 0),
	SOC_DAPM_SINGLE("ADCR Switch", COD3026X_37_MIX_DA2,
			EN_SPK_MIX_MIXR_SHIFT, 1, 0),
};

static const struct snd_kcontrol_new spk_on[] = {
	SOC_DAPM_SINGLE("SPK On", COD3026X_76_CHOP_DA,
				EN_SPK_PGA_CHOP_SHIFT, 1, 0),
};

static const struct snd_kcontrol_new hp_on[] = {
	SOC_DAPM_SINGLE("HP On", COD3026X_76_CHOP_DA, EN_HP_CHOP_SHIFT, 1, 0),
};

static const struct snd_kcontrol_new ep_on[] = {
	SOC_DAPM_SINGLE("EP On", COD3026X_76_CHOP_DA, EN_EP_CHOP_SHIFT, 1, 0),
};

static const struct snd_kcontrol_new mic1_on[] = {
	SOC_DAPM_SINGLE("MIC1 On", COD3026X_78_MIC_ON,
					EN_MIC1_SHIFT, 1, 0),
};

static const struct snd_kcontrol_new mic2_on[] = {
	SOC_DAPM_SINGLE("MIC2 On", COD3026X_78_MIC_ON,
					EN_MIC2_SHIFT, 1, 0),
};

static const struct snd_kcontrol_new mic3_on[] = {
	SOC_DAPM_SINGLE("MIC3 On", COD3026X_78_MIC_ON,
					EN_MIC3_SHIFT, 1, 0),
};

static const struct snd_kcontrol_new linein_on[] = {
	SOC_DAPM_SINGLE("LINEIN On", COD3026X_78_MIC_ON,
					EN_LN_SHIFT, 1, 0),
};

static const char * const cod3026x_fm_texts[] = {
	"None",
	"FM On",
};

static SOC_ENUM_SINGLE_VIRT_DECL(cod3026x_fm_enum, cod3026x_fm_texts);

static const struct snd_kcontrol_new cod3026x_fm_mux[] = {
	SOC_DAPM_ENUM("FM Link", cod3026x_fm_enum),
};

static const struct snd_soc_dapm_widget cod3026x_dapm_widgets[] = {
	SND_SOC_DAPM_SWITCH("SPK", SND_SOC_NOPM, 0, 0, spk_on),
	SND_SOC_DAPM_SWITCH("HP", SND_SOC_NOPM, 0, 0, hp_on),
	SND_SOC_DAPM_SWITCH("EP", SND_SOC_NOPM, 0, 0, ep_on),
	SND_SOC_DAPM_SWITCH("MIC1", SND_SOC_NOPM, 0, 0, mic1_on),
	SND_SOC_DAPM_SWITCH("MIC2", SND_SOC_NOPM, 0, 0, mic2_on),
	SND_SOC_DAPM_SWITCH("MIC3", SND_SOC_NOPM, 0, 0, mic3_on),
	SND_SOC_DAPM_SWITCH("LINEIN", SND_SOC_NOPM, 0, 0, linein_on),

	SND_SOC_DAPM_SUPPLY("VMID", SND_SOC_NOPM, 0, 0, vmid_ev,
			SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_OUT_DRV_E("SPKDRV", SND_SOC_NOPM, 0, 0, NULL, 0,
			spkdrv_ev, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_OUT_DRV_E("EPDRV", SND_SOC_NOPM, 0, 0, NULL, 0,
			epdrv_ev, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_OUT_DRV_E("HPDRV", SND_SOC_NOPM, 0, 0, NULL, 0,
			hpdrv_ev, SND_SOC_DAPM_PRE_PMU |
			SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_MUX("FM Link", SND_SOC_NOPM, 0, 0,
			cod3026x_fm_mux),
	SND_SOC_DAPM_PGA_E("MIC1_PGA", SND_SOC_NOPM, 0, 0,
			NULL, 0, mic1_pga_ev,
			SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_PGA_E("MIC2_PGA", SND_SOC_NOPM, 0, 0,
			NULL, 0, mic2_pga_ev,
			SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_PGA_E("MIC3_PGA", SND_SOC_NOPM, 0, 0,
			NULL, 0, mic3_pga_ev,
			SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_PGA_E("LINEIN_PGA", SND_SOC_NOPM, 0, 0,
			NULL, 0, linein_pga_ev,
			SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_MIXER("ADCL Mixer", SND_SOC_NOPM, 0, 0, adcl_mix,
			ARRAY_SIZE(adcl_mix)),
	SND_SOC_DAPM_MIXER("ADCR Mixer", SND_SOC_NOPM, 0, 0, adcr_mix,
			ARRAY_SIZE(adcr_mix)),
	SND_SOC_DAPM_MIXER("HPL Mixer", SND_SOC_NOPM, 0, 0, hpl_mix,
			ARRAY_SIZE(hpl_mix)),
	SND_SOC_DAPM_MIXER("HPR Mixer", SND_SOC_NOPM, 0, 0, hpr_mix,
			ARRAY_SIZE(hpr_mix)),
	SND_SOC_DAPM_MIXER("EP Mixer", SND_SOC_NOPM, 0, 0, ep_mix,
			ARRAY_SIZE(ep_mix)),
	SND_SOC_DAPM_MIXER("SPK Mixer", SND_SOC_NOPM, 0, 0, spk_mix,
			ARRAY_SIZE(spk_mix)),

	SND_SOC_DAPM_DAC_E("DAC", "AIF Playback", SND_SOC_NOPM, 0, 0,
			dac_ev, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_DAC_E("DAC", "AIF2 Playback", SND_SOC_NOPM, 0, 0,
			dac_ev, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_ADC_E("ADC", "AIF Capture", SND_SOC_NOPM, 0, 0,
			adc_ev, SND_SOC_DAPM_PRE_PMU |
			SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_ADC_E("ADC", "AIF2 Capture", SND_SOC_NOPM, 0, 0,
			adc_ev, SND_SOC_DAPM_PRE_PMU |
			SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_OUTPUT("SPKOUTLN"),
	SND_SOC_DAPM_OUTPUT("HPOUTLN"),
	SND_SOC_DAPM_OUTPUT("EPOUTN"),
	SND_SOC_DAPM_OUTPUT("AIF4OUT"),

	SND_SOC_DAPM_INPUT("IN1L"),
	SND_SOC_DAPM_INPUT("IN2L"),
	SND_SOC_DAPM_INPUT("IN3L"),
	SND_SOC_DAPM_INPUT("IN4L"),

	SND_SOC_DAPM_INPUT("AIF4IN"),
};

static const struct snd_soc_dapm_route cod3026x_dapm_routes[] = {
	/* Sink, Control, Source */
	{"SPK Mixer", "ADCL Switch", "ADCL Mixer"},
	{"SPK Mixer", "ADCR Switch", "ADCR Mixer"},
	{"SPK Mixer", "DACL Switch", "DAC"},
	{"SPK Mixer", "DACR Switch", "DAC"},

	{"SPKDRV", NULL, "SPK Mixer"},
	{"SPKDRV", NULL, "DAC"},
	{"SPK" , "SPK On", "SPKDRV"},
	{"SPKOUTLN", NULL, "SPK"},

	{"EP Mixer", "ADCL Switch", "ADCL Mixer"},
	{"EP Mixer", "ADCR Switch", "ADCR Mixer"},
	{"EP Mixer", "DACL Switch", "DAC"},
	{"EP Mixer", "DACR Switch", "DAC"},

	{"EPDRV", NULL, "EP Mixer"},
	{"EPDRV", NULL, "DAC"},
	{"EP", "EP On", "EPDRV"},
	{"EPOUTN", NULL, "EP"},

	{"HPL Mixer", "ADCL Switch", "ADCL Mixer"},
	{"HPL Mixer", "ADCR Switch", "ADCR Mixer"},
	{"HPL Mixer", "DACL Switch", "DAC"},
	{"HPL Mixer", "DACR Switch", "DAC"},

	{"HPR Mixer", "ADCL Switch", "ADCL Mixer"},
	{"HPR Mixer", "ADCR Switch", "ADCR Mixer"},
	{"HPR Mixer", "DACL Switch", "DAC"},
	{"HPR Mixer", "DACR Switch", "DAC"},

	{"HPDRV", NULL, "HPL Mixer"},
	{"HPDRV", NULL, "HPR Mixer"},
	{"HPDRV", NULL, "DAC"},
	{"HP", "HP On", "HPDRV"},
	{"HPOUTLN", NULL, "HP"},

	{"DAC" , NULL, "AIF Playback"},
	{"DAC" , NULL, "AIF2 Playback"},

	{"MIC1_PGA", NULL, "IN1L"},
	{"MIC1_PGA", NULL, "VMID"},
	{"MIC1", "MIC1 On", "MIC1_PGA"},

	{"ADCL Mixer", "MIC1L Switch", "MIC1"},
	{"ADCR Mixer", "MIC1R Switch", "MIC1"},

	{"ADCL Mixer", "MIC1L Bypass", "MIC1"},
	{"ADCR Mixer", "MIC1R Bypass", "MIC1"},

	{"MIC2_PGA", NULL, "IN2L"},
	{"MIC2_PGA", NULL, "VMID"},
	{"MIC2", "MIC2 On", "MIC2_PGA"},

	{"ADCL Mixer", "MIC2L Switch", "MIC2"},
	{"ADCR Mixer", "MIC2R Switch", "MIC2"},

	{"ADCL Mixer", "MIC2L Bypass", "MIC2"},
	{"ADCR Mixer", "MIC2R Bypass", "MIC2"},

	{"MIC3_PGA", NULL, "IN3L"},
	{"MIC3_PGA", NULL, "VMID"},
	{"MIC3", "MIC3 On", "MIC3_PGA"},

	{"ADCL Mixer", "MIC3L Switch", "MIC3"},
	{"ADCR Mixer", "MIC3R Switch", "MIC3"},

	{"ADCL Mixer", "MIC3L Bypass", "MIC3"},
	{"ADCR Mixer", "MIC3R Bypass", "MIC3"},

	{"LINEIN_PGA", NULL, "IN4L"},
	{"LINEIN_PGA", NULL, "VMID"},
	{"LINEIN", "LINEIN On", "LINEIN_PGA"},

	{"ADCL Mixer", "LINELL Switch", "LINEIN"},
	{"ADCL Mixer", "LINERL Switch", "LINEIN"},
	{"ADCR Mixer", "LINELR Switch", "LINEIN"},
	{"ADCR Mixer", "LINERR Switch", "LINEIN"},

	{"ADC", NULL, "ADCL Mixer"},
	{"ADC", NULL, "ADCR Mixer"},

	{"AIF Capture", NULL, "ADC"},
	{"AIF2 Capture", NULL, "ADC"},

	{"FM Link", "FM On", "ADC"},
	{"DAC", NULL, "FM Link"},
};

static int cod3026x_dai_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	int bclk = 0, lrclk = 0;

	dev_dbg(codec->dev, "%s called\n", __func__);

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_LEFT_J:
		fmt = LRJ_AUDIO_FORMAT_MASK;
		break;

	case SND_SOC_DAIFMT_I2S:
		fmt = I2S_AUDIO_FORMAT_MASK;
		break;

	default:
		pr_err("Unsupported DAI format %d\n",
				fmt & SND_SOC_DAIFMT_FORMAT_MASK);
		return -EINVAL;
	}

	snd_soc_update_bits(codec, COD3026X_41_FORMAT,
			I2S_AUDIO_FORMAT_MASK | LRJ_AUDIO_FORMAT_MASK, fmt);

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		bclk = BCLK_POL_MASK;
		lrclk = LRCLK_POL_MASK;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		bclk = BCLK_POL_MASK;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		lrclk = LRCLK_POL_MASK;
		break;
	default:
		pr_err("Unsupported Polartiy selection %d\n",
				fmt & SND_SOC_DAIFMT_INV_MASK);
		return -EINVAL;
	}

	snd_soc_update_bits(codec, COD3026X_41_FORMAT,
			BCLK_POL_MASK | LRCLK_POL_MASK, bclk | lrclk);
	return 0;
}

int cod3026x_set_externel_jd(struct snd_soc_codec *codec)
{
	int ret;
	struct cod3026x_priv *cod3026x;

	if (codec == NULL) {
		pr_err("Initilaise codec, before calling %s\n", __func__);
		return -1;
	}

	dev_dbg(codec->dev, "%s called\n", __func__);

	cod3026x = snd_soc_codec_get_drvdata(codec);

	cod3026x->use_external_jd = true;

#ifdef CONFIG_PM_RUNTIME
	pm_runtime_get_sync(codec->dev);
#else
	cod3026x_enable(codec->dev);
#endif
	/* Enable External jack detecter */
	ret = snd_soc_update_bits(codec, COD3026X_83_JACK_DET1,
			CTMP_JD_MODE_MASK, CTMP_JD_MODE_MASK);

	/* Disable Internel Jack detecter */
	ret |= snd_soc_update_bits(codec, COD3026X_81_DET_ON,
			EN_PDB_JD_CLK_MASK | EN_PDB_JD_MASK,
			EN_PDB_JD_CLK_MASK);

	/* Keep mic2 bias always high */
	snd_soc_update_bits(codec, COD3026X_86_DET_TIME,
			CTMD_BTN_DBNC_MASK | CTMF_BTN_ON_MASK | CTMF_DETB_PERIOD_MASK,
			((CTMD_BTN_DBNC_5 << CTMD_BTN_DBNC_SHIFT) |
			(CTMF_BTN_ON_14_CLK << CTMF_BTN_ON_SHIFT) |
			(CTMF_DETB_PERIOD_8 << CTMF_DETB_PERIOD_SHIFT)));

	snd_soc_update_bits(codec, COD3026X_06_IRQ1M,
			IRQ1M_MASK_ALL, 0xFF);
	snd_soc_update_bits(codec, COD3026X_07_IRQ2M,
			IRQ2M_MASK_ALL, 0xFF);

	/* Set Jack debounce time */
	snd_soc_write(codec, COD3026X_84_JACK_DET2, 0x37);
	snd_soc_write(codec, COD3026X_87_LDO_DIG, 0x03);
#ifdef CONFIG_PM_RUNTIME
	pm_runtime_put_sync(codec->dev);
#else
	cod3026x_disable(codec->dev);
#endif

	return ret;
}
EXPORT_SYMBOL_GPL(cod3026x_set_externel_jd);

static int cod3026x_dai_startup(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;

	dev_dbg(codec->dev, "(%s) %s completed\n",
			substream->stream ? "C" : "P", __func__);

	return 0;
}

static void cod3026x_dai_shutdown(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;

	dev_dbg(codec->dev, "(%s) %s completed\n",
			substream->stream ? "C" : "P", __func__);
}

static void cod3026x_sys_reset(struct snd_soc_codec *codec)
{
	dev_dbg(codec->dev, "%s called\n", __func__);

	snd_soc_update_bits(codec, COD3026X_40_DIGITAL_POWER,
			PDB_DACDIG_SHIFT | RSTB_DAT_DA_SHIFT | DIGITAL_POWER_RVD_SHIFT,
			0);

	snd_soc_update_bits(codec, COD3026X_40_DIGITAL_POWER,
			PDB_DACDIG_SHIFT | RSTB_DAT_DA_SHIFT | DIGITAL_POWER_RVD_SHIFT,
			PDB_DACDIG_MASK | RSTB_DAT_DA_MASK | DIGITAL_POWER_RVD_MASK);
}

static int cod3026x_dai_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct cod3026x_priv *cod3026x = snd_soc_codec_get_drvdata(codec);
	unsigned int cur_aifrate;
	int dnc;
	int ret;

	/* 192 KHz support */
	cur_aifrate = params_rate(params);

	dev_dbg(codec->dev, "%s called cur_aifrate: %d\n",
			__func__, cur_aifrate);

	if (cod3026x->aifrate != cur_aifrate) {
		/* DNC needs to be disabled while switching samplerate */
		dnc = snd_soc_read(codec, COD3026X_54_DNC1);
		snd_soc_write(codec, COD3026X_54_DNC1, 0);

		/* Need to reset H/W while switching from 192KHz to 48KHz */
		if (cur_aifrate == COD3026X_SAMPLE_RATE_192KHZ) {
			snd_soc_update_bits(codec, COD3026X_53_MQS,
					MQS_MODE_MASK, MQS_MODE_MASK);
			snd_soc_update_bits(codec, COD3026X_53_MQS,
					NORMQS_GN_MASK, MQS_20KHZ << NORMQS_GN_SHIFT);
		} else if (cod3026x->aifrate == COD3026X_SAMPLE_RATE_192KHZ) {
			snd_soc_update_bits(codec, COD3026X_53_MQS,
					MQS_MODE_MASK, 0);
			snd_soc_update_bits(codec, COD3026X_53_MQS,
					NORMQS_GN_MASK, NO_MQS_1 << NORMQS_GN_SHIFT);
			cod3026x_sys_reset(codec);
		}

		/* DNC mode can be restored after the samplerate switch */
		snd_soc_write(codec, COD3026X_54_DNC1, dnc);
		cod3026x->aifrate = cur_aifrate;
	}

	/*
	 * Codec supports only 24bits per sample, Mixer performs the required
	 * conversion to 24 bits. BFS is fixed at 64fs for mixer<->codec
	 * interface.
	 */
	ret = snd_soc_update_bits(codec, COD3026X_41_FORMAT,
			DATA_WORD_LENGTH_MASK,
			(DATA_WORD_LENGTH_24 << DATA_WORD_LENGTH_SHIFT));
	if (ret < 0) {
		dev_err(codec->dev, "%s failed to set bits per sample\n",
				__func__);
		return ret;
	}

	return 0;
}

static void cod3026x_adc_mute_work(struct work_struct *work)
{
	struct cod3026x_priv *cod3026x =
		container_of(work, struct cod3026x_priv, adc_mute_work);
	struct snd_soc_codec *codec = cod3026x->codec;

	mutex_lock(&cod3026x->adc_mute_lock);
	msleep(220);
	dev_dbg(codec->dev, "%s called\n", __func__);
	cod3026x_adc_digital_mute(codec, false);
	mutex_unlock(&cod3026x->adc_mute_lock);
}

static void jack_mic_delay_work(struct work_struct *work)
{
	struct cod3026x_priv *cod3026x =
		container_of(work, struct cod3026x_priv, jack_det_work);
	struct cod3026x_jack_det *jackdet = &cod3026x->jack_det;
	unsigned int stat1;

	dev_err(cod3026x->codec->dev, "[DEBUG] %s(%d)\n", __func__, __LINE__);
	mutex_lock(&cod3026x->jackdet_lock);

	if (jackdet->jack_det == true) {
		/* set delay for read correct adc value */
		msleep(cod3026x->mic_det_delay);

		/* read adc for mic detect */
		if (cod3026x->is_suspend)
			regcache_cache_only(cod3026x->regmap, false);

		stat1 = snd_soc_read(cod3026x->codec, COD3026X_0B_STATUS1);
		dev_err(cod3026x->dev, "%s reg 0x0B stat1 %02x\n",
				__func__, stat1);

		if (cod3026x->is_suspend)
			regcache_cache_only(cod3026x->regmap, true);

		if (stat1 & BIT(STATUS1_MIC_DET_SHIFT))
			jackdet->mic_det = true;
		else
			jackdet->mic_det = false;
	} else {
		/* jack/mic out */
		jackdet->mic_det = false;
	}

	if (jackdet->jack_det && jackdet->mic_det)
		switch_set_state(&cod3026x->sdev, 1);
	else if (jackdet->jack_det)
		switch_set_state(&cod3026x->sdev, 2);
	else
		switch_set_state(&cod3026x->sdev, 0);

	dev_err(cod3026x->codec->dev, "Jack %s, Mic %s\n",
			jackdet->jack_det ? "inserted" : "removed",
			jackdet->mic_det ? "inserted" : "removed");

	if (cod3026x->is_suspend)
		regcache_cache_only(cod3026x->regmap, false);

	if (jackdet->jack_det && jackdet->mic_det) {
		dev_err(cod3026x->codec->dev, "[DEBUG] %s(%d)\n",
				__func__, __LINE__);
		snd_soc_write(cod3026x->codec, COD3026X_85_MIC_DET, 0x02);
		snd_soc_write(cod3026x->codec, COD3026X_80_DET_PDB, 0x00);
	} else if (jackdet->jack_det) {
		dev_err(cod3026x->codec->dev, "[DEBUG] %s(%d)\n",
				__func__, __LINE__);
		snd_soc_write(cod3026x->codec, COD3026X_85_MIC_DET, 0x02);
		snd_soc_write(cod3026x->codec, COD3026X_80_DET_PDB, 0x01);
	} else {
		dev_err(cod3026x->codec->dev, "[DEBUG] %s(%d)\n",
				__func__, __LINE__);
		snd_soc_write(cod3026x->codec, COD3026X_85_MIC_DET, 0xFF);
		snd_soc_write(cod3026x->codec, COD3026X_80_DET_PDB, 0x00);
	}

	if (cod3026x->is_suspend)
		regcache_cache_only(cod3026x->regmap, true);

	mutex_unlock(&cod3026x->jackdet_lock);
	wake_unlock(&cod3026x->jack_wake_lock);
}

static void cod3026x_set_adc_gpio(struct cod3026x_priv *cod3026x, int val)
{
	if (gpio_is_valid(cod3026x->adc_pin)) {
		gpio_direction_output(cod3026x->adc_pin, val);
		dev_dbg(cod3026x->dev, "%s : adc gpio value: %d\n",
				__func__, gpio_get_value(cod3026x->adc_pin));
	}
}

#define JACK_IN_CHK_MORE_NO	3

static int cod3026x_jack_in_chk_more(struct cod3026x_priv *cod3026x)
{
	int i;
	int gdet_adc = 0;
	bool jack_in_det = 1;

	snd_soc_write(cod3026x->codec, COD3026X_84_JACK_DET2, 0x01);
	snd_soc_update_bits(cod3026x->codec, COD3026X_81_DET_ON,
			EN_PDB_JD_MASK, EN_PDB_JD_MASK);

	for (i = 0; i < JACK_IN_CHK_MORE_NO; i++) {
		if (cod3026x->use_det_gdet_adc_mode == 2)
			cod3026x_set_adc_gpio(cod3026x, 1);

		mdelay(50);

		if (cod3026x->use_det_gdet_adc_mode == 1)
			gdet_adc = cod3026x_gdet_adc_get_value(cod3026x);
		else if (cod3026x->use_det_gdet_adc_mode == 2)
			gdet_adc = cod3026x_adc_get_value(cod3026x);

		dev_dbg(cod3026x->dev, "%s called. gdet_adc:%d\n",
				__func__, gdet_adc);

		if (gdet_adc == 0 || gdet_adc > cod3026x->water_threshold_adc_min2)
			jack_in_det = 0;
	}

	dev_dbg(cod3026x->dev, "%s called. jack_in_det:%d\n",
			__func__, jack_in_det);

	return jack_in_det;
}

#define WATER_FINISH_CHK_MORE_NO 3

static int cod3026x_water_finish_chk_more(struct cod3026x_priv *cod3026x)
{
	int i;
	int gdet_adc = 0;
	bool water_finish_det = 1;

	snd_soc_write(cod3026x->codec, COD3026X_84_JACK_DET2, 0x01);
	snd_soc_update_bits(cod3026x->codec, COD3026X_81_DET_ON,
			EN_PDB_JD_MASK, EN_PDB_JD_MASK);

	for (i = 0; i < WATER_FINISH_CHK_MORE_NO; i++) {
		if (cod3026x->use_det_gdet_adc_mode == 2)
			cod3026x_set_adc_gpio(cod3026x, 1);

		mdelay(50);

		if (cod3026x->use_det_gdet_adc_mode == 1)
			gdet_adc = cod3026x_gdet_adc_get_value(cod3026x);
		else if (cod3026x->use_det_gdet_adc_mode == 2)
			gdet_adc = cod3026x_adc_get_value(cod3026x);

		dev_dbg(cod3026x->dev, "%s called. gdet_adc:%d\n",
				__func__, gdet_adc);

		if (gdet_adc < cod3026x->water_threshold_adc_max)
			water_finish_det = 0;
	}

	dev_dbg(cod3026x->dev, "%s called. water_finish_det:%d\n",
			__func__, water_finish_det);

	return water_finish_det;
}

#define WRONG_JACK_IN_CHK_NO 3

static void cod3026x_water_polling_work(struct work_struct *work)
{
	struct cod3026x_priv *cod3026x =
		container_of(work, struct cod3026x_priv, water_det_polling_work.work);
	struct snd_soc_codec *codec = cod3026x->codec;
	struct cod3026x_jack_det *jackdet = &cod3026x->jack_det;
	struct cod3026x_water_det *waterdet = &cod3026x->water_det;
	int gdet_adc = 0;

	dev_dbg(cod3026x->dev, "%s called.\n", __func__);

	if (cod3026x->is_suspend)
		regcache_cache_only(cod3026x->regmap, false);

	snd_soc_write(codec, COD3026X_84_JACK_DET2, 0x01);
	snd_soc_update_bits(codec, COD3026X_81_DET_ON,
			EN_PDB_JD_MASK, EN_PDB_JD_MASK);

	/* read adc for water detection */
	if (cod3026x->use_det_gdet_adc_mode == 1) {
		gdet_adc = cod3026x_gdet_adc_get_value(cod3026x);
	} else if (cod3026x->use_det_gdet_adc_mode == 2) {
		cod3026x_set_adc_gpio(cod3026x, 1);
		gdet_adc = cod3026x_adc_get_value(cod3026x);
	}

	dev_dbg(cod3026x->dev, "%s gdet adc %d\n", __func__, gdet_adc);
	waterdet->gdet_adc_val = gdet_adc;
	snd_soc_write(codec, COD3026X_84_JACK_DET2, 0x0D);

	/* do not jack det power off when jack inserted */
	if (waterdet->water_det == COD3026X_DET_WATER)
		snd_soc_update_bits(codec, COD3026X_81_DET_ON, EN_PDB_JD_MASK, 0);

	/* need to implement to turn off mic bias in manual mode */
	if (jackdet->jack_det && jackdet->mic_det)
		snd_soc_write(codec, COD3026X_80_DET_PDB, 0x7D);
	else
		snd_soc_write(codec, COD3026X_80_DET_PDB, 0x01);

	if (gdet_adc != 0 && gdet_adc >= cod3026x->water_threshold_adc_min2) {
		if (gdet_adc < cod3026x->water_threshold_adc_max) {
			dev_dbg(cod3026x->dev, "%s water is detected.\n", __func__);
			waterdet->wrong_jack_cnt++;
			waterdet->jack_det_bypass = true;
			waterdet->water_det = COD3026X_DET_WATER;

			if (waterdet->jack_det == true) {
				/* jack out after water detection */
				waterdet->jack_det = false;
				jackdet->jack_det = false;

				/* need to implement to turn on mic bias in manual mode */
				snd_soc_write(codec, COD3026X_80_DET_PDB, 0x0D);

				/* jack detection workqueue */
				cancel_work_sync(&cod3026x->jack_det_adc_work);
				queue_work(cod3026x->jack_det_adc_wq,
						&cod3026x->jack_det_adc_work);
			}
			/* polling for water detection */
			schedule_delayed_work(&cod3026x->water_det_polling_work,
					msecs_to_jiffies(COD3026X_WATER_DET_POLLING_TIME));
		} else {
			dev_dbg(cod3026x->dev, "%s polling is canceled.\n", __func__);
			if (cod3026x_water_finish_chk_more(cod3026x)) {
				waterdet->jack_det_bypass = false;
				waterdet->water_det = COD3026X_NO_DET_WATER;
				waterdet->wrong_jack_cnt = 0;

				/* set mic bias auto mode */
				snd_soc_write(codec, COD3026X_80_DET_PDB, 0x00);
				snd_soc_update_bits(codec, COD3026X_81_DET_ON,
						EN_PDB_JD_MASK, EN_PDB_JD_MASK);

				/* cancel the polling work */
				cancel_delayed_work(&cod3026x->water_det_polling_work);

				snd_soc_write(codec, COD3026X_84_JACK_DET2, 0x0d);
				snd_soc_update_bits(codec, COD3026X_07_IRQ2M,
						IRQ2M_MASK_ALL, 0xC0);
				snd_soc_update_bits(codec, COD3026X_08_IRQ3M,
						IRQ3M_MASK_ALL, 0xC0);

				if (cod3026x->is_suspend)
					regcache_cache_only(cod3026x->regmap, true);
			} else {
				/* polling for water detection */
				schedule_delayed_work(&cod3026x->water_det_polling_work,
						msecs_to_jiffies(COD3026X_WATER_DET_POLLING_TIME));
			}
		}
	} else {
		/* water and jack in detection */
		dev_dbg(cod3026x->dev, "%s water is detected and jack in.\n", __func__);
		if (gdet_adc != 0 && cod3026x_jack_in_chk_more(cod3026x)) {
			waterdet->jack_det_bypass = true;
			waterdet->water_det = COD3026X_DET_WATER_JACK_IN;
			if (waterdet->jack_det == false) {
				/* jack in after water detection */
				waterdet->jack_det = true;
				jackdet->jack_det = true;

				/* need to implement to turn on mic bias in manual mode */
				snd_soc_write(codec, COD3026X_80_DET_PDB, 0x0D);

				/* jack detection workqueue */
				cancel_work_sync(&cod3026x->jack_det_adc_work);
				queue_work(cod3026x->jack_det_adc_wq,
						&cod3026x->jack_det_adc_work);
			}
		}

		dev_dbg(cod3026x->dev, "%s waterdet->wrong_jack_cnt: %d\n",
				__func__, waterdet->wrong_jack_cnt);
		if (waterdet->wrong_jack_cnt <= WRONG_JACK_IN_CHK_NO) {
			waterdet->jack_det_bypass = false;
			waterdet->water_det = COD3026X_NO_DET_WATER;
			waterdet->wrong_jack_cnt = 0;

			/* set mic bias auto mode */
			snd_soc_write(codec, COD3026X_80_DET_PDB, 0x00);
			snd_soc_update_bits(codec, COD3026X_81_DET_ON,
					EN_PDB_JD_MASK, EN_PDB_JD_MASK);

			/* cancel the polling work */
			cancel_delayed_work(&cod3026x->water_det_polling_work);

			snd_soc_write(codec, COD3026X_84_JACK_DET2, 0x0d);
			snd_soc_update_bits(codec, COD3026X_07_IRQ2M,
					IRQ2M_MASK_ALL, 0xC0);
			snd_soc_update_bits(codec, COD3026X_08_IRQ3M,
					IRQ3M_MASK_ALL, 0xC0);

			if (cod3026x->is_suspend)
				regcache_cache_only(cod3026x->regmap, true);
		} else {
			/* polling for water detection */
			schedule_delayed_work(&cod3026x->water_det_polling_work,
					msecs_to_jiffies(COD3026X_WATER_DET_POLLING_TIME));
		}
	}
}

static void cod3026x_water_det_work(struct work_struct *work)
{
	struct cod3026x_priv *cod3026x =
		container_of(work, struct cod3026x_priv, water_det_adc_work.work);
	struct snd_soc_codec *codec = cod3026x->codec;
	struct cod3026x_jack_det *jackdet = &cod3026x->jack_det;
	struct cod3026x_water_det *waterdet = &cod3026x->water_det;
	int gdet_adc = 0;

	waterdet->wrong_jack_cnt = 0;
	dev_dbg(cod3026x->dev, "%s called.\n", __func__);
	dev_dbg(cod3026x->dev, "%s : jack det %d\n", __func__, jackdet->jack_det);

	if (cod3026x->is_suspend)
		regcache_cache_only(cod3026x->regmap, false);

	snd_soc_write(codec, COD3026X_84_JACK_DET2, 0x01);
	/* read adc for water detection */
	if (cod3026x->use_det_gdet_adc_mode == 1) {
		gdet_adc = cod3026x_gdet_adc_get_value(cod3026x);
	} else if (cod3026x->use_det_gdet_adc_mode == 2) {
		cod3026x_set_adc_gpio(cod3026x, 1);
		gdet_adc = cod3026x_adc_get_value(cod3026x);
	}

	dev_dbg(cod3026x->dev, "%s gdet adc: %d\n", __func__, gdet_adc);
	waterdet->gdet_adc_val = gdet_adc;
	snd_soc_write(codec, COD3026X_84_JACK_DET2, 0x0d);

	if (cod3026x->is_suspend)
		regcache_cache_only(cod3026x->regmap, true);

	if (gdet_adc != 0 && gdet_adc >= cod3026x->water_threshold_adc_min1) {
		if (gdet_adc < cod3026x->water_threshold_adc_max) {
			dev_dbg(cod3026x->dev, "%s water is detected.\n", __func__);
			if (cod3026x->is_suspend)
				regcache_cache_only(cod3026x->regmap, false);
			waterdet->water_det = COD3026X_DET_WATER;
			waterdet->jack_det = false;
			waterdet->wrong_jack_cnt++;

			/* set mic bias manual mode */
			snd_soc_write(codec, COD3026X_80_DET_PDB, 0x01);
			snd_soc_update_bits(codec, COD3026X_81_DET_ON,
					EN_PDB_JD_MASK, 0);

			snd_soc_update_bits(codec, COD3026X_07_IRQ2M,
					IRQ2M_MASK_ALL, 0xc3);
			snd_soc_update_bits(codec, COD3026X_08_IRQ3M,
					IRQ3M_MASK_ALL, 0xc3);
			if (cod3026x->is_suspend)
				regcache_cache_only(cod3026x->regmap, true);
			/* cancel the polling work */
			cancel_delayed_work(&cod3026x->water_det_polling_work);
			/* polling for water detection */
			schedule_delayed_work(&cod3026x->water_det_polling_work,
					msecs_to_jiffies(COD3026X_WATER_DET_POLLING_TIME));
		}
	}
}

#if !defined(CONFIG_SEC_FACTORY) && defined(CONFIG_SND_SOC_COD30XX_EXT_ANT)
#define WRONG_JACK_CHK_NO 2

static int cod3026x_wrong_jack_chk(struct cod3026x_priv *cod3026x)
{
	int i;
	int gdet_adc = 0;
	int wrong_jack = 1;

	for (i = 0; i < WRONG_JACK_CHK_NO; i++) {
		if (cod3026x->use_det_gdet_adc_mode == 2)
			cod3026x_set_adc_gpio(cod3026x, 1);
		msleep(50);
		if (cod3026x->use_det_gdet_adc_mode == 1)
			gdet_adc = cod3026x_gdet_adc_get_value(cod3026x);
		else if (cod3026x->use_det_gdet_adc_mode == 2)
			gdet_adc = cod3026x_adc_get_value(cod3026x);

		dev_dbg(cod3026x->dev, "%s called. gdet_adc: %d\n",
				__func__, gdet_adc);

		if (gdet_adc < COD3026X_JACK_ERR_THRESHOLD_MIN
				|| gdet_adc > COD3026X_JACK_ERR_THRESHOLD_MAX)
			wrong_jack = 0;
	}

	dev_dbg(cod3026x->dev, "%s called. wrong_jack: %d\n",
			__func__, wrong_jack);

	return wrong_jack;
}
#endif

static void cod3026x_jack_det_work(struct work_struct *work)
{
	struct cod3026x_priv *cod3026x =
		container_of(work, struct cod3026x_priv, jack_det_adc_work);
	struct snd_soc_codec *codec = cod3026x->codec;
	struct cod3026x_jack_det *jackdet = &cod3026x->jack_det;
	struct cod3026x_water_det *waterdet = &cod3026x->water_det;
	int adc = 0, gdet_adc = 0;

#if !defined(CONFIG_SND_SOC_COD30XX_EXT_ANT) || defined(CONFIG_SEC_FACTORY)
	/* original code */
	dev_dbg(cod3026x->dev, "%s called.\n", __func__);
	dev_dbg(cod3026x->dev, "%s jackdet->jack_det: %d\n",
			__func__, jackdet->jack_det);
	dev_dbg(cod3026x->dev, "%s waterdet->jack_det_bypass: %d\n",
			__func__, waterdet->jack_det_bypass);
	dev_dbg(cod3026x->dev, "%s waterdet->water_det: %d\n",
			__func__, waterdet->water_det);

	mutex_lock(&cod3026x->jackdet_lock);

	if (cod3026x->use_det_gdet_adc_mode) {
		if (waterdet->jack_det_bypass == false) {
			if (waterdet->water_det == 1) {
				mutex_unlock(&cod3026x->jackdet_lock);
				wake_unlock(&cod3026x->jack_wake_lock);
				return;
			}
		}
	}

	if (jackdet->jack_det == true) {
		/* set delay for read correct adc value */
		msleep(cod3026x->mic_det_delay);

		/* read gdet adc for wrong jack interrupt check */
		if (cod3026x->is_suspend)
			regcache_cache_only(cod3026x->regmap, false);
		snd_soc_write(codec, COD3026X_84_JACK_DET2, 0x01);

		if (cod3026x->use_det_gdet_adc_mode == 1) {
			gdet_adc = cod3026x_gdet_adc_get_value(cod3026x);
		} else if (cod3026x->use_det_gdet_adc_mode == 2) {
			cod3026x_set_adc_gpio(cod3026x, 1);
			gdet_adc = cod3026x_adc_get_value(cod3026x);
		}

		snd_soc_write(codec, COD3026X_84_JACK_DET2, 0x0d);
		if (cod3026x->is_suspend)
			regcache_cache_only(cod3026x->regmap, true);

		dev_err(cod3026x->dev, "%s gdet adc: %d\n", __func__, gdet_adc);

		if (gdet_adc >= cod3026x->water_threshold_adc_min1 &&
				gdet_adc < cod3026x->water_threshold_adc_max) {
			dev_dbg(cod3026x->dev, "%s water is detected.\n", __func__);
			jackdet->jack_det = false;
			mutex_unlock(&cod3026x->jackdet_lock);
			wake_unlock(&cod3026x->jack_wake_lock);
			return;
		}

		/* read adc for mic detect */
		if (cod3026x->use_det_gdet_adc_mode == 2)
			cod3026x_set_adc_gpio(cod3026x, 0);
		adc = cod3026x_adc_get_value(cod3026x);

		dev_err(cod3026x->dev, "%s mic det adc: %d\n", __func__, adc);

		if (adc > cod3026x->mic_adc_range)
			jackdet->mic_det = true;
		else
			jackdet->mic_det = false;

		jackdet->adc_val = adc;
	} else {
		/* jack/mic out */
		jackdet->mic_det = false;
		jackdet->adc_val = -EINVAL;
	}

	if (jackdet->jack_det && jackdet->mic_det)
		switch_set_state(&cod3026x->sdev, 1);
	else if (jackdet->jack_det)
		switch_set_state(&cod3026x->sdev, 2);
	else
		switch_set_state(&cod3026x->sdev, 0);

	if (cod3026x->is_suspend)
		regcache_cache_only(cod3026x->regmap, false);

	if (jackdet->jack_det && jackdet->mic_det) {
		dev_err(cod3026x->codec->dev, "[DEBUG] %s(%d)\n",
				__func__, __LINE__);
		snd_soc_write(cod3026x->codec, COD3026X_85_MIC_DET, 0x02);
		snd_soc_write(cod3026x->codec, COD3026X_80_DET_PDB, 0x00);
	} else if (jackdet->jack_det) {
		dev_err(cod3026x->codec->dev, "[DEBUG] %s(%d)\n",
				__func__, __LINE__);
		snd_soc_write(cod3026x->codec, COD3026X_85_MIC_DET, 0x02);
		snd_soc_write(cod3026x->codec, COD3026X_80_DET_PDB, 0x01);
	} else {
		dev_err(cod3026x->codec->dev, "[DEBUG] %s(%d)\n",
				__func__, __LINE__);
		snd_soc_write(cod3026x->codec, COD3026X_85_MIC_DET, 0xff);
		snd_soc_write(cod3026x->codec, COD3026X_80_DET_PDB, 0x00);
		snd_soc_update_bits(cod3026x->codec, COD3026X_12_PD_AD2,
				PDB_MIC_BST3_MASK, 0);
	}

	if (cod3026x->is_suspend)
		regcache_cache_only(cod3026x->regmap, true);

	dev_dbg(cod3026x->codec->dev, "Jack %s, Mic %s\n",
				jackdet->jack_det ? "inserted" : "removed",
				jackdet->mic_det ? "inserted" : "removed");
#else
	/* if defined CONFIG_SND_SOC_COD30XX_EXT_ANT */
	unsigned int  stat1, pend2, pend3;
	bool jack_state;

	dev_dbg(cod3026x->dev, "%s called.\n", __func__);
	dev_dbg(cod3026x->dev, "%s jackdet->jack_det: %d\n",
			__func__, jackdet->jack_det);
	dev_dbg(cod3026x->dev, "%s waterdet->jack_det_bypass: %d\n",
			__func__, waterdet->jack_det_bypass);
	dev_dbg(cod3026x->dev, "%s waterdet->water_det: %d\n",
			__func__, waterdet->water_det);

	mutex_lock(&cod3026x->jackdet_lock);

	if (cod3026x->use_det_gdet_adc_mode) {
		if (waterdet->jack_det_bypass == false) {
			if (waterdet->water_det == 1) {
				mutex_unlock(&cod3026x->jackdet_lock);
				wake_unlock(&cod3026x->jack_wake_lock);
				return;
			}
		}
	}

	pend2 = snd_soc_read(cod3026x->codec, COD3026X_02_IRQ2PEND);
	pend3 = snd_soc_read(cod3026x->codec, COD3026X_03_IRQ3PEND);
	stat1 = snd_soc_read(cod3026x->codec, COD3026X_0B_STATUS1);

	pr_err("[DEBUG] %s: line %d  02:%02x, 03:%02x , 0x0B:%02x\n",
			__func__, __LINE__, pend2, pend3, stat1);

	if ((pend2 != 0xffffffff) && (pend3 != 0xffffffff)) {
		if (!waterdet->water_det) {
			jack_state = stat1 & BIT(STATUS1_JACK_DET_SHIFT);
			jackdet->jack_det = jack_state ? true : false;
		}
		pr_info("%s use state jack_det[%d], mic_det[%d], ant_det[%d]\n",
				__func__, jackdet->jack_det, jackdet->mic_det, jackdet->ant_det);
	} else {
		pr_info("%s not use state jack_det[%d], mic_det[%d], ant_det[%d]\n",
				__func__, jackdet->jack_det, jackdet->mic_det, jackdet->ant_det);
	}

	/* case 1~8 check */
	if (jackdet->jack_det == true) {
		/* set delay for read correct adc value */
		msleep(cod3026x->mic_det_delay);

		/* read gdet adc for wrong jack interrupt check */
		if (cod3026x->is_suspend)
			regcache_cache_only(cod3026x->regmap, false);

		snd_soc_write(codec, COD3026X_84_JACK_DET2, 0x01);

		if (cod3026x->use_det_gdet_adc_mode == 1) {
			gdet_adc = cod3026x_gdet_adc_get_value(cod3026x);
		} else if (cod3026x->use_det_gdet_adc_mode == 2) {
			cod3026x_set_adc_gpio(cod3026x, 1);
			gdet_adc = cod3026x_adc_get_value(cod3026x);
		}

		snd_soc_write(codec, COD3026X_84_JACK_DET2, 0x0d);
		if (cod3026x->is_suspend)
			regcache_cache_only(cod3026x->regmap, true);

		dev_err(cod3026x->dev, "%s gdet adc: %d\n", __func__, gdet_adc);

		if (gdet_adc >= cod3026x->water_threshold_adc_min1 &&
				gdet_adc < cod3026x->water_threshold_adc_max) {
			dev_dbg(cod3026x->dev, "%s water is detected.\n", __func__);
			jackdet->jack_det = false;
			mutex_unlock(&cod3026x->jackdet_lock);
			wake_unlock(&cod3026x->jack_wake_lock);
			return;
		}

		/* read adc for mic detect */
		if (cod3026x->use_det_gdet_adc_mode == 2)
			cod3026x_set_adc_gpio(cod3026x, 0);
		adc = cod3026x_adc_get_value(cod3026x);

		dev_err(cod3026x->dev, "%s mic det adc: %d\n", __func__, adc);
		if (adc > cod3026x->mic_adc_range)
			jackdet->mic_det = true;
		else
			jackdet->mic_det = false;
	} else {
		/* jack/mic out */
		jackdet->mic_det = false;
	}

	/* case 9~10 check */
	if (adc > cod3026x->ant_adc_range) {
		jackdet->ant_det = true;
		jackdet->mic_det = false;
		pr_info("6. %s jack_det[%d], mic_det[%d], ant_det[%d]\n",
				__func__, jackdet->jack_det, jackdet->mic_det, jackdet->ant_det);
	} else {
		jackdet->ant_det = false;
	}

	/* case 11~14 check */
	if (jackdet->ant_irq == true) {
		if (gpio_get_value(cod3026x->ant_det_gpio) == true) {
			jackdet->ant_det = true;
			jackdet->mic_det = false;
			pr_info("8. %s jack_det[%d], mic_det[%d], ant_det[%d]\n",
					__func__, jackdet->jack_det, jackdet->mic_det, jackdet->ant_det);
		} else {
			jackdet->ant_det = false;
			pr_info("9. %s jack_det[%d], mic_det[%d], ant_det[%d]\n",
					__func__, jackdet->jack_det, jackdet->mic_det, jackdet->ant_det);
		}
		jackdet->ant_irq = false;
	} else {
		pr_info("10. %s jack_det[%d], mic_det[%d], ant_det[%d]\n",
				__func__, jackdet->jack_det, jackdet->mic_det, jackdet->ant_det);
	}

	if (jackdet->ant_det)
		/* external antenna */
		switch_set_state(&cod3026x->sdev, 256);
	else if (jackdet->jack_det && jackdet->mic_det)
		switch_set_state(&cod3026x->sdev, 1);
	else if (jackdet->jack_det)
		switch_set_state(&cod3026x->sdev, 2);
	else
		switch_set_state(&cod3026x->sdev, 0);

	if (cod3026x->is_suspend)
		regcache_cache_only(cod3026x->regmap, false);

	if (jackdet->ant_det) {
		/* micbias enable when external antena inserted */
		dev_err(cod3026x->codec->dev, "[DEBUG] %s(%d)\n",
				__func__, __LINE__);
		snd_soc_write(cod3026x->codec, COD3026X_80_DET_PDB, 0x00);
	} else if (jackdet->jack_det && jackdet->mic_det) {
		dev_err(cod3026x->codec->dev, "[DEBUG] %s(%d)\n",
				__func__, __LINE__);
		snd_soc_write(cod3026x->codec, COD3026X_85_MIC_DET, 0x02);
		snd_soc_write(cod3026x->codec, COD3026X_80_DET_PDB, 0x00);
	} else if (jackdet->jack_det) {
		dev_err(cod3026x->codec->dev, "[DEBUG] %s(%d)\n",
				__func__, __LINE__);
		snd_soc_write(cod3026x->codec, COD3026X_85_MIC_DET, 0x02);
		if (cod3026x_wrong_jack_chk(cod3026x))
			snd_soc_write(cod3026x->codec, COD3026X_80_DET_PDB, 0x01);
		else
			snd_soc_write(cod3026x->codec, COD3026X_80_DET_PDB, 0x00);
	} else {
		dev_err(cod3026x->codec->dev, "[DEBUG] %s(%d)\n",
				__func__, __LINE__);
		snd_soc_write(cod3026x->codec, COD3026X_85_MIC_DET, 0xff);
		snd_soc_write(cod3026x->codec, COD3026X_80_DET_PDB, 0x00);
		snd_soc_update_bits(cod3026x->codec, COD3026X_12_PD_AD2,
				PDB_MIC_BST3_MASK, 0);
	}

	if (cod3026x->is_suspend)
		regcache_cache_only(cod3026x->regmap, true);

	dev_dbg(cod3026x->codec->dev, "Jack %s, Mic %s Ant %s\n",
				jackdet->jack_det ? "inserted" : "removed",
				jackdet->mic_det ? "inserted" : "removed",
				jackdet->ant_det ? "inserted" : "removed");
#endif
	mutex_unlock(&cod3026x->jackdet_lock);
	wake_unlock(&cod3026x->jack_wake_lock);
}

#define ADC_TRACE_NUM		5
#define ADC_TRACE_NUM2		2
#define ADC_READ_DELAY_US	500
#define ADC_READ_DELAY_MS	1
#define ADC_DEVI_THRESHOLD	18000

#define BUTTON_PRESS 1
#define BUTTON_RELEASE 0

static int get_adc_avg(int *adc_values)
{
	int i;
	int adc_sum = 0;

	for (i = 2; i < ADC_TRACE_NUM; i++)
		adc_sum += adc_values[i];

	adc_sum = adc_sum / (ADC_TRACE_NUM-2);

	return adc_sum;
}

static int get_adc_devi(int avg , int *adc_values)
{
	int i;
	int devi = 0, diff;

	for (i = 2; i < ADC_TRACE_NUM; i++) {
		diff = adc_values[i] - avg;
		devi += (diff * diff);
	}

	return devi;
}

static void cod3026x_buttons_work(struct work_struct *work)
{
	struct cod3026x_priv *cod3026x =
		container_of(work, struct cod3026x_priv, buttons_work.work);
	struct cod3026x_jack_det *jd = &cod3026x->jack_det;
	struct jack_buttons_zone *btn_zones = cod3026x->jack_buttons_zones;
	int num_buttons_zones = ARRAY_SIZE(cod3026x->jack_buttons_zones);
	int adc_values[ADC_TRACE_NUM];
	int current_button_state;
	int adc;
	int i, avg, devi;
	int adc_final_values[ADC_TRACE_NUM2];
	int j;
	int adc_final = 0;
	int adc_max = 0;

	dev_dbg(cod3026x->dev, "%s called.\n", __func__);

	if (!jd->jack_det) {
		dev_err(cod3026x->dev, "Skip button events for jack_out\n");
		if (jd->privious_button_state == BUTTON_PRESS) {
			jd->button_det = false;
			input_report_key(cod3026x->input, jd->button_code, 0);
			input_sync(cod3026x->input);
			cod3026x_process_button_ev(cod3026x->codec, jd->button_code, 0);
			dev_dbg(cod3026x->dev, "key %d released when jack_out\n",
					jd->button_code);
		}
		wake_unlock(&cod3026x->jack_wake_lock);
		return;
	}

	if (!jd->mic_det) {
		dev_err(cod3026x->dev, "Skip button events for 3-pole jack\n");
		wake_unlock(&cod3026x->jack_wake_lock);
		return;
	}

	for (j = 0; j < ADC_TRACE_NUM2; j++) {
		/* read GPADC for button */
		for (i = 0; i < ADC_TRACE_NUM; i++) {
			if (cod3026x->use_det_gdet_adc_mode == 2)
				cod3026x_set_adc_gpio(cod3026x, 0);
			adc = cod3026x_adc_get_value(cod3026x);
			adc_values[i] = adc;
			udelay(ADC_READ_DELAY_US);
		}

		/*
		 * check avg/devi value is proper
		 * if not read adc after 5 ms
		 */
		avg = get_adc_avg(adc_values);
		devi = get_adc_devi(avg, adc_values);
		dev_err(cod3026x->dev, "button adc avg: %d, devi: %d\n", avg, devi);

		if (devi > ADC_DEVI_THRESHOLD) {
			queue_delayed_work(cod3026x->buttons_wq,
					&cod3026x->buttons_work, msecs_to_jiffies(5));
			for (i = 0; i < ADC_TRACE_NUM; ) {
				dev_err(cod3026x->dev, "retry button_work: %d %d %d %d %d\n",
						adc_values[i+0], adc_values[i+1], adc_values[i+2],
						adc_values[i+3], adc_values[i+4]);
				i += 5;
			}
			wake_unlock(&cod3026x->jack_wake_lock);
			return;
		}
		adc_final_values[j] = avg;

		if (avg > adc_max)
			adc_max = avg;

		mdelay(ADC_READ_DELAY_MS);
	}
	adc_final = adc_max;

	/* check button press/release */
	if (adc_final > cod3026x->btn_release_value)
		current_button_state = BUTTON_RELEASE;
	else
		current_button_state = BUTTON_PRESS;

	if (jd->privious_button_state == current_button_state) {
		dev_err(cod3026x->dev, "Button state did not changed\n");
		wake_unlock(&cod3026x->jack_wake_lock);
		return;
	}

	jd->privious_button_state = current_button_state;

	adc = adc_final;
	jd->adc_val = adc_final;

	/* determine which button press or release */
	if (current_button_state == BUTTON_PRESS) {
		for (i = 0; i < num_buttons_zones; i++)
			if (adc >= btn_zones[i].adc_low &&
				adc <= btn_zones[i].adc_high) {
				jd->button_code = btn_zones[i].code;
				input_report_key(cod3026x->input, jd->button_code, 1);
				input_sync(cod3026x->input);
				jd->button_det = true;
				cod3026x_process_button_ev(cod3026x->codec, jd->button_code, 1);
				dev_err(cod3026x->dev, "key %d is pressed, adc %d\n",
						 btn_zones[i].code, adc);
				wake_unlock(&cod3026x->jack_wake_lock);
				return;
			}
		dev_err(cod3026x->dev, "key skipped. ADC %d\n", adc);
	} else {
		snd_soc_update_bits(cod3026x->codec, COD3026X_12_PD_AD2,
				PDB_MIC_BST3_MASK, 0);

		msleep(40);

		snd_soc_update_bits(cod3026x->codec, COD3026X_12_PD_AD2,
				PDB_MIC_BST3_MASK, PDB_MIC_BST3_MASK);

		jd->button_det = false;
		input_report_key(cod3026x->input, jd->button_code, 0);
		input_sync(cod3026x->input);

		cod3026x_process_button_ev(cod3026x->codec, jd->button_code, 0);
		dev_err(cod3026x->dev, "key %d released\n", jd->button_code);
	}
	wake_unlock(&cod3026x->jack_wake_lock);
}

static irqreturn_t cod3026x_threaded_isr(int irq, void *data)
{
	struct cod3026x_priv *cod3026x = data;
	struct snd_soc_codec *codec = cod3026x->codec;
	struct cod3026x_jack_det *jd = &cod3026x->jack_det;
	struct cod3026x_water_det *waterdet = &cod3026x->water_det;
	unsigned int  stat1, pend1, pend2, pend3;
	int jackdet = COD3026X_MJ_DET_INVALID;
	bool det_status_change = false;
	unsigned int i, key_r, key_f;

	mutex_lock(&cod3026x->key_lock);

	if (cod3026x->is_suspend)
		regcache_cache_only(cod3026x->regmap, false);

	pend1 = snd_soc_read(codec, COD3026X_01_IRQ1PEND);
	pend2 = snd_soc_read(codec, COD3026X_02_IRQ2PEND);
	pend3 = snd_soc_read(codec, COD3026X_03_IRQ3PEND);
	stat1 = snd_soc_read(codec, COD3026X_0B_STATUS1);

	pr_err("[DEBUG] %s, line:%d, 01:%02x, 02:%02x, 03:%02x, 0x0B:%02x\n",
			__func__, __LINE__, pend1, pend2, pend3, stat1);

	/*
	 * Sequence for Jack/Mic detection
	 *
	 * (JACK bit 0, MIC bit 1)
	 *
	 * 1. Check bits in IRQ2PEND and IRQ3PEND.
	 * 2. If either of them is 1, then the STATUS1 register tells current
	 * status of Jack/Mic. Connected if bit value is 1, removed otherwise.
	 */
	if ((pend2 & IRQ2_JACK_DET_R) || (pend3 & IRQ3_JACK_DET_F)) {
		if (waterdet->water_det) {
			mutex_unlock(&cod3026x->key_lock);
			goto out;
		}
		det_status_change = true;
		jackdet = stat1 & BIT(STATUS1_JACK_DET_SHIFT);
		jd->jack_det = jackdet ? true : false;
		if (jd->jack_det == true)
			dev_dbg(cod3026x->dev, "%s : jack in interrupt\n", __func__);
		else
			dev_dbg(cod3026x->dev, "%s : jack out interrupt\n", __func__);
	}

	if (det_status_change) {
		if (cod3026x->use_det_adc_mode) {
			if (cod3026x->use_det_gdet_adc_mode) {
				if (jd->jack_det == true) {
					queue_delayed_work(cod3026x->water_det_adc_wq,
							&cod3026x->water_det_adc_work, msecs_to_jiffies(400));
				}
			}
			wake_lock(&cod3026x->jack_wake_lock);
			/* jack detection workqueue */
			cancel_work_sync(&cod3026x->jack_det_adc_work);
			queue_work(cod3026x->jack_det_adc_wq, &cod3026x->jack_det_adc_work);
		} else {
			wake_lock(&cod3026x->jack_wake_lock);
			/* jack detection workqueue */
			cancel_work_sync(&cod3026x->jack_det_work);
			queue_work(cod3026x->jack_det_wq, &cod3026x->jack_det_work);
		}
		mutex_unlock(&cod3026x->key_lock);
		goto out;
	}

	if (cod3026x->use_det_adc_mode) {
		wake_lock(&cod3026x->jack_wake_lock);
		/* start button work */
		queue_delayed_work(cod3026x->buttons_wq,
					&cod3026x->buttons_work,
					msecs_to_jiffies(cod3026x->btn_press_delay));
	} else {
		for (i = COD3026X_START_IRQ_CHK_BIT;
				i <= COD3026X_MAX_IRQ_CHK_BITS; i++) {
			key_r = pend2 & BIT(i);
			key_f = pend3 & BIT(i);
			switch (key_r) {
			case IRQ2_VOICE_DET_R:
				input_event(cod3026x->input, EV_KEY, KEY_MEDIA, 1);
				jd->button_det = true;
				dev_err(codec->dev, "voice pushed\n");
				break;
			case IRQ2_VOL_DN_DET_R:
				input_event(cod3026x->input, EV_KEY, KEY_VOLUMEDOWN, 1);
				jd->button_det = true;
				dev_err(codec->dev, "vol- pushed\n");
				break;
			case IRQ2_VOL_UP_DET_R:
				input_event(cod3026x->input, EV_KEY, KEY_VOLUMEUP, 1);
				jd->button_det = true;
				dev_err(codec->dev, "vol+ pushed\n");
				break;
			case IRQ2_HOOK_DET_R:
				input_event(cod3026x->input, EV_KEY, KEY_MEDIA, 1);
				jd->button_det = true;
				dev_err(codec->dev, "hook pushed\n");
				break;
			default:
				break;
			}

			switch (key_f) {
			case IRQ3_VOICE_DET_F:
				input_event(cod3026x->input, EV_KEY, KEY_MEDIA, 0);
				jd->button_det = false;
				dev_err(codec->dev, "voice released\n");
				break;
			case IRQ3_VOL_DN_DET_F:
				input_event(cod3026x->input, EV_KEY, KEY_VOLUMEDOWN, 0);
				jd->button_det = false;
				dev_err(codec->dev, "vol- released\n");
				break;
			case IRQ3_VOL_UP_DET_F:
				input_event(cod3026x->input, EV_KEY, KEY_VOLUMEUP, 0);
				jd->button_det = false;
				dev_err(codec->dev, "vol+ released\n");
				break;
			case IRQ3_HOOK_DET_F:
				input_event(cod3026x->input, EV_KEY, KEY_MEDIA, 0);
				jd->button_det = false;
				dev_err(codec->dev, "hook released\n");
				break;
			default:
				break;
			}
			input_sync(cod3026x->input);
		}
	}

	mutex_unlock(&cod3026x->key_lock);

out:
	if (cod3026x->is_suspend)
		regcache_cache_only(cod3026x->regmap, true);

	return IRQ_HANDLED;
}

#if !defined(CONFIG_SEC_FACTORY) && defined(CONFIG_SND_SOC_COD30XX_EXT_ANT)
static irqreturn_t cod3026x_ant_thread_isr(int irq, void *data)
{
	struct cod3026x_priv *cod3026x = data;
	struct cod3026x_jack_det *jd = &cod3026x->jack_det;
	bool det_status_change = false;
	int curr_data;
	int pre_data;
	int loopcnt;
	int check_loop_cnt = 16; /* 500ms h/w req */

	mutex_lock(&cod3026x->key_lock);

	if (cod3026x->is_suspend)
		regcache_cache_only(cod3026x->regmap, false);

	pr_info("%s jack_det[%d], mic_det[%d], ant_det[%d]\n",
			__func__, jd->jack_det, jd->mic_det, jd->ant_det);

	/*
	 * Sequence for EAR ANT DETECT IRQ
	 * case 1[3 pole jack insert]:
	 * case 2[3 pole jack remove]:
	 * case 3[4 pole jack insert]:
	 * case 4[4 pole jack remove]:
	 * case 5[EXT ANT + 3 pole jack insert]:
	 * case 6[EXT ANT + 3 pole jack remove]:
	 * case 7[EXT ANT + 4 pole jack insert]:
	 * case 8[EXT ANT + 4 pole jack remove]:
		=> EAR_ANT_DET pin don't case, ADC(mic_adc_range) check in cod3026_jack_det_work()

	 * case 9[EXT ANT insert]:
	 * case 10[EXT ANT remove]:
		=> EAR_ANT_DET pin don't case, ADC(ant_adc_range) check in cod3026_jack_det_work()

	 * case 11[EXT ANT inserted + 3 pole jack insert]: High -> Low
	 * case 12[EXT ANT inserted + 3 pole jack remove]: Low -> High
	 * case 13[EXT ANT inserted + 4 pole jack insert]: High -> Low
	 * case 14[EXT ANT inserted + 4 pole jack remove]: Low -> High
		=> use cod3026x_ant_thread_isr()
	 */
	pre_data = 0;
	loopcnt = 0;
	while (true) {
		curr_data = gpio_get_value(cod3026x->ant_det_gpio);
		if (pre_data == curr_data)
			loopcnt++;
		else
			loopcnt = 0;
		pre_data = curr_data;

		if (loopcnt >= check_loop_cnt)
			break;

		msleep(20);
	}
	det_status_change = true;
	jd->ant_irq = true;

	if (det_status_change) {
		if (cod3026x->use_det_adc_mode) {
			wake_lock(&cod3026x->jack_wake_lock);
			/* jack detection workqueue */
			cancel_work_sync(&cod3026x->jack_det_adc_work);
			queue_work(cod3026x->jack_det_wq, &cod3026x->jack_det_adc_work);
		} else {
			wake_lock(&cod3026x->jack_wake_lock);
			/* jack detection workqueue */
			cancel_work_sync(&cod3026x->jack_det_work);
			queue_work(cod3026x->jack_det_wq, &cod3026x->jack_det_work);
		}
		mutex_unlock(&cod3026x->key_lock);
		goto out;
	}
	mutex_unlock(&cod3026x->key_lock);

out:
	if (cod3026x->is_suspend)
		regcache_cache_only(cod3026x->regmap, true);

	return IRQ_HANDLED;
}
#endif

int cod3026x_jack_mic_register(struct snd_soc_codec *codec)
{
	struct cod3026x_priv *cod3026x = snd_soc_codec_get_drvdata(codec);
	int num_buttons_zones = ARRAY_SIZE(cod3026x->jack_buttons_zones);
	int i, ret;

	cod3026x->sdev.name = "h2w";

	ret = switch_dev_register(&cod3026x->sdev);
	if (ret < 0)
		dev_err(codec->dev, "Switch registration failed\n");


	cod3026x->input = devm_input_allocate_device(codec->dev);
	if (!cod3026x->input) {
		dev_err(codec->dev, "Failed to allocate input device\n");
		return -ENOMEM;
	}

	/* Not handling Headset events for now.Headset event handling
	 * registered as Input device, causing some conflict with Keyboard Input
	 * device.So, temporarily not handling Headset event, it will be enabled
	 * after proper fix.
	 */
	cod3026x->input->name = "Codec3026 Headset Events";
	cod3026x->input->phys = dev_name(codec->dev);
	cod3026x->input->id.bustype = BUS_I2C;

	cod3026x->input->evbit[0] = BIT_MASK(EV_KEY);
	for (i = 0; i < num_buttons_zones; i++)
		set_bit(cod3026x->jack_buttons_zones[i].code, cod3026x->input->keybit);
	cod3026x->input->dev.parent = codec->dev;
	input_set_drvdata(cod3026x->input, codec);

	ret = input_register_device(cod3026x->input);
	if (ret != 0) {
		cod3026x->input = NULL;
		dev_err(codec->dev, "Failed to register 3026 input device\n");
	}

#ifdef CONFIG_PM_RUNTIME
	pm_runtime_get_sync(codec->dev);
#else
	cod3026x_enable(codec->dev);
#endif

#ifdef CONFIG_PM_RUNTIME
	pm_runtime_put_sync(codec->dev);
#else
	cod3026x_disable(codec->dev);
#endif
	return 0;
}
EXPORT_SYMBOL_GPL(cod3026x_jack_mic_register);

static const struct snd_soc_dai_ops cod3026x_dai_ops = {
	.set_fmt = cod3026x_dai_set_fmt,
	.startup = cod3026x_dai_startup,
	.shutdown = cod3026x_dai_shutdown,
	.hw_params = cod3026x_dai_hw_params,
};

#define COD3026X_RATES		SNDRV_PCM_RATE_8000_192000

#define COD3026X_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE |		\
				SNDRV_PCM_FMTBIT_S20_3LE |	\
				SNDRV_PCM_FMTBIT_S24_LE |	\
				SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver cod3026x_dai[] = {
	{
		.name = "cod3026x-aif",
		.id = 1,
		.playback = {
			.stream_name = "AIF Playback",
			.channels_min = 1,
			.channels_max = 8,
			.rates = COD3026X_RATES,
			.formats = COD3026X_FORMATS,
		},
		.capture = {
			.stream_name = "AIF Capture",
			.channels_min = 1,
			.channels_max = 8,
			.rates = COD3026X_RATES,
			.formats = COD3026X_FORMATS,
		},
		.ops = &cod3026x_dai_ops,
		.symmetric_rates = 1,
	},
	{
		.name = "cod3026x-aif2",
		.id = 2,
		.playback = {
			.stream_name = "AIF2 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = COD3026X_RATES,
			.formats = COD3026X_FORMATS,
		},
		.capture = {
			.stream_name = "AIF2 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = COD3026X_RATES,
			.formats = COD3026X_FORMATS,
		},
		.ops = &cod3026x_dai_ops,
		.symmetric_rates = 1,
	},
};

static int cod3026x_regulators_enable(struct snd_soc_codec *codec)
{
	struct cod3026x_priv *cod3026x = snd_soc_codec_get_drvdata(codec);
	int ret;

	ret = regulator_enable(cod3026x->vdd);

	return ret;
}

static void cod3026x_regulators_disable(struct snd_soc_codec *codec)
{
	struct cod3026x_priv *cod3026x = snd_soc_codec_get_drvdata(codec);

	regulator_disable(cod3026x->vdd);
}

/* The clock for COD3026X is provided by the Audio sub-system. Hence we need to
 * ensure that the audio subsystem is active during codec operation. The
 * easiest way to do this is by calling s2803x_{get/put}_sync() helper
 * functions.
 */
static void cod3026x_clock_enable(struct snd_soc_codec *codec)
{
	audmixer_get_sync();
}

static void cod3026x_clock_disable(struct snd_soc_codec *codec)
{
	audmixer_put_sync();
}

static void cod3026x_save_otp_registers(struct snd_soc_codec *codec)
{
	int i;
	struct cod3026x_priv *cod3026x = snd_soc_codec_get_drvdata(codec);

	dev_dbg(codec->dev, "%s called\n", __func__);
	for (i = 0; i < COD3026X_OTP_MAX_REG; i++)
		cod3026x->otp_reg[i] = (unsigned char) snd_soc_read(codec,
				(COD3026X_D0_CTRL_IREF1 + i));
}

static void cod3026x_restore_otp_registers(struct snd_soc_codec *codec)
{
	int i;
	struct cod3026x_priv *cod3026x = snd_soc_codec_get_drvdata(codec);

	dev_dbg(codec->dev, "%s called\n", __func__);
	for (i = 0; i < COD3026X_OTP_MAX_REG; i++)
		snd_soc_write(codec, (COD3026X_D0_CTRL_IREF1 + i),
				cod3026x->otp_reg[i]);
}

static void cod3026x_reset_io_selector_bits(struct snd_soc_codec *codec)
{
	/* Reset output selector bits */
	snd_soc_update_bits(codec, COD3026X_76_CHOP_DA,
			EN_HP_CHOP_MASK | EN_EP_CHOP_MASK | EN_SPK_PGA_CHOP_MASK, 0x0);
}

/*
 * Configure the mic1 and mic2 bias voltages with default value or the value
 * received from the device tree.
 * Also configure the internal LDO voltage.
 */
static void cod3026x_configure_mic_bias(struct snd_soc_codec *codec)
{
	struct cod3026x_priv *cod3026x = snd_soc_codec_get_drvdata(codec);

	/* Configure Mic1 Bias Voltage */
	snd_soc_update_bits(codec, COD3026X_18_CTRL_REF,
			CTRV_MCB1_MASK,
			(cod3026x->mic_bias1_voltage << CTRV_MCB1_SHIFT));

	/* Configure Mic2 Bias Voltage */
	snd_soc_update_bits(codec, COD3026X_82_MIC_BIAS,
			CTRV_MCB2_MASK,
			(cod3026x->mic_bias2_voltage << CTRV_MCB2_SHIFT));

	/* Configure Mic Bias LDO Voltage */
	snd_soc_update_bits(codec, COD3026X_82_MIC_BIAS,
			CTRV_MCB_LDO_MASK,
			(cod3026x->mic_bias_ldo_voltage << CTRV_MCB_LDO_SHIFT));
}

/**
 * cod3026x_post_fw_update_failure: To be called if f/w update fails
 *
 * In case the firmware is not present or corrupt, we should still be able to
 * run the codec with decent parameters. This values are updated as per the
 * latest stable firmware.
 *
 * The values provided in this function are hard-coded register values, and we
 * need not update these values as per bit-fields.
 */
static void cod3026x_post_fw_update_failure(void *context)
{
	struct snd_soc_codec *codec = (struct snd_soc_codec *)context;
	struct cod3026x_priv *cod3026x = snd_soc_codec_get_drvdata(codec);

	dev_dbg(codec->dev, "%s called, setting defaults\n", __func__);

#ifdef CONFIG_PM_RUNTIME
	pm_runtime_get_sync(codec->dev);
#else
	cod3026x_enable(codec->dev);
#endif

	if (cod3026x->use_external_jd) {
		snd_soc_update_bits(codec, COD3026X_06_IRQ1M,
				IRQ1M_MASK_ALL, 0xff);
		snd_soc_update_bits(codec, COD3026X_07_IRQ2M,
				IRQ2M_MASK_ALL, 0xff);
		snd_soc_update_bits(codec, COD3026X_08_IRQ3M,
				IRQ3M_MASK_ALL, 0xff);
	} else {
		snd_soc_update_bits(codec, COD3026X_06_IRQ1M,
				IRQ1M_MASK_ALL, 0x80);
		snd_soc_update_bits(codec, COD3026X_07_IRQ2M,
				IRQ2M_MASK_ALL, 0xC0);
		snd_soc_update_bits(codec, COD3026X_08_IRQ3M,
				IRQ3M_MASK_ALL, 0xC0);
	}

	snd_soc_write(codec, COD3026X_81_DET_ON, 0x42);
	mdelay(1);
	snd_soc_write(codec, COD3026X_81_DET_ON, 0x43);
	snd_soc_write(codec, COD3026X_85_MIC_DET, 0xff);
	snd_soc_write(codec, COD3026X_86_DET_TIME, 0xff);
	snd_soc_write(codec, COD3026X_87_LDO_DIG, 0x0f);
	snd_soc_write(codec, COD3026X_88_KEY_TIME, 0xf2);
	snd_soc_write(codec, COD3026X_8B_MICBIAS2, 0x0B);
	snd_soc_write(codec, COD3026X_84_JACK_DET2, 0x0d);

	/* Default value, enabling HPF and setting freq at 100Hz */
	snd_soc_write(codec, COD3026X_42_ADC1, 0x0c);

	/* DNC Start Gain setting scenario rev 0.1*/
	snd_soc_write(codec, COD3026X_5A_DNC7, 0x1A);

	/* CP Freq setting scenario rev 0.1*/
	snd_soc_write(codec, COD3026X_77_CTRL_CP, 0x02);

	/* CP Freq setting scenario rev 0.1*/
	snd_soc_write(codec, COD3026X_DD_CTRL_SPKS1, 0x82);

	/* mic3 tdma trim register default set rev 0.4*/
	snd_soc_write(codec, COD3026X_75_AD_MIC3N_TRIM1, 0x11);
	snd_soc_write(codec, COD3026X_78_AD_MIC3N_TRIM2, 0x01);

	/* Reset input/output selector bits */
	cod3026x_reset_io_selector_bits(codec);

	/* Configure mic bias voltage */
	cod3026x_configure_mic_bias(codec);

	/* Frequence selection of ADC analog chopping clk */
	snd_soc_update_bits(codec, COD3026X_70_CLK1_AD,
			SEL_CHCLK_AD_MASK,
			CHOPPING_CLK_1_BY_4 << SEL_CHCLK_AD_SHIFT);

	/* All boot time hardware access is done. Put the device to sleep. */
#ifdef CONFIG_PM_RUNTIME
	pm_runtime_put_sync(codec->dev);
#else
	cod3026x_disable(codec->dev);
#endif
}

/**
 * cod3026x_post_fw_update_success: To be called after f/w update
 *
 * The firmware may be enabling some of the path and power registers which are
 * used during path enablement. We need to keep the values of these registers
 * consistent so that the functionality of the codec driver doesn't change
 * because of the firmware.
 */
static void cod3026x_post_fw_update_success(void *context)
{
	struct snd_soc_codec *codec = (struct snd_soc_codec *)context;
	struct cod3026x_priv *cod3026x = snd_soc_codec_get_drvdata(codec);

	dev_dbg(codec->dev, "%s called\n", __func__);

#ifdef CONFIG_PM_RUNTIME
	pm_runtime_get_sync(codec->dev);
#else
	cod3026x_enable(codec->dev);
#endif

	if (cod3026x->use_external_jd) {
		snd_soc_update_bits(codec, COD3026X_06_IRQ1M,
				IRQ1M_MASK_ALL, 0xff);
		snd_soc_update_bits(codec, COD3026X_07_IRQ2M,
				IRQ2M_MASK_ALL, 0xff);
	} else {
		snd_soc_update_bits(codec, COD3026X_06_IRQ1M,
				IRQ1M_MASK_ALL, 0);
		snd_soc_update_bits(codec, COD3026X_07_IRQ2M,
				IRQ2M_MASK_ALL, 0);
	}

	/* Update for 3-pole jack detection */
	snd_soc_write(codec, COD3026X_85_MIC_DET, 0x03);

	/* Reset input/output selector bits */
	cod3026x_reset_io_selector_bits(codec);

	/* Reset the mixer switches for AD and DA */
	snd_soc_write(codec, COD3026X_23_MIX_AD1, 0x0);
	snd_soc_write(codec, COD3026X_36_MIX_DA1, 0x0);
	snd_soc_write(codec, COD3026X_37_MIX_DA2, 0x0);

	/* Reset the auto power bits for AD */
	snd_soc_update_bits(codec, COD3026X_16_PWAUTO_AD,
			APW_AUTO_AD_MASK | APW_MIC3_MASK |
			APW_MIC1_MASK | APW_MIC2_MASK,
			0x0);

	/* Reset the auto power bits for DA */
	snd_soc_update_bits(codec, COD3026X_17_PWAUTO_DA,
			PW_AUTO_DA_MASK | APW_SPK_MASK |
			APW_HP_MASK | APW_EP_MASK,
			0x0);

	/* Configure mic bias voltage */
	cod3026x_configure_mic_bias(codec);

	/*
	 * Need to restore back the device specific OTP values as the firmware
	 * binary might have corrupted the OTP values
	 */
	cod3026x_restore_otp_registers(codec);

	/* All boot time hardware access is done. Put the device to sleep. */
#ifdef CONFIG_PM_RUNTIME
	pm_runtime_put_sync(codec->dev);
#else
	cod3026x_disable(codec->dev);
#endif
}

static void cod3026x_regmap_sync(struct device *dev)
{
	struct cod3026x_priv *cod3026x = dev_get_drvdata(dev);
	unsigned char reg[COD3026X_MAX_REGISTER] = {0,};
	int i;

	/* Read from Cache */
	for (i = 0; i < COD3026X_REGCACHE_SYNC_END_REG; i++)
		if (cod3026x_readable_register(dev, i) &&
				(!cod3026x_volatile_register(dev, i)))
			reg[i] = (unsigned char)
				snd_soc_read(cod3026x->codec, i);

	regcache_cache_bypass(cod3026x->regmap, true);

	snd_soc_write(cod3026x->codec, COD3026X_40_DIGITAL_POWER,
			reg[COD3026X_40_DIGITAL_POWER]);

	/* Update HW */
	for (i = 0; i < COD3026X_REGCACHE_SYNC_END_REG ; i++)
		if (cod3026x_writeable_register(dev, i) &&
				(!cod3026x_volatile_register(dev, i)))
			snd_soc_write(cod3026x->codec, i, reg[i]);

	regcache_cache_bypass(cod3026x->regmap, false);
}

static void cod3026x_reg_restore(struct snd_soc_codec *codec)
{
	struct cod3026x_priv *cod3026x = snd_soc_codec_get_drvdata(codec);

	snd_soc_update_bits(codec, COD3026X_81_DET_ON,
			EN_PDB_JD_CLK_MASK, EN_PDB_JD_CLK_MASK);

	/* Give 15ms delay before storing the otp values */
	usleep_range(15000, 15000 + 1000);

	/*
	 * The OTP values are the boot-time values. For registers D0-DE, we need
	 * to save these register values during boot time. After system reset,
	 * these values are lost and we need to restore them using saved values.
	 */
	if (!cod3026x->is_probe_done) {
		cod3026x_regmap_sync(codec->dev);
		cod3026x_reset_io_selector_bits(codec);
		cod3026x_save_otp_registers(codec);
	} else {
		cod3026x_regmap_sync(codec->dev);
		cod3026x_restore_otp_registers(cod3026x->codec);
	}
}

static void cod3026x_i2c_parse_dt(struct cod3026x_priv *cod3026x)
{
	/* todo .. Need to add DT parsing for 3026 */
	struct device *dev = cod3026x->dev;
	struct device_node *np = dev->of_node;
	unsigned int bias_v_conf;
	int mic_range, mic_delay, btn_rel_val, btn_delay;
	int water_threshold_min1, water_threshold_min2, water_threshold_max;
#ifdef CONFIG_SND_SOC_COD30XX_EXT_ANT
	int ant_range;
#endif
	int gdet_mode;
	struct of_phandle_args args;
	int i = 0;
	int ret;

	cod3026x->int_gpio = of_get_gpio(np, 0);

	if (cod3026x->int_gpio < 0)
		dev_err(dev, "(*)Error in getting Codec-3026 Interrupt gpio\n");

#ifdef CONFIG_SND_SOC_COD30XX_EXT_ANT
	cod3026x->ant_det_gpio = of_get_named_gpio(np, "ant-det-gpio", 0);

	if (cod3026x->ant_det_gpio < 0)
		pr_err("%s: can not find the earjack-antdet-gpio in the dt\n", __func__);
	else
		pr_info("%s: earjack-ant-det-gpio =%d\n",
				__func__, cod3026x->ant_det_gpio);

	ret = of_property_read_u32(dev->of_node, "ant-adc-range", &ant_range);
	if (!ret)
		cod3026x->ant_adc_range = ant_range;
	else
		cod3026x->ant_adc_range = 3400;
#endif

	cod3026x->adc_pin = of_get_gpio(np, 1);

	if (gpio_is_valid(cod3026x->adc_pin)) {
		ret = devm_gpio_request(cod3026x->codec->dev,
				cod3026x->adc_pin, "codec_adc_gpio");
		if (ret)
			dev_dbg(cod3026x->codec->dev,
					"%s: fail to assignment codec_adc_gpio\n", __func__);
	}

	/* Default Bias Voltages */
	cod3026x->mic_bias1_voltage = MIC_BIAS1_VO_3_0V;
	cod3026x->mic_bias2_voltage = MIC_BIAS2_VO_3_0V;
	cod3026x->mic_bias_ldo_voltage = MIC_BIAS_LDO_VO_3_3V;

	ret = of_property_read_u32(dev->of_node,
				"mic-bias1-voltage", &bias_v_conf);
	if ((!ret) && ((bias_v_conf >= MIC_BIAS1_VO_2_8V) &&
			(bias_v_conf <= MIC_BIAS1_VO_3_0V)))
		cod3026x->mic_bias1_voltage = bias_v_conf;
	else
		dev_dbg(dev, "Property 'mic-bias1-voltage' %s",
				ret ? "not found" : "invalid value (use:1-3)");

	ret = of_property_read_u32(dev->of_node,
				"mic-bias2-voltage", &bias_v_conf);
	if ((!ret) && ((bias_v_conf >= MIC_BIAS2_VO_2_8V) &&
			(bias_v_conf <= MIC_BIAS2_VO_3_0V)))
		cod3026x->mic_bias2_voltage = bias_v_conf;
	else
		dev_dbg(dev, "Property 'mic-bias2-voltage' %s",
				ret ? "not found" : "invalid value (use:1-3)");

	ret = of_property_read_u32(dev->of_node, "mic-adc-range", &mic_range);
	if (!ret)
		cod3026x->mic_adc_range = mic_range;
	else
		cod3026x->mic_adc_range = 1120;

	ret = of_property_read_u32(dev->of_node, "mic-det-delay", &mic_delay);
	if (!ret)
		cod3026x->mic_det_delay = mic_delay;
	else
		cod3026x->mic_det_delay = 550;

	ret = of_property_read_u32(dev->of_node, "btn-release-value", &btn_rel_val);
	if (!ret)
		cod3026x->btn_release_value = btn_rel_val;
	else
		cod3026x->btn_release_value = 1100;

	ret = of_property_read_u32(dev->of_node, "btn-press-delay", &btn_delay);
	if (!ret)
		cod3026x->btn_press_delay = btn_delay;
	else
		cod3026x->btn_press_delay = 30;

	ret = of_property_read_u32(dev->of_node, "water-threshold-min1", &water_threshold_min1);
	if (!ret)
		cod3026x->water_threshold_adc_min1 = water_threshold_min1;
	else
		cod3026x->water_threshold_adc_min1 = COD3026X_WATER_DET_THRESHOLD_MIN_FIRST;

	ret = of_property_read_u32(dev->of_node, "water-threshold-min2", &water_threshold_min2);
	if (!ret)
		cod3026x->water_threshold_adc_min2 = water_threshold_min2;
	else
		cod3026x->water_threshold_adc_min2 = COD3026X_WATER_DET_THRESHOLD_MIN;

	ret = of_property_read_u32(dev->of_node, "water-threshold-max", &water_threshold_max);
	if (!ret)
		cod3026x->water_threshold_adc_max = water_threshold_max;
	else
		cod3026x->water_threshold_adc_max = COD3026X_WATER_DET_THRESHOLD_MAX;

	ret = of_property_read_u32(dev->of_node,
			"mic-bias-ldo-voltage", &bias_v_conf);
	if ((!ret) && ((bias_v_conf >= MIC_BIAS_LDO_VO_2_8V) &&
				(bias_v_conf <= MIC_BIAS_LDO_VO_3_3V)))
		cod3026x->mic_bias_ldo_voltage = bias_v_conf;
	else
		dev_dbg(dev, "Property 'mic-bias-ldo-voltage' %s",
				ret ? "not found" : "invalid value (use:1-3)");

	dev_dbg(dev, "Bias voltage values: bias1 = %d, bias2= %d, ldo = %d\n",
			cod3026x->mic_bias1_voltage,
			cod3026x->mic_bias2_voltage,
			cod3026x->mic_bias_ldo_voltage);

	if (of_find_property(dev->of_node, "update-firmware", NULL))
		cod3026x->update_fw = true;
	else
		cod3026x->update_fw = false;

	if (of_find_property(dev->of_node, "use-det-adc-mode", NULL) != NULL)
		cod3026x->use_det_adc_mode = true;

	if (of_find_property(dev->of_node, "use-det-gdet-adc-mode", NULL) != NULL) {
		cod3026x->use_det_gdet_adc_mode = true;
		ret = of_property_read_u32(dev->of_node,
				"use-det-gdet-adc-mode", &gdet_mode);
		if (!ret)
			cod3026x->use_det_gdet_adc_mode = gdet_mode;
		else
			cod3026x->use_det_gdet_adc_mode = false;

		dev_dbg(dev, "%s : cod3026x->use_det_gdet_adc_mode: %d\n",
				__func__, cod3026x->use_det_gdet_adc_mode);
	}

	dev_err(dev, "Using %s for jack/button detection\n",
			cod3026x->use_det_adc_mode ? "GPADC" : "internal h/w");
	if (cod3026x->use_det_adc_mode) {
		/* Parsing but-zones, a maximum of 4 buttons are supported */
		for (i = 0; i < 4; i++) {
			if (of_parse_phandle_with_args(dev->of_node,
						"but-zones-list", "#list-but-cells", i, &args))
				break;

			cod3026x->jack_buttons_zones[i].code = args.args[0];
			cod3026x->jack_buttons_zones[i].adc_low = args.args[1];
			cod3026x->jack_buttons_zones[i].adc_high = args.args[2];
		}
		/* initialize button status */
		cod3026x->jack_det.privious_button_state = BUTTON_RELEASE;

		for (i = 0; i < 4; i++)
			dev_err(dev, "[DEBUG]: buttons: code(%d), low(%d), high(%d)\n",
					cod3026x->jack_buttons_zones[i].code,
					cod3026x->jack_buttons_zones[i].adc_low,
					cod3026x->jack_buttons_zones[i].adc_high);
	}
}

static int cod3026x_codec_probe(struct snd_soc_codec *codec)
{
	int ret;
	struct cod3026x_priv *cod3026x = snd_soc_codec_get_drvdata(codec);

	dev_dbg(codec->dev, "(*) %s\n", __func__);
	cod3026x->codec = codec;

	cod3026x->vdd = devm_regulator_get(codec->dev, "vdd");
	if (IS_ERR(cod3026x->vdd)) {
		dev_warn(codec->dev, "failed to get regulator vdd\n");
		return PTR_ERR(cod3026x->vdd);
	}

#ifdef CONFIG_PM_RUNTIME
	pm_runtime_get_sync(codec->dev);
#else
	cod3026x_enable(codec->dev);
#endif

	cod3026x->is_probe_done = true;

	/* Initialize work queue for button handling */
	INIT_DELAYED_WORK(&cod3026x->buttons_work, cod3026x_buttons_work);

	cod3026x->buttons_wq = create_singlethread_workqueue("buttons_wq");
	if (cod3026x->buttons_wq == NULL) {
		dev_err(codec->dev, "Failed to create buttons_wq\n");
		return -ENOMEM;
	}

	INIT_WORK(&cod3026x->jack_det_work, jack_mic_delay_work);

	cod3026x->jack_det_wq = create_singlethread_workqueue("jack_det_wq");
	if (cod3026x->jack_det_wq == NULL) {
		dev_err(codec->dev, "Failed to create jack_det_wq\n");
		return -ENOMEM;
	}

	INIT_WORK(&cod3026x->jack_det_adc_work, cod3026x_jack_det_work);

	cod3026x->jack_det_adc_wq = create_singlethread_workqueue("jack_det_adc_wq");
	if (cod3026x->jack_det_adc_wq == NULL) {
		dev_err(codec->dev, "Failed to create jack_det_wq\n");
		return -ENOMEM;
	}

	INIT_DELAYED_WORK(&cod3026x->water_det_adc_work , cod3026x_water_det_work);

	cod3026x->water_det_adc_wq = create_singlethread_workqueue("water_det_adc_wq");
	if (cod3026x->water_det_adc_wq == NULL) {
		dev_err(codec->dev, "Failed to create water_det_adc_wq\n");
		return -ENOMEM;
	}

	INIT_DELAYED_WORK(&cod3026x->water_det_polling_work, cod3026x_water_polling_work);

	INIT_WORK(&cod3026x->adc_mute_work, cod3026x_adc_mute_work);
	cod3026x->adc_mute_wq = create_singlethread_workqueue("adc_mute_wq");
	if (cod3026x->adc_mute_wq == NULL) {
		dev_err(codec->dev, "Failed to create adc_mute_wq\n");
		return -ENOMEM;
	}

	cod3026x_adc_start(cod3026x);

	cod3026x->aifrate = COD3026X_SAMPLE_RATE_48KHZ;

	cod3026x_i2c_parse_dt(cod3026x);

	cod3026x->jack_det.adc_val = -EINVAL;

	cod3026x->water_det.water_det = COD3026X_NO_DET_WATER;

	if (cod3026x->update_fw)
		exynos_regmap_update_fw(COD3026X_FIRMWARE_NAME,
				codec->dev, cod3026x->regmap, cod3026x->i2c_addr,
				cod3026x_post_fw_update_success, codec,
				cod3026x_post_fw_update_failure, codec);
	else
		cod3026x_post_fw_update_failure(codec);

	mutex_init(&cod3026x->adc_mute_lock);

	/* it should be modify to move machine driver */
	cod3026x_jack_mic_register(codec);

	if (cod3026x->int_gpio > 0) {
		dev_err(codec->dev, "[DEBUG]%s : int_gpio %d\n",
				__func__, (int)cod3026x->int_gpio);
		ret = gpio_request(cod3026x->int_gpio, "cod3026x_irq");
		if (ret < 0)
			dev_err(codec->dev, "%s : Request for %d GPIO failed\n",
					__func__, (int)cod3026x->int_gpio);

		ret = gpio_direction_input(cod3026x->int_gpio);
		if (ret < 0)
			dev_err(codec->dev,
					"Setting 3026 interrupt GPIO direction to input: failed\n");

		mutex_init(&cod3026x->jackdet_lock);
		mutex_init(&cod3026x->key_lock);
		wake_lock_init(&cod3026x->jack_wake_lock, WAKE_LOCK_SUSPEND, "jack_wl");

		ret = request_threaded_irq(
				gpio_to_irq(cod3026x->int_gpio),
				NULL, cod3026x_threaded_isr,
				IRQF_TRIGGER_LOW | IRQF_ONESHOT,
				"cod3026_theaded_isr", cod3026x);
		if (ret < 0)
			dev_err(codec->dev,
					"Error %d in requesting 3026 interrupt line:%d\n",
					ret, cod3026x->int_gpio);

		ret = irq_set_irq_wake(gpio_to_irq(cod3026x->int_gpio), 1);
		if (ret < 0)
			dev_err(codec->dev, "cannot set 3026 irq_set_irq_wake\n");
	}

#ifdef CONFIG_SND_SOC_COD30XX_EXT_ANT
	if (cod3026x->ant_det_gpio > 0) {
		dev_err(codec->dev, "[DEBUG]%s : ant_det_gpio %d\n",
				__func__, (int)cod3026x->ant_det_gpio);

		ret = gpio_request(cod3026x->ant_det_gpio, "cod3026x_ant_detect");
		if (ret < 0)
			dev_err(codec->dev, "%s : Request for %d GPIO failed\n",
					__func__, (int)cod3026x->ant_det_gpio);

		ret = gpio_direction_input(cod3026x->ant_det_gpio);
		if (ret < 0)
			dev_err(codec->dev,
					"Setting 3026 interrupt GPIO direction to input: failed\n");

		/* If not set int_gpio, do lock init */
		if (cod3026x->int_gpio <= 0) {
			mutex_init(&cod3026x->jackdet_lock);
			mutex_init(&cod3026x->key_lock);
			wake_lock_init(&cod3026x->jack_wake_lock,
					WAKE_LOCK_SUSPEND, "jack_wl");
		}

#ifndef CONFIG_SEC_FACTORY
		ret = request_threaded_irq(
				gpio_to_irq(cod3026x->ant_det_gpio),
				NULL, cod3026x_ant_thread_isr,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				"sec_ant_detect", cod3026x);
		if (ret < 0)
			dev_err(codec->dev,
					"Error %d in requesting 3026 interrupt line:%d\n",
					ret, cod3026x->ant_det_gpio);
#endif

		ret = irq_set_irq_wake(gpio_to_irq(cod3026x->ant_det_gpio), 1);
		if (ret < 0)
			dev_err(codec->dev, "cannot set 3026 irq_set_irq_wake\n");
	}
#endif

	snd_soc_dapm_ignore_suspend(&codec->dapm, "SPKOUTLN");
	snd_soc_dapm_ignore_suspend(&codec->dapm, "HPOUTLN");
	snd_soc_dapm_ignore_suspend(&codec->dapm, "EPOUTN");
	snd_soc_dapm_ignore_suspend(&codec->dapm, "IN1L");
	snd_soc_dapm_ignore_suspend(&codec->dapm, "IN2L");
	snd_soc_dapm_ignore_suspend(&codec->dapm, "IN3L");
	snd_soc_dapm_ignore_suspend(&codec->dapm, "IN4L");
	snd_soc_dapm_ignore_suspend(&codec->dapm, "AIF Playback");
	snd_soc_dapm_ignore_suspend(&codec->dapm, "AIF Capture");
	snd_soc_dapm_ignore_suspend(&codec->dapm, "AIF2 Playback");
	snd_soc_dapm_ignore_suspend(&codec->dapm, "AIF2 Capture");
	snd_soc_dapm_sync(&codec->dapm);

#ifdef CONFIG_PM_RUNTIME
	pm_runtime_put_sync(codec->dev);
#else
	cod3026x_disable(codec->dev);
#endif

	return 0;
}

static int cod3026x_codec_remove(struct snd_soc_codec *codec)
{
	struct cod3026x_priv *cod3026x = snd_soc_codec_get_drvdata(codec);

	dev_dbg(codec->dev, "(*) %s called\n", __func__);

	cancel_delayed_work_sync(&cod3026x->key_work);
	if (cod3026x->int_gpio) {
		free_irq(gpio_to_irq(cod3026x->int_gpio), cod3026x);
		gpio_free(cod3026x->int_gpio);
	}

	if ((cod3026x->use_det_gdet_adc_mode == 2) && cod3026x->adc_pin)
		devm_gpio_free(cod3026x->codec->dev, cod3026x->adc_pin);

	cod3026x_regulators_disable(codec);

	destroy_workqueue(cod3026x->buttons_wq);
	destroy_workqueue(cod3026x->jack_det_wq);
	destroy_workqueue(cod3026x->jack_det_adc_wq);
	destroy_workqueue(cod3026x->water_det_adc_wq);

	wake_lock_destroy(&cod3026x->jack_wake_lock);

	cancel_delayed_work(&cod3026x->water_det_polling_work);

	cod3026x_adc_stop(cod3026x);

	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_cod3026x = {
	.probe = cod3026x_codec_probe,
	.remove = cod3026x_codec_remove,
	.controls = cod3026x_snd_controls,
	.num_controls = ARRAY_SIZE(cod3026x_snd_controls),
	.dapm_widgets = cod3026x_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(cod3026x_dapm_widgets),
	.dapm_routes = cod3026x_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(cod3026x_dapm_routes),
	.ignore_pmdown_time = true,
	.idle_bias_off = true,
};

#ifdef CONFIG_SND_SOC_SEC_HS_SYSFS
#include "../samsung/jack_joshua.c"
#endif
static int cod3026x_i2c_probe(struct i2c_client *i2c,
		const struct i2c_device_id *id)
{
	struct cod3026x_priv *cod3026x;
	struct pinctrl *pinctrl;
	int ret;

	cod3026x = kzalloc(sizeof(struct cod3026x_priv), GFP_KERNEL);
	if (cod3026x == NULL)
		return -ENOMEM;
	cod3026x->dev = &i2c->dev;
	cod3026x->i2c_addr = i2c->addr;
	cod3026x->use_external_jd = false;
	cod3026x->is_probe_done = false;
	cod3026x->use_det_adc_mode = false;
	cod3026x->use_det_gdet_adc_mode = false;

	cod3026x->regmap = devm_regmap_init_i2c(i2c, &cod3026x_regmap);
	if (IS_ERR(cod3026x->regmap)) {
		dev_err(&i2c->dev, "Failed to allocate regmap: %li\n",
				PTR_ERR(cod3026x->regmap));
		kfree(cod3026x);
		return -ENOMEM;
	}

	regcache_mark_dirty(cod3026x->regmap);

	pinctrl = devm_pinctrl_get(&i2c->dev);
	if (IS_ERR(pinctrl))
		dev_warn(&i2c->dev, "did not get pins for codec: %li\n",
				PTR_ERR(pinctrl));
	else
		cod3026x->pinctrl = pinctrl;

	i2c_set_clientdata(i2c, cod3026x);

	ret = snd_soc_register_codec(&i2c->dev, &soc_codec_dev_cod3026x,
			cod3026x_dai, ARRAY_SIZE(cod3026x_dai));
	if (ret < 0)
		dev_err(&i2c->dev, "Failed to register codec: %d\n", ret);

#ifdef CONFIG_PM_RUNTIME
	pm_runtime_enable(cod3026x->dev);
#endif

#ifdef CONFIG_SND_SOC_SEC_HS_SYSFS
	create_jack_devices(cod3026x);
#endif

	return ret;
}

static int cod3026x_i2c_remove(struct i2c_client *i2c)
{
	snd_soc_unregister_codec(&i2c->dev);
	return 0;
}

static void cod3026x_cfg_gpio(struct device *dev, const char *name)
{
	struct pinctrl_state *pin_state;
	struct cod3026x_priv *cod3026x = dev_get_drvdata(dev);

	pin_state = pinctrl_lookup_state(cod3026x->pinctrl, name);
	if (IS_ERR(pin_state))
		goto err;

	if (pinctrl_select_state(cod3026x->pinctrl, pin_state) < 0)
		goto err;

	return;
err:
	dev_err(dev, "Unable to configure codec gpio as %s\n", name);
}

static int cod3026x_enable(struct device *dev)
{
	struct cod3026x_priv *cod3026x = dev_get_drvdata(dev);

	dev_dbg(dev, "(*) %s\n", __func__);

	cod3026x_cfg_gpio(dev, "default");
	cod3026x_regulators_enable(cod3026x->codec);
	cod3026x_clock_enable(cod3026x->codec);
	/*
	 * Below sequence should be maintained, so that even the jd interupt
	 * changes the cache mode between below two line should not cause
	 * issue
	 */
	cod3026x->is_suspend = false;

	/* Disable cache_only feature and sync the cache with h/w */
	regcache_cache_only(cod3026x->regmap, false);
	cod3026x_reg_restore(cod3026x->codec);

	return 0;
}

static int cod3026x_disable(struct device *dev)
{
	struct cod3026x_priv *cod3026x = dev_get_drvdata(dev);

	dev_dbg(dev, "(*) %s\n", __func__);

	/*
	 * Below sequence should be maintained, so that even the jd interupt
	 * changes the cache mode between below two line should not cause
	 * issue
	 */
	cod3026x->is_suspend = true;

	/* As device is going to suspend-state, limit the writes to cache */
	regcache_cache_only(cod3026x->regmap, true);

	cod3026x_clock_disable(cod3026x->codec);
	cod3026x_regulators_disable(cod3026x->codec);
	cod3026x_cfg_gpio(dev, "idle");

	return 0;
}

static int cod3026x_sys_suspend(struct device *dev)
{
#ifndef CONFIG_PM_RUNTIME
	if (is_cp_aud_enabled()) {
		dev_dbg(dev, "(*)Don't suspend Codec-3026, cp functioning\n");
		return 0;
	}
	dev_dbg(dev, "(*) %s\n", __func__);
	cod3026x_disable(dev);
#endif

	return 0;
}

static int cod3026x_sys_resume(struct device *dev)
{
#ifndef CONFIG_PM_RUNTIME
	struct cod3026x_priv *cod3026x = dev_get_drvdata(dev);

	if (!cod3026x->is_suspend) {
		dev_dbg(dev, "(*)Codec-3026 not resuming, cp functioning\n");
		return 0;
	}
	dev_dbg(dev, "(*) %s\n", __func__);
	cod3026x_enable(dev);
#endif

	return 0;
}

#ifdef CONFIG_PM_RUNTIME
static int cod3026x_runtime_resume(struct device *dev)
{
	dev_dbg(dev, "(*) %s\n", __func__);

	cod3026x_enable(dev);

	return 0;
}

static int cod3026x_runtime_suspend(struct device *dev)
{
	dev_dbg(dev, "(*) %s\n", __func__);

	cod3026x_disable(dev);

	return 0;
}
#endif

static const struct dev_pm_ops cod3026x_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(
			cod3026x_sys_suspend,
			cod3026x_sys_resume
	)
	SET_RUNTIME_PM_OPS(
			cod3026x_runtime_suspend,
			cod3026x_runtime_resume,
			NULL
	)
};

static const struct i2c_device_id cod3026x_i2c_id[] = {
	{ "cod3026x", 3026 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, cod3026x_i2c_id);

const struct of_device_id cod3026x_of_match[] = {
	{ .compatible = "codec,cod3026x",},
	{},
};

static struct i2c_driver cod3026x_i2c_driver = {
	.driver = {
		.name = "cod3026x",
		.owner = THIS_MODULE,
		.pm = &cod3026x_pm,
		.of_match_table = of_match_ptr(cod3026x_of_match),
	},
	.probe = cod3026x_i2c_probe,
	.remove = cod3026x_i2c_remove,
	.id_table = cod3026x_i2c_id,
};

module_i2c_driver(cod3026x_i2c_driver);

MODULE_DESCRIPTION("ASoC COD3026X driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:COD3026X-codec");
MODULE_FIRMWARE(COD3026X_FIRMWARE_NAME);
