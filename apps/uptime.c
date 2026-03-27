/**
 * @brief Print system uptime
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2015-2018 K. Lange
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

void print_time(void) {
	struct timeval now;
	struct tm * timeinfo;
	char clocktime[10];

	gettimeofday(&now, NULL);
	timeinfo = localtime((time_t *)&now.tv_sec);
	strftime(clocktime, 80, "%H:%M:%S", timeinfo);

	printf(" %s ", clocktime);
}

#define MINUTE (60)
#define HOUR (60 * MINUTE)
#define DAY (24 * HOUR)
void print_seconds(int seconds) {
	if (seconds > DAY) {
		int days = seconds / DAY;
		seconds -= DAY * days;
		printf("%d day%s, ", days, days != 1 ? "s" : "");
	}
	if (seconds > HOUR) {
		int hours = seconds / HOUR;
		seconds -= HOUR * hours;
		int minutes = seconds / MINUTE;
		printf("%2d:%02d", hours, minutes);
		return;
	} else if (seconds > MINUTE) {
		int minutes = seconds / MINUTE;
		printf("%d minute%s,  ", minutes, minutes != 1 ? "s" : "");
		seconds -= MINUTE * minutes;
	}

	printf("%2d second%s", seconds, seconds != 1 ? "s" : "");
}

void print_uptime(void) {
	FILE * f = fopen("/proc/uptime", "r");
	if (!f) return;

	int seconds;

	char buf[1024] = {0};
	fgets(buf, 1024, f);
	char * dot = strchr(buf, '.');
	*dot = '\0';
	dot++;
	dot[3] = '\0';

	seconds = atoi(buf);

	printf("up ");

	print_seconds(seconds);
}

int show_usage(int argc, char * argv[]) {
#define X_S "\033[3m"
#define X_E "\033[0m"
	fprintf(stderr,
			"%s - display system uptime information\n"
			"\n"
			"usage: %s [-p]\n"
			"\n"
			" -p     " X_S "show just the uptime info" X_E "\n"
			" -?     " X_S "show this help text" X_E" \n"
			"\n", argv[0], argv[0]);
	return 1;
}

int main(int argc, char * argv[]) {
	int just_pretty_uptime = 0;
	int opt;

	while ((opt = getopt(argc, argv, "?p")) != -1 ) {
		switch (opt) {
			case 'p':
				just_pretty_uptime = 1;
				break;
			case '?':
				return show_usage(argc, argv);
		}
	}


	if (!just_pretty_uptime)
		print_time();
	print_uptime();

	printf("\n");

	return 0;
}


