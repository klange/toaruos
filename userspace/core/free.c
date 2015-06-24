/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2015 Kevin Lange
 *
 * free - Show free / used / total RAM
 */
#include <stdio.h>
#include <getopt.h>

void show_usage(int argc, char * argv[]) {
	printf(
			"free - show available memory\n"
			"\n"
			"usage: %s [-utk?]\n"
			"\n"
			" -u     \033[3mshow used instead of free\033[0m\n"
			" -t     \033[3minclude a total\033[0m\n"
			" -k     \033[3muse kilobytes instead of megabytes\033[0m\n"
			" -?     \033[3mshow this help text\033[0m\n"
			"\n", argv[0]);
}


int main(int argc, char * argv[]) {
	int show_used = 0;
	int use_kilobytes = 0;
	int show_total = 0;

	if (argc > 1) {
		int index, c;
		while ((c = getopt(argc, argv, "ktu?")) != -1) {
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
					show_usage(argc, argv);
					return 0;
			}
		}
	}

	const char * unit = "kB";

	FILE * f = fopen("/proc/meminfo", "r");
	if (!f) return 1;

	int total, free, used;
	fscanf(f, "MemTotal: %d kB\nMemFree: %d kB\n", &total, &free);
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

