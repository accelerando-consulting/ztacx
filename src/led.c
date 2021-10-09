#include "config.h"
#include "globals.h"
#include "bluetooth.h"
#include "modules.h"

#include <device.h>
#include <stdlib.h>
#include <shell/shell.h>
#include <drivers/gpio.h>

static const struct device *led_dev = NULL;

#define LED0_NODE DT_ALIAS(led0)
#if DT_NODE_HAS_STATUS(LED0_NODE, okay)
#define LED0	DT_GPIO_LABEL(LED0_NODE, gpios)
#define PIN	DT_GPIO_PIN(LED0_NODE, gpios)
#if DT_PHA_HAS_CELL(LED0_NODE, gpios, flags)
#define FLAGS	DT_GPIO_FLAGS(LED0_NODE, gpios)
#endif
#else
/* A build error here means your board isn't set up to blink an LED. */
#error "Unsupported board: led0 devicetree alias is not defined"
#define LED0	""
#define PIN	0
#endif

#ifndef FLAGS
#define FLAGS	0
#endif

int led_cycle = 500;
int led_duty = 10;

static struct k_work_delayable ledoff;
static struct k_work_delayable ledon;

void post_error_history_clear()
{
	for (int j=0; j<POST_DEV_MAX; j++) {
		for (int i=0; i<POST_ERROR_HISTORY_PERDEV; i++) {
			post_error_history[j*(POST_ERROR_HISTORY_PERDEV+1)+i]='0';
		}
		if (j < (POST_DEV_MAX-1)) {                                                                                 post_error_history[j*(POST_ERROR_HISTORY_PERDEV+1)+POST_ERROR_HISTORY_PERDEV]='-';
		}
		else {                                                                                                      post_error_history[j*(POST_ERROR_HISTORY_PERDEV+1)+POST_ERROR_HISTORY_PERDEV]='\0';
		}
	}
}


int post_error_history_notify()
{
	return bt_gatt_notify(NULL, post_attr, post_error_history, POST_ERROR_HISTORY_LEN);
}

uint8_t post_error_history_entry(enum post_device dev, int pos)
{
	if (pos >= POST_ERROR_HISTORY_PERDEV) {
		pos = POST_ERROR_HISTORY_PERDEV-1;
	}
	return post_error_history[((POST_ERROR_HISTORY_PERDEV+1)*dev)+pos]-'0';
}

void post_error_history_update(enum post_device dev, enum post_error err)
{
	// rotate the buffer of POST code history
	// (each device has 5 codes recorded in a fifo)
	int devpos = dev*(POST_ERROR_HISTORY_PERDEV+1);
	for (int i=POST_ERROR_HISTORY_PERDEV-1; i>0;i--) {
		post_error_history[devpos+i] = post_error_history[devpos+i-1];
	}
	post_error_history[devpos] = '0'+err;
	if (notify_post) {
		post_error_history_notify();
	}
}

int post_error(enum post_error err, int count)
{
	LOG_ERR("POST error %d: %s, repeated %dx",
		err,
		((err<POST_ERROR_MAX) && post_error_names[err])?post_error_names[err]:"",
		count);

	enum post_device dev;
	post_error_history_update(dev, err);

	if (!led_dev) return -ENODEV;
	int blinks = (int)err;
	// Deliver ${blinks} short blinks, then wait to seconds.
	// Repeat ${count} times
	gpio_pin_set(led_dev, PIN, 0);
	k_busy_wait(500*1000);

	for (int i = 0; i< count ; i++) {
		for (int j = 0; j<blinks; j++) {
			gpio_pin_set(led_dev, PIN, 1);
			k_busy_wait(125000);
			gpio_pin_set(led_dev, PIN, 0);
			k_busy_wait(125000);
		}
		k_busy_wait(2*1000*1000);
	}
	return 0;
}

void set_led(int duty, int cycle) 
{
	if (duty > 0) led_duty = (duty%100);
	if (cycle > 0) led_cycle = cycle;
}


void turn_led_on(struct k_work *work)
{
	//LOG_DBG("");
	gpio_pin_set(led_dev, PIN, 1);
	if (work) {
		if (blink_value) {
			// when the identify optin is set, fixed 100/100 blink
			k_work_schedule(&ledoff, K_MSEC(100));
		}
		else {
			if (led_duty == 100) {
				// degenerate case, always on
				k_work_schedule(&ledon, K_MSEC(led_cycle));
			}
			else {
				// otherwise, use defined cycle and duty
				int delay = led_cycle * led_duty / 100;
				k_work_schedule(&ledoff, K_MSEC(delay));
			}
		}
	}
}

void turn_led_off(struct k_work *work)
{
	//LOG_DBG("");
	gpio_pin_set(led_dev, PIN, 0);
	if (work) {
		if (blink_value) {
			// when the identify optin is set, fixed 100/100 blink
			k_work_schedule(&ledoff, K_MSEC(100));
		}
		else {
			if (led_duty == 0) {
				// degenerate case, always off
				k_work_schedule(&ledoff, K_MSEC(led_cycle));
			}
			else {
				// otherwise, use defined cycle and duty
				int delay = led_cycle * (100-led_duty) / 100;
				k_work_schedule(&ledon, K_MSEC(delay));
			}
		}
	}
}

int led_setup(void)
{
	int rc;

	/*
	 * Set up the identification LED
	 */
	LOG_DBG("Looking up LED device");
	led_dev = device_get_binding(LED0);
	if (led_dev == NULL) {
		LOG_ERR("Could not get LED device");
		return -ENODEV;
	}
	else {
		rc = gpio_pin_configure(led_dev, PIN, GPIO_OUTPUT_ACTIVE | FLAGS);
		if (rc < 0) {
			LOG_ERR("Could not configure LED GPIO");
		}
		else {
			LOG_INF("LED on pin %d", PIN);
			gpio_pin_set(led_dev, PIN, 0);
		}

	}

	k_work_init_delayable(&ledoff, turn_led_off);
	k_work_init_delayable(&ledon, turn_led_on);

	k_work_schedule(&ledon, K_NO_WAIT);

	return 0;

}

int cmd_test_led(const struct shell *shell, size_t argc, char **argv)
{
	bool do_setup = false;
	if ((argc > 1) && (strcmp(argv[1],"setup")==0)) {
		do_setup = true;
	}

	if (do_setup) {
		led_setup();
	}

	if ((argc > 1) && (strcmp(argv[1], "on")==0)) {
		gpio_pin_set(led_dev, PIN, 1);
	}
	else if ((argc > 1) && (strcmp(argv[1], "off")==0)) {
		gpio_pin_set(led_dev, PIN, 0);
	}
	else if ((argc > 2) && (strcmp(argv[1], "duty")==0)) {
		led_duty = atoi(argv[2]);
	}
	else if ((argc > 2) && (strcmp(argv[1], "cycle")==0)) {
		led_cycle = atoi(argv[2]);
	}
	else if ((argc > 2) && (strcmp(argv[1], "blink")==0)) {
		blink_value =(strcmp(argv[2], "on")==0);
	}

	return(0);
}


int led_pre_sleep()
{
	if (led_dev) gpio_pin_set(led_dev, PIN, 0);
	return 0;
}
