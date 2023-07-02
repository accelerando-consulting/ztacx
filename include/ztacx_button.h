#pragma once

/*
 * Settings:
 *
 * Values:
 */
#include <zephyr/drivers/gpio.h>

#ifndef ZTACX_BUTTON_MAX
#define ZTACX_BUTTON_MAX 4
#endif


struct ztacx_button_context 
{
	struct gpio_dt_spec button;
	struct gpio_callback button_cb_data;
	struct ztacx_variable state;
#if CONFIG_EVENT
	uint32_t event_bit;
#endif
	struct k_work *handler;
};


extern struct k_event ztacx_button_event;

	
ZTACX_CLASS_AUTO_DEFINE(button);


