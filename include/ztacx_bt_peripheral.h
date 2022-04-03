#pragma once

//
// The user of this library should define
//
// enum peripheral_characteristic
// enum post_error
// enum post_device

#define STRING_CHAR_MAX 20
#define INVALID_UNSIGNED 65535
#define INVALID_SIGNED -32768
#define INVALID_NIBBLE 15

enum peripheral_connection_state {
	PERIPHERAL_STATE_DISCONNECTED = 0,
	PERIPHERAL_STATE_SCANNING,
	PERIPHERAL_STATE_CONNECTING,
	PERIPHERAL_STATE_CONNECTED,
	PERIPHERAL_STATE_DISCONNECTING
};

extern int ztacx_bt_peripheral_setup(struct ztacx_leaf *leaf);
extern int ztacx_bt_peripheral_start(struct ztacx_leaf *leaf);

ZTACX_CLASS_DEFINE(bt_peripheral, ((struct ztacx_leaf_cb){.init=&ztacx_bt_peripheral_init,.start=&ztacx_bt_peripheral_start}));
ZTACX_LEAF_DEFINE(bt_peripheral, bt_peripheral, NULL);

#endif
