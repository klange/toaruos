/**
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2026 K. Lange
 */
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/wait.h>

static int usage(int argc, char * argv[]) {
	fprintf(stderr, "usage: %s cmd...\n", argv[0]);
	return 1;
}

static void time_diff(struct timeval * start, struct timeval * end, int *minutes, time_t *sec_diff, suseconds_t * usec_diff) {
	*sec_diff = end->tv_sec - start->tv_sec;
	*usec_diff = end->tv_usec - start->tv_usec;
	if (end->tv_usec < start->tv_usec) {
		*sec_diff -= 1;
		*usec_diff = (1000000 + end->tv_usec) - start->tv_usec;
	}

	*minutes = *sec_diff / 60;
	*sec_diff = *sec_diff % 60;
}

int main(int argc, char * argv[]) {
	int opt;

	while ((opt = getopt(argc, argv, "+p")) != -1) {
		switch (opt) {
			case 'p':
				/* ignored for compatibility, we use this format normally. */
				break;
			default:
				return usage(argc, argv);
		}
	}

	struct timeval start, end;
	gettimeofday(&start, NULL);

	struct rusage before, after;
	getrusage(RUSAGE_CHILDREN, &before);

	int ret_code = 0;

	if (optind != argc) {
		pid_t pid;
		pid_t child_pid = fork();
		if (!child_pid) {
			execvp(argv[optind], &argv[optind]);
			_Exit(255);
		}

		signal(SIGINT,  SIG_IGN);
		signal(SIGQUIT, SIG_IGN);

		do {
			pid = waitpid(child_pid, &ret_code, 0);
		} while (pid != -1 || (pid == -1 && errno != ECHILD));
	}

	gettimeofday(&end, NULL);
	getrusage(RUSAGE_CHILDREN, &after);

	int minutes;
	time_t sec_diff;
	suseconds_t usec_diff;

	time_diff(&start, &end, &minutes, &sec_diff, &usec_diff);
	fprintf(stderr, "\nreal\t%dm%d.%.03ds\n", minutes, (int)sec_diff, (int)(usec_diff / 1000));

	time_diff(&before.ru_utime, &after.ru_utime, &minutes, &sec_diff, &usec_diff);
	fprintf(stderr, "user\t%dm%d.%.03ds\n", minutes, (int)sec_diff, (int)(usec_diff / 1000));
	time_diff(&before.ru_stime, &after.ru_stime, &minutes, &sec_diff, &usec_diff);
	fprintf(stderr, "sys\t%dm%d.%.03ds\n", minutes, (int)sec_diff, (int)(usec_diff / 1000));

	return WEXITSTATUS(ret_code);
}
