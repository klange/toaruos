/**
 * @brief taskset - control CPU affinity
 *
 * @copyright
 * This file is pa rt of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2026 K. Lange
 */
#define _TOARU_SOURCE
#include <unistd.h>
#include <sched.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>

static int show_usage(char * argv[]) {
	fprintf(stderr,
		"usage: %s mask command [argument...]\n"
		"       %s -p [mask] pid\n",
		argv[0], argv[0]);
	return 1;
}

static uint64_t read_mask(const char * arg) {
	return strtoul(arg, NULL, 16);
}

int main(int argc, char * argv[]) {
	uint64_t mask;
	int pid_mode = 0;
	int opt;

	while ((opt = getopt(argc, argv, "+p")) != -1) {
		switch (opt) {
			case 'p':
				pid_mode = 1;
				break;
			default:
				return show_usage(argv);
		}
	}

	if (pid_mode) {
		if (optind == argc || optind + 2 < argc) return show_usage(argv);
		pid_t p = atoi(argv[argc-1]);
		if (sched_getaffinity_t(p, &mask) < 0) err(1, "sched_getaffinity");
		printf("pid %d's current affinity mask: %zx\n", p, mask);
		if (optind + 1 < argc) {
			/* Set */
			mask = read_mask(argv[optind]);
			if (sched_setaffinity_t(p, mask) < 0) err(1, "failed to set pid %d's affinity", p);
			if (sched_getaffinity_t(p, &mask) < 0) err(1, "sched_getaffinity");
			printf("pid %d's new affinity mask: %zx\n", p, mask);
		}
	} else {
		if (optind + 2 > argc) return show_usage(argv);
		mask = read_mask(argv[optind]);
		if (sched_setaffinity_t(0, mask) < 0) err(1, "failed to set pid %d's affinity", getpid());
		if (execvp(argv[optind+1], &argv[optind+1])) err(1, "execvp");
	}

	return 0;
}
