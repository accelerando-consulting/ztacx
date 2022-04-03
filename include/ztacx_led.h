#pragma once

struct ztacx_led_context
{
	int cycle;
	int duty;
	uint16_t blink_value;
	struct k_work_delayable ledoff;
	struct k_work_delayable ledon;
};
extern struct ztacx_led_context led_leaf_context;

ZTACX_CLASS_AUTO_DEFINE(led);

ZTACX_LEAF_DEFINE(led, led, &led_leaf_context);

#if CONFIG_SHELL
extern int cmd_test_led(const struct shell *shell, size_t argc, char **argv);
#endif
