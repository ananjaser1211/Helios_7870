/* linux/drivers/video/exynos/decon_7580/panels/s6e3fa2_aid_dimming.h
 *
 * Header file for Samsung AID Dimming Driver.
 *
 * Copyright (c) 2013 Samsung Electronics
 * Minwoo Kim <minwoo7945.kim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __EA8064G_AID_DIMMING_H__
#define __EA8064G_AID_DIMMING_H__

static signed char ctbl5nit_B[30] = {0, 0, 0, 0, 0, 0,-13, 0, -1, -13, 0, 6, -7, 0, 7, -6, 0, 9, -3, 0, 3, -1, 0, 0, 1, 0, 0, 0, 0, 3};
static signed char ctbl6nit_B[30] = {0, 0, 0, 0, 0, 0,-12, 0, -1, -13, 0, 5, -6, 0, 6, -5, 0, 9, -3, 0, 2, 1, 0, 1, 0, 0, -1, 0, 0, 3};
static signed char ctbl7nit_B[30] = {0, 0, 0, 0, 0, 0,-11, 0, -2, -12, 0, 4, -5, 0, 7, -4, 0, 9, -2, 0, 3, 0, 0, 0, -1, 0, -2, 0, 0, 3};
static signed char ctbl8nit_B[30] = {0, 0, 0, 0, 0, 0,-9, 0, 1, -11, 0, 5, -5, 0, 5, -4, 0, 8, -2, 0, 2, 0, 0, 0, -1, 0, -2, 0, 0, 3};
static signed char ctbl9nit_B[30] = {0, 0, 0, 0, 0, 0,-8, 0, 2, -10, 0, 3, -4, 0, 6, -4, 0, 8, -2, 0, 2, 0, 0, 0, 1, 0, -1, 0, 0, 3};
static signed char ctbl10nit_B[30] = {0, 0, 0, 0, 0, 0,-8, 0, 2, -10, 0, 3, -4, 0, 5, -3, 0, 8, -3, 0, 1, 0, 0, 0, -1, 0, -2, 1, 0, 3};
static signed char ctbl11nit_B[30] = {0, 0, 0, 0, 0, 0,-9, 0, 2, -11, 0, 3, -4, 0, 4, -3, 0, 8, -3, 0, 1, 0, 0, 0, -1, 0, -2, 1, 0, 3};
static signed char ctbl12nit_B[30] = {0, 0, 0, 0, 0, 0,-11, 0, 1, -9, 0, 3, -3, 0, 4, -3, 0, 7, -3, 0, 1, 0, 0, 0, -1, 0, -2, 1, 0, 3};
static signed char ctbl13nit_B[30] = {0, 0, 0, 0, 0, 0,-12, 0, 1, -9, 0, 3, -2, 0, 3, -3, 0, 7, -2, 0, 1, 0, 0, 0, -1, 0, -2, 1, 0, 3};
static signed char ctbl14nit_B[30] = {0, 0, 0, 0, 0, 0,-16, 0, 0, -7, 0, 3, -3, 0, 2, -2, 0, 7, -2, 0, 1, 0, 0, 0, 0, 0, -1, 0, 0, 2};
static signed char ctbl15nit_B[30] = {0, 0, 0, 0, 0, 0,-15, 0, 1, -8, 0, 2, -3, 0, 3, -2, 0, 6, -2, 0, 1, 0, 0, 0, 0, 0, -1, 0, 0, 2};
static signed char ctbl16nit_B[30] = {0, 0, 0, 0, 0, 0,-15, 0, 0, -7, 0, 2, -1, 0, 3, -2, 0, 7, -2, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 2};
static signed char ctbl17nit_B[30] = {0, 0, 0, 0, 0, 0,-15, 0, -1, -6, 0, 3, -3, 0, 2, -1, 0, 6, -2, 0, 0, 0, 0, 0, 0, 0, -1, 0, 0, 2};
static signed char ctbl19nit_B[30] = {0, 0, 0, 0, 0, 0,-14, 0, 1, -6, 0, 3, -2, 0, 0, -1, 0, 7, -2, 0, 1, 0, 0, -1, 0, 0, -1, 0, 0, 2};
static signed char ctbl20nit_B[30] = {0, 0, 0, 0, 0, 0,-12, 0, 1, -6, 0, 2, -2, 0, 1, -1, 0, 6, -2, 0, 1, 0, 0, -1, 0, 0, -1, 0, 0, 2};
static signed char ctbl21nit_B[30] = {0, 0, 0, 0, 0, 0,-12, 0, -1, -6, 0, 2, -2, 0, 0, -1, 0, 5, -2, 0, 0, -1, 0, -1, 1, 0, -1, 0, 0, 2};
static signed char ctbl22nit_B[30] = {0, 0, 0, 0, 0, 0,-10, 0, -2, -4, 0, 3, -2, 0, -1, -2, 0, 4, -2, 0, 0, 0, 0, 0, 1, 0, -1, 0, 0, 2};
static signed char ctbl24nit_B[30] = {0, 0, 0, 0, 0, 0,-10, 0, -3, -4, 0, 2, -3, 0, -2, -1, 0, 4, -3, 0, 0, 0, 0, -1, 1, 0, -1, 0, 0, 2};
static signed char ctbl25nit_B[30] = {0, 0, 0, 0, 0, 0,-8, 0, -2, -4, 0, 2, -4, 0, -3, -2, 0, 3, -2, 0, 0, 0, 0, -1, 1, 0, -1, 0, 0, 2};
static signed char ctbl27nit_B[30] = {0, 0, 0, 0, 0, 0,-8, 0, -3, -5, 0, 0, -3, 0, -2, -2, 0, 3, -2, 0, -1, 0, 0, -1, 1, 0, -1, 0, 0, 2};
static signed char ctbl29nit_B[30] = {0, 0, 0, 0, 0, 0,-7, 0, -4, -5, 0, 0, -4, 0, -3, -1, 0, 3, -2, 0, -1, 1, 0, 0, 0, 0, -1, 0, 0, 1};
static signed char ctbl30nit_B[30] = {0, 0, 0, 0, 0, 0,-8, 0, -5, -4, 0, 0, -3, 0, -3, -2, 0, 2, -2, 0, -1, 1, 0, 0, 0, 0, -1, 0, 0, 1};
static signed char ctbl32nit_B[30] = {0, 0, 0, 0, 0, 0,-8, 0, -4, -4, 0, -1, -2, 0, -2, -2, 0, 2, -2, 0, -1, 1, 0, 0, -1, 0, -2, 0, 0, 1};
static signed char ctbl34nit_B[30] = {0, 0, 0, 0, 0, 0,-8, 0, -6, -6, 0, -2, -2, 0, -2, -1, 0, 2, -2, 0, -1, 1, 0, 0, -1, 0, -2, 0, 0, 1};
static signed char ctbl37nit_B[30] = {0, 0, 0, 0, 0, 0,-9, 0, -4, -4, 0, -1, -1, 0, -3, -2, 0, 2, -2, 0, -1, 1, 0, 0, -1, 0, -2, 0, 0, 1};
static signed char ctbl39nit_B[30] = {0, 0, 0, 0, 0, 0,-8, 0, -6, -4, 0, -1, -2, 0, -3, -1, 0, 2, -2, 0, -1, 0, 0, 0, 0, 0, -2, 0, 0, 1};
static signed char ctbl41nit_B[30] = {0, 0, 0, 0, 0, 0,-9, 0, -5, -4, 0, -2, -2, 0, -3, -1, 0, 2, -2, 0, -2, 0, 0, 0, 0, 0, -2, 0, 0, 1};
static signed char ctbl44nit_B[30] = {0, 0, 0, 0, 0, 0,-8, 0, -3, -4, 0, -2, -2, 0, -3, -1, 0, 2, -2, 0, -2, 0, 0, 0, 0, 0, -2, 0, 0, 1};
static signed char ctbl47nit_B[30] = {0, 0, 0, 0, 0, 0,-7, 0, -5, -4, 0, -3, -2, 0, -3, -1, 0, 2, -2, 0, -2, 0, 0, 0, 0, 0, -2, 0, 0, 1};
static signed char ctbl50nit_B[30] = {0, 0, 0, 0, 0, 0,-8, 0, -4, -4, 0, -3, -1, 0, -3, 0, 0, 3, -2, 0, -2, 0, 0, 0, 0, 0, -2, 0, 0, 1};
static signed char ctbl53nit_B[30] = {0, 0, 0, 0, 0, 0,-8, 0, -5, -4, 0, -4, -1, 0, -3, 0, 0, 2, -2, 0, -1, 0, 0, 0, 0, 0, -2, 0, 0, 1};
static signed char ctbl56nit_B[30] = {0, 0, 0, 0, 0, 0,-8, 0, -2, -4, 0, -4, -1, 0, -3, -1, 0, 1, -2, 0, -1, 0, 0, 0, 0, 0, -2, 0, 0, 1};
static signed char ctbl60nit_B[30] = {0, 0, 0, 0, 0, 0,-8, 0, -2, -4, 0, -4, -1, 0, -3, -1, 0, 1, -2, 0, -1, 0, 0, 0, 0, 0, -2, 0, 0, 1};
static signed char ctbl64nit_B[30] = {0, 0, 0, 0, 0, 0,-8, 0, -2, -3, 0, -3, 0, 0, -3, -1, 0, 1, -2, 0, -1, 0, 0, 0, 0, 0, -2, 0, 0, 1};
static signed char ctbl68nit_B[30] = {0, 0, 0, 0, 0, 0,-7, 0, -1, -3, 0, -3, 1, 0, -3, -2, 0, 1, -1, 0, -1, 0, 0, 0, 0, 0, -2, 0, 0, 1};
static signed char ctbl72nit_B[30] = {0, 0, 0, 0, 0, 0,-6, 0, -1, -3, 0, -3, 0, 0, -3, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, -2, 0, 0, 2};
static signed char ctbl77nit_B[30] = {0, 0, 0, 0, 0, 0,-5, 0, 0, -2, 0, -3, 1, 0, -3, -1, 0, 2, -1, 0, -1, 0, 0, 0, 0, 0, -1, 1, 0, 2};
static signed char ctbl82nit_B[30] = {0, 0, 0, 0, 0, 0,-5, 0, 1, -2, 0, -2, 1, 0, -3, -1, 0, 2, -1, 0, -1, 1, 0, 0, 0, 0, -1, 0, 0, 1};
static signed char ctbl87nit_B[30] = {0, 0, 0, 0, 0, 0,-6, 0, 0, -2, 0, -2, 0, 0, -3, 0, 0, 2, -1, 0, -1, 0, 0, -1, 1, 0, 0, 0, 0, 2};
static signed char ctbl93nit_B[30] = {0, 0, 0, 0, 0, 0,-4, 0, 2, -2, 0, -2, 0, 0, -3, -2, 0, 1, -1, 0, 0, 0, 0, -1, 0, 0, 0, 1, 0, 1};
static signed char ctbl98nit_B[30] = {0, 0, 0, 0, 0, 0,-5, 0, -1, -2, 0, -2, 1, 0, -2, -1, 0, 2, 0, 0, 0, 0, 0, -1, 1, 0, -1, 0, 0, 1};
static signed char ctbl105nit_B[30] = {0, 0, 0, 0, 0, 0,-6, 0, -1, -1, 0, -1, 0, 0, -3, -1, 0, 1, 0, 0, 1, -1, 0, -2, 1, 0, -1, 0, 0, 1};
static signed char ctbl111nit_B[30] = {0, 0, 0, 0, 0, 0,-5, 0, 0, -1, 0, -2, 1, 0, -2, -1, 0, 2, -1, 0, 0, -1, 0, -2, 1, 0, 0, 0, 0, 1};
static signed char ctbl119nit_B[30] = {0, 0, 0, 0, 0, 0,-4, 0, 0, -2, 0, 0, 2, 0, -1, -1, 0, 1, 0, 0, 0, -1, 0, -2, 1, 0, 0, 0, 0, 2};
static signed char ctbl126nit_B[30] = {0, 0, 0, 0, 0, 0,-4, 0, 1, -1, 0, 0, 1, 0, -1, 0, 0, 2, -1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 0, 1};
static signed char ctbl134nit_B[30] = {0, 0, 0, 0, 0, 0,-5, 0, 0, -2, 0, 0, 1, 0, -2, -1, 0, 1, 0, 0, 0, 0, 0, -1, 0, 0, 0, 0, 0, 1};
static signed char ctbl143nit_B[30] = {0, 0, 0, 0, 0, 0,-2, 0, 1, -1, 0, 1, 0, 0, -2, -1, 0, 0, -1, 0, 0, 0, 0, -2, 1, 0, 0, 1, 0, 2};
static signed char ctbl152nit_B[30] = {0, 0, 0, 0, 0, 0,-2, 0, 2, -1, 0, 0, 1, 0, -1, 0, 0, 1, -1, 0, 0, 0, 0, -2, 0, 0, 0, 0, 0, 1};
static signed char ctbl162nit_B[30] = {0, 0, 0, 0, 0, 0,-1, 0, 2, -1, 0, 0, 1, 0, -1, -1, 0, 1, 0, 0, 0, 0, 0, -1, 0, 0, 0, 0, 0, 1};
static signed char ctbl172nit_B[30] = {0, 0, 0, 0, 0, 0,0, 0, 2, -1, 0, 2, 2, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, 0, 0, 1};
static signed char ctbl183nit_B[30] = {0, 0, 0, 0, 0, 0,-1, 0, 2, -1, 0, 2, 1, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, 0, 0, 1};
static signed char ctbl195nit_B[30] = {0, 0, 0, 0, 0, 0,0, 0, 4, -1, 0, -1, 2, 0, -1, 0, 0, 1, -1, 0, 0, 0, 0, 0, 0, 0, -1, 0, 0, 1};
static signed char ctbl207nit_B[30] = {0, 0, 0, 0, 0, 0,0, 0, 4, -1, 0, -1, 2, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, -1, 1, 0, 0, 0, 0, 0};
static signed char ctbl220nit_B[30] = {0, 0, 0, 0, 0, 0,0, 0, 8, -2, 0, -2, 2, 0, -1, 0, 0, 2, 0, 0, 0, -1, 0, -1, 1, 0, 0, 0, 0, 0};
static signed char ctbl234nit_B[30] = {0, 0, 0, 0, 0, 0,0, 0, 8, -2, 0, -2, 2, 0, -1, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, -1, 0, 0, 0};
static signed char ctbl249nit_B[30] = {0, 0, 0, 0, 0, 0,0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static signed char ctbl265nit_B[30] = {0, 0, 0, 0, 0, 0,0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static signed char ctbl282nit_B[30] = {0, 0, 0, 0, 0, 0,0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static signed char ctbl300nit_B[30] = {0, 0, 0, 0, 0, 0,0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static signed char ctbl316nit_B[30] = {0, 0, 0, 0, 0, 0,0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static signed char ctbl333nit_B[30] = {0, 0, 0, 0, 0, 0,0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static signed char ctbl360nit_B[30] = {0, 0, 0, 0, 0, 0,0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static signed char aid5_B[5] = {0xB2, 0x00, 0x0E ,0x07, 0x47};
static signed char aid6_B[5] = {0xB2, 0x00, 0x0E ,0x07, 0x36};
static signed char aid7_B[5] = {0xB2, 0x00, 0x0E ,0x07, 0x26};
static signed char aid8_B[5] = {0xB2, 0x00, 0x0E ,0x07, 0x19};
static signed char aid9_B[5] = {0xB2, 0x00, 0x0E ,0x07, 0x0A};
static signed char aid10_B[5] = {0xB2, 0x00, 0x0E ,0x07, 0x00};
static signed char aid11_B[5] = {0xB2, 0x00, 0x0E ,0x06, 0xF1};
static signed char aid12_B[5] = {0xB2, 0x00, 0x0E ,0x06, 0xE1};
static signed char aid13_B[5] = {0xB2, 0x00, 0x0E ,0x06, 0xD1};
static signed char aid14_B[5] = {0xB2, 0x00, 0x0E ,0x06, 0xC1};
static signed char aid15_B[5] = {0xB2, 0x00, 0x0E ,0x06, 0xB1};
static signed char aid16_B[5] = {0xB2, 0x00, 0x0E ,0x06, 0xA2};
static signed char aid17_B[5] = {0xB2, 0x00, 0x0E ,0x06, 0x8A};
static signed char aid19_B[5] = {0xB2, 0x00, 0x0E ,0x06, 0x72};
static signed char aid20_B[5] = {0xB2, 0x00, 0x0E ,0x06, 0x62};
static signed char aid21_B[5] = {0xB2, 0x00, 0x0E ,0x06, 0x51};
static signed char aid22_B[5] = {0xB2, 0x00, 0x0E ,0x06, 0x40};
static signed char aid24_B[5] = {0xB2, 0x00, 0x0E ,0x06, 0x20};
static signed char aid25_B[5] = {0xB2, 0x00, 0x0E ,0x06, 0x05};
static signed char aid27_B[5] = {0xB2, 0x00, 0x0E ,0x05, 0xE2};
static signed char aid29_B[5] = {0xB2, 0x00, 0x0E ,0x05, 0xBA};
static signed char aid30_B[5] = {0xB2, 0x00, 0x0E ,0x05, 0xA7};
static signed char aid32_B[5] = {0xB2, 0x00, 0x0E ,0x05, 0x85};
static signed char aid34_B[5] = {0xB2, 0x00, 0x0E ,0x05, 0x65};
static signed char aid37_B[5] = {0xB2, 0x00, 0x0E ,0x05, 0x33};
static signed char aid39_B[5] = {0xB2, 0x00, 0x0E ,0x05, 0x10};
static signed char aid41_B[5] = {0xB2, 0x00, 0x0E ,0x04, 0xE8};
static signed char aid44_B[5] = {0xB2, 0x00, 0x0E ,0x04, 0xB5};
static signed char aid47_B[5] = {0xB2, 0x00, 0x0E ,0x04, 0x82};
static signed char aid50_B[5] = {0xB2, 0x00, 0x0E ,0x04, 0x49};
static signed char aid53_B[5] = {0xB2, 0x00, 0x0E ,0x04, 0x16};
static signed char aid56_B[5] = {0xB2, 0x00, 0x0E ,0x03, 0xE3};
static signed char aid60_B[5] = {0xB2, 0x00, 0x0E ,0x03, 0x97};
static signed char aid64_B[5] = {0xB2, 0x00, 0x0E ,0x03, 0x52};
static signed char aid68_B[5] = {0xB2, 0x00, 0x0E ,0x03, 0x06};
static signed char aid172_B[5] = {0xB2, 0x00, 0x0E ,0x02, 0xB5};
static signed char aid183_B[5] = {0xB2, 0x00, 0x0E ,0x02, 0x57};
static signed char aid195_B[5] = {0xB2, 0x00, 0x0E ,0x01, 0xF2};
static signed char aid207_B[5] = {0xB2, 0x00, 0x0E ,0x01, 0x84};
static signed char aid220_B[5] = {0xB2, 0x00, 0x0E ,0x01, 0x0B};
static signed char aid234_B[5] = {0xB2, 0x00, 0x0E ,0x00, 0x92};
static signed char aid249_B[5] = {0xB2, 0x00, 0x0E ,0x00, 0x10};

static unsigned char elv84[6] = {0xB6, 0x58, 0x84, 0x12, 0x1C, 0x1F};
static unsigned char elv85[6] = {0xB6, 0x58, 0x85, 0x00, 0x00, 0x00};
static unsigned char elv86[6] = {0xB6, 0x58, 0x86, 0x12, 0x1A, 0x1E};
static unsigned char elv87[6] = {0xB6, 0x58, 0x87, 0x00, 0x00, 0x00};
static unsigned char elv88[6] = {0xB6, 0x58, 0x88, 0x12, 0x18, 0x1C};
static unsigned char elv89[6] = {0xB6, 0x58, 0x89, 0x00, 0x00, 0x00};
static unsigned char elv8a[6] = {0xB6, 0x58, 0x8A, 0x12, 0x16, 0x1A};
static unsigned char elv8b[6] = {0xB6, 0x58, 0x8B, 0x00, 0x00, 0x00};
static unsigned char elv8c[6] = {0xB6, 0x58, 0x8C, 0x12, 0x15, 0x18};
static unsigned char elv8d[6] = {0xB6, 0x58, 0x8D, 0x00, 0x00, 0x00};
static unsigned char elv8e[6] = {0xB6, 0x58, 0x8E, 0x12, 0x14, 0x16};
static unsigned char elv8f[6] = {0xB6, 0x58, 0x8F, 0x00, 0x00, 0x00};
static unsigned char elv90[6] = {0xB6, 0x58, 0x90, 0x12, 0x12, 0x14};
static unsigned char elv91[6] = {0xB6, 0x58, 0x91, 0x00, 0x00, 0x00};
static unsigned char elv92[6] = {0xB6, 0x58, 0x92, 0x00, 0x00, 0x00};

static unsigned char m_gray_005[] = {0, 2, 19, 23, 29, 36, 58, 95, 125, 151};
static unsigned char m_gray_006[] = {0, 2, 19, 22, 28, 36, 58, 95, 125, 151};
static unsigned char m_gray_007[] = {0, 2, 18, 21, 27, 36, 57, 95, 124, 151};
static unsigned char m_gray_008[] = {0, 2, 18, 21, 27, 35, 58, 95, 124, 151};
static unsigned char m_gray_009[] = {0, 2, 17, 20, 26, 35, 57, 95, 125, 151};
static unsigned char m_gray_010[] = {0, 2, 16, 20, 26, 35, 57, 94, 124, 151};
static unsigned char m_gray_011[] = {0, 2, 16, 19, 25, 35, 56, 94, 124, 151};
static unsigned char m_gray_012[] = {0, 2, 15, 19, 25, 34, 56, 94, 124, 151};
static unsigned char m_gray_013[] = {0, 2, 14, 19, 25, 34, 56, 94, 124, 151};
static unsigned char m_gray_014[] = {0, 2, 14, 19, 25, 34, 56, 94, 124, 151};
static unsigned char m_gray_015[] = {0, 2, 14, 18, 24, 34, 56, 94, 124, 151};
static unsigned char m_gray_016[] = {0, 2, 13, 18, 24, 33, 56, 94, 125, 151};
static unsigned char m_gray_017[] = {0, 2, 13, 18, 24, 33, 56, 93, 124, 151};
static unsigned char m_gray_019[] = {0, 2, 13, 18, 24, 33, 56, 94, 124, 151};
static unsigned char m_gray_020[] = {0, 2, 13, 18, 24, 33, 56, 94, 124, 151};
static unsigned char m_gray_021[] = {0, 2, 13, 18, 24, 33, 56, 93, 125, 151};
static unsigned char m_gray_022[] = {0, 2, 14, 19, 25, 34, 56, 94, 125, 151};
static unsigned char m_gray_024[] = {0, 2, 14, 19, 25, 34, 56, 94, 125, 151};
static unsigned char m_gray_025[] = {0, 2, 15, 20, 26, 34, 56, 94, 125, 151};
static unsigned char m_gray_027[] = {0, 2, 15, 20, 26, 35, 57, 94, 125, 151};
static unsigned char m_gray_029[] = {0, 2, 15, 20, 26, 35, 57, 94, 125, 151};
static unsigned char m_gray_030[] = {0, 2, 15, 20, 26, 35, 57, 94, 125, 151};
static unsigned char m_gray_032[] = {0, 2, 15, 20, 26, 35, 57, 94, 124, 151};
static unsigned char m_gray_034[] = {0, 2, 14, 19, 26, 35, 57, 94, 124, 151};
static unsigned char m_gray_037[] = {0, 2, 14, 19, 25, 34, 56, 94, 124, 151};
static unsigned char m_gray_039[] = {0, 2, 13, 19, 25, 34, 56, 94, 124, 151};
static unsigned char m_gray_041[] = {0, 2, 13, 19, 24, 34, 56, 93, 124, 151};
static unsigned char m_gray_044[] = {0, 2, 13, 18, 24, 34, 56, 93, 124, 151};
static unsigned char m_gray_047[] = {0, 2, 12, 18, 24, 34, 56, 93, 124, 151};
static unsigned char m_gray_050[] = {0, 2, 12, 18, 24, 33, 56, 93, 124, 151};
static unsigned char m_gray_053[] = {0, 2, 11, 17, 24, 33, 56, 93, 124, 151};
static unsigned char m_gray_056[] = {0, 2, 11, 17, 24, 33, 55, 93, 124, 151};
static unsigned char m_gray_060[] = {0, 2, 11, 17, 23, 33, 55, 93, 124, 151};
static unsigned char m_gray_064[] = {0, 2, 11, 17, 23, 33, 55, 93, 124, 151};
static unsigned char m_gray_068[] = {0, 8, 10, 17, 23, 33, 55, 93, 124, 151};
static unsigned char m_gray_072[] = {0, 8, 10, 17, 23, 33, 56, 95, 126, 155};
static unsigned char m_gray_077[] = {0, 8, 10, 17, 24, 34, 58, 97, 129, 159};
static unsigned char m_gray_082[] = {0, 8, 11, 18, 26, 35, 59, 100, 133, 164};
static unsigned char m_gray_087[] = {0, 8, 11, 18, 26, 36, 62, 103, 136, 168};
static unsigned char m_gray_093[] = {0, 8, 11, 18, 26, 37, 63, 106, 140, 173};
static unsigned char m_gray_098[] = {0, 8, 11, 19, 27, 38, 64, 108, 143, 176};
static unsigned char m_gray_105[] = {0, 7, 11, 19, 28, 39, 67, 112, 147, 182};
static unsigned char m_gray_111[] = {0, 7, 11, 19, 28, 40, 68, 113, 150, 186};
static unsigned char m_gray_119[] = {0, 7, 11, 20, 29, 41, 70, 117, 153, 191};
static unsigned char m_gray_126[] = {0, 7, 11, 20, 29, 42, 72, 121, 159, 196};
static unsigned char m_gray_134[] = {0, 7, 11, 21, 30, 44, 74, 124, 163, 202};
static unsigned char m_gray_143[] = {0, 7, 11, 21, 31, 44, 75, 127, 167, 207};
static unsigned char m_gray_152[] = {0, 7, 11, 21, 32, 46, 77, 130, 172, 212};
static unsigned char m_gray_162[] = {0, 7, 12, 22, 33, 47, 79, 134, 177, 218};
static unsigned char m_gray_172[] = {0, 6, 11, 22, 32, 46, 79, 134, 177, 218};
static unsigned char m_gray_183[] = {0, 6, 11, 22, 32, 46, 79, 134, 176, 218};
static unsigned char m_gray_195[] = {0, 6, 11, 21, 32, 46, 78, 133, 176, 218};
static unsigned char m_gray_207[] = {0, 5, 11, 21, 32, 46, 78, 132, 175, 218};
static unsigned char m_gray_220[] = {0, 5, 10, 21, 31, 45, 77, 132, 175, 218};
static unsigned char m_gray_234[] = {0, 5, 10, 21, 31, 45, 77, 132, 175, 218};
static unsigned char m_gray_249[] = {0, 5, 10, 21, 31, 45, 77, 132, 174, 218};
static unsigned char m_gray_265[] = {0, 5, 10, 21, 32, 46, 79, 135, 179, 223};
static unsigned char m_gray_282[] = {0, 4, 11, 22, 33, 47, 81, 138, 184, 230};
static unsigned char m_gray_300[] = {0, 4, 11, 22, 34, 48, 83, 142, 190, 236};
static unsigned char m_gray_316[] = {0, 5, 11, 22, 34, 49, 84, 145, 193, 241};
static unsigned char m_gray_333[] = {0, 4, 11, 23, 35, 50, 86, 148, 199, 247};
static unsigned char m_gray_360[] = {0, 3, 11, 23, 35, 51, 87, 151, 203, 255};

#endif
