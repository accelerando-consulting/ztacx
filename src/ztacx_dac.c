#include "ztacx.h"
#include "ztacx_settings.h"

#include <math.h>
#include <zephyr/init.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/dac.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/shell/shell.h>

#define INVALID_LEVEL INT32_MAX

enum dac_setting_index {
	SETTING_BITS = 0
};

static struct ztacx_variable dac_settings[] = {
	{"dac_bits", ZTACX_VALUE_INT32,{.val_int32=CONFIG_ZTACX_DAC_BITS}},
};

enum dac_value_index {
	VALUE_OK = 0,
	VALUE_LEVEL,
};

static struct ztacx_variable dac_values[]={
	{"dac_ok", ZTACX_VALUE_BOOL, {.val_bool=false}},
	{"dac_level", ZTACX_VALUE_INT32, {.val_int32=INVALID_LEVEL}},
};

static const struct device *dac_dev= DEVICE_DT_GET(DT_ALIAS(dac0));
static struct k_work_delayable dac_work;

void dac_update(struct k_work *work);
int cmd_ztacx_dac(const struct shell *shell, size_t argc, char **argv);

int ztacx_dac_init(struct ztacx_leaf *leaf)
{
	LOG_INF("dac_init");
	ztacx_settings_register(dac_settings, ARRAY_SIZE(dac_settings));
	ztacx_variables_register(dac_values, ARRAY_SIZE(dac_values));

	if (!dac_dev) {
		dac_dev = device_get_binding(CONFIG_ZTACX_DAC_DEVICE);
	}
	if (!dac_dev) {
		LOG_ERR("  DAC device not present");
		ztacx_variable_value_set_bool(&dac_values[VALUE_OK],false);
		return -ENODEV;
	}

#if CONFIG_SHELL
	ztacx_shell_cmd_register(((struct shell_static_entry){
				.syntax="dac",
				.help="View DAC status",
				.handler=&cmd_ztacx_dac
				}));
#endif

	LOG_INF("  DAC present on I2C as %s", dac_dev->name);
	ztacx_variable_value_set_bool(&dac_values[VALUE_OK],true);
	return 0;
}


int ztacx_dac_start(struct ztacx_leaf *leaf)
{
	k_work_init_delayable(&dac_work, dac_update);
	//k_work_schedule(&dac_work, K_NO_WAIT);

	return 0;
}

void dac_update(struct k_work *work)
{
	LOG_DBG("dac_update");

	if (!device_is_ready(dac_dev) || !ztacx_variable_value_get_bool(&dac_values[VALUE_OK])) {
		return;
	}

	uint32_t dac_level = ztacx_variable_value_get_bool(&dac_values[VALUE_LEVEL]);
	if (dac_level == INVALID_LEVEL)
		return;


	return;
}

int cmd_ztacx_dac(const struct shell *shell, size_t argc, char **argv)
{
	LOG_INF("cmd_ztacx_dac argc=%d", argc);
	if (!dac_values[VALUE_OK].value.val_bool) {
		shell_fprintf(shell, SHELL_NORMAL,"DAC device not found\n");
		return -1;
	}
	//dac_dev = device_get_binding(CONFIG_ZTACX_DAC_DEVICE);
	if (argc < 1) return -1;
	int32_t level = atoi(argv[1]);

	ztacx_variable_value_set_int32(&(dac_values[VALUE_LEVEL]), level);
	k_work_schedule(&dac_work, K_NO_WAIT);
	shell_fprintf(shell, SHELL_NORMAL,"DAC output %d\n", (int)level);
	return 0;
}
