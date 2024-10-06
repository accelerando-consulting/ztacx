#pragma once

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include "ztacx_bt_common.h"

extern int ztacx_bt_central_init(struct ztacx_leaf *leaf);
extern int ztacx_bt_central_start(struct ztacx_leaf *leaf);

#define PERIPHERAL_CHARACTERISTIC_COUNT 4

struct ztacx_bt_central_context 
{
//	struct btl_addr_le_t peripheral_addr;
	struct bt_conn *conn;
	uint16_t conn_handle;
	struct bt_gatt_discover_params discover_params;
	struct bt_gatt_discover_params discover_characteristic_params;
	struct bt_gatt_discover_params discover_descriptor_params;
	uint16_t characteristic_handle[PERIPHERAL_CHARACTERISTIC_COUNT];
	uint16_t read_handles[PERIPHERAL_CHARACTERISTIC_COUNT];
	struct bt_gatt_subscribe_params subscribe_params[PERIPHERAL_CHARACTERISTIC_COUNT];
	const struct bt_uuid *discover_service_uuid;
	struct bt_gatt_read_params read_params;
	int8_t rssi;
};

extern struct ztacx_bt_central_context ztacx_bt_central_context;

ZTACX_CLASS_DEFINE(bt_central, ((struct ztacx_leaf_cb){.init=&ztacx_bt_central_init,.start=&ztacx_bt_central_start}));
ZTACX_LEAF_DEFINE(bt_central, bt_central, &ztacx_bt_central_context);

#if CONFIG_SHELL
extern int cmd_ztacx_bt_central(const struct shell *shell, size_t argc, char **argv);
#endif
