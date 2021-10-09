#include "globals.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <zephyr.h>
#include <init.h>
#include <drivers/gpio.h>
#include <drivers/adc.h>
#include <drivers/sensor.h>

#include <shell/shell.h>

#include "config.h"
#include "bluetooth.h"

#include <bluetooth/bluetooth.h>
#include <bluetooth/services/bas.h>

#define VBATT                   DT_PATH(vbatt)
#define ADC_CHANNEL             DT_IO_CHANNELS_INPUT(VBATT)
#define ADC_RESOLUTION		10
#define ADC_GAIN		ADC_GAIN_1_6
#define ADC_REFERENCE		ADC_REF_INTERNAL
#define ADC_ACQUISITION_TIME	ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 40)
#define BUFFER_SIZE		6

bool battery_ok = false;
uint8_t battery_level = 0;
static int16_t battery_samples[BUFFER_SIZE];

#ifdef FAKE_BATTERY
int battery_setup(const struct device *arg) 
{
	LOG_INF("FAKE Battery setup");
	battery_ok = true;
	battery_level = 100;
	return 0;
}

#else

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

int battery_setup(void)
{
	LOG_INF("Battery setup");

	battery_adc = DEVICE_DT_GET(DT_IO_CHANNELS_CTLR(VBATT));
	if (battery_adc == NULL) {
		LOG_ERR("  Battery device not available");
		return -ENOENT;
	}

	memset(battery_samples, 0, sizeof(battery_samples));

	int err = adc_channel_setup(battery_adc, &battery_acc);
	
	if (err) {
		LOG_ERR("  battery setup failed (%d)", err);
		battery_ok = false;
		return err;
	}

	LOG_INF("  battery adc on channel %u", ADC_CHANNEL);
	battery_ok = true;

	int rc = adc_read(battery_adc, &battery_asp);
	if (rc < 0) {
		LOG_ERR("Initial battery read failed");
	}
	else {
		int32_t raw = battery_samples[0];
		LOG_INF("Initial raw battery value %d", raw);
		LOG_HEXDUMP_INF(battery_samples, sizeof(battery_samples), "battery_samples");
	}

	return 0;
}

#endif

int battery_read(void)
{
	int rc = 0;
	
	battery_level = bt_bas_get_battery_level();
	
	
#ifdef FAKE_BATTERY
	battery_level--;

	if (battery_level==0) {
		battery_level = 100U;
	}
	battery_millivolt_value = 2800 + (battery_level * 540 / 100); // 0% => 2800mV, 100% => 3200mV
#else
	if (battery_ok) {
		rc = adc_read(battery_adc, &battery_asp);
		if (rc == 0) {
			int32_t raw = battery_samples[0];
			int32_t val;
			LOG_INF("Raw reading is %d", raw);

			// We are using 1/6th gain comparing to a 600mv
			// reference, so FSR would be 3.6v
			//
			val = 3600 * (raw / 1024.0) * (400/100.0);
			LOG_INF("Naive millivolt conversion is %d", val);

			val = raw;
			adc_raw_to_millivolts(adc_ref_internal(battery_adc),
					      battery_acc.gain,
					      battery_asp.resolution,
					      &val);
			LOG_INF("Zephyr millivolt conversion is %d", val);

			int delta = abs(val - battery_millivolt_value);
			bool first_reading = (battery_millivolt_value == INVALID_SIGNED);
			bool significant_change = !first_reading && (delta >= battery_millivolt_threshold_setting);
			if (first_reading || force_notify || significant_change) {
				if (first_reading || significant_change) {
					battery_millivolt_value = val;
					// 2032 presume 3340mv=full, 2240mv=flat
					//battery_level = (val - 2240) * 100 / (3340-2240);
					// 3s 18650 presume 12300mv=full, 9600mv=flat
					battery_level = (val - 9600) * 100 / (12300-9600);
				}

				if (significant_change) {
					// Battery voltage has changed
					LOG_INF("battery voltage ~ %d mV (raw %d, %d bogo%%)",
						(int)battery_millivolt_value,
						(int)battery_samples[0],
						(int)battery_level);
				}
				if (notify_millivolt) {
					bt_gatt_notify(NULL, battery_millivolt_attr,
						       &battery_millivolt_value, sizeof(battery_millivolt_value));
				}
			}
		}
		else {
			LOG_ERR("Battery read failed %d", rc);
		}
	}
	else {
		// battery sensor was not found
		rc = -ENOENT;
	}
	
#endif
	bt_bas_set_battery_level(battery_level);
	return rc;
}

int cmd_test_battery(const struct shell *shell, size_t argc, char **argv) 
{
	bool notify = (argc > 1) && (strcmp(argv[1],"notify")==0);

	if (!battery_ok) {
		shell_fprintf(shell, SHELL_NORMAL,"Battery sensor not found\n");
		return -1;
	}

	if (battery_read()!=0) {
		shell_fprintf(shell, SHELL_NORMAL,"Battery read error\n");
		return -1;
	}
	if (notify) {
		bt_gatt_notify(NULL, battery_millivolt_attr,
			       &battery_millivolt_value, sizeof(battery_millivolt_value));
	}

	shell_fprintf(shell, SHELL_NORMAL,"Battery voltage %dmV (%d%%)\n", (int)battery_millivolt_value, (int)battery_level);
	return 0;
}
