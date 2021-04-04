#pragma once

struct timespec {
    int tv_sec;
    int tv_nsec;
};

#define CLOCK_MONOTONIC 0
#define clock_gettime(a,b)
