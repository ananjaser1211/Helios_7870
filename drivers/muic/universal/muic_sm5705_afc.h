/*
 * Copyright (C) 2010 Samsung Electronics
 * Hyoyoung Kim <hyway.kim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#ifndef __SM5705_H__
#define __SM5705_H__

#include "muic-internal.h"
#include <linux/muic/muic.h>

struct muic_data_t;

int set_afc_ctrl_reg(muic_data_t *pmuic, int shift, bool on);
int set_afc_ctrl_enafc(muic_data_t *pmuic, bool on);
int set_afc_vbus_read(muic_data_t *pmuic, bool on);

#endif /* __SM5705_H__ */
