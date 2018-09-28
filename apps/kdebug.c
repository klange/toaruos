/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014-2018 K. Lange
 *
 * kdebug - Launch kernel shell
 */
#include <syscall.h>
#include <sys/wait.h>

int main(int argc, char * argv[]) {
	syscall_system_function(7, NULL);
	int status;
	wait(&status);
	return WEXITSTATUS(status);
}
