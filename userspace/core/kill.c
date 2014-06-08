/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013 Kevin Lange
 */
/*
 * kill
 *
 * Send a signal to another process
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

void usage(char * argv[]) {
	printf(
			"kill - send a signal to another process\n"
			"\n"
			"usage: %s [-\033[3mx\033[0m] \033[3mprocess\033[0m\n"
			"\n"
			" -h --help       \033[3mShow this help message.\033[0m\n"
			" -\033[3mx\033[0m              \033[3mSignal number to send\033[0m\n"
			"\n",
			argv[0]);
}

int main(int argc, char * argv[]) {
	int signum = SIGKILL;
	int pid = 0;

	if (argc < 2) {
		usage(argv);
		return 1;
	}

	if (argc > 2) {
		if (argv[1][0] == '-') {
			signum = atoi(argv[1]+1);
		} else {
			usage(argv);
			return 1;
		}
		pid = atoi(argv[2]);
	} else if (argc == 2) {
		pid = atoi(argv[1]);
	}

	if (pid) {
		kill(pid, signum);
	}

	return 0;
}
