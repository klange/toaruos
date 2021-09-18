#pragma once

#include <_cheader.h>
#include <stddef.h>
#include <sys/types.h>

_Begin_C_Header

struct tm {
    int tm_sec;    /* Seconds (0-60) */
    int tm_min;    /* Minutes (0-59) */
    int tm_hour;   /* Hours (0-23) */
    int tm_mday;   /* Day of the month (1-31) */
    int tm_mon;    /* Month (0-11) */
    int tm_year;   /* Year - 1900 */
    int tm_wday;   /* Day of the week (0-6, Sunday = 0) */
    int tm_yday;   /* Day in the year (0-365, 1 Jan = 0) */
    int tm_isdst;  /* Daylight saving time */

    const char * _tm_zone_name;
    int _tm_zone_offset;
};

extern struct tm *localtime(const time_t *timep);
extern struct tm *gmtime(const time_t *timep);

extern struct tm *localtime_r(const time_t *timep, struct tm * buf);
extern struct tm *gmtime_r(const time_t *timep, struct tm * buf);

extern size_t strftime(char *s, size_t max, const char *format, const struct tm *tm);
extern time_t time(time_t * out);
extern double difftime(time_t a, time_t b);
extern time_t mktime(struct tm *tm);

extern char * asctime(const struct tm *tm);
extern char * ctime(const time_t * timep);

extern clock_t clock(void);
#define CLOCKS_PER_SEC 1000000

struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

typedef int clockid_t;

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

extern int clock_gettime(clockid_t clk_id, struct timespec *tp);
extern int clock_getres(clockid_t clk_id, struct timespec *res);

_End_C_Header
