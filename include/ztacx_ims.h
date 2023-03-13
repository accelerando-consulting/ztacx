#pragma once

/*
 * Settings:
 *	ims_read_interval_usec ZTACX_VALUE_INT32
 *	ims_change_threshold_mg ZTACX_VALUE_INT32
 *
 * Values:
 *      ztacx_ims_ok  ZTACX_VALUE_BOOL
 *	ztacx_ims_notify ZTACX_VALUE_BOOL
 *	ztacx_ims_level_x ZTACX_VALUE_INT32  // x acceleration in cm/s/s
 *	ztacx_ims_level_y ZTACX_VALUE_INT32
 *	ztacx_ims_level_z ZTACX_VALUE_INT32
 *	ztacx_ims_level_m ZTACX_VALUE_INT32  // magnitude of xyz vector in cm/s/s
 *	ztacx_ims_peak_x ZTACX_VALUE_INT32
 *	ztacx_ims_peak_y ZTACX_VALUE_INT32
 *	ztacx_ims_peak_z ZTACX_VALUE_INT32
 *	ztacx_ims_peak_m ZTACX_VALUE_INT32
 */

extern int ztacx_ims_init(struct ztacx_leaf *leaf);
extern int ztacx_ims_start(struct ztacx_leaf *leaf);

ZTACX_CLASS_DEFINE(ims, ((struct ztacx_leaf_cb){.init=&ztacx_ims_init,.start=&ztacx_ims_start}));
ZTACX_LEAF_DEFINE(ims, ims, NULL);

