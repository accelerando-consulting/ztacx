/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define __main__
#include "ztacx.h"
#include "ztacx_kp.h"
#include <zephyr/bluetooth/hci.h>
#include <zephyr/usb/class/hid.h>

static struct ztacx_variable *led0_duty;
static struct ztacx_variable *kp0_pins;
static struct ztacx_variable *kp0_event;
static struct ztacx_variable *bt_peripheral_connected;

bool connected = false;
bool notify_subscribed = false;


struct ztacx_bt_peripheral_context *peripheral_context = NULL;

#define DEVICE_NAME     CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_GAP_APPEARANCE,
		      (CONFIG_BT_DEVICE_APPEARANCE >> 0) & 0xff,
		      (CONFIG_BT_DEVICE_APPEARANCE >> 8) & 0xff),
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_HIDS_VAL),
					  BT_UUID_16_ENCODE(BT_UUID_BAS_VAL)),
};

/*
static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};
*/

enum {
	HIDS_REMOTE_WAKE = BIT(0),
	HIDS_NORMALLY_CONNECTABLE = BIT(1),
};

struct hids_info {
	uint16_t version; /* version number of base USB HID Specification */
	uint8_t code; /* country HID Device hardware is localized for. */
	uint8_t flags;
} __packed;

struct hids_report {
	uint8_t id; /* report id */
	uint8_t type; /* report type */
} __packed;

static struct hids_info info = {
	.version = 0x0000,
	.code = 0x00,
	.flags = HIDS_NORMALLY_CONNECTABLE,
};

enum {
	HIDS_INPUT = 0x01,
	HIDS_OUTPUT = 0x02,
	HIDS_FEATURE = 0x03,
};

static struct hids_report input = {
	.id = 0x01,
	.type = HIDS_INPUT,
};

static uint8_t ctrl_point;

static const uint8_t report_map[] = HID_KEYBOARD_REPORT_DESC(); // see usb/class/hid.h

static ssize_t read_info(struct bt_conn *conn,
			  const struct bt_gatt_attr *attr, void *buf,
			  uint16_t len, uint16_t offset)
{
	LOG_HEXDUMP_INF(&info, sizeof(info),"read_info");
	return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data,
				 sizeof(struct hids_info));
}

static ssize_t read_report_map(struct bt_conn *conn,
			       const struct bt_gatt_attr *attr, void *buf,
			       uint16_t len, uint16_t offset)
{
	LOG_HEXDUMP_INF(&report_map, sizeof(report_map),"read_report_map");
	return bt_gatt_attr_read(conn, attr, buf, len, offset, report_map,
				 sizeof(report_map));
}

static ssize_t read_report(struct bt_conn *conn,
			   const struct bt_gatt_attr *attr, void *buf,
			   uint16_t len, uint16_t offset)
{
	LOG_HEXDUMP_INF(attr->user_data, sizeof(struct hids_report), "read_report");
	return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data,
				 sizeof(struct hids_report));
}

static void input_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	notify_subscribed = (value == BT_GATT_CCC_NOTIFY) ? 1 : 0;
	LOG_INF("input_ccc_changed notify_subscribed <= %s",notify_subscribed?"true":"false");
}

static ssize_t read_input_report(struct bt_conn *conn,
				 const struct bt_gatt_attr *attr, void *buf,
				 uint16_t len, uint16_t offset)
{
	LOG_INF("read_input_report");
	return bt_gatt_attr_read(conn, attr, buf, len, offset, NULL, 0);
}

static ssize_t write_ctrl_point(struct bt_conn *conn,
				const struct bt_gatt_attr *attr,
				const void *buf, uint16_t len, uint16_t offset,
				uint8_t flags)
{
	LOG_INF("write_ctrl_point");
	uint8_t *value = attr->user_data;

	if (offset + len > sizeof(ctrl_point)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	memcpy(value + offset, buf, len);

	return len;
}


/* HID Service Declaration */
BT_GATT_SERVICE_DEFINE(hog_svc,
        BT_GATT_PRIMARY_SERVICE(BT_UUID_HIDS), // 0
        BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_INFO, BT_GATT_CHRC_READ, // 1,2
			       BT_GATT_PERM_READ, read_info, NULL, &info),
	BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT_MAP, BT_GATT_CHRC_READ, // 3,4
			       BT_GATT_PERM_READ, read_report_map, NULL, NULL),
	BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, // 5,6
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ_ENCRYPT,
			       read_input_report, NULL, NULL),
	BT_GATT_CCC(input_ccc_changed, // 7
		    BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT
		),
	BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ, // 8
			   read_report, NULL, &input),
	BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_CTRL_POINT, // 9,10
			       BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       BT_GATT_PERM_WRITE,
			       NULL, write_ctrl_point, &ctrl_point),
);


struct key_config 
{
	enum hid_kbd_code code;
	const char *desc;
};

struct key_config kp_map[] = {
	{HID_KEY_S, "down"},
	{HID_KEY_P, "enter"},
	{HID_KEY_D, "right"},
	{HID_KEY_A, "left"},
	{HID_KEY_W, "up"},
	{HID_KEY_ESC, "escape"},
	{HID_KEY_BACKSPACE, "backspace"},
	{HID_KEY_SPACE, "space"}
};
	
/*
enum hid_kbd_code kp_map[] = {
	HID_KEY_S, //DOWN,
	HID_KEY_P, //ENTER,
	HID_KEY_E, //RIGHT,
	HID_KEY_W, //LEFT,
	HID_KEY_N, //UP,
	HID_KEY_ESC,
	HID_KEY_BACKSPACE,
	HID_KEY_SPACE
};
*/

	

static int app_init(void) 
{
	printk("bt_keypad sample app_init\n");
	LOG_INF("NOTICE bt_keypad sample Initialising app variables");

	ZTACX_SETTING_FIND(led0_duty);
	ZTACX_VAR_FIND(kp0_pins);
	ZTACX_VAR_FIND(kp0_event);
	ZTACX_VAR_FIND(bt_peripheral_connected);
	
	return 0;
}

static struct k_work connect_change_work;

void connect_change(struct k_work *work) 
{
	connected = ztacx_variable_value_get_bool(bt_peripheral_connected);
	
	LOG_INF("Bluetooth connection %s", connected?"established":"closed");

	ztacx_variable_value_set_byte(led0_duty, connected?1:50);;

	if (!peripheral_context) {
		LOG_ERR("No peripheral context");
		return;
	}
}

static int app_start(void) 
{
	printk("bt_keypad sample app_start\n");

	struct ztacx_leaf *leaf = ztacx_leaf_get("bt_peripheral");
	if (leaf && leaf->context) {
		peripheral_context = leaf->context;
	}

	LOG_INF("Registering connect hook");
	k_work_init(&connect_change_work, connect_change);
	ztacx_variable_ptr_set_onchange(bt_peripheral_connected, &connect_change_work);

	LOG_INF("Registering advertising data");
//	if (ztacx_bt_adv_register(ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd)) != 0) {
	if (ztacx_bt_adv_register(ad, ARRAY_SIZE(ad), NULL, 0) != 0) {
		LOG_ERR("Bluetooth advertising register failed");
	}

	
	return 0;
}


SYS_INIT(app_init, APPLICATION, ZTACX_APP_INIT_PRIORITY);
SYS_INIT(app_start, APPLICATION, ZTACX_APP_START_PRIORITY);


int main(void)
{
	printk("Ztacx Bluetooth keypad sample\n");

	while (1) {
		uint32_t event = ztacx_variable_value_wait_event(kp0_event, 0xFFFFFFFF, K_FOREVER);
		if (!event) continue;
		if (!(event & (KP_EVENT_PRESS|KP_EVENT_RELEASE))) {
			LOG_WRN("Event mask %08X contains neither a press nor release event", event);
			continue;
		}
		if (!(event & 0xFF)) {
			LOG_WRN("Event mask %08X contains no key identifier", event);
			continue;
		}
		enum hid_kbd_code key_code=0;
		
		for (int i=0; i<8;i++) {
			if (event & (1<<i)) {
				key_code = kp_map[i].code;
				const char *desc = kp_map[i].desc;
				bool is_press = (event & KP_EVENT_PRESS);
				LOG_INF("Key %s event for key %d => code %d (%s)",
					is_press?"PRESS":"RELEASE",
					i, (int)key_code, desc);
			}
		}

		uint8_t  report[8]={0,0,0,0,0,0,0,0};
		int p = 0;
		report[p++] = 0; // control key bits 
		report[p++] = 0; // reserved
		report[p++] = (event & KP_EVENT_PRESS)?key_code:0; // keypress 1
		            // can report up to 6 keys pressed at once but meh
		LOG_HEXDUMP_INF(report, sizeof(report), "keyboard report");

		if (!ztacx_variable_value_get_bool(bt_peripheral_connected)) {
			// not connected
			LOG_WRN("No BT connection");
			continue;
		}
		if (!notify_subscribed) {
			LOG_INF("Device connected but not subscribed to notifications");
			continue;
		}
		//LOG_INF("Sending report");
		bt_gatt_notify(NULL, &hog_svc.attrs[5], report, sizeof(report));
	}
}

