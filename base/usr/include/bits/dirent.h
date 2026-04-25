#pragma once

#include <_cheader.h>

#include <sys/types.h>

_Begin_C_Header

typedef struct dirent {
	ino_t d_ino;
	char d_name[256];
} dirent;

#ifndef _KERNEL_
typedef struct DIR DIR;
DIR * opendir (const char * dirname);
DIR * fdopendir (int fd);
int closedir (DIR * dir);
struct dirent * readdir (DIR * dirp);
long telldir (DIR * dirp);
void rewinddir (DIR * dirp);
void seekdir (DIR * dirp, long loc);

__redirect(readdir,readdir64);
#endif

_End_C_Header
