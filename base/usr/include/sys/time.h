#pragma once

#include <_cheader.h>
#include <sys/types.h>

#include <bits/timeval.h>

_Begin_C_Header

struct timezone {
	int tz_minuteswest;     /* minutes west of Greenwich */
	int tz_dsttime;         /* type of DST correction */
};

extern int gettimeofday(struct timeval *p, void *z);
extern int settimeofday(struct timeval *p, void *z);
extern int futimes(int fd, const struct timeval times[2]);
extern int utimes(const char *path, const struct timeval times[2]);

_End_C_Header
