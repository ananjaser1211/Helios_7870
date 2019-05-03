#ifndef __SOUND_EXYNOS_AUDMIXER_H__
#define __SOUND_EXYNOS_AUDMIXER_H__

#include <sound/pcm_params.h>

enum audmixer_if_t {
	AUDMIXER_IF_AP,
	AUDMIXER_IF_CP,
	AUDMIXER_IF_BT,
	AUDMIXER_IF_FM,
	AUDMIXER_IF_ADC,
	AUDMIXER_IF_AP1,
	AUDMIXER_IF_CP1,
	AUDMIXER_IF_COUNT,
};

#ifdef CONFIG_SND_SOC_EXYNOS_AUDMIXER
/**
 * audmixer_get_sync(): Call runtime resume function of mixer. The codec driver
 * requires the mixer driver to be operational before it gets configured. The
 * codec driver can make use of this API to power on the mixer driver before
 * starting its own resume sequence.
 */
void audmixer_get_sync(void);

/**
* audmixer_put_sync(): Call runtime suspend function of mixer. This function is
 * the counterpart of s2801x_put_sync() function and it is designed to balance
 * the resume call made through s2801x_get_sync().
 */
void audmixer_put_sync(void);

/**
 * is_cp_aud_enabled(void): Checks the current status of CP path
 *
 * Returns true if CP audio path is enabled, false otherwise.
 */
bool is_cp_aud_enabled(void);
#else
void audmixer_get_sync(void)
{
}

void audmixer_put_sync(void)
{
}

bool is_cp_aud_enabled(void)
{
	return false;
}
#endif

#endif /* __SOUND_EXYNOS_AUDMIXER_H__ */
