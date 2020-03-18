#ifndef __S6D78A0_PARAM_H__
#define __S6D78A0_PARAM_H__
#include <linux/types.h>
#include <linux/kernel.h>

#define UI_DEFAULT_BRIGHTNESS	144
#define UI_MAX_BRIGHTNESS	255
#define EXTEND_BRIGHTNESS	306

#define S6D78A0_ID_REG		0xDA	/* LCD ID1,ID2,ID3 */
#define S6D78A0_ID_LEN		3

struct lcd_seq_info {
	unsigned char	*cmd;
	unsigned int	len;
	unsigned int	sleep;
};

/*Initializing  Sequence(1*/
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


/*Initializing  Sequence(2) */
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


/* SEQ_SLEEP_OUT Sequence*/
static const unsigned char SEQ_SLEEP_OUT[] = {
	0x11
};

static const unsigned char SEQ_DISPLAY_ON[] = {
	0x29
};

/* Initializing Sequence(3) */

static const unsigned char SEQ_INTERNAL_CLK[] = {
	0xB1,
	0x93
};

static const unsigned char SEQ_PANEL_PROTECTION[] = {
	0xB5,
	0x10
};

static const unsigned char SEQ_INTERNAL_POWER[] = {
	0xF4,
	0x01, 0x10, 0x32, 0x00, 0x24, 0x26, 0x28, 0x27, 0x27, 0x27,
	0xB7, 0x2B, 0x2C, 0x65, 0x6A, 0x34, 0x20
};

static const unsigned char SEQ_GOA_TIMING[] = {
	0xEF,
	0x01, 0x01, 0x81, 0x22, 0x83, 0x04, 0x05, 0x00, 0x00, 0x00,
	0x28, 0x81, 0x00, 0x21, 0x21, 0x03, 0x03, 0x40, 0x00, 0x10
};

static const unsigned char SEQ_INTERNAL_PORCH[] = {
	0xF2,
	0x19, 0x04, 0x08, 0x08, 0x08, 0x3F, 0x23, 0x00
};

static unsigned char SEQ_SOURCE_CTL[] = {
	0xF6,
	0x63, 0x23, 0x15, 0x00, 0x00, 0x06
};

static const unsigned char SEQ_MIPI_ABNORMAL_DETECT[] = {
	0xE1,
	0x01, 0xFF, 0x01, 0x1B, 0x20, 0x17
};

static unsigned char SEQ_MIPI_RECOVER[] = {
	0xE2,
	0xED, 0xC7, 0x23
};

static unsigned char SEQ_GOA_OUTPUT[] = {
	0xF7,
	0x01, 0x01, 0x01, 0x0A, 0x0B, 0x00, 0x1A, 0x05, 0x1B, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x08, 0x09, 0x00, 0x1A, 0x04, 0x1B, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01
};

static const unsigned char SEQ_MEM_DATA_ACCESS[] = {
	0x36,
	0x10
};

static const unsigned char SEQ_POSITIVE_GAMMA[] = {
	0xFA,
	0x11, 0x3B, 0x33, 0x2A, 0x1A, 0x09, 0x08, 0x04, 0x03, 0x07,
	0x05, 0x06, 0x17, 0x12, 0x3B, 0x33, 0x2A, 0x1D, 0x0B, 0x07,
	0x04, 0x03, 0x07, 0x05, 0x07, 0x17, 0x10, 0x3B, 0x33, 0x2A,
	0x1A, 0x07, 0x05, 0x02, 0x02, 0x05, 0x03, 0x04, 0x15
};

static const unsigned char SEQ_NEGATIVE_GAMMA[] = {
	0xFB,
	0x11, 0x3B, 0x33, 0x2A, 0x1A, 0x0A, 0x08, 0x04, 0x03, 0x06,
	0x05, 0x06, 0x17, 0x12, 0x3B, 0x33, 0x2A, 0x1D, 0x0B, 0x07,
	0x04, 0x03, 0x07, 0x05, 0x07, 0x17, 0x10, 0x3B, 0x33, 0x2A,
	0x1A, 0x08, 0x05, 0x02, 0x02, 0x05, 0x03, 0x04, 0x14
};

static const unsigned char SEQ_DISPLAY_OFF[] = {
	0x28
};

static const unsigned char SEQ_SLEEP_IN[] = {
	0x10
};

/* platform brightness <-> bl reg */
static unsigned int brightness_table[EXTEND_BRIGHTNESS + 1] = {
	 0,
	 0, 0, 0, 0, 2, 2, 2, 2, 2, 2, /* 5: 2 */
	 2, 2, 2, 2, 3, 3, 3, 3, 3, 3,
	 3, 3, 3, 3, 4, 4, 4, 4, 4, 4,
	 4, 4, 4, 4, 5, 5, 5, 5, 5, 5,
	 5, 5, 5, 5, 6, 6, 6, 6, 6, 6,
	 6, 6, 6, 6, 7, 7, 7, 7, 7, 7,
	 7, 7, 7, 7, 8, 8, 8, 8, 8, 8,
	 8, 8, 8, 8, 9, 9, 9, 9, 9, 9,
	 9, 9, 9, 9, 10, 10, 10, 10, 10, 10,
	 10, 10, 10, 10, 11, 11, 11, 11, 11, 11,
	 11, 11, 11, 11, 12, 12, 12, 12, 12, 12,
	 12, 12, 12, 12, 13, 13, 13, 13, 13, 13,
	 13, 13, 13, 13, 14, 14, 14, 14, 14, 14,
	 14, 14, 14, 14, 15, 15, 15, 15, 15, 15,
	 15, 15, 15, 16, 16, 16, 16, 16, 16, 16, /* 144: 16 */
	 16, 16, 16, 16, 17, 17, 17, 17, 17, 17,
	 17, 17, 17, 17, 18, 18, 18, 18, 18, 18,
	 18, 18, 18, 18, 19, 19, 19, 19, 19, 19,
	 19, 19, 19, 19, 20, 20, 20, 20, 20, 20,
	 20, 20, 20, 20, 21, 21, 21, 21, 21, 21,
	 21, 21, 21, 21, 22, 22, 22, 22, 22, 22,
	 22, 22, 22, 22, 23, 23, 23, 23, 23, 23,
	 23, 23, 23, 23, 24, 24, 24, 24, 24, 24,
	 24, 24, 24, 24, 25, 25, 25, 25, 25, 25,
	 25, 25, 25, 25, 26, 26, 26, 26, 26, 26,
	 26, 26, 26, 26, 27, 27, 27, 27, 27, 27, /* 255: 27 */
	 27, 27, 27, 27, 27, 27, 27, 27, 27, 27,
	 27, 27, 27, 27, 27, 27, 27, 27, 27, 27,
	 27, 27, 27, 27, 27, 27, 27, 27, 27, 27,
	 27, 27, 27, 27, 27, 27, 27, 27, 27, 27,
	 27, 27, 27, 27, 27, 32,
};

#endif /* __S6D78A0_PARAM_H__ */
