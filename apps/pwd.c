/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * pwd
 */
#include <unistd.h>
#include <stdio.h>

int main(int argc, char * argv[]) {
	char tmp[1024];
	if (getcwd(tmp, 1023)) {
		puts(tmp);
		return 0;
	} else {
		return 1;
	}
}
