/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2015 Mike Gerow
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lib/testing.h"

int main(int argc, char * argv[]) {
	INFO("Starting fchdir test");
	int failed = 0;
	int fd = open("/home", 0);
	if (fd == -1) {
		perror("open(\"/home\", 0)");
		FAIL("open failed for directory");
		failed = 1;
	}
	int rv = fchdir(fd);
	if (rv == -1) {
		perror("fchdir");
		FAIL("fchdir failed");
		failed = 1;
	}
	rv = close(fd);
	if (rv == -1) {
		perror("close");
		FAIL("close failed");
		failed = 1;
	}
	char buf[4096];
	if (getcwd(buf, sizeof(buf)) == NULL) {
		perror("getcwd");
		FAIL("getcwd failed");
		failed = 1;
	}
	if (strcmp(buf, "/home") != 0) {
		FAIL("cwd does not match -- expected \"/home\", got \"%s\"", buf);
		failed = 1;
	}

	if (failed) {
		FAIL("test-fchdir failed");
		exit(EXIT_FAILURE);
	}

	INFO("test-fchdir passed");
	exit(EXIT_SUCCESS);
}
