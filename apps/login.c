/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2014 K. Lange
 *
 * Login Service
 *
 * Provides the user with a login prompt and starts their session.
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

#define LINE_LEN 1024

uint32_t child = 0;

void sig_pass(int sig) {
	/* Pass onto the shell */
	if (child) {
		kill(child, sig);
	}
	/* Else, ignore */
}

void sig_segv(int sig) {
	printf("Segmentation fault.\n");
	exit(127 + sig);
	/* no return */
}

int main(int argc, char ** argv) {

	char * user = NULL;
	int uid;
	pid_t pid, f;

	int opt;
	while ((opt = getopt(argc, argv, "f:")) != -1) {
		switch (opt) {
			case 'f':
				user = optarg;
				break;
		}
	}

	if (user) {
		struct passwd * pw = getpwnam(user);
		if (pw) {
			uid = pw->pw_uid;
			goto do_fork;
		} else {
			fprintf(stderr, "%s: no such user\n", argv[0]);
			return 1;
		}
	}

	printf("\n");
	system("uname -a");
	printf("\n");

	signal(SIGINT, sig_pass);
	signal(SIGWINCH, sig_pass);
	signal(SIGSEGV, sig_segv);

	while (1) {

		char username[1024] = {0};
		char password[1024] = {0};

		/* TODO: gethostname() */
		char _hostname[256];
		gethostname(_hostname, 255);

		fprintf(stdout, "%s login: ", _hostname);
		fflush(stdout);
		char * r = fgets(username, 1024, stdin);
		if (!r) {
			clearerr(stdin);
			fprintf(stderr, "\n");
			sleep(2);
			fprintf(stderr, "\nLogin failed.\n");
			continue;
		}
		username[strlen(username)-1] = '\0';

		if (!strcmp(username, "reboot")) {
			/* Quick hack so vga text mode login can exit */
			system("reboot");
		}

		fprintf(stdout, "password: ");
		fflush(stdout);

		/* Disable echo */
		struct termios old, new;
		tcgetattr(fileno(stdin), &old);
		new = old;
		new.c_lflag &= (~ECHO);
		tcsetattr(fileno(stdin), TCSAFLUSH, &new);

		r = fgets(password, 1024, stdin);
		if (!r) {
			clearerr(stdin);
			tcsetattr(fileno(stdin), TCSAFLUSH, &old);
			fprintf(stderr, "\n");
			sleep(2);
			fprintf(stderr, "\nLogin failed.\n");
			continue;
		}
		password[strlen(password)-1] = '\0';
		tcsetattr(fileno(stdin), TCSAFLUSH, &old);
		fprintf(stdout, "\n");

		uid = toaru_auth_check_pass(username, password);

		if (uid < 0) {
			sleep(2);
			fprintf(stdout, "\nLogin failed.\n");
			continue;
		}

		break;
	}

	system("cat /etc/motd");

do_fork:
	pid = getpid();
	f = fork();
	if (getpid() != pid) {
		setuid(uid);
		toaru_auth_set_vars();
		char * args[] = {
			getenv("SHELL"),
			NULL
		};
		execvp(args[0], args);
		return 1;
	} else {
		child = f;
		int result;
		do {
			result = waitpid(f, NULL, 0);
		} while (result < 0);
	}
	child = 0;

	return 0;
}
