#ifndef __S6E8AA5X01_PARAM_H__
#define __S6E8AA5X01_PARAM_H__

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

// 0xC8h Info
// 01~39th : gamma mtp
// 41~45th : manufacture date
// 73~87th : HBM gamma

#define S6E8AA5X01_MTP_ADDR		0xC8
#define S6E8AA5X01_MTP_SIZE		33
#define S6E8AA5X01_MTP_HBM_GAMMA_SIZE		21
#define S6E8AA5X01_MTP_DATE_SIZE		87

#define S6E8AA5X01_MTP_OFFSET_GAMMA		0x00 //0
#define S6E8AA5X01_MTP_OFFSET_DATE		0x28 //40
#define S6E8AA5X01_MTP_OFFSET_HBM		0x48 //72

#define S6E8AA5X01_MTP_SIZE_GAMMA		39
#define S6E8AA5X01_MTP_SIZE_DATE		5
#define S6E8AA5X01_MTP_SIZE_HBM		15

#define S6E8AA5X01_CODE_REG		0xD5
#define S6E8AA5X01_CODE_LEN		5

#define S6E8AA5X01_COORDINATE_REG		0xD7
#define S6E8AA5X01_COORDINATE_LEN		7 /*4~7th*/
#define S6E8AA5X01_ID_REG			0x04
#define S6E8AA5X01_ID_LEN			3

#define S6E8AA5X01_CHIP_ID_REG			0xD5
#define S6E8AA5X01_CHIP_ID_LEN			5

#define TSET_REG			0xB8
#define TSET_LEN			2
#define ELVSS_REG			0xB6
#define ELVSS_MTP_LEN		22
#define ELVSS_LEN			3
#define S6E8AA5X01_DATE_REG		0xD7
#define S6E8AA5X01_DATE_LEN		7	/* 4 ~ 7th para */

#define GAMMA_CMD_CNT			34
#define AID_CMD_CNT			5
#define ELVSS_CMD_CNT			3
#define HBM_GAMMA_CMD_CNT			35


static const unsigned char SEQ_GAMMA_CONDITION_SET[] = {
	0xCA,
	0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x80, 0x80, 0x80, 0x80,
	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
	0x00, 0x00, 0x00
};

static const unsigned char SEQ_AID_SET[] = {
	0xB2,
	0x40, 0x0A, 0x17, 0x00, 0x0A,
};

static const unsigned char SEQ_ELVSS_SET[] = {
	0xB6,
	0x2C, 0x0B,
};

static const unsigned char SEQ_ELVSS_GLOBAL[] = {
	0xB0,
	0x15,
};

static const unsigned char SEQ_HBM_OFF[] = {
	0x53,
	0x00,
};

static const unsigned char SEQ_HBM_ON[] = {
	0x53,
	0xC0,
};

static const unsigned char SEQ_ACL_SET[] = {
	0x55,
	0x01,
};


static const unsigned char SEQ_ACL_15[] = {
	0x55,
	0x02,
};

static const unsigned char SEQ_ACL_ON_OPR_AVR[] = {
	0xB5,
	0x50
};

static const unsigned char SEQ_ACL_OFF_OPR_AVR[] = {
	0xB5,
	0x40
};

/* ACL on 15% */
static const unsigned char SEQ_ACL_SET_S6E88A0[] = {
	0x55,
	0x01,
};

static const unsigned char SEQ_SLEEP_IN[] = {
	0x10,
	0x00, 0x00
};

static const unsigned char SEQ_SLEEP_OUT[] = {
	0x11,
	0x00, 0x00
};

static const unsigned char SEQ_DISPLAY_ON[] = {
	0x29,
	0x00, 0x00
};

static const unsigned char SEQ_DISPLAY_OFF[] = {
	0x28,
	0x00, 0x00
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

static const unsigned char SEQ_PENTILE_SETTING[] = {
	0xC0,
	0xD8, 0xD8, 0x40, /* pentile setting */
};

static const unsigned char SEQ_DE_DIM_GP[] = {
	0xB0,
	0x06, /* Global para(7th) */
};

static const unsigned char SEQ_DE_DIM_SETTING[] = {
	0xB8,
	0xA8, /* DE_DIN On */
};

static const unsigned char SEQ_AID_360NIT[] = {
	0xB2,
	0x00, 0x0F, 0x00, 0x0F,
};

static const unsigned char SEQ_ELVSS_360NIT[] = {
	0xB6,
	0xBC, 0x0F,
};

static const unsigned char SEQ_GAMMA_UPDATE[] = {
	0xF7,
	0x03,
};

static const unsigned char SEQ_GAMMA_UPDATE_L[] = {
	0xF7,
	0x00,
};

static const unsigned char SEQ_ACL_OFF_OPR[] = {
	0xB5,
	0x40, /* 0x40 : at ACL OFF */
};

static const unsigned char SEQ_ACL_OFF[] = {
	0x55,
	0x00, /* 0x00 : ACL OFF */
};

static const unsigned char SEQ_TSET_GP[] = {
	0xB0,
	0x07,
};

static const unsigned char SEQ_TSET[] = {
	0xB8,
	0x19,
};


static const unsigned char SEQ_MTP_READ_DATE_GP[] = {
	0xB0,
	S6E8AA5X01_MTP_OFFSET_DATE,
};

static const unsigned char SEQ_MTP_READ_HBM_GP[] = {
	0xB0,
	S6E8AA5X01_MTP_OFFSET_HBM,
};

#endif /* __S6E8AA5X01_PARAM_H__ */

