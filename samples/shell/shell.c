/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define __main__
#include "ztacx.h"
#include "ztacx_led.h"

static struct ztacx_variable *led_duty;
static struct ztacx_variable *led_cycle;
static struct ztacx_variable *led_blink;
static struct ztacx_variable *led_lit;

static int app_init(const struct device *unused) 
{
	LOG_INF("NOTICE Initialising app variables");
#if CONFIG_ZTACX_LEAF_SETTINGS
	ZTACX_SETTING_FIND_AS(led_duty, led0_duty);
	ZTACX_SETTING_FIND_AS(led_cycle, led0_cycle);
#endif
	//ZTACX_VAR_FIND_AS(led_blink, led0_blink);
	//ZTACX_VAR_FIND_AS(led_lit, led0_lit);

	return 0;
}
SYS_INIT(app_init, APPLICATION, ZTACX_APP_INIT_PRIORITY);

void main(void)
{
	printk("Ztacx shell sample\n");
	LOG_WRN("Ztacx shell sample\n");
}
