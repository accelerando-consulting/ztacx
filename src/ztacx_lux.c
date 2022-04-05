#include "ztacx.h"
#include "ztacx_settings.h"

#include <math.h>
#include <init.h>
#include <drivers/gpio.h>
#include <drivers/adc.h>
#include <drivers/sensor.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/gatt.h>
#include <bluetooth/services/bas.h>
#include <shell/shell.h>

#define INVALID_LEVEL 65535

enum lux_setting_index {
	SETTING_READ_INTERVAL_SEC = 0,
	SETTING_CHANGE_THRESHOLD = 0,
};

static struct ztacx_variable lux_settings[] = {
	{"lux_read_interval_sec", ZTACX_VALUE_UINT16,{.val_uint16=10}},
	{"lux_change_threshold", ZTACX_VALUE_UINT16,{.val_uint16=1}},
};

enum lux_value_index {
	VALUE_OK = 0,
	VALUE_NOTIFY,
	VALUE_LEVEL,

};
static struct ztacx_variable lux_values[]={
	{"lux_ok", ZTACX_VALUE_BOOL, {.val_bool=false}},
	{"lux_notify", ZTACX_VALUE_BOOL, {.val_bool=false}},
	{"lux_level", ZTACX_VALUE_UINT16, {.val_uint16=INVALID_LEVEL}},
};

static const struct device *lux_dev=NULL;
static struct k_work_delayable lux_work;

void lux_read(struct k_work *work);
int cmd_ztacx_lux(const struct shell *shell, size_t argc, char **argv);

int ztacx_lux_init(struct ztacx_leaf *leaf)
{
	ztacx_settings_register(lux_settings, ARRAY_SIZE(lux_settings));
	ztacx_variables_register(lux_values, ARRAY_SIZE(lux_values));

#if CONFIG_MAX44009
	lux_dev = device_get_binding("MAX44009");
#endif	

	if (!lux_dev) {
		LOG_ERR("  LUX device not present");
		lux_values[VALUE_OK].value.val_bool = false;
		return -ENODEV;
	}

#if CONFIG_SHELL
	ztacx_shell_cmd_register(((struct shell_static_entry){
				.syntax="lux",
				.help="View LUX status",
				.handler=&cmd_ztacx_lux
				}));
#endif

	LOG_INF("  LUX present at I2C %u", 0x29);
	lux_values[VALUE_OK].value.val_bool = true;
	return 0;
}


int ztacx_lux_start(struct ztacx_leaf *leaf)
{
	k_work_init_delayable(&lux_work, lux_read);
	k_work_schedule(&lux_work, K_NO_WAIT);

	return 0;
}



void lux_read(struct k_work *work)
{
	int rc;
	LOG_DBG("lux_read");

	if (!lux_dev || !lux_values[VALUE_OK].value.val_bool) {
		return;
	}

	rc = sensor_sample_fetch_chan(lux_dev, SENSOR_CHAN_LIGHT);
	if (rc != 0) {
		LOG_ERR("LUX sample fetch failed, error %d\n", rc);
		return;
	}

	struct sensor_value value;
	rc = sensor_channel_get(lux_dev, SENSOR_CHAN_LIGHT, &value);
	int val = value.val1;
	int prev_val = lux_values[VALUE_LEVEL].value.val_uint16;
	LOG_DBG("LUX value is %d\n", val);

	int delta = abs(val - prev_val);
	int delta_threshold = lux_settings[SETTING_CHANGE_THRESHOLD].value.val_uint16;
	bool first_reading = (lux_values[VALUE_LEVEL].value.val_uint16 == INVALID_LEVEL);
	bool significant_change = !first_reading && (delta >= delta_threshold);
	if (first_reading || significant_change) {

		ztacx_variable_value_set(&(lux_values[VALUE_LEVEL]), &val);
		LOG_INF("LUX level %d", val);

#if CONFIG_BT_GATT_CLIENT
		if (lux_values[VALUE_NOTIFY].value.val_bool) {
			uint16_t val16 = val;
			bt_gatt_notify(NULL, lux_value_attr, &val16, sizeof(val16));
		}
#endif
	}
	
	if (work){
		k_work_reschedule(&lux_work,
				  K_SECONDS(lux_settings[SETTING_READ_INTERVAL_SEC].value.val_uint16));
	}


	return;
}

int cmd_ztacx_lux(const struct shell *shell, size_t argc, char **argv)
{
	if (!lux_values[VALUE_OK].value.val_bool) {
		shell_fprintf(shell, SHELL_NORMAL,"LUX sensor not found\n");
		return -1;
	}

	lux_read(NULL);

	uint16_t level;

	ztacx_variable_value_get(&(lux_values[VALUE_LEVEL]), &level, sizeof(level));
	shell_fprintf(shell, SHELL_NORMAL,"LUX level %d\n", (int)level);
	return 0;
}
