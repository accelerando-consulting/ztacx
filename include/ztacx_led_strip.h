#pragma once

struct ztacx_led_strip_context
{
	const struct device *dev;
	struct led_rgb *pixels;
	int cursor;
	struct ztacx_variable *settings;
	int settings_count;
	struct ztacx_variable *values;
	int values_count;
	struct k_work_delayable refresh;
};

extern struct ztacx_led_strip_context ztacx_led_strip_context;

ZTACX_CLASS_AUTO_DEFINE(led_strip);
ZTACX_LEAF_DEFINE(led_strip, led_strip, &ztacx_led_strip_context);

#if CONFIG_SHELL
extern int cmd_test_led_strip(const struct shell *shell, size_t argc, char **argv);
#endif
