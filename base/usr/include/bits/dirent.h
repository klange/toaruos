#pragma once

#include <_cheader.h>

#include <sys/types.h>

_Begin_C_Header

typedef struct dirent {
	ino_t d_ino;
	char d_name[256];
} dirent;

#ifndef _KERNEL_
typedef struct DIR {
	int fd;
	int cur_entry;
} DIR;

DIR * opendir (const char * dirname);
int closedir (DIR * dir);
struct dirent * readdir (DIR * dirp) __asm__("readdir64");
long telldir (DIR * dirp);
void rewinddir (DIR * dirp);
void seekdir (DIR * dirp, long loc);
#endif

_End_C_Header
