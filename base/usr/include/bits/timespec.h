#pragma once

#define UTIME_NOW  -1
#define UTIME_OMIT -2

struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

