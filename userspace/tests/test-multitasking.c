/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013 Kevin Lange
 */
/* vim: tabstop=4 noexpandtab shiftwidth=4
 *
 * Multitasking Thrasher
 *
 * Useful for testing that multitasking still works.
 * Starts up a bunch of threads, lets them do stuff,
 * cleans them up, etc.
 *
 */

#include <stdio.h>
#include <stdint.h>
#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char ** argv) {
	int nthreads = 2, base_pid = getpid(), npid = -1;

	if (argc > 1) {
		/* Read some arguments */
		int c;
		while ((c = getopt(argc, argv, "n:")) != -1) {
			switch (c) {
				case 'n':
					nthreads = atoi(optarg);
					break;
				default:
					break;
			}
		}
	}

	printf("I am pid %d\n", base_pid);
	printf("Starting %d threads.\n", nthreads);

	for (int i = 0; i < nthreads; ++i) {
		int pid = fork();
		if (!pid) {
			while (1) {
				printf("%c", i + 'A');
				fflush(stdout);
			}
		} else {
			npid = pid;
		}
	}

	printf("Done.\n");

	return 0;
}
