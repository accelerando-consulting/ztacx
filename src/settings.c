#include <zephyr.h>
#include <init.h>
#include <stdio.h>
#include <stdlib.h>
#include <settings/settings.h>
#include <drivers/hwinfo.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/conn.h>
#include <bluetooth/uuid.h>
#include <bluetooth/bluetooth.h>
#include <shell/shell.h>

#ifdef CONFIG_MCUMGR_CMD_FS_MGMT
#include <device.h>
#include <fs/fs.h>
#include "fs_mgmt/fs_mgmt.h"
#include <fs/littlefs.h>
#endif

#ifdef CONFIG_MCUMGR_CMD_OS_MGMT
#include "os_mgmt/os_mgmt.h"
#endif

#ifdef CONFIG_MCUMGR_CMD_IMG_MGMT
#include "img_mgmt/img_mgmt.h"
#endif

#ifdef CONFIG_MCUMGR_CMD_STAT_MGMT
#include "stat_mgmt/stat_mgmt.h"
#endif

#ifdef CONFIG_MCUMGR_CMD_SHELL_MGMT
#include "shell_mgmt/shell_mgmt.h"
#endif

#ifdef CONFIG_MCUMGR_CMD_FS_MGMT
#include "fs_mgmt/fs_mgmt.h"
#endif

#include "../include/settings.h"

#if CONFIG_STATS
#include <stats/stats.h>

/* Define an example stats group; approximates seconds since boot. */
STATS_SECT_START(app_stats)
STATS_SECT_ENTRY(ticks)
STATS_SECT_END;

/* Assign a name to the `ticks` stat. */
STATS_NAME_START(app_stats)
STATS_NAME(app_stats, ticks)
STATS_NAME_END(app_stats);

/* Define an instance of the stats group. */
STATS_SECT_DECL(app_stats) app_stats;
#endif

#ifdef CONFIG_MCUMGR_CMD_FS_MGMT
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(cstorage);
static struct fs_mount_t littlefs_mnt = {
	.type = FS_LITTLEFS,
	.fs_data = &cstorage,
	.storage_dev = (void *)FLASH_AREA_ID(storage),
	.mnt_point = "/lfs1"
};
#endif

#ifdef CONFIG_SETTINGS_RUNTIME
int settings_runtime_load(void)
{
	LOG_INF("");

#if defined(CONFIG_BT_DIS_SETTINGS)
	settings_runtime_set("bt/dis/model",
			     CONFIG_BT_DIS_MODEL,
			     sizeof(CONFIG_BT_DIS_MODEL));
	settings_runtime_set("bt/dis/manuf",
			     CONFIG_BT_DIS_MANUF,
			     sizeof(CONFIG_BT_DIS_MANUF));

	settings_runtime_set("bt/dis/serial",
			     device_id_short,
			     strlen(device_id_short));

#if defined(CONFIG_BT_DIS_SW_REV)
	char buf[20];
	snprintf(buf, sizeof(buf), "%s (build %d)", CONFIG_BT_DIS_SW_REV_STR, BUILD_NUMBER);
	settings_runtime_set("bt/dis/sw", buf, strlen(buf));
#endif
#if defined(CONFIG_BT_DIS_FW_REV)
	settings_runtime_set("bt/dis/fw",
			     CONFIG_BT_DIS_FW_REV_STR,
			     sizeof(CONFIG_BT_DIS_FW_REV_STR));
#endif
#if defined(CONFIG_BT_DIS_HW_REV)
	settings_runtime_set("bt/dis/hw",
			     CONFIG_BT_DIS_HW_REV_STR,
			     sizeof(CONFIG_BT_DIS_HW_REV_STR));
#endif
#endif
	return 0;
}
#endif

int settings_load_after_bluetooth_ready()
{
	LOG_INF("load settings (after BT)");

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		LOG_DBG("load flash settings");
		settings_load();
	}

#ifdef CONFIG_SETTINGS_RUNTIME	
	LOG_DBG("load runtime settings");
	settings_runtime_load();
#endif
	
	LOG_INF("Initial settings:");
	for (int i=0; string_settings[i].name; i++) {
		LOG_INF("    %s=[%s]", log_strdup(string_settings[i].name), log_strdup(string_settings[i].value));
	}
	for (int i=0; uint16_settings[i].name; i++) {
		LOG_INF("    %s=[%u]", log_strdup(uint16_settings[i].name), (unsigned int)*uint16_settings[i].value);
	}
	for (int i=0; uint64_settings[i].name; i++) {
		LOG_INF("    %s=[%llu]", log_strdup(uint64_settings[i].name), (unsigned long long)*uint64_settings[i].value);
	}

	return 0;
}

static int settings_handle_set(const char *name, size_t len, settings_read_cb read_cb,
		  void *cb_arg)
{
	const char *next;
	size_t next_len;
	int rc = 0;

	LOG_INF("name=%s len=%d\n", log_strdup(name), (int)len);

	for (struct setting_string *s = string_settings; s->name; s++) {
		if (settings_name_steq(name, s->name, &next) && !next) {
			if (len >= s->size) {
				return -EINVAL;
			}
			rc = read_cb(cb_arg, s->value, s->size);
			LOG_INF("Loaded %s [%s]", log_strdup(s->name), log_strdup(s->value));
			s->value[len]='\0';
			return 0;
		}
	}

	for (struct setting_uint16 *s = uint16_settings; s->name; s++) {
		if (settings_name_steq(name, s->name, &next) && !next) {
			if (len != sizeof(*s->value)) {
				return -EINVAL;
			}
			rc = read_cb(cb_arg, s->value, sizeof(*s->value));
			LOG_INF("Loaded %s 0x%04x", log_strdup(s->name), *s->value);
			if (*s->value > s->max) {
				LOG_WRN("Value 0x%04x exceeds max, reducing to 0x%04x",
					*s->value, s->max);
				*s->value=s->max;
			}
			if (s->value == &mode_setting) {
				char buf[80];
				LOG_INF("  mode coerced to 0x%04x: %s", *s->value, log_strdup(get_mode_desc(buf, sizeof(buf))));
			}
			return 0;
		}
	}

	for (struct setting_uint64 *s = uint64_settings; s->name; s++) {
		if (settings_name_steq(name, s->name, &next) && !next) {
			if (len != sizeof(*s->value)) {
				return -EINVAL;
			}
			rc = read_cb(cb_arg, s->value, sizeof(*s->value));
			LOG_INF("Loaded %s %llu", log_strdup(s->name), *s->value);
			return 0;
		}
	}

	LOG_WRN("Unhandled setting %s", log_strdup(name));

	next_len = settings_name_next(name, &next);

	if (!next) {
		LOG_ERR("returing ENOENT");
		return -ENOENT;
	}

	return 0;
}

static int settings_handle_commit(void)
{
	LOG_INF("Application settings load completed.");
	return 0;
}

static int settings_handle_export(int (*cb)(const char *name,
			       const void *value, size_t val_len))
{
	LOG_INF("export keys under app handler\n");
	for (struct setting_string *s=string_settings; s->name; s++) {
		LOG_INF("string %s %s", log_strdup(s->name), log_strdup(s->value));
		(void)cb(s->name, s->value, s->size);
	}
	for (struct setting_uint16 *s=uint16_settings; s->name; s++) {
		LOG_INF("uint16 %s %d", log_strdup(s->name), (int)*s->value);
		(void)cb(s->name, s->value, sizeof(uint16_t));
	}
	for (struct setting_uint64 *s=uint64_settings; s->name; s++) {
		LOG_INF("uint64 %s %qu", log_strdup(s->name), *s->value);
		(void)cb(s->name, s->value, sizeof(uint64_t));
	}

	return 0;
}

#if 0
static int settings_handle_get(const char *key, char *val, int val_len_max) 
{
	LOG_INF("name=%s len=%d\n", log_strdup(key), (int)val_len_max);
	return 0;
}
#endif

static struct settings_handler settings_handler = {
		.name = "app",
		//.h_get = settings_handle_get,
		.h_set = settings_handle_set,
		.h_commit = settings_handle_commit,
		.h_export = settings_handle_export
};


int settings_setup()
{
	LOG_DBG("");
	memset(code_values, 0, sizeof(code_values));

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_subsys_init();
		settings_register(&settings_handler);
	}
	// settings will get loaded after bluetooth is ready

#if CONFIG_STATS
	int stats_err = STATS_INIT_AND_REG(app_stats, STATS_SIZE_32, "app_stats");
	if (stats_err < 0) {
		LOG_ERR("Error initializing stats system [%d]", stats_err);
	}
#endif

	/* Register the built-in mcumgr command handlers. */
#ifdef CONFIG_MCUMGR_CMD_FS_MGMT
	int fs_err = fs_mount(&littlefs_mnt);
	if (fs_err < 0) {
		LOG_ERR("Error mounting littlefs [%d]", fs_err);
	}
	fs_mgmt_register_group();
#endif

#ifdef CONFIG_MCUMGR_CMD_OS_MGMT
	os_mgmt_register_group();
#endif
#ifdef CONFIG_MCUMGR_CMD_IMG_MGMT
	img_mgmt_register_group();
#endif
#ifdef CONFIG_MCUMGR_CMD_STAT_MGMT
	stat_mgmt_register_group();
#endif
#ifdef CONFIG_MCUMGR_CMD_SHELL_MGMT
	shell_mgmt_register_group();
#endif
#ifdef CONFIG_MCUMGR_CMD_FS_MGMT
	fs_mgmt_register_group();
#endif
	/*
	 * Get the device ID
	 */
	device_id_len = hwinfo_get_device_id(device_id, sizeof(device_id));
	char *p = device_id_str;
	for (int i=0;i<device_id_len;i++) {
		snprintk(p, 3, "%02x", (int)device_id[i]);
		p+=2;
	}
	if (strlen(device_id_str) < 4) {
		strcpy(device_id_short, device_id_str);
	}
		else {
		strcpy(device_id_short, device_id_str + strlen(device_id_str) - 4);
	}
	LOG_WRN("Hardware id is '%s' [%s]", log_strdup(device_id_str), log_strdup(device_id_short));

	LOG_DBG("done");
	return 0;

}

int settings_loop(void) {
#if CONFIG_STATS
	STATS_INC(app_stats, ticks);
#endif
	return 0;
}

static int _print_setting(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg, void *param)
{
	const struct shell *shell = param;
	char buf[256];
	if (len>=sizeof(buf)) len=sizeof(buf)-1; // clip value to buffer

	shell_fprintf(shell, SHELL_NORMAL, "    %s len=%d ", key, len);
	read_cb(cb_arg, buf, len);
	shell_hexdump(shell, buf, len);
	return 0;

}


/** 
 * Implementation of the "app settings" CLI component
 */
int cmd_app_settings(const struct shell *shell, size_t argc, char **argv)
{
	LOG_DBG("");
	if (argc <= 1) {
		settings_load();
		return 0;
	}
	LOG_INF("app_settings argc=%d argv[1]=%s", argc, log_strdup(argv[1]));
	if (strcmp(argv[1], "setup")==0) {
		LOG_INF("setup");
		settings_setup();
	}
	else if ((argc >= 2) && (strcmp(argv[1], "show")==0)) {
		int i;
		char buf[32];
		for (i=0; string_settings[i].name; i++) {
			shell_print(shell, "\t%s=[%s]", string_settings[i].name, string_settings[i].value);
		}
		for (i=0; uint16_settings[i].name; i++) {
			shell_print(shell, "\t%s=[%u]", uint16_settings[i].name, (unsigned int)*uint16_settings[i].value);
		}
		for (i=0; uint64_settings[i].name; i++) {
			shell_print(shell, "\t%s=[%llu]", uint64_settings[i].name, (unsigned long long)*uint64_settings[i].value);
		}
	}
	else if ((argc >= 2) && (strcmp(argv[1], "list")==0)) {
		LOG_DBG("settings_list");
		settings_load_subtree_direct((argc>2)?argv[2]:"app", _print_setting, (void *)shell);
	}
	else if ((argc == 2) && (strcmp(argv[1], "load")==0)) {
		LOG_INF("settings_load");
		settings_load();
	}
	else if ((argc == 3) && (strcmp(argv[1], "load")==0)) {
		LOG_INF("settings_load_subtree %s", argv[2]);
		settings_load_subtree(log_strdup(argv[2]));
	}
	else if ((argc == 2) && (strcmp(argv[1], "save")== 0)) {
		LOG_INF("settings_save");
		settings_save();
	}
	else if (strcmp(argv[1], "set-int")==0) {
		char key[64];
		uint16_t value = (uint16_t) atoi(argv[3]);
		snprintf(key, sizeof(key), "app/%s", argv[2]);
		for (int i=0; uint16_settings[i].name; i++) {
			if (strcmp(uint16_settings[i].name, argv[2])==0) {
				uint16_settings[i].value[0]=value;
				break;
			}
		}
		LOG_INF("settings set-int k=[%s] v=%d", log_strdup(key), (int)value);
		int err = settings_save_one(key, (const void *)&value,
				       sizeof(value));
		if (err != 0) {
			LOG_ERR("Settings save failed: %d", err);
		}
	}
	else if ((argc >= 4) && (strcmp(argv[1], "set-str")==0)) {
		char key[64];
		const char *value = argv[3];
		snprintf(key, sizeof(key), "app/%s", argv[2]);
		LOG_INF("settings set-str k=[%s] v=[%s]", log_strdup(key), log_strdup(value));
		int err = settings_save_one(key, (const void *)value, strlen(value)+1);
		if (err != 0) {
			LOG_ERR("Settings save failed: %d", err);
		}
	}
#if CONFIG_APP_RETENTION
	else if (strcmp(argv[1], "unretain")==0) {
		retained_erase();
	}
#endif
	else {
		shell_print(shell, "app settings <setup|list|load|save|set-int|set-str>\n");
	}

	return 0;

}
