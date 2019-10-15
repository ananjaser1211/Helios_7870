#ifndef __SOUND_COD3022X_H__
#define __SOUND_COD3022X_H__

#define COD3026X_MICBIAS1	0
#define COD3026X_MICBIAS2	1

int cod3026x_mic_bias_ev(struct snd_soc_codec *codec,int mic_bias, int event);

#endif /* __SOUND_COD3022X_H__ */
