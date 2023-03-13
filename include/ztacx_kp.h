#pragma once

/*
 * Simple non-matrix keypad using PCF8574 I2C gpio
 *
 * Settings:
 *	kp_bus_name
 *      kp_i2c_addr
 *
 * Values:
 *      ztacx_kp_ok  ZTACX_VALUE_BOOL
 *	ztacx_kp_pins ZTACX_VALUE_BYTE
 */

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
};

extern struct ztacx_kp_context ztacx_kp_kp0_context;

ZTACX_CLASS_DEFINE(kp, ((struct ztacx_leaf_cb){.init=&ztacx_kp_init,.start=&ztacx_kp_start}));
ZTACX_LEAF_DEFINE(kp, kp0, &ztacx_kp_kp0_context);

