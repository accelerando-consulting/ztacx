#pragma once

struct ztacx_led_context
{
	struct gpio_dt_spec *gpio;
	struct ztacx_variable *settings;
	struct ztacx_variable *values;
	struct k_work_delayable ledoff;
	struct k_work_delayable ledon;
};
extern struct ztacx_led_context led0_leaf_context;

ZTACX_CLASS_AUTO_DEFINE(led);

ZTACX_LEAF_DEFINE(led, led0, &led0_leaf_context);

#if CONFIG_SHELL
extern int cmd_test_led(const struct shell *shell, size_t argc, char **argv);
#endif
