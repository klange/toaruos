/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014 Kevin Lange
 *
 * sudo
 *
 */

#include <stdio.h>
#include <stdint.h>
#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <termios.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/utsname.h>

#include "lib/toaru_auth.h"

uint32_t child = 0;

void usage(int argc, char * argv[]) {
	fprintf(stderr, "usage: %s [command]\n", argv[0]);
}

int main(int argc, char ** argv) {

	int fails = 0;

	if (argc < 2) {
		usage(argc, argv);
		return 1;
	}

	while (1) {
		/*
		 * This is not very secure, but I'm lazy and just want this to exist.
		 * It's not like we have file system permissions or anything like
		 * that sitting around anyway... So, XXX: make this not dumb.
		 */
		char * username = getenv("USER");
		char * password = malloc(sizeof(char) * 1024);

		fprintf(stdout, "[%s] password for %s: ", argv[0], username);
		fflush(stdout);

		/* Disable echo */
		struct termios old, new;
		tcgetattr(fileno(stdin), &old);
		new = old;
		new.c_lflag &= (~ECHO);
		tcsetattr(fileno(stdin), TCSAFLUSH, &new);

		fgets(password, 1024, stdin);
		password[strlen(password)-1] = '\0';
		tcsetattr(fileno(stdin), TCSAFLUSH, &old);
		fprintf(stdout, "\n");

		int uid = toaru_auth_check_pass(username, password);

		if (uid < 0) {
			fails++;
			if (fails == 3) {
				fprintf(stderr, "%s: %d incorrect password attempts\n", argv[0], fails);
				break;
			}
			fprintf(stderr, "Sorry, try again.\n");
			continue;
		}

		char ** args = &argv[1];
		int i = execvp(args[0], args);

		/* XXX: There are other things that can cause an exec to fail. */
		fprintf(stderr, "%s: %s: command not found\n", argv[0], args[0]);
		return 1;
	}

	return 1;
}
