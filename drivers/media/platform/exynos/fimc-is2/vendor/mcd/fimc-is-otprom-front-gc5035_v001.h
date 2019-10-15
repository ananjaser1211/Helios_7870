#ifndef FIMC_IS_OTPROM_FRONT_GC5035_V001_H
#define FIMC_IS_OTPROM_FRONT_GC5035_V001_H

/* Header Offset Addr Section */
//#define OTP_HEADER_DIRECT_ADDR_FRONT

#define HEADER_START_ADDR_FRONT                      0x04
#define OTP_HEADER_CAL_MAP_VER_START_ADDR_FRONT      0x30
#define OTP_HEADER_VERSION_START_ADDR_FRONT          0x20
#define OTP_HEADER_AWB_START_ADDR_FRONT              0x10
#define OTP_HEADER_AWB_END_ADDR_FRONT                0x14

/* AWB referenced section */
/*#define OTP_AWB_VER_START_ADDR_FRONT                 0x200*/

/* Checksum referenced section */
#define OTP_CHECKSUM_HEADER_ADDR_FRONT               0xAC

/* etc section */
#define FIMC_IS_MAX_CAL_SIZE_FRONT                   (8 * 1024)
#define HEADER_CRC32_LEN_FRONT                       (156)
#define OTP_USED_CAL_SIZE                            (256)


#define OTP_PAGE_ADDR                                0x0A02
#define OTP_REG_ADDR_START                           0x0A04
#define OTP_REG_ADDR_MAX                             0x0A43
#define OTP_PAGE_START_ADDR                          0x0A04
#define OTP_START_PAGE                               0x17 
#define HEADER_START_ADDR                            (0x0)

#define OTP_HEADER_PROJECT_NAME_START_ADDR_FRONT     0x38
#define OTP_HEADER_MODULE_ID_ADDR_FRONT              0x86

#define OTP_BANK
#ifdef OTP_BANK
#define OTP_BANK_ADDR                                0x200
#define OTP_START_ADDR                               0x200
static const u32 OTP_first_page_select_reg[] = {
	0x0A00, 0x04, 0x1,
	0x0A02, 0x02, 0x1,
	0x0A00, 0x01, 0x1,
};

static const u32 OTP_first_page_select_reg_size =
	sizeof(OTP_first_page_select_reg) / sizeof(OTP_first_page_select_reg[0]);

static const u32 OTP_second_page_select_reg[] = {
	0x0A00, 0x04, 0x1,
	0x0A02, 0x03, 0x1,
	0x0A00, 0x01, 0x1,
};

static const u32 OTP_second_page_select_reg_size =
	sizeof(OTP_second_page_select_reg) / sizeof(OTP_second_page_select_reg[0]);
#endif

//#define OTP_MODE_CHANGE

#ifdef OTP_MODE_CHANGE
static const u32 sensor_mode_change_to_OTP_reg[] = {
	0x0A00, 0x04, 0x1,
	0x0A02, 0x02, 0x1,
	0x0A00, 0x01, 0x1,
};

static const u32 sensor_mode_change_to_OTP_reg_size =
	sizeof(sensor_mode_change_to_OTP_reg) / sizeof(sensor_mode_change_to_OTP_reg[0]);

static const u32 sensor_mode_change_from_OTP_reg[] = {
	0x0A00, 0x04, 0x1,
	0x0A00, 0x00, 0x1,
};

static const u32 sensor_mode_change_from_OTP_reg_size =
	sizeof(sensor_mode_change_from_OTP_reg) / sizeof(sensor_mode_change_from_OTP_reg[0]);
#endif

static const u32 sensor_mode_read_initial_setting[] = {
	0xfa, 0x10, 0x0,
	0xf5, 0xe9, 0x0,
	0xfe, 0x02, 0x0,
	0x67, 0xc0, 0x0,
	0x59, 0x3f, 0x0,
	0x55, 0x80, 0x0,
	0x65, 0x80, 0x0,
	0x66, 0x03, 0x0,
};

static const u32 sensor_Global_gc5035[] = {
	0xfc, 0x01, 0x00,
	0xf4, 0x40, 0x00,
	0xf5, 0xe9, 0x00,
	0xf6, 0x14, 0x00,
	0xf8, 0x44, 0x00,
	0xf9, 0x82, 0x00,
	0xfa, 0x00, 0x00,
	0xfc, 0x81, 0x00,
	0xfe, 0x00, 0x00,
	0x36, 0x01, 0x00,
	0xd3, 0x87, 0x00,
	0x36, 0x00, 0x00,
	0x33, 0x00, 0x00,
	0xfe, 0x03, 0x00,
	0x01, 0xe7, 0x00,
	0xf7, 0x01, 0x00,
	0xfc, 0x8f, 0x00,
	0xfc, 0x8f, 0x00,
	0xfc, 0x8e, 0x00,
	0xfe, 0x00, 0x00,
	0xee, 0x30, 0x00,
	0x87, 0x18, 0x00,
	0xfe, 0x01, 0x00,
	0x8c, 0x90, 0x00,
	0xfe, 0x00, 0x00,
};

static const u32 sensor_mode_read_initial_setting_size =
    sizeof( sensor_mode_read_initial_setting) / sizeof( sensor_mode_read_initial_setting[0] );
	
static const u32 sensor_Global_gc5035_size =
    sizeof( sensor_Global_gc5035) / sizeof( sensor_Global_gc5035[0] );

#endif /* FIMC_IS_OTPROM_FRONT_GC5035_V001_H */
