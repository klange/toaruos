#pragma once

struct option {
	int * value;
	char * title;
	char * description_1;
	char * description_2;
};

extern struct option boot_options[25];

#define BOOT_OPTION(_value, default_val, option, d1, d2) \
	int _value = default_val;\
	boot_options[_boot_offset].value = &_value; \
	boot_options[_boot_offset].title = option; \
	boot_options[_boot_offset].description_1 = d1; \
	boot_options[_boot_offset].description_2 = d2; \
	_boot_offset++

struct bootmode {
	int index;
	char * key;
	char * title;
};

extern struct bootmode boot_mode_names[];
extern unsigned int BASE_SEL;

extern int sel_max;
extern int sel;
extern int _boot_offset;
extern void toggle(int ndx, int value, char *str);

