#pragma once

/*
 * Simple non-matrix keypad using PCF8574 I2C gpio
 *
 * Settings:
 *	kp_bus_name
 *      kp_i2c_addr
 *
 * Values:
 *      ztacx_kp_ok    ZTACX_VALUE_BOOL
 *	ztacx_kp_pins  ZTACX_VALUE_BYTE
 *	ztacx_kp_event ZTACX_VALUE_EVENT
 */

#define KP_EVENT_PRESS   0x8000000
#define KP_EVENT_RELEASE 0x4000000

#define KP_EVENT_KEY0 0x01
#define KP_EVENT_KEY1 0x02
#define KP_EVENT_KEY2 0x04
#define KP_EVENT_KEY3 0x08
#define KP_EVENT_KEY4 0x10
#define KP_EVENT_KEY5 0x20
#define KP_EVENT_KEY6 0x40
#define KP_EVENT_KEY7 0x80

extern int ztacx_kp_init(struct ztacx_leaf *leaf);
extern int ztacx_kp_start(struct ztacx_leaf *leaf);

struct ztacx_kp_context 
{
	bool copy_default_context;
	const struct device *dev;
	struct ztacx_variable *settings;
	int settings_count;
	struct ztacx_variable *values;
	int values_count;
	struct k_work_delayable scan;
	struct k_event event;
};

extern struct ztacx_kp_context ztacx_kp_kp0_context;

ZTACX_CLASS_DEFINE(kp, ((struct ztacx_leaf_cb){.init=&ztacx_kp_init,.start=&ztacx_kp_start}));
ZTACX_LEAF_DEFINE(kp, kp0, &ztacx_kp_kp0_context);

