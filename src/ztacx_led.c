#include "ztacx.h"
#include "ztacx_led.h"

#include <device.h>
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

struct ztacx_led_context led_leaf_context={
	.cycle=CONFIG_ZTACX_LED_CYCLE,
	.duty=CONFIG_ZTACX_LED_DUTY,
	.blink_value=0,
};


#if CONFIG_SHELL
static int cmd_ztacx_led(const struct shell *shell, size_t argc, char **argv);
#endif

static void turn_led_on(struct k_work *work);
static void turn_led_off(struct k_work *work);


int ztacx_led_init(struct ztacx_leaf *leaf)
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

#if CONFIG_SHELL
	ztacx_shell_cmd_register(((struct shell_static_entry){
				.syntax="led",
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
	gpio_pin_set(led_dev, PIN, 0);
	return 0;
}

int ztacx_led_pre_sleep()
{
	if (led_dev) gpio_pin_set(led_dev, PIN, 0);
	return 0;
}

int ztacx_led_post_sleep()
{
	return 0;
}


#if CONFIG_SHELL
static int cmd_ztacx_led(const struct shell *shell, size_t argc, char **argv)
{
	bool do_setup = false;
	if ((argc > 1) && (strcmp(argv[1],"setup")==0)) {
		do_setup = true;
	}

	if (do_setup) {
		ztacx_led_init(&ztacx_leaf_led_led);
	}

	if ((argc > 1) && (strcmp(argv[1], "on")==0)) {
		gpio_pin_set(led_dev, PIN, 1);
	}
	else if ((argc > 1) && (strcmp(argv[1], "off")==0)) {
		gpio_pin_set(led_dev, PIN, 0);
	}
	else if ((argc > 2) && (strcmp(argv[1], "duty")==0)) {
		led_leaf_context.duty = atoi(argv[2]);
	}
	else if ((argc > 2) && (strcmp(argv[1], "cycle")==0)) {
		led_leaf_context.cycle = atoi(argv[2]);
	}
	else if ((argc > 2) && (strcmp(argv[1], "blink")==0)) {
		led_leaf_context.blink_value =(strcmp(argv[2], "on")==0);
	}

	return(0);
}
#endif


static void turn_led_on(struct k_work *work)
{
	gpio_pin_set(led_dev, PIN, 1);
	if (work) {
		if (led_leaf_context.blink_value) {
			// when the identify optin is set, fixed 100/100 blink
			k_work_schedule(&(led_leaf_context.ledoff), K_MSEC(100));
		}
		else {
			if (led_leaf_context.duty == 100) {
				// degenerate case, always on
				k_work_schedule(&(led_leaf_context.ledon), K_MSEC(led_leaf_context.cycle));
			}
			else {
				// otherwise, use defined cycle and duty
				int delay = led_leaf_context.cycle * led_leaf_context.duty / 100;
				k_work_schedule(&(led_leaf_context.ledoff), K_MSEC(delay));
			}
		}
	}
}

static void turn_led_off(struct k_work *work)
{
	gpio_pin_set(led_dev, PIN, 0);
	if (work) {
		if (led_leaf_context.blink_value) {
			// when the identify optin is set, fixed 100/100 blink
			k_work_schedule(&(led_leaf_context.ledoff), K_MSEC(100));
		}
		else {
			if (led_leaf_context.duty == 0) {
				// degenerate case, always off
				k_work_schedule(&(led_leaf_context.ledoff), K_MSEC(led_leaf_context.cycle));
			}
			else {
				// otherwise, use defined cycle and duty
				int delay = led_leaf_context.cycle * (100-led_leaf_context.duty) / 100;
				k_work_schedule(&(led_leaf_context.ledon), K_MSEC(delay));
			}
		}
	}
}

void post_error_history_clear()
{
	for (int i=0; i<POST_ERROR_HISTORY_LEN; i++) {
		post_error_history[i]='0';
	}
}

void post_error_history_update(uint8_t err)
{
	for (int i=POST_ERROR_HISTORY_LEN-1; i>0;i--) {
		post_error_history[i] = post_error_history[i-1];
	}
	post_error_history[0] = '0'+err;
}

int post_error(uint8_t err, int count)
{
	LOG_ERR("POST error %d, repeated %dx",err,count);

	post_error_history_update(err);

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
	if (duty > 0) led_leaf_context.duty = (duty%100);
	if (cycle > 0) led_leaf_context.cycle = cycle;
}
