/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2014 Kevin Lange
 */
/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * hostname
 *
 * Prints or sets the system hostname.
 */
#include <stdio.h>
#include <syscall.h>

#define ROOT_UID 0

int main(int argc, char * argv[]) {
	if ((argc > 1 && argv[1][0] == '-') || (argc < 2)) {
		char tmp[256];
		syscall_gethostname(tmp);
		printf("%s\n", tmp);
		return 0;
	} else {
		if (syscall_getuid() != ROOT_UID) {
			fprintf(stderr,"Must be root to set hostname.\n");
			return 1;
		} else {
			syscall_sethostname(argv[1]);
			FILE * file = fopen("/etc/hostname", "w");
			if (!file) {
				return 1;
			} else {
				fprintf(file, "%s\n", argv[1]);
				fclose(file);
				return 0;
			}
		}
	}
	return 0;
}
