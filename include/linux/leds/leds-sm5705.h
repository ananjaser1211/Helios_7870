/*
 * Flash-LED device driver for SM5705
 *
 * Copyright (C) 2015 Silicon Mitus
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __LEDS_SM5705_H__
#define __LEDS_SM5705_H__

#define TORCH_STEP 5

enum {
	SM5705_FLED_0 = 0,
	SM5705_FLED_1,
	SM5705_FLED_MAX,
};

enum {
	SM5705_FLED_OFF = 0,
	SM5705_FLED_PREFLASH,
	SM5705_FLED_FLASH,
	SM5705_FLED_MOVIE,
	SM5705_FLED_TORCH,
};

struct sm5705_fled_platform_data {
	struct {
		unsigned short preflash_current_mA;
		unsigned short flash_current_mA;
		unsigned short movie_current_mA;
		unsigned short torch_current_mA;
		unsigned short factory_current_mA;

		bool used_gpio;
		int flash_en_pin;
		int torch_en_pin;
		int torch_table[TORCH_STEP];
	}led[SM5705_FLED_MAX];

	struct pinctrl *fled_pinctrl;
	struct pinctrl_state *gpio_state_active;
	struct pinctrl_state *gpio_state_suspend;
};

int sm5705_fled_prepare_flash(unsigned char index);
int sm5705_fled_torch_on(unsigned char index, unsigned char mode);
int sm5705_fled_flash_on(unsigned char index);
int sm5705_fled_led_off(unsigned char index);
int sm5705_fled_close_flash(unsigned char index);

#endif
