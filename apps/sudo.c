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
#include <termios.h>
#include <errno.h>
#include <pwd.h>
#include <dirent.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <toaru/auth.h>

#define MINUTES * 60

#define SUDO_TIME 5 MINUTES

static int sudo_loop(int (*prompt_callback)(char * username, char * password, int failures, char * argv[]), char * argv[]) {

	int fails = 0;

	struct stat buf;
	if (stat("/var/sudoers", &buf)) {
		mkdir("/var/sudoers", 0700);
	}

	while (1) {
		int need_password = 1;
		int need_sudoers  = 1;

		uid_t me = getuid();
		if (me == 0) {
			need_password = 0;
			need_sudoers  = 0;
		}

		struct passwd * p = getpwuid(me);
		if (!p) {
			fprintf(stderr, "%s: unable to obtain username for real uid=%d\n", argv[0], getuid());
			return 1;
		}
		char * username = p->pw_name;

		char token_file[64];
		sprintf(token_file, "/var/sudoers/%d", me); /* TODO: Restrict to this session? */

		if (need_password) {
			struct stat buf;
			if (!stat(token_file, &buf)) {
				/* check the time */
				if (buf.st_mtime > (SUDO_TIME) && time(NULL) - buf.st_mtime < (SUDO_TIME)) {
					need_password = 0;
				}
			}
		}

		if (need_password) {
			char * password = calloc(sizeof(char) * 1024, 1);

			if (prompt_callback(username, password, fails, argv)) {
				return 1;
			}

			int uid = toaru_auth_check_pass(username, password);

			free(password);

			if (uid < 0) {
				fails++;
				if (fails == 3) {
					fprintf(stderr, "%s: %d incorrect password attempts\n", argv[0], fails);
					return 1;
				}
				fprintf(stderr, "Sorry, try again.\n");
				continue;
			}
		}

		/* Determine if this user is in the sudoers file */
		if (need_sudoers) {
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
		}

		/* Write a timestamp file */
		FILE * f = fopen(token_file, "a");
		if (!f) {
			fprintf(stderr, "%s: (warning) failed to create token file\n", argv[0]);
		}
		fclose(f);

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

	return 0;
}

static int basic_callback(char * username, char * password, int fails, char * argv[]) {
	fprintf(stderr, "[%s] password for %s: ", argv[0], username);
	fflush(stderr);

	/* Disable echo */
	struct termios old, new;
	tcgetattr(fileno(stdin), &old);
	new = old;
	new.c_lflag &= (~ECHO);
	tcsetattr(fileno(stdin), TCSAFLUSH, &new);

	fgets(password, 1024, stdin);
	if (feof(stdin)) return 1;

	password[strlen(password)-1] = '\0';
	tcsetattr(fileno(stdin), TCSAFLUSH, &old);
	fprintf(stderr, "\n");

	return 0;
}

void usage(int argc, char * argv[]) {
	fprintf(stderr, "usage: %s [command]\n", argv[0]);
}

int main(int argc, char ** argv) {

	if (argc < 2) {
		usage(argc, argv);
		return 1;
	}

	return sudo_loop(basic_callback, argv);
}

