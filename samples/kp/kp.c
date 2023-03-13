/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define __main__
#include "ztacx.h"

static struct ztacx_variable *kp_pins;

static int app_init(const struct device *unused) 
{
	LOG_INF("NOTICE Initialising app variables");
	ZTACX_VAR_FIND_AS(kp_pins, kp0_pins);

	return 0;
}
SYS_INIT(app_init, APPLICATION, ZTACX_APP_INIT_PRIORITY);

void main(void)
{
	printk("Ztacx keypad sample\n");
	LOG_WRN("Ztacx keypad sample\n");
}
