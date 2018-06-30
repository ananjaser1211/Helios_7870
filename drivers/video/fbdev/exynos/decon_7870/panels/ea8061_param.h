#ifndef __EA8061_PARAM_H__
#define __EA8061_PARAM_H__

#include <linux/types.h>
#include <linux/kernel.h>

struct lcd_seq_info {
	unsigned char	*cmd;
	unsigned int	len;
	unsigned int	sleep;
};

#define EA8061_READ_TX_REG			0xFD
#define EA8061_READ_RX_REG			0xFE
#define EA8061_ID_REG				0xD1
#define EA8061_ID_LEN				3

enum {
	HBM_STATUS_OFF,
	HBM_STATUS_ON,
	HBM_STATUS_MAX,
};

enum {
	ACL_STATUS_0P,
	ACL_STATUS_8P,
	ACL_STATUS_MAX
};

enum {
	ACL_OPR_16_FRAME,
	ACL_OPR_32_FRAME,
	ACL_OPR_MAX
};

#define POWER_IS_ON(pwr)			(pwr <= FB_BLANK_NORMAL)
#define LEVEL_IS_HBM(brightness)		(brightness == EXTEND_BRIGHTNESS)
#define UNDER_MINUS_20(temperature)	(temperature <= -20)
#define ACL_IS_ON(nit)			(nit != 350)

#define NORMAL_TEMPERATURE			25	/* 25 degrees Celsius */
#define EXTEND_BRIGHTNESS	355
#define UI_MAX_BRIGHTNESS	255
#define UI_MIN_BRIGHTNESS	0
#define UI_DEFAULT_BRIGHTNESS	134

#define EA8061_MTP_ADDR				0xDA
#define EA8061_MTP_SIZE				32

#define EA8061_MTP_DB_ADDR			0xDB
#define EA8061_MTP_DB_SIZE			56

#define EA8061_MTP_B2_ADDR			0xB2
#define EA8061_MTP_B2_SIZE			7

#define EA8061_MTP_D4_ADDR			0xD4
#define EA8061_MTP_D4_SIZE			18

#define EA8061_DATE_SIZE			2

#define EA8061_MTP_DATE_SIZE		EA8061_MTP_SIZE
#define EA8061_COORDINATE_REG		0xA1
#define EA8061_COORDINATE_LEN		4
#define EA8061_HBMGAMMA_REG		0xB4
#define EA8061_HBMGAMMA_LEN		31
#define HBM_INDEX					65
#define EA8061_CODE_REG			0xD6
#define EA8061_CODE_LEN			5
#define TSET_REG			0xB8
#define TSET_LEN			3
#define TSET_MINUS_OFFSET			0x03
#define ELVSS_REG			0xB6
#define ELVSS_LEN			18   /* elvss: Global para 4th */

#define GAMMA_CMD_CNT		33
#define AID_CMD_CNT			5
#define ELVSS_CMD_CNT		3

/* EA8061 */
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

static const unsigned char SEQ_DISPLAY_ON[] = {
	0x29,
	0x00, 0x00
};

static const unsigned char SEQ_DISPLAY_OFF[] = {
	0x28,
	0x00,  0x00
};

static const unsigned char SEQ_SLEEP_IN[] = {
	0x10,
	0x00, 0x00
};

static const unsigned char SEQ_SLEEP_OUT[] = {
	0x11,
	0x00, 0x00
};

static const unsigned char SEQ_EA8061_LTPS_STOP[] = {
	0xF7,
	0x5A, 0x5A
};

static const unsigned char SEQ_EA8061_LTPS_TIMING[] = {
	0xC4,
	0x51, 0xAD, 0x51, 0xAD, 0x60, 0x95, 0x60, 0x95,
	0x00, 0x00, 0x0B, 0xF1, 0x00, 0x0B, 0xF1, 0x00,
	0x00, 0x08, 0x08, 0x08, 0x34, 0x64, 0xA5, 0x00,
	0x00, 0x0A, 0x04, 0x03, 0x00, 0x0C, 0x00
};

static const unsigned char SEQ_EA8061_LTPS_UPDATE[] = {
	0xF7,
	0xA5, 0xA5
};

static const unsigned char SEQ_EA8061_SCAN_DIRECTION[] = {
	0x36,
	0x02
};

static const unsigned char SEQ_EA8061_AID_SET_DEFAULT[] = {
	0xB3,
	0x00, 0x00, 0x00, 0x30
};

static const unsigned char SEQ_EA8061_SLEW_CONTROL[] = {
	0xB4,
	0x33, 0x07, 0x00
};

static const unsigned char SEQ_EA8061_GAMMA_350CD[] = {
	0xCA,
	0x01, 0x00, 0x01, 0x00, 0x01, 0x00, /* V255 R,G,B*/
	0x80, 0x80, 0x80, /* V203 R,G,B*/
	0x80, 0x80, 0x80, /* V151 R,G,B*/
	0x80, 0x80, 0x80, /* V87 R,G,B*/
	0x80, 0x80, 0x80, /* V51 R,G,B*/
	0x80, 0x80, 0x80, /* V35 R,G,B*/
	0x80, 0x80, 0x80, /* V23 R,G,B*/
	0x80, 0x80, 0x80, /* V11 R,G,B*/
	0x80, 0x80, 0x80, /* V3 R,G,B*/
	0x00, 0x00 /* VT R,G,B*/
};

static const unsigned char SEQ_EA8061_AID_SET_MAX[] = {
	0xB3,
	0x00, 0x30, 0x00, 0x30
};

static const unsigned char SEQ_EA8061_ELVSS_SET_MAX[] = {
	0xB2,
	0x0F,
};

static const unsigned char SEQ_EA8061_ELVSS_SET_HBM[] = {
	0xB2,
	0x0F, 0xB4, 0xA0, 0x13, 0x00, 0x00, 0x00,
};

static const unsigned char SEQ_EA8061_ELVSS_SET_HBM_D4[] = {
	0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x60, 0x66, 0xC6, 0x0C,
};

static const unsigned char SEQ_EA8061_MPS_SET_MAX[] = {
	0xD4,
	0x38, 0x00, 0x48
};

static const unsigned char SEQ_EA8061_READ_ID[] = {
	EA8061_READ_TX_REG,
	EA8061_ID_REG
};

static const unsigned char SEQ_EA8061_READ_MTP[] = {
	EA8061_READ_TX_REG,
	EA8061_MTP_ADDR
};

static const unsigned char SEQ_EA8061_READ_ELVSS[] = {
	EA8061_READ_TX_REG,
	0xD4
};

static const unsigned char SEQ_EA8061_READ_DB[] = {
	EA8061_READ_TX_REG,
	EA8061_MTP_DB_ADDR
};

static const unsigned char SEQ_EA8061_READ_B2[] = {
	EA8061_READ_TX_REG,
	EA8061_MTP_B2_ADDR
};

static const unsigned char SEQ_EA8061_READ_D4[] = {
	EA8061_READ_TX_REG,
	EA8061_MTP_D4_ADDR
};



static const unsigned char SEQ_GAMMA_CONDITION_SET[] = {
	0xCA,
	0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x80, 0x80, 0x80, 0x80,
	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
	0x00, 0x00,
};

static const unsigned char SEQ_AID_SET[] = {
	0xB2,
	0x00, 0x0E, 0x00, 0x0E,
};

static const unsigned char SEQ_ELVSS_SET[] = {
	0xB6,
	0x98, 0x0A,
};

static const unsigned char SEQ_CAPS_ELVSS_SET[] = {
	0xB6,
	0x98, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x55, 0x54,
	0x20, 0x00, 0x08, 0x88, 0x8F, 0x0F, 0x02, 0x11, 0x11, 0x10
};

static const unsigned char SEQ_CAPS_ELVSS_DEFAULT[] = {
	0x0F, 0xB4, 0xA0, 0x13, 0x00, 0x00, 0x00
};

static const unsigned char SEQ_GAMMA_UPDATE[] = {
	0xF7,
	0x03
};

static const unsigned char SEQ_GAMMA_UPDATE_L[] = {
	0xF7,
	0x00
};

static const unsigned char SEQ_GAMMA_UPDATE_EA8064G[] = {
	0xF7,
	0x01
};

static const unsigned char SEQ_HBM_OFF[] = {
	0x53,
	0x00
};

static const unsigned char SEQ_HBM_ON[] = {
	0x53,
	0xD0
};

static const unsigned char SEQ_ACL_SET[] = {
	0x55,
	0x02
};

static const unsigned char SEQ_ACL_OFF[] = {
	0x55,
	0x00
};

static const unsigned char SEQ_ACL_15[] = {
	0x55,
	0x02,
};

static const unsigned char SEQ_ACL_OFF_OPR_AVR[] = {
	0xB5,
	0x41
};

static const unsigned char SEQ_ACL_ON_OPR_AVR[] = {
	0xB5,
	0x51
};

static const unsigned char SEQ_TSET_GLOBAL[] = {
	0xB0,
	0x05
};

static const unsigned char SEQ_TSET[] = {
	0xB8,
	0x19,
};

static const unsigned char SEQ_MONITOR_GLOBAL[] = {
	0xB0,
	0x05,
};

static const unsigned char SEQ_MONITOR_0[] = {
	0xD7,
	0x0A,
};

static const unsigned char SEQ_MONITOR_1[] = {
	0xFF,
	0x0A,
};

static const unsigned char SEQ_SET_READ_ADDRESS[] = {
	0xFD,
	0x00, /* Adress to be read */
};

static const unsigned char SEQ_READ_ENABLE[] = {
	0xFE,
	0x00, /* Para.Number to be read */
};

static const unsigned char SEQ_GP_01[] = {
	0xB0,
	0x01
};

static const unsigned char SEQ_GP_02[] = {
	0xB0,
	0x02
};

static const unsigned char SEQ_F3_ON[] = {
	0xF3,
	0x01
};

static const unsigned char SEQ_F3_08[] = {
	0xF3,
	0x08
};

static const unsigned char SEQ_F3_OFF[] = {
	0xF3,
	0x00
};

#endif /* __S6D7AA0X62_PARAM_H__ */
