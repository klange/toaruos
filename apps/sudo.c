/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014 K. Lange
 *
 * sudo
 *
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <termios.h>
#include <errno.h>
#include <pwd.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <toaru/auth.h>

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
		uid_t me = getuid();
		if (me == 0) goto _do_it;

		struct passwd * p = getpwuid(me);
		if (!p) {
			fprintf(stderr, "%s: unable to obtain username for real uid=%d\n", argv[0], getuid());
			return 1;
		}
		char * username = p->pw_name;
		char * password = malloc(sizeof(char) * 1024);

		fprintf(stderr, "[%s] password for %s: ", argv[0], username);
		fflush(stderr);

		/* Disable echo */
		struct termios old, new;
		tcgetattr(fileno(stdin), &old);
		new = old;
		new.c_lflag &= (~ECHO);
		tcsetattr(fileno(stdin), TCSAFLUSH, &new);

		fgets(password, 1024, stdin);
		password[strlen(password)-1] = '\0';
		tcsetattr(fileno(stdin), TCSAFLUSH, &old);
		fprintf(stderr, "\n");

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

		/* Determine if this user is in the sudoers file */
		FILE * sudoers = fopen("/etc/sudoers","r");
		if (!sudoers) {
			fprintf(stderr, "%s: /etc/sudoers is not available\n", argv[0]);
			return 1;
		}

		/* Read each line */
		int in_sudoers = 0;
		while (!feof(sudoers)) {
			char line[1024];
			fgets(line, 1024, sudoers);
			char * nl = strchr(line, '\n');
			if (nl) {
				*nl = '\0';
			}
			if (!strncmp(line,username,1024)) {
				in_sudoers = 1;
				break;
			}
		}
		fclose(sudoers);

		if (!in_sudoers) {
			fprintf(stderr, "%s is not in sudoers file.\n", username);
			return 1;
		}

_do_it:
		/* Set username to root */
		putenv("USER=root");

		/* Actually become root, so real user id = 0 */
		setuid(0);

		if (!strcmp(argv[1], "-s")) {
			argv[1] = getenv("SHELL");
		}

		char ** args = &argv[1];
		execvp(args[0], args);

		/* XXX: There are other things that can cause an exec to fail. */
		fprintf(stderr, "%s: %s: command not found\n", argv[0], args[0]);
		return 1;
	}

	return 1;
}
