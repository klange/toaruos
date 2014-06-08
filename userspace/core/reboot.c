/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2014 Kevin Lange
 */
/*
 * reboot
 *
 * Reboot the system.
 */
#include <stdio.h>
#include <syscall.h>

int main(int argc, char ** argv) {
	if (syscall_reboot() < 0) {
		printf("%s: permission denied\n", argv[0]);
	}
	return 1;
}
