#ifndef __EA8061S_PARAM_H__
#define __EA8061S_PARAM_H__

#include <linux/types.h>
#include <linux/kernel.h>

struct lcd_seq_info {
	unsigned char	*cmd;
	unsigned int	len;
	unsigned int	sleep;
};

enum {
	HBM_STATUS_OFF,
	HBM_STATUS_ON,
	HBM_STATUS_MAX,
};

enum {
	ACL_STATUS_0P,
	ACL_STATUS_15P,
	ACL_STATUS_MAX
};

enum {
	ACL_OPR_16_FRAME,
	ACL_OPR_32_FRAME,
	ACL_OPR_MAX
};

#define POWER_IS_ON(pwr)		(pwr <= FB_BLANK_NORMAL)
#define LEVEL_IS_HBM(brightness)	(brightness == EXTEND_BRIGHTNESS)
#define UNDER_MINUS_20(temperature)	(temperature <= -20)
#define ACL_IS_ON(nit)			(nit != 360)

#define NORMAL_TEMPERATURE	25	/* 25 degrees Celsius */
#define EXTEND_BRIGHTNESS	355
#define UI_MAX_BRIGHTNESS	255
#define UI_MIN_BRIGHTNESS	0
#define UI_DEFAULT_BRIGHTNESS	134

#define MTP_ADDR		0xC8
#define MTP_SIZE		33
#define ID_REG			0x04
#define ID_LEN			3
#define DATE_REG		0xA1
#define DATE_LEN		11
#define TSET_REG		SEQ_TSET[0]
#define TSET_DEF		SEQ_TSET[1]
#define TSET_LEN		ARRAY_SIZE(SEQ_TSET)

#define ELVSS_READ_ADDR		0xB6
#define ELVSS_READ_SIZE		8

#define GAMMA_CMD_CNT			ARRAY_SIZE(SEQ_GAMMA_CONDITION_SET)
#define AID_CMD_CNT		ARRAY_SIZE(SEQ_AID_SET)

static const unsigned char SEQ_SOURCE_SLEW[] = {
	0xBA,
	0x77,
};

static const unsigned char SEQ_S_WIRE[] = {
	0xB8,
	0x19, 0x00,
};

static const unsigned char SEQ_GAMMA_CONDITION_SET[] = {
	0xCA,
	0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x80, 0x80, 0x80, 0x80,
	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
	0x00, 0x00, 0x00
};

static const unsigned char SEQ_AID_SET[] = {
	0xB2,
	0x00, 0x00, 0x00, 0x0C,
};

static const unsigned char SEQ_ELVSS_SET[] = {
	0xB6,
	0xDC, 0x84,
};

static const unsigned char SEQ_GAMMA_UPDATE[] = {
	0xF7,
	0x03,
};

static const unsigned char SEQ_HBM_OFF[] = {
	0x53,
	0x00,
};

static const unsigned char SEQ_HBM_ON[] = {
	0x53,
	0xC0,
};

static const unsigned char SEQ_ACL_OFF[] = {
	0x55,
	0x00,
};

static const unsigned char SEQ_ACL_15[] = {
	0x55,
	0x02,
};

/* Tset 25 degree */
static const unsigned char SEQ_TSET[] = {
	0xB8,
	0x19,
};

static const unsigned char SEQ_SLEEP_IN[] = {
	0x10,
};

static const unsigned char SEQ_SLEEP_OUT[] = {
	0x11,
};

static const unsigned char SEQ_DISPLAY_ON[] = {
	0x29,
};

static const unsigned char SEQ_DISPLAY_OFF[] = {
	0x28,
};

static const unsigned char SEQ_TEST_KEY_ON_F0[] = {
	0xF0,
	0x5A, 0x5A,
};

static const unsigned char SEQ_TEST_KEY_OFF_F0[] = {
	0xF0,
	0xA5, 0xA5,
};

static const unsigned char SEQ_TEST_KEY_ON_F1[] = {
	0xF1,
	0x5A, 0x5A,
};

static const unsigned char SEQ_TEST_KEY_OFF_F1[] = {
	0xF1,
	0xA5, 0xA5,
};

static const unsigned char SEQ_TEST_KEY_ON_FC[] = {
	0xFC,
	0x5A, 0x5A,
};

static const unsigned char SEQ_TEST_KEY_OFF_FC[] = {
	0xFC,
	0xA5, 0xA5,
};

static const unsigned char SEQ_HSYNC_GEN_ON[] = {
	0xCF,
	0x30, 0x09,
};

#endif /* __EA8061S_PARAM_H__ */

