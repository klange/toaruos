#pragma once

#include <_cheader.h>
#include <sys/types.h>

_Begin_C_Header

struct utimbuf {
    time_t actime;
    time_t modtime;
};

extern int utime(const char *filename, const struct utimbuf *times);

_End_C_Header
