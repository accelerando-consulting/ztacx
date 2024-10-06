#include "ztacx.h"
#include "ztacx_led_strip.h"
#include "ztacx_settings.h"

#include <zephyr/device.h>
#include <zephyr/shell/shell.h>
#include <zephyr/drivers/led_strip.h>

#define STRIP_NODE		DT_ALIAS(led_strip)
#define STRIP_NUM_PIXELS	DT_PROP(DT_ALIAS(led_strip), chain_length)

enum led_setting_index {
	SETTING_LENGTH = 0,
	SETTING_CYCLE,
	SETTING_DUTY
};

enum led_value_index {
	VALUE_OK = 0,
	VALUE_CURSOR,
	VALUE_COLOR,
	VALUE_BLINK,  //NOTIMPL
	VALUE_LIT     //NOTIMPL
};

static struct k_work led_strip_cursor_onchange;
static struct k_work led_strip_color_onchange;

static struct ztacx_variable ztacx_led_strip_settings[] = {
	{"length", ZTACX_VALUE_UINT16,{.val_uint16=STRIP_NUM_PIXELS}},
};

static struct ztacx_variable ztacx_led_strip_values[] = {
	{.name="led_strip_ok",     .kind=ZTACX_VALUE_BOOL },
	{.name="led_strip_cursor", .kind=ZTACX_VALUE_UINT16,.value={.val_uint16=0}},
	{.name="led_strip_color",  .kind=ZTACX_VALUE_STRING},
	{.name="led_strip_blink",  .kind=ZTACX_VALUE_BOOL },
	{.name="led_strip_lit",    .kind=ZTACX_VALUE_BOOL }
};

#if CONFIG_SHELL
static int cmd_ztacx_led_strip(const struct shell *shell, size_t argc, char **argv);
#endif
static void ztacx_led_strip_refresh(struct k_work *work);
static void ztacx_led_strip_off(struct ztacx_led_strip_context *context);
static void ztacx_led_strip_cursor_onchange(struct k_work *work);
static void ztacx_led_strip_color_onchange(struct k_work *work);

#define REFRESH_INTERVAL K_MSEC(100)

static struct led_rgb led_strip_pixels[STRIP_NUM_PIXELS];

#define RGB(_r, _g, _b) { .r = (_r), .g = (_g), .b = (_b) }

// TODO refactor as a map of name->led_rgb
static const struct led_rgb colors[] = {
	RGB(0x20, 0x00, 0x00), /* red */
	RGB(0x00, 0x20, 0x00), /* green */
	RGB(0x00, 0x00, 0x20), /* blue */
	RGB(0x20, 0x10, 0x00), /* orange */
	RGB(0x10, 0x20, 0x00), /* yellow */
	RGB(0x20, 0x20, 0x20), /* white */
};

int color_parse(const char *c, struct led_rgb *color_r)
{
	if (!color_r) return -1;
	if (c[0]=='#') {
		unsigned long rgb=strtoul(c+1, NULL, 16);
		color_r->r=(rgb>>16)&0xFF;
		color_r->g=(rgb>> 8)&0xFF;
		color_r->b=(rgb    )&0xFF;
	}
	else if (strncmp(c, "rgb:", 4)==0) {
		do {
			c++;
			color_r->r=atoi(c);
			c = strchr(c,',');
			if (!c) break;
			c++;
			color_r->g=atoi(c);
			c = strchr(c,',');
			if (!c) break;
			c++;
			color_r->b=atoi(c);
		} while (0);
	}
	else if (strcmp(c, "red") == 0) {
		*color_r = colors[0];
	}
	else if (strcmp(c, "green") == 0) {
		*color_r = colors[1];
	}
	else if (strcmp(c, "blue") == 0) {
		*color_r = colors[2];
	}
	else if (strcmp(c, "orange") == 0) {
		*color_r = colors[3];
	}
	else if (strcmp(c, "yellow") == 0) {
		*color_r = colors[4];
	}
	else if (strcmp(c, "white") == 0) {
		*color_r = colors[5];
	}
	else {
		return -1;
	}
	return 0;
}


static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);

// note: only supports one instance (see led.c if multi instances are needed)
struct ztacx_led_strip_context ztacx_led_strip_context = {
	.dev = strip,
	.pixels = led_strip_pixels,
	.settings = ztacx_led_strip_settings,
	.settings_count = ARRAY_SIZE(ztacx_led_strip_settings),
	.values = ztacx_led_strip_values,
	.values_count = ARRAY_SIZE(ztacx_led_strip_values)
};

int ztacx_led_strip_init(struct ztacx_leaf *leaf)
{
	LOG_INF("ztacx_led_strip_init");
	struct ztacx_led_strip_context *context = (struct ztacx_led_strip_context *)leaf->context;
	if (!context) {
		LOG_INF("Use default led string context");
		context = &ztacx_led_strip_context;
	}

#if CONFIG_ZTACX_LEAF_SETTINGS
	if (context->settings && context->settings_count) {
		ztacx_settings_register(context->settings, context->settings_count);
	}
#endif
	if (context->values && context->values_count) {
		ztacx_variables_register(context->values, context->values_count);
	}

	if (!device_is_ready(context->dev)) {
		LOG_ERR("No led_strip device");
		return -ENODEV;
	}

	k_work_init_delayable(&(context->refresh), ztacx_led_strip_refresh);
	k_work_init(&led_strip_cursor_onchange, ztacx_led_strip_cursor_onchange);
	k_work_init(&led_strip_color_onchange, ztacx_led_strip_color_onchange);
	ztacx_led_strip_values[VALUE_CURSOR].on_change = &led_strip_cursor_onchange;
	ztacx_led_strip_values[VALUE_COLOR].on_change = &led_strip_color_onchange;
	LOG_INF("led strip change handlers=%p %p", &led_strip_cursor_onchange, &led_strip_color_onchange);

#if CONFIG_SHELL
	ztacx_shell_cmd_register(((struct shell_static_entry){
				.syntax=leaf->name,
				.help="Control ztacx led-string",
				.handler=&cmd_ztacx_led_strip
				}));
#endif

	LOG_INF("LED string ready, %s (i2s) with %d pixels", context->dev->name, STRIP_NUM_PIXELS);

	return 0;
}

static void ztacx_led_strip_clear(struct ztacx_led_strip_context *context)
{
	memset(&context->pixels[0], 0x00, sizeof(struct led_rgb)*STRIP_NUM_PIXELS);
}

static void ztacx_led_strip_set(struct ztacx_led_strip_context *context, int pos, const struct led_rgb *color)
{
	//LOG_INF("ztacx_led_strip_set(%d, [%d,%d,%d])", pos, (int)color->r, (int)color->g, (int)color->b);

	if ((pos < -1) || (pos >= STRIP_NUM_PIXELS)) {
		LOG_ERR("Invalid pixel position %d", pos);
		return;
	}
	if (pos==-1) {
		for (int p=0; p<STRIP_NUM_PIXELS; p++) {
			context->pixels[pos].r = color->r;
			context->pixels[pos].g = color->g;
			context->pixels[pos].b = color->b;
		}
	}
	else {
		context->pixels[pos].r = color->r;
		context->pixels[pos].g = color->g;
		context->pixels[pos].b = color->b;
	}
}

static int ztacx_led_strip_show(struct ztacx_led_strip_context *context)
{
	int rc = led_strip_update_rgb(context->dev, context->pixels, STRIP_NUM_PIXELS);
	if (rc != 0) {
		LOG_ERR("LED strip update failed");
	}
	return rc;
}

static void ztacx_led_strip_off(struct ztacx_led_strip_context *context)
{
	LOG_INF("");
	ztacx_led_strip_clear(context);
	ztacx_led_strip_show(context);
}

int ztacx_led_strip_start(struct ztacx_leaf *leaf)
{
	struct ztacx_led_strip_context *context = leaf->context;

	k_work_schedule(&context->refresh, K_SECONDS(1));
	context->cursor = ztacx_variable_value_get_uint16(&(ztacx_led_strip_values[VALUE_CURSOR]));

	// test pattern at startup
	for (int i=ARRAY_SIZE(colors); i >= 0; i--) {
		memcpy(&context->pixels[0], &colors[i-1], sizeof(struct led_rgb));
		int rc = led_strip_update_rgb(context->dev, context->pixels, STRIP_NUM_PIXELS);
		if (rc) {
			LOG_ERR("couldn't update strip: %d", rc);
		}
		k_sleep(K_MSEC(100));
	}
	ztacx_led_strip_off(context);

	return 0;
}

int ztacx_led_strip_stop(struct ztacx_leaf *leaf)
{
	struct ztacx_led_strip_context *context = leaf->context;
	k_work_cancel_delayable(&context->refresh);
	ztacx_led_strip_off(context);
	return 0;
}

int ztacx_led_strip_pre_sleep(struct ztacx_leaf *leaf)
{
	struct ztacx_led_strip_context *context = leaf->context;
	ztacx_led_strip_off(context);

	return 0;
}

int ztacx_led_strip_post_sleep(struct ztacx_leaf *leaf)
{
	return 0;
}

static void ztacx_led_strip_refresh(struct k_work *work)
{
	//TODO does it make sense to support multi instances?  see led class if sso
	struct ztacx_led_strip_context *context = &ztacx_led_strip_context;

#if CONFIG_ZTACX_LED_STRIP_USE_TEST_PATTERN
	// for testing a
	static int pos = 0;
	static int color = 0;
	ztacx_led_strip_clear(context);
	ztacx_led_strip_set(context, pos++, colors+color);
	if (pos >= STRIP_NUM_PIXELS) {
		pos=0;
		color++;
		if (color >= ARRAY_SIZE(colors)) {
			color = 0;
		}
	}
#endif
	ztacx_led_strip_show(context);
	k_work_schedule(&context->refresh, REFRESH_INTERVAL);
}

static void ztacx_led_strip_cursor_onchange(struct k_work *work)
{
	LOG_ERR("");
	//TODO does it make sense to support multi instances?  see led class if sso
	struct ztacx_led_strip_context *context = &ztacx_led_strip_context;
	context->cursor = ztacx_variable_value_get_uint16(&(ztacx_led_strip_values[VALUE_CURSOR]));
}

static void ztacx_led_strip_color_onchange(struct k_work *work)
{
	LOG_INF("");
	//TODO does it make sense to support multi instances?  see led class if sso
	struct ztacx_led_strip_context *context = &ztacx_led_strip_context;
	const char *c = ztacx_led_strip_values[VALUE_COLOR].value.val_string;
	struct led_rgb color=RGB(0,0,0);
	LOG_INF("cursor=%d color=%s", context->cursor, c);

	if (c==NULL) {
		return;
	}
	if (color_parse(c, &color) != 0) {
		LOG_ERR("Cannot parse color nanme [%s]", c);
		return;
	}
	ztacx_led_strip_set(context, context->cursor, &color);
}

#if CONFIG_SHELL
static int cmd_ztacx_led_strip(const struct shell *shell, size_t argc, char **argv)
{
	if (argc < 1) {
		return -ENODEV;
	}

	struct ztacx_led_strip_context *context = &ztacx_led_strip_context;

	if ((argc > 1) && (strcmp(argv[1], "on")==0)) {
		k_work_reschedule(&context->refresh, K_NO_WAIT);
	}
	else if ((argc > 1) && (strcmp(argv[1], "off")==0)) {
		k_work_cancel_delayable(&context->refresh);
		ztacx_led_strip_off(context);
	}
	else if ((argc > 3) && (strcmp(argv[1], "set")==0)) {
		struct led_rgb color;
		int pos;
		if (strcmp(argv[2], "all")==0) {
			pos = -1;
		}
		else {
			pos = atoi(argv[2]);
			if ((pos < 0) || (pos > STRIP_NUM_PIXELS))
			{
				// invalid index, just use zero (lazy)
				pos=0;
			}
		}
		if (color_parse(argv[3], &color) == 0) {
			if (pos < 0) {
				for (int p=0; p<STRIP_NUM_PIXELS;p++) {
					ztacx_led_strip_set(context, p, &color);
				}
			}
			else {
				ztacx_led_strip_set(context, pos, &color);
			}
			ztacx_led_strip_show(context);
		}
	}
	else if ((argc > 5) && (strcmp(argv[1], "rgb")==0)) {
		int pos;
		if (strcmp(argv[2], "all")==0) {
			pos = -1;
		}
		else {
			pos = atoi(argv[2]);
			if ((pos < 0) || (pos > STRIP_NUM_PIXELS))
			{
				// invalid index, just use zero (lazy)
				pos=0;
			}
		}
		struct led_rgb color = RGB(atoi(argv[3]), atoi(argv[4]), atoi(argv[5]));
		if (pos < 0) {
			for (int p=0; p<STRIP_NUM_PIXELS;p++) {
				ztacx_led_strip_set(context, p, &color);
			}
		}
		else {
			ztacx_led_strip_set(context, pos, &color);
		}
		ztacx_led_strip_show(context);
	}

	return(0);
}
#endif
