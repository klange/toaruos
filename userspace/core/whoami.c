/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2014 Kevin Lange
 *
 * Who Am I?
 *
 */
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pwd.h>

int main(int argc, char ** argv) {
	struct passwd * p = getpwuid(getuid());
	if (!p) return 0;

	fprintf(stdout, "%s\n", p->pw_name);

	endpwent();

	return 0;
}

