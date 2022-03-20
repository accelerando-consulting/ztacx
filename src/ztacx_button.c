#include "ztacx.h"
#include "ztacx_settings.h"
#include "ztacx_button.h"

#include <drivers/gpio.h>

static struct ztacx_leaf *ztacx_buttons[CONFIG_ZTACX_BUTTON_MAX];
#if CONFIG_EVENTS
K_EVENT_DEFINE(ztacx_button_event);
#endif

void button_pressed(const struct device *dev, struct gpio_callback *cb,
		    uint32_t pins)
{
	struct ztacx_leaf *leaf;
	struct ztacx_button_context *ctx;
	int i;
	
	for (i=0; i<CONFIG_ZTACX_BUTTON_MAX; i++) {
		leaf = ztacx_buttons[i];
		ctx = leaf->context;
		
		if (pins & (1<<ctx->button.pin)) {
			break;
		}
	}
	if (i == CONFIG_ZTACX_BUTTON_MAX) {
		LOG_WRN("Interrupt received for pin not maching any button %X", pins);
		return;
	}
	LOG_INF("Button %s pressed", leaf->name);
#if CONFIG_EVENTS
	k_event_post(&ztacx_button_event, ctx->event_bit);
#endif
	if (ctx->handler) {
		k_work_submit(ctx->handler);
	}
}

int ztacx_button_init(struct ztacx_leaf *leaf)
{
	struct ztacx_button_context *ctx = leaf->context;
	int err,i;

	for (i=0; i<CONFIG_ZTACX_BUTTON_MAX; i++) {
		if (ztacx_buttons[i] == NULL) {
			ztacx_buttons[i] = leaf;
			break;
		}
	}
	if (i==CONFIG_ZTACX_BUTTON_MAX) {
		LOG_ERR("Too many buttons, increase CONFIG_ZTACX_BUTTON_MAX");
		return -ENOMEM;
	}
	
	if (!device_is_ready(ctx->button.port)) {
		LOG_ERR("Error: button device %s is not ready\n",
			log_strdup(ctx->button.port->name));
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&ctx->button, GPIO_INPUT);
	if (err != 0) {
		LOG_ERR("Error %d: failed to configure %s pin %d\n",
			err,
			log_strdup(ctx->button.port->name),
			ctx->button.pin);
		return err;
	}

	err = gpio_pin_interrupt_configure_dt(&ctx->button,
					      GPIO_INT_EDGE_TO_ACTIVE);
	if (err != 0) {
		LOG_ERR("Error %d: failed to configure interrupt on %s pin %d\n",
			err, log_strdup(ctx->button.port->name), ctx->button.pin);
		return err;
	}

	gpio_init_callback(&ctx->button_cb_data, button_pressed, BIT(ctx->button.pin));
	gpio_add_callback(ctx->button.port, &ctx->button_cb_data);
	LOG_INF("Set up button at %s pin %d\n", log_strdup(ctx->button.port->name), ctx->button.pin);

	// register a state variable or the button
	snprintf(ctx->state.name, CONFIG_ZTACX_VALUE_NAME_MAX, "%s_state", leaf->name);
	ctx->state.kind = ZTACX_VALUE_BOOL;
	ctx->state.value.val_bool = false;
	ztacx_variables_register(&ctx->state, 1);

	return 0;
}

int ztacx_button_start(struct ztacx_leaf *leaf)
{
	return 0;
}

int ztacx_button_stop(struct ztacx_leaf *leaf)
{
	return 0;
}

int ztacx_button_pre_sleep(struct ztacx_leaf *leaf)
{
	return 0;
}

int ztacx_button_post_sleep(struct ztacx_leaf *leaf)
{
	return 0;
}


