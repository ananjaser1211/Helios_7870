#ifndef __S6D7AA0_PARAM_H__
#define __S6D7AA0_PARAM_H__
#include <linux/types.h>
#include <linux/kernel.h>

#define UI_MAX_BRIGHTNESS	255
#define UI_DEFAULT_BRIGHTNESS	115

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
	0x00, 0x02, 0x03, 0x21, 0x00, 0x58
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

static unsigned int brightness_table[UI_MAX_BRIGHTNESS + 1] = {
	0,
	3, 3, 3, 4, 5, 6, 7, 8, 9, 10, /* 3: 3 */
	11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
	21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
	31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
	41, 42, 43, 44, 45, 46, 47, 48, 49, 50,
	51, 52, 53, 54, 55, 56, 57, 58, 59, 60,
	61, 62, 63, 64, 65, 66, 67, 68, 69, 70,
	71, 72, 73, 74, 75, 76, 77, 78, 79, 80,
	81, 82, 83, 84, 85, 86, 87, 88, 89, 90,
	91, 92, 93, 94, 95, 96, 97, 98, 99, 100,
	101, 102, 103, 104, 105, 106, 107, 108, 109, 110,
	111, 112, 113, 114, 115, 116, 117, 118, 119, 120, /* 115: 115 */
	121, 122, 123, 124, 125, 126, 127, 128, 129, 130,
	131, 132, 133, 134, 135, 136, 137, 138, 139, 140,
	141, 142, 143, 144, 145, 146, 147, 148, 149, 150,
	151, 152, 153, 154, 155, 156, 157, 158, 159, 160,
	161, 162, 163, 164, 165, 166, 167, 168, 169, 170,
	171, 172, 173, 174, 175, 176, 177, 178, 179, 180,
	181, 182, 183, 184, 185, 186, 187, 188, 189, 190,
	191, 192, 193, 194, 195, 196, 197, 198, 199, 200,
	201, 202, 203, 204, 205, 206, 207, 208, 209, 210,
	211, 212, 213, 214, 215, 216, 217, 218, 219, 220,
	221, 222, 223, 224, 225, 226, 227, 228, 229, 230,
	231, 232, 233, 234, 235, 236, 237, 238, 239, 240,
	241, 242, 243, 244, 245, 246, 247, 248, 249, 250,
	251, 252, 253, 254, 255,
};

#endif /* __S6D7AA0_PARAM_H__ */
