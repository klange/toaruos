#include <stdio.h>
#include <time.h>
#include <sys/time.h>

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
        int t_year = 1900 + date->tm_year;
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
