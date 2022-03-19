#pragma once

/*
 * Settings:
 *	battery_read_interval_sec ZTACX_VALUE_UINT16
 *	battery_divider_high ZTACX_VALUE_UINT16
 *	battery_divider_low ZTACX_VALUE_UINT16
 *	battery_millivolt_threshold ZTACX_VALUE_UINT16
 *
 * Values:
 *      ztacx_battery_ok  ZTACX_VALUE_BOOL
 *	ztacx_battery_level ZTACX_VALUE_BYTE
 *	ztacx_battery_millivolts ZTACX_VALUE_UINT16
 *	ztacx_battery_notify ZTACX_VALUE_BOOL
 */

extern int ztacx_battery_init(struct ztacx_leaf *leaf);
extern int ztacx_battery_start(struct ztacx_leaf *leaf);

ZTACX_CLASS_DEFINE(battery, ((struct ztacx_leaf_cb){.init=&ztacx_battery_init,.start=&ztacx_battery_start}));
ZTACX_LEAF_DEFINE(battery, battery, NULL);
