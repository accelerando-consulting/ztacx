#include "ztacx.h"
#include "ztacx_settings.h"

#include <math.h>
#include <init.h>
#include <drivers/gpio.h>
#include <drivers/adc.h>
#include <drivers/sensor.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/gatt.h>
#include <shell/shell.h>

#ifdef CONFIG_TEMP_NRF5
static const struct device *temp_dev = DEVICE_DT_GET_ANY(nordic_nrf_temp);
#else
static const struct device *temp_dev;
#endif

enum temp_setting_index {
	SETTING_READ_INTERVAL_SEC = 0,
	SETTING_MILLIDEGREE_CHANGE_THRESHOLD,
};

static struct ztacx_variable temp_settings[] = {
	{"temp_read_interval_sec", ZTACX_VALUE_UINT16,{.val_uint16=60}},
	{"temp_millidegree_change_threshold", ZTACX_VALUE_UINT16,{.val_int64 = 500}},
};

enum temp_value_index {
	VALUE_OK = 0,
	VALUE_MILLIDEGREE,
	VALUE_NOTIFY,
};
static struct ztacx_variable temp_values[]={
	{"temp_ok", ZTACX_VALUE_BOOL, {.val_bool=false}},
	{"temp_millidegree", ZTACX_VALUE_INT64, {.val_int64=0}},
	{"temp_notify", ZTACX_VALUE_BOOL, {.val_bool=false}},
};

static struct k_work_delayable temp_work;

const struct bt_gatt_attr *temp_millidegree_attr = NULL;

void temp_read(struct k_work *work);
int cmd_ztacx_temp(const struct shell *shell, size_t argc, char **argv);

int ztacx_temp_init(struct ztacx_leaf *leaf)
{
	if (temp_dev == NULL || !device_is_ready(temp_dev)) {
                LOG_WRN("no temperature device\n");
                temp_dev = NULL;
		return -ENOENT;
        }
	else {
                LOG_INF("temp device is %p, name is %s\n", temp_dev, temp_dev->name);
        }
	ztacx_settings_register(temp_settings, ARRAY_SIZE(temp_settings));
	ztacx_variables_register(temp_values, ARRAY_SIZE(temp_values));

#if CONFIG_SHELL
	ztacx_shell_cmd_register(((struct shell_static_entry){
				.syntax="temp",
				.help="View temperature status",
				.handler=&cmd_ztacx_temp
				}));
#endif
	temp_values[VALUE_OK].value.val_bool = true;

	return 0;
}


int ztacx_temp_start(struct ztacx_leaf *leaf)
{
	LOG_DBG("");
	
	k_work_init_delayable(&temp_work, temp_read);
	k_work_schedule(&temp_work, K_NO_WAIT);

	return 0;
}

void temp_read(struct k_work *work)
{
	LOG_DBG("");
	int rc;
	struct sensor_value temp_value;

	if (!temp_values[VALUE_OK].value.val_bool) {
		LOG_WRN("temperature module not ready");
		return;
	}

	rc = sensor_sample_fetch(temp_dev);
	if (rc != 0) {
		LOG_ERR("sensor_sample_fetch failed, error %d", rc);
		return;
	}

	rc = sensor_channel_get(temp_dev, SENSOR_CHAN_DIE_TEMP, &temp_value);
	if (rc != 0) {
		LOG_ERR("sensor_channel_get failed, error %d", rc);
		return;
	}

	double temperature = sensor_value_to_double(&temp_value);
	int val = temperature * 1000;

	int delta = abs(val - temp_values[VALUE_MILLIDEGREE].value.val_int64);

	int threshold = temp_settings[SETTING_MILLIDEGREE_CHANGE_THRESHOLD].value.val_int64;
	static bool first_reading = true;
	bool significant_change = !first_reading && (delta >= threshold);

	if (first_reading || significant_change) {
		first_reading = false;

		ztacx_variable_value_set(&(temp_values[VALUE_MILLIDEGREE]), &val);
		LOG_INF("temperature ~ %d mC", (int)val);

#if CONFIG_BT_GATT_CLIENT
			if (temp_values[VALUE_NOTIFY].value.val_bool) {
				bt_gatt_notify(NULL, temp_millidegree_attr, &val, sizeof(val));
			}
#endif
	}

	if (work){
		k_work_reschedule(&temp_work,
				  K_SECONDS(temp_settings[SETTING_READ_INTERVAL_SEC].value.val_uint16));
	}

	return;
}

int cmd_ztacx_temp(const struct shell *shell, size_t argc, char **argv)
{
	if (!temp_values[VALUE_OK].value.val_bool) {
		shell_fprintf(shell, SHELL_NORMAL,"Temperature sensor not found\n");
		return -1;
	}

	temp_read(NULL);
	uint64_t temp_millidegree;

	ztacx_variable_get(&(temp_values[VALUE_MILLIDEGREE]), &temp_millidegree, sizeof(temp_millidegree));
	shell_fprintf(shell, SHELL_NORMAL,"Temperature %dmC\n", (int)temp_millidegree);
	return 0;
}
