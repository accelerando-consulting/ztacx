#include "ztacx.h"
#include <zephyr/types.h>
#include <device.h>
#include <sys/printk.h>
#include <sys/util.h>
#include <sys/byteorder.h>
#include <sys/reboot.h>
#include <settings/settings.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/gatt.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_vs.h>
#include <bluetooth/uuid.h>

#include "ztacx_bluetooth.h"

#if CONFIG_MCUMGR_SMP_BT
#include <mgmt/mcumgr/smp_bt.h>
#endif


extern bool is_peripheral_conn(struct bt_conn *conn);
extern void connected_to_peripheral(struct bt_conn *conn, uint8_t err);
extern void disconnected_from_peripheral(struct bt_conn *conn, uint8_t reason);
extern int settings_load_after_bluetooth_ready();

static bool is_connected=false;
static bool force_notify=false;
static int64_t last_connect=-1;
static int64_t last_disconnect=-1;

static struct bt_data *bt_adv_data = NULL;
static int bt_adv_data_size=0;
static struct bt_data *bt_scanresp = NULL;
static int bt_scanresp_size=0;




/* Custom Service Variables
 *
 * Generated using  uuid | perl -p -e 's/-//g; s/([0-9a-f]{2})/0x$1,/g;' -e 's/,$//'
 */
#if CONFIG_BT_ID_MAX > 1
static bt_addr_le_t bt_addr;
#endif

static struct k_work advertise_work;


ssize_t read_bool(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 void *buf, uint16_t len, uint16_t offset)
{
	const bool *vp = (bool*)attr->user_data;
	LOG_INF("read_bool %p %s", vp, log_strdup((*vp)?"true":"false"));

	return bt_gatt_attr_read(conn, attr, buf, len, offset, vp, sizeof(bool));
}

ssize_t read_uint16(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 void *buf, uint16_t len, uint16_t offset)
{
	const uint16_t *vp = (uint16_t*)attr->user_data;
	LOG_INF("read_uint16 %p %d", vp, (int)*vp);

	return bt_gatt_attr_read(conn, attr, buf, len, offset, vp, sizeof(uint16_t));
}

ssize_t read_string(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			   void *buf, uint16_t len, uint16_t offset)
{
	char *vp = (char *)attr->user_data;

	LOG_INF("read_string %p %s", vp, log_strdup(vp));

	return bt_gatt_attr_read(conn, attr, buf, len, offset, vp, strlen(vp));
}

ssize_t write_bool(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			    const void *buf, uint16_t len, uint16_t offset,
			    uint8_t flags)
{
	bool *vp = (bool *)attr->user_data;
	bool newval = *vp;
	LOG_INF("write_bool %p %s", vp, log_strdup(newval?"true":"false"));

	if (offset + len > sizeof(bool)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	memcpy(value + offset, buf, len);

	return len;
}

ssize_t write_uint16(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			    const void *buf, uint16_t len, uint16_t offset,
			    uint8_t flags)
{
	uint16_t *vp = (uint16_t*)attr->user_data;
	uint16_t newval = ((uint16_t*)buf)[0];
	//int err;

	LOG_INF("write_uint16 %p %d", vp, newval);

	if (offset + len > sizeof(uint16_t)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	memcpy(value + offset, buf, len);

	// TODO: update settings if relevant

	return len;
}

ssize_t write_string(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			    const void *buf, uint16_t len, uint16_t offset,
			    uint8_t flags)
{
	char *vp = attr->user_data;
	char value_buf[STRING_CHAR_MAX+1];
	//int err;

	// Take a temporary copy of the payload that can be null-terminated
	if (len > STRING_CHAR_MAX) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	memcpy(value_buf, buf, len);
	value_buf[len] = '\0';
	LOG_INF("write_string %p[%d:%d] <= [%s]", vp, (int)offset, (int)len, log_strdup(value_buf));

	// Copy the payload into the destination at the specified offset
	if (offset + len > STRING_CHAR_MAX) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	strncpy(vp + offset, value_buf, STRING_CHAR_MAX-offset+1);

	// FIXME: update settings or do shell command if relevant

	return len;
}

ssize_t write_action(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			    const void *buf, uint16_t len, uint16_t offset,
			    uint8_t flags)
{
	uint8_t *value = attr->user_data;

	// copy the supplied value into the supplied buffer
	if (offset != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	memcpy(value + offset, buf, len);

	// FIXME recfactor this
#if 0
	// Handle the pending action

	LOG_WRN("write_action 0x%02x", (unsigned)action_value);

	//
	// Values 0-7 correspond to action bits that can be passed to the ESP32
	// Values 8-15 are not passed to the ESP32
	//
	switch(action_value) {
	case ACTION_VALUE_IDENTIFY:
		spkr_tone(440, 1000);
		break;
	case ACTION_VALUE_REBOOT:
		sys_reboot(SYS_REBOOT_COLD);
		break;
	}

	// now clear the pending action
	action_value = 0;
#endif

	return len;
}

const struct bt_gatt_cpf bool_cpf = {
	.format = 1,
};

const struct bt_gatt_cpf uint8_cpf = {
	.format = 4,
};

const struct bt_gatt_cpf uint16_cpf = {
	.format = 6,
};
const struct bt_gatt_cpf string_cpf = {
	.format = 25,
};

const struct bt_gatt_cpf millivolt_cpf = {
	.format = 14,
	.exponent = -3,
	.unit = 0x2728,
};



static void set_tx_power(uint8_t handle_type, uint16_t handle, int8_t tx_pwr_lvl)
{
	struct bt_hci_cp_vs_write_tx_power_level *cp;
	struct bt_hci_rp_vs_write_tx_power_level *rp;
	struct net_buf *buf, *rsp = NULL;
	int err;

	buf = bt_hci_cmd_create(BT_HCI_OP_VS_WRITE_TX_POWER_LEVEL,
				sizeof(*cp));
	if (!buf) {
		LOG_ERR("Unable to allocate command buffer\n");
		return;
	}

	cp = net_buf_add(buf, sizeof(*cp));
	cp->handle = sys_cpu_to_le16(handle);
	cp->handle_type = handle_type;
	cp->tx_power_level = tx_pwr_lvl;

	err = bt_hci_cmd_send_sync(BT_HCI_OP_VS_WRITE_TX_POWER_LEVEL,
				   buf, &rsp);
	if (err) {
		uint8_t reason = rsp ?
			((struct bt_hci_rp_vs_write_tx_power_level *)
			  rsp->data)->status : 0;
		LOG_ERR("Set Tx power err: %d reason 0x%02x\n", err, reason);
		return;
	}

	rp = (void *)rsp->data;
	LOG_DBG("Actual Tx Power: %d\n", rp->selected_tx_power);

	net_buf_unref(rsp);
}

static void get_tx_power(uint8_t handle_type, uint16_t handle, int8_t *tx_pwr_lvl)
{
	struct bt_hci_cp_vs_read_tx_power_level *cp;
	struct bt_hci_rp_vs_read_tx_power_level *rp;
	struct net_buf *buf, *rsp = NULL;
	int err;

	*tx_pwr_lvl = 0xFF;
	buf = bt_hci_cmd_create(BT_HCI_OP_VS_READ_TX_POWER_LEVEL,
				sizeof(*cp));
	if (!buf) {
		LOG_ERR("Unable to allocate command buffer\n");
		return;
	}

	cp = net_buf_add(buf, sizeof(*cp));
	cp->handle = sys_cpu_to_le16(handle);
	cp->handle_type = handle_type;

	err = bt_hci_cmd_send_sync(BT_HCI_OP_VS_READ_TX_POWER_LEVEL,
				   buf, &rsp);
	if (err) {
		uint8_t reason = rsp ?
			((struct bt_hci_rp_vs_read_tx_power_level *)
			  rsp->data)->status : 0;
		LOG_ERR("Read Tx power err: %d reason 0x%02x\n", err, reason);
		return;
	}

	rp = (void *)rsp->data;
	*tx_pwr_lvl = rp->tx_power_level;

	net_buf_unref(rsp);
}

static void connected(struct bt_conn *conn, uint8_t err)
{
#if CONFIG_BT_CENTRAL
	if (is_peripheral_conn(conn)) {
		connected_to_peripheral(conn, err);
		return;
	}
#endif

	if (err) {
		LOG_ERR("Connection failed (err 0x%02x)", err);
	} else {
		LOG_WRN("Bluetooth central device connected");
	}
	is_connected = true;
	force_notify = true;
	last_connect = k_uptime_get();

	// Switch to slow blink to signify "it's all good now"
	//FIXME refactor
	//set_led(1,5000);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
#if CONFIG_BT_CENTRAL
	if (is_peripheral_conn(conn)) {
		disconnected_from_peripheral(conn, reason);
		return;
	}
#endif

	LOG_WRN("Bluetooth central device disconnected (reason 0x%02x)", reason);
	last_disconnect = k_uptime_get();
	is_connected = false;
	k_work_submit(&advertise_work);

	// Switch to fast blink to signify "waiting for connection"
	//FIXME refactor
	//set_led(10,500);
}

static struct bt_conn_cb conn_callbacks = {
	.connected = connected,
	.disconnected = disconnected,
};


static void advertise(struct k_work *work)
{
	int err;

	err = bt_le_adv_stop();
	if (err != 0) {
		LOG_ERR("Advertising stop error %d", err);
	}

	if (!bt_adv_data) {
		LOG_ERR("No advertising data");
		return;
	}

	err = bt_le_adv_start(BT_LE_ADV_CONN_NAME,
			      bt_adv_data, bt_adv_data_size,
			      bt_scanresp, bt_scanresp_size
		);
	if (err != 0) {
		LOG_ERR("Advertising failed to start (err %d)", err);
		return;
	}

	LOG_INF("Bluetooth advertising started (%s)", log_strdup(bt_get_name()));
}

int bt_advertise_name(const char *name)
{
	LOG_INF("name=%s", log_strdup(name));
	int err;

	//bt_le_adv_stop();

	err = bt_set_name(name);
	if (err != 0) {
		LOG_ERR("Bluetooth name set error %d", err);
		return err;
	}
	k_work_submit(&advertise_work);
	return 0;
}


static void bt_ready(int err)
{
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return;
	}

	LOG_DBG("Bluetooth initialized");
	settings_load_after_bluetooth_ready();
	LOG_INF("Bluetooth peripheral name is [%s]", log_strdup(name_setting));
	bt_advertise_name(name_setting);
}


int create_bt_addr_for_device()
{
	LOG_DBG("Generating fixed bluetooth address");
#if CONFIG_BT_ID_MAX > 1
	int err;

	// use a fixed BT addr
	bt_addr.type = BT_ADDR_LE_RANDOM;
	for (int i=0; i<6; i++) {
		uint8_t octet;
		octet = device_id[(device_id_len-1-i)];
		LOG_DBG("BT addr[%d] = 0x%02x", i, octet);
		bt_addr.a.val[i] = octet;
	}
	BT_ADDR_SET_STATIC(&(bt_addr.a));

	err = bt_id_create(&bt_addr, NULL);
	if (err != 0) {
		LOG_ERR("bt_id_create: error %d", err);
		return err;
	}
#endif
	return 0;
}

int peripheral_setup(void)
{
	//int8_t txp_get = 0xFF;

	LOG_DBG("Starting Bluetooth");

	k_work_init(&advertise_work, advertise);

	// global pointers to attributes for use by bt_gatt_notify

	create_bt_addr_for_device();

	int err = bt_set_name(name_setting);
	if (err) {
		LOG_ERR("Bluetooth name set error %d", err);
		return err;
	}

	err = bt_enable(bt_ready);
	if (err != 0) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return err;
	}

	//LOG_DBG("Get Tx power level ->");
	//get_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_ADV, 0, &txp_get);
	//LOG_DBG("-> default TXP = %d\n", txp_get);

	//LOG_INF("Set Tx power level to %d\n", 10);
	//set_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_ADV, 0, 10);

	bt_conn_cb_register(&conn_callbacks);

#if CONFIG_MCUMGR_SMP_BT
	smp_bt_register();
#endif

	return 0;
}

int cmd_test_peripheral(const struct shell *shell, size_t argc, char **argv)
{
	int8_t power = 0;

	if ((argc > 1) && (strcmp(argv[1], "txpower")==0)) {
		if (argc > 2) {
			power = (int8_t)atoi(argv[2]);
			LOG_INF("Set Tx power level to %d\n", power);
			set_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_ADV,
				     0, power);
		}
		else {
			get_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_ADV,
				     0, &power);
			shell_fprintf(shell, SHELL_NORMAL, "TX power is %d\n", (int)power);
		}
	}


	if ((argc > 1) && (strcmp(argv[1], "advertise")==0)) {
		if (argc == 2) {
			advertise(NULL);
		}
		else if ((argc > 2) && (strcmp(argv[2], "stop")==0)){
			bt_le_adv_stop();
		}
		else if ((argc > 3) && (strcmp(argv[2], "name")==0)){
			bt_advertise_name(argv[3]);
		}
	}


	return 0;
}
