#pragma once

extern int ztacx_settings_init(struct ztacx_leaf *leaf);
extern int ztacx_settings_start(struct ztacx_leaf *leaf);
extern int ztacx_settings_load();

extern int ztacx_settings_register(struct ztacx_variable *s, int count);
extern int ztacx_settings_add_kind(
	const char *name, enum ztacx_value_kind kind,
	void *value, int value_len);

extern struct ztacx_variable *ztacx_setting_find(const char *name);
extern int ztacx_setting_set(struct ztacx_variable *s, const char *value);
extern void ztacx_settings_show();

ZTACX_CLASS_DEFINE(settings, ((struct ztacx_leaf_cb){.init=&ztacx_settings_init,.start=&ztacx_settings_start}));
ZTACX_LEAF_DEFINE(settings, settings, NULL);

