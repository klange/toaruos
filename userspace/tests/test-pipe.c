/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013 Kevin Lange
 */
/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * pipe test
 *
 * Makes a pipe. Pipes stuff to it. Yeah.
 */

#include <stdio.h>
#include <stdint.h>
#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char ** argv) {
	int fd = syscall_mkpipe();
	printf("%d <- pipe\n", fd);
	int pid = getpid();
	uint32_t f = fork();
	if (getpid() != pid) {
		char buf[512];
		int r = read(fd, buf, 13);
		printf("[%d] %s\n", r, buf);
		return 0;
	} else {
		char * buf = "Hello world!";
		write(fd, buf, strlen(buf) + 1);
		return 0;
	}

	return 0;
}
