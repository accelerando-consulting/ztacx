#include "ztacx.h"
#include "ztacx_settings.h"
#include <zephyr/settings/settings.h>

#ifdef CONFIG_MCUMGR_CMD_FS_MGMT
#include <zephyr/fs/fs.h>
#include "fs_mgmt/fs_mgmt.h"
#include <zephyr/fs/littlefs.h>
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

#if CONFIG_STATS
#include <zephyr/stats/stats.h>

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

sys_slist_t ztacx_settings;
SYS_MUTEX_DEFINE(ztacx_settings_mutex);

#if CONFIG_SETTINGS_RUNTIME
int ztacx_settings_runtime_load(void)
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
	char buf[32];
#ifdef CONFIG_APP_BUILD_NUMBER
	snprintf(buf, sizeof(buf), "%s (build %d)", CONFIG_BT_DIS_SW_REV_STR, CONFIG_APP_BUILD_NUMBER);
#else
	snprintf(buf, sizeof(buf), "%s", CONFIG_BT_DIS_SW_REV_STR);
#endif
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

int ztacx_settings_load()
{
	LOG_INF("load settings");

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		LOG_INF("load flash settings");
		settings_load();
	}

#if CONFIG_SETTINGS_RUNTIME
	LOG_INF("load runtime settings (spragged)");
	//settings_runtime_load();
#endif

	LOG_INF("Settings ready:");
	sys_slist_t *list = &ztacx_settings;
	struct ztacx_variable *s;
	char desc[132];

	SYS_SLIST_FOR_EACH_CONTAINER(list, s, node) {
		ztacx_variable_describe(desc,sizeof(desc), s);
		LOG_INF("   %s", desc);
	}
	return 0;
}


int ztacx_settings_add(const char *name,
		       enum ztacx_value_kind kind,
		       void *value)
{
	int err;

	struct ztacx_variable *setting = calloc(1, sizeof(struct ztacx_variable));
	if (!setting) return -ENOMEM;
	strncpy(setting->name, name, sizeof(setting->name));
	setting->kind = kind;
	err = ztacx_variable_value_set(setting, value);
	if (err < 0) {
		LOG_ERR("ztacx_settings_add: error %d", err);
		return err;
	}
	while (sys_mutex_lock(&ztacx_settings_mutex, K_MSEC(500)) != 0) {
		LOG_WRN("ztacx settings mutex is held too long");
	}
	sys_slist_append(&ztacx_settings, &setting->node);
	sys_mutex_unlock(&ztacx_settings_mutex);
	return 0;
}

int ztacx_settings_register(struct ztacx_variable *s, int count)
{
	LOG_INF("%d", count);
	return ztacx_values_register(&ztacx_settings, &ztacx_settings_mutex, s, count);
}

static int settings_handle_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	LOG_INF("name=%s len=%d", name, (int)len);

	const char *next;
	size_t next_len;
	int rc = 0;
	//char desc[132];
	sys_slist_t *list = &ztacx_settings;
	struct ztacx_variable *s;
	SYS_SLIST_FOR_EACH_CONTAINER(list, s, node) {
		if (settings_name_steq(name, s->name, &next) && !next) {
			//LOG_INF("Found setting record of kind %d for %s", s->kind, name);
			switch (s->kind) {
			case ZTACX_VALUE_STRING:
				if (s->value.val_string==NULL) {
					//LOG_INF("Allocate string space %d", len+1);
					
					s->value.val_string=calloc(1,len+1);
				}
				else if (len != strlen(s->value.val_string)) {
					//LOG_INF("Reallocate string space to fit %d", len+1);
					s->value.val_string = realloc(s->value.val_string, len+1);
				}
				if (s->value.val_string==NULL) {
					LOG_ERR("Allocation failed");
					return -ENOMEM;
				}
				read_cb(cb_arg, s->value.val_string, len);
				s->value.val_string[len]='\0';
				break;
			case ZTACX_VALUE_BOOL:
				if (len != sizeof(s->value.val_bool)) {
					LOG_ERR("Incorrect size %s:%d",name,len);
					return -EINVAL;
				}
				read_cb(cb_arg, &s->value.val_bool, len);
				break;
			case ZTACX_VALUE_BYTE:
				if (len != sizeof(s->value.val_byte)) {
					LOG_ERR("Incorrect size %s:%d",name,len);
					return -EINVAL;
				}
				read_cb(cb_arg, &s->value.val_byte, len);
				break;
			case ZTACX_VALUE_UINT16:
				if (len != sizeof(s->value.val_uint16)) {
					LOG_ERR("Incorrect size %s:%d",name,len);
					return -EINVAL;
				}
				read_cb(cb_arg, &s->value.val_uint16, len);
				break;
			case ZTACX_VALUE_INT16:
				if (len != sizeof(s->value.val_int16)) {
					LOG_ERR("Incorrect size %s:%d",name,len);
					return -EINVAL;
				}
				read_cb(cb_arg, &s->value.val_int16, len);
				break;
			case ZTACX_VALUE_INT32:
				if (len != sizeof(s->value.val_int32)) {
					LOG_ERR("Incorrect size %s:%d",name,len);
					return -EINVAL;
				}
				read_cb(cb_arg, &s->value.val_int32, len);
				break;
			case ZTACX_VALUE_INT64:
				if (len != sizeof(s->value.val_int64)) {
					LOG_ERR("Incorrect size %s:%d",name,len);
					return -EINVAL;
				}
				read_cb(cb_arg, &s->value.val_int64, len);
				break;
			default:
				LOG_ERR("Unhandled setting type %s:%d",name,(int)s->kind);
				rc = -EINVAL;
				break;
			}
			/*
			if (rc == 0) {
				ztacx_variable_describe(desc, sizeof(desc), s);
				LOG_INF("Loaded %s", desc);
			}
			*/
			return rc;
		}
	}

	//NOCOMMIT LOG_WRN("Unhandled setting %s", name);

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

	sys_slist_t *list = &ztacx_settings;
	struct ztacx_variable *s;
	SYS_SLIST_FOR_EACH_CONTAINER(list, s, node) {
		switch (s->kind) {
		case ZTACX_VALUE_STRING:
			(void)cb(s->name, s->value.val_string, strlen(s->value.val_string));
			break;
		case ZTACX_VALUE_BOOL:
			(void)cb(s->name, &s->value.val_bool, sizeof(s->value.val_bool));
			break;
		case ZTACX_VALUE_BYTE:
			(void)cb(s->name, &s->value.val_byte, sizeof(s->value.val_byte));
			break;
		case ZTACX_VALUE_UINT16:
			(void)cb(s->name, &s->value.val_uint16, sizeof(s->value.val_uint16));
			break;
		case ZTACX_VALUE_INT16:
			(void)cb(s->name, &s->value.val_int16, sizeof(s->value.val_int16));
			break;
		case ZTACX_VALUE_INT32:
			(void)cb(s->name, &s->value.val_int32, sizeof(s->value.val_int32));
			break;
		case ZTACX_VALUE_INT64:
			(void)cb(s->name, &s->value.val_int64, sizeof(s->value.val_int64));
			break;
		default:
			LOG_WRN("Unhandled type for setting %s", s->name);
		}
	}

	return 0;
}

#if 0
static int settings_handle_get(const char *key, char *val, int val_len_max)
{
	LOG_INF("name=%s len=%d\n", key, (int)val_len_max);
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



#if CONFIG_SHELL
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
int cmd_ztacx_settings(const struct shell *shell, size_t argc, char **argv)
{
	LOG_INF("cmd_ztacx_settings argc=%d argv[1]=%s", argc, (argc>1)?argv[1]:"");

	if (argc <= 1) {
		LOG_INF("settings load");
		settings_load();
		return 0;
	}
	if (strcmp(argv[1], "init")==0) {
		LOG_INF("settings init");
		ztacx_settings_init(ztacx_leaf_get("settings"));
	}
	else if ((argc >= 2) && (strcmp(argv[1], "show")==0)) {
		LOG_INF("settings show");
		sys_slist_t *list = &ztacx_settings;
		struct ztacx_variable *s;
		char desc[132];

		SYS_SLIST_FOR_EACH_CONTAINER(list, s, node) {
			ztacx_variable_describe(desc,sizeof(desc), s);
			LOG_INF("setting desc %s", desc);
			shell_print(shell, "%s", desc);
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
		settings_load_subtree(argv[2]);
	}
	else if ((argc == 2) && (strcmp(argv[1], "save")== 0)) {
		LOG_INF("settings_save");
		settings_save();
	}
	else if ((argc>=4) && strcmp(argv[1], "set")==0) {
		int err;
		const char *value = argv[3];
		struct ztacx_variable *s= ztacx_setting_find(argv[2]);
		if (!s) {
			shell_print(shell, "No setting named '%s'", argv[2]);
			return -ENOENT;
		}

		err = ztacx_setting_set(s, value);
		if (err != 0) {
			shell_print(shell, "Setting update failed for '%s': error %d", argv[2], err);
			return -EINVAL;
		}
		else {
			char desc[132];
			ztacx_variable_describe(desc,sizeof(desc), s);
			shell_print(shell, "updated: %s", desc);
		}
	}
	else if ((argc==3) && strcmp(argv[1], "get")==0) {

		struct ztacx_variable *s= ztacx_setting_find(argv[2]);
		if (!s) {
			shell_print(shell, "No setting named '%s'", argv[2]);
			return -ENOENT;
		}
		char desc[132];
		ztacx_variable_describe(desc,sizeof(desc), s);
		shell_print(shell, "%s", desc);
	}
	else if (strcmp(argv[1], "unretain")==0) {
#if CONFIG_APP_RETENTION
		retained_erase();
#endif
	}
	else {
		shell_print(shell, "app settings <setup|list|load|save|set|unretain>\n");
	}

	return 0;

}
#endif


int ztacx_settings_init(struct ztacx_leaf *leaf)
{
	LOG_INF("");

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_subsys_init();
		settings_register(&settings_handler);
	}

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

#if CONFIG_SHELL
	ztacx_shell_cmd_register(((struct shell_static_entry){
				.syntax="settings",
				.help="View and update ztacx settings",
				.handler=&cmd_ztacx_settings
				}));
#endif


	LOG_INF("done");
	return 0;
}

int ztacx_settings_start(struct ztacx_leaf *leaf)
{
	LOG_INF("");

	ztacx_settings_load();
	return 0;
}

int settings_loop(void) {
#if CONFIG_STATS
	STATS_INC(app_stats, ticks);
#endif
	return 0;
}


/**
 * Look up a setting by name from the settings list
 */
struct ztacx_variable *ztacx_setting_find(const char *name)
{
	sys_slist_t *list = &ztacx_settings;
	struct ztacx_variable *s;
	SYS_SLIST_FOR_EACH_CONTAINER(list, s, node) {
		if (strcmp(s->name, name) == 0) {
			return s;
		}
	}
	LOG_WRN("no setting named '%s' found", name);
	return NULL;
}



/**
 * Store a value (from string) into a ztacx_setting
 */
int ztacx_setting_set(struct ztacx_variable *s, const char *value)
{
	if (!s) {
		return -EINVAL;
	}
	int err;
	char key[64];
	snprintf(key, sizeof(key), "app/%s", s->name);

	err = ztacx_variable_value_set_string(s, value);
	if (err != 0) {
		return err;
	}

	switch (s->kind) {
	case ZTACX_VALUE_STRING:
		err = settings_save_one(key, (const void *)s->value.val_string, strlen(s->value.val_string));
		break;
	case ZTACX_VALUE_BOOL:
		err = settings_save_one(key, (const void *)&s->value.val_bool, sizeof(s->value.val_bool));
		break;
	case ZTACX_VALUE_BYTE:
		err = settings_save_one(key, (const void *)&s->value.val_byte, sizeof(s->value.val_byte));
		break;
	case ZTACX_VALUE_UINT16:
		err = settings_save_one(key, (const void *)&s->value.val_uint16, sizeof(s->value.val_uint16));
		break;
	case ZTACX_VALUE_INT16:
		err = settings_save_one(key, (const void *)&s->value.val_int16, sizeof(s->value.val_int16));
		break;
	case ZTACX_VALUE_INT32:
		err = settings_save_one(key, (const void *)&s->value.val_int32, sizeof(s->value.val_int32));
		break;
	case ZTACX_VALUE_INT64:
		err = settings_save_one(key, (const void *)&s->value.val_int64, sizeof(s->value.val_int64));
		break;
	default:
		LOG_ERR("Unhandled setting type %d", (int)s->kind);
		return -EINVAL;
	}
	if (err != 0) {
		LOG_ERR("Settings save failed: %d", err);
	}
	char desc[132];
	ztacx_variable_describe(desc,sizeof(desc), s);
	LOG_INF("Saved %s", desc);
	return err;
}

void ztacx_settings_show()
{
	sys_slist_t *list = &ztacx_settings;
	struct ztacx_variable *s;
	char desc[132];
	LOG_INF("Configuration settings:");
	SYS_SLIST_FOR_EACH_CONTAINER(list, s, node) {
		ztacx_variable_describe(desc,sizeof(desc), s);
		LOG_INF("    %s", desc);
	}
}
