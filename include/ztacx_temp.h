#pragma once
#if CONFIG_ZTACX_LEAF_TEMP
/*
 * Settings:
 *	temp_read_interval_sec ZTACX_VALUE_UINT16
 *	temp_centidegree_threshold ZTACX_VALUE_UINT16
 *
 * Values:
 *      ztacx_temp_ok  ZTACX_VALUE_BOOL
 *	ztacx_temp_centidegree ZTACX_VALUE_INT16
 *	ztacx_temp_notify ZTACX_VALUE_BOOL
 */

extern int ztacx_temp_init(struct ztacx_leaf *leaf);
extern int ztacx_temp_start(struct ztacx_leaf *leaf);

ZTACX_CLASS_DEFINE(temp, ((struct ztacx_leaf_cb){.init=&ztacx_temp_init,.start=&ztacx_temp_start}));
ZTACX_LEAF_DEFINE(temp, temp, NULL);
#endif
