/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * write-out - write stdin into stdout until eof
 *
 * Not really necessary any more. This existed before the shell
 * had support for >output.
 */
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

int main(int argc, char * argv[]) {
	int fd = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0666);

	while (1) {
		char buf[1024];
		size_t r = read(0, buf, 1024);
		if (r == 0) break;
		write(fd, buf, r);
	}

}
