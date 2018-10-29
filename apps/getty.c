/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * getty - Manage a TTY.
 *
 * Wraps a serial port (or other dumb connection) with a pty
 * and manages a login for it.
 */
#include <stdio.h>
#include <stdlib.h>
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

	system("ttysize -q");

	char * tokens[] = {"/bin/login",NULL,NULL,NULL};

	if (user) {
		tokens[1] = "-f";
		tokens[2] = user;
	}

	execvp(tokens[0], tokens);
	exit(1);
}
