/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define __main__
#include "ztacx.h"
#include <zephyr/drivers/gpio.h>

static int cmd_port0(const struct shell *shell, size_t argc, char **argv);
static int cmd_setpin(const struct shell *shell, size_t argc, char **argv);
static int cmd_clrpin(const struct shell *shell, size_t argc, char **argv);

SHELL_CMD_REGISTER(port0, NULL, "mass alter GPIO port 0", cmd_port0);
SHELL_CMD_REGISTER(setpin, NULL, "set a pin on GPIO port 0", cmd_setpin);
SHELL_CMD_REGISTER(clrpin, NULL, "clear a pin on GPIO port 0", cmd_clrpin);

static const struct device *port0=NULL;

int main(void)
{
	printk("Ztacx Shell sample\n");
	port0 = shell_device_lookup(0, "gpio");
	if (!port0) {
		LOG_ERR("gpio port 0 not found");
	}
}

static int cmd_port0(const struct shell *shell, size_t argc, char **argv)
{
	if (argc < 3) {
		shell_error(shell, "Insufficent arguments: expect port0 <set|clear> <hexval>");
		return -EINVAL;
	}
	if (!port0) {
		shell_error(shell, "port0 not availble");
		return -ENODEV;
	}
	int err=0;
	uint32_t mask = strtoul(argv[2], NULL, 16);
	LOG_INF("Mask is 0x%08x", mask);
	if (strcmp(argv[1], "set")==0) {
		for (int i=0; i<32; i++) {
			if (mask & (1<<i)) {
				err = gpio_pin_configure(port0, i, GPIO_OUTPUT);
				gpio_pin_set(port0, i, 1);
				LOG_INF("  set pin %d", i);
			}
		}
	}
	else if (strcmp(argv[1], "clear")==0) {
		for (int i=0; i<32; i++) {
			if (mask & (1<<i)) {
				err = gpio_pin_configure(port0, i, GPIO_OUTPUT);
				gpio_pin_set(port0, i, 0);
				LOG_INF("  clear pin %d", i);
			}
		}
	}
	return err;
}

static int cmd_setpin(const struct shell *shell, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_error(shell, "Insufficent arguments: expect setpin <pinno>");
		return -EINVAL;
	}
	if (!port0) {
		shell_error(shell, "port0 not availble");
		return -ENODEV;
	}
	int pin = atoi(argv[1]);
	int err = gpio_pin_configure(port0, pin, GPIO_OUTPUT);
	gpio_pin_set(port0, pin, 1);
	LOG_INF("  set pin %d", pin);
	return err;
}

static int cmd_clrpin(const struct shell *shell, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_error(shell, "Insufficent arguments: expect clrpin <pinno>");
		return -EINVAL;
	}
	if (!port0) {
		shell_error(shell, "port0 not available");
		return -ENODEV;
	}
	int pin = atoi(argv[1]);
	int err = gpio_pin_configure(port0, pin, GPIO_OUTPUT);
	gpio_pin_set(port0, pin, 0);
	LOG_INF("  clr pin %d", pin);
	return err;
}

static int app_init(void) 
{
	LOG_INF("NOTICE Initialising app variables");

	return 0;
}

SYS_INIT(app_init, APPLICATION, ZTACX_APP_INIT_PRIORITY);

