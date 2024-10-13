#include "ztacx.h"
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/drivers/hwinfo.h>

uint8_t device_id[16]="";
int device_id_len = 0;
char name_setting[21] = CONFIG_ZTACX_NAME_BASE; // max length of ble string

char device_id_str[35]="";
char device_id_short[7]="";

static sys_slist_t ztacx_classes;
static sys_slist_t ztacx_leaves;
static sys_slist_t ztacx_variables;

const char *ztacx_value_kind_names[ZTACX_VALUE_MAX] = {
	"ZTACX_VALUE_STRING",
	"ZTACX_VALUE_BOOL",
	"ZTACX_VALUE_BYTE",
	"ZTACX_VALUE_UINT16",
	"ZTACX_VALUE_INT16",
	"ZTACX_VALUE_INT32",
	"ZTACX_VALUE_INT64",
	"ZTACX_VALUE_EVENT",
};


static int ztacx_init(void) ;
SYS_INIT(ztacx_init, APPLICATION, ZTACX_INIT_PRIORITY);

bool ztacx_init_done = false;

SYS_MUTEX_DEFINE(ztacx_registry_mutex);
SYS_MUTEX_DEFINE(ztacx_variables_mutex);

#if CONFIG_SHELL
#include <zephyr/shell/shell.h>

#define MAX_CMD_CNT (20u)

/* buffer holding dynamicly created user commands */
static struct shell_static_entry dynamic_cmd_table[MAX_CMD_CNT];
/* commands counter */
static uint8_t dynamic_cmd_cnt;

static void ztacx_dynamic_cmd_get(size_t idx, struct shell_static_entry *entry);
int cmd_ztacx_boot(const struct shell *shell, size_t argc, char **argv);
int cmd_ztacx_init(const struct shell *shell, size_t argc, char **argv);
int cmd_ztacx_status(const struct shell *shell, size_t argc, char **argv);
int cmd_ztacx_stop(const struct shell *shell, size_t argc, char **argv);
int cmd_ztacx_start(const struct shell *shell, size_t argc, char **argv);
int cmd_ztacx_settings(const struct shell *shell, size_t argc, char **argv);
int cmd_ztacx_value(const struct shell *shell, size_t argc, char **argv);
struct ztacx_leaf *ztacx_leaf_get(const char *name);


static int cmd_dynamic_execute(const struct shell *shell,
			       size_t argc, char **argv)
{
	LOG_INF("");
	return 0;
}


SHELL_DYNAMIC_CMD_CREATE(m_sub_ztacx_set, ztacx_dynamic_cmd_get);
SHELL_STATIC_SUBCMD_SET_CREATE(
	m_sub_ztacx,
	SHELL_CMD_ARG(leaf, &m_sub_ztacx_set,
		"Access dynamic commands defined by ztacx leaves.", cmd_dynamic_execute, 2, 0),
	SHELL_CMD_ARG(boot, NULL,"Initialise the ztacx framework.", cmd_ztacx_boot,0,0),
	SHELL_CMD_ARG(status, NULL,"Show status of ztacx leaves.", cmd_ztacx_status,0,0),
	SHELL_CMD(init, NULL,"Initialise a leaf.", cmd_ztacx_init),
	SHELL_CMD(stop, NULL,"Stop a leaf.", cmd_ztacx_stop),
	SHELL_CMD(start, NULL,"Start a leaf.", cmd_ztacx_start),
#if CONFIG_ZTACX_LEAF_SETTINGS
	SHELL_CMD(setting, NULL,"Show/edit persistent settings.", cmd_ztacx_settings),
#endif
	SHELL_CMD(value, NULL,"Show/edit status of runtime variables.", cmd_ztacx_value),
	SHELL_SUBCMD_SET_END
	);
SHELL_CMD_REGISTER(ztacx, &m_sub_ztacx,
	   "Manage the ztacx framework and its leaves.", NULL);

#endif

static int ztacx_init(void)
{
	LOG_INF("ztacx_init");
	/*
	 * Get the device ID
	 */
#if CONFIG_HWINFO
	device_id_len = hwinfo_get_device_id(device_id, sizeof(device_id));
#else
	// CONFIG_HWINFO is not enabled, generate hardware ID 0x00000000
	memset(device_id,0,sizeof(device_id));
	device_id_len=4;
#endif
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

	LOG_INF("NOTICE Hardware id is '%s' [%s]", device_id_str, device_id_short);
	strncat(name_setting, "-", sizeof(name_setting)-1);
	strncat(name_setting, device_id_short, sizeof(name_setting)-1);

	if (sys_mutex_lock(&ztacx_registry_mutex, K_NO_WAIT) != 0) {
		LOG_ERR("ztacx registry mutex is held, that's impossible");
		return -EWOULDBLOCK;
	}
	sys_slist_init(&ztacx_classes);
	sys_slist_init(&ztacx_leaves);
	sys_mutex_unlock(&ztacx_registry_mutex);

	ztacx_init_done=true;
	LOG_INF("ztacx_init OK");
	return 0;
}


int ztacx_class_register(struct ztacx_leaf_class *class)
{
	LOG_INF("ztacx_class_register %s", class->name);
	while (sys_mutex_lock(&ztacx_registry_mutex, K_MSEC(100)) != 0) {
		LOG_WRN("ztacx registry mutex is held too long");
	}
	sys_slist_append(&ztacx_classes, &class->node);
	sys_mutex_unlock(&ztacx_registry_mutex);
	LOG_DBG("ztacx_class_register %s OK", class->name);
	return 0;
}

int ztacx_leaf_sys_init(struct ztacx_leaf *leaf)
{
	LOG_INF("ztacx_leaf_sys_init %s", leaf->name);
	int rc = 0;

	LOG_INF("NOTICE >INIT %s/%s", leaf->class->name,
		leaf->name);
	while (sys_mutex_lock(&ztacx_registry_mutex, K_MSEC(100)) != 0) {
		LOG_WRN("ztacx registry mutex is held too long");
	}
	sys_slist_append(&ztacx_leaves, &leaf->node);
	sys_mutex_unlock(&ztacx_registry_mutex);
	leaf->ready = leaf->running = false;
	if (leaf->class->cb->init) {
		rc = leaf->class->cb->init(leaf);

		if (rc == 0) {
			LOG_INF("NOTICE <READY %s", leaf->name);
			leaf->ready = true;
		}
		else {
			LOG_WRN("INIT RESULT %s rc=%d", leaf->name, rc);
		}
	}
	else {
		// no callback, but emit the same log messages as if there were
		LOG_INF("NOTICE <READY %s", leaf->name);
		leaf->ready = true;
	}
	return rc;
}

int ztacx_leaf_sys_start(struct ztacx_leaf *leaf)
{
	LOG_INF("ztacx_leaf_sys_start %s", leaf->name);
	int rc = 0;
	if (!leaf->ready) {
		return -ESRCH;
	}
	if (leaf->class->cb->start) {
		LOG_INF("NOTICE >START %s/%s",
			leaf->class->name, leaf->name);
		rc = leaf->class->cb->start(leaf);
		if (rc == 0) {
			leaf->running=true;
			LOG_INF("NOTICE <STARTED %s", leaf->name);
		}
		else {
			LOG_WRN("START RESULT %s rc=%d", leaf->name, rc);
		}
	}
	else {
		// no callback, but emit the same log messages as if there were
		LOG_INF("NOTICE >START %s/%s",
			leaf->class->name, leaf->name);
		leaf->running=true;
		LOG_INF("NOTICE <STARTED %s", leaf->name);
	}
	return rc;
}

#if CONFIG_SHELL
/* comparison function for sorting shell commands with qsort */



static int shell_cmd_cmp(const void *p_a, const void *p_b)
{
	return strcmp(((const struct shell_static_entry *)p_a)->syntax, ((const struct shell_static_entry *)p_b)->syntax);
}

int ztacx_shell_cmd_register(struct shell_static_entry entry)
{
	LOG_INF("ztacx_shell_cmd_register %s", entry.syntax);

	if (dynamic_cmd_cnt >= MAX_CMD_CNT) {
		LOG_ERR("Ztacx shell command table is full");
		return -ENOMEM;
	}
	dynamic_cmd_table[dynamic_cmd_cnt++]=entry;

	qsort(dynamic_cmd_table, dynamic_cmd_cnt,
	      sizeof(struct shell_static_entry), shell_cmd_cmp);

	return 0;
}

static void ztacx_dynamic_cmd_get(size_t idx, struct shell_static_entry *entry)
{
	//LOG_INF("%d", idx);
	if (idx < dynamic_cmd_cnt) {
		/* Dynamic command table must be sorted alphabetically to ensure
		 * correct CLI completion
		 */
		*entry = dynamic_cmd_table[idx];
	} else {
		/* if there are no more dynamic commands available syntax
		 * must be set to NULL.
		 */
		entry->syntax = NULL;
	}
}

int cmd_ztacx_status(const struct shell *shell, size_t argc, char **argv)
{
	LOG_DBG("");

	if (!ztacx_init_done) {
		shell_print(shell, "ztacx is not initialised");
		return 0;
	}
	shell_print(shell, "ztacx is initialised");

	sys_slist_t *list = &ztacx_leaves;
	struct ztacx_leaf *leaf;
	SYS_SLIST_FOR_EACH_CONTAINER(list, leaf, node) {
		shell_print(shell, "%s: %s %s", (leaf->name), leaf->ready?"READY":"FAILED", leaf->running?"RUNNING":"STOPPED");
	}

	return 0;
}

int cmd_ztacx_boot(const struct shell *shell, size_t argc, char **argv)
{
	LOG_DBG("");
	int rc  = ztacx_init();
	if (rc == 0) {
		shell_print(shell, "booted");
	}
	else {
		shell_print(shell, "failed");
	}

	return rc;
}

int cmd_ztacx_init(const struct shell *shell, size_t argc, char **argv)
{
	LOG_DBG("");
	struct ztacx_leaf *leaf = ztacx_leaf_get(argv[1]);

	int rc  = ztacx_leaf_sys_init(leaf);
	if (rc == 0) {
		shell_print(shell, "started");
	}
	else {
		shell_print(shell, "failed");
	}

	return rc;
}

int cmd_ztacx_start(const struct shell *shell, size_t argc, char **argv)
{
	LOG_DBG("");
	int rc  = ztacx_leaf_start(argv[1]);
	if (rc == 0) {
		shell_print(shell, "started");
	}
	else {
		shell_print(shell, "failed");
	}

	return rc;
}

int cmd_ztacx_stop(const struct shell *shell, size_t argc, char **argv)
{
	LOG_DBG("");
	int rc  = ztacx_leaf_stop(argv[1]);
	if (rc == 0) {
		shell_print(shell, "stopped");
	}
	else {
		shell_print(shell, "failed");
	}

	return rc;
}

/**
 * Implementation of the "ztacx value" CLI component
 */
int cmd_ztacx_value(const struct shell *shell, size_t argc, char **argv)
{
	LOG_INF("cmd_ztacx_value argc=%d argv[1]=%s", argc, (argv[1]));

	if ((argc == 1) ||
	    ((argc >= 2) && ((strcmp(argv[1], "list")==0) || (strcmp(argv[1], "show")==0)))) {
		sys_slist_t *list = &ztacx_variables;
		struct ztacx_variable *s;
		char desc[132];

		SYS_SLIST_FOR_EACH_CONTAINER(list, s, node) {
			ztacx_variable_describe(desc,sizeof(desc), s);
			shell_print(shell, "%s", desc);
		}
	}
	else if (strcmp(argv[1], "get")==0) {
		struct ztacx_variable *v= ztacx_variable_find(argv[2]);
		char desc[132];
		ztacx_variable_describe(desc,sizeof(desc), v);
		shell_print(shell, "%s", desc);
	}
	else if (strcmp(argv[1], "set")==0) {
		int err=0;
		const char *value = argv[3];
		struct ztacx_variable *v= ztacx_variable_find(argv[2]);
		if (!v) {
			shell_print(shell, "No value named '%s'", argv[2]);
			return -ENOENT;
		}

		err = ztacx_variable_value_set_string(v, value);
		if (err != 0) {
			shell_print(shell, "Value update failed for '%s'", argv[2]);
			return -EINVAL;
		}
	}
#if CONFIG_APP_RETENTION
	else if (strcmp(argv[1], "unretain")==0) {
		retained_erase();
	}
#endif
	else {
		shell_print(shell, "ztacx value <list|get|set|unretain>\n");
	}

	return 0;

}

#endif

void ztacx_variables_show()
{
	sys_slist_t *list = &ztacx_variables;
	struct ztacx_variable *s;
	char desc[132];
	LOG_INF("Current runtime values:");
	SYS_SLIST_FOR_EACH_CONTAINER(list, s, node) {
		ztacx_variable_describe(desc,sizeof(desc), s);
		LOG_INF("    %s", desc);
	}
}

struct ztacx_variable *ztacx_variables_copy(struct ztacx_variable *dst, const struct ztacx_variable *src, int count, const char *prefix)
{
	int size = count * sizeof(struct ztacx_variable);
	if (!dst) return NULL;

	memcpy(dst, src, size);

	if (prefix != NULL) {
		// prepend supplied prefix to each variable name
		for (int i=0; i<count; i++) {
			int prefix_len = strlen(prefix);
			int name_len = strlen(dst[i].name);
			if (name_len + prefix_len >= (CONFIG_ZTACX_VALUE_NAME_MAX)) {
				LOG_ERR("Name overflow for %s_ + %s",
					prefix, dst[i].name);
				continue;
			}
			memmove(dst[i].name+prefix_len+1, dst[i].name, name_len);
			memcpy(dst[i].name, prefix, prefix_len);
			dst[i].name[prefix_len]='_';
		}
	}

	return dst;
}

struct ztacx_variable *ztacx_variables_dup(const struct ztacx_variable *v, int count, const char *prefix)
{
	int size = count * sizeof(struct ztacx_variable);
	struct ztacx_variable *result = malloc(size);
	if (!result) return NULL;
	return ztacx_variables_copy(result, v, count, prefix);
}

struct ztacx_leaf *ztacx_leaf_get(const char *name)
{
	sys_slist_t *list = &ztacx_leaves;
	struct ztacx_leaf *leaf;

	SYS_SLIST_FOR_EACH_CONTAINER(list, leaf, node) {
		if (strcmp(leaf->name, name)==0) {
			return leaf;
		}
	}
	return NULL;
}

struct ztacx_leaf *ztacx_leaf_find(const char *class, void *compare, ztacx_leaf_find_cb_t cb)
{
	sys_slist_t *list = &ztacx_leaves;
	struct ztacx_leaf *leaf;

	SYS_SLIST_FOR_EACH_CONTAINER(list, leaf, node) {
		if (class && (strcmp(leaf->class->name, class) != 0)) {
			// does not match the class filter
			continue;
		}
		if (cb(leaf, compare) == 0) {
			return leaf;
		}
	}
	return NULL;
}

int ztacx_leaf_start(const char *name)
{
	struct ztacx_leaf *leaf = ztacx_leaf_get(name);
	if (!name) return -ENOENT;
	int rc=0;


	if (leaf->class->cb->start) {
		rc = leaf->class->cb->start(leaf);
	}
	if (rc == 0) leaf->running=true;
	return rc;
}

int ztacx_leaf_stop(const char *name)
{
	struct ztacx_leaf *leaf = ztacx_leaf_get(name);
	if (!name) return -ENOENT;
	int rc = 0;

	if (leaf->class->cb->stop) {
		rc = leaf->class->cb->stop(leaf);
	}
	if (rc == 0) leaf->running = false;
	return rc;
}

int ztacx_pre_sleep(void)
{
	sys_slist_t *list = &ztacx_leaves;
	struct ztacx_leaf *leaf;
	int rc = 0;

	SYS_SLIST_FOR_EACH_CONTAINER(list, leaf, node) {
		if (leaf->class->cb->pre_sleep) {
			int err = leaf->class->cb->pre_sleep(leaf);
			if (err != 0) {
				rc = err;
			}
		}
	}
	return rc;
}

int ztacx_post_sleep(void)
{
	sys_slist_t *list = &ztacx_leaves;
	struct ztacx_leaf *leaf;
	int rc = 0;

	SYS_SLIST_FOR_EACH_CONTAINER(list, leaf, node) {

		if (leaf->class->cb->post_sleep) {
			int err = leaf->class->cb->post_sleep(leaf);
			if (err != 0) {
				rc = err;
			}
		}
	}
	return rc;
}

bool ztacx_leaf_is_ready(const char *name)
{
	struct ztacx_leaf *leaf = ztacx_leaf_get(name);
	if (!name) return false;
	return leaf->ready;
}

bool ztacx_leaf_is_running(const char *name)
{
	struct ztacx_leaf *leaf = ztacx_leaf_get(name);
	if (!name) return false;
	return leaf->running;
}

int ztacx_values_register(sys_slist_t *list, struct sys_mutex *mutex, struct ztacx_variable *v, int count)
{
	while (sys_mutex_lock(mutex, K_MSEC(500)) != 0) {
		LOG_WRN("ztacx value list mutex is held too long");
	}
	for (int i=0; i<count; i++) {
		LOG_INF("register %s %d/%d: %s kind=%d",
			(list==&ztacx_variables)?"variable":"setting",
			i+1, count, v[i].name, (int)(v[i].kind));
		//LOG_HEXDUMP_INF(&(s[i].value), sizeof(s[i].value), "value dump");
		sys_slist_append(list, &(v[i].node));
	}
	sys_mutex_unlock(mutex);
	return 0;
}

/**
 * @brief Register a table of ztacx variable structures
 */
int ztacx_variables_register(struct ztacx_variable *s, int count)
{
	LOG_INF("%d", count);
	return ztacx_values_register(&ztacx_variables, &ztacx_variables_mutex, s, count);
}

/**
 * @brief Produce a string representation of the type and value of a ztacx_variable
 *
 * The description is suitable for display in log or shell messages.
 */
int ztacx_variable_describe(char *buf, int buf_max, const struct ztacx_variable *s)
{
	switch (s->kind) {
	case ZTACX_VALUE_STRING:
		snprintf(buf, buf_max, "%s=(string)[%s]", s->name, s->value.val_string?s->value.val_string:"[empty]");
		break;
	case ZTACX_VALUE_BOOL:
		snprintf(buf, buf_max, "%s=(bool)[%s]", s->name, s->value.val_bool?"true":"false");
		break;
	case ZTACX_VALUE_BYTE:
		snprintf(buf, buf_max, "%s=(byte)[%d]", s->name, (int)s->value.val_byte);
		break;
	case ZTACX_VALUE_UINT16:
		snprintf(buf, buf_max, "%s=(uint16)[%d]", s->name, (int)s->value.val_uint16);
		break;
	case ZTACX_VALUE_INT16:
		snprintf(buf, buf_max, "%s=(int16)[%d]", s->name, (int)s->value.val_int16);
		break;
	case ZTACX_VALUE_INT32:
		snprintf(buf, buf_max, "%s=(int32)[%d]", s->name, (int)s->value.val_int32);
		break;
	case ZTACX_VALUE_INT64:
		snprintf(buf, buf_max, "%s=(int64)[%ld]", s->name, (long)s->value.val_int64); // fixme lld?
		break;
	default:
		LOG_ERR("   %s=(unk)[%d]", s->name, (int)s->kind);
		return -EINVAL;
	}
	return 0;
}


/**
 * Store a value (from pointer) into a ztacx_variable
 */
int ztacx_variable_value_set(struct ztacx_variable *setting, const void *value)
{
	int size;

	if (!value) return 0;

	// TODO use a mutex for get/set operations

	switch (setting->kind) {
	case ZTACX_VALUE_STRING: {
		size = strlen((const char *)value) + 1;
		char *old = setting->value.val_string;
		char *val_string = calloc(size, sizeof(char));
		if (!val_string) {
			return -ENOMEM;
		}
		strcpy(val_string, value);
		setting->value.val_string = val_string;
		if (old) {
			free(old);
		}
		break;
	}
	case ZTACX_VALUE_BOOL:
		setting->value.val_bool = *(bool *)value;
		break;
	case ZTACX_VALUE_BYTE:
		setting->value.val_byte = *(uint8_t *)value;
		break;
	case ZTACX_VALUE_UINT16:
		setting->value.val_uint16 = *(uint16_t *)value;
		break;
	case ZTACX_VALUE_INT16:
		setting->value.val_int16 = *(int16_t *)value;
		break;
	case ZTACX_VALUE_INT32:
		setting->value.val_int32 = *(int32_t *)value;
		break;
	case ZTACX_VALUE_INT64:
		setting->value.val_int64 = *(int64_t *)value;
		break;
	default:
		LOG_ERR("Unhandled setting kind %d", (int)setting->kind);
		return -EINVAL;
	}
	if (setting->on_change) {
		LOG_DBG("Trigger on-change for %s", setting->name);
		k_work_submit(setting->on_change);
	}

	return 0;
}

/**
 * Store a value (from string) into a ztacx_variable
 */
int ztacx_variable_value_set_string(struct ztacx_variable *s, const char *value)
{
	if (!s) {
		LOG_ERR("Attempt to set null variable (%s)", value);
		return -EINVAL;
	}
	LOG_DBG("%s (%s) <= [%s]", s->name, ztacx_value_kind_names[s->kind], value);

	int err = 0;

	switch (s->kind) {
	case ZTACX_VALUE_STRING:
		ztacx_variable_value_set(s, value);
		break;
	case ZTACX_VALUE_BOOL: {
		bool val_bool;
		if ((strcmp(value,"true")==0) ||
		    (strcmp(value,"on")==0) ||
		    (strcmp(value,"1")==0)) {
			val_bool = true;
		}
		else {
			val_bool = false;
		}
		ztacx_variable_value_set(s, &val_bool);
		break;
	}
	case ZTACX_VALUE_BYTE: {
		uint8_t val_byte = atoi(value);
		ztacx_variable_value_set(s, &val_byte);
		break;
	}
	case ZTACX_VALUE_UINT16:
	{
		uint16_t val_uint16 = atoi(value);
		ztacx_variable_value_set(s, &val_uint16);
		break;
	}
	case ZTACX_VALUE_INT16: {
		int16_t val_int16 = atoi(value);
		ztacx_variable_value_set(s, &val_int16);
		break;
	}
	case ZTACX_VALUE_INT32:
	{
		int32_t val_int32 = atoi(value);
		ztacx_variable_value_set(s, &val_int32);
		break;
	}
	case ZTACX_VALUE_INT64: {
		int64_t val_int64 = atoi(value);// FIXME strtoull?
		ztacx_variable_value_set(s, &val_int64);
		break;
	}
	default:
		LOG_ERR("Unhandled variable type %d", (int)s->kind);
		err = -EINVAL;
	}
	if (err != 0) {
		LOG_ERR("variable parse failed: %d", err);
	}
	return err;
}

int ztacx_variable_value_set_bool(struct ztacx_variable *v, bool value)
{
	return ztacx_variable_value_set(v, &value);
}

int ztacx_variable_value_set_byte(struct ztacx_variable *v, uint8_t value)
{
	return ztacx_variable_value_set(v, &value);
}

int ztacx_variable_value_set_uint16(struct ztacx_variable *v, uint16_t value)
{
	return ztacx_variable_value_set(v, &value);
}

int ztacx_variable_value_set_int16(struct ztacx_variable *v, int16_t value)
{
	return ztacx_variable_value_set(v, &value);
}

int ztacx_variable_value_set_int32(struct ztacx_variable *v, int32_t value)
{
	return ztacx_variable_value_set(v, &value);
}

int ztacx_variable_value_set_int64(struct ztacx_variable *v, int64_t value)
{
	return ztacx_variable_value_set(v, &value);
}

int ztacx_variable_value_inc_int64(struct ztacx_variable *v)
{
	int64_t val_int64 = ztacx_variable_value_get_int64(v) + 1;
	return ztacx_variable_value_set_int64(v, val_int64);
}

int ztacx_variable_value_set_event(struct ztacx_variable *v, uint32_t event)
{
	if (v->value.val_event) {
		k_event_set(v->value.val_event, event);
		return 0;
	}
	return -ENODEV;
}

int ztacx_variable_value_post_event(struct ztacx_variable *v, uint32_t event)
{
	if (v->value.val_event) {
		k_event_post(v->value.val_event, event);
		return 0;
	}
	return -ENODEV;
}

uint32_t ztacx_variable_value_wait_event(struct ztacx_variable *v, uint32_t mask, k_timeout_t timeout)
{
	if (v->value.val_event) {
		return k_event_wait(v->value.val_event, mask, true, timeout);
	}
	return 0;
}

bool ztacx_variable_value_get_bool(struct ztacx_variable *v)
{
	bool value = false;
	if (ztacx_variable_value_get(v, &value, sizeof(value)) != 0) {
		LOG_ERR("Error fetching value of variable [%s]", v->name);
	}
	return value;
}

uint8_t ztacx_variable_value_get_byte(struct ztacx_variable *v)
{
	uint8_t value = 0;
	if (ztacx_variable_value_get(v, &value, sizeof(value)) != 0) {
		LOG_ERR("Error fetching value of variable [%s]", v->name);
	}
	return value;
}

uint16_t ztacx_variable_value_get_uint16(struct ztacx_variable *v)
{
	uint16_t value = 0;
	if (ztacx_variable_value_get(v, &value, sizeof(value)) != 0) {
		LOG_ERR("Error fetching value of variable [%s]", v->name);
	}
	return value;
}

int16_t ztacx_variable_value_get_int16(struct ztacx_variable *v)
{
	int16_t value = 0;
	if (ztacx_variable_value_get(v, &value, sizeof(value)) != 0) {
		LOG_ERR("Error fetching value of variable [%s]", v->name);
	}
	return value;
}

int32_t ztacx_variable_value_get_int32(struct ztacx_variable *v)
{
	int32_t value = 0;
	if (ztacx_variable_value_get(v, &value, sizeof(value)) != 0) {
		LOG_ERR("Error fetching value of variable [%s]", v->name);
	}
	return value;
}

int64_t ztacx_variable_value_get_int64(struct ztacx_variable *v)
{
	int64_t value = 0;
	if (ztacx_variable_value_get(v, &value, sizeof(value)) != 0) {
		LOG_ERR("Error fetching value of variable [%s]", v->name);
	}
	return value;
}

/**
 * Extract a value from ztacx_variable into a pointer
 */
int ztacx_variable_value_get(const struct ztacx_variable *v, void *value_r, int value_size)
{
	if (!v) {
		return -EINVAL;
	}

	switch (v->kind) {
	case ZTACX_VALUE_STRING:
		if ((value_size<1) || strlen(v->value.val_string) >= value_size) {
			// no room for return value
			return -E2BIG;
		}
		if (v->value.val_string==NULL) {
			// empty value => empty string
			((char *)value_r)[0]='\0';
			return 0;
		}
		// copy the value string into the return buffer
		strncpy(value_r, v->value.val_string, value_size);
		LOG_DBG("GET %s => [%s]", v->name, v->value.val_string);
		break;
	case ZTACX_VALUE_BOOL:
		*(bool *)value_r = v->value.val_bool;
		LOG_DBG("GET %s => %c", v->name, v->value.val_bool?'T':'F');
		break;
	case ZTACX_VALUE_BYTE:
		*(uint8_t *)value_r = v->value.val_byte;
		LOG_DBG("GET %s => %d", v->name, (int)v->value.val_byte);
		break;
	case ZTACX_VALUE_UINT16:
		*(uint16_t *)value_r = v->value.val_uint16;
		LOG_DBG("GET %s => %d", v->name, (int)v->value.val_uint16);
		break;
	case ZTACX_VALUE_INT16:
		*(int16_t *)value_r = v->value.val_int16;
		LOG_DBG("GET %s => %d", v->name, (int)v->value.val_int16);
		break;
	case ZTACX_VALUE_INT32:
		*(int32_t *)value_r = v->value.val_int32;
		LOG_DBG("GET %s => %d", v->name, (int)v->value.val_int32);
		break;
	case ZTACX_VALUE_INT64:
		*(int64_t *)value_r = v->value.val_int64;
		LOG_DBG("GET %s => %d", v->name, (int)v->value.val_int64);
		break;
	default:
		LOG_ERR("Unhandled variable type %d", (int)v->kind);
		return -EINVAL;
	}
	return 0;
}

/**
 * Look up a variable by name from the settings list
 */
struct ztacx_variable *ztacx_variable_find(const char *name)
{
	sys_slist_t *list = &ztacx_variables;
	struct ztacx_variable *v;
	SYS_SLIST_FOR_EACH_CONTAINER(list, v, node) {
		if (strcmp(v->name, name) == 0) {
			return v;
		}
	}
	LOG_WRN("no variable named '%s' found", name);
	return NULL;
}

int ztacx_variable_get(const char *name, void *value_r, int value_size)
{
	struct ztacx_variable *v = ztacx_variable_find(name);
	if (!v) return -ENOENT;

	return ztacx_variable_value_get(v, value_r, value_size);
}

int ztacx_variable_set(const char *name, void *value)
{
	struct ztacx_variable *v = ztacx_variable_find(name);
	if (!v) return -ENOENT;

	return ztacx_variable_value_set(v, value);
}


int ztacx_variable_ptr_set_onchange(struct ztacx_variable *v, struct k_work *work)
{
	if (v->on_change) {
		return -EEXIST;
	}
	v->on_change = work;
	return 0;
}



int ztacx_variable_set_onchange(const char *name, struct k_work *work)
{
	struct ztacx_variable *v = ztacx_variable_find(name);
	if (!v) return -ENOENT;
	return ztacx_variable_ptr_set_onchange(v, work);
}
