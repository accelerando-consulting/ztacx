#pragma once

/*
 * Settings:
 *	lidar_read_interval_sec ZTACX_VALUE_UINT16
 *	lidar_distance_threshold_mm ZTACX_VALUE_UINT16
 *	lidar_distance_delta_mm ZTACX_VALUE_UINT16
 *
 * Values:
 *      ztacx_lidar_ok  ZTACX_VALUE_BOOL
 *	ztacx_lidar_notify ZTACX_VALUE_BOOL
 *	ztacx_lidar_detect ZTACX_VALUE_BOOL
 *	ztacx_lidar_distance_mm ZTACX_VALUE_UINT16
 */

extern int ztacx_lidar_init(struct ztacx_leaf *leaf);
extern int ztacx_lidar_start(struct ztacx_leaf *leaf);

ZTACX_CLASS_DEFINE(lidar, ((struct ztacx_leaf_cb){.init=&ztacx_lidar_init,.start=&ztacx_lidar_start}));
ZTACX_LEAF_DEFINE(lidar, lidar, NULL);
