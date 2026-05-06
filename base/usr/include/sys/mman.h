#pragma once

#include <_cheader.h>
#include <sys/types.h>

#define PROT_NONE   0
#define PROT_READ   1
#define PROT_WRITE  2
#define PROT_EXEC   4

#define MAP_SHARED     0x0001
#define MAP_PRIVATE    0x0002

#define MAP_FIXED      0x0010
#define MAP_ANONYMOUS  0x0020

#define MAP_ANON       MAP_ANONYMOUS

_Begin_C_Header

#ifndef __kernel__

extern void * mmap(void *,size_t,int,int,int,off_t);
extern int munmap(void*,size_t);

#endif

_End_C_Header
