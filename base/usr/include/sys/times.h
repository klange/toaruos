#pragma once

#include <_cheader.h>
#include <sys/types.h>

_Begin_C_Header

struct tms {
	clock_t tms_utime;
	clock_t tms_stime;
	clock_t tms_cutime;
	clock_t tms_cstime;
};

extern clock_t times(struct tms *buf);

_End_C_Header
