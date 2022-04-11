/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define __main__
#include "ztacx.h"
#include "ztacx_led.h"

static struct ztacx_variable *led_duty;
static struct ztacx_variable *led_cycle;
static struct ztacx_variable *led_blink;
static struct ztacx_variable *led_lit;

#define SERVICE_ID 0xed,0x34,0xe5,0xd4,0xb6,0xbf,0x11,0xec,0xb8,0x19,0xd7,0x85,0xeb,0xdd,0x2f,0x40
static struct bt_uuid_128 service_uuid = BT_UUID_INIT_128(SERVICE_ID);

static const struct bt_data bt_adv_data[]={
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, SERVICE_ID)
};

/*
 * UUIDs generated using uuid | perl -p -e 's/-//g; s/([0-9a-f]{2})/0x$1,/g;' -e 's/,$//'
 */

static const struct bt_uuid_128 char_led_duty_uuid = BT_UUID_INIT_128(0xd0,0xb3,0x66,0x1c,0xb6,0xc7,0x11,0xec,0x9c,0x80,0x3b,0x60,0x4e,0x8f,0x9c,0x6f);
static const struct bt_uuid_128 char_led_cycle_uuid = BT_UUID_INIT_128(0xd1,0xb2,0x58,0xa2,0xb6,0xc7,0x11,0xec,0xa0,0x7b,0xf7,0x62,0xa2,0x5a,0x3c,0x35);
static const struct bt_uuid_128 char_led_blink_uuid = BT_UUID_INIT_128(0xd2,0x29,0xcb,0x58,0xb6,0xc7,0x11,0xec,0xac,0xef,0xab,0x9a,0x7f,0xe0,0x4a,0x2f);
static const struct bt_uuid_128 char_led_lit_uuid = BT_UUID_INIT_128(0xd2,0xaa,0x3e,0x6e,0xb6,0xc7,0x11,0xec,0x92,0xaa,0xf3,0x2d,0xb0,0x38,0xd6,0x70);

static const struct bt_gatt_cpf bt_gatt_cpf_millisecond = {
	.format = 6,
	.exponent = -3,
	.unit = 0x2703,
};

BT_GATT_SERVICE_DEFINE(
	svc,
	BT_GATT_PRIMARY_SERVICE(&service_uuid),
	ZTACX_BT_SETTING(led_duty,  uint8,       "Duty cycle (percent)"),
	ZTACX_BT_SETTING(led_cycle, millisecond, "Length of blink cycle in milliseconds"),
	ZTACX_BT_SETTING(led_blink, boolean,     "True if LED is is in fast-blink identify mode"),
	ZTACX_BT_SENSOR(led_lit,    boolean,     "True if LED is lit"),
	);

static int app_init(const struct device *unused) 
{
	LOG_INF("NOTICE Initialising app variables");
	ZTACX_SETTING_FIND_AS(led_duty, led0_duty);
	ZTACX_SETTING_FIND_AS(led_cycle, led0_cycle);
	ZTACX_VAR_FIND_AS(led_blink, led0_blink);
	ZTACX_VAR_FIND_AS(led_lit, led0_lit);

	if (ztacx_bt_adv_register(bt_adv_data, ARRAY_SIZE(bt_adv_data), NULL, 0) != 0) {
		LOG_ERR("Bluetooth advertising register failed");
	}
	return 0;
}
SYS_INIT(app_init, APPLICATION, ZTACX_PRIORITY_APP_INIT);

void main(void)
{

	printk("Ztacx Bluetooth peripheral sample\n");
	printk("Use nrfConnect mobile app to browse your device\n");
}
