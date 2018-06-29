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

#ifndef __EA8061_AID_DIMMING_H__
#define __EA8061_AID_DIMMING_H__
//rev D
static signed char D_aid9685[5] = {0xB3, 0x00, 0x00, 0x04, 0xEB};
static signed char D_aid9585[5] = {0xB3, 0x00, 0x00, 0x04, 0xDE};
static signed char D_aid9523[5] = {0xB3, 0x00, 0x00, 0x04, 0xD6};
static signed char D_aid9438[5] = {0xB3, 0x00, 0x00, 0x04, 0xCB};
static signed char D_aid9338[5] = {0xB3, 0x00, 0x00, 0x04, 0xBE};
static signed char D_aid9285[5] = {0xB3, 0x00, 0x00, 0x04, 0xB7};
static signed char D_aid9200[5] = {0xB3, 0x00, 0x00, 0x04, 0xAC};
static signed char D_aid9100[5] = {0xB3, 0x00, 0x00, 0x04, 0x9F};
static signed char D_aid9046[5] = {0xB3, 0x00, 0x00, 0x04, 0x98};
static signed char D_aid8954[5] = {0xB3, 0x00, 0x00, 0x04, 0x8C};
static signed char D_aid8923[5] = {0xB3, 0x00, 0x00, 0x04, 0x88};
static signed char D_aid8800[5] = {0xB3, 0x00, 0x00, 0x04, 0x78};
static signed char D_aid8715[5] = {0xB3, 0x00, 0x00, 0x04, 0x6D};
static signed char D_aid8546[5] = {0xB3, 0x00, 0x00, 0x04, 0x57};
static signed char D_aid8462[5] = {0xB3, 0x00, 0x00, 0x04, 0x4C};
static signed char D_aid8346[5] = {0xB3, 0x00, 0x00, 0x04, 0x3D};
static signed char D_aid8246[5] = {0xB3, 0x00, 0x00, 0x04, 0x30};
static signed char D_aid8085[5] = {0xB3, 0x00, 0x00, 0x04, 0x1B};
static signed char D_aid7969[5] = {0xB3, 0x00, 0x00, 0x04, 0x0C};
static signed char D_aid7769[5] = {0xB3, 0x00, 0x00, 0x03, 0xF2};
static signed char D_aid7577[5] = {0xB3, 0x00, 0x00, 0x03, 0xD9};
static signed char D_aid7508[5] = {0xB3, 0x00, 0x00, 0x03, 0xD0};
static signed char D_aid7323[5] = {0xB3, 0x00, 0x00, 0x03, 0xB8};
static signed char D_aid7138[5] = {0xB3, 0x00, 0x00, 0x03, 0xA0};
static signed char D_aid6892[5] = {0xB3, 0x00, 0x00, 0x03, 0x80};
static signed char D_aid6715[5] = {0xB3, 0x00, 0x00, 0x03, 0x69};
static signed char D_aid6531[5] = {0xB3, 0x00, 0x00, 0x03, 0x51};
static signed char D_aid6262[5] = {0xB3, 0x00, 0x00, 0x03, 0x2E};
static signed char D_aid6000[5] = {0xB3, 0x00, 0x00, 0x03, 0x0C};
static signed char D_aid5731[5] = {0xB3, 0x00, 0x00, 0x02, 0xE9};
static signed char D_aid5454[5] = {0xB3, 0x00, 0x00, 0x02, 0xC5};
static signed char D_aid5177[5] = {0xB3, 0x00, 0x00, 0x02, 0xA1};
static signed char D_aid4800[5] = {0xB3, 0x00, 0x00, 0x02, 0x70};
static signed char D_aid4438[5] = {0xB3, 0x00, 0x00, 0x02, 0x41};
static signed char D_aid4062[5] = {0xB3, 0x00, 0x00, 0x02, 0x10};
static signed char D_aid3662[5] = {0xB3, 0x00, 0x00, 0x01, 0xDC};
static signed char D_aid3200[5] = {0xB3, 0x00, 0x00, 0x01, 0xA0};
static signed char D_aid2708[5] = {0xB3, 0x00, 0x00, 0x01, 0x60};
static signed char D_aid2185[5] = {0xB3, 0x00, 0x00, 0x01, 0x1C};
static signed char D_aid1654[5] = {0xB3, 0x00, 0x00, 0x00, 0xD7};
static signed char D_aid1038[5] = {0xB3, 0x00, 0x00, 0x00, 0x87};
static signed char D_aid0369[5] = {0xB3, 0x00, 0x00, 0x00, 0x30};

static signed char D_ctbl5nit[30] = {0, 0, 0, 0, 0, 0,-42, 0, -11, -20, 0, -4, -7, 0, 0, -13, 0, 3, -7, 0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1};
static signed char D_ctbl6nit[30] = {0, 0, 0, 0, 0, 0,-41, 0, -9, -17, 0, -4, -6, 0, 0, -13, 0, 3, -6, 0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1};
static signed char D_ctbl7nit[30] = {0, 0, 0, 0, 0, 0,-40, 0, -9, -16, 0, -3, -7, 0, 0, -11, 0, 3, -5, 0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1};
static signed char D_ctbl8nit[30] = {0, 0, 0, 0, 0, 0,-38, 0, -8, -14, 0, -4, -7, 0, 0, -10, 0, 3, -4, 0, -1, 1, 0, 1, -1, 0, -1, 0, 0, 1};
static signed char D_ctbl9nit[30] = {0, 0, 0, 0, 0, 0,-36, 0, -6, -14, 0, -4, -7, 0, 0, -8, 0, 3, -4, 0, -1, 1, 0, 1, -1, 0, -1, 0, 0, 1};
static signed char D_ctbl10nit[30] = {0, 0, 0, 0, 0, 0,-34, 0, -6, -13, 0, -4, -5, 0, 0, -7, 0, 3, -4, 0, -1, 1, 0, 1, -1, 0, -1, 0, 0, 1};
static signed char D_ctbl11nit[30] = {0, 0, 0, 0, 0, 0,-32, 0, -5, -12, 0, -4, -5, 0, 0, -8, 0, 2, -3, 0, -1, 1, 0, 1, -1, 0, -1, 0, 0, 1};
static signed char D_ctbl12nit[30] = {0, 0, 0, 0, 0, 0,-30, 0, -5, -11, 0, -4, -5, 0, 0, -7, 0, 2, -2, 0, -1, 0, 0, 1, 0, 0, -1, 0, 0, 1};
static signed char D_ctbl13nit[30] = {0, 0, 0, 0, 0, 0,-29, 0, -5, -10, 0, -4, -5, 0, 0, -6, 0, 2, -2, 0, -1, 0, 0, 1, 0, 0, -1, 0, 0, 1};
static signed char D_ctbl14nit[30] = {0, 0, 0, 0, 0, 0,-28, 0, -5, -9, 0, -4, -5, 0, 0, -6, 0, 2, -1, 0, 0, -1, 0, 0, 0, 0, -1, 0, 0, 1};
static signed char D_ctbl15nit[30] = {0, 0, 0, 0, 0, 0,-27, 0, -5, -9, 0, -4, -3, 0, 0, -7, 0, 2, -1, 0, 0, -1, 0, 0, 0, 0, -1, 0, 0, 1};
static signed char D_ctbl16nit[30] = {0, 0, 0, 0, 0, 0,-25, 0, -5, -8, 0, -4, -3, 0, 0, -7, 0, 1, -1, 0, 0, -1, 0, 0, 1, 0, -1, 0, 0, 1};
static signed char D_ctbl17nit[30] = {0, 0, 0, 0, 0, 0,-23, 0, -6, -7, 0, -4, -3, 0, 0, -7, 0, 1, -1, 0, 0, -1, 0, 0, 1, 0, -1, 0, 0, 1};
static signed char D_ctbl19nit[30] = {0, 0, 0, 0, 0, 0,-23, 0, -6, -5, 0, -4, -3, 0, 0, -6, 0, 1, -1, 0, 0, -1, 0, 0, 1, 0, -1, 0, 0, 1};
static signed char D_ctbl20nit[30] = {0, 0, 0, 0, 0, 0,-20, 1, -3, -5, 0, -4, -3, 0, 0, -6, 0, 1, -1, 0, 0, -1, 0, 0, 1, 0, -1, 0, 0, 1};
static signed char D_ctbl21nit[30] = {0, 0, 0, 0, 0, 0,-18, 0, -4, -4, 0, -4, -3, 0, 0, -5, 0, 1, -2, 0, -1, -1, 0, 0, 1, 0, -1, 0, 0, 1};
static signed char D_ctbl22nit[30] = {0, 0, 0, 0, 0, 0,-16, 0, -5, -4, 0, -5, -4, 0, 0, -4, 0, 1, -2, 0, -1, -1, 0, 0, 1, 0, 0, 0, 0, 0};
static signed char D_ctbl24nit[30] = {0, 0, 0, 0, 0, 0,-14, 0, -4, -3, 0, -4, -3, 0, -1, -4, 0, 1, -2, 0, -1, -1, 0, 0, 1, 0, 0, 0, 0, 0};
static signed char D_ctbl25nit[30] = {0, 0, 0, 0, 0, 0,-12, 0, -5, -3, 0, -4, -2, 0, -2, -5, 0, 0, -2, 0, -1, -1, 0, 0, 0, 0, 0, 1, 0, 0};
static signed char D_ctbl27nit[30] = {0, 0, 0, 0, 0, 0,-10, 0, -8, -4, 0, -5, -3, 0, -3, -5, 0, 0, -1, 0, -1, -1, 0, 0, 0, 0, 0, 1, 0, 0};
static signed char D_ctbl29nit[30] = {0, 0, 0, 0, 0, 0,-10, 0, -10, -3, 0, -6, -3, 0, -3, -5, 0, 0, -1, 0, -1, -1, 0, 0, 0, 0, 0, 1, 0, 0};
static signed char D_ctbl30nit[30] = {0, 0, 0, 0, 0, 0,-11, 0, -6, -2, 0, -5, -2, 0, -2, -5, 0, 0, -1, 0, -1, -1, 0, 0, 0, 0, 0, 1, 0, 0};
static signed char D_ctbl32nit[30] = {0, 0, 0, 0, 0, 0,-15, 0, -10, -1, 0, -4, -2, 0, -2, -4, 0, 0, -1, 0, -1, -1, 0, 0, 0, 0, 0, 1, 0, 0};
static signed char D_ctbl34nit[30] = {0, 0, 0, 0, 0, 0,-13, 0, -8, -1, 0, -4, -2, 0, -2, -4, 0, 0, -1, 0, -1, -1, 0, 0, 0, 0, 0, 1, 0, 0};
static signed char D_ctbl37nit[30] = {0, 0, 0, 0, 0, 0,-12, 0, -6, -2, 0, -5, -2, 0, -2, -4, 0, 0, -1, 0, -1, -1, 0, 0, 0, 0, 0, 1, 0, 0};
static signed char D_ctbl39nit[30] = {0, 0, 0, 0, 0, 0,-13, 0, -9, -2, 0, -5, -1, 0, -2, -4, 0, 0, -1, 0, -1, -1, 0, 0, 0, 0, 0, 1, 0, 0};
static signed char D_ctbl41nit[30] = {0, 0, 0, 0, 0, 0,-12, 0, -8, -2, 0, -5, -1, 0, -2, -4, 0, 0, -1, 0, -1, -1, 0, 0, 0, 0, 0, 1, 0, 0};
static signed char D_ctbl44nit[30] = {0, 0, 0, 0, 0, 0,-11, 0, -6, -2, 0, -5, -1, 0, -2, -4, 0, 0, -1, 0, -1, -1, 0, 0, 0, 0, 0, 1, 0, 0};
static signed char D_ctbl47nit[30] = {0, 0, 0, 0, 0, 0,-10, 0, -4, -1, 0, -5, -1, 0, -2, -4, 0, 0, -1, 0, -1, -1, 0, 0, 0, 0, 0, 1, 0, 0};
static signed char D_ctbl50nit[30] = {0, 0, 0, 0, 0, 0,-9, 0, -6, 0, 0, -5, -1, 0, -2, -4, 0, 0, -1, 0, -1, -1, 0, 0, 0, 0, 0, 1, 0, 0};
static signed char D_ctbl53nit[30] = {0, 0, 0, 0, 0, 0,-8, 0, -4, 0, 0, -5, -1, 0, -2, -3, 0, 0, -1, 0, -1, -1, 0, 0, 0, 0, 0, 1, 0, 0};
static signed char D_ctbl56nit[30] = {0, 0, 0, 0, 0, 0,-7, 0, -3, 0, 0, -5, -1, 0, -2, -3, 0, 0, -1, 0, -1, -1, 0, 0, 0, 0, 0, 1, 0, 0};
static signed char D_ctbl60nit[30] = {0, 0, 0, 0, 0, 0,-6, 0, -1, 1, 0, -5, -1, 0, -2, -3, 0, 0, -1, 0, -1, -1, 0, 0, 0, 0, 0, 1, 0, 0};
static signed char D_ctbl64nit[30] = {0, 0, 0, 0, 0, 0,-6, 0, -1, 2, 0, -5, -1, 0, -2, -3, 0, 0, -1, 0, -1, -1, 0, 0, 0, 0, 0, 1, 0, 0};
static signed char D_ctbl68nit[30] = {0, 0, 0, 0, 0, 0,-5, 0, -2, 2, 0, -5, -1, 0, -2, -3, 0, 0, -1, 0, -1, -1, 0, 0, 0, 0, 0, 1, 0, 0};
static signed char D_ctbl72nit[30] = {0, 0, 0, 0, 0, 0,-6, 0, -2, 1, 0, -4, -1, 0, -3, -3, 0, 0, 0, 0, -1, -1, 0, 0, 0, 0, 0, 1, 0, 0};
static signed char D_ctbl77nit[30] = {0, 0, 0, 0, 0, 0,-6, 0, -1, 1, 0, -5, 0, 0, -2, -3, 0, 0, 0, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0};
static signed char D_ctbl82nit[30] = {0, 0, 0, 0, 0, 0,-5, 0, 0, 2, 0, -5, -1, 0, -2, -3, 0, 0, 0, 0, -1, 1, 0, 0, 0, 0, 0, 1, 0, 1};
static signed char D_ctbl87nit[30] = {0, 0, 0, 0, 0, 0,-6, 0, 0, 1, 0, -5, -1, 0, -2, -2, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0};
static signed char D_ctbl93nit[30] = {0, 0, 0, 0, 0, 0,-6, 0, 0, 1, 0, -5, -1, 0, -2, -2, 0, 0, 0, 0, 0, -1, 0, -1, 1, 0, 0, 0, 0, 1};
static signed char D_ctbl98nit[30] = {0, 0, 0, 0, 0, 0,-6, 0, 0, 1, 0, -5, -1, 0, -2, -2, 0, 0, 0, 0, 0, -1, 0, -1, 1, 0, 0, 0, 0, 1};
static signed char D_ctbl105nit[30] = {0, 0, 0, 0, 0, 0,-6, 0, -4, 1, 0, -3, -1, 0, -2, -1, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 1};
static signed char D_ctbl111nit[30] = {0, 0, 0, 0, 0, 0,-6, 0, -4, 1, 0, -1, 0, 0, -1, -1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static signed char D_ctbl119nit[30] = {0, 0, 0, 0, 0, 0,-4, 0, -3, 1, 0, -3, 0, 0, -1, -1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static signed char D_ctbl126nit[30] = {0, 0, 0, 0, 0, 0,-4, 0, -2, 0, 0, -3, 0, 0, -1, -1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static signed char D_ctbl134nit[30] = {0, 0, 0, 0, 0, 0,-4, 0, -3, 0, 0, -2, 0, 0, 0, -1, 0, 1, 0, 0, 0, 0, 0, 0, -1, 0, 0, 0, 0, 0};
static signed char D_ctbl143nit[30] = {0, 0, 0, 0, 0, 0,-4, 0, -2, 0, 0, -2, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, 0, 0, 1};
static signed char D_ctbl152nit[30] = {0, 0, 0, 0, 0, 0,-4, 0, -2, 0, 0, -1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, 0, 0, 1};
static signed char D_ctbl162nit[30] = {0, 0, 0, 0, 0, 0,-4, 0, -2, -1, 0, -1, 0, 0, 0, -1, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0};
static signed char D_ctbl172nit[30] = {0, 0, 0, 0, 0, 0,-4, 0, -2, -1, 0, -1, 0, 0, 0, -1, 0, 0, 1, 0, 2, -1, 0, 0, -1, 0, 0, 0, 0, 0};
static signed char D_ctbl183nit[30] = {0, 0, 0, 0, 0, 0,-2, 0, -1, -1, 0, -1, 0, 0, 0, -1, 0, 0, 1, 0, 2, -1, 0, 0, -1, 0, 0, 0, 0, 0};
static signed char D_ctbl195nit[30] = {0, 0, 0, 0, 0, 0,-2, 0, -1, -1, 0, -2, 1, 0, 0, -1, 0, 0, 1, 0, 1, -1, 0, 0, -1, 0, 0, 0, 0, 0};
static signed char D_ctbl207nit[30] = {0, 0, 0, 0, 0, 0,-2, 0, -1, -1, 0, -2, 1, 0, 0, -1, 0, 0, 1, 0, 1, -1, 0, 0, -1, 0, 0, 0, 0, 0};
static signed char D_ctbl220nit[30] = {0, 0, 0, 0, 0, 0,-1, 0, 0, -1, 0, -2, 1, 0, 0, -1, 0, 0, 1, 0, 1, -1, 0, 0, -1, 0, 0, 0, 0, 0};
static signed char D_ctbl234nit[30] = {0, 0, 0, 0, 0, 0,-1, 0, 0, 0, 0, -2, 1, 0, 0, -1, 0, 0, 1, 0, 1, -1, 0, 0, -1, 0, 0, 0, 0, 0};
static signed char D_ctbl249nit[30] = {0, 0, 0, 0, 0, 0,0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static signed char D_ctbl265nit[30] = {0, 0, 0, 0, 0, 0,0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static signed char D_ctbl282nit[30] = {0, 0, 0, 0, 0, 0,0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static signed char D_ctbl300nit[30] = {0, 0, 0, 0, 0, 0,0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static signed char D_ctbl316nit[30] = {0, 0, 0, 0, 0, 0,0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static signed char D_ctbl333nit[30] = {0, 0, 0, 0, 0, 0,0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static signed char D_ctbl350nit[30] = {0, 0, 0, 0, 0, 0,0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static unsigned char elv0 [4] = {0x0F, 0x05, 0x0B, 0x11}; // 5 ~2 0
static unsigned char elv1 [4] = {0x0F, 0x07, 0x0D, 0x12}; // 21
static unsigned char elv2 [4] = {0x11, 0x08, 0x0B, 0x11}; // 22
static unsigned char elv3 [4] = {0x15, 0x07, 0x09, 0x0E}; // 24
static unsigned char elv4 [4] = {0x18, 0x07, 0x08, 0x0C}; //25
static unsigned char elv5 [4] = {0x1B, 0x07, 0x07, 0x0A}; //27
static unsigned char elv6 [4] = {0x19, 0x0C, 0x0B, 0x0D}; //29

static unsigned char elv7 [4] = {0x1C, 0x00, 0x00, 0x00}; //30 ~ 105
static unsigned char elv8 [4] = {0x1B, 0x00, 0x00, 0x00}; //111~119
static unsigned char elv9 [4] = {0x1A, 0x00, 0x00, 0x00}; //126~183
static unsigned char elv10 [4] = {0x19, 0x00, 0x00, 0x00};  //195
static unsigned char elv11 [4] = {0x18, 0x00, 0x00, 0x00};  //207
static unsigned char elv12 [4] = {0x17, 0x00, 0x00, 0x00};  //220
static unsigned char elv13 [4] = {0x16, 0x00, 0x00, 0x00};  //234
static unsigned char elv14 [4] = {0x15, 0x00, 0x00, 0x00};  //249
static unsigned char elv15 [4] = {0x14, 0x00, 0x00, 0x00};  //265
static unsigned char elv16 [4] = {0x13, 0x00, 0x00, 0x00};  //282
static unsigned char elv17 [4] = {0x12, 0x00, 0x00, 0x00};  //300
static unsigned char elv18 [4] = {0x11, 0x00, 0x00, 0x00};  //316
static unsigned char elv19 [4] = {0x10, 0x00, 0x00, 0x00};  //333
static unsigned char elv20 [4] = {0x0F, 0x00, 0x00, 0x00};  //350

static unsigned char m_gray_005[] = {0, 14, 17, 21, 26, 34, 53, 88, 119, 145};
static unsigned char m_gray_006[] = {0, 14, 17, 21, 26, 34, 53, 88, 119, 145};
static unsigned char m_gray_007[] = {0, 12, 15, 20, 25, 33, 52, 88, 119, 145};
static unsigned char m_gray_008[] = {0, 12, 15, 19, 24, 33, 52, 88, 119, 145};
static unsigned char m_gray_009[] = {0, 12, 15, 19, 24, 33, 52, 88, 119, 145};
static unsigned char m_gray_010[] = {0, 11, 14, 18, 24, 33, 52, 88, 119, 145};
static unsigned char m_gray_011[] = {0, 11, 14, 18, 23, 33, 52, 88, 119, 145};
static unsigned char m_gray_012[] = {0, 10, 13, 18, 23, 33, 52, 88, 119, 145};
static unsigned char m_gray_013[] = {0, 9, 12, 18, 23, 33, 52, 88, 119, 145};
static unsigned char m_gray_014[] = {0, 9, 12, 18, 23, 32, 52, 88, 119, 145};
static unsigned char m_gray_015[] = {0, 8, 11, 18, 23, 32, 52, 88, 119, 145};
static unsigned char m_gray_016[] = {0, 8, 11, 17, 23, 32, 52, 88, 119, 145};
static unsigned char m_gray_017[] = {0, 8, 11, 17, 23, 32, 52, 88, 119, 145};
static unsigned char m_gray_019[] = {0, 7, 10, 17, 23, 32, 52, 88, 118, 145};
static unsigned char m_gray_020[] = {0, 7, 10, 17, 23, 32, 52, 88, 118, 145};
static unsigned char m_gray_021[] = {0, 8, 11, 17, 23, 32, 52, 88, 118, 145};
static unsigned char m_gray_022[] = {0, 9, 12, 17, 23, 32, 52, 88, 118, 145};
static unsigned char m_gray_024[] = {0, 10, 13, 18, 23, 32, 52, 88, 118, 145};
static unsigned char m_gray_025[] = {0, 10, 13, 18, 24, 33, 53, 88, 118, 145};
static unsigned char m_gray_027[] = {0, 10, 13, 18, 24, 33, 53, 89, 118, 145};
static unsigned char m_gray_029[] = {0, 10, 13, 18, 24, 33, 53, 89, 118, 145};
static unsigned char m_gray_030[] = {0, 10, 13, 18, 24, 33, 52, 89, 118, 145};
static unsigned char m_gray_032[] = {0, 9, 12, 18, 24, 33, 52, 89, 118, 145};
static unsigned char m_gray_034[] = {0, 9, 12, 18, 24, 33, 52, 88, 118, 145};
static unsigned char m_gray_037[] = {0, 9, 12, 18, 23, 33, 52, 88, 118, 145};
static unsigned char m_gray_039[] = {0, 8, 11, 17, 23, 33, 52, 88, 118, 145};
static unsigned char m_gray_041[] = {0, 8, 11, 17, 23, 33, 52, 88, 118, 145};
static unsigned char m_gray_044[] = {0, 8, 11, 17, 23, 32, 52, 88, 118, 145};
static unsigned char m_gray_047[] = {0, 8, 11, 17, 23, 32, 52, 88, 118, 145};
static unsigned char m_gray_050[] = {0, 7, 10, 16, 23, 32, 52, 88, 118, 145};
static unsigned char m_gray_053[] = {0, 7, 10, 16, 23, 32, 52, 88, 118, 145};
static unsigned char m_gray_056[] = {0, 7, 10, 16, 23, 32, 52, 88, 118, 145};
static unsigned char m_gray_060[] = {0, 7, 10, 16, 23, 32, 52, 88, 118, 145};
static unsigned char m_gray_064[] = {0, 7, 10, 16, 23, 32, 52, 88, 118, 145};
static unsigned char m_gray_068[] = {0, 6, 9, 16, 23, 32, 52, 88, 118, 145};
static unsigned char m_gray_072[] = {0, 6, 9, 15, 22, 32, 52, 89, 118, 145};
static unsigned char m_gray_077[] = {0, 6, 9, 15, 22, 33, 54, 91, 122, 149};
static unsigned char m_gray_082[] = {0, 6, 10, 16, 23, 34, 55, 94, 125, 153};
static unsigned char m_gray_087[] = {0, 6, 10, 16, 24, 35, 57, 97, 129, 158};
static unsigned char m_gray_093[] = {0, 6, 10, 17, 24, 36, 59, 100, 132, 163};
static unsigned char m_gray_098[] = {0, 6, 10, 17, 25, 36, 61, 103, 135, 167};
static unsigned char m_gray_105[] = {0, 6, 9, 18, 26, 38, 63, 106, 140, 173};
static unsigned char m_gray_111[] = {0, 6, 10, 18, 27, 38, 64, 109, 143, 177};
static unsigned char m_gray_119[] = {0, 5, 10, 19, 28, 40, 67, 113, 149, 184};
static unsigned char m_gray_126[] = {0, 5, 10, 19, 29, 41, 68, 116, 152, 188};
static unsigned char m_gray_134[] = {0, 5, 10, 19, 30, 42, 70, 119, 156, 193};
static unsigned char m_gray_143[] = {0, 5, 11, 20, 30, 43, 72, 123, 161, 199};
static unsigned char m_gray_152[] = {0, 5, 11, 20, 30, 44, 74, 126, 165, 205};
static unsigned char m_gray_162[] = {0, 5, 11, 21, 31, 45, 76, 130, 170, 211};
static unsigned char m_gray_172[] = {0, 5, 11, 22, 32, 46, 78, 133, 175, 217};
static unsigned char m_gray_183[] = {0, 5, 11, 22, 32, 46, 78, 133, 175, 217};
static unsigned char m_gray_195[] = {0, 4, 11, 21, 32, 46, 77, 133, 175, 217};
static unsigned char m_gray_207[] = {0, 4, 11, 21, 32, 45, 77, 132, 174, 217};
static unsigned char m_gray_220[] = {0, 4, 10, 21, 32, 45, 77, 132, 174, 217};
static unsigned char m_gray_234[] = {0, 4, 10, 21, 32, 45, 77, 132, 174, 217};
static unsigned char m_gray_249[] = {0, 4, 10, 21, 31, 45, 76, 131, 174, 217};
static unsigned char m_gray_265[] = {0, 3, 10, 21, 32, 46, 78, 135, 179, 223};
static unsigned char m_gray_282[] = {0, 3, 10, 22, 33, 47, 81, 138, 184, 230};
static unsigned char m_gray_300[] = {0, 3, 10, 22, 34, 48, 83, 142, 190, 237};
static unsigned char m_gray_316[] = {0, 3, 11, 23, 34, 50, 84, 145, 195, 244};
static unsigned char m_gray_333[] = {0, 3, 11, 23, 35, 51, 86, 149, 199, 250};
static unsigned char m_gray_350[] = {0, 3, 11, 23, 35, 51, 87, 151, 203, 255};

#endif

