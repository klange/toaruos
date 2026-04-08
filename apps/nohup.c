/**
 * @brief nohup - Ignore SIGHUP and redirect terminals
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2026 K. Lange
 */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <errno.h>

#define NOHUP_EXEC_FAILURE     126
#define NOHUP_INTERNAL_FAILURE 127

enum DoStuff {
	Redirect_stdout = 1,
	Redirect_stderr = 2,
	Redirect_stdin  = 4,
};

int main(int argc, char ** argv) {
	enum DoStuff do_what = 0;

	int opt;
	while ((opt = getopt(argc, argv, "")) != -1); /* there are no options */
	if (optind == argc) {
		fprintf(stderr, "usage: %s [--] cmd [args]\n", argv[0]);
		return NOHUP_INTERNAL_FAILURE;
	}

	if (isatty(STDIN_FILENO)) do_what |= Redirect_stdin;
	if (isatty(STDOUT_FILENO)) do_what |= Redirect_stdout;
	if (isatty(STDERR_FILENO)) do_what |= Redirect_stderr;

	char * file = NULL;

	if (do_what & Redirect_stdin) {
		int new_stdin = open("/dev/null",O_WRONLY);
		if (new_stdin < 0) {
			fprintf(stderr, "%s: can not redirect stdin to /dev/null: %s\n", argv[0], strerror(errno));
			return NOHUP_INTERNAL_FAILURE;
		}
		dup2(new_stdin, STDIN_FILENO);
	}

	if (do_what & Redirect_stdout) {
		asprintf(&file, "nohup.out");
		int new_stdout = open(file,O_APPEND|O_WRONLY|O_CREAT,0644);
		if (new_stdout < 0) {
			if (getenv("HOME")) {
				free(file);
				asprintf(&file, "%s/nohup.out", getenv("HOME"));
				new_stdout = open(file,O_APPEND|O_WRONLY|O_CREAT,0644);
			}
			if (new_stdout < 0) {
				fprintf(stderr, "%s: %s: %s\n", argv[0], file, strerror(errno));
				return NOHUP_INTERNAL_FAILURE;
			}
		}

		dup2(new_stdout, STDOUT_FILENO);
	}

	if ((do_what & Redirect_stdin)  && (do_what & Redirect_stdout)) {
		fprintf(stderr, "%s: ignoring input and appending output to '%s'\n", argv[0], file);
	} else if ((do_what & Redirect_stdin) && (do_what & Redirect_stderr)) {
		fprintf(stderr, "%s: ignoring input and redirecting stderr to stdout\n", argv[0]);
	} else if ((do_what & Redirect_stdin)) {
		fprintf(stderr, "%s: ignoring input\n", argv[0]);
	} else if ((do_what & Redirect_stdout)) {
		fprintf(stderr, "%s: appending output to '%s'\n", argv[0], file);
	} else if ((do_what & Redirect_stderr)) {
		fprintf(stderr, "%s: redirecting stderr to stdout\n", argv[0]);
	}

	if (file) free(file);

	if (do_what & Redirect_stderr) {
		dup2(STDOUT_FILENO,STDERR_FILENO);
	}

	signal(SIGHUP, SIG_IGN);

	execvp(argv[optind], &argv[optind]);
	int failure = (errno == ENOENT) ? NOHUP_INTERNAL_FAILURE : NOHUP_EXEC_FAILURE;
	fprintf(stderr, "%s: %s: %s\n", argv[0], argv[optind], strerror(errno));
	return failure;
}

