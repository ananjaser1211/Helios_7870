/*
 * Universal7870-RT5659 Audio Machine driver.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/gpio.h>

#include <sound/tlv.h>

#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/exynos-audmixer.h>

#include "i2s.h"
#include "i2s-regs.h"

#include "../codecs/rt5659.h"

#include <linux/mfd/syscon.h>

#include <linux/sec_jack.h>
#include <linux/iio/consumer.h>

#define CODEC_BFS_48KHZ		32
#define CODEC_RFS_48KHZ		512
#define CODEC_SAMPLE_RATE_48KHZ	48000

#define CODEC_BFS_192KHZ		64
#define CODEC_RFS_192KHZ		128
#define CODEC_SAMPLE_RATE_192KHZ	192000
#define CODEC_PLL_192KHZ		49152000
#define CODEC_PLL_48KHZ			24576000

#define JOSHUA_MCLK_FREQ 26000000

#ifdef CONFIG_SND_SOC_SAMSUNG_VERBOSE_DEBUG
#define RX_SRAM_SIZE            (0x2000)        /* 8 KB */
#ifdef dev_dbg
#undef dev_dbg
#endif
#define dev_dbg dev_err
#endif

#define EXYNOS_PMU_PMU_DEBUG_OFFSET		0x0A00
struct regmap *pmureg;

static struct snd_soc_card universal7870_rt5659_card;
extern void set_ip_idle(bool value);

struct rt5659_machine_priv {
	struct snd_soc_codec *codec;
	int aifrate;
	bool use_external_jd;
	int earmic_bias;
	struct iio_channel *adc_channel;
	struct clk *mclk;
};

static const struct snd_soc_component_driver universal7870_rt5659_cmpnt = {
	.name = "Universal7870-rt5659-audio",
};

void universal7870_set_mclk(int enable)
{
	struct snd_soc_card *card = &universal7870_rt5659_card;
	struct rt5659_machine_priv *priv = snd_soc_card_get_drvdata(card);
	static unsigned int mclk_usecount;
	int ret;

	if (priv->mclk) {
		if (enable) {
			ret = clk_enable(priv->mclk);
			if (ret < 0) {
				dev_err(card->dev,
					"clk_enable failed = %d\n", ret);
				return;
			}
			mclk_usecount++;
			dev_info(card->dev, "mclk enabled: cnt %d\n", mclk_usecount);
		} else {
			clk_disable(priv->mclk);
			mclk_usecount--;
			dev_info(card->dev, "mclk disbled: cnt %d\n", mclk_usecount);
		}
	} else {
		dev_info(card->dev, "%s: %d usecount %d\n", __func__, enable, mclk_usecount);

		if (pmureg == NULL) {
			dev_info(card->dev, "pmureg is NULL\n");
			return;
		}

		if (enable) {
			if (mclk_usecount == 0) {
				dev_info(card->dev, "%s: mclk_enable\n", __func__);
				regmap_update_bits(pmureg,
						EXYNOS_PMU_PMU_DEBUG_OFFSET,
						0x1F01, 0x1F00);
			}
			mclk_usecount++;
		} else {
			if (mclk_usecount > 0)
				mclk_usecount--;
		
			if (mclk_usecount == 0) {
				dev_info(card->dev, "%s: mclk_disable\n", __func__);
				regmap_update_bits(pmureg,
						EXYNOS_PMU_PMU_DEBUG_OFFSET,
						0x1F01, 0x1F01);
			}
		}
	}
}

int rt5659_enable_ear_micbias(bool state)
{
	struct snd_soc_card *card = &universal7870_rt5659_card;
	struct rt5659_machine_priv *priv = snd_soc_card_get_drvdata(card);
	int mic_bias;

	pr_info("%s: bias%d(%d)\n", __func__, priv->earmic_bias, state);

	switch (priv->earmic_bias) {
	case 2:
		mic_bias = RT5659_MICBIAS2;
		break;
	default:
		mic_bias = RT5659_MICBIAS1;
		break;
	}
	rt5659_micbias_output(mic_bias, state);

	return 0;
}

int universal7870_get_jack_adc(void)
{
	int adc_data;
	int ret;
	struct snd_soc_card *card = &universal7870_rt5659_card;
	struct rt5659_machine_priv *priv = snd_soc_card_get_drvdata(card);

	pr_info("%s\n", __func__);

	ret = iio_read_channel_raw(priv->adc_channel, &adc_data);

	return adc_data;
}

static int universal7870_aif1_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *amixer_dai = rtd->codec_dais[0];
	struct snd_soc_dai *codec_dai = rtd->codec_dais[1];
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int ret;
	int rfs, bfs, codec_pll;

	dev_info(card->dev, "%s-%d %dch, %dHz, %dbytes\n",
			rtd->dai_link->name, substream->stream,
			params_channels(params), params_rate(params),
			params_buffer_bytes(params));

	universal7870_set_mclk(1);

	if (params_rate(params) == CODEC_SAMPLE_RATE_192KHZ) {
		rfs = CODEC_RFS_192KHZ;
		bfs = CODEC_BFS_192KHZ;
	} else {
		rfs = CODEC_RFS_48KHZ;
		bfs = CODEC_BFS_48KHZ;
	}

	/* Set Codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S
					 | SND_SOC_DAIFMT_NB_NF
					 | SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0) {
		dev_err(card->dev, "Failed to set aif1 codec fmt: %d\n", ret);
		return ret;
	}

	/* Set CPU DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S
					 | SND_SOC_DAIFMT_NB_NF
					 | SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0) {
		dev_err(card->dev, "Failed to set aif1 cpu fmt: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(cpu_dai, SAMSUNG_I2S_CDCLK,
				rfs, SND_SOC_CLOCK_OUT);
	if (ret < 0) {
		dev_err(card->dev, "aif1: Failed to set SAMSUNG_I2S_CDCLK\n");
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(cpu_dai, SAMSUNG_I2S_OPCLK,
						0, MOD_OPCLK_PCLK);
	if (ret < 0) {
		dev_err(card->dev, "aif1: Failed to set SAMSUNG_I2S_OPCLK\n");
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(cpu_dai, SAMSUNG_I2S_RCLKSRC_1, 0, 0);
	if (ret < 0) {
		dev_err(card->dev,
				"aif1: Failed to set SAMSUNG_I2S_RCLKSRC_1\n");
		return ret;
	}

	ret = snd_soc_dai_set_clkdiv(cpu_dai,
			SAMSUNG_I2S_DIV_BCLK, bfs);
	if (ret < 0) {
		dev_err(card->dev, "aif1: Failed to set BFS\n");
		return ret;
	}

	ret = snd_soc_dai_set_clkdiv(cpu_dai,
			SAMSUNG_I2S_DIV_RCLK, rfs);
	if (ret < 0) {
		dev_err(card->dev, "aif1: Failed to set RFS\n");
		return ret;
	}

	ret = snd_soc_dai_set_bclk_ratio(amixer_dai, bfs);
	if (ret < 0) {
		dev_err(card->dev, "aif1: Failed to configure mixer\n");
		return ret;
	}

	if (params_rate(params) == CODEC_SAMPLE_RATE_192KHZ) {
		codec_pll = CODEC_PLL_192KHZ;

		ret = snd_soc_dai_set_pll(codec_dai, 0, RT5659_PLL1_S_BCLK1,
			CODEC_SAMPLE_RATE_192KHZ * 32 * 2, codec_pll);

		if (ret < 0) {
			dev_err(card->dev, "codec_dai pll not set\n");
			return ret;
		}
	} else {
		codec_pll = CODEC_PLL_48KHZ;

		ret = snd_soc_dai_set_pll(codec_dai, 0, RT5659_PLL1_S_MCLK,
			JOSHUA_MCLK_FREQ, codec_pll);

		if (ret < 0) {
			dev_err(card->dev, "codec_dai pll not set\n");
			return ret;
		}
	}

	ret = snd_soc_dai_set_sysclk(codec_dai, RT5659_SCLK_S_PLL1,
		codec_pll, SND_SOC_CLOCK_IN);

	if (ret < 0) {
		dev_err(card->dev, "codec_dai clock not set\n");
		return ret;
	}

	return ret;

}

static int universal7870_aif2_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *amixer_dai = rtd->codec_dais[0];
	struct snd_soc_dai *codec_dai = rtd->codec_dais[1];
	struct snd_interval *rate =
			hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	int bfs, ret;

	dev_info(card->dev, "aif2: %dch, %dHz, %dbytes\n",
		 params_channels(params), params_rate(params),
		 params_buffer_bytes(params));

	dev_info(card->dev, "Fix up the rate to 48KHz\n");
	rate->min = rate->max = 48000;

	universal7870_set_mclk(1);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_U24:
	case SNDRV_PCM_FORMAT_S24:
		bfs = 48;
		break;
	case SNDRV_PCM_FORMAT_U16_LE:
	case SNDRV_PCM_FORMAT_S16_LE:
		bfs = 32;
		break;
	default:
		dev_err(card->dev, "aif2: Unsupported PCM_FORMAT\n");
		return -EINVAL;
	}

	ret = snd_soc_dai_set_bclk_ratio(amixer_dai, bfs);
	if (ret < 0) {
		dev_err(card->dev, "aif2: Failed to configure mixer\n");
		return ret;
	}

	/* Set Codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S
					 | SND_SOC_DAIFMT_NB_NF
					 | SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0) {
		dev_err(card->dev, "Failed to set aif2 codec fmt: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_pll(codec_dai, 0, RT5659_PLL1_S_MCLK,
			JOSHUA_MCLK_FREQ, CODEC_PLL_48KHZ);
	if (ret < 0) {
		dev_err(card->dev, "codec_dai pll not set\n");
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(codec_dai, RT5659_SCLK_S_PLL1,
			CODEC_PLL_48KHZ, SND_SOC_CLOCK_IN);

	if (ret < 0) {
		dev_err(card->dev, "codec_dai clock not set\n");
		return ret;
	}

	return 0;
}

static int universal7870_aif3_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *amixer_dai = rtd->codec_dais[0];
	int bfs, ret;

	dev_info(card->dev, "aif3: %dch, %dHz, %dbytes\n",
		 params_channels(params), params_rate(params),
		 params_buffer_bytes(params));

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_U24:
	case SNDRV_PCM_FORMAT_S24:
		bfs = 48;
		break;
	case SNDRV_PCM_FORMAT_U16_LE:
	case SNDRV_PCM_FORMAT_S16_LE:
		bfs = 32;
		break;
	default:
		dev_err(card->dev, "aif3: Unsupported PCM_FORMAT\n");
		return -EINVAL;
	}

	ret = snd_soc_dai_set_bclk_ratio(amixer_dai, bfs);
	if (ret < 0) {
		dev_err(card->dev, "aif3: Failed to configure mixer\n");
		return ret;
	}

	return 0;
}

static int universal7870_aif1_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;

	dev_dbg(card->dev, "aif1: (%s) %s called\n",
			substream->stream ? "C" : "P", __func__);

	return 0;
}

void universal7870_aif1_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;

	dev_dbg(card->dev, "aif1: (%s) %s called\n",
			substream->stream ? "C" : "P", __func__);

	universal7870_set_mclk(0);
}

static int universal7870_aif2_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;

	dev_dbg(card->dev, "aif2: (%s) %s called\n",
			substream->stream ? "C" : "P", __func__);

	return 0;
}

void universal7870_aif2_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;

	dev_dbg(card->dev, "aif2: (%s) %s called\n",
			substream->stream ? "C" : "P", __func__);

	universal7870_set_mclk(0);
}

static int universal7870_aif3_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;

	dev_dbg(card->dev, "aif3: (%s) %s called\n",
			substream->stream ? "C" : "P", __func__);

	return 0;
}

void universal7870_aif3_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;

	dev_dbg(card->dev, "aif3: (%s) %s called\n",
			substream->stream ? "C" : "P", __func__);

}

static int universal7870_set_bias_level(struct snd_soc_card *card,
				 struct snd_soc_dapm_context *dapm,
				 enum snd_soc_bias_level level)
{
	struct rt5659_machine_priv *priv = snd_soc_card_get_drvdata(card);

	if (!priv->codec || dapm != &priv->codec->dapm)
		return 0;

	dev_info(card->dev, "%s: %d\n", __func__, level);

	switch (level) {
	case SND_SOC_BIAS_OFF:
		if (card->dapm.bias_level == SND_SOC_BIAS_STANDBY)
			universal7870_set_mclk(0);
		break;
	case SND_SOC_BIAS_STANDBY:
		if (card->dapm.bias_level == SND_SOC_BIAS_OFF)
			universal7870_set_mclk(1);
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_ON:
		break;
	default:
		break;
	}

	card->dapm.bias_level = level;

	return 0;
}

static int universal7870_late_probe(struct snd_soc_card *card)
{
	struct snd_soc_codec *codec = card->rtd[0].codec_dais[1]->codec;
	struct rt5659_machine_priv *priv = snd_soc_card_get_drvdata(card);

	dev_info(card->dev, "%s\n", __func__);

	priv->codec = codec;

	snd_soc_dapm_ignore_suspend(&card->dapm, "Main Mic");
	snd_soc_dapm_ignore_suspend(&card->dapm, "Sub Mic");
	snd_soc_dapm_ignore_suspend(&card->dapm, "Headset Mic");
	snd_soc_dapm_ignore_suspend(&card->dapm, "RCV");
	snd_soc_dapm_ignore_suspend(&card->dapm, "SPK");
	snd_soc_dapm_ignore_suspend(&card->dapm, "HP");
	snd_soc_dapm_sync(&card->dapm);

	snd_soc_dapm_ignore_suspend(&codec->dapm, "AIF1 Playback");
	snd_soc_dapm_ignore_suspend(&codec->dapm, "AIF1 Capture");
	snd_soc_dapm_ignore_suspend(&codec->dapm, "AIF2 Playback");
	snd_soc_dapm_ignore_suspend(&codec->dapm, "AIF2 Capture");
	snd_soc_dapm_ignore_suspend(&codec->dapm, "AIF3 Playback");
	snd_soc_dapm_ignore_suspend(&codec->dapm, "AIF3 Capture");
	snd_soc_dapm_sync(&codec->dapm);

	return 0;
}

static int audmixer_init(struct snd_soc_component *cmp)
{
	dev_dbg(cmp->dev, "%s called\n", __func__);

	return 0;
}

static const struct snd_kcontrol_new universal7870_rt5659_controls[] = {
	SOC_DAPM_PIN_SWITCH("HP"),
	SOC_DAPM_PIN_SWITCH("SPK"),
	SOC_DAPM_PIN_SWITCH("RCV"),
	SOC_DAPM_PIN_SWITCH("Main Mic"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
};

const struct snd_soc_dapm_widget universal7870_rt5659_dapm_widgets[] = {
	SND_SOC_DAPM_HP("HP", NULL),
	SND_SOC_DAPM_SPK("SPK", NULL),
	SND_SOC_DAPM_SPK("RCV", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Main Mic", NULL),
};

const struct snd_soc_dapm_route universal7870_rt5659_dapm_routes[] = {
	{ "HP", NULL, "HPOL" },
	{ "HP", NULL, "HPOR" },
	{ "RCV", NULL, "MONOOUT" },
	{ "SPK", NULL, "SPOL" },
	{ "SPK", NULL, "SPOR" },
	{ "IN1P", NULL, "Headset Mic" },
	{ "IN1N", NULL, "Headset Mic" },
	{ "IN3P", NULL, "MICBIAS2" },
	{ "IN3P", NULL, "Main Mic" },
	{ "IN3N", NULL, "Main Mic" },
};

static struct snd_soc_ops universal7870_aif1_ops = {
	.hw_params = universal7870_aif1_hw_params,
	.startup = universal7870_aif1_startup,
	.shutdown = universal7870_aif1_shutdown,
};

static struct snd_soc_ops universal7870_aif2_ops = {
	.hw_params = universal7870_aif2_hw_params,
	.startup = universal7870_aif2_startup,
	.shutdown = universal7870_aif2_shutdown,
};

static struct snd_soc_ops universal7870_aif3_ops = {
	.hw_params = universal7870_aif3_hw_params,
	.startup = universal7870_aif3_startup,
	.shutdown = universal7870_aif3_shutdown,
};

static struct snd_soc_dai_driver universal7870_ext_dai[] = {
	{
		.name = "universal7870 voice call",
		.playback = {
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 48000,
			.rates = (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
					SNDRV_PCM_RATE_48000),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
			 SNDRV_PCM_FMTBIT_S24_LE)
		},
		.capture = {
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 48000,
			.rates = (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
					SNDRV_PCM_RATE_48000),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
			 SNDRV_PCM_FMTBIT_S24_LE)
		},
	},
	{
		.name = "universal7870 BT",
		.playback = {
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 48000,
			.rates = (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
					SNDRV_PCM_RATE_48000),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
			 SNDRV_PCM_FMTBIT_S24_LE)
		},
		.capture = {
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 48000,
			.rates = (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
					SNDRV_PCM_RATE_48000),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
			 SNDRV_PCM_FMTBIT_S24_LE)
		},
	},
};

static struct snd_soc_dai_link_component codecs_ap0[] = {
	{
		.name = "14880000.s1402x",
		.dai_name = "AP0",
	}, {
		.dai_name = "rt5659-aif1",
	},
};

static struct snd_soc_dai_link_component codecs_cp0[] = {
	{
		.name = "14880000.s1402x",
		.dai_name = "CP0",
	}, {
		.dai_name = "rt5659-aif1",
	},
};

static struct snd_soc_dai_link_component codecs_bt[] = {
	{
		.name = "14880000.s1402x",
		.dai_name = "BT",
	}, {
		.dai_name = "dummy-aif2",
	},
};

static struct snd_soc_dai_link universal7870_rt5659_dai[] = {
	/* Playback and Recording */
	{
		.name = "universal7870-rt5659-pri",
		.stream_name = "i2s0-pri",
		.codecs = codecs_ap0,
		.num_codecs = ARRAY_SIZE(codecs_ap0),
		.ops = &universal7870_aif1_ops,
		.ignore_pmdown_time = true,
	},

	/* Deep buffer playback */
	{
		.name = "universal7870-rt5659-sec",
		.cpu_dai_name = "samsung-i2s-sec",
		.stream_name = "i2s0-sec",
		.platform_name = "samsung-i2s-sec",
		.codecs = codecs_ap0,
		.num_codecs = ARRAY_SIZE(codecs_ap0),
		.ops = &universal7870_aif1_ops,
		.ignore_pmdown_time = true,
	},

	/* Voice Call */
	{
		.name = "cp",
		.stream_name = "voice call",
		.cpu_dai_name = "universal7870 voice call",
		.platform_name = "snd-soc-dummy",
		.codecs = codecs_cp0,
		.num_codecs = ARRAY_SIZE(codecs_cp0),
		.ops = &universal7870_aif2_ops,
		.ignore_suspend = 1,
		.ignore_pmdown_time = true,
	},

	/* BT */
	{
		.name = "bt",
		.stream_name = "bluetooth audio",
		.cpu_dai_name = "universal7870 BT",
		.platform_name = "snd-soc-dummy",
		.codecs = codecs_bt,
		.num_codecs = ARRAY_SIZE(codecs_bt),
		.ops = &universal7870_aif3_ops,
		.ignore_suspend = 1,
	},

	/* SW MIXER1 Interface */
	{
		.name = "playback-eax0",
		.stream_name = "eax0",
		.cpu_dai_name = "samsung-eax.0",
		.platform_name = "samsung-eax.0",
		.codecs = codecs_ap0,
		.num_codecs = ARRAY_SIZE(codecs_ap0),
		.ops = &universal7870_aif1_ops,
		.ignore_suspend = 1,
		.ignore_pmdown_time = true,
	},

	/* SW MIXER2 Interface */
	{
		.name = "playback-eax1",
		.stream_name = "eax1",
		.cpu_dai_name = "samsung-eax.1",
		.platform_name = "samsung-eax.1",
		.codecs = codecs_ap0,
		.num_codecs = ARRAY_SIZE(codecs_ap0),
		.ops = &universal7870_aif1_ops,
		.ignore_suspend = 1,
		.ignore_pmdown_time = true,
	},

	/* SW MIXER3 Interface */
	{
		.name = "playback-eax2",
		.stream_name = "eax2",
		.cpu_dai_name = "samsung-eax.2",
		.platform_name = "samsung-eax.2",
		.codecs = codecs_ap0,
		.num_codecs = ARRAY_SIZE(codecs_ap0),
		.ops = &universal7870_aif1_ops,
		.ignore_suspend = 1,
		.ignore_pmdown_time = true,
	},

	/* SW MIXER4 Interface */
	{
		.name = "playback-eax3",
		.stream_name = "eax3",
		.cpu_dai_name = "samsung-eax.3",
		.platform_name = "samsung-eax.3",
		.codecs = codecs_ap0,
		.num_codecs = ARRAY_SIZE(codecs_ap0),
		.ops = &universal7870_aif1_ops,
		.ignore_suspend = 1,
		.ignore_pmdown_time = true,
	},
};

static struct snd_soc_aux_dev audmixer_aux_dev[] = {
	{
		.init = audmixer_init,
	},
};

static struct snd_soc_codec_conf audmixer_codec_conf[] = {
	{
		.name_prefix = "AudioMixer",
	},
};

static struct snd_soc_card universal7870_rt5659_card = {
	.name = "Universal7870-RT5659",
	.owner = THIS_MODULE,

	.dai_link = universal7870_rt5659_dai,
	.num_links = ARRAY_SIZE(universal7870_rt5659_dai),

	.controls = universal7870_rt5659_controls,
	.num_controls = ARRAY_SIZE(universal7870_rt5659_controls),
	.dapm_widgets = universal7870_rt5659_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(universal7870_rt5659_dapm_widgets),
	.dapm_routes = universal7870_rt5659_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(universal7870_rt5659_dapm_routes),

	.late_probe = universal7870_late_probe,
	.set_bias_level = universal7870_set_bias_level,
	.aux_dev = audmixer_aux_dev,
	.num_aux_devs = ARRAY_SIZE(audmixer_aux_dev),
	.codec_conf = audmixer_codec_conf,
	.num_configs = ARRAY_SIZE(audmixer_codec_conf),
};

static int universal7870_rt5659_audio_probe(struct platform_device *pdev)
{
	int n, ret;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *cpu_np, *codec_np, *auxdev_np;
	struct snd_soc_card *card = &universal7870_rt5659_card;
	struct rt5659_machine_priv *priv;
	int of_route;

	if (!np) {
		dev_err(&pdev->dev, "Failed to get device node\n");
		return -EINVAL;
	}

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	card->dev = &pdev->dev;
	card->num_links = 0;

	priv->mclk = devm_clk_get(card->dev, "mclk");
	if (IS_ERR(priv->mclk)) {
		dev_info(card->dev, "Device tree node not found for mclk");
		priv->mclk = NULL;
	} else
		clk_prepare(priv->mclk);

	ret = snd_soc_register_component(card->dev, &universal7870_rt5659_cmpnt,
			universal7870_ext_dai,
			ARRAY_SIZE(universal7870_ext_dai));
	if (ret) {
		dev_err(&pdev->dev, "Failed to register component: %d\n", ret);
		return ret;
	}

	of_route = of_property_read_bool(np, "samsung,audio-routing");
	if (!card->dapm_routes || !card->num_dapm_routes || of_route) {
		ret = snd_soc_of_parse_audio_routing(card,
				"samsung,audio-routing");
		if (ret != 0) {
			dev_err(&pdev->dev,
				"Failed to parse audio routing: %d\n",
				ret);
			ret = -EINVAL;
			goto err_audio_route;
		}
	}

	for (n = 0; n < ARRAY_SIZE(universal7870_rt5659_dai); n++) {
		/* Skip parsing DT for fully formed dai links */
		if (universal7870_rt5659_dai[n].platform_name &&
				universal7870_rt5659_dai[n].codec_name) {
			dev_dbg(card->dev,
			"Skipping dt for populated dai link %s\n",
			universal7870_rt5659_dai[n].name);
			card->num_links++;
			continue;
		}

		cpu_np = of_parse_phandle(np, "samsung,audio-cpu", n);
		if (!cpu_np) {
			dev_err(&pdev->dev,
				"Property 'samsung,audio-cpu' missing\n");
			break;
		}

		codec_np = of_parse_phandle(np, "samsung,audio-codec", n);
		if (!codec_np) {
			dev_err(&pdev->dev,
				"Property 'samsung,audio-codec' missing\n");
			break;
		}

		universal7870_rt5659_dai[n].codecs[1].of_node = codec_np;
		if (!universal7870_rt5659_dai[n].cpu_dai_name)
			universal7870_rt5659_dai[n].cpu_of_node = cpu_np;
		if (!universal7870_rt5659_dai[n].platform_name)
			universal7870_rt5659_dai[n].platform_of_node = cpu_np;

		card->num_links++;
	}

	for (n = 0; n < ARRAY_SIZE(audmixer_aux_dev); n++) {
		auxdev_np = of_parse_phandle(np, "samsung,auxdev", n);
		if (!auxdev_np) {
			dev_err(&pdev->dev,
				"Property 'samsung,auxdev' missing\n");
			ret = -EINVAL;
			goto err_audio_route;
		}

		audmixer_aux_dev[n].codec_of_node = auxdev_np;
		audmixer_codec_conf[n].of_node = auxdev_np;
	}

	ret = of_property_read_u32_array(np, "earmic_bias",
			&priv->earmic_bias, 1);
	if (ret == -EINVAL)
		priv->earmic_bias = 1;

	pmureg = syscon_regmap_lookup_by_phandle(np,
			"samsung,syscon-phandle");
	if (IS_ERR(pmureg)) {
		dev_err(&pdev->dev, "syscon regmap lookup failed.\n");
		pmureg = NULL;
	}

	snd_soc_card_set_drvdata(card, priv);

	ret = snd_soc_register_card(card);
	if (ret)
		dev_err(&pdev->dev, "Failed to register card:%d\n", ret);

	priv->adc_channel = iio_channel_get(&pdev->dev, NULL);
	if (IS_ERR(priv->adc_channel)) {
		dev_err(&pdev->dev, "%s: failed to iio_channel_get\n",
				__func__);
	} else {
		sec_jack_set_snd_card_registered(1);
		sec_jack_register_set_micbias_cb(rt5659_enable_ear_micbias);
		sec_jack_register_get_adc_cb(universal7870_get_jack_adc);
	}

	return ret;

err_audio_route:
	snd_soc_unregister_component(card->dev);
	return ret;
}

static int universal7870_rt5659_audio_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct rt5659_machine_priv *priv = card->drvdata;

	sec_jack_set_snd_card_registered(0);

	snd_soc_unregister_component(card->dev);
	snd_soc_unregister_card(card);

	if (priv->mclk) {
		clk_unprepare(priv->mclk);
		devm_clk_put(card->dev, priv->mclk);
	}

	return 0;
}

static const struct of_device_id universal7870_rt5659_of_match[] = {
	{.compatible = "samsung,universal7870-rt5659",},
	{},
};
MODULE_DEVICE_TABLE(of, universal7870_rt5659_of_match);

static struct platform_driver universal7870_rt5659_audio_driver = {
	.driver = {
		.name = "Universal7870-rt5659-audio",
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table = of_match_ptr(universal7870_rt5659_of_match),
	},
	.probe = universal7870_rt5659_audio_probe,
	.remove = universal7870_rt5659_audio_remove,
};

module_platform_driver(universal7870_rt5659_audio_driver);

MODULE_DESCRIPTION("ALSA SoC Universal7870 RT5659");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:universal7870-audio-rt5659");
