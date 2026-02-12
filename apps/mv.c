/**
 * @brief Move files
 *
 * Poor implementation, mostly just 'cp' and 'rm'.
 *
 * Ideally, should figure out if it can use 'rename'... and also
 * we should implement 'rename'...
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
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

	/* Try to use `rename` */
	if (rename(argv[1], argv[2]) < 0) {
		if (errno != EXDEV && errno != ENOTSUP) {
			fprintf(stderr, "%s: can not rename '%s': %s\n", argv[0], argv[1], strerror(errno));
			return 1;
		}
	} else {
		return 0;
	}

	/* Fall back to cp and rm */
	if (call((char *[]){"/bin/cp","-r",argv[1],argv[2],NULL})) return 1;
	if (call((char *[]){"/bin/rm","-r",argv[1],NULL})) return 1;
	return 0;
}
