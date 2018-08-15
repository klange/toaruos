/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2018 K. Lange
 *
 * env - Print environment
 *
 * Prints all the environment values.
 */
#include <stdio.h>
#include <unistd.h>

int main(int argc, char ** argv) {
	char ** env = environ;

	while (*env) {
		printf("%s\n", *env);
		env++;
	}

	return 0;
}
