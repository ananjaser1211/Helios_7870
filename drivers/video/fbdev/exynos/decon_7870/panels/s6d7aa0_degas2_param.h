#ifndef __S6D7AA0_PARAM_H__
#define __S6D7AA0_PARAM_H__
#include <linux/types.h>
#include <linux/kernel.h>

#define UI_MAX_BRIGHTNESS	255
#define UI_MIN_BRIGHTNESS	0
#define UI_DEFAULT_BRIGHTNESS	128
#define NORMAL_TEMPERATURE	25	/* 25 degrees Celsius */

struct lcd_seq_info {
	unsigned char	*cmd;
	unsigned int	len;
	unsigned int	sleep;
};

static const unsigned char SEQ_PASSWD1[] = {
	0xF0,
	0x5A, 0x5A
};

static const unsigned char SEQ_PASSWD2[] = {
	0xF1,
	0x5A, 0x5A
};

static const unsigned char SEQ_PASSWD3[] = {
	0xFC,
	0xA5, 0xA5
};

static const unsigned char SEQ_PASSWD1_LOCK[] = {
	0xF0,
	0xA5, 0xA5
};

static const unsigned char SEQ_PASSWD2_LOCK[] = {
	0xF1,
	0xA5, 0xA5
};

static const unsigned char SEQ_PASSWD3_LOCK[] = {
	0xFC,
	0x5A, 0x5A
};

static const unsigned char SEQ_OTP_RELOAD[] = {
	0xD0,
	0x00, 0x10
};

static const unsigned char SEQ_BC_PARAM_MDNIE[] = {
	0xBC,
	0x01, 0x4E, 0x0A
};

static const unsigned char SEQ_FD_PARAM_MDNIE[] = {
	0xFD,
	0x16, 0x10, 0x11, 0x23, 0x09
};

static const unsigned char SEQ_FE_PARAM_MDNIE[] = {
	0xFE,
	0x00, 0x02, 0x03, 0x21, 0x00, 0x78
};

static const unsigned char SEQ_B3_PARAM[] = {
	0xB3,
	0x51, 0x00
};

static const unsigned char SEQ_BACKLIGHT_CTL[] = {
	0x53,
	0x2C, 0x00
};

static const unsigned char SEQ_PORCH_CTL[] = {
	0xF2,
	0x02, 0x08, 0x08
};

static const unsigned char SEQ_BL_ON_CTL[] = {
	0xC3,
	0x5B, 0x00, 0x2A	// PMIC_ON, PWM 22.08 kHz
};

static const unsigned char SEQ_BL_OFF_CTL[] = {
	0xC3,
	0x5B, 0x00, 0x20
};

static const unsigned char SEQ_TEON_CTL[] = {
	0x35,
	0x00, 0x00
};

static const unsigned char SEQ_SLEEP_OUT[] = {
	0x11,
	0x00, 0x00
};

const const unsigned char SEQ_SLEEP_IN[] = {
	0x10,
	0x00, 0x00
};

const const unsigned char SEQ_DISPLAY_OFF[] = {
	0x28,
	0x00, 0x00
};

static const unsigned char SEQ_DISPLAY_ON[] = {
	0x29,
	0x00, 0x00
};


#endif /* __S6D7AA0_PARAM_H__ */
