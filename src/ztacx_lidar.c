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

#define INVALID_DISTANCE 65535

enum lidar_setting_index {
	SETTING_READ_INTERVAL_SEC = 0,
	SETTING_DISTANCE_THRESHOLD_MM,
	SETTING_DISTANCE_CHANGE_THRESHOLD_MM
};

static struct ztacx_variable lidar_settings[] = {
	{"lidar_read_interval_sec", ZTACX_VALUE_UINT16,{.val_uint16=12}},
	{"lidar_distance_threshold_mm", ZTACX_VALUE_UINT16,{.val_uint16 = 1000}},
	{"lidar_distance_change_threshold_mm", ZTACX_VALUE_UINT16,{.val_uint16 = 50}},
};

enum lidar_value_index {
	VALUE_OK = 0,
	VALUE_DETECT,
	VALUE_DISTANCE_MM,
	VALUE_NOTIFY,

};
static struct ztacx_variable lidar_values[]={
	{"lidar_ok", ZTACX_VALUE_BOOL, {.val_bool=false}},
	{"lidar_detect", ZTACX_VALUE_BOOL, {.val_bool=false}},
	{"lidar_distance_mm", ZTACX_VALUE_UINT16, {.val_uint16=INVALID_DISTANCE}},
	{"lidar_notify", ZTACX_VALUE_BOOL, {.val_bool=false}},
};


static const struct device *lidar_dev=NULL;
const struct bt_gatt_attr *lidar_distance_attr = NULL;
static struct k_work_delayable lidar_work;

void lidar_read(struct k_work *work);
int cmd_ztacx_lidar(const struct shell *shell, size_t argc, char **argv);

int ztacx_lidar_init(struct ztacx_leaf *leaf)
{
	ztacx_settings_register(lidar_settings, ARRAY_SIZE(lidar_settings));
	ztacx_variables_register(lidar_values, ARRAY_SIZE(lidar_values));

#if CONFIG_VL53L0X
	lidar_dev = device_get_binding(DT_LABEL(DT_INST(0, st_vl53l0x)));
#elif CONFIG_VL53L1X
	lidar_dev = device_get_binding(DT_LABEL(DT_INST(0, st_vl53l1x)));
#endif
	if (!lidar_dev) {
		LOG_ERR("  LIDAR device not present");
		lidar_values[VALUE_OK].value.val_bool = false;
		return -ENODEV;
	}

#if CONFIG_SHELL
	ztacx_shell_cmd_register(((struct shell_static_entry){
				.syntax="lidar",
				.help="View LIDAR status",
				.handler=&cmd_ztacx_lidar
				}));
#endif

	LOG_INF("  LIDAR present on I2C as %s", log_strdup(lidar_dev->name));
	lidar_values[VALUE_OK].value.val_bool = true;
	return 0;
}


int ztacx_lidar_start(struct ztacx_leaf *leaf)
{
	k_work_init_delayable(&lidar_work, lidar_read);
	k_work_schedule(&lidar_work, K_NO_WAIT);

	return 0;
}



void lidar_read(struct k_work *work)
{
	int rc;

	if (!lidar_values[VALUE_OK].value.val_bool) {
		return;
	}

	rc = sensor_sample_fetch(lidar_dev);
	if (rc != 0) {
		LOG_ERR("LIDAR sample fetch failed, error %d\n", rc);
		return;
	}

	struct sensor_value value;
	rc = sensor_channel_get(lidar_dev, SENSOR_CHAN_PROX, &value);
	if (rc != 0) {
		LOG_ERR("LIDAR Prox channel get error %d", rc);
	}


	rc = sensor_channel_get(lidar_dev,
				 SENSOR_CHAN_DISTANCE,
				 &value);
	if (rc != 0) {
		LOG_ERR("LIDAR Distance channel get error %d", rc);
	}
	uint16_t distance_mm = (int)(sensor_value_to_double(&value)*1000);
	LOG_DBG("LIDAR distance is %dmm\n", (int)distance_mm);
	
	uint16_t lidar_distance;
	ztacx_variable_value_get(&(lidar_values[VALUE_DISTANCE_MM]), &lidar_distance, sizeof(lidar_distance));

	int delta = abs(distance_mm - lidar_distance);
	int delta_threshold = lidar_settings[SETTING_DISTANCE_CHANGE_THRESHOLD_MM].value.val_uint16;
	bool first_reading = (lidar_values[VALUE_DISTANCE_MM].value.val_uint16 == INVALID_DISTANCE);
	bool significant_change = !first_reading && (delta >= delta_threshold);
	if (first_reading || significant_change) {

		bool detect = distance_mm  < lidar_settings[SETTING_DISTANCE_THRESHOLD_MM].value.val_uint16;
		LOG_INF("LIDAR distance %d mm (%s)", distance_mm, detect?"detect":"nodetect");
		ztacx_variable_value_set(&(lidar_values[VALUE_DISTANCE_MM]), &distance_mm);

		if (detect != lidar_values[VALUE_DETECT].value.val_bool) {
			LOG_INF("LIDAR_DETECT changed");
			ztacx_variable_value_set(&(lidar_values[VALUE_DETECT]), &detect);
		}

#if CONFIG_BT_GATT_CLIENT
		if (lidar_values[VALUE_NOTIFY].value.val_bool) {
			bt_gatt_notify(NULL, lidar_distance_attr, &distance_mm, sizeof(distance_mm));
		}
#endif
	}

	if (work){
		k_work_reschedule(&lidar_work,
				  K_SECONDS(lidar_settings[SETTING_READ_INTERVAL_SEC].value.val_uint16));
	}


	return;
}

int cmd_ztacx_lidar(const struct shell *shell, size_t argc, char **argv)
{
	if (!lidar_values[VALUE_OK].value.val_bool) {
		shell_fprintf(shell, SHELL_NORMAL,"LIDAR sensor not found\n");
		return -1;
	}

	lidar_read(NULL);
#if CONFIG_BT_GATT_CLIENT
	bool notify = (argc > 1) && (strcmp(argv[1],"notify")==0);
	if (notify) {
		uint16_t distance = lidar_values[VALUE_DISTANCE_MM].value.val_uint16;
		bt_gatt_notify(NULL, lidar_distance_attr, distance, sizeof(distance));
	}
#endif

	bool detect;
	uint16_t distance;

	ztacx_variable_value_get(&(lidar_values[VALUE_DETECT]), &detect, sizeof(detect));
	ztacx_variable_value_get(&(lidar_values[VALUE_DISTANCE_MM]), &distance, sizeof(distance));
	shell_fprintf(shell, SHELL_NORMAL,"LIDAR distance %dmm (%s)\n",
		      (int)distance, detect?"detect":"nodetect");
	return 0;
}
