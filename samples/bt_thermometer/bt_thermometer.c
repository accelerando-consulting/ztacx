/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define __main__
#include "ztacx.h"

static struct ztacx_variable *battery_level_percent;
static struct ztacx_variable *temp_read_interval_msec;
static struct ztacx_variable *temp_centidegree;

#if CONFIG_ZTACX_LEAF_BT_PERIPHERAL

static const struct bt_data bt_adv_data[]={
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL,
		      BT_UUID_16_ENCODE(BT_UUID_HTS_VAL),
		      BT_UUID_16_ENCODE(BT_UUID_DIS_VAL),
		      BT_UUID_16_ENCODE(BT_UUID_BAS_VAL)),
};

/*
 * Standard 16 bit UUIDs see include/zephyr/bluetooth/uuid.h
 * Custom 128-bit UUIDs can be generated using uuid | perl -p -e 's/-//g; s/([0-9a-f]{2})/0x$1,/g;' -e 's/,$//'
 */
static const struct bt_uuid_16 char_battery_level_percent_uuid = BT_UUID_INIT_16(BT_UUID_BAS_BATTERY_LEVEL_VAL);
static const struct bt_uuid_16 char_temp_read_interval_msec_uuid = BT_UUID_INIT_16(BT_UUID_HTS_INTERVAL_VAL);
static const struct bt_uuid_16 char_temp_centidegree_uuid = BT_UUID_INIT_16(BT_UUID_HTS_TEMP_C_VAL);

#if 0
static const struct bt_gatt_cpf bt_gatt_cpf_mv = {
	.format = 16,   // int32
	.exponent = -3, // scale factor 1/1000
	.unit = 0x2728, // unit = Volt
};
#endif

static const struct bt_gatt_cpf bt_gatt_cpf_cC = {
	.format = 16,   // int32
	.exponent = -2, // scale factor 1/100
	.unit = 0x272F, // unit = degree Celsius
};

static const struct bt_gatt_cpf bt_gatt_cpf_percent = {
	.format = 4,   // uint8
	.exponent = 0, // scale factor 1
	.unit = 0x27AD, // unit = percent
};

static const struct bt_gatt_cpf bt_gatt_cpf_msec = {
	.format = 16,   // int32
	.exponent = -3, // scale factor 1/1000
	.unit = 0x2703, // unit = second
};

BT_GATT_SERVICE_DEFINE(
	bas_svc,
	BT_GATT_PRIMARY_SERVICE(BT_UUID_BAS),
	ZTACX_BT_SENSOR( battery_level_percent,   percent,   "Battery level in percent"),
	);

BT_GATT_SERVICE_DEFINE(
	hts_svc,
	BT_GATT_PRIMARY_SERVICE(BT_UUID_HTS),
	ZTACX_BT_SETTING(temp_read_interval_msec, msec, "Temperature read interval (ms)"),
	ZTACX_BT_SENSOR( temp_centidegree,        cC,   "Temperature reading in centidegree Celsius"),
	);
#endif

static int app_init(void) 
{
	printk("bt_sensor sample app_init\n");
	LOG_INF("NOTICE bt_sensor sample Initialising app variables");

	ZTACX_VAR_FIND(battery_level_percent);
	ZTACX_VAR_FIND(temp_centidegree);

	ZTACX_SETTING_FIND(temp_read_interval_msec);
	
	return 0;
}

static int app_start(void) 
{
	printk("bt_thermometer sample app_start\n");
#if CONFIG_ZTACX_LEAF_BT_PERIPHERAL
	LOG_INF("Registering advertising data");
	if (ztacx_bt_adv_register(bt_adv_data, ARRAY_SIZE(bt_adv_data), NULL, 0) != 0) {
		LOG_ERR("Bluetooth advertising register failed");
	}
#else
	LOG_ERR("BT peripheral leaf is disabled");
#endif

	return 0;
}

SYS_INIT(app_init, APPLICATION, ZTACX_APP_INIT_PRIORITY);
SYS_INIT(app_start, APPLICATION, ZTACX_APP_START_PRIORITY);


void main(void)
{
	printk("Ztacx Bluetooth thermometer sample\n");
	printk("Use nrfConnect mobile app to browse your device\n");
}
