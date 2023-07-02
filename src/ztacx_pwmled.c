#include "ztacx.h"
#include <zephyr/device.h>
#include <zephyr/shell/shell.h>
#include <zephyr/drivers/pwm.h>

static const struct device *pwmled_dev = NULL;
bool pwmled_value;
uint16_t pwmled_duty_value;
uint16_t pwmled_duration_value;

#define PWMLED0_NODE DT_ALIAS(pwmled0)

#if DT_NODE_HAS_STATUS(PWMLED0_NODE, okay)
#define PWM_CTLR	DT_PWMS_CTLR(PWMLED0_NODE)
#define PWM_CHANNEL	DT_PWMS_CHANNEL(PWMLED0_NODE)
#define PWM_FLAGS	DT_PWMS_FLAGS(PWMLED0_NODE)
#endif

int turn_pwmled_off(void)
{
	LOG_INF("pwmled off");
#ifdef PWM_CHANNEL
	pwm_pin_set_usec(pwmled_dev, PWM_CHANNEL, 0, 0, 0);
#endif
	pwmled_value = false;
	return 0;
}

#ifdef PWM_CHANNEL
static struct k_work_delayable pwmledoff;
void turn_pwmled_off_cb(struct k_work *work)
{
	(void)turn_pwmled_off();
}

#endif


int pwmled_setup(void)
{
	/*
	 * Set up the PWM LED device
	 */
	LOG_DBG("Looking up PWMLED device");
#ifdef PWM_CHANNEL
	pwmled_dev = DEVICE_DT_GET(PWM_CTLR);
	if (!device_is_ready(pwmled_dev)) {
		printk("Error: PWMLED device is not ready\n");
		return -ENODEV;
	}
	LOG_INF("PWMLED on PWM channel %d", PWM_CHANNEL);

	k_work_init_delayable(&pwmledoff, turn_pwmled_off_cb);
#else
	LOG_INF("PWMLED not configured");
#endif
	return 0;
}

int pwmled_set_duty(int duty)
{
#ifdef PWM_CHANNEL
	uint32_t period = 1000;
	pwm_pin_set_usec(pwmled_dev, PWM_CHANNEL, period, period * duty / 100, PWM_FLAGS);
#endif
	return 0;
}

int pwmled_adjust_duty(int delta)
{
	int new_duty = pwmled_duty_value;
	new_duty += delta;
	if ((delta < 0) && (new_duty < 0)) new_duty = 0;
	if ((delta > 0) && (new_duty > 100)) new_duty = 100;
	pwmled_duty_value = new_duty;
	if (pwmled_value) return pwmled_set_duty(pwmled_duty_value);
	return 0;
}

int turn_pwmled_on(int duty, int duration_ms)
{

	if (duty == 0) {
		duty = pwmled_duty_value;
	}
	if (duration_ms == 0) {
		duration_ms = pwmled_duration_value;
	}

	LOG_INF("PWM duty=%u duration=%u", duty, duration_ms);

#ifdef PWM_CHANNEL
	// Start the led by using PWM to generate a square wave
	pwmled_set_duty(duty);
	pwmled_value = true;

	// Schedule a stop in {duration_ms} msec
	k_work_schedule(&pwmledoff, K_MSEC(duration_ms));
#endif
	return 0;
}

int cmd_test_pwmled(const struct shell *shell, size_t argc, char **argv)
{
	bool do_setup = false;
	if ((argc > 1) && (strcmp(argv[1],"setup")==0)) {
		do_setup = true;
	}

	if (do_setup) {
		pwmled_setup();
	}

	if ((argc > 2) && (strcmp(argv[1], "duty")==0)) {
		pwmled_duty_value = atoi(argv[2]);
	}
	else if ((argc > 2) && (strcmp(argv[1], "duration")==0)) {
		pwmled_duration_value = atoi(argv[2]);
	}
	else if ((argc > 1) && (strcmp(argv[1], "on")==0)) {
		int duty=0, duration=0;
		if (argc > 2) {
			duty = atoi(argv[2]);
		}
		if (argc > 3) {
			duration = atoi(argv[3]);
		}
		turn_pwmled_on(duty, duration);
	}

	return(0);
}


int pwmled_pre_sleep()
{
	if (pwmled_dev) (void)turn_pwmled_off();
	return 0;
}
