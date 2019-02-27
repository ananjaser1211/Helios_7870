/*
 * Copyright (c) 2015 Samsung Electronics Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/workqueue.h>
#include <linux/i2c.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <linux/modem_notifier.h>

#include <sound/exynos.h>
#include <sound/jack.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#ifdef SND_SOC_REGMAP_FIRMWARE
#include <sound/exynos_regmap_fw.h>
#endif
#include <sound/exynos-audmixer-v2.h>
#include "exynos-audmixer-regs.h"

/*
 * The sysclk is derived from the audio PLL. The value of the PLL is not always
 * rounded, many times the actual rate is a little bit higher or less than the
 * rounded value.
 *
 * 'clk_set_rate' operation tries to set the given rate or the next lower
 * possible value. Thus if the PLL rate is slightly higher than rounded value,
 * we won't get the given rate through a direct division. This will result in
 * getting a lower clock rate. Asking for a slightly higher clock rate will
 * result in setting appropriate clock rate.
 *
 * The value '100' is a heuristic value and there is no clear rule to derive
 * this value.
 */
#define AUDMIXER_SYS_CLK_FREQ_48KHZ	(24576000U + 100)
#define AUDMIXER_SYS_CLK_FREQ_192KHZ	(49152000U + 100)

#define AUDMIXER_SAMPLE_RATE_8KHZ	8000
#define AUDMIXER_SAMPLE_RATE_16KHZ	16000
#define AUDMIXER_SAMPLE_RATE_48KHZ	48000
#define AUDMIXER_SAMPLE_RATE_192KHZ	192000



/*
 * During every mixer control operation, the Audio Mixer needs to be resumed and
 * for power-saving, it needs to be suspended afterwards. In normal scenario,
 * a batch of control operations is performed. In this case, the mixer would
 * require to perform a resume-suspend cycle for each operation. To avoid the
 * unnecessary suspend-resume cycles, mixer suspend is called after a certain
 * delay. Following macro defines that delay period in milli-seconds.
 *
 * Further details, refer to function audmixer_soc_kcontrol_handler.
 */
#define AUDMIXER_RPM_SUSPEND_DELAY_MS	(500)

#define S2803X_FIRMWARE_NAME	"cod3025x-s2803x-aud-fw.bin"

#ifdef CONFIG_SND_SOC_SAMSUNG_VERBOSE_DEBUG
#ifdef dev_dbg
#undef dev_dbg
#endif
#define dev_dbg dev_err
#endif

/* enum audmixer_type: Audio Mixer revision identifier */
enum audmixer_type {
	SRC2801X,
	SRC2803X,
	SRC1402X,
};

/*
 * enum audmixer_bus_type: Connection interface type of the Audio Mixer
 *
 * The initial revision of Audio Mixer chip (SRC2801X) was connected to an I2C
 * bus, whereas the later revisions were connected to the APB bus. This changes
 * the way the regmap/cache etc works.
 *
 * If bus type is AUDMIXER_I2C, regmap-i2c is used, regcache is used.
 * If bus type is AUDMIXER_APB, regmap-mmio is used, regcache is not used.
 */
enum audmixer_bus_type {
	AUDMIXER_I2C = 0,
	AUDMIXER_APB,
};

/*
 * struct audmixer_hw_config: H/W configuration information for Audio Mixer
 * @type: Audio Mixer chip revision identifier
 * @bus: Audio Mixer chip connection interface type
 */
struct audmixer_hw_config {
	enum audmixer_type type;
	enum audmixer_bus_type bus;
};

/* H/W configuration information for SRC2801X */
static struct audmixer_hw_config s2801x_config = {
	.type = SRC2801X,
	.bus = AUDMIXER_I2C,
};

/* H/W configuration information for SRC2803X */
static struct audmixer_hw_config s2803x_config = {
	.type = SRC2803X,
	.bus = AUDMIXER_APB,
};

/* H/W configuration information for SRC1402X */
static struct audmixer_hw_config s1402x_config = {
	.type = SRC1402X,
	.bus = AUDMIXER_APB,
};

/* I2C device ID information for SRC2801X */
static const struct i2c_device_id audmixer_i2c_id[] = {
	{ "s2801x", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, audmixer_i2c_id);

/*
 * auxmixer_i2c_dt_ids: List of supported compatible strings for I2C driver
 *
 * @compatible: The supported compatible string name
 * @data: Related H/W configuration structure
 */
static const struct of_device_id audmixer_i2c_dt_ids[] = {
	{ .compatible = "samsung,s2801x", .data = (void *) &s2801x_config },
	{ }
};
MODULE_DEVICE_TABLE(of, audmixer_i2c_dt_ids);

/*
 * auxmixer_apb_dt_ids: List of supported compatible strings for APB driver
 *
 * @compatible: The supported compatible string name
 * @data: Related H/W configuration structure
 */
static const struct of_device_id audmixer_apb_dt_ids[] = {
	{ .compatible = "samsung,s2803x", .data = (void *) &s2803x_config },
	{ .compatible = "samsung,s1402x", .data = (void *) &s1402x_config },
	{ }
};
MODULE_DEVICE_TABLE(of, audmixer_apb_dt_ids);

/*
 * struct audmixer_extrareg_info: Structure to hold information about some SoC
 * specific bits
 *
 */
struct audmixer_soc_reg_info {
	void __iomem *reg;
	unsigned int bit;
	bool active_high;
};

/*
 * struct audmixer_priv: Private data structure for Audio Mixer Driver
 *
 * @regmap: Pointer to regmap handler
 * @codec: Pointer to ALSA codec structure
 * @dev: Pointer to core device structure
 * @regs: IO mapped address for Audio Mixer SFR base address (for APB bus)
 * @aclk: ACLK for Audio Mixer SFR access (for APB bus)
 * @sclk: SCLK for Audio Mixer
 * @bclk0, @bclk1, @bclk2: BCLK for AP/BT/CP interfaces of Audio Mixer
 * @clk_dout: Clock to manage the rate for SCLK
 * @sysreg_reset: RESET register structure variable
 * @reg_alive: ALIVE register  structure variable
 * @sysreg_i2c_id: IO mapped address for I2C_ADDR register (for I2C bus)
 * @i2c_addr: The I2C slave address of the device
 * @is_cp_running: Counter to detect if Audio Mixer is active
 * @num_active_stream: Counter to find number of streams active in all i/f
 * @use_count: Counter to find number of active streams per i/f
 * @pinctrl: Pointer to pinctrl handler
 * @aifrate: Current active sample rate of AIF1 interface (to set clock rate)
 * @is_bck4_mcko_enabled: If true, codec MCLK is provided through BCLK line of
 * AIF4, otherwise normal BCLK is provided. Set it to false if a voice-processor
 * is connected between AIF4 and the audio codec; otherwise it should be true.
 * @update_fw: Flag to check if firmware needs to be updated during boot time
 * @hw: H/W configuration for specific revision of Audio Mixer chip
 */
struct audmixer_priv {
	struct regmap *regmap;
	struct snd_soc_codec *codec;
	struct device *dev;
	void __iomem *regs;
	struct clk *aclk;
	struct clk *sclk;
	struct clk *audclk;
	struct clk *clk_dout;
	void *sysreg_i2c_id;
	unsigned short i2c_addr;
	struct audmixer_soc_reg_info reg_reset;
	struct audmixer_soc_reg_info reg_alive;
	struct audmixer_soc_reg_info reg_i2c;
	atomic_t num_active_stream;
	atomic_t use_count[AUDMIXER_IF_COUNT];
	struct pinctrl *pinctrl;
	unsigned int aifrate;
	bool is_active;
	bool is_bck4_mcko_enabled;
	bool is_alc_enabled;
	bool update_fw;
	bool is_regs_stored;
	bool is_bt_fm_combo;
	const struct audmixer_hw_config *hw;
	unsigned long cp_event;
	struct work_struct cp_notification_work;
	struct workqueue_struct *mixer_cp_wq;
};

struct audmixer_priv *g_audmixer;

/* When the user-space needs to read the value of a register, it reads the
 * regcache value first. If the cache value is not updated (in case where there
 * hasn't been any write to this register yet), it tries to read the value from
 * the hardware. If the device is run-time suspended during that time, it
 * returns an error and read operation fails.
 *
 * To fix this scenario, following registers need to be updated in boot time
 * before they are read by user-space.
 */
static struct reg_default audmixer_init_reg_list[] = {
	/* { reg, def } */
	{ 0x0d, 0x04 },
	{ 0x10, 0x00 },
	{ 0x11, 0x00 },
	{ 0x16, 0x00 },
	{ 0x17, 0x00 },
};

/**
 * Return value:
 * true: if the register value cannot be cached, hence we have to read from the
 * register directly
 * false: if the register value can be read from cache
 */
static bool audmixer_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case AUDMIXER_REG_00_SOFT_RSTB:
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
static bool audmixer_readable_register(struct device *dev, unsigned int reg)
{
	if (g_audmixer->hw->bus == AUDMIXER_APB) {
		if (reg % 4)
			return false;
	}

	switch (reg) {
	case AUDMIXER_REG_00_SOFT_RSTB ... AUDMIXER_REG_11_DMIX2:
	case AUDMIXER_REG_16_DOUTMX1 ... AUDMIXER_REG_17_DOUTMX2:
	case AUDMIXER_REG_68_ALC_CTL ... AUDMIXER_REG_72_ALC_SGR:
		return true;
	case AUDMIXER_REG_18_INAMP_CTL ... AUDMIXER_REG_1A_OUTCP1_CTL:
		if (g_audmixer->hw->type > SRC2803X)
			return true;
		else
			return false;
	default:
		return false;
	}
}

/**
 * Return value:
 * true: if the register value can be modified
 * flase: if the register value cannot be modified
 */
static bool audmixer_writeable_register(struct device *dev, unsigned int reg)
{
	/* If the Audio Mixer is on APB bus, the register offsets are multiple
	 * of 4.
	 */
	if (g_audmixer->hw->bus == AUDMIXER_APB) {
		if (reg % 4)
			return false;
	}

	switch (reg) {
	case AUDMIXER_REG_00_SOFT_RSTB ... AUDMIXER_REG_11_DMIX2:
	case AUDMIXER_REG_16_DOUTMX1 ... AUDMIXER_REG_17_DOUTMX2:
	case AUDMIXER_REG_68_ALC_CTL ... AUDMIXER_REG_72_ALC_SGR:
		return true;
	case AUDMIXER_REG_18_INAMP_CTL ... AUDMIXER_REG_1A_OUTCP1_CTL:
		if (g_audmixer->hw->type > SRC2803X)
			return true;
		else
			return false;
	default:
		return false;
	}
}

static const struct reg_default audmixer_reg_defaults[] = {
	{.reg = 0x0004, .def = 0x0C},
	{.reg = 0x0008, .def = 0x62},
	{.reg = 0x000C, .def = 0x24},
	{.reg = 0x0010, .def = 0x0C},
	{.reg = 0x0014, .def = 0x62},
	{.reg = 0x0018, .def = 0x24},
	{.reg = 0x001C, .def = 0x0C},
	{.reg = 0x0020, .def = 0x62},
	{.reg = 0x0024, .def = 0x24},
	{.reg = 0x0028, .def = 0x04},
	{.reg = 0x002C, .def = 0x01},
	{.reg = 0x0030, .def = 0x02},
	{.reg = 0x0034, .def = 0x00},
	{.reg = 0x0038, .def = 0x01},
	{.reg = 0x003C, .def = 0x00},
	{.reg = 0x0040, .def = 0x00},
	{.reg = 0x0044, .def = 0x00},
	{.reg = 0x0058, .def = 0x00},
	{.reg = 0x005C, .def = 0x00},
	{.reg = 0x0060, .def = 0x2A},
	{.reg = 0x0064, .def = 0x22},
	{.reg = 0x0068, .def = 0x22},
	{.reg = 0x01A0, .def = 0x00},
	{.reg = 0x01A4, .def = 0x80},
	{.reg = 0x01A8, .def = 0x58},
	{.reg = 0x01AC, .def = 0x1A},
	{.reg = 0x01B0, .def = 0x1A},
	{.reg = 0x01B4, .def = 0x12},
	{.reg = 0x01B8, .def = 0x12},
	{.reg = 0x01BC, .def = 0x12},
	{.reg = 0x01C0, .def = 0x17},
	{.reg = 0x01C4, .def = 0x6C},
	{.reg = 0x01C8, .def = 0x6C},
};

static const struct regmap_config audmixer_regmap_mmio = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,

	.max_register = AUDMIXER_MAX_REGISTER,
	.volatile_reg = audmixer_volatile_register,
	.readable_reg = audmixer_readable_register,
	.writeable_reg = audmixer_writeable_register,
	.reg_defaults = audmixer_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(audmixer_reg_defaults),
	.cache_type = REGCACHE_RBTREE,
};

static const struct regmap_config audmixer_regmap_i2c = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = AUDMIXER_MAX_REGISTER,
	.volatile_reg = audmixer_volatile_register,
	.readable_reg = audmixer_readable_register,
	.writeable_reg = audmixer_writeable_register,
	.cache_type = REGCACHE_RBTREE,
};

#ifdef CONFIG_PM_RUNTIME
static int audmixer_reset_sys_data(void)
{
	regmap_write(g_audmixer->regmap, AUDMIXER_REG_00_SOFT_RSTB, 0x0);

	msleep(1);

	regmap_write(g_audmixer->regmap, AUDMIXER_REG_00_SOFT_RSTB,
			BIT(SOFT_RSTB_DATA_RSTB_SHIFT) |
			BIT(SOFT_RSTB_SYS_RSTB_SHIFT));

	msleep(1);

	return 0;
}
#endif

/*
 * audmixer_reset_data: Reset the data path of audio mixer
 *
 * This function needs to be called after the h/w configuration of the audio
 * mixer is modified so that existing data buffer is flushed.
 */
static void audmixer_reset_data(void)
{
	/* Data reset sequence, toggle bit 1 */
	regmap_update_bits(g_audmixer->regmap, AUDMIXER_REG_00_SOFT_RSTB,
			BIT(SOFT_RSTB_DATA_RSTB_SHIFT), 0x0);

	regmap_update_bits(g_audmixer->regmap, AUDMIXER_REG_00_SOFT_RSTB,
			BIT(SOFT_RSTB_DATA_RSTB_SHIFT),
			BIT(SOFT_RSTB_DATA_RSTB_SHIFT));
}

/*
 * audmixer_init_mixer: Configure mixer registers for default operations
 *
 * This function is called once during boot time.
 */
static int audmixer_init_mixer(void)
{
	/**
	 * Set default configuration for AP/CP/BT interfaces
	 *
	 * BCLK = 32fs
	 * LRCLK polarity normal
	 * I2S data format is in I2S standard
	 * I2S data length is 16 bits per sample
	 */
	regmap_write(g_audmixer->regmap, AUDMIXER_REG_02_IN1_CTL2,
			I2S_XFS_32FS << INCTL2_I2S_XFS_SHIFT |
			LRCLK_POL_LEFT << INCTL2_LRCK_POL_SHIFT |
			I2S_DF_I2S << INCTL2_I2S_DF_SHIFT |
			I2S_DL_16BIT << INCTL2_I2S_DL_SHIFT);

	regmap_write(g_audmixer->regmap, AUDMIXER_REG_05_IN2_CTL2,
			I2S_XFS_32FS << INCTL2_I2S_XFS_SHIFT |
			LRCLK_POL_LEFT << INCTL2_LRCK_POL_SHIFT |
			I2S_DF_I2S << INCTL2_I2S_DF_SHIFT |
			I2S_DL_16BIT << INCTL2_I2S_DL_SHIFT);

	/* BT Configuration Initialisation */
	/* I2s mode - Mixer Slave - 32 BCK configuration*/
	regmap_write(g_audmixer->regmap, AUDMIXER_REG_07_IN3_CTL1,
			MIXER_SLAVE << INCTL1_MASTER_SHIFT |
			MPCM_SLOT_32BCK << INCTL1_MPCM_SLOT_SHIFT |
			I2S_PCM_MODE_I2S << INCTL1_I2S_PCM_SHIFT);

	/* 32xfs - i2s format 16bit */
	regmap_write(g_audmixer->regmap, AUDMIXER_REG_08_IN3_CTL2,
			I2S_XFS_32FS << INCTL2_I2S_XFS_SHIFT |
			LRCLK_POL_LEFT << INCTL2_LRCK_POL_SHIFT |
			I2S_DF_I2S << INCTL2_I2S_DF_SHIFT |
			I2S_DL_16BIT << INCTL2_I2S_DL_SHIFT);

	/*
	 * Below setting only requird for PCM mode, but it has no impact for I2S
	 * mode.
	 */
	/* 0 delay, pcm short frame sync */
	regmap_write(g_audmixer->regmap, AUDMIXER_REG_09_IN3_CTL3,
			PCM_DAD_0BCK << INCTL3_PCM_DAD_SHIFT |
			PCM_DF_SHORT_FRAME << INCTL3_PCM_DF_SHIFT);

	/* SLOT_L and SLOT_R registers are different since SRC1402X */
	/* SLOT_L - 1st slot */
	regmap_write(g_audmixer->regmap, AUDMIXER_REG_0B_SLOT_L,
			SLOT_SEL_1ST_SLOT << SLOT_L_SEL_SHIFT);

	/* SLOT_R - 2nd slot */
	regmap_write(g_audmixer->regmap, AUDMIXER_REG_0C_SLOT_R,
			SLOT_SEL_2ND_SLOT << SLOT_R_SEL_SHIFT);

	/* T - Slots 2 slots used */
	regmap_write(g_audmixer->regmap, AUDMIXER_REG_0E_TSLOT,
			TSLOT_USED_2 << TSLOT_SLOT_SHIFT);

	/* amp input configiration */
	regmap_write(g_audmixer->regmap, AUDMIXER_REG_18_INAMP_CTL,
			LRCLK_POL_LEFT << INAMP_CTL_LRCLK_POL_SHIFT |
			BCK_POL_NORMAL << INAMP_CTL_BCK_POL_SHIFT |
			2 << 4 /* reserved data value 2 */ |
			I2S_XFS_64FS << INAMP_CTL_I2S_XFS_SHIFT |
			AMP_I2S_DL_24BIT << INAMP_CTL_I2S_DL_SHIFT);

	/* amp output configiration fot ap */
	regmap_write(g_audmixer->regmap, AUDMIXER_REG_19_OUTAP1_CTL,
			LRCLK_POL_LEFT << OUTAMP_CTL_LRCLK_POL_SHIFT |
			BCK_POL_NORMAL << OUTAMP_CTL_BCK_POL_SHIFT |
			AMP_I2S_DL_16BIT << OUTAMP_CTL_I2S_DL_SHIFT |
			I2S_XFS_32FS << OUTAMP_CTL_I2S_XFS_SHIFT);

	/* amp output configiration fot cp */
	regmap_write(g_audmixer->regmap, AUDMIXER_REG_1A_OUTCP1_CTL,
			LRCLK_POL_LEFT << OUTAMP_CTL_LRCLK_POL_SHIFT |
			BCK_POL_NORMAL << OUTAMP_CTL_BCK_POL_SHIFT |
			AMP_I2S_DL_16BIT << OUTAMP_CTL_I2S_DL_SHIFT |
			I2S_XFS_32FS << OUTAMP_CTL_I2S_XFS_SHIFT);

	/**
	 * BCK4 output is normal BCK for Universal board, the clock output goes
	 * to voice processor. It should be MCLK for SMDK board, as the clock
	 * output goes to codec as MCLK.
	 */
	if (g_audmixer->is_bck4_mcko_enabled)
		regmap_write(g_audmixer->regmap, AUDMIXER_REG_0A_HQ_CTL,
				BIT(HQ_CTL_MCKO_EN_SHIFT) |
				BIT(HQ_CTL_BCK4_MODE_SHIFT));
	else
		regmap_write(g_audmixer->regmap, AUDMIXER_REG_0A_HQ_CTL,
				BIT(HQ_CTL_MCKO_EN_SHIFT));

	/* Enable digital mixer */
	regmap_write(g_audmixer->regmap, AUDMIXER_REG_0F_DIG_EN,
			BIT(DIG_EN_MIX_EN_SHIFT));

	/* Reset DATA path */
	audmixer_reset_data();

	return 0;
}

/*
 * audmixer_cfg_gpio: Configure pin-control states
 *
 * This function is used to update the pin-control states during device
 * suspend/resume and interface startup/shutdown calls.
 */
static void audmixer_cfg_gpio(struct device *dev, const char *name)
{
	struct pinctrl_state *pin_state;
	int ret;

	pin_state = pinctrl_lookup_state(g_audmixer->pinctrl, name);
	if (IS_ERR(pin_state)) {
		dev_err(dev, "Couldn't find pinctrl %s\n", name);
	} else {
		ret = pinctrl_select_state(g_audmixer->pinctrl, pin_state);
		if (ret < 0)
			dev_err(dev, "Unable to configure pinctrl %s\n", name);
	}
}
#ifdef CONFIG_PM_RUNTIME

/*
 * audmixer_save_regs: Prepare the registers before suspend
 */
static void audmixer_save_regs(struct device *dev)
{
	regcache_cache_only(g_audmixer->regmap, true);
	regcache_mark_dirty(g_audmixer->regmap);
}

/*
 * audmixer_restore_regs: Restore the h/w register values
 */
static void audmixer_restore_regs(struct device *dev)
{
	regcache_cache_only(g_audmixer->regmap, false);
	regcache_sync(g_audmixer->regmap);
}
#endif

static int audmixer_set_bclk_ratio(struct snd_soc_dai *dai, unsigned int ratio)
{
	enum audmixer_if_t interface = (enum audmixer_if_t)(dai->id);
	int xfs;
	int ret = 0;

	/* Only I2S_XFS_32FS is verified now */
	switch (ratio) {
	case 32:
		xfs = I2S_XFS_32FS;
		break;

	case 48:
		xfs = I2S_XFS_48FS;
		break;

	case 64:
		xfs = I2S_XFS_64FS;
		break;

	default:
		dev_err(g_audmixer->dev, "%s: Unsupported bfs (%d)\n",
				__func__, ratio);
		return -EINVAL;
	}

	switch (interface) {
	case AUDMIXER_IF_AP:
		ret = regmap_update_bits(g_audmixer->regmap,
			AUDMIXER_REG_02_IN1_CTL2,
			(INCTL2_I2S_XFS_MASK << INCTL2_I2S_XFS_SHIFT),
			(xfs << INCTL2_I2S_XFS_SHIFT));
		break;
	case AUDMIXER_IF_CP:
		ret = regmap_update_bits(g_audmixer->regmap,
			AUDMIXER_REG_05_IN2_CTL2,
			(INCTL2_I2S_XFS_MASK << INCTL2_I2S_XFS_SHIFT),
			(xfs << INCTL2_I2S_XFS_SHIFT));
		break;
	case AUDMIXER_IF_BT:
		ret = regmap_update_bits(g_audmixer->regmap,
			AUDMIXER_REG_08_IN3_CTL2,
			(INCTL2_I2S_XFS_MASK << INCTL2_I2S_XFS_SHIFT),
			(xfs << INCTL2_I2S_XFS_SHIFT));
		break;
	case AUDMIXER_IF_AP1:
		ret = regmap_update_bits(g_audmixer->regmap,
			AUDMIXER_REG_19_OUTAP1_CTL,
			(OUTAMP_CTL_I2S_XFS_MASK << OUTAMP_CTL_I2S_XFS_SHIFT),
			(xfs << OUTAMP_CTL_I2S_XFS_SHIFT));
		break;
	case AUDMIXER_IF_CP1:
		ret = regmap_update_bits(g_audmixer->regmap,
			AUDMIXER_REG_1A_OUTCP1_CTL,
			(OUTAMP_CTL_I2S_XFS_MASK << OUTAMP_CTL_I2S_XFS_SHIFT),
			(xfs << OUTAMP_CTL_I2S_XFS_SHIFT));
		break;
	default:
		dev_err(g_audmixer->dev, "%s: Unsupported interface (%d)\n",
				__func__, interface);
		break;
	}
	return ret;
}

static int audmixer_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{

	if (g_audmixer == NULL)
		return -EINVAL;

	/* Format is priority */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
		case SND_SOC_DAIFMT_I2S:
			regmap_update_bits(g_audmixer->regmap,
					AUDMIXER_REG_07_IN3_CTL1,
					(I2S_PCM_MODE_MASK << INCTL1_I2S_PCM_SHIFT),
					(I2S_PCM_MODE_I2S << INCTL1_I2S_PCM_SHIFT));
			regmap_update_bits(g_audmixer->regmap,
					AUDMIXER_REG_09_IN3_CTL3,
					(INCTL3_PCM_DF_MASK << INCTL3_PCM_DF_SHIFT),
					(PCM_DF_SHORT_FRAME << INCTL3_PCM_DF_SHIFT));
			break;
		case SND_SOC_DAIFMT_DSP_A:
			regmap_update_bits(g_audmixer->regmap,
					AUDMIXER_REG_07_IN3_CTL1,
					(I2S_PCM_MODE_MASK << INCTL1_I2S_PCM_SHIFT),
					(I2S_PCM_MODE_PCM << INCTL1_I2S_PCM_SHIFT));
			regmap_update_bits(g_audmixer->regmap,
					AUDMIXER_REG_09_IN3_CTL3,
					(INCTL3_PCM_DF_MASK << INCTL3_PCM_DF_SHIFT),
					(PCM_DF_LONG_FRAME << INCTL3_PCM_DF_SHIFT));
			break;
		default:
			dev_err(g_audmixer->dev, "Data Format not supported\n");
			return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
		case SND_SOC_DAIFMT_CBS_CFS:
			regmap_update_bits(g_audmixer->regmap,
				AUDMIXER_REG_07_IN3_CTL1,
				(MIXER_MODE_MASK << INCTL1_MASTER_SHIFT),
				MIXER_MASTER << INCTL1_MASTER_SHIFT);
			break;
		case SND_SOC_DAIFMT_CBM_CFM:
			regmap_update_bits(g_audmixer->regmap,
				AUDMIXER_REG_07_IN3_CTL1,
				(MIXER_MODE_MASK << INCTL1_MASTER_SHIFT),
				MIXER_SLAVE << INCTL1_MASTER_SHIFT);
			break;
		default:
			dev_err(g_audmixer->dev, "Format not supported\n");
			return -EINVAL;
	}

	return 0;
}

/*
 * audmixer_hw_params: Configure different interfaces of Audio Mixer.
 *
 * This function is called from the hw_params call of sound-card.
 * It does following:
 * 1. Sets BFS value
 * 2. Sets Data length (16bit or 24bit)
 * 3. Sets sysclk rate (depending on 48KHz or 192KHz playback)
 *
 * @substream: The substream structure passed from ALSA core to sound-card
 * @params: The hw_params structure passed from ALSA core to sound-card
 * @bfs: The BFS value for the particular interface
 * @interface: The ID of the current interface
 *
 * Returns 0 on success otherwise some error code.
 */
static int audmixer_hw_params(struct snd_pcm_substream *substream,
			struct snd_pcm_hw_params *params,
			struct snd_soc_dai *dai)
{
	enum audmixer_if_t interface = (enum audmixer_if_t)(dai->id);
	int dl_bit;
	unsigned int hq_mode = 0;
	unsigned int sys_clk_freq;
	unsigned int aifrate;
	int ret;

	if (g_audmixer == NULL)
		return -EINVAL;

	dev_dbg(g_audmixer->dev, "(%s) %s called for aif%d\n",
			substream->stream ? "C" : "P", __func__, interface);

	/* Only I2S_DL_16BIT is verified now */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_U24:
	case SNDRV_PCM_FORMAT_S24:
		if ((interface == AUDMIXER_IF_AP1) ||
				(interface == AUDMIXER_IF_CP1))
			dl_bit = AMP_I2S_DL_24BIT;
		else
			dl_bit = I2S_DL_24BIT;
		break;

	case SNDRV_PCM_FORMAT_U16_LE:
	case SNDRV_PCM_FORMAT_S16_LE:
		dl_bit = I2S_DL_16BIT;
		break;

	default:
		dev_err(g_audmixer->dev, "%s: Unsupported format\n", __func__);
		return -EINVAL;
	}

	switch (interface) {
	case AUDMIXER_IF_AP:
		ret = regmap_update_bits(g_audmixer->regmap,
			AUDMIXER_REG_02_IN1_CTL2,
			(INCTL2_I2S_DL_MASK << INCTL2_I2S_DL_SHIFT),
			dl_bit);

		aifrate = params_rate(params);

		switch (aifrate) {
		case AUDMIXER_SAMPLE_RATE_192KHZ:
			sys_clk_freq = AUDMIXER_SYS_CLK_FREQ_192KHZ;
			hq_mode = HQ_CTL_HQ_EN_MASK;
			break;
		case AUDMIXER_SAMPLE_RATE_48KHZ:
			sys_clk_freq = AUDMIXER_SYS_CLK_FREQ_48KHZ;
			hq_mode = 0;
			break;
		default:
			dev_err(g_audmixer->dev,
					"%s: Unsupported sample-rate (%u)\n",
					__func__, aifrate);
			return -EINVAL;
		}

		ret = clk_set_rate(g_audmixer->clk_dout, sys_clk_freq);
		if (ret != 0) {
			dev_err(g_audmixer->dev,
				"%s: Error setting mixer sysclk rate as %u\n",
				__func__, sys_clk_freq);
			return ret;
		}

		regmap_update_bits(g_audmixer->regmap, AUDMIXER_REG_0A_HQ_CTL,
				HQ_CTL_HQ_EN_MASK, hq_mode);

		g_audmixer->aifrate = aifrate;
		break;

	case AUDMIXER_IF_CP:
		ret = clk_set_rate(g_audmixer->clk_dout, AUDMIXER_SYS_CLK_FREQ_48KHZ);
		if (ret != 0) {
			dev_err(g_audmixer->dev,
				"%s: Error setting mixer sysclk rate as %u\n",
				__func__, AUDMIXER_SYS_CLK_FREQ_48KHZ);
			return ret;
		}
		ret = regmap_update_bits(g_audmixer->regmap,
			AUDMIXER_REG_05_IN2_CTL2,
			(INCTL2_I2S_DL_MASK << INCTL2_I2S_DL_SHIFT),
			dl_bit);
		break;

	case AUDMIXER_IF_BT:
	case AUDMIXER_IF_FM:
		switch (g_audmixer->hw->type) {
		default:
			ret = regmap_update_bits(g_audmixer->regmap,
				AUDMIXER_REG_08_IN3_CTL2,
			(INCTL2_I2S_DL_MASK << INCTL2_I2S_DL_SHIFT),
			dl_bit);

			/*
			 * Sample rate setting only requird for PCM master mode, but the
			 * below configuration have no impact in I2S mode.
			 */
			aifrate = params_rate(params);

			switch (aifrate) {
			case AUDMIXER_SAMPLE_RATE_8KHZ:
				ret = regmap_update_bits(g_audmixer->regmap,
					AUDMIXER_REG_07_IN3_CTL1,
					(INCTL1_MPCM_SRATE_MASK << INCTL1_MPCM_SRATE_SHIFT),
					(MPCM_SRATE_8KHZ << INCTL1_MPCM_SRATE_SHIFT));
				break;
			case AUDMIXER_SAMPLE_RATE_16KHZ:
				ret = regmap_update_bits(g_audmixer->regmap,
					AUDMIXER_REG_07_IN3_CTL1,
					(INCTL1_MPCM_SRATE_MASK << INCTL1_MPCM_SRATE_SHIFT),
					(MPCM_SRATE_16KHZ << INCTL1_MPCM_SRATE_SHIFT));
				break;
			case AUDMIXER_SAMPLE_RATE_48KHZ:
				break;
			default:
				dev_warn(g_audmixer->dev,
						"%s: Unsupported BT/FM samplerate (%d)\n",
						__func__, aifrate);
				break;
			}
			regmap_update_bits(g_audmixer->regmap, AUDMIXER_REG_0A_HQ_CTL,
				HQ_CTL_HQ_EN_MASK, hq_mode);
			sys_clk_freq = AUDMIXER_SYS_CLK_FREQ_48KHZ;
			ret = clk_set_rate(g_audmixer->clk_dout, sys_clk_freq);
			if (ret != 0) {
				dev_err(g_audmixer->dev,
					"%s: Error setting mixer sysclk rate as %u\n",
					__func__, sys_clk_freq);
				return ret;
			}
			break;
		}
		break;

	case AUDMIXER_IF_AP1:
		ret = regmap_update_bits(g_audmixer->regmap,
			AUDMIXER_REG_19_OUTAP1_CTL,
			(OUTAMP_CTL_I2S_DL_MASK << OUTAMP_CTL_I2S_DL_SHIFT),
			(dl_bit << OUTAMP_CTL_I2S_DL_SHIFT));

		break;

	case AUDMIXER_IF_CP1:
		ret = regmap_update_bits(g_audmixer->regmap,
			AUDMIXER_REG_1A_OUTCP1_CTL,
			(OUTAMP_CTL_I2S_DL_MASK << OUTAMP_CTL_I2S_DL_SHIFT),
			(dl_bit << OUTAMP_CTL_I2S_DL_SHIFT));

		break;

	default:
		dev_err(g_audmixer->dev, "%s: Unsupported interface (%d)\n",
				__func__, interface);
		return -EINVAL;
	}

	/* Reset the data path only if current substream is active */
	if (atomic_read(&g_audmixer->num_active_stream) == 1)
		audmixer_reset_data();

	return 0;
}


/*
 * audmixer_startup: Start a particular interface of Audio Mixer
 *
 * This function is called from the startup call of sound-card.
 * It does following.
 * 1. Enables mixer device through runtime resume call
 * 2. Increments various usage counts
 * 3. Configures BT GPIOs, if required.
 *
 * @interface: The ID of the current interface
 */
static int audmixer_startup(struct snd_pcm_substream *stream, struct snd_soc_dai *dai)
{
	enum audmixer_if_t interface = (enum audmixer_if_t)(dai->id);

	if (g_audmixer == NULL)
		return -ENODEV;

	dev_dbg(g_audmixer->dev, "aif%d: %s called\n", interface, __func__);

	/*
	 * Runtime resume sequence internally checks is_active variable for
	 * CP call mode. If the value of is_active variable is non-zero, the
	 * system assumes that the it is resuming from CP call mode and skips
	 * the power-domain powering on sequence.
	 *
	 * If this variable is incremented before pm_runtime_get_sync() is
	 * called, the framework won't power on the power-domain even though the
	 * call was made during the start of a call.
	 *
	 * Hence, is_active variable should be enabled after the
	 * pm_runtime_get_sync() function is called.
	 */

	pm_runtime_get_sync(g_audmixer->dev);

	/* Keep a count of number of active interfaces, useful if we need to
	 * perform some operations based on the usage count of an interface
	 */
	atomic_inc(&g_audmixer->use_count[interface]);

	switch (g_audmixer->hw->type) {
	default:
		switch (interface) {
		case AUDMIXER_IF_BT:
			if (atomic_read(&g_audmixer->use_count[AUDMIXER_IF_BT]) == 1)
				audmixer_cfg_gpio(g_audmixer->dev, "bt");
			lpass_set_fm_bt_mux(0);
			break;
		case AUDMIXER_IF_FM:
			if (atomic_read(&g_audmixer->use_count[AUDMIXER_IF_FM]) == 1)
				audmixer_cfg_gpio(g_audmixer->dev, "fm");
			if (g_audmixer->is_bt_fm_combo)
				lpass_set_fm_bt_mux(0);
			else
				lpass_set_fm_bt_mux(1);
			break;
		default:
			break;
		}
		break;
	}

	atomic_inc(&g_audmixer->num_active_stream);

	return 0;
}

/*
 * audmixer_shutdown: Stop a particular interface of Audio Mixer
 *
 * This function is called from the shutdown call of sound-card.
 * It does following.
 * 1. Decrements various usage counts
 * 2. Reset BT GPIOs, if required.
 * 3. Disables mixer device through runtime suspend call if use count is zero
 *
 * @interface: The ID of the current interface
 */
static void audmixer_shutdown(struct snd_pcm_substream *stream, struct snd_soc_dai *dai)
{
	enum audmixer_if_t interface = (enum audmixer_if_t)(dai->id);

	if (g_audmixer == NULL)
		return;

	dev_dbg(g_audmixer->dev, "aif%d: %s called\n", interface, __func__);

	atomic_dec(&g_audmixer->num_active_stream);

	atomic_dec(&g_audmixer->use_count[interface]);

	switch (g_audmixer->hw->type) {
	default:
		switch (interface) {
		case AUDMIXER_IF_BT:
			if (atomic_read(&g_audmixer->use_count[AUDMIXER_IF_BT]) == 0)
				audmixer_cfg_gpio(g_audmixer->dev, "bt-idle");
			break;
		case AUDMIXER_IF_FM:
			if (atomic_read(&g_audmixer->use_count[AUDMIXER_IF_FM]) == 0)
				audmixer_cfg_gpio(g_audmixer->dev, "fm-idle");
			break;
		default:
			break;
		}
		break;
	}

	pm_runtime_put_sync(g_audmixer->dev);
}

static const struct snd_soc_dai_ops audmixer_dai_ops = {
	.set_bclk_ratio = audmixer_set_bclk_ratio,
	.startup = audmixer_startup,
	.shutdown = audmixer_shutdown,
	.hw_params = audmixer_hw_params,
	.set_fmt = audmixer_set_fmt,
};

/**
 * is_cp_aud_enabled(void): Checks whether mixer is active or not
 *
 * This function is used by PM core to find out if the audio path is active or
 * not. If audio path is active, the system goes into LPA or CP_CALL mode during
 * system suspend time.
 *
 * Returns true if audio path is enabled, false otherwise.
 */
bool is_cp_aud_enabled(void)
{
	if (g_audmixer == NULL)
		return false;

	return g_audmixer->is_active;
}
EXPORT_SYMBOL_GPL(is_cp_aud_enabled);

/* thread run whenever the cp event received */
static void audmixer_cp_notification_work(struct work_struct *work)
{
	struct audmixer_priv *audmixer =
		container_of(work, struct audmixer_priv, cp_notification_work);
	enum modem_event event = audmixer->cp_event;

	if ((event == MODEM_EVENT_EXIT ||
		event == MODEM_EVENT_RESET || event == MODEM_EVENT_WATCHDOG)) {
		/*
		 * Get runtime PM, To keep the clocks enabled untill below
		 * write executes.
		 */
#ifdef CONFIG_PM_RUNTIME
		pm_runtime_get_sync(audmixer->dev);
#endif
		regmap_update_bits(audmixer->regmap, AUDMIXER_REG_10_DMIX1,
						DMIX1_MIX_EN2_MASK, 0);

#ifdef CONFIG_PM_RUNTIME
		pm_runtime_put_sync(audmixer->dev);
#endif
		dev_dbg(g_audmixer->dev, "cp dmix path disabled\n");
	}
}

static int audmixer_cp_notification_handler(struct notifier_block *nb,
				unsigned long action, void *data)
{

	dev_dbg(g_audmixer->dev, "%s called, event = %ld\n", __func__,
							action);

	if (!is_cp_aud_enabled()) {
		dev_dbg(g_audmixer->dev, "Mixer not active, Exiting..\n");
		return 0;
	}

	g_audmixer->cp_event = action;
	queue_work(g_audmixer->mixer_cp_wq, &g_audmixer->cp_notification_work);

	return 0;
}

struct notifier_block audmixer_cp_nb = {
		.notifier_call = audmixer_cp_notification_handler,
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
 * DONE
 * audmixer_rmix_tlv
 *
 * Range:
 * 0dB, -2.87dB, -6.02dB, -9.28dB, -12.04dB, -14.54dB, -18.06dB, -20.56dB
 *
 * This range is used for following controls
 * RMIX1_LVL, reg(0x0d), shift(0), width(3), invert(1), max(7)
 * RMIX2_LVL, reg(0x0d), shift(4), width(3), invert(1), max(7)
 * MIX1_LVL,  reg(0x10), shift(0), width(3), invert(1), max(7)
 * MIX2_LVL,  reg(0x10), shift(4), width(3), invert(1), max(7)
 * MIX3_LVL,  reg(0x11), shift(0), width(3), invert(1), max(7)
 * MIX4_LVL,  reg(0x11), shift(4), width(3), invert(1), max(7)
 */
static const unsigned int audmixer_mix_tlv[] = {
	TLV_DB_RANGE_HEAD(4),
	0x0, 0x1, TLV_DB_SCALE_ITEM(0, 287, 0),
	0x2, 0x3, TLV_DB_SCALE_ITEM(602, 326, 0),
	0x4, 0x5, TLV_DB_SCALE_ITEM(1204, 250, 0),
	0x6, 0x7, TLV_DB_SCALE_ITEM(1806, 250, 0),
};

/**
 * audmixer_alc_ng_hys_tlv
 *
 * Range: 3dB to 12dB, step 3dB
 *
 * ALC_NG_HYS, reg(0x68), shift(6), width(2), invert(0), max(31)
 */
static const DECLARE_TLV_DB_SCALE(audmixer_alc_ng_hys_tlv, 300, 300, 0);

/**
 * audmixer_alc_max_gain_tlv
 *
 * Range:
 * 0x6c to 0x9c => 0dB to 24dB, step 0.5dB
 *
 * ALC_MAX_GAIN,     reg(0x69), shift(0), width(8), min(0x6c), max(0x9c)
 * ALC_START_GAIN_L, reg(0x71), shift(0), width(8), min(0x6c), max(0x9c)
 * ALC_START_GAIN_R, reg(0x72), shift(0), width(8), min(0x6c), max(0x9c)
 */
static const DECLARE_TLV_DB_SCALE(audmixer_alc_max_gain_tlv, 0, 50, 0);

/**
 * audmixer_alc_min_gain_tlv
 *
 * Range:
 * 0x00 to 0x6c => -54dB to 0dB, step 0.5dB
 *
 * ALC_MIN_GAIN, reg(0x6a), shift(0), width(8), invert(0), max(0x6c)
 */
static const DECLARE_TLV_DB_SCALE(audmixer_alc_min_gain_tlv, -5400, 50, 0);

/**
 * audmixer_alc_lvl_tlv
 *
 * Range: -48dB to 0, step 1.5dB
 *
 * ALC_LVL_L, reg(0x6b), shift(0), width(5), invert(0), max(31)
 * ALC_LVL_R, reg(0x6c), shift(0), width(5), invert(0), max(31)
 */
static const DECLARE_TLV_DB_SCALE(audmixer_alc_lvl_tlv, -4800, 150, 0);


/**
 * audmixer_alc_ng_th_tlv
 *
 * Range: -76.5dB to -30dB, step 1.5dB
 *
 * ALCNGTH, reg(0x70), shift(0), width(5), invert(0), max(31)
 */
static const DECLARE_TLV_DB_SCALE(audmixer_alc_ng_th_tlv, -7650, 150, 0);

/**
 * audmixer_alc_winsel
 *
 * ALC Window-Length Select
 */
static const char *audmixer_alc_winsel_text[] = {
	"fs600", "fs1200", "fs2400", "fs300"
};

static SOC_ENUM_SINGLE_DECL(audmixer_alc_winsel_enum, AUDMIXER_REG_68_ALC_CTL,
				ALC_CTL_WINSEL_SHIFT, audmixer_alc_winsel_text);


/**
 * audmixer_alc_mode
 *
 * ALC Function Select
 */
static const char *audmixer_alc_mode_text[] = {
	"Stereo", "Right", "Left", "Independent"
};

static SOC_ENUM_SINGLE_DECL(audmixer_alc_mode_enum, AUDMIXER_REG_68_ALC_CTL,
				ALC_CTL_ALC_MODE_SHIFT, audmixer_alc_mode_text);



/**
 * ALC Path selection
 */
static const char *audmixer_alc_path_sel_text[] = {
	"ADC", "Mixer"
};

static const struct soc_enum audmixer_alc_path_sel_enum =
SOC_ENUM_SINGLE(AUDMIXER_REG_6D_ALC_HLD, ALC_HLD_ALC_PATH_SEL_SHIFT,
		ARRAY_SIZE(audmixer_alc_path_sel_text), audmixer_alc_path_sel_text);

/**
 * audmixer_mpcm_srate
 *
 * Master PCM sample rate selection
 */
static const char *audmixer_mpcm_master_srate_text[] = {
	"8KHz", "16KHz", "24KHz", "32KHz"
};

static const struct soc_enum audmixer_mpcm_srate1_enum =
	SOC_ENUM_SINGLE(AUDMIXER_REG_01_IN1_CTL1, INCTL1_MPCM_SRATE_SHIFT,
			ARRAY_SIZE(audmixer_mpcm_master_srate_text),
			audmixer_mpcm_master_srate_text);

static const struct soc_enum audmixer_mpcm_srate2_enum =
	SOC_ENUM_SINGLE(AUDMIXER_REG_04_IN2_CTL1, INCTL1_MPCM_SRATE_SHIFT,
			ARRAY_SIZE(audmixer_mpcm_master_srate_text),
			audmixer_mpcm_master_srate_text);

static const struct soc_enum audmixer_mpcm_srate3_enum =
	SOC_ENUM_SINGLE(AUDMIXER_REG_07_IN3_CTL1, INCTL1_MPCM_SRATE_SHIFT,
			ARRAY_SIZE(audmixer_mpcm_master_srate_text),
			audmixer_mpcm_master_srate_text);

/**
 * mpcm_slot_sel
 *
 * Master PCM slot selection
 */
static const char *audmixer_mpcm_slot_text[] = {
	"1 slot", "2 slots", "3 slots", "4 slots"
};

static const struct soc_enum audmixer_mpcm_slot1_enum =
	SOC_ENUM_SINGLE(AUDMIXER_REG_01_IN1_CTL1, INCTL1_MPCM_SLOT_SHIFT,
			ARRAY_SIZE(audmixer_mpcm_slot_text),
			audmixer_mpcm_slot_text);

static const struct soc_enum audmixer_mpcm_slot2_enum =
	SOC_ENUM_SINGLE(AUDMIXER_REG_04_IN2_CTL1, INCTL1_MPCM_SLOT_SHIFT,
			ARRAY_SIZE(audmixer_mpcm_slot_text),
			audmixer_mpcm_slot_text);

static const struct soc_enum audmixer_mpcm_slot3_enum =
	SOC_ENUM_SINGLE(AUDMIXER_REG_07_IN3_CTL1, INCTL1_MPCM_SLOT_SHIFT,
			ARRAY_SIZE(audmixer_mpcm_slot_text),
			audmixer_mpcm_slot_text);

/**
 * bclk_pol
 *
 * Polarity of various bit-clocks
 */
static const char *audmixer_clock_pol_text[] = {
	"Normal", "Inverted"
};

static const struct soc_enum audmixer_bck_pol1_enum =
	SOC_ENUM_SINGLE(AUDMIXER_REG_01_IN1_CTL1, INCTL1_BCK_POL_SHIFT,
			ARRAY_SIZE(audmixer_clock_pol_text),
			audmixer_clock_pol_text);

static const struct soc_enum audmixer_bck_pol2_enum =
	SOC_ENUM_SINGLE(AUDMIXER_REG_04_IN2_CTL1, INCTL1_BCK_POL_SHIFT,
			ARRAY_SIZE(audmixer_clock_pol_text),
			audmixer_clock_pol_text);

static const struct soc_enum audmixer_bck_pol3_enum =
	SOC_ENUM_SINGLE(AUDMIXER_REG_07_IN3_CTL1, INCTL1_BCK_POL_SHIFT,
			ARRAY_SIZE(audmixer_clock_pol_text),
			audmixer_clock_pol_text);

static const struct soc_enum audmixer_lrck_pol1_enum =
	SOC_ENUM_SINGLE(AUDMIXER_REG_02_IN1_CTL2, INCTL2_LRCK_POL_SHIFT,
			ARRAY_SIZE(audmixer_clock_pol_text),
			audmixer_clock_pol_text);

static const struct soc_enum audmixer_lrck_pol2_enum =
	SOC_ENUM_SINGLE(AUDMIXER_REG_05_IN2_CTL2, INCTL2_LRCK_POL_SHIFT,
			ARRAY_SIZE(audmixer_clock_pol_text),
			audmixer_clock_pol_text);

static const struct soc_enum audmixer_lrck_pol3_enum =
	SOC_ENUM_SINGLE(AUDMIXER_REG_08_IN3_CTL2, INCTL2_LRCK_POL_SHIFT,
			ARRAY_SIZE(audmixer_clock_pol_text),
			audmixer_clock_pol_text);

/**
 * i2s_pcm
 *
 * Input Audio Mode
 */
static const char *audmixer_i2s_pcm_text[] = {
	"I2S", "PCM"
};

static const struct soc_enum audmixer_i2s_pcm1_enum =
	SOC_ENUM_SINGLE(AUDMIXER_REG_01_IN1_CTL1, INCTL1_I2S_PCM_SHIFT,
			ARRAY_SIZE(audmixer_i2s_pcm_text),
			audmixer_i2s_pcm_text);

static const struct soc_enum audmixer_i2s_pcm2_enum =
	SOC_ENUM_SINGLE(AUDMIXER_REG_04_IN2_CTL1, INCTL1_I2S_PCM_SHIFT,
			ARRAY_SIZE(audmixer_i2s_pcm_text),
			audmixer_i2s_pcm_text);

static const struct soc_enum audmixer_i2s_pcm3_enum =
	SOC_ENUM_SINGLE(AUDMIXER_REG_07_IN3_CTL1, INCTL1_I2S_PCM_SHIFT,
			ARRAY_SIZE(audmixer_i2s_pcm_text),
			audmixer_i2s_pcm_text);

/**
 * i2s_xfs
 *
 * BCK vs LRCK condition
 */
static const char *audmixer_i2s_xfs_text[] = {
	"32fs", "48fs", "64fs", "64fs"
};

static const struct soc_enum audmixer_i2s_xfs1_enum =
	SOC_ENUM_SINGLE(AUDMIXER_REG_02_IN1_CTL2, INCTL2_I2S_XFS_SHIFT,
			ARRAY_SIZE(audmixer_i2s_xfs_text),
			audmixer_i2s_xfs_text);

static const struct soc_enum audmixer_i2s_xfs2_enum =
	SOC_ENUM_SINGLE(AUDMIXER_REG_05_IN2_CTL2, INCTL2_I2S_XFS_SHIFT,
			ARRAY_SIZE(audmixer_i2s_xfs_text),
			audmixer_i2s_xfs_text);

static const struct soc_enum audmixer_i2s_xfs3_enum =
	SOC_ENUM_SINGLE(AUDMIXER_REG_08_IN3_CTL2, INCTL2_I2S_XFS_SHIFT,
			ARRAY_SIZE(audmixer_i2s_xfs_text),
			audmixer_i2s_xfs_text);

/**
 * i2s_df
 *
 * I2S Data Format
 */
static const char *audmixer_i2s_df_text[] = {
	"I2S", "Left-Justified", "Right-Justified", "Invalid"
};

static const struct soc_enum audmixer_i2s_df1_enum =
	SOC_ENUM_SINGLE(AUDMIXER_REG_02_IN1_CTL2, INCTL2_I2S_DF_SHIFT,
			ARRAY_SIZE(audmixer_i2s_df_text),
			audmixer_i2s_df_text);

static const struct soc_enum audmixer_i2s_df2_enum =
	SOC_ENUM_SINGLE(AUDMIXER_REG_05_IN2_CTL2, INCTL2_I2S_DF_SHIFT,
			ARRAY_SIZE(audmixer_i2s_df_text),
			audmixer_i2s_df_text);

static const struct soc_enum audmixer_i2s_df3_enum =
	SOC_ENUM_SINGLE(AUDMIXER_REG_08_IN3_CTL2, INCTL2_I2S_DF_SHIFT,
			ARRAY_SIZE(audmixer_i2s_df_text),
			audmixer_i2s_df_text);

/**
 * i2s_dl
 *
 * I2S Data Length
 */
static const char *audmixer_i2s_dl_text[] = {
	"16-bit", "18-bit", "20-bit", "24-bit"
};

static const struct soc_enum audmixer_i2s_dl1_enum =
	SOC_ENUM_SINGLE(AUDMIXER_REG_02_IN1_CTL2, INCTL2_I2S_DL_SHIFT,
			ARRAY_SIZE(audmixer_i2s_dl_text),
			audmixer_i2s_dl_text);

static const struct soc_enum audmixer_i2s_dl2_enum =
	SOC_ENUM_SINGLE(AUDMIXER_REG_05_IN2_CTL2, INCTL2_I2S_DL_SHIFT,
			ARRAY_SIZE(audmixer_i2s_dl_text),
			audmixer_i2s_dl_text);

static const struct soc_enum audmixer_i2s_dl3_enum =
	SOC_ENUM_SINGLE(AUDMIXER_REG_08_IN3_CTL2, INCTL2_I2S_DL_SHIFT,
			ARRAY_SIZE(audmixer_i2s_dl_text),
			audmixer_i2s_dl_text);

/**
 * pcm_dad
 *
 * PCM Data Additional Delay
 */
static const char *audmixer_pcm_dad_text[] = {
	"1 bck", "0 bck", "2 bck", "", "3 bck", "", "4 bck", ""
};

static const struct soc_enum audmixer_pcm_dad1_enum =
	SOC_ENUM_SINGLE(AUDMIXER_REG_03_IN1_CTL3, INCTL3_PCM_DAD_SHIFT,
			ARRAY_SIZE(audmixer_pcm_dad_text),
			audmixer_pcm_dad_text);

static const struct soc_enum audmixer_pcm_dad2_enum =
	SOC_ENUM_SINGLE(AUDMIXER_REG_06_IN2_CTL3, INCTL3_PCM_DAD_SHIFT,
			ARRAY_SIZE(audmixer_pcm_dad_text),
			audmixer_pcm_dad_text);

static const struct soc_enum audmixer_pcm_dad3_enum =
	SOC_ENUM_SINGLE(AUDMIXER_REG_09_IN3_CTL3, INCTL3_PCM_DAD_SHIFT,
			ARRAY_SIZE(audmixer_pcm_dad_text),
			audmixer_pcm_dad_text);

/**
 * pcm_df
 *
 * PCM Data Format
 */
static const char *audmixer_pcm_df_text[] = {
	"", "", "", "", "Short Frame", "", "", "",
	"", "", "", "", "Long Frame"
};

static const struct soc_enum audmixer_pcm_df1_enum =
	SOC_ENUM_SINGLE(AUDMIXER_REG_03_IN1_CTL3, INCTL3_PCM_DF_SHIFT,
			ARRAY_SIZE(audmixer_pcm_df_text),
			audmixer_pcm_df_text);

static const struct soc_enum audmixer_pcm_df2_enum =
	SOC_ENUM_SINGLE(AUDMIXER_REG_06_IN2_CTL3, INCTL3_PCM_DF_SHIFT,
			ARRAY_SIZE(audmixer_pcm_df_text),
			audmixer_pcm_df_text);

static const struct soc_enum audmixer_pcm_df3_enum =
	SOC_ENUM_SINGLE(AUDMIXER_REG_09_IN3_CTL3, INCTL3_PCM_DF_SHIFT,
			ARRAY_SIZE(audmixer_pcm_df_text),
			audmixer_pcm_df_text);

/**
 * bck4_mode
 *
 * BCK4 Output Selection
 */
static const char *audmixer_bck4_mode_text[] = {
	"Normal BCK", "MCKO"
};

static const struct soc_enum audmixer_bck4_mode_enum =
	SOC_ENUM_SINGLE(AUDMIXER_REG_0A_HQ_CTL, HQ_CTL_BCK4_MODE_SHIFT,
			ARRAY_SIZE(audmixer_bck4_mode_text),
			audmixer_bck4_mode_text);

/**
 * dout_sel1
 *
 * CH1 Digital Output Selection
 */
static const char *audmixer_dout_sel1_text[] = {
	"DMIX_OUT", "AIF4IN", "RMIX_OUT"
};

static SOC_ENUM_SINGLE_DECL(audmixer_dout_sel1_enum, AUDMIXER_REG_16_DOUTMX1,
		DOUTMX1_DOUT_SEL1_SHIFT, audmixer_dout_sel1_text);

/**
 * dout_sel2
 *
 * CH2 Digital Output Selection
 */
static const char *audmixer_dout_sel2_text[] = {
	"DMIX_OUT", "AIF4IN", "AIF3IN"
};

static SOC_ENUM_SINGLE_DECL(audmixer_dout_sel2_enum, AUDMIXER_REG_16_DOUTMX1,
		DOUTMX1_DOUT_SEL2_SHIFT, audmixer_dout_sel2_text);

/**
 * dout_sel3
 *
 * CH3 Digital Output Selection
 */
static const char *audmixer_dout_sel3_text[] = {
	"DMIX_OUT", "AIF4IN", "AIF2IN"
};

static SOC_ENUM_SINGLE_DECL(audmixer_dout_sel3_enum, AUDMIXER_REG_17_DOUTMX2,
		DOUTMX2_DOUT_SEL3_SHIFT, audmixer_dout_sel3_text);

static const char *audmixer_off_on_text[] = {
	"Off", "On"
};

static const struct soc_enum audmixer_hq_en_enum =
SOC_ENUM_SINGLE(AUDMIXER_REG_0A_HQ_CTL, HQ_CTL_HQ_EN_SHIFT,
		ARRAY_SIZE(audmixer_off_on_text), audmixer_off_on_text);

static const struct soc_enum audmixer_ch3_rec_en_enum =
SOC_ENUM_SINGLE(AUDMIXER_REG_0A_HQ_CTL, HQ_CTL_CH3_SEL_SHIFT,
		ARRAY_SIZE(audmixer_off_on_text), audmixer_off_on_text);

static const struct soc_enum audmixer_mcko_en_enum =
SOC_ENUM_SINGLE(AUDMIXER_REG_0A_HQ_CTL, HQ_CTL_MCKO_EN_SHIFT,
		ARRAY_SIZE(audmixer_off_on_text), audmixer_off_on_text);

static const struct soc_enum audmixer_rmix1_en_enum =
SOC_ENUM_SINGLE(AUDMIXER_REG_0D_RMIX_CTL, RMIX_CTL_RMIX1_EN_SHIFT,
		ARRAY_SIZE(audmixer_off_on_text), audmixer_off_on_text);

static const struct soc_enum audmixer_rmix2_en_enum =
SOC_ENUM_SINGLE(AUDMIXER_REG_0D_RMIX_CTL, RMIX_CTL_RMIX2_EN_SHIFT,
		ARRAY_SIZE(audmixer_off_on_text), audmixer_off_on_text);

static const struct soc_enum mixer_ch1_enable_enum =
SOC_ENUM_SINGLE(AUDMIXER_REG_10_DMIX1, DMIX1_MIX_EN1_SHIFT,
		ARRAY_SIZE(audmixer_off_on_text), audmixer_off_on_text);
static const struct soc_enum mixer_ch2_enable_enum =
SOC_ENUM_SINGLE(AUDMIXER_REG_10_DMIX1, DMIX1_MIX_EN2_SHIFT,
		ARRAY_SIZE(audmixer_off_on_text), audmixer_off_on_text);
static const struct soc_enum mixer_ch3_enable_enum =
SOC_ENUM_SINGLE(AUDMIXER_REG_11_DMIX2, DMIX2_MIX_EN3_SHIFT,
		ARRAY_SIZE(audmixer_off_on_text), audmixer_off_on_text);
static const struct soc_enum mixer_ch4_enable_enum =
SOC_ENUM_SINGLE(AUDMIXER_REG_11_DMIX2, DMIX2_MIX_EN4_SHIFT,
		ARRAY_SIZE(audmixer_off_on_text), audmixer_off_on_text);

static const struct soc_enum mixer_enable_enum =
SOC_ENUM_SINGLE(AUDMIXER_REG_0F_DIG_EN, DIG_EN_MIX_EN_SHIFT,
		ARRAY_SIZE(audmixer_off_on_text), audmixer_off_on_text);
static const struct soc_enum src3_enable_enum =
SOC_ENUM_SINGLE(AUDMIXER_REG_0F_DIG_EN, DIG_EN_SRC3_EN_SHIFT,
		ARRAY_SIZE(audmixer_off_on_text), audmixer_off_on_text);
static const struct soc_enum src2_enable_enum =
SOC_ENUM_SINGLE(AUDMIXER_REG_0F_DIG_EN, DIG_EN_SRC2_EN_SHIFT,
		ARRAY_SIZE(audmixer_off_on_text), audmixer_off_on_text);
#ifdef CONFIG_SOC_EXYNOS7870
static const struct soc_enum ap0_enable_enum =
SOC_ENUM_SINGLE(AUDMIXER_REG_0F_DIG_EN, DIG_EN_AP0_EN_SHIFT,
		ARRAY_SIZE(audmixer_off_on_text), audmixer_off_on_text);
static const struct soc_enum ap1_enable_enum =
SOC_ENUM_SINGLE(AUDMIXER_REG_0F_DIG_EN, DIG_EN_AP1_EN_SHIFT,
		ARRAY_SIZE(audmixer_off_on_text), audmixer_off_on_text);
static const struct soc_enum cp1_enable_enum =
SOC_ENUM_SINGLE(AUDMIXER_REG_0F_DIG_EN, DIG_EN_CP1_EN_SHIFT,
		ARRAY_SIZE(audmixer_off_on_text), audmixer_off_on_text);
#else
static const struct soc_enum src1_enable_enum =
SOC_ENUM_SINGLE(AUDMIXER_REG_0F_DIG_EN, DIG_EN_SRC1_EN_SHIFT,
		ARRAY_SIZE(audmixer_off_on_text), audmixer_off_on_text);
#endif
static const struct soc_enum alc_enable_enum =
SOC_ENUM_SINGLE(AUDMIXER_REG_68_ALC_CTL, ALC_CTL_ALC_EN_SHIFT,
		ARRAY_SIZE(audmixer_off_on_text), audmixer_off_on_text);

static const struct soc_enum audmixer_alc_enable_enum =
SOC_ENUM_SINGLE(AUDMIXER_REG_68_ALC_CTL, ALC_CTL_ALC_EN_SHIFT,
		ARRAY_SIZE(audmixer_off_on_text), audmixer_off_on_text);

static const struct soc_enum audmixer_alc_limiter_mode_enum =
SOC_ENUM_SINGLE(AUDMIXER_REG_68_ALC_CTL, ALC_CTL_ALC_LIM_SHIFT,
		ARRAY_SIZE(audmixer_off_on_text), audmixer_off_on_text);

static const struct soc_enum audmixer_alc_start_gain_en_enum =
SOC_ENUM_SINGLE(AUDMIXER_REG_6D_ALC_HLD, ALC_HLD_ST_GAIN_EN_SHIFT,
		ARRAY_SIZE(audmixer_off_on_text), audmixer_off_on_text);

static const struct soc_enum audmixer_noise_gate_enable_enum =
SOC_ENUM_SINGLE(AUDMIXER_REG_70_ALC_NG, ALC_NG_NGAT_SHIFT,
		ARRAY_SIZE(audmixer_off_on_text), audmixer_off_on_text);

/*
 * audmixer_soc_kcontrol_handler: Provide runtime PM handling during the get/put
 * event handler of a specific control
 *
 * Since we don't have regcache support for this device, the get/put events will
 * access the hardware everytime the user-space accesses the device controls. If
 * the audio block is in suspend state, this might will result in OOPs because
 * the power-domain/clocks might be in off state. The best approach would be to
 * enable the device, execute the get/put function and then put the device in
 * suspended state. This function provides helpers for this purpose.
 *
 * Arguments
 * kcontrol/ucontrol: Passed to the framework API
 * func: The default framework API used to manage this event.
 */
static int audmixer_soc_kcontrol_handler(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol,
		int (*func)(struct snd_kcontrol *, struct snd_ctl_elem_value *))
{
	return func(kcontrol, ucontrol);
}

/* Function to get TLV control value */
static int audmixer_snd_soc_get_volsw(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	return audmixer_soc_kcontrol_handler(kcontrol, ucontrol,
			snd_soc_get_volsw);
}

/* Function to set TLV control value */
static int audmixer_snd_soc_put_volsw(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	return audmixer_soc_kcontrol_handler(kcontrol, ucontrol,
			snd_soc_put_volsw);
}

/* Function to get TLV control value */
static int audmixer_snd_soc_get_volsw_range(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	return audmixer_soc_kcontrol_handler(kcontrol, ucontrol,
			snd_soc_get_volsw_range);
}

/* Function to set TLV control value */
static int audmixer_snd_soc_put_volsw_range(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	return audmixer_soc_kcontrol_handler(kcontrol, ucontrol,
			snd_soc_put_volsw_range);
}

/* Function to get ENUM control value */
static int audmixer_soc_enum_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	return audmixer_soc_kcontrol_handler(kcontrol, ucontrol,
			snd_soc_get_enum_double);
}

/* Function to set ENUM control value */
static int audmixer_soc_enum_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	return audmixer_soc_kcontrol_handler(kcontrol, ucontrol,
			snd_soc_put_enum_double);
}

/**
 * struct snd_kcontrol_new audmixer_snd_controls
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
static const struct snd_kcontrol_new audmixer_snd_controls[] = {
	SOC_SINGLE_EXT_TLV("RMIX1_LVL", AUDMIXER_REG_0D_RMIX_CTL,
			RMIX_CTL_RMIX2_LVL_SHIFT,
			BIT(RMIX_CTL_RMIX2_LVL_WIDTH) - 1, 0,
			audmixer_snd_soc_get_volsw, audmixer_snd_soc_put_volsw,
			audmixer_mix_tlv),

	SOC_SINGLE_EXT_TLV("RMIX2_LVL", AUDMIXER_REG_0D_RMIX_CTL,
			RMIX_CTL_RMIX1_LVL_SHIFT,
			BIT(RMIX_CTL_RMIX1_LVL_WIDTH) - 1, 0,
			audmixer_snd_soc_get_volsw, audmixer_snd_soc_put_volsw,
			audmixer_mix_tlv),

	SOC_SINGLE_EXT_TLV("MIX1_LVL", AUDMIXER_REG_10_DMIX1,
			DMIX1_MIX_LVL1_SHIFT,
			BIT(DMIX1_MIX_LVL1_WIDTH) - 1, 0,
			audmixer_snd_soc_get_volsw, audmixer_snd_soc_put_volsw,
			audmixer_mix_tlv),

	SOC_SINGLE_EXT_TLV("MIX2_LVL", AUDMIXER_REG_10_DMIX1,
			DMIX1_MIX_LVL2_SHIFT,
			BIT(DMIX1_MIX_LVL2_WIDTH) - 1, 0,
			audmixer_snd_soc_get_volsw, audmixer_snd_soc_put_volsw,
			audmixer_mix_tlv),

	SOC_SINGLE_EXT_TLV("MIX3_LVL", AUDMIXER_REG_11_DMIX2,
			DMIX2_MIX_LVL3_SHIFT,
			BIT(DMIX2_MIX_LVL3_WIDTH) - 1, 0,
			audmixer_snd_soc_get_volsw, audmixer_snd_soc_put_volsw,
			audmixer_mix_tlv),

	SOC_SINGLE_EXT_TLV("MIX4_LVL", AUDMIXER_REG_11_DMIX2,
			DMIX2_MIX_LVL4_SHIFT,
			BIT(DMIX2_MIX_LVL4_WIDTH) - 1, 0,
			audmixer_snd_soc_get_volsw, audmixer_snd_soc_put_volsw,
			audmixer_mix_tlv),

	SOC_SINGLE_EXT_TLV("ALC NG HYS", AUDMIXER_REG_68_ALC_CTL,
			ALC_CTL_ALC_NG_HYS_SHIFT,
			BIT(ALC_CTL_ALC_NG_HYS_WIDTH) - 1, 0,
			audmixer_snd_soc_get_volsw, audmixer_snd_soc_put_volsw,
			audmixer_alc_ng_hys_tlv),

	SOC_SINGLE_RANGE_EXT_TLV("ALC Max Gain", AUDMIXER_REG_69_ALC_GA1,
			ALC_GA1_ALC_MAX_GAIN_SHIFT,
			ALC_GA1_ALC_MAX_GAIN_MINVAL,
			ALC_GA1_ALC_MAX_GAIN_MAXVAL, 0,
			audmixer_snd_soc_get_volsw_range,
			audmixer_snd_soc_put_volsw_range,
			audmixer_alc_max_gain_tlv),

	SOC_SINGLE_RANGE_EXT_TLV("ALC Min Gain", AUDMIXER_REG_6A_ALC_GA2,
			ALC_GA2_ALC_MIN_GAIN_SHIFT,
			ALC_GA2_ALC_MIN_GAIN_MINVAL,
			ALC_GA2_ALC_MIN_GAIN_MAXVAL, 0,
			audmixer_snd_soc_get_volsw_range,
			audmixer_snd_soc_put_volsw_range,
			audmixer_alc_min_gain_tlv),

	SOC_SINGLE_RANGE_EXT_TLV("ALC Start Gain Left", AUDMIXER_REG_71_ALC_SGL,
			ALC_SGL_START_GAIN_L_SHIFT,
			ALC_SGL_START_GAIN_L_MINVAL,
			ALC_SGL_START_GAIN_L_MAXVAL, 0,
			audmixer_snd_soc_get_volsw_range,
			audmixer_snd_soc_put_volsw_range,
			audmixer_alc_max_gain_tlv),

	SOC_SINGLE_RANGE_EXT_TLV("ALC Start Gain Right", AUDMIXER_REG_72_ALC_SGR,
			ALC_SGR_START_GAIN_R_SHIFT,
			ALC_SGR_START_GAIN_R_MINVAL,
			ALC_SGR_START_GAIN_R_MAXVAL, 0,
			audmixer_snd_soc_get_volsw_range,
			audmixer_snd_soc_put_volsw_range,
			audmixer_alc_max_gain_tlv),

	SOC_SINGLE_EXT_TLV("ALC LVL Left", AUDMIXER_REG_6B_ALC_LVL,
			ALC_LVL_LVL_SHIFT,
			BIT(ALC_LVL_LVL_WIDTH) - 1, 0,
			audmixer_snd_soc_get_volsw, audmixer_snd_soc_put_volsw,
			audmixer_alc_lvl_tlv),

	SOC_SINGLE_EXT_TLV("ALC LVL Right", AUDMIXER_REG_6C_ALC_LVR,
			ALC_LVR_LVL_SHIFT,
			BIT(ALC_LVR_LVL_WIDTH) - 1, 0,
			audmixer_snd_soc_get_volsw, audmixer_snd_soc_put_volsw,
			audmixer_alc_lvl_tlv),

	SOC_SINGLE_EXT_TLV("ALC Noise Gate Threshold", AUDMIXER_REG_70_ALC_NG,
			ALC_NG_ALCNGTH_SHIFT,
			BIT(ALC_NG_ALCNGTH_WIDTH) - 1, 0,
			audmixer_snd_soc_get_volsw, audmixer_snd_soc_put_volsw,
			audmixer_alc_ng_th_tlv),

	SOC_ENUM_EXT("CH1 Master PCM Sample Rate", audmixer_mpcm_srate1_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
	SOC_ENUM_EXT("CH2 Master PCM Sample Rate", audmixer_mpcm_srate2_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
	SOC_ENUM_EXT("CH3 Master PCM Sample Rate", audmixer_mpcm_srate3_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),

	SOC_ENUM_EXT("CH1 Master PCM Slot", audmixer_mpcm_slot1_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
	SOC_ENUM_EXT("CH2 Master PCM Slot", audmixer_mpcm_slot2_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
	SOC_ENUM_EXT("CH3 Master PCM Slot", audmixer_mpcm_slot3_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),

	SOC_ENUM_EXT("CH1 BCLK Polarity", audmixer_bck_pol1_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
	SOC_ENUM_EXT("CH2 BCLK Polarity", audmixer_bck_pol2_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
	SOC_ENUM_EXT("CH3 BCLK Polarity", audmixer_bck_pol3_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),

	SOC_ENUM_EXT("CH1 LRCLK Polarity", audmixer_lrck_pol1_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
	SOC_ENUM_EXT("CH2 LRCLK Polarity", audmixer_lrck_pol2_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
	SOC_ENUM_EXT("CH3 LRCLK Polarity", audmixer_lrck_pol3_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),

	SOC_ENUM_EXT("CH1 Input Audio Mode", audmixer_i2s_pcm1_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
	SOC_ENUM_EXT("CH2 Input Audio Mode", audmixer_i2s_pcm2_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
	SOC_ENUM_EXT("CH3 Input Audio Mode", audmixer_i2s_pcm3_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),

	SOC_ENUM_EXT("CH1 XFS", audmixer_i2s_xfs1_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
	SOC_ENUM_EXT("CH2 XFS", audmixer_i2s_xfs2_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
	SOC_ENUM_EXT("CH3 XFS", audmixer_i2s_xfs3_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),

	SOC_ENUM_EXT("CH1 I2S Format", audmixer_i2s_df1_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
	SOC_ENUM_EXT("CH2 I2S Format", audmixer_i2s_df2_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
	SOC_ENUM_EXT("CH3 I2S Format", audmixer_i2s_df3_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),

	SOC_ENUM_EXT("CH1 I2S Data Length", audmixer_i2s_dl1_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
	SOC_ENUM_EXT("CH2 I2S Data Length", audmixer_i2s_dl2_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
	SOC_ENUM_EXT("CH3 I2S Data Length", audmixer_i2s_dl3_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),

	SOC_ENUM_EXT("CH1 PCM DAD", audmixer_pcm_dad1_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
	SOC_ENUM_EXT("CH2 PCM DAD", audmixer_pcm_dad2_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
	SOC_ENUM_EXT("CH3 PCM DAD", audmixer_pcm_dad3_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),

	SOC_ENUM_EXT("CH1 PCM Data Format", audmixer_pcm_df1_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
	SOC_ENUM_EXT("CH2 PCM Data Format", audmixer_pcm_df2_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
	SOC_ENUM_EXT("CH3 PCM Data Format", audmixer_pcm_df3_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),

	SOC_ENUM_EXT("CH3 Rec En", audmixer_ch3_rec_en_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
	SOC_ENUM_EXT("MCKO En", audmixer_mcko_en_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),

	SOC_ENUM_EXT("RMIX1 En", audmixer_rmix1_en_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
	SOC_ENUM_EXT("RMIX2 En", audmixer_rmix2_en_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),

	SOC_ENUM_EXT("BCK4 Output Selection", audmixer_bck4_mode_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),

	SOC_ENUM_EXT("HQ En", audmixer_hq_en_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),

	SOC_ENUM_EXT("CH1 DOUT Select", audmixer_dout_sel1_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
	SOC_ENUM_EXT("CH2 DOUT Select", audmixer_dout_sel2_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
	SOC_ENUM_EXT("CH3 DOUT Select", audmixer_dout_sel3_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),

	SOC_ENUM_EXT("CH1 Mixer En", mixer_ch1_enable_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
	SOC_ENUM_EXT("CH2 Mixer En", mixer_ch2_enable_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
	SOC_ENUM_EXT("CH3 Mixer En", mixer_ch3_enable_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
	SOC_ENUM_EXT("CH4 Mixer En", mixer_ch4_enable_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
	SOC_ENUM_EXT("Mixer En", mixer_enable_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
#ifdef CONFIG_SOC_EXYNOS7870
	SOC_ENUM_EXT("AP0 En", ap0_enable_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
	SOC_ENUM_EXT("AP1 En", ap1_enable_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
	SOC_ENUM_EXT("CP1 En", cp1_enable_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
#else
	SOC_ENUM_EXT("SRC1 En", src1_enable_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
#endif
	SOC_ENUM_EXT("SRC2 En", src2_enable_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
	SOC_ENUM_EXT("SRC3 En", src3_enable_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),

	SOC_ENUM_EXT("ALC Window Length", audmixer_alc_winsel_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
	SOC_ENUM_EXT("ALC Mode", audmixer_alc_mode_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),

	SOC_ENUM_EXT("ALC En", audmixer_alc_enable_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
	SOC_ENUM_EXT("ALC Limiter Mode En", audmixer_alc_limiter_mode_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
	SOC_ENUM_EXT("ALC Start Gain En", audmixer_alc_start_gain_en_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),

	SOC_SINGLE_EXT("ALC Hold Time", AUDMIXER_REG_6D_ALC_HLD,
			ALC_HLD_HOLD_SHIFT, ALC_HLD_HOLD_MASK, 0,
			audmixer_snd_soc_get_volsw, audmixer_snd_soc_put_volsw),

	SOC_SINGLE_EXT("ALC Attack Time", AUDMIXER_REG_6E_ALC_ATK,
			ALC_ATK_ATTACK_SHIFT, ALC_ATK_ATTACK_MASK, 0,
			audmixer_snd_soc_get_volsw, audmixer_snd_soc_put_volsw),

	SOC_SINGLE_EXT("ALC Decay Time", AUDMIXER_REG_6F_ALC_DCY,
			ALC_DCY_DECAY_SHIFT, ALC_DCY_DECAY_MASK, 0,
			audmixer_snd_soc_get_volsw, audmixer_snd_soc_put_volsw),

	SOC_ENUM_EXT("ALC Path", audmixer_alc_path_sel_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
	SOC_ENUM_EXT("ALC Noise Gate En", audmixer_noise_gate_enable_enum,
		audmixer_soc_enum_get, audmixer_soc_enum_put),
};

/**
 * audmixer_get_clk: Get clock references
 *
 * This function gets all the clock related to the audio i2smixer and stores in
 * mixer private structure.
 */
static int audmixer_get_clk(struct device *dev)
{

	g_audmixer->clk_dout = devm_clk_get(dev, "audmixer_dout");
	if (IS_ERR(g_audmixer->clk_dout)) {
		dev_err(dev, "audmixer_dout clk not found\n");
		return PTR_ERR(g_audmixer->clk_dout);
	}

	if (g_audmixer->hw->bus == AUDMIXER_APB) {
		g_audmixer->aclk = devm_clk_get(dev, "audmixer_aclk");
		if (IS_ERR(g_audmixer->aclk)) {
			dev_err(dev, "audmixer_aclk clk not found\n");
			return PTR_ERR(g_audmixer->aclk);
		}
	}

	return 0;
}

/**
 * audmixer_clk_put: Reset clock references
 *
 * This function puts all the clock related to the audio i2smixer
 */
static void audmixer_clk_put(struct device *dev)
{
	clk_put(g_audmixer->clk_dout);
	if (g_audmixer->hw->bus == AUDMIXER_APB) {
		clk_put(g_audmixer->aclk);
	}
}

/**
 * audmixer_clk_enable: Enables all audio mixer related clocks
 *
 * This function enables all the clock related to the audio i2smixer
 */
static void audmixer_clk_enable(struct device *dev)
{
	/* TODO: Check if aclk can be enabled here */
	clk_prepare_enable(g_audmixer->aclk);
	clk_prepare_enable(g_audmixer->clk_dout);
}

/**
 * audmixer_clk_disable: Disables all audio mixer related clocks
 *
 * This function disable all the clock related to the audio i2smixer
 */
static void audmixer_clk_disable(struct device *dev)
{
	clk_disable_unprepare(g_audmixer->clk_dout);
	clk_disable_unprepare(g_audmixer->aclk);
	/* TODO: Check if aclk can be disabled here */
}

/*
 * audmixer_update_soc_reg: Update SoC specific registers
 *
 * Audio Mixer requires a few SoC specific registers to be updated during its
 * operations.
 * PMU_ALIVE: To enable PMU specific bit for the Audio Mixer
 * SYS_RESET: The sysreg register to reset the Audio Mixer block
 *
 * @socreg: The structure holding information about the SoC specific register,
 * target bit and whether the bit should be 1 or 0 during Audio Mixer operation
 * @enable: When the Audio Mixer is getting enabled or disabled
 */
static void audmixer_update_soc_reg(struct audmixer_soc_reg_info *socreg,
		bool enable)
{
	/*
	 * If socreg->active_high is true, the specific bit should be set while
	 * enabling and cleared while disabling.
	 * If socreg->active_high is false, the specific bit should be cleared
	 * while enabling and set while disabling.
	 */
	bool set_val = socreg->active_high ? enable : !enable;
	unsigned int val;

	if (socreg->reg) {
		val = readl(socreg->reg);
		if (set_val)
			val |= BIT(socreg->bit);
		else
			val &= ~BIT(socreg->bit);
		writel(val, socreg->reg);
	}
}
#ifdef CONFIG_PM_RUNTIME
/**
 * audmixer_power_on: SoC specific powering on configurations
 *
 * This functions sets sysreg regsiters to enable Audio Mixer.
 */

static int audmixer_power_on(struct device *dev)
{

	/* Audio mixer PMU ALIVE configuration */
	audmixer_update_soc_reg(&g_audmixer->reg_alive, true);

	/* Audio mixer unreset */
	audmixer_update_soc_reg(&g_audmixer->reg_reset, true);

	if (g_audmixer->hw->bus == AUDMIXER_I2C) {
		/*write Audio mixer i2c address */
		if (g_audmixer->sysreg_i2c_id == NULL) {
			dev_err(dev, "sysreg_i2c_id registers not set\n");
			return -ENXIO;
		}
		writel(g_audmixer->i2c_addr, g_audmixer->sysreg_i2c_id);
	}

	return 0;
}
#endif

/**
 * audmixer_power_off: SoC specific power off configuration
 *
 * This functions resets sysreg regsiters to disable Audio Mixer.
 */
static void audmixer_power_off(struct device *dev)
{

	/* Audio mixer unreset */
	audmixer_update_soc_reg(&g_audmixer->reg_reset, false);

	/* Audio mixer PMU ALIVE configuration */
	audmixer_update_soc_reg(&g_audmixer->reg_alive, false);
}

#define AUDMIXER_RATES SNDRV_PCM_RATE_8000_192000
#define AUDMIXER_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE)

static struct snd_soc_dai_driver audmixer_dais[] = {{
		.name = "AP0",
		.id = AUDMIXER_IF_AP,
		.ops = &audmixer_dai_ops,
		.playback = {
			.stream_name = "AP0 Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rate_min = 48000,
			.rate_max = 192000,
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_192000,
			.formats = AUDMIXER_FORMATS,
		},
		.capture = {
			.stream_name = "AP0 Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rate_min = 48000,
			.rate_max = 192000,
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_192000,
			.formats = AUDMIXER_FORMATS,
		},
		.symmetric_rates = 1,
		.symmetric_channels = 1,
		.symmetric_samplebits = 1,
	},{
		.name = "CP0",
		.id = AUDMIXER_IF_CP,
		.ops = &audmixer_dai_ops,
		.playback = {
			.stream_name = "CP0 Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 48000,
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_48000,
			.formats = AUDMIXER_FORMATS,
		},
		.capture = {
			.stream_name = "CP0 Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 48000,
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_48000,
			.formats = AUDMIXER_FORMATS,
		},
		.symmetric_channels = 1,
		.symmetric_samplebits = 1,
	},{
		.name = "BT",
		.id = AUDMIXER_IF_BT,
		.ops = &audmixer_dai_ops,
		.playback = {
			.stream_name = "BT Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 48000,
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
					SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_KNOT,
			.formats = AUDMIXER_FORMATS,
		},
		.capture = {
			.stream_name = "BT Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 48000,
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
					SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_KNOT,
			.formats = AUDMIXER_FORMATS,
		},
		.symmetric_rates = 1,
		.symmetric_channels = 1,
		.symmetric_samplebits = 1,
	},{
		.name = "FM",
		.id = AUDMIXER_IF_FM,
		.ops = &audmixer_dai_ops,
		.playback = {
			.stream_name = "FM Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 48000,
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
					SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_KNOT,
			.formats = AUDMIXER_FORMATS,
		},
		.capture = {
			.stream_name = "FM Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 48000,
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
					SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_KNOT,
			.formats = AUDMIXER_FORMATS,
		},
		.symmetric_rates = 1,
		.symmetric_channels = 1,
		.symmetric_samplebits = 1,
	},{
		.name = "AMP",
		.id = AUDMIXER_IF_ADC,
		.capture = {
			.stream_name = "AMP Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rate_min = 48000,
			.rate_max = 48000,
			.rates = SNDRV_PCM_RATE_48000,
			.formats = AUDMIXER_FORMATS,
		},
	},{
		.name = "AP1",
		.id = AUDMIXER_IF_AP1,
		.ops = &audmixer_dai_ops,
		.capture = {
			.stream_name = "AP1 Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rate_min = 48000,
			.rate_max = 48000,
			.rates = SNDRV_PCM_RATE_48000,
			.formats = AUDMIXER_FORMATS,
		},
	},{
		.name = "CP1",
		.id = AUDMIXER_IF_CP1,
		.ops = &audmixer_dai_ops,
		.capture = {
			.stream_name = "CP1 Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 16000,
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000,
			.formats = AUDMIXER_FORMATS,
		},
	},
};

/*
 * post_update_fw: Configurations after f/w update operations
 *
 * Audio Mixer driver supports updating its tuning registers through firmware.
 * After updating the tuning registers, this function is called to reinitilize
 * some of the configuration registers which might have been updated by the
 * firmware. This function is also called when the firmware could not be found
 * to set default values.
 */
static void post_update_fw(void *context)
{
	struct snd_soc_codec *codec = (struct snd_soc_codec *)context;

	dev_dbg(codec->dev, "(*) %s\n", __func__);

	pm_runtime_get_sync(codec->dev);

	/* set ap path by defaut*/
	audmixer_init_mixer();

	pm_runtime_put_sync(codec->dev);
}

/*
 * audmixer_drvdata_init: Initialize device specific private data structure
 */
static void audmixer_drvdata_init(void)
{
	int i;
	g_audmixer->is_alc_enabled = false;
	g_audmixer->aifrate = 0;

	g_audmixer->is_active = false;
	g_audmixer->is_regs_stored = false;
	atomic_set(&g_audmixer->num_active_stream, 0);
	for (i = 0; i < AUDMIXER_IF_COUNT; i++)
		atomic_set(&g_audmixer->use_count[i], 0);
}


/**
 * audmixer_parse_dt: Parse DT properties and update private data structure
 *
 * All of following properties are optional.
 * reg-alive: Details about PMU ALIVE register
 * reg-reset: Details about SYSREG RESET register
 * sysreg-i2c: Register offset where I2C ID needs to be updated (for SRC2801X)
 * samsung,lpass-subip: To bind this IP to LPASS
 * bck-mcko-mode: To provide codec MCLK in AIF4 bclk line
 * update-firmware: To enable update of tuning register through firmware
 */
static int audmixer_parse_dt(struct device *dev)
{
	int ret;
	unsigned int sysreg;
	unsigned int res[3];
	const struct of_device_id *match;

	match = of_match_device(audmixer_i2c_dt_ids, dev);
	if (!match)
		match = of_match_device(audmixer_apb_dt_ids, dev);
	if (!match){
		dev_err(dev, "Device not found in device dt id list \n");
		return -ENODEV;
	}
	g_audmixer->hw = match->data;

	/* 'pmureg-alive' specifies the ALIVE register for Audio Mixer
	 * IP block. If not specified, it is taken care by LPASS block.
	 */
	ret = of_property_read_u32_array(dev->of_node, "reg-alive", res, 3);
	if (ret) {
		dev_info(dev, "Property 'reg-alive' not found\n");
		g_audmixer->reg_alive.reg = NULL;
	} else {
		g_audmixer->reg_alive.reg = devm_ioremap(dev, res[0], 0x4);
		if (g_audmixer->reg_alive.reg == NULL) {
			dev_err(dev, "Cannot ioremap %x\n", res[0]);
			return -ENXIO;
		}
		g_audmixer->reg_alive.bit = res[1];
		g_audmixer->reg_alive.active_high = !!res[2];
	}

	/* 'sysreg-reset' specifies the reset register for Audio Mixer
	 * IP block. If not specified, the reset is taken care by LPASS block.
	 */
	ret = of_property_read_u32_array(dev->of_node, "reg-reset", res, 3);
	if (ret) {
		dev_info(dev, "Property 'reg-reset' not found\n");
		g_audmixer->reg_reset.reg = NULL;
	} else {
		g_audmixer->reg_reset.reg = devm_ioremap(dev, res[0], 0x4);
		if (g_audmixer->reg_reset.reg == NULL) {
			dev_err(dev, "Cannot ioremap %x\n", res[0]);
			return -ENXIO;
		}
		g_audmixer->reg_reset.bit = res[1];
		g_audmixer->reg_reset.active_high = !!res[2];
	}

	/* 'sysreg-i2c' specifies the system register on which the I2C slave
	 * address of the Audio Mixer needs to be updated. This register is
	 * valid only for SRC2801X.
	 */
	if (g_audmixer->hw->bus == AUDMIXER_I2C) {
		ret = of_property_read_u32(dev->of_node,
				"sysreg-i2c", &sysreg);
		if (ret) {
			dev_err(dev, "Property 'sysreg-i2c' not found\n");
			return -ENOENT;
		}

		g_audmixer->sysreg_i2c_id = devm_ioremap(dev, sysreg, 0x4);
		if (g_audmixer->sysreg_i2c_id == NULL) {
			dev_err(dev, "Cannot ioremap %x\n", sysreg);
			return -ENXIO;
		}
	}

	/* 'samsung,lpass-subip' specifies binding with LPASS subsystem */
	if (of_find_property(g_audmixer->dev->of_node,
				"samsung,lpass-subip", NULL))
		lpass_register_subip(g_audmixer->dev, "audiomixer");

	/* 'bck-mcko-mode' specifies that MCLK should be provided in BCK line */
	if (of_find_property(g_audmixer->dev->of_node,
				"bck-mcko-mode", NULL))
		g_audmixer->is_bck4_mcko_enabled = true;
	else
		g_audmixer->is_bck4_mcko_enabled = false;

	/* 'update-firmware' specifies that tuning values should be updated
	 * through fimrware binary
	 */
	if (of_find_property(g_audmixer->dev->of_node,
				"update-firmware", NULL))
		g_audmixer->update_fw = true;
	else
		g_audmixer->update_fw = false;

	if (of_find_property(g_audmixer->dev->of_node, "bt-fm-combo", NULL))
		g_audmixer->is_bt_fm_combo= true;
	else
		g_audmixer->is_bt_fm_combo = false;

	return 0;
}

/* audmixer_initialize_regs: Initialize a list of registers during boot time
 *
 * When user-space tries to read the value of some controls, the values are read
 * from the regcache. If the regcache doesn't have a copy of the particualr
 * register, the value is read from the hardware.
 *
 * In case the device is suspended and cache-only mode is set, regmap read
 * returns an error code and the user-space is not able to read the hardware
 * value. This results in some contols not getting updated at the boot time.
 *
 * To fix this issue, it is ensured that the registers for which user-space
 * controls are defined are initialized at the boot time. This in turn updates
 * the regcache and there is no read error during control operation.
 */
static void audmixer_initialize_regs(void)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(audmixer_init_reg_list); i++)
		regmap_write(g_audmixer->regmap,
				audmixer_init_reg_list[i].reg,
				audmixer_init_reg_list[i].def);
}

/*
 * audmixer_probe: probe function for ALSA codec driver
 *
 * It does following.
 * 1. Parses the DT properties and updates device specific private data
 * structure
 * 2. Sets codec cache IO
 * 3. Gets pin-control handle (to be used to configure different pin-control
 * states)
 * 4. Gets clock handles
 * 5. Enables the codec, updates firmware if required, configures mixer as per
 * initial settings and then disables the codec.
 *
 * Returns 0 on success, otherwise an error code
 */
static int audmixer_probe(struct snd_soc_codec *codec)
{
	int ret = 0;

	dev_dbg(codec->dev, "(*) %s\n", __func__);

	codec->dev = g_audmixer->dev;
	codec->control_data = g_audmixer->regmap;

	g_audmixer->codec = codec;

	/* Should we move this to bus-probe */
	ret = audmixer_parse_dt(codec->dev);
	if (ret) {
		dev_err(codec->dev, "Failed to parse Device-Tree\n");
		return ret;
	}

	/* Get pin-control handle */
	g_audmixer->pinctrl = devm_pinctrl_get(codec->dev);
	if (IS_ERR(g_audmixer->pinctrl)) {
		dev_err(codec->dev, "Couldn't get pins (%li)\n",
				PTR_ERR(g_audmixer->pinctrl));
		return PTR_ERR(g_audmixer->pinctrl);
	}

	/* Get Clock for Mixer */
	ret = audmixer_get_clk(codec->dev);
	if (ret) {
		dev_err(codec->dev, "Failed to get clk for mixer\n");
		return ret;
	}

#ifndef CONFIG_PM_RUNTIME
	audmixer_clk_enable(codec->dev);
#endif

	audmixer_cfg_gpio(codec->dev, "default");
	/* Set auto-suspend delay and enable RPM support */
	pm_runtime_enable(g_audmixer->dev);
	pm_runtime_use_autosuspend(g_audmixer->dev);
	pm_runtime_set_autosuspend_delay(g_audmixer->dev,
				AUDMIXER_RPM_SUSPEND_DELAY_MS);
	pm_runtime_get_sync(g_audmixer->dev);

	/* Find out whether we need the firmware update code for audio mixer as
	 * it is not used in any of the products
	 */
#ifdef SND_SOC_REGMAP_FIRMWARE
	if (g_audmixer->update_fw)
		exynos_regmap_update_fw(S2803X_FIRMWARE_NAME,
			codec->dev, g_audmixer->regmap, g_audmixer->i2c_addr,
			post_update_fw, codec, post_update_fw, codec);

	else
#endif
		post_update_fw(codec);

	/* Initialize work queue for cp notification handling */
	INIT_WORK(&g_audmixer->cp_notification_work, audmixer_cp_notification_work);

	g_audmixer->mixer_cp_wq = create_singlethread_workqueue("mixer-cp-wq");
	if (g_audmixer->mixer_cp_wq == NULL) {
		dev_err(codec->dev, "Failed to create mixer-cp-wq\n");
		return -ENOMEM;
	}

	register_modem_event_notifier(&audmixer_cp_nb);

	/* Update the default value of registers that are accessible from
	 * user-space, as the regcache needs to have a copy of those registers
	 * before they are read for the first time
	 */
	if (g_audmixer->hw->bus != AUDMIXER_APB)
		audmixer_initialize_regs();

	pm_runtime_put_sync(g_audmixer->dev);

	return 0;
}

/*
 * audmixer_remove: ALSA code driver removal settings
 */
static int audmixer_remove(struct snd_soc_codec *codec)
{
	dev_dbg(codec->dev, "(*) %s\n", __func__);

	audmixer_clk_disable(codec->dev);
	audmixer_clk_put(codec->dev);
	audmixer_power_off(codec->dev);

	return 0;
}

/* Codec driver structure */
static struct snd_soc_codec_driver soc_codec_dev_audmixer = {
	.probe = audmixer_probe,
	.remove = audmixer_remove,
	.controls = audmixer_snd_controls,
	.num_controls = ARRAY_SIZE(audmixer_snd_controls),
	.idle_bias_off = true,
	.ignore_pmdown_time = true,
};

/*
 * audmixer_allocate_drvdata: Called from bus probe function to allocation of
 * private data structure
 */
static int audmixer_allocate_drvdata(struct device *dev)
{
	g_audmixer = devm_kzalloc(dev, sizeof(struct audmixer_priv),
			GFP_KERNEL);
	if (g_audmixer == NULL) {
		dev_err(dev, "Error allocating driver private data\n");
		return -ENOMEM;
	}

	g_audmixer->dev = dev;
	audmixer_drvdata_init();
	return 0;
}

/*
 * audmixer_apb_probe: Probe function for APB bus based Audio Mixer
 *
 * It does following.
 * 1. Allocates private data structure and links that to device structure.
 * 2. Prepares SFR base address.
 * 3. Prepares regmap handle.
 * 4. Registers codec driver
 *
 * Returns 0 on success, otherwise an error code.
 */
static int audmixer_apb_probe(struct platform_device *pdev)
{
	int ret;
	struct resource *res;

	dev_dbg(&pdev->dev, "(*) %s\n", __func__);

	ret = audmixer_allocate_drvdata(&pdev->dev);
	if (ret)
		return ret;
	dev_set_drvdata(&pdev->dev, g_audmixer);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Unable to get SFR base addr\n");
		return -ENXIO;
	}

	g_audmixer->regs = devm_ioremap_resource(&pdev->dev, res);
	if (!g_audmixer->regs) {
		dev_err(&pdev->dev, "SFR ioremap failed\n");
		return -ENOMEM;
	}

	g_audmixer->regmap = devm_regmap_init_mmio_clk(&pdev->dev,
			"audmixer_aclk", g_audmixer->regs,
			&audmixer_regmap_mmio);
	if (IS_ERR(g_audmixer->regmap)) {
		ret = PTR_ERR(g_audmixer->regmap);
		dev_err(&pdev->dev, "Failed to allocate regmap: %d\n", ret);
		return ret;
	}

	ret = snd_soc_register_codec(&pdev->dev,
			&soc_codec_dev_audmixer, audmixer_dais, ARRAY_SIZE(audmixer_dais));
	if (ret < 0)
		dev_err(&pdev->dev, "Failed to register codec: %d\n", ret);

	return ret;
}

/* audmixer_apb_remove: APB bus specific removal sequence */
static int audmixer_apb_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

/*
 * audmixer_i2c_probe: Probe function for I2C bus based Audio Mixer
 *
 * It does following.
 * 1. Allocates private data structure and links that to device structure.
 * 3. Prepares regmap handle.
 * 4. Registers codec driver
 *
 * Returns 0 on success, otherwise an error code.
 */
static int audmixer_i2c_probe(struct i2c_client *i2c,
				 const struct i2c_device_id *id)
{
	int ret;

	dev_dbg(&i2c->dev, "(*) %s\n", __func__);

	ret = audmixer_allocate_drvdata(&i2c->dev);
	if (ret)
		return ret;

	g_audmixer->i2c_addr = i2c->addr;
	i2c_set_clientdata(i2c, g_audmixer);

	g_audmixer->regmap = devm_regmap_init_i2c(i2c, &audmixer_regmap_i2c);
	if (IS_ERR(g_audmixer->regmap)) {
		ret = PTR_ERR(g_audmixer->regmap);
		dev_err(&i2c->dev, "Failed to allocate regmap: %d\n", ret);
		return ret;
	}

	ret = snd_soc_register_codec(&i2c->dev,
			&soc_codec_dev_audmixer, audmixer_dais, ARRAY_SIZE(audmixer_dais));
	if (ret < 0)
		dev_err(&i2c->dev, "Failed to register codec: %d\n", ret);

	return ret;
}

/* audmixer_apb_remove: APB bus specific removal sequence */
static int audmixer_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	return 0;
}

#ifdef CONFIG_PM_RUNTIME
/*
 * audmixer_runtime_resume: Runtime resume handler for Audio Mixer
 *
 * It does following.
 * 1. Enables LPASS (as clocks/reset etc are controlled by LPASS)
 * 2. Configures SoC specific registers for power/reset
 * 3. Enables clocks
 * 4. Sets pins(GPIOs) to appropriates states
 * 5. Restores the h/w registers
 * 6. Marks the mixer as active (to be used indirectly by PM framework for
 * checking whether mixer is active)
 */
static int audmixer_runtime_resume(struct device *dev)
{
	static unsigned int count;

	dev_dbg(dev, "(*) %s (count = %d)\n", __func__, ++count);

	lpass_get_sync(dev);
	audmixer_power_on(dev);
	audmixer_clk_enable(dev);
	audmixer_cfg_gpio(dev, "default");
	/* sys reset audio mixer */
	audmixer_reset_sys_data();
	if (g_audmixer->is_regs_stored == true)
		audmixer_restore_regs(dev);
	g_audmixer->is_regs_stored = false;
	g_audmixer->is_active = true;
	return 0;
}

/*
 * audmixer_runtime_suspend: Runtime suspend handler for Audio Mixer
 *
 * It does following.
 * 1. Marks the mixer as inactive (to be used indirectly by PM framework for
 * checking whether mixer is active)
 * 2. Saves the h/w registers
 * 3. Sets pins(GPIOs) to appropriates states
 * 4. Disables clocks
 * 5. Configures SoC specific registers for power/reset
 * 6. Disables LPASS (LPASS is disabled only if it internal use count is zero)
 */
static int audmixer_runtime_suspend(struct device *dev)
{
	static unsigned int count;

	dev_dbg(dev, "(*) %s (count = %d)\n", __func__, ++count);

	g_audmixer->is_active = false;
	audmixer_save_regs(dev);
	g_audmixer->is_regs_stored = true;
	audmixer_cfg_gpio(dev, "idle");
	audmixer_clk_disable(dev);
	audmixer_power_off(dev);
	lpass_put_sync(dev);

	return 0;
}

/*
 * audmixer_get_sync: Exported function to enable the mixer
 *
 * The sound-card driver doesn't have the handle to the mixer, hence uses this
 * function to enable the mixer.
 *
 * TODO: Use the device structure reference in sound-card driver to
 * enable/disable the mixer.
 */
void audmixer_get_sync()
{
	if (g_audmixer != NULL)
		pm_runtime_get_sync(g_audmixer->dev);
}

/*
 * audmixer_put_sync: Exported function to disable the mixer
 *
 * The sound-card driver doesn't have the handle to the mixer, hence uses this
 * function to disable the mixer.
 *
 * TODO: Use the device structure reference in sound-card driver to
 * enable/disable the mixer.
 */
void audmixer_put_sync()
{
	if (g_audmixer != NULL)
		pm_runtime_put_sync(g_audmixer->dev);
}
#endif

static const struct dev_pm_ops audmixer_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(
			NULL,
			NULL
	)
#ifdef CONFIG_PM_RUNTIME
	SET_RUNTIME_PM_OPS(
			audmixer_runtime_suspend,
			audmixer_runtime_resume,
			NULL
	)
#endif
};

static struct platform_driver audmixer_apb_driver = {
	.driver = {
		.name = "audmixer",
		.owner = THIS_MODULE,
		.pm = &audmixer_pm,
		.of_match_table = of_match_ptr(audmixer_apb_dt_ids),
	},
	.probe = audmixer_apb_probe,
	.remove = audmixer_apb_remove,
};
module_platform_driver(audmixer_apb_driver);

static struct i2c_driver audmixer_i2c_driver = {
	.driver = {
		.name = "s2801x",
		.owner = THIS_MODULE,
		.pm = &audmixer_pm,
		.of_match_table = of_match_ptr(audmixer_i2c_dt_ids),
	},
	.probe = audmixer_i2c_probe,
	.remove = audmixer_i2c_remove,
	.id_table = audmixer_i2c_id,
};

module_i2c_driver(audmixer_i2c_driver);
MODULE_DESCRIPTION("ASoC AudMixer driver");
MODULE_AUTHOR("Tushar Behera <tushar.b@samsung.com>");
MODULE_AUTHOR("R Chandrasekar <rcsekar@samsung.com>");
MODULE_AUTHOR("Dong-Gyun Go <donggyun.ko@samsung.com>");
MODULE_LICENSE("GPL v2");
MODULE_FIRMWARE(S2803X_FIRMWARE_NAME);
