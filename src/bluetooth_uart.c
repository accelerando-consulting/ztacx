#include <zephyr/types.h>

#include <device.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/printk.h>
#include <sys/util.h>
#include <sys/types.h>
#include <sys/byteorder.h>
#include <sys/ring_buffer.h>
#include <drivers/uart.h>
#include <logging/log_backend.h>
#include <logging/log_backend_std.h>
#include <logging/log_ctrl.h>
#include <sys/mutex.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/uuid.h>

#include "config.h"
#include "globals.h"
#include "modules.h"
#include "bluetooth.h"

#ifndef BT_UART_BUFSZ
#define BT_UART_BUFSZ 256
#endif

static const struct device *uart_dev;
const struct bt_gatt_attr *bt_uart_tx_attr = NULL;

// #define MIN(a,b) ((a<=b)?a:b)

static ssize_t bt_uart_char_write(
	struct bt_conn *conn,
	const struct bt_gatt_attr *attr,
	const void *buf,
	uint16_t len,
	uint16_t offset,
	uint8_t flags)
{
	LOG_DBG("uart_write buf=%p len=%d offset=%d", buf, (int)len, (int)offset);
	if (offset != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	for (int i=0; i<len; i++) {
		char c = ((char *)buf)[i];
		/*
		if (c < ' ') {
			printk("MDM> 0x%02x\n", (int)c);
		}
		else {
			printk("MDM> %c\n", c);
		}
		*/
		uart_poll_out(uart_dev, c);
	}
	return len;
}

static void bt_uart_ccc_change(
	const struct bt_gatt_attr *attr,
	uint16_t value) {
	notify_uart = (value == BT_GATT_CCC_NOTIFY);
	LOG_INF("notify_uart: %s", notify_uart?"ON":"OFF");
}

static struct bt_uuid_128 uart_service_uuid = BT_UUID_INIT_128(
	0x9E,0xCA,0xDC,0x24,0x0E,0xE5,0xA9,0xE0,0x93,0xF3,0xA3,0xB5,0x01,0x00,0x40,0x6E,
);

static const struct bt_uuid_128 char_uart_rx_uuid = BT_UUID_INIT_128(
	0x9E,0xCA,0xDC,0x24,0x0E,0xE5,0xA9,0xE0,0x93,0xF3,0xA3,0xB5,0x02,0x00,0x40,0x6E,
);

const struct bt_uuid_128 char_uart_tx_uuid = BT_UUID_INIT_128(
	0x9E,0xCA,0xDC,0x24,0x0E,0xE5,0xA9,0xE0,0x93,0xF3,0xA3,0xB5,0x03,0x00,0x40,0x6E,
);


BT_GATT_SERVICE_DEFINE(
	uart_svc,
	BT_GATT_PRIMARY_SERVICE(&uart_service_uuid), // 0
	BT_GATT_CHARACTERISTIC(&char_uart_rx_uuid.uuid, // 1,2
			       BT_GATT_CHRC_WRITE|BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       BT_GATT_PERM_READ|BT_GATT_PERM_WRITE,
			       NULL, bt_uart_char_write, NULL),
	BT_GATT_CUD("UART RX", BT_GATT_PERM_READ), //3

	BT_GATT_CHARACTERISTIC(&char_uart_tx_uuid.uuid, // 4,5
			       BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ|BT_GATT_PERM_WRITE,
			       NULL, NULL, NULL),
	BT_GATT_CCC(bt_uart_ccc_change, BT_GATT_PERM_READ|BT_GATT_PERM_WRITE), //6
	BT_GATT_CUD("UART TX", BT_GATT_PERM_READ) // 7
);


int bluetooth_uart_setup(const char *device)
{
	uart_dev = device_get_binding(device);

	if (uart_dev == NULL) {
		LOG_ERR("UART device %s not available", device);
		return -ENODEV;
	}

	bt_uart_tx_attr = &uart_svc.attrs[4];

	return 0;
}

SYS_MUTEX_DEFINE(btuart_log_mutex);

static int btuart_char_out(uint8_t *data, size_t length, void *ctx)
{
	ARG_UNUSED(ctx);

	if (notify_uart) {
		if (length <= 4) {
			char buf[10];
			memcpy(buf, data, length);
			buf[length]='\0';
			printk("WTF is dis: %d:[%s]", length, buf);
			return length;
		}
		if (sys_mutex_lock(&btuart_log_mutex, K_NO_WAIT) != 0) {
			// Do not reentrantly log (eg if bluetooth tries to
			// log, don't!)
			return length;
		}

		int ofs = 0;
		while (ofs < length) {
			int sz = MIN(length,20);
			bt_gatt_notify(NULL, bt_uart_tx_attr, data+ofs, sz);
			ofs += sz;
		}
		sys_mutex_unlock(&btuart_log_mutex);
	}

	return length;
}
static uint8_t btuart_output_buf[BT_UART_BUFSZ];


LOG_OUTPUT_DEFINE(log_output_btuart, btuart_char_out, btuart_output_buf, sizeof(btuart_output_buf));


void btuart_log_put(const struct log_backend *const backend,
		    struct log_msg *msg)
{
	log_backend_std_put(&log_output_btuart, 0, msg);
}

void btuart_log_put_sync_string(const struct log_backend *const backend,
				struct log_msg_ids src_level, uint32_t timestamp,
				const char *fmt, va_list ap)
{
	log_backend_std_sync_string(&log_output_btuart, 0, src_level,
				    timestamp, fmt, ap);
}

void btuart_log_put_sync_hexdump(const struct log_backend *const backend,
			     struct log_msg_ids src_level, uint32_t timestamp,
			     const char *metadata, const uint8_t *data, uint32_t len)
{
	log_backend_std_sync_hexdump(&log_output_btuart, 0, src_level,
				     timestamp, metadata, data, len);
}


void btuart_log_dropped(const struct log_backend *const backend, uint32_t cnt)
{
	//log_backend_std_dropped(&log_output_btuart, cnt);
}

void btuart_log_panic(const struct log_backend *const backend)
{
	log_backend_std_panic(&log_output_btuart);
}

static struct log_backend const bt_uart_log_backend;
void btuart_log_init(const struct log_backend *backend)
{
	LOG_DBG("");
	int source_count = log_src_cnt_get(0);
	for (int i=0; i<source_count; i++) {
		const char *name = log_source_name_get(0, i);
		//printk("Log source %d: %s", i, name);
		if (strncmp(name, "bt_", 3)==0){
			//printk(" DISABLED");
			log_filter_set(&bt_uart_log_backend, 0, i, LOG_LEVEL_NONE);
		}
		//printk("\n");
	}
}

static const struct log_backend_api btlog_api = {
	.put=btuart_log_put,
	.put_sync_string=btuart_log_put_sync_string,
	.put_sync_hexdump=btuart_log_put_sync_hexdump,
	.dropped=btuart_log_dropped,
	.panic=btuart_log_panic,
	.init=btuart_log_init
};
LOG_BACKEND_DEFINE(bt_uart, btlog_api, true);


