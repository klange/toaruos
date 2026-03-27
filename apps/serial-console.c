/**
 * @brief serial console
 *
 * Old tool for poking serial ports. Probably doesn't work right
 * anymore since serial ports are now behind PTYs.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2014 K. Lange
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <termios.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/fswait.h>

int fd = 0;

int keep_echo = 0;
int dos_lines = 0;
int keep_canon = 0;

struct termios old;

void set_unbuffered() {
	tcgetattr(fileno(stdin), &old);
	struct termios new = old;
	if (!keep_canon) {
		new.c_lflag &= (~ICANON);
	}
	if (!keep_echo) {
		new.c_lflag &= (~ECHO);
	}
	tcsetattr(fileno(stdin), TCSAFLUSH, &new);
}

void set_buffered() {
	tcsetattr(fileno(stdin), TCSAFLUSH, &old);
}

int show_usage(int argc, char * argv[]) {
#define X_S "\033[3m"
#define X_E "\033[0m"
	fprintf(stderr,
			"Serial client.\n"
			"\n"
			"usage: %s [-e] [-r] [-c] [" X_S "DEVICE" X_E "]\n"
			"\n"
			" Type ^] to enter command prompt.\n"
			"\n"
			" -e     " X_S "keep echo enabled" X_E "\n"
			" -c     " X_S "keep canon enabled" X_E "\n"
			" -r     " X_S "transform line feeds to \\r\\n" X_E "\n"
			" -?     " X_S "show this help text" X_E "\n"
			"\n", argv[0]);
	return 1;
}

int main(int argc, char ** argv) {

	char * device = "/dev/ttyS0";
	int opt;

	while ((opt = getopt(argc, argv, "erc?")) != -1) {
		switch (opt) {
			case 'e':
				keep_echo = 1;
				break;
			case 'r':
				dos_lines = 1;
				break;
			case 'c':
				keep_canon = 1;
				break;
			case '?':
				return show_usage(argc, argv);
		}
	}

	if (optind < argc) {
		device = argv[optind];
	}

	if (optind + 1 < argc) {
		/* Too many arguments */
		return show_usage(argc, argv);
	}

	set_unbuffered();

	fd = open(device, O_RDWR);

	if (fd < 0) {
		fprintf(stderr, "%s: %s: %s\n", argv[0], device, strerror(errno));
		return 1;
	}

	if (!isatty(fd)) {
		fprintf(stderr, "%s: %s: not a tty\n", argv[0], device);
		return 1;
	}

	int fds[2] = {STDIN_FILENO, fd};

	while (1) {
		int index = fswait(2, fds);

		if (index == -1) {
			fprintf(stderr, "%s: fswait: erroneous file descriptor\n", argv[0]);
			fprintf(stderr, "%s: (did you try to open a file that isn't a serial console?\n", argv[0]);
			return 1;
		}

		if (index == 0) {
			char c = fgetc(stdin);
			if (c == 0x1D) { /* ^] */
				while (1) {
					printf("serial-console> ");
					set_buffered();
					fflush(stdout);

					char line[1024];
					fgets(line, 1024, stdin);

					if (feof(stdin)) {
						return 0;
					}

					int i = strlen(line);
					line[i-1] = '\0';

					if (!strcmp(line, "quit")) {
						return 0;
					} else if (!strcmp(line, "continue")) {
						set_unbuffered();
						fflush(stdout);
						break;
					} else {
						fprintf(stderr, "%s: unknown command\n", line);
						continue;
					}
				}
			} else {
				if (dos_lines && c == '\n') {
					char buf[1] = {'\r'};
					write(fd, buf, 1);
				}
				char buf[1] = {c};
				write(fd, buf, 1);
			}
		} else {
			char buf[1024];
			size_t r = read(fd, buf, 1024);
			fwrite(buf, 1, r, stdout);
			fflush(stdout);
		}
	}

	close(fd);
	set_buffered();
	return 0;
}

