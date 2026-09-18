#pragma once
#include <_cheader.h>
#include <sys/stat.h>

_Begin_C_Header

struct NamedColor {
	char * name;
	char * color;
};

enum ColorNames {
	LS_COLOR_LEFT,
	LS_COLOR_RIGHT,
	LS_COLOR_END,
	LS_COLOR_RESET,
	LS_COLOR_DIR,
	LS_COLOR_SYM,
	LS_COLOR_BDEV,
	LS_COLOR_CDEV,
	LS_COLOR_ORPHAN,
	LS_COLOR_EXE,
	LS_COLOR_SETUID,
	LS_COLOR_SETGID,

	LS_COLOR_PIPE,
	LS_COLOR_SOCK,
	LS_COLOR_NORM,
	LS_COLOR_FILE,
	LS_COLOR_MISS,
	LS_COLOR_ST,
	LS_COLOR_OW,
	LS_COLOR_TW,

	LS_COLOR_MAX,
};


const char * ls_color_str(const char * name, struct stat * sb);
int ls_colors_init(void);
struct NamedColor * ls_base_colors_ptr(void);

#define LS_C(type) (ls_base_colors_ptr()[LS_COLOR_ ## type].color)

_End_C_Header
