/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define __main__
#include "ztacx.h"

static struct ztacx_variable *led0_duty;

static struct ztacx_variable *ims_read_interval_usec;
static struct ztacx_variable *ims_change_threshold;

static struct ztacx_variable *ims_level_x;
static struct ztacx_variable *ims_level_y;
static struct ztacx_variable *ims_level_z;
static struct ztacx_variable *ims_level_m;
static struct ztacx_variable *ims_peak_x;
static struct ztacx_variable *ims_peak_y;
static struct ztacx_variable *ims_peak_z;
static struct ztacx_variable *ims_peak_m;
static struct ztacx_variable *ims_samples;
static struct ztacx_variable *shock_threshold;
static struct ztacx_variable *shock_alert;

static struct ztacx_variable app_values[] = {
	{"shock_threshold",ZTACX_VALUE_INT32,{.val_int32=CONFIG_APP_SHOCK_THRESHOLD}},
	{"shock_alert",ZTACX_VALUE_BOOL,{.val_bool=false}}
};

static struct ztacx_variable *ims_samples;


#if CONFIG_ZTACX_LEAF_BT_PERIPHERAL

#define SERVICE_ID 0xe3,0x97,0x25,0x12,0xb8,0xd2,0x11,0xed,0x98,0x62,0x13,0x4a,0x95,0x00,0x26,0x8e
static struct bt_uuid_128 service_uuid = BT_UUID_INIT_128(SERVICE_ID);

static const struct bt_data bt_adv_data[]={
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, SERVICE_ID)
};

/*
 * UUIDs generated using uuid | perl -p -e 's/-//g; s/([0-9a-f]{2})/0x$1,/g;' -e 's/,$//'
 */

static const struct bt_uuid_128 char_ims_read_interval_usec_uuid = BT_UUID_INIT_128(0x10,0xbe,0x21,0x62,0xb8,0xd8,0x11,0xed,0x8c,0x20,0x2b,0x70,0x66,0xff,0x29,0xf3);
static const struct bt_uuid_128 char_ims_change_threshold_uuid  = BT_UUID_INIT_128(0x17,0x37,0xf5,0x7c,0xb8,0xd8,0x11,0xed,0x9f,0x51,0xb3,0x9b,0x52,0x86,0x56,0xf9);
static const struct bt_uuid_128 char_ims_level_x_uuid       = BT_UUID_INIT_128(0xfe,0x25,0x34,0x90,0xb8,0xd3,0x11,0xed,0xb0,0x56,0xf3,0x40,0xb7,0xe5,0x58,0xa8);
static const struct bt_uuid_128 char_ims_level_y_uuid       = BT_UUID_INIT_128(0x04,0xf1,0x81,0x3e,0xb8,0xd4,0x11,0xed,0xab,0x57,0xeb,0x87,0xa6,0x89,0x83,0xe0);
static const struct bt_uuid_128 char_ims_level_z_uuid       = BT_UUID_INIT_128(0x09,0x2d,0xdd,0x1a,0xb8,0xd4,0x11,0xed,0xad,0xee,0x7b,0xb9,0xea,0xee,0x3f,0xe0);
static const struct bt_uuid_128 char_ims_level_m_uuid       = BT_UUID_INIT_128(0xf3,0xbb,0x21,0xa4,0xb8,0xd3,0x11,0xed,0x9f,0x78,0x23,0x92,0x20,0x91,0x08,0x2c);
static const struct bt_uuid_128 char_ims_peak_x_uuid  = BT_UUID_INIT_128(0x11,0xda,0x19,0xe2,0xb8,0xd4,0x11,0xed,0xac,0xf0,0xf7,0x04,0x2a,0xa0,0x8e,0xf1);
static const struct bt_uuid_128 char_ims_peak_y_uuid  = BT_UUID_INIT_128(0x16,0x51,0xbe,0x80,0xb8,0xd4,0x11,0xed,0x87,0x3b,0x2f,0x2a,0x93,0xa6,0xd3,0x0a);
static const struct bt_uuid_128 char_ims_peak_z_uuid  = BT_UUID_INIT_128(0x1a,0x36,0xb1,0x9a,0xb8,0xd4,0x11,0xed,0xac,0xec,0xdf,0x2d,0x84,0x9f,0x89,0xaa);
static const struct bt_uuid_128 char_ims_peak_m_uuid  = BT_UUID_INIT_128(0x0c,0xfe,0x6d,0x60,0xb8,0xd4,0x11,0xed,0xbf,0x91,0x6f,0x68,0x58,0xf2,0x6d,0xff);
static const struct bt_uuid_128 char_ims_samples_uuid  = BT_UUID_INIT_128(0xa1,0x4d,0xed,0x9e,0xb9,0x90,0x11,0xed,0x8c,0x13,0xaf,0xf9,0x9c,0x0e,0x9b,0x5d);
static const struct bt_uuid_128 char_shock_threshold_uuid  = BT_UUID_INIT_128(0xf9,0x8f,0xbc,0x66,0xb9,0x96,0x11,0xed,0x9d,0x61,0x3b,0xdb,0x62,0x21,0x16,0x96);
static const struct bt_uuid_128 char_shock_alert_uuid  = BT_UUID_INIT_128(0x19,0x83,0x28,0xaa,0xb9,0x97,0x11,0xed,0x83,0xea,0x03,0x09,0xd9,0x95,0xf0,0xc3);


static const struct bt_gatt_cpf bt_gatt_cpf_cmpsps = {
	.format = 16,   // int32
	.exponent = -2, // scale factor 1/100
	.unit = 0x2713, // unit m/s/s
};

static const struct bt_gatt_cpf bt_gatt_cpf_usec = {
	.format = 16,   // int32
	.exponent = -6, // scale factor 1/1000000
	.unit = 0x2703, // unit second
};

BT_GATT_SERVICE_DEFINE(
	svc,
	BT_GATT_PRIMARY_SERVICE(&service_uuid),
	ZTACX_BT_SETTING(ims_read_interval_usec, usec,   "Read interval in microseconds"),
	ZTACX_BT_SETTING(ims_change_threshold,   cmpsps, "Change threshold in cm/s/s)"),
	ZTACX_BT_SENSOR(ims_level_x,             cmpsps, "X-axis acceleration in cm/s/s)"),
	ZTACX_BT_SENSOR(ims_level_y,             cmpsps, "Y-axis acceleration in cm/s/s)"),
	ZTACX_BT_SENSOR(ims_level_z,             cmpsps, "Z-axis acceleration in cm/s/s)"),
	ZTACX_BT_SENSOR(ims_level_m,             cmpsps, "Polar magnitude of acceleration in cm/s/s)"),
	ZTACX_BT_SENSOR(ims_peak_x,              cmpsps, "Peak X-axis acceleration in cm/s/s)"),
	ZTACX_BT_SENSOR(ims_peak_y,              cmpsps, "Peak Y-axis acceleration in cm/s/s)"),
	ZTACX_BT_SENSOR(ims_peak_z,              cmpsps, "Peak Z-axis acceleration in cm/s/s)"),
	ZTACX_BT_SENSOR(ims_peak_m,              cmpsps, "Peak magnitude of acceleration in cm/s/s)"),
	ZTACX_BT_SENSOR(ims_samples,             int64,  "Number of samples taken"),
	ZTACX_BT_SETTING(shock_threshold,        int32,  "Alert threshold for shock"),
	ZTACX_BT_SENSOR(shock_alert,           boolean,  "Status of alert"),
	);
#endif

static int app_init(const struct device *unused) 
{
	printk("bt_sensor sample app_init\n");
	LOG_INF("NOTICE bt_sensor sample Initialising app variables");

	ztacx_variables_register(app_values, ARRAY_SIZE(app_values));
	
	ZTACX_SETTING_FIND(ims_read_interval_usec);
	ZTACX_SETTING_FIND(ims_change_threshold);
	ZTACX_SETTING_FIND(led0_duty);
	ZTACX_VAR_FIND(ims_level_x);
	ZTACX_VAR_FIND(ims_level_y);
	ZTACX_VAR_FIND(ims_level_z);
	ZTACX_VAR_FIND(ims_level_m);
	ZTACX_VAR_FIND(ims_peak_x);
	ZTACX_VAR_FIND(ims_peak_y);
	ZTACX_VAR_FIND(ims_peak_z);
	ZTACX_VAR_FIND(ims_peak_m);
	ZTACX_VAR_FIND(ims_samples);
	ZTACX_VAR_FIND(shock_threshold);
	ZTACX_VAR_FIND(shock_alert);
	
	return 0;
}

static struct k_work peak_change_work;
static struct k_work alert_clear_work;

void peak_change(struct k_work *work) 
{
	int64_t count = ztacx_variable_value_get_int64(ims_samples);
	int32_t peak = ztacx_variable_value_get_int32(ims_peak_m);
	int32_t threshold = ztacx_variable_value_get_int32(shock_threshold);

	LOG_INF("Peak change %d (sample count=%lld)", (int)peak, (long long)count);

	bool alert = ztacx_variable_value_get_bool(shock_alert);
	if (!alert && (peak > threshold)) {
		LOG_WRN("Shock alert triggered (peak %d > threshold %d)", (int)peak, (int)threshold);
		ztacx_variable_value_set_bool(shock_alert, true);
		//bt_gatt_notify(NULL, ims_value_attr, &mag_mg, sizeof(mag_mg));
		ztacx_variable_value_set_byte(led0_duty, 50);
	}
}

void alert_clear(struct k_work *work) 
{
	if (ztacx_variable_value_get_bool(shock_alert)) {
		LOG_INF("Shock alert was triggered");
		return;
	}

	LOG_INF("Clear shock alert state");
	ztacx_variable_value_set_byte(led0_duty, 1);
	ztacx_variable_value_set_int32(ims_peak_m, INT32_MAX);
}

static int app_start(const struct device *unused) 
{
	printk("bt_sensor sample app_start\n");
#if CONFIG_ZTACX_LEAF_BT_PERIPHERAL
	LOG_INF("Registering advertising data");
	if (ztacx_bt_adv_register(bt_adv_data, ARRAY_SIZE(bt_adv_data), NULL, 0) != 0) {
		LOG_ERR("Bluetooth advertising register failed");
	}
#else
	LOG_ERR("BT peripheral leaf is disabled");
#endif

	k_work_init(&peak_change_work, peak_change);
	ztacx_variable_ptr_set_onchange(ims_peak_m, &peak_change_work);
	k_work_init(&alert_clear_work, alert_clear);
	ztacx_variable_ptr_set_onchange(shock_alert, &alert_clear_work);
	
	return 0;
}


SYS_INIT(app_init, APPLICATION, ZTACX_APP_INIT_PRIORITY);
SYS_INIT(app_start, APPLICATION, ZTACX_APP_START_PRIORITY);


void main(void)
{
	printk("Ztacx Bluetooth sensor sample\n");
	printk("Use nrfConnect mobile app to browse your device\n");
}
