/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013 Kevin Lange
 */
/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * uname
 *
 * Prints the kernel version information.
 */
#include <stdio.h>
#include <syscall.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/utsname.h>

#define FLAG_SYSNAME  0x01
#define FLAG_NODENAME 0x02
#define FLAG_RELEASE  0x04
#define FLAG_VERSION  0x08
#define FLAG_MACHINE  0x10

#define FLAG_ALL (FLAG_SYSNAME|FLAG_NODENAME|FLAG_RELEASE|FLAG_VERSION|FLAG_MACHINE)

#define _ITALIC "\033[3m"
#define _END    "\033[0m\n"

void show_usage(int argc, char * argv[]) {
	fprintf(stderr,
			"uname - Print system version information.\n"
			"\n"
			"usage: %s [-asnrvm]\n"
			"\n"
			" -a     " _ITALIC "Print the standard uname string we all love" _END
			" -s     " _ITALIC "Print kernel name" _END
			" -n     " _ITALIC "Print system name" _END
			" -r     " _ITALIC "Print kernel version number" _END
			" -v     " _ITALIC "Print the extra kernel version information" _END
			" -m     " _ITALIC "Print the architecture name" _END
			"\n", argv[0]);
	exit(1);
}

int main(int argc, char * argv[]) {
	struct utsname u;

	int c;
	int flags = 0;
	int space = 0;

	while ((c = getopt(argc, argv, "ahmnrsv")) != -1) {
		switch (c) {
			case 'a':
				flags |= FLAG_ALL;
				break;
			case 's':
				flags |= FLAG_SYSNAME;
				break;
			case 'n':
				flags |= FLAG_NODENAME;
				break;
			case 'r':
				flags |= FLAG_RELEASE;
				break;
			case 'v':
				flags |= FLAG_VERSION;
				break;
			case 'm':
				flags |= FLAG_MACHINE;
				break;
			case 'h':
			default:
				show_usage(argc, argv);
				break;
		}
	}

	uname(&u);

	if (!flags) {
		/* By default, we just print the kernel name */
		flags = FLAG_SYSNAME;
	}

	if (flags & FLAG_SYSNAME) {
		if (space++) printf(" ");
		printf("%s", u.sysname);
	}

	if (flags & FLAG_NODENAME) {
		if (space++) printf(" ");
		printf("%s", u.nodename);
	}

	if (flags & FLAG_RELEASE) {
		if (space++) printf(" ");
		printf("%s", u.release);
	}

	if (flags & FLAG_VERSION) {
		if (space++) printf(" ");
		printf("%s", u.version);
	}

	if (flags & FLAG_MACHINE) {
		if (space++) printf(" ");
		printf("%s", u.machine);
	}

	printf("\n");

	return 0;
}
