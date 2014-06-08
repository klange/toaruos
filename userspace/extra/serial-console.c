/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2014 Kevin Lange
 */
/*
 * cat
 *
 * Concatenates files together to standard output.
 * In a supporting terminal, you can then pipe
 * standard out to another file or other useful
 * things like that.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <syscall.h>
#include <signal.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/wait.h>

#include "lib/pthread.h"

int fd = 0;

int child_pid = 0;

void *print_serial_stuff(void * garbage) {
	child_pid = gettid();
	while (1) {
		char x;
		read(fd, &x, 1);
		fputc(x, stdout);
		fflush(stdout);
	}

	pthread_exit(garbage);
}

struct termios old;

void set_unbuffered() {
	tcgetattr(fileno(stdin), &old);
	struct termios new = old;
	new.c_lflag &= (~ICANON & ~ECHO);
	tcsetattr(fileno(stdin), TCSAFLUSH, &new);
}

void set_buffered() {
	tcsetattr(fileno(stdin), TCSAFLUSH, &old);
}

int main(int argc, char ** argv) {
	pthread_t receive_thread;
	pthread_t flush_thread;
	char * device = argv[1];

	if (argc < 1) {
		device = "/dev/ttyS0";
	}

	set_unbuffered();

	fd = open(device, 0, 0);

	pthread_create(&receive_thread, NULL, print_serial_stuff, NULL);

	while (1) {
		char c = fgetc(stdin);
		if (c == 27) {
			char x = fgetc(stdin);
			if (x == ']') {
				while (1) {
					printf("serial-console>\033[1561z ");
					set_buffered();
					fflush(stdout);

					char line[1024];
					fgets(line, 1024, stdin);

					int i = strlen(line);
					line[i-1] = '\0';

					if (!strcmp(line, "quit")) {
						kill(child_pid, SIGKILL);
						printf("Waiting for threads to shut down...\n");
						while (wait(NULL) != -1);
						printf("Exiting.\n");
						return 0;
					} else if (!strcmp(line, "continue")) {
						set_unbuffered();
						fflush(stdout);
						break;
					}
				}
			} else {
				ungetc(x, stdin);
			}
		}
		char buf[1] = {c};
		write(fd, buf, 1);
	}

	close(fd);
	set_buffered();
	return 0;
}

/*
 * vim:tabstop=4
 * vim:noexpandtab
 * vim:shiftwidth=4
 */
