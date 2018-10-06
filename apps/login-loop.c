/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * login-loop - Continuously call `login`
 *
 * Used by the VGA terminal to provide interactive login sessions.
 */
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

int main(int argc, char * argv[]) {
	while (1) {
		pid_t f = fork();
		if (!f) {
			char * args[] = {
				"login",
				NULL
			};
			execvp(args[0], args);
		} else {
			int result;
			do {
				result = waitpid(f, NULL, 0);
			} while (result < 0);
		}
	}

	return 1;
}
