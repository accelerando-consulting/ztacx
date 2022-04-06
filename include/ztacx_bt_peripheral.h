#pragma once

#include <bluetooth/bluetooth.h>
#include <bluetooth/gatt.h>
#include <bluetooth/uuid.h>


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

extern int ztacx_bt_adv_register(const struct bt_data *adv_data, int adv_len, const struct bt_data *scanresp, int sr_len);
extern int ztacx_bt_service_register(const struct bt_gatt_attr *attrs, int count, struct bt_gatt_service **service_r);
extern int ztacx_bt_characteristic_register(struct bt_gatt_service *service, const struct ztacx_bt_characteristic *chars, int char_count);

extern ssize_t bt_read_variable(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				void *buf, uint16_t len, uint16_t offset);
extern ssize_t bt_write_variable(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				 const void *buf, uint16_t len, uint16_t offset,
				 uint8_t flags);


#if CONFIG_BT_GATT_DYNAMIC_DB 

#define ZTACX_BT_DYNAMIC_CHAR(_name, _desc,...) {		\
	.variable=&(_name),				\
	.desc=(_desc),					\
	.uuid=(struct bt_uuid *)&(char_##_name##_uuid),   \
        __VA_ARGS__                                     \
}
#define ZTACX_BT_DYNAMIC_SENSOR(_name, _desc, ...)  \
	ZTACX_BT_DECLARE_CHAR(_name, _desc,				\
			      .props=(BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY),\
			      .perm=(BT_GATT_PERM_READ | BT_GATT_PERM_WRITE), \
			      __VA_ARGS__)
#define ZTACX_BT_DYNAMIC_SETTING(_name, _desc, ...) ZTACX_VT_DECLARE_CHAR(\
		_name, _desc,\
		.props=(BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE),\
		.perm=(BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),	\
		__VA_ARGS__)


#endif

#define ZTACX_BT_CHAR(_val, _props, _perms, _fmt, _desc) \
        BT_GATT_CHARACTERISTIC(&(char_##_val##_uuid.uuid), _props, _perms, bt_read_variable, bt_write_variable, &(_val)), \
	BT_GATT_CUD(_desc, BT_GATT_PERM_READ), \
	BT_GATT_CPF(&(bt_gatt_cpf_##_fmt))

#define ZTACX_BT_SENSOR( _var, _cpf, _desc) \
	ZTACX_BT_CHAR(_var,					\
		      (BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY),\
		      (BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),	\
		      _cpf,					\
		      _desc)

#define ZTACX_BT_SETTING(_var, _cpf, _desc) \
	ZTACX_BT_CHAR(_var, \
		      (BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE),	\
		      (BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),	\
		      _cpf, \
		      _desc)

extern const struct bt_gatt_cpf bt_gatt_cpf_boolean;
extern const struct bt_gatt_cpf bt_gatt_cpf_uint8;
extern const struct bt_gatt_cpf bt_gatt_cpf_uint16;
extern const struct bt_gatt_cpf bt_gatt_cpf_int16;
extern const struct bt_gatt_cpf bt_gatt_cpf_int32;
extern const struct bt_gatt_cpf bt_gatt_cpf_int64;
extern const struct bt_gatt_cpf bt_gatt_cpf_string;
extern const struct bt_gatt_cpf bt_gatt_cpf_millivolt;
extern const struct bt_gatt_cpf bt_gatt_cpf_centidegree;
extern const struct bt_gatt_cpf bt_gatt_cpf_millimetre;
extern const struct bt_gatt_cpf bt_gatt_cpf_second;
extern const struct bt_gatt_cpf bt_gatt_cpf_lux;

extern int ztacx_bt_peripheral_init(struct ztacx_leaf *leaf);
extern int ztacx_bt_peripheral_start(struct ztacx_leaf *leaf);

ZTACX_CLASS_DEFINE(bt_peripheral, ((struct ztacx_leaf_cb){.init=&ztacx_bt_peripheral_init,.start=&ztacx_bt_peripheral_start}));
ZTACX_LEAF_DEFINE(bt_peripheral, bt_peripheral, NULL);


