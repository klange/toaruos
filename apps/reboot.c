/**
 * @brief (Try to) reboot the system.
 *
 * Note that only root can syscall_reboot, and this doesn't
 * do any fancy setuid stuff.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2014 K. Lange
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/reboot.h>

int main(int argc, char ** argv) {
	if (reboot(0) < 0) {
		fprintf(stderr, "%s: %s\n", argv[0], strerror(errno));
	}
	return 1;
}
