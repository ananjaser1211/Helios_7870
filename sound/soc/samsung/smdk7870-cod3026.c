/*
 * Smdk7870-COD3025X Audio Machine driver.
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

#define CODEC_BFS_48KHZ		32
#define CODEC_RFS_48KHZ		512
#define CODEC_SAMPLE_RATE_48KHZ	48000

#define CODEC_BFS_192KHZ		64
#define CODEC_RFS_192KHZ		128
#define CODEC_SAMPLE_RATE_192KHZ	192000

#ifdef CONFIG_SND_SOC_SAMSUNG_VERBOSE_DEBUG
#define RX_SRAM_SIZE            (0x2000)        /* 8 KB */
#ifdef dev_dbg
#undef dev_dbg
#endif
#define dev_dbg dev_err
#endif

static struct snd_soc_card smdk7870_cod3025x_card;

struct cod3025x_machine_priv {
	struct snd_soc_codec *codec;
	int aifrate;
	bool use_external_jd;
};

static const struct snd_soc_component_driver smdk7870_cmpnt = {
	.name = "Smdk7870-audio",
};

static int smdk7870_aif1_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct cod3025x_machine_priv *priv = snd_soc_card_get_drvdata(card);
	int ret;
	int rfs, bfs;

	dev_info(card->dev, "aif1: %dch, %dHz, %dbytes\n",
		 params_channels(params), params_rate(params),
		 params_buffer_bytes(params));

	priv->aifrate = params_rate(params);

	if (priv->aifrate == CODEC_SAMPLE_RATE_192KHZ) {
		rfs = CODEC_RFS_192KHZ;
		bfs = CODEC_BFS_192KHZ;
	} else {
		rfs = CODEC_RFS_48KHZ;
		bfs = CODEC_BFS_48KHZ;
	}

	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S
						| SND_SOC_DAIFMT_NB_NF
						| SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0) {
		dev_err(card->dev, "aif1: Failed to set Codec DAIFMT\n");
		return ret;
	}

	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S
						| SND_SOC_DAIFMT_NB_NF
						| SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0) {
		dev_err(card->dev, "aif1: Failed to set CPU DAIFMT\n");
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

	ret = audmixer_hw_params(substream, params, bfs, AUDMIXER_IF_AP);
	if (ret < 0) {
		dev_err(card->dev, "aif1: Failed to configure mixer\n");
		return ret;
	}

	return 0;
}

static int smdk7870_aif2_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	int bfs, ret;

	dev_info(card->dev, "aif2: %dch, %dHz, %dbytes\n",
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
		dev_err(card->dev, "aif2: Unsupported PCM_FORMAT\n");
		return -EINVAL;
	}

	ret = audmixer_hw_params(substream, params, bfs, AUDMIXER_IF_CP);
	if (ret < 0) {
		dev_err(card->dev, "aif2: Failed to configure mixer\n");
		return ret;
	}

	return 0;
}

static int smdk7870_aif3_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
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

	ret = audmixer_hw_params(substream, params, bfs, AUDMIXER_IF_BT);
	if (ret < 0) {
		dev_err(card->dev, "aif3: Failed to configure mixer\n");
		return ret;
	}

	return 0;
}

static int smdk7870_aif5_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct cod3025x_machine_priv *priv = snd_soc_card_get_drvdata(card);
	int ret;
	int rfs, bfs;

	dev_info(card->dev, "aif5: %dch, %dHz, %dbytes\n",
		 params_channels(params), params_rate(params),
		 params_buffer_bytes(params));

	priv->aifrate = params_rate(params);

	if (priv->aifrate == CODEC_SAMPLE_RATE_192KHZ) {
		rfs = CODEC_RFS_192KHZ;
		bfs = CODEC_BFS_192KHZ;
	} else {
		rfs = CODEC_RFS_48KHZ;
		bfs = CODEC_BFS_48KHZ;
	}

	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S
						| SND_SOC_DAIFMT_NB_NF
						| SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0) {
		dev_err(card->dev, "aif5: Failed to set CPU DAIFMT\n");
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(cpu_dai, SAMSUNG_I2S_CDCLK,
				rfs, SND_SOC_CLOCK_OUT);
	if (ret < 0) {
		dev_err(card->dev, "aif5: Failed to set SAMSUNG_I2S_CDCLK\n");
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(cpu_dai, SAMSUNG_I2S_OPCLK,
						0, MOD_OPCLK_PCLK);
	if (ret < 0) {
		dev_err(card->dev, "aif5: Failed to set SAMSUNG_I2S_OPCLK\n");
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(cpu_dai, SAMSUNG_I2S_RCLKSRC_1, 0, 0);
	if (ret < 0) {
		dev_err(card->dev,
				"aif5: Failed to set SAMSUNG_I2S_RCLKSRC_1\n");
		return ret;
	}

	ret = snd_soc_dai_set_clkdiv(cpu_dai,
			SAMSUNG_I2S_DIV_BCLK, bfs);
	if (ret < 0) {
		dev_err(card->dev, "aif5: Failed to set BFS\n");
		return ret;
	}

	ret = snd_soc_dai_set_clkdiv(cpu_dai,
			SAMSUNG_I2S_DIV_RCLK, rfs);
	if (ret < 0) {
		dev_err(card->dev, "aif5: Failed to set RFS\n");
		return ret;
	}

	ret = audmixer_hw_params(substream, params, bfs, AUDMIXER_IF_AP1);
	if (ret < 0) {
		dev_err(card->dev, "aif5: Failed to configure mixer\n");
		return ret;
	}

	return 0;
}

static int smdk7870_aif6_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	int bfs, ret;

	dev_info(card->dev, "aif6: %dch, %dHz, %dbytes\n",
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
			dev_err(card->dev, "aif6: Unsupported PCM_FORMAT\n");
			return -EINVAL;
	}

	ret = audmixer_hw_params(substream, params, bfs, AUDMIXER_IF_CP1);
	if (ret < 0) {
		dev_err(card->dev, "aif6: Failed to configure mixer\n");
		return ret;
	}

	return 0;
}

static int smdk7870_aif1_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;

	dev_dbg(card->dev, "aif1: (%s) %s called\n",
			substream->stream ? "C" : "P", __func__);

	audmixer_startup(AUDMIXER_IF_AP);
	return 0;
}

void smdk7870_aif1_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;

	dev_dbg(card->dev, "aif1: (%s) %s called\n",
			substream->stream ? "C" : "P", __func__);

	audmixer_shutdown(AUDMIXER_IF_AP);
}

static int smdk7870_aif2_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;

	dev_dbg(card->dev, "aif2: (%s) %s called\n",
			substream->stream ? "C" : "P", __func__);

	audmixer_startup(AUDMIXER_IF_CP);

	return 0;
}

void smdk7870_aif2_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;

	dev_dbg(card->dev, "aif2: (%s) %s called\n",
			substream->stream ? "C" : "P", __func__);

	audmixer_shutdown(AUDMIXER_IF_CP);
}

static int smdk7870_aif3_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;

	dev_dbg(card->dev, "aif3: (%s) %s called\n",
			substream->stream ? "C" : "P", __func__);

	audmixer_startup(AUDMIXER_IF_BT);

	return 0;
}

void smdk7870_aif3_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;

	dev_dbg(card->dev, "aif3: (%s) %s called\n",
			substream->stream ? "C" : "P", __func__);

	audmixer_shutdown(AUDMIXER_IF_BT);
}

static int smdk7870_aif4_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;

	dev_dbg(card->dev, "aif4: (%s) %s called\n",
			substream->stream ? "C" : "P", __func__);

	audmixer_startup(AUDMIXER_IF_BT);

	return 0;
}

void smdk7870_aif4_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;

	dev_dbg(card->dev, "aif4: (%s) %s called\n",
			substream->stream ? "C" : "P", __func__);

	audmixer_shutdown(AUDMIXER_IF_BT);
}

static int smdk7870_aif5_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;

	dev_dbg(card->dev, "aif5: (%s) %s called\n",
			substream->stream ? "C" : "P", __func__);

	audmixer_startup(AUDMIXER_IF_AP1);
	return 0;

}

void smdk7870_aif5_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;

	dev_dbg(card->dev, "aif5: (%s) %s called\n",
			substream->stream ? "C" : "P", __func__);

	audmixer_shutdown(AUDMIXER_IF_AP1);
}

static int smdk7870_aif6_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;

	dev_dbg(card->dev, "aif6: (%s) %s called\n",
			substream->stream ? "C" : "P", __func__);

	audmixer_startup(AUDMIXER_IF_CP1);

	return 0;

}

void smdk7870_aif6_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;

	dev_dbg(card->dev, "aif6: (%s) %s called\n",
			substream->stream ? "C" : "P", __func__);

	audmixer_shutdown(AUDMIXER_IF_CP1);
}

static int smdk7870_set_bias_level(struct snd_soc_card *card,
				 struct snd_soc_dapm_context *dapm,
				 enum snd_soc_bias_level level)
{
	return 0;
}

static int smdk7870_late_probe(struct snd_soc_card *card)
{

	dev_dbg(card->dev, "%s called\n", __func__);
	return 0;
}

static int audmixer_init(struct snd_soc_component *cmp)
{
	dev_dbg(cmp->dev, "%s called\n", __func__);

	return 0;
}

static const struct snd_kcontrol_new smdk7870_controls[] = {
};

const struct snd_soc_dapm_widget smdk7870_dapm_widgets[] = {
};

const struct snd_soc_dapm_route smdk7870_dapm_routes[] = {
};

static struct snd_soc_ops smdk7870_aif1_ops = {
	.hw_params = smdk7870_aif1_hw_params,
	.startup = smdk7870_aif1_startup,
	.shutdown = smdk7870_aif1_shutdown,
};

static struct snd_soc_ops smdk7870_aif2_ops = {
	.hw_params = smdk7870_aif2_hw_params,
	.startup = smdk7870_aif2_startup,
	.shutdown = smdk7870_aif2_shutdown,
};

static struct snd_soc_ops smdk7870_aif3_ops = {
	.hw_params = smdk7870_aif3_hw_params,
	.startup = smdk7870_aif3_startup,
	.shutdown = smdk7870_aif3_shutdown,
};

static struct snd_soc_ops smdk7870_aif4_ops = {
	.startup = smdk7870_aif4_startup,
	.shutdown = smdk7870_aif4_shutdown,
};

static struct snd_soc_ops smdk7870_aif5_ops = {
	.hw_params = smdk7870_aif5_hw_params,
	.startup = smdk7870_aif5_startup,
	.shutdown = smdk7870_aif5_shutdown,
};

static struct snd_soc_ops smdk7870_aif6_ops = {
	.hw_params = smdk7870_aif6_hw_params,
	.startup = smdk7870_aif6_startup,
	.shutdown = smdk7870_aif6_shutdown,
};


static struct snd_soc_dai_driver smdk7870_ext_dai[] = {
	{
		.name = "smdk7870 voice call",
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
		.name = "smdk7870 BT",
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
		.name = "smdk7870 FM",
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
	},
};

static struct snd_soc_dai_link smdk7870_cod3025x_dai[] = {
	/* Playback and Recording */
	{
		.name = "smdk7870-cod3025x",
		.stream_name = "i2s0-pri",
		.codec_dai_name = "cod3026x-aif",
		.ops = &smdk7870_aif1_ops,
	},
	/* Deep buffer playback */
	{
		.name = "smdk7870-cod3025x-sec",
		.cpu_dai_name = "samsung-i2s-sec",
		.stream_name = "i2s0-sec",
		.platform_name = "samsung-i2s-sec",
		.codec_dai_name = "cod3026x-aif",
		.ops = &smdk7870_aif1_ops,
	},
	/* Voice Call */
	{
		.name = "cp",
		.stream_name = "voice call",
		.cpu_dai_name = "smdk7870 voice call",
		.platform_name = "snd-soc-dummy",
		.codec_dai_name = "cod3026x-aif2",
		.ops = &smdk7870_aif2_ops,
		.ignore_suspend = 1,
	},
	/* BT */
	{
		.name = "bt",
		.stream_name = "bluetooth audio",
		.cpu_dai_name = "smdk7870 BT",
		.platform_name = "snd-soc-dummy",
		.codec_dai_name = "dummy-aif2",
		.ops = &smdk7870_aif3_ops,
		.ignore_suspend = 1,
	},
	/* FM */
	{
		.name = "fm",
		.stream_name = "FM audio",
		.cpu_dai_name = "smdk7870 FM",
		.platform_name = "snd-soc-dummy",
		.codec_dai_name = "cod3026x-aif",
		.ops = &smdk7870_aif4_ops,
		.ignore_suspend = 1,
	},
	/* AMP AP Interface */
	{
		.name = "smdk7870-cod3026x-amp",
		.stream_name = "i2s1-pri",
		.codec_dai_name = "dummy-aif2",
		.ops = &smdk7870_aif5_ops,
	},

	/* AMP CP Interface */
	{
		.name = "cp-amp",
		.stream_name = "voice call amp",
		.cpu_dai_name = "smdk7870 voice call",
		.platform_name = "snd-soc-dummy",
		.codec_dai_name = "dummy-aif2",
		.ops = &smdk7870_aif6_ops,
		.ignore_suspend = 1,
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

static struct snd_soc_card smdk7870_cod3025x_card = {
	.name = "Smdk7870-I2S",
	.owner = THIS_MODULE,

	.dai_link = smdk7870_cod3025x_dai,
	.num_links = ARRAY_SIZE(smdk7870_cod3025x_dai),

	.controls = smdk7870_controls,
	.num_controls = ARRAY_SIZE(smdk7870_controls),
	.dapm_widgets = smdk7870_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(smdk7870_dapm_widgets),
	.dapm_routes = smdk7870_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(smdk7870_dapm_routes),

	.late_probe = smdk7870_late_probe,
	.set_bias_level = smdk7870_set_bias_level,
	.aux_dev = audmixer_aux_dev,
	.num_aux_devs = ARRAY_SIZE(audmixer_aux_dev),
	.codec_conf = audmixer_codec_conf,
	.num_configs = ARRAY_SIZE(audmixer_codec_conf),
};

static int smdk7870_audio_probe(struct platform_device *pdev)
{
	int n, ret;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *cpu_np, *codec_np, *auxdev_np;
	struct snd_soc_card *card = &smdk7870_cod3025x_card;
	struct cod3025x_machine_priv *priv;

	if (!np) {
		dev_err(&pdev->dev, "Failed to get device node\n");
		return -EINVAL;
	}

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	card->dev = &pdev->dev;
	card->num_links = 0;

	ret = snd_soc_register_component(card->dev, &smdk7870_cmpnt,
			smdk7870_ext_dai,
			ARRAY_SIZE(smdk7870_ext_dai));
	if (ret) {
		dev_err(&pdev->dev, "Failed to register component: %d\n", ret);
		return ret;
	}

	for (n = 0; n < ARRAY_SIZE(smdk7870_cod3025x_dai); n++) {
		/* Skip parsing DT for fully formed dai links */
		if (smdk7870_cod3025x_dai[n].platform_name &&
				smdk7870_cod3025x_dai[n].codec_name) {
			dev_dbg(card->dev,
			"Skipping dt for populated dai link %s\n",
			smdk7870_cod3025x_dai[n].name);
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

		smdk7870_cod3025x_dai[n].codec_of_node = codec_np;
		if (!smdk7870_cod3025x_dai[n].cpu_dai_name)
			smdk7870_cod3025x_dai[n].cpu_of_node = cpu_np;
		if (!smdk7870_cod3025x_dai[n].platform_name)
			smdk7870_cod3025x_dai[n].platform_of_node = cpu_np;

		card->num_links++;
	}

	for (n = 0; n < ARRAY_SIZE(audmixer_aux_dev); n++) {
		auxdev_np = of_parse_phandle(np, "samsung,auxdev", n);
		if (!auxdev_np) {
			dev_err(&pdev->dev,
				"Property 'samsung,auxdev' missing\n");
			return -EINVAL;
		}

		audmixer_aux_dev[n].codec_of_node = auxdev_np;
		audmixer_codec_conf[n].of_node = auxdev_np;
	}

	snd_soc_card_set_drvdata(card, priv);

	ret = snd_soc_register_card(card);
	if (ret)
		dev_err(&pdev->dev, "Failed to register card:%d\n", ret);

	return ret;
}

static int smdk7870_audio_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);

	return 0;
}

static const struct of_device_id smdk7870_cod3026x_of_match[] = {
	{.compatible = "samsung,smdk7870-cod3026x",},
	{},
};
MODULE_DEVICE_TABLE(of, smdk7870_cod3026x_of_match);

static struct platform_driver smdk7870_audio_driver = {
	.driver = {
		.name = "Smdk7870-audio",
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table = of_match_ptr(smdk7870_cod3026x_of_match),
	},
	.probe = smdk7870_audio_probe,
	.remove = smdk7870_audio_remove,
};

module_platform_driver(smdk7870_audio_driver);

MODULE_DESCRIPTION("ALSA SoC Smdk7870 COD3025X");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:smdk7870-audio");
