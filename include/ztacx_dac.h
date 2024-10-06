#pragma once

/*
 * Settings:
 *      ztacx_dac_bits  ZTACX_VALUE_INT32
 *
 *
 *
 * Values:
 *      ztacx_dac_ok  ZTACX_VALUE_BOOL
 *	ztacx_dac_level ZTACX_VALUE_INT32
 */

extern int ztacx_dac_init(struct ztacx_leaf *leaf);
extern int ztacx_dac_start(struct ztacx_leaf *leaf);

ZTACX_CLASS_DEFINE(dac, ((struct ztacx_leaf_cb){.init=&ztacx_dac_init,.start=&ztacx_dac_start}));
ZTACX_LEAF_DEFINE(dac, dac, NULL);
