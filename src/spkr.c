#include "config.h"
#include "globals.h"

#include <device.h>
#include <stdlib.h>
#include <shell/shell.h>
#include <drivers/pwm.h>
#include <drivers/gpio.h>

static const struct device *spkr_dev = NULL;

#define SPKR_NODE DT_ALIAS(spkr)

#if DT_NODE_HAS_STATUS(SPKR_NODE, okay)
#define PWM_CTLR	DT_PWMS_CTLR(SPKR_NODE)
#define PWM_CHANNEL	DT_PWMS_CHANNEL(SPKR_NODE)
#define PWM_FLAGS	DT_PWMS_FLAGS(SPKR_NODE)
#endif

#define MIN_PERIOD_USEC	(USEC_PER_SEC / 64U)
#define MAX_PERIOD_USEC	USEC_PER_SEC

int spkr_freq = 440;
int spkr_duration = 250;

static struct k_work_delayable spkroff;

void turn_spkr_off(struct k_work *work)
{
	LOG_DBG("spkr off");
#ifdef PWM_CHANNEL
	pwm_pin_set_usec(spkr_dev, PWM_CHANNEL, 0, 0, 0);
#endif
}

int spkr_setup(void)
{
	/*
	 * Set up the speaker device
	 */
	LOG_DBG("Looking up SPKR device");
#ifdef PWM_CHANNEL
	spkr_dev = DEVICE_DT_GET(PWM_CTLR);
	if (!device_is_ready(spkr_dev)) {
		printk("Error: SPKR device is not ready\n");
		return -ENODEV;
	}
	LOG_INF("SPKR on PWM channel %d (vol=%d)", PWM_CHANNEL, spkr_vol);
#else
	LOG_INF("SPKR is not configured");
#endif

	k_work_init_delayable(&spkroff, turn_spkr_off);
	return 0;
}

int spkr_beep()
{
	// Start the speaker by using PWM to generate a square wave
	LOG_INF("BEEP!");
	if (spkr_vol==0) {
		LOG_DBG("No beep, volume is zero");
		return 0;
	}
	
#ifdef PWM_CHANNEL
	uint32_t period = 1000000/spkr_freq ;
	uint32_t period2 = period*spkr_vol/200;
	
	LOG_DBG("Beep period=%u on-time=%d duration=%u", period, period2, spkr_duration);
	int rc = pwm_pin_set_usec(spkr_dev, PWM_CHANNEL, period, period2, 0);
	if (rc < 0) {
		LOG_ERR("PWM error %d", rc);
	}
	
	// Schedule a stop in {spkr_duration} msec
	rc = k_work_schedule(&spkroff, K_MSEC(spkr_duration));
	if (rc < 0) {
		LOG_INF("Off event scheduling errror");
	}
#else
	LOG_WRN("Speaker not configured");
#endif
	return 0;
}

int spkr_tone(int freq, int duration_ms)
{
	if (freq == 0) freq = spkr_freq;
	if (duration_ms == 0) duration_ms = spkr_duration;
	//LOG_INF("freq=%dHz duration=%dms vol=%d", freq, duration_ms, spkr_vol);
	if (spkr_vol==0) {
		return 0;
	}

	// Start the speaker by using PWM to generate a
	// square wave with period 1/{freq}
	//
#ifdef PWM_CHANNEL
	uint32_t period = 1000000/freq ;
	uint32_t period2 = period*spkr_vol/200;
	LOG_DBG("Play %dHz tone %dms period=%d on-time=%d", freq, duration_ms, period, period2);
	pwm_pin_set_usec(spkr_dev, PWM_CHANNEL, period, period2, 0);

	// Schedule a stop in {duration_ms} msec
	k_work_schedule(&spkroff, K_MSEC(duration_ms));
#else
	LOG_WRN("Speaker not configured");
#endif
	return 0;
}

int cmd_test_spkr(const struct shell *shell, size_t argc, char **argv)
{
	bool do_setup = false;
	if ((argc > 1) && (strcmp(argv[1],"setup")==0)) {
		do_setup = true;
	}

	if (do_setup) {
		spkr_setup();
	}

	if ((argc > 2) && (strcmp(argv[1], "freq")==0)) {
		spkr_freq = atoi(argv[2]);
	}
	else if ((argc > 2) && (strcmp(argv[1], "duration")==0)) {
		spkr_duration = atoi(argv[2]);
	}
	else if ((argc==1) || ((argc > 1) && (strcmp(argv[1], "beep")==0))) {
		spkr_beep();
	}
	else if ((argc==1) || ((argc > 1) && (strcmp(argv[1], "gpio")==0))) {
		// manually frob the speaker using gpio, not pwm
		const struct device *dev;
		int ret;

		dev = device_get_binding("gpio1");
		if (dev == NULL) {
			return -1;
		}
		ret = gpio_pin_configure(dev, 13, GPIO_OUTPUT_ACTIVE);
		if (ret < 0) {
			return -1;
		}
		for (int loops=0; loops<1000; loops++) {
			gpio_pin_set(dev, 13, loops%2);
			k_msleep(2);
		}
	}
	else if ((argc > 1) && (strcmp(argv[1], "tone")==0)) {
		int f=0;
		int d=0;
		if (argc > 2) {
			f = atoi(argv[2]);
			if (argc > 3) {
				d=atoi(argv[3]);
			}
		}
		spkr_tone(f,d);
	}
	if ((argc >= 2) && (strcmp(argv[1], "vol")==0)) {
		if (argc>2) {
			spkr_vol = atoi(argv[2]);
			if (spkr_vol > 100) spkr_vol=100;
			if (spkr_vol < 0) spkr_vol=0;
			int err = settings_save_one("app/spkr_vol",
						    (const void *)&spkr_vol,
						    sizeof(spkr_vol));
			if (err < 0) {
				LOG_ERR("Settings save failed for spkr_vol: %d", err);
			}
			else {
				LOG_INF("Saved volume setting %d", spkr_vol);
			}
			
		}
		shell_print(shell, "speaker volume is %d", spkr_vol);
	}

	return(0);
}


int spkr_pre_sleep()
{
	if (spkr_dev) turn_spkr_off(NULL);
	return 0;
}
