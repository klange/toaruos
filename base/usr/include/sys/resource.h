#pragma once

#include <_cheader.h>
#include <sys/time.h>

#define RUSAGE_SELF 0
#define RUSAGE_CHILDREN - 1

_Begin_C_Header

struct rusage {
	struct timeval ru_utime;
	struct timeval ru_stime;
};

#ifndef _KERNEL_
int getrusage(int, struct rusage *);
#endif

_End_C_Header

