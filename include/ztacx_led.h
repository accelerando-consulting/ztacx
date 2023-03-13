#pragma once

struct ztacx_led_context
{
	struct gpio_dt_spec *gpio;
	bool copy_default_context;
	struct ztacx_variable *settings;
	int settings_count;
	struct ztacx_variable *values;
	int values_count;
	struct k_work_delayable ledoff;
	struct k_work_delayable ledon;
};

extern struct ztacx_led_context ztacx_led_led0_context;

ZTACX_CLASS_AUTO_DEFINE(led);
ZTACX_LEAF_DEFINE(led, led0, &ztacx_led_led0_context);

#if CONFIG_SHELL
extern int cmd_test_led(const struct shell *shell, size_t argc, char **argv);
#endif
