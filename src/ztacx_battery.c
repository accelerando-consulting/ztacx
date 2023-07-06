#include "ztacx.h"
#include "ztacx_settings.h"

#include <math.h>
#include <zephyr/init.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/shell/shell.h>

#define INVALID_SIGNED -32768

#define VBATT                   DT_PATH(vbatt)
#define ADC_CHANNEL             DT_IO_CHANNELS_INPUT(VBATT)
#define ADC_RESOLUTION		10
#define ADC_GAIN		ADC_GAIN_1_6
#define ADC_REFERENCE		ADC_REF_INTERNAL
#define ADC_ACQUISITION_TIME	ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 40)
#define BUFFER_SIZE		6

enum battery_setting_index {
	SETTING_READ_INTERVAL_SEC = 0,
	SETTING_DIVIDER_HIGH,
	SETTING_DIVIDER_LOW,
	SETTING_MILLIVOLT_CHANGE_THRESHOLD,
};

static struct ztacx_variable battery_settings[] = {
	{"battery_read_interval_sec", ZTACX_VALUE_UINT16,{.val_uint16=CONFIG_ZTACX_BATTERY_READ_INTERVAL}},
	{"battery_divider_high", ZTACX_VALUE_UINT16,{.val_uint16 = CONFIG_ZTACX_BATTERY_DIVIDER_HIGH}},
	{"battery_divider_low", ZTACX_VALUE_UINT16,{.val_uint16 = CONFIG_ZTACX_BATTERY_DIVIDER_LOW}},
	{"battery_millivolt_change_threshold", ZTACX_VALUE_UINT16,{.val_uint16 = CONFIG_ZTACX_BATTERY_CHANGE_THRESHOLD}},
};

enum battery_value_index {
	VALUE_OK = 0,
	VALUE_LEVEL_PERCENT,
	VALUE_MILLIVOLTS,
	VALUE_NOTIFY,
};
static struct ztacx_variable battery_values[]={
	{"battery_ok", ZTACX_VALUE_BOOL, {.val_bool=false}},
	{"battery_level_percent", ZTACX_VALUE_BYTE, {.val_byte=0}},
	{"battery_millivolts", ZTACX_VALUE_UINT16, {.val_uint16=0}},
	{"battery_notify", ZTACX_VALUE_BOOL, {.val_bool=false}},
};


static int16_t battery_samples[BUFFER_SIZE];
static struct k_work_delayable battery_work;

const struct bt_gatt_attr *battery_millivolt_attr = NULL;

static const struct device *battery_adc;

// the channel configuration with channel not yet filled in
static struct adc_channel_cfg battery_acc = {
	.gain             = ADC_GAIN,
	.reference        = ADC_REFERENCE,
	.acquisition_time = ADC_ACQUISITION_TIME,
	.channel_id       = ADC_CHANNEL,
	.differential	  = 0,
#if CONFIG_ADC_CONFIGURABLE_INPUTS
	.input_positive   = ADC_CHANNEL+1
#endif
};

// read operation sequence
static struct adc_sequence battery_asp = {
	.channels = BIT(ADC_CHANNEL),
	.buffer = battery_samples,
	.buffer_size = sizeof(battery_samples),
	.resolution = ADC_RESOLUTION,
	.oversampling = 0,
	.calibrate = 0,
};


void battery_read(struct k_work *work);
int cmd_ztacx_battery(const struct shell *shell, size_t argc, char **argv);

int ztacx_battery_init(struct ztacx_leaf *leaf)
{
	battery_adc = DEVICE_DT_GET(DT_IO_CHANNELS_CTLR(VBATT));
	if (battery_adc == NULL) {
		LOG_ERR("  Battery device not available");
		return -ENOENT;
	}

	ztacx_settings_register(battery_settings, ARRAY_SIZE(battery_settings));
	ztacx_variables_register(battery_values, ARRAY_SIZE(battery_values));

	memset(battery_samples, 0, sizeof(battery_samples));

#if CONFIG_SHELL
	ztacx_shell_cmd_register(((struct shell_static_entry){
				.syntax="battery",
				.help="View battery status",
				.handler=&cmd_ztacx_battery
				}));
#endif

	int err = adc_channel_setup(battery_adc, &battery_acc);

	if (err) {
		LOG_ERR("  battery setup failed (%d)", err);
		ztacx_variable_value_set_bool(&battery_values[VALUE_OK], false);
	}
	else {
		LOG_INF("  battery adc on channel %u", ADC_CHANNEL);
		ztacx_variable_value_set_bool(&battery_values[VALUE_OK], true);
	}

	return err;
}


int ztacx_battery_start(struct ztacx_leaf *leaf)
{
	k_work_init_delayable(&battery_work, battery_read);
	k_work_schedule(&battery_work, K_NO_WAIT);

	return 0;
}

void battery_read(struct k_work *work)
{
	int rc;
	uint8_t battery_level_percent = 0;

	if (!battery_values[VALUE_OK].value.val_bool) {
		return;
	}

	ztacx_variable_value_get(&(battery_values[VALUE_LEVEL_PERCENT]), &battery_level_percent, sizeof(battery_level_percent));
	rc = adc_read(battery_adc, &battery_asp);
	if (rc == 0) {
		int32_t raw = battery_samples[0];
		int32_t val;
		LOG_INF("Raw reading is %d", raw);

		// We are using 1/6th gain comparing to a 600mv
		// reference, so FSR would be 3.6v
		//
		// When divider is 1M-1M  scale by 2x
		// When divider is 100k-220k scale by 1.455
		int div_low = battery_settings[SETTING_DIVIDER_LOW].value.val_uint16;
		int div_high = battery_settings[SETTING_DIVIDER_HIGH].value.val_uint16;
		float scale = (float)(div_high+div_low) / div_low;
		val = 3600 * (raw / 1024.0) * scale;
		LOG_INF("Naive millivolt conversion (using scale %.3f) is %d=> %d" , scale, raw, val);

		val = raw;
		adc_raw_to_millivolts(adc_ref_internal(battery_adc),
				      battery_acc.gain,
				      battery_asp.resolution,
				      &val);
		LOG_INF("Zephyr millivolt conversion is %d", val);


		int delta = abs(val - battery_values[VALUE_MILLIVOLTS].value.val_uint16);
		int threshold = battery_settings[SETTING_MILLIVOLT_CHANGE_THRESHOLD].value.val_uint16;
		bool first_reading = (battery_values[VALUE_MILLIVOLTS].value.val_uint16 == 0);
		bool significant_change = !first_reading && (delta >= threshold);
		if (first_reading || significant_change) {
			uint16_t battery_millivolt_value = val;

			ztacx_variable_value_set(&(battery_values[VALUE_MILLIVOLTS]), &battery_millivolt_value);
			// 2032 presume 3340mv=full, 2240mv=flat
			//battery_level_percent = (val - 2240) * 100 / (3340-2240);
			// 3s 18650 presume 12300mv=full, 9600mv=flat
			//battery_level_percent = (val - 9600) * 100 / (12300-9600);
			//1s lipo presume 4200mv=full, 2800=flat
			battery_level_percent = (val - 2800) * 100 / (4200-2800);


			LOG_INF("battery voltage ~ %d mV (raw %d, %d bogo%%)",
				(int)battery_millivolt_value,
				(int)battery_samples[0],
				(int)battery_level_percent);
			ztacx_variable_value_set(&(battery_values[VALUE_LEVEL_PERCENT]), &battery_level_percent);

#if CONFIG_BT_GATT_CLIENT
			if (battery_values[VALUE_NOTIFY].value.val_bool) {
				bt_gatt_notify(NULL, battery_millivolt_attr,
					       &battery_millivolt_value, sizeof(battery_millivolt_value));
			}
#endif
		}
	}
	else {
		LOG_ERR("Battery read failed %d", rc);
	}

#if CONFIG_BT_BAS
	bt_bas_set_battery_level(battery_level_percent);
#endif

	if (work){
		k_work_reschedule(&battery_work,
				  K_SECONDS(battery_settings[SETTING_READ_INTERVAL_SEC].value.val_uint16));
	}


	return;
}

int cmd_ztacx_battery(const struct shell *shell, size_t argc, char **argv)
{
	if (!battery_values[VALUE_OK].value.val_bool) {
		shell_fprintf(shell, SHELL_NORMAL,"Battery sensor not found\n");
		return -1;
	}

	battery_read(NULL);
#ifdef NOCOMMIT
#if CONFIG_BT_GATT_CLIENT
	bool notify = (argc > 1) && (strcmp(argv[1],"notify")==0);
	if (notify) {
		bt_gatt_notify(NULL, battery_millivolt_attr,
			       &battery_millivolt_value, sizeof(battery_millivolt_value));
	}
#endif
#endif

	uint8_t battery_level_percent;
	uint16_t battery_millivolts;

	ztacx_variable_value_get(&(battery_values[VALUE_LEVEL_PERCENT]), &battery_level_percent, sizeof(battery_level_percent));
	ztacx_variable_value_get(&(battery_values[VALUE_MILLIVOLTS]), &battery_millivolts, sizeof(battery_millivolts));
	shell_fprintf(shell, SHELL_NORMAL,"Battery voltage %dmV (%d%%)\n", (int)battery_millivolts, (int)battery_level_percent);
	return 0;
}
