/**
 * @brief free - Show free / used / total RAM
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2015-2018 K. Lange
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#define MEMINFO_PATH "/proc/meminfo"

int show_usage(int argc, char * argv[]) {
#define X_S "\033[3m"
#define X_E "\033[0m"
	fprintf(stderr,
			"free - show available memory\n"
			"\n"
			"usage: %s [-utk?]\n"
			"\n"
			" -u     " X_S "show used instead of free" X_E "\n"
			" -t     " X_S "include a total" X_E "\n"
			" -k     " X_S "use kilobytes instead of megabytes" X_E "\n"
			" -?     " X_S "show this help text" X_E "\n"
			"\n", argv[0]);
	return 1;
}

int main(int argc, char * argv[]) {
	int show_used = 0;
	int use_kilobytes = 0;
	int show_total = 0;

	int c;
	while ((c = getopt(argc, argv, "utk?")) != -1) {
		switch (c) {
			case 'u':
				show_used = 1;
				break;
			case 't':
				show_total = 1;
				break;
			case 'k':
				use_kilobytes = 1;
				break;
			case '?':
				return show_usage(argc, argv);
		}
	}

	if (optind != argc) return show_usage(argc, argv);

	const char * unit = "kB";

	FILE * f = fopen(MEMINFO_PATH, "r");
	if (!f) {
		fprintf(stderr, "%s: %s: %s\n", argv[0], MEMINFO_PATH, strerror(errno));
		return 1;
	}

	int total, free, used;
	char buf[1024] = {0};
	fgets(buf, 1024, f);
	char * a, * b;

	a = strchr(buf, ' ');
	a++;
	b = strchr(a, '\n');
	*b = '\0';
	total = atoi(a);

	fgets(buf, 1024, f);
	a = strchr(buf, ' ');
	a++;
	b = strchr(a, '\n');
	*b = '\0';
	free = atoi(a);

	//fscanf(f, "MemTotal: %d kB\nMemFree: %d kB\n", &total, &free);
	used = total - free;

	if (!use_kilobytes) {
		unit = "MB";
		free /= 1024;
		used /= 1024;
		total /= 1024;
	}

	if (show_used) {
		printf("%d %s", used, unit);
	} else {
		printf("%d %s", free, unit);
	}

	if (show_total) {
		printf(" / %d %s", total, unit);
	}

	printf("\n");

	return 0;
}

