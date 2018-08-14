/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * date - Print the current date and time.
 *
 * TODO: The traditional POSIX version of this tool is supposed
 *       to accept a format *and* allow you to set the time.
 *       We currently lack system calls for setting the time,
 *       but when we add those this should probably be updated.
 *
 *       At the very least, improving this to print the "correct"
 *       default format would be good.
 */
#include <stdio.h>
#include <time.h>
#include <sys/time.h>

/* XXX Why do we have our own list of weekdays */
char * daysofweeks[] = {
    "Sunday",
    "Monday",
    "Tuesday",
    "Wednesday",
    "Thursday",
    "Friday",
    "Saturday"
};

void print_time(time_t time) {
    struct tm * date = localtime(&time);
    if (!date) {
        fprintf(stderr, "Failure.\n");
    } else {
        printf("%d-%02d-%02d %02d:%02d:%02d (%s, day %d)\n",
                date->tm_year + 1900,
                date->tm_mon + 1,
                date->tm_mday,
                date->tm_hour,
                date->tm_min,
                date->tm_sec,
                daysofweeks[date->tm_wday],
                date->tm_yday);
    }
}

int main(int argc, char * argv[]) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    print_time(tv.tv_sec);
    return 0;
}
