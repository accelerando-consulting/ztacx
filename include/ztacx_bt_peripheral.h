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



extern int peripheral_setup();
extern int central_setup();
extern int central_loop();
extern int cmd_test_peripheral(const struct shell *shell, size_t argc, char **argv);
extern int cmd_test_central(const struct shell *shell, size_t argc, char **argv);
extern int bluetooth_uart_setup(const char *device);

#endif
