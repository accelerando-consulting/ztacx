#include "globals.h"
#include <stdio.h>
#include <zephyr.h>
#include <device.h>
#include <init.h>
#include <pm/pm.h>
#include <hal/nrf_gpio.h>
#include <shell/shell.h>

#define CONSOLE_LABEL DT_LABEL(DT_CHOSEN(zephyr_console))

#define BUSY_WAIT_S 5U
#define SLEEP_S 5U

/* Prevent deep sleep (system off) from being entered on long timeouts
 * or `K_FOREVER` due to the default residency policy.
 *
 * This has to be done before anything tries to sleep, which means
 * before the threading system starts up between PRE_KERNEL_2 and
 * POST_KERNEL.  Do it at the start of PRE_KERNEL_2.
 */
static int disable_ds_1(const struct device *dev)
{
	ARG_UNUSED(dev);

	pm_constraint_set(PM_STATE_SOFT_OFF);
	return 0;
}

SYS_INIT(disable_ds_1, PRE_KERNEL_2, 0);

int power_setup(void)
{

	return 0;
}

int power_off(void)
{
	LOG_WRN("Entering deep sleep");
	pre_sleep();

	/* Above we disabled entry to deep sleep based on duration of
	 * controlled delay.  Here we need to override that, then
	 * force entry to deep sleep on any delay.
	 */
	pm_power_state_force((struct pm_state_info){PM_STATE_SOFT_OFF, 0, 0});
	k_sleep(K_FOREVER);

	return -1; // if this gets reached, we failed
}

int power_sleep(int seconds)
{
	int err;
	const struct device *cons = device_get_binding(CONSOLE_LABEL);

	// wake if the wake pin reports input
#ifdef APP_WAKE_PIN
	nrf_gpio_cfg_input(    APP_WAKE_PIN, NRF_GPIO_PIN_PULLUP);
	nrf_gpio_cfg_sense_set(APP_WAKE_PIN, NRF_GPIO_PIN_SENSE_LOW);
#endif

	if (seconds == 0) {
		LOG_WRN("Going to low power mode indefinitely");
	}
	else {
		LOG_WRN("Going to low power mode for %d seconds", seconds);
	}

	err = pm_device_state_set(cons, PM_DEVICE_STATE_LOW_POWER);
	if (err) {
		LOG_ERR("Error setting low power state: %d", err);
		return err;
	}

	if (seconds) {
		k_sleep(K_SECONDS(seconds));
		err = pm_device_state_set(cons, PM_DEVICE_STATE_ACTIVE);
	}

	return err;
}


int cmd_test_power(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	//int err;
	//const struct device *cons = device_get_binding(CONSOLE_LABEL);

	shell_fprintf(shell, SHELL_NORMAL,"\n%s system off demo\n", CONFIG_BOARD);

	shell_fprintf(shell, SHELL_NORMAL,"Busy-wait %u s\n", BUSY_WAIT_S);
	k_busy_wait(BUSY_WAIT_S * USEC_PER_SEC);

	/*
	shell_fprintf(shell, SHELL_NORMAL,"Busy-wait %u s with UART off\n", BUSY_WAIT_S);
	err = device_set_power_state(cons, DEVICE_PM_LOW_POWER_STATE, NULL, NULL);
	k_busy_wait(BUSY_WAIT_S * USEC_PER_SEC);
	err = device_set_power_state(cons, DEVICE_PM_ACTIVE_STATE, NULL, NULL);
	*/

	shell_fprintf(shell, SHELL_NORMAL,"Sleep %u s\n", SLEEP_S);
	k_sleep(K_SECONDS(SLEEP_S));

	/*
	shell_fprintf(shell, SHELL_NORMAL,"Sleep %u s with UART off\n", SLEEP_S);
	power_sleep(SLEEP_S);
	*/

	shell_fprintf(shell, SHELL_NORMAL,"Entering system off; press BUTTON1 to restart\n");
	k_sleep(K_SECONDS(1));

	power_off();

	shell_fprintf(shell, SHELL_NORMAL,"ERROR: System off failed\n");
	while (true) {
		/* spin to avoid fall-off behavior */
	}
	return -1;
}

int cmd_test_poweroff(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	power_off();
	return -1;
}


int cmd_test_idleoff(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int64_t now = k_uptime_get();
	int64_t then = last_disconnect + idle_timeout;
	int64_t remain = then - now;

	shell_fprintf(shell, SHELL_NORMAL, "now=%lld last_disconnect=%lld timeout=%lld then=%lld remain=%lld\n",
		      now, last_connect,idle_timeout,then,remain);

	return 0;
}
