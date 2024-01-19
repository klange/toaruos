/**
 * @brief getty - Manage a TTY.
 *
 * Wraps a serial port (or other dumb connection) with a pty
 * and manages a login for it.
 *
 * This is not really what 'getty' should do - see @ref login-loop.c
 * for something more akin to the "getty" on other platforms.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pty.h>
#include <sys/wait.h>
#include <sys/fswait.h>

int main(int argc, char * argv[]) {
	int fd_serial;
	char * file = "/dev/ttyS0";
	char * user = NULL;

	if (getuid() != 0) {
		fprintf(stderr, "%s: only root can do that\n", argv[0]);
		return 1;
	}

	int opt;
	while ((opt = getopt(argc, argv, "a:")) != -1) {
		switch (opt) {
			case 'a':
				user = optarg;
				break;
		}
	}

	if (optind < argc) {
		file = argv[optind];
		optind++;
	}

	fd_serial = open(file, O_RDWR);

	if (fd_serial < 0) {
		perror("open");
		return 1;
	}

	setsid();
	dup2(fd_serial, 0);
	dup2(fd_serial, 1);
	dup2(fd_serial, 2);
	ioctl(STDIN_FILENO, TIOCSCTTY, &(int){1});
	tcsetpgrp(STDIN_FILENO, getpid());

	system("stty sane");

	if (optind < argc) {
		if (*argv[optind] >= '0' && *argv[optind] <= '9' && strlen(argv[optind]) < 30) {
			char tmp[100];
			snprintf(tmp, 100, "stty %s", argv[optind]);
			system(tmp);
			optind++;
		}
	}

	if (optind < argc) {
		/* If there's still arguments, assume TERM value */
		setenv("TERM", argv[optind], 1);
		optind++;
	}

	system("ttysize -q");

	char * tokens[] = {"/bin/login",NULL,NULL,NULL};

	if (user) {
		tokens[1] = "-f";
		tokens[2] = user;
	}

	execvp(tokens[0], tokens);
	exit(1);
}
