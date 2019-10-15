#ifndef FIMC_IS_OTPROM_REAR_SR544_V001_H
#define FIMC_IS_OTPROM_REAR_SR544_V001_H

/* OTPROM I2C Addr Section */
#define OTP_I2C_HEADER_VERSION_START_ADDR      0x30
#define OTP_I2C_HEADER_CAL_MAP_VER_START_ADDR  0x40

/* Header Offset Addr Section */
#define OTP_HEADER_VERSION_START_ADDR      0x30
#define OTP_HEADER_CAL_MAP_VER_START_ADDR  0x40
#define OTP_HEADER_OEM_START_ADDR          0x0
#define OTP_HEADER_OEM_END_ADDR            0x4
#define OTP_HEADER_AWB_START_ADDR          0x8
#define OTP_HEADER_AWB_END_ADDR            0xC
#define OTP_HEADER_AP_SHADING_START_ADDR   0x10
#define OTP_HEADER_AP_SHADING_END_ADDR	   0x14
#define OTP_HEADER_PROJECT_NAME_START_ADDR 0x48

/* OEM referenced section */
#define OTP_OEM_VER_START_ADDR				0x60

/* AWB referenced section */
#define OTP_AWB_VER_START_ADDR				0xE0

/* AP Shading referenced section */
#define OTP_AP_SHADING_VER_START_ADDR		0x120

/* Checksum referenced section */
#define OTP_CHECKSUM_HEADER_ADDR           0x5C
#define OTP_CHECKSUM_OEM_ADDR              0xDC
#define OTP_CHECKSUM_AWB_ADDR              0x11C
#define OTP_CHECKSUM_AP_SHADING_ADDR       0x83C

/* etc section */
#define FIMC_IS_MAX_CAL_SIZE               (8 * 1024)
#define FIMC_IS_MAX_FW_SIZE                (8 * 1024)
#define FIMC_IS_MAX_SETFILE_SIZE           (1120 * 1024)

#define HEADER_CRC32_LEN					(0x5C)
#define OEM_CRC32_LEN						(0x70)
#define AWB_CRC32_LEN						(0x30)
#define SHADING_CRC32_LEN					(0x710)

#define OTPROM_AF_CAL_PAN_ADDR             0x60
#define OTPROM_AF_CAL_MACRO_ADDR           0x68

/* OTPROM Value */
#define OTP_USED_CAL_SIZE				0x0850
#define OTP_SETTING_DELAY				0xFFFF
#define OTP_START_ADDR_HIGH				0x010A
#define OTP_START_ADDR_LOW				0x010B
#define OTP_SINGLE_READ					0x0102
#define OTP_SINGLE_READ_ADDR			0x0108

#define OTP_NEED_INIT_SETTING
#define OTP_BANK

#ifdef OTP_BANK
#define OTP_BANK_ADDR_HIGH				0x06
#define OTP_BANK_ADDR_LOW				0x80

#define OTP_BANK1_START_ADDR_HIGH 		0x06
#define OTP_BANK1_START_ADDR_LOW		0x90
#define OTP_BANK2_START_ADDR_HIGH 		0x0E
#define OTP_BANK2_START_ADDR_LOW		0xE0
#define OTP_BANK3_START_ADDR_HIGH		0x17
#define OTP_BANK3_START_ADDR_LOW		0x30
#endif

#define OTP_MODE_CHANGE

#ifdef OTP_MODE_CHANGE
static const u32 sensor_mode_change_to_OTP_reg[] = {
	0x0A02, 0x01, 0x1,
	0x0118, 0x00, 0x1,
	OTP_SETTING_DELAY, 100, 0x1,
	0x0F02, 0x00, 0x1,
	0x011A, 0x01, 0x1,
	0x011B, 0x09, 0x1,
	0x0D04, 0x01, 0x1,
	0x0D00, 0x07, 0x1,
	0x004C, 0x01, 0x1,
	0x003E, 0x01, 0x1,
	0x0118, 0x01, 0x1,
};

static const u32 sensor_mode_change_to_OTP_reg_size =
	sizeof(sensor_mode_change_to_OTP_reg) / sizeof(sensor_mode_change_to_OTP_reg[0]);

static const u32 sensor_mode_change_from_OTP_reg[] = {
	0x0118, 0x00, 0x1,
	0x003E, 0x00, 0x1,
	0x0118, 0x01, 0x1,
};

static const u32 sensor_mode_change_from_OTP_reg_size =
	sizeof(sensor_mode_change_from_OTP_reg) / sizeof(sensor_mode_change_from_OTP_reg[0]);
#endif

#endif /* FIMC_IS_OTPROM_REAR_SR544_V001_H */
