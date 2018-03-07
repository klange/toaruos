#pragma once

#include <stddef.h>
#include <sys/types.h>

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
};

extern struct tm *localtime(const time_t *timep);
extern size_t strftime(char *s, size_t max, const char *format, const struct tm *tm);
extern time_t time(time_t * out);
extern double difftime(time_t a, time_t b);
