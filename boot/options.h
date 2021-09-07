#pragma once

struct option {
	int * value;
	char * title;
	char * description_1;
	char * description_2;
};

extern struct option boot_options[20];
static int _boot_offset = 0;

#define BOOT_OPTION(_value, default_val, option, d1, d2) \
	int _value = default_val;\
	boot_options[_boot_offset].value = &_value; \
	boot_options[_boot_offset].title = option; \
	boot_options[_boot_offset].description_1 = d1; \
	boot_options[_boot_offset].description_2 = d2; \
	_boot_offset++;

struct bootmode {
	int index;
	char * key;
	char * title;
};

#define BASE_SEL ((sizeof(boot_mode_names)/sizeof(*boot_mode_names))-1)
extern int base_sel;

#define BOOT_SET() do { \
	base_sel = BASE_SEL; \
	_boot_offset = 0; \
	memset(boot_options, 0, sizeof(boot_options)); \
} while (0)

extern char * VERSION_TEXT;
extern char * HELP_TEXT;
extern char * HELP_TEXT_OPT;
extern char * COPYRIGHT_TEXT;
extern char * LINK_TEXT;
extern char * kernel_path;
extern char * ramdisk_path;
extern char cmdline[1024];

extern struct bootmode boot_mode_names[];
