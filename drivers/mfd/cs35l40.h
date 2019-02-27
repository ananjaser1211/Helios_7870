/*
 * cs35l40.h  --  MFD internals for Cirrus Logic CS35L40 codec
 *
 * Copyright 2017 Cirrus Logic
 *
 * Author:	David Rhodes	<david.rhodes@cirrus.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */



#ifndef CS35L40_MFD_H
#define CS35L40_MFD_H

#include <linux/pm.h>
#include <linux/of.h>

#include <linux/mfd/cs35l40/core.h>

int cs35l40_dev_init(struct cs35l40_data *cs35l40);
int cs35l40_dev_exit(struct cs35l40_data *cs35l40);

#endif