#pragma once

#include <_cheader.h>
#include <sys/types.h>

_Begin_C_Header

struct timeval {
	time_t      tv_sec;     /* seconds */
	suseconds_t tv_usec;    /* microseconds */
};

struct timezone {
	int tz_minuteswest;     /* minutes west of Greenwich */
	int tz_dsttime;         /* type of DST correction */
};

extern int gettimeofday(struct timeval *p, void *z);

_End_C_Header
