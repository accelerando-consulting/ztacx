#ifndef _ZTACX_GPS_H
#define _ZTACX_GPS_H 1

#include <zephyr.h>

struct setting_string
{
	const char *name;
	char *value;
	size_t size;
};

struct setting_uint16
{
	const char *name;
	uint16_t *value;
	uint16_t max;
};

struct setting_uint64
{
	const char *name;
	uint64_t *value;
};

// define these somewhere in your app code
extern struct setting_string string_settings[];
extern struct setting_uint16 uint16_settings[];
extern struct setting_uint64 uint64_settings[];

#endif
