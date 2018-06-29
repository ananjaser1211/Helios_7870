#ifndef __S2MU005_REG_H__
 #define __S2MU005_REG_H__

 
#define S2MU005_RGB_LED_DRIVER_NAME 	"leds-s2mu005-rgb"

#define SEC_LED_SPECIFIC
//#define S2MU005_ENABLE_INDIVIDUAL_CURRENT

#ifdef CONFIG_SEC_FACTORY
#define CONFIG_SEC_SVCLED_POWERON_GLOW
#endif //CONFIG_SEC_FACTORY


/////////////// REGISTER MACROS /////////////////////////
#define S2MU005_LED_BASE_REG 				0x3D
#define S2MU005_LED_ENABLE_REG 				0x3D
#define S2MU005_LEDx_CURRENT_REG			0x3E
#define S2MU005_LED1_CURRENT_REG 			0x3E
#define S2MU005_LED2_CURRENT_REG 			0x3F
#define S2MU005_LED3_CURRENT_REG 			0x40

#define S2MU005_LED_R_CURRENT_REG 			S2MU005_LED3_CURRENT_REG
#define S2MU005_LED_G_CURRENT_REG 			S2MU005_LED2_CURRENT_REG
#define S2MU005_LED_B_CURRENT_REG 			S2MU005_LED1_CURRENT_REG

#define S2MU005_LEDx_RAMPDUR_REG 			0x41
#define S2MU005_LED1_RAMP_REG 				0x41
#define S2MU005_LED1_DUR_REG 				0x42
#define S2MU005_LED2_RAMP_REG 				0x43
#define S2MU005_LED2_DUR_REG 				0x44
#define S2MU005_LED3_RAMP_REG 				0x45
#define S2MU005_LED3_DUR_REG 				0x46
#define S2MU005_LED_CONTROL0_REG 			0x48
#define S2MU005_LED_MAX_REG 				S2MU005_LED_CONTROL0_REG
#define S2MU005_LED_MAX_REG_COUNT			(S2MU005_LED_MAX_REG - S2MU005_LED_BASE_REG)


//#define S2MU005_LED_RESET_OFF				0xBF
#define S2MU005_LED_CONST_CURR				0b00010101
#define S2MU005_LED_SLOPE_CURR				0b00101010
#define S2MU005_LED_RESET_WRITE 			0x40 // write the registers
#define S2MU005_LED_RESET_NORMAL 			0xAA //normal mode(on mode)
//#define S2MU005_LED_RESET_OFF 			0x00
#define S2MU005_LEDx_LOW_CURRENT 			0x3F
#define S2MU005_LEDx_MAX_CURRENT 			0xFF


#define S2MU005_LED_R_MAX_CURRENT 			70
#define S2MU005_LED_G_MAX_CURRENT 			20
#define S2MU005_LED_B_MAX_CURRENT 			100

#define S2MU005_LED_R_LOW_CURRENT 			0xFF
#define S2MU005_LED_G_LOW_CURRENT 			0xFF
#define S2MU005_LED_B_LOW_CURRENT 			0xFF

#define LED_R_MASK 							0xFF
#define LED_G_MASK 							0xFF
#define LED_B_MASK 							0xFF

#define LED_BRIGHTNESS_MASK 				0xFF

#define LED_R_SHIFT 						16
#define LED_G_SHIFT 						8
#define LED_B_SHIFT 						0

#ifdef CONFIG_SEC_SVCLED_POWERON_GLOW
#define JIG_STATUS_FILE_PATH		"/sys/class/sec/switch/is_jig_powered"
#endif


#ifdef SEC_LED_SPECIFIC
/**
* s2mu005_pattern - enum constants for different fixed displayable LED pattern 
**/
enum s2mu005_pattern {
	PATTERN_OFF,
	CHARGING,
	CHARGING_ERR,
	MISSED_NOTI,
	LOW_BATTERY,
	FULLY_CHARGED,
	POWERING,
};

enum s2mu005_LED {
	LED_B,
	LED_G,
	LED_R,
	LED_MAX
};
#endif // SEC_LED_SPECIFIC

/**
* s2mu005_led_tuning_table - Structure to fetch the tuning param from dtb.
* @lcd_type - LCD ID tells about the panel color
* @led_max_current - LED_B, LED_G, LED_G's Normal mode max current value.
* @led_low_current - LED_B, LED_G, LED_G's lowpower mode max current value.
*
* For RGB index, refer the enum enum s2mu005_LED.
**/
struct s2mu005_led_tuning_table{
	u32 lcd_type;
	u32 b_max_current;
	u32 g_max_current;
	u32 r_max_current;
	u32 b_lowpower_current;
	u32 g_lowpower_current;
	u32 r_lowpower_current;
};

/**
* s2mu005_rgb_led - structure to hold all service led related driver data 
* @led_dynamic_current - holds max permissible (tuned) LED current 
*          in the present configuration
* @led_max_current - holds max current of each LED for normal mode
* @led_low_current - holds max current of each LED for lowpower mode
* @led_lowpower_mode - holds whether led is in low power mode
* @type - TBD
* @color - holds presently displaying color on SVC LED.
* @led_on_time - holds blink on time
* @led_off_time - holds blink off time
* @mode - current SEC LED pattern displaying on the phone
* @i2c - holds the parent driver's (PMIC's struct s2mu005_dev)'s I2C client 
*         reference
* @blink_work - work queue to handle the request from user space
* @led_dev - pointer to hold the device attribute to communicate through sysfs
**/
struct s2mu005_rgb_led{
	u8 led_dynamic_current[LED_MAX];
	u8 led_max_current[LED_MAX];
	u8 led_low_current[LED_MAX];  
	u8 led_lowpower_mode;		
	u32	color;
	u32 led_on_time;
    u32 led_off_time;
	enum s2mu005_pattern mode;
	struct i2c_client *i2c;
	struct work_struct blink_work;
	struct device *led_dev;
	//struct s2mu005_dev   s2mu005_dev;
};


#endif //	__S2MU005_REG_H__
