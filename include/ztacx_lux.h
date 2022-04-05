#pragma once

/*
 * Settings:
 *	lux_read_interval_sec ZTACX_VALUE_UINT16
 *
 * Values:
 *      ztacx_lux_ok  ZTACX_VALUE_BOOL
 *	ztacx_lux_notify ZTACX_VALUE_BOOL
 *	ztacx_lux_level ZTACX_VALUE_UINT16
 */

extern int ztacx_lux_init(struct ztacx_leaf *leaf);
extern int ztacx_lux_start(struct ztacx_leaf *leaf);

ZTACX_CLASS_DEFINE(lux, ((struct ztacx_leaf_cb){.init=&ztacx_lux_init,.start=&ztacx_lux_start}));
ZTACX_LEAF_DEFINE(lux, lux, NULL);

