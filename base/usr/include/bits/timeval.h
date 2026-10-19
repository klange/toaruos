#pragma once

#include <_cheader.h>
#include <sys/types.h>

_Begin_C_Header

struct timeval {
	time_t      tv_sec;     /* seconds */
	suseconds_t tv_usec;    /* microseconds */
};

_End_C_Header
