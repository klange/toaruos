/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2018 K. Lange
 *
 * cat - Concatenate files
 *
 * Concatenates files together to standard output.
 * In a supporting terminal, you can then pipe
 * standard out to another file or other useful
 * things like that.
 */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#define CHUNK_SIZE 4096

void doit(int fd) {
	while (1) {
		char buf[CHUNK_SIZE];
		memset(buf, 0, CHUNK_SIZE);
		ssize_t r = read(fd, buf, CHUNK_SIZE);
		if (!r) return;
		write(STDOUT_FILENO, buf, r);
	}
}

int main(int argc, char ** argv) {
	int ret = 0;
	if (argc == 1) {
		doit(0);
	}

	for (int i = 1; i < argc; ++i) {
		if (!strcmp(argv[i],"-")) {
			doit(0);
			continue;
		}
		int fd = open(argv[i], O_RDONLY);
		if (fd < 0) {
			fprintf(stderr, "%s: %s: %s\n", argv[0], argv[i], strerror(errno));
			ret = 1;
			continue;
		}

		struct stat _stat;
		fstat(fd, &_stat);

		if (S_ISDIR(_stat.st_mode)) {
			fprintf(stderr, "%s: %s: Is a directory\n", argv[0], argv[i]);
			close(fd);
			ret = 1;
			continue;
		}

		doit(fd);

		close(fd);
	}

	return ret;
}

