/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014 Kevin Lange
 */
#include <syscall.h>
#include <sys/wait.h>

int main(int argc, char * argv[]) {
	int pid = syscall_system_function(7, NULL);
	int status;
	wait(&status);
	return status;
}
