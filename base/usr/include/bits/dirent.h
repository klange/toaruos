#pragma once

#include <_cheader.h>
#include <stdint.h>

_Begin_C_Header

typedef struct dirent {
	uint32_t d_ino;
	char d_name[256];
} dirent;

typedef struct DIR {
	int fd;
	int cur_entry;
} DIR;

DIR * opendir (const char * dirname);
int closedir (DIR * dir);
struct dirent * readdir (DIR * dirp);

_End_C_Header
