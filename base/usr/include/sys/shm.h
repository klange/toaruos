#pragma once

#include <_cheader.h>
#include <sys/types.h>

_Begin_C_Header
extern void * shm_obtain(char * path, size_t * size);
extern int shm_release(char * path);
_End_C_Header

