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

#define INVALID_LEVEL INT32_MAX

enum ims_setting_index {
	SETTING_READ_INTERVAL_USEC = 0,
	SETTING_CHANGE_THRESHOLD,
};

static struct ztacx_variable ims_settings[] = {
	{"ims_read_interval_usec", ZTACX_VALUE_INT32,{.val_int32=CONFIG_ZTACX_IMS_READ_INTERVAL_USEC}},
	{"ims_change_threshold", ZTACX_VALUE_INT32,{.val_int32=CONFIG_ZTACX_IMS_CHANGE_THRESHOLD}},
};

enum ims_value_index {
	VALUE_OK = 0,
	VALUE_NOTIFY,
	VALUE_LEVEL_X,
	VALUE_LEVEL_Y,
	VALUE_LEVEL_Z,
	VALUE_LEVEL_M,
	VALUE_PEAK_X,
	VALUE_PEAK_Y,
	VALUE_PEAK_Z,
	VALUE_PEAK_M,
	VALUE_SAMPLES,
};

static struct ztacx_variable ims_values[]={
	{"ims_ok", ZTACX_VALUE_BOOL, {.val_bool=false}},
	{"ims_notify", ZTACX_VALUE_BOOL, {.val_bool=false}},
	{"ims_level_x", ZTACX_VALUE_INT32, {.val_int32=INVALID_LEVEL}},
	{"ims_level_y", ZTACX_VALUE_INT32, {.val_int32=INVALID_LEVEL}},
	{"ims_level_z", ZTACX_VALUE_INT32, {.val_int32=INVALID_LEVEL}},
	{"ims_level_m", ZTACX_VALUE_INT32, {.val_int32=INVALID_LEVEL}},
	{"ims_peak_x", ZTACX_VALUE_INT32, {.val_int32=INVALID_LEVEL}},
	{"ims_peak_y", ZTACX_VALUE_INT32, {.val_int32=INVALID_LEVEL}},
	{"ims_peak_z", ZTACX_VALUE_INT32, {.val_int32=INVALID_LEVEL}},
	{"ims_peak_m", ZTACX_VALUE_INT32, {.val_int32=INVALID_LEVEL}},
	{"ims_samples", ZTACX_VALUE_INT64, {.val_int64=0}},
};

static const struct device *ims_dev=NULL;
static struct k_work_delayable ims_work;

void ims_read(struct k_work *work);
int cmd_ztacx_ims(const struct shell *shell, size_t argc, char **argv);

int ztacx_ims_init(struct ztacx_leaf *leaf)
{
	LOG_INF("ims_init");
	ztacx_settings_register(ims_settings, ARRAY_SIZE(ims_settings));
	ztacx_variables_register(ims_values, ARRAY_SIZE(ims_values));

	ims_dev = device_get_binding(CONFIG_ZTACX_IMS_DEVICE);

	if (!ims_dev) {
		LOG_ERR("  IMS device not present");
		ztacx_variable_value_set_bool(&ims_values[VALUE_OK],false);
		return -ENODEV;
	}

#if CONFIG_SHELL
	ztacx_shell_cmd_register(((struct shell_static_entry){
				.syntax="ims",
				.help="View IMS status",
				.handler=&cmd_ztacx_ims
				}));
#endif

	LOG_INF("  IMS present on I2C as %s", log_strdup(ims_dev->name));
	ztacx_variable_value_set_bool(&ims_values[VALUE_OK],true);
	return 0;
}


int ztacx_ims_start(struct ztacx_leaf *leaf)
{
	k_work_init_delayable(&ims_work, ims_read);
	k_work_schedule(&ims_work, K_NO_WAIT);

	return 0;
}

static bool update_variable_peak(struct ztacx_variable *v, struct ztacx_variable *peak_v, int32_t level, int change_threshold)
{
	int32_t prev = ztacx_variable_value_get_int32(v);
	int32_t change = abs(level - prev);
	bool first_reading = (prev == INVALID_LEVEL);
	bool significant_change = !first_reading && (change >= change_threshold);
	if (first_reading || significant_change) {
		ztacx_variable_value_set(v, &level);
		//LOG_INF("IMS %s level %d", v->name, (int)level);

		int32_t peak_level = ztacx_variable_value_get_int32(peak_v);
		if (peak_level == INVALID_LEVEL || (abs(level)>peak_level)) {
			LOG_INF("IMS %s new peak %d", v->name, (int)level);
			peak_level = abs(level);
			ztacx_variable_value_set_int32(peak_v, peak_level);
		}
		return true;
	}
	return false;
}

void ims_read(struct k_work *work)
{
	int rc;
	LOG_DBG("ims_read");

	ims_dev = device_get_binding(CONFIG_ZTACX_IMS_DEVICE);//NOCOMMIT
	
	if (!ims_dev || !ztacx_variable_value_get_bool(&ims_values[VALUE_OK])) {
		return;
	}

	rc = sensor_sample_fetch(ims_dev);
	if (rc < 0) {
		LOG_ERR("IMS sample fetch failed, error %d\n", rc);
	}
	else {
		
		struct sensor_value accel[3];
		rc = sensor_channel_get(ims_dev, SENSOR_CHAN_ACCEL_XYZ, accel);

#if CONFIG_ADXL345
		// ADXL345 reports directly in cm/s/s
		//LOG_INF("IMS XYZ reading [%d.%06d,%d.%06d,%d.%06d]",accel[0].val1,accel[0].val2,accel[1].val1,accel[2].val2,accel[2].val1,accel[2].val2);
		int x_cmpsps = accel[0].val1;
		int y_cmpsps = accel[1].val1;
		int z_cmpsps = accel[2].val1 ;
#else
		// sane sensors report in m/s because it is not 1973
		int x_cmpsps = accel[0].val1*100+accel[0].val2/10000;
		int y_cmpsps = accel[1].val1*100+accel[1].val2/10000;
		int z_cmpsps = accel[2].val1*100+accel[2].val2/10000;
#endif
	
		int m_cmpsps = sqrt((x_cmpsps*x_cmpsps)+(y_cmpsps*y_cmpsps)+(z_cmpsps*z_cmpsps));                 // polar magnitude of acceleration in cm/s/s
		//LOG_INF("Acceleration vector magnitude is %dcm/s/s", m_cmpsps);

		int change_threshold = ztacx_variable_value_get_int32(&ims_settings[SETTING_CHANGE_THRESHOLD]);
		bool change = false;

		change |= update_variable_peak(&ims_values[VALUE_LEVEL_M], &ims_values[VALUE_PEAK_M], m_cmpsps, change_threshold);
		change |= update_variable_peak(&ims_values[VALUE_LEVEL_X], &ims_values[VALUE_PEAK_X], x_cmpsps, change_threshold);
		change |= update_variable_peak(&ims_values[VALUE_LEVEL_Y], &ims_values[VALUE_PEAK_Y], y_cmpsps, change_threshold);
		change |= update_variable_peak(&ims_values[VALUE_LEVEL_Z], &ims_values[VALUE_PEAK_Z], z_cmpsps, change_threshold);
		ztacx_variable_value_inc_int64(&ims_values[VALUE_SAMPLES]);

#if 0  // CONFIG_BT_GATT_CLIENT
		if (change && ztacx_variable_value_get_bool(&ims_values[VALUE_NOTIFY])) {
			bt_gatt_notify(NULL, ims_value_attr, &mag_mg, sizeof(mag_mg));
		}
#endif
	}
	
	if (work){
		k_work_reschedule(&ims_work,
				  K_USEC(ztacx_variable_value_get_int32(&ims_settings[SETTING_READ_INTERVAL_USEC])));
	}

	return;
}

int cmd_ztacx_ims(const struct shell *shell, size_t argc, char **argv)
{
	if (!ims_values[VALUE_OK].value.val_bool) {
		shell_fprintf(shell, SHELL_NORMAL,"IMS sensor not found\n");
		return -1;
	}
	ims_dev = device_get_binding(CONFIG_ZTACX_IMS_DEVICE);

	ims_read(NULL);

	int32_t level;

	ztacx_variable_value_get(&(ims_values[VALUE_LEVEL_M]), &level, sizeof(level));
	shell_fprintf(shell, SHELL_NORMAL,"IMS magnitude %d\n", (int)level);
	return 0;
}
