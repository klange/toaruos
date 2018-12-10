/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014-2018 K. Lange
 *
 * kdebug - Launch kernel shell
 */
#include <errno.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/sysfunc.h>

int main(int argc, char * argv[]) {
	sysfunc(TOARU_SYS_FUNC_KDEBUG, NULL);
	int status;
	while (wait(&status)) {
		if (errno == ECHILD) break;
	}
	return WEXITSTATUS(status);
}
