/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * DUMMY mv implementation that calls cp + rm
 *
 * TODO: Actually implement the plumbing for mv!
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

static int call(char * args[]) {
	pid_t pid = fork();
	if (!pid) {
		execvp(args[0], args);
		exit(1);
	} else {
		int status;
		waitpid(pid, &status, 0);
		return status;
	}
}

int main(int argc, char * argv[]) {
	if (argc < 3) {
		fprintf(stderr, "%s: missing operand\n", argv[0]);
		return 1;
	}
	if (!strcmp(argv[1], argv[2])) {
		fprintf(stderr, "%s: %s and %s are the same file\n", argv[0], argv[1], argv[2]);
		return 1;
	}
	/* TODO stat magic for other ways to reference the same file */
	if (call((char *[]){"/bin/cp",argv[1],argv[2],NULL})) return 1;
	if (call((char *[]){"/bin/rm",argv[1],NULL})) return 1;
	return 0;
}
