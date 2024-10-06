#pragma once

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

struct ztacx_bt_characteristic
{
	struct ztacx_variable **variable;
	const struct bt_uuid *uuid;
	uint8_t props;
	uint8_t perm;
	const char *desc;
	const struct bt_gatt_cpf *cpf;
	const struct bt_gatt_ccc *ccc;
};
