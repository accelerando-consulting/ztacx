#include "ztacx.h"
#include "ztacx_led.h"
#include "ztacx_settings.h"

#include <device.h>
#include <drivers/gpio.h>
#include <shell/shell.h>

enum led_setting_index {
	SETTING_CYCLE = 0,
	SETTING_DUTY
};

enum led_value_index {
	VALUE_OK = 0,
	VALUE_BLINK,
	VALUE_LIT
};

static struct ztacx_variable led_default_settings[] = {
	{"cycle", ZTACX_VALUE_UINT16,{.val_uint16=CONFIG_ZTACX_LED_CYCLE}},
	{"duty" , ZTACX_VALUE_BYTE,{.val_byte = CONFIG_ZTACX_LED_DUTY}}
};

static struct ztacx_variable led_default_values[] = {
	{"ok", ZTACX_VALUE_BOOL},
	{"blink", ZTACX_VALUE_BOOL},
	{"lit", ZTACX_VALUE_BOOL}
};

#if CONFIG_SHELL
static int cmd_ztacx_led(const struct shell *shell, size_t argc, char **argv);
#endif

static void turn_led_on(struct k_work *work);
static void turn_led_off(struct k_work *work);

static struct gpio_dt_spec default_led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

struct ztacx_variable ztacx_led_led0_settings[ARRAY_SIZE(led_default_settings)];
struct ztacx_variable ztacx_led_led0_values[ARRAY_SIZE(led_default_values)];

struct ztacx_led_context ztacx_led_led0_context = {
	.gpio = &default_led,
	.copy_default_context = true,
	.settings = ztacx_led_led0_settings,
	.settings_count = ARRAY_SIZE(ztacx_led_led0_settings),
	.values = ztacx_led_led0_values,
	.values_count = ARRAY_SIZE(ztacx_led_led0_values)
};

int ztacx_led_init(struct ztacx_leaf *leaf)
{
	LOG_INF("ztacx_led_init");
	int rc;
	struct ztacx_led_context *context = (struct ztacx_led_context *)leaf->context;
	
	if (context) {
		if (context->copy_default_context) {
			int settings_count = ARRAY_SIZE(led_default_settings);
			ztacx_variables_copy(context->settings, led_default_settings, settings_count, leaf->name);
			context->settings_count = settings_count;

			int values_count = ARRAY_SIZE(led_default_values);
			ztacx_variables_copy(context->values, led_default_values, values_count, leaf->name);
			context->values_count = values_count;
		}
	}
	else { // null context, auto create
		size_t sz = sizeof(struct ztacx_led_context);
		LOG_INF("Allocating new context structure (%d bytes)", (int)sz);
		context = malloc(sz);
		if (context==NULL) {
			LOG_ERR("ENOMEM");
			return -ENOMEM;
		}
		leaf->context = context;
		context->gpio = &default_led;

		context->settings_count = ARRAY_SIZE(led_default_settings);
		int settings_size =  context->settings_count * sizeof(struct ztacx_variable);

		context->values_count = ARRAY_SIZE(led_default_values);
		int values_size =  context->values_count * sizeof(struct ztacx_variable);
		context->settings = ztacx_variables_dup(led_default_settings, settings_size, leaf->name);
		
		if (!context->settings) {
			LOG_ERR("ENOMEM");
			return -ENOMEM;
		}

		context->values = ztacx_variables_dup(led_default_values, values_size, leaf->name);
		if (!context->values) {
			LOG_ERR("ENOMEM");
			return -ENOMEM;
		}
	}

	if (context->settings && context->settings_count) {
		ztacx_settings_register(context->settings, context->settings_count);
	}
	if (context->values && context->values_count) {
		ztacx_variables_register(context->values, context->values_count);
	}

	if (!context->gpio) {
		LOG_ERR("No gpio device");
		return -ENODEV;
	}
	

	/*
	 * Set up the identification LED
	 */
	LOG_INF("Checking status of %s device", log_strdup(leaf->name));
	if (!context->gpio->port) {
		LOG_ERR("LED Device '%s' not configured", log_strdup(leaf->name));
		return -ENODEV;
	}
			
	if (!device_is_ready(context->gpio->port)) {
		LOG_ERR("LED device is not ready");
		return -ENODEV;
	}

	rc = gpio_pin_configure_dt(context->gpio, GPIO_OUTPUT);
	if (rc < 0) {
		LOG_ERR("Could not configure LED GPIO");
	}
	else {
		LOG_INF("LED %s on pin %d", log_strdup(leaf->name), context->gpio->pin);
		gpio_pin_set_dt(context->gpio, 0);
	}

#if CONFIG_SHELL
	ztacx_shell_cmd_register(((struct shell_static_entry){
				.syntax=leaf->name,
				.help="Control ztacx status led",
				.handler=&cmd_ztacx_led
				}));
#endif
	return 0;
}

int ztacx_led_start(struct ztacx_leaf *leaf)
{
	struct ztacx_led_context *context = leaf->context;
	k_work_init_delayable(&(context->ledoff), turn_led_off);
	k_work_init_delayable(&(context->ledon), turn_led_on);

	k_work_schedule(&context->ledon, K_NO_WAIT);

	return 0;
}

int ztacx_led_stop(struct ztacx_leaf *leaf)
{
	struct ztacx_led_context *context = leaf->context;
	k_work_cancel_delayable(&context->ledon);
	k_work_cancel_delayable(&context->ledoff);
	gpio_pin_set_dt(context->gpio, 0);
	return 0;
}

int ztacx_led_pre_sleep(struct ztacx_leaf *leaf)
{
	struct ztacx_led_context *context = leaf->context;

	if (context->gpio->port) {
		gpio_pin_set_dt(context->gpio, 0);
	}
	return 0;
}

int ztacx_led_post_sleep(struct ztacx_leaf *leaf)
{
	return 0;
}


#if CONFIG_SHELL
static int cmd_ztacx_led(const struct shell *shell, size_t argc, char **argv)
{
	if (argc < 2) {
		return -ENODEV;
	}

	struct ztacx_leaf *leaf = ztacx_leaf_get(argv[1]);
	if (!leaf && !leaf->context) {
		return -ENODEV;
	}
	struct ztacx_led_context *context = leaf->context;


	if ((argc > 2) && (strcmp(argv[2], "on")==0)) {
		k_work_reschedule(&context->ledon, K_NO_WAIT);
	}
	else if ((argc > 2) && (strcmp(argv[2], "off")==0)) {
		k_work_reschedule(&context->ledoff, K_NO_WAIT);
	}
	else if ((argc > 3) && (strcmp(argv[2], "duty")==0)) {
		ztacx_variable_value_set_string(&CTX_SETTING(DUTY), argv[3]);
	}
	else if ((argc > 2) && (strcmp(argv[1], "cycle")==0)) {
		ztacx_variable_value_set_string(&CTX_SETTING(CYCLE), argv[3]);
	}
	else if ((argc > 2) && (strcmp(argv[1], "blink")==0)) {
		ztacx_variable_value_set_string(&CTX_VALUE(BLINK), argv[3]);
	}

	return(0);
}
#endif

static int led_leaf_compare_ledon(const struct ztacx_leaf *leaf, void *compare) 
{
	struct ztacx_led_context *context = leaf->context;
	if (&context->ledon == compare) {
		return 0;
	}
	return -1;
}

static int led_leaf_compare_ledoff(const struct ztacx_leaf *leaf, void *compare) 
{
	struct ztacx_led_context *context = leaf->context;
	if (&context->ledoff == compare) {
		return 0;
	}
	return -1;
}

static void turn_led_on(struct k_work *work)
{
	struct ztacx_leaf *leaf = ztacx_leaf_find("led", work, led_leaf_compare_ledon);
	if (!leaf || !leaf->context) return;
	struct ztacx_led_context *context = leaf->context;

	gpio_pin_set_dt(context->gpio, 1);
	if (!work) return;
	
	if (ztacx_variable_value_get_bool(&CTX_VALUE(BLINK))) {
		// when the identify option is set, fixed 100/100 blink
		k_work_schedule(&context->ledoff, K_MSEC(100));
	}
	else {
		int duty = ztacx_variable_value_get_byte(&CTX_SETTING(DUTY));
		int cycle = ztacx_variable_value_get_uint16(&CTX_SETTING(CYCLE));
		if (duty == 100) {
			// degenerate case, always on
			k_work_schedule(&(context->ledon), K_MSEC(cycle));
		}
		else {
			// otherwise, use defined cycle and duty
			int delay = cycle * duty / 100;
			k_work_schedule(&context->ledoff, K_MSEC(delay));
		}
	}
}

static void turn_led_off(struct k_work *work)
{
	struct ztacx_leaf *leaf = ztacx_leaf_find("led", work, led_leaf_compare_ledoff);
	if (!leaf || !leaf->context) return;
	struct ztacx_led_context *context = leaf->context;

	gpio_pin_set_dt(context->gpio, 0);
	if (!work) return;
	

	if (ztacx_variable_value_get_bool(&context->values[VALUE_BLINK])) {
		// when the identify option is set, fixed 100/100 blink
		k_work_schedule(&context->ledon, K_MSEC(100));
	}
	else {
		int duty = ztacx_variable_value_get_byte(&CTX_SETTING(DUTY));
		int cycle = ztacx_variable_value_get_uint16(&CTX_SETTING(CYCLE));
		if (duty == 0) {
			// degenerate case, always off
			k_work_schedule(&context->ledoff, K_MSEC(cycle));
		}
		else {
			// otherwise, use defined cycle and duty
			int delay = cycle * (100-duty) / 100;
			k_work_schedule(&context->ledon, K_MSEC(delay));
		}
	}
}

