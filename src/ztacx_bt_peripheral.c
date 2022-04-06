#include "ztacx.h"
#include "ztacx_settings.h"
#include "ztacx_bt_peripheral.h"

#include <sys/util.h>
#include <sys/byteorder.h>
#include <sys/reboot.h>
#include <bluetooth/gap.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_vs.h>


#if CONFIG_MCUMGR_SMP_BT
#include <mgmt/mcumgr/smp_bt.h>
#endif

enum bt_peripheral_setting_index {
	SETTING_DEVICE_NAME = 0,
	SETTING_SERVICE_ID,
	SETTING_TX_POWER,
};

static struct ztacx_variable bt_peripheral_settings[] = {
	{"peripheral_name", ZTACX_VALUE_STRING},
	{"peripheral_service_id", ZTACX_VALUE_STRING},
	{"peripheral_tx_power", ZTACX_VALUE_UINT16,{.val_uint16 = 0}},
};

enum peripheral_value_index {
	VALUE_OK = 0,
	VALUE_ADVERTISING,
	VALUE_CONNECTED, 
	VALUE_LAST_CONNECT, 
	VALUE_LAST_DISCONNECT, 
};

static struct ztacx_variable bt_peripheral_values[]={
	{"bt_peripheral_ok", ZTACX_VALUE_BOOL, {.val_bool=false}},
	{"bt_peripheral_advertising", ZTACX_VALUE_BOOL, {.val_bool=false}},
	{"bt_peripheral_connected", ZTACX_VALUE_BOOL, {.val_bool=false}},
	{"bt_peripheral_last_connect", ZTACX_VALUE_INT64},
	{"bt_peripheral_last_disconnect", ZTACX_VALUE_INT64},
};


static const struct bt_data *bt_adv_data = NULL;
static int bt_adv_data_size=0;
static const struct bt_data *bt_scanresp = NULL;
static int bt_scanresp_size=0;

#if CONFIG_BT_GATT_DYNAMIC_DB
static struct bt_gatt_service bt_default_service = {
	.attrs=NULL,
	.attr_count=0
};
#endif

const struct bt_gatt_cpf bt_gatt_cpf_boolean = {
	.format = 1,
};

const struct bt_gatt_cpf bt_gatt_cpf_uint8 = {
	.format = 4,
};

const struct bt_gatt_cpf bt_gatt_cpf_uint16 = {
	.format = 6,
};

const struct bt_gatt_cpf bt_gatt_cpf_int16 = {
	.format = 14,
};

const struct bt_gatt_cpf bt_gatt_cpf_int32 = {
	.format = 16,
};

const struct bt_gatt_cpf bt_gatt_cpf_int64 = {
	.format = 18,
};

const struct bt_gatt_cpf bt_gatt_cpf_string = {
	.format = 25,
};

const struct bt_gatt_cpf bt_gatt_cpf_millivolt = {
	.format = 14,
	.exponent = -3,
	.unit = 0x2728,
};

const struct bt_gatt_cpf bt_gatt_cpf_centidegree = {
	.format = 14,
	.exponent = -2,
	.unit = 0x272F,
};

const struct bt_gatt_cpf bt_gatt_cpf_millimetre = {
	.format = 6,
	.exponent = -3,
	.unit = 0x2701,
};

const struct bt_gatt_cpf bt_gatt_cpf_second = {
	.format = 6,
	.unit = 0x2703,
};

const struct bt_gatt_cpf bt_gatt_cpf_lux = {
	.format = 6,
	.unit = 0x2731,
};

#if CONFIG_BT_ID_MAX > 1
static bt_addr_le_t bt_addr;
#endif

static struct k_work advertise_work;
int cmd_ztacx_bt_peripheral(const struct shell *shell, size_t argc, char **argv);
static void stop_advertise();

ssize_t bt_read_variable(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 void *buf, uint16_t len, uint16_t offset)
{
	char desc[100];
	struct ztacx_variable *v = *(struct ztacx_variable **)(attr->user_data);
	ztacx_variable_describe(desc, sizeof(desc), v);
	LOG_INF("read_variable %s len=%d offset=%d", log_strdup(desc),len, offset);

	if (offset != 0) {
		LOG_ERR("Offset handling not implemented");
		return -EIO;
	}

	switch (v->kind) {
	case ZTACX_VALUE_STRING:
		if (len < strlen(v->value.val_string)) {
			LOG_WRN("Unexpected length");
		}
		return bt_gatt_attr_read(conn, attr, buf, len, offset, v->value.val_string, strlen(v->value.val_string));
	case ZTACX_VALUE_BOOL:
		if (len != 1) {
			LOG_WRN("Unexpected length");
		}
		return bt_gatt_attr_read(conn, attr, buf, len, offset, &v->value.val_bool, sizeof(bool));
	case ZTACX_VALUE_BYTE:
		if (len != 1) {
			LOG_WRN("Unexpected length");
		}
		return bt_gatt_attr_read(conn, attr, buf, len, offset, &v->value.val_byte, sizeof(uint8_t));
	case ZTACX_VALUE_UINT16:
		if (len != 2) {
			LOG_WRN("Unexpected length");
		}
		return bt_gatt_attr_read(conn, attr, buf, len, offset, &v->value.val_uint16, sizeof(uint16_t));
	case ZTACX_VALUE_INT16:
		if (len != 2) {
			LOG_WRN("Unexpected length");
		}
		return bt_gatt_attr_read(conn, attr, buf, len, offset, &v->value.val_int16, sizeof(int16_t));
	case ZTACX_VALUE_INT32:
		if (len != 4) {
			LOG_WRN("Unexpected length");
		}
		return bt_gatt_attr_read(conn, attr, buf, len, offset, &v->value.val_int32, sizeof(int32_t));
	case ZTACX_VALUE_INT64:
		if (len != 8) {
			LOG_WRN("Unexpected length");
		}
		return bt_gatt_attr_read(conn, attr, buf, len, offset, &v->value.val_int64, sizeof(int64_t));
	default:
		LOG_ERR("Unhandled variable kind %d", (int)v->kind);
		return -EINVAL;
	}
}


ssize_t bt_write_variable(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			    const void *buf, uint16_t len, uint16_t offset,
			    uint8_t flags)
{
	struct ztacx_variable *v = *(struct ztacx_variable **)(attr->user_data);
	char desc[100];
	ztacx_variable_describe(desc, sizeof(desc), v);

	switch (v->kind) {
	case ZTACX_VALUE_STRING:
	{
		char value_buf[STRING_CHAR_MAX+1];

		// Take a temporary copy of the payload that can be null-terminated
		if (len > STRING_CHAR_MAX) {
			return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
		}
		memcpy(value_buf, buf, len);
		value_buf[len] = '\0';
		LOG_INF("write_string %s <= [%s]", log_strdup(desc), log_strdup(value_buf));

		// Copy the payload into the destination at the specified offset
		if (offset + len > STRING_CHAR_MAX) {
			return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
		}

		if (offset+len > strlen(v->value.val_string)) {
			v->value.val_string = realloc(v->value.val_string, offset+len);
		}
		strncpy(v->value.val_string + offset, value_buf, STRING_CHAR_MAX-offset+1);
	}
		break;
	case ZTACX_VALUE_BOOL: 
	{
		bool *newval = (bool *)buf;
		LOG_INF("write_bool %s <= %s", log_strdup(desc), newval?"true":"false");
		if (offset + len > sizeof(bool)) {
			return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
		}
		char *value = (char *)&v->value.val_bool;
		memcpy(value + offset, buf, len);
	}
		break;
	case ZTACX_VALUE_BYTE:
	{
		uint8_t newval = ((uint8_t*)buf)[0];
		LOG_INF("write_byte %s <= %d", log_strdup(desc), (int)newval);

		if (offset + len > sizeof(uint16_t)) {
			return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
		}
		char *value = (char *)&v->value.val_byte;
		memcpy(value + offset, buf, len);
	}
	        break;
	case ZTACX_VALUE_UINT16: 
	{
		uint16_t newval = ((uint16_t*)buf)[0];
		LOG_INF("write_uint16 %s <= %d", log_strdup(desc), (int)newval);

		if (offset + len > sizeof(uint16_t)) {
			return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
		}
		char *value = (char *)&v->value.val_uint16;
		memcpy(value + offset, buf, len);
	}
		break;
	case ZTACX_VALUE_INT16:
	{
		int16_t newval = ((int16_t*)buf)[0];
		LOG_INF("write_int16 %s <= %d", log_strdup(desc), (int)newval);

		if (offset + len > sizeof(int16_t)) {
			return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
		}
		char *value = (char *)&v->value.val_int16;
		memcpy(value + offset, buf, len);
	}
	case ZTACX_VALUE_INT32:
	{
		int32_t newval = ((int32_t*)buf)[0];
		LOG_INF("write_int32 %s <= %d", log_strdup(desc), (int)newval);

		if (offset + len > sizeof(int32_t)) {
			return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
		}
		char *value = (char *)&v->value.val_int32;
		memcpy(value + offset, buf, len);
	}
	case ZTACX_VALUE_INT64:
	{
		int64_t newval = ((int64_t*)buf)[0];
		LOG_INF("write_int64 %s <= %d", log_strdup(desc), (int)newval);

		if (offset + len > sizeof(int64_t)) {
			return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
		}
		char *value = (char *)&v->value.val_int64;
		memcpy(value + offset, buf, len);
	}
	default:
		LOG_ERR("Unhandled variable kind %d", (int)v->kind);
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	return len;
}

int ztacx_bt_adv_register(const struct bt_data *adv_data, int adv_len, const struct bt_data *scanresp, int sr_len) 
{
	stop_advertise();

	bt_adv_data = adv_data;
	bt_adv_data_size = adv_len;
	bt_scanresp = scanresp;
	bt_scanresp_size = sr_len;
	
	
	if (ztacx_variable_value_get_bool(&bt_peripheral_values[VALUE_OK]) && bt_adv_data && bt_adv_data_size) {
		LOG_INF("Registering advertising data with bluetooth subsystem");
		k_work_submit(&advertise_work);
	}
	return 0;
}


#if CONFIG_BT_GATT_DYNAMIC_DB
int ztacx_bt_service_register(const struct bt_gatt_attr *attrs, int count, struct bt_gatt_service **service_r) 
{
	struct bt_gatt_service *service;
	
	if (!service_r) {
		// If the default service has characteristics already,
		// prepend the service info to the attribute list
		service = &bt_default_service;
		if (service->attr_count) {
			service->attrs = realloc(service->attrs, (service->attr_count + count)*sizeof(struct bt_gatt_attr));
			if (!service->attrs) {
				return -ENOMEM;
			}
			memmove(service->attrs+count, service->attrs, service->attr_count*sizeof(struct bt_gatt_attr));
			LOG_INF("Prepending %d attrs to existing collection", count);
		}
	}
	else {
		// Create a new service
		service = calloc(1, sizeof(struct bt_gatt_service));
		if (!service) {
			return -ENOMEM;
		}
		*service_r = service;
		service->attrs = calloc(count, sizeof(struct bt_gatt_attr));
		LOG_INF("Allocated space for %d attrs", count);
	}
	memcpy(service->attrs, attrs, count*sizeof(struct bt_gatt_attr));
	service->attr_count += count;

	if (ztacx_variable_value_get_bool(&bt_peripheral_values[VALUE_OK])) {
		LOG_INF("Registering service (total %d attrs) with bluetooth subsystem", (int)service->attr_count);
		return bt_gatt_service_register(service);
	}
	return 0;
}

int ztacx_bt_characteristic_register(struct bt_gatt_service *service, const struct ztacx_bt_characteristic *chars, int char_count) 
{
	int attr_count = 0;

	for (int c=0; c < char_count; c++) {
		attr_count += 4; // one for name, one for value, one for desc, one for type
		if (chars[c].ccc) attr_count++;
	}

	if (!service) {
		service = &bt_default_service;
	}

	if (bt_gatt_service_is_registered(service)) {
		LOG_INF("Deregistering service while adding characteristics");
		int err = bt_gatt_service_unregister(service);
		if (err != 0) {
			return err;
		}
	}

	if (service->attr_count) {
		service->attrs = realloc(service->attrs, (service->attr_count + attr_count)*sizeof(struct bt_gatt_attr));
	}
	else {
		service->attrs = calloc(attr_count, sizeof(struct  bt_gatt_attr));
	}
	if (!service->attrs) {
		return -ENOMEM;
	}

	for (int c=0; c < char_count; c++) {
		// Every characteristic has a type and value attr
		struct ztacx_variable *v = *(chars[c].variable);
		if (!v) {
			LOG_WRN("No variable handle for characteristic %d", c);
			continue;
		}
		struct bt_gatt_attr char_attrs[2] = {
			BT_GATT_CHARACTERISTIC(chars[c].uuid, chars[c].props,
					       chars[c].perm,
					       bt_read_variable,
					       bt_write_variable,
					       v
					       )
		};
		memcpy(service->attrs+service->attr_count, char_attrs, 2*sizeof(struct bt_gatt_attr));
		service->attr_count += 2;

		// generate a CUD (name) descriptor, either explicitly or implicitly
		if (chars[c].desc) {
			struct bt_gatt_attr cud = BT_GATT_CUD(chars[c].desc, BT_GATT_PERM_READ);
			memcpy(&(service->attrs[service->attr_count]), &cud, sizeof(struct bt_gatt_attr));
			service->attr_count ++;
		}
		else {
			struct bt_gatt_attr cud = BT_GATT_CUD(v->name, BT_GATT_PERM_READ);
			memcpy(service->attrs+service->attr_count, &cud, sizeof(struct bt_gatt_attr));
			service->attr_count ++;
		}

		// perhaps a custom CPF (type) descriptor (or can infer from variable type)
		if (chars[c].cpf) {
			memcpy(service->attrs+service->attr_count, chars[c].cpf, sizeof(struct bt_gatt_attr));
			service->attr_count ++;
		}
		else {
			struct bt_gatt_attr attr = BT_GATT_CPF(NULL);
			switch (v->kind) {
			case ZTACX_VALUE_STRING:
				attr.user_data = (void*)&bt_gatt_cpf_string;
				break;
			case ZTACX_VALUE_BOOL:
				attr.user_data = (void*)&bt_gatt_cpf_boolean;
				break;
			case ZTACX_VALUE_BYTE:
				attr.user_data = (void*)&bt_gatt_cpf_uint8;
				break;
			case ZTACX_VALUE_UINT16:
				attr.user_data = (void*)&bt_gatt_cpf_uint16;
				break;
			case ZTACX_VALUE_INT16:
				attr.user_data = (void*)&bt_gatt_cpf_int16;
				break;
			case ZTACX_VALUE_INT32:
				attr.user_data = (void*)&bt_gatt_cpf_int32;
				break;
			case ZTACX_VALUE_INT64:
				attr.user_data = (void*)&bt_gatt_cpf_int64;
				break;
			default:
				LOG_ERR("Unhandled variable kind %d", (int)v->kind);
				return -EINVAL;
			}
			memcpy(service->attrs+service->attr_count, &attr, sizeof(struct bt_gatt_attr));
		}
		
		// perhaps a CCC (change notify) descriptor
		if (chars[c].ccc) {
			memcpy(service->attrs+service->attr_count, chars[c].ccc, sizeof(struct bt_gatt_attr));
			service->attr_count ++;
		}
	}
	LOG_INF("Processed %d characteristics, generating %d attributes",
		char_count, service->attr_count);

	if (ztacx_variable_value_get_bool(&bt_peripheral_values[VALUE_OK]) &&
	    ((service->attrs[0].uuid == BT_UUID_GATT_PRIMARY) ||
	     (service->attrs[0].uuid == BT_UUID_GATT_SECONDARY))) {
		// there is a service ID configure, can register the service
		// (if not, wait until ztacx_bt_service_register() gets
		// called)
		LOG_INF("Registering service and characteristics with bluetooth subsystem");
		return bt_gatt_service_register(service);
	}
	return 0;
}
#endif

#if CONFIG_BT_CTLR_TX_PWR_DYNAMIC_CONTROL
void set_tx_power(uint8_t handle_type, uint16_t handle, int8_t tx_pwr_lvl)
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

void get_tx_power(uint8_t handle_type, uint16_t handle, int8_t *tx_pwr_lvl)
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
#endif

void bt_cb_connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		LOG_ERR("Connection failed (err 0x%02x)", err);
	} else {
		LOG_WRN("Bluetooth central device connected");
	}
	ztacx_variable_value_set_bool(&bt_peripheral_values[VALUE_CONNECTED], true);
	ztacx_variable_value_set_int64(&bt_peripheral_values[VALUE_LAST_CONNECT], k_uptime_get());
}

void bt_cb_disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_WRN("Bluetooth central device disconnected (reason 0x%02x)", reason);
	ztacx_variable_value_set_int64(&bt_peripheral_values[VALUE_LAST_DISCONNECT], k_uptime_get());
	ztacx_variable_value_set_bool(&bt_peripheral_values[VALUE_CONNECTED], false);
	k_work_submit(&advertise_work);
}

static struct bt_conn_cb conn_callbacks = {
	.connected = bt_cb_connected,
	.disconnected = bt_cb_disconnected,
};

static void stop_advertise() 
{
	if (ztacx_variable_value_get_bool(&bt_peripheral_values[VALUE_ADVERTISING])) {
		int err = bt_le_adv_stop();
		if (err != 0) {
			LOG_ERR("Advertising stop error %d", err);
		}
		ztacx_variable_value_set_bool(&bt_peripheral_values[VALUE_ADVERTISING], false);
	}
}


static void advertise(struct k_work *work)
{
	int err;

	stop_advertise();

	if (!bt_adv_data || !bt_adv_data_size) {
		LOG_INF("No advertising data provided yet");
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
	ztacx_variable_value_set_bool(&bt_peripheral_values[VALUE_ADVERTISING], true);
	
	LOG_INF("Bluetooth advertising started (%s)", log_strdup(bt_get_name()));
}

int bt_advertise_name(const char *name)
{
	LOG_INF("name=%s", log_strdup(name));

	//bt_le_adv_stop();

#if CONFIG_BT_DEVICE_NAME_DYNAMIC
	int err = bt_set_name(name);
	if (err != 0) {
		LOG_ERR("Bluetooth name set error %d", err);
		return err;
	}
#endif
	
	k_work_submit(&advertise_work);
	return 0;
}

static void bt_ready(int err)
{
	if (err != 0) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return;
	}
	LOG_INF("NOTICE Bluetooth init complete");

#if CONFIG_BT_GATT_DYNAMIC_DB
	if (bt_default_service.attr_count) {
		LOG_INF("Registering default bluetooth service");
		err = bt_gatt_service_register(&bt_default_service);
		if (err != 0) {
			LOG_ERR("Service registration failed: %d", err);
		}
	}
#endif
	
#if CONFIG_BT_SETTINGS
	LOG_INF("Loading bluetooth peristent state");
	settings_load_subtree("bt");
#endif
			
#if CONFIG_BT_DEVICE_NAME_DYNAMIC
	const char *name_override = bt_peripheral_settings[SETTING_DEVICE_NAME].value.val_string;
	if (name_override && strlen(name_override)) {
		strncpy(name_setting, name_override, sizeof(name_setting));
	}
#endif
	LOG_INF("Bluetooth peripheral name is [%s]", log_strdup(name_setting));
	bt_advertise_name(name_setting);

	ztacx_variable_value_set_bool(&bt_peripheral_values[VALUE_OK], true);
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

int ztacx_bt_peripheral_init(struct ztacx_leaf *leaf)
{
	//int8_t txp_get = 0xFF;

	LOG_INF("Initialising Bluetooth Peripheral");

	k_work_init(&advertise_work, advertise);

	ztacx_settings_register(bt_peripheral_settings, ARRAY_SIZE(bt_peripheral_settings));
	ztacx_variables_register(bt_peripheral_values, ARRAY_SIZE(bt_peripheral_values));

	bt_conn_cb_register(&conn_callbacks);

#if CONFIG_MCUMGR_SMP_BT
	smp_bt_register();
#endif

#if CONFIG_SHELL
	ztacx_shell_cmd_register(((struct shell_static_entry){
				.syntax="bt_peripheral",
				.help="View bluetooth peripheral status",
				.handler=&cmd_ztacx_bt_peripheral
				}));
#endif

	return 0;
}

int ztacx_bt_peripheral_start(struct ztacx_leaf *leaf)
{
	LOG_INF("");
	
	create_bt_addr_for_device();

	// global pointers to attributes for use by bt_gatt_notify

	int err;
#if CONFIG_BT_DEVICE_NAME_DYNAMIC
	err = bt_set_name(name_setting);
	if (err) {
		LOG_ERR("Bluetooth name set error %d", err);
		return err;
	}
#endif
	
	err = bt_enable(bt_ready);
	if (err != 0) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return err;
	}

#if CONFIG_BT_CTLR_TX_PWR_DYNAMIC_CONTROL
	int8_t txp_get = 0xFF;
	LOG_DBG("Get Tx power level ->");
	get_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_ADV, 0, &txp_get);
	LOG_DBG("-> default TXP = %d\n", txp_get);

	uint16_t power = ztacx_variable_value_get_uint16(&bt_peripheral_settings[SETTING_TX_POWER]);
	if (power > 0) {
		LOG_INF("Set Tx power level to %d\n", power);
		set_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_ADV, 0, power);
	}
#endif
	return 0;
}

int cmd_ztacx_bt_peripheral(const struct shell *shell, size_t argc, char **argv)
{
#if CONFIG_BT_CTLR_TX_PWR_DYNAMIC_CONTROL
	if ((argc > 1) && (strcmp(argv[1], "txpower")==0)) {
		int8_t power = 0;

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
		return 0;
	}
#endif

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
