#pragma once

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <zephyr.h>
#include <device.h>
#include <devicetree.h>
#include <init.h>
#include <kernel.h>
#include <logging/log.h>
#include <logging/log_ctrl.h>
#include <shell/shell.h>
#include <sys/printk.h>
#include <sys/mutex.h>

#ifdef __main__
LOG_MODULE_REGISTER(app, LOG_LEVEL_DBG);
#else
LOG_MODULE_DECLARE(app);
#endif

extern uint8_t device_id[16];
extern int device_id_len;
extern char device_id_str[35];
extern char device_id_short[7];
extern char name_setting[21];

#ifndef POST_ERROR_HISTORY_LEN
#define POST_ERROR_HISTORY_LEN 5
#endif

extern char post_error_history[POST_ERROR_HISTORY_LEN];
extern const char *post_error_names[];

struct ztacx_leaf;
struct ztacx_leaf_class;

/** @brief Ztacx leaf class callbacks
 *
 * This structure defines the lifecycle functions for each instance (leaf) of a leaf class.
 * It should be declared inline in an invocation @ref ZTACX_CLASS_DEFINE
 *
 * Only the 'setup' and 'start' functions are mandatory.
 *
 * The leaf setup function is triggered by SYS_INIT level APPLICATION, priority 50
 * The leaf start function is triggered by SYS_INIT level APPLICATION, priority 75 (after all leaves are setup)
 *
 */
struct ztacx_leaf_cb
{
	/** @brief A function to be triggered via the SYS_INIT mechanism */
	int (*init)(struct ztacx_leaf *leaf);
	/** @brief A function to start processing of a module, invoked after all leaves are setup  */
	int (*start)(struct ztacx_leaf *leaf);
	/** @brief A function to stop processing of a module */
	int (*stop)(struct ztacx_leaf *leaf);
	/** @brief A function called before entering system_off (deep sleep) */
	int (*pre_sleep)(struct ztacx_leaf *leaf);
	/** @brief A function called when resuming from sleep */
	int (*post_sleep)(struct ztacx_leaf *leaf);
};

struct ztacx_leaf_class {
	const char *name;
	struct ztacx_leaf_class *super;
	struct ztacx_leaf_cb *cb;
	sys_snode_t node;
};

struct ztacx_leaf
{
	const char *name;
	struct ztacx_leaf_class *class;
	bool ready;
	bool running;
	void *context;
	sys_snode_t node;
};


/**
 * @def ZTACX_CLASS_DEFINE
 *
 * @brief Define a "Leaf" (module) class in the ztacx framework, and its lifecycle callbacks
 *
 * @details This macro lets you define a leaf class, and register its callback functions
 *
 * The callback functions are called for each INSTANCE (if any) declared by ZTACX_LEAF_DEFINE, not for the class.
 * The function ztacx_class_register is called at startup for the class.
 *
 * The instance init function should arrange for any interrupts or poll timers to be installed.
 *
 */
#ifdef __main__
#define ZTACX_CLASS_DEFINE(class_name,class_cb) \
	struct ztacx_leaf_cb ztacx_class_cb_ ## class_name = ((struct ztacx_leaf_cb)(class_cb)); \
	struct ztacx_leaf_class ztacx_class_ ## class_name = {.name=#class_name,.super=NULL,.cb=&ztacx_class_cb_ ## class_name}; \
	Z_INIT_ENTRY_DEFINE(Z_SYS_NAME(ztacx_class_init_ ## class_name), (int(*)(const struct device *))ztacx_class_register, (struct device *)&ztacx_class_ ## class_name, APPLICATION, 25)
#define ZTACX_SUBCLASS_DEFINE(class_name, super_name , class_cb)				\
	struct ztacx_leaf_cb ztacx_class_cb_ ## class_name = class_cb;	\
	struct ztacx_leaf_class ztacx_class_ ## class_name = (struct ztacx_leaf_cb){.name=#class_name,.super=&ztacx_class_ ## super_name,.cb=&ztacx_class_cb_ ## class_name}; \
	Z_INIT_ENTRY_DEFINE(Z_SYS_NAME(ztacx_class_init_ ## class_name), (int(*)(const struct device *))ztacx_class_register, (struct device *)&ztacx_class_ ## class_name, APPLICATION, 25);
#define ZTACX_CLASS_AUTO_DEFINE(class_name) \
	extern int ztacx_##class_name##_init(struct ztacx_leaf *leaf);	\
	extern int ztacx_##class_name##_start(struct ztacx_leaf *leaf);	\
	extern int ztacx_##class_name##_stop(struct ztacx_leaf *leaf);	\
	extern int ztacx_##class_name##_pre_sleep(struct ztacx_leaf *leaf);	\
	extern int ztacx_##class_name##_post_sleep(struct ztacx_leaf *leaf);	\
	ZTACX_CLASS_DEFINE(class_name, ((struct ztacx_leaf_cb){		\
		.init = ztacx_##class_name##_init,			\
		.start = ztacx_##class_name##_start,			\
		.stop = ztacx_##class_name##_stop,			\
		.pre_sleep = ztacx_##class_name##_pre_sleep,		\
		.post_sleep = ztacx_##class_name##_post_sleep}))
#else
#define ZTACX_CLASS_DEFINE(class_name,_unused) \
	extern struct ztacx_leaf_cb ztacx_class_cb ## class_name;	\
	extern struct ztacx_leaf_class ztacx_class ## class_name
#define ZTACX_CLASS_AUTO_DEFINE(class_name) \
	extern struct ztacx_leaf_cb ztacx_class_cb ## class_name;	\
	extern struct ztacx_leaf_class ztacx_class ## class_name
#endif

/**
 * @def ZTACX_LEAF_DEFINE
 *
 * @brief Define a "Leaf" (module) instance in the ztacx framework
 *
 * @details This macro lets you define a leaf instance,
 *          and schedule its init function to run at startup via SYS_INIT
 *
 * The SYS_INIT 'dev' pointer is (ab)used to carry the leaf pointer
 *
 */
#ifdef __main__
#define ZTACX_LEAF_DEFINE(class_name, leaf_name, context_ptr) \
	struct ztacx_leaf ztacx_leaf_##class_name##_##leaf_name = {.name=#leaf_name,.class=&(ztacx_class_##class_name), .context=(void*)(context_ptr)}; \
	Z_INIT_ENTRY_DEFINE(Z_SYS_NAME(ztacx_leaf_init_##class_name##_##leaf_name), (int(*)(const struct device *))ztacx_leaf_sys_init, (struct device *)&ztacx_leaf_##class_name##_##leaf_name, APPLICATION, 50); \
	Z_INIT_ENTRY_DEFINE(Z_SYS_NAME(ztacx_leaf_start_##class_name##_##leaf_name), (int(*)(const struct device *))ztacx_leaf_sys_start, (struct device *)&ztacx_leaf_##class_name##_##leaf_name, APPLICATION, 75);
#else
#define ZTACX_LEAF_DEFINE(class_name, leaf_name, context_ptr) extern struct ztacx_leaf ztacx_leaf_##class_name##_##leaf_name
#endif

/**
 * @brief the type of a setting or value (int string etc)
 *
 */
enum ztacx_value_kind {
	ZTACX_VALUE_STRING=0,
	ZTACX_VALUE_BOOL,
	ZTACX_VALUE_BYTE,
	ZTACX_VALUE_UINT16,
	ZTACX_VALUE_INT64,
	ZTACX_VALUE_MAX
};


/**
 * @brief a union holding a typed value
 */
union ztacx_value
{
	char *val_string;
	bool val_bool;
	uint8_t val_byte;
	uint16_t val_uint16;
	int64_t val_int64;
};

/**
 * @brief a named variable (a persistent setting or a state value)
 */
struct ztacx_variable
{
	char name[CONFIG_ZTACX_VALUE_NAME_MAX];
	enum ztacx_value_kind kind;
	union ztacx_value value;
	sys_snode_t node;
};
int ztacx_values_register(sys_slist_t *list, struct sys_mutex *mutex, struct ztacx_variable *v, int count);

#define ZTACX_SETTING_FIND(n) if (!(n=ztacx_setting_find(#n))) {        \
       LOG_ERR("APP ABORT Setting '"#n"' not found");                   \
       return -1;                                                       \
       }

#define ZTACX_VAR_FIND(n) if (!(n=ztacx_variable_find(#n))) {		\
       LOG_ERR("APP ABORT Variable '"#n"' not found");                  \
       return -1;                                                       \
       }

//
// functions for working with ztacx_variable items
//
int ztacx_variable_describe(char *buf, int buf_max, const struct ztacx_variable *s);
int ztacx_variable_value_set(struct ztacx_variable *v, void *value);
int ztacx_variable_set(struct ztacx_variable *v, const char *value);
int ztacx_variable_get(const struct ztacx_variable *v, void *value_r, int value_size);
struct ztacx_variable *ztacx_variable_find(const char *name);
int ztacx_variables_register(struct ztacx_variable *v, int count);
void ztacx_variables_show();


// Functions for inspecting and modifying leaves (modules)
//
extern struct ztacx_leaf *ztacx_leaf_get(const char *name);
extern int ztacx_leaf_start(const char *name);
extern int ztacx_leaf_stop(const char *name);
extern bool ztacx_leaf_is_ready(const char *name);
extern bool ztacx_leaf_is_running(const char *name);
#if CONFIG_SHELL
extern int ztacx_shell_cmd_register(struct shell_static_entry entry);
#endif

// Functions for the lifecycle of the whole ztacx framework
// You probably won't need to call these directly
extern int ztacx_class_register(struct ztacx_leaf_class *class);
extern int ztacx_leaf_sys_init(struct ztacx_leaf *leaf);
extern int ztacx_leaf_sys_start(struct ztacx_leaf *leaf);
extern int ztacx_pre_sleep(void);
extern int ztacx_post_sleep(void);
