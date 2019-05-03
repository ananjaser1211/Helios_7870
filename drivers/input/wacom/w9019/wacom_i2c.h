#ifndef _LINUX_WACOM_I2C_H_
#define _LINUX_WACOM_I2C_H_

#define WACOM_GPIO_CNT 5

/*sec_class sysfs*/
extern struct class *sec_class;

struct wacom_g5_platform_data {
	int gpios[WACOM_GPIO_CNT];
	u32 flag_gpio[WACOM_GPIO_CNT];
	int boot_addr;
	int irq_type;
	int x_invert;
	int y_invert;
	int xy_switch;
	int max_x;
	int max_y;
	u32 origin[2];
	int max_pressure;
	int min_pressure;
	const char *fw_path;
	u32 ic_type;
	bool boot_on_ldo;
	const char *project_name;
	const char *model_name;
};

#endif /* _LINUX_WACOM_I2C_H */
