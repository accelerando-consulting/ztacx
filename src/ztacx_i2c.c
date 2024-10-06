#include "ztacx.h"

#include <zephyr/drivers/i2c.h>
#include <zephyr/shell/shell.h>

#if CONFIG_SHELL
static int cmd_ztacx_i2c(const struct shell *shell, size_t argc, char **argv);
#endif

void ztacx_i2c_scan(const char *bus)
{
	LOG_INF("i2c_scan bus=%s", bus);

	const struct device *dev;
	uint8_t cnt = 0, first = 0x04, last = 0x77;
	bool presence [256];

	dev = device_get_binding(bus);

	if (!dev) {
		printk("I2C: Device driver not found\n");
		return;
	}

	for (uint8_t i = 0; i <= last; i += 16) {
		for (uint8_t j = 0; j < 16; j++) {
			if (i + j < first || i + j > last) {
				continue;
			}

			struct i2c_msg msgs[1];
			uint8_t dst;

			/* Send the address to read from */
			msgs[0].buf = &dst;
			msgs[0].len = 0U;
			msgs[0].flags = I2C_MSG_WRITE | I2C_MSG_STOP;
			if (i2c_transfer(dev, &msgs[0], 1, i + j) == 0) {
				presence[i+j]=true;
				++cnt;
			}
			else {
				presence[i+j]=false;
			}
		}
	}
	printk("\n%u devices found on %s\n", cnt, bus);
	printk("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");
	for (uint8_t i = 0; i <= last; i += 16) {
		printk("%02x: ", i);
		for (uint8_t j = 0; j < 16; j++) {
			if (i + j < first || i + j > last) {
				printk("   ");
				continue;
			}

			if (presence[i+j]) {
				printk("%02x ", i + j);
			} else {
				printk("-- ");
			}
		}
		printk("\n");
	}
}

#if CONFIG_SHELL
static int cmd_ztacx_i2c(const struct shell *shell, size_t argc, char **argv)
{
	if (argc < 2) {
		return -ENODEV;
	}

	struct ztacx_leaf *leaf = ztacx_leaf_get(argv[1]);
	if (!leaf && !leaf->context) {
		return -ENODEV;
	}
	if ((argc > 3) && (strcmp(argv[2], "scan")==0)) {
		ztacx_i2c_scan(argv[3]);
	}

	return(0);
}
#endif


int ztacx_i2c_init(struct ztacx_leaf *leaf)
{
#if CONFIG_SHELL
	ztacx_shell_cmd_register(((struct shell_static_entry){
				.syntax=leaf->name,
				.help="Control ztacx i2c",
				.handler=&cmd_ztacx_i2c
				}));
#endif

	//ztacx_i2c_scan(leaf->name);
	return 0;
}

int ztacx_i2c_start(struct ztacx_leaf *leaf)
{
	return 0;
}
