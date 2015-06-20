/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2015 Mike Gerow
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lib/testing.h"

static char test58[] =
	"0000000000000000000000000000000000000000000000000000000000";
static char test59[] =
	"00000000000000000000000000000000000000000000000000000000000";
static char test60[] =
	"000000000000000000000000000000000000000000000000000000000000";
static char test61[] =
	"0000000000000000000000000000000000000000000000000000000000000";

int main(int argc, char * argv[]) {
	INFO("Starting symlink test");
	if (symlink(test58, "/home/root/test58") < 0) {
		perror("symlink(/home/root/test58)");
	}
	if (symlink(test59, "/home/root/test59") < 0) {
		perror("symlink(/home/root/test59)");
	}
	if (symlink(test60, "/home/root/test60") < 0) {
		perror("symlink(/home/root/test60)");
	}
	if (symlink(test61, "/home/root/test61") < 0) {
		perror("symlink(/home/root/test61)");
	}

	char buf[128];
	int failed = 0;

	if (readlink("/home/root/test58", buf, sizeof(buf)) < 0) {
		perror("readlink(/home/root/test58)");
	}
	if (strcmp(buf, test58) != 0) {
		FAIL("Link sized 58 is wrong");
		failed = 1;
	}
	if (readlink("/home/root/test59", buf, sizeof(buf)) < 0) {
		perror("readlink(/home/root/test59)");
	}
	if (strcmp(buf, test59) != 0) {
		FAIL("Link sized 59 is wrong");
		failed = 1;
	}
	if (readlink("/home/root/test60", buf, sizeof(buf)) < 0) {
		perror("readlink(/home/root/test60)");
	}
	if (strcmp(buf, test60) != 0) {
		FAIL("Link sized 60 is wrong");
		failed = 1;
	}
	if (readlink("/home/root/test61", buf, sizeof(buf)) < 0) {
		perror("readlink(/home/root/test61)");
	}
	if (strcmp(buf, test61) != 0) {
		FAIL("Link sized 61 is wrong");
		failed = 1;
	}

	if (failed) {
		exit(EXIT_FAILURE);
	}

	exit(EXIT_SUCCESS);
}
