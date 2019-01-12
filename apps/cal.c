/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2019 K. Lange
 *
 * cal - print a calendar
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>

const char * month_names[] = {
	"January",
	"February",
	"March",
	"April",
	"May",
	"June",
	"July",
	"August",
	"September",
	"October",
	"November",
	"December",
};

int days_in_months[] = {
	31, 0, 31, 30, 31, 30, 31,
	31, 30, 31, 30, 31,
};

int main(int argc, char * argv[]) {
	if (argc > 1) {
		fprintf(stderr, "%s: arguments not currently supported\n", argv[0]);
		return 1;
	}
	struct timeval now;
	gettimeofday(&now, NULL);

	struct tm actual;
	struct tm * timeinfo;
	timeinfo = localtime((time_t *)&now.tv_sec);
	memcpy(&actual, timeinfo, sizeof(struct tm));
	timeinfo = &actual;

	char month[20];
	sprintf(month, "%s %d", month_names[timeinfo->tm_mon], timeinfo->tm_year + 1900);

	int len = (20 - strlen(month)) / 2;
	while (len > 0) {
		printf(" ");
		len--;
	}

	/* Heading */
	printf("%s\n", month);
	printf("Su Mo Tu We Th Fr Sa\n");

	/* Now's the fun part. */

	int days_in_month = days_in_months[timeinfo->tm_mon];
	if (days_in_month == 0) {
		/* How many days in February? */
		struct tm tmp;
		memcpy(&tmp, timeinfo, sizeof(struct tm));
		tmp.tm_mday = 29;
		tmp.tm_hour = 12;
		time_t tmp3 = mktime(&tmp);
		struct tm * tmp2 = localtime(&tmp3);
		if (tmp2->tm_mday == 29) {
			days_in_month = 29;
		} else {
			days_in_month = 28;
		}
	}

	int mday = timeinfo->tm_mday;
	int wday = timeinfo->tm_wday; /* 0 == sunday */

	while (mday > 1) {
		mday--;
		wday = (wday + 6) % 7;
	}

	for (int i = 0; i < wday; ++i) {
		printf("   ");
	}

	while (mday <= days_in_month) {
		if (mday == timeinfo->tm_mday) {
			printf("\033[7m%2d\033[0m ", mday);
		} else {
			printf("%2d ", mday);
		}

		if (wday == 6) {
			printf("\n");
		}

		mday += 1;
		wday = (wday + 1) % 7;
	}

	if (wday != 0) {
		printf("\n");
	}

	return 0;
}

