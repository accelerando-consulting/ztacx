#include "ztacx.h"
#include "ztacx_kp.h"
#include "ztacx_settings.h"

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/shell/shell.h>

enum kp_setting_index {
	SETTING_BUS = 0,
	SETTING_ADDR,
	SETTING_INTERVAL,
};

enum kp_value_index {
	VALUE_OK = 0,
	VALUE_PINS,
	VALUE_EVENT
};

static struct ztacx_variable kp_default_settings[] = {
	{"bus", ZTACX_VALUE_STRING,{.val_string=CONFIG_ZTACX_KP_BUS}},
	{"addr" , ZTACX_VALUE_UINT16,{.val_uint16 = CONFIG_ZTACX_KP_ADDR}},
	{"interval" , ZTACX_VALUE_INT32,{.val_int32 = CONFIG_ZTACX_KP_INTERVAL}}
};

static struct ztacx_variable kp_default_values[] = {
	{"ok", ZTACX_VALUE_BOOL,{.val_bool=false}},
	{"pins", ZTACX_VALUE_BYTE,{.val_byte=0xFF}},
	{"event", ZTACX_VALUE_EVENT,{.val_event=NULL}},
};

#if CONFIG_SHELL
static int cmd_ztacx_kp(const struct shell *shell, size_t argc, char **argv);
#endif

static void kp_scan(struct k_work *work);

struct ztacx_variable ztacx_kp_kp0_settings[ARRAY_SIZE(kp_default_settings)];
struct ztacx_variable ztacx_kp_kp0_values[ARRAY_SIZE(kp_default_values)];

struct ztacx_kp_context ztacx_kp_kp0_context = {
	.copy_default_context = true,
	.settings = ztacx_kp_kp0_settings,
	.settings_count = ARRAY_SIZE(ztacx_kp_kp0_settings),
	.values = ztacx_kp_kp0_values,
	.values_count = ARRAY_SIZE(ztacx_kp_kp0_values)
};

int ztacx_kp_init(struct ztacx_leaf *leaf)
{
	LOG_INF("ztacx_kp_init %s", leaf->name);
	struct ztacx_kp_context *context = (struct ztacx_kp_context *)leaf->context;
	
	if (context) {
		if (context->copy_default_context) {
			LOG_INF("Copying default context structure for %s", leaf->name);
			int settings_count = ARRAY_SIZE(kp_default_settings);
			ztacx_variables_copy(context->settings, kp_default_settings, settings_count, leaf->name);
			context->settings_count = settings_count;

			int values_count = ARRAY_SIZE(kp_default_values);
			ztacx_variables_copy(context->values, kp_default_values, values_count, leaf->name);
			context->values_count = values_count;
		}
	}
	else { // null context, auto create
		size_t sz = sizeof(struct ztacx_kp_context);
		LOG_INF("Allocating new context structure (%d bytes)", (int)sz);
		context = malloc(sz);
		if (context==NULL) {
			LOG_ERR("ENOMEM");
			return -ENOMEM;
		}
		leaf->context = context;

		context->settings_count = ARRAY_SIZE(kp_default_settings);
		int settings_size =  context->settings_count * sizeof(struct ztacx_variable);
		context->settings = ztacx_variables_dup(
			kp_default_settings, settings_size, leaf->name);
		if (!context->settings) {
			LOG_ERR("ENOMEM");
			return -ENOMEM;
		}

		context->values_count = ARRAY_SIZE(kp_default_values);
		int values_size =  context->values_count * sizeof(struct ztacx_variable);
		context->values = ztacx_variables_dup(
			kp_default_values, values_size, leaf->name);
		if (!context->values) {
			LOG_ERR("ENOMEM");
			return -ENOMEM;
		}
	}

	if (context->settings && context->settings_count) {
		ztacx_settings_register(context->settings, context->settings_count);
	}

	k_event_init(&context->event);
	context->values[VALUE_EVENT].value.val_event = &context->event;

	if (context->values && context->values_count) {
		ztacx_variables_register(context->values, context->values_count);
	}

	if (context->settings && !context->dev) {
		context->dev = device_get_binding(context->settings[SETTING_BUS].value.val_string);
	}
	if (!context->dev) {
		LOG_ERR("No i2c bus device");
		return -ENODEV;
	}
	LOG_INF("Keypad %s uses I2C bus %s",
		leaf->name, context->settings[SETTING_BUS].value.val_string);
	
	/*
	 * Set up the i2c bus
	 */
	LOG_INF("Checking status of %s device", leaf->name);
	if (!device_is_ready(context->dev)) {
		LOG_ERR("KP device is not ready");
		return -ENODEV;
	}

	/* 
	 * Set the pins to input
	 */
	char buf[] = {0xFF};
	int err = i2c_write(context->dev,
			    buf,
			    1,
			    (CTX_SETTING(ADDR).value.val_uint16));
	if (err < 0) {
		LOG_ERR("Failed to configure PCF8574 pins for input");
		return -EIO;
	}
	
#if CONFIG_SHELL
	ztacx_shell_cmd_register(((struct shell_static_entry){
				.syntax=leaf->name,
				.help="Control keypad",
				.handler=&cmd_ztacx_kp
				}));
#endif
	return 0;
}

int ztacx_kp_start(struct ztacx_leaf *leaf)
{
	LOG_INF("start");
	struct ztacx_kp_context *context = leaf->context;
	k_work_init_delayable(&(context->scan), kp_scan);
	k_work_schedule(&context->scan, K_NO_WAIT);

	return 0;
}

int ztacx_kp_stop(struct ztacx_leaf *leaf)
{
	LOG_INF("stop");
	struct ztacx_kp_context *context = leaf->context;
	k_work_cancel_delayable(&context->scan);
	return 0;
}

int ztacx_kp_pre_sleep(struct ztacx_leaf *leaf)
{
	return 0;
}

int ztacx_kp_post_sleep(struct ztacx_leaf *leaf)
{
	return 0;
}

#if CONFIG_SHELL
static int cmd_ztacx_kp(const struct shell *shell, size_t argc, char **argv)
{
	if (argc < 2) {
		LOG_ERR("Insufficient arguments");
		return -ENODEV;
	}
	for (int i=0; i<argc;i++) {
		LOG_INF("  argv[%d]=%s", i, argv[i]);
	}

	struct ztacx_leaf *leaf = ztacx_leaf_get(argv[0]);
	if (!leaf) {
		LOG_ERR("Did not find leaf matching %s", argv[0]);
		return -ENODEV;
	}
	if (!leaf->context) {
		LOG_ERR("Did not find leaf context for %s", leaf->name);
		return -ENODEV;
	}
	struct ztacx_kp_context *context = leaf->context;

	if ((argc > 1) && (strcmp(argv[1], "scan")==0)) {
		LOG_INF("Doing immediate scan");
		k_work_cancel_delayable(&context->scan);
		k_work_reschedule(&context->scan, K_NO_WAIT);
	}
	else if ((argc > 1) && (strcmp(argv[1], "show")==0)) {
		LOG_INF("Showing leaf status");
		shell_fprintf(shell, SHELL_NORMAL, "%s at %s:%02x interval=%d pins=%02x\n",
			     leaf->name,
			     CTX_SETTING(BUS).value.val_string,
			     (int)(CTX_SETTING(ADDR).value.val_uint16),
			     (int)(CTX_SETTING(INTERVAL).value.val_int32),
			     (int)(CTX_VALUE(PINS).value.val_byte));
	}
	else if ((argc > 1) && (strcmp(argv[1], "stop")==0)) {
		LOG_INF("Stopping kp scan");
		ztacx_kp_stop(leaf);
	}
	else if ((argc > 1) && (strcmp(argv[1], "start")==0)) {
		LOG_INF("Starting kp scan");
		ztacx_kp_start(leaf);
	}
	else if ((argc > 1) && (strcmp(argv[1], "addr")==0)) {
		if (argc > 2) {
			ztacx_variable_value_set_uint16(&CTX_SETTING(ADDR), atoi(argv[2]));
		}
		shell_fprintf(shell, SHELL_NORMAL, "%s addr=%d\n", leaf->name, (int)(CTX_SETTING(ADDR).value.val_uint16));
	}
	else if ((argc > 1) && (strcmp(argv[1], "interval")==0)) {
		if (argc > 2) {
			ztacx_variable_value_set_int32(&CTX_SETTING(INTERVAL), atoi(argv[2]));
		}
		shell_fprintf(shell, SHELL_NORMAL, "%s interval=%d\n", leaf->name, (int)(CTX_SETTING(INTERVAL).value.val_int32));
	}

	return(0);
}
#endif

static int kp_leaf_compare_scan(const struct ztacx_leaf *leaf, void *compare) 
{
	struct ztacx_kp_context *context = leaf->context;
	if (&context->scan == compare) {
		return 0;
	}
	return -1;
}

static void kp_scan(struct k_work *work)
{
	struct ztacx_leaf *leaf = ztacx_leaf_find("kp", work, kp_leaf_compare_scan);
	if (!leaf || !leaf->context) {
		LOG_ERR("kp not found");
		return;
	}
	struct ztacx_kp_context *context = leaf->context;
	uint8_t kp_was = CTX_VALUE(PINS).value.val_byte;
	uint8_t kp_new = 0xFF;
	
	int rc = i2c_read(context->dev,
			  &kp_new,
			  1,
			  (CTX_SETTING(ADDR).value.val_uint16));
	if (rc < 0) {
		LOG_ERR("i2c read error %d", rc);
	}
	else {
		if (kp_new != kp_was) {
			LOG_DBG("Keypad change %02x => %02x", (int)kp_was, (int)kp_new);
			ztacx_variable_value_set_byte(&CTX_VALUE(PINS), kp_new);
			for (int bit=0; bit<8; bit++) {
				uint8_t was = kp_was&(1<<bit);
				uint8_t new = kp_new&(1<<bit);
				if (was==new) continue;
				if (new==0) {
					LOG_INF("Button %d press", bit);
					ztacx_variable_value_set_event(
						&CTX_VALUE(EVENT),
						KP_EVENT_PRESS|(1<<bit));
				}
				else {
					LOG_DBG("Button %d release", bit);
					ztacx_variable_value_set_event(
						&CTX_VALUE(EVENT),
						KP_EVENT_RELEASE|(1<<bit));
				}
			}
		}
	}
	
	if (!work) return;
	
	k_work_schedule(&context->scan, K_MSEC(CTX_SETTING(INTERVAL).value.val_int32));
}


