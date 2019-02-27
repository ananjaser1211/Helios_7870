/*
 * leds-s2mu005-rgb.c - Service notification LED driver based on s2mu005 RGB LED
 *
 * Copyright (C) 2016, Samsung Electronics Co. Ltd. All Rights Reserved.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/workqueue.h>
#include <linux/leds.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/mfd/samsung/s2mu005.h>
#include <linux/mfd/samsung/s2mu005-private.h>
#include <linux/leds-s2mu005.h>
#include <linux/platform_device.h>
#include <linux/sec_batt.h>
#include <linux/sec_sysfs.h>

#include "leds-s2mu005-rgb.h"

/**
* g_s2mu005_rgb_led_data - Holds all service led related driver data  
**/
struct s2mu005_rgb_led *g_s2mu005_rgb_led_data;


/**
* lcdtype - extending the scope of lcdtype to match LED current tuning based on phone color.  
**/
extern unsigned int lcdtype;


/**
* s2mu005_rgb_write_reg - writes LED data to s2mu005 RGB LED registers.
* @reg - register offset
* @val -> data to be written on to the register
**/
static int __inline s2mu005_rgb_write_reg(u8 reg, u8 val)
{
	return s2mu005_write_reg(g_s2mu005_rgb_led_data->i2c, reg, val);
}

/**
* s2mu005_rgb_write_reg - Holds all service led related driver data 
**/
static u8 __inline s2mu005_rgb_dynamic_current(u32 color, u8 dynamic_current)
{
	return ((color * dynamic_current) / S2MU005_LEDx_MAX_CURRENT);
}

/**
* s2mu005_rgb_leds_write_all - writes to s2mu005's rgb registers with user
*        requested color and blink timing. also applies current tuning
* @color - color in 0xRRGGBB format
* @on_time -> blink on time in ms
* @off_time -> blink off time in ms
**/
static int s2mu005_rgb_leds_write_all(u32 color, long int on_time, long int off_time)
{
    u32 ramp_up_time;
    u32 ramp_down_time;
    u32 stable_on_time;
	u32 stable_on_time_value;
	u32 ramp_up_time_value;
	//u32 ramp_down_time_value;
    u32 temp;
#ifdef S2MU005_ENABLE_INDIVIDUAL_CURRENT		
    u8 mode = 0;
    u8 curr;
#endif	

	//  Write RESET to ENABEL register to initialize register writes
	s2mu005_rgb_write_reg(S2MU005_LED_ENABLE_REG, S2MU005_LED_RESET_WRITE);

	// Switch off the LED
	//if(!color || !on_time ) return 0;
	if(!color) return 0;  // ON time DONT CARE


#ifdef S2MU005_ENABLE_INDIVIDUAL_CURRENT
	//  Write LED3 current
	curr = s2mu005_rgb_dynamic_current((u32)(color>>16 & 0xFF), \
		g_s2mu005_rgb_led_data->led_dynamic_current[LED_R]);
	if(curr) {
		s2mu005_rgb_write_reg(S2MU005_LED_R_CURRENT_REG, curr);
		mode = 0b11;
	}
    //  Write LED2 current
	curr = s2mu005_rgb_dynamic_current((u32)(color>>8 & 0xFF), \
		g_s2mu005_rgb_led_data->led_dynamic_current[LED_G]);
	if(curr) {
		s2mu005_rgb_write_reg(S2MU005_LED_G_CURRENT_REG, curr);
		mode |= 0b1100;
	}
    //  Write LED1 current
	curr = s2mu005_rgb_dynamic_current((u32)(color & 0xFF), \
		g_s2mu005_rgb_led_data->led_dynamic_current[LED_B]);
	if(curr) {
		s2mu005_rgb_write_reg(S2MU005_LED_B_CURRENT_REG, curr);
		mode |= 0b110000;
	}
	
#else
	//  Write LED3 current
    s2mu005_rgb_write_reg(S2MU005_LED_R_CURRENT_REG, \
			s2mu005_rgb_dynamic_current((u32)(color>>16 & 0xFF), \
				g_s2mu005_rgb_led_data->led_dynamic_current[LED_R]));
	//  Write LED2 current
    s2mu005_rgb_write_reg(S2MU005_LED_G_CURRENT_REG, \
			s2mu005_rgb_dynamic_current((u32)(color>>8 & 0xFF),
				g_s2mu005_rgb_led_data->led_dynamic_current[LED_G]));
	//  Write LED1 current
    s2mu005_rgb_write_reg(S2MU005_LED_B_CURRENT_REG, \
			s2mu005_rgb_dynamic_current((u32)(color & 0xFF), 
				g_s2mu005_rgb_led_data->led_dynamic_current[LED_B]));
#endif			

	if(off_time == 0) //constant glow
	{
		//constant mode in enable reg
#ifdef S2MU005_ENABLE_INDIVIDUAL_CURRENT		
		s2mu005_rgb_write_reg(S2MU005_LED_ENABLE_REG, (mode & S2MU005_LED_CONST_CURR));
#else		
		s2mu005_rgb_write_reg(S2MU005_LED_ENABLE_REG, S2MU005_LED_CONST_CURR);
#endif	
		return 0;
	}

	if(off_time <= 5000)
		off_time /= 500;
	else if (off_time <= 8000)
		off_time = ((off_time - 5000)/1000) + 10 ;
	else if (off_time <= 12000)
		off_time = ((off_time - 8000)/2000) + 13 ;
	else
		off_time = 0xF;

	if(on_time < 300)
	{
		ramp_up_time = ramp_down_time=0;
		stable_on_time = on_time/100;
	}
	else
	{
		if(on_time > 7650)
			on_time = 7650;

		temp = on_time/3;

		if(temp > 2200)
			temp = 2200;

		if(temp <= 800)
		{	
			ramp_up_time = temp/100;
			ramp_up_time_value = ramp_up_time*100; 
		}
		else if(temp <= 2200)
		{
			ramp_up_time = ((temp - 800)/200) + 8;
			ramp_up_time_value = (ramp_up_time-8)*200+800;
		}	
		else
		{
			ramp_up_time = 0xF;
			ramp_up_time_value = 2200;
		}

		ramp_down_time = ramp_up_time;
		stable_on_time_value = on_time - (ramp_up_time_value << 1);
	}

	temp  = (ramp_up_time << 4) | ramp_down_time;

	// Write LED1 Ramp up and down
	s2mu005_rgb_write_reg(S2MU005_LED1_RAMP_REG, temp);
	// Write LED2 Ramp up and down
	s2mu005_rgb_write_reg(S2MU005_LED2_RAMP_REG, temp);
	// Write LED3 Ramp up and down
	s2mu005_rgb_write_reg(S2MU005_LED3_RAMP_REG, temp);

	if(stable_on_time_value > 3250)
		stable_on_time_value = 3250;
	if(stable_on_time_value <= 500)
	{
		stable_on_time = (stable_on_time_value-100)/100;
	}
	else 
	{
		stable_on_time = (stable_on_time_value-500)/250 +4;
	}
	
	temp  = (stable_on_time<<4)|off_time ;

	// Write LED1 Duration
	s2mu005_rgb_write_reg(S2MU005_LED1_DUR_REG, temp);
	// Write LED2 Duration
	s2mu005_rgb_write_reg(S2MU005_LED2_DUR_REG, temp);
	// Write LED3 Duration
	s2mu005_rgb_write_reg(S2MU005_LED3_DUR_REG, temp);

	//Write LED on to Enable reg
#ifdef S2MU005_ENABLE_INDIVIDUAL_CURRENT	
    s2mu005_rgb_write_reg(S2MU005_LED_ENABLE_REG, (mode & S2MU005_LED_SLOPE_CURR));
#else
	s2mu005_rgb_write_reg(S2MU005_LED_ENABLE_REG, S2MU005_LED_SLOPE_CURR);
#endif	

    return 0;
}

#ifdef CONFIG_OF
/**
* s2mu005_rgb_led_parse_dt - parses hw details from dts
**/
static int s2mu005_rgb_led_parse_dt(struct platform_device *dev, struct s2mu005_rgb_led *data)
{
	struct device_node *np;
	//const u32 *prop;
	int ret, i;
	struct s2mu005_led_tuning_table *pled_tuning_table;
	u32 table_size;
	struct s2mu005_dev *s2mu005 = dev_get_drvdata(dev->dev.parent);
	

	if(unlikely(!dev->dev.parent->of_node))
	{
		dev_err(&dev->dev, "[SVC LED] err: could not parse parent-node\n");
		return -ENODEV;
	}
	
	if(unlikely(!(np = of_find_node_by_name(dev->dev.parent->of_node, "leds-rgb"))))
	{
		dev_err(&dev->dev, "[SVC LED] err: could not parse sub-node\n");
		return -ENODEV;
	}	
	
	//prop = of_get_property(np, "led_current_tuning", &size);
	
	if(unlikely(((ret = of_property_read_u32(np, "led_current_tuning_count", &table_size)))))
	{
		dev_err(&dev->dev, "[SVC LED] err: error in parsing tuning table\n");
		return -ENODEV;
	}
	else
	{	
		dev_err(&dev->dev, "[SVC LED] Table Size: %hx\n", table_size);
		
		if(table_size == 0)
		{
			dev_err(&dev->dev, "[SVC LED] Window color based tuning is not available: %hx\n", table_size);
			return -ENODEV;
		}
		
		if(unlikely(!( pled_tuning_table = devm_kzalloc(s2mu005->dev,
				sizeof(struct s2mu005_led_tuning_table) * table_size, GFP_KERNEL)))) {
			dev_err(&dev->dev, "[SVC LED] err: failed to allocate for tuning table\n");
			return -ENOMEM;
		}
		
		if(unlikely(((ret = of_property_read_u32_array(np, "led_current_tuning", \
				(u32 *)pled_tuning_table, 
				(sizeof(struct s2mu005_led_tuning_table)/sizeof(u32))* table_size)))))
		{
			dev_err(&dev->dev, "[SVC LED] err: error in parsing tuning table\n");
			devm_kfree(s2mu005->dev, pled_tuning_table);
			return -ENODEV;			
		}
		
		dev_err(&dev->dev, "[SVC LED] Tuning Table:\n==============\n");
		for(i = 0; i < table_size; i++)
		{
			dev_err(&dev->dev, "lcdtype:%x\n", pled_tuning_table[i].lcd_type);
			dev_err(&dev->dev, "led_max_current:%d %d %d\n", \
				pled_tuning_table[i].b_max_current,
				pled_tuning_table[i].g_max_current,
				pled_tuning_table[i].r_max_current
				);
			dev_err(&dev->dev, "led_low_current:%d %d %d\n", \
				pled_tuning_table[i].b_lowpower_current,
				pled_tuning_table[i].g_lowpower_current,
				pled_tuning_table[i].r_lowpower_current
				);
		}
		
		for(i = 0; i < table_size; i++)
		{
			if(pled_tuning_table[i].lcd_type == lcdtype)
			{
				break;
			}
			
		}
		if(i == table_size) 
		{ 
			/* If no LCD match is found by default take the first entry in the tuning table */
			i = 0;
		}
		
		g_s2mu005_rgb_led_data->led_max_current[LED_B] = 
					pled_tuning_table[i].b_max_current;	
		g_s2mu005_rgb_led_data->led_max_current[LED_G] = 
					pled_tuning_table[i].g_max_current;
		g_s2mu005_rgb_led_data->led_max_current[LED_R] = 
					pled_tuning_table[i].r_max_current;
		g_s2mu005_rgb_led_data->led_low_current[LED_B] = 
					pled_tuning_table[i].b_lowpower_current;
		g_s2mu005_rgb_led_data->led_low_current[LED_G] = 
					pled_tuning_table[i].g_lowpower_current;
		g_s2mu005_rgb_led_data->led_low_current[LED_R] = 
					pled_tuning_table[i].r_lowpower_current;

		devm_kfree(s2mu005->dev, pled_tuning_table);
	}
	
	return 0;
}
#endif // CONFIG_OF

#ifdef SEC_LED_SPECIFIC
/**
* s2mu005_start_led_pattern - Displays specific service LED patterns
*       such as charging, missed notification, etc.,
* @mode -> denotes LED pattern to be displayed
* @type -> TBD
**/
static void s2mu005_start_led_pattern(int mode)
{
	switch (mode) {
	case PATTERN_OFF:
		pr_info("[SVC LED] Pattern off\n");
		g_s2mu005_rgb_led_data->color = 0;
	break;

	case CHARGING:
		pr_info("[SVC LED] Battery Charging Pattern on\n");
		g_s2mu005_rgb_led_data->color = 0xFF0000;  // Red Color
		g_s2mu005_rgb_led_data->led_on_time = 100;
		g_s2mu005_rgb_led_data->led_off_time = 0;

		break;

	case CHARGING_ERR:
		pr_info("[SVC LED] Battery Charging error Pattern on\n");
		g_s2mu005_rgb_led_data->color = 0xFF0000;  // Red Color
		g_s2mu005_rgb_led_data->led_on_time = 500;
		g_s2mu005_rgb_led_data->led_off_time = 500;
		break;

	case MISSED_NOTI:
		pr_info("[SVC LED] Missed Notifications Pattern on\n");
		g_s2mu005_rgb_led_data->color = 0xFF;  // Blue Color
		g_s2mu005_rgb_led_data->led_on_time = 500;
		g_s2mu005_rgb_led_data->led_off_time = 5000;
		break;

	case LOW_BATTERY:
		pr_info("[SVC LED] Low Battery Pattern on\n");
		g_s2mu005_rgb_led_data->color = 0xFF0000;  // Red Color
		g_s2mu005_rgb_led_data->led_on_time = 500;
		g_s2mu005_rgb_led_data->led_off_time = 5000;
		break;

	case FULLY_CHARGED:
		pr_info("[SVC LED] full Charged battery Pattern on\n");
		g_s2mu005_rgb_led_data->color = 0xFF00;  // Green Color
		g_s2mu005_rgb_led_data->led_on_time = 100;
		g_s2mu005_rgb_led_data->led_off_time = 0;
		break;

	case POWERING:
		pr_info("[SVC LED] Powering Pattern on\n");
		g_s2mu005_rgb_led_data->color = 0xFF;  // Blue Color
		g_s2mu005_rgb_led_data->led_on_time = 1000;
		g_s2mu005_rgb_led_data->led_off_time = 1000;
		break;

	default:
		pr_info("[SVC LED] Wrong Pattern\n");
		return;
	}

	schedule_work(&g_s2mu005_rgb_led_data->blink_work);
}
#endif // SEC_LED_SPECIFIC


/**
* s2mu005_rgb_led_blink_work - work handler to execute HW access for blink works
**/
static void s2mu005_rgb_led_blink_work(struct work_struct *work)
{
	pr_info("[SVC LED] %s %d", __func__, __LINE__);

	s2mu005_rgb_leds_write_all(g_s2mu005_rgb_led_data->color, \
		g_s2mu005_rgb_led_data->led_on_time, g_s2mu005_rgb_led_data->led_off_time);
}

/**
* s2mu005_rgb_led_blink_store - sysfs write interface to get the LED 
*               color data in the format 
*				"<COLOR in 0xRRGGBB> <ON TIME in ms> <OFF TIME in ms>" 
**/
static ssize_t s2mu005_rgb_led_blink_store(struct device *dev, \
		struct device_attribute *attr,const char *buf, size_t len)
{
	int retval;
	u32 color, delay_on_time, delay_off_time;

	dev_err(dev, "%s %s\n", __func__, buf);

	retval = sscanf(buf, "0x%x %u %u", &color, &delay_on_time, &delay_off_time); // brightness from here??
    if (unlikely(retval == 0)) {
		dev_err(dev, "%s fail to get led_blink value.\n", __func__);
	}

    g_s2mu005_rgb_led_data->color = color;
    g_s2mu005_rgb_led_data->led_on_time = delay_on_time;
    g_s2mu005_rgb_led_data->led_off_time = delay_off_time;

	schedule_work(&g_s2mu005_rgb_led_data->blink_work);

	return len;
}

/**
* s2mu005_rgb_led_r_store - internal call from sysfs interface to read
*			brightness value from Userspace.
* @colorshift -> specifies R or G or B's color info.	
**/
void s2mu005_rgb_led_get_brightness(struct device *dev, \
		const char *buf, int colorshift)
{
	u8 brightness;
	
	if(likely(!kstrtou8(buf, 0, &brightness)))
	{
		g_s2mu005_rgb_led_data->color = brightness << colorshift;
		g_s2mu005_rgb_led_data->led_on_time = 1000;
		g_s2mu005_rgb_led_data->led_off_time = 0;

		schedule_work(&g_s2mu005_rgb_led_data->blink_work);
	}
	else
	{
		dev_err(dev, "[SVC LED] Error in getting brightness!\n");
	}
}

/**
* s2mu005_rgb_led_r_store - sysfs write interface to get the R LED brightness.
**/
static ssize_t s2mu005_rgb_led_r_store(struct device *dev, \
		struct device_attribute *attr,const char *buf, size_t len)
{
	dev_info(dev, "%s %s\n", __func__, buf);

	s2mu005_rgb_led_get_brightness(dev, buf, LED_R_SHIFT);	

	return len;
}

/**
* s2mu005_rgb_led_g_store - sysfs write interface to get the G LED brightness.
**/
static ssize_t s2mu005_rgb_led_g_store(struct device *dev, \
		struct device_attribute *attr,const char *buf, size_t len)
{
	dev_info(dev, "%s %s\n", __func__, buf);

	s2mu005_rgb_led_get_brightness(dev, buf, LED_G_SHIFT);	
	
	return len;
}

/**
* s2mu005_rgb_led_b_store - sysfs write interface to get the B LED brightness.
**/
static ssize_t s2mu005_rgb_led_b_store(struct device *dev, \
		struct device_attribute *attr,const char *buf, size_t len)
{
	dev_info(dev, "%s %s\n", __func__, buf);

	s2mu005_rgb_led_get_brightness(dev, buf, LED_B_SHIFT);	
	
	return len;
}

/**
* s2mu005_rgb_led_store_lowpower - sysfs write interface to enable/disable 
*                  lowpower mode. 0 - normal mode, 1 - lowpower mode.
**/
static ssize_t s2mu005_rgb_led_store_lowpower(struct device *dev, \
		struct device_attribute *attr, const char *buf, size_t len)
{
	u8 led_lowpower;
	
	if(likely(!kstrtou8(buf, 0, &led_lowpower)))
	{
		g_s2mu005_rgb_led_data->led_lowpower_mode = led_lowpower;
		
		if(led_lowpower)
		{
			g_s2mu005_rgb_led_data->led_dynamic_current[LED_R] = g_s2mu005_rgb_led_data->led_low_current[LED_R];
			g_s2mu005_rgb_led_data->led_dynamic_current[LED_G] = g_s2mu005_rgb_led_data->led_low_current[LED_G];
			g_s2mu005_rgb_led_data->led_dynamic_current[LED_B] = g_s2mu005_rgb_led_data->led_low_current[LED_B];
		}
		else		
		{
			g_s2mu005_rgb_led_data->led_dynamic_current[LED_R] = g_s2mu005_rgb_led_data->led_max_current[LED_R];
			g_s2mu005_rgb_led_data->led_dynamic_current[LED_G] = g_s2mu005_rgb_led_data->led_max_current[LED_G];
			g_s2mu005_rgb_led_data->led_dynamic_current[LED_B] = g_s2mu005_rgb_led_data->led_max_current[LED_B];
		}
		
		// Schedule the work, so that the low power can be updated.
		schedule_work(&g_s2mu005_rgb_led_data->blink_work);
	}
	else
	{
		dev_err(dev, "[SVC LED] Wrong low power mode\n");
	}
	
	return len;
}

/**
* s2mu005_rgb_led_store_led_pattern - sysfs write interface to get the LED 
*               Pattern to be displayed
**/
static ssize_t s2mu005_rgb_led_store_led_pattern(struct device *dev, \
		struct device_attribute *attr, const char *buf, size_t len)
{
	int retval;
	unsigned int mode = 0;

	retval = sscanf(buf, "%d", &mode);

	dev_err(dev, "[SVC LED] Store Pattern: %d\n", mode);

	if (unlikely(retval == 0)) {
		dev_err(dev, "[SVC LED] fail to get led_pattern mode.\n");
	}
	else
	{
		g_s2mu005_rgb_led_data->mode = mode;
		s2mu005_start_led_pattern(mode);
	}

	return len;
}

#ifndef CONFIG_SAMSUNG_PRODUCT_SHIP
/**
* s2mu005_rgb_led_show_lowpower - sysfs read interface to show the low power mode
*     0 - normal mode, 1 - lowpower mode.
*     Accessible only on non-ship binaries.
**/
static ssize_t s2mu005_rgb_led_show_lowpower(struct device *dev, \
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d", g_s2mu005_rgb_led_data->led_lowpower_mode);
}

/**
* s2mu005_rgb_led_blink_show - sysfs read interface to show the LED 
*      configuration (color, blink on time, blink off time) currently 
*      being displayed.
*      Accessible only on non-ship binaries.
**/
static ssize_t s2mu005_rgb_led_blink_show(struct device *dev, \
		struct device_attribute *attr, char *buf)
{

	return sprintf(buf, "%x %u %u", g_s2mu005_rgb_led_data->color, \
		g_s2mu005_rgb_led_data->led_on_time, \
		g_s2mu005_rgb_led_data->led_off_time);
}

/**
* s2mu005_rgb_led_r_show - sysfs read interface to show the R LED 
*      brightness value.
*      Accessible only on non-ship binaries.
**/
static ssize_t s2mu005_rgb_led_r_show(struct device *dev, \
		struct device_attribute *attr, char *buf)
{

	return sprintf(buf, "%d", g_s2mu005_rgb_led_data->color >> LED_R_SHIFT);
}

/**
* s2mu005_rgb_led_g_show - sysfs read interface to show the G LED 
*      brightness value.
*      Accessible only on non-ship binaries.
**/
static ssize_t s2mu005_rgb_led_g_show(struct device *dev, \
		struct device_attribute *attr, char *buf)
{

	return sprintf(buf, "%d", 
	(g_s2mu005_rgb_led_data->color >> LED_G_SHIFT) & LED_BRIGHTNESS_MASK);
}

/**
* s2mu005_rgb_led_b_show - sysfs read interface to show the B LED 
*      brightness value.
*      Accessible only on non-ship binaries.
**/
static ssize_t s2mu005_rgb_led_b_show(struct device *dev, \
		struct device_attribute *attr, char *buf)
{

	return sprintf(buf, "%d", 
		g_s2mu005_rgb_led_data->color & LED_BRIGHTNESS_MASK);
}

/**
* s2mu005_rgb_led_show_led_pattern - sysfs read interface to show the  
*      LED pattern currently being displayed.
*      Accessible only on non-ship binaries.
**/
static ssize_t s2mu005_rgb_led_show_led_pattern(struct device *dev, \
		struct device_attribute *attr, char *buf)
{

	return sprintf(buf, "%d", g_s2mu005_rgb_led_data->mode);
}

/**
* s2mu005_rgb_led_show_reg - debug sysfs read interface to show   
*      all the RGB LED register values of s2mu005.
*      Accessible only on non-ship binaries.
**/
static ssize_t s2mu005_rgb_led_show_reg(struct device *dev, \
		struct device_attribute *attr, char *buf)
{
	int i, len = 0;
	u8 data[S2MU005_LED_MAX_REG_COUNT];
	
	s2mu005_bulk_read(g_s2mu005_rgb_led_data->i2c, S2MU005_LED_BASE_REG, \
		S2MU005_LED_MAX_REG_COUNT, data);
	
	for(i = 0; i < S2MU005_LED_MAX_REG_COUNT; i++) {
		len = sprintf((buf + len), "%X ", data[i]);
	}

	return len;
}

/**
* s2mu005_rgb_led_show_reg - debug sysfs write interface to write   
*      specified 8 bit value on to the specified register of s2mu005's 
*      RGB LED register values of .
*      Accessible only on non-ship binaries.
**/
static ssize_t s2mu005_rgb_led_store_reg(struct device *dev, \
		struct device_attribute *attr, const char *buf, size_t len)
{
	int reg, val;
	
	sscanf(buf, "%x %x", &reg, &val);
	
	s2mu005_rgb_write_reg(reg, val);
	
	return len;
}

/**
* s2mu005_rgb_led_show_dynamic_current - debug sysfs read interface to show   
*      max current for normal mode and low power mode of each LEDs (R, G, B)
*      Accessible only on non-ship binaries.
**/
static ssize_t s2mu005_rgb_led_show_dynamic_current(struct device *dev, \
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%x %x %x %x %x %x", \
		g_s2mu005_rgb_led_data->led_max_current[LED_R],
		g_s2mu005_rgb_led_data->led_max_current[LED_G],
		g_s2mu005_rgb_led_data->led_max_current[LED_B],
		g_s2mu005_rgb_led_data->led_low_current[LED_R],
		g_s2mu005_rgb_led_data->led_low_current[LED_G],
		g_s2mu005_rgb_led_data->led_low_current[LED_B]);		
}

/**
* s2mu005_rgb_led_show_dynamic_current - debug sysfs write interface to update   
*      max current for normal mode and low power mode of each LEDs (R, G, B)
*      Accessible only on non-ship binaries.
**/
static ssize_t s2mu005_rgb_led_store_dynamic_current(struct device *dev, \
		struct device_attribute *attr, const char *buf, size_t len)
{	
	int max_current[LED_MAX], low_current[LED_MAX];
	
	sscanf(buf, "%x %x %x %x %x %x", \
			&max_current[0], &max_current[1], &max_current[2],
			&low_current[0], &low_current[1], &low_current[2]);
	
	g_s2mu005_rgb_led_data->led_max_current[LED_R] = max_current[0];
	g_s2mu005_rgb_led_data->led_max_current[LED_G] = max_current[1];
	g_s2mu005_rgb_led_data->led_max_current[LED_B] = max_current[2];
	g_s2mu005_rgb_led_data->led_low_current[LED_R] = low_current[0];
	g_s2mu005_rgb_led_data->led_low_current[LED_G] = low_current[1];
	g_s2mu005_rgb_led_data->led_low_current[LED_B] = low_current[2];
	
	return len;
}


/* List of nodes, created for non ship binary */
static DEVICE_ATTR(reg, 0664, s2mu005_rgb_led_show_reg, s2mu005_rgb_led_store_reg);
static DEVICE_ATTR(dynamic_current, 0664, s2mu005_rgb_led_show_dynamic_current, s2mu005_rgb_led_store_dynamic_current);
static DEVICE_ATTR(led_lowpower, 0664, s2mu005_rgb_led_show_lowpower, s2mu005_rgb_led_store_lowpower);
static DEVICE_ATTR(led_pattern, 0664, s2mu005_rgb_led_show_led_pattern, s2mu005_rgb_led_store_led_pattern);
static DEVICE_ATTR(led_blink, 0644, s2mu005_rgb_led_blink_show, s2mu005_rgb_led_blink_store);
static DEVICE_ATTR(led_r, 0644, s2mu005_rgb_led_r_show, s2mu005_rgb_led_r_store);
static DEVICE_ATTR(led_g, 0644, s2mu005_rgb_led_g_show, s2mu005_rgb_led_g_store);
static DEVICE_ATTR(led_b, 0644, s2mu005_rgb_led_b_show, s2mu005_rgb_led_b_store);
#else
	
/* List of nodes, created for ship binary */
static DEVICE_ATTR(led_lowpower, 0664, NULL, s2mu005_rgb_led_store_lowpower);
static DEVICE_ATTR(led_pattern, 0664, NULL, s2mu005_rgb_led_store_led_pattern);	
static DEVICE_ATTR(led_blink, 0644, NULL, s2mu005_rgb_led_blink_store);
/* the followig nodes are used in HW module test */
static DEVICE_ATTR(led_r, 0644, NULL, s2mu005_rgb_led_r_store);
static DEVICE_ATTR(led_g, 0644, NULL, s2mu005_rgb_led_g_store);
static DEVICE_ATTR(led_b, 0644, NULL, s2mu005_rgb_led_b_store);
#endif //CONFIG_SAMSUNG_PRODUCT_SHIP

#ifdef SEC_LED_SPECIFIC
static struct attribute *sec_led_attributes[] = {
	&dev_attr_led_pattern.attr,
	&dev_attr_led_blink.attr,
	&dev_attr_led_r.attr,
	&dev_attr_led_g.attr,
	&dev_attr_led_b.attr,
	&dev_attr_led_lowpower.attr,
#ifndef CONFIG_SAMSUNG_PRODUCT_SHIP
	&dev_attr_reg.attr,
	&dev_attr_dynamic_current.attr,
#endif //CONFIG_SAMSUNG_PRODUCT_SHIP
	NULL,
};

static struct attribute_group sec_led_attr_group = {
	.attrs = sec_led_attributes,
};
#endif //SEC_LED_SPECIFIC
 
#ifdef CONFIG_SEC_SVCLED_POWERON_GLOW
/**
* s2mu005_is_jig_powered - get whether the phone is powered on by JIG or not.
**/
int s2mu005_is_jig_powered(void)
{
	char buf[24] = {0};
	struct file *fp;
	
	fp = filp_open(JIG_STATUS_FILE_PATH, O_RDONLY, 0664);	
	if(IS_ERR(fp))
	{
		printk(KERN_ERR "%s %s open failed\n", __func__, JIG_STATUS_FILE_PATH);
		goto jig_exit;
	}
	
	kernel_read(fp, fp->f_pos, buf, 1);
	
	filp_close(fp, current->files);
	
jig_exit:
	return (buf[0] == '1');
}
#endif //CONFIG_SEC_SVCLED_POWERON_GLOW

/**
* s2mu005_rgb_led_probe - Enumerates service LED resources.
**/
static int s2mu005_rgb_led_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct s2mu005_dev *s2mu005 = dev_get_drvdata(pdev->dev.parent);

	dev_info(&pdev->dev, "start! %s %d\n", __func__, __LINE__);

    if(unlikely(!(g_s2mu005_rgb_led_data = devm_kzalloc(s2mu005->dev,
			sizeof(struct s2mu005_rgb_led), GFP_KERNEL)))) {
		dev_err(&pdev->dev, "failed to allocate driver data.\n");
		return -ENOMEM;
	}

	g_s2mu005_rgb_led_data->i2c = s2mu005->i2c;
	platform_set_drvdata(pdev, g_s2mu005_rgb_led_data); //TBD

#ifdef CONFIG_OF
	ret =  s2mu005_rgb_led_parse_dt(pdev, g_s2mu005_rgb_led_data);
	if (ret) {
		dev_err(&pdev->dev, "[%s] s2mu005_rgb_led parse dt failed\n", __func__);
		goto exit;
	}
#else
	g_s2mu005_rgb_led_data->led_max_current[LED_R] = S2MU005_LED_R_MAX_CURRENT;
	g_s2mu005_rgb_led_data->led_max_current[LED_G] = S2MU005_LED_G_MAX_CURRENT;
	g_s2mu005_rgb_led_data->led_max_current[LED_B] = S2MU005_LED_B_MAX_CURRENT;
	g_s2mu005_rgb_led_data->led_low_current[LED_R] = S2MU005_LED_R_LOW_CURRENT;
	g_s2mu005_rgb_led_data->led_low_current[LED_G] = S2MU005_LED_G_LOW_CURRENT;
	g_s2mu005_rgb_led_data->led_low_current[LED_B] = S2MU005_LED_B_LOW_CURRENT;
#endif //CONFIG_OF

	/* Update Currently the mode is normal mode */
	g_s2mu005_rgb_led_data->led_lowpower_mode = 0;
	
	g_s2mu005_rgb_led_data->led_dynamic_current[LED_R] = 
				g_s2mu005_rgb_led_data->led_max_current[LED_R];
	g_s2mu005_rgb_led_data->led_dynamic_current[LED_G] = 
				g_s2mu005_rgb_led_data->led_max_current[LED_G];;
	g_s2mu005_rgb_led_data->led_dynamic_current[LED_B] = 
				g_s2mu005_rgb_led_data->led_max_current[LED_B];
	
	INIT_WORK(&(g_s2mu005_rgb_led_data->blink_work),
				 s2mu005_rgb_led_blink_work);

#ifdef CONFIG_SEC_SVCLED_POWERON_GLOW
	if(!s2mu005_is_jig_powered() && lcdtype == 0)
	{
		s2mu005_start_led_pattern(MISSED_NOTI);
		return 0;
	}
#endif

#ifdef SEC_LED_SPECIFIC
	g_s2mu005_rgb_led_data->led_dev = sec_device_create(g_s2mu005_rgb_led_data, "led");
	if (unlikely(IS_ERR(g_s2mu005_rgb_led_data->led_dev))) {
		dev_err(&pdev->dev,
			"Failed to create device for Samsung specific led\n");
		ret = -ENODEV;
		goto exit;
	}
	
	ret = sysfs_create_group(&g_s2mu005_rgb_led_data->led_dev->kobj, &sec_led_attr_group);
	if (unlikely(ret)) {
		dev_err(&pdev->dev,
			"Failed to create sysfs group for Samsung specific led\n");
		goto exit;
	}
#endif

	return 0;
exit:
	dev_err(&pdev->dev, "[SVC LED] err: %s %d\n", __func__, __LINE__);
	kfree(g_s2mu005_rgb_led_data);
	g_s2mu005_rgb_led_data = NULL;
	return ret;
}

/**
* s2mu005_rgb_led_shutdown - Things to be done on shutdown call.
**/
static void s2mu005_rgb_led_shutdown(struct device *dev)
{
	s2mu005_rgb_leds_write_all(0, 0, 0);
}

/**
* s2mu005_rgb_led_shutdown - Things to be done on device removal.
**/
static int s2mu005_rgb_led_remove(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "Device Remove\n");
	
	s2mu005_rgb_leds_write_all(0, 0, 0);
	
	return 0;
}

#if 1
static struct of_device_id s2mu005_rgb_leds_id[] = {
	{ .compatible = S2MU005_RGB_LED_DRIVER_NAME, },
	{ },
};
#else
static const struct platform_device_id s2mu005_rgb_leds_id[] = {
	{S2MU005_RGB_LED_DRIVER_NAME, 0},
	{ },
};
#endif


/**
* s2mu005_rgb_led_driver - Holds driver mapping of LED platform driver
*        for Service notification LED.
**/
static struct platform_driver s2mu005_rgb_led_driver = {
	.probe = s2mu005_rgb_led_probe,
	//.id_table = s2mu005_rgb_leds_id,
	.remove = s2mu005_rgb_led_remove,
	.driver = {
		.name =  S2MU005_RGB_LED_DRIVER_NAME,
		.owner = THIS_MODULE,
		.shutdown = s2mu005_rgb_led_shutdown,
		.of_match_table = s2mu005_rgb_leds_id,
	},
};

module_platform_driver(s2mu005_rgb_led_driver);

MODULE_DESCRIPTION("S2MU005 RGB LED driver");
MODULE_AUTHOR("Architha Chitukuri <a.chitukuri@samsung.com>");
MODULE_LICENSE("GPL");



