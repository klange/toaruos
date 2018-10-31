/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2014 K. Lange
 *
 * serial console
 *
 * Runs a dumb console on a serial port or something similar.
 *
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <termios.h>
#include <fcntl.h>
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
	printf(
			"Serial client.\n"
			"\n"
			"usage: %s [-e] [-r] [-c] [device path]\n"
			"\n"
			" -e     \033[3mkeep echo enabled\033[0m\n"
			" -c     \033[3mkeep canon enabled\033[0m\n"
			" -r     \033[3mtransform line feeds to \\r\\n\033[0m\n"
			" -?     \033[3mshow this help text\033[0m\n"
			"\n", argv[0]);
	return 1;
}

int main(int argc, char ** argv) {

	int arg = 1;
	char * device;

	while (arg < argc) {
		if (argv[arg][0] != '-') break;
		if (!strcmp(argv[arg], "-e")) {
			keep_echo = 1;
		} else if (!strcmp(argv[arg], "-r")) {
			dos_lines = 1;
		} else if (!strcmp(argv[arg], "-c")) {
			keep_canon = 1;
		} else if (!strcmp(argv[arg], "-?")) {
			return show_usage(argc, argv);
		} else {
			fprintf(stderr, "%s: Unrecognized option: %s\n", argv[0], argv[arg]);
		}
		arg++;
	}

	if (arg == argc) {
		device = "/dev/ttyS0";
	} else {
		device = argv[arg];
	}

	set_unbuffered();

	fd = open(device, 0, 0);

	int fds[2] = {STDIN_FILENO, fd};

	while (1) {
		int index = fswait(2, fds);

		if (index == -1) {
			fprintf(stderr, "serial-console: fswait: erroneous file descriptor\n");
			fprintf(stderr, "serial-console: (did you try to open a file that isn't a serial console?\n");
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

