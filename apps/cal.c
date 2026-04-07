/**
 * @brief cal - print a calendar
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2019 K. Lange
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>

static int days_in_months[] = {
	31, 0, 31, 30, 31, 30, 31,
	31, 30, 31, 30, 31,
};

static int usage(char * argv[]) {
	fprintf(stderr,
		"usage: %s [-h3O] [-A n] [-B n] [[month] year]\n",
		argv[0]);
	return 1;
}

static void next_month(struct tm * target) {
	if (target->tm_mon == 11) {
		target->tm_mon = 0;
		target->tm_year += 1;
	} else {
		target->tm_mon += 1;
	}
}

static void previous_month(struct tm * target) {
	if (target->tm_mon == 0) {
		target->tm_mon = 11;
		target->tm_year -= 1;
	} else {
		target->tm_mon -= 1;
	}
}

static void print_calendars(struct tm *today, struct tm *target, int count, int highlight_today, int is_year) {
	/* Now turn our time back into the actual time. */
	time_t target_time;
	struct tm * actual = calloc(sizeof(struct tm), count);
	struct tm ** timeinfo = calloc(sizeof(struct tm*), count);

	for (int i = 0; i < count; ++i) {
		target_time = mktime(target);
		timeinfo[i] = localtime_r(&target_time, &actual[i]);
		next_month(target);
	}

	for (int i = 0; i < count; ++i) {
		char month[20];
		strftime(month, sizeof(month), is_year ? "%B" : "%B %Y", timeinfo[i]);

		int len = (20 - strlen(month)) / 2;
		for (int j = 0; j < len; ++j) printf(" ");
		printf("%s", month);
		for (int j = 0; j < 22 - (int)strlen(month) - len; ++j) {
			printf(" ");
		}
	}
	printf("\n");
	for (int i = 0; i < count; ++i) {
		printf("Su Mo Tu We Th Fr Sa  ");
	}
	printf("\n");

	int * days_in_month = calloc(sizeof(int), count);
	int * mday = calloc(sizeof(int), count);
	int * wday = calloc(sizeof(int), count);

	/* Figure out how many days are in each of months we are going
	 * to display, and what day of the week each starts on */
	for (int i = 0; i < count; ++i) {
		days_in_month[i] = days_in_months[timeinfo[i]->tm_mon];
		if (days_in_month[i] == 0) {
			/* How many days in February? */
			struct tm tmp;
			memcpy(&tmp, timeinfo[i], sizeof(struct tm));
			tmp.tm_mday = 29;
			tmp.tm_hour = 12;
			time_t tmp3 = mktime(&tmp);
			struct tm * tmp2 = localtime(&tmp3);
			if (tmp2->tm_mday == 29) {
				days_in_month[i] = 29;
			} else {
				days_in_month[i] = 28;
			}
		}
		mday[i] = timeinfo[i]->tm_mday;
		wday[i] = timeinfo[i]->tm_wday;

		while (mday[i] > 1) {
			mday[i]--;
			wday[i] = (wday[i] + 6) % 7;
		}
	}

	/* Each pass through this loop is one row */
	while (1) {
		int maybe_stop = 0;
		for (int i = 0; i < count; ++i) {
			if (mday[i] == 1) for (int j = 0; j < wday[i]; ++j) printf("   ");
			int printed = 0;
			while (mday[i] <= days_in_month[i]) {
				if (mday[i] == today->tm_mday && timeinfo[i]->tm_mon == today->tm_mon && timeinfo[i]->tm_year == today->tm_year && highlight_today) {
					printf("\033[7m%2d\033[0m ", mday[i]);
				} else {
					printf("%2d ", mday[i]);
				}

				mday[i] += 1;
				wday[i] = (wday[i] + 1) % 7;
				printed = 1;
				if (wday[i] == 0) break;
			}

			if (i + 1 != count) {
				if (!printed) {
					for (int j = 0; j < 22; ++j) printf(" ");
				} else {
					for (int j = wday[i]; j != 0 && j < 7; ++j) printf("   ");
					printf(" ");
				}
			} else {
				printf("\n");
			}

			if (mday[i] > days_in_month[i]) maybe_stop += 1;
		}

		if (maybe_stop == count) break;
	}

	free(mday);
	free(wday);
	free(days_in_month);
	free(timeinfo);
	free(actual);
}

int main(int argc, char * argv[]) {
	int highlight_today = 1;
	int months_before = 0;
	int months_after = 0;
	int one_row = 0;
	int opt;

	while ((opt = getopt(argc, argv, "h3A:B:O")) != -1) {
		switch (opt) {
			case 'h':
				highlight_today = 0;
				break;
			case '3':
				months_before = 1;
				months_after = 1;
				break;
			case 'A':
				months_after = strtoul(optarg, NULL, 10);
				break;
			case 'B':
				months_before = strtoul(optarg, NULL, 10);
				break;
			case 'O':
				one_row = 1;
				break;
			case '?':
				return usage(argv);
		}
	}

	if (optind + 3 <= argc) return usage(argv);

	/* Get today as a reference point */
	struct timeval now;
	gettimeofday(&now, NULL);
	struct tm today;
	localtime_r((time_t *)&now.tv_sec, &today);

	struct tm target = {0};
	target.tm_year = today.tm_year;
	target.tm_mon = today.tm_mon;
	target.tm_mday = 3; /* Use the third so I don't have to think about timezones at all */

	int whole_year = (optind + 1 == argc);

	/* If we have a month and a year, process a month first */
	if (optind + 2 <= argc) {
		char * end = NULL;
		long month = strtol(argv[optind], &end, 10);
		if (end == argv[optind] || *end || month < 1 || month > 12) {
			fprintf(stderr, "%s: %s: not an understood month\n", argv[0], argv[optind]);
			return usage(argv);
		}
		target.tm_mon = month - 1;
		optind++;
	}

	/* If we still have a year to process... */
	if (optind + 1 <= argc) {
		char * end = NULL;
		long year = strtol(argv[optind], &end, 10);
		if (end == argv[optind] || *end) {
			fprintf(stderr, "%s: %s: year must be an integer\n", argv[0], argv[optind]);
			return usage(argv);
		}
		if (year < 1970 || year > 2099) {
			fprintf(stderr, "%s: %s: this tool only supports years between 1970 and 2099\n", argv[0], argv[optind]);
			return usage(argv);
		}
		target.tm_year = year - 1900;
		if (whole_year) {
			target.tm_mon = 0;
		}
	}

	if (whole_year && (months_before || months_after || one_row)) {
		fprintf(stderr, "%s: can not mix -3/-A/-B/-O and year with no month\n", argv[0]);
		return usage(argv);
	}

	if (whole_year) {
		for (int i = 0; i < 30; ++i) printf(" ");
		printf("%d", target.tm_year + 1900);
		for (int i = 0; i < 30; ++i) printf(" ");
		printf("\n");
		print_calendars(&today, &target, 3, highlight_today, 1);
		printf("\n");
		print_calendars(&today, &target, 3, highlight_today, 1);
		printf("\n");
		print_calendars(&today, &target, 3, highlight_today, 1);
		printf("\n");
		print_calendars(&today, &target, 3, highlight_today, 1);
	} else {
		for (int i = 0; i < months_before; ++i) previous_month(&target);
		if (one_row) {
			print_calendars(&today, &target, months_after + months_before + 1, highlight_today, 0);
		} else {
			int months_left = months_after + months_before + 1;
			while (months_left > 0) {
				print_calendars(&today, &target, months_left >= 3 ? 3 : months_left, highlight_today, 0);
				months_left -= 3;
				if (months_left > 0) printf("\n");
			}
		}
	}

	return 0;
}

